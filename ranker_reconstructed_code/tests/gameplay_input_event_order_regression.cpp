#include "ranker_gameplay_input_actions.h"
#include "ranker_gameplay_packets.h"
#include "ranker_reliable_packets.h"

#include <cstdlib>
#include <iostream>
#include <vector>

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

std::vector<InputEvent> g_events;
std::size_t g_read_index = 0;
std::vector<u32> g_dispatched_producers;
bool g_pending_command = false;
u32 g_pending_producer = 0;
u32 g_finalize_count = 0;

bool has_event(GameplayInputActionState&) {
    return g_read_index < g_events.size();
}

bool pop_event(GameplayInputActionState&, InputEvent& event) {
    if (g_read_index >= g_events.size()) {
        return false;
    }
    event = g_events[g_read_index++];
    return true;
}

void handle_keyboard(GameplayInputActionState& state, const InputEvent& event) {
    state.selected_unit_offset = event.code;
}

void handle_pointer(GameplayInputActionState& state, const InputEvent&) {
    g_pending_command = true;
    g_pending_producer = state.selected_unit_offset;
}

void finalize_event(GameplayInputActionState&) {
    ++g_finalize_count;
    if (!g_pending_command) {
        return;
    }
    g_dispatched_producers.push_back(g_pending_producer);
    g_pending_command = false;
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "GAMEPLAY_INPUT_EVENT_ORDER_FAIL " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

InputEvent keyboard(u32 code) {
    InputEvent event{};
    event.kind = InputEventKind::keyboard;
    event.code = code;
    return event;
}

InputEvent pointer() {
    InputEvent event{};
    event.kind = InputEventKind::mouse;
    event.message = 0x0202u;
    return event;
}

} // namespace

int main() {
    GameplayInputActionState state{};
    state.callbacks.has_pending_input_event = has_event;
    state.callbacks.pop_input_event = pop_event;
    state.callbacks.handle_keyboard_event = handle_keyboard;
    state.callbacks.handle_pointer_event = handle_pointer;
    state.callbacks.finalize_input_event = finalize_event;

    // Model the reported sequence: group 2, production click, group 3,
    // production click.  Each click must be committed before the next group
    // selection is allowed to replace the current producer.
    g_events = {keyboard(2u), pointer(), keyboard(3u), pointer()};
    DrainGameplayInputEvents(state);

    require(g_dispatched_producers.size() == 2u,
        "both rapid production clicks were not dispatched");
    require(g_dispatched_producers[0] == 2u,
        "first click observed a later control-group selection");
    require(g_dispatched_producers[1] == 3u,
        "second click did not preserve its selected producer");
    require(g_finalize_count == g_events.size(),
        "input records were not finalized one at a time");

    // Even a keyboard record filtered by a modal gate remains an input record
    // boundary; any command created by a preceding handler must not leak past
    // it if a custom producer injects such a sequence.
    g_events = {keyboard(0x20u)};
    g_read_index = 0;
    g_finalize_count = 0;
    state.keyboard_filter_active = true;
    DrainGameplayInputEvents(state);
    require(g_finalize_count == 1u,
        "filtered keyboard record skipped the event boundary");

    std::cout << "GAMEPLAY_INPUT_EVENT_ORDER_PASS\n";
    return EXIT_SUCCESS;
}
