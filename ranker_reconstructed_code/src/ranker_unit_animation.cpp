#include "ranker_unit_animation.h"

#include <algorithm>

namespace ranker {
namespace {

const UnitAnimationDefinition kFallbackDefinition{};
constexpr std::array<i32, kUnitAnimationPrimaryDirectionTableCount>
    kOriginalPrimaryDirectionRows = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 4, 3, 2};
constexpr std::array<bool, kUnitAnimationPrimaryDirectionTableCount>
    kOriginalPrimaryDirectionFlips = {
        false, false, false, false, false, false, false, false, false,
        false, false, false, false, false, false, true, true, true};
constexpr std::array<i32, kUnitAnimationExtendedDirectionTableCount>
    kOriginalExtendedDirectionRows = {
        0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 0, 0, 0, 1, 2, 3, 4,
        5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1, 0};
constexpr std::array<bool, kUnitAnimationExtendedDirectionTableCount>
    kOriginalExtendedDirectionFlips = {
        false, false, false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, false,
        false, true, true, true, true, true, true, true, false};

struct UnitBarPixelOffset {
    i32 dx;
    i32 dy;
};

const UnitAnimationDefinition& definition_or_fallback(
    const UnitAnimationDrawContext& context) {
    return context.definition != nullptr ? *context.definition : kFallbackDefinition;
}

bool uses_extended_direction_table(UnitAnimationSequence sequence) {
    return sequence == UnitAnimationSequence::action_fallback_extended;
}

u32 direction_index(const UnitAnimationDrawContext& context,
    const UnitAnimationDefinition& definition, const UnitAnimationUnit& unit,
    UnitAnimationSequence sequence) {
    const bool extended = uses_extended_direction_table(sequence);
    const i32 direction = unit.direction +
        definition.direction_offset * (extended ? 2 : 1);
    const u32 capacity = static_cast<u32>(
        extended ? context.tables.extended_direction_to_resource_row.size()
                 : context.tables.direction_to_resource_row.size());
    u32 count = extended ? context.tables.extended_direction_entry_count
                         : context.tables.direction_entry_count;
    if (count == 0 || count > capacity) {
        count = capacity;
    }
    if (direction <= 0) {
        return 0;
    }
    const u32 index = static_cast<u32>(direction);
    return std::min(index, count - 1);
}

u32 direction_resource_row(const UnitAnimationDrawContext& context,
    const UnitAnimationDefinition& definition, const UnitAnimationUnit& unit,
    UnitAnimationSequence sequence) {
    const u32 dir_index = direction_index(context, definition, unit, sequence);
    if (uses_extended_direction_table(sequence)) {
        return static_cast<u32>(
            context.tables.extended_direction_to_resource_row[dir_index]);
    }
    return static_cast<u32>(context.tables.direction_to_resource_row[dir_index]);
}

u32 sequence_bias(UnitAnimationSequence sequence) {
    switch (sequence) {
    case UnitAnimationSequence::alternate_default:
        return 0x0800;
    case UnitAnimationSequence::moving:
        return 0x1000;
    case UnitAnimationSequence::action:
        return 0x2000;
    case UnitAnimationSequence::action_fallback:
    case UnitAnimationSequence::action_fallback_extended:
        return 0x2400;
    case UnitAnimationSequence::alternate_action:
        return 0x2500;
    case UnitAnimationSequence::conditional_alternate_action:
        return 0x2600;
    case UnitAnimationSequence::direct_action:
        return 0x2800;
    case UnitAnimationSequence::queued_command:
        return 0x2c00;
    case UnitAnimationSequence::direct_sprite:
        return 0x3000;
    case UnitAnimationSequence::direct_timed:
        return 0x3400;
    case UnitAnimationSequence::moving_action_primary:
        return 0x3800;
    case UnitAnimationSequence::moving_action_alternate:
        return 0x3c00;
    case UnitAnimationSequence::cell_base:
        return 0x5000;
    case UnitAnimationSequence::cell_flag4:
        return 0x5400;
    case UnitAnimationSequence::cell_flag40:
        return 0x5800;
    case UnitAnimationSequence::cell_construction:
        return 0x5c00;
    case UnitAnimationSequence::cell_progress:
        return 0x6000;
    case UnitAnimationSequence::cell_channel_additive:
        return 0x6200;
    case UnitAnimationSequence::low_health_overlay:
        return 0x6400;
    case UnitAnimationSequence::shadow_attachment:
        return 0x6800;
    case UnitAnimationSequence::forced_highlight:
        return 0x4000;
    case UnitAnimationSequence::default_idle:
    default:
        return 0;
    }
}

void dispatch_draw(UnitAnimationDrawContext& context, const UnitAnimationUnit& unit,
    UnitAnimationSequence sequence, UnitAnimationDrawKind kind, u32 resource_frame,
    u32 animation_frame, u32 direction_row, bool flipped,
    u32 resource_draw_mode = 0) {
    const UnitAnimationDrawCommand command{
        &unit,
        sequence,
        kind,
        resource_frame,
        animation_frame,
        direction_row,
        unit.screen_x,
        unit.screen_y,
        flipped,
        resource_draw_mode,
    };
    context.last_command = command;

    UnitAnimationDrawCallback callback = context.callbacks.draw_sprite;
    if (sequence == UnitAnimationSequence::direct_sprite &&
        context.callbacks.draw_direct_sprite != nullptr) {
        callback = context.callbacks.draw_direct_sprite;
    }
    else if (sequence == UnitAnimationSequence::forced_highlight &&
        context.callbacks.draw_highlight_sprite != nullptr) {
        callback = context.callbacks.draw_highlight_sprite;
    }

    if (callback != nullptr) {
        callback(context, command);
    }
}

u32 resolve_unit_frame(const UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence, u32 animation_frame) {
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    const u32 row = direction_resource_row(context, definition, unit, sequence);
    return sequence_bias(sequence) + unit.type_id * 0x100 + animation_frame + row;
}

void resource_frame_parts(const UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence, u32 resource_frame,
    u32& animation_frame, u32& direction_row) {
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    direction_row = direction_resource_row(context, definition, unit, sequence);
    const u32 low_frame = resource_frame & 0xffu;
    animation_frame = low_frame >= direction_row
        ? low_frame - direction_row : unit.animation_frame;
}

void prime_tail_draw_command(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence,
    UnitAnimationDrawKind kind, u32 resource_frame, u32 animation_frame,
    u32 direction_row, bool flipped) {
    context.last_command = UnitAnimationDrawCommand{
        &unit,
        sequence,
        kind,
        resource_frame,
        animation_frame,
        direction_row,
        unit.screen_x,
        unit.screen_y,
        flipped,
        0,
    };
}

UnitAnimationDrawKind select_tail_draw_kind(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, bool flipped) {
    if ((unit.draw_flags & kUnitAnimDrawMode2) != 0) {
        return (unit.draw_flags & kUnitAnimDrawMode80) != 0 ?
            UnitAnimationDrawKind::mode_80 : UnitAnimationDrawKind::mode_2;
    }
    if ((unit.state_flags & kUnitAnimStateBlendMode20) != 0) {
        return UnitAnimationDrawKind::blend_20;
    }
    if ((unit.state_flags & kUnitAnimStateBlendMode40) != 0) {
        return UnitAnimationDrawKind::blend_40;
    }
    if ((unit.command_flags & 0x40) != 0 || (unit.command_bit_mask & 0x80) != 0) {
        return unit.visible_to_local_owner
            ? UnitAnimationDrawKind::blend_factor_0f
            : UnitAnimationDrawKind::neighbor_copy;
    }
    if ((unit.state_flags & kUnitAnimStateDirectSpriteMode) != 0 &&
        (unit.owner_id == context.local_owner_id || context.local_owner_is_observer)) {
        return UnitAnimationDrawKind::ally_or_local;
    }
    if ((unit.state_flags & kUnitAnimStateShadowProbe) != 0 &&
        unit.visible_to_local_owner) {
        return UnitAnimationDrawKind::shadow_probe_additive_tint;
    }
    return flipped ? UnitAnimationDrawKind::flipped : UnitAnimationDrawKind::normal;
}

void draw_tail_overlays_and_final_sprite(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence, u32 resource_frame,
    u32 animation_frame, u32 direction_row, bool flipped) {
    prime_tail_draw_command(context, unit, sequence,
        flipped ? UnitAnimationDrawKind::flipped : UnitAnimationDrawKind::normal,
        resource_frame, animation_frame, direction_row, flipped);

    if ((unit.state_flags & kUnitAnimStateSpecialOverlay) != 0) {
        DrawUnitSpecialOverlayIfEnabled(context, unit);
    }
    if ((unit.command_flags & kUnitAnimCommandStatusOverlayMask) != 0) {
        DrawUnitStatusOverlayIfEnabled(context, unit);
    }
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        ApplyUnitOwnerRelationTint(context, unit);
    }

    const UnitAnimationDrawKind draw_kind =
        select_tail_draw_kind(context, unit, flipped);
    dispatch_draw(context, unit, sequence, draw_kind, resource_frame, animation_frame,
        direction_row, flipped);

    DrawUnitSelectionOrTargetMarker(context, unit);
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        DrawUnitHealthAndSecondaryBars(context, unit);
    }
    DrawUnitDisplayNameIfPresent(context, unit);
}

void draw_resolved_frame(UnitAnimationDrawContext& context, const UnitAnimationUnit& unit,
    UnitAnimationSequence sequence) {
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    const u32 resource_frame = ResolveUnitAnimationFrame(context, unit, sequence);
    if (IsUnitAnimationDirectionFlipped(context, definition, unit, sequence)) {
        DrawUnitAnimationFrameFlipped(context, unit, resource_frame, sequence);
        return;
    }

    DrawUnitAnimationFrameSharedTail(context, unit, resource_frame, sequence);
}

void initialize_ramp(UnitRenderColorRamps& ramps, i32 x_step, i32 y_step,
    i32 secondary_step) {
    i32 x = 0;
    i32 y = 0;
    i32 secondary = 0;
    u32 index = 0;
    for (; index < 0x10; ++index) {
        x += x_step * 2;
        y += y_step;
        secondary += secondary_step * 2;
        ramps.x_offsets[index] = x;
        ramps.y_offsets[index] = y;
        ramps.secondary_offsets[index] = secondary;
    }
    for (; index < 0x20; ++index) {
        x -= x_step * 2;
        y -= y_step;
        secondary -= secondary_step * 2;
        ramps.x_offsets[index] = x;
        ramps.y_offsets[index] = y;
        ramps.secondary_offsets[index] = secondary;
    }
}

u32 ratio_31(u32 value, u32 max_value) {
    if (max_value == 0) {
        return 0;
    }
    return static_cast<u32>((static_cast<u64>(value) * 0x1f) / max_value);
}

u32 cell_health_blend_factor(u32 value, u32 max_value) {
    // DAT_0072c4b0 clamps the HP ratio plus six to [10, 31].
    return std::clamp<u32>(ratio_31(value, max_value) + 6u, 10u, 0x1fu);
}

UnitAnimationDrawKind forced_channel_additive_kind(UnitAnimationSequence sequence) {
    return sequence == UnitAnimationSequence::direct_timed
        ? UnitAnimationDrawKind::timed_channel_additive_tint
        : UnitAnimationDrawKind::palette_channel_additive_tint;
}

void draw_cell_frame(UnitAnimationDrawContext& context, const UnitAnimationUnit& unit,
    UnitAnimationSequence sequence, u32 animation_frame) {
    const u32 resource_frame = resolve_unit_frame(context, unit, sequence, animation_frame);
    u32 resolved_animation_frame = 0;
    u32 direction_row = 0;
    resource_frame_parts(
        context, unit, sequence, resource_frame, resolved_animation_frame, direction_row);
    dispatch_draw(context, unit, sequence, UnitAnimationDrawKind::normal, resource_frame,
        resolved_animation_frame, direction_row, false);
}

void draw_cell_frame_with_kind(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence, u32 animation_frame,
    UnitAnimationDrawKind kind) {
    const u32 resource_frame = resolve_unit_frame(context, unit, sequence, animation_frame);
    u32 resolved_animation_frame = 0;
    u32 direction_row = 0;
    resource_frame_parts(
        context, unit, sequence, resource_frame, resolved_animation_frame, direction_row);
    dispatch_draw(context, unit, sequence, kind, resource_frame, resolved_animation_frame,
        direction_row, false);
}

void draw_cell_frame_with_resource_mode(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence, u32 animation_frame,
    u32 resource_draw_mode) {
    const u32 resource_frame = resolve_unit_frame(context, unit, sequence, animation_frame);
    u32 resolved_animation_frame = 0;
    u32 direction_row = 0;
    resource_frame_parts(
        context, unit, sequence, resource_frame, resolved_animation_frame, direction_row);
    dispatch_draw(context, unit, sequence, UnitAnimationDrawKind::resource_mode,
        resource_frame, resolved_animation_frame, direction_row, false,
        resource_draw_mode);
}

void draw_cell_resource_layer(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence,
    u32 animation_frame, u32 definition_blit_mode) {
    // FUN_004c538a/FUN_004c5468/FUN_004c568e use this exact precedence for
    // the real group-2/group-11/group-1 layer.  The optional group-0
    // construction-stage layer is drawn separately before reaching here.
    if ((unit.draw_flags & kUnitAnimDrawMode2) != 0) {
        const UnitAnimationDrawKind kind =
            (unit.draw_flags & kUnitAnimDrawMode80) != 0
            ? UnitAnimationDrawKind::mode_80
            : UnitAnimationDrawKind::mode_2;
        draw_cell_frame_with_kind(context, unit, sequence, animation_frame, kind);
        return;
    }

    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    if (definition.cell_construction_special_draw) {
        if (unit.max_hit_points != 0) {
            context.highlight_level =
                cell_health_blend_factor(unit.hit_points, unit.max_hit_points);
            if (context.highlight_level != 0x1f) {
                draw_cell_frame_with_kind(context, unit, sequence, animation_frame,
                    UnitAnimationDrawKind::blend_factor_ramp);
                return;
            }
        }
        // The original uses an `else if` for the per-layer byte.  A special
        // definition at full HP therefore draws normally instead of falling
        // through to +0x348/+0x349/+0x34a.
        draw_cell_frame(context, unit, sequence, animation_frame);
        return;
    }

    if (definition_blit_mode != 0) {
        draw_cell_frame_with_resource_mode(
            context, unit, sequence, animation_frame, definition_blit_mode);
        return;
    }
    draw_cell_frame(context, unit, sequence, animation_frame);
}

u32 construction_progress_frame(const UnitAnimationUnit& unit) {
    if (unit.construction_stage_count == 0) {
        return 0;
    }
    if (unit.construction_progress_limit == 0) {
        return unit.construction_stage_count - 1;
    }
    const u64 scaled = static_cast<u64>(unit.construction_stage_count - 1) *
        unit.construction_progress;
    return static_cast<u32>(scaled / unit.construction_progress_limit);
}

void draw_bar_cap_pixels(UnitAnimationDrawContext& context, const UnitAnimationUnit& unit,
    i32 x, i32 y, u16 color, const std::array<UnitBarPixelOffset, 9>& offsets) {
    if (context.callbacks.draw_bar_pixel == nullptr) {
        return;
    }
    for (const UnitBarPixelOffset& offset : offsets) {
        context.callbacks.draw_bar_pixel(context, unit, x + offset.dx, y + offset.dy,
            color);
    }
}

void dispatch_command_state_animation(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    const u32 command_id = unit.command_state & 0x00ffffffu;
    switch (command_id) {
    case 0x01: case 0x05: case 0x08: case 0x09: case 0x0c: case 0x0d:
    case 0x10: case 0x14: case 0x17: case 0x1c: case 0x1f: case 0x23:
    case 0x28: case 0x2b: case 0x30: case 0x33: case 0x35: case 0x36:
    case 0x3c: case 0x3d: case 0x41: case 0x42: case 0x48: case 0x53:
    case 0x56: case 0x57: case 0x5a: case 0x5f: case 0x64: case 0x6c:
    case 0x73: case 0x74: case 0x7d: case 0x87: case 0x88:
        DrawUnitDefaultAnimationFrame(context, unit);
        return;
    case 0x02: case 0x03: case 0x06: case 0x0a: case 0x0e: case 0x12:
    case 0x15: case 0x16: case 0x1e: case 0x21: case 0x22: case 0x25:
    case 0x31: case 0x32: case 0x37: case 0x38: case 0x3e: case 0x3f:
    case 0x43: case 0x49: case 0x55: case 0x5c: case 0x61: case 0x66:
    case 0x6a: case 0x75: case 0x7f: case 0x89:
        DrawUnitActionFallbackAnimationFrame(context, unit);
        return;
    case 0x04: case 0x1d: case 0x20: case 0x39: case 0x3a: case 0x54:
    case 0x58: case 0x5b: case 0x65: case 0x69: case 0x7e:
        DrawUnitQueuedCommandAnimationFrame(context, unit);
        return;
    case 0x2a: case 0x2c:
        DrawUnitActionAnimationFrame(context, unit);
        return;
    case 0x11:
        DrawUnitAlternateActionAnimationFrame(context, unit);
        return;
    case 0x29:
        DrawUnitConditionalAlternateActionAnimationFrame(context, unit);
        return;
    case 0x2d:
        DrawUnitMovingAnimationFrame(context, unit);
        return;
    case 0x60:
        DrawUnitPaletteRampHighlightFrame(context, unit);
        return;
    case 0x6e: case 0x6f:
        DrawUnitTimedPaletteHighlightFrame(context, unit);
        return;
    case 0x78:
        DrawUnitMovingActionPrimaryFrame(context, unit);
        return;
    case 0x79:
        DrawUnitMovingActionAlternateFrame(context, unit);
        return;
    default:
        return;
    }
}

} // namespace

u32 ResolveUnitAnimationFrame(const UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence) {
    return resolve_unit_frame(context, unit, sequence, unit.animation_frame);
}

bool IsUnitAnimationDirectionFlipped(const UnitAnimationDrawContext& context,
    const UnitAnimationDefinition& definition, const UnitAnimationUnit& unit,
    UnitAnimationSequence sequence) {
    const u32 dir_index = direction_index(context, definition, unit, sequence);
    if (uses_extended_direction_table(sequence)) {
        return context.tables.extended_direction_is_flipped[dir_index];
    }
    return context.tables.direction_is_flipped[dir_index];
}

void ApplyUnitOwnerRelationTint(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    const bool allied = context.callbacks.is_owner_allied != nullptr &&
        context.callbacks.is_owner_allied(
            context, context.local_owner_id, unit.owner_id);
    const UnitOwnerRelationTint tint = ResolveUnitOwnerRelationTint(
        context.local_owner_id, unit.owner_id, allied);
    context.last_tint = tint;
    if (context.callbacks.apply_owner_tint != nullptr) {
        context.callbacks.apply_owner_tint(context, unit, tint);
    }
}

void DrawUnitSelectionOrTargetMarker(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (context.callbacks.draw_marker == nullptr) {
        return;
    }

    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    const u32 previous_marker_entry = context.current_marker_sprite_entry;
    i32 marker_x =
        unit.screen_x + definition.marker_offset_x + definition.marker_width - 8;
    i32 marker_y =
        unit.screen_y + definition.marker_offset_y + definition.marker_height - 8;
    const u32 marker_state = unit.animation_flags & kUnitAnimFlagSelectedOrTargeted;
    if (marker_state != 0) {
        const u32 marker_offset = marker_state < 0x0a ? marker_state : 0;
        context.current_marker_sprite_entry =
            context.selection_marker_base_entry != kUnitAnimationInvalidMarkerEntry
                ? context.selection_marker_base_entry + marker_offset
                : kUnitAnimationInvalidMarkerEntry;
        context.callbacks.draw_marker(context, unit,
            marker_x, marker_y);
        marker_y -= 8;
    }
    if ((unit.marker_flags & kUnitAnimSelectionMarkerBit) != 0) {
        context.current_marker_sprite_entry =
            context.selection_marker_base_entry != kUnitAnimationInvalidMarkerEntry
                ? context.selection_marker_base_entry + 0x18
                : kUnitAnimationInvalidMarkerEntry;
        context.callbacks.draw_marker(context, unit, marker_x, marker_y);
    }
    context.current_marker_sprite_entry = previous_marker_entry;
}

void DrawUnitSpecialOverlayIfEnabled(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (context.special_overlay_resources_loaded &&
        context.callbacks.draw_special_overlay != nullptr) {
        context.callbacks.draw_special_overlay(context, unit);
    }
}

void DrawUnitStatusOverlayIfEnabled(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (context.status_overlay_resources_loaded &&
        context.callbacks.draw_status_overlay != nullptr) {
        context.callbacks.draw_status_overlay(context, unit);
    }
}

void DrawUnitHealthAndSecondaryBars(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    const i32 x = unit.screen_x + definition.bars_offset_x;
    i32 y = unit.screen_y + definition.bars_offset_y;
    const i32 width = definition.bars_width + 1;

    if (context.callbacks.draw_health_bar != nullptr) {
        context.callbacks.draw_health_bar(context, unit, x, y, width);
    }
    if (unit.max_hit_points != 0) {
        y += 4;
    }
    if (unit.owner_id == context.local_owner_id && unit.secondary_bar_enabled &&
        unit.max_secondary_value != 0 &&
        context.callbacks.draw_secondary_bar != nullptr) {
        context.callbacks.draw_secondary_bar(context, unit, x, y, width);
    }
}

void DrawUnitDisplayNameIfPresent(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    // FUN_004c523d / 0x004c50ed gate this tail on the raw string-slot word,
    // not on the first byte stored in the slot.  Script-created empty slots
    // therefore still select font four and issue an empty draw.
    if (!unit.display_name_slot_present ||
        context.callbacks.draw_display_name == nullptr) {
        return;
    }

    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    const i32 center_x = unit.screen_x + definition.name_offset_x +
        (definition.name_width >> 1);
    const i32 baseline_y = unit.screen_y + definition.name_offset_y -
        (context.text_half_height >> 1);
    context.callbacks.draw_display_name(context, unit, center_x, baseline_y);
}

void DrawUnitAnimationFrameFlipped(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame, UnitAnimationSequence sequence) {
    u32 animation_frame = 0;
    u32 direction_row = 0;
    resource_frame_parts(
        context, unit, sequence, resource_frame, animation_frame, direction_row);
    draw_tail_overlays_and_final_sprite(context, unit, sequence, resource_frame,
        animation_frame, direction_row, true);
}

void DrawUnitAnimationFrameForcedHighlight(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame, UnitAnimationSequence sequence) {
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        ApplyUnitOwnerRelationTint(context, unit);
    }
    u32 animation_frame = 0;
    u32 direction_row = 0;
    resource_frame_parts(
        context, unit, sequence, resource_frame, animation_frame, direction_row);
    dispatch_draw(context, unit, sequence, forced_channel_additive_kind(sequence),
        resource_frame, animation_frame, direction_row, true);
    DrawUnitSelectionOrTargetMarker(context, unit);
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        DrawUnitHealthAndSecondaryBars(context, unit);
    }
    DrawUnitDisplayNameIfPresent(context, unit);
}

void DrawUnitAnimationFrameSharedTail(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame, UnitAnimationSequence sequence) {
    u32 animation_frame = 0;
    u32 direction_row = 0;
    resource_frame_parts(
        context, unit, sequence, resource_frame, animation_frame, direction_row);
    draw_tail_overlays_and_final_sprite(context, unit, sequence, resource_frame,
        animation_frame, direction_row, false);
}

void DrawUnitAlternateDefaultAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    const bool alternate_resource_present =
        (unit.command_flags & kUnitAnimCommandMoving) != 0
            ? definition.has_alternate_default_resource_alt
            : definition.has_alternate_default_resource;
    if (!alternate_resource_present) {
        // The original branches at 0x004c4697 fall back directly to the
        // ordinary group-0/1 frame resolver when the group-8/13 frame count
        // is zero.  Calling DrawUnitDefaultAnimationFrame here would inspect
        // the alternate flag again and recurse.
        draw_resolved_frame(context, unit, UnitAnimationSequence::default_idle);
        return;
    }
    draw_resolved_frame(context, unit, UnitAnimationSequence::alternate_default);
}

void DrawUnitDefaultAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if ((unit.command_flags & kUnitAnimCommandAlternate) != 0) {
        DrawUnitAlternateDefaultAnimationFrame(context, unit);
        return;
    }
    draw_resolved_frame(context, unit, UnitAnimationSequence::default_idle);
}

void DrawUnitMovingAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    if ((unit.command_flags & kUnitAnimCommandMoving) == 0 &&
        !definition.has_move_resource) {
        DrawUnitDefaultAnimationFrame(context, unit);
        return;
    }
    if ((unit.command_flags & kUnitAnimCommandMoving) != 0 &&
        !definition.has_move_resource_alt) {
        DrawUnitDefaultAnimationFrame(context, unit);
        return;
    }
    draw_resolved_frame(context, unit, UnitAnimationSequence::moving);
}

void DrawUnitActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    draw_resolved_frame(context, unit, UnitAnimationSequence::action);
}

void DrawUnitActionFallbackAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if ((unit.command_flags & kUnitAnimCommandAlternate) != 0) {
        DrawUnitActionAnimationFrame(context, unit);
        return;
    }
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    draw_resolved_frame(context, unit,
        definition.action_fallback_uses_extended_directions
            ? UnitAnimationSequence::action_fallback_extended
            : UnitAnimationSequence::action_fallback);
}

void DrawUnitDirectActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    draw_resolved_frame(context, unit, UnitAnimationSequence::direct_action);
}

void DrawUnitQueuedCommandAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (unit.command_lockout_ticks != 0) {
        UnitAnimationUnit frame_unit = unit;
        frame_unit.animation_frame = unit.animation_timer;
        DrawUnitDefaultAnimationFrame(context, frame_unit);
        return;
    }
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    if (!definition.has_queued_command_resource) {
        DrawUnitDefaultAnimationFrame(context, unit);
        return;
    }
    draw_resolved_frame(context, unit, UnitAnimationSequence::queued_command);
}

void DrawUnitAlternateActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    draw_resolved_frame(context, unit, UnitAnimationSequence::alternate_action);
}

void DrawUnitConditionalAlternateActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if ((unit.command_flags & kUnitAnimCommandConditionalAlternateMask) == 0 &&
        unit.previous_command_state != 0) {
        return;
    }
    draw_resolved_frame(context, unit, UnitAnimationSequence::conditional_alternate_action);
}

void DrawUnitDirectSpriteAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if ((unit.state_flags & kUnitAnimStateDirectSpriteMode) != 0) {
        const u32 resource_frame = ResolveUnitAnimationFrame(
            context, unit, UnitAnimationSequence::direct_sprite);
        u32 animation_frame = 0;
        u32 direction_row = 0;
        resource_frame_parts(context, unit, UnitAnimationSequence::direct_sprite,
            resource_frame, animation_frame, direction_row);
        dispatch_draw(context, unit, UnitAnimationSequence::direct_sprite,
            UnitAnimationDrawKind::normal, resource_frame, animation_frame, direction_row, false);
        return;
    }

    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    const u32 frame = ResolveUnitAnimationFrame(
        context, unit, UnitAnimationSequence::direct_sprite);
    if ((unit.command_state & kUnitAnimCommandStateMirror) != 0) {
        const u32 elapsed = std::min(
            unit.animation_timer, unit.command_entry_lockout_ticks);
        context.highlight_level = unit.command_entry_lockout_ticks != 0
            ? ratio_31(unit.command_entry_lockout_ticks - elapsed,
                unit.command_entry_lockout_ticks)
            : 0;
        u32 animation_frame = 0;
        u32 direction_row = 0;
        resource_frame_parts(context, unit, UnitAnimationSequence::direct_sprite,
            frame, animation_frame, direction_row);
        const bool flipped = IsUnitAnimationDirectionFlipped(
            context, definition, unit, UnitAnimationSequence::direct_sprite);
        dispatch_draw(context, unit, UnitAnimationSequence::direct_sprite,
            UnitAnimationDrawKind::blend_factor_ramp, frame, animation_frame,
            direction_row, flipped);
        DrawUnitSelectionOrTargetMarker(context, unit);
        DrawUnitDisplayNameIfPresent(context, unit);
        return;
    }
    if (IsUnitAnimationDirectionFlipped(
            context, definition, unit, UnitAnimationSequence::direct_sprite)) {
        DrawUnitAnimationFrameFlipped(context, unit, frame,
            UnitAnimationSequence::direct_sprite);
        return;
    }
    DrawUnitAnimationFrameSharedTail(context, unit, frame,
        UnitAnimationSequence::direct_sprite);
}

void DrawUnitPaletteRampHighlightFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (!ShouldDrawUnitPaletteRampHighlight(unit.cargo_amount)) {
        return;
    }
    context.highlight_level = unit.animation_frame & 0x1fu;
    const u32 frame = ResolveUnitAnimationFrame(
        context, unit, UnitAnimationSequence::default_idle);
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    if (IsUnitAnimationDirectionFlipped(
            context, definition, unit, UnitAnimationSequence::default_idle)) {
        DrawUnitAnimationFrameForcedHighlight(
            context, unit, frame, UnitAnimationSequence::default_idle);
        return;
    }
    DrawUnitAnimationFrameForcedNormalHighlight(
        context, unit, frame, UnitAnimationSequence::default_idle);
}

void DrawUnitTimedPaletteHighlightFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    context.highlight_level = unit.animation_frame & 0x1fu;
    const u32 frame = ResolveUnitAnimationFrame(
        context, unit, UnitAnimationSequence::direct_timed);
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    if (IsUnitAnimationDirectionFlipped(
            context, definition, unit, UnitAnimationSequence::direct_timed)) {
        DrawUnitAnimationFrameForcedHighlight(
            context, unit, frame, UnitAnimationSequence::direct_timed);
        return;
    }
    DrawUnitAnimationFrameForcedNormalHighlight(
        context, unit, frame, UnitAnimationSequence::direct_timed);
}

void DrawUnitMovingActionPrimaryFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    draw_resolved_frame(context, unit, UnitAnimationSequence::moving_action_primary);
}

void DrawUnitMovingActionAlternateFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    draw_resolved_frame(context, unit, UnitAnimationSequence::moving_action_alternate);
}

void DrawUnitAnimationFrameForcedNormalHighlight(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame, UnitAnimationSequence sequence) {
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        ApplyUnitOwnerRelationTint(context, unit);
    }
    u32 animation_frame = 0;
    u32 direction_row = 0;
    resource_frame_parts(context, unit, sequence,
        resource_frame, animation_frame, direction_row);
    dispatch_draw(context, unit, sequence, forced_channel_additive_kind(sequence),
        resource_frame, animation_frame, direction_row, false);
    DrawUnitSelectionOrTargetMarker(context, unit);
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        DrawUnitHealthAndSecondaryBars(context, unit);
    }
    DrawUnitDisplayNameIfPresent(context, unit);
}

void DispatchUnitCellResourceDraw(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    context.current_unit = &unit;
    // FUN_004c523d tests raw state bit 2 before entering the ordinary
    // structure-render tail.  Its direct-sprite/shadow path therefore never
    // calls ApplyUnitOwnerRelationTint (0x004c526b is only reached when bit 2
    // is clear).  Keep that gate ahead of the relation overlay so transient
    // direct-sprite structures do not leave a stray owner marker behind.
    if ((unit.state_flags & kUnitAnimStateDirectSprite) != 0) {
        DrawUnitShadowAndAttachmentSprites(context, unit);
        return;
    }
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        ApplyUnitOwnerRelationTint(context, unit);
    }
    if (unit.cell_construction_progress_active) {
        DrawUnitCellConstructionProgressFrame(context, unit);
    }
    else if (unit.cell_channel_additive_active) {
        context.highlight_level = unit.cell_channel_additive_frame;
        draw_cell_frame_with_kind(context, unit,
            UnitAnimationSequence::cell_channel_additive,
            unit.construction_stage_count != 0 ? unit.construction_stage_count - 1 : 0,
            UnitAnimationDrawKind::channel_additive_tint);
        return;
    }
    else if ((unit.command_metadata_flags & 0x4) != 0 ||
        (unit.command_metadata_flags == 0 && (unit.terrain_cell_flags & 0x4) != 0)) {
        DrawUnitCellFlag4ResourceFrame(context, unit);
    }
    else if ((unit.command_metadata_flags & 0x40) != 0 ||
        (unit.command_metadata_flags == 0 && (unit.terrain_cell_flags & 0x40) != 0)) {
        DrawUnitCellFlag40ResourceFrame(context, unit);
    }
    else {
        if ((unit.definition_cell_flags & 0x1) != 0) {
            DrawUnitCellConstructionStageFrame(context, unit);
        }
        DrawUnitCellBaseResourceFrame(context, unit);
    }

    if (!unit.cell_construction_progress_active && unit.max_hit_points != 0 &&
        unit.hit_points < unit.max_hit_points - (unit.max_hit_points >> 2)) {
        DrawUnitLowHealthDamageOverlay(context, unit);
    }
    DrawUnitSelectionOrTargetMarker(context, unit);
    if (ShouldDrawUnitWorldBars(unit.animation_flags)) {
        DrawUnitHealthAndSecondaryBars(context, unit);
    }
    DrawUnitDisplayNameIfPresent(context, unit);
}

void DrawUnitCellFlag4ResourceFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if ((unit.definition_cell_flags & 0x2) != 0) {
        DrawUnitCellConstructionStageFrame(context, unit);
    }
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    draw_cell_resource_layer(context, unit, UnitAnimationSequence::cell_flag4,
        unit.cell_animation_frame, definition.cell_flag4_blit_mode);
}

void DrawUnitCellFlag40ResourceFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if ((unit.definition_cell_flags & 0x4) != 0) {
        DrawUnitCellConstructionStageFrame(context, unit);
    }
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    draw_cell_resource_layer(context, unit, UnitAnimationSequence::cell_flag40,
        unit.cell_flag40_animation_frame, definition.cell_flag40_blit_mode);
}

void DrawUnitCellConstructionStageFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (unit.construction_stage_count == 0) {
        return;
    }
    const u32 frame = unit.construction_stage_count - 1;

    // FUN_004c5546 selects the final group-0 frame, but it does not always
    // use the normal blitter.  Raw +0xa4 draw modes take precedence, then
    // raw +0x9c bit 0x40 forces factor 0x0f, and definitions whose +0x5e8
    // field is 1 fade with the original HP lookup table.
    if ((unit.draw_flags & kUnitAnimDrawMode2) != 0) {
        const UnitAnimationDrawKind kind =
            (unit.draw_flags & kUnitAnimDrawMode80) != 0
            ? UnitAnimationDrawKind::mode_80
            : UnitAnimationDrawKind::mode_2;
        draw_cell_frame_with_kind(context, unit,
            UnitAnimationSequence::cell_construction, frame, kind);
        return;
    }
    if ((unit.command_flags & 0x40u) != 0) {
        context.highlight_level = 0x0f;
        draw_cell_frame_with_kind(context, unit,
            UnitAnimationSequence::cell_construction, frame,
            UnitAnimationDrawKind::blend_factor_ramp);
        return;
    }
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    if (definition.cell_construction_special_draw && unit.max_hit_points != 0) {
        context.highlight_level = std::clamp<u32>(
            ratio_31(unit.hit_points, unit.max_hit_points) + 6u, 10u, 0x1fu);
        if (context.highlight_level != 0x1f) {
            draw_cell_frame_with_kind(context, unit,
                UnitAnimationSequence::cell_construction, frame,
                UnitAnimationDrawKind::blend_factor_ramp);
            return;
        }
    }
    draw_cell_frame(context, unit, UnitAnimationSequence::cell_construction, frame);
}

void DrawPlacementPreviewDefinitionSprite(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    UnitAnimationUnit preview = unit;
    preview.owner_id = context.local_owner_id;
    // FUN_004c5627 prefers the final image from definition group 0 and only
    // falls back to the first group-1 image when group 0 is empty.
    if (preview.construction_stage_count != 0) {
        DrawUnitCellConstructionStageFrame(context, preview);
        return;
    }
    draw_cell_frame(context, preview, UnitAnimationSequence::cell_base,
        preview.cell_animation_frame);
}

void DrawUnitCellBaseResourceFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    const UnitAnimationDefinition& definition = definition_or_fallback(context);
    draw_cell_resource_layer(context, unit, UnitAnimationSequence::cell_base,
        unit.cell_animation_frame, definition.cell_base_blit_mode);
}

void DrawUnitCellConstructionProgressFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (unit.construction_stage_count == 0) {
        return;
    }

    const u32 frame = construction_progress_frame(unit);
    if ((unit.draw_flags & kUnitAnimDrawMode2) != 0) {
        const UnitAnimationDrawKind kind = (unit.draw_flags & kUnitAnimDrawMode80) != 0
            ? UnitAnimationDrawKind::mode_80
            : UnitAnimationDrawKind::mode_2;
        draw_cell_frame_with_kind(
            context, unit, UnitAnimationSequence::cell_progress, frame, kind);
        return;
    }

    // FUN_004c573c tests group-0's raw (frame_count - 1) value before it
    // scales that value by construction progress.  Consequently the HP fade
    // belongs only to definitions with exactly one construction frame.  A
    // multi-frame definition may scale to frame zero early in construction,
    // but the original still draws that frame normally.
    if (unit.construction_stage_count == 1 &&
        unit.construction_progress_limit != 0 &&
        unit.max_hit_points != 0) {
        context.highlight_level = ratio_31(unit.hit_points, unit.max_hit_points);
        if (context.highlight_level != 0x1f) {
            draw_cell_frame_with_kind(context, unit, UnitAnimationSequence::cell_progress, frame,
                UnitAnimationDrawKind::blend_factor_ramp);
            return;
        }
    }

    draw_cell_frame(context, unit, UnitAnimationSequence::cell_progress, frame);
}

void DrawUnitCellPaletteRampFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (unit.construction_stage_count == 0) {
        return;
    }
    context.highlight_level = unit.cell_channel_additive_frame;
    draw_cell_frame_with_kind(context, unit, UnitAnimationSequence::cell_channel_additive,
        unit.construction_stage_count - 1, UnitAnimationDrawKind::channel_additive_tint);
}

void DrawUnitLowHealthDamageOverlay(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    if (unit.max_hit_points == 0) {
        return;
    }
    u32 frame = unit.low_health_overlay_frame;
    const u32 quarter = unit.max_hit_points >> 2;
    // FUN_004c58b1 receives the already-computed 75-percent threshold from
    // FUN_004c523d.  Its first and second subtractions therefore test 50 and
    // 25 percent, not 75 and 50 percent.
    const u32 half_threshold = unit.max_hit_points - quarter - quarter;
    if (unit.hit_points < half_threshold) {
        frame += 0x15;
        if (unit.hit_points < half_threshold - quarter) {
            frame += 0x15;
        }
    }
    draw_cell_frame(context, unit, UnitAnimationSequence::low_health_overlay, frame);
}

void DrawUnitShadowAndAttachmentSprites(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    draw_cell_frame(context, unit, UnitAnimationSequence::shadow_attachment,
        unit.cell_animation_frame);
}

void DrawUnitMirrorShadowResourceIfPresent(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    draw_cell_frame(context, unit, UnitAnimationSequence::shadow_attachment,
        unit.cell_animation_frame);
}

void DrawUnitHealthBarLeftCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color) {
    static constexpr std::array<UnitBarPixelOffset, 9> kOffsets{{
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0},
        {0, 1}, {0, 2}, {0, 3}, {0, 4},
    }};
    draw_bar_cap_pixels(context, unit, x, y, color, kOffsets);
}

void DrawUnitHealthBarRightCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color) {
    static constexpr std::array<UnitBarPixelOffset, 9> kOffsets{{
        {0, 0}, {-1, 0}, {-2, 0}, {-3, 0}, {-4, 0},
        {0, 1}, {0, 2}, {0, 3}, {0, 4},
    }};
    draw_bar_cap_pixels(context, unit, x, y, color, kOffsets);
}

void DrawUnitSecondaryBarLeftCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color) {
    static constexpr std::array<UnitBarPixelOffset, 9> kOffsets{{
        {0, 0}, {0, -1}, {0, -2}, {0, -3}, {0, -4},
        {1, 0}, {2, 0}, {3, 0}, {4, 0},
    }};
    draw_bar_cap_pixels(context, unit, x, y, color, kOffsets);
}

void DrawUnitSecondaryBarRightCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color) {
    static constexpr std::array<UnitBarPixelOffset, 9> kOffsets{{
        {0, 0}, {-1, 0}, {-2, 0}, {-3, 0}, {-4, 0},
        {0, -1}, {0, -2}, {0, -3}, {0, -4},
    }};
    draw_bar_cap_pixels(context, unit, x, y, color, kOffsets);
}

void DispatchUnitAnimationDraw(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit) {
    context.current_unit = &unit;

    if ((unit.state_flags & kUnitAnimStateDirectSprite) != 0) {
        DrawUnitDirectSpriteAnimationFrame(context, unit);
        return;
    }

    if ((unit.command_state & kUnitAnimCommandStateMirror) == 0 ||
        (unit.state_flags & 0x20060) != 0) {
        dispatch_command_state_animation(context, unit);
        return;
    }

    if ((unit.command_flags & kUnitAnimCommandAlternate) != 0) {
        DrawUnitAlternateDefaultAnimationFrame(context, unit);
        return;
    }
    // DispatchUnitAnimationDraw (0x004c4364) checks the active command's
    // metadata table (DAT_007300e0), bit 0x20, before selecting image groups
    // 12/7.  The moving flag only chooses between those two groups; it is not
    // itself the gate.  Using runtime/status bits here selected the idle or
    // action image set for many high-bit command states.
    if ((unit.command_metadata_flags & 0x20u) != 0) {
        DrawUnitMovingAnimationFrame(context, unit);
        return;
    }

    DrawUnitDefaultAnimationFrame(context, unit);
}

void InitializeUnitRenderColorRamps(UnitAnimationDrawContext& context) {
    if (!context.use_555_color) {
        context.color_ramps.colors = {0x18e3, 0x7800, 0xf800, 0xffe0, 0x03e0, 0x3c1f};
        initialize_ramp(context.color_ramps, context.ramp_x_step,
            context.ramp_y_step * 3, context.ramp_secondary_step);
        return;
    }

    context.color_ramps.colors = {0x0c63, 0x3c00, 0x7c00, 0x7fe0, 0x01e0, 0x3c1f};
    initialize_ramp(context.color_ramps, context.ramp_x_step,
        context.ramp_y_step * 2, context.ramp_secondary_step);
}

void InitializeOriginalUnitAnimationFrameTables(UnitAnimationFrameTables& tables) {
    tables.direction_entry_count = kUnitAnimationPrimaryDirectionTableCount;
    tables.direction_to_resource_row.fill(0);
    tables.direction_is_flipped.fill(false);
    for (std::size_t i = 0; i < kOriginalPrimaryDirectionRows.size(); ++i) {
        tables.direction_to_resource_row[i] = kOriginalPrimaryDirectionRows[i];
        tables.direction_is_flipped[i] = kOriginalPrimaryDirectionFlips[i];
    }

    tables.extended_direction_entry_count = kUnitAnimationExtendedDirectionTableCount;
    tables.extended_direction_to_resource_row.fill(0);
    tables.extended_direction_is_flipped.fill(false);
    for (std::size_t i = 0; i < kOriginalExtendedDirectionRows.size(); ++i) {
        tables.extended_direction_to_resource_row[i] = kOriginalExtendedDirectionRows[i];
        tables.extended_direction_is_flipped[i] = kOriginalExtendedDirectionFlips[i];
    }
}

} // namespace ranker
