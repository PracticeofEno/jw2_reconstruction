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
    static_assert(GameplayTooltipWorldCoordinateFromScreen(
        320, 400, 800, 800) == 720);
    static_assert(GameplayTooltipWorldCoordinateFromScreen(
        320, 400, 800, 1000) == 820);
    static_assert(GameplayTooltipWorldCoordinateFromScreen(
        640, 300, 600, 750) == 1015);

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

    GameplayTooltipState wider_view{};
    wider_view.screen_width = 800;
    wider_view.screen_height = 600;
    wider_view.world_view_width = 1000;
    wider_view.world_view_height = 750;
    wider_view.camera_x = 320;
    wider_view.camera_y = 640;
    wider_view.cursor_x = 400;
    wider_view.cursor_y = 300;
    wider_view.map_width_tiles = 64;
    wider_view.map_height_tiles = 64;
    wider_view.terrain_flags.assign(64u * 64u, 0);
    constexpr u32 expected_berry_amount = 321;
    constexpr u32 berry_tile_x = 820u >> 5;
    constexpr u32 berry_tile_y = 1015u >> 5;
    wider_view.terrain_flags[berry_tile_y * 64u + berry_tile_x] =
        (expected_berry_amount << 12) | 0x100u;
    require(GameplayTooltipTerrainResourceAmount(wider_view) ==
            expected_berry_amount,
        "wider-view Berry tooltip read the unscaled screen-coordinate tile");

    std::cout << "gameplay tooltip extent regression: PASS\n";
    return EXIT_SUCCESS;
}
