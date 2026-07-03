#include "ranker_wizard_login.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_setup_data.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed = 0x10cf0000;
constexpr DWORD kAccountEditStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
constexpr DWORD kPasswordEditStyle =
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD;
constexpr DWORD kStatusEditStyle = WS_CHILD | ES_MULTILINE | ES_READONLY;
constexpr COLORREF kWizardSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kWizardErrorBlue = RGB(10, 10, 250);
constexpr COLORREF kWizardWhite = RGB(255, 255, 255);
constexpr COLORREF kWizardYellow = RGB(255, 255, 0);
constexpr COLORREF kWizardBlack = RGB(0, 0, 0);
constexpr const char* kDefaultPatchDownloadTempPath = "TheRankerPatcher.pat";
constexpr const char* kDefaultPatchDownloadFinalPath = "TheRankerPatcher.exe";
constexpr std::size_t kStartupPatchProgramSaveFailureRow = 18;

WizardLoginState g_wizard_login_state;
bool g_background_shutdown_registered = false;
bool g_new_account_shutdown_registered = false;
bool g_change_password_shutdown_registered = false;
bool g_ok_shutdown_registered = false;
bool g_cancel_shutdown_registered = false;
bool g_info_background_shutdown_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK wizard_login_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleWizardLoginWindowMessage(g_wizard_login_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK wizard_login_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleWizardLoginControlMessage(g_wizard_login_state, hwnd, message,
        wparam, lparam);
}

void shutdown_global_background() {
    ShutdownWizardLoginBackgroundBitmap(g_wizard_login_state);
}

void shutdown_global_new_account_button() {
    ShutdownWizardLoginNewAccountButton(g_wizard_login_state);
}

void shutdown_global_change_password_button() {
    ShutdownWizardLoginChangePasswordButton(g_wizard_login_state);
}

void shutdown_global_ok_button() {
    ShutdownWizardLoginOkButton(g_wizard_login_state);
}

void shutdown_global_cancel_button() {
    ShutdownWizardLoginCancelButton(g_wizard_login_state);
}

void shutdown_global_info_background_button() {
    ShutdownWizardLoginInfoBackgroundButton(g_wizard_login_state);
}

WizardLoginLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return WizardLoginLayoutRect{};
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

u32 read_le32(const std::vector<u8>& buffer, std::size_t offset) {
    if (offset + 4 > buffer.size()) {
        return 0;
    }
    return static_cast<u32>(buffer[offset]) |
        (static_cast<u32>(buffer[offset + 1]) << 8) |
        (static_cast<u32>(buffer[offset + 2]) << 16) |
        (static_cast<u32>(buffer[offset + 3]) << 24);
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
    if (text != nullptr) {
        std::strncpy(reinterpret_cast<char*>(buffer.data() + offset), text,
            available - 1);
    }
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& destination, const char* text) {
    destination.fill(0);
    if (text != nullptr) {
        std::strncpy(destination.data(), text, N - 1);
    }
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

void clear_text_control(WizardLoginTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_text_control(WizardLoginTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(wizard_login_control_proc));
}

void subclass_button(LegacyImageButtonControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(wizard_login_control_proc));
}

bool create_text_control(WizardLoginTextControl& control, HWND parent,
    HINSTANCE instance, DWORD style, int id, const WizardLoginLayoutRect& rect) {
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

bool create_image_button(LegacyImageButtonControl& control, HWND parent,
    const char* text, int id, const WizardLoginLayoutRect& rect, u32 normal_record,
    u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(control, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    subclass_button(control);
    LoadLegacyImageButtonBitmaps(control, normal_record, pressed_record);
    return true;
}

LegacyImageButtonControl* button_for_id(WizardLoginState& state, int id) {
    switch (id) {
    case kWizardLoginNewAccountButtonId:
        return &state.new_account_button;
    case kWizardLoginChangePasswordButtonId:
        return &state.change_password_button;
    case kWizardLoginOkButtonId:
        return &state.ok_button;
    case kWizardLoginCancelButtonId:
        return &state.cancel_button;
    case kWizardLoginInfoBackgroundButtonId:
        return &state.info_background_button;
    default:
        return nullptr;
    }
}

WNDPROC original_proc_for_id(WizardLoginState& state, int id) {
    switch (id) {
    case kWizardLoginAccountEditId:
        return state.account_edit.original_window_proc;
    case kWizardLoginPasswordEditId:
        return state.password_edit.original_window_proc;
    case kWizardLoginStatusEditId:
        return state.status_edit.original_window_proc;
    case kWizardLoginNewAccountButtonId:
        return state.new_account_button.original_window_proc;
    case kWizardLoginChangePasswordButtonId:
        return state.change_password_button.original_window_proc;
    case kWizardLoginOkButtonId:
        return state.ok_button.original_window_proc;
    case kWizardLoginCancelButtonId:
        return state.cancel_button.original_window_proc;
    case kWizardLoginInfoBackgroundButtonId:
        return state.info_background_button.original_window_proc;
    default:
        return nullptr;
    }
}

void show_message(WizardLoginState& state, const char* text,
    COLORREF color = kWizardSoftWhite) {
    state.last_message = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
    }
    if (state.status_edit.window != nullptr) {
        SetWindowTextA(state.status_edit.window, state.last_message.c_str());
    }
}

void show_status(WizardLoginState& state, const char* text,
    COLORREF color = kWizardSoftWhite) {
    state.last_message = text == nullptr ? "" : text;
    if (state.callbacks.show_status != nullptr && state.window != nullptr) {
        state.callbacks.show_status(state.window, state.last_message.c_str(), color);
        return;
    }
    show_message(state, text, color);
}

const char* startup_message_row(std::size_t index, const char* fallback) {
    const auto& rows = startup_text_tables().message_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

const char* wizard_login_status_message(u32 status) {
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

void queue_packet(WizardLoginState& state, const void* packet, i32 byte_count) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet, byte_count);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket,
            const_cast<void*>(packet), byte_count, nullptr);
    }
}

void queue_download_packet(WizardLoginState& state, const void* packet,
    i32 byte_count) {
    if (state.callbacks.queue_download_packet != nullptr) {
        state.callbacks.queue_download_packet(state, packet, byte_count);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        QueueLegacyAsyncTcpSend(*state.async_tcp_socket, packet, byte_count, nullptr);
    }
}

std::vector<u8> packet_with_header(u32 opcode, std::size_t byte_count) {
    std::vector<u8> packet(byte_count, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, opcode);
    write_le32(packet, 8, static_cast<u32>(byte_count));
    return packet;
}

BitmapIconResourceCollection& wizard_icon_collection(WizardLoginState& state) {
    if (state.icon_collection == nullptr) {
        state.icon_collection = &GlobalBitmapIconResourceCollection();
    }
    return *state.icon_collection;
}

void queue_guild_icon_request(WizardLoginState& state, u32 index) {
    std::vector<u8> packet = packet_with_header(0x40, 0x11);
    write_le32(packet, 0x0d, index);
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void queue_setup_version_packet(WizardLoginState& state, u32 opcode) {
    std::vector<u8> packet =
        packet_with_header(opcode, kWizardLoginVersionPacketBytes);
    write_le32(packet, 0x0d, LoadTrcRecord9Value());
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void destroy_login_window(WizardLoginState& state) {
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
}

void close_async_socket(WizardLoginState& state) {
    if (state.async_tcp_socket != nullptr) {
        CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
    }
    state.server_connected = false;
    state.request_pending = false;
}

void close_download_state(WizardLoginState& state) {
    if (state.download_file != nullptr) {
        std::fclose(state.download_file);
        state.download_file = nullptr;
    }
    state.download_active = false;
    state.expected_download_bytes = 0;
    state.downloaded_bytes = 0;
    state.download_progress_percent = 0;
    state.downloaded_map_bytes.clear();
}

const char* patch_download_temp_path(const WizardLoginState& state) {
    return state.patch_download_temp_path[0] != '\0' ?
        state.patch_download_temp_path.data() : kDefaultPatchDownloadTempPath;
}

const char* patch_download_final_path(const WizardLoginState& state) {
    return state.patch_download_final_path[0] != '\0' ?
        state.patch_download_final_path.data() : kDefaultPatchDownloadFinalPath;
}

void release_window_resources(WizardLoginState& state) {
    ReleaseBitmapMemoryResource(state.background);
    ReleaseLegacyImageButtonControlWindow(state.new_account_button);
    ReleaseLegacyImageButtonControlWindow(state.change_password_button);
    ReleaseLegacyImageButtonControlWindow(state.ok_button);
    ReleaseLegacyImageButtonControlWindow(state.cancel_button);
    ReleaseLegacyImageButtonControlWindow(state.info_background_button);
    state.account_edit.window = nullptr;
    state.password_edit.window = nullptr;
    state.status_edit.window = nullptr;
}

bool connect_to_configured_server(WizardLoginState& state) {
    if (state.callbacks.receive_payload != nullptr ||
        state.async_tcp_socket == nullptr ||
        state.server_address[0] == '\0' || state.server_port == 0) {
        return true;
    }
    ResetLegacyAsyncTcpSocketQueues(*state.async_tcp_socket);
    if (!ConnectLegacyAsyncTcpSocket(*state.async_tcp_socket,
            state.async_winsock_started, state.window, kWizardLoginNetworkMessage,
            state.server_address.data(), state.server_port, -1, -1)) {
        show_message(state,
            startup_message_row(3,
                "Winsock initialization or connection failed."),
            kWizardErrorBlue);
        return false;
    }
    char message[256]{};
    std::snprintf(message, sizeof(message),
        startup_message_row(4, "Connecting to %s..."),
        state.server_address.data());
    show_status(state, message);
    return true;
}

bool start_download_socket(WizardLoginState& state, const char* remote_address) {
    if (remote_address == nullptr || remote_address[0] == '\0') {
        return false;
    }
    copy_c_string(state.download_address, remote_address);
    state.download_active = true;
    state.downloaded_map_bytes.clear();
    state.expected_download_bytes = 0;
    state.downloaded_bytes = 0;
    state.download_progress_percent = 0;
    if (state.callbacks.receive_download_payload != nullptr ||
        state.async_tcp_socket == nullptr || state.download_port == 0) {
        show_status(state, "Map download requested.");
        return true;
    }
    return ConnectLegacyAsyncTcpSocket(*state.async_tcp_socket,
        state.download_winsock_started, state.window, kWizardLoginDownloadSocketMessage,
        remote_address, state.download_port, -1, -1);
}

void start_udp_after_server_connect(WizardLoginState& state) {
    if (state.udp_port == 0) {
        return;
    }
    sockaddr_in local_address{};
    const char* address_text = nullptr;
    if (state.async_tcp_socket != nullptr &&
        CopyLegacyAsyncTcpLocalSockaddr(*state.async_tcp_socket, local_address)) {
        address_text = inet_ntoa(local_address.sin_addr);
    }
    if (address_text == nullptr || address_text[0] == '\0') {
        address_text = state.local_udp_address[0] == '\0' ? "0.0.0.0" :
            state.local_udp_address.data();
    }
    copy_c_string(state.local_udp_address, address_text);
    if (StartLegacyUdpSocket(state.local_udp_address.data(), state.udp_port) ==
        INVALID_SOCKET) {
        state.udp_failed = true;
        char message[256]{};
        std::snprintf(message, sizeof(message),
            startup_message_row(93, "Unable to initialize UDP port %d."),
            static_cast<int>(state.udp_port));
        show_message(state, message, kWizardErrorBlue);
    } else {
        state.udp_failed = false;
    }
}

void queue_locale_handshake(WizardLoginState& state) {
    std::vector<u8> packet =
        packet_with_header(0x1f, kWizardLoginLocalePacketBytes);
    char locale[0x80]{};
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTLANGUAGE, locale,
        static_cast<int>(sizeof(locale)));
    copy_c_string(packet, 0x0d, 0x80, locale);
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void route_to_connect(WizardLoginState& state) {
    state.returning_to_connect = true;
    close_async_socket(state);
    close_download_state(state);
    destroy_login_window(state);
    if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    }
}

void route_to_lobby(WizardLoginState& state) {
    destroy_login_window(state);
    if (state.callbacks.open_lobby != nullptr) {
        state.callbacks.open_lobby(state);
    }
}

void confirm_and_route_game_start(WizardLoginState& state) {
    if (state.callbacks.confirm_game_start != nullptr &&
        !state.callbacks.confirm_game_start(state)) {
        if (state.callbacks.cancel_game_start != nullptr) {
            state.callbacks.cancel_game_start(state);
        }
        return;
    }
    if (state.callbacks.route_game_start != nullptr) {
        state.callbacks.route_game_start(state);
    } else {
        show_status(state, state.launch_context.data());
    }
}

void write_setup_data(WizardLoginState& state) {
    if (state.callbacks.write_setup_data != nullptr) {
        state.callbacks.write_setup_data(state);
        return;
    }
    ExportSetupText(kSetupWizardAccountOffset, state.account);
    WriteDefaultSetupDataBuffer();
}

void finalize_patch_download_file(WizardLoginState& state) {
    if (state.download_file != nullptr) {
        std::fclose(state.download_file);
        state.download_file = nullptr;
    }
    const char* temp_path = patch_download_temp_path(state);
    const char* final_path = patch_download_final_path(state);
    std::remove(final_path);
    std::rename(temp_path, final_path);
}

void handle_login_status(WizardLoginState& state, u32 status) {
    state.request_pending = false;
    switch (status) {
    case 0:
        read_window_text(state.account_edit.window, state.account.data(),
            static_cast<int>(state.account.size()));
        write_setup_data(state);
        route_to_lobby(state);
        break;
    default:
        show_message(state, wizard_login_status_message(status), kWizardErrorBlue);
        break;
    }
}

void handle_login_success_payload(WizardLoginState& state, const u8* payload,
    i32 byte_count) {
    if (byte_count > 0x0d) {
        copy_c_string(state.display_name,
            reinterpret_cast<const char*>(payload + 0x0d));
        SetWindowTextA(state.status_edit.window, state.display_name.data());
    }
    state.server_connected = true;
    state.request_pending = false;

    char key[kWizardLoginKeyBytes]{};
    if (!BuildTrcRecord10Key(key)) {
        show_message(state,
            startup_message_row(21,
                "The CD key data is invalid. Please reinstall the game."),
            kWizardErrorBlue);
        return;
    }

    std::vector<u8> key_packet =
        packet_with_header(0x43, kWizardLoginKeyPacketBytes);
    copy_c_string(key_packet, 0x0d, kWizardLoginKeyBytes, key);
    queue_packet(state, key_packet.data(), static_cast<i32>(key_packet.size()));
    queue_setup_version_packet(state, 0x2b);
}

void handle_map_download_offer(WizardLoginState& state, const u8* payload,
    i32 byte_count) {
    if (byte_count <= 0x0d || payload[0x0d] == 0) {
        queue_setup_version_packet(state, 0x2d);
        return;
    }
    const char* remote_address =
        byte_count > 0x0e ? reinterpret_cast<const char*>(payload + 0x0e) : "";
    if (!start_download_socket(state, remote_address)) {
        show_message(state,
            startup_message_row(3,
                "Winsock initialization or connection failed."),
            kWizardErrorBlue);
    } else {
        show_status(state,
            startup_message_row(14, "Patch program preparing (0%)."));
    }
}

void handle_start_game_offer(WizardLoginState& state, const u8* payload,
    i32 byte_count) {
    if (byte_count <= 0x0d || payload[0x0d] == 0) {
        return;
    }
    const char* session_name =
        byte_count > 0x0e ? reinterpret_cast<const char*>(payload + 0x0e) : "";
    std::snprintf(state.launch_context.data(), state.launch_context.size(), "%s %u",
        session_name, LoadTrcRecord9Value());
    confirm_and_route_game_start(state);
}

void handle_player_slot_payload(WizardLoginState& state, const u8* payload,
    i32 byte_count) {
    if (payload == nullptr || byte_count <= 0x0d) {
        return;
    }
    const std::size_t payload_bytes =
        std::min<std::size_t>(kWizardLoginPlayerSlotPayloadBytes,
            static_cast<std::size_t>(byte_count - 0x0d));
    state.player_slot_payload.assign(payload + 0x0d, payload + 0x0d + payload_bytes);
}

void handle_server_packet(WizardLoginState& state, const u8* payload,
    i32 byte_count) {
    if (payload == nullptr || byte_count < 0x0d) {
        return;
    }
    const u32 opcode = read_le32(payload, byte_count, 4);
    switch (opcode) {
    case 2:
        handle_login_status(state, read_le32(payload, byte_count, 0x0d));
        break;
    case 0x20:
        handle_login_success_payload(state, payload, byte_count);
        break;
    case 0x2c:
        handle_map_download_offer(state, payload, byte_count);
        break;
    case 0x2e:
        handle_start_game_offer(state, payload, byte_count);
        break;
    case 0x3f: {
        const u32 count = read_le32(payload, byte_count, 0x0d);
        const u32* ids = byte_count >= 0x11 ?
            reinterpret_cast<const u32*>(payload + 0x11) : nullptr;
        QueueWizardLoginGuildIconRequests(state, count, ids);
        break;
    }
    case 0x41:
        HandleWizardLoginGuildIconPayload(state, payload, byte_count);
        break;
    case 0x44:
        show_message(state,
            byte_count > 0x0d ? reinterpret_cast<const char*>(payload + 0x0d) :
                                "Wizard soft-net server error.",
            kWizardErrorBlue);
        PostMessageA(state.window, kWizardLoginReturnMessage, 0, 0);
        break;
    case 0x59:
        handle_player_slot_payload(state, payload, byte_count);
        break;
    default:
        state.last_server_payload.assign(payload, payload + byte_count);
        break;
    }
}

void consume_server_payloads(WizardLoginState& state) {
    if (state.callbacks.receive_payload != nullptr) {
        i32 byte_count = 0;
        const u8* payload = state.callbacks.receive_payload(state, byte_count);
        handle_server_packet(state, payload, byte_count);
        return;
    }
    if (state.async_tcp_socket == nullptr) {
        return;
    }
    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= 0x0d) {
        u32 packet_bytes = read_le32(payload, byte_count, 8);
        if (packet_bytes < 0x0d || packet_bytes > static_cast<u32>(byte_count)) {
            packet_bytes = static_cast<u32>(byte_count);
        }
        handle_server_packet(state, payload, static_cast<i32>(packet_bytes));
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
            static_cast<i32>(packet_bytes));
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

void handle_download_packet(WizardLoginState& state, const u8* packet,
    i32 byte_count) {
    if (packet == nullptr || byte_count < 8) {
        return;
    }
    const u32 packet_bytes = read_le32(packet, byte_count, 0);
    const u32 opcode = read_le32(packet, byte_count, 4);
    if (packet_bytes > static_cast<u32>(byte_count)) {
        return;
    }
    if (opcode == 1 && byte_count >= 0x0c) {
        state.expected_download_bytes = read_le32(packet, byte_count, 8);
        state.downloaded_bytes = 0;
        state.download_progress_percent = 0;
        state.downloaded_map_bytes.clear();
        if (state.download_file != nullptr) {
            std::fclose(state.download_file);
            state.download_file = nullptr;
        }
        state.download_file = std::fopen(patch_download_temp_path(state), "wb");
        if (state.download_file == nullptr) {
            show_message(state,
                startup_message_row(kStartupPatchProgramSaveFailureRow,
                    "Patch data file could not be created."),
                kWizardErrorBlue);
            return;
        }
        std::vector<u8> ack(0x0c, 0);
        write_le32(ack, 0, 0x0c);
        write_le32(ack, 4, 0x0c);
        queue_download_packet(state, ack.data(), static_cast<i32>(ack.size()));
    } else if (opcode == 0x0d && byte_count >= 0x0c) {
        const i32 chunk_bytes = static_cast<i32>(read_le32(packet, byte_count, 8));
        if (chunk_bytes < 0) {
            finalize_patch_download_file(state);
            state.download_active = false;
            std::snprintf(state.launch_context.data(), state.launch_context.size(),
                "%s %u", state.download_address.data(), LoadTrcRecord9Value());
            confirm_and_route_game_start(state);
            return;
        }
        const std::size_t available =
            std::min<std::size_t>(static_cast<std::size_t>(std::max(chunk_bytes, 0)),
                static_cast<std::size_t>(byte_count - 0x0c));
        if (state.download_file != nullptr && available != 0) {
            std::fwrite(packet + 0x0c, available, 1, state.download_file);
        }
        state.downloaded_map_bytes.insert(state.downloaded_map_bytes.end(),
            packet + 0x0c, packet + 0x0c + available);
        if (state.expected_download_bytes != 0) {
            const u32 percent =
                (state.downloaded_bytes * 100u) / state.expected_download_bytes;
            if (percent != state.download_progress_percent) {
                state.download_progress_percent = percent;
                char message[80]{};
                std::snprintf(message, sizeof(message),
                    startup_message_row(19,
                        "Patch program preparing (%d%%)"),
                    static_cast<int>(percent));
                show_status(state, message);
            }
        }
        state.downloaded_bytes += static_cast<u32>(available);
        std::vector<u8> ack(0x0c, 0);
        write_le32(ack, 0, 0x0c);
        write_le32(ack, 4, 0x0c);
        write_le32(ack, 8, state.downloaded_bytes);
        queue_download_packet(state, ack.data(), static_cast<i32>(ack.size()));
    }
}

void consume_download_payloads(WizardLoginState& state) {
    if (state.callbacks.receive_download_payload != nullptr) {
        i32 byte_count = 0;
        const u8* payload = state.callbacks.receive_download_payload(state, byte_count);
        handle_download_packet(state, payload, byte_count);
        return;
    }
    if (state.async_tcp_socket == nullptr) {
        return;
    }
    if (!ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket)) {
        show_message(state,
            startup_message_row(17, "A communication error occurred."),
            kWizardErrorBlue);
        return;
    }
    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= 8) {
        u32 packet_bytes = read_le32(payload, byte_count, 0);
        if (packet_bytes < 8 || packet_bytes > static_cast<u32>(byte_count)) {
            break;
        }
        handle_download_packet(state, payload, static_cast<i32>(packet_bytes));
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
            static_cast<i32>(packet_bytes));
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

} // namespace

WizardLoginState& wizard_login_state() {
    return g_wizard_login_state;
}

void InitializeWizardSessionArchiveDescriptor(WizardSessionArchiveDescriptor& descriptor) {
    descriptor.magic = 0;
    descriptor.version = 0;
    descriptor.title.fill(0);
    descriptor.description.fill(0);
    descriptor.player_count = 0;
    descriptor.terrain_type = 0;
    descriptor.session_game_type = 0;
    descriptor.map_width = 0;
    descriptor.map_height = 0;
    descriptor.runtime_options.fill(0);
    descriptor.archive_path.fill(0);
    descriptor.file_size = 0;
    descriptor.file_time = FILETIME{};
    descriptor.raw_header_record.clear();
    descriptor.raw_summary_record.clear();
    descriptor.raw_mode_record.clear();
    descriptor.raw_player_record.clear();
    InitializeSessionRuntimeBufferPairs(descriptor.user_runtime_buffers);
    descriptor.invalid = true;
}

void DestroyWizardSessionArchiveDescriptor(WizardSessionArchiveDescriptor& descriptor) {
    ReleaseSessionRuntimeBufferPairs(descriptor.user_runtime_buffers);
    descriptor.raw_header_record.clear();
    descriptor.raw_summary_record.clear();
    descriptor.raw_mode_record.clear();
    descriptor.raw_player_record.clear();
    descriptor.invalid = true;
}

bool load_wizard_descriptor_record(const char* archive_name, u32 record_index,
    std::vector<u8>& out) {
    out.clear();
    u32 original_size = 0;
    if (!QueryTrcRecordSizes(archive_name, record_index, nullptr, &original_size)) {
        return false;
    }
    out.assign(original_size, 0);
    return LoadTrcRecordIntoBuffer(archive_name, record_index, out.data(), out.size());
}

bool LoadWizardSessionArchiveDescriptor(WizardSessionArchiveDescriptor& descriptor,
    const char* archive_name) {
    InitializeWizardSessionArchiveDescriptor(descriptor);
    if (archive_name == nullptr || archive_name[0] == '\0') {
        return false;
    }
    copy_c_string(descriptor.archive_path, archive_name);
    if (!load_wizard_descriptor_record(archive_name, 0, descriptor.raw_header_record) ||
        !load_wizard_descriptor_record(archive_name, 1, descriptor.raw_summary_record) ||
        !load_wizard_descriptor_record(archive_name, 2, descriptor.raw_mode_record) ||
        !load_wizard_descriptor_record(archive_name, 3, descriptor.raw_player_record) ||
        LoadUserForceSessionRuntimeOverrideRecord(archive_name,
            descriptor.user_runtime_buffers) == SessionRuntimeOverrideLoadStatus::Failed) {
        return false;
    }
    descriptor.magic = read_le32(descriptor.raw_header_record, 0);
    descriptor.version = read_le32(descriptor.raw_header_record, 4);
    if (!descriptor.raw_summary_record.empty()) {
        const std::size_t title_bytes =
            std::min<std::size_t>(descriptor.title.size() - 1,
                descriptor.raw_summary_record.size());
        std::memcpy(descriptor.title.data(), descriptor.raw_summary_record.data(),
            title_bytes);
    }
    if (descriptor.raw_summary_record.size() > 0x20) {
        const std::size_t description_bytes =
            std::min<std::size_t>(descriptor.description.size() - 1,
                descriptor.raw_summary_record.size() - 0x20);
        std::memcpy(descriptor.description.data(),
            descriptor.raw_summary_record.data() + 0x20, description_bytes);
    }
    descriptor.terrain_type = read_le32(descriptor.raw_summary_record, 0x160);
    descriptor.map_width = read_le32(descriptor.raw_summary_record, 0x164);
    descriptor.map_height = read_le32(descriptor.raw_summary_record, 0x168);
    descriptor.session_game_type = read_le32(descriptor.raw_summary_record, 0x16c);
    if (descriptor.raw_summary_record.size() > 0x174) {
        descriptor.player_count = descriptor.raw_summary_record[0x174];
    }
    WIN32_FILE_ATTRIBUTE_DATA file_data{};
    if (GetFileAttributesExA(archive_name, GetFileExInfoStandard, &file_data)) {
        descriptor.file_size = file_data.nFileSizeLow;
        descriptor.file_time = file_data.ftLastWriteTime;
    }
    descriptor.invalid = descriptor.magic != kWizardLoginSessionMagic ||
        descriptor.version != kWizardLoginSessionVersion || descriptor.terrain_type >= 6 ||
        descriptor.player_count > 8;
    return !descriptor.invalid;
}

void InitializeWizardLoginBackgroundResourceAndShutdown(WizardLoginState& state) {
    InitializeWizardLoginBackgroundBitmap(state);
    RegisterWizardLoginBackgroundShutdown(state);
}

void InitializeWizardLoginBackgroundBitmap(WizardLoginState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterWizardLoginBackgroundShutdown(WizardLoginState&) {
    if (!g_background_shutdown_registered) {
        std::atexit(shutdown_global_background);
        g_background_shutdown_registered = true;
    }
}

void ShutdownWizardLoginBackgroundBitmap(WizardLoginState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeWizardLoginNewAccountButtonSupport(WizardLoginState& state) {
    InitializeWizardLoginNewAccountButton(state);
    RegisterWizardLoginNewAccountButtonShutdown(state);
}

void InitializeWizardLoginNewAccountButton(WizardLoginState& state) {
    InitializeLegacyImageButtonControl(state.new_account_button);
}

void RegisterWizardLoginNewAccountButtonShutdown(WizardLoginState&) {
    if (!g_new_account_shutdown_registered) {
        std::atexit(shutdown_global_new_account_button);
        g_new_account_shutdown_registered = true;
    }
}

void ShutdownWizardLoginNewAccountButton(WizardLoginState& state) {
    DestroyLegacyImageButtonControl(state.new_account_button);
}

void InitializeWizardLoginChangePasswordButtonSupport(WizardLoginState& state) {
    InitializeWizardLoginChangePasswordButton(state);
    RegisterWizardLoginChangePasswordButtonShutdown(state);
}

void InitializeWizardLoginChangePasswordButton(WizardLoginState& state) {
    InitializeLegacyImageButtonControl(state.change_password_button);
}

void RegisterWizardLoginChangePasswordButtonShutdown(WizardLoginState&) {
    if (!g_change_password_shutdown_registered) {
        std::atexit(shutdown_global_change_password_button);
        g_change_password_shutdown_registered = true;
    }
}

void ShutdownWizardLoginChangePasswordButton(WizardLoginState& state) {
    DestroyLegacyImageButtonControl(state.change_password_button);
}

void InitializeWizardLoginOkButtonSupport(WizardLoginState& state) {
    InitializeWizardLoginOkButton(state);
    RegisterWizardLoginOkButtonShutdown(state);
}

void InitializeWizardLoginOkButton(WizardLoginState& state) {
    InitializeLegacyImageButtonControl(state.ok_button);
}

void RegisterWizardLoginOkButtonShutdown(WizardLoginState&) {
    if (!g_ok_shutdown_registered) {
        std::atexit(shutdown_global_ok_button);
        g_ok_shutdown_registered = true;
    }
}

void ShutdownWizardLoginOkButton(WizardLoginState& state) {
    DestroyLegacyImageButtonControl(state.ok_button);
}

void InitializeWizardLoginCancelButtonSupport(WizardLoginState& state) {
    InitializeWizardLoginCancelButton(state);
    RegisterWizardLoginCancelButtonShutdown(state);
}

void InitializeWizardLoginCancelButton(WizardLoginState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterWizardLoginCancelButtonShutdown(WizardLoginState&) {
    if (!g_cancel_shutdown_registered) {
        std::atexit(shutdown_global_cancel_button);
        g_cancel_shutdown_registered = true;
    }
}

void ShutdownWizardLoginCancelButton(WizardLoginState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InitializeWizardLoginInfoBackgroundButtonSupport(WizardLoginState& state) {
    InitializeWizardLoginInfoBackgroundButton(state);
    RegisterWizardLoginInfoBackgroundButtonShutdown(state);
}

void InitializeWizardLoginInfoBackgroundButton(WizardLoginState& state) {
    InitializeLegacyImageButtonControl(state.info_background_button);
}

void RegisterWizardLoginInfoBackgroundButtonShutdown(WizardLoginState&) {
    if (!g_info_background_shutdown_registered) {
        std::atexit(shutdown_global_info_background_button);
        g_info_background_shutdown_registered = true;
    }
}

void ShutdownWizardLoginInfoBackgroundButton(WizardLoginState& state) {
    DestroyLegacyImageButtonControl(state.info_background_button);
}

void InstallWizardLoginAccelerators(WizardLoginState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kWizardLoginAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreWizardLoginAccelerators(WizardLoginState& state) {
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

void CloseWizardLoginAndReturn(WizardLoginState& state) {
    destroy_login_window(state);
    if (state.return_context == 2 &&
        state.callbacks.open_figs_address_book != nullptr) {
        state.callbacks.open_figs_address_book(state);
    } else if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    }
}

bool CreateWizardLoginWindow(WizardLoginState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.request_pending = false;
    state.returning_to_connect = false;
    state.close_after_error = false;
    state.visible = false;
    state.last_message.clear();

    InitializeWizardLoginBackgroundBitmap(state);
    InitializeWizardLoginNewAccountButton(state);
    InitializeWizardLoginChangePasswordButton(state);
    InitializeWizardLoginOkButton(state);
    InitializeWizardLoginCancelButton(state);
    InitializeWizardLoginInfoBackgroundButton(state);

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kWizardLoginLayoutTrcRecord)) {
        return false;
    }

    const WizardLoginLayoutRect window_rect = layout_at(layout.table, 0);
    const POINT origin = RankerFrontendWindowOrigin();
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Light", "Light", style,
        origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(wizard_login_window_proc));

    if (!create_text_control(state.account_edit, state.window, instance,
            kAccountEditStyle, kWizardLoginAccountEditId,
            layout_at(layout.table, 1)) ||
        !create_text_control(state.password_edit, state.window, instance,
            kPasswordEditStyle, kWizardLoginPasswordEditId,
            layout_at(layout.table, 2)) ||
        !create_text_control(state.status_edit, state.window, instance,
            kStatusEditStyle, kWizardLoginStatusEditId,
            layout_at(layout.table, 3)) ||
        !create_image_button(state.new_account_button, state.window, "New &Account",
            kWizardLoginNewAccountButtonId, layout_at(layout.table, 4),
            kWizardLoginNewAccountNormalBitmapRecord,
            kWizardLoginNewAccountPressedBitmapRecord) ||
        !create_image_button(state.change_password_button, state.window,
            "C&hange Password", kWizardLoginChangePasswordButtonId,
            layout_at(layout.table, 5), kWizardLoginChangePasswordNormalBitmapRecord,
            kWizardLoginChangePasswordPressedBitmapRecord) ||
        !create_image_button(state.ok_button, state.window, "",
            kWizardLoginOkButtonId, layout_at(layout.table, 6),
            kWizardLoginOkNormalBitmapRecord,
            kWizardLoginOkPressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kWizardLoginCancelButtonId, layout_at(layout.table, 7),
            kWizardLoginCancelNormalBitmapRecord,
            kWizardLoginCancelPressedBitmapRecord) ||
        !create_image_button(state.info_background_button, state.window, "&FIGSBG",
            kWizardLoginInfoBackgroundButtonId, layout_at(layout.table, 8),
            kWizardLoginInfoBackgroundBitmapRecord,
            kWizardLoginInfoBackgroundBitmapRecord)) {
        release_window_resources(state);
        return false;
    }

    SendMessageA(state.account_edit.window, EM_LIMITTEXT,
        kWizardLoginAccountBytes - 1, 0);
    SendMessageA(state.password_edit.window, EM_LIMITTEXT,
        kWizardLoginPasswordBytes - 1, 0);
    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.status_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kWizardLoginBackgroundBitmapRecord);
    InstallWizardLoginAccelerators(state);
    SetWindowTextA(state.account_edit.window, state.account.data());
    SetWindowTextA(state.status_edit.window, state.display_name.data());
    SetFocus(state.password_edit.window);
    ShowWindow(state.status_edit.window, SW_SHOW);
    if (state.return_context != 2) {
        ShowWindow(GetLegacyImageButtonWindow(state.info_background_button), SW_HIDE);
    }
    ShowWindow(state.window, SW_SHOW);
    connect_to_configured_server(state);
    state.visible = true;
    return true;
}

void QueueWizardLoginGuildIconRequests(WizardLoginState& state, u32 count,
    const u32* server_icon_ids) {
    state.guild_icon_ids.assign(count, 0);
    state.guild_icon_payloads.clear();
    state.guild_icon_payloads.resize(count);
    BitmapIconResourceCollection& icons = wizard_icon_collection(state);
    bool request_sent = false;
    for (u32 index = 0; index < count; ++index) {
        const u32 server_id = server_icon_ids == nullptr ? 0 : server_icon_ids[index];
        state.guild_icon_ids[index] = server_id;

        if (index >= kGuildBitmapIconSlotCount) {
            continue;
        }

        const u32 slot = kGuildBitmapIconSlotBase + index;
        const bool loaded =
            slot < kBitmapIconSlotCount && icons.loaded[slot];
        const bool timestamp_matches =
            loaded && static_cast<u32>(icons.guild_timestamps[index]) == server_id;
        if (!timestamp_matches) {
            if (loaded) {
                ReleaseBitmapIconSlot(icons, slot);
            }
            if (!request_sent) {
                queue_guild_icon_request(state, index);
                request_sent = true;
            }
        }
    }
    if (!request_sent) {
        queue_guild_icon_request(state, 0xffffffffu);
    }
}

void HandleWizardLoginGuildIconPayload(WizardLoginState& state, const u8* payload,
    i32 byte_count) {
    if (payload == nullptr || byte_count < 0x15) {
        return;
    }
    const u32 index = read_le32(payload, byte_count, 0x0d);
    const u32 payload_bytes = read_le32(payload, byte_count, 0x11);
    const std::size_t payload_offset = 0x15;
    if (index >= state.guild_icon_ids.size() ||
        index >= kGuildBitmapIconSlotCount ||
        payload_offset + static_cast<std::size_t>(payload_bytes) >
            static_cast<std::size_t>(byte_count)) {
        return;
    }

    const u8* icon_bytes = payload + payload_offset;
    if (index < state.guild_icon_payloads.size()) {
        state.guild_icon_payloads[index].assign(icon_bytes, icon_bytes + payload_bytes);
    }

    BitmapIconResourceCollection& icons = wizard_icon_collection(state);
    WriteGuildIconBitmapFile(index, icon_bytes, payload_bytes,
        static_cast<std::time_t>(state.guild_icon_ids[index]));
    LoadGuildIconBitmapSlot(icons, index);

    for (u32 next = index; next < state.guild_icon_ids.size(); ++next) {
        if (next >= kGuildBitmapIconSlotCount) {
            continue;
        }
        const u32 slot = kGuildBitmapIconSlotBase + next;
        if (slot < kBitmapIconSlotCount && !icons.loaded[slot]) {
            queue_guild_icon_request(state, next);
            return;
        }
    }

    queue_guild_icon_request(state, 0xffffffffu);
}

LRESULT HandleWizardLoginWindowMessage(WizardLoginState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        RestoreWizardLoginAccelerators(state);
        release_window_resources(state);
        state.window = nullptr;
        state.visible = false;
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
        LegacyImageButtonControl* button =
            button_for_id(state, static_cast<int>(wparam));
        if (button != nullptr) {
            DrawLegacyImageButtonItem(*button,
                *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kWizardYellow);
        SetBkColor(reinterpret_cast<HDC>(wparam), kWizardBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kWizardWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kWizardBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kWizardSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kWizardBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == kWizardLoginChangePasswordButtonId) {
            read_window_text(state.account_edit.window, state.account.data(),
                static_cast<int>(state.account.size()));
            read_window_text(state.password_edit.window, state.password.data(),
                static_cast<int>(state.password.size()));
            destroy_login_window(state);
            if (state.callbacks.open_change_password != nullptr) {
                state.callbacks.open_change_password(state, state.account.data(),
                    state.password.data());
            }
            break;
        }
        if (id == kWizardLoginOkButtonId) {
            if (!state.request_pending) {
                HandleDefaultFrontendUiClickSound();
                SubmitWizardLoginRequest(state);
            } else {
                PostMessageA(hwnd, WM_COMMAND, wparam, lparam);
            }
            break;
        }
        if (id == kWizardLoginCancelButtonId) {
            HandleDefaultFrontendUiClickSound();
            close_async_socket(state);
            route_to_connect(state);
            break;
        }
        if (id == kWizardLoginNewAccountButtonId) {
            read_window_text(state.account_edit.window, state.account.data(),
                static_cast<int>(state.account.size()));
            read_window_text(state.password_edit.window, state.password.data(),
                static_cast<int>(state.password.size()));
            destroy_login_window(state);
            if (state.callbacks.open_new_account != nullptr) {
                state.callbacks.open_new_account(state, state.account.data(),
                    state.password.data());
            }
            break;
        }
        if (id == kWizardLoginFocusPasswordCommandId) {
            SetFocus(state.password_edit.window);
            break;
        }
        if (id == kWizardLoginFocusAccountCommandId) {
            SetFocus(state.account_edit.window);
            break;
        }
        if (id > 40000 && id < 0x9c43) {
            HWND focus = GetFocus();
            const int focus_id = static_cast<int>(GetWindowLongPtrA(focus, GWLP_ID));
            SetFocus(focus_id == kWizardLoginAccountEditId ?
                state.password_edit.window : state.account_edit.window);
            if (focus_id == kWizardLoginAccountEditId ||
                focus_id == kWizardLoginPasswordEditId) {
                return 0;
            }
            break;
        }
        break;
    }
    case kWizardLoginNetworkMessage:
        DispatchWizardLoginNetworkMessage(state, wparam, lparam);
        break;
    case kWizardLoginDownloadSocketMessage:
        DispatchWizardLoginDownloadSocketMessage(state, wparam, lparam);
        break;
    case kWizardLoginReturnMessage:
        close_async_socket(state);
        close_download_state(state);
        CloseWizardLoginAndReturn(state);
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleWizardLoginControlMessage(WizardLoginState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    switch (id) {
    case kWizardLoginAccountEditId:
    case kWizardLoginPasswordEditId:
    case kWizardLoginNewAccountButtonId:
    case kWizardLoginChangePasswordButtonId:
    case kWizardLoginStatusEditId:
    case kWizardLoginInfoBackgroundButtonId:
    case kWizardLoginOkButtonId:
    case kWizardLoginCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

bool SubmitWizardLoginRequest(WizardLoginState& state) {
    read_window_text(state.account_edit.window, state.account.data(),
        static_cast<int>(state.account.size()));
    read_window_text(state.password_edit.window, state.password.data(),
        static_cast<int>(state.password.size()));
    if (std::strlen(state.account.data()) == 0 ||
        std::strlen(state.password.data()) == 0) {
        show_message(state,
            startup_message_row(20, "Enter account and password."),
            kWizardSoftWhite);
        return false;
    }

    char trc_key[kWizardLoginKeyBytes]{};
    if (!BuildTrcRecord10Key(trc_key)) {
        show_message(state,
            startup_message_row(21,
                "The CD key data is invalid. Please reinstall the game."),
            kWizardErrorBlue);
        return false;
    }

    std::vector<u8> packet =
        packet_with_header(1, kWizardLoginSubmitPacketBytes);
    copy_c_string(packet, 0x0d, 0x80, state.account.data());
    copy_c_string(packet, 0x8d, 0x80, state.password.data());
    copy_c_string(packet, 0x10d, kWizardLoginKeyBytes, trc_key);
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
    state.request_pending = true;
    return true;
}

void DispatchWizardLoginNetworkMessage(WizardLoginState& state, WPARAM, LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == FD_READ || event == 1) {
        if (state.async_tcp_socket != nullptr) {
            ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
        }
        consume_server_payloads(state);
        return;
    }
    if (event == FD_CONNECT || event == 0x10) {
        state.server_connected = true;
        queue_locale_handshake(state);
        start_udp_after_server_connect(state);
        return;
    }
    if ((event == FD_CLOSE || event == 0x20) && !state.close_after_error) {
        show_message(state,
            startup_message_row(5, "Disconnected from the server."),
            kWizardErrorBlue);
        route_to_connect(state);
    }
}

void DispatchWizardLoginDownloadSocketMessage(WizardLoginState& state, WPARAM,
    LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == FD_READ || event == 1) {
        consume_download_payloads(state);
        return;
    }
    if (event == FD_CONNECT || event == 0x10) {
        if (state.async_tcp_socket != nullptr) {
            RegisterLegacyAsyncTcpSocketEvents(*state.async_tcp_socket, state.window,
                kWizardLoginDownloadSocketMessage, FD_READ | FD_WRITE | FD_CLOSE);
        }
        std::vector<u8> request(0x118, 0);
        write_le32(request, 0, 0x118);
        queue_download_packet(state, request.data(), static_cast<i32>(request.size()));
        return;
    }
    if (event == FD_CLOSE || event == 0x20) {
        show_message(state,
            startup_message_row(16,
                "Patch data connection closed. Please reconnect and patch again."),
            kWizardErrorBlue);
        close_download_state(state);
    }
}

} // namespace ranker

#endif
