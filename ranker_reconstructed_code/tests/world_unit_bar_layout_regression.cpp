#include "ranker_unit_animation.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace ranker;

enum class BarKind {
    health,
    secondary,
};

struct BarCall {
    BarKind kind = BarKind::health;
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
};

std::vector<BarCall> g_calls;

void capture_health(UnitAnimationDrawContext&, const UnitAnimationUnit&,
    i32 x, i32 y, i32 width) {
    g_calls.push_back({BarKind::health, x, y, width});
}

void capture_secondary(UnitAnimationDrawContext&, const UnitAnimationUnit&,
    i32 x, i32 y, i32 width) {
    g_calls.push_back({BarKind::secondary, x, y, width});
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "WORLD_UNIT_BAR_LAYOUT_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

UnitAnimationDrawContext make_context(const UnitAnimationDefinition& definition) {
    UnitAnimationDrawContext context{};
    context.definition = &definition;
    context.local_owner_id = 3;
    context.callbacks.draw_health_bar = capture_health;
    context.callbacks.draw_secondary_bar = capture_secondary;
    return context;
}

UnitAnimationUnit make_unit() {
    UnitAnimationUnit unit{};
    unit.owner_id = 3;
    unit.screen_x = 100;
    unit.screen_y = 200;
    unit.max_hit_points = 450;
    unit.hit_points = 225;
    unit.max_secondary_value = 80;
    unit.secondary_value = 40;
    unit.secondary_bar_enabled = true;
    return unit;
}

void test_definition_bar_bounds_and_secondary_row() {
    UnitAnimationDefinition definition{};
    // Original DrawUnitHealthAndSecondaryBars (0x004c5c7d) reads the
    // dedicated definition fields +0x350/+0x354/+0x358.  These deliberately
    // differ from the unit's name/selection rectangle in this regression.
    definition.name_offset_x = -40;
    definition.name_offset_y = -30;
    definition.name_width = 99;
    definition.bars_offset_x = -13;
    definition.bars_offset_y = 27;
    definition.bars_width = 31;

    UnitAnimationDrawContext context = make_context(definition);
    UnitAnimationUnit unit = make_unit();
    g_calls.clear();
    DrawUnitHealthAndSecondaryBars(context, unit);

    require(g_calls.size() == 2,
        "local unit with both maxima must draw two bars");
    require(g_calls[0].kind == BarKind::health &&
            g_calls[0].x == 87 && g_calls[0].y == 227 &&
            g_calls[0].width == 32,
        "health bar did not use dedicated definition bounds and width+1");
    require(g_calls[1].kind == BarKind::secondary &&
            g_calls[1].x == 87 && g_calls[1].y == 231 &&
            g_calls[1].width == 32,
        "secondary bar did not use the original four-pixel row offset");
}

void test_zero_health_max_keeps_secondary_on_the_first_row() {
    UnitAnimationDefinition definition{};
    definition.bars_offset_x = 2;
    definition.bars_offset_y = 5;
    definition.bars_width = 20;
    UnitAnimationDrawContext context = make_context(definition);
    UnitAnimationUnit unit = make_unit();
    unit.max_hit_points = 0;

    g_calls.clear();
    DrawUnitHealthAndSecondaryBars(context, unit);

    require(g_calls.size() == 2,
        "zero-HP-maximum unit must retain the original empty health shell");
    require(g_calls[0].y == 205 && g_calls[1].y == 205,
        "zero HP maximum must not advance the secondary-bar row");
}

void test_secondary_bar_owner_and_base_maximum_gates() {
    UnitAnimationDefinition definition{};
    UnitAnimationDrawContext context = make_context(definition);
    UnitAnimationUnit unit = make_unit();

    unit.owner_id = 2;
    g_calls.clear();
    DrawUnitHealthAndSecondaryBars(context, unit);
    require(g_calls.size() == 1 && g_calls[0].kind == BarKind::health,
        "non-local unit unexpectedly drew the secondary bar");

    unit.owner_id = context.local_owner_id;
    unit.secondary_bar_enabled = false;
    g_calls.clear();
    DrawUnitHealthAndSecondaryBars(context, unit);
    require(g_calls.size() == 1 && g_calls[0].kind == BarKind::health,
        "zero base secondary maximum unexpectedly drew an upgrade-only bar");

    unit.secondary_bar_enabled = true;
    unit.max_secondary_value = 0;
    g_calls.clear();
    DrawUnitHealthAndSecondaryBars(context, unit);
    require(g_calls.size() == 1 && g_calls[0].kind == BarKind::health,
        "zero effective secondary maximum unexpectedly drew a bar");
}

} // namespace

int main() {
    test_definition_bar_bounds_and_secondary_row();
    test_zero_health_max_keeps_secondary_on_the_first_row();
    test_secondary_bar_owner_and_base_maximum_gates();
    std::cout << "WORLD_UNIT_BAR_LAYOUT_PASS dedicated=350/354/358 "
                 "width=plus1 secondary-row=plus4 owner-gate\n";
    return EXIT_SUCCESS;
}
