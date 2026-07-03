#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr UINT kChangePasswordNetworkMessage = 0x465;
constexpr UINT kChangePasswordReconnectMessage = 0x503;
constexpr UINT kChangePasswordStatusMessage0 = 0x512;
constexpr UINT kChangePasswordStatusMessage1 = 0x513;
constexpr UINT kChangePasswordStatusMessage2 = 0x514;
constexpr UINT kChangePasswordStatusMessage3 = 0x515;
constexpr UINT kChangePasswordBusyMessage = 0x516;

constexpr int kChangePasswordAccountEditId = 0x1194;
constexpr int kChangePasswordOldPasswordEditId = 0x1195;
constexpr int kChangePasswordNewPasswordEditId = 0x1196;
constexpr int kChangePasswordConfirmPasswordEditId = 0x1197;
constexpr int kChangePasswordFocusAccountCommandId = 0x1198;
constexpr int kChangePasswordFocusOldPasswordCommandId = 0x1199;
constexpr int kChangePasswordFocusNewPasswordCommandId = 0x119a;
constexpr int kChangePasswordFocusConfirmPasswordCommandId = 0x119b;
constexpr int kChangePasswordStatusEditId = 0x119c;
constexpr int kChangePasswordOkButtonId = IDOK;
constexpr int kChangePasswordCancelButtonId = IDCANCEL;
constexpr int kChangePasswordAcceleratorResourceId = 0x1c2;

constexpr u32 kChangePasswordLayoutTrcRecord = 0x162;
constexpr u32 kChangePasswordBackgroundBitmapRecord = 0xcb;
constexpr u32 kChangePasswordOkNormalBitmapRecord = 0xcd;
constexpr u32 kChangePasswordOkPressedBitmapRecord = 0xcc;
constexpr u32 kChangePasswordCancelNormalBitmapRecord = 0xcf;
constexpr u32 kChangePasswordCancelPressedBitmapRecord = 0xce;
constexpr std::size_t kChangePasswordWirePacketBytes = 0x19d;
constexpr std::size_t kChangePasswordStatusRequestBytes = 0x0d;

struct ChangePasswordState;

using ChangePasswordActionCallback = void (*)(ChangePasswordState& state);
using ChangePasswordMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);
using ChangePasswordPacketCallback = void (*)(ChangePasswordState& state,
    const void* packet, i32 byte_count);
using ChangePasswordNetworkPayloadCallback = const u8* (*)(
    ChangePasswordState& state, i32& byte_count);

struct ChangePasswordCallbacks {
    ChangePasswordActionCallback open_connect_frontend = nullptr;
    ChangePasswordActionCallback open_wizard_soft_net = nullptr;
    ChangePasswordActionCallback write_setup_data = nullptr;
    ChangePasswordActionCallback set_busy = nullptr;
    ChangePasswordMessageCallback show_message = nullptr;
    ChangePasswordPacketCallback queue_packet = nullptr;
    ChangePasswordNetworkPayloadCallback receive_payload = nullptr;
};

struct ChangePasswordLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ChangePasswordTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct ChangePasswordState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    ChangePasswordTextControl account_edit;
    ChangePasswordTextControl old_password_edit;
    ChangePasswordTextControl new_password_edit;
    ChangePasswordTextControl confirm_password_edit;
    ChangePasswordTextControl status_edit;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<char, 0x20> submitted_account{};
    std::array<char, 10> submitted_old_password{};
    std::array<char, 10> submitted_new_password{};
    std::array<char, 0x80> submitted_confirm_password{};
    std::vector<u8> last_server_payload;
    std::string last_message;
    bool visible = false;
    bool reconnect_requested = false;
    ChangePasswordCallbacks callbacks{};
};

ChangePasswordState& change_password_state();

void InitializeChangePasswordBackgroundResourceAndShutdown(
    ChangePasswordState& state);
void InitializeChangePasswordBackgroundBitmap(ChangePasswordState& state);
void RegisterChangePasswordBackgroundShutdown(ChangePasswordState& state);
void ShutdownChangePasswordBackgroundBitmap(ChangePasswordState& state);
void InitializeChangePasswordOkButtonSupport(ChangePasswordState& state);
void InitializeChangePasswordOkButton(ChangePasswordState& state);
void RegisterChangePasswordOkButtonShutdown(ChangePasswordState& state);
void ShutdownChangePasswordOkButton(ChangePasswordState& state);
void InitializeChangePasswordCancelButtonSupport(ChangePasswordState& state);
void InitializeChangePasswordCancelButton(ChangePasswordState& state);
void RegisterChangePasswordCancelButtonShutdown(ChangePasswordState& state);
void ShutdownChangePasswordCancelButton(ChangePasswordState& state);

void InstallChangePasswordAccelerators(ChangePasswordState& state);
void RestoreChangePasswordAccelerators(ChangePasswordState& state);
bool CreateChangePasswordWindow(ChangePasswordState& state, HWND parent,
    HINSTANCE instance, LPARAM account_text, LPARAM old_password_text);
LRESULT HandleChangePasswordWindowMessage(ChangePasswordState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
void DeleteChangePasswordButton(LegacyImageButtonControl& button, bool free_storage);
LRESULT HandleChangePasswordControlMessage(ChangePasswordState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
bool SubmitChangePasswordRequest(ChangePasswordState& state);
void QueueChangePasswordStatusRequest(ChangePasswordState& state);
void DispatchChangePasswordNetworkMessage(ChangePasswordState& state, WPARAM wparam,
    LPARAM lparam);

#endif

}
