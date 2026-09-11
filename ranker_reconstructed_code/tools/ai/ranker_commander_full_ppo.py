"""Continue an evaluated roster with PPO over the complete commander policy.

The native executor and existing policies are immutable. Full paired evaluations
protect the best roster; temporary score drops do not reset the learning roster.
Only repeated large regressions reduce its learning rate. Each fit uses fresh
current-policy episodes and preserves Adam state across accepted full-policy fits.
"""
from __future__ import annotations

import argparse
import copy
from dataclasses import asdict
import json
from pathlib import Path
import traceback

import torch

from ranker_commander_improve import (Campaign, RACES, builtin_jobs, measure, pin,
    read, roster, save, summarize, validate_reports, verify)
from ranker_commander_model import ARCHITECTURE, HEAD_SIZES, load_weights
from ranker_commander_rollout import read_rollout
from ranker_commander_selfplay_batch import current_episodes
from ranker_commander_train import (TrainConfig, build_batch, load_optimizer,
    save_checkpoint, train_update)

SCOPE = 'all_policy_parameters'


def full_policy_scope(policy):
    if policy.architecture != ARCHITECTURE or policy.head_sizes != HEAD_SIZES:
        raise ValueError('full PPO requires the current commander architecture')
    names = []
    for name, parameter in policy.named_parameters():
        parameter.requires_grad_(True)
        names.append(name)
    return dict(trainable=names, frozen=[])


def select_learning_rosters(best, current, candidate, baseline, evaluation,
                            preflight, rates, streaks, *, gap=6, patience=3,
                            reduction=.5, minimum_rate=3e-6):
    if gap < 1 or patience < 2 or not 0 < reduction < 1 or minimum_rate <= 0:
        raise ValueError('invalid regression control')
    next_best, next_current, next_rates, next_streaks, decisions = {}, {}, {}, {}, {}
    for race in RACES:
        before, after = baseline[race], evaluation[race]
        if not all(s['coverage_complete'] and s['valid'] and s['games'] == 48
                   and 0 <= s['wins'] <= 48 for s in (before, after)):
            raise ValueError('selection requires complete valid paired evaluations')
        accepted = bool(preflight[race])
        improved = accepted and after['wins'] > before['wins']
        large_drop = accepted and before['wins'] - after['wins'] >= gap
        streak = streaks[race] + 1 if large_drop else 0
        reduce_rate = not accepted or streak >= patience
        next_best[race] = candidate[race] if improved else best[race]
        next_current[race] = candidate[race] if accepted else current[race]
        next_rates[race] = max(minimum_rate, rates[race] * reduction) if reduce_rate else rates[race]
        next_streaks[race] = 0 if reduce_rate else streak
        decisions[race] = dict(improved=improved, learner_advanced=accepted,
            before_wins=before['wins'], after_wins=after['wins'],
            large_regression=large_drop, consecutive_large_regressions=streak,
            learning_rate_reduced=reduce_rate, learning_rate=next_rates[race],
            reason='update_width_rejected' if not accepted else
                'repeated_large_regressions' if reduce_rate else 'continue_learning')
    return next_best, next_current, next_rates, next_streaks, decisions


def merge_best_reports(baseline, evaluation, decisions):
    improved = {r for r in RACES if decisions[r]['improved']}
    reports = [r for r in baseline['reports'] if r['own_tribe'] not in improved]
    reports += [r for r in evaluation['reports'] if r['own_tribe'] in improved]
    return dict(reports=reports, summary=summarize(reports))


def bootstrap(parent_directory, directory):
    """Import completed evaluations only, while holding the stopped parent's lock."""
    parent, directory = Path(parent_directory).resolve(), Path(directory).resolve()
    if (directory / 'contract.json').exists():
        if read(directory / 'contract.json')['parent_directory'] != str(parent):
            raise ValueError('continuation belongs to another parent')
        return
    import msvcrt
    with (parent / 'controller.lock').open('r+b') as lock:
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            contract = read(parent / 'contract.json')
            for item in contract['pins']:
                verify(item)
            initial = roster(contract['initial'])
            current = roster(read(parent / 'round_000/fit/roster.json'))
            baseline = read(parent / 'baseline/result.json')
            evaluation = read(parent / 'round_000/evaluation/result.json')
            for result, models in ((baseline, initial), (evaluation, current)):
                validate_reports(result['reports'], builtin_jobs(models, contract['layouts'],
                    policy_seed_base=contract['evaluation_seed']), models, deterministic=True)
                result['summary'] = summarize(result['reports'])
            preflight = {r: read(parent / f'round_000/fit/race{r}/fit.json')['preflight_passed'] for r in RACES}
            rates, streaks = {r: 1e-5 for r in RACES}, {r: 0 for r in RACES}
            best, learner, _, _, decisions = select_learning_rosters(initial, current,
                current, baseline['summary'], evaluation['summary'], preflight, rates, streaks)
            selection = dict(best=best, current=learner,
                baseline=merge_best_reports(baseline, evaluation, decisions),
                learning_rates=rates, regression_streaks=streaks,
                decisions=decisions, no_improvement_rounds=0)
            directory.mkdir(parents=True, exist_ok=True)
            save(directory / 'initial_selection.json', selection)
            inherited = copy.deepcopy(contract)
            inherited.update(parent_directory=str(parent), workers=contract['workers'],
                rounds=6, learning_rate=1e-5, training_seed=2026091190000,
                scope=SCOPE, regression_gap=6, regression_patience=3,
                learning_rate_reduction=.5, minimum_learning_rate=3e-6,
                plateau_patience=6, version_base=2000,
                selection='Preserve best full-evaluation scores; keep accepted learners through temporary declines',
                lr_rationale='Small full-policy PPO steps; reduce only after trust-gate rejection or three large score regressions',
                runtime_change='None; continue the evaluated runtime and existing action space',
                plateau='Review learning after six full-policy rounds without a best-score improvement')
            inherited['pins'] += [pin(path) for path in [Path(__file__),
                parent / 'contract.json', parent / 'baseline/result.json',
                parent / 'round_000/evaluation/result.json',
                parent / 'round_000/fit/roster.json', directory / 'initial_selection.json']]
            inherited['pins'] += [pin(current[r]['path']) for r in RACES]
            save(directory / 'contract.json', inherited)
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


class FullPolicyCampaign(Campaign):
    def fit_full(self, stage, collection, current, rates, round_index):
        self.check()
        self.status(stage)
        selected = current_episodes(collection['reports'], current)
        audits = {a['rollout']['path']: a for a in collection['audits']}
        trained, gates = {}, {}
        for race in RACES:
            output = self.directory / stage / f'race{race}/policy.bin'
            report_path = output.parent / 'fit.json'
            config = TrainConfig(mode='ppo', iteration=round_index + 1, epochs=3,
                minibatch=1024, critic_warmup=0, learning_rate_initial=rates[race],
                learning_rate_final=rates[race], gamma=1., gae_lambda=.98,
                teacher_kl_initial=.05, teacher_kl_floor=.05,
                seed=self.contract['training_seed'] + race + round_index * 4)
            inputs = json.loads(json.dumps(dict(scope=SCOPE, source=current[race], config=asdict(config),
                episodes=[audits[i['path']]['rollout'] for i in selected[race]])))
            for item in inputs['episodes']:
                verify(item)
            if report_path.exists():
                report = read(report_path)
                if report['inputs'] != inputs:
                    raise ValueError('cached full-policy fit inputs changed')
                verify(report['checkpoint'])
                verify(report['checkpoint']['optimizer'])
                trained[race], gates[race] = report['checkpoint'], report['preflight_passed']
                continue
            if output.exists():
                raise RuntimeError(f'partial checkpoint needs inspection: {output}')
            episodes = []
            try:
                for item in selected[race]:
                    episodes.append(read_rollout(item['path'], current_version=current[race]['version'], teacher=False))
                torch.manual_seed(config.seed)
                policy = load_weights(current[race]['path'])
                scope = full_policy_scope(policy)
                original = {n: p.detach().clone() for n, p in policy.named_parameters()}
                reference = load_weights(self.contract['initial'][str(race)]['path'])
                optimizer = torch.optim.Adam(policy.parameters(), lr=rates[race])
                resumed_optimizer = False
                if current[race].get('full_policy_optimizer'):
                    verify(current[race]['optimizer'])
                    load_optimizer(optimizer, current[race]['optimizer']['path'], version=current[race]['version'])
                    resumed_optimizer = True
                batch, reward = build_batch(episodes, config)
                before = measure(policy, batch)
                if abs(before['approximate_kl']) > 1e-6 or before['clip_fraction'] != 0:
                    raise ValueError('PPO batch is not from the exact current actor')
                optimizer, metrics = train_update(policy, batch, config, optimizer=optimizer,
                    teacher_policy=reference,
                    progress_callback=lambda p: self.event(stage=stage, race=race, training=p))
                after = measure(policy, batch)
                changed = [n for n, p in policy.named_parameters() if not torch.equal(p, original[n])]
                if not changed or not set(changed) <= set(scope['trainable']):
                    raise ValueError('invalid full-policy update')
                if not any(n.startswith(('heads.', 'head_adapters.')) for n in changed):
                    raise ValueError('full PPO failed to update the actor')
                passed = after['approximate_kl'] <= .02 and after['clip_fraction'] <= .15
                version = self.contract['version_base'] + (round_index + 1) * 10 + race
                save_checkpoint(policy, output, version=version,
                    metadata=dict(**inputs, mode='ppo', own_tribe=race, before=before,
                        after=after, changed=changed, all_parameters_trainable=True,
                        optimizer_resumed=resumed_optimizer, reward=reward, metrics=metrics,
                        preflight_passed=passed), optimizer=optimizer)
                exported = load_weights(output)
                if any(not torch.equal(p, exported.state_dict()[n]) for n, p in policy.named_parameters()):
                    raise ValueError('checkpoint export changed weights')
                checkpoint = dict(**pin(output), version=version, race=race,
                    optimizer=pin(output.with_suffix('.bin.optimizer.npz')), full_policy_optimizer=True)
                report = dict(inputs=inputs, checkpoint=checkpoint, preflight_passed=passed,
                    before=before, after=after, decisions=len(batch['target']), changed=changed,
                    all_parameters_trainable=True, optimizer_resumed=resumed_optimizer)
                save(report_path, report)
                trained[race], gates[race] = checkpoint, passed
                self.event(stage=stage, race=race, fit=report)
            finally:
                for episode in episodes:
                    episode.close()
        save(self.directory / stage / 'roster.json', trained)
        return trained, gates

    def run(self):
        torch.set_num_threads(2)
        state = read(self.directory / 'initial_selection.json')
        for index in range(self.contract['rounds']):
            best, current = roster(state['best']), roster(state['current'])
            baseline = state['baseline']
            baseline['summary'] = {int(k): v for k, v in baseline['summary'].items()}
            rates = {int(k): v for k, v in state['learning_rates'].items()}
            streaks = {int(k): v for k, v in state['regression_streaks'].items()}
            stage = f'round_{index:03d}'
            selection_path = self.directory / stage / 'selection.json'
            if selection_path.exists():
                state = read(selection_path)
            else:
                data = self.collect(stage + '/collect', current, best, index)
                candidate, gates = self.fit_full(stage + '/fit', data, current, rates, index)
                screened = {r: candidate[r] if gates[r] else current[r] for r in RACES}
                evaluation = self.evaluate(stage + '/evaluation', screened)
                best, current, rates, streaks, decisions = select_learning_rosters(best, current,
                    screened, baseline['summary'], evaluation['summary'], gates, rates, streaks,
                    gap=self.contract['regression_gap'], patience=self.contract['regression_patience'],
                    reduction=self.contract['learning_rate_reduction'], minimum_rate=self.contract['minimum_learning_rate'])
                improved = any(d['improved'] for d in decisions.values())
                state = dict(best=best, current=current, baseline=merge_best_reports(baseline, evaluation, decisions),
                    learning_rates=rates, regression_streaks=streaks, decisions=decisions,
                    no_improvement_rounds=0 if improved else state['no_improvement_rounds'] + 1)
                save(selection_path, state)
            best, current = roster(state['best']), roster(state['current'])
            self.status('round_complete', round=index, selection=state['decisions'],
                best=best, current=current, learning_rates=state['learning_rates'],
                no_improvement_rounds=state['no_improvement_rounds'])
            if all(s['passed_100_percent'] for s in state['baseline']['summary'].values()):
                confirmation = self.evaluate(stage + '/stochastic_confirmation', best,
                    deterministic=False, seed_offset=200000 + index * 1000)
                if all(s['passed_100_percent'] for s in confirmation['summary'].values()):
                    self.status('independent_validation_required', best=best,
                        reason='Verify unseen game seeds before goal completion')
                    return
            if state['no_improvement_rounds'] >= self.contract['plateau_patience']:
                self.status('analysis_required', best=best, current=current,
                    reason='Six full-policy evaluation rounds without a best-score improvement; review learning before any code changes')
                return
        self.status('batch_complete_goal_active', best=state['best'], current=state['current'],
            reason='Continue full-policy learning; this is not goal completion')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--parent', type=Path)
    args = parser.parse_args(argv)
    torch.set_num_threads(2)
    args.directory.mkdir(parents=True, exist_ok=True)
    import msvcrt
    with (args.directory / 'controller.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            if args.parent:
                bootstrap(args.parent, args.directory)
            campaign = FullPolicyCampaign(args.directory)
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
