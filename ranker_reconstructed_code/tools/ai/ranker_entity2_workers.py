"""Deterministic worker economy/defence and one task dispatcher per snapshot.

Only the dispatcher's BUILD or global MOVE (scouting) enters policy loss.
HARVEST and emergency responses remain legal ENTCMD02 engine commands, but
never become policy rows, probabilities, or actor/churn terms.
"""
from __future__ import annotations

import math
from collections import Counter

import ranker_entity2_contract as wire


def has_bit(words, index):
    return bool(words[index >> 5] & (1 << (index & 31)))


def tracking(request, source):
    return request["own_order_status"][source] in (1, 2)


def threatened(request, source):
    # C++ emits only local MOVE points under threat; calm task MOVE is global.
    mask = request["command_mask"][source]
    points = request["point_mask"][source]
    return bool(mask & (1 << wire.COMMAND_ATTACK_UNIT)) or bool(
        mask & (1 << wire.COMMAND_MOVE) and not (points[0] or points[1]) and points[2])


def distance(request, source, x, y):
    own = request["own_feature"][source]
    return (own[0] - x) ** 2 + (own[1] - y) ** 2


def task_available(request, source):
    state = request["own_source_state_bits"][source]
    if not state & wire.STATE_COMPLETED or state & wire.STATE_OUTSTANDING_RESERVATION:
        return False
    if request["own_walking_build_type_id"][source] != wire.TYPE_SENTINEL:
        return False
    if request["own_semantic_order"][source] == wire.SEMANTIC_EXTERNAL_UNKNOWN:
        return False
    if tracking(request, source) and request["own_semantic_order"][source] not in (
            wire.SEMANTIC_NONE, wire.SEMANTIC_HARVEST):
        return False
    return not threatened(request, source)


def dispatch_sources(request, workers):
    """Bind each legal task argument to one capable idle/harvesting worker.

    Bindings are computed before sampling and kept in the snapshot layout.
    Different worker capabilities/reachability are a union, never a vote.
    """
    available = [i for i in workers if task_available(request, i)]
    build_sources = [-1] * len(request["candidates"])
    point_sources = [-1] * wire.POINT_COUNT

    def choose(sources, x, y):
        # Prefer idle workers; within that class choose the nearest source.
        return min(sources, key=lambda i: (
            tracking(request, i) and request["own_semantic_order"][i] == wire.SEMANTIC_HARVEST,
            distance(request, i, x, y), i), default=-1)

    for c, candidate in enumerate(request["candidates"]):
        if candidate.kind != wire.CAND_BUILD_SITE:
            continue
        eligible = [i for i in available if
                    request["command_mask"][i] & (1 << wire.COMMAND_BUILD) and
                    has_bit(request["economy_pair_mask_words"][i], c)]
        build_sources[c] = choose(eligible, candidate.feature[0], candidate.feature[1])
    # An active worker scout completes its current trip before a replacement
    # is dispatched. Returning to idle automatically resumes harvesting.
    scouting = any(tracking(request, i) and
                   request["own_semantic_order"][i] == wire.SEMANTIC_MOVE for i in workers)
    if not scouting:
        for cell in range(wire.GLOBAL_CELL_COUNT):
            eligible = [i for i in available if
                        request["command_mask"][i] & (1 << wire.COMMAND_MOVE) and
                        has_bit(request["point_mask"][i], cell)]
            point_sources[cell] = choose(eligible, (cell % 8 + 0.5) / 8,
                                         (cell // 8 + 0.5) / 8)
    return tuple(build_sources), tuple(point_sources)


def autopilot_commands(request, workers):
    """Return cost-free commands, without running any neural worker heads."""
    result = []
    load = Counter(request["own_active_economy_candidate_row"][i] for i in workers)
    for source in workers:
        mask = request["command_mask"][source]
        command, argument = wire.COMMAND_KEEP, -1
        own = request["own_feature"][source]
        if threatened(request, source):
            enemies = [t for t, feat in enumerate(request["target_feature"])
                       if feat[8] > 0 and request["target_role"][t] == 0]
            # Masked attack targets are also authoritative threat evidence.
            attacks = [t for t in range(len(request["target_feature"]))
                       if has_bit(request["attack_pair_mask_words"][source], t)]
            enemies = enemies or attacks
            if enemies:
                nearest = min(enemies, key=lambda t: distance(
                    request, source, *request["target_feature"][t][:2]))
                enemy = request["target_feature"][nearest]
                away_x, away_y = own[0] - enemy[0], own[1] - enemy[1]
            else:
                away_x, away_y = -1.0, 0.0
            local = [p for p in range(64, wire.POINT_COUNT)
                     if has_bit(request["point_mask"][source], p)]
            if mask & (1 << wire.COMMAND_MOVE) and local:
                # Local geometry v1: E, SE, S, SW, W, NW, N, NE; four radii.
                def escape_score(p):
                    angle = (p - 64) % 8 * math.pi / 4
                    return (math.cos(angle) * away_x + math.sin(angle) * away_y,
                            (p - 64) // 8, -p)
                # Keep an already active escape whose destination lies away
                # from the threat, avoiding a fresh path request every tick.
                escaping = tracking(request, source) and \
                    request["own_semantic_order"][source] == wire.SEMANTIC_MOVE and \
                    own[17] * away_x + own[18] * away_y > 0
                if not escaping:
                    command, argument = wire.COMMAND_MOVE, max(local, key=escape_score)
            elif mask & (1 << wire.COMMAND_ATTACK_UNIT) and attacks:
                command = wire.COMMAND_ATTACK_UNIT
                argument = min(attacks, key=lambda t: distance(
                    request, source, *request["target_feature"][t][:2]))
        elif mask & (1 << wire.COMMAND_HARVEST) and task_available(request, source) and \
                not tracking(request, source):
            resources = [c for c, candidate in enumerate(request["candidates"])
                         if candidate.kind == wire.CAND_RESOURCE and
                         has_bit(request["economy_pair_mask_words"][source], c)]
            if resources:
                argument = min(resources, key=lambda c: (
                    distance(request, source, *request["candidates"][c].feature[:2]) *
                    (1 + max(load[c], 0)), c))
                command = wire.COMMAND_HARVEST
                load[argument] += 1
        result.append((source, command, argument))
    return tuple(result)
