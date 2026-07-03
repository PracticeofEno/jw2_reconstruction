#pragma once

#include "ranker_production_orders.h"
#include "ranker_unit_movement.h"

#include <array>

namespace ranker {

constexpr u32 kUnitTargetHelperRuntimeSkipMask = 0x20000080;
constexpr u32 kUnitTargetHelperTransientFlag = 0x00000004;
constexpr u32 kUnitTargetHelperOneTileDistance = 0x20;
constexpr u32 kUnitTargetHelperSpecialSpawnType = 0x10;
constexpr u32 kUnitTargetHelperEliteTypeBase = 0x60;

struct UnitTargetHelperContext;

using UnitTargetHelperCanTargetCallback = bool (*)(UnitTargetHelperContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& candidate);
using UnitTargetHelperDistanceCallback = u32 (*)(UnitTargetHelperContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& candidate);
using UnitTargetHelperCallback = void (*)(UnitTargetHelperContext& context,
    UnitMovementUnit& unit);
using UnitTargetHelperPairCallback = void (*)(UnitTargetHelperContext& context,
    UnitMovementUnit& source, UnitMovementUnit& target);

struct UnitTargetHelperCallbacks {
    UnitTargetHelperCanTargetCallback can_target = nullptr;
    UnitTargetHelperDistanceCallback distance_to_target = nullptr;
    UnitTargetHelperPairCallback on_target_progress = nullptr;
    UnitTargetHelperPairCallback on_target_progress_complete = nullptr;
    UnitTargetHelperCallback on_local_progress_blocked = nullptr;
};

struct UnitTargetHelperContext {
    UnitMovementContext* movement_context = nullptr;
    UnitTargetHelperCallbacks callbacks;
    std::array<u32, kProductionOrderOwnerCount> source_owner_masks{};
    std::array<u32, kProductionOrderOwnerCount> target_owner_masks{};
    std::array<u32, kProductionOrderOwnerCount> owner_primary_progress{};
    std::array<u32, kProductionOrderOwnerCount> owner_secondary_progress{};
    u32 local_owner_id = 0;
};

struct UnitTargetProgressResult {
    bool blocked = false;
    bool completed = false;
};

UnitMovementPoint CalculateUnitCenterPoint(const UnitMovementUnit& unit);
UnitMovementPoint CalculateCurrentUnitCenterPoint(const UnitMovementUnit& unit);
u32 CalculateUnitCenterDistance(const UnitMovementUnit& source,
    const UnitMovementUnit& target);
u32 CalculateCurrentTargetCenterDistance(const UnitMovementUnit& source,
    const UnitMovementUnit& target);
bool CheckUnitTargetOwnerMask(UnitTargetHelperContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
UnitMovementUnit* FindNearestHostileActionTargetByActiveList(
    UnitTargetHelperContext& context, const UnitMovementUnit& source);
bool CheckCurrentTargetBelowStoredHealthCap(const UnitMovementUnit& source);
bool CheckCurrentTargetOutsideExpandedFootprint(UnitMovementUnit& source);
bool CheckTargetInteractionNeedsApproach(UnitMovementUnit& source);
UnitTargetProgressResult ApplyCurrentTargetBuildOrRepairProgress(
    UnitTargetHelperContext& context, UnitMovementUnit& source);
bool CheckCurrentTargetWithinAxisOneTile(const UnitMovementUnit& source);
bool CheckUnitPairApproxDistanceWithinOneTile(const UnitMovementUnit& source,
    const UnitMovementUnit& target);
bool CheckPathTargetEqualsCurrentPosition(const UnitMovementUnit& source);

}
