#pragma once

#include "ranker_bitmap_icon_collection.h"
#include "ranker_bitmap_resource.h"
#include "ranker_game_session_tables.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#endif

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr u32 kWizardLoginSessionMagic = 0x5241574a;
constexpr u32 kWizardLoginSessionVersion = 0x97;
constexpr u32 kWizardLoginLayoutTrcRecord = 0x169;
constexpr u32 kWizardLoginBackgroundBitmapRecord = 0x5a;
constexpr u32 kWizardLoginNewAccountNormalBitmapRecord = 0x5b;
constexpr u32 kWizardLoginNewAccountPressedBitmapRecord = 0x5c;
constexpr u32 kWizardLoginChangePasswordNormalBitmapRecord = 0x5d;
constexpr u32 kWizardLoginChangePasswordPressedBitmapRecord = 0x5e;
constexpr u32 kWizardLoginOkNormalBitmapRecord = 0x5f;
constexpr u32 kWizardLoginOkPressedBitmapRecord = 0x60;
constexpr u32 kWizardLoginCancelNormalBitmapRecord = 0x61;
constexpr u32 kWizardLoginCancelPressedBitmapRecord = 0x62;
constexpr u32 kWizardLoginInfoBackgroundBitmapRecord = 0x63;
constexpr int kWizardLoginAccountEditId = 0x138a;
constexpr int kWizardLoginPasswordEditId = 0x138b;
constexpr int kWizardLoginNewAccountButtonId = 0x1389;
constexpr int kWizardLoginChangePasswordButtonId = 0x138c;
constexpr int kWizardLoginStatusEditId = 0x138d;
constexpr int kWizardLoginFocusAccountCommandId = 0x138e;
constexpr int kWizardLoginFocusPasswordCommandId = 0x138f;
constexpr int kWizardLoginInfoBackgroundButtonId = 0x1390;
constexpr int kWizardLoginOkButtonId = IDOK;
constexpr int kWizardLoginCancelButtonId = IDCANCEL;
constexpr int kWizardLoginAcceleratorResourceId = 0x1f4;
constexpr UINT kWizardLoginNetworkMessage = 0x465;
constexpr UINT kWizardLoginDownloadSocketMessage = 0x466;
constexpr UINT kWizardLoginReturnMessage = 0x503;
constexpr std::size_t kWizardLoginAccountBytes = 0x20;
constexpr std::size_t kWizardLoginPasswordBytes = 10;
constexpr std::size_t kWizardLoginKeyBytes = 16;
constexpr std::size_t kWizardLoginSubmitPacketBytes = 0x11d;
constexpr std::size_t kWizardLoginLocalePacketBytes = 0x10d;
constexpr std::size_t kWizardLoginKeyPacketBytes = 0x1d;
constexpr std::size_t kWizardLoginVersionPacketBytes = 0x11;
constexpr std::size_t kWizardLoginPlayerSlotPayloadBytes = 0x3f4;

struct WizardLoginState;

using WizardLoginActionCallback = void (*)(WizardLoginState& state);
using WizardLoginConfirmCallback = bool (*)(WizardLoginState& state);
using WizardLoginAccountCallback = void (*)(WizardLoginState& state,
    const char* account, const char* password);
using WizardLoginMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);
using WizardLoginPacketCallback = void (*)(WizardLoginState& state,
    const void* packet, i32 byte_count);
using WizardLoginNetworkPayloadCallback = const u8* (*)(WizardLoginState& state,
    i32& byte_count);

struct WizardLoginCallbacks {
    WizardLoginActionCallback open_connect_frontend = nullptr;
    WizardLoginActionCallback open_lobby = nullptr;
    WizardLoginActionCallback open_figs_address_book = nullptr;
    WizardLoginActionCallback route_game_start = nullptr;
    WizardLoginActionCallback cancel_game_start = nullptr;
    WizardLoginActionCallback write_setup_data = nullptr;
    WizardLoginConfirmCallback confirm_game_start = nullptr;
    WizardLoginAccountCallback open_change_password = nullptr;
    WizardLoginAccountCallback open_new_account = nullptr;
    WizardLoginMessageCallback show_message = nullptr;
    WizardLoginMessageCallback show_status = nullptr;
    WizardLoginPacketCallback queue_packet = nullptr;
    WizardLoginPacketCallback queue_download_packet = nullptr;
    WizardLoginNetworkPayloadCallback receive_payload = nullptr;
    WizardLoginNetworkPayloadCallback receive_download_payload = nullptr;
};

struct WizardLoginLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct WizardLoginTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct WizardSessionArchiveDescriptor {
    u32 magic = 0;
    u32 version = 0;
    std::array<char, 0x20> title{};
    std::array<char, 0x140> description{};
    u8 player_count = 0;
    u32 terrain_type = 0;
    u32 session_game_type = 0;
    u32 map_width = 0;
    u32 map_height = 0;
    std::array<u32, 6> runtime_options{};
    std::array<char, MAX_PATH> archive_path{};
    u32 file_size = 0;
    FILETIME file_time{};
    std::vector<u8> raw_header_record;
    std::vector<u8> raw_summary_record;
    std::vector<u8> raw_mode_record;
    std::vector<u8> raw_player_record;
    SessionRuntimeBufferPairs user_runtime_buffers;
    bool invalid = true;
};

struct WizardLoginState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;
    BitmapIconResourceCollection* icon_collection = nullptr;

    BitmapMemoryResource background;
    WizardLoginTextControl account_edit;
    WizardLoginTextControl password_edit;
    WizardLoginTextControl status_edit;
    LegacyImageButtonControl new_account_button;
    LegacyImageButtonControl change_password_button;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;
    LegacyImageButtonControl info_background_button;
    HFONT ui_font = nullptr;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<char, kWizardLoginAccountBytes> account{};
    std::array<char, kWizardLoginPasswordBytes> password{};
    std::array<char, kWizardLoginAccountBytes> display_name{};
    std::array<char, 0x100> server_address{};
    std::array<char, 0x100> download_address{};
    std::array<char, MAX_PATH> patch_download_temp_path{};
    std::array<char, MAX_PATH> patch_download_final_path{};
    std::array<char, 0x100> local_udp_address{};
    std::array<char, 0x120> launch_context{};
    u16 server_port = 0;
    u16 download_port = 0;
    u16 udp_port = 0;

    bool visible = false;
    bool request_pending = false;
    bool server_connected = false;
    bool returning_to_connect = false;
    bool close_after_error = false;
    bool udp_failed = false;
    bool download_active = false;
    bool async_winsock_started = false;
    bool download_winsock_started = false;

    std::FILE* download_file = nullptr;
    u32 expected_download_bytes = 0;
    u32 downloaded_bytes = 0;
    u32 download_progress_percent = 0;
    std::vector<u8> downloaded_map_bytes;
    std::vector<u8> last_server_payload;
    std::vector<u8> player_slot_payload;
    std::vector<u32> guild_icon_ids;
    std::vector<std::vector<u8>> guild_icon_payloads;
    WizardSessionArchiveDescriptor selected_session;
    std::string last_message;
    WizardLoginCallbacks callbacks{};
};

WizardLoginState& wizard_login_state();

void InitializeWizardSessionArchiveDescriptor(WizardSessionArchiveDescriptor& descriptor);
void DestroyWizardSessionArchiveDescriptor(WizardSessionArchiveDescriptor& descriptor);
bool LoadWizardSessionArchiveDescriptor(WizardSessionArchiveDescriptor& descriptor,
    const char* archive_name);

void InitializeWizardLoginBackgroundResourceAndShutdown(WizardLoginState& state);
void InitializeWizardLoginBackgroundBitmap(WizardLoginState& state);
void RegisterWizardLoginBackgroundShutdown(WizardLoginState& state);
void ShutdownWizardLoginBackgroundBitmap(WizardLoginState& state);
void InitializeWizardLoginNewAccountButtonSupport(WizardLoginState& state);
void InitializeWizardLoginNewAccountButton(WizardLoginState& state);
void RegisterWizardLoginNewAccountButtonShutdown(WizardLoginState& state);
void ShutdownWizardLoginNewAccountButton(WizardLoginState& state);
void InitializeWizardLoginChangePasswordButtonSupport(WizardLoginState& state);
void InitializeWizardLoginChangePasswordButton(WizardLoginState& state);
void RegisterWizardLoginChangePasswordButtonShutdown(WizardLoginState& state);
void ShutdownWizardLoginChangePasswordButton(WizardLoginState& state);
void InitializeWizardLoginOkButtonSupport(WizardLoginState& state);
void InitializeWizardLoginOkButton(WizardLoginState& state);
void RegisterWizardLoginOkButtonShutdown(WizardLoginState& state);
void ShutdownWizardLoginOkButton(WizardLoginState& state);
void InitializeWizardLoginCancelButtonSupport(WizardLoginState& state);
void InitializeWizardLoginCancelButton(WizardLoginState& state);
void RegisterWizardLoginCancelButtonShutdown(WizardLoginState& state);
void ShutdownWizardLoginCancelButton(WizardLoginState& state);
void InitializeWizardLoginInfoBackgroundButtonSupport(WizardLoginState& state);
void InitializeWizardLoginInfoBackgroundButton(WizardLoginState& state);
void RegisterWizardLoginInfoBackgroundButtonShutdown(WizardLoginState& state);
void ShutdownWizardLoginInfoBackgroundButton(WizardLoginState& state);

void InstallWizardLoginAccelerators(WizardLoginState& state);
void RestoreWizardLoginAccelerators(WizardLoginState& state);
void CloseWizardLoginAndReturn(WizardLoginState& state);
bool CreateWizardLoginWindow(WizardLoginState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context);
void QueueWizardLoginGuildIconRequests(WizardLoginState& state, u32 count,
    const u32* server_icon_ids);
void HandleWizardLoginGuildIconPayload(WizardLoginState& state, const u8* payload,
    i32 byte_count);
LRESULT HandleWizardLoginWindowMessage(WizardLoginState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleWizardLoginControlMessage(WizardLoginState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
bool SubmitWizardLoginRequest(WizardLoginState& state);
void DispatchWizardLoginNetworkMessage(WizardLoginState& state, WPARAM wparam,
    LPARAM lparam);
void DispatchWizardLoginDownloadSocketMessage(WizardLoginState& state, WPARAM wparam,
    LPARAM lparam);

#endif

}
