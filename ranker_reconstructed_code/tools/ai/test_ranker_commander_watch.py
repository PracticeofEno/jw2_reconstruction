"""A failed/newer job must not replace the latest completed replay."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import ranker_commander_watch as watch
from ranker_commander_strategy import write_strategy


class LatestReplayTests(unittest.TestCase):
    def match(self, work, name, finished, **changes):
        output = work / f"elf/update_{name}/games/game_00000/output"
        output.mkdir(parents=True)
        result = dict(end_frame=60000, reason="max_frames")
        row = dict(valid=True, teacher=False, status=3, own_tribe=1, tribe=0,
                   seed=10, end_frame=60000, result=result, **changes)
        (output / "job.json").write_text(json.dumps(row), encoding="utf-8")
        (output / "ai_selfplay_replay.ply").write_bytes(b"recorded commands")
        path = output / "ai_selfplay_result.json"
        path.write_text(json.dumps(result), encoding="utf-8")
        os.utime(path, ns=(finished * 1_000_000_000, finished * 1_000_000_000))
        return output

    def test_completion_time_wins_over_update_name_and_rollout_can_be_deleted(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            expected = self.match(work, "00001", 20)
            self.match(work, "00099", 10)
            latest = watch.latest_match([work])
            self.assertEqual(latest.replay.parent, expected)
            self.assertEqual(latest.report["status"], 3)
            self.assertFalse((expected / "commander.rlo").exists())

    def test_failed_teacher_and_inconsistent_newer_games_are_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            expected = self.match(work, "1", 10)
            for name, change in (("2", {"valid": False}), ("3", {"teacher": True}),
                                  ("4", {"end_frame": 1})):
                output = self.match(work, name, int(name) * 10)
                path = output / "job.json"
                row = json.loads(path.read_text())
                row.update(change)
                path.write_text(json.dumps(row))
            self.assertEqual(watch.latest_match([work]).replay.parent, expected)

    def test_missing_replay_and_in_progress_receipt_are_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            expected = self.match(work, "1", 10)
            output = self.match(work, "2", 20)
            (output / "ai_selfplay_replay.ply").unlink()
            output = self.match(work, "3", 30)
            (output / "job.json").write_text('{"valid":')
            self.assertEqual(watch.latest_match([work]).replay.parent, expected)

    def test_verified_reward_search_is_visible_but_zero_scaffold_is_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            work=Path(temporary)
            self.match(work,'nn',10)
            exe=work/'ranker_rebuild.exe';exe.write_bytes(b'profile executor')
            expected=None
            for name,purpose,finished in [('search','search',20),('zero','baseline',30)]:
                binding=write_strategy(work/name,[0]*6,exe,purpose=purpose)
                output=self.match(work,name,finished)
                path=output/'job.json';row=json.loads(path.read_text())
                row.update(teacher=True,elf_strategy_profile=binding,strategy_profile_verified=True,command=[str(exe)])
                path.write_text(json.dumps(row))
                if purpose=='search':expected=output
            latest=watch.latest_match([work])
            self.assertEqual(latest.replay.parent,expected)
            self.assertEqual(latest.describe()['policy_kind'],'reward_optimized_elf_strategy')

    def test_router_zero_branch_is_visible_and_wrong_route_is_skipped(self):
        # Router integrity has separate tests; exercise the viewer's boundary
        # using actual immutable child profiles and a retained .ply only.
        with tempfile.TemporaryDirectory() as temporary:
            work=Path(temporary)
            exe=work/'ranker_rebuild.exe';exe.write_bytes(b'router executor')
            zero=write_strategy(work/'zero',[0]*6,exe,purpose='baseline')
            other=write_strategy(work/'other',[0,0,0,0,1,0],exe,purpose='search')
            router=dict(manifest=str(work/'router.json'),manifest_sha256='f'*64,
                definition=dict(kind='reward_optimized_elf_contextual_strategy'))
            expected=None
            for name,profile,finished in [('correct',zero,20),('wrong',other,30)]:
                output=self.match(work,name,finished)
                path=output/'job.json';row=json.loads(path.read_text())
                row.update(teacher=True,elf_strategy_profile=profile,strategy_profile_verified=True,
                    elf_strategy_router=router,command=[str(exe)])
                path.write_text(json.dumps(row))
                if name=='correct':expected=output
            with mock.patch('ranker_commander_strategy_router.validate_router',return_value=router['definition']), \
                    mock.patch('ranker_commander_strategy_router.select_strategy',return_value=zero):
                latest=watch.latest_match([work])
            self.assertEqual(latest.replay.parent,expected)
            self.assertEqual(latest.describe()['policy_sha256'],router['manifest_sha256'])
            self.assertEqual(latest.describe()['strategy_profile_sha256'],zero['definition']['profile_sha256'])
            self.assertIsNone(latest.describe()['policy_version'])


if __name__ == "__main__":
    unittest.main()
