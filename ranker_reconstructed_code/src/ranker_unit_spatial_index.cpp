#include "ranker_unit_spatial_index.h"

#include "ranker_unit_damage.h"

#include <algorithm>
#include <limits>

namespace ranker {
namespace {

u32 runtime_command_id(const UnitMovementUnit& unit) {
    constexpr u32 kRuntimeCommandMask = 0x00ffffffu;
    return unit.command_state & kRuntimeCommandMask;
}

u32 command_category(const UnitMovementUnit& unit,
    const std::vector<u32>* command_state_categories) {
    const u32 command_id = runtime_command_id(unit);
    if (command_state_categories != nullptr &&
        command_id < command_state_categories->size()) {
        return (*command_state_categories)[command_id];
    }
    return std::numeric_limits<u32>::max();
}

bool include_unit_for_mode(const UnitMovementUnit& unit,
    UnitSpatialIndexBuildMode mode,
    const std::vector<u32>* command_state_categories) {
    switch (mode) {
    case UnitSpatialIndexBuildMode::all_active_units:
        return true;
    case UnitSpatialIndexBuildMode::non_structure_non_terminal_command_units:
        if (unit.definition.lifecycle_class == 3 ||
            unit.definition.lifecycle_class == 1) {
            return false;
        }
        return command_category(unit, command_state_categories) != 8 &&
            command_category(unit, command_state_categories) != 0x0e;
    case UnitSpatialIndexBuildMode::non_structure_active_command_units:
        if (unit.definition.lifecycle_class == 3 ||
            unit.definition.lifecycle_class == 1 ||
            unit.command_state == 1) {
            return false;
        }
        return command_category(unit, command_state_categories) != 8;
    case UnitSpatialIndexBuildMode::lifecycle_class3_units:
        return unit.definition.lifecycle_class == 3;
    }
    return false;
}

void reset_query(UnitSpatialIndex& index) {
    index.query_results.clear();
    index.query_active = false;
    index.query_cursor = 0;
    index.last_query_distance = 0xffffffffu;
}

u32 unit_slot_index(const UnitMovementUnit& unit) {
    return unit.id;
}

void rebuild_slot_map(UnitSpatialIndex& index) {
    std::fill(index.unit_to_sorted_index.begin(), index.unit_to_sorted_index.end(),
        kInvalidUnitSpatialSortedIndex);
    for (std::size_t sorted_index = 0; sorted_index < index.sorted_units.size();
         ++sorted_index) {
        UnitMovementUnit* unit = index.sorted_units[sorted_index].unit;
        if (unit == nullptr) {
            continue;
        }
        const u32 slot = unit_slot_index(*unit);
        if (slot < index.unit_to_sorted_index.size()) {
            index.unit_to_sorted_index[slot] = static_cast<i32>(sorted_index);
        }
    }
}

i32 find_sorted_index(const UnitSpatialIndex& index, const UnitMovementUnit& unit) {
    const u32 slot = unit_slot_index(unit);
    if (slot < index.unit_to_sorted_index.size()) {
        const i32 mapped = index.unit_to_sorted_index[slot];
        if (mapped >= 0 && static_cast<std::size_t>(mapped) < index.sorted_units.size() &&
            index.sorted_units[static_cast<std::size_t>(mapped)].unit == &unit) {
            return mapped;
        }
    }

    const auto it = std::find_if(index.sorted_units.begin(), index.sorted_units.end(),
        [&unit](const UnitSpatialIndexEntry& entry) {
            return entry.unit == &unit;
        });
    if (it == index.sorted_units.end()) {
        return kInvalidUnitSpatialSortedIndex;
    }
    return static_cast<i32>(std::distance(index.sorted_units.begin(), it));
}

std::size_t first_index_with_x_at_least(const UnitSpatialIndex& index, i32 x) {
    const auto it = std::lower_bound(index.sorted_units.begin(),
        index.sorted_units.end(), x,
        [](const UnitSpatialIndexEntry& entry, i32 value) {
            return entry.x < value;
        });
    return static_cast<std::size_t>(std::distance(index.sorted_units.begin(), it));
}

std::size_t last_index_with_x_at_most(const UnitSpatialIndex& index, i32 x) {
    const auto it = std::upper_bound(index.sorted_units.begin(),
        index.sorted_units.end(), x,
        [](i32 value, const UnitSpatialIndexEntry& entry) {
            return value < entry.x;
        });
    if (it == index.sorted_units.begin()) {
        return index.sorted_units.size();
    }
    return static_cast<std::size_t>(std::distance(index.sorted_units.begin(), it - 1));
}

UnitMovementUnit* activate_query(UnitSpatialIndex& index, bool sort_by_distance) {
    if (index.query_results.empty()) {
        index.query_active = false;
        index.query_cursor = 0;
        index.last_query_distance = 0xffffffffu;
        return nullptr;
    }

    if (sort_by_distance) {
        std::sort(index.query_results.begin(), index.query_results.end(),
            [](const UnitSpatialQueryEntry& left, const UnitSpatialQueryEntry& right) {
                return left.distance < right.distance;
            });
    }

    index.query_active = true;
    index.query_cursor = 0;
    index.last_query_distance = index.query_results.front().distance;
    return index.query_results.front().unit;
}

UnitMovementUnit* query_relative_box(UnitSpatialIndex& index,
    UnitMovementUnit& source_unit, i32 min_x, i32 max_x, i32 min_y, i32 max_y) {
    reset_query(index);
    if (find_sorted_index(index, source_unit) == kInvalidUnitSpatialSortedIndex) {
        return nullptr;
    }

    const std::size_t first = first_index_with_x_at_least(index, min_x);
    const std::size_t last = last_index_with_x_at_most(index, max_x);
    if (first >= index.sorted_units.size() || last >= index.sorted_units.size() ||
        first > last) {
        return nullptr;
    }

    for (std::size_t sorted_index = first; sorted_index <= last; ++sorted_index) {
        UnitMovementUnit* candidate = index.sorted_units[sorted_index].unit;
        if (candidate == nullptr || candidate == &source_unit) {
            continue;
        }
        if (candidate->y < min_y || candidate->y >= max_y) {
            continue;
        }
        index.query_results.push_back(UnitSpatialQueryEntry{0, candidate});
    }
    return activate_query(index, false);
}

} // namespace

void InitializeUnitSpatialIndex(UnitSpatialIndex& index, u32 capacity) {
    index.capacity = capacity == 0 ? kUnitSpatialIndexCapacity : capacity;
    index.sorted_units.clear();
    index.sorted_units.reserve(index.capacity);
    index.unit_to_sorted_index.assign(index.capacity, kInvalidUnitSpatialSortedIndex);
    index.query_results.clear();
    index.query_results.reserve(index.capacity);
    index.query_active = false;
    index.query_cursor = 0;
    index.last_query_distance = 0xffffffffu;
}

void ShutdownUnitSpatialIndex(UnitSpatialIndex& index) {
    index.sorted_units.clear();
    index.unit_to_sorted_index.clear();
    index.query_results.clear();
    index.query_active = false;
    index.query_cursor = 0;
    index.last_query_distance = 0xffffffffu;
}

void SortUnitSpatialIndexEntriesByX(std::vector<UnitSpatialIndexEntry>& entries) {
    std::sort(entries.begin(), entries.end(),
        [](const UnitSpatialIndexEntry& left, const UnitSpatialIndexEntry& right) {
            return left.x < right.x;
        });
}

void RebuildUnitSpatialIndex(UnitSpatialIndex& index,
    const std::vector<UnitMovementUnit*>& active_units,
    UnitSpatialIndexBuildMode mode,
    const std::vector<u32>* command_state_categories) {
    reset_query(index);
    index.sorted_units.clear();
    index.sorted_units.reserve(index.capacity);

    for (UnitMovementUnit* unit : active_units) {
        if (unit == nullptr || !unit->active) {
            continue;
        }
        if (!include_unit_for_mode(*unit, mode, command_state_categories)) {
            continue;
        }
        if (index.sorted_units.size() >= index.capacity) {
            break;
        }
        index.sorted_units.push_back(UnitSpatialIndexEntry{unit->x, unit});
    }

    if (index.sorted_units.size() > 1) {
        SortUnitSpatialIndexEntriesByX(index.sorted_units);
    }
    if (index.unit_to_sorted_index.size() != index.capacity) {
        index.unit_to_sorted_index.assign(index.capacity,
            kInvalidUnitSpatialSortedIndex);
    }
    rebuild_slot_map(index);
}

UnitMovementUnit* QueryUnitSpatialIndexRadius(UnitSpatialIndex& index,
    UnitMovementUnit* source_unit, i32 x, i32 y, u32 radius) {
    reset_query(index);
    if (index.sorted_units.empty()) {
        return nullptr;
    }

    i32 center_x = x;
    i32 center_y = y;
    if (source_unit != nullptr) {
        center_x = source_unit->x;
        center_y = source_unit->y;
        if (find_sorted_index(index, *source_unit) == kInvalidUnitSpatialSortedIndex) {
            return nullptr;
        }
    }

    const i32 signed_radius = static_cast<i32>(radius);
    const i32 min_x = center_x - signed_radius;
    const i32 max_x = center_x + signed_radius;
    const i32 min_y = center_y - signed_radius;
    const i32 max_y = center_y + signed_radius;
    const std::size_t first = first_index_with_x_at_least(index, min_x);
    const std::size_t last = last_index_with_x_at_most(index, max_x);
    if (first >= index.sorted_units.size() || last >= index.sorted_units.size() ||
        first > last) {
        return nullptr;
    }

    for (std::size_t sorted_index = first; sorted_index <= last; ++sorted_index) {
        UnitMovementUnit* candidate = index.sorted_units[sorted_index].unit;
        if (candidate == nullptr) {
            continue;
        }
        if (candidate->y < min_y || candidate->y > max_y) {
            continue;
        }
        const u32 distance = CalculateApproxUnitDistance(center_x, center_y,
            candidate->x, candidate->y);
        if (distance <= radius) {
            index.query_results.push_back(UnitSpatialQueryEntry{distance, candidate});
        }
    }
    return activate_query(index, true);
}

UnitMovementUnit* QueryUnitSpatialIndexRelativeBox(UnitSpatialIndex& index,
    UnitMovementUnit* source_unit, i32 left, i32 right, i32 top, i32 bottom) {
    if (source_unit == nullptr) {
        reset_query(index);
        return nullptr;
    }
    return query_relative_box(index, *source_unit, source_unit->x + left,
        source_unit->x + right, source_unit->y + top, source_unit->y + bottom);
}

UnitMovementUnit* NextUnitSpatialIndexQueryResult(UnitSpatialIndex& index) {
    ++index.query_cursor;
    if (!index.query_active || index.query_cursor >= index.query_results.size()) {
        index.last_query_distance = 0xffffffffu;
        return nullptr;
    }
    const UnitSpatialQueryEntry& entry = index.query_results[index.query_cursor];
    index.last_query_distance = entry.distance;
    return entry.unit;
}

void InitializeUnitSpatialIndexSet(UnitSpatialIndexSet& indexes) {
    InitializeUnitSpatialIndex(indexes.all_units);
    InitializeUnitSpatialIndex(indexes.non_terminal_units);
    InitializeUnitSpatialIndex(indexes.active_command_units);
    InitializeUnitSpatialIndex(indexes.lifecycle_class3_units);
}

void ShutdownUnitSpatialIndexSet(UnitSpatialIndexSet& indexes) {
    ShutdownUnitSpatialIndex(indexes.all_units);
    ShutdownUnitSpatialIndex(indexes.non_terminal_units);
    ShutdownUnitSpatialIndex(indexes.active_command_units);
    ShutdownUnitSpatialIndex(indexes.lifecycle_class3_units);
}

void AllocateNonTerminalUnitSpatialIndexStorage(UnitSpatialIndexSet& indexes) {
    InitializeUnitSpatialIndex(indexes.non_terminal_units);
}

void RegisterNonTerminalUnitSpatialIndexShutdown(UnitSpatialIndexSet&) {
}

void ShutdownNonTerminalUnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    ShutdownUnitSpatialIndex(indexes.non_terminal_units);
}

void AllocateActiveCommandUnitSpatialIndexStorage(UnitSpatialIndexSet& indexes) {
    InitializeUnitSpatialIndex(indexes.active_command_units);
}

void RegisterActiveCommandUnitSpatialIndexShutdown(UnitSpatialIndexSet&) {
}

void ShutdownActiveCommandUnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    ShutdownUnitSpatialIndex(indexes.active_command_units);
}

void AllocateLifecycleClass3UnitSpatialIndexStorage(UnitSpatialIndexSet& indexes) {
    InitializeUnitSpatialIndex(indexes.lifecycle_class3_units);
}

void RegisterLifecycleClass3UnitSpatialIndexShutdown(UnitSpatialIndexSet&) {
}

void ShutdownLifecycleClass3UnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    ShutdownUnitSpatialIndex(indexes.lifecycle_class3_units);
}

void RebuildUnitSpatialIndexSet(UnitSpatialIndexSet& indexes,
    const UnitMovementContext& context,
    const std::vector<u32>* command_state_categories) {
    RebuildUnitSpatialIndex(indexes.all_units, context.active_units,
        UnitSpatialIndexBuildMode::all_active_units, command_state_categories);
    RebuildUnitSpatialIndex(indexes.non_terminal_units, context.active_units,
        UnitSpatialIndexBuildMode::non_structure_non_terminal_command_units,
        command_state_categories);
    RebuildUnitSpatialIndex(indexes.active_command_units, context.active_units,
        UnitSpatialIndexBuildMode::non_structure_active_command_units,
        command_state_categories);
    RebuildUnitSpatialIndex(indexes.lifecycle_class3_units, context.active_units,
        UnitSpatialIndexBuildMode::lifecycle_class3_units, command_state_categories);
}

UnitMovementUnit* QueryAllUnitsSpatialIndexAroundUnit(
    UnitSpatialIndexSet& indexes, UnitMovementUnit& unit, u32 radius) {
    return QueryUnitSpatialIndexRadius(indexes.all_units, &unit, 0, 0, radius);
}

UnitMovementUnit* QueryAllUnitsSpatialIndexAroundPoint(
    UnitSpatialIndexSet& indexes, i32 x, i32 y, u32 radius) {
    return QueryUnitSpatialIndexRadius(indexes.all_units, nullptr, x, y, radius);
}

UnitMovementUnit* NextAllUnitsSpatialIndexResult(UnitSpatialIndexSet& indexes) {
    return NextUnitSpatialIndexQueryResult(indexes.all_units);
}

UnitMovementUnit* QueryNonTerminalUnitSpatialBox(UnitSpatialIndexSet& indexes,
    UnitMovementUnit& unit) {
    return QueryUnitSpatialIndexRelativeBox(indexes.non_terminal_units, &unit,
        unit.definition.spatial_query_left, unit.definition.spatial_query_right,
        unit.definition.spatial_query_top, unit.definition.spatial_query_bottom);
}

UnitMovementUnit* QueryActiveCommandUnitSpatialBox(UnitSpatialIndexSet& indexes,
    UnitMovementUnit& unit) {
    return QueryUnitSpatialIndexRelativeBox(indexes.active_command_units, &unit,
        unit.definition.spatial_query_left, unit.definition.spatial_query_right,
        unit.definition.spatial_query_top, unit.definition.spatial_query_bottom);
}

UnitMovementUnit* QueryLifecycleClass3UnitSpatialBox(UnitSpatialIndexSet& indexes,
    UnitMovementUnit& unit) {
    return QueryUnitSpatialIndexRelativeBox(indexes.lifecycle_class3_units, &unit,
        unit.definition.spatial_query_left, unit.definition.spatial_query_right,
        unit.definition.spatial_query_top, unit.definition.spatial_query_bottom);
}

} // namespace ranker
