#include "ranker_online_dialogs.h"

#ifdef _WIN32

#include "ranker_system_ui.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace ranker {
namespace {

constexpr DWORD kOnlineModelessPromptPopupStyle =
    WS_POPUP | WS_VISIBLE | BS_DEFPUSHBUTTON;
constexpr DWORD kOnlineModelessPromptChildStyle =
    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;
constexpr DWORD kOnlinePromptButtonStyle = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
constexpr int kOnlinePromptFallbackWidth = 320;
constexpr int kOnlinePromptFallbackHeight = 160;
constexpr UINT_PTR kOnlineModelessPromptInitialPaintTimerId = 0x5a1;
constexpr UINT kOnlineModelessPromptInitialPaintDelayMs = 50;

OnlineModelessPromptState g_modeless_prompt_state;
OnlineModalPromptState g_modal_prompt_state;
bool g_modeless_background_destructor_registered = false;
bool g_modeless_ok_button_destructor_registered = false;
bool g_modeless_cancel_button_destructor_registered = false;
bool g_modal_background_destructor_registered = false;
bool g_modal_ok_normal_destructor_registered = false;
bool g_modal_ok_pressed_destructor_registered = false;
bool g_modal_cancel_normal_destructor_registered = false;
bool g_modal_cancel_pressed_destructor_registered = false;

LRESULT CALLBACK online_modeless_prompt_proc(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return HandleOnlineModelessPromptMessage(g_modeless_prompt_state, hwnd,
        message, wparam, lparam);
}

LRESULT CALLBACK online_modeless_button_proc(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return HandleOnlineModelessPromptButtonMessage(g_modeless_prompt_state, hwnd,
        message, wparam, lparam);
}

void shutdown_modeless_background() {
    DestroyOnlineModelessPromptBackground(g_modeless_prompt_state);
}

void shutdown_modeless_ok_button() {
    DestroyOnlineModelessPromptOkButton(g_modeless_prompt_state);
}

void shutdown_modeless_cancel_button() {
    DestroyOnlineModelessPromptCancelButton(g_modeless_prompt_state);
}

void shutdown_modal_background() {
    DestroyOnlineModalPromptBackground(g_modal_prompt_state);
}

void shutdown_modal_ok_normal() {
    DestroyOnlineModalPromptOkNormal(g_modal_prompt_state);
}

void shutdown_modal_ok_pressed() {
    DestroyOnlineModalPromptOkPressed(g_modal_prompt_state);
}

void shutdown_modal_cancel_normal() {
    DestroyOnlineModalPromptCancelNormal(g_modal_prompt_state);
}

void shutdown_modal_cancel_pressed() {
    DestroyOnlineModalPromptCancelPressed(g_modal_prompt_state);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void clear_modal_resources(OnlineModalPromptState& state) {
    ReleaseBitmapMemoryResource(state.background);
    ReleaseBitmapMemoryResource(state.ok_normal);
    ReleaseBitmapMemoryResource(state.ok_pressed);
    ReleaseBitmapMemoryResource(state.cancel_normal);
    ReleaseBitmapMemoryResource(state.cancel_pressed);
}

HINSTANCE owner_instance(HWND owner) {
    if (owner == nullptr) {
        return GetModuleHandleA(nullptr);
    }
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrA(owner, GWLP_HINSTANCE));
    return instance != nullptr ? instance : GetModuleHandleA(nullptr);
}

bool create_prompt_button(LegacyImageButtonControl& button, HWND parent, int id,
    int x, int y, u32 normal_record, u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, "",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), x, y, 0x4c, 0x3c)) {
        return false;
    }
    SetWindowLongPtrA(button.window, GWL_STYLE, kOnlinePromptButtonStyle);
    LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    SetWindowLongPtrA(button.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(online_modeless_button_proc));
    return true;
}

void draw_centered_prompt_text(HDC dc, const RECT& bounds, const std::string& text,
    COLORREF color) {
    if (dc == nullptr) {
        return;
    }
    SetTextColor(dc, color);
    SetBkColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);

    SIZE extent{};
    GetTextExtentPoint32A(dc, text.c_str(), static_cast<int>(text.size()), &extent);
    RECT rect = bounds;
    const int inner_left = bounds.left + 0x14;
    const int inner_top = bounds.top + 0x1e;
    rect.right = bounds.right - 0x14;
    rect.bottom = bounds.bottom - 0x32;
    rect.left = inner_left + ((rect.right - inner_left) - extent.cx) / 2;
    rect.top = inner_top + ((rect.bottom - inner_top) - extent.cy) / 2;
    if (rect.right - inner_left < extent.cx) {
        rect.left = inner_left;
        rect.top = inner_top;
    }
    DrawTextA(dc, text.c_str(), -1, &rect, DT_WORDBREAK | DT_NOCLIP);
}

void post_prompt_result(OnlineModelessPromptState& state, UINT message) {
    if (state.owner != nullptr) {
        PostMessageA(state.owner, message, state.accept_wparam, state.accept_lparam);
    }
}

LRESULT handle_modeless_prompt_control_color(HDC dc, UINT message) {
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

LRESULT handle_modal_prompt_control_color(HDC dc, UINT message) {
    switch (message) {
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        SetBkColor(dc, RGB(0, 0, 0));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    default:
        return 0;
    }
}

bool paint_modeless_prompt_if_current(OnlineModelessPromptState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
    RECT client{};
    GetClientRect(hwnd, &client);
    draw_centered_prompt_text(dc, client, state.text, state.text_color);
    EndPaint(hwnd, &paint);
    return true;
}

void draw_modal_button(const BitmapMemoryResource& normal,
    const BitmapMemoryResource& pressed, const DRAWITEMSTRUCT& draw) {
    const bool pushed = draw.itemAction == ODA_SELECT &&
        (draw.itemState & ODS_FOCUS) == 0;
    StretchBitmapMemoryResourceToDc(pushed ? pressed : normal, draw.hDC, 0, 0);
}

void append_template_bytes(std::vector<u8>& bytes, const void* source,
    std::size_t size) {
    const auto* raw = static_cast<const u8*>(source);
    bytes.insert(bytes.end(), raw, raw + size);
}

void append_template_u16(std::vector<u8>& bytes, u16 value) {
    append_template_bytes(bytes, &value, sizeof(value));
}

void append_template_i16(std::vector<u8>& bytes, i16 value) {
    append_template_bytes(bytes, &value, sizeof(value));
}

void append_template_u32(std::vector<u8>& bytes, u32 value) {
    append_template_bytes(bytes, &value, sizeof(value));
}

void append_template_empty_string(std::vector<u8>& bytes) {
    append_template_u16(bytes, 0);
}

void append_template_button_class(std::vector<u8>& bytes) {
    append_template_u16(bytes, 0xffffu);
    append_template_u16(bytes, 0x0080u);
}

void align_template_dword(std::vector<u8>& bytes) {
    while ((bytes.size() & 3u) != 0) {
        bytes.push_back(0);
    }
}

void append_template_button_item(std::vector<u8>& bytes, i16 x, i16 y, i16 width,
    i16 height, u16 id) {
    align_template_dword(bytes);
    append_template_u32(bytes, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW);
    append_template_u32(bytes, 0);
    append_template_i16(bytes, x);
    append_template_i16(bytes, y);
    append_template_i16(bytes, width);
    append_template_i16(bytes, height);
    append_template_u16(bytes, id);
    append_template_button_class(bytes);
    append_template_empty_string(bytes);
    append_template_u16(bytes, 0);
}

std::vector<u8> build_modal_prompt_dialog_template(bool two_buttons) {
    std::vector<u8> bytes;
    bytes.reserve(256);
    append_template_u32(bytes, WS_POPUP | WS_VISIBLE | DS_MODALFRAME | DS_CENTER);
    append_template_u32(bytes, 0);
    append_template_u16(bytes, static_cast<u16>(two_buttons ? 3 : 2));
    append_template_i16(bytes, 0);
    append_template_i16(bytes, 0);
    append_template_i16(bytes, 220);
    append_template_i16(bytes, 120);
    append_template_empty_string(bytes);
    append_template_empty_string(bytes);
    append_template_empty_string(bytes);

    append_template_button_item(bytes, 0, 0, 220, 80,
        kOnlinePromptTextControlId);
    if (two_buttons) {
        append_template_button_item(bytes, 50, 90, 76, 60,
            kOnlinePromptModalOkButtonId);
        append_template_button_item(bytes, 150, 90, 98, 59,
            kOnlinePromptModalCancelButtonId);
    } else {
        append_template_button_item(bytes, 105, 90, 76, 60,
            kOnlinePromptModalOkButtonId);
    }
    return bytes;
}

void layout_synthetic_modal_prompt(OnlineModalPromptState& state, HWND hwnd) {
    const int width = std::max(GetBitmapMemoryResourceWidth(state.background),
        kOnlinePromptFallbackWidth);
    const int height = std::max(GetBitmapMemoryResourceHeight(state.background),
        kOnlinePromptFallbackHeight);
    RECT window_rect{0, 0, width, height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrA(hwnd, GWL_STYLE));
    const DWORD ex_style = static_cast<DWORD>(GetWindowLongPtrA(hwnd, GWL_EXSTYLE));
    AdjustWindowRectEx(&window_rect, style, FALSE, ex_style);
    const int window_width = window_rect.right - window_rect.left;
    const int window_height = window_rect.bottom - window_rect.top;
    SetWindowPos(hwnd, nullptr, 400 - window_width / 2, 300 - window_height / 2,
        window_width, window_height, SWP_NOZORDER | SWP_NOACTIVATE);

    if (HWND text = GetDlgItem(hwnd, kOnlinePromptTextControlId)) {
        SetWindowPos(text, nullptr, 0, 0, width, height - 0x3c,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HWND ok = GetDlgItem(hwnd, kOnlinePromptModalOkButtonId);
    HWND cancel = GetDlgItem(hwnd, kOnlinePromptModalCancelButtonId);
    if (cancel != nullptr) {
        SetWindowPos(ok, nullptr, 0x32, 0x60, 0x4c, 0x3c,
            SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(cancel, nullptr, 0x96, 0x60, 0x62, 0x3b,
            SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (ok != nullptr) {
        SetWindowPos(ok, nullptr, 0x69, 0x60, 0x4c, 0x3c,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

INT_PTR show_modal_fallback(OnlineModalPromptState& state, int resource_id) {
    const bool two_buttons = resource_id != 0x3a2;
    std::vector<u8> dialog_template =
        build_modal_prompt_dialog_template(two_buttons);
    state.synthetic_template = true;
    INT_PTR result = DialogBoxIndirectParamA(state.instance,
        reinterpret_cast<LPCDLGTEMPLATEA>(dialog_template.data()), state.owner,
        HandleOnlineModalPromptDialogMessage, 0);
    state.synthetic_template = false;
    if (result != -1) {
        return result;
    }

    int message_result = MessageBoxA(state.owner, state.text.c_str(), "MyModeless",
        MB_APPLMODAL | (two_buttons ? MB_OKCANCEL : MB_OK));
    state.result = message_result == IDOK ? 1 : 2;
    return state.result;
}

} // namespace

OnlineModelessPromptState& online_modeless_prompt_state() {
    return g_modeless_prompt_state;
}

OnlineModalPromptState& online_modal_prompt_state() {
    return g_modal_prompt_state;
}

void InitializeOnlineModelessPromptBackgroundStatic(
    OnlineModelessPromptState& state) {
    InitializeOnlineModelessPromptBackground(state);
    RegisterOnlineModelessPromptBackgroundDestructor(state);
}

void InitializeOnlineModelessPromptBackground(OnlineModelessPromptState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterOnlineModelessPromptBackgroundDestructor(
    OnlineModelessPromptState&) {
    register_atexit_once(g_modeless_background_destructor_registered,
        shutdown_modeless_background);
}

void DestroyOnlineModelessPromptBackground(OnlineModelessPromptState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

void InitializeOnlineModelessPromptOkButtonStatic(OnlineModelessPromptState& state) {
    InitializeOnlineModelessPromptOkButton(state);
    RegisterOnlineModelessPromptOkButtonDestructor(state);
}

void InitializeOnlineModelessPromptOkButton(OnlineModelessPromptState& state) {
    InitializeLegacyImageButtonControl(state.ok_button);
}

void RegisterOnlineModelessPromptOkButtonDestructor(OnlineModelessPromptState&) {
    register_atexit_once(g_modeless_ok_button_destructor_registered,
        shutdown_modeless_ok_button);
}

void DestroyOnlineModelessPromptOkButton(OnlineModelessPromptState& state) {
    DestroyLegacyImageButtonControl(state.ok_button);
}

void InitializeOnlineModelessPromptCancelButtonStatic(
    OnlineModelessPromptState& state) {
    InitializeOnlineModelessPromptCancelButton(state);
    RegisterOnlineModelessPromptCancelButtonDestructor(state);
}

void InitializeOnlineModelessPromptCancelButton(OnlineModelessPromptState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterOnlineModelessPromptCancelButtonDestructor(
    OnlineModelessPromptState&) {
    register_atexit_once(g_modeless_cancel_button_destructor_registered,
        shutdown_modeless_cancel_button);
}

void DestroyOnlineModelessPromptCancelButton(OnlineModelessPromptState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InitializeOnlineModelessPromptButtons(OnlineModelessPromptState& state) {
    InitializeOnlineModelessPromptOkButtonStatic(state);
    InitializeOnlineModelessPromptCancelButtonStatic(state);
}

void DestroyOnlineModelessPromptButtons(OnlineModelessPromptState& state) {
    DestroyOnlineModelessPromptOkButton(state);
    DestroyOnlineModelessPromptCancelButton(state);
}

void IgnoreOnlineModelessPromptReservedHelper() {
}

void InstallOnlineModelessPromptAcceleratorTarget(OnlineModelessPromptState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = active.active_accelerators;
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreOnlineModelessPromptAcceleratorTarget(OnlineModelessPromptState& state) {
    if (RankerMainWindowState().active_accelerator_window != state.window) {
        return;
    }
    SetActiveAcceleratorState(nullptr, state.active_accelerators);
    DestroyAcceleratorTable(state.active_accelerators);
    state.active_accelerators = state.saved_accelerators;
    state.active_accelerator_window = state.saved_accelerator_window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

bool CreateOnlineModelessPrompt(OnlineModelessPromptState& state, HWND owner,
    HINSTANCE instance, const char* text, COLORREF text_color, bool two_buttons,
    WPARAM accept_wparam, LPARAM accept_lparam) {
    state.owner = owner;
    state.main_window = owner;
    state.instance = instance != nullptr ? instance : owner_instance(owner);
    state.text = text == nullptr ? "" : text;
    state.text_color = text_color;
    state.two_buttons = two_buttons;
    state.accept_wparam = accept_wparam;
    state.accept_lparam = accept_lparam;

    InitializeOnlineModelessPromptBackgroundStatic(state);
    InitializeOnlineModelessPromptButtons(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kOnlinePromptBackgroundRecord);

    const int width = std::max(GetBitmapMemoryResourceWidth(state.background),
        kOnlinePromptFallbackWidth);
    const int height = std::max(GetBitmapMemoryResourceHeight(state.background),
        kOnlinePromptFallbackHeight);
    DWORD window_style = kOnlineModelessPromptPopupStyle;
    int window_x = 400 - width / 2;
    int window_y = 300 - height / 2;
    if (owner != nullptr && IsWindow(owner)) {
        // Reconstructed frontend screens are child windows of the main game
        // window.  The original absolute 800x600 popup origin therefore put
        // status prompts at the desktop's upper-left instead of in the game.
        // Keep the prompt in the frontend hierarchy and center it using the
        // owner's client coordinates.
        RECT owner_client{};
        GetClientRect(owner, &owner_client);
        window_style = kOnlineModelessPromptChildStyle;
        const int owner_width = static_cast<int>(
            owner_client.right - owner_client.left);
        const int owner_height = static_cast<int>(
            owner_client.bottom - owner_client.top);
        window_x = std::max(0, (owner_width - width) / 2);
        window_y = std::max(0, (owner_height - height) / 2);
    }
    state.window = CreateWindowExA(0, "button", "MyModeless",
        window_style, window_x, window_y, width, height,
        owner, nullptr, state.instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }

    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(online_modeless_prompt_proc));
    if ((window_style & WS_CHILD) != 0) {
        // CreateWindowEx places this reconstructed child behind the P2P
        // screen's existing edit/button controls.  Put the prompt at the top
        // of the sibling z-order so the full bitmap is visible as an overlay.
        SetWindowPos(state.window, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (two_buttons) {
        create_prompt_button(state.ok_button, state.window, kOnlinePromptOkButtonId,
            0x32, 0x60, kOnlinePromptOkNormalRecord, kOnlinePromptOkPressedRecord);
        create_prompt_button(state.cancel_button, state.window,
            kOnlinePromptCancelButtonId, 0x96, 0x60,
            kOnlinePromptCancelNormalRecord, kOnlinePromptCancelPressedRecord);
    } else {
        create_prompt_button(state.ok_button, state.window, kOnlinePromptOkButtonId,
            0x69, 0x60, kOnlinePromptOkNormalRecord, kOnlinePromptOkPressedRecord);
    }
    InstallOnlineModelessPromptAcceleratorTarget(state);
    RedrawWindow(state.window, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    // The caller completes one final frontend paint after this function
    // returns.  Repaint after that stack unwinds so the overlay is not left
    // showing the parent's pixels until an external invalidation.
    SetTimer(state.window, kOnlineModelessPromptInitialPaintTimerId,
        kOnlineModelessPromptInitialPaintDelayMs, nullptr);
    return true;
}

LRESULT HandleOnlineModelessPromptMessage(OnlineModelessPromptState& state,
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }
    if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        LRESULT brush = handle_modeless_prompt_control_color(
            reinterpret_cast<HDC>(wparam), message);
        if (brush != 0) {
            return brush;
        }
    }

    switch (message) {
    case WM_DESTROY:
        KillTimer(hwnd, kOnlineModelessPromptInitialPaintTimerId);
        DestroyOnlineModelessPromptButtons(state);
        DestroyOnlineModelessPromptBackground(state);
        RestoreOnlineModelessPromptAcceleratorTarget(state);
        state.window = nullptr;
        return 0;
    case WM_TIMER:
        if (wparam == kOnlineModelessPromptInitialPaintTimerId) {
            KillTimer(hwnd, kOnlineModelessPromptInitialPaintTimerId);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(hwnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW |
                    RDW_ALLCHILDREN);
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw != nullptr &&
            (draw->CtlID == kOnlinePromptOkButtonId ||
                draw->CtlID == kOnlinePromptCancelButtonId)) {
            LegacyImageButtonControl* button =
                draw->CtlID == kOnlinePromptOkButtonId ?
                &state.ok_button : &state.cancel_button;
            DrawLegacyImageButtonItem(*button, *draw);
            break;
        }
        break;
    }
    case WM_KEYDOWN:
        if (wparam == VK_RETURN) {
            post_prompt_result(state, kOnlinePromptAcceptMessage);
            DestroyWindow(hwnd);
            if (paint_modeless_prompt_if_current(state, hwnd)) {
                return 0;
            }
            break;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wparam) == kOnlinePromptOkButtonId) {
            post_prompt_result(state, kOnlinePromptAcceptMessage);
            DestroyWindow(hwnd);
            if (paint_modeless_prompt_if_current(state, hwnd)) {
                return 0;
            }
            break;
        }
        if (LOWORD(wparam) == kOnlinePromptCancelButtonId) {
            post_prompt_result(state, kOnlinePromptCancelMessage);
            DestroyWindow(hwnd);
            if (paint_modeless_prompt_if_current(state, hwnd)) {
                return 0;
            }
            break;
        }
        break;
    case WM_PAINT: {
        if (paint_modeless_prompt_if_current(state, hwnd)) {
            return 0;
        }
        break;
    }
    case kOnlinePromptCancelMessage:
        DestroyWindow(hwnd);
        break;
    case kOnlinePromptStatusMessage0:
        ShowOnlineModalPrompt0(g_modal_prompt_state, hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage1:
        ShowOnlineModalPrompt1(g_modal_prompt_state, hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage2:
        ShowOnlineModalPrompt2(g_modal_prompt_state, hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage3:
        ShowOnlineModalPrompt3(g_modal_prompt_state, hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptEndMessage:
        EndOnlineModalPrompt(g_modal_prompt_state, static_cast<INT_PTR>(wparam));
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleOnlineModelessPromptButtonMessage(OnlineModelessPromptState& state,
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const LONG_PTR id = GetWindowLongPtrA(hwnd, GWLP_ID);
    if (id == kOnlinePromptOkButtonId) {
        return CallWindowProcA(state.ok_button.original_window_proc, hwnd,
            message, wparam, lparam);
    }
    if (id == kOnlinePromptCancelButtonId) {
        return CallWindowProcA(state.cancel_button.original_window_proc, hwnd,
            message, wparam, lparam);
    }
    return 0;
}

void InitializeOnlineModalPromptBackgroundStatic(OnlineModalPromptState& state) {
    InitializeOnlineModalPromptBackground(state);
    RegisterOnlineModalPromptBackgroundDestructor(state);
}

void InitializeOnlineModalPromptBackground(OnlineModalPromptState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterOnlineModalPromptBackgroundDestructor(OnlineModalPromptState&) {
    register_atexit_once(g_modal_background_destructor_registered,
        shutdown_modal_background);
}

void DestroyOnlineModalPromptBackground(OnlineModalPromptState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

void InitializeOnlineModalPromptOkNormalStatic(OnlineModalPromptState& state) {
    InitializeOnlineModalPromptOkNormal(state);
    RegisterOnlineModalPromptOkNormalDestructor(state);
}

void InitializeOnlineModalPromptOkNormal(OnlineModalPromptState& state) {
    InitializeBitmapMemoryResource(state.ok_normal);
}

void RegisterOnlineModalPromptOkNormalDestructor(OnlineModalPromptState&) {
    register_atexit_once(g_modal_ok_normal_destructor_registered,
        shutdown_modal_ok_normal);
}

void DestroyOnlineModalPromptOkNormal(OnlineModalPromptState& state) {
    HandleBitmapMemoryResourceDestructor(state.ok_normal);
}

void InitializeOnlineModalPromptOkPressedStatic(OnlineModalPromptState& state) {
    InitializeOnlineModalPromptOkPressed(state);
    RegisterOnlineModalPromptOkPressedDestructor(state);
}

void InitializeOnlineModalPromptOkPressed(OnlineModalPromptState& state) {
    InitializeBitmapMemoryResource(state.ok_pressed);
}

void RegisterOnlineModalPromptOkPressedDestructor(OnlineModalPromptState&) {
    register_atexit_once(g_modal_ok_pressed_destructor_registered,
        shutdown_modal_ok_pressed);
}

void DestroyOnlineModalPromptOkPressed(OnlineModalPromptState& state) {
    HandleBitmapMemoryResourceDestructor(state.ok_pressed);
}

void InitializeOnlineModalPromptCancelNormalStatic(OnlineModalPromptState& state) {
    InitializeOnlineModalPromptCancelNormal(state);
    RegisterOnlineModalPromptCancelNormalDestructor(state);
}

void InitializeOnlineModalPromptCancelNormal(OnlineModalPromptState& state) {
    InitializeBitmapMemoryResource(state.cancel_normal);
}

void RegisterOnlineModalPromptCancelNormalDestructor(OnlineModalPromptState&) {
    register_atexit_once(g_modal_cancel_normal_destructor_registered,
        shutdown_modal_cancel_normal);
}

void DestroyOnlineModalPromptCancelNormal(OnlineModalPromptState& state) {
    HandleBitmapMemoryResourceDestructor(state.cancel_normal);
}

void InitializeOnlineModalPromptCancelPressedStatic(OnlineModalPromptState& state) {
    InitializeOnlineModalPromptCancelPressed(state);
    RegisterOnlineModalPromptCancelPressedDestructor(state);
}

void InitializeOnlineModalPromptCancelPressed(OnlineModalPromptState& state) {
    InitializeBitmapMemoryResource(state.cancel_pressed);
}

void RegisterOnlineModalPromptCancelPressedDestructor(OnlineModalPromptState&) {
    register_atexit_once(g_modal_cancel_pressed_destructor_registered,
        shutdown_modal_cancel_pressed);
}

void DestroyOnlineModalPromptCancelPressed(OnlineModalPromptState& state) {
    HandleBitmapMemoryResourceDestructor(state.cancel_pressed);
}

void InitializeOnlineModalPromptResources(OnlineModalPromptState& state) {
    InitializeOnlineModalPromptBackgroundStatic(state);
    InitializeOnlineModalPromptOkNormalStatic(state);
    InitializeOnlineModalPromptOkPressedStatic(state);
    InitializeOnlineModalPromptCancelNormalStatic(state);
    InitializeOnlineModalPromptCancelPressedStatic(state);
}

void DestroyOnlineModalPromptResources(OnlineModalPromptState& state) {
    DestroyOnlineModalPromptBackground(state);
    DestroyOnlineModalPromptOkNormal(state);
    DestroyOnlineModalPromptOkPressed(state);
    DestroyOnlineModalPromptCancelNormal(state);
    DestroyOnlineModalPromptCancelPressed(state);
}

void LoadOnlineModalPromptResources(OnlineModalPromptState& state) {
    clear_modal_resources(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kOnlinePromptBackgroundRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.ok_normal, "Jw2_19.trc",
        kOnlinePromptOkNormalRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.ok_pressed, "Jw2_19.trc",
        kOnlinePromptOkPressedRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.cancel_normal, "Jw2_19.trc",
        kOnlinePromptCancelNormalRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.cancel_pressed, "Jw2_19.trc",
        kOnlinePromptCancelPressedRecord);
}

INT_PTR ShowOnlineModalPromptResource(OnlineModalPromptState& state, HWND owner,
    HINSTANCE instance, int resource_id, const char* text, COLORREF text_color) {
    if (state.dialog != nullptr) {
        EndOnlineModalPrompt(state, 0);
    }
    state.owner = owner;
    state.main_window = owner;
    state.instance = instance != nullptr ? instance : owner_instance(owner);
    state.text = text == nullptr ? "" : text;
    state.text_color = text_color;
    InitializeOnlineModalPromptResources(state);
    LoadOnlineModalPromptResources(state);

    INT_PTR result = DialogBoxParamA(state.instance,
        MAKEINTRESOURCEA(resource_id), owner, HandleOnlineModalPromptDialogMessage,
        0);
    if (result == -1) {
        result = show_modal_fallback(state, resource_id);
    }
    state.result = result;
    return result;
}

INT_PTR ShowOnlineModalPrompt0(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color) {
    return ShowOnlineModalPromptResource(state, owner, owner_instance(owner), 0x3a2,
        text, text_color);
}

INT_PTR ShowOnlineModalPrompt1(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color) {
    return ShowOnlineModalPromptResource(state, owner, owner_instance(owner), 0x3a3,
        text, text_color);
}

INT_PTR ShowOnlineModalPrompt2(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color) {
    return ShowOnlineModalPromptResource(state, owner, owner_instance(owner), 0x3a4,
        text, text_color);
}

INT_PTR ShowOnlineModalPrompt3(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color) {
    return ShowOnlineModalPromptResource(state, owner, owner_instance(owner), 0x3a5,
        text, text_color);
}

void EndOnlineModalPrompt(OnlineModalPromptState& state, INT_PTR result) {
    state.result = result;
    if (state.dialog != nullptr) {
        HWND dialog = state.dialog;
        EndDialog(dialog, result);
        state.dialog = nullptr;
    }
}

void UpdateOnlineModalPromptText(OnlineModalPromptState& state, const char* text) {
    if (state.dialog == nullptr) {
        return;
    }
    state.text = text == nullptr ? "" : text;
    RedrawWindow(state.dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

INT_PTR CALLBACK HandleOnlineModalPromptDialogMessage(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    OnlineModalPromptState& state = g_modal_prompt_state;
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }
    if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        LRESULT brush = handle_modal_prompt_control_color(
            reinterpret_cast<HDC>(wparam), message);
        if (brush != 0) {
            return brush;
        }
    }

    switch (message) {
    case WM_INITDIALOG:
        state.dialog = hwnd;
        SendDlgItemMessageA(hwnd, kOnlinePromptTextControlId, WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        if (state.synthetic_template) {
            layout_synthetic_modal_prompt(state, hwnd);
        }
        return TRUE;
    case WM_DESTROY:
        clear_modal_resources(state);
        state.dialog = nullptr;
        state.synthetic_template = false;
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
        if (GetDlgItem(hwnd, kOnlinePromptTextControlId) == nullptr) {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            draw_centered_prompt_text(dc, rect, state.text, state.text_color);
        }
        EndPaint(hwnd, &paint);
        return TRUE;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return FALSE;
        }
        if (draw->CtlID == kOnlinePromptTextControlId) {
            draw_centered_prompt_text(draw->hDC, draw->rcItem, state.text,
                state.text_color);
            return TRUE;
        }
        if (draw->CtlID == kOnlinePromptModalOkButtonId) {
            draw_modal_button(state.ok_normal, state.ok_pressed, *draw);
            return TRUE;
        }
        if (draw->CtlID == kOnlinePromptModalCancelButtonId) {
            draw_modal_button(state.cancel_normal, state.cancel_pressed, *draw);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == kOnlinePromptModalOkButtonId) {
            if (state.owner != nullptr) {
                PostMessageA(state.owner, kOnlinePromptAcceptMessage, 0, 0);
            }
            EndOnlineModalPrompt(state, 1);
            return TRUE;
        }
        if (LOWORD(wparam) == kOnlinePromptModalCancelButtonId) {
            HWND cancel = GetDlgItem(hwnd, kOnlinePromptModalCancelButtonId);
            LONG_PTR style = cancel != nullptr ?
                GetWindowLongPtrA(cancel, GWL_STYLE) : 0;
            if ((style & WS_DISABLED) == 0) {
                if (state.owner != nullptr) {
                    PostMessageA(state.owner, kOnlinePromptCancelMessage, 0, 0);
                }
                EndOnlineModalPrompt(state, 2);
            }
            return TRUE;
        }
        break;
    default:
        break;
    }
    (void)lparam;
    return FALSE;
}

} // namespace ranker

#endif
