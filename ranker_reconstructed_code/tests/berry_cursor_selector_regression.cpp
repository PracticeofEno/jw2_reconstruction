#include "ranker_gameplay_context_cursor.h"

#include <iostream>

namespace {

using namespace ranker;

bool expect(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

GameplayContextCursorInput berry_hover_input() {
    GameplayContextCursorInput input{};
    input.current_tick_ms = 1000u;
    input.hover_kind = 0x0cu;
    input.current_mode = 0u;
    input.selected_unit_present = true;
    input.selected_unit_local = true;
    input.selected_unit_type = 0x20u;
    input.selected_unit_command_bit_mask = 0x80u;
    return input;
}

bool test_harvest_capable_local_mobile_uses_fixed_berry_cursor() {
    GameplayContextCursorState state{};
    state.animation_frame = 5u;
    state.animation_tick_ms = 900u;

    GameplayContextCursorInput input = berry_hover_input();
    const GameplayContextCursorResolution first =
        ResolveGameplayContextCursor(state, input);

    bool ok = true;
    ok &= expect(first.applied, "berry cursor result was not applied");
    ok &= expect(first.cursor_index == 0x24u,
        "harvest-capable berry hover did not select cursor frame 0x24");
    ok &= expect(state.cursor_index == 0x24u &&
            state.cursor_base_index == 0x24u,
        "berry cursor frame was not published as its fixed base");
    ok &= expect(state.animation_mode == 7u,
        "berry cursor did not enter original target mode 7");
    ok &= expect(state.target_kind == 0x0cu,
        "berry cursor did not retain terrain target kind 0x0c");
    ok &= expect(state.animation_frame == 5u &&
            state.animation_tick_ms == 900u,
        "fixed berry cursor unexpectedly rewrote the animation clock");

    input.current_tick_ms = 100000u;
    const GameplayContextCursorResolution later =
        ResolveGameplayContextCursor(state, input);
    ok &= expect(later.applied && later.cursor_index == 0x24u,
        "berry cursor changed frame after elapsed ticks");
    ok &= expect(state.animation_frame == 5u &&
            state.animation_tick_ms == 900u,
        "tick advance animated the original fixed berry cursor");
    return ok;
}

bool test_non_harvest_mobile_uses_ordinary_context_cursor() {
    GameplayContextCursorState state{};
    GameplayContextCursorInput input = berry_hover_input();
    input.selected_unit_command_bit_mask = 0u;

    const GameplayContextCursorResolution first =
        ResolveGameplayContextCursor(state, input);
    bool ok = true;
    ok &= expect(first.applied && first.cursor_index == 0x40u,
        "non-harvest mobile did not use ordinary contextual cursor 0x40");
    ok &= expect(state.animation_mode == 4u,
        "non-harvest mobile did not enter ordinary cursor animation mode 4");

    input.current_tick_ms += 100u;
    const GameplayContextCursorResolution second =
        ResolveGameplayContextCursor(state, input);
    ok &= expect(second.applied && second.cursor_index == 0x41u,
        "ordinary non-harvest cursor did not advance on its 100 ms boundary");
    return ok;
}

bool test_nonlocal_or_missing_selection_cannot_claim_berry_cursor() {
    bool ok = true;

    GameplayContextCursorState remote_state{};
    GameplayContextCursorInput remote = berry_hover_input();
    remote.selected_unit_local = false;
    const GameplayContextCursorResolution remote_result =
        ResolveGameplayContextCursor(remote_state, remote);
    ok &= expect(remote_result.applied && remote_result.cursor_index == 0u,
        "remote selection incorrectly received the berry cursor");

    GameplayContextCursorState missing_state{};
    GameplayContextCursorInput missing = berry_hover_input();
    missing.selected_unit_present = false;
    const GameplayContextCursorResolution missing_result =
        ResolveGameplayContextCursor(missing_state, missing);
    ok &= expect(missing_result.applied && missing_result.cursor_index == 0u,
        "missing selection incorrectly received the berry cursor");

    GameplayContextCursorState structure_state{};
    GameplayContextCursorInput structure = berry_hover_input();
    structure.selected_unit_type = 0x60u;
    const GameplayContextCursorResolution structure_result =
        ResolveGameplayContextCursor(structure_state, structure);
    ok &= expect(structure_result.applied && structure_result.cursor_index == 0u,
        "non-mobile selection incorrectly received the berry cursor");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_harvest_capable_local_mobile_uses_fixed_berry_cursor();
    ok &= test_non_harvest_mobile_uses_ordinary_context_cursor();
    ok &= test_nonlocal_or_missing_selection_cannot_claim_berry_cursor();
    if (!ok) {
        return 1;
    }

    std::cout << "BERRY_CURSOR_SELECTOR_REGRESSION_PASS "
                 "cursor=0x24 mode=7 target=0x0c fixed=yes "
                 "negative=nonharvest/nonlocal/missing/nonmobile\n";
    return 0;
}
