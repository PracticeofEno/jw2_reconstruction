"""Regression coverage for actual SHD3 slot ISSUE + personal KEEP labels."""
from __future__ import annotations

import copy
import unittest

import torch

import ranker_entity2_bc as bc
import ranker_entity2_contract as wire
import ranker_entity2_ppo as ppo
from test_ranker_entity2_squads import fixture, parse


def keep():
    return wire.ShadowLabel(wire.SHADOW_KEEP, wire.COMMAND_KEEP, 0, -1, 1.0)


def record_for(body, header, labels, slot_labels):
    request, header = parse(body, header)
    replay = wire.replay_ledger(
        request, header,
        [label.command if label.label == wire.SHADOW_ISSUE else 0 for label in labels],
        [label.argument if label.label == wire.SHADOW_ISSUE else -1 for label in labels],
        assigns=[label.assign if label.assign_label == wire.SHADOW_ISSUE else 0 for label in labels])
    data = wire.pack_shadow_record(
        header, wire.pack_act_request(body), labels, replay["dynamic_command_mask"],
        replay["remaining_budget"], replay["dynamic_economy_pair_mask_words"],
        replay["dynamic_assign_mask"], slot_labels)
    return list(wire.parse_shadow_records(data))[0]


class SquadTeacherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_commander_attack_move_trains_issue_instead_of_keep(self):
        body, header = fixture([(2, 5), (2, 5)])
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        slots[wire.SLOT_MAIN] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                    wire.SLOT_COMMAND_ATTACK_MOVE, 9)
        record = record_for(body, header, [keep(), keep()], slots)
        original = copy.deepcopy(record)
        item = bc.shadow_steps([record])[0]
        self.assertEqual(item.step.u, 1)
        self.assertEqual((item.labels[0].label, item.labels[0].command, item.labels[0].argument),
                         (wire.SHADOW_ISSUE, wire.COMMAND_ATTACK_MOVE, 9))
        self.assertEqual(item.slot_labels[wire.SLOT_MAIN].label, wire.SHADOW_EXCLUDED)
        net = ppo.Entity2Net(hidden=16)
        loss = ppo.bc_loss(net, item.step, item.labels, item.slot_labels)
        self.assertIsNotNone(loss)
        loss.backward()
        self.assertLess(float(net.gate_head.bias.grad[0]), 0.0,
                        "gradient descent must increase the probability of issuing the teacher's march")
        self.assertEqual(record, original)

    def test_point_and_hold_slot_commands_transfer_to_each_type(self):
        for slot in (wire.SLOT_MAIN, wire.SLOT_RAID_A, wire.SLOT_RAID_B):
            for slot_command, command, argument in (
                    (wire.SLOT_COMMAND_MOVE, wire.COMMAND_MOVE, 7),
                    (wire.SLOT_COMMAND_ATTACK_MOVE, wire.COMMAND_ATTACK_MOVE, 9),
                    (wire.SLOT_COMMAND_PATROL, wire.COMMAND_PATROL, 11),
                    (wire.SLOT_COMMAND_HOLD, wire.COMMAND_HOLD, -1)):
                with self.subTest(slot=slot, command=command):
                    body, header = fixture([(2, 5), (2, 6), (2, 5)])
                    body["own_slot_id"] = [slot] * 3
                    body["slots"] = [wire.SlotBlock() for _ in range(wire.SLOT_COUNT)]
                    body["slots"][slot].member_count = 3
                    slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
                    slots[slot] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE, slot_command, argument)
                    item = bc.shadow_steps([record_for(body, header, [keep()] * 3, slots)])[0]
                    self.assertEqual([(l.label, l.command, l.argument) for l in item.labels],
                                     [(wire.SHADOW_ISSUE, command, argument)] * 2)

    def test_same_tick_teacher_assignment_selects_destination_slot_order(self):
        body, header = fixture([(2, 5), (2, 5)])
        body["own_assign_mask"] = [(1 << wire.SLOT_RAID_A) | (1 << wire.SLOT_SCOUT)] * 2
        labels = [keep(), keep()]
        for label in labels:
            label.assign_label, label.assign = wire.SHADOW_ISSUE, wire.SLOT_RAID_A + 1
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        slots[wire.SLOT_RAID_A] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                     wire.SLOT_COMMAND_ATTACK_MOVE, 23)
        item = bc.shadow_steps([record_for(body, header, labels, slots)])[0]
        self.assertEqual((item.labels[0].label, item.labels[0].command, item.labels[0].argument),
                         (wire.SHADOW_ISSUE, wire.COMMAND_ATTACK_MOVE, 23))
        self.assertEqual(item.labels[0].assign_label, wire.SHADOW_EXCLUDED)

    def test_different_slot_goals_and_personal_overrides_are_not_averaged(self):
        body, header = fixture([(2, 5), (2, 5)])
        body["own_slot_id"][1] = wire.SLOT_RAID_A
        body["slots"][wire.SLOT_MAIN].member_count = 1
        body["slots"][wire.SLOT_RAID_A].member_count = 1
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        slots[wire.SLOT_MAIN] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                   wire.SLOT_COMMAND_ATTACK_MOVE, 9)
        slots[wire.SLOT_RAID_A] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                     wire.SLOT_COMMAND_ATTACK_MOVE, 10)
        item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)
        # Matching goals from different old slots can share one type command.
        slots[wire.SLOT_RAID_A].cell = 9
        item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
        self.assertEqual(item.labels[0].command, wire.COMMAND_ATTACK_MOVE)
        labels = [wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_ATTACK_UNIT, 0, 0, 1.0)] * 2
        item = bc.shadow_steps([record_for(body, header, labels, slots)])[0]
        self.assertEqual(item.labels[0].command, wire.COMMAND_ATTACK_UNIT)

    def test_active_legacy_order_with_closed_personal_mask_is_excluded_not_keep(self):
        body, header = fixture([(2, 5), (2, 5)])
        body["slots"][wire.SLOT_MAIN].active = 1
        body["slots"][wire.SLOT_MAIN].command = wire.SLOT_COMMAND_ATTACK_MOVE
        body["slots"][wire.SLOT_MAIN].cell = 9
        body["command_mask"] = [(1 << wire.COMMAND_KEEP) | (1 << wire.COMMAND_ATTACK_UNIT) |
                                 (1 << wire.COMMAND_HOLD)] * 2
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)
        self.assertEqual(item.labels[0].exclude_reason, wire.SHADOW_REASON_MASK_MISMATCH)
        self.assertFalse(bool(item.step.command_mask[0, wire.COMMAND_ATTACK_MOVE]))

    def test_unknown_or_unrepresentable_slot_orders_do_not_teach_keep(self):
        for label in (wire.ShadowSlotLabel(wire.SHADOW_EXCLUDED),
                      wire.ShadowSlotLabel(wire.SHADOW_ISSUE, wire.SLOT_COMMAND_STOP, -1),
                      wire.ShadowSlotLabel(wire.SHADOW_ISSUE, wire.SLOT_COMMAND_HUNT_NEUTRAL, -1)):
            with self.subTest(label=label):
                body, header = fixture([(2, 5), (2, 5)])
                slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
                slots[wire.SLOT_MAIN] = label
                item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
                self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)

    def test_repeated_personal_attack_keep_does_not_become_slot_hold(self):
        # C++ also emits KEEP for an unchanged personal shadow latch. Such a
        # member must keep attacking rather than learn to obey the slot HOLD.
        for status in (1, 2):  # awaiting_apply, active
            for slot_label in (wire.ShadowSlotLabel(),
                               wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                    wire.SLOT_COMMAND_MOVE, 9)):
                with self.subTest(status=status, slot_label=slot_label):
                    body, header = fixture([(2, 5), (2, 5)])
                    body["own_semantic_order"] = [wire.SEMANTIC_ATTACK_UNIT] * 2
                    body["own_order_status"] = [status] * 2
                    body["own_active_target_row"] = [0, 0]
                    body["own_slot_order_relation"] = [wire.SLOT_RELATION_DIFFERS] * 2
                    body["slots"][wire.SLOT_MAIN].active = 1
                    body["slots"][wire.SLOT_MAIN].command = wire.SLOT_COMMAND_HOLD
                    slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
                    slots[wire.SLOT_MAIN] = slot_label
                    item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
                    self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)

    def test_untracked_engine_order_is_not_assumed_to_follow_new_slot_goal(self):
        body, header = fixture([(2, 5), (2, 5)])
        body["own_semantic_order"] = [wire.SEMANTIC_EXTERNAL_UNKNOWN] * 2
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        slots[wire.SLOT_MAIN] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                    wire.SLOT_COMMAND_HOLD, -1)
        item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)

    def test_tracked_slot_order_can_transfer_but_old_slot_origin_does_not_prove_new_goal(self):
        body, header = fixture([(2, 5), (2, 5)])
        body["own_semantic_order"] = [wire.SEMANTIC_HOLD] * 2
        body["own_order_status"] = [2, 2]
        body["own_slot_order_relation"] = [wire.SLOT_RELATION_MATCH] * 2
        body["slots"][wire.SLOT_MAIN].active = 1
        body["slots"][wire.SLOT_MAIN].command = wire.SLOT_COMMAND_HOLD
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
        self.assertEqual((item.labels[0].label, item.labels[0].command),
                         (wire.SHADOW_ISSUE, wire.COMMAND_HOLD))
        # MATCH describes the snapshot's MAIN order, not the newly assigned RAID.
        body["own_assign_mask"] = [1 << wire.SLOT_RAID_A] * 2
        labels = [keep(), keep()]
        for label in labels:
            label.assign_label, label.assign = wire.SHADOW_ISSUE, wire.SLOT_RAID_A + 1
        slots[wire.SLOT_RAID_A] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                     wire.SLOT_COMMAND_ATTACK_MOVE, 9)
        item = bc.shadow_steps([record_for(body, header, labels, slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)

    def test_scout_commander_and_true_no_order_keep_are_preserved(self):
        body, header = fixture([(3, 5)])
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        slots[wire.SLOT_SCOUT] = wire.ShadowSlotLabel(wire.SHADOW_ISSUE,
                                                    wire.SLOT_COMMAND_MOVE, 9)
        item = bc.shadow_steps([record_for(body, header, [keep()], slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_KEEP)
        self.assertEqual(item.slot_labels[wire.SLOT_SCOUT].command, wire.SLOT_COMMAND_MOVE)
        body, header = fixture([(2, 5), (2, 5)])
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        item = bc.shadow_steps([record_for(body, header, [keep(), keep()], slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_KEEP)


if __name__ == "__main__":
    unittest.main()
