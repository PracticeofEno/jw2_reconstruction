#pragma once

#include "ranker_gameplay_context_cursor.h"
#include "ranker_map_brush.h"
#include "ranker_resource_store.h"
#include "ranker_types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

constexpr u8 kUiOverlayPreserveMetricFont = 0xffu;

struct UnitMovementUnit;

constexpr std::size_t kUiOverlayDispatchHandlerCount = 0x400;
constexpr std::size_t kUiOverlayDynamicIconRectCount = 0x10;
constexpr std::size_t kUiOverlaySideSlotRectCount = 0x10;
constexpr std::size_t kUiOverlayGroupedCommandOwnerCount = 8;
constexpr std::size_t kUiOverlayGroupedCommandOrderCount = 0x40;
constexpr std::size_t kUiOverlayGroupedCommandTypeCount = 0xaa;
constexpr u32 kUiOverlayPointerPress = 0x02;
constexpr u32 kUiOverlayPointerRelease = 0x04;
constexpr u32 kUiOverlayPointerHoldPress = 0x08;
constexpr u32 kUiOverlayPointerHoldRelease = 0x10;
constexpr u32 kUiOverlayPointerDrag = 0x20;
constexpr u32 kUiOverlayPointerMenuAction = 0x80;
constexpr u32 kUiOverlayPointerMinimapDrag = 0x100;
constexpr u32 kUiOverlayCommandActionClick = 1;
constexpr u32 kUiOverlayCommandActionHold = 2;
constexpr u32 kUiOverlayCommandActionMinimap = 3;
constexpr u32 kUiOverlayCommandActionPlacement = 4;
constexpr u32 kUiOverlayCommandActionSelection = 5;
constexpr u32 kUiOverlayCommandActionContextual = 6;
// Original 0x004ea939 stages a nonzero-mode JW2_11 production action by
// storing selector + 0x2a in DAT_00869dfc.  The next map/unit click converts
// that mode back to command item 0xd4 + selector.
constexpr u32 kUiOverlayProductionActionModeBase = 0x2au;
constexpr u32 kUiOverlayProductionActionSelectorCount = 0x20u;
constexpr bool IsUiOverlayProductionActionMode(u32 mode) {
    return mode >= kUiOverlayProductionActionModeBase &&
        mode < kUiOverlayProductionActionModeBase +
            kUiOverlayProductionActionSelectorCount;
}
constexpr u32 UiOverlayProductionActionMode(u32 selector) {
    return kUiOverlayProductionActionModeBase + selector;
}
constexpr u32 UiOverlayProductionActionSelector(u32 mode) {
    return mode - kUiOverlayProductionActionModeBase;
}

// DAT_0086a860, indexed by the low-byte scan code in FUN_004e77a5.  The
// resulting byte is compared with the TRC marker stored at command record
// +0x1c by FUN_004e3ece.  This deliberately follows physical scan codes and
// is independent of the active Win32 keyboard layout.
constexpr std::array<u8, 0x59> MakeOriginalGameplayHotkeyMarkerTable() {
    std::array<u8, 0x59> markers{};
    markers[0x02] = '1';
    markers[0x03] = '2';
    markers[0x04] = '3';
    markers[0x05] = '4';
    markers[0x06] = '5';
    markers[0x07] = '6';
    markers[0x08] = '7';
    markers[0x09] = '8';
    markers[0x0a] = '9';
    markers[0x0b] = '0';
    markers[0x0c] = '-';
    markers[0x0d] = '=';
    markers[0x10] = 'Q';
    markers[0x11] = 'W';
    markers[0x12] = 'E';
    markers[0x13] = 'R';
    markers[0x14] = 'T';
    markers[0x15] = 'Y';
    markers[0x16] = 'U';
    markers[0x17] = 'I';
    markers[0x18] = 'O';
    markers[0x19] = 'P';
    markers[0x1a] = '[';
    markers[0x1b] = ']';
    markers[0x1e] = 'A';
    markers[0x1f] = 'S';
    markers[0x20] = 'D';
    markers[0x21] = 'F';
    markers[0x22] = 'G';
    markers[0x23] = 'H';
    markers[0x24] = 'J';
    markers[0x25] = 'K';
    markers[0x26] = 'L';
    markers[0x27] = ';';
    markers[0x28] = '`';
    markers[0x29] = '\'';
    markers[0x2b] = '\\';
    markers[0x2c] = 'Z';
    markers[0x2d] = 'X';
    markers[0x2e] = 'C';
    markers[0x2f] = 'V';
    markers[0x30] = 'B';
    markers[0x31] = 'N';
    markers[0x32] = 'M';
    markers[0x33] = ',';
    markers[0x34] = '.';
    markers[0x35] = '/';
    markers[0x39] = ' ';
    markers[0x58] = 0xffu;
    return markers;
}

constexpr std::array<u8, 0x59> kOriginalGameplayHotkeyMarkers =
    MakeOriginalGameplayHotkeyMarkerTable();

constexpr u8 ResolveUiOverlayGameplayHotkeyMarker(u32 legacy_scan_code) {
    return legacy_scan_code < kOriginalGameplayHotkeyMarkers.size()
        ? kOriginalGameplayHotkeyMarkers[legacy_scan_code]
        : 0;
}

// The 91-entry switch table at 0x004e70cc does not send every nonzero marker
// through FUN_004e77a5.  Brackets, space, digits, function keys, and the
// speed/minimap keys have dedicated branches (or return immediately).  Keep
// the exact set of switch entries which target the command-hotkey thunk at
// 0x004030f8; otherwise '['/']' and F12 can activate unrelated records.
constexpr bool OriginalGameplayScanRoutesToCommandHotkey(
    u32 legacy_scan_code) {
    switch (legacy_scan_code) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
    case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x22:
    case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28:
    case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
    case 0x35:
        return true;
    default:
        return false;
    }
}

// WM_KEYDOWN supplies the scan code. WM_CHAR is retained solely for chat
// text; otherwise one physical key press would publish the same command twice.
constexpr u8 ResolveUiOverlayGameplayCommandMarker(
    u32 legacy_scan_code, u8 ascii, bool chat_active) {
    return !chat_active && ascii == 0 &&
            OriginalGameplayScanRoutesToCommandHotkey(legacy_scan_code)
        ? ResolveUiOverlayGameplayHotkeyMarker(legacy_scan_code)
        : 0;
}

enum class UiOverlayGameplayKeyboardRoute : u8 {
    none = 0,
    chat_character,
    chat_escape,
    begin_chat,
    cancel_mode,
    control_group,
    increase_speed,
    decrease_speed,
    toggle_minimap,
    cycle_control_group,
    stored_minimap_point,
    camera_bookmark,
    save_menu,
    load_menu,
    options_menu,
    pause_menu,
    toggle_overlay,
    command_hotkey,
};

constexpr UiOverlayGameplayKeyboardRoute ResolveUiOverlayGameplayKeyboardRoute(
    u32 legacy_scan_code, u8 ascii, bool chat_active) {
    if (chat_active) {
        // At 0x004e70c2 the original returns for scan-only messages while
        // chat is active. Escape closes chat only when WM_CHAR supplies 0x1b.
        if (ascii == 0) {
            return UiOverlayGameplayKeyboardRoute::none;
        }
        return ascii == 0x1b
            ? UiOverlayGameplayKeyboardRoute::chat_escape
            : UiOverlayGameplayKeyboardRoute::chat_character;
    }
    if (ascii != 0) {
        return ascii == '\r' || ascii == '\n'
            ? UiOverlayGameplayKeyboardRoute::begin_chat
            : UiOverlayGameplayKeyboardRoute::none;
    }
    switch (legacy_scan_code) {
    case 1:
        return UiOverlayGameplayKeyboardRoute::cancel_mode;
    case 2: case 3: case 4: case 5: case 6:
    case 7: case 8: case 9: case 10: case 11:
        return UiOverlayGameplayKeyboardRoute::control_group;
    case 0x0c: case 0x4a:
        return UiOverlayGameplayKeyboardRoute::increase_speed;
    case 0x0d: case 0x4e:
        return UiOverlayGameplayKeyboardRoute::decrease_speed;
    case 0x0f:
        return UiOverlayGameplayKeyboardRoute::toggle_minimap;
    case 0x29:
        return UiOverlayGameplayKeyboardRoute::cycle_control_group;
    case 0x39:
        return UiOverlayGameplayKeyboardRoute::stored_minimap_point;
    case 0x3b: case 0x3f: case 0x40: case 0x41: case 0x42: case 0x43:
        return UiOverlayGameplayKeyboardRoute::camera_bookmark;
    case 0x3c:
        return UiOverlayGameplayKeyboardRoute::save_menu;
    case 0x3d:
        return UiOverlayGameplayKeyboardRoute::load_menu;
    case 0x3e:
        return UiOverlayGameplayKeyboardRoute::options_menu;
    case 0x44:
        return UiOverlayGameplayKeyboardRoute::pause_menu;
    case 0x57:
        return UiOverlayGameplayKeyboardRoute::toggle_overlay;
    default:
        return ResolveUiOverlayGameplayCommandMarker(
                   legacy_scan_code, ascii, chat_active) != 0
            ? UiOverlayGameplayKeyboardRoute::command_hotkey
            : UiOverlayGameplayKeyboardRoute::none;
    }
}

constexpr u8 ResolveUiOverlayRecordHotkey(u32 icon_marker) {
    return static_cast<u8>(icon_marker & 0xffu);
}

// Both flag-1 offscreen paths in FUN_004e5621/FUN_004e56a2 explicitly write
// zero to record +0x1c.  Their records preserve original bookkeeping geometry
// but are deliberately unavailable to the keyboard scan.
constexpr u32 ResolveUiOverlayOffscreenRecordMarker(u32 command_icon_marker) {
    (void)command_icon_marker;
    return 0;
}

constexpr u32 ResolveUiOverlayEffectiveHotkeyFlags(
    u32 record_flags, bool enabled) {
    return enabled ? record_flags : (record_flags | 0x02u);
}

constexpr bool IsUiOverlayHotkeyActionBlocked(
    u32 record_flags, bool enabled) {
    return (ResolveUiOverlayEffectiveHotkeyFlags(record_flags, enabled) &
        0x36u) != 0;
}

// DAT_0162ea48 is a wrapping legacy millisecond tick.  The repeated control
// group press focuses the camera for deltas through 0x190 (400 ms), inclusive.
constexpr bool IsOriginalControlGroupDoubleTap(
    u32 current_tick_ms, u32 previous_tick_ms) {
    return current_tick_ms - previous_tick_ms <= 0x190u;
}

constexpr std::array<u32, 10> kOriginalSelectedHealthTextColors{{
    0x09u, 0x09u, 0x99u, 0x11u, 0x11u,
    0xa9u, 0xc1u, 0xc9u, 0xd1u, 0x71u,
}};

// FUN_004e1544 indexes the DWORD table at 0x008640d8 without clamping the
// HP * 4 / (max HP + effect 00) result.  The original table tail following
// its ten nonzero entries is zero, rather than a repeat of entry nine.
constexpr u32 ResolveSelectedUnitHealthTextColor(u32 health_color_step) {
    return health_color_step < kOriginalSelectedHealthTextColors.size()
        ? kOriginalSelectedHealthTextColors[health_color_step]
        : 0u;
}

// FUN_004e1544 tests raw unit +0x48 before looking at the selected name.  A
// valid nonzero slot owns the name even when its first byte is NUL; only a
// missing slot selects the unit-definition fallback.
constexpr bool UsesSelectedUnitDynamicNameSlot(bool movement_available,
    u32 string_slot, std::size_t string_slot_count) {
    return movement_available && string_slot != 0 &&
        string_slot < string_slot_count;
}
constexpr std::size_t kCameraScrollSpeedCount = 16;
constexpr std::size_t kCameraScrollRampCount = 8;
constexpr std::array<std::array<u32, kCameraScrollRampCount>,
    kCameraScrollSpeedCount>
    kDefaultCameraScrollSteps{{
        {{64, 64, 64, 64, 64, 64, 64, 64}},
        {{56, 56, 56, 56, 56, 56, 56, 56}},
        {{48, 48, 48, 48, 48, 48, 48, 48}},
        {{32, 32, 32, 32, 32, 32, 32, 40}},
        {{2, 4, 8, 16, 16, 32, 32, 32}},
        {{1, 2, 4, 8, 16, 28, 28, 28}},
        {{1, 2, 4, 8, 16, 18, 19, 24}},
        {{1, 2, 4, 8, 17, 18, 19, 20}},
        {{1, 2, 4, 8, 13, 14, 15, 16}},
        {{1, 2, 4, 8, 11, 12, 13, 14}},
        {{1, 2, 4, 8, 9, 10, 11, 12}},
        {{1, 2, 4, 6, 7, 8, 9, 9}},
        {{1, 2, 3, 4, 5, 6, 6, 6}},
        {{1, 2, 3, 4, 4, 4, 4, 4}},
        {{1, 2, 2, 2, 2, 2, 2, 2}},
        {{1, 1, 1, 1, 1, 1, 1, 1}},
    }};

struct UiOverlayRect {
    i32 x = 0;
    i32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

struct UiOverlayPlacementGridCoordinates {
    i32 tile_x = 0;
    i32 tile_y = 0;
    i32 aligned_world_x = 0;
    i32 aligned_world_y = 0;
    i32 screen_x = 0;
    i32 screen_y = 0;
};

struct UiOverlayDrawRecord {
    u32 item_id = 0;
    u32 aux = 0;
    u32 flags = 0;
    i32 x = 0;
    i32 y = 0;
    u32 width = 0;
    u32 height = 0;
    u32 icon_marker = 0;
};

enum class UiOverlayIconBlitKind : u32 {
    base = 0,
    palette_table = 1,
    unit = 2,
    object = 3,
    equipment = 4,
    equipment_half_sampled = 5,
    production = 6,
    unit_clipped = 7,
    base_clipped = 8,
    equipment_clipped = 9,
    production_clipped = 10,
    disabled_base_clipped = 11,
    disabled_unit_clipped = 12,
    disabled_object_clipped = 13,
    disabled_production_clipped = 14,
    masked_disabled_base = 15,
    masked_disabled_unit = 16,
    masked_disabled_object = 17,
    masked_disabled_production = 18,
};

struct UiOverlayIconBlitRequest {
    UiOverlayIconBlitKind kind = UiOverlayIconBlitKind::base;
    u32 item_id = 0;
    i32 x = 0;
    i32 y = 0;
    u32 width = 0x26;
    u32 height = 0x26;
    u32 palette_selector = 0;
    bool clipped = false;
    bool disabled = false;
    bool half_sampled = false;
    bool masked_palette = false;
};

struct UiOverlayTextCommand {
    std::string text;
    i32 x = 0;
    i32 y = 0;
    u8 color = 1;
    u8 draw_font = 0;
    // 0xff mirrors original draw-only font selections that deliberately
    // leave the current metric font untouched.
    u8 metric_font = 0;
    bool centered = false;
    bool right_aligned = false;
    bool bottom_aligned = false;
};

struct UiOverlayProgressCommand {
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
    u32 numerator = 0;
    u32 denominator = 0;
};

enum class UiOverlayMinimapMarkerKind : u8 {
    terrain_overlay = 0,
    object_footprint = 1,
    active_unit = 2,
    fog_hidden = 3,
    fog_dimmed = 4,
    placement_preview = 5,
};

enum class UiOverlayDoubleClickSelectionResult : u8 {
    ignored = 0,
    selected = 1,
    fallback_release = 2,
};

struct UiOverlayMinimapMarker {
    UiOverlayMinimapMarkerKind kind = UiOverlayMinimapMarkerKind::terrain_overlay;
    i32 x = 0;
    i32 y = 0;
    u32 width = 1;
    u32 height = 1;
    u16 color = 0;
    u32 item_id = 0;
    u32 owner_id = 0;
    bool valid = true;
};

struct UiOverlayMinimapUnit {
    u32 unit_id = 0;
    u32 type_id = 0;
    u32 owner_id = 0;
    u32 runtime_flags = 0;
    u32 health = 0;
    u32 max_health = 0;
    u32 status_timer = 0;
    u32 action_effect_flags = 0;
    u32 selection_score = 0;
    i32 world_x = 0;
    i32 world_y = 0;
    i32 bounds_left = 0;
    i32 bounds_top = 0;
    i32 bounds_width = 0;
    i32 bounds_height = 0;
    u32 footprint_width_tiles = 1;
    u32 footprint_height_tiles = 1;
    bool visible_to_local_player = true;
    bool hidden_from_minimap = false;
    // FUN_004d6cb0 applies FUN_004d6cca only to command-flag/bitmap-gated
    // units.  Both FUN_004e284a's minimap marker pass and FUN_004e96ae's
    // hit-selection pass consume that same gate before their fog-tile tests.
    bool special_visibility_gate_passed = true;
};

constexpr bool UiOverlayUnitVisibleToLocalPlayer(
    const UiOverlayMinimapUnit& unit) {
    return !unit.hidden_from_minimap && unit.visible_to_local_player &&
        unit.special_visibility_gate_passed;
}

constexpr bool ShouldRenderMinimapUnitMarker(const UiOverlayMinimapUnit& unit) {
    return unit.type_id < 0x60 && UiOverlayUnitVisibleToLocalPlayer(unit);
}

struct UiOverlayMapEffect {
    u32 instance_id = 0;
    u32 effect_id = 0;
    u32 amount = 0;
    i32 world_x = 0;
    i32 world_y = 0;
};

struct UiOverlaySelectionRectScanResult {
    u32 flags = 0;
    u32 enemy_unit_id = 0;
    u32 local_object_id = 0;
    u32 enemy_object_id = 0;
    bool local_unit_found = false;
};

struct UiOverlayHudPulseCommand {
    i32 x = 0;
    i32 y = 0;
    u32 phase = 0;
    u32 frame_counter = 0;
};

struct UiOverlayCommandOption {
    u32 item_id = 0;
    u32 aux = 0;
    u32 flags = 0;
    u32 icon_marker = 0;
    u8 hotkey = 0;
    bool enabled = true;
};

// FUN_004e5292 at 0x004e534f copies original raw unit +0x68 into ECX for the
// active 0x1aa/0x1ab/0x1ac queue record.  The reconstruction splits that raw
// state-dependent word from its optional typed target pointer; the UI must
// keep the raw command value even when the pointer is null or names another
// unit.  Taking both values here makes that original choice explicit.
constexpr u32 ResolveUiOverlayActiveQueueRecordPayload(
    u32 raw_command_value, u32 typed_target_id) {
    (void)typed_target_id;
    return raw_command_value;
}

// FUN_004e5292 compares the complete command-state dword before choosing the
// indexed queue record kind.  In particular, high flag bits must not turn an
// unrelated state into a production state merely because its low byte agrees.
constexpr u32 ResolveUiOverlayActiveQueueDispatchItem(u32 command_state) {
    switch (command_state) {
    case 0x50u:
    case 0x51u:
        return 0x1aau;
    case 0x4du:
    case 0x4eu:
        return 0x1abu;
    case 0x82u:
    case 0x83u:
        return 0x1acu;
    default:
        return 0;
    }
}

constexpr u32 ResolveUiOverlayDeferredQueueDispatchItem(u32 command_state) {
    switch (command_state) {
    case 0x10u:
        return 0x1aau;
    case 0x17u:
        return 0x1abu;
    case 0x22u:
        return 0x1acu;
    default:
        return 0;
    }
}

// 0x004e5298/0x004e52c9 and 0x004e52d6..0x004e5300 admit queue records only
// for one selected structure that the local player may inspect.  Player type
// 2 and the replay/scenario override retain the original observer access.
constexpr bool ShouldPublishUiOverlaySelectedStructureQueue(
    u32 selected_count, u32 selected_type, bool replay_or_scenario_override,
    u32 local_player_type, u32 selected_owner, u32 local_owner) {
    return selected_count == 1u && selected_type >= 0x60u &&
        (replay_or_scenario_override || local_player_type == 2u ||
            selected_owner == local_owner);
}

// FUN_004e2042/FUN_004e208f/FUN_004e20dc consume the aux dword as the frame
// index in the base-unit, production-order, or equipment command table.  Keep
// this resolution shared with the draw dispatcher so regression tests observe
// the exact request that the renderer consumes.
constexpr UiOverlayIconBlitRequest ResolveUiOverlayIndexedQueueIconRequest(
    u32 dispatch_item_id, u32 payload) {
    UiOverlayIconBlitRequest request{};
    request.item_id = payload;
    if (dispatch_item_id == 0x1abu) {
        request.kind = UiOverlayIconBlitKind::production;
    }
    else if (dispatch_item_id == 0x1acu) {
        request.kind = UiOverlayIconBlitKind::equipment;
    }
    return request;
}

struct UiOverlayTransportPassenger {
    u32 unit_id = 0;
    u32 type_id = 0;
};

struct UiOverlayHotRegion {
    UiOverlayDrawRecord record;
    u8 hotkey = 0;
    bool enabled = true;
};

struct UiOverlayCameraBookmark {
    bool valid = false;
    i32 camera_x = 0;
    i32 camera_y = 0;
};

struct UiOverlayControlGroup {
    std::vector<u32> unit_ids;
};

struct UiOverlayChatMessage {
    std::string text;
    u32 channel = 0;
    u32 expire_frame = 0;
    bool local_echo = true;
};

struct UiOverlayHoverContext {
    u32 kind = 0;
    u32 item_id = 0;
    u32 unit_id = 0;
    i32 x = 0;
    i32 y = 0;
};

struct UiOverlayCommandAction {
    u32 item_id = 0;
    u32 aux = 0;
    u32 flags = 0;
    u32 action = 0;
    i32 world_x = 0;
    i32 world_y = 0;
};

struct UiOverlayGroupedCommandGateDefinition {
    u32 order_variant_gate = 0;
    u32 order_variant_index = 0;
    u32 completed_type_gate = 0;
    u32 completed_type_index = 0;
};

struct UiOverlayState;

using UiOverlayFrameCallback = void (*)(UiOverlayState& state);
using UiOverlayDrawRecordCallback =
    bool (*)(UiOverlayState& state, const UiOverlayDrawRecord& record);
using UiOverlayRectangleCallback =
    void (*)(UiOverlayState& state, i32 left, i32 top, i32 right, i32 bottom);
using UiOverlayUnitSelectionCallback =
    void (*)(UiOverlayState& state, const UiOverlayMinimapUnit& unit);
using UiOverlayProductionGateCallback =
    bool (*)(UiOverlayState& state, u32 selector, u32& failure_code);
using UiOverlayChatSubmitCallback = bool (*)(UiOverlayState& state,
    const std::string& text, u32 channel);

struct UiOverlayCallbacks {
    UiOverlayFrameCallback draw_placement_preview = nullptr;
    UiOverlayFrameCallback draw_world_surface = nullptr;
    UiOverlayFrameCallback draw_after_queue = nullptr;
    UiOverlayFrameCallback draw_after_overlay = nullptr;
    UiOverlayDrawRecordCallback draw_record = nullptr;
    UiOverlayRectangleCallback draw_selection_rectangle = nullptr;
    UiOverlayUnitSelectionCallback on_unit_selected = nullptr;
    UiOverlayProductionGateCallback check_selected_production_action_gate = nullptr;
    UiOverlayFrameCallback on_chat_input_begin = nullptr;
    UiOverlayFrameCallback on_chat_input_end = nullptr;
    UiOverlayFrameCallback request_script_wait_break = nullptr;
    UiOverlayFrameCallback play_click_sound = nullptr;
    UiOverlayFrameCallback open_save_session_dialog = nullptr;
    UiOverlayFrameCallback open_load_session_dialog = nullptr;
    UiOverlayFrameCallback open_options_menu = nullptr;
    UiOverlayFrameCallback open_pause_menu = nullptr;
    UiOverlayFrameCallback update_catchup_target_if_active = nullptr;
};

struct UiOverlayState {
    UiOverlayCallbacks callbacks;
    std::array<UiOverlayDrawRecordCallback, kUiOverlayDispatchHandlerCount>
        dispatch_handlers{};
    std::vector<UiOverlayDrawRecord> queued_records;
    std::vector<UiOverlayDrawRecord> dispatched_records;
    std::vector<UiOverlayIconBlitRequest> icon_blit_requests;
    std::vector<UiOverlayTextCommand> text_commands;
    std::vector<UiOverlayProgressCommand> progress_commands;
    // Keep the diagnostic command streams intact after an immediate
    // record-level draw.  These parallel bytes prevent the frame-tail pass
    // from drawing that command a second time.
    std::vector<u8> text_command_flushed;
    std::vector<u8> progress_command_flushed;
    std::vector<UiOverlayMinimapMarker> minimap_markers;
    std::vector<UiOverlayHudPulseCommand> pulse_commands;
    std::vector<UiOverlayCommandOption> command_options;
    std::vector<UiOverlayCommandOption> primary_production_options;
    std::vector<UiOverlayTransportPassenger> selected_transport_passengers;
    std::vector<UiOverlayHotRegion> hot_regions;
    std::vector<u32> selected_unit_ids;
    std::vector<UiOverlayChatMessage> chat_messages;
    std::vector<UiOverlayCommandAction> command_actions;

    u32 selected_context_id = 0;
    u32 small_icon_resource_base = kInvalidResourceEntry;
    u32 large_icon_resource_base = kInvalidResourceEntry;
    u32 marker_resource_base = kInvalidResourceEntry;
    u32 glyph_resource_base = kInvalidResourceEntry;
    u32 digit_resource_base = kInvalidResourceEntry;
    u32 command_ack_resource_base = kInvalidResourceEntry;
    u32 alternate_slot_a = 1;
    u32 alternate_slot_b = 1;

    i32 small_slot1_x = 0;
    i32 small_slot1_y = 0;
    i32 small_slot2_x = 0;
    i32 small_slot2_y = 0;
    i32 small_slot3_x = 0;
    i32 small_slot3_y = 0;
    i32 large_slot_x = 0;
    i32 large_slot_y = 0;
    i32 large_slot3_x = 0;
    i32 large_slot3_y = 0;
    i32 large_slot4_x = 0;
    i32 large_slot4_y = 0;
    i32 large_slot5_x = 0;
    i32 large_slot5_y = 0;
    i32 large_slot6_x = 0;
    i32 large_slot6_y = 0;

    UiOverlayRect small_slot1_bounds;
    UiOverlayRect small_slot2_bounds;
    UiOverlayRect small_slot3_bounds;
    UiOverlayRect large_slot0_bounds;
    UiOverlayRect large_slot3_bounds;
    UiOverlayRect large_slot6_bounds;
    UiOverlayRect large_slot9_bounds;
    UiOverlayRect large_slot12_bounds;
    UiOverlayRect wide_slot_bounds;
    std::array<UiOverlayRect, 7> manual_equipment_slot_bounds{};
    std::array<UiOverlayRect, kUiOverlaySideSlotRectCount> side_slot_bounds{};
    std::array<UiOverlayRect, kUiOverlayDynamicIconRectCount> dynamic_icon_bounds{};
    std::vector<UiOverlayRect> indexed_slot_bounds;
    std::vector<u32> unit_definition_icon_markers;
    std::vector<u32> object_icon_markers;
    std::vector<u32> production_action_icon_markers;
    std::vector<u32> production_order_icon_markers;
    std::vector<u32> equipment_icon_markers;
    std::vector<u32> equipment_icon_frame_indices;

    u32 screen_width = 800;
    u32 screen_height = 600;
    u32 interface_theme_index = 0;
    u32 dynamic_icon_index = 0;
    u32 side_slot_index = 0;
    u32 current_icon_marker = 0;
    u32 current_record_size = 0x26;
    u32 current_palette_selector = 0;
    u32 current_detail_item_id = 0;
    u32 detail_progress = 0;
    u32 detail_progress_total = 0;
    u32 selected_unit_health = 0;
    u32 selected_unit_health_ratio_max = 0;
    u32 selected_unit_health_text_color = 0x11;
    // FUN_004e1d25 renders raw unit +0x54 plus one over the single-selected
    // mobile portrait when definition +0x1f4 bit 1 is set.
    u32 selected_unit_level = 0;
    bool selected_unit_level_glyph_enabled = false;
    u32 selected_unit_max_health = 0;
    u32 selected_unit_base_max_health = 0;
    u32 selected_unit_secondary = 0;
    u32 selected_unit_secondary_ratio_max = 0;
    bool selected_unit_secondary_line_enabled = false;
    u32 selected_unit_max_secondary = 0;
    u32 selected_unit_base_max_secondary = 0;
    u32 selected_unit_slot_value = 0;
    // Original raw unit +0x30..+0x44.  The command overlay presents these in
    // the non-linear 4,5,0,1,2,3 order used by dispatch ids 0x1ae..0x1b3.
    std::array<u32, 6> selected_unit_equipment_slots{};
    u32 selected_unit_command_state = 0;
    u32 selected_unit_command_flags = 0;
    u32 selected_unit_runtime_flags = 0;
    u32 selected_unit_action_mode_gate = 0;
    // FUN_004e4150 aggregates every active selected mobile, including units
    // selected through script paths whose owner is not the local player.
    u32 selected_mobile_unit_count = 0;
    u32 selected_mobile_action_mode_sum = 0;
    u32 selected_mobile_command_flags_or = 0;
    u32 selected_mobile_production_action_mask = 0;
    u32 selected_mobile_runtime_command_mask = 0;
    std::array<u32, 0x60> selected_grouped_mobile_type_counts{};
    u32 selected_transport_load_flags = 0x02u;
    u32 selected_transport_unload_flags = 0x02u;
    bool all_selected_mobile_can_produce = false;
    bool all_selected_mobile_command_state_60 = false;
    u32 selected_unit_raw_production_reference_count = 0;
    bool selected_unit_uses_avatar_production_slots = false;
    bool selected_unit_details_visible = false;
    u32 current_icon_number = 0;
    std::string detail_primary_text;
    std::string detail_secondary_text;
    std::string detail_clock_text;
    std::string detail_route_text;
    std::string selected_unit_name_text;
    std::string selected_unit_indestructible_text;
    std::string selected_unit_owner_text;
    std::string selected_unit_experience_text;
    std::string selected_unit_order_text;
    std::string chat_input_text;
    // Original FUN_004e7b48 forwards a chat line beginning with '!' to the
    // subtype-0x19 selected-unit publisher before the chat buffer is cleared.
    // Keep the complete line (including '!') so a lone "!" still publishes an
    // empty payload and clears the selected units' pending string slots.
    std::string pending_unit_action_text;
    std::vector<std::string> selected_unit_capability_lines;

    MinimapRenderState minimap;
    u32 screen_layout_bucket = 0;
    u32 selected_faction_id = 0;
    u32 local_player_slot = 0;
    u32 local_player_type = 0;
    u32 local_owner_relation_mask = 0;
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
    i32 camera_x = 0;
    i32 camera_y = 0;
    i32 camera_max_x = 0;
    i32 camera_max_y = 0;
    i32 minimap_camera_anchor_x = 0;
    i32 minimap_camera_anchor_y = 0;
    u32 world_viewport_height = 0;
    i32 stored_minimap_world_x = 0;
    i32 stored_minimap_world_y = 0;
    bool stored_minimap_point_valid = false;
    u32 placement_mode = 0;
    u32 placement_definition_id = 0;
    u32 placement_equipment_slot_code = 0;
    i32 placement_pointer_x = 0;
    i32 placement_pointer_y = 0;
    u32 placement_footprint_width_tiles = 1;
    u32 placement_footprint_height_tiles = 1;
    bool placement_preview_valid = true;
    std::vector<u8> placement_preview_cell_validity;
    bool reveal_minimap_fog = false;
    bool scenario_ai_profile_override = false;
    // Mirrors original DAT_00725bf8.  Both P2P and the worker-driven
    // single-player profile use this non-modal gameplay path.
    bool generic_ai_profile_mode = false;
    bool replay_timing_enabled = false;
    bool scripted_input_restricted = false;
    std::vector<u32> minimap_visibility_flags;
    std::vector<u32> minimap_object_flags;
    std::vector<u32> minimap_overlay_flags;
    std::vector<UiOverlayMinimapUnit> minimap_units;
    // Lifecycle/death-list units are kept out of ordinary selection and
    // minimap rendering.  Direction-mode-4 abilities query this list through
    // FindFreeUnitUnderStoredPointer to target a revivable corpse.
    std::vector<UiOverlayMinimapUnit> lifecycle_units;
    std::vector<UiOverlayMapEffect> map_effects;
    std::vector<u16> minimap_owner_colors;
    std::vector<u16> minimap_owner_footprint_colors;
    std::vector<UiOverlayRect> minimap_definition_footprints;
    u16 minimap_terrain_marker_color = 0x149f;
    u16 minimap_local_unit_color = 0x07c2;
    u16 minimap_local_footprint_color = 0x05e2;
    u16 minimap_remote_unit_color = 0x05e2;
    u16 minimap_remote_footprint_color = 0x05e2;
    u16 minimap_hidden_color = 0;
    u16 minimap_dim_mask = 0xf7de;
    u32 minimap_background_entry = 0;
    u32 resource_icon_entry = 0;
    u32 population_icon_entry = 0;
    u32 resource_amount = 0;
    u32 population_used = 0;
    u32 population_available = 0;
    u32 population_limit = 0;
    i32 resource_counter_x = 0;
    i32 resource_counter_y = 5;
    i32 population_counter_x = 0;
    i32 population_counter_y = 5;
    u32 hud_pulse_phase = 0x0f;
    u32 hud_pulse_start_frame = 0;
    i32 hud_pulse_x = 0;
    i32 hud_pulse_y = 0;
    u32 command_slot_count = 0;
    u32 command_slot_size = 0x26;
    u32 command_icon_marker = 0;
    u32 selected_unit_id = 0;
    u32 selected_unit_type = 0;
    u32 selected_unit_owner = 0;
    u32 selected_unit_count = 0;
    u32 max_selected_unit_count = 0x0e;
    u32 selected_unit_capability_mask = 0;
    u32 selected_unit_status_mask = 0;
    // DAT_00864b94 is intentionally not cleared at FUN_004e4150 entry.  It
    // survives category/cancel early returns until the 32-action loop shifts
    // it completely out.
    u32 selected_production_status_latch = 0;
    u32 selected_unit_command_bit_mask = 0;
    std::array<u32, 4> selected_unit_special_action_states{};
    bool selected_unit_order_2a_available = false;
    u32 selected_production_category = 0;
    u32 staged_unit_action_id = 0xffffffffu;
    u32 replay_speed_index = 4;
    u32 last_hotkey_command = 0;
    u32 last_hotkey_aux = 0;
    u32 last_hotkey_flags = 0;
    u32 last_hotkey_hover_kind = 0;
    i32 last_hot_region_x = 0;
    i32 last_hot_region_y = 0;
    std::array<u32, 4> selected_production_gate_masks{};
    std::array<u32, 3> selected_production_class_counts{};
    std::array<UiOverlayGroupedCommandGateDefinition,
        kUiOverlayGroupedCommandTypeCount> grouped_command_gate_definitions{};
    std::array<std::array<u8, kUiOverlayGroupedCommandOrderCount>,
        kUiOverlayGroupedCommandOwnerCount> grouped_command_order_variant_counts{};
    std::array<std::array<u32, kUiOverlayGroupedCommandTypeCount>,
        kUiOverlayGroupedCommandOwnerCount> grouped_command_completed_type_counts{};
    std::array<UiOverlayRect, 8> command_slot_bounds{};
    std::array<UiOverlayCameraBookmark, 12> camera_bookmarks{};
    std::array<UiOverlayControlGroup, 16> control_groups{};
    // False only between a session UI reset and the one-time import from raw
    // unit +0x08.  It prevents a deliberate Ctrl+group clear from being
    // repopulated by the old unit nibble on a later render frame.
    bool control_groups_initialized_from_unit_flags = false;
    bool control_groups_dirty_for_unit_flags = false;
    // FUN_004e74e2 can select units yet leave the primary pointer null when
    // every six-stat selection score is zero. Preserve that exact result
    // across the following per-frame recount only for this recall path.
    bool control_group_recall_primary_intentionally_null = false;
    std::array<std::array<u32, kCameraScrollRampCount>, kCameraScrollSpeedCount>
        camera_scroll_steps = kDefaultCameraScrollSteps;
    bool chat_active = false;
    bool chat_cursor_visible = false;
    bool pending_local_command = false;
    bool direct_music_available = false;
    bool direct_music_paused = false;
    bool replay_vpos_available = false;
    bool setup_write_requested = false;
    bool camera_scroll_dirty = false;
    bool gameplay_overlay_flag = false;
    bool minimap_mode = false;
    bool additive_selection_mode = false;
    bool box_select_same_type_only = false;
    bool shift_modifier_down = false;
    bool ctrl_modifier_down = false;
    bool alt_modifier_down = false;
    bool control_group_assign_mode = false;
    bool camera_edge_pointer_valid = true;
    bool camera_left_key_down = false;
    bool camera_right_key_down = false;
    bool camera_up_key_down = false;
    bool camera_down_key_down = false;
    u32 chat_channel = 0;
    u32 default_chat_channel = 0;
    u32 chat_expire_frame = 0;
    u32 current_tick_ms = 0;
    u32 current_frame_counter = 0;
    u32 game_speed = 0;
    u32 max_game_speed = 0x0f;
    u32 camera_scroll_speed_index = 0;
    u32 last_control_group = 0xffffffffu;
    u32 last_control_group_tick_ms = 0;
    u32 camera_scroll_ramp = 0;
    u32 camera_scroll_tick_bucket = 0;
    u32 camera_edge_cursor_index = 0;
    GameplayContextCursorState context_cursor;
    UiOverlayHoverContext hover_context;
    u32 pointer_state = 0;
    u32 pressed_command_id = 0xffffffffu;
    u32 pressed_command_aux = 0xffffffffu;
    u32 held_command_id = 0xffffffffu;
    bool command_button_press_active = false;
    i32 mouse_x = 0;
    i32 mouse_y = 0;
    bool selection_rectangle_active = false;
    i32 selection_left = 0;
    i32 selection_top = 0;
    i32 selection_right = 0;
    i32 selection_bottom = 0;
    bool emit_sprite_draws = false;
    bool clear_queue_after_flush = false;
};

// FUN_004e030b dispatches the selected-info record (0x1a6) before the command
// records which follow it.  Its HP/name/progress draws complete at that record
// boundary.  The record itself draws portrait/text before its progress frame
// and fill.  Keep the deferred diagnostic streams, but flush only the suffix
// created by this record so later command icons retain the original z-order.
template <typename DrawRecord, typename FlushText, typename DrawProgressFrame,
    typename FlushProgress>
void DrawUiOverlayRecordAndFlushSuffixCore(UiOverlayState& state,
    DrawRecord draw_record, FlushText flush_text,
    DrawProgressFrame draw_progress_frame, FlushProgress flush_progress) {
    const std::size_t first_progress = state.progress_commands.size();
    const std::size_t first_text = state.text_commands.size();
    draw_record(state);
    if (!state.emit_sprite_draws) {
        return;
    }

    flush_text(state, first_text);
    draw_progress_frame(state);
    flush_progress(state, first_progress);
    state.progress_command_flushed.resize(state.progress_commands.size(), 0);
    std::fill(state.progress_command_flushed.begin() + first_progress,
        state.progress_command_flushed.end(), 1);
    state.text_command_flushed.resize(state.text_commands.size(), 0);
    std::fill(state.text_command_flushed.begin() + first_text,
        state.text_command_flushed.end(), 1);
}

UiOverlayState& ui_overlay_state();
void SetUiOverlayChatSubmitCallback(UiOverlayChatSubmitCallback callback);
void ResetUiOverlayState();
void ResetUiOverlayStatePreservingSessionCamera();
void ResetUiOverlayDrawQueue(UiOverlayState& state);
void InstallDefaultUiOverlayDispatchHandlers(UiOverlayState& state);

void RenderGameplayWorldAndUiOverlay(UiOverlayState& state);
void DrawGameplaySelectionRectangleOverlay(UiOverlayState& state);
void FlushUiOverlayDrawQueue(UiOverlayState& state);
bool DispatchUiOverlayDrawRecord(UiOverlayState& state, const UiOverlayDrawRecord& record);

void QueueProductionActionDynamicIconRecord(UiOverlayState& state, u32 object_id,
    u32 aux, u32 flags);
void QueueProductionOrderDynamicIconRecord(UiOverlayState& state, u32 object_id,
    u32 aux, u32 flags);
void QueueEquipmentDefinitionDynamicIconRecord(UiOverlayState& state, u32 object_id,
    u32 aux, u32 flags);
void QueueUiOverlayManual26Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags, i32 x, i32 y);
void QueueUiOverlayManual26RecordAlternate(UiOverlayState& state, u32 item_id,
    u32 aux, u32 flags, i32 x, i32 y);
void QueueUiOverlayDynamicIconRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlaySmallSlot1Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlaySmallSlot2Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlaySmallSlot3Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlayLargeSlot0Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlayLargeSlot3Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlayLargeSlot6Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlayLargeSlot9Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlayLargeSlot12Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlayWideSlotRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlaySideSlotRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags);
void QueueUiOverlayIndexedSlotRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags, u32 rect_index);
bool DrawUiOverlaySmallUnitIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record);
bool DrawUiOverlayLargeUnitIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record);
bool DrawUiOverlayObjectIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record);
bool DrawUiOverlayUnitOrObjectIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record);
bool DrawUiOverlayEquipmentIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record);
bool DrawUiOverlayProductionIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record);
void BlitUiOverlayBaseIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y);
void BlitUiOverlayPaletteTableIcon(UiOverlayState& state, u32 palette_selector,
    u32 item_id, i32 x, i32 y);
void BlitUiOverlayUnitIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y);
void BlitUiOverlayObjectIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y);
void BlitUiOverlayEquipmentIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y);
void BlitUiOverlayEquipmentIconHalfSampled(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayProductionIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y);
void BlitUiOverlayUnitIconClipped(UiOverlayState& state, u32 item_id, i32 x, i32 y);
void BlitUiOverlayBaseIconClipped(UiOverlayState& state, u32 item_id, i32 x, i32 y);
void BlitUiOverlayEquipmentIconClipped(UiOverlayState& state, u32 item_id, i32 x,
    i32 y);
void BlitUiOverlayProductionIconClipped(UiOverlayState& state, u32 item_id, i32 x,
    i32 y);
void BlitUiOverlayDisabledBaseIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayDisabledUnitIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayDisabledObjectIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayDisabledProductionIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayMaskedDisabledBaseIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayMaskedDisabledUnitIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayMaskedDisabledObjectIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void BlitUiOverlayMaskedDisabledProductionIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y);
void DrawUiLargeSlotDetailTexts(UiOverlayState& state);
void NoOpUiOverlayDetailHandler(UiOverlayState& state);
void RenderSelectedUnitInfoPanel(UiOverlayState& state);
void RenderSelectedUnitMaxStatText(UiOverlayState& state);
void RenderSelectedUnitCapabilityLines(UiOverlayState& state);
void RenderSelectedUnitCargoLine(UiOverlayState& state);
void RenderSelectedUnitBaseStatLine(UiOverlayState& state);
void RenderSelectedUnitOrderStatLine(UiOverlayState& state);
void DrawUiOverlayIconTextGlyphBase(UiOverlayState& state);
void DrawUiOverlayIconTextGlyphProduction(UiOverlayState& state);
void DrawUiOverlayIconTextGlyphEquipment(UiOverlayState& state);
void DrawUiOverlayIndexedIconNumber(UiOverlayState& state, u32 item_id);
void DrawUiOverlaySelectedUnitSlotNumber(UiOverlayState& state);
void DrawEquipmentDefinitionSpriteIfAvailable(UiOverlayState& state, u32 item_id);
bool ResolveUiOverlayPlacementGridCoordinates(i32 pointer_x, i32 pointer_y,
    i32 camera_x, i32 camera_y, UiOverlayPlacementGridCoordinates& coordinates);
void RenderProductionPlacementPreviewOverlay(UiOverlayState& state);
void RenderGameplayMinimapOverlay(UiOverlayState& state);
void RenderMinimapObjectAndTerrainMarkers(UiOverlayState& state);
void RenderMinimapFogMask(UiOverlayState& state);
void FillMinimapFootprintMarker(UiOverlayState& state, u32 definition_id,
    i32 x, i32 y, u16 color);
void RenderMinimapUnitMarkers(UiOverlayState& state);
void DrawUiOverlayRectangleOutline(UiOverlayState& state, i32 left, i32 top,
    i32 width, i32 height, u16 color = 0xffff);
void ClampCameraToMinimapPoint(UiOverlayState& state, i32 world_x, i32 world_y);
void RenderGameplayResourceCounters(UiOverlayState& state);
void StartGameplayHudPulse(UiOverlayState& state, i32 world_x, i32 world_y, u32 tick_ms);
void StopGameplayHudPulse(UiOverlayState& state);
void RenderGameplayHudPulse(UiOverlayState& state, u32 tick_ms);
i32 ResolveUiOverlayInterfaceTop(const UiOverlayState& state);
void ConfigureGameplayUiOverlayLayout(UiOverlayState& state);
void ResetUiOverlayCommandPanelState(UiOverlayState& state);
bool HitTestUiOverlayHotRegion(UiOverlayState& state, i32 x, i32 y);
bool CheckUiOverlayCommandRecordEnabled(const UiOverlayState& state, u32 item_id);
u32 ResolveUiOverlayHotkeyCommand(UiOverlayState& state, u8 key);
bool HitTestUiOverlayHotRegionFromPointer(UiOverlayState& state, i32 x, i32 y);
bool CheckUiOverlayIconMaskPixel(const UiOverlayState& state, i32 x, i32 y);
void BuildSelectedUnitCommandPanel(UiOverlayState& state);
void QueueDefaultGameplayCommandSlots(UiOverlayState& state);
void BuildMultiSelectedUnitCommandPanel(UiOverlayState& state);
void QueueAvailableProductionClassButtons(UiOverlayState& state);
void QueueProductionClassButtonsForSelectedUnit(UiOverlayState& state, u32 category);
void BuildSingleSelectedUnitCommandPanel(UiOverlayState& state);
bool IsUiOverlayAvatarProductionStructureType(u32 type_id);
bool MatchesUiOverlayAvatarAttachmentSlot(const UnitMovementUnit& unit,
    u32 owner_id, u32 slot_id);
bool MatchesUiOverlayAvatarProducerQueueSlot(const UnitMovementUnit& unit,
    u32 owner_id, u32 slot_id);
u32 ResolveUiOverlaySelectedUnitProgressValue(u32 command_state,
    u32 action_mode_gate, u32 action_mode, u32 animation_frame, u32 work_timer);
u32 ResolveUiOverlayConstructionProgressTotal(u32 production_spawn_time);
bool FindOwnerTransportAttachmentUnit(const UiOverlayState& state, u32 owner_id,
    u32 attachment_mask);
bool FindOwnerCarrierLinkedToSlot(const UiOverlayState& state, u32 owner_id,
    u32 slot_id);
void PadUiOverlayLargeCommandSlots(UiOverlayState& state);
void QueueSelectedUnitCurrentOrderButtons(UiOverlayState& state);
void CollectSelectedUnitProductionActionMasks(UiOverlayState& state);
u32 CheckGroupedMorphCommandDisabled(const UiOverlayState& state,
    u32 command_type, u32 owner_id);
void QueueUiOverlayCommandRecordByItemId(UiOverlayState& state, u32 item_id,
    u32 aux = 0, u32 flags = 0);
void QueueUnitDefinitionDynamicIconOrHotRegion(UiOverlayState& state, u32 item_id,
    u32 aux = 0, u32 flags = 0);
void QueueObjectDefinitionDynamicIconOrHotRegion(UiOverlayState& state, u32 item_id,
    u32 aux = 0, u32 flags = 0);
void QueueProductionDefinitionCommandSlot(UiOverlayState& state, u32 item_id,
    u32 aux = 0, u32 flags = 0);
void AppendUiOverlayCommandSlot(UiOverlayState& state, u32 item_id, u32 aux = 0,
    u32 flags = 0, u32 icon_marker = 0);
void QueueEquipmentDefinitionCommandSlot(UiOverlayState& state, u32 item_id,
    u32 aux = 0, u32 flags = 0);
void DispatchGameplayUiKeyboardInput(UiOverlayState& state, u32 legacy_scan_code,
    u8 ascii = 0);
void ClampCameraToStoredMinimapPoint(UiOverlayState& state, i32 world_x, i32 world_y,
    bool valid);
void IncreaseGameplaySpeed(UiOverlayState& state);
void DecreaseGameplaySpeed(UiOverlayState& state);
void ToggleMinimapModeAndPersistSetup(UiOverlayState& state);
void CancelCurrentUiModeOrActivateCommand(UiOverlayState& state, u32 command_id = 0);
void DecrementSelectedUnitCooldownTimers(UiOverlayState& state);
void RecallOrStoreCameraBookmark(UiOverlayState& state, u32 bookmark_index,
    bool store);
void HandleGameplayMenuKey3c(UiOverlayState& state, u32 bookmark_index, bool store);
void HandleGameplayMenuKey3d(UiOverlayState& state, u32 bookmark_index, bool store);
void OpenGameplayMenuKey3e(UiOverlayState& state);
void OpenGameplayPauseMenuKey44(UiOverlayState& state);
void ToggleGameplayOverlayFlag(UiOverlayState& state);
void SelectControlGroupFromDigit(UiOverlayState& state, u32 group);
void AssignSelectedUnitsToControlGroup(UiOverlayState& state, u32 group);
void SelectUnitsInControlGroup(UiOverlayState& state, u32 group);
void CycleSelectedControlGroup(UiOverlayState& state);
void FocusCameraOnSelectedUnitsBounds(UiOverlayState& state);
void ActivateCommandHotkey(UiOverlayState& state, u8 key);
void BeginGameplayChatInput(UiOverlayState& state);
void HandleGameplayChatKey(UiOverlayState& state, u8 key);
void QueueGameplayChatMessageDisplay(UiOverlayState& state, const std::string& text,
    u32 channel, bool local_echo);
void HandleGameplayChatBangCommand(UiOverlayState& state);
bool DispatchGameplayChatSlashCommand(UiOverlayState& state, const std::string& text);
void ScrollCameraLeft(UiOverlayState& state);
void ScrollCameraRight(UiOverlayState& state);
void ScrollCameraUp(UiOverlayState& state);
void ScrollCameraDown(UiOverlayState& state);
void UpdateCameraScrollRamp(UiOverlayState& state);
u32 ResolveCameraScrollStep(UiOverlayState& state);
void IncreaseCameraScrollRamp(UiOverlayState& state);
void DecreaseCameraScrollRamp(UiOverlayState& state);
void ResetCameraBookmarks(UiOverlayState& state);
void SeedInitialCameraBookmark(UiOverlayState& state);
void UpdateGameplayHoverContextAndTooltip(UiOverlayState& state);
void ResolveGameplayHoverContext(UiOverlayState& state);
bool FindUnitUnderMousePointer(UiOverlayState& state, i32 screen_x, i32 screen_y);
bool FindMapEffectUnderMousePointer(UiOverlayState& state, i32 screen_x, i32 screen_y);
void NoOpHoverContextHandler(UiOverlayState& state);
bool FindUnitUnderStoredPointer(UiOverlayState& state);
bool FindFreeUnitUnderStoredPointer(UiOverlayState& state);
void HandleGameplayPointerActionFrame(UiOverlayState& state);
void DispatchGameplayPointerUnitCommand(UiOverlayState& state, u32 command_id);
void DispatchGameplayPointerTerrainCommand(UiOverlayState& state, u32 command_id);
void NoOpGameplayPointerCommandHandler(UiOverlayState& state);
void UpdateCameraFromMinimapDrag(UiOverlayState& state);
void ScrollCameraFromEdgeOrKeys(UiOverlayState& state);
void BeginUiCommandButtonPress(UiOverlayState& state);
void ReleaseUiCommandButtonPress(UiOverlayState& state);
void BeginUiCommandButtonHold(UiOverlayState& state);
void ReleaseUiCommandButtonHold(UiOverlayState& state);
bool CheckPointerInsideMinimapAndPlacementMode(UiOverlayState& state);
bool CheckPointerInsideMinimapForAction(UiOverlayState& state);
bool CheckMouseInsideMinimap(UiOverlayState& state);
bool TryBeginUiOverlayBuildingPlacement(UiOverlayState& state,
    u32 building_type_id, bool requirements_met);
void DispatchUiOverlayCommandAction(UiOverlayState& state, u32 item_id);
void DispatchUiOverlayHeldCommandAction(UiOverlayState& state, u32 item_id);
void ResetGameplaySelectionState(UiOverlayState& state);
void RecountGameplaySelectedUnits(UiOverlayState& state);
void NotifyPrimaryGameplayUnitSelected(UiOverlayState& state);
bool ClearSelectedUnitMembershipFlagAndRefreshSelection(UiOverlayState& state,
    u32 unit_id);
UiOverlaySelectionRectScanResult ScanVisibleUnitsInSelectionRect(
    UiOverlayState& state);
void SelectUnitsInDragRectangle(UiOverlayState& state);
void ResolveGameplayClickSelection(UiOverlayState& state);
UiOverlayDoubleClickSelectionResult ResolveGameplayDoubleClickSelection(
    UiOverlayState& state);
void AddUnitsInDragRectangleToSelection(UiOverlayState& state);
void AddLocalUnitsInDragRectangleWithModifiers(UiOverlayState& state);
bool FindSelectableUnitInDragRectangle(UiOverlayState& state);
void ToggleUnitSelectionState(UiOverlayState& state);
bool CheckScenarioSelectionOverride(const UiOverlayState& state);

bool DrawUiSmallSlot1(u32 context_id);
bool DrawUiSmallSlot2(u32 context_id);
bool DrawUiSmallSlot3(u32 context_id);
bool DrawUiLargeSlot0(u32 context_id);
bool DrawUiLargeSlot3(u32 context_id);
bool DrawUiLargeSlot0AndDetails(u32 context_id);
bool DrawUiLargeSlot3AndDetails(u32 context_id);
bool DrawUiLargeSlot6(u32 context_id, u32 flags);
bool DrawUiLargeSlot9(u32 context_id, u32 flags);
bool DrawUiLargeSlot12(u32 flags);
bool DrawUiLargeSlot15(u32 flags);

bool DrawUiEncodedGlyphRun11px(const char* text, std::size_t count, i32 slot_x, i32 slot_y);
bool DrawUiEncodedDigitRun9px(const char* text, std::size_t count, i32 slot_x, i32 slot_y);
bool DrawUiGlyphRun(const char* text, std::size_t count, i32 right_aligned_x, i32 y,
    i32 advance, u32 resource_base, u8 first_encoded_char);

}
