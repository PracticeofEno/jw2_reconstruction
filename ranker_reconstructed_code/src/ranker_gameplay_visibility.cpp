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
    const i32 world_x = unit.large_centered ? unit.center_x : unit.x;
    return static_cast<u32>(world_x) >> 5;
}

u32 unit_center_tile_y(const GameplayVisibilityUnit& unit) {
    const i32 world_y = unit.large_centered ? unit.center_y : unit.y;
    return static_cast<u32>(world_y) >> 5;
}

u32 unit_visibility_radius_tiles(const GameplayVisibilityUnit& unit) {
    if (unit.type_id > 0x5f && unit.variant == 1) {
        return 5;
    }
    if (unit.interaction_range_pixels != 0) {
        return std::max<u32>(1, unit.interaction_range_pixels >> 5);
    }
    return std::max<u32>(1, unit.fallback_range_tiles);
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
    return terrain_class_at(grid, static_cast<i32>(unit_center_tile_x(unit)),
        static_cast<i32>(unit_center_tile_y(unit)));
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

void apply_visibility_radii(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit, u32 radius) {
    const i32 center_x = static_cast<i32>(unit_center_tile_x(unit));
    const i32 center_y = static_cast<i32>(unit_center_tile_y(unit));
    const i32 span = static_cast<i32>(radius);
    for (i32 y = center_y - span; y <= center_y + span; ++y) {
        for (i32 x = center_x - span; x <= center_x + span; ++x) {
            MarkVisibilityTileAtRadius(context, unit, radius, x, y);
        }
    }
}

u16 apply_fog_factor_to_pixel(u16 pixel, u8 factor, bool preserve_factor_30) {
    if (factor == 0) {
        return 0;
    }
    if (factor == 0x1f || (preserve_factor_30 && factor == 0x1e)) {
        return pixel;
    }

    const bool pixel_mode_555 = SurfacePixelMode555();
    const u16 rb_mask = pixel_mode_555 ? 0x7c1fu : 0xf81fu;
    const u16 green_mask = pixel_mode_555 ? 0x03e0u : 0x07e0u;
    const u16 sixteenth_mask = pixel_mode_555 ? 0x4210u : 0x8610u;
    const u16 eighth_mask = pixel_mode_555 ? 0x6318u : 0xc718u;
    const u16 fourth_mask = pixel_mode_555 ? 0x739cu : 0xe79cu;
    const u16 half_mask = pixel_mode_555 ? 0x7bdeu : 0xf7deu;

    switch (factor) {
    case 1:
    case 2:
        return static_cast<u16>((pixel & sixteenth_mask) >> 4);
    case 4:
        return static_cast<u16>((pixel & eighth_mask) >> 3);
    case 8:
        return static_cast<u16>((pixel & fourth_mask) >> 2);
    case 0x0f:
    case 0x10:
        return static_cast<u16>((pixel & half_mask) >> 1);
    default:
        return static_cast<u16>(
            ((((pixel & rb_mask) * factor) >> 5) & rb_mask) |
            ((((pixel & green_mask) * factor) >> 5) & green_mask));
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

u32 neighbor_mask_for_flag(const GameplayVisibilityGrid& grid, i32 tile_x, i32 tile_y,
    u32 flag_mask) {
    if (!grid_layer_ready(grid, grid.current)) {
        return 0;
    }

    constexpr std::array<i32, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
    constexpr std::array<i32, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};
    u32 mask = 0;
    for (u32 i = 0; i < kDx.size(); ++i) {
        const i32 x = tile_x + kDx[i];
        const i32 y = tile_y + kDy[i];
        if (tile_in_bounds(grid, x, y) &&
            (grid.current[tile_index(grid, x, y)] & flag_mask) != 0) {
            mask |= (1u << i);
        }
    }
    return mask;
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
    if ((unit.state_flags & kGameplayVisibilityUnitDying) != 0) {
        if ((unit.max_health >> 1) <= unit.animation_timer) {
            return;
        }
        radius = (unit.animation_timer < (unit.max_health >> 2)) ? 5u : 2u;
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

    const u32 current = grid.current[tile_index(grid, tile_x, tile_y)];
    if ((current & kGameplayVisibilityRevealed) != 0) {
        return 0xff;
    }

    const u32 revealed_neighbors =
        neighbor_mask_for_flag(grid, tile_x, tile_y, kGameplayVisibilityRevealed);
    if (revealed_neighbors != 0) {
        return revealed_neighbors;
    }

    if ((current & kGameplayVisibilityVisible) != 0) {
        return 0x1ff;
    }

    return 0x100 |
        neighbor_mask_for_flag(grid, tile_x, tile_y, kGameplayVisibilityLocalMask);
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

    i32 tile_y = context.camera_y >> 5;
    i32 screen_y = -(context.camera_y & 0x1f);
    for (u32 row = 0; row < context.metrics.tile_rows; ++row) {
        i32 tile_x = context.camera_x >> 5;
        i32 screen_x = -(context.camera_x & 0x1f);
        for (u32 col = 0; col < context.metrics.tile_columns; ++col) {
            const u32 mask = ResolveGameplayFogBlockMask(*context.grid, tile_x, tile_y);
            if (mask == 0x100) {
                ClearGameplayFogBlock(context.target, screen_x, screen_y);
            } else if (mask != 0xff) {
                DrawGameplayFogBlock(context, mask, screen_x, screen_y);
            }
            ++tile_x;
            screen_x += static_cast<i32>(kGameplayFogBlockPixels);
        }
        ++tile_y;
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
                dst[col] = apply_fog_factor_to_pixel(dst[col], factor, true);
                dst[col + 1] = apply_fog_factor_to_pixel(dst[col + 1], factor, true);
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
            dst[px] = apply_fog_factor_to_pixel(dst[px], src[col], false);
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
    if (!LoadTrcRecordAlloc(archive_name, record_index, table)) {
        return false;
    }
    if (table.size() < kGameplayFogMaskTableBytes) {
        table.resize(kGameplayFogMaskTableBytes);
    }
    return true;
}

bool CheckUnitVisibilityGateFlags(const GameplayVisibilityUnit& unit) {
    return (unit.state_flags & 0x40) != 0 ||
        (unit.command_bits[0] & 0x80) != 0;
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
    if (context.grid == nullptr || !ensure_grid(*context.grid) ||
        !tile_in_bounds(*context.grid, tile_x, tile_y)) {
        return;
    }

    const u32 terrain_class = terrain_class_at(*context.grid, tile_x, tile_y);
    if (terrain_class > unit.terrain_class) {
        return;
    }

    const i32 center_x = static_cast<i32>(unit_center_tile_x(unit));
    const i32 center_y = static_cast<i32>(unit_center_tile_y(unit));
    if (octile_half_distance(tile_x - center_x, tile_y - center_y) >= radius) {
        const u32 index = tile_index(*context.grid, tile_x, tile_y);
        context.grid->current[index] |= unit.owner_visibility_mask & kGameplayVisibilityVisible;
        return;
    }

    const u32 index = tile_index(*context.grid, tile_x, tile_y);
    context.grid->owner[index] |= unit.owner_explore_mask;
    context.grid->current[index] |= unit.owner_visibility_mask;
    if ((unit.owner_visibility_mask & kGameplayVisibilityRevealed) != 0) {
        context.grid->terrain_backup[index] = context.grid->terrain[index];
        context.grid->previous[index] &= 0x07fc0000;
        context.grid->previous[index] |= context.grid->current[index] & 0xf803ffff;
    }
}

}
