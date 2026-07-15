#include "ranker_unit_animation.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

UnitAnimationDrawContext make_context() {
    UnitAnimationDrawContext context{};
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

} // namespace

int main() {
    test_multiframe_early_frame_zero_is_opaque();
    test_single_frame_uses_hp_fade();
    test_single_frame_full_hp_and_zero_duration_are_opaque();
    test_draw_mode_precedes_single_frame_hp_fade();
    std::cout << "CONSTRUCTION_PROGRESS_FRAME_PARITY_PASS\n";
    return EXIT_SUCCESS;
}
