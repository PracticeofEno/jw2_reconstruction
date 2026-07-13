#include "ranker_gameplay_frame_render.h"

#include "ranker_palette_cache.h"
#include "ranker_runtime_resources.h"
#include "ranker_rng.h"
#include "ranker_text_renderer.h"
#include "ranker_ui_screen.h"
#include "ranker_unit_animation.h"
#include "ranker_unit_action.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace ranker {
namespace {

u32 resolve_remembered_structure_overlay_frame_index(u32 frame_base,
    u32 overlay_class, u32 class_stride_factor, u32 class_frame_count,
    u32 packed_blend_factor_value) {
    return frame_base + overlay_class * class_stride_factor * class_frame_count +
        (packed_blend_factor_value & 7u);
}


constexpr std::size_t kUnitDefinitionAnimationDirectionOffset = 0x240c;
constexpr std::size_t kUnitDefinitionOwnerRelationOverlayEntryOffset = 0x380;
constexpr std::size_t kUnitDefinitionOwnerRelationOverlayXOffset = 0x384;
constexpr std::size_t kUnitDefinitionOwnerRelationOverlayYOffset = 0x388;
constexpr std::size_t kUnitDefinitionHealthBarXOffset = 0x350;
constexpr std::size_t kUnitDefinitionHealthBarYOffset = 0x354;
constexpr std::size_t kUnitDefinitionHealthBarWidthOffset = 0x358;
constexpr std::size_t kUnitDefinitionAnimationFrameOffsetTableBase = 0x140c;
constexpr std::size_t kUnitDefinitionAnimationFrameOffsetTableStride = 0x100;
constexpr std::size_t kUnitDefinitionAnimationRowOffsetTableBase = 0x2248;
constexpr std::size_t kUnitDefinitionAnimationRowOffsetTableStride = 0x20;
constexpr std::size_t kUnitDefinitionQueuedCommandFrameCountOffset = 0x13dc;
constexpr std::size_t kUnitDefinitionMovingAltFrameCountOffset = 0x13f0;
constexpr std::size_t kUnitDefinitionMovingFrameCountOffset = 0x1404;
constexpr std::size_t kUnitDefinitionSpecialDrawFlagOffset = 0x340;
constexpr std::size_t kUnitDefinitionCellBaseBlitModeOffset = 0x348;
constexpr std::size_t kUnitDefinitionCellFlag4BlitModeOffset = 0x349;
constexpr std::size_t kUnitDefinitionCellFlag40BlitModeOffset = 0x34a;
constexpr std::size_t kUnitDefinitionLowHealthOverlayCountOffset = 0x13e0;
constexpr std::size_t kUnitDefinitionLowHealthOverlayDrawModeOffset = 0x338;
constexpr std::size_t kUnitDefinitionLowHealthOverlayFrameTableBase = 0x170c;
constexpr std::size_t kUnitDefinitionLowHealthOverlayXOffsetTableBase = 0x180c;
constexpr std::size_t kUnitDefinitionLowHealthOverlayYOffsetTableBase = 0x190c;
constexpr std::size_t kUnitDefinitionShadowAttachmentCountOffset = 0x13f0;
constexpr std::size_t kUnitDefinitionShadowDebrisSelectorOffset = 0x13fc;
constexpr std::size_t kUnitDefinitionAlternateMovingFrameCountOffset = 0x13f4;
constexpr std::size_t kUnitDefinitionAlternateIdleFrameCountOffset = 0x1408;
constexpr std::size_t kUnitDefinitionShadowDrawModeOffset = 0x33c;
constexpr std::size_t kUnitDefinitionShadowAttachmentFrameTableBase = 0x1b0c;
constexpr std::size_t kUnitDefinitionShadowAttachmentXOffsetTableBase = 0x1c0c;
constexpr std::size_t kUnitDefinitionShadowAttachmentYOffsetTableBase = 0x1d0c;
constexpr std::size_t kUnitDefinitionActionFallbackExtendedFlagOffset = 0x220c;
constexpr std::size_t kUnitDefinitionActionFallbackExtendedRowStrideOffset = 0x2270;
constexpr std::size_t kUnitDefinitionOverlayClassOffset = 0x180;
constexpr u32 kJw211StatusOverlayRecord = 36;
constexpr u32 kJw211RememberedStructureOverlayRecord = 39;
constexpr u32 kJw211SpecialOverlayRecord = 43;
constexpr u32 kJw211DirectSpriteRecord = 0;
constexpr std::size_t kAuxOverlayClassStrideFactorOffset = 0x210;
constexpr std::size_t kAuxOverlayDrawModeOffset = 0x21c;
constexpr std::size_t kAuxOverlayFrameCycleCountOffset = 0x228;
constexpr std::size_t kAuxOverlayClassFrameCountOffset = 0x22c;
constexpr std::size_t kAuxOverlayFrameTableBase = 0x630;

GameplayTextExtent default_measure_text(const char* text) {
    GameplayTextExtent extent{};
    if (text == nullptr) {
        return extent;
    }
    extent.width = static_cast<u32>(std::strlen(text) * 8);
    extent.height = 16;
    return extent;
}

GameplayTextExtent measure_text(GameplayHudTextState& state, const char* text) {
    if (state.callbacks.measure_text != nullptr) {
        return state.callbacks.measure_text(state, text);
    }
    return default_measure_text(text);
}

void select_draw_font(GameplayHudTextState& state) {
    if (state.callbacks.select_draw_font != nullptr) {
        state.callbacks.select_draw_font(state);
    }
}

void select_metric_font(GameplayHudTextState& state) {
    if (state.callbacks.select_metric_font != nullptr) {
        state.callbacks.select_metric_font(state);
    }
}

void draw_shadow_text(
    GameplayHudTextState& state, const char* text, i32 x, i32 y, u8 color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    if (state.callbacks.draw_shadow_and_advance != nullptr) {
        state.callbacks.draw_shadow_and_advance(state, text, x, y, color);
        return;
    }
    if (state.callbacks.draw_text != nullptr) {
        state.callbacks.draw_text(state, text, x, y, color);
    }
}

void draw_text(GameplayHudTextState& state, const char* text, i32 x, i32 y, u8 color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    if (state.callbacks.draw_text != nullptr) {
        state.callbacks.draw_text(state, text, x, y, color);
    }
}

void draw_centered_text(
    GameplayHudTextState& state, const char* text, i32 x, i32 y, u8 color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    if (state.callbacks.draw_centered_text != nullptr) {
        state.callbacks.draw_centered_text(state, text, x, y, color);
        return;
    }
    draw_text(state, text, x, y, color);
}

std::string format_i32(i32 value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

i32 resource_text_width(GameplayPlayerResourceHudState& state,
    const std::string& text) {
    if (state.callbacks.measure_text != nullptr) {
        return static_cast<i32>(state.callbacks.measure_text(
            state, text.c_str()).width);
    }
    return static_cast<i32>(text.size() * 8);
}

void emit_player_resource_sprite(GameplayPlayerResourceHudState& state, u32 player,
    u32 sprite_entry, i32 x, i32 y) {
    state.draw_requests.push_back(GameplayPlayerResourceHudDrawRequest{
        GameplayPlayerResourceHudDrawKind::sprite, player, sprite_entry, {}, x, y, 1, false});
    if (state.callbacks.draw_sprite != nullptr) {
        state.callbacks.draw_sprite(state, sprite_entry, x, y, player);
    }
}

i32 emit_player_resource_text(GameplayPlayerResourceHudState& state, u32 player,
    const std::string& text, i32 x, i32 y, u8 color, bool centered = false) {
    if (text.empty()) {
        return x;
    }
    state.draw_requests.push_back(GameplayPlayerResourceHudDrawRequest{
        GameplayPlayerResourceHudDrawKind::text, player, 0, text, x, y, color, centered});
    const i32 width = resource_text_width(state, text);
    if (state.callbacks.draw_text != nullptr) {
        state.callbacks.draw_text(state, text.c_str(), x, y, color, centered);
    }
    return x + width;
}

std::string player_display_name(const GameplayPlayerResourceHudPlayer& player) {
    if (player.name.size() <= 20) {
        return player.name;
    }
    return player.name.substr(0, 20);
}

const char* current_message_text(const GameplayHudMessage& message) {
    return message.text != nullptr && message.text[0] != '\0' ? message.text : nullptr;
}

i32 centered_x_for_text(GameplayHudTextState& state, const char* text) {
    const GameplayTextExtent extent = measure_text(state, text);
    return static_cast<i32>((state.screen_width - std::min(state.screen_width, extent.width)) >> 1);
}

void recompute_message_center(GameplayHudTextState& state, GameplayHudMessage& message) {
    if (current_message_text(message) == nullptr) {
        message.x = 0;
        return;
    }
    message.x = centered_x_for_text(state, message.text);
}

void render_selected_status(GameplayHudTextState& state) {
    GameplayHudSelectedStatus& selected = state.selected_status;
    if (!selected.active) {
        return;
    }

    const std::size_t category =
        std::min<std::size_t>(selected.category, selected.category_labels.size() - 1);
    const u8 color = selected.category_colors[category] != 0 ?
        selected.category_colors[category] : 1;
    const char* label = selected.category_labels[category];
    i32 x = 0x14;
    const i32 y = static_cast<i32>(state.screen_height) - 0x14;

    if (label != nullptr) {
        draw_shadow_text(state, label, x, y, color);
        x += static_cast<i32>(measure_text(state, label).width);
    }
    if (!selected.typed_text.empty()) {
        draw_shadow_text(state, selected.typed_text.c_str(), x, y, 1);
        x += static_cast<i32>(measure_text(state, selected.typed_text.c_str()).width);
    }
    if (selected.extra_text_active && !selected.extra_text.empty()) {
        draw_shadow_text(state, selected.extra_text.c_str(), x, y, 1);
        x += static_cast<i32>(measure_text(state, selected.extra_text.c_str()).width);
    }
    if ((state.frame_counter & 4u) == 0 && selected.blink_text != nullptr) {
        draw_shadow_text(state, selected.blink_text, x, y, 1);
    }
}

void promote_queued_message_if_ready(GameplayHudTextState& state) {
    if (current_message_text(state.queued_message) == nullptr) {
        return;
    }

    if (state.bottom_text_y == state.queued_message.y) {
        state.current_message = state.queued_message;
        state.current_message.tick_ms = state.current_tick_ms;
        state.queued_message = {};
    }
}

void render_queued_message(GameplayHudTextState& state) {
    if (current_message_text(state.queued_message) == nullptr) {
        return;
    }

    promote_queued_message_if_ready(state);
    if (current_message_text(state.queued_message) == nullptr) {
        return;
    }

    draw_shadow_text(state, state.queued_message.text, state.queued_message.x,
        state.queued_message.y, 1);
    if (state.current_tick_ms - state.queued_message.tick_ms >
        kGameplayHudScrollStepDelayMs - 1) {
        state.queued_message.tick_ms = state.current_tick_ms;
        const i32 delta = state.queued_message.y - state.bottom_text_y;
        state.queued_message.y -= (delta >> 1) + 1;
    }
}

void render_current_message(GameplayHudTextState& state) {
    if (current_message_text(state.current_message) == nullptr) {
        return;
    }
    draw_shadow_text(state, state.current_message.text, state.current_message.x,
        state.current_message.y, 1);
    if (state.current_tick_ms - state.current_message.tick_ms >=
        kGameplayHudMessageLifetimeMs) {
        state.current_message = {};
    }
}

void frame_callback(GameplayFrameRenderContext& context, GameplayFrameCallback callback) {
    if (callback != nullptr) {
        callback(context);
    }
}

const GameplayRenderUnitSpriteDefinition* resolve_unit_definition(
    const GameplayRenderCommandQueue& queue, u32 type_id) {
    if (type_id >= queue.definition_index_by_type.size()) {
        return nullptr;
    }
    const u32 definition_index = queue.definition_index_by_type[type_id];
    if (definition_index >= queue.unit_sprite_definitions.size()) {
        return nullptr;
    }
    return &queue.unit_sprite_definitions[definition_index];
}

const UnitDefinitionResourceRecord* loaded_unit_definition_record(u32 unit_type) {
    if (!unit_definition_resource_catalog_state().loaded) {
        LoadUnitDefinitionResourceCatalog();
    }

    const UnitDefinitionResourceCatalogState& catalog =
        unit_definition_resource_catalog_state();
    if (unit_type >= catalog.records.size()) {
        return nullptr;
    }
    const UnitDefinitionResourceRecord& record = catalog.records[unit_type];
    if (!record.loaded || record.definition_bytes.empty()) {
        return nullptr;
    }
    return &record;
}

i32 read_unit_definition_i32(u32 unit_type, std::size_t offset, i32 fallback) {
    const UnitDefinitionResourceRecord* record = loaded_unit_definition_record(unit_type);
    if (record == nullptr || offset + sizeof(u32) > record->definition_bytes.size()) {
        return fallback;
    }
    const std::vector<u8>& bytes = record->definition_bytes;
    const u32 value = static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
    return static_cast<i32>(value);
}

u32 read_unit_definition_u8(u32 unit_type, std::size_t offset, u32 fallback) {
    const UnitDefinitionResourceRecord* record = loaded_unit_definition_record(unit_type);
    if (record == nullptr || offset >= record->definition_bytes.size()) {
        return fallback;
    }
    return record->definition_bytes[offset];
}

u32 read_unit_definition_u32(
    const UnitDefinitionResourceRecord& record, std::size_t offset, u32 fallback) {
    if (offset + sizeof(u32) > record.definition_bytes.size()) {
        return fallback;
    }
    const std::vector<u8>& bytes = record.definition_bytes;
    return static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

i32 read_unit_definition_record_i32(
    const UnitDefinitionResourceRecord& record, std::size_t offset, i32 fallback) {
    return static_cast<i32>(
        read_unit_definition_u32(record, offset, static_cast<u32>(fallback)));
}

u32 health_ratio_31(u32 value, u32 maximum) {
    if (maximum == 0) {
        return 0;
    }
    return static_cast<u32>((static_cast<u64>(value) * 0x1fu) / maximum);
}

u32 remembered_stage_blend_factor(u32 value, u32 maximum) {
    // FUN_004c5546 indexes DAT_0072c4b0 after calculating HP * 31 / max HP.
    // That 32-entry table clamps ratio + 6 to the inclusive range [10, 31].
    return std::clamp<u32>(health_ratio_31(value, maximum) + 6u, 10u, 0x1fu);
}

bool unit_cell_draws_construction_stage(const UnitRenderItem& item) {
    const bool flag4_path = (item.command_metadata_flags & 0x4u) != 0 ||
        (item.command_metadata_flags == 0 && (item.terrain_cell_flags & 0x4u) != 0);
    if (flag4_path) {
        return (item.definition_cell_flags & 0x2u) != 0;
    }

    const bool flag40_path = (item.command_metadata_flags & 0x40u) != 0 ||
        (item.command_metadata_flags == 0 && (item.terrain_cell_flags & 0x40u) != 0);
    if (flag40_path) {
        return (item.definition_cell_flags & 0x4u) != 0;
    }
    return (item.definition_cell_flags & 0x1u) != 0;
}

void update_explored_fog_structure_snapshot(
    UnitRenderQueueContext& render_context, const UnitRenderItem& item) {
    GameplayVisibilityGrid* grid = render_context.visibility.authoritative_grid;
    if (grid == nullptr || grid->width == 0 || grid->height == 0) {
        return;
    }

    // FUN_004c523d constructs DAT_0072c6c0 from raw unit +0xc0/+0xc4.
    // Those current-cell coordinates are distinct from +0xb8/+0xbc, which
    // ProcessVisibleUnitRenderQueue uses for its initial visibility gate.
    const u32 tile_x = static_cast<u32>(item.visibility_cell_x) >> 5;
    const u32 tile_y = static_cast<u32>(item.visibility_cell_y) >> 5;
    if (tile_x >= grid->width || tile_y >= grid->height) {
        return;
    }
    const std::size_t cell_index =
        static_cast<std::size_t>(tile_y) * grid->width + tile_x;
    if (cell_index >= grid->current.size() || cell_index >= grid->previous.size()) {
        return;
    }

    if ((item.state_flags & kUnitAnimStateDirectSprite) != 0) {
        return;
    }

    const UnitDefinitionResourceRecord* record =
        loaded_unit_definition_record(item.type_id);
    if (item.cell_construction_progress_active) {
        if (item.construction_stage_count == 0) {
            return;
        }

        u32 frame = item.construction_stage_count - 1u;
        if (item.construction_progress_limit != 0) {
            if (frame == 0) {
                // FUN_004c573c's one-frame HP-fade path skips the snapshot
                // entirely for draw mode 2/80.
                if ((item.draw_flags & kUnitAnimDrawMode2) != 0) {
                    return;
                }
                u32 packed = (grid->previous[cell_index] & 0xf8000fffu) |
                    0x80000000u;
                if (item.max_hit_points != 0) {
                    packed |= health_ratio_31(
                        item.hit_points, item.max_hit_points) << 22;
                }
                grid->previous[cell_index] = packed;
                return;
            }
            frame = static_cast<u32>(
                (static_cast<u64>(frame) * item.construction_progress) /
                item.construction_progress_limit);
        }
        grid->previous[cell_index] =
            (grid->previous[cell_index] & 0xf8000fffu) |
            (frame << 18) | 0x80000000u;
        return;
    }

    if (item.cell_channel_additive_active) {
        return;
    }

    if (item.construction_stage_count != 0 &&
        unit_cell_draws_construction_stage(item)) {
        u32 packed = (grid->previous[cell_index] & 0x7803ffffu) |
            ((item.construction_stage_count - 1u) << 18);
        const u32 special_draw = record != nullptr
            ? read_unit_definition_u32(
                *record, kUnitDefinitionSpecialDrawFlagOffset, 0)
            : 0;
        if ((item.draw_flags & kUnitAnimDrawMode2) == 0 &&
            (item.command_flags & 0x40u) == 0 && special_draw == 1 &&
            item.max_hit_points != 0) {
            packed |= remembered_stage_blend_factor(
                item.hit_points, item.max_hit_points) << 22;
        }
        grid->previous[cell_index] = packed;
    }

    if (item.max_hit_points == 0 ||
        item.hit_points >= item.max_hit_points - (item.max_hit_points >> 2)) {
        return;
    }
    const u32 overlay_count = record != nullptr
        ? read_unit_definition_u32(
            *record, kUnitDefinitionLowHealthOverlayCountOffset, 0)
        : 0;
    if (overlay_count == 0) {
        return;
    }

    u32 severity = 0;
    const u32 quarter = item.max_hit_points >> 2;
    if (item.hit_points < item.max_hit_points - quarter) {
        severity = 0x15;
        if (item.hit_points < item.max_hit_points - quarter - quarter) {
            severity += 0x15;
        }
    }
    grid->current[cell_index] =
        (grid->current[cell_index] & 0xfffc0fffu) |
        ((item.low_health_overlay_frame + severity + 1u) << 12);
}

u32 read_auxiliary_record_u32(
    const AuxiliaryRuntimeCatalogRecord& record, std::size_t offset, u32 fallback) {
    if (offset + sizeof(u32) > record.definition_bytes.size()) {
        return fallback;
    }
    const std::vector<u8>& bytes = record.definition_bytes;
    return static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

u32 auxiliary_image_entry(
    const AuxiliaryRuntimeCatalogRecord& record, u32 frame_index) {
    if (frame_index < record.image_resource_entries.size()) {
        return record.image_resource_entries[frame_index];
    }
    if (!record.image_resource_entries.empty()) {
        return record.image_resource_entries.front() + frame_index;
    }
    return kInvalidResourceEntry;
}

enum class UnitAnimationFrameIndexMode {
    direct,
    frame_table_only,
    frame_and_row_tables,
    frame_and_extended_row_stride,
};

UnitAnimationFrameIndexMode unit_animation_frame_index_mode(
    UnitAnimationSequence sequence) {
    switch (sequence) {
    case UnitAnimationSequence::cell_base:
    case UnitAnimationSequence::cell_flag4:
    case UnitAnimationSequence::cell_flag40:
        return UnitAnimationFrameIndexMode::frame_table_only;
    case UnitAnimationSequence::cell_construction:
    case UnitAnimationSequence::cell_progress:
    case UnitAnimationSequence::cell_channel_additive:
    case UnitAnimationSequence::low_health_overlay:
    case UnitAnimationSequence::shadow_attachment:
        return UnitAnimationFrameIndexMode::direct;
    case UnitAnimationSequence::default_idle:
    case UnitAnimationSequence::alternate_default:
    case UnitAnimationSequence::moving:
    case UnitAnimationSequence::action:
    case UnitAnimationSequence::action_fallback:
        return UnitAnimationFrameIndexMode::frame_and_row_tables;
    case UnitAnimationSequence::action_fallback_extended:
        return UnitAnimationFrameIndexMode::frame_and_extended_row_stride;
    case UnitAnimationSequence::alternate_action:
    case UnitAnimationSequence::conditional_alternate_action:
    case UnitAnimationSequence::direct_action:
    case UnitAnimationSequence::queued_command:
    case UnitAnimationSequence::direct_sprite:
    case UnitAnimationSequence::direct_timed:
    case UnitAnimationSequence::moving_action_primary:
    case UnitAnimationSequence::moving_action_alternate:
    case UnitAnimationSequence::forced_highlight:
    default:
        return UnitAnimationFrameIndexMode::frame_and_row_tables;
    }
}

u32 unit_animation_frame_table_group(UnitAnimationSequence sequence, u32 group) {
    switch (sequence) {
    case UnitAnimationSequence::default_idle:
    case UnitAnimationSequence::direct_timed:
        return group == 1 ? 0 : group;
    default:
        return group;
    }
}

u32 original_unit_animation_frame_index(const UnitAnimationDrawCommand& command,
    u32 group, u32 fallback_frame_index) {
    if (command.unit == nullptr || group >= kUnitDefinitionImageGroupCount) {
        return fallback_frame_index;
    }
    const UnitAnimationFrameIndexMode mode =
        unit_animation_frame_index_mode(command.sequence);
    if (mode == UnitAnimationFrameIndexMode::direct) {
        return command.animation_frame;
    }
    const UnitDefinitionResourceRecord* record =
        loaded_unit_definition_record(command.unit->type_id);
    if (record == nullptr) {
        return fallback_frame_index;
    }

    const u32 frame_table_group =
        unit_animation_frame_table_group(command.sequence, group);
    const std::size_t frame_offset =
        kUnitDefinitionAnimationFrameOffsetTableBase +
        static_cast<std::size_t>(frame_table_group) *
            kUnitDefinitionAnimationFrameOffsetTableStride +
        static_cast<std::size_t>(command.animation_frame) * sizeof(u32);
    const std::size_t row_offset =
        kUnitDefinitionAnimationRowOffsetTableBase +
        static_cast<std::size_t>(group) * kUnitDefinitionAnimationRowOffsetTableStride +
        static_cast<std::size_t>(command.direction_row) * sizeof(u32);

    const u32 frame_base = read_unit_definition_u32(*record, frame_offset, 0xffffffffu);
    if (mode == UnitAnimationFrameIndexMode::frame_table_only) {
        return frame_base != 0xffffffffu ? frame_base : fallback_frame_index;
    }
    if (mode == UnitAnimationFrameIndexMode::frame_and_extended_row_stride) {
        const u32 row_stride = read_unit_definition_u32(*record,
            kUnitDefinitionActionFallbackExtendedRowStrideOffset, 0xffffffffu);
        if (frame_base == 0xffffffffu || row_stride == 0xffffffffu) {
            return fallback_frame_index;
        }
        return frame_base + command.direction_row * row_stride;
    }
    const u32 row_base = read_unit_definition_u32(*record, row_offset, 0xffffffffu);
    if (frame_base == 0xffffffffu || row_base == 0xffffffffu) {
        return fallback_frame_index;
    }
    return frame_base + row_base;
}

u32 packed_type_id(const GameplayRenderCommand& command) {
    return command.packed_flags & kGameplayRenderPackedTypeMask;
}

u32 packed_blend_factor(const GameplayRenderCommand& command) {
    return (command.packed_flags & kGameplayRenderPackedBlendMask) >> 22;
}

u32 packed_palette_ramp(const GameplayRenderCommand& command) {
    return (command.packed_flags & kGameplayRenderPackedPaletteRampMask) >> 8;
}

u32 packed_overlay_selector(const GameplayRenderCommand& command) {
    return (command.packed_flags & kGameplayRenderPackedOverlayMask) >> 12;
}

bool draw_unit_base_sprite(const GameplayRenderCommand& command,
    const GameplayRenderUnitSpriteDefinition* definition) {
    switch (command.draw_variant) {
    case GameplayRenderSpriteVariant::unit_ramp_low_blue_mask:
        return DrawResourceSpriteUnitRampLowBlueMask(command.sprite_entry_index,
            command.screen_x, command.screen_y);
    case GameplayRenderSpriteVariant::grayscale:
        return DrawResourceSpriteGrayscale(command.sprite_entry_index,
            command.screen_x, command.screen_y);
    case GameplayRenderSpriteVariant::high_green_mask:
        return DrawResourceSpriteHighGreenMask(command.sprite_entry_index,
            command.screen_x, command.screen_y);
    case GameplayRenderSpriteVariant::high_blue_mask:
        return DrawResourceSpriteHighBlueMask(command.sprite_entry_index,
            command.screen_x, command.screen_y);
    case GameplayRenderSpriteVariant::high_red_mask:
        return DrawResourceSpriteHighRedMask(command.sprite_entry_index,
            command.screen_x, command.screen_y);
    case GameplayRenderSpriteVariant::half_blend:
        return DrawResourceSpriteBlendFactor(command.sprite_entry_index,
            command.screen_x, command.screen_y, 0x0f);
    case GameplayRenderSpriteVariant::channel_additive_tint:
        return DrawResourceSpriteChannelAdditiveTint(command.sprite_entry_index,
            command.screen_x, command.screen_y, 0, 0, 0);
    case GameplayRenderSpriteVariant::unit_ramp_token1_shadow:
    default:
        break;
    }

    const u32 blend_factor = packed_blend_factor(command);
    if (definition != nullptr && definition->has_special_draw && blend_factor != 0x1f) {
        return DrawResourceSpriteBlendFactor(command.sprite_entry_index,
            command.screen_x, command.screen_y, blend_factor);
    }
    return DrawResourceSpriteUnitRampToken1Shadow(command.sprite_entry_index,
        command.screen_x, command.screen_y);
}

bool draw_extra_overlays(GameplayRenderCommandQueue& queue,
    const GameplayRenderCommand& command,
    const GameplayRenderUnitSpriteDefinition& definition) {
    const u32 selector = packed_overlay_selector(command);
    if (selector == 0 || definition.overlays.empty() ||
        queue.overlay_base_entry == kInvalidResourceEntry) {
        return true;
    }

    bool ok = true;
    const u32 base_entry = queue.overlay_base_entry + selector - 1;
    for (const GameplayRenderOverlaySprite& overlay : definition.overlays) {
        const u32 entry_index = base_entry + overlay.entry_offset * 0x3f;
        ok = DrawResourceSpriteMode(entry_index,
                 command.screen_x + overlay.x_offset,
                 command.screen_y + overlay.y_offset,
                 definition.blit_mode) && ok;
    }
    return ok;
}

bool draw_highbit_special_overlay(GameplayRenderCommandQueue& queue,
    const GameplayRenderCommand& command,
    const GameplayRenderUnitSpriteDefinition& definition) {
    if (queue.callbacks.highbit_special_overlay != nullptr) {
        return queue.callbacks.highbit_special_overlay(queue, command);
    }
    if (!jw211_runtime_catalog_state().loaded && !LoadJw211RuntimeCatalog()) {
        return false;
    }
    const AuxiliaryRuntimeCatalogState& catalog = jw211_runtime_catalog_state();
    if (kJw211RememberedStructureOverlayRecord >= catalog.records.size()) {
        return false;
    }
    const AuxiliaryRuntimeCatalogRecord& record =
        catalog.records[kJw211RememberedStructureOverlayRecord];
    if (!record.loaded || record.definition_bytes.empty()) {
        return false;
    }

    // FUN_004d80ae (0x004d81ab..0x004d8248) uses JW2_11 record 39.  Its
    // remembered construction/HP phase is the low three bits of the packed
    // blend factor, not a present-time animation counter.
    const u32 frame_base = read_auxiliary_record_u32(
        record, kAuxOverlayFrameTableBase, 0xffffffffu);
    if (frame_base == 0xffffffffu) {
        return false;
    }
    const u32 overlay_class = static_cast<u32>(std::max<i32>(
        read_unit_definition_i32(
            packed_type_id(command), kUnitDefinitionOverlayClassOffset, 0),
        0));
    const u32 class_stride_factor = read_auxiliary_record_u32(
        record, kAuxOverlayClassStrideFactorOffset, 0);
    const u32 class_frame_count = read_auxiliary_record_u32(
        record, kAuxOverlayClassFrameCountOffset, 0);
    const u32 frame_index = resolve_remembered_structure_overlay_frame_index(
        frame_base, overlay_class, class_stride_factor, class_frame_count,
        packed_blend_factor(command));
    const u32 entry_index = auxiliary_image_entry(record, frame_index);
    if (entry_index == kInvalidResourceEntry) {
        return false;
    }
    const i32 x = command.screen_x + definition.center_offset_x +
        (definition.center_width >> 1);
    const i32 y = command.screen_y + definition.center_offset_y +
        (definition.center_height >> 1);
    const u32 draw_mode = read_auxiliary_record_u32(
        record, kAuxOverlayDrawModeOffset, 0);
    return DrawResourceSpriteMode(entry_index, x, y, draw_mode);
}

bool has_unit_animation_image_group(u32 type_id, u32 group) {
    return GetUnitDefinitionImageResourceEntry(type_id, group) != kInvalidResourceEntry;
}

u32 unit_animation_default_image_group(u32 type_id) {
    return has_unit_animation_image_group(type_id, 0) ? 0 : 1;
}

u32 unit_animation_image_group(const UnitAnimationDrawCommand& command) {
    const UnitAnimationUnit* unit = command.unit;
    const u32 type_id = unit != nullptr ? unit->type_id : 0;
    const u32 command_flags = unit != nullptr ? unit->command_flags : 0;

    switch (command.sequence) {
    case UnitAnimationSequence::moving:
        return (command_flags & kUnitAnimCommandMoving) != 0 ? 7 : 12;
    case UnitAnimationSequence::action:
        if ((command_flags & kUnitAnimCommandMoving) != 0) {
            return (command_flags & kUnitAnimCommandAlternate) != 0 ? 6 : 5;
        }
        return (command_flags & kUnitAnimCommandAlternate) != 0 ? 11 : 10;
    case UnitAnimationSequence::action_fallback:
    case UnitAnimationSequence::action_fallback_extended:
        return 1;
    case UnitAnimationSequence::alternate_action:
        return 9;
    case UnitAnimationSequence::conditional_alternate_action:
        return (command_flags & kUnitAnimCommandConditionalAlternateMask) != 0
            ? 4 : 9;
    case UnitAnimationSequence::direct_action:
    case UnitAnimationSequence::queued_command:
        return 2;
    case UnitAnimationSequence::moving_action_primary:
        return 5;
    case UnitAnimationSequence::moving_action_alternate:
        return 4;
    case UnitAnimationSequence::direct_sprite:
        return 3;
    case UnitAnimationSequence::direct_timed:
        return unit_animation_default_image_group(type_id);
    case UnitAnimationSequence::cell_base:
        return 1;
    case UnitAnimationSequence::cell_flag4:
        return 2;
    case UnitAnimationSequence::cell_flag40:
        return 11;
    case UnitAnimationSequence::cell_construction:
    case UnitAnimationSequence::cell_progress:
    case UnitAnimationSequence::cell_channel_additive:
        return 0;
    case UnitAnimationSequence::low_health_overlay:
    case UnitAnimationSequence::shadow_attachment:
        return 11;
    case UnitAnimationSequence::forced_highlight:
        return 2;
    case UnitAnimationSequence::alternate_default:
        if ((command_flags & kUnitAnimCommandMoving) != 0) {
            return 8;
        }
        return 13;
    case UnitAnimationSequence::default_idle:
    default:
        return unit_animation_default_image_group(type_id);
    }
}

bool unit_animation_allows_default_group_fallback(UnitAnimationSequence sequence) {
    return sequence == UnitAnimationSequence::default_idle ||
        sequence == UnitAnimationSequence::direct_timed;
}

u32 resolve_unit_animation_sprite_entry(const UnitAnimationDrawCommand& command) {
    if (command.unit == nullptr) {
        return kInvalidResourceEntry;
    }

    const u32 type_id = command.unit->type_id;
    const u32 group = unit_animation_image_group(command);
    const u32 fallback_frame_index = command.resource_frame & 0xffu;
    const u32 frame_index =
        original_unit_animation_frame_index(command, group, fallback_frame_index);
    u32 entry = GetUnitDefinitionImageFrameResourceEntry(type_id, group, frame_index);
    if (entry != kInvalidResourceEntry) {
        return entry;
    }
    entry = GetUnitDefinitionImageResourceEntry(type_id, group);
    if (entry != kInvalidResourceEntry) {
        return entry;
    }
    if (group != 0 &&
        unit_animation_allows_default_group_fallback(command.sequence)) {
        entry = GetUnitDefinitionImageFrameResourceEntry(type_id, 0, frame_index);
        if (entry != kInvalidResourceEntry) {
            return entry;
        }
        return GetUnitDefinitionImageResourceEntry(type_id, 0);
    }
    return kInvalidResourceEntry;
}

bool draw_jw211_direct_sprite_mode(const UnitAnimationDrawCommand& command);

bool draw_unit_shadow_attachment_sprites(const UnitAnimationDrawCommand& command) {
    if (command.unit == nullptr) {
        return false;
    }
    if (!jw207_resource_pack_state().loaded && !LoadJw207GameplayResourcePacks()) {
        return false;
    }
    const Jw207ResourcePackState& jw207 = jw207_resource_pack_state();
    const UnitDefinitionResourceRecord* record =
        loaded_unit_definition_record(command.unit->type_id);
    if (record == nullptr) {
        return false;
    }

    bool drew = false;
    const u32 debris_selector = read_unit_definition_u32(
        *record, kUnitDefinitionShadowDebrisSelectorOffset, 0);
    if ((command.unit->command_state & kUnitAnimCommandStateMirror) != 0) {
        if (debris_selector != 0 && debris_selector != 0xffffffffu &&
            jw207.debris_start != kInvalidResourceEntry) {
            const u32 progress = command.unit->command_entry_lockout_ticks != 0
                ? std::min<u32>((3u * command.unit->animation_timer) /
                    command.unit->command_entry_lockout_ticks, 2)
                : 0;
            const u32 entry = jw207.debris_start + (debris_selector - 1) * 3 + progress;
            drew = DrawResourceSpriteToken1Shadow(
                entry, command.screen_x, command.screen_y) || drew;
        }
        return drew;
    }

    if (debris_selector != 0 && jw207.debris_start != kInvalidResourceEntry) {
        const u32 entry = jw207.debris_start + (debris_selector - 1) * 3;
        drew = DrawResourceSpriteToken1Shadow(
            entry, command.screen_x, command.screen_y) || drew;
    }

    if (jw207.destruction_start == kInvalidResourceEntry) {
        return drew;
    }
    const u32 attachment_count = std::min<u32>(
        read_unit_definition_u32(*record, kUnitDefinitionShadowAttachmentCountOffset, 0),
        64);
    const u32 draw_mode = read_unit_definition_u32(
        *record, kUnitDefinitionShadowDrawModeOffset, 0);
    for (u32 i = 0; i < attachment_count; ++i) {
        const u32 frame_group = read_unit_definition_u32(*record,
            kUnitDefinitionShadowAttachmentFrameTableBase +
                static_cast<std::size_t>(i) * sizeof(u32), 0);
        const i32 x_offset = read_unit_definition_record_i32(*record,
            kUnitDefinitionShadowAttachmentXOffsetTableBase +
                static_cast<std::size_t>(i) * sizeof(u32), 0);
        const i32 y_offset = read_unit_definition_record_i32(*record,
            kUnitDefinitionShadowAttachmentYOffsetTableBase +
                static_cast<std::size_t>(i) * sizeof(u32), 0);
        const u32 entry = jw207.destruction_start + frame_group * 0x13 +
            command.unit->animation_frame;
        drew = DrawResourceSpriteMode(entry,
            command.screen_x + x_offset,
            command.screen_y + y_offset,
            draw_mode) || drew;
    }
    return drew;
}

bool draw_unit_low_health_overlay_sprites(const UnitAnimationDrawCommand& command) {
    if (command.unit == nullptr) {
        return false;
    }
    if (!jw207_resource_pack_state().loaded && !LoadJw207GameplayResourcePacks()) {
        return false;
    }
    const Jw207ResourcePackState& jw207 = jw207_resource_pack_state();
    if (jw207.unit_start == kInvalidResourceEntry) {
        return false;
    }
    const UnitDefinitionResourceRecord* record =
        loaded_unit_definition_record(command.unit->type_id);
    if (record == nullptr) {
        return false;
    }

    const u32 overlay_count = std::min<u32>(
        read_unit_definition_u32(*record, kUnitDefinitionLowHealthOverlayCountOffset, 0),
        64);
    if (overlay_count == 0) {
        return false;
    }

    bool drew = false;
    const u32 draw_mode = read_unit_definition_u32(
        *record, kUnitDefinitionLowHealthOverlayDrawModeOffset, 0);
    for (u32 i = 0; i < overlay_count; ++i) {
        const u32 frame_group = read_unit_definition_u32(*record,
            kUnitDefinitionLowHealthOverlayFrameTableBase +
                static_cast<std::size_t>(i) * sizeof(u32), 0);
        const i32 x_offset = read_unit_definition_record_i32(*record,
            kUnitDefinitionLowHealthOverlayXOffsetTableBase +
                static_cast<std::size_t>(i) * sizeof(u32), 0);
        const i32 y_offset = read_unit_definition_record_i32(*record,
            kUnitDefinitionLowHealthOverlayYOffsetTableBase +
                static_cast<std::size_t>(i) * sizeof(u32), 0);
        const u32 entry = jw207.unit_start + frame_group * 0x3f +
            command.animation_frame;
        drew = DrawResourceSpriteMode(entry,
            command.screen_x + x_offset,
            command.screen_y + y_offset,
            draw_mode) || drew;
    }
    return drew;
}

void draw_unit_animation_sprite(UnitAnimationDrawContext& context,
    const UnitAnimationDrawCommand& command) {
    if (command.sequence == UnitAnimationSequence::direct_sprite &&
        draw_jw211_direct_sprite_mode(command)) {
        return;
    }
    if (command.sequence == UnitAnimationSequence::low_health_overlay) {
        // FUN_004c58b1 returns directly when a definition has zero damage
        // overlays.  That empty result is authoritative; falling through
        // would incorrectly draw the definition's unrelated group-11 frame.
        (void)draw_unit_low_health_overlay_sprites(command);
        return;
    }
    if (command.sequence == UnitAnimationSequence::shadow_attachment) {
        // FUN_004c523d commits to the structure debris/attachment branch as
        // soon as raw state bit 4 selects it.  A definition with no debris or
        // attachments is still a handled, intentionally empty frame and must
        // not fall through to the generic group-11 sprite path.
        (void)draw_unit_shadow_attachment_sprites(command);
        return;
    }

    const u32 entry = resolve_unit_animation_sprite_entry(command);
    if (entry == kInvalidResourceEntry) {
        return;
    }

    const bool flipped = command.flipped ||
        command.kind == UnitAnimationDrawKind::flipped;

    switch (command.kind) {
    case UnitAnimationDrawKind::flipped:
    case UnitAnimationDrawKind::normal:
        if (flipped) {
            DrawResourceSpriteFlippedUnitRampToken1Shadow(
                entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteUnitRampToken1Shadow(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::highlight:
        if (flipped) {
            DrawResourceSpriteFlippedHighGreenMask(
                entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteHighGreenMask(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::blend_20:
        if (flipped) {
            DrawResourceSpriteFlippedGrayscale(entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteGrayscale(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::blend_40:
        if (flipped) {
            DrawResourceSpriteFlippedHighGreenMask(
                entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteHighGreenMask(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::mode_2:
        if (flipped) {
            DrawResourceSpriteFlippedHighBlueMask(
                entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteHighBlueMask(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::mode_80:
        if (flipped) {
            DrawResourceSpriteFlippedHighRedMask(
                entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteHighRedMask(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::blend_factor_0f:
        if (flipped) {
            DrawResourceSpriteFlippedBlendFactor(
                entry, command.screen_x, command.screen_y, 0x0f);
            return;
        }
        DrawResourceSpriteBlendFactor(entry, command.screen_x, command.screen_y, 0x0f);
        return;
    case UnitAnimationDrawKind::blend_factor_ramp:
    {
        const u32 blend_factor = context.highlight_level;
        if (flipped) {
            DrawResourceSpriteFlippedBlendFactor(
                entry, command.screen_x, command.screen_y, blend_factor);
            return;
        }
        DrawResourceSpriteBlendFactor(
            entry, command.screen_x, command.screen_y, blend_factor);
        return;
    }
    case UnitAnimationDrawKind::neighbor_copy:
        if (flipped) {
            DrawResourceSpriteFlippedNeighborCopy(
                entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteNeighborCopy(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::resource_mode:
        // Cell base/group-2/group-11 sprites are never direction-flipped in
        // FUN_004c538a/FUN_004c5468/FUN_004c568e.
        DrawResourceSpriteMode(
            entry, command.screen_x, command.screen_y, command.resource_draw_mode);
        return;
    case UnitAnimationDrawKind::ally_or_local:
        if (flipped) {
            DrawResourceSpriteFlippedUnitRampLowBlueMask(
                entry, command.screen_x, command.screen_y);
            return;
        }
        DrawResourceSpriteUnitRampLowBlueMask(entry, command.screen_x, command.screen_y);
        return;
    case UnitAnimationDrawKind::channel_additive_tint:
    {
        const u32 ramp = std::min<u32>(context.highlight_level, 31);
        const u16 red_delta = static_cast<u16>(context.color_ramps.x_offsets[ramp]);
        const u16 green_delta = static_cast<u16>(context.color_ramps.y_offsets[ramp]);
        const u16 blue_delta =
            static_cast<u16>(context.color_ramps.secondary_offsets[ramp]);
        if (flipped) {
            DrawResourceSpriteFlippedChannelAdditiveTint(
                entry, command.screen_x, command.screen_y,
                red_delta, green_delta, blue_delta);
            return;
        }
        DrawResourceSpriteChannelAdditiveTint(entry, command.screen_x, command.screen_y,
            red_delta, green_delta, blue_delta);
        return;
    }
    case UnitAnimationDrawKind::palette_channel_additive_tint:
    {
        const u32 ramp = std::min<u32>(context.highlight_level, 31);
        const u16 red_delta = static_cast<u16>(context.color_ramps.x_offsets[ramp]);
        const u16 blue_delta =
            static_cast<u16>(context.color_ramps.secondary_offsets[ramp]);
        if (flipped) {
            DrawResourceSpriteFlippedChannelAdditiveTint(
                entry, command.screen_x, command.screen_y, red_delta, 0, blue_delta);
            return;
        }
        DrawResourceSpriteChannelAdditiveTint(
            entry, command.screen_x, command.screen_y, red_delta, 0, blue_delta);
        return;
    }
    case UnitAnimationDrawKind::timed_channel_additive_tint:
    {
        const u32 ramp = std::min<u32>(context.highlight_level, 31);
        const u16 green_delta =
            static_cast<u16>(context.color_ramps.y_offsets[ramp]);
        const u16 blue_delta =
            static_cast<u16>(context.color_ramps.secondary_offsets[ramp]);
        if (flipped) {
            DrawResourceSpriteFlippedChannelAdditiveTint(
                entry, command.screen_x, command.screen_y, 0, green_delta, blue_delta);
            return;
        }
        DrawResourceSpriteChannelAdditiveTint(
            entry, command.screen_x, command.screen_y, 0, green_delta, blue_delta);
        return;
    }
    case UnitAnimationDrawKind::shadow_probe_additive_tint:
    {
        constexpr u32 kProbeRampIndex = 26;
        const u16 green_delta =
            static_cast<u16>(context.color_ramps.y_offsets[kProbeRampIndex]);
        const u16 blue_delta =
            static_cast<u16>(context.color_ramps.secondary_offsets[kProbeRampIndex]);
        if (flipped) {
            DrawResourceSpriteFlippedChannelAdditiveTint(
                entry, command.screen_x, command.screen_y, 0, green_delta, blue_delta);
            return;
        }
        DrawResourceSpriteChannelAdditiveTint(
            entry, command.screen_x, command.screen_y, 0, green_delta, blue_delta);
        return;
    }
    default:
        break;
    }

    if (flipped) {
        DrawResourceSpriteFlippedUnitRampToken1Shadow(
            entry, command.screen_x, command.screen_y);
        return;
    }
    DrawResourceSpriteUnitRampToken1Shadow(entry, command.screen_x, command.screen_y);
}

bool resolve_jw211_unit_overlay_entry(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 catalog_index, u32& entry, u32& draw_mode) {
    if (!jw211_runtime_catalog_state().loaded && !LoadJw211RuntimeCatalog()) {
        return false;
    }
    const AuxiliaryRuntimeCatalogState& catalog = jw211_runtime_catalog_state();
    if (catalog_index >= catalog.records.size()) {
        return false;
    }
    const AuxiliaryRuntimeCatalogRecord& record = catalog.records[catalog_index];
    if (!record.loaded || record.definition_bytes.empty()) {
        return false;
    }

    const u32 cycle_count = read_auxiliary_record_u32(
        record, kAuxOverlayFrameCycleCountOffset, 0);
    if (cycle_count == 0) {
        return false;
    }
    const u32 phase = (unit.runtime_slot_index + context.global_frame_counter) %
        cycle_count;
    const u32 frame_offset = read_auxiliary_record_u32(record,
        kAuxOverlayFrameTableBase + static_cast<std::size_t>(phase) * sizeof(u32),
        0xffffffffu);
    if (frame_offset == 0xffffffffu) {
        return false;
    }

    const u32 class_stride_factor = read_auxiliary_record_u32(
        record, kAuxOverlayClassStrideFactorOffset, 0);
    const u32 class_frame_count = read_auxiliary_record_u32(
        record, kAuxOverlayClassFrameCountOffset, 0);
    const u32 overlay_class = static_cast<u32>(std::max<i32>(
        read_unit_definition_i32(unit.type_id, kUnitDefinitionOverlayClassOffset, 0),
        0));
    const u32 frame_index =
        frame_offset + overlay_class * class_stride_factor * class_frame_count;
    entry = auxiliary_image_entry(record, frame_index);
    draw_mode = read_auxiliary_record_u32(record, kAuxOverlayDrawModeOffset, 0);
    return entry != kInvalidResourceEntry;
}

bool draw_jw211_direct_sprite_mode(const UnitAnimationDrawCommand& command) {
    if (command.unit == nullptr ||
        (command.unit->state_flags & kUnitAnimStateDirectSpriteMode) == 0) {
        return false;
    }
    if (!jw211_runtime_catalog_state().loaded && !LoadJw211RuntimeCatalog()) {
        return false;
    }
    const AuxiliaryRuntimeCatalogState& catalog = jw211_runtime_catalog_state();
    if (kJw211DirectSpriteRecord >= catalog.records.size()) {
        return false;
    }
    const AuxiliaryRuntimeCatalogRecord& record =
        catalog.records[kJw211DirectSpriteRecord];
    if (!record.loaded || record.definition_bytes.empty()) {
        return false;
    }

    const u32 frame_index = read_auxiliary_record_u32(record,
        kAuxOverlayFrameTableBase +
            static_cast<std::size_t>(command.animation_frame) * sizeof(u32),
        0xffffffffu);
    if (frame_index == 0xffffffffu) {
        return false;
    }
    const u32 entry = auxiliary_image_entry(record, frame_index);
    if (entry == kInvalidResourceEntry) {
        return false;
    }
    const u32 draw_mode = read_auxiliary_record_u32(record, kAuxOverlayDrawModeOffset, 0);
    return DrawResourceSpriteMode(entry, command.screen_x, command.screen_y, draw_mode);
}

void draw_unit_special_overlay(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    u32 entry = kInvalidResourceEntry;
    u32 draw_mode = 0;
    if (!resolve_jw211_unit_overlay_entry(
            context, unit, kJw211SpecialOverlayRecord, entry, draw_mode)) {
        return;
    }
    DrawResourceSpriteMode(entry, unit.screen_x, unit.screen_y, draw_mode);
}

void draw_unit_status_overlay(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    u32 entry = kInvalidResourceEntry;
    u32 ignored_draw_mode = 0;
    if (!resolve_jw211_unit_overlay_entry(
            context, unit, kJw211StatusOverlayRecord, entry, ignored_draw_mode)) {
        return;
    }
    DrawResourceSpriteTableBlend(entry, unit.screen_x, unit.screen_y);
}

u32 unit_bar_fill_width(i32 width, u32 value, u32 maximum) {
    if (maximum == 0 || width <= 3) {
        return 0;
    }
    // DrawUnitHealthAndSecondaryBars multiplies the raw current value at
    // 0x004c5cb1/0x004c5da5 and divides by its maximum without first
    // clamping it.  Preserve that behavior for temporary over-max HP/cargo
    // values instead of shortening their bar to the shell width.
    const u64 scaled = static_cast<u64>(static_cast<u32>(width - 3)) *
        value;
    return static_cast<u32>(scaled / maximum);
}

u16 unit_health_bar_color(const UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (unit.max_hit_points == 0) {
        return context.color_ramps.colors[0];
    }
    const u32 quarter = unit.max_hit_points >> 2;
    if (unit.hit_points < quarter) {
        return context.color_ramps.colors[1];
    }
    if (unit.hit_points < (unit.max_hit_points >> 1)) {
        return context.color_ramps.colors[2];
    }
    if (unit.hit_points < quarter + (unit.max_hit_points >> 1)) {
        return context.color_ramps.colors[3];
    }
    return context.color_ramps.colors[4];
}

void draw_unit_bar_shell(const UnitAnimationDrawContext& context,
    i32 x, i32 y, i32 width) {
    if (width <= 0) {
        return;
    }
    DrawBackBufferFilledRectangle16(x, y, x + width - 1, y + 4, 0);
    DrawBackBufferStippledRectangle16(x + 1, y + 1, x + width - 2, y + 3,
        context.color_ramps.colors[0]);
}

void draw_unit_bar_value(i32 x, i32 y, u32 fill_width, u16 color) {
    // DrawUnitHealthAndSecondaryBars increments the top row, then passes
    // base-y + 3 as the inclusive bottom of both value rectangles
    // (0x004c5cca..0x004c5d10 and 0x004c5ddd..0x004c5de6).
    DrawBackBufferStippledRectangle16(x + 1, y + 1,
        x + 1 + static_cast<i32>(fill_width), y + 3, color);
}

void draw_unit_health_bar(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, i32 width) {
    draw_unit_bar_shell(context, x, y, width);
    if (unit.max_hit_points == 0) {
        return;
    }
    draw_unit_bar_value(x, y,
        unit_bar_fill_width(width, unit.hit_points, unit.max_hit_points),
        unit_health_bar_color(context, unit));
}

void draw_unit_secondary_bar(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, i32 width) {
    draw_unit_bar_shell(context, x, y, width);
    draw_unit_bar_value(x, y,
        unit_bar_fill_width(width, unit.secondary_value, unit.max_secondary_value),
        context.color_ramps.colors[5]);
}

void draw_unit_bar_pixel(UnitAnimationDrawContext&, const UnitAnimationUnit&,
    i32 x, i32 y, u16 color) {
    DrawBackBufferFilledRectangle16(x, y, x, y, color);
}

void draw_unit_marker(UnitAnimationDrawContext& context,
    const UnitAnimationUnit&, i32 x, i32 y) {
    if (context.current_marker_sprite_entry == kUnitAnimationInvalidMarkerEntry) {
        return;
    }
    DrawResourceSpriteDirectToken1Shadow(context.current_marker_sprite_entry, x, y);
}

void draw_unit_display_name(UnitAnimationDrawContext&,
    const UnitAnimationUnit& unit, i32 center_x, i32 baseline_y) {
    if (unit.display_name.empty()) {
        return;
    }
    // Original unit-name tail selects font 4 for both drawing and metrics at
    // 0x004c50fc/0x004c5103 before centering the dynamic name.
    SelectTextDrawFont(4);
    SelectTextMetricFont(4);
    SetTextCursor(center_x, baseline_y, 0xff);
    DrawCenterAlignedText(unit.display_name.c_str());
}

void apply_unit_owner_tint(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitOwnerRelationTint tint) {
    if (context.definition == nullptr) {
        return;
    }
    const u32 resource_base = gameplay_ui_resource_state().green_numbers_start;
    if (resource_base == kInvalidResourceEntry) {
        return;
    }
    // ApplyUnitOwnerRelationTint (0x004c5b90) uses the unit-definition record
    // fields at +0x380/+0x384/+0x388 to draw the dedicated relation marker.
    // It does not redraw the unit's most recent body-sprite command.
    const UnitAnimationDefinition& definition = *context.definition;
    const u32 entry = resource_base + definition.owner_relation_overlay_entry_offset;
    DrawResourceSpritePaletteIndexOffset(entry,
        unit.screen_x + definition.owner_relation_overlay_offset_x,
        unit.screen_y + definition.owner_relation_overlay_offset_y,
        static_cast<u8>(tint));
}

bool unit_owner_allied(UnitAnimationDrawContext& context,
    u32 owner_id, u32 other_owner_id) {
    if (owner_id >= context.owner_relation_masks.size() || other_owner_id >= 32) {
        return false;
    }
    return (context.owner_relation_masks[owner_id] & (1u << other_owner_id)) != 0;
}

UnitAnimationUnit make_unit_animation_unit(const UnitRenderItem& item,
    i32 screen_x, i32 screen_y) {
    UnitAnimationUnit unit{};
    unit.type_id = item.type_id;
    unit.owner_id = item.owner_id;
    unit.runtime_slot_index = item.runtime_slot_index;
    unit.command_flags = item.command_flags;
    unit.command_bit_mask = item.command_bit_mask;
    unit.command_value = item.command_value;
    unit.command_state = item.command_state;
    unit.previous_command_state = item.previous_command_state;
    unit.animation_flags = item.animation_flags;
    unit.marker_flags = item.marker_flags;
    unit.state_flags = item.state_flags;
    unit.draw_flags = item.draw_flags;
    unit.runtime_flags = item.runtime_flags;
    unit.animation_frame = item.animation_frame;
    unit.animation_timer = item.animation_timer;
    unit.command_entry_lockout_ticks = item.command_entry_lockout_ticks;
    unit.command_lockout_ticks = item.command_lockout_ticks;
    unit.direction = item.direction;
    unit.screen_x = screen_x;
    unit.screen_y = screen_y;
    unit.cargo_amount = item.cargo_amount;
    unit.max_hit_points = item.max_hit_points;
    unit.hit_points = item.hit_points;
    unit.max_secondary_value = item.max_secondary_value;
    unit.secondary_value = item.secondary_value;
    unit.secondary_bar_enabled = item.secondary_bar_enabled;
    unit.terrain_cell_flags = item.terrain_cell_flags;
    unit.command_metadata_flags = item.command_metadata_flags;
    unit.definition_cell_flags = item.definition_cell_flags;
    unit.cell_animation_frame = item.cell_animation_frame;
    unit.cell_flag40_animation_frame = item.cell_flag40_animation_frame;
    unit.cell_channel_additive_frame = item.cell_channel_additive_frame;
    unit.construction_stage_count = item.construction_stage_count;
    unit.construction_progress = item.construction_progress;
    unit.construction_progress_limit = item.construction_progress_limit;
    unit.low_health_overlay_frame = item.low_health_overlay_frame;
    unit.ability_id = item.ability_id;
    unit.cell_construction_progress_active = item.cell_construction_progress_active;
    unit.cell_channel_additive_active = item.cell_channel_additive_active;
    unit.display_name = item.display_name;
    return unit;
}

UnitAnimationDefinition make_unit_animation_definition(const UnitRenderItem& item) {
    UnitAnimationDefinition definition{};
    definition.type_id = item.type_id;
    definition.direction_offset = read_unit_definition_i32(
        item.type_id, kUnitDefinitionAnimationDirectionOffset, 0);
    definition.name_offset_x = item.center_offset_x;
    definition.name_offset_y = item.center_offset_y;
    definition.name_width = item.center_width;
    definition.marker_offset_x = item.center_offset_x;
    definition.marker_offset_y = item.center_offset_y;
    definition.marker_width = item.center_width;
    definition.marker_height = item.center_height;
    // DrawUnitHealthAndSecondaryBars reads the dedicated definition fields at
    // +0x350/+0x354/+0x358.  These are independent of the +0x360..+0x36c
    // name/selection bounds and differ for several mobile definitions.
    definition.bars_offset_x = read_unit_definition_i32(
        item.type_id, kUnitDefinitionHealthBarXOffset, item.center_offset_x);
    definition.bars_offset_y = read_unit_definition_i32(
        item.type_id, kUnitDefinitionHealthBarYOffset,
        item.center_offset_y + item.center_height);
    definition.bars_width = read_unit_definition_i32(
        item.type_id, kUnitDefinitionHealthBarWidthOffset, item.center_width);
    definition.owner_relation_overlay_entry_offset = static_cast<u32>(
        read_unit_definition_i32(item.type_id,
            kUnitDefinitionOwnerRelationOverlayEntryOffset, 0));
    definition.owner_relation_overlay_offset_x = read_unit_definition_i32(
        item.type_id, kUnitDefinitionOwnerRelationOverlayXOffset, 0);
    definition.owner_relation_overlay_offset_y = read_unit_definition_i32(
        item.type_id, kUnitDefinitionOwnerRelationOverlayYOffset, 0);
    definition.cell_base_blit_mode = read_unit_definition_u8(
        item.type_id, kUnitDefinitionCellBaseBlitModeOffset, 0);
    definition.cell_flag4_blit_mode = read_unit_definition_u8(
        item.type_id, kUnitDefinitionCellFlag4BlitModeOffset, 0);
    definition.cell_flag40_blit_mode = read_unit_definition_u8(
        item.type_id, kUnitDefinitionCellFlag40BlitModeOffset, 0);
    definition.cell_construction_special_draw =
        read_unit_definition_i32(
            item.type_id, kUnitDefinitionSpecialDrawFlagOffset, 0) == 1;
    definition.has_move_resource =
        read_unit_definition_i32(item.type_id,
            kUnitDefinitionMovingFrameCountOffset, 0) != 0 &&
        has_unit_animation_image_group(item.type_id, 12);
    definition.has_move_resource_alt =
        read_unit_definition_i32(item.type_id,
            kUnitDefinitionMovingAltFrameCountOffset, 0) != 0 &&
        has_unit_animation_image_group(item.type_id, 7);
    definition.has_alternate_default_resource =
        read_unit_definition_i32(item.type_id,
            kUnitDefinitionAlternateIdleFrameCountOffset, 0) != 0 &&
        has_unit_animation_image_group(item.type_id, 13);
    definition.has_alternate_default_resource_alt =
        read_unit_definition_i32(item.type_id,
            kUnitDefinitionAlternateMovingFrameCountOffset, 0) != 0 &&
        has_unit_animation_image_group(item.type_id, 8);
    definition.has_queued_command_resource =
        read_unit_definition_i32(item.type_id,
            kUnitDefinitionQueuedCommandFrameCountOffset, 0) != 0 &&
        has_unit_animation_image_group(item.type_id, 2);
    definition.action_fallback_uses_extended_directions =
        read_unit_definition_i32(item.type_id,
            kUnitDefinitionActionFallbackExtendedFlagOffset, 0) == 1;
    return definition;
}

UnitAnimationDrawContext make_unit_animation_context(
    const UnitAnimationDefinition& definition, u32 global_frame_counter,
    u32 local_owner_id, bool local_owner_is_observer,
    const std::array<u32, 8>& owner_relation_masks) {
    UnitAnimationDrawContext context{};
    context.definition = &definition;
    context.global_frame_counter = global_frame_counter;
    context.local_owner_id = local_owner_id;
    context.local_owner_is_observer = local_owner_is_observer;
    context.owner_relation_masks = owner_relation_masks;
    context.callbacks.draw_sprite = draw_unit_animation_sprite;
    context.callbacks.draw_direct_sprite = draw_unit_animation_sprite;
    context.callbacks.draw_highlight_sprite = draw_unit_animation_sprite;
    context.callbacks.draw_special_overlay = draw_unit_special_overlay;
    context.callbacks.draw_status_overlay = draw_unit_status_overlay;
    context.callbacks.draw_health_bar = draw_unit_health_bar;
    context.callbacks.draw_secondary_bar = draw_unit_secondary_bar;
    context.callbacks.draw_bar_pixel = draw_unit_bar_pixel;
    context.callbacks.draw_marker = draw_unit_marker;
    context.callbacks.draw_display_name = draw_unit_display_name;
    context.callbacks.apply_owner_tint = apply_unit_owner_tint;
    context.callbacks.is_owner_allied = unit_owner_allied;
    context.special_overlay_resources_loaded = true;
    context.status_overlay_resources_loaded = true;
    context.use_555_color = SurfacePixelMode555();
    // ConfigureDirectDrawSurfaces initializes these packed-channel steps at
    // 0x004f468c/0x004f46a0/0x004f46b4 (565) and
    // 0x004f4723/0x004f4737/0x004f474b (555).  The ramp builder at
    // 0x004c5ef0 consumes them rather than deriving them itself.
    context.ramp_x_step = context.use_555_color ? 0x400 : 0x800;
    context.ramp_y_step = 0x20;
    context.ramp_secondary_step = 1;
    // 0x004c5139 subtracts half of the selected font-4 height from the
    // definition's +0x364 name baseline.
    context.text_half_height =
        static_cast<i32>(text_renderer_state().fonts[4].height);
    // DrawUnitSelectionOrTargetMarker reads DAT_008685f8, initialized by
    // LoadGameplayUiResourcePacks (0x004e8714) to the JW2_02 small-character
    // resource sequence base + 6.  Interface-theme entry + 6 is msg_p.spz and
    // causes unrelated HUD art to be used as world markers.
    context.selection_marker_base_entry =
        gameplay_ui_resource_state().small_character_aliases[6];
    InitializeOriginalUnitAnimationFrameTables(context.tables);
    InitializeUnitRenderColorRamps(context);
    return context;
}

} // namespace

u32 UpdateGameplayFrameAnimationSlot(GameplayFrameRenderContext& context) {
    const u32 table_index =
        (context.frame_counter >> 2) % kGameplayFrameAnimationSlotCount;
    context.animation_frame_slot = context.animation_frame_table[table_index];
    context.animation_cycle = (context.frame_counter >> 2) /
        static_cast<u32>(kGameplayFrameAnimationSlotCount);
    return context.animation_cycle;
}

void RenderGameplayFrameComposite(GameplayFrameRenderContext& context) {
    UpdateGameplayFrameAnimationSlot(context);
    context.expanded_left = context.camera_x - 0x80;
    context.expanded_top = context.camera_y - 0x80;
    context.expanded_right =
        context.camera_x + static_cast<i32>(context.viewport_width) + 0x80;
    context.expanded_bottom =
        context.camera_y + static_cast<i32>(context.viewport_height) + 0x80;

    if (context.render_command_queue != nullptr) {
        ResetGameplayRenderCommandQueue(*context.render_command_queue);
    }
    if (context.callbacks.draw_terrain != nullptr) {
        context.callbacks.draw_terrain(context, context.camera_x, context.camera_y);
    }
    if (context.unit_render_queue != nullptr) {
        context.unit_render_queue->viewport.left = context.expanded_left;
        context.unit_render_queue->viewport.top = context.expanded_top;
        context.unit_render_queue->viewport.right = context.expanded_right;
        context.unit_render_queue->viewport.bottom = context.expanded_bottom;
        ProcessVisibleUnitRenderQueue(*context.unit_render_queue);
        ProcessVisibleEffectRenderQueue(*context.unit_render_queue);
    }
    frame_callback(context, context.callbacks.prepare_visible_runtime_resources);
    frame_callback(context, context.callbacks.mirror_visible_map_effects);
    frame_callback(context, context.callbacks.draw_terrain_decorations);
    if (context.render_command_queue != nullptr) {
        ProcessGameplayRenderCommandQueue(*context.render_command_queue);
    }
    // FUN_004d7790 proceeds directly from FUN_004d8050 (sorted render-queue
    // dispatch) to FUN_00420580 (fog).  The standalone viewport-brush builder
    // was reconstruction-only work performed after its output could be queued,
    // and its commands were never consumed by this or the following frame.
    if (context.fog != nullptr) {
        context.fog->camera_x = context.camera_x;
        context.fog->camera_y = context.camera_y;
        RenderGameplayFogOverlay(*context.fog);
    }
    frame_callback(context, context.callbacks.draw_first_overlay);
    frame_callback(context, context.callbacks.draw_second_overlay);
    frame_callback(context, context.callbacks.draw_third_overlay);
    if (context.hud != nullptr) {
        context.hud->frame_counter = context.frame_counter;
        context.hud->current_tick_ms = context.current_tick_ms;
        RenderGameplayHudText(*context.hud);
    }
    frame_callback(context, context.callbacks.draw_system_hud);
    frame_callback(context, context.callbacks.draw_resource_hud);
    frame_callback(context, context.callbacks.publish_present_flag);
    frame_callback(context, context.callbacks.draw_ui_overlay);
    if (context.hud != nullptr) {
        RenderGameplayDebugFpsCounter(*context.hud);
    }
    if (context.pause_overlay_active) {
        frame_callback(context, context.callbacks.show_pause_overlay);
    }
    frame_callback(context, context.callbacks.present_cursor);
}

void ResetGameplayHudTextLayout(GameplayHudTextState& state) {
    state.bottom_text_y = static_cast<i32>(state.screen_height) - 0x14;
    state.selected_status.active = false;
    state.selected_status.typed_text.clear();
    state.selected_status.extra_text.clear();
    state.selected_status.extra_text_active = false;
    state.current_message.y = state.bottom_text_y;
    state.queued_message.y = state.bottom_text_y;

    if (current_message_text(state.current_message) != nullptr) {
        select_draw_font(state);
        recompute_message_center(state, state.current_message);
    }
    if (current_message_text(state.queued_message) != nullptr) {
        select_draw_font(state);
        recompute_message_center(state, state.queued_message);
    }
}

void RenderGameplayHudAsciiTextLine(const char* text, i32 x, i32 y, u8 color) {
    if (text == nullptr) {
        return;
    }
    SetTextCursor(x, y, color);
    RenderAsciiOnlyTextLine(text);
}

std::string FormatTeamReserveRotationCountdownText(u32 countdown_ticks) {
    const u32 seconds = countdown_ticks % 60;
    const u32 minutes = (countdown_ticks / 60) % 60;
    const u32 hours = countdown_ticks / 3600;

    std::ostringstream text;
    if (hours == 0) {
        text << minutes << " : " << seconds;
    } else {
        text << hours << " : " << minutes << " : " << seconds;
    }
    return text.str();
}

void DrawTeamReserveRotationCountdownText(GameplayHudTextState& state,
    u32 countdown_ticks, i32 x, i32 y, u8 color) {
    const std::string text = FormatTeamReserveRotationCountdownText(countdown_ticks);
    draw_centered_text(state, text.c_str(), x + 1, y + 1, 0xe9);
    draw_centered_text(state, text.c_str(), x, y, color);
}

void QueueGameplayHudMessage(GameplayHudTextState& state, const char* text) {
    if (state.current_message.text == text) {
        state.current_message = {};
    }
    state.queued_message.text = text;
    select_draw_font(state);
    const GameplayTextExtent extent = measure_text(state, text);
    state.queued_message.x =
        static_cast<i32>((state.screen_width - std::min(state.screen_width, extent.width)) >> 1);
    state.queued_message.y = static_cast<i32>(state.screen_height) -
        static_cast<i32>(extent.height) * 2;
    state.queued_message.tick_ms = state.current_tick_ms;
}

bool QueueGameplayHudMessageAndSound(GameplayHudTextState& state,
    GameplaySoundState& sound, const char* text, u32 sound_slot,
    i32 world_delta, i32 pan) {
    QueueGameplayHudMessage(state, text);
    return HandleCurrentGameplaySoundQueued(sound, sound_slot, world_delta, pan);
}

void RenderGameplayHudText(GameplayHudTextState& state) {
    select_draw_font(state);
    render_selected_status(state);

    constexpr std::array<i32, kGameplayTimedHudNotificationCount> kOffsets{
        -0x96, -0x82, -0x6e, -0x5a, -0x46};
    for (std::size_t i = 0; i < state.timed_notifications.size(); ++i) {
        GameplayTimedHudNotification& notification = state.timed_notifications[i];
        if (!notification.active) {
            continue;
        }
        RenderTimedGameplayHudNotification(state, notification, 0x14,
            static_cast<i32>(state.screen_height) + kOffsets[i]);
    }

    select_draw_font(state);
    render_queued_message(state);
    render_current_message(state);
    if (state.callbacks.flush_status_tail != nullptr) {
        state.callbacks.flush_status_tail(state);
    }
    if (state.alert_markers != nullptr) {
        state.alert_markers->frame_counter = state.frame_counter;
        TickAndRenderGameplayHudAlertMarkers(*state.alert_markers);
    }
}

void RenderTimedGameplayHudNotification(
    GameplayHudTextState& state, GameplayTimedHudNotification& notification,
    i32 x, i32 y) {
    if (notification.expires_tick_ms <= state.current_tick_ms) {
        notification.active = false;
    }
    if (!notification.primary_text.empty()) {
        draw_shadow_text(state, notification.primary_text.c_str(), x, y,
            notification.primary_color);
        x += static_cast<i32>(measure_text(state, notification.primary_text.c_str()).width);
    }
    if (!notification.secondary_text.empty()) {
        draw_shadow_text(state, notification.secondary_text.c_str(), x, y,
            notification.secondary_color);
    }
}

bool GameplayPlayerResourceHudSlotVisible(
    const GameplayPlayerResourceHudState& state, u32 player) {
    if (player >= state.players.size()) {
        return false;
    }

    const u8 slot_state = state.players[player].slot_state;
    if (slot_state == 0x14 || slot_state == 2 || slot_state == 3) {
        return false;
    }
    if (slot_state == 1 &&
        (state.flags & kGameplayPlayerResourceHudIncludePlayerControlled) == 0) {
        return false;
    }
    return true;
}

void RenderGameplayPlayerResourceRows(GameplayPlayerResourceHudState& state) {
    state.draw_requests.clear();
    i32 y = state.start_y;

    for (u32 player = 0; player < state.players.size(); ++player) {
        if (!GameplayPlayerResourceHudSlotVisible(state, player)) {
            continue;
        }

        const GameplayPlayerResourceHudPlayer& row = state.players[player];
        if (state.callbacks.select_font != nullptr) {
            state.callbacks.select_font(state, player);
        }

        i32 x = state.start_x;
        if ((state.flags & kGameplayPlayerResourceHudPrimary) != 0) {
            emit_player_resource_sprite(state, player, state.primary_resource_icon, x, y);
            emit_player_resource_text(state, player, format_i32(row.primary_resource),
                x + 0x12, y + 2, state.normal_color);
            x += 0x46;
        }

        if ((state.flags & kGameplayPlayerResourceHudPopulation) != 0) {
            emit_player_resource_sprite(state, player, state.population_icon, x, y);
            const u8 display_color = row.population_current < row.population_display ?
                state.warning_color : state.normal_color;
            const i32 after_display = emit_player_resource_text(state, player,
                format_i32(row.population_display), x + 0x12, y + 2, display_color);

            i32 current = row.population_current;
            u8 current_color = state.normal_color;
            if (row.population_cap < current) {
                current = row.population_cap;
                current_color = state.capped_color;
            }
            emit_player_resource_text(state, player, "/" + format_i32(current),
                after_display, y + 2, current_color);
            x += 0x46;
        }

        if ((state.flags & kGameplayPlayerResourceHudUnitCount) != 0) {
            emit_player_resource_text(state, player,
                format_i32(row.active_unit_count + row.queued_unit_count),
                x, y, state.normal_color);
            x += 0x32;
        }

        if ((state.flags & kGameplayPlayerResourceHudScore) != 0) {
            emit_player_resource_text(
                state, player, format_i32(row.score), x, y, state.normal_color);
            x += 0x3c;
        }

        if ((state.flags & kGameplayPlayerResourceHudPlayerIcon) != 0) {
            emit_player_resource_sprite(state, player, state.player_icon_base + player, x, y);
            x += 0x12;
        }

        if ((state.flags & kGameplayPlayerResourceHudName) != 0) {
            if (state.callbacks.select_name_font != nullptr) {
                state.callbacks.select_name_font(state, player);
            }
            emit_player_resource_text(
                state, player, player_display_name(row), x, y, state.normal_color);
        }

        y += 0x12;
    }

    if ((state.flags & kGameplayPlayerResourceHudRotationCountdown) != 0) {
        emit_player_resource_text(state, 0xffffffffu,
            FormatTeamReserveRotationCountdownText(state.rotation_countdown_ticks),
            state.rotation_countdown_x, state.rotation_countdown_y,
            state.rotation_countdown_color, true);
    }
}

void DrawCenteredGameplayBottomText(GameplayHudTextState& state, const char* text) {
    select_draw_font(state);
    select_metric_font(state);
    const GameplayTextExtent extent = measure_text(state, text);
    const i32 x =
        static_cast<i32>((state.screen_width - std::min(state.screen_width, extent.width)) >> 1);
    const i32 y = static_cast<i32>(state.screen_height) - static_cast<i32>(extent.height);
    draw_text(state, text, x, y, 1);
}

void DrawBottomLeftGameplayText(GameplayHudTextState& state, const char* text) {
    if (state.bottom_left_text_suppressed) {
        return;
    }
    select_draw_font(state);
    select_metric_font(state);
    const GameplayTextExtent extent = measure_text(state, text);
    const i32 y = static_cast<i32>(state.screen_height) - static_cast<i32>(extent.height);
    draw_text(state, text, 0, y, 1);
}

void TickGameplayDebugFrameCounter(GameplayHudTextState& state) {
    GameplayDebugCounterState& debug = state.debug_counter;
    if (!debug.enabled) {
        return;
    }
    if (state.current_tick_ms - debug.tick_last_sample_ms > 999) {
        debug.tick_last_sample_ms = state.current_tick_ms;
        debug.tick_previous_count = debug.tick_current_count;
        debug.tick_current_count = 0;
    }
    ++debug.tick_current_count;
}

void RenderGameplayDebugFpsCounter(GameplayHudTextState& state) {
    GameplayDebugCounterState& debug = state.debug_counter;
    if (!debug.enabled) {
        return;
    }
    select_metric_font(state);
    select_draw_font(state);
    if (state.current_tick_ms - debug.render_last_sample_ms > 999) {
        debug.render_last_sample_ms = state.current_tick_ms;
        debug.render_previous_count = debug.render_current_count;
        debug.render_current_count = 0;
    }
    ++debug.render_current_count;

    std::ostringstream text;
    text << debug.render_previous_count << '/' << debug.tick_previous_count;
    const std::string text_value = text.str();
    draw_centered_text(state, text_value.c_str(), debug.x, debug.y, 1);
}

void ResetGameplayHudAlertMarkers(GameplayHudAlertMarkerState& state) {
    for (GameplayHudAlertMarker& marker : state.markers) {
        marker = {};
    }
    state.draw_requests.clear();
}

bool QueueGameplayHudAlertMarker(GameplayHudAlertMarkerState& state, u32 kind,
    i32 world_x, i32 world_y) {
    state.last_alert_world_x = world_x;
    state.last_alert_world_y = world_y;
    state.last_alert_valid = true;

    if (kind == 0) {
        for (const GameplayHudAlertMarker& marker : state.markers) {
            if (!marker.active || marker.kind != 0) {
                continue;
            }
            const i32 dx = std::abs(marker.world_x - world_x);
            const i32 dy = std::abs(marker.world_y - world_y);
            if (std::max(dx, dy) < static_cast<i32>(state.duplicate_radius)) {
                return false;
            }
        }
    }

    for (GameplayHudAlertMarker& marker : state.markers) {
        if (marker.active) {
            continue;
        }
        marker.active = true;
        marker.kind = kind;
        marker.world_x = world_x;
        marker.world_y = world_y;
        const u32 tile_x = static_cast<u32>(world_x >> 5);
        const u32 tile_y = static_cast<u32>(world_y >> 5);
        marker.screen_x = state.minimap_x +
            static_cast<i32>(static_cast<u64>(tile_x) * state.minimap_width /
                std::max<u32>(1, state.map_width_tiles));
        marker.screen_y = state.minimap_y +
            static_cast<i32>(static_cast<u64>(tile_y) * state.minimap_height /
                std::max<u32>(1, state.map_height_tiles));
        marker.animation_frame = 0;
        marker.remaining_ticks = state.marker_lifetime_ticks;
        marker.last_frame_counter = state.frame_counter;
        return true;
    }
    return false;
}

void TickAndRenderGameplayHudAlertMarkers(GameplayHudAlertMarkerState& state) {
    state.draw_requests.clear();
    for (GameplayHudAlertMarker& marker : state.markers) {
        if (!marker.active) {
            continue;
        }
        if (marker.last_frame_counter != state.frame_counter) {
            marker.last_frame_counter = state.frame_counter;
            if (marker.remaining_ticks != 0) {
                --marker.remaining_ticks;
            }
            if (marker.remaining_ticks == 0) {
                marker.active = false;
            } else {
                marker.animation_frame = (marker.animation_frame + 1) & 7u;
            }
        }
        const GameplayHudAlertMarkerDraw draw{
            state.sprite_base_entry + marker.animation_frame,
            marker.kind << 2,
            marker.screen_x,
            marker.screen_y,
        };
        state.draw_requests.push_back(draw);

        // FUN_0042a500 does not merely publish a draw record: after advancing
        // the 8-frame alert animation it immediately blits the JW2_07 sprite,
        // adding kind * 4 to each non-transparent palette index.  Keeping the
        // request is useful to callers/tests, but without this matching blit
        // queued construction and under-attack markers never reached the
        // gameplay back buffer.
        DrawResourceSpritePaletteIndexOffset(draw.sprite_entry, draw.x, draw.y,
            static_cast<u8>(draw.palette_selector));
    }
}

void BindGameplayRenderTarget(GameplayFrameRenderContext& context,
    const SpriteRenderTarget& target) {
    SetSpriteRenderTarget(target.pixels, target.width, target.height, target.stride_words);
    context.viewport_width = target.width;
    context.viewport_height = target.height;
    if (context.fog != nullptr) {
        context.fog->target.pixels = target.pixels;
        context.fog->target.width = target.width;
        context.fog->target.height = target.height;
        context.fog->target.stride_words = target.stride_words;
        context.fog->metrics = RecalculateGameplayFogViewportMetrics(target.width, target.height);
    }
    const bool pixel_mode_555 = SurfacePixelMode555();
    ConfigureSpritePixelMaskConstants(pixel_mode_555);
    BuildSpriteBlendTables(pixel_mode_555);
}

bool ReserveGameplayRenderQueueIndex(
    GameplayRenderCommandQueue& queue, std::size_t& entry_index) {
    if (queue.commands.size() >= kGameplayRenderCommandCapacity) {
        return false;
    }
    entry_index = queue.commands.size();
    if (queue.sorted_indices.size() <= entry_index) {
        queue.sorted_indices.push_back(entry_index);
    } else {
        queue.sorted_indices[entry_index] = entry_index;
    }
    queue.sorted = false;
    return true;
}

bool QueueGameplayRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command) {
    std::size_t entry_index = 0;
    if (!ReserveGameplayRenderQueueIndex(queue, entry_index)) {
        return false;
    }
    if (queue.commands.size() <= entry_index) {
        queue.commands.push_back(command);
    } else {
        queue.commands[entry_index] = command;
    }
    return true;
}

void ResetGameplayRenderCommandQueue(GameplayRenderCommandQueue& queue) {
    queue.commands.clear();
    queue.sorted_indices.clear();
    queue.sorted = false;
}

void SortGameplayRenderCommandQueue(GameplayRenderCommandQueue& queue) {
    EnsureGameplayRenderSortedIndices(queue);
}

void SwapGameplayRenderSortedIndices(GameplayRenderCommandQueue& queue,
    std::size_t lhs, std::size_t rhs) {
    if (lhs >= queue.sorted_indices.size() || rhs >= queue.sorted_indices.size()) {
        return;
    }
    std::swap(queue.sorted_indices[lhs], queue.sorted_indices[rhs]);
    queue.sorted = false;
}

void QuickSortGameplayRenderSortedIndicesBySortKey(GameplayRenderCommandQueue& queue,
    std::size_t first, std::size_t last) {
    if (queue.sorted_indices.empty() || first >= last ||
        last >= queue.sorted_indices.size()) {
        return;
    }

    const auto sort_key_for_index = [&queue](std::size_t index) {
        const std::size_t command_index = queue.sorted_indices[index];
        return command_index < queue.commands.size()
            ? queue.commands[command_index].sort_key
            : 0xffffffffu;
    };

    std::size_t left = first;
    std::size_t right = last;
    const u32 pivot = sort_key_for_index((first + last) / 2);
    while (left <= right) {
        while (left < last && sort_key_for_index(left) < pivot) {
            ++left;
        }
        while (first < right && pivot < sort_key_for_index(right)) {
            --right;
        }
        if (left <= right) {
            std::swap(queue.sorted_indices[left], queue.sorted_indices[right]);
            ++left;
            if (right == 0) {
                break;
            }
            --right;
        }
    }
    if (first < right) {
        QuickSortGameplayRenderSortedIndicesBySortKey(queue, first, right);
    }
    if (left < last) {
        QuickSortGameplayRenderSortedIndicesBySortKey(queue, left, last);
    }
}

void EnsureGameplayRenderSortedIndices(GameplayRenderCommandQueue& queue) {
    if (queue.sorted) {
        return;
    }
    if (queue.sorted_indices.size() != queue.commands.size()) {
        queue.sorted_indices.resize(queue.commands.size());
        for (std::size_t i = 0; i < queue.sorted_indices.size(); ++i) {
            queue.sorted_indices[i] = i;
        }
    }
    if (queue.sorted_indices.size() > 1) {
        QuickSortGameplayRenderSortedIndicesBySortKey(
            queue, 0, queue.sorted_indices.size() - 1);
    }
    queue.sorted = true;
}

void NoOpGameplayRenderQueueTail() {
}

bool NoOpQueuedRenderCommand(
    GameplayRenderCommandQueue&, const GameplayRenderCommand&) {
    return true;
}

void ProcessGameplayRenderCommandQueue(GameplayRenderCommandQueue& queue) {
    SortGameplayRenderCommandQueue(queue);
    for (std::size_t sorted_index : queue.sorted_indices) {
        if (sorted_index >= queue.commands.size()) {
            continue;
        }
        const GameplayRenderCommand& command = queue.commands[sorted_index];
        GameplayRenderCommandCallback callback = nullptr;
        if (command.class_id < queue.callbacks.dispatch_by_class.size()) {
            callback = queue.callbacks.dispatch_by_class[command.class_id];
        }
        if (callback == nullptr) {
            callback = queue.callbacks.default_dispatch;
        }
        if (callback != nullptr) {
            callback(queue, command);
        }
    }
}

GameplayRenderSpriteVariant ResolveGameplayRenderSpriteVariant(
    const UnitRenderItem& item) {
    const u32 state_flags = item.state_flags != 0 ? item.state_flags : item.runtime_flags;
    if ((item.draw_flags & kUnitAnimDrawMode2) != 0) {
        return (item.draw_flags & kUnitAnimDrawMode80) != 0 ?
            GameplayRenderSpriteVariant::high_red_mask :
            GameplayRenderSpriteVariant::high_blue_mask;
    }
    if ((state_flags & kUnitAnimStateBlendMode20) != 0) {
        return GameplayRenderSpriteVariant::grayscale;
    }
    if ((state_flags & kUnitAnimStateBlendMode40) != 0) {
        return GameplayRenderSpriteVariant::high_green_mask;
    }
    if ((item.command_flags & 0x40u) != 0 ||
        (item.command_bit_mask & 0x80u) != 0) {
        return GameplayRenderSpriteVariant::half_blend;
    }
    if ((state_flags & kUnitAnimStateDirectSpriteMode) != 0) {
        return GameplayRenderSpriteVariant::unit_ramp_low_blue_mask;
    }
    if ((state_flags & kUnitAnimStateShadowProbe) != 0) {
        return GameplayRenderSpriteVariant::channel_additive_tint;
    }
    return GameplayRenderSpriteVariant::unit_ramp_token1_shadow;
}

bool QueueGameplayUnitRenderCommand(GameplayRenderCommandQueue& queue,
    const UnitRenderQueueEntry& entry, const UnitRenderItem& item,
    i32 camera_x, i32 camera_y, UnitRenderQueueContext* source_context) {
    u32 sprite_entry = entry.type_id;
    if (entry.unit != nullptr) {
        const u32 image_group = std::min<u32>(entry.render_class,
            kUnitDefinitionImageGroupCount - 1);
        const u32 resolved_entry =
            GetUnitDefinitionImageResourceEntry(entry.type_id, image_group);
        if (resolved_entry != kInvalidResourceEntry) {
            sprite_entry = resolved_entry;
        }
        else {
            const u32 fallback_entry = GetUnitDefinitionImageResourceEntry(entry.type_id, 0);
            if (fallback_entry != kInvalidResourceEntry) {
                sprite_entry = fallback_entry;
            }
        }
    }

    GameplayRenderCommand command{};
    command.class_id = entry.layer;
    command.payload = entry.type_id;
    command.sort_key = entry.sort_key;
    command.sprite_entry_index = sprite_entry;
    command.screen_x = item.x - camera_x;
    command.screen_y = item.y - camera_y;
    command.packed_flags = item.type_id & kGameplayRenderPackedTypeMask;
    command.unit_render_context = source_context;
    command.unit_render_item = &item;
    command.draw_variant = ResolveGameplayRenderSpriteVariant(item);
    return QueueGameplayRenderCommand(queue, command);
}

void DispatchUnitAnimationRenderQueueItem(UnitRenderQueueContext& render_context,
    const UnitRenderItem& item, i32 screen_x, i32 screen_y) {
    // DispatchUnitRenderByType writes the raw unit owner to DAT_00758a4c at
    // 0x004c408f before jumping to either the mobile or structure renderer.
    // The typed render callback bypasses DrawQueuedUnitRenderCommand, so set
    // the same per-unit palette ramp here instead of inheriting the preceding
    // sprite's owner colour.
    SetSpriteUnitPaletteRamp(static_cast<u8>(item.owner_id));
    UnitAnimationUnit unit = make_unit_animation_unit(item, screen_x, screen_y);
    unit.visible_to_local_owner =
        IsUnitRenderItemIndividuallyVisibleToLocal(render_context, item);
    const UnitAnimationDefinition definition =
        make_unit_animation_definition(item);
    UnitAnimationDrawContext context = make_unit_animation_context(
        definition, item.global_frame_counter, render_context.local_owner_id,
        render_context.local_owner_is_observer,
        render_context.owner_relation_masks);
    DispatchUnitAnimationDraw(context, unit);
}

void DispatchUnitCellRenderQueueItem(UnitRenderQueueContext& render_context,
    const UnitRenderItem& item, i32 screen_x, i32 screen_y) {
    // See DispatchUnitRenderByType (0x004c408f): structures use their raw
    // owner palette ramp just like mobile units.
    SetSpriteUnitPaletteRamp(static_cast<u8>(item.owner_id));
    // The original cell renderers update the authoritative visibility grids
    // while their sorted draw command executes (0x004c5546/573c/58b1).  This
    // is what lets the map-brush pass reproduce the structure's last visible
    // construction, HP-blend, and damage-overlay state under explored fog.
    update_explored_fog_structure_snapshot(render_context, item);
    UnitAnimationUnit unit = make_unit_animation_unit(item, screen_x, screen_y);
    unit.visible_to_local_owner =
        IsUnitRenderItemIndividuallyVisibleToLocal(render_context, item);
    const UnitAnimationDefinition definition =
        make_unit_animation_definition(item);
    UnitAnimationDrawContext context = make_unit_animation_context(
        definition, item.global_frame_counter, render_context.local_owner_id,
        render_context.local_owner_is_observer,
        render_context.owner_relation_masks);
    DispatchUnitCellResourceDraw(context, unit);
}

void DispatchPlacementPreviewDefinitionSprite(
    UnitRenderQueueContext& render_context, const UnitRenderItem& item,
    i32 screen_x, i32 screen_y) {
    const UnitDefinitionResourceRecord* record =
        loaded_unit_definition_record(item.type_id);
    if (record == nullptr ||
        (record->image_group_counts[0] == 0 &&
            record->image_group_counts[1] == 0)) {
        return;
    }

    // FUN_004c5627 (called by FUN_004e2338) selects the local-player palette
    // ramp before forwarding the definition preview to 0x004d2f5a.  That
    // global ramp is intentionally left selected; the original does not save
    // and restore its previous value around this draw.
    SetSpriteUnitPaletteRamp(static_cast<u8>(render_context.local_owner_id));

    u32 entry = kInvalidResourceEntry;
    if (record->image_group_counts[0] != 0) {
        // 0x004c563c..0x004c564b computes group-0 base + count - 1
        // directly.  Do not route the preview through the ordinary animation
        // frame tables or construction blend policies.
        entry = GetUnitDefinitionImageFrameResourceEntry(
            item.type_id, 0, record->image_group_counts[0] - 1u);
    }
    else {
        // 0x004c5673..0x004c567c selects the first group-1 image directly.
        entry = GetUnitDefinitionImageResourceEntry(item.type_id, 1);
    }
    if (entry != kInvalidResourceEntry) {
        DrawResourceSpriteUnitRampToken1Shadow(entry, screen_x, screen_y);
    }
}

bool DispatchQueuedUnitRenderByTypeCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command) {
    if (command.unit_render_context != nullptr && command.unit_render_item != nullptr) {
        const u32 type_index = std::min<u32>(
            command.unit_render_item->type_id, kUnitRenderDispatchCount - 1);
        if (command.unit_render_context->callbacks.dispatch_by_type[type_index] != nullptr) {
            DispatchUnitRenderByType(*command.unit_render_context,
                *command.unit_render_item, command.screen_x, command.screen_y);
            return true;
        }
    }
    return DrawQueuedUnitRenderCommand(queue, command);
}

bool DrawQueuedUnitRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command) {
    SetSpriteUnitPaletteRamp(static_cast<u8>(packed_palette_ramp(command)));

    const u32 type_id = packed_type_id(command);
    const GameplayRenderUnitSpriteDefinition* definition =
        resolve_unit_definition(queue, type_id);
    const bool highbit = (command.packed_flags & kGameplayRenderPackedRawUnitRamp) != 0;

    if (highbit && (definition == nullptr || !definition->has_special_draw)) {
        return draw_unit_base_sprite(command, definition);
    }

    bool ok = draw_unit_base_sprite(command, definition);
    if (definition != nullptr && !highbit) {
        ok = draw_extra_overlays(queue, command, *definition) && ok;
    } else if (definition != nullptr && highbit) {
        ok = draw_highbit_special_overlay(queue, command, *definition) && ok;
    }
    return ok;
}

bool DrawQueuedTerrainTileRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command) {
    (void)queue;
    if ((command.packed_flags & 1u) != 0) {
        return DrawResourceSpriteToken1ShadowOrMask(
            command.sprite_entry_index, command.screen_x, command.screen_y, 0x001fu);
    }
    return DrawResourceSpriteToken1Shadow(
        command.sprite_entry_index, command.screen_x, command.screen_y);
}

bool draw_unit_effect_trail_segments(
    const UnitEffectRuntimeState& state, std::size_t first_segment) {
    bool drew = false;
    for (std::size_t i = first_segment; i < state.trail_segments.size(); ++i) {
        const UnitEffectTrailSegment& segment = state.trail_segments[i];
        drew = DrawBackBufferLine16(segment.x0, segment.y0,
            segment.x1, segment.y1, segment.color) || drew;
    }
    return drew;
}

bool draw_low_id_effect_inline_trails(
    UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (effect.effect_id != 0x1e ||
        (effect.flags & kUnitEffectFlagImpact) == 0) {
        return false;
    }

    const std::size_t first_trail_segment = state.trail_segments.size();
    DrawUnitEffectWideImpactLineTrail(state, effect,
        effect.previous_x - state.viewport_left,
        effect.previous_y - state.viewport_top,
        effect.x - state.viewport_left,
        effect.y - state.viewport_top);
    return draw_unit_effect_trail_segments(state, first_trail_segment);
}

bool DispatchQueuedUnitEffectRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command) {
    (void)queue;
    UnitEffectRuntimeState* state = command.effect_runtime_context;
    UnitEffectRuntime* effect = command.effect_runtime;
    bool drew_inline_trail = false;
    if (state != nullptr && effect != nullptr && effect->active &&
        effect->effect_id >= 0x3du) {
        const std::size_t first_trail_segment = state->trail_segments.size();
        if (DispatchUnitEffectProjectileTrailRenderer(*state, *effect,
                effect->effect_id, command.screen_x, command.screen_y)) {
            if (state->trail_segments.size() == first_trail_segment) {
                return true;
            }
            return draw_unit_effect_trail_segments(*state, first_trail_segment);
        }
    }
    else if (state != nullptr && effect != nullptr && effect->active) {
        drew_inline_trail = draw_low_id_effect_inline_trails(*state, *effect);
    }

    bool drew_sprite = false;
    if (state != nullptr && effect != nullptr) {
        u32 sprite_entry = 0;
        u32 draw_mode = 0;
        // The low-ID 0x1e renderer draws its impact trail before validating
        // the impact frame, then anchors the valid sprite at the previous
        // endpoint rather than the queued current point (0x004eda5c..4edb08).
        if (!ResolveUnitEffectGenericSpriteRender(
                *state, *effect, sprite_entry, draw_mode)) {
            return drew_inline_trail;
        }
        i32 screen_x = command.screen_x;
        i32 screen_y = command.screen_y;
        if (effect->effect_id == 0x1e &&
            (effect->flags & kUnitEffectFlagImpact) != 0) {
            screen_x = effect->previous_x - state->viewport_left;
            screen_y = effect->previous_y - state->viewport_top;
        }
        drew_sprite = DrawResourceSpriteMode(
            sprite_entry, screen_x, screen_y, draw_mode);
        return drew_inline_trail || drew_sprite;
    }

    // Classes 1 and 7 also use this dispatcher for lifecycle sprites without
    // a UnitEffectRuntime pointer; retain their pre-existing fallback path.
    if (command.sprite_draw_mode_valid) {
        drew_sprite = DrawResourceSpriteMode(command.sprite_entry_index,
            command.screen_x, command.screen_y, command.sprite_draw_mode);
    } else {
        drew_sprite = DrawResourceSpriteNormal(
            command.sprite_entry_index, command.screen_x, command.screen_y);
    }
    return drew_inline_trail || drew_sprite;
}

bool DrawQueuedTerrainDecorationRenderCommand(
    GameplayRenderCommandQueue& queue, const GameplayRenderCommand& command) {
    (void)queue;
    return DrawResourceSpriteNormal(
        command.sprite_entry_index, command.screen_x, command.screen_y);
}

GameplayFrameRandomResult SelectGameplayFrameRandomLimit(
    GameplayFrameRandomState& state, u32 preserved_value, u32 frame_counter) {
    GameplayFrameRandomResult result{};
    result.preserved_value = preserved_value;
    if (state.limit == 0) {
        return result;
    }

    const u32 dividend = state.seed + frame_counter;
    const u32 quotient = dividend / state.limit;
    result.selected_value = dividend % state.limit;
    state.seed += quotient;
    state.seed ^=
        ReadOriginalRandomScrambleDword(state.scramble,
            (quotient >> 28) & 0x0f) + 1u;
    ++state.call_count;
    return result;
}

} // namespace ranker
