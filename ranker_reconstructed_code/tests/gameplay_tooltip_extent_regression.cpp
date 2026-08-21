#include "ranker_gameplay_tooltips.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace ranker;

    static_assert(GameplayTooltipCostTokenAdvance(0) == 20);
    static_assert(GameplayTooltipCostTokenAdvance(8) == 28);

    constexpr u32 food_digits = 16;
    constexpr u32 wood_digits = 16;
    constexpr u32 population_digits = 8;
    const u32 rendered_row_width =
        GameplayTooltipCostTokenAdvance(food_digits) +
        GameplayTooltipCostTokenAdvance(wood_digits) +
        GameplayTooltipCostTokenAdvance(population_digits);
    const u32 legacy_fixed_width = 3 * 0x14;

    require(rendered_row_width == 100,
        "cost row width must include every rendered numeric extent");
    require(rendered_row_width > legacy_fixed_width,
        "population cost regression must expose the legacy clipping gap");

    std::cout << "gameplay tooltip extent regression: PASS\n";
    return EXIT_SUCCESS;
}
