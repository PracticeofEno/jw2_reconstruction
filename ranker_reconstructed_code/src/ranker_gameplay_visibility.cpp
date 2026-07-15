#include "ranker_gameplay_visibility.h"

#include "ranker_palette_cache.h"
#include "ranker_trc.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace ranker {
namespace {

bool ensure_grid(GameplayVisibilityGrid& grid) {
    const std::size_t count = static_cast<std::size_t>(grid.width) * grid.height;
    if (grid.width == 0 || grid.height == 0) {
        return false;
    }
    if (grid.current.size() < count) {
        grid.current.resize(count);
    }
    if (grid.previous.size() < count) {
        grid.previous.resize(count);
    }
    if (grid.owner.size() < count) {
        grid.owner.resize(count);
    }
    if (grid.terrain.size() < count) {
        grid.terrain.resize(count);
    }
    if (grid.terrain_class_flags.size() < count) {
        grid.terrain_class_flags.resize(count);
    }
    if (grid.terrain_backup.size() < count) {
        grid.terrain_backup.resize(count);
    }
    return true;
}

u32 tile_index(const GameplayVisibilityGrid& grid, i32 tile_x, i32 tile_y) {
    return static_cast<u32>(tile_y) * grid.width + static_cast<u32>(tile_x);
}

bool tile_in_bounds(const GameplayVisibilityGrid& grid, i32 tile_x, i32 tile_y) {
    return tile_x >= 0 && tile_y >= 0 &&
        static_cast<u32>(tile_x) < grid.width &&
        static_cast<u32>(tile_y) < grid.height;
}

bool grid_layer_ready(const GameplayVisibilityGrid& grid, const std::vector<u32>& layer) {
    if (grid.width == 0 || grid.height == 0) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(grid.width) * grid.height;
    return layer.size() >= count;
}

bool target_valid(const GameplayFogRenderTarget& target) {
    return target.pixels != nullptr && target.width != 0 && target.height != 0 &&
        target.stride_words != 0;
}

bool metrics_match_target(const GameplayFogRenderMetrics& metrics,
    const GameplayFogRenderTarget& target) {
    return metrics.tile_columns == ((target.width >> 5) + 1) &&
        metrics.tile_rows == ((target.height >> 5) + 1) &&
        metrics.half_width == (static_cast<i32>(target.width) >> 1) &&
        metrics.quarter_width == (static_cast<i32>(target.width) >> 2) &&
        metrics.half_height == (static_cast<i32>(target.height) >> 1);
}

u32 player_visibility_mask(const GameplayVisibilityContext& context, u32 owner) {
    if (context.players == nullptr || owner >= kPlayerSlotCount) {
        return 0;
    }
    return context.players->owner_visibility_masks[owner] & kGameplayVisibilityOwnerLowMask;
}

u32 local_player_bit(const GameplayVisibilityContext& context) {
    return context.local_player_slot < 32 ? (1u << context.local_player_slot) : 0;
}

u32 unit_center_tile_x(const GameplayVisibilityUnit& unit) {
    // FUN_004d5ccd starts mobile units at raw +0xc0 (the current-cell
    // coordinate).  Only type >= 0x60 switches to FUN_004c36de's bounds-
    // adjusted raw +0xb8 world position.
    const i32 world_x = unit.large_centered ? unit.center_x :
        (unit.terrain_probe_valid ? unit.terrain_probe_x : unit.x);
    return static_cast<u32>(world_x) >> 5;
}

u32 unit_center_tile_y(const GameplayVisibilityUnit& unit) {
    const i32 world_y = unit.large_centered ? unit.center_y :
        (unit.terrain_probe_valid ? unit.terrain_probe_y : unit.y);
    return static_cast<u32>(world_y) >> 5;
}

i32 unit_visibility_radius_tiles(const GameplayVisibilityUnit& unit) {
    if (unit.type_id > 0x5f && unit.variant == 1) {
        return 5;
    }
    // FUN_004d5ccd uses SAR ECX,5.  A negative stat is therefore a real
    // non-expanding radius, not a request for the reconstructed fallback.
    return static_cast<i32>(unit.interaction_range_pixels) >> 5;
}

u32 terrain_class_at(const GameplayVisibilityGrid& grid, i32 tile_x, i32 tile_y) {
    if (!tile_in_bounds(grid, tile_x, tile_y)) {
        return 7;
    }
    const u32 index = tile_index(grid, tile_x, tile_y);
    const u32 flags = index < grid.terrain_class_flags.size() ?
        grid.terrain_class_flags[index] : grid.terrain[index];
    return (flags & 0x1c000000) >> 26;
}

u32 terrain_class_for_visibility_unit(const GameplayVisibilityGrid& grid,
    const GameplayVisibilityUnit& unit) {
    if (unit.movement_class == 3) {
        return 7;
    }
    const i32 probe_x = unit.terrain_probe_valid ? unit.terrain_probe_x :
        (unit.large_centered ? unit.center_x : unit.x);
    const i32 probe_y = unit.terrain_probe_valid ? unit.terrain_probe_y :
        (unit.large_centered ? unit.center_y : unit.y);
    return terrain_class_at(grid, static_cast<i32>(static_cast<u32>(probe_x) >> 5),
        static_cast<i32>(static_cast<u32>(probe_y) >> 5));
}

u32 octile_half_distance(i32 dx, i32 dy) {
    u32 ax = static_cast<u32>(std::abs(dx));
    u32 ay = static_cast<u32>(std::abs(dy));
    if (ax < ay) {
        ax >>= 1;
    } else {
        ay >>= 1;
    }
    return ax + ay;
}

u32 owner_visibility_bits_from_mask(u32 mask) {
    const u32 low = mask & kGameplayVisibilityOwnerLowMask;
    if (low != 0) {
        return low;
    }

    const u32 owner_layer =
        (mask & kGameplayVisibilityOwnerLayerHighMask) >> kGameplayVisibilityOwnerLayerShift;
    if (owner_layer != 0) {
        return owner_layer;
    }

    return (mask & kGameplayVisibilityCurrentOwnerMask) >>
        kGameplayVisibilityCurrentOwnerShift;
}

u32 owner_visibility_bits_for_unit(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit) {
    u32 mask = owner_visibility_bits_from_mask(unit.owner_visibility_mask);
    if (mask == 0) {
        mask = player_visibility_mask(context, unit.owner_id);
    }
    return mask & kGameplayVisibilityOwnerLowMask;
}

u32 current_visibility_mask_for_unit(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit, bool include_owner_current_bits) {
    const u32 owner_bits = owner_visibility_bits_for_unit(context, unit);
    u32 mask = include_owner_current_bits && unit.current_visibility_enabled ?
        owner_bits << kGameplayVisibilityCurrentOwnerShift : 0;
    if ((owner_bits & local_player_bit(context)) != 0) {
        mask |= kGameplayVisibilityLocalMask;
    }
    return mask;
}

u32 owner_layer_mask_for_unit(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit) {
    u32 owner_bits = owner_visibility_bits_from_mask(unit.owner_explore_mask);
    if (owner_bits == 0) {
        owner_bits = owner_visibility_bits_for_unit(context, unit);
    }
    owner_bits &= kGameplayVisibilityOwnerLowMask;
    return (owner_bits << kGameplayVisibilityOwnerLayerShift) | owner_bits;
}

bool mark_visibility_tile_at_radius(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit, i32 radius, i32 tile_x, i32 tile_y) {
    if (context.grid == nullptr || !tile_in_bounds(*context.grid, tile_x, tile_y) ||
        terrain_class_at(*context.grid, tile_x, tile_y) > unit.terrain_class) {
        return false;
    }

    const i32 center_x = static_cast<i32>(unit_center_tile_x(unit));
    const i32 center_y = static_cast<i32>(unit_center_tile_y(unit));
    const u32 index = tile_index(*context.grid, tile_x, tile_y);
    if (static_cast<i32>(octile_half_distance(
            tile_x - center_x, tile_y - center_y)) >= radius) {
        context.grid->current[index] |=
            unit.owner_visibility_mask & kGameplayVisibilityVisible;
        return false;
    }

    context.grid->owner[index] |= unit.owner_explore_mask;
    context.grid->current[index] |= unit.owner_visibility_mask;
    if ((unit.owner_visibility_mask & kGameplayVisibilityRevealed) != 0) {
        context.grid->terrain_backup[index] = context.grid->terrain[index];
        context.grid->previous[index] &= 0x07fc0000;
        context.grid->previous[index] |=
            context.grid->current[index] & 0xf803ffff;
    }
    return true;
}

void apply_visibility_direction(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit, i32 radius, i32 tile_x, i32 tile_y,
    u32 direction) {
    if (!mark_visibility_tile_at_radius(
            context, unit, radius, tile_x, tile_y)) {
        return;
    }

    // FUN_004d5fc1's jump table expands one octant at a time.  Diagonal
    // entries also launch the two bordering axial rays before continuing the
    // diagonal.  A rejected terrain cell returns before this expansion, which
    // is what makes hills/terrain classes occlude cells behind them.
    switch (direction) {
    case 1: // north
        apply_visibility_direction(context, unit, radius,
            tile_x, tile_y - 1, 1);
        break;
    case 2: { // north-east
        const i32 next_x = tile_x + 1;
        const i32 next_y = tile_y - 1;
        if (!tile_in_bounds(*context.grid, next_x, next_y)) {
            return;
        }
        apply_visibility_direction(context, unit, radius,
            next_x, next_y - 1, 1);
        apply_visibility_direction(context, unit, radius,
            next_x + 1, next_y, 3);
        apply_visibility_direction(context, unit, radius,
            next_x, next_y, 2);
        break;
    }
    case 3: // east
        apply_visibility_direction(context, unit, radius,
            tile_x + 1, tile_y, 3);
        break;
    case 4: { // south-east
        const i32 next_x = tile_x + 1;
        const i32 next_y = tile_y + 1;
        if (!tile_in_bounds(*context.grid, next_x, next_y)) {
            return;
        }
        apply_visibility_direction(context, unit, radius,
            next_x + 1, next_y, 3);
        apply_visibility_direction(context, unit, radius,
            next_x, next_y + 1, 5);
        apply_visibility_direction(context, unit, radius,
            next_x, next_y, 4);
        break;
    }
    case 5: // south
        apply_visibility_direction(context, unit, radius,
            tile_x, tile_y + 1, 5);
        break;
    case 6: { // south-west
        const i32 next_x = tile_x - 1;
        const i32 next_y = tile_y + 1;
        if (!tile_in_bounds(*context.grid, next_x, next_y)) {
            return;
        }
        apply_visibility_direction(context, unit, radius,
            next_x, next_y + 1, 5);
        apply_visibility_direction(context, unit, radius,
            next_x - 1, next_y, 7);
        apply_visibility_direction(context, unit, radius,
            next_x, next_y, 6);
        break;
    }
    case 7: // west
        apply_visibility_direction(context, unit, radius,
            tile_x - 1, tile_y, 7);
        break;
    case 8: { // north-west
        const i32 next_x = tile_x - 1;
        const i32 next_y = tile_y - 1;
        if (!tile_in_bounds(*context.grid, next_x, next_y)) {
            return;
        }
        apply_visibility_direction(context, unit, radius,
            next_x - 1, next_y, 7);
        apply_visibility_direction(context, unit, radius,
            next_x, next_y - 1, 1);
        apply_visibility_direction(context, unit, radius,
            next_x, next_y, 8);
        break;
    }
    default:
        break;
    }
}

void apply_visibility_radii(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit, i32 radius) {
    if (context.grid == nullptr || !ensure_grid(*context.grid)) {
        return;
    }
    const i32 center_x = static_cast<i32>(unit_center_tile_x(unit));
    const i32 center_y = static_cast<i32>(unit_center_tile_y(unit));
    for (u32 direction = 1; direction < 9; ++direction) {
        apply_visibility_direction(
            context, unit, radius, center_x, center_y, direction);
    }
}

struct FogPixelMasks {
    u16 red_blue;
    u16 green;
    u16 sixteenth;
    u16 eighth;
    u16 fourth;
    u16 half;
};

FogPixelMasks current_fog_pixel_masks() {
    if (SurfacePixelMode555()) {
        return {0x7c1fu, 0x03e0u, 0x4210u, 0x6318u, 0x739cu, 0x7bdeu};
    }
    return {0xf81fu, 0x07e0u, 0x8610u, 0xc718u, 0xe79cu, 0xf7deu};
}

u16 apply_fog_factor_to_pixel(u16 pixel, u8 factor, bool preserve_factor_30,
    const FogPixelMasks& masks) {
    if (factor == 0) {
        return 0;
    }
    if (factor == 0x1f || (preserve_factor_30 && factor == 0x1e)) {
        return pixel;
    }

    switch (factor) {
    case 1:
    case 2:
        return static_cast<u16>((pixel & masks.sixteenth) >> 4);
    case 4:
        return static_cast<u16>((pixel & masks.eighth) >> 3);
    case 8:
        return static_cast<u16>((pixel & masks.fourth) >> 2);
    case 0x0f:
    case 0x10:
        return static_cast<u16>((pixel & masks.half) >> 1);
    default:
        return static_cast<u16>(
            ((((pixel & masks.red_blue) * factor) >> 5) & masks.red_blue) |
            ((((pixel & masks.green) * factor) >> 5) & masks.green));
    }
}

void write_clear_pixel(GameplayFogRenderTarget& target, i32 x, i32 y) {
    if (x < 0 || y < 0 || static_cast<u32>(x) >= target.width ||
        static_cast<u32>(y) >= target.height) {
        return;
    }
    target.pixels[static_cast<std::size_t>(y) * target.stride_words +
        static_cast<std::size_t>(x)] = 0;
}

u32 layer_bit_at(const GameplayVisibilityGrid& grid, const std::vector<u32>& layer,
    i32 world_x, i32 world_y, u32 bit_index) {
    if (bit_index >= 32 || !grid_layer_ready(grid, layer)) {
        return 0;
    }

    const i32 tile_x = static_cast<i32>(static_cast<u32>(world_x) >> 5);
    const i32 tile_y = static_cast<i32>(static_cast<u32>(world_y) >> 5);
    if (!tile_in_bounds(grid, tile_x, tile_y)) {
        return 0;
    }
    return layer[tile_index(grid, tile_x, tile_y)] & (1u << bit_index);
}

enum class FogTileClass : u8 {
    dark = 0,
    visible = 1,
    revealed = 2,
};

FogTileClass raw_fog_tile_class(
    const GameplayVisibilityGrid& grid, i32 tile_x, i32 tile_y) {
    if (!grid_layer_ready(grid, grid.current) ||
        !tile_in_bounds(grid, tile_x, tile_y)) {
        return FogTileClass::dark;
    }

    const u32 flags = grid.current[tile_index(grid, tile_x, tile_y)];
    if ((flags & kGameplayVisibilityRevealed) != 0) {
        return FogTileClass::revealed;
    }
    if ((flags & kGameplayVisibilityVisible) != 0) {
        return FogTileClass::visible;
    }
    return FogTileClass::dark;
}

FogTileClass smoothed_fog_tile_class(
    const GameplayVisibilityGrid& grid, i32 tile_x, i32 tile_y) {
    const FogTileClass tile_class =
        raw_fog_tile_class(grid, tile_x, tile_y);
    if (tile_class != FogTileClass::revealed) {
        return tile_class;
    }

    // FUN_00420370 first converts DAT_00758d40 bit 27/28 into classes
    // 2/1/0.  A class-2 tile touching any in-map class-0 neighbor is then
    // demoted to class 1 before FUN_004206e0 builds the 8-neighbor mask.
    // Omitting this pass left hard, square fully-lit edges in rebuilt fog.
    constexpr std::array<i32, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
    constexpr std::array<i32, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};
    for (std::size_t i = 0; i < kDx.size(); ++i) {
        const i32 neighbor_x = tile_x + kDx[i];
        const i32 neighbor_y = tile_y + kDy[i];
        if (tile_in_bounds(grid, neighbor_x, neighbor_y) &&
            raw_fog_tile_class(grid, neighbor_x, neighbor_y) ==
                FogTileClass::dark) {
            return FogTileClass::visible;
        }
    }
    return FogTileClass::revealed;
}

u32 neighbor_mask_for_fog_class(const GameplayVisibilityGrid& grid,
    i32 tile_x, i32 tile_y, FogTileClass expected_class) {
    constexpr std::array<i32, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
    constexpr std::array<i32, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};
    u32 mask = 0;
    for (u32 i = 0; i < kDx.size(); ++i) {
        const i32 x = tile_x + kDx[i];
        const i32 y = tile_y + kDy[i];
        if (tile_in_bounds(grid, x, y) &&
            smoothed_fog_tile_class(grid, x, y) == expected_class) {
            mask |= 1u << i;
        }
    }
    return mask;
}

u32 resolve_fog_mask_from_class_grid(const std::vector<u8>& classes,
    u32 storage_width, u32 storage_height, u32 neighbor_width,
    u32 neighbor_height, u32 x, u32 y) {
    if (x >= storage_width || y >= storage_height || classes.size() <
        static_cast<std::size_t>(storage_width) * storage_height) {
        return 0x100;
    }

    const auto class_at = [&classes, storage_width](u32 column, u32 row) {
        return static_cast<FogTileClass>(
            classes[static_cast<std::size_t>(row) * storage_width + column]);
    };
    const FogTileClass current = class_at(x, y);
    if (current == FogTileClass::revealed) {
        return 0xff;
    }

    constexpr std::array<i32, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
    constexpr std::array<i32, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};
    const auto neighbor_mask = [&](FogTileClass expected_class) {
        u32 mask = 0;
        for (u32 i = 0; i < kDx.size(); ++i) {
            const i32 neighbor_x = static_cast<i32>(x) + kDx[i];
            const i32 neighbor_y = static_cast<i32>(y) + kDy[i];
            if (neighbor_x >= 0 && neighbor_y >= 0 &&
                static_cast<u32>(neighbor_x) < neighbor_width &&
                static_cast<u32>(neighbor_y) < neighbor_height &&
                static_cast<u32>(neighbor_x) < storage_width &&
                static_cast<u32>(neighbor_y) < storage_height &&
                class_at(static_cast<u32>(neighbor_x),
                    static_cast<u32>(neighbor_y)) == expected_class) {
                mask |= 1u << i;
            }
        }
        return mask;
    };

    const u32 revealed_neighbors =
        neighbor_mask(FogTileClass::revealed);
    if (revealed_neighbors != 0) {
        return revealed_neighbors;
    }
    if (current == FogTileClass::visible) {
        return 0x1ff;
    }
    return 0x100 | neighbor_mask(FogTileClass::visible);
}

} // namespace

void UpdateGameplayVisibilityMap(GameplayVisibilityContext& context) {
    if (context.grid == nullptr || !ensure_grid(*context.grid)) {
        return;
    }

    if ((context.frame_counter & 0x3f) == 0) {
        ClearGameplayVisibilityTransientBits(
            *context.grid, context.current_mask_clear, context.owner_mask_clear);
    }

    for (GameplayVisibilityUnit* unit : context.active_units) {
        if (unit != nullptr && (unit->runtime_flags & kGameplayVisibilityUnitHidden) == 0) {
            ApplyActiveUnitVisibility(context, *unit);
        }
    }

    for (GameplayVisibilityUnit* unit : context.lifecycle_units) {
        if (unit != nullptr) {
            ApplyLifecycleUnitVisibility(context, *unit);
        }
    }
}

bool ShouldUpdateUnitVisibilityThisFrame(u32 frame_counter, u32 unit_index) {
    if ((frame_counter & 0x3f) == 0) {
        return true;
    }
    return ((unit_index / 0x1d0) & 0x0f) == (frame_counter & 0x0f);
}

void ClearGameplayVisibilityTransientBits(GameplayVisibilityGrid& grid,
    u32 current_mask, u32 owner_mask) {
    if (!ensure_grid(grid)) {
        return;
    }

    const std::size_t count = static_cast<std::size_t>(grid.width) * grid.height;
    for (std::size_t i = 0; i < count; ++i) {
        grid.current[i] &= current_mask;
        grid.previous[i] &= 0xf7ffffff;
        grid.owner[i] &= owner_mask;
    }
}

void RevealAllGameplayVisibilityTiles(GameplayVisibilityGrid& grid) {
    if (!ensure_grid(grid)) {
        return;
    }

    const std::size_t count = static_cast<std::size_t>(grid.width) * grid.height;
    for (std::size_t i = 0; i < count; ++i) {
        grid.current[i] |= kGameplayVisibilityVisible;
        grid.owner[i] |= kGameplayVisibilityOwnerLayerHighMask;
    }
}

void ClearGameplayVisibilityOwnerBits(GameplayVisibilityGrid& grid) {
    if (!ensure_grid(grid)) {
        return;
    }

    const std::size_t count = static_cast<std::size_t>(grid.width) * grid.height;
    for (std::size_t i = 0; i < count; ++i) {
        grid.current[i] &= 0xe7ffffff;
        grid.previous[i] &= 0xe7ffffff;
        grid.owner[i] &= 0xfe00fe00;
    }
}

void ApplyActiveUnitVisibility(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit) {
    if (context.grid == nullptr || !ensure_grid(*context.grid)) {
        return;
    }

    GameplayVisibilityUnit adjusted = unit;
    adjusted.owner_visibility_mask = current_visibility_mask_for_unit(context, unit, true);
    adjusted.owner_explore_mask = owner_layer_mask_for_unit(context, unit);
    adjusted.terrain_class = terrain_class_for_visibility_unit(*context.grid, unit);
    apply_visibility_radii(context, adjusted, unit_visibility_radius_tiles(adjusted));
}

void ApplyUnitVisionFootprint(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit) {
    if (context.grid == nullptr || !ensure_grid(*context.grid)) {
        return;
    }

    GameplayVisibilityUnit adjusted = unit;
    adjusted.owner_visibility_mask = current_visibility_mask_for_unit(context, unit, true);
    adjusted.owner_explore_mask = owner_layer_mask_for_unit(context, unit);
    adjusted.terrain_class = terrain_class_for_visibility_unit(*context.grid, unit);
    apply_visibility_radii(context, adjusted, unit_visibility_radius_tiles(adjusted));
}

void ApplyLifecycleUnitVisibility(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit) {
    if (context.grid == nullptr || !ensure_grid(*context.grid)) {
        return;
    }

    GameplayVisibilityUnit adjusted = unit;
    u32 radius = 5;
    if ((unit.command_state & kGameplayVisibilityUnitDying) != 0) {
        if ((unit.command_entry_lockout_ticks >> 1) <= unit.animation_timer) {
            return;
        }
        radius = (unit.animation_timer <
            (unit.command_entry_lockout_ticks >> 2)) ? 5u : 2u;
    }
    adjusted.owner_visibility_mask = current_visibility_mask_for_unit(context, unit, false);
    adjusted.owner_explore_mask = owner_layer_mask_for_unit(context, unit);
    adjusted.terrain_class = terrain_class_for_visibility_unit(*context.grid, unit);
    apply_visibility_radii(context, adjusted, radius);
}

void ApplyLinkedUnitVisibilityFromSource(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& source, const GameplayVisibilityUnit& linked) {
    GameplayVisibilityUnit adjusted = source;
    adjusted.owner_visibility_mask = current_visibility_mask_for_unit(context, linked, false);
    adjusted.owner_explore_mask = owner_layer_mask_for_unit(context, linked);
    adjusted.fallback_range_tiles = 2;
    adjusted.terrain_class = 7;
    adjusted.large_centered = source.type_id > 0x5f;
    apply_visibility_radii(context, adjusted, 2);
}

u32 ResolveGameplayFogBlockMask(const GameplayVisibilityGrid& grid,
    i32 tile_x, i32 tile_y) {
    if (!grid_layer_ready(grid, grid.current) || !tile_in_bounds(grid, tile_x, tile_y)) {
        return 0x100;
    }

    const FogTileClass current =
        smoothed_fog_tile_class(grid, tile_x, tile_y);
    if (current == FogTileClass::revealed) {
        return 0xff;
    }

    const u32 revealed_neighbors =
        neighbor_mask_for_fog_class(
            grid, tile_x, tile_y, FogTileClass::revealed);
    if (revealed_neighbors != 0) {
        return revealed_neighbors;
    }

    if (current == FogTileClass::visible) {
        return 0x1ff;
    }

    return 0x100 |
        neighbor_mask_for_fog_class(
            grid, tile_x, tile_y, FogTileClass::visible);
}

void PromoteGameplayFogVisibleTiles(
    GameplayVisibilityGrid& grid, bool require_current_visible) {
    if (!ensure_grid(grid)) {
        return;
    }

    const std::size_t count = static_cast<std::size_t>(grid.width) * grid.height;
    for (std::size_t i = 0; i < count; ++i) {
        if (!require_current_visible ||
            (grid.current[i] & kGameplayVisibilityVisible) != 0) {
            grid.current[i] |= kGameplayVisibilityLocalMask;
            grid.previous[i] |= kGameplayVisibilityLocalMask;
            grid.owner[i] |= kGameplayVisibilityAllOwnerMask;
        }
    }
}

void RenderGameplayFogOverlay(GameplayFogRenderContext& context) {
    if (context.grid == nullptr || !target_valid(context.target)) {
        return;
    }

    if (!metrics_match_target(context.metrics, context.target)) {
        context.metrics =
            RecalculateGameplayFogViewportMetrics(context.target.width, context.target.height);
    }

    const i32 start_tile_x = context.camera_x >= 0
        ? context.camera_x >> 5
        : (context.camera_x + 0x1f) >> 5;
    const i32 start_tile_y = context.camera_y >= 0
        ? context.camera_y >> 5
        : (context.camera_y + 0x1f) >> 5;
    const u32 class_columns = static_cast<u32>(
        (static_cast<u32>(context.camera_x +
            static_cast<i32>(context.target.width)) >> 5) -
        start_tile_x + 1);
    const u32 class_rows = static_cast<u32>(
        (static_cast<u32>(context.camera_y +
            static_cast<i32>(context.target.height)) >> 5) -
        start_tile_y + 1);
    std::vector<u8> fog_classes(
        static_cast<std::size_t>(class_columns) * class_rows, 0);
    for (u32 row = 0; row < class_rows; ++row) {
        for (u32 column = 0; column < class_columns; ++column) {
            fog_classes[static_cast<std::size_t>(row) * class_columns + column] =
                static_cast<u8>(smoothed_fog_tile_class(*context.grid,
                    start_tile_x + static_cast<i32>(column),
                    start_tile_y + static_cast<i32>(row)));
        }
    }

    i32 screen_y = -(context.camera_y & 0x1f);
    for (u32 row = 0; row < class_rows; ++row) {
        i32 screen_x = -(context.camera_x & 0x1f);
        for (u32 col = 0; col < class_columns; ++col) {
            const u32 mask = resolve_fog_mask_from_class_grid(
                fog_classes, class_columns, class_rows,
                context.metrics.tile_columns, context.metrics.tile_rows,
                col, row);
            if (mask == 0x100) {
                ClearGameplayFogBlock(context.target, screen_x, screen_y);
            } else if (mask != 0xff) {
                DrawGameplayFogBlock(context, mask, screen_x, screen_y);
            }
            screen_x += static_cast<i32>(kGameplayFogBlockPixels);
        }
        screen_y += static_cast<i32>(kGameplayFogBlockPixels);
    }
}

void DrawGameplayFogBlock(
    GameplayFogRenderContext& context, u32 mask, i32 x, i32 y) {
    if (mask == 0xff || !target_valid(context.target)) {
        return;
    }
    if (mask == 0x100) {
        ClearGameplayFogBlock(context.target, x, y);
        return;
    }

    const std::size_t offset =
        static_cast<std::size_t>(mask) * kGameplayFogMaskBytesPerBlock;
    if (context.fog_mask_table == nullptr ||
        offset + kGameplayFogMaskBytesPerBlock > context.fog_mask_table_bytes) {
        return;
    }

    GameplayFogRenderTarget& target = context.target;
    const u8* block = context.fog_mask_table + offset;
    // The surface pixel mode is stable for the duration of a block draw.
    // Querying DirectDraw state for every one of the 1024 pixels made the
    // software fog pass dominate the frame and delayed gameplay hotkeys and
    // edge scrolling even though the resulting pixels were identical.
    const FogPixelMasks pixel_masks = current_fog_pixel_masks();
    const bool fully_inside = x >= 0 && y >= 0 &&
        static_cast<u32>(x) + kGameplayFogBlockPixels <= target.width &&
        static_cast<u32>(y) + kGameplayFogBlockPixels <= target.height;

    if (fully_inside) {
        for (u32 row = 0; row < kGameplayFogBlockPixels; ++row) {
            const std::size_t base =
                static_cast<std::size_t>(y + static_cast<i32>(row)) *
                target.stride_words + static_cast<std::size_t>(x);
            u16* dst = target.pixels + base;
            const u8* src = block + static_cast<std::size_t>(row) *
                kGameplayFogBlockPixels;
            for (u32 col = 0; col < kGameplayFogBlockPixels; col += 2) {
                const u8 factor = src[col];
                dst[col] = apply_fog_factor_to_pixel(
                    dst[col], factor, true, pixel_masks);
                dst[col + 1] = apply_fog_factor_to_pixel(
                    dst[col + 1], factor, true, pixel_masks);
            }
        }
        return;
    }

    for (u32 row = 0; row < kGameplayFogBlockPixels; ++row) {
        const i32 py = y + static_cast<i32>(row);
        const u8* src = block + static_cast<std::size_t>(row) *
            kGameplayFogBlockPixels;
        if (py < 0 || static_cast<u32>(py) >= target.height) {
            continue;
        }
        u16* dst = target.pixels +
            static_cast<std::size_t>(py) * target.stride_words;
        for (u32 col = 0; col < kGameplayFogBlockPixels; ++col) {
            const i32 px = x + static_cast<i32>(col);
            if (px < 0 || static_cast<u32>(px) >= target.width) {
                continue;
            }
            dst[px] = apply_fog_factor_to_pixel(
                dst[px], src[col], false, pixel_masks);
        }
    }
}

void ClearGameplayFogBlock(GameplayFogRenderTarget& target, i32 x, i32 y) {
    if (!target_valid(target)) {
        return;
    }

    const bool fully_inside = x >= 0 && y >= 0 &&
        static_cast<u32>(x) + kGameplayFogBlockPixels <= target.width &&
        static_cast<u32>(y) + kGameplayFogBlockPixels <= target.height;
    if (fully_inside) {
        for (u32 row = 0; row < kGameplayFogBlockPixels; ++row) {
            const std::size_t base =
                static_cast<std::size_t>(y + static_cast<i32>(row)) *
                target.stride_words + static_cast<std::size_t>(x);
            std::fill_n(target.pixels + base, kGameplayFogBlockPixels,
                static_cast<u16>(0));
        }
        return;
    }

    for (u32 row = 0; row < kGameplayFogBlockPixels; ++row) {
        const i32 py = y + static_cast<i32>(row);
        for (u32 col = 0; col < kGameplayFogBlockPixels; ++col) {
            write_clear_pixel(target, x + static_cast<i32>(col), py);
        }
    }
}

GameplayFogRenderMetrics RecalculateGameplayFogViewportMetrics(
    u32 width, u32 height) {
    GameplayFogRenderMetrics metrics{};
    metrics.tile_columns = (width >> 5) + 1;
    metrics.tile_rows = (height >> 5) + 1;
    metrics.half_width = static_cast<i32>(width) >> 1;
    metrics.quarter_width = static_cast<i32>(width) >> 2;
    metrics.half_height = static_cast<i32>(height) >> 1;
    return metrics;
}

bool LoadGameplayFogMaskTable(std::vector<u8>& table,
    const char* archive_name, u32 record_index) {
    table.clear();
    // FUN_00420930 loads JW2_02.TRC records 0x153 and 0x154 into two
    // contiguous 0x40000-byte buffers.  Masks 0x000..0x0ff live in the
    // first record and masks 0x100..0x1ff (the explored/dark edge shapes)
    // live in the second.  Loading only 0x153 and zero-extending the vector
    // turns every dark-edge mask into a solid black 32x32 tile.
    constexpr std::size_t kRecordBytes =
        kGameplayFogMaskTableBytes / 2;
    std::vector<u8> first_record;
    std::vector<u8> second_record;
    if (!LoadTrcRecordAlloc(
            archive_name, record_index, first_record) ||
        !LoadTrcRecordAlloc(
            archive_name, record_index + 1, second_record) ||
        first_record.size() < kRecordBytes ||
        second_record.size() < kRecordBytes) {
        return false;
    }

    table.reserve(kGameplayFogMaskTableBytes);
    table.insert(table.end(), first_record.begin(),
        first_record.begin() + kRecordBytes);
    table.insert(table.end(), second_record.begin(),
        second_record.begin() + kRecordBytes);
    return true;
}

bool CheckUnitVisibilityGateFlags(const GameplayVisibilityUnit& unit) {
    return HasUnitSpecialVisibilityGateBits(
        unit.command_flags, unit.command_bits[0]);
}

bool CheckUnitOwnerMaskOrCurrentVisibilityBit(const PlayerSlotRuntimeState& players,
    const GameplayVisibilityGrid& grid, const GameplayVisibilityUnit& unit,
    u32 bit_index) {
    if (bit_index < 32 && unit.owner_id < kPlayerSlotCount &&
        (players.owner_visibility_masks[unit.owner_id] & (1u << bit_index)) != 0) {
        return true;
    }

    return layer_bit_at(grid, grid.current, unit.visibility_probe_x,
        unit.visibility_probe_y, bit_index + kGameplayVisibilityCurrentOwnerShift) != 0;
}

bool CheckUnitOwnerLayerBitAtTarget(const GameplayVisibilityGrid& grid,
    const GameplayVisibilityUnit& unit, u32 bit_index) {
    return layer_bit_at(grid, grid.owner, unit.owner_layer_probe_x,
        unit.owner_layer_probe_y, bit_index) != 0;
}

u32 BuildUnitOwnerVisibilityMaskPair(const PlayerSlotRuntimeState& players,
    const GameplayVisibilityUnit& unit) {
    if (unit.owner_id >= kPlayerSlotCount) {
        return 0;
    }
    const u32 mask =
        players.owner_visibility_masks[unit.owner_id] & kGameplayVisibilityOwnerLowMask;
    return (mask << kGameplayVisibilityOwnerLayerShift) | mask;
}

void MarkVisibilityTileAtRadius(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit, u32 radius, i32 tile_x, i32 tile_y) {
    if (context.grid != nullptr && ensure_grid(*context.grid)) {
        mark_visibility_tile_at_radius(context, unit,
            static_cast<i32>(radius), tile_x, tile_y);
    }
}

}
