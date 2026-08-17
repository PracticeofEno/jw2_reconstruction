#pragma once

#include "ranker_unit_damage.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

struct GameplayVisibilityGrid;

constexpr u32 kMapTileRevealed = 0x08000000;
constexpr u32 kMapTileVisible = 0x10000000;
constexpr u32 kMapTileFogMaskPreserve = 0x78000000;
constexpr u32 kUnitRenderHiddenFlag = 0x80;
constexpr u32 kRenderSortRowShift = 13;
constexpr std::size_t kUnitRenderDispatchCount = 0x100;

struct UnitRenderViewport {
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
};

struct UnitRenderItem {
    UnitRecord* unit = nullptr;
    u32 type_id = 0;
    u32 owner_id = 0;
    u32 runtime_slot_index = 0;
    u32 render_class = 0;
    u32 definition_kind = 0;
    u32 animation_flags = 0;
    u32 marker_flags = 0;
    u32 command_state = 0;
    u32 command_flags = 0;
    u32 command_bit_mask = 0;
    u32 command_value = 0;
    u32 previous_command_state = 0;
    u32 command_metadata_flags = 0;
    u32 definition_cell_flags = 0;
    u32 terrain_cell_flags = 0;
    u32 state_flags = 0;
    u32 runtime_flags = 0;
    u32 draw_flags = 0;
    u32 effect_state = 0;
    u32 animation_frame = 0;
    u32 animation_timer = 0;
    u32 command_entry_lockout_ticks = 0;
    u32 command_lockout_ticks = 0;
    u32 global_frame_counter = 0;
    i32 direction = 0;
    u32 cargo_amount = 0;
    u32 max_hit_points = 0;
    u32 hit_points = 0;
    u32 max_secondary_value = 0;
    u32 secondary_value = 0;
    // Raw +0x14 controls whether the secondary bar exists; its denominator
    // separately includes the owner/type production-effect slot.
    bool secondary_bar_enabled = true;
    u32 cell_animation_frame = 0;
    u32 cell_flag40_animation_frame = 0;
    u32 cell_channel_additive_frame = 0;
    u32 construction_stage_count = 0;
    u32 construction_progress = 0;
    u32 construction_progress_limit = 0;
    u32 low_health_overlay_frame = 0;
    u32 ability_id = 0;
    i32 x = 0;
    i32 y = 0;
    i32 visibility_cell_x = 0;
    i32 visibility_cell_y = 0;
    i32 center_offset_x = 0;
    i32 center_offset_y = 0;
    i32 center_width = 0;
    i32 center_height = 0;
    bool cell_construction_progress_active = false;
    bool cell_channel_additive_active = false;
    // The original name tail gates on raw unit +0x48, independently of the
    // bytes stored in that string slot.  An allocated empty slot must still
    // execute the font/color/cursor setup and empty text draw.
    bool display_name_slot_present = false;
    std::string display_name;
};

struct UnitRenderQueueEntry {
    UnitRecord* unit = nullptr;
    u32 type_id = 0;
    u32 render_class = 0;
    u32 layer = 0;
    u32 sort_key = 0;
};

struct UnitVisibilityGrid {
    u32 width = 0x100;
    u32 height = 0x100;
    bool require_revealed = false;
    std::vector<u32> cells;
    std::vector<u32>* fog_blocked_cells = nullptr;
    GameplayVisibilityGrid* authoritative_grid = nullptr;
};

struct UnitRenderQueueContext;

using UnitRenderCallback = void (*)(UnitRenderQueueContext& context,
    const UnitRenderItem& item);
using UnitRenderDispatchCallback = void (*)(UnitRenderQueueContext& context,
    const UnitRenderItem& item, i32 screen_x, i32 screen_y);

struct UnitRenderQueueCallbacks {
    UnitRenderCallback on_fog_blocked_unit = nullptr;
    UnitRenderCallback on_queue_entry = nullptr;
    std::array<UnitRenderDispatchCallback, kUnitRenderDispatchCount> dispatch_by_type{};
};

struct UnitRenderQueueContext {
    UnitRenderQueueCallbacks callbacks;
    UnitRenderViewport viewport;
    UnitVisibilityGrid visibility;
    std::vector<UnitRenderItem> units;
    std::vector<UnitRenderItem> effects;
    std::vector<UnitRenderQueueEntry> queued_entries;
    u32 local_owner_id = 0;
    bool local_owner_is_observer = false;
    std::array<u32, 8> owner_relation_masks{};
    std::array<u32, 8> owner_visibility_masks{};
    std::array<u32, 8> unit_layer_by_class{};
    std::array<u32, 8> unit_sort_bias_by_class{};
    std::array<u32, 8> effect_layer_by_class{};
    std::array<u32, 8> effect_sort_bias_by_class{};
};

bool IsUnitRenderItemInViewport(const UnitRenderViewport& viewport,
    const UnitRenderItem& item);
bool IsMapTileVisible(const UnitVisibilityGrid& grid, u32 tile_x, u32 tile_y);
bool IsUnitRenderItemIndividuallyVisibleToLocal(
    const UnitRenderQueueContext& context, const UnitRenderItem& item);
void ProcessVisibleUnitRenderQueue(UnitRenderQueueContext& context);
void ProcessVisibleEffectRenderQueue(UnitRenderQueueContext& context);
void DispatchUnitRenderByType(UnitRenderQueueContext& context, const UnitRenderItem& item,
    i32 screen_x, i32 screen_y);

}
