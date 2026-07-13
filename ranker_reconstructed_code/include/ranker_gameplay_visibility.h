#pragma once

#include "ranker_player_slots.h"
#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <vector>

namespace ranker {

constexpr u32 kGameplayVisibilityTileCount = 0x10000;
constexpr u32 kGameplayVisibilityTileWidth = 0x100;
constexpr u32 kGameplayVisibilityRevealed = 0x08000000;
constexpr u32 kGameplayVisibilityVisible = 0x10000000;
constexpr u32 kGameplayVisibilityLocalMask = 0x18000000;
constexpr u32 kGameplayVisibilityOwnerLowMask = 0x000001ff;
constexpr u32 kGameplayVisibilityOwnerLayerHighMask = 0x01ff0000;
constexpr u32 kGameplayVisibilityOwnerLayerShift = 0x10;
constexpr u32 kGameplayVisibilityCurrentOwnerMask = 0x07fc0000;
constexpr u32 kGameplayVisibilityCurrentOwnerShift = 0x12;
constexpr u32 kGameplayVisibilityOwnerMask = kGameplayVisibilityOwnerLayerHighMask;
constexpr u32 kGameplayVisibilityAllOwnerMask = 0x01ff01ff;
constexpr u32 kGameplayVisibilityUnitHidden = 0x80;
constexpr u32 kGameplayVisibilityUnitDying = 0x40000000;
constexpr u32 kGameplayFogBlockPixels = 0x20;
constexpr u32 kGameplayFogMaskCount = 0x200;
constexpr u32 kGameplayFogMaskBytesPerBlock =
    kGameplayFogBlockPixels * kGameplayFogBlockPixels;
constexpr u32 kGameplayFogMaskTableBytes =
    kGameplayFogMaskCount * kGameplayFogMaskBytesPerBlock;

struct GameplayVisibilityGrid {
    u32 width = kGameplayVisibilityTileWidth;
    u32 height = kGameplayVisibilityTileWidth;
    std::vector<u32> current;
    std::vector<u32> previous;
    std::vector<u32> owner;
    std::vector<u32> terrain;
    std::vector<u32> terrain_class_flags;
    std::vector<u32> terrain_backup;
};

struct GameplayVisibilityUnit {
    u32 owner_id = 0;
    u32 type_id = 0;
    u32 variant = 0;
    u32 runtime_flags = 0;
    // Raw unit +0x60.  Lifecycle/death state is independent of the command
    // flags at raw +0x9c; combining them makes ordinary states such as 0x41
    // look like the special-visibility flag 0x40.
    u32 command_state = 0;
    // Raw unit +0x9c.  FUN_004d6cca callers use only bit 0x40 from this word
    // (or bit 0x80 from the command bitmap below) to request the extra
    // owner-relation/current-cell visibility gate.
    u32 command_flags = 0;
    std::array<u8, 32> command_bits{};
    u32 owner_visibility_mask = 0;
    u32 owner_explore_mask = 0;
    // Raw unit +0x98.  Lifecycle visibility uses this command/death phase
    // duration, not the combat HP fields at +0x10/+0x18.
    u32 command_entry_lockout_ticks = 0;
    u32 animation_timer = 0;
    i32 x = 0;
    i32 y = 0;
    i32 center_x = 0;
    i32 center_y = 0;
    i32 visibility_probe_x = 0;
    i32 visibility_probe_y = 0;
    i32 owner_layer_probe_x = 0;
    i32 owner_layer_probe_y = 0;
    i32 terrain_probe_x = 0;
    i32 terrain_probe_y = 0;
    u32 interaction_range_pixels = 0;
    u32 fallback_range_tiles = 5;
    u32 terrain_class = 7;
    u32 movement_class = 0;
    bool large_centered = false;
    bool is_structure = false;
    bool current_visibility_enabled = true;
    bool terrain_probe_valid = false;
};

struct GameplayVisibilityContext {
    GameplayVisibilityGrid* grid = nullptr;
    PlayerSlotRuntimeState* players = nullptr;
    std::vector<GameplayVisibilityUnit*> active_units;
    std::vector<GameplayVisibilityUnit*> lifecycle_units;
    u32 frame_counter = 0;
    u32 local_player_slot = 0;
    u32 current_mask_clear = 0xffffffff;
    u32 owner_mask_clear = 0xffffffff;
    bool fog_disabled = false;
};

struct GameplayFogRenderTarget {
    u16* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;
    u32 stride_words = 0;
};

struct GameplayFogRenderMetrics {
    u32 tile_columns = 0;
    u32 tile_rows = 0;
    i32 half_width = 0;
    i32 quarter_width = 0;
    i32 half_height = 0;
};

struct GameplayFogRenderContext {
    GameplayVisibilityGrid* grid = nullptr;
    GameplayFogRenderTarget target{};
    const u8* fog_mask_table = nullptr;
    std::size_t fog_mask_table_bytes = 0;
    i32 camera_x = 0;
    i32 camera_y = 0;
    GameplayFogRenderMetrics metrics{};
};

void UpdateGameplayVisibilityMap(GameplayVisibilityContext& context);
bool ShouldUpdateUnitVisibilityThisFrame(u32 frame_counter, u32 unit_index);
void ClearGameplayVisibilityTransientBits(GameplayVisibilityGrid& grid,
    u32 current_mask, u32 owner_mask);
void RevealAllGameplayVisibilityTiles(GameplayVisibilityGrid& grid);
void ClearGameplayVisibilityOwnerBits(GameplayVisibilityGrid& grid);
void ApplyActiveUnitVisibility(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit);
void ApplyUnitVisionFootprint(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit);
void ApplyLifecycleUnitVisibility(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit);
void ApplyLinkedUnitVisibilityFromSource(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& source, const GameplayVisibilityUnit& linked);
void MarkVisibilityTileAtRadius(GameplayVisibilityContext& context,
    const GameplayVisibilityUnit& unit, u32 radius, i32 tile_x, i32 tile_y);
u32 ResolveGameplayFogBlockMask(const GameplayVisibilityGrid& grid,
    i32 tile_x, i32 tile_y);
void PromoteGameplayFogVisibleTiles(
    GameplayVisibilityGrid& grid, bool require_current_visible);
void RenderGameplayFogOverlay(GameplayFogRenderContext& context);
void DrawGameplayFogBlock(
    GameplayFogRenderContext& context, u32 mask, i32 x, i32 y);
void ClearGameplayFogBlock(GameplayFogRenderTarget& target, i32 x, i32 y);
GameplayFogRenderMetrics RecalculateGameplayFogViewportMetrics(
    u32 width, u32 height);
bool LoadGameplayFogMaskTable(std::vector<u8>& table,
    const char* archive_name = "JW2_02.TRC", u32 record_index = 0x153);
constexpr bool HasUnitSpecialVisibilityGateBits(
    u32 command_flags, u8 first_command_bitmap_byte) {
    return (command_flags & 0x40) != 0 ||
        (first_command_bitmap_byte & 0x80) != 0;
}
bool CheckUnitVisibilityGateFlags(const GameplayVisibilityUnit& unit);
bool CheckUnitOwnerMaskOrCurrentVisibilityBit(const PlayerSlotRuntimeState& players,
    const GameplayVisibilityGrid& grid, const GameplayVisibilityUnit& unit,
    u32 bit_index);
bool CheckUnitOwnerLayerBitAtTarget(const GameplayVisibilityGrid& grid,
    const GameplayVisibilityUnit& unit, u32 bit_index);
u32 BuildUnitOwnerVisibilityMaskPair(const PlayerSlotRuntimeState& players,
    const GameplayVisibilityUnit& unit);

}
