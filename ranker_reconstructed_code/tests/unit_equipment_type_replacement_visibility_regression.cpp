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
    // Original 0x00410300 writes the replacement id at 0x00410559, then
    // reaches the command-flag check without any raw +0x58 store.  A
    // replacement definition must therefore not inject bit 0x20 (or any
    // other capability flag) into the live unit.
    if (UnitEquipmentReplacementRuntimeTypeFlags(0x2213u, 0x2233u) !=
        0x2213u) {
        fail("type replacement overwrote live runtime type flags");
    }

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
