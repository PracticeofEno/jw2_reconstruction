#include "ranker_p2p_lobby.h"

#ifdef _WIN32

#include "ranker_directplay.h"
#include "ranker_gameplay_sound.h"
#include "ranker_game_session_tables.h"
#include "ranker_frontend_layout.h"
#include "ranker_link_lobby.h"
#include "ranker_miles.h"
#include "ranker_network.h"
#include "ranker_online_dialogs.h"
#include "ranker_reliable_packets.h"
#include "ranker_replay.h"
#include "ranker_system_ui.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <winsock.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = WS_POPUP;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kChildEditStyle = WS_CHILD;
constexpr DWORD kChildEditAutoHScrollStyle = WS_CHILD | ES_AUTOHSCROLL;
constexpr COLORREF kP2PWhite = RGB(255, 255, 255);
constexpr COLORREF kP2PBlack = RGB(0, 0, 0);
constexpr COLORREF kP2PStartFailureRed = RGB(255, 20, 20);
constexpr COLORREF kP2PNetworkFailureRed = RGB(250, 10, 10);
constexpr COLORREF kP2PNetworkWaitYellow = RGB(250, 250, 10);
constexpr COLORREF kP2PDisconnectRed = RGB(250, 20, 20);
constexpr HRESULT kP2PConnectStartFailed = static_cast<HRESULT>(0x887700aa);
constexpr std::size_t kStartupP2PClockFormatRow = 193;
constexpr std::size_t kStartupP2PRouteStateLabelRowBase = 211;

HFONT p2p_lobby_ui_font(const P2PLobbyState& state) {
    if (state.ui_font != nullptr) {
        return state.ui_font;
    }
    HFONT font = GetUiFontHandle(1);
    return font != nullptr ? font :
        reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

int scaled_p2p_lobby_x(int legacy_value) {
    const FrontendLayoutPoint target = FrontendLayoutTargetSize();
    return ScaleFrontendLayoutValue(legacy_value,
        kLegacyFrontendLayoutWidth, target.x);
}

int scaled_p2p_lobby_y(int legacy_value) {
    const FrontendLayoutPoint target = FrontendLayoutTargetSize();
    return ScaleFrontendLayoutValue(legacy_value,
        kLegacyFrontendLayoutHeight, target.y);
}

P2PLobbyState g_p2p_lobby_state;
P2PNetworkLaunchParameters g_p2p_network_launch_parameters;
bool g_background_shutdown_registered = false;
std::array<bool, 4> g_image_button_shutdown_registered{};

const char* kP2PTribeNames[] = {
    "Primitive",
    "Elf",
    "Tyrano",
    "Demon",
    "Random",
    "Observer",
};

const char* kP2PWinResultNames[] = {
    "win",
    "lose",
    "draw",
    "dissconnect",
};

const char* kP2PGameEndReasonNames[] = {
    "no error",
    "no game play, i'm abort",
    "game canceled by host",
    "map file error",
    "connect cancel/fail - i'm client",
    "network error",
};

constexpr const char* kReplayVposHeader = "Jwar2 Replay Vpos File.";

LRESULT CALLBACK p2p_lobby_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleP2PLobbyWindowMessage(g_p2p_lobby_state, hwnd, message, wparam,
        lparam);
}

LRESULT CALLBACK p2p_lobby_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleP2PLobbyControlMessage(g_p2p_lobby_state, hwnd, message, wparam,
        lparam);
}

void shutdown_global_background() {
    ShutdownP2PLobbyBackgroundBitmap(g_p2p_lobby_state);
}

void register_image_button_shutdown(std::size_t slot, void (*callback)()) {
    if (slot >= g_image_button_shutdown_registered.size() ||
        g_image_button_shutdown_registered[slot]) {
        return;
    }
    std::atexit(callback);
    g_image_button_shutdown_registered[slot] = true;
}

std::vector<P2PLobbyLayoutRect> read_p2p_layout_record() {
    FrontendLayoutRectTable table{};
    if (!LoadFrontendLayoutFromJw219TrcRecord(table, kP2PLobbyLayoutTrcRecord)) {
        return {};
    }
    const std::vector<FrontendLayoutRect> source =
        CopyFrontendLayoutRectTable(table);
    ReleaseFrontendLayoutRectTable(table);

    std::vector<P2PLobbyLayoutRect> result;
    result.reserve(source.size());
    for (const FrontendLayoutRect& rect : source) {
        result.push_back({rect.x, rect.y, rect.width, rect.height});
    }
    return result;
}

P2PLobbyLayoutRect layout_at(const std::vector<P2PLobbyLayoutRect>& layout,
    std::size_t index) {
    if (index < layout.size()) {
        return layout[index];
    }
    return P2PLobbyLayoutRect{};
}

void copy_limited(char* target, std::size_t target_size, const char* source) {
    if (target == nullptr || target_size == 0) {
        return;
    }
    if (source == target) {
        // StartP2PLobbyJoinAttempt receives the state's own player-name and
        // remote-address buffers on the interactive Join path.  Clearing the
        // destination first would erase the source before the TCP callback can
        // use it.
        target[target_size - 1] = '\0';
        return;
    }
    target[0] = '\0';
    if (source != nullptr) {
        std::strncpy(target, source, target_size - 1);
        target[target_size - 1] = '\0';
    }
}

void copy_limited_segment(char* target, std::size_t target_size, const char* source,
    std::size_t source_size) {
    if (target == nullptr || target_size == 0) {
        return;
    }
    target[0] = '\0';
    if (source == nullptr) {
        return;
    }

    const std::size_t count = std::min(target_size - 1, source_size);
    std::memcpy(target, source, count);
    target[count] = '\0';
}

char ascii_upper(char value) {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - ('a' - 'A'));
    }
    return value;
}

std::string uppercase_ascii_copy(const char* value) {
    std::string result = value == nullptr ? "" : value;
    for (char& ch : result) {
        ch = ascii_upper(ch);
    }
    return result;
}

void copy_token_value_until_space(char* target, std::size_t target_size,
    const char* original_command_line, const char* uppercase_base,
    const char* uppercase_value) {
    const char* end = std::strstr(uppercase_value, " ");
    const std::size_t offset = static_cast<std::size_t>(uppercase_value - uppercase_base);
    const char* source = original_command_line + offset;
    if (end == nullptr) {
        copy_limited(target, target_size, source);
        return;
    }

    copy_limited_segment(target, target_size, source,
        static_cast<std::size_t>(end - uppercase_value));
}

void copy_command_line_value(char* target, std::size_t target_size,
    const char* original_command_line, const char* uppercase_base,
    const char* uppercase_value) {
    const std::size_t offset = static_cast<std::size_t>(uppercase_value - uppercase_base);
    const char* source = original_command_line + offset;
    if (*source == '"' || *source == '\'') {
        const char quote = *source++;
        const char* end = std::strchr(source, quote);
        if (end == nullptr) {
            copy_limited(target, target_size, source);
            return;
        }
        copy_limited_segment(target, target_size, source,
            static_cast<std::size_t>(end - source));
        return;
    }

    const char* end = source;
    while (*end != '\0' && *end != ' ' && *end != '\t' &&
           *end != '\r' && *end != '\n') {
        ++end;
    }
    copy_limited_segment(target, target_size, source,
        static_cast<std::size_t>(end - source));
}

const char* bounded_table_name(const char* const* names, std::size_t count,
    u32 index) {
    return index < count ? names[index] : "";
}

std::string replace_legacy_extension(const std::string& path, const char* extension) {
    const std::size_t slash = path.find_last_of("\\/");
    const std::size_t dot = path.find_last_of('.');
    const bool has_extension = dot != std::string::npos &&
        (slash == std::string::npos || slash < dot);
    if (has_extension) {
        return path.substr(0, dot) + extension;
    }
    return path + extension;
}

u32 read_payload_u32(const std::vector<u8>& payload, std::size_t offset) {
    if (offset > payload.size() || payload.size() - offset < sizeof(u32)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, payload.data() + offset, sizeof(value));
    return value;
}

u32 read_packet_u32(const void* payload, std::size_t byte_count,
    std::size_t offset) {
    if (payload == nullptr || offset > byte_count ||
        byte_count - offset < sizeof(u32)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(payload) + offset, sizeof(value));
    return value;
}

u32 p2p_payload_declared_size(const void* payload) {
    if (payload == nullptr) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(payload) + 8, sizeof(value));
    return value;
}

bool post_copied_p2p_payload(P2PLobbyState& state, WPARAM sender,
    const void* payload, std::size_t byte_count) {
    if (state.window == nullptr || payload == nullptr || byte_count == 0) {
        return false;
    }

    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byte_count);
    void* copy = global != nullptr ? GlobalLock(global) : nullptr;
    if (copy == nullptr) {
        if (global != nullptr) {
            GlobalFree(global);
        }
        return false;
    }
    std::memcpy(copy, payload, byte_count);
    if (PostMessageA(state.window, kP2PLobbyNetworkPayloadMessage, sender,
            reinterpret_cast<LPARAM>(copy)) != 0) {
        return true;
    }
    GlobalUnlock(global);
    GlobalFree(global);
    return false;
}

void free_posted_p2p_payload(LPARAM payload) {
    if (payload == 0) {
        return;
    }
    HGLOBAL global = GlobalHandle(reinterpret_cast<LPCVOID>(payload));
    if (global == nullptr) {
        return;
    }
    GlobalUnlock(global);
    GlobalFree(global);
}

void end_active_p2p_modal_prompt() {
    EndOnlineModalPrompt(online_modal_prompt_state(), 0);
    OnlineModelessPromptState& modeless = online_modeless_prompt_state();
    if (modeless.window != nullptr && IsWindow(modeless.window)) {
        DestroyWindow(modeless.window);
    }
}

bool active_transport_uses_legacy_tcp() {
    const i32 mode = async_com_state().active_network_transport_mode;
    return mode >= 0 && mode < 3;
}

bool hydrate_replay_delayed_packets(P2PGameSessionStartState& state,
    const ReplayRecordingState& recording) {
    state.delayed_packets.clear();
    state.network_index_by_owner.fill(0);
    state.replay_target_frame_count = 0;

    if (!recording.playback_mode ||
        recording.playback_payload.size() < kReplayHeaderBytes) {
        return false;
    }

    const std::vector<u8>& payload = recording.playback_payload;
    const std::size_t packet_count =
        (payload.size() - kReplayHeaderBytes) / kReplayPacketBytes;
    state.delayed_packets.reserve(packet_count);

    for (std::size_t index = 0; index < packet_count; ++index) {
        const std::size_t offset = kReplayHeaderBytes + index * kReplayPacketBytes;
        P2PDelayedGameplayPacket packet{};
        std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(offset),
            kReplayPacketBytes, packet.bytes.begin());
        packet.due_frame = read_payload_u32(payload, offset + 0x04);
        packet.serial = read_payload_u32(payload, offset + 0x08);
        packet.owner_slot = payload[offset + 0x0c];
        packet.subtype = payload[offset + 0x0f];
        packet.flags1 = read_payload_u32(payload, offset + 0x1c);
        packet.flags2 = read_payload_u32(payload, offset + 0x20);
        state.delayed_packets.push_back(packet);
    }

    if (!state.delayed_packets.empty()) {
        state.replay_target_frame_count = state.delayed_packets.back().due_frame;
    } else {
        state.replay_target_frame_count = recording.playback_last_frame_tick;
    }

    for (u32 owner = 0; owner < state.network_index_by_owner.size(); ++owner) {
        u32 cursor = 0;
        while (cursor < state.delayed_packets.size() &&
               state.delayed_packets[cursor].owner_slot != owner) {
            ++cursor;
        }
        state.network_index_by_owner[owner] = cursor;
    }
    return true;
}

bool load_replay_vpos_metadata(P2PGameSessionStartState& state) {
    state.replay_vpos_loaded = false;
    state.replay_vpos_count = 0;
    state.replay_vpos_cursor = 0xffffffffu;
    state.replay_vpos_seed.fill(0);
    state.replay_vpos_records.clear();

    if (state.replay_vpos_path.empty()) {
        return false;
    }

    std::ifstream file(state.replay_vpos_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::array<char, 0x40> header{};
    file.read(header.data(), header.size());
    if (file.gcount() != static_cast<std::streamsize>(header.size()) ||
        std::strncmp(header.data(), kReplayVposHeader, std::strlen(kReplayVposHeader)) != 0) {
        return false;
    }

    for (;;) {
        P2PReplayVposRecord record{};
        file.read(reinterpret_cast<char*>(&record.frame), sizeof(record.frame));
        if (file.gcount() == 0) {
            break;
        }
        if (file.gcount() != static_cast<std::streamsize>(sizeof(record.frame))) {
            break;
        }
        file.read(reinterpret_cast<char*>(&record.camera_x), sizeof(record.camera_x));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(record.camera_x))) {
            break;
        }
        file.read(reinterpret_cast<char*>(&record.camera_y), sizeof(record.camera_y));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(record.camera_y))) {
            break;
        }
        state.replay_vpos_records.push_back(record);
    }

    if (!state.replay_vpos_records.empty()) {
        const P2PReplayVposRecord& first = state.replay_vpos_records.front();
        std::memcpy(state.replay_vpos_seed.data(), &first.frame, sizeof(first.frame));
        std::memcpy(state.replay_vpos_seed.data() + sizeof(first.frame),
            &first.camera_x, sizeof(first.camera_x));
        std::memcpy(state.replay_vpos_seed.data() + sizeof(first.frame) +
                sizeof(first.camera_x),
            &first.camera_y, sizeof(first.camera_y));
    }
    state.replay_vpos_loaded = true;
    state.replay_vpos_cursor = 0;
    state.replay_vpos_count = static_cast<u32>(state.replay_vpos_records.size());
    return true;
}

void show_lobby_message(P2PLobbyState& state, const char* text, COLORREF color) {
    state.last_status_text = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_status_text.c_str(), color);
    }
}

void show_startup_lobby_message(P2PLobbyState& state, std::size_t index,
    const char* fallback, COLORREF color) {
    show_lobby_message(state, startup_message_row(index, fallback), color);
}

const char* p2p_tribe_name(std::size_t index) {
    return bounded_table_name(kP2PTribeNames, std::size(kP2PTribeNames), index);
}

std::string format_p2p_version_mismatch(u32 remote_version) {
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

std::string format_startup_lobby_number(std::size_t index,
    const char* fallback, int value) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), startup_message_row(index, fallback),
        value);
    return buffer;
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

void set_control_proc(HWND window) {
    if (window != nullptr) {
        SetWindowLongPtrA(window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(p2p_lobby_control_proc));
    }
}

HWND create_edit(HWND parent, HINSTANCE instance, int id, const P2PLobbyLayoutRect& rect,
    DWORD style) {
    return CreateWindowExA(0, "edit", nullptr, style, rect.x, rect.y, rect.width,
        rect.height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance, nullptr);
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const P2PLobbyLayoutRect& rect,
    u32 normal_record, u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    HWND window = GetLegacyImageButtonWindow(button);
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(window, GWLP_WNDPROC));
    set_control_proc(window);
    return true;
}

LegacyImageButtonControl* button_for_id(P2PLobbyState& state, int id) {
    switch (id) {
    case kP2PLobbyInfoControlId:
        return &state.info_control;
    case kP2PLobbyHostButtonId:
        return &state.host_button;
    case kP2PLobbyJoinButtonId:
        return &state.join_button;
    case kP2PLobbyCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

void release_lobby_resources(P2PLobbyState& state) {
    if (state.join_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.join_timer);
        state.join_timer = 0;
    }
    ReleaseBitmapMemoryResource(state.background);
    state.instruction_text.clear();
    state.instruction_text.shrink_to_fit();
    DestroyLegacyImageButtonControl(state.info_control);
    DestroyLegacyImageButtonControl(state.cancel_button);
    DestroyLegacyImageButtonControl(state.host_button);
    DestroyLegacyImageButtonControl(state.join_button);
    if (state.ui_font != nullptr) {
        DeleteObject(state.ui_font);
        state.ui_font = nullptr;
    }
    state.name_edit = nullptr;
    state.local_address_edit = nullptr;
    state.remote_address_edit = nullptr;
    state.window = nullptr;
}

void handle_host_command(P2PLobbyState& state, HWND hwnd) {
    HandleDefaultFrontendUiClickSound();
    read_control_text(state.name_edit, state.player_name.data(),
        static_cast<int>(state.player_name.size()));
    read_control_text(state.local_address_edit, state.local_address.data(),
        static_cast<int>(state.local_address.size()));
    if (state.callbacks.start_host != nullptr) {
        state.callbacks.start_host(state);
    }
    else {
        state.start_game_requested = true;
    }
    DestroyWindow(hwnd);
    if (state.callbacks.return_to_parent != nullptr) {
        state.callbacks.return_to_parent(state.parent_window, state.instance,
            state.return_context);
    }
}

void cancel_p2p_connection(P2PLobbyState& state) {
    state.join_pending = false;
    state.join_socket = INVALID_SOCKET;
    ShutdownLegacyUdpNetworking();
}

void handle_cancel_command(P2PLobbyState& state, HWND hwnd) {
    HandleDefaultFrontendUiClickSound();
    if (state.callbacks.cancel_connection != nullptr) {
        state.callbacks.cancel_connection(state);
    }
    else {
        cancel_p2p_connection(state);
    }
    DestroyWindow(hwnd);
    if (state.callbacks.return_to_parent != nullptr) {
        state.callbacks.return_to_parent(state.parent_window, state.instance,
            state.return_context);
    }
}

void handle_join_command(P2PLobbyState& state) {
    HandleDefaultFrontendUiClickSound();
    read_control_text(state.name_edit, state.player_name.data(),
        static_cast<int>(state.player_name.size()));
    read_control_text(state.remote_address_edit, state.remote_address.data(),
        static_cast<int>(state.remote_address.size()));
    if (state.remote_address[0] == '\0') {
        show_startup_lobby_message(state, 92,
            "Enter the IP address of the game to connect to.",
            RGB(250, 250, 250));
        return;
    }

    HRESULT result = StartP2PLobbyJoinAttempt(state, state.player_name.data(),
        state.remote_address.data(), state.default_tcp_port);
    if (FAILED(result)) {
        show_startup_lobby_message(state, 34,
            "A TCP/IP initialization error occurred.", RGB(250, 250, 250));
    }
}

WNDPROC original_proc_for_id(P2PLobbyState& state, int id) {
    switch (id) {
    case kP2PLobbyInfoControlId:
        return state.info_control.original_window_proc;
    case kP2PLobbyHostButtonId:
        return state.host_button.original_window_proc;
    case kP2PLobbyJoinButtonId:
        return state.join_button.original_window_proc;
    case kP2PLobbyCancelButtonId:
        return state.cancel_button.original_window_proc;
    case kP2PLobbyNameEditId:
        return state.original_name_edit_proc;
    case kP2PLobbyLocalAddressEditId:
        return state.original_local_address_proc;
    case kP2PLobbyRemoteAddressEditId:
        return state.original_remote_address_proc;
    default:
        return nullptr;
    }
}

} // namespace

P2PLobbyState& p2p_lobby_state() {
    return g_p2p_lobby_state;
}

P2PNetworkLaunchParameters& p2p_network_launch_parameters() {
    return g_p2p_network_launch_parameters;
}

void ResetP2PNetworkLaunchParameters(P2PNetworkLaunchParameters& parameters) {
    parameters.player_name.fill('\0');
    parameters.password.fill('\0');
    parameters.map_path.fill('\0');
    parameters.remote_address.fill('\0');
    parameters.self_play_commander = false;
    parameters.self_play_teacher = false;
    parameters.self_play_teacher2 = false;
    parameters.self_play_dagger = false;
    parameters.self_play_deterministic = false;
    parameters.self_play_autoscout = true;
    parameters.self_play_no_sleep = false;
    parameters.self_play_curriculum = 2;
    parameters.self_play_teacher_variant = 0;
    parameters.self_play_teacher_variant2 = 0;
    parameters.self_play_weights.fill('\0');
    parameters.self_play_weights2.fill('\0');
    parameters.self_play_rollout.fill('\0');
    parameters.uses_map_file = false;
    parameters.valid = false;
}

bool ParseP2PNetworkCommandLine(P2PNetworkLaunchParameters& parameters,
    const char* command_line) {
    ResetP2PNetworkLaunchParameters(parameters);
    if (command_line == nullptr) {
        return false;
    }

    const std::string uppercase = uppercase_ascii_copy(command_line);
    const char* upper = uppercase.c_str();

    // Local AI self-play autostart.  This deliberately bypasses the -NET/-ID
    // requirements and the whitespace-truncating -MAP parser: the MVP is fixed
    // to a single map whose path contains spaces, so it is resolved internally
    // instead of read from the command line.  The command-line host flow then
    // fills slot 1 with Computer(AI) and auto-starts the lobby.
    if (std::strstr(upper, "-AISELF") != nullptr ||
        std::strstr(upper, "-AI1V1") != nullptr) {
        parameters.self_play = true;
        parameters.self_play_1v1 = std::strstr(upper, "-AI1V1") != nullptr;
        parameters.self_play_draw = std::strstr(upper, "-AIDRAW") != nullptr;
        // Whole-token match: "-AITEACHER" must not be satisfied by the
        // prefix of "-AITEACHERVAR:" or "-AITEACHER2".
        const auto has_token = [&](const char* flag) {
            const std::size_t length = std::strlen(flag);
            for (const char* found = std::strstr(upper, flag); found != nullptr;
                 found = std::strstr(found + 1, flag)) {
                const char next = found[length];
                if (next == '\0' || next == ' ' || next == '\t' || next == '"') return true;
            }
            return false;
        };
        parameters.self_play_teacher = has_token("-AITEACHER");
        // -AITEACHER2: under -AIVS only the second policy owner is a rule
        // commander (policy-vs-teacher-variant games for curricula/leagues).
        parameters.self_play_teacher2 = has_token("-AITEACHER2");
        parameters.self_play_dagger = has_token("-AIDAGGER");
        parameters.self_play_deterministic =
            std::strstr(upper, "-AIDETERMINISTIC") != nullptr;
        // Self-play only: headless instances may skip the per-frame Sleep(1).
        parameters.self_play_no_sleep = std::strstr(upper, "-AINOSLEEP") != nullptr;
        // Quoted paths work as well as whitespace-free tokens. Always copy
        // from the original command line to preserve filenames.
        const auto commander_path = [&](const char* flag, auto& destination) {
            const char* found = std::strstr(upper, flag);
            if (found == nullptr) return true;
            const char* source = command_line + (found - upper) + std::strlen(flag);
            const char* flag_start = command_line + (found - upper);
            const bool quoted = *source == '"' ||
                (flag_start > command_line && flag_start[-1] == '"');
            if (*source == '"') ++source;
            std::size_t length = 0;
            while (source[length] != '\0' &&
                   (quoted ? source[length] != '"' :
                       source[length] != ' ' && source[length] != '\t')) {
                if (length + 1 >= destination.size()) return false;
                destination[length] = source[length];
                ++length;
            }
            destination[length] = '\0';
            return length != 0 && (!quoted || source[length] == '"');
        };
        if (!commander_path("-AIWEIGHTS:", parameters.self_play_weights) ||
            !commander_path("-AIWEIGHTS2:", parameters.self_play_weights2) ||
            !commander_path("-AIROLLOUT:", parameters.self_play_rollout)) return false;
        parameters.self_play_commander = parameters.self_play_teacher ||
            parameters.self_play_teacher2 ||
            parameters.self_play_weights[0] != '\0' ||
            std::strstr(upper, "-AICOMMANDER") != nullptr;
        const char* curriculum = std::strstr(upper, "-AICURRICULUM:");
        if (curriculum != nullptr) parameters.self_play_curriculum =
            static_cast<u32>(std::strtoul(curriculum + std::strlen("-AICURRICULUM:"), nullptr, 10));
        const char* teacher_variant = std::strstr(upper, "-AITEACHERVAR:");
        if (teacher_variant != nullptr) parameters.self_play_teacher_variant =
            static_cast<u32>(std::strtoul(teacher_variant + std::strlen("-AITEACHERVAR:"), nullptr, 10));
        const char* teacher_variant2 = std::strstr(upper, "-AITEACHERVAR2:");
        if (teacher_variant2 != nullptr) parameters.self_play_teacher_variant2 =
            static_cast<u32>(std::strtoul(teacher_variant2 + std::strlen("-AITEACHERVAR2:"), nullptr, 10));
        parameters.uses_map_file = true;
        std::snprintf(parameters.map_path.data(), parameters.map_path.size(),
            "%s", "Maps\\Rank Maps\\(4) Python Jurassic v0.1.trk");
        std::snprintf(parameters.player_name.data(),
            parameters.player_name.size(), "%s", "AIHost");
        const char* max_frames = std::strstr(upper, "-MAXFRAMES:");
        if (max_frames != nullptr) {
            parameters.self_play_max_frames = static_cast<u32>(
                std::strtoul(max_frames + std::strlen("-MAXFRAMES:"), nullptr, 10));
        }
        parameters.self_play_random =
            std::strstr(upper, "-AIRANDOM") != nullptr;
        parameters.self_play_imitate =
            std::strstr(upper, "-AIIMITATE") != nullptr;
        parameters.self_play_shadow2 =
            std::strstr(upper, "-AISHADOW2") != nullptr;
        parameters.self_play_shadow =
            std::strstr(upper, "-AISHADOW") != nullptr &&
            !parameters.self_play_shadow2;
        const char* imitate_owner = std::strstr(upper, "-AIIMITOWNER:");
        parameters.self_play_imitate_owner = imitate_owner != nullptr ?
            static_cast<u32>(std::strtoul(
                imitate_owner + std::strlen("-AIIMITOWNER:"), nullptr, 10)) :
            0xffu;
        // -AIOUT:DIR — copy the whitespace-free directory token from the
        // ORIGINAL (case-preserving) command line, since paths are case- and
        // slash-sensitive.
        const char* out_upper = std::strstr(upper, "-AIOUT:");
        if (out_upper != nullptr && command_line != nullptr) {
            const char* out = std::strstr(command_line, "-AIOUT:");
            if (out == nullptr) {
                out = command_line +
                    (out_upper - upper);  // fallback: same offset
            }
            out += std::strlen("-AIOUT:");
            std::size_t i = 0;
            while (out[i] != '\0' && out[i] != ' ' && out[i] != '\t' &&
                   i + 1 < parameters.self_play_output_dir.size()) {
                parameters.self_play_output_dir[i] = out[i];
                ++i;
            }
            parameters.self_play_output_dir[i] = '\0';
        }
        if (parameters.self_play_commander &&
            !commander_path("-AIOUT:", parameters.self_play_output_dir)) return false;
        const char* entity = std::strstr(upper, "-AIENTITY:");
        parameters.self_play_entity_port = entity != nullptr ?
            static_cast<u16>(std::strtoul(entity + std::strlen("-AIENTITY:"),
                nullptr, 10)) : 0u;
        const char* act3 = std::strstr(upper, "-AIACT3:");
        parameters.self_play_act3_port = act3 != nullptr ?
            static_cast<u16>(std::strtoul(act3 + std::strlen("-AIACT3:"),
                nullptr, 10)) : 0u;
        const char* worker_floor = std::strstr(upper, "-AIWORKERFLOOR:");
        parameters.self_play_worker_floor = worker_floor != nullptr ?
            static_cast<u32>(std::strtoul(
                worker_floor + std::strlen("-AIWORKERFLOOR:"), nullptr, 10)) :
            0u;
        const char* opp_slow = std::strstr(upper, "-AIOPPSLOW:");
        parameters.self_play_opponent_slow = opp_slow != nullptr ?
            static_cast<u32>(std::strtoul(
                opp_slow + std::strlen("-AIOPPSLOW:"), nullptr, 10)) : 0u;
        parameters.self_play_reveal_base =
            std::strstr(upper, "-AIREVEALBASE") != nullptr;
        const char* ipc = std::strstr(upper, "-AIIPC:");
        parameters.self_play_ipc_port = ipc != nullptr ?
            static_cast<u16>(std::strtoul(ipc + std::strlen("-AIIPC:"),
                nullptr, 10)) : 0u;
        const char* net = std::strstr(upper, "-AINET:");
        parameters.self_play_net_offset = net != nullptr ?
            static_cast<u16>(std::strtoul(net + std::strlen("-AINET:"),
                nullptr, 10)) : 0u;
        // -AIREPLAY:PATH is case-preserving (paths), parsed like -AIOUT.
        const char* replay_upper = std::strstr(upper, "-AIREPLAY:");
        if (replay_upper != nullptr && command_line != nullptr) {
            const char* src = command_line + (replay_upper - upper) +
                std::strlen("-AIREPLAY:");
            std::size_t i = 0;
            while (src[i] != '\0' && src[i] != ' ' && src[i] != '\t' &&
                   i + 1 < parameters.self_play_replay_path.size()) {
                parameters.self_play_replay_path[i] = src[i];
                ++i;
            }
            parameters.self_play_replay_path[i] = '\0';
        }
        const char* tribe = std::strstr(upper, "-AITRIBE:");
        parameters.self_play_opponent_tribe = tribe != nullptr ?
            static_cast<u32>(std::strtoul(tribe + std::strlen("-AITRIBE:"),
                nullptr, 10)) : 2u;
        // v9 macro autopilot / base-defense reflex / event decision gate:
        // default ON, -AIAUTOPILOT:0 / -AIREFLEX:0 / -AIGATE:0 disable (the
        // A/B lever for measuring their effect).
        const auto parse_bool_flag = [&](const char* name, bool fallback) {
            const char* value = std::strstr(upper, name);
            if (value == nullptr) {
                return fallback;
            }
            return std::strtoul(value + std::strlen(name), nullptr, 10) != 0;
        };
        parameters.self_play_autopilot = parse_bool_flag("-AIAUTOPILOT:", true);
        parameters.self_play_reflex = parse_bool_flag("-AIREFLEX:", true);
        parameters.self_play_gate = parse_bool_flag("-AIGATE:", true);
        parameters.self_play_autoscout = parse_bool_flag("-AIAUTOSCOUT:", true);
        parameters.self_play_versus = std::strstr(upper, "-AIVS") != nullptr;
        if (parameters.self_play_commander &&
            (parameters.self_play_entity_port != 0 || parameters.self_play_act3_port != 0 ||
             parameters.self_play_ipc_port != 0 || parameters.self_play_reveal_base ||
             parameters.self_play_shadow || parameters.self_play_shadow2 ||
             parameters.self_play_imitate || parameters.self_play_replay_path[0] != '\0' ||
             parameters.self_play_curriculum > 4)) return false;
        if (parameters.self_play_weights2[0] != '\0' &&
            (!parameters.self_play_commander || !parameters.self_play_versus)) return false;
        if (parameters.self_play_teacher2 && !parameters.self_play_versus) return false;
        // The random-legal / IPC policies run inside the packet-controller path,
        // so they imply the scripted-owner handover too.
        parameters.self_play_scripted =
            parameters.self_play_commander ||
            parameters.self_play_1v1 ||
            parameters.self_play_random ||
            parameters.self_play_ipc_port != 0 ||
            std::strstr(upper, "-AISCRIPT") != nullptr;
        const char* seed = std::strstr(upper, "-SEED:");
        parameters.self_play_seed = seed != nullptr ?
            static_cast<u32>(std::strtoul(seed + std::strlen("-SEED:"),
                nullptr, 10)) : 0u;
        if (parameters.self_play_commander && parameters.self_play_seed == 0)
            parameters.self_play_seed = 1;
        if (parameters.self_play_commander && parameters.self_play_max_frames > 60000)
            return false;
        parameters.valid = true;
        return true;
    }

    if (std::strstr(upper, "-NET") == nullptr) {
        return false;
    }

    const char* id = std::strstr(upper, "-ID:");
    if (id == nullptr) {
        return false;
    }
    copy_command_line_value(parameters.player_name.data(),
        parameters.player_name.size(), command_line, upper, id + 4);

    const char* password = std::strstr(upper, "-PASS:");
    if (password != nullptr) {
        copy_command_line_value(parameters.password.data(),
            parameters.password.size(), command_line, upper, password + 6);
    }

    const char* map = std::strstr(upper, "-MAP:");
    if (map != nullptr) {
        parameters.uses_map_file = true;
        copy_command_line_value(parameters.map_path.data(), parameters.map_path.size(),
            command_line, upper, map + 5);
        parameters.valid = true;
        return true;
    }

    parameters.uses_map_file = false;
    const char* remote_address = std::strstr(upper, "-IP:");
    if (remote_address == nullptr) {
        return false;
    }
    copy_command_line_value(parameters.remote_address.data(),
        parameters.remote_address.size(), command_line, upper, remote_address + 4);
    parameters.valid = true;
    return true;
}

bool ParseP2PNetworkCommandLine(const char* command_line) {
    return ParseP2PNetworkCommandLine(g_p2p_network_launch_parameters, command_line);
}

bool WriteP2PGameResultFile(const P2PGameResultFileInput& input,
    P2PGameWinResult win_result, P2PGameEndReason end_reason, const char* path) {
    const char* output_path = path == nullptr ? "Result.txt" : path;
    DeleteFileA(output_path);

    std::ofstream out(output_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    const u32 reason = static_cast<u32>(end_reason);
    out << "\nTitle = Jw2 Ver " << (input.version_packed & 0xffffu)
        << "-" << ((input.version_packed >> 16) & 0xffu)
        << "-" << ((input.version_packed >> 24) & 0xffu) << "\n\n";
    out << "GameEnd = " << reason << " ("
        << bounded_table_name(kP2PGameEndReasonNames,
            std::size(kP2PGameEndReasonNames), reason)
        << ")\n";

    if (end_reason == P2PGameEndReason::NoError) {
        const std::size_t local_slot = std::min<std::size_t>(
            input.local_player_slot, input.players.size() - 1);
        const P2PGameResultPlayer& local_player = input.players[local_slot];
        out << "ID = " << local_player.result_name.data() << "\n";
        out << "Tribe = " << p2p_tribe_name(local_player.faction);
        if (local_player.selected_tribe < 4) {
            out << "\n";
        }
        else {
            out << ", " << p2p_tribe_name(local_player.selected_tribe) << "\n";
        }

        out << "Allies = ";
        u32 ally_mask = local_player.ally_mask;
        const std::size_t active_count = std::min<std::size_t>(
            input.active_player_count, input.players.size());
        for (std::size_t slot = 0; slot < active_count; ++slot) {
            if ((ally_mask & 1u) != 0 && input.players[slot].playing) {
                out << input.players[slot].result_name.data() << " ";
            }
            ally_mask >>= 1;
        }
        out << "\n";

        out << "Wins = " << bounded_table_name(kP2PWinResultNames,
            std::size(kP2PWinResultNames), static_cast<u32>(win_result)) << "\n";
        out << "\n";

        for (std::size_t slot = 0; slot < active_count; ++slot) {
            const P2PGameResultPlayer& player = input.players[slot];
            if (player.result_name[0] == '\0') {
                continue;
            }
            out << "Player " << slot << " = " << player.result_name.data();
            if (!player.playing || slot == local_slot) {
                out << " EndGame\n";
            }
            else {
                out << " Playing\n";
            }
        }
    }
    else {
        out << "ID = " << (input.client_player_name == nullptr ?
            "" : input.client_player_name) << "\n";
        out << "\n";

        const std::size_t connected_count = std::min<std::size_t>(
            input.connected_player_count, input.players.size());
        for (std::size_t slot = 0; slot < connected_count; ++slot) {
            const P2PGameResultPlayer& player = input.players[slot];
            if (player.playing) {
                out << "Player " << slot << " = "
                    << player.network_name.data() << "\n";
            }
        }
    }

    out << "\nEND DATA\n";
    return true;
}

bool PrepareP2PGameSessionStart(P2PGameSessionStartState& state,
    const P2PGameSessionStartInput& input,
    const P2PGameSessionStartCallbacks& callbacks) {
    ReplayRecordingState& replay = replay_recording_state();
    const bool replay_header_available =
        replay.playback_mode && replay.playback_payload.size() >= kReplayHeaderBytes;
    if (input.reference_tables != nullptr) {
        RebuildUnitTypeReverseReferenceTables(*input.reference_tables);
    }

    state.start_parameter_payload_present = input.start_parameter_payload_present;
    state.start_parameter_payload = input.start_parameter_payload;
    state.network_player_count = std::min<u32>(input.network_player_count,
        static_cast<u32>(input.players.size()));
    state.copied_runtime_local_player = input.copied_runtime_local_player;
    state.route_state = 4;
    state.setup_flags |= 0x0f;
    state.ai_profile_mode = input.ai_profile_mode;
    if (replay_header_available) {
        state.start_parameter_payload_present = true;
        state.start_parameter_payload.fill(0);
        const std::size_t metadata_offset = 0x63;
        const std::size_t metadata_bytes = std::min<std::size_t>(
            state.start_parameter_payload.size(),
            replay.playback_payload.size() - metadata_offset);
        std::copy_n(replay.playback_payload.begin() +
                static_cast<std::ptrdiff_t>(metadata_offset),
            metadata_bytes, state.start_parameter_payload.begin());
        state.copied_runtime_local_player = replay.playback_payload[0x5f];
        state.ai_profile_mode = replay.playback_payload[0x1b];
    }
    state.generic_ai_profile_mode = false;
    state.network_ai_profile_override = false;
    state.scenario_ai_profile_override =
        input.scenario_ai_profile_override || replay_header_available;
    if (state.ai_profile_mode == 1) {
        state.generic_ai_profile_mode = !replay_header_available;
        if (callbacks.apply_ai_profile_mode1 != nullptr) {
            callbacks.apply_ai_profile_mode1(callbacks.user_data);
        }
    }
    else if (state.ai_profile_mode == 2) {
        state.network_ai_profile_override = true;
        if (callbacks.apply_ai_profile_mode2 != nullptr) {
            callbacks.apply_ai_profile_mode2(callbacks.user_data);
        }
    }

    for (std::size_t slot = 0; slot < input.players.size(); ++slot) {
        copy_limited(state.player_names[slot].data(), state.player_names[slot].size(),
            input.players[slot].name.data());
    }
    if (replay_header_available) {
        for (std::size_t slot = 0; slot < state.player_names.size(); ++slot) {
            const std::size_t name_offset = 0x1fff +
                slot * kP2PGameSessionPlayerNameBytes;
            if (name_offset >= replay.playback_payload.size()) {
                break;
            }
            const std::size_t available_name_bytes =
                std::min<std::size_t>(kP2PGameSessionPlayerNameBytes,
                    replay.playback_payload.size() - name_offset);
            copy_limited_segment(state.player_names[slot].data(),
                state.player_names[slot].size(),
                reinterpret_cast<const char*>(replay.playback_payload.data() + name_offset),
                available_name_bytes);
        }
    }

    state.local_player_slot = 9;
    for (std::size_t slot = 0; slot < state.slot_flags.size(); ++slot) {
        if (slot != 8) {
            state.slot_flags[slot] |= 0x200;
        }
    }
    state.all_players_mask = 0xffffffffu;
    state.ready_bytes.fill(0);
    state.delayed_packet_serial_by_owner.fill(0);

    const bool replay_payload_loaded = hydrate_replay_delayed_packets(state, replay);
    if (!replay_payload_loaded) {
        state.delayed_packets.clear();
        state.replay_target_frame_count = 0;
        const u32 network_count = std::min<u32>(input.network_player_count,
            static_cast<u32>(input.players.size()));
        for (std::size_t owner = 0; owner < state.network_index_by_owner.size(); ++owner) {
            u32 index = 0;
            while (index < network_count && input.players[index].owner_slot != owner) {
                ++index;
            }
            state.network_index_by_owner[owner] = index;
        }
    }

    const u32 replay_game_version = LoadTrcRecord9Value();
    const u8 replay_reliable_mode = state.generic_ai_profile_mode ? 1u : 0u;
    const bool replay_forced_mode = state.network_ai_profile_override;
    const u8 replay_local_player =
        state.copied_runtime_local_player < kReplayChannelCount ?
        static_cast<u8>(state.copied_runtime_local_player) : 0;
    const std::vector<u8> replay_metadata(
        state.start_parameter_payload.begin(), state.start_parameter_payload.end());
    ResetMode1ReliableRuntimeAndReplayState(state.scenario_ai_profile_override,
        replay_game_version, replay_reliable_mode, replay_forced_mode,
        replay_local_player, replay_metadata);
    if (!replay.playback_mode) {
        replay.source_archive_path = input.map_path;
    }

    const std::string media_base_path =
        replay.playback_mode && !replay.playback_archive_path.empty() ?
        replay.playback_archive_path :
        input.map_path;
    state.replay_vpos_path = replace_legacy_extension(media_base_path, ".vpo");
    load_replay_vpos_metadata(state);

    state.direct_music_path = replace_legacy_extension(media_base_path, ".mp3");
    state.direct_music_paused = false;
    state.direct_music_volume_enabled = true;
    if (callbacks.play_direct_music != nullptr) {
        state.direct_music_started = callbacks.play_direct_music(
            state.direct_music_path.c_str(), callbacks.user_data);
    }
    else {
        state.direct_music_started = PlayDirectMilesMusic(state.direct_music_path.c_str());
    }
    if (state.direct_music_started) {
        if (callbacks.set_direct_music_volume != nullptr) {
            callbacks.set_direct_music_volume(100, callbacks.user_data);
        }
        else {
            SetDirectMilesMusicVolume(100);
        }
        state.direct_music_current_ms = callbacks.get_direct_music_current_ms != nullptr ?
            callbacks.get_direct_music_current_ms(callbacks.user_data) :
            GetDirectMilesMusicCurrentMs();
        state.direct_music_frame_offset = 0;
    }
    return true;
}

bool TickP2PDirectMusicSync(P2PGameSessionStartState& state, u32 gameplay_frame,
    u32 target_frame_count, u32 music_length_ms, u32 music_status) {
    const u32 denominator = state.direct_music_current_ms == 0 ?
        1u : static_cast<u32>(state.direct_music_current_ms);
    const u32 music_frame = (target_frame_count * music_length_ms) / denominator;
    if (gameplay_frame < music_frame) {
        ++state.direct_music_idle_counter;
        if (state.direct_music_idle_counter > 0xfa) {
            state.direct_music_idle_counter = 0;
            return false;
        }
        return true;
    }

    if ((music_status & 0xffu) == 0) {
        if (gameplay_frame < target_frame_count) {
            state.music_phase_toggle = !state.music_phase_toggle;
            return state.music_phase_toggle;
        }
        state.game_end_requested = true;
    }
    state.direct_music_idle_counter = 0;
    return true;
}

u8 GetBoundedP2PSetupValue(u8 value) {
    return value <= 3 ? value : 0;
}

std::string FormatP2PGameplayClockText(u32 gameplay_frame, u32 target_frame_count,
    const char* format) {
    char buffer[128]{};
    const char* fmt = format == nullptr ?
        startup_platform_row(kStartupP2PClockFormatRow, "%d:%d / %d:%d") :
        format;
    std::snprintf(buffer, sizeof(buffer), fmt,
        static_cast<int>(gameplay_frame / 0x528),
        static_cast<int>((static_cast<unsigned long long>(gameplay_frame) / 0x16) % 0x3c),
        static_cast<int>(target_frame_count / 0x528),
        static_cast<int>((static_cast<unsigned long long>(target_frame_count) / 0x16) % 0x3c));
    return buffer;
}

std::string FormatP2PRouteStateText(u32 route_state, const char* format,
    const char* const* labels, std::size_t label_count) {
    char buffer[128]{};
    const char* fmt = format == nullptr ? "%s" : format;
    const char* label = labels != nullptr && route_state < label_count ?
        labels[route_state] : nullptr;
    if (label == nullptr) {
        label = startup_platform_row(
            kStartupP2PRouteStateLabelRowBase + route_state, "");
    }
    std::snprintf(buffer, sizeof(buffer), fmt, label == nullptr ? "" : label);
    return buffer;
}

void TickP2PReplayVposCamera(P2PGameSessionStartState& state, u32 gameplay_frame) {
    if (!state.replay_vpos_loaded || state.replay_vpos_cursor >= state.replay_vpos_count) {
        return;
    }

    while (state.replay_vpos_cursor < state.replay_vpos_count) {
        const P2PReplayVposRecord& record =
            state.replay_vpos_records[state.replay_vpos_cursor];
        if (gameplay_frame < record.frame) {
            break;
        }

        if (state.apply_replay_vpos_camera) {
            state.camera_x = record.camera_x;
            state.camera_y = record.camera_y;
            state.replay_vpos_camera_dirty = true;
        }
        ++state.replay_vpos_cursor;
    }

    if (state.replay_vpos_cursor >= state.replay_vpos_count) {
        state.replay_vpos_loaded = false;
    }
}

u32 PumpP2PDelayedGameplayPackets(P2PGameSessionStartState& state, u32 gameplay_frame,
    P2PDelayedGameplayPacketCallback dispatch_packet, void* user_data) {
    bool all_owner_cursors_finished = true;

    for (u32 owner = 0; owner < state.network_index_by_owner.size(); ++owner) {
        u32 cursor = state.network_index_by_owner[owner];
        if (cursor >= state.delayed_packets.size()) {
            continue;
        }

        all_owner_cursors_finished = false;
        for (; cursor < state.delayed_packets.size(); ++cursor) {
            P2PDelayedGameplayPacket& packet = state.delayed_packets[cursor];
            if (packet.owner_slot != owner) {
                continue;
            }
            if (gameplay_frame < packet.due_frame) {
                break;
            }

            packet.serial = state.delayed_packet_serial_by_owner[owner]++;
            packet.due_frame = 0x24;
            if (packet.subtype == 0x14) {
                packet.flags1 |= 0xfffffe00u;
                packet.flags2 |= 0xfffffe00u;
            }
            if (dispatch_packet != nullptr) {
                dispatch_packet(packet, user_data);
            }
        }
        state.network_index_by_owner[owner] = cursor;
    }

    TickP2PReplayVposCamera(state, gameplay_frame);
    if (all_owner_cursors_finished) {
        state.game_end_requested = true;
    }
    return 1;
}

void InitializeP2PLobbySupport(P2PLobbyState& state) {
    InitializeP2PLobbyBackgroundBitmap(state);
    RegisterP2PLobbyBackgroundShutdown(state);
}

void InitializeP2PLobbyBackgroundBitmap(P2PLobbyState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterP2PLobbyBackgroundShutdown(P2PLobbyState&) {
    if (!g_background_shutdown_registered) {
        std::atexit(shutdown_global_background);
        g_background_shutdown_registered = true;
    }
}

void ShutdownP2PLobbyBackgroundBitmap(P2PLobbyState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeP2PLobbyImageButtonSlot0() {
    ConstructP2PLobbyImageButtonSlot0();
    RegisterP2PLobbyImageButtonSlot0Shutdown();
}

void ConstructP2PLobbyImageButtonSlot0() {
    InitializeLegacyImageButtonControl(g_p2p_lobby_state.info_control);
}

void RegisterP2PLobbyImageButtonSlot0Shutdown() {
    register_image_button_shutdown(0, DestroyP2PLobbyImageButtonSlot0);
}

void DestroyP2PLobbyImageButtonSlot0() {
    DestroyLegacyImageButtonControl(g_p2p_lobby_state.info_control);
}

void InitializeP2PLobbyImageButtonSlot1() {
    ConstructP2PLobbyImageButtonSlot1();
    RegisterP2PLobbyImageButtonSlot1Shutdown();
}

void ConstructP2PLobbyImageButtonSlot1() {
    InitializeLegacyImageButtonControl(g_p2p_lobby_state.host_button);
}

void RegisterP2PLobbyImageButtonSlot1Shutdown() {
    register_image_button_shutdown(1, DestroyP2PLobbyImageButtonSlot1);
}

void DestroyP2PLobbyImageButtonSlot1() {
    DestroyLegacyImageButtonControl(g_p2p_lobby_state.host_button);
}

void InitializeP2PLobbyImageButtonSlot2() {
    ConstructP2PLobbyImageButtonSlot2();
    RegisterP2PLobbyImageButtonSlot2Shutdown();
}

void ConstructP2PLobbyImageButtonSlot2() {
    InitializeLegacyImageButtonControl(g_p2p_lobby_state.join_button);
}

void RegisterP2PLobbyImageButtonSlot2Shutdown() {
    register_image_button_shutdown(2, DestroyP2PLobbyImageButtonSlot2);
}

void DestroyP2PLobbyImageButtonSlot2() {
    DestroyLegacyImageButtonControl(g_p2p_lobby_state.join_button);
}

void InitializeP2PLobbyImageButtonSlot3() {
    ConstructP2PLobbyImageButtonSlot3();
    RegisterP2PLobbyImageButtonSlot3Shutdown();
}

void ConstructP2PLobbyImageButtonSlot3() {
    InitializeLegacyImageButtonControl(g_p2p_lobby_state.cancel_button);
}

void RegisterP2PLobbyImageButtonSlot3Shutdown() {
    register_image_button_shutdown(3, DestroyP2PLobbyImageButtonSlot3);
}

void DestroyP2PLobbyImageButtonSlot3() {
    DestroyLegacyImageButtonControl(g_p2p_lobby_state.cancel_button);
}

void InstallP2PLobbyAccelerators(P2PLobbyState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kP2PLobbyAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreP2PLobbyAccelerators(P2PLobbyState& state) {
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

bool start_p2p_socket_connect(P2PLobbyState& state, const char* remote_address,
    DWORD app_context, HWND notify_window, UINT notify_message) {
    const u16 port = static_cast<u16>(app_context);
    if (remote_address == nullptr || remote_address[0] == '\0' || port == 0) {
        return false;
    }
    if (state.join_socket != INVALID_SOCKET) {
        CloseLegacySocketRecord(state.join_socket);
        state.join_socket = INVALID_SOCKET;
    }
    return StartLegacySocketConnect(state.join_socket, remote_address, port,
        notify_window, notify_message);
}

HRESULT StartP2PLobbyJoinAttempt(P2PLobbyState& state, const char* display_name,
    const char* remote_address, DWORD app_context) {
    copy_limited(state.player_name.data(), state.player_name.size(), display_name);
    copy_limited(state.remote_address.data(), state.remote_address.size(), remote_address);
    state.join_pending = true;
    state.start_game_requested = false;

    bool started = true;
    if (state.callbacks.start_join != nullptr) {
        started = state.callbacks.start_join(state, state.remote_address.data(), app_context,
            state.window, kP2PLobbySocketNotifyMessage);
    }
    else {
        started = start_p2p_socket_connect(state, state.remote_address.data(),
            app_context, state.window, kP2PLobbySocketNotifyMessage);
    }
    if (!started) {
        state.join_pending = false;
        return kP2PConnectStartFailed;
    }

    if (state.window != nullptr) {
        state.join_timer = SetTimer(state.window, kP2PLobbyJoinTimerId,
            kP2PLobbyJoinRetryMs, nullptr);
    }
    char message[160]{};
    std::snprintf(message, sizeof(message),
        startup_message_row(33, "Connecting to %s."),
        remote_address != nullptr ? remote_address : "");
    show_lobby_message(state, message, RGB(250, 250, 250));
    return S_OK;
}

bool continue_p2p_join_attempt(P2PLobbyState& state) {
    if (!state.join_pending) {
        return true;
    }
    if (state.callbacks.continue_join != nullptr) {
        return state.callbacks.continue_join(state);
    }
    return start_p2p_socket_connect(state, state.remote_address.data(),
        state.default_tcp_port, state.window, kP2PLobbySocketNotifyMessage);
}

bool initialize_p2p_network_defaults(P2PLobbyState& state) {
    P2PNetworkLaunchParameters& launch = p2p_network_launch_parameters();
    if (launch.player_name[0] != '\0') {
        copy_limited(state.player_name.data(), state.player_name.size(),
            launch.player_name.data());
    }
    if (launch.remote_address[0] != '\0') {
        copy_limited(state.remote_address.data(), state.remote_address.size(),
            launch.remote_address.data());
    }

    sockaddr_in local_address{};
    if (ResolveLocalHostIpv4Address(local_address)) {
        const char* text = inet_ntoa(local_address.sin_addr);
        if (text != nullptr) {
            copy_limited(state.local_address.data(), state.local_address.size(), text);
        }
    }
    return true;
}

bool CreateP2PLobbyWindow(P2PLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.join_socket = INVALID_SOCKET;
    state.join_pending = false;
    state.start_game_requested = false;
    state.join_timer = 0;
    InitializeLegacyImageButtonControl(state.info_control);
    InitializeLegacyImageButtonControl(state.cancel_button);
    InitializeLegacyImageButtonControl(state.host_button);
    InitializeLegacyImageButtonControl(state.join_button);
    state.instruction_text.clear();

    const std::vector<P2PLobbyLayoutRect> layout = read_p2p_layout_record();
    if (layout.empty()) {
        return false;
    }

    const P2PLobbyLayoutRect window_rect = layout_at(layout, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(parent,
              window_rect.width, window_rect.height)
        : RankerCenteredFrontendWindowOrigin(
              window_rect.width, window_rect.height);
    const DWORD window_style =
        IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "P2P", "P2P", window_style,
        origin.x, origin.y, window_rect.width, window_rect.height, parent,
        nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }

    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(p2p_lobby_window_proc));

    state.name_edit = create_edit(state.window, instance, kP2PLobbyNameEditId,
        layout_at(layout, 1), kChildEditStyle);
    state.local_address_edit = create_edit(state.window, instance,
        kP2PLobbyLocalAddressEditId, layout_at(layout, 2),
        kChildEditAutoHScrollStyle);
    state.remote_address_edit = create_edit(state.window, instance,
        kP2PLobbyRemoteAddressEditId, layout_at(layout, 3),
        kChildEditAutoHScrollStyle);
    if (state.name_edit == nullptr || state.local_address_edit == nullptr ||
        state.remote_address_edit == nullptr) {
        return false;
    }

    SendMessageA(state.name_edit, EM_LIMITTEXT, state.player_name.size(), 0);
    SendMessageA(state.local_address_edit, EM_LIMITTEXT, state.local_address.size(), 0);
    SendMessageA(state.remote_address_edit, EM_LIMITTEXT, state.remote_address.size(), 0);

    std::vector<u8> text_record;
    if (LoadTrcRecordAlloc("Jw2_19.trc", kP2PLobbyInstructionTextTrcRecord,
            text_record, 1)) {
        state.instruction_text.assign(
            reinterpret_cast<const char*>(text_record.data()));
    }

    if (!create_image_button(state.info_control, state.window, "",
            kP2PLobbyInfoControlId, layout_at(layout, 4),
            kP2PLobbyInfoNormalBitmapRecord, kP2PLobbyInfoPressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kP2PLobbyCancelButtonId, layout_at(layout, 5),
            kP2PLobbyCancelNormalBitmapRecord, kP2PLobbyCancelPressedBitmapRecord) ||
        !create_image_button(state.host_button, state.window, "&Host",
            kP2PLobbyHostButtonId, layout_at(layout, 6),
            kP2PLobbyHostNormalBitmapRecord, kP2PLobbyHostPressedBitmapRecord) ||
        !create_image_button(state.join_button, state.window, "&Join",
            kP2PLobbyJoinButtonId, layout_at(layout, 7),
            kP2PLobbyJoinNormalBitmapRecord, kP2PLobbyJoinPressedBitmapRecord)) {
        return false;
    }
    SetWindowLongPtrA(state.info_control.window, GWL_STYLE, 0x5800000b);

    state.original_name_edit_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.name_edit, GWLP_WNDPROC));
    state.original_local_address_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.local_address_edit, GWLP_WNDPROC));
    state.original_remote_address_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.remote_address_edit, GWLP_WNDPROC));
    set_control_proc(state.name_edit);
    set_control_proc(state.local_address_edit);
    set_control_proc(state.remote_address_edit);

    if (state.ui_font != nullptr) {
        DeleteObject(state.ui_font);
        state.ui_font = nullptr;
    }
    state.ui_font = CreateScaledFrontendUiFont(1);
    const HFONT ui_font = p2p_lobby_ui_font(state);
    SendMessageA(state.window, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.info_control.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.cancel_button.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.host_button.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.join_button.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.name_edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.local_address_edit, WM_SETFONT,
        reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.remote_address_edit, WM_SETFONT,
        reinterpret_cast<WPARAM>(ui_font), TRUE);
    SendMessageA(state.name_edit, EM_LIMITTEXT, 0x13, 0);

    InstallP2PLobbyAccelerators(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kP2PLobbyBackgroundBitmapTrcRecord);

    const bool initialized = state.callbacks.initialize_network != nullptr ?
        state.callbacks.initialize_network(state) : initialize_p2p_network_defaults(state);
    if (!initialized) {
        DestroyWindow(state.window);
        return false;
    }

    if (state.player_name[0] == '\0') {
        DWORD size = static_cast<DWORD>(state.player_name.size());
        GetUserNameA(state.player_name.data(), &size);
    }
    SetWindowTextA(state.name_edit, state.player_name.data());
    const LRESULT name_length = SendMessageA(state.name_edit, WM_GETTEXTLENGTH, 0, 0);
    SendMessageA(state.name_edit, EM_SETSEL, 0, name_length);
    SetWindowTextA(state.local_address_edit, state.local_address.data());
    SetWindowTextA(state.remote_address_edit, state.remote_address.data());
    ShowWindow(state.name_edit, SW_SHOW);
    ShowWindow(state.local_address_edit, SW_SHOW);
    ShowWindow(state.remote_address_edit, SW_SHOW);
    ShowWindow(state.window, SW_SHOW);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE |
        RDW_UPDATENOW | RDW_ALLCHILDREN);
    SetFocus(state.name_edit);
    state.visible = true;
    return true;
}

LRESULT HandleP2PLobbyWindowMessage(P2PLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        if (state.callbacks.shutdown_network != nullptr) {
            state.callbacks.shutdown_network(state);
        }
        else {
            DeleteLinkLobbySocketCriticalSection(link_lobby_state());
        }
        RestoreP2PLobbyAccelerators(state);
        release_lobby_resources(state);
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
    case WM_ERASEBKGND:
        if (hwnd == state.window) {
            StretchBitmapMemoryResourceToClient(state.background,
                reinterpret_cast<HDC>(wparam), state.window);
            return 1;
        }
        break;
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kP2PWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kP2PBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw != nullptr && draw->CtlID == kP2PLobbyInfoControlId) {
            FillRect(draw->hDC, &draw->rcItem,
                reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            RECT text_rect{
                scaled_p2p_lobby_x(2), scaled_p2p_lobby_y(12),
                scaled_p2p_lobby_x(0x18c), scaled_p2p_lobby_y(0x87)};
            SetTextColor(draw->hDC, kP2PWhite);
            SetBkColor(draw->hDC, kP2PBlack);
            SetBkMode(draw->hDC, TRANSPARENT);
            const HGDIOBJ previous_font = SelectObject(draw->hDC,
                p2p_lobby_ui_font(state));
            const char* text = state.instruction_text.empty() ?
                state.last_status_text.c_str() : state.instruction_text.c_str();
            DrawTextA(draw->hDC, text, -1, &text_rect, 0);
            if (previous_font != nullptr && previous_font != HGDI_ERROR) {
                SelectObject(draw->hDC, previous_font);
            }
            break;
        }
        if (draw != nullptr) {
            LegacyImageButtonControl* button = button_for_id(state, draw->CtlID);
            if (button != nullptr) {
                DrawLegacyImageButtonItem(*button, *draw);
            }
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kP2PLobbyHostButtonId:
            handle_host_command(state, hwnd);
            break;
        case kP2PLobbyCancelButtonId:
            handle_cancel_command(state, hwnd);
            break;
        case kP2PLobbyInfoControlId:
            SetFocus(state.name_edit);
            break;
        case kP2PLobbyJoinButtonId:
            handle_join_command(state);
            break;
        default:
            break;
        }
        break;
    case WM_TIMER:
        if (state.join_pending && !continue_p2p_join_attempt(state)) {
            if (state.join_timer != 0) {
                KillTimer(hwnd, state.join_timer);
                state.join_timer = 0;
            }
            end_active_p2p_modal_prompt();
            show_startup_lobby_message(state, 91,
                "Connection was lost while joining the game.",
                kP2PDisconnectRed);
            return 0;
        }
        break;
    case kOnlinePromptAcceptMessage:
    case kOnlinePromptCancelMessage:
        if (state.join_timer != 0) {
            KillTimer(state.window, state.join_timer);
            state.join_timer = 0;
        }
        if (state.callbacks.handle_prompt_result != nullptr) {
            state.callbacks.handle_prompt_result(state, message);
        }
        break;
    case kP2PLobbyNetworkPayloadMessage:
        DispatchP2PLobbyNetworkPayload(state, wparam, lparam);
        free_posted_p2p_payload(lparam);
        if (state.start_game_requested) {
            end_active_p2p_modal_prompt();
            PostMessageA(hwnd, kP2PLobbyStartGameMessage, 0, 0);
            if (AsyncComContext* context = async_com_state().active_context;
                context != nullptr) {
                context->system_message_101_seen = false;
            }
        }
        break;
    case kP2PLobbyStartGameMessage:
        DestroyWindow(hwnd);
        if (state.callbacks.start_game != nullptr) {
            state.callbacks.start_game(state.parent_window, state.instance,
                state.return_context);
        }
        break;
    case kP2PLobbyFormatStatusMessage:
        show_lobby_message(state,
            format_startup_lobby_number(35, "Connection failed (send error=%d)",
                static_cast<int>(lparam)).c_str(),
            kP2PNetworkFailureRed);
        break;
    case kP2PLobbyStaticStatusMessage:
        show_startup_lobby_message(state, 36, "Getting connected player info.",
            kP2PNetworkWaitYellow);
        break;
    case kP2PLobbySocketNotifyMessage:
        HandleP2PLobbySocketNotification(state, wparam, lparam);
        break;
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleP2PLobbyControlMessage(P2PLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (id) {
    case kP2PLobbyInfoControlId:
    case kP2PLobbyHostButtonId:
    case kP2PLobbyJoinButtonId:
    case kP2PLobbyCancelButtonId:
    case kP2PLobbyNameEditId:
    case kP2PLobbyLocalAddressEditId:
    case kP2PLobbyRemoteAddressEditId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

void HandleP2PLobbyConnectResult(P2PLobbyState& state, WPARAM, LPARAM result) {
    if (state.join_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.join_timer);
        state.join_timer = 0;
    }
    state.join_pending = false;

    u32 result_code = 0xffffffffu;
    if (result != 0) {
        result_code = read_packet_u32(reinterpret_cast<const void*>(result), 0x14, 0x10);
    }

    if (result_code != 10 && active_transport_uses_legacy_tcp()) {
        ShutdownLegacyTcpNetworking();
    }
    end_active_p2p_modal_prompt();

    switch (result_code) {
    case 0:
        show_startup_lobby_message(state, 42, "Connection failed - general error.",
            kP2PStartFailureRed);
        break;
    case 1:
        show_startup_lobby_message(state, 43,
            "Connection failed - map file send error.", kP2PStartFailureRed);
        break;
    case 2:
        show_startup_lobby_message(state, 44,
            "Connection failed - failed to get connected player info.",
            kP2PStartFailureRed);
        break;
    case 3:
        show_startup_lobby_message(state, 45, "Connection failed - game is full.",
            kP2PStartFailureRed);
        break;
    case 4:
        show_startup_lobby_message(state, 46,
            "Connection failed - player information send error.",
            kP2PStartFailureRed);
        break;
    case 5:
        show_startup_lobby_message(state, 47,
            "Connection failed - game already started.", kP2PStartFailureRed);
        break;
    case 6:
        show_startup_lobby_message(state, 48,
            "Connection failed - different game type.", kP2PStartFailureRed);
        break;
    case 7: {
        const u32 remote_version = result != 0 ?
            read_packet_u32(reinterpret_cast<const void*>(result), 0x10, 0x0c) : 0;
        const std::string message = format_p2p_version_mismatch(remote_version);
        show_lobby_message(state, message.c_str(), kP2PStartFailureRed);
        break;
    }
    case 8:
        show_startup_lobby_message(state, 50,
            "Connection failed - different connection mode.", kP2PStartFailureRed);
        break;
    case 9:
        show_startup_lobby_message(state, 51,
            "Connection failed - wrong password.", kP2PStartFailureRed);
        break;
    default:
        break;
    }
}

void DispatchP2PLobbyNetworkPayload(P2PLobbyState& state, WPARAM sender,
    LPARAM payload) {
    const void* packet = reinterpret_cast<const void*>(payload);
    const u32 byte_count = p2p_payload_declared_size(packet);
    const u32 opcode = read_packet_u32(packet, byte_count >= 8 ? byte_count : 8, 4);
    LinkLobbyState& link = link_lobby_state();

    switch (opcode) {
    case 10:
        ApplyLinkLobbySessionSeedPacket(link, packet, byte_count);
        state.start_game_requested = true;
        break;
    case 0x0b:
        HandleP2PLobbyConnectResult(state, sender, payload);
        break;
    case 0x15:
        ApplyLinkLobbyMapDescriptorPacket(link, packet, byte_count);
        break;
    case 0x20:
        ApplyLinkLobbyPlayerPresencePacket(link, packet, byte_count);
        break;
    default:
        break;
    }

    if (state.callbacks.handle_network_payload != nullptr) {
        state.callbacks.handle_network_payload(state, sender, payload);
    }
}

void PumpP2PLobbyNetworkBuffer(P2PLobbyState& state, WPARAM sender,
    LegacySocketRecord& network_buffer) {
    while (network_buffer.receive_queue.size() > 3) {
        const u8* bytes = network_buffer.receive_queue.data();
        const std::size_t available = network_buffer.receive_queue.size();
        const u32 packet_type = read_packet_u32(bytes, available, 0);
        if (packet_type == 0) {
            if (available <= 7) {
                break;
            }
            const std::size_t first_text_length = bytes[7];
            const std::size_t second_length_offset = first_text_length + 0x0b;
            if (available <= second_length_offset) {
                break;
            }
            const std::size_t second_text_length = bytes[second_length_offset];
            const std::size_t packet_size =
                first_text_length + 0x0c + second_text_length;
            if (available < packet_size) {
                break;
            }
            ConsumeLegacySocketReceiveQueue(network_buffer,
                static_cast<u32>(packet_size));
            continue;
        }

        if (packet_type == 2) {
            if (available < 0x0c) {
                break;
            }
            const u32 packet_size = read_packet_u32(bytes, available, 8);
            if (packet_size == 0 || available < packet_size) {
                break;
            }
            post_copied_p2p_payload(state, sender, bytes, packet_size);
            ConsumeLegacySocketReceiveQueue(network_buffer, packet_size);
            continue;
        }
        break;
    }
}

void send_p2p_relay_join_packet(P2PLobbyState& state, SOCKET socket_handle) {
    sockaddr_in local_address{};
    if (RefreshSocketLocalAddress(socket_handle, local_address)) {
        if (const char* text = inet_ntoa(local_address.sin_addr);
            text != nullptr) {
            copy_limited(state.local_address.data(), state.local_address.size(), text);
            if (state.local_address_edit != nullptr) {
                SetWindowTextA(state.local_address_edit, state.local_address.data());
            }
        }
    }

    const P2PNetworkLaunchParameters& launch = p2p_network_launch_parameters();
    const char* password =
        launch.password[0] != '\0' ? launch.password.data() : "";
    SendLinkLobbyRelayJoinPacket(link_lobby_state(), socket_handle,
        state.player_name.data(), password);
}

void ApplyP2PLobbySocketEvent(P2PLobbyState& state, WPARAM socket,
    LPARAM event) {
    const SOCKET socket_handle = static_cast<SOCKET>(socket);
    switch (LOWORD(event)) {
    case FD_READ:
        if (LegacySocketRecord* record = ReceiveIntoLegacySocketQueue(socket_handle)) {
            PumpP2PLobbyNetworkBuffer(state, socket, *record);
        }
        break;
    case FD_CONNECT:
        state.join_socket = socket_handle;
        if (FindLegacySocketRecord(socket_handle) == nullptr) {
            RegisterLegacySocketRecord(socket_handle);
        }
        break;
    case FD_WRITE:
        if (state.join_pending) {
            send_p2p_relay_join_packet(state, socket_handle);
            state.join_pending = false;
            break;
        }
        if (FindLegacySocketRecord(socket_handle) == nullptr) {
            RegisterLegacySocketRecord(socket_handle);
        }
        QueueAndFlushSocketSend(0, nullptr, socket_handle);
        break;
    case FD_CLOSE:
        CloseLegacySocketRecord(socket_handle);
        if (state.join_socket == socket_handle) {
            state.join_socket = INVALID_SOCKET;
        }
        break;
    default:
        break;
    }
}

void HandleP2PLobbySocketNotification(P2PLobbyState& state, WPARAM socket,
    LPARAM event) {
    const WORD network_event = LOWORD(event);
    switch (network_event) {
    case FD_READ:
        if (state.callbacks.handle_socket_event != nullptr) {
            state.callbacks.handle_socket_event(state, socket, event);
        } else {
            ApplyP2PLobbySocketEvent(state, socket, event);
        }
        break;
    case FD_WRITE:
        if (state.callbacks.handle_socket_event != nullptr) {
            state.callbacks.handle_socket_event(state, socket, event);
        } else {
            ApplyP2PLobbySocketEvent(state, socket, event);
        }
        break;
    case FD_CONNECT:
        if (state.callbacks.handle_socket_event != nullptr) {
            state.callbacks.handle_socket_event(state, socket, event);
        } else {
            ApplyP2PLobbySocketEvent(state, socket, event);
        }
        break;
    case FD_CLOSE: {
        const bool was_join_pending = state.join_pending;
        if (state.callbacks.handle_socket_event != nullptr) {
            state.callbacks.handle_socket_event(state, socket, event);
        } else {
            ApplyP2PLobbySocketEvent(state, socket, event);
        }
        // Original 0x004baf90 leaves the pending join and its five-second
        // retry timer alive after a close.  Once the join packet has already
        // been sent, there is nothing left to retry and the timer must be
        // retired with the connection-failed message.
        if (!was_join_pending) {
            if (state.join_timer != 0 && state.window != nullptr) {
                KillTimer(state.window, state.join_timer);
                state.join_timer = 0;
            }
            end_active_p2p_modal_prompt();
            show_startup_lobby_message(state, 52,
                "Connection failed - game already started or disconnected.",
                kP2PDisconnectRed);
        }
        break;
    }
    default:
        if (state.callbacks.handle_socket_event != nullptr) {
            state.callbacks.handle_socket_event(state, socket, event);
        } else {
            ApplyP2PLobbySocketEvent(state, socket, event);
        }
        break;
    }
}

}

#endif
