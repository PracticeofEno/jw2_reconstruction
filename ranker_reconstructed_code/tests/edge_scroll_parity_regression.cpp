#include "ranker_input.h"
#include "ranker_ui_overlay.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace ranker {

bool SurfacePixelMode555() {
    return false;
}

} // namespace ranker

namespace {
using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "EDGE_SCROLL_PARITY_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

UiOverlayState make_state() {
    UiOverlayState state{};
    state.screen_width = 800;
    state.screen_height = 600;
    state.camera_x = 400;
    state.camera_y = 400;
    state.camera_max_x = 2000;
    state.camera_max_y = 2000;
    state.mouse_x = 400;
    state.mouse_y = 300;
    state.camera_edge_pointer_valid = true;
    return state;
}

void apply_set1_camera_directions(
    UiOverlayState& state, const InputState& input) {
    const InputCameraDirectionState directions =
        ResolveSet1CameraDirectionState(input);
    state.camera_left_key_down = directions.left;
    state.camera_right_key_down = directions.right;
    state.camera_up_key_down = directions.up;
    state.camera_down_key_down = directions.down;
}

void test_set1_arrow_table_drives_camera() {
    struct DirectionCase {
        u32 scan;
        i32 x_sign;
        i32 y_sign;
    };
    constexpr std::array<DirectionCase, 4> cases{{
        {kSet1ScanArrowLeft, -1, 0},
        {kSet1ScanArrowRight, 1, 0},
        {kSet1ScanArrowUp, 0, -1},
        {kSet1ScanArrowDown, 0, 1},
    }};

    for (const DirectionCase& direction : cases) {
        InputState input{};
        input.set1_scan_down[direction.scan] = 1;
        UiOverlayState state = make_state();
        state.camera_edge_pointer_valid = false;
        apply_set1_camera_directions(state, input);
        const i32 old_x = state.camera_x;
        const i32 old_y = state.camera_y;
        ScrollCameraFromEdgeOrKeys(state);
        require((state.camera_x - old_x) * direction.x_sign > 0 ||
                direction.x_sign == 0,
            "set-1 held scan moved the camera on the wrong horizontal axis");
        require((state.camera_y - old_y) * direction.y_sign > 0 ||
                direction.y_sign == 0,
            "set-1 held scan moved the camera on the wrong vertical axis");

        input.set1_scan_down[direction.scan] = 0;
        UiOverlayState released = make_state();
        released.camera_edge_pointer_valid = false;
        apply_set1_camera_directions(released, input);
        ScrollCameraFromEdgeOrKeys(released);
        require(released.camera_x == 400 && released.camera_y == 400,
            "released set-1 direction remained held by the camera path");
    }
}

void test_exact_edges_and_cursor_masks() {
    UiOverlayState right = make_state();
    right.mouse_x = 799;
    const i32 old_right = right.camera_x;
    ScrollCameraFromEdgeOrKeys(right);
    require(right.camera_x > old_right && right.camera_scroll_dirty &&
            right.camera_edge_cursor_index == 2,
        "rightmost client pixel must move with cursor index 2");

    UiOverlayState outside = make_state();
    outside.mouse_x = 800;
    ScrollCameraFromEdgeOrKeys(outside);
    require(outside.camera_x == 400 && !outside.camera_scroll_dirty &&
            outside.camera_edge_cursor_index == 0,
        "outside captured coordinate must not count as an edge");

    UiOverlayState top_left = make_state();
    top_left.mouse_x = 0;
    top_left.mouse_y = 0;
    ScrollCameraFromEdgeOrKeys(top_left);
    require(top_left.camera_x < 400 && top_left.camera_y < 400 &&
            top_left.camera_edge_cursor_index == 9,
        "top-left combines up1 and left8 cursor bits");

    UiOverlayState bottom_right = make_state();
    bottom_right.mouse_x = 799;
    bottom_right.mouse_y = 599;
    ScrollCameraFromEdgeOrKeys(bottom_right);
    require(bottom_right.camera_x > 400 && bottom_right.camera_y > 400 &&
            bottom_right.camera_edge_cursor_index == 6,
        "bottom-right combines right2 and down4 cursor bits");
}

void test_edge_priority_and_arrow_keys() {
    UiOverlayState edge_wins = make_state();
    edge_wins.mouse_x = 799;
    edge_wins.camera_left_key_down = true;
    ScrollCameraFromEdgeOrKeys(edge_wins);
    require(edge_wins.camera_x > 400,
        "successful edge movement suppresses the arrow-key pass");

    UiOverlayState clamped_edge = make_state();
    clamped_edge.camera_x = 0;
    clamped_edge.mouse_x = 0;
    clamped_edge.camera_right_key_down = true;
    ScrollCameraFromEdgeOrKeys(clamped_edge);
    require(clamped_edge.camera_x > 0 && clamped_edge.camera_scroll_dirty &&
            clamped_edge.camera_edge_cursor_index == 8,
        "a clamped edge leaves arrow keys live and preserves its edge mask");

    UiOverlayState diagonal = make_state();
    diagonal.camera_left_key_down = true;
    diagonal.camera_up_key_down = true;
    ScrollCameraFromEdgeOrKeys(diagonal);
    require(diagonal.camera_x < 400 && diagonal.camera_y < 400 &&
            diagonal.camera_edge_cursor_index == 0,
        "keyboard-only diagonal movement uses cursor index zero");

    UiOverlayState no_pointer = make_state();
    no_pointer.camera_edge_pointer_valid = false;
    no_pointer.mouse_x = 0;
    no_pointer.mouse_y = 0;
    no_pointer.camera_right_key_down = true;
    ScrollCameraFromEdgeOrKeys(no_pointer);
    require(no_pointer.camera_x > 400 && no_pointer.camera_y == 400,
        "invalid startup pointer suppresses only edges, not arrow keys");
}

void test_boundary_does_not_consume_replay_step() {
    UiOverlayState state = make_state();
    state.replay_timing_enabled = true;
    state.current_tick_ms = 10u * 0x1fu;
    state.camera_scroll_tick_bucket = 9;
    state.camera_x = 0;
    state.mouse_x = 0;
    state.mouse_y = 0;
    const i32 old_y = state.camera_y;
    ScrollCameraFromEdgeOrKeys(state);
    require(state.camera_y < old_y && state.camera_scroll_tick_bucket == 10,
        "clamped x edge must not consume the replay step before y movement");
}

void test_ramp_and_minimap_drag_contract() {
    UiOverlayState moving = make_state();
    moving.camera_scroll_dirty = true;
    moving.camera_scroll_ramp = 2;
    UpdateCameraScrollRamp(moving);
    require(moving.camera_scroll_ramp == 3 && !moving.camera_scroll_dirty,
        "successful movement increments ramp and clears dirty");
    UpdateCameraScrollRamp(moving);
    require(moving.camera_scroll_ramp == 2,
        "idle frame decrements camera ramp");

    UiOverlayState minimap = make_state();
    minimap.minimap.output_x = 10;
    minimap.minimap.output_y = 20;
    minimap.minimap.minimap_width_pixels = 100;
    minimap.minimap.minimap_height_pixels = 100;
    minimap.minimap.map_width_tiles = 128;
    minimap.minimap.map_height_tiles = 128;
    minimap.minimap.viewport_width_pixels = 800;
    minimap.minimap.viewport_height_pixels = 600;
    minimap.mouse_x = 50;
    minimap.mouse_y = 60;
    minimap.camera_scroll_dirty = false;
    UpdateCameraFromMinimapDrag(minimap);
    require(!minimap.camera_scroll_dirty,
        "minimap drag must not feed the edge-scroll ramp dirty flag");
}

void test_resolution_theme_map_bounds() {
    struct Resolution {
        u32 width;
        u32 height;
        u32 bucket;
    };
    constexpr std::array<Resolution, 4> resolutions{{
        {640, 480, 0}, {800, 600, 1}, {1024, 768, 2}, {777, 555, 2},
    }};
    constexpr std::array<i32, 4> effective_heights{{496, 487, 487, 480}};
    constexpr std::array<std::array<i32, 4>, 3> world_viewport_heights{{
        {{356, 362, 356, 364}},
        {{439, 436, 421, 452}},
        {{446, 458, 447, 460}},
    }};

    for (const Resolution& resolution : resolutions) {
        for (u32 theme = 0; theme < 4; ++theme) {
            UiOverlayState state{};
            state.screen_width = resolution.width;
            state.screen_height = resolution.height;
            state.interface_theme_index = theme;
            state.map_width_tiles = 128;
            state.map_height_tiles = 96;
            ConfigureGameplayUiOverlayLayout(state);

            require(state.screen_layout_bucket == resolution.bucket,
                "display width selected the wrong original layout bucket");
            require(state.world_viewport_height == static_cast<u32>(
                        world_viewport_heights[resolution.bucket][theme]),
                "theme/layout world viewport height differs from original table");
            require(state.camera_max_x ==
                        128 * 0x20 - static_cast<i32>(resolution.width),
                "horizontal map bound did not subtract the client width");
            require(state.camera_max_y == 96 * 0x20 - effective_heights[theme],
                "vertical map bound did not subtract the theme viewport height");
            require(state.resource_counter_x ==
                        static_cast<i32>(resolution.width) - 0x8c &&
                    state.population_counter_x ==
                        static_cast<i32>(resolution.width) - 0x46,
                "top-right HUD anchors did not follow the display width");
        }
    }

    UiOverlayState tiny{};
    tiny.screen_width = 1024;
    tiny.screen_height = 768;
    tiny.interface_theme_index = 3;
    tiny.map_width_tiles = 8;
    tiny.map_height_tiles = 8;
    ConfigureGameplayUiOverlayLayout(tiny);
    require(tiny.camera_max_x == 0 && tiny.camera_max_y == 0,
        "map bounds must clamp to zero when the viewport exceeds the map");
}

void test_all_edges_corners_outside_and_hud() {
    struct Resolution {
        i32 width;
        i32 height;
    };
    constexpr std::array<Resolution, 4> resolutions{{
        {640, 480}, {800, 600}, {1024, 768}, {777, 555},
    }};
    struct PointCase {
        i32 x_selector;
        i32 y_selector;
        u32 mask;
        i32 x_sign;
        i32 y_sign;
    };
    // -1 selects zero, -2 selects the last client pixel and zero otherwise
    // selects a non-edge center coordinate.
    constexpr std::array<PointCase, 8> points{{
        {-1, 0, 8, -1, 0}, {-2, 0, 2, 1, 0},
        {0, -1, 1, 0, -1}, {0, -2, 4, 0, 1},
        {-1, -1, 9, -1, -1}, {-2, -1, 3, 1, -1},
        {-1, -2, 12, -1, 1}, {-2, -2, 6, 1, 1},
    }};

    for (const Resolution& resolution : resolutions) {
        for (const PointCase& point : points) {
            UiOverlayState state = make_state();
            state.screen_width = static_cast<u32>(resolution.width);
            state.screen_height = static_cast<u32>(resolution.height);
            state.mouse_x = point.x_selector == -1 ? 0 :
                point.x_selector == -2 ? resolution.width - 1 :
                resolution.width / 2;
            state.mouse_y = point.y_selector == -1 ? 0 :
                point.y_selector == -2 ? resolution.height - 1 :
                resolution.height / 2;
            const i32 before_x = state.camera_x;
            const i32 before_y = state.camera_y;
            ScrollCameraFromEdgeOrKeys(state);
            require(state.camera_edge_cursor_index == point.mask,
                "edge/corner cursor mask differs from original bit sum");
            require((state.camera_x - before_x) * point.x_sign >= 0 &&
                    (state.camera_y - before_y) * point.y_sign >= 0,
                "edge/corner camera movement used the wrong direction");
            if (point.x_sign != 0) {
                require(state.camera_x != before_x,
                    "horizontal edge/corner did not move the camera");
            }
            if (point.y_sign != 0) {
                require(state.camera_y != before_y,
                    "vertical edge/corner did not move the camera");
            }
        }

        constexpr std::array<std::array<i32, 2>, 8> outside_offsets{{
            {{-1, 200}}, {{-8, -8}}, {{64000, 200}}, {{200, -1}},
            {{200, 64000}}, {{-1, -1}}, {{64000, -1}}, {{-1, 64000}},
        }};
        for (const auto& outside : outside_offsets) {
            UiOverlayState state = make_state();
            state.screen_width = static_cast<u32>(resolution.width);
            state.screen_height = static_cast<u32>(resolution.height);
            state.mouse_x = outside[0] == 64000 ? resolution.width : outside[0];
            state.mouse_y = outside[1] == 64000 ? resolution.height : outside[1];
            ScrollCameraFromEdgeOrKeys(state);
            require(state.camera_x == 400 && state.camera_y == 400 &&
                    !state.camera_scroll_dirty &&
                    state.camera_edge_cursor_index == 0,
                "captured outside coordinate incorrectly triggered edge scroll");
        }

        UiOverlayState hud = make_state();
        hud.screen_width = static_cast<u32>(resolution.width);
        hud.screen_height = static_cast<u32>(resolution.height);
        hud.world_viewport_height = static_cast<u32>(resolution.height / 2);
        hud.mouse_x = resolution.width / 2;
        hud.mouse_y = static_cast<i32>(hud.world_viewport_height);
        ScrollCameraFromEdgeOrKeys(hud);
        require(hud.camera_y == 400 && hud.camera_edge_cursor_index == 0,
            "ordinary lower-HUD pixels incorrectly triggered bottom scrolling");
        hud.mouse_y = resolution.height - 1;
        ScrollCameraFromEdgeOrKeys(hud);
        require(hud.camera_y > 400 && hud.camera_edge_cursor_index == 4,
            "the exact bottom client pixel must scroll even over HUD artwork");
    }
}
}

int main() {
    test_set1_arrow_table_drives_camera();
    test_exact_edges_and_cursor_masks();
    test_edge_priority_and_arrow_keys();
    test_boundary_does_not_consume_replay_step();
    test_ramp_and_minimap_drag_contract();
    test_resolution_theme_map_bounds();
    test_all_edges_corners_outside_and_hud();
    std::cout << "EDGE_SCROLL_PARITY_PASS exact=4 masks=1/2/4/8 "
                 "keys=edge-priority replay=boundary-safe minimap=no-ramp "
                 "resolutions=640/800/1024/777 themes=4 bounds=table "
                 "corners=4 outside=8 hud=interior/bottom\n";
}
