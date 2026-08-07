#include "ranker_unit_movement.h"

#include "ranker_production_orders.h"
#include "ranker_trc.h"
#include "ranker_unit_damage.h"
#include "ranker_unit_equipment.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ranker {
namespace {

constexpr std::array<u8, 16> kOriginalDirectionLookup = {
    0, 1, 3, 2, 5, 0, 4, 0, 7, 8, 0, 0, 6, 0, 5, 6,
};
constexpr std::array<u32, 16> kJw207Record0PositiveYMirror = {
    8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9,
};
constexpr std::array<u32, 16> kJw207Record0NegativeXMirror = {
    0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
};
constexpr std::array<UnitMovementPoint, 9> kUnitMovementDirection8Deltas = {{
    {0, 0}, {0, -4}, {2, -2}, {4, 0}, {2, 2},
    {0, 4}, {-2, 2}, {-4, 0}, {-2, -2},
}};
// DAT_0072cee0 is a separate one-pixel separation-step table.  It must not be
// conflated with the four-pixel animation movement deltas above.
constexpr std::array<UnitMovementPoint, 9> kLegacySeparationDirectionDeltas = {{
    {0, 0}, {0, -1}, {1, -1}, {1, 0}, {1, 1},
    {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
}};
constexpr std::array<UnitMovementPoint, 9> kUnitMovementTileDirection8Deltas = {{
    {0, 0}, {0, -32}, {32, -32}, {32, 0}, {32, 32},
    {0, 32}, {-32, 32}, {-32, 0}, {-32, -32},
}};
constexpr std::array<UnitMovementPoint, 8> kLegacyPathfinderNeighborOffsets = {{
    {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    {-1, -1}, {1, -1}, {1, 1}, {-1, 1},
}};
constexpr std::array<UnitMovementPoint, 8> kLegacyPathfinderNeighborStepDeltas = {{
    {1, 1}, {-1, 1}, {-1, -1}, {0, -1},
    {2, 0}, {0, 2}, {-2, 0}, {1, -2},
}};
constexpr std::array<u32, 9> kLegacyFallbackDirectionRemap = {
    1, 5, 6, 7, 8, 1, 2, 3, 4,
};
LegacyPathfinderScratchState g_legacy_pathfinder_scratch;
constexpr std::array<UnitMovementPoint, 17> kUnitMovementDirection16Deltas = {{
    {0, 0}, {0, -4}, {1, -3}, {2, -2}, {3, -1},
    {4, 0}, {3, 1}, {2, 2}, {1, 3}, {0, 4},
    {-1, 3}, {-2, 2}, {-3, 1}, {-4, 0}, {-3, -1},
    {-2, -2}, {-1, -3},
}};
constexpr std::array<u32, 16> kMovementStepTriangularThresholds = {
    0, 1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 66, 78, 91, 105, 120,
};
constexpr u32 kUnitMovementTurnTimeoutTicks = 10;
// These two combat percentages are loaded into DAT_012204b0 and
// DAT_0120c9c8 by the original data bootstrap.  The shipped catalog resolves
// them to 10 and 100 respectively.  They are consumed only while the matching
// temporary command/runtime flags are active (0x004084d0 and 0x00408590).
constexpr u32 kCommandFlag4000MaxHealthBonusPercent = 10;
constexpr u32 kRuntimeFlag0c00DefenseBonusPercent = 100;

u32 apply_signed_u16_cap(u32 value, u32 cap) {
    return WrappedU32ToI32(value) < static_cast<i32>(cap) ?
        value : cap;
}

void sync_unit_runtime_stats(UnitMovementUnit& unit,
    const UnitRuntimeStatBlock& stats) {
    unit.max_health = stats.max_health;
    unit.health = stats.health;
    unit.runtime_stat_1c = stats.stat_1c;
    unit.runtime_stat_20 = stats.stat_20;
    unit.max_secondary_value = stats.max_secondary_value;
    unit.secondary_value = stats.secondary_value;
    unit.runtime_stat_28 = stats.stat_28;
}

const UnitActionDamageProfile* unit_action_damage_profile(
    const UnitActionDamageProfileTable* table, u32 profile_index) {
    if (table == nullptr || profile_index >= table->profiles.size()) {
        return nullptr;
    }
    return &table->profiles[profile_index];
}

i64 apply_unit_action_damage_percent(i64 value, i32 percent) {
    return value * static_cast<i64>(percent) / 100;
}

u32 clamp_unit_action_damage(i64 damage) {
    if (damage < 1) {
        return 1;
    }
    if (damage > static_cast<i64>(0xffffffffu)) {
        return 0xffffffffu;
    }
    return static_cast<u32>(damage);
}
constexpr std::array<std::array<i32, 9>, 9> kUnitMovementDirection8TurnSteps = {{
    {{0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 1, 1, 1, 1, -1, -1, -1}},
    {{0, -1, 0, 1, 1, 1, 1, -1, -1}},
    {{0, -1, -1, 0, 1, 1, 1, 1, -1}},
    {{0, -1, -1, -1, 0, 1, 1, 1, 1}},
    {{0, 1, -1, -1, -1, 0, 1, 1, 1}},
    {{0, 1, 1, -1, -1, -1, 0, 1, 1}},
    {{0, 1, 1, 1, -1, -1, -1, 0, 1}},
    {{0, 1, 1, 1, 1, -1, -1, -1, 0}},
}};
constexpr std::array<std::array<i32, 17>, 17> kUnitMovementDirection16TurnSteps = {{
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1}},
    {{0, -1, 0, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1}},
    {{0, -1, -1, 0, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1}},
    {{0, -1, -1, -1, 0, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1}},
    {{0, -1, -1, -1, -1, 0, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1}},
    {{0, -1, -1, -1, -1, -1, 0, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1}},
    {{0, -1, -1, -1, -1, -1, -1, 0, 1, 1, 1, 1, 1, 1, 1, 1, -1}},
    {{0, -1, -1, -1, -1, -1, -1, -1, 0, 1, 1, 1, 1, 1, 1, 1, 1}},
    {{0, 1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 1, 1, 1, 1, 1, 1}},
    {{0, 1, 1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 1, 1, 1, 1, 1}},
    {{0, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 1, 1, 1, 1}},
    {{0, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 1, 1, 1}},
    {{0, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 1, 1}},
    {{0, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 1}},
    {{0, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 0, 1}},
    {{0, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 0}},
}};

u32 tile_index(const UnitMovementMap& map, u32 tile_x, u32 tile_y) {
    return UnitMovementMapTileIndex(map, tile_x, tile_y);
}

u32 world_to_tile(i32 value) {
    return static_cast<u32>(std::max(value, 0) >> 5);
}

u32 signed_world_to_tile(i32 value) {
    return static_cast<u32>(value >> 5);
}

u32 logical_world_to_tile(i32 value) {
    return static_cast<u32>(value) >> 5;
}

i32 tile_to_center(u32 tile) {
    return static_cast<i32>(tile * 32 + 16);
}

i32 tile_to_origin(u32 tile) {
    return static_cast<i32>(tile * 32);
}

UnitMovementPoint signed_world_point_to_tile(i32 x, i32 y) {
    return UnitMovementPoint{static_cast<i32>(signed_world_to_tile(x)),
        static_cast<i32>(signed_world_to_tile(y))};
}

bool tile_point_in_bounds(const UnitMovementMap& map, UnitMovementPoint tile) {
    return tile.x >= 0 && tile.y >= 0 &&
        static_cast<u32>(tile.x) < map.width &&
        static_cast<u32>(tile.y) < map.height;
}

u32 tile_point_index(const UnitMovementMap& map, UnitMovementPoint tile) {
    return UnitMovementMapTileIndex(
        map, static_cast<u32>(tile.x), static_cast<u32>(tile.y));
}

UnitMovementPoint tile_center_point(UnitMovementPoint tile) {
    return UnitMovementPoint{tile_to_center(static_cast<u32>(tile.x)),
        tile_to_center(static_cast<u32>(tile.y))};
}

bool in_world_bounds(const UnitMovementMap& map, i32 x, i32 y) {
    return x >= 0 && y >= 0 &&
        static_cast<u32>(x) < map.width * 32 &&
        static_cast<u32>(y) < map.height * 32;
}

i32 clamp_world_coordinate_to_tiles(i32 value, u32 max_tiles) {
    if (value < 0) {
        return 0;
    }
    if (max_tiles == 0) {
        return value;
    }
    const i64 max_world = static_cast<i64>(max_tiles) * 32 - 1;
    if (static_cast<i64>(value) > max_world) {
        return static_cast<i32>(max_world);
    }
    return value;
}

void refresh_unit_current_cell(UnitMovementUnit& unit) {
    unit.current_cell_x = unit.x & ~0x1f;
    unit.current_cell_y = unit.y & ~0x1f;
}

u32 default_distance(UnitMovementContext& context, const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    if (context.callbacks.distance_to_unit != nullptr) {
        return context.callbacks.distance_to_unit(context, source, target);
    }
    return CalculateApproxUnitDistance(source.x, source.y, target.x, target.y);
}

bool is_blocking_link_command(u32 command_state) {
    const u32 normalized = command_state & 0x00ffffff;
    return normalized == kReservedTileCommandA || normalized == kReservedTileCommandBAlt;
}

bool unit_point_inside_expanded_bounds(const UnitMovementUnit& unit,
    const UnitMovementUnit& bounds_owner, i32 padding) {
    const i32 left = bounds_owner.x + bounds_owner.definition.bounds_left - padding;
    const i32 top = bounds_owner.y + bounds_owner.definition.bounds_top - padding;
    const i32 right = left + bounds_owner.definition.bounds_width + padding * 2;
    const i32 bottom = top + bounds_owner.definition.bounds_height + padding * 2;
    return unit.x >= left && unit.x <= right && unit.y >= top && unit.y <= bottom;
}

struct UnitMovementRect {
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
};

UnitMovementRect unit_interaction_bounds_rect(const UnitMovementUnit& unit) {
    UnitMovementRect rect;
    rect.left = unit.x + unit.definition.interaction_bounds_left;
    rect.top = unit.y + unit.definition.interaction_bounds_top;
    rect.right = rect.left + unit.definition.interaction_bounds_width;
    rect.bottom = rect.top + unit.definition.interaction_bounds_height;
    return rect;
}

UnitMovementRect unit_interaction_bounds_rect_at(const UnitMovementUnit& unit,
    i32 x, i32 y) {
    UnitMovementRect rect;
    rect.left = x + unit.definition.interaction_bounds_left;
    rect.top = y + unit.definition.interaction_bounds_top;
    rect.right = rect.left + unit.definition.interaction_bounds_width;
    rect.bottom = rect.top + unit.definition.interaction_bounds_height;
    return rect;
}

UnitMovementRect target_footprint_rect(const UnitMovementUnit& target, i32 padding) {
    UnitMovementRect rect;
    rect.left = target.x - padding;
    rect.top = target.y - padding;
    rect.right = target.x +
        static_cast<i32>(target.definition.footprint_width_tiles * 32) + padding;
    rect.bottom = target.y +
        static_cast<i32>(target.definition.footprint_height_tiles * 32) + padding;
    return rect;
}

bool rects_overlap(const UnitMovementRect& a, const UnitMovementRect& b) {
    return a.left <= b.right && a.right >= b.left &&
        a.top <= b.bottom && a.bottom >= b.top;
}

bool rects_overlap_strict(const UnitMovementRect& a, const UnitMovementRect& b) {
    return a.left < b.right && a.right > b.left &&
        a.top < b.bottom && a.bottom > b.top;
}

u32 rotate_direction8(u32 direction, i32 step) {
    if (direction == 0) {
        return 0;
    }
    i32 value = static_cast<i32>(direction) + step;
    while (value < 1) {
        value += 8;
    }
    while (value > 8) {
        value -= 8;
    }
    return static_cast<u32>(value);
}

bool direction_can_enter(UnitMovementContext& context, const UnitMovementUnit& unit,
    u32 direction) {
    const UnitMovementPoint delta =
        direction < kUnitMovementTileDirection8Deltas.size()
            ? kUnitMovementTileDirection8Deltas[direction]
            : UnitMovementPoint{};
    return CheckUnitCanEnterTerrainCell(context, unit, unit.x + delta.x, unit.y + delta.y);
}

bool check_pathfinder_tile_can_enter(UnitMovementContext& context, const UnitMovementUnit& unit,
    UnitMovementPoint tile, u32* checked_count = nullptr) {
    if (!tile_point_in_bounds(context.map, tile)) {
        return false;
    }
    if (checked_count != nullptr) {
        ++*checked_count;
    }
    const UnitMovementPoint center = tile_center_point(tile);
    return CheckUnitCanEnterTerrainCell(context, unit, center.x, center.y);
}

std::vector<UnitMovementPoint> default_nearby_offsets() {
    std::vector<UnitMovementPoint> offsets;
    offsets.reserve(121);
    for (i32 radius = 0; radius <= 5; ++radius) {
        for (i32 y = -radius; y <= radius; ++y) {
            for (i32 x = -radius; x <= radius; ++x) {
                if (std::max(std::abs(x), std::abs(y)) != radius) {
                    continue;
                }
                offsets.push_back(UnitMovementPoint{x, y});
            }
        }
    }
    return offsets;
}

const std::vector<UnitMovementPoint>& search_offsets(const UnitMovementContext& context) {
    static const std::vector<UnitMovementPoint> fallback = default_nearby_offsets();
    return context.map.nearby_tile_offsets.empty() ? fallback : context.map.nearby_tile_offsets;
}

UnitTileSearchResult find_near_tile(const UnitMovementContext& context, i32 x, i32 y,
    bool require_unoccupied) {
    const i32 base_x = static_cast<i32>(signed_world_to_tile(x));
    const i32 base_y = static_cast<i32>(signed_world_to_tile(y));
    for (const UnitMovementPoint& offset : search_offsets(context)) {
        const i32 tile_x_signed = base_x + offset.x;
        const i32 tile_y_signed = base_y + offset.y;
        if (tile_x_signed < 0 || tile_y_signed < 0) {
            continue;
        }
        const u32 tile_x = static_cast<u32>(tile_x_signed);
        const u32 tile_y = static_cast<u32>(tile_y_signed);
        const UnitMovementCell* cell = GetMovementCell(context.map, tile_x, tile_y);
        if (cell == nullptr) {
            continue;
        }
        const bool passable = require_unoccupied ?
            IsUnoccupiedPassableTerrainCell(*cell) : IsPassableTerrainCell(*cell);
        if (passable) {
            return UnitTileSearchResult{true, tile_to_origin(tile_x), tile_to_origin(tile_y)};
        }
    }
    return {};
}

u32 movement_cell_brush_flags(const UnitMovementCell& cell) {
    return cell.visibility_flags != 0 ? cell.visibility_flags : cell.flags;
}

bool movement_visibility_tile_in_bounds(const UnitMovementVisibilityLayers& layers,
    u32 tile_x, u32 tile_y) {
    const u32 stride = layers.stride_tiles != 0 ? layers.stride_tiles : layers.width;
    return tile_x < layers.width && tile_y < layers.height && stride != 0;
}

u32 movement_visibility_layer_flags(const UnitMovementVisibilityLayers& layers,
    const std::vector<u32>* values, u32 tile_x, u32 tile_y, u32 fallback) {
    if (values == nullptr ||
        !movement_visibility_tile_in_bounds(layers, tile_x, tile_y)) {
        return fallback;
    }

    const u32 stride = layers.stride_tiles != 0 ? layers.stride_tiles : layers.width;
    const std::size_t index =
        static_cast<std::size_t>(tile_y) * stride + tile_x;
    return index < values->size() ? (*values)[index] : fallback;
}

bool movement_cell_has_legacy_entry_layers(const UnitMovementCell& cell) {
    return cell.alternate_flags != 0 || cell.visibility_flags != 0;
}

bool legacy_movement_class_can_enter_cell(const UnitMovementUnit& unit,
    const UnitMovementCell& cell, bool allow_command_shortcut) {
    const u32 decoration_flags = cell.alternate_flags;
    const u32 brush_flags = cell.visibility_flags;
    const bool terrain_clear = (cell.flags & kMapCellTerrainMask) == 0;
    const bool brush_clear = (brush_flags & 0x20000000u) == 0;
    switch (unit.definition.movement_class) {
    case 0:
        if ((decoration_flags & 0x20000000u) == 0 ||
            (decoration_flags & 0x60000000u) == 0) {
            return false;
        }
        // Original 0x004c810d tests raw unit +0xac bit zero here.  That word
        // is movement_flags; command_flags lives at raw +0x9c.  Harvest
        // commands set command_flags bit zero while approaching a blocked
        // resource tile, so consulting the wrong word skips the original
        // nearest-passable-goal adjustment.
        if (allow_command_shortcut && (unit.movement_flags & 1u) != 0) {
            return true;
        }
        return terrain_clear && brush_clear;
    case 1:
        return false;
    case 2:
        if ((decoration_flags & 0x60000000u) == 0) {
            return false;
        }
        if (allow_command_shortcut && (unit.movement_flags & 1u) != 0) {
            return true;
        }
        return terrain_clear && brush_clear;
    case 3:
        return true;
    case 4:
        if ((decoration_flags & 0x40000000u) == 0) {
            return false;
        }
        if (allow_command_shortcut) {
            return true;
        }
        return terrain_clear && brush_clear;
    default:
        return false;
    }
}

UnitMovementPoint legacy_fallback_direction_delta(u32 direction) {
    if (direction >= kLegacyFallbackDirectionRemap.size()) {
        direction = 1;
    }
    const u32 remapped = kLegacyFallbackDirectionRemap[direction];
    if (remapped >= kLegacySeparationDirectionDeltas.size()) {
        return {};
    }
    return kLegacySeparationDirectionDeltas[remapped];
}

bool legacy_ground_fallback_cell_allows(const UnitMovementMap& map,
    const UnitMovementUnit& unit, const UnitMovementCell& cell) {
    if (!map.legacy_entry_layers_present &&
        !movement_cell_has_legacy_entry_layers(cell)) {
        return IsUnoccupiedPassableTerrainCell(cell);
    }

    const u32 decoration_flags = cell.alternate_flags;
    const u32 brush_flags = cell.visibility_flags;
    const bool terrain_clear = (cell.flags & kMapCellTerrainMask) == 0;
    const bool brush_clear = (brush_flags & 0x20000000u) == 0;
    if (unit.definition.movement_class == 4) {
        return (decoration_flags & 0x40000000u) != 0 &&
            terrain_clear && brush_clear;
    }
    return (unit.definition.movement_class == 2 ||
            (decoration_flags & 0x20000000u) != 0) &&
        (decoration_flags & 0x60000000u) != 0 &&
        terrain_clear && brush_clear;
}

u32 movement_command_metadata_flags(UnitMovementContext& context,
    const UnitMovementUnit& unit) {
    return context.callbacks.command_metadata_flags != nullptr
        ? context.callbacks.command_metadata_flags(context, unit)
        : 0;
}

u32 movement_random_limit(UnitMovementContext& context, u32 limit) {
    if (limit == 0 || context.callbacks.random_limit == nullptr) {
        return 0;
    }
    return context.callbacks.random_limit(context, limit) % limit;
}

const ProductionOrderRuntimeState& movement_production_state_or_empty(
    const UnitMovementContext& context) {
    static const ProductionOrderRuntimeState empty_state{};
    return context.production_state != nullptr ? *context.production_state : empty_state;
}

i32 movement_additional_modifier_for_unit(const UnitMovementContext& context,
    const UnitMovementUnit& unit) {
    return (unit.command_flags & 0x10000u) != 0 ?
        context.additional_movement_modifier : 0;
}

UnitMovementPoint movement_frame_delta_with_context(
    const UnitMovementContext& context, const UnitMovementUnit& unit) {
    return CalculateUnitMovementFrameDeltaWithProductionEffects(
        movement_production_state_or_empty(context), unit,
        movement_additional_modifier_for_unit(context, unit),
        context.equipment_catalog);
}

UnitMovementPoint movement_frame_delta_for_direction_with_context(
    const UnitMovementContext& context, const UnitMovementUnit& unit, u32 direction) {
    return CalculateUnitMovementFrameDeltaForDirectionWithProductionEffects(
        movement_production_state_or_empty(context), unit, direction,
        movement_additional_modifier_for_unit(context, unit),
        context.equipment_catalog);
}

bool try_blocked_movement_direction_step(UnitMovementContext& context,
    UnitMovementUnit& unit, u32 direction, u32 previous_direction) {
    // Each original jump-table arm compares the saved incoming direction
    // against the *opposite* of the candidate (for example, candidate 2 is
    // skipped only when the incoming direction was 6).  It then writes the
    // candidate direction before calculating/probing its frame delta, so a
    // failed probe also leaves that direction behind for the next arm.
    if (direction == 0 ||
        direction >= kLegacyFallbackDirectionRemap.size() ||
        kLegacyFallbackDirectionRemap[direction] == previous_direction) {
        return false;
    }

    unit.direction = direction;
    const UnitMovementPoint delta =
        movement_frame_delta_for_direction_with_context(context, unit, direction);
    const i32 next_x = unit.x + delta.x;
    const i32 next_y = unit.y + delta.y;
    if (!CheckUnitCanEnterTerrainCell(context, unit, next_x, next_y)) {
        return false;
    }

    unit.x = next_x;
    unit.y = next_y;
    unit.current_cell_x = next_x & ~0x1f;
    unit.current_cell_y = next_y & ~0x1f;
    unit.direction = direction;
    return true;
}

bool ProcessBlockedMovementDirectionFallback(UnitMovementContext& context,
    UnitMovementUnit& unit, u32 desired_direction) {
    static constexpr std::array<std::array<u8, 4>, 9> kFallbackDirections{{
        {{0, 0, 0, 0}},
        {{2, 8, 3, 7}},
        {{3, 1, 4, 8}},
        {{4, 2, 5, 1}},
        {{5, 3, 6, 2}},
        {{6, 4, 7, 3}},
        {{7, 5, 8, 4}},
        {{8, 6, 1, 5}},
        {{1, 7, 2, 6}},
    }};

    if (desired_direction >= kFallbackDirections.size()) {
        return false;
    }

    const u32 previous_direction = unit.direction;
    const auto& fallback_directions = kFallbackDirections[desired_direction];
    for (std::size_t index = 0; index < 2; ++index) {
        const u32 direction = fallback_directions[index];
        if (try_blocked_movement_direction_step(context, unit, direction,
                previous_direction)) {
            return true;
        }
    }

    // The original blocked-movement jump table has an extra corner probe for
    // diagonal headings (0x004c7868, 0x004c7a67, 0x004c7c66 and 0x004c7e65).
    // After both adjacent full animation steps fail, it first checks the
    // diagonal tile 32 pixels away.  If that tile is enterable, it retries the
    // two component axes with DAT_0072cee0's one-pixel separation deltas.  The
    // one-pixel move is important: omitting it makes neutral idle wanderers
    // choose a different fallback direction, finish their route on a different
    // tick and consequently consume the shared gameplay RNG in another order.
    if ((desired_direction & 1u) == 0 && desired_direction != 0) {
        const UnitMovementPoint diagonal_tile =
            kUnitMovementTileDirection8Deltas[desired_direction];
        if (CheckUnitCanEnterTerrainCell(context, unit,
                unit.x + diagonal_tile.x, unit.y + diagonal_tile.y)) {
            for (std::size_t index = 0; index < 2; ++index) {
                const u32 direction = fallback_directions[index];
                const UnitMovementPoint delta =
                    kLegacySeparationDirectionDeltas[direction];
                const i32 next_x = unit.x + delta.x;
                const i32 next_y = unit.y + delta.y;
                // The diagonal corner arms likewise store the candidate
                // before the one-pixel entry probe and retain it on failure.
                unit.direction = direction;
                if (!CheckUnitCanEnterTerrainCell(
                        context, unit, next_x, next_y)) {
                    continue;
                }
                unit.x = next_x;
                unit.y = next_y;
                unit.current_cell_x = next_x & ~0x1f;
                unit.current_cell_y = next_y & ~0x1f;
                unit.direction = direction;
                return true;
            }
        }
    }

    for (std::size_t index = 2; index < fallback_directions.size(); ++index) {
        const u32 direction = fallback_directions[index];
        if (try_blocked_movement_direction_step(context, unit, direction,
                previous_direction)) {
            return true;
        }
    }
    return false;
}

bool is_reserved_command(u32 command_state) {
    const u32 normalized = command_state & 0x10ffffff;
    return normalized == kReservedTileCommandA || normalized == kReservedTileCommandB;
}

bool remove_unit_from_list(std::vector<UnitMovementUnit*>& list,
    const UnitMovementUnit& unit) {
    auto it = std::find(list.begin(), list.end(), &unit);
    if (it == list.end()) {
        return false;
    }
    list.erase(it);
    return true;
}

void push_unit_front_unique(std::vector<UnitMovementUnit*>& list,
    UnitMovementUnit& unit) {
    remove_unit_from_list(list, unit);
    list.insert(list.begin(), &unit);
}

} // namespace

u32 UnitMovementMapStrideTiles(const UnitMovementMap& map) {
    const u32 stride = map.stride_tiles != 0 ? map.stride_tiles : map.width;
    return std::max(stride, map.width);
}

u32 UnitMovementMapTileIndex(const UnitMovementMap& map, u32 tile_x, u32 tile_y) {
    return tile_y * UnitMovementMapStrideTiles(map) + tile_x;
}

UnitMovementCell* GetMovementCell(UnitMovementMap& map, u32 tile_x, u32 tile_y) {
    if (tile_x >= map.width || tile_y >= map.height) {
        return nullptr;
    }
    const u32 index = tile_index(map, tile_x, tile_y);
    if (index >= map.cells.size()) {
        return nullptr;
    }
    return &map.cells[index];
}

const UnitMovementCell* GetMovementCell(const UnitMovementMap& map, u32 tile_x,
    u32 tile_y) {
    if (tile_x >= map.width || tile_y >= map.height) {
        return nullptr;
    }
    const u32 index = tile_index(map, tile_x, tile_y);
    if (index >= map.cells.size()) {
        return nullptr;
    }
    return &map.cells[index];
}

bool IsPassableTerrainCell(const UnitMovementCell& cell) {
    return (cell.flags & kMapCellTerrainMask) == kMapCellPassableTerrain;
}

bool IsUnoccupiedPassableTerrainCell(const UnitMovementCell& cell) {
    return IsPassableTerrainCell(cell) &&
        (cell.flags & (kMapCellBlockedTerrain | kMapCellReservedByUnit)) == 0;
}

bool LoadUnitDirectionLookupTableFromTrcRecord(UnitDirectionLookupTable& lookup,
    const char* archive_name, u32 record_index) {
    lookup.values.fill(0);
    std::size_t bytes_read = 0;
    if (!LoadTrcRecordIntoBuffer(archive_name, record_index, lookup.values.data(),
            lookup.values.size(), &bytes_read) ||
        bytes_read < lookup.values.size()) {
        return false;
    }
    return true;
}

bool LoadJw207DirectionLookupRecords(UnitDirectionLookupTable& record0_lookup,
    UnitDirectionLookupTable& record1_lookup, const char* archive_name) {
    if (!LoadUnitDirectionLookupTableFromTrcRecord(
            record0_lookup, archive_name, 0)) {
        return false;
    }
    return LoadUnitDirectionLookupTableFromTrcRecord(
        record1_lookup, archive_name, 1);
}

u32 CalculatePointDirectionFromLookup(UnitMovementPoint source,
    UnitMovementPoint target, const UnitDirectionLookupTable& lookup) {
    i32 dx = target.x - source.x;
    i32 dy = target.y - source.y;
    const bool negative_x = dx < 0;
    const bool negative_y = dy < 0;
    if (negative_x) {
        dx = -dx;
    }
    if (negative_y) {
        dy = -dy;
    }

    while (dx >= static_cast<i32>(kUnitDirectionLookupWidth) ||
        dy >= static_cast<i32>(kUnitDirectionLookupHeight)) {
        dx >>= 1;
        dy >>= 1;
    }

    const u32 index = static_cast<u32>(dx) +
        static_cast<u32>(dy) * kUnitDirectionLookupWidth;
    u32 direction = lookup.values[index];
    if (negative_x && direction < lookup.mirror_negative_x.size()) {
        direction = lookup.mirror_negative_x[direction];
    }
    if (negative_y && direction < lookup.mirror_negative_y.size()) {
        direction = lookup.mirror_negative_y[direction];
    }
    return direction + 1;
}

u32 CalculatePointDirection16FromLookup(UnitMovementPoint source,
    UnitMovementPoint target, const UnitDirectionLookupTable& lookup) {
    i32 dx = target.x - source.x;
    i32 dy = source.y - target.y;
    const bool mirror_positive_y = dy >= 1;
    if (!mirror_positive_y) {
        dy = -dy;
    }
    const bool negative_x = dx < 0;
    if (negative_x) {
        dx = -dx;
    }

    while (dx >= static_cast<i32>(kUnitDirectionLookupWidth) ||
        dy >= static_cast<i32>(kUnitDirectionLookupHeight)) {
        dx >>= 1;
        dy >>= 1;
    }

    const u32 index = static_cast<u32>(dx) +
        static_cast<u32>(dy) * kUnitDirectionLookupWidth;
    u32 direction = lookup.values[index];
    if (mirror_positive_y &&
        direction < kJw207Record0PositiveYMirror.size()) {
        direction = kJw207Record0PositiveYMirror[direction];
    }
    if (negative_x && direction < kJw207Record0NegativeXMirror.size()) {
        direction = kJw207Record0NegativeXMirror[direction];
    }
    return direction + 1;
}

UnitMovementPoint GetUnitMovementDirection8Delta(u32 direction) {
    if (direction >= kUnitMovementDirection8Deltas.size()) {
        return {};
    }
    return kUnitMovementDirection8Deltas[direction];
}

UnitMovementPoint GetUnitMovementDirection16Delta(u32 direction) {
    if (direction >= kUnitMovementDirection16Deltas.size()) {
        return {};
    }
    return kUnitMovementDirection16Deltas[direction];
}

i32 LookupUnitMovementDirectionTurnStep(u32 current_direction,
    u32 target_direction, u32 direction_count) {
    if (direction_count == 8) {
        if (current_direction >= kUnitMovementDirection8TurnSteps.size() ||
            target_direction >= kUnitMovementDirection8TurnSteps[current_direction].size()) {
            return 0;
        }
        return kUnitMovementDirection8TurnSteps[current_direction][target_direction];
    }
    if (direction_count == 16) {
        if (current_direction >= kUnitMovementDirection16TurnSteps.size() ||
            target_direction >= kUnitMovementDirection16TurnSteps[current_direction].size()) {
            return 0;
        }
        return kUnitMovementDirection16TurnSteps[current_direction][target_direction];
    }
    return 0;
}

u32 ApplyUnitMovementDirectionTurnStep(u32 current_direction, i32 turn_step,
    u32 direction_count) {
    if (direction_count == 0 || turn_step == 0) {
        return current_direction;
    }
    i32 next = static_cast<i32>(current_direction) + turn_step;
    if (next > static_cast<i32>(direction_count)) {
        next -= static_cast<i32>(direction_count);
    }
    if (next < 1) {
        next += static_cast<i32>(direction_count);
    }
    return static_cast<u32>(next);
}

UnitMovementDirectionTurnResult PrepareUnitMovementDirectionForStep(
    UnitMovementUnit& unit, u32 target_direction, u32 direction_count) {
    UnitMovementDirectionTurnResult result;
    result.target_direction = target_direction;
    result.direction_count = direction_count;
    result.turn_step = LookupUnitMovementDirectionTurnStep(unit.direction,
        target_direction, direction_count);

    if (result.turn_step == 0) {
        if ((unit.movement_flags & kUnitMovementFlagInterpolatingTowardTarget) == 0) {
            unit.movement_flags |= kUnitMovementFlagInterpolatingTowardTarget;
            unit.movement_residual_x = 0;
            unit.movement_residual_y = 0;
            unit.movement_interpolation_x = static_cast<float>(unit.x);
            unit.movement_interpolation_y = static_cast<float>(unit.y);
        }
        result.interpolation_active = true;
        result.can_advance = true;
        return result;
    }

    if ((unit.movement_flags & kUnitMovementFlagInterpolatingTowardTarget) != 0 &&
        std::abs(static_cast<i32>(target_direction) -
            static_cast<i32>(unit.direction)) > 1) {
        unit.movement_flags &= ~kUnitMovementFlagInterpolatingTowardTarget;
        unit.movement_residual_x = 0;
        unit.movement_residual_y = 0;
    }

    if ((unit.movement_flags & kUnitMovementFlagInterpolatingTowardTarget) != 0) {
        result.interpolation_active = true;
        result.can_advance = true;
        return result;
    }

    unit.direction = ApplyUnitMovementDirectionTurnStep(unit.direction,
        result.turn_step, direction_count);
    ++unit.movement_turn_ticks;
    result.turned = true;
    if (unit.movement_turn_ticks < kUnitMovementTurnTimeoutTicks) {
        result.can_advance = true;
        return result;
    }

    unit.current_cell_x = unit.x & ~0x1f;
    unit.current_cell_y = unit.y & ~0x1f;
    unit.movement_step_accumulator = 0;
    unit.movement_turn_ticks = 0;
    result.turn_timeout = true;
    return result;
}

u32 CalculateUnitDirectionToPoint(const UnitMovementUnit& unit, i32 target_x,
    i32 target_y) {
    u32 index = 0;
    if (unit.x != target_x) {
        if (target_x <= unit.x) {
            index = 6;
        }
        index = static_cast<u8>(index + 2);
    }
    if (unit.y != target_y) {
        if (unit.y < target_y) {
            index = static_cast<u8>(index + 3);
        }
        index = static_cast<u8>(index + 1);
    }

    u32 direction = kOriginalDirectionLookup[std::min<std::size_t>(index,
        kOriginalDirectionLookup.size() - 1)];
    const u32 dx = static_cast<u32>(std::abs(target_x - unit.x));
    const u32 dy = static_cast<u32>(std::abs(target_y - unit.y));

    switch (direction) {
    case 2:
    case 6:
        if (dx * 2 < dy) {
            return direction - 1;
        }
        if (dy * 2 < dx) {
            return direction + 1;
        }
        return direction;
    case 4:
    case 8:
        if (dy <= dx * 2) {
            if (dy * 2 < dx) {
                return direction - 1;
            }
            return direction;
        }
        direction += 1;
        return direction < 9 ? direction : 1;
    default:
        return direction;
    }
}

UnitLinkedTargetCheck CheckLinkedTargetStatusRejectingDeadLink(
    UnitMovementContext& context, UnitMovementUnit& unit) {
    if (unit.linked_unit != nullptr && (unit.linked_unit->runtime_flags & 4) != 0) {
        return UnitLinkedTargetCheck{1, 0};
    }
    return CheckLinkedTargetStatus(context, unit);
}

bool CheckTerrainCellAlternateFlag40000000(const UnitMovementContext& context,
    i32 x, i32 y) {
    const UnitMovementCell* cell = GetMovementCell(context.map, logical_world_to_tile(x),
        logical_world_to_tile(y));
    return cell != nullptr && (cell->alternate_flags & 0x40000000u) != 0;
}

UnitLinkedTargetCheck CheckLinkedTargetStatus(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* linked = unit.linked_unit != nullptr ? unit.linked_unit : unit.target;
    if (linked == nullptr) {
        return UnitLinkedTargetCheck{0, 0};
    }

    if (linked->target != nullptr && linked->target != &unit) {
        if (is_blocking_link_command(linked->target->command_state) ||
            (linked->target->runtime_flags & 4) == 0) {
            return UnitLinkedTargetCheck{2, 0};
        }
        linked->target = nullptr;
    }

    if (!unit_point_inside_expanded_bounds(unit, *linked, 10)) {
        return UnitLinkedTargetCheck{0, 0};
    }

    return UnitLinkedTargetCheck{0, default_distance(context, unit, *linked)};
}

UnitArrivalCheck CheckUnitDestinationArrivalStatus(UnitMovementContext& context,
    const UnitMovementUnit& unit) {
    const u32 dx = static_cast<u32>(std::abs(unit.x - unit.destination_x));
    const u32 dy = static_cast<u32>(std::abs(unit.y - unit.destination_y));
    const UnitMovementCell* cell = GetMovementCell(context.map,
        signed_world_to_tile(unit.destination_x),
        signed_world_to_tile(unit.destination_y));

    if (dx < 0x31 && dy < 0x2f) {
        if (cell != nullptr && (cell->flags & kMapCellReservedByUnit) == 0) {
            if (IsPassableTerrainCell(*cell)) {
                return UnitArrivalCheck{true, 0,
                    CalculateUnitDirectionToPoint(unit, unit.destination_x,
                        unit.destination_y)};
            }
            return UnitArrivalCheck{false, 2, 0};
        }
    }
    else {
        if (dx > 0x40 || dy > 0x40) {
            return UnitArrivalCheck{false, 1, 0};
        }
        if (cell != nullptr && (cell->flags & kMapCellReservedByUnit) == 0) {
            return UnitArrivalCheck{false, 0, 0};
        }
    }
    return UnitArrivalCheck{false, 3, 0};
}

UnitArrivalCheck CheckUnitRangeDestinationStatus(UnitMovementContext& context,
    const UnitMovementUnit& unit) {
    const u32 distance = CalculateApproxUnitDistance(unit.x, unit.y,
        unit.destination_x, unit.destination_y);
    if (unit.definition.range_threshold <= distance * 2) {
        return UnitArrivalCheck{false, 1, 0};
    }

    const UnitMovementCell* cell = GetMovementCell(context.map,
        signed_world_to_tile(unit.destination_x),
        signed_world_to_tile(unit.destination_y));
    if (cell != nullptr && (cell->flags & kMapCellReservedByUnit) == 0) {
        if (IsPassableTerrainCell(*cell)) {
            return UnitArrivalCheck{true, 0,
                CalculateUnitDirectionToPoint(unit, unit.destination_x,
                    unit.destination_y)};
        }
        return UnitArrivalCheck{false, 2, 0};
    }
    return UnitArrivalCheck{false, 3, 0};
}

UnitTileSearchResult FindPassableVerticalNudgeTile(const UnitMovementContext& context,
    i32 x, i32 y) {
    for (i32 candidate_y : {y, y - 15, y + 15}) {
        const u32 tile_x = signed_world_to_tile(x);
        const u32 tile_y = signed_world_to_tile(candidate_y);
        const UnitMovementCell* cell = GetMovementCell(context.map, tile_x, tile_y);
        if (cell != nullptr &&
            (movement_visibility_layer_flags(context.visibility_layers,
                context.visibility_layers.previous_flags, tile_x, tile_y,
                movement_cell_brush_flags(*cell)) & kMapCellReservedByUnit) != 0 &&
            (movement_visibility_layer_flags(context.visibility_layers,
                context.visibility_layers.terrain_backup_flags, tile_x, tile_y,
                cell->flags) & kMapCellTerrainMask) == kMapCellPassableTerrain) {
            return UnitTileSearchResult{true, x, candidate_y};
        }
    }
    return {};
}

UnitTileSearchResult FindNearestPassableTerrainTile(const UnitMovementContext& context,
    i32 x, i32 y) {
    return find_near_tile(context, x, y, false);
}

UnitTileSearchResult FindNearestUnoccupiedTerrainTile(const UnitMovementContext& context,
    i32 x, i32 y) {
    return find_near_tile(context, x, y, true);
}

u32 ProcessHarvestableTileAmount(UnitMovementMap& map, u32 index, u32 amount) {
    if (index >= map.cells.size()) {
        return 0;
    }

    UnitMovementCell& cell = map.cells[index];
    const u32 stored = (cell.flags & kMapCellHarvestAmountMask) >>
        kMapCellHarvestAmountShift;
    if (amount <= stored) {
        const u32 remaining = stored - amount;
        u32 level = 0;
        if (remaining < 1500) {
            level = 1;
            if (remaining < 1000) {
                level = 2;
                if (remaining < 500) {
                    level = 3;
                }
            }
        }
        cell.flags &= ~0x00000003u;
        cell.flags |= level;
        cell.flags &= ~kMapCellHarvestAmountMask;
        cell.flags |= remaining << kMapCellHarvestAmountShift;
        return amount;
    }

    cell.flags &= 0xf0000000;
    if (index + 1 < map.cells.size()) {
        map.cells[index + 1].flags &= 0xf0000000;
    }
    return stored;
}

void RegisterUnitReservedMapTile(UnitMovementContext& context, const UnitMovementUnit& unit) {
    for (UnitReservedMapTile& slot : context.reserved_tiles) {
        if (slot.active && slot.unit_id == unit.id) {
            return;
        }
    }

    for (UnitReservedMapTile& slot : context.reserved_tiles) {
        if (slot.active) {
            continue;
        }
        const u32 tile_x = signed_world_to_tile(unit.destination_x);
        const u32 tile_y = signed_world_to_tile(unit.destination_y);
        UnitMovementCell* cell = GetMovementCell(context.map, tile_x, tile_y);
        if (cell == nullptr) {
            return;
        }
        slot.active = true;
        slot.unit_id = unit.id;
        slot.tile_index = tile_index(context.map, tile_x, tile_y);
        cell->flags |= kMapCellReservedByUnit;
        ++context.reserved_tile_count;
        return;
    }
}

UnitReservedTileReleaseResult ReleaseUnitReservedMapTileWithIndex(
    UnitMovementContext& context, const UnitMovementUnit& unit) {
    for (UnitReservedMapTile& slot : context.reserved_tiles) {
        if (!slot.active || slot.unit_id != unit.id) {
            continue;
        }
        const u32 released_tile_index = slot.tile_index;
        if (slot.tile_index < context.map.cells.size()) {
            context.map.cells[slot.tile_index].flags &= ~kMapCellReservedByUnit;
        }
        slot = {};
        if (context.reserved_tile_count != 0) {
            --context.reserved_tile_count;
        }
        return UnitReservedTileReleaseResult{true, released_tile_index};
    }
    return {};
}

bool ReleaseUnitReservedMapTile(UnitMovementContext& context, const UnitMovementUnit& unit) {
    return ReleaseUnitReservedMapTileWithIndex(context, unit).released;
}

void ProcessInvalidUnitReservedTiles(UnitMovementContext& context) {
    for (UnitReservedMapTile& slot : context.reserved_tiles) {
        if (!slot.active) {
            continue;
        }
        auto it = std::find_if(context.active_units.begin(), context.active_units.end(),
            [&](const UnitMovementUnit* unit) {
                return unit != nullptr && unit->id == slot.unit_id;
            });
        if (it != context.active_units.end() && is_reserved_command((*it)->command_state)) {
            continue;
        }
        if (slot.tile_index < context.map.cells.size()) {
            context.map.cells[slot.tile_index].flags &= ~kMapCellReservedByUnit;
        }
        slot = {};
        if (context.reserved_tile_count != 0) {
            --context.reserved_tile_count;
        }
    }
}

UnitTargetBoundsMovementStatus CheckUnitTargetBoundsMovementStatus(
    const UnitMovementUnit& unit) {
    UnitTargetBoundsMovementStatus result;
    const UnitMovementUnit* target = unit.target;
    if (target == nullptr || (target->runtime_flags & 4u) != 0) {
        result.status = 0;
        return result;
    }

    // CalculateUnitTargetBoundsScratch (0x004c6ed5) reads definition
    // +0x370/+0x374/+0x378/+0x37c for the moving unit.  Those are the
    // interaction bounds in the reconstructed catalog; +0x360..+0x36c are
    // the separate name/center rectangle and can make a harvester accept or
    // reject a dropoff on a different tick.
    const UnitMovementRect source_bounds = unit_interaction_bounds_rect(unit);
    const UnitMovementRect loose_target = target_footprint_rect(*target, 0x2a);
    if (!rects_overlap(source_bounds, loose_target)) {
        result.status = 1;
        return result;
    }

    const UnitMovementRect close_target = target_footprint_rect(*target, 5);
    // ProcessWorkerApproachDropoff 0x004ca457 calls 0x004c715a before it
    // copies the status-2 EDX/EBX result into raw +0x6c/+0x70.  Despite its
    // old no-op label, 0x004c715a replaces the temporary target.x-5/y-5
    // rectangle corner with the target definition's +0x370 interaction-
    // bounds center.  Keeping the corner made returning harvesters approach
    // the HQ from its top-left instead of following the original center path.
    result.suggested_path_x = target->x +
        target->definition.interaction_bounds_left +
        (target->definition.interaction_bounds_width >> 1);
    result.suggested_path_y = target->y +
        target->definition.interaction_bounds_top +
        (target->definition.interaction_bounds_height >> 1);
    if (!rects_overlap(source_bounds, close_target)) {
        result.status = 2;
        return result;
    }

    const UnitMovementPoint point = FindNearestPointInTargetFootprint(unit, *target);
    result.reached = true;
    result.direction = CalculateUnitDirectionToPoint(unit, point.x, point.y);
    return result;
}

UnitTargetBoundsMovementStatus CalculateUnitTargetBoundsScratch(
    const UnitMovementUnit& unit) {
    return CheckUnitTargetBoundsMovementStatus(unit);
}

UnitMovementPoint CalculateUnitMovementCenterPoint(const UnitMovementUnit& unit) {
    return UnitMovementPoint{
        unit.x + unit.definition.center_bounds_left +
            (unit.definition.center_bounds_width >> 1),
        unit.y + unit.definition.center_bounds_top +
            (unit.definition.center_bounds_height >> 1),
    };
}

UnitMovementPoint FindNearestPointInTargetFootprint(const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    const u32 width = target.definition.footprint_width_tiles;
    const u32 height = target.definition.footprint_height_tiles;
    if (width == 0 || height == 0) {
        return UnitMovementPoint{target.x, target.y};
    }

    UnitMovementPoint best{target.x, target.y};
    u32 best_distance = 0xffffffff;
    for (u32 y = 0; y < height; ++y) {
        const i32 world_y = target.y + 16 + static_cast<i32>(y * 32);
        for (u32 x = 0; x < width; ++x) {
            const i32 world_x = target.x + 16 + static_cast<i32>(x * 32);
            const u32 distance = CalculateApproxUnitDistance(source.x, source.y,
                world_x, world_y);
            if (distance <= best_distance) {
                best = UnitMovementPoint{world_x, world_y};
                best_distance = distance;
            }
        }
    }
    return best;
}

UnitDropoffSearchResult FindNearestOwnedDropoffBuilding(UnitMovementContext& context,
    const UnitMovementUnit& source) {
    UnitDropoffSearchResult result;
    for (UnitMovementUnit* candidate : context.active_units) {
        if (candidate == nullptr || candidate == &source) {
            continue;
        }
        // Original FindNearestOwnedDropoffBuilding (0x004c6feb) excludes
        // construction-gated buildings via raw +0x30, not movement state.
        if (candidate->owner_id != source.owner_id || candidate->action_mode_gate == 1) {
            continue;
        }
        if (candidate->type_id != 0x60 && candidate->type_id != 0x70 &&
            candidate->type_id != 0x80 && candidate->type_id != 0x90) {
            continue;
        }
        // FUN_004c3718 obtains both unit centers through FUN_004c36de before
        // CalculateApproxUnitDistance.  Comparing raw origins biases the
        // choice toward the top-left corner of large dropoff buildings and
        // can select a different depot once an owner has more than one.
        const UnitMovementPoint source_center{
            source.x + source.definition.center_bounds_left +
                (source.definition.center_bounds_width >> 1),
            source.y + source.definition.center_bounds_top +
                (source.definition.center_bounds_height >> 1)};
        const UnitMovementPoint candidate_center{
            candidate->x + candidate->definition.center_bounds_left +
                (candidate->definition.center_bounds_width >> 1),
            candidate->y + candidate->definition.center_bounds_top +
                (candidate->definition.center_bounds_height >> 1)};
        const u32 distance = CalculateApproxUnitDistance(source_center.x,
            source_center.y, candidate_center.x, candidate_center.y);
        if (distance <= result.distance) {
            result.unit = candidate;
            result.distance = distance;
        }
    }
    return result;
}

i32 GetUnitProductionCompletionEffect(const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 effect_index) {
    return GetProductionOrderCompletionEffectTotal(production_state, effect_index,
        unit.owner_id, unit.type_id);
}

u32 AddSignedUnitStatDelta(u32 base_value, i32 delta, u32 minimum_value) {
    const i64 value = static_cast<i64>(base_value) + static_cast<i64>(delta);
    if (value <= static_cast<i64>(minimum_value)) {
        return minimum_value;
    }
    if (value > static_cast<i64>(0xffffffffu)) {
        return 0xffffffffu;
    }
    return static_cast<u32>(value);
}

UnitMovementPoint ApplyUnitMovementDeltaModifier(UnitMovementPoint delta, i32 modifier) {
    if (modifier == 0) {
        return delta;
    }
    auto adjust = [modifier](i32 value) {
        if (value < 0) {
            return value - modifier;
        }
        if (value > 0) {
            return value + modifier;
        }
        return value;
    };
    delta.x = adjust(delta.x);
    delta.y = adjust(delta.y);
    return delta;
}

u32 CalculateUnitRuntimeMaxHealthWithProductionEffect00(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit) {
    return AddSignedUnitStatDelta(unit.max_health,
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotRuntimeMaxHealth));
}

u32 CalculateUnitHitPointBarFillWithProductionEffect00(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 scale) {
    const u32 max_health =
        CalculateUnitRuntimeMaxHealthWithProductionEffect00(production_state, unit);
    if (max_health == 0) {
        return 0;
    }
    const u64 fill = static_cast<u64>(unit.health) * scale / max_health;
    return fill > 0xffffffffu ? 0xffffffffu : static_cast<u32>(fill);
}

bool CheckUnitBelowRuntimeMaxHealthWithProductionEffect00(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit) {
    return unit.health <
        CalculateUnitRuntimeMaxHealthWithProductionEffect00(production_state, unit);
}

void AddUnitHealthClampedToProductionEffect00(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    u32 amount) {
    const u32 max_health =
        CalculateUnitRuntimeMaxHealthWithProductionEffect00(production_state, unit);
    const u64 value = static_cast<u64>(unit.health) + amount;
    unit.health = value > max_health ? max_health : static_cast<u32>(value);
}

u32 CalculateUnitRuntimeMaxSecondaryValueWithProductionEffect01(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit) {
    return AddSignedUnitStatDelta(unit.max_secondary_value,
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotRuntimeMaxSecondaryValue));
}

bool CheckUnitBelowRuntimeMaxSecondaryValueWithProductionEffect01(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit) {
    return unit.secondary_value <
        CalculateUnitRuntimeMaxSecondaryValueWithProductionEffect01(production_state, unit);
}

void AddUnitSecondaryValueClampedToProductionEffect01(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    u32 amount) {
    const u32 max_secondary =
        CalculateUnitRuntimeMaxSecondaryValueWithProductionEffect01(production_state, unit);
    const u64 value = static_cast<u64>(unit.secondary_value) + amount;
    unit.secondary_value =
        value > max_secondary ? max_secondary : static_cast<u32>(value);
}

void AddUnitRuntimeHealthClamped(UnitRuntimeStatBlock& stats, u32 amount) {
    const u64 value = static_cast<u64>(stats.health) + amount;
    stats.health =
        value > stats.max_health ? stats.max_health : static_cast<u32>(value);
}

void AddUnitRuntimeSecondaryClamped(UnitRuntimeStatBlock& stats, u32 amount) {
    const u64 value = static_cast<u64>(stats.secondary_value) + amount;
    stats.secondary_value = value > stats.max_secondary_value
        ? stats.max_secondary_value
        : static_cast<u32>(value);
}

void SubtractUnitRuntimeHealthClamped(UnitRuntimeStatBlock& stats, u32 amount) {
    stats.health = amount > stats.health ? 0 : stats.health - amount;
}

void SubtractUnitRuntimeSecondaryClamped(UnitRuntimeStatBlock& stats, u32 amount) {
    stats.secondary_value =
        amount > stats.secondary_value ? 0 : stats.secondary_value - amount;
}

void ReduceUnitRuntimeMaxHealthClamped(UnitRuntimeStatBlock& stats, u32 amount) {
    stats.max_health = amount >= stats.max_health ? 1 : stats.max_health - amount;
    stats.health = std::min(stats.health, stats.max_health);
}

void ReduceUnitRuntimeMaxSecondaryClamped(UnitRuntimeStatBlock& stats, u32 amount) {
    stats.max_secondary_value =
        amount > stats.max_secondary_value ? 0 : stats.max_secondary_value - amount;
    stats.secondary_value = std::min(stats.secondary_value, stats.max_secondary_value);
}

void SubtractUnitRuntimeStat1cClamped(UnitRuntimeStatBlock& stats, u32 amount) {
    stats.stat_1c = amount > stats.stat_1c ? 0 : stats.stat_1c - amount;
}

void SubtractUnitRuntimeStat20Clamped(UnitRuntimeStatBlock& stats, u32 amount) {
    stats.stat_20 = amount > stats.stat_20 ? 0 : stats.stat_20 - amount;
}

void SubtractUnitRuntimeStat28Clamped(UnitRuntimeStatBlock& stats, u32 amount) {
    stats.stat_28 = amount > stats.stat_28 ? 0 : stats.stat_28 - amount;
}

u32 CalculateUnitVariantStepBudget(const UnitMovementUnit& unit) {
    const UnitMovementDefinition& definition = unit.definition;
    const u32 next_variant = unit.production_variant + 1;
    const i32 signed_next_variant = WrappedU32ToI32(next_variant);
    if (signed_next_variant < 1) {
        return 0;
    }

    switch (definition.variant_step_mode) {
    case 0:
        return next_variant;
    case 1:
        return definition.variant_step_base +
            definition.variant_step_linear * next_variant;
    case 2:
        return definition.variant_step_base +
            definition.variant_step_linear * next_variant +
            definition.variant_step_extra;
    case 3:
        if (definition.variant_step_extra == 0) {
            return definition.variant_step_base;
        }
        return definition.variant_step_base +
            definition.variant_step_linear *
                static_cast<u32>(
                    signed_next_variant /
                    static_cast<i32>(definition.variant_step_extra));
    case 4: {
        u32 triangular = 0;
        for (i32 value = 1; value <= signed_next_variant; ++value) {
            triangular +=
                definition.variant_step_extra * static_cast<u32>(value);
        }
        const u32 result = definition.variant_step_base +
            definition.variant_step_linear * next_variant +
            triangular;
        return result;
    }
    default:
        return 0;
    }
}

u32 CalculateUnitVariantScaledBonus61a(const UnitMovementUnit& unit) {
    const u32 value =
        (unit.production_variant * unit.definition.variant_scaled_bonus61a_per_level) >> 3;
    return apply_signed_u16_cap(value, unit.definition.variant_scaled_bonus61a_cap);
}

u32 CalculateUnitVariantScaledBonus61b(const UnitMovementUnit& unit) {
    const u32 value =
        unit.production_variant * unit.definition.variant_scaled_bonus61b_per_level;
    return apply_signed_u16_cap(value, unit.definition.variant_scaled_bonus61b_cap);
}

u32 CalculateUnitVariantScaledBonus61c(const UnitMovementUnit& unit) {
    const u32 value =
        unit.production_variant * unit.definition.variant_scaled_bonus61c_per_level;
    return apply_signed_u16_cap(value, unit.definition.variant_scaled_bonus61c_cap);
}

u32 CalculateUnitVariantScaledBonus61d(const UnitMovementUnit& unit) {
    const u32 value =
        unit.production_variant * unit.definition.variant_scaled_bonus61d_per_level;
    return apply_signed_u16_cap(value, unit.definition.variant_scaled_bonus61d_cap);
}

UnitVariantGrowthResult CalculateUnitVariantGrowthRoll(const UnitMovementUnit& unit,
    UnitVariantRandomLimitCallback random_limit) {
    const UnitMovementDefinition& definition = unit.definition;
    const std::array<u32, 5> weights{
        definition.variant_growth_health_weight,
        definition.variant_growth_secondary_weight,
        definition.variant_growth_stat1c_weight,
        definition.variant_growth_stat20_weight,
        definition.variant_growth_stat28_weight,
    };
    u64 total_weight = 0;
    for (u32 weight : weights) {
        total_weight += static_cast<u64>(weight) * 10;
    }
    if (total_weight == 0 || total_weight > 0xffffffffu) {
        return {};
    }

    const u32 threshold = static_cast<u32>(total_weight);
    std::array<u32, 5> accumulators{};
    UnitVariantGrowthResult result{};
    u32 remaining = CalculateUnitVariantStepBudget(unit);
    while (remaining != 0) {
        bool consumed = false;
        auto consume = [&](u32 index, u32& destination) {
            while (remaining != 0 && accumulators[index] >= threshold) {
                accumulators[index] -= threshold;
                ++destination;
                --remaining;
                consumed = true;
            }
        };

        consume(0, result.health_steps);
        consume(1, result.secondary_steps);
        consume(2, result.stat_1c_steps);
        consume(3, result.stat_20_steps);
        consume(4, result.stat_28_steps);
        if (remaining == 0) {
            break;
        }

        if (consumed) {
            continue;
        }

        for (std::size_t i = 0; i < weights.size(); ++i) {
            const u32 random_window = weights[i] * 40 + 1;
            const u32 random_bonus =
                random_limit != nullptr ? random_limit(10000) % random_window : 0;
            accumulators[i] += weights[i] * 10 + random_bonus;
        }
    }
    return result;
}

void IncreaseUnitVariantStats(UnitMovementUnit& unit, UnitRuntimeStatBlock& stats,
    UnitVariantRandomLimitCallback random_limit) {
    const UnitVariantGrowthResult growth =
        CalculateUnitVariantGrowthRoll(unit, random_limit);
    const u32 health_delta =
        growth.health_steps * unit.definition.variant_health_delta_percent / 100;
    const u32 secondary_delta =
        growth.secondary_steps * unit.definition.variant_secondary_delta_percent / 100;

    stats.max_health += health_delta;
    stats.health += health_delta;
    stats.max_secondary_value += secondary_delta;
    stats.secondary_value += secondary_delta;
    stats.stat_1c += growth.stat_1c_steps;
    stats.stat_20 += growth.stat_20_steps;
    stats.stat_28 += growth.stat_28_steps;

    sync_unit_runtime_stats(unit, stats);
}

bool DecreaseUnitVariantStats(UnitMovementUnit& unit, UnitRuntimeStatBlock& stats,
    UnitVariantRandomLimitCallback random_limit) {
    if (unit.production_variant == 0) {
        return false;
    }

    const u32 original_variant = unit.production_variant;
    const u32 original_status_timer = unit.status_timer;
    --unit.production_variant;
    if (unit.status_timer != 0) {
        --unit.status_timer;
    }
    const UnitVariantGrowthResult growth =
        CalculateUnitVariantGrowthRoll(unit, random_limit);
    const u32 health_delta =
        growth.health_steps * unit.definition.variant_health_delta_percent / 100;
    const u32 secondary_delta =
        growth.secondary_steps * unit.definition.variant_secondary_delta_percent / 100;

    ReduceUnitRuntimeMaxHealthClamped(stats, health_delta);
    ReduceUnitRuntimeMaxSecondaryClamped(stats, secondary_delta);
    SubtractUnitRuntimeStat1cClamped(stats, growth.stat_1c_steps);
    SubtractUnitRuntimeStat20Clamped(stats, growth.stat_20_steps);
    SubtractUnitRuntimeStat28Clamped(stats, growth.stat_28_steps);
    ++unit.variant_reduction_count;
    unit.production_variant = original_variant;
    unit.status_timer = original_status_timer;

    sync_unit_runtime_stats(unit, stats);
    return true;
}

bool ApplyUnitVariantProgressFromStoredValue(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    UnitRuntimeStatBlock& stats, bool* attachment_effect_started,
    UnitVariantRandomLimitCallback random_limit) {
    bool changed = false;
    for (;;) {
        const u32 cost = CalculateOrder2bAdjustedUnitValue(production_state, unit,
            unit.definition.variant_progress_base_cost,
            unit.definition.variant_progress_cost_per_level);
        // FUN_004099e0 uses CMP/JGE: both raw dwords are interpreted as
        // signed values for the progress gate.  A wrapped high-bit progress
        // value therefore waits instead of producing spurious rank-ups.
        if (WrappedU32ToI32(unit.elite_progress_value) <
            WrappedU32ToI32(cost)) {
            break;
        }
        unit.elite_progress_value -= cost;
        IncreaseUnitVariantStats(unit, stats, random_limit);
        ++unit.production_variant;
        ++unit.status_timer;
        changed = true;
    }

    if (attachment_effect_started != nullptr) {
        *attachment_effect_started = changed;
    }
    return changed;
}

void RebuildUnitRuntimeStatsFromDefinitionAndParents(UnitMovementUnit& unit,
    UnitRuntimeStatBlock& stats, const UnitMovementUnit* parent_a,
    const UnitRuntimeStatBlock* parent_a_stats, const UnitMovementUnit* parent_b,
    const UnitRuntimeStatBlock* parent_b_stats,
    UnitVariantRandomLimitCallback random_limit) {
    u32 target_variant = unit.production_variant;
    u32 progress_value = unit.elite_progress_value;
    u32 health_percent =
        stats.max_health == 0 ? 100 : stats.health * 100 / stats.max_health;
    u32 secondary_percent = stats.max_secondary_value == 0
        ? 0
        : stats.secondary_value * 100 / stats.max_secondary_value;

    if (parent_a != nullptr && parent_a_stats != nullptr) {
        const bool use_second_parent = unit.type_id == 0x2b &&
            parent_b != nullptr && parent_b_stats != nullptr;
        const u32 divisor = use_second_parent ? 3 : 2;
        // Original FUN_00408c80 adds raw unit +0x2c from every consumed
        // parent without averaging it.  This is separate from the averaged
        // variant/progress fields below.
        unit.action_mode += parent_a->action_mode;
        if (use_second_parent) {
            unit.action_mode += parent_b->action_mode;
        }
        // FUN_00408c80 has a deliberate type-0x2b quirk: raw +0x54 from the
        // first linked parent is added twice when averaging the rebuilt level.
        target_variant = (target_variant + parent_a->production_variant +
            (use_second_parent ? parent_a->production_variant : 0)) / divisor;
        progress_value = (progress_value + parent_a->elite_progress_value +
            (use_second_parent ? parent_b->elite_progress_value : 0)) / divisor;
        unit.elite_progress_count += parent_a->elite_progress_count +
            (use_second_parent ? parent_b->elite_progress_count : 0);

        const u32 max_health_sum = stats.max_health + parent_a_stats->max_health +
            (use_second_parent ? parent_b_stats->max_health : 0);
        const u32 health_sum = stats.health + parent_a_stats->health +
            (use_second_parent ? parent_b_stats->health : 0);
        health_percent = max_health_sum == 0 ? 100 : health_sum * 100 / max_health_sum;

        // The two-parent path divides by self.max_secondary twice in the
        // original; only type 0x2b sums all three maximums.
        const u32 max_secondary_sum = use_second_parent ?
            stats.max_secondary_value + parent_a_stats->max_secondary_value +
                parent_b_stats->max_secondary_value :
            stats.max_secondary_value + stats.max_secondary_value;
        const u32 secondary_sum = stats.secondary_value + parent_a_stats->secondary_value +
            (use_second_parent ? parent_b_stats->secondary_value : 0);
        secondary_percent =
            max_secondary_sum == 0 ? 0 : secondary_sum * 100 / max_secondary_sum;
    }

    unit.elite_progress_value = progress_value;
    unit.production_variant = 0;
    unit.status_timer = 0;
    stats.max_health = std::max<u32>(1, unit.definition.initial_max_health);
    stats.max_secondary_value = unit.definition.initial_max_secondary_value;
    stats.health = stats.max_health;
    stats.secondary_value = stats.max_secondary_value;
    stats.stat_1c = unit.definition.profile_offense_value;
    stats.stat_20 = unit.definition.profile_defense_value;
    stats.stat_28 = unit.definition.initial_secondary_value;

    for (u32 i = 0; i < target_variant; ++i) {
        IncreaseUnitVariantStats(unit, stats, random_limit);
        ++unit.production_variant;
        ++unit.status_timer;
    }

    stats.health = std::max<u32>(1, stats.max_health * health_percent / 100);
    stats.secondary_value = stats.max_secondary_value * secondary_percent / 100;
    sync_unit_runtime_stats(unit, stats);
}

u32 ResolveUnitActionProfileIndexForTarget(const UnitMovementUnit& source,
    u32 target_render_class) {
    return target_render_class == 3
        ? source.definition.action_profile_index_vs_class3
        : source.definition.action_profile_index;
}

u32 CalculateUnitMaxHealthWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit) {
    // Original 0x004084d0 reads raw unit +0x1c (the mutable OP value), not
    // raw +0x10/max_health.  This helper's legacy name is misleading: its
    // result is the source offense used by both damage and the selected HUD.
    u32 value = AddSignedUnitStatDelta(unit.runtime_stat_1c,
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotUnitMaxHealth));

    // CalculateUnitMaxHealthWithProductionEffects (0x004084d0) applies both
    // temporary combat modifiers after production effect slot 2.  Omitting
    // these made the 0x4000 source buff and the 0x1000 runtime form deal their
    // unmodified damage even though the effect runtime set the original flags.
    if ((unit.command_flags & 0x4000u) != 0) {
        const u64 adjusted = static_cast<u64>(value) +
            (static_cast<u64>(value) *
                kCommandFlag4000MaxHealthBonusPercent) / 100u;
        value = adjusted > 0xffffffffu
            ? 0xffffffffu : static_cast<u32>(adjusted);
    }
    if ((unit.runtime_flags & 0x1000u) != 0) {
        const u64 adjusted = static_cast<u64>(value) + value / 2u;
        value = adjusted > 0xffffffffu
            ? 0xffffffffu : static_cast<u32>(adjusted);
    }
    return value;
}

u32 CalculateUnitMaxSecondaryValueWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit) {
    // Original 0x004085a8..0x004085c6 reads raw unit +0x20 before adding
    // production-effect table 3.  That slot is the definition +0x15c runtime
    // defense value mirrored by runtime_stat_20; raw +0x14/max_secondary_value
    // is the separate secondary-capacity field used by the HUD/runtime meter.
    u32 value = AddSignedUnitStatDelta(unit.runtime_stat_20,
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotUnitMaxSecondaryValue));

    // The original defense helper first adds max-health / 10 + 1 while
    // command flag 0x20000 is active, then applies the DAT_0120c9c8 percent
    // bonus for either runtime flag 0x400 or 0x800.  Both flags are emitted by
    // reconstructed unit effects, so the helper must consume them as well.
    if ((unit.command_flags & 0x20000u) != 0) {
        const u32 max_health =
            CalculateUnitMaxHealthWithProductionEffects(production_state, unit);
        const u64 adjusted = static_cast<u64>(value) + max_health / 10u + 1u;
        value = adjusted > 0xffffffffu
            ? 0xffffffffu : static_cast<u32>(adjusted);
    }
    if ((unit.runtime_flags & 0x0c00u) != 0) {
        const u64 adjusted = static_cast<u64>(value) +
            (static_cast<u64>(value) *
                kRuntimeFlag0c00DefenseBonusPercent) / 100u;
        value = adjusted > 0xffffffffu
            ? 0xffffffffu : static_cast<u32>(adjusted);
    }
    return value;
}

u32 CalculateUnitActionDamageWithDefinitionModifiers(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& source, const UnitMovementUnit& target,
    const UnitActionDamageProfileTable* damage_profiles,
    UnitVariantRandomLimitCallback random_limit) {
    if ((source.runtime_flags & 0x10u) != 0) {
        return 0;
    }

    const u32 target_max_secondary =
        CalculateUnitMaxSecondaryValueWithProductionEffects(production_state, target);
    const u32 source_max_health =
        CalculateUnitMaxHealthWithProductionEffects(production_state, source);
    i64 damage = static_cast<i64>(source_max_health) -
        static_cast<i64>(target_max_secondary);

    if (damage < 1) {
        if (source_max_health == 0) {
            return 0;
        }
        const u64 scaled_limit =
            (static_cast<u64>(target_max_secondary) << 1) / source_max_health;
        const u32 limit = scaled_limit > 0xffffffffu
            ? 0xffffffffu
            : static_cast<u32>(scaled_limit);
        const u32 selected =
            limit == 0 ? 0 :
                (random_limit != nullptr ? random_limit(limit) % limit : 0);
        return selected == 0 ? 1 : 0;
    }

    const u32 profile_index =
        ResolveUnitActionProfileIndexForTarget(source, target.definition.render_class);
    if (const UnitActionDamageProfile* profile =
            unit_action_damage_profile(damage_profiles, profile_index)) {
        const u32 projectile_impact_class = target.definition.projectile_impact_class;
        if (projectile_impact_class <
            profile->projectile_impact_class_percent.size()) {
            damage = apply_unit_action_damage_percent(
                damage,
                profile->projectile_impact_class_percent[projectile_impact_class]);
        }

        const u32 target_render_class = target.definition.render_class;
        if (target_render_class < profile->render_class_percent.size()) {
            damage = apply_unit_action_damage_percent(
                damage, profile->render_class_percent[target_render_class]);
        }
    }

    return clamp_unit_action_damage(damage);
}

u32 CalculateOrder2bAdjustedUnitValue(const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 base_value, u32 per_variant_delta,
    u32 minimum_value) {
    u32 wrapped_value = base_value + unit.production_variant * per_variant_delta;
    const i32 bonus = GetProductionOrderOrder2bBonusTotal(production_state, unit.owner_id);
    wrapped_value -= static_cast<u32>(bonus);

    const i32 signed_value = WrappedU32ToI32(wrapped_value);
    const i32 signed_minimum = minimum_value > 0x7fffffffu ?
        std::numeric_limits<i32>::max() : static_cast<i32>(minimum_value);
    if (signed_value < signed_minimum) {
        return minimum_value;
    }
    return static_cast<u32>(signed_value);
}

u32 CalculateUnitActionRecoveryReductionWithProductionEffect05(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit) {
    // CalculateUnitActionRecoveryReductionWithProductionEffect05
    // (0x0040a830) multiplies the base by original raw unit +0x54.  That
    // field is the unit's production/growth variant; cargo lives at +0x4c.
    // Using cargo made a ranked combat unit's recovery one tick too long and
    // incorrectly tied attack speed to carried resources.
    const u64 scaled = static_cast<u64>(unit.definition.action_recovery_base_ticks) *
        unit.production_variant * unit.definition.action_recovery_scale_percent / 100;
    const u32 base = scaled > 0xffffffffu ? 0xffffffffu : static_cast<u32>(scaled);
    return AddSignedUnitStatDelta(base,
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotUnitScaledValue));
}

u32 CalculateUnitActionRecoveryTicksWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    const UnitEquipmentCatalog* equipment_catalog) {
    const u32 base = unit.definition.action_recovery_base_ticks;
    if (base == 0) {
        return 0;
    }

    i64 reduction = static_cast<i64>(
        CalculateUnitActionRecoveryReductionWithProductionEffect05(production_state, unit));
    if (equipment_catalog != nullptr) {
        reduction += CalculateUnitEquipmentActionRecoveryModifier(unit,
            *equipment_catalog);
    }
    const i64 remaining = static_cast<i64>(base) - reduction;
    if (remaining <= 0) {
        return 0;
    }
    return remaining > static_cast<i64>(0xffffffffu)
        ? 0xffffffffu
        : static_cast<u32>(remaining);
}

u32 CalculateUnitActionRangeScaledBonus(const UnitMovementUnit& unit) {
    const u32 bonus = unit.production_variant *
        unit.definition.action_range_bonus_per_count;
    return apply_signed_u16_cap(bonus, unit.definition.action_range_bonus_cap);
}

u32 CalculateUnitActionRangeWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 target_render_class, const UnitEquipmentCatalog* equipment_catalog) {
    const u32 base = target_render_class == 3
        ? unit.definition.action_range_base_vs_class3
        : unit.definition.action_range_base;
    u64 value = static_cast<u64>(base) + CalculateUnitActionRangeScaledBonus(unit);
    if (value > 0xffffffffu) {
        value = 0xffffffffu;
    }

    i32 modifier = GetUnitProductionCompletionEffect(production_state, unit,
        kProductionEffectSlotUnitActionRange);
    if (equipment_catalog != nullptr) {
        modifier += CalculateUnitEquipmentActionRangeModifier(unit, *equipment_catalog);
    }
    return AddSignedUnitStatDelta(static_cast<u32>(value), modifier);
}

UnitMovementPoint CalculateUnitMovementFrameDeltaWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    i32 additional_modifier, const UnitEquipmentCatalog* equipment_catalog) {
    const UnitMovementPoint base = CalculateUnitMovementFrameDelta(unit);
    if (unit.owner_id >= kProductionOrderOwnerCount) {
        return base;
    }
    i32 modifier = additional_modifier +
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotUnitMovementDelta);
    if (equipment_catalog != nullptr) {
        modifier += CalculateUnitEquipmentMovementFrameModifier(unit,
            *equipment_catalog);
    }
    return ApplyUnitMovementDeltaModifier(base, modifier);
}

UnitMovementPoint CalculateUnitMovementFrameDeltaForDirectionWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 direction, i32 additional_modifier,
    const UnitEquipmentCatalog* equipment_catalog) {
    const UnitMovementPoint base = CalculateUnitMovementFrameDeltaForDirection(unit,
        direction);
    if (unit.owner_id >= kProductionOrderOwnerCount) {
        return base;
    }
    i32 modifier = additional_modifier +
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotUnitMovementDelta);
    if (equipment_catalog != nullptr) {
        modifier += CalculateUnitEquipmentMovementFrameModifier(unit,
            *equipment_catalog);
    }
    return ApplyUnitMovementDeltaModifier(base, modifier);
}

u32 LookupConfiguredPointDirection(UnitMovementPoint source, UnitMovementPoint target,
    const UnitDirectionLookupTable& lookup, bool use_16_direction_lookup) {
    return use_16_direction_lookup
        ? CalculatePointDirection16FromLookup(source, target, lookup)
        : CalculatePointDirectionFromLookup(source, target, lookup);
}

u32 CalculateUnitMovementStepLimitWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 base_step_limit, i32 additional_modifier,
    const UnitEquipmentCatalog* equipment_catalog) {
    if (unit.owner_id >= kProductionOrderOwnerCount) {
        return base_step_limit;
    }
    i32 modifier = additional_modifier +
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotUnitMovementDelta);
    if (equipment_catalog != nullptr) {
        modifier += CalculateUnitEquipmentMovementFrameModifier(unit,
            *equipment_catalog);
    }
    return base_step_limit + static_cast<u32>(modifier);
}

u32 CalculateMovementStepDistanceThreshold(u32 step_limit, u32 movement_period) {
    if (step_limit < kMovementStepTriangularThresholds.size()) {
        return kMovementStepTriangularThresholds[step_limit] * movement_period;
    }

    u32 threshold = kMovementStepTriangularThresholds.back();
    for (u32 step = static_cast<u32>(kMovementStepTriangularThresholds.size());
         step <= step_limit; ++step) {
        threshold += step * movement_period;
    }
    return threshold;
}

u32 CalculateMovementStepFromDistance(u32 distance, u32 movement_period) {
    const i32 signed_distance = WrappedU32ToI32(distance);
    u32 threshold = 1;
    u32 step = 1;
    while (WrappedU32ToI32(threshold) <= signed_distance) {
        ++step;
        threshold += step * movement_period;
    }
    return step - 1;
}

void ResetUnitMovementInterpolationState(UnitMovementUnit& unit,
    u32 command_metadata_flags, std::optional<u32> random_animation_frame) {
    unit.movement_flags &= ~kUnitMovementFlagInterpolatingTowardTarget;
    unit.movement_residual_x = 0;
    unit.movement_residual_y = 0;
    unit.movement_interpolation_x = 0.0f;
    unit.movement_interpolation_y = 0.0f;
    unit.movement_turn_ticks = 0;

    if ((command_metadata_flags & kUnitCommandMetadataPreserveAnimationFrame) != 0) {
        return;
    }

    const u32 frame_count = unit.definition.movement_animation_frame_count;
    unit.animation_frame =
        frame_count == 0 ? 0 : random_animation_frame.value_or(0) % frame_count;
}

void ResetUnitMovementStepProgress(UnitMovementUnit& unit) {
    refresh_unit_current_cell(unit);
    unit.movement_step_accumulator = 0;
    unit.movement_turn_ticks = 0;
}

UnitMovementStepAdvanceResult AdvanceUnitMovementPositionTowardTarget(
    UnitMovementUnit& unit, u32 direction_count, u32 movement_period,
    u32 map_width_tiles, u32 map_height_tiles) {
    if (movement_period == 0) {
        movement_period = 1;
    }

    UnitMovementStepAdvanceResult result;
    result.before_distance = CalculateApproxUnitDistance(unit.x, unit.y,
        unit.path_target_x, unit.path_target_y);
    const i32 old_x = unit.x;
    const i32 old_y = unit.y;
    const u32 scaled_step = unit.movement_step_accumulator / movement_period;

    if ((unit.movement_flags & kUnitMovementFlagInterpolatingTowardTarget) == 0) {
        const UnitMovementPoint delta = direction_count == 16
            ? GetUnitMovementDirection16Delta(unit.direction)
            : GetUnitMovementDirection8Delta(unit.direction);
        unit.movement_residual_x += delta.x * static_cast<i32>(scaled_step);
        unit.movement_residual_y += delta.y * static_cast<i32>(scaled_step);

        if (std::abs(unit.movement_residual_x) > 4) {
            unit.x += unit.movement_residual_x / 4;
            unit.movement_residual_x %= 4;
            unit.x = clamp_world_coordinate_to_tiles(unit.x, map_width_tiles);
            unit.current_cell_x = unit.x & ~0x1f;
        }
        if (std::abs(unit.movement_residual_y) > 4) {
            unit.y += unit.movement_residual_y / 4;
            unit.movement_residual_y %= 4;
            unit.y = clamp_world_coordinate_to_tiles(unit.y, map_height_tiles);
            unit.current_cell_y = unit.y & ~0x1f;
        }

        result.after_distance = CalculateApproxUnitDistance(unit.x, unit.y,
            unit.path_target_x, unit.path_target_y);
        result.moved = unit.x != old_x || unit.y != old_y;
        if (result.after_distance <= 1 && result.after_distance > result.before_distance) {
            ResetUnitMovementStepProgress(unit);
            result.reset_progress = true;
        }
        return result;
    }

    result.used_interpolation = true;
    float step_x = 0.0f;
    float step_y = 1.0f;
    const float from_x = unit.movement_interpolation_x;
    const float from_y = unit.movement_interpolation_y;
    const float to_x = static_cast<float>(unit.path_target_x);
    const float to_y = static_cast<float>(unit.path_target_y);
    const float dx = to_x - from_x;
    const float dy = from_y - to_y;
    if (dx != 0.0f) {
        // The original x87 sequence promotes both float operands before FDIV,
        // so retain the double-precision quotient passed to atan.
        const double angle = std::atan(
            static_cast<double>(dy) / static_cast<double>(dx));
        const float stored_angle = static_cast<float>(angle);
        step_x = static_cast<float>(std::abs(std::cos(angle)));
        step_y = static_cast<float>(
            std::abs(std::sin(static_cast<double>(stored_angle))));
    }
    step_x *= static_cast<float>(scaled_step);
    step_y *= static_cast<float>(scaled_step);

    if (static_cast<u32>(unit.x) < static_cast<u32>(unit.path_target_x)) {
        unit.movement_interpolation_x = from_x + step_x;
    }
    else if (static_cast<u32>(unit.path_target_x) < static_cast<u32>(unit.x)) {
        unit.movement_interpolation_x = from_x - step_x;
    }
    if (static_cast<u32>(unit.y) < static_cast<u32>(unit.path_target_y)) {
        unit.movement_interpolation_y = from_y + step_y;
    }
    else if (static_cast<u32>(unit.path_target_y) < static_cast<u32>(unit.y)) {
        unit.movement_interpolation_y = from_y - step_y;
    }

    unit.x = static_cast<i32>(unit.movement_interpolation_x);
    unit.y = static_cast<i32>(unit.movement_interpolation_y);
    result.after_distance = CalculateApproxUnitDistance(unit.x, unit.y,
        unit.path_target_x, unit.path_target_y);
    result.moved = unit.x != old_x || unit.y != old_y;

    if (result.after_distance <= result.before_distance || result.after_distance > 9) {
        refresh_unit_current_cell(unit);
    }
    else {
        ResetUnitMovementStepProgress(unit);
        result.reset_progress = true;
    }
    return result;
}

u32 ResolveMovementStepLimitForDistance(u32 distance, u32 movement_period,
    u32 base_step_limit) {
    if (movement_period == 0) {
        movement_period = 1;
    }
    const u32 threshold =
        CalculateMovementStepDistanceThreshold(base_step_limit, movement_period);
    if (distance < threshold) {
        return CalculateMovementStepFromDistance(distance, movement_period);
    }
    return base_step_limit;
}

bool AdjustUnitMovementStepAccumulatorTowardLimit(UnitMovementUnit& unit,
    u32 movement_period, u32 step_limit) {
    if (movement_period == 0) {
        movement_period = 1;
    }
    if (step_limit == 0) {
        return false;
    }
    const u32 target_accumulator = step_limit * movement_period;
    if (unit.movement_step_accumulator < target_accumulator) {
        ++unit.movement_step_accumulator;
    }
    if (target_accumulator < unit.movement_step_accumulator) {
        --unit.movement_step_accumulator;
    }
    return unit.movement_step_accumulator != 0;
}

UnitMovementCoreUpdateResult UpdateUnitMovementTowardPathTarget(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    const UnitMovementCoreUpdateConfig& config) {
    UnitMovementCoreUpdateResult result;
    result.direction_count = config.use_16_direction_lookup ? 16 : 8;
    result.movement_period = config.movement_period == 0 ? 1 : config.movement_period;

    const UnitMovementPoint source{unit.x, unit.y};
    const UnitMovementPoint target{unit.path_target_x, unit.path_target_y};
    if (config.use_16_direction_lookup && config.direction_lookup_16 != nullptr) {
        result.target_direction =
            CalculatePointDirection16FromLookup(source, target, *config.direction_lookup_16);
    }
    else if (config.direction_lookup_8 != nullptr) {
        result.target_direction =
            CalculatePointDirectionFromLookup(source, target, *config.direction_lookup_8);
    }
    else {
        result.target_direction =
            CalculateUnitDirectionToPoint(unit, unit.path_target_x, unit.path_target_y);
    }

    result.turn = PrepareUnitMovementDirectionForStep(unit,
        result.target_direction, result.direction_count);
    if (!result.turn.can_advance) {
        return result;
    }

    result.advance = AdvanceUnitMovementPositionTowardTarget(unit,
        result.direction_count, result.movement_period, config.map_width_tiles,
        config.map_height_tiles);
    if (result.advance.reset_progress) {
        return result;
    }

    result.base_step_limit = CalculateUnitMovementStepLimitWithProductionEffects(
        production_state, unit, config.base_step_limit,
        config.additional_movement_modifier, config.equipment_catalog);
    result.resolved_step_limit = ResolveMovementStepLimitForDistance(
        result.advance.after_distance, result.movement_period, result.base_step_limit);
    result.accumulator_active = AdjustUnitMovementStepAccumulatorTowardLimit(
        unit, result.movement_period, result.resolved_step_limit);
    if (!result.accumulator_active) {
        ResetUnitMovementStepProgress(unit);
    }
    return result;
}

u32 CalculateUnitInteractionRangeWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 base_range, const UnitEquipmentCatalog* equipment_catalog) {
    i32 modifier = GetUnitProductionCompletionEffect(production_state, unit,
        kProductionEffectSlotUnitInteractionRange);
    if (equipment_catalog != nullptr) {
        modifier += CalculateUnitEquipmentInteractionRangeModifier(unit,
            *equipment_catalog);
    }
    return AddSignedUnitStatDelta(base_range, modifier);
}

u32 CalculateUnitDamageReactionAllyRange(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    const UnitEquipmentCatalog* equipment_catalog) {
    // HandleUnitDamageReaction 0x004c2587 calls the shared interaction-range
    // helper and halves its ECX result at 0x004c25a0.  That helper reads raw
    // definition +0x440, which is archive +0x198; support/acquisition +0x19c
    // and action range +0x1b0 are unrelated fields.
    return CalculateUnitInteractionRangeWithProductionAndEquipmentEffects(
        production_state, unit,
        unit.definition.effect_adjusted_interaction_range_base,
        equipment_catalog) >> 1;
}

u32 CalculateUnitTransportCapacityWithProductionEffect09(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 base_capacity) {
    return AddSignedUnitStatDelta(base_capacity,
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotTransportCapacity));
}

bool CheckUnitCommandGateWithProductionEffect12(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    bool helper_allows, u32 definition_flags, u32 command_metadata_flags,
    const UnitEquipmentCatalog* equipment_catalog) {
    if ((unit.runtime_flags & 0x10) != 0) {
        return false;
    }
    const bool equipment_allows = equipment_catalog != nullptr &&
        CalculateUnitEquipmentCommandGateModifier(unit, *equipment_catalog) != 0;
    if (helper_allows || equipment_allows) {
        return true;
    }
    const bool definition_or_upgrade_allows = (definition_flags & 1) != 0 ||
        GetUnitProductionCompletionEffect(production_state, unit,
            kProductionEffectSlotUnitCommandGate) != 0;
    if (!definition_or_upgrade_allows) {
        return false;
    }
    if ((command_metadata_flags & 0x80) != 0) {
        return false;
    }
    if ((unit.runtime_flags & 0x20) != 0) {
        return false;
    }
    // Original raw +0x30 is the construction/action gate field.  Movement
    // state lives at raw +0xb0 and must not suppress this command gate.
    if (unit.type_id >= 0x60 && unit.action_mode_gate == 1) {
        return false;
    }
    return true;
}

template <typename Visit>
bool WalkLegacyStraightTilePath(UnitMovementPoint start_tile, UnitMovementPoint goal_tile,
    const Visit& visit) {
    i32 x = start_tile.x;
    i32 y = start_tile.y;
    const i32 dx = std::abs(start_tile.x - goal_tile.x);
    const i32 dy = std::abs(start_tile.y - goal_tile.y);
    i32 step_x = goal_tile.x < start_tile.x ? -1 : 1;
    i32 step_y = goal_tile.y < start_tile.y ? -1 : 1;
    if (dx == 0) {
        step_x = 0;
    }
    if (dy == 0) {
        step_y = 0;
    }

    i32 accum_x = dx;
    i32 accum_y = dy;
    const bool x_major = dy < dx;
    while (x != goal_tile.x || y != goal_tile.y) {
        if (x_major) {
            x += step_x;
            accum_y += dy;
            if (dx < accum_y) {
                y += step_y;
                accum_y -= dx;
            }
        } else {
            y += step_y;
            accum_x += dx;
            if (dy < accum_x) {
                x += step_x;
                accum_x -= dy;
            }
        }

        if (!visit(UnitMovementPoint{x, y})) {
            return false;
        }
    }

    return true;
}

bool CheckStraightUnitPathTiles(UnitMovementContext& context, const UnitMovementUnit& unit,
    UnitMovementPoint start_tile, UnitMovementPoint goal_tile) {
    if (!tile_point_in_bounds(context.map, start_tile) ||
        !tile_point_in_bounds(context.map, goal_tile)) {
        return false;
    }

    return WalkLegacyStraightTilePath(start_tile, goal_tile,
        [&](UnitMovementPoint point) {
            return check_pathfinder_tile_can_enter(context, unit, point);
        });
}

std::vector<UnitMovementPoint> BuildStraightUnitPathTiles(
    UnitMovementPoint start_tile, UnitMovementPoint goal_tile) {
    std::vector<UnitMovementPoint> path;
    path.push_back(start_tile);
    WalkLegacyStraightTilePath(start_tile, goal_tile,
        [&](UnitMovementPoint point) {
            path.push_back(point);
            return true;
        });
    return path;
}

bool FindNearestPathableGoalTile(UnitMovementContext& context, const UnitMovementUnit& unit,
    UnitMovementPoint start_tile, UnitMovementPoint desired_goal_tile,
    UnitMovementPoint& resolved_goal_tile) {
    if (context.map.width == 0 || context.map.height == 0) {
        return false;
    }

    i32 min_x = desired_goal_tile.x - 1;
    i32 min_y = desired_goal_tile.y - 1;
    i32 max_x = desired_goal_tile.x + 1;
    i32 max_y = desired_goal_tile.y + 1;
    // FindNearestPathableGoalTile stores its Manhattan score in a 16-bit
    // stack slot (0x00508aa0/0x00508b60/0x00508c46/0x00508d05).  Keep the
    // truncation and strict tie rule observable on large maps.
    u16 best_distance = std::numeric_limits<u16>::max();
    bool found = false;

    auto consider_candidate = [&](i32 x, i32 y) {
        if (!check_pathfinder_tile_can_enter(context, unit, UnitMovementPoint{x, y})) {
            return;
        }
        const u16 distance = static_cast<u16>(
            std::abs(x - start_tile.x) + std::abs(y - start_tile.y));
        if (!found || distance < best_distance) {
            resolved_goal_tile = UnitMovementPoint{x, y};
            best_distance = distance;
            found = true;
        }
    };

    const i32 max_iterations = static_cast<i32>(context.map.width + context.map.height + 4);
    for (i32 iteration = 0; iteration < max_iterations; ++iteration) {
        for (i32 x = min_x; x < max_x; ++x) {
            consider_candidate(x, min_y);
            consider_candidate(x, max_y);
        }
        // Original 0x00508bf1 compares the side cursor against max_y - 1.
        // The asymmetric perimeter deliberately omits y == max_y - 1 (as
        // well as the right corners already omitted by x < max_x above).
        // Including that row can select a closer east-side fallback that the
        // shipped pathfinder never examines.
        for (i32 y = min_y + 1; y < max_y - 1; ++y) {
            consider_candidate(min_x, y);
            consider_candidate(max_x, y);
        }
        if (found) {
            return true;
        }

        // Original 0x00508d94..0x00508dbd uses JC/JA for all four bounds.
        // This unsigned comparison is significant at the top/left map edge:
        // desired y == 0 produces min_y == -1, which must not make a nearby
        // start tile look enclosed.  A signed comparison stopped one ring
        // early and selected the start tile instead of the original fallback.
        const u32 start_x = static_cast<u32>(start_tile.x);
        const u32 start_y = static_cast<u32>(start_tile.y);
        if (static_cast<u32>(min_x) <= start_x &&
            start_x <= static_cast<u32>(max_x) &&
            static_cast<u32>(min_y) <= start_y &&
            start_y <= static_cast<u32>(max_y)) {
            break;
        }

        const i32 old_min_x = min_x;
        const i32 old_min_y = min_y;
        const i32 old_max_x = max_x;
        const i32 old_max_y = max_y;
        if (min_x != 0) {
            --min_x;
        }
        if (min_y != 0) {
            --min_y;
        }
        if (static_cast<u32>(max_x) < context.map.width) {
            ++max_x;
        }
        if (static_cast<u32>(max_y) < context.map.height) {
            ++max_y;
        }
        if (old_min_x == min_x && old_min_y == min_y &&
            old_max_x == max_x && old_max_y == max_y) {
            break;
        }
    }

    return false;
}

LegacyPathfinderScratchState& GetLegacyPathfinderScratchState() {
    return g_legacy_pathfinder_scratch;
}

void ConfigureLegacyPathfinderGrid(i32 width_tiles, i32 height_tiles) {
    g_legacy_pathfinder_scratch.width_tiles = std::max<i32>(0, width_tiles);
    g_legacy_pathfinder_scratch.height_tiles = std::max<i32>(0, height_tiles);
    const u64 total_tiles =
        static_cast<u64>(g_legacy_pathfinder_scratch.width_tiles) *
        static_cast<u64>(g_legacy_pathfinder_scratch.height_tiles);
    g_legacy_pathfinder_scratch.visit_budget =
        static_cast<u32>(std::min<u64>(total_tiles / 3, 0xffffffffu));
    g_legacy_pathfinder_scratch.checked_tile_count = 0;
    g_legacy_pathfinder_scratch.neighbor_cursor = {};
    g_legacy_pathfinder_scratch.neighbor_direction = 0;
}

UnitMovementPoint AdvanceLegacyPathfinderNeighborCursor() {
    const UnitMovementPoint delta = kLegacyPathfinderNeighborStepDeltas[
        g_legacy_pathfinder_scratch.neighbor_direction %
            kLegacyPathfinderNeighborStepDeltas.size()];
    g_legacy_pathfinder_scratch.neighbor_cursor.x += delta.x;
    g_legacy_pathfinder_scratch.neighbor_cursor.y += delta.y;
    g_legacy_pathfinder_scratch.neighbor_direction =
        (g_legacy_pathfinder_scratch.neighbor_direction + 1) %
        kLegacyPathfinderNeighborStepDeltas.size();
    return g_legacy_pathfinder_scratch.neighbor_cursor;
}

bool CheckLegacyPathfinderTileCanEnter(UnitMovementContext& context,
    const UnitMovementUnit& unit, i32 tile_x, i32 tile_y) {
    ++g_legacy_pathfinder_scratch.checked_tile_count;
    return check_pathfinder_tile_can_enter(context, unit,
        UnitMovementPoint{tile_x, tile_y});
}

bool RunLegacyUnitPathfinder(UnitMovementContext& context, UnitMovementUnit& unit,
    std::vector<UnitMovementPoint>* out_path_tiles) {
    if (out_path_tiles != nullptr) {
        out_path_tiles->clear();
    }
    if (context.map.width == 0 || context.map.height == 0 || context.map.cells.empty()) {
        return false;
    }

    UnitMovementPoint start_tile = signed_world_point_to_tile(unit.x, unit.y);
    UnitMovementPoint goal_tile =
        signed_world_point_to_tile(unit.path_target_x, unit.path_target_y);
    if (!tile_point_in_bounds(context.map, start_tile)) {
        if (out_path_tiles != nullptr) {
            out_path_tiles->push_back(start_tile);
        }
        return false;
    }

    UnitMovementPoint resolved_goal = goal_tile;
    bool goal_adjusted = false;
    auto apply_legacy_pathfinder_outputs =
        [&](UnitMovementPoint path_target_tile, UnitMovementPoint next_path_tile,
            bool direct_path, bool publish_waypoint = true) {
            g_legacy_pathfinder_scratch.direct_path = direct_path ? 1u : 0u;
            if (publish_waypoint) {
                g_legacy_pathfinder_scratch.waypoint_tile = next_path_tile;
            }
            if (goal_adjusted) {
                const UnitMovementPoint target_center =
                    tile_center_point(path_target_tile);
                unit.path_target_x = target_center.x;
                unit.path_target_y = target_center.y;
            }
            if (g_legacy_pathfinder_scratch.direct_path != 1u) {
                const UnitMovementPoint next_center = tile_center_point(
                    g_legacy_pathfinder_scratch.waypoint_tile);
                unit.next_path_x = next_center.x;
                unit.next_path_y = next_center.y;
            }
        };

    if (!check_pathfinder_tile_can_enter(context, unit, goal_tile) &&
        !FindNearestPathableGoalTile(context, unit, start_tile, goal_tile, resolved_goal)) {
        goal_adjusted = true;
        apply_legacy_pathfinder_outputs(start_tile, start_tile, false);
        if (out_path_tiles != nullptr) {
            out_path_tiles->push_back(start_tile);
        }
        return false;
    }
    goal_adjusted =
        resolved_goal.x != goal_tile.x || resolved_goal.y != goal_tile.y;

    if (start_tile.x == resolved_goal.x && start_tile.y == resolved_goal.y) {
        apply_legacy_pathfinder_outputs(resolved_goal, resolved_goal,
            !goal_adjusted);
        if (out_path_tiles != nullptr) {
            out_path_tiles->push_back(start_tile);
        }
        return true;
    }

    if (CheckStraightUnitPathTiles(context, unit, start_tile, resolved_goal)) {
        apply_legacy_pathfinder_outputs(resolved_goal, resolved_goal, true);
        if (out_path_tiles != nullptr) {
            *out_path_tiles = BuildStraightUnitPathTiles(start_tile, resolved_goal);
        }
        return true;
    }

    const u32 start_index = tile_point_index(context.map, start_tile);
    const u32 goal_index = tile_point_index(context.map, resolved_goal);
    const u32 storage_tiles = static_cast<u32>(
        std::min<std::size_t>(
            context.map.cells.size(), std::numeric_limits<u32>::max()));
    if (start_index >= storage_tiles || goal_index >= storage_tiles) {
        return false;
    }

    std::vector<u16> distance(storage_tiles, 0xffffu);
    // Original 0x00508e50 keeps a fixed 0x400-entry open list.  It is not a
    // FIFO queue: every iteration scans the current array and removes the
    // first strict-minimum Manhattan score by replacing it with the last
    // entry.  The resulting order (including equal-score ties) is observable
    // in the chosen path.
    constexpr u32 kLegacyOpenCapacity = 0x400;
    std::array<UnitMovementPoint, kLegacyOpenCapacity> open_points{};
    std::array<u16, kLegacyOpenCapacity> open_scores{};

    const auto heuristic_score = [&](UnitMovementPoint point) {
        const u32 dx = static_cast<u32>(std::abs(point.x - resolved_goal.x));
        const u32 dy = static_cast<u32>(std::abs(point.y - resolved_goal.y));
        return static_cast<u16>(static_cast<u16>(dx) + static_cast<u16>(dy));
    };

    ConfigureLegacyPathfinderGrid(static_cast<i32>(context.map.width),
        static_cast<i32>(context.map.height));
    g_legacy_pathfinder_scratch.neighbor_cursor = start_tile;
    const u32 visit_budget = g_legacy_pathfinder_scratch.visit_budget;
    u32 checked_count = 0;
    distance[start_index] = 1;
    open_points[0] = start_tile;
    open_scores[0] = heuristic_score(start_tile);
    u32 open_count = 1;

    UnitMovementPoint best_point = start_tile;
    u32 best_score = std::numeric_limits<u32>::max();
    bool reached_goal = false;

    do {
        u32 selected = 0;
        for (u32 index = 0; index < open_count; ++index) {
            if (open_scores[index] < open_scores[selected]) {
                selected = index;
            }
        }

        const UnitMovementPoint current = open_points[selected];
        const u16 current_score = open_scores[selected];
        if (current_score < best_score) {
            best_score = current_score;
            best_point = current;
        }

        --open_count;
        open_points[selected] = open_points[open_count];
        open_scores[selected] = open_scores[open_count];

        const u32 current_index = tile_point_index(context.map, current);
        const u16 next_distance =
            static_cast<u16>(distance[current_index] + 1u);

        for (const UnitMovementPoint& offset : kLegacyPathfinderNeighborOffsets) {
            const UnitMovementPoint next{current.x + offset.x, current.y + offset.y};
            if (tile_point_in_bounds(context.map, next)) {
                const u32 next_index = tile_point_index(context.map, next);
                if (next_index < storage_tiles && distance[next_index] == 0xffffu) {
                    // CheckLegacyPathfinderTileCanEnter increments the visit
                    // counter only for a previously unseen candidate.
                    ++checked_count;
                    if (check_pathfinder_tile_can_enter(context, unit, next)) {
                        distance[next_index] = next_distance;
                        open_points[open_count] = next;
                        open_scores[open_count] = heuristic_score(next);
                        ++open_count;

                        // The original breaks only this neighbor scan when the
                        // fixed array reaches 0x400 entries.  Subsequent outer
                        // iterations pop one entry and may append again.
                        if (open_count > 0x3ffu) {
                            break;
                        }
                    }
                }
            }

            // Goal recognition occurs after processing each neighbor, even
            // when that tile was already visited.  It is deliberately not a
            // test of the node selected at the top of the outer loop.
            if (next.x == resolved_goal.x && next.y == resolved_goal.y) {
                reached_goal = true;
                break;
            }
        }
    } while (checked_count <= visit_budget && !reached_goal && open_count != 0);
    g_legacy_pathfinder_scratch.checked_tile_count = checked_count;

    if (!reached_goal) {
        // A budget/open-list failure does not preserve the requested goal.
        // 0x00509471 marks it adjusted and replaces it with the strict-best
        // heuristic tile, which the caller writes back to unit.path_target.
        goal_adjusted = true;
        resolved_goal = best_point;
    }

    // The original has no parent array.  Starting at the reached/strict-best
    // tile, it scans N,E,S,W,NW,NE,SE,SW and repeatedly chooses the first
    // strict-minimum u16 visit distance.  The stored reverse path excludes the
    // goal and includes the start, up to 0x200 entries.
    std::vector<UnitMovementPoint> reverse_path;
    reverse_path.reserve(0x200);
    UnitMovementPoint cursor = resolved_goal;
    u16 lowest_distance = 0xffffu;
    bool reconstruction_failed = false;
    while (cursor.x != start_tile.x || cursor.y != start_tile.y) {
        UnitMovementPoint predecessor{};
        bool found_predecessor = false;
        for (const UnitMovementPoint& offset : kLegacyPathfinderNeighborOffsets) {
            const UnitMovementPoint candidate{
                cursor.x + offset.x, cursor.y + offset.y};
            if (!tile_point_in_bounds(context.map, candidate)) {
                continue;
            }
            const u32 candidate_index = tile_point_index(context.map, candidate);
            if (candidate_index >= storage_tiles) {
                continue;
            }
            const u16 candidate_distance = distance[candidate_index];
            if (candidate_distance < lowest_distance) {
                lowest_distance = candidate_distance;
                predecessor = candidate;
                found_predecessor = true;
            }
        }

        if (!found_predecessor || reverse_path.size() >= 0x200) {
            reconstruction_failed = true;
            break;
        }
        reverse_path.push_back(predecessor);
        cursor = predecessor;
        // 0x00509639 tests the 0x200 limit immediately after appending and
        // before the loop can observe that the last predecessor was start.
        if (reverse_path.size() >= 0x200) {
            reconstruction_failed = true;
            break;
        }
    }

    if (reconstruction_failed) {
        // This is the original 512-step overflow fallback: mark the goal as
        // adjusted to the start and set the direct-path flag, leaving the
        // caller's already-copied next_path coordinates untouched.
        goal_adjusted = true;
        resolved_goal = start_tile;
        apply_legacy_pathfinder_outputs(start_tile, start_tile, true);
        if (out_path_tiles != nullptr) {
            out_path_tiles->push_back(start_tile);
        }
        return false;
    }

    if (out_path_tiles != nullptr) {
        out_path_tiles->assign(reverse_path.rbegin(), reverse_path.rend());
        if (out_path_tiles->empty() ||
            out_path_tiles->back().x != resolved_goal.x ||
            out_path_tiles->back().y != resolved_goal.y) {
            out_path_tiles->push_back(resolved_goal);
        }
    }

    const bool reconstructed_waypoint = !reverse_path.empty();
    UnitMovementPoint waypoint = reconstructed_waypoint
        ? start_tile
        : g_legacy_pathfinder_scratch.waypoint_tile;
    for (std::size_t count = reverse_path.size(); count != 0;) {
        const UnitMovementPoint candidate = reverse_path[--count];
        if (!CheckStraightUnitPathTiles(context, unit, start_tile, candidate)) {
            break;
        }
        waypoint = candidate;
    }

    // RunLegacyUnitPathfinder 0x00509662 exits before DAT_0162fc90/94 when
    // strict-best is the start tile and the reconstructed reverse path has no
    // entries.  DAT_0162fc80 is still zero, so the caller writes the waypoint
    // left by an earlier pathfinder invocation into unit.next_path.
    apply_legacy_pathfinder_outputs(
        resolved_goal, waypoint, false, reconstructed_waypoint);
    return reached_goal;
}

void ProcessUnitPathfinderToDestination(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    if (context.map.width == 0 || context.map.height == 0) {
        return;
    }

    if (context.callbacks.run_pathfinder != nullptr) {
        context.callbacks.run_pathfinder(context, unit);
        return;
    }
    RunLegacyUnitPathfinder(context, unit);
}

u32 ProcessUnitPathToDestination(UnitMovementContext& context, UnitMovementUnit& unit) {
    if (context.map.width == 0 || context.map.height == 0) {
        return 0;
    }

    // Original 0x004c7440 preserves every coordinate inside the final map
    // tile (for a 96-wide map, 3040..3071).  Only a value at/after the world
    // extent is replaced with the final tile origin.  std::clamp to
    // width*32-32 incorrectly collapsed those valid sub-tile coordinates.
    const i32 world_width = static_cast<i32>(context.map.width * 32u);
    const i32 world_height = static_cast<i32>(context.map.height * 32u);
    if (unit.path_target_x >= world_width) {
        unit.path_target_x = world_width - 32;
    }
    if (unit.path_target_x < 0) {
        unit.path_target_x = 0;
    }
    if (unit.path_target_y >= world_height) {
        unit.path_target_y = world_height - 32;
    }
    if (unit.path_target_y < 0) {
        unit.path_target_y = 0;
    }
    unit.next_path_x = unit.path_target_x;
    unit.next_path_y = unit.path_target_y;

    if (unit.definition.movement_class != 3) {
        ProcessUnitPathfinderToDestination(context, unit);
        const u32 direction = CalculateUnitDirectionToPoint(unit, unit.next_path_x,
            unit.next_path_y);
        if (direction != 0) {
            unit.direction = direction;
            // Original ProcessUnitPathToDestination (0x004c7440) writes raw
            // +0xb4 = 4.  UpdateUnitMovementTowardPathTarget uses that same
            // field as its direction-turn timeout counter.
            unit.movement_turn_ticks = 4;
            unit.runtime_flags &= ~0x8u;
            const u32 command_metadata_flags =
                movement_command_metadata_flags(context, unit);
            if ((command_metadata_flags &
                    kUnitCommandMetadataPreserveAnimationFrame) == 0) {
                // Original ProcessUnitPathToDestination 0x004c7465..0x004c748b
                // consumes one gameplay RNG call with definition +0x2218 for
                // every successful ground path replan.
                unit.animation_frame = movement_random_limit(context,
                    unit.definition.movement_animation_frame_count);
            }
            unit.movement_state = 2;
            // Raw +0x110 is the movement step accumulator.  The original
            // resets it for every successful ground path replan at 0x004c74a0.
            unit.movement_step_accumulator = 0;
            if (context.callbacks.on_path_replanned != nullptr) {
                context.callbacks.on_path_replanned(context, unit);
            }
        }
        return direction;
    }

    const u32 command_metadata_flags =
        movement_command_metadata_flags(context, unit);
    std::optional<u32> random_animation_frame;
    if ((command_metadata_flags & kUnitCommandMetadataPreserveAnimationFrame) == 0) {
        random_animation_frame = movement_random_limit(context,
            unit.definition.movement_animation_frame_count);
    }
    ResetUnitMovementInterpolationState(
        unit, command_metadata_flags, random_animation_frame);
    return unit.type_id;
}

UnitMovementPoint CalculateUnitMovementFrameDelta(const UnitMovementUnit& unit) {
    const u32 direction = std::min<u32>(unit.direction, 8);
    const u32 frame = std::min<u32>(unit.animation_frame, 31);
    return unit.definition.frame_delta_by_direction[direction][frame];
}

UnitMovementPoint CalculateUnitMovementFrameDeltaForDirection(
    const UnitMovementUnit& unit, u32 direction) {
    const u32 clamped_direction = std::min<u32>(direction, 8);
    const u32 frame = std::min<u32>(unit.animation_frame, 31);
    return unit.definition.frame_delta_by_direction[clamped_direction][frame];
}

void StoreMovementFrameDeltaScratch(const UnitMovementUnit& unit, u32 direction,
    UnitMovementPoint& out_delta, u32& out_direction) {
    out_delta = CalculateUnitMovementFrameDeltaForDirection(unit, direction);
    out_direction = direction;
}

bool CheckUnitCanEnterTerrainCell(UnitMovementContext& context, const UnitMovementUnit& unit,
    i32 x, i32 y) {
    if (!in_world_bounds(context.map, x, y)) {
        return false;
    }
    if (context.callbacks.can_enter_cell != nullptr) {
        return context.callbacks.can_enter_cell(context, unit, x, y);
    }
    const UnitMovementCell* cell = GetMovementCell(context.map, world_to_tile(x),
        world_to_tile(y));
    if (cell == nullptr) {
        return false;
    }
    if (!context.map.legacy_entry_layers_present &&
        !movement_cell_has_legacy_entry_layers(*cell)) {
        return IsUnoccupiedPassableTerrainCell(*cell);
    }
    return legacy_movement_class_can_enter_cell(unit, *cell, true);
}

u32 ResolvePathDirectionOrReplan(UnitMovementContext& context, UnitMovementUnit& unit) {
    u32 direction = CalculateUnitDirectionToPoint(unit, unit.next_path_x,
        unit.next_path_y);
    if (direction != 0) {
        unit.direction = direction;
        if (CheckCurrentDirectionNextPathCell(context, unit)) {
            return direction;
        }
        ProcessUnitPathfinderToDestination(context, unit);
        direction = CalculateUnitDirectionToPoint(unit, unit.next_path_x,
            unit.next_path_y);
    }
    return direction;
}

u32 FindPassableDirectionRotatingLeft(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    u32 direction = unit.direction;
    if (unit.movement_step_accumulator != 0) {
        direction = rotate_direction8(direction, -1);
        --unit.movement_step_accumulator;
        if (unit.movement_step_accumulator != 0) {
            direction = rotate_direction8(direction, -1);
            --unit.movement_step_accumulator;
        }
    }

    for (u32 attempts = 0; attempts < 8; ++attempts) {
        if (direction_can_enter(context, unit, direction)) {
            if (unit.movement_step_accumulator == 0) {
                unit.movement_state = 2;
            }
            unit.direction = direction;
            return direction;
        }
        ++unit.movement_step_accumulator;
        direction = rotate_direction8(direction, 1);
    }
    return 0;
}

u32 FindPassableDirectionRotatingRight(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    u32 direction = unit.direction;
    if (unit.movement_step_accumulator != 0) {
        direction = rotate_direction8(direction, 1);
        --unit.movement_step_accumulator;
        if (unit.movement_step_accumulator != 0) {
            direction = rotate_direction8(direction, 1);
            --unit.movement_step_accumulator;
        }
    }

    for (u32 attempts = 0; attempts < 8; ++attempts) {
        if (direction_can_enter(context, unit, direction)) {
            if (unit.movement_step_accumulator == 0) {
                unit.movement_state = 2;
            }
            unit.direction = direction;
            return direction;
        }
        ++unit.movement_step_accumulator;
        direction = rotate_direction8(direction, -1);
    }
    return 0;
}

bool CheckCurrentDirectionNextPathCell(UnitMovementContext& context,
    const UnitMovementUnit& unit) {
    const UnitMovementPoint delta =
        unit.direction < kUnitMovementTileDirection8Deltas.size()
            ? kUnitMovementTileDirection8Deltas[unit.direction]
            : UnitMovementPoint{};
    return DispatchTerrainClassEntryProbe(context, unit, unit.current_cell_x + delta.x,
        unit.current_cell_y + delta.y);
}

bool DispatchTerrainProbeJumpTableEntry(UnitMovementContext& context,
    const UnitMovementUnit& unit, u32 movement_class, i32 x, i32 y) {
    if (movement_class != 0 && unit.definition.movement_class != movement_class) {
        return false;
    }
    return DispatchTerrainClassEntryProbe(context, unit, x, y);
}

bool DispatchTerrainClassEntryProbe(UnitMovementContext& context,
    const UnitMovementUnit& unit, i32 x, i32 y) {
    return DispatchTerrainClassEntryProbeDetailed(context, unit, x, y).status == 0;
}

UnitTerrainClassProbeResult DispatchTerrainClassEntryProbeDetailed(
    UnitMovementContext& context, const UnitMovementUnit& unit, i32 x, i32 y) {
    if (!in_world_bounds(context.map, x, y)) {
        return UnitTerrainClassProbeResult{1, nullptr};
    }

    const UnitMovementCell* base_cell = GetMovementCell(context.map, world_to_tile(x),
        world_to_tile(y));
    if (base_cell != nullptr &&
        (context.map.legacy_entry_layers_present ||
            movement_cell_has_legacy_entry_layers(*base_cell))) {
        if (!legacy_movement_class_can_enter_cell(unit, *base_cell, false)) {
            return UnitTerrainClassProbeResult{1, nullptr};
        }

        // Original FUN_004c8270 builds both collision rectangles from the
        // definition's raw +0x370..+0x37c fields.  Those are the interaction
        // bounds, not the larger raw +0x360 render/selection bounds.  The
        // distinction is observable when state 0x49 separates two workers:
        // their 15x18 interaction rectangles stop on an edge touch, while the
        // 28x37 bounds rectangles remain overlapped for several extra ticks.
        const UnitMovementRect probe_bounds =
            unit_interaction_bounds_rect_at(unit, x, y);
        if (unit.definition.movement_class == 3) {
            for (UnitMovementUnit* other : context.active_units) {
                if (other == nullptr || other == &unit) {
                    continue;
                }
                if (other->definition.movement_class == 3 &&
                    rects_overlap_strict(probe_bounds,
                        unit_interaction_bounds_rect(*other))) {
                    return UnitTerrainClassProbeResult{2, other};
                }
            }
            return UnitTerrainClassProbeResult{0, nullptr};
        }

        for (UnitMovementUnit* other : context.active_units) {
            if (other == nullptr || other == &unit) {
                continue;
            }
            if (other->definition.movement_class == 3 ||
                other->definition.movement_class == 1 ||
                (other->runtime_flags & 0x80u) != 0) {
                continue;
            }
            if (rects_overlap_strict(probe_bounds,
                    unit_interaction_bounds_rect(*other))) {
                return UnitTerrainClassProbeResult{2, other};
            }
        }
        return UnitTerrainClassProbeResult{0, nullptr};
    }

    const i32 left = x + unit.definition.bounds_left;
    const i32 top = y + unit.definition.bounds_top;
    const i32 right = left + std::max<i32>(unit.definition.bounds_width, 1) - 1;
    const i32 bottom = top + std::max<i32>(unit.definition.bounds_height, 1) - 1;
    for (i32 py = top; py <= bottom; py += 32) {
        for (i32 px = left; px <= right; px += 32) {
            if (!CheckUnitCanEnterTerrainCell(context, unit, px, py)) {
                return UnitTerrainClassProbeResult{1, nullptr};
            }
        }
    }
    return UnitTerrainClassProbeResult{0, nullptr};
}

void ProcessGroundUnitTerrainStep(UnitMovementContext& context, UnitMovementUnit& unit) {
    // The original common terrain pass is a short-range unit-separation pass.
    // It does not advance the command waypoint: it first queries the active-
    // command spatial index and does nothing when that query has no hit.
    UnitMovementUnit* target =
        context.callbacks.query_ground_separation_target != nullptr
            ? context.callbacks.query_ground_separation_target(context, unit)
            : nullptr;
    if (target == nullptr || target == &unit) {
        return;
    }
    if ((movement_command_metadata_flags(context, unit) & 1u) != 0) {
        return;
    }
    u32 direction = CalculateUnitDirectionToPoint(unit, target->x, target->y);
    if (direction == 0) {
        direction = movement_random_limit(context, 8) + 1;
    }
    const UnitMovementPoint delta = legacy_fallback_direction_delta(direction);
    const i32 next_x = unit.x + delta.x;
    const i32 next_y = unit.y + delta.y;
    if (!in_world_bounds(context.map, next_x, next_y)) {
        return;
    }
    const UnitMovementCell* cell = GetMovementCell(context.map, world_to_tile(next_x),
        world_to_tile(next_y));
    if (cell == nullptr ||
        !legacy_ground_fallback_cell_allows(context.map, unit, *cell)) {
        return;
    }
    unit.x = next_x;
    unit.y = next_y;
    // Original FUN_004c852e only applies the one-pixel separation to the
    // world coordinates.  The cached cell is owned by the main movement
    // step and deliberately remains unchanged here.
}

void ProcessAirUnitTerrainStep(UnitMovementContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target =
        context.callbacks.query_air_separation_target != nullptr
            ? context.callbacks.query_air_separation_target(context, unit)
            : nullptr;
    if (target == nullptr || target == &unit) {
        return;
    }
    u32 direction = CalculateUnitDirectionToPoint(unit, target->x, target->y);
    if (direction == 0) {
        direction = movement_random_limit(context, 8) + 1;
    }
    const UnitMovementPoint delta = legacy_fallback_direction_delta(direction);
    const i32 next_x = unit.x + delta.x;
    const i32 next_y = unit.y + delta.y;
    if (in_world_bounds(context.map, next_x, next_y)) {
        unit.x = next_x;
        unit.y = next_y;
        // FUN_004c8679 has the same contract for air separation: update the
        // world position, but do not cross/update the cached movement cell.
    }
}

void DrawMovementProbePixelAndPresent(UnitMovementContext& context,
    i32 x, i32 y, u16 color) {
    if (context.callbacks.on_debug_pixel_present != nullptr) {
        context.callbacks.on_debug_pixel_present(context, x, y, color);
    }
}

bool ProcessUnitMovementStep(UnitMovementContext& context, UnitMovementUnit& unit) {
    if ((unit.command_flags & 0x1000) != 0) {
        // FUN_004c756b routes this flag directly to FUN_004d0067.  That
        // helper changes raw state +0x60 to idle and returns the resulting
        // non-zero state to the caller; it is not a failed movement step.
        // Returning false here made guard-pursuit immediately replan toward
        // its moving target (replay 10, frame 9428) while the original
        // stopped in state 1 with the existing path tuple untouched.
        if (unit.command_state != 1) {
            unit.command_state = 1;
            if (unit.definition.animation_timer_period <= unit.animation_frame) {
                unit.animation_frame = 0;
            }
        }
        if (context.callbacks.on_reached_destination != nullptr) {
            context.callbacks.on_reached_destination(context, unit);
        }
        return unit.command_state != 0;
    }

    ++unit.animation_frame;
    if (unit.definition.animation_frame_count != 0 &&
        unit.animation_frame >= unit.definition.animation_frame_count) {
        unit.animation_frame = 0;
    }

    if (unit.definition.movement_class == 3) {
        UnitMovementCoreUpdateConfig config{};
        config.use_16_direction_lookup =
            unit.definition.use_16_direction_lookup &&
            context.direction_lookup_16 != nullptr;
        config.direction_lookup_8 = context.direction_lookup_8;
        config.direction_lookup_16 = context.direction_lookup_16;
        config.movement_period = unit.definition.movement_period;
        config.base_step_limit = unit.definition.movement_step_limit;
        config.additional_movement_modifier =
            movement_additional_modifier_for_unit(context, unit);
        config.equipment_catalog = context.equipment_catalog;
        config.map_width_tiles = context.map.width;
        config.map_height_tiles = context.map.height;
        const UnitMovementCoreUpdateResult result =
            UpdateUnitMovementTowardPathTarget(
                movement_production_state_or_empty(context), unit, config);
        // HandleUnitMovementTargetStepEntry (original 0x0040b450) returns the
        // current direction only while the movement-step accumulator remains
        // non-zero.  In particular, reaching the exact target drives the
        // accumulator to zero, clears the movement progress fields, and
        // returns zero so command state 2 can fall back to idle.  Treating
        // can_advance as success keeps class-3 units in travel forever because
        // the zero-direction interpolation branch itself is advanceable.
        return result.accumulator_active;
    }

    const u32 before = CalculateApproxUnitDistance(unit.x, unit.y, unit.next_path_x,
        unit.next_path_y);
    const UnitMovementPoint delta = movement_frame_delta_with_context(context, unit);
    const i32 next_x = unit.x + delta.x;
    const i32 next_y = unit.y + delta.y;
    const u32 after = CalculateApproxUnitDistance(next_x, next_y, unit.next_path_x,
        unit.next_path_y);
    if (before < after) {
        unit.next_path_x = unit.path_target_x;
        unit.next_path_y = unit.path_target_y;
        if (CalculateApproxUnitDistance(unit.x, unit.y, unit.next_path_x,
                unit.next_path_y) < 0x20) {
            return false;
        }
        return ProcessUnitPathToDestination(context, unit) != 0;
    }

    if (!in_world_bounds(context.map, next_x, next_y)) {
        return false;
    }

    if ((unit.x & ~0x1f) == (next_x & ~0x1f) &&
        (unit.y & ~0x1f) == (next_y & ~0x1f)) {
        unit.x = next_x;
        unit.y = next_y;
        return true;
    }

    const u32 direction = CalculateUnitDirectionToPoint(unit, unit.next_path_x,
        unit.next_path_y);
    if (direction == 0) {
        return false;
    }

    const UnitMovementPoint direction_delta =
        movement_frame_delta_for_direction_with_context(context, unit, direction);
    const i32 directed_x = unit.x + direction_delta.x;
    const i32 directed_y = unit.y + direction_delta.y;
    if (!CheckUnitCanEnterTerrainCell(context, unit, directed_x, directed_y)) {
        return ProcessBlockedMovementDirectionFallback(context, unit, direction);
    }

    unit.x = directed_x;
    unit.y = directed_y;
    unit.current_cell_x = directed_x & ~0x1f;
    unit.current_cell_y = directed_y & ~0x1f;
    unit.direction = direction;
    return true;
}

void ProcessUnitAnimationTimer(UnitMovementUnit& unit) {
    if (unit.definition.animation_timer_period == 0) {
        return;
    }
    ++unit.animation_timer;
    if (unit.animation_timer >= unit.definition.animation_timer_period) {
        unit.animation_timer = 0;
    }
}

void HandleUnitRuntimeDeathState(UnitMovementContext& context, UnitMovementUnit& unit) {
    unit.runtime_flags |= 4;
    unit.command_state &= ~0x40000000u;
    unit.animation_frame = 0;
    unit.draw_flags = 0;
    unit.command_value = 0;
    unit.target = nullptr;
    unit.path_target_x = 0;
    unit.command_flags &= 0xfffff7bf;
    unit.command_bits[0] &= static_cast<u8>(~0x80u);
    unit.runtime_flags &= 0xfffdef1d;
    if (context.callbacks.on_unit_marked_dead != nullptr) {
        context.callbacks.on_unit_marked_dead(context, unit);
    }
}

void HandleAttachedUnitParentDeath(UnitMovementContext& context, UnitMovementUnit& parent) {
    if ((parent.type_flags & 0x400u) == 0 || parent.cargo_amount == 0) {
        return;
    }
    for (UnitMovementUnit* unit : context.active_units) {
        if (unit == nullptr || unit == &parent) {
            continue;
        }
        if (unit->command_state == 0x45 && unit->target == &parent) {
            const bool released =
                context.callbacks.on_attached_child_parent_death != nullptr &&
                context.callbacks.on_attached_child_parent_death(context, parent, *unit);
            if (!released) {
                unit->x = parent.x;
                unit->y = parent.y;
                unit->current_cell_x = parent.x & ~0x1f;
                unit->current_cell_y = parent.y & ~0x1f;
            }
            unit->command_state |= kUnitCommandDead;
        }
    }
}

UnitMovementUnit* HandleFreeUnitActivation(UnitMovementContext& context) {
    if (context.free_units.empty()) {
        return nullptr;
    }
    UnitMovementUnit* unit = context.free_units.front();
    context.free_units.erase(context.free_units.begin());
    if (unit == nullptr) {
        return nullptr;
    }
    remove_unit_from_list(context.lifecycle_units, *unit);
    push_unit_front_unique(context.active_units, *unit);
    unit->active = true;
    return unit;
}

void HandleActiveUnitFreeListMove(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    remove_unit_from_list(context.active_units, unit);
    remove_unit_from_list(context.lifecycle_units, unit);
    push_unit_front_unique(context.free_units, unit);
    unit.active = false;
}

void HandleActiveUnitLifecycleListMove(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    remove_unit_from_list(context.active_units, unit);
    remove_unit_from_list(context.free_units, unit);
    push_unit_front_unique(context.lifecycle_units, unit);
    unit.active = false;
}

void HandleLifecycleUnitFreeListMove(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    remove_unit_from_list(context.lifecycle_units, unit);
    remove_unit_from_list(context.active_units, unit);
    push_unit_front_unique(context.free_units, unit);
    unit.active = false;
}

void HandleLifecycleUnitActiveListMove(UnitMovementContext& context,
    UnitMovementUnit& unit) {
    remove_unit_from_list(context.lifecycle_units, unit);
    remove_unit_from_list(context.free_units, unit);
    push_unit_front_unique(context.active_units, unit);
    unit.active = true;
}

void CopyCString(char* destination, const char* source) {
    if (destination == nullptr) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    for (;;) {
        const char ch = *source++;
        *destination++ = ch;
        if (ch == '\0') {
            return;
        }
    }
}

void CopyUnitStringSlotText(char* destination, std::size_t max_count,
    const char* source) {
    if (destination == nullptr || max_count == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    while (max_count != 0) {
        const char ch = *source++;
        *destination++ = ch;
        if (ch == '\0') {
            return;
        }
        --max_count;
    }
    *destination = '\0';
}

bool CompareUnitStringSlotText(const char* source, const char* destination) {
    if (source == nullptr || destination == nullptr) {
        return source == destination;
    }

    while (*source == *destination) {
        if (*source == '\0') {
            return true;
        }
        ++source;
        ++destination;
    }
    return false;
}

void AppendCString(char* destination, const char* source) {
    if (destination == nullptr) {
        return;
    }
    while (*destination != '\0') {
        ++destination;
    }
    CopyCString(destination, source);
}

void AppendBoundedCharacter(char* text, std::size_t max_count, char ch) {
    if (text == nullptr) {
        return;
    }

    std::size_t offset = 0;
    while (max_count != 0 && text[offset] != '\0') {
        --max_count;
        if (max_count == 0) {
            break;
        }
        ++offset;
    }
    text[offset] = ch;
    text[offset + 1] = '\0';
}

void RemoveTrailingCharacterRespectingDbcs(char* text, std::size_t max_count) {
    if (text == nullptr || max_count == 0) {
        return;
    }

    std::size_t offset = 0;
    while (offset < max_count && text[offset] != '\0') {
        ++offset;
    }
    if (offset == 0) {
        return;
    }

    const auto removed = static_cast<unsigned char>(text[offset - 1]);
    text[offset - 1] = '\0';
    if (offset >= 2 && removed >= 0x80u &&
        static_cast<unsigned char>(text[offset - 2]) >= 0x80u) {
        text[offset - 2] = '\0';
    }
}

u32 FindUnitStringSlot(const UnitMovementContext& context, const char* text) {
    if (text == nullptr || *text == '\0') {
        return kInvalidUnitStringSlot;
    }

    for (u32 slot = 1; slot < kUnitStringSlotCount; ++slot) {
        const auto& value = context.string_slots[slot];
        if (value[0] != '\0' && CompareUnitStringSlotText(text, value.data())) {
            return slot;
        }
    }
    return kInvalidUnitStringSlot;
}

u32 InternUnitStringSlot(UnitMovementContext& context, const char* text) {
    if (text == nullptr) {
        return kInvalidUnitStringSlot;
    }

    const u32 existing = FindUnitStringSlot(context, text);
    if (existing != kInvalidUnitStringSlot) {
        return existing;
    }

    for (u32 slot = 1; slot < kUnitStringSlotCount; ++slot) {
        auto& value = context.string_slots[slot];
        if (value[0] != '\0') {
            continue;
        }

        CopyUnitStringSlotText(value.data(), kUnitStringSlotBytes - 1, text);
        return slot;
    }
    return kInvalidUnitStringSlot;
}

void ClearUnitStringSlotIfUnused(UnitMovementContext& context, u32 slot_index) {
    if (slot_index >= kUnitStringSlotCount) {
        return;
    }

    auto slot_in_use = [slot_index](const std::vector<UnitMovementUnit*>& units) {
        return std::any_of(units.begin(), units.end(),
            [slot_index](const UnitMovementUnit* unit) {
                return unit != nullptr && unit->string_slot == slot_index;
            });
    };

    if (slot_in_use(context.active_units) || slot_in_use(context.lifecycle_units)) {
        return;
    }

    context.string_slots[slot_index][0] = '\0';
}

bool ProcessUnitRuntimeStateTick(UnitMovementContext& context, UnitMovementUnit& unit,
    UnitRuntimePreTerrainCallback pre_terrain, void* user_data) {
    if ((unit.draw_flags & 0x7f) != 0) {
        --unit.draw_flags;
    }
    if ((unit.runtime_flags & 0x0c00) != 0) {
        unit.runtime_flags -= 0x400;
    }

    if ((unit.command_state & kUnitCommandDead) != 0) {
        HandleUnitRuntimeDeathState(context, unit);
        return false;
    }

    if ((unit.runtime_flags & 0x20060) != 0) {
        return false;
    }

    if (pre_terrain != nullptr && !pre_terrain(context, unit, user_data)) {
        return false;
    }

    if (unit.definition.movement_class == 3) {
        ProcessAirUnitTerrainStep(context, unit);
    }
    else if (unit.definition.movement_class != 1) {
        ProcessGroundUnitTerrainStep(context, unit);
    }

    if ((unit.command_state & 0x40000000) != 0) {
        const u32 period = unit.definition.animation_timer_period;
        if (period != 0) {
            ++unit.animation_timer;
            if (unit.animation_timer >= period) {
                unit.animation_timer = 0;
            }
        }
        return false;
    }

    // Original ProcessUnitRuntimeStateTick (0x004c8ddc) consumes the same
    // post-impact recovery counter as the extended-unit dispatcher.  Without
    // this block units handled by the base runtime list attack only once and
    // remain locked on the target forever.
    if (unit.command_lockout_ticks != 0) {
        ProcessUnitAnimationTimer(unit);
        --unit.command_lockout_ticks;
        if (unit.command_lockout_ticks == 0) {
            unit.command_flags &= ~0x10u;
        }
    }

    if ((unit.runtime_flags & 0x80) != 0 && (unit.runtime_flags & 2) == 0 &&
        unit.command_state != 0x41) {
        return false;
    }
    return true;
}

} // namespace ranker
