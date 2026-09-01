# -*- coding: utf-8 -*-
"""act2 entity-command policy server (plan section 11).

Serves the binary act2 protocol to `ranker_rebuild.exe -AIENTITY:PORT`.
Default policy is random-legal (plumbing validation, the entity analogue of
-AIRANDOM): every sampled command/point/target obeys the request's hard masks
bit-for-bit, KEEP-biased so orders persist long enough to observe engine
feedback.  A trained policy plugs in through the same serve loop later.

Usage:
    python ranker_entity_server.py --port 6001 [--keep-bias 0.6] [--seed 1]
"""

from __future__ import annotations

import argparse
import random
import socket
import struct
import sys

import ranker_entity_contract as wire

COMMAND_KEEP = 0
COMMAND_MOVE = 1
COMMAND_ATTACK_MOVE = 2
COMMAND_PATROL = 3
COMMAND_ATTACK_UNIT = 4
COMMAND_HOLD = 5
COMMAND_STOP = 6
_POINT_COMMANDS = (COMMAND_MOVE, COMMAND_ATTACK_MOVE, COMMAND_PATROL)


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

    def __init__(self, checkpoint: str, rng: random.Random,
                 train: bool = False, out: str = "", lr: float = 1e-4,
                 epochs: int = 2, issue_prior: float = None,
                 max_train_steps: int = 1024):
        import torch
        import ranker_entity_ppo as ppo
        import ranker_entity_bc as bc
        self.torch = torch
        self.ppo = ppo
        self.bc = bc
        self.rng = rng
        self.train = train
        self.out = out or checkpoint
        self.lr = lr
        self.epochs = epochs
        self.max_train_steps = max_train_steps
        # BC-free start: a fresh net takes the calibrated KEEP-gate prior
        # (plan 10.1) so it does not thrash orders from step one.
        self.net = bc.load_checkpoint(checkpoint) if checkpoint else \
            ppo.EntityNet(issue_prior=issue_prior)
        self.optimizer = torch.optim.Adam(self.net.parameters(), lr=lr) \
            if train else None
        self.episodes = {}      # owner -> list[EntityStep]
        self.pending = {}       # owner -> dict(step fields...)
        self.updates = 0

    def _seal(self, owner: int, losses_next, frame_next: int,
              terminal_payoff: float = 0.0, terminated: bool = False,
              truncated: bool = False) -> None:
        pending = self.pending.pop(owner, None)
        if pending is None:
            return
        losses_prev = pending["losses"]
        own_delta = (losses_next[0] - losses_prev[0]) + \
            (losses_next[1] - losses_prev[1])
        hostile_delta = (losses_next[2] - losses_prev[2]) + \
            (losses_next[3] - losses_prev[3])
        reward = 5.0 * (hostile_delta - own_delta) / 1000.0 + \
            terminal_payoff
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
            old_logp=pending["logp"], reward=reward,
            dt=float(max(frame_next - pending["frame"], 1)),
            terminal=terminated, truncated=truncated)
        self.episodes.setdefault(owner, []).append(step)

    def act(self, header: wire.Header, request: dict) -> bytes:
        torch = self.torch
        self._seal(header.owner, request["cumulative_losses"], header.frame)
        tensors = self.ppo.step_from_request(request, header)
        with torch.no_grad():
            sample = self.ppo.sample_actions(self.net, tensors)
        macro_legal = [i for i in range(wire.MACRO_ACTION_COUNT)
                       if (request["macro_mask_words"][i >> 5] >>
                           (i & 31)) & 1]
        # Combat-exposure curriculum for the random macro head: the entity
        # policy only learns when fighters exist, so legal army-production /
        # build actions are preferred 70% of the time (9/30 games of the
        # first run never produced a single fighter under uniform sampling).
        army_macro = [i for i in macro_legal
                      if (1 <= i <= 7) or (14 <= i <= 27)]
        if army_macro and self.rng.random() < 0.7:
            macro = self.rng.choice(army_macro)
        else:
            macro = self.rng.choice(macro_legal) if macro_legal else 0
        u = header.own_rows
        self.pending[header.owner] = {
            "tensors": tensors, "command": sample["command"],
            "point": sample["point"], "target": sample["target"],
            "logp": sample["logp"],
            "trainable": torch.ones(u, dtype=torch.bool),
            "losses": list(request["cumulative_losses"]),
            "frame": header.frame, "sequence": header.sequence,
        }
        return wire.pack_reply(
            macro, -1, [int(c) for c in sample["command"]],
            [int(x) for x in sample["point"]],
            [int(x) for x in sample["target"]])

    def outcome(self, header: wire.Header, outcome: dict) -> None:
        pending = self.pending.get(header.owner)
        if pending is None or pending["sequence"] != header.sequence:
            return
        torch = self.torch
        trainable = outcome["trainable"]
        if len(trainable) == pending["trainable"].shape[0]:
            pending["trainable"] = torch.tensor(trainable,
                                                dtype=torch.bool)

    def terminal(self, header: wire.Header, request: dict) -> None:
        outcome_code = request.get("terminal_outcome", 0)
        terminated = bool(header.flags & wire.FLAG_TERMINATED)
        truncated = bool(header.flags & wire.FLAG_TRUNCATED)
        payoff = {0: 0.0, 1: 6.0, 2: -6.0, 3: 0.0}.get(outcome_code, 0.0) \
            if terminated else 0.0
        self._seal(header.owner, request["cumulative_losses"], header.frame,
                   terminal_payoff=payoff, terminated=terminated,
                   truncated=truncated)
        if not self.train:
            return
        steps = self.episodes.pop(header.owner, [])
        if not steps:
            return
        final_value = 0.0
        if truncated:
            tensors = self.ppo.step_from_request(request, header)
            with self.torch.no_grad():
                final_value = float(self.net(tensors)["value"])
        stats = {}
        for _ in range(self.epochs):
            # Cost-bounded: a full-length match must not stall the accept
            # loop past the next game's HELLO deadline.
            stats = self.ppo.ppo_update_batched(
                self.net, self.optimizer, steps, final_value=final_value,
                max_train_steps=self.max_train_steps)
        self.updates += 1
        self.bc.save_checkpoint(self.net, self.out,
                                extra={"updates": self.updates})
        print("ranker_entity_server: PPO update #%d over %d steps %r -> %s"
              % (self.updates, len(steps), stats, self.out), flush=True)


class RandomLegalPolicy:
    """Samples strictly inside the request's command/point/pair masks."""

    def __init__(self, keep_bias: float, rng: random.Random):
        self.keep_bias = keep_bias
        self.rng = rng

    def act(self, header: wire.Header, request: dict) -> bytes:
        u = header.own_rows
        commands = []
        points = []
        targets = []
        macro_legal = [i for i in range(wire.MACRO_ACTION_COUNT)
                       if (request["macro_mask_words"][i >> 5] >>
                           (i & 31)) & 1]
        macro = self.rng.choice(macro_legal) if macro_legal else 0
        for row in range(u):
            mask = request["command_mask"][row]
            legal = [c for c in range(wire.COMMAND_COUNT)
                     if (mask >> c) & 1]
            command = COMMAND_KEEP
            if legal and (COMMAND_KEEP not in legal or
                          self.rng.random() >= self.keep_bias):
                command = self.rng.choice(legal)
            point = -1
            target = -1
            if command in _POINT_COMMANDS:
                words = request["point_mask"][row]
                legal_points = [t for t in range(wire.POINT_COUNT)
                                if (words[t >> 5] >> (t & 31)) & 1]
                if legal_points:
                    point = self.rng.choice(legal_points)
                else:
                    command = COMMAND_KEEP
            elif command == COMMAND_ATTACK_UNIT:
                words = request["attack_pair_mask_words"][row]
                legal_targets = [t for t in range(header.target_rows)
                                 if (words[t >> 5] >> (t & 31)) & 1]
                if legal_targets:
                    target = self.rng.choice(legal_targets)
                else:
                    command = COMMAND_KEEP
            commands.append(command)
            points.append(point)
            targets.append(target)
        return wire.pack_reply(macro, -1, commands, points, targets)


def serve_connection(sock, policy, stats) -> None:
    while True:
        header, payload = recv_frame(sock)
        if header.kind == wire.KIND_HELLO:
            hello = wire.parse_hello(payload)
            ack_header = wire.Header(kind=wire.KIND_ACK,
                                     owner=header.owner,
                                     episode=header.episode,
                                     frame=header.frame)
            # Echo the owner records byte-for-byte (the client verifies
            # the frozen hostile masks; the server may not change them).
            send_frame(sock, ack_header, wire.pack_hello(hello))
            print("ranker_entity_server: HELLO owners=%s timeout=%dms" %
                  ([o.owner for o in hello.owners],
                   hello.reply_timeout_ms), flush=True)
        elif header.kind == wire.KIND_ACT_REQ:
            request = wire.parse_act_request(header, payload)
            reply_payload = policy.act(header, request)
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
                print("ranker_entity_server: act#%d frame=%d owner=%d "
                      "U=%d E=%d" % (stats["act"], header.frame,
                                     header.owner, header.own_rows,
                                     header.target_rows), flush=True)
        elif header.kind == wire.KIND_OUTCOME:
            outcome = wire.parse_outcome(header, payload)
            stats["outcome"] += 1
            stats["issue"] += sum(
                1 for r in outcome["entity_result"]
                if r == wire.RESULT_PUBLISHED)
            if hasattr(policy, "outcome"):
                policy.outcome(header, outcome)
        elif header.kind == wire.KIND_TERMINAL:
            terminated = bool(header.flags & wire.FLAG_TERMINATED)
            request = wire.parse_act_request(header, payload,
                                             terminal=True)
            # ACK before the (possibly slow) update so the game process can
            # tear down immediately; the driver gates the next launch on the
            # post-update READY line instead.
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
                policy.terminal(header, request)
            print("ranker_entity_server: TERMINAL owner=%d frame=%d "
                  "outcome=%d%s" % (header.owner, header.frame,
                                    request.get("terminal_outcome", 0),
                                    "" if terminated else " (truncated)"),
                  flush=True)
        elif header.kind == wire.KIND_ERROR:
            error = wire.parse_error(payload)
            print("ranker_entity_server: peer error %d: %s" %
                  (error["code"], error["message"]), flush=True)
        else:
            raise wire.WireError("unexpected frame kind %d" % header.kind)


def serve(port: int, policy, episodes: int = 1) -> int:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(1)
    print("ranker_entity_server: listening on 127.0.0.1:%d" % port,
          flush=True)
    for episode in range(episodes):
        sock, address = listener.accept()
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print("ranker_entity_server: episode %d client %s" %
              (episode + 1, address), flush=True)
        stats = {"act": 0, "outcome": 0, "issue": 0, "rows": 0}
        try:
            serve_connection(sock, policy, stats)
        except (ConnectionError, wire.WireError) as exc:
            print("ranker_entity_server: connection ended: %s" % exc,
                  flush=True)
        finally:
            print("ranker_entity_server: stats %r" % (stats,), flush=True)
            sock.close()
            # Launch gate for the driver: everything for this episode
            # (including the PPO update) is done.
            print("ranker_entity_server: READY %d" % (episode + 1),
                  flush=True)
    listener.close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--keep-bias", type=float, default=0.6)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--policy", default="",
                        help="EntityNet checkpoint (.pt); empty = "
                             "random-legal")
    parser.add_argument("--train", action="store_true",
                        help="online PPO: update after every TERMINAL")
    parser.add_argument("--out", default="",
                        help="checkpoint output (default: --policy path)")
    parser.add_argument("--episodes", type=int, default=1,
                        help="connections to serve before exiting")
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--issue-prior", type=float, default=None,
                        help="fresh-net calibrated KEEP gate (e.g. 0.08 = "
                             "the measured teacher ISSUE rate); ignored "
                             "when --policy loads a checkpoint")
    parser.add_argument("--max-train-steps", type=int, default=1024,
                        help="per-update sampled timestep budget")
    arguments = parser.parse_args()
    rng = random.Random(arguments.seed)
    if arguments.policy or arguments.train:
        policy = NetPolicy(arguments.policy, rng, train=arguments.train,
                           out=arguments.out, lr=arguments.lr,
                           epochs=arguments.epochs,
                           issue_prior=arguments.issue_prior,
                           max_train_steps=arguments.max_train_steps)
    else:
        policy = RandomLegalPolicy(arguments.keep_bias, rng)
    return serve(arguments.port, policy, arguments.episodes)


if __name__ == "__main__":
    sys.exit(main())
