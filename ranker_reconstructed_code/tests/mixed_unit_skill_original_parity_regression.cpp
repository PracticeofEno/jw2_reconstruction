#include "ranker_gameplay_production_actions.h"
#include "ranker_ui_overlay.h"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

using namespace ranker;

constexpr u32 kOrdinaryUnit = 0x100u;
constexpr u32 kFirstCapableUnit = 0x200u;
constexpr u32 kEnemyCapableUnit = 0x300u;
constexpr u32 kUnselectedCapableUnit = 0x400u;
constexpr u32 kInactiveCapableUnit = 0x500u;
constexpr u32 kSecondCapableUnit = 0x600u;
constexpr u32 kUnmarkedCapableUnit = 0x700u;
constexpr u32 kTargetUnit = 0x900u;
constexpr u32 kSelectedMarker = 0x80u;
constexpr u32 kHiddenCommandFlag = 0x800u;

enum class OriginalMixedSelectionPolicy : u8 {
    first_capable,
    all_capable,
    queued_all_capable,
    no_op_success,
    silent_failure,
    hide_off_all_hidden,
};

struct OriginalSelectorEntry {
    u32 handler;
    OriginalMixedSelectionPolicy policy;
};

// Original ranker.exe pointer table at 0x004db238.  Wrapper handlers at
// 0x004db383/0x004db3a5/0x004db3c7/0x004db3f9/0x004db42b/0x004db45a/
// 0x004db55f perform their target precondition and then enter the indicated
// first/all-selected publisher.  Keeping all 32 entries here makes additions
// to the reconstructed command panel fail closed instead of silently changing
// mixed-selection behavior.
constexpr std::array<OriginalSelectorEntry, kGameplayProductionSelectorCount>
    kOriginalSelectors = {{
        {0x004db383u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db47cu, OriginalMixedSelectionPolicy::queued_all_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db55du, OriginalMixedSelectionPolicy::no_op_success},
        {0x004db3a5u, OriginalMixedSelectionPolicy::all_capable},
        {0x004db31du, OriginalMixedSelectionPolicy::all_capable},
        {0x004db3c7u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db31du, OriginalMixedSelectionPolicy::all_capable},
        {0x004db31du, OriginalMixedSelectionPolicy::all_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db31du, OriginalMixedSelectionPolicy::all_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db31du, OriginalMixedSelectionPolicy::all_capable},
        {0x004db31du, OriginalMixedSelectionPolicy::all_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db55fu, OriginalMixedSelectionPolicy::all_capable},
        {0x004db45au, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db42bu, OriginalMixedSelectionPolicy::first_capable},
        {0x004db3f9u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db2b8u, OriginalMixedSelectionPolicy::first_capable},
        {0x004db5dfu, OriginalMixedSelectionPolicy::silent_failure},
        {0x004db4ecu, OriginalMixedSelectionPolicy::hide_off_all_hidden},
    }};

constexpr std::array<u8, kGameplayProductionSelectorCount>
    kOriginalRedirectFlags = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
        1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    };

constexpr std::array<u8, kGameplayProductionSelectorCount>
    kOriginalResultStates = {
        0x88, 0x88, 0x08, 0x88, 0x08, 0x88, 0x08, 0x08,
        0x88, 0x88, 0x08, 0x88, 0x88, 0x88, 0x88, 0x88,
        0x88, 0x08, 0x88, 0x08, 0x08, 0x88, 0x88, 0x08,
        0x88, 0x08, 0x08, 0x08, 0x08, 0x88, 0x00, 0x00,
    };

constexpr bool selector_requires_target(u32 selector) {
    return selector == 0x00u || selector == 0x08u || selector == 0x0au ||
        selector == 0x18u || selector == 0x19u || selector == 0x1bu ||
        selector == 0x1cu;
}

[[noreturn]] void fail(u32 selector, const std::string& message) {
    std::cerr << "MIXED_UNIT_SKILL_ORIGINAL_PARITY_FAIL selector=0x"
              << std::hex << std::setw(2) << std::setfill('0') << selector
              << " " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require_selector(bool condition, u32 selector, const std::string& message) {
    if (!condition) {
        fail(selector, message);
    }
}

GameplayProductionUnitState make_source_unit(u32 offset, u32 owner,
    bool selected, bool active, bool marked, bool capable, u32 selector,
    i32 x) {
    GameplayProductionUnitState unit{};
    unit.offset = offset;
    unit.type = 0x20u + (offset >> 8);
    unit.owner = owner;
    unit.status_flags = marked ? kSelectedMarker : 0u;
    unit.production_bits = capable ? (1ull << selector) : 0u;
    unit.active_count_metric = 1;
    unit.queued_count_metric = 1;
    unit.resource_metric = 1;
    unit.x = x;
    unit.y = 0x80;
    unit.selected = selected;
    unit.active = active;
    return unit;
}

bool select_target(GameplayProductionActionState& state, u32 selector,
    i32 world_x, i32 world_y) {
    (void)selector;
    (void)world_x;
    (void)world_y;
    state.last_validation_unit_offset = kTargetUnit;
    return true;
}

GameplayProductionActionState make_mixed_state(u32 selector) {
    GameplayProductionActionState state{};
    InitializeOriginalGameplayProductionSelectorTables(state);
    state.local_player_index = 0;
    state.selected_unit_offset = kOrdinaryUnit;
    state.current_unit_offset = kOrdinaryUnit;
    state.selected_count = 6;
    state.last_world_x = 0x10;
    state.last_world_y = 0x10;
    state.map_width_tiles = 1;
    state.map_height_tiles = 1;
    state.callbacks.validate_low_action = select_target;

    state.definitions.resize(kGameplayProductionSelectorCount);
    for (u32 index = 0; index < kGameplayProductionSelectorCount; ++index) {
        GameplayProductionActionDefinition& definition = state.definitions[index];
        definition.mode = selector_requires_target(index) ? 3u : 0u;
        definition.allowed_movement_class_mask = 0xffffffffu;
        definition.owner_requirement = 0xffffffffu;
        // FUN_004db92c accepts when each definition-side threshold is below
        // the corresponding live-unit metric.
        definition.active_limit = 0;
        definition.queued_limit = 0;
        definition.resource_limit = 0;
    }

    state.units.push_back(make_source_unit(kOrdinaryUnit, 0, true, true,
        true, false, selector, 0x40));
    state.units.back().command_flags = kHiddenCommandFlag;
    state.units.push_back(make_source_unit(kFirstCapableUnit, 0, true, true,
        true, true, selector, 0x60));
    state.units.back().command_flags = kHiddenCommandFlag;
    state.units.push_back(make_source_unit(kEnemyCapableUnit, 1, true, true,
        true, true, selector, 0x80));
    state.units.push_back(make_source_unit(kUnselectedCapableUnit, 0, false,
        true, true, true, selector, 0xa0));
    state.units.push_back(make_source_unit(kInactiveCapableUnit, 0, true, false,
        true, true, selector, 0xc0));
    state.units.push_back(make_source_unit(kSecondCapableUnit, 0, true, true,
        true, true, selector, 0xe0));
    state.units.back().command_flags = kHiddenCommandFlag;
    state.units.push_back(make_source_unit(kUnmarkedCapableUnit, 0, true, true,
        false, true, selector, 0x100));

    GameplayProductionUnitState target{};
    target.offset = kTargetUnit;
    target.type = 0x31u;
    target.owner = 1;
    target.definition_action_flags = 0x70u;
    target.movement_class = 0;
    target.x = 0x120;
    target.y = 0x80;
    target.active = true;
    state.units.push_back(target);
    state.owner_relation_masks[1] = 1u << state.local_player_index;

    state.placement_map.width = 1;
    state.placement_map.height = 1;
    state.placement_map.cells.resize(1);
    state.placement_map.cells[0].owner_flags = 0x80000000u;
    state.placement_map.cells[0].route_flags = 0x10000000u;
    return state;
}

std::vector<u32> expected_units(OriginalMixedSelectionPolicy policy,
    bool capable_primary) {
    switch (policy) {
    case OriginalMixedSelectionPolicy::first_capable:
        return {capable_primary ? kSecondCapableUnit : kFirstCapableUnit};
    case OriginalMixedSelectionPolicy::all_capable:
    case OriginalMixedSelectionPolicy::queued_all_capable:
        return {kFirstCapableUnit, kSecondCapableUnit};
    case OriginalMixedSelectionPolicy::hide_off_all_hidden:
        return {kOrdinaryUnit, kFirstCapableUnit, kSecondCapableUnit};
    case OriginalMixedSelectionPolicy::no_op_success:
    case OriginalMixedSelectionPolicy::silent_failure:
        return {};
    }
    return {};
}

void verify_original_selector_metadata() {
    GameplayProductionActionState state{};
    InitializeOriginalGameplayProductionSelectorTables(state);
    for (u32 selector = 0; selector < kGameplayProductionSelectorCount;
         ++selector) {
        require_selector(state.selector_redirect_flags[selector] ==
                kOriginalRedirectFlags[selector], selector,
            "redirect flag differs from ranker.exe DAT_008629be");
        require_selector(state.selector_result_states[selector] ==
                kOriginalResultStates[selector], selector,
            "result state differs from ranker.exe DAT_00862a14");
        require_selector(state.selector_definition_indices[selector] == selector,
            selector, "selector no longer maps to its original definition slot");
        require_selector(kOriginalSelectors[selector].handler != 0, selector,
            "original handler audit entry is missing");
    }
}

void verify_p2p_hurdle_authority_gate() {
    constexpr u32 kSelector = 0x17u;
    GameplayProductionActionState state = make_mixed_state(kSelector);
    state.local_player_index = 1;
    state.units.front().owner = 1;
    GameplayProductionPlacementCell& cell = state.placement_map.cells[0];

    // Only the command source owner has explored the cell.
    cell.route_flags = 0x10000000u;
    cell.visibility_owner_flags = 1u << 1;

    // Regular building preview/click paths leave the authority selector
    // disabled and must keep accepting the local player's explored cell.
    require_selector(CheckPreviewProductionPlacementFootprintGateCells(
            state, 0x61u, 0, 0, kOrdinaryUnit), kSelector,
        "ordinary local building placement used the remote authority view");

    // A locally issued Hurdle must not leave this peer when any active P2P
    // participant would fail the original process-local explored-fog test.
    const u32 all_active_players = (1u << 0) | (1u << 1);
    require_selector(ResolvePreviewPlacementAuthorityMask(1, 1,
            all_active_players) == all_active_players, kSelector,
        "local Hurdle did not select the all-player explored intersection");
    require_selector(!CheckPreviewProductionPlacementFootprintGateCellsForOwnerMask(
            state, 0x7du, 0, 0, kOrdinaryUnit, all_active_players), kSelector,
        "local Hurdle accepted a cell unexplored by another active player");
    require_selector(state.preview_placement_authority_player == 0xffffffffu,
        kSelector, "failed all-player Hurdle check leaked its authority owner");

    cell.visibility_owner_flags = all_active_players;
    require_selector(CheckPreviewProductionPlacementFootprintGateCellsForOwnerMask(
            state, 0x7du, 0, 0, kOrdinaryUnit, all_active_players), kSelector,
        "local Hurdle rejected the all-player explored intersection");
    require_selector(state.preview_placement_authority_player == 0xffffffffu,
        kSelector, "successful all-player Hurdle check leaked its authority owner");

    // A receiving reconstructed peer must reproduce an original sender's
    // accepted local decision from the sender-owner layer.  Applying the
    // receiver's all-player mask here would reject a packet already accepted
    // and emitted by the unmodified original.
    state.local_player_index = 0;
    cell.route_flags = 0;
    const u32 remote_authority = ResolvePreviewPlacementAuthorityMask(
        state.local_player_index, 1, all_active_players);
    require_selector(remote_authority == (1u << 1), kSelector,
        "remote Hurdle did not retain the original sender's authority");
    require_selector(CheckPreviewProductionPlacementFootprintGateCellsForOwnerMask(
            state, 0x7du, 0, 0, kOrdinaryUnit, remote_authority), kSelector,
        "Hurdle rejected a cell explored by the command source owner");

    cell.visibility_owner_flags = 0;
    require_selector(!CheckPreviewProductionPlacementFootprintGateCellsForOwnerMask(
            state, 0x7du, 0, 0, kOrdinaryUnit, remote_authority), kSelector,
        "Hurdle accepted a cell unexplored by the command source owner");

    // Verify that the local all-player failure happens before the command is
    // published, which also keeps effect allocation, cost debit and RNG out
    // of the divergent simulation path.
    state = make_mixed_state(kSelector);
    state.preview_placement_required_owner_mask = all_active_players;
    state.placement_map.cells[0].visibility_owner_flags = 1u << 0;
    DispatchOwnerProductionActionCommand(state, kSelector, 0, 0,
        kOrdinaryUnit);
    require_selector(state.last_dispatch_failed, kSelector,
        "unsafe local Hurdle command was not rejected before dispatch");
    require_selector(state.published_actions.empty() &&
            state.queued_commands.empty(), kSelector,
        "unsafe local Hurdle command emitted a P2P action");
}

void verify_production_action_target_mode_encoding() {
    for (u32 selector = 0; selector < kGameplayProductionSelectorCount;
         ++selector) {
        const u32 mode = UiOverlayProductionActionMode(selector);
        require_selector(IsUiOverlayProductionActionMode(mode), selector,
            "nonzero-mode spell did not enter the original target range");
        require_selector(UiOverlayProductionActionSelector(mode) == selector,
            selector, "target mode did not round-trip to its spell selector");
    }
    require_selector(!IsUiOverlayProductionActionMode(
            kUiOverlayProductionActionModeBase - 1u), 0,
        "target-mode lower boundary accepted a non-spell command");
    require_selector(!IsUiOverlayProductionActionMode(
            kUiOverlayProductionActionModeBase +
                kGameplayProductionSelectorCount), 0,
        "target-mode upper boundary accepted a non-spell command");
}

void verify_passenger_icon_unload_packet() {
    constexpr u32 kPassenger = 0x1200u;
    constexpr u32 kCarrier = 0x3400u;
    GameplayProductionActionState state{};
    state.local_player_index = 2;
    state.current_unit_offset = kPassenger;

    GameplayProductionUnitState passenger{};
    passenger.offset = kPassenger;
    passenger.command_state = 0x45u;
    passenger.command_target_offset = kCarrier;
    passenger.command_target_lockout_ticks = 0;
    state.units.push_back(passenger);

    require_selector(PublishLinkedUnitCommand24IfIdle(state), 0x24u,
        "idle passenger icon did not publish an unload command");
    require_selector(state.published_actions.size() == 1u, 0x24u,
        "passenger icon published an unexpected packet count");
    const GameplayPublishedAction& action = state.published_actions.front();
    require_selector(action.subtype == 0x02u, 0x24u,
        "passenger icon used the wrong packet subtype");
    require_selector(action.unit_offset == kPassenger, 0x24u,
        "passenger icon assigned command 0x24 to the carrier");
    require_selector(action.arg0 == 0x24u && action.arg1 == kCarrier, 0x24u,
        "passenger icon did not encode its target carrier");

    state.published_actions.clear();
    state.units.front().command_target_lockout_ticks = 1;
    require_selector(!PublishLinkedUnitCommand24IfIdle(state), 0x24u,
        "passenger ignored the carrier +0xac lockout gate");
    require_selector(state.published_actions.empty(), 0x24u,
        "locked carrier still received an unload packet");
}

void verify_mixed_selection_dispatch(u32 selector, bool capable_primary) {
    GameplayProductionActionState state = make_mixed_state(selector);
    const OriginalMixedSelectionPolicy policy =
        kOriginalSelectors[selector].policy;
    const u32 primary = capable_primary ?
        kSecondCapableUnit : kOrdinaryUnit;
    DispatchOwnerProductionActionCommand(state, selector, 0x10, 0x10, primary);

    const bool expected_failure =
        policy == OriginalMixedSelectionPolicy::silent_failure;
    require_selector(state.last_dispatch_failed == expected_failure, selector,
        expected_failure ? "silent-failure selector unexpectedly succeeded" :
            "originally successful selector was rejected");

    const std::vector<u32> expected = expected_units(policy, capable_primary);
    require_selector(state.published_actions.size() == expected.size(), selector,
        "published packet count differs from the original mixed-selection policy");

    for (std::size_t index = 0; index < expected.size(); ++index) {
        const GameplayPublishedAction& action = state.published_actions[index];
        require_selector(action.subtype == 0x09u, selector,
            "skill did not emit an extended-unit-order packet");
        require_selector(action.unit_offset == expected[index], selector,
            "skill packet was assigned to the wrong mixed-party unit");
        const u32 expected_command =
            policy == OriginalMixedSelectionPolicy::hide_off_all_hidden ?
                0x13u : selector;
        const u32 expected_aux =
            policy == OriginalMixedSelectionPolicy::hide_off_all_hidden ?
                0xffffffffu :
                (selector_requires_target(selector) ? kTargetUnit : 0u);
        require_selector(action.arg0 == expected_command, selector,
            "skill command id differs from ranker.exe");
        require_selector(action.arg1 == expected_aux, selector,
            "skill target/auxiliary id differs from ranker.exe");
    }

    const GameplayProductionUnitState& target = state.units.back();
    if (selector_requires_target(selector)) {
        require_selector(target.result_state == kOriginalResultStates[selector],
            selector, "target result state differs from ranker.exe");
    }
    else {
        require_selector(target.result_state == 0, selector,
            "non-target skill unexpectedly mutated the target result state");
    }

    if (selector == 0x13u) {
        require_selector(expected ==
                std::vector<u32>({kFirstCapableUnit, kSecondCapableUnit}),
            selector, "Hide reached a unit without the Hide capability bit");
    }
}

} // namespace

int main() {
    verify_original_selector_metadata();
    verify_p2p_hurdle_authority_gate();
    verify_production_action_target_mode_encoding();
    verify_passenger_icon_unload_packet();
    for (u32 selector = 0; selector < kGameplayProductionSelectorCount;
         ++selector) {
        // The first pass proves fallback from a non-capable primary selection;
        // the second proves that original first-unit actions prefer a capable
        // primary even when another capable unit appears earlier in the list.
        verify_mixed_selection_dispatch(selector, false);
        verify_mixed_selection_dispatch(selector, true);
    }
    std::cout << "MIXED_UNIT_SKILL_ORIGINAL_PARITY_PASS selectors=32 cases=64\n";
    return EXIT_SUCCESS;
}
