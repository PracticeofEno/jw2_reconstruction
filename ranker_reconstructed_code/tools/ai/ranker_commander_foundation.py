"""Build race-specific teachers before BC/DAgger and PPO.

All matches use normal builtin opponents and an explicit measured layout plan.
Screening never approves a teacher or learned checkpoint. The 48-condition
development benchmark includes conditions used for teacher selection; it is
not an independent generalization measurement.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import time

import numpy as np

from ranker_commander_cohort_report import atomic_write
from ranker_commander_eval import _run_game, extract_start_slots, validate_terminal_result
from ranker_commander_model import SCHEMA_CRC, load_weights
from ranker_commander_rollout import WIN, LOSS, read_rollout, relabel_with_teacher
from ranker_commander_train import actor_trajectory_fingerprint, close_episode

ROOT = Path(__file__).resolve().parents[3]
NAMES = {0: 'primitive', 1: 'elf', 3: 'demon'}
ACTIVE = (1, 3, 0)
SCREEN = [12 * t + 4 * k + ((t + k) % 4) for t in range(4) for k in range(3)]
PAIRS = {(a, b) for a in range(4) for b in range(4) if a != b}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(data)
    return digest.hexdigest()


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    atomic_write(path, json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n')


def stamp():
    return datetime.now().astimezone().isoformat(timespec='seconds')


def scenario(row):
    pair = tuple(row['start_pair'])
    require(pair in PAIRS and row['tribe'] in range(4), 'invalid opponent/start pair')
    return row['tribe'], *pair


def validate_plan(jobs):
    require(len(jobs) == 48 and {scenario(j) for j in jobs} == {
        (t, *p) for t in range(4) for p in PAIRS}, 'plan must contain all 48 distinct conditions')
    require(all(j['curriculum'] == 2 and j['max_frames'] == 60000 and j.get('opp_slow', 0) == 0
                for j in jobs), 'plan must use normal builtin C2 opponents for 60000 frames')
    selected = [jobs[i] for i in SCREEN]
    require(Counter(j['tribe'] for j in selected) == {t: 3 for t in range(4)}
            and {tuple(j['start_pair']) for j in selected} == PAIRS,
            'screen must balance four opponents and all twelve ordered start pairs')


def phase_jobs(contract, tribe, variant, mode):
    require(tribe in ACTIVE, 'Tyrano is excluded from foundation training')
    require(mode in ('teacher', 'argmax', 'sampling'), 'unsupported collection mode')
    require(type(variant) is int and 0 <= variant <= 0xffffffff, 'invalid teacher variant')
    return [dict(seed=j['seed'], tribe=j['tribe'], start_pair=j['start_pair'],
                 own_tribe=tribe, curriculum=2, max_frames=60000, opp_slow=0,
                 coordinated_transfers=False, teacher_variant=variant,
                 dagger=mode != 'teacher', policy_seed=202609090000 + tribe * 1000 + i)
            for i, j in enumerate(contract['jobs48'])]


def prepare(args):
    require(not (args.work_dir / 'contract.json').exists(), 'foundation contract already exists')
    source_plan = read(args.plan)
    jobs = source_plan['jobs48']
    validate_plan(jobs)
    parent_raw = args.parent.read_bytes()
    parent = json.loads(parent_raw)
    require(parent['schema_crc'] == SCHEMA_CRC, 'parent schema is not current')
    require(args.executable.name == 'ranker_rebuild.exe', 'unexpected rebuilt executable filename')
    args.work_dir.mkdir(parents=True, exist_ok=True)
    (args.work_dir / 'parent_state.json').write_bytes(parent_raw)
    inputs = [Path(__file__), Path(__file__).with_name('ranker_commander_eval.py'),
              Path(__file__).with_name('ranker_commander_rollout.py'),
              Path(__file__).with_name('ranker_commander_model.py'),
              Path(__file__).with_name('ranker_commander_train.py'),
              Path(__file__).with_name('commander_races.json'),
              Path(__file__).with_name('commander_skills.json'),
              ROOT / 'ranker_reconstructed_code/src/ranker_ai_commander.cpp',
              ROOT / 'ranker_reconstructed_code/include/ranker_ai_commander.h', args.executable]
    inputs.extend((ROOT / 'RankerOCPV_Win').glob('*.trc'))
    weights = {}
    for tribe in ACTIVE:
        row = parent['races'][str(tribe)]
        entry = row.get('best_evaluated', row.get('baseline', row))
        require(sha(entry['policy']) == entry['sha256'], 'parent policy hash changed')
        model = load_weights(entry['policy'])
        require(model.weight_version == entry['version'], 'parent policy version mismatch')
        destination = args.work_dir / NAMES[tribe] / 'initial.bin'
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry['policy'], destination)
        weights[str(tribe)] = dict(policy=str(destination.resolve()), sha256=sha(destination),
                                  version=entry['version'], source=entry['policy'])
    tyrano = parent['races']['2']
    require(sha(tyrano['policy']) == tyrano['sha256'], 'excluded Tyrano policy hash changed')
    contract = dict(created=stamp(), schema_crc=SCHEMA_CRC, active_races=list(ACTIVE),
        jobs48=jobs, screen_indices=SCREEN, coordinated_transfers=False,
        install_dir=str((ROOT / 'RankerOCPV_Win').resolve()), executable=str(args.executable.resolve()),
        inputs={str(p.resolve()): sha(p) for p in inputs}, weights=weights,
        frozen_tyrano={k: tyrano[k] for k in ('policy', 'sha256', 'version')},
        parent_snapshot_sha256=hashlib.sha256(parent_raw).hexdigest(),
        teacher_gate=dict(full_games=48, minimum_wins=36, minimum_per_opponent=6),
        learning_order=['teacher_screen_12', 'promising_teacher_full_48',
                        'race_specific_BC_with_scenario_holdout', 'corrective_DAgger',
                        'BC_gameplay_screen_12_and_full_48', 'PPO_after_foundation_gate'],
        evaluation_use='fixed development benchmark; not independent generalization',
        data_contract='native current-schema RLOs only; transfer mode 0; scenario-group holdout; actor deduplication')
    save(args.work_dir / 'contract.json', contract)
    save(args.work_dir / 'state.json', dict(status='teacher_prepared', phase='foundation',
         pid=None, active=None, created=stamp(), races=weights, ppo_enabled=False))
    print(json.dumps(dict(prepared=str(args.work_dir), baseline=weights, screen=SCREEN)), flush=True)


def verify_contract(work):
    contract = read(work / 'contract.json')
    validate_plan(contract['jobs48'])
    require(contract['schema_crc'] == SCHEMA_CRC and contract['coordinated_transfers'] is False,
            'foundation observation/action contract changed')
    for path, expected in contract['inputs'].items():
        require(sha(path) == expected, f'foundation input changed: {path}; prepare a new contract')
    require(sha(work / 'parent_state.json') == contract['parent_snapshot_sha256'], 'parent snapshot changed')
    for entry in [*contract['weights'].values(), contract['frozen_tyrano']]:
        require(sha(entry['policy']) == entry['sha256'], 'frozen policy changed')
    return contract


def validate_receipt(row, job, *, weights_sha, teacher, deterministic, executable):
    require(row.get('valid') and row.get('evaluation_valid'), 'unfinished or invalid match')
    require(all(row.get(k) == value for k, value in job.items()), 'receipt differs from declared job')
    require(row.get('teacher') is teacher and row.get('deterministic') is deterministic,
            'teacher/policy mode mismatch')
    require(row.get('weights_sha256') == weights_sha, 'receipt policy hash mismatch')
    require(not row.get('rollout2') and row.get('train_owner', 1) == 1,
            'foundation must control owner 1 against builtin owner 2')
    command = row['command']
    require(Path(command[0]).resolve() == Path(executable).resolve(), 'receipt runtime mismatch')
    for flag in ('-AICOORDINATEDTRANSFERS:0', '-AICURRICULUM:2', '-MAXFRAMES:60000',
                 f"-AIOWNTRIBE:{job['own_tribe']}", f"-AITRIBE:{job['tribe']}",
                 f"-SEED:{job['seed']}", f"-AIPOLICYSEED:{job['policy_seed']}"):
        require(command.count(flag) == 1, f'missing or duplicate command flag: {flag}')
    require(('-AITEACHER' in command) is teacher and ('-AIDETERMINISTIC' in command) is deterministic,
            'actual actor command differs from receipt')
    require(('-AIDAGGER' in command) is job['dagger'], 'DAgger recording mode mismatch')
    require(not any(c == '-AIVS' or c.startswith('-AIOPPSLOW:') or c == '-AITEACHER2' for c in command),
            'opponent is not the normal builtin AI')
    variants = [c for c in command if c.startswith('-AITEACHERVAR:')]
    require(variants == ([f"-AITEACHERVAR:{job['teacher_variant']}"] if job['teacher_variant'] else []),
            'actual teacher recipe differs from declared recipe')
    require(row['commander_metrics']['mask_violations'] == 0, 'native illegal action detected')
    return scenario(row)


def validate_training_elimination(result, status):
    # Historical RLOs correctly labelled trap-only owners as eliminated, but
    # the harness kept collecting decisions until the cap. A correct final
    # label does not make that post-elimination trajectory valid training data.
    if result['reason'] == 'max_frames' and status in (WIN, LOSS):
        buildings = {row['owner']: row['buildings'] for row in result['owners']}
        require(not (buildings.get(1, 0) > 0 and buildings.get(2, 0) > 0),
                'possible trap-delayed termination; recollect with the corrected runtime before training')


def audit_episode(row, job, weights_sha, *, mode, executable):
    validate_receipt(row, job, weights_sha=weights_sha, teacher=mode == 'teacher',
                     deterministic=mode != 'sampling', executable=executable)
    episode = read_rollout(row['rollout'], teacher=mode == 'teacher')
    try:
        require(episode.owner == 1 and episode.seed == job['seed']
                and episode.weight_version == row['weight_version'], 'RLO owner/seed/version differs')
        require(np.all(episode.records['vector'][:, 606:610] == np.eye(4)[job['own_tribe']]),
                'RLO contains the wrong race observation')
        output = Path(row['rollout']).parent
        result = read(output / 'ai_selfplay_result.json')
        require(validate_terminal_result(episode, result) == row['status'] and result == row['result'],
                'terminal result differs from receipt')
        validate_training_elimination(result, row['status'])
        slots = extract_start_slots((output / 'Jw2.log').read_text(encoding='utf-8', errors='replace'))
        require([slots[1]['map_slot'], slots[2]['map_slot']] == job['start_pair']
                and slots[1]['tribe'] == job['own_tribe'] and slots[2]['tribe'] == job['tribe'],
                'actual startup scenario differs')
        record = dict(rollout=row['rollout'], rollout_sha256=sha(row['rollout']),
            actor_fingerprint=actor_trajectory_fingerprint(episode),
            decisions=len(episode.decisions), status=row['status'], scenario=list(scenario(row)),
            max_bases=int(round(float(episode.records['vector'][:, 86].max()) * 4)))
        if mode != 'teacher':
            labelled = relabel_with_teacher(episode)
            try:
                record['teacher_labels_sha256'] = sha(str(episode.path) + '.teacher.bin')
                record['labelled_actor_fingerprint'] = actor_trajectory_fingerprint(labelled)
            finally:
                close_episode(labelled)
        return record
    finally:
        close_episode(episode)


def summarize(rows, expected):
    require(len(rows) == len(expected), 'incomplete phase')
    require(len({scenario(r) for r in rows}) == len(rows), 'repeated deterministic scenario')
    require({scenario(r) for r in rows} == {scenario(j) for j in expected}, 'phase scenario coverage differs')
    counts = Counter(r['status'] for r in rows)
    return dict(games=len(rows), wins=counts[1], losses=counts[2], truncated=counts[3],
        unique_conditions=len(rows), per_opponent={str(t):dict(
            games=sum(r['tribe'] == t for r in rows), wins=sum(r['tribe'] == t and r['status'] == WIN for r in rows))
            for t in range(4)}, mask_violations=sum(r['commander_metrics']['mask_violations'] for r in rows),
        silent_rejections=sum(r['commander_metrics']['silent_rejections'] for r in rows))


def teacher_ready(summary, contract):
    gate = contract['teacher_gate']
    return (summary['games'] == summary['unique_conditions'] == gate['full_games']
            and summary['wins'] >= gate['minimum_wins'] and summary['mask_violations'] == 0
            and all(summary['per_opponent'][str(t)]['games'] == 12
                    and summary['per_opponent'][str(t)]['wins'] >= gate['minimum_per_opponent'] for t in range(4)))


def run_phase(args, contract, tribe, variant, *, full=False):
    mode = args.mode
    jobs = phase_jobs(contract, tribe, variant, mode)
    indices = list(range(48)) if full else SCREEN
    baseline = contract['weights'][str(tribe)]
    weights = args.weights.resolve() if args.weights else Path(baseline['policy'])
    weights_sha = sha(weights)
    if mode == 'teacher':
        directory = args.work_dir / NAMES[tribe] / 'teachers' / f'variant_{variant:010d}'
        game_root = directory / 'games'
    else:
        # Existing latest-match viewer recognizes this registered campaign path.
        directory = args.work_dir / NAMES[tribe] / f'update_foundation_{weights_sha[:12]}_v{variant}' / 'evaluation' / mode
        game_root = directory
    directory.mkdir(parents=True, exist_ok=True)
    phase = dict(contract_sha256=sha(args.work_dir / 'contract.json'), mode=mode, tribe=tribe,
                 variant=variant, weights=str(weights), weights_sha256=weights_sha, jobs=jobs)
    if (directory / 'contract.json').exists():
        require(read(directory / 'contract.json') == phase, 'cached phase contract differs')
    else:
        save(directory / 'contract.json', phase)
    saved = read(directory / 'receipts.json') if (directory / 'receipts.json').exists() else {}
    rows, audits = {}, {}
    for index in indices:
        durable = directory / 'validated' / f'game_{index:05d}.json'
        if str(index) not in saved and durable.exists():
            saved[str(index)] = read(durable)
        if str(index) not in saved:
            continue
        item = saved[str(index)]
        require(sha(item['receipt']) == item['receipt_sha256'], 'cached receipt changed')
        row = read(item['receipt'])
        audited = audit_episode(row, jobs[index], weights_sha, mode=mode, executable=contract['executable'])
        require(audited == item['audit'], 'cached episode or labels changed')
        rows[index], audits[index] = row, audited
    pending = [index for index in indices if index not in rows]
    state_path = args.work_dir / 'state.json'

    def progress(status):
        state = read(state_path)
        state.update(status=status, pid=os.getpid(), updated=stamp(), active=dict(
            race=NAMES[tribe], teacher_variant=variant, mode=mode, completed=len(rows),
            total=len(indices), directory=str(directory.resolve()), screen=not full))
        save(state_path, state)

    progress('collecting')

    def worker(slot, index):
        if index in pending[:args.workers]:
            time.sleep(slot * 0.3)
        # No live directory is overwritten on resume. Failed partial launches
        # are preserved in a sibling diagnostic directory before retry.
        existing = game_root / f'game_{index:05d}'
        if existing.exists():
            retry = existing.with_name(existing.name + f'_incomplete_{time.time_ns()}')
            require(existing.resolve().is_relative_to(args.work_dir)
                    and retry.resolve().is_relative_to(args.work_dir), 'retry path escaped experiment')
            existing.rename(retry)
        row = _run_game(contract['install_dir'], weights, game_root, index, jobs[index],
            args.slot_offset + slot, teacher=mode == 'teacher', deterministic=mode != 'sampling',
            timeout=1200, executable=contract['executable'])
        audit = audit_episode(row, jobs[index], weights_sha, mode=mode, executable=contract['executable'])
        receipt = Path(row['rollout']).with_name('job.json')
        save(directory / 'validated' / f'game_{index:05d}.json',
             dict(receipt=str(receipt), receipt_sha256=sha(receipt), audit=audit))
        print(json.dumps(dict(time=stamp(), kind='game', race=NAMES[tribe], variant=variant,
            mode=mode, job=index, status=row['status'], end_frame=row['end_frame'],
            max_bases=audit['max_bases'])), flush=True)
        return index, row, audit

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        remaining = iter(pending)
        running = {}
        for slot in range(min(args.workers, len(pending))):
            running[executor.submit(worker, slot, next(remaining))] = slot
        while running:
            completed, _ = wait(running, timeout=5, return_when=FIRST_COMPLETED)
            for future in completed:
                slot = running.pop(future)
                index, row, audit = future.result()
                receipt = Path(row['rollout']).with_name('job.json')
                rows[index], audits[index] = row, audit
                saved[str(index)] = dict(receipt=str(receipt), receipt_sha256=sha(receipt), audit=audit)
                next_index = next(remaining, None)
                if next_index is not None:
                    running[executor.submit(worker, slot, next_index)] = slot
            if completed:
                save(directory / 'receipts.json', saved)
            progress('collecting')
    verify_contract(args.work_dir)
    require(sha(weights) == weights_sha, 'weights changed during collection')
    reports = [rows[i] for i in indices]
    summary = summarize(reports, [jobs[i] for i in indices])
    record = dict(time=stamp(), kind='teacher_cohort' if mode == 'teacher' else 'policy_evaluation',
        race=NAMES[tribe], variant=variant, mode=mode, screen=not full, summary=summary,
        teacher_ready=mode == 'teacher' and teacher_ready(summary, contract),
        ppo_enabled=False, reports=reports, audits=[audits[i] for i in indices])
    save(directory / ('full.json' if full else 'screen.json'), record)
    with (args.work_dir / 'events.jsonl').open('a', encoding='utf-8') as stream:
        stream.write(json.dumps({k:v for k,v in record.items() if k not in ('reports', 'audits')}) + '\n')
    progress('cohort_complete')
    print(json.dumps({k:v for k,v in record.items() if k not in ('reports', 'audits')}), flush=True)
    return record


def split_sources(sources, held_scenarios):
    """Hold out entire opponent/start groups across policy seeds and recipes."""
    training, validation, seen = [], [], set()
    for source in sources:
        fingerprint = source['actor_fingerprint']
        if fingerprint in seen:
            continue
        seen.add(fingerprint)
        target = validation if tuple(source['scenario']) in held_scenarios else training
        target.append(source)
    train_hashes = {s['actor_fingerprint'] for s in training}
    require(not train_hashes & {s['actor_fingerprint'] for s in validation}, 'actor overlap across split')
    require(not {tuple(s['scenario']) for s in training} & {tuple(s['scenario']) for s in validation},
            'scenario overlap across split')
    return training, validation


def dataset(args, contract, tribe):
    sources, qualified = [], set()
    for path in sorted((args.work_dir / NAMES[tribe] / 'teachers').glob('*/full.json')):
        phase = read(path)
        require(phase['mode'] == 'teacher' and not phase['screen'], 'teacher phase metadata mismatch')
        jobs = phase_jobs(contract, tribe, phase['variant'], 'teacher')
        summary = summarize(phase['reports'], jobs)
        require(summary == phase['summary'], 'teacher summary differs from recorded matches')
        if not teacher_ready(summary, contract):
            continue
        qualified.add(phase['variant'])
        expected_audits = {a['rollout']: a for a in phase['audits']}
        phase_contract = read(path.parent / 'contract.json')
        for row in phase['reports']:
            audited = audit_episode(row, jobs[row['job']], phase_contract['weights_sha256'],
                                    mode='teacher', executable=contract['executable'])
            require(audited == expected_audits[row['rollout']], 'teacher source changed since evaluation')
            if row['status'] == WIN:
                sources.append(dict(**audited, receipt=str(Path(row['rollout']).with_name('job.json')),
                                    kind='teacher', variant=phase['variant'], mode='teacher'))
    require(qualified, 'no teacher has passed the full race-specific foundation gate; improve teachers first')
    # Corrective labels are admitted only from the same validated teacher recipe.
    for phase_path in args.dagger_phases:
        phase = read(phase_path)
        require(phase['mode'] in ('argmax', 'sampling') and phase['variant'] in qualified
                and phase['race'] == NAMES[tribe], 'DAgger teacher recipe/race is not qualified')
        jobs = phase_jobs(contract, tribe, phase['variant'], phase['mode'])
        phase_contract = read(phase_path.parent / 'contract.json')
        expected_audits = {a['rollout']: a for a in phase['audits']}
        for row in phase['reports']:
            audited = audit_episode(row, jobs[row['job']], phase_contract['weights_sha256'],
                                    mode=phase['mode'], executable=contract['executable'])
            require(audited == expected_audits[row['rollout']], 'corrective rollout or labels changed')
            # A failed policy is precisely where corrective expert labels matter.
            # Retain actual native history/observations; replace only masks/actions.
            sources.append(dict(**audited, receipt=str(Path(row['rollout']).with_name('job.json')),
                kind='dagger', variant=phase['variant'], mode=phase['mode']))
            sources[-1]['executed_actor_fingerprint'] = sources[-1]['actor_fingerprint']
            sources[-1]['actor_fingerprint'] = sources[-1]['labelled_actor_fingerprint']
    held_scenarios = {scenario(contract['jobs48'][i]) for i in SCREEN}
    training, validation = split_sources(sources, held_scenarios)
    require(training and validation, 'both scenario-separated training and validation are required')
    output = dict(race=NAMES[tribe], tribe=tribe, schema_crc=SCHEMA_CRC,
        contract_sha256=sha(args.work_dir / 'contract.json'), qualified_variants=sorted(qualified),
        method='scenario holdout across all recipes/seeds plus exact actor deduplication',
        training=training, validation=validation, ppo_enabled=False,
        counts={name:dict(episodes=len(rows), decisions=sum(r['decisions'] for r in rows),
                         teacher=sum(r['kind'] == 'teacher' for r in rows),
                         dagger=sum(r['kind'] == 'dagger' for r in rows))
                for name, rows in [('training', training), ('validation', validation)]})
    save(args.work_dir / NAMES[tribe] / 'dataset.json', output)
    return output


def fit(args, contract, tribe):
    import torch
    from dataclasses import asdict
    from ranker_commander_train import (TrainConfig, build_batch, train_update,
        assess_bc_accuracy, save_checkpoint)
    prepared = dataset(args, contract, tribe)
    torch.set_num_threads(4)
    episodes = {'training': [], 'validation': []}
    try:
        for group, loaded in episodes.items():
            for source in prepared[group]:
                episode = read_rollout(source['rollout'], teacher=source['kind'] == 'teacher')
                if source['kind'] == 'dagger':
                    labelled = relabel_with_teacher(episode)
                    close_episode(episode)
                    episode = labelled
                loaded.append(episode)
        entry = contract['weights'][str(tribe)]
        policy = load_weights(args.weights or entry['policy'])
        config = TrainConfig(mode='bc', epochs=args.epochs, minibatch=2048,
            learning_rate_initial=1e-4, learning_rate_final=1e-4, gamma=1., gae_lambda=.98,
            bc_rare_weight=8, bc_class_power=.5, bc_class_cap=20,
            bc_class_skip=(38, 39, 40, 41), seed=20260909 + tribe)
        batch, rewards = build_batch(episodes['training'], config)
        before = assess_bc_accuracy(policy, episodes['validation'])
        _, metrics = train_update(policy, batch, config, progress_callback=lambda p:
                                  print(json.dumps(dict(race=NAMES[tribe], bc=p)), flush=True))
        after = assess_bc_accuracy(policy, episodes['validation'])
        directory = args.work_dir / NAMES[tribe] / 'warmstart' / f'fit_{time.time_ns()}'
        directory.mkdir(parents=True)
        output = directory / 'policy.bin'
        verify_contract(args.work_dir)
        save_checkpoint(policy, output, version=policy.weight_version + 1,
            metadata=dict(mode='race_foundation_bc', own_tribe=tribe, training_config=asdict(config),
                dataset_sha256=sha(args.work_dir / NAMES[tribe] / 'dataset.json'),
                source_policy=str(args.weights or entry['policy']),
                source_sha256=sha(args.weights or entry['policy']),
                validation_before=before, validation_after=after, ppo_enabled=False,
                admission='candidate requires paired 12-screen and full-48 gameplay validation',
                optimizer_reset_for_ppo=True, **rewards, **metrics))
        save(directory / 'dataset.json', prepared)
        print(json.dumps(dict(candidate=str(output), sha256=sha(output), counts=prepared['counts'],
            validation=after, ppo_enabled=False)), flush=True)
    finally:
        for group in episodes.values():
            for episode in group:
                close_episode(episode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'collect', 'dataset', 'fit', 'status'))
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--parent', type=Path)
    parser.add_argument('--plan', type=Path, default=ROOT / 'debug_artifacts/commander/strong_bot_20260908/plan.json')
    parser.add_argument('--executable', type=Path, default=ROOT / 'build/commander_race_teacher/ranker_rebuild.exe')
    parser.add_argument('--races', default='1,3,0')
    parser.add_argument('--variants', default='0,1073741824,3221225472')
    parser.add_argument('--mode', choices=('teacher', 'argmax', 'sampling'), default='teacher')
    parser.add_argument('--weights', type=Path)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--slot-offset', type=int, default=200)
    parser.add_argument('--full', action='store_true')
    parser.add_argument('--expand-promising', action='store_true')
    parser.add_argument('--dagger-phases', nargs='*', type=Path, default=[])
    parser.add_argument('--epochs', type=int, default=20)
    args = parser.parse_args()
    args.work_dir = args.work_dir.resolve()
    if args.command == 'prepare':
        require(args.parent is not None, 'prepare requires --parent')
        prepare(args)
        return
    if args.command == 'status':
        print(json.dumps(read(args.work_dir / 'state.json')), flush=True)
        return
    require(1 <= args.workers <= 8 and args.slot_offset >= 200, 'invalid isolated worker range')
    contract = verify_contract(args.work_dir)
    tribes = [int(x) for x in args.races.split(',')]
    variants = [int(x) for x in args.variants.split(',')]
    require(bool(tribes) and len(set(tribes)) == len(tribes) and set(tribes) <= set(ACTIVE), 'invalid race list')
    require(bool(variants) and len(set(variants)) == len(variants), 'invalid recipe list')
    require(not args.weights or len(tribes) == 1, 'explicit weights require exactly one race')
    if args.command in ('dataset', 'fit'):
        require(args.epochs > 0, 'BC epochs must be positive')
        for tribe in tribes:
            if args.command == 'dataset':
                result = dataset(args, contract, tribe)
                print(json.dumps(dict(race=NAMES[tribe], counts=result['counts'])), flush=True)
            else:
                fit(args, contract, tribe)
        return
    for variant in variants:
        for tribe in tribes:
            if (args.work_dir / 'STOP').exists():
                state = read(args.work_dir / 'state.json')
                state.update(status='stopped', active=None, updated=stamp())
                save(args.work_dir / 'state.json', state)
                return
            result = run_phase(args, contract, tribe, variant, full=args.full)
            # This threshold only spends additional evaluation time; a teacher
            # still needs 36/48 and at least 6/12 against EACH opponent for BC.
            if args.expand_promising and not args.full and result['summary']['wins'] >= 8:
                run_phase(args, contract, tribe, variant, full=True)
    state = read(args.work_dir / 'state.json')
    state.update(status='collection_complete', active=None, updated=stamp())
    save(args.work_dir / 'state.json', state)


if __name__ == '__main__':
    main()
