#include "ranker_ui_overlay.h"
#include "ranker_ui_screen.h"
#include "ranker_unit_animation.h"

using namespace ranker;

static_assert(ResolveGameplayMessageSettingsRoute(false, false, 0) ==
    GameplayMessageSettingsRoute::None);
static_assert(ResolveGameplayMessageSettingsRoute(false, true, 0) ==
    GameplayMessageSettingsRoute::OpenDialog);
static_assert(ResolveGameplayMessageSettingsRoute(false, true, 2) ==
    GameplayMessageSettingsRoute::ToggleOverlayFlags);
static_assert(ResolveGameplayMessageSettingsRoute(true, false, 0) ==
    GameplayMessageSettingsRoute::ToggleOverlayFlags);

static_assert(ShouldOpenGameplayRelationMaskDialog(false, true, 0));
static_assert(!ShouldOpenGameplayRelationMaskDialog(true, true, 0));
static_assert(!ShouldOpenGameplayRelationMaskDialog(false, false, 0));
static_assert(!ShouldOpenGameplayRelationMaskDialog(false, true, 2));
static_assert(!ShouldOpenGameplayRelationMaskDialog(false, true, 4));

static_assert(SynchronizeUiOverlayUnitSelectionFlag(0x05u, true) == 0x85u);
static_assert(SynchronizeUiOverlayUnitSelectionFlag(0x85u, false) == 0x05u);
static_assert(ShouldDrawUnitWorldBars(
    SynchronizeUiOverlayUnitSelectionFlag(0u, true)));
static_assert(!ShouldDrawUnitWorldBars(
    SynchronizeUiOverlayUnitSelectionFlag(0x80u, false)));

// The three fixed HUD buttons (Menu/Alliance/Message) use their alternate
// sprite only while the corresponding pointer press is still active.
static_assert(IsUiOverlayCommandButtonPressed(
    true, 0x194u, 0u, 0x194u, 0u, false));
static_assert(!IsUiOverlayCommandButtonPressed(
    false, 0xffffffffu, 0xffffffffu, 0x194u, 0u, false));
static_assert(!IsUiOverlayCommandButtonPressed(
    true, 0x195u, 0u, 0x194u, 0u, false));
static_assert(!IsUiOverlayCommandButtonPressed(
    true, 0x194u, 2u, 0x194u, 1u, true));

int main() {
    return 0;
}
