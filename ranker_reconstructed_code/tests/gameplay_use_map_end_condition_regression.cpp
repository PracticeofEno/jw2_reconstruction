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

// Building elimination: on a Use Map Setting map whose imported masks are
// unit-based (0x1), a Computer whose buildings are all razed must count as
// eliminated even while a stray mobile unit survives — and the same rule
// defeats the local player once their own last building falls.
void verify_building_elimination_ends_use_map_game() {
    PlayerSlotRuntimeState players{};
    players.slot_states.fill(static_cast<u8>(PlayerSlotState::disabled));
    players.slot_states[0] = static_cast<u8>(PlayerSlotState::active);
    players.slot_states[1] = static_cast<u8>(PlayerSlotState::player_controlled);
    // Each owner is related to itself (the live tables always carry the self
    // bit); nobody is allied with anybody else.
    players.owner_relation_masks[0] = 1u << 0;
    players.owner_relation_masks[1] = 1u << 1;

    GameplayEndConditionState state{};
    state.players = &players;
    state.session_mode = 5;
    state.generic_ai_profile_mode = 1;
    state.local_player_slot = 0;
    state.scenario_victory_condition_mask = kGameplayEndConditionAnyUnit;
    state.scenario_defeat_condition_mask = kGameplayEndConditionAnyUnit;

    GameplayEndUnit local_nest{0, 0x80, 0};
    GameplayEndUnit local_worker{0, 0x20, 0};
    GameplayEndUnit enemy_nest{1, 0x80, 0};
    GameplayEndUnit enemy_fighter{1, 0x21, 0};
    state.active_units = {&local_nest, &local_worker, &enemy_nest, &enemy_fighter};

    state.frame_counter = 0x800;
    TickGameplayEndConditionMonitor(state);
    require(!state.end_requested, "game ended while both sides held buildings");

    // Enemy nest razed, fighter still alive -> the Computer is eliminated and
    // the local player wins on the next monitor tick.
    enemy_nest.state_flags = kGameplayEndDeadUnitFlag;
    state.frame_counter = 0x840;
    TickGameplayEndConditionMonitor(state);
    require(state.end_requested && state.result_code == kGameplayEndResultVictory,
        "razed Computer with a stray unit did not end the game in victory");

    // Symmetric: the local player's last building falls -> defeat, even with
    // a surviving worker.
    GameplayEndConditionState mirror{};
    mirror.players = &players;
    mirror.session_mode = 5;
    mirror.generic_ai_profile_mode = 1;
    mirror.local_player_slot = 0;
    mirror.scenario_victory_condition_mask = kGameplayEndConditionAnyUnit;
    mirror.scenario_defeat_condition_mask = kGameplayEndConditionAnyUnit;
    GameplayEndUnit my_nest{0, 0x80, 0};
    GameplayEndUnit my_worker{0, 0x20, 0};
    GameplayEndUnit their_nest{1, 0x80, 0};
    mirror.active_units = {&my_nest, &my_worker, &their_nest};
    mirror.frame_counter = 0x800;
    TickGameplayEndConditionMonitor(mirror);
    require(!mirror.end_requested, "mirror ended early");
    my_nest.state_flags = kGameplayEndDeadUnitFlag;
    mirror.frame_counter = 0x840;
    TickGameplayEndConditionMonitor(mirror);
    require(mirror.end_requested && mirror.result_code == kGameplayEndResultDefeat,
        "losing the last own building did not end the game in defeat");

    // A unit-only owner that never had a building is NOT eliminated (scenario
    // hero players keep the map's own rules).
    GameplayEndConditionState hero{};
    hero.players = &players;
    hero.session_mode = 5;
    hero.generic_ai_profile_mode = 1;
    hero.local_player_slot = 0;
    hero.scenario_victory_condition_mask = kGameplayEndConditionAnyUnit;
    GameplayEndUnit hero_nest{0, 0x80, 0};
    GameplayEndUnit enemy_hero{1, 0x21, 0};
    hero.active_units = {&hero_nest, &enemy_hero};
    hero.frame_counter = 0x800;
    TickGameplayEndConditionMonitor(hero);
    require(!hero.end_requested,
        "a unit-only opponent that never built was wrongly eliminated");
}

// Live melee vs Computer, masks exactly as the session builds them: every
// owner has only its self relation bit and the global-active mask carries the
// Computer (player_controlled) slot but NOT the human (active) slot.  Razing
// the Computer's last building must be a victory for the human.
void verify_melee_victory_with_human_slot_outside_global_active_mask() {
    PlayerSlotRuntimeState players{};
    players.slot_states.fill(static_cast<u8>(PlayerSlotState::disabled));
    players.slot_states[0] = static_cast<u8>(PlayerSlotState::active);
    players.slot_states[1] = static_cast<u8>(PlayerSlotState::player_controlled);
    players.owner_relation_masks[0] = 1u << 0;
    players.owner_relation_masks[1] = 1u << 1;
    players.global_active_slot_mask = 1u << 1;  // Computer only (melee)

    GameplayEndConditionState state{};
    state.players = &players;
    state.session_mode = 1;  // melee
    state.generic_ai_profile_mode = 1;
    state.local_player_slot = 0;
    ApplyGameplayEndConditionSessionModeDefaults(state);

    GameplayEndUnit local_nest{0, 0x80, 0};
    GameplayEndUnit local_worker{0, 0x20, 0};
    GameplayEndUnit enemy_nest{1, 0x80, 0};
    state.active_units = {&local_nest, &local_worker, &enemy_nest};
    state.frame_counter = 0x800;
    TickGameplayEndConditionMonitor(state);
    require(!state.end_requested, "melee ended while the Computer still had a nest");

    enemy_nest.state_flags = kGameplayEndDeadUnitFlag;
    state.frame_counter = 0x840;
    TickGameplayEndConditionMonitor(state);
    require(state.end_requested && state.result_code == kGameplayEndResultVictory,
        "razing the Computer's last building did not win the melee game");
}

int main() {
    verify_building_elimination_ends_use_map_game();
    verify_melee_victory_with_human_slot_outside_global_active_mask();
    verify_zero_use_map_masks_remain_script_controlled();
    verify_serialized_use_map_masks_are_preserved();
    verify_built_in_modes_keep_original_elimination_default();
    verify_original_snapshot_cadence_precedes_unit_walk();
    return EXIT_SUCCESS;
}
