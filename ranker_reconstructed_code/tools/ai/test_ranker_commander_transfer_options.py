"""A requested transfer experiment must be applied by the actual engine."""
import unittest
from unittest.mock import patch

import ranker_commander_eval as evaluation


class TransferOptionTests(unittest.TestCase):
    def test_missing_duplicate_wrong_owner_or_wrong_mode_confirmation_is_rejected(self):
        correct = "ai-commander: coordinated-transfers owner=1 enabled=1\n"
        evaluation.validate_transfer_mode_log(correct, owner=1, enabled=True)
        evaluation.validate_transfer_mode_log(correct.replace("enabled=1", "enabled=0"), owner=1, enabled=False)
        for bad in ("", correct + correct, correct.replace("owner=1", "owner=2"),
                    correct.replace("enabled=1", "enabled=0")):
            with self.subTest(log=bad), self.assertRaisesRegex(RuntimeError, "not confirmed"):
                evaluation.validate_transfer_mode_log(bad, owner=1, enabled=True)

    def test_non_boolean_experiment_option_fails_before_game_creation(self):
        for value in (0, 1, "0", "1", None):
            with self.subTest(value=value), patch.object(evaluation, "prepare_job_directory") as prepare:
                with self.assertRaisesRegex(ValueError, "boolean"):
                    evaluation._run_game("install", "model", "output", 0,
                        {"seed": 1, "tribe": 0, "coordinated_transfers": value}, 0,
                        teacher=False, deterministic=True, timeout=1)
                prepare.assert_not_called()


if __name__ == "__main__":
    unittest.main()
