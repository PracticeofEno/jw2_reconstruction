"""Merge imitation datasets (ranker_imitation.py --save-dataset .npz files:
X features, y labels, M masks, n_games) into one, so replay-derived and
built-in-AI-derived samples train a single BC prior.  Optional per-input
sample weights (stored as ``w``; ranker_ppo.bc_pretrain honours them) let a
human replay's decisions count more than the built-in AI's.

    python ranker_dataset_merge.py --out merged.npz a.npz b.npz ...
    python ranker_dataset_merge.py --out merged.npz --weights 5,1 replay.npz live.npz
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--weights", type=str, default=None,
                        help="comma-separated per-input sample weight "
                             "(default: all 1)")
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args(argv)
    weights = ([float(v) for v in args.weights.split(",")] if args.weights
               else [1.0] * len(args.inputs))
    if len(weights) != len(args.inputs):
        raise SystemExit("--weights needs one value per input")
    Xs, ys, Ms, ws, n_games = [], [], [], [], 0
    for path, weight in zip(args.inputs, weights):
        with np.load(path) as data:
            X, y, M = np.array(data["X"]), np.array(data["y"]), np.array(data["M"])
            n = int(data["n_games"]) if "n_games" in data.files else 1
        if Xs and X.shape[1] != Xs[0].shape[1]:
            raise SystemExit(f"{path}: feature width {X.shape[1]} != "
                             f"{Xs[0].shape[1]} (contract mismatch)")
        Xs.append(X); ys.append(y); Ms.append(M)
        ws.append(np.full(len(y), weight, dtype=np.float32))
        n_games += n
        print(f"{path.name}: {len(X)} samples, weight {weight}")
    X = np.concatenate(Xs); y = np.concatenate(ys); M = np.concatenate(Ms)
    w = np.concatenate(ws)
    np.savez(args.out, X=X, y=y, M=M, w=w, n_games=n_games)
    print(f"merged -> {args.out}: {len(X)} samples from {n_games} game(s), "
          f"features={X.shape[1]} mask={M.shape[1]} weighted={args.weights is not None}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
