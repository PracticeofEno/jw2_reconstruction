# -*- coding: utf-8 -*-
"""Entity-mode online training driver (plan section 13.2 steps 4-5).

Loops headless entity-mode matches against the built-in Computer opponent
while one act2 server process (ranker_entity_server.py --train) updates the
policy after every TERMINAL.  Synchronous episodes: the policy version never
changes inside an episode (plan section 12; the server updates only between
connections).

Usage:
    python ranker_entity_train.py --install-dir C:/.../RankerOCPV_Win \
        --policy entity_bc_gen0.pt --out entity_online.pt \
        --games 20 --max-frames 20000 [--port 6001] [--seed0 100]
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", required=True)
    parser.add_argument("--exe", default="ranker_rebuild.exe")
    parser.add_argument("--policy", default="",
                        help="initial checkpoint (empty = fresh net)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--io", default="entity_train_io",
                        help="per-game -AIOUT base directory")
    parser.add_argument("--games", type=int, default=10)
    parser.add_argument("--max-frames", type=int, default=20000)
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--seed0", type=int, default=100)
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--issue-prior", type=float, default=None,
                        help="BC-free start: calibrated KEEP gate prior "
                             "for the fresh net (plan 10.1)")
    parser.add_argument("--max-train-steps", type=int, default=1024,
                        help="per-update sampled timestep budget (server)")
    parser.add_argument("--game-timeout", type=float, default=900.0)
    arguments = parser.parse_args()

    tools_dir = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(arguments.io, exist_ok=True)
    server_log = open(os.path.join(arguments.io, "server.log"), "w")
    server_cmd = [sys.executable,
                  os.path.join(tools_dir, "ranker_entity_server.py"),
                  "--port", str(arguments.port),
                  "--train", "--out", arguments.out,
                  "--episodes", str(arguments.games),
                  "--lr", str(arguments.lr),
                  "--epochs", str(arguments.epochs)]
    if arguments.policy:
        server_cmd += ["--policy", arguments.policy]
    if arguments.issue_prior is not None:
        server_cmd += ["--issue-prior", str(arguments.issue_prior)]
    server_cmd += ["--max-train-steps", str(arguments.max_train_steps)]
    server = subprocess.Popen(server_cmd, cwd=tools_dir, stdout=server_log,
                              stderr=subprocess.STDOUT)
    time.sleep(3.0)
    server_log_path = os.path.join(arguments.io, "server.log")

    def wait_server_ready(count: int, timeout_s: float = 900.0) -> None:
        """Block until the server finished `count` episodes (READY lines):
        a slow post-episode PPO update must never eat the next game's
        HELLO deadline."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if server.poll() is not None:
                raise RuntimeError("act2 server exited early")
            try:
                with open(server_log_path, encoding="utf-8",
                          errors="replace") as handle:
                    ready = sum(1 for line in handle
                                if "READY" in line)
            except FileNotFoundError:
                ready = 0
            if ready >= count:
                return
            time.sleep(1.0)
        raise RuntimeError("server READY %d timeout" % count)

    results = []
    try:
        for game in range(arguments.games):
            if server.poll() is not None:
                raise RuntimeError("act2 server exited early")
            wait_server_ready(game)
            out_dir = os.path.abspath(
                os.path.join(arguments.io, "g%03d" % game))
            game_cmd = [os.path.join(arguments.install_dir, arguments.exe),
                        "-AISELF", "-AIENTITY:%d" % arguments.port,
                        "-MAXFRAMES:%d" % arguments.max_frames,
                        "-SEED:%d" % (arguments.seed0 + game),
                        "-AIOUT:%s" % out_dir]
            process = subprocess.Popen(game_cmd, cwd=arguments.install_dir)
            try:
                process.wait(timeout=arguments.game_timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                print("game %d: TIMEOUT (killed)" % game, flush=True)
                continue
            result_path = os.path.join(out_dir, "ai_selfplay_result.json")
            if os.path.exists(result_path):
                result = json.load(open(result_path, encoding="utf-8"))
                own = next((o for o in result["owners"]
                            if o["owner"] == 1), {})
                results.append(result)
                print("game %d: %s frame=%d units=%s value=%s lost=%s" %
                      (game, result["reason"], result["end_frame"],
                       own.get("units"), own.get("unit_value"),
                       own.get("unit_value_lost")), flush=True)
            else:
                print("game %d: no result json" % game, flush=True)
    finally:
        try:
            server.wait(timeout=15.0)
        except subprocess.TimeoutExpired:
            server.kill()
        server_log.close()
    print("training run complete: %d/%d games -> %s" %
          (len(results), arguments.games, arguments.out), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
