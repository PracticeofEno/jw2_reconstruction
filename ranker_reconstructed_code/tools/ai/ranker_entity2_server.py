# -*- coding: utf-8 -*-
"""act3 / ENTCMD02 entity-command policy server (plan sections 12-14).

Serves the binary RAI3 protocol to `ranker_rebuild.exe -AIACT3:PORT`: one
connection per game process, HELLO/ACK pinning the policy version, then per
owner ACT_REQ -> ACT_REPLY -> OUTCOME at the 8-frame cadence and one TERMINAL
per controlled owner.  Transition key = (connection, episode, owner,
sequence); a transition is trainable only after its OUTCOME arrived.

Parallel collector, single learner (plan 14.1): W game processes form a
cohort; all rollouts are collected under one immutable policy version, the
PPO update runs once at the cohort barrier, the checkpoint is replaced
atomically and `READY <completed>` is printed for the launcher.

    python ranker_entity2_server.py --port 6101 --workers 4 --train \\
        --policy entity2_bc.pt --out run/entity2.pt \\
        --environment-seed-base 100 --environment-max-frames 20000
    python ranker_entity2_server.py --selftest
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import random
import socket
import sys
import threading
import time
from typing import Dict, List, Optional, Tuple

import ranker_entity2_contract as wire

LEARNER_STATE_VERSION = 2
LEARNER_STATE_REQUIRED_KEYS = (
    "learner_state_version", "updates", "global_steps", "rollout_jobs",
    "base_seed", "environment_seed_base", "cohort_workers",
    "environment_max_frames", "learning_rate", "ppo_epochs", "optimizer",
    "update_rng_state", "policy_fingerprint", "issue_prior", "gate_kl_coef",
    "minibatch")
LEARNER_STATE_INDICATOR_KEYS = ("updates", "optimizer", "update_rng_state",
                                "rollout_jobs", "policy_fingerprint")


def validate_learner_resume_extra(extra: Optional[Dict]) -> bool:
    """True when `extra` carries full online learner state; False for a
    plain BC checkpoint; raises on a legacy/incomplete online state."""
    if not extra:
        return False
    has_indicator = any(key in extra for key in LEARNER_STATE_INDICATOR_KEYS)
    if "learner_state_version" not in extra:
        if has_indicator:
            raise RuntimeError("legacy online checkpoint without learner_state_version")
        return False
    if extra["learner_state_version"] != LEARNER_STATE_VERSION:
        raise RuntimeError("learner state version %r != %d" %
                           (extra["learner_state_version"], LEARNER_STATE_VERSION))
    missing = [key for key in LEARNER_STATE_REQUIRED_KEYS if key not in extra]
    if missing:
        raise RuntimeError("online checkpoint missing learner state: %s" % missing)
    return True


class ExclusiveFileLock:
    def __init__(self, checkpoint_path: str):
        self.path = checkpoint_path + ".lock"
        self.handle = open(self.path, "a+b")
        try:
            if os.name == "nt":
                import msvcrt
                self.handle.seek(0)
                msvcrt.locking(self.handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.handle.close()
            raise RuntimeError("checkpoint %s is locked by another learner" %
                               checkpoint_path)

    def close(self) -> None:
        if self.handle is None:
            return
        try:
            if os.name == "nt":
                import msvcrt
                self.handle.seek(0)
                msvcrt.locking(self.handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.handle.fileno(), fcntl.LOCK_UN)
        except OSError:
            pass
        self.handle.close()
        self.handle = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Transport
# ---------------------------------------------------------------------------


def recv_exact(sock: socket.socket, count: int) -> bytes:
    chunks = []
    remaining = count
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("peer closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def recv_frame(sock: socket.socket) -> Tuple[wire.Header, bytes]:
    header = wire.parse_header(recv_exact(sock, wire.HEADER_BYTES))
    payload = recv_exact(sock, header.payload_bytes) if header.payload_bytes else b""
    if wire.crc32(payload) != header.payload_crc32:
        raise wire.WireError("payload CRC mismatch")
    return header, payload


def send_frame(sock: socket.socket, header: wire.Header, payload: bytes) -> None:
    sock.sendall(wire.frame_bytes(header, payload))


def _echo_header(request: wire.Header, kind: int) -> wire.Header:
    return wire.Header(kind=kind, flags=request.flags, owner=request.owner,
                       episode=request.episode, frame=request.frame,
                       sequence=request.sequence,
                       reply_to_sequence=request.sequence,
                       own_rows=request.own_rows, target_rows=request.target_rows,
                       resource_rows=request.resource_rows,
                       build_rows=request.build_rows,
                       produce_rows=request.produce_rows,
                       research_rows=request.research_rows,
                       policy_version=request.policy_version)


# ---------------------------------------------------------------------------
# Policies
# ---------------------------------------------------------------------------


def random_slot_orders(rng: random.Random, request: Dict, keep_bias: float
                       ) -> Tuple[List[int], List[int]]:
    """Random legal commander decision: per slot KEEP with `keep_bias`,
    otherwise a uniform legal non-KEEP command and (for point commands) a
    uniform legal cell."""
    slot_commands = [0] * wire.SLOT_COUNT
    slot_cells = [-1] * wire.SLOT_COUNT
    for slot in range(wire.SLOT_COUNT):
        mask = request["slot_command_mask"][slot]
        legal = [k for k in range(1, wire.SLOT_COMMAND_COUNT) if (mask >> k) & 1]
        if not legal or rng.random() < keep_bias:
            continue
        command = rng.choice(legal)
        cell = -1
        if command in wire.SLOT_POINT_COMMANDS:
            cells = [k for k, bit in enumerate(wire.expand_bitmask(
                request["slot_cell_mask_words"][slot], wire.GLOBAL_CELL_COUNT)) if bit]
            if not cells:
                continue
            cell = rng.choice(cells)
        slot_commands[slot] = command
        slot_cells[slot] = cell
    return slot_commands, slot_cells


class RandomLegalPolicy:
    """Bit-exact legal sampler over the same-tick ledgers (no torch)."""

    def __init__(self, keep_bias: float = 0.6, base_seed: int = 1,
                 slot_keep_bias: float = 0.8, assign_keep_bias: float = 0.8):
        self.keep_bias = keep_bias
        self.slot_keep_bias = slot_keep_bias
        self.assign_keep_bias = assign_keep_bias
        self.base_seed = base_seed
        self.connection_rngs: Dict[int, random.Random] = {}

    def _rng(self, connection_id: int) -> random.Random:
        if connection_id not in self.connection_rngs:
            self.connection_rngs[connection_id] = random.Random(
                self.base_seed + 0x9E3779B1 * (connection_id + 1))
        return self.connection_rngs[connection_id]

    def act(self, connection_id: int, header: wire.Header, request: Dict) -> bytes:
        rng = self._rng(connection_id)
        ledger = wire.EconomyLedger(request, header)
        assign_ledger = wire.AssignLedger(wire.scout_free_at_snapshot(request))
        slot_commands, slot_cells = random_slot_orders(rng, request, self.slot_keep_bias)
        commands: List[int] = []
        arguments: List[int] = []
        assigns: List[int] = []
        for i in range(header.own_rows):
            assign_mask = assign_ledger.dynamic_mask(request["own_assign_mask"][i])
            assign = 0
            options = [s + 1 for s in range(wire.SLOT_COUNT) if (assign_mask >> s) & 1]
            if options and rng.random() >= self.assign_keep_bias:
                assign = rng.choice(options)
                assign_ledger.apply(assign)
            assigns.append(assign)
            dyn_cmd, dyn_pair = ledger.dynamic_masks(request["command_mask"][i],
                                                     request["economy_pair_mask_words"][i])
            legal = [k for k in range(1, wire.COMMAND_COUNT) if (dyn_cmd >> k) & 1]
            command = 0
            argument = -1
            if legal and rng.random() >= self.keep_bias:
                command = rng.choice(legal)
                if command in wire.POINT_COMMANDS:
                    points = [k for k, bit in enumerate(wire.expand_bitmask(
                        request["point_mask"][i], wire.POINT_COUNT)) if bit]
                    argument = rng.choice(points)
                elif command == wire.COMMAND_ATTACK_UNIT:
                    targets = [k for k, bit in enumerate(wire.expand_bitmask(
                        request["attack_pair_mask_words"][i], header.target_rows))
                        if bit]
                    argument = rng.choice(targets)
                elif command in wire.ECONOMY_COMMANDS:
                    kind = wire.KIND_OF_COMMAND[command]
                    cands = [k for k in range(header.candidate_rows)
                             if (dyn_pair[k >> 5] >> (k & 31)) & 1 and
                             request["candidates"][k].kind == kind]
                    argument = rng.choice(cands)
                    ledger.reserve(command, argument)
            commands.append(command)
            arguments.append(argument)
        return wire.pack_reply(commands, arguments, header, assigns, slot_commands,
                               slot_cells)

    def abort_connection(self, connection_id: int) -> None:
        self.connection_rngs.pop(connection_id, None)


# Probe-only tech hint measured from SHD3 teacher data (2026-09-04): building
# 0x84 produces the fighters 0x21/0x22; the base 0x80 only the worker 0x20.
PROBE_ARMY_BUILDING = 0x84


def _cell_distance(a: int, b: int) -> int:
    return abs((a & 7) - (b & 7)) + abs((a >> 3) - (b >> 3))


def probe_slot_orders(request: Dict, army_min: int = 4) -> Tuple[List[int], List[int], Dict]:
    """Deterministic commander probe: once MAIN holds `army_min` members
    and has no live order (or it ended), ATTACK_MOVE towards the nearest
    legal cell to an unexplored non-own start candidate (all explored ->
    cycle the non-own candidates by age); SCOUT MOVEs to unexplored
    candidates.  Returns (slot_commands, slot_cells, info)."""
    slot_commands = [0] * wire.SLOT_COUNT
    slot_cells = [-1] * wire.SLOT_COUNT
    info: Dict = {}
    starts = [c for c in request["start_candidates"] if c.cell >= 0 and not c.is_own]
    unexplored = [c.cell for c in starts if not c.explored]
    # Visible hostile structures (target rows: kind bit 1 = building, bit 2
    # = neutral) name the enemy base directly: their cell from the
    # normalized position (feature 0/1 = x/width, y/height; 8x8 grid).
    enemy_cells = []
    for t in range(len(request["target_id"])):
        bits = request["target_kind_bits"][t]
        if (bits & 2) == 0 or (bits & 4) != 0:
            continue
        fx, fy = request["target_feature"][t][0], request["target_feature"][t][1]
        cx = min(7, max(0, int(fx * 8)))
        cy = min(7, max(0, int(fy * 8)))
        enemy_cells.append(cy * 8 + cx)
    targets = enemy_cells or unexplored or [c.cell for c in starts]
    if not targets:
        return slot_commands, slot_cells, info

    def legal_cell_near(slot: int, wanted: int) -> int:
        words = request["slot_cell_mask_words"][slot]
        cells = [k for k, bit in enumerate(wire.expand_bitmask(words, wire.GLOBAL_CELL_COUNT))
                 if bit]
        if not cells:
            return -1
        return min(cells, key=lambda k: (_cell_distance(k, wanted), k))

    for slot, command, min_members in ((wire.SLOT_MAIN, wire.SLOT_COMMAND_ATTACK_MOVE, army_min),
                                       (wire.SLOT_SCOUT, wire.SLOT_COMMAND_MOVE, 1)):
        block = request["slots"][slot]
        if block.member_count < min_members:
            continue
        # A live order that members still pursue is left alone; an ended
        # one (everyone terminal / arrived) gets the next target.  With the
        # enemy base in sight the order is re-aimed at it right away.
        if block.active and block.command in wire.SLOT_POINT_COMMANDS and \
                block.pursuing > 0 and block.age_frames < 2400 and \
                (not enemy_cells or block.cell in enemy_cells):
            continue
        wanted_list = sorted(targets, key=lambda c: (_cell_distance(c, block.cell)
                                                     if block.cell >= 0 else 0, c))
        # Rotate away from the cell just visited.
        wanted = wanted_list[0]
        if block.active and block.cell == wanted and len(wanted_list) > 1:
            wanted = wanted_list[1]
        cell = legal_cell_near(slot, wanted)
        if cell < 0 or not wire.slot_choice_legal(request, slot, command, cell):
            continue
        if block.active and block.command == command and block.cell == cell:
            continue
        slot_commands[slot] = command
        slot_cells[slot] = cell
        info[slot] = (wanted, cell)
    return slot_commands, slot_cells, info


class EconomyProbePolicy(RandomLegalPolicy):
    """Deterministic economy + commander plumbing probe (no learning): every
    free worker HARVESTs the least-crowded reachable resource, every
    producer PRODUCEs the cheapest legal unit, every idle researcher takes
    the first legal research, combat rows KEEP; the commander marches MAIN
    at the unexplored start candidates once it holds 4 units and sends the
    first spare fighter to SCOUT.  Exercises the HARVEST latch, the repeated
    enqueue path, the same-tick ledgers and the slot derivation chain
    against the live engine."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.last_request: Dict[Tuple[int, int], Dict] = {}
        self.slot_orders_sent = 0
        self.assigns_sent = 0

    def act(self, connection_id: int, header: wire.Header, request: Dict) -> bytes:
        ledger = wire.EconomyLedger(request, header)
        assign_ledger = wire.AssignLedger(wire.scout_free_at_snapshot(request))
        slot_commands, slot_cells, slot_info = probe_slot_orders(request)
        self.slot_orders_sent += sum(1 for c in slot_commands if c)
        self.last_request[(connection_id, header.owner)] = request
        commands: List[int] = []
        arguments: List[int] = []
        assigns: List[int] = []
        cands = request["candidates"]
        # One supply structure at a time when the population is exhausted.
        walking = any(t != wire.TYPE_SENTINEL
                      for t in request["own_walking_build_type_id"])
        want_build = request["spendable_population"] == 0 and not walking
        main_members = request["slots"][wire.SLOT_MAIN].member_count
        # Production: the cheapest unit until 8 workers stand, then the
        # cheapest NON-worker type (worker types = the types of our worker
        # rows) so an army exists for the commander probe.  The fighter
        # production building (PROBE_ARMY_BUILDING, from the SHD3 teacher
        # data: 0x84 produces 0x21/0x22, 0x80 only the worker 0x20) is built
        # once when 8 workers stand and none exists yet.
        worker_types = {request["own_type_id"][i] for i in range(header.own_rows)
                        if request["own_role"][i] == wire.ROLE_WORKER}
        worker_count = sum(1 for i in range(header.own_rows)
                           if request["own_role"][i] == wire.ROLE_WORKER)
        has_army_building = any(request["own_type_id"][i] == PROBE_ARMY_BUILDING
                                for i in range(header.own_rows)) or \
            PROBE_ARMY_BUILDING in request["own_walking_build_type_id"]
        want_army_building = worker_count >= 8 and not has_army_building and not walking
        for i in range(header.own_rows):
            # SCOUT: the first MAIN member beyond a 2-unit core takes the
            # free seat (the ledger closes it for the rest of the tick).
            assign = 0
            assign_mask = assign_ledger.dynamic_mask(request["own_assign_mask"][i])
            if (assign_mask >> wire.SLOT_SCOUT) & 1 and main_members > 2 and \
                    request["own_slot_id"][i] == wire.SLOT_MAIN:
                assign = wire.SLOT_SCOUT + 1
                assign_ledger.apply(assign)
                main_members -= 1
                self.assigns_sent += 1
            assigns.append(assign)
            dyn_cmd, dyn_pair = ledger.dynamic_masks(request["command_mask"][i],
                                                     request["economy_pair_mask_words"][i])
            command = 0
            argument = -1
            role = request["own_role"][i]

            def legal(kind):
                return [k for k in range(header.candidate_rows)
                        if (dyn_pair[k >> 5] >> (k & 31)) & 1 and cands[k].kind == kind]
            if role == wire.ROLE_WORKER and want_army_building and \
                    (dyn_cmd >> wire.COMMAND_BUILD) & 1 and \
                    [k for k in legal(wire.CAND_BUILD_SITE)
                     if cands[k].object_id == PROBE_ARMY_BUILDING]:
                options = [k for k in legal(wire.CAND_BUILD_SITE)
                           if cands[k].object_id == PROBE_ARMY_BUILDING]
                command = wire.COMMAND_BUILD
                argument = min(options, key=lambda k: (cands[k].feature[7], k))
                want_army_building = False
            elif role == wire.ROLE_WORKER and want_build and \
                    (dyn_cmd >> wire.COMMAND_BUILD) & 1:
                options = legal(wire.CAND_BUILD_SITE)
                if options:
                    command = wire.COMMAND_BUILD
                    argument = min(options, key=lambda k: (cands[k].raw0, cands[k].feature[7], k))
                    want_build = False
            elif role == wire.ROLE_WORKER and (dyn_cmd >> wire.COMMAND_HARVEST) & 1 and \
                    request["own_semantic_order"][i] != wire.SEMANTIC_HARVEST:
                options = legal(wire.CAND_RESOURCE)
                if options:
                    command = wire.COMMAND_HARVEST
                    argument = min(options, key=lambda k: (cands[k].raw2, k))
            elif role == wire.ROLE_BUILDING:
                if (dyn_cmd >> wire.COMMAND_PRODUCE_UNIT) & 1:
                    options = legal(wire.CAND_PRODUCE_UNIT)
                    if options and worker_count >= 8:
                        fighters = [k for k in options
                                    if cands[k].object_id not in worker_types]
                        options = fighters or options
                    if options:
                        command = wire.COMMAND_PRODUCE_UNIT
                        argument = min(options, key=lambda k: (cands[k].raw0, k))
                elif (dyn_cmd >> wire.COMMAND_RESEARCH_UPGRADE) & 1:
                    options = legal(wire.CAND_RESEARCH_UPGRADE)
                    if options:
                        command = wire.COMMAND_RESEARCH_UPGRADE
                        argument = options[0]
            if command in wire.ECONOMY_COMMANDS:
                ledger.reserve(command, argument)
            commands.append(command)
            arguments.append(argument)
        return wire.pack_reply(commands, arguments, header, assigns, slot_commands,
                               slot_cells)

    def terminal(self, connection_id: int, header: wire.Header,
                 request: Dict) -> Optional[Dict]:
        """Print the intent/slot end state so a probe run can be judged
        from the server log (start candidates explored, enemy base known,
        army distance, slot blocks)."""
        material = request["intent_reward_material"]
        blocks = ["%d:n%d cmd%d act%d cell%d age%d pur%d term%d" % (
            s, b.member_count, b.command, b.active, b.cell, b.age_frames, b.pursuing,
            b.terminal) for s, b in enumerate(request["slots"])]
        starts = ["%d%s%s" % (c.cell, "e" if c.explored else "-", "o" if c.is_own else "")
                  for c in request["start_candidates"] if c.cell >= 0]
        print("ranker_entity2_server: probe owner %d frame %d explored %d/%d enemy_known %d "
              "army_dist %s losses %s slots [%s] starts [%s] slot_orders %d assigns %d" % (
                  header.owner, header.frame, material[0], material[1], material[2],
                  "none" if material[3] == 0xFFFFFFFF else material[3],
                  list(request["cumulative_losses"]), "; ".join(blocks), " ".join(starts),
                  self.slot_orders_sent, self.assigns_sent), flush=True)
        return None


class NetPolicy:
    """Learner: immutable per-cohort policy, per-connection RNG streams,
    OUTCOME-joined transitions, barrier PPO update, atomic checkpoint."""

    def __init__(self, checkpoint: str, train: bool = False, out: str = "",
                 lr: float = 1e-4, epochs: int = 2, issue_prior: Optional[float] = None,
                 slot_keep_prior: float = 0.98,
                 economy_issue_prior: Optional[float] = None,
                 gate_kl_coef: float = 0.0, minibatch: int = 32, base_seed: int = 1,
                 environment_seed_base: Optional[int] = None,
                 cohort_workers: Optional[int] = None,
                 environment_max_frames: Optional[int] = None,
                 reset_lineage: bool = False,
                 expected_rollout_jobs: Optional[int] = None,
                 expected_policy_fingerprint: str = "", hidden: int = 128,
                 max_update_steps: int = 0, issue_cost: float = 0.0):
        import torch
        import ranker_entity2_ppo as ppo
        import ranker_entity2_bc as bc
        import ranker_entity2_squads as squads
        self.torch = torch
        self.ppo = ppo
        self.bc = bc
        self.squads = squads
        self.train = train
        self.out = out
        self.lr = lr
        self.epochs = epochs
        self.issue_prior = issue_prior
        self.gate_kl_coef = gate_kl_coef
        self.minibatch = minibatch
        self.max_update_steps = max_update_steps
        # Reward cost per published command: command churn (STOP/HARVEST
        # cycles on workers) is otherwise free and PPO drifts into it.
        self.issue_cost = issue_cost
        self.base_seed = base_seed
        self.environment_seed_base = environment_seed_base
        self.cohort_workers = cohort_workers
        self.environment_max_frames = environment_max_frames
        if train and (environment_seed_base is None or cohort_workers is None or
                      environment_max_frames is None):
            raise RuntimeError("training needs --environment-seed-base, --workers and "
                               "--environment-max-frames")
        torch.manual_seed(base_seed)
        torch.set_num_threads(1)
        self.lock = threading.RLock()
        self.episodes: Dict[Tuple[int, int, int], List] = {}
        self.pending: Dict[Tuple[int, int, int], Dict] = {}
        self.connection_torch_rngs: Dict[int, "torch.Generator"] = {}
        self.connection_versions: Dict[int, int] = {}
        self.connection_fingerprints: Dict[int, bytes] = {}
        self.update_generator = torch.Generator().manual_seed(base_seed ^ 0x5EEDB47C)
        self.updates = 0
        self.global_steps = 0
        self.rollout_jobs = 0
        self.dropped_open_actions = 0
        self.invalid_records = 0
        self.file_lock: Optional[ExclusiveFileLock] = None
        payload: Dict = {}
        if checkpoint:
            self.net, payload = bc.load_checkpoint_payload(checkpoint)
        else:
            self.net = ppo.Entity2Net(hidden=hidden, issue_prior=issue_prior,
                                      slot_keep_prior=slot_keep_prior,
                                      economy_issue_prior=economy_issue_prior)
        self.optimizer = torch.optim.Adam(self.net.parameters(), lr=lr) if train else None
        extra = payload.get("extra") or {}
        if reset_lineage:
            extra = {}
        if validate_learner_resume_extra(extra):
            for key, value in (("base_seed", base_seed),
                               ("environment_seed_base", environment_seed_base),
                               ("cohort_workers", cohort_workers),
                               ("environment_max_frames", environment_max_frames),
                               ("issue_prior", issue_prior),
                               ("gate_kl_coef", gate_kl_coef),
                               ("learning_rate", lr), ("ppo_epochs", epochs),
                               ("minibatch", minibatch)):
                if extra[key] != value:
                    raise RuntimeError("resume %s mismatch: checkpoint %r vs %r "
                                       "(use --reset-lineage for a new lineage)" %
                                       (key, extra[key], value))
            self.updates = int(extra["updates"])
            self.global_steps = int(extra["global_steps"])
            self.rollout_jobs = int(extra["rollout_jobs"])
            if self.optimizer is not None:
                self.optimizer.load_state_dict(extra["optimizer"])
            self.update_generator.set_state(extra["update_rng_state"])
        self.policy_fingerprint = bc.module_fingerprint(self.net)
        if expected_rollout_jobs is not None and expected_rollout_jobs != self.rollout_jobs:
            raise RuntimeError("launcher manifest rollout_jobs %d != checkpoint %d" %
                               (expected_rollout_jobs, self.rollout_jobs))
        if expected_policy_fingerprint and \
                expected_policy_fingerprint != self.policy_fingerprint.hex():
            raise RuntimeError("launcher manifest policy fingerprint mismatch")
        if train:
            self.file_lock = ExclusiveFileLock(out)
            if checkpoint and os.path.abspath(checkpoint) == os.path.abspath(out):
                # Locked re-read: detect an atomic replace between load and lock.
                _, again = bc.load_checkpoint_payload(checkpoint)
                again_extra = again.get("extra") or {}
                if int(again_extra.get("rollout_jobs", 0)) != self.rollout_jobs or \
                        bc.module_fingerprint(again[0] if False else self.net) != \
                        self.policy_fingerprint:
                    pass
                if int(again_extra.get("rollout_jobs", 0)) != self.rollout_jobs and \
                        not reset_lineage:
                    raise RuntimeError("in-place checkpoint changed before learner "
                                       "lock acquisition")

    # ---- per-connection RNG / pins ----
    def _torch_rng(self, connection_id: int):
        if connection_id not in self.connection_torch_rngs:
            gen = self.torch.Generator()
            gen.manual_seed((self.base_seed + 0x517CC1B727220A95 * (connection_id + 1))
                            & 0xFFFFFFFFFFFFFFFF)
            self.connection_torch_rngs[connection_id] = gen
        return self.connection_torch_rngs[connection_id]

    def hello(self, connection_id: int, hello: wire.HelloBody) -> wire.HelloBody:
        with self.lock:
            version = self.updates
            for record in hello.owners:
                if record.requested_policy_version not in (0xFFFFFFFF, version):
                    raise wire.WireError("requested policy version %d not served (%d)" %
                                         (record.requested_policy_version, version))
                if record.requested_checkpoint_sha256 not in (b"\x00" * 32,
                                                              self.policy_fingerprint):
                    raise wire.WireError("requested checkpoint fingerprint not served")
                record.requested_policy_version = version
                record.requested_checkpoint_sha256 = self.policy_fingerprint
            self.connection_versions[connection_id] = version
            self.connection_fingerprints[connection_id] = self.policy_fingerprint
            return hello

    def validate_header(self, connection_id: int, header: wire.Header) -> None:
        with self.lock:
            if connection_id not in self.connection_versions:
                raise wire.WireError("gameplay frame before HELLO policy pin")
            if header.policy_version != self.connection_versions[connection_id]:
                raise wire.WireError("policy version %d != pinned %d" %
                                     (header.policy_version,
                                      self.connection_versions[connection_id]))

    @staticmethod
    def _key(connection_id: int, header: wire.Header) -> Tuple[int, int, int]:
        return (connection_id, header.episode, header.owner)

    # ---- transitions ----
    def _seal(self, key, losses_next, frame_next, phi_next, intent_next,
              terminal_payoff=0.0, terminated=False, truncated=False) -> None:
        pending = self.pending.pop(key, None)
        if pending is None:
            return
        if not pending["outcome_received"]:
            self.dropped_open_actions += 1
            return
        step = pending["step"]
        dt = float(max(frame_next - pending["frame"], 1))
        step.dt = dt
        step.reward = self.ppo.step_reward(pending["losses"], losses_next,
                                           pending["phi"], phi_next, dt,
                                           terminal_payoff, terminated,
                                           pending["intent"], intent_next)
        step.reward -= self.issue_cost * pending.get("published", 0)
        step.terminal = terminated
        step.truncated = truncated
        self.episodes.setdefault(key, []).append(step)

    def act(self, connection_id: int, header: wire.Header, request: Dict) -> bytes:
        torch = self.torch
        with self.lock:
            self.validate_header(connection_id, header)
            key = self._key(connection_id, header)
            phi = self.ppo.econ_potential(request["economy_reward_material"])
            intent = self.ppo.intent_potential(request["intent_reward_material"])
            self._seal(key, request["cumulative_losses"], header.frame, phi, intent)
            step = self.ppo.step_from_request(request, header)
            with torch.no_grad():
                sample = self.ppo.sample_actions(self.net, step,
                                                 self._torch_rng(connection_id))
            self.ppo.attach_sample(step, sample)
            expanded = self.squads.expand_actions(step, sample)
            # Trainable bits are settled by the OUTCOME; until then nothing
            # in this step is trainable.
            step.trainable = torch.zeros(step.u, dtype=torch.bool)
            step.assign_trainable = torch.zeros(step.u, dtype=torch.bool)
            step.slot_trainable = torch.zeros(wire.SLOT_COUNT, dtype=torch.bool)
            self.pending[key] = {
                "step": step, "stochastic": sample["stochastic"],
                "assign_stochastic": sample["assign_stochastic"],
                "slot_stochastic": sample["slot_stochastic"],
                "expanded": expanded,
                "outcome_received": False,
                "losses": list(request["cumulative_losses"]), "frame": header.frame,
                "sequence": header.sequence, "phi": phi, "intent": intent,
            }
            return wire.pack_reply(expanded["command"],
                                   expanded["argument"], header,
                                   expanded["assign"],
                                   sample["slot_command"].tolist(),
                                   sample["slot_cell"].tolist())

    def control_counts(self, connection_id: int, header: wire.Header) -> Dict:
        with self.lock:
            step = self.pending[self._key(connection_id, header)]["step"]
            return {"control_rows": step.u,
                    "squad_rows": sum(row.kind == self.squads.SQUAD
                                      for row in step.control_layout.rows),
                    "worker_task_rows": sum(row.kind == self.squads.WORKER_TASK
                                            for row in step.control_layout.rows),
                    "autopilot_workers": len(step.control_layout.worker_commands)}

    def outcome(self, connection_id: int, header: wire.Header, outcome: Dict) -> None:
        with self.lock:
            key = self._key(connection_id, header)
            pending = self.pending.get(key)
            if pending is None or pending["sequence"] != header.sequence:
                raise wire.WireError("OUTCOME without a matching open action")
            trainable = outcome["trainable"]
            if len(trainable) != pending["step"].control_layout.wire_rows:
                raise wire.WireError("OUTCOME row count mismatch")
            torch = self.torch
            step = pending["step"]
            # Trainable only where the receiver says so AND our own dynamic
            # mask had a genuine choice (forced choices carry log-prob 0).
            row_bits, assign_bits = self.squads.reduce_outcome(outcome, pending["expanded"])
            step.trainable = row_bits & pending["stochastic"]
            step.assign_trainable = assign_bits & pending["assign_stochastic"]
            step.slot_trainable = torch.tensor(outcome["slot_trainable"],
                                               dtype=torch.bool) & \
                pending["slot_stochastic"]
            pending["published"] = self.squads.published_decisions(outcome, pending["expanded"])
            pending["outcome_received"] = True

    def terminal(self, connection_id: int, header: wire.Header,
                 request: Dict) -> Optional[Dict]:
        with self.lock:
            self.validate_header(connection_id, header)
            key = self._key(connection_id, header)
            terminated = bool(header.flags & wire.FLAG_TERMINATED)
            payoff = self.ppo.TERMINAL_PAYOFF[request["terminal_outcome"]] if terminated \
                else 0.0
            phi = self.ppo.econ_potential(request["economy_reward_material"])
            intent = self.ppo.intent_potential(request["intent_reward_material"])
            self._seal(key, request["cumulative_losses"], header.frame, phi, intent,
                       payoff, terminated=terminated, truncated=not terminated)
            steps = self.episodes.pop(key, [])
            final_value = 0.0
            if not terminated and steps:
                step = self.ppo.step_from_request(request, header)
                with self.torch.no_grad():
                    final_value = float(self.net.encode(step)["value"])
            material = request["intent_reward_material"]
            blocks = ["%d:n%d cmd%d cell%d pur%d term%d" % (
                s, b.member_count, b.command if b.active else 0, b.cell if b.active else -1,
                b.pursuing, b.terminal) for s, b in enumerate(request["slots"])]
            print("ranker_entity2_server: intent owner %d frame %d %s explored %d/%d "
                  "enemy_known %d army_dist %s losses %s slots [%s]" % (
                      header.owner, header.frame,
                      "terminated" if terminated else "truncated", material[0],
                      material[1], material[2],
                      "none" if material[3] == 0xFFFFFFFF else material[3],
                      list(request["cumulative_losses"]), "; ".join(blocks)), flush=True)
            if not steps:
                return None
            return {"steps": steps, "final_value": final_value,
                    "policy_version": self.connection_versions.get(connection_id),
                    "policy_fingerprint": self.connection_fingerprints.get(connection_id),
                    "connection_id": connection_id, "episode": header.episode,
                    "owner": header.owner}

    def cutoff_connection(self, connection_id: int) -> List[Dict]:
        """Infrastructure cutoff: keep the sealed prefix, bootstrap with the
        last parsed observation's value, drop the open action."""
        rollouts = []
        with self.lock:
            for key in [k for k in self.episodes if k[0] == connection_id]:
                steps = self.episodes.pop(key)
                pending = self.pending.pop(key, None)
                final_value = 0.0
                if pending is not None:
                    final_value = float(pending["step"].behavior_value)
                    self.dropped_open_actions += 1
                if steps:
                    steps[-1].cutoff = True
                    rollouts.append({"steps": steps, "final_value": final_value,
                                     "policy_version": self.connection_versions.get(
                                         connection_id),
                                     "policy_fingerprint":
                                         self.connection_fingerprints.get(connection_id),
                                     "connection_id": connection_id,
                                     "episode": key[1], "owner": key[2]})
        return rollouts

    def abort_connection(self, connection_id: int) -> None:
        with self.lock:
            for key in [k for k in self.pending if k[0] == connection_id]:
                self.pending.pop(key, None)
                self.dropped_open_actions += 1
            for key in [k for k in self.episodes if k[0] == connection_id]:
                self.episodes.pop(key, None)
            self.connection_torch_rngs.pop(connection_id, None)
            self.connection_versions.pop(connection_id, None)
            self.connection_fingerprints.pop(connection_id, None)

    # ---- learner ----
    def _checkpoint_extra(self) -> Dict:
        return {
            "learner_state_version": LEARNER_STATE_VERSION,
            "updates": self.updates, "global_steps": self.global_steps,
            "rollout_jobs": self.rollout_jobs, "base_seed": self.base_seed,
            "environment_seed_base": self.environment_seed_base,
            "cohort_workers": self.cohort_workers,
            "environment_max_frames": self.environment_max_frames,
            "issue_prior": self.issue_prior, "gate_kl_coef": self.gate_kl_coef,
            "learning_rate": self.lr, "ppo_epochs": self.epochs,
            "minibatch": self.minibatch,
            "optimizer": self.optimizer.state_dict() if self.optimizer else None,
            "update_rng_state": self.update_generator.get_state(),
            "policy_fingerprint": self.policy_fingerprint.hex(),
            "dropped_open_actions": self.dropped_open_actions,
            "invalid_records": self.invalid_records,
        }

    def update_batch(self, rollouts: List[Dict], cohort_size: Optional[int] = None) -> Dict:
        with self.lock:
            job_count = cohort_size if cohort_size is not None else len(rollouts)
            if not self.train:
                self.rollout_jobs += job_count
                return {"trained_steps": 0}
            rollouts = sorted(rollouts, key=lambda r: (r["connection_id"], r["episode"],
                                                       r["owner"]))
            for rollout in rollouts:
                if rollout["policy_version"] != self.updates or \
                        rollout["policy_fingerprint"] != self.policy_fingerprint:
                    raise RuntimeError("rollout from a foreign policy version")
            stats = {"trained_steps": 0}
            if rollouts:
                stats = self.ppo.ppo_update_batched_episodes(
                    self.net, self.optimizer, rollouts, generator=self.update_generator,
                    gate_prior=self.issue_prior, gate_kl_coef=self.gate_kl_coef,
                    epochs=self.epochs, minibatch=self.minibatch,
                    max_steps=self.max_update_steps)
                self.updates += 1
                self.global_steps += int(stats.get("trained_steps", 0))
                self.invalid_records += int(stats.get("invalid_records", 0))
                self.policy_fingerprint = self.bc.module_fingerprint(self.net)
            self.rollout_jobs += job_count
            self.bc.save_checkpoint(self.net, self.out, extra=self._checkpoint_extra())
            print("ranker_entity2_server: update %d steps=%s sampled=%s loss=%.4f "
                  "actor=%.4f value=%.4f return_scale=%s invalid=%s jobs=%d dropped=%d" % (
                      self.updates, stats.get("trained_steps"),
                      stats.get("sampled_steps", stats.get("trained_steps")),
                      float(stats.get("loss", 0.0)), float(stats.get("actor", 0.0)),
                      float(stats.get("value", 0.0)), stats.get("return_scale", 1.0),
                      stats.get("invalid_records", 0),
                      self.rollout_jobs, self.dropped_open_actions),
                  flush=True)
            return stats

    def close(self) -> None:
        if self.file_lock is not None:
            self.file_lock.close()
            self.file_lock = None


# ---------------------------------------------------------------------------
# Connection serving (protocol invariants, plan 12.1 / 14)
# ---------------------------------------------------------------------------


DUMP_REQUEST_DIR = ""      # --dump-requests: raw ACT_REQ/REPLY/OUTCOME frames
DUMP_REQUEST_LIMIT = 8     # --dump-limit: frames of each kind per connection


def serve_connection(sock: socket.socket, policy, stats: Dict, connection_id: int,
                     reply_timeout_ms: int = 15000) -> Dict:
    sessions: Dict[int, Dict] = {}
    hello_done = False
    expected_owners = 0
    terminal_owners = 0
    rollouts: List[Dict] = []
    dumped = 0

    def dump(kind_name: str, header_: wire.Header, payload_: bytes) -> None:
        nonlocal dumped
        if not DUMP_REQUEST_DIR or dumped >= 3 * DUMP_REQUEST_LIMIT:
            return
        os.makedirs(DUMP_REQUEST_DIR, exist_ok=True)
        with open(os.path.join(DUMP_REQUEST_DIR, "%s_c%d_o%d_f%d_s%d.bin" % (
                kind_name, connection_id, header_.owner, header_.frame,
                header_.sequence)), "wb") as handle:
            handle.write(wire.pack_header(header_) + payload_)
        dumped += 1

    try:
        while True:
            header, payload = recv_frame(sock)
            if header.kind == wire.KIND_ACT_REQ:
                dump("act_req", header, payload)
            elif header.kind == wire.KIND_OUTCOME:
                dump("outcome", header, payload)
            if header.kind != wire.KIND_HELLO:
                if not hello_done:
                    raise wire.WireError("gameplay frame before HELLO")
                if header.kind in (wire.KIND_ACT_REQ, wire.KIND_OUTCOME,
                                   wire.KIND_TERMINAL):
                    session = sessions.get(header.owner)
                    if session is None:
                        raise wire.WireError("frame for an uncontrolled owner")
                    if session["done"]:
                        raise wire.WireError("gameplay frame after the owner TERMINAL")
                    if header.episode != session["episode"]:
                        raise wire.WireError("episode changed inside the connection")
                    if header.kind == wire.KIND_ACT_REQ:
                        if session["awaiting"] is not None:
                            raise wire.WireError("ACT_REQ before the previous OUTCOME")
                        if header.sequence != session["last_sequence"] + 1 or \
                                header.reply_to_sequence != session["last_sequence"]:
                            raise wire.WireError("ACT_REQ sequence violation")
                        if header.frame < session["last_frame"]:
                            raise wire.WireError("ACT_REQ frame went backwards")
                        if header.flags != 0:
                            raise wire.WireError("ACT_REQ with flags")
                        session["awaiting"] = (header.sequence, header.frame,
                                               header.own_rows, header.target_rows,
                                               header.candidate_rows)
                        session["last_sequence"] = header.sequence
                        session["last_frame"] = header.frame
                    elif header.kind == wire.KIND_OUTCOME:
                        expected = (header.sequence, header.frame, header.own_rows,
                                    header.target_rows, header.candidate_rows)
                        if session["awaiting"] != expected or \
                                header.reply_to_sequence != header.sequence:
                            raise wire.WireError("OUTCOME does not match the open ACT_REQ")
                        session["awaiting"] = None
                    else:
                        if session["awaiting"] is not None:
                            raise wire.WireError("TERMINAL before the previous OUTCOME")
                        if header.sequence != session["last_sequence"] or \
                                header.reply_to_sequence != header.sequence:
                            raise wire.WireError("TERMINAL sequence violation")
                        if header.frame < session["last_frame"]:
                            raise wire.WireError("TERMINAL frame went backwards")
                        if not (header.flags & (wire.FLAG_TERMINATED |
                                                wire.FLAG_TRUNCATED)):
                            raise wire.WireError("TERMINAL without terminated/truncated")
            if header.kind == wire.KIND_HELLO:
                if hello_done:
                    raise wire.WireError("duplicate HELLO")
                if header.owner != 0xFFFFFFFF or header.frame != 0 or \
                        header.own_rows or header.target_rows or header.candidate_rows or \
                        header.flags or header.policy_version:
                    raise wire.WireError("HELLO header invariant violated")
                hello = wire.parse_hello(payload)
                hello.reply_timeout_ms = reply_timeout_ms
                if hasattr(policy, "hello"):
                    hello = policy.hello(connection_id, hello)
                for record in hello.owners:
                    sessions[record.owner] = {"episode": header.episode,
                                              "last_sequence": 0, "last_frame": 0,
                                              "awaiting": None, "done": False}
                expected_owners = len(hello.owners)
                hello_done = True
                ack = wire.Header(kind=wire.KIND_ACK, owner=header.owner,
                                  episode=header.episode)
                send_frame(sock, ack, wire.pack_hello(hello))
            elif header.kind == wire.KIND_ACT_REQ:
                started = time.monotonic()
                request = wire.parse_act_request(header, payload)
                if hasattr(policy, "validate_header"):
                    policy.validate_header(connection_id, header)
                reply = policy.act(connection_id, header, request)
                send_frame(sock, _echo_header(header, wire.KIND_ACT_REPLY), reply)
                dump("reply", _echo_header(header, wire.KIND_ACT_REPLY), reply)
                session["last_reply"] = wire.parse_reply(header, reply)
                session["last_roles"] = list(request["own_role"])
                latency = time.monotonic() - started
                stats["act"] = stats.get("act", 0) + 1
                stats["rows"] = stats.get("rows", 0) + header.own_rows
                if hasattr(policy, "control_counts"):
                    for name, count in policy.control_counts(connection_id, header).items():
                        stats[name] = stats.get(name, 0) + count
                stats["candidates"] = stats.get("candidates", 0) + header.candidate_rows
                # Reply latency (parse + policy + send, including lock waits):
                # a stall beyond the game's reply timeout kills its controller.
                stats["act_seconds"] = stats.get("act_seconds", 0.0) + latency
                if latency > stats.get("act_max_seconds", 0.0):
                    stats["act_max_seconds"] = round(latency, 3)
                    stats["act_max_rows"] = header.own_rows
                    stats["act_max_candidates"] = header.candidate_rows
                if latency > 5.0:
                    stats["act_over_5s"] = stats.get("act_over_5s", 0) + 1
            elif header.kind == wire.KIND_OUTCOME:
                outcome = wire.parse_outcome(header, payload)
                if hasattr(policy, "outcome"):
                    policy.outcome(connection_id, header, outcome)
                published = sum(1 for r in outcome["result"]
                                if r == wire.RESULT_PUBLISHED)
                stats["issue"] = stats.get("issue", 0) + published
                # Published command histogram per role ("<role>:<command>"),
                # to tell economy churn from combat orders in a cohort.
                last_reply = session.get("last_reply")
                if last_reply is not None:
                    histogram = stats.setdefault("issued", {})
                    roles = session.get("last_roles", [])
                    for i, result in enumerate(outcome["result"]):
                        if result != wire.RESULT_PUBLISHED or i >= len(last_reply["command"]):
                            continue
                        # role letters: m melee, r ranged, w worker, b building,
                        # t transport, o other (wire.ROLE_* order)
                        key = "%s:%d" % ("mrwbto"[roles[i]] if i < len(roles) and
                                         roles[i] < 6 else "?", last_reply["command"][i])
                        histogram[key] = histogram.get(key, 0) + 1
                    for s, command in enumerate(last_reply["slot_command"]):
                        if command and outcome["slot_result"][s] == wire.RESULT_PUBLISHED:
                            key = "slot%d:%d" % (s, command)
                            histogram[key] = histogram.get(key, 0) + 1
                for code in outcome["reject_code"]:
                    if code:
                        stats.setdefault("rejects", {})
                        stats["rejects"][code] = stats["rejects"].get(code, 0) + 1
                slot_published = sum(1 for r in outcome["slot_result"]
                                     if r == wire.RESULT_PUBLISHED)
                if slot_published:
                    stats["slot_issue"] = stats.get("slot_issue", 0) + slot_published
                for code in outcome["slot_reject_code"]:
                    if code:
                        stats.setdefault("slot_rejects", {})
                        stats["slot_rejects"][code] = stats["slot_rejects"].get(code, 0) + 1
                assigned = sum(1 for bit in outcome["assign_trainable"] if bit)
                if assigned:
                    stats["assign_trainable"] = stats.get("assign_trainable", 0) + assigned
            elif header.kind == wire.KIND_TERMINAL:
                request = wire.parse_act_request(header, payload, terminal=True)
                sessions[header.owner]["done"] = True
                terminal_owners += 1
                send_frame(sock, _echo_header(header, wire.KIND_ACK), b"")
                if hasattr(policy, "terminal"):
                    rollout = policy.terminal(connection_id, header, request)
                    if rollout is not None:
                        rollouts.append(rollout)
                stats["terminal"] = stats.get("terminal", 0) + 1
                if terminal_owners >= expected_owners:
                    return {"valid": True, "rollouts": rollouts, "error": ""}
            elif header.kind == wire.KIND_ERROR:
                error = wire.parse_error(payload)
                print("ranker_entity2_server: peer error %d %s" %
                      (error["code"], error["message"]), flush=True)
            else:
                raise wire.WireError("unexpected frame kind %d" % header.kind)
    except (ConnectionError, socket.timeout, wire.WireError, OSError) as exc:
        if hasattr(policy, "cutoff_connection"):
            rollouts.extend(policy.cutoff_connection(connection_id))
        return {"valid": False, "rollouts": rollouts, "error": "%s: %s" %
                (type(exc).__name__, exc)}
    finally:
        if hasattr(policy, "abort_connection"):
            policy.abort_connection(connection_id)


def _open_listener(port: int, accept_timeout: float) -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if os.name == "nt":
        listener.setsockopt(socket.SOL_SOCKET, getattr(socket, "SO_EXCLUSIVEADDRUSE"), 1)
    else:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(1)
    listener.settimeout(accept_timeout)
    return listener


def serve(port: int, policy, episodes: int = 1, workers: int = 1,
          accept_timeout: float = 30.0, connection_timeout: float = 900.0,
          reply_timeout_ms: int = 15000, cohorts_per_update: int = 1) -> int:
    """`cohorts_per_update` > 1 accumulates that many cohorts' rollouts
    before one PPO update (more games per update than one policy process
    can serve concurrently within the reply timeout)."""
    worker_ports = [port + slot for slot in range(workers)]
    listeners = [_open_listener(p, accept_timeout) for p in worker_ports]
    completed = 0
    cohort_index = 0
    held_rollouts: List[Dict] = []
    held_jobs = 0
    print("ranker_entity2_server: READY 0", flush=True)
    try:
        while completed < episodes:
            cohort = min(workers, episodes - completed)
            rollout_job_base = getattr(policy, "rollout_jobs", completed)
            clients: List[Tuple[int, socket.socket]] = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=cohort) as pool:
                futures = []
                for slot in range(cohort):
                    def accept(slot=slot):
                        listener = listeners[slot]
                        sock, _ = listener.accept()
                        listener.close()
                        sock.settimeout(connection_timeout)
                        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                        return slot, sock
                    futures.append(pool.submit(accept))
                for future in futures:
                    clients.append(future.result())
            stats: Dict = {}
            results = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=cohort) as pool:
                futures = [pool.submit(serve_connection, sock, policy, stats,
                                       rollout_job_base + slot, reply_timeout_ms)
                           for slot, sock in clients]
                for future in futures:
                    results.append(future.result())
            for _, sock in clients:
                try:
                    sock.close()
                except OSError:
                    pass
            errors = [r["error"] for r in results if not r["valid"]]
            if errors:
                print("ranker_entity2_server: COHORT_FAILED %s" % errors, flush=True)
                return 2
            cohort_rollouts = [r for result in results for r in result["rollouts"]]
            cohort_index += 1
            held_rollouts.extend(cohort_rollouts)
            held_jobs += cohort
            completed += cohort
            if hasattr(policy, "update_batch"):
                if cohort_index % max(cohorts_per_update, 1) == 0 or completed >= episodes:
                    policy.update_batch(held_rollouts, held_jobs)
                    held_rollouts = []
                    held_jobs = 0
                else:
                    print("ranker_entity2_server: holding %d rollouts for the next update" %
                          len(held_rollouts), flush=True)
            if stats.get("act"):
                stats["act_mean_ms"] = round(1000.0 * stats.pop("act_seconds", 0.0) /
                                             stats["act"], 1)
            print("ranker_entity2_server: cohort done stats=%s" % stats, flush=True)
            for slot in range(workers):
                if completed < episodes and slot < min(workers, episodes - completed):
                    listeners[slot] = _open_listener(worker_ports[slot], accept_timeout)
            print("ranker_entity2_server: READY %d" % completed, flush=True)
        return 0
    finally:
        for listener in listeners:
            try:
                listener.close()
            except OSError:
                pass
        if hasattr(policy, "close"):
            policy.close()


# ---------------------------------------------------------------------------
# Selftest: a fake game client drives the protocol end to end.
# ---------------------------------------------------------------------------


def _fake_game(port: int, owners: List[int], ticks: int, terminal_outcome: int,
               seed: int, result: Dict) -> None:
    """Connects, HELLOs, sends `ticks` ACT_REQs per owner (validating every
    reply against the same-tick ledger), then TERMINALs each owner."""
    rng = random.Random(seed)
    sock = socket.create_connection(("127.0.0.1", port), timeout=30)
    try:
        body, base_header = wire._slot_fixture_request()
        hello = wire.HelloBody(controlled_owner_mask=sum(1 << o for o in owners),
                               owners=[wire.HelloOwnerRecord(o, 0b10 if o == 0 else 0b01)
                                       for o in owners])
        send_frame(sock, wire.Header(kind=wire.KIND_HELLO, owner=0xFFFFFFFF, episode=1),
                   wire.pack_hello(hello))
        ack, ack_payload = recv_frame(sock)
        assert ack.kind == wire.KIND_ACK
        ack_body = wire.parse_hello(ack_payload)
        version = ack_body.owners[0].requested_policy_version
        sequences = {o: 0 for o in owners}
        losses = [0, 0, 0, 0]
        for tick in range(ticks):
            frame = 8 * (tick + 1)
            for owner in owners:
                losses[2] += 100 if tick % 3 == 0 else 0
                b = dict(body)
                b["cumulative_losses"] = list(losses)
                b["spendable_primary"] = 400 + 10 * tick
                payload = wire.pack_act_request(b)
                header = wire.Header(**{**base_header.__dict__, "owner": owner,
                                        "episode": 1, "frame": frame,
                                        "sequence": sequences[owner] + 1,
                                        "reply_to_sequence": sequences[owner],
                                        "policy_version": version})
                sequences[owner] += 1
                send_frame(sock, header, payload)
                reply_header, reply_payload = recv_frame(sock)
                assert reply_header.kind == wire.KIND_ACT_REPLY
                assert reply_header.sequence == header.sequence
                reply = wire.parse_reply(header, reply_payload)
                request = wire.parse_act_request(
                    wire.Header(**{**header.__dict__, "payload_bytes": len(payload),
                                   "payload_crc32": wire.crc32(payload)}), payload)
                replay = wire.replay_ledger(request, header, reply["command"],
                                            reply["argument"], assigns=reply["assign"])
                results = []
                rejects = []
                trainable = []
                assign_trainable = []
                for i in range(header.own_rows):
                    legal = wire.command_legal(
                        reply["command"][i], reply["argument"][i],
                        replay["dynamic_command_mask"][i],
                        replay["dynamic_economy_pair_mask_words"][i],
                        request["point_mask"][i], request["attack_pair_mask_words"][i],
                        request["candidates"])
                    assert legal, "policy replied an illegal choice"
                    assert replay["assign_legal"][i], "policy replied an illegal assign"
                    kept = reply["command"][i] == 0
                    results.append(wire.RESULT_KEPT if kept else wire.RESULT_PUBLISHED)
                    rejects.append(0)
                    trainable.append(wire.stochastic_rows(
                        replay["dynamic_command_mask"])[i])
                    assign_trainable.append(replay["dynamic_assign_mask"][i] != 0)
                    result.setdefault("issued", 0)
                    result["issued"] += 0 if kept else 1
                    if reply["assign"][i]:
                        result["assigned"] = result.get("assigned", 0) + 1
                slot_results = []
                slot_bits = 0
                for s in range(wire.SLOT_COUNT):
                    command = reply["slot_command"][s]
                    cell = reply["slot_cell"][s]
                    assert command == 0 and cell == -1 or \
                        wire.slot_choice_legal(request, s, command, cell), \
                        "policy replied an illegal slot order"
                    slot_results.append(wire.RESULT_KEPT if command == 0
                                        else wire.RESULT_PUBLISHED)
                    if request["slot_command_mask"][s] & ~1:
                        slot_bits |= 1 << s
                    if command:
                        result["slot_issued"] = result.get("slot_issued", 0) + 1
                outcome_header = wire.Header(**{**header.__dict__,
                                                "kind": wire.KIND_OUTCOME,
                                                "reply_to_sequence": header.sequence})
                send_frame(sock, outcome_header,
                           wire.pack_outcome(results, rejects, trainable, slot_results,
                                             [0] * wire.SLOT_COUNT, slot_bits,
                                             assign_trainable))
        for owner in owners:
            frame = 8 * (ticks + 2)
            payload = wire.pack_act_request(body, terminal_outcome=terminal_outcome)
            flags = wire.FLAG_TERMINATED if terminal_outcome else wire.FLAG_TRUNCATED
            header = wire.Header(**{**base_header.__dict__, "kind": wire.KIND_TERMINAL,
                                    "flags": flags, "owner": owner, "episode": 1,
                                    "frame": frame, "sequence": sequences[owner],
                                    "reply_to_sequence": sequences[owner],
                                    "policy_version": version})
            send_frame(sock, header, payload)
            ack, _ = recv_frame(sock)
            assert ack.kind == wire.KIND_ACK
        result["ok"] = True
    except Exception as exc:  # pragma: no cover - surfaced by the selftest
        result["error"] = "%s: %s" % (type(exc).__name__, exc)
    finally:
        sock.close()


def _free_port_pair() -> int:
    for _ in range(50):
        port = random.randint(20000, 40000)
        try:
            for p in (port, port + 1):
                s = socket.socket()
                s.bind(("127.0.0.1", p))
                s.close()
            return port
        except OSError:
            continue
    raise RuntimeError("no free port pair")


def selftest() -> None:
    import tempfile
    # 1. Random legal policy, two owners, two workers in one cohort.
    port = _free_port_pair()
    policy = RandomLegalPolicy(keep_bias=0.3)
    results = [{}, {}]
    server = threading.Thread(target=lambda: results.append(
        serve(port, policy, episodes=2, workers=2, accept_timeout=20.0)))
    server.start()
    time.sleep(0.3)
    games = [threading.Thread(target=_fake_game, args=(port + slot, [0, 1], 5, 1,
                                                       slot, results[slot]))
             for slot in range(2)]
    for game in games:
        game.start()
    for game in games:
        game.join()
    server.join(60)
    assert results[0].get("ok") and results[1].get("ok"), results
    assert results[2] == 0, "server exit code"
    assert results[0].get("issued", 0) > 0
    assert results[0].get("slot_issued", 0) + results[1].get("slot_issued", 0) > 0
    assert results[0].get("assigned", 0) + results[1].get("assigned", 0) > 0

    # 2. NetPolicy end to end with training: HELLO pin, act/outcome join,
    #    terminal seal, one PPO update at the barrier, checkpoint written.
    import torch
    import ranker_entity2_bc as bc
    directory = tempfile.mkdtemp()
    out = os.path.join(directory, "entity2.pt")
    net_policy = NetPolicy("", train=True, out=out, lr=1e-3, epochs=1,
                           environment_seed_base=100, cohort_workers=1,
                           environment_max_frames=2000, hidden=32)
    port = _free_port_pair()
    results = [{}]
    server = threading.Thread(target=lambda: results.append(
        serve(port, net_policy, episodes=1, workers=1, accept_timeout=20.0)))
    server.start()
    time.sleep(0.3)
    _fake_game(port, [0], 6, 1, 7, results[0])
    server.join(120)
    assert results[0].get("ok"), results[0]
    assert results[1] == 0
    assert net_policy.updates == 1 and net_policy.rollout_jobs == 1
    assert os.path.exists(out)
    loaded, payload = bc.load_checkpoint_payload(out)
    assert payload["extra"]["rollout_jobs"] == 1
    assert validate_learner_resume_extra(payload["extra"])
    # 3. Resume: identical config resumes, changed config is refused.
    resumed = NetPolicy(out, train=True, out=out, lr=1e-3, epochs=1,
                        environment_seed_base=100, cohort_workers=1,
                        environment_max_frames=2000, hidden=32,
                        expected_rollout_jobs=1)
    assert resumed.updates == 1
    resumed.close()
    try:
        NetPolicy(out, train=True, out=out, lr=1e-3, epochs=1,
                  environment_seed_base=101, cohort_workers=1,
                  environment_max_frames=2000, hidden=32)
    except RuntimeError:
        pass
    else:
        raise AssertionError("changed seed base resumed silently")
    # 4. OUTCOME join violation is protocol-fatal (exit 2, no update).
    port = _free_port_pair()
    policy = RandomLegalPolicy()
    results = [{}]
    server = threading.Thread(target=lambda: results.append(
        serve(port, policy, episodes=1, workers=1, accept_timeout=10.0)))
    server.start()
    time.sleep(0.3)
    sock = socket.create_connection(("127.0.0.1", port), timeout=10)
    body, base_header = wire._fixture_request()
    hello = wire.HelloBody(controlled_owner_mask=1,
                           owners=[wire.HelloOwnerRecord(0, 0b10)])
    send_frame(sock, wire.Header(kind=wire.KIND_HELLO, owner=0xFFFFFFFF, episode=1),
               wire.pack_hello(hello))
    recv_frame(sock)
    payload = wire.pack_act_request(body)
    header = wire.Header(**{**base_header.__dict__, "owner": 0, "episode": 1,
                            "frame": 8, "sequence": 1, "reply_to_sequence": 0,
                            "policy_version": 0})
    send_frame(sock, header, payload)
    recv_frame(sock)
    # Second ACT_REQ before the OUTCOME.
    header2 = wire.Header(**{**header.__dict__, "sequence": 2, "reply_to_sequence": 1})
    send_frame(sock, header2, payload)
    try:
        recv_frame(sock)
    except (ConnectionError, OSError, wire.WireError):
        pass
    sock.close()
    server.join(30)
    assert results[1] == 2, "OUTCOME-join violation was not fatal"
    print("ranker_entity2_server: selftest passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--port", type=int, default=6101)
    parser.add_argument("--keep-bias", type=float, default=0.6)
    parser.add_argument("--slot-keep-bias", type=float, default=0.8,
                        help="random policy: P(KEEP) per slot for the commander")
    parser.add_argument("--assign-keep-bias", type=float, default=0.8,
                        help="random policy: P(no slot move) per row")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--environment-seed-base", type=int, default=None)
    parser.add_argument("--environment-max-frames", type=int, default=None)
    parser.add_argument("--expected-rollout-jobs", type=int, default=None)
    parser.add_argument("--expected-policy-fingerprint", default="")
    parser.add_argument("--reset-lineage", action="store_true")
    parser.add_argument("--policy", default="")
    parser.add_argument("--train", action="store_true")
    parser.add_argument("--out", default="")
    parser.add_argument("--episodes", type=int, default=1)
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--accept-timeout", type=float, default=30.0)
    parser.add_argument("--connection-timeout", type=float, default=900.0)
    parser.add_argument("--reply-timeout-ms", type=int, default=15000)
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--minibatch", type=int, default=32)
    parser.add_argument("--gate-kl-coef", type=float, default=0.0)
    parser.add_argument("--issue-prior", type=float, default=None)
    parser.add_argument("--slot-keep-prior", type=float, default=0.98,
                        help="fresh-init commander P(KEEP) per slot per tick")
    parser.add_argument("--economy-issue-prior", type=float, default=None,
                        help="fresh-init gate P(issue) for worker / building rows "
                             "(fighters keep --issue-prior)")
    parser.add_argument("--hidden", type=int, default=128)
    parser.add_argument("--max-update-steps", type=int, default=0,
                        help="train each PPO epoch on at most this many sampled steps")
    parser.add_argument("--issue-cost", type=float, default=0.0,
                        help="reward subtracted per published command (anti-churn)")
    parser.add_argument("--cohorts-per-update", type=int, default=1,
                        help="accumulate this many cohorts before each PPO update")
    parser.add_argument("--dump-requests", default="",
                        help="directory receiving the first ACT_REQ/REPLY/OUTCOME frames")
    parser.add_argument("--dump-limit", type=int, default=8,
                        help="frames of each kind dumped per connection")
    parser.add_argument("--probe-economy", action="store_true",
                        help="deterministic harvest/produce/research probe policy")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if args.workers < 1 or args.port + args.workers > 65535:
        print("bad port/worker range", file=sys.stderr)
        return 1
    if not 1000 <= args.reply_timeout_ms <= 60000:
        print("reply timeout must be 1000..60000 ms", file=sys.stderr)
        return 1
    if args.train and not args.out:
        print("--train needs --out", file=sys.stderr)
        return 1
    global DUMP_REQUEST_DIR, DUMP_REQUEST_LIMIT
    DUMP_REQUEST_DIR = args.dump_requests
    DUMP_REQUEST_LIMIT = max(1, args.dump_limit)
    if args.policy or args.train:
        policy = NetPolicy(args.policy, train=args.train, out=args.out, lr=args.lr,
                           epochs=args.epochs, issue_prior=args.issue_prior,
                           slot_keep_prior=args.slot_keep_prior,
                           economy_issue_prior=args.economy_issue_prior,
                           gate_kl_coef=args.gate_kl_coef, minibatch=args.minibatch,
                           base_seed=args.seed,
                           environment_seed_base=args.environment_seed_base,
                           cohort_workers=args.workers if args.train else None,
                           environment_max_frames=args.environment_max_frames,
                           reset_lineage=args.reset_lineage,
                           expected_rollout_jobs=args.expected_rollout_jobs,
                           expected_policy_fingerprint=args.expected_policy_fingerprint,
                           hidden=args.hidden, max_update_steps=args.max_update_steps,
                           issue_cost=args.issue_cost)
    elif args.probe_economy:
        policy = EconomyProbePolicy(keep_bias=args.keep_bias, base_seed=args.seed)
    else:
        policy = RandomLegalPolicy(keep_bias=args.keep_bias, base_seed=args.seed,
                                   slot_keep_bias=args.slot_keep_bias,
                                   assign_keep_bias=args.assign_keep_bias)
    return serve(args.port, policy, episodes=args.episodes, workers=args.workers,
                 accept_timeout=args.accept_timeout,
                 connection_timeout=args.connection_timeout,
                 reply_timeout_ms=args.reply_timeout_ms,
                 cohorts_per_update=max(1, args.cohorts_per_update))


if __name__ == "__main__":
    raise SystemExit(main())
