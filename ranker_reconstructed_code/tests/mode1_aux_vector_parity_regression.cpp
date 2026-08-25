#include "ranker_ui_overlay.h"
#include "ranker_unit_movement.h"

static_assert(ranker::ResolveUiOverlayProductionRallyRightClickAction(
    0u, 1u, 0x60u, 2u, 2u, 0u, 0u, 1u, false) ==
    ranker::kUiOverlayProductionRallyAction);
static_assert(ranker::ResolveUiOverlayProductionRallyRightClickAction(
    4u, 1u, 0x6fu, 2u, 2u, 0u, 0u, 0u, true) ==
    ranker::kUiOverlayProductionRallyAction);
static_assert(ranker::ResolveUiOverlayProductionRallyRightClickAction(
    4u, 1u, 0x22u, 2u, 2u, 0u, 0u, 1u, false) == 4u);
static_assert(ranker::ResolveUiOverlayProductionRallyRightClickAction(
    0u, 1u, 0x60u, 3u, 2u, 0u, 0u, 1u, false) == 0u);
static_assert(ranker::ResolveUiOverlayProductionRallyRightClickAction(
    0u, 1u, 0x60u, 2u, 2u, 1u, 0u, 1u, false) == 0u);
static_assert(ranker::ResolveUiOverlayProductionRallyRightClickAction(
    0u, 1u, 0x60u, 2u, 2u, 0u, 0x10000000u, 1u, false) == 0u);
static_assert(ranker::ResolveUiOverlayProductionRallyRightClickAction(
    0u, 1u, 0x60u, 2u, 2u, 0u, 0u, 0u, false) == 0u);
static_assert(ranker::ResolveUiOverlayMinimapRightClickItemId(
    1u, 0x60u, 2u, 2u, 0u, 0u, 1u, false) == 0xc9u);
static_assert(ranker::ResolveUiOverlayMinimapRightClickItemId(
    1u, 0x22u, 2u, 2u, 0u, 0u, 1u, false) == 0xaeu);
static_assert(ranker::ResolveUiOverlayMinimapRightClickItemId(
    2u, 0x60u, 2u, 2u, 0u, 0u, 1u, false) == 0xc9u);
static_assert(ranker::UiOverlayDoubleClickTargetAllowsGroupSelection(2u, 2u));
static_assert(!ranker::UiOverlayDoubleClickTargetAllowsGroupSelection(1u, 2u));
static_assert(ranker::UiOverlayDoubleClickRuntimeClassMatches(
    0x60u, 0x01u, 0x31u));
static_assert(!ranker::UiOverlayDoubleClickRuntimeClassMatches(
    0x20u, 0x01u, 0x31u));

int main() {
    ranker::UnitMovementUnit source{};
    ranker::UnitMovementUnit linked{};
    source.saved_path_target_x = 0x1234;
    source.saved_path_target_y = -0x2345;

    ranker::ApplyMode1Subtype08AuxVector(
        source, &linked, 0x3456, -0x4567, 0x5678);

    if (source.linked_object_id != 0x3456 ||
        source.linked_unit != &linked ||
        source.next_path_x != -0x4567 ||
        source.next_path_y != 0x5678) {
        return 1;
    }
    if (source.saved_path_target_x != 0x1234 ||
        source.saved_path_target_y != -0x2345) {
        return 2;
    }
    return 0;
}
