#include "ranker_gameplay_production_actions.h"
#include "ranker_reliable_packets.h"
#include "ranker_runtime_resources.h"
#include "ranker_unit_movement.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace ranker {

Mode1ReliableRuntimeState& mode1_reliable_state() {
    static Mode1ReliableRuntimeState state{};
    return state;
}

void BroadcastMode1PacketRange(u32, u32) {}

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

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "PLACEMENT_PREVIEW_BOUNDARY_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

GameplayProductionActionState make_passable_class3_map() {
    GameplayProductionActionState state{};
    state.local_player_index = 0;
    state.preview_placement_terrain_class = 3;
    state.placement_map.width = 2;
    state.placement_map.height = 3;
    state.placement_map.cells.resize(6);
    for (GameplayProductionPlacementCell& cell : state.placement_map.cells) {
        // FUN_004dbae2: terrain class 3, valid terrain and required route,
        // with none of the blocked/temporary/collision bits present.
        cell.owner_flags = 0x80000000u | (3u << 26);
        cell.route_flags = 0x10000000u;
    }
    return state;
}

void test_negative_origin_continues_each_footprint_cell() {
    GameplayProductionActionState state = make_passable_class3_map();
    constexpr std::array<std::array<i32, 2>, 4> cells{{
        {{-1, 1}}, {{0, 1}}, {{-1, 2}}, {{0, 2}},
    }};
    constexpr std::array<bool, 4> expected{{false, true, false, true}};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        require(CheckPreviewProductionPlacementGateCell(state,
                    cells[i][0], cells[i][1], 0, false) == expected[i],
            "negative origin did not preserve per-cell boundary validity");
    }
}

void test_right_and_bottom_edges_are_individually_clipped() {
    GameplayProductionActionState state = make_passable_class3_map();
    require(CheckPreviewProductionPlacementGateCell(state, 1, 2, 0, false),
        "last in-map cell should remain preview-valid");
    require(!CheckPreviewProductionPlacementGateCell(state, 2, 2, 0, false),
        "right out-of-map cell should be preview-invalid");
    require(!CheckPreviewProductionPlacementGateCell(state, 1, 3, 0, false),
        "bottom out-of-map cell should be preview-invalid");
}

void test_deferred_ability_gate_refreshes_live_unit_state() {
    GameplayProductionActionState state{};
    InitializeOriginalGameplayProductionSelectorTables(state);
    state.definitions.resize(kGameplayProductionSelectorCount);
    constexpr u32 kSelector = 5;
    GameplayProductionActionDefinition& action = state.definitions[kSelector];
    action.active_limit = 6;
    action.queued_limit = 200;
    action.resource_limit = 0;

    GameplayProductionUnitState stale{};
    stale.offset = 0x75d0;
    stale.owner = 0;
    stale.production_bits = 0;
    stale.active_count_metric = 0;
    stale.queued_count_metric = 0;
    stale.resource_metric = 0;
    state.units.push_back(stale);
    state.current_unit_offset = 0x1234;

    UnitMovementUnit live{};
    live.id = stale.offset;
    live.owner_id = 1;
    live.script_bit_flags = 1u << kSelector;
    live.status_timer = 7;
    live.secondary_value = 10'000;
    live.health = 210;

    require(CheckLiveUnitProductionActionGate(state, live, kSelector),
        "deferred ability gate used the stale UI/production mirror");
    require(state.current_unit_offset == 0x1234,
        "live ability gate did not restore the caller's selected unit");
    const GameplayProductionUnitState& refreshed = state.units.front();
    require(refreshed.owner == live.owner_id &&
            refreshed.production_bits == live.script_bit_flags &&
            refreshed.active_count_metric == live.status_timer &&
            refreshed.queued_count_metric == live.secondary_value &&
            refreshed.resource_metric == live.health,
        "live ability gate did not refresh every raw gate field");

    live.secondary_value = action.queued_limit - 1;
    require(!CheckLiveUnitProductionActionGate(state, live, kSelector) &&
            state.last_gate_failure == GameplayProductionGateFailure::queued_limit,
        "live ability gate ignored the current secondary-resource threshold");
}

} // namespace

int main() {
    test_negative_origin_continues_each_footprint_cell();
    test_right_and_bottom_edges_are_individually_clipped();
    test_deferred_ability_gate_refreshes_live_unit_state();
    std::cout << "PLACEMENT_PREVIEW_BOUNDARY_PASS negative=0/1/0/1 "
                 "right-bottom=per-cell terrain-class=3 "
                 "live-ability-gate=refreshed\n";
    return EXIT_SUCCESS;
}
