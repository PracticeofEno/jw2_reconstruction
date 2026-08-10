#include "ranker_create_game.h"
#include "ranker_client_config.h"
#include "ranker_display_constants.h"
#include "ranker_frontend_layout.h"
#include "ranker_online_lobby.h"
#include "ranker_winmain.h"

#include <cassert>

using namespace ranker;

namespace {

constexpr FrontendLayoutRect kBounds{100, 50, 800, 600};

static_assert(FrontendLayoutOrigin(kBounds).x == 100);
static_assert(FrontendLayoutOrigin(kBounds).y == 50);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 400, 300).x == 300);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 400, 300).y == 200);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 1200, 800).x == -100);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 1200, 800).y == -50);
static_assert(
    CenteredContainedFrontendLayoutOrigin(kBounds, 1200, 800).x == 100);
static_assert(
    CenteredContainedFrontendLayoutOrigin(kBounds, 1200, 800).y == 50);
constexpr FrontendLayoutRect kLegacyControl{100, 50, 200, 100};
constexpr FrontendLayoutRect kScaledControl =
    ScaleFrontendLayoutRect(kLegacyControl,
        kDefaultPresentationClientWidth, kDefaultPresentationClientHeight);
constexpr FrontendLayoutRect kLegacyRoomMapPreview{629, 40, 111, 120};
constexpr FrontendLayoutRect kScaledRoomMapPreview =
    ScaleFrontendLayoutRect(kLegacyRoomMapPreview,
        kDefaultPresentationClientWidth, kDefaultPresentationClientHeight);
static_assert(kDefaultPresentationClientWidth == 1280);
static_assert(kDefaultPresentationClientHeight == 960);
static_assert(IsSupportedPresentationClientSize(1280, 960));
static_assert(IsSupportedPresentationClientSize(1024, 768));
static_assert(!IsSupportedPresentationClientSize(1280, 720));
static_assert(!IsSupportedPresentationClientSize(0, 960));
static_assert(kScaledControl.x == 160);
static_assert(kScaledControl.y == 80);
static_assert(kScaledControl.width == 320);
static_assert(kScaledControl.height == 160);
static_assert(kScaledRoomMapPreview.x == 1006);
static_assert(kScaledRoomMapPreview.y == 64);
static_assert(kScaledRoomMapPreview.width == 178);
static_assert(kScaledRoomMapPreview.height == 192);
static_assert(OnlineLobbyButtonLayoutIndex(0) == 1);
static_assert(OnlineLobbyButtonLayoutIndex(1) == 7);
static_assert(OnlineLobbyButtonLayoutIndex(8) == 14);
static_assert(OnlineLobbyButtonLayoutIndex(27) == 33);
static_assert(SelectCreateGameWindowPlacement(true, 6) ==
    CreateGameWindowPlacement::contained_child);
static_assert(SelectCreateGameWindowPlacement(true, 1) ==
    CreateGameWindowPlacement::contained_child);
static_assert(SelectCreateGameWindowPlacement(false, 6) ==
    CreateGameWindowPlacement::fullscreen_popup);

} // namespace

int main() {
    assert(ScalePresentationCoordinateToLogical(0, 1280, 800) == 0);
    assert(ScalePresentationCoordinateToLogical(640, 1280, 800) == 400);
    assert(ScalePresentationCoordinateToLogical(1279, 1280, 800) == 799);
    assert(ScalePresentationCoordinateToLogical(480, 960, 600) == 300);
    assert(ScalePresentationCoordinateToLogical(959, 960, 600) == 599);
#ifdef _WIN32
    const RankerClientDisplayConfig display = LoadRankerClientDisplayConfig();
    assert(display.width == 1280);
    assert(display.height == 960);
    assert(!display.resizable);
    assert(display.border);
    assert(display.center);
    assert(!display.position_set);
#endif
    return 0;
}
