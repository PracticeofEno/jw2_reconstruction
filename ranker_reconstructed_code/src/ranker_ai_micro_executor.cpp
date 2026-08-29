#include "ranker_ai_micro_executor.h"
#include "ranker_unit_commands.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ranker {
namespace {

constexpr u32 kAttackCommandBit = 5u;
constexpr u32 kHarvestCommandBit = 7u;
constexpr u32 kPlayerOwnerCount = 8u;
constexpr u32 kNeutralMonsterOwnerId = 8u;
constexpr u32 kMobileTypeLimit = 0x60u;
constexpr u32 kBuildingTypeStart = 0x80u;
constexpr i32 kDefaultSightPixels = 160;
constexpr i32 kMoveTolerancePixels = 32;

i64 squared_distance(i32 x0, i32 y0, i32 x1, i32 y1) {
    const i64 dx = static_cast<i64>(x0) - x1;
    const i64 dy = static_cast<i64>(y0) - y1;
    return dx * dx + dy * dy;
}

i64 squared(i64 value) {
    return value * value;
}

bool own_alive(const AiObservedUnit& unit) {
    return unit.controlled && unit.alive;
}

bool unit_has_command(const AiObservedUnit& unit, u32 bit) {
    return bit < 32u && (unit.type_flags & (1u << bit)) != 0;
}

bool unit_can_attack(const AiObservedUnit& unit) {
    return unit.alive && unit.type_id < kMobileTypeLimit &&
        unit_has_command(unit, kAttackCommandBit);
}

bool unit_can_harvest(const AiObservedUnit& unit) {
    return own_alive(unit) && unit.type_id < kMobileTypeLimit &&
        unit_has_command(unit, kHarvestCommandBit);
}

// Mirrors the scripted bot's positive idle definition: runtime idle states
// with an empty deferred queue.
bool unit_is_idle(const AiObservedUnit& unit) {
    if (!unit.controlled || !unit.alive || unit.under_construction ||
        unit.deferred_command_count != 0) {
        return false;
    }
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return state == 0u || state == kUnitStateRuntimeIdleAcquire;
}

bool unit_is_constructing(const AiObservedUnit& unit) {
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return state >= kUnitStateLegacySpawnPlacementStart &&
        state <= kUnitStateLegacySpawnPlacementApproach;
}

bool unit_is_harvesting(const AiObservedUnit& unit) {
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return (state >= kUnitStateWorkerApproachHarvest &&
            state <= kUnitStateWorkerHarvestFailed) ||
        (unit.command_flags & 4u) != 0;
}

bool is_hostile_visible_unit(const AiObservation& observation,
    const AiObservedUnit& unit) {
    if (!unit.visible || !unit.alive || unit.owner_id == observation.local_owner ||
        unit.owner_id >= kPlayerOwnerCount ||
        (observation.active_owner_mask & (1u << unit.owner_id)) == 0) {
        return false;
    }
    return (observation.local_relation_mask & (1u << unit.owner_id)) == 0;
}

bool is_neutral_monster(const AiObservedUnit& unit) {
    return unit.visible && unit.alive && !unit.controlled &&
        unit.owner_id == kNeutralMonsterOwnerId &&
        unit.type_id < kMobileTypeLimit;
}

i32 unit_sight(const AiObservedUnit& unit) {
    return unit.sight_range > 0 ? static_cast<i32>(unit.sight_range) :
        kDefaultSightPixels;
}

struct DesiredOrder {
    AiSemanticActionKind kind = AiSemanticActionKind::no_op;
    u32 target_id = 0;
    i32 x = 0;
    i32 y = 0;
};

DesiredOrder attack_order(u32 target_id) {
    DesiredOrder order;
    order.kind = AiSemanticActionKind::attack_unit;
    order.target_id = target_id;
    return order;
}

DesiredOrder point_order(AiSemanticActionKind kind, i32 x, i32 y) {
    DesiredOrder order;
    order.kind = kind;
    order.x = x;
    order.y = y;
    return order;
}

bool same_order(const AiMicroUnitRecord& record, const DesiredOrder& order) {
    if (record.last_kind != order.kind ||
        record.last_target_id != order.target_id) {
        return false;
    }
    return std::abs(record.last_x - order.x) <= kMoveTolerancePixels &&
        std::abs(record.last_y - order.y) <= kMoveTolerancePixels;
}

struct StepContext {
    const AiObservation& observation;
    const AiMicroExecutorConfig& config;
    u32 frame = 0;
    std::vector<const AiObservedUnit*> hostiles;   // visible enemy units
    std::vector<const AiObservedUnit*> neutrals;   // visible neutral monsters
    std::vector<UnitMovementPoint> bases;          // own base buildings
    // Per-frame target load for the melee spread rule (target id, count).
    std::vector<std::pair<u32, u32>> target_load;
};

u32 target_load_of(const StepContext& context, u32 target_id) {
    for (const std::pair<u32, u32>& entry : context.target_load) {
        if (entry.first == target_id) {
            return entry.second;
        }
    }
    return 0;
}

void bump_target_load(StepContext& context, u32 target_id) {
    for (std::pair<u32, u32>& entry : context.target_load) {
        if (entry.first == target_id) {
            ++entry.second;
            return;
        }
    }
    context.target_load.push_back({target_id, 1u});
}

const AiObservedUnit* find_unit(const AiObservation& observation, u32 id) {
    if (id == 0) {
        return nullptr;
    }
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.id == id) {
            return &unit;
        }
    }
    return nullptr;
}

UnitMovementPoint nearest_point(const std::vector<UnitMovementPoint>& points,
    i32 x, i32 y) {
    UnitMovementPoint best{-1, -1};
    i64 best_distance = 0;
    for (const UnitMovementPoint& point : points) {
        const i64 distance = squared_distance(x, y, point.x, point.y);
        if (best.x < 0 || distance < best_distance) {
            best = point;
            best_distance = distance;
        }
    }
    return best;
}

UnitMovementPoint clamp_to_map(const AiObservation& observation, i32 x, i32 y) {
    const i32 max_x = observation.map_width_tiles == 0 ? 0 :
        static_cast<i32>(observation.map_width_tiles * 32u) - 32;
    const i32 max_y = observation.map_height_tiles == 0 ? 0 :
        static_cast<i32>(observation.map_height_tiles * 32u) - 32;
    return {std::clamp(x, 32, std::max(max_x, 32)),
        std::clamp(y, 32, std::max(max_y, 32))};
}

// A hostile that can hurt this unit (or that this unit can hit) is inside its
// weapon envelope: in contact.  `scale` widens the envelope (hysteresis).
bool unit_in_contact(const StepContext& context, const AiObservedUnit& unit,
    i32 scale_numerator) {
    for (const AiObservedUnit* hostile : context.hostiles) {
        const i64 envelope = (static_cast<i64>(std::max(unit.attack_range,
            hostile->attack_range)) + context.config.contact_margin) *
            scale_numerator;
        if (squared_distance(unit.x, unit.y, hostile->x, hostile->y) <=
            squared(envelope)) {
            return true;
        }
    }
    return false;
}

// Focus-fire pick: keep the current target while it is valid (stability),
// else the lowest-health reachable candidate (mobile before building), with
// the melee spread cap so close fighters do not pile onto one target.
const AiObservedUnit* pick_target(StepContext& context,
    const AiObservedUnit& unit, AiMicroRole role,
    const std::vector<const AiObservedUnit*>& candidates, u32 current_target) {
    const i64 reach = static_cast<i64>(unit.attack_range) +
        (role == AiMicroRole::melee ? context.config.melee_reach :
            context.config.contact_margin);
    if (current_target != 0) {
        for (const AiObservedUnit* candidate : candidates) {
            if (candidate->id == current_target &&
                squared_distance(unit.x, unit.y, candidate->x, candidate->y) <=
                    squared(reach * 3 / 2)) {
                bump_target_load(context, candidate->id);
                return candidate;
            }
        }
    }
    const AiObservedUnit* best = nullptr;
    i64 best_distance = 0;
    for (const AiObservedUnit* candidate : candidates) {
        const i64 distance =
            squared_distance(unit.x, unit.y, candidate->x, candidate->y);
        if (distance > squared(reach)) {
            continue;
        }
        if (role == AiMicroRole::melee &&
            target_load_of(context, candidate->id) >=
                context.config.melee_per_target) {
            continue;
        }
        if (best == nullptr) {
            best = candidate;
            best_distance = distance;
            continue;
        }
        const bool best_building = best->type_id >= kMobileTypeLimit;
        const bool candidate_building = candidate->type_id >= kMobileTypeLimit;
        bool better = false;
        if (candidate_building != best_building) {
            better = !candidate_building;
        } else if (candidate->health != best->health) {
            better = candidate->health < best->health;
        } else if (distance != best_distance) {
            better = distance < best_distance;
        } else {
            better = candidate->id < best->id;
        }
        if (better) {
            best = candidate;
            best_distance = distance;
        }
    }
    if (best != nullptr) {
        bump_target_load(context, best->id);
    }
    return best;
}

const AiObservedUnit* nearest_visible_target(const StepContext& context,
    i32 x, i32 y, bool include_neutral) {
    const AiObservedUnit* best = nullptr;
    i64 best_distance = 0;
    const auto consider = [&](const AiObservedUnit* candidate) {
        const i64 distance = squared_distance(x, y, candidate->x, candidate->y);
        if (best == nullptr || distance < best_distance ||
            (distance == best_distance && candidate->id < best->id)) {
            best = candidate;
            best_distance = distance;
        }
    };
    for (const AiObservedUnit* hostile : context.hostiles) {
        consider(hostile);
    }
    if (include_neutral) {
        for (const AiObservedUnit* neutral : context.neutrals) {
            consider(neutral);
        }
    }
    return best;
}

// Nearest berry tile to (x, y): passable explored terrain with a remembered
// amount (fog-honest observation semantics).
UnitMovementPoint nearest_resource_tile(const AiObservation& observation,
    i32 x, i32 y) {
    UnitMovementPoint best{-1, -1};
    i64 best_distance = 0;
    if (observation.map_width_tiles == 0 ||
        observation.tiles.size() != static_cast<std::size_t>(
            observation.map_width_tiles) * observation.map_height_tiles) {
        return best;
    }
    for (u32 tile_index = 0; tile_index < observation.tiles.size();
         ++tile_index) {
        const AiObservedMapTile& tile = observation.tiles[tile_index];
        // The planner's harvest gate is the explored projection: an amount
        // on an unexplored tile is not orderable (target_not_visible).
        if (!tile.passable || !tile.explored || tile.resource_amount == 0) {
            continue;
        }
        const u32 tile_x = tile_index % observation.map_width_tiles;
        const u32 tile_y = tile_index / observation.map_width_tiles;
        const UnitMovementPoint point{
            static_cast<i32>(tile_x * 32u + 16u),
            static_cast<i32>(tile_y * 32u + 16u)};
        const i64 distance = squared_distance(x, y, point.x, point.y);
        if (best.x < 0 || distance < best_distance) {
            best = point;
            best_distance = distance;
        }
    }
    return best;
}

bool any_resource_tile(const AiObservation& observation) {
    for (const AiObservedMapTile& tile : observation.tiles) {
        if (tile.passable && tile.explored && tile.resource_amount != 0) {
            return true;
        }
    }
    return false;
}

AiMicroUnitRecord* find_record(AiMicroExecutorState& state, u32 unit_id) {
    auto it = std::lower_bound(state.units.begin(), state.units.end(), unit_id,
        [](const AiMicroUnitRecord& record, u32 id) {
            return record.unit_id < id;
        });
    if (it == state.units.end() || it->unit_id != unit_id) {
        return nullptr;
    }
    return &*it;
}

AiMicroUnitRecord& ensure_record(AiMicroExecutorState& state, u32 unit_id,
    AiMicroGroup group, u32 frame) {
    auto it = std::lower_bound(state.units.begin(), state.units.end(), unit_id,
        [](const AiMicroUnitRecord& record, u32 id) {
            return record.unit_id < id;
        });
    if (it != state.units.end() && it->unit_id == unit_id) {
        return *it;
    }
    AiMicroUnitRecord record{};
    record.unit_id = unit_id;
    record.group = group;
    record.state_since_frame = frame;
    return *state.units.insert(it, record);
}

AiMicroGroup default_group(AiMicroRole role) {
    return role == AiMicroRole::worker ? AiMicroGroup::economy :
        AiMicroGroup::army;
}

AiMicroObjective make_defend(UnitMovementPoint post, i32 radius, u32 frame) {
    AiMicroObjective objective;
    objective.kind = AiMicroObjectiveKind::defend;
    objective.target_x = post.x;
    objective.target_y = post.y;
    objective.radius = radius;
    objective.set_frame = frame;
    objective.assigned = true;
    return objective;
}

// ---- per-unit default behaviors -------------------------------------------

DesiredOrder fighter_order(StepContext& context, AiMicroExecutorState& state,
    AiMicroUnitRecord& record, const AiObservedUnit& unit, AiMicroRole role,
    const AiMicroObjective& objective) {
    const AiMicroExecutorConfig& config = context.config;
    const UnitMovementPoint nearest_base =
        nearest_point(context.bases, unit.x, unit.y);

    // Low-health pull-back (attack/defend only): leave contact toward the
    // nearest base, resume once the weapon envelopes are clear.
    if (record.state == AiMicroUnitState::pulling_back) {
        if (!unit_in_contact(context, unit, 2)) {
            record.state = AiMicroUnitState::normal;
            record.state_since_frame = context.frame;
        } else {
            return point_order(AiSemanticActionKind::move, nearest_base.x,
                nearest_base.y);
        }
    }
    if (objective.kind != AiMicroObjectiveKind::retreat &&
        unit.max_health != 0 &&
        static_cast<u64>(unit.health) * 100u <
            static_cast<u64>(unit.max_health) * config.low_health_percent &&
        unit_in_contact(context, unit, 1)) {
        record.state = AiMicroUnitState::pulling_back;
        record.state_since_frame = context.frame;
        return point_order(AiSemanticActionKind::move, nearest_base.x,
            nearest_base.y);
    }

    const u32 current_target =
        record.last_kind == AiSemanticActionKind::attack_unit ?
        record.last_target_id : 0u;

    switch (objective.kind) {
    case AiMicroObjectiveKind::attack: {
        std::vector<const AiObservedUnit*> candidates = context.hostiles;
        if (objective.include_neutral) {
            candidates.insert(candidates.end(), context.neutrals.begin(),
                context.neutrals.end());
            std::sort(candidates.begin(), candidates.end(),
                [](const AiObservedUnit* lhs, const AiObservedUnit* rhs) {
                    return lhs->id < rhs->id;
                });
        }
        if (const AiObservedUnit* target =
                pick_target(context, unit, role, candidates, current_target)) {
            return attack_order(target->id);
        }
        if (objective.target_unit_id != 0) {
            return attack_order(objective.target_unit_id);
        }
        if (objective.target_x >= 0 &&
            squared_distance(unit.x, unit.y, objective.target_x,
                objective.target_y) > squared(config.arrival_radius)) {
            return point_order(AiSemanticActionKind::attack_move,
                objective.target_x, objective.target_y);
        }
        return {};
    }
    case AiMicroObjectiveKind::defend: {
        const UnitMovementPoint post{objective.target_x, objective.target_y};
        // Bubble anchors: every own base for a real base defense, else just
        // the post (hold/patrol at the army's own spot).
        std::vector<UnitMovementPoint> anchors;
        if (objective.radius >= config.defend_radius) {
            anchors = context.bases;
        }
        if (post.x >= 0) {
            anchors.push_back(post);
        }
        std::vector<const AiObservedUnit*> bubble;
        for (const AiObservedUnit* hostile : context.hostiles) {
            for (const UnitMovementPoint& anchor : anchors) {
                if (squared_distance(anchor.x, anchor.y, hostile->x,
                        hostile->y) <= squared(objective.radius)) {
                    bubble.push_back(hostile);
                    break;
                }
            }
        }
        const UnitMovementPoint leash = nearest_point(anchors, unit.x, unit.y);
        if (leash.x >= 0 &&
            squared_distance(unit.x, unit.y, leash.x, leash.y) >
                squared(objective.radius) && post.x >= 0) {
            return point_order(AiSemanticActionKind::move, post.x, post.y);
        }
        if (const AiObservedUnit* target =
                pick_target(context, unit, role, bubble, current_target)) {
            return attack_order(target->id);
        }
        if (post.x >= 0 &&
            squared_distance(unit.x, unit.y, post.x, post.y) >
                squared(std::min<i64>(config.return_radius,
                    std::max<i64>(objective.radius, config.arrival_radius)))) {
            return point_order(AiSemanticActionKind::move, post.x, post.y);
        }
        return {};
    }
    case AiMicroObjectiveKind::retreat:
        if (objective.target_x >= 0 &&
            squared_distance(unit.x, unit.y, objective.target_x,
                objective.target_y) > squared(config.arrival_radius)) {
            return point_order(AiSemanticActionKind::move, objective.target_x,
                objective.target_y);
        }
        return {};
    default:
        return {};
    }
    (void)state;
}

DesiredOrder worker_order(StepContext& context, AiMicroExecutorState& state,
    AiMicroUnitRecord& record, const AiObservedUnit& unit,
    const AiMicroObjective& objective) {
    if (objective.kind != AiMicroObjectiveKind::harvest) {
        // No berries left: workers defend like everyone else.
        return fighter_order(context, state, record, unit, AiMicroRole::melee,
            objective);
    }
    const i64 sight = unit_sight(unit);
    const auto threat_within = [&](i64 radius) {
        for (const AiObservedUnit* hostile : context.hostiles) {
            if (hostile->type_id < kMobileTypeLimit &&
                unit_can_attack(*hostile) &&
                squared_distance(unit.x, unit.y, hostile->x, hostile->y) <=
                    squared(radius)) {
                return true;
            }
        }
        return false;
    };
    const UnitMovementPoint nearest_base =
        nearest_point(context.bases, unit.x, unit.y);
    if (record.state == AiMicroUnitState::fleeing) {
        if (!threat_within(sight * 3 / 2)) {
            record.state = AiMicroUnitState::normal;
            record.state_since_frame = context.frame;
        } else {
            return point_order(AiSemanticActionKind::move, nearest_base.x,
                nearest_base.y);
        }
    }
    if (threat_within(sight)) {
        record.state = AiMicroUnitState::fleeing;
        record.state_since_frame = context.frame;
        return point_order(AiSemanticActionKind::move, nearest_base.x,
            nearest_base.y);
    }
    if (unit_is_idle(unit) && unit_can_harvest(unit) &&
        !unit_is_constructing(unit) && !unit_is_harvesting(unit)) {
        // Berry nearest to the worker's nearest nest (round-trip time is what
        // decides harvest efficiency), not nearest to the worker.
        const UnitMovementPoint berry = nearest_resource_tile(
            context.observation, nearest_base.x, nearest_base.y);
        if (berry.x >= 0) {
            return point_order(AiSemanticActionKind::harvest, berry.x, berry.y);
        }
    }
    return {};
}

DesiredOrder scout_order(StepContext& context, AiMicroUnitRecord& record,
    const AiObservedUnit& unit, const AiMicroObjective& objective) {
    const i64 sight = unit_sight(unit);
    const AiObservedUnit* threat = nullptr;
    i64 threat_distance = 0;
    for (const AiObservedUnit* hostile : context.hostiles) {
        if (hostile->type_id >= kMobileTypeLimit) {
            continue;
        }
        const i64 distance =
            squared_distance(unit.x, unit.y, hostile->x, hostile->y);
        if (distance <= squared(sight * 3 / 2) &&
            (threat == nullptr || distance < threat_distance)) {
            threat = hostile;
            threat_distance = distance;
        }
    }
    if (threat != nullptr) {
        // Step away from the sighted hostile by half a sight radius; the next
        // frame re-evaluates so the scout keeps its distance.
        record.state = AiMicroUnitState::evading;
        record.state_since_frame = context.frame;
        double away_x = static_cast<double>(unit.x) - threat->x;
        double away_y = static_cast<double>(unit.y) - threat->y;
        const double length = std::sqrt(away_x * away_x + away_y * away_y);
        if (length < 1.0) {
            away_x = 1.0;
            away_y = 0.0;
        } else {
            away_x /= length;
            away_y /= length;
        }
        const double step = static_cast<double>(sight) / 2.0;
        const UnitMovementPoint destination = clamp_to_map(context.observation,
            unit.x + static_cast<i32>(away_x * step),
            unit.y + static_cast<i32>(away_y * step));
        return point_order(AiSemanticActionKind::move, destination.x,
            destination.y);
    }
    record.state = AiMicroUnitState::normal;
    if (objective.target_x >= 0 &&
        squared_distance(unit.x, unit.y, objective.target_x,
            objective.target_y) > squared(context.config.arrival_radius)) {
        return point_order(AiSemanticActionKind::move, objective.target_x,
            objective.target_y);
    }
    return {};
}

} // namespace

AiMicroRole AiMicroRoleOf(const AiObservedUnit& unit,
    const AiMicroExecutorConfig& config) {
    if (unit.type_id >= kMobileTypeLimit) {
        return AiMicroRole::building;
    }
    if (unit_has_command(unit, kHarvestCommandBit)) {
        return AiMicroRole::worker;
    }
    if (unit.transport_capacity > 0 && !unit_has_command(unit, kAttackCommandBit)) {
        return AiMicroRole::transport;
    }
    if (!unit_has_command(unit, kAttackCommandBit)) {
        return AiMicroRole::other;
    }
    // Range 0 with an attack command = a weapon the definition does not
    // describe (트윈 람포스, user-audited as ranged): treat as ranged.
    return unit.attack_range != 0 &&
        unit.attack_range <= config.melee_range_threshold ?
        AiMicroRole::melee : AiMicroRole::ranged;
}

void AiMicroReset(AiMicroExecutorState& state) {
    state = AiMicroExecutorState{};
}

void AiMicroSetObjective(AiMicroExecutorState& state, AiMicroGroup group,
    const AiMicroObjective& objective) {
    state.objectives[static_cast<std::size_t>(group)] = objective;
    state.objectives[static_cast<std::size_t>(group)].assigned = true;
}

const AiMicroObjective& AiMicroObjectiveOf(const AiMicroExecutorState& state,
    AiMicroGroup group) {
    return state.objectives[static_cast<std::size_t>(group)];
}

void AiMicroAssignGroup(AiMicroExecutorState& state, u32 unit_id,
    AiMicroGroup group) {
    AiMicroUnitRecord& record = ensure_record(state, unit_id, group, 0);
    if (record.group != group) {
        record.group = group;
        record.state = AiMicroUnitState::normal;
        // Force a fresh order under the new objective.
        record.last_kind = AiSemanticActionKind::no_op;
        record.last_target_id = 0;
    }
}

AiMicroGroup AiMicroGroupOf(AiMicroExecutorState& state,
    const AiObservedUnit& unit, const AiMicroExecutorConfig& config) {
    return ensure_record(state, unit.id,
        default_group(AiMicroRoleOf(unit, config)), 0).group;
}

void AiMicroHoldUnits(AiMicroExecutorState& state,
    const std::vector<u32>& unit_ids, u32 until_frame) {
    for (u32 unit_id : unit_ids) {
        if (AiMicroUnitRecord* record = find_record(state, unit_id)) {
            record->policy_hold_until_frame = until_frame;
            // The policy's order supersedes whatever we last issued.
            record->last_kind = AiSemanticActionKind::no_op;
            record->last_target_id = 0;
        }
    }
}

std::vector<const AiObservedUnit*> AiMicroGroupMembers(
    AiMicroExecutorState& state, const AiObservation& observation,
    AiMicroGroup group, const AiMicroExecutorConfig& config) {
    std::vector<const AiObservedUnit*> members;
    for (const AiObservedUnit& unit : observation.units) {
        if (!own_alive(unit) || unit.type_id >= kMobileTypeLimit ||
            unit.under_construction) {
            continue;
        }
        if (AiMicroGroupOf(state, unit, config) == group) {
            members.push_back(&unit);
        }
    }
    std::sort(members.begin(), members.end(),
        [](const AiObservedUnit* lhs, const AiObservedUnit* rhs) {
            return lhs->id < rhs->id;
        });
    return members;
}

UnitMovementPoint AiMicroNearestBase(const AiObservation& observation,
    i32 x, i32 y, const AiMicroExecutorConfig& config) {
    std::vector<UnitMovementPoint> bases;
    std::vector<UnitMovementPoint> buildings;
    for (const AiObservedUnit& unit : observation.units) {
        if (!own_alive(unit) || unit.type_id < kBuildingTypeStart ||
            unit.under_construction) {
            continue;
        }
        buildings.push_back({unit.x, unit.y});
        if (unit.type_id == config.base_type_id) {
            bases.push_back({unit.x, unit.y});
        }
    }
    UnitMovementPoint best = nearest_point(bases, x, y);
    if (best.x < 0) {
        best = nearest_point(buildings, x, y);
    }
    if (best.x < 0) {
        best = {std::max(observation.start_x, 0),
            std::max(observation.start_y, 0)};
    }
    return best;
}

bool AiMicroGroupCentroid(AiMicroExecutorState& state,
    const AiObservation& observation, AiMicroGroup group,
    UnitMovementPoint& centroid, const AiMicroExecutorConfig& config) {
    const std::vector<const AiObservedUnit*> members =
        AiMicroGroupMembers(state, observation, group, config);
    if (members.empty()) {
        return false;
    }
    i64 sum_x = 0;
    i64 sum_y = 0;
    for (const AiObservedUnit* member : members) {
        sum_x += member->x;
        sum_y += member->y;
    }
    centroid = {static_cast<i32>(sum_x / static_cast<i64>(members.size())),
        static_cast<i32>(sum_y / static_cast<i64>(members.size()))};
    return true;
}

std::vector<AiSemanticAction> AiMicroExecutorStep(AiMicroExecutorState& state,
    const AiObservation& observation, const AiMicroExecutorConfig& config) {
    std::vector<AiSemanticAction> actions;
    if (observation.game_ended) {
        return actions;
    }
    StepContext context{observation, config};
    context.frame = observation.simulation_frame;
    state.last_step_frame = context.frame;

    // ---- gather ----------------------------------------------------------
    std::vector<const AiObservedUnit*> own;
    for (const AiObservedUnit& unit : observation.units) {
        if (own_alive(unit) && unit.type_id < kMobileTypeLimit &&
            !unit.under_construction) {
            own.push_back(&unit);
        }
        if (own_alive(unit) && unit.type_id == config.base_type_id &&
            !unit.under_construction) {
            context.bases.push_back({unit.x, unit.y});
        }
        if (is_hostile_visible_unit(observation, unit)) {
            context.hostiles.push_back(&unit);
        } else if (is_neutral_monster(unit)) {
            context.neutrals.push_back(&unit);
        }
    }
    const auto by_id = [](const AiObservedUnit* lhs, const AiObservedUnit* rhs) {
        return lhs->id < rhs->id;
    };
    std::sort(own.begin(), own.end(), by_id);
    std::sort(context.hostiles.begin(), context.hostiles.end(), by_id);
    std::sort(context.neutrals.begin(), context.neutrals.end(), by_id);
    if (context.bases.empty()) {
        context.bases.push_back(AiMicroNearestBase(observation,
            std::max(observation.start_x, 0), std::max(observation.start_y, 0),
            config));
    }
    const UnitMovementPoint home = AiMicroNearestBase(observation,
        std::max(observation.start_x, 0), std::max(observation.start_y, 0),
        config);

    if (!state.initialized) {
        // Group defaults for whatever the policy has not assigned yet.
        state.initialized = true;
        AiMicroObjective& economy_default =
            state.objectives[static_cast<std::size_t>(AiMicroGroup::economy)];
        if (!economy_default.assigned) {
            economy_default = AiMicroObjective{};
            economy_default.kind = AiMicroObjectiveKind::harvest;
            economy_default.set_frame = context.frame;
            economy_default.assigned = true;
        }
        AiMicroObjective& army_default =
            state.objectives[static_cast<std::size_t>(AiMicroGroup::army)];
        if (!army_default.assigned) {
            army_default = make_defend(home, config.defend_radius, context.frame);
        }
        AiMicroObjective& scout_default =
            state.objectives[static_cast<std::size_t>(AiMicroGroup::scout)];
        if (!scout_default.assigned) {
            scout_default = AiMicroObjective{};
            scout_default.kind = AiMicroObjectiveKind::scout;
            scout_default.set_frame = context.frame;
            scout_default.assigned = true;
        }
    }

    // ---- sync unit records (drop the dead, register the new) --------------
    {
        std::vector<AiMicroUnitRecord> kept;
        kept.reserve(own.size());
        std::size_t record_index = 0;
        for (const AiObservedUnit* unit : own) {
            while (record_index < state.units.size() &&
                   state.units[record_index].unit_id < unit->id) {
                ++record_index;  // dead / gone
            }
            if (record_index < state.units.size() &&
                state.units[record_index].unit_id == unit->id) {
                kept.push_back(state.units[record_index]);
                ++record_index;
            } else {
                AiMicroUnitRecord record{};
                record.unit_id = unit->id;
                record.group = default_group(AiMicroRoleOf(*unit, config));
                record.state_since_frame = context.frame;
                kept.push_back(record);
            }
        }
        state.units = std::move(kept);
    }

    // ---- objective transitions (default behavior, not decisions) -----------
    AiMicroObjective& economy =
        state.objectives[static_cast<std::size_t>(AiMicroGroup::economy)];
    AiMicroObjective& army =
        state.objectives[static_cast<std::size_t>(AiMicroGroup::army)];
    const bool resources_known = any_resource_tile(observation);
    if (economy.kind == AiMicroObjectiveKind::harvest && !resources_known) {
        economy = make_defend(home, config.defend_radius, context.frame);
    } else if (economy.kind == AiMicroObjectiveKind::defend && resources_known) {
        AiMicroObjective harvest;
        harvest.kind = AiMicroObjectiveKind::harvest;
        harvest.set_frame = context.frame;
        harvest.assigned = true;
        economy = harvest;
    }
    if (army.kind == AiMicroObjectiveKind::retreat && army.target_x >= 0) {
        UnitMovementPoint centroid{};
        if (AiMicroGroupCentroid(state, observation, AiMicroGroup::army,
                centroid, config) &&
            squared_distance(centroid.x, centroid.y, army.target_x,
                army.target_y) <= squared(config.arrival_radius)) {
            army = make_defend({army.target_x, army.target_y},
                config.defend_radius, context.frame);
        }
    }
    if (army.kind == AiMicroObjectiveKind::attack && army.target_unit_id != 0) {
        const AiObservedUnit* target = find_unit(observation, army.target_unit_id);
        const bool valid = target != nullptr && target->alive && target->visible &&
            (is_hostile_visible_unit(observation, *target) ||
                (army.include_neutral && is_neutral_monster(*target)));
        if (!valid) {
            // Target gone: stay in attack, continue on the nearest visible
            // hostile; with nothing in sight, stand (the policy re-picks).
            UnitMovementPoint centroid{std::max(observation.start_x, 0),
                std::max(observation.start_y, 0)};
            AiMicroGroupCentroid(state, observation, AiMicroGroup::army,
                centroid, config);
            const AiObservedUnit* next = nearest_visible_target(context,
                centroid.x, centroid.y, army.include_neutral);
            army.target_unit_id = next != nullptr ? next->id : 0u;
            army.target_x = -1;
            army.target_y = -1;
            ++state.targets_retargeted;
        }
    }

    // ---- per-unit default behavior ---------------------------------------
    struct PendingOrder {
        u32 unit_id;
        DesiredOrder order;
    };
    std::vector<PendingOrder> pending;
    for (std::size_t index = 0; index < own.size(); ++index) {
        const AiObservedUnit& unit = *own[index];
        AiMicroUnitRecord& record = state.units[index];
        if (record.policy_hold_until_frame != 0 &&
            context.frame < record.policy_hold_until_frame) {
            continue;
        }
        const AiMicroRole role = AiMicroRoleOf(unit, config);
        if (role == AiMicroRole::transport || role == AiMicroRole::building ||
            role == AiMicroRole::other) {
            continue;  // carriers are the drop autopilot's
        }
        if (unit_is_constructing(unit)) {
            continue;
        }
        const AiMicroObjective& objective =
            state.objectives[static_cast<std::size_t>(record.group)];
        DesiredOrder order;
        if (record.group == AiMicroGroup::scout) {
            order = scout_order(context, record, unit, objective);
        } else if (role == AiMicroRole::worker) {
            order = worker_order(context, state, record, unit, objective);
        } else {
            order = fighter_order(context, state, record, unit, role, objective);
        }
        if (order.kind == AiSemanticActionKind::no_op) {
            continue;
        }
        if (same_order(record, order)) {
            // Already told it.  Re-issue only if the engine dropped the order
            // (unit idle) and the re-issue interval passed.
            if (!unit_is_idle(unit) ||
                (record.last_issue_frame != 0xffffffffu &&
                    context.frame - record.last_issue_frame <
                        config.reissue_interval_frames)) {
                continue;
            }
        }
        record.last_kind = order.kind;
        record.last_target_id = order.target_id;
        record.last_x = order.x;
        record.last_y = order.y;
        record.last_issue_frame = context.frame;
        ++state.orders_issued;
        pending.push_back({unit.id, order});
    }

    // ---- batch by (kind, target) into planner-sized actions ---------------
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const PendingOrder& head = pending[index];
        if (head.unit_id == 0) {
            continue;  // consumed
        }
        AiSemanticAction action{};
        action.kind = head.order.kind;
        action.target_unit_id = head.order.target_id;
        action.target_x = head.order.x;
        action.target_y = head.order.y;
        action.unit_ids.push_back(head.unit_id);
        // Harvest orders stay one unit each (per-tile assignment).
        if (head.order.kind != AiSemanticActionKind::harvest) {
            for (std::size_t other = index + 1; other < pending.size() &&
                 action.unit_ids.size() < kAiMaximumUnitsPerAction; ++other) {
                PendingOrder& candidate = pending[other];
                if (candidate.unit_id == 0 ||
                    candidate.order.kind != head.order.kind ||
                    candidate.order.target_id != head.order.target_id ||
                    candidate.order.x != head.order.x ||
                    candidate.order.y != head.order.y) {
                    continue;
                }
                action.unit_ids.push_back(candidate.unit_id);
                candidate.unit_id = 0;
            }
        }
        actions.push_back(std::move(action));
    }
    return actions;
}

} // namespace ranker
