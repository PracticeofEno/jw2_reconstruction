"""Policy RNG seed plumbing and sampled-case identity; no game processes run."""
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import ranker_commander_eval as evaluation


class PolicySeedEvaluationTests(unittest.TestCase):
    @staticmethod
    def report(**overrides):
        return {"seed": 9, "tribe": 0, "start_pair": [0, 1], "policy_seed": 0,
                "valid": True, "evaluation_valid": True, "deterministic": False,
                "teacher": False, "win": True, "end_frame": 22000, **overrides}

    def test_command_and_job_record_preserve_omission_zero_and_u64_maximum(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            install = root / "install"
            install.mkdir()
            (install / "ranker_rebuild.exe").write_bytes(b"not executed")
            weights = root / "weights.bin"
            weights.write_bytes(b"not loaded")
            for index, seed in enumerate((None, 0, evaluation.MAX_POLICY_SEED)):
                job = {"seed": 9, "tribe": 0}
                if seed is not None:
                    job["policy_seed"] = seed
                with self.subTest(seed=seed), patch.object(evaluation.subprocess, "Popen",
                        side_effect=OSError("test launch disabled")) as launch:
                    report = evaluation._run_game(install, weights, root / "jobs", index, job, 2,
                        teacher=False, deterministic=False, timeout=1)
                    output = Path(report["rollout"]).parent
                    command = [str(install / "ranker_rebuild.exe"), "-AISELF", "-AICOMMANDER",
                        f"-AIWEIGHTS:{weights}", f"-AIROLLOUT:{output / 'commander.rlo'}",
                        "-AINET:302", "-SEED:9", "-AITRIBE:0", "-MAXFRAMES:60000",
                        f"-AIOUT:{output}", "-AICURRICULUM:2", "-AIAUTOSCOUT:1"]
                    if seed is not None:
                        command.append(f"-AIPOLICYSEED:{seed}")
                        self.assertEqual(report["policy_seed"], seed)
                    else:
                        self.assertNotIn("policy_seed", report)
                    command.append("-AINOSLEEP")
                    self.assertEqual(report["command"], command)
                    self.assertEqual(launch.call_args.args[0], command)
                    self.assertEqual(json.loads((output / "job.json").read_text()), report)

    def test_invalid_job_seed_is_rejected_before_files_or_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "never-created"
            for seed in (True, False, -1, 1 << 64, 1.0, "1", None):
                job = {"seed": 9, "tribe": 0, "policy_seed": seed}
                with self.subTest(seed=seed), patch.object(evaluation, "prepare_job_directory") as prepare:
                    with self.assertRaisesRegex(ValueError, "policy_seed"):
                        evaluation._run_game("install", "weights", output, 0, job, 0,
                            teacher=False, deterministic=False, timeout=1)
                    prepare.assert_not_called()
                    with patch.object(evaluation, "_run_game") as play:
                        with self.assertRaisesRegex(ValueError, "policy_seed"):
                            evaluation.run_games("install", "weights", output, [job], stagger=0)
                        play.assert_not_called()
                    self.assertFalse(output.exists())

    def test_engine_must_confirm_requested_seed_before_job_is_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            install = root / "install"
            install.mkdir()
            (install / "ranker_rebuild.exe").write_bytes(b"not executed")
            weights = root / "weights.bin"
            weights.write_bytes(b"not loaded")
            def launch(command, **kwargs):
                output = Path(next(value.removeprefix("-AIOUT:") for value in command if value.startswith("-AIOUT:")))
                (output / "ai_selfplay_result.json").write_text(json.dumps({"end_frame": 22000, "reason": "game_end"}))
                (output / "Jw2.log").write_text("start-slots: owner=1 state=1 map_slot=0 tribe=2\n"
                    "start-slots: owner=2 state=1 map_slot=1 tribe=0\n" + confirmation)
                return Mock(wait=Mock(return_value=0))
            episode = SimpleNamespace(owner=1, seed=9, weight_version=8,
                terminal={"frame": 22000, "status": evaluation.WIN}, decisions=[0], close=Mock())
            correct = "ai-commander: policy-seed-override owner=1 policy_seed=0 rng_version_salt=0 game_seed=9 model_version=8\n"
            for index, confirmation in enumerate(("", correct.replace("policy_seed=0", "policy_seed=1"), correct)):
                with self.subTest(log=confirmation), patch.object(evaluation.subprocess, "Popen", side_effect=launch), \
                        patch.object(evaluation, "read_rollout", return_value=episode):
                    report = evaluation._run_game(install, weights, root / "jobs", index,
                        {"seed": 9, "tribe": 0, "policy_seed": 0}, 0,
                        teacher=False, deterministic=False, timeout=1)
                self.assertEqual(report["valid"], index == 2)
                if index == 2:
                    self.assertIs(report["policy_seed_verified"], True)
                else:
                    self.assertIn("not confirmed by engine", report["reason"])
                    self.assertNotIn("policy_seed_verified", report)

    def test_engine_confirmation_checks_owner_version_salt_and_full_u64(self):
        seed = evaluation.MAX_POLICY_SEED
        line = f"ai-commander: policy-seed-override owner=2 policy_seed={seed} rng_version_salt=0 game_seed=9 model_version=11\n"
        options = dict(owner=2, policy_seed=seed, game_seed=9, model_version=11)
        evaluation.validate_policy_seed_log(line, **options)
        for malformed in (line + line, line.replace("owner=2", "owner=1"),
                line.replace("rng_version_salt=0", "rng_version_salt=11"),
                line.replace("game_seed=9", "game_seed=10"), line.replace("model_version=11", "model_version=12")):
            with self.subTest(log=malformed), self.assertRaisesRegex(RuntimeError, "not confirmed"):
                evaluation.validate_policy_seed_log(malformed, **options)

    def test_schedule_assigns_unique_explicit_seeds_without_changing_layout_jobs(self):
        old = evaluation.evaluation_jobs({}, sampling=True)
        for base in (0, evaluation.MAX_POLICY_SEED - 199):
            jobs = evaluation.evaluation_jobs({}, sampling=True, policy_seed_base=base)
            self.assertEqual([job["policy_seed"] for job in jobs], list(range(base, base + 200)))
            self.assertEqual([{k: v for k, v in job.items() if k != "policy_seed"} for job in jobs], old)
        self.assertTrue(all("policy_seed" not in job for job in old))
        with self.assertRaisesRegex(ValueError, "exceed"):
            evaluation.evaluation_jobs({}, sampling=True, policy_seed_base=evaluation.MAX_POLICY_SEED - 198)
        with self.assertRaisesRegex(ValueError, "sampled"):
            evaluation.evaluation_jobs({}, policy_seed_base=0)

    def test_cli_passes_explicit_seed_schedule_and_rejects_invalid_options(self):
        def collect(install, weights, output, jobs, **options):
            Path(output).mkdir()
            self.assertFalse(options["deterministic"])
            self.assertEqual([job["policy_seed"] for job in jobs], list(range(200)))
            return [self.report(**job) for job in jobs]

        with tempfile.TemporaryDirectory() as directory, patch.object(evaluation, "run_games", side_effect=collect), \
                redirect_stdout(io.StringIO()):
            output = Path(directory) / "cli"
            self.assertEqual(evaluation.main(["sample", "--install-dir", "assets", "--weights", "policy.bin",
                "--io", str(output), "--policy-seed-base", "0"]), 0)
            summary = json.loads((output / "summary.json").read_text())
            self.assertEqual(summary["unique_sampling_cases"], 200)
            self.assertTrue(summary["coverage_complete"])
        for value in ("-1", "+1", "1.0", "1x", str(1 << 64), str(evaluation.MAX_POLICY_SEED)):
            with self.subTest(value=value), patch.object(evaluation, "run_games") as play, \
                    redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                evaluation.main(["sample", "--io", "unused", "--policy-seed-base=" + value])
            play.assert_not_called()
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            evaluation.main(["argmax", "--io", "unused", "--policy-seed-base", "0"])

    def test_policy_seeds_distinguish_samples_but_repeated_layout_seeds_do_not(self):
        first = self.report()
        independent = self.report(policy_seed=1, win=False)
        original = evaluation.summarize_evaluation([first, independent], expected_games=2)
        self.assertTrue(original["coverage_complete"])
        self.assertEqual(original["unique_sampling_cases"], 2)
        self.assertEqual(original["wilson_games"], 2)
        repeated = [first, independent, {**first, "seed": 2009}, {**independent, "seed": 2010}]
        summary = evaluation.summarize_evaluation(repeated, expected_games=4)
        self.assertFalse(summary["coverage_complete"])
        self.assertEqual(summary["wilson_games"], 2)
        self.assertEqual(summary["wilson_95"], original["wilson_95"])
        repeated[-1]["win"] = True
        conflicting = evaluation.summarize_evaluation(repeated, expected_games=4)
        self.assertFalse(conflicting["sampling_outcomes_consistent"])
        self.assertIsNone(conflicting["wilson_95"])

    def test_layout_and_tribe_are_part_of_sample_identity_and_start_pair_is_required(self):
        rows = [self.report(), self.report(start_pair=[1, 0]), self.report(tribe=1)]
        summary = evaluation.summarize_evaluation(rows, expected_games=3)
        self.assertTrue(summary["coverage_complete"])
        self.assertEqual(summary["unique_sampling_cases"], 3)
        del rows[0]["start_pair"]
        with self.assertRaisesRegex(ValueError, "measured start_pair"):
            evaluation.summarize_evaluation(rows, expected_games=3)

    def test_argmax_and_rule_actions_do_not_gain_samples_from_policy_seed(self):
        rows = [self.report(deterministic=True), self.report(deterministic=True, policy_seed=1)]
        summary = evaluation.summarize_evaluation(rows, expected_games=2)
        self.assertEqual(summary["wilson_games"], 1)
        self.assertEqual(summary["wilson_basis"], "unique_deterministic_conditions_descriptive_only")
        rows = [self.report(teacher=True), self.report(teacher=True, policy_seed=1)]
        summary = evaluation.summarize_evaluation(rows, expected_games=2)
        self.assertEqual(summary["wilson_games"], 1)
        self.assertFalse(summary["coverage_complete"])


if __name__ == "__main__":
    unittest.main()
