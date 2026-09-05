# -*- coding: utf-8 -*-
"""Type squads at the policy boundary; ENTCMD02 remains an entity wire format.

Ordinary mobile commands have one decision per raw type id. Economic tasks
have a decision per source; worker defence and SCOUT stay individual. Only
the compact control rows enter the network, rollout and PPO/BC loss. The
layout lives for one snapshot, so spawning, death and id reuse cannot leave
stale members behind. C++ still validates every expanded source and packet.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Dict, List, Tuple, TYPE_CHECKING

import torch

import ranker_entity2_contract as wire

if TYPE_CHECKING:
    from ranker_entity2_ppo import EntityStep

CONTROL_SCHEMA_ID = "type-squads-v1"
CONTROL_FEATURE_COUNT = 6
SQUAD = "squad"
ECONOMY = "economy"
INDIVIDUAL = "individual"


@dataclass(frozen=True)
class ControlRow:
    kind: str
    members: Tuple[int, ...]       # original canonical wire row indices
    scout_source: int = -1         # one designated source, never a whole squad


@dataclass(frozen=True)
class SquadLayout:
    wire_rows: int
    rows: Tuple[ControlRow, ...]


def _on_individual_task(request: Dict, row: int) -> bool:
    state = request["own_source_state_bits"][row]
    return bool(state & (wire.STATE_ACTIVE_ECONOMY_ORDER |
                         wire.STATE_OUTSTANDING_RESERVATION |
                         wire.STATE_CARGO_NONZERO)) or \
        request["own_walking_build_type_id"][row] != wire.TYPE_SENTINEL or \
        request["own_active_economy_candidate_row"][row] >= 0


def _needs_worker_defence(request: Dict, row: int) -> bool:
    # C++ opens MOVE/ATTACK_UNIT for workers only when a nearby armed enemy
    # allows a local retreat or defence. Use its authoritative mask instead
    # of reimplementing threat geometry. Each worker needs its own row: even
    # two threatened workers can have disjoint local points/attack targets.
    return request["own_role"][row] == wire.ROLE_WORKER and bool(
        request["command_mask"][row] &
        ((1 << wire.COMMAND_MOVE) | (1 << wire.COMMAND_ATTACK_UNIT)))


def compact_step(step: EntityStep, request: Dict) -> EntityStep:
    """Pool BEFORE the own encoder, with intersections of member legality.

    Idle workers have a shared ordinary-command row plus an economy-only
    source row. An issued economy action overrides that worker's shared
    action. Busy or threatened workers keep their individual task/defence
    masks and never receive a squad order. Buildings and non-mobile context
    stay individual. Economy source order is unchanged, preserving the C++
    budget ledger.
    """
    if step.control_layout is not None:
        raise ValueError("step is already compacted")
    rows: List[ControlRow] = []
    groups: Dict[int, List[int]] = {}
    for i in range(step.u):
        role = request["own_role"][i]
        mobile = role in (wire.ROLE_MELEE, wire.ROLE_RANGED, wire.ROLE_WORKER)
        completed = bool(request["own_source_state_bits"][i] & wire.STATE_COMPLETED)
        scout = request["own_slot_id"][i] == wire.SLOT_SCOUT
        busy = _on_individual_task(request, i)
        defence = _needs_worker_defence(request, i)
        if not mobile or not completed or scout or busy or defence:
            rows.append(ControlRow(INDIVIDUAL, (i,)))
            continue
        groups.setdefault(request["own_type_id"][i], []).append(i)
        if bool(step.command_mask[i, wire.COMMAND_HARVEST:].any()):
            rows.append(ControlRow(ECONOMY, (i,)))
    for members in groups.values():
        scout_source = next((i for i in members
                             if bool(step.assign_mask[i, wire.SLOT_SCOUT])), -1)
        rows.append(ControlRow(SQUAD, tuple(members), scout_source))
    rows.sort(key=lambda row: (row.members[0], row.kind != SQUAD))
    layout = SquadLayout(step.u, tuple(rows))
    first = torch.tensor([row.members[0] for row in rows], dtype=torch.long)
    fields = ("own_cat", "own_feat", "own_role", "active_cand_row", "command_mask",
              "point_mask", "pair_mask", "econ_mask", "own_slot", "own_relation",
              "assign_mask")
    values = {name: getattr(step, name)[first].clone() for name in fields}
    summary = torch.zeros(len(rows), CONTROL_FEATURE_COUNT)
    for j, row in enumerate(rows):
        members = list(row.members)
        feat = step.own_feat[members]
        # count, min HP, x/y spread, squad flag, economy-only flag. Count is
        # deliberately not clipped: 100 and 1000 units must remain distinct.
        summary[j] = torch.tensor([len(members) / 16.0, float(feat[:, 2].min()),
                                   float(feat[:, 0].max() - feat[:, 0].min()),
                                   float(feat[:, 1].max() - feat[:, 1].min()),
                                   float(row.kind == SQUAD), float(row.kind == ECONOMY)])
        values["assign_mask"][j] = False
        if row.kind == SQUAD:
            values["own_feat"][j] = feat.mean(0)
            values["command_mask"][j] = step.command_mask[members].all(0)
            values["command_mask"][j, wire.COMMAND_HARVEST:] = False
            values["point_mask"][j] = step.point_mask[members].all(0)
            values["pair_mask"][j] = step.pair_mask[members].all(0)
            values["econ_mask"][j] = False
            values["active_cand_row"][j] = -1
            # Type squads have their own orders, independent of the old
            # MAIN/RAID commanders. SCOUT is the only persistent slot used.
            values["own_slot"][j] = wire.SLOT_COUNT
            values["own_relation"][j] = wire.SLOT_RELATION_NONE
            values["assign_mask"][j, wire.SLOT_SCOUT] = row.scout_source >= 0
            if not bool(values["point_mask"][j].any()):
                values["command_mask"][j, list(wire.POINT_COMMANDS)] = False
            if not bool(values["pair_mask"][j].any()):
                values["command_mask"][j, wire.COMMAND_ATTACK_UNIT] = False
        elif row.kind == ECONOMY:
            values["command_mask"][j, 1:wire.COMMAND_HARVEST] = False
            values["point_mask"][j] = False
            values["pair_mask"][j] = False
        elif request["own_slot_id"][members[0]] == wire.SLOT_SCOUT:
            # Returning to MAIN returns this unit to its type squad on the
            # next snapshot. Same-tick departure does not reopen SCOUT.
            values["assign_mask"][j, wire.SLOT_MAIN] = step.assign_mask[
                members[0], wire.SLOT_MAIN]
    slot_mask = step.slot_command_mask.clone()
    slot_mask[:wire.SLOT_SCOUT] = False
    slot_mask[:wire.SLOT_SCOUT, wire.SLOT_COMMAND_KEEP] = True
    return replace(step, **values, slot_command_mask=slot_mask,
                   control_layout=layout, control_feat=summary)


def expand_actions(step: EntityStep, sample: Dict) -> Dict:
    """Broadcast shared orders, then apply individual task overrides.

    Return recipient maps too: OUTCOME must train a squad decision once,
    and only for members that actually received it, excluding overrides.
    """
    layout = step.control_layout
    if layout is None:
        raise ValueError("wire expansion requires a squad layout")
    commands = [wire.COMMAND_KEEP] * layout.wire_rows
    arguments = [-1] * layout.wire_rows
    assigns = [0] * layout.wire_rows
    recipients = [[] for _ in layout.rows]
    assign_recipients = [[] for _ in layout.rows]
    command_owner = [-1] * layout.wire_rows
    # Assign first so a newly selected scout does not receive a squad order.
    new_scouts = set()
    for j, row in enumerate(layout.rows):
        assign = int(sample["assign"][j])
        source = row.scout_source if row.kind == SQUAD else row.members[0]
        if row.kind == SQUAD and assign not in (0, wire.SLOT_SCOUT + 1):
            raise ValueError("squads may only detach one scout")
        if source >= 0 and row.kind != ECONOMY:
            assign_recipients[j] = [source]
            assigns[source] = assign
            if assign == wire.SLOT_SCOUT + 1:
                new_scouts.add(source)
        elif assign:
            raise ValueError("assignment has no eligible source")
    for j, row in enumerate(layout.rows):
        if row.kind != SQUAD:
            continue
        command, argument = int(sample["command"][j]), int(sample["argument"][j])
        if command in wire.ECONOMY_COMMANDS:
            raise ValueError("economy commands cannot be broadcast")
        for source in row.members:
            if source in new_scouts:
                continue
            commands[source], arguments[source] = command, argument
            command_owner[source] = j
    for j, row in enumerate(layout.rows):
        if row.kind == SQUAD:
            continue
        source = row.members[0]
        command, argument = int(sample["command"][j]), int(sample["argument"][j])
        if row.kind == ECONOMY and command not in (wire.COMMAND_KEEP, *wire.ECONOMY_COMMANDS):
            raise ValueError("economy source attempted an ordinary command")
        if row.kind == ECONOMY and command == wire.COMMAND_KEEP:
            # This KEEP decision still means declining an economic task.
            recipients[j] = [source]
            continue
        commands[source], arguments[source] = command, argument
        command_owner[source] = j
    for source, owner in enumerate(command_owner):
        if owner >= 0:
            recipients[owner].append(source)
    return {"command": commands, "argument": arguments, "assign": assigns,
            "recipients": recipients, "assign_recipients": assign_recipients,
            "issued": [int(command) != wire.COMMAND_KEEP for command in sample["command"]]}


def reduce_outcome(outcome: Dict, expanded: Dict) -> Tuple[torch.Tensor, torch.Tensor]:
    """One training bit per decision, never a sum over copied unit orders.

    Require all recipients to accept a shared decision. A partial rejection
    must not be credited as successful execution of the entire squad order.
    """
    success = (wire.RESULT_KEPT, wire.RESULT_DEDUPED, wire.RESULT_PUBLISHED)
    row_bits = [bool(members) and all(outcome["trainable"][i] and
                                    outcome["result"][i] in success for i in members)
                for members in expanded["recipients"]]
    assign_bits = [bool(members) and all(outcome["assign_trainable"][i] for i in members)
                   for members in expanded["assign_recipients"]]
    return torch.tensor(row_bits, dtype=torch.bool), torch.tensor(assign_bits, dtype=torch.bool)


def published_decisions(outcome: Dict, expanded: Dict) -> int:
    """Churn penalty counts a broadcast once, independent of army size."""
    return sum(issued and any(outcome["result"][i] == wire.RESULT_PUBLISHED for i in members)
               for issued, members in zip(expanded["issued"], expanded["recipients"])) + \
        sum(outcome["slot_result"][s] == wire.RESULT_PUBLISHED
            for s in range(wire.SLOT_COUNT)) + \
        sum(any(expanded["assign"][i] and outcome["assign_trainable"][i]
                for i in members) for members in expanded["assign_recipients"])
