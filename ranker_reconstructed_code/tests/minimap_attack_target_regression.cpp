#include "ranker_gameplay_input_actions.h"
#include "ranker_gameplay_packets.h"
#include "ranker_reliable_packets.h"

#include <cstdlib>
#include <iostream>

namespace ranker {

bool HasQueuedInputEvent() {
    return false;
}

bool PopInputEvent(InputEvent&) {
    return false;
}

Mode1ReliableRuntimeState& mode1_reliable_state() {
    static Mode1ReliableRuntimeState state;
    return state;
}

void ResetMode1GameplayVoteCompletionGate() {
}

} // namespace ranker

namespace {

using namespace ranker;

u32 g_dispatched_target = 0xffffffffu;

bool capture_dispatch_target(GameplayInputActionState& state, u32 action_index) {
    if (action_index != 5u) {
        return false;
    }
    g_dispatched_target = state.last_validation_unit_offset;
    return true;
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "MINIMAP_ATTACK_TARGET_FAIL " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    GameplayInputActionState state{};
    state.selector_modes[5] = 2;
    state.selector_enabled[5] = 1;
    state.current_snapshot.field2 = 1; // FUN_004da08a minimap hover kind.
    state.last_validation_unit_offset = 0x1234u; // Previous world-view hit.
    state.map_width_tiles = 64;
    state.map_height_tiles = 64;
    state.callbacks.dispatch_action = capture_dispatch_target;

    const u32 result = DispatchSelectedUnitActionCommand(
        state, 5, 320, 448, 0x100u);

    require(result == 5u, "attack selector changed during minimap dispatch");
    require(g_dispatched_target == 0u,
        "minimap attack inherited a stale world-view unit target");
    require(state.last_validation_unit_offset == 0u,
        "minimap target register mirror was not cleared");
    require(state.last_action_world_x == 320u &&
            state.last_action_world_y == 448u,
        "minimap attack point changed during dispatch");

    std::cout << "MINIMAP_ATTACK_TARGET_PASS\n";
    return EXIT_SUCCESS;
}
