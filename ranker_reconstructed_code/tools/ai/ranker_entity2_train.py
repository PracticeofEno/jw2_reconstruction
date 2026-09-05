# -*- coding: utf-8 -*-
"""ENTCMD02 (act3) type-squad online training driver.

Loops headless act3 matches against the built-in Computer opponent while one
learner/server accepts a synchronous cohort of game workers.  All workers of
a cohort use one pinned policy version; the learner merges their owner
trajectories and updates exactly once after the cohort barrier.

Usage:
    python ranker_entity2_train.py --install-dir C:/.../RankerOCPV_Win \\
        --policy entity2_bc.pt --out entity2_online.pt \\
        --games 20 --workers 4 --max-frames 60000 [--port 6101] [--seed0 100]
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
import re
import subprocess
import sys
import threading
import time


READY_PATTERN = re.compile(r"ranker_entity2_server:\s+READY\s+(\d+)\s*$")
# Server log lines worth relaying to the terminal after every cohort.
RELAY_PATTERN = re.compile(r"ranker_entity2_server:\s+(update|cohort done|probe owner|"
                           r"intent owner|COHORT_FAILED|peer error)")


def read_ready_count(path: str):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            values = []
            for line in handle:
                match = READY_PATTERN.search(line)
                if match:
                    values.append(int(match.group(1)))
            return max(values) if values else None
    except FileNotFoundError:
        return None


def publish_latest_cohort(install_dir: str, prefix: str, run_id: str, cohort_index: int,
                          entries) -> int:
    """Copy the cohort's replays to <install>/Replays as flat
    <prefix>_g<game>_<verdict>.ply / .vpo / .result.json (the naming the
    in-game replay browser lists), replacing the previous cohort's copies.
    `entries` are (game, verdict, game_out_dir, result) tuples.  A manifest
    <prefix>.txt records which run / cohort the files came from."""
    import glob
    import shutil
    replay_root = os.path.join(install_dir, "Replays")
    os.makedirs(replay_root, exist_ok=True)
    for old in glob.glob(os.path.join(replay_root, prefix + "_g*")):
        try:
            os.remove(old)
        except OSError:
            pass
    copied = 0
    lines = ["run %s cohort %d published %s" % (run_id, cohort_index,
                                                time.strftime("%Y-%m-%d %H:%M:%S"))]
    for game, verdict, out_dir, result in sorted(entries, key=lambda e: e[0]):
        base = os.path.join(replay_root, "%s_g%02d_%s" % (prefix, game, verdict))
        source_ply = os.path.join(out_dir, "ai_selfplay_replay.ply")
        if not os.path.isfile(source_ply):
            lines.append("game %d %s: no replay" % (game, verdict))
            continue
        try:
            shutil.copyfile(source_ply, base + ".ply")
            source_vpo = os.path.join(out_dir, "ai_selfplay_replay.vpo")
            if os.path.isfile(source_vpo):
                shutil.copyfile(source_vpo, base + ".vpo")
            source_result = os.path.join(out_dir, "ai_selfplay_result.json")
            if os.path.isfile(source_result):
                shutil.copyfile(source_result, base + ".result.json")
        except OSError as exc:
            lines.append("game %d %s: copy failed: %s" % (game, verdict, exc))
            continue
        copied += 1
        lines.append("game %d %s frame=%s -> %s" % (
            game, verdict, result.get("end_frame") if isinstance(result, dict) else "?",
            os.path.basename(base) + ".ply"))
    try:
        with open(os.path.join(replay_root, prefix + ".txt"), "w", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
    except OSError:
        pass
    return copied


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", required=True)
    parser.add_argument("--exe", default="ranker_rebuild.exe")
    parser.add_argument("--policy", default="", help="type-squad checkpoint (empty = fresh); "
                        "convert per-unit weights with ranker_entity2_bc.py --convert-squads-from")
    parser.add_argument("--out", required=True)
    parser.add_argument("--io", default="entity2_train_io")
    parser.add_argument("--run-id", default="")
    parser.add_argument("--games", type=int, default=10)
    parser.add_argument("--workers", type=int, default=4)
    # After every cohort its replays are copied to <install>/Replays as
    # <prefix>_g<game>_<verdict>.ply/.vpo/.result.json (the in-game replay
    # browser lists that folder), replacing the previous cohort's copies.
    parser.add_argument("--latest-replays", default="latest_cohort",
                        help="file prefix for the latest cohort's replays in "
                             "<install>/Replays (empty = do not copy)")
    # Game length cap decided 2026-09-04: 60000 frames (a game that has not
    # ended by then is truncated and bootstrapped, never judged).
    parser.add_argument("--max-frames", type=int, default=60000)
    parser.add_argument("--opp-slow", type=int, default=0)
    parser.add_argument("--reveal-base", action="store_true")
    parser.add_argument("--port", type=int, default=6101)
    parser.add_argument("--net-offset-base", type=int, default=300)
    parser.add_argument("--seed0", type=int, default=100)
    parser.add_argument("--policy-seed", type=int, default=1)
    parser.add_argument("--reset-lineage", action="store_true")
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--minibatch", type=int, default=32)
    # A 60000-frame cohort of 8 games is ~60000 steps; the PPO recompute runs
    # ~10 steps/s, so each epoch trains on a sampled subset of this size.
    parser.add_argument("--max-update-steps", type=int, default=8000)
    # Per published policy decision cost (a squad broadcast counts once);
    # without it PPO drifted into worker
    # STOP/HARVEST churn (2026-09-04 curriculum: 14000 STOPs per cohort).
    parser.add_argument("--issue-cost", type=float, default=0.001)
    parser.add_argument("--cohorts-per-update", type=int, default=1,
                        help="server accumulates this many cohorts per PPO update")
    parser.add_argument("--gate-kl-coef", type=float, default=0.0)
    parser.add_argument("--issue-prior", type=float, default=None)
    parser.add_argument("--slot-keep-prior", type=float, default=None,
                        help="fresh-init commander P(KEEP) per slot per tick")
    parser.add_argument("--economy-issue-prior", type=float, default=None,
                        help="fresh-init gate P(issue) for worker / building rows")
    parser.add_argument("--hidden", type=int, default=128)
    parser.add_argument("--random-policy", action="store_true",
                        help="no learner: the bit-exact random legal policy "
                             "(smoke / plumbing runs)")
    parser.add_argument("--probe-economy", action="store_true",
                        help="no learner: deterministic harvest/produce/research "
                             "probe (state-machine plumbing runs)")
    # Wall-clock budget per game; a 60000-frame game with a torch policy on
    # ~100 rows per tick needs well over the old 900 s.
    parser.add_argument("--game-timeout", type=float, default=3600.0)
    parser.add_argument("--startup-timeout", type=float, default=60.0)
    parser.add_argument("--update-timeout", type=float, default=7200.0)
    parser.add_argument("--accept-timeout", type=float, default=60.0)
    # The game gives up its controller for good after one reply timeout, and
    # eight games share one policy process: allow a full minute.
    parser.add_argument("--reply-timeout-ms", type=int, default=60000)
    arguments = parser.parse_args()

    if arguments.probe_economy:
        arguments.random_policy = True
    if arguments.games < 1 or arguments.workers < 1 or arguments.max_frames < 1:
        parser.error("--games, --workers and --max-frames must be at least 1")
    if not 1 <= arguments.seed0 <= 0xffffffff:
        parser.error("--seed0 must be in 1..4294967295")
    workers = min(arguments.workers, arguments.games)
    if not 1 <= arguments.port <= 65535 - workers + 1:
        parser.error("--port .. --port+workers-1 must be in 1..65535")
    if not 1000 <= arguments.reply_timeout_ms <= 60000:
        parser.error("--reply-timeout-ms must be in 1000..60000")
    if arguments.net_offset_base < 1 or arguments.net_offset_base + workers - 1 > 50000:
        parser.error("--net-offset-base worker range must be in 1..50000")
    inherited_offset = os.environ.get("RANKER_RECONSTRUCTED_PORT_OFFSET", "")
    if inherited_offset not in ("", "0"):
        parser.error("RANKER_RECONSTRUCTED_PORT_OFFSET must be unset/0")

    tools_dir = os.path.dirname(os.path.abspath(__file__))
    install_dir = os.path.abspath(arguments.install_dir)
    executable = os.path.join(install_dir, arguments.exe)
    if not os.path.isfile(executable):
        parser.error("game executable not found: %s" % executable)
    policy_path = os.path.abspath(arguments.policy) if arguments.policy else ""
    out_path = os.path.abspath(arguments.out)
    if policy_path and not os.path.isfile(policy_path):
        parser.error("policy checkpoint not found: %s" % policy_path)
    if (os.path.exists(out_path) and not arguments.random_policy and
            (not policy_path or os.path.normcase(policy_path) != os.path.normcase(out_path))):
        parser.error("existing --out may only be resumed by passing the same path as "
                     "--policy")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    resume_rollout_jobs = 0
    expected_policy_fingerprint = ""
    if policy_path and not arguments.random_policy:
        try:
            import ranker_entity2_bc as entity_bc
            import ranker_entity2_server as entity_server
            resume_net, resume_payload = entity_bc.load_checkpoint_payload(policy_path)
            resume_extra = resume_payload.get("extra", {}) or {}
            online_resume = False if arguments.reset_lineage else \
                entity_server.validate_learner_resume_extra(resume_extra)
            expected_policy_fingerprint = entity_bc.module_fingerprint(resume_net).hex()
            resume_rollout_jobs = int(resume_extra["rollout_jobs"]) if online_resume else 0
            if online_resume:
                if int(resume_extra["environment_seed_base"]) != arguments.seed0:
                    parser.error("--seed0 differs from checkpoint environment seed base")
                if int(resume_extra["cohort_workers"]) != arguments.workers:
                    parser.error("--workers differs from checkpoint cohort width")
                if int(resume_extra["environment_max_frames"]) != arguments.max_frames:
                    parser.error("--max-frames differs from checkpoint horizon")
            del resume_net, resume_extra, resume_payload
        except Exception as exc:
            parser.error("cannot inspect policy checkpoint %s: %s" % (policy_path, exc))
    last_global_job = resume_rollout_jobs + arguments.games - 1
    if arguments.seed0 + last_global_job > 0xffffffff:
        parser.error("environment seed range exceeds uint32")

    run_id = arguments.run_id or "%s-%d" % (time.strftime("%Y%m%d-%H%M%S"), os.getpid())
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", run_id):
        parser.error("--run-id may contain only letters, digits, . _ -")
    run_dir = os.path.join(os.path.abspath(arguments.io), run_id)
    try:
        os.makedirs(run_dir, exist_ok=False)
    except FileExistsError:
        parser.error("run directory already exists: %s" % run_dir)
    longest_out = os.path.join(run_dir, "g%09d" % last_global_job)
    if any(char.isspace() for char in longest_out):
        parser.error("-AIOUT paths cannot contain whitespace: %s" % run_dir)
    if len(os.fsencode(longest_out)) >= 260:
        parser.error("-AIOUT directory exceeds its 260-byte buffer: %s" % longest_out)
    replay_root = os.path.abspath(os.path.join(install_dir, "Replays"))
    replay_run_dir = os.path.abspath(os.path.join(replay_root, "EntityTrain", run_id))
    if os.path.commonpath([replay_root, replay_run_dir]) != replay_root:
        parser.error("worker replay directory escaped install Replays")
    try:
        os.makedirs(replay_run_dir, exist_ok=False)
    except (FileExistsError, OSError) as exc:
        parser.error("cannot reserve replay run directory %s: %s" % (replay_run_dir, exc))

    server_log_path = os.path.join(run_dir, "server.log")
    server_log = open(server_log_path, "w", encoding="utf-8")
    server_cmd = [sys.executable, os.path.join(tools_dir, "ranker_entity2_server.py"),
                  "--port", str(arguments.port),
                  "--episodes", str(arguments.games),
                  "--workers", str(arguments.workers),
                  "--seed", str(arguments.policy_seed),
                  "--accept-timeout", str(arguments.accept_timeout),
                  "--connection-timeout", str(arguments.game_timeout + 30.0),
                  "--reply-timeout-ms", str(arguments.reply_timeout_ms)]
    if arguments.probe_economy:
        server_cmd += ["--probe-economy"]
    if not arguments.random_policy:
        server_cmd += ["--train", "--out", out_path,
                       "--environment-seed-base", str(arguments.seed0),
                       "--environment-max-frames", str(arguments.max_frames),
                       "--lr", str(arguments.lr), "--epochs", str(arguments.epochs),
                       "--minibatch", str(arguments.minibatch),
                       "--max-update-steps", str(arguments.max_update_steps),
                       "--issue-cost", str(arguments.issue_cost),
                       "--cohorts-per-update", str(arguments.cohorts_per_update),
                       "--gate-kl-coef", str(arguments.gate_kl_coef),
                       "--hidden", str(arguments.hidden)]
        if policy_path:
            server_cmd += ["--policy", policy_path,
                           "--expected-rollout-jobs", str(resume_rollout_jobs),
                           "--expected-policy-fingerprint", expected_policy_fingerprint]
        if arguments.reset_lineage:
            server_cmd += ["--reset-lineage"]
        if arguments.issue_prior is not None:
            server_cmd += ["--issue-prior", str(arguments.issue_prior)]
        if arguments.slot_keep_prior is not None:
            server_cmd += ["--slot-keep-prior", str(arguments.slot_keep_prior)]
        if arguments.economy_issue_prior is not None:
            server_cmd += ["--economy-issue-prior", str(arguments.economy_issue_prior)]
    server = subprocess.Popen(server_cmd, cwd=tools_dir, stdout=server_log,
                              stderr=subprocess.STDOUT)
    abort_event = threading.Event()
    child_lock = threading.Lock()
    child_processes = set()

    def stop_children() -> None:
        abort_event.set()
        with child_lock:
            processes = list(child_processes)
        for process in processes:
            if process.poll() is None:
                process.terminate()
        deadline = time.monotonic() + 5.0
        for process in processes:
            if process.poll() is not None:
                continue
            try:
                process.wait(timeout=max(deadline - time.monotonic(), 0.1))
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    def wait_server_ready(count: int, timeout_s: float) -> None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            ready = read_ready_count(server_log_path)
            if ready is not None and ready >= count:
                return
            if server.poll() is not None:
                raise RuntimeError("act3 server exited before READY %d; see %s" %
                                   (count, server_log_path))
            time.sleep(0.2)
        raise RuntimeError("server READY %d timeout; see %s" % (count, server_log_path))

    server_log_cursor = [0]

    def relay_server_lines() -> None:
        """Print the server's update / cohort statistics lines that appeared
        since the last call, so every cohort's outcome shows in the terminal
        without opening server.log."""
        try:
            with open(server_log_path, encoding="utf-8", errors="replace") as handle:
                handle.seek(server_log_cursor[0])
                chunk = handle.read()
                server_log_cursor[0] = handle.tell()
        except OSError:
            return
        for line in chunk.splitlines():
            if RELAY_PATTERN.search(line):
                print("  server: " + line.split("ranker_entity2_server: ", 1)[-1],
                      flush=True)

    def run_game(game: int, worker_slot: int):
        global_job = resume_rollout_jobs + game
        out_dir = os.path.join(run_dir, "g%09d" % global_job)
        os.makedirs(out_dir, exist_ok=False)
        worker_replay_dir = os.path.join(replay_run_dir, "g%09d" % global_job)
        os.makedirs(worker_replay_dir, exist_ok=False)
        game_cmd = [executable, "-AISELF",
                    "-AIACT3:%d" % (arguments.port + worker_slot),
                    "-AINET:%d" % (arguments.net_offset_base + worker_slot),
                    "-MAXFRAMES:%d" % arguments.max_frames,
                    "-SEED:%d" % (arguments.seed0 + global_job),
                    "-AIOUT:%s" % out_dir]
        if arguments.opp_slow > 1:
            game_cmd.append("-AIOPPSLOW:%d" % arguments.opp_slow)
        if arguments.reveal_base:
            game_cmd.append("-AIREVEALBASE")
        log_path = os.path.join(out_dir, "process.log")
        game_env = os.environ.copy()
        game_env["RANKER_RECONSTRUCTED_LOG_PATH"] = os.path.join(out_dir, "Jw2.log")
        game_env["RANKER_RECONSTRUCTED_REPLAY_DIR"] = worker_replay_dir
        try:
            with open(log_path, "w", encoding="utf-8") as game_log:
                process = subprocess.Popen(game_cmd, cwd=install_dir, stdout=game_log,
                                           stderr=subprocess.STDOUT, env=game_env)
                with child_lock:
                    child_processes.add(process)
                try:
                    deadline = time.monotonic() + arguments.game_timeout
                    while process.poll() is None:
                        server_code = server.poll()
                        if abort_event.is_set() or (server_code is not None and
                                                    server_code != 0):
                            process.terminate()
                            try:
                                process.wait(timeout=5.0)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
                            return {"game": game, "status": "aborted", "job": global_job,
                                    "out_dir": out_dir}
                        if time.monotonic() >= deadline:
                            process.kill()
                            process.wait()
                            return {"game": game, "status": "timeout", "job": global_job,
                                    "out_dir": out_dir}
                        time.sleep(0.2)
                finally:
                    with child_lock:
                        child_processes.discard(process)
                if process.returncode != 0:
                    return {"game": game, "status": "exit-code", "job": global_job,
                            "error": str(process.returncode), "out_dir": out_dir}
        except Exception as exc:
            return {"game": game, "status": "spawn-error", "job": global_job,
                    "error": "%s: %s" % (type(exc).__name__, exc), "out_dir": out_dir}
        result_path = os.path.join(out_dir, "ai_selfplay_result.json")
        if not os.path.exists(result_path):
            return {"game": game, "status": "no-result", "job": global_job,
                    "out_dir": out_dir}
        try:
            with open(result_path, encoding="utf-8") as handle:
                result = json.load(handle)
        except (OSError, json.JSONDecodeError) as exc:
            return {"game": game, "status": "bad-result", "job": global_job,
                    "error": str(exc), "out_dir": out_dir}
        return {"game": game, "job": global_job, "status": "ok", "result": result,
                "out_dir": out_dir}

    results = []
    verdicts = []
    run_failed = False
    try:
        wait_server_ready(0, arguments.startup_timeout)
        print("entity2 training: run=%s workers=%d games=%d jobs=%d..%d" %
              (run_dir, workers, arguments.games, resume_rollout_jobs, last_global_job),
              flush=True)
        for cohort_start in range(0, arguments.games, workers):
            cohort_end = min(cohort_start + workers, arguments.games)
            cohort_games = list(range(cohort_start, cohort_end))
            wait_server_ready(cohort_start, arguments.update_timeout)
            executor = ThreadPoolExecutor(max_workers=len(cohort_games))
            future_to_game = {}
            try:
                future_to_game = {executor.submit(run_game, game, slot): game
                                  for slot, game in enumerate(cohort_games)}
                cohort_results = [future.result() for future in as_completed(future_to_game)]
            except BaseException:
                stop_children()
                for future in future_to_game:
                    future.cancel()
                raise
            finally:
                executor.shutdown(wait=True, cancel_futures=True)
            cohort_verdicts = []
            cohort_entries = []
            for item in sorted(cohort_results, key=lambda entry: entry["game"]):
                game = item["game"]
                job = item["job"]
                if item["status"] == "ok":
                    result = item["result"]
                    owners = result.get("owners", [])
                    own = next((o for o in owners if o.get("owner") == 1), {})
                    opp = next((o for o in owners if o.get("owner") == 2), {})
                    if opp and not opp.get("alive", True) and own.get("alive"):
                        verdict = "WIN"
                    elif own and not own.get("alive", True):
                        verdict = "DEATH"
                    else:
                        verdict = "TRUNC"
                    results.append(result)
                    verdicts.append(verdict)
                    cohort_verdicts.append(verdict)
                    cohort_entries.append((game, verdict, item["out_dir"], result))
                    print("game %d job %d: %s %s frame=%s units=%s value=%s lost=%s" %
                          (game, job, verdict, result.get("reason", "unknown"),
                           result.get("end_frame"), own.get("units"),
                           own.get("unit_value"), own.get("unit_value_lost")),
                          flush=True)
                else:
                    run_failed = True
                    detail = (": " + item["error"]) if item.get("error") else ""
                    print("game %d job %d: %s%s" % (game, job, item["status"].upper(),
                                                    detail), flush=True)
            wait_server_ready(cohort_end, arguments.update_timeout)
            cohort_index = cohort_start // workers + 1
            cohort_total = (arguments.games + workers - 1) // workers
            print("cohort %d/%d: WIN %d DEATH %d TRUNC %d | cumulative WIN %d/%d (%.0f%%)" %
                  (cohort_index, cohort_total, cohort_verdicts.count("WIN"),
                   cohort_verdicts.count("DEATH"), cohort_verdicts.count("TRUNC"),
                   verdicts.count("WIN"), len(verdicts),
                   100.0 * verdicts.count("WIN") / max(len(verdicts), 1)), flush=True)
            if arguments.latest_replays:
                copied = publish_latest_cohort(install_dir, arguments.latest_replays, run_id,
                                               cohort_index, cohort_entries)
                print("latest replays: %d games -> %s" % (
                    copied, os.path.join(install_dir, "Replays",
                                         arguments.latest_replays + "_g*")), flush=True)
            relay_server_lines()
    finally:
        stop_children()
        if server.poll() is None:
            try:
                server.wait(timeout=30.0)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
        server_log.close()
    print("training run complete: %d/%d games -> %s" %
          (len(results), arguments.games, out_path), flush=True)
    print("verdicts: WIN %d / DEATH %d / TRUNC %d" %
          (verdicts.count("WIN"), verdicts.count("DEATH"), verdicts.count("TRUNC")),
          flush=True)
    print("run artifacts: %s" % run_dir, flush=True)
    print("P2PDrop artifacts: %s" % replay_run_dir, flush=True)
    return 1 if run_failed else 0


if __name__ == "__main__":
    sys.exit(main())
