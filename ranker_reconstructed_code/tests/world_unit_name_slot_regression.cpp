#include "ranker_unit_animation.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace ranker;

u32 g_draw_count = 0;
std::string g_last_name;
i32 g_last_center_x = 0;
i32 g_last_baseline_y = 0;

void capture_name(UnitAnimationDrawContext&,
    const UnitAnimationUnit& unit, i32 center_x, i32 baseline_y) {
    ++g_draw_count;
    g_last_name = unit.display_name;
    g_last_center_x = center_x;
    g_last_baseline_y = baseline_y;
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_world_name_tail_gates_on_raw_slot_presence() {
    UnitAnimationDefinition definition{};
    definition.name_offset_x = 4;
    definition.name_offset_y = 17;
    definition.name_width = 20;

    UnitAnimationDrawContext context{};
    context.definition = &definition;
    context.text_half_height = 6;
    context.callbacks.draw_display_name = capture_name;

    UnitAnimationUnit unit{};
    unit.screen_x = 100;
    unit.screen_y = 200;
    DrawUnitDisplayNameIfPresent(context, unit);
    require(g_draw_count == 0,
        "raw string slot zero must suppress the world-name tail");

    unit.display_name_slot_present = true;
    DrawUnitDisplayNameIfPresent(context, unit);
    require(g_draw_count == 1 && g_last_name.empty(),
        "nonzero empty string slot must still execute the world-name draw");
    require(g_last_center_x == 114 && g_last_baseline_y == 214,
        "empty slot must preserve original name centering coordinates");

    unit.display_name = "Rex";
    DrawUnitDisplayNameIfPresent(context, unit);
    require(g_draw_count == 2 && g_last_name == "Rex",
        "nonempty string slot must preserve ordinary world-name drawing");
}

} // namespace

int main() {
    test_world_name_tail_gates_on_raw_slot_presence();
    std::cout << "WORLD_UNIT_NAME_SLOT_PASS gate=raw+0x48 empty=draw "
                 "coordinates=definition-bounds\n";
    return EXIT_SUCCESS;
}
