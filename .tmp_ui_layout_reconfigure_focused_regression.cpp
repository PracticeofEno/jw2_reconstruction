#include "ranker_ui_overlay.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool same_rect(const UiOverlayRect& a, const UiOverlayRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width &&
        a.height == b.height;
}

void poison_reconfigured_fields(UiOverlayState& state) {
    state.camera_max_x = 11;
    state.camera_max_y = 12;
    state.minimap.output_x = 13;
    state.minimap.output_y = 14;
    state.minimap.minimap_width_pixels = 15;
    state.minimap.minimap_height_pixels = 16;
    state.minimap.viewport_width_pixels = 17;
    state.minimap.viewport_height_pixels = 18;
    state.minimap_hidden_color = 0x7777;
    state.resource_counter_x = 19;
    state.resource_counter_y = 20;
    state.population_counter_x = 21;
    state.population_counter_y = 22;
    state.small_slot1_bounds = {23, 24, 25, 26};
    state.large_slot0_bounds = {27, 28, 29, 30};
    state.dynamic_icon_bounds.fill({31, 32, 33, 34});
    state.command_slot_bounds.fill({35, 36, 37, 38});
    state.side_slot_bounds.fill({39, 40, 41, 42});
    state.indexed_slot_bounds.assign(1, {43, 44, 45, 46});
    state.wide_slot_bounds = {47, 48, 49, 50};
    state.manual_equipment_slot_bounds.fill({51, 52, 53, 54});
}

void require_manual_slots(const UiOverlayState& state) {
    constexpr std::array<UiOverlayRect, 7> expected{{
        {266, 523, 38, 38}, {175, 570, 19, 19},
        {197, 570, 19, 19}, {223, 570, 19, 19},
        {245, 570, 19, 19}, {267, 570, 19, 19},
        {289, 570, 19, 19},
    }};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require(same_rect(state.manual_equipment_slot_bounds[i], expected[i]),
            "manual slot table retained stale state");
    }
}

void require_common_rewrites(const UiOverlayState& state) {
    require(state.minimap_hidden_color == 0,
        "FUN_004e2bb7 hidden minimap color must be rewritten to zero");
    require(state.resource_counter_x == static_cast<i32>(state.screen_width) - 0x8c &&
            state.resource_counter_y == 5,
        "resource counter anchor retained stale state");
    require(state.population_counter_x == static_cast<i32>(state.screen_width) - 0x46 &&
            state.population_counter_y == 5,
        "population counter anchor retained stale state");
    require_manual_slots(state);
    require(same_rect(state.wide_slot_bounds, {19, 518, 50, 50}),
        "wide slot retained stale state");
    require(state.indexed_slot_bounds.size() == 5,
        "indexed slot table retained stale vector shape");
    require(state.command_slot_bounds[6].width == 0 &&
            state.command_slot_bounds[7].width == 0,
        "unused command slots must be cleared on every configure");
}

void test_reuse_across_buckets_and_themes() {
    UiOverlayState state{};
    state.map_width_tiles = 128;
    state.map_height_tiles = 96;

    state.screen_width = 800;
    state.screen_height = 600;
    state.interface_theme_index = 0;
    ConfigureGameplayUiOverlayLayout(state);
    require(state.screen_layout_bucket == 1 &&
            state.world_viewport_height == 439 &&
            state.minimap_camera_anchor_x == 400 &&
            state.minimap_camera_anchor_y == 219,
        "initial 800/theme0 camera layout mismatch");
    require(state.minimap.output_x == 329 && state.minimap.output_y == 480 &&
            state.minimap.minimap_width_pixels == 115 &&
            state.minimap.minimap_height_pixels == 96,
        "initial 800 minimap table/clamp mismatch");
    require(state.camera_max_x == 3296 && state.camera_max_y == 2576,
        "initial 800 camera bounds mismatch");
    require_common_rewrites(state);

    poison_reconfigured_fields(state);
    state.screen_width = 640;
    state.screen_height = 480;
    state.interface_theme_index = 3;
    ConfigureGameplayUiOverlayLayout(state);
    require(state.screen_layout_bucket == 0 &&
            state.world_viewport_height == 364 &&
            state.minimap_camera_anchor_x == 320 &&
            state.minimap_camera_anchor_y == 182,
        "800->640/theme3 camera layout retained stale state");
    require(state.minimap.output_x == 258 && state.minimap.output_y == 376 &&
            state.minimap.minimap_width_pixels == 95 &&
            state.minimap.minimap_height_pixels == 95,
        "800->640 minimap origin/extent retained stale state");
    require(state.minimap.viewport_width_pixels == 640 &&
            state.minimap.viewport_height_pixels == 480,
        "800->640 minimap viewport retained stale dimensions");
    require(state.camera_max_x == 3456 && state.camera_max_y == 2592,
        "800->640 camera bounds retained stale values");
    require(state.small_slot1_x == 364 && state.small_slot1_y == 372 &&
            same_rect(state.small_slot1_bounds, {364, 372, 65, 26}),
        "800->640 small fixed slot retained stale state");
    require(same_rect(state.dynamic_icon_bounds[0], {370, 407, 38, 38}) &&
            same_rect(state.side_slot_bounds[13], {210, 440, 38, 38}),
        "800->640 dynamic/side slot tables retained stale state");
    require_common_rewrites(state);

    poison_reconfigured_fields(state);
    state.screen_width = 1024;
    state.screen_height = 768;
    state.interface_theme_index = 1;
    state.map_width_tiles = 80;
    state.map_height_tiles = 64;
    ConfigureGameplayUiOverlayLayout(state);
    require(state.screen_layout_bucket == 2 &&
            state.world_viewport_height == 458 &&
            state.minimap_camera_anchor_x == 512 &&
            state.minimap_camera_anchor_y == 229,
        "640->other/theme1 camera layout retained stale state");
    require(state.minimap.output_x == 347 && state.minimap.output_y == 500 &&
            state.minimap.minimap_width_pixels == 80 &&
            state.minimap.minimap_height_pixels == 64,
        "small-map minimap clamp/0x74 centering mismatch");
    require(state.minimap.viewport_width_pixels == 1024 &&
            state.minimap.viewport_height_pixels == 768,
        "640->other minimap viewport retained stale dimensions");
    require(state.camera_max_x == 1536 && state.camera_max_y == 1561,
        "640->other camera bounds retained stale values");
    require(state.small_slot1_x == 396 && state.small_slot1_y == 641 &&
            same_rect(state.small_slot1_bounds, {401, 652, 50, 17}),
        "other/theme1 fixed slot retained stale state");
    require(same_rect(state.side_slot_bounds[0], {12, 73, 38, 38}) &&
            same_rect(state.side_slot_bounds[1], {12, 40, 38, 38}),
        "other-layout side slot vertical order mismatch");
    require_common_rewrites(state);

    poison_reconfigured_fields(state);
    state.interface_theme_index = 3;
    ConfigureGameplayUiOverlayLayout(state);
    require(state.world_viewport_height == 460 &&
            state.minimap_camera_anchor_y == 230 &&
            state.camera_max_y == 1568,
        "same-bucket theme change retained stale camera fields");
    require(state.small_slot1_x == 396 && state.small_slot1_y == 643 &&
            same_rect(state.small_slot1_bounds, {397, 650, 58, 19}),
        "same-bucket theme change retained stale fixed-slot fields");
    require_common_rewrites(state);
}

}  // namespace

int main() {
    test_reuse_across_buckets_and_themes();
    std::cout << "UI_LAYOUT_RECONFIGURE_FOCUSED_PASS transitions="
        "800t0->640t3->other-t1->other-t3\n";
    return EXIT_SUCCESS;
}
