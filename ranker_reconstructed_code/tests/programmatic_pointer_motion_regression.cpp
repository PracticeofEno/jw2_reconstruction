#include "ranker_cursor.h"
#include "ranker_gameplay_input_actions.h"
#include "ranker_input.h"
#include "ranker_screenshot.h"

#include <cstdlib>
#include <iostream>

namespace ranker {

GameplayInputActionState& gameplay_input_action_state() {
    static GameplayInputActionState state{};
    return state;
}

void ResetGameplayInputSnapshotRing(GameplayInputActionState&) {}
bool PopGameplayInputSnapshot(GameplayInputActionState&) { return false; }
bool PushGameplayInputSnapshot(GameplayInputActionState&) { return true; }
bool PushGameplayInputSnapshot(
    GameplayInputActionState&, const GameplayInputSnapshot&) { return true; }

SoftwareCursorState& software_cursor_state() {
    static SoftwareCursorState state{};
    return state;
}

void SetGameCursorPointerPosition(i32, i32) {}
void RequestScreenshotCapture() {}
void SetContinuousScreenshotCapture(bool) {}

} // namespace ranker

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* message) {
    std::cerr << "PROGRAMMATIC_POINTER_MOTION_FAIL " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

u32 pointer_lparam(i32 x, i32 y) {
    return static_cast<u32>(static_cast<u16>(x)) |
        (static_cast<u32>(static_cast<u16>(y)) << 16);
}

void require_startup_pointer_untouched(const char* message) {
    const InputState& input = input_state();
    require(input.mouse_x == 0 && input.mouse_y == 0 &&
            !input.pointer_motion_seen, message);
}

} // namespace

int main() {
    input_state() = InputState{};
    input_state().mouse_x = 91;
    input_state().mouse_y = 73;
    input_state().pointer_motion_seen = true;
    ResetPointerMotionToLegacyStartupState();
    require_startup_pointer_untouched(
        "legacy startup reset did not restore zero device coordinates");

    SuppressNextProgrammaticPointerMotion(400, 300, 0, 0);

    HandlePointerMotion(pointer_lparam(400, 300));
    require_startup_pointer_untouched(
        "the first SetCursorPos target became a device sample");

    HandlePointerMotion(pointer_lparam(400, 300));
    require_startup_pointer_untouched(
        "a duplicate SetCursorPos target became a device sample");

    HandlePointerMotion(pointer_lparam(401, 300));
    require(input_state().mouse_x == 401 && input_state().mouse_y == 300 &&
            input_state().pointer_motion_seen,
        "the first genuine post-warp movement remained suppressed");

    HandlePointerMotion(pointer_lparam(402, 301));
    require(input_state().mouse_x == 402 && input_state().mouse_y == 301,
        "normal pointer sampling did not resume after genuine movement");

    SuppressNextProgrammaticPointerMotion(10, 20, 0, 0);
    ResetPointerMotionToLegacyStartupState();
    HandlePointerMotion(pointer_lparam(5, 6));
    require(input_state().mouse_x == 5 && input_state().mouse_y == 6,
        "startup reset retained an obsolete programmatic-motion latch");

    software_cursor_state().pointer_updates_suppressed = true;
    HandlePointerMotion(pointer_lparam(321, 222));
    require(input_state().mouse_x == 321 && input_state().mouse_y == 222 &&
            input_state().pointer_motion_seen,
        "native-cursor transition discarded a real logical pointer sample");
    software_cursor_state().pointer_updates_suppressed = false;

    std::cout << "PROGRAMMATIC_POINTER_MOTION_PASS\n";
    return EXIT_SUCCESS;
}
