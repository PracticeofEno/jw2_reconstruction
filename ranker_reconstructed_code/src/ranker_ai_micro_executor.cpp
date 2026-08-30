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
    std::array<u32, kAiMicroGroupCount> group_attackable_mask{};
    // Diagnostics folded back into the executor state at the end of the step.
    u32 unattackable_skipped = 0;
    u32 cohesion_holds = 0;
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
const AiObservedUnit* pick_group_target(const StepContext& context,
    AiMicroAttackTactic tactic, i32 x, i32 y, u32 attackable_mask) {
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
        return nearest_of(context.neutrals, true);
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
            if (context.centroid_valid[group_index] &&
                !unit_in_contact(context, unit, 1)) {
                const UnitMovementPoint centroid =
                    context.centroid[group_index];
                const i64 unit_gap = squared_distance(unit.x, unit.y,
                    centroid.x, centroid.y);
                const i64 unit_to_goal = squared_distance(unit.x, unit.y,
                    objective.target_x, objective.target_y);
                const i64 group_to_goal = squared_distance(centroid.x,
                    centroid.y, objective.target_x, objective.target_y);
                if (unit_gap > squared(config.cohesion_radius) &&
                    unit_to_goal < group_to_goal) {
                    ++context.cohesion_holds;
                    return point_order(AiSemanticActionKind::move, centroid.x,
                        centroid.y);
                }
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
            // is not a threat and must not scatter the economy.
            if (hostile->type_id < kMobileTypeLimit &&
                can_engage(*hostile, unit, context.config) &&
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

// Tiles a unit can reach: everything for a flyer; for a ground unit the
// passable tiles 4-connected to the tile it stands on (terrain is public, so
// unexplored passable ground counts).  Empty vector = map invalid.
std::vector<u8> reachable_tiles(const StepContext& context,
    const AiObservedUnit& unit) {
    const AiObservation& observation = context.observation;
    const u32 width = observation.map_width_tiles;
    const u32 height = observation.map_height_tiles;
    std::vector<u8> reachable;
    if (width == 0 || height == 0 ||
        observation.tiles.size() != static_cast<std::size_t>(width) * height) {
        return reachable;
    }
    reachable.assign(observation.tiles.size(), 0u);
    if (unit.render_class == context.config.flying_render_class) {
        std::fill(reachable.begin(), reachable.end(), 1u);
        return reachable;
    }
    const u32 start_x = static_cast<u32>(std::max(unit.x, 0)) >> 5;
    const u32 start_y = static_cast<u32>(std::max(unit.y, 0)) >> 5;
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
    }

    // ---- objective transitions (default behavior, not decisions) -----------
    AiMicroObjective& economy =
        state.objectives[static_cast<std::size_t>(AiMicroGroup::economy)];
    AiMicroObjective& army =
        state.objectives[static_cast<std::size_t>(AiMicroGroup::army)];
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
    const std::size_t army_index = static_cast<std::size_t>(AiMicroGroup::army);
    const UnitMovementPoint army_centroid = context.centroid_valid[army_index] ?
        context.centroid[army_index] : home;
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
    if (army.kind == AiMicroObjectiveKind::attack) {
        // The policy chose a TACTIC; the group's target is re-derived from it
        // every frame.  Keep the current target while it is still valid and of
        // the preferred class (stability); switch when it dies, leaves sight,
        // or a preferred-class target appears while we hold a fallback one.
        const u32 army_mask = context.group_attackable_mask[army_index];
        const AiObservedUnit* current =
            find_unit(observation, army.target_unit_id);
        // Item 1 - a target no army member can engage is not a valid target.
        // Leaving it set parks the whole group on an order the engine rejects.
        const bool engageable = current != nullptr &&
            (current->render_class >= 32u ||
                (army_mask & (1u << current->render_class)) != 0);
        bool keep = current != nullptr && current->alive && current->visible &&
            engageable && tactic_accepts(context, army.tactic, *current);
        const AiObservedUnit* best = pick_group_target(context, army.tactic,
            army_centroid.x, army_centroid.y, army_mask);
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
                army_centroid.x, army_centroid.y);
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
            found = pick_frontier(context, member->x, member->y, reachable,
                destination);
            if (found) {
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
        if (unit_is_constructing(unit)) {
            continue;
        }
        const AiMicroObjective& objective =
            state.objectives[static_cast<std::size_t>(record.group)];
        DesiredOrder order;
        if (record.group == AiMicroGroup::scout ||
            record.group == AiMicroGroup::berry_scout ||
            record.group == AiMicroGroup::explorer ||
            record.group == AiMicroGroup::roamer) {
            order = scout_order(context, record, unit, objective);
        } else if (role == AiMicroRole::worker) {
            order = worker_order(context, state, record, unit, objective);
        } else {
            order = fighter_order(context, state, record, unit, role, objective);
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
    state.cohesion_holds += context.cohesion_holds;
    state.scout_sweep_picks += context.scout_picks;
    state.search_sweep_picks += context.search_picks;
    state.explore_picks += context.explore_picks;
    state.roam_picks += context.roam_picks;
    return actions;
}

} // namespace ranker
