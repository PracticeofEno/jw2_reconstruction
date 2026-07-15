#include "ranker_gameplay_frame_render.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_original_world_viewport_baselines() {
    // ConfigureGameplayUiOverlayLayout's original 3 buckets x 4 themes.
    constexpr std::array<std::array<u32, 4>, 3> kViewportHeights{{
        {{356u, 362u, 356u, 364u}},
        {{439u, 436u, 421u, 452u}},
        {{446u, 458u, 447u, 460u}},
    }};
    constexpr std::array<std::array<i32, 4>, 3> kExpectedY{{
        {{336, 342, 336, 344}},
        {{419, 416, 401, 432}},
        {{426, 438, 427, 440}},
    }};
    constexpr std::array<i32, 5> kNotificationOffsets{
        -0x96, -0x82, -0x6e, -0x5a, -0x46};

    for (std::size_t bucket = 0; bucket < kViewportHeights.size(); ++bucket) {
        for (std::size_t theme = 0; theme < kViewportHeights[bucket].size();
             ++theme) {
            expect(ranker::ResolveGameplayHudBottomTextY(
                       600u, kViewportHeights[bucket][theme]) ==
                    kExpectedY[bucket][theme],
                "HUD message baseline did not use the original viewport table");
            for (const i32 offset : kNotificationOffsets) {
                expect(ranker::ResolveGameplayHudViewportOffsetY(600u,
                           kViewportHeights[bucket][theme], offset) ==
                        static_cast<i32>(kViewportHeights[bucket][theme]) +
                            offset,
                    "timed HUD notification did not use the viewport boundary");
            }
        }
    }
}

void test_queue_origin_remains_full_back_buffer_height() {
    expect(ranker::ResolveGameplayHudQueuedMessageStartY(600u, 9u) == 582,
        "queued message no longer starts from full logical height");
    expect(ranker::ResolveGameplayHudBottomTextY(600u, 421u) == 401,
        "800/theme-2 shortage message baseline is not original y=401");
    expect(ranker::ResolveGameplayHudBottomTextY(600u, 0u) == 580,
        "standalone fallback did not retain full-height behavior");
}

void test_original_font_and_centering_contract() {
    expect(ranker::kGameplayHudTextDrawFontIndex == 4,
        "gameplay HUD measure/draw font is not original slot four");
    expect(ranker::ResolveGameplayHudCenteredTextX(800u, 266u) == 267,
        "original shortage text width no longer centers at x=267");
    expect(ranker::ResolveGameplayHudCenteredTextX(800u, 900u) ==
            static_cast<i32>(0x7fffffceu),
        "oversized HUD message lost the original unsigned SUB/SHR result");
}

void test_runtime_surface_layout_transitions_are_atomic() {
    ranker::GameplayHudTextState state{};
    state.screen_width = 800;
    state.screen_height = 600;
    state.world_viewport_height = 421;

    expect(!ranker::UpdateGameplayHudSurfaceLayoutMetrics(
               state, 800, 600, 421),
        "unchanged 800/theme-2 layout requested a redundant HUD reset");
    expect(ranker::UpdateGameplayHudSurfaceLayoutMetrics(
               state, 640, 480, 356),
        "800-to-640 surface transition did not request a HUD reset");
    expect(state.screen_width == 640 && state.screen_height == 480 &&
            state.world_viewport_height == 356,
        "640 surface metrics were not updated as one layout snapshot");
    expect(ranker::ResolveGameplayHudBottomTextY(
               state.screen_height, state.world_viewport_height) == 336,
        "640/theme-0 surface transition retained the 800 baseline");

    expect(ranker::UpdateGameplayHudSurfaceLayoutMetrics(
               state, 800, 600, 452),
        "640-to-800/theme-3 transition did not request a HUD reset");
    expect(state.screen_width == 800 && state.screen_height == 600 &&
            state.world_viewport_height == 452,
        "800/theme-3 metrics were not updated as one layout snapshot");
    expect(ranker::ResolveGameplayHudBottomTextY(
               state.screen_height, state.world_viewport_height) == 432,
        "800/theme-3 surface transition retained the 640 baseline");

    expect(ranker::UpdateGameplayHudSurfaceLayoutMetrics(
               state, 800, 600, 421),
        "theme-only viewport transition did not request a HUD reset");
    expect(!ranker::UpdateGameplayHudSurfaceLayoutMetrics(
               state, 800, 600, 421),
        "stable post-transition layout requested more than one HUD reset");
}

} // namespace

int main() {
    test_original_world_viewport_baselines();
    test_queue_origin_remains_full_back_buffer_height();
    test_original_font_and_centering_contract();
    test_runtime_surface_layout_transitions_are_atomic();
    std::cout << "HUD_SHORTAGE_MESSAGE_LAYOUT_PASS origin=582 target=401 "
                 "font=4 width=266 x=267 "
                 "source=DAT_01440004/DAT_0086358c\n";
    return 0;
}
