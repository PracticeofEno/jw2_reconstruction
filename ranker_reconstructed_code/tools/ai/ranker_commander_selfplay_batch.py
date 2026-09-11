"""Explicit race jobs and exact on-policy ownership for multi-race PPO.

Current policies are immutable throughout a collection round. An opponent's
rollout is trainable only by the matching race AND exact checkpoint/version.
Historical opponents provide competition, never mislabeled PPO experience.
"""
from pathlib import Path
import random


def balanced_jobs(current, historical, layouts, *, round_index, policy_seed_base):
    if set(current)!=set(range(4)) or set(historical)!=set(range(4)):
        raise ValueError('four explicit race policies are required')
    if len(layouts)!=12 or len({tuple(x['start_pair']) for x in layouts})!=12:
        raise ValueError('twelve measured ordered start pairs are required')
    jobs=[]
    for slot in range(10):
        for race in range(4):
            index=len(jobs)
            layout=layouts[(slot*4+race+round_index*5)%12]
            opponent=(race+slot%4)%4
            job=dict(seed=layout['seed'],start_pair=list(layout['start_pair']),own_tribe=race,
                tribe=opponent,opponent_policy_tribe=opponent,primary_weights=str(current[race]['path']),
                policy_seed=policy_seed_base+round_index*1000+index,curriculum=2,max_frames=60000,
                coordinated_transfers=False,teacher_variant=0,dagger=False,opp_slow=0)
            if slot<8:
                opponent_pool=current if slot<4 else historical
                job.update(opponent_weights=str(opponent_pool[opponent]['path']),
                    opponent_key=('current_' if slot<4 else 'historical_')+str(opponent))
            else:
                job.update(tribe=(race+2*(slot-8)+round_index)%4,opponent_key='builtin')
            jobs.append(job)
    return jobs


def randomized_jobs(current, historical, layouts, *, round_index, seed):
    """Seeded race/layout permutations retain exact 80/20 opponent quotas.

    Randomize the race assignment as well as execution order. Both current
    and historical blocks still contain every ordered race matchup once.
    """
    rng = random.Random(seed + round_index)
    races = list(range(4))
    rng.shuffle(races)
    shuffled_layouts = list(layouts)
    rng.shuffle(shuffled_layouts)
    jobs = balanced_jobs({i: current[r] for i, r in enumerate(races)},
        {i: historical[r] for i, r in enumerate(races)}, shuffled_layouts,
        round_index=round_index, policy_seed_base=seed)
    for job in jobs:
        job['own_tribe'] = races[job['own_tribe']]
        job['tribe'] = races[job['tribe']]
        job['opponent_policy_tribe'] = job['tribe']
        if job['opponent_key'] != 'builtin':
            kind = job['opponent_key'].split('_')[0]
            job['opponent_key'] = f"{kind}_{job['tribe']}"
    rng.shuffle(jobs)
    return jobs


def current_episodes(reports, current):
    result={race:[] for race in current}
    paths=set()
    for row in reports:
        if not row.get('valid') or row.get('teacher') or row.get('dagger') or row.get('deterministic'):
            raise ValueError('training requires valid stochastic neural episodes without teacher labels')
        if row.get('status') not in (1,2,3):
            raise ValueError('training requires a completed terminal result')
        race=row.get('own_tribe')
        if type(race) is not int or race not in current:
            raise ValueError('primary race must be explicit')
        if row.get('weights_sha256')!=current[race]['sha256'] or row.get('weight_version')!=current[race]['version']:
            raise ValueError('primary actor does not match its current race checkpoint')
        candidates=[(race,1,row['rollout'],row['weights_sha256'],row['weight_version'])]
        if row.get('opponent_weights'):
            other=row.get('opponent_policy_tribe')
            if type(other) is not int or other not in current or not row.get('rollout2'):
                raise ValueError('neural opponent needs an explicit race and complete owner 2 rollout')
            if row.get('status2')!={1:2,2:1,3:3}[row['status']]:
                raise ValueError('the two owners must have complementary terminal outcomes')
            if row.get('weights_sha2562')==current[other]['sha256']:
                if row.get('weight_version2')!=current[other]['version']:
                    raise ValueError('opponent version disagrees with the exact checkpoint')
                candidates.append((other,2,row['rollout2'],row['weights_sha2562'],row['weight_version2']))
        for race,owner,path,weights,version in candidates:
            identity=str(Path(path).resolve())
            if identity in paths:raise ValueError('duplicate training trajectory')
            paths.add(identity)
            result[race].append(dict(path=identity,owner=owner,race=race,weights_sha256=weights,version=version,
                game=row['job'],seed=row['seed']))
    return result
