import copy
from pathlib import Path
import tempfile
import unittest

import numpy as np
import torch

from ranker_commander_combat_ppo import freeze_economy
from ranker_commander_full_ppo import FullPolicyCampaign, bootstrap, full_policy_scope, select_learning_rosters
from ranker_commander_improve import builtin_jobs, pin, read, save
from ranker_commander_model import CommanderPolicy, HEAD_SIZES, MAP_SHAPE, load_weights
from ranker_commander_rollout import RECORD_DTYPE, write_rollout
from ranker_commander_train import save_checkpoint


class FullPolicyTests(unittest.TestCase):
    def setUp(self):
        self.best = {r: dict(path=f'best{r}') for r in range(4)}
        self.current = {r: dict(path=f'current{r}') for r in range(4)}
        self.candidate = {r: dict(path=f'candidate{r}') for r in range(4)}
        self.baseline = {r: dict(games=48, wins=44, valid=True, coverage_complete=True) for r in range(4)}
        self.rates = {r: 1e-5 for r in range(4)}
        self.streaks = {r: 0 for r in range(4)}

    def select(self, wins, *, streaks=None, preflight=None):
        after = copy.deepcopy(self.baseline)
        for r in range(4):
            after[r]['wins'] = wins[r]
        return select_learning_rosters(self.best, self.current, self.candidate,
            self.baseline, after, preflight or {r: True for r in range(4)},
            self.rates, self.streaks if streaks is None else streaks)

    def test_temporary_declines_keep_learner_and_preserve_best(self):
        best, current, rates, streaks, _ = self.select([45, 44, 40, 34])
        self.assertEqual(best[0], self.candidate[0])
        self.assertEqual(best[1], self.best[1])
        self.assertEqual(best[3], self.best[3])
        self.assertEqual(current, self.candidate)
        self.assertEqual(rates, self.rates)
        self.assertEqual(streaks, {0: 0, 1: 0, 2: 0, 3: 1})

    def test_only_repeated_large_declines_reduce_learning_rate(self):
        result = self.select([37] * 4)
        result = self.select([37] * 4, streaks=result[3])
        self.assertEqual(result[2], self.rates)
        result = self.select([37] * 4, streaks=result[3])
        self.assertEqual(result[1], self.candidate)
        self.assertEqual(result[2], {r: 5e-6 for r in range(4)})
        self.assertEqual(result[3], self.streaks)
        self.assertTrue(all(d['reason'] == 'repeated_large_regressions' for d in result[4].values()))

    def test_recovery_resets_regression_streak(self):
        result = self.select([44, 43, 40, 39], streaks={r: 2 for r in range(4)})
        self.assertEqual(result[3], self.streaks)
        self.assertEqual(result[2], self.rates)

    def test_failed_update_gate_retains_current_not_best(self):
        result = self.select([48] * 4, preflight={r: False for r in range(4)})
        self.assertEqual(result[0], self.best)
        self.assertEqual(result[1], self.current)
        self.assertTrue(all(d['reason'] == 'update_width_rejected' for d in result[4].values()))

    def test_incomplete_evaluations_cannot_promote(self):
        self.baseline[1]['coverage_complete'] = False
        with self.assertRaises(ValueError):
            self.select([48] * 4)

    def test_scope_unfreezes_economy_skills_workers_and_shared_features(self):
        policy = CommanderPolicy()
        freeze_economy(policy)
        self.assertFalse(policy.heads[0].weight.requires_grad)
        self.assertFalse(policy.heads[6].weight.requires_grad)
        scope = full_policy_scope(policy)
        self.assertFalse(scope['frozen'])
        self.assertTrue(all(p.requires_grad for p in policy.parameters()))

    def test_completed_parent_import_keeps_latest_learner_and_best_history(self):
        torch.set_num_threads(2)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            parent, target = work / 'parent', work / 'full'
            parent.mkdir()
            (parent / 'controller.lock').write_bytes(b'0')
            initial, candidate = {}, {}
            for race in range(4):
                for label, version, pool in (('initial', 10 + race, initial), ('candidate', 20 + race, candidate)):
                    path = parent / f'{label}{race}.bin'
                    save_checkpoint(CommanderPolicy(), path, version=version, metadata={})
                    pool[race] = dict(**pin(path), version=version, race=race)
                save(parent / f'round_000/fit/race{race}/fit.json', dict(preflight_passed=True))
            layouts = [dict(seed=i + 1, start_pair=[a, b]) for i, (a, b) in enumerate(
                (a, b) for a in range(4) for b in range(4) if a != b)]
            contract = dict(initial=initial, pins=list(initial.values()), workers=4,
                layouts=layouts, evaluation_seed=10000)
            save(parent / 'contract.json', contract)
            save(parent / 'round_000/fit/roster.json', candidate)

            def reports(models, wins):
                counts = {r: 0 for r in range(4)}
                rows = []
                for i, job in enumerate(builtin_jobs(models, layouts, policy_seed_base=10000)):
                    race = job['own_tribe']
                    rows.append(dict(job, job=i, valid=True, evaluation_valid=True,
                        status=1 if counts[race] < wins[race] else 2,
                        teacher=False, deterministic=True, weights_sha256=models[race]['sha256'],
                        weight_version=models[race]['version']))
                    counts[race] += 1
                return rows

            save(parent / 'baseline/result.json', dict(reports=reports(initial, [44] * 4), audits=[]))
            evaluation = dict(reports=reports(candidate, [45, 43, 36, 44]), audits=[])
            save(parent / 'round_000/evaluation/result.json', evaluation)
            bootstrap(parent, target)
            campaign = FullPolicyCampaign(target)
            self.assertEqual(campaign.contract['scope'], 'all_policy_parameters')
            selection = read(target / 'initial_selection.json')
            self.assertEqual(selection['current'], {str(r): v for r, v in candidate.items()})
            self.assertEqual(selection['best']['0'], candidate[0])
            for race in (1, 2, 3):
                self.assertEqual(selection['best'][str(race)], initial[race])
            self.assertEqual(selection['regression_streaks'], {str(r): 0 for r in range(4)})
            bootstrap(parent, target)
            evaluation['reports'].pop()
            save(parent / 'round_000/evaluation/result.json', evaluation)
            with self.assertRaises(ValueError):
                bootstrap(parent, work / 'incomplete')

    def test_actual_full_fit_updates_all_heads_and_resumes_optimizer(self):
        torch.set_num_threads(2)
        torch.manual_seed(211)
        rng = np.random.default_rng(211)
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
                    records = np.zeros(17, dtype=RECORD_DTYPE)
                    records['frame'] = np.arange(1, 18) * 32
                    records['status'][-1] = 1 if race % 2 else 2
                    records['vector'] = rng.normal(0, .1, records['vector'].shape)
                    records['vector'][:, 606:610] = np.eye(4)[race]
                    records['vector'][:, 66:70] = np.eye(4)[0]
                    records['map'] = rng.integers(0, 256, records['map'].shape, dtype=np.uint8)
                    records['mask'][:-1] = 1
                    records['action'][:-1] = [[(i * 7 + race) % width for width in HEAD_SIZES] for i in range(16)]
                    records['action'][:4, 0] = [64, 72, 80, 95]
                    with torch.no_grad():
                        out = policy.evaluate(torch.from_numpy(records['vector'][:-1].copy()),
                            torch.from_numpy(records['map'][:-1].copy()).float().reshape(-1, *MAP_SHAPE) / 255.,
                            torch.from_numpy(records['action'][:-1].astype(np.int64)),
                            torch.from_numpy(records['mask'][:-1].copy()).bool(), torch.zeros(16, 32))
                    records['logp'][:-1] = out['logp'].numpy()
                    records['value'][:-1] = out['value'].numpy()
                    path = work / f'{label}_{race}.rlo'
                    write_rollout(path, records, owner=1, seed=90 + race, weight_version=source['version'])
                    reports.append(dict(job=race, valid=True, evaluation_valid=True,
                        status=int(records['status'][-1]), own_tribe=race, teacher=False,
                        deterministic=False, dagger=False, seed=90 + race, rollout=str(path),
                        weights_sha256=source['sha256'], weight_version=source['version']))
                    audits.append(dict(rollout=pin(path)))
                return dict(reports=reports, audits=audits)

            campaign = object.__new__(FullPolicyCampaign)
            campaign.directory = work
            campaign.contract = dict(training_seed=211, version_base=2000,
                initial={str(r): v for r, v in initial.items()})
            campaign.check = lambda: None
            campaign.status = lambda *args, **kwargs: None
            campaign.event = lambda **kwargs: None
            first_data = data(initial, 'first')
            trained, gates = campaign.fit_full('first_fit', first_data, initial, self.rates, 0)
            self.assertTrue(all(gates.values()))
            self.assertEqual(campaign.fit_full('first_fit', first_data, initial, self.rates, 0), (trained, gates))
            for race in range(4):
                old, new = load_weights(initial[race]['path']), load_weights(trained[race]['path'])
                for head in range(8):
                    self.assertFalse(torch.equal(old.heads[head].weight, new.heads[head].weight), head)
                self.assertFalse(torch.equal(old.heads[0].weight[64:], new.heads[0].weight[64:]))
                self.assertFalse(torch.equal(old.vector1.weight, new.vector1.weight))
                self.assertFalse(torch.equal(old.conv1.weight, new.conv1.weight))
                self.assertFalse(torch.equal(old.embeddings[0].weight, new.embeddings[0].weight))
            with self.assertRaises(ValueError):
                campaign.fit_full('first_fit', first_data, initial, {r: 2e-5 for r in range(4)}, 0)
            again, gates = campaign.fit_full('second_fit', data(trained, 'fresh'), trained, self.rates, 1)
            self.assertTrue(all(gates.values()))
            for race in range(4):
                with np.load(trained[race]['optimizer']['path'], allow_pickle=False) as old:
                    with np.load(again[race]['optimizer']['path'], allow_pickle=False) as new:
                        self.assertGreater(float(new['p0_step']), float(old['p0_step']))


if __name__ == '__main__':
    unittest.main()
