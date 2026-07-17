#include "ranker_unit_commands.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "OWNER_STRATEGIC_PATH_LAYER_FAIL " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace ranker;

    UnitMovementUnit ground_probe{};
    ground_probe.id = 49;
    ground_probe.definition.movement_class = 0;
    ground_probe.definition.lifecycle_class = 7;

    UnitMovementUnit water_probe{};
    water_probe.id = 48;
    water_probe.definition.movement_class = 2;
    water_probe.definition.lifecycle_class = 0;

    UnitMovementContext movement{};
    movement.active_units = {&water_probe, nullptr, &ground_probe};
    require(SelectOwnerStrategicPathProbeUnit(movement) == &ground_probe,
        "probe selection used lifecycle_class instead of preferring movement class zero");

    UnitMovementUnit blocked_probe{};
    blocked_probe.id = 47;
    blocked_probe.definition.movement_class = 4;
    movement.active_units = {&blocked_probe, &water_probe, &ground_probe};
    require(SelectOwnerStrategicPathProbeUnit(movement) == &ground_probe,
        "first pass did not skip an earlier class two probe for class zero");

    UnitMovementUnit air_probe{};
    air_probe.id = 50;
    air_probe.definition.movement_class = 3;
    movement.active_units = {&blocked_probe, &air_probe, &water_probe};
    require(SelectOwnerStrategicPathProbeUnit(movement) == &air_probe,
        "fallback pass did not preserve active-list order for classes two and three");

    movement.active_units = {nullptr, &blocked_probe};
    require(SelectOwnerStrategicPathProbeUnit(movement) == nullptr,
        "probe selection accepted a movement class outside zero, two and three");

    UnitMovementCell cell{};
    cell.flags = 0x20000000u;
    require(!CheckOwnerStrategicPathWindowTileOpen(cell),
        "terrain-layer bit 29 was treated as the source-grid open bit");

    cell.flags = 0;
    cell.alternate_flags = 0x20000000u;
    require(CheckOwnerStrategicPathWindowTileOpen(cell),
        "DAT_00e99e74 source-layer bit 29 was not treated as open");

    cell.alternate_flags = 0x40000000u;
    require(!CheckOwnerStrategicPathWindowTileOpen(cell),
        "source-layer bit 30 was confused with the bit-29 open flag");

    std::cout << "OWNER_STRATEGIC_PATH_LAYER_PASS "
                 "source=alternate_flags bit=0x20000000 "
                 "probe=movement_class(0->2/3)\n";
    return EXIT_SUCCESS;
}
