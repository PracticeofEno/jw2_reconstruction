#include "ranker_input.h"
#include "ranker_cursor.h"
#include "ranker_gameplay_input_actions.h"
#include "ranker_screenshot.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {
namespace {

InputState g_input_state;
std::atomic_flag g_mouse_event_snapshot_gate = ATOMIC_FLAG_INIT;

class MouseEventSnapshotGuard {
public:
    MouseEventSnapshotGuard() {
        while (g_mouse_event_snapshot_gate.test_and_set(
            std::memory_order_acquire)) {
        }
    }

    ~MouseEventSnapshotGuard() {
        g_mouse_event_snapshot_gate.clear(std::memory_order_release);
    }

    MouseEventSnapshotGuard(const MouseEventSnapshotGuard&) = delete;
    MouseEventSnapshotGuard& operator=(const MouseEventSnapshotGuard&) = delete;
};

constexpr u32 kButtonLeft = 1;
constexpr u32 kButtonRight = 2;
constexpr u32 kButtonMiddle = 4;

constexpr u32 kMouseCodeLeftDown = 2;
constexpr u32 kMouseCodeLeftUp = 4;
constexpr u32 kMouseCodeRightDown = 8;
constexpr u32 kMouseCodeRightUp = 0x10;
constexpr u32 kMouseCodeLeftDouble = 0x20;
constexpr u32 kMouseCodeRightDouble = 0x40;
constexpr u32 kMouseCodeMiddleDown = 0x80;
constexpr u32 kMouseCodeMiddleUp = 0x100;

i32 signed_lparam_word(u32 value, u32 shift) {
    return static_cast<i16>((value >> shift) & 0xffffu);
}

bool push_event(const InputEvent& event) {
    const u32 head = g_input_state.head.load(std::memory_order_relaxed);
    const u32 next = (head + 1) % kInputEventQueueSize;
    if (next == g_input_state.tail.load(std::memory_order_acquire)) {
        return false;
    }
    g_input_state.events[head] = event;
    g_input_state.head.store(next, std::memory_order_release);
    return true;
}

bool push_mouse_event_with_snapshot(const InputEvent& event,
    GameplayInputActionState& gameplay_input,
    const GameplayInputSnapshot& snapshot) {
    const u32 head = g_input_state.head.load(std::memory_order_relaxed);
    const u32 next = (head + 1) % kInputEventQueueSize;
    if (next == g_input_state.tail.load(std::memory_order_acquire)) {
        return false;
    }

    // Reserve and populate the main-ring slot without publishing its head.
    // Publishing the matching snapshot first makes the later main-head release
    // the single visibility gate observed by PopInputEvent.  If either ring is
    // full, no cursor advances and the streams cannot become misaligned.
    g_input_state.events[head] = event;
    if (!PushGameplayInputSnapshot(gameplay_input, snapshot)) {
        return false;
    }
    g_input_state.head.store(next, std::memory_order_release);
    return true;
}

bool push_mouse_input_event_locked(
    u32 message, u32 code, u32 wparam, u32 lparam) {
    InputEvent event{};
    event.kind = InputEventKind::mouse;
    event.message = message;
    event.code = code;
    event.wparam = wparam;
    event.lparam = lparam;
    event.x = static_cast<i32>(g_input_state.mouse_x);
    event.y = static_cast<i32>(g_input_state.mouse_y);
    event.dx = g_input_state.mouse_dx;
    event.dy = g_input_state.mouse_dy;
    event.button_mask = g_input_state.mouse_button_mask;

    GameplayInputSnapshot snapshot{};
    snapshot.field0 = static_cast<u32>(InputEventKind::mouse);
    snapshot.field1 = code;
    snapshot.field2 = static_cast<u32>(event.x);
    snapshot.field3 = static_cast<u32>(event.y);
    snapshot.field4 = event.button_mask;
    return push_mouse_event_with_snapshot(
        event, gameplay_input_action_state(), snapshot);
}

void update_mouse_position(u32 lparam) {
    const i32 x = signed_lparam_word(lparam, 0);
    const i32 y = signed_lparam_word(lparam, 16);
    g_input_state.mouse_dx = static_cast<i32>(g_input_state.mouse_x) - x;
    g_input_state.mouse_dy = static_cast<i32>(g_input_state.mouse_y) - y;
    g_input_state.mouse_x = static_cast<u32>(x);
    g_input_state.mouse_y = static_cast<u32>(y);
    g_input_state.pointer_motion_seen = true;
}

void refresh_modifier_state() {
#ifdef _WIN32
    g_input_state.shift_down = GetAsyncKeyState(VK_SHIFT) < 0;
    g_input_state.ctrl_down = GetAsyncKeyState(VK_CONTROL) < 0;
    g_input_state.alt_down = GetAsyncKeyState(VK_MENU) < 0;
#endif
}

void set_key_state(u32 key, bool down) {
    if (key < g_input_state.key_down.size()) {
        g_input_state.key_down[key] = down ? 1 : 0;
    }

    // WM_KEYDOWN/WM_KEYUP do not pass through refresh_modifier_state().
    // Keep these booleans synchronized at message time so Ctrl+digit and
    // Shift+function-key events see the modifier that accompanied them.  The
    // virtual-key array remains separate and continues to drive held arrows.
    const auto key_is_down = [](u32 virtual_key) {
        return virtual_key < g_input_state.key_down.size() &&
            g_input_state.key_down[virtual_key] != 0;
    };
    switch (key) {
    case 0x10: case 0xa0: case 0xa1: // VK_SHIFT/L/RSHIFT
        g_input_state.shift_down = key_is_down(0x10) ||
            key_is_down(0xa0) || key_is_down(0xa1);
        break;
    case 0x11: case 0xa2: case 0xa3: // VK_CONTROL/L/RCONTROL
        g_input_state.ctrl_down = key_is_down(0x11) ||
            key_is_down(0xa2) || key_is_down(0xa3);
        break;
    case 0x12: case 0xa4: case 0xa5: // VK_MENU/L/RMENU
        g_input_state.alt_down = key_is_down(0x12) ||
            key_is_down(0xa4) || key_is_down(0xa5);
        break;
    default:
        break;
    }
}

bool handle_mouse_button(u32 message, u32 code, u32 wparam, u32 lparam,
    u32 button, bool down) {
    MouseEventSnapshotGuard paired_stream_guard;
    switch (message) {
    case 0x0201: g_input_state.left_button_down_seen = true; break;
    case 0x0202: g_input_state.left_button_up_seen = true; break;
    case 0x0203: g_input_state.left_button_double_seen = true; break;
    case 0x0204: g_input_state.right_button_down_seen = true; break;
    case 0x0205: g_input_state.right_button_up_seen = true; break;
    case 0x0206: g_input_state.right_button_double_seen = true; break;
    case 0x0207: g_input_state.middle_button_down_seen = true; break;
    case 0x0208: g_input_state.middle_button_up_seen = true; break;
    default: break;
    }
    if (down) {
        g_input_state.mouse_button_mask |= button;
    }
    else {
        g_input_state.mouse_button_mask &= ~button;
    }
    g_input_state.last_mouse_code = code;
    update_mouse_position(lparam);
    return push_mouse_input_event_locked(message, code, wparam, lparam);
}

void reset_input_state_only() {
    g_input_state.mouse_button_mask = 0;
    g_input_state.left_button_down_seen = false;
    g_input_state.left_button_up_seen = false;
    g_input_state.right_button_down_seen = false;
    g_input_state.right_button_up_seen = false;
    g_input_state.middle_button_down_seen = false;
    g_input_state.middle_button_up_seen = false;
    g_input_state.left_button_double_seen = false;
    g_input_state.right_button_double_seen = false;
    g_input_state.current_event.code = 0;
    // WndProc owns head and may publish while the gameplay worker resets its
    // input phase.  Advancing only the consumer-owned tail to the acquired
    // head forms a linearizable flush without resurrecting stale ring slots.
    const u32 published_head =
        g_input_state.head.load(std::memory_order_acquire);
    g_input_state.tail.store(published_head, std::memory_order_release);
}

} // namespace

InputState& input_state() {
    return g_input_state;
}

void ResetInputState() {
    ResetInputEventState();
}

void ResetInputEventState() {
    // Mouse events and their gameplay snapshots are a coupled stream.  Hold
    // the same short gate as their producer so a reset cannot land between
    // the snapshot publication and the main-ring head publication.
    MouseEventSnapshotGuard paired_stream_guard;
    reset_input_state_only();
    ResetGameplayInputSnapshotRing(gameplay_input_action_state());
}

bool HasQueuedInputEvent() {
    return g_input_state.head.load(std::memory_order_acquire) !=
        g_input_state.tail.load(std::memory_order_acquire);
}

bool PopInputEvent(InputEvent& event) {
    const u32 tail = g_input_state.tail.load(std::memory_order_relaxed);
    if (g_input_state.head.load(std::memory_order_acquire) == tail) {
        return false;
    }
    event = g_input_state.events[tail];
    if (event.kind == InputEventKind::mouse) {
        PopGameplayInputSnapshot(gameplay_input_action_state());
    }
    g_input_state.current_event = event;
    g_input_state.tail.store(
        (tail + 1) % kInputEventQueueSize, std::memory_order_release);
    return true;
}

void ClearInputHeldKeysForFocusLoss() {
    g_input_state.key_down.fill(0);
    g_input_state.shift_down = false;
    g_input_state.ctrl_down = false;
    g_input_state.alt_down = false;
}

InputEvent GetNextInputEventBlocking() {
    InputEvent event{};
    while (!PopInputEvent(event)) {
    }
    return event;
}

void NoOpInputIdleHook(u32 flags) {
    (void)flags;
}

void WaitForPressedMouseButtonsReleasedThenResetInput() {
    for (;;) {
        if ((g_input_state.mouse_button_mask & kButtonLeft) != 0 &&
            !g_input_state.left_button_up_seen) {
            continue;
        }
        if ((g_input_state.mouse_button_mask & kButtonRight) != 0 &&
            !g_input_state.right_button_up_seen) {
            continue;
        }
        if ((g_input_state.mouse_button_mask & kButtonMiddle) != 0 &&
            !g_input_state.middle_button_up_seen) {
            continue;
        }
        break;
    }
    ResetInputState();
}

u32 WaitForKeyboardInputEventCode() {
    for (;;) {
        NoOpInputIdleHook(0);
        const InputEvent event = GetNextInputEventBlocking();
        if (event.kind == InputEventKind::keyboard) {
            return event.code & 0xffu;
        }
    }
}

bool PushKeyboardInputEvent(u32 key_or_char) {
    InputEvent event{};
    event.kind = InputEventKind::keyboard;
    event.code = key_or_char;
    event.wparam = key_or_char;
    return push_event(event);
}

bool PushMouseInputEvent(u32 message, u32 code, u32 wparam, u32 lparam) {
    // Build the snapshot from a producer-local value.  The worker owns
    // GameplayInputActionState::live_snapshot and may clear it independently.
    // Protect the button-state read and both cursor publications so reset can
    // only linearize before or after the complete mouse-event pair.
    MouseEventSnapshotGuard paired_stream_guard;
    return push_mouse_input_event_locked(message, code, wparam, lparam);
}

bool HandlePointerMotion(u32 lparam) {
#ifdef _WIN32
    SoftwareCursorState& cursor = software_cursor_state();
    if (cursor.cursor_change_depth != 0) {
        ++cursor.cursor_change_depth;
        return true;
    }
#endif
    const i32 x = signed_lparam_word(lparam, 0);
    const i32 y = signed_lparam_word(lparam, 16);
    // Original 004fcd91 overwrites the X-delta slot with the Y delta.
    g_input_state.mouse_dx = static_cast<i32>(g_input_state.mouse_x) - x;
    g_input_state.mouse_dx = static_cast<i32>(g_input_state.mouse_y) - y;
#ifdef _WIN32
    if (cursor.pointer_updates_suppressed) {
        return true;
    }
#endif
    g_input_state.mouse_x = static_cast<u32>(x);
    g_input_state.mouse_y = static_cast<u32>(y);
    g_input_state.pointer_motion_seen = true;
#ifdef _WIN32
    SetGameCursorPointerPosition(x, y);
#endif
    return true;
}

bool HandleLeftButtonDown(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0201, kMouseCodeLeftDown, wparam, lparam,
        kButtonLeft, true);
}

bool HandleLeftButtonUp(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0202, kMouseCodeLeftUp, wparam, lparam,
        kButtonLeft, false);
}

bool HandleRightButtonDown(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0204, kMouseCodeRightDown, wparam, lparam,
        kButtonRight, true);
}

bool HandleRightButtonUp(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0205, kMouseCodeRightUp, wparam, lparam,
        kButtonRight, false);
}

bool HandleMiddleButtonDown(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0207, kMouseCodeMiddleDown, wparam, lparam,
        kButtonMiddle, true);
}

bool HandleMiddleButtonUp(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0208, kMouseCodeMiddleUp, wparam, lparam,
        kButtonMiddle, false);
}

bool HandleLeftButtonDoubleClick(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0203, kMouseCodeLeftDouble, wparam, lparam,
        kButtonLeft, true);
}

bool HandleRightButtonDoubleClick(u32 wparam, u32 lparam) {
    return handle_mouse_button(0x0206, kMouseCodeRightDouble, wparam, lparam,
        kButtonRight, true);
}

bool HandleKeyDown(u32 key, u32 legacy_scan_code) {
    set_key_state(key, true);
    return PushKeyboardInputEvent(legacy_scan_code & 0xffu);
}

void HandleKeyUp(u32 key) {
    set_key_state(key, false);
}

bool HandleAltKeyPress(u32 key, u32 legacy_scan_code) {
    if (key == 0) {
        return false;
    }
    set_key_state(key, true);
    return PushKeyboardInputEvent(legacy_scan_code & 0xffu);
}

void HandleAltKeyRelease(u32 key) {
    if (key != 0) {
        set_key_state(key, false);
    }
}

bool HandleCharacterInput(u32 character) {
    return PushKeyboardInputEvent((character & 0xffu) << 8);
}

bool HandleWindowInputMessage(u32 message, u32 wparam, u32 lparam) {
    switch (message) {
    case 0x0200:
        return HandlePointerMotion(lparam);
    case 0x0201:
        return HandleLeftButtonDown(wparam, lparam);
    case 0x0202:
        return HandleLeftButtonUp(wparam, lparam);
    case 0x0203:
        return HandleLeftButtonDoubleClick(wparam, lparam);
    case 0x0204:
        return HandleRightButtonDown(wparam, lparam);
    case 0x0205:
        return HandleRightButtonUp(wparam, lparam);
    case 0x0206:
        return HandleRightButtonDoubleClick(wparam, lparam);
    case 0x0207:
        return HandleMiddleButtonDown(wparam, lparam);
    case 0x0208:
        return HandleMiddleButtonUp(wparam, lparam);
    case 0x0100:
        if ((lparam & 0x40000000u) == 0) {
            return HandleKeyDown(
                wparam, ResolveLegacyKeyboardScanCode(wparam, lparam));
        }
        return true;
    case 0x0101:
        HandleKeyUp(wparam);
        if (wparam == 0x2c) {
            if (g_input_state.ctrl_down) {
                g_input_state.print_screen_toggled =
                    !g_input_state.print_screen_toggled;
#ifdef _WIN32
                SetContinuousScreenshotCapture(g_input_state.print_screen_toggled);
#endif
            }
            else {
                g_input_state.print_screen_pressed = true;
#ifdef _WIN32
                RequestScreenshotCapture();
#endif
            }
        }
        return true;
    case 0x0102:
        return HandleCharacterInput(wparam);
    case 0x0104:
        HandleAltKeyPress(
            wparam, ResolveLegacyKeyboardScanCode(wparam, lparam));
        if (((lparam >> 16) & 0xffu) == 0x3e && g_input_state.alt_down) {
            g_input_state.alt_f4_seen = true;
        }
        return true;
    case 0x0105:
        HandleAltKeyRelease(wparam);
        return true;
    default:
        refresh_modifier_state();
        break;
    }
    return false;
}

}
