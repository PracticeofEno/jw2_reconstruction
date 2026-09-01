"""Gym-style environment and dataset tools for the ranker Computer(AI) RL loop.

The reconstructed game (``ranker_rebuild.exe``) runs a deterministic, headless
self-play match (``-AISELF``) in which the Computer(AI) owner is driven by a
high-level RL action each decision cycle.  With ``-AIRANDOM`` those actions are
sampled uniformly from the legal set (plumbing validation); a learned policy
replaces that sampler.  Every decision is logged as a full transition to
``ai_rl_episode.jsonl``:

    {"owner":1,"f":8,"a":2,"tgt":-1,"why":2048,"dt":24,
     "r":-0.0052,"sh":-0.0052,"tm":0.0,"done":false,
     "feat":[N_FEATURES floats],"mask":[N_ACTIONS ints]}  (sizes follow the C++ constants)

``why`` (v9) is the decision-gate trigger bitmask that fired this decision
(AiDecisionTrigger in ranker_ai_decision_gate.h) and ``dt`` the frames since
the previous decision - decisions are EVENT-BASED, not fixed-interval.

``feat`` is the exact policy/value-network input (see EncodeAiObservationForRl),
``mask`` the legal-action mask, ``a`` the action taken, ``r`` the reward earned
(potential-based shaping + sparse terminal), ``done`` the terminal flag.  s' is
the next line's ``feat`` (or the episode ends on ``done``).

This module is the off-sim (Python) side per AGENTS.md: the network lives here,
outside the deterministic simulation.  It provides
  * ``load_episode`` / ``compute_returns``        — dataset loading + returns,
  * ``RankerRolloutCollector``                     — run the exe, collect a game,
  * ``RankerSelfPlayEnv``                          — a Gymnasium-style env over a
        collected episode (replay mode; online policy-in-the-loop is the next
        IPC step),
  * ``RandomLegalPolicy``                          — the Python mirror of the
        current C++ sampler, and the interface a learned policy implements.

No third-party deps are required; numpy is used if present.
"""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, Protocol, Sequence

try:  # numpy is optional; fall back to plain lists.
    import numpy as _np
except Exception:  # pragma: no cover - numpy is expected but not required
    _np = None

# --- Action space (must match ranker_ai_rl_features.h AiRlHighLevelAction) ---
ACTION_NAMES: tuple[str, ...] = (
    "no_op",
    "produce_worker",
    "produce_masos",
    "produce_dilophos",
    "build_population_nest",
    "build_egg_nest",
    "build_land_nest",
    "expand_base_nest",
    "scout_map",
    "attack_nearest_enemy",
    "attack_enemy_base",
    "defend_base",
    "retreat",
    "hunt_neutral_monster",
    "produce_unit_x22",
    "build_nest_x86",
    "build_nest_x87",
    "produce_unit_x25",
    "produce_unit_x27",
    "produce_unit_x28",
    "produce_unit_x2e",
    "produce_unit_x2c",
    "produce_unit_x29",
    "produce_unit_x2a",
    "build_nest_x83",
    "build_nest_x88",
    "build_nest_x89",
    "build_nest_x8a",
    # v2 (docs/AI_PLAY_TYRANO_FULL_CAPABILITY_DESIGN.md): merges, morph,
    # stance, positional micro and the carrier drop composite.
    "merge_twin_velocis",
    "merge_twin_rhampos",
    "merge_twin_pteras",
    "merge_mutant",
    "morph_enter_army",
    "morph_exit_army",
    "stance_on_army",
    "stance_off_army",
    "hold_army",
    "patrol_defense",
    "drop_attack",
    # Per-order research (audited Tyrano tree; replaces research_next).
    "research_harvest",          # 0x14
    "research_ground_attack",    # 0x19
    "research_ground_defense",   # 0x1a
    "research_movement",         # 0x16
    "research_air_attack",       # 0x1c
    "research_air_defense",      # 0x1d
    "research_mutant_merge",     # 0x18
    "research_morph",            # 0x2a
    "research_haste",            # 0x38
    "research_exp_down",         # 0x2b
    "research_melee_reinforce",  # 0x1b
    "research_triceps_speed",    # 0x2d
    "research_air_reinforce",    # 0x1e
    # v6: the army strategies are explicit and disjoint - search the map for
    # the enemy base (no building known) / attack the known base / defend.
    "search_enemy_base",
    # v7: expansion chain - light the next expansion site, then build there.
    "scout_berry",
    # v7: the search split by purpose - start candidates (army), reachable
    # frontier (one unit, air first), random patrol outside active vision.
    "explore_frontier",
    "roam_scout",
    # v8 (docs/1순위.md): the detachable raid group.  The original army
    # actions keep their meaning (MAIN army only); these drive the raid's own
    # objective.  detach/merge are deterministic executor rules (mobility-
    # first 30%); raid actions are never BC-labelable (RL discovers them).
    "detach_raid",
    "merge_raid",
    "raid_attack_units",
    "raid_attack_base",
    "raid_defend_base",
    "raid_retreat",
    "raid_hunt_neutral",
    "raid_search",
    # v10: two more raid slots (four fighting bodies) - exact raid semantics.
    "detach_raid_b",
    "merge_raid_b",
    "raid_b_attack_units",
    "raid_b_attack_base",
    "raid_b_defend_base",
    "raid_b_retreat",
    "raid_b_hunt_neutral",
    "raid_b_search",
    "detach_raid_c",
    "merge_raid_c",
    "raid_c_attack_units",
    "raid_c_attack_base",
    "raid_c_defend_base",
    "raid_c_retreat",
    "raid_c_hunt_neutral",
    "raid_c_search",
)
N_ACTIONS = len(ACTION_NAMES)      # 80 (kAiRlActionCount; v10 adds the 16 raid_b/c actions)
N_FEATURES = 802                   # kAiRlFeatureCount (v10: +14 raid_b/c group state [788..801])
# v8 spatial-target head (kAiRlTargetCellCount): the 8x8 grid cell argument
# of attack_enemy_base / raid_attack_base / defend_base / raid_defend_base.
# The game sends its legality as "tmask" in the IPC request and logs the
# chosen cell as "tgt" in the episode JSONL (-1 = none).
N_TARGET_CELLS = 64
TARGET_ACTIONS = ("attack_enemy_base", "raid_attack_base",
                  "raid_b_attack_base", "raid_b_defend_base",
                  "raid_c_attack_base", "raid_c_defend_base",
                  "defend_base", "raid_defend_base")
TARGET_ACTION_IDS = tuple(ACTION_NAMES.index(name) for name in TARGET_ACTIONS)
# Discount horizon matched to real game length: eliminations land at
# 31k-40k frames = ~4k-5k decisions.  0.997 gave an effective horizon of
# ~333 decisions (2.6k frames), so the elimination reward was inaudible to
# the actions that set it up.  0.9998 = half-life ln2/(1-γ) ≈ 3466 decisions
# ≈ 27.7k frames.
DEFAULT_DISCOUNT = 0.9998


def _vec(values: Sequence[float]):
    """Return a numpy array if numpy is available, else the list unchanged."""
    return _np.asarray(values, dtype=_np.float32) if _np is not None else list(values)


@dataclass
class Transition:
    owner: int
    frame: int
    action: int
    reward: float
    shaping: float
    terminal: float
    done: bool
    features: object   # np.ndarray[N_FEATURES] or list[float]
    mask: object       # np.ndarray[N_ACTIONS] or list[int]
    # v8 spatial-target cell chosen with the action (-1 = none).
    target: int = -1
    # v9 decision-gate context: trigger bitmask + frames since the previous
    # decision (0 on pre-v9 episodes).
    why: int = 0
    dt: int = 0

    @property
    def action_name(self) -> str:
        return ACTION_NAMES[self.action] if 0 <= self.action < N_ACTIONS else "?"


def load_episode(path: str | Path, owner: int | None = None) -> list[Transition]:
    """Load ``ai_rl_episode.jsonl`` into a list of transitions.

    If ``owner`` is given, keep only that owner's transitions (a self-play match
    can log several RL owners into one file).
    """
    transitions: list[Transition] = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if owner is not None and row["owner"] != owner:
                continue
            feat = row["feat"]
            if len(feat) != N_FEATURES:
                raise ValueError(
                    f"expected {N_FEATURES} features, got {len(feat)}")
            transitions.append(Transition(
                owner=row["owner"],
                frame=row["f"],
                action=row["a"],
                reward=row["r"],
                shaping=row.get("sh", 0.0),
                terminal=row.get("tm", 0.0),
                done=bool(row["done"]),
                features=_vec(feat),
                mask=(_np.asarray(row["mask"], dtype=_np.int8)
                      if _np is not None else list(row["mask"])),
                target=int(row.get("tgt", -1)),
                why=int(row.get("why", 0)),
                dt=int(row.get("dt", 0)),
            ))
    return transitions


def split_episodes(transitions: Sequence[Transition]) -> list[list[Transition]]:
    """Split a flat transition list into episodes on ``done`` boundaries."""
    episodes: list[list[Transition]] = []
    current: list[Transition] = []
    for tr in transitions:
        current.append(tr)
        if tr.done:
            episodes.append(current)
            current = []
    if current:  # trailing (truncated, no terminal) segment
        episodes.append(current)
    return episodes


def compute_returns(transitions: Sequence[Transition],
                    gamma: float = DEFAULT_DISCOUNT) -> list[float]:
    """Discounted return-to-go G_t for each step, reset at ``done`` boundaries."""
    returns = [0.0] * len(transitions)
    running = 0.0
    for i in range(len(transitions) - 1, -1, -1):
        if transitions[i].done:
            running = 0.0
        running = transitions[i].reward + gamma * running
        returns[i] = running
    return returns


def action_histogram(transitions: Sequence[Transition]) -> dict[str, int]:
    hist: dict[str, int] = {}
    for tr in transitions:
        hist[tr.action_name] = hist.get(tr.action_name, 0) + 1
    return hist


# --- Rollout collection: run the exe to produce a fresh episode ---

@dataclass
class RankerRolloutCollector:
    """Runs ``ranker_rebuild.exe`` self-play and loads the resulting episode."""

    install_dir: Path
    exe: str = "ranker_rebuild.exe"
    max_frames: int = 4000
    timeout: float = 300.0

    def collect(self, seed: int | None = None,
                extra_args: Sequence[str] = ()) -> list[Transition]:
        install = Path(self.install_dir)
        episode_path = install / "ai_rl_episode.jsonl"
        for stale in (episode_path, install / "ai_rl_reward_trace.json"):
            try:
                stale.unlink()
            except FileNotFoundError:
                pass
        # -AIRANDOM drives owner 1 with the (currently random) legal RL policy
        # and logs the full-state episode.  A learned policy will replace the
        # in-sim sampler via the online IPC hook (next step).
        args = [str(install / self.exe), "-AISELF", "-AIRANDOM",
                f"-MAXFRAMES:{self.max_frames}", *extra_args]
        if seed is not None:
            args.append(f"-SEED:{seed}")
        proc = subprocess.Popen(args, cwd=str(install))
        try:
            proc.wait(timeout=self.timeout)
        except subprocess.TimeoutExpired:  # pragma: no cover - safety net
            proc.kill()
            proc.wait()
        if not episode_path.exists():
            raise RuntimeError(f"no episode written at {episode_path}")
        return load_episode(episode_path)


# --- Gymnasium-style env over a collected episode (replay mode) ---

@dataclass
class BoxLike:
    shape: tuple[int, ...]
    low: float = 0.0
    high: float = 1.0


@dataclass
class DiscreteLike:
    n: int


class RankerSelfPlayEnv:
    """A Gymnasium-style wrapper over a single RL owner's self-play episode.

    Replay mode: ``reset`` collects (or accepts) one episode; ``step`` returns
    the next logged transition.  The agent's ``action`` argument is advisory
    here — the behavior action that actually drove the sim is returned in
    ``info['behavior_action']`` so behavior cloning and off-policy learning can
    consume real trajectories.  Online mode (the agent's action drives the sim,
    over IPC) is the next integration step and keeps this same API.
    """

    metadata = {"render_modes": []}

    def __init__(self, collector: RankerRolloutCollector | None = None,
                 owner: int = 1, gamma: float = DEFAULT_DISCOUNT):
        self.collector = collector
        self.owner = owner
        self.gamma = gamma
        self.observation_space = {
            "features": BoxLike((N_FEATURES,)),
            "mask": BoxLike((N_ACTIONS,), 0.0, 1.0),
        }
        self.action_space = DiscreteLike(N_ACTIONS)
        self._episode: list[Transition] = []
        self._index = 0

    def load(self, transitions: Sequence[Transition]) -> None:
        """Use a pre-collected episode instead of running the exe."""
        self._episode = [t for t in transitions if t.owner == self.owner]
        self._index = 0

    def reset(self, seed: int | None = None):
        if self.collector is not None:
            all_tr = self.collector.collect(seed=seed)
            self.load(all_tr)
        if not self._episode:
            raise RuntimeError("no episode loaded; call load() or pass a "
                               "collector to run the exe")
        self._index = 0
        first = self._episode[0]
        obs = {"features": first.features, "mask": first.mask}
        return obs, {"frame": first.frame}

    def step(self, action: int):
        if self._index >= len(self._episode):
            raise RuntimeError("step past end of episode; call reset()")
        current = self._episode[self._index]
        self._index += 1
        terminated = current.done
        truncated = (not current.done) and self._index >= len(self._episode)
        if self._index < len(self._episode):
            nxt = self._episode[self._index]
            obs = {"features": nxt.features, "mask": nxt.mask}
        else:  # terminal / end of buffer: repeat the last observation
            obs = {"features": current.features, "mask": current.mask}
        info = {
            "behavior_action": current.action,
            "agreed": action == current.action,
            "frame": current.frame,
            "shaping": current.shaping,
            "terminal": current.terminal,
        }
        return obs, current.reward, terminated, truncated, info

    def __len__(self) -> int:
        return len(self._episode)


# --- Policy interface (the seam a learned policy plugs into) ---

class Policy(Protocol):
    def act(self, features, mask) -> int:  # pragma: no cover - protocol
        ...


@dataclass
class RandomLegalPolicy:
    """Python mirror of the current in-sim sampler: uniform over legal actions.

    Deterministic given ``seed`` so Python-side rollouts are reproducible too.
    """
    seed: int = 0
    _state: int = field(default=0, init=False)

    def __post_init__(self) -> None:
        self._state = self.seed & 0xFFFFFFFF

    def _rand(self) -> int:  # xorshift32 — no external RNG dependency
        x = self._state or 0x9E3779B9
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self._state = x & 0xFFFFFFFF
        return self._state

    def act(self, features, mask) -> int:
        legal = [i for i, m in enumerate(mask) if m]
        if not legal:
            return 0  # no_op
        return legal[self._rand() % len(legal)]


def _summarize(transitions: Sequence[Transition], gamma: float) -> str:
    episodes = split_episodes(transitions)
    returns = compute_returns(transitions, gamma)
    hist = action_histogram(transitions)
    lines = [
        f"transitions: {len(transitions)}  episodes: {len(episodes)}",
        f"undiscounted return (sum r): {sum(t.reward for t in transitions):.5f}",
        f"discounted G_0: {returns[0]:.5f}" if returns else "discounted G_0: n/a",
        "action histogram: " + ", ".join(
            f"{k}={v}" for k, v in sorted(hist.items())),
        f"done steps: {sum(1 for t in transitions if t.done)}",
    ]
    return "\n".join(lines)


def _main(argv: Sequence[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=None,
                        help="run the exe here to collect a fresh episode")
    parser.add_argument("--episode", type=Path, default=None,
                        help="load an existing ai_rl_episode.jsonl instead")
    parser.add_argument("--owner", type=int, default=1)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-frames", type=int, default=4000)
    parser.add_argument("--gamma", type=float, default=DEFAULT_DISCOUNT)
    args = parser.parse_args(argv)

    if args.episode is not None:
        transitions = load_episode(args.episode, owner=args.owner)
        source = str(args.episode)
    elif args.install_dir is not None:
        collector = RankerRolloutCollector(args.install_dir,
                                           max_frames=args.max_frames)
        transitions = [t for t in collector.collect(seed=args.seed)
                       if t.owner == args.owner]
        source = f"{args.install_dir} (seed {args.seed})"
    else:
        parser.error("provide either --install-dir or --episode")
        return 2

    print(f"loaded owner {args.owner} from {source}")
    print(_summarize(transitions, args.gamma))

    # Exercise the gym API end to end with the random-legal policy.
    env = RankerSelfPlayEnv(owner=args.owner, gamma=args.gamma)
    env.load(transitions)
    obs, _ = env.reset()
    policy = RandomLegalPolicy(seed=args.seed)
    total_r, agreed, steps = 0.0, 0, 0
    while True:
        action = policy.act(obs["features"], obs["mask"])
        obs, reward, terminated, truncated, info = env.step(action)
        total_r += reward
        agreed += int(info["agreed"])
        steps += 1
        if terminated or truncated:
            break
    print(f"gym rollout: {steps} steps, replay return {total_r:.5f}, "
          f"policy/behavior agreement {agreed}/{steps}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
