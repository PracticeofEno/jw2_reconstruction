#include "ranker_game_session_tables.h"
#include "ranker_ui_overlay.h"

#include <cstdlib>
#include <cstring>
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
    using ranker::ReadSessionUnitDefinitionNameField;
    using ranker::RuntimeDefinitionRecord;
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

    RuntimeDefinitionRecord absent_record{};
    require(!ReadSessionUnitDefinitionNameField(absent_record).present,
        "an absent session definition acquired an empty name override");

    RuntimeDefinitionRecord empty_name_record{};
    empty_name_record.bytes.resize(
        ranker::kSessionUnitDefinitionNameOffset +
        ranker::kSessionUnitDefinitionNameBytes);
    const auto empty_name =
        ReadSessionUnitDefinitionNameField(empty_name_record);
    require(empty_name.present && empty_name.text.empty(),
        "a present empty session name fell back to the auxiliary table");

    constexpr char kDefinitionName[] = "TYRANO";
    std::memcpy(empty_name_record.bytes.data() +
            ranker::kSessionUnitDefinitionNameOffset,
        kDefinitionName, sizeof(kDefinitionName));
    const auto populated_name =
        ReadSessionUnitDefinitionNameField(empty_name_record);
    require(populated_name.present && populated_name.text == "TYRANO",
        "a populated session definition name was not preserved");

    std::cout << "SELECTED_UNIT_NAME_SLOT_PASS empty-slot=dynamic "
                 "empty-definition=blank absent-definition=fallback\n";
    return 0;
}
