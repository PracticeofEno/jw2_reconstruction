#include "ranker_ui_overlay.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "SELECTED_UNIT_NAME_SLOT_FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using ranker::UsesSelectedUnitDynamicNameSlot;

    constexpr std::size_t kSlotCount = 0x100;
    require(!UsesSelectedUnitDynamicNameSlot(false, 1, kSlotCount),
        "a missing movement table selected a dynamic name");
    require(!UsesSelectedUnitDynamicNameSlot(true, 0, kSlotCount),
        "slot zero suppressed the definition name");
    require(UsesSelectedUnitDynamicNameSlot(true, 1, kSlotCount),
        "a valid empty dynamic slot fell back to the definition name");
    require(UsesSelectedUnitDynamicNameSlot(true, kSlotCount - 1, kSlotCount),
        "the last valid dynamic slot was rejected");
    require(!UsesSelectedUnitDynamicNameSlot(true, kSlotCount, kSlotCount),
        "an out-of-range dynamic slot suppressed the definition name");

    std::cout << "SELECTED_UNIT_NAME_SLOT_PASS empty-slot=dynamic "
                 "slot0=definition out-of-range=definition\n";
    return 0;
}
