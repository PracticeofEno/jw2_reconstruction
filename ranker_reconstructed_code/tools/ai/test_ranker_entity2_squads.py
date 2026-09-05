"""Focused regression tests for the type-squad policy boundary.

Run: python -m unittest test_ranker_entity2_squads
Benchmark: python test_ranker_entity2_squads.py --benchmark
"""
from __future__ import annotations

import copy
from dataclasses import replace
import json
import math
from pathlib import Path
import statistics
import sys
import tempfile
import time
import unittest

import torch

import ranker_entity2_bc as bc
import ranker_entity2_contract as wire
import ranker_entity2_ppo as ppo
import ranker_entity2_squads as squads


def fixture(specs):
    """Build a real, serialized observation with arbitrary interleaved types.

    Specs are (template index: worker=0/building=1/fighter=2/scout=3, type id).
    """
    source, header = wire._slot_fixture_request()
    body = copy.deepcopy(source)
    fields = [name for name, _, _ in wire.OWN_PREFIX_FIELDS + wire.OWN_APPENDIX_FIELDS]
    fields += ["queue_slots", "attack_pair_mask_words", "economy_pair_mask_words"]
    for name in fields:
        body[name] = [copy.deepcopy(source[name][template]) for template, _ in specs]
    body["own_id"] = [0x1D0 * (i + 1) for i in range(len(specs))]
    body["own_type_id"] = [type_id for _, type_id in specs]
    body["own_slot_order_relation"] = [0] * len(specs)
    body["slots"] = [wire.SlotBlock() for _ in range(wire.SLOT_COUNT)]
    for i, (template, _) in enumerate(specs):
        body["own_feature"][i][0:3] = [0.1 + i / 4096.0, 0.2, 0.75]
        if template >= 2:
            body["command_mask"][i] = (1 << wire.COMMAND_HARVEST) - 1
            body["own_assign_mask"][i] = 0b1000 if template == 2 else 0b0001
        slot = body["own_slot_id"][i]
        if slot != wire.SLOT_NONE:
            body["slots"][slot].member_count += 1
    if any(template == 3 for template, _ in specs):
        body["own_assign_mask"] = [mask & ~(1 << wire.SLOT_SCOUT)
                                   for mask in body["own_assign_mask"]]
    return body, replace(header, own_rows=len(specs))


def parse(body, header):
    payload = wire.pack_act_request(body)
    header = replace(header, payload_bytes=len(payload), payload_crc32=wire.crc32(payload))
    return wire.parse_act_request(header, payload), header


def blank_sample(step):
    return {"command": torch.zeros(step.u, dtype=torch.long),
            "argument": torch.full((step.u,), -1, dtype=torch.long),
            "assign": torch.zeros(step.u, dtype=torch.long)}


def outcome_for(expanded):
    count = len(expanded["command"])
    return {"trainable": [True] * count, "assign_trainable": [True] * count,
            "result": [wire.RESULT_PUBLISHED if c else wire.RESULT_KEPT
                       for c in expanded["command"]],
            "slot_result": [wire.RESULT_KEPT] * wire.SLOT_COUNT,
            "slot_trainable": [False] * wire.SLOT_COUNT}


def assert_wire_legal(test, request, header, expanded):
    replay = wire.replay_ledger(request, header, expanded["command"], expanded["argument"],
                                assigns=expanded["assign"])
    test.assertTrue(all(replay["assign_legal"]))
    for i in range(header.own_rows):
        test.assertTrue(wire.command_legal(
            expanded["command"][i], expanded["argument"][i], replay["dynamic_command_mask"][i],
            replay["dynamic_economy_pair_mask_words"][i], request["point_mask"][i],
            request["attack_pair_mask_words"][i], request["candidates"]), (i, expanded))
    return replay


class SquadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_pooling_before_encoder_and_no_fixed_four_squad_limit(self):
        body, header = fixture([(2, 5 + i % 7) for i in range(140)])
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        self.assertEqual(step.u, 7)
        self.assertEqual(step.control_layout.wire_rows, 140)
        self.assertTrue(torch.equal(step.control_feat[:, 0], torch.full((7,), 20 / 16)))
        net = ppo.Entity2Net(hidden=32)
        seen = []
        hook = net.own_encoder.register_forward_pre_hook(lambda _, args: seen.append(args[0].shape[0]))
        enc = net.encode(step)
        hook.remove()
        self.assertEqual(seen, [7])
        self.assertEqual(enc["h_own"].shape[0], 7)
        sample = blank_sample(step)
        sample["command"][:] = wire.COMMAND_MOVE
        sample["argument"][:] = torch.arange(7)
        expanded = squads.expand_actions(step, sample)
        for i in range(140):
            self.assertEqual(expanded["argument"][i], i % 7)
        assert_wire_legal(self, request, header, expanded)
        # Shared commands and anti-churn penalty are counted seven times,
        # not once for each of the 140 resulting unit packets.
        self.assertEqual(squads.published_decisions(outcome_for(expanded), expanded), 7)

    def test_worker_tasks_protected_and_economy_overrides_only_one_source(self):
        body, header = fixture([(0, 0x20)] * 5 + [(1, 0x80)])
        body["own_source_state_bits"][1] |= wire.STATE_ACTIVE_ECONOMY_ORDER
        body["own_active_economy_candidate_row"][1] = 0
        body["own_walking_build_type_id"][2] = 0x82
        body["own_source_state_bits"][3] |= wire.STATE_CARGO_NONZERO
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        group = next(i for i, row in enumerate(step.control_layout.rows) if row.kind == squads.SQUAD)
        self.assertEqual(step.control_layout.rows[group].members, (0, 4))
        sample = blank_sample(step)
        sample["command"][group], sample["argument"][group] = wire.COMMAND_MOVE, 64
        economic = next(i for i, row in enumerate(step.control_layout.rows)
                        if row.kind == squads.ECONOMY and row.members == (4,))
        sample["command"][economic], sample["argument"][economic] = wire.COMMAND_BUILD, 1
        expanded = squads.expand_actions(step, sample)
        self.assertEqual(expanded["command"], [wire.COMMAND_MOVE, 0, 0, 0, wire.COMMAND_BUILD, 0])
        self.assertEqual(expanded["recipients"][group], [0])
        assert_wire_legal(self, request, header, expanded)
        self.assertEqual(squads.published_decisions(outcome_for(expanded), expanded), 2)

    def test_shared_legality_intersections_and_no_empty_pointer_sampling(self):
        body, header = fixture([(2, 5), (2, 5)])
        body["point_mask"] = [[1, 0, 0], [2, 0, 0]]
        body["attack_pair_mask_words"][1] = [0]
        body["command_mask"][1] &= ~(1 << wire.COMMAND_ATTACK_UNIT)
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        self.assertFalse(bool(step.point_mask.any()))
        self.assertFalse(bool(step.command_mask[0, list(wire.POINT_COMMANDS)].any()))
        self.assertFalse(bool(step.command_mask[0, wire.COMMAND_ATTACK_UNIT]))
        net = ppo.Entity2Net(hidden=16)
        sample = ppo.sample_actions(net, step, torch.Generator().manual_seed(1))
        self.assertTrue(torch.isfinite(sample["logp"]).all())
        assert_wire_legal(self, request, header, squads.expand_actions(step, sample))

    def test_one_scout_detaches_and_rejoins_on_new_snapshot(self):
        body, header = fixture([(2, 5), (2, 5), (2, 6), (2, 6)])
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        sample = blank_sample(step)
        sample["assign"][0] = wire.SLOT_SCOUT + 1
        sample["command"][:] = wire.COMMAND_MOVE
        sample["argument"][:] = 9
        expanded = squads.expand_actions(step, sample)
        self.assertEqual(expanded["assign"], [4, 0, 0, 0])
        self.assertEqual(expanded["command"], [0, 1, 1, 1])
        assert_wire_legal(self, request, header, expanded)
        body["own_slot_id"][0] = wire.SLOT_SCOUT
        body["slots"][wire.SLOT_SCOUT].member_count = 1
        body["slots"][wire.SLOT_MAIN].member_count -= 1
        body["own_assign_mask"] = [1, 0, 0, 0]
        request, header = parse(body, header)
        scout_step = ppo.step_from_request(request, header)
        self.assertEqual(scout_step.u, 3)
        self.assertEqual(scout_step.control_layout.rows[0].kind, squads.INDIVIDUAL)
        self.assertTrue(bool(scout_step.assign_mask[0, wire.SLOT_MAIN]))
        body["own_slot_id"][0] = wire.SLOT_MAIN
        body["slots"][wire.SLOT_SCOUT].member_count = 0
        body["slots"][wire.SLOT_MAIN].member_count += 1
        body["own_generation"][1] += 1    # reused activation, no persistent member cache
        request, header = parse(body, header)
        self.assertEqual(ppo.step_from_request(request, header).u, 2)

    def test_sample_logp_recompute_and_canonical_economy_ledger(self):
        body, header = fixture([(2, 5), (0, 0x20), (2, 6), (1, 0x80),
                                (2, 5), (0, 0x20), (2, 6), (1, 0x80)])
        request, header = parse(body, header)
        net = ppo.Entity2Net(hidden=32, issue_prior=0.8, assign_keep_prior=0.1)
        for seed in range(24):
            step = ppo.step_from_request(request, header)
            sample = ppo.sample_actions(net, step, torch.Generator().manual_seed(seed))
            ppo.attach_sample(step, sample)
            recomputed = ppo.teacher_forced_logp(net, step, step.command, step.argument,
                                                step.assign, step.slot_command, step.slot_cell)
            self.assertTrue(recomputed["ok"])
            for key in ("logp", "assign_logp", "slot_logp"):
                self.assertTrue(torch.allclose(sample[key], recomputed[key], atol=2e-6), key)
            expanded = squads.expand_actions(step, sample)
            replay = assert_wire_legal(self, request, header, expanded)
            self.assertLessEqual(sum(a == wire.SLOT_SCOUT + 1 for a in expanded["assign"]), 1)
            for j, row in enumerate(step.control_layout.rows):
                if row.kind != squads.SQUAD:
                    self.assertEqual(tuple(step.budget_before[j].tolist()),
                                     tuple(replay["remaining_budget"][row.members[0]]))

    def test_outcome_reduces_once_and_rejects_partial_or_overridden_squad(self):
        body, header = fixture([(2, 5), (2, 5), (2, 5)])
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        sample = blank_sample(step)
        sample["command"][0], sample["argument"][0] = wire.COMMAND_ATTACK_UNIT, 0
        expanded = squads.expand_actions(step, sample)
        outcome = outcome_for(expanded)
        bits, _ = squads.reduce_outcome(outcome, expanded)
        self.assertEqual(bits.tolist(), [True])
        outcome["result"][1] = wire.RESULT_REJECTED_STALE
        self.assertEqual(squads.reduce_outcome(outcome, expanded)[0].tolist(), [False])
        body, header = fixture([(0, 0x20), (0, 0x20)])
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        sample = blank_sample(step)
        for j, row in enumerate(step.control_layout.rows):
            sample["command"][j] = wire.COMMAND_MOVE if row.kind == squads.SQUAD else wire.COMMAND_HARVEST
            sample["argument"][j] = 9 if row.kind == squads.SQUAD else 0
        expanded = squads.expand_actions(step, sample)
        bits, _ = squads.reduce_outcome(outcome_for(expanded), expanded)
        self.assertFalse(bool(bits[0]))   # both workers took individual tasks

    def test_compact_rollouts_train_and_invalid_member_does_not_multiply_loss(self):
        body, header = fixture([(2, 5), (2, 5), (0, 0x20), (1, 0x80)])
        request, header = parse(body, header)
        net = ppo.Entity2Net(hidden=16)
        steps = []
        rng = torch.Generator().manual_seed(71)
        for i in range(3):
            step = ppo.step_from_request(request, header)
            sample = ppo.sample_actions(net, step, rng)
            ppo.attach_sample(step, sample)
            expanded = squads.expand_actions(step, sample)
            outcome = outcome_for(expanded)
            if i == 1:
                outcome["result"][1] = wire.RESULT_REJECTED_STALE
            rows, assigns = squads.reduce_outcome(outcome, expanded)
            step.trainable &= rows
            step.assign_trainable &= assigns
            step.reward = float(i)
            step.terminal = i == 2
            steps.append(step)
        stats = ppo.ppo_update_batched_episodes(net, torch.optim.Adam(net.parameters(), lr=1e-3),
                                                [{"steps": steps, "final_value": 0.0}], epochs=1)
        self.assertEqual(stats["invalid_records"], 0)
        self.assertTrue(math.isfinite(stats["loss"]))

    def test_bc_consensus_and_conflict_exclusion(self):
        body, header = fixture([(2, 5), (2, 5)])
        request, header = parse(body, header)
        labels = [wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_MOVE, 0, 9, 1.0)
                  for _ in range(2)]
        def record():
            replay = wire.replay_ledger(request, header, [l.command for l in labels],
                                        [l.argument for l in labels])
            packed = wire.pack_shadow_record(header, wire.pack_act_request(body), labels,
                                             replay["dynamic_command_mask"], replay["remaining_budget"],
                                             replay["dynamic_economy_pair_mask_words"],
                                             replay["dynamic_assign_mask"])
            return list(wire.parse_shadow_records(packed))
        item = bc.shadow_steps(record())[0]
        self.assertEqual(item.step.u, 1)
        self.assertEqual(item.labels[0].label, wire.SHADOW_ISSUE)
        net = ppo.Entity2Net(hidden=16)
        self.assertIsNotNone(ppo.bc_loss(net, item.step, item.labels, item.slot_labels))
        labels[1].argument = 10
        item = bc.shadow_steps(record())[0]
        self.assertEqual(item.labels[0].label, wire.SHADOW_EXCLUDED)

    def test_checkpoint_conversion_is_explicit_and_resets_lineage(self):
        net = ppo.Entity2Net(hidden=16)
        old_state = {name: value.clone() for name, value in net.state_dict().items()}
        old_state["own_encoder.0.weight"] = old_state["own_encoder.0.weight"][:, :-squads.CONTROL_FEATURE_COUNT]
        metadata = dict(bc.CHECKPOINT_METADATA)
        metadata["architecture_id"] = "entv5-slot-hunt-mlp"
        metadata.pop("control_schema_id")
        metadata.pop("control_feature_count")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "old.pt"
            dest = Path(directory) / "squads.pt"
            torch.save({"model": old_state, "metadata": metadata, "hidden": 16,
                        "extra": {"updates": 12, "optimizer": {}}}, source)
            before = source.read_bytes()
            with self.assertRaisesRegex(RuntimeError, "convert-squads-from"):
                bc.load_checkpoint(str(source))
            converted = bc.convert_squad_checkpoint(str(source))
            bc.save_checkpoint(converted["net"], str(dest), extra={"converter": converted["mapping_id"]})
            loaded, payload = bc.load_checkpoint_payload(str(dest))
            self.assertNotIn("optimizer", payload["extra"])
            self.assertTrue(torch.equal(loaded.own_encoder[0].weight[:, :-6], old_state["own_encoder.0.weight"]))
            self.assertEqual(int(torch.count_nonzero(loaded.own_encoder[0].weight[:, -6:])), 0)
            self.assertEqual(before, source.read_bytes())

    def test_bc_scout_commands_are_not_mistaken_for_squad_orders(self):
        body, header = fixture([(2, 5), (2, 5)])
        request, header = parse(body, header)
        labels = [wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_MOVE, 0, 10, 1.0,
                                  wire.SHADOW_ISSUE, wire.SLOT_SCOUT + 1),
                  wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_MOVE, 0, 9, 1.0)]
        def projected():
            replay = wire.replay_ledger(request, header, [l.command for l in labels],
                                        [l.argument for l in labels], assigns=[l.assign for l in labels])
            packed = wire.pack_shadow_record(header, wire.pack_act_request(body), labels,
                                             replay["dynamic_command_mask"], replay["remaining_budget"],
                                             replay["dynamic_economy_pair_mask_words"],
                                             replay["dynamic_assign_mask"])
            return bc.shadow_steps(list(wire.parse_shadow_records(packed)))[0]
        item = projected()
        self.assertEqual(item.labels[0].argument, 9)
        self.assertEqual(item.labels[0].assign, wire.SLOT_SCOUT + 1)
        self.assertIsNotNone(ppo.bc_loss(ppo.Entity2Net(hidden=16), item.step,
                                       item.labels, item.slot_labels))
        labels[0].assign_label, labels[0].assign = wire.SHADOW_KEEP, 0
        labels[1].assign_label, labels[1].assign = wire.SHADOW_ISSUE, wire.SLOT_SCOUT + 1
        item = projected()
        self.assertEqual(item.labels[0].assign_label, wire.SHADOW_EXCLUDED)

    def test_empty_army_and_no_targets_or_candidates(self):
        body, header = fixture([])
        for name, _, _ in wire.TARGET_FIELDS:
            body[name] = []
        body["candidates"] = []
        header = replace(header, target_rows=0, resource_rows=0, build_rows=0,
                         produce_rows=0, research_rows=0)
        request, header = parse(body, header)
        step = ppo.step_from_request(request, header)
        sample = ppo.sample_actions(ppo.Entity2Net(hidden=16), step)
        self.assertEqual(step.u, 0)
        self.assertTrue(torch.isfinite(sample["value"]))
        self.assertEqual(squads.expand_actions(step, sample)["command"], [])

    def test_server_wire_outcome_join_across_changing_squad_members(self):
        from ranker_entity2_server import NetPolicy
        policy = NetPolicy("", hidden=16)
        hello = wire.HelloBody(controlled_owner_mask=2,
                               owners=[wire.HelloOwnerRecord(1, 1)])
        policy.hello(11, hello)
        try:
            for tick, specs in enumerate(([(2, 5), (2, 6), (2, 5), (2, 6), (2, 7), (2, 7)],
                                          [(2, 5), (2, 5), (2, 8)]), 1):
                body, header = fixture(specs)
                header = replace(header, policy_version=0, sequence=tick,
                                 reply_to_sequence=tick - 1, frame=8 * tick)
                request, header = parse(body, header)
                reply = wire.parse_reply(header, policy.act(11, header, request))
                self.assertEqual(len(reply["command"]), len(specs))
                assert_wire_legal(self, request, header, reply)
                key = policy._key(11, header)
                pending = policy.pending[key]
                self.assertEqual(pending["step"].u, 3 if tick == 1 else 2)
                outcome = outcome_for(reply)
                bad = dict(outcome, trainable=[True] * pending["step"].u)
                with self.assertRaisesRegex(wire.WireError, "row count"):
                    policy.outcome(11, header, bad)
                policy.outcome(11, header, outcome)
                self.assertEqual(pending["step"].trainable.shape[0], pending["step"].u)
                self.assertTrue(pending["outcome_received"])
            self.assertEqual(len(policy.episodes[key]), 1)
            self.assertEqual(policy.episodes[key][0].control_layout.wire_rows, 6)
        finally:
            policy.close()


def benchmark():
    torch.set_num_threads(1)
    torch.manual_seed(7)
    body, header = fixture([(0, 0x20)] * 12 + [(1, 0x80)] * 4 +
                            [(2, 5 + i % 4) for i in range(128)])
    request, header = parse(body, header)
    net = ppo.Entity2Net(hidden=128, issue_prior=0.3)
    results = {"wire_units": header.own_rows, "torch_threads": 1, "hidden": 128,
               "note": "synthetic CPU policy timings; excludes game/IPC, includes compaction in inference"}
    for grouped, name in ((False, "per_unit"), (True, "type_squads")):
        inference, train = [], []
        for iteration in range(13):
            started = time.perf_counter()
            step = ppo.step_from_request(request, header, grouped=grouped)
            sample = ppo.sample_actions(net, step, torch.Generator().manual_seed(iteration))
            if grouped:
                squads.expand_actions(step, sample)
            elapsed = (time.perf_counter() - started) * 1000
            ppo.attach_sample(step, sample)
            started = time.perf_counter()
            res = ppo.teacher_forced_logp(net, step, step.command, step.argument,
                                         step.assign, step.slot_command, step.slot_cell)
            assert res["ok"]
            loss = -(res["logp"].sum() + res["assign_logp"].sum() + res["slot_logp"].sum()) + res["enc"]["value"] ** 2
            net.zero_grad()
            loss.backward()
            training_elapsed = (time.perf_counter() - started) * 1000
            if iteration > 2:
                inference.append(elapsed)
                train.append(training_elapsed)
        results[name] = {"control_rows": step.u, "inference_median_ms": round(statistics.median(inference), 3),
                         "recompute_backward_median_ms": round(statistics.median(train), 3)}
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    if "--benchmark" in sys.argv:
        benchmark()
    else:
        unittest.main()
