# -*- coding: utf-8 -*-
"""Entity-mode online training driver (plan section 13.2 steps 4-5).

Loops headless entity-mode matches against the built-in Computer opponent
while one act2 learner/server accepts a synchronous cohort of game workers.
All workers in a cohort use one pinned policy version; the learner merges
their owner trajectories and updates exactly once after the cohort barrier.

Usage:
    python ranker_entity_train.py --install-dir C:/.../RankerOCPV_Win \
        --policy entity_bc_gen0.pt --out entity_online.pt \
        --games 20 --workers 4 --max-frames 20000 \
        [--port 6001] [--seed0 100]
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


READY_PATTERN = re.compile(r"ranker_entity_server:\s+READY\s+(\d+)\s*$")


def read_ready_count(path: str):
    """Return the cumulative READY barrier, or None before server bind."""
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", required=True)
    parser.add_argument("--exe", default="ranker_rebuild.exe")
    parser.add_argument("--policy", default="",
                        help="initial checkpoint (empty = fresh net)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--io", default="entity_train_io",
                        help="base directory for isolated training runs")
    parser.add_argument("--run-id", default="",
                        help="run subdirectory name (default: timestamp-pid)")
    parser.add_argument("--games", type=int, default=10)
    parser.add_argument("--workers", type=int, default=4,
                        help="parallel rollout games per PPO cohort")
    parser.add_argument("--max-frames", type=int, default=20000)
    parser.add_argument("--port", type=int, default=6001)
    parser.add_argument("--net-offset-base", type=int, default=100,
                        help="first -AINET offset reserved for this run")
    parser.add_argument("--seed0", type=int, default=100)
    parser.add_argument("--policy-seed", type=int, default=1,
                        help="stable learner/sampling seed (kept on resume)")
    parser.add_argument("--reset-lineage", action="store_true",
                        help="load policy weights with fresh optimizer/RNG/jobs")
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--issue-prior", type=float, default=None,
                        help="BC-free start: calibrated KEEP gate prior "
                             "for the fresh net (plan 10.1)")
    parser.add_argument("--max-train-steps", type=int, default=1024,
                        help="sampled timestep budget per rollout worker; "
                             "the cohort cap scales by --workers")
    parser.add_argument("--macro-policy", default="",
                        help="trained legacy macro checkpoint for the server")
    parser.add_argument("--game-timeout", type=float, default=900.0)
    parser.add_argument("--startup-timeout", type=float, default=30.0)
    parser.add_argument("--update-timeout", type=float, default=900.0)
    parser.add_argument("--accept-timeout", type=float, default=30.0,
                        help="server wait for each worker connection")
    parser.add_argument("--reply-timeout-ms", type=int, default=15000,
                        help="game-side ACT/TERMINAL reply deadline")
    arguments = parser.parse_args()

    if arguments.games < 1:
        parser.error("--games must be at least 1")
    if arguments.workers < 1:
        parser.error("--workers must be at least 1")
    if arguments.max_frames < 1:
        parser.error("--max-frames must be at least 1")
    if not 1 <= arguments.seed0 <= 0xffffffff:
        parser.error("--seed0 must be in 1..4294967295")
    if arguments.max_train_steps < 1 or arguments.epochs < 1:
        parser.error("--max-train-steps and --epochs must be at least 1")
    workers = min(arguments.workers, arguments.games)
    if not 1 <= arguments.port <= 65535 - workers + 1:
        parser.error("--port .. --port+workers-1 must be in 1..65535")
    if min(arguments.game_timeout, arguments.startup_timeout,
           arguments.update_timeout, arguments.accept_timeout) <= 0:
        parser.error("timeouts must be positive")
    if not 1000 <= arguments.reply_timeout_ms <= 60000:
        parser.error("--reply-timeout-ms must be in 1000..60000")
    if (arguments.net_offset_base < 1 or
            arguments.net_offset_base + workers - 1 > 50000):
        parser.error("--net-offset-base worker range must be in 1..50000")
    inherited_offset = os.environ.get("RANKER_RECONSTRUCTED_PORT_OFFSET", "")
    if inherited_offset not in ("", "0"):
        parser.error("RANKER_RECONSTRUCTED_PORT_OFFSET must be unset/0; "
                     "entity workers use -AINET exclusively")

    tools_dir = os.path.dirname(os.path.abspath(__file__))
    install_dir = os.path.abspath(arguments.install_dir)
    executable = os.path.join(install_dir, arguments.exe)
    if not os.path.isfile(executable):
        parser.error("game executable not found: %s" % executable)

    policy_path = os.path.abspath(arguments.policy) \
        if arguments.policy else ""
    macro_policy_path = os.path.abspath(arguments.macro_policy) \
        if arguments.macro_policy else ""
    out_path = os.path.abspath(arguments.out)
    if policy_path and not os.path.isfile(policy_path):
        parser.error("policy checkpoint not found: %s" % policy_path)
    if macro_policy_path and not os.path.isfile(macro_policy_path):
        parser.error("macro checkpoint not found: %s" % macro_policy_path)
    if (os.path.exists(out_path) and
            (not policy_path or
             os.path.normcase(policy_path) != os.path.normcase(out_path))):
        parser.error(
            "existing --out may only be resumed by passing the same path "
            "as --policy")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    resume_rollout_jobs = 0
    expected_policy_fingerprint = ""
    if policy_path:
        try:
            import ranker_entity_bc as entity_bc
            import ranker_entity_server as entity_server
            resume_net, resume_payload = \
                entity_bc.load_checkpoint_payload(policy_path)
            resume_extra = resume_payload.get("extra", {})
            online_resume = False if arguments.reset_lineage else \
                entity_server.validate_learner_resume_extra(resume_extra)
            expected_policy_fingerprint = \
                entity_server.module_fingerprint(resume_net).hex()
            resume_rollout_jobs = int(resume_extra["rollout_jobs"]) \
                if online_resume else 0
            saved_environment_seed = resume_extra.get(
                "environment_seed_base")
            if (online_resume and saved_environment_seed is not None and
                    int(saved_environment_seed) != arguments.seed0):
                parser.error(
                    "--seed0 differs from checkpoint environment seed base")
            if (online_resume and
                    int(resume_extra["cohort_workers"]) !=
                    arguments.workers):
                parser.error(
                    "--workers differs from checkpoint cohort width")
            if (online_resume and
                    int(resume_extra["environment_max_frames"]) !=
                    arguments.max_frames):
                parser.error(
                    "--max-frames differs from checkpoint environment "
                    "horizon")
            del resume_net, resume_extra, resume_payload
        except Exception as exc:
            parser.error("cannot inspect policy checkpoint %s: %s" %
                         (policy_path, exc))
    if resume_rollout_jobs < 0:
        parser.error("checkpoint rollout_jobs must not be negative")
    last_global_job = resume_rollout_jobs + arguments.games - 1
    if arguments.seed0 + last_global_job > 0xffffffff:
        parser.error("environment seed range exceeds uint32")

    run_id = arguments.run_id or "%s-%d" % (
        time.strftime("%Y%m%d-%H%M%S"), os.getpid())
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", run_id):
        parser.error("--run-id may contain only letters, digits, . _ -")
    io_base = os.path.abspath(arguments.io)
    run_dir = os.path.join(io_base, run_id)
    try:
        os.makedirs(run_dir, exist_ok=False)
    except FileExistsError:
        parser.error("run directory already exists: %s" % run_dir)
    # The reconstructed executable's legacy -AIOUT parser stops at spaces
    # and has a fixed path buffer.  Fail before spawning instead of silently
    # sending every worker to the wrong/shared directory.
    longest_out = os.path.join(run_dir, "g%09d" % last_global_job)
    if any(char.isspace() for char in longest_out):
        parser.error("-AIOUT paths cannot contain whitespace: %s" % run_dir)
    if len(os.fsencode(longest_out)) >= 260:
        parser.error("-AIOUT directory exceeds its 260-byte buffer: %s" %
                     longest_out)
    longest_ai_output = os.path.join(
        longest_out, "ai_entity_shadow_4294967295.bin")
    if os.name == "nt" and len(os.fsencode(longest_ai_output)) >= 260:
        parser.error("worker AI output path is too long: %s" %
                     longest_ai_output)

    replay_root = os.path.abspath(os.path.join(install_dir, "Replays"))
    replay_run_dir = os.path.abspath(os.path.join(
        replay_root, "EntityTrain", run_id))
    if os.path.commonpath([replay_root, replay_run_dir]) != replay_root:
        parser.error("worker replay directory escaped install Replays")
    # Leave room for the longest P2PDrop timestamp/trigger/frame/sequence leaf
    # on legacy Windows path handling before any process is started.
    capture_leaf = (
        "P2PDrop_YYYYMMDD_HHMMSS_remote_player_inactive_"
        "f4294967295_s4294967295.sync.csv")
    longest_replay = os.path.join(
        replay_run_dir, "g%09d" % last_global_job, capture_leaf)
    if os.name == "nt" and len(os.fsencode(longest_replay)) >= 260:
        parser.error("worker P2PDrop path is too long: %s" % longest_replay)
    try:
        # This is also the cross-launcher lock for a user-supplied run-id.
        os.makedirs(replay_run_dir, exist_ok=False)
    except FileExistsError:
        parser.error("replay run directory already exists: %s" %
                     replay_run_dir)
    except OSError as exc:
        parser.error("cannot reserve replay run directory %s: %s" %
                     (replay_run_dir, exc))

    server_log_path = os.path.join(run_dir, "server.log")
    server_log = open(server_log_path, "w", encoding="utf-8")
    server_cmd = [sys.executable,
                  os.path.join(tools_dir, "ranker_entity_server.py"),
                  "--port", str(arguments.port),
                  "--train", "--out", out_path,
                  "--episodes", str(arguments.games),
                  "--workers", str(arguments.workers),
                  "--seed", str(arguments.policy_seed),
                  "--environment-seed-base", str(arguments.seed0),
                  "--environment-max-frames", str(arguments.max_frames),
                  "--accept-timeout", str(arguments.accept_timeout),
                  "--connection-timeout",
                  str(arguments.game_timeout + 30.0),
                  "--reply-timeout-ms", str(arguments.reply_timeout_ms),
                  "--lr", str(arguments.lr),
                  "--epochs", str(arguments.epochs)]
    if policy_path:
        server_cmd += ["--policy", policy_path]
        server_cmd += ["--expected-rollout-jobs",
                       str(resume_rollout_jobs),
                       "--expected-policy-fingerprint",
                       expected_policy_fingerprint]
    if arguments.reset_lineage:
        server_cmd += ["--reset-lineage"]
    if arguments.issue_prior is not None:
        server_cmd += ["--issue-prior", str(arguments.issue_prior)]
    server_cmd += ["--max-train-steps", str(arguments.max_train_steps)]
    if macro_policy_path:
        server_cmd += ["--macro-policy", macro_policy_path]
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

    def wait_server_ready(count: int, timeout_s: float = 900.0) -> None:
        """Wait for an exact cumulative cohort barrier."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            ready = read_ready_count(server_log_path)
            if ready is not None and ready >= count:
                return
            if server.poll() is not None:
                raise RuntimeError(
                    "act2 server exited before READY %d; see %s" %
                    (count, server_log_path))
            time.sleep(0.2)
        raise RuntimeError("server READY %d timeout; see %s" %
                           (count, server_log_path))

    def run_game(game: int, worker_slot: int):
        global_job = resume_rollout_jobs + game
        out_dir = os.path.join(run_dir, "g%09d" % global_job)
        os.makedirs(out_dir, exist_ok=False)
        worker_replay_dir = os.path.join(
            replay_run_dir, "g%09d" % global_job)
        os.makedirs(worker_replay_dir, exist_ok=False)
        game_cmd = [executable, "-AISELF",
                    "-AIENTITY:%d" % (arguments.port + worker_slot),
                    "-AINET:%d" %
                    (arguments.net_offset_base + worker_slot),
                    "-MAXFRAMES:%d" % arguments.max_frames,
                    "-SEED:%d" % (arguments.seed0 + global_job),
                    "-AIOUT:%s" % out_dir]
        log_path = os.path.join(out_dir, "process.log")
        game_env = os.environ.copy()
        game_env["RANKER_RECONSTRUCTED_LOG_PATH"] = os.path.join(
            out_dir, "Jw2.log")
        game_env["RANKER_RECONSTRUCTED_REPLAY_DIR"] = os.path.join(
            worker_replay_dir)
        try:
            with open(log_path, "w", encoding="utf-8") as game_log:
                process = subprocess.Popen(
                    game_cmd, cwd=install_dir, stdout=game_log,
                    stderr=subprocess.STDOUT, env=game_env)
                with child_lock:
                    child_processes.add(process)
                try:
                    deadline = time.monotonic() + arguments.game_timeout
                    while process.poll() is None:
                        server_code = server.poll()
                        if (abort_event.is_set() or
                                (server_code is not None and
                                 server_code != 0)):
                            process.terminate()
                            try:
                                process.wait(timeout=5.0)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
                            return {"game": game, "status": "aborted",
                                    "job": global_job,
                                    "out_dir": out_dir}
                        if time.monotonic() >= deadline:
                            process.kill()
                            process.wait()
                            return {"game": game, "status": "timeout",
                                    "job": global_job,
                                    "out_dir": out_dir}
                        time.sleep(0.2)
                finally:
                    with child_lock:
                        child_processes.discard(process)
                if process.returncode != 0:
                    return {"game": game, "status": "exit-code",
                            "job": global_job,
                            "error": str(process.returncode),
                            "out_dir": out_dir}
        except Exception as exc:  # one failed worker must leave the barrier
            return {"game": game, "status": "spawn-error",
                    "job": global_job,
                    "error": "%s: %s" % (type(exc).__name__, exc),
                    "out_dir": out_dir}

        result_path = os.path.join(out_dir, "ai_selfplay_result.json")
        if not os.path.exists(result_path):
            return {"game": game, "status": "no-result",
                    "job": global_job,
                    "out_dir": out_dir}
        try:
            with open(result_path, encoding="utf-8") as handle:
                result = json.load(handle)
        except (OSError, json.JSONDecodeError) as exc:
            return {"game": game, "status": "bad-result",
                    "job": global_job,
                    "error": str(exc), "out_dir": out_dir}
        return {"game": game, "job": global_job,
                "status": "ok", "result": result,
                "out_dir": out_dir}

    results = []
    run_failed = False
    try:
        wait_server_ready(0, arguments.startup_timeout)
        print("entity training: run=%s workers=%d games=%d jobs=%d..%d" %
              (run_dir, workers, arguments.games, resume_rollout_jobs,
               last_global_job), flush=True)
        for cohort_start in range(0, arguments.games, workers):
            cohort_end = min(cohort_start + workers, arguments.games)
            cohort_games = list(range(cohort_start, cohort_end))
            wait_server_ready(cohort_start, arguments.update_timeout)
            executor = ThreadPoolExecutor(max_workers=len(cohort_games))
            future_to_game = {}
            try:
                future_to_game = {
                    executor.submit(run_game, game, slot): game
                    for slot, game in enumerate(cohort_games)
                }
                cohort_results = [
                    future.result()
                    for future in as_completed(future_to_game)
                ]
            except BaseException:
                stop_children()
                for future in future_to_game:
                    future.cancel()
                raise
            finally:
                executor.shutdown(wait=True, cancel_futures=True)
            for item in sorted(cohort_results,
                               key=lambda entry: entry["game"]):
                game = item["game"]
                job = item["job"]
                if item["status"] == "ok":
                    result = item["result"]
                    owners = result.get("owners", [])
                    own = next((owner for owner in owners
                                if owner.get("owner") == 1), {})
                    results.append(result)
                    print("game %d job %d: %s frame=%s units=%s value=%s "
                          "lost=%s" %
                          (game, job, result.get("reason", "unknown"),
                           result.get("end_frame"), own.get("units"),
                           own.get("unit_value"),
                           own.get("unit_value_lost")), flush=True)
                else:
                    run_failed = True
                    detail = (": " + item["error"]) \
                        if item.get("error") else ""
                    print("game %d job %d: %s%s" %
                          (game, job, item["status"].upper(), detail),
                          flush=True)
            # No new game starts until all accepted connections have closed,
            # the learner has updated once, and the checkpoint is published.
            wait_server_ready(cohort_end, arguments.update_timeout)
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
    print("run artifacts: %s" % run_dir, flush=True)
    print("P2PDrop artifacts: %s" % replay_run_dir, flush=True)
    return 1 if run_failed else 0


if __name__ == "__main__":
    sys.exit(main())
