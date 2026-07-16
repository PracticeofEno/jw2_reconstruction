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
                 "source=alternate_flags bit=0x20000000\n";
    return EXIT_SUCCESS;
}
