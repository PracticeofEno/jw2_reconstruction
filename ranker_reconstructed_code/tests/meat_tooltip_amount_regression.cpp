#include "ranker_gameplay_tooltips.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "MEAT_TOOLTIP_AMOUNT_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_original_tier_boundaries() {
    struct Case {
        u32 amount;
        u32 tier;
    };
    constexpr Case cases[] = {
        {0, 0},
        {1, 1},
        {100, 1},
        {101, 2},
        {500, 2},
        {501, 3},
        {1000, 3},
        {1001, 4},
        {0xffffffffu, 4},
    };
    for (const Case& test : cases) {
        require(GameplayTooltipMeatTierForAmount(test.amount) == test.tier,
            "raw +0x2c amount selected the wrong tooltip tier");
    }
}

void test_selected_unit_adapter_uses_meat_not_experience() {
    UnitMovementUnit unit{};
    unit.id = 0x1234;
    unit.type_id = 0x20;
    unit.owner_id = 3;
    unit.area_marker_flags = 0x80000000u;
    unit.action_mode = 501;
    unit.elite_progress_value = 0xffffffffu;
    unit.equipment_slots = {11, 12, 13, 14, 15, 16};

    const GameplayTooltipSelectedUnitState selected =
        BuildGameplayTooltipSelectedUnitState(unit);
    require(selected.offset == unit.id && selected.type == unit.type_id &&
            selected.owner == unit.owner_id &&
            selected.area_marker_flags == unit.area_marker_flags,
        "selected-unit identity fields were not copied");
    require(selected.meat_amount == 501 &&
            GameplayTooltipMeatTierForAmount(selected.meat_amount) == 3,
        "tooltip adapter used experience instead of carried meat");
    require(selected.equipment_slots == unit.equipment_slots,
        "selected-unit equipment slots were not copied");

    unit.action_mode = 0;
    unit.elite_progress_value = 1001;
    const GameplayTooltipSelectedUnitState no_meat =
        BuildGameplayTooltipSelectedUnitState(unit);
    require(no_meat.meat_amount == 0 &&
            GameplayTooltipMeatTierForAmount(no_meat.meat_amount) == 0,
        "experience alone made a carried-meat tooltip visible");
}

} // namespace

int main() {
    test_original_tier_boundaries();
    test_selected_unit_adapter_uses_meat_not_experience();
    std::cout << "MEAT_TOOLTIP_AMOUNT_PASS "
                 "source=raw+0x2c thresholds=1/101/501/1001 "
                 "experience=ignored\n";
    return EXIT_SUCCESS;
}
