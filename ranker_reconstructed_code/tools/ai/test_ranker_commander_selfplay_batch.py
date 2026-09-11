import copy
import unittest
from collections import Counter
from ranker_commander_selfplay_batch import balanced_jobs,current_episodes,randomized_jobs


class SelfplayBatchTests(unittest.TestCase):
    def setUp(self):
        self.current={r:dict(path=f'current{r}.bin',sha256=f'hash{r}',version=200+r) for r in range(4)}
        self.historical={r:dict(path=f'old{r}.bin',sha256=f'oldhash{r}',version=100+r) for r in range(4)}
        self.layouts=[dict(seed=i+1,start_pair=[a,b]) for i,(a,b) in enumerate((a,b) for a in range(4) for b in range(4) if a!=b)]

    def reports(self):
        rows=balanced_jobs(self.current,self.historical,self.layouts,round_index=0,policy_seed_base=9000)
        for i,row in enumerate(rows):
            race=row['own_tribe'];other=row['opponent_policy_tribe']
            row.update(job=i,valid=True,teacher=False,deterministic=False,status=1,
                rollout=f'game{i}/one.rlo',weights_sha256=self.current[race]['sha256'],weight_version=self.current[race]['version'])
            if 'opponent_weights' in row:
                source=self.current if row['opponent_key'].startswith('current') else self.historical
                row.update(rollout2=f'game{i}/two.rlo',status2=2,weights_sha2562=source[other]['sha256'],weight_version2=source[other]['version'])
        return rows

    def test_round_balances_races_and_preserves_measured_layouts(self):
        rows=self.reports()
        self.assertEqual(Counter(x['own_tribe'] for x in rows),{r:10 for r in range(4)})
        self.assertEqual(sum('opponent_weights' in x for x in rows),32)
        self.assertEqual(len({x['policy_seed'] for x in rows}),40)
        for row in rows:
            self.assertIn((row['seed'],row['start_pair']),[(x['seed'],x['start_pair']) for x in self.layouts])
            if 'opponent_weights' in row:self.assertEqual(row['tribe'],row['opponent_policy_tribe'])

    def test_only_current_exact_actor_owners_enter_each_race(self):
        selected=current_episodes(self.reports(),self.current)
        for race,episodes in selected.items():
            self.assertEqual(len(episodes),14)
            self.assertEqual(Counter(x['owner'] for x in episodes),{1:10,2:4})
            self.assertTrue(all(x['weights_sha256']==self.current[race]['sha256'] for x in episodes))

    def test_random_races_keep_policy_identity_and_full_matchup_coverage(self):
        for seed in (7, 79, 931):
            rows = randomized_jobs(self.current, self.historical, self.layouts,
                                   round_index=2, seed=seed)
            self.assertEqual(Counter(x['own_tribe'] for x in rows), {r: 10 for r in range(4)})
            self.assertEqual(len({x['policy_seed'] for x in rows}), 40)
            for row in rows:
                self.assertEqual(row['primary_weights'], self.current[row['own_tribe']]['path'])
                self.assertEqual(row['opponent_policy_tribe'], row['tribe'])
                if row['opponent_key'] != 'builtin':
                    pool = self.current if row['opponent_key'].startswith('current_') else self.historical
                    self.assertEqual(row['opponent_weights'], pool[row['tribe']]['path'])
            for kind in ('current_', 'historical_'):
                matchups = [(x['own_tribe'], x['tribe']) for x in rows if x['opponent_key'].startswith(kind)]
                self.assertEqual(set(matchups), {(a,b) for a in range(4) for b in range(4)})
                self.assertEqual(len(matchups), 16)

    def test_random_schedule_is_reproducible_and_changes_each_round(self):
        def jobs(index):
            return randomized_jobs(self.current, self.historical, self.layouts, round_index=index, seed=4000)
        self.assertEqual(jobs(0), jobs(0))
        self.assertNotEqual(jobs(0), jobs(1))
        self.assertFalse({x['policy_seed'] for x in jobs(0)} & {x['policy_seed'] for x in jobs(1)})

    def test_rejects_wrong_primary_hash_version_or_race(self):
        for key,value in [('weights_sha256','old'),('weight_version',199),('own_tribe',None),('own_tribe',False)]:
            rows=self.reports();rows[0][key]=value
            with self.assertRaises(ValueError):current_episodes(rows,self.current)

    def test_rejects_missing_or_inconsistent_second_owner(self):
        for key,value in [('rollout2',None),('status2',1),('opponent_policy_tribe',None),('weight_version2',199)]:
            rows=self.reports();rows[0][key]=value
            with self.assertRaises(ValueError):current_episodes(rows,self.current)

    def test_rejects_eval_teacher_invalid_and_duplicate_trajectories(self):
        for key,value in [('deterministic',True),('teacher',True),('dagger',True),('valid',False),('status',0)]:
            rows=self.reports();rows[0][key]=value
            with self.assertRaises(ValueError):current_episodes(rows,self.current)
        rows=self.reports();rows.append(copy.deepcopy(rows[0]))
        with self.assertRaises(ValueError):current_episodes(rows,self.current)


if __name__=='__main__':unittest.main()
