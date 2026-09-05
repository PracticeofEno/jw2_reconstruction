"""Online policy server (#5b): drive a live ranker match with a Python policy.

The game (``ranker_rebuild.exe -AISELF -AIIPC:PORT``) connects to this server and
asks for a high-level action every decision cycle; the server answers with the
policy's choice.  The neural net thus lives entirely off-sim (AGENTS.md): the
simulation stays the single deterministic controller and publishes the returned
action as ordered Mode1 packets.  With a deterministic (argmax) policy and a
fixed ``-SEED`` the whole match is reproducible.

Wire format (newline-delimited JSON; array sizes follow the C++ constants,
v8 = 772 features / 64 actions / 64 target cells):
  game -> server : {"t":"act","owner":O,"frame":F,"feat":[...],"mask":[...],
                    "tmask":[...]}
  server -> game : {"action":N} or {"action":N,"target":C}   (v8 spatial cell)
  game -> server : {"t":"end","reason":"...","frame":F}

Usage (server spawns the game itself):
  python ranker_ipc_server.py --install-dir <deploy> --policy imitation_policy.npz \
      --seed 1 --max-frames 4000

This is the runtime harness a self-play RL trainer (#7) builds on: swap the fixed
policy for the policy-being-trained, collect the transition/reward trace the game
already emits, update, repeat.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
from collections import Counter
from pathlib import Path

import numpy as np

from ranker_rl_env import ACTION_NAMES, RandomLegalPolicy


def _load_policy(policy_path, seed, stochastic: bool = False,
                 verbose: bool = True):
    """Load one policy instance.

    Stateful policies (TorchPolicy history, RandomLegalPolicy xorshift state)
    must serve exactly one match at a time — parallel runners call this per
    match (policy factory) instead of sharing one object across a thread pool
    (docs/1순위.md 5.1).  Stochastic torch policies get a PRIVATE generator
    seeded with ``seed`` (not the global torch RNG), so a match's sampling
    stream is reproducible regardless of how the pool schedules matches."""
    if policy_path is None:
        if verbose:
            print("policy: random-legal (no --policy given)")
        return RandomLegalPolicy(seed=seed)
    path = Path(policy_path)
    if path.suffix == ".pt":  # trained PPO / actor-critic checkpoint
        from ranker_ppo import load_policy
        mode = "stochastic" if stochastic else "argmax"
        if verbose:
            print(f"policy: torch actor-critic ({policy_path}, {mode})")
        # Deployment samples from the distribution; a per-policy generator
        # keeps the evaluation reproducible.  (Pure argmax can lock into a
        # repetitive action loop over long games and understate strength.)
        return load_policy(path, deterministic=not stochastic,
                           seed=seed if stochastic else None)
    from ranker_imitation import ImitationPolicy
    if verbose:
        print(f"policy: imitation ({policy_path})")
    return ImitationPolicy(policy_path)


def _recv_lines(conn):
    """Yield complete newline-delimited messages from a socket.

    A recv timeout (the connected socket carries one, see serve_match) ends
    the stream instead of blocking the harness forever on a hung game."""
    buffer = b""
    while True:
        try:
            chunk = conn.recv(65536)
        except (ConnectionError, socket.timeout):
            return
        if not chunk:
            return
        buffer += chunk
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            if line.strip():
                yield line


def serve_match(install_dir: Path | None, policy, port: int, seed: int | None,
                max_frames: int, exe: str = "ranker_rebuild.exe",
                timeout: float = 300.0, out_dir: Path | None = None,
                net_offset: int = 0, quiet: bool = False,
                policy2=None, versus: bool = False,
                opp_tribe: int | None = None,
                extra_flags: list | None = None) -> dict:
    """Serve one match.  With ``versus``/``policy2`` the game runs -AIVS and
    owner 2's decisions are answered by ``policy2`` (owner 1 -> ``policy``), so
    two policies play head-to-head; otherwise owner 2 is the built-in AI.
    ``opp_tribe`` sets the built-in opponent's tribe (-AITRIBE: 0=Primitive
    1=Elf 2=Tyrano 3=Demon, 4=rotate by seed); None keeps the game default."""
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(1)
    server.settimeout(timeout)
    bound_port = server.getsockname()[1]

    proc = None
    if install_dir is not None:
        install = Path(install_dir)
        args = [str(install / exe), "-AISELF", f"-AIIPC:{bound_port}",
                f"-MAXFRAMES:{max_frames}"]
        if versus or policy2 is not None:
            args.append("-AIVS")
        if seed is not None:
            args.append(f"-SEED:{seed}")
        if out_dir is not None:
            Path(out_dir).mkdir(parents=True, exist_ok=True)
            args.append(f"-AIOUT:{Path(out_dir).as_posix()}")
        if net_offset:
            args.append(f"-AINET:{net_offset}")
        if opp_tribe is not None:
            args.append(f"-AITRIBE:{opp_tribe}")
        if extra_flags:
            args.extend(str(flag) for flag in extra_flags)
        if not quiet:
            print(f"launching: {' '.join(args)}")
        proc = subprocess.Popen(args, cwd=str(install))

    if not quiet:
        print(f"listening on 127.0.0.1:{bound_port} ...")
    conn, _ = server.accept()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    # A game that connects and then hangs (crash before the end message,
    # debugger break, ...) must not block the harness forever: bound each
    # recv by the same timeout the accept uses.  Decisions arrive every
    # <=64 sim frames, so a healthy game never comes close.
    conn.settimeout(timeout)
    if not quiet:
        print("game connected")

    hist = Counter()
    steps = 0
    end_info = {}
    try:
        for raw in _recv_lines(conn):
            msg = json.loads(raw)
            if msg.get("t") == "end":
                end_info = msg
                break
            feat = np.asarray(msg["feat"], dtype=np.float32)
            mask = np.asarray(msg["mask"], dtype=np.int8)
            # v8: target-cell legality for the spatial actions (absent on
            # pre-v8 exes).
            tmask = (np.asarray(msg["tmask"], dtype=np.int8)
                     if "tmask" in msg else None)
            # Head-to-head: owner 2's decisions go to policy2 when provided.
            chooser = policy2 if (policy2 is not None and
                                  msg.get("owner") == 2) else policy
            # A v8 policy may return (action, target_cell); everything else
            # returns a bare action and the game falls back to no-cell.
            # v8 policies are stateful per owner (action history), so they
            # also receive the owner id.
            picked = (chooser.act(feat, mask, tmask,
                                  owner=int(msg.get("owner", 1)))
                      if getattr(chooser, "wants_target_mask", False)
                      else chooser.act(feat, mask))
            if isinstance(picked, tuple):
                action, target = int(picked[0]), int(picked[1])
            else:
                action, target = int(picked), -1
            if mask[action] == 0:  # honor legality even if the policy slips
                legal = np.nonzero(mask)[0]
                action = int(legal[0]) if len(legal) else 0
                target = -1
            if target >= 0 and (tmask is None or target >= len(tmask) or
                                tmask[target] == 0):
                target = -1
            reply = {"action": action}
            if target >= 0:
                reply["target"] = target
            conn.sendall((json.dumps(reply) + "\n").encode())
            hist[f"o{msg.get('owner', '?')}:{ACTION_NAMES[action]}"] += 1
            steps += 1
    finally:
        conn.close()
        server.close()
        # Stateful (v8) policies carry per-owner action history; clear it so
        # the next game served with the same policy object starts fresh.
        for chooser in (policy, policy2):
            if chooser is not None and hasattr(chooser, "reset"):
                chooser.reset()
        if proc is not None:
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:  # pragma: no cover
                proc.kill()

    return {"steps": steps, "end": end_info,
            "action_histogram": dict(sorted(hist.items(), key=lambda kv: -kv[1]))}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=None,
                        help="spawn ranker_rebuild.exe here; omit to only listen")
    parser.add_argument("--policy", type=Path, default=None,
                        help="imitation_policy.npz; omit for random-legal")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-frames", type=int, default=4000)
    args = parser.parse_args(argv)

    policy = _load_policy(args.policy, args.seed)
    result = serve_match(args.install_dir, policy, args.port, args.seed,
                         args.max_frames)
    print(f"served {result['steps']} decisions")
    print(f"end: {result['end']}")
    print("action histogram:")
    for name, count in result["action_histogram"].items():
        print(f"  {name:22s} {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
