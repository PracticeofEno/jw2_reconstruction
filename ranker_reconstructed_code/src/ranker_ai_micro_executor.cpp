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

// v10 threat measurement: durability times damage output.  Relative - both
// sides run through the same formula, so the scale cancels in comparisons.
// The +10 keeps a zero-OP unit (some monsters, workers) from scoring zero
// while it can still soak hits.
u64 unit_power(const AiObservedUnit& unit) {
    return static_cast<u64>(unit.health) *
        (10u + static_cast<u64>(unit.attack_power));
}

i32 unit_sight(const AiObservedUnit& unit) {
    return unit.sight_range > 0 ? static_cast<i32>(unit.sight_range) :
        kDefaultSightPixels;
}

// Item 1 - the engine's class gate.  `default_unit_action_can_target` rejects
// a target whose render class is missing from the attacker's damage-profile
// mask, and a rejected attack order leaves the unit idle.  The executor's
// idle re-issue rule then re-sends the same impossible order forever, so an
// army ordered onto, say, a flyer it cannot reach simply stops fighting.
// Every target pick must pass through here.
bool can_engage(const AiObservedUnit& attacker, const AiObservedUnit& target,
    const AiMicroExecutorConfig& config) {
    if (!unit_can_attack(attacker)) {
        return false;
    }
    (void)config;
    if (target.render_class >= 32u) {
        return true;  // engine treats an unknown class as unrestricted
    }
    return (attacker.attackable_class_mask & (1u << target.render_class)) != 0;
}

// Item 2 - the engine resolves a different range stat for a flying target
// (`action_range_base_vs_class3`), and both values already carry the
// variant/research/equipment modifiers for controlled units.
i32 engagement_range(const AiObservedUnit& attacker,
    const AiObservedUnit& target, const AiMicroExecutorConfig& config) {
    return static_cast<i32>(target.render_class == config.flying_render_class ?
        attacker.attack_range_vs_air : attacker.attack_range);
}

bool is_move_order_kind(AiSemanticActionKind kind) {
    return kind == AiSemanticActionKind::move ||
        kind == AiSemanticActionKind::attack_move ||
        kind == AiSemanticActionKind::pickup_move ||
        kind == AiSemanticActionKind::harvest;
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

// One harvestable map tile, gathered once per step (item 7 needs the whole
// candidate set, and scanning the map per idle worker was the executor's
// hottest loop).
struct ResourceTile {
    u32 tile_index = 0;
    UnitMovementPoint point{};
};

struct StepContext {
    const AiObservation& observation;
    const AiMicroExecutorConfig& config;
    u32 frame = 0;
    std::vector<const AiObservedUnit*> hostiles;   // visible enemy units
    std::vector<const AiObservedUnit*> neutrals;   // visible neutral monsters
    std::vector<UnitMovementPoint> bases;          // own base buildings
    std::vector<UnitMovementPoint> buildings;      // every own completed building
    // Remembered (fog) enemy building tiles, gathered only when the army's
    // objective needs them (attack / buildings_first march point).
    std::vector<UnitMovementPoint> remembered_buildings;
    // Per-frame target load for the melee spread rule (target id, count).
    std::vector<std::pair<u32, u32>> target_load;
    // Harvestable tiles (tile-index order) and how many workers each already
    // has assigned this frame.
    std::vector<ResourceTile> resource_tiles;
    std::vector<u32> resource_tile_load;
    // Per-group centroid (item 4) and the union of the group's weapon target
    // masks (item 1, used when re-picking a group target).
    std::array<UnitMovementPoint, kAiMicroGroupCount> centroid{};
    std::array<bool, kAiMicroGroupCount> centroid_valid{};
    // v10.3 cohesion anchor: the component-wise MEDIAN of the group.  The
    // mean let a tail of freshly produced units (walking up from the base)
    // drag the reference point backwards, so the marching front kept
    // stop-and-going (2026-09-01 user replay report); the median stays with
    // the main body and ignores the reinforcement tail.
    std::array<UnitMovementPoint, kAiMicroGroupCount> cohesion_anchor{};
    // v10.4: per-(group, wave) median anchor - cohesion applies WITHIN a
    // wave, so a staging tail can never stop a marching wave.
    struct WaveAnchor {
        u32 group;
        u32 wave;
        UnitMovementPoint anchor;
    };
    std::vector<WaveAnchor> wave_anchors;
    std::array<u32, kAiMicroGroupCount> group_attackable_mask{};
    // Diagnostics folded back into the executor state at the end of the step.
    u32 unattackable_skipped = 0;
    u32 cohesion_holds = 0;
    // v10 meat pickup: per-frame drop -> collector assignment (unit id, drop
    // position), built in one pre-pass.  One collector per drop; fighters
    // WITHOUT a held meat reserve are preferred (user directive), then the
    // nearest.  Replaces the old first-come claim in unit-id order.
    std::vector<std::pair<u32, UnitMovementPoint>> meat_assignments;
    u32 meat_orders = 0;
    // v10 hunt guard: monsters dropped from the hunt pick because the group's
    // ground fighters cannot reach them.
    u32 hunt_unreachable = 0;
    u32 scout_picks = 0;
    u32 search_picks = 0;
    u32 explore_picks = 0;
    u32 roam_picks = 0;
};

std::size_t resource_slot_of(const StepContext& context, u32 tile_index) {
    auto it = std::lower_bound(context.resource_tiles.begin(),
        context.resource_tiles.end(), tile_index,
        [](const ResourceTile& tile, u32 index) {
            return tile.tile_index < index;
        });
    if (it == context.resource_tiles.end() || it->tile_index != tile_index) {
        return context.resource_tiles.size();
    }
    return static_cast<std::size_t>(it - context.resource_tiles.begin());
}

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
        // Only a hostile that can actually trade with this unit counts: an
        // anti-ground weapon standing next to a flyer is not contact.
        const bool we_reach = can_engage(unit, *hostile, context.config);
        const bool they_reach = can_engage(*hostile, unit, context.config);
        if (!we_reach && !they_reach) {
            continue;
        }
        const i64 our_range = we_reach ?
            engagement_range(unit, *hostile, context.config) : 0;
        const i64 their_range = they_reach ?
            engagement_range(*hostile, unit, context.config) : 0;
        const i64 envelope = (std::max(our_range, their_range) +
            context.config.contact_margin) * scale_numerator;
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
// `prefer_buildings` is the attack tactic's class priority inside reach
// (buildings_first); defend and units_first hit the enemy army first.
const AiObservedUnit* pick_target(StepContext& context,
    const AiObservedUnit& unit, AiMicroRole role,
    const std::vector<const AiObservedUnit*>& candidates, u32 current_target,
    bool prefer_buildings = false) {
    // Reach is per target: the weapon range against a flyer is a different
    // stat from the one against a ground target.
    const auto reach_to = [&](const AiObservedUnit& target) {
        return static_cast<i64>(engagement_range(unit, target, context.config)) +
            (role == AiMicroRole::melee ? context.config.melee_reach :
                context.config.contact_margin);
    };
    if (current_target != 0) {
        for (const AiObservedUnit* candidate : candidates) {
            if (candidate->id == current_target &&
                can_engage(unit, *candidate, context.config) &&
                squared_distance(unit.x, unit.y, candidate->x, candidate->y) <=
                    squared(reach_to(*candidate) * 3 / 2)) {
                bump_target_load(context, candidate->id);
                return candidate;
            }
        }
    }
    const AiObservedUnit* best = nullptr;
    i64 best_distance = 0;
    for (const AiObservedUnit* candidate : candidates) {
        if (!can_engage(unit, *candidate, context.config)) {
            ++context.unattackable_skipped;
            continue;
        }
        const i64 distance =
            squared_distance(unit.x, unit.y, candidate->x, candidate->y);
        if (distance > squared(reach_to(*candidate))) {
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
            better = candidate_building == prefer_buildings;
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

bool is_building_unit(const AiObservedUnit& unit) {
    return unit.type_id >= kMobileTypeLimit;
}

// Whether a unit is the class the tactic prefers (units_first -> mobile,
// buildings_first -> building).  neutral_only has no second class.
bool tactic_prefers(AiMicroAttackTactic tactic, const AiObservedUnit& unit) {
    switch (tactic) {
    case AiMicroAttackTactic::units_first:
        return !is_building_unit(unit);
    case AiMicroAttackTactic::buildings_first:
        return is_building_unit(unit);
    default:
        return true;
    }
}

// Whether a unit is a legal target under the tactic at all.
bool tactic_accepts(const StepContext& context, AiMicroAttackTactic tactic,
    const AiObservedUnit& unit) {
    return tactic == AiMicroAttackTactic::neutral_only ?
        is_neutral_monster(unit) :
        is_hostile_visible_unit(context.observation, unit);
}

// The group's target under a tactic, nearest to (x, y): the preferred class
// first, the other class only when none of the preferred is in sight.
// `attackable_mask` is the union of the group's weapon masks: a target no
// member can engage is not a target (item 1).
// `neutral_pool` (v10): the huntable subset of context.neutrals for
// neutral_only - the caller filters out monsters its ground fighters cannot
// reach; null keeps the unfiltered list.
const AiObservedUnit* pick_group_target(const StepContext& context,
    AiMicroAttackTactic tactic, i32 x, i32 y, u32 attackable_mask,
    const std::vector<const AiObservedUnit*>* neutral_pool = nullptr) {
    const auto nearest_of = [&](const std::vector<const AiObservedUnit*>& list,
                                bool preferred_only) -> const AiObservedUnit* {
        const AiObservedUnit* best = nullptr;
        i64 best_distance = 0;
        for (const AiObservedUnit* candidate : list) {
            if (candidate->render_class < 32u &&
                (attackable_mask & (1u << candidate->render_class)) == 0) {
                continue;
            }
            if (tactic_prefers(tactic, *candidate) != preferred_only) {
                continue;
            }
            const i64 distance =
                squared_distance(x, y, candidate->x, candidate->y);
            if (best == nullptr || distance < best_distance ||
                (distance == best_distance && candidate->id < best->id)) {
                best = candidate;
                best_distance = distance;
            }
        }
        return best;
    };
    if (tactic == AiMicroAttackTactic::neutral_only) {
        return nearest_of(neutral_pool != nullptr ? *neutral_pool :
            context.neutrals, true);
    }
    if (const AiObservedUnit* preferred = nearest_of(context.hostiles, true)) {
        return preferred;
    }
    return nearest_of(context.hostiles, false);
}

// Every harvestable tile, gathered once per step in tile-index order.  The
// planner's harvest gate is the explored projection: an amount on an
// unexplored tile is not orderable (target_not_visible), so the fog-honest
// filter stays here.  This replaces a full-map scan per idle worker.
void gather_resource_tiles(StepContext& context) {
    const AiObservation& observation = context.observation;
    if (observation.map_width_tiles == 0 ||
        observation.tiles.size() != static_cast<std::size_t>(
            observation.map_width_tiles) * observation.map_height_tiles) {
        return;
    }
    for (u32 tile_index = 0; tile_index < observation.tiles.size();
         ++tile_index) {
        const AiObservedMapTile& tile = observation.tiles[tile_index];
        // Berry tiles are not `passable` (engine terrain class 0x100 is
        // entered only through the harvest command); the amount is the
        // harvestability test.
        if (!tile.explored || tile.resource_amount == 0) {
            continue;
        }
        const u32 tile_x = tile_index % observation.map_width_tiles;
        const u32 tile_y = tile_index / observation.map_width_tiles;
        context.resource_tiles.push_back(ResourceTile{tile_index,
            UnitMovementPoint{static_cast<i32>(tile_x * 32u + 16u),
                static_cast<i32>(tile_y * 32u + 16u)}});
    }
    context.resource_tile_load.assign(context.resource_tiles.size(), 0u);
}

// Item 7 - berry nearest to (x, y) that is not already saturated.  Round-trip
// time is what decides harvest rate, so the reference point is the worker's
// nest, not the worker.  Falls back to the plain nearest tile when every
// candidate is at the cap, so a worker is never left idle.
std::size_t pick_resource_slot(const StepContext& context, i32 x, i32 y) {
    const std::size_t count = context.resource_tiles.size();
    std::size_t best = count;
    std::size_t fallback = count;
    i64 best_distance = 0;
    i64 fallback_distance = 0;
    for (std::size_t slot = 0; slot < count; ++slot) {
        const UnitMovementPoint& point = context.resource_tiles[slot].point;
        const i64 distance = squared_distance(x, y, point.x, point.y);
        if (fallback == count || distance < fallback_distance) {
            fallback = slot;
            fallback_distance = distance;
        }
        if (context.resource_tile_load[slot] >=
            context.config.workers_per_resource_tile) {
            continue;
        }
        if (best == count || distance < best_distance) {
            best = slot;
            best_distance = distance;
        }
    }
    return best != count ? best : fallback;
}

// Drops a worker's berry reservation so the next pick sees the freed slot.
void release_resource_slot(StepContext& context, AiMicroUnitRecord& record) {
    if (record.assigned_resource_tile == kAiMicroNoResourceTile) {
        return;
    }
    const std::size_t slot =
        resource_slot_of(context, record.assigned_resource_tile);
    if (slot < context.resource_tile_load.size() &&
        context.resource_tile_load[slot] != 0) {
        --context.resource_tile_load[slot];
    }
    record.assigned_resource_tile = kAiMicroNoResourceTile;
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

    // `returning` belongs to the defend leash; any other objective clears it.
    if (record.state == AiMicroUnitState::returning &&
        objective.kind != AiMicroObjectiveKind::defend) {
        record.state = AiMicroUnitState::normal;
        record.state_since_frame = context.frame;
    }

    // Low-health pull-back (attack/defend only): leave contact toward the
    // nearest base, resume once the weapon envelopes are clear.
    if (record.state == AiMicroUnitState::pulling_back) {
        if (!unit_in_contact(context, unit, 2)) {
            record.state = AiMicroUnitState::normal;
            record.state_since_frame = context.frame;
            // Item 5 - without this cooldown the unit walks straight back in,
            // drops under the threshold again and oscillates on the spot.
            record.pullback_ready_frame =
                context.frame + config.pullback_cooldown_frames;
        } else {
            return point_order(AiSemanticActionKind::move, nearest_base.x,
                nearest_base.y);
        }
    }
    // Item 5 - melee fighters never disengage: turning away inside a melee
    // envelope trades free hits and the unit dies anyway.  Only ranged units,
    // which can actually break contact, pull back.
    if (objective.kind != AiMicroObjectiveKind::retreat &&
        role != AiMicroRole::melee &&
        context.frame >= record.pullback_ready_frame &&
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
    case AiMicroObjectiveKind::attack:
    case AiMicroObjectiveKind::search: {
        // v10.4 staging (user design): a fighter produced AFTER the attack
        // order does not trickle into the ongoing assault - it defends the
        // base while gathering, and leaves only as part of a full new wave
        // (assignment happens in the objective-transition pass).
        if (objective.kind == AiMicroObjectiveKind::attack &&
            record.attack_wave == 0) {
            const AiMicroObjective staging = make_defend(nearest_base,
                config.defend_radius, context.frame);
            return fighter_order(context, state, record, unit, role, staging);
        }
        // search never engages: the sweep exists to reveal the map, and a
        // fight on the way is the policy's call (it sees the hostile and can
        // switch to attack).  So no per-unit pick, and a plain move below.
        if (objective.kind == AiMicroObjectiveKind::attack) {
            // Per-unit engagement: anything hostile in reach, in the
            // tactic's class order (buildings_first hits buildings first);
            // neutrals join only under the hunt.
            std::vector<const AiObservedUnit*> candidates = context.hostiles;
            if (objective.tactic == AiMicroAttackTactic::neutral_only) {
                candidates.insert(candidates.end(), context.neutrals.begin(),
                    context.neutrals.end());
                std::sort(candidates.begin(), candidates.end(),
                    [](const AiObservedUnit* lhs, const AiObservedUnit* rhs) {
                        return lhs->id < rhs->id;
                    });
            }
            if (const AiObservedUnit* target = pick_target(context, unit,
                    role, candidates, current_target,
                    objective.tactic == AiMicroAttackTactic::buildings_first)) {
                return attack_order(target->id);
            }
            // v10 meat pickup order (user replay report: hunters walked past
            // the drop to the NEXT monster and the meat rotted): with nothing
            // left to fight in reach, an assigned collector (pre-pass:
            // no-reserve fighters first, then nearest; one per drop) walks
            // onto its drop BEFORE the named-monster chase below.  The
            // engine's cmd-5 point path collects on arrival.
            for (const std::pair<u32, UnitMovementPoint>& assignment :
                 context.meat_assignments) {
                if (assignment.first != unit.id) {
                    continue;
                }
                if (record.last_kind != AiSemanticActionKind::pickup_move ||
                    record.last_x != assignment.second.x ||
                    record.last_y != assignment.second.y) {
                    ++context.meat_orders;
                }
                return point_order(AiSemanticActionKind::pickup_move,
                    assignment.second.x, assignment.second.y);
            }
            // Out of reach: the enemy tactics ADVANCE on the march point
            // (attack_move, so the engine engages whatever is met) - the
            // policy chose only the class priority, not a unit to chase.
            // The hunt is the exception: a monster is hunted by name.
            if (objective.tactic == AiMicroAttackTactic::neutral_only &&
                objective.target_unit_id != 0) {
                const AiObservedUnit* named = find_unit(context.observation,
                    objective.target_unit_id);
                if (named == nullptr || can_engage(unit, *named, config)) {
                    return attack_order(objective.target_unit_id);
                }
                // This unit's weapon cannot touch the monster: an attack
                // order would be rejected and the unit would sit idle.
                ++context.unattackable_skipped;
                return {};
            }
        }
        if (objective.target_x >= 0 &&
            squared_distance(unit.x, unit.y, objective.target_x,
                objective.target_y) > squared(config.arrival_radius)) {
            // Item 4 - do not arrive alone.  A fighter that is both far from
            // its group and ahead of it (closer to the objective) walks back
            // to the group first.  Laggards keep advancing, so the centroid
            // moves up and the leaders are released; the gate cannot stall
            // the whole group, and contact disables it outright.
            const std::size_t group_index =
                static_cast<std::size_t>(record.group);
            bool cohesion_return = false;
            if (context.centroid_valid[group_index] &&
                !unit_in_contact(context, unit, 1)) {
                UnitMovementPoint anchor =
                    context.cohesion_anchor[group_index];
                if (objective.kind == AiMicroObjectiveKind::attack) {
                    for (const StepContext::WaveAnchor& wave_anchor :
                         context.wave_anchors) {
                        if (wave_anchor.group ==
                                static_cast<u32>(record.group) &&
                            wave_anchor.wave == record.attack_wave) {
                            anchor = wave_anchor.anchor;
                            break;
                        }
                    }
                }
                const i64 unit_gap = squared_distance(unit.x, unit.y,
                    anchor.x, anchor.y);
                const i64 unit_to_goal = squared_distance(unit.x, unit.y,
                    objective.target_x, objective.target_y);
                const i64 group_to_goal = squared_distance(anchor.x,
                    anchor.y, objective.target_x, objective.target_y);
                // Hysteresis band (2026-08-31 user replay report): enter the
                // return state only beyond cohesion_engage_radius, hold it
                // until back inside cohesion_radius.  One shared threshold
                // flipped a boundary leader between advance and return every
                // frame — and a CHANGED order is issued immediately, so the
                // unit vibrated in place at the army front (= vision edge).
                const i32 engage = std::max(config.cohesion_engage_radius,
                    config.cohesion_radius);
                const i64 trigger = record.cohesion_returning != 0 ?
                    squared(config.cohesion_radius) : squared(engage);
                cohesion_return = unit_gap > trigger &&
                    unit_to_goal < group_to_goal;
            }
            record.cohesion_returning = cohesion_return ? 1u : 0u;
            if (cohesion_return) {
                // v10 (user replay report: the army surged forward and
                // backward, and fast units fought far ahead alone): a leader
                // WAITS IN PLACE for its group instead of walking back to the
                // centroid.  Walking back was the surge - and it also walked
                // leaders backwards through enemy vision (trembling, free
                // hits).  Standing still, the fast unit cannot enter the
                // fight alone either; weapon contact still releases the gate
                // so a reached fight is never refused.
                ++context.cohesion_holds;
                return point_order(AiSemanticActionKind::move, unit.x, unit.y);
            }
            return point_order(objective.kind == AiMicroObjectiveKind::search ?
                AiSemanticActionKind::move : AiSemanticActionKind::attack_move,
                objective.target_x, objective.target_y);
        }
        return {};
    }
    case AiMicroObjectiveKind::defend: {
        const UnitMovementPoint post{objective.target_x, objective.target_y};
        // Bubble anchors: every own BUILDING for a real base defense (an
        // outlying nest or a research building under attack is still ours to
        // hold), else just the post (hold/patrol at the army's own spot).
        std::vector<UnitMovementPoint> anchors;
        if (objective.radius >= config.defend_radius) {
            anchors = context.buildings.empty() ? context.bases :
                context.buildings;
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
        // Item 5 - hysteresis on the leash.  Leaving and re-engaging shared
        // one threshold, so a unit sitting on the bubble edge flipped between
        // "walk back to post" and "attack" every single frame.
        const UnitMovementPoint leash = nearest_point(anchors, unit.x, unit.y);
        if (leash.x >= 0 && post.x >= 0) {
            const i64 leash_gap =
                squared_distance(unit.x, unit.y, leash.x, leash.y);
            const i64 rejoin = static_cast<i64>(objective.radius) *
                static_cast<i64>(config.leash_return_percent) / 100;
            if (record.state == AiMicroUnitState::returning) {
                if (leash_gap <= squared(rejoin)) {
                    record.state = AiMicroUnitState::normal;
                    record.state_since_frame = context.frame;
                } else {
                    return point_order(AiSemanticActionKind::move, post.x,
                        post.y);
                }
            } else if (leash_gap > squared(objective.radius)) {
                record.state = AiMicroUnitState::returning;
                record.state_since_frame = context.frame;
                return point_order(AiSemanticActionKind::move, post.x, post.y);
            }
        }
        if (const AiObservedUnit* target =
                pick_target(context, unit, role, bubble, current_target)) {
            return attack_order(target->id);
        }
        // v10 (user replay report): drops near the base rotted once the hunt
        // objective ended.  A defender with no bubble target collects its
        // assigned drop (the pre-pass only assigns defenders meat INSIDE the
        // bubble, so this never fights the leash).
        for (const std::pair<u32, UnitMovementPoint>& assignment :
             context.meat_assignments) {
            if (assignment.first != unit.id) {
                continue;
            }
            if (record.last_kind != AiSemanticActionKind::pickup_move ||
                record.last_x != assignment.second.x ||
                record.last_y != assignment.second.y) {
                ++context.meat_orders;
            }
            return point_order(AiSemanticActionKind::pickup_move,
                assignment.second.x, assignment.second.y);
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
            // A hostile whose weapon cannot engage this worker's render class
            // is not a threat and must not scatter the economy.  Neither is a
            // non-COMBAT unit: an enemy WORKER wandering in is a scout, and
            // fleeing from it starved the economy (user replay report) - only
            // melee/ranged hostiles scatter workers.
            if (hostile->type_id >= kMobileTypeLimit ||
                !can_engage(*hostile, unit, context.config)) {
                continue;
            }
            const AiMicroRole role = AiMicroRoleOf(*hostile, context.config);
            if (role != AiMicroRole::melee && role != AiMicroRole::ranged) {
                continue;
            }
            if (squared_distance(unit.x, unit.y, hostile->x, hostile->y) <=
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
    // v10 (user replay report: idle workers stood around instead of mining):
    // no unit_is_harvesting() here - a truly IDLE unit (idle state, empty
    // queue) cannot be harvesting, but a STALE harvest command flag (0x4)
    // left behind by an interrupted trip made that test true and parked the
    // worker forever.
    if (unit_is_idle(unit) && unit_can_harvest(unit) &&
        !unit_is_constructing(unit)) {
        // Item 7 - free this worker's old reservation first so its own slot is
        // available again, then take the nearest berry that is not saturated.
        release_resource_slot(context, record);
        const std::size_t slot = pick_resource_slot(context, nearest_base.x,
            nearest_base.y);
        if (slot < context.resource_tiles.size()) {
            const ResourceTile& tile = context.resource_tiles[slot];
            ++context.resource_tile_load[slot];
            record.assigned_resource_tile = tile.tile_index;
            return point_order(AiSemanticActionKind::harvest, tile.point.x,
                tile.point.y);
        }
    }
    return {};
}

// ---- map-knowledge targets (search / explore / roam) ---------------------

// Nearest unexplored map start candidate (not our own) to (from_x, from_y).
bool pick_unexplored_start(const StepContext& context, i32 from_x, i32 from_y,
    UnitMovementPoint& destination) {
    const AiObservation& observation = context.observation;
    const u32 width = observation.map_width_tiles;
    const u32 height = observation.map_height_tiles;
    if (width == 0 || height == 0 ||
        observation.tiles.size() != static_cast<std::size_t>(width) * height) {
        return false;
    }
    UnitMovementPoint best{-1, -1};
    i64 best_distance = 0;
    for (u32 slot = 0; slot < 8u; ++slot) {
        if ((observation.start_candidate_mask & (1u << slot)) == 0) {
            continue;
        }
        const i32 candidate_x = observation.start_candidate_x[slot];
        const i32 candidate_y = observation.start_candidate_y[slot];
        if (candidate_x == observation.start_x &&
            candidate_y == observation.start_y) {
            continue;
        }
        const u32 tile_x = static_cast<u32>(std::max(candidate_x, 0)) >> 5;
        const u32 tile_y = static_cast<u32>(std::max(candidate_y, 0)) >> 5;
        if (tile_x >= width || tile_y >= height ||
            observation.tiles[tile_y * width + tile_x].explored) {
            continue;
        }
        const i64 distance = squared_distance(from_x, from_y, candidate_x,
            candidate_y);
        if (best.x < 0 || distance < best_distance) {
            best = {candidate_x, candidate_y};
            best_distance = distance;
        }
    }
    if (best.x < 0) {
        return false;
    }
    destination = best;
    return true;
}

// Passable tiles 4-connected to the tile under (x, y) px (terrain is public,
// so unexplored passable ground counts).  Empty vector = map invalid.
std::vector<u8> ground_reachable_tiles(const AiObservation& observation,
    i32 x, i32 y) {
    const u32 width = observation.map_width_tiles;
    const u32 height = observation.map_height_tiles;
    std::vector<u8> reachable;
    if (width == 0 || height == 0 ||
        observation.tiles.size() != static_cast<std::size_t>(width) * height) {
        return reachable;
    }
    reachable.assign(observation.tiles.size(), 0u);
    const u32 start_x = static_cast<u32>(std::max(x, 0)) >> 5;
    const u32 start_y = static_cast<u32>(std::max(y, 0)) >> 5;
    if (start_x >= width || start_y >= height) {
        return reachable;
    }
    std::vector<u32> stack;
    stack.push_back(start_y * width + start_x);
    reachable[start_y * width + start_x] = 1u;
    while (!stack.empty()) {
        const u32 index = stack.back();
        stack.pop_back();
        const u32 x = index % width;
        const u32 y = index / width;
        const i32 offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& offset : offsets) {
            const i64 nx = static_cast<i64>(x) + offset[0];
            const i64 ny = static_cast<i64>(y) + offset[1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                continue;
            }
            const u32 neighbour = static_cast<u32>(ny) * width +
                static_cast<u32>(nx);
            if (reachable[neighbour] != 0 || !observation.tiles[neighbour].passable) {
                continue;
            }
            reachable[neighbour] = 1u;
            stack.push_back(neighbour);
        }
    }
    return reachable;
}

// Tiles a unit can reach: everything for a flyer; for a ground unit the
// ground flood from the tile it stands on.  Empty vector = map invalid.
std::vector<u8> reachable_tiles(const StepContext& context,
    const AiObservedUnit& unit) {
    const AiObservation& observation = context.observation;
    const u32 width = observation.map_width_tiles;
    const u32 height = observation.map_height_tiles;
    if (unit.render_class == context.config.flying_render_class) {
        std::vector<u8> reachable;
        if (width != 0 && height != 0 && observation.tiles.size() ==
                static_cast<std::size_t>(width) * height) {
            reachable.assign(observation.tiles.size(), 1u);
        }
        return reachable;
    }
    return ground_reachable_tiles(observation, unit.x, unit.y);
}

// v10 hunt guard: whether a unit at (target.x, target.y) can be reached by a
// ground force whose flood is `reachable` - its own tile or any 4-neighbour
// (a monster may stand on impassable ground, e.g. a berry tile edge).
bool target_ground_reachable(const AiObservation& observation,
    const std::vector<u8>& reachable, const AiObservedUnit& target) {
    const u32 width = observation.map_width_tiles;
    const u32 height = observation.map_height_tiles;
    if (width == 0 || height == 0 ||
        reachable.size() != static_cast<std::size_t>(width) * height) {
        return true;  // no flood available - do not filter
    }
    const i64 tile_x = std::max(target.x, 0) >> 5;
    const i64 tile_y = std::max(target.y, 0) >> 5;
    static const i32 kSteps[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& step : kSteps) {
        const i64 nx = tile_x + step[0];
        const i64 ny = tile_y + step[1];
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
            continue;
        }
        if (reachable[static_cast<std::size_t>(ny) * width +
                static_cast<std::size_t>(nx)] != 0) {
            return true;
        }
    }
    return false;
}

// Nearest reachable frontier tile to (from_x, from_y): unexplored passable
// ground bordering explored passable ground.
bool pick_frontier(const StepContext& context, i32 from_x, i32 from_y,
    const std::vector<u8>& reachable, UnitMovementPoint& destination) {
    const AiObservation& observation = context.observation;
    const u32 width = observation.map_width_tiles;
    const u32 height = observation.map_height_tiles;
    if (reachable.size() != observation.tiles.size() || width == 0) {
        return false;
    }
    UnitMovementPoint best{-1, -1};
    i64 best_distance = 0;
    for (u32 tile_y = 0; tile_y < height; ++tile_y) {
        for (u32 tile_x = 0; tile_x < width; ++tile_x) {
            const u32 index = tile_y * width + tile_x;
            const AiObservedMapTile& tile = observation.tiles[index];
            if (tile.explored || !tile.passable || reachable[index] == 0) {
                continue;
            }
            bool borders_explored = false;
            const i32 offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& offset : offsets) {
                const i64 nx = static_cast<i64>(tile_x) + offset[0];
                const i64 ny = static_cast<i64>(tile_y) + offset[1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                    continue;
                }
                const AiObservedMapTile& neighbour = observation.tiles[
                    static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx)];
                if (neighbour.explored && neighbour.passable) {
                    borders_explored = true;
                    break;
                }
            }
            if (!borders_explored) {
                continue;
            }
            const UnitMovementPoint point{
                static_cast<i32>(tile_x * 32u + 16u),
                static_cast<i32>(tile_y * 32u + 16u)};
            const i64 distance = squared_distance(from_x, from_y, point.x, point.y);
            if (best.x < 0 || distance < best_distance) {
                best = point;
                best_distance = distance;
            }
        }
    }
    if (best.x < 0) {
        return false;
    }
    destination = best;
    return true;
}

u32 xorshift32(u32& state) {
    u32 x = state == 0 ? 0x9e3779b9u : state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

// A random reachable passable tile outside the owner's ACTIVE vision (the
// roamer's next waypoint).  Deterministic (xorshift32 in the executor state).
bool pick_roam_point(const StepContext& context, const std::vector<u8>& reachable,
    u32& rng, UnitMovementPoint& destination) {
    const AiObservation& observation = context.observation;
    if (reachable.size() != observation.tiles.size() ||
        observation.map_width_tiles == 0) {
        return false;
    }
    std::vector<u32> candidates;
    for (u32 index = 0; index < observation.tiles.size(); ++index) {
        const AiObservedMapTile& tile = observation.tiles[index];
        if (reachable[index] != 0 && tile.passable && !tile.visible) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        return false;
    }
    const u32 index = candidates[xorshift32(rng) % candidates.size()];
    destination = {
        static_cast<i32>((index % observation.map_width_tiles) * 32u + 16u),
        static_cast<i32>((index / observation.map_width_tiles) * 32u + 16u)};
    return true;
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
        // Only COMBAT units are worth evading - an enemy worker scouting past
        // must not push the picket off its post (user replay report).
        const AiMicroRole hostile_role =
            AiMicroRoleOf(*hostile, context.config);
        if (hostile_role != AiMicroRole::melee &&
            hostile_role != AiMicroRole::ranged) {
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
    const AiMicroExecutorConfig& config = context.config;
    // Picket: walk to the post and hold it.  The post (between home and the
    // known enemy base) is re-derived every frame in the step's objective
    // transitions; the scout's job is to SEE an attack coming early and stay
    // alive, so it never wanders off the post and never fights.
    if (objective.target_x >= 0 &&
        squared_distance(unit.x, unit.y, objective.target_x,
            objective.target_y) > squared(config.arrival_radius)) {
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
    // Classify from the RAW definition range: `attack_range` now carries the
    // research/equipment/variant bonuses, and a range upgrade must not
    // reclassify a melee unit as ranged half-way through a match.
    return unit.attack_range_base != 0 &&
        unit.attack_range_base <= config.melee_range_threshold ?
        AiMicroRole::melee : AiMicroRole::ranged;
}

u64 AiMicroCombatPower(const AiObservedUnit& unit) {
    return unit_power(unit);
}

bool AiMicroHuntableNeutralExists(const AiObservation& observation,
    const AiMicroExecutorConfig& config, i32 center_x, i32 center_y,
    i32 radius) {
    std::vector<const AiObservedUnit*> neutrals;
    bool own_flyer_attacker = false;
    for (const AiObservedUnit& unit : observation.units) {
        if (is_neutral_monster(unit)) {
            if (radius > 0 && center_x >= 0 &&
                squared_distance(center_x, center_y, unit.x, unit.y) >
                    squared(radius)) {
                continue;  // v10: outside the hunt bound
            }
            neutrals.push_back(&unit);
        } else if (own_alive(unit) && !unit.under_construction &&
            unit_can_attack(unit) &&
            unit.render_class == config.flying_render_class) {
            own_flyer_attacker = true;
        }
    }
    if (neutrals.empty()) {
        return false;
    }
    if (own_flyer_attacker) {
        return true;  // a flyer reaches everything
    }
    const std::vector<u8> reachable = ground_reachable_tiles(observation,
        std::max(observation.start_x, 0), std::max(observation.start_y, 0));
    for (const AiObservedUnit* neutral : neutrals) {
        if (target_ground_reachable(observation, reachable, *neutral)) {
            return true;
        }
    }
    return false;
}

void AiMicroReset(AiMicroExecutorState& state) {
    state = AiMicroExecutorState{};
}

void AiMicroSetObjective(AiMicroExecutorState& state, AiMicroGroup group,
    const AiMicroObjective& objective) {
    state.objectives[static_cast<std::size_t>(group)] = objective;
    state.objectives[static_cast<std::size_t>(group)].assigned = true;
    // v10 attack commit: a fresh policy objective restarts the "has this
    // group met the enemy yet" latch.
    state.group_engaged[static_cast<std::size_t>(group)] = 0;
    // v10.4: a fresh ATTACK forms a new wave from the members present at the
    // next step; any other objective dissolves the wave structure (everyone
    // follows the new objective together - the staged tail merges here).
    if (objective.kind == AiMicroObjectiveKind::attack) {
        ++state.attack_wave_seq[static_cast<std::size_t>(group)];
        state.attack_wave_pending[static_cast<std::size_t>(group)] = 1;
    } else {
        state.attack_wave_pending[static_cast<std::size_t>(group)] = 0;
    }
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
        // v10.4: wave membership does not carry across groups - a unit that
        // changes group joins the new group's attack as staging.
        record.attack_wave = 0;
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
            // Free the berry reservation: the unit is off doing policy work
            // and must not keep a harvest slot blocked.
            record->assigned_resource_tile = kAiMicroNoResourceTile;
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
        if (own_alive(unit) && unit.type_id >= kBuildingTypeStart &&
            !unit.under_construction) {
            context.buildings.push_back({unit.x, unit.y});
            if (unit.type_id == config.base_type_id) {
                context.bases.push_back({unit.x, unit.y});
            }
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
        AiMicroObjective& berry_default = state.objectives[
            static_cast<std::size_t>(AiMicroGroup::berry_scout)];
        if (!berry_default.assigned) {
            berry_default = AiMicroObjective{};
            berry_default.kind = AiMicroObjectiveKind::scout;
            berry_default.set_frame = context.frame;
            berry_default.assigned = true;
        }
        AiMicroObjective& explorer_default = state.objectives[
            static_cast<std::size_t>(AiMicroGroup::explorer)];
        if (!explorer_default.assigned) {
            explorer_default = AiMicroObjective{};
            explorer_default.kind = AiMicroObjectiveKind::explore;
            explorer_default.set_frame = context.frame;
            explorer_default.assigned = true;
        }
        AiMicroObjective& roamer_default = state.objectives[
            static_cast<std::size_t>(AiMicroGroup::roamer)];
        if (!roamer_default.assigned) {
            roamer_default = AiMicroObjective{};
            roamer_default.kind = AiMicroObjectiveKind::roam;
            roamer_default.set_frame = context.frame;
            roamer_default.assigned = true;
        }
        for (const AiMicroGroup raid_group : kAiMicroRaidGroups) {
            AiMicroObjective& raid_default = state.objectives[
                static_cast<std::size_t>(raid_group)];
            if (!raid_default.assigned) {
                raid_default = make_defend(home, config.defend_radius,
                    context.frame);
            }
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

    // ---- per-step tables --------------------------------------------------
    gather_resource_tiles(context);
    // Seed berry reservations from the records that still hold one; a stale
    // assignment (tile exhausted, or the map memory dropped it) is released.
    for (AiMicroUnitRecord& record : state.units) {
        if (record.assigned_resource_tile == kAiMicroNoResourceTile) {
            continue;
        }
        const std::size_t slot =
            resource_slot_of(context, record.assigned_resource_tile);
        if (slot < context.resource_tiles.size()) {
            ++context.resource_tile_load[slot];
        } else {
            record.assigned_resource_tile = kAiMicroNoResourceTile;
        }
    }
    // Group centroids (item 4) and the union of each group's weapon target
    // masks (item 1).  `own` and `state.units` are index-aligned here: the
    // sync block above rebuilt the records from `own`, in the same order.
    {
        std::array<i64, kAiMicroGroupCount> sum_x{};
        std::array<i64, kAiMicroGroupCount> sum_y{};
        std::array<i64, kAiMicroGroupCount> count{};
        for (std::size_t index = 0; index < own.size(); ++index) {
            const std::size_t group =
                static_cast<std::size_t>(state.units[index].group);
            sum_x[group] += own[index]->x;
            sum_y[group] += own[index]->y;
            ++count[group];
            if (unit_can_attack(*own[index])) {
                context.group_attackable_mask[group] |=
                    own[index]->attackable_class_mask;
            }
        }
        for (std::size_t group = 0; group < kAiMicroGroupCount; ++group) {
            if (count[group] == 0) {
                continue;
            }
            context.centroid[group] = {
                static_cast<i32>(sum_x[group] / count[group]),
                static_cast<i32>(sum_y[group] / count[group])};
            context.centroid_valid[group] = true;
        }
        // v10.3 cohesion anchors: per-group component-wise median.
        std::array<std::vector<i32>, kAiMicroGroupCount> xs;
        std::array<std::vector<i32>, kAiMicroGroupCount> ys;
        for (std::size_t index = 0; index < own.size(); ++index) {
            const std::size_t group =
                static_cast<std::size_t>(state.units[index].group);
            xs[group].push_back(own[index]->x);
            ys[group].push_back(own[index]->y);
        }
        const auto median_of = [](std::vector<i32>& values) -> i32 {
            const std::size_t mid = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + mid,
                values.end());
            i32 result = values[mid];
            if (values.size() % 2 == 0) {
                std::nth_element(values.begin(), values.begin() + mid - 1,
                    values.begin() + mid);
                result = static_cast<i32>((static_cast<i64>(result) +
                    values[mid - 1]) / 2);
            }
            return result;
        };
        for (std::size_t group = 0; group < kAiMicroGroupCount; ++group) {
            if (!xs[group].empty()) {
                context.cohesion_anchor[group] = {median_of(xs[group]),
                    median_of(ys[group])};
            }
        }
        // v10.4 per-wave anchors (attack objectives only need them, but the
        // pairs are few and cheap to build for everyone).
        for (std::size_t index = 0; index < own.size(); ++index) {
            const AiMicroUnitRecord& record = state.units[index];
            if (record.attack_wave == 0) {
                continue;
            }
            const u32 group = static_cast<u32>(record.group);
            bool found = false;
            for (const StepContext::WaveAnchor& anchor : context.wave_anchors) {
                if (anchor.group == group && anchor.wave == record.attack_wave) {
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }
            std::vector<i32> wave_xs;
            std::vector<i32> wave_ys;
            for (std::size_t other = 0; other < own.size(); ++other) {
                const AiMicroUnitRecord& peer = state.units[other];
                if (static_cast<u32>(peer.group) == group &&
                    peer.attack_wave == record.attack_wave) {
                    wave_xs.push_back(own[other]->x);
                    wave_ys.push_back(own[other]->y);
                }
            }
            context.wave_anchors.push_back({group, record.attack_wave,
                {median_of(wave_xs), median_of(wave_ys)}});
        }
    }

    // ---- v9 base-defense reflex (overlay; objectives[] untouched) ----------
    // Threat = a hostile COMBAT mobile (melee/ranged role - a harvesting
    // worker nearby is not an attack) inside the defend bubble of any own
    // building, or an own completed building losing health since last frame.
    // While active, the army (and a nearby raid) fights under a temporary
    // defend objective anchored at the threatened building; cleared after
    // threat_clear_frames without a threat in the bubble.
    if (config.reflex_enabled) {
        u64 building_health = 0;
        for (const AiObservedUnit& unit : observation.units) {
            if (unit.controlled && unit.alive && !unit.under_construction &&
                is_building_unit(unit)) {
                building_health += unit.health;
            }
        }
        const bool buildings_hurt =
            state.last_building_health != 0xffffffffffffffffull &&
            building_health < state.last_building_health;
        state.last_building_health = building_health;
        UnitMovementPoint anchor{-1, -1};
        // v10 threat measurement: combat power of every hostile inside any
        // building's bubble, so the response can be SIZED to the threat.
        // v10.5 (user directive): the anchor is the CENTROID OF THE VISIBLE
        // THREAT, not the threatened building - defenders walk at the enemy
        // force instead of standing at the nest watching it shoot.
        u64 threat_power = 0;
        i64 threat_sum_x = 0;
        i64 threat_sum_y = 0;
        i64 threat_count = 0;
        for (const AiObservedUnit* hostile : context.hostiles) {
            if (is_building_unit(*hostile)) {
                continue;
            }
            const AiMicroRole role = AiMicroRoleOf(*hostile, config);
            if (role != AiMicroRole::melee && role != AiMicroRole::ranged) {
                continue;
            }
            bool in_bubble = false;
            for (const UnitMovementPoint& building : context.buildings) {
                if (squared_distance(building.x, building.y, hostile->x,
                        hostile->y) <= squared(config.defend_radius)) {
                    in_bubble = true;
                    break;
                }
            }
            if (in_bubble) {
                threat_power += unit_power(*hostile);
                threat_sum_x += hostile->x;
                threat_sum_y += hostile->y;
                ++threat_count;
            }
        }
        if (threat_count > 0) {
            anchor = {static_cast<i32>(threat_sum_x / threat_count),
                static_cast<i32>(threat_sum_y / threat_count)};
        }
        const bool threat_now = anchor.x >= 0 ||
            (buildings_hurt && !context.buildings.empty());
        if (threat_now) {
            if (!state.threat.active) {
                state.threat.active = true;
                state.threat.since_frame = context.frame;
                ++state.reflex_activations;
            }
            if (anchor.x >= 0) {
                state.threat.anchor_x = anchor.x;
                state.threat.anchor_y = anchor.y;
            } else if (state.threat.anchor_x < 0) {
                state.threat.anchor_x = context.buildings.front().x;
                state.threat.anchor_y = context.buildings.front().y;
            }
            state.threat.last_seen_frame = context.frame;
            // v10 proportional detail (2026-08-31 user replay review: one
            // harasser recalled the whole army).  Keep the standing members
            // that are still alive in the army group, then top the detail up
            // nearest-first until its combat power covers the measured threat
            // times the margin.  Members are only ever ADDED while the
            // overlay stands (released when it clears), so the selection
            // cannot flap.  An unmeasurable threat (buildings losing health,
            // attacker unseen) sends a small investigation picket of at most
            // reflex_unseen_defenders fighters (user directive: <= 3), never
            // the whole army.  Only updated while a threat is PRESENT this
            // frame - during the linger window the standing detail keeps
            // fighting.
            std::vector<u32> kept_defenders;
            u64 detail_power = 0;
            for (std::size_t index = 0; index < own.size(); ++index) {
                if (state.units[index].group != AiMicroGroup::army) {
                    continue;
                }
                if (std::find(state.reflex_defenders.begin(),
                        state.reflex_defenders.end(), own[index]->id) ==
                        state.reflex_defenders.end()) {
                    continue;
                }
                kept_defenders.push_back(own[index]->id);
                detail_power += unit_power(*own[index]);
            }
            state.reflex_defenders = std::move(kept_defenders);
            // Measured threat: power target with the usual floor.  Unseen
            // attacker: no power target, just the small picket cap.
            const u64 needed = threat_power *
                config.reflex_margin_percent / 100u;
            const std::size_t want_count = threat_power != 0 ?
                config.reflex_min_defenders : config.reflex_unseen_defenders;
            if (detail_power < needed ||
                state.reflex_defenders.size() < want_count) {
                struct DefenseCandidate {
                    i64 gap;
                    u32 id;
                    u64 power;
                };
                std::vector<DefenseCandidate> candidates;
                for (std::size_t index = 0; index < own.size(); ++index) {
                    if (state.units[index].group != AiMicroGroup::army) {
                        continue;
                    }
                    const AiMicroRole own_role =
                        AiMicroRoleOf(*own[index], config);
                    if (own_role != AiMicroRole::melee &&
                        own_role != AiMicroRole::ranged) {
                        continue;
                    }
                    if (std::find(state.reflex_defenders.begin(),
                            state.reflex_defenders.end(),
                            own[index]->id) !=
                            state.reflex_defenders.end()) {
                        continue;
                    }
                    candidates.push_back({squared_distance(own[index]->x,
                        own[index]->y, state.threat.anchor_x,
                        state.threat.anchor_y), own[index]->id,
                        unit_power(*own[index])});
                }
                std::sort(candidates.begin(), candidates.end(),
                    [](const DefenseCandidate& lhs,
                        const DefenseCandidate& rhs) {
                        return lhs.gap != rhs.gap ? lhs.gap < rhs.gap :
                            lhs.id < rhs.id;
                    });
                for (const DefenseCandidate& candidate : candidates) {
                    if (detail_power >= needed &&
                        state.reflex_defenders.size() >= want_count) {
                        break;
                    }
                    state.reflex_defenders.push_back(candidate.id);
                    detail_power += candidate.power;
                }
            }
        } else if (state.threat.active &&
            context.frame - state.threat.last_seen_frame >=
                config.threat_clear_frames) {
            state.threat = AiMicroThreatOverlay{};
            state.reflex_defenders.clear();
        }
    } else if (state.threat.active) {
        state.threat = AiMicroThreatOverlay{};
        state.reflex_defenders.clear();
    }

    // ---- objective transitions (default behavior, not decisions) -----------
    AiMicroObjective& economy =
        state.objectives[static_cast<std::size_t>(AiMicroGroup::economy)];
    const bool resources_known = !context.resource_tiles.empty();
    if (economy.kind == AiMicroObjectiveKind::harvest && !resources_known) {
        economy = make_defend(home, config.defend_radius, context.frame);
    } else if (economy.kind == AiMicroObjectiveKind::defend && resources_known) {
        AiMicroObjective harvest;
        harvest.kind = AiMicroObjectiveKind::harvest;
        harvest.set_frame = context.frame;
        harvest.assigned = true;
        economy = harvest;
    }
    // Remembered enemy buildings (fog memory in the observation) - known
    // enemy locations for the attack march point and the scout picket.
    if (observation.map_width_tiles != 0 &&
        observation.enemy_building_memory.size() == observation.tiles.size()) {
        for (std::size_t t = 0; t < observation.tiles.size(); ++t) {
            if (observation.enemy_building_memory[t] == 0 ||
                observation.tiles[t].visible) {
                continue;
            }
            const u32 tile_x = static_cast<u32>(t % observation.map_width_tiles);
            const u32 tile_y = static_cast<u32>(t / observation.map_width_tiles);
            context.remembered_buildings.push_back({
                static_cast<i32>(tile_x * 32u + 16u),
                static_cast<i32>(tile_y * 32u + 16u)});
        }
    }
    // Scout picket post: the scout is an early-warning picket, not a base
    // finder.  It stands between home and the nearest KNOWN enemy building
    // (visible or remembered), forward of the midpoint so an attack is seen
    // early, but at least picket_enemy_gap from that building so it is not
    // standing in the enemy's base.  Re-derived every frame; with no enemy
    // building known the last post stands (the policy cannot pick scout_map
    // then anyway).
    {
        AiMicroObjective& scout =
            state.objectives[static_cast<std::size_t>(AiMicroGroup::scout)];
        if (scout.kind == AiMicroObjectiveKind::scout) {
            std::vector<UnitMovementPoint> known = context.remembered_buildings;
            for (const AiObservedUnit* hostile : context.hostiles) {
                if (is_building_unit(*hostile)) {
                    known.push_back({hostile->x, hostile->y});
                }
            }
            const UnitMovementPoint enemy = nearest_point(known, home.x, home.y);
            if (enemy.x >= 0) {
                const double dx = static_cast<double>(enemy.x - home.x);
                const double dy = static_cast<double>(enemy.y - home.y);
                const double length = std::sqrt(dx * dx + dy * dy);
                double forward = length * config.picket_forward_percent / 100.0;
                forward = std::min(forward, length - config.picket_enemy_gap);
                forward = std::max(forward, 0.0);
                const UnitMovementPoint post = length < 1.0 ? home :
                    clamp_to_map(observation,
                        home.x + static_cast<i32>(dx / length * forward),
                        home.y + static_cast<i32>(dy / length * forward));
                scout.target_x = post.x;
                scout.target_y = post.y;
            }
        }
    }
    // Berry scout: its job is done the moment the expansion site tile is
    // explored - release the unit back to its default group (the policy
    // re-picks scout_berry for the next dark site; the mask stays open while
    // one exists).  A default transition, not a decision.
    {
        AiMicroObjective& berry = state.objectives[
            static_cast<std::size_t>(AiMicroGroup::berry_scout)];
        if (berry.target_x >= 0 && observation.map_width_tiles != 0) {
            const u32 tile_x = static_cast<u32>(berry.target_x) >> 5;
            const u32 tile_y = static_cast<u32>(berry.target_y) >> 5;
            const std::size_t index = static_cast<std::size_t>(tile_y) *
                observation.map_width_tiles + tile_x;
            if (index < observation.tiles.size() &&
                observation.tiles[index].explored) {
                for (std::size_t unit_index = 0; unit_index < own.size();
                     ++unit_index) {
                    AiMicroUnitRecord& record = state.units[unit_index];
                    if (record.group != AiMicroGroup::berry_scout) {
                        continue;
                    }
                    record.group = default_group(
                        AiMicroRoleOf(*own[unit_index], config));
                    record.state = AiMicroUnitState::normal;
                    record.last_kind = AiSemanticActionKind::no_op;
                    record.last_target_id = 0;
                }
                berry.target_x = -1;
                berry.target_y = -1;
            }
        }
    }
    // The two FIGHTING groups (main army and the detached raid) run the same
    // objective machinery, each around its own centroid and objective.
    for (const AiMicroGroup fighting : {AiMicroGroup::army, AiMicroGroup::raid,
             AiMicroGroup::raid_b, AiMicroGroup::raid_c}) {
    const std::size_t army_index = static_cast<std::size_t>(fighting);
    AiMicroObjective& army = state.objectives[army_index];
    // v10.4 attack waves.  A fresh attack enrolls everyone present as the
    // new FIRST wave; afterwards, fighters that joined the group later
    // (attack_wave 0) stage at the base until attack_wave_minimum of them
    // have gathered, then leave together as their own wave.
    if (army.kind == AiMicroObjectiveKind::attack) {
        const auto is_wave_fighter = [&](std::size_t index) {
            if (state.units[index].group != fighting) {
                return false;
            }
            const AiMicroRole role = AiMicroRoleOf(*own[index], config);
            return role == AiMicroRole::melee || role == AiMicroRole::ranged;
        };
        if (state.attack_wave_pending[army_index] != 0) {
            for (std::size_t index = 0; index < own.size(); ++index) {
                if (is_wave_fighter(index)) {
                    state.units[index].attack_wave =
                        state.attack_wave_seq[army_index];
                }
            }
            state.attack_wave_pending[army_index] = 0;
        } else {
            std::vector<std::size_t> staging;
            for (std::size_t index = 0; index < own.size(); ++index) {
                if (is_wave_fighter(index) &&
                    state.units[index].attack_wave == 0) {
                    staging.push_back(index);
                }
            }
            if (staging.size() >= config.attack_wave_minimum) {
                ++state.attack_wave_seq[army_index];
                for (const std::size_t index : staging) {
                    state.units[index].attack_wave =
                        state.attack_wave_seq[army_index];
                }
            }
        }
    }
    const UnitMovementPoint army_centroid = context.centroid_valid[army_index] ?
        context.centroid[army_index] : home;
    if (army.kind == AiMicroObjectiveKind::retreat && army.target_x >= 0) {
        UnitMovementPoint centroid{};
        if (AiMicroGroupCentroid(state, observation, fighting,
                centroid, config) &&
            squared_distance(centroid.x, centroid.y, army.target_x,
                army.target_y) <= squared(config.arrival_radius)) {
            army = make_defend({army.target_x, army.target_y},
                config.defend_radius, context.frame);
        }
    }
    if (army.kind == AiMicroObjectiveKind::attack) {
        // The policy chose a TACTIC; the group's target is re-derived from it
        // every frame.  Keep the current target while it is still valid and of
        // the preferred class (stability); switch when it dies, leaves sight,
        // or a preferred-class target appears while we hold a fallback one.
        const u32 army_mask = context.group_attackable_mask[army_index];
        // v8 strike zone: while the policy's preferred point is set, targets
        // are hunted around IT (not around the group), so the group fights
        // where it was sent; abandoned once the group stands there and
        // nothing of the tactic's class is known within the zone.
        const bool zoned = army.preferred_x >= 0 &&
            army.tactic != AiMicroAttackTactic::neutral_only;
        const i32 hunt_x = zoned ? army.preferred_x : army_centroid.x;
        const i32 hunt_y = zoned ? army.preferred_y : army_centroid.y;
        // v10 hunt guard (user replay report: the army parked at a cliff
        // hunting a monster on a walkable island it could never reach).  For
        // the hunt, monsters the group's GROUND fighters cannot walk to (not
        // 4-connected to where they stand) are no targets; a group with no
        // ground fighter (all flyers) hunts unfiltered.
        std::vector<const AiObservedUnit*> huntable;
        const std::vector<const AiObservedUnit*>* neutral_pool = nullptr;
        if (army.tactic == AiMicroAttackTactic::neutral_only &&
            !context.neutrals.empty()) {
            const AiObservedUnit* ground = nullptr;
            for (std::size_t index = 0; index < own.size(); ++index) {
                if (state.units[index].group != fighting) {
                    continue;
                }
                if (unit_can_attack(*own[index]) &&
                    own[index]->render_class != config.flying_render_class) {
                    ground = own[index];
                    break;
                }
            }
            if (ground != nullptr) {
                const std::vector<u8> reachable =
                    ground_reachable_tiles(observation, ground->x, ground->y);
                // v10 hunt bound: the MAIN army only hunts near itself; far
                // monsters are the raid slots' job (user directive).
                const bool bounded = fighting == AiMicroGroup::army &&
                    config.army_hunt_radius > 0 &&
                    context.centroid_valid[army_index];
                for (const AiObservedUnit* neutral : context.neutrals) {
                    if (bounded && squared_distance(army_centroid.x,
                            army_centroid.y, neutral->x, neutral->y) >
                            squared(config.army_hunt_radius)) {
                        continue;
                    }
                    if (target_ground_reachable(observation, reachable,
                            *neutral)) {
                        huntable.push_back(neutral);
                    } else {
                        ++context.hunt_unreachable;
                    }
                }
                neutral_pool = &huntable;
            }
        }
        const AiObservedUnit* current =
            find_unit(observation, army.target_unit_id);
        // Item 1 - a target no army member can engage is not a valid target.
        // Leaving it set parks the whole group on an order the engine rejects.
        const bool engageable = current != nullptr &&
            (current->render_class >= 32u ||
                (army_mask & (1u << current->render_class)) != 0);
        bool keep = current != nullptr && current->alive && current->visible &&
            engageable && tactic_accepts(context, army.tactic, *current);
        if (keep && neutral_pool != nullptr &&
            std::find(huntable.begin(), huntable.end(), current) ==
                huntable.end()) {
            keep = false;  // the held monster became unreachable
        }
        const AiObservedUnit* best = pick_group_target(context, army.tactic,
            hunt_x, hunt_y, army_mask, neutral_pool);
        if (zoned && best != nullptr &&
            squared_distance(best->x, best->y, army.preferred_x,
                army.preferred_y) > squared(config.preferred_zone_radius)) {
            best = nullptr;  // outside the strike zone
        }
        if (keep && !tactic_prefers(army.tactic, *current) && best != nullptr &&
            tactic_prefers(army.tactic, *best)) {
            keep = false;
        }
        if (!keep) {
            const u32 previous = army.target_unit_id;
            army.target_unit_id = best != nullptr ? best->id : 0u;
            if (previous != 0 && previous != army.target_unit_id) {
                ++state.targets_retargeted;
            }
        }
        // March point (attack_move destination) - the policy chose only the
        // class priority; the army advances on where that class is known to
        // be.  Remembered enemy buildings (fog memory in the observation) are
        // known locations too:
        //   buildings_first: visible building > remembered building > visible unit
        //   units_first:     visible unit > visible building > remembered building
        //   neutral_only:    the monster (hunted by name, see fighter_order)
        UnitMovementPoint remembered{-1, -1};
        if (army.tactic != AiMicroAttackTactic::neutral_only) {
            remembered = nearest_point(context.remembered_buildings,
                hunt_x, hunt_y);
            if (zoned && remembered.x >= 0 &&
                squared_distance(remembered.x, remembered.y, army.preferred_x,
                    army.preferred_y) > squared(config.preferred_zone_radius)) {
                remembered = {-1, -1};  // outside the strike zone
            }
        }
        const AiObservedUnit* visible =
            find_unit(observation, army.target_unit_id);
        army.target_x = -1;
        army.target_y = -1;
        if (visible != nullptr &&
            (remembered.x < 0 || tactic_prefers(army.tactic, *visible) ||
                army.tactic == AiMicroAttackTactic::units_first)) {
            army.target_x = visible->x;
            army.target_y = visible->y;
        } else if (remembered.x >= 0) {
            army.target_x = remembered.x;
            army.target_y = remembered.y;
        } else if (zoned) {
            army.target_x = army.preferred_x;
            army.target_y = army.preferred_y;
        }
        if (zoned && visible == nullptr && remembered.x < 0 &&
            context.centroid_valid[army_index] &&
            squared_distance(army_centroid.x, army_centroid.y,
                army.preferred_x, army.preferred_y) <=
                squared(config.arrival_radius)) {
            // Arrived at the strike zone and nothing of the tactic's class is
            // known inside it: the zone is done, fall back to the global
            // chain from the next frame on.
            army.preferred_x = -1;
            army.preferred_y = -1;
        }
    }
    if (army.kind == AiMicroObjectiveKind::search) {
        // Sweep for the enemy's buildings: nearest unexplored start candidate,
        // then the exploration frontier, then (map fully explored) a rotating
        // cycle of the centre and corners.  The point is re-picked when the
        // group reaches it or its tile is revealed, at most once per
        // scout_repick_interval_frames so a distant reveal does not restart
        // the map scan every frame; a cycle point only on arrival.
        army.target_unit_id = 0;
        bool repick = army.target_x < 0;
        if (!repick && squared_distance(army_centroid.x, army_centroid.y,
                army.target_x, army.target_y) <= squared(config.arrival_radius)) {
            repick = true;
        } else if (!repick && !army.sweep_cycle &&
            context.frame - army.sweep_pick_frame >=
                config.scout_repick_interval_frames) {
            const u32 width = observation.map_width_tiles;
            const u32 tile_x = static_cast<u32>(army.target_x) >> 5;
            const u32 tile_y = static_cast<u32>(army.target_y) >> 5;
            const std::size_t index =
                static_cast<std::size_t>(tile_y) * width + tile_x;
            if (width == 0 || index >= observation.tiles.size() ||
                observation.tiles[index].explored) {
                repick = true;
            }
        }
        if (repick) {
            // Start candidates only (v7 split): with none left the army
            // stands - the mask has already closed search_enemy_base.
            UnitMovementPoint destination{-1, -1};
            army.sweep_pick_frame = context.frame;
            army.sweep_cycle = false;
            if (pick_unexplored_start(context, army_centroid.x, army_centroid.y,
                    destination)) {
                ++context.search_picks;
            }
            army.target_x = destination.x;
            army.target_y = destination.y;
        }
    }
    }  // for (fighting group: army, raid)
    // Explorer: nearest reachable frontier from the unit; released (default
    // transition) when its reachable frontier is exhausted.  Roamer: random
    // reachable ground outside the active vision, re-picked on arrival.
    for (const AiMicroGroup group : {AiMicroGroup::explorer, AiMicroGroup::roamer}) {
        const std::size_t group_index = static_cast<std::size_t>(group);
        AiMicroObjective& objective = state.objectives[group_index];
        const AiObservedUnit* member = nullptr;
        std::size_t member_index = 0;
        for (std::size_t index = 0; index < own.size(); ++index) {
            if (state.units[index].group == group) {
                member = own[index];
                member_index = index;
                break;
            }
        }
        if (member == nullptr) {
            objective.target_x = -1;
            objective.target_y = -1;
            continue;
        }
        bool repick = objective.target_x < 0;
        std::vector<u8> reachable;
        if (!repick && squared_distance(member->x, member->y, objective.target_x,
                objective.target_y) <= squared(config.arrival_radius)) {
            repick = true;
        } else if (!repick && group == AiMicroGroup::explorer &&
            context.frame - objective.sweep_pick_frame >=
                config.scout_repick_interval_frames) {
            // Re-pick when the target got revealed or became unreachable
            // (terrain is public, but a unit's reachable set is re-derived
            // from where it stands now).
            const u32 width = observation.map_width_tiles;
            const u32 tile_x = static_cast<u32>(objective.target_x) >> 5;
            const u32 tile_y = static_cast<u32>(objective.target_y) >> 5;
            const std::size_t index = static_cast<std::size_t>(tile_y) * width + tile_x;
            reachable = reachable_tiles(context, *member);
            if (width == 0 || index >= observation.tiles.size() ||
                observation.tiles[index].explored ||
                reachable.size() != observation.tiles.size() ||
                reachable[index] == 0) {
                repick = true;
            }
        }
        if (!repick) {
            continue;
        }
        objective.sweep_pick_frame = context.frame;
        if (reachable.empty()) {
            reachable = reachable_tiles(context, *member);
        }
        UnitMovementPoint destination{-1, -1};
        bool found = false;
        if (group == AiMicroGroup::explorer) {
            // v9 (user directive): enemy-base finding order.  The explorer
            // visits the UNEXPLORED START CANDIDATES first - that is where an
            // enemy base can actually be - and only once every candidate is
            // checked does it fall back to the generic frontier sweep.
            UnitMovementPoint start_candidate{-1, -1};
            if (pick_unexplored_start(context, member->x, member->y,
                    start_candidate)) {
                const u32 candidate_tile_x =
                    static_cast<u32>(std::max(start_candidate.x, 0)) >> 5;
                const u32 candidate_tile_y =
                    static_cast<u32>(std::max(start_candidate.y, 0)) >> 5;
                const std::size_t candidate_tile =
                    static_cast<std::size_t>(candidate_tile_y) *
                        observation.map_width_tiles + candidate_tile_x;
                if (candidate_tile < reachable.size() &&
                    reachable[candidate_tile] != 0) {
                    destination = start_candidate;
                    found = true;
                }
            }
            if (found) {
                ++context.explore_picks;
            } else if ((found = pick_frontier(context, member->x, member->y,
                    reachable, destination))) {
                ++context.explore_picks;
            } else {
                // Nothing reachable left to reveal: release the unit.
                AiMicroUnitRecord& record = state.units[member_index];
                record.group = default_group(AiMicroRoleOf(*member, config));
                record.state = AiMicroUnitState::normal;
                record.last_kind = AiSemanticActionKind::no_op;
                record.last_target_id = 0;
            }
        } else {
            found = pick_roam_point(context, reachable, state.roam_rng, destination);
            if (found) {
                ++context.roam_picks;
            }
        }
        objective.target_x = found ? destination.x : -1;
        objective.target_y = found ? destination.y : -1;
    }

    // ---- per-unit default behavior ---------------------------------------
    // v9 reflex overlay: the temporary defend objective the fighting groups
    // run under while a base threat stands.  defend_radius >= the defend
    // anchor rule, so the existing defend logic (bubble over every own
    // building, focus fire, melee spread, leash, low-hp pull-back) does the
    // fighting.  A raid joins only when it is already near the anchor.
    const AiMicroObjective reflex_objective = state.threat.active ?
        make_defend({state.threat.anchor_x, state.threat.anchor_y},
            config.defend_radius, state.threat.since_frame) :
        AiMicroObjective{};
    std::array<bool, kAiMicroGroupCount> raid_joins_reflex{};
    if (state.threat.active) {
        for (const AiMicroGroup raid_group : kAiMicroRaidGroups) {
            const std::size_t raid_index =
                static_cast<std::size_t>(raid_group);
            raid_joins_reflex[raid_index] =
                context.centroid_valid[raid_index] &&
                squared_distance(context.centroid[raid_index].x,
                    context.centroid[raid_index].y, state.threat.anchor_x,
                    state.threat.anchor_y) <=
                    squared(config.reflex_raid_join_radius);
        }
    }
    // Effective objective of a unit's group with the reflex overlay applied:
    // the defense detail (army) and any joined raid fight at the threat
    // anchor; a policy retreat is always respected.
    const auto effective_objective =
        [&](const AiMicroUnitRecord& record,
            u32 unit_id) -> const AiMicroObjective* {
        const AiMicroObjective* objective =
            &state.objectives[static_cast<std::size_t>(record.group)];
        if (state.threat.active &&
            objective->kind != AiMicroObjectiveKind::retreat &&
            ((record.group == AiMicroGroup::army &&
                std::find(state.reflex_defenders.begin(),
                    state.reflex_defenders.end(), unit_id) !=
                    state.reflex_defenders.end()) ||
                (AiMicroIsRaidGroup(record.group) &&
                    raid_joins_reflex[
                        static_cast<std::size_t>(record.group)]))) {
            objective = &reflex_objective;
        }
        return objective;
    };
    // ---- v10 meat-pickup pre-pass ------------------------------------------
    // One collector per drop, chosen over ALL eligible fighters instead of
    // first-come in unit-id order: fighters WITHOUT a held meat reserve
    // (action_mode == 0) first (user directive - a full unit wastes the
    // drop's healing), then the nearest.  Eligible = pickup-capable fighter
    // of a fighting group whose EFFECTIVE objective (incl. the reflex
    // overlay) is attack or defend, out of contact and not policy-held; a
    // defender is only assigned drops inside its bubble so pickup never
    // fights the leash.  The per-unit behaviors consume the assignment only
    // when they have nothing to fight in reach.
    if (!observation.map_effects.empty()) {
        struct MeatCollector {
            const AiObservedUnit* unit;
            const AiMicroObjective* objective;
            bool assigned;
        };
        std::vector<MeatCollector> collectors;
        for (std::size_t index = 0; index < own.size(); ++index) {
            const AiObservedUnit& unit = *own[index];
            const AiMicroUnitRecord& record = state.units[index];
            if ((unit.type_flags & 0x2u) == 0 ||
                (record.group != AiMicroGroup::army &&
                    !AiMicroIsRaidGroup(record.group))) {
                continue;
            }
            if (record.policy_hold_until_frame != 0 &&
                context.frame < record.policy_hold_until_frame) {
                continue;
            }
            const AiMicroObjective* objective =
                effective_objective(record, unit.id);
            if ((objective->kind != AiMicroObjectiveKind::attack &&
                    objective->kind != AiMicroObjectiveKind::defend) ||
                unit_in_contact(context, unit, 1)) {
                continue;
            }
            collectors.push_back({&unit, objective, false});
        }
        // v10.1: sticky assignments first - keep every standing (drop ->
        // collector) pair whose drop is still unclaimed and whose collector
        // is still eligible, so the choice cannot flap between two moving
        // units.  Only drops left over get a fresh collector.
        std::vector<std::pair<u32, u32>> kept_assignments;
        for (const std::pair<u32, u32>& assignment : state.meat_assignments) {
            const AiObservedMapEffect* effect = nullptr;
            for (const AiObservedMapEffect& candidate : observation.map_effects) {
                if (candidate.id == assignment.first && !candidate.linked) {
                    effect = &candidate;
                    break;
                }
            }
            if (effect == nullptr) {
                continue;
            }
            for (MeatCollector& collector : collectors) {
                if (collector.assigned ||
                    collector.unit->id != assignment.second) {
                    continue;
                }
                if (squared_distance(collector.unit->x, collector.unit->y,
                        effect->x, effect->y) >
                        squared(config.meat_pickup_radius)) {
                    break;  // walked out of range - release the pair
                }
                collector.assigned = true;
                kept_assignments.push_back(assignment);
                context.meat_assignments.push_back({collector.unit->id,
                    UnitMovementPoint{effect->x, effect->y}});
                break;
            }
        }
        state.meat_assignments = std::move(kept_assignments);
        for (const AiObservedMapEffect& effect : observation.map_effects) {
            if (effect.linked || collectors.empty()) {
                continue;
            }
            bool already_kept = false;
            for (const std::pair<u32, u32>& assignment :
                 state.meat_assignments) {
                if (assignment.first == effect.id) {
                    already_kept = true;
                    break;
                }
            }
            if (already_kept) {
                continue;
            }
            MeatCollector* best = nullptr;
            i64 best_gap = 0;
            for (MeatCollector& collector : collectors) {
                if (collector.assigned) {
                    continue;
                }
                if (collector.objective->kind == AiMicroObjectiveKind::defend &&
                    collector.objective->target_x >= 0 &&
                    squared_distance(effect.x, effect.y,
                        collector.objective->target_x,
                        collector.objective->target_y) >
                        squared(collector.objective->radius)) {
                    continue;
                }
                const i64 gap = squared_distance(collector.unit->x,
                    collector.unit->y, effect.x, effect.y);
                if (gap > squared(config.meat_pickup_radius)) {
                    continue;
                }
                if (best != nullptr) {
                    const bool best_empty = best->unit->action_mode == 0;
                    const bool empty = collector.unit->action_mode == 0;
                    if (best_empty != empty) {
                        if (best_empty) {
                            continue;
                        }
                    } else if (gap > best_gap ||
                        (gap == best_gap &&
                            collector.unit->id > best->unit->id)) {
                        continue;
                    }
                }
                best = &collector;
                best_gap = gap;
            }
            if (best != nullptr) {
                best->assigned = true;
                state.meat_assignments.push_back({effect.id, best->unit->id});
                context.meat_assignments.push_back({best->unit->id,
                    UnitMovementPoint{effect.x, effect.y}});
            }
        }
    }

    if (observation.map_effects.empty()) {
        state.meat_assignments.clear();
    }
    struct PendingOrder {
        u32 unit_id;
        DesiredOrder order;
    };
    std::vector<PendingOrder> pending;
    for (std::size_t index = 0; index < own.size(); ++index) {
        const AiObservedUnit& unit = *own[index];
        AiMicroUnitRecord& record = state.units[index];
        // Item 6 - movement is the only evidence the engine still holds our
        // order.  A unit whose order was dropped in a NON-idle state never
        // reaches the idle re-issue path below and would stall forever.
        if (record.last_issue_frame == 0xffffffffu ||
            std::abs(unit.x - record.last_position_x) >
                config.stuck_move_epsilon ||
            std::abs(unit.y - record.last_position_y) >
                config.stuck_move_epsilon) {
            record.last_position_x = unit.x;
            record.last_position_y = unit.y;
            record.stationary_since_frame = context.frame;
            record.stuck_jitter = 0;
        }
        if (record.policy_hold_until_frame != 0 &&
            context.frame < record.policy_hold_until_frame) {
            continue;
        }
        const AiMicroRole role = AiMicroRoleOf(unit, config);
        if (role == AiMicroRole::transport || role == AiMicroRole::building ||
            role == AiMicroRole::other) {
            continue;  // carriers are the drop autopilot's
        }
        // v10 attack commit: latch "this group met the enemy" on first weapon
        // contact of any fighter since the objective was set.
        if ((record.group == AiMicroGroup::army ||
                AiMicroIsRaidGroup(record.group)) &&
            (role == AiMicroRole::melee || role == AiMicroRole::ranged) &&
            state.group_engaged[static_cast<std::size_t>(record.group)] == 0 &&
            unit_in_contact(context, unit, 1)) {
            state.group_engaged[static_cast<std::size_t>(record.group)] = 1;
        }
        if (unit_is_constructing(unit)) {
            continue;
        }
        // v9 reflex: the fighting groups switch to the threat overlay while
        // it stands - unless the policy ordered a RETREAT (deliberate flight
        // is respected).  Workers/scouts keep their own rules (flee/evade).
        // v10: only the defense detail (reflex_defenders) responds; the rest
        // of the army keeps the policy's objective.  A joined raid fights
        // whole (it is already near the anchor).
        const AiMicroObjective* objective = effective_objective(record, unit.id);
        DesiredOrder order;
        if (record.group == AiMicroGroup::scout ||
            record.group == AiMicroGroup::berry_scout ||
            record.group == AiMicroGroup::explorer ||
            record.group == AiMicroGroup::roamer) {
            order = scout_order(context, record, unit, *objective);
        } else if (role == AiMicroRole::worker) {
            order = worker_order(context, state, record, unit, *objective);
        } else {
            order = fighter_order(context, state, record, unit, role,
                *objective);
        }
        if (order.kind == AiSemanticActionKind::no_op) {
            continue;
        }
        const bool stuck = is_move_order_kind(order.kind) &&
            record.last_issue_frame != 0xffffffffu &&
            context.frame - record.stationary_since_frame >=
                config.stuck_frames &&
            squared_distance(unit.x, unit.y, order.x, order.y) >
                squared(config.arrival_radius) &&
            !unit_in_contact(context, unit, 1);
        if (stuck) {
            // Rotate the offset on every attempt: re-sending the byte-identical
            // destination the engine already failed to path is unlikely to work
            // twice.  Seeded from the unit id so the choice stays deterministic.
            record.stuck_jitter = record.stuck_jitter == 0 ?
                1u + (unit.id % 8u) : 1u + (record.stuck_jitter % 8u);
        }
        if (record.stuck_jitter != 0 && is_move_order_kind(order.kind)) {
            // Hold the offset while the unit is still frozen: recomputing the
            // clean destination next frame would undo the nudge immediately.
            static const i32 kStuckOffsetX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
            static const i32 kStuckOffsetY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
            const std::size_t direction =
                static_cast<std::size_t>((record.stuck_jitter - 1u) % 8u);
            const UnitMovementPoint nudged = clamp_to_map(observation,
                order.x + kStuckOffsetX[direction] * config.stuck_jitter,
                order.y + kStuckOffsetY[direction] * config.stuck_jitter);
            order.x = nudged.x;
            order.y = nudged.y;
        }
        if (state.desired_order_tap != nullptr) {
            // Shadow teacher tap: the desired order BEFORE dedupe (plan
            // section 13.1).  Observing only changed packets would mislabel
            // a between-tick change as the next tick's KEEP.
            state.desired_order_tap->entries.push_back({unit.id, order.kind,
                order.target_id, order.x, order.y});
        }
        if (same_order(record, order)) {
            // Already told it.  Re-issue only if the engine dropped the order
            // (unit idle) or the unit is stuck, and the re-issue interval
            // passed.
            if ((!unit_is_idle(unit) && !stuck) ||
                (record.last_issue_frame != 0xffffffffu &&
                    context.frame - record.last_issue_frame <
                        config.reissue_interval_frames)) {
                continue;
            }
        }
        if (stuck) {
            // Charge the next stuck window from now, so the recovery fires on
            // a stuck_frames cadence instead of every reissue interval.
            record.stationary_since_frame = context.frame;
            ++state.stuck_reissues;
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
        // Harvest orders stay one unit each (per-tile assignment).  Batching
        // them was measured and reverted: workers go idle on different frames,
        // so same-tile orders almost never coincide in one step (10895-frame
        // headless run: 5738 -> 5727 actions).
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
    state.unattackable_targets_skipped += context.unattackable_skipped;
    state.meat_pickup_orders += context.meat_orders;
    state.hunt_unreachable_skipped += context.hunt_unreachable;
    state.cohesion_holds += context.cohesion_holds;
    state.scout_sweep_picks += context.scout_picks;
    state.search_sweep_picks += context.search_picks;
    state.explore_picks += context.explore_picks;
    state.roam_picks += context.roam_picks;
    return actions;
}

} // namespace ranker
