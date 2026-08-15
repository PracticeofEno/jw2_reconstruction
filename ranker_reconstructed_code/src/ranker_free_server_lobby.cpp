#include "ranker_free_server_lobby.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_link_lobby.h"
#include "ranker_online_lobby.h"
#include "ranker_startup_environment.h"
#include "ranker_text_tables.h"
#include "ranker_winmain.h"
#include "ranker_wizardnet_relay.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kPasswordEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kListBoxStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr DWORD kComboStyle =
    WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
constexpr DWORD kInfoButtonStyle = WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW;
constexpr COLORREF kFreeWhite = RGB(255, 255, 255);
constexpr COLORREF kFreeSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kFreeListText = RGB(200, 200, 200);
constexpr COLORREF kFreeBlack = RGB(0, 0, 0);
constexpr COLORREF kFreeErrorBlue = RGB(20, 20, 255);
constexpr COLORREF kFreeErrorRed = RGB(255, 20, 20);
constexpr COLORREF kFreeCloseRed = RGB(250, 20, 20);
constexpr COLORREF kFreeStatusCyan = RGB(10, 250, 250);

FreeServerLobbyState g_free_server_lobby_state;
bool g_background_shutdown_registered = false;
bool g_info_button_shutdown_registered = false;
bool g_join_button_shutdown_registered = false;
bool g_cancel_button_shutdown_registered = false;
bool g_scroll_control_shutdown_registered = false;
bool g_game_type_combo_shutdown_registered = false;

const char* const kFreeServerGameTypeFallbacks[] = {
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

const char* const kFreeServerScreenSizeFallbacks[] = {
    "Free Size",
    "640x480",
    "800x600",
    "1024x768",
};

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK free_server_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleFreeServerLobbyWindowMessage(g_free_server_lobby_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK free_server_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleFreeServerLobbyControlMessage(g_free_server_lobby_state, hwnd, message,
        wparam, lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_global_background() {
    ShutdownFreeServerBackgroundBitmap(g_free_server_lobby_state);
}

void shutdown_global_info_button() {
    ShutdownFreeServerInfoButton(g_free_server_lobby_state);
}

void shutdown_global_join_button() {
    ShutdownFreeServerJoinButton(g_free_server_lobby_state);
}

void shutdown_global_cancel_button() {
    ShutdownFreeServerCancelButton(g_free_server_lobby_state);
}

void shutdown_global_scroll_control() {
    ShutdownFreeServerScrollControl(g_free_server_lobby_state);
}

void shutdown_global_game_type_combo() {
    ShutdownFreeServerGameTypeCombo(g_free_server_lobby_state);
}

FreeServerLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return FreeServerLayoutRect{};
}

void clear_control(FreeServerControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_control(FreeServerControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(free_server_control_proc));
}

bool create_control(FreeServerControl& control, HWND parent, HINSTANCE instance,
    const char* class_name, const char* text, DWORD style, int id,
    const FreeServerLayoutRect& rect) {
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

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const FreeServerLayoutRect& rect,
    u32 normal_record, u32 pressed_record) {
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
        reinterpret_cast<LONG_PTR>(free_server_control_proc));
    return true;
}

WNDPROC original_proc_for_id(FreeServerLobbyState& state, int id) {
    switch (id) {
    case kFreeServerNameEditId:
        return state.name_edit.original_window_proc;
    case kFreeServerPasswordEditId:
        return state.password_edit.original_window_proc;
    case kFreeServerGameListId:
        return state.game_list.original_window_proc;
    case kFreeServerScrollControlId:
        return state.scroll_control.original_window_proc;
    case kFreeServerInfoButtonId:
        return state.info_button.original_window_proc;
    case kFreeServerJoinButtonId:
        return state.join_button.original_window_proc;
    case kFreeServerCancelButtonId:
        return state.cancel_button.original_window_proc;
    case kFreeServerGameTypeComboId:
        return state.game_type_combo.original_window_proc;
    default:
        return nullptr;
    }
}

LegacyImageButtonControl* button_for_id(FreeServerLobbyState& state, int id) {
    switch (id) {
    case kFreeServerInfoButtonId:
        return &state.info_button;
    case kFreeServerJoinButtonId:
        return &state.join_button;
    case kFreeServerCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

void read_control_text(HWND window, char* buffer, int buffer_size) {
    if (buffer == nullptr || buffer_size <= 0) {
        return;
    }
    buffer[0] = '\0';
    if (window != nullptr) {
        GetWindowTextA(window, buffer, buffer_size);
    }
}

u32 read_u32(const void* data, std::size_t byte_count, std::size_t offset) {
    if (data == nullptr || offset > byte_count || byte_count - offset < sizeof(u32)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(data) + offset, sizeof(value));
    return value;
}

u16 read_u16(const void* data, std::size_t byte_count, std::size_t offset) {
    if (data == nullptr || offset > byte_count || byte_count - offset < sizeof(u16)) {
        return 0;
    }
    u16 value = 0;
    std::memcpy(&value, static_cast<const u8*>(data) + offset, sizeof(value));
    return value;
}

std::string c_string_at(const void* data, std::size_t byte_count, std::size_t offset,
    std::size_t max_count) {
    if (data == nullptr || offset >= byte_count) {
        return {};
    }
    const auto* chars = static_cast<const char*>(data) + offset;
    const std::size_t available = std::min(max_count, byte_count - offset);
    std::size_t length = 0;
    while (length < available && chars[length] != '\0') {
        ++length;
    }
    return std::string(chars, chars + length);
}

std::string ipv4_string(u32 address) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
        address & 0xff, (address >> 8) & 0xff, (address >> 16) & 0xff,
        (address >> 24) & 0xff);
    return buffer;
}

bool is_loopback_or_unspecified_ipv4(u32 address) {
    return address == INADDR_ANY || (address & 0xffu) == 127u;
}

u32 resolve_free_server_join_address(const FreeServerLobbyState& state,
    u32 advertised_address) {
    in_addr advertised{};
    advertised.s_addr = advertised_address;
    if (!is_loopback_or_unspecified_ipv4(advertised_address) &&
        !IsPrivateIpv4Address(advertised)) {
        return advertised_address;
    }
    if (state.async_tcp_socket == nullptr) {
        return advertised_address;
    }

    sockaddr_in wizardnet_peer{};
    int peer_size = sizeof(wizardnet_peer);
    if (getpeername(GetLegacyAsyncTcpSocket(*state.async_tcp_socket),
            reinterpret_cast<sockaddr*>(&wizardnet_peer), &peer_size) == SOCKET_ERROR ||
        wizardnet_peer.sin_family != AF_INET) {
        return advertised_address;
    }
    const u32 peer_address = wizardnet_peer.sin_addr.s_addr;
    in_addr peer{};
    peer.s_addr = peer_address;
    if (is_loopback_or_unspecified_ipv4(peer_address) ||
        IsPrivateIpv4Address(peer)) {
        return advertised_address;
    }
    return peer_address;
}

const char* free_server_game_type_label(int game_type) {
    if (game_type >= 0 &&
        game_type < static_cast<int>(std::size(kFreeServerGameTypeFallbacks))) {
        return startup_message_row(109 + static_cast<std::size_t>(game_type),
            kFreeServerGameTypeFallbacks[game_type]);
    }
    return "";
}

const char* free_server_screen_size_label(int screen_size) {
    if (screen_size >= 0 &&
        screen_size < static_cast<int>(std::size(kFreeServerScreenSizeFallbacks))) {
        return kFreeServerScreenSizeFallbacks[screen_size];
    }
    return "";
}

FreeServerGameEntry entry_from_server_record(const char* name, const void* game_info,
    u32 game_id, const void* map_descriptor) {
    constexpr std::size_t kSockaddrBytes = 0x10;
    constexpr std::size_t kMapDescriptorBytes = 0x2dc;
    FreeServerGameEntry entry;
    entry.name = name == nullptr ? "" : name;
    entry.id = static_cast<int>(game_id);
    if (game_info != nullptr) {
        entry.address = read_u32(game_info, kSockaddrBytes, 4);
        entry.port = ntohs(read_u16(game_info, kSockaddrBytes, 2));
    }
    if (map_descriptor != nullptr) {
        entry.game_type = static_cast<int>(
            read_u32(map_descriptor, kMapDescriptorBytes, 0x2a8));
        entry.width = static_cast<int>(
            read_u32(map_descriptor, kMapDescriptorBytes, 0x174));
        entry.height = static_cast<int>(
            read_u32(map_descriptor, kMapDescriptorBytes, 0x178));
        entry.display_mode = static_cast<int>(
            read_u32(map_descriptor, kMapDescriptorBytes, 0x2ac));
        entry.map_name = c_string_at(
            map_descriptor, kMapDescriptorBytes, 0x08, 0x20);
        entry.description = c_string_at(
            map_descriptor, kMapDescriptorBytes, 0x28, 0x140);
        entry.host_name = c_string_at(
            map_descriptor, kMapDescriptorBytes, 0x2b0, 0x20);
    }
    return entry;
}

void sync_game_list(FreeServerLobbyState& state) {
    if (state.game_list.window == nullptr) {
        return;
    }
    int top_index = static_cast<int>(
        SendMessageA(state.game_list.window, LB_GETTOPINDEX, 0, 0));
    if (top_index < 0) {
        top_index = 0;
    }
    SendMessageA(state.game_list.window, LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < state.games.size(); ++i) {
        const FreeServerGameEntry& entry = state.games[i];
        if (state.selected_game_type != 0 && entry.game_type != state.selected_game_type - 1) {
            continue;
        }
        LRESULT index = SendMessageA(state.game_list.window, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(entry.name.c_str()));
        if (index != LB_ERR) {
            SendMessageA(state.game_list.window, LB_SETITEMDATA,
                static_cast<WPARAM>(index), static_cast<LPARAM>(i));
        }
    }

    const int count = static_cast<int>(
        SendMessageA(state.game_list.window, LB_GETCOUNT, 0, 0));
    const int visible_rows = std::max(1, state.visible_count);
    const int max_top = std::max(0, count - visible_rows);
    const int clamped_top = std::min(top_index, max_top);
    SetLegacyCustomScrollControlRange(state.scroll_control, 0, max_top, false);
    SetLegacyCustomScrollControlValue(state.scroll_control, clamped_top, false);
    SetLegacyCustomScrollControlVisible(state.scroll_control, max_top > 0);
    SendMessageA(state.game_list.window, LB_SETTOPINDEX,
        static_cast<WPARAM>(clamped_top), 0);
}

std::size_t selected_entry_index(FreeServerLobbyState& state) {
    if (state.game_list.window == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    LRESULT selected = SendMessageA(state.game_list.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return static_cast<std::size_t>(-1);
    }
    LRESULT item_data = SendMessageA(state.game_list.window, LB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    if (item_data == LB_ERR || item_data < 0) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(item_data);
}

bool free_server_list_has_selection(const FreeServerLobbyState& state) {
    return state.game_list.window != nullptr &&
        SendMessageA(state.game_list.window, LB_GETCURSEL, 0, 0) != LB_ERR;
}

void draw_game_list_item(FreeServerLobbyState&, const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return;
    }
    char text[256]{};
    SendMessageA(draw.hwndItem, LB_GETTEXT, draw.itemID, reinterpret_cast<LPARAM>(text));
    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(draw.hDC, kFreeListText);
    SetBkMode(draw.hDC, (draw.itemState & ODS_SELECTED) != 0 ? OPAQUE : TRANSPARENT);
    if ((draw.itemState & ODS_SELECTED) != 0) {
        SetBkColor(draw.hDC, RGB(0, 0, 255));
    }
    rect.left += 8;
    DrawTextA(draw.hDC, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void draw_info_panel(FreeServerLobbyState& state, const DRAWITEMSTRUCT& draw) {
    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    rect.left += 6;
    rect.top += 6;
    rect.right -= 6;
    rect.bottom -= 6;

    SetTextColor(draw.hDC, kFreeWhite);
    SetBkColor(draw.hDC, kFreeBlack);
    SetBkMode(draw.hDC, TRANSPARENT);

    std::string text;
    switch (state.info_state) {
    case FreeServerInfoState::Selected: {
        const std::size_t index = selected_entry_index(state);
        if (index < state.games.size()) {
            const FreeServerGameEntry& entry = state.games[index];
            char buffer[768]{};
            std::snprintf(buffer, sizeof(buffer),
                "%s\n%s (%dx%d)\n%s%s\n%s%s\n%s%s\n%s%s",
                startup_message_row(133, "Map: "), entry.map_name.c_str(),
                entry.width, entry.height,
                startup_message_row(134, "Creator: "), entry.host_name.c_str(),
                startup_message_row(141, "Game type: "),
                free_server_game_type_label(entry.game_type),
                startup_message_row(135, "Screen size: "),
                free_server_screen_size_label(entry.display_mode),
                startup_message_row(136, "Time: "), entry.description.c_str());
            text = buffer;
        }
        break;
    }
    case FreeServerInfoState::PasswordRequired:
        text = startup_message_row(39, "This game requires a password.");
        break;
    case FreeServerInfoState::BadSession:
        text = startup_message_row(40, "The selected game entry is no longer valid.");
        break;
    case FreeServerInfoState::VersionMismatch: {
        char buffer[160]{};
        std::snprintf(buffer, sizeof(buffer),
            startup_message_row(41,
                "Version mismatch. Host version %u, local version %u."),
            state.remote_version, state.expected_version);
        text = buffer;
        break;
    }
    case FreeServerInfoState::Empty:
    default:
        text = state.info_text.empty() ?
            startup_message_row(38, "No game selected.") : state.info_text;
        break;
    }

    DrawTextA(draw.hDC, text.c_str(), -1, &rect, DT_LEFT | DT_WORDBREAK);
}

void redraw_info(FreeServerLobbyState& state) {
    HWND info = GetLegacyImageButtonWindow(state.info_button);
    if (info != nullptr) {
        RedrawWindow(info, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

void show_status(FreeServerLobbyState& state, const char* text, COLORREF color) {
    state.last_message = text == nullptr ? "" : text;
    state.info_text = state.last_message;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
    }
    redraw_info(state);
}

void show_startup_status(FreeServerLobbyState& state, std::size_t index,
    const char* fallback, COLORREF color) {
    show_status(state, startup_message_row(index, fallback), color);
}

std::string format_startup_status(std::size_t index, const char* fallback,
    const char* text) {
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), startup_message_row(index, fallback),
        text != nullptr ? text : "");
    return buffer;
}

std::string format_version_status(FreeServerLobbyState& state, const void* packet,
    std::size_t packet_size) {
    const u32 remote = read_u32(packet, packet_size, 0x0c);
    const u32 local = state.expected_version;
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer),
        startup_message_row(49,
            "Connection failed - game version mismatch. (my version = %d-%d-%d, host version = %d-%d-%d)"),
        local & 0xffffu, (local >> 16) & 0xffu, (local >> 24) & 0xffu,
        remote & 0xffffu, (remote >> 16) & 0xffu, (remote >> 24) & 0xffu);
    return buffer;
}

bool free_server_rank_count_available(const FreeServerLobbyState& state, int game_type) {
    if (game_type == 2) {
        return state.server_top_bottom_counts[0] + state.server_top_bottom_counts[1] >= 10;
    }
    if (game_type == 4) {
        return state.server_use_map_counts[0] + state.server_use_map_counts[1] >= 10;
    }
    return true;
}

void queue_server_packet(FreeServerLobbyState& state, const void* packet,
    i32 byte_count) {
    if (state.callbacks.queue_server_packet != nullptr) {
        state.callbacks.queue_server_packet(state, packet, byte_count);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket,
            const_cast<void*>(packet), byte_count, nullptr);
    }
}

void schedule_free_server_async_drain(FreeServerLobbyState& state) {
    if (state.window == nullptr || !IsWindow(state.window)) {
        return;
    }
    PostMessageA(state.window, kFreeServerNetworkMessage, 0,
        MAKELPARAM(FD_READ, 0));
}

bool post_free_server_socket_payload(FreeServerLobbyState& state, SOCKET sender,
    const void* packet, u32 byte_count) {
    if (packet == nullptr || byte_count == 0 || state.window == nullptr) {
        return false;
    }
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byte_count);
    void* copy = global == nullptr ? nullptr : GlobalLock(global);
    if (copy == nullptr) {
        if (global != nullptr) {
            GlobalFree(global);
        }
        return false;
    }
    std::memcpy(copy, packet, byte_count);
    if (PostMessageA(state.window, kFreeServerSocketPayloadMessage,
            static_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(copy)) == FALSE) {
        GlobalUnlock(global);
        GlobalFree(global);
        return false;
    }
    return true;
}

void write_le32(std::vector<u8>& packet, std::size_t offset, u32 value) {
    if (offset + sizeof(u32) > packet.size()) {
        return;
    }
    std::memcpy(packet.data() + offset, &value, sizeof(value));
}

void queue_initial_server_requests(FreeServerLobbyState& state) {
    std::vector<u8> packet(0x4d, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x45);
    write_le32(packet, 8, 0x4d);
    queue_server_packet(state, packet.data(), static_cast<i32>(packet.size()));

    packet.assign(0x2d, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x3d);
    write_le32(packet, 8, 0x2d);
    queue_server_packet(state, packet.data(), static_cast<i32>(packet.size()));

    packet.assign(0x4d, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 99);
    write_le32(packet, 8, 0x4d);
    queue_server_packet(state, packet.data(), static_cast<i32>(packet.size()));

    packet.assign(0x11, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x1d);
    write_le32(packet, 8, 0x11);
    queue_server_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void queue_game_type_filter(FreeServerLobbyState& state) {
    std::vector<u8> packet(0x11, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x1d);
    write_le32(packet, 8, 0x11);
    // Opcode 0x1d carries the game-list record index, not the UI filter.
    // Filtering is local, so a refresh always restarts from the first record.
    write_le32(packet, 0x0d, 0);
    queue_server_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void resume_free_server_game_list_after_relay_join(FreeServerLobbyState& state) {
    const OnlineLobbyState& online = online_lobby_state();
    const bool joined_from_wizardnet =
        online.window != nullptr && state.parent_window == online.window;
    if (!joined_from_wizardnet || state.async_tcp_socket == nullptr) {
        return;
    }
    queue_game_type_filter(state);
}

void close_free_server_game_socket(FreeServerLobbyState& state) {
    if (state.game_socket == INVALID_SOCKET) {
        state.relay_game_id = 0;
        return;
    }
    if (WizardNetRelaySocketIsMember(state.game_socket)) {
        QueueWizardNetRelayLeaveForGame(state.relay_game_id);
    } else {
        CloseLegacySocketRecord(state.game_socket);
    }
    state.game_socket = INVALID_SOCKET;
    state.relay_game_id = 0;
}

void release_resources(FreeServerLobbyState& state) {
    if (state.join_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.join_timer);
        state.join_timer = 0;
    }
    if (state.window != nullptr) {
        KillTimer(state.window, kFreeServerBrowserPumpTimerId);
    }
    close_free_server_game_socket(state);
    RestoreFreeServerAccelerators(state);
    ClearFreeServerLobbyEntries(state, state.game_list.window);
    ReleaseBitmapMemoryResource(state.background);
    ShutdownFreeServerInfoButton(state);
    ShutdownFreeServerJoinButton(state);
    ShutdownFreeServerCancelButton(state);
    ShutdownFreeServerGameTypeCombo(state);
    ShutdownFreeServerScrollControl(state);
    clear_control(state.name_edit);
    clear_control(state.password_edit);
    clear_control(state.game_list);
    state.window = nullptr;
    state.visible = false;
}

void close_lobby(FreeServerLobbyState& state, bool return_to_connect) {
    state.visible = false;
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
    if (return_to_connect && state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    }
}

void fill_combo(HWND combo) {
    if (combo == nullptr) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    SendMessageA(combo, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(startup_message_row(127, "Show All")));
    for (std::size_t index = 0; index < 9; ++index) {
        char label[128]{};
        std::snprintf(label, sizeof(label), "%s %s",
            startup_message_row(126, "Show"),
            startup_message_row(109 + index, "Game Type"));
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    SendMessageA(combo, CB_SETCURSEL, 0, 0);
}

void handle_combo_change(FreeServerLobbyState& state) {
    LRESULT selected = SendMessageA(state.game_type_combo.window,
        CB_GETCURSEL, 0, 0);
    if (selected != CB_ERR && selected != state.selected_game_type) {
        state.selected_game_type = static_cast<int>(selected);
        state.info_state = FreeServerInfoState::Empty;
        sync_game_list(state);
        queue_game_type_filter(state);
    }
}

void handle_list_selection(FreeServerLobbyState& state) {
    const std::size_t index = selected_entry_index(state);
    if (index >= state.games.size()) {
        state.selected_index = -1;
        state.info_state = FreeServerInfoState::Empty;
        redraw_info(state);
        return;
    }
    state.selected_index = static_cast<int>(index);
    SetWindowTextA(state.name_edit.window, state.games[index].name.c_str());
    SelectFreeServerLobbyEntry(state, state.game_list.window);
    SetFocus(state.password_edit.window);
}

} // namespace

FreeServerLobbyState& free_server_lobby_state() {
    return g_free_server_lobby_state;
}

void InitializeFreeServerBackgroundResourceAndShutdown(FreeServerLobbyState& state) {
    InitializeFreeServerBackgroundBitmap(state);
    RegisterFreeServerBackgroundShutdown(state);
}

void InitializeFreeServerBackgroundBitmap(FreeServerLobbyState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterFreeServerBackgroundShutdown(FreeServerLobbyState&) {
    register_atexit_once(g_background_shutdown_registered,
        shutdown_global_background);
}

void ShutdownFreeServerBackgroundBitmap(FreeServerLobbyState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeFreeServerInfoButtonSupport(FreeServerLobbyState& state) {
    InitializeFreeServerInfoButton(state);
    RegisterFreeServerInfoButtonShutdown(state);
}

void InitializeFreeServerInfoButton(FreeServerLobbyState& state) {
    InitializeLegacyImageButtonControl(state.info_button);
}

void RegisterFreeServerInfoButtonShutdown(FreeServerLobbyState&) {
    register_atexit_once(g_info_button_shutdown_registered,
        shutdown_global_info_button);
}

void ShutdownFreeServerInfoButton(FreeServerLobbyState& state) {
    DestroyLegacyImageButtonControl(state.info_button);
}

void InitializeFreeServerJoinButtonSupport(FreeServerLobbyState& state) {
    InitializeFreeServerJoinButton(state);
    RegisterFreeServerJoinButtonShutdown(state);
}

void InitializeFreeServerJoinButton(FreeServerLobbyState& state) {
    InitializeLegacyImageButtonControl(state.join_button);
}

void RegisterFreeServerJoinButtonShutdown(FreeServerLobbyState&) {
    register_atexit_once(g_join_button_shutdown_registered,
        shutdown_global_join_button);
}

void ShutdownFreeServerJoinButton(FreeServerLobbyState& state) {
    DestroyLegacyImageButtonControl(state.join_button);
}

void InitializeFreeServerCancelButtonSupport(FreeServerLobbyState& state) {
    InitializeFreeServerCancelButton(state);
    RegisterFreeServerCancelButtonShutdown(state);
}

void InitializeFreeServerCancelButton(FreeServerLobbyState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterFreeServerCancelButtonShutdown(FreeServerLobbyState&) {
    register_atexit_once(g_cancel_button_shutdown_registered,
        shutdown_global_cancel_button);
}

void ShutdownFreeServerCancelButton(FreeServerLobbyState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InitializeFreeServerScrollControlSupport(FreeServerLobbyState& state) {
    InitializeFreeServerScrollControl(state);
    RegisterFreeServerScrollControlShutdown(state);
}

void InitializeFreeServerScrollControl(FreeServerLobbyState& state) {
    InitializeLegacyCustomScrollControl(state.scroll_control);
}

void RegisterFreeServerScrollControlShutdown(FreeServerLobbyState&) {
    register_atexit_once(g_scroll_control_shutdown_registered,
        shutdown_global_scroll_control);
}

void ShutdownFreeServerScrollControl(FreeServerLobbyState& state) {
    DestroyLegacyCustomScrollControl(state.scroll_control);
}

void InitializeFreeServerGameTypeComboSupport(FreeServerLobbyState& state) {
    InitializeFreeServerGameTypeCombo(state);
    RegisterFreeServerGameTypeComboShutdown(state);
}

void InitializeFreeServerGameTypeCombo(FreeServerLobbyState& state) {
    InitializeLegacyImageComboBoxControl(state.game_type_combo);
}

void RegisterFreeServerGameTypeComboShutdown(FreeServerLobbyState&) {
    register_atexit_once(g_game_type_combo_shutdown_registered,
        shutdown_global_game_type_combo);
}

void ShutdownFreeServerGameTypeCombo(FreeServerLobbyState& state) {
    DestroyLegacyImageComboBoxControl(state.game_type_combo);
}

void InstallFreeServerAccelerators(FreeServerLobbyState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kFreeServerAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreFreeServerAccelerators(FreeServerLobbyState& state) {
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

void ShowFreeServerSelectGameMessage(FreeServerLobbyState& state) {
    show_startup_status(state, 31, "Select a game to join.", kFreeSoftWhite);
}

bool SubmitFreeServerJoinRequest(FreeServerLobbyState& state) {
    const std::size_t index = selected_entry_index(state);
    if (state.player_name[0] == '\0') {
        show_startup_status(state, 32, "Select a game to join.", kFreeSoftWhite);
        return false;
    }
    read_control_text(state.password_edit.window, state.password.data(),
        static_cast<int>(state.password.size()));

    if (index >= state.games.size()) {
        show_startup_status(state, 53,
            "Connection failed - game already started or no response.",
            kFreeSoftWhite);
        return false;
    }
    const FreeServerGameEntry& entry = state.games[index];
    if (!free_server_rank_count_available(state, entry.game_type)) {
        show_startup_status(state, 1,
            "Rank games require at least ten normal games of this type.",
            kFreeSoftWhite);
        return false;
    }
    const OnlineLobbyState& online = online_lobby_state();
    const bool joined_from_wizardnet =
        online.window != nullptr && state.parent_window == online.window;
    const bool relay_available =
        joined_from_wizardnet && state.async_tcp_socket != nullptr && entry.id > 0;
    const u32 target_address =
        resolve_free_server_join_address(state, entry.address);
    if (!entry.joinable ||
        (!relay_available && (target_address == 0 || entry.port == 0))) {
        show_startup_status(state, 53,
            "Connection failed - game already started or no response.",
            kFreeSoftWhite);
        return false;
    }

    const std::string address = ipv4_string(target_address);
    if (state.join_timer != 0) {
        KillTimer(state.window, state.join_timer);
        state.join_timer = 0;
    }
    if (state.game_socket != INVALID_SOCKET) {
        close_free_server_game_socket(state);
    }
    state.relay_game_id = 0;
    state.join_phase = FreeServerJoinPhase::Connecting;
    state.game_start_requested = false;
    if (relay_available) {
        std::vector<u8> relay_join = BuildLinkLobbyRelayJoinPacket(
            link_lobby_state(), state.player_name.data(), state.password.data());
        if (QueueWizardNetRelayJoin(static_cast<u32>(entry.id),
                relay_join.data(), static_cast<u32>(relay_join.size()))) {
            append_startup_log(
                "wizardnet relay browser join queued game=%ld room=%s bytes=%zu",
                static_cast<long>(entry.id), entry.name.c_str(),
                relay_join.size());
            state.game_socket = WizardNetRelaySocketForMember(1);
            state.relay_game_id = static_cast<u32>(entry.id);
            state.join_timer = SetTimer(state.window, kFreeServerJoinTimerId,
                kFreeServerJoinRetryMs, nullptr);
            const std::string message =
                format_startup_status(33, "%s connecting.", state.player_name.data());
            show_status(state, message.c_str(), kFreeStatusCyan);
            return true;
        }
        append_startup_log("wizardnet relay browser join queue failed game=%ld room=%s",
            static_cast<long>(entry.id), entry.name.c_str());
    }
    if (joined_from_wizardnet) {
        state.join_phase = FreeServerJoinPhase::Idle;
        resume_free_server_game_list_after_relay_join(state);
        show_startup_status(state, 37,
            "Connection failed - no response.", kFreeErrorRed);
        return false;
    }
    if (target_address == 0 || entry.port == 0) {
        state.join_phase = FreeServerJoinPhase::Idle;
        show_startup_status(state, 34, "TCP/IP initialization error.",
            kFreeSoftWhite);
        return false;
    }
    if (StartLegacySocketConnect(state.game_socket, address.c_str(), entry.port,
            state.window, kFreeServerSocketNotifyMessage)) {
        state.join_timer =
            SetTimer(state.window, kFreeServerJoinTimerId, kFreeServerJoinRetryMs, nullptr);
        const std::string message =
            format_startup_status(33, "%s connecting.", state.player_name.data());
        show_status(state, message.c_str(), kFreeStatusCyan);
        return true;
    }

    state.join_phase = FreeServerJoinPhase::Idle;
    show_startup_status(state, 34, "TCP/IP initialization error.",
        kFreeSoftWhite);
    return false;
}

bool CreateFreeServerLobbyWindow(FreeServerLobbyState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.games.clear();
    state.player_name.fill(0);
    state.password.fill(0);
    state.launch_context.fill(0);
    state.server_top_bottom_counts = {};
    state.server_use_map_counts = {};
    state.selected_index = -1;
    state.selected_game_type = 0;
    state.info_state = FreeServerInfoState::Empty;
    state.join_phase = FreeServerJoinPhase::Idle;
    state.game_start_requested = false;

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kFreeServerLayoutTrcRecord)) {
        return false;
    }

    const FreeServerLayoutRect window_rect = layout_at(layout.table, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(parent,
              window_rect.width, window_rect.height)
        : RankerCenteredFrontendWindowOrigin(
              window_rect.width, window_rect.height);
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Join Game", "Join Game",
        style, origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(free_server_window_proc));

    if (state.async_tcp_socket != nullptr &&
        !RegisterLegacyAsyncTcpSocketEvents(*state.async_tcp_socket, state.window,
            kFreeServerNetworkMessage, FD_READ | FD_WRITE | FD_CLOSE)) {
        DestroyWindow(state.window);
        return false;
    }

    const FreeServerLayoutRect list_rect = layout_at(layout.table, 3);
    const FreeServerLayoutRect scroll_rect = layout_at(layout.table, 4);
    const FreeServerLayoutRect combo_rect = layout_at(layout.table, 8);
    if (!create_control(state.name_edit, state.window, instance, "edit", nullptr,
            kEditStyle, kFreeServerNameEditId, layout_at(layout.table, 1)) ||
        !create_control(state.password_edit, state.window, instance, "edit", nullptr,
            kPasswordEditStyle, kFreeServerPasswordEditId,
            layout_at(layout.table, 2)) ||
        !create_control(state.game_list, state.window, instance, "listbox", nullptr,
            kListBoxStyle, kFreeServerGameListId, list_rect) ||
        !CreateLegacyCustomScrollControlWindow(state.scroll_control, state.window,
            "Join Game",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFreeServerScrollControlId)),
            false, scroll_rect.x, scroll_rect.y, scroll_rect.width,
            scroll_rect.height) ||
        !create_image_button(state.info_button, state.window, "Game infos",
            kFreeServerInfoButtonId, layout_at(layout.table, 5), 0, 0) ||
        !create_image_button(state.join_button, state.window, "Join &Game",
            kFreeServerJoinButtonId, layout_at(layout.table, 6),
            kFreeServerJoinNormalBitmapRecord, kFreeServerJoinPressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kFreeServerCancelButtonId, layout_at(layout.table, 7),
            kFreeServerCancelNormalBitmapRecord, kFreeServerCancelPressedBitmapRecord) ||
        !CreateLegacyImageComboBoxWindow(state.game_type_combo, state.window,
            "Show Game Type",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFreeServerGameTypeComboId)),
            kComboStyle, combo_rect.x, combo_rect.y, combo_rect.width,
            combo_rect.height + 0x96)) {
        release_resources(state);
        return false;
    }
    SetWindowLongPtrA(GetLegacyImageButtonWindow(state.info_button), GWL_STYLE,
        static_cast<LONG_PTR>(kInfoButtonStyle));

    HWND combo = state.game_type_combo.window;
    state.game_type_combo.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(combo, GWLP_WNDPROC));
    SetWindowLongPtrA(combo, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(free_server_control_proc));
    LoadLegacyImageComboBoxBitmaps(state.game_type_combo, kFreeServerComboBitmapRecord, 0);
    fill_combo(combo);

    SendMessageA(state.name_edit.window, EM_LIMITTEXT, state.player_name.size() - 1, 0);
    SendMessageA(state.password_edit.window, EM_LIMITTEXT, state.password.size() - 1, 0);
    SetWindowLongPtrA(state.scroll_control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(free_server_control_proc));
    LoadLegacyCustomScrollControlBitmaps(state.scroll_control,
        kFreeServerIconRecord0, 0, kFreeServerIconRecord1, 0,
        kFreeServerIconRecord2, kFreeServerIconRecord3);
    const int item_height = static_cast<int>(
        SendMessageA(state.game_list.window, LB_GETITEMHEIGHT, 0, 0));
    state.visible_count = std::max(1, list_rect.height / std::max(1, item_height));
    SetLegacyCustomScrollControlPageStep(state.scroll_control, state.visible_count);
    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.game_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    InstallFreeServerAccelerators(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kFreeServerBackgroundBitmapRecord);
    ShowWindow(state.game_list.window, SW_SHOW);
    ShowWindow(combo, SW_SHOW);
    SetFocus(state.name_edit.window);
    queue_initial_server_requests(state);
    state.visible = true;
    SetTimer(state.window, kFreeServerBrowserPumpTimerId,
        kFreeServerBrowserPumpMs, nullptr);
    // The Online Lobby can leave stale or newly-arrived bytes in the shared
    // async TCP queue while socket events are being moved to the Join Game HWND.
    // Drain once after the initial browser requests so the first game-list page
    // cannot depend on a later Winsock edge.
    schedule_free_server_async_drain(state);
    return true;
}

LRESULT HandleFreeServerLobbyWindowMessage(FreeServerLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        release_resources(state);
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
        SetTextColor(reinterpret_cast<HDC>(wparam), kFreeSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kFreeBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kFreeSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kFreeBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kFreeServerGameTypeComboId) {
            DrawLegacyImageComboBoxItem(state.game_type_combo, *draw);
            break;
        }
        if (draw->CtlID == kFreeServerGameListId) {
            draw_game_list_item(state, *draw);
            break;
        }
        if (draw->CtlID == kFreeServerInfoButtonId) {
            draw_info_panel(state, *draw);
            break;
        }
        LegacyImageButtonControl* button = button_for_id(state, draw->CtlID);
        if (button != nullptr) {
            DrawLegacyImageButtonItem(*button, *draw);
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        switch (id) {
        case kFreeServerFocusNameCommandId:
            SetFocus(state.name_edit.window);
            break;
        case kFreeServerFocusPasswordCommandId:
            SetFocus(state.password_edit.window);
            break;
        case kFreeServerCancelButtonId:
            HandleDefaultFrontendUiClickSound();
            close_lobby(state, true);
            break;
        case kFreeServerGameTypeComboId:
            if (notify == CBN_SELCHANGE) {
                handle_combo_change(state);
            }
            break;
        case kFreeServerGameListId:
            if (notify == LBN_SELCHANGE) {
                handle_list_selection(state);
                break;
            }
            if (notify == LBN_DBLCLK) {
                PostMessageA(hwnd, WM_COMMAND,
                    static_cast<WPARAM>(kFreeServerJoinButtonId), 0);
            }
            break;
        case kFreeServerJoinButtonId:
            if (free_server_list_has_selection(state)) {
                // A server refresh can restore the native list-box selection
                // without emitting LBN_SELCHANGE. Normalize the matching
                // reconstructed state before deciding whether Join is valid.
                SelectFreeServerLobbyEntry(state, state.game_list.window);
                SubmitFreeServerJoinRequest(state);
            }
            else {
                ShowFreeServerSelectGameMessage(state);
            }
            break;
        default:
            break;
        }
        break;
    }
    case WM_TIMER:
        if (wparam == kFreeServerBrowserPumpTimerId) {
            schedule_free_server_async_drain(state);
            return 0;
        }
        if (wparam == kFreeServerJoinTimerId &&
            state.join_phase != FreeServerJoinPhase::Idle) {
            close_free_server_game_socket(state);
            if (state.join_timer != 0) {
                KillTimer(hwnd, state.join_timer);
                state.join_timer = 0;
            }
            state.join_phase = FreeServerJoinPhase::Idle;
            resume_free_server_game_list_after_relay_join(state);
            show_startup_status(state, 37,
                "Connection failed - no response.",
                kFreeErrorRed);
            return 0;
        }
        break;
    case kFreeServerNetworkMessage:
        DispatchFreeServerNetworkMessage(state, hwnd, message, wparam, lparam);
        break;
    case kFreeServerSocketPayloadMessage:
        DispatchFreeServerSocketPayload(state, wparam, lparam);
        break;
    case kFreeServerStartGameMessage:
        state.game_start_requested = true;
            {
                // The joined Link lobby takes ownership of this socket. Detach it
                // before destroying the browser so release_resources keeps it open.
                const SOCKET joined_socket = state.game_socket;
                const u32 joined_relay_game_id = state.relay_game_id;
                state.game_socket = INVALID_SOCKET;
                state.relay_game_id = 0;
                close_lobby(state, false);
                state.game_socket = joined_socket;
                state.relay_game_id = joined_relay_game_id;
                if (state.callbacks.start_game != nullptr) {
                    state.callbacks.start_game(state);
                }
                state.game_socket = INVALID_SOCKET;
                state.relay_game_id = 0;
            }
            break;
    case kFreeServerJoinErrorMessage: {
        show_startup_status(state, 42, "Connection failed - general error.",
            kFreeErrorRed);
        break;
    }
    case kFreeServerJoinStatusMessage:
        show_startup_status(state, 36, "Getting connected player information.",
            kFreeStatusCyan);
        break;
    case kFreeServerSocketNotifyMessage:
        HandleFreeServerSocketMessage(state, static_cast<SOCKET>(wparam), lparam);
        break;
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void DeleteFreeServerGameTypeComboBox(FreeServerLobbyState& state, bool free_storage) {
    ShutdownFreeServerGameTypeCombo(state);
    if (free_storage) {
        // Original scalar deleting destructor optionally freed object storage.
    }
}

LRESULT HandleFreeServerLobbyControlMessage(FreeServerLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (message == WM_PAINT && hwnd == state.scroll_control.window) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.scroll_control, paint.hdc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    if (id == kFreeServerGameTypeComboId && message == WM_PAINT) {
        PaintLegacyImageComboBoxBackground(state.game_type_combo);
    }

    if (id == kFreeServerScrollControlId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.scroll_control, message, wparam, lparam);
        if (changed && state.game_list.window != nullptr) {
            const int top = GetLegacyCustomScrollControlValue(state.scroll_control);
            SendMessageA(state.game_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(top), 0);
        }
    }
    switch (id) {
    case kFreeServerNameEditId:
    case kFreeServerPasswordEditId:
    case kFreeServerInfoButtonId:
    case kFreeServerGameTypeComboId:
    case kFreeServerGameListId:
    case kFreeServerScrollControlId:
    case kFreeServerJoinButtonId:
    case kFreeServerCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

void ReportFreeServerJoinError(FreeServerLobbyState& state, const void* packet,
    std::size_t packet_size) {
    const u32 status = read_u32(packet, packet_size, 0x10);
    switch (status) {
    case 0:
        show_startup_status(state, 42, "Connection failed - general error.",
            kFreeErrorRed);
        break;
    case 1:
        show_startup_status(state, 43, "Connection failed - map file send error.",
            kFreeErrorRed);
        break;
    case 2:
        show_startup_status(state, 44,
            "Connection failed - connected player info error.",
            kFreeErrorRed);
        break;
    case 3:
        show_startup_status(state, 45,
            "Connection failed - selected game is full.", kFreeErrorRed);
        break;
    case 4:
        show_startup_status(state, 46,
            "Connection failed - player info send error.", kFreeErrorRed);
        break;
    case 5:
        show_startup_status(state, 47,
            "Connection failed - game already started.", kFreeErrorRed);
        break;
    case 6:
        show_startup_status(state, 48,
            "Connection failed - different game type.", kFreeErrorRed);
        break;
    case 7:
        {
            const std::string message =
                format_version_status(state, packet, packet_size);
            show_status(state, message.c_str(), kFreeErrorRed);
        }
        break;
    case 8:
        show_startup_status(state, 50,
            "Connection failed - connection mode mismatch.", kFreeErrorRed);
        break;
    case 9:
        show_startup_status(state, 51,
            "Connection failed - wrong password.", kFreeErrorRed);
        break;
    case 10:
        state.join_phase = FreeServerJoinPhase::WaitingForStart;
        show_startup_status(state, 36, "Getting connected player information.",
            kFreeStatusCyan);
        break;
    default:
        show_startup_status(state, 42, "Connection failed - general error.",
            kFreeErrorRed);
        break;
    }
}

bool AddFreeServerLobbyEntry(FreeServerLobbyState& state, const char* name,
    const void* game_info, const void*, u32 game_id, const void* map_descriptor) {
    FreeServerGameEntry entry = entry_from_server_record(
        name, game_info, game_id, map_descriptor);
    if (entry.name.empty()) {
        entry.name = "Game";
    }
    if (entry.id < 0) {
        entry.id = static_cast<int>(state.games.size());
    }
    auto existing = std::find_if(state.games.begin(), state.games.end(),
        [entry](const FreeServerGameEntry& value) { return value.id == entry.id; });
    if (existing != state.games.end()) {
        *existing = entry;
    }
    else {
        state.games.push_back(entry);
    }
    sync_game_list(state);
    return true;
}

bool RemoveFreeServerLobbyEntryById(FreeServerLobbyState& state, int id) {
    auto it = std::find_if(state.games.begin(), state.games.end(),
        [id](const FreeServerGameEntry& value) { return value.id == id; });
    if (it == state.games.end()) {
        return false;
    }
    state.games.erase(it);
    sync_game_list(state);
    return true;
}

void SelectFreeServerLobbyEntry(FreeServerLobbyState& state, HWND) {
    const std::size_t index = selected_entry_index(state);
    if (index >= state.games.size()) {
        state.info_state = FreeServerInfoState::Empty;
        redraw_info(state);
        return;
    }
    const FreeServerGameEntry& entry = state.games[index];
    state.info_state = entry.password_required ?
        FreeServerInfoState::PasswordRequired : FreeServerInfoState::Selected;
    redraw_info(state);
}

void ClearFreeServerLobbyEntries(FreeServerLobbyState& state, HWND listbox) {
    state.games.clear();
    state.selected_index = -1;
    if (listbox != nullptr) {
        SendMessageA(listbox, LB_RESETCONTENT, 0, 0);
    }
    SetLegacyCustomScrollControlRange(state.scroll_control, 0, 0, false);
    SetLegacyCustomScrollControlValue(state.scroll_control, 0, false);
    SetLegacyCustomScrollControlVisible(state.scroll_control, false);
}

void DispatchFreeServerSocketPayload(FreeServerLobbyState& state, WPARAM sender,
    LPARAM payload) {
    if (payload == 0) {
        return;
    }
    const void* packet = reinterpret_cast<const void*>(payload);
    HGLOBAL global = GlobalHandle(const_cast<void*>(packet));
    const std::size_t payload_size =
        global != nullptr ? static_cast<std::size_t>(GlobalSize(global)) : 0;
    if (payload_size == 0) {
        if (global != nullptr) {
            GlobalUnlock(global);
            GlobalFree(global);
        }
        return;
    }

    const u32 type = read_u32(packet, payload_size, 4);
    LinkLobbyState& link = link_lobby_state();
    std::size_t packet_size = read_u32(packet, payload_size, 8);
    if (packet_size < 0x0c || packet_size > payload_size) {
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
    if (packet_size > payload_size) {
        packet_size = payload_size;
    }
    if (packet_size == 0) {
        GlobalUnlock(global);
        GlobalFree(global);
        return;
    }
    FreeServerSocketPayload socket_payload;
    socket_payload.type = type;
    const auto* first = static_cast<const u8*>(packet);
    socket_payload.bytes.assign(first, first + packet_size);
    if (state.callbacks.handle_socket_payload != nullptr) {
        state.callbacks.handle_socket_payload(state, socket_payload);
    }

    switch (type) {
    case 10:
        ApplyLinkLobbySessionSeedPacket(link, packet, packet_size);
        state.game_start_requested = true;
        PostMessageA(state.window, kFreeServerStartGameMessage, 0, 0);
        break;
    case 0x0b:
        ReportFreeServerJoinError(state, packet, socket_payload.bytes.size());
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

    if (global != nullptr) {
        GlobalUnlock(global);
        GlobalFree(global);
    }
    (void)sender;
}

void PumpFreeServerSocketReceiveQueue(FreeServerLobbyState& state,
    LegacySocketRecord& record) {
    while (record.receive_queue.size() >= 8) {
        const u32 type = read_u32(record.receive_queue.data(), record.receive_queue.size(), 0);
        if (type == 0) {
            const u32 header_size = record.receive_queue.size() > 0x23 ?
                record.receive_queue[0x23] + 0x0c : 0;
            if (header_size == 0 || header_size > record.receive_queue.size()) {
                return;
            }
            ConsumeLegacySocketReceiveQueue(record, header_size);
            continue;
        }
        if (type == 2) {
            const u32 byte_count = read_u32(record.receive_queue.data(),
                record.receive_queue.size(), 8);
            if (byte_count == 0 || byte_count > record.receive_queue.size()) {
                return;
            }
            if (!post_free_server_socket_payload(state, record.socket,
                    record.receive_queue.data(), byte_count)) {
                return;
            }
            ConsumeLegacySocketReceiveQueue(record, byte_count);
            continue;
        }
        return;
    }
}

void HandleFreeServerSocketMessage(FreeServerLobbyState& state, SOCKET socket,
    LPARAM event) {
    const int socket_error = HIWORD(event);
    if (socket_error != 0) {
        if (state.join_timer != 0 && state.window != nullptr) {
            KillTimer(state.window, state.join_timer);
            state.join_timer = 0;
        }
        if (WizardNetRelaySocketIsMember(socket)) {
            QueueWizardNetRelayLeaveForGame(state.relay_game_id);
            state.relay_game_id = 0;
        } else {
            CloseLegacySocketRecord(socket);
        }
        if (state.game_socket == socket) {
            state.game_socket = INVALID_SOCKET;
        }
        state.join_phase = FreeServerJoinPhase::Idle;
        show_startup_status(state, 37,
            "Connection failed - no response.", kFreeErrorRed);
        return;
    }

    switch (LOWORD(event)) {
    case FD_READ: {
        LegacySocketRecord* record = ReceiveIntoLegacySocketQueue(socket);
        if (record != nullptr) {
            PumpFreeServerSocketReceiveQueue(state, *record);
        }
        break;
    }
    case FD_WRITE:
        if (state.join_phase == FreeServerJoinPhase::Connecting) {
            SendLinkLobbyRelayJoinPacket(link_lobby_state(), socket,
                state.player_name.data(), state.password.data());
            state.join_phase = FreeServerJoinPhase::WaitingForStart;
            show_startup_status(state, 36,
                "Getting connected player information.", kFreeStatusCyan);
        }
        else {
            QueueAndFlushSocketSend(0, nullptr, socket);
        }
        break;
    case FD_CONNECT:
        if (FindLegacySocketRecord(socket) == nullptr) {
            RegisterLegacySocketRecord(socket);
        }
        break;
    case FD_CLOSE:
        if (WizardNetRelaySocketIsMember(socket)) {
            QueueWizardNetRelayLeaveForGame(state.relay_game_id);
            state.relay_game_id = 0;
        } else {
            CloseLegacySocketRecord(socket);
        }
        state.game_socket = INVALID_SOCKET;
        if (state.join_phase != FreeServerJoinPhase::Idle) {
            state.join_phase = FreeServerJoinPhase::Idle;
            show_startup_status(state, 52,
                "Connection failed - game already started or disconnected.",
                kFreeCloseRed);
        }
        break;
    default:
        break;
    }
}

void DispatchFreeServerNetworkMessage(FreeServerLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        show_startup_status(state, 5, "Disconnected from the server.",
            RGB(10, 10, 250));
        if (state.async_tcp_socket != nullptr) {
            CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
        }
        close_lobby(state, true);
        return;
    }
    if (event == FD_WRITE) {
        FlushWizardNetRelayAsyncSendQueue();
        return;
    }
    if (event != 1) {
        return;
    }

    if (state.async_tcp_socket != nullptr) {
        ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
    }
    if (state.async_tcp_socket == nullptr) {
        return;
    }

    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= 0x0d) {
        const auto available = static_cast<std::size_t>(byte_count);
        const u32 packet_bytes = read_u32(payload, available, 8);
        if (packet_bytes < 0x0d || packet_bytes > available) {
            break;
        }
        if (state.callbacks.handle_server_payload != nullptr) {
            state.callbacks.handle_server_payload(state, payload,
                static_cast<i32>(packet_bytes));
        }

        const u32 opcode = read_u32(payload, packet_bytes, 4);
        switch (opcode) {
        case 0x1c: {
            const u32 status = read_u32(payload, packet_bytes, 0x0d);
            show_startup_status(state, status == 0 ? 53 : 54,
                status == 0 ?
                    "Connection failed - game already started or no response." :
                    "Connection failed - selected game is full.",
                kFreeSoftWhite);
            break;
        }
        case kWizardNetRelayJoinStatusOpcode: {
            const u32 status = read_u32(payload, packet_bytes, 0x0d);
            const u32 game_id = read_u32(payload, packet_bytes, 0x11);
            const u32 member_id = read_u32(payload, packet_bytes, 0x15);
            const bool waiting_for_this_relay_game =
                state.game_socket == WizardNetRelaySocketForMember(1) &&
                state.relay_game_id != 0 &&
                state.relay_game_id == game_id;
            if (!waiting_for_this_relay_game) {
                append_startup_log(
                    "wizardnet relay join status ignored status=%lu game=%lu "
                    "member=%lu waiting_game=%lu socket=%llu",
                    static_cast<unsigned long>(status),
                    static_cast<unsigned long>(game_id),
                    static_cast<unsigned long>(member_id),
                    static_cast<unsigned long>(state.relay_game_id),
                    static_cast<unsigned long long>(state.game_socket));
                break;
            }
            if (status == 0 && game_id != 0 && member_id != 0) {
                const void* relay_secret =
                    packet_bytes >= 0x19 + kWizardNetRelaySecretBytes ?
                        payload + 0x19 : nullptr;
                const u32 relay_secret_bytes =
                    relay_secret != nullptr ? kWizardNetRelaySecretBytes : 0;
                ConfigureWizardNetRelayState(game_id, member_id, false,
                    relay_secret, relay_secret_bytes);
                state.game_socket = WizardNetRelaySocketForMember(1);
                state.relay_game_id = game_id;
                state.join_phase = FreeServerJoinPhase::WaitingForStart;
                append_startup_log(
                    "wizardnet relay browser join accepted game=%lu member=%lu",
                    static_cast<unsigned long>(game_id),
                    static_cast<unsigned long>(member_id));
                show_startup_status(state, 36,
                    "Getting connected player information.", kFreeStatusCyan);
            } else {
                state.join_phase = FreeServerJoinPhase::Idle;
                state.game_socket = INVALID_SOCKET;
                state.relay_game_id = 0;
                resume_free_server_game_list_after_relay_join(state);
                append_startup_log(
                    "wizardnet relay browser join rejected status=%lu game=%lu member=%lu",
                    static_cast<unsigned long>(status),
                    static_cast<unsigned long>(game_id),
                    static_cast<unsigned long>(member_id));
                show_startup_status(state, status == 2 ? 45 : 53,
                    status == 2 ?
                        "Connection failed - selected game is full." :
                        "Connection failed - game already started or no response.",
                    kFreeErrorRed);
            }
            break;
        }
        case kWizardNetRelayFrameOpcode: {
            if (packet_bytes < 0x19) {
                break;
            }
            const u32 game_id = read_u32(payload, packet_bytes, 0x0d);
            const u32 from_member = read_u32(payload, packet_bytes, 0x11);
            const u32 stream_id = read_u32(payload, packet_bytes, 0x15);
            const u8* frame_payload = payload + 0x19;
            const u32 frame_bytes = packet_bytes - 0x19;
            if (!WizardNetRelayReadyForGame(game_id)) {
                append_startup_log(
                    "wizardnet relay browser frame ignored active_game=%lu "
                    "packet_game=%lu member=%lu stream=%lu bytes=%lu",
                    static_cast<unsigned long>(wizardnet_relay_state().game_id),
                    static_cast<unsigned long>(game_id),
                    static_cast<unsigned long>(from_member),
                    static_cast<unsigned long>(stream_id),
                    static_cast<unsigned long>(frame_bytes));
                break;
            }
            std::vector<u8> decoded;
            if (!DecodeWizardNetRelayPayload(game_id, frame_payload,
                    frame_bytes, decoded)) {
                append_startup_log(
                    "wizardnet relay browser crypto rejected game=%lu "
                    "member=%lu stream=%lu bytes=%lu",
                    static_cast<unsigned long>(game_id),
                    static_cast<unsigned long>(from_member),
                    static_cast<unsigned long>(stream_id),
                    static_cast<unsigned long>(frame_bytes));
                break;
            }
            if (stream_id == kWizardNetRelayStreamLink) {
                RecordWizardNetRelayLinkFrameReceived(game_id, from_member,
                    static_cast<u32>(decoded.size()), "browser");
                post_free_server_socket_payload(state,
                    WizardNetRelaySocketForMember(from_member),
                    decoded.data(), static_cast<u32>(decoded.size()));
            } else if (stream_id == kWizardNetRelayStreamMode1) {
                EnqueueWizardNetRelayMode1Payload(decoded.data(),
                    static_cast<u32>(decoded.size()), from_member);
            }
            break;
        }
        case kWizardNetRelayMemberLeftOpcode: {
            if (packet_bytes < 0x15) {
                break;
            }
            const u32 game_id = read_u32(payload, packet_bytes, 0x0d);
            const u32 member_id = read_u32(payload, packet_bytes, 0x11);
            if (WizardNetRelayReadyForGame(game_id) && member_id == 1) {
                append_startup_log(
                    "wizardnet relay browser host-left game=%lu member=%lu",
                    static_cast<unsigned long>(game_id),
                    static_cast<unsigned long>(member_id));
                ResetWizardNetRelayState();
                state.join_phase = FreeServerJoinPhase::Idle;
                state.game_socket = INVALID_SOCKET;
                state.relay_game_id = 0;
                resume_free_server_game_list_after_relay_join(state);
                show_startup_status(state, 52,
                    "Connection failed - game already started or disconnected.",
                    kFreeCloseRed);
            }
            break;
        }
        case 0x1e:
        case 0x27: {
            if (packet_bytes < 0x395) {
                break;
            }
            const u32 game_id = read_u32(payload, packet_bytes, 0xb1);
            const char* game_name =
                reinterpret_cast<const char*>(payload + 0x0d);
            // The final paged response is a full-size empty sentinel. It is
            // not a game and must not become a selectable fallback "Game".
            if (game_name[0] != '\0') {
                AddFreeServerLobbyEntry(state, game_name, payload + 0x8d,
                    payload + 0x9d, game_id, payload + 0xb9);
                auto game = std::find_if(state.games.begin(), state.games.end(),
                    [game_id](const FreeServerGameEntry& entry) {
                        return entry.id == static_cast<int>(game_id);
                    });
                if (game != state.games.end()) {
                    game->password_required =
                        read_u32(payload, packet_bytes, 0xb5) != 0;
                }
            }
            if (opcode == 0x1e) {
                const i32 next_index = static_cast<i32>(
                    read_u32(payload, packet_bytes, 0xad));
                if (next_index >= 0) {
                    std::vector<u8> next_packet(0x11, 0);
                    write_le32(next_packet, 0, 3);
                    write_le32(next_packet, 4, 0x1d);
                    write_le32(next_packet, 8, 0x11);
                    write_le32(next_packet, 0x0d,
                        static_cast<u32>(next_index + 1));
                    queue_server_packet(state, next_packet.data(),
                        static_cast<i32>(next_packet.size()));
                }
            }
            break;
        }
        case 0x26:
            RemoveFreeServerLobbyEntryById(state,
                static_cast<int>(read_u32(payload, packet_bytes, 0x0d)));
            break;
        case 0x3e:
            state.server_top_bottom_counts[0] = read_u32(payload, packet_bytes, 0x0d);
            state.server_top_bottom_counts[1] = read_u32(payload, packet_bytes, 0x11);
            state.server_top_bottom_counts[2] = read_u32(payload, packet_bytes, 0x15);
            break;
        case 0x46:
            break;
        case 100:
            state.server_use_map_counts[0] = read_u32(payload, packet_bytes, 0x15);
            state.server_use_map_counts[1] = read_u32(payload, packet_bytes, 0x19);
            state.server_use_map_counts[2] = read_u32(payload, packet_bytes, 0x1d);
            state.server_use_map_counts[3] = read_u32(payload, packet_bytes, 0x0d);
            state.server_use_map_counts[4] = read_u32(payload, packet_bytes, 0x11);
            state.server_use_map_counts[5] = read_u32(payload, packet_bytes, 0x29);
            state.server_use_map_counts[6] = read_u32(payload, packet_bytes, 0x2d);
            state.server_use_map_counts[7] = read_u32(payload, packet_bytes, 0x31);
            state.server_use_map_counts[8] = read_u32(payload, packet_bytes, 0x21);
            state.server_use_map_counts[9] = read_u32(payload, packet_bytes, 0x25);
            break;
        default:
            if (WizardNetRelayCanDiscardStaleAsyncOpcode(opcode)) {
                append_startup_log(
                    "free server discarded stale async opcode=0x%02lx bytes=%lu",
                    static_cast<unsigned long>(opcode),
                    static_cast<unsigned long>(packet_bytes));
                schedule_free_server_async_drain(state);
                break;
            }
            append_startup_log(
                "free server forwarded unknown opcode=0x%02lx bytes=%lu to parent",
                static_cast<unsigned long>(opcode),
                static_cast<unsigned long>(packet_bytes));
            if (state.parent_window != nullptr && IsWindow(state.parent_window)) {
                PostMessageA(state.parent_window, message, wparam, lparam);
            }
            return;
        }

        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
            static_cast<i32>(packet_bytes));
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
    (void)hwnd;
}

} // namespace ranker

#endif
