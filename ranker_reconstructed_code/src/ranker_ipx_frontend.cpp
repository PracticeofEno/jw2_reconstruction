#include "ranker_ipx_frontend.h"

#ifdef _WIN32

#include "ranker_connect_frontend.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_ipx_lobby.h"
#include "ranker_online_dialogs.h"
#include "ranker_setup_data.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed = 0x10cf0000;
constexpr DWORD kNameEditStyle = WS_CHILD;
constexpr DWORD kInfoButtonStyle = 0x5800000b;
constexpr COLORREF kIpxFrontendWhite = RGB(255, 255, 255);
constexpr COLORREF kIpxFrontendSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kIpxFrontendBlack = RGB(0, 0, 0);
constexpr RECT kInfoTextRect{0x0f, 0x0c, 0x146, 0x76};
constexpr std::size_t kStartupIpxModeMissingMessageRow = 86;
constexpr std::size_t kStartupIpxInitializeFailureMessageRow = 87;
constexpr std::size_t kStartupIpxLobbyOpenFailureMessageRow = 88;
constexpr std::size_t kStartupIpxEmptyNameMessageRow = 89;

IpxFrontendState g_ipx_frontend_state;
bool g_background_shutdown_registered = false;
bool g_info_button_destructor_registered = false;
bool g_ok_button_destructor_registered = false;
bool g_cancel_button_destructor_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK ipx_frontend_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleIpxFrontendWindowMessage(g_ipx_frontend_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK ipx_frontend_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleIpxFrontendControlMessage(g_ipx_frontend_state, hwnd, message,
        wparam, lparam);
}

void shutdown_global_background() {
    ShutdownIpxFrontendBackgroundBitmap(g_ipx_frontend_state);
}

void shutdown_global_info_button() {
    DestroyIpxFrontendInfoButton(g_ipx_frontend_state);
}

void shutdown_global_ok_button() {
    DestroyIpxFrontendOkButton(g_ipx_frontend_state);
}

void shutdown_global_cancel_button() {
    DestroyIpxFrontendCancelButton(g_ipx_frontend_state);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

IpxFrontendLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return IpxFrontendLayoutRect{};
}

std::string read_info_text() {
    std::vector<u8> bytes;
    if (!LoadTrcRecordAlloc("Jw2_19.trc", kIpxFrontendInfoTextTrcRecord, bytes, 1)) {
        return "Local Area Network (IPX)";
    }
    if (bytes.empty()) {
        return {};
    }
    const auto nul = std::find(bytes.begin(), bytes.end(), 0);
    return std::string(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::size_t>(nul - bytes.begin()));
}

void copy_c_string(char* target, std::size_t target_size, const char* source) {
    if (target == nullptr || target_size == 0) {
        return;
    }
    std::memset(target, 0, target_size);
    if (source != nullptr) {
        std::strncpy(target, source, target_size - 1);
    }
}

template <std::size_t N>
void copy_to_array(std::array<char, N>& target, const char* source) {
    copy_c_string(target.data(), target.size(), source);
}

void clear_text_control(IpxFrontendTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void destroy_text_control(IpxFrontendTextControl& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    clear_text_control(control);
}

void subclass_text_control(IpxFrontendTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ipx_frontend_control_proc));
}

bool create_name_edit(IpxFrontendState& state, HWND parent, HINSTANCE instance,
    const IpxFrontendLayoutRect& rect) {
    state.name_edit.id = kIpxFrontendNameEditId;
    state.name_edit.window = CreateWindowExA(0, "edit", nullptr, kNameEditStyle,
        rect.x, rect.y, rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIpxFrontendNameEditId)),
        instance, nullptr);
    if (state.name_edit.window == nullptr) {
        clear_text_control(state.name_edit);
        return false;
    }
    subclass_text_control(state.name_edit);
    SendMessageA(state.name_edit.window, EM_LIMITTEXT, kIpxFrontendPlayerNameBytes, 0);
    return true;
}

void subclass_image_button(LegacyImageButtonControl& button) {
    if (button.window == nullptr) {
        return;
    }
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(button.window, GWLP_WNDPROC));
    SetWindowLongPtrA(button.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ipx_frontend_control_proc));
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const IpxFrontendLayoutRect& rect, u32 normal_record,
    u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    subclass_image_button(button);
    return true;
}

LegacyImageButtonControl* button_for_id(IpxFrontendState& state, int id) {
    switch (id) {
    case kIpxFrontendInfoButtonId:
        return &state.info_button;
    case kIpxFrontendOkButtonId:
        return &state.ok_button;
    case kIpxFrontendCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

WNDPROC original_proc_for_id(IpxFrontendState& state, int id) {
    if (id == kIpxFrontendNameEditId) {
        return state.name_edit.original_window_proc;
    }
    if (LegacyImageButtonControl* button = button_for_id(state, id)) {
        return button->original_window_proc;
    }
    return nullptr;
}

void show_message(IpxFrontendState& state, HWND owner, const char* text,
    COLORREF color = kIpxFrontendSoftWhite) {
    state.last_message = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(owner, state.last_message.c_str(), color);
        return;
    }
    if (owner != nullptr) {
        ShowOnlineModalPrompt1(online_modal_prompt_state(), owner,
            state.last_message.c_str(), color);
    }
}

void play_click_sound(IpxFrontendState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

bool is_ipx_mode_enabled(IpxFrontendState& state) {
    if (state.callbacks.is_ipx_mode_enabled != nullptr) {
        return state.callbacks.is_ipx_mode_enabled(state);
    }
    return state.ipx_mode_enabled;
}

bool initialize_ipx_connection(IpxFrontendState& state) {
    if (state.async_context == nullptr) {
        return false;
    }
    if (state.callbacks.initialize_ipx_connection != nullptr) {
        return state.callbacks.initialize_ipx_connection(state, *state.async_context);
    }
    if (state.ipx_connection != nullptr) {
        return SUCCEEDED(InitializeDirectPlayConnection(
            *state.ipx_connection, state.async_context));
    }
    return state.async_context->direct_play != nullptr &&
        state.async_context->selected_connection != nullptr;
}

bool open_ipx_lobby(IpxFrontendState& state) {
    if (state.callbacks.open_ipx_lobby != nullptr) {
        return state.callbacks.open_ipx_lobby(state);
    }
    if (state.async_context == nullptr) {
        return false;
    }
    return CreateIpxLobbyWindow(ipx_lobby_state(), state.main_window, state.instance,
        state.return_context, state.async_context);
}

void open_connect_frontend(IpxFrontendState& state) {
    if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
        return;
    }
    CreateConnectFrontendWindow(connect_frontend_state(), state.main_window,
        state.instance, state.return_context);
}

void shutdown_failed_connection(IpxFrontendState& state) {
    if (state.async_context != nullptr &&
        state.callbacks.shutdown_ipx_connection != nullptr) {
        state.callbacks.shutdown_ipx_connection(state, *state.async_context);
    }
}

void write_setup_data(IpxFrontendState& state) {
    if (state.callbacks.write_setup_data != nullptr) {
        state.callbacks.write_setup_data(state);
        return;
    }
    ExportSetupText(kSetupIpxPlayerNameOffset, state.saved_player_name);
    WriteDefaultSetupDataBuffer();
}

void release_window_resources(IpxFrontendState& state) {
    RestoreIpxFrontendAccelerators(state);
    ReleaseBitmapMemoryResource(state.background);
    DestroyIpxFrontendInfoButton(state);
    DestroyIpxFrontendOkButton(state);
    DestroyIpxFrontendCancelButton(state);
    destroy_text_control(state.name_edit);
    state.info_text.clear();
    state.layout.clear();
    state.window = nullptr;
    state.visible = false;
}

void draw_info_panel(IpxFrontendState& state, const DRAWITEMSTRUCT& draw) {
    StretchBitmapMemoryResourceToDc(state.info_button.normal_bitmap, draw.hDC, 0, 0);
    RECT text_rect = kInfoTextRect;
    SetTextColor(draw.hDC, kIpxFrontendWhite);
    SetBkColor(draw.hDC, kIpxFrontendBlack);
    SetBkMode(draw.hDC, TRANSPARENT);
    DrawTextA(draw.hDC, state.info_text.c_str(), -1, &text_rect,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

void set_initial_player_name(IpxFrontendState& state) {
    char initial[kIpxFrontendPlayerNameBytes]{};
    if (state.saved_player_name[0] != '\0') {
        copy_c_string(initial, sizeof(initial), state.saved_player_name.data());
    }
    else {
        char user_name[256]{};
        DWORD size = sizeof(user_name);
        if (GetUserNameA(user_name, &size)) {
            copy_c_string(initial, sizeof(initial), user_name);
        }
    }

    SetWindowTextA(state.name_edit.window, initial);
    const LRESULT length = SendMessageA(state.name_edit.window, WM_GETTEXTLENGTH, 0, 0);
    SendMessageA(state.name_edit.window, EM_SETSEL, 0, length);
}

} // namespace

IpxFrontendState& ipx_frontend_state() {
    return g_ipx_frontend_state;
}

void InitializeIpxFrontendBackgroundResourceAndShutdown(IpxFrontendState& state) {
    InitializeIpxFrontendBackgroundBitmap(state);
    RegisterIpxFrontendBackgroundShutdown(state);
}

void InitializeIpxFrontendBackgroundBitmap(IpxFrontendState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterIpxFrontendBackgroundShutdown(IpxFrontendState&) {
    register_atexit_once(g_background_shutdown_registered,
        shutdown_global_background);
}

void ShutdownIpxFrontendBackgroundBitmap(IpxFrontendState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeIpxFrontendInfoButtonSupport(IpxFrontendState& state) {
    InitializeIpxFrontendInfoButton(state);
    RegisterIpxFrontendInfoButtonDestructor(state);
}

void InitializeIpxFrontendInfoButton(IpxFrontendState& state) {
    InitializeLegacyImageButtonControl(state.info_button);
}

void RegisterIpxFrontendInfoButtonDestructor(IpxFrontendState&) {
    register_atexit_once(g_info_button_destructor_registered,
        shutdown_global_info_button);
}

void DestroyIpxFrontendInfoButton(IpxFrontendState& state) {
    DestroyLegacyImageButtonControl(state.info_button);
}

void InitializeIpxFrontendOkButtonSupport(IpxFrontendState& state) {
    InitializeIpxFrontendOkButton(state);
    RegisterIpxFrontendOkButtonDestructor(state);
}

void InitializeIpxFrontendOkButton(IpxFrontendState& state) {
    InitializeLegacyImageButtonControl(state.ok_button);
}

void RegisterIpxFrontendOkButtonDestructor(IpxFrontendState&) {
    register_atexit_once(g_ok_button_destructor_registered,
        shutdown_global_ok_button);
}

void DestroyIpxFrontendOkButton(IpxFrontendState& state) {
    DestroyLegacyImageButtonControl(state.ok_button);
}

void InitializeIpxFrontendCancelButtonSupport(IpxFrontendState& state) {
    InitializeIpxFrontendCancelButton(state);
    RegisterIpxFrontendCancelButtonDestructor(state);
}

void InitializeIpxFrontendCancelButton(IpxFrontendState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterIpxFrontendCancelButtonDestructor(IpxFrontendState&) {
    register_atexit_once(g_cancel_button_destructor_registered,
        shutdown_global_cancel_button);
}

void DestroyIpxFrontendCancelButton(IpxFrontendState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InitializeIpxFrontendControls(IpxFrontendState& state) {
    clear_text_control(state.name_edit);
    InitializeIpxFrontendInfoButtonSupport(state);
    InitializeIpxFrontendOkButtonSupport(state);
    InitializeIpxFrontendCancelButtonSupport(state);
}

void ReleaseIpxFrontendControls(IpxFrontendState& state) {
    DestroyIpxFrontendInfoButton(state);
    DestroyIpxFrontendOkButton(state);
    DestroyIpxFrontendCancelButton(state);
    destroy_text_control(state.name_edit);
}

void InstallIpxFrontendAccelerators(IpxFrontendState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kIpxFrontendAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreIpxFrontendAccelerators(IpxFrontendState& state) {
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

bool OpenIpxFrontendWindow(IpxFrontendState& state) {
    return CreateIpxFrontendWindow(state, state.main_window, state.instance,
        state.return_context, state.async_context);
}

bool CreateIpxFrontendWindow(IpxFrontendState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context, AsyncComContext* async_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.async_context = async_context != nullptr ?
        async_context : async_com_state().active_context;
    state.player_name.fill(0);
    ImportSetupText(state.saved_player_name, kSetupIpxPlayerNameOffset);
    state.last_message.clear();
    state.visible = false;

    InitializeIpxFrontendControls(state);
    state.layout.clear();
    state.info_text = read_info_text();

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kIpxFrontendLayoutTrcRecord)) {
        release_window_resources(state);
        return false;
    }

    const IpxFrontendLayoutRect window_rect = layout_at(layout.table, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(parent,
              window_rect.width, window_rect.height)
        : RankerCenteredFrontendWindowOrigin(
              window_rect.width, window_rect.height);
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "IPX", "IPX", style,
        origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        release_window_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ipx_frontend_window_proc));

    if (!create_name_edit(state, state.window, instance,
            layout_at(layout.table, 1)) ||
        !create_image_button(state.info_button, state.window, "",
            kIpxFrontendInfoButtonId, layout_at(layout.table, 2),
            kIpxFrontendInfoBitmapRecord, 0) ||
        !create_image_button(state.ok_button, state.window, "",
            kIpxFrontendOkButtonId, layout_at(layout.table, 3),
            kIpxFrontendOkNormalBitmapRecord, kIpxFrontendOkPressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kIpxFrontendCancelButtonId, layout_at(layout.table, 4),
            kIpxFrontendCancelNormalBitmapRecord,
            kIpxFrontendCancelPressedBitmapRecord)) {
        DestroyWindow(state.window);
        return false;
    }

    SetWindowLongPtrA(state.info_button.window, GWL_STYLE, kInfoButtonStyle);
    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.info_button.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.name_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    InstallIpxFrontendAccelerators(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kIpxFrontendBackgroundBitmapRecord);
    set_initial_player_name(state);
    ShowWindow(state.name_edit.window, SW_SHOW);
    SetFocus(state.name_edit.window);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW |
        RDW_ALLCHILDREN);
    state.visible = true;
    return true;
}

bool SubmitIpxFrontendName(IpxFrontendState& state) {
    if (!is_ipx_mode_enabled(state)) {
        show_message(state, state.window,
            startup_message_row(kStartupIpxModeMissingMessageRow,
                "IPX networking is not available."),
            kIpxFrontendSoftWhite);
        return false;
    }

    if (!initialize_ipx_connection(state)) {
        show_message(state, state.window,
            startup_message_row(kStartupIpxInitializeFailureMessageRow,
                "Unable to initialize IPX connection."),
            kIpxFrontendSoftWhite);
        return false;
    }

    char name[kIpxFrontendPlayerNameBytes]{};
    GetWindowTextA(state.name_edit.window, name, static_cast<int>(sizeof(name)));
    if (name[0] == '\0') {
        show_message(state, state.window,
            startup_message_row(kStartupIpxEmptyNameMessageRow,
                "Enter a player name."),
            kIpxFrontendSoftWhite);
        SetFocus(state.name_edit.window);
        return false;
    }

    copy_to_array(state.player_name, name);
    copy_to_array(state.saved_player_name, name);
    write_setup_data(state);

    HWND old_window = state.window;
    if (old_window != nullptr) {
        DestroyWindow(old_window);
    }

    if (open_ipx_lobby(state)) {
        return true;
    }

    shutdown_failed_connection(state);
    OpenIpxFrontendWindow(state);
    show_message(state, state.window,
        startup_message_row(kStartupIpxLobbyOpenFailureMessageRow,
            "Unable to open IPX game lobby."),
        kIpxFrontendSoftWhite);
    return false;
}

LRESULT HandleIpxFrontendWindowMessage(IpxFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        release_window_resources(state);
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
        if (draw->CtlID == kIpxFrontendInfoButtonId) {
            draw_info_panel(state, *draw);
            break;
        }
        if (LegacyImageButtonControl* button =
                button_for_id(state, static_cast<int>(draw->CtlID))) {
            DrawLegacyImageButtonItem(*button, *draw);
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        switch (id) {
        case kIpxFrontendInfoButtonId:
        case kIpxFrontendFocusNameCommandId:
            SetFocus(state.name_edit.window);
            break;
        case kIpxFrontendOkButtonId:
            play_click_sound(state);
            if (SubmitIpxFrontendName(state)) {
                return 0;
            }
            break;
        case kIpxFrontendCancelButtonId:
            play_click_sound(state);
            if (state.window != nullptr) {
                DestroyWindow(state.window);
            }
            open_connect_frontend(state);
            break;
        default:
            break;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kIpxFrontendWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kIpxFrontendBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleIpxFrontendControlMessage(IpxFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    switch (id) {
    case kIpxFrontendNameEditId:
    case kIpxFrontendInfoButtonId:
    case kIpxFrontendOkButtonId:
    case kIpxFrontendCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

} // namespace ranker

#endif
