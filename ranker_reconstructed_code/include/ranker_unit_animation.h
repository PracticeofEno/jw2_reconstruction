#pragma once

#include "ranker_player_slots.h"
#include "ranker_types.h"

#include <array>
#include <string>

namespace ranker {

constexpr u32 kUnitAnimFlagSelectedOrTargeted = 0x0f;
constexpr u32 kUnitAnimFlagShowBars = 0x80;
constexpr bool ShouldDrawUnitWorldBars(u32 animation_flags) {
    return (animation_flags & kUnitAnimFlagShowBars) != 0;
}
constexpr bool ShouldDrawUnitPaletteRampHighlight(u32 cargo_amount) {
    // Command state 0x60 tests original raw unit +0x4c before entering its
    // palette-ramp draw path.  That state-dependent union is cargo_amount in
    // the typed runtime; raw +0x68 is the independent target/value word.
    return cargo_amount != 0;
}
constexpr u32 kUnitAnimCommandMoving = 0x1;
constexpr u32 kUnitAnimCommandConditionalAlternateMask = 0x3;
constexpr u32 kUnitAnimCommandAlternate = 0x4;
constexpr u32 kUnitAnimCommandStatusOverlayMask = 0x003c0000;
constexpr u32 kUnitAnimStateDirectSprite = 0x4;
constexpr u32 kUnitAnimStateDirectSpriteMode = 0x10;
constexpr u32 kUnitAnimStateSpecialOverlay = 0x1000;
constexpr u32 kUnitAnimStateBlendMode20 = 0x20;
constexpr u32 kUnitAnimStateBlendMode40 = 0x40;
constexpr u32 kUnitAnimStateShadowProbe = 0x40000;
constexpr u32 kUnitAnimCommandStateMirror = 0x40000000;
constexpr u32 kUnitAnimDrawMode2 = 0x2;
constexpr u32 kUnitAnimDrawMode80 = 0x80;
constexpr u32 kUnitAnimSelectionMarkerBit = 0x80000000;
constexpr u32 kUnitAnimationInvalidMarkerEntry = 0xffffffffu;
constexpr std::size_t kUnitAnimationDirectionTableCapacity = 36;
constexpr u32 kUnitAnimationPrimaryDirectionTableCount = 18;
constexpr u32 kUnitAnimationExtendedDirectionTableCount = 36;

enum class UnitAnimationSequence : u32 {
    default_idle,
    alternate_default,
    moving,
    action,
    action_fallback,
    action_fallback_extended,
    alternate_action,
    conditional_alternate_action,
    direct_action,
    queued_command,
    direct_sprite,
    direct_timed,
    moving_action_primary,
    moving_action_alternate,
    cell_base,
    cell_flag4,
    cell_flag40,
    cell_construction,
    cell_progress,
    cell_channel_additive,
    low_health_overlay,
    shadow_attachment,
    forced_highlight,
};

enum class UnitAnimationDrawKind : u32 {
    normal,
    flipped,
    highlight,
    blend_20,
    blend_40,
    mode_2,
    mode_80,
    blend_factor_0f,
    blend_factor_ramp,
    neighbor_copy,
    resource_mode,
    ally_or_local,
    channel_additive_tint,
    palette_channel_additive_tint,
    timed_channel_additive_tint,
    shadow_probe_additive_tint,
};

enum class UnitOwnerRelationTint : u32 {
    local = 0,
    enemy = 1,
    ally = 3,
};

constexpr UnitOwnerRelationTint ResolveUnitOwnerRelationTint(
    u32 local_owner_id, u32 unit_owner_id, bool allied) {
    if (local_owner_id == unit_owner_id) {
        return UnitOwnerRelationTint::local;
    }
    // Replay playback publishes owner 9 and leaves raw relation row 9 at
    // 0xffffffff. ApplyUnitOwnerRelationTint therefore takes its allied
    // palette path for every map owner, even though owner 9 is not one of the
    // eight gameplay-player rows retained by PlayerSlotRuntimeState.
    if (local_owner_id == kNoLocalPlayerSlot) {
        return UnitOwnerRelationTint::ally;
    }
    return allied ? UnitOwnerRelationTint::ally :
        UnitOwnerRelationTint::enemy;
}

struct UnitAnimationBounds {
    i32 x_offset = 0;
    i32 y_offset = 0;
    i32 width = 0;
    i32 height = 0;
};

struct UnitAnimationDefinition {
    u32 type_id = 0;
    i32 direction_offset = 0;
    i32 name_offset_x = 0;
    i32 name_offset_y = 0;
    i32 name_width = 0;
    i32 marker_offset_x = 0;
    i32 marker_offset_y = 0;
    i32 marker_width = 0;
    i32 marker_height = 0;
    i32 bars_offset_x = 0;
    i32 bars_offset_y = 0;
    i32 bars_width = 0;
    u32 owner_relation_overlay_entry_offset = 0;
    i32 owner_relation_overlay_offset_x = 0;
    i32 owner_relation_overlay_offset_y = 0;
    u32 cell_base_blit_mode = 0;
    u32 cell_flag4_blit_mode = 0;
    u32 cell_flag40_blit_mode = 0;
    bool cell_construction_special_draw = false;
    bool has_move_resource = false;
    bool has_move_resource_alt = false;
    bool has_alternate_default_resource = false;
    bool has_alternate_default_resource_alt = false;
    bool action_fallback_uses_extended_directions = false;
    bool has_queued_command_resource = false;
};

struct UnitAnimationUnit {
    u32 type_id = 0;
    u32 owner_id = 0;
    u32 runtime_slot_index = 0;
    u32 command_flags = 0;
    u32 command_bit_mask = 0;
    u32 command_value = 0;
    u32 command_state = 0;
    u32 previous_command_state = 0;
    u32 animation_flags = 0;
    u32 marker_flags = 0;
    u32 state_flags = 0;
    u32 draw_flags = 0;
    u32 runtime_flags = 0;
    u32 animation_frame = 0;
    u32 animation_timer = 0;
    u32 command_entry_lockout_ticks = 0;
    u32 command_lockout_ticks = 0;
    i32 direction = 0;
    i32 screen_x = 0;
    i32 screen_y = 0;
    u32 cargo_amount = 0;
    u32 max_hit_points = 0;
    u32 hit_points = 0;
    u32 max_secondary_value = 0;
    u32 secondary_value = 0;
    bool secondary_bar_enabled = true;
    u32 terrain_cell_flags = 0;
    u32 command_metadata_flags = 0;
    u32 definition_cell_flags = 0;
    u32 cell_animation_frame = 0;
    u32 cell_flag40_animation_frame = 0;
    u32 cell_channel_additive_frame = 0;
    u32 construction_stage_count = 0;
    u32 construction_progress = 0;
    u32 construction_progress_limit = 0;
    u32 low_health_overlay_frame = 0;
    u32 ability_id = 0;
    bool visible_to_local_owner = false;
    bool cell_construction_progress_active = false;
    bool cell_channel_additive_active = false;
    bool display_name_slot_present = false;
    std::string display_name;
};

struct UnitAnimationFrameTables {
    u32 direction_entry_count = kUnitAnimationPrimaryDirectionTableCount;
    std::array<i32, kUnitAnimationDirectionTableCapacity> direction_to_resource_row{};
    std::array<bool, kUnitAnimationDirectionTableCapacity> direction_is_flipped{};
    u32 extended_direction_entry_count = kUnitAnimationExtendedDirectionTableCount;
    std::array<i32, kUnitAnimationDirectionTableCapacity> extended_direction_to_resource_row{};
    std::array<bool, kUnitAnimationDirectionTableCapacity> extended_direction_is_flipped{};
};

struct UnitRenderColorRamps {
    std::array<u16, 6> colors{};
    std::array<i32, 32> x_offsets{};
    std::array<i32, 32> y_offsets{};
    std::array<i32, 32> secondary_offsets{};
};

struct UnitAnimationDrawCommand {
    const UnitAnimationUnit* unit = nullptr;
    UnitAnimationSequence sequence = UnitAnimationSequence::default_idle;
    UnitAnimationDrawKind kind = UnitAnimationDrawKind::normal;
    u32 resource_frame = 0;
    u32 animation_frame = 0;
    u32 direction_row = 0;
    i32 screen_x = 0;
    i32 screen_y = 0;
    bool flipped = false;
    u32 resource_draw_mode = 0;
};

struct UnitAnimationDrawContext;

using UnitAnimationDrawCallback = void (*)(UnitAnimationDrawContext& context,
    const UnitAnimationDrawCommand& command);
using UnitAnimationOverlayCallback = void (*)(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
using UnitAnimationTintCallback = void (*)(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitOwnerRelationTint tint);
using UnitAnimationHealthBarCallback = void (*)(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, i32 width);
using UnitAnimationPixelCallback = void (*)(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color);
using UnitAnimationTextCallback = void (*)(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 center_x, i32 baseline_y);
using UnitAnimationMarkerCallback = void (*)(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y);
using UnitAnimationRelationCallback = bool (*)(UnitAnimationDrawContext& context,
    u32 owner_id, u32 other_owner_id);

struct UnitAnimationCallbacks {
    UnitAnimationDrawCallback draw_sprite = nullptr;
    UnitAnimationDrawCallback draw_direct_sprite = nullptr;
    UnitAnimationDrawCallback draw_highlight_sprite = nullptr;
    UnitAnimationOverlayCallback draw_special_overlay = nullptr;
    UnitAnimationOverlayCallback draw_status_overlay = nullptr;
    UnitAnimationOverlayCallback draw_shadow_or_probe = nullptr;
    UnitAnimationOverlayCallback draw_blend_20 = nullptr;
    UnitAnimationOverlayCallback draw_blend_40 = nullptr;
    UnitAnimationOverlayCallback draw_mode_2 = nullptr;
    UnitAnimationOverlayCallback draw_mode_80 = nullptr;
    UnitAnimationOverlayCallback draw_ally_or_local_mode = nullptr;
    UnitAnimationTintCallback apply_owner_tint = nullptr;
    UnitAnimationMarkerCallback draw_marker = nullptr;
    UnitAnimationHealthBarCallback draw_health_bar = nullptr;
    UnitAnimationHealthBarCallback draw_secondary_bar = nullptr;
    UnitAnimationPixelCallback draw_bar_pixel = nullptr;
    UnitAnimationTextCallback draw_display_name = nullptr;
    UnitAnimationRelationCallback is_owner_allied = nullptr;
};

struct UnitAnimationDrawContext {
    UnitAnimationCallbacks callbacks;
    UnitAnimationFrameTables tables;
    UnitRenderColorRamps color_ramps;
    const UnitAnimationDefinition* definition = nullptr;
    const UnitAnimationUnit* current_unit = nullptr;
    u32 local_owner_id = 0;
    bool local_owner_is_observer = false;
    u32 global_frame_counter = 0;
    u32 highlight_level = 0;
    u32 selection_marker_base_entry = kUnitAnimationInvalidMarkerEntry;
    u32 current_marker_sprite_entry = kUnitAnimationInvalidMarkerEntry;
    std::array<u32, 8> owner_relation_masks{};
    bool special_overlay_resources_loaded = false;
    bool status_overlay_resources_loaded = false;
    bool use_555_color = false;
    i32 ramp_x_step = 0;
    i32 ramp_y_step = 0;
    i32 ramp_secondary_step = 0;
    i32 text_half_height = 0;
    UnitAnimationDrawCommand last_command;
    UnitOwnerRelationTint last_tint = UnitOwnerRelationTint::local;
};

u32 ResolveUnitAnimationFrame(const UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, UnitAnimationSequence sequence);
bool IsUnitAnimationDirectionFlipped(const UnitAnimationDrawContext& context,
    const UnitAnimationDefinition& definition, const UnitAnimationUnit& unit,
    UnitAnimationSequence sequence = UnitAnimationSequence::default_idle);
void ApplyUnitOwnerRelationTint(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitSelectionOrTargetMarker(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitSpecialOverlayIfEnabled(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitStatusOverlayIfEnabled(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitHealthAndSecondaryBars(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitDisplayNameIfPresent(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitAnimationFrameFlipped(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame,
    UnitAnimationSequence sequence = UnitAnimationSequence::default_idle);
void DrawUnitAnimationFrameForcedHighlight(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame,
    UnitAnimationSequence sequence = UnitAnimationSequence::default_idle);
void DrawUnitAnimationFrameSharedTail(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame,
    UnitAnimationSequence sequence = UnitAnimationSequence::default_idle);
void DrawUnitAlternateDefaultAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitDefaultAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitMovingAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitActionFallbackAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitDirectActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitQueuedCommandAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitAlternateActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitConditionalAlternateActionAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitDirectSpriteAnimationFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitPaletteRampHighlightFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitTimedPaletteHighlightFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitMovingActionPrimaryFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitMovingActionAlternateFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitAnimationFrameForcedNormalHighlight(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, u32 resource_frame,
    UnitAnimationSequence sequence = UnitAnimationSequence::default_idle);
void DispatchUnitCellResourceDraw(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitCellFlag4ResourceFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitCellFlag40ResourceFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitCellConstructionStageFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawPlacementPreviewDefinitionSprite(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitCellBaseResourceFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitCellConstructionProgressFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitCellPaletteRampFrame(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitLowHealthDamageOverlay(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitShadowAndAttachmentSprites(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitMirrorShadowResourceIfPresent(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void DrawUnitHealthBarLeftCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color);
void DrawUnitHealthBarRightCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color);
void DrawUnitSecondaryBarLeftCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color);
void DrawUnitSecondaryBarRightCapPixels(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit, i32 x, i32 y, u16 color);
void DispatchUnitAnimationDraw(UnitAnimationDrawContext& context,
    const UnitAnimationUnit& unit);
void InitializeUnitRenderColorRamps(UnitAnimationDrawContext& context);
void InitializeOriginalUnitAnimationFrameTables(UnitAnimationFrameTables& tables);

}
