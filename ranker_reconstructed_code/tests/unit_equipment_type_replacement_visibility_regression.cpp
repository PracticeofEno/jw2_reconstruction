#include "ranker_unit_equipment.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* message) {
    std::cerr << "UNIT_EQUIPMENT_TYPE_REPLACEMENT_VISIBILITY_FAIL "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    // The incident's primitive upgrade type has type_flags bit 0x2 while its
    // original definition +0x1f8 word and all six equipment slots are zero.
    // Original ranker.exe therefore preserves command_flags 0x28.  The
    // 0x00042233 type-flags value is intentionally absent from this API: it
    // must never influence the replacement visibility decision.
    if (ShouldSetUnitEquipmentReplacementCommandFlag(0, 0)) {
        fail("zero definition/equipment state published command flag 0x40");
    }

    // Definition +0x1f8 bit 0x2 remains the real original cloaking/footprint
    // source and must still publish command flag 0x40.
    if (!ShouldSetUnitEquipmentReplacementCommandFlag(0x2u, 0)) {
        fail("definition +0x1f8 bit 0x2 did not publish command flag 0x40");
    }

    if (!ShouldSetUnitEquipmentReplacementCommandFlag(0, 1)) {
        fail("equipment command-flag modifier was ignored");
    }

    std::cout << "UNIT_EQUIPMENT_TYPE_REPLACEMENT_VISIBILITY_PASS\n";
    return EXIT_SUCCESS;
}
