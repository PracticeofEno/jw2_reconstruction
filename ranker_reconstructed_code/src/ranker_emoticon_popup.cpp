#include "ranker_emoticon_popup.h"

#ifdef _WIN32

#include "ranker_cursor.h"
#include "ranker_gameplay_sound.h"
#include "ranker_online_dialogs.h"
#include "ranker_winmain.h"

#include <cstdlib>

namespace ranker {
namespace {

constexpr DWORD kPopupWindowStyle = 0x90000001;
constexpr DWORD kImageButtonStyle = 0x5000000b;
constexpr DWORD kClearDrawRop = 0x00ff0062;

EmoticonPopupState g_emoticon_popup_state;

LRESULT CALLBACK emoticon_popup_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleEmoticonPopupMessage(g_emoticon_popup_state, hwnd, message, wparam,
        lparam);
}

LRESULT CALLBACK emoticon_popup_button_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleEmoticonPopupButtonMessage(g_emoticon_popup_state, hwnd, message,
        wparam, lparam);
}

void shutdown_global_emoticon_popup() {
    ShutdownEmoticonPopupButton(g_emoticon_popup_state);
}

void clear_image_button_bitmaps(EmoticonImageButtonControl& button) {
    ReleaseBitmapMemoryResource(button.normal_bitmap);
    ReleaseBitmapMemoryResource(button.pressed_bitmap);
}

bool create_image_button(EmoticonImageButtonControl& button, HWND parent,
    const char* text, int id, int x, int y, int width, int height) {
    button.x = x;
    button.y = y;
    button.width = width;
    button.height = height;
    button.parent = parent;

    HINSTANCE instance = reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrA(parent, GWLP_HINSTANCE));
    button.window = CreateWindowExA(0, "button", text == nullptr ? "" : text,
        kImageButtonStyle, x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(button.window, GWLP_WNDPROC));
    clear_image_button_bitmaps(button);
    return button.window != nullptr;
}

void set_image_button_bitmap_records(EmoticonImageButtonControl& button,
    u16 normal_record, u16 pressed_record) {
    clear_image_button_bitmaps(button);
    if (normal_record != 0) {
        LoadBitmapMemoryResourceFromTrcRecord(button.normal_bitmap, "Jw2_19.trc",
            normal_record);
    }
    if (pressed_record != 0) {
        LoadBitmapMemoryResourceFromTrcRecord(button.pressed_bitmap, "Jw2_19.trc",
            pressed_record);
    }
}

void accept_emoticon_popup_selection(EmoticonPopupState& state, HWND hwnd) {
    HandleDefaultFrontendUiClickSound();
    PostMessageA(state.owner_dialog, kEmoticonPopupAcceptMessage, 0, 0);
    state.active_modal_window = state.owner_dialog;
    DestroyWindow(hwnd);
}

void close_emoticon_popup(EmoticonPopupState& state, HWND hwnd) {
    const HWND owner_dialog = state.owner_dialog;
    const HWND focus_after_close = state.focus_after_close;
    ReleaseCapture();
    RestoreEmoticonPopupAccelerators(state);
    clear_image_button_bitmaps(state.button);
    state.popup_window = nullptr;
    if (hwnd != nullptr && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    if (owner_dialog != nullptr && IsWindow(owner_dialog)) {
        // activate_frontend_window routes input to the popup while it is open.
        // Leaving that destroyed HWND as the active route makes the main
        // window's WM_SETCURSOR path restore the locked-game NULL cursor.
        SetRankerMainWindowFrontendRouteWindow(owner_dialog);
        SetCursor(GetFrontendGameCursor());
        if (focus_after_close != nullptr && IsWindow(focus_after_close)) {
            SetFocus(focus_after_close);
        } else {
            SetFocus(owner_dialog);
        }
    }
}

LRESULT handle_control_color(HDC dc, UINT message) {
    switch (message) {
    case WM_CTLCOLOREDIT:
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkColor(dc, RGB(0, 0, 0));
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(dc, RGB(250, 250, 250));
        SetBkColor(dc, RGB(0, 0, 0));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        SetTextColor(dc, RGB(250, 250, 250));
        SetBkColor(dc, RGB(0, 0, 0));
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLORSTATIC:
        SetTextColor(dc, RGB(255, 255, 0));
        SetBkColor(dc, RGB(0, 0, 0));
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    default:
        return 0;
    }
}

} // namespace

EmoticonPopupState& emoticon_popup_state() {
    return g_emoticon_popup_state;
}

void InitializeEmoticonPopupSupport(EmoticonPopupState& state) {
    InitializeEmoticonPopupButton(state);
    RegisterEmoticonPopupShutdown(state);
}

void InitializeEmoticonPopupButton(EmoticonPopupState& state) {
    InitializeBitmapMemoryResource(state.button.normal_bitmap);
    InitializeBitmapMemoryResource(state.button.pressed_bitmap);
    state.button.window = nullptr;
    state.button.original_window_proc = nullptr;
    clear_image_button_bitmaps(state.button);
}

void RegisterEmoticonPopupShutdown(EmoticonPopupState&) {
    std::atexit(shutdown_global_emoticon_popup);
}

void ShutdownEmoticonPopupButton(EmoticonPopupState& state) {
    clear_image_button_bitmaps(state.button);
    if (state.button.window != nullptr) {
        DestroyWindow(state.button.window);
    }
    state.button.window = nullptr;
    HandleBitmapMemoryResourceDestructor(state.button.pressed_bitmap);
    HandleBitmapMemoryResourceDestructor(state.button.normal_bitmap);
}

void HandleEmoticonPopupNoop(EmoticonPopupState&) {
}

void SetEmoticonPopupHostState(EmoticonPopupState& state, HWND main_window,
    HWND owner_dialog, HWND focus_after_close, HACCEL active_accelerators,
    HWND active_accelerator_window) {
    state.main_window = main_window;
    state.owner_dialog = owner_dialog;
    state.focus_after_close = focus_after_close;
    state.active_accelerators = active_accelerators;
    state.active_accelerator_window = active_accelerator_window;
}

void InstallEmoticonPopupAccelerators(EmoticonPopupState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kEmoticonPopupAcceleratorResourceId));
    state.active_accelerator_window = state.popup_window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreEmoticonPopupAccelerators(EmoticonPopupState& state) {
    if (RankerMainWindowState().active_accelerator_window != state.popup_window) {
        return;
    }

    SetActiveAcceleratorState(nullptr, state.active_accelerators);
    DestroyAcceleratorTable(state.active_accelerators);
    state.active_accelerators = state.saved_accelerators;
    state.active_accelerator_window = state.saved_accelerator_window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

bool CreateEmoticonPopupWindow(EmoticonPopupState& state, HWND parent,
    HINSTANCE instance, int anchor_x, int anchor_y, const char* class_name,
    BitmapTileSheetSelector& selector) {
    state.selector = &selector;
    state.instance = instance;
    state.registered_class_name = class_name == nullptr ? "" : class_name;

    const int sheet_width = GetBitmapMemoryResourceWidth(selector.sheet);
    const int sheet_height = GetBitmapMemoryResourceHeight(selector.sheet);
    state.popup_window = CreateWindowExA(0, state.registered_class_name.c_str(), "Emo",
        kPopupWindowStyle, anchor_x - sheet_width, anchor_y - sheet_height,
        sheet_width + 2, sheet_height + 2, parent, nullptr, instance, nullptr);
    if (state.popup_window == nullptr) {
        return false;
    }

    SetWindowLongPtrA(state.popup_window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(emoticon_popup_window_proc));

    if (!create_image_button(state.button, state.popup_window, "",
            kEmoticonPopupButtonId, 0, 0, 0x1e, 0x10)) {
        return false;
    }

    set_image_button_bitmap_records(state.button, 0, 0);
    SetWindowLongPtrA(state.button.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(emoticon_popup_button_proc));
    SetCapture(state.popup_window);
    return true;
}

LRESULT HandleEmoticonPopupMessage(EmoticonPopupState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    if (message == WM_MOUSEMOVE && state.selector != nullptr) {
        const i32 x = static_cast<i16>(LOWORD(lparam)) - 1;
        const i32 y = static_cast<i16>(HIWORD(lparam)) - 1;
        if (SetBitmapTileSheetSelection(*state.selector, x, y)) {
            SetWindowPos(state.button.window, nullptr, state.selector->selected_x,
                state.selector->selected_y, 0, 0, SWP_NOSIZE);
            RedrawWindow(state.button.window, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW);
        }
    }

    if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        LRESULT brush = handle_control_color(reinterpret_cast<HDC>(wparam), message);
        if (brush != 0) {
            return brush;
        }
    }

    switch (message) {
    case WM_COMMAND:
        accept_emoticon_popup_selection(state, hwnd);
        break;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw != nullptr && wparam == kEmoticonPopupButtonId &&
            state.selector != nullptr &&
            NotifyBitmapTileSheetSelectionIfValid(*state.selector) >= 0) {
            BitBlt(draw->hDC, 0, 0, draw->rcItem.right, draw->rcItem.bottom, nullptr,
                0, 0, kClearDrawRop);
            DrawSelectedBitmapTileSheetCell(*state.selector, draw->hDC);
            break;
        }
        break;
    }
    case WM_DESTROY:
        close_emoticon_popup(state, nullptr);
        return 0;
    case WM_PAINT:
        if (hwnd == state.popup_window && state.selector != nullptr) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            BitBlt(dc, 0, 0, paint.rcPaint.right, paint.rcPaint.bottom, nullptr, 0, 0,
                kClearDrawRop);
            StretchBitmapMemoryResourceToDc(state.selector->sheet, dc, 1, 1);
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        break;
    case WM_LBUTTONUP:
        accept_emoticon_popup_selection(state, hwnd);
        break;
    case kEmoticonPopupAbortMessage:
        state.active_modal_window = nullptr;
        state.exit_to_main_requested = true;
        DestroyWindow(hwnd);
        break;
    case kEmoticonPopupCommandMessage:
        if (LOWORD(lparam) == 0x20) {
            state.active_modal_window = nullptr;
            state.exit_to_main_requested = true;
            DestroyWindow(hwnd);
            if (state.owner_dialog != nullptr) {
                DestroyWindow(state.owner_dialog);
            }
        }
        break;
    case kOnlinePromptStatusMessage0:
        ShowOnlineModalPrompt0(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage1:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage2:
        ShowOnlineModalPrompt2(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage3:
        ShowOnlineModalPrompt3(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptEndMessage:
        EndOnlineModalPrompt(online_modal_prompt_state(), static_cast<INT_PTR>(wparam));
        break;
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleEmoticonPopupButtonMessage(EmoticonPopupState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message > 0x103 && message < 0x106 && state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const LONG_PTR id = GetWindowLongPtrA(hwnd, GWLP_ID);
    if (id == kEmoticonPopupButtonId) {
        return CallWindowProcA(state.button.original_window_proc, hwnd, message, wparam,
            lparam);
    }
    return 0;
}

}

#endif
