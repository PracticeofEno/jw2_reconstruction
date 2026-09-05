# -*- coding: utf-8 -*-
"""SHD3 (ENTCMD02 feature v3) teacher collection: the v10b legacy stack
(IPC macro + autopilot + micro executor) plays the built-in opponent while
every economy order it publishes (build / produce / research / harvest), the
micro executor's desired combat orders, its group objectives (-> commander
slot labels) and group memberships (-> assign labels) are labelled on the
ENTCMD02 snapshot (-AISHADOW2, plan section 15.1).  Each record carries the
teacher-forced ledger replays (economy + SCOUT assign) so the reader can
verify the same-tick budget and assign masks byte-for-byte.

    python collect_entity2_shadow.py --install-dir <deploy> --out <dir> \\
        --games 8 --opp-slow 16 --max-frames 60000 [--assault]
"""
from __future__ import annotations

import argparse
import json
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from ranker_ipc_server import serve_match, _load_policy
from collect_assault_shadow import AssaultTeacher


def one_game(install: Path, policy_path: Path, port: int, seed: int,
             max_frames: int, index: int, out_root: Path, opp_slow: int,
             assault: bool):
    inner = _load_policy(policy_path, seed, stochastic=False)
    policy = AssaultTeacher(inner) if assault else inner
    out_dir = (out_root / f"g{index}").resolve()
    flags = ["-AISHADOW2"]
    if opp_slow > 1:
        flags.append(f"-AIOPPSLOW:{opp_slow}")
    info = serve_match(install, policy, port, seed, max_frames,
                       out_dir=out_dir, net_offset=400 + index, quiet=True,
                       extra_flags=flags, timeout=3600.0)
    result = {}
    result_path = out_dir / "ai_selfplay_result.json"
    if result_path.exists():
        result = json.loads(result_path.read_text())
    owners = {o["owner"]: o for o in result.get("owners", [])}
    we, opp = owners.get(1, {}), owners.get(2, {})
    win = (not opp.get("alive", True)) and bool(we.get("alive"))
    shadow = sorted(out_dir.glob("ai_entity2_shadow_*.bin"))
    return {
        "index": index, "seed": seed, "win": win,
        "end_frame": result.get("end_frame"),
        "reason": result.get("reason"),
        "we_units": we.get("units"), "opp_units": opp.get("units"),
        "shadow_bytes": sum(p.stat().st_size for p in shadow),
        "shadow_files": [str(p) for p in shadow],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--policy", type=Path, default=None,
                        help="legacy macro checkpoint (default: v10b champion)")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--games", type=int, default=8)
    parser.add_argument("--seed0", type=int, default=7300)
    parser.add_argument("--port", type=int, default=5900)
    parser.add_argument("--opp-slow", type=int, default=16)
    parser.add_argument("--max-frames", type=int, default=60000)
    parser.add_argument("--assault", action="store_true",
                        help="force base-assault objectives after the opening")
    parser.add_argument("--verify", action="store_true",
                        help="parse every record with the SHD3 reader afterwards")
    args = parser.parse_args()

    policy_path = args.policy or (args.install_dir / "ppo_policy.selfplay.v10b.pt")
    args.out.mkdir(parents=True, exist_ok=True)
    def describe(r):
        return "g%d seed %d f%s %-10s WE u%s | OPP u%s | shadow %dB -> %s" % (
            r["index"], r["seed"], r["end_frame"], str(r["reason"])[:10],
            r["we_units"], r["opp_units"], r["shadow_bytes"],
            "WIN" if r["win"] else "no")

    results = []
    with ThreadPoolExecutor(max_workers=args.games) as pool:
        futures = [pool.submit(one_game, args.install_dir, policy_path,
                               args.port + i, args.seed0 + i, args.max_frames, i,
                               args.out, args.opp_slow, args.assault)
                   for i in range(args.games)]
        # Report every game the moment it ends (the batch is one cohort).
        for future in as_completed(futures):
            r = future.result()
            results.append(r)
            print("done %d/%d: %s" % (len(results), args.games, describe(r)), flush=True)
    wins = sum(r["win"] for r in results)
    print("== cohort summary ==")
    for r in sorted(results, key=lambda r: r["index"]):
        print(describe(r))
    print("wins %d/%d" % (wins, len(results)), flush=True)
    if args.verify:
        import ranker_entity2_bc as bc
        files = [f for r in results for f in r["shadow_files"]]
        records = bc.load_shadow_records(files)
        print("shd3 verify: %s" % bc.dataset_stats(records))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
