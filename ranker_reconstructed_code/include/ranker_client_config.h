#pragma once

#include "ranker_display_constants.h"

#include <string>

namespace ranker {

constexpr int kMaximumPresentationClientExtent = 0x7fff;
constexpr unsigned int kDefaultRankerClientWizardNetPort = 19777;

constexpr bool IsSupportedPresentationClientSize(int width, int height) {
    return width > 0 && height > 0 &&
        width <= kMaximumPresentationClientExtent &&
        height <= kMaximumPresentationClientExtent &&
        width * kOriginalClientHeight == height * kOriginalClientWidth;
}

constexpr bool IsSupportedGameplayViewPercent(int percent) {
    return percent >= 60 && percent <= 100;
}

constexpr unsigned int NormalizeRankerClientWizardNetPort(
    unsigned int port) {
    return port != 0 && port <= 0xffffu ?
        port : kDefaultRankerClientWizardNetPort;
}

struct RankerClientDisplayConfig {
    int width = kDefaultPresentationClientWidth;
    int height = kDefaultPresentationClientHeight;
    int gameplay_view_percent = 80;
    int x = 0;
    int y = 0;
    bool resizable = false;
    bool border = true;
    bool center = true;
    bool position_set = false;
};

struct RankerClientWizardNetConfig {
    std::string address = "127.0.0.1";
    unsigned int port = kDefaultRankerClientWizardNetPort;
};

#ifdef _WIN32
const std::string& RankerClientConfigPath();
RankerClientDisplayConfig LoadRankerClientDisplayConfig();
RankerClientWizardNetConfig LoadRankerClientWizardNetConfig();
std::string LoadRankerClientLastWizardAccount();
bool SaveRankerClientLastWizardAccount(const char* account);
#endif

} // namespace ranker
