#pragma once

#include "ranker_unit_movement.h"

#include <array>

namespace ranker {

constexpr u32 kUnitFootprintOccupied = 0x20000000;
constexpr u32 kUnitFootprintSpecialOccupied = 0x40000000;
constexpr u32 kUnitFootprintUnderConstruction = 0x80000000;
constexpr u32 kUnitFootprintTypeMask = 0x000000ff;
constexpr u32 kUnitFootprintOwnerShift = 8;
constexpr u32 kUnitOwnerTypeCountOwners = 8;
constexpr u32 kUnitOwnerTypeCountTypes = 0xaa;

enum class UnitProductionRequirementCode : u32 {
    ok = 0x0f,
    missing_primary_resource = 0,
    missing_secondary_resource = 1,
    missing_prerequisite = 2,
    population_limit = 3,
    population_reserved_base = 0x0b,
};

struct UnitLifecycleContext;

using UnitLifecycleCallback = void (*)(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
using UnitLifecyclePhaseCallback = void (*)(UnitLifecycleContext& context);
using UnitLifecyclePlacementCallback = bool (*)(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32& x, i32& y);
using UnitLifecycleDefinitionCallback = const UnitMovementDefinition* (*)(
    UnitLifecycleContext& context, u32 type_id);
using UnitLifecycleRandomLimitCallback = u32 (*)(UnitLifecycleContext& context,
    u32 limit);

struct UnitLifecycleCallbacks {
    UnitLifecycleCallback on_unit_became_active = nullptr;
    UnitLifecycleCallback on_unit_lifecycle_removed = nullptr;
    UnitLifecycleCallback on_footprint_changed = nullptr;
    UnitLifecyclePhaseCallback on_before_active_simulation = nullptr;
    UnitLifecycleCallback on_active_unit_runtime_dispatch = nullptr;
    UnitLifecyclePlacementCallback find_placement = nullptr;
    UnitLifecycleDefinitionCallback find_definition = nullptr;
    UnitLifecycleRandomLimitCallback random_limit = nullptr;
};

struct UnitLifecycleContext {
    UnitMovementContext* movement = nullptr;
    UnitLifecycleCallbacks callbacks;
    std::array<u32, 16> owner_primary_resources{};
    std::array<u32, 16> owner_secondary_resources{};
    std::array<u32, 16> owner_population_limit{};
    std::array<u32, 16> owner_population_used{};
    std::array<u32, 16> owner_population_reserved{};
    std::array<u32, 16> owner_faction_ids{};
    std::array<u32, 16> owner_unit_active_count{};
    std::array<u32, 16> owner_building_active_count{};
    std::array<u32, 16> owner_unit_completed_count{};
    std::array<u32, 16> owner_building_completed_count{};
    std::array<u32, 16> owner_unit_lost_count{};
    std::array<u32, 16> owner_building_lost_count{};
    std::array<u32, 16> owner_unit_score{};
    std::array<u32, 16> owner_building_score{};
    std::array<std::array<u32, kUnitOwnerTypeCountTypes>, kUnitOwnerTypeCountOwners>
        owner_unit_type_counts{};
    u32 placement_terrain_class_override = 0;
    bool placement_terrain_class_override_enabled = false;
};

void HandleUnitCreationRegisterFootprint(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
bool CheckOwnerResourceAdjustmentGate(const UnitLifecycleContext& context,
    u32 owner_id, u32 amount);
bool HandleOwnerPrimaryResourceSpendIfAllowed(UnitLifecycleContext& context,
    u32 owner_id, u32 amount);
void HandleUnitPrimaryResourceCostRefund(UnitLifecycleContext& context,
    UnitMovementUnit& unit, u32 type_id);
void HandleUnitLifecycleDispatchListTick(UnitLifecycleContext& context);
void HandleUnitLifecycleGrowthOrDecay(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
bool HandleUnitConstructionActivation(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
void HandleUnitLifecycleDecayTimer(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
bool HandleUnitLifecycleTimedRemoval(UnitMovementUnit& unit, u32 duration);
void HandleUnitSimulationListTick(UnitLifecycleContext& context);
void HandleOwnerPopulationReservationTotals(UnitLifecycleContext& context);
void HandleUnitDeathLifecycleTransition(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
void SetUnitFootprintOccupancyBits(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
void ClearUnitFootprintOccupancyBits(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
UnitProductionRequirementCode CheckUnitProductionRequirements(
    UnitLifecycleContext& context, u32 owner_id, u32 type_id);
bool CheckUnitProductionPrerequisites(UnitLifecycleContext& context,
    u32 owner_id, u32 type_id);
bool InitializePlacedUnitFromMapSlot(UnitLifecycleContext& context,
    UnitMovementUnit& unit, u32 type_id, u32 owner_id, i32 x, i32 y);
bool FindStrictUnitPlacementPoint(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32& x, i32& y);
bool FindMatchingTerrainUnitPlacementPoint(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32& x, i32& y);
u32 GetPlacementTerrainClass(UnitLifecycleContext& context, u32 tile_x, u32 tile_y);
bool CheckPlacementFootprintCell(UnitLifecycleContext& context,
    UnitMovementUnit& unit, u32 tile_x, u32 tile_y, u32 terrain_class,
    bool require_matching_terrain);
bool CheckUnitPlacementFootprintArea(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32 x, i32 y, bool require_matching_terrain);
bool FindNearbyPassablePlacementTile(UnitLifecycleContext& context, u32 tile_x,
    u32 tile_y, u32& out_tile_x, u32& out_tile_y);
void HandleUnitRemovalAccounting(UnitLifecycleContext& context, UnitMovementUnit& unit);
void HandleUnitCompletionOwnerCounters(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
void HandleUnitDeathOwnerCounters(UnitLifecycleContext& context,
    UnitMovementUnit& unit);
void HandleOwnerUnitTypeCountRebuild(UnitLifecycleContext& context);

} // namespace ranker
