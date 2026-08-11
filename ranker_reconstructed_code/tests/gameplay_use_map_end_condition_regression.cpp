#include "ranker_gameplay_end_conditions.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* message) {
    std::cerr << "GAMEPLAY_USE_MAP_END_CONDITION_FAIL " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

GameplayEndConditionState make_garsian2_start_state(
    PlayerSlotRuntimeState& players) {
    players.slot_states.fill(static_cast<u8>(PlayerSlotState::disabled));
    players.slot_states[0] = static_cast<u8>(PlayerSlotState::active);
    players.slot_states[6] = static_cast<u8>(PlayerSlotState::player_controlled);
    players.slot_states[7] = static_cast<u8>(PlayerSlotState::player_controlled);

    GameplayEndConditionState state{};
    state.players = &players;
    state.frame_counter = 0;
    state.session_mode = 5;
    state.generic_ai_profile_mode = 1;
    state.local_player_slot = 0;
    return state;
}

void verify_zero_use_map_masks_remain_script_controlled() {
    PlayerSlotRuntimeState players{};
    GameplayEndConditionState state = make_garsian2_start_state(players);

    ApplyGameplayEndConditionSessionModeDefaults(state);
    require(state.scenario_victory_condition_mask == 0,
        "mode 5 synthesized a victory condition");
    require(state.scenario_defeat_condition_mask == 0,
        "mode 5 synthesized a defeat condition");

    TickGameplayEndConditionMonitor(state);
    require(!state.end_requested,
        "Garsian 2 ended at frame zero instead of waiting for its triggers");
}

void verify_serialized_use_map_masks_are_preserved() {
    PlayerSlotRuntimeState players{};
    GameplayEndConditionState state = make_garsian2_start_state(players);
    state.scenario_victory_condition_mask = kGameplayEndConditionAnyUnit;
    state.scenario_defeat_condition_mask = kGameplayEndConditionNonEliteUnit;

    ApplyGameplayEndConditionSessionModeDefaults(state);
    require(state.scenario_victory_condition_mask == kGameplayEndConditionAnyUnit,
        "mode 5 replaced the serialized victory mask");
    require(state.scenario_defeat_condition_mask ==
            kGameplayEndConditionNonEliteUnit,
        "mode 5 replaced the serialized defeat mask");
}

void verify_built_in_modes_keep_original_elimination_default() {
    GameplayEndConditionState state{};
    state.session_mode = 0;
    ApplyGameplayEndConditionSessionModeDefaults(state);
    require(state.scenario_victory_condition_mask ==
            kGameplayEndConditionEliteUnit,
        "built-in mode lost its victory default");
    require(state.scenario_defeat_condition_mask ==
            kGameplayEndConditionEliteUnit,
        "built-in mode lost its defeat default");
}

void verify_original_snapshot_cadence_precedes_unit_walk() {
    require(ShouldRefreshGameplayEndConditionSnapshot(0, false),
        "frame zero did not refresh the end-condition snapshot");
    require(!ShouldRefreshGameplayEndConditionSnapshot(1, false),
        "non-cadence frame refreshed the end-condition snapshot");
    require(!ShouldRefreshGameplayEndConditionSnapshot(63, false),
        "frame 63 refreshed the end-condition snapshot");
    require(ShouldRefreshGameplayEndConditionSnapshot(64, false),
        "frame 64 did not refresh the end-condition snapshot");
    require(!ShouldRefreshGameplayEndConditionSnapshot(64, true),
        "scenario override still refreshed the unit snapshot");
}

} // namespace

int main() {
    verify_zero_use_map_masks_remain_script_controlled();
    verify_serialized_use_map_masks_are_preserved();
    verify_built_in_modes_keep_original_elimination_default();
    verify_original_snapshot_cadence_precedes_unit_walk();
    return EXIT_SUCCESS;
}
