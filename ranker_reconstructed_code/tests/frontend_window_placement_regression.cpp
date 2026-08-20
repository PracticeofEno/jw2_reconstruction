#include "ranker_create_game.h"
#include "ranker_client_config.h"
#include "ranker_cursor.h"
#include "ranker_display_constants.h"
#include "ranker_frontend_layout.h"
#include "ranker_link_lobby.h"
#include "ranker_map_brush.h"
#include "ranker_online_lobby.h"
#include "ranker_ui_screen.h"
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
constexpr FrontendLayoutRect kMovedBounds{280, 146, 800, 600};
constexpr FrontendLayoutPoint kOwnedFrontendOrigin{210, 125};
constexpr FrontendLayoutPoint kMovedOwnedFrontendOrigin =
    TranslateFrontendLayoutOriginForHostMove(
        kOwnedFrontendOrigin, kBounds, kMovedBounds);
static_assert(kMovedOwnedFrontendOrigin.x == 390);
static_assert(kMovedOwnedFrontendOrigin.y == 221);
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
static_assert(NormalizeRankerClientWizardNetPort(19777) == 19777);
static_assert(NormalizeRankerClientWizardNetPort(0) ==
    kDefaultRankerClientWizardNetPort);
static_assert(NormalizeRankerClientWizardNetPort(0x10000u) ==
    kDefaultRankerClientWizardNetPort);
static_assert(kScaledControl.x == 160);
static_assert(kScaledControl.y == 80);
static_assert(kScaledControl.width == 320);
static_assert(kScaledControl.height == 160);
static_assert(kScaledRoomMapPreview.x == 1006);
static_assert(kScaledRoomMapPreview.y == 64);
static_assert(kScaledRoomMapPreview.width == 178);
static_assert(kScaledRoomMapPreview.height == 192);
static_assert(ScaleFrontendLayoutValue(12,
    kLegacyFrontendLayoutHeight, kDefaultPresentationClientHeight) == 19);
static_assert(ScaleFrontendLayoutValue(23,
    kLegacyFrontendLayoutWidth, kDefaultPresentationClientWidth) == 37);
static_assert(ResolveMinimapTerrainScalePercent(
    111, 120, 96, 96, false) == 100);
static_assert(ResolveMinimapTerrainScalePercent(
    111, 120, 96, 96, true) == 115);
static_assert(OnlineLobbyButtonLayoutIndex(0) == 1);
static_assert(OnlineLobbyButtonLayoutIndex(1) == 7);
static_assert(OnlineLobbyButtonLayoutIndex(8) == 14);
static_assert(OnlineLobbyButtonLayoutIndex(27) == 33);
constexpr OnlineLobbyLayoutRect kLegacyChatEdit{469, 578, 409, 22};
constexpr OnlineLobbyLayoutRect kLegacySendSlot{960, 578, 17, 22};
constexpr OnlineLobbyLayoutRect kLegacyEmoticonButton{888, 578, 27, 22};
constexpr OnlineLobbyLayoutRect kSingleEmoticonButton =
    InsetOnlineLobbyComposerButton(
        RightAlignOnlineLobbyComposerButton(
            kLegacyEmoticonButton, kLegacySendSlot), 6);
constexpr OnlineLobbyLayoutRect kExpandedChatEdit =
    ExpandOnlineLobbyChatEditToButton(
        kLegacyChatEdit, kSingleEmoticonButton, 8);
static_assert(kSingleEmoticonButton.x == 944);
static_assert(kSingleEmoticonButton.width == 27);
static_assert(kExpandedChatEdit.x == 469);
static_assert(kExpandedChatEdit.width == 467);
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x1a));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x1e));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x26));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x27));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x3e));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x46));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x64));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x93));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x94));
static_assert(IsOnlineLobbyTransientChildResponseOpcode(0x95));
static_assert(!IsOnlineLobbyTransientChildResponseOpcode(0x07));
static_assert(!IsOnlineLobbyTransientChildResponseOpcode(0x13));
static_assert(ShouldShowLinkLobbyStartButton(true));
static_assert(!ShouldShowLinkLobbyStartButton(false));
static_assert((kScenarioUiVideoBackdropExtendedStyle & 0x00000008u) == 0);
static_assert(SelectCreateGameWindowPlacement(true, 6) ==
    CreateGameWindowPlacement::contained_child);
static_assert(SelectCreateGameWindowPlacement(true, 1) ==
    CreateGameWindowPlacement::contained_child);
static_assert(SelectCreateGameWindowPlacement(false, 6) ==
    CreateGameWindowPlacement::fullscreen_popup);
static_assert(FrontendCursorArgbFromRgb565(0x0000) == 0x00000000u);
static_assert(FrontendCursorArgbFromRgb565(0xf800) == 0xffff0000u);
static_assert(FrontendCursorArgbFromRgb565(0x07e0) == 0xff00ff00u);
static_assert(FrontendCursorArgbFromRgb565(0x001f) == 0xff0000ffu);
static_assert(FrontendCursorArgbFromRgb565(0xffff) == 0xffffffffu);

} // namespace

int main() {
    assert(BuildCreateGameDefaultTitle("Wizard") == "Wizard's Game");
    assert(BuildCreateGameDefaultTitle("") == "Player's Game");
    assert(BuildCreateGameDefaultTitle("12345678901234567890") ==
        "123456789012's Game");
    assert(ScalePresentationCoordinateToLogical(0, 1280, 800) == 0);
    assert(ScalePresentationCoordinateToLogical(640, 1280, 800) == 400);
    assert(ScalePresentationCoordinateToLogical(1279, 1280, 800) == 799);
    assert(ScalePresentationCoordinateToLogical(480, 960, 600) == 300);
    assert(ScalePresentationCoordinateToLogical(959, 960, 600) == 599);
    assert(ScaleLogicalCursorCoordinateToPresentation(3, 800, 1280) == 4);
    assert(ResolveProgrammaticPointerMotionLogicalTarget(3, 800, 1280) == 2);
    assert(ResolveProgrammaticPointerMotionLogicalTarget(400, 800, 1280) == 400);
    assert(ResolveProgrammaticPointerMotionLogicalTarget(799, 800, 1280) == 799);
    assert(ResolveProgrammaticPointerMotionLogicalTarget(3, 600, 960) == 2);
#ifdef _WIN32
    const RankerClientDisplayConfig display = LoadRankerClientDisplayConfig();
    assert(display.width == 1280);
    assert(display.height == 960);
    assert(!display.resizable);
    assert(display.border);
    assert(display.center);
    assert(!display.position_set);
    const RankerClientWizardNetConfig wizardnet =
        LoadRankerClientWizardNetConfig();
    assert(wizardnet.address == "115.22.136.89");
    assert(wizardnet.port == 19777);
#endif
    return 0;
}
