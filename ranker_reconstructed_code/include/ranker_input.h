#pragma once

#include "ranker_types.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace ranker {

constexpr u32 kInputEventQueueSize = 32;
constexpr u32 kInputKeyCount = 0x100;

// The original gameplay dispatcher consumes the low-byte IBM/PC set-1 scan
// code carried by the window message, not the Win32 virtual-key value.  Keep
// virtual keys for held-key state, but publish this value to the event queue.
constexpr u32 ResolveLegacyKeyboardScanCode(u32 virtual_key, u32 lparam) {
    (void)virtual_key;
    // WndProc stores the scan byte from lParam verbatim.  In particular, a
    // synthetic message with scan zero must remain zero; substituting VK_A
    // (0x41) would accidentally route it as the F7 camera-bookmark scan.
    return (lparam >> 16) & 0xffu;
}

enum class InputEventKind : u32 {
    keyboard = 1,
    mouse = 0,
};

struct InputEvent {
    InputEventKind kind = InputEventKind::mouse;
    u32 message = 0;
    u32 code = 0;
    u32 wparam = 0;
    u32 lparam = 0;
    i32 x = 0;
    i32 y = 0;
    i32 dx = 0;
    i32 dy = 0;
    u32 button_mask = 0;
};

// The original Win32 message thread writes one-byte held-key globals while
// the gameplay worker reads their current values.  Preserve that live-read
// behavior (an input record does not snapshot modifiers), but make the same
// cross-thread byte loads/stores well-defined in C++.
struct InputAtomicByte {
    InputAtomicByte() noexcept = default;
    InputAtomicByte(u8 initial) noexcept : value(initial) {}
    InputAtomicByte(const InputAtomicByte& other) noexcept :
        value(other.load()) {}
    InputAtomicByte& operator=(const InputAtomicByte& other) noexcept {
        store(other.load());
        return *this;
    }
    InputAtomicByte& operator=(u8 next) noexcept {
        store(next);
        return *this;
    }
    operator u8() const noexcept {
        return load();
    }
    u8 load(
        std::memory_order order = std::memory_order_relaxed) const noexcept {
        return value.load(order);
    }
    void store(u8 next,
        std::memory_order order = std::memory_order_relaxed) noexcept {
        value.store(next, order);
    }

    std::atomic<u8> value{0};
};

static_assert(sizeof(InputAtomicByte) == sizeof(u8));
static_assert(alignof(InputAtomicByte) == alignof(u8));
static_assert(sizeof(InputAtomicByte) == sizeof(bool));
static_assert(alignof(InputAtomicByte) == alignof(bool));
static_assert(std::atomic<u8>::is_always_lock_free);

// The Win32 window thread publishes events while the gameplay worker drains
// them.  Keep the original four-byte indices and make their SPSC hand-off
// explicit.  The copy operations retain InputState's value semantics for
// resets and focused regressions.
struct InputQueueIndex {
    InputQueueIndex() noexcept = default;
    explicit InputQueueIndex(u32 initial) noexcept : value(initial) {}
    InputQueueIndex(const InputQueueIndex& other) noexcept :
        value(other.load(std::memory_order_relaxed)) {}
    InputQueueIndex& operator=(const InputQueueIndex& other) noexcept {
        store(other.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }
    InputQueueIndex& operator=(u32 next) noexcept {
        store(next, std::memory_order_relaxed);
        return *this;
    }
    operator u32() const noexcept {
        return load(std::memory_order_relaxed);
    }
    u32 load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return value.load(order);
    }
    void store(u32 next,
        std::memory_order order = std::memory_order_seq_cst) noexcept {
        value.store(next, order);
    }

    std::atomic<u32> value{0};
};

static_assert(sizeof(InputQueueIndex) == sizeof(u32));
static_assert(alignof(InputQueueIndex) == alignof(u32));
static_assert(std::atomic<u32>::is_always_lock_free);

struct InputState {
    std::array<InputAtomicByte, kInputKeyCount> key_down{};
    std::array<InputEvent, kInputEventQueueSize> events{};
    InputQueueIndex head{};
    InputQueueIndex tail{};
    u32 mouse_x = 0;
    u32 mouse_y = 0;
    i32 mouse_dx = 0;
    i32 mouse_dy = 0;
    u32 mouse_button_mask = 0;
    u32 last_mouse_code = 0;
    bool pointer_motion_seen = false;
    bool pointer_inside_client = false;
    bool left_button_down_seen = false;
    bool left_button_up_seen = false;
    bool right_button_down_seen = false;
    bool right_button_up_seen = false;
    bool middle_button_down_seen = false;
    bool middle_button_up_seen = false;
    bool left_button_double_seen = false;
    bool right_button_double_seen = false;
    InputAtomicByte shift_down{};
    InputAtomicByte ctrl_down{};
    InputAtomicByte alt_down{};
    bool print_screen_pressed = false;
    bool print_screen_toggled = false;
    bool alt_f4_seen = false;
    InputEvent current_event{};
};

static_assert(offsetof(InputState, events) ==
    sizeof(decltype(InputState::key_down)));
static_assert(offsetof(InputState, ctrl_down) ==
    offsetof(InputState, shift_down) + sizeof(InputAtomicByte));
static_assert(offsetof(InputState, alt_down) ==
    offsetof(InputState, ctrl_down) + sizeof(InputAtomicByte));
static_assert(sizeof(InputState) == 0x658u);

InputState& input_state();
// Queue reset is a consumer-side flush during gameplay.  Call it from the
// gameplay consumer or while the producer is quiescent; it deliberately does
// not rewrite the producer-owned head cursor.
void ResetInputState();
void ResetInputEventState();
bool HasQueuedInputEvent();
bool PopInputEvent(InputEvent& event);
InputEvent GetNextInputEventBlocking();
void NoOpInputIdleHook(u32 flags = 0);
void WaitForPressedMouseButtonsReleasedThenResetInput();
u32 WaitForKeyboardInputEventCode();
bool PushKeyboardInputEvent(u32 key_or_char);
bool PushMouseInputEvent(u32 message, u32 code, u32 wparam, u32 lparam);
bool HandlePointerMotion(u32 lparam);
bool HandleLeftButtonDown(u32 wparam, u32 lparam);
bool HandleLeftButtonUp(u32 wparam, u32 lparam);
bool HandleRightButtonDown(u32 wparam, u32 lparam);
bool HandleRightButtonUp(u32 wparam, u32 lparam);
bool HandleMiddleButtonDown(u32 wparam, u32 lparam);
bool HandleMiddleButtonUp(u32 wparam, u32 lparam);
bool HandleLeftButtonDoubleClick(u32 wparam, u32 lparam);
bool HandleRightButtonDoubleClick(u32 wparam, u32 lparam);
bool HandleKeyDown(u32 key, u32 legacy_scan_code);
void HandleKeyUp(u32 key);
bool HandleAltKeyPress(u32 key, u32 legacy_scan_code);
void HandleAltKeyRelease(u32 key);
bool HandleCharacterInput(u32 character);
bool HandleWindowInputMessage(u32 message, u32 wparam, u32 lparam);
void ClearInputHeldKeysForFocusLoss();

}
