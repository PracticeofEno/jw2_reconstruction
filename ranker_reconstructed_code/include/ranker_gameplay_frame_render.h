#pragma once

#include "ranker_gameplay_sound.h"
#include "ranker_gameplay_visibility.h"
#include "ranker_sprite_renderer.h"
#include "ranker_unit_render_queue.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

constexpr std::size_t kGameplayFrameAnimationSlotCount = 10;
constexpr std::size_t kGameplayTimedHudNotificationCount = 5;
constexpr std::size_t kGameplayHudAlertMarkerCount = 16;
constexpr std::size_t kGameplayRenderCommandCapacity = 3000;
constexpr std::size_t kGameplayRenderCommandDispatchCount = 16;
constexpr u32 kGameplayHudMessageLifetimeMs = 5000;
constexpr u32 kGameplayHudScrollStepDelayMs = 10;
// FUN_004d7919 and the message enqueue helper at 0x004d7c1b both select
// draw-font slot four before measuring or drawing gameplay HUD text.
constexpr u8 kGameplayHudTextDrawFontIndex = 4;
constexpr u32 kGameplayRenderPackedTypeMask = 0x000000ff;
constexpr u32 kGameplayRenderPackedPaletteRampMask = 0x00000f00;
constexpr u32 kGameplayRenderPackedOverlayMask = 0x0003f000;
constexpr u32 kGameplayRenderPackedBlendMask = 0x07c00000;
constexpr u32 kGameplayRenderPackedRawUnitRamp = 0x80000000;

// The original keeps two different vertical layout values for gameplay HUD
// messages.  DAT_01440004 is the full logical back-buffer height and is used
// as the queued message's off-screen/scroll-in origin.  DAT_0086358c is the
// theme-specific world viewport boundary and is used as the final baseline.
// Falling back to the full height keeps standalone/test states compatible.
constexpr u32 ResolveGameplayHudViewportBoundary(
    u32 screen_height, u32 world_viewport_height) {
    return world_viewport_height != 0 ? world_viewport_height : screen_height;
}

constexpr i32 ResolveGameplayHudViewportOffsetY(
    u32 screen_height, u32 world_viewport_height, i32 offset) {
    return static_cast<i32>(ResolveGameplayHudViewportBoundary(
               screen_height, world_viewport_height)) + offset;
}

constexpr i32 ResolveGameplayHudBottomTextY(
    u32 screen_height, u32 world_viewport_height) {
    return ResolveGameplayHudViewportOffsetY(
        screen_height, world_viewport_height, -0x14);
}

constexpr i32 ResolveGameplayHudQueuedMessageStartY(
    u32 screen_height, u32 text_height) {
    return static_cast<i32>(screen_height) -
        static_cast<i32>(text_height) * 2;
}

constexpr i32 ResolveGameplayHudCenteredTextX(
    u32 screen_width, u32 text_width) {
    // 0x004d7c3f..0x004d7c4b performs unsigned SUB/SHR without clamping.
    return static_cast<i32>((screen_width - text_width) >> 1);
}

enum class GameplayRenderSpriteVariant : u32 {
    unit_ramp_token1_shadow = 0,
    unit_ramp_low_blue_mask = 1,
    grayscale = 2,
    high_green_mask = 3,
    high_blue_mask = 4,
    high_red_mask = 5,
    half_blend = 6,
    channel_additive_tint = 7,
};

struct GameplayTextExtent {
    u32 width = 0;
    u32 height = 0;
};

struct GameplayHudTextState;
struct GameplayPlayerResourceHudState;
struct GameplayFrameRenderContext;
struct GameplayRenderCommand;
struct GameplayRenderCommandQueue;
struct UnitEffectRuntime;
struct UnitEffectRuntimeState;

using GameplayHudSelectFontCallback = void (*)(GameplayHudTextState& state);
using GameplayHudMeasureTextCallback = GameplayTextExtent (*)(
    GameplayHudTextState& state, const char* text);
using GameplayHudDrawTextCallback = void (*)(
    GameplayHudTextState& state, const char* text, i32 x, i32 y, u8 color);
using GameplayHudNoArgCallback = void (*)(GameplayHudTextState& state);
using GameplayPlayerResourceHudSpriteCallback = void (*)(
    GameplayPlayerResourceHudState& state, u32 sprite_entry, i32 x, i32 y, u32 player);
using GameplayPlayerResourceHudTextCallback = void (*)(
    GameplayPlayerResourceHudState& state, const char* text, i32 x, i32 y,
    u8 color, bool centered);
using GameplayPlayerResourceHudSelectFontCallback = void (*)(
    GameplayPlayerResourceHudState& state, u32 player);
using GameplayPlayerResourceHudMeasureTextCallback = GameplayTextExtent (*)(
    GameplayPlayerResourceHudState& state, const char* text);

struct GameplayHudCallbacks {
    GameplayHudSelectFontCallback select_draw_font = nullptr;
    GameplayHudSelectFontCallback select_metric_font = nullptr;
    GameplayHudMeasureTextCallback measure_text = nullptr;
    GameplayHudDrawTextCallback draw_shadow_and_advance = nullptr;
    GameplayHudDrawTextCallback draw_text = nullptr;
    GameplayHudDrawTextCallback draw_centered_text = nullptr;
    GameplayHudNoArgCallback flush_status_tail = nullptr;
};

enum class GameplayPlayerResourceHudDrawKind : u8 {
    sprite = 0,
    text = 1,
};

enum GameplayPlayerResourceHudFlags : u32 {
    kGameplayPlayerResourceHudPrimary = 0x01,
    kGameplayPlayerResourceHudPopulation = 0x02,
    kGameplayPlayerResourceHudPlayerIcon = 0x04,
    kGameplayPlayerResourceHudName = 0x08,
    kGameplayPlayerResourceHudUnitCount = 0x10,
    kGameplayPlayerResourceHudScore = 0x20,
    kGameplayPlayerResourceHudRotationCountdown = 0x40,
    kGameplayPlayerResourceHudIncludePlayerControlled = 0x80,
};

struct GameplayPlayerResourceHudCallbacks {
    GameplayPlayerResourceHudSelectFontCallback select_font = nullptr;
    GameplayPlayerResourceHudSelectFontCallback select_name_font = nullptr;
    GameplayPlayerResourceHudMeasureTextCallback measure_text = nullptr;
    GameplayPlayerResourceHudSpriteCallback draw_sprite = nullptr;
    GameplayPlayerResourceHudTextCallback draw_text = nullptr;
};

struct GameplayPlayerResourceHudPlayer {
    u8 slot_state = 0;
    std::string name;
    i32 primary_resource = 0;
    i32 population_current = 0;
    i32 population_display = 0;
    i32 population_cap = 0;
    i32 active_unit_count = 0;
    i32 queued_unit_count = 0;
    i32 score = 0;
};

struct GameplayPlayerResourceHudDrawRequest {
    GameplayPlayerResourceHudDrawKind kind = GameplayPlayerResourceHudDrawKind::text;
    u32 player = 0;
    u32 sprite_entry = 0;
    std::string text;
    i32 x = 0;
    i32 y = 0;
    u8 color = 1;
    bool centered = false;
};

struct GameplayPlayerResourceHudState {
    GameplayPlayerResourceHudCallbacks callbacks;
    std::array<GameplayPlayerResourceHudPlayer, 8> players{};
    std::vector<GameplayPlayerResourceHudDrawRequest> draw_requests;
    u32 flags = 0;
    i32 start_x = 0;
    i32 start_y = 0;
    u32 primary_resource_icon = 0;
    u32 population_icon = 0;
    u32 player_icon_base = 0;
    u32 rotation_countdown_ticks = 0;
    i32 rotation_countdown_x = 0;
    i32 rotation_countdown_y = 0;
    u8 rotation_countdown_color = 1;
    u8 normal_color = 1;
    u8 warning_color = 9;
    u8 capped_color = 0x11;
};

struct GameplayHudMessage {
    const char* text = nullptr;
    i32 x = 0;
    i32 y = 0;
    u32 tick_ms = 0;
};

struct GameplayHudSelectedStatus {
    bool active = false;
    u8 category = 0;
    std::array<u8, 8> category_colors{};
    std::array<const char*, 8> category_labels{};
    std::string typed_text;
    std::string extra_text;
    bool extra_text_active = false;
    const char* blink_text = "_";
};

struct GameplayTimedHudNotification {
    bool active = false;
    u32 expires_tick_ms = 0;
    u8 primary_color = 1;
    u8 secondary_color = 1;
    std::string primary_text;
    std::string secondary_text;
};

struct GameplayHudAlertMarker {
    bool active = false;
    u32 kind = 0;
    i32 world_x = 0;
    i32 world_y = 0;
    i32 screen_x = 0;
    i32 screen_y = 0;
    u32 animation_frame = 0;
    u32 remaining_ticks = 0x50;
    u32 last_frame_counter = 0;
};

struct GameplayHudAlertMarkerDraw {
    u32 sprite_entry = 0;
    u32 palette_selector = 0;
    i32 x = 0;
    i32 y = 0;
};

struct GameplayHudAlertMarkerState {
    std::array<GameplayHudAlertMarker, kGameplayHudAlertMarkerCount> markers{};
    std::vector<GameplayHudAlertMarkerDraw> draw_requests;
    u32 frame_counter = 0;
    u32 sprite_base_entry = 0;
    u32 marker_lifetime_ticks = 0x50;
    u32 duplicate_radius = 0x1e0;
    i32 minimap_x = 0;
    i32 minimap_y = 0;
    u32 minimap_width = 1;
    u32 minimap_height = 1;
    u32 map_width_tiles = 1;
    u32 map_height_tiles = 1;
    i32 last_alert_world_x = 0;
    i32 last_alert_world_y = 0;
    bool last_alert_valid = false;
};

struct GameplayDebugCounterState {
    bool enabled = false;
    u32 tick_last_sample_ms = 0;
    u32 tick_current_count = 0;
    u32 tick_previous_count = 0;
    u32 render_last_sample_ms = 0;
    u32 render_current_count = 0;
    u32 render_previous_count = 0;
    i32 x = 0;
    i32 y = 10;
};

struct GameplayHudTextState {
    GameplayHudCallbacks callbacks;
    u32 screen_width = 800;
    u32 screen_height = 600;
    // Mirrors DAT_0086358c.  This is not the full screen height: it is the
    // top-level world/HUD boundary selected by resolution bucket and theme.
    u32 world_viewport_height = 0;
    u32 frame_counter = 0;
    u32 current_tick_ms = 0;
    i32 bottom_text_y = 0;
    bool bottom_left_text_suppressed = false;
    GameplayHudSelectedStatus selected_status;
    GameplayHudMessage current_message;
    GameplayHudMessage queued_message;
    // Script opcode 0x59 draws directly during the current frame.  Keep it
    // separate from the timed HUD message queue so it neither scrolls nor
    // replaces a player-facing notification.
    GameplayHudMessage frame_message;
    std::array<GameplayTimedHudNotification, kGameplayTimedHudNotificationCount>
        timed_notifications{};
    GameplayDebugCounterState debug_counter;
    GameplayHudAlertMarkerState* alert_markers = nullptr;
};

constexpr bool GameplayHudSurfaceLayoutChanged(u32 current_screen_width,
    u32 current_screen_height, u32 current_world_viewport_height,
    u32 next_screen_width, u32 next_screen_height,
    u32 next_world_viewport_height) {
    return current_screen_width != next_screen_width ||
        current_screen_height != next_screen_height ||
        current_world_viewport_height != next_world_viewport_height;
}

inline bool UpdateGameplayHudSurfaceLayoutMetrics(GameplayHudTextState& state,
    u32 screen_width, u32 screen_height, u32 world_viewport_height) {
    const bool changed = GameplayHudSurfaceLayoutChanged(state.screen_width,
        state.screen_height, state.world_viewport_height, screen_width,
        screen_height, world_viewport_height);
    state.screen_width = screen_width;
    state.screen_height = screen_height;
    state.world_viewport_height = world_viewport_height;
    return changed;
}

using GameplayFrameCallback = void (*)(GameplayFrameRenderContext& context);
using GameplayFrameDrawTerrainCallback =
    void (*)(GameplayFrameRenderContext& context, i32 camera_x, i32 camera_y);
using GameplayRenderCommandCallback =
    bool (*)(GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);

struct GameplayFrameCallbacks {
    GameplayFrameDrawTerrainCallback draw_terrain = nullptr;
    GameplayFrameCallback prepare_visible_runtime_resources = nullptr;
    GameplayFrameCallback mirror_visible_map_effects = nullptr;
    GameplayFrameCallback draw_terrain_decorations = nullptr;
    GameplayFrameCallback draw_map_brushes = nullptr;
    GameplayFrameCallback draw_first_overlay = nullptr;
    GameplayFrameCallback draw_second_overlay = nullptr;
    GameplayFrameCallback draw_third_overlay = nullptr;
    GameplayFrameCallback draw_system_hud = nullptr;
    GameplayFrameCallback draw_resource_hud = nullptr;
    GameplayFrameCallback publish_present_flag = nullptr;
    GameplayFrameCallback draw_ui_overlay = nullptr;
    GameplayFrameCallback show_pause_overlay = nullptr;
    GameplayFrameCallback present_cursor = nullptr;
};

struct GameplayRenderOverlaySprite {
    u32 entry_offset = 0;
    i32 x_offset = 0;
    i32 y_offset = 0;
};

struct GameplayRenderUnitSpriteDefinition {
    bool has_special_draw = false;
    u32 blit_mode = 0;
    i32 center_offset_x = 0;
    i32 center_offset_y = 0;
    i32 center_width = 0;
    i32 center_height = 0;
    std::vector<GameplayRenderOverlaySprite> overlays;
};

struct GameplayRenderCommand {
    u32 class_id = 0;
    u32 payload = 0;
    u32 sort_key = 0;
    u32 sprite_entry_index = 0;
    u32 sprite_draw_mode = 0;
    i32 screen_y = 0;
    i32 screen_x = 0;
    u32 packed_flags = 0;
    bool sprite_draw_mode_valid = false;
    UnitRenderQueueContext* unit_render_context = nullptr;
    const UnitRenderItem* unit_render_item = nullptr;
    UnitEffectRuntimeState* effect_runtime_context = nullptr;
    UnitEffectRuntime* effect_runtime = nullptr;
    GameplayRenderSpriteVariant draw_variant =
        GameplayRenderSpriteVariant::unit_ramp_token1_shadow;
};

struct GameplayRenderCommandCallbacks {
    std::array<GameplayRenderCommandCallback, kGameplayRenderCommandDispatchCount>
        dispatch_by_class{};
    GameplayRenderCommandCallback default_dispatch = nullptr;
    GameplayRenderCommandCallback highbit_special_overlay = nullptr;
};

struct GameplayRenderCommandQueue {
    GameplayRenderCommandCallbacks callbacks;
    std::vector<GameplayRenderCommand> commands;
    std::vector<std::size_t> sorted_indices;
    std::vector<u32> definition_index_by_type;
    std::vector<GameplayRenderUnitSpriteDefinition> unit_sprite_definitions;
    u32 overlay_base_entry = 0;
    u32 highbit_overlay_base = 0;
    u32 highbit_overlay_stride = 1;
    u32 highbit_overlay_blit_mode = 0;
    bool sorted = false;
};

struct GameplayFrameRandomResult {
    u32 preserved_value = 0;
    u32 selected_value = 0;
};

struct GameplayFrameRandomState {
    u32 limit = 0;
    u32 seed = 0;
    u32 call_count = 0;
    std::array<u32, 16> scramble{
        0xa075a321u, 0xb304323cu, 0xc43a2059u, 0xd3d4f745u,
        0xe9999996u, 0xf124e654u, 0x158c6670u, 0x28374832u,
        0x3a576385u, 0x4748e52du, 0x54302323u, 0x696d376fu,
        0x7a323d17u, 0x81c97674u, 0x99a213eeu, 0xa4020f23u};
};

struct GameplayFrameRenderContext {
    GameplayFrameCallbacks callbacks;
    // Original DAT_0083f420 table consumed by FUN_004d7863.  It advances
    // animated terrain through a six-frame ping-pong sequence from the
    // simulation frame counter (DAT_007071a4).
    std::array<u32, kGameplayFrameAnimationSlotCount> animation_frame_table{
        0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
    u32 animation_frame_slot = 0;
    u32 animation_cycle = 0;
    u32 frame_counter = 0;
    u32 current_tick_ms = 0;
    i32 camera_x = 0;
    i32 camera_y = 0;
    u32 viewport_width = 800;
    u32 viewport_height = 600;
    i32 expanded_left = 0;
    i32 expanded_top = 0;
    i32 expanded_right = 0;
    i32 expanded_bottom = 0;
    bool pause_overlay_active = false;
    std::array<std::string, 3> pause_overlay_lines{};
    bool pause_overlay_blinks_third_line = true;
    UnitRenderQueueContext* unit_render_queue = nullptr;
    GameplayRenderCommandQueue* render_command_queue = nullptr;
    GameplayHudTextState* hud = nullptr;
    GameplayFogRenderContext* fog = nullptr;
};

u32 UpdateGameplayFrameAnimationSlot(GameplayFrameRenderContext& context);
void RenderGameplayFrameComposite(GameplayFrameRenderContext& context);

void ResetGameplayHudTextLayout(GameplayHudTextState& state);
void RenderGameplayHudAsciiTextLine(const char* text, i32 x, i32 y, u8 color = 1);
std::string FormatTeamReserveRotationCountdownText(u32 countdown_ticks);
void DrawTeamReserveRotationCountdownText(GameplayHudTextState& state,
    u32 countdown_ticks, i32 x, i32 y, u8 color = 1);
void QueueGameplayHudMessage(GameplayHudTextState& state, const char* text);
bool QueueGameplayHudMessageAndSound(GameplayHudTextState& state,
    GameplaySoundState& sound, const char* text, u32 sound_slot,
    i32 world_delta = 0, i32 pan = 0);
void RenderGameplayHudText(GameplayHudTextState& state);
void RenderTimedGameplayHudNotification(
    GameplayHudTextState& state, GameplayTimedHudNotification& notification,
    i32 x, i32 y);
bool GameplayPlayerResourceHudSlotVisible(
    const GameplayPlayerResourceHudState& state, u32 player);
void RenderGameplayPlayerResourceRows(GameplayPlayerResourceHudState& state);
void DrawCenteredGameplayBottomText(GameplayHudTextState& state, const char* text);
void DrawBottomLeftGameplayText(GameplayHudTextState& state, const char* text);
void TickGameplayDebugFrameCounter(GameplayHudTextState& state);
void RenderGameplayDebugFpsCounter(GameplayHudTextState& state);
void ResetGameplayHudAlertMarkers(GameplayHudAlertMarkerState& state);
bool QueueGameplayHudAlertMarker(GameplayHudAlertMarkerState& state, u32 kind,
    i32 world_x, i32 world_y);
void TickAndRenderGameplayHudAlertMarkers(GameplayHudAlertMarkerState& state);

void BindGameplayRenderTarget(GameplayFrameRenderContext& context,
    const SpriteRenderTarget& target);

bool ReserveGameplayRenderQueueIndex(
    GameplayRenderCommandQueue& queue, std::size_t& entry_index);
bool QueueGameplayRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);
void ResetGameplayRenderCommandQueue(GameplayRenderCommandQueue& queue);
void SortGameplayRenderCommandQueue(GameplayRenderCommandQueue& queue);
void SwapGameplayRenderSortedIndices(GameplayRenderCommandQueue& queue,
    std::size_t lhs, std::size_t rhs);
void QuickSortGameplayRenderSortedIndicesBySortKey(GameplayRenderCommandQueue& queue,
    std::size_t first, std::size_t last);
void EnsureGameplayRenderSortedIndices(GameplayRenderCommandQueue& queue);
void NoOpGameplayRenderQueueTail();
void ProcessGameplayRenderCommandQueue(GameplayRenderCommandQueue& queue);
GameplayRenderSpriteVariant ResolveGameplayRenderSpriteVariant(
    const UnitRenderItem& item);
bool QueueGameplayUnitRenderCommand(GameplayRenderCommandQueue& queue,
    const UnitRenderQueueEntry& entry, const UnitRenderItem& item,
    i32 camera_x, i32 camera_y, UnitRenderQueueContext* source_context = nullptr);
bool NoOpQueuedRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);
void DispatchUnitAnimationRenderQueueItem(UnitRenderQueueContext& context,
    const UnitRenderItem& item, i32 screen_x, i32 screen_y);
void DispatchUnitCellRenderQueueItem(UnitRenderQueueContext& context,
    const UnitRenderItem& item, i32 screen_x, i32 screen_y);
void DispatchPlacementPreviewDefinitionSprite(UnitRenderQueueContext& context,
    const UnitRenderItem& item, i32 screen_x, i32 screen_y);
bool DispatchQueuedUnitRenderByTypeCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);
bool DrawQueuedUnitRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);
bool DrawQueuedTerrainTileRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);
bool DispatchQueuedUnitEffectRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);
bool DrawQueuedTerrainDecorationRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command);

GameplayFrameRandomResult SelectGameplayFrameRandomLimit(
    GameplayFrameRandomState& state, u32 preserved_value, u32 frame_counter);

} // namespace ranker
