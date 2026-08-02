#include "ranker_unit_target_helpers.h"

#include "ranker_unit_damage.h"

#include <algorithm>
#include <cstdlib>

namespace ranker {
namespace {

bool has_owner_masks(const UnitTargetHelperContext& context) {
    const auto has_value = [](const auto& values) {
        return std::any_of(values.begin(), values.end(), [](u32 value) {
            return value != 0;
        });
    };
    return has_value(context.source_owner_masks) || has_value(context.target_owner_masks);
}

bool has_owner_index(u32 owner_id) {
    return owner_id < kProductionOrderOwnerCount;
}

bool target_active_for_helper(const UnitMovementUnit& target) {
    return target.active &&
        (target.runtime_flags & kUnitTargetHelperRuntimeSkipMask) == 0;
}

bool debit_owner_progress(std::array<u32, kProductionOrderOwnerCount>& counters,
    u32 owner_id) {
    if (!has_owner_index(owner_id)) {
        return true;
    }
    if (counters[owner_id] == 0) {
        return false;
    }
    --counters[owner_id];
    return true;
}

void refund_owner_progress(std::array<u32, kProductionOrderOwnerCount>& counters,
    u32 owner_id) {
    if (has_owner_index(owner_id)) {
        ++counters[owner_id];
    }
}

UnitMovementPoint target_center_without_source_offset(const UnitMovementUnit& target) {
    return UnitMovementPoint{
        target.x + (target.definition.interaction_bounds_width >> 1),
        target.y + (target.definition.interaction_bounds_height >> 1),
    };
}

} // namespace

UnitMovementPoint CalculateUnitCenterPoint(const UnitMovementUnit& unit) {
    return UnitMovementPoint{
        unit.x + unit.definition.center_bounds_left +
            (unit.definition.center_bounds_width >> 1),
        unit.y + unit.definition.center_bounds_top +
            (unit.definition.center_bounds_height >> 1),
    };
}

UnitMovementPoint CalculateCurrentUnitCenterPoint(const UnitMovementUnit& unit) {
    return CalculateUnitCenterPoint(unit);
}

u32 CalculateUnitCenterToCenterDirection(const UnitMovementContext* movement,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    const UnitMovementPoint source_center = CalculateUnitCenterPoint(source);
    const UnitMovementPoint target_center = CalculateUnitCenterPoint(target);
    if (movement != nullptr && movement->direction_lookup_8 != nullptr) {
        return CalculatePointDirectionFromLookup(
            source_center, target_center, *movement->direction_lookup_8);
    }

    UnitMovementUnit direction_source = source;
    direction_source.x = source_center.x;
    direction_source.y = source_center.y;
    return CalculateUnitDirectionToPoint(
        direction_source, target_center.x, target_center.y);
}

u32 CalculateUnitCenterDistance(const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    const UnitMovementPoint source_center = CalculateUnitCenterPoint(source);
    const UnitMovementPoint target_center = CalculateUnitCenterPoint(target);
    return CalculateApproxUnitDistance(source_center.x, source_center.y,
        target_center.x, target_center.y);
}

u32 CalculateCurrentTargetCenterDistance(const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    return CalculateUnitCenterDistance(source, target);
}

bool CheckUnitTargetOwnerMask(UnitTargetHelperContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    if (has_owner_index(source.owner_id) && has_owner_index(target.owner_id) &&
        has_owner_masks(context)) {
        return (context.source_owner_masks[source.owner_id] &
            context.target_owner_masks[target.owner_id]) != 0;
    }
    return source.owner_id == target.owner_id;
}

UnitMovementUnit* FindNearestHostileActionTargetByActiveList(
    UnitTargetHelperContext& context, const UnitMovementUnit& source) {
    if (context.movement_context == nullptr) {
        return nullptr;
    }

    UnitMovementUnit* best = nullptr;
    u32 best_distance = 0xffffffffu;
    for (UnitMovementUnit* candidate : context.movement_context->active_units) {
        if (candidate == nullptr || candidate == &source ||
            !target_active_for_helper(*candidate)) {
            continue;
        }
        if (CheckUnitTargetOwnerMask(context, source, *candidate)) {
            continue;
        }
        if (context.callbacks.can_target != nullptr &&
            !context.callbacks.can_target(context, source, *candidate)) {
            continue;
        }

        const u32 distance = context.callbacks.distance_to_target != nullptr
            ? context.callbacks.distance_to_target(context, source, *candidate)
            : CalculateApproxUnitDistance(source.x, source.y, candidate->x,
                  candidate->y);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

bool CheckCurrentTargetBelowStoredHealthCap(const UnitMovementUnit& source) {
    const UnitMovementUnit* target = source.target;
    return target != nullptr &&
        (target->runtime_flags & kUnitTargetHelperTransientFlag) == 0 &&
        target->health < target->max_health;
}

bool CheckCurrentTargetOutsideExpandedFootprint(UnitMovementUnit& source) {
    UnitMovementUnit* target = source.target;
    if (target == nullptr || source.distance_check_mode == 1) {
        return true;
    }

    if (source.type_id == kUnitTargetHelperSpecialSpawnType) {
        const UnitMovementPoint center = target_center_without_source_offset(*target);
        source.path_target_x = center.x;
        source.path_target_y = center.y;
        return CalculateApproxUnitDistance(source.x, source.y, center.x, center.y) >
            kUnitTargetHelperOneTileDistance;
    }

    const i32 source_left = source.x + source.definition.interaction_bounds_left * 3;
    const i32 source_top = source.y + source.definition.interaction_bounds_top * 3;
    const i32 source_right =
        source_left + source.definition.interaction_bounds_width * 3;
    const i32 source_bottom =
        source_top + source.definition.interaction_bounds_height * 3;
    const i32 target_left = target->x + target->definition.interaction_bounds_left;
    const i32 target_top = target->y + target->definition.interaction_bounds_top;
    const i32 target_right =
        target_left + target->definition.interaction_bounds_width;
    const i32 target_bottom =
        target_top + target->definition.interaction_bounds_height;

    return source_right < target_left || source_bottom < target_top ||
        target_right < source_left || target_bottom < source_top;
}

bool CheckTargetInteractionNeedsApproach(UnitMovementUnit& source) {
    UnitMovementUnit* target = source.target;
    if (target == nullptr) {
        return CalculateApproxUnitDistance(source.x, source.y, source.path_target_x,
            source.path_target_y) >= kUnitTargetHelperOneTileDistance + 1;
    }

    if (target->type_id == 0x60 || target->type_id == 0x70 ||
        target->type_id == 0x80 || target->type_id == 0x90) {
        return CheckCurrentTargetOutsideExpandedFootprint(source);
    }

    return CalculateApproxUnitDistance(source.x, source.y, target->x, target->y) >=
        kUnitTargetHelperOneTileDistance + 1;
}

UnitTargetProgressResult ApplyCurrentTargetBuildOrRepairProgress(
    UnitTargetHelperContext& context, UnitMovementUnit& source) {
    UnitTargetProgressResult result;
    source.command_flags &= ~0x7u;
    UnitMovementUnit* target = source.target;
    if (target == nullptr) {
        result.blocked = true;
        return result;
    }

    const u32 owner = source.owner_id;
    if (target->definition.target_progress_primary_cost != 0 &&
        !debit_owner_progress(context.owner_primary_progress, owner)) {
        result.blocked = true;
        if (owner == context.local_owner_id &&
            context.callbacks.on_local_progress_blocked != nullptr) {
            context.callbacks.on_local_progress_blocked(context, source);
        }
        return result;
    }

    if (target->definition.target_progress_secondary_cost != 0 &&
        !debit_owner_progress(context.owner_secondary_progress, owner)) {
        refund_owner_progress(context.owner_primary_progress, owner);
        result.blocked = true;
        if (owner == context.local_owner_id &&
            context.callbacks.on_local_progress_blocked != nullptr) {
            context.callbacks.on_local_progress_blocked(context, source);
        }
        return result;
    }

    if (target->type_id >= kUnitTargetHelperEliteTypeBase &&
        target->action_mode_gate == 1) {
        // FUN_004c3504 (0x004c3626..0x004c365c) advances a structure under
        // construction through raw +0x2c and accumulates definition +0x154 in
        // raw +0x34.  Those are action_mode and generic equipment/item slot 1,
        // not the elite level/progress words at raw +0x54/+0x50.
        ++target->action_mode;
        target->equipment_slots[1] += target->definition.initial_max_health;
        target->item_slots[1] = target->equipment_slots[1];
    }
    else {
        target->health = std::min(target->health + 2, target->max_health);
        result.completed = target->health >= target->max_health;
    }

    if (context.callbacks.on_target_progress != nullptr) {
        context.callbacks.on_target_progress(context, source, *target);
    }
    if (result.completed && context.callbacks.on_target_progress_complete != nullptr) {
        context.callbacks.on_target_progress_complete(context, source, *target);
    }
    return result;
}

bool CheckCurrentTargetWithinAxisOneTile(const UnitMovementUnit& source) {
    const UnitMovementUnit* target = source.target;
    if (target == nullptr || target->distance_check_mode == 1) {
        return false;
    }
    return std::abs(source.x - target->x) <=
        static_cast<i32>(kUnitTargetHelperOneTileDistance) &&
        std::abs(source.y - target->y) <=
        static_cast<i32>(kUnitTargetHelperOneTileDistance);
}

bool CheckUnitPairApproxDistanceWithinOneTile(const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    return CalculateApproxUnitDistance(source.x, source.y, target.x, target.y) <=
        kUnitTargetHelperOneTileDistance;
}

bool CheckPathTargetEqualsCurrentPosition(const UnitMovementUnit& source) {
    return source.path_target_x == source.x && source.path_target_y == source.y;
}

} // namespace ranker
