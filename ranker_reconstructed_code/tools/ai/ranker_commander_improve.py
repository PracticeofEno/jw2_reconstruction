"""Repeat native four-race PPO with immutable inputs and separate win gates.

The learner may advance through small evaluation fluctuations; the best roster
advances only on full paired builtin evaluations. Three rounds without a best
score improvement return control for log/executor analysis. No deployment or
global champion record is modified. State is resumable only with this process's
OS lock acquired; unfinished native job directories require process inspection.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
from queue import Empty, Queue
import shutil
import threading
import traceback

import numpy as np
import torch

from ranker_commander_combat_ppo import freeze_economy
from ranker_commander_eval import _run_game
from ranker_commander_model import MAP_SHAPE, load_weights
from ranker_commander_rollout import episode_returns, read_rollout
from ranker_commander_selfplay_batch import current_episodes, randomized_jobs
from ranker_commander_train import TrainConfig, build_batch, save_checkpoint, train_update

RACES = range(4)
PAIRS = {(a, b) for a in RACES for b in RACES if a != b}


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    temporary.replace(path)


def pin(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return dict(path=str(path), sha256=digest.hexdigest())


def verify(item):
    if pin(item['path'])['sha256'] != item['sha256']:
        raise ValueError(f"immutable input changed: {item['path']}")


def roster(value):
    result = {int(k): v for k, v in value.items()}
    if set(result) != set(RACES):
        raise ValueError('all four race checkpoints are required')
    for race, source in result.items():
        verify(source)
        if source.get('race') != race or load_weights(source['path']).weight_version != source['version']:
            raise ValueError('checkpoint race/version mismatch')
    return result


def builtin_jobs(current, layouts, *, policy_seed_base):
    if len(layouts) != 12 or {tuple(x['start_pair']) for x in layouts} != PAIRS:
        raise ValueError('evaluation requires all twelve measured ordered start pairs')
    return [dict(seed=layout['seed'], start_pair=list(layout['start_pair']),
        own_tribe=race, tribe=enemy, opponent_policy_tribe=enemy,
        primary_weights=current[race]['path'], policy_seed=policy_seed_base + race * 100 + enemy * 12 + i,
        curriculum=2, max_frames=60000, opp_slow=0, coordinated_transfers=False,
        teacher_variant=0, dagger=False, opponent_key='builtin')
        for i, layout in enumerate(layouts) for enemy in RACES for race in RACES]


def summarize(reports):
    summaries = {}
    for race in RACES:
        rows = [r for r in reports if r['own_tribe'] == race]
        coverage = Counter((r['tribe'], tuple(r.get('start_pair', []))) for r in rows)
        complete = (len(rows) == 48 and set(coverage) == {(t, p) for t in RACES for p in PAIRS}
                    and set(coverage.values()) == {1})
        valid = all(r.get('valid') and r.get('evaluation_valid') and not r.get('teacher')
                    and not r.get('opponent_weights') and r.get('opp_slow', 0) == 0 for r in rows)
        wins = sum(r.get('status') == 1 for r in rows)
        summaries[race] = dict(games=len(rows), wins=wins,
            losses=sum(r.get('status') == 2 for r in rows), truncated=sum(r.get('status') == 3 for r in rows),
            coverage_complete=complete, valid=valid,
            passed_100_percent=complete and valid and wins == 48,
            per_opponent={t: dict(games=sum(r['tribe'] == t for r in rows),
                wins=sum(r['tribe'] == t and r.get('status') == 1 for r in rows)) for t in RACES})
    return summaries


def validate_reports(reports, jobs, current, *, deterministic):
    if len(reports) != len(jobs) or {r['job'] for r in reports} != set(range(len(jobs))):
        raise ValueError('missing or duplicated native game receipts')
    for row in reports:
        job = jobs[row['job']]
        source = current[job['own_tribe']]
        if (any(row.get(k) != v for k, v in job.items())
                or row.get('weights_sha256') != source['sha256']
                or row.get('weight_version') != source['version']
                or row.get('teacher') or row.get('deterministic') != deterministic
                or not row.get('valid') or not row.get('evaluation_valid')):
            raise ValueError('receipt does not match its exact planned policy/game/mode')


def choose_rosters(best, current, candidate, baseline, evaluation, preflight):
    """Exact 48-case gates; ties retain best but permit further PPO exploration."""
    next_best, next_current, decisions = {}, {}, {}
    for r in RACES:
        before, after = baseline[r], evaluation[r]
        if not (before['coverage_complete'] and before['valid'] and after['coverage_complete'] and after['valid']):
            raise ValueError('selection requires complete valid paired evaluation')
        accepted = bool(preflight[r])
        improved = accepted and after['wins'] > before['wins']
        next_best[r] = candidate[r] if improved else best[r]
        # Four additional nonwins out of 48 is a material regression; retain
        # smaller fluctuations in the learning trajectory, never as a champion.
        advance = accepted and after['wins'] >= before['wins'] - 3
        next_current[r] = candidate[r] if advance else best[r]
        decisions[r] = dict(improved=improved, learner_advanced=advance,
                            before_wins=before['wins'], after_wins=after['wins'])
    return next_best, next_current, decisions


def audit_episode(path, source, *, race, enemy, owner, seed):
    verify(source)
    policy = load_weights(source['path'])
    episode = read_rollout(path, current_version=source['version'], teacher=False)
    try:
        if episode.owner != owner or episode.seed != seed:
            raise ValueError('rollout owner/seed mismatch')
        x = episode.decisions
        if not ((x['vector'][:, 606:610] == np.eye(4)[race]).all()
                and (x['vector'][:, 66:70] == np.eye(4)[enemy]).all()):
            raise ValueError('observation race does not match the assigned policy')
        log_error = value_error = 0.
        with torch.no_grad():
            for start in range(0, len(x), 256):
                chunk = x[start:start + 256]
                out = policy.evaluate(torch.from_numpy(chunk['vector'].copy()),
                    torch.from_numpy(chunk['map'].copy()).float().reshape(-1, *MAP_SHAPE) / 255.,
                    torch.from_numpy(chunk['action'].astype(np.int64)),
                    torch.from_numpy(chunk['mask'].copy()).bool(),
                    torch.from_numpy(chunk['privileged'].copy()))
                log_error = max(log_error, float((out['logp'].sum(1) - torch.from_numpy(chunk['logp'].copy()).sum(1)).abs().max()))
                value_error = max(value_error, float((out['value'] - torch.from_numpy(chunk['value'].copy())).abs().max()))
        if log_error > .005 or value_error > .005:
            raise ValueError(f'native/Python policy mismatch: {log_error}, {value_error}')
        metrics = read(Path(path).parent / f'commander_metrics_{owner}.json')
        if (metrics['owner'] != owner or metrics['mask_violations'] != 0
                or metrics['end_frame'] != int(episode.terminal['frame'])
                or metrics['status'] != int(episode.terminal['status'])):
            raise ValueError('native execution metrics disagree with rollout')
        actions = x['action']
        return dict(rollout=pin(path), owner=owner, race=race, enemy=enemy, decisions=len(x),
            logp_error=log_error, value_error=value_error, mask_violations=0,
            silent_rejections=metrics['silent_rejections'],
            hunt_commands=int(((actions[:, 2] > 0) & (actions[:, 3] == 5)).sum()),
            macro_counts=np.bincount(actions[:, 0].astype(int)).tolist(),
            intent_counts=np.bincount(actions[actions[:, 2] > 0, 3].astype(int), minlength=9).tolist())
    finally:
        episode.close()


def measure(policy, batch):
    sums = dict(value_loss=0., approximate_kl=0., clip_fraction=0.)
    with torch.no_grad():
        for start in range(0, len(batch['target']), 512):
            x = {k: v[start:start + 512] for k, v in batch.items()}
            out = policy.evaluate(x['vector'], x['maps'], x['actions'], x['masks'], x['privileged'])
            z = out['logp'].sum(1) - x['old_logp']
            ratio = z.exp()
            if not torch.isfinite(ratio).all():
                raise ValueError('nonfinite PPO ratio')
            sums['value_loss'] += float((.5 * (out['value'] - x['target']).square()).sum())
            sums['approximate_kl'] += float((ratio - 1 - z).sum())
            sums['clip_fraction'] += float(((ratio - 1).abs() > .2).sum())
    return {k: v / len(batch['target']) for k, v in sums.items()}


class Campaign:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.contract = read(self.directory / 'contract.json')
        self.event_lock = threading.Lock()
        self.state = read(self.directory / 'state.json') if (self.directory / 'state.json').exists() else {}
        self.check()

    def check(self):
        for item in self.contract['pins']:
            verify(item)
        if shutil.disk_usage(self.directory).free < 8 * 1024**3:
            raise RuntimeError('less than 8 GiB free; archive completed data before continuing')

    def event(self, **value):
        row = dict(time=datetime.now().astimezone().isoformat(timespec='seconds'), **value)
        with self.event_lock:
            with (self.directory / 'events.jsonl').open('a', encoding='utf-8') as stream:
                stream.write(json.dumps(row) + '\n')
            print(json.dumps(row), flush=True)

    def status(self, phase, **value):
        self.state.update(phase=phase, pid=os.getpid(), updated=datetime.now().astimezone().isoformat(), **value)
        save(self.directory / 'state.json', self.state)
        self.event(phase=phase, **value)

    def games(self, stage, jobs, current, historical, *, deterministic):
        self.check()
        directory = self.directory / stage
        plan = dict(jobs=jobs, current=current, historical=historical, deterministic=deterministic)
        plan_path = directory / 'plan.json'
        if plan_path.exists():
            if read(plan_path) != json.loads(json.dumps(plan)):
                raise ValueError(f'cannot change an existing stage plan: {stage}')
        else:
            save(plan_path, plan)
        self.status(stage, jobs=len(jobs))
        queue = Queue()
        for i in range(len(jobs)):
            queue.put(i)
        def shard(slot):
            rows = []
            while True:
                try:
                    i = queue.get_nowait()
                except Empty:
                    break
                job = jobs[i]
                receipt = directory / 'games' / f'game_{i:05d}' / 'output/job.json'
                if receipt.exists():
                    row = read(receipt)
                else:
                    # prepare_job_directory refuses an existing incomplete
                    # directory. Never infer process death from missing output.
                    row = _run_game(self.contract['install'], current[job['own_tribe']]['path'],
                        directory / 'games', i, job, 4800 + slot, teacher=False,
                        deterministic=deterministic, timeout=1800, executable=self.contract['runtime']['path'])
                if not row.get('valid') or not row.get('evaluation_valid'):
                    raise RuntimeError(f"invalid native game {stage}/{i}: {row.get('reason')}")
                if any(row.get(k) != v for k, v in job.items()):
                    raise ValueError('native receipt differs from the planned game')
                if (row.get('teacher') or bool(row.get('deterministic')) != deterministic
                        or row['weights_sha256'] != current[job['own_tribe']]['sha256']):
                    raise ValueError('receipt actor/mode mismatch')
                audits = [audit_episode(row['rollout'], current[job['own_tribe']], race=job['own_tribe'],
                    enemy=job['tribe'], owner=1, seed=job['seed'])]
                if job.get('opponent_weights'):
                    pool = current if job['opponent_key'].startswith('current_') else historical
                    if row['weights_sha2562'] != pool[job['tribe']]['sha256']:
                        raise ValueError('opponent checkpoint mismatch')
                    audits.append(audit_episode(row['rollout2'], pool[job['tribe']], race=job['tribe'],
                        enemy=job['own_tribe'], owner=2, seed=job['seed']))
                save(directory / 'audits' / f'{i:05d}.json', audits)
                rows.append((i, row, audits))
                self.event(stage=stage, job=i, race=job['own_tribe'], opponent=job['opponent_key'],
                    enemy=job['tribe'], status=row['status'], frame=row['end_frame'],
                    decisions=audits[0]['decisions'], hunts=audits[0]['hunt_commands'])
            return rows
        rows = []
        with ThreadPoolExecutor(max_workers=self.contract['workers']) as pool:
            for future in as_completed([pool.submit(shard, slot) for slot in range(self.contract['workers'])]):
                rows.extend(future.result())
        rows.sort()
        result = dict(reports=[r for _, r, _ in rows], audits=[a for _, _, items in rows for a in items])
        validate_reports(result['reports'], jobs, current, deterministic=deterministic)
        save(directory / 'result.json', result)
        self.check()
        return result

    def evaluate(self, stage, current, *, deterministic=True, seed_offset=0):
        result_path = self.directory / stage / 'result.json'
        jobs = builtin_jobs(current, self.contract['layouts'],
            policy_seed_base=self.contract['evaluation_seed'] + seed_offset)
        if result_path.exists():
            result = read(result_path)
        else:
            result = self.games(stage, jobs, current, current, deterministic=deterministic)
        validate_reports(result['reports'], jobs, current, deterministic=deterministic)
        result['summary'] = summarize(result['reports'])
        if not all(r['coverage_complete'] and r['valid'] for r in result['summary'].values()):
            raise ValueError('incomplete builtin evaluation')
        save(self.directory / stage / 'summary.json', result['summary'])
        self.event(stage=stage, summary=result['summary'])
        return result

    def collect(self, stage, current, historical, index):
        path = self.directory / stage / 'result.json'
        jobs = []
        for block in range(self.contract['collection_blocks']):
            jobs.extend(randomized_jobs(current, historical, self.contract['layouts'],
                round_index=index * self.contract['collection_blocks'] + block,
                seed=self.contract['training_seed']))
        result = read(path) if path.exists() else self.games(stage, jobs, current, historical, deterministic=False)
        validate_reports(result['reports'], jobs, current, deterministic=False)
        return result

    def fit(self, stage, collection, current, *, warm=False, round_index=0):
        self.check()
        self.status(stage)
        selected = current_episodes(collection['reports'], current)
        audits = {a['rollout']['path']: a for a in collection['audits']}
        trained, gates = {}, {}
        for race in RACES:
            output = self.directory / stage / f'race{race}/policy.bin'
            audit_path = output.parent / 'fit.json'
            if audit_path.exists():
                report = read(audit_path)
                verify(report['checkpoint'])
                trained[race], gates[race] = report['checkpoint'], report['preflight_passed']
                continue
            if output.exists():
                raise RuntimeError(f'partial checkpoint needs inspection: {output}')
            episodes = []
            try:
                for item in selected[race]:
                    verify(audits[item['path']]['rollout'])
                    episodes.append(read_rollout(item['path'], current_version=current[race]['version'], teacher=False))
                torch.manual_seed(self.contract['training_seed'] + race + round_index * 4)
                policy = load_weights(current[race]['path'])
                reference = load_weights(self.contract['initial'][str(race)]['path'])
                scope = freeze_economy(policy)
                before_parameters = {n: p.detach().clone() for n, p in policy.named_parameters()}
                lr = 1e-4 if warm else self.contract['learning_rate'] * .7 ** round_index
                config = TrainConfig(mode='ppo', iteration=0 if warm else round_index + 1,
                    epochs=6 if warm else 3, minibatch=1024, critic_warmup=1 if warm else 0,
                    learning_rate_initial=lr, learning_rate_final=lr, gamma=1., gae_lambda=.98,
                    teacher_kl_initial=0 if warm else .05, teacher_kl_floor=0 if warm else .05,
                    seed=self.contract['training_seed'] + race)
                batch, reward = build_batch(episodes, config)
                if warm:
                    batch['target'] = torch.from_numpy(np.concatenate([episode_returns(ep,
                        shaping_scale=config.shaping_scale, gamma=1., gae_lambda=.98)['mc_return'] for ep in episodes]))
                before = measure(policy, batch)
                if abs(before['approximate_kl']) > 1e-6 or before['clip_fraction'] != 0:
                    raise ValueError('PPO batch is not from the exact current actor')
                optimizer, metrics = train_update(policy, batch, config,
                    teacher_policy=None if warm else reference,
                    progress_callback=lambda p: self.event(stage=stage, race=race, training=p))
                after = measure(policy, batch)
                changed = [n for n, p in policy.named_parameters() if not torch.equal(p, before_parameters[n])]
                if not changed or not set(changed) <= set(scope['trainable']):
                    raise ValueError('unexpected PPO update scope')
                if warm and (not all(n.startswith(('value1.', 'value2.')) for n in changed)
                             or after['value_loss'] >= before['value_loss']):
                    raise ValueError('critic warmup failed or changed the actor')
                if not warm and not any(n.startswith(('heads.', 'head_adapters.')) for n in changed):
                    raise ValueError('PPO failed to update the actor')
                passed = after['approximate_kl'] <= .02 and after['clip_fraction'] <= .15
                version = 1000 + (round_index + 1) * 10 + race if not warm else 1000 + race
                metadata = dict(mode='ppo', own_tribe=race, warmup=warm, config=asdict(config),
                    source=current[race], before=before, after=after, changed=changed,
                    protected_parameters_exact=True, preflight_passed=passed,
                    episodes=[audits[item['path']]['rollout'] for item in selected[race]],
                    reward=reward, metrics=metrics)
                save_checkpoint(policy, output, version=version, metadata=metadata, optimizer=optimizer)
                exported = load_weights(output)
                if any(not torch.equal(p, exported.state_dict()[n]) for n, p in policy.named_parameters()):
                    raise ValueError('checkpoint export changed weights')
                checkpoint = dict(**pin(output), version=version, race=race)
                report = dict(checkpoint=checkpoint, preflight_passed=passed, before=before, after=after,
                    decisions=len(batch['target']), actor_exact=warm, protected_parameters_exact=True)
                save(audit_path, report)
                trained[race], gates[race] = checkpoint, passed
                self.event(stage=stage, race=race, fit=report)
            finally:
                for episode in episodes:
                    episode.close()
        save(self.directory / stage / 'roster.json', trained)
        return trained, gates

    def run(self):
        torch.set_num_threads(2)
        initial = roster(self.contract['initial'])
        best = initial
        baseline = self.evaluate('baseline', initial)
        # Runtime and supervised-policy changes require a fresh value calibration.
        warm_data = self.collect('warm_collect', initial, initial, 0)
        current, gates = self.fit('warm_fit', warm_data, initial, warm=True)
        if not all(gates.values()):
            raise RuntimeError('warmup failed its policy preservation gate')
        no_improvement = 0
        for index in range(self.contract['rounds']):
            stage = f'round_{index:03d}'
            decision_path = self.directory / stage / 'selection.json'
            if decision_path.exists():
                decision = read(decision_path)
                best, current = roster(decision['best']), roster(decision['current'])
                baseline = decision['baseline']
                baseline['summary'] = {int(k): v for k, v in baseline['summary'].items()}
                no_improvement = decision['no_improvement_rounds']
                continue
            data = self.collect(stage + '/collect', current, best, index + 1)
            candidate, gates = self.fit(stage + '/fit', data, current, round_index=index)
            # A failed trust-region gate is rolled back before spending native
            # evaluation games on it; every evaluated roster remains deployable.
            screened = {r: candidate[r] if gates[r] else current[r] for r in RACES}
            evaluation = self.evaluate(stage + '/evaluation', screened)
            next_best, current, decisions = choose_rosters(best, current, screened,
                baseline['summary'], evaluation['summary'], gates)
            improved = [r for r in RACES if decisions[r]['improved']]
            no_improvement = 0 if improved else no_improvement + 1
            for race in improved:
                baseline['summary'][race] = evaluation['summary'][race]
                baseline['reports'] = [r for r in baseline['reports'] if r['own_tribe'] != race] + [
                    r for r in evaluation['reports'] if r['own_tribe'] == race]
            best = next_best
            save(decision_path, dict(best=best, current=current, baseline=baseline,
                decisions=decisions, no_improvement_rounds=no_improvement))
            self.status('round_complete', round=index, selection=decisions,
                        best=best, current=current, no_improvement_rounds=no_improvement)
            if all(s['passed_100_percent'] for s in baseline['summary'].values()):
                confirmation = self.evaluate(stage + '/stochastic_confirmation', best,
                    deterministic=False, seed_offset=100000 + index * 1000)
                if all(s['passed_100_percent'] for s in confirmation['summary'].values()):
                    self.status('independent_validation_required', best=best,
                        reason='Fixed development and stochastic gates passed; verify unseen game seeds before goal completion')
                    return
            if no_improvement >= 3:
                self.status('analysis_required', best=best,
                    reason='Three full evaluation rounds without an improved best race score; inspect failure trajectories before further tuning')
                return
        self.status('batch_complete_goal_active', best=best, current=current,
                    reason='Continue training or analyze results; this is not goal completion')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args(argv)
    # A kernel-owned byte lock is released on process death. A stale state PID
    # alone cannot authorize another concurrent controller.
    import msvcrt
    args.directory.mkdir(parents=True, exist_ok=True)
    with (args.directory / 'controller.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        campaign = Campaign(args.directory)
        try:
            campaign.run()
        except BaseException:
            campaign.status('failed_needs_inspection', traceback=traceback.format_exc())
            raise
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


if __name__ == '__main__':
    main()
