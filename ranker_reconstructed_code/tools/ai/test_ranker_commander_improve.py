import copy
import unittest
from pathlib import Path
import tempfile

import numpy as np
import torch

from ranker_commander_improve import (Campaign, builtin_jobs, choose_rosters, pin, summarize,
                                      validate_reports)
from ranker_commander_model import CommanderPolicy, HEAD_SIZES, MAP_SHAPE, load_weights
from ranker_commander_rollout import RECORD_DTYPE, write_rollout
from ranker_commander_train import save_checkpoint


class ImprovementGateTests(unittest.TestCase):
    def setUp(self):
        self.current = {r: dict(path=f'race{r}.bin', sha256=f'hash{r}', version=r, race=r) for r in range(4)}
        self.layouts = [dict(seed=i + 1, start_pair=[a,b]) for i,(a,b) in enumerate(
            (a,b) for a in range(4) for b in range(4) if a != b)]
        self.jobs = builtin_jobs(self.current, self.layouts, policy_seed_base=10000)
        self.rows = [dict(j, job=i, valid=True, evaluation_valid=True, status=1,
            teacher=False, deterministic=True, weights_sha256=self.current[j['own_tribe']]['sha256'],
            weight_version=j['own_tribe']) for i,j in enumerate(self.jobs)]

    def test_full_gate_requires_all_four_opponents_and_twelve_positions(self):
        self.assertEqual(len(self.jobs), 192)
        validate_reports(self.rows, self.jobs, self.current, deterministic=True)
        self.assertTrue(all(x['passed_100_percent'] for x in summarize(self.rows).values()))
        self.rows[0]['start_pair'] = self.rows[16]['start_pair']
        self.assertFalse(summarize(self.rows)[0]['passed_100_percent'])

    def test_failures_timeouts_invalid_and_slowed_games_never_pass(self):
        for key, value in [('status', 2), ('status', 3), ('valid', False),
                ('teacher', True), ('evaluation_valid', False), ('opp_slow', 2), ('opponent_weights', 'bot.bin')]:
            with self.subTest(key=key, value=value):
                rows = copy.deepcopy(self.rows)
                rows[0][key] = value
                self.assertFalse(summarize(rows)[0]['passed_100_percent'])

    def test_cached_receipts_cannot_change_policy_seed_mode_or_checkpoint(self):
        for key, value in [('policy_seed', 4), ('weight_version', 8),
                ('weights_sha256', 'other'), ('deterministic', False), ('own_tribe', 1)]:
            with self.subTest(key=key):
                rows = copy.deepcopy(self.rows)
                rows[0][key] = value
                with self.assertRaises(ValueError):
                    validate_reports(rows, self.jobs, self.current, deterministic=True)
        with self.assertRaises(ValueError):
            validate_reports(self.rows + [self.rows[0]], self.jobs, self.current, deterministic=True)

    def test_best_roster_never_promotes_a_tie_or_regression(self):
        baseline = summarize(self.rows)
        for s in baseline.values():
            s.update(wins=40, passed_100_percent=False)
        after = copy.deepcopy(baseline)
        for r, wins in enumerate([41,40,38,36]):
            after[r]['wins'] = wins
        candidates = {r: dict(self.current[r], path=f'new{r}.bin') for r in range(4)}
        best, current, decision = choose_rosters(self.current, self.current, candidates,
            baseline, after, {r: True for r in range(4)})
        self.assertEqual(best[0], candidates[0])
        self.assertEqual(best[1], self.current[1])
        self.assertEqual(current[2], candidates[2])
        self.assertEqual(current[3], self.current[3])
        best, _, _ = choose_rosters(self.current, self.current, candidates, baseline, after,
                                   {r: False for r in range(4)})
        self.assertEqual(best, self.current)

    def test_partial_evaluations_cannot_select_champions(self):
        baseline = summarize(self.rows)
        after = summarize(self.rows[1:])
        with self.assertRaises(ValueError):
            choose_rosters(self.current, self.current, self.current, baseline, after, {r: True for r in range(4)})

    def test_real_critic_then_fresh_ppo_fits_export_and_resume_for_four_races(self):
        torch.set_num_threads(2)
        torch.manual_seed(97)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            initial = {}
            for race in range(4):
                path = work / f'initial{race}.bin'
                save_checkpoint(CommanderPolicy(), path, version=10 + race, metadata={})
                initial[race] = dict(**pin(path), race=race, version=10 + race)
            def data(current, label):
                reports, audits = [], []
                for race in range(4):
                    source = current[race]
                    policy = load_weights(source['path'])
                    records = np.zeros(9, dtype=RECORD_DTYPE)
                    records['frame'] = [32,64,96,128,160,192,224,256,20000]
                    records['status'][-1] = 1
                    records['vector'][:,606 + race] = 1
                    records['vector'][:,66] = 1
                    records['mask'][:-1] = 1
                    records['potential'][:,0] = np.linspace(0, .1, 9)
                    records['action'][:-1] = np.array([[i % width for width in HEAD_SIZES] for i in range(8)])
                    with torch.no_grad():
                        out = policy.evaluate(torch.from_numpy(records['vector'][:-1].copy()),
                            torch.zeros(8, *MAP_SHAPE), torch.from_numpy(records['action'][:-1].astype(np.int64)),
                            torch.from_numpy(records['mask'][:-1].copy()).bool(), torch.zeros(8,32))
                    records['logp'][:-1] = out['logp'].numpy()
                    records['value'][:-1] = out['value'].numpy()
                    path = work / f'{label}_{race}.rlo'
                    write_rollout(path, records, owner=1, seed=90 + race, weight_version=source['version'])
                    reports.append(dict(job=race, valid=True, evaluation_valid=True, status=1,
                        own_tribe=race, teacher=False, deterministic=False, dagger=False,
                        seed=90 + race, rollout=str(path), weights_sha256=source['sha256'],
                        weight_version=source['version']))
                    audits.append(dict(rollout=pin(path)))
                return dict(reports=reports, audits=audits)
            campaign = object.__new__(Campaign)
            campaign.directory = work
            campaign.contract = dict(training_seed=72, learning_rate=3e-5,
                                     initial={str(r): s for r,s in initial.items()})
            campaign.check = lambda: None
            campaign.status = lambda *args, **kwargs: None
            campaign.event = lambda **kwargs: None
            warm, gates = campaign.fit('warm', data(initial, 'before'), initial, warm=True)
            self.assertTrue(all(gates.values()))
            for race in range(4):
                before, after = load_weights(initial[race]['path']), load_weights(warm[race]['path'])
                for name, parameter in before.named_parameters():
                    if not name.startswith(('value1.', 'value2.')):
                        self.assertTrue(torch.equal(parameter, after.state_dict()[name]), name)
            # Fresh old_logp and value from the calibrated checkpoints.
            fresh = data(warm, 'fresh')
            trained, gates = campaign.fit('ppo', fresh, warm)
            self.assertTrue(all(gates.values()))
            self.assertEqual(campaign.fit('ppo', fresh, warm), (trained, gates))


if __name__ == '__main__':
    unittest.main()
