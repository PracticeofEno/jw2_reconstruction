#include "ranker_client_config.h"
#include "ranker_game_loop.h"
#include "ranker_unit_render_queue.h"

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

    require(GameplayLoopState{}.render_target_fps == 0,
        "the high-FPS experiment must be disabled by default");
    require(NormalizeConfiguredRenderFramesPerSecond(0) == 0 &&
            NormalizeConfiguredRenderFramesPerSecond(29) == 0 &&
            NormalizeConfiguredRenderFramesPerSecond(361) == 0,
        "disabled and out-of-range render FPS values must use original pacing");
    require(NormalizeConfiguredRenderFramesPerSecond(60) == 60 &&
            NormalizeConfiguredRenderFramesPerSecond(144) == 144,
        "supported render FPS values must remain configurable");
    require(kGameplayExperimentalRenderFramesPerSecond == 144,
        "the optional experiment must retain its 144 FPS preset");
    const u64 interval_ns = GameplayTargetRenderIntervalNanoseconds(
        kGameplayExperimentalRenderFramesPerSecond);
    require(interval_ns == 6944444ull,
        "the 144 Hz interval must use the high-resolution render clock");

    bool initialized = false;
    u64 next_present_ns = 0;
    require(ShouldPresentGameplayTargetFrame(900000000ull,
            next_present_ns, initialized, 0) && !initialized,
        "zero render FPS must preserve the uncapped original presentation path");
    require(ShouldPresentGameplayTargetFrame(1000000000ull,
            next_present_ns, initialized, 144),
        "the first frame must present immediately");
    require(!ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns - 1,
            next_present_ns, initialized, 144),
        "a frame before the 144 Hz deadline must stay presentation-only idle");
    require(ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns,
            next_present_ns, initialized, 144),
        "the 144 Hz deadline must publish a frame");
    require(ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns * 5,
            next_present_ns, initialized, 144),
        "a late renderer must skip missed deadlines without catch-up redraws");
    require(!ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns * 5,
            next_present_ns, initialized, 144),
        "a skipped deadline must not redraw twice at the same clock value");

    constexpr u64 simulation_interval_ns = 45000000ull;
    require(GameplayRenderInterpolationAlpha(2000000000ull, 2000000000ull,
            simulation_interval_ns) == 0,
        "a new simulation snapshot must begin at its previous position");
    require(GameplayRenderInterpolationAlpha(2022500000ull, 2000000000ull,
            simulation_interval_ns) == kGameplayRenderInterpolationOne / 2,
        "half a simulation interval must produce a half interpolation step");
    require(GameplayRenderInterpolationAlpha(2045000000ull, 2000000000ull,
            simulation_interval_ns) == kGameplayRenderInterpolationOne,
        "a complete simulation interval must reach the authoritative target");

    UnitRenderQueueContext render_queue{};
    UnitRenderItem moving{};
    moving.x = 100;
    moving.y = 200;
    moving.interpolation_start_x = 100;
    moving.interpolation_start_y = 200;
    moving.interpolation_target_x = 112;
    moving.interpolation_target_y = 192;
    moving.interpolated_x = moving.x;
    moving.interpolated_y = moving.y;
    moving.interpolation_enabled = true;
    render_queue.units.push_back(moving);

    ApplyUnitRenderInterpolation(
        render_queue, kGameplayRenderInterpolationOne / 2);
    require(render_queue.units[0].interpolated_x == 106 &&
            render_queue.units[0].interpolated_y == 196,
        "the renderer must interpolate only the draw coordinates");
    require(render_queue.units[0].x == 100 && render_queue.units[0].y == 200,
        "interpolation must not alter visibility or sort coordinates");
    require(render_queue.units[0].interpolation_target_x == 112 &&
            render_queue.units[0].interpolation_target_y == 192,
        "interpolation must preserve the authoritative render snapshot");

    ApplyUnitRenderInterpolation(render_queue, kGameplayRenderInterpolationOne);
    require(render_queue.units[0].interpolated_x == 112 &&
            render_queue.units[0].interpolated_y == 192,
        "the final visual frame must land exactly on the simulation snapshot");

    std::cout << "144 fps render pacing regression: PASS\n";
    return EXIT_SUCCESS;
}
