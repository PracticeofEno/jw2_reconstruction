"""Exercise CLI checkpoint transitions without collecting games or optimizing."""
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch

import ranker_commander_train as trainer
from ranker_commander_model import CommanderPolicy
from ranker_commander_rollout import Episode, RECORD_DTYPE


class ResumeSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def run_cli(self, metadata, extra=(), mode="ppo"):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            source, target = directory / "initial.bin", directory / "updated.bin"
            trainer.save_checkpoint(CommanderPolicy(), source, version=7, metadata=metadata)
            episode = Episode(directory / "input.rlo", 1, 1, 7, np.zeros(2, dtype=RECORD_DTYPE))
            captured = []

            def capture(policy, batch, config, **kwargs):
                captured.append(config)
                return None, {"optimizer_steps": 0}

            args = [mode, "--policy", str(source), "--out", str(target),
                    "--rollouts", str(episode.path), "--threads", "1", *extra]
            if mode == "ppo":
                args.append("--no-bc-control")
            with patch.object(trainer, "load_cohort", return_value=([episode], [])) as load, \
                 patch.object(trainer, "build_batch", return_value=({}, {})), \
                 patch.object(trainer, "train_update", side_effect=capture), \
                 redirect_stdout(io.StringIO()):
                try:
                    self.assertEqual(trainer.main(args), 0)
                except SystemExit:
                    load.assert_not_called()
                    self.assertFalse(target.exists())
                    raise
            saved = json.loads(target.with_suffix(".bin.json").read_text(encoding="utf-8"))
            self.assertEqual(len(captured), 1)
            return saved

    @staticmethod
    def resumed_metadata():
        return {"mode": "ppo", "iteration": 4, "curriculum": 2,
                "training_config": {"teacher_kl_initial": .5, "teacher_kl_floor": .1,
                                    "teacher_kl_decay": 100, "critic_warmup": 10,
                                    "gamma": .999, "gae_lambda": .98}}

    def test_ppo_resume_restores_curriculum(self):
        saved = self.run_cli(self.resumed_metadata())
        self.assertEqual(saved["curriculum"], 2)
        self.assertEqual(saved["iteration"], 5)

    def test_ppo_resume_restores_anchor_warmup_and_return_settings(self):
        metadata = self.resumed_metadata()
        saved = self.run_cli(metadata)
        for name, expected in metadata["training_config"].items():
            with self.subTest(setting=name):
                self.assertEqual(saved["training_config"][name], expected, name)

    def test_explicit_overrides_including_zero_take_precedence(self):
        saved = self.run_cli(self.resumed_metadata(), ["--curriculum", "0",
            "--teacher-kl-initial", "0", "--teacher-kl-floor", "0",
            "--teacher-kl-decay", "7", "--critic-warmup", "0", "--gamma", "1",
            "--gae-lambda", "0"])
        self.assertEqual(saved["curriculum"], 0)
        expected = dict(teacher_kl_initial=0, teacher_kl_floor=0, teacher_kl_decay=7,
                        critic_warmup=0, gamma=1, gae_lambda=0)
        self.assertEqual({name: saved["training_config"][name] for name in expected}, expected)

    def test_legacy_ppo_metadata_keeps_original_defaults(self):
        saved = self.run_cli({"mode": "ppo", "iteration": 4})
        self.assertEqual(saved["curriculum"], 0)
        expected = dict(teacher_kl_initial=.05, teacher_kl_floor=0, teacher_kl_decay=30,
                        critic_warmup=0, gamma=.997, gae_lambda=.95)
        self.assertEqual({name: saved["training_config"][name] for name in expected}, expected)

    def test_new_ppo_does_not_inherit_non_ppo_settings(self):
        saved = self.run_cli({"mode": "explicit_checkpoint", "curriculum": 2,
            "training_config": self.resumed_metadata()["training_config"]})
        self.assertEqual(saved["curriculum"], 0)
        defaults = trainer.TrainConfig()
        for name in self.resumed_metadata()["training_config"]:
            self.assertEqual(saved["training_config"][name], getattr(defaults, name))

    def test_invalid_saved_return_setting_is_rejected_before_collection(self):
        metadata = self.resumed_metadata()
        metadata["training_config"]["gamma"] = 0
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                self.run_cli(metadata)
            self.assertEqual(error.exception.code, 2)

    def test_explicit_valid_setting_overrides_invalid_saved_value(self):
        metadata = self.resumed_metadata()
        metadata["training_config"]["gae_lambda"] = -1
        saved = self.run_cli(metadata, ["--gae-lambda", ".98"])
        self.assertEqual(saved["training_config"]["gae_lambda"], .98)

    def test_invalid_cli_return_setting_is_rejected(self):
        for flag, value in (("--gamma", "nan"), ("--gamma", "0"),
                            ("--gae-lambda", "inf"), ("--gae-lambda", "1.01")):
            with self.subTest(flag=flag, value=value), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    self.run_cli({}, [flag, value])
                self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
