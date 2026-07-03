#include "ranker_change_password.h"

#ifdef _WIN32

#include "ranker_connect_frontend.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_setup_data.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed = 0x10cf0000;
constexpr DWORD kAccountEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kPasswordEditStyle = WS_CHILD | WS_VISIBLE | ES_PASSWORD;
constexpr DWORD kStatusEditStyle = WS_CHILD | ES_MULTILINE | ES_READONLY;
constexpr COLORREF kChangePasswordTextYellow = RGB(255, 255, 0);
constexpr COLORREF kChangePasswordSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kChangePasswordWhite = RGB(255, 255, 255);
constexpr COLORREF kChangePasswordBlack = RGB(0, 0, 0);
constexpr COLORREF kChangePasswordErrorBlue = RGB(10, 10, 250);

ChangePasswordState g_change_password_state;
bool g_background_shutdown_registered = false;
bool g_ok_button_shutdown_registered = false;
bool g_cancel_button_shutdown_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK change_password_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleChangePasswordWindowMessage(g_change_password_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK change_password_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleChangePasswordControlMessage(g_change_password_state, hwnd, message,
        wparam, lparam);
}

void shutdown_global_background() {
    ShutdownChangePasswordBackgroundBitmap(g_change_password_state);
}

void shutdown_global_ok_button() {
    ShutdownChangePasswordOkButton(g_change_password_state);
}

void shutdown_global_cancel_button() {
    ShutdownChangePasswordCancelButton(g_change_password_state);
}

ChangePasswordLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return ChangePasswordLayoutRect{};
}

void clear_text_control(ChangePasswordTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_text_control(ChangePasswordTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_password_control_proc));
}

bool create_text_control(ChangePasswordTextControl& control, HWND parent,
    HINSTANCE instance, DWORD style, int id, const ChangePasswordLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, "edit", nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_text_control(control);
        return false;
    }
    subclass_text_control(control);
    return true;
}

LegacyImageButtonControl* image_button_for_id(ChangePasswordState& state, int id) {
    if (id == kChangePasswordOkButtonId) {
        return &state.ok_button;
    }
    if (id == kChangePasswordCancelButtonId) {
        return &state.cancel_button;
    }
    return nullptr;
}

WNDPROC original_proc_for_id(ChangePasswordState& state, int id) {
    switch (id) {
    case kChangePasswordAccountEditId:
        return state.account_edit.original_window_proc;
    case kChangePasswordOldPasswordEditId:
        return state.old_password_edit.original_window_proc;
    case kChangePasswordNewPasswordEditId:
        return state.new_password_edit.original_window_proc;
    case kChangePasswordConfirmPasswordEditId:
        return state.confirm_password_edit.original_window_proc;
    case kChangePasswordStatusEditId:
        return state.status_edit.original_window_proc;
    case kChangePasswordOkButtonId:
        return state.ok_button.original_window_proc;
    case kChangePasswordCancelButtonId:
        return state.cancel_button.original_window_proc;
    default:
        return nullptr;
    }
}

void show_password_message(ChangePasswordState& state, const char* text,
    COLORREF color = kChangePasswordSoftWhite) {
    state.last_message = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
    }
}

void queue_packet(ChangePasswordState& state, const void* packet, i32 byte_count) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet, byte_count);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket,
            const_cast<void*>(packet), byte_count, nullptr);
    }
}

void write_le32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset + 4 > buffer.size()) {
        return;
    }
    buffer[offset] = static_cast<u8>(value & 0xff);
    buffer[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
    buffer[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
    buffer[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

u32 read_le32(const u8* buffer, i32 byte_count, std::size_t offset) {
    if (buffer == nullptr || byte_count < 0 ||
        offset + 4 > static_cast<std::size_t>(byte_count)) {
        return 0;
    }
    return static_cast<u32>(buffer[offset]) |
        (static_cast<u32>(buffer[offset + 1]) << 8) |
        (static_cast<u32>(buffer[offset + 2]) << 16) |
        (static_cast<u32>(buffer[offset + 3]) << 24);
}

void copy_c_string(std::vector<u8>& buffer, std::size_t offset, std::size_t field_size,
    const char* text) {
    if (offset >= buffer.size()) {
        return;
    }
    const std::size_t available = std::min(field_size, buffer.size() - offset);
    if (available == 0) {
        return;
    }
    std::memset(buffer.data() + offset, 0, available);
    if (text == nullptr) {
        return;
    }
    std::strncpy(reinterpret_cast<char*>(buffer.data() + offset), text, available - 1);
}

void read_window_text(HWND window, char* target, int target_size) {
    if (target == nullptr || target_size <= 0) {
        return;
    }
    target[0] = '\0';
    if (window != nullptr) {
        GetWindowTextA(window, target, target_size);
    }
}

const char* startup_message_row(std::size_t index, const char* fallback) {
    const auto& rows = startup_text_tables().message_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

const char* change_password_status_message(u32 status) {
    switch (status) {
    case 1:
        return startup_message_row(6, "Another player is using this ID.");
    case 2:
        return startup_message_row(7, "The password is incorrect.");
    case 3:
        return startup_message_row(8, "The CD key is invalid.");
    case 4:
        return startup_message_row(9, "The CD key cannot be used.");
    case 5:
        return startup_message_row(10, "The CD key is already in use.");
    case 6:
        return startup_message_row(11, "Too many users are connected.");
    case 7:
        return startup_message_row(12, "This ID is not registered.");
    default:
        return startup_message_row(13, "An unknown error occurred.");
    }
}

void destroy_password_window(ChangePasswordState& state) {
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
}

void write_setup_data(ChangePasswordState& state) {
    if (state.callbacks.write_setup_data != nullptr) {
        state.callbacks.write_setup_data(state);
        return;
    }
    ExportSetupText(kSetupWizardAccountOffset, state.submitted_account);
    WriteDefaultSetupDataBuffer();
}

void handle_success(ChangePasswordState& state) {
    read_window_text(state.account_edit.window, state.submitted_account.data(),
        static_cast<int>(state.submitted_account.size()));
    write_setup_data(state);
    destroy_password_window(state);
    if (state.callbacks.open_wizard_soft_net != nullptr) {
        state.callbacks.open_wizard_soft_net(state);
    }
}

void handle_response_status(ChangePasswordState& state, u32 status) {
    switch (status) {
    case 0:
        handle_success(state);
        break;
    default:
        show_password_message(state, change_password_status_message(status),
            kChangePasswordErrorBlue);
        break;
    }
}

void release_password_resources(ChangePasswordState& state) {
    RestoreChangePasswordAccelerators(state);
    ShutdownChangePasswordBackgroundBitmap(state);
    ShutdownChangePasswordOkButton(state);
    ShutdownChangePasswordCancelButton(state);
    clear_text_control(state.account_edit);
    clear_text_control(state.old_password_edit);
    clear_text_control(state.new_password_edit);
    clear_text_control(state.confirm_password_edit);
    clear_text_control(state.status_edit);
    state.window = nullptr;
    state.visible = false;
}

} // namespace

ChangePasswordState& change_password_state() {
    return g_change_password_state;
}

void InitializeChangePasswordBackgroundResourceAndShutdown(
    ChangePasswordState& state) {
    InitializeChangePasswordBackgroundBitmap(state);
    RegisterChangePasswordBackgroundShutdown(state);
}

void InitializeChangePasswordBackgroundBitmap(ChangePasswordState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterChangePasswordBackgroundShutdown(ChangePasswordState&) {
    if (!g_background_shutdown_registered) {
        std::atexit(shutdown_global_background);
        g_background_shutdown_registered = true;
    }
}

void ShutdownChangePasswordBackgroundBitmap(ChangePasswordState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeChangePasswordOkButtonSupport(ChangePasswordState& state) {
    InitializeChangePasswordOkButton(state);
    RegisterChangePasswordOkButtonShutdown(state);
}

void InitializeChangePasswordOkButton(ChangePasswordState& state) {
    InitializeLegacyImageButtonControl(state.ok_button);
}

void RegisterChangePasswordOkButtonShutdown(ChangePasswordState&) {
    if (!g_ok_button_shutdown_registered) {
        std::atexit(shutdown_global_ok_button);
        g_ok_button_shutdown_registered = true;
    }
}

void ShutdownChangePasswordOkButton(ChangePasswordState& state) {
    DestroyLegacyImageButtonControl(state.ok_button);
}

void InitializeChangePasswordCancelButtonSupport(ChangePasswordState& state) {
    InitializeChangePasswordCancelButton(state);
    RegisterChangePasswordCancelButtonShutdown(state);
}

void InitializeChangePasswordCancelButton(ChangePasswordState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterChangePasswordCancelButtonShutdown(ChangePasswordState&) {
    if (!g_cancel_button_shutdown_registered) {
        std::atexit(shutdown_global_cancel_button);
        g_cancel_button_shutdown_registered = true;
    }
}

void ShutdownChangePasswordCancelButton(ChangePasswordState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InstallChangePasswordAccelerators(ChangePasswordState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kChangePasswordAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreChangePasswordAccelerators(ChangePasswordState& state) {
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

bool CreateChangePasswordWindow(ChangePasswordState& state, HWND parent,
    HINSTANCE instance, LPARAM account_text, LPARAM old_password_text) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = 0;
    state.reconnect_requested = false;
    InitializeChangePasswordBackgroundResourceAndShutdown(state);
    InitializeChangePasswordOkButtonSupport(state);
    InitializeChangePasswordCancelButtonSupport(state);

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kChangePasswordLayoutTrcRecord)) {
        return false;
    }

    const ChangePasswordLayoutRect window_rect = layout_at(layout.table, 0);
    const POINT origin = RankerFrontendWindowOrigin();
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "ChangePassword",
        "ChangePassword", style, origin.x, origin.y, window_rect.width,
        window_rect.height, parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_password_window_proc));

    if (!create_text_control(state.account_edit, state.window, instance,
            kAccountEditStyle, kChangePasswordAccountEditId,
            layout_at(layout.table, 1)) ||
        !create_text_control(state.old_password_edit, state.window, instance,
            kPasswordEditStyle, kChangePasswordOldPasswordEditId,
            layout_at(layout.table, 2)) ||
        !create_text_control(state.new_password_edit, state.window, instance,
            kPasswordEditStyle, kChangePasswordNewPasswordEditId,
            layout_at(layout.table, 3)) ||
        !create_text_control(state.confirm_password_edit, state.window, instance,
            kPasswordEditStyle, kChangePasswordConfirmPasswordEditId,
            layout_at(layout.table, 4)) ||
        !create_text_control(state.status_edit, state.window, instance,
            kStatusEditStyle, kChangePasswordStatusEditId,
            layout_at(layout.table, 5))) {
        release_password_resources(state);
        return false;
    }

    SendMessageA(state.account_edit.window, EM_LIMITTEXT, 0x1f, 0);
    SendMessageA(state.old_password_edit.window, EM_LIMITTEXT, 9, 0);
    SendMessageA(state.new_password_edit.window, EM_LIMITTEXT, 9, 0);
    SendMessageA(state.confirm_password_edit.window, EM_LIMITTEXT, 9, 0);

    const ChangePasswordLayoutRect ok_rect = layout_at(layout.table, 6);
    if (!CreateLegacyImageButtonWindow(state.ok_button, state.window, "",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChangePasswordOkButtonId)),
            ok_rect.x, ok_rect.y, ok_rect.width, ok_rect.height)) {
        release_password_resources(state);
        return false;
    }
    LoadLegacyImageButtonBitmaps(state.ok_button, kChangePasswordOkNormalBitmapRecord,
        kChangePasswordOkPressedBitmapRecord);

    const ChangePasswordLayoutRect cancel_rect = layout_at(layout.table, 7);
    if (!CreateLegacyImageButtonWindow(state.cancel_button, state.window, "&Cancel",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChangePasswordCancelButtonId)),
            cancel_rect.x, cancel_rect.y, cancel_rect.width, cancel_rect.height)) {
        release_password_resources(state);
        return false;
    }
    LoadLegacyImageButtonBitmaps(state.cancel_button,
        kChangePasswordCancelNormalBitmapRecord,
        kChangePasswordCancelPressedBitmapRecord);

    SetWindowLongPtrA(GetLegacyImageButtonWindow(state.ok_button), GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_password_control_proc));
    SetWindowLongPtrA(GetLegacyImageButtonWindow(state.cancel_button), GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_password_control_proc));

    SendMessageA(state.window, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.status_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    InstallChangePasswordAccelerators(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kChangePasswordBackgroundBitmapRecord);
    ShowWindow(state.status_edit.window, SW_SHOW);
    SetFocus(state.new_password_edit.window);
    SetWindowTextA(state.account_edit.window, reinterpret_cast<LPCSTR>(account_text));
    SetWindowTextA(state.old_password_edit.window,
        reinterpret_cast<LPCSTR>(old_password_text));

    state.visible = true;
    QueueChangePasswordStatusRequest(state);
    return true;
}

LRESULT HandleChangePasswordWindowMessage(ChangePasswordState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        release_password_resources(state);
        return 0;
    case WM_PAINT:
        if (hwnd == state.window) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        LegacyImageButtonControl* button = image_button_for_id(state, draw->CtlID);
        if (button != nullptr) {
            DrawLegacyImageButtonItem(*button, *draw);
            break;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        switch (id) {
        case kChangePasswordFocusOldPasswordCommandId:
            SetFocus(state.old_password_edit.window);
            return 0;
        case kChangePasswordFocusNewPasswordCommandId:
            SetFocus(state.new_password_edit.window);
            return 0;
        case kChangePasswordFocusConfirmPasswordCommandId:
            SetFocus(state.confirm_password_edit.window);
            return 0;
        case kChangePasswordFocusAccountCommandId:
            SetFocus(state.account_edit.window);
            return 0;
        case kChangePasswordOkButtonId:
            HandleDefaultFrontendUiClickSound();
            SubmitChangePasswordRequest(state);
            return 0;
        case kChangePasswordCancelButtonId:
            HandleDefaultFrontendUiClickSound();
            destroy_password_window(state);
            if (state.callbacks.open_connect_frontend != nullptr) {
                state.callbacks.open_connect_frontend(state);
            }
            return 0;
        case 0x9c41: {
            HWND focus = GetFocus();
            const int focused_id = static_cast<int>(GetWindowLongPtrA(focus, GWLP_ID));
            switch (focused_id - kChangePasswordAccountEditId) {
            case 0:
                SetFocus(state.old_password_edit.window);
                break;
            case 1:
                SetFocus(state.new_password_edit.window);
                break;
            case 2:
                SetFocus(state.confirm_password_edit.window);
                break;
            case 3:
                SetFocus(state.account_edit.window);
                break;
            default:
                SetFocus(state.account_edit.window);
                break;
            }
            return 0;
        }
        default:
            break;
        }
        break;
    }
    case kChangePasswordNetworkMessage:
        DispatchChangePasswordNetworkMessage(state, wparam, lparam);
        return 0;
    case kChangePasswordReconnectMessage:
        state.reconnect_requested = true;
        destroy_password_window(state);
        if (state.callbacks.open_connect_frontend != nullptr) {
            state.callbacks.open_connect_frontend(state);
        }
        return 0;
    case kChangePasswordStatusMessage0:
    case kChangePasswordStatusMessage1:
    case kChangePasswordStatusMessage2:
    case kChangePasswordStatusMessage3:
        show_password_message(state, reinterpret_cast<const char*>(wparam),
            static_cast<COLORREF>(lparam));
        return 0;
    case kChangePasswordBusyMessage:
        if (state.callbacks.set_busy != nullptr) {
            state.callbacks.set_busy(state);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kChangePasswordTextYellow);
        SetBkColor(reinterpret_cast<HDC>(wparam), kChangePasswordBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kChangePasswordWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kChangePasswordBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kChangePasswordSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kChangePasswordBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void DeleteChangePasswordButton(LegacyImageButtonControl& button, bool free_storage) {
    DestroyLegacyImageButtonControl(button);
    if (free_storage) {
        // Original scalar deleting destructor optionally freed the object storage.
        // Buildable objects are state-owned, so no heap free is performed here.
    }
}

LRESULT HandleChangePasswordControlMessage(ChangePasswordState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    switch (id) {
    case kChangePasswordAccountEditId:
    case kChangePasswordOldPasswordEditId:
    case kChangePasswordNewPasswordEditId:
    case kChangePasswordConfirmPasswordEditId:
    case kChangePasswordStatusEditId:
    case kChangePasswordOkButtonId:
    case kChangePasswordCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message, wparam,
            lparam);
    default:
        return 0;
    }
}

bool SubmitChangePasswordRequest(ChangePasswordState& state) {
    read_window_text(state.account_edit.window, state.submitted_account.data(),
        static_cast<int>(state.submitted_account.size()));
    read_window_text(state.old_password_edit.window,
        state.submitted_old_password.data(),
        static_cast<int>(state.submitted_old_password.size()));
    read_window_text(state.new_password_edit.window,
        state.submitted_new_password.data(),
        static_cast<int>(state.submitted_new_password.size()));
    read_window_text(state.confirm_password_edit.window,
        state.submitted_confirm_password.data(),
        static_cast<int>(state.submitted_confirm_password.size()));

    if (std::strlen(state.submitted_account.data()) == 0 ||
        std::strlen(state.submitted_old_password.data()) == 0 ||
        std::strlen(state.submitted_new_password.data()) == 0 ||
        std::strlen(state.submitted_confirm_password.data()) == 0) {
        show_password_message(state,
            startup_message_row(67, "Fill in every required field."));
        return false;
    }

    const std::size_t new_len = std::strlen(state.submitted_new_password.data());
    const std::size_t confirm_len =
        std::strlen(state.submitted_confirm_password.data());
    if (new_len < 4 || confirm_len < 4) {
        show_password_message(state,
            startup_message_row(68, "Password must be at least 4 characters."));
        return false;
    }
    if (new_len >= 0x0b || confirm_len >= 0x0b) {
        char text[256]{};
        std::snprintf(text, sizeof(text),
            startup_message_row(69, "Password must be %d characters or fewer."), 10);
        show_password_message(state, text);
        return false;
    }
    if (std::strcmp(state.submitted_new_password.data(),
            state.submitted_confirm_password.data()) != 0) {
        show_password_message(state,
            startup_message_row(70, "Password confirmation does not match."));
        return false;
    }

    char trc_key[16]{};
    if (!BuildTrcRecord10Key(trc_key)) {
        show_password_message(state,
            startup_message_row(21, "Unable to build setup verification key."));
        return false;
    }

    std::vector<u8> packet(kChangePasswordWirePacketBytes, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x17);
    write_le32(packet, 8, kChangePasswordWirePacketBytes);
    copy_c_string(packet, 0x0d, 0x80, state.submitted_account.data());
    copy_c_string(packet, 0x8d, 0x80, state.submitted_old_password.data());
    copy_c_string(packet, 0x10d, 0x80, state.submitted_new_password.data());
    copy_c_string(packet, 0x18d, 0x13, trc_key);
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
    return true;
}

void QueueChangePasswordStatusRequest(ChangePasswordState& state) {
    std::vector<u8> packet(kChangePasswordStatusRequestBytes, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x3b);
    write_le32(packet, 8, kChangePasswordStatusRequestBytes);
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void DispatchChangePasswordNetworkMessage(ChangePasswordState& state, WPARAM,
    LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        show_password_message(state,
            startup_message_row(5, "Disconnected from the server."),
            kChangePasswordErrorBlue);
        if (state.async_tcp_socket != nullptr) {
            CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
        }
        state.reconnect_requested = true;
        destroy_password_window(state);
        if (state.callbacks.open_connect_frontend != nullptr) {
            state.callbacks.open_connect_frontend(state);
        }
        return;
    }
    if (event != 1) {
        return;
    }

    if (state.callbacks.receive_payload != nullptr) {
        i32 byte_count = 0;
        const u8* payload = nullptr;
        payload = state.callbacks.receive_payload(state, byte_count);
        if (payload == nullptr || byte_count < 0x11) {
            return;
        }
        const u32 packet_length = read_le32(payload, byte_count, 4);
        if (packet_length == 0x18) {
            handle_response_status(state, read_le32(payload, byte_count, 0x0d));
        } else if (packet_length == 0x3c) {
            SetWindowTextA(state.status_edit.window,
                reinterpret_cast<const char*>(payload + 0x0d));
        } else if (packet_length == 0x59) {
            state.last_server_payload.assign(payload + 0x0d, payload + byte_count);
        }
        return;
    }
    if (state.async_tcp_socket == nullptr) {
        return;
    }

    ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= 0x11) {
        const u32 packet_bytes = read_le32(payload, byte_count, 8);
        if (packet_bytes < 0x11 ||
            packet_bytes > static_cast<u32>(byte_count)) {
            break;
        }
        const u32 packet_length = read_le32(payload, static_cast<i32>(packet_bytes), 4);
        if (packet_length == 0x18) {
            handle_response_status(state,
                read_le32(payload, static_cast<i32>(packet_bytes), 0x0d));
        } else if (packet_length == 0x3c) {
            SetWindowTextA(state.status_edit.window,
                reinterpret_cast<const char*>(payload + 0x0d));
        } else if (packet_length == 0x59) {
            state.last_server_payload.assign(payload + 0x0d, payload + packet_bytes);
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
            static_cast<i32>(packet_bytes));
        if (state.window == nullptr) {
            return;
        }
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

}

#endif
