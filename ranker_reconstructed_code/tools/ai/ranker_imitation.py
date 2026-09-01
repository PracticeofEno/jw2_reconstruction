"""Imitation learning (#6): behavior-clone the built-in Owner AI into the
high-level RL policy.

The built-in Owner AI is strong but plays via direct in-sim, god-mode commands
that do not map to our 15 high-level actions.  So we imitate *observationally*:
run ``ranker_rebuild.exe -AISELF -AIIMITATE`` (the Computer(AI) owner stays on the
built-in AI; each decision cycle its RL observation is logged to
``ai_rl_observe.jsonl``), then infer the effective high-level action for each
cycle from the state deltas to the next cycle, and train a masked classifier to
predict it.

Label inference (priority order — the rarest strategic event wins):
  expand_base > land nest > egg nest > pop nest        (a new building started)
  > dilophos > masos > worker                          (a new unit started)
  > attack_nearest_enemy                               (army closing on a foe)
  > no_op                                              (nothing changed)

research_next is not inferable from the current feature vector (no research
feature) and is left unlabeled — a known gap noted for a later feature add.

The trained policy is the seam a self-play RL run (#7) starts from; here it is a
numpy masked multinomial logistic regression (no torch dependency).  Saved to
``imitation_policy.npz``.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Sequence

import numpy as np

from ranker_rl_env import ACTION_NAMES, N_ACTIONS, N_FEATURES

# Action indices, derived from ACTION_NAMES so a reordered/removed action can
# never silently shift a label (v4 removed harvest_saturate).
_A = ACTION_NAMES.index
A_NO_OP = _A("no_op")
A_PRODUCE_WORKER = _A("produce_worker")
A_PRODUCE_MASOS = _A("produce_masos")
A_PRODUCE_DILOPHOS = _A("produce_dilophos")
A_BUILD_POP = _A("build_population_nest")
A_BUILD_EGG = _A("build_egg_nest")
A_BUILD_LAND = _A("build_land_nest")
A_EXPAND_BASE = _A("expand_base_nest")
A_SCOUT = _A("scout_map")
A_ATTACK_NEAREST = _A("attack_nearest_enemy")
A_ATTACK_BASE = _A("attack_enemy_base")
A_DEFEND = _A("defend_base")
A_RETREAT = _A("retreat")
A_HUNT_NEUTRAL = _A("hunt_neutral_monster")
A_PRODUCE_X22 = _A("produce_unit_x22")
A_BUILD_X86 = _A("build_nest_x86")
A_BUILD_X87 = _A("build_nest_x87")
A_PRODUCE_X25 = _A("produce_unit_x25")
A_PRODUCE_X27 = _A("produce_unit_x27")
A_PRODUCE_X28 = _A("produce_unit_x28")
A_PRODUCE_X2E = _A("produce_unit_x2e")
A_PRODUCE_X2C = _A("produce_unit_x2c")
A_PRODUCE_X29 = _A("produce_unit_x29")
A_PRODUCE_X2A = _A("produce_unit_x2a")
A_BUILD_X83 = _A("build_nest_x83")
A_BUILD_X88 = _A("build_nest_x88")
A_BUILD_X89 = _A("build_nest_x89")
A_BUILD_X8A = _A("build_nest_x8a")
A_MERGE_VELOCIS = _A("merge_twin_velocis")
A_MERGE_RHAMPOS = _A("merge_twin_rhampos")
A_MERGE_PTERAS = _A("merge_twin_pteras")
A_MERGE_MUTANT = _A("merge_mutant")
A_MORPH_ENTER = _A("morph_enter_army")
A_MORPH_EXIT = _A("morph_exit_army")
A_STANCE_ON = _A("stance_on_army")
A_STANCE_OFF = _A("stance_off_army")
A_DROP_ATTACK = _A("drop_attack")
# Per-order research: (feature index of the order's completed level, action).
# Feature 36..38 = tracked orders 0x14/0x16/0x19; 46..55 = the v2 research row
# 0x1a,0x1c,0x1d,0x18,0x2a,0x38,0x2b,0x1b,0x2d,0x1e.
RESEARCH_FEATURES = (
    (36, _A("research_harvest")),
    (37, _A("research_movement")),
    (38, _A("research_ground_attack")),
    (46, _A("research_ground_defense")),
    (47, _A("research_air_attack")),
    (48, _A("research_air_defense")),
    (49, _A("research_mutant_merge")),
    (50, _A("research_morph")),
    (51, _A("research_haste")),
    (52, _A("research_exp_down")),
    (53, _A("research_melee_reinforce")),
    (54, _A("research_triceps_speed")),
    (55, _A("research_air_reinforce")),
)


def load_observations(path: str | Path, owner: int | None = 1
                      ) -> list[tuple[int, np.ndarray, np.ndarray, int]]:
    """Load ai_rl_observe.jsonl -> [(frame, features, mask, label)].

    ``label`` is the exact action decoded from the owner's own packets
    (replay imitation), -1 when the game could not decode one — the dataset
    builder then falls back to delta inference.  ``owner=None`` accepts every
    logged owner (a replay logs exactly one)."""
    out = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if owner is not None and row["owner"] != owner:
                continue
            feat = np.asarray(row["feat"], dtype=np.float32)
            mask = np.asarray(row["mask"], dtype=np.int8)
            if feat.shape[0] != N_FEATURES:
                raise ValueError(f"bad feature length {feat.shape[0]}")
            # v9 decision-gate context (0 on pre-v9 logs): trigger bitmask +
            # frames since the previous sample.
            out.append((row["f"], feat, mask, int(row.get("label", -1)),
                        int(row.get("why", 0)), int(row.get("dt", 0))))
    return out


def collect_replay_game(install_dir: Path, replay_path: Path,
                        owner: int | None = None, max_frames: int = 100000,
                        exe: str = "ranker_rebuild.exe",
                        timeout: float = 1800.0) -> list:
    """Play a recorded .ply back headless (-AIREPLAY) with imitation logging
    of the recorded player (default: the replay's own local player) and return
    its samples, each with the packet-decoded exact label."""
    install = Path(install_dir)
    obs_path = install / "ai_rl_observe.jsonl"
    try:
        obs_path.unlink()
    except FileNotFoundError:
        pass
    args = [str(install / exe), "-AISELF", f"-AIREPLAY:{Path(replay_path)}",
            "-AIIMITATE", f"-MAXFRAMES:{max_frames}"]
    if owner is not None:
        args.append(f"-AIIMITOWNER:{owner}")
    proc = subprocess.Popen(args, cwd=str(install))
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:  # pragma: no cover
        proc.kill()
        proc.wait()
    if not obs_path.exists():
        raise RuntimeError(f"no observation log at {obs_path} for {replay_path}")
    return load_observations(obs_path, owner=None)


def collect_imitation_game(install_dir: Path, seed: int,
                           max_frames: int = 4000, exe: str = "ranker_rebuild.exe",
                           timeout: float = 300.0,
                           opp_tribe: int | None = 4) -> list:
    """Run one -AIIMITATE game and return its observation samples.

    ``opp_tribe`` feeds -AITRIBE (default 4 = rotate by seed) so the observed
    built-in Tyrano AI plays against every tribe, not only the Tyrano mirror.
    """
    install = Path(install_dir)
    obs_path = install / "ai_rl_observe.jsonl"
    try:
        obs_path.unlink()
    except FileNotFoundError:
        pass
    args = [str(install / exe), "-AISELF", "-AIIMITATE",
            f"-MAXFRAMES:{max_frames}", f"-SEED:{seed}"]
    if opp_tribe is not None:
        args.append(f"-AITRIBE:{opp_tribe}")
    proc = subprocess.Popen(args, cwd=str(install))
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:  # pragma: no cover
        proc.kill()
        proc.wait()
    if not obs_path.exists():
        raise RuntimeError(f"no observation log at {obs_path}")
    return load_observations(obs_path)


# --- Un-normalize the feature counts we diff (mirror the encoder scales) ---

def _counts(f: np.ndarray) -> dict:
    r = lambda x: int(round(float(x)))
    return {
        "workers": r(f[13] * 50 + f[14] * 10),
        "masos": r(f[15] * 50 + f[16] * 10),
        "dilophos": r(f[17] * 50 + f[18] * 10),
        "base": r(f[19] * 10 + f[20] * 5),
        "pop": r(f[21] * 10 + f[22] * 5),
        "egg": r(f[23] * 10 + f[24] * 5),
        "land": r(f[25] * 10 + f[26] * 5),
        "harvesting": r(f[27] * 50),
        "idle_workers": r(f[28] * 50),
        "army": r(f[29] * 50),
        # Research (f[36..38] = level/3) and neutral monster count (f[39]*20).
        "research": r(f[36] * 3 + f[37] * 3 + f[38] * 3),
        "neutral": r(f[39] * 20),
        # Tech-tree extension: mid-tier fighter and late buildings.
        "unit22": r(f[42] * 50 + f[43] * 10),
        "nest86": r(f[44] * 5),
        "nest87": r(f[45] * 5),
        # v2 completed roster counts [56..66] (0x23,0x25,0x26,0x27,0x28,0x29,
        # 0x2a,0x2b,0x2c,0x2d,0x2e; 0x2b/0x2c scale 10, others 50).
        "twin_velocis": r(f[56] * 50),
        "rhampos": r(f[57] * 50),
        "twin_rhampos": r(f[58] * 50),
        "pteras": r(f[59] * 50),
        "triceps": r(f[60] * 50),
        "carrier": r(f[61] * 50),
        "egg_thrower": r(f[62] * 50),
        "mutant": r(f[63] * 10),
        "tyranos": r(f[64] * 10),
        "twin_pteras": r(f[65] * 50),
        "kentros": r(f[66] * 50),
        # v2 extra buildings [67..70] (scale 5) and mechanic aggregates.
        "nest83": r(f[67] * 5),
        "nest88": r(f[68] * 5),
        "nest89": r(f[69] * 5),
        "nest8a": r(f[70] * 5),
        "stance_active": r(f[71] * 20),
        "morphed": r(f[73] * 20),
        "passengers": r(f[74] * 14),
    }


def infer_label(cur: np.ndarray, nxt: np.ndarray) -> int:
    """Infer the high-level action taken in state `cur` from the delta to `nxt`.

    Labels are rising/falling edges of the built-in AI's observable state.
    Completed-count features (roster, buildings, research) label the
    COMPLETION cycle, an approximate but usable "do this here" signal.  Not
    inferable and never labeled: scout_map, defend_base, hold_army,
    patrol_defense (no observable delta) — RL exploration owns those.  Since
    v4 idle workers are the micro executor's job, so they no longer label
    anything (harvest_saturate was removed from the action set)."""
    a, b = _counts(cur), _counts(nxt)
    d = {k: b[k] - a[k] for k in a}

    # A new building that came into being (rising edge of count+uc).
    if d["base"] > 0:
        return A_EXPAND_BASE
    if d["nest86"] > 0:
        return A_BUILD_X86
    if d["nest87"] > 0:
        return A_BUILD_X87
    if d["nest88"] > 0:
        return A_BUILD_X88
    if d["nest89"] > 0:
        return A_BUILD_X89
    if d["nest8a"] > 0:
        return A_BUILD_X8A
    if d["nest83"] > 0:
        return A_BUILD_X83
    if d["land"] > 0:
        return A_BUILD_LAND
    if d["egg"] > 0:
        return A_BUILD_EGG
    if d["pop"] > 0:
        return A_BUILD_POP
    # A research/upgrade completed (level rising edge) -> that order's action.
    for feature_index, action in RESEARCH_FEATURES:
        if nxt[feature_index] > cur[feature_index] + 1e-4:
            return action
    # Merges: a twin/mutant appears (only merges create these types).
    if d["mutant"] > 0:
        return A_MERGE_MUTANT
    if d["twin_velocis"] > 0:
        return A_MERGE_VELOCIS
    if d["twin_rhampos"] > 0:
        return A_MERGE_RHAMPOS
    if d["twin_pteras"] > 0:
        return A_MERGE_PTERAS
    # Units completed (rarest / most strategic first).
    if d["tyranos"] > 0:
        return A_PRODUCE_X2C
    if d["egg_thrower"] > 0:
        return A_PRODUCE_X2A
    if d["carrier"] > 0:
        return A_PRODUCE_X29
    if d["triceps"] > 0:
        return A_PRODUCE_X28
    if d["kentros"] > 0:
        return A_PRODUCE_X2E
    if d["pteras"] > 0:
        return A_PRODUCE_X27
    if d["rhampos"] > 0:
        return A_PRODUCE_X25
    if d["dilophos"] > 0:
        return A_PRODUCE_DILOPHOS
    if d["unit22"] > 0:
        return A_PRODUCE_X22
    if d["masos"] > 0:
        return A_PRODUCE_MASOS
    if d["workers"] > 0:
        return A_PRODUCE_WORKER
    # Mechanics: morph / stance / boarding edges.
    if d["morphed"] > 0:
        return A_MORPH_ENTER
    if d["morphed"] < 0 and b["army"] >= a["army"]:
        return A_MORPH_EXIT
    if d["stance_active"] > 0:
        return A_STANCE_ON
    if d["stance_active"] < 0 and b["army"] >= a["army"]:
        return A_STANCE_OFF
    if d["passengers"] > 0:
        return A_DROP_ATTACK

    # Army closing on a visible enemy (feature 34 = nearest distance, 35 = has).
    has_enemy = nxt[35] > 0.5
    closing = has_enemy and (nxt[34] < cur[34] - 1e-3)
    if a["army"] > 0 and closing:
        return A_ATTACK_NEAREST
    # Army backing away from a visible enemy while it is still in sight.
    opening = has_enemy and cur[35] > 0.5 and (nxt[34] > cur[34] + 1e-3)
    if a["army"] > 0 and opening:
        return A_RETREAT

    # Army closing on / killing a neutral monster (feat 40 = nearest neutral
    # distance, 41 = has neutral).
    has_neutral = nxt[41] > 0.5
    neutral_closing = has_neutral and (nxt[40] < cur[40] - 1e-3)
    if a["army"] > 0 and (d["neutral"] < 0 or neutral_closing):
        return A_HUNT_NEUTRAL

    return A_NO_OP


def build_dataset(samples: Sequence[tuple[int, np.ndarray, np.ndarray]]):
    """Turn a per-game observation series into (X, y, mask) arrays.

    The label for step t is inferred from the delta to step t+1, so the last
    step of each game is dropped.
    """
    X, y, M = [], [], []
    for t in range(len(samples) - 1):
        _, feat, mask, exact = samples[t][:4]
        nxt = samples[t + 1][1]
        # Exact packet-decoded label (replay imitation) beats delta inference.
        label = int(exact) if exact >= 0 else infer_label(feat, nxt)
        # A label illegal in its own state is usually a COMPLETION edge seen
        # too late: the build/research was paid for long before it completed,
        # so at the edge the mask has already closed (cost).  Dropping the
        # sample erased every tech-build label from the dataset (2026-09-01
        # user replay report: the policy never learned to tech).  Instead,
        # walk BACK to the nearest earlier no_op sample of the same game
        # where the action WAS legal - approximately where the built-in AI
        # actually decided it - and label that sample.
        if mask[label] == 0 and label != A_NO_OP:
            moved = False
            for back in range(len(y) - 1, max(len(y) - 16, -1), -1):
                if y[back] == A_NO_OP and M[back][label] != 0:
                    y[back] = label
                    moved = True
                    break
            if not moved:
                continue
            label = A_NO_OP  # this sample itself stays a quiet step
        X.append(feat)
        y.append(label)
        M.append(mask)
    y_arr = np.asarray(y, dtype=np.int64)
    if len(y_arr):
        # Label-mix summary per game: the v9 decision gate exists to push the
        # no_op share down from the 73% of the fixed-8-frame logger.
        no_op_share = float((y_arr == A_NO_OP).mean())
        print(f"    labels: {len(y_arr)} (no_op {no_op_share:.1%})", flush=True)
    return (np.asarray(X, dtype=np.float32),
            y_arr,
            np.asarray(M, dtype=np.int8))


# --- Masked multinomial logistic regression (behavior cloning) ---

def _masked_softmax(logits: np.ndarray, mask: np.ndarray) -> np.ndarray:
    neg = np.where(mask > 0, logits, -1e9)
    neg = neg - neg.max(axis=1, keepdims=True)
    e = np.exp(neg)
    return e / np.clip(e.sum(axis=1, keepdims=True), 1e-12, None)


def class_weights(y, power: float = 0.5) -> np.ndarray:
    """Inverse-frequency weights so rare strategic actions are not drowned out by
    the dominant no_op/harvest classes.  Normalized to mean 1."""
    w = np.ones(N_ACTIONS, dtype=np.float32)
    vals, cnts = np.unique(y, return_counts=True)
    freq = {int(v): int(c) for v, c in zip(vals, cnts)}
    total = len(y)
    for cls in range(N_ACTIONS):
        c = freq.get(cls, 0)
        if c > 0:
            w[cls] = (total / (len(vals) * c)) ** power
    # Normalize over present classes to keep the effective learning rate stable.
    present = [c for c in range(N_ACTIONS) if freq.get(c, 0) > 0]
    mean = np.mean([w[c] for c in present])
    w /= max(mean, 1e-6)
    return w


def train_bc(X, y, M, epochs: int = 300, lr: float = 0.5, l2: float = 1e-4,
             seed: int = 0, weights: np.ndarray | None = None):
    rng = np.random.default_rng(seed)
    n, f = X.shape
    W = rng.normal(0, 0.01, size=(f, N_ACTIONS)).astype(np.float32)
    b = np.zeros(N_ACTIONS, dtype=np.float32)
    onehot = np.zeros((n, N_ACTIONS), dtype=np.float32)
    onehot[np.arange(n), y] = 1.0
    if weights is None:
        weights = np.ones(N_ACTIONS, dtype=np.float32)
    sample_w = weights[y].reshape(-1, 1)  # (n, 1)
    wsum = float(sample_w.sum())
    for _ in range(epochs):
        p = _masked_softmax(X @ W + b, M)
        grad_logits = sample_w * (p - onehot) / wsum
        gW = X.T @ grad_logits + l2 * W
        gb = grad_logits.sum(axis=0)
        W -= lr * gW
        b -= lr * gb
    return W, b


def evaluate(W, b, X, y, M):
    p = _masked_softmax(X @ W + b, M)
    pred = p.argmax(axis=1)
    acc = float((pred == y).mean())
    # Per-class recall for the classes that appear.
    per_class = {}
    for cls in np.unique(y):
        idx = y == cls
        per_class[ACTION_NAMES[cls]] = float((pred[idx] == cls).mean())
    return acc, pred, per_class


class ImitationPolicy:
    """The behavior-cloned policy as a plug-in for the RL loop.

    Implements the ``Policy`` protocol from ranker_rl_env (``act(features,
    mask)``), so it drops straight into ``RankerSelfPlayEnv`` and is the warm
    start for self-play RL (#7): the online IPC hook queries this per decision.
    """

    def __init__(self, path: str | Path):
        data = np.load(path, allow_pickle=True)
        self.W = data["W"].astype(np.float32)
        self.b = data["b"].astype(np.float32)

    def logits(self, features, mask):
        x = np.asarray(features, dtype=np.float32)
        z = x @ self.W + self.b
        return np.where(np.asarray(mask) > 0, z, -1e9)

    def action_probs(self, features, mask):
        return _masked_softmax(self.logits(features, mask)[None, :],
                               np.asarray(mask)[None, :])[0]

    def act(self, features, mask) -> int:
        return int(self.logits(features, mask).argmax())


def _hist(y) -> dict:
    out = {}
    for cls, cnt in zip(*np.unique(y, return_counts=True)):
        out[ACTION_NAMES[int(cls)]] = int(cnt)
    return out


def split_train_test(n: int, groups=None, frac: float = 0.8, seed: int = 0):
    """Episode-level train/test split (docs/1순위.md 5.2).

    Two consecutive RTS decision states are nearly identical, so a random
    TRANSITION split leaks each test state's near-duplicate into train and the
    test accuracy stops measuring generalization.  With per-row ``groups``
    (game ids) whole games go to one side: games are shuffled (seeded) and
    accumulated into train until ``frac`` of the samples are covered, keeping
    at least one game on each side.  Without groups (old caches, single game)
    falls back to a CONTIGUOUS time cut — still no adjacent-state leakage
    beyond the single boundary.  Returns (train_idx, test_idx)."""
    if groups is not None:
        groups = np.asarray(groups)
        unique = np.unique(groups)
        if len(unique) >= 2:
            rng = np.random.default_rng(seed)
            order = rng.permutation(unique)
            counts = {int(g): int((groups == g).sum()) for g in unique}
            train_games, covered = [], 0
            for g in order:
                if covered < frac * n and len(order) - len(train_games) > 1:
                    train_games.append(int(g))
                    covered += counts[int(g)]
            train_set = set(train_games)
            tr = np.nonzero([int(g) in train_set for g in groups])[0]
            te = np.nonzero([int(g) not in train_set for g in groups])[0]
            return tr, te
    cut = min(max(int(n * frac), 1), n - 1)
    return np.arange(cut), np.arange(cut, n)


def _main(argv=None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=None,
                        help="run -AIIMITATE games here to collect data")
    parser.add_argument("--replay-dir", type=Path, default=None,
                        help="play back every *.ply here with -AIREPLAY -AIIMITATE "
                             "(needs --install-dir) and learn from the recorded "
                             "player's packet-decoded actions")
    parser.add_argument("--replay-owner", type=int, default=None,
                        help="recorded owner slot to log (default: the replay's "
                             "local player)")
    parser.add_argument("--observe", type=Path, default=None,
                        help="use an existing ai_rl_observe.jsonl instead")
    parser.add_argument("--games", type=int, default=5)
    parser.add_argument("--seed", type=int, default=1,
                        help="base seed; game i uses seed+i")
    parser.add_argument("--max-frames", type=int, default=4000)
    parser.add_argument("--epochs", type=int, default=600)
    parser.add_argument("--power", type=float, default=0.5,
                        help="class-weight exponent (0=none, 1=full inverse "
                             "frequency); 0.5 = sqrt-balanced")
    parser.add_argument("--dataset", type=Path, default=None,
                        help="load a cached (X,y,M) dataset and skip collection")
    parser.add_argument("--save-dataset", type=Path, default=None,
                        help="save the collected (X,y,M) dataset for fast reruns")
    parser.add_argument("--out", type=Path, default=Path("imitation_policy.npz"))
    args = parser.parse_args(argv)

    if args.dataset is not None:
        cache = np.load(args.dataset)
        X, y, M = cache["X"], cache["y"], cache["M"]
        n_games = int(cache["n_games"]) if "n_games" in cache else 1
        groups = cache["g"] if "g" in cache.files else None
        if groups is None:
            print("WARNING: cached dataset has no per-row game ids ('g'); "
                  "using a contiguous time split instead of an episode "
                  "split — re-collect to get a leak-free evaluation")
    else:
        games: list[list] = []
        if args.observe is not None:
            games.append(load_observations(args.observe, owner=None))
        elif args.replay_dir is not None:
            if args.install_dir is None:
                parser.error("--replay-dir needs --install-dir")
                return 2
            replays = sorted(Path(args.replay_dir).glob("*.ply"))
            if not replays:
                parser.error(f"no *.ply under {args.replay_dir}")
                return 2
            for i, replay in enumerate(replays):
                print(f"[{i+1}/{len(replays)}] replaying {replay.name} ...",
                      flush=True)
                series = collect_replay_game(args.install_dir, replay,
                                             args.replay_owner)
                exact = sum(1 for row in series if row[3] >= 0)
                print(f"    {len(series)} samples, {exact} packet-labeled",
                      flush=True)
                games.append(series)
        elif args.install_dir is not None:
            for i in range(args.games):
                seed = args.seed + i
                print(f"[{i+1}/{args.games}] collecting -AIIMITATE seed={seed}"
                      " ...", flush=True)
                games.append(collect_imitation_game(
                    args.install_dir, seed, args.max_frames))
        else:
            parser.error("provide --install-dir, --observe, or --dataset")
            return 2
        Xs, ys, Ms, gs = [], [], [], []
        for game_id, series in enumerate(games):
            Xg, yg, Mg = build_dataset(series)
            if len(Xg):
                Xs.append(Xg); ys.append(yg); Ms.append(Mg)
                gs.append(np.full(len(yg), game_id, dtype=np.int32))
        X = np.concatenate(Xs); y = np.concatenate(ys); M = np.concatenate(Ms)
        groups = np.concatenate(gs)
        n_games = len(games)
        if args.save_dataset is not None:
            # A bare filename lands next to the game artifacts (install dir),
            # not the script CWD — a CWD-relative save once left a stale copy
            # in the install dir that a later BC train silently picked up.
            save_path = args.save_dataset
            if not save_path.is_absolute() and args.install_dir is not None:
                save_path = Path(args.install_dir) / save_path
            np.savez(save_path, X=X, y=y, M=M, g=groups, n_games=n_games)
            print(f"cached dataset -> {save_path}")

    print(f"dataset: {len(X)} labeled transitions from {n_games} game(s)")
    print("label histogram:", _hist(y))

    # Episode-level train/test split (no adjacent-transition leakage).
    tr, te = split_train_test(len(X), groups)
    if groups is not None:
        held = sorted(set(int(g) for g in np.asarray(groups)[te]))
        print(f"split: {len(tr)} train / {len(te)} test transitions; "
              f"held-out game(s): {held}")

    # Class-balanced training so the rare-but-decisive build/produce actions are
    # actually learned (raw accuracy is dominated by no_op and is misleading;
    # balanced accuracy = mean per-class recall is the real objective).
    weights = class_weights(y[tr], power=args.power)
    W, b = train_bc(X[tr], y[tr], M[tr], epochs=args.epochs, weights=weights)

    tr_acc, _, _ = evaluate(W, b, X[tr], y[tr], M[tr])
    te_acc, pred, per_class = evaluate(W, b, X[te], y[te], M[te])
    vals, cnts = np.unique(y[tr], return_counts=True)
    majority = vals[cnts.argmax()]
    base_acc = float((y[te] == majority).mean())
    balanced = float(np.mean(list(per_class.values()))) if per_class else 0.0

    print(f"train acc: {tr_acc:.3f}   test acc: {te_acc:.3f}   "
          f"majority-baseline: {base_acc:.3f} ({ACTION_NAMES[int(majority)]})")
    print(f"balanced acc (mean per-class recall): {balanced:.3f}")
    print("per-class test recall:",
          {k: round(v, 3) for k, v in per_class.items()})

    out_path = args.out
    if not out_path.is_absolute() and args.install_dir is not None:
        out_path = Path(args.install_dir) / out_path
    np.savez(out_path, W=W, b=b, action_names=np.array(ACTION_NAMES),
             class_weights=weights)
    print(f"saved behavior-cloned policy -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
