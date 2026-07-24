#include "ranker_unit_commands.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "EVIL_PORTAL_RELEASE_FAIL: " << message << '\n';
        std::exit(1);
    }
}

UnitMovementCell& cell(UnitMovementMap& map, u32 x, u32 y) {
    return map.cells[static_cast<std::size_t>(y) * map.stride_tiles + x];
}

void test_transient_overlay_recovers_original_exit() {
    UnitMovementContext movement;
    movement.map.width = 96;
    movement.map.height = 96;
    movement.map.stride_tiles = 256;
    movement.map.cells.resize(96 * 256);

    UnitMovementUnit builder;
    builder.type_id = 48;
    builder.definition.movement_class = 2;
    builder.active = true;

    UnitMovementUnit portal;
    portal.type_id = 155;
    portal.x = 2528;
    portal.y = 2592;
    portal.active = true;
    portal.footprint_registered = true;
    portal.definition.footprint_width_tiles = 4;
    portal.definition.footprint_height_tiles = 3;

    cell(movement.map, 79, 81).alternate_flags = 0xa0664924u;
    UnitMovementCell& exit = cell(movement.map, 80, 84);
    exit.alternate_flags = 0xa1264914u;
    exit.visibility_flags = 0x38080000u; // transient raw 0x20000000 overlay
    movement.active_units = {&portal, &builder};

    UnitCommandContext context;
    context.movement = &movement;
    UnitMovementPoint resolved{2528, 2688};
    require(RecoverLegacyEvilPortalReleasePoint(context, builder, portal,
                {2589, 2709}, resolved),
        "transient overlay did not recover requested exit");
    require(resolved.x == 2560 && resolved.y == 2688,
        "recovered exit is not the original aligned tile");
}

void test_registered_structure_keeps_fallback() {
    UnitMovementContext movement;
    movement.map.width = 96;
    movement.map.height = 96;
    movement.map.stride_tiles = 256;
    movement.map.cells.resize(96 * 256);

    UnitMovementUnit builder;
    builder.type_id = 48;
    builder.definition.movement_class = 2;
    builder.active = true;

    UnitMovementUnit portal;
    portal.type_id = 155;
    portal.x = 2528;
    portal.y = 2592;
    portal.active = true;
    portal.footprint_registered = true;
    portal.definition.footprint_width_tiles = 4;
    portal.definition.footprint_height_tiles = 3;

    UnitMovementUnit blocker;
    blocker.type_id = 144;
    blocker.x = 2560;
    blocker.y = 2688;
    blocker.active = true;
    blocker.footprint_registered = true;
    blocker.definition.footprint_width_tiles = 1;
    blocker.definition.footprint_height_tiles = 1;

    cell(movement.map, 79, 81).alternate_flags = 0xa0664924u;
    UnitMovementCell& exit = cell(movement.map, 80, 84);
    exit.alternate_flags = 0xa1264914u;
    exit.visibility_flags = 0x38080000u;
    movement.active_units = {&portal, &blocker, &builder};

    UnitCommandContext context;
    context.movement = &movement;
    UnitMovementPoint resolved{2528, 2688};
    require(!RecoverLegacyEvilPortalReleasePoint(context, builder, portal,
                {2589, 2709}, resolved),
        "registered structure footprint was bypassed");
    require(resolved.x == 2528 && resolved.y == 2688,
        "blocked exit changed the fallback point");
}

} // namespace

int main() {
    test_transient_overlay_recovers_original_exit();
    test_registered_structure_keeps_fallback();
    std::cout << "evil portal construction release regression: PASS\n";
}
