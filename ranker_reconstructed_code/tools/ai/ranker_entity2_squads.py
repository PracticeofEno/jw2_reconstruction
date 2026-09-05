# -*- coding: utf-8 -*-
"""Type squads at the policy boundary; ENTCMD02 remains an entity wire format.

Ordinary combat commands have one decision per raw type id. All workers
share one task dispatcher; harvesting and defence are deterministic. Only
the compact control rows enter the network, rollout and PPO/BC loss. The
layout lives for one snapshot, so spawning, death and id reuse cannot leave
stale members behind. C++ still validates every expanded source and packet.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Dict, List, Tuple, TYPE_CHECKING

import torch

import ranker_entity2_contract as wire
import ranker_entity2_workers as workers

if TYPE_CHECKING:
    from ranker_entity2_ppo import EntityStep

CONTROL_SCHEMA_ID = "type-squads-worker-tasks-v2"
CONTROL_FEATURE_COUNT = 6
SQUAD = "squad"
INDIVIDUAL = "individual"
WORKER_TASK = "worker_task"


@dataclass(frozen=True)
class ControlRow:
    kind: str
    members: Tuple[int, ...]       # original canonical wire row indices
    scout_source: int = -1         # one designated source, never a whole squad
    build_sources: Tuple[int, ...] = ()
    point_sources: Tuple[int, ...] = ()


@dataclass(frozen=True)
class SquadLayout:
    wire_rows: int
    rows: Tuple[ControlRow, ...]
    worker_commands: Tuple[Tuple[int, int, int], ...] = ()


def _on_individual_task(request: Dict, row: int) -> bool:
    state = request["own_source_state_bits"][row]
    return bool(state & (wire.STATE_ACTIVE_ECONOMY_ORDER |
                         wire.STATE_OUTSTANDING_RESERVATION |
                         wire.STATE_CARGO_NONZERO)) or \
        request["own_walking_build_type_id"][row] != wire.TYPE_SENTINEL or \
        request["own_active_economy_candidate_row"][row] >= 0


def compact_step(step: EntityStep, request: Dict) -> EntityStep:
    """Pool BEFORE the own encoder, with intersections of member legality.

    Workers contribute one aggregate task row. Each legal BUILD/global MOVE
    argument binds to one eligible source; task ISSUE overrides that source's
    automatic command. Buildings and non-mobile context stay individual.
    The policy ledger orders decisions, the wire ledger orders actual sources;
    both reserve the same feasible set of costs/sites.
    """
    if step.control_layout is not None:
        raise ValueError("step is already compacted")
    rows: List[ControlRow] = []
    groups: Dict[int, List[int]] = {}
    worker_members = []
    for i in range(step.u):
        role = request["own_role"][i]
        if role == wire.ROLE_WORKER:
            worker_members.append(i)
            continue
        mobile = role in (wire.ROLE_MELEE, wire.ROLE_RANGED)
        completed = bool(request["own_source_state_bits"][i] & wire.STATE_COMPLETED)
        scout = request["own_slot_id"][i] == wire.SLOT_SCOUT
        busy = _on_individual_task(request, i)
        if not mobile or not completed or scout or busy:
            rows.append(ControlRow(INDIVIDUAL, (i,)))
            continue
        groups.setdefault(request["own_type_id"][i], []).append(i)
    for members in groups.values():
        scout_source = next((i for i in members
                             if bool(step.assign_mask[i, wire.SLOT_SCOUT])), -1)
        rows.append(ControlRow(SQUAD, tuple(members), scout_source))
    if worker_members:
        build_sources, point_sources = workers.dispatch_sources(request, worker_members)
        rows.append(ControlRow(WORKER_TASK, tuple(worker_members),
                               build_sources=build_sources, point_sources=point_sources))
    rows.sort(key=lambda row: (row.members[0], row.kind != SQUAD))
    layout = SquadLayout(step.u, tuple(rows), workers.autopilot_commands(request, worker_members))
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
                                   float(row.kind == SQUAD), float(row.kind == WORKER_TASK)])
        values["assign_mask"][j] = False
        if row.kind == WORKER_TASK:
            values["own_feat"][j] = feat.mean(0)
            values["command_mask"][j] = False
            values["command_mask"][j, wire.COMMAND_KEEP] = True
            values["econ_mask"][j] = torch.tensor([i >= 0 for i in row.build_sources])
            values["point_mask"][j] = torch.tensor([i >= 0 for i in row.point_sources])
            values["command_mask"][j, wire.COMMAND_BUILD] = bool(values["econ_mask"][j].any())
            values["command_mask"][j, wire.COMMAND_MOVE] = bool(values["point_mask"][j].any())
            values["pair_mask"][j] = False
            values["active_cand_row"][j] = -1
            values["own_slot"][j] = wire.SLOT_COUNT
            values["own_relation"][j] = wire.SLOT_RELATION_NONE
        elif row.kind == SQUAD:
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
    for source, command, argument in layout.worker_commands:
        commands[source], arguments[source] = command, argument
    recipients = [[] for _ in layout.rows]
    assign_recipients = [[] for _ in layout.rows]
    command_owner = [-1] * layout.wire_rows
    # Assign first so a newly selected scout does not receive a squad order.
    new_scouts = set()
    for j, row in enumerate(layout.rows):
        assign = int(sample["assign"][j])
        if row.kind == WORKER_TASK:
            if assign:
                raise ValueError("worker tasks use a dispatcher, not slot assignment")
            continue
        source = row.scout_source if row.kind == SQUAD else row.members[0]
        if row.kind == SQUAD and assign not in (0, wire.SLOT_SCOUT + 1):
            raise ValueError("squads may only detach one scout")
        if source >= 0:
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
        if row.kind == WORKER_TASK:
            if command == wire.COMMAND_KEEP:
                # This is a real decision to dispatch nobody. Autopilot
                # actions are independent and must not masquerade as its ISSUE.
                recipients[j] = list(row.members)
                continue
            bindings = row.build_sources if command == wire.COMMAND_BUILD else \
                row.point_sources if command == wire.COMMAND_MOVE else ()
            if not 0 <= argument < len(bindings) or bindings[argument] < 0:
                raise ValueError("worker task has no legal source")
            source = bindings[argument]
        commands[source], arguments[source] = command, argument
        command_owner[source] = j
    for source, owner in enumerate(command_owner):
        if owner >= 0:
            recipients[owner].append(source)
    return {"command": commands, "argument": arguments, "assign": assigns,
            "recipients": recipients, "assign_recipients": assign_recipients,
            "worker_task_keeps": [row.kind == WORKER_TASK and
                                  int(sample["command"][j]) == wire.COMMAND_KEEP
                                  for j, row in enumerate(layout.rows)],
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
    # The dispatcher KEEP spends nothing and sends no policy command. Raw
    # worker masks can force an autopilot KEEP (trainable=False), or automatic
    # actions can reject; neither changes the validity of declining a task.
    for j, keep in enumerate(expanded.get("worker_task_keeps", ())):
        if keep:
            row_bits[j] = True
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
