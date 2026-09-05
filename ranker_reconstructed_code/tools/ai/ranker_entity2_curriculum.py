# -*- coding: utf-8 -*-
"""ENTCMD02 opponent-speed curriculum: run the act3 league stage by stage
(built-in AI slowed 24x -> 16x -> 8x -> 4x -> 2x -> full speed), advancing
when the win rate of the last cohorts reaches the threshold and repeating a
stage otherwise.  Every launcher line (per game verdicts, `cohort k/n`,
server update / intent relays) is echoed to the terminal as it appears.

    python ranker_entity2_curriculum.py --install-dir <deploy> \\
        --policy entity2_runs/bc_60k_e1.pt --out-dir entity2_runs/curriculum \\
        --stages 24,16,8,4,2,1 --games 16 --workers 8 [--exe ranker_rebuild_siege.exe]
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time

COHORT_PATTERN = re.compile(r"^cohort (\d+)/(\d+): WIN (\d+) DEATH (\d+) TRUNC (\d+)")


def run_stage(arguments, stage_index: int, opp_slow: int, policy: str, out: str,
              attempt: int, port: int, net_offset: int) -> list:
    """One launcher run; returns the per-cohort (win, death, trunc) list."""
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    command = [sys.executable, os.path.join(tools_dir, "ranker_entity2_train.py"),
               "--install-dir", arguments.install_dir, "--exe", arguments.exe,
               "--policy", policy, "--out", out,
               "--games", str(arguments.games), "--workers", str(arguments.workers),
               "--max-frames", str(arguments.max_frames),
               "--hidden", str(arguments.hidden), "--epochs", str(arguments.epochs),
               "--lr", str(arguments.lr),
               "--max-update-steps", str(arguments.max_update_steps),
               "--issue-cost", str(arguments.issue_cost),
               "--cohorts-per-update", str(arguments.cohorts_per_update),
               "--run-id", "%s_s%d_slow%d_a%d" % (
                   os.path.basename(os.path.normpath(arguments.out_dir)) or "curriculum",
                   stage_index, opp_slow, attempt),
               "--port", str(port), "--net-offset-base", str(net_offset),
               "--game-timeout", str(arguments.game_timeout),
               "--seed0", str(arguments.seed0 + 1000 * stage_index + 100 * attempt),
               "--reset-lineage"]
    if opp_slow > 1:
        command += ["--opp-slow", str(opp_slow)]
    if arguments.issue_prior is not None:
        command += ["--issue-prior", str(arguments.issue_prior)]
    if arguments.slot_keep_prior is not None:
        command += ["--slot-keep-prior", str(arguments.slot_keep_prior)]
    if arguments.economy_issue_prior is not None:
        command += ["--economy-issue-prior", str(arguments.economy_issue_prior)]
    if arguments.gate_kl_coef:
        command += ["--gate-kl-coef", str(arguments.gate_kl_coef)]
    print("== stage %d: opp-slow %d attempt %d policy=%s ==" %
          (stage_index, opp_slow, attempt, os.path.basename(policy)), flush=True)
    cohorts = []
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace")
    assert process.stdout is not None
    for line in process.stdout:
        line = line.rstrip("\n")
        print("  " + line, flush=True)
        match = COHORT_PATTERN.match(line)
        if match:
            cohorts.append((int(match.group(3)), int(match.group(4)), int(match.group(5))))
    code = process.wait()
    if code != 0:
        print("== stage %d attempt %d: launcher exit %d ==" % (stage_index, attempt, code),
              flush=True)
    return cohorts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", required=True)
    parser.add_argument("--exe", default="ranker_rebuild.exe")
    parser.add_argument("--policy", default="",
                        help="starting checkpoint (empty = fresh initialisation)")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--stages", default="24,16,8,4,2,1",
                        help="opponent slow factors, hardest last (1 = full speed)")
    parser.add_argument("--games", type=int, default=16)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--max-frames", type=int, default=60000)
    parser.add_argument("--hidden", type=int, default=128)
    parser.add_argument("--epochs", type=int, default=1)
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--max-update-steps", type=int, default=8000)
    parser.add_argument("--issue-cost", type=float, default=0.001)
    parser.add_argument("--cohorts-per-update", type=int, default=1)
    parser.add_argument("--issue-prior", type=float, default=None,
                        help="fresh-init gate prior P(issue) (and the gate KL target)")
    parser.add_argument("--gate-kl-coef", type=float, default=0.0)
    parser.add_argument("--slot-keep-prior", type=float, default=None,
                        help="fresh-init commander P(KEEP) per slot per tick")
    parser.add_argument("--economy-issue-prior", type=float, default=None,
                        help="fresh-init gate P(issue) for worker / building rows")
    parser.add_argument("--advance-win-rate", type=float, default=0.75,
                        help="win rate of the last --advance-cohorts cohorts to advance")
    parser.add_argument("--advance-cohorts", type=int, default=2)
    parser.add_argument("--max-attempts", type=int, default=6,
                        help="launcher runs per stage before giving up")
    parser.add_argument("--game-timeout", type=float, default=7200.0)
    parser.add_argument("--port", type=int, default=6301)
    parser.add_argument("--net-offset-base", type=int, default=700)
    parser.add_argument("--seed0", type=int, default=100)
    arguments = parser.parse_args()
    stages = [int(s) for s in arguments.stages.split(",") if s.strip()]
    os.makedirs(arguments.out_dir, exist_ok=True)
    policy = arguments.policy
    started = time.time()
    for stage_index, opp_slow in enumerate(stages):
        advanced = False
        for attempt in range(arguments.max_attempts):
            out = os.path.join(arguments.out_dir,
                               "stage%d_slow%d_a%d.pt" % (stage_index, opp_slow, attempt))
            port = arguments.port + 10 * ((stage_index * arguments.max_attempts + attempt) % 20)
            net_offset = arguments.net_offset_base + \
                20 * ((stage_index * arguments.max_attempts + attempt) % 20)
            cohorts = run_stage(arguments, stage_index, opp_slow, policy, out, attempt,
                                port, net_offset)
            if not cohorts or not os.path.exists(out):
                print("== stage %d attempt %d produced no cohorts; stopping ==" %
                      (stage_index, attempt), flush=True)
                return 2
            policy = out
            recent = cohorts[-arguments.advance_cohorts:]
            wins = sum(c[0] for c in recent)
            games = sum(sum(c) for c in recent)
            rate = wins / max(games, 1)
            print("== stage %d (opp-slow %d) attempt %d: last %d cohorts WIN %d/%d (%.0f%%), "
                  "elapsed %.0f min ==" % (stage_index, opp_slow, attempt, len(recent), wins,
                                            games, 100.0 * rate, (time.time() - started) / 60.0),
                  flush=True)
            if rate >= arguments.advance_win_rate:
                advanced = True
                break
        if not advanced:
            print("== stage %d (opp-slow %d) not passed after %d attempts; stopping at %s ==" %
                  (stage_index, opp_slow, arguments.max_attempts, policy), flush=True)
            return 1
        print("== stage %d passed -> next stage with %s ==" % (stage_index, policy), flush=True)
    print("== curriculum complete: %s ==" % policy, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
