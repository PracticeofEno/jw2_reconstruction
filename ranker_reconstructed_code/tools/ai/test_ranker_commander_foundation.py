"""Foundation data must preserve race/action semantics and actual scenarios."""
from copy import deepcopy
from pathlib import Path
import unittest

import ranker_commander_foundation as foundation


def contract():
    return dict(jobs48=[dict(seed=i + 1, tribe=t, start_pair=list(pair), curriculum=2,
                            max_frames=60000, opp_slow=0)
                       for t in range(4) for i, pair in enumerate(sorted(foundation.PAIRS))],
                teacher_gate=dict(full_games=48, minimum_wins=36, minimum_per_opponent=6))


class FoundationTests(unittest.TestCase):
    def test_correct_final_label_cannot_admit_trap_delayed_training_tail(self):
        historical = dict(reason='max_frames', owners=[dict(owner=1, buildings=1),
                                                       dict(owner=2, buildings=32)])
        for status in (foundation.WIN, foundation.LOSS):
            with self.assertRaisesRegex(ValueError, 'trap-delayed.*recollect'):
                foundation.validate_training_elimination(historical, status)
        foundation.validate_training_elimination(historical, 3)  # Genuine time limit.
        corrected = deepcopy(historical)
        corrected['owners'][0]['buildings'] = 0
        foundation.validate_training_elimination(corrected, foundation.LOSS)
        corrected = deepcopy(historical)
        corrected['owners'][1]['buildings'] = 0
        foundation.validate_training_elimination(corrected, foundation.WIN)

    def test_screen_balances_actual_conditions_and_rejects_seed_alias_duplicates(self):
        jobs = contract()['jobs48']
        foundation.validate_plan(jobs)
        duplicate = deepcopy(jobs)
        duplicate[1]['start_pair'] = duplicate[0]['start_pair']
        duplicate[1]['seed'] = 999999  # A new seed cannot make a new start condition.
        with self.assertRaisesRegex(ValueError, '48 distinct'):
            foundation.validate_plan(duplicate)

    def test_collector_excludes_tyrano_and_keeps_teacher_and_policy_action_contract(self):
        teacher = foundation.phase_jobs(contract(), 1, 3221225472, 'teacher')
        policy = foundation.phase_jobs(contract(), 1, 3221225472, 'sampling')
        self.assertEqual(len(teacher), 48)
        for a, b in zip(teacher, policy):
            self.assertEqual({k:v for k,v in a.items() if k != 'dagger'},
                             {k:v for k,v in b.items() if k != 'dagger'})
            self.assertIs(a['coordinated_transfers'], False)
            self.assertFalse(a['dagger'])
            self.assertTrue(b['dagger'])
        with self.assertRaisesRegex(ValueError, 'Tyrano'):
            foundation.phase_jobs(contract(), 2, 0, 'teacher')

    def test_receipt_rejects_other_transfer_mode_or_recipe_or_slow_opponent(self):
        job = foundation.phase_jobs(contract(), 3, 1073741824, 'teacher')[0]
        exe = str(Path('ranker_rebuild.exe').resolve())
        command = [exe, '-AITEACHER', '-AIDETERMINISTIC', '-AICOORDINATEDTRANSFERS:0',
            '-AICURRICULUM:2', '-MAXFRAMES:60000', '-AIOWNTRIBE:3', '-AITRIBE:0',
            f"-SEED:{job['seed']}", f"-AIPOLICYSEED:{job['policy_seed']}", '-AITEACHERVAR:1073741824']
        row = dict(**job, valid=True, evaluation_valid=True, teacher=True, deterministic=True,
                   weights_sha256='weight', command=command, commander_metrics=dict(mask_violations=0))
        validate = lambda r: foundation.validate_receipt(r, job, weights_sha='weight',
            teacher=True, deterministic=True, executable=exe)
        validate(row)
        for old, new in [('-AICOORDINATEDTRANSFERS:0', '-AICOORDINATEDTRANSFERS:1'),
                         ('-AITEACHERVAR:1073741824', '-AITEACHERVAR:0')]:
            changed = deepcopy(row)
            changed['command'][changed['command'].index(old)] = new
            with self.assertRaises(ValueError):
                validate(changed)
        changed = deepcopy(row)
        changed['command'].append('-AIOPPSLOW:4')
        with self.assertRaisesRegex(ValueError, 'normal builtin'):
            validate(changed)

    def test_strong_teacher_needs_full_coverage_and_each_opponent(self):
        summary = dict(games=48, unique_conditions=48, wins=36, mask_violations=0,
                       per_opponent={str(t):dict(games=12, wins=9) for t in range(4)})
        self.assertTrue(foundation.teacher_ready(summary, contract()))
        self.assertFalse(foundation.teacher_ready(dict(summary, games=12, unique_conditions=12), contract()))
        summary['per_opponent']['2']['wins'] = 5
        self.assertFalse(foundation.teacher_ready(summary, contract()))

    def test_holdout_groups_layout_aliases_and_teacher_variants_and_deduplicates(self):
        sources = [dict(scenario=[0, 0, 1], actor_fingerprint='a', seed=1, variant=0),
                   dict(scenario=[0, 0, 1], actor_fingerprint='b', seed=999, variant=7),
                   dict(scenario=[1, 0, 1], actor_fingerprint='c', seed=1, variant=0),
                   dict(scenario=[1, 0, 1], actor_fingerprint='c', seed=2, variant=3)]
        training, validation = foundation.split_sources(sources, {(0, 0, 1)})
        self.assertEqual([s['actor_fingerprint'] for s in training], ['c'])
        self.assertEqual([s['actor_fingerprint'] for s in validation], ['a', 'b'])

    def test_summary_cannot_count_duplicate_scenario_as_an_additional_game(self):
        job = contract()['jobs48'][0]
        row = dict(job, status=1, commander_metrics=dict(mask_violations=0, silent_rejections=0))
        with self.assertRaisesRegex(ValueError, 'repeated deterministic'):
            foundation.summarize([row, dict(row, seed=999)], [job, dict(job, seed=999)])


if __name__ == '__main__':
    unittest.main()
