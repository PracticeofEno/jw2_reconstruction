#include "ranker_create_game.h"
#include "ranker_client_config.h"
#include "ranker_cursor.h"
#include "ranker_display_constants.h"
#include "ranker_frontend_layout.h"
#include "ranker_link_lobby.h"
#include "ranker_map_brush.h"
#include "ranker_online_lobby.h"
#include "ranker_player_profile.h"
#include "ranker_ui_screen.h"
#include "ranker_ui_overlay.h"
#include "ranker_view_rank.h"
#include "ranker_winmain.h"
#include "ranker_wizardnet_services.h"

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
static_assert(IsSupportedGameplayViewPercent(60));
static_assert(IsSupportedGameplayViewPercent(80));
static_assert(IsSupportedGameplayViewPercent(100));
static_assert(!IsSupportedGameplayViewPercent(59));
static_assert(!IsSupportedGameplayViewPercent(101));
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
static_assert(!IsOnlineLobbyThemedButtonId(kOnlineLobbyMainTabButtonId));
static_assert(IsOnlineLobbyThemedButtonId(kOnlineLobbyCreateGameButtonId));
static_assert(IsOnlineLobbyThemedButtonId(kOnlineLobbyJoinGameButtonId));
static_assert(IsOnlineLobbyThemedButtonId(kOnlineLobbyViewRankButtonId));
static_assert(IsOnlineLobbyThemedButtonId(kOnlineLobbyReplayButtonId));
static_assert(IsOnlineLobbyThemedButtonId(
    kOnlineLobbyReplayDownloadButtonId));
static_assert(IsOnlineLobbyThemedButtonId(kOnlineLobbyReplayCloseButtonId));
static_assert(IsOnlineLobbyThemedButtonId(IDCANCEL));
static_assert(!IsOnlineLobbyThemedButtonId(kOnlineLobbyEmoticonButtonId));
static_assert(OnlineLobbySimplifiedActionIndex(
    kOnlineLobbyCreateGameButtonId) == 0);
static_assert(OnlineLobbySimplifiedActionIndex(
    kOnlineLobbyJoinGameButtonId) == 1);
static_assert(OnlineLobbySimplifiedActionIndex(
    kOnlineLobbyViewRankButtonId) == 2);
static_assert(OnlineLobbySimplifiedActionIndex(
    kOnlineLobbyReplayButtonId) == 3);
static_assert(OnlineLobbySimplifiedActionIndex(IDCANCEL) == 4);
static_assert(OnlineLobbySimplifiedActionIndex(
    kOnlineLobbyMainTabButtonId) == -1);
constexpr OnlineLobbyLayoutRect kSimplifiedActionRow{47, 540, 705, 38};
constexpr OnlineLobbyLayoutRect kSimplifiedCreateAction =
    ArrangeOnlineLobbySimplifiedAction(kSimplifiedActionRow, 0, 10);
constexpr OnlineLobbyLayoutRect kSimplifiedJoinAction =
    ArrangeOnlineLobbySimplifiedAction(kSimplifiedActionRow, 1, 10);
constexpr OnlineLobbyLayoutRect kSimplifiedRankAction =
    ArrangeOnlineLobbySimplifiedAction(kSimplifiedActionRow, 2, 10);
constexpr OnlineLobbyLayoutRect kSimplifiedReplayAction =
    ArrangeOnlineLobbySimplifiedAction(kSimplifiedActionRow, 3, 10);
constexpr OnlineLobbyLayoutRect kSimplifiedExitAction =
    ArrangeOnlineLobbySimplifiedAction(kSimplifiedActionRow, 4, 10);
static_assert(kSimplifiedCreateAction.x == 47 &&
    kSimplifiedCreateAction.width == 133);
static_assert(kSimplifiedJoinAction.x == 190 &&
    kSimplifiedJoinAction.width == 133);
static_assert(kSimplifiedRankAction.x == 333 &&
    kSimplifiedRankAction.width == 133);
static_assert(kSimplifiedReplayAction.x == 476 &&
    kSimplifiedReplayAction.width == 133);
static_assert(kSimplifiedExitAction.x == 619 &&
    kSimplifiedExitAction.width == 133 &&
    kSimplifiedExitAction.x + kSimplifiedExitAction.width == 752);
constexpr OnlineLobbyLayoutRect kCompactSimplifiedCreateAction =
    InsetOnlineLobbyButton(kSimplifiedCreateAction,
        kOnlineLobbySimplifiedActionHorizontalInset,
        kOnlineLobbySimplifiedActionVerticalInset);
constexpr OnlineLobbyLayoutRect kCompactReplayDownloadAction =
    InsetOnlineLobbyButton({202,
        533 + kOnlineLobbyReplayActionDownwardOffset, 144, 54},
        kOnlineLobbyReplayActionHorizontalInset,
        kOnlineLobbyReplayActionVerticalInset);
constexpr OnlineLobbyLayoutRect kCompactReplayCloseAction =
    InsetOnlineLobbyButton({462,
        533 + kOnlineLobbyReplayActionDownwardOffset, 144, 54},
        kOnlineLobbyReplayActionHorizontalInset,
        kOnlineLobbyReplayActionVerticalInset);
static_assert(kCompactSimplifiedCreateAction.x == 57 &&
    kCompactSimplifiedCreateAction.y == 545 &&
    kCompactSimplifiedCreateAction.width == 113 &&
    kCompactSimplifiedCreateAction.height == 28);
static_assert(kCompactReplayDownloadAction.x == 226 &&
    kCompactReplayDownloadAction.y == 555 &&
    kCompactReplayDownloadAction.width == 96 &&
    kCompactReplayDownloadAction.height == 30);
static_assert(kCompactReplayCloseAction.x == 486 &&
    kCompactReplayCloseAction.y == 555 &&
    kCompactReplayCloseAction.width == 96 &&
    kCompactReplayCloseAction.height == 30);
static_assert(ResolveOnlineLobbyThemeButtonVisual(true, false, false, false) ==
    OnlineLobbyThemeButtonVisual::Normal);
static_assert(ResolveOnlineLobbyThemeButtonVisual(true, false, true, false) ==
    OnlineLobbyThemeButtonVisual::Hot);
static_assert(ResolveOnlineLobbyThemeButtonVisual(true, false, false, true) ==
    OnlineLobbyThemeButtonVisual::Hot);
static_assert(ResolveOnlineLobbyThemeButtonVisual(true, true, true, true) ==
    OnlineLobbyThemeButtonVisual::Pressed);
static_assert(ResolveOnlineLobbyThemeButtonVisual(false, true, true, true) ==
    OnlineLobbyThemeButtonVisual::Disabled);
static_assert(ResolveOnlineLobbyReplayScrollbarVisual(true, false) ==
    OnlineLobbyReplayScrollbarVisual::Normal);
static_assert(ResolveOnlineLobbyReplayScrollbarVisual(true, true) ==
    OnlineLobbyReplayScrollbarVisual::Active);
static_assert(ResolveOnlineLobbyReplayScrollbarVisual(false, true) ==
    OnlineLobbyReplayScrollbarVisual::Disabled);
constexpr OnlineLobbyReplayScrollbarSourceGeometry kNormalReplayScrollSource =
    ResolveOnlineLobbyReplayScrollbarSourceGeometry(
        OnlineLobbyReplayScrollbarVisual::Normal);
constexpr OnlineLobbyReplayScrollbarSourceGeometry kActiveReplayScrollSource =
    ResolveOnlineLobbyReplayScrollbarSourceGeometry(
        OnlineLobbyReplayScrollbarVisual::Active);
constexpr OnlineLobbyReplayScrollbarSourceGeometry kDisabledReplayScrollSource =
    ResolveOnlineLobbyReplayScrollbarSourceGeometry(
        OnlineLobbyReplayScrollbarVisual::Disabled);
static_assert(kNormalReplayScrollSource.source_height == 391 &&
    kNormalReplayScrollSource.top_end == 58 &&
    kNormalReplayScrollSource.thumb_top == 106 &&
    kNormalReplayScrollSource.thumb_bottom == 184);
static_assert(kActiveReplayScrollSource.source_height == 393 &&
    kActiveReplayScrollSource.top_end == 58 &&
    kActiveReplayScrollSource.thumb_top == 89 &&
    kActiveReplayScrollSource.thumb_bottom == 176);
static_assert(kDisabledReplayScrollSource.source_height == 390 &&
    kDisabledReplayScrollSource.top_end == 58 &&
    kDisabledReplayScrollSource.thumb_top == 89 &&
    kDisabledReplayScrollSource.thumb_bottom == 176);
static_assert(!ShouldPlayWizardNetDownloadedReplay(
    OnlineLobbyReplayDownloadAction::DownloadOnly, true));
static_assert(!ShouldPlayWizardNetDownloadedReplay(
    OnlineLobbyReplayDownloadAction::DownloadAndPlay, false));
static_assert(ShouldPlayWizardNetDownloadedReplay(
    OnlineLobbyReplayDownloadAction::DownloadAndPlay, true));
constexpr OnlineLobbyLayoutRect kLegacyChatEdit{469, 578, 409, 22};
constexpr OnlineLobbyLayoutRect kPaddedChatEdit =
    OffsetOnlineLobbyChatComposer(kLegacyChatEdit,
        kOnlineLobbyChatComposerVerticalPadding);
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
static_assert(kPaddedChatEdit.y == 594);
static_assert(kPaddedChatEdit.height == kLegacyChatEdit.height);
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
static_assert(UsesWizardNetNormalGameStatistics(0));
static_assert(UsesWizardNetNormalGameStatistics(1));
static_assert(!UsesWizardNetNormalGameStatistics(2));
static_assert(UsesWizardNetRankingStatistics(2));
static_assert(ShouldAutoUploadWizardNetReplay(1));
static_assert(ShouldAutoUploadWizardNetReplay(2));
static_assert(!ShouldAutoUploadWizardNetReplay(0));
static_assert(WizardNetOutcomeFromGameplayResult(0) ==
    WizardNetMatchOutcome::Win);
static_assert(WizardNetOutcomeFromGameplayResult(1) ==
    WizardNetMatchOutcome::Loss);
static_assert(WizardNetOutcomeFromGameplayResult(2) ==
    WizardNetMatchOutcome::Draw);
static_assert(ShouldShowLinkLobbyStartButton(true));
static_assert(!ShouldShowLinkLobbyStartButton(false));
static_assert(ShouldShowLinkLobbyStartButton(true, true, true));
static_assert(!ShouldShowLinkLobbyStartButton(true, true, false));
static_assert(!ShouldShowLinkLobbyStartButton(false, true, true));
static_assert(!ShouldMaskLinkLobbyStartButtonBackground(true));
static_assert(ShouldMaskLinkLobbyStartButtonBackground(false));
static_assert(!ShouldMaskLinkLobbyStartButtonBackground(true, true, true));
static_assert(ShouldMaskLinkLobbyStartButtonBackground(true, true, false));
static_assert(ShouldMaskLinkLobbyStartButtonBackground(false, true, true));
static_assert((kScenarioUiVideoBackdropExtendedStyle & 0x00000008u) == 0);
static_assert(SelectCreateGameWindowPlacement(true, 6) ==
    CreateGameWindowPlacement::contained_child);
static_assert(SelectCreateGameWindowPlacement(true, 1) ==
    CreateGameWindowPlacement::contained_child);
static_assert(SelectCreateGameWindowPlacement(false, 6) ==
    CreateGameWindowPlacement::fullscreen_popup);
static_assert(SelectPlayerProfileWindowPlacement(true) ==
    PlayerProfileWindowPlacement::owned_modal_overlay);
static_assert(SelectPlayerProfileWindowPlacement(false) ==
    PlayerProfileWindowPlacement::fullscreen_popup);
static_assert(IsPlayerProfileRemovedControlId(kPlayerProfileNameEditId));
static_assert(IsPlayerProfileRemovedControlId(kPlayerProfileGuildNameEditId));
static_assert(IsPlayerProfileRemovedControlId(kPlayerProfileMemoButtonId));
static_assert(IsPlayerProfileRemovedControlId(
    kPlayerProfileAvatarFirstButtonId));
static_assert(IsPlayerProfileRemovedControlId(
    kPlayerProfileAvatarMeleeEditId));
static_assert(IsPlayerProfileRemovedControlId(
    kPlayerProfileAvatarRankEditId));
static_assert(!IsPlayerProfileRemovedControlId(
    kPlayerProfileNormalRankEditId));
static_assert(!IsPlayerProfileRemovedControlId(
    kPlayerProfileDescriptionEditId));
constexpr OnlineLobbyLayoutRect kReplayButtonBeforeNudge{453, 540, 117, 38};
constexpr OnlineLobbyLayoutRect kReplayButtonAfterNudge =
    ShiftOnlineLobbyReplayButtonRight(kReplayButtonBeforeNudge,
        kOnlineLobbyReplayButtonRightNudge);
static_assert(kReplayButtonAfterNudge.x == 461);
static_assert(kReplayButtonAfterNudge.width == 117);
static_assert(kOnlineLobbyReplayListRowHeight == 40);
static_assert(ScaleFrontendLayoutValue(kOnlineLobbyReplayListRowHeight,
    600, 960) == 64);
static_assert(ScaleFrontendLayoutValue(
    kOnlineLobbyReplayInnerFrameHorizontalInset,
    kOnlineLobbyReplayButtonBaseWidth, 187) == 19);
static_assert(ScaleFrontendLayoutValue(
    kOnlineLobbyReplayInnerFrameVerticalInset,
    kOnlineLobbyReplayButtonBaseHeight, 61) == 16);
static_assert(IsViewRankRemovedButtonId(kViewRankGoSiteButtonId));
static_assert(IsViewRankRemovedButtonId(kViewRankNormalTabButtonId));
static_assert(IsViewRankRemovedButtonId(kViewRankAvatarTabButtonId));
static_assert(IsViewRankRemovedButtonId(kViewRankGuildTabButtonId));
static_assert(!IsViewRankRemovedButtonId(kViewRankCloseButtonId));
static_assert(CalculateViewRankWinRate(0, 0, 0) == 0);
static_assert(CalculateViewRankWinRate(7, 2, 1) == 70);
static_assert(CalculateViewRankWinRate(1, 1, 1) == 33);
static_assert(CalculateViewRankWinRate(0xffffffffu, 0xffffffffu,
    0xffffffffu) == 33);
static_assert(IsViewRankThemedButtonId(kViewRankUpButtonId));
static_assert(IsViewRankThemedButtonId(kViewRankDownButtonId));
static_assert(IsViewRankThemedButtonId(kViewRankSearchButtonId));
static_assert(IsViewRankThemedButtonId(kViewRankCloseButtonId));
static_assert(!IsViewRankThemedButtonId(kViewRankNormalTabButtonId));
static_assert(!IsViewRankThemedButtonId(kViewRankGoSiteButtonId));
static_assert(ResolveViewRankThemeButtonVisual(true, false, false) ==
    ViewRankThemeButtonVisual::Normal);
static_assert(ResolveViewRankThemeButtonVisual(true, false, true) ==
    ViewRankThemeButtonVisual::Hot);
static_assert(ResolveViewRankThemeButtonVisual(true, true, true) ==
    ViewRankThemeButtonVisual::Pressed);
static_assert(ResolveViewRankThemeButtonVisual(false, true, true) ==
    ViewRankThemeButtonVisual::Disabled);
constexpr ViewRankLayoutRect kViewRankCompactSearchButton =
    ResolveViewRankPngButtonRect(kViewRankSearchButtonId);
constexpr ViewRankLayoutRect kViewRankCompactPreviousButton =
    ResolveViewRankPngButtonRect(kViewRankUpButtonId);
constexpr ViewRankLayoutRect kViewRankCompactNextButton =
    ResolveViewRankPngButtonRect(kViewRankDownButtonId);
constexpr ViewRankLayoutRect kViewRankCompactCloseButton =
    ResolveViewRankPngButtonRect(kViewRankCloseButtonId);
static_assert(kViewRankCompactSearchButton.width == 84 &&
    kViewRankCompactSearchButton.height == 27);
static_assert(kViewRankCompactPreviousButton.width == 84 &&
    kViewRankCompactPreviousButton.height == 27);
static_assert(kViewRankCompactNextButton.width == 84 &&
    kViewRankCompactNextButton.height == 27);
static_assert(kViewRankCompactCloseButton.width == 84 &&
    kViewRankCompactCloseButton.height == 27);
static_assert(kViewRankCompactSearchButton.x +
    kViewRankCompactSearchButton.width < kViewRankCompactPreviousButton.x);
static_assert(kViewRankCompactPreviousButton.x +
    kViewRankCompactPreviousButton.width < kViewRankCompactNextButton.x);
static_assert(FrontendCursorArgbFromRgb565(0x0000) == 0x00000000u);
static_assert(FrontendCursorArgbFromRgb565(0xf800) == 0xffff0000u);
static_assert(FrontendCursorArgbFromRgb565(0x07e0) == 0xff00ff00u);
static_assert(FrontendCursorArgbFromRgb565(0x001f) == 0xff0000ffu);
static_assert(FrontendCursorArgbFromRgb565(0xffff) == 0xffffffffu);
static_assert(ShouldPreserveNativeFrontendCursorOnMainFocus(true, true));
static_assert(!ShouldPreserveNativeFrontendCursorOnMainFocus(true, false));
static_assert(!ShouldPreserveNativeFrontendCursorOnMainFocus(false, true));
static_assert(kWizardNetReplayListProtocolVersion == 1);
static_assert(kWizardNetReplayListLegacyRecordBytes == 0xb0);
static_assert(kWizardNetReplayListMetadataRecordBytes == 0xf4);
static_assert(kWizardNetReplayPresenceRequestOpcode == 0xa3);

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
    UiOverlayState wider_view{};
    wider_view.screen_width = 800;
    wider_view.screen_height = 600;
    wider_view.world_render_width = 1000;
    wider_view.world_render_height = 750;
    wider_view.camera_x = 320;
    wider_view.camera_y = 640;
    assert(UiOverlayWorldViewXFromScreen(wider_view, 400) == 500);
    assert(UiOverlayWorldViewYFromScreen(wider_view, 300) == 375);
    assert(UiOverlayScreenXFromWorldView(wider_view, 500) == 400);
    assert(UiOverlayScreenYFromWorldView(wider_view, 375) == 300);
    assert(UiOverlayWorldXFromScreen(wider_view, 400) == 820);
    assert(UiOverlayWorldYFromScreen(wider_view, 300) == 1015);
#ifdef _WIN32
    const RankerClientDisplayConfig display = LoadRankerClientDisplayConfig();
    assert(display.width == 1280);
    assert(display.height == 960);
    assert(display.gameplay_view_percent == 80);
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
