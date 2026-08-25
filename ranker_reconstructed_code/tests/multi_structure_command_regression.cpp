#include "ranker_gameplay_input_actions.h"
#include "ranker_gameplay_production_actions.h"
#include "ranker_reliable_packets.h"
#include "ranker_runtime_resources.h"
#include "ranker_ui_overlay.h"
#include "ranker_unit_movement.h"

#include <cstdlib>
#include <initializer_list>

// The focused command test links the two dispatcher translation units without
// the unrelated live input, replay-export, movement-map and reliable-network
// implementations.  These callbacks are unreachable from the exercised group
// publishers but remain referenced by the complete objects on MinGW.
namespace ranker {

bool HasQueuedInputEvent() {
    return false;
}

bool PopInputEvent(InputEvent&) {
    return false;
}

void ResetMode1GameplayVoteCompletionGate() {
}

Mode1ReliableRuntimeState& mode1_reliable_state() {
    static Mode1ReliableRuntimeState state{};
    return state;
}

void BroadcastMode1PacketRange(u32, u32) {
}

const GameplaySessionLoadState& gameplay_session_load_state() {
    static GameplaySessionLoadState state{};
    return state;
}

const std::vector<GameplaySessionExportRecordSpec>& gameplay_session_export_specs() {
    static const std::vector<GameplaySessionExportRecordSpec> specs;
    return specs;
}

bool HandleGameplaySessionBundleExport(
    const char*, const std::vector<TrcWriteRecord>&, u16, u32) {
    return false;
}

u32 UnitMovementMapTileIndex(const UnitMovementMap&, u32, u32) {
    return 0;
}

} // namespace ranker

namespace {

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

ranker::GameplayActionUnitState make_input_structure(
    u32 id, u32 type, u32 owner, bool selected, u32 queued = 0) {
    ranker::GameplayActionUnitState unit{};
    unit.offset = id;
    unit.type = type;
    unit.owner = owner;
    unit.selected = selected;
    unit.flags = selected ? 0x80u : 0u;
    unit.deferred_command_count = queued;
    unit.command_capabilities.push_back(0x12u);
    unit.production_capabilities.push_back(0x05u);
    return unit;
}

ranker::GameplayProductionUnitState make_cost_structure(
    u32 id, u32 type, u32 owner, bool selected, u32 queued = 0) {
    ranker::GameplayProductionUnitState unit{};
    unit.offset = id;
    unit.type = type;
    unit.owner = owner;
    unit.selected = selected;
    unit.status_flags = selected ? 0x80u : 0u;
    unit.deferred_command_count = queued;
    unit.production_cost_actions = {9u, 10u};
    return unit;
}

void require_unit_order(const std::vector<ranker::GameplayPublishedAction>& actions,
    std::initializer_list<u32> expected) {
    require(actions.size() == expected.size());
    std::size_t index = 0;
    for (u32 unit_id : expected) {
        require(actions[index].unit_offset == unit_id);
        ++index;
    }
}

void verify_group_input_commands() {
    ranker::GameplayInputActionState state{};
    state.local_player_index = 2u;
    state.selected_unit_offset = 0x100u;
    state.current_unit_offset = 0x100u;
    state.multi_select_count = 6u;
    state.indexed_payloads = {0x2au};
    state.units.push_back(make_input_structure(0x100u, 0x60u, 2u, true));
    state.units.push_back(make_input_structure(0x101u, 0x60u, 2u, true));
    state.units.push_back(make_input_structure(0x102u, 0x60u, 2u, true, 4u));
    state.units.push_back(make_input_structure(0x103u, 0x60u, 2u, true));
    state.units.back().action_mode_gate = 1u;
    state.units.push_back(make_input_structure(0x200u, 0x61u, 2u, true));
    state.units.push_back(make_input_structure(0x104u, 0x60u, 1u, true));
    state.units.push_back(make_input_structure(0x105u, 0x60u, 2u, false));

    require(ranker::PublishSelectedStructureGroupCapabilityAction(state, 0x12u));
    require_unit_order(state.published_actions, {0x100u, 0x101u, 0x103u});
    for (const ranker::GameplayPublishedAction& action : state.published_actions) {
        require(action.subtype == 0x01u);
        require(action.arg0 == 0x12u);
    }
    require(state.current_unit_offset == 0x100u);

    state.published_actions.clear();
    require(ranker::PublishSelectedStructureGroupIndexedPayloadAction(state, 1u));
    require_unit_order(state.published_actions, {0x100u, 0x101u, 0x103u});
    for (const ranker::GameplayPublishedAction& action : state.published_actions) {
        require(action.subtype == 0x05u);
        require(action.arg0 == 0x2au);
    }

    state.published_actions.clear();
    require(ranker::PublishSelectedStructureGroupProductionAction(state, 0x05u));
    require_unit_order(state.published_actions, {0x100u, 0x101u, 0x103u});
    for (const ranker::GameplayPublishedAction& action : state.published_actions) {
        require(action.subtype == 0x0cu);
        require(action.arg0 == 0x05u);
    }

    state.published_actions.clear();
    require(ranker::PublishSelectedStructureGroupRallyAction(
        state, 0x3456u, 0x4567u, 0x5678u));
    require_unit_order(state.published_actions, {0x100u, 0x101u, 0x102u});
    for (const ranker::GameplayPublishedAction& action : state.published_actions) {
        require(action.subtype == 0x08u);
        require(action.arg0 == 0x1fu);
        require(action.arg1 == 0x3456u);
        require(action.arg2 == 0x4567u);
        require(action.arg3 == 0x5678u);
    }
    require(state.current_unit_offset == 0x100u);
}

void verify_group_cost_commands() {
    ranker::GameplayProductionActionState state{};
    state.local_player_index = 2u;
    state.selected_unit_offset = 0x300u;
    state.current_unit_offset = 0x300u;
    state.selected_count = 5u;
    state.units.push_back(make_cost_structure(0x300u, 0x60u, 2u, true));
    state.units.push_back(make_cost_structure(0x301u, 0x60u, 2u, true));
    state.units.push_back(make_cost_structure(0x302u, 0x60u, 2u, true, 4u));
    state.units.push_back(make_cost_structure(0x400u, 0x61u, 2u, true));
    state.units.push_back(make_cost_structure(0x303u, 0x60u, 1u, true));
    state.units.push_back(make_cost_structure(0x304u, 0x60u, 2u, false));

    require(ranker::PublishSelectedStructureGroupProductionCostAction(state, 9u));
    require_unit_order(state.published_actions, {0x300u, 0x301u});
    for (const ranker::GameplayPublishedAction& action : state.published_actions) {
        require(action.subtype == 0x1au);
        require(action.arg0 == 9u);
        require(action.arg1 == 0u);
        require(action.arg2 == 2u);
        require(action.arg3 == 0u);
    }
    require(state.current_unit_offset == 0x300u);
}

void verify_mobile_group_semantics_are_unchanged() {
    ranker::GameplayInputActionState state{};
    state.local_player_index = 2u;
    state.selected_unit_offset = 0x500u;
    state.current_unit_offset = 0x500u;
    state.multi_select_count = 2u;
    state.units.push_back(make_input_structure(0x500u, 0x20u, 2u, true));
    state.units.push_back(make_input_structure(0x501u, 0x20u, 2u, true));

    require(ranker::PublishSelectedStructureGroupCapabilityAction(state, 0x12u));
    require_unit_order(state.published_actions, {0x500u});
}

} // namespace

int main() {
    static_assert(ranker::ResolveUiOverlayMinimapRightClickItemId(
        2u, 0x60u, 2u, 2u, 0u, 0u, 1u, false) == 0xc9u);
    static_assert(ranker::UiOverlayDoubleClickTargetAllowsGroupSelection(2u, 2u));
    static_assert(!ranker::UiOverlayDoubleClickTargetAllowsGroupSelection(1u, 2u));
    static_assert(ranker::UiOverlayDoubleClickRuntimeClassMatches(
        0x60u, 0x01u, 0x31u));

    verify_group_input_commands();
    verify_group_cost_commands();
    verify_mobile_group_semantics_are_unchanged();
    return 0;
}
