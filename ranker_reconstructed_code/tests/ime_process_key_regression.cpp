#include "ranker_input.h"
#include "ranker_cursor.h"
#include "ranker_gameplay_input_actions.h"
#include "ranker_screenshot.h"

#include <cstdlib>
#include <iostream>

namespace ranker {

GameplayInputActionState& gameplay_input_action_state() {
    static GameplayInputActionState state{};
    return state;
}

void ResetGameplayInputSnapshotRing(GameplayInputActionState&) {}

bool PopGameplayInputSnapshot(GameplayInputActionState&) {
    return false;
}

bool PushGameplayInputSnapshot(GameplayInputActionState&) {
    return true;
}

bool PushGameplayInputSnapshot(
    GameplayInputActionState&, const GameplayInputSnapshot&) {
    return true;
}

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
    std::cerr << "IME_PROCESS_KEY_FAIL " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    constexpr u32 kVkProcessKey = 0xe5u;
    constexpr u32 kVkDigit1 = 0x31u;
    constexpr u32 kDigit1Scan = 0x02u;

    input_state() = InputState{};

    // Some Korean IMEs expose only VK_PROCESSKEY for a gameplay shortcut.  Its
    // physical scan must remain usable as the single control-group event.
    require(HandleKeyDown(kVkProcessKey, kDigit1Scan),
        "VK_PROCESSKEY keydown was not consumed");
    require(input_state().head == 1u,
        "VK_PROCESSKEY did not publish the physical scan event");
    require(input_state().key_down[kVkProcessKey] == 0u,
        "VK_PROCESSKEY polluted virtual-key held state");
    require(input_state().set1_scan_down[kDigit1Scan] == 1u,
        "VK_PROCESSKEY did not retain physical scan state");
    require(input_state().events[0].kind == InputEventKind::keyboard &&
            input_state().events[0].code == kDigit1Scan,
        "VK_PROCESSKEY published the wrong gameplay event");

    // Other Korean IMEs follow it with a matching real keydown.  The shared
    // still-held scan identifies that message as a duplicate.
    require(HandleKeyDown(kVkDigit1, kDigit1Scan),
        "matching digit keydown was not consumed");
    require(input_state().head == 1u,
        "matching digit published a duplicate gameplay event");
    require(input_state().key_down[kVkDigit1] == 1u,
        "matching digit did not update virtual-key held state");

    HandleKeyUp(kVkProcessKey, kDigit1Scan);
    require(input_state().set1_scan_down[kDigit1Scan] == 0u,
        "VK_PROCESSKEY keyup did not release scan-code state");
    HandleKeyUp(kVkDigit1, kDigit1Scan);
    require(input_state().key_down[kVkDigit1] == 0u,
        "real digit keyup did not release virtual-key state");

    // A later physical press must not remain blocked by the de-duplication
    // latch after either form of keyup.
    require(HandleKeyDown(kVkProcessKey, kDigit1Scan),
        "second physical VK_PROCESSKEY press was not consumed");
    require(input_state().head == 2u,
        "second physical press remained incorrectly suppressed");
    HandleKeyUp(kVkProcessKey, kDigit1Scan);

    std::cout << "IME_PROCESS_KEY_PASS\n";
    return EXIT_SUCCESS;
}
