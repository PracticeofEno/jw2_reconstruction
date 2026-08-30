"""Expansion-chain probe: a fixed policy that scouts the next expansion site
and expands as soon as the mask allows, so the v7 chain can be verified
end-to-end in one headless game (scout_berry -> site lit -> expand_base_nest
-> second nest).  Everything else: produce workers, else no_op.

    python ranker_expand_probe.py --install-dir <deploy> --seed 1 --max-frames 20000
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from ranker_rl_env import ACTION_NAMES
from ranker_ipc_server import serve_match

A = {name: i for i, name in enumerate(ACTION_NAMES)}


class ExpandProbePolicy:
    def __init__(self):
        self.picks = {"scout_berry": 0, "expand_base_nest": 0}

    def act(self, feat, mask):
        # Save up for the expansion: only a few workers, no other structures.
        workers = feat[13] * 50.0
        for name in ("expand_base_nest", "scout_berry"):
            if mask[A[name]]:
                self.picks[name] += 1
                return A[name]
        if workers < 6 and mask[A["produce_worker"]]:
            return A["produce_worker"]
        return A["no_op"]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", required=True)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-frames", type=int, default=20000)
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args(argv)
    policy = ExpandProbePolicy()
    result = serve_match(Path(args.install_dir), policy, args.port, args.seed,
                         args.max_frames, quiet=True)
    print(json.dumps({"steps": result["steps"], "picks": policy.picks,
                      "end": result["end"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
