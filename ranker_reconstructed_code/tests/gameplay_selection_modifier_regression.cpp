#include "ranker_ui_overlay.h"
#include "ranker_gameplay_session_flow.h"

#include <cstdlib>
#include <cstring>

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

void verify_worker_build_category_selection_reset() {
    ranker::UiOverlayState state{};
    u32 pointer_aux_state = 3;
    state.selected_production_category = pointer_aux_state;
    state.staged_unit_action_id = 0x2cu;

    ranker::ResetUiOverlayProductionCategoryForSelectionChange(state);
    require(state.selected_production_category == 0);
    require(state.production_category_reset_requested);

    // A frame sync can occur before mutations are published.  It must not
    // resurrect the previous worker's construction category in that window.
    ranker::SyncUiOverlayProductionCategoryFromPointerState(
        state, pointer_aux_state);
    require(state.selected_production_category == 0);

    ranker::ApplyUiOverlayProductionCategorySelectionReset(
        state, pointer_aux_state);
    require(pointer_aux_state == 0);
    require(state.selected_production_category == 0);
    require(!state.production_category_reset_requested);
    require(state.staged_unit_action_id == 0x2cu);

    // Once the reset is published, normal category mirroring remains active.
    pointer_aux_state = 2;
    ranker::SyncUiOverlayProductionCategoryFromPointerState(
        state, pointer_aux_state);
    require(state.selected_production_category == 2);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 &&
        std::strcmp(argv[1], "worker_build_category_selection_reset") == 0) {
        verify_worker_build_category_selection_reset();
        return 0;
    }
    // FUN_004eb063 uses the live Shift bytes directly for both click and box
    // selection.  This is the exact routing that the old disconnected
    // additive_selection_mode field failed to provide.
    require(!ranker::UiOverlaySelectionIsAdditive(false));
    require(ranker::UiOverlaySelectionIsAdditive(true));
    require(ranker::ResolveUiOverlayClickSelectionPolicy(false, false) ==
        ranker::UiOverlayClickSelectionPolicy::preserve);
    require(ranker::ResolveUiOverlayClickSelectionPolicy(false, true) ==
        ranker::UiOverlayClickSelectionPolicy::preserve);
    require(ranker::ResolveUiOverlayClickSelectionPolicy(true, false) ==
        ranker::UiOverlayClickSelectionPolicy::replace);
    require(ranker::ResolveUiOverlayClickSelectionPolicy(true, true) ==
        ranker::UiOverlayClickSelectionPolicy::additive);

    require(!ranker::ShouldCloseApplicationAfterP2PMatch(false, false));
    require(!ranker::ShouldCloseApplicationAfterP2PMatch(false, true));
    require(ranker::ShouldCloseApplicationAfterP2PMatch(true, false));

    // FUN_004eb063 reads the live Shift/Ctrl key bytes when the gameplay
    // worker handles a queued click.  The MK_* bits captured in the window
    // message are not consulted, so releasing a modifier before the worker
    // drains the event must not leave a stale additive/same-type selection.
    require(!ranker::ResolveUiOverlayPointerModifierDown(0x0004u, 0x0004u, false));
    require(!ranker::ResolveUiOverlayPointerModifierDown(0x0008u, 0x0008u, false));

    // Conversely, a modifier pressed after the mouse message was queued is
    // active by the time the original selection routine consumes the event.
    require(ranker::ResolveUiOverlayPointerModifierDown(0, 0x0004u, true));
    require(ranker::ResolveUiOverlayPointerModifierDown(0, 0x0008u, true));
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
    verify_worker_build_category_selection_reset();

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
        require(ranker::ResolveUiOverlayCommandSnapshotField3(
            0xb5u, result_type, 0xfeedu) == result_type);
    }
    require(ranker::ResolveUiOverlayCommandSnapshotField3(
        0xb6u, 0x2du, 0xfeedu) == 0xfeedu);
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
