# -*- coding: utf-8 -*-
"""act2 entity-command policy server (plan section 11).

Serves the binary act2 protocol to `ranker_rebuild.exe -AIENTITY:PORT`.
Default policy is random-legal (plumbing validation, the entity analogue of
-AIRANDOM): every sampled command/point/target obeys the request's hard masks
bit-for-bit, KEEP-biased so orders persist long enough to observe engine
feedback.  A trained policy plugs in through the same serve loop later.

Usage:
    python ranker_entity_server.py --port 6001 --workers 4 --episodes 20 \
        [--keep-bias 0.6] [--seed 1]
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import os
import random
import socket
import sys
import tempfile
import threading
import time

import ranker_entity_contract as wire

COMMAND_KEEP = 0
COMMAND_MOVE = 1
COMMAND_ATTACK_MOVE = 2
COMMAND_PATROL = 3
COMMAND_ATTACK_UNIT = 4
COMMAND_HOLD = 5
COMMAND_STOP = 6
_POINT_COMMANDS = (COMMAND_MOVE, COMMAND_ATTACK_MOVE, COMMAND_PATROL)
LEARNER_STATE_VERSION = 1
LEARNER_STATE_REQUIRED_KEYS = frozenset({
    "learner_state_version", "updates", "global_steps", "rollout_jobs",
    "base_seed", "environment_seed_base", "issue_prior", "gate_kl_coef",
    "learning_rate", "ppo_epochs", "max_train_steps_per_worker", "optimizer",
    "update_rng_state", "policy_fingerprint", "macro_fingerprint",
    "cohort_workers", "environment_max_frames",
})
LEARNER_STATE_INDICATOR_KEYS = frozenset({
    "learner_state_version", "updates", "global_steps", "rollout_jobs",
    "optimizer", "update_rng_state",
})


def module_fingerprint(module) -> bytes:
    """Stable SHA-256 over names, shapes, dtypes, and tensor bytes."""
    digest = hashlib.sha256()
    for name, tensor in sorted(module.state_dict().items()):
        value = tensor.detach().cpu().contiguous()
        digest.update(name.encode("utf-8"))
        digest.update(str(value.dtype).encode("ascii"))
        digest.update(repr(tuple(value.shape)).encode("ascii"))
        digest.update(value.numpy().tobytes())
    return digest.digest()


def validate_learner_resume_extra(extra) -> bool:
    """Return True for a complete online state, False for a BC checkpoint."""
    if not isinstance(extra, dict):
        raise RuntimeError("checkpoint extra must be a dictionary")
    version = extra.get("learner_state_version")
    if version is None:
        indicators = sorted(LEARNER_STATE_INDICATOR_KEYS.intersection(extra))
        if indicators:
            raise RuntimeError(
                "legacy/incomplete online learner state requires an explicit "
                "lineage reset: %s" % ",".join(indicators))
        return False
    if int(version) != LEARNER_STATE_VERSION:
        raise RuntimeError(
            "unsupported online learner state version %r" % version)
    missing = sorted(LEARNER_STATE_REQUIRED_KEYS.difference(extra))
    if missing:
        raise RuntimeError(
            "incomplete online learner state: missing %s" %
            ",".join(missing))
    if extra["environment_seed_base"] is None:
        raise RuntimeError(
            "online learner state is missing its environment seed base")
    return True


class ExclusiveFileLock:
    """Process-lifetime nonblocking lock beside a learner checkpoint."""

    def __init__(self, checkpoint_path: str):
        self.path = os.path.abspath(checkpoint_path) + ".lock"
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        self.handle = open(self.path, "a+b")
        if os.path.getsize(self.path) == 0:
            self.handle.write(b"\0")
            self.handle.flush()
        self.handle.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.handle.fileno(),
                            fcntl.LOCK_EX | fcntl.LOCK_NB)
        except (OSError, IOError) as exc:
            self.handle.close()
            self.handle = None
            raise RuntimeError(
                "checkpoint already has an active learner: %s" %
                checkpoint_path) from exc

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
        finally:
            self.handle.close()
            self.handle = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


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


def recv_frame(sock: socket.socket):
    header = wire.parse_header(recv_exact(sock, wire.HEADER_BYTES))
    payload = recv_exact(sock, header.payload_bytes)
    if wire.crc32(payload) != header.payload_crc32:
        raise wire.WireError("payload CRC mismatch")
    return header, payload


def send_frame(sock: socket.socket, header: wire.Header,
               payload: bytes) -> None:
    sock.sendall(wire.frame_bytes(header, payload))


class NetPolicy:
    """EntityNet-driven policy + online PPO collection (plan sections 10 /
    12 / 13.2).  Entity rows come from the network; the macro head stays
    random-legal in this v1 server (the macro tower is a separate model).
    Transitions seal on the NEXT ACT_REQ (reward from the u64 loss deltas,
    dt from the frame difference) exactly like the act2 contract."""

    APPROACH_DIST_FEATURE = 478   # army centroid -> nearest known enemy
    APPROACH_FLAG_FEATURE = 479   # building (visible or fog-remembered)
    ENEMY_BUILDING_GRID_START = 278   # 8x8 remembered/visible, row-major
    ENEMY_BUILDING_GRID_END = 342

    def __init__(self, checkpoint: str, rng: random.Random,
                 train: bool = False, out: str = "", lr: float = 1e-4,
                 epochs: int = 2, issue_prior: float = None,
                 gate_kl_coef: float = 0.01, approach_coef: float = 0.0,
                 explore_bias: float = 0.0,
                 explore_bias_command: float = None,
                 max_train_steps: int = 1024, macro_policy: str = "",
                 base_seed: int = 1,
                 environment_seed_base: int = None,
                 cohort_workers: int = None,
                 environment_max_frames: int = None,
                 reset_lineage: bool = False,
                 expected_rollout_jobs: int = None,
                 expected_policy_fingerprint: str = ""):
        import torch
        import ranker_entity_ppo as ppo
        import ranker_entity_bc as bc
        self.torch = torch
        self.ppo = ppo
        self.bc = bc
        self.rng = rng
        self.base_seed = int(base_seed)
        self.environment_seed_base = int(environment_seed_base) \
            if environment_seed_base is not None else None
        self.cohort_workers = int(cohort_workers) \
            if cohort_workers is not None else None
        self.environment_max_frames = int(environment_max_frames) \
            if environment_max_frames is not None else None
        self.reset_lineage = bool(reset_lineage)
        if (self.environment_seed_base is not None and
                not 1 <= self.environment_seed_base <= 0xffffffff):
            raise ValueError("environment seed base must fit nonzero uint32")
        if train and (self.environment_seed_base is None or
                      self.cohort_workers is None or
                      self.environment_max_frames is None):
            raise ValueError(
                "training requires environment seed/horizon and cohort width")
        if ((self.cohort_workers is not None and self.cohort_workers < 1) or
                (self.environment_max_frames is not None and
                 self.environment_max_frames < 1)):
            raise ValueError("cohort width and environment horizon must be positive")
        torch.manual_seed(self.base_seed)
        torch.set_num_threads(1)
        self.train = train
        self.out = out or checkpoint
        self.checkpoint_lock = None
        self.lr = lr
        self.epochs = epochs
        self.max_train_steps_per_worker = max_train_steps
        self.issue_prior = issue_prior
        self.gate_kl_coef = float(gate_kl_coef)
        self.approach_coef = float(approach_coef)
        self.explore_bias = float(explore_bias)
        self.explore_bias_command = float(explore_bias_command)             if explore_bias_command is not None else self.explore_bias
        self.bias_source_counts = {'base': 0, 'candidate': 0,
                                   'dark': 0, 'none': 0}
        # BC-free start: a fresh net takes the calibrated KEEP-gate prior
        # (plan 10.1) so it does not thrash orders from step one.
        payload = None
        if checkpoint:
            self.net, payload = bc.load_checkpoint_payload(checkpoint)
        else:
            self.net = ppo.EntityNet(issue_prior=issue_prior)
        self.optimizer = torch.optim.Adam(self.net.parameters(), lr=lr) \
            if train else None
        # All inference/state operations use this short critical section.
        # Game simulation remains parallel; only the small model call is
        # serialized, avoiding PyTorch RNG/state races with ragged requests.
        self.lock = threading.RLock()
        self.episodes = {}      # (connection, episode, owner) -> steps
        self.pending = {}       # (connection, episode, owner) -> open action
        self.connection_rngs = {}
        self.connection_torch_rngs = {}
        self.connection_versions = {}
        self.connection_fingerprints = {}
        self.update_generator = torch.Generator().manual_seed(
            (self.base_seed ^ 0x5EEDB47C) & 0x7fffffffffffffff)
        self.updates = 0
        self.global_steps = 0
        self.rollout_jobs = 0
        self.dropped_open_actions = 0
        resume_extra = {}
        if payload is not None and train:
            # BC checkpoints do not have learner state; online checkpoints do.
            resume_extra = {} if self.reset_lineage else \
                payload.get("extra", {})
            online_resume = False if self.reset_lineage else \
                validate_learner_resume_extra(resume_extra)
            if online_resume:
                self.optimizer.load_state_dict(resume_extra["optimizer"])
                self.updates = int(resume_extra["updates"])
                self.global_steps = int(resume_extra["global_steps"])
                self.rollout_jobs = int(resume_extra["rollout_jobs"])
                if min(self.updates, self.global_steps,
                       self.rollout_jobs) < 0:
                    raise RuntimeError(
                        "online learner counters must not be negative")
            saved_seed = resume_extra.get("base_seed")
            if (online_resume and
                    int(saved_seed) != self.base_seed):
                raise RuntimeError(
                    "resume base_seed differs from the checkpoint")
            saved_environment_seed = resume_extra.get(
                "environment_seed_base")
            if online_resume:
                if self.environment_seed_base is None:
                    self.environment_seed_base = int(
                        saved_environment_seed)
                elif (int(saved_environment_seed) !=
                      int(self.environment_seed_base)):
                    raise RuntimeError(
                        "resume environment seed base differs from the "
                        "checkpoint")
            if online_resume:
                saved_prior = resume_extra["issue_prior"]
                if self.issue_prior is None:
                    self.issue_prior = saved_prior
                elif saved_prior != self.issue_prior:
                    raise RuntimeError(
                        "resume issue_prior differs from the checkpoint")
            if (online_resume and
                    float(resume_extra["gate_kl_coef"]) !=
                    self.gate_kl_coef):
                raise RuntimeError(
                    "resume gate_kl_coef differs from this learner")
            # Reward-shaping/exploration coefficients are curriculum knobs:
            # each cohort is collected on-policy under the CURRENT values, so
            # changing them between legs cannot corrupt stored data — warn
            # only (2026-09-03: the approach-coef exploit fix needs this).
            if online_resume:
                for label, saved, current in (
                        ("approach_coef",
                         float(resume_extra.get("approach_coef", 0.0)),
                         self.approach_coef),
                        ("explore_bias",
                         float(resume_extra.get("explore_bias", 0.0)),
                         self.explore_bias)):
                    if saved != current:
                        print("ranker_entity_server: resume %s %s -> %s"
                              % (label, saved, current), flush=True)
            if online_resume:
                saved_cmd_bias = float(resume_extra.get(
                    "explore_bias_command",
                    resume_extra.get("explore_bias", 0.0)))
                if saved_cmd_bias != self.explore_bias_command:
                    print("ranker_entity_server: resume explore_bias_command"
                          " %s -> %s" % (saved_cmd_bias,
                                         self.explore_bias_command),
                          flush=True)
            # Training-SCHEDULE knobs (lr / epochs / per-worker sample
            # budget) may evolve across curriculum legs: warn, don't reject.
            # Environment semantics (seed base, horizon, cohort width,
            # reward coefficients) stay hard-checked below/above.
            if online_resume:
                for label, saved, current in (
                        ("learning_rate",
                         float(resume_extra["learning_rate"]),
                         float(self.lr)),
                        ("ppo_epochs",
                         int(resume_extra["ppo_epochs"]),
                         int(self.epochs)),
                        ("max_train_steps_per_worker",
                         int(resume_extra["max_train_steps_per_worker"]),
                         int(self.max_train_steps_per_worker))):
                    if saved != current:
                        print("ranker_entity_server: resume %s %s -> %s"
                              % (label, saved, current), flush=True)
            if online_resume:
                if (self.cohort_workers is None or
                        int(resume_extra["cohort_workers"]) !=
                        self.cohort_workers):
                    raise RuntimeError(
                        "resume cohort worker count differs from the "
                        "checkpoint")
                if (self.environment_max_frames is None or
                        int(resume_extra["environment_max_frames"]) !=
                        self.environment_max_frames):
                    raise RuntimeError(
                        "resume environment max frames differs from the "
                        "checkpoint")
            if online_resume:
                self.update_generator.set_state(
                    resume_extra["update_rng_state"])
        # Plan 10.1 macro tower: the trained legacy 802->80 policy decides
        # the macro head (production/build/research under the entity-mode
        # mask); the entity head stays this module's own net.  130 measured
        # BC-free games showed random/heuristic macro never creates the
        # combat exposure the entity head needs.
        self.macro_net = None
        if macro_policy:
            from pathlib import Path
            import ranker_selfplay
            self.macro_net = ranker_selfplay.load_net(Path(macro_policy))
            self.macro_net.eval()
            print("ranker_entity_server: macro policy <- %s" % macro_policy,
                  flush=True)
        self.policy_fingerprint = module_fingerprint(self.net)
        self.macro_fingerprint = module_fingerprint(self.macro_net) \
            if self.macro_net is not None else b"\x00" * 32
        saved_policy_sha = resume_extra.get("policy_fingerprint")
        if (saved_policy_sha is not None and
                bytes.fromhex(saved_policy_sha) != self.policy_fingerprint):
            raise RuntimeError("resume policy fingerprint mismatch")
        saved_macro_sha = resume_extra.get("macro_fingerprint")
        if (saved_macro_sha is not None and
                bytes.fromhex(saved_macro_sha) != self.macro_fingerprint):
            raise RuntimeError("resume macro fingerprint mismatch")
        if (expected_rollout_jobs is not None and
                int(expected_rollout_jobs) != self.rollout_jobs):
            raise RuntimeError(
                "checkpoint changed after launcher inspection: "
                "rollout_jobs %d != expected %d" %
                (self.rollout_jobs, int(expected_rollout_jobs)))
        if expected_policy_fingerprint:
            try:
                expected_fingerprint = bytes.fromhex(
                    expected_policy_fingerprint)
            except ValueError as exc:
                raise RuntimeError(
                    "invalid expected policy fingerprint") from exc
            if expected_fingerprint != self.policy_fingerprint:
                raise RuntimeError(
                    "checkpoint changed after launcher inspection: "
                    "policy fingerprint mismatch")
        # Acquire the sole-writer lease only after read-only checkpoint/model
        # validation.  For in-place resume, re-read under that lease to close
        # the load -> competing commit -> lock TOCTOU window.
        if train:
            self.checkpoint_lock = ExclusiveFileLock(self.out)
            if (checkpoint and
                    os.path.normcase(os.path.abspath(checkpoint)) ==
                    os.path.normcase(os.path.abspath(self.out))):
                try:
                    locked_net, locked_payload = \
                        bc.load_checkpoint_payload(checkpoint)
                    locked_extra = {} if self.reset_lineage else \
                        locked_payload.get("extra", {})
                    locked_online = False if self.reset_lineage else \
                        validate_learner_resume_extra(locked_extra)
                    locked_jobs = int(locked_extra["rollout_jobs"]) \
                        if locked_online else 0
                    locked_fingerprint = module_fingerprint(locked_net)
                    if (locked_jobs != self.rollout_jobs or
                            locked_fingerprint != self.policy_fingerprint):
                        raise RuntimeError(
                            "in-place checkpoint changed before learner "
                            "lock acquisition")
                    del locked_net, locked_payload, locked_extra
                except BaseException:
                    self.checkpoint_lock.close()
                    self.checkpoint_lock = None
                    raise

    @staticmethod
    def _key(connection_id: int, header: wire.Header):
        return connection_id, header.episode, header.owner

    def _connection_rng(self, connection_id: int) -> random.Random:
        rng = self.connection_rngs.get(connection_id)
        if rng is None:
            seed = (self.base_seed + 0x9e3779b1 * (connection_id + 1)) & \
                0x7fffffffffffffff
            rng = random.Random(seed)
            self.connection_rngs[connection_id] = rng
        return rng

    def _connection_torch_rng(self, connection_id: int):
        generator = self.connection_torch_rngs.get(connection_id)
        if generator is None:
            seed = (self.base_seed + 0x517cc1b727220a95 *
                    (connection_id + 1)) & 0x7fffffffffffffff
            generator = self.torch.Generator().manual_seed(seed)
            self.connection_torch_rngs[connection_id] = generator
        return generator

    def hello(self, connection_id: int, hello: wire.HelloBody
              ) -> wire.HelloBody:
        """Pin this whole connection to the current cohort policy."""
        with self.lock:
            version = self.updates
            self.connection_versions[connection_id] = version
            self.connection_fingerprints[connection_id] = \
                self.policy_fingerprint
            for record in hello.owners:
                if record.requested_entity_version not in (
                        0xffffffff, version):
                    raise wire.WireError(
                        "requested entity policy version unavailable")
                requested_sha = record.requested_checkpoint_sha256
                if requested_sha not in (
                        b"\x00" * 32, self.policy_fingerprint):
                    raise wire.WireError(
                        "requested entity checkpoint unavailable")
                record.requested_entity_version = version
                record.requested_checkpoint_sha256 = \
                    self.policy_fingerprint
            return hello

    def _check_version(self, connection_id: int,
                       header: wire.Header) -> None:
        expected = self.connection_versions.get(connection_id)
        if expected is None:
            raise wire.WireError("ACT before HELLO policy pin")
        if header.entity_policy_version != expected:
            raise wire.WireError(
                "entity policy version %d != pinned %d" %
                (header.entity_policy_version, expected))

    def validate_header(self, connection_id: int,
                        header: wire.Header) -> None:
        with self.lock:
            self._check_version(connection_id, header)

    START_CELL_X_FEATURE = 470      # own start cell / 7
    START_CELL_Y_FEATURE = 471
    CANDIDATE_DX_FEATURE = 480      # home -> nearest UNEXPLORED start
    CANDIDATE_DY_FEATURE = 481      # candidate ((d/|d|+1)/2 per axis)
    CANDIDATE_DIST_FEATURE = 482    # |d| / 2048 px, clamped to 1
    CANDIDATE_FLAG_FEATURE = 483

    def _direction_bias(self, global_feat, u):
        """Directed-exploration prior: bias the point head toward the enemy
        base's global 8x8 cell and the command head toward ATTACK_MOVE.
        Point tokens 0..63 and the building-grid features share the same
        row-major cell order.  When NO enemy building is known (honest
        mode before discovery), fall back to the nearest UNEXPLORED start
        candidate's cell so the army sweeps the spawn spots — horizon eval
        2026-09-03 showed dominant armies otherwise never find the base at
        any cap.  Applied to sampling and update recompute identically
        (ppo._apply_direction_bias)."""
        if not self.explore_bias or u == 0:
            return None, None
        grid = global_feat[self.ENEMY_BUILDING_GRID_START:
                           self.ENEMY_BUILDING_GRID_END]
        if len(grid) != 64:
            return None, None
        best = max(range(64), key=lambda c: grid[c])
        known = grid[best] > 0.0
        source = 'base'
        target = best
        if not known:
            target = self._candidate_cell(global_feat)
            source = 'candidate'
            if target is None:
                # Stage 3: candidates all explored yet the base never seen
                # (worker scouts light tiles but die before SIGHTING the
                # buildings — v18 leg3 measured 66% of decisions here).
                # Point the ARMY at the darkest cell: many units sweep with
                # wide vision where a lone scout kept failing.
                target = self._dark_cell(global_feat)
                source = 'dark' if target is not None else 'none'
        self.bias_source_counts[source] += 1
        total = sum(self.bias_source_counts.values())
        if total % 2000 == 0:
            print('ranker_entity_server: bias sources %r'
                  % dict(self.bias_source_counts), flush=True)
        if target is None:
            return None, None
        point_bias = self.torch.zeros(96)
        point_bias[target] = self.explore_bias
        command_bias = None
        if self.explore_bias_command:
            command_bias = self.torch.zeros(6)
            command_bias[1] = self.explore_bias_command   # ATTACK_MOVE (2)
        return point_bias, command_bias

    def _candidate_cell(self, global_feat):
        """Approximate global cell of the nearest unexplored start
        candidate from the home->candidate vector features (a 128x128-tile
        map spans 4096px, so one 8x8 cell is ~512px)."""
        if (len(global_feat) <= self.CANDIDATE_FLAG_FEATURE or
                global_feat[self.CANDIDATE_FLAG_FEATURE] <= 0.5):
            return None
        cx = global_feat[self.START_CELL_X_FEATURE] * 7.0
        cy = global_feat[self.START_CELL_Y_FEATURE] * 7.0
        dx = global_feat[self.CANDIDATE_DX_FEATURE] * 2.0 - 1.0
        dy = global_feat[self.CANDIDATE_DY_FEATURE] * 2.0 - 1.0
        dist_cells = global_feat[self.CANDIDATE_DIST_FEATURE] * 2048.0 / 512.0
        tx = min(7, max(0, int(round(cx + dx * dist_cells))))
        ty = min(7, max(0, int(round(cy + dy * dist_cells))))
        return ty * 8 + tx

    EXPLORED_GRID_START = 406   # 8x8 explored-fraction, row-major
    EXPLORED_GRID_END = 470

    def _dark_cell(self, global_feat):
        """Least-explored global cell, or None once the map is essentially
        lit (then the stalemate is not an exploration problem)."""
        grid = global_feat[self.EXPLORED_GRID_START:self.EXPLORED_GRID_END]
        if len(grid) != 64:
            return None
        best = min(range(64), key=lambda c: grid[c])
        return best if grid[best] < 0.95 else None

    def _approach_phi(self, global_feat):
        """Staged potential for approach shaping.  Base known (feature 479):
        phi = -(distance/2048) in [-1, 0].  Base unknown: a FLAT -1.5, so
        the moment a unit reveals the enemy base phi jumps by >= 0.5 — a
        potential-based discovery bonus (a distance-to-candidate stage would
        PENALIZE clearing the nearest empty candidate, so it stays flat)."""
        if len(global_feat) <= self.APPROACH_FLAG_FEATURE:
            return None
        if global_feat[self.APPROACH_FLAG_FEATURE] > 0.5:
            return -float(global_feat[self.APPROACH_DIST_FEATURE])
        return -1.5

    def _seal(self, key, losses_next, frame_next: int,
              terminal_payoff: float = 0.0, terminated: bool = False,
              truncated: bool = False, phi_next=None) -> None:
        pending = self.pending.pop(key, None)
        if pending is None:
            return
        # ACT_REPLY alone is not enough to make a transition trainable.  If
        # its matching OUTCOME never arrived, executed-action identity is
        # unknown and the open action must be dropped.
        if not pending["outcome_received"]:
            self.dropped_open_actions += 1
            return
        losses_prev = pending["losses"]
        own_delta = (losses_next[0] - losses_prev[0]) + \
            (losses_next[1] - losses_prev[1])
        hostile_delta = (losses_next[2] - losses_prev[2]) + \
            (losses_next[3] - losses_prev[3])
        reward = 5.0 * (hostile_delta - own_delta) / 1000.0 + \
            terminal_payoff
        # Approach shaping (curriculum lever): potential-based reward on
        # closing the distance to the known enemy base.  2026-09-03: this
        # block was silently lost in a concurrent edit and phi became dead
        # code — keep the += HERE, next to the base reward, so any future
        # reward rewrite has to face it.
        if (self.approach_coef and not terminated and
                phi_next is not None and pending.get("phi") is not None):
            reward += self.approach_coef * (phi_next - pending["phi"])
        step = self.ppo.EntityStep(
            global_feat=pending["tensors"]["global_feat"],
            own_cat=pending["tensors"]["own_cat"],
            own_feat=pending["tensors"]["own_feat"],
            command_mask=pending["tensors"]["command_mask"],
            point_mask=pending["tensors"]["point_mask"],
            target_cat=pending["tensors"]["target_cat"],
            target_feat=pending["tensors"]["target_feat"],
            pair_mask=pending["tensors"]["pair_mask"],
            command=pending["command"], point=pending["point"],
            target=pending["target"], trainable=pending["trainable"],
            old_logp=pending["logp"],
            behavior_value=pending["value"], reward=reward,
            dt=float(max(frame_next - pending["frame"], 1)),
            terminal=terminated, truncated=truncated,
            point_bias=pending["tensors"].get("point_bias"),
            command_bias=pending["tensors"].get("command_bias"))
        self.episodes.setdefault(key, []).append(step)

    def act(self, connection_id: int, header: wire.Header,
            request: dict) -> bytes:
        with self.lock:
            self._check_version(connection_id, header)
            torch = self.torch
            key = self._key(connection_id, header)
            phi_next = self._approach_phi(request["global"])
            self._seal(key, request["cumulative_losses"], header.frame,
                       phi_next=phi_next)
            tensors = self.ppo.step_from_request(request, header)
            tensors["point_bias"], tensors["command_bias"] =                 self._direction_bias(request["global"], header.own_rows)
            torch_rng = self._connection_torch_rng(connection_id)
            with torch.no_grad():
                sample = self.ppo.sample_actions(
                    self.net, tensors, torch_rng)
            macro_legal = [i for i in range(wire.MACRO_ACTION_COUNT)
                           if (request["macro_mask_words"][i >> 5] >>
                               (i & 31)) & 1]
            if self.macro_net is not None and len(macro_legal) > 1:
                with torch.no_grad():
                    x = torch.tensor(request["global"],
                                     dtype=torch.float32).unsqueeze(0)
                    action_logits, _, _ = self.macro_net(x)
                    masked = torch.full_like(action_logits, -1e9)
                    for index in macro_legal:
                        masked[0, index] = action_logits[0, index]
                    probs = torch.softmax(masked[0], dim=-1)
                    macro = int(torch.multinomial(
                        probs, 1, generator=torch_rng))
            else:
                rng = self._connection_rng(connection_id)
                macro = rng.choice(macro_legal) if macro_legal else 0
            u = header.own_rows
            self.pending[key] = {
                "tensors": tensors, "command": sample["command"],
                "point": sample["point"], "target": sample["target"],
                "logp": sample["logp"],
                "value": float(sample["value"]),
                "trainable": torch.zeros(u, dtype=torch.bool),
                "outcome_received": False,
                "losses": list(request["cumulative_losses"]),
                "frame": header.frame, "sequence": header.sequence,
                "phi": phi_next,
            }
            return wire.pack_reply(
                macro, -1, [int(c) for c in sample["command"]],
                [int(x) for x in sample["point"]],
                [int(x) for x in sample["target"]])

    def outcome(self, connection_id: int, header: wire.Header,
                outcome: dict) -> None:
        with self.lock:
            self._check_version(connection_id, header)
            key = self._key(connection_id, header)
            pending = self.pending.get(key)
            if pending is None or pending["sequence"] != header.sequence:
                return
            trainable = outcome["trainable"]
            if len(trainable) == pending["trainable"].shape[0]:
                pending["trainable"] = self.torch.tensor(
                    trainable, dtype=self.torch.bool)
                pending["outcome_received"] = True

    def terminal(self, connection_id: int, header: wire.Header,
                 request: dict):
        with self.lock:
            self._check_version(connection_id, header)
            key = self._key(connection_id, header)
            outcome_code = request.get("terminal_outcome", 0)
            terminated = bool(header.flags & wire.FLAG_TERMINATED)
            truncated = bool(header.flags & wire.FLAG_TRUNCATED)
            payoff = {0: 0.0, 1: 6.0, 2: -6.0,
                      3: 0.0}.get(outcome_code, 0.0) if terminated else 0.0
            self._seal(key, request["cumulative_losses"], header.frame,
                       terminal_payoff=payoff, terminated=terminated,
                       truncated=truncated)
            steps = self.episodes.pop(key, [])
            if not self.train or not steps:
                return None
            final_value = 0.0
            if truncated:
                tensors = self.ppo.step_from_request(request, header)
                with self.torch.no_grad():
                    final_value = float(self.net(tensors)["value"])
            return {
                "steps": steps,
                "final_value": final_value,
                "policy_version": self.connection_versions[connection_id],
                "policy_fingerprint":
                    self.connection_fingerprints[connection_id],
                "connection_id": connection_id,
                "episode": header.episode,
                "owner": header.owner,
            }

    def _checkpoint_extra(self, rollout_episodes: int):
        return {
            "learner_state_version": LEARNER_STATE_VERSION,
            "updates": self.updates,
            "global_steps": self.global_steps,
            "rollout_jobs": self.rollout_jobs,
            "base_seed": self.base_seed,
            "environment_seed_base": self.environment_seed_base,
            "issue_prior": self.issue_prior,
            "gate_kl_coef": self.gate_kl_coef,
            "approach_coef": self.approach_coef,
            "explore_bias": self.explore_bias,
            "explore_bias_command": self.explore_bias_command,
            "learning_rate": self.lr,
            "ppo_epochs": self.epochs,
            "max_train_steps_per_worker":
                self.max_train_steps_per_worker,
            "cohort_workers": self.cohort_workers,
            "environment_max_frames": self.environment_max_frames,
            "optimizer": self.optimizer.state_dict(),
            "update_rng_state": self.update_generator.get_state(),
            "rollout_episodes": int(rollout_episodes),
            "policy_fingerprint": self.policy_fingerprint.hex(),
            "macro_fingerprint": self.macro_fingerprint.hex(),
        }

    def _cohort_train_step_cap(self, rollout_job_count: int) -> int:
        return self.max_train_steps_per_worker * int(rollout_job_count)

    def update_batch(self, rollouts, cohort_size: int = None) -> None:
        """Single learner write after every connection in the cohort ends."""
        if not self.train:
            return
        with self.lock:
            rollout_job_count = int(
                cohort_size if cohort_size is not None else
                len({rollout["connection_id"] for rollout in rollouts}))
            if not rollouts:
                # A valid very-short cohort may contain no sealed transition.
                # It performs no optimizer/version step, but its manifest jobs
                # still have to be checkpointed so resume never reuses RNG IDs.
                self.rollout_jobs += rollout_job_count
                self.bc.save_checkpoint(
                    self.net, self.out,
                    extra=self._checkpoint_extra(rollout_episodes=0))
                print("ranker_entity_server: cohort had no sealed steps; "
                      "checkpointed %d rollout jobs -> %s" %
                      (rollout_job_count, self.out), flush=True)
                return
            # Socket/thread completion order must not change how the fixed
            # update RNG maps permutations to behavior samples.
            rollouts = sorted(
                rollouts,
                key=lambda rollout: (
                    rollout["connection_id"],
                    rollout.get("episode", 0),
                    rollout.get("owner", 0)))
            for rollout in rollouts:
                if (rollout["policy_version"] != self.updates or
                        rollout["policy_fingerprint"] !=
                        self.policy_fingerprint):
                    raise RuntimeError(
                        "mixed behavior policies in one PPO cohort")
            episodes = [(rollout["steps"], rollout["final_value"])
                        for rollout in rollouts]
            stats = self.ppo.ppo_update_batched_episodes(
                self.net, self.optimizer, episodes,
                max_train_steps=self._cohort_train_step_cap(
                    rollout_job_count),
                generator=self.update_generator,
                gate_prior=self.issue_prior,
                gate_kl_coef=self.gate_kl_coef, epochs=self.epochs)
            self.updates += 1
            self.global_steps += int(stats["trained_steps"])
            self.rollout_jobs += rollout_job_count
            self.policy_fingerprint = module_fingerprint(self.net)
            self.bc.save_checkpoint(
                self.net, self.out,
                extra=self._checkpoint_extra(stats["episodes"]))
            print("ranker_entity_server: PPO cohort update #%d over %d "
                  "episodes/%d steps %r -> %s" %
                  (self.updates, len(rollouts),
                   sum(len(rollout["steps"]) for rollout in rollouts), stats,
                   self.out), flush=True)

    def cutoff_connection(self, connection_id: int):
        """Retain only prefixes with a fully parsed bootstrap observation."""
        if not self.train:
            return []
        with self.lock:
            rollouts = []
            keys = [key for key in self.episodes
                    if key[0] == connection_id]
            for key in keys:
                steps = self.episodes.pop(key, [])
                pending = self.pending.get(key)
                if not steps or pending is None:
                    continue
                # The pending action itself has no safe next observation and
                # is discarded.  Its already parsed state/value is exactly
                # the bootstrap observation for the preceding sealed prefix.
                steps[-1].cutoff = True
                rollouts.append({
                    "steps": steps,
                    "final_value": float(pending["value"]),
                    "policy_version":
                        self.connection_versions[connection_id],
                    "policy_fingerprint":
                        self.connection_fingerprints[connection_id],
                    "connection_id": connection_id,
                    "episode": key[1],
                    "owner": key[2],
                })
            self.dropped_open_actions += sum(
                1 for key in self.pending if key[0] == connection_id)
            return rollouts

    def abort_connection(self, connection_id: int) -> None:
        with self.lock:
            for storage in (self.pending, self.episodes):
                stale = [key for key in storage if key[0] == connection_id]
                for key in stale:
                    del storage[key]
            self.connection_rngs.pop(connection_id, None)
            self.connection_torch_rngs.pop(connection_id, None)
            self.connection_versions.pop(connection_id, None)
            self.connection_fingerprints.pop(connection_id, None)

    def close(self) -> None:
        if self.checkpoint_lock is not None:
            self.checkpoint_lock.close()


class RandomLegalPolicy:
    """Samples strictly inside the request's command/point/pair masks."""

    def __init__(self, keep_bias: float, rng: random.Random,
                 base_seed: int = 1):
        self.keep_bias = keep_bias
        self.rng = rng
        self.base_seed = int(base_seed)
        self.lock = threading.Lock()
        self.connection_rngs = {}

    def _connection_rng(self, connection_id: int) -> random.Random:
        rng = self.connection_rngs.get(connection_id)
        if rng is None:
            seed = (self.base_seed + 0x9e3779b1 * (connection_id + 1)) & \
                0x7fffffffffffffff
            rng = random.Random(seed)
            self.connection_rngs[connection_id] = rng
        return rng

    def act(self, connection_id: int, header: wire.Header,
            request: dict) -> bytes:
        with self.lock:
            rng = self._connection_rng(connection_id)
            u = header.own_rows
            commands = []
            points = []
            targets = []
            macro_legal = [i for i in range(wire.MACRO_ACTION_COUNT)
                           if (request["macro_mask_words"][i >> 5] >>
                               (i & 31)) & 1]
            macro = rng.choice(macro_legal) if macro_legal else 0
            for row in range(u):
                mask = request["command_mask"][row]
                legal = [c for c in range(wire.COMMAND_COUNT)
                         if (mask >> c) & 1]
                command = COMMAND_KEEP
                if legal and (COMMAND_KEEP not in legal or
                              rng.random() >= self.keep_bias):
                    command = rng.choice(legal)
                point = -1
                target = -1
                if command in _POINT_COMMANDS:
                    words = request["point_mask"][row]
                    legal_points = [t for t in range(wire.POINT_COUNT)
                                    if (words[t >> 5] >> (t & 31)) & 1]
                    if legal_points:
                        point = rng.choice(legal_points)
                    else:
                        command = COMMAND_KEEP
                elif command == COMMAND_ATTACK_UNIT:
                    words = request["attack_pair_mask_words"][row]
                    legal_targets = [t for t in range(header.target_rows)
                                     if (words[t >> 5] >> (t & 31)) & 1]
                    if legal_targets:
                        target = rng.choice(legal_targets)
                    else:
                        command = COMMAND_KEEP
                commands.append(command)
                points.append(point)
                targets.append(target)
            return wire.pack_reply(macro, -1, commands, points, targets)

    def abort_connection(self, connection_id: int) -> None:
        with self.lock:
            self.connection_rngs.pop(connection_id, None)


class AssaultProbePolicy(RandomLegalPolicy):
    """Mechanism probe: EVERY unit is ordered ATTACK_MOVE (else MOVE) at the
    known enemy base's global cell each tick; macro stays random-legal so
    production keeps flowing.  If even this cannot close a far-spawn game,
    the blocker is mechanical (point resolve / order path), not learning."""

    def act(self, connection_id: int, header: wire.Header,
            request: dict) -> bytes:
        with self.lock:
            rng = self._connection_rng(connection_id)
            macro_legal = [i for i in range(wire.MACRO_ACTION_COUNT)
                           if (request["macro_mask_words"][i >> 5] >>
                               (i & 31)) & 1]
            macro = rng.choice(macro_legal) if macro_legal else 0
            grid = request["global"][278:342]
            base_cell = -1
            if len(grid) == 64:
                best = max(range(64), key=lambda c: grid[c])
                if grid[best] > 0.0:
                    base_cell = best
            commands = []
            points = []
            targets = []
            for row in range(header.own_rows):
                mask = request["command_mask"][row]
                command = COMMAND_KEEP
                point = -1
                if base_cell >= 0:
                    words = request["point_mask"][row]
                    cell_legal = (words[base_cell >> 5] >>
                                  (base_cell & 31)) & 1
                    for candidate in (2, 1):   # ATTACK_MOVE, then MOVE
                        if ((mask >> candidate) & 1) and cell_legal:
                            command = candidate
                            point = base_cell
                            break
                commands.append(command)
                points.append(point)
                targets.append(-1)
            return wire.pack_reply(macro, -1, commands, points, targets)



def serve_connection(sock, policy, stats, connection_id: int,
                     reply_timeout_ms: int = 15000):
    """Collect one process, returning all completed owner trajectories."""
    expected_owners = None
    terminal_owners = set()
    sessions = {}
    rollouts = []
    try:
        while True:
            header, payload = recv_frame(sock)
            if header.kind in (wire.KIND_ACT_REQ, wire.KIND_OUTCOME,
                               wire.KIND_TERMINAL):
                if expected_owners is None:
                    raise wire.WireError("gameplay frame before HELLO")
                if header.owner not in expected_owners:
                    raise wire.WireError(
                        "owner %d not negotiated by HELLO" % header.owner)
                if header.owner in terminal_owners:
                    raise wire.WireError(
                        "gameplay frame received after owner TERMINAL")
                if hasattr(policy, "validate_header"):
                    policy.validate_header(connection_id, header)
                session = sessions.setdefault(header.owner, {
                    "episode": header.episode, "last_sequence": 0,
                    "last_frame": 0, "awaiting": None,
                })
                if header.episode != session["episode"]:
                    raise wire.WireError("episode changed inside connection")
                if header.kind == wire.KIND_ACT_REQ:
                    expected_sequence = session["last_sequence"] + 1
                    if (session["awaiting"] is not None or
                            header.sequence != expected_sequence or
                            header.reply_to_sequence !=
                            session["last_sequence"] or
                            header.frame < session["last_frame"]):
                        raise wire.WireError(
                            "ACT sequence/frame/OUTCOME violation")
                    session["last_sequence"] = header.sequence
                    session["last_frame"] = header.frame
                    session["awaiting"] = (
                        header.sequence, header.frame, header.own_rows,
                        header.target_rows)
                elif header.kind == wire.KIND_OUTCOME:
                    expected_outcome = session["awaiting"]
                    if (expected_outcome is None or
                            (header.sequence, header.frame,
                             header.own_rows, header.target_rows) !=
                            expected_outcome or
                            header.reply_to_sequence != header.sequence):
                        raise wire.WireError("OUTCOME join violation")
                    session["awaiting"] = None
                else:  # TERMINAL
                    if (session["awaiting"] is not None or
                            header.owner in terminal_owners or
                            header.sequence != session["last_sequence"] or
                            header.reply_to_sequence != header.sequence or
                            header.frame < session["last_frame"]):
                        raise wire.WireError(
                            "TERMINAL sequence/frame/OUTCOME violation")
            if header.kind == wire.KIND_HELLO:
                if expected_owners is not None:
                    raise wire.WireError("duplicate HELLO")
                hello = wire.parse_hello(payload)
                hello.reply_timeout_ms = int(reply_timeout_ms)
                expected_owners = {record.owner for record in hello.owners}
                if hasattr(policy, "hello"):
                    hello = policy.hello(connection_id, hello)
                ack_header = wire.Header(kind=wire.KIND_ACK,
                                         owner=header.owner,
                                         episode=header.episode,
                                         frame=header.frame)
                # Frozen hostile masks are echoed unchanged; only requested
                # policy-version fields may be pinned by the policy.
                send_frame(sock, ack_header, wire.pack_hello(hello))
                print("ranker_entity_server: connection=%d HELLO owners=%s "
                      "timeout=%dms" %
                      (connection_id, sorted(expected_owners),
                       hello.reply_timeout_ms), flush=True)
            elif header.kind == wire.KIND_ACT_REQ:
                request = wire.parse_act_request(header, payload)
                reply_payload = policy.act(connection_id, header, request)
                reply_header = wire.Header(
                    kind=wire.KIND_ACT_REPLY, flags=header.flags,
                    owner=header.owner, episode=header.episode,
                    frame=header.frame, sequence=header.sequence,
                    reply_to_sequence=header.sequence,
                    own_rows=header.own_rows,
                    target_rows=header.target_rows,
                    entity_policy_version=header.entity_policy_version,
                    macro_policy_version=header.macro_policy_version)
                send_frame(sock, reply_header, reply_payload)
                stats["act"] += 1
                stats["rows"] += header.own_rows
                if stats["act"] % 100 == 1:
                    print("ranker_entity_server: connection=%d act#%d "
                          "frame=%d owner=%d U=%d E=%d" %
                          (connection_id, stats["act"], header.frame,
                           header.owner, header.own_rows,
                           header.target_rows), flush=True)
            elif header.kind == wire.KIND_OUTCOME:
                outcome = wire.parse_outcome(header, payload)
                stats["outcome"] += 1
                stats["issue"] += sum(
                    1 for result in outcome["entity_result"]
                    if result == wire.RESULT_PUBLISHED)
                if hasattr(policy, "outcome"):
                    policy.outcome(connection_id, header, outcome)
            elif header.kind == wire.KIND_TERMINAL:
                terminated = bool(header.flags & wire.FLAG_TERMINATED)
                request = wire.parse_act_request(header, payload,
                                                 terminal=True)
                # ACK first so game teardown never waits for learner work.
                # The actual PPO update is deferred until the cohort barrier.
                ack_header = wire.Header(
                    kind=wire.KIND_ACK, flags=header.flags,
                    owner=header.owner, episode=header.episode,
                    frame=header.frame, sequence=header.sequence,
                    reply_to_sequence=header.reply_to_sequence,
                    own_rows=header.own_rows,
                    target_rows=header.target_rows,
                    entity_policy_version=header.entity_policy_version,
                    macro_policy_version=header.macro_policy_version)
                send_frame(sock, ack_header, b"")
                if hasattr(policy, "terminal"):
                    rollout = policy.terminal(connection_id, header, request)
                    if rollout is not None:
                        rollouts.append(rollout)
                terminal_owners.add(header.owner)
                print("ranker_entity_server: connection=%d TERMINAL "
                      "owner=%d frame=%d outcome=%d%s" %
                      (connection_id, header.owner, header.frame,
                       request.get("terminal_outcome", 0),
                       "" if terminated else " (truncated)"), flush=True)
                if (expected_owners is not None and
                        terminal_owners >= expected_owners):
                    return {"valid": True, "rollouts": rollouts,
                            "error": ""}
            elif header.kind == wire.KIND_ERROR:
                error = wire.parse_error(payload)
                print("ranker_entity_server: connection=%d peer error %d: "
                      "%s" % (connection_id, error["code"],
                              error["message"]), flush=True)
            else:
                raise wire.WireError(
                    "unexpected frame kind %d" % header.kind)
    except (ConnectionError, socket.timeout, wire.WireError) as exc:
        print("ranker_entity_server: connection=%d ended: %s" %
              (connection_id, exc), flush=True)
        if hasattr(policy, "cutoff_connection"):
            rollouts.extend(policy.cutoff_connection(connection_id))
        return {"valid": False, "rollouts": rollouts,
                "error": str(exc)}
    finally:
        if hasattr(policy, "abort_connection"):
            policy.abort_connection(connection_id)


def _serve_client(sock, policy, connection_id: int,
                  reply_timeout_ms: int):
    stats = {"act": 0, "outcome": 0, "issue": 0, "rows": 0}
    try:
        return serve_connection(sock, policy, stats, connection_id,
                                reply_timeout_ms)
    finally:
        print("ranker_entity_server: connection=%d stats %r" %
              (connection_id, stats), flush=True)
        sock.close()


def _open_listener(port: int, accept_timeout: float) -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
        listener.setsockopt(socket.SOL_SOCKET,
                            socket.SO_EXCLUSIVEADDRUSE, 1)
    else:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(1)
    listener.settimeout(accept_timeout)
    return listener


def _accept_worker(listener: socket.socket):
    try:
        return listener.accept()
    except socket.timeout:
        return None


def serve(port: int, policy, episodes: int = 1, workers: int = 1,
          accept_timeout: float = 30.0,
          connection_timeout: float = 900.0,
          reply_timeout_ms: int = 15000) -> int:
    workers = max(1, min(int(workers), int(episodes)))
    worker_ports = [port + slot for slot in range(workers)]
    listeners = []
    try:
        for worker_port in worker_ports:
            listeners.append(_open_listener(worker_port, accept_timeout))
    except Exception:
        for listener in listeners:
            listener.close()
        if hasattr(policy, "close"):
            policy.close()
        raise
    print("ranker_entity_server: listening ports=%s workers=%d" %
          (worker_ports, workers), flush=True)
    print("ranker_entity_server: READY 0", flush=True)
    completed = 0
    rollout_job_base = int(getattr(policy, "rollout_jobs", 0))
    try:
        with ThreadPoolExecutor(max_workers=workers) as client_executor:
            while completed < episodes:
                cohort_size = min(workers, episodes - completed)
                client_futures = {}
                cohort_errors = []
                with ThreadPoolExecutor(
                        max_workers=cohort_size) as accept_executor:
                    accept_futures = {
                        accept_executor.submit(
                            _accept_worker, listeners[slot]): slot
                        for slot in range(cohort_size)
                    }
                    for future in as_completed(accept_futures):
                        slot = accept_futures[future]
                        accepted = future.result()
                        # One listener represents one manifest job.  Close it
                        # immediately so a late/extra client can never enter
                        # this or the next cohort through the same slot.
                        listeners[slot].close()
                        listeners[slot] = None
                        if accepted is None:
                            message = "worker slot=%d accept timeout" % slot
                            cohort_errors.append(message)
                            print("ranker_entity_server: %s" % message,
                                  flush=True)
                            continue
                        sock, address = accepted
                        current_id = rollout_job_base + completed + slot
                        sock.setsockopt(socket.IPPROTO_TCP,
                                        socket.TCP_NODELAY, 1)
                        sock.settimeout(connection_timeout)
                        print("ranker_entity_server: connection=%d slot=%d "
                              "client=%s" %
                              (current_id, slot, address), flush=True)
                        client_futures[slot] = client_executor.submit(
                            _serve_client, sock, policy, current_id,
                            reply_timeout_ms)

                cohort_rollouts = []
                for slot in range(cohort_size):
                    future = client_futures.get(slot)
                    if future is None:
                        continue
                    result = future.result()
                    if not result["valid"]:
                        cohort_errors.append(result["error"])
                    cohort_rollouts.extend(result["rollouts"])
                if cohort_errors:
                    print("ranker_entity_server: COHORT_FAILED %r" %
                          cohort_errors, flush=True)
                    return 2
                if hasattr(policy, "update_batch"):
                    policy.update_batch(cohort_rollouts, cohort_size)
                completed += cohort_size

                # Rebind every next-cohort endpoint before announcing READY.
                # A missing old worker therefore cannot be accepted as a new
                # generation even if its process lingers.
                next_size = min(workers, episodes - completed)
                for slot in range(next_size):
                    listeners[slot] = _open_listener(
                        worker_ports[slot], accept_timeout)
                print("ranker_entity_server: READY %d" % completed,
                      flush=True)
    finally:
        for listener in listeners:
            if listener is not None:
                listener.close()
        if hasattr(policy, "close"):
            policy.close()
    return 0


def selftest() -> None:
    """Socket-level check for the two-worker cohort barrier."""
    class BarrierPolicy:
        def __init__(self):
            self.lock = threading.Lock()
            self.terminals = []
            self.updates = []

        def terminal(self, connection_id, header, request):
            with self.lock:
                self.terminals.append(connection_id)
            return [connection_id], 0.0

        def update_batch(self, rollouts, cohort_size=None):
            with self.lock:
                self.updates.append(list(rollouts))

        def abort_connection(self, connection_id):
            pass

    def empty_request_body():
        body = {
            "global": [0.0] * wire.GLOBAL_COUNT,
            "macro_gate": [0.0, 0.0],
            "macro_mask_words": [1, 0, 0],
            "cumulative_losses": [0, 0, 0, 0],
            "attack_pair_mask_words": [],
        }
        for name, _, _ in wire._OWN_FIELDS:
            body[name] = []
        for name, _, _ in wire._TARGET_FIELDS:
            body[name] = []
        return body

    def connect_with_retry(port):
        deadline = time.monotonic() + 3.0
        while True:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                sock.connect(("127.0.0.1", port))
                sock.settimeout(3.0)
                return sock
            except ConnectionRefusedError:
                sock.close()
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.01)

    def handshake(sock, owners):
        hello = wire.HelloBody(
            controlled_owner_mask=sum(1 << owner for owner in owners),
            owners=[wire.HelloOwnerRecord(owner, 0) for owner in owners])
        send_frame(sock, wire.Header(kind=wire.KIND_HELLO,
                                     owner=0xffffffff, episode=1),
                   wire.pack_hello(hello))
        header, _ = recv_frame(sock)
        assert header.kind == wire.KIND_ACK

    def terminal(sock, owner):
        payload = wire.pack_act_request(
            empty_request_body(), terminal_outcome=wire.TERMINAL_DRAW)
        send_frame(sock, wire.Header(
            kind=wire.KIND_TERMINAL, flags=wire.FLAG_TERMINATED,
            owner=owner, episode=1, frame=8, sequence=0,
            reply_to_sequence=0, own_rows=0, target_rows=0), payload)
        header, _ = recv_frame(sock)
        assert header.kind == wire.KIND_ACK

    while True:
        first_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        first_probe.bind(("127.0.0.1", 0))
        port = first_probe.getsockname()[1]
        second_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            if port == 65535:
                raise OSError("no adjacent port")
            second_probe.bind(("127.0.0.1", port + 1))
            first_probe.close()
            second_probe.close()
            break
        except OSError:
            first_probe.close()
            second_probe.close()
    policy = BarrierPolicy()
    with ThreadPoolExecutor(max_workers=3) as executor:
        server_future = executor.submit(
            serve, port, policy, 2, 2, 2.0, 3.0)
        first = connect_with_retry(port)
        second = connect_with_retry(port + 1)
        handshake(first, [1, 2])
        handshake(second, [1])
        terminal(first, 1)
        terminal(second, 1)
        second.close()
        time.sleep(0.05)
        assert not policy.updates, \
            "learner updated before every owner/worker reached the barrier"
        terminal(first, 2)
        first.close()
        assert server_future.result(timeout=5.0) == 0
    assert sorted(policy.terminals) == [0, 0, 1]
    assert len(policy.updates) == 1 and len(policy.updates[0]) == 3
    assert policy.updates[0] == [([0], 0.0), ([0], 0.0), ([1], 0.0)], \
        "rollout merge order followed thread completion instead of slot order"

    # The same exclusive worker port must be reusable at the next cohort
    # barrier on Windows as well as POSIX.
    rebind_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    rebind_probe.bind(("127.0.0.1", 0))
    rebind_port = rebind_probe.getsockname()[1]
    rebind_probe.close()
    rebind_policy = BarrierPolicy()
    with ThreadPoolExecutor(max_workers=2) as executor:
        rebind_future = executor.submit(
            serve, rebind_port, rebind_policy, 2, 1, 2.0, 3.0)
        for _ in range(2):
            client = connect_with_retry(rebind_port)
            handshake(client, [1])
            terminal(client, 1)
            client.close()
        assert rebind_future.result(timeout=5.0) == 0
    assert len(rebind_policy.updates) == 2

    # An ACT must be joined by its OUTCOME before the next ACT or TERMINAL.
    # Otherwise the reward/GAE stream would silently cross an execution hole.
    session_policy = RandomLegalPolicy(1.0, random.Random(3), base_seed=3)
    server_sock, client_sock = socket.socketpair()
    session_stats = {"act": 0, "outcome": 0, "issue": 0, "rows": 0}
    with ThreadPoolExecutor(max_workers=1) as executor:
        session_future = executor.submit(
            serve_connection, server_sock, session_policy, session_stats, 4)
        handshake(client_sock, [1])
        request_payload = wire.pack_act_request(empty_request_body())
        send_frame(client_sock, wire.Header(
            kind=wire.KIND_ACT_REQ, owner=1, episode=1, frame=8,
            sequence=1, reply_to_sequence=0, own_rows=0,
            target_rows=0), request_payload)
        reply_header, _ = recv_frame(client_sock)
        assert reply_header.kind == wire.KIND_ACT_REPLY
        terminal_payload = wire.pack_act_request(
            empty_request_body(), terminal_outcome=wire.TERMINAL_DRAW)
        send_frame(client_sock, wire.Header(
            kind=wire.KIND_TERMINAL, flags=wire.FLAG_TERMINATED,
            owner=1, episode=1, frame=16, sequence=1,
            reply_to_sequence=1, own_rows=0, target_rows=0),
            terminal_payload)
        invalid_session = session_future.result(timeout=3.0)
    client_sock.close()
    server_sock.close()
    assert not invalid_session["valid"] and \
        "OUTCOME" in invalid_session["error"]

    terminal_policy = RandomLegalPolicy(1.0, random.Random(4), base_seed=4)
    terminal_server, terminal_client = socket.socketpair()
    terminal_stats = {"act": 0, "outcome": 0, "issue": 0, "rows": 0}
    with ThreadPoolExecutor(max_workers=1) as executor:
        terminal_future = executor.submit(
            serve_connection, terminal_server, terminal_policy,
            terminal_stats, 5)
        handshake(terminal_client, [1, 2])
        terminal(terminal_client, 1)
        send_frame(terminal_client, wire.Header(
            kind=wire.KIND_ACT_REQ, owner=1, episode=1, frame=16,
            sequence=1, reply_to_sequence=0, own_rows=0,
            target_rows=0), request_payload)
        post_terminal = terminal_future.result(timeout=3.0)
    terminal_client.close()
    terminal_server.close()
    assert not post_terminal["valid"] and \
        "after owner TERMINAL" in post_terminal["error"]

    missing_policy = BarrierPolicy()
    missing_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    missing_probe.bind(("127.0.0.1", 0))
    missing_port = missing_probe.getsockname()[1]
    missing_probe.close()
    assert serve(missing_port, missing_policy, 1, 1, 0.05, 1.0) == 2
    assert not missing_policy.updates, \
        "missing worker advanced or updated a cohort"

    # Collector state uses connection/episode/owner keys, and an ACT without
    # its matching OUTCOME is discarded rather than trained as all-accepted.
    net_policy = NetPolicy("", random.Random(7), train=False, base_seed=7)

    def pin(connection_id):
        hello = net_policy.hello(connection_id, wire.HelloBody(
            controlled_owner_mask=1 << 1,
            owners=[wire.HelloOwnerRecord(1, 0)]))
        assert hello.owners[0].requested_entity_version == 0
        assert hello.owners[0].requested_checkpoint_sha256 == \
            net_policy.policy_fingerprint

    def act_header(frame, sequence):
        return wire.Header(
            kind=wire.KIND_ACT_REQ, owner=1, episode=1, frame=frame,
            sequence=sequence, own_rows=0, target_rows=0,
            entity_policy_version=0)

    pin(10)
    net_policy.act(10, act_header(8, 1), empty_request_body())
    net_policy.act(10, act_header(16, 2), empty_request_body())
    assert net_policy.dropped_open_actions == 1
    assert not net_policy.episodes
    net_policy.abort_connection(10)

    for connection_id in (20, 21):
        pin(connection_id)
        first_header = act_header(8, 1)
        net_policy.act(connection_id, first_header, empty_request_body())
        net_policy.outcome(connection_id, first_header, {"trainable": []})
        net_policy.act(connection_id, act_header(16, 2),
                       empty_request_body())
    assert len(net_policy.episodes[(20, 1, 1)]) == 1
    assert len(net_policy.episodes[(21, 1, 1)]) == 1
    net_policy.abort_connection(20)
    net_policy.abort_connection(21)

    # Atomic online checkpoint state is resumable and only one learner can
    # own a target path at a time.
    with tempfile.TemporaryDirectory() as directory:
        checkpoint = os.path.join(directory, "entity_online.pt")
        optimizer = net_policy.torch.optim.Adam(
            net_policy.net.parameters(), lr=1e-4)
        update_rng = net_policy.torch.Generator().manual_seed(99)
        net_policy.bc.save_checkpoint(net_policy.net, checkpoint, extra={
            "learner_state_version": LEARNER_STATE_VERSION,
            "updates": 3, "global_steps": 17, "rollout_jobs": 8,
            "base_seed": 7, "issue_prior": None,
            "environment_seed_base": 100,
            "gate_kl_coef": 0.01, "learning_rate": 1e-4,
            "ppo_epochs": 2, "max_train_steps_per_worker": 1024,
            "cohort_workers": 4, "environment_max_frames": 20000,
            "optimizer": optimizer.state_dict(),
            "update_rng_state": update_rng.get_state(),
            "policy_fingerprint": net_policy.policy_fingerprint.hex(),
            "macro_fingerprint": (b"\x00" * 32).hex(),
        })
        resumed = NetPolicy(checkpoint, random.Random(7), train=True,
                            out=checkpoint, base_seed=7,
                            environment_seed_base=100,
                            cohort_workers=4,
                            environment_max_frames=20000)
        assert (resumed.updates, resumed.global_steps,
                resumed.rollout_jobs) == (3, 17, 8)
        assert resumed._cohort_train_step_cap(1) == 1024
        assert resumed._cohort_train_step_cap(4) == 4096
        step = resumed.ppo._synthetic_step(2, 1, seed=91)
        sample = resumed.ppo.sample_actions(
            resumed.net, resumed.ppo._step_tensors(step),
            resumed.torch.Generator().manual_seed(92))
        step.command = sample["command"]
        step.point = sample["point"]
        step.target = sample["target"]
        step.old_logp = sample["logp"]
        step.behavior_value = float(sample["value"])
        step.reward = 0.25
        step.terminal = True
        resumed.update_batch([{
            "steps": [step], "final_value": 0.0,
            "policy_version": 3,
            "policy_fingerprint": resumed.policy_fingerprint,
            "connection_id": 8,
        }], cohort_size=1)
        assert (resumed.updates, resumed.rollout_jobs) == (4, 9)
        _, saved = resumed.bc.load_checkpoint_payload(checkpoint)
        assert saved["extra"]["updates"] == 4
        assert saved["extra"]["rollout_jobs"] == 9
        assert saved["extra"]["policy_fingerprint"] == \
            resumed.policy_fingerprint.hex()
        fingerprint_after_update = resumed.policy_fingerprint
        resumed.update_batch([], cohort_size=1)
        assert (resumed.updates, resumed.rollout_jobs) == (4, 10)
        assert resumed.policy_fingerprint == fingerprint_after_update
        _, saved_empty = resumed.bc.load_checkpoint_payload(checkpoint)
        assert saved_empty["extra"]["rollout_jobs"] == 10
        try:
            NetPolicy(checkpoint, random.Random(7), train=True,
                      out=checkpoint, base_seed=7,
                      environment_seed_base=100,
                      cohort_workers=4,
                      environment_max_frames=20000)
        except RuntimeError as exc:
            assert "active learner" in str(exc)
        else:
            raise AssertionError("second checkpoint writer was accepted")
        resumed.close()

        # Simulate another learner atomically publishing after our first load
        # but before our output-lock acquisition.  The locked re-read must
        # reject stale model/optimizer lineage instead of overwriting it.
        original_load = net_policy.bc.load_checkpoint_payload
        load_count = [0]

        def racing_load(path):
            loaded_net, loaded_payload = original_load(path)
            load_count[0] += 1
            if load_count[0] == 1:
                replacement_extra = dict(loaded_payload["extra"])
                replacement_extra["rollout_jobs"] += 1
                net_policy.bc.save_checkpoint(
                    loaded_net, path, extra=replacement_extra)
            return loaded_net, loaded_payload

        net_policy.bc.load_checkpoint_payload = racing_load
        try:
            try:
                NetPolicy(checkpoint, random.Random(7), train=True,
                          out=checkpoint, base_seed=7,
                          environment_seed_base=100,
                          cohort_workers=4,
                          environment_max_frames=20000)
            except RuntimeError as exc:
                assert "changed before learner lock" in str(exc)
            else:
                raise AssertionError("in-place checkpoint TOCTOU accepted")
        finally:
            net_policy.bc.load_checkpoint_payload = original_load
        try:
            NetPolicy(checkpoint, random.Random(7), train=True,
                      out=checkpoint, base_seed=7,
                      environment_seed_base=101,
                      cohort_workers=4,
                      environment_max_frames=20000)
        except RuntimeError as exc:
            assert "environment seed base" in str(exc)
        else:
            raise AssertionError("resume environment seed mismatch accepted")
        try:
            NetPolicy(checkpoint, random.Random(7), train=True,
                      out=checkpoint, base_seed=7,
                      environment_seed_base=100,
                      cohort_workers=2,
                      environment_max_frames=20000)
        except RuntimeError as exc:
            assert "cohort worker count" in str(exc)
        else:
            raise AssertionError("resume cohort-width change accepted")
        try:
            NetPolicy(checkpoint, random.Random(7), train=True,
                      out=checkpoint, base_seed=7,
                      environment_seed_base=100,
                      cohort_workers=4,
                      environment_max_frames=20000,
                      expected_rollout_jobs=9)
        except RuntimeError as exc:
            assert "changed after launcher inspection" in str(exc)
        else:
            raise AssertionError("stale launcher checkpoint manifest accepted")
        legacy_checkpoint = os.path.join(directory, "legacy_online.pt")
        net_policy.bc.save_checkpoint(
            net_policy.net, legacy_checkpoint, extra={"updates": 1})
        try:
            NetPolicy(legacy_checkpoint, random.Random(7), train=True,
                      out=legacy_checkpoint, base_seed=7,
                      environment_seed_base=100,
                      cohort_workers=4,
                      environment_max_frames=20000)
        except RuntimeError as exc:
            assert "legacy/incomplete" in str(exc)
        else:
            raise AssertionError("incomplete online learner state accepted")
        reset_legacy = NetPolicy(
            legacy_checkpoint, random.Random(7), train=True,
            out=legacy_checkpoint, base_seed=7,
            environment_seed_base=100, cohort_workers=2,
            environment_max_frames=64, reset_lineage=True,
            expected_rollout_jobs=0,
            expected_policy_fingerprint=
            module_fingerprint(net_policy.net).hex())
        assert (reset_legacy.updates, reset_legacy.rollout_jobs) == (0, 0)
        reset_legacy.close()
    print("ranker_entity_server: selftest passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--port", type=int, default=6001,
                        help="first port in the per-worker controller range")
    parser.add_argument("--keep-bias", type=float, default=0.6)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--environment-seed-base", type=int, default=None,
                        help="launcher -SEED base persisted for exact resume")
    parser.add_argument("--environment-max-frames", type=int, default=None,
                        help="launcher horizon persisted for exact resume")
    parser.add_argument("--expected-rollout-jobs", type=int, default=None,
                        help="launcher-inspected checkpoint job counter")
    parser.add_argument("--expected-policy-fingerprint", default="",
                        help="launcher-inspected model SHA-256")
    parser.add_argument("--reset-lineage", action="store_true",
                        help="load model weights but discard learner state")
    parser.add_argument("--policy", default="",
                        help="EntityNet checkpoint (.pt); empty = "
                             "random-legal")
    parser.add_argument("--train", action="store_true",
                        help="online PPO: update once per worker cohort")
    parser.add_argument("--out", default="",
                        help="checkpoint output (default: --policy path)")
    parser.add_argument("--episodes", type=int, default=1,
                        help="connections to serve before exiting")
    parser.add_argument("--workers", type=int, default=1,
                        help="parallel connections per synchronous cohort")
    parser.add_argument("--accept-timeout", type=float, default=30.0,
                        help="seconds to wait for each cohort connection")
    parser.add_argument("--connection-timeout", type=float, default=900.0,
                        help="idle socket timeout for a connected game")
    parser.add_argument("--reply-timeout-ms", type=int, default=15000,
                        help="HELLO-negotiated ACT/TERMINAL reply deadline")
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--probe-assault", action="store_true",
                        help="mechanism probe: all units attack-move at the "
                             "known enemy base cell every tick")
    parser.add_argument("--explore-bias-command", type=float, default=None,
                        help="command-head share of the exploration bias "
                             "(default: same as --explore-bias)")
    parser.add_argument("--explore-bias", type=float, default=0.0,
                        help="directed-exploration logit bias toward the "
                             "known enemy base cell (curriculum lever)")
    parser.add_argument("--approach-coef", type=float, default=0.0,
                        help="potential-based approach shaping toward the "
                             "known enemy base (curriculum closing lever)")
    parser.add_argument("--gate-kl-coef", type=float, default=0.01,
                        help="KEEP-gate KL anchor coefficient (0.01 was "
                             "~50x too weak in v5; 0.5 re-expanded the "
                             "gate in the smoke test)")
    parser.add_argument("--issue-prior", type=float, default=None,
                        help="fresh-net calibrated KEEP gate (e.g. 0.08 = "
                             "the measured teacher ISSUE rate); ignored "
                             "when --policy loads a checkpoint")
    parser.add_argument("--max-train-steps", type=int, default=1024,
                        help="sampled timestep budget per rollout worker")
    parser.add_argument("--macro-policy", default="",
                        help="trained legacy 802->80 macro checkpoint "
                             "(plan 10.1 macro tower); empty = random-legal")
    arguments = parser.parse_args()
    if arguments.selftest:
        selftest()
        return 0
    if arguments.episodes < 1:
        parser.error("--episodes must be at least 1")
    if arguments.workers < 1:
        parser.error("--workers must be at least 1")
    if arguments.epochs < 1 or arguments.max_train_steps < 1:
        parser.error("--epochs and --max-train-steps must be at least 1")
    active_workers = min(arguments.workers, arguments.episodes)
    if not 1 <= arguments.port <= 65535 - active_workers + 1:
        parser.error("--port .. --port+workers-1 must be in 1..65535")
    if arguments.accept_timeout <= 0 or arguments.connection_timeout <= 0:
        parser.error("timeouts must be positive")
    if not 1000 <= arguments.reply_timeout_ms <= 60000:
        parser.error("--reply-timeout-ms must be in 1000..60000")
    if (arguments.environment_seed_base is not None and
            not 1 <= arguments.environment_seed_base <= 0xffffffff):
        parser.error("--environment-seed-base must fit nonzero uint32")
    if (arguments.environment_max_frames is not None and
            arguments.environment_max_frames < 1):
        parser.error("--environment-max-frames must be at least 1")
    if arguments.train and not (arguments.out or arguments.policy):
        parser.error("--train requires --out or --policy")
    resolved_out = os.path.abspath(arguments.out or arguments.policy) \
        if arguments.train else ""
    resolved_policy = os.path.abspath(arguments.policy) \
        if arguments.policy else ""
    if (arguments.train and os.path.exists(resolved_out) and
            (not resolved_policy or
             os.path.normcase(resolved_out) !=
             os.path.normcase(resolved_policy))):
        parser.error(
            "existing --out may only be resumed from the same --policy")
    if arguments.train and arguments.environment_seed_base is None:
        parser.error("--train requires --environment-seed-base")
    if arguments.train and arguments.environment_max_frames is None:
        parser.error("--train requires --environment-max-frames")
    rng = random.Random(arguments.seed)
    if arguments.policy or arguments.train:
        policy = NetPolicy(arguments.policy, rng, train=arguments.train,
                           out=arguments.out, lr=arguments.lr,
                           epochs=arguments.epochs,
                           issue_prior=arguments.issue_prior,
                           gate_kl_coef=arguments.gate_kl_coef,
                           approach_coef=arguments.approach_coef,
                           explore_bias=arguments.explore_bias,
                           explore_bias_command=
                           arguments.explore_bias_command,
                           max_train_steps=arguments.max_train_steps,
                           macro_policy=arguments.macro_policy,
                           base_seed=arguments.seed,
                           environment_seed_base=
                           arguments.environment_seed_base,
                           cohort_workers=arguments.workers,
                           environment_max_frames=
                           arguments.environment_max_frames,
                           reset_lineage=arguments.reset_lineage,
                           expected_rollout_jobs=
                           arguments.expected_rollout_jobs,
                           expected_policy_fingerprint=
                           arguments.expected_policy_fingerprint)
    elif arguments.probe_assault:
        policy = AssaultProbePolicy(arguments.keep_bias, rng,
                                    base_seed=arguments.seed)
    else:
        policy = RandomLegalPolicy(arguments.keep_bias, rng,
                                   base_seed=arguments.seed)
    return serve(arguments.port, policy, arguments.episodes,
                 workers=arguments.workers,
                 accept_timeout=arguments.accept_timeout,
                 connection_timeout=arguments.connection_timeout,
                 reply_timeout_ms=arguments.reply_timeout_ms)


if __name__ == "__main__":
    sys.exit(main())
