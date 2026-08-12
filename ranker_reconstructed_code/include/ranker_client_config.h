#pragma once

#include "ranker_display_constants.h"

#include <string>

namespace ranker {

constexpr int kMaximumPresentationClientExtent = 0x7fff;
constexpr int kMinimumConfiguredRenderFramesPerSecond = 30;
constexpr int kMaximumConfiguredRenderFramesPerSecond = 360;
constexpr int kDefaultConfiguredRenderFramesPerSecond = 144;

constexpr int NormalizeConfiguredRenderFramesPerSecond(int frames_per_second) {
    return frames_per_second >= kMinimumConfiguredRenderFramesPerSecond &&
        frames_per_second <= kMaximumConfiguredRenderFramesPerSecond ?
        frames_per_second : 0;
}

constexpr bool IsSupportedPresentationClientSize(int width, int height) {
    return width > 0 && height > 0 &&
        width <= kMaximumPresentationClientExtent &&
        height <= kMaximumPresentationClientExtent &&
        width * kOriginalClientHeight == height * kOriginalClientWidth;
}

struct RankerClientDisplayConfig {
    int width = kDefaultPresentationClientWidth;
    int height = kDefaultPresentationClientHeight;
    int x = 0;
    int y = 0;
    // Rendering defaults to 144 Hz. An explicit RenderFPS=0 preserves the
    // original presentation cadence and disables render-only interpolation.
    int render_frames_per_second = kDefaultConfiguredRenderFramesPerSecond;
    bool resizable = false;
    bool border = true;
    bool center = true;
    bool position_set = false;
};

#ifdef _WIN32
const std::string& RankerClientConfigPath();
RankerClientDisplayConfig LoadRankerClientDisplayConfig();
std::string LoadRankerClientLastWizardAccount();
bool SaveRankerClientLastWizardAccount(const char* account);
#endif

} // namespace ranker
