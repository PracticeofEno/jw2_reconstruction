#include "ranker_unit_animation.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace ranker;

std::vector<UnitAnimationDrawCommand> g_draws;

void capture_draw(UnitAnimationDrawContext&,
    const UnitAnimationDrawCommand& command) {
    g_draws.push_back(command);
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

UnitAnimationDrawContext make_context() {
    UnitAnimationDrawContext context{};
    context.callbacks.draw_sprite = capture_draw;
    InitializeOriginalUnitAnimationFrameTables(context.tables);
    return context;
}

UnitAnimationUnit make_unit(u32 stage_count, u32 progress, u32 limit,
    u32 hit_points = 50, u32 max_hit_points = 450) {
    UnitAnimationUnit unit{};
    unit.type_id = 130;
    unit.construction_stage_count = stage_count;
    unit.construction_progress = progress;
    unit.construction_progress_limit = limit;
    unit.hit_points = hit_points;
    unit.max_hit_points = max_hit_points;
    return unit;
}

void test_multiframe_early_frame_zero_is_opaque() {
    UnitAnimationDrawContext context = make_context();
    const UnitAnimationUnit unit = make_unit(4, 50, 400);

    DrawUnitCellConstructionProgressFrame(context, unit);

    require(context.last_command.sequence == UnitAnimationSequence::cell_progress,
        "multi-frame construction must use the progress sequence");
    require(context.last_command.animation_frame == 0,
        "early 4-frame construction must select frame zero");
    require(context.last_command.kind == UnitAnimationDrawKind::normal,
        "computed frame zero of a multi-frame definition must remain opaque");
}

void test_single_frame_uses_hp_fade() {
    UnitAnimationDrawContext context = make_context();
    const UnitAnimationUnit unit = make_unit(1, 50, 400);

    DrawUnitCellConstructionProgressFrame(context, unit);

    require(context.last_command.animation_frame == 0,
        "single-frame construction must select frame zero");
    require(context.last_command.kind == UnitAnimationDrawKind::blend_factor_ramp,
        "single-frame construction below full HP must use the HP fade");
    require(context.highlight_level == 3,
        "single-frame HP fade must use floor(HP * 31 / max HP)");
}

void test_single_frame_full_hp_and_zero_duration_are_opaque() {
    UnitAnimationDrawContext full_context = make_context();
    const UnitAnimationUnit full = make_unit(1, 400, 400, 450, 450);
    DrawUnitCellConstructionProgressFrame(full_context, full);
    require(full_context.last_command.kind == UnitAnimationDrawKind::normal,
        "single-frame construction at full HP must draw normally");

    UnitAnimationDrawContext zero_duration_context = make_context();
    const UnitAnimationUnit zero_duration = make_unit(1, 0, 0);
    DrawUnitCellConstructionProgressFrame(zero_duration_context, zero_duration);
    require(zero_duration_context.last_command.kind == UnitAnimationDrawKind::normal,
        "zero-duration single-frame definition must bypass the HP fade");
}

void test_draw_mode_precedes_single_frame_hp_fade() {
    UnitAnimationDrawContext context = make_context();
    UnitAnimationUnit unit = make_unit(1, 50, 400);
    unit.draw_flags = kUnitAnimDrawMode2 | kUnitAnimDrawMode80;

    DrawUnitCellConstructionProgressFrame(context, unit);

    require(context.last_command.kind == UnitAnimationDrawKind::mode_80,
        "draw mode 2/80 must precede the single-frame HP fade");
}

void test_elf_completion_channel_additive_ramp() {
    UnitAnimationDefinition definition{};
    UnitAnimationDrawContext context = make_context();
    context.definition = &definition;
    context.use_555_color = true;
    context.ramp_x_step = 0x400;
    context.ramp_y_step = 0x20;
    context.ramp_secondary_step = 1;
    InitializeUnitRenderColorRamps(context);

    // InitializeUnitRenderColorRamps (0x004c5ef0) builds the triangular
    // 32-entry ramp consumed by the structure-completion branch at
    // 0x004c523d.  Every buildable Elf structure has one group-0 frame and
    // takes this branch for the 31 ticks following construction.
    require(context.color_ramps.x_offsets[0] == 0x800 &&
            context.color_ramps.y_offsets[0] == 0x40 &&
            context.color_ramps.secondary_offsets[0] == 2,
        "555 completion ramp did not start with the original packed steps");
    require(context.color_ramps.x_offsets[15] == 0x8000 &&
            context.color_ramps.y_offsets[15] == 0x400 &&
            context.color_ramps.secondary_offsets[15] == 32,
        "555 completion ramp peak differs from the original");
    require(context.color_ramps.x_offsets[16] == 0x7800 &&
            context.color_ramps.y_offsets[16] == 0x3c0 &&
            context.color_ramps.secondary_offsets[16] == 30 &&
            context.color_ramps.x_offsets[31] == 0 &&
            context.color_ramps.y_offsets[31] == 0 &&
            context.color_ramps.secondary_offsets[31] == 0,
        "555 completion ramp did not descend to the original terminal zero");

    UnitAnimationUnit unit{};
    unit.type_id = 117;
    unit.construction_stage_count = 1;
    unit.cell_channel_additive_active = true;
    unit.cell_channel_additive_frame = 16;
    unit.animation_flags = kUnitAnimFlagShowBars;
    unit.max_hit_points = 1800;
    unit.hit_points = 1800;

    g_draws.clear();
    DispatchUnitCellResourceDraw(context, unit);
    require(g_draws.size() == 1,
        "Elf completion glow must return before markers and status bars");
    require(g_draws.front().sequence ==
                UnitAnimationSequence::cell_channel_additive &&
            g_draws.front().animation_frame == 0 &&
            g_draws.front().kind ==
                UnitAnimationDrawKind::channel_additive_tint &&
            context.highlight_level == 16,
        "Elf completion glow did not use group-0 frame and raw +0x78 ramp index");
}

UnitAnimationSequence dispatch_sequence(UnitAnimationDrawContext& context,
    u32 state, u32 flags = 0, u32 command_value = 0) {
    UnitAnimationUnit unit{};
    unit.type_id = 7;
    unit.command_state = state;
    unit.command_flags = flags;
    unit.command_value = command_value;
    unit.direction = 15;
    g_draws.clear();
    DispatchUnitAnimationDraw(context, unit);
    require(g_draws.size() == 1,
        "mapped mobile command must emit exactly one sprite");
    return g_draws.front().sequence;
}

void test_original_mobile_animation_group_matrix() {
    UnitAnimationDefinition definition{};
    definition.has_move_resource = true;
    definition.has_move_resource_alt = true;
    definition.has_alternate_default_resource = true;
    definition.has_alternate_default_resource_alt = true;
    definition.has_queued_command_resource = true;
    UnitAnimationDrawContext context = make_context();
    context.definition = &definition;

    require(context.tables.direction_to_resource_row[15] == 4 &&
            context.tables.direction_is_flipped[15],
        "primary direction 15 must mirror row 4");
    require(context.tables.extended_direction_to_resource_row[28] == 7 &&
            context.tables.extended_direction_is_flipped[28],
        "extended direction 28 must mirror row 7");
    require(context.tables.extended_direction_to_resource_row[35] == 0 &&
            !context.tables.extended_direction_is_flipped[35],
        "extended terminal direction must use unflipped row 0");

    // PTR_LAB_004c43f0 contains 138 entries (states 0x00..0x89).  These
    // arrays are its complete grouping after following the small 0x0040xxxx
    // jump thunks to their actual render functions.
    constexpr std::array<u32, 54> kNoDrawStates{{
        0x00, 0x07, 0x0b, 0x0f, 0x13, 0x18, 0x19, 0x1a, 0x1b,
        0x24, 0x26, 0x27, 0x2e, 0x2f, 0x34, 0x3b, 0x40, 0x44,
        0x45, 0x46, 0x47, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
        0x50, 0x51, 0x52, 0x59, 0x5d, 0x5e, 0x62, 0x63, 0x67,
        0x68, 0x6b, 0x6d, 0x70, 0x71, 0x72, 0x76, 0x77, 0x7a,
        0x7b, 0x7c, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86,
    }};
    constexpr std::array<u32, 35> kDefaultStates{{
        0x01, 0x05, 0x08, 0x09, 0x0c, 0x0d, 0x10, 0x14, 0x17,
        0x1c, 0x1f, 0x23, 0x28, 0x2b, 0x30, 0x33, 0x35, 0x36,
        0x3c, 0x3d, 0x41, 0x42, 0x48, 0x53, 0x56, 0x57, 0x5a,
        0x5f, 0x64, 0x6c, 0x73, 0x74, 0x7d, 0x87, 0x88,
    }};
    constexpr std::array<u32, 28> kActionFallbackStates{{
        0x02, 0x03, 0x06, 0x0a, 0x0e, 0x12, 0x15, 0x16, 0x1e,
        0x21, 0x22, 0x25, 0x31, 0x32, 0x37, 0x38, 0x3e, 0x3f,
        0x43, 0x49, 0x55, 0x5c, 0x61, 0x66, 0x6a, 0x75, 0x7f,
        0x89,
    }};
    constexpr std::array<u32, 11> kQueuedStates{{
        0x04, 0x1d, 0x20, 0x39, 0x3a, 0x54,
        0x58, 0x5b, 0x65, 0x69, 0x7e,
    }};

    for (u32 state : kNoDrawStates) {
        UnitAnimationUnit unit{};
        unit.type_id = 7;
        unit.command_state = state;
        unit.direction = 15;
        g_draws.clear();
        DispatchUnitAnimationDraw(context, unit);
        require(g_draws.empty(),
            "original RET animation-table entry unexpectedly drew a sprite");
    }
    for (u32 state : kDefaultStates) {
        require(dispatch_sequence(context, state) ==
                UnitAnimationSequence::default_idle,
            "original default animation-table group mismatch");
    }
    for (u32 state : kActionFallbackStates) {
        require(dispatch_sequence(context, state) ==
                UnitAnimationSequence::action_fallback,
            "original action-fallback animation-table group mismatch");
    }
    for (u32 state : kQueuedStates) {
        require(dispatch_sequence(context, state) ==
                UnitAnimationSequence::queued_command,
            "original queued-command animation-table group mismatch");
    }

    require(dispatch_sequence(context, 0x11) ==
            UnitAnimationSequence::alternate_action,
        "alternate action table entry mismatch");
    require(dispatch_sequence(context, 0x29, 0x01) ==
            UnitAnimationSequence::conditional_alternate_action,
        "reserved berry harvesting table entry mismatch");
    require(dispatch_sequence(context, 0x2a) ==
            UnitAnimationSequence::action &&
            dispatch_sequence(context, 0x2c) ==
            UnitAnimationSequence::action,
        "berry/dropoff action table entries mismatch");
    require(dispatch_sequence(context, 0x2d) ==
            UnitAnimationSequence::moving,
        "failed/retry berry moving table entry mismatch");
    require(dispatch_sequence(context, 0x60, 0, 1) ==
            UnitAnimationSequence::default_idle &&
            g_draws.front().kind ==
                UnitAnimationDrawKind::palette_channel_additive_tint,
        "palette-ramp animation table entry mismatch");
    require(dispatch_sequence(context, 0x6e) ==
            UnitAnimationSequence::direct_timed &&
            dispatch_sequence(context, 0x6f) ==
            UnitAnimationSequence::direct_timed,
        "timed palette animation table entries mismatch");
    require(dispatch_sequence(context, 0x78) ==
            UnitAnimationSequence::moving_action_primary,
        "moving action primary table entry mismatch");
    require(dispatch_sequence(context, 0x79) ==
            UnitAnimationSequence::moving_action_alternate,
        "moving action alternate table entry mismatch");
}

void test_structure_damage_overlay_frame() {
    UnitAnimationDefinition definition{};
    UnitAnimationDrawContext context = make_context();
    context.definition = &definition;
    UnitAnimationUnit structure{};
    structure.type_id = 130;
    structure.max_hit_points = 450;
    structure.hit_points = 200;
    structure.low_health_overlay_frame = 3;

    g_draws.clear();
    DispatchUnitCellResourceDraw(context, structure);
    require(g_draws.size() >= 2,
        "damaged structure must draw its base and damage overlay");
    require(g_draws.back().sequence == UnitAnimationSequence::low_health_overlay &&
            g_draws.back().animation_frame == 45,
        "structure damage severity did not select original overlay frame 45");
}

} // namespace

int main() {
    test_multiframe_early_frame_zero_is_opaque();
    test_single_frame_uses_hp_fade();
    test_single_frame_full_hp_and_zero_duration_are_opaque();
    test_draw_mode_precedes_single_frame_hp_fade();
    test_elf_completion_channel_additive_ramp();
    test_original_mobile_animation_group_matrix();
    test_structure_damage_overlay_frame();
    std::cout << "CONSTRUCTION_PROGRESS_FRAME_PARITY_PASS "
                 "animation-table=138 berry-actions damage-overlay directions\n";
    return EXIT_SUCCESS;
}
