"""Worker count, task dispatch, automatic actions, and training boundaries."""
import copy
from pathlib import Path
import tempfile
import unittest

import torch

import ranker_entity2_bc as bc
import ranker_entity2_contract as wire
import ranker_entity2_ppo as ppo
import ranker_entity2_squads as squads
from test_ranker_entity2_squads import fixture, parse, blank_sample, assert_wire_legal, outcome_for
from test_ranker_entity2_squad_bc import keep, record_for


class WorkerTaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_one_neural_worker_row_for_128_workers_and_no_harvest_output(self):
        body, header = fixture([(0, 0x20)] * 128)
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        self.assertEqual(step.u, 1)
        self.assertEqual(step.control_layout.rows[0].kind, squads.WORKER_TASK)
        net = ppo.Entity2Net(hidden=16)
        seen = []
        hook = net.own_encoder.register_forward_pre_hook(lambda _, args: seen.append(len(args[0])))
        net.encode(step)
        hook.remove()
        self.assertEqual(seen, [1])
        self.assertEqual(net.command_head.out_features, 8)
        self.assertNotIn(wire.COMMAND_HARVEST, ppo.POLICY_COMMANDS)
        self.assertEqual(torch.where(step.command_mask[0])[0].tolist(),
                         [wire.COMMAND_KEEP, wire.COMMAND_MOVE, wire.COMMAND_BUILD])
        expanded = squads.expand_actions(step, blank_sample(step))
        self.assertEqual(expanded["command"], [wire.COMMAND_HARVEST] * 128)
        self.assertEqual(squads.published_decisions(outcome_for(expanded), expanded), 0)
        assert_wire_legal(self, request, header, expanded)

    def test_build_selects_one_capable_harvester_when_idle_worker_cannot_build(self):
        body, header = fixture([(0, 0x20)] * 3)
        body["command_mask"][0] &= ~(1 << wire.COMMAND_BUILD)
        for i in (1, 2):
            body["own_semantic_order"][i] = wire.SEMANTIC_HARVEST
            body["own_order_status"][i] = 2
            body["own_active_economy_candidate_row"][i] = 0
            body["own_source_state_bits"][i] |= wire.STATE_ACTIVE_ECONOMY_ORDER | wire.STATE_CARGO_NONZERO
            body["command_mask"][i] &= ~(1 << wire.COMMAND_HARVEST)
        body["own_feature"][2][0:2] = body["candidates"][1].feature[0:2]
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        self.assertEqual(step.control_layout.rows[0].build_sources[1], 2)
        sample = blank_sample(step)
        sample["command"][0], sample["argument"][0] = wire.COMMAND_BUILD, 1
        expanded = squads.expand_actions(step, sample)
        self.assertEqual(expanded["command"], [wire.COMMAND_HARVEST, 0, wire.COMMAND_BUILD])
        self.assertEqual(expanded["recipients"], [[2]])
        assert_wire_legal(self, request, header, expanded)
        outcome = outcome_for(expanded)
        outcome["result"][0] = wire.RESULT_REJECTED_STALE
        self.assertEqual(squads.reduce_outcome(outcome, expanded)[0].tolist(), [True])
        self.assertEqual(squads.published_decisions(outcome, expanded), 1)
        outcome["result"][2] = wire.RESULT_REJECTED_STALE
        self.assertEqual(squads.reduce_outcome(outcome, expanded)[0].tolist(), [False])

    def test_scout_is_one_worker_then_locked_until_finished_then_harvests(self):
        body, header = fixture([(0, 0x20)] * 5)
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        sample = blank_sample(step)
        sample["command"][0], sample["argument"][0] = wire.COMMAND_MOVE, 9
        expanded = squads.expand_actions(step, sample)
        self.assertEqual(expanded["command"].count(wire.COMMAND_MOVE), 1)
        source = expanded["command"].index(wire.COMMAND_MOVE)
        self.assertEqual(expanded["command"].count(wire.COMMAND_HARVEST), 4)
        assert_wire_legal(self, request, header, expanded)
        body["own_semantic_order"][source] = wire.SEMANTIC_MOVE
        body["own_order_status"][source] = 2
        body["command_mask"][source] = 1  # C++ active task lock
        request, header = parse(body, header)
        busy = ppo.step_from_request(request, header)
        self.assertFalse(bool(busy.command_mask[0, wire.COMMAND_MOVE]))
        self.assertNotIn(source, busy.control_layout.rows[0].build_sources)
        self.assertEqual(squads.expand_actions(busy, blank_sample(busy))["command"][source], 0)
        body["own_order_status"][source] = 3
        body["command_mask"][source] = 1 | (1 << wire.COMMAND_MOVE) | (1 << wire.COMMAND_HARVEST)
        request, header = parse(body, header)
        idle = ppo.step_from_request(request, header)
        self.assertEqual(squads.expand_actions(idle, blank_sample(idle))["command"][source],
                         wire.COMMAND_HARVEST)

    def test_task_keep_and_automatic_failure_do_not_train_an_automatic_action(self):
        body, header = fixture([(0, 0x20)] * 2)
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        expanded = squads.expand_actions(step, blank_sample(step))
        outcome = outcome_for(expanded)
        outcome["trainable"] = [False, False]
        outcome["result"] = [wire.RESULT_REJECTED_MASK, wire.RESULT_REJECTED_STALE]
        self.assertEqual(squads.reduce_outcome(outcome, expanded)[0].tolist(), [True])
        self.assertEqual(squads.published_decisions(outcome, expanded), 0)
        sample = blank_sample(step)
        sample["command"][0] = wire.COMMAND_HARVEST
        sample["argument"][0] = 0
        with self.assertRaisesRegex(ValueError, "no legal source"):
            squads.expand_actions(step, sample)

    def test_unknown_reserved_or_building_worker_is_not_treated_as_idle(self):
        body, header = fixture([(0, 0x20)] * 3)
        body["own_semantic_order"][0] = wire.SEMANTIC_EXTERNAL_UNKNOWN
        body["own_source_state_bits"][1] |= wire.STATE_OUTSTANDING_RESERVATION
        body["own_walking_build_type_id"][2] = 0x82
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        self.assertFalse(bool(step.command_mask[0, 1:].any()))
        self.assertEqual(squads.expand_actions(step, blank_sample(step))["command"], [0, 0, 0])

    def test_bc_ignores_autoharvest_and_projects_only_one_task(self):
        body, header = fixture([(0, 0x20)] * 2)
        slots = [wire.ShadowSlotLabel() for _ in range(wire.SLOT_COUNT)]
        harvest = wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_HARVEST, 0, 0, 1.0)
        build = wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_BUILD, 0, 1, 1.0)
        item = bc.shadow_steps([record_for(body, header, [harvest, build], slots)])[0]
        self.assertEqual(item.step.u, 1)
        self.assertEqual((item.labels[0].label, item.labels[0].command, item.labels[0].argument),
                         (wire.SHADOW_ISSUE, wire.COMMAND_BUILD, 1))
        self.assertIsNotNone(ppo.bc_loss(ppo.Entity2Net(hidden=16), item.step, item.labels, item.slot_labels))
        item = bc.shadow_steps([record_for(body, header, [harvest, harvest], slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_KEEP)
        moves = [wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_MOVE, 0, cell, 1.0) for cell in (7, 9)]
        item = bc.shadow_steps([record_for(body, header, moves, slots)])[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)

    def test_v6_checkpoint_conversion_removes_exact_harvest_row(self):
        net = ppo.Entity2Net(hidden=16)
        state = copy.deepcopy(net.state_dict())
        state["command_head.weight"] = torch.arange(9 * 16, dtype=torch.float32).reshape(9, 16)
        state["command_head.bias"] = torch.arange(9, dtype=torch.float32)
        meta = dict(bc.CHECKPOINT_METADATA, architecture_id="entv6-type-squads-mlp",
                    entity_action_version=5, control_schema_id="type-squads-v1")
        meta.pop("policy_commands")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "old.pt"
            torch.save({"model": state, "metadata": meta, "hidden": 16,
                        "extra": {"optimizer": {"old": True}}}, path)
            with self.assertRaisesRegex(RuntimeError, "convert-squads-from"):
                bc.load_checkpoint(str(path))
            converted = bc.convert_squad_checkpoint(str(path))
            self.assertEqual(converted["net"].command_head.bias.tolist(), [0, 1, 2, 3, 4, 6, 7, 8])
            self.assertNotIn("optimizer", converted)

    def test_legacy_action_header_is_only_allowed_for_offline_shadow(self):
        _, header = fixture([(0, 0x20)])
        raw = bytearray(wire.pack_header(header))
        raw[30] = 5
        with self.assertRaises(wire.WireError):
            wire.parse_header(bytes(raw))
        self.assertEqual(wire.parse_header(bytes(raw), legacy_shadow=True), header)


if __name__ == "__main__":
    unittest.main()
