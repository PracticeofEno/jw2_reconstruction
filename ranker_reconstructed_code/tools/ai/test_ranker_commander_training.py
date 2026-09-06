"""Targeted binary, reward, optimizer, and worker-isolation regression checks."""
from pathlib import Path
from collections import Counter
from contextlib import redirect_stderr, redirect_stdout
import io
import hashlib
import json
import random
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch

from ranker_commander_eval import (curriculum_promotion, discover_seed_mapping,
    evaluation_jobs, extract_start_slots, prepare_job_directory, sample_league_opponent,
    aggregate_commander_metrics, assess_bc_gate, summarize_evaluation, validate_terminal_result, wilson_interval)
from ranker_commander_model import CommanderPolicy, load_weights
from ranker_commander_league import (champion_challenge_jobs, champion_gate, curriculum_jobs,
                                     league_jobs)
from ranker_commander_rollout import (DECISION, Episode, HEADER, HEAD_OFFSETS, HEAD_SIZES,
    LOSS, MAP_SHAPE, RECORD_DTYPE, RECORD_SIZE, RolloutError, TRUNCATED, WIN,
    episode_returns, potential_components, read_rollout, write_rollout)
from ranker_commander_train import (TrainConfig, build_batch, load_cohort, load_optimizer,
    LazyBcBatch, assess_bc_accuracy, main as train_main, ppo_admission, save_checkpoint,
    split_teacher_episodes, train_update)


def fixture(status=WIN, teacher=False):
    records = np.zeros(4, dtype=RECORD_DTYPE)
    records["frame"] = [32, 64, 80, 20000]
    records["status"][-1] = status
    records["teacher"] = int(teacher)
    records["value"] = [0.1, 0.2, 0.3, 0.4]
    for offset in HEAD_OFFSETS:
        records["mask"][:-1, offset] = 1
    records["potential"] = [potential_components(400 * step, 100, 500 * step, step, 1)
                              for step in range(4)]
    records["terminal_reward"][-1] = 1.2 if status == WIN else -1 if status == LOSS else 0
    return records


class RolloutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "episode.rlo"

    def read(self, **kwargs):
        episode = read_rollout(self.path, **kwargs)
        self.addCleanup(episode.records._mmap.close)
        return episode

    def test_roundtrip_packed_wire_and_version(self):
        write_rollout(self.path, fixture(), owner=1, seed=22, weight_version=7)
        episode = self.read(current_version=8)
        self.assertEqual((episode.owner, episode.seed, episode.weight_version), (1, 22, 7))
        self.assertEqual(self.path.stat().st_size, HEADER.size + 4 * RECORD_SIZE)
        self.assertEqual(RECORD_SIZE, 3522)
        self.assertEqual(episode.records.dtype.itemsize, RECORD_DTYPE.itemsize)
        self.assertEqual(episode.records["map"].shape, (4, 2304))
        with self.assertRaisesRegex(RolloutError, "version"):
            read_rollout(self.path, current_version=9)
        with self.assertRaisesRegex(RolloutError, "version"):
            read_rollout(self.path, current_version=6)

    def test_partial_and_crc_and_schema_rejected(self):
        write_rollout(self.path, fixture())
        raw = self.path.read_bytes()
        for altered, expected in ((raw[:-1], "partial"),
                                  (raw[:8] + b"\0\0\0\0" + raw[12:], "schema"),
                                  (raw[:100] + bytes([raw[100] ^ 1]) + raw[101:], "CRC32")):
            self.path.write_bytes(altered)
            with self.assertRaisesRegex(RolloutError, expected):
                read_rollout(self.path)

    def test_terminal_required_illegal_action_and_nonfinite_rejected(self):
        for mutation, expected in ((lambda row: row["status"].fill(0), "terminal"),
                                  (lambda row: row["action"].__setitem__((0, 0), 2), "masked"),
                                  (lambda row: row["vector"].__setitem__((0, 3), np.nan), "nonfinite")):
            records = fixture()
            mutation(records)
            with self.assertRaisesRegex(RolloutError, expected):
                write_rollout(self.path, records)

    def test_teacher_filter_and_incomplete_cohort(self):
        write_rollout(self.path, fixture(teacher=True))
        episodes, errors = load_cohort([self.path], version=0, teacher=False)
        self.assertFalse(episodes)
        self.assertIn("teacher", errors[0]["reason"])

    def test_terminal_masks_may_be_empty(self):
        records = fixture()
        records["action"][-1].fill(255)
        write_rollout(self.path, records)
        self.assertEqual(int(self.read().terminal["status"]), WIN)

    def test_zero_duration_final_action_is_not_a_training_transition(self):
        records = fixture(TRUNCATED)
        records["frame"] = [32, 64, 96, 96]
        # Host writers replace the final decision with the same-frame terminal.
        # A stale/corrupt file retaining both must not create a zero-dt sample.
        with self.assertRaisesRegex(RolloutError, "frames must increase"):
            write_rollout(self.path, records)
        canonical = np.concatenate((records[:-2], records[-1:]))
        write_rollout(self.path, canonical)
        episode = self.read()
        target = episode_returns(episode, iteration=300)
        self.assertEqual(episode.decisions["frame"].tolist(), [32, 64])
        self.assertEqual(len(target["advantage"]), 2)
        self.assertAlmostEqual(float(target["discount"][-1]), 0.997, places=6)
        self.assertAlmostEqual(float(target["mc_return"][-1]),
                               0.997 * float(records[-1]["value"]), places=6)


class ReturnTests(unittest.TestCase):
    def episode(self, records):
        return Episode(Path("synthetic.rlo"), 1, 1, 0, records)

    def test_shaping_telescopes_and_time_discount(self):
        records = fixture()
        target = episode_returns(self.episode(records))
        self.assertAlmostEqual(float(target["discount"][1]), 0.997 ** 0.5, places=6)
        weights = np.r_[1.0, np.cumprod(target["discount"][:-1])]
        shaped_return = np.sum(weights * target["shape"].sum(axis=1))
        self.assertAlmostEqual(float(shaped_return), -float(records["potential"][0].sum()), places=6)
        self.assertAlmostEqual(float(target["terminal"][-1]), 1.2, places=6)

    def test_truncation_bootstraps_and_true_terminal_zeroes_value(self):
        records = fixture(TRUNCATED)
        records["frame"] = [32, 64, 96, 128]
        records["potential"].fill(0)
        records["value"].fill(2)
        truncated = episode_returns(self.episode(records), iteration=300)
        self.assertAlmostEqual(float(truncated["mc_return"][-1]), 0.997 * 2, places=6)
        self.assertTrue(np.all(truncated["terminal"] == 0))
        records["status"][-1] = LOSS
        records["terminal_reward"][-1] = -1
        loss = episode_returns(self.episode(records), iteration=300)
        self.assertEqual(float(loss["mc_return"][-1]), -1)
        self.assertAlmostEqual(float(loss["mc_return"][0]), -(0.997 ** 2), places=6)

    def test_no_event_reward_and_potential_fades(self):
        records = fixture()
        records["event"] = [1, 3, 255, 0]
        first = episode_returns(self.episode(records), iteration=300)
        records["event"].fill(0)
        second = episode_returns(self.episode(records), iteration=300)
        np.testing.assert_array_equal(first["reward"], second["reward"])
        self.assertTrue(np.all(first["shape"] == 0))


class TrainerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_bc_update_and_safe_checkpoint(self):
        torch.manual_seed(2)
        policy = CommanderPolicy()
        records = fixture(teacher=True)
        records["mask"][:-1].fill(1)
        episode = Episode(Path("teacher.rlo"), 1, 1, 0, records)
        config = TrainConfig(mode="bc", epochs=2, minibatch=3)
        batch, metrics = build_batch([episode], config)
        before = policy.heads[0].weight.detach().clone()
        optimizer, report = train_update(policy, batch, config)
        self.assertFalse(torch.equal(before, policy.heads[0].weight))
        self.assertTrue(np.isfinite(report["loss"]))
        self.assertEqual(metrics["decisions"], 3)
        with tempfile.TemporaryDirectory() as directory:
            checkpoint = Path(directory) / "commander.bin"
            save_checkpoint(policy, checkpoint, version=8, metadata=metrics, optimizer=optimizer)
            restored = load_weights(checkpoint)
            self.assertEqual(restored.weight_version, 8)
            torch.testing.assert_close(policy.heads[0].weight, restored.heads[0].weight)
            restored_optimizer = load_optimizer(torch.optim.Adam(restored.parameters()),
                checkpoint.with_suffix(".bin.optimizer.npz"), version=8)
            expected_state = optimizer.state_dict()["state"]
            actual_state = restored_optimizer.state_dict()["state"]
            for parameter in expected_state:
                for name in expected_state[parameter]:
                    torch.testing.assert_close(expected_state[parameter][name], actual_state[parameter][name])
            with self.assertRaisesRegex(ValueError, "version"):
                load_optimizer(restored_optimizer, checkpoint.with_suffix(".bin.optimizer.npz"), version=9)

    def test_ppo_uses_saved_joint_masked_probabilities(self):
        torch.manual_seed(3)
        policy = CommanderPolicy()
        records = fixture(TRUNCATED)
        records["mask"][:-1].fill(1)
        with torch.no_grad():
            output = policy.sample(torch.zeros(3, 528), torch.zeros(3, *MAP_SHAPE),
                                   torch.ones(3, 95, dtype=torch.bool), torch.zeros(3, 32))
        records["action"][:-1] = output["action"].numpy()
        records["mask"][:-1] = output["mask"].numpy()
        records["logp"][:-1] = output["logp"].numpy()
        records["value"][:-1] = output["value"].reshape(-1).numpy()
        episode = Episode(Path("policy.rlo"), 1, 1, 0, records)
        config = TrainConfig(mode="ppo", epochs=1, minibatch=3)
        batch, _ = build_batch([episode], config)
        _, report = train_update(policy, batch, config, teacher_policy=CommanderPolicy())
        self.assertAlmostEqual(report["approximate_kl"], 0.0, places=6)
        self.assertEqual(report["clip_fraction"], 0.0)
        self.assertEqual(report["teacher_kl_coefficient"], 0.05)
        self.assertFalse(report["critic_only"])

    def test_warm_start_admits_gameplay_pass_without_accuracy_and_rejects_neither(self):
        from ranker_commander_train import ppo_admission
        gate = {"weight_version": 1, "policy_sha256": "abc", "complete": True,
                "gameplay_passed": True, "passed": False}
        metadata = {"bc_gate": gate, "bc_validation": {"accuracy_passed": False}, "weights_sha256": "abc"}
        admission = ppo_admission(metadata, version=1, weights_sha256="abc", warm_start=True)
        self.assertEqual(admission["mode"], "bc_warm_start")
        self.assertTrue(admission["gameplay_passed"])
        self.assertFalse(admission["accuracy_passed"])
        neither = {"bc_gate": {**gate, "gameplay_passed": False},
                   "bc_validation": {"accuracy_passed": False}, "weights_sha256": "abc"}
        with self.assertRaises(ValueError):
            ppo_admission(neither, version=1, weights_sha256="abc", warm_start=True)
        with self.assertRaises(ValueError):
            ppo_admission(metadata, version=1, weights_sha256="abc", warm_start=False)

    def test_teacher_kl_schedule_holds_floor_and_critic_warmup_freezes_actor(self):
        schedule = TrainConfig(mode="ppo", teacher_kl_initial=0.5, teacher_kl_floor=0.1, teacher_kl_decay=100)
        self.assertAlmostEqual(schedule.teacher_coefficient(), 0.5)
        schedule.iteration = 50
        self.assertAlmostEqual(schedule.teacher_coefficient(), 0.3)
        schedule.iteration = 400
        self.assertAlmostEqual(schedule.teacher_coefficient(), 0.1)
        released = TrainConfig(mode="ppo", iteration=30)
        self.assertEqual(released.teacher_coefficient(), 0.0)
        policy = CommanderPolicy()
        records = fixture()
        records["mask"][:-1].fill(1)
        with torch.no_grad():
            output = policy.sample(torch.from_numpy(records["vector"][:-1].astype(np.float32)),
                                   torch.from_numpy(records["map"][:-1].copy()).float() / 255,
                                   torch.ones(3, 95, dtype=torch.bool), torch.zeros(3, 32))
        records["action"][:-1] = output["action"].numpy()
        records["mask"][:-1] = output["mask"].numpy()
        records["logp"][:-1] = output["logp"].numpy()
        records["value"][:-1] = output["value"].reshape(-1).numpy()
        episode = Episode(Path("policy.rlo"), 1, 1, 0, records)
        config = TrainConfig(mode="ppo", epochs=1, minibatch=3, critic_warmup=2,
                             teacher_kl_initial=0.0, teacher_kl_floor=0.0)
        batch, _ = build_batch([episode], config)
        before = [parameter.detach().clone() for parameter in policy.heads.parameters()]
        _, report = train_update(policy, batch, config, teacher_policy=None)
        self.assertTrue(report["critic_only"])
        self.assertEqual(report["teacher_kl_coefficient"], 0.0)
        for previous, parameter in zip(before, policy.heads.parameters()):
            self.assertTrue(torch.equal(previous, parameter.detach()))

    def test_bc_holds_out_seed_groups_and_does_not_claim_gameplay_pass(self):
        records = fixture(teacher=True)
        records["mask"][:-1].fill(1)
        episodes = [Episode(Path(f"teacher_{seed}_{owner}.rlo"), owner, seed, 0, records)
                    for seed in range(1, 21) for owner in (1, 2)]
        training, held_out = split_teacher_episodes(episodes)
        self.assertFalse({episode.seed for episode in training} & {episode.seed for episode in held_out})
        self.assertEqual(len(held_out), 4)
        policy = CommanderPolicy()
        with torch.no_grad():
            for parameter in policy.parameters():
                parameter.zero_()
        measured = assess_bc_accuracy(policy, held_out)
        self.assertTrue(measured["accuracy_passed"])
        self.assertFalse(measured["passed"])
        with torch.no_grad():
            policy.heads[3].bias[1] = 10
        self.assertFalse(assess_bc_accuracy(policy, held_out)["accuracy_passed"])

    def test_bc_export_never_deletes_caller_inputs_and_requires_complete_teacher_collection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for keep in (False, True):
                inputs = root / ("keep" if keep else "discard")
                inputs.mkdir()
                for seed in (1, 2):
                    write_rollout(inputs / f"teacher_{seed}.rlo", fixture(teacher=True), seed=seed)
                arguments = ["bc", "--rollouts", str(inputs), "--out", str(root / f"bc_{keep}.bin"),
                             "--epochs", "1", "--threads", "1", "--minibatch", "3"]
                if keep:
                    arguments.append("--keep-rollouts")
                with redirect_stdout(io.StringIO()):
                    train_main(arguments)
                # --rollouts inputs belong to the caller; only rollouts a run
                # collected itself are subject to the default discard.
                self.assertEqual(len(list(inputs.glob("*.rlo"))), 2)
                metadata = json.loads((root / f"bc_{keep}.bin.json").read_text())
                self.assertEqual(metadata["bc_validation"]["episodes"], 1)
                self.assertFalse(metadata["bc_validation"]["passed"])
                self.assertEqual(metadata["curriculum"], 2)
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                train_main(["bc", "--install-dir", str(root), "--out", str(root / "unused.bin"),
                            "--teacher-games", "399"])
            reports = [{"valid": True, "rollout": "unused.rlo"}] * 399 + [{"valid": False}]
            # The failed game is replayed once (second run_games call); when the
            # replay fails too, BC collection is incomplete and must abort.
            with patch("ranker_commander_eval.run_games",
                       side_effect=[reports, [{"valid": False, "reason": "game process timeout"}]]), \
                    self.assertRaisesRegex(RuntimeError, "every requested teacher game"):
                train_main(["bc", "--install-dir", str(root), "--io", str(root / "collection"),
                            "--out", str(root / "unused.bin"), "--teacher-games", "400", "--threads", "1"])

    def test_400_compact_teacher_files_feed_lazy_minibatches_and_hashed_bc_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = []
            for seed in range(1, 401):
                records = fixture(teacher=True)
                records["vector"][:, 0] = seed / 400
                records["mask"][:-1].fill(1)
                paths.append(write_rollout(root / f"teacher_{seed}.rlo", records, seed=seed))
            episodes, rejected = load_cohort(paths, version=0, teacher=True)
            self.assertFalse(rejected)
            try:
                training, held_out = split_teacher_episodes(episodes)
                batch, metrics = build_batch(training, TrainConfig(mode="bc"))
                self.assertIsInstance(batch, LazyBcBatch)
                self.assertEqual(metrics["decisions"], 1080)
                indices = torch.tensor([1079, 0, 300, 1])
                selected = batch["vector"][indices][:, 0].numpy()
                expected = np.concatenate([episode.decisions["vector"][:, 0] for episode in training])[indices.numpy()]
                np.testing.assert_array_equal(selected, expected)
                policy = CommanderPolicy()
                with torch.no_grad():
                    for parameter in policy.parameters():
                        parameter.zero_()
                validation = assess_bc_accuracy(policy, held_out)
                validation["teacher_games_total"] = 400
                self.assertTrue(validation["accuracy_passed"])
                checkpoint = root / "bc.bin"
                save_checkpoint(policy, checkpoint, version=7, metadata={"mode": "bc", "bc_validation": validation})
                metadata = json.loads(checkpoint.with_suffix(".bin.json").read_text())
                fingerprint = hashlib.sha256(checkpoint.read_bytes()).hexdigest()
                mapping = {(a, b): 1 + 4 * a + b for a in range(4) for b in range(4) if a != b}
                reports = [{**job, "valid": True, "evaluation_valid": True, "deterministic": True,
                    "win": True, "end_frame": 20000, "weight_version": 7, "weights_sha256": fingerprint}
                    for job in evaluation_jobs(mapping)]
                gate = assess_bc_gate(validation, [{**row, "teacher": True} for row in reports],
                    [{**row, "teacher": False} for row in reports], version=7,
                    policy_sha256=fingerprint, teacher_sha256=fingerprint)
                metadata["bc_gate"] = gate
                self.assertEqual(ppo_admission(metadata, version=7, weights_sha256=fingerprint)["mode"], "bc_approved")
                save_checkpoint(CommanderPolicy(), checkpoint, version=7, metadata={})
                with self.assertRaisesRegex(ValueError, "requires"):
                    ppo_admission(metadata, version=7, weights_sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest())
            finally:
                for episode in episodes:
                    episode.close()


class EvaluationTests(unittest.TestCase):
    def test_cap_elimination_uses_rollout_outcome_and_keeps_invalid_checks(self):
        # A surviving trap can keep the broad legacy JSON count positive.
        # The RLO WIN/LOSS came from the actual end-condition elimination check.
        result = {"reason": "max_frames", "end_frame": 60000, "result_code": 0,
                  "owners": [{"owner": 1, "alive": True, "buildings": 2},
                             {"owner": 2, "alive": True, "buildings": 1}]}
        for status, reward in ((WIN, 1.0), (LOSS, -1.0), (TRUNCATED, 0.0)):
            records = fixture(status)
            records["frame"][-1] = 60000
            records["terminal_reward"][-1] = reward
            episode = Episode(Path("cap.rlo"), 1, 1, 0, records)
            self.assertEqual(validate_terminal_result(episode, result), status)
            targets = episode_returns(episode, iteration=300)
            self.assertEqual(float(targets["terminal"][-1]), reward)
        with self.assertRaisesRegex(RuntimeError, "frame-limit"):
            validate_terminal_result(episode, {**result, "reason": "game_end"})
        with self.assertRaisesRegex(RuntimeError, "frame.*disagree"):
            validate_terminal_result(episode, {**result, "end_frame": 59999})
        early = fixture(WIN)
        early["frame"][-1] = 100
        with self.assertRaisesRegex(RuntimeError, "early invalid"):
            validate_terminal_result(Episode(Path("early.rlo"), 1, 1, 0, early),
                                     {"reason": "game_end", "end_frame": 100, "result_code": 2})

    def test_measured_permutations_and_sampling_schedule(self):
        reports = [{"valid": True, "start_pair": [a, b], "seed": 1 + 4 * a + b}
                   for a in range(4) for b in range(4) if a != b]
        mapping = discover_seed_mapping(reports)
        self.assertEqual(len(evaluation_jobs(mapping)), 48)
        sampled = evaluation_jobs({}, sampling=True)
        self.assertEqual(len(sampled), 200)
        self.assertEqual({job["seed"] for job in sampled}, set(range(100, 150)))
        with self.assertRaisesRegex(ValueError, "12 measured"):
            evaluation_jobs({})

    def test_incomplete_or_repeated_wins_cannot_pass(self):
        row = {"valid": True, "evaluation_valid": True, "win": True, "tribe": 2,
               "start_pair": [0, 1], "deterministic": True, "end_frame": 22000}
        summary = summarize_evaluation([row] * 48, expected_games=48)
        self.assertFalse(summary["passed_100_percent"])
        self.assertFalse(summarize_evaluation([row] * 47, expected_games=48)["passed_100_percent"])
        self.assertLess(wilson_interval(48, 48)[0], 1.0)

    def test_deterministic_repeats_do_not_narrow_interval(self):
        mapping = {(a, b): 1 + 4 * a + b for a in range(4) for b in range(4) if a != b}
        reports = [{**job, "valid": True, "evaluation_valid": True,
                    "deterministic": True, "win": index < 40, "end_frame": 22000}
                   for index, job in enumerate(evaluation_jobs(mapping))]
        original = summarize_evaluation(reports, expected_games=48)
        repeated = reports + [{**row, "seed": row["seed"] + 1000} for row in reports]
        summary = summarize_evaluation(repeated, expected_games=96)
        self.assertEqual(summary["wins"], 80)
        self.assertTrue(summary["coverage_complete"])
        self.assertEqual(summary["wilson_95"], original["wilson_95"])
        self.assertEqual(summary["wilson_games"], 48)
        self.assertEqual(summary["wilson_basis"], "unique_deterministic_conditions_descriptive_only")
        self.assertTrue(summary["deterministic_outcomes_consistent"])
        repeated[-1] = {**repeated[-1], "win": True}
        inconsistent = summarize_evaluation(repeated, expected_games=96)
        self.assertFalse(inconsistent["deterministic_outcomes_consistent"])
        self.assertIsNone(inconsistent["wilson_95"])
        sampled = summarize_evaluation([{**row, "deterministic": False} for row in reports], expected_games=48)
        self.assertEqual(sampled["wilson_95"], wilson_interval(40, 48))
        self.assertEqual(sampled["wilson_basis"], "policy_sampling")

    def test_worker_cwd_does_not_share_mutable_files_or_copy_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            install = root / "install"
            install.mkdir()
            (install / "ranker_rebuild.exe").write_bytes(b"fixture")
            (install / "_SETUP.DAT").write_bytes(b"setup")
            (install / "Result.txt").write_text("old")
            (install / "JW2_09.trc").write_bytes(b"asset")
            (install / "unrelated_download.zip").write_bytes(b"archive")
            (install / "rlout").mkdir()
            (install / "rlout" / "large.json").write_text("diagnostic")
            (install / "ai_trace.jsonl").write_text("diagnostic")
            job = prepare_job_directory(install, root / "job")
            (job / "_SETUP.DAT").write_bytes(b"changed")
            self.assertEqual((install / "_SETUP.DAT").read_bytes(), b"setup")
            self.assertFalse((job / "ranker_rebuild.exe").exists())
            self.assertFalse((job / "Result.txt").exists())
            self.assertEqual((job / "JW2_09.trc").read_bytes(), b"asset")
            self.assertFalse((job / "unrelated_download.zip").exists())
            self.assertFalse((job / "rlout").exists())
            self.assertFalse((job / "ai_trace.jsonl").exists())
            build = root / "build"
            build.mkdir()
            (build / "ranker_rebuild.exe").write_bytes(b"new-build")
            (install / "ranker_rebuild.exe").unlink()
            prepare_job_directory(install, root / "build_job", build / "ranker_rebuild.exe")

    def test_start_log_and_league_mixture(self):
        slots = extract_start_slots("start-slots: owner=1 state=1 map_slot=2 faction=1 tribe=2 xy=1,2")
        self.assertEqual(slots[1]["map_slot"], 2)
        rng = random.Random(1)
        opponents = [sample_league_opponent(["a", "b"], {"a": 0.9, "b": 0.1}, rng) for _ in range(1000)]
        builtins = sum(item["kind"] == "builtin" for item in opponents)
        self.assertTrue(150 <= builtins <= 250)
        self.assertGreater(sum(item.get("weights") == "b" for item in opponents),
                           sum(item.get("weights") == "a" for item in opponents))


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.mapping = {(a, b): 1 + 4 * a + b for a in range(4) for b in range(4) if a != b}

    def reports(self, jobs, deterministic, wins=None):
        return [{**job, "valid": True, "evaluation_valid": True, "deterministic": deterministic,
                 "win": wins is None or index < wins, "end_frame": 22000,
                 "start_pair": job.get("start_pair", [job["seed"] % 4, (job["seed"] + 1) % 4])}
                for index, job in enumerate(jobs)]

    def test_curriculum_gates_require_full_distinct_coverage(self):
        for stage in range(4):
            argmax = self.reports(curriculum_jobs(self.mapping, stage, sampling=False), True)
            sampled = self.reports(curriculum_jobs(self.mapping, stage, sampling=True), False)
            self.assertTrue(curriculum_promotion(stage, argmax, sampled)["promote"])
            self.assertFalse(curriculum_promotion(stage, argmax[:-1], sampled)["promote"])
            sampled[-1] = sampled[0]
            self.assertFalse(curriculum_promotion(stage, argmax, sampled)["promote"])
        sampled = self.reports(curriculum_jobs(self.mapping, 2, sampling=True), False, wins=194)
        self.assertTrue(curriculum_promotion(2, self.reports(evaluation_jobs(self.mapping), True), sampled)["promote"])
        self.assertFalse(curriculum_promotion(3, self.reports(evaluation_jobs(self.mapping), True), sampled)["promote"])

    def test_league_builtin_quota_and_champion_gate(self):
        jobs = league_jobs({"old": "old.bin"}, {}, seed=1, start_game=0, games=100, primary="main.bin")
        self.assertEqual(sum(job["opponent_key"] == "builtin" for job in jobs), 20)
        self.assertEqual(Counter(job["tribe"] for job in jobs if job["opponent_key"] == "builtin"),
                         {0: 5, 1: 5, 2: 5, 3: 5})
        catalog = {pair: list(range(100 + index * 6, 106 + index * 6)) for index, pair in enumerate(sorted(self.mapping))}
        challenges = champion_challenge_jobs(catalog, "challenger.bin", "champion.bin")
        reports = self.reports(challenges, False)
        for index, report in enumerate(reports):
            winner = WIN if index < 55 else LOSS
            report["status"] = winner if report["evaluate_owner"] == 1 else (LOSS if winner == WIN else WIN)
            report["status2"] = LOSS if report["status"] == WIN else WIN
        builtin = self.reports(evaluation_jobs(self.mapping), True)
        self.assertTrue(champion_gate(reports, builtin)["promote"])
        self.assertFalse(champion_gate(reports, builtin[:-1])["promote"])
        reports[54]["status2"] = LOSS
        reports[54]["status"] = WIN
        self.assertFalse(champion_gate(reports, builtin)["promote"])
        with self.assertRaisesRegex(ValueError, "more measured"):
            champion_challenge_jobs({pair: [seed] for pair, seed in self.mapping.items()}, "a", "b")

    def test_bc_gameplay_gate_and_ppo_admission_bind_exact_checkpoint(self):
        teacher = self.reports(evaluation_jobs(self.mapping), True, wins=40)
        policy = self.reports(evaluation_jobs(self.mapping), True, wins=38)
        for row in teacher:
            row.update(teacher=True, weight_version=7, weights_sha256="candidate-hash")
        for row in policy:
            row.update(teacher=False, weight_version=7, weights_sha256="candidate-hash")
        validation = {"held_out": True, "decisions": 100, "H1_accuracy": .85,
                      "H3_accuracy": .80, "H4_accuracy": .80, "accuracy_passed": True,
                      "teacher_games_total": 400}
        gate = assess_bc_gate(validation, teacher, policy, version=7,
                              policy_sha256="candidate-hash", teacher_sha256="candidate-hash")
        self.assertTrue(gate["passed"])
        metadata = {"mode": "bc", "bc_validation": validation, "bc_gate": gate,
                    "weights_sha256": "candidate-hash"}
        self.assertEqual(ppo_admission(metadata, version=7, weights_sha256="candidate-hash")["mode"], "bc_approved")
        with self.assertRaisesRegex(ValueError, "requires"):
            ppo_admission(metadata, version=7, weights_sha256="different-weights-same-version")
        with self.assertRaisesRegex(ValueError, "requires"):
            ppo_admission({"bc_validation": validation}, version=7, weights_sha256="candidate-hash")
        self.assertEqual(ppo_admission({}, version=0, no_bc_control=True, fresh=True)["initialization"], "fresh_random")
        policy[37]["win"] = False
        self.assertFalse(assess_bc_gate(validation, teacher, policy, version=7,
            policy_sha256="candidate-hash", teacher_sha256="candidate-hash")["passed"])
        policy[37]["win"] = True
        teacher[-1] = teacher[0]
        self.assertFalse(assess_bc_gate(validation, teacher, policy, version=7,
            policy_sha256="candidate-hash", teacher_sha256="candidate-hash")["passed"])

    def test_metrics_aggregate_counts_and_does_not_invent_global_latency_percentile(self):
        report = {"commander_metrics": {"end_frame": 20000, "decisions": 100,
            "packets": 2000, "interrupts": 10, "silent_rejections": 2, "mask_violations": 0,
            "kills_investment": 4000, "losses_investment": 2000, "inference_p99_ms": .4}}
        metrics = aggregate_commander_metrics([report, report])
        self.assertEqual(metrics["K_over_L"], 2)
        self.assertEqual(metrics["packets_per_frame"], .1)
        self.assertEqual(metrics["interrupt_rate"], .1)
        self.assertEqual(metrics["silent_rejections"], 4)
        self.assertEqual(metrics["worst_game_inference_p99_ms"], .4)
        self.assertIsNone(metrics["worst_game_inference_p50_ms"])


if __name__ == "__main__":
    unittest.main()
