import copy
import unittest

from ranker_commander_holdout import gate, measured_layouts, reserved_jobs
from ranker_commander_improve import builtin_jobs


class HoldoutTests(unittest.TestCase):
    def setUp(self):
        self.sources = {r: dict(path=f'{r}.bin', sha256=f'hash{r}', version=r) for r in range(4)}
        self.layouts = [dict(seed=1819000000+i, start_pair=[a,b]) for i,(a,b) in enumerate(
            (a,b) for a in range(4) for b in range(4) if a != b)]
        self.seed = 90000
        def reports(deterministic, offset):
            return [dict(j, job=i, valid=True, evaluation_valid=True, status=1,
                teacher=False, deterministic=deterministic, policy_seed_verified=True,
                weights_sha256=self.sources[j['own_tribe']]['sha256'], weight_version=j['own_tribe'],
                reason='elimination', commander_metrics=dict(mask_violations=0)) for i,j in enumerate(
                    builtin_jobs(self.sources, self.layouts, policy_seed_base=self.seed+offset))]
        self.argmax, self.sampling = reports(True,0), reports(False,10000)

    def result(self):
        return gate(self.argmax, self.sampling, self.sources, self.layouts,
                    excluded_seeds=[1,2,3,17], policy_seed=self.seed)

    def test_gate_needs_both_complete_modes_and_every_race(self):
        self.assertTrue(self.result()['passed'])
        self.sampling[191]['status'] = 2
        self.assertFalse(self.result()['passed'])
        self.sampling.pop()
        with self.assertRaises(ValueError):
            self.result()

    def test_reused_training_seed_and_changed_roster_are_rejected(self):
        self.layouts[0]['seed'] = 17
        with self.assertRaisesRegex(ValueError, 'reuse'):
            self.result()
        self.layouts[0]['seed'] = 1819000000
        self.sampling[0]['weights_sha256'] = 'different'
        with self.assertRaises(ValueError):
            self.result()

    def test_discovery_is_never_counted_as_a_win_evaluation(self):
        self.argmax[0].update(max_frames=64, end_frame=64, status=3)
        with self.assertRaises(ValueError):
            self.result()

    def test_layouts_take_first_observation_of_each_pair_without_outcome_selection(self):
        reports = [dict(j, valid=True, purpose='layout_discovery_only', max_frames=64, end_frame=64)
                   for j in self.layouts]
        reports.insert(1, dict(reports[0], seed=1900000000))
        self.assertEqual(measured_layouts(reports, excluded_seeds=[1,2,3]), self.layouts)
        reports[0]['valid'] = False
        with self.assertRaises(ValueError):
            measured_layouts(reports, excluded_seeds=[])

    def test_reserved_seed_range_cannot_overlap_or_wrap(self):
        for base,count,excluded in [(1,12,[9]), (0,12,[]), (0xfffffff0,64,[])]:
            with self.assertRaises(ValueError):
                reserved_jobs(self.sources[0], excluded_seeds=excluded, seed_base=base, count=count)
        self.assertEqual(len(reserved_jobs(self.sources[0], excluded_seeds=[1,2,3],
                                          seed_base=1819000000, count=128)), 128)

    def test_false_win_or_mask_violation_cannot_pass(self):
        for key,value in [('reason','max_frames'), ('policy_seed_verified',False),
                ('commander_metrics',dict(mask_violations=1))]:
            rows = copy.deepcopy(self.argmax)
            self.argmax[0][key] = value
            with self.assertRaises(ValueError):
                self.result()
            self.argmax = rows


if __name__ == '__main__':
    unittest.main()
