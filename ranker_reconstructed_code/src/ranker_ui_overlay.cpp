#include "ranker_ui_overlay.h"

#include "ranker_gameplay_tooltips.h"
#include "ranker_palette_cache.h"
#include "ranker_runtime_resources.h"
#include "ranker_sprite_renderer.h"
#include "ranker_text_renderer.h"
#include "ranker_ui_screen.h"
#include "ranker_unit_movement.h"

#include <algorithm>
#include <utility>

namespace ranker {
namespace {

UiOverlayState g_ui_overlay_state;

constexpr u32 kUiOverlayFlagHidden = 0x01;
constexpr u32 kUiOverlayFlagDisabled = 0x02;
constexpr u32 kUiOverlayFlagAlternateA = 0x04;
constexpr u32 kUiOverlayFlagAlternateB = 0x10;
constexpr u32 kUiOverlayFlagAlternateC = 0x20;

constexpr u32 kPointerPress = 0x02;
constexpr u32 kPointerRelease = 0x04;
constexpr u32 kPointerHoldPress = 0x08;
constexpr u32 kPointerHoldRelease = 0x10;
constexpr u32 kPointerDrag = 0x20;
constexpr u32 kPointerPromoteHold = 0x40;
constexpr u32 kPointerMenuAction = 0x80;
constexpr u32 kPointerMinimapDrag = 0x100;

constexpr u32 kCommandActionClick = 1;
constexpr u32 kCommandActionHold = 2;
constexpr u32 kCommandActionMinimap = 3;
constexpr u32 kCommandActionPlacement = 4;
constexpr u32 kCommandActionSelection = 5;
constexpr u32 kCommandActionContextual = 6;
constexpr u32 kProductionGateFailureOwnerRequirement = 1;
constexpr u32 kProductionGateFailureActiveLimit = 3;
constexpr u32 kProductionGateFailureResourceLimit = 4;
constexpr u32 kProductionGateFlagQueuedOrOther = 0x04;
constexpr u32 kProductionGateFlagResourceLimit = 0x10;
constexpr u32 kProductionGateFlagOwnerOrActiveLimit = 0x02;
constexpr u32 kCommandIconFrameSmall = 0x26;
constexpr u32 kCommandIconFrameLarge = 0x32;

constexpr std::array<UiOverlayRect, 7> kDefaultManualEquipmentSlotBounds{{
    {266, 523, 0x26, 0x26},
    {175, 570, 0x13, 0x13},
    {197, 570, 0x13, 0x13},
    {223, 570, 0x13, 0x13},
    {245, 570, 0x13, 0x13},
    {267, 570, 0x13, 0x13},
    {289, 570, 0x13, 0x13},
}};

struct RawCommandIconSource {
    const std::vector<u8>* frames = nullptr;
    const u16* palette = nullptr;
    u32 frame_index = 0;
    u32 frame_width = kCommandIconFrameSmall;
    u32 frame_height = kCommandIconFrameSmall;
    bool half_sampled = false;
    bool mask_palette = false;
};

u32 selected_increment(u32 context_id) {
    return context_id == g_ui_overlay_state.selected_context_id ? 1u : 0u;
}

bool draw_small_slot(u32 context_id, u32 base_offset, i32 x, i32 y) {
    if (g_ui_overlay_state.small_icon_resource_base == kInvalidResourceEntry) {
        return false;
    }
    return DrawResourceSpriteNormal(
        g_ui_overlay_state.small_icon_resource_base + base_offset + selected_increment(context_id),
        x, y);
}

u32 flag_or_selected_offset(u32 context_id, u32 flags) {
    if ((flags & 2u) != 0) {
        return 2;
    }
    return selected_increment(context_id);
}

bool draw_large_slot(u32 entry_offset, i32 x, i32 y) {
    if (g_ui_overlay_state.large_icon_resource_base == kInvalidResourceEntry) {
        return false;
    }
    return DrawResourceSpriteNormal(g_ui_overlay_state.large_icon_resource_base + entry_offset, x, y);
}

void frame_callback(UiOverlayState& state, UiOverlayFrameCallback callback) {
    if (callback != nullptr) {
        callback(state);
    }
}

UiOverlayDrawRecord make_record(
    u32 item_id, u32 aux, u32 flags, const UiOverlayRect& rect, u32 icon_marker) {
    UiOverlayDrawRecord record{};
    record.item_id = item_id;
    record.aux = aux;
    record.flags = flags;
    record.x = rect.x;
    record.y = rect.y;
    record.width = rect.width;
    record.height = rect.height;
    record.icon_marker = icon_marker;
    return record;
}

void append_record(UiOverlayState& state, const UiOverlayDrawRecord& record) {
    state.queued_records.push_back(record);
}

UiOverlayRect full_screen_rect(const UiOverlayState& state) {
    return {0, 0, state.screen_width, state.screen_height};
}

UiOverlayRect rect_or_full_screen(
    const UiOverlayState& state, const std::array<UiOverlayRect,
    kUiOverlayDynamicIconRectCount>& rects, u32 index) {
    if (index < rects.size() && (rects[index].width != 0 || rects[index].height != 0)) {
        return rects[index];
    }
    return full_screen_rect(state);
}

UiOverlayRect rect_or_default(const UiOverlayRect& rect, u32 width, u32 height) {
    if (rect.width != 0 || rect.height != 0) {
        return rect;
    }
    return {rect.x, rect.y, width, height};
}

u32 vector_value_or_zero(const std::vector<u32>& values, u32 index) {
    return index < values.size() ? values[index] : 0;
}

bool is_indexed_queue_command_item(u32 item_id) {
    return item_id >= 0x1aau && item_id <= 0x1acu;
}

bool record_visible(const UiOverlayState& state, const UiOverlayDrawRecord& record) {
    // FUN_004e5cca stores the production-queue logical index (0..4) in the
    // record's third dword.  For 0x1aa..0x1ac that value is not the ordinary
    // hidden/disabled flag word.
    return (is_indexed_queue_command_item(record.item_id) ||
               (record.flags & kUiOverlayFlagHidden) == 0) &&
        record.x < static_cast<i32>(state.screen_width) &&
        record.y < static_cast<i32>(state.screen_height);
}

UiOverlayDrawRecord selected_adjusted_record(
    const UiOverlayState& state, const UiOverlayDrawRecord& record, bool match_aux) {
    UiOverlayDrawRecord adjusted = record;
    const bool pressed = state.command_button_press_active &&
        record.item_id == state.pressed_command_id &&
        (!match_aux || record.aux == state.pressed_command_aux);
    const bool selected = !pressed && record.item_id == state.selected_context_id &&
        (!match_aux || record.aux == state.alternate_slot_a);
    if (pressed || selected) {
        ++adjusted.x;
        ++adjusted.y;
    }
    return adjusted;
}

u32 marker_sprite_entry(const UiOverlayState& state, u32 marker_code);
bool draw_record_marker_overlay(UiOverlayState& state, const UiOverlayDrawRecord& record);

bool draw_record_sprite(UiOverlayState& state, const UiOverlayDrawRecord& record,
    u32 entry_id, bool direct_shadow) {
    bool ok = true;
    if (state.emit_sprite_draws) {
        ok = direct_shadow ?
            DrawResourceSpriteDirectToken1Shadow(entry_id, record.x, record.y) :
            DrawResourceSpriteNormal(entry_id, record.x, record.y);
    }
    ok = draw_record_marker_overlay(state, record) && ok;
    return ok;
}

u32 marker_sprite_entry(const UiOverlayState& state, u32 marker_code) {
    if (marker_code == 0) {
        return kInvalidResourceEntry;
    }
    if (marker_code > 0xffu) {
        return marker_code;
    }
    if (state.marker_resource_base == kInvalidResourceEntry ||
        marker_code < static_cast<u32>('0')) {
        return kInvalidResourceEntry;
    }
    return state.marker_resource_base + marker_code - static_cast<u32>('0');
}

bool draw_record_marker_overlay(
    UiOverlayState& state, const UiOverlayDrawRecord& record) {
    if (!state.emit_sprite_draws || record.icon_marker == 0) {
        return true;
    }
    const u32 entry = marker_sprite_entry(state, record.icon_marker);
    if (entry == kInvalidResourceEntry) {
        return false;
    }
    const u32 size = record.width != 0 ? record.width : 0x26u;
    const i32 x = record.x + static_cast<i32>(size) - 0x0b;
    const i32 y = record.y + static_cast<i32>(size) - 0x0b;
    return DrawResourceSpriteDirectToken1Shadow(entry, x, y);
}

bool icon_blit_kind_uses_unit_resource(UiOverlayIconBlitKind kind) {
    return kind == UiOverlayIconBlitKind::unit ||
        kind == UiOverlayIconBlitKind::unit_clipped ||
        kind == UiOverlayIconBlitKind::disabled_unit_clipped ||
        kind == UiOverlayIconBlitKind::masked_disabled_unit;
}

bool icon_blit_kind_uses_equipment_source(UiOverlayIconBlitKind kind) {
    return kind == UiOverlayIconBlitKind::equipment ||
        kind == UiOverlayIconBlitKind::equipment_half_sampled ||
        kind == UiOverlayIconBlitKind::equipment_clipped;
}

bool icon_blit_kind_uses_command_source(UiOverlayIconBlitKind kind) {
    switch (kind) {
    case UiOverlayIconBlitKind::base:
    case UiOverlayIconBlitKind::palette_table:
    case UiOverlayIconBlitKind::unit:
    case UiOverlayIconBlitKind::object:
    case UiOverlayIconBlitKind::equipment:
    case UiOverlayIconBlitKind::equipment_half_sampled:
    case UiOverlayIconBlitKind::production:
    case UiOverlayIconBlitKind::unit_clipped:
    case UiOverlayIconBlitKind::base_clipped:
    case UiOverlayIconBlitKind::equipment_clipped:
    case UiOverlayIconBlitKind::production_clipped:
    case UiOverlayIconBlitKind::disabled_base_clipped:
    case UiOverlayIconBlitKind::disabled_unit_clipped:
    case UiOverlayIconBlitKind::disabled_object_clipped:
    case UiOverlayIconBlitKind::disabled_production_clipped:
    case UiOverlayIconBlitKind::masked_disabled_base:
    case UiOverlayIconBlitKind::masked_disabled_unit:
    case UiOverlayIconBlitKind::masked_disabled_object:
    case UiOverlayIconBlitKind::masked_disabled_production:
        return true;
    default:
        return false;
    }
}

u32 resolve_icon_blit_entry(UiOverlayIconBlitKind kind, u32 item_id) {
    if (item_id == kInvalidResourceEntry) {
        return kInvalidResourceEntry;
    }
    if (icon_blit_kind_uses_unit_resource(kind)) {
        const u32 entry = GetUnitDefinitionImageResourceEntry(item_id, 0);
        if (entry != kInvalidResourceEntry) {
            return entry;
        }
    }
    return item_id;
}

u32 resolve_equipment_icon_frame_index(const UiOverlayState& state, u32 item_id) {
    const u32 equipment_id = item_id >= 0x24a ? item_id - 0x24a : item_id;
    if (equipment_id >= state.equipment_icon_frame_indices.size()) {
        return kInvalidResourceEntry;
    }
    return state.equipment_icon_frame_indices[equipment_id];
}

const PaletteSlotRef& command_palette_ref(
    const CommandThemeResourceState& resources, CommandPaletteKind kind, bool disabled) {
    const auto index = static_cast<std::size_t>(kind);
    return disabled ? resources.grayscale_palettes[index] : resources.palettes[index];
}

bool set_base_command_icon_source(const CommandThemeResourceState& resources,
    const UiOverlayIconBlitRequest& request, RawCommandIconSource& source) {
    const bool large = request.width == kCommandIconFrameLarge ||
        request.height == kCommandIconFrameLarge;
    const CommandBlobKind blob_kind = large ?
        CommandBlobKind::MiddleCharacterTable : CommandBlobKind::SmallCharacterTable;
    source.frames = &resources.blobs[static_cast<std::size_t>(blob_kind)];
    source.frame_width = large ? kCommandIconFrameLarge : kCommandIconFrameSmall;
    source.frame_height = source.frame_width;
    return true;
}

const u16* masked_or_disabled_palette(const CommandThemeResourceState& resources,
    CommandPaletteKind kind, bool disabled, bool masked, bool& mask_palette) {
    mask_palette = masked;
    return command_palette_ref(resources, kind, disabled && !masked).pixels;
}

bool resolve_raw_command_icon_source(UiOverlayState& state,
    const UiOverlayIconBlitRequest& request, RawCommandIconSource& source) {
    const CommandThemeResourceState& resources = command_theme_resource_state();
    source.frame_index = request.item_id;
    switch (request.kind) {
    case UiOverlayIconBlitKind::base:
    case UiOverlayIconBlitKind::base_clipped:
    case UiOverlayIconBlitKind::disabled_base_clipped:
    case UiOverlayIconBlitKind::masked_disabled_base:
        set_base_command_icon_source(resources, request, source);
        source.palette = masked_or_disabled_palette(resources,
            CommandPaletteKind::SmallCharacter, request.disabled,
            request.masked_palette, source.mask_palette);
        return true;
    case UiOverlayIconBlitKind::palette_table:
        set_base_command_icon_source(resources, request, source);
        if (request.palette_selector >= resources.red_adjusted_palettes.size()) {
            return false;
        }
        source.palette =
            resources.red_adjusted_palettes[request.palette_selector].pixels;
        return true;
    case UiOverlayIconBlitKind::unit:
    case UiOverlayIconBlitKind::unit_clipped:
    case UiOverlayIconBlitKind::disabled_unit_clipped:
    case UiOverlayIconBlitKind::masked_disabled_unit:
        source.frames =
            &resources.blobs[static_cast<std::size_t>(CommandBlobKind::ActionTable)];
        source.palette = masked_or_disabled_palette(resources,
            CommandPaletteKind::Action, request.disabled, request.masked_palette,
            source.mask_palette);
        return true;
    case UiOverlayIconBlitKind::object:
    case UiOverlayIconBlitKind::disabled_object_clipped:
    case UiOverlayIconBlitKind::masked_disabled_object:
        source.frames =
            &resources.blobs[static_cast<std::size_t>(CommandBlobKind::MagicTable)];
        source.palette = masked_or_disabled_palette(resources,
            CommandPaletteKind::Magic, request.disabled, request.masked_palette,
            source.mask_palette);
        return true;
    case UiOverlayIconBlitKind::production:
    case UiOverlayIconBlitKind::production_clipped:
    case UiOverlayIconBlitKind::disabled_production_clipped:
    case UiOverlayIconBlitKind::masked_disabled_production:
        source.frames =
            &resources.blobs[static_cast<std::size_t>(CommandBlobKind::UpgradeTable)];
        source.palette = masked_or_disabled_palette(resources,
            CommandPaletteKind::Upgrade, request.disabled, request.masked_palette,
            source.mask_palette);
        return true;
    case UiOverlayIconBlitKind::equipment:
    case UiOverlayIconBlitKind::equipment_half_sampled:
    case UiOverlayIconBlitKind::equipment_clipped:
        source.frames =
            &resources.blobs[static_cast<std::size_t>(CommandBlobKind::ItemTable)];
        source.palette =
            resources.palettes[static_cast<std::size_t>(CommandPaletteKind::Item)].pixels;
        source.frame_index = resolve_equipment_icon_frame_index(state, request.item_id);
        source.half_sampled = request.half_sampled;
        return source.frame_index != kInvalidResourceEntry;
    default:
        return false;
    }
}

bool draw_raw_command_icon_blit(
    UiOverlayState& state, const UiOverlayIconBlitRequest& request) {
    RawCommandIconSource source{};
    if (!resolve_raw_command_icon_source(state, request, source)) {
        return false;
    }
    const SpriteRenderTarget& target = sprite_render_state().target;
    if (source.frames == nullptr || target.pixels == nullptr || target.width == 0 ||
        target.height == 0 || target.stride_words == 0 || source.palette == nullptr) {
        return false;
    }

    const std::size_t frame_bytes =
        static_cast<std::size_t>(source.frame_width) * source.frame_height;
    const std::size_t frame_offset =
        static_cast<std::size_t>(source.frame_index) * frame_bytes;
    if (source.frame_index == kInvalidResourceEntry ||
        frame_offset + frame_bytes > source.frames->size()) {
        return false;
    }

    const u8* frame = source.frames->data() + frame_offset;
    const u32 draw_width = source.half_sampled ? 0x13u : source.frame_width;
    const u32 draw_height = source.half_sampled ? 0x13u : source.frame_height;
    const u16 mask = source.mask_palette ? SurfaceRedMask() : 0;
    for (u32 row = 0; row < draw_height; ++row) {
        const i32 target_y = request.y + static_cast<i32>(row);
        if (target_y < 0 || target_y >= static_cast<i32>(target.height)) {
            continue;
        }
        const u32 source_y = source.half_sampled ? row * 2u : row;
        for (u32 col = 0; col < draw_width; ++col) {
            const i32 target_x = request.x + static_cast<i32>(col);
            if (target_x < 0 || target_x >= static_cast<i32>(target.width)) {
                continue;
            }
            const u32 source_x = source.half_sampled ? col * 2u : col;
            const u8 index =
                frame[source_y * source.frame_width + source_x];
            // The fixed 0x26 command-icon blitters write every palette entry,
            // including index zero.  Masked variants also OR the red mask into
            // that background entry; command icons are not sprite-transparent.
            target.pixels[static_cast<std::size_t>(target_y) * target.stride_words +
                static_cast<std::size_t>(target_x)] =
                static_cast<u16>(source.palette[index] | mask);
        }
    }

    return true;
}

bool draw_icon_blit_fallback(UiOverlayState& state, const UiOverlayIconBlitRequest& request) {
    if (icon_blit_kind_uses_command_source(request.kind) &&
        draw_raw_command_icon_blit(state, request)) {
        return true;
    }
    if (icon_blit_kind_uses_equipment_source(request.kind)) {
        return false;
    }
    const u32 entry_id = resolve_icon_blit_entry(request.kind, request.item_id);
    if (entry_id == kInvalidResourceEntry) {
        return false;
    }
    return request.disabled || request.masked_palette ?
        DrawResourceSpriteDirectToken1Shadow(entry_id, request.x, request.y) :
        DrawResourceSpriteNormal(entry_id, request.x, request.y);
}

bool draw_record_command_icon_blit(UiOverlayState& state,
    const UiOverlayDrawRecord& record, UiOverlayIconBlitKind kind, u32 item_id,
    bool disabled = false, bool masked = false) {
    if (!state.emit_sprite_draws) {
        return true;
    }

    UiOverlayIconBlitRequest request{};
    request.kind = kind;
    request.item_id = item_id;
    request.x = record.x;
    request.y = record.y;
    request.width = record.width;
    request.height = record.height;
    request.clipped = true;
    request.disabled = disabled;
    request.masked_palette = masked;

    bool ok = draw_raw_command_icon_blit(state, request);
    // Disabled object/production/base/unit paths tail-jump from their disabled
    // blitter and skip the lower-right marker entirely.  Masked (red) icons
    // are not that disabled branch and retain their marker.
    if (!disabled) {
        ok = draw_record_marker_overlay(state, record) && ok;
    }
    return ok;
}

void append_icon_blit(UiOverlayState& state, UiOverlayIconBlitKind kind,
    u32 item_id, i32 x, i32 y, u32 width, u32 height, u32 palette_selector,
    bool clipped, bool disabled, bool half_sampled, bool masked_palette) {
    UiOverlayIconBlitRequest request{};
    request.kind = kind;
    request.item_id = item_id;
    request.x = x;
    request.y = y;
    request.width = width;
    request.height = height;
    request.palette_selector = palette_selector;
    request.clipped = clipped;
    request.disabled = disabled;
    request.half_sampled = half_sampled;
    request.masked_palette = masked_palette;
    state.icon_blit_requests.push_back(request);
    if (state.emit_sprite_draws) {
        draw_icon_blit_fallback(state, request);
    }
}

void append_icon_blit(UiOverlayState& state, UiOverlayIconBlitKind kind,
    u32 item_id, i32 x, i32 y) {
    const bool clipped = kind == UiOverlayIconBlitKind::unit_clipped ||
        kind == UiOverlayIconBlitKind::base_clipped ||
        kind == UiOverlayIconBlitKind::equipment_clipped ||
        kind == UiOverlayIconBlitKind::production_clipped ||
        kind == UiOverlayIconBlitKind::disabled_base_clipped ||
        kind == UiOverlayIconBlitKind::disabled_unit_clipped ||
        kind == UiOverlayIconBlitKind::disabled_object_clipped ||
        kind == UiOverlayIconBlitKind::disabled_production_clipped;
    const bool disabled = kind == UiOverlayIconBlitKind::disabled_base_clipped ||
        kind == UiOverlayIconBlitKind::disabled_unit_clipped ||
        kind == UiOverlayIconBlitKind::disabled_object_clipped ||
        kind == UiOverlayIconBlitKind::disabled_production_clipped ||
        kind == UiOverlayIconBlitKind::masked_disabled_base ||
        kind == UiOverlayIconBlitKind::masked_disabled_unit ||
        kind == UiOverlayIconBlitKind::masked_disabled_object ||
        kind == UiOverlayIconBlitKind::masked_disabled_production;
    const bool masked = kind == UiOverlayIconBlitKind::masked_disabled_base ||
        kind == UiOverlayIconBlitKind::masked_disabled_unit ||
        kind == UiOverlayIconBlitKind::masked_disabled_object ||
        kind == UiOverlayIconBlitKind::masked_disabled_production;
    const bool half = kind == UiOverlayIconBlitKind::equipment_half_sampled;
    const u32 size = icon_blit_kind_uses_equipment_source(kind) ? 0x26 :
        (state.current_record_size == 0x32 ? 0x32 : 0x26);
    append_icon_blit(state, kind, item_id, x, y, half ? 0x13 : size,
        half ? 0x13 : size, state.current_palette_selector, clipped,
        disabled, half, masked);
}

void append_text(UiOverlayState& state, std::string text, i32 x, i32 y, u8 color,
    bool centered = false, bool right_aligned = false, bool bottom_aligned = false,
    u8 draw_font = 0, u8 metric_font = 0) {
    if (text.empty()) {
        return;
    }
    UiOverlayTextCommand command{};
    command.text = std::move(text);
    command.x = x;
    command.y = y;
    command.color = color;
    command.draw_font = draw_font;
    command.metric_font = metric_font;
    command.centered = centered;
    command.right_aligned = right_aligned;
    command.bottom_aligned = bottom_aligned;
    state.text_commands.push_back(std::move(command));
}

void append_progress(UiOverlayState& state, i32 left, i32 top, i32 right, i32 bottom,
    u32 numerator, u32 denominator) {
    UiOverlayProgressCommand command{};
    command.left = left;
    command.top = top;
    command.right = right;
    command.bottom = bottom;
    command.numerator = numerator;
    command.denominator = denominator;
    state.progress_commands.push_back(command);
}

void reset_frame_output_commands(UiOverlayState& state) {
    state.dispatched_records.clear();
    state.icon_blit_requests.clear();
    state.text_commands.clear();
    state.progress_commands.clear();
    state.minimap_markers.clear();
}

void flush_ui_overlay_text_commands(UiOverlayState& state) {
    if (!state.emit_sprite_draws) {
        return;
    }

    for (const UiOverlayTextCommand& command : state.text_commands) {
        if (command.text.empty()) {
            continue;
        }
        SelectTextDrawFont(command.draw_font);
        if (command.metric_font != kUiOverlayPreserveMetricFont) {
            SelectTextMetricFont(command.metric_font);
        }
        i32 x = command.x;
        i32 y = command.y;
        if ((command.right_aligned || command.bottom_aligned) &&
            MeasureTextExtent(command.text.c_str())) {
            const TextRendererState& renderer = text_renderer_state();
            if (command.right_aligned) {
                x -= static_cast<i32>(renderer.measured_width);
            }
            if (command.bottom_aligned) {
                y -= static_cast<i32>(renderer.measured_height);
            }
        }
        SetTextCursor(x, y, command.color);
        if (command.centered) {
            DrawCenterAlignedText(command.text.c_str());
        } else if (command.metric_font == kUiOverlayPreserveMetricFont) {
            // The draw-only HUD paths (FUN_004e2042 and the counters at
            // 0x004e2a86/ef/0x004e2b2d) call DrawTextGlyph or
            // RenderAsciiOnlyTextLine.  Both inspect only the draw font;
            // letting the preserved metric font select the generic DBCS path
            // incorrectly switches these small numerals to the Win32 font.
            RenderAsciiOnlyTextLine(command.text.c_str());
        } else {
            DrawTextString(command.text.c_str());
        }
    }
}

void flush_ui_overlay_progress_commands(UiOverlayState& state) {
    if (!state.emit_sprite_draws) {
        return;
    }

    for (const UiOverlayProgressCommand& command : state.progress_commands) {
        if (command.denominator == 0 || command.right < command.left ||
            command.bottom < command.top) {
            continue;
        }
        // FUN_004e1544 scales the endpoint span (right - left), then passes
        // left + fill as an inclusive endpoint to FUN_005083fd.
        const i32 width = command.right - command.left;
        const i32 filled_width = static_cast<i32>(
            (static_cast<u64>(width) * command.numerator) / command.denominator);
        if (filled_width <= 0) {
            continue;
        }
        // FUN_004e2bb7 derives this from the green channel mask:
        // ((mask >> 1) + (mask >> 2)) & mask.
        const u16 color = SurfacePixelMode555() ? 0x02e0u : 0x05e0u;
        DrawBackBufferStippledRectangle16(command.left, command.top,
            command.left + filled_width, command.bottom, color);
    }
}

void apply_minimap_marker_to_output(UiOverlayState& state,
    const UiOverlayMinimapMarker& marker) {
    if (marker.kind == UiOverlayMinimapMarkerKind::placement_preview) {
        return;
    }
    MinimapRenderState& minimap = state.minimap;
    if (minimap.output_pitch_pixels == 0 || marker.width == 0 ||
        marker.height == 0) {
        return;
    }

    const i32 right = marker.x + static_cast<i32>(marker.width);
    const i32 bottom = marker.y + static_cast<i32>(marker.height);
    if (right <= 0 || bottom <= 0) {
        return;
    }

    const u32 required_height =
        std::max<u32>(minimap.output_height_pixels,
            static_cast<u32>(std::max<i32>(bottom, 0)));
    const std::size_t required_size =
        static_cast<std::size_t>(minimap.output_pitch_pixels) *
        required_height;
    if (minimap.output_pixels.size() < required_size) {
        minimap.output_pixels.resize(required_size);
    }

    const i32 clipped_left = std::max<i32>(marker.x, 0);
    const i32 clipped_top = std::max<i32>(marker.y, 0);
    const i32 clipped_right = std::min<i32>(
        right, static_cast<i32>(minimap.output_pitch_pixels));
    const i32 clipped_bottom = std::min<i32>(
        bottom, static_cast<i32>(required_height));
    if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) {
        return;
    }

    for (i32 y = clipped_top; y < clipped_bottom; ++y) {
        const std::size_t row =
            static_cast<std::size_t>(y) * minimap.output_pitch_pixels;
        for (i32 x = clipped_left; x < clipped_right; ++x) {
            u16& pixel = minimap.output_pixels[row + static_cast<std::size_t>(x)];
            if (marker.kind == UiOverlayMinimapMarkerKind::fog_dimmed) {
                // FUN_004e26f3 halves an explored-but-not-current minimap
                // pixel with (pixel & DAT_01440000) >> 1.  OR-ing a red mask
                // brightened the stale area instead of dimming it.
                pixel = static_cast<u16>((pixel & marker.color) >> 1);
            } else {
                pixel = marker.color;
            }
        }
    }
}

void apply_minimap_markers_to_output(UiOverlayState& state) {
    for (const UiOverlayMinimapMarker& marker : state.minimap_markers) {
        apply_minimap_marker_to_output(state, marker);
    }
}

void flush_minimap_output_to_backbuffer(UiOverlayState& state) {
    if (!state.emit_sprite_draws) {
        return;
    }

    const MinimapRenderState& minimap = state.minimap;
    if (minimap.output_pitch_pixels == 0 || minimap.minimap_width_pixels == 0 ||
        minimap.minimap_height_pixels == 0 || minimap.output_pixels.empty()) {
        return;
    }

    const i32 left = minimap.output_x;
    const i32 top = minimap.output_y;
    const i32 right = left + static_cast<i32>(minimap.minimap_width_pixels);
    const i32 bottom = top + static_cast<i32>(minimap.minimap_height_pixels);
    if (right <= 0 || bottom <= 0) {
        return;
    }
    const i32 clipped_left = std::max<i32>(left, 0);
    const i32 clipped_right = std::min<i32>(
        right, static_cast<i32>(minimap.output_pitch_pixels));
    if (clipped_left >= clipped_right) {
        return;
    }

    for (u32 y = 0; y < minimap.minimap_height_pixels; ++y) {
        const i32 screen_y = top + static_cast<i32>(y);
        if (screen_y < 0) {
            continue;
        }
        const std::size_t row =
            static_cast<std::size_t>(screen_y) * minimap.output_pitch_pixels;
        if (row + static_cast<std::size_t>(clipped_right) >
            minimap.output_pixels.size()) {
            continue;
        }

        i32 run_left = clipped_left;
        while (run_left < clipped_right) {
            const u16 color =
                minimap.output_pixels[row + static_cast<std::size_t>(run_left)];
            i32 run_right = run_left + 1;
            while (run_right < clipped_right &&
                minimap.output_pixels[row + static_cast<std::size_t>(run_right)] ==
                    color) {
                ++run_right;
            }
            DrawBackBufferFilledRectangle16(run_left, screen_y,
                run_right - 1, screen_y, color);
            run_left = run_right;
        }
    }
}

void flush_minimap_object_footprint_spill_to_backbuffer(UiOverlayState& state) {
    if (!state.emit_sprite_draws) {
        return;
    }

    const MinimapRenderState& minimap = state.minimap;
    if (minimap.output_pitch_pixels == 0 || minimap.minimap_width_pixels == 0 ||
        minimap.minimap_height_pixels == 0 || minimap.output_pixels.empty()) {
        return;
    }

    const i64 logical_left = minimap.output_x;
    const i64 logical_top = minimap.output_y;
    const i64 logical_right = logical_left + minimap.minimap_width_pixels;
    const i64 logical_bottom = logical_top + minimap.minimap_height_pixels;
    const i64 backing_width = minimap.output_width_pixels != 0
        ? std::min<u32>(minimap.output_width_pixels, minimap.output_pitch_pixels)
        : minimap.output_pitch_pixels;
    const i64 available_rows = static_cast<i64>(
        minimap.output_pixels.size() / minimap.output_pitch_pixels);
    const i64 backing_height = minimap.output_height_pixels != 0
        ? std::min<i64>(minimap.output_height_pixels, available_rows)
        : available_rows;
    if (logical_right <= 0 || logical_bottom <= 0 || backing_width <= 0 ||
        backing_height <= 0) {
        return;
    }

    const auto flush_rectangle = [&](i64 left, i64 top, i64 right, i64 bottom) {
        left = std::max<i64>(left, 0);
        top = std::max<i64>(top, 0);
        right = std::min<i64>(right, backing_width);
        bottom = std::min<i64>(bottom, backing_height);
        if (left >= right || top >= bottom) {
            return;
        }

        for (i64 y = top; y < bottom; ++y) {
            const std::size_t row =
                static_cast<std::size_t>(y) * minimap.output_pitch_pixels;
            i64 run_left = left;
            while (run_left < right) {
                const u16 color = minimap.output_pixels[
                    row + static_cast<std::size_t>(run_left)];
                i64 run_right = run_left + 1;
                while (run_right < right && minimap.output_pixels[
                    row + static_cast<std::size_t>(run_right)] == color) {
                    ++run_right;
                }
                DrawBackBufferFilledRectangle16(static_cast<i32>(run_left),
                    static_cast<i32>(y), static_cast<i32>(run_right - 1),
                    static_cast<i32>(y), color);
                run_left = run_right;
            }
        }
    };

    for (const UiOverlayMinimapMarker& marker : state.minimap_markers) {
        if (marker.kind != UiOverlayMinimapMarkerKind::object_footprint ||
            marker.width == 0 || marker.height == 0) {
            continue;
        }

        const i64 marker_left = marker.x;
        const i64 marker_top = marker.y;
        // FUN_004e24e9 always supplies an anchor inside the logical minimap.
        // Restricting the spill path to that contract prevents another caller's
        // arbitrary footprint marker from expanding the ordinary flush region.
        if (marker_left < logical_left || marker_left >= logical_right ||
            marker_top < logical_top || marker_top >= logical_bottom) {
            continue;
        }

        const i64 marker_right = marker_left + marker.width;
        const i64 marker_bottom = marker_top + marker.height;

        // FUN_004e27f8 writes the complete remembered-building footprint at
        // 0x004e282a and advances rows at 0x004e2833/0x004e2839 without a
        // logical minimap bounds check.  Present only those right/bottom spill
        // pixels; terrain, fog, active-unit, placement and viewport output keep
        // the original logical minimap flush bounds above.
        if (marker_right > logical_right) {
            flush_rectangle(logical_right, marker_top,
                marker_right, marker_bottom);
        }
        if (marker_bottom > logical_bottom) {
            flush_rectangle(marker_left, logical_bottom,
                std::min<i64>(marker_right, logical_right), marker_bottom);
        }
    }
}

void flush_placement_preview_markers_to_backbuffer(UiOverlayState& state) {
    if (!state.emit_sprite_draws) {
        return;
    }

    for (const UiOverlayMinimapMarker& marker : state.minimap_markers) {
        if (marker.kind != UiOverlayMinimapMarkerKind::placement_preview) {
            continue;
        }
        if (marker.valid) {
            OrBackBufferLowBlueMask32x32(marker.x, marker.y);
        } else {
            OrBackBufferHighRedMask32x32(marker.x, marker.y);
        }
    }
}

std::string ratio_text(u32 value, u32 max_value) {
    return std::to_string(value) + "/" + std::to_string(max_value);
}

u32 minimap_width_tiles(const UiOverlayState& state) {
    return state.map_width_tiles != 0 ? state.map_width_tiles : state.minimap.map_width_tiles;
}

u32 minimap_height_tiles(const UiOverlayState& state) {
    return state.map_height_tiles != 0 ? state.map_height_tiles : state.minimap.map_height_tiles;
}

std::size_t minimap_tile_index(const UiOverlayState& state, u32 tile_x, u32 tile_y) {
    return static_cast<std::size_t>(tile_y) * minimap_width_tiles(state) + tile_x;
}

u32 minimap_layer_value(
    const UiOverlayState& state, const std::vector<u32>& layer, u32 tile_x, u32 tile_y) {
    if (tile_x >= minimap_width_tiles(state) || tile_y >= minimap_height_tiles(state)) {
        return 0;
    }
    const std::size_t index = minimap_tile_index(state, tile_x, tile_y);
    return index < layer.size() ? layer[index] : 0;
}

i32 minimap_screen_x_for_tile(const UiOverlayState& state, u32 tile_x) {
    const u32 map_width = minimap_width_tiles(state);
    if (map_width == 0) {
        return state.minimap.output_x;
    }
    // FUN_004e24e9 advances the map-cell accumulator before emitting the
    // object/terrain pixel.  This is ceil((tile + 1) * mini / map) - 1,
    // distinct from the mobile-unit floor(tile * mini / map) mapping.
    return state.minimap.output_x + static_cast<i32>(
        ((static_cast<u64>(tile_x + 1) * state.minimap.minimap_width_pixels) - 1) /
        map_width);
}

i32 minimap_screen_y_for_tile(const UiOverlayState& state, u32 tile_y) {
    const u32 map_height = minimap_height_tiles(state);
    if (map_height == 0) {
        return state.minimap.output_y;
    }
    return state.minimap.output_y + static_cast<i32>(
        ((static_cast<u64>(tile_y + 1) * state.minimap.minimap_height_pixels) - 1) /
        map_height);
}

i32 minimap_command_screen_to_world_y(
    const UiOverlayState& state, i32 local_y) {
    if (state.minimap.map_width_tiles == 0 ||
        state.minimap.minimap_height_pixels == 0) {
        return 0;
    }
    // The two minimap command publishers at 0x004ea62b/0x004ea815 retain the
    // original rectangular-map quirk: Y is scaled by map width, unlike camera
    // drag/hover which correctly use map height.
    const i64 tile_y = (static_cast<i64>(local_y) *
        state.minimap.map_width_tiles) /
        state.minimap.minimap_height_pixels;
    return static_cast<i32>(tile_y * 0x20);
}

i32 minimap_input_screen_to_world_x(
    const UiOverlayState& state, i32 local_x) {
    if (state.minimap.map_width_tiles == 0 ||
        state.minimap.minimap_width_pixels == 0) {
        return 0;
    }
    const i64 tile_x = (static_cast<i64>(local_x) *
        state.minimap.map_width_tiles) /
        state.minimap.minimap_width_pixels;
    return static_cast<i32>(tile_x * 0x20);
}

i32 minimap_input_screen_to_world_y(
    const UiOverlayState& state, i32 local_y) {
    if (state.minimap.map_height_tiles == 0 ||
        state.minimap.minimap_height_pixels == 0) {
        return 0;
    }
    const i64 tile_y = (static_cast<i64>(local_y) *
        state.minimap.map_height_tiles) /
        state.minimap.minimap_height_pixels;
    return static_cast<i32>(tile_y * 0x20);
}

u16 minimap_owner_color(
    const UiOverlayState& state, u32 owner_id, bool footprint = false) {
    if (owner_id == state.local_player_slot) {
        return footprint ? state.minimap_local_footprint_color :
            state.minimap_local_unit_color;
    }

    const std::vector<u16>& configured = footprint ?
        state.minimap_owner_footprint_colors : state.minimap_owner_colors;
    if (owner_id < configured.size() && configured[owner_id] != 0) {
        return configured[owner_id];
    }

    // FUN_004e24e9/FUN_004e284a use remote owner ramp words 3/0 from
    // DAT_0156e8d0 + owner * 0x20. Palette slot zero mirrors that table.
    if (owner_id < 0x10u) {
        const auto& palette = palette_cache_state().pixel_slots[0];
        const std::size_t index =
            static_cast<std::size_t>(owner_id) * 0x10u + (footprint ? 3u : 0u);
        // FUN_004e25f4/FUN_004e28f8 use the owner-ramp word verbatim.  A
        // zero entry is a valid (black/hidden) colour, not a signal to fall
        // back to the generic remote green.
        if (index < palette.size()) {
            return palette[index];
        }
    }
    return footprint ? state.minimap_remote_footprint_color :
        state.minimap_remote_unit_color;
}

UiOverlayRect definition_footprint(const UiOverlayState& state, u32 definition_id) {
    if (definition_id < state.minimap_definition_footprints.size()) {
        const UiOverlayRect& rect = state.minimap_definition_footprints[definition_id];
        if (rect.width != 0 && rect.height != 0) {
            return rect;
        }
    }
    return {0, 0, 1, 1};
}

void append_minimap_marker(UiOverlayState& state, UiOverlayMinimapMarkerKind kind,
    i32 x, i32 y, u32 width, u32 height, u16 color, u32 item_id,
    u32 owner_id, bool valid = true) {
    UiOverlayMinimapMarker marker{};
    marker.kind = kind;
    marker.x = x;
    marker.y = y;
    marker.width = std::max<u32>(1, width);
    marker.height = std::max<u32>(1, height);
    marker.color = color;
    marker.item_id = item_id;
    marker.owner_id = owner_id;
    marker.valid = valid;
    state.minimap_markers.push_back(marker);
}

UiOverlayRect command_slot_rect(const UiOverlayState& state) {
    if (state.command_slot_count < state.command_slot_bounds.size()) {
        const UiOverlayRect& rect = state.command_slot_bounds[state.command_slot_count];
        if (rect.width != 0 || rect.height != 0) {
            return rect;
        }
    }
    // FUN_004e5731/FUN_004e5762 use the six coordinates copied into
    // DAT_008645cc/DAT_008645d0.  Once DAT_008663b4 reaches six they use the
    // screen dimensions as the off-screen hotkey position; the side-selection
    // table is driven by the separate DAT_008663bc counter.
    return {static_cast<i32>(state.screen_width),
        static_cast<i32>(state.screen_height), state.command_slot_size,
        state.command_slot_size};
}

void append_hot_region(UiOverlayState& state, const UiOverlayDrawRecord& record,
    u8 hotkey, bool enabled) {
    UiOverlayHotRegion region{};
    region.record = record;
    region.hotkey = hotkey;
    region.enabled = enabled;
    state.hot_regions.push_back(region);
}

const UiOverlayCommandOption* find_command_option(
    const UiOverlayState& state, u32 item_id) {
    const auto primary = std::find_if(state.primary_production_options.begin(),
        state.primary_production_options.end(),
        [item_id](const UiOverlayCommandOption& option) {
            return option.item_id == item_id;
        });
    if (primary != state.primary_production_options.end()) {
        return &*primary;
    }
    const auto it = std::find_if(state.command_options.begin(),
        state.command_options.end(), [item_id](const UiOverlayCommandOption& option) {
            return option.item_id == item_id;
        });
    return it == state.command_options.end() ? nullptr : &*it;
}

u8 uppercase_hotkey(u8 key) {
    if (key >= 'a' && key <= 'z') {
        return static_cast<u8>(key - 0x20);
    }
    return key;
}

u32 alternate_offset(const UiOverlayDrawRecord& record) {
    if ((record.flags & kUiOverlayFlagAlternateC) != 0) {
        return 3;
    }
    if ((record.flags & kUiOverlayFlagAlternateB) != 0) {
        return 2;
    }
    if ((record.flags & kUiOverlayFlagAlternateA) != 0) {
        return 1;
    }
    return 0;
}

bool record_contains_point(const UiOverlayDrawRecord& record, i32 x, i32 y) {
    if ((!is_indexed_queue_command_item(record.item_id) &&
            (record.flags & kUiOverlayFlagHidden) != 0) || record.width == 0 ||
        record.height == 0) {
        return false;
    }
    const i32 right = record.x + static_cast<i32>(record.width);
    const i32 bottom = record.y + static_cast<i32>(record.height);
    return x >= record.x && x < right && y >= record.y && y < bottom;
}

bool hot_region_contains_point_original(
    const UiOverlayDrawRecord& record, i32 x, i32 y) {
    if ((!is_indexed_queue_command_item(record.item_id) &&
            (record.flags & kUiOverlayFlagHidden) != 0) || record.width == 0 ||
        record.height == 0) {
        return false;
    }
    const i32 right = record.x + static_cast<i32>(record.width);
    const i32 bottom = record.y + static_cast<i32>(record.height);
    return x >= record.x && x <= right && y >= record.y && y <= bottom;
}

const UiOverlayHotRegion* hot_region_at(
    const UiOverlayState& state, i32 x, i32 y) {
    for (const UiOverlayHotRegion& region : state.hot_regions) {
        if (hot_region_contains_point_original(region.record, x, y)) {
            return &region;
        }
    }
    return nullptr;
}

bool is_script_always_capture_command_button(u32 item_id) {
    return is_indexed_queue_command_item(item_id);
}

bool draw_indexed_queue_command_record(
    UiOverlayState& state, const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record)) {
        return false;
    }

    UiOverlayIconBlitKind kind = UiOverlayIconBlitKind::base;
    if (record.item_id == 0x1abu) {
        kind = UiOverlayIconBlitKind::production;
    }
    else if (record.item_id == 0x1acu) {
        kind = UiOverlayIconBlitKind::equipment;
    }

    UiOverlayDrawRecord icon = record;
    icon.width = icon.height = 0x26;
    const bool drawn = draw_record_command_icon_blit(
        state, icon, kind, record.aux);

    // FUN_004e2042/FUN_004e208f/FUN_004e20dc draw '1' + logical index at
    // (slot_x + 0x21, slot_y + 0x1e) after the queued command icon.  Those
    // paths select draw font zero without changing the current metric font.
    append_text(state, std::to_string(record.flags + 1u),
        record.x + 0x21, record.y + 0x1e, 1,
        false, false, false, 0, kUiOverlayPreserveMetricFont);
    return drawn;
}

u32 selected_equipment_slot_index_for_dispatch(u32 item_id) {
    switch (item_id) {
    case 0x1ae:
        return 4;
    case 0x1af:
        return 5;
    case 0x1b0:
        return 0;
    case 0x1b1:
        return 1;
    case 0x1b2:
        return 2;
    case 0x1b3:
        return 3;
    default:
        return 6;
    }
}

void draw_selected_unit_slot_value_at(
    UiOverlayState& state, u32 value, i32 x, i32 y) {
    if (value == 0) {
        return;
    }

    // FUN_004e21c3 selects one of four item.trt frames from the exact raw
    // +0x2c thresholds before printing the unscaled value over the icon.
    u32 icon_id = 1;
    if (value > 100u) {
        ++icon_id;
    }
    if (value > 500u) {
        ++icon_id;
    }
    if (value > 1000u) {
        ++icon_id;
    }
    const u32 previous_record_size = state.current_record_size;
    state.current_record_size = 0x26;
    BlitUiOverlayEquipmentIcon(state, icon_id, x, y);
    state.current_record_size = previous_record_size;
    append_text(state, std::to_string(value), x + 0x22, y + 0x24, 1,
        false, true, true, 0, 0);
}

void draw_equipment_command_icon_at(
    UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    if (item_id < 0x1b4 || item_id >= 0x24a) {
        return;
    }

    // FUN_004e2129 indexes item.trt with dispatch_id - 0x1b4.  For the first
    // five entries it overlays the selected unit's raw +0x2c value; this is
    // not a one-based command shortcut despite the tempting 0..4 range.
    const u32 equipment_id = item_id - 0x1b4;
    const u32 previous_record_size = state.current_record_size;
    state.current_record_size = 0x26;
    BlitUiOverlayEquipmentIcon(state, equipment_id, x, y);
    state.current_record_size = previous_record_size;
    if (equipment_id < 5) {
        append_text(state, std::to_string(state.selected_unit_slot_value),
            x + 0x22, y + 0x24, 1, false, true, true, 0, 0);
    }
}

bool draw_selected_unit_slot_value_record(
    UiOverlayState& state, const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record) || state.selected_unit_slot_value == 0) {
        return false;
    }
    const UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, false);
    draw_selected_unit_slot_value_at(state, state.selected_unit_slot_value,
        adjusted.x, adjusted.y);
    return true;
}

bool draw_selected_unit_equipment_slot_record(
    UiOverlayState& state, const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record)) {
        return false;
    }
    const u32 slot = selected_equipment_slot_index_for_dispatch(record.item_id);
    if (slot >= state.selected_unit_equipment_slots.size()) {
        return false;
    }
    const u32 equipment_id = state.selected_unit_equipment_slots[slot];
    if (equipment_id == 0) {
        return false;
    }

    const UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, false);
    const u32 previous_record_size = state.current_record_size;
    state.current_record_size = 0x13;
    BlitUiOverlayEquipmentIconHalfSampled(
        state, equipment_id, adjusted.x, adjusted.y);
    state.current_record_size = previous_record_size;
    return true;
}

bool draw_equipment_command_icon_record(
    UiOverlayState& state, const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record)) {
        return false;
    }
    const UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, false);
    draw_equipment_command_icon_at(state, record.item_id, adjusted.x, adjusted.y);
    return true;
}

u32 selected_production_category_index(const UiOverlayState& state) {
    if (state.selected_production_category == 2) {
        return 1;
    }
    if (state.selected_production_category > 2) {
        return 2;
    }
    return 0;
}

bool selected_production_category_active(const UiOverlayState& state) {
    if (state.selected_production_category == 0) {
        return false;
    }
    const u32 category = selected_production_category_index(state);
    return category < state.selected_production_class_counts.size() &&
        state.selected_production_class_counts[category] != 0;
}

bool should_probe_selected_production_action(const UiOverlayState& state, u32 bit) {
    if (bit >= 32) {
        return false;
    }
    if ((state.selected_unit_capability_mask & (1u << bit)) == 0) {
        return false;
    }
    return bit != 0x13u || (state.selected_unit_command_flags & 0x800u) == 0;
}

std::size_t selected_production_gate_mask_index_for_failure(u32 failure_code) {
    if (failure_code == kProductionGateFailureOwnerRequirement ||
        failure_code == kProductionGateFailureActiveLimit) {
        return 3;
    }
    if (failure_code == kProductionGateFailureResourceLimit) {
        return 2;
    }
    return 1;
}

bool production_action_gate_flags(const UiOverlayState& state, u32 action_id,
    u32& flags) {
    if (action_id >= 32) {
        return false;
    }
    const u32 bit = 1u << action_id;
    if ((state.selected_production_gate_masks[0] & bit) != 0) {
        flags = 0;
        return true;
    }
    if ((state.selected_production_gate_masks[1] & bit) != 0) {
        flags = kProductionGateFlagQueuedOrOther;
        return true;
    }
    if ((state.selected_production_gate_masks[2] & bit) != 0) {
        flags = kProductionGateFlagResourceLimit;
        return true;
    }
    if ((state.selected_production_gate_masks[3] & bit) != 0) {
        flags = kProductionGateFlagOwnerOrActiveLimit;
        return true;
    }
    return false;
}

bool handle_local_command_panel_selector(UiOverlayState& state, u32 item_id) {
    static_cast<void>(state);
    static_cast<void>(item_id);
    return false;
}

u32 original_effective_hot_region_flags(const UiOverlayHotRegion& region) {
    return region.enabled ? region.record.flags :
        (region.record.flags | kUiOverlayFlagDisabled);
}

bool can_capture_ui_command_button_press(
    const UiOverlayState& state, const UiOverlayHotRegion& region) {
    const u32 item_id = region.record.item_id;
    if (state.scripted_input_restricted && item_id != 0x194u) {
        return false;
    }
    if (is_script_always_capture_command_button(item_id)) {
        return true;
    }
    return item_id != 0xc8u &&
        (original_effective_hot_region_flags(region) & 0x36u) == 0;
}

void append_command_action_at_world(UiOverlayState& state, u32 item_id, u32 aux,
    u32 action, u32 flags, i32 world_x, i32 world_y) {
    if (item_id == 0xffffffffu) {
        return;
    }
    UiOverlayCommandAction command{};
    command.item_id = item_id;
    command.aux = aux;
    command.flags = flags;
    command.action = action;
    command.world_x = world_x;
    command.world_y = world_y;
    state.command_actions.push_back(command);
    state.last_hotkey_command = item_id;
    state.last_hotkey_aux = aux;
    state.last_hotkey_flags = flags;
    state.pending_local_command = true;
}

void append_command_action(
    UiOverlayState& state, u32 item_id, u32 aux, u32 action, u32 flags = 0) {
    append_command_action_at_world(state, item_id, aux, action, flags,
        state.camera_x + state.mouse_x, state.camera_y + state.mouse_y);
}

u32 hover_kind_for_command_item(u32 item_id) {
    if (item_id <= 0x05f) {
        return 0x02;
    }
    if (item_id <= 0x0d3) {
        return 0x0d;
    }
    if (item_id <= 0x0f3) {
        return 0x0e;
    }
    if (item_id <= 0x133) {
        return 0x12;
    }
    if (item_id <= 0x193) {
        return 0x0d;
    }
    if (item_id <= 0x19c) {
        return 0x03;
    }
    if (item_id <= 0x19e) {
        return 0;
    }
    if (item_id == 0x19f) {
        return 0x10;
    }
    if (item_id <= 0x1a1) {
        return 0;
    }
    if (item_id <= 0x1a3) {
        return 0x03;
    }
    if (item_id <= 0x1a5) {
        return 0;
    }
    if (item_id == 0x1a6) {
        return 0x05;
    }
    if (item_id == 0x1a7) {
        return 0;
    }
    if (item_id == 0x1a8) {
        return 0x11;
    }
    if (item_id <= 0x1ac) {
        return 0;
    }
    if (item_id <= 0x1b3) {
        return 0x14;
    }
    if (item_id <= 0x249) {
        return 0x0f;
    }
    if (item_id <= 0x2df) {
        return 0x13;
    }
    return 0;
}

void set_hot_region_result(UiOverlayState& state, const UiOverlayHotRegion& region) {
    state.last_hotkey_command = region.record.item_id;
    state.last_hotkey_aux = region.record.aux;
    state.last_hotkey_flags = original_effective_hot_region_flags(region);
    state.last_hotkey_hover_kind = hover_kind_for_command_item(region.record.item_id);
}

bool hover_kind_uses_immediate_tooltip_schedule(u32 kind) {
    switch (kind) {
    case 0x02:
    case 0x05:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
    case 0x12:
    case 0x13:
    case 0x14:
        return true;
    default:
        return false;
    }
}

bool hover_context_is_command_record(const UiOverlayHoverContext& context) {
    return context.item_id < 0x2e0 &&
        context.kind == hover_kind_for_command_item(context.item_id);
}

void set_tooltip_payload_for_hover(UiOverlayState& state) {
    GameplayTooltipState& tooltip = gameplay_tooltip_state();
    const bool command_record_hover =
        hover_context_is_command_record(state.hover_context);
    tooltip.screen_width = state.screen_width;
    tooltip.local_owner = state.local_player_slot;
    tooltip.current_unit_type =
        command_record_hover && state.hover_context.item_id == 0xb5 ?
        state.hover_context.unit_id : state.hover_context.item_id;
    tooltip.current_object_id = state.hover_context.item_id;
    tooltip.current_payload = command_record_hover ?
        state.hover_context.unit_id :
        state.hover_context.unit_id != 0 ?
        state.hover_context.unit_id : state.hover_context.item_id;
    if (state.hover_context.kind == 9) {
        const u32 object_offset = state.hover_context.unit_id * 0x3c;
        tooltip.current_payload = object_offset;
        if (tooltip.indexed_values.size() <= object_offset) {
            tooltip.indexed_values.resize(object_offset + 1);
        }
        if (tooltip.indexed_amounts.size() <= object_offset) {
            tooltip.indexed_amounts.resize(object_offset + 1);
        }
        tooltip.indexed_values[object_offset] = state.hover_context.item_id;
        tooltip.indexed_amounts[object_offset] = 0;
        for (const UiOverlayMapEffect& effect : state.map_effects) {
            if (effect.instance_id == state.hover_context.unit_id) {
                tooltip.indexed_amounts[object_offset] = effect.amount;
                break;
            }
        }
    }
    tooltip.current_production_order_packed = 0;
    if (state.hover_context.item_id >= 0xf4 && state.hover_context.item_id < 0x134) {
        const u32 order_id = state.hover_context.item_id - 0xf4;
        tooltip.current_production_order_packed =
            (order_id << 16) | std::max<u32>(1, state.last_hotkey_aux);
    }
    tooltip.hover_flags = 0;
    if (const UiOverlayCommandOption* option =
            find_command_option(state, state.hover_context.item_id)) {
        tooltip.hover_flags = option->flags;
    }
    tooltip.selected_unit.offset = state.selected_unit_id;
    tooltip.selected_unit.type = state.selected_unit_type;
    tooltip.selected_unit.owner = state.selected_unit_owner;
    tooltip.selected_unit.area_marker_flags = 0;
    tooltip.selected_unit_valid = state.selected_unit_count != 0;
    tooltip.current_text.clear();
    tooltip.multiline_text.clear();
    switch (state.hover_context.kind) {
    case 1:
        tooltip.current_text = "Minimap";
        break;
    case 5:
        tooltip.current_text = "Interface";
        break;
    case 6:
    case 7:
    case 8:
        tooltip.current_text = "Unit";
        break;
    case 9:
        tooltip.current_text = "Map effect";
        break;
    case 0x0c:
        tooltip.current_text = "Placement";
        break;
    default:
        tooltip.current_text = "Command";
        break;
    }
}

const UiOverlayMinimapUnit* unit_at_screen_point(
    const UiOverlayState& state, i32 screen_x, i32 screen_y, bool free_unit_only) {
    const i32 world_x = state.camera_x + screen_x;
    const i32 world_y = state.camera_y + screen_y;
    const UiOverlayMinimapUnit* enemy_unit = nullptr;
    const UiOverlayMinimapUnit* local_object = nullptr;
    const UiOverlayMinimapUnit* enemy_object = nullptr;
    const std::vector<UiOverlayMinimapUnit>& candidates = free_unit_only
        ? state.lifecycle_units
        : state.minimap_units;
    for (const UiOverlayMinimapUnit& unit : candidates) {
        if (!UiOverlayUnitVisibleToLocalPlayer(unit)) {
            continue;
        }
        if (free_unit_only &&
            (unit.type_id >= 0x60 || (unit.runtime_flags & 4u) == 0)) {
            continue;
        }
        const i32 left = unit.world_x + unit.bounds_left;
        const i32 top = unit.world_y + unit.bounds_top;
        const i32 right = left + unit.bounds_width;
        const i32 bottom = top + unit.bounds_height;
        if (world_x < left || world_x > right ||
            world_y < top || world_y > bottom) {
            continue;
        }

        if (free_unit_only) {
            return &unit;
        }
        if (unit.type_id < 0x60) {
            if (unit.owner_id == state.local_player_slot) {
                return &unit;
            }
            enemy_unit = &unit;
        } else if (unit.owner_id == state.local_player_slot) {
            local_object = &unit;
        } else {
            enemy_object = &unit;
        }
    }
    if (enemy_unit != nullptr) {
        return enemy_unit;
    }
    if (local_object != nullptr) {
        return local_object;
    }
    return enemy_object;
}

void set_hover_from_unit(
    UiOverlayState& state, const UiOverlayMinimapUnit& unit, i32 screen_x, i32 screen_y) {
    state.hover_context.item_id = unit.type_id;
    state.hover_context.unit_id = unit.unit_id;
    state.hover_context.x = state.camera_x + screen_x;
    state.hover_context.y = state.camera_y + screen_y;
    // FUN_004e9458: local owner -> 6, directed relation bit -> 7, otherwise
    // 8.  Replay/scenario mode is unrelated to this classification.
    state.hover_context.kind = ResolveGameplayUnitHoverKind(
        state.local_player_slot, unit.owner_id,
        state.local_owner_relation_mask);
}

u32 minimap_screen_width(const UiOverlayState& state) {
    if (state.minimap.minimap_width_pixels != 0) {
        return state.minimap.minimap_width_pixels;
    }
    const u32 capacity = state.screen_layout_bucket == 0 ? 0x5fu : 0x73u;
    return std::min<u32>(capacity, std::max<u32>(1, minimap_width_tiles(state)));
}

u32 minimap_screen_height(const UiOverlayState& state) {
    if (state.minimap.minimap_height_pixels != 0) {
        return state.minimap.minimap_height_pixels;
    }
    const u32 capacity = state.screen_layout_bucket == 0 ? 0x5fu : 0x73u;
    return std::min<u32>(capacity, std::max<u32>(1, minimap_height_tiles(state)));
}

bool point_inside_minimap_rect(UiOverlayState& state, bool use_height_for_y) {
    ConfigureGameplayUiOverlayLayout(state);
    const i32 width = static_cast<i32>(minimap_screen_width(state));
    const i32 y_extent = static_cast<i32>(
        use_height_for_y ? minimap_screen_height(state) : minimap_screen_width(state));
    return state.mouse_x >= state.minimap.output_x &&
        state.mouse_y >= state.minimap.output_y &&
        state.mouse_x < state.minimap.output_x + width &&
        state.mouse_y < state.minimap.output_y + y_extent;
}

UiOverlayRect normalized_selection_rect(const UiOverlayState& state) {
    const i32 left = std::min(state.selection_left, state.selection_right) + state.camera_x;
    const i32 right = std::max(state.selection_left, state.selection_right) + state.camera_x;
    const i32 top = std::min(state.selection_top, state.selection_bottom) + state.camera_y;
    const i32 bottom = std::max(state.selection_top, state.selection_bottom) + state.camera_y;
    return {left, top, static_cast<u32>(std::max(0, right - left + 1)),
        static_cast<u32>(std::max(0, bottom - top + 1))};
}

UiOverlayRect unit_world_rect(const UiOverlayMinimapUnit& unit) {
    const u32 width_tiles = std::max<u32>(1, unit.footprint_width_tiles);
    const u32 height_tiles = std::max<u32>(1, unit.footprint_height_tiles);
    const i32 width = static_cast<i32>(width_tiles << 5);
    const i32 height = static_cast<i32>(height_tiles << 5);
    const i32 left = unit.type_id < 0x60 ? unit.world_x - (width / 2) : unit.world_x;
    const i32 top = unit.type_id < 0x60 ? unit.world_y - (height / 2) : unit.world_y;
    return {left, top, static_cast<u32>(width), static_cast<u32>(height)};
}

bool rects_intersect(const UiOverlayRect& a, const UiOverlayRect& b) {
    const i32 a_right = a.x + static_cast<i32>(a.width);
    const i32 a_bottom = a.y + static_cast<i32>(a.height);
    const i32 b_right = b.x + static_cast<i32>(b.width);
    const i32 b_bottom = b.y + static_cast<i32>(b.height);
    return a.x < b_right && b.x < a_right && a.y < b_bottom && b.y < a_bottom;
}

const UiOverlayMinimapUnit* find_unit_by_id(const UiOverlayState& state, u32 unit_id) {
    const auto it = std::find_if(state.minimap_units.begin(), state.minimap_units.end(),
        [unit_id](const UiOverlayMinimapUnit& unit) {
            return unit.unit_id == unit_id;
        });
    return it == state.minimap_units.end() ? nullptr : &*it;
}

bool unit_already_selected(const UiOverlayState& state, u32 unit_id) {
    return std::find(state.selected_unit_ids.begin(), state.selected_unit_ids.end(),
        unit_id) != state.selected_unit_ids.end();
}

bool unit_selectable_for_local_player(
    const UiOverlayState& state, const UiOverlayMinimapUnit& unit) {
    return UiOverlayUnitVisibleToLocalPlayer(unit) &&
        unit.type_id < 0x60 &&
        (unit.owner_id == state.local_player_slot || CheckScenarioSelectionOverride(state));
}

bool double_click_unit_visibility_passes(const UiOverlayState& state,
    const UiOverlayMinimapUnit& unit, bool require_current_visibility) {
    // FUN_004e96ae/FUN_004ead82 apply the raw hidden bit and the special
    // owner/cell visibility predicate before reading fog bits 28 and 27.
    if ((unit.runtime_flags & 0x80u) != 0 || unit.hidden_from_minimap ||
        !unit.visible_to_local_player || !unit.special_visibility_gate_passed ||
        unit.world_x < 0 || unit.world_y < 0) {
        return false;
    }

    const u32 tile_x = static_cast<u32>(unit.world_x) >> 5;
    const u32 tile_y = static_cast<u32>(unit.world_y) >> 5;
    const u32 visibility = minimap_layer_value(
        state, state.minimap_visibility_flags, tile_x, tile_y);
    if ((visibility & 0x10000000u) == 0) {
        return false;
    }
    return (!require_current_visibility && state.reveal_minimap_fog) ||
        (visibility & 0x08000000u) != 0;
}

bool double_click_unit_contains_world_point(
    const UiOverlayMinimapUnit& unit, i32 world_x, i32 world_y) {
    const i64 left = static_cast<i64>(unit.world_x) + unit.bounds_left;
    const i64 top = static_cast<i64>(unit.world_y) + unit.bounds_top;
    const i64 right = left + unit.bounds_width;
    const i64 bottom = top + unit.bounds_height;
    return static_cast<i64>(world_x) >= left &&
        static_cast<i64>(world_x) <= right &&
        static_cast<i64>(world_y) >= top &&
        static_cast<i64>(world_y) <= bottom;
}

bool double_click_unit_intersects_viewport(
    const UiOverlayState& state, const UiOverlayMinimapUnit& unit) {
    const i64 viewport_left = state.camera_x;
    const i64 viewport_top = state.camera_y;
    const i64 viewport_right = viewport_left + state.screen_width;
    const i64 viewport_bottom = viewport_top + state.world_viewport_height;
    const i64 unit_left = static_cast<i64>(unit.world_x) + unit.bounds_left;
    const i64 unit_top = static_cast<i64>(unit.world_y) + unit.bounds_top;
    const i64 unit_right = unit_left + unit.bounds_width;
    const i64 unit_bottom = unit_top + unit.bounds_height;
    // Original comparisons exclude only strict separation.  Both the sprite
    // bounds and camera+extent endpoints are therefore inclusive.
    return unit_right >= viewport_left && unit_left <= viewport_right &&
        unit_bottom >= viewport_top && unit_top <= viewport_bottom;
}

const UiOverlayMinimapUnit* double_click_unit_at_screen_point(
    const UiOverlayState& state) {
    const i32 world_x = state.camera_x + state.mouse_x;
    const i32 world_y = state.camera_y + state.mouse_y;
    const UiOverlayMinimapUnit* enemy_unit = nullptr;
    const UiOverlayMinimapUnit* local_object = nullptr;
    const UiOverlayMinimapUnit* enemy_object = nullptr;
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (!double_click_unit_visibility_passes(state, unit, false) ||
            !double_click_unit_contains_world_point(unit, world_x, world_y)) {
            continue;
        }
        if (unit.type_id < 0x60u) {
            if (unit.owner_id == state.local_player_slot) {
                return &unit;
            }
            enemy_unit = &unit;
        }
        else if (unit.owner_id == state.local_player_slot) {
            local_object = &unit;
        }
        else {
            enemy_object = &unit;
        }
    }
    if (enemy_unit != nullptr) {
        return enemy_unit;
    }
    if (local_object != nullptr) {
        return local_object;
    }
    return enemy_object;
}

void clear_primary_selection_command_state(UiOverlayState& state) {
    state.placement_mode = 0;
    state.placement_definition_id = 0;
    state.pressed_command_id = 0xffffffffu;
    state.pressed_command_aux = 0xffffffffu;
    state.held_command_id = 0xffffffffu;
    state.command_button_press_active = false;
}

bool select_unit(UiOverlayState& state, const UiOverlayMinimapUnit& unit,
    bool make_primary = true) {
    if (unit_already_selected(state, unit.unit_id) ||
        state.selected_unit_ids.size() >= state.max_selected_unit_count) {
        return false;
    }
    state.selected_unit_ids.push_back(unit.unit_id);
    if (make_primary || state.selected_unit_id == 0) {
        state.selected_unit_id = unit.unit_id;
        state.selected_unit_type = unit.type_id;
        state.selected_unit_owner = unit.owner_id;
    }
    state.selected_unit_count = static_cast<u32>(state.selected_unit_ids.size());
    return true;
}

void deselect_unit(UiOverlayState& state, u32 unit_id) {
    const bool was_primary = state.selected_unit_id == unit_id;
    state.selected_unit_ids.erase(std::remove(state.selected_unit_ids.begin(),
        state.selected_unit_ids.end(), unit_id), state.selected_unit_ids.end());
    if (was_primary) {
        state.selected_unit_id = 0;
        state.selected_unit_type = 0;
        state.selected_unit_owner = 0;
        clear_primary_selection_command_state(state);
    }
    RecountGameplaySelectedUnits(state);
}

} // namespace

bool IsUiOverlayAvatarProductionStructureType(u32 type_id) {
    return type_id == 0x6fu || type_id == 0x7fu ||
        type_id == 0x8fu || type_id == 0x9fu;
}

bool MatchesUiOverlayAvatarAttachmentSlot(const UnitMovementUnit& unit,
    u32 owner_id, u32 slot_id) {
    return unit.owner_id == owner_id &&
        (unit.command_flags & 0x003c0000u) == (slot_id << 18);
}

bool MatchesUiOverlayAvatarProducerQueueSlot(const UnitMovementUnit& unit,
    u32 owner_id, u32 slot_id) {
    if (unit.owner_id != owner_id ||
        !IsUiOverlayAvatarProductionStructureType(unit.type_id)) {
        return false;
    }
    if ((unit.command_state == 0x50u || unit.command_state == 0x51u) &&
        unit.path_target_y == static_cast<i32>(slot_id)) {
        return true;
    }
    const u32 deferred_count = std::min<u32>(4u,
        std::min<u32>(unit.deferred_command_count,
            static_cast<u32>(unit.deferred_commands.size())));
    for (u32 index = 0; index < deferred_count; ++index) {
        const UnitQueuedCommand& queued = unit.deferred_commands[index];
        if (queued.state == 0x10u && queued.value == slot_id) {
            return true;
        }
    }
    return false;
}

u32 ResolveUiOverlaySelectedUnitProgressValue(u32 command_state,
    u32 action_mode_gate, u32 action_mode, u32 animation_frame, u32 work_timer) {
    const u32 command = command_state & 0xffu;
    if (command == 0x50u || command == 0x51u) {
        return animation_frame;
    }
    if (command == 0x82u || command == 0x83u ||
        action_mode_gate == 1u || command == 0x4du || command == 0x4eu) {
        return action_mode;
    }
    return work_timer;
}

u32 ResolveUiOverlayConstructionProgressTotal(u32 production_spawn_time) {
    return std::max<u32>(production_spawn_time, 1u);
}

UiOverlayState& ui_overlay_state() {
    return g_ui_overlay_state;
}

void ResetUiOverlayState() {
    g_ui_overlay_state = {};
}

void ResetUiOverlayDrawQueue(UiOverlayState& state) {
    state.queued_records.clear();
    state.dispatched_records.clear();
    state.icon_blit_requests.clear();
    state.text_commands.clear();
    state.progress_commands.clear();
    state.minimap_markers.clear();
    state.pulse_commands.clear();
    state.dynamic_icon_index = 0;
    state.side_slot_index = 0;
}

void InstallDefaultUiOverlayDispatchHandlers(UiOverlayState& state) {
    if (state.dispatch_handlers[0] != nullptr) {
        return;
    }
    for (u32 id = 0; id < 0x60; ++id) {
        state.dispatch_handlers[id] = DrawUiOverlaySmallUnitIconRecord;
    }
    for (u32 id = 0x60; id < 0xaa; ++id) {
        state.dispatch_handlers[id] = DrawUiOverlayLargeUnitIconRecord;
    }
    for (u32 id = 0xaa; id < 0xd4; ++id) {
        state.dispatch_handlers[id] = DrawUiOverlayUnitOrObjectIconRecord;
    }
    for (u32 id = 0xd4; id < 0xf4; ++id) {
        state.dispatch_handlers[id] = DrawUiOverlayObjectIconRecord;
    }
    for (u32 id = 0xf4; id < 0x134; ++id) {
        state.dispatch_handlers[id] = DrawUiOverlayProductionIconRecord;
    }
    for (u32 id = 0x134; id < 0x194; ++id) {
        state.dispatch_handlers[id] = DrawUiOverlayEquipmentIconRecord;
    }
    for (u32 id = 0x24a; id <= 0x2df; ++id) {
        state.dispatch_handlers[id] = DrawUiOverlayEquipmentIconRecord;
    }
    state.dispatch_handlers[0x194] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiSmallSlot1(record.item_id);
        };
    state.dispatch_handlers[0x195] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiSmallSlot2(record.item_id);
        };
    state.dispatch_handlers[0x196] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiSmallSlot3(record.item_id);
        };
    state.dispatch_handlers[0x197] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiLargeSlot0AndDetails(record.item_id);
        };
    state.dispatch_handlers[0x198] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiLargeSlot3AndDetails(record.item_id);
        };
    state.dispatch_handlers[0x199] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiLargeSlot6(record.item_id, record.flags);
        };
    state.dispatch_handlers[0x19a] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiLargeSlot9(record.item_id, record.flags);
        };
    state.dispatch_handlers[0x19b] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiLargeSlot12(record.flags);
        };
    state.dispatch_handlers[0x19c] =
        [](UiOverlayState&, const UiOverlayDrawRecord& record) {
            return DrawUiLargeSlot15(record.flags);
        };
    state.dispatch_handlers[0x1a6] =
        [](UiOverlayState& overlay, const UiOverlayDrawRecord&) {
            const i32 previous_x = overlay.large_slot_x;
            const i32 previous_y = overlay.large_slot_y;
            overlay.large_slot_x = overlay.wide_slot_bounds.x;
            overlay.large_slot_y = overlay.wide_slot_bounds.y;
            RenderSelectedUnitInfoPanel(overlay);
            overlay.large_slot_x = previous_x;
            overlay.large_slot_y = previous_y;
            return true;
        };
    state.dispatch_handlers[0x1a8] =
        [](UiOverlayState& overlay, const UiOverlayDrawRecord& record) {
            const UiOverlayMinimapUnit* unit =
                find_unit_by_id(overlay, record.aux);
            if (unit == nullptr) {
                return false;
            }

            const u32 previous_record_size = overlay.current_record_size;
            const u32 previous_palette = overlay.current_palette_selector;
            overlay.current_record_size = 0x26;
            if (unit->max_health == 0) {
                overlay.current_palette_selector = previous_palette;
                overlay.current_record_size = previous_record_size;
                return false;
            }
            const u32 health_palette = 0x1fu - static_cast<u32>(
                (static_cast<u64>(unit->health) * 0x1fu /
                    unit->max_health) & 0x1fu);
            BlitUiOverlayPaletteTableIcon(overlay, health_palette,
                unit->type_id, record.x, record.y);

            if ((unit->action_effect_flags & 2u) != 0 &&
                overlay.emit_sprite_draws) {
                const std::string level =
                    std::to_string(unit->status_timer + 1u);
                DrawUiGlyphRun(level.c_str(), level.size(),
                    record.x + 0x1a, record.y + 0x16, 9,
                    overlay.glyph_resource_base, '0');
            }
            overlay.current_palette_selector = previous_palette;
            overlay.current_record_size = previous_record_size;
            return true;
        };
    state.dispatch_handlers[0x1aa] = draw_indexed_queue_command_record;
    state.dispatch_handlers[0x1ab] = draw_indexed_queue_command_record;
    state.dispatch_handlers[0x1ac] = draw_indexed_queue_command_record;
    state.dispatch_handlers[0x1ad] = draw_selected_unit_slot_value_record;
    for (u32 id = 0x1ae; id <= 0x1b3; ++id) {
        state.dispatch_handlers[id] = draw_selected_unit_equipment_slot_record;
    }
    for (u32 id = 0x1b4; id < 0x24a; ++id) {
        state.dispatch_handlers[id] = draw_equipment_command_icon_record;
    }
}

void RenderGameplayWorldAndUiOverlay(UiOverlayState& state) {
    reset_frame_output_commands(state);
    RenderProductionPlacementPreviewOverlay(state);
    flush_placement_preview_markers_to_backbuffer(state);
    frame_callback(state, state.callbacks.draw_world_surface);
    RenderGameplayMinimapOverlay(state);
    FlushUiOverlayDrawQueue(state);
    RenderGameplayResourceCounters(state);
    flush_ui_overlay_progress_commands(state);
    flush_ui_overlay_text_commands(state);
    frame_callback(state, state.callbacks.draw_after_overlay);
}

void DrawGameplaySelectionRectangleOverlay(UiOverlayState& state) {
    if (!state.selection_rectangle_active) {
        return;
    }
    // FUN_004e02b5 normalizes each drag axis before drawing, so dragging
    // toward the upper-left has the same inclusive outline as lower-right.
    const i32 left = std::min(state.selection_left, state.selection_right);
    const i32 top = std::min(state.selection_top, state.selection_bottom);
    const i32 right = std::max(state.selection_left, state.selection_right);
    const i32 bottom = std::max(state.selection_top, state.selection_bottom);
    if (state.callbacks.draw_selection_rectangle != nullptr) {
        state.callbacks.draw_selection_rectangle(state, left, top, right, bottom);
        return;
    }
    DrawBackBufferRectangleOutline16(left, top, right - left + 1,
        bottom - top + 1, SurfacePixelMode555() ? 0x7fffu : 0xffffu);
}

void FlushUiOverlayDrawQueue(UiOverlayState& state) {
    for (const UiOverlayDrawRecord& record : state.queued_records) {
        DispatchUiOverlayDrawRecord(state, record);
    }
    frame_callback(state, state.callbacks.draw_after_queue);
    if (state.clear_queue_after_flush) {
        state.queued_records.clear();
    }
}

bool DispatchUiOverlayDrawRecord(UiOverlayState& state, const UiOverlayDrawRecord& record) {
    InstallDefaultUiOverlayDispatchHandlers(state);
    state.dispatched_records.push_back(record);
    if (state.callbacks.draw_record != nullptr) {
        return state.callbacks.draw_record(state, record);
    }
    if (record.item_id < state.dispatch_handlers.size() &&
        state.dispatch_handlers[record.item_id] != nullptr) {
        return state.dispatch_handlers[record.item_id](state, record);
    }
    if (state.emit_sprite_draws) {
        return DrawResourceSpriteNormal(record.item_id, record.x, record.y);
    }
    return true;
}

void QueueProductionActionDynamicIconRecord(UiOverlayState& state, u32 object_id,
    u32 aux, u32 flags) {
    const u32 action_id = object_id >= 0xd4 ? object_id - 0xd4 : object_id;
    state.current_icon_marker =
        vector_value_or_zero(state.production_action_icon_markers, action_id);
    QueueUiOverlayDynamicIconRecord(state, object_id, aux, flags);
}

void QueueProductionOrderDynamicIconRecord(UiOverlayState& state, u32 object_id,
    u32 aux, u32 flags) {
    const u32 order_id = object_id >= 0xf4 ? object_id - 0xf4 : object_id;
    state.current_icon_marker =
        vector_value_or_zero(state.production_order_icon_markers, order_id);
    QueueUiOverlayDynamicIconRecord(state, object_id, aux, flags);
}

void QueueEquipmentDefinitionDynamicIconRecord(UiOverlayState& state, u32 object_id,
    u32 aux, u32 flags) {
    const u32 equipment_id = object_id >= 0x24a ? object_id - 0x24a : object_id;
    state.current_icon_marker =
        vector_value_or_zero(state.equipment_icon_markers, equipment_id);
    QueueUiOverlayDynamicIconRecord(state, object_id, aux, flags);
}

void append_fixed_interactive_record(
    UiOverlayState& state, const UiOverlayDrawRecord& record) {
    append_record(state, record);
    const UiOverlayCommandOption* option = find_command_option(state, record.item_id);
    append_hot_region(state, record, option != nullptr ? option->hotkey : 0,
        option == nullptr || option->enabled);
}

void append_offscreen_hotkey_record(
    UiOverlayState& state, u32 item_id, u32 aux, u32 flags) {
    // FUN_004e5621/FUN_004e56a2 store flag-1 commands at the screen
    // dimensions without advancing DAT_008663b4. They remain available to
    // the hotkey scan but do not occupy (or overlap) a dynamic icon slot.
    const UiOverlayRect rect{static_cast<i32>(state.screen_width),
        static_cast<i32>(state.screen_height), state.current_record_size,
        state.current_record_size};
    append_fixed_interactive_record(
        state, make_record(item_id, aux, flags, rect, 0));
}

void QueueUiOverlayManual26Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags, i32 x, i32 y) {
    append_fixed_interactive_record(
        state, make_record(item_id, aux, flags, {x, y, 0x26, 0x26}, 0));
}

void QueueUiOverlayManual26RecordAlternate(UiOverlayState& state, u32 item_id,
    u32 aux, u32 flags, i32 x, i32 y) {
    QueueUiOverlayManual26Record(state, item_id, aux, flags, x, y);
}

void QueueUiOverlayManual13Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags, i32 x, i32 y) {
    append_fixed_interactive_record(
        state, make_record(item_id, aux, flags, {x, y, 0x13, 0x13}, 0));
}

void QueueUiOverlayDynamicIconRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    UiOverlayRect rect = rect_or_full_screen(
        state, state.dynamic_icon_bounds, state.dynamic_icon_index);
    rect.width = state.current_record_size;
    rect.height = state.current_record_size;
    UiOverlayDrawRecord record =
        make_record(item_id, aux, flags, rect, state.current_icon_marker);
    append_record(state, record);
    const UiOverlayCommandOption* option = find_command_option(state, item_id);
    append_hot_region(state, record, option != nullptr ? option->hotkey : 0,
        option == nullptr || option->enabled);
    ++state.dynamic_icon_index;
}

void QueueUiOverlaySmallSlot1Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.small_slot1_bounds, 0x26, 0x26), 0));
}

void QueueUiOverlaySmallSlot2Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.small_slot2_bounds, 0x26, 0x26), 0));
}

void QueueUiOverlaySmallSlot3Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.small_slot3_bounds, 0x26, 0x26), 0));
}

void QueueUiOverlayLargeSlot0Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.large_slot0_bounds, 0x32, 0x32), 0));
}

void QueueUiOverlayLargeSlot3Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.large_slot3_bounds, 0x32, 0x32), 0));
}

void QueueUiOverlayLargeSlot6Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.large_slot6_bounds, 0x32, 0x32), 0));
}

void QueueUiOverlayLargeSlot9Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.large_slot9_bounds, 0x32, 0x32), 0));
}

void QueueUiOverlayLargeSlot12Record(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.large_slot12_bounds, 0x32, 0x32), 0));
}

void QueueUiOverlayWideSlotRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    state.current_record_size = 0x32;
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.wide_slot_bounds, 0x32, 0x32), 0));
    ++state.side_slot_index;
}

void QueueUiOverlaySideSlotRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags) {
    state.current_record_size = 0x26;
    const u32 index = std::min<u32>(
        state.side_slot_index, static_cast<u32>(state.side_slot_bounds.size() - 1));
    append_fixed_interactive_record(state, make_record(item_id, aux, flags,
        rect_or_default(state.side_slot_bounds[index], 0x26, 0x26), 0));
    ++state.side_slot_index;
}

void QueueUiOverlayIndexedSlotRecord(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags, u32 rect_index) {
    UiOverlayRect rect{0, 0, 0x26, 0x26};
    if (rect_index < state.indexed_slot_bounds.size()) {
        rect = rect_or_default(state.indexed_slot_bounds[rect_index], 0x26, 0x26);
    }
    // These records are actionable cancel entries.  Their flags dword is the
    // logical queue index and must survive unchanged into the published packet.
    append_fixed_interactive_record(
        state, make_record(item_id, aux, flags, rect, 0));
}

bool DrawUiOverlaySmallUnitIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record)) {
        return false;
    }
    UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, true);
    adjusted.width = adjusted.height = 0x26;
    const bool disabled = (adjusted.flags & kUiOverlayFlagDisabled) != 0;
    return draw_record_command_icon_blit(state, adjusted,
        UiOverlayIconBlitKind::base, adjusted.item_id, disabled);
}

bool DrawUiOverlayLargeUnitIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record)) {
        return false;
    }
    UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, false);
    adjusted.width = adjusted.height = 0x32;
    const bool disabled = (adjusted.flags & kUiOverlayFlagDisabled) != 0;
    return draw_record_command_icon_blit(state, adjusted,
        UiOverlayIconBlitKind::base, adjusted.item_id, disabled);
}

bool DrawUiOverlayObjectIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record) || record.item_id == 0xc8) {
        return false;
    }
    UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, false);
    adjusted.width = adjusted.height = 0x26;
    const bool disabled = (adjusted.flags & kUiOverlayFlagDisabled) != 0;
    const bool masked = !disabled && (adjusted.flags & 0x34u) != 0;
    // FUN_004e102f normalizes the dispatch id before selecting magic.trt:
    // ids 0xd4..0xf3 address frames 0..31, not frames 0xd4..0xf3.
    const u32 magic_frame = adjusted.item_id >= 0xd4u ?
        adjusted.item_id - 0xd4u : adjusted.item_id;
    return draw_record_command_icon_blit(state, adjusted,
        UiOverlayIconBlitKind::object, magic_frame, disabled, masked);
}

bool DrawUiOverlayUnitOrObjectIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record) || record.item_id == 0xc8) {
        return false;
    }
    UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, true);
    if (adjusted.item_id == 0xc6 && adjusted.width != 0x26) {
        adjusted.width = adjusted.height = 0x32;
        return draw_record_command_icon_blit(state, adjusted,
            UiOverlayIconBlitKind::base, 0xa9);
    }
    adjusted.width = adjusted.height = 0x26;
    const bool disabled = (adjusted.flags & kUiOverlayFlagDisabled) != 0;
    if (adjusted.item_id == 0xb5) {
        return draw_record_command_icon_blit(state, adjusted,
            UiOverlayIconBlitKind::unit, adjusted.aux, disabled);
    }
    const bool masked = !disabled && (adjusted.flags & 0x34u) != 0;
    const u32 action_frame = adjusted.item_id >= 0xaau ?
        adjusted.item_id - 0xaau : adjusted.item_id;
    return draw_record_command_icon_blit(state, adjusted,
        UiOverlayIconBlitKind::unit, action_frame, disabled, masked);
}

bool DrawUiOverlayEquipmentIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record)) {
        return false;
    }
    UiOverlayDrawRecord adjusted = record;
    adjusted.width = adjusted.height = 0x26;
    if (adjusted.item_id >= 0x24au) {
        // FUN_004e22d5 subtracts 0x24a and FUN_004e66c7 resolves the
        // equipment definition's icon-frame field before indexing item.trt.
        return draw_record_command_icon_blit(state, adjusted,
            UiOverlayIconBlitKind::equipment, adjusted.item_id);
    }
    const u32 entry_id = adjusted.item_id >= 0x134 ?
        adjusted.item_id - 0x134 : adjusted.item_id;
    if (!state.emit_sprite_draws) {
        return true;
    }
    UiOverlayIconBlitRequest request{};
    request.kind = UiOverlayIconBlitKind::base;
    request.item_id = entry_id;
    request.x = adjusted.x;
    request.y = adjusted.y;
    request.width = adjusted.width;
    request.height = adjusted.height;
    return draw_raw_command_icon_blit(state, request);
}

bool DrawUiOverlayProductionIconRecord(UiOverlayState& state,
    const UiOverlayDrawRecord& record) {
    if (!record_visible(state, record)) {
        return false;
    }
    UiOverlayDrawRecord adjusted = selected_adjusted_record(state, record, false);
    adjusted.width = adjusted.height = 0x26;
    const bool disabled = (adjusted.flags & kUiOverlayFlagDisabled) != 0;
    // FUN_004e1216 subtracts 0xf4 before indexing upgrade.trt.
    const u32 production_frame = adjusted.item_id >= 0xf4u ?
        adjusted.item_id - 0xf4u : adjusted.item_id;
    return draw_record_command_icon_blit(state, adjusted,
        UiOverlayIconBlitKind::production, production_frame, disabled);
}

void BlitUiOverlayBaseIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::base, item_id, x, y);
}

void BlitUiOverlayPaletteTableIcon(UiOverlayState& state, u32 palette_selector,
    u32 item_id, i32 x, i32 y) {
    state.current_palette_selector = palette_selector;
    append_icon_blit(state, UiOverlayIconBlitKind::palette_table, item_id, x, y);
}

void BlitUiOverlayUnitIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::unit, item_id, x, y);
}

void BlitUiOverlayObjectIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::object, item_id, x, y);
}

void BlitUiOverlayEquipmentIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::equipment, item_id, x, y);
}

void BlitUiOverlayEquipmentIconHalfSampled(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::equipment_half_sampled,
        item_id, x, y);
}

void BlitUiOverlayProductionIcon(UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::production, item_id, x, y);
}

void BlitUiOverlayUnitIconClipped(UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::unit_clipped, item_id, x, y);
}

void BlitUiOverlayBaseIconClipped(UiOverlayState& state, u32 item_id, i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::base_clipped, item_id, x, y);
}

void BlitUiOverlayEquipmentIconClipped(UiOverlayState& state, u32 item_id, i32 x,
    i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::equipment_clipped,
        item_id, x, y);
}

void BlitUiOverlayProductionIconClipped(UiOverlayState& state, u32 item_id, i32 x,
    i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::production_clipped,
        item_id, x, y);
}

void BlitUiOverlayDisabledBaseIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::disabled_base_clipped,
        item_id, x, y);
}

void BlitUiOverlayDisabledUnitIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::disabled_unit_clipped,
        item_id, x, y);
}

void BlitUiOverlayDisabledObjectIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::disabled_object_clipped,
        item_id, x, y);
}

void BlitUiOverlayDisabledProductionIconClipped(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::disabled_production_clipped,
        item_id, x, y);
}

void BlitUiOverlayMaskedDisabledBaseIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::masked_disabled_base,
        item_id, x, y);
}

void BlitUiOverlayMaskedDisabledUnitIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::masked_disabled_unit,
        item_id, x, y);
}

void BlitUiOverlayMaskedDisabledObjectIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::masked_disabled_object,
        item_id, x, y);
}

void BlitUiOverlayMaskedDisabledProductionIcon(UiOverlayState& state, u32 item_id,
    i32 x, i32 y) {
    append_icon_blit(state, UiOverlayIconBlitKind::masked_disabled_production,
        item_id, x, y);
}

void DrawUiLargeSlotDetailTexts(UiOverlayState& state) {
    if (state.detail_progress_total != 0) {
        append_progress(state, state.large_slot_x, state.large_slot_y,
            state.large_slot_x + 0x32, state.large_slot_y + 0x0d,
            state.detail_progress, state.detail_progress_total);
    }
    append_text(state, state.detail_primary_text, state.large_slot_x + 5,
        state.large_slot_y - 0x17, 1);
    append_text(state, state.detail_secondary_text, state.large_slot_x + 5,
        state.large_slot_y - 0x0c, 1);
    append_text(state, state.detail_clock_text, state.large_slot_x + 0x32,
        state.large_slot_y - 0x11, 1, true);
    append_text(state, state.detail_route_text, state.large_slot_x + 0x32,
        state.large_slot_y - 6, 1, true);
}

void NoOpUiOverlayDetailHandler(UiOverlayState&) {
}

std::string stat_delta_text(u32 value, u32 base) {
    if (base == 0 || value == base) {
        return {};
    }
    if (value > base) {
        return "+" + std::to_string(value - base);
    }
    return "-" + std::to_string(base - value);
}

void append_stat_text_with_delta(UiOverlayState& state, const char* label,
    u32 value, u32 base, i32 x, i32 y) {
    if (value == 0) {
        return;
    }
    const std::string value_text = std::string(label) + std::to_string(base);
    append_text(state, value_text, x, y, 0x11,
        false, false, false, 1, 3);
    const std::string delta = stat_delta_text(value, base);
    if (!delta.empty()) {
        const u8 color = value > base ? 0x41 : 9;
        // MeasureTextExtent (0x005021af) measures ASCII through the active
        // draw font, while its metric font is only used for DBCS glyphs.
        SelectTextDrawFont(1);
        SelectTextMetricFont(1);
        const i32 text_width = MeasureTextExtent(value_text.c_str())
            ? static_cast<i32>(text_renderer_state().measured_width)
            : static_cast<i32>(value_text.size() * 6u);
        const i32 delta_x = x + text_width;
        append_text(state, delta, delta_x, y, color,
            false, false, false, 1, 3);
    }
}

bool selected_unit_command_progress_active(const UiOverlayState& state) {
    const u32 command = state.selected_unit_command_state;
    return command == 0x51 || command == 0x50 ||
        command == 0x4e || command == 0x4d ||
        command == 0x83 || command == 0x82 ||
        state.selected_unit_action_mode_gate == 1;
}

void append_selected_unit_command_progress(UiOverlayState& state) {
    // FUN_004e2bb7 copies these three screen-width-specific endpoint pairs
    // from DAT_008642dc using DAT_00863588 (640, 800, or other width).  The
    // progress frame in FUN_004e1544 uses that same layout selector.
    constexpr std::array<UiOverlayRect, 3> kProgressBounds{{
        {77, 433, 98, 2}, {129, 537, 126, 2}, {77, 433, 98, 2},
    }};
    const u32 layout = std::min<u32>(state.screen_layout_bucket, 2);
    const UiOverlayRect& rect = kProgressBounds[layout];
    if (state.emit_sprite_draws &&
        state.glyph_resource_base != kInvalidResourceEntry) {
        // DAT_00868600 is the JW2_02 misc-icon base.  Each progress branch in
        // FUN_004e1544 draws base + layout + 0x24 three pixels outside the bar.
        DrawResourceSpriteNormal(state.glyph_resource_base + layout + 0x24u,
            rect.x - 3, rect.y - 3);
    }
    // FUN_004e1544 always draws the production/construction frame and then
    // returns from the detail panel, even when the definition duration is
    // zero.  Only the filled bar itself is skipped for a zero denominator.
    if (state.detail_progress_total != 0) {
        append_progress(state, rect.x, rect.y,
            rect.x + static_cast<i32>(rect.width),
            rect.y + static_cast<i32>(rect.height),
            state.detail_progress, state.detail_progress_total);
    }
}

void RenderSelectedUnitInfoPanel(UiOverlayState& state) {
    state.current_record_size = 0x32;
    BlitUiOverlayPaletteTableIcon(state, state.current_palette_selector,
        state.current_detail_item_id, state.large_slot_x, state.large_slot_y);
    append_text(state, state.selected_unit_name_text, state.large_slot_x + 0x38,
        state.large_slot_y + 1, 1, false, false, false, 4, 4);
    if ((state.selected_unit_runtime_flags & 0x20000000u) != 0) {
        append_text(state, state.selected_unit_indestructible_text,
            state.large_slot_x + 0x19, state.large_slot_y + 0x3a,
            0x41, true, false, false, 1, 3);
    }
    else if (state.selected_unit_health_ratio_max != 0) {
        append_text(state, ratio_text(state.selected_unit_health,
            state.selected_unit_health_ratio_max), state.large_slot_x + 0x19,
            state.large_slot_y + 0x3a,
            static_cast<u8>(state.selected_unit_health_text_color),
            true, false, false, 1, 3);
    }
    if (state.selected_unit_type < 0x60) {
        if (state.selected_unit_details_visible) {
            RenderSelectedUnitCargoLine(state);
        }
        RenderSelectedUnitBaseStatLine(state);
        RenderSelectedUnitMaxStatText(state);
        RenderSelectedUnitCapabilityLines(state);
        return;
    }

    RenderSelectedUnitCargoLine(state);
    if (state.selected_unit_details_visible) {
        if (selected_unit_command_progress_active(state)) {
            append_selected_unit_command_progress(state);
            return;
        }
        RenderSelectedUnitCapabilityLines(state);
        RenderSelectedUnitOrderStatLine(state);
    }
    RenderSelectedUnitMaxStatText(state);
}

void RenderSelectedUnitMaxStatText(UiOverlayState& state) {
    append_stat_text_with_delta(state, "OP ", state.selected_unit_max_health,
        state.selected_unit_base_max_health, state.large_slot_x + 0x3c,
        state.large_slot_y + 0x1a);
    append_stat_text_with_delta(state, "DP ", state.selected_unit_max_secondary,
        state.selected_unit_base_max_secondary, state.large_slot_x + 0x78,
        state.large_slot_y + 0x1a);
}

void RenderSelectedUnitCapabilityLines(UiOverlayState& state) {
    i32 y = state.large_slot_y + 0x1a;
    for (const std::string& line : state.selected_unit_capability_lines) {
        y += 10;
        append_text(state, line, state.large_slot_x + 0x3c, y, 0x41,
            false, false, false, 0, 3);
    }
}

void RenderSelectedUnitCargoLine(UiOverlayState& state) {
    if (state.selected_unit_secondary_line_enabled) {
        append_text(state, ratio_text(state.selected_unit_secondary,
            state.selected_unit_secondary_ratio_max), state.large_slot_x + 0x19,
            state.large_slot_y + 0x44, 0x51,
            true, false, false, 1, 3);
    }
}

void RenderSelectedUnitBaseStatLine(UiOverlayState& state) {
    append_text(state, state.selected_unit_owner_text, state.large_slot_x + 0x3c,
        state.large_slot_y + 0x0f, 0x11, false, false, false, 1, 3);
    append_text(state, state.selected_unit_experience_text,
        state.large_slot_x + 0x78, state.large_slot_y + 0x0e, 0x31,
        false, false, false, 1, 3);
}

void RenderSelectedUnitOrderStatLine(UiOverlayState& state) {
    append_text(state, state.selected_unit_order_text, state.large_slot_x + 0x3c,
        state.large_slot_y + 0x0f, 0x11, false, false, false, 1, 3);
    append_text(state, state.selected_unit_experience_text,
        state.large_slot_x + 0x78, state.large_slot_y + 0x0e, 0x31,
        false, false, false, 1, 3);
}

void DrawUiOverlayIconTextGlyphBase(UiOverlayState& state) {
    BlitUiOverlayBaseIcon(state, state.current_detail_item_id,
        state.small_slot1_x, state.small_slot1_y);
    append_text(state, std::to_string(state.current_icon_number),
        state.small_slot1_x + 0x21, state.small_slot1_y + 0x1e, 1);
}

void DrawUiOverlayIconTextGlyphProduction(UiOverlayState& state) {
    BlitUiOverlayProductionIcon(state, state.current_detail_item_id,
        state.small_slot1_x, state.small_slot1_y);
    append_text(state, std::to_string(state.current_icon_number),
        state.small_slot1_x + 0x21, state.small_slot1_y + 0x1e, 1);
}

void DrawUiOverlayIconTextGlyphEquipment(UiOverlayState& state) {
    BlitUiOverlayEquipmentIcon(state, state.current_detail_item_id,
        state.small_slot1_x, state.small_slot1_y);
    append_text(state, std::to_string(state.current_icon_number),
        state.small_slot1_x + 0x21, state.small_slot1_y + 0x1e, 1);
}

void DrawUiOverlayIndexedIconNumber(UiOverlayState& state, u32 item_id) {
    draw_equipment_command_icon_at(
        state, item_id, state.small_slot1_x, state.small_slot1_y);
}

void DrawUiOverlaySelectedUnitSlotNumber(UiOverlayState& state) {
    draw_selected_unit_slot_value_at(state, state.selected_unit_slot_value,
        state.small_slot1_x, state.small_slot1_y);
}

void DrawEquipmentDefinitionSpriteIfAvailable(UiOverlayState& state, u32 item_id) {
    if (item_id < 0x24a) {
        return;
    }
    const u32 equipment_id = item_id - 0x24a;
    BlitUiOverlayEquipmentIcon(state, equipment_id, state.small_slot1_x,
        state.small_slot1_y);
    const u32 marker_code = state.current_icon_marker != 0 ?
        state.current_icon_marker :
        vector_value_or_zero(state.equipment_icon_markers, equipment_id);
    const u32 marker_entry = marker_sprite_entry(state, marker_code);
    if (state.emit_sprite_draws && marker_entry != kInvalidResourceEntry) {
        DrawResourceSpriteDirectToken1Shadow(marker_entry,
            state.small_slot1_x + 0x1b, state.small_slot1_y + 0x1b);
    }
}

void RenderProductionPlacementPreviewOverlay(UiOverlayState& state) {
    if (state.placement_mode != 6 && state.placement_mode != 0x41) {
        return;
    }

    if (state.placement_mode == 0x41) {
        state.placement_definition_id = 0x1d;
    }

    // FUN_004e2338 calls FUN_004e3f43 at 0x004e2364 and returns on carry at
    // 0x004e2369.  A non-zero interface-background mask pixel therefore
    // suppresses both the placement sprite and every green/red footprint cell;
    // transparent holes in the interface remain valid preview space.
    if (CheckUiOverlayIconMaskPixel(state, state.mouse_x, state.mouse_y)) {
        return;
    }

    frame_callback(state, state.callbacks.draw_placement_preview);

    // The original draws the definition sprite first, then exits at
    // 0x004e23f1/0x004e23fe when either raw footprint dimension is zero.
    // Do not synthesize a one-cell invalid marker for those definitions.
    if (state.placement_footprint_width_tiles == 0 ||
        state.placement_footprint_height_tiles == 0) {
        return;
    }

    const i32 tile_x = (state.placement_pointer_x + state.camera_x) >> 5;
    const i32 tile_y = (state.placement_pointer_y + state.camera_y) >> 5;
    const i32 screen_x = (tile_x << 5) - state.camera_x;
    const i32 screen_y = (tile_y << 5) - state.camera_y;
    const u32 width = state.placement_footprint_width_tiles;
    const u32 height = state.placement_footprint_height_tiles;

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const std::size_t cell_index =
                static_cast<std::size_t>(y) * width + x;
            const bool cell_valid =
                cell_index < state.placement_preview_cell_validity.size()
                    ? state.placement_preview_cell_validity[cell_index] != 0
                    : state.placement_preview_valid;
            append_minimap_marker(state, UiOverlayMinimapMarkerKind::placement_preview,
                screen_x + static_cast<i32>(x * 0x20),
                screen_y + static_cast<i32>(y * 0x20), 0x20, 0x20,
                cell_valid ? 0x07e0 : 0xf800,
                state.placement_definition_id, state.local_player_slot,
                cell_valid);
        }
    }
}

void RenderGameplayMinimapOverlay(UiOverlayState& state) {
    ConfigureGameplayUiOverlayLayout(state);
    if (state.minimap.map_width_tiles == 0) {
        state.minimap.map_width_tiles = minimap_width_tiles(state);
    }
    if (state.minimap.map_height_tiles == 0) {
        state.minimap.map_height_tiles = minimap_height_tiles(state);
    }
    if (state.minimap.viewport_width_pixels == 0) {
        state.minimap.viewport_width_pixels = state.screen_width;
    }
    if (state.minimap.viewport_height_pixels == 0) {
        state.minimap.viewport_height_pixels = state.screen_height;
    }
    if (state.minimap.output_pitch_pixels == 0) {
        state.minimap.output_pitch_pixels = state.screen_width;
    }
    if (state.minimap.output_height_pixels == 0) {
        state.minimap.output_height_pixels = state.screen_height;
    }

    if (state.emit_sprite_draws && state.minimap_background_entry != 0) {
        BlitImageResourceNormal(state.minimap_background_entry,
            state.minimap.output_x, state.minimap.output_y);
    }

    CopyMinimapScratchToOutput(state.minimap);
    RenderMinimapObjectAndTerrainMarkers(state);
    RenderMinimapFogMask(state);
    RenderMinimapUnitMarkers(state);
    apply_minimap_markers_to_output(state);
    DrawMinimapViewportBorder(state.minimap, state.camera_x, state.camera_y);
    flush_minimap_output_to_backbuffer(state);
    flush_minimap_object_footprint_spill_to_backbuffer(state);
}

void RenderMinimapObjectAndTerrainMarkers(UiOverlayState& state) {
    const u32 width = minimap_width_tiles(state);
    const u32 height = minimap_height_tiles(state);
    if (width == 0 || height == 0) {
        return;
    }

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const u32 visibility = minimap_layer_value(
                state, state.minimap_visibility_flags, x, y);
            const bool currently_visible = (visibility & 0x08000000u) != 0;
            const bool explored = (visibility & 0x10000000u) != 0;
            if (!explored && !currently_visible) {
                continue;
            }

            const u32 object_flags = minimap_layer_value(
                state, state.minimap_object_flags, x, y);
            const u32 object_id = object_flags & 0xffu;
            const i32 screen_x = minimap_screen_x_for_tile(state, x);
            const i32 screen_y = minimap_screen_y_for_tile(state, y);

            if (object_id == 0) {
                const u32 overlay_flags = minimap_layer_value(
                    state, state.minimap_overlay_flags, x, y);
                if ((overlay_flags & 0x700u) == 0x100u) {
                    append_minimap_marker(state,
                        UiOverlayMinimapMarkerKind::terrain_overlay,
                        screen_x, screen_y, 1, 2, state.minimap_terrain_marker_color,
                        0, state.local_player_slot);
                }
                continue;
            }

            const u32 owner_id = (object_flags >> 8) & 0x0fu;
            FillMinimapFootprintMarker(state, object_id, screen_x, screen_y,
                minimap_owner_color(state, owner_id, true));
        }
    }
}

void RenderMinimapFogMask(UiOverlayState& state) {
    const u32 map_width = minimap_width_tiles(state);
    const u32 map_height = minimap_height_tiles(state);
    const u32 output_width = state.minimap.minimap_width_pixels;
    const u32 output_height = state.minimap.minimap_height_pixels;
    if (map_width == 0 || map_height == 0 || output_width == 0 ||
        output_height == 0 || state.reveal_minimap_fog) {
        return;
    }

    // FUN_004e26f3 visits each minimap output pixel once.  Its accumulators
    // select the last source tile covered by that pixel; this closed form is
    // floor((((pixel + 1) * map_dimension) - 1) / output_dimension).
    for (u32 pixel_y = 0; pixel_y < output_height; ++pixel_y) {
        const u32 tile_y = static_cast<u32>(
            ((static_cast<u64>(pixel_y + 1) * map_height) - 1) /
            output_height);
        for (u32 pixel_x = 0; pixel_x < output_width; ++pixel_x) {
            const u32 tile_x = static_cast<u32>(
                ((static_cast<u64>(pixel_x + 1) * map_width) - 1) /
                output_width);
            const u32 visibility = minimap_layer_value(
                state, state.minimap_visibility_flags, tile_x, tile_y);
            // FUN_004e26f3 treats bit 27 as current/full visibility and bit
            // 28 as the persistent explored/dim state.
            const bool currently_visible = (visibility & 0x08000000u) != 0;
            const bool explored = (visibility & 0x10000000u) != 0;
            if (!explored && !currently_visible) {
                append_minimap_marker(state, UiOverlayMinimapMarkerKind::fog_hidden,
                    state.minimap.output_x + static_cast<i32>(pixel_x),
                    state.minimap.output_y + static_cast<i32>(pixel_y), 1, 1,
                    state.minimap_hidden_color, 0, state.local_player_slot, false);
            } else if (!currently_visible) {
                append_minimap_marker(state, UiOverlayMinimapMarkerKind::fog_dimmed,
                    state.minimap.output_x + static_cast<i32>(pixel_x),
                    state.minimap.output_y + static_cast<i32>(pixel_y), 1, 1,
                    state.minimap_dim_mask, 0, state.local_player_slot, true);
            }
        }
    }
}

void FillMinimapFootprintMarker(UiOverlayState& state, u32 definition_id,
    i32 x, i32 y, u16 color) {
    const UiOverlayRect footprint = definition_footprint(state, definition_id);
    append_minimap_marker(state, UiOverlayMinimapMarkerKind::object_footprint,
        x, y, footprint.width, footprint.height, color, definition_id,
        state.local_player_slot);
}

void RenderMinimapUnitMarkers(UiOverlayState& state) {
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        // FUN_004e284a only draws mobile definitions. Structures already came
        // from FUN_004e24e9's remembered object/footprint layer above.
        if (!ShouldRenderMinimapUnitMarker(unit)) {
            continue;
        }
        const i32 x = state.minimap.output_x +
            MinimapWorldToScreenX(state.minimap, unit.world_x);
        const i32 y = state.minimap.output_y +
            MinimapWorldToScreenY(state.minimap, unit.world_y);
        const u16 color = minimap_owner_color(state, unit.owner_id);
        append_minimap_marker(state, UiOverlayMinimapMarkerKind::active_unit,
            x, y, 2, 2, color, unit.type_id, unit.owner_id);
    }
}

void DrawUiOverlayRectangleOutline(UiOverlayState&, i32 left, i32 top,
    i32 width, i32 height, u16 color) {
    DrawBackBufferRectangleOutline16(left, top, width, height, color);
}

void ClampCameraToMinimapPoint(UiOverlayState& state, i32 world_x, i32 world_y) {
    const i32 anchor_x = state.minimap_camera_anchor_x != 0 ?
        state.minimap_camera_anchor_x : static_cast<i32>(state.screen_width >> 1);
    const i32 anchor_y = state.minimap_camera_anchor_y != 0 ?
        state.minimap_camera_anchor_y : static_cast<i32>(state.screen_height >> 1);
    state.camera_x = std::clamp(world_x - anchor_x, 0, state.camera_max_x);
    state.camera_y = std::clamp(world_y - anchor_y, 0, state.camera_max_y);
}

void RenderGameplayResourceCounters(UiOverlayState& state) {
    if (state.scenario_ai_profile_override || state.local_player_type == 2) {
        return;
    }

    if (state.emit_sprite_draws && state.resource_icon_entry != 0) {
        DrawResourceSpriteNormal(state.resource_icon_entry,
            state.resource_counter_x, state.resource_counter_y);
    }
    append_text(state, std::to_string(state.resource_amount),
        state.resource_counter_x + 0x12, state.resource_counter_y + 2,
        1, false, false, false, 1, kUiOverlayPreserveMetricFont);

    if (state.emit_sprite_draws && state.population_icon_entry != 0) {
        DrawResourceSpriteNormal(state.population_icon_entry,
            state.population_counter_x, state.population_counter_y);
    }
    const u8 used_color =
        (state.population_available < state.population_used ||
            state.population_limit < state.population_used) ? 9 : 1;
    const auto measure_counter_text = [](const std::string& text) -> i32 {
        SelectTextDrawFont(1);
        i32 width = 0;
        for (const unsigned char ch : text) {
            if (MeasureAsciiGlyphMetrics(ch)) {
                width += static_cast<i32>(
                    text_renderer_state().measured_width);
            }
        }
        return width;
    };
    const std::string used_text = std::to_string(state.population_used);
    i32 text_x = state.population_counter_x + 0x12;
    append_text(state, used_text, text_x, state.population_counter_y + 2,
        used_color, false, false, false, 1, kUiOverlayPreserveMetricFont);
    text_x += measure_counter_text(used_text);
    append_text(state, "/", text_x, state.population_counter_y + 2,
        1, false, false, false, 1, kUiOverlayPreserveMetricFont);
    text_x += measure_counter_text("/");
    const bool available_capped =
        state.population_limit < state.population_available;
    const u8 available_color = available_capped ? 0x11 : 1;
    const u32 displayed_available = available_capped ?
        state.population_limit : state.population_available;
    append_text(state, std::to_string(displayed_available),
        text_x, state.population_counter_y + 2, available_color,
        false, false, false, 1, kUiOverlayPreserveMetricFont);
}

void StartGameplayHudPulse(UiOverlayState& state, i32 world_x, i32 world_y, u32 tick_ms) {
    state.hud_pulse_phase = 0;
    state.hud_pulse_x = world_x;
    state.hud_pulse_y = world_y;
    state.hud_pulse_start_frame = tick_ms;
}

void StopGameplayHudPulse(UiOverlayState& state) {
    state.hud_pulse_phase = 0x0f;
}

void RenderGameplayHudPulse(UiOverlayState& state, u32 tick_ms) {
    if (state.hud_pulse_phase >= 8) {
        return;
    }
    const i32 screen_x = state.hud_pulse_x - state.camera_x;
    const i32 screen_y = state.hud_pulse_y - state.camera_y;
    UiOverlayHudPulseCommand command{};
    command.x = screen_x;
    command.y = screen_y;
    command.phase = state.hud_pulse_phase;
    command.frame_counter = tick_ms;
    state.pulse_commands.push_back(command);
    if (state.emit_sprite_draws &&
        state.command_ack_resource_base != kInvalidResourceEntry) {
        // FUN_004e2b61 indexes DAT_0086351c[phase] (0..7) from the
        // nine-image JW2_02 record-0x12a sequence.  Active phases are
        // 0, 2, 4 and 6.
        DrawResourceSpriteNormal(
            state.command_ack_resource_base + state.hud_pulse_phase,
            screen_x, screen_y);
    }
    if (tick_ms != state.hud_pulse_start_frame) {
        state.hud_pulse_start_frame = tick_ms;
        state.hud_pulse_phase += 2;
    }
}

void ConfigureGameplayUiOverlayLayout(UiOverlayState& state) {
    state.screen_layout_bucket = 0;
    if (state.screen_width != 0x280) {
        state.screen_layout_bucket = state.screen_width == 800 ? 1 : 2;
    }

    // DAT_0083f3b8 / DAT_0086358c are the camera-center anchors consumed by
    // FUN_004e29d1.  The vertical anchor is a separate layout-table field,
    // not the top edge of the lower HUD artwork.
    constexpr std::array<std::array<i32, 4>, 3> kWorldViewportHeight{{
        {{356, 362, 356, 364}},
        {{439, 436, 421, 452}},
        {{446, 458, 447, 460}},
    }};
    const u32 camera_layout = std::min<u32>(state.screen_layout_bucket, 2);
    const u32 camera_theme = std::min<u32>(state.interface_theme_index, 3);
    state.world_viewport_height = static_cast<u32>(
        kWorldViewportHeight[camera_layout][camera_theme]);
    state.minimap_camera_anchor_x = static_cast<i32>(state.screen_width / 2);
    state.minimap_camera_anchor_y =
        static_cast<i32>(state.world_viewport_height >> 1);

    // FUN_004e2bb7 selects these records by display-layout bucket first and
    // interface theme second.  Each source record contains an outer anchor,
    // an inner rectangle origin, and its exclusive end; bucket 2 proves the
    // outer anchor cannot be reconstructed from the inner rectangle.
    using FixedSlotRecord = std::array<i32, 6>;
    constexpr std::array<std::array<FixedSlotRecord, 4>, 3> kSmallSlot1{{
        {{{364, 372, 364, 372, 429, 398},
          {364, 372, 364, 372, 429, 398},
          {364, 372, 364, 372, 429, 398},
          {364, 372, 364, 372, 429, 398}}},
        {{{460, 464, 460, 464, 541, 495},
          {460, 467, 460, 467, 544, 496},
          {467, 467, 467, 467, 547, 494},
          {457, 468, 457, 468, 543, 494}}},
        {{{390, 636, 399, 648, 456, 672},
          {396, 641, 401, 652, 451, 669},
          {396, 641, 401, 652, 451, 669},
          {396, 643, 397, 650, 455, 669}}},
    }};
    constexpr std::array<std::array<FixedSlotRecord, 4>, 3> kSmallSlot2{{
        {{{213, 375, 213, 375, 242, 394},
          {213, 375, 213, 375, 242, 394},
          {213, 375, 213, 375, 242, 394},
          {213, 375, 213, 375, 242, 394}}},
        {{{273, 464, 273, 464, 313, 495},
          {273, 466, 273, 466, 314, 496},
          {276, 467, 276, 467, 305, 494},
          {269, 468, 269, 468, 308, 494}}},
        {{{542, 641, 555, 649, 605, 671},
          {544, 644, 556, 653, 598, 672},
          {544, 644, 556, 653, 598, 672},
          {542, 646, 554, 651, 598, 669}}},
    }};
    constexpr std::array<std::array<FixedSlotRecord, 4>, 3> kSmallSlot3{{
        {{{178, 372, 178, 372, 209, 396},
          {178, 372, 178, 372, 209, 396},
          {178, 372, 178, 372, 209, 396},
          {178, 372, 178, 372, 209, 396}}},
        {{{230, 464, 230, 464, 266, 495},
          {229, 467, 229, 467, 268, 496},
          {227, 467, 227, 467, 271, 494},
          {227, 468, 227, 468, 265, 494}}},
        {{{384, 722, 392, 736, 452, 756},
          {385, 721, 388, 731, 445, 765},
          {385, 721, 388, 731, 445, 765},
          {384, 722, 388, 734, 449, 752}}},
    }};
    const u32 panel_layout = std::min<u32>(state.screen_layout_bucket, 2);
    const u32 theme = std::min<u32>(state.interface_theme_index, 3);
    const auto apply_fixed_slot = [](const FixedSlotRecord& source,
                                     i32& outer_x, i32& outer_y,
                                     UiOverlayRect& inner) {
        outer_x = source[0];
        outer_y = source[1];
        inner = {source[2], source[3],
            static_cast<u32>(source[4] - source[2]),
            static_cast<u32>(source[5] - source[3])};
    };
    apply_fixed_slot(kSmallSlot1[panel_layout][theme],
        state.small_slot1_x, state.small_slot1_y, state.small_slot1_bounds);
    apply_fixed_slot(kSmallSlot2[panel_layout][theme],
        state.small_slot2_x, state.small_slot2_y, state.small_slot2_bounds);
    apply_fixed_slot(kSmallSlot3[panel_layout][theme],
        state.small_slot3_x, state.small_slot3_y, state.small_slot3_bounds);

    constexpr std::array<FixedSlotRecord, 5> kLargeSlot800{{
        {570, 450, 570, 450, 608, 488},
        {616, 450, 616, 450, 654, 488},
        {662, 450, 662, 450, 700, 488},
        {708, 450, 708, 450, 746, 488},
        {754, 450, 754, 450, 792, 488},
    }};
    std::array<FixedSlotRecord, 5> large_slots{};
    if (panel_layout == 0) {
        large_slots.fill(kSmallSlot3[0][theme]);
    } else if (panel_layout == 1) {
        large_slots = kLargeSlot800;
    } else {
        large_slots.fill(kSmallSlot3[2][theme]);
    }
    apply_fixed_slot(large_slots[0], state.large_slot_x, state.large_slot_y,
        state.large_slot0_bounds);
    apply_fixed_slot(large_slots[1], state.large_slot3_x, state.large_slot3_y,
        state.large_slot3_bounds);
    apply_fixed_slot(large_slots[2], state.large_slot4_x, state.large_slot4_y,
        state.large_slot6_bounds);
    apply_fixed_slot(large_slots[3], state.large_slot5_x, state.large_slot5_y,
        state.large_slot9_bounds);
    apply_fixed_slot(large_slots[4], state.large_slot6_x, state.large_slot6_y,
        state.large_slot12_bounds);
        // FUN_004e2bb7 selects all four panel-coordinate tables with
        // DAT_00863588.  That global is the three-way display-layout bucket,
        // not the interface-resource/tribe theme used by the small fixed
        // controls above.  A live 800x600 original reports DAT_00863588=1
        // (and copies row-1 x/y) even when the peer reconstruction's resource
        // theme is 2.  The original tables are:
        // 0x0086444c (16 dynamic slots), 0x008645fc (six 0x32 slots),
        // 0x00864170 (14 side slots), and 0x00864354 (five queue slots).
        // Keeping the display-layout index shared is important: layout 2
        // reuses layout 0's production grid while its side-selection strip is
        // at the top.
        constexpr std::array<std::array<UiOverlayRect, 16>, 3>
            kDynamicIconBounds{{
                {{{370, 407, 38, 38}, {403, 407, 38, 38},
                    {436, 407, 38, 38}, {469, 407, 38, 38},
                    {502, 407, 38, 38}, {535, 407, 38, 38},
                    {568, 407, 38, 38}, {601, 407, 38, 38},
                    {370, 440, 38, 38}, {403, 440, 38, 38},
                    {436, 440, 38, 38}, {469, 440, 38, 38},
                    {502, 440, 38, 38}, {535, 440, 38, 38},
                    {568, 440, 38, 38}, {601, 440, 38, 38}}},
                {{{464, 513, 38, 38}, {505, 513, 38, 38},
                    {546, 513, 38, 38}, {587, 513, 38, 38},
                    {628, 513, 38, 38}, {669, 513, 38, 38},
                    {710, 513, 38, 38}, {751, 513, 38, 38},
                    {464, 554, 38, 38}, {505, 554, 38, 38},
                    {546, 554, 38, 38}, {587, 554, 38, 38},
                    {628, 554, 38, 38}, {669, 554, 38, 38},
                    {710, 554, 38, 38}, {751, 554, 38, 38}}},
                {{{370, 407, 38, 38}, {403, 407, 38, 38},
                    {436, 407, 38, 38}, {469, 407, 38, 38},
                    {502, 407, 38, 38}, {535, 407, 38, 38},
                    {568, 407, 38, 38}, {601, 407, 38, 38},
                    {370, 440, 38, 38}, {403, 440, 38, 38},
                    {436, 440, 38, 38}, {469, 440, 38, 38},
                    {502, 440, 38, 38}, {535, 440, 38, 38},
                    {568, 440, 38, 38}, {601, 440, 38, 38}}},
            }};
        constexpr std::array<std::array<UiOverlayRect, 6>, 3>
            kCommandSlotBounds{{
                {{{373, 421, 50, 50}, {416, 421, 50, 50},
                    {459, 421, 50, 50}, {502, 421, 50, 50},
                    {545, 421, 50, 50}, {588, 421, 50, 50}}},
                {{{468, 530, 50, 50}, {521, 530, 50, 50},
                    {574, 530, 50, 50}, {627, 530, 50, 50},
                    {680, 530, 50, 50}, {733, 530, 50, 50}}},
                {{{373, 421, 50, 50}, {416, 421, 50, 50},
                    {459, 421, 50, 50}, {502, 421, 50, 50},
                    {545, 421, 50, 50}, {588, 421, 50, 50}}},
            }};
        constexpr std::array<std::array<UiOverlayRect, 14>, 3>
            kSideSlotBounds{{
                {{{12, 407, 38, 38}, {12, 440, 38, 38},
                    {45, 407, 38, 38}, {45, 440, 38, 38},
                    {78, 407, 38, 38}, {78, 440, 38, 38},
                    {111, 407, 38, 38}, {111, 440, 38, 38},
                    {144, 407, 38, 38}, {144, 440, 38, 38},
                    {177, 407, 38, 38}, {177, 440, 38, 38},
                    {210, 407, 38, 38}, {210, 440, 38, 38}}},
                {{{15, 513, 38, 38}, {15, 554, 38, 38},
                    {56, 513, 38, 38}, {56, 554, 38, 38},
                    {97, 513, 38, 38}, {97, 554, 38, 38},
                    {138, 513, 38, 38}, {138, 554, 38, 38},
                    {179, 513, 38, 38}, {179, 554, 38, 38},
                    {220, 513, 38, 38}, {220, 554, 38, 38},
                    {261, 513, 38, 38}, {261, 554, 38, 38}}},
                {{{12, 73, 38, 38}, {12, 40, 38, 38},
                    {45, 73, 38, 38}, {45, 40, 38, 38},
                    {78, 73, 38, 38}, {78, 40, 38, 38},
                    {111, 73, 38, 38}, {111, 40, 38, 38},
                    {144, 73, 38, 38}, {144, 40, 38, 38},
                    {177, 73, 38, 38}, {177, 40, 38, 38},
                    {210, 73, 38, 38}, {210, 40, 38, 38}}},
            }};
        constexpr std::array<std::array<UiOverlayRect, 5>, 3>
            kIndexedSlotBounds{{
                {{{74, 440, 38, 38}, {113, 440, 38, 38},
                    {146, 440, 38, 38}, {179, 440, 38, 38},
                    {212, 440, 38, 38}}},
                {{{84, 537, 38, 38}, {132, 549, 38, 38},
                    {175, 549, 38, 38}, {218, 549, 38, 38},
                    {261, 549, 38, 38}}},
                {{{74, 440, 38, 38}, {113, 440, 38, 38},
                    {146, 440, 38, 38}, {179, 440, 38, 38},
                    {212, 440, 38, 38}}},
            }};
        state.dynamic_icon_bounds = kDynamicIconBounds[panel_layout];
        state.command_slot_bounds.fill({});
        std::copy(kCommandSlotBounds[panel_layout].begin(),
            kCommandSlotBounds[panel_layout].end(),
            state.command_slot_bounds.begin());
        state.side_slot_bounds.fill({});
        std::copy(kSideSlotBounds[panel_layout].begin(),
            kSideSlotBounds[panel_layout].end(),
            state.side_slot_bounds.begin());
        state.indexed_slot_bounds.assign(
            kIndexedSlotBounds[panel_layout].begin(),
            kIndexedSlotBounds[panel_layout].end());
        // 0x004e2f8e hardcodes this anchor independently of the display
        // bucket; only the surrounding panel tables are bucket-selected.
        state.wide_slot_bounds = {19, 518, 50, 50};

    state.minimap.map_width_tiles = minimap_width_tiles(state);
    state.minimap.map_height_tiles = minimap_height_tiles(state);
    if (state.minimap.minimap_width_pixels == 0) {
        const u32 capacity = state.screen_layout_bucket == 0 ? 0x5fu : 0x73u;
        state.minimap.minimap_width_pixels =
            std::min<u32>(capacity,
                std::max<u32>(1, state.minimap.map_width_tiles));
    }
    if (state.minimap.minimap_height_pixels == 0) {
        const u32 capacity = state.screen_layout_bucket == 0 ? 0x5fu : 0x73u;
        state.minimap.minimap_height_pixels =
            std::min<u32>(capacity,
                std::max<u32>(1, state.minimap.map_height_tiles));
    }
    if (state.minimap.scale_percent == 0) {
        const u32 map_width = std::max<u32>(1, state.minimap.map_width_tiles);
        state.minimap.scale_percent =
            std::max<u32>(1, (state.minimap.minimap_width_pixels * 100) / map_width);
    }
    if (state.camera_max_x == 0 && state.map_width_tiles != 0) {
        state.camera_max_x = static_cast<i32>(
            std::max<i32>(0, static_cast<i32>(state.map_width_tiles * 0x20) -
                static_cast<i32>(state.screen_width)));
    }
    if (state.camera_max_y == 0 && state.map_height_tiles != 0) {
        // FUN_004e2bb7 writes DAT_0086359c from the map height minus the
        // interface-theme table at DAT_008635a0, not minus the physical
        // display height.  The lower HUD remains outside the scrollable world
        // viewport at every display-layout bucket.
        constexpr std::array<i32, 4> kCameraEffectiveHeight{
            {496, 487, 487, 480}};
        state.camera_max_y = static_cast<i32>(
            std::max<i32>(0, static_cast<i32>(state.map_height_tiles * 0x20) -
                kCameraEffectiveHeight[theme]));
    }
    if (state.resource_counter_x == 0) {
        state.resource_counter_x = static_cast<i32>(state.screen_width) - 0x8c;
    }
    if (state.population_counter_x == 0) {
        state.population_counter_x = static_cast<i32>(state.screen_width) - 0x46;
    }
    for (std::size_t i = 0; i < state.manual_equipment_slot_bounds.size(); ++i) {
        if (state.manual_equipment_slot_bounds[i].width == 0 ||
            state.manual_equipment_slot_bounds[i].height == 0) {
            state.manual_equipment_slot_bounds[i] = kDefaultManualEquipmentSlotBounds[i];
        }
    }

    const bool pixel_mode_555 = SurfacePixelMode555();
    state.minimap_terrain_marker_color = pixel_mode_555 ? 0x0a5f : 0x149f;
    state.minimap_local_unit_color = pixel_mode_555 ? 0x03e2 : 0x07c2;
    state.minimap_local_footprint_color = pixel_mode_555 ? 0x02e2 : 0x05e2;
    state.minimap_remote_unit_color = pixel_mode_555 ? 0x02e2 : 0x05e2;
    state.minimap_remote_footprint_color = pixel_mode_555 ? 0x02e2 : 0x05e2;
    // DAT_01440000 in FUN_004e26f3 is the repeated half-brightness mask.
    state.minimap_dim_mask = pixel_mode_555 ? 0x7bde : 0xf7de;
}

void ResetUiOverlayCommandPanelState(UiOverlayState& state) {
    state.hot_regions.clear();
    state.queued_records.clear();
    state.dispatched_records.clear();
    state.icon_blit_requests.clear();
    state.command_slot_count = 0;
    state.command_icon_marker = 0;
    state.dynamic_icon_index = 0;
    state.side_slot_index = 0;
    state.selected_production_gate_masks = {};
    state.selected_production_class_counts = {};
    state.last_hotkey_command = 0;
    state.last_hotkey_aux = 0;
    state.last_hotkey_flags = 0;
    state.last_hotkey_hover_kind = 0;
}

bool HitTestUiOverlayHotRegion(UiOverlayState& state, i32 x, i32 y) {
    state.last_hot_region_x = x;
    state.last_hot_region_y = y;
    if (const UiOverlayHotRegion* region = hot_region_at(state, x, y)) {
        set_hot_region_result(state, *region);
        return true;
    }
    return false;
}

bool CheckUiOverlayCommandRecordEnabled(const UiOverlayState& state, u32 item_id) {
    const UiOverlayCommandOption* option = find_command_option(state, item_id);
    if (option != nullptr) {
        return option->enabled &&
            (is_indexed_queue_command_item(item_id) ||
                (option->flags & kUiOverlayFlagDisabled) == 0);
    }
    for (const UiOverlayHotRegion& region : state.hot_regions) {
        if (region.record.item_id == item_id) {
            return region.enabled &&
                (is_indexed_queue_command_item(item_id) ||
                    (region.record.flags & kUiOverlayFlagDisabled) == 0);
        }
    }
    return true;
}

u32 ResolveUiOverlayHotkeyCommand(UiOverlayState& state, u8 key) {
    const u8 normalized = uppercase_hotkey(key);
    for (const UiOverlayHotRegion& region : state.hot_regions) {
        if (uppercase_hotkey(region.hotkey) == normalized && region.enabled &&
            (is_indexed_queue_command_item(region.record.item_id) ||
                (region.record.flags & kUiOverlayFlagDisabled) == 0)) {
            set_hot_region_result(state, region);
            return region.record.item_id;
        }
    }
    return 0;
}

bool HitTestUiOverlayHotRegionFromPointer(UiOverlayState& state, i32 x, i32 y) {
    return HitTestUiOverlayHotRegion(state, x, y);
}

bool CheckUiOverlayIconMaskPixel(const UiOverlayState& state, i32 x, i32 y) {
    if (x < 0 || y < 0) {
        return false;
    }
    if (state.small_icon_resource_base != kInvalidResourceEntry) {
        if (const ResourceStoreEntry* entry =
                GetResourceEntry(state.small_icon_resource_base)) {
            const u32 width = entry->metadata[0];
            const u32 height = entry->metadata[1];
            const i32 top = state.screen_height > height ?
                static_cast<i32>(state.screen_height - height) : 0;
            const i32 local_y = y - top;
            if (width != 0 && height != 0 && x < static_cast<i32>(width) &&
                local_y >= 0 && local_y < static_cast<i32>(height)) {
                const std::size_t offset =
                    static_cast<std::size_t>(local_y) * width +
                    static_cast<std::size_t>(x);
                return offset < entry->payload.size() && entry->payload[offset] != 0;
            }
        }
    }
    const auto contains_point = [x, y](const UiOverlayDrawRecord& record) {
        return record_contains_point(record, x, y);
    };
    return std::any_of(state.queued_records.begin(), state.queued_records.end(),
               contains_point) ||
        std::any_of(state.dispatched_records.begin(), state.dispatched_records.end(),
            contains_point);
}

void BuildSelectedUnitCommandPanel(UiOverlayState& state) {
    ResetUiOverlayCommandPanelState(state);
    QueueDefaultGameplayCommandSlots(state);
    // FUN_004e5292 runs before both placement-mode and selected-unit command
    // branches.  For a single selection it publishes the 0x1a6 portrait first,
    // followed by any active/deferred production queue records.
    if (state.selected_unit_count == 1) {
        QueueUiOverlayWideSlotRecord(
            state, 0x1a6, state.selected_unit_id, 0);
    }
    else if (state.selected_unit_count > 1) {
        for (u32 unit_id : state.selected_unit_ids) {
            if (find_unit_by_id(state, unit_id) != nullptr) {
                QueueUiOverlaySideSlotRecord(state, 0x1a8, unit_id, 0);
            }
        }
    }
    QueueSelectedUnitCurrentOrderButtons(state);
    if (state.placement_mode != 0) {
        state.command_slot_size = 0x32;
        AppendUiOverlayCommandSlot(state, 0xc6u, 0, 0);
        return;
    }
    if (state.selected_unit_count == 0) {
        return;
    }
    // 0x004e4020..0x004e4048 keeps FUN_004e5292's portrait/queue-info pass,
    // then returns before every mobile/structure command builder unless the
    // scenario override, player-type-2 override, or local ownership applies.
    // In particular, an enemy structure must not expose production actions or
    // the construction-cancel c6 record.
    if (!state.scenario_ai_profile_override && state.local_player_type != 2u &&
        state.selected_unit_owner != state.local_player_slot) {
        return;
    }
    if (state.selected_unit_type >= 0x60u &&
        state.selected_unit_action_mode_gate == 1) {
        // FUN_004e3f6e's construction branch at 0x004e4e93 does not build
        // the structure's ordinary production/action list.  It tail-calls
        // FUN_004e5762 with EAX=0xc6, ECX=3 and record size 0x32, leaving a
        // single large cancel button in the command panel.
        state.command_slot_size = 0x32;
        AppendUiOverlayCommandSlot(state, 0xc6u, 3, 0);
        return;
    }
    if (state.selected_unit_type < 0x60) {
        BuildMultiSelectedUnitCommandPanel(state);
        return;
    }
    BuildSingleSelectedUnitCommandPanel(state);
}

u32 CountSelectedUnitsOfType(const UiOverlayState& state, u32 type_id) {
    u32 count = 0;
    for (u32 unit_id : state.selected_unit_ids) {
        const UiOverlayMinimapUnit* unit = find_unit_by_id(state, unit_id);
        if (unit != nullptr && unit->type_id == type_id) {
            ++count;
        }
    }
    return count;
}

u32 CheckGroupedMorphCommandDisabled(const UiOverlayState& state,
    u32 command_type, u32 owner_id) {
    if (command_type >= state.grouped_command_gate_definitions.size()) {
        return 0x02;
    }

    const UiOverlayGroupedCommandGateDefinition& gate =
        state.grouped_command_gate_definitions[command_type];
    if (gate.order_variant_gate != 0) {
        if (owner_id >= state.grouped_command_order_variant_counts.size() ||
            gate.order_variant_index >=
                state.grouped_command_order_variant_counts[owner_id].size() ||
            state.grouped_command_order_variant_counts[owner_id]
                [gate.order_variant_index] == 0) {
            return 0x02;
        }
    }

    if (gate.completed_type_gate != 0) {
        if (owner_id >= state.grouped_command_completed_type_counts.size() ||
            gate.completed_type_index >=
                state.grouped_command_completed_type_counts[owner_id].size() ||
            state.grouped_command_completed_type_counts[owner_id]
                [gate.completed_type_index] == 0) {
            return 0x02;
        }
    }

    return 0;
}

void QueueGroupedSpecialUnitCommandIfPresent(UiOverlayState& state,
    u32 source_type, u32 command_type) {
    if (CountSelectedUnitsOfType(state, source_type) < 2) {
        return;
    }
    QueueUiOverlayCommandRecordByItemId(state, 0xb5, command_type,
        CheckGroupedMorphCommandDisabled(
            state, command_type, state.selected_unit_owner));
}

void QueueGroupedSpecialUnitCommands(UiOverlayState& state) {
    if ((state.selected_unit_command_bit_mask & 0x800u) == 0) {
        return;
    }

    QueueGroupedSpecialUnitCommandIfPresent(state, 0x22, 0x23);
    QueueGroupedSpecialUnitCommandIfPresent(state, 0x25, 0x26);
    QueueGroupedSpecialUnitCommandIfPresent(state, 0x27, 0x2d);
    if (CountSelectedUnitsOfType(state, 0x24) != 0 &&
        CountSelectedUnitsOfType(state, 0x27) != 0 &&
        CountSelectedUnitsOfType(state, 0x28) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xb5, 0x2b,
            CheckGroupedMorphCommandDisabled(
                state, 0x2b, state.local_player_slot));
    }
}

void QueueDefaultGameplayCommandSlots(UiOverlayState& state) {
    QueueUiOverlayCommandRecordByItemId(state, 0x194, 0, 0);
    QueueUiOverlayCommandRecordByItemId(state, 0x195, 1, 0);
    QueueUiOverlayCommandRecordByItemId(state, 0x196, 2, 0);
    if (state.scenario_ai_profile_override) {
        QueueUiOverlayCommandRecordByItemId(state,
            state.direct_music_paused ? 0x197 : 0x198,
            state.direct_music_paused ? 3 : 4, 0);
        QueueUiOverlayCommandRecordByItemId(state, 0x199, 5,
            (state.direct_music_available || state.replay_speed_index == 0) ?
                kUiOverlayFlagDisabled : 0);
        QueueUiOverlayCommandRecordByItemId(state, 0x19a, 6,
            (state.direct_music_available || state.replay_speed_index >= 6) ?
                kUiOverlayFlagDisabled : 0);
        QueueUiOverlayCommandRecordByItemId(state, 0x19b, 7,
            state.direct_music_available ? 0 : kUiOverlayFlagDisabled);
        QueueUiOverlayCommandRecordByItemId(state, 0x19c, 8,
            state.replay_vpos_available ? 0 : kUiOverlayFlagDisabled);
    }
}

void QueueSelectedUnitCoreActionButtons(UiOverlayState& state) {
    const u32 mask = state.selected_unit_command_bit_mask;
    if (state.selected_unit_type >= 0x60u) {
        if ((mask & 0x20u) != 0) {
            QueueUiOverlayCommandRecordByItemId(state, 0xafu, 0, 0);
        }
        if ((mask & 0x01u) != 0) {
            QueueUiOverlayCommandRecordByItemId(state, 0xaau, 0, 0);
        }
        return;
    }

    if ((mask & 0x20u) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xafu, 0, 0);
        QueueUiOverlayCommandRecordByItemId(state, 0xb7u, 0, 0);
    }
    if ((mask & 0x200u) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xb3u, 0, 0);
    }
    if ((mask & 0x01u) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xaau, 0, 0);
    }
    QueueUiOverlayCommandRecordByItemId(state, 0xcbu, 0, 0);
    QueueUiOverlayCommandRecordByItemId(state,
        (mask & 0x10u) != 0 ? 0xaeu : 0xc8u, 0, kUiOverlayFlagHidden);
    constexpr std::array<u32, 4> kPrimaryActions{
        0xbcu, 0xbdu, 0xbeu, 0xbfu};
    constexpr std::array<u32, 4> kSecondaryActions{
        0xd0u, 0xd1u, 0xd2u, 0xd3u};
    for (u32 index = 0; index < kPrimaryActions.size(); ++index) {
        const u32 action_state =
            state.selected_unit_special_action_states[index];
        if (action_state == 0) {
            continue;
        }
        if ((action_state & 1u) != 0) {
            QueueUiOverlayCommandRecordByItemId(
                state, kPrimaryActions[index], 0, 0);
        }
        else if ((action_state & 0x10u) != 0) {
            QueueUiOverlayCommandRecordByItemId(
                state, kPrimaryActions[index], 0, kUiOverlayFlagAlternateC);
        }
        else if ((action_state & 4u) == 0) {
            QueueUiOverlayCommandRecordByItemId(
                state, kPrimaryActions[index], 0, kUiOverlayFlagDisabled);
        }
        if ((action_state & 2u) != 0) {
            QueueUiOverlayCommandRecordByItemId(
                state, kSecondaryActions[index], 0, 0);
        }
    }
    if ((mask & 0x20000u) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xbbu, 0,
            state.selected_unit_order_2a_available ? 0 : kUiOverlayFlagDisabled);
    }
    if ((mask & 0x08000000u) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xc5u, 0, 0);
    }
    QueueGroupedSpecialUnitCommands(state);
    if ((state.selected_unit_command_flags & 0x800u) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xf3u, 0, 0);
    }
    if ((mask & 0x400u) != 0) {
        QueueUiOverlayCommandRecordByItemId(state, 0xb4u, 0, 0);
        QueueUiOverlayCommandRecordByItemId(state, 0xceu, 0, 0);
    }
}

void QueueSingleSelectedUnitHeldItemSlots(UiOverlayState& state) {
    // FUN_004e4150 publishes these seven fixed records only for a single
    // mobile-unit selection.  ECX is the selected unit's raw pool offset for
    // every record; the draw handlers then read the live +0x2c amount and the
    // six +0x30..+0x44 equipment fields from the selected-unit snapshot.
    if (state.selected_unit_count != 1) {
        return;
    }
    if (state.selected_unit_slot_value != 0) {
        QueueUiOverlayCommandRecordByItemId(
            state, 0x1adu, state.selected_unit_id, 0);
    }
    constexpr std::array<std::size_t, 6> kStorageOrder{4, 5, 0, 1, 2, 3};
    for (std::size_t ui_index = 0; ui_index < kStorageOrder.size(); ++ui_index) {
        if (state.selected_unit_equipment_slots[kStorageOrder[ui_index]] == 0) {
            continue;
        }
        QueueUiOverlayCommandRecordByItemId(state,
            0x1aeu + static_cast<u32>(ui_index), state.selected_unit_id, 0);
    }
}

void BuildMultiSelectedUnitCommandPanel(UiOverlayState& state) {
    state.command_slot_size = 0x26;
    // FUN_004e4150 restores DAT_00867684 after the 0x1a6 details record has
    // temporarily selected the 0x32 frame size.
    state.current_record_size = 0x26;
    CollectSelectedUnitProductionActionMasks(state);
    if (selected_production_category_active(state)) {
        QueueProductionClassButtonsForSelectedUnit(
            state, selected_production_category_index(state));
        return;
    }
    QueueSelectedUnitCoreActionButtons(state);
    QueueSingleSelectedUnitHeldItemSlots(state);
    QueueAvailableProductionClassButtons(state);
}

void QueueAvailableProductionClassButtons(UiOverlayState& state) {
    if ((state.selected_unit_command_bit_mask & 0x40u) == 0) {
        return;
    }
    PadUiOverlayLargeCommandSlots(state);
    for (u32 category = 0; category < state.selected_production_class_counts.size();
         ++category) {
        if (state.selected_production_class_counts[category] != 0) {
            QueueUiOverlayCommandRecordByItemId(state, 0xc2 + category, 0, 0);
        }
    }
}

void QueueProductionClassButtonsForSelectedUnit(UiOverlayState& state, u32 category) {
    state.command_slot_size = 0x32;
    for (const UiOverlayCommandOption& option : state.command_options) {
        if (!option.enabled || option.aux != category) {
            continue;
        }
        if (option.item_id >= 0x60 && option.item_id <= 0xa9) {
            QueueProductionDefinitionCommandSlot(state, option.item_id,
                option.aux, option.flags);
        }
    }
    AppendUiOverlayCommandSlot(state, 0xc6, category == 0 ? 1 : 2, 0);
    state.side_slot_index = 0;
}

void BuildSingleSelectedUnitCommandPanel(UiOverlayState& state) {
    state.command_slot_size = 0x26;
    // 0x004e4e89 resets DAT_00867684 after the 0x1a6 portrait builder has
    // temporarily selected 0x32.  Dynamic production/action records and their
    // hit regions are therefore 0x26, even when no queued command was present.
    state.current_record_size = 0x26;
    CollectSelectedUnitProductionActionMasks(state);
    QueueSelectedUnitCoreActionButtons(state);
    for (const UiOverlayCommandOption& option : state.primary_production_options) {
        QueueUiOverlayCommandRecordByItemId(
            state, option.item_id, option.aux, option.flags);
    }
    // Normal structures reach 0x004e4fee when their raw alternate-reference
    // count is nonzero, even when owner availability filtered every icon.
    // The four AVATAR producers reach it unconditionally.
    if (state.selected_unit_raw_production_reference_count != 0 ||
        state.selected_unit_uses_avatar_production_slots) {
        QueueUiOverlayCommandRecordByItemId(state, 0xc9u, 0, 0);
    }
    // 0x004e4ffa calls FUN_004e5269 after the optional production-reference
    // list and 0xc9 queue button.  It does so even when that list is empty,
    // except for the original type-0x67 special case.  Consuming the unused
    // first-row positions makes the following upgrades/orders start in row 2.
    if (state.selected_unit_type != 0x67u) {
        PadUiOverlayLargeCommandSlots(state);
    }
    for (const UiOverlayCommandOption& option : state.command_options) {
        if (!option.enabled || option.item_id < 0xaau ||
            is_indexed_queue_command_item(option.item_id)) {
            continue;
        }
        QueueUiOverlayCommandRecordByItemId(
            state, option.item_id, option.aux, option.flags);
    }
    // 0x004e50e8..0x004e5122 compares the complete raw DWORD against each
    // active production state.  High flag bits must not create a cancel slot
    // merely because the low byte resembles one of those states.
    const u32 command = state.selected_unit_command_state;
    if (command == 0x51u || command == 0x50u ||
        command == 0x83u || command == 0x82u ||
        command == 0x4eu || command == 0x4du) {
        // 0x004e5124 appends the ordinary 0x26-size c6 record with aux=4
        // after every structure action while one of the six production states
        // is active.  This is distinct from placement aux 0..2 and the
        // construction aux=3 cancel button.
        state.command_slot_size = 0x26;
        QueueUiOverlayCommandRecordByItemId(state, 0xc6u, 4, 0);
    }
}

bool FindOwnerTransportAttachmentUnit(const UiOverlayState& state, u32 owner_id,
    u32 attachment_mask) {
    for (const UiOverlayCommandOption& option : state.command_options) {
        if (option.aux == owner_id && (option.flags & attachment_mask) == attachment_mask) {
            return true;
        }
    }
    return false;
}

bool FindOwnerCarrierLinkedToSlot(const UiOverlayState& state, u32 owner_id,
    u32 slot_id) {
    for (const UiOverlayCommandOption& option : state.command_options) {
        if (option.aux == owner_id && option.icon_marker == slot_id &&
            (option.item_id == 0x6f || option.item_id == 0x7f ||
             option.item_id == 0x8f || option.item_id == 0x9f)) {
            return true;
        }
    }
    return false;
}

void PadUiOverlayLargeCommandSlots(UiOverlayState& state) {
    // FUN_004e5269 fills the remainder of the first eight dynamic positions
    // with disabled 0xc8 records. Following production-class buttons then
    // begin at dynamic slot 8 (the second row).
    while (state.dynamic_icon_index < 8) {
        QueueUiOverlayCommandRecordByItemId(
            state, 0xc8u, 0, kUiOverlayFlagDisabled);
    }
}

void QueueSelectedUnitCurrentOrderButtons(UiOverlayState& state) {
    if (state.selected_unit_count != 1 || state.selected_unit_type < 0x60u ||
        (!state.scenario_ai_profile_override && state.local_player_type != 2u &&
            state.selected_unit_owner != state.local_player_slot)) {
        return;
    }
    for (const UiOverlayCommandOption& option : state.command_options) {
        if (is_indexed_queue_command_item(option.item_id)) {
            QueueUiOverlayCommandRecordByItemId(state, option.item_id,
                option.aux, option.flags);
        }
    }
}

void CollectSelectedUnitProductionActionMasks(UiOverlayState& state) {
    state.selected_unit_capability_mask = state.selected_unit_command_bit_mask;
    state.selected_unit_status_mask = 0;
    state.selected_production_gate_masks = {};
    state.selected_production_class_counts = {};
    for (const UiOverlayCommandOption& option : state.command_options) {
        if (!option.enabled || is_indexed_queue_command_item(option.item_id)) {
            continue;
        }
        if (option.item_id >= 0xf4 && option.item_id <= 0x133) {
            const u32 bit = (option.item_id - 0xf4) & 0x1f;
            state.selected_unit_status_mask |= 1u << bit;
        }
        if (option.aux < state.selected_production_class_counts.size()) {
            ++state.selected_production_class_counts[option.aux];
        }
    }
    if (state.callbacks.check_selected_production_action_gate == nullptr) {
        for (u32 bit = 0; bit < 32; ++bit) {
            if (should_probe_selected_production_action(state, bit)) {
                state.selected_production_gate_masks[0] |= 1u << bit;
            }
        }
        return;
    }
    for (u32 bit = 0; bit < 32; ++bit) {
        if (!should_probe_selected_production_action(state, bit)) {
            continue;
        }
        u32 failure_code = 0;
        const u32 mask = 1u << bit;
        if (state.callbacks.check_selected_production_action_gate(
                state, bit, failure_code)) {
            state.selected_production_gate_masks[0] |= mask;
            continue;
        }
        state.selected_production_gate_masks[
            selected_production_gate_mask_index_for_failure(failure_code)] |= mask;
    }
}

void QueueUiOverlayCommandRecordByItemId(UiOverlayState& state, u32 item_id,
    u32 aux, u32 flags) {
    if (item_id < 0x60) {
        QueueUnitDefinitionDynamicIconOrHotRegion(state, item_id, aux, flags);
        return;
    }
    if (item_id < 0xaa) {
        QueueProductionDefinitionCommandSlot(state, item_id, aux, flags);
        return;
    }
    if (item_id < 0xd4) {
        QueueObjectDefinitionDynamicIconOrHotRegion(state, item_id, aux, flags);
        return;
    }
    if (item_id < 0xf4) {
        QueueProductionActionDynamicIconRecord(state, item_id, aux, flags);
        return;
    }
    if (item_id < 0x134) {
        QueueProductionOrderDynamicIconRecord(state, item_id, aux, flags);
        return;
    }
    if (item_id < 0x194) {
        QueueEquipmentDefinitionCommandSlot(state, item_id, aux, flags);
        return;
    }

    switch (item_id) {
    case 0x194:
        QueueUiOverlaySmallSlot1Record(state, item_id, aux, flags);
        return;
    case 0x195:
        QueueUiOverlaySmallSlot2Record(state, item_id, aux, flags);
        return;
    case 0x196:
        QueueUiOverlaySmallSlot3Record(state, item_id, aux, flags);
        return;
    case 0x197:
    case 0x198:
        QueueUiOverlayLargeSlot0Record(state, item_id, aux, flags);
        return;
    case 0x199:
        QueueUiOverlayLargeSlot3Record(state, item_id, aux, flags);
        return;
    case 0x19a:
        QueueUiOverlayLargeSlot6Record(state, item_id, aux, flags);
        return;
    case 0x19b:
        QueueUiOverlayLargeSlot9Record(state, item_id, aux, flags);
        return;
    case 0x19c:
        QueueUiOverlayLargeSlot12Record(state, item_id, aux, flags);
        return;
    case 0x1a6:
        QueueUiOverlayWideSlotRecord(state, item_id, aux, flags);
        return;
    case 0x1a8:
        QueueUiOverlaySideSlotRecord(state, item_id, aux, flags);
        return;
    case 0x1aa:
    case 0x1ab:
    case 0x1ac:
        // FUN_004e5cca selects DAT_0086432c[EDI], where EDI is the logical
        // queue index stored in the record flags dword (0=current, 1..4=queued).
        QueueUiOverlayIndexedSlotRecord(state, item_id, aux, flags, flags);
        return;
    case 0x1ad:
    {
        const UiOverlayRect rect =
            rect_or_default(state.manual_equipment_slot_bounds[0], 0x26, 0x26);
        QueueUiOverlayManual26RecordAlternate(state, item_id, aux, flags,
            rect.x, rect.y);
        return;
    }
    case 0x19d:
    case 0x19e:
    case 0x19f:
    case 0x1a0:
    case 0x1a1:
    case 0x1a2:
    case 0x1a3:
    case 0x1a4:
    case 0x1a5:
    case 0x1a7:
    case 0x1a9:
        return;
    default:
        break;
    }

    if (item_id >= 0x1ae && item_id <= 0x1b3) {
        const std::size_t index = static_cast<std::size_t>(item_id - 0x1ad);
        const UiOverlayRect rect = index < state.manual_equipment_slot_bounds.size()
            ? rect_or_default(state.manual_equipment_slot_bounds[index], 0x13, 0x13)
            : UiOverlayRect{state.small_slot1_x, state.small_slot1_y, 0x13, 0x13};
        QueueUiOverlayManual13Record(state, item_id, aux, flags, rect.x, rect.y);
        return;
    }
    if (item_id >= 0x1b4 && item_id < 0x24a) {
        const i32 x = state.last_hot_region_x != 0 ? state.last_hot_region_x :
            state.small_slot1_x;
        const i32 y = state.last_hot_region_y != 0 ? state.last_hot_region_y :
            state.small_slot1_y;
        QueueUiOverlayManual26Record(state, item_id, aux, flags, x, y);
        return;
    }
    if (item_id >= 0x24a) {
        QueueEquipmentDefinitionDynamicIconRecord(state, item_id, aux, flags);
        return;
    }
    AppendUiOverlayCommandSlot(state, item_id, aux, flags);
}

void QueueUnitDefinitionDynamicIconOrHotRegion(UiOverlayState& state, u32 item_id,
    u32 aux, u32 flags) {
    state.command_icon_marker =
        vector_value_or_zero(state.unit_definition_icon_markers, item_id);
    if ((flags & 1u) != 0) {
        append_offscreen_hotkey_record(state, item_id, aux, flags);
        return;
    }
    state.current_icon_marker = state.command_icon_marker;
    QueueUiOverlayDynamicIconRecord(state, item_id, aux, flags);
}

void QueueObjectDefinitionDynamicIconOrHotRegion(UiOverlayState& state, u32 item_id,
    u32 aux, u32 flags) {
    if (item_id == 0xb5) {
        state.command_icon_marker =
            vector_value_or_zero(state.unit_definition_icon_markers, aux);
    } else {
        state.command_icon_marker = vector_value_or_zero(
            state.object_icon_markers, item_id - 0xaa);
    }
    if ((flags & 1u) != 0) {
        append_offscreen_hotkey_record(state, item_id, aux, flags);
        return;
    }
    state.current_icon_marker = state.command_icon_marker;
    QueueUiOverlayDynamicIconRecord(state, item_id, aux, flags);
}

void QueueProductionDefinitionCommandSlot(UiOverlayState& state, u32 item_id,
    u32 aux, u32 flags) {
    const UiOverlayCommandOption* option = find_command_option(state, item_id);
    const u32 icon_marker = option != nullptr ?
        option->icon_marker :
        vector_value_or_zero(state.unit_definition_icon_markers, item_id);
    AppendUiOverlayCommandSlot(state, item_id, aux, flags, icon_marker);
}

void AppendUiOverlayCommandSlot(UiOverlayState& state, u32 item_id, u32 aux,
    u32 flags, u32 icon_marker) {
    UiOverlayRect rect = command_slot_rect(state);
    rect = rect_or_default(rect, state.command_slot_size, state.command_slot_size);
    UiOverlayDrawRecord record =
        make_record(item_id, aux, flags, rect, icon_marker);
    append_record(state, record);
    const UiOverlayCommandOption* option = find_command_option(state, item_id);
    append_hot_region(state, record, option != nullptr ? option->hotkey : 0,
        option == nullptr || option->enabled);
    ++state.command_slot_count;
}

void QueueEquipmentDefinitionCommandSlot(UiOverlayState& state, u32 item_id,
    u32 aux, u32 flags) {
    AppendUiOverlayCommandSlot(state, item_id, aux, flags, state.command_icon_marker);
}

void DispatchGameplayUiKeyboardInput(UiOverlayState& state, u32 virtual_key,
    u8 ascii) {
    if (state.chat_active) {
        HandleGameplayChatKey(state, ascii != 0 ? ascii : static_cast<u8>(virtual_key));
        return;
    }
    if (ascii == '\r' || ascii == '\n') {
        BeginGameplayChatInput(state);
        return;
    }

    switch (virtual_key) {
    case 1:
        CancelCurrentUiModeOrActivateCommand(state);
        return;
    case 2: case 3: case 4: case 5: case 6:
    case 7: case 8: case 9: case 10: case 11:
        SelectControlGroupFromDigit(state, virtual_key - 1);
        return;
    case 0x0c:
    case 0x4a:
        IncreaseGameplaySpeed(state);
        return;
    case 0x0d:
    case 0x4e:
        DecreaseGameplaySpeed(state);
        return;
    case 0x0f:
        ToggleMinimapModeAndPersistSetup(state);
        return;
    case 0x29:
        CycleSelectedControlGroup(state);
        return;
    case 0x39:
        ClampCameraToStoredMinimapPoint(state, state.stored_minimap_world_x,
            state.stored_minimap_world_y, state.stored_minimap_point_valid);
        return;
    case 0x3b:
    case 0x3f:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
        RecallOrStoreCameraBookmark(state, virtual_key - 0x3b,
            state.shift_modifier_down);
        return;
    case 0x3c:
        HandleGameplayMenuKey3c(state, virtual_key - 0x3b,
            state.shift_modifier_down);
        return;
    case 0x3d:
        HandleGameplayMenuKey3d(state, virtual_key - 0x3b,
            state.shift_modifier_down);
        return;
    case 0x3e:
        OpenGameplayMenuKey3e(state);
        return;
    case 0x57:
        ToggleGameplayOverlayFlag(state);
        return;
    default:
        if (ascii != 0) {
            ActivateCommandHotkey(state, ascii);
        }
        return;
    }
}

void ClampCameraToStoredMinimapPoint(UiOverlayState& state, i32 world_x, i32 world_y,
    bool valid) {
    if (valid) {
        ClampCameraToMinimapPoint(state, world_x, world_y);
    }
}

void IncreaseGameplaySpeed(UiOverlayState& state) {
    const u32 original_cap = std::min<u32>(state.max_game_speed, 0x0f);
    if (!state.scenario_ai_profile_override && state.game_speed < original_cap) {
        ++state.game_speed;
    }
}

void DecreaseGameplaySpeed(UiOverlayState& state) {
    if (!state.scenario_ai_profile_override && state.game_speed != 0) {
        --state.game_speed;
    }
}

void ToggleMinimapModeAndPersistSetup(UiOverlayState& state) {
    state.minimap_mode = !state.minimap_mode;
    state.setup_write_requested = true;
    ConfigureGameplayUiOverlayLayout(state);
}

void CancelCurrentUiModeOrActivateCommand(UiOverlayState& state, u32 command_id) {
    if (state.placement_mode != 0) {
        state.placement_mode = 0;
        state.staged_unit_action_id = 0xffffffffu;
        state.selected_production_category = 0;
        state.command_slot_count = 0;
        state.context_cursor.animation_mode = 0;
        if (state.callbacks.play_click_sound != nullptr) {
            state.callbacks.play_click_sound(state);
        }
        return;
    }
    if (command_id != 0 && CheckUiOverlayCommandRecordEnabled(state, command_id)) {
        state.pending_local_command = true;
        state.last_hotkey_command = command_id;
    }
}

void DecrementSelectedUnitCooldownTimers(UiOverlayState& state) {
    if (state.selected_unit_health != 0) {
        --state.selected_unit_health;
    }
    if (state.selected_unit_secondary != 0) {
        --state.selected_unit_secondary;
    }
}

void RecallOrStoreCameraBookmark(UiOverlayState& state, u32 bookmark_index,
    bool store) {
    if (bookmark_index >= state.camera_bookmarks.size()) {
        return;
    }
    UiOverlayCameraBookmark& bookmark = state.camera_bookmarks[bookmark_index];
    if (store) {
        bookmark.valid = true;
        bookmark.camera_x = state.camera_x;
        bookmark.camera_y = state.camera_y;
        return;
    }
    if (bookmark.valid) {
        state.camera_x = std::clamp(bookmark.camera_x, 0, state.camera_max_x);
        state.camera_y = std::clamp(bookmark.camera_y, 0, state.camera_max_y);
    }
}

void HandleGameplayMenuKey3c(UiOverlayState& state, u32 bookmark_index, bool store) {
    if (state.scenario_ai_profile_override) {
        RecallOrStoreCameraBookmark(state, bookmark_index, store);
        return;
    }
    if (state.callbacks.open_save_session_dialog != nullptr) {
        state.callbacks.open_save_session_dialog(state);
    }
    if (state.callbacks.update_catchup_target_if_active != nullptr) {
        state.callbacks.update_catchup_target_if_active(state);
    }
}

void HandleGameplayMenuKey3d(UiOverlayState& state, u32 bookmark_index, bool store) {
    if (state.scenario_ai_profile_override) {
        RecallOrStoreCameraBookmark(state, bookmark_index, store);
        return;
    }
    if (state.callbacks.open_load_session_dialog != nullptr) {
        state.callbacks.open_load_session_dialog(state);
    }
    if (state.callbacks.update_catchup_target_if_active != nullptr) {
        state.callbacks.update_catchup_target_if_active(state);
    }
}

void OpenGameplayMenuKey3e(UiOverlayState& state) {
    if (state.callbacks.open_options_menu != nullptr) {
        state.callbacks.open_options_menu(state);
    }
    if (state.callbacks.update_catchup_target_if_active != nullptr) {
        state.callbacks.update_catchup_target_if_active(state);
    }
}

void ToggleGameplayOverlayFlag(UiOverlayState& state) {
    state.gameplay_overlay_flag = !state.gameplay_overlay_flag;
}

void SelectControlGroupFromDigit(UiOverlayState& state, u32 group) {
    if (group >= state.control_groups.size()) {
        return;
    }
    if (state.control_group_assign_mode) {
        AssignSelectedUnitsToControlGroup(state, group);
        return;
    }
    SelectUnitsInControlGroup(state, group);
    if (state.selected_unit_id == 0) {
        return;
    }
    if (state.last_control_group == group &&
        state.current_frame_counter - state.last_control_group_frame < 0x191) {
        FocusCameraOnSelectedUnitsBounds(state);
    }
    state.last_control_group = group;
    state.last_control_group_frame = state.current_frame_counter;
}

void AssignSelectedUnitsToControlGroup(UiOverlayState& state, u32 group) {
    if (group >= state.control_groups.size()) {
        return;
    }
    if (state.selected_unit_id == 0 ||
        state.selected_unit_owner != state.local_player_slot) {
        return;
    }

    // The original stores one low-nibble control-group id on each unit.  A
    // group assignment replaces the target group and moves the selected units
    // out of every other group instead of allowing duplicate membership.
    state.control_groups[group].unit_ids.clear();
    for (u32 other_group = 1; other_group < state.control_groups.size();
         ++other_group) {
        if (other_group == group) {
            continue;
        }
        auto& unit_ids = state.control_groups[other_group].unit_ids;
        unit_ids.erase(std::remove_if(unit_ids.begin(), unit_ids.end(),
            [&](u32 unit_id) {
                return std::find(state.selected_unit_ids.begin(),
                           state.selected_unit_ids.end(), unit_id) !=
                    state.selected_unit_ids.end();
            }), unit_ids.end());
    }
    state.control_groups[group].unit_ids = state.selected_unit_ids;
}

void SelectUnitsInControlGroup(UiOverlayState& state, u32 group) {
    if (group >= state.control_groups.size()) {
        return;
    }
    state.selected_unit_ids = state.control_groups[group].unit_ids;
    RecountGameplaySelectedUnits(state);
    NotifyPrimaryGameplayUnitSelected(state);
}

void CycleSelectedControlGroup(UiOverlayState& state) {
    std::array<bool, 16> group_present{};
    bool any_group = false;
    bool any_selected_group = false;
    u32 min_group = 0xffffffffu;
    u32 min_selected_group = 0xffffffffu;
    u32 max_selected_group = 0;

    const u32 group_count =
        std::min<u32>(static_cast<u32>(state.control_groups.size()),
            static_cast<u32>(group_present.size()));
    for (u32 group = 1; group < group_count; ++group) {
        for (u32 unit_id : state.control_groups[group].unit_ids) {
            const UiOverlayMinimapUnit* unit = find_unit_by_id(state, unit_id);
            if (unit == nullptr || unit->owner_id != state.local_player_slot) {
                continue;
            }
            group_present[group] = true;
            any_group = true;
            min_group = std::min(min_group, group);
            if (unit_already_selected(state, unit_id)) {
                any_selected_group = true;
                min_selected_group = std::min(min_selected_group, group);
                max_selected_group = std::max(max_selected_group, group);
            }
            break;
        }
    }

    if (!any_group) {
        return;
    }

    u32 target_group = min_group;
    if (any_selected_group && min_selected_group == max_selected_group) {
        target_group = min_selected_group;
        do {
            ++target_group;
            if (target_group >= group_count) {
                target_group = 0;
            }
            if (group_present[target_group]) {
                break;
            }
        } while (target_group != min_selected_group);

        if (target_group == min_selected_group) {
            return;
        }
    }

    SelectUnitsInControlGroup(state, target_group);
    state.last_control_group = target_group;
}

void FocusCameraOnSelectedUnitsBounds(UiOverlayState& state) {
    if (state.selected_unit_ids.empty()) {
        return;
    }
    bool found = false;
    i32 min_x = 0;
    i32 min_y = 0;
    i32 max_x = 0;
    i32 max_y = 0;
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (std::find(state.selected_unit_ids.begin(), state.selected_unit_ids.end(),
                unit.unit_id) == state.selected_unit_ids.end()) {
            continue;
        }
        if (!found) {
            min_x = max_x = unit.world_x;
            min_y = max_y = unit.world_y;
            found = true;
        } else {
            min_x = std::min(min_x, unit.world_x);
            min_y = std::min(min_y, unit.world_y);
            max_x = std::max(max_x, unit.world_x);
            max_y = std::max(max_y, unit.world_y);
        }
    }
    if (found) {
        ClampCameraToMinimapPoint(state, (min_x + max_x) / 2, (min_y + max_y) / 2);
    }
}

void ActivateCommandHotkey(UiOverlayState& state, u8 key) {
    if (key >= 0x59) {
        return;
    }
    const u32 command = ResolveUiOverlayHotkeyCommand(state, key);
    if (command != 0 && CheckUiOverlayCommandRecordEnabled(state, command)) {
        DispatchUiOverlayCommandAction(state, command);
    }
}

void BeginGameplayChatInput(UiOverlayState& state) {
    if (state.callbacks.on_chat_input_begin != nullptr) {
        state.callbacks.on_chat_input_begin(state);
    }
    state.chat_active = true;
    state.chat_cursor_visible = true;
    state.chat_input_text.clear();
    if (!state.scenario_ai_profile_override) {
        state.chat_channel = 4;
        return;
    }
    if (state.local_player_type != 2) {
        if (state.shift_modifier_down) {
            state.chat_channel = 3;
            return;
        }
        if (state.ctrl_modifier_down || state.control_group_assign_mode) {
            state.chat_channel = 1;
            return;
        }
        if (state.alt_modifier_down) {
            state.chat_channel = 2;
            return;
        }
    }
    state.chat_channel = state.default_chat_channel;
}

void HandleGameplayChatKey(UiOverlayState& state, u8 key) {
    if (!state.chat_active) {
        return;
    }
    if (key == '\r' || key == '\n') {
        HandleGameplayChatBangCommand(state);
        if (!state.chat_input_text.empty() &&
            !DispatchGameplayChatSlashCommand(state, state.chat_input_text)) {
            QueueGameplayChatMessageDisplay(state, state.chat_input_text,
                state.chat_channel, true);
        }
        state.chat_input_text.clear();
        state.chat_active = false;
        state.chat_cursor_visible = false;
        if (state.callbacks.on_chat_input_end != nullptr) {
            state.callbacks.on_chat_input_end(state);
        }
        return;
    }
    if (key == 0x1b) {
        state.chat_input_text.clear();
        state.chat_active = false;
        state.chat_cursor_visible = false;
        if (state.callbacks.on_chat_input_end != nullptr) {
            state.callbacks.on_chat_input_end(state);
        }
        return;
    }
    if (key == 8) {
        if (!state.chat_input_text.empty()) {
            const u8 removed =
                static_cast<u8>(state.chat_input_text.back());
            state.chat_input_text.pop_back();
            if (removed > 0x7f && !state.chat_input_text.empty() &&
                static_cast<u8>(state.chat_input_text.back()) > 0x7f) {
                state.chat_input_text.pop_back();
            }
        }
        return;
    }
    if (state.chat_input_text.size() < 0x30 && key >= 0x20) {
        state.chat_input_text.push_back(static_cast<char>(key));
    }
}

void QueueGameplayChatMessageDisplay(UiOverlayState& state, const std::string& text,
    u32 channel, bool local_echo) {
    if (text.empty()) {
        return;
    }
    UiOverlayChatMessage message{};
    message.text = text;
    message.channel = channel;
    message.local_echo = local_echo;
    message.expire_frame = state.current_frame_counter + 8000;
    state.chat_messages.push_back(std::move(message));
}

void HandleGameplayChatBangCommand(UiOverlayState& state) {
    if (!state.chat_input_text.empty() && state.chat_input_text.front() == '!') {
        state.pending_unit_action_text = state.chat_input_text;
        state.pending_local_command = true;
    }
}

bool DispatchGameplayChatSlashCommand(UiOverlayState& state, const std::string& text) {
    if (text.empty() || text.front() != '/') {
        return false;
    }
    u32 checksum = static_cast<u32>(text.size());
    for (char ch : text) {
        checksum = (checksum ^ static_cast<u8>(ch)) + checksum;
    }
    state.last_hotkey_command = checksum;
    state.pending_local_command = true;
    return true;
}

void ScrollCameraLeft(UiOverlayState& state) {
    if (state.camera_x > 0) {
        const u32 step = ResolveCameraScrollStep(state);
        if (state.camera_x < static_cast<i32>(step)) {
            state.camera_x = 0;
            return;
        }
        state.camera_x -= static_cast<i32>(step);
        state.camera_scroll_dirty = true;
        return;
    }
    state.camera_x = 0;
}

void ScrollCameraRight(UiOverlayState& state) {
    if (state.camera_x < state.camera_max_x) {
        const u32 step = ResolveCameraScrollStep(state);
        state.camera_x = std::min(state.camera_x + static_cast<i32>(step),
            state.camera_max_x);
        state.camera_scroll_dirty = true;
    } else {
        state.camera_x = state.camera_max_x;
    }
}

void ScrollCameraUp(UiOverlayState& state) {
    if (state.camera_y > 0) {
        const u32 step = ResolveCameraScrollStep(state);
        if (state.camera_y < static_cast<i32>(step)) {
            state.camera_y = 0;
            return;
        }
        state.camera_y -= static_cast<i32>(step);
        state.camera_scroll_dirty = true;
        return;
    }
    state.camera_y = 0;
}

void ScrollCameraDown(UiOverlayState& state) {
    if (state.camera_y < state.camera_max_y) {
        const u32 step = ResolveCameraScrollStep(state);
        state.camera_y = std::min(state.camera_y + static_cast<i32>(step),
            state.camera_max_y);
        state.camera_scroll_dirty = true;
    } else {
        state.camera_y = state.camera_max_y;
    }
}

void UpdateCameraScrollRamp(UiOverlayState& state) {
    if (state.camera_scroll_dirty) {
        IncreaseCameraScrollRamp(state);
    } else {
        DecreaseCameraScrollRamp(state);
    }
    state.camera_scroll_dirty = false;
}

u32 ResolveCameraScrollStep(UiOverlayState& state) {
    if (state.replay_timing_enabled) {
        const u32 bucket = state.current_tick_ms / 0x1fu;
        if (state.camera_scroll_tick_bucket == bucket) {
            return 0;
        }
        state.camera_scroll_tick_bucket = bucket;
    }

    const u32 speed_index = std::min<u32>(state.camera_scroll_speed_index,
        static_cast<u32>(state.camera_scroll_steps.size() - 1));
    const auto& speed_steps = state.camera_scroll_steps[speed_index];
    const u32 ramp_index = std::min<u32>(
        state.camera_scroll_ramp, static_cast<u32>(speed_steps.size() - 1));
    return speed_steps[ramp_index];
}

void IncreaseCameraScrollRamp(UiOverlayState& state) {
    if (state.camera_scroll_ramp + 1 < kCameraScrollRampCount) {
        ++state.camera_scroll_ramp;
    }
}

void DecreaseCameraScrollRamp(UiOverlayState& state) {
    if (state.camera_scroll_ramp != 0) {
        --state.camera_scroll_ramp;
    }
}

void ResetCameraBookmarks(UiOverlayState& state) {
    for (UiOverlayCameraBookmark& bookmark : state.camera_bookmarks) {
        bookmark = {};
    }
}

void UpdateGameplayHoverContextAndTooltip(UiOverlayState& state) {
    ResolveGameplayHoverContext(state);
    set_tooltip_payload_for_hover(state);
    // FUN_004e9458 publishes the stored hot-region origin (rather than the
    // moving cursor) to FUN_004de7cf/FUN_004de7f3.  Terrain-resource hover
    // likewise publishes the vertically nudged screen point returned by
    // FUN_004c6b5e.  Anchoring these boxes at the raw pointer made them cover
    // the command icon and placement ghost.
    const bool use_resolved_anchor =
        hover_context_is_command_record(state.hover_context) ||
        state.hover_context.kind == 0x0c;
    const i32 tooltip_x = use_resolved_anchor ?
        state.hover_context.x : state.mouse_x;
    const i32 tooltip_y = use_resolved_anchor ?
        state.hover_context.y : state.mouse_y;
    if (hover_kind_uses_immediate_tooltip_schedule(state.hover_context.kind)) {
        ScheduleGameplayTooltipImmediate(gameplay_tooltip_state(),
            state.hover_context.kind, tooltip_x, tooltip_y);
    } else {
        ScheduleGameplayTooltip(gameplay_tooltip_state(), state.hover_context.kind,
            tooltip_x, tooltip_y);
    }
    if (state.hover_context.kind == 0) {
        NoOpHoverContextHandler(state);
    }
}

void ResolveGameplayHoverContext(UiOverlayState& state) {
    state.hover_context = {};
    state.hover_context.x = state.mouse_x;
    state.hover_context.y = state.mouse_y;

    if (const UiOverlayHotRegion* region =
            hot_region_at(state, state.mouse_x, state.mouse_y)) {
        set_hot_region_result(state, *region);
        state.hover_context.kind = state.last_hotkey_hover_kind;
        state.hover_context.item_id = region->record.item_id;
        state.hover_context.unit_id = region->record.aux;
        state.hover_context.x = region->record.x;
        state.hover_context.y = region->record.y;
        return;
    }

    if (CheckMouseInsideMinimap(state)) {
        const i32 local_x = state.mouse_x - state.minimap.output_x;
        const i32 local_y = state.mouse_y - state.minimap.output_y;
        state.hover_context.kind = 1;
        state.hover_context.x = minimap_input_screen_to_world_x(state, local_x);
        state.hover_context.y = minimap_input_screen_to_world_y(state, local_y);
        return;
    }

    if (CheckUiOverlayIconMaskPixel(state, state.mouse_x, state.mouse_y)) {
        state.hover_context.kind = 5;
        return;
    }

    if (const UiOverlayMinimapUnit* unit =
            unit_at_screen_point(state, state.mouse_x, state.mouse_y, false)) {
        if (unit->type_id >= 0x60 &&
            FindMapEffectUnderMousePointer(state, state.mouse_x, state.mouse_y)) {
            return;
        }
        set_hover_from_unit(state, *unit, state.mouse_x, state.mouse_y);
        return;
    }

    if (FindMapEffectUnderMousePointer(state, state.mouse_x, state.mouse_y)) {
        return;
    }

    // FUN_004e9458's final 0x0c branch is not a generic placement tooltip.
    // FUN_004c6b5e accepts only a currently-visible terrain resource cell
    // (terrain class 1), probing y, y-15, then y+15.  Treating every map point
    // as kind 0x0c caused the spurious "Berry 0" UI while placing buildings.
    const GameplayTooltipState& tooltip = gameplay_tooltip_state();
    const i32 world_x = state.camera_x + state.mouse_x;
    const i32 world_y = state.camera_y + state.mouse_y;
    if (world_x >= 0 && world_y >= 0 &&
        tooltip.map_width_tiles != 0 && tooltip.map_height_tiles != 0) {
        constexpr std::array<i32, 3> kTerrainHoverYNudges{{0, -0x0f, 0x0f}};
        const u32 tile_x = static_cast<u32>(world_x >> 5);
        for (i32 y_nudge : kTerrainHoverYNudges) {
            const i32 candidate_world_y = world_y + y_nudge;
            if (candidate_world_y < 0) {
                continue;
            }
            const u32 tile_y = static_cast<u32>(candidate_world_y >> 5);
            if (tile_x >= tooltip.map_width_tiles ||
                tile_y >= tooltip.map_height_tiles) {
                continue;
            }
            const std::size_t index =
                static_cast<std::size_t>(tile_y) * tooltip.map_width_tiles + tile_x;
            // FindPassableVerticalNudgeTile checks DAT_00798d40, the previous /
            // explored visibility layer, not the current minimap word.
            const u32 visibility = minimap_layer_value(
                state, state.minimap_object_flags, tile_x, tile_y);
            if ((visibility & 0x10000000u) == 0 ||
                index >= tooltip.terrain_flags.size() ||
                (tooltip.terrain_flags[index] & 0x700u) != 0x100u) {
                continue;
            }
            state.hover_context.kind = 0x0c;
            state.hover_context.item_id = 0x0c;
            state.hover_context.x = state.mouse_x;
            state.hover_context.y = state.mouse_y + y_nudge;
            break;
        }
    }
}

bool FindUnitUnderMousePointer(UiOverlayState& state, i32 screen_x, i32 screen_y) {
    if (const UiOverlayMinimapUnit* unit =
            unit_at_screen_point(state, screen_x, screen_y, false)) {
        set_hover_from_unit(state, *unit, screen_x, screen_y);
        return true;
    }
    return false;
}

bool FindMapEffectUnderMousePointer(UiOverlayState& state, i32 screen_x, i32 screen_y) {
    const i32 world_x = state.camera_x + screen_x;
    const i32 world_y = state.camera_y + screen_y;
    if (world_x < 0 || world_y < 0) {
        return false;
    }
    const u32 tile_x = static_cast<u32>(world_x >> 5);
    const u32 tile_y = static_cast<u32>(world_y >> 5);
    if (tile_x >= minimap_width_tiles(state) || tile_y >= minimap_height_tiles(state)) {
        return false;
    }

    const u32 visibility = minimap_layer_value(
        state, state.minimap_visibility_flags, tile_x, tile_y);
    if ((visibility & 0x08000000u) == 0) {
        return false;
    }

    const i32 effect_x = static_cast<i32>(tile_x << 5);
    const i32 effect_y = static_cast<i32>(tile_y << 5);
    for (const UiOverlayMapEffect& effect : state.map_effects) {
        if (effect.world_x != effect_x || effect.world_y != effect_y) {
            continue;
        }
        state.hover_context.kind = 9;
        state.hover_context.item_id = effect.effect_id;
        state.hover_context.unit_id = effect.instance_id;
        state.hover_context.x = effect_x;
        state.hover_context.y = effect_y;
        return true;
    }
    return false;
}

void NoOpHoverContextHandler(UiOverlayState&) {
}

bool FindUnitUnderStoredPointer(UiOverlayState& state) {
    return FindUnitUnderMousePointer(state, state.mouse_x, state.mouse_y);
}

bool FindFreeUnitUnderStoredPointer(UiOverlayState& state) {
    if (const UiOverlayMinimapUnit* unit =
            unit_at_screen_point(state, state.mouse_x, state.mouse_y, true)) {
        set_hover_from_unit(state, *unit, state.mouse_x, state.mouse_y);
        return true;
    }
    return false;
}

void HandleGameplayPointerActionFrame(UiOverlayState& state) {
    if ((state.pointer_state & kPointerPromoteHold) != 0) {
        state.pointer_state &= ~kPointerPromoteHold;
        state.pointer_state |= kPointerHoldPress;
    }

    if ((state.pointer_state & kPointerPress) != 0) {
        BeginUiCommandButtonPress(state);
        if (state.pressed_command_id == 0xffffffffu) {
            const bool over_hud =
                CheckUiOverlayIconMaskPixel(state, state.mouse_x, state.mouse_y);
            if (state.placement_mode == 6 &&
                !CheckMouseInsideMinimap(state) && !over_hud) {
                // A mobile unit's build icon stores (unit_type - 0x60) in the
                // placement definition.  Route the map click through original
                // object action 6 so the lockstep command contains the chosen
                // building index and placement point.
                append_command_action(state, 0xaau + 6u,
                    state.placement_definition_id, kCommandActionPlacement);
                state.pending_local_command = true;
            }
            else if (state.placement_mode == 2 &&
                !CheckMouseInsideMinimap(state) && !over_hud) {
                // FUN_004e9ed0 sends every low placement mode through
                // FUN_004da02c.  Mode two is the held food/equipment transfer
                // command; retaining the logical slot in aux also snapshots
                // raw +0x2c's slot-code zero for deferred UI processing.
                append_command_action(state, 0xaau + 2u,
                    state.placement_definition_id, kCommandActionPlacement);
                state.pending_local_command = true;
            }
            else if (state.placement_mode == 0x0eu &&
                !CheckMouseInsideMinimap(state) && !over_hud) {
                // FUN_004e9ed0's dedicated mode-0e branch calls FUN_004db650,
                // which publishes the staged equipment slot at this point.
                append_command_action(state, 0x0eu,
                    state.placement_equipment_slot_code,
                    kCommandActionPlacement);
                state.pending_local_command = true;
            }
            else if (state.staged_unit_action_id != 0xffffffffu &&
                !CheckMouseInsideMinimap(state) && !over_hud) {
                const u32 action_id = state.staged_unit_action_id;
                append_command_action(state, 0xaau + action_id, 0,
                    kCommandActionPlacement);
                state.staged_unit_action_id = 0xffffffffu;
                state.placement_mode = 0;
                state.pending_local_command = true;
            }
            else if (!CheckPointerInsideMinimapAndPlacementMode(state) &&
                !over_hud) {
                state.selection_rectangle_active = true;
                state.selection_left = state.selection_right = state.mouse_x;
                state.selection_top = state.selection_bottom = state.mouse_y;
            }
        }
    }

    if ((state.pointer_state & kPointerDrag) != 0) {
        if ((state.pointer_state & kPointerMinimapDrag) != 0) {
            UpdateCameraFromMinimapDrag(state);
        } else if (state.selection_rectangle_active) {
            state.selection_right = state.mouse_x;
            state.selection_bottom = state.mouse_y;
        }
    }

    if ((state.pointer_state & kPointerRelease) != 0) {
        if (state.selection_rectangle_active) {
            const i32 left = std::min(state.selection_left, state.selection_right);
            const i32 right = std::max(state.selection_left, state.selection_right);
            const i32 top = std::min(state.selection_top, state.selection_bottom);
            const i32 bottom = std::max(state.selection_top, state.selection_bottom);
            state.selection_left = left;
            state.selection_right = right;
            state.selection_top = top;
            state.selection_bottom = bottom;
            append_command_action(state, 0, 0, kCommandActionSelection);
        }
        ReleaseUiCommandButtonPress(state);
        state.selection_rectangle_active = false;
        state.pointer_state &= ~kPointerMinimapDrag;
    }

    if ((state.pointer_state & kPointerHoldPress) != 0) {
        // RBUTTONDOWN snapshots raw placement mode before any HUD/minimap hit
        // testing.  A nonzero mode cancels it and emits no world command.
        if (state.placement_mode != 0) {
            CancelCurrentUiModeOrActivateCommand(state);
            return;
        }
        BeginUiCommandButtonHold(state);
        if (state.held_command_id == 0xffffffffu) {
            const bool minimap_action = CheckPointerInsideMinimapForAction(state);
            if (!minimap_action &&
                !CheckUiOverlayIconMaskPixel(state, state.mouse_x, state.mouse_y)) {
                // RBUTTONDOWN forwards the resolved contextual cursor mode
                // verbatim: pickup 1, repair 3, move 4, attack 5, harvest 7,
                // special 8, boarding 10, and the preserved/stale table cases.
                const u32 action_id = state.context_cursor.animation_mode;
                if (action_id == 0u) {
                    return;
                }
                // Snapshot +0x08/+0x0c are hover kind and raw hover aux, not
                // the Win32 event x/y stored in the generic input ring.  Keep
                // both values with the deferred UI action until dispatch.
                u32 contextual_target = 0;
                if (state.hover_context.kind >= 6u &&
                    state.hover_context.kind <= 9u) {
                    contextual_target = state.hover_context.unit_id;
                }
                else if (state.hover_context.kind == 0x0bu ||
                    state.hover_context.kind == 0x0cu) {
                    contextual_target = state.hover_context.kind;
                }
                append_command_action(state, 0xaau + action_id,
                    contextual_target,
                    kCommandActionContextual, state.hover_context.kind);
                state.pending_local_command = true;
            }
        }
    }

    if ((state.pointer_state & kPointerHoldRelease) != 0) {
        ReleaseUiCommandButtonHold(state);
    }

    if ((state.pointer_state & kPointerMenuAction) != 0) {
        state.pending_local_command = true;
    }

}

void DispatchGameplayPointerUnitCommand(UiOverlayState& state, u32 command_id) {
    state.pressed_command_id = command_id;
    const bool found_target =
        FindUnitUnderStoredPointer(state) || FindFreeUnitUnderStoredPointer(state);
    if (!found_target) {
        StopGameplayHudPulse(state);
        return;
    }

    append_command_action(state, command_id, state.hover_context.unit_id,
        kCommandActionClick);
    StartGameplayHudPulse(state, state.hover_context.x,
        state.hover_context.y, state.current_tick_ms);
}

void DispatchGameplayPointerTerrainCommand(UiOverlayState& state, u32 command_id) {
    state.pressed_command_id = command_id;
    const i32 world_x = state.camera_x + state.mouse_x;
    const i32 world_y = state.camera_y + state.mouse_y;
    if (world_x < 0 || world_y < 0) {
        StopGameplayHudPulse(state);
        return;
    }

    state.hover_context.kind = 0x0c;
    state.hover_context.item_id = command_id;
    state.hover_context.x = world_x & ~0x1f;
    state.hover_context.y = world_y & ~0x1f;
    const u32 packed_tile =
        (static_cast<u32>(state.hover_context.y) << 16) ^
        (static_cast<u32>(state.hover_context.x) & 0xffffu);
    append_command_action(state, command_id,
        packed_tile, kCommandActionClick);
    StartGameplayHudPulse(state, world_x, world_y,
        state.current_tick_ms);
}

void NoOpGameplayPointerCommandHandler(UiOverlayState&) {
}

void UpdateCameraFromMinimapDrag(UiOverlayState& state) {
    ConfigureGameplayUiOverlayLayout(state);
    const i32 width = static_cast<i32>(minimap_screen_width(state));
    const i32 height = static_cast<i32>(minimap_screen_height(state));
    const i32 local_x = std::clamp(state.mouse_x - state.minimap.output_x, 0,
        std::max(0, width - 1));
    const i32 local_y = std::clamp(state.mouse_y - state.minimap.output_y, 0,
        std::max(0, height - 1));
    const u32 map_width = std::max<u32>(1, state.minimap.map_width_tiles);
    const u32 map_height = std::max<u32>(1, state.minimap.map_height_tiles);
    const u32 mini_width = std::max<u32>(1, state.minimap.minimap_width_pixels);
    const u32 mini_height = std::max<u32>(1, state.minimap.minimap_height_pixels);
    const u32 view_mini_width = static_cast<u32>(
        (static_cast<u64>(state.minimap.viewport_width_pixels >> 5) *
            mini_width) / map_width);
    const u32 view_mini_height = static_cast<u32>(
        (static_cast<u64>(state.minimap.viewport_height_pixels >> 5) *
            mini_height) / map_height);

    // FUN_004ea2d7 centers and clamps in minimap space before converting the
    // upper-left back to a tile-aligned world coordinate.  Converting the
    // pointer to world space first changes the integer-rounding order.
    const i32 max_left = std::max<i32>(0,
        static_cast<i32>(mini_width) - static_cast<i32>(view_mini_width));
    const i32 left = std::clamp(
        local_x - static_cast<i32>(view_mini_width >> 1), 0, max_left);
    const i32 top = std::max(
        local_y - static_cast<i32>(view_mini_height >> 1), 0);
    state.camera_x = static_cast<i32>(
        ((static_cast<i64>(left) * map_width) / mini_width) * 0x20);
    state.camera_y = std::min<i32>(static_cast<i32>(
        ((static_cast<i64>(top) * map_height) / mini_height) * 0x20),
        state.camera_max_y);
}

void ScrollCameraFromEdgeOrKeys(UiOverlayState& state) {
    // FUN_004ea3c9 publishes the edge bit mask itself as the directional
    // cursor index: up=1, right=2, down=4 and left=8.  Only exact client-edge
    // coordinates participate; captured negative/outside coordinates do not.
    state.camera_edge_cursor_index = 0;
    if (state.camera_edge_pointer_valid) {
        if (state.mouse_x == 0) {
            ScrollCameraLeft(state);
            state.camera_edge_cursor_index += 8;
        }
        if (state.screen_width != 0 &&
            state.mouse_x + 1 == static_cast<i32>(state.screen_width)) {
            ScrollCameraRight(state);
            state.camera_edge_cursor_index += 2;
        }
        if (state.mouse_y == 0) {
            ScrollCameraUp(state);
            state.camera_edge_cursor_index += 1;
        }
        if (state.screen_height != 0 &&
            state.mouse_y + 1 == static_cast<i32>(state.screen_height)) {
            ScrollCameraDown(state);
            state.camera_edge_cursor_index += 4;
        }
    }

    // DAT_00868140 suppresses the DIK arrow-key pass when an edge direction
    // actually moved the camera.  At a clamped edge the keys remain live.
    if (state.camera_scroll_dirty) {
        return;
    }
    if (state.camera_left_key_down) {
        ScrollCameraLeft(state);
    }
    if (state.camera_right_key_down) {
        ScrollCameraRight(state);
    }
    if (state.camera_up_key_down) {
        ScrollCameraUp(state);
    }
    if (state.camera_down_key_down) {
        ScrollCameraDown(state);
    }
}

void BeginUiCommandButtonPress(UiOverlayState& state) {
    state.pressed_command_id = 0xffffffffu;
    state.pressed_command_aux = 0xffffffffu;
    state.command_button_press_active = false;
    state.last_hot_region_x = state.mouse_x;
    state.last_hot_region_y = state.mouse_y;
    const UiOverlayHotRegion* region =
        hot_region_at(state, state.mouse_x, state.mouse_y);
    if (region == nullptr) {
        return;
    }
    set_hot_region_result(state, *region);
    if (!can_capture_ui_command_button_press(state, *region)) {
        return;
    }
    state.pressed_command_id = region->record.item_id;
    state.pressed_command_aux = region->record.aux;
    state.command_button_press_active = true;
}

void ReleaseUiCommandButtonPress(UiOverlayState& state) {
    if (state.pressed_command_id == 0xffffffffu) {
        return;
    }
    const u32 pressed = state.pressed_command_id;
    state.pressed_command_id = 0xffffffffu;
    state.pressed_command_aux = 0xffffffffu;
    state.command_button_press_active = false;
    if (!HitTestUiOverlayHotRegion(state, state.mouse_x, state.mouse_y)) {
        return;
    }
    if (state.last_hotkey_command == pressed) {
        DispatchUiOverlayCommandAction(state, pressed);
    }
}

void BeginUiCommandButtonHold(UiOverlayState& state) {
    state.held_command_id = 0xffffffffu;
    if (HitTestUiOverlayHotRegion(state, state.mouse_x, state.mouse_y)) {
        state.held_command_id = state.last_hotkey_command;
    }
}

void ReleaseUiCommandButtonHold(UiOverlayState& state) {
    if (state.held_command_id == 0xffffffffu) {
        return;
    }
    const u32 held = state.held_command_id;
    state.held_command_id = 0xffffffffu;
    if (HitTestUiOverlayHotRegion(state, state.mouse_x, state.mouse_y) &&
        state.last_hotkey_command == held) {
        DispatchUiOverlayHeldCommandAction(state, held);
    }
}

bool CheckPointerInsideMinimapAndPlacementMode(UiOverlayState& state) {
    if (!CheckMouseInsideMinimap(state)) {
        return false;
    }
    const i32 local_x = state.mouse_x - state.minimap.output_x;
    const i32 local_y = state.mouse_y - state.minimap.output_y;
    const i32 world_x = minimap_input_screen_to_world_x(state, local_x);
    const i32 world_y = minimap_command_screen_to_world_y(state, local_y);
    if (state.placement_mode == 2) {
        append_command_action_at_world(state, 0xaau + 2u,
            state.placement_definition_id, kCommandActionPlacement, 0,
            world_x, world_y);
        return true;
    }
    if (state.placement_mode == 0x0eu) {
        append_command_action_at_world(state, 0x0eu,
            state.placement_equipment_slot_code, kCommandActionPlacement, 0,
            world_x, world_y);
        return true;
    }
    if (state.staged_unit_action_id != 0xffffffffu) {
        append_command_action_at_world(state,
            0xaau + state.staged_unit_action_id, 0,
            kCommandActionPlacement, 0, world_x, world_y);
        return true;
    }
    if (state.placement_mode != 0 && state.placement_mode != 6) {
        append_command_action_at_world(state, state.placement_mode,
            state.placement_definition_id, kCommandActionPlacement, 0,
            world_x, world_y);
        return true;
    }
    state.pointer_state |= kPointerMinimapDrag;
    UpdateCameraFromMinimapDrag(state);
    return true;
}

bool CheckPointerInsideMinimapForAction(UiOverlayState& state) {
    if (!point_inside_minimap_rect(state, true)) {
        return false;
    }
    if (state.scenario_ai_profile_override) {
        return true;
    }
    const i32 local_x = state.mouse_x - state.minimap.output_x;
    const i32 local_y = state.mouse_y - state.minimap.output_y;
    state.hover_context.kind = 1;
    state.hover_context.x = minimap_input_screen_to_world_x(state, local_x);
    state.hover_context.y = minimap_command_screen_to_world_y(state, local_y);
    append_command_action_at_world(state, 0xaeu, 0,
        kCommandActionContextual, 1,
        state.hover_context.x, state.hover_context.y);
    return true;
}

bool CheckMouseInsideMinimap(UiOverlayState& state) {
    return point_inside_minimap_rect(state, false);
}

void DispatchUiOverlayCommandAction(UiOverlayState& state, u32 item_id) {
    if (handle_local_command_panel_selector(state, item_id)) {
        return;
    }
    if (item_id == 0x194u || item_id == 0x195u || item_id == 0x196u) {
        state.selected_context_id = item_id;
    }
    const u32 aux = state.last_hotkey_command == item_id ? state.last_hotkey_aux : 0;
    const u32 flags =
        state.last_hotkey_command == item_id ? state.last_hotkey_flags : 0;
    append_command_action(state, item_id, aux, kCommandActionClick, flags);
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
    }
}

void DispatchUiOverlayHeldCommandAction(UiOverlayState& state, u32 item_id) {
    if (handle_local_command_panel_selector(state, item_id)) {
        return;
    }
    const u32 aux = state.last_hotkey_command == item_id ? state.last_hotkey_aux : 0;
    const u32 flags =
        state.last_hotkey_command == item_id ? state.last_hotkey_flags : 0;
    append_command_action(state, item_id, aux, kCommandActionHold, flags);
}

void ResetGameplaySelectionState(UiOverlayState& state) {
    ResetUiOverlayCommandPanelState(state);
    state.staged_unit_action_id = 0xffffffffu;
    state.selected_unit_ids.clear();
    state.selected_unit_id = 0;
    state.selected_unit_type = 0;
    state.selected_unit_owner = 0;
    state.selected_unit_count = 0;
    clear_primary_selection_command_state(state);
    state.selection_rectangle_active = false;
}

void RecountGameplaySelectedUnits(UiOverlayState& state) {
    state.selected_unit_ids.erase(std::remove_if(state.selected_unit_ids.begin(),
        state.selected_unit_ids.end(), [&state](u32 unit_id) {
            return find_unit_by_id(state, unit_id) == nullptr;
        }), state.selected_unit_ids.end());
    state.selected_unit_count = static_cast<u32>(state.selected_unit_ids.size());
    if (state.selected_unit_ids.empty()) {
        state.selected_unit_id = 0;
        state.selected_unit_type = 0;
        state.selected_unit_owner = 0;
        return;
    }
    if (!unit_already_selected(state, state.selected_unit_id)) {
        state.selected_unit_id = state.selected_unit_ids.front();
    }
    if (const UiOverlayMinimapUnit* unit = find_unit_by_id(state, state.selected_unit_id)) {
        state.selected_unit_type = unit->type_id;
        state.selected_unit_owner = unit->owner_id;
    }
}

void NotifyPrimaryGameplayUnitSelected(UiOverlayState& state) {
    if (state.selected_unit_id == 0 || state.callbacks.on_unit_selected == nullptr) {
        return;
    }
    if (const UiOverlayMinimapUnit* unit =
            find_unit_by_id(state, state.selected_unit_id)) {
        state.callbacks.on_unit_selected(state, *unit);
    }
}

bool ClearSelectedUnitMembershipFlagAndRefreshSelection(UiOverlayState& state,
    u32 unit_id) {
    const bool was_primary = state.selected_unit_id == unit_id;
    const std::size_t before = state.selected_unit_ids.size();
    state.selected_unit_ids.erase(std::remove(state.selected_unit_ids.begin(),
        state.selected_unit_ids.end(), unit_id), state.selected_unit_ids.end());
    if (before == state.selected_unit_ids.size()) {
        return false;
    }

    if (was_primary) {
        state.selected_unit_id = 0;
        state.selected_unit_type = 0;
        state.selected_unit_owner = 0;
        clear_primary_selection_command_state(state);
    }
    RecountGameplaySelectedUnits(state);
    BuildSelectedUnitCommandPanel(state);
    return true;
}

UiOverlaySelectionRectScanResult ScanVisibleUnitsInSelectionRect(
    UiOverlayState& state) {
    UiOverlaySelectionRectScanResult result{};
    const UiOverlayRect selection = normalized_selection_rect(state);
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (!UiOverlayUnitVisibleToLocalPlayer(unit) ||
            !rects_intersect(selection, unit_world_rect(unit))) {
            continue;
        }

        const bool local = unit.owner_id == state.local_player_slot;
        if (unit.type_id < 0x60) {
            if (local) {
                result.local_unit_found = true;
                return result;
            }
            result.flags |= 2;
            result.enemy_unit_id = unit.unit_id;
            continue;
        }

        if (local) {
            result.flags |= 4;
            result.local_object_id = unit.unit_id;
        } else {
            result.flags |= 8;
            result.enemy_object_id = unit.unit_id;
        }
    }
    return result;
}

bool unit_is_local_small_selection_candidate(
    const UiOverlayState& state, const UiOverlayMinimapUnit& unit) {
    return UiOverlayUnitVisibleToLocalPlayer(unit) &&
        unit.type_id < 0x60 && unit.owner_id == state.local_player_slot;
}

const UiOverlayMinimapUnit* best_local_small_unit_in_selection_rect(
    const UiOverlayState& state) {
    const UiOverlayRect selection = normalized_selection_rect(state);
    const UiOverlayMinimapUnit* best = nullptr;
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (!unit_is_local_small_selection_candidate(state, unit) ||
            !rects_intersect(selection, unit_world_rect(unit))) {
            continue;
        }
        if (best == nullptr || unit.selection_score > best->selection_score) {
            best = &unit;
        }
    }
    return best;
}

const UiOverlayMinimapUnit* first_visible_unit_in_selection_rect_by_priority(
    const UiOverlayState& state) {
    const UiOverlayRect selection = normalized_selection_rect(state);
    const UiOverlayMinimapUnit* enemy_unit = nullptr;
    const UiOverlayMinimapUnit* local_object = nullptr;
    const UiOverlayMinimapUnit* enemy_object = nullptr;
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (!UiOverlayUnitVisibleToLocalPlayer(unit) ||
            !rects_intersect(selection, unit_world_rect(unit))) {
            continue;
        }
        if (unit.type_id < 0x60) {
            if (unit.owner_id == state.local_player_slot ||
                CheckScenarioSelectionOverride(state)) {
                return &unit;
            }
            if (enemy_unit == nullptr) {
                enemy_unit = &unit;
            }
            continue;
        }
        if (unit.owner_id == state.local_player_slot) {
            if (local_object == nullptr) {
                local_object = &unit;
            }
        } else if (enemy_object == nullptr) {
            enemy_object = &unit;
        }
    }
    if (enemy_unit != nullptr) {
        return enemy_unit;
    }
    if (local_object != nullptr) {
        return local_object;
    }
    return enemy_object;
}

bool select_clicked_unit_by_original_priority(UiOverlayState& state) {
    if (const UiOverlayMinimapUnit* local =
            best_local_small_unit_in_selection_rect(state)) {
        if (select_unit(state, *local, true)) {
            NotifyPrimaryGameplayUnitSelected(state);
        }
        return true;
    }
    if (const UiOverlayMinimapUnit* unit =
            first_visible_unit_in_selection_rect_by_priority(state)) {
        if (select_unit(state, *unit, true)) {
            NotifyPrimaryGameplayUnitSelected(state);
        }
        return true;
    }
    return false;
}

void SelectLocalSameTypeUnitsFromDragRectangle(UiOverlayState& state) {
    const UiOverlayRect selection = normalized_selection_rect(state);
    const UiOverlayMinimapUnit* primary = nullptr;
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (unit_is_local_small_selection_candidate(state, unit) &&
            rects_intersect(selection, unit_world_rect(unit))) {
            primary = &unit;
            break;
        }
    }
    if (primary == nullptr) {
        return;
    }

    const u32 reference_type = primary->type_id;
    const u32 reference_flags = primary->runtime_flags & 0x31u;
    bool selection_changed = select_unit(state, *primary, true);

    if (state.selected_unit_ids.size() < state.max_selected_unit_count) {
        UiOverlayRect viewport{};
        viewport.x = state.camera_x;
        viewport.y = state.camera_y;
        viewport.width = std::max<u32>(1, state.screen_width);
        viewport.height = std::max<u32>(1, state.screen_height);
        for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
            if (unit.unit_id == primary->unit_id ||
                !unit_is_local_small_selection_candidate(state, unit) ||
                unit.type_id != reference_type ||
                (unit.runtime_flags & 0x31u) != reference_flags ||
                !rects_intersect(viewport, unit_world_rect(unit))) {
                continue;
            }
            selection_changed = select_unit(state, unit, false) || selection_changed;
            if (state.selected_unit_ids.size() >= state.max_selected_unit_count) {
                break;
            }
        }
    }
    if (selection_changed) {
        NotifyPrimaryGameplayUnitSelected(state);
    }
}

void SelectUnitsInDragRectangle(UiOverlayState& state) {
    if (!state.additive_selection_mode) {
        ResetGameplaySelectionState(state);
        SelectLocalSameTypeUnitsFromDragRectangle(state);
        BuildSelectedUnitCommandPanel(state);
        return;
    }
    AddUnitsInDragRectangleToSelection(state);
    BuildSelectedUnitCommandPanel(state);
}

void ResolveGameplayClickSelection(UiOverlayState& state) {
    const i32 drag_width = std::abs(state.selection_right - state.selection_left);
    const i32 drag_height = std::abs(state.selection_bottom - state.selection_top);
    if (drag_width >= 5 || drag_height >= 5) {
        SelectUnitsInDragRectangle(state);
        return;
    }

    if (!FindUnitUnderStoredPointer(state)) {
        if (!state.additive_selection_mode) {
            ResetGameplaySelectionState(state);
        }
        return;
    }

    if (state.additive_selection_mode) {
        if (unit_already_selected(state, state.hover_context.unit_id)) {
            ToggleUnitSelectionState(state);
        } else if (const UiOverlayMinimapUnit* unit =
                       find_unit_by_id(state, state.hover_context.unit_id)) {
            if (unit_is_local_small_selection_candidate(state, *unit)) {
                if (select_unit(state, *unit, false)) {
                    NotifyPrimaryGameplayUnitSelected(state);
                }
            }
        }
    } else {
        ResetGameplaySelectionState(state);
        select_clicked_unit_by_original_priority(state);
    }
    BuildSelectedUnitCommandPanel(state);
}

UiOverlayDoubleClickSelectionResult ResolveGameplayDoubleClickSelection(
    UiOverlayState& state) {
    // FUN_004e9ed0 handles code 0x20 before the ordinary press/release state.
    // Preserve its gate order: scripted lockout, actionable hot record,
    // minimap/placement side effects, opaque interface pixel, then point scan.
    if (state.scripted_input_restricted) {
        return UiOverlayDoubleClickSelectionResult::ignored;
    }

    if (const UiOverlayHotRegion* region =
            hot_region_at(state, state.mouse_x, state.mouse_y)) {
        set_hot_region_result(state, *region);
        if (can_capture_ui_command_button_press(state, *region)) {
            return UiOverlayDoubleClickSelectionResult::ignored;
        }
    }

    if (CheckPointerInsideMinimapAndPlacementMode(state)) {
        return UiOverlayDoubleClickSelectionResult::ignored;
    }
    if (state.mouse_y >= static_cast<i32>(state.world_viewport_height) &&
        CheckUiOverlayIconMaskPixel(state, state.mouse_x, state.mouse_y)) {
        return UiOverlayDoubleClickSelectionResult::ignored;
    }

    const UiOverlayMinimapUnit* target =
        double_click_unit_at_screen_point(state);
    if (target == nullptr) {
        // Carry clear at 004ea1c6 jumps directly to the ignored exit.  It does
        // not rewrite code 0x20 into the release fallback.
        return UiOverlayDoubleClickSelectionResult::ignored;
    }
    if (target->owner_id != state.local_player_slot || target->type_id >= 0x60u) {
        return UiOverlayDoubleClickSelectionResult::fallback_release;
    }

    bool primary_is_remote = false;
    if (state.selected_unit_id != 0) {
        const UiOverlayMinimapUnit* primary =
            find_unit_by_id(state, state.selected_unit_id);
        primary_is_remote = primary != nullptr
            ? primary->owner_id != state.local_player_slot
            : state.selected_unit_owner != state.local_player_slot;
    }
    if (!state.shift_modifier_down || primary_is_remote) {
        ResetGameplaySelectionState(state);
    }
    else {
        RecountGameplaySelectedUnits(state);
    }

    constexpr std::size_t kOriginalSelectionCapacity = 14;
    const std::size_t configured_capacity = state.max_selected_unit_count == 0
        ? kOriginalSelectionCapacity
        : static_cast<std::size_t>(state.max_selected_unit_count);
    const std::size_t selection_capacity =
        std::min(kOriginalSelectionCapacity, configured_capacity);
    std::size_t selection_budget =
        std::min(state.selected_unit_ids.size(), selection_capacity);
    const auto append_expansion_if_room = [&](const UiOverlayMinimapUnit& unit) {
        if (unit_already_selected(state, unit.unit_id) ||
            selection_budget >= selection_capacity) {
            return false;
        }
        state.selected_unit_ids.push_back(unit.unit_id);
        ++selection_budget;
        return true;
    };

    bool target_selected = unit_already_selected(state, target->unit_id);
    if (selection_budget < selection_capacity) {
        // FUN_004ead82 consumes one local-count slot for the clicked unit even
        // when Shift preserved its existing membership.  Keep the vector
        // unique, but retain that original cap-budget quirk.
        ++selection_budget;
        if (!target_selected) {
            state.selected_unit_ids.push_back(target->unit_id);
            target_selected = true;
        }
    }
    const u32 reference_type = target->type_id;
    const u32 reference_flags = target->runtime_flags & 0x31u;
    if (target_selected) {
        state.selected_unit_id = target->unit_id;
        state.selected_unit_type = target->type_id;
        state.selected_unit_owner = target->owner_id;
    }

    if (selection_budget < selection_capacity) {
        for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
            if (unit.unit_id == target->unit_id ||
                unit.owner_id != target->owner_id || unit.type_id != reference_type ||
                (unit.runtime_flags & 0x31u) != reference_flags ||
                !double_click_unit_visibility_passes(state, unit, true) ||
                !double_click_unit_intersects_viewport(state, unit)) {
                continue;
            }
            append_expansion_if_room(unit);
            if (selection_budget >= selection_capacity) {
                break;
            }
        }
    }

    RecountGameplaySelectedUnits(state);
    if (target_selected && unit_already_selected(state, target->unit_id)) {
        // Recount may discard stale preserved ids; restore the clicked unit as
        // the finalized primary exactly once before panel rebuild and voice.
        state.selected_unit_id = target->unit_id;
        state.selected_unit_type = target->type_id;
        state.selected_unit_owner = target->owner_id;
    }
    BuildSelectedUnitCommandPanel(state);
    if (target_selected) {
        NotifyPrimaryGameplayUnitSelected(state);
    }
    return UiOverlayDoubleClickSelectionResult::selected;
}

void AddUnitsInDragRectangleToSelection(UiOverlayState& state) {
    const UiOverlayRect selection = normalized_selection_rect(state);
    if (state.selected_unit_id != 0) {
        const UiOverlayMinimapUnit* selected =
            find_unit_by_id(state, state.selected_unit_id);
        if (selected != nullptr &&
            (selected->type_id >= 0x60 ||
                selected->owner_id != state.local_player_slot)) {
            return;
        }
    }

    u32 reference_type = 0xffffffffu;
    if (state.box_select_same_type_only && state.selected_unit_id != 0) {
        if (const UiOverlayMinimapUnit* selected =
                find_unit_by_id(state, state.selected_unit_id)) {
            reference_type = selected->type_id;
        }
    }

    bool selection_changed = false;
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (!unit_is_local_small_selection_candidate(state, unit)) {
            continue;
        }
        if (reference_type != 0xffffffffu && unit.type_id != reference_type) {
            continue;
        }
        if (rects_intersect(selection, unit_world_rect(unit))) {
            selection_changed = select_unit(state, unit, false) || selection_changed;
        }
        if (state.selected_unit_ids.size() >= state.max_selected_unit_count) {
            break;
        }
    }
    RecountGameplaySelectedUnits(state);
    if (selection_changed) {
        NotifyPrimaryGameplayUnitSelected(state);
    }
}

void AddLocalUnitsInDragRectangleWithModifiers(UiOverlayState& state) {
    AddUnitsInDragRectangleToSelection(state);
}

bool FindSelectableUnitInDragRectangle(UiOverlayState& state) {
    const UiOverlayRect selection = normalized_selection_rect(state);
    for (const UiOverlayMinimapUnit& unit : state.minimap_units) {
        if (unit_selectable_for_local_player(state, unit) &&
            rects_intersect(selection, unit_world_rect(unit))) {
            set_hover_from_unit(state, unit, unit.world_x - state.camera_x,
                unit.world_y - state.camera_y);
            return true;
        }
    }
    return false;
}

void ToggleUnitSelectionState(UiOverlayState& state) {
    const u32 unit_id = state.hover_context.unit_id;
    if (unit_id == 0) {
        return;
    }
    if (unit_already_selected(state, unit_id)) {
        deselect_unit(state, unit_id);
    }
}

bool CheckScenarioSelectionOverride(const UiOverlayState& state) {
    return state.replay_timing_enabled || state.local_player_type == 2;
}

bool DrawUiSmallSlot1(u32 context_id) {
    return draw_small_slot(context_id, 1, g_ui_overlay_state.small_slot1_x,
        g_ui_overlay_state.small_slot1_y);
}

bool DrawUiSmallSlot2(u32 context_id) {
    return draw_small_slot(context_id, 5, g_ui_overlay_state.small_slot2_x,
        g_ui_overlay_state.small_slot2_y);
}

bool DrawUiSmallSlot3(u32 context_id) {
    return draw_small_slot(context_id, 3, g_ui_overlay_state.small_slot3_x,
        g_ui_overlay_state.small_slot3_y);
}

bool DrawUiLargeSlot0(u32 context_id) {
    return draw_large_slot(selected_increment(context_id), g_ui_overlay_state.large_slot_x,
        g_ui_overlay_state.large_slot_y);
}

bool DrawUiLargeSlot3(u32 context_id) {
    return draw_large_slot(3 + selected_increment(context_id), g_ui_overlay_state.large_slot_x,
        g_ui_overlay_state.large_slot_y);
}

bool DrawUiLargeSlot0AndDetails(u32 context_id) {
    const bool ok = DrawUiLargeSlot0(context_id);
    DrawUiLargeSlotDetailTexts(g_ui_overlay_state);
    return ok;
}

bool DrawUiLargeSlot3AndDetails(u32 context_id) {
    const bool ok = DrawUiLargeSlot3(context_id);
    DrawUiLargeSlotDetailTexts(g_ui_overlay_state);
    return ok;
}

bool DrawUiLargeSlot6(u32 context_id, u32 flags) {
    return draw_large_slot(6 + flag_or_selected_offset(context_id, flags),
        g_ui_overlay_state.large_slot3_x, g_ui_overlay_state.large_slot3_y);
}

bool DrawUiLargeSlot9(u32 context_id, u32 flags) {
    return draw_large_slot(9 + flag_or_selected_offset(context_id, flags),
        g_ui_overlay_state.large_slot4_x, g_ui_overlay_state.large_slot4_y);
}

bool DrawUiLargeSlot12(u32 flags) {
    const u32 state_offset = (flags & 2u) != 0 ? 2u : g_ui_overlay_state.alternate_slot_a;
    return draw_large_slot(12 + state_offset, g_ui_overlay_state.large_slot5_x,
        g_ui_overlay_state.large_slot5_y);
}

bool DrawUiLargeSlot15(u32 flags) {
    const u32 state_offset = (flags & 2u) != 0 ? 2u : g_ui_overlay_state.alternate_slot_b;
    return draw_large_slot(15 + state_offset, g_ui_overlay_state.large_slot6_x,
        g_ui_overlay_state.large_slot6_y);
}

bool DrawUiEncodedGlyphRun11px(const char* text, std::size_t count, i32 slot_x, i32 slot_y) {
    return DrawUiGlyphRun(text, count, slot_x + 0x23, slot_y + 0x1f, 0x0b,
        g_ui_overlay_state.glyph_resource_base, 0x26);
}

bool DrawUiEncodedDigitRun9px(const char* text, std::size_t count, i32 slot_x, i32 slot_y) {
    return DrawUiGlyphRun(text, count, slot_x + 0x1a, slot_y + 0x16, 9,
        g_ui_overlay_state.glyph_resource_base, '0');
}

bool DrawUiGlyphRun(const char* text, std::size_t count, i32 right_aligned_x, i32 y,
    i32 advance, u32 resource_base, u8 first_encoded_char) {
    if (text == nullptr || advance <= 0 || resource_base == kInvalidResourceEntry) {
        return false;
    }

    i32 x = right_aligned_x - (static_cast<i32>(count) * advance - advance);
    for (std::size_t i = 0; i < count; ++i) {
        const u8 ch = static_cast<u8>(text[i]);
        if (ch >= first_encoded_char) {
            if (!DrawResourceSpriteNormal(resource_base + ch - first_encoded_char, x, y)) {
                return false;
            }
        }
        x += advance;
    }
    return true;
}

}
