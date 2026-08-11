#include "ranker_ui_overlay.h"

#include <cstdlib>

namespace {

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

ranker::UiOverlayMinimapUnit make_unit(u32 type, u32 owner) {
    ranker::UiOverlayMinimapUnit unit{};
    unit.type_id = type;
    unit.owner_id = owner;
    return unit;
}

} // namespace

int main() {
    // FUN_004eb063 uses the live Shift bytes directly for both click and box
    // selection.  This is the exact routing that the old disconnected
    // additive_selection_mode field failed to provide.
    require(!ranker::UiOverlaySelectionIsAdditive(false));
    require(ranker::UiOverlaySelectionIsAdditive(true));

    // Pointer-event flags retain Shift/Ctrl if the key is released before the
    // gameplay worker drains the event; live state remains the injection
    // fallback for messages with no MK_* bits.
    require(ranker::ResolveUiOverlayPointerModifierDown(0x0004u, 0x0004u, false));
    require(ranker::ResolveUiOverlayPointerModifierDown(0x0008u, 0x0008u, false));
    require(ranker::ResolveUiOverlayPointerModifierDown(0, 0x0004u, true));
    require(!ranker::ResolveUiOverlayPointerModifierDown(0, 0x0004u, false));

    ranker::UiOverlayState state{};
    state.local_player_slot = 2;
    // Shift-drag is not a same-type filter: every visible local mobile is a
    // candidate, while an enemy mobile and a local structure are not.
    require(ranker::UiOverlayLocalMobileSelectionCandidate(
        state, make_unit(0x10u, 2u)));
    require(ranker::UiOverlayLocalMobileSelectionCandidate(
        state, make_unit(0x2fu, 2u)));
    require(!ranker::UiOverlayLocalMobileSelectionCandidate(
        state, make_unit(0x10u, 1u)));
    require(!ranker::UiOverlayLocalMobileSelectionCandidate(
        state, make_unit(0x60u, 2u)));

    return 0;
}
