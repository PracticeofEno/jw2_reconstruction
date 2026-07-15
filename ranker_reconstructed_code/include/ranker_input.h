#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>

namespace ranker {

constexpr u32 kInputEventQueueSize = 32;
constexpr u32 kInputKeyCount = 0x100;

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

struct InputState {
    std::array<u8, kInputKeyCount> key_down{};
    std::array<InputEvent, kInputEventQueueSize> events{};
    u32 head = 0;
    u32 tail = 0;
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
    bool shift_down = false;
    bool ctrl_down = false;
    bool alt_down = false;
    bool print_screen_pressed = false;
    bool print_screen_toggled = false;
    bool alt_f4_seen = false;
    InputEvent current_event{};
};

InputState& input_state();
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
bool HandleKeyDown(u32 key);
void HandleKeyUp(u32 key);
bool HandleAltKeyPress(u32 key);
void HandleAltKeyRelease(u32 key);
bool HandleCharacterInput(u32 character);
bool HandleWindowInputMessage(u32 message, u32 wparam, u32 lparam);

}
