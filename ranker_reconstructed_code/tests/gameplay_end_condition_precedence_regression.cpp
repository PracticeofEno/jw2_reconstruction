#include "ranker_gameplay_end_conditions.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "GAMEPLAY_END_CONDITION_PRECEDENCE_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

PlayerSlotRuntimeState two_player_slots() {
    PlayerSlotRuntimeState players{};
    players.slot_states.fill(static_cast<u8>(PlayerSlotState::disabled));
    players.slot_states[0] = static_cast<u8>(PlayerSlotState::active);
    players.slot_states[1] = static_cast<u8>(PlayerSlotState::active);
    players.global_active_slot_mask = 0x03;
    players.active_slot_count = 2;
    players.local_player_slot = 0;
    players.owner_relation_masks[0] = 0x01;
    players.owner_relation_masks[1] = 0x02;
    return players;
}

void test_local_defeat_is_not_overwritten_by_same_tick_victory() {
    PlayerSlotRuntimeState players = two_player_slots();
    GameplayEndConditionState state{};
    state.players = &players;
    state.frame_counter = 0x740;
    state.generic_ai_profile_mode = 1;
    state.local_player_slot = 0;

    // With both elite lists empty, the normal-mode defeat and victory
    // predicates are simultaneously true.  FUN_004d55c0 takes the defeat
    // branch immediately and never evaluates victory in the same tick.
    TickGameplayEndConditionMonitor(state);

    require(state.end_requested, "empty local elite list did not request termination");
    require(state.result_code == kGameplayEndResultDefeat,
        "same-tick victory overwrote the local defeat result");
}

void test_victory_still_runs_when_local_elite_survives() {
    PlayerSlotRuntimeState players = two_player_slots();
    GameplayEndUnit local_elite{0, 0x60, 0};
    GameplayEndConditionState state{};
    state.players = &players;
    state.active_units.push_back(&local_elite);
    state.frame_counter = 0x740;
    state.generic_ai_profile_mode = 1;
    state.local_player_slot = 0;

    TickGameplayEndConditionMonitor(state);

    require(state.end_requested, "missing hostile elite did not request victory");
    require(state.result_code == kGameplayEndResultVictory,
        "victory result changed when the local elite survived");
}

} // namespace

int main() {
    test_local_defeat_is_not_overwritten_by_same_tick_victory();
    test_victory_still_runs_when_local_elite_survives();
    std::cout << "GAMEPLAY_END_CONDITION_PRECEDENCE_PASS "
                 "defeat=terminal victory=reachable\n";
    return EXIT_SUCCESS;
}
