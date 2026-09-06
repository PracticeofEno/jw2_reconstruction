"""Isolated commander workers, measured start permutations, and honest evaluation gates."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import statistics
import subprocess
import threading
import time

from ranker_commander_rollout import LOSS, TRUNCATED, WIN, RolloutError, read_rollout

START_PATTERN = re.compile(r"start-slots: owner=(\d+) state=(\d+) map_slot=(\d+).*?tribe=(\d+)")
ASSET_DIRECTORIES = {"data", "maps", "map", "sound", "sounds", "music", "movie", "movies",
                     "resources", "media", "icons"}
OUTPUT_DIRECTORIES = {"replays", "logs", "debug_artifacts", "result", "results", ".git"}


# Startup watchdog for game processes. A normal launch reaches its first
# commander decision (which creates the rollout file) within ~30 s even on a
# loaded host; a launch stuck in the frontend flow never does and would
# otherwise burn the full per-game timeout before the cohort can retry it.
STARTUP_TIMEOUT_SECONDS = 120.0
STARTUP_POLL_SECONDS = 5.0
STARTUP_ATTEMPTS = 2


def curriculum_settings(stage: int) -> dict:
    if stage not in range(5):
        raise ValueError("curriculum must be C0..C4")
    return {"curriculum": stage, "opp_slow": 4 if stage == 0 else 2 if stage == 1 else 0,
            "max_frames": 40000 if stage < 2 else 60000}


def prepare_job_directory(install_dir: str | Path, job_dir: str | Path,
                          executable: str | Path | None = None) -> Path:
    """Private mutable files/CWD; immutable runtime assets may be shared.

    Invoke the existing ranker_rebuild.exe by absolute path. No executable is
    deployed or renamed and neither Result.txt nor _SETUP.DAT is shared.
    """
    install_dir, job_dir = Path(install_dir).resolve(), Path(job_dir).resolve()
    executable = Path(executable).resolve() if executable else install_dir / "ranker_rebuild.exe"
    if executable.name.lower() != "ranker_rebuild.exe":
        raise ValueError("the reconstructed executable must be named ranker_rebuild.exe")
    if not executable.is_file():
        raise FileNotFoundError(executable)
    if job_dir == install_dir or install_dir in job_dir.parents:
        raise ValueError("job directory must be outside the installation")
    job_dir.mkdir(parents=True, exist_ok=False)
    for source in install_dir.iterdir():
        name = source.name.lower()
        destination = job_dir / source.name
        if source.is_file():
            if source.suffix.lower() in (".exe", ".log", ".ply", ".rlo", ".json", ".jsonl", ".csv", ".pt", ".npz", ".tmp", ".zip", ".7z", ".rar"):
                continue
            if name in {"result.txt", "ai_selfplay_result.json"} or name.startswith(("ai_", "commander_")):
                continue
            if source.suffix.lower() in (".trc", ".dll", ".asi", ".chm"):
                # These are loaded read-only. Copying every resource blob
                # per game exhausted disk during the 96-game teacher gate.
                # Settings and results still belong to the individual CWD.
                try:
                    destination.symlink_to(source)
                    continue
                except OSError:
                    pass
            shutil.copy2(source, destination)
        elif source.is_dir() and name in ASSET_DIRECTORIES:
            try:
                destination.symlink_to(source, target_is_directory=True)
                continue
            except OSError:
                pass
            if os.name == "nt":
                # Junctions do not require the Windows symbolic-link privilege.
                destination_text = str(destination).replace("'", "''")
                source_text = str(source).replace("'", "''")
                try:
                    subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command",
                        f"New-Item -ItemType Junction -ErrorAction Stop -Path '{destination_text}' -Target '{source_text}' | Out-Null"],
                        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        creationflags=subprocess.CREATE_NO_WINDOW)
                    continue
                except (OSError, subprocess.CalledProcessError):
                    pass
            shutil.copytree(source, destination, ignore=shutil.ignore_patterns("*.rlo", "*.log"))
    return job_dir


def extract_start_slots(log: str) -> dict[int, dict[str, int]]:
    slots = {}
    for owner, state, map_slot, tribe in START_PATTERN.findall(log):
        slots[int(owner)] = {"state": int(state), "map_slot": int(map_slot), "tribe": int(tribe)}
    return slots


def validate_terminal_result(episode, result):
    """Use the engine-backed RLO outcome when elimination and the cap coincide.

    The output writer may retain reason=max_frames even when the end-condition
    check has already eliminated an owner. Its broad building counts include
    traps, so they cannot override the rollout's strict elimination outcome.
    """
    end_frame = int(episode.terminal["frame"])
    if end_frame != int(result["end_frame"]):
        raise RuntimeError("result frame and rollout terminal frame disagree")
    status = int(episode.terminal["status"])
    if status not in (WIN, LOSS, TRUNCATED):
        raise RuntimeError("rollout has no valid terminal outcome")
    if status == TRUNCATED and result["reason"] != "max_frames":
        raise RuntimeError("rollout truncation requires a frame-limit result")
    if result.get("result_code") == 2 and end_frame < 1800 and status != TRUNCATED:
        raise RuntimeError("early invalid result 2")
    return status


def _run_game(install_dir, weights, job_root, index, job, slot, *, teacher,
              deterministic, timeout, weights2=None, executable=None, no_sleep=True):
    weights = job.get("primary_weights", weights)
    weights2 = job.get("opponent_weights", weights2)
    job_dir = prepare_job_directory(install_dir, Path(job_root) / f"game_{index:05d}", executable)
    output = job_dir / "output"
    output.mkdir()
    replay_dir = output / "Replays"
    replay_dir.mkdir()
    rollout = output / "commander.rlo"
    game_executable = Path(executable).resolve() if executable else Path(install_dir).resolve() / "ranker_rebuild.exe"
    command = [str(game_executable), "-AISELF", "-AICOMMANDER",
               f"-AIWEIGHTS:{Path(weights).resolve()}", f"-AIROLLOUT:{rollout}",
               f"-AINET:{300 + slot}", f"-SEED:{job['seed']}", f"-AITRIBE:{job['tribe']}",
               f"-MAXFRAMES:{job.get('max_frames', 60000)}", f"-AIOUT:{output}",
               f"-AICURRICULUM:{job.get('curriculum', 2)}", "-AIAUTOSCOUT:1"]
    if teacher:
        command.append("-AITEACHER")
    if job.get("teacher_variant"):
        # Rule-commander variant (design 8.3): randomized opening, timings,
        # attack threshold, harassment and target priority derived from N.
        command.append(f"-AITEACHERVAR:{int(job['teacher_variant'])}")
    if job.get("teacher_variant2"):
        command.append(f"-AITEACHERVAR2:{int(job['teacher_variant2'])}")
    if job.get("dagger"):
        # Policy acts; the rule commander's decisions are recorded as labels
        # in commander.rlo.teacher.bin (DAgger data for BC).
        command.append("-AIDAGGER")
    teacher2 = bool(job.get("teacher2"))
    if teacher2 and weights2 is None:
        # Policy (owner 1) versus a rule-commander (variant) on owner 2.
        command.extend(["-AIVS", "-AITEACHER2"])
    if deterministic:
        command.append("-AIDETERMINISTIC")
    if no_sleep:
        # Headless self-play default (user decision 2026-09-05): skip the
        # engine's per-frame Sleep(1). Pass --keep-sleep to retain it.
        command.append("-AINOSLEEP")
    if job.get("opp_slow", 0) > 1:
        command.append(f"-AIOPPSLOW:{job['opp_slow']}")
    if weights2 is not None:
        command.extend(["-AIVS", f"-AIWEIGHTS2:{Path(weights2).resolve()}"])
    environment = os.environ.copy()
    environment["RANKER_RECONSTRUCTED_LOG_PATH"] = str(output / "Jw2.log")
    environment["RANKER_RECONSTRUCTED_REPLAY_DIR"] = str(replay_dir)
    environment.pop("RANKER_RECONSTRUCTED_PORT_OFFSET", None)
    report = {**job, "job": index, "rollout": str(rollout), "directory": str(job_dir),
              "deterministic": deterministic, "teacher": teacher, "valid": False,
              "win": False, "reason": "not_started", "command": command}
    started = time.monotonic()
    try:
        report["weights_sha256"] = hashlib.sha256(Path(weights).read_bytes()).hexdigest()
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        for attempt in range(STARTUP_ATTEMPTS):
            if attempt:
                # A fresh output directory: the engine must not see a partial
                # log or replay from the hung launch.
                shutil.rmtree(output)
                output.mkdir()
                replay_dir.mkdir()
                report["startup_relaunches"] = attempt
            with (output / "process.log").open("w", encoding="utf-8") as log:
                process = subprocess.Popen(command, cwd=job_dir, env=environment, stdout=log,
                                           stderr=subprocess.STDOUT, creationflags=flags)
                launched = time.monotonic()
                hung = False
                while True:
                    try:
                        exit_code = process.wait(timeout=STARTUP_POLL_SECONDS)
                        break
                    except subprocess.TimeoutExpired:
                        elapsed = time.monotonic() - launched
                        if elapsed > timeout:
                            process.kill()
                            process.wait()
                            raise RuntimeError("game process timeout")
                        # The engine writes the rollout header at its first
                        # decision (frame 1). A process that has not reached it
                        # by then is stuck in the frontend flow: kill and relaunch.
                        if elapsed > STARTUP_TIMEOUT_SECONDS and not rollout.exists():
                            process.kill()
                            process.wait()
                            hung = True
                            break
            if not hung:
                break
        else:
            raise RuntimeError(f"game startup hang ({STARTUP_ATTEMPTS} launches without a first decision)")
        if exit_code != 0:
            raise RuntimeError(f"game process exited with code {exit_code}")
        result_path = output / "ai_selfplay_result.json"
        result = json.loads(result_path.read_text(encoding="utf-8"))
        episode = read_rollout(rollout, teacher=teacher)
        if episode.owner != 1:
            raise RuntimeError("primary rollout must belong to owner 1")
        end_frame = int(episode.terminal["frame"])
        status = validate_terminal_result(episode, result)
        if episode.seed != job["seed"]:
            raise RuntimeError("rollout seed does not match job")
        slots = extract_start_slots((output / "Jw2.log").read_text(encoding="utf-8", errors="replace"))
        if 1 not in slots or 2 not in slots:
            raise RuntimeError("startup log has no competing start slots")
        pair = [slots[1]["map_slot"], slots[2]["map_slot"]]
        # Under -AIVS slot 2 is the second (Tyrano) policy owner, not the
        # built-in opponent the job's tribe field describes.
        expected_tribe = 2 if (weights2 is not None or teacher2) else job["tribe"]
        if pair[0] == pair[1] or slots[2]["tribe"] != expected_tribe:
            raise RuntimeError("actual map slots or opponent tribe disagree with job")
        if "start_pair" in job and pair != list(job["start_pair"]):
            raise RuntimeError("measured start pair changed; rediscover seed mapping")
        if weights2 is not None or teacher2:
            # Under -AITEACHER both policy owners are rule commanders, and under
            # -AITEACHER2 the second owner alone is one; read its rollout in
            # the matching cohort.
            other = read_rollout(str(rollout) + ".owner2.rlo", teacher=teacher or teacher2)
            other_status = int(other.terminal["status"])
            expected_other = {WIN: LOSS, LOSS: WIN, TRUNCATED: TRUNCATED}[status]
            if (other.owner != 2 or other.seed != episode.seed or
                    int(other.terminal["frame"]) != end_frame or other_status != expected_other):
                raise RuntimeError("the two policy rollouts disagree on the terminal outcome")
            report["rollout2"] = str(other.path)
            report["weight_version2"] = other.weight_version
            report["status2"] = other_status
            if hasattr(other, "close"):
                other.close()
            elif hasattr(other.records, "_mmap"):
                other.records._mmap.close()
        report.update(valid=True, win=status == WIN, status=status, end_frame=end_frame,
                      reason=result["reason"], start_pair=pair, decisions=len(episode.decisions),
                      weight_version=episode.weight_version,
                      evaluation_valid=status in (WIN, LOSS) or end_frame >= 20000,
                      result=result)
        metrics_path = output / "commander_metrics_1.json"
        if metrics_path.exists():
            metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
            if metrics.get("owner") != 1 or metrics.get("end_frame") != end_frame or metrics.get("status") != status:
                raise RuntimeError("commander metrics disagree with the rollout terminal")
            report["commander_metrics"] = metrics
        if weights2 is not None and (output / "commander_metrics_2.json").exists():
            report["commander_metrics2"] = json.loads((output / "commander_metrics_2.json").read_text(encoding="utf-8"))
        if hasattr(episode, "close"):
            episode.close()
        elif hasattr(episode.records, "_mmap"):
            episode.records._mmap.close()
    except (OSError, ValueError, RuntimeError, KeyError, RolloutError) as error:
        report.update(valid=False, win=False, reason=str(error))
    report["wall_seconds"] = time.monotonic() - started
    (output / "job.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def run_games(install_dir, weights, job_root, jobs, *, workers=8, teacher=False,
              deterministic=False, timeout=1200.0, stagger=3.0, weights2=None,
              executable=None, no_sleep=True):
    if workers < 1 or timeout <= 0 or stagger < 0:
        raise ValueError("invalid worker count, timeout, or stagger")
    jobs = list(jobs)
    for job in jobs:
        if not 1 <= job["seed"] <= 0xFFFFFFFF or job["tribe"] not in range(4):
            raise ValueError("seed must be nonzero u32 and tribe must be fixed 0..3")
    Path(job_root).mkdir(parents=True, exist_ok=True)
    reports = []
    # Each long-lived worker owns a port; jobs cannot reuse a live worker's port.
    start_lock = threading.Lock()
    next_start = [time.monotonic()]
    def worker(slot):
        worker_reports = []
        for index in range(slot, len(jobs), workers):
            with start_lock:
                delay = max(0.0, next_start[0] - time.monotonic())
                if delay:
                    time.sleep(delay)
                next_start[0] = time.monotonic() + stagger
            worker_reports.append(_run_game(install_dir, weights, job_root, index, jobs[index],
                slot, teacher=teacher, deterministic=deterministic, timeout=timeout, weights2=weights2,
                executable=executable, no_sleep=no_sleep))
        return worker_reports
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(worker, slot) for slot in range(min(workers, len(jobs)))]
        for future in as_completed(futures):
            reports.extend(future.result())
    reports.sort(key=lambda report: report["job"])
    (Path(job_root) / "manifest.json").write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
    return reports


def discover_seed_mapping(reports) -> dict[tuple[int, int], int]:
    mapping = {}
    for report in reports:
        if not report.get("valid") or "start_pair" not in report:
            continue
        pair = tuple(report["start_pair"])
        if len(pair) != 2 or pair[0] == pair[1] or any(value not in range(4) for value in pair):
            raise ValueError("unexpected start-slot layout; protocol assumes four map positions")
        mapping.setdefault(pair, int(report["seed"]))
    return mapping


def evaluation_jobs(mapping, *, sampling=False):
    if sampling:
        return [{"seed": seed, "tribe": tribe, **curriculum_settings(2)}
                for tribe in range(4) for seed in range(100, 150)]
    expected = {(first, second) for first in range(4) for second in range(4) if first != second}
    if set(mapping) != expected:
        raise ValueError(f"need all 12 measured ordered start pairs; missing {sorted(expected - set(mapping))}")
    return [{"seed": mapping[pair], "tribe": tribe, "start_pair": list(pair), **curriculum_settings(2)}
            for tribe in range(4) for pair in sorted(mapping)]


def wilson_interval(wins, games, z=1.959963984540054):
    if games == 0:
        return [0.0, 1.0]
    mean = wins / games
    denominator = 1 + z * z / games
    center = (mean + z * z / (2 * games)) / denominator
    radius = z * math.sqrt(mean * (1 - mean) / games + z * z / (4 * games * games)) / denominator
    return [max(0.0, center - radius), min(1.0, center + radius)]


def summarize_evaluation(reports, *, expected_games):
    usable = [item for item in reports if item.get("valid") and item.get("evaluation_valid")]
    wins = sum(bool(item["win"]) for item in usable)
    tribes = {str(tribe): {"games": sum(item["tribe"] == tribe for item in usable),
                           "wins": sum(item["tribe"] == tribe and item["win"] for item in usable)}
              for tribe in range(4)}
    winning_frames = [item["end_frame"] for item in usable if item["win"]]
    identities = {(item["tribe"], tuple(item.get("start_pair", ()))) for item in usable}
    deterministic = bool(reports) and all(item.get("deterministic") for item in reports)
    interval_wins, interval_games = wins, len(usable)
    consistent = None
    if deterministic:
        expected_pairs = {(a, b) for a in range(4) for b in range(4) if a != b}
        expected_tribes = (2,) if expected_games == 12 else range(4)
        coverage = identities == {(tribe, pair) for tribe in expected_tribes for pair in expected_pairs}
        # Design 7.3: changing SEED without changing the start permutation
        # adds no new deterministic game. Keep the requested Wilson statistic
        # descriptive; repeated runs must not narrow it or imply generalization.
        outcomes = {}
        for item in usable:
            identity = (item["tribe"], tuple(item.get("start_pair", ())))
            outcomes.setdefault(identity, set()).add(bool(item["win"]))
        consistent = all(len(values) == 1 for values in outcomes.values())
        interval_games = len(outcomes) if consistent else None
        interval_wins = sum(next(iter(values)) for values in outcomes.values()) if consistent else None
    else:
        sampled_cases = {(item["tribe"], item.get("seed")) for item in usable}
        coverage = len(sampled_cases) == expected_games
        if expected_games == 200:
            coverage = sampled_cases == {(tribe, seed) for tribe in range(4) for seed in range(100, 150)}
    return {"expected_games": expected_games, "valid_games": len(usable), "wins": wins,
            "invalid_games": len(reports) - len(usable), "win_rate": wins / len(usable) if usable else 0.0,
            "wilson_95": wilson_interval(interval_wins, interval_games) if consistent is not False else None,
            "wilson_games": interval_games,
            "wilson_basis": "unique_deterministic_conditions_descriptive_only" if deterministic else "policy_sampling",
            "deterministic_outcomes_consistent": consistent, "tribes": tribes,
            "median_win_frame": statistics.median(winning_frames) if winning_frames else None,
            "unique_start_tribe_cases": len(identities), "coverage_complete": coverage,
            "passed_100_percent": len(usable) == expected_games and wins == expected_games and coverage,
            "commander_metrics": aggregate_commander_metrics(usable)}


def aggregate_commander_metrics(reports):
    metrics = [row["commander_metrics"] for row in reports if isinstance(row.get("commander_metrics"), dict)]
    totals = {name: sum(float(item.get(name, 0)) for item in metrics) for name in (
        "end_frame", "decisions", "packets", "interrupts", "silent_rejections", "mask_violations",
        "kills_investment", "losses_investment")}
    result = {**totals, "games_with_metrics": len(metrics), "complete": len(metrics) == len(reports),
        "decisions_per_game": totals["decisions"] / len(metrics) if metrics else None,
        "packets_per_frame": totals["packets"] / totals["end_frame"] if totals["end_frame"] else None,
        "interrupt_rate": totals["interrupts"] / totals["decisions"] if totals["decisions"] else None,
        "K_over_L": totals["kills_investment"] / totals["losses_investment"] if totals["losses_investment"] else None}
    # Per-game percentiles cannot be combined into a global percentile.
    for name in ("inference_p50_ms", "inference_p99_ms", "decision_p50_ms", "decision_p99_ms",
                 "observation_p99_ms", "snapshot_p99_ms", "commander_view_p99_ms", "executor_p99_ms"):
        values = [float(item[name]) for item in metrics if name in item]
        result["worst_game_" + name] = max(values) if values else None
    for name in ("nonvisible_target_references", "first_wave_repulsed"):
        values = [float(item[name]) for item in metrics if name in item]
        result[name] = sum(values) if len(values) == len(metrics) and metrics else None
    return result


def assess_bc_gate(validation, teacher_reports, policy_reports, *, version,
                    policy_sha256, teacher_sha256):
    teacher = summarize_evaluation(teacher_reports, expected_games=48)
    policy = summarize_evaluation(policy_reports, expected_games=48)
    complete = all(summary["valid_games"] == 48 and summary["coverage_complete"]
                   for summary in (teacher, policy))
    # Teacher games never load the weights, so their rollouts carry model
    # version 0; the checkpoint hash still binds the run to this assessment.
    complete = complete and all(row.get("teacher") and row.get("weight_version") in (0, version)
                                and row.get("weights_sha256") == teacher_sha256
                                for row in teacher_reports)
    complete = complete and all(not row.get("teacher") and row.get("weight_version") == version
                                and row.get("weights_sha256") == policy_sha256
                                for row in policy_reports)
    delta = policy["win_rate"] - teacher["win_rate"]
    gameplay_passed = complete and delta >= -0.05 - 1e-12
    accuracy_passed = bool(validation.get("held_out") and validation.get("decisions", 0) > 0
        and validation.get("H1_accuracy", 0) >= 0.85 and validation.get("H3_accuracy", 0) >= 0.80
        and validation.get("H4_accuracy", 0) >= 0.80)
    dataset_complete = validation.get("teacher_games_total", 0) >= 400
    return {"weight_version": version, "policy_sha256": policy_sha256, "teacher_sha256": teacher_sha256,
            "complete": complete, "accuracy_passed": accuracy_passed,
            "gameplay_passed": gameplay_passed, "dataset_complete": dataset_complete,
            "passed": dataset_complete and accuracy_passed and gameplay_passed,
            "win_rate_delta": delta, "minimum_win_rate_delta": -0.05, "teacher": teacher, "policy": policy}


def curriculum_promotion(stage, deterministic_reports, sampling_reports):
    """Gate progression using actual games; truncations never count as wins."""
    deterministic_count = 12 if stage == 0 else 48
    expected_sampling = 48 if stage <= 1 else 200
    deterministic = summarize_evaluation(deterministic_reports, expected_games=deterministic_count)
    sampled = summarize_evaluation(sampling_reports, expected_games=expected_sampling)
    threshold = 0.85 if stage <= 1 else 0.97 if stage == 2 else 1.0
    sampling_ok = (sampled["valid_games"] == expected_sampling and sampled["coverage_complete"]
                   and sampled["win_rate"] >= threshold)
    if stage == 1:
        sampling_ok = sampling_ok and all(row["games"] >= 12 and row["wins"] / row["games"] >= 0.85
                                         for row in sampled["tribes"].values())
    return {"stage": stage, "promote": deterministic["passed_100_percent"] and sampling_ok,
            "argmax": deterministic, "sampled": sampled}


def sample_league_opponent(pool, win_rates, rng: random.Random):
    """20% built-in mixture; otherwise prioritized fictitious self-play."""
    if not pool or rng.random() < 0.2:
        return {"kind": "builtin", "tribe": rng.randrange(4)}
    weights = [max(1e-6, (1.0 - min(1.0, max(0.0, win_rates.get(name, 0.5)))) ** 2) for name in pool]
    return {"kind": "checkpoint", "weights": rng.choices(list(pool), weights=weights, k=1)[0]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("discover", "argmax", "sample", "report", "bc-assess"))
    parser.add_argument("--install-dir", type=Path)
    parser.add_argument("--weights", type=Path)
    parser.add_argument("--exe", type=Path, help="built ranker_rebuild.exe; assets still come from --install-dir")
    parser.add_argument("--keep-sleep", action="store_true", help="keep the engine's per-frame Sleep(1); default passes -AINOSLEEP")
    parser.add_argument("--io", type=Path, required=True)
    parser.add_argument("--mapping", type=Path, help="manifest(s) with measured start_pair fields")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--max-seed", type=int, default=128)
    parser.add_argument("--teacher", action="store_true")
    parser.add_argument("--curriculum", type=int, choices=range(4),
                        help="argmax/sample: play the 48 cases under this curriculum stage's settings (default: C2 normal speed)")
    args = parser.parse_args(argv)
    if args.mode == "report":
        reports = json.loads(args.io.read_text(encoding="utf-8"))
        print(json.dumps(summarize_evaluation(reports, expected_games=48 if all(
            row.get("deterministic") for row in reports) else 200), indent=2))
        return 0
    if args.install_dir is None or args.weights is None:
        parser.error("--install-dir and --weights required")
    if args.mode == "bc-assess":
        if args.mapping is None:
            parser.error("BC assessment requires --mapping with all 12 measured start pairs")
        from ranker_commander_league import collect_evaluation, write_state
        from ranker_commander_model import load_weights
        mapping = discover_seed_mapping(json.loads(args.mapping.read_text(encoding="utf-8")))
        jobs = evaluation_jobs(mapping)
        sidecar = args.weights.with_suffix(args.weights.suffix + ".json")
        metadata = json.loads(sidecar.read_text(encoding="utf-8"))
        weight_hash = hashlib.sha256(args.weights.read_bytes()).hexdigest()
        if metadata.get("weights_sha256") != weight_hash:
            parser.error("BC checkpoint hash does not match its held-out accuracy metadata")
        policy = load_weights(args.weights)
        validation = metadata.get("bc_validation", {})
        if metadata.get("mode") != "bc" or not validation.get("held_out"):
            parser.error("BC assessment requires a checkpoint with held-out BC accuracy metadata")
        if validation.get("teacher_games_total", 0) < 400:
            parser.error("BC quality admission requires 400 complete teacher games; a small smoke export is unassessed")
        teacher_reports = collect_evaluation(args.install_dir, args.weights, args.io / "teacher", jobs,
            workers=args.workers, teacher=True, deterministic=True, executable=args.exe, no_sleep=not args.keep_sleep)
        policy_reports = collect_evaluation(args.install_dir, args.weights, args.io / "policy", jobs,
            workers=args.workers, deterministic=True, executable=args.exe, no_sleep=not args.keep_sleep)
        if hashlib.sha256(args.weights.read_bytes()).hexdigest() != weight_hash:
            raise RuntimeError("BC weights changed during assessment")
        gate = assess_bc_gate(validation, teacher_reports, policy_reports, version=policy.weight_version,
                              policy_sha256=weight_hash, teacher_sha256=weight_hash)
        metadata["bc_gate"] = gate
        metadata["bc_reference"] = str(args.weights.resolve())
        metadata["bc_validation"]["gameplay_passed"] = gate["gameplay_passed"]
        metadata["bc_validation"]["passed"] = gate["passed"]
        write_state(sidecar, metadata)
        write_state(args.io / "bc_gate.json", gate)
        print(json.dumps(gate, indent=2))
        return 0 if gate["passed"] else 1
    if args.mode == "discover":
        jobs = [{"seed": seed, "tribe": 2, "max_frames": 64, "curriculum": 2}
                for seed in range(1, args.max_seed + 1)]
    else:
        mapping = discover_seed_mapping(json.loads(args.mapping.read_text(encoding="utf-8"))) if args.mapping else {}
        jobs = evaluation_jobs(mapping, sampling=args.mode == "sample")
        if args.curriculum is not None:
            # Same 48 cases under a curriculum stage's opponent speed and
            # frame cap, e.g. to compare checkpoints under the C1 gate's terms.
            jobs = [{**job, **curriculum_settings(args.curriculum)} for job in jobs]
    reports = run_games(args.install_dir, args.weights, args.io, jobs, workers=args.workers,
                        teacher=args.teacher, deterministic=args.mode != "sample",
                        executable=args.exe, no_sleep=not args.keep_sleep)
    if args.mode == "discover":
        mapping = discover_seed_mapping(reports)
        print(json.dumps({"measured_pairs": len(mapping), "complete": len(mapping) == 12,
                          "seeds": [{"start_pair": list(pair), "seed": seed} for pair, seed in sorted(mapping.items())]}, indent=2))
        return 0 if len(mapping) == 12 else 1
    summary = summarize_evaluation(reports, expected_games=len(jobs))
    (args.io / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if summary["valid_games"] == len(jobs) else 1


if __name__ == "__main__":
    raise SystemExit(main())
