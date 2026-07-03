#include "ranker_string_selector.h"

#ifdef _WIN32

#include <cstdint>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kSelectorStyle = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
constexpr COLORREF kSelectorTextColor = RGB(200, 200, 200);
constexpr COLORREF kSelectorBackgroundColor = RGB(0, 0, 0);
constexpr UINT kSelectorRedrawFlags = RDW_INVALIDATE | RDW_NOERASE;

HINSTANCE parent_instance(HWND parent) {
    return reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(parent, GWLP_HINSTANCE));
}

WNDPROC original_proc(HWND window) {
    if (window == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<WNDPROC>(GetWindowLongPtrA(window, GWLP_WNDPROC));
}

char* selector_text_slot(LegacyStringSelectorControl& control, int index) {
    const auto base = reinterpret_cast<std::intptr_t>(control.items.data());
    const auto offset = static_cast<std::intptr_t>(index) *
        static_cast<std::intptr_t>(kLegacyStringSelectorTextBytes);
    return reinterpret_cast<char*>(base + offset);
}

const char* selector_text_slot(const LegacyStringSelectorControl& control,
    int index) {
    const auto base = reinterpret_cast<std::intptr_t>(control.items.data());
    const auto offset = static_cast<std::intptr_t>(index) *
        static_cast<std::intptr_t>(kLegacyStringSelectorTextBytes);
    return reinterpret_cast<const char*>(base + offset);
}

void redraw_selector(const LegacyStringSelectorControl& control) {
    RedrawWindow(control.window, nullptr, nullptr, kSelectorRedrawFlags);
}

void draw_selected_text(LegacyStringSelectorControl& control, HWND hwnd) {
    if (hwnd != control.window) {
        return;
    }

    const char* text = selector_text_slot(control, control.selected_index);
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    BitBlt(dc, 0, 0, paint.rcPaint.right, paint.rcPaint.bottom, nullptr, 0, 0,
        BLACKNESS);
    SetTextColor(dc, kSelectorTextColor);
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, kSelectorBackgroundColor);
    RECT text_rect = paint.rcPaint;
    text_rect.left += 2;
    text_rect.top += 2;
    DrawTextA(dc, text, -1, &text_rect, DT_NOPREFIX);
    EndPaint(hwnd, &paint);
}

void draw_button_item(LegacyStringSelectorControl& control, WPARAM wparam,
    LPARAM lparam) {
    auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
    if (item == nullptr || control.buttons_hidden) {
        return;
    }

    const int relative_id = static_cast<int>(wparam) - control.id;
    if (relative_id == 1) {
        DrawLegacyImageButtonItem(control.increment_button, *item);
    } else if (relative_id == 2) {
        DrawLegacyImageButtonItem(control.decrement_button, *item);
    }
}

bool handle_button_command(LegacyStringSelectorControl& control, WPARAM wparam) {
    const int relative_id = LOWORD(wparam) - control.id;
    if (relative_id == 1) {
        ++control.selected_index;
        if (control.item_count <= control.selected_index) {
            control.selected_index = control.item_count - 1;
        }
        redraw_selector(control);
        return true;
    }
    if (relative_id == 2) {
        --control.selected_index;
        if (control.selected_index < 0) {
            control.selected_index = 0;
        }
        redraw_selector(control);
        return true;
    }
    return false;
}

} // namespace

LegacyStringSelectorControl& InitializeLegacyStringSelectorControl(
    LegacyStringSelectorControl& control) {
    InitializeLegacyImageButtonControl(control.increment_button);
    InitializeLegacyImageButtonControl(control.decrement_button);
    ResetLegacyStringSelectorControl(control);
    return control;
}

void ResetLegacyStringSelectorControl(LegacyStringSelectorControl& control) {
    control.original_window_proc = nullptr;
    control.min_index = 0;
    control.item_count = 0;
    control.selected_index = 0;
    control.window = nullptr;
    control.buttons_hidden = false;
    control.selected_index = control.min_index;
}

void SetLegacyStringSelectorButtonsHidden(LegacyStringSelectorControl& control,
    bool hidden) {
    control.buttons_hidden = hidden;
    const int command = hidden ? SW_HIDE : SW_SHOW;
    HWND increment = GetLegacyImageButtonWindow(control.increment_button);
    ShowWindow(increment, command);
    HWND decrement = GetLegacyImageButtonWindow(control.decrement_button);
    ShowWindow(decrement, command);
}

bool ConstructLegacyStringSelectorControl(LegacyStringSelectorControl& control,
    HWND parent, const char* text, HMENU id_or_menu, int x, int y, int width,
    int height, int button_width) {
    InitializeLegacyStringSelectorControl(control);
    return CreateLegacyStringSelectorWindow(control, parent, text, id_or_menu, x, y,
        width, height, button_width);
}

void DestroyLegacyStringSelectorControl(LegacyStringSelectorControl& control) {
    DestroyLegacyImageButtonControl(control.increment_button);
    DestroyLegacyImageButtonControl(control.decrement_button);
    if (control.window != nullptr) {
        DestroyWindow(control.window);
        control.window = nullptr;
    }
}

bool CreateLegacyStringSelectorWindow(LegacyStringSelectorControl& control,
    HWND parent, const char* text, HMENU id_or_menu, int x, int y, int width,
    int height, int button_width) {
    ResetLegacyStringSelectorControl(control);
    control.id = static_cast<int>(reinterpret_cast<INT_PTR>(id_or_menu));
    control.x = x;
    control.y = y;
    control.width = width;
    control.height = height;
    control.button_width = button_width;
    control.parent = parent;

    control.window = CreateWindowExA(0, "button", text,
        kSelectorStyle, x, y, width, height, parent, id_or_menu,
        parent_instance(parent), nullptr);

    const int half_height = height / 2;
    CreateLegacyImageButtonWindow(control.increment_button, control.window, "^",
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control.id + 1)),
        width - button_width, 0, button_width, half_height);
    CreateLegacyImageButtonWindow(control.decrement_button, control.window, "v",
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control.id + 2)),
        width - button_width, half_height, button_width, half_height);

    control.original_window_proc = original_proc(control.window);
    ShowWindow(control.window, SW_SHOW);
    return control.window != nullptr;
}

HWND GetLegacyStringSelectorWindow(const LegacyStringSelectorControl& control) {
    return control.window;
}

void LoadLegacyStringSelectorIncrementButtonBitmaps(
    LegacyStringSelectorControl& control, u32 normal_record, u32 pressed_record) {
    LoadLegacyImageButtonBitmaps(control.increment_button, normal_record,
        pressed_record);
}

void LoadLegacyStringSelectorDecrementButtonBitmaps(
    LegacyStringSelectorControl& control, u32 normal_record, u32 pressed_record) {
    LoadLegacyImageButtonBitmaps(control.decrement_button, normal_record,
        pressed_record);
}

void SetLegacyStringSelectorBounds(LegacyStringSelectorControl& control,
    int min_index, int item_count) {
    control.min_index = min_index;
    control.item_count = item_count;
}

void SetLegacyStringSelectorSelectedIndex(LegacyStringSelectorControl& control,
    int selected_index) {
    if (control.min_index <= selected_index && selected_index <= control.item_count) {
        control.selected_index = selected_index;
    }
}

int GetLegacyStringSelectorSelectedIndex(const LegacyStringSelectorControl& control) {
    return control.selected_index;
}

const char* GetLegacyStringSelectorSelectedText(
    const LegacyStringSelectorControl& control) {
    if (control.item_count < 1 || control.item_count <= control.selected_index) {
        return nullptr;
    }
    return selector_text_slot(control, control.selected_index);
}

void IgnoreLegacyStringSelectorReservedMessage(LPARAM) {
}

void AddLegacyStringSelectorText(LegacyStringSelectorControl& control,
    const char* text) {
    if (control.item_count >= static_cast<int>(control.items.size())) {
        return;
    }

    std::strcpy(selector_text_slot(control, control.item_count), text);
    ++control.item_count;
}

LRESULT HandleLegacyStringSelectorMessage(LegacyStringSelectorControl& control,
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_DRAWITEM:
        draw_button_item(control, wparam, lparam);
        break;
    case WM_COMMAND:
        handle_button_command(control, wparam);
        [[fallthrough]];
    case WM_PAINT:
        if (control.window == hwnd && control.item_count > 0) {
            draw_selected_text(control, hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        return 0;
    default:
        break;
    }

    return CallWindowProcA(control.original_window_proc, hwnd, message, wparam,
        lparam);
}

} // namespace ranker

#endif
