#pragma once

#include "ranker_display_constants.h"

#include <string>

namespace ranker {

constexpr int kMaximumPresentationClientExtent = 0x7fff;

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
    bool resizable = false;
    bool border = true;
    bool center = true;
    bool position_set = false;
};

#ifdef _WIN32
const std::string& RankerClientConfigPath();
RankerClientDisplayConfig LoadRankerClientDisplayConfig();
#endif

} // namespace ranker
