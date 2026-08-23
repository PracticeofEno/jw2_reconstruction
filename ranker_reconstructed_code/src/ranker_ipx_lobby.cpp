#include "ranker_ipx_lobby.h"

#ifdef _WIN32

#include "ranker_bitmap_icon_collection.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_link_lobby.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kPasswordEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kListBoxStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr DWORD kInfoButtonStyle = WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW;
constexpr COLORREF kIpxWhite = RGB(255, 255, 255);
constexpr COLORREF kIpxSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kIpxBlack = RGB(0, 0, 0);
constexpr COLORREF kIpxStartFailureRed = RGB(255, 20, 20);
constexpr COLORREF kIpxNetworkFailureRed = RGB(250, 10, 10);
constexpr COLORREF kIpxWaitYellow = RGB(250, 250, 10);
constexpr std::size_t kIpxGameTitleOffset = 0x08;
constexpr std::size_t kIpxGameTitleBytes = 0x20;
constexpr std::size_t kIpxGameTypeOffset = 0x2a8;
constexpr std::size_t kIpxScreenSizeOffset = 0x2ac;
constexpr std::size_t kIpxHostNameOffset = 0x2b0;
constexpr std::size_t kIpxHostNameBytes = 0x20;
constexpr std::size_t kIpxTimeTextOffset = 0x2d0;
constexpr std::size_t kIpxTimeTextBytes = 0x0c;

const char* const kIpxGameTypeFallbacks[] = {
    "Top Vs Bottom",
    "Melee",
    "Rank",
    "Avatar Melee",
    "Avatar Rank",
    "Use Map Setting",
    "Melee Observer",
    "Avatar Observer",
    "Relay",
};

const char* const kIpxScreenSizeFallbacks[] = {
    "Free Size",
    "640x480",
    "800x600",
    "1024x768",
};

IpxLobbyState g_ipx_lobby_state;
bool g_ipx_background_shutdown_registered = false;
bool g_ipx_info_button_shutdown_registered = false;
bool g_ipx_create_button_shutdown_registered = false;
bool g_ipx_join_button_shutdown_registered = false;
bool g_ipx_cancel_button_shutdown_registered = false;
bool g_ipx_scroll_control_shutdown_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK ipx_lobby_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleIpxLobbyWindowMessage(g_ipx_lobby_state, hwnd, message, wparam,
        lparam);
}

LRESULT CALLBACK ipx_lobby_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleIpxLobbyControlMessage(g_ipx_lobby_state, hwnd, message, wparam,
        lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_global_background() {
    ShutdownIpxLobbyBackgroundBitmap(g_ipx_lobby_state);
}

void shutdown_global_info_button() {
    ShutdownIpxLobbyInfoButton(g_ipx_lobby_state);
}

void shutdown_global_create_button() {
    ShutdownIpxLobbyCreateButton(g_ipx_lobby_state);
}

void shutdown_global_join_button() {
    ShutdownIpxLobbyJoinButton(g_ipx_lobby_state);
}

void shutdown_global_cancel_button() {
    ShutdownIpxLobbyCancelButton(g_ipx_lobby_state);
}

void shutdown_global_scroll_control() {
    ShutdownIpxLobbyScrollControl(g_ipx_lobby_state);
}

IpxLobbyLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return IpxLobbyLayoutRect{};
}

void clear_control(IpxLobbyControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_control(IpxLobbyControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ipx_lobby_control_proc));
}

bool create_control(IpxLobbyControl& control, HWND parent, HINSTANCE instance,
    const char* class_name, const char* text, DWORD style, int id,
    const IpxLobbyLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, text, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_control(control);
    return true;
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent, const char* text,
    int id, const IpxLobbyLayoutRect& rect, u32 normal_record, u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    if (normal_record != 0 || pressed_record != 0) {
        LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    }
    HWND window = GetLegacyImageButtonWindow(button);
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(window, GWLP_WNDPROC));
    SetWindowLongPtrA(window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ipx_lobby_control_proc));
    return true;
}

WNDPROC original_proc_for_id(IpxLobbyState& state, int id) {
    switch (id) {
    case kIpxLobbySessionNameEditId:
        return state.session_name_edit.original_window_proc;
    case kIpxLobbyPasswordEditId:
        return state.password_edit.original_window_proc;
    case kIpxLobbySessionListId:
        return state.session_list.original_window_proc;
    case kIpxLobbyScrollControlId:
        return state.scroll_control.original_window_proc;
    case kIpxLobbyInfoButtonId:
        return state.info_button.original_window_proc;
    case kIpxLobbyCreateButtonId:
        return state.create_button.original_window_proc;
    case kIpxLobbyJoinButtonId:
        return state.join_button.original_window_proc;
    case kIpxLobbyCancelButtonId:
        return state.cancel_button.original_window_proc;
    default:
        return nullptr;
    }
}

LegacyImageButtonControl* image_button_for_id(IpxLobbyState& state, int id) {
    switch (id) {
    case kIpxLobbyInfoButtonId:
        return &state.info_button;
    case kIpxLobbyCreateButtonId:
        return &state.create_button;
    case kIpxLobbyJoinButtonId:
        return &state.join_button;
    case kIpxLobbyCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

void read_control_text(HWND window, char* target, int target_size) {
    if (target == nullptr || target_size <= 0) {
        return;
    }
    target[0] = '\0';
    if (window != nullptr) {
        GetWindowTextA(window, target, target_size);
    }
}

u32 read_packet_u32(const void* packet, std::size_t packet_size, std::size_t offset) {
    if (packet == nullptr || offset > packet_size || packet_size - offset < sizeof(u32)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(packet) + offset, sizeof(value));
    return value;
}

u32 read_game_data_u32(const IpxLobbyState& state, std::size_t offset) {
    if (offset > state.selected_game_data.size() ||
        state.selected_game_data.size() - offset < sizeof(u32)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, state.selected_game_data.data() + offset, sizeof(value));
    return value;
}

std::string read_game_data_text(const IpxLobbyState& state, std::size_t offset,
    std::size_t byte_count) {
    if (offset >= state.selected_game_data.size()) {
        return {};
    }
    byte_count = std::min(byte_count, state.selected_game_data.size() - offset);
    const char* first = reinterpret_cast<const char*>(
        state.selected_game_data.data() + offset);
    std::size_t length = 0;
    while (length < byte_count && first[length] != '\0') {
        ++length;
    }
    return std::string(first, length);
}

const char* ipx_game_type_label(u32 game_type) {
    if (game_type < std::size(kIpxGameTypeFallbacks)) {
        return kIpxGameTypeFallbacks[game_type];
    }
    return "Unknown";
}

const char* ipx_screen_size_label(u32 screen_size) {
    if (screen_size < std::size(kIpxScreenSizeFallbacks)) {
        return kIpxScreenSizeFallbacks[screen_size];
    }
    return "Unknown";
}

void release_browser_connection(IpxLobbyState& state) {
    ReleaseDirectPlay4AComObject(state.browser_context.direct_play);
    state.browser_context.selected_connection = nullptr;
    state.browser_context.session_descriptor_data.clear();
    state.browser_context.session_descriptor = nullptr;
    state.browser_context.local_player = 0;
    state.browser_context.is_host = false;
}

bool initialize_browser_connection(IpxLobbyState& state) {
    release_browser_connection(state);
    if (state.callbacks.initialize_browser_connection != nullptr) {
        return state.callbacks.initialize_browser_connection(
            state, state.browser_context);
    }
    const DirectPlayConnectionRecord* connection = state.async_context != nullptr ?
        state.async_context->selected_connection : nullptr;
    return connection != nullptr && SUCCEEDED(
        InitializeDirectPlayConnection(*connection, &state.browser_context));
}

void show_startup_status(IpxLobbyState& state, std::size_t index,
    const char* fallback, COLORREF color) {
    ShowIpxLobbyStatusMessage(state, startup_message_row(index, fallback), color);
}

std::string format_startup_status(std::size_t index, const char* fallback,
    const char* value) {
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), startup_message_row(index, fallback),
        value != nullptr ? value : "");
    return buffer;
}

std::string format_startup_status(std::size_t index, const char* fallback,
    int value) {
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), startup_message_row(index, fallback),
        value);
    return buffer;
}

std::string format_ipx_panel_version_mismatch(u32 remote_version) {
    char buffer[160]{};
    std::snprintf(buffer, sizeof(buffer),
        startup_message_row(41, "Game version (%d.%d) differs."),
        remote_version >> 16, remote_version & 0xffffu);
    return buffer;
}

std::string format_ipx_join_version_mismatch(u32 remote_version) {
    const u32 local_version = LoadTrcRecord9Value();
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer),
        startup_message_row(49,
            "Connection failed - game version mismatch. (my version = %d-%d-%d, host version = %d-%d-%d)"),
        local_version & 0xffffu, (local_version >> 16) & 0xffu,
        (local_version >> 24) & 0xffu, remote_version & 0xffffu,
        (remote_version >> 16) & 0xffu, (remote_version >> 24) & 0xffu);
    return buffer;
}

std::string session_display_name(const DirectPlaySessionRecord& session,
    std::size_t index) {
    if (!session.session_name.empty()) {
        return session.session_name;
    }
    char fallback[64]{};
    std::snprintf(fallback, sizeof(fallback), "IPX Game %u",
        static_cast<unsigned>(index + 1));
    return fallback;
}

bool session_password_required(const DPSESSIONDESC2& descriptor) {
    return (descriptor.dwFlags & DPSESSION_PASSWORDREQUIRED) != 0;
}

IpxLobbySessionListItem item_from_session(const DirectPlaySessionRecord& session,
    std::size_t index) {
    IpxLobbySessionListItem item;
    item.name = session_display_name(session, index);
    item.instance = session.descriptor.guidInstance;
    item.application = session.descriptor.guidApplication;
    item.flags = session.descriptor.dwFlags;
    item.magic = session.descriptor.dwUser2;
    item.version = session.descriptor.dwUser3;
    item.host_player_id = session.descriptor.dwUser4;
    item.password_required = session_password_required(session.descriptor);
    item.joinable = true;
    return item;
}

bool load_selected_game_data(IpxLobbyState& state,
    const IpxLobbySessionListItem& item) {
    state.selected_game_data.fill(0);
    state.selected_game_data_valid = false;
    if (state.browser_context.direct_play == nullptr || item.host_player_id == 0) {
        return false;
    }

    DPSESSIONDESC2 descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.dwFlags = item.flags;
    descriptor.guidInstance = item.instance;
    descriptor.guidApplication = item.application;
    descriptor.lpszSessionNameA = const_cast<char*>(item.name.c_str());
    descriptor.dwUser2 = item.magic;
    descriptor.dwUser3 = item.version;
    descriptor.dwUser4 = item.host_player_id;

    HRESULT result = state.browser_context.direct_play->Open(&descriptor, DPOPEN_JOIN);
    if (FAILED(result)) {
        return false;
    }

    DWORD byte_count = 0;
    result = state.browser_context.direct_play->GetPlayerData(
        item.host_player_id, nullptr, &byte_count, 0);
    if (byte_count == state.selected_game_data.size() &&
        (result == DPERR_BUFFERTOOSMALL || SUCCEEDED(result))) {
        result = state.browser_context.direct_play->GetPlayerData(
            item.host_player_id, state.selected_game_data.data(), &byte_count, 0);
    }
    else {
        result = DPERR_INVALIDPARAMS;
    }
    state.browser_context.direct_play->Close();
    state.selected_game_data_valid = SUCCEEDED(result) &&
        byte_count == state.selected_game_data.size();
    return state.selected_game_data_valid;
}

void sync_session_listbox(IpxLobbyState& state) {
    if (state.session_list.window == nullptr) {
        return;
    }
    int top_index = static_cast<int>(
        SendMessageA(state.session_list.window, LB_GETTOPINDEX, 0, 0));
    if (top_index < 0) {
        top_index = 0;
    }
    SendMessageA(state.session_list.window, LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < state.sessions.size(); ++i) {
        const IpxLobbySessionListItem& item = state.sessions[i];
        LRESULT index = SendMessageA(state.session_list.window, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(item.name.c_str()));
        if (index != LB_ERR) {
            SendMessageA(state.session_list.window, LB_SETITEMDATA,
                static_cast<WPARAM>(index), static_cast<LPARAM>(i));
        }
    }
    const int count = static_cast<int>(
        SendMessageA(state.session_list.window, LB_GETCOUNT, 0, 0));
    const int visible_rows = std::max(1, state.visible_count);
    const int max_top = std::max(0, count - visible_rows);
    const int clamped_top = std::min(top_index, max_top);
    SetLegacyCustomScrollControlRange(state.scroll_control, 0, max_top, false);
    SetLegacyCustomScrollControlValue(state.scroll_control, clamped_top, false);
    SetLegacyCustomScrollControlVisible(state.scroll_control, max_top > 0);
    SendMessageA(state.session_list.window, LB_SETTOPINDEX,
        static_cast<WPARAM>(clamped_top), 0);
}

void draw_session_list_item(IpxLobbyState& state, const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return;
    }

    char text[256]{};
    SendMessageA(draw.hwndItem, LB_GETTEXT, draw.itemID, reinterpret_cast<LPARAM>(text));
    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(draw.hDC, kIpxWhite);
    SetBkMode(draw.hDC, (draw.itemState & ODS_SELECTED) != 0 ? OPAQUE : TRANSPARENT);
    if ((draw.itemState & ODS_SELECTED) != 0) {
        SetBkColor(draw.hDC, RGB(0, 0, 255));
    }
    BitmapIconResourceCollection& icons = GlobalBitmapIconResourceCollection();
    const BitmapMemoryResource& icon = GetBitmapIconSlotOrDefault(
        icons, kBitmapIconDefaultSlot);
    if (icon.loaded) {
        StretchBitmapMemoryResourceToDc(icon, draw.hDC,
            rect.left + 4, rect.top + 2);
        rect.left += 0x2d;
    }
    else {
        rect.left += 8;
    }
    rect.right -= 4;
    DrawTextA(draw.hDC, text, -1, &rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    (void)state;
}

const IpxLobbySessionListItem* selected_session_item(const IpxLobbyState& state) {
    if (state.selected_session < 0) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(state.selected_session);
    if (index >= state.sessions.size()) {
        return nullptr;
    }
    return &state.sessions[index];
}

void draw_info_panel(IpxLobbyState& state, const DRAWITEMSTRUCT& draw) {
    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    rect.left += 6;
    rect.top += 6;
    rect.right -= 6;
    rect.bottom -= 6;

    SetTextColor(draw.hDC, kIpxWhite);
    SetBkColor(draw.hDC, kIpxBlack);
    SetBkMode(draw.hDC, TRANSPARENT);

    std::string text;
    switch (state.selected_status) {
    case IpxLobbySessionStatus::Joinable: {
        if (!state.selected_game_data_valid) {
            text = startup_message_row(39,
                "This game cannot provide game information.");
            break;
        }
        const std::string title = read_game_data_text(
            state, kIpxGameTitleOffset, kIpxGameTitleBytes);
        const std::string host = read_game_data_text(
            state, kIpxHostNameOffset, kIpxHostNameBytes);
        const std::string time = read_game_data_text(
            state, kIpxTimeTextOffset, kIpxTimeTextBytes);
        char buffer[768]{};
        std::snprintf(buffer, sizeof(buffer), "%s%s\n%s%s\n%s%s\n%s%s\n%s%s",
            startup_message_row(140, "Title: "), title.c_str(),
            startup_message_row(134, "Creator: "), host.c_str(),
            startup_message_row(141, "Game type: "),
            ipx_game_type_label(read_game_data_u32(state, kIpxGameTypeOffset)),
            startup_message_row(135, "Screen size: "),
            ipx_screen_size_label(read_game_data_u32(state, kIpxScreenSizeOffset)),
            startup_message_row(136, "Time: "), time.c_str());
        text = buffer;
        break;
    }
    case IpxLobbySessionStatus::Blocked:
        text = startup_message_row(39,
            "This password-protected game cannot provide game information.");
        break;
    case IpxLobbySessionStatus::BadMagic:
        text = startup_message_row(40,
            "This is a different game. You cannot join.");
        break;
    case IpxLobbySessionStatus::VersionMismatch:
        text = format_ipx_panel_version_mismatch(state.remote_version);
        break;
    case IpxLobbySessionStatus::None:
    default:
        text = startup_message_row(100, "No game is selected.");
        break;
    }

    DrawTextA(draw.hDC, text.c_str(), -1, &rect, DT_LEFT | DT_WORDBREAK);
}

void close_ipx_lobby(IpxLobbyState& state, bool return_to_connect) {
    state.visible = false;
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
    if (return_to_connect && state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    }
}

void release_window_resources(IpxLobbyState& state) {
    if (state.refresh_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.refresh_timer);
        state.refresh_timer = 0;
    }

    SetDirectPlayMode7DispatchEnabled(false);
    ConfigureDirectPlayMode7WindowDispatch(nullptr);
    release_browser_connection(state);
    RestoreIpxLobbyAccelerators(state);
    ReleaseBitmapMemoryResource(state.background);
    ShutdownIpxLobbyInfoButton(state);
    ShutdownIpxLobbyCreateButton(state);
    ShutdownIpxLobbyJoinButton(state);
    ShutdownIpxLobbyCancelButton(state);
    ShutdownIpxLobbyScrollControl(state);
    clear_control(state.session_name_edit);
    clear_control(state.password_edit);
    clear_control(state.session_list);
    state.window = nullptr;
    state.visible = false;
}

void handle_list_selection(IpxLobbyState& state) {
    if (state.session_list.window == nullptr) {
        return;
    }
    LRESULT selected = SendMessageA(state.session_list.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        state.selected_session = -1;
        state.selected_status = IpxLobbySessionStatus::None;
        return;
    }

    char text[0x80]{};
    SendMessageA(state.session_list.window, LB_GETTEXT, static_cast<WPARAM>(selected),
        reinterpret_cast<LPARAM>(text));
    SetWindowTextA(state.session_name_edit.window, text);
    state.selected_status = ValidateIpxLobbySelectedSession(
        state, static_cast<int>(selected));
    SetFocus(state.password_edit.window);
}

void handle_create_command(IpxLobbyState& state) {
    close_ipx_lobby(state, false);
    if (state.callbacks.open_create_game != nullptr) {
        state.callbacks.open_create_game(state);
    }
}

void handle_cancel_command(IpxLobbyState& state) {
    if (state.session_list.window != nullptr) {
        SendMessageA(state.session_list.window, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        SendMessageA(state.session_list.window, LB_RESETCONTENT, 0, 0);
    }
    SetLegacyCustomScrollControlRange(state.scroll_control, 0, 0, false);
    SetLegacyCustomScrollControlValue(state.scroll_control, 0, false);
    SetLegacyCustomScrollControlVisible(state.scroll_control, false);
    state.start_game_requested = false;
    close_ipx_lobby(state, true);
}

void dispatch_join_result(IpxLobbyState& state, HRESULT result) {
    if (FAILED(result)) {
        state.join_phase = IpxLobbyJoinPhase::Idle;
        const bool password_state =
            state.selected_status == IpxLobbySessionStatus::Blocked;
        show_startup_status(state, password_state ? 51 : 42,
            password_state ? "Connection failed - wrong password." :
                "Connection failed - general error.",
            kIpxStartFailureRed);
        return;
    }

    state.join_phase = IpxLobbyJoinPhase::WaitingForStart;
    ConfigureDirectPlayMode7WindowDispatch(state.window);
    SetDirectPlayMessageDispatchMode(7);
    SetDirectPlayMode7DispatchEnabled(true);
    PostMessageA(state.window, kIpxLobbyJoinStatusMessage, 0, 0);
}

void recover_ipx_lobby_connection(IpxLobbyState& state) {
    if (state.join_phase == IpxLobbyJoinPhase::JoinRequested) {
        state.join_phase = IpxLobbyJoinPhase::Idle;
        return;
    }
    if (state.join_phase != IpxLobbyJoinPhase::WaitingForStart &&
        state.join_phase != IpxLobbyJoinPhase::Busy) {
        return;
    }

    if (state.async_context != nullptr && state.async_context->direct_play != nullptr) {
        if (state.async_context->local_player != 0) {
            state.async_context->direct_play->DestroyPlayer(
                state.async_context->local_player);
        }
        state.async_context->local_player = 0;
        state.async_context->direct_play->Close();
        state.async_context->session_descriptor_data.clear();
        state.async_context->session_descriptor = nullptr;
        state.async_context->is_host = false;
    }

    state.join_phase = IpxLobbyJoinPhase::Idle;
    state.selected_session = -1;
    state.selected_status = IpxLobbySessionStatus::None;
    state.selected_game_data.fill(0);
    state.selected_game_data_valid = false;
    if (state.callbacks.set_busy != nullptr) {
        state.callbacks.set_busy(FALSE);
    }
    if (!initialize_browser_connection(state)) {
        show_startup_status(state, 42,
            "Connection failed - general error.", kIpxStartFailureRed);
        return;
    }
    RefreshIpxLobbySessionList(state);
}

} // namespace

IpxLobbyState& ipx_lobby_state() {
    return g_ipx_lobby_state;
}

void InitializeIpxLobbyBackgroundResourceAndShutdown(IpxLobbyState& state) {
    InitializeIpxLobbyBackgroundBitmap(state);
    RegisterIpxLobbyBackgroundShutdown(state);
}

void InitializeIpxLobbyBackgroundBitmap(IpxLobbyState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterIpxLobbyBackgroundShutdown(IpxLobbyState&) {
    register_atexit_once(g_ipx_background_shutdown_registered,
        shutdown_global_background);
}

void ShutdownIpxLobbyBackgroundBitmap(IpxLobbyState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeIpxLobbyInfoButtonSupport(IpxLobbyState& state) {
    InitializeIpxLobbyInfoButton(state);
    RegisterIpxLobbyInfoButtonShutdown(state);
}

void InitializeIpxLobbyInfoButton(IpxLobbyState& state) {
    InitializeLegacyImageButtonControl(state.info_button);
}

void RegisterIpxLobbyInfoButtonShutdown(IpxLobbyState&) {
    register_atexit_once(g_ipx_info_button_shutdown_registered,
        shutdown_global_info_button);
}

void ShutdownIpxLobbyInfoButton(IpxLobbyState& state) {
    DestroyLegacyImageButtonControl(state.info_button);
}

void InitializeIpxLobbyCreateButtonSupport(IpxLobbyState& state) {
    InitializeIpxLobbyCreateButton(state);
    RegisterIpxLobbyCreateButtonShutdown(state);
}

void InitializeIpxLobbyCreateButton(IpxLobbyState& state) {
    InitializeLegacyImageButtonControl(state.create_button);
}

void RegisterIpxLobbyCreateButtonShutdown(IpxLobbyState&) {
    register_atexit_once(g_ipx_create_button_shutdown_registered,
        shutdown_global_create_button);
}

void ShutdownIpxLobbyCreateButton(IpxLobbyState& state) {
    DestroyLegacyImageButtonControl(state.create_button);
}

void InitializeIpxLobbyJoinButtonSupport(IpxLobbyState& state) {
    InitializeIpxLobbyJoinButton(state);
    RegisterIpxLobbyJoinButtonShutdown(state);
}

void InitializeIpxLobbyJoinButton(IpxLobbyState& state) {
    InitializeLegacyImageButtonControl(state.join_button);
}

void RegisterIpxLobbyJoinButtonShutdown(IpxLobbyState&) {
    register_atexit_once(g_ipx_join_button_shutdown_registered,
        shutdown_global_join_button);
}

void ShutdownIpxLobbyJoinButton(IpxLobbyState& state) {
    DestroyLegacyImageButtonControl(state.join_button);
}

void InitializeIpxLobbyCancelButtonSupport(IpxLobbyState& state) {
    InitializeIpxLobbyCancelButton(state);
    RegisterIpxLobbyCancelButtonShutdown(state);
}

void InitializeIpxLobbyCancelButton(IpxLobbyState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterIpxLobbyCancelButtonShutdown(IpxLobbyState&) {
    register_atexit_once(g_ipx_cancel_button_shutdown_registered,
        shutdown_global_cancel_button);
}

void ShutdownIpxLobbyCancelButton(IpxLobbyState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InitializeIpxLobbyScrollControlSupport(IpxLobbyState& state) {
    InitializeIpxLobbyScrollControl(state);
    RegisterIpxLobbyScrollControlShutdown(state);
}

void InitializeIpxLobbyScrollControl(IpxLobbyState& state) {
    InitializeLegacyCustomScrollControl(state.scroll_control);
}

void RegisterIpxLobbyScrollControlShutdown(IpxLobbyState&) {
    register_atexit_once(g_ipx_scroll_control_shutdown_registered,
        shutdown_global_scroll_control);
}

void ShutdownIpxLobbyScrollControl(IpxLobbyState& state) {
    DestroyLegacyCustomScrollControl(state.scroll_control);
}

void InstallIpxLobbyAccelerators(IpxLobbyState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kIpxLobbyAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreIpxLobbyAccelerators(IpxLobbyState& state) {
    if (RankerMainWindowState().active_accelerator_window == state.window &&
        state.active_accelerators != nullptr) {
        SetActiveAcceleratorState(nullptr, state.active_accelerators);
        SetActiveAcceleratorState(state.saved_accelerator_window,
            state.saved_accelerators);
    }
    if (state.active_accelerators != nullptr) {
        DestroyAcceleratorTable(state.active_accelerators);
    }
    state.active_accelerators = nullptr;
}

void ShowIpxLobbyStatusMessage(IpxLobbyState& state, const char* text, COLORREF color) {
    state.last_message = text == nullptr ? "" : text;
    state.info_text = state.last_message;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
    }
    if (state.info_button.window != nullptr) {
        RedrawWindow(GetLegacyImageButtonWindow(state.info_button), nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

void HandleIpxLobbyJoinCommand(IpxLobbyState& state) {
    if (state.selected_status == IpxLobbySessionStatus::Joinable) {
        SubmitIpxLobbySelectedSession(state);
        return;
    }
    if (state.selected_status == IpxLobbySessionStatus::Blocked &&
        state.session_list.window != nullptr &&
        SendMessageA(state.session_list.window, LB_GETCURSEL, 0, 0) != LB_ERR) {
        SubmitIpxLobbySelectedSession(state);
        return;
    }

    show_startup_status(state, 31, "Select a game to join.", kIpxSoftWhite);
}

bool SubmitIpxLobbySelectedSession(IpxLobbyState& state) {
    if (state.session_list.window == nullptr) {
        return false;
    }

    LRESULT selected = SendMessageA(state.session_list.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        show_startup_status(state, 31, "Select a game to join.", kIpxSoftWhite);
        return false;
    }

    const IpxLobbySessionStatus status =
        ValidateIpxLobbySelectedSession(state, static_cast<int>(selected));
    state.selected_status = status;
    if (status != IpxLobbySessionStatus::Joinable &&
        status != IpxLobbySessionStatus::Blocked) {
        show_startup_status(state, 31, "Select a game to join.", kIpxSoftWhite);
        return false;
    }

    read_control_text(state.session_name_edit.window, state.session_name.data(),
        static_cast<int>(state.session_name.size()));
    if (state.session_name[0] == '\0') {
        show_startup_status(state, 31, "Select a game to join.", kIpxSoftWhite);
        return false;
    }
    read_control_text(state.password_edit.window, state.password.data(),
        static_cast<int>(state.password.size()));
    if (status == IpxLobbySessionStatus::Blocked && state.password[0] == '\0') {
        show_startup_status(state, 71,
            "The game you are trying to join requires a password.", kIpxSoftWhite);
        return false;
    }

    const std::size_t session_index = static_cast<std::size_t>(state.selected_session);
    if (session_index >= state.sessions.size()) {
        return false;
    }

    state.join_phase = IpxLobbyJoinPhase::JoinRequested;
    const std::string progress =
        format_startup_status(33, "Connecting to %s.",
            state.sessions[session_index].name.c_str());
    ShowIpxLobbyStatusMessage(state, progress.c_str(), kIpxSoftWhite);

    HRESULT result = S_OK;
    if (state.callbacks.join_session != nullptr) {
        result = state.callbacks.join_session(state, state.sessions[session_index]);
    }
    else if (state.async_context != nullptr) {
        const IpxLobbySessionListItem& session = state.sessions[session_index];
        const char* password = state.password[0] != '\0' ?
            state.password.data() : nullptr;
        result = JoinDirectPlaySessionWithPlayer(session.instance, session.name.c_str(),
            password, state.local_player_name.data(), state.local_player_data.data(),
            static_cast<DWORD>(state.local_player_data.size()), 0, state.async_context);
    }
    else {
        result = E_POINTER;
    }

    dispatch_join_result(state, result);
    return SUCCEEDED(result);
}

bool CreateIpxLobbyWindow(IpxLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context, AsyncComContext* async_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.async_context = async_context != nullptr ?
        async_context : async_com_state().active_context;
    state.sessions.clear();
    state.session_name.fill(0);
    state.password.fill(0);
    state.local_player_data.fill(0);
    if (kIpxLobbyPlayerNameOffset < state.local_player_data.size()) {
        const std::size_t available =
            state.local_player_data.size() - kIpxLobbyPlayerNameOffset;
        std::memcpy(state.local_player_data.data() + kIpxLobbyPlayerNameOffset,
            state.local_player_name.data(),
            std::min(available, state.local_player_name.size()));
    }
    state.selected_game_data.fill(0);
    state.selected_game_data_valid = false;
    state.selected_session = -1;
    state.selected_status = IpxLobbySessionStatus::None;
    state.join_phase = IpxLobbyJoinPhase::Idle;
    state.start_game_requested = false;
    state.remote_version = 0;
    state.expected_version = LoadTrcRecord9Value();

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kIpxLobbyLayoutTrcRecord)) {
        return false;
    }

    const IpxLobbyLayoutRect window_rect = layout_at(layout.table, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(parent,
              window_rect.width, window_rect.height)
        : RankerCenteredFrontendWindowOrigin(
              window_rect.width, window_rect.height);
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "IPX Game", "IPX Game",
        style, origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ipx_lobby_window_proc));

    const IpxLobbyLayoutRect list_rect = layout_at(layout.table, 3);
    const IpxLobbyLayoutRect scroll_rect = layout_at(layout.table, 4);
    if (!create_control(state.session_name_edit, state.window, instance, "edit",
            nullptr, kEditStyle, kIpxLobbySessionNameEditId,
            layout_at(layout.table, 1)) ||
        !create_control(state.password_edit, state.window, instance, "edit",
            nullptr, kPasswordEditStyle, kIpxLobbyPasswordEditId,
            layout_at(layout.table, 2)) ||
        !create_control(state.session_list, state.window, instance, "listbox",
            nullptr, kListBoxStyle, kIpxLobbySessionListId, list_rect) ||
        !CreateLegacyCustomScrollControlWindow(state.scroll_control, state.window,
            "IPX Game",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIpxLobbyScrollControlId)),
            false, scroll_rect.x, scroll_rect.y, scroll_rect.width,
            scroll_rect.height) ||
        !create_image_button(state.info_button, state.window, "Game infos",
            kIpxLobbyInfoButtonId, layout_at(layout.table, 5), 0, 0) ||
        !create_image_button(state.create_button, state.window, "Create &Game",
            kIpxLobbyCreateButtonId, layout_at(layout.table, 6),
            kIpxLobbyCreateNormalBitmapRecord, kIpxLobbyCreatePressedBitmapRecord) ||
        !create_image_button(state.join_button, state.window, "&Join Game",
            kIpxLobbyJoinButtonId, layout_at(layout.table, 7),
            kIpxLobbyJoinNormalBitmapRecord, kIpxLobbyJoinPressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kIpxLobbyCancelButtonId, layout_at(layout.table, 8),
            kIpxLobbyCancelNormalBitmapRecord, kIpxLobbyCancelPressedBitmapRecord)) {
        DestroyWindow(state.window);
        return false;
    }
    SetWindowLongPtrA(GetLegacyImageButtonWindow(state.info_button), GWL_STYLE,
        static_cast<LONG_PTR>(kInfoButtonStyle));

    SendMessageA(state.session_name_edit.window, EM_LIMITTEXT,
        state.session_name.size() - 1, 0);
    SendMessageA(state.password_edit.window, EM_LIMITTEXT,
        state.password.size() - 1, 0);
    SetWindowLongPtrA(state.scroll_control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ipx_lobby_control_proc));
    LoadLegacyCustomScrollControlBitmaps(state.scroll_control,
        kIpxLobbyIconRecord0, 0, kIpxLobbyIconRecord1, 0,
        kIpxLobbyIconRecord2, kIpxLobbyIconRecord3);
    const int item_height = static_cast<int>(
        SendMessageA(state.session_list.window, LB_GETITEMHEIGHT, 0, 0));
    state.visible_count = std::max(1, list_rect.height / std::max(1, item_height));
    SetLegacyCustomScrollControlPageStep(state.scroll_control, state.visible_count);
    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.session_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    InstallIpxLobbyAccelerators(state);
    if (!initialize_browser_connection(state)) {
        DestroyWindow(state.window);
        return false;
    }

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kIpxLobbyBackgroundBitmapRecord);
    ShowWindow(state.session_list.window, SW_SHOW);
    SetFocus(state.session_name_edit.window);
    RefreshIpxLobbySessionList(state);
    state.refresh_timer =
        SetTimer(state.window, kIpxLobbyRefreshTimerId, kIpxLobbyRefreshTimerMs, nullptr);
    ConfigureDirectPlayMode7WindowDispatch(state.window);
    SetDirectPlayMessageDispatchMode(7);
    SetDirectPlayMode7DispatchEnabled(true);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    state.visible = true;
    return true;
}

LRESULT HandleIpxLobbyWindowMessage(IpxLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
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
            StretchBitmapMemoryResourceToClient(state.background, dc, state.window);
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kIpxSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kIpxBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSCROLLBAR:
        SetTextColor(reinterpret_cast<HDC>(wparam), kIpxSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kIpxBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kIpxLobbySessionListId) {
            draw_session_list_item(state, *draw);
            return TRUE;
        }
        if (draw->CtlID == kIpxLobbyInfoButtonId) {
            draw_info_panel(state, *draw);
            break;
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
        const int notify = HIWORD(wparam);
        switch (id) {
        case kIpxLobbyCreateButtonId:
            HandleDefaultFrontendUiClickSound();
            handle_create_command(state);
            break;
        case kIpxLobbyCancelButtonId:
            HandleDefaultFrontendUiClickSound();
            handle_cancel_command(state);
            break;
        case kIpxLobbyFocusSessionNameCommandId:
            SetFocus(state.session_name_edit.window);
            break;
        case kIpxLobbyFocusPasswordCommandId:
            SetFocus(state.password_edit.window);
            break;
        case kIpxLobbyJoinButtonId:
            HandleDefaultFrontendUiClickSound();
            HandleIpxLobbyJoinCommand(state);
            break;
        case kIpxLobbySessionListId:
            if (notify == LBN_SELCHANGE) {
                handle_list_selection(state);
                break;
            }
            if (notify == LBN_DBLCLK) {
                PostMessageA(hwnd, WM_COMMAND,
                    static_cast<WPARAM>(kIpxLobbyJoinButtonId), 0);
                break;
            }
            break;
        default:
            break;
        }
        break;
    }
    case WM_TIMER:
        if (wparam == kIpxLobbyRefreshTimerId) {
            if (state.join_phase == IpxLobbyJoinPhase::Idle && !state.refresh_in_progress) {
                RefreshIpxLobbySessionList(state);
            }
            return 0;
        }
        break;
    case kIpxLobbyConnectionLostMessage:
    case kIpxLobbySessionLostMessage:
        recover_ipx_lobby_connection(state);
        return 0;
    case kIpxLobbyDirectPlayPayloadMessage:
        DispatchIpxLobbyDirectPlayPayload(state, wparam, lparam);
        break;
    case kIpxLobbyStartGameMessage:
        state.start_game_requested = true;
        close_ipx_lobby(state, false);
        if (state.callbacks.start_game != nullptr) {
            state.callbacks.start_game(state);
        }
        break;
    case kIpxLobbyJoinErrorMessage: {
        const std::string text = format_startup_status(35,
            "Connection failed (send error=%d)", static_cast<int>(lparam));
        ShowIpxLobbyStatusMessage(state, text.c_str(), kIpxNetworkFailureRed);
        break;
    }
    case kIpxLobbyJoinStatusMessage:
        show_startup_status(state, 36, "Getting connected player information.",
            kIpxWaitYellow);
        break;
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void ReleaseIpxLobbyButton(LegacyImageButtonControl& button, bool free_storage) {
    DestroyLegacyImageButtonControl(button);
    if (free_storage) {
        // Original scalar deleting destructor optionally freed object storage.
        // Reconstructed buttons are state-owned.
    }
}

void DeleteIpxLobbyScrollControl(IpxLobbyState& state, bool free_storage) {
    ShutdownIpxLobbyScrollControl(state);
    if (free_storage) {
        // Original custom-scroll object could free storage; reconstructed state owns it.
    }
}

LRESULT HandleIpxLobbyControlMessage(IpxLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (message == WM_PAINT && hwnd == state.scroll_control.window) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.scroll_control, paint.hdc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    if (id == kIpxLobbyScrollControlId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.scroll_control, message, wparam, lparam);
        if (changed && state.session_list.window != nullptr) {
            const int top = GetLegacyCustomScrollControlValue(state.scroll_control);
            SendMessageA(state.session_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(top), 0);
        }
    }
    switch (id) {
    case kIpxLobbySessionNameEditId:
    case kIpxLobbyPasswordEditId:
    case kIpxLobbyCreateButtonId:
    case kIpxLobbyJoinButtonId:
    case kIpxLobbySessionListId:
    case kIpxLobbyInfoButtonId:
    case kIpxLobbyScrollControlId:
    case kIpxLobbyCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

void RefreshIpxLobbySessionList(IpxLobbyState& state) {
    state.refresh_in_progress = true;
    int previously_selected = -1;
    if (state.session_list.window != nullptr) {
        LRESULT selected = SendMessageA(state.session_list.window, LB_GETCURSEL, 0, 0);
        if (selected != LB_ERR) {
            previously_selected = static_cast<int>(selected);
        }
    }

    if (state.browser_context.direct_play != nullptr) {
        RefreshDirectPlaySessionRecords(&state.browser_context);
    }

    state.sessions.clear();
    const AsyncComRuntimeState& runtime = async_com_state();
    for (std::size_t i = 0; i < runtime.sessions.size(); ++i) {
        state.sessions.push_back(item_from_session(runtime.sessions[i], i));
    }
    sync_session_listbox(state);

    if (!state.sessions.empty() && state.session_list.window != nullptr) {
        const int target = previously_selected >= 0 &&
                previously_selected < static_cast<int>(state.sessions.size()) ?
            previously_selected : 0;
        SendMessageA(state.session_list.window, LB_SETCURSEL, target, 0);
        state.selected_status = ValidateIpxLobbySelectedSession(state, target);
    }
    else {
        state.selected_session = -1;
        state.selected_status = IpxLobbySessionStatus::None;
    }

    if (state.session_list.window != nullptr) {
        RedrawWindow(state.session_list.window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }
    if (state.info_button.window != nullptr) {
        RedrawWindow(GetLegacyImageButtonWindow(state.info_button), nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }
    state.refresh_in_progress = false;
}

IpxLobbySessionStatus ValidateIpxLobbySelectedSession(IpxLobbyState& state,
    int list_index) {
    state.selected_session = -1;
    state.remote_version = 0;
    state.selected_game_data.fill(0);
    state.selected_game_data_valid = false;
    if (list_index < 0 || static_cast<std::size_t>(list_index) >= state.sessions.size()) {
        return IpxLobbySessionStatus::None;
    }

    state.selected_session = list_index;
    const IpxLobbySessionListItem& item =
        state.sessions[static_cast<std::size_t>(list_index)];
    if (item.magic != kIpxLobbySessionMagic) {
        return IpxLobbySessionStatus::BadMagic;
    }
    state.remote_version = item.version;
    if (item.version != state.expected_version) {
        return IpxLobbySessionStatus::VersionMismatch;
    }
    if (item.password_required || !item.joinable) {
        return IpxLobbySessionStatus::Blocked;
    }
    if (!load_selected_game_data(state, item)) {
        return IpxLobbySessionStatus::Blocked;
    }
    return IpxLobbySessionStatus::Joinable;
}

void ReportIpxLobbyDirectPlayJoinError(IpxLobbyState& state, const void* packet,
    std::size_t packet_size) {
    const u32 status = read_packet_u32(packet, packet_size, 0x10);
    switch (status) {
    case 0:
        show_startup_status(state, 42, "Connection failed - general error.",
            kIpxStartFailureRed);
        break;
    case 1:
        show_startup_status(state, 43, "Connection failed - map file send error.",
            kIpxStartFailureRed);
        break;
    case 2:
        show_startup_status(state, 44,
            "Connection failed - connected player info error.", kIpxStartFailureRed);
        break;
    case 3:
        show_startup_status(state, 45,
            "Connection failed - selected game is full.", kIpxStartFailureRed);
        break;
    case 4:
        show_startup_status(state, 46,
            "Connection failed - player info send error.", kIpxStartFailureRed);
        break;
    case 5:
        show_startup_status(state, 47,
            "Connection failed - game already started.", kIpxStartFailureRed);
        break;
    case 6:
        show_startup_status(state, 48,
            "Connection failed - different game type.", kIpxStartFailureRed);
        break;
    case 7: {
        const std::string text =
            format_ipx_join_version_mismatch(read_packet_u32(packet, packet_size, 0x0c));
        ShowIpxLobbyStatusMessage(state, text.c_str(), kIpxStartFailureRed);
        break;
    }
    case 8:
        show_startup_status(state, 50,
            "Connection failed - connection mode mismatch.", kIpxStartFailureRed);
        break;
    case 9:
        show_startup_status(state, 51,
            "Connection failed - wrong password.", kIpxStartFailureRed);
        break;
    default:
        show_startup_status(state, 42, "Connection failed - general error.",
            kIpxStartFailureRed);
        break;
    }
    state.join_phase = IpxLobbyJoinPhase::Idle;
}

void DispatchIpxLobbyDirectPlayPayload(IpxLobbyState& state, WPARAM sender,
    LPARAM payload) {
    if (payload == 0) {
        return;
    }

    const void* packet = reinterpret_cast<const void*>(payload);
    const u32 type = read_packet_u32(packet, 0x24, 4);
    LinkLobbyState& link = link_lobby_state();
    std::size_t packet_size = read_packet_u32(packet, 0x0c, 8);
    if (packet_size < 0x0c) {
        switch (type) {
        case 10:
            packet_size = 0x0c + link.session_seed_payload.size();
            break;
        case 0x15:
            packet_size = 0x0c + link.map_descriptor.size();
            break;
        case 0x20:
            packet_size = 0x1ae;
            break;
        default:
            packet_size = 0x24;
            break;
        }
    }
    if (packet_size > 0x4000) {
        packet_size = 0x24;
    }
    IpxLobbyDirectPlayMessage message;
    message.type = type;
    const auto* first = static_cast<const u8*>(packet);
    message.bytes.assign(first, first + packet_size);

    if (state.callbacks.handle_payload != nullptr) {
        state.callbacks.handle_payload(state, message);
    }

    switch (type) {
    case 10:
        ApplyLinkLobbySessionSeedPacket(link, packet, packet_size);
        state.start_game_requested = true;
        PostMessageA(state.window, kIpxLobbyStartGameMessage, 0, 0);
        break;
    case 0x0b:
        ReportIpxLobbyDirectPlayJoinError(state, packet, message.bytes.size());
        break;
    case 0x15:
        ApplyLinkLobbyMapDescriptorPacket(link, packet, packet_size);
        break;
    case 0x20:
        ApplyLinkLobbyPlayerPresencePacket(link, packet, packet_size);
        break;
    default:
        break;
    }

    HGLOBAL global = GlobalHandle(const_cast<void*>(packet));
    if (global != nullptr) {
        GlobalUnlock(global);
        GlobalFree(global);
    }
    (void)sender;
}

} // namespace ranker

#endif
