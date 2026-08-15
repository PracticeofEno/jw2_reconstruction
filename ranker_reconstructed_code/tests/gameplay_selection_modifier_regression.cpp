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

    // A normal replacement selection returns the command panel to its root.
    // In particular, an open worker construction category must not survive
    // selecting a different worker.
    state.selected_production_category = 3;
    state.staged_unit_action_id = 0x2cu;
    ranker::ResetUiOverlayProductionCategoryForSelectionChange(state);
    require(state.selected_production_category == 0);
    require(state.staged_unit_action_id == 0x2cu);

    // Original 0x004e1134's item-0xb5 branch draws the result unit from the
    // 170-frame char_small table.  The ordinary object-command path instead
    // indexes the 42-frame action.trt table after subtracting 0xaa.  Keep all
    // four Tyrano linked-release results on the former path, including ids 43
    // and 45 which are out of range for action.trt.
    for (u32 result_type : {0x23u, 0x26u, 0x2bu, 0x2du}) {
        require(ranker::ResolveUiOverlayUnitOrObjectIconBlitKind(0xb5u) ==
            ranker::UiOverlayIconBlitKind::base);
        require(ranker::ResolveUiOverlayUnitOrObjectIconFrame(
            0xb5u, result_type) == result_type);
    }
    require(ranker::ResolveUiOverlayUnitOrObjectIconBlitKind(0xb6u) ==
        ranker::UiOverlayIconBlitKind::unit);
    require(ranker::ResolveUiOverlayUnitOrObjectIconFrame(0xb6u, 0x2du) ==
        0x0cu);

    // The panel publishes the three paired releases at two selected sources
    // and the Mutant release only when all three distinct sources are present.
    std::array<u32, 0x60> grouped_counts{};
    grouped_counts[0x22u] = 2;
    grouped_counts[0x25u] = 2;
    grouped_counts[0x27u] = 2;
    require(ranker::HasUiOverlayGroupedPairSelection(
        grouped_counts, 0x22u));
    require(ranker::HasUiOverlayGroupedPairSelection(
        grouped_counts, 0x25u));
    require(ranker::HasUiOverlayGroupedPairSelection(
        grouped_counts, 0x27u));
    require(!ranker::HasUiOverlayMutantTriadSelection(grouped_counts));
    grouped_counts[0x24u] = 1;
    grouped_counts[0x28u] = 1;
    require(ranker::HasUiOverlayMutantTriadSelection(grouped_counts));

    return 0;
}
