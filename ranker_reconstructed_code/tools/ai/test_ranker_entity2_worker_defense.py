"""Worker defence regressions using the masks emitted by the C++ snapshot.

Run: python -m unittest test_ranker_entity2_worker_defense
"""
from __future__ import annotations

import copy
from dataclasses import replace
import unittest

import torch

import ranker_entity2_contract as wire
import ranker_entity2_ppo as ppo
import ranker_entity2_squads as squads
from test_ranker_entity2_squads import (
    assert_wire_legal, blank_sample, fixture, parse,
)


def worker_fixture(specs):
    body, header = fixture(specs)
    # ranker_ai_entity_economy.cpp: calm workers cannot move or attack.
    for i, (template, _) in enumerate(specs):
        if template == 0:
            body["command_mask"][i] = sum(1 << c for c in (
                wire.COMMAND_KEEP, wire.COMMAND_HARVEST, wire.COMMAND_BUILD))
            body["attack_pair_mask_words"][i] = [0]
    return body, header


def threaten(body, row, commands, local_points=1, targets=1):
    # Threat response closes economy commands and all 64 global point tokens.
    body["command_mask"][row] = (1 << wire.COMMAND_KEEP) | sum(1 << c for c in commands)
    body["point_mask"][row] = [0, 0, local_points]
    body["attack_pair_mask_words"][row] = [targets]


class WorkerDefenseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_idle_worker_keeps_each_available_defence_without_ordering_calm_workers(self):
        for commands in ((wire.COMMAND_MOVE,), (wire.COMMAND_ATTACK_UNIT,),
                         (wire.COMMAND_MOVE, wire.COMMAND_ATTACK_UNIT)):
            with self.subTest(commands=commands):
                body, header = worker_fixture([(0, 0x20)] * 3)
                threaten(body, 1, commands, local_points=1 << 5,
                         targets=int(wire.COMMAND_ATTACK_UNIT in commands))
                request, header = parse(body, header)
                step = ppo.step_from_request(request, header)
                rows = step.control_layout.rows
                defense = next(j for j, row in enumerate(rows)
                               if row.kind == squads.INDIVIDUAL and row.members == (1,))
                self.assertEqual([row.members for row in rows if row.kind == squads.SQUAD],
                                 [(0, 2)])
                self.assertFalse(any(row.kind == squads.ECONOMY and row.members == (1,)
                                     for row in rows))
                for command in commands:
                    self.assertTrue(bool(step.command_mask[defense, command]))
                    sample = blank_sample(step)
                    sample["command"][defense] = command
                    sample["argument"][defense] = 69 if command == wire.COMMAND_MOVE else 0
                    expanded = squads.expand_actions(step, sample)
                    self.assertEqual(expanded["command"], [wire.COMMAND_KEEP, command,
                                                           wire.COMMAND_KEEP])
                    self.assertEqual(expanded["recipients"][defense], [1])
                    assert_wire_legal(self, request, header, expanded)

    def test_separate_threats_keep_local_retreats_and_targets_while_fighters_stay_grouped(self):
        body, header = worker_fixture([(0, 0x20), (2, 5), (0, 0x20), (2, 5)])
        for name, _, _ in wire.TARGET_FIELDS:
            body[name].append(copy.deepcopy(body[name][0]))
        body["target_id"][1] += 0x1D0
        header = replace(header, target_rows=2)
        for row, local_points, targets in ((0, 1, 1), (2, 2, 2)):
            threaten(body, row, (wire.COMMAND_MOVE, wire.COMMAND_ATTACK_UNIT),
                     local_points=local_points, targets=targets)
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        self.assertEqual([(r.kind, r.members) for r in step.control_layout.rows],
                         [(squads.INDIVIDUAL, (0,)), (squads.SQUAD, (1, 3)),
                          (squads.INDIVIDUAL, (2,))])
        for command, arguments in ((wire.COMMAND_MOVE, (64, 65)),
                                   (wire.COMMAND_ATTACK_UNIT, (0, 1))):
            sample = blank_sample(step)
            sample["command"][[0, 2]] = command
            sample["argument"][[0, 2]] = torch.tensor(arguments)
            expanded = squads.expand_actions(step, sample)
            self.assertEqual(expanded["argument"], [arguments[0], -1, arguments[1], -1])
            assert_wire_legal(self, request, header, expanded)

    def test_worker_rejoins_after_threat_clears_and_active_tasks_stay_individual(self):
        body, header = worker_fixture([(0, 0x20)] * 3)
        body["own_source_state_bits"][2] |= wire.STATE_ACTIVE_ECONOMY_ORDER
        body["own_active_economy_candidate_row"][2] = 0
        calm = copy.deepcopy(body)
        threaten(body, 0, (wire.COMMAND_MOVE,))
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        self.assertTrue(any(row.kind == squads.INDIVIDUAL and row.members == (0,)
                            for row in step.control_layout.rows))
        request, header = parse(calm, header)
        step = ppo.step_from_request(request, header)
        self.assertEqual([row.members for row in step.control_layout.rows
                          if row.kind == squads.SQUAD], [(0, 1)])
        self.assertTrue(any(row.kind == squads.INDIVIDUAL and row.members == (2,)
                            for row in step.control_layout.rows))

    def test_defence_sampling_recompute_and_interleaved_economy_ledger(self):
        body, header = worker_fixture([(0, 0x20), (1, 0x80), (0, 0x20),
                                       (0, 0x20), (1, 0x80)])
        threaten(body, 2, (wire.COMMAND_MOVE, wire.COMMAND_ATTACK_UNIT), local_points=3)
        request, header = parse(body, header)
        torch.manual_seed(74)
        net = ppo.Entity2Net(hidden=16, issue_prior=0.8)
        seen_defence = set()
        for seed in range(16):
            step = ppo.step_from_request(request, header)
            sample = ppo.sample_actions(net, step, torch.Generator().manual_seed(seed))
            ppo.attach_sample(step, sample)
            recomputed = ppo.teacher_forced_logp(net, step, step.command, step.argument,
                                                step.assign, step.slot_command, step.slot_cell)
            self.assertTrue(recomputed["ok"])
            for key in ("logp", "assign_logp", "slot_logp"):
                self.assertTrue(torch.allclose(sample[key], recomputed[key], atol=2e-6), key)
            expanded = squads.expand_actions(step, sample)
            seen_defence.add(expanded["command"][2])
            replay = assert_wire_legal(self, request, header, expanded)
            for j, row in enumerate(step.control_layout.rows):
                if row.kind != squads.SQUAD:
                    self.assertEqual(step.budget_before[j].tolist(),
                                     list(replay["remaining_budget"][row.members[0]]))
        self.assertTrue(seen_defence & {wire.COMMAND_MOVE, wire.COMMAND_ATTACK_UNIT})


if __name__ == "__main__":
    unittest.main()
