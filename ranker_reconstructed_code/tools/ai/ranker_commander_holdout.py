"""Native start-layout discovery and independent four-race win validation.

Reserve seeds outside the campaign's training/development layouts. Discovery
stops at 64 frames and only measures spawn positions: its RLOs never count as
games won or PPO data. Full evaluation pins one candidate roster and covers
every ordered start pair/opponent race in both argmax and stochastic modes.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from ranker_commander_eval import _run_game
from ranker_commander_improve import (Campaign, PAIRS, RACES, builtin_jobs, pin, read,
                                      roster, save, summarize, validate_reports, verify)


def reserved_jobs(source, *, excluded_seeds, seed_base, count):
    if count < 12 or seed_base < 1 or seed_base + count > 0x100000000:
        raise ValueError('at least twelve nonzero u32 discovery seeds are required')
    seeds = list(range(seed_base, seed_base + count))
    if set(seeds) & set(excluded_seeds):
        raise ValueError('holdout seed range overlaps training/development seeds')
    return [dict(seed=seed, own_tribe=0, tribe=0, opponent_policy_tribe=0,
        primary_weights=source['path'], max_frames=64, curriculum=2, opp_slow=0,
        coordinated_transfers=False, teacher_variant=0, dagger=False,
        policy_seed=2026091170000 + i, purpose='layout_discovery_only') for i, seed in enumerate(seeds)]


def measured_layouts(reports, *, excluded_seeds):
    selected = {}
    for row in reports:
        if not row.get('valid') or row.get('purpose') != 'layout_discovery_only':
            raise ValueError('layout discovery requires valid native receipts')
        if row['seed'] in excluded_seeds or row['max_frames'] != 64 or row.get('end_frame') != 64:
            raise ValueError('invalid held-out discovery seed/frame limit')
        pair = tuple(row['start_pair'])
        if pair not in PAIRS:
            raise ValueError('invalid native start pair')
        selected.setdefault(pair, dict(seed=row['seed'], start_pair=list(pair)))
    return [selected[pair] for pair in sorted(selected)]


def gate(argmax, sampling, sources, layouts, *, excluded_seeds, policy_seed):
    if {x['seed'] for x in layouts} & set(excluded_seeds):
        raise ValueError('validation cannot reuse training/development game seeds')
    for reports, deterministic, offset in ((argmax, True, 0), (sampling, False, 10000)):
        jobs = builtin_jobs(sources, layouts, policy_seed_base=policy_seed + offset)
        validate_reports(reports, jobs, sources, deterministic=deterministic)
        for row in reports:
            metrics = row.get('commander_metrics', {})
            if (metrics.get('mask_violations') != 0 or not row.get('policy_seed_verified')
                    or row.get('reason') != 'elimination' and row.get('status') == 1):
                raise ValueError('independent wins require verified native elimination and masks')
    summaries = dict(argmax=summarize(argmax), sampling=summarize(sampling))
    return dict(passed=all(s['passed_100_percent'] for mode in summaries.values() for s in mode.values()),
        summaries=summaries, total_games=len(argmax) + len(sampling),
        distinct_game_seeds=len({r['seed'] for r in argmax}),
        scope='Four races, four normal builtins, twelve ordered starts on the campaign map, argmax and sampling',
        limitation='An empirical finite validation result, not a guarantee for every random game or other maps')


def discover(campaign, directory, *, seed_base=1819000000, count=128):
    directory.mkdir(parents=True, exist_ok=True)
    parent = read(campaign / 'contract.json')
    excluded = sorted({layout['seed'] for layout in parent['layouts']})
    initial = roster(parent['initial'])
    jobs = reserved_jobs(initial[0], excluded_seeds=excluded, seed_base=seed_base, count=count)
    contract_path = directory / 'discovery_contract.json'
    contract = dict(parent=pin(campaign / 'contract.json'), source=initial[0], runtime=parent['runtime'],
        helper=pin(__file__), excluded_seeds=excluded, jobs=jobs,
        selected_method='First observed seed per ordered pair; selection ignores gameplay strength and outcomes',
        no_training=True, no_win_measurement=True)
    if contract_path.exists():
        if read(contract_path) != contract:
            raise ValueError('discovery contract changed')
    else:
        save(contract_path, contract)
    for item in (contract['parent'], contract['runtime'], contract['source'], contract['helper']):
        verify(item)
    reports = []
    for index, job in enumerate(jobs):
        receipt = directory / 'discovery' / f'game_{index:05d}' / 'output/job.json'
        # Existing unfinished directories are refused by the native worker.
        # Inspect the actual process before deciding how to resume them.
        row = read(receipt) if receipt.exists() else _run_game(parent['install'], initial[0]['path'],
            directory / 'discovery', index, job, 4900, teacher=False, deterministic=True,
            timeout=120, executable=parent['runtime']['path'])
        if (any(row.get(k) != v for k,v in job.items()) or row['weights_sha256'] != initial[0]['sha256']
                or row.get('weight_version') != initial[0]['version'] or row.get('teacher')):
            raise ValueError('discovery receipt does not match its planned source/seed')
        reports.append(row)
        layouts = measured_layouts(reports, excluded_seeds=excluded)
        save(directory / 'discovery.json', dict(reports=reports, layouts=layouts, complete=len(layouts) == 12))
        print(f"discovery {index + 1}: seed={job['seed']} pair={row['start_pair']} coverage={len(layouts)}/12", flush=True)
        if len(layouts) == 12:
            save(directory / 'layouts.json', dict(layouts=layouts, excluded_seeds=excluded,
                discovery=pin(directory / 'discovery.json'), contract=pin(contract_path),
                status='heldout_layouts_ready_not_a_win_evaluation'))
            return
    raise RuntimeError('incomplete start coverage; inspect discovery and reserve an additional disjoint seed range')


def evaluate(campaign, directory, weights, workers):
    parent = read(campaign / 'contract.json')
    discovery = read(directory / 'layouts.json')
    for item in (discovery['discovery'], discovery['contract']):
        verify(item)
    discovery_contract = read(discovery['contract']['path'])
    if discovery_contract['runtime'] != parent['runtime']:
        raise ValueError('holdout discovery/runtime mismatch')
    selected = read(weights)
    sources = roster(selected.get('best', selected))
    target = directory / 'validation'
    seed = 2026091180000
    contract = dict(parent=pin(campaign / 'contract.json'), weights_input=pin(weights), initial=sources,
        runtime=parent['runtime'], install=parent['install'], layouts=discovery['layouts'],
        workers=workers, evaluation_seed=seed, pins=[parent['runtime'], pin(__file__),
            pin(weights), pin(directory / 'layouts.json'), *sources.values(),
            *[item for item in parent['pins'] if Path(item['path']).suffix == '.py']],
        excluded_seeds=discovery['excluded_seeds'], purpose='independent_validation_only_no_ppo')
    path = target / 'contract.json'
    if path.exists():
        if read(path) != json.loads(json.dumps(contract)):
            raise ValueError('cannot change the roster of a started validation')
    else:
        save(path, contract)
    runner = Campaign(target)
    import msvcrt
    with (target / 'validation.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            deterministic = runner.evaluate('argmax', sources)
            stochastic = runner.evaluate('sampling', sources, deterministic=False, seed_offset=10000)
            result = gate(deterministic['reports'], stochastic['reports'], sources, discovery['layouts'],
                excluded_seeds=discovery['excluded_seeds'], policy_seed=seed)
            save(target / 'gate.json', dict(**result, roster=sources, contract=pin(path),
                argmax=pin(target / 'argmax/result.json'), sampling=pin(target / 'sampling/result.json')))
            runner.status('independent_validation_passed' if result['passed'] else 'independent_validation_failed', gate=result)
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['discover', 'evaluate'])
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--roster', type=Path)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    if args.mode == 'discover':
        discover(args.campaign.resolve(), args.directory.resolve())
    elif not args.roster or args.workers < 1:
        parser.error('evaluate requires --roster and positive --workers')
    else:
        evaluate(args.campaign.resolve(), args.directory.resolve(), args.roster.resolve(), args.workers)
