#pragma once

#include "ranker_unit_movement.h"

#include <array>
#include <vector>

namespace ranker {

constexpr u32 kUnitSpatialIndexCapacity = 0x800;
constexpr i32 kInvalidUnitSpatialSortedIndex = -1;

enum class UnitSpatialIndexBuildMode : u32 {
    all_active_units = 0,
    non_structure_non_terminal_command_units = 1,
    non_structure_active_command_units = 2,
    lifecycle_class3_units = 3,
};

struct UnitSpatialIndexEntry {
    i32 x = 0;
    UnitMovementUnit* unit = nullptr;
};

struct UnitSpatialQueryEntry {
    u32 distance = 0;
    UnitMovementUnit* unit = nullptr;
};

struct UnitSpatialIndex {
    u32 capacity = kUnitSpatialIndexCapacity;
    std::vector<UnitSpatialIndexEntry> sorted_units;
    std::vector<i32> unit_to_sorted_index;
    std::vector<UnitSpatialQueryEntry> query_results;
    bool query_active = false;
    u32 query_cursor = 0;
    u32 last_query_distance = 0xffffffffu;
};

struct UnitSpatialIndexSet {
    UnitSpatialIndex all_units;
    UnitSpatialIndex non_terminal_units;
    UnitSpatialIndex active_command_units;
    UnitSpatialIndex lifecycle_class3_units;
};

void InitializeUnitSpatialIndex(UnitSpatialIndex& index,
    u32 capacity = kUnitSpatialIndexCapacity);
void ShutdownUnitSpatialIndex(UnitSpatialIndex& index);
void SortUnitSpatialIndexEntriesByX(std::vector<UnitSpatialIndexEntry>& entries);
void RebuildUnitSpatialIndex(UnitSpatialIndex& index,
    const std::vector<UnitMovementUnit*>& active_units,
    UnitSpatialIndexBuildMode mode,
    const std::vector<u32>* command_state_categories = nullptr);
UnitMovementUnit* QueryUnitSpatialIndexRadius(UnitSpatialIndex& index,
    UnitMovementUnit* source_unit, i32 x, i32 y, u32 radius);
UnitMovementUnit* QueryUnitSpatialIndexRelativeBox(UnitSpatialIndex& index,
    UnitMovementUnit* source_unit, i32 left, i32 right, i32 top, i32 bottom);
UnitMovementUnit* NextUnitSpatialIndexQueryResult(UnitSpatialIndex& index);

void InitializeUnitSpatialIndexSet(UnitSpatialIndexSet& indexes);
void ShutdownUnitSpatialIndexSet(UnitSpatialIndexSet& indexes);
void RebuildUnitSpatialIndexSet(UnitSpatialIndexSet& indexes,
    const UnitMovementContext& context,
    const std::vector<u32>* command_state_categories = nullptr);
void AllocateNonTerminalUnitSpatialIndexStorage(UnitSpatialIndexSet& indexes);
void RegisterNonTerminalUnitSpatialIndexShutdown(UnitSpatialIndexSet& indexes);
void ShutdownNonTerminalUnitSpatialIndex(UnitSpatialIndexSet& indexes);
void AllocateActiveCommandUnitSpatialIndexStorage(UnitSpatialIndexSet& indexes);
void RegisterActiveCommandUnitSpatialIndexShutdown(UnitSpatialIndexSet& indexes);
void ShutdownActiveCommandUnitSpatialIndex(UnitSpatialIndexSet& indexes);
void AllocateLifecycleClass3UnitSpatialIndexStorage(UnitSpatialIndexSet& indexes);
void RegisterLifecycleClass3UnitSpatialIndexShutdown(UnitSpatialIndexSet& indexes);
void ShutdownLifecycleClass3UnitSpatialIndex(UnitSpatialIndexSet& indexes);

UnitMovementUnit* QueryAllUnitsSpatialIndexAroundUnit(
    UnitSpatialIndexSet& indexes, UnitMovementUnit& unit, u32 radius);
UnitMovementUnit* QueryAllUnitsSpatialIndexAroundPoint(
    UnitSpatialIndexSet& indexes, i32 x, i32 y, u32 radius);
UnitMovementUnit* NextAllUnitsSpatialIndexResult(UnitSpatialIndexSet& indexes);
UnitMovementUnit* QueryNonTerminalUnitSpatialBox(UnitSpatialIndexSet& indexes,
    UnitMovementUnit& unit);
UnitMovementUnit* QueryActiveCommandUnitSpatialBox(UnitSpatialIndexSet& indexes,
    UnitMovementUnit& unit);
UnitMovementUnit* QueryLifecycleClass3UnitSpatialBox(UnitSpatialIndexSet& indexes,
    UnitMovementUnit& unit);

} // namespace ranker
