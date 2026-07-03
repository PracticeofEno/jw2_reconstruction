#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_directplay.h"
#include "ranker_image_controls.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr int kIpxFrontendInfoButtonId = 0x0fa0;
constexpr int kIpxFrontendNameEditId = 0x0fa1;
constexpr int kIpxFrontendFocusNameCommandId = 0x0fa2;
constexpr int kIpxFrontendOkButtonId = IDOK;
constexpr int kIpxFrontendCancelButtonId = IDCANCEL;
constexpr int kIpxFrontendAcceleratorResourceId = 0x190;

constexpr u32 kIpxFrontendInfoTextTrcRecord = 0x15a;
constexpr u32 kIpxFrontendLayoutTrcRecord = 0x166;
constexpr u32 kIpxFrontendBackgroundBitmapRecord = 0x4d;
constexpr u32 kIpxFrontendInfoBitmapRecord = 0x4e;
constexpr u32 kIpxFrontendOkNormalBitmapRecord = 0x4f;
constexpr u32 kIpxFrontendOkPressedBitmapRecord = 0x50;
constexpr u32 kIpxFrontendCancelNormalBitmapRecord = 0x51;
constexpr u32 kIpxFrontendCancelPressedBitmapRecord = 0x52;
constexpr std::size_t kIpxFrontendPlayerNameBytes = 0x20;

struct IpxFrontendState;

struct IpxFrontendLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct IpxFrontendTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

using IpxFrontendActionCallback = void (*)(IpxFrontendState& state);
using IpxFrontendBoolCallback = bool (*)(IpxFrontendState& state);
using IpxFrontendConnectionCallback = bool (*)(IpxFrontendState& state,
    AsyncComContext& context);
using IpxFrontendShutdownCallback = void (*)(IpxFrontendState& state,
    AsyncComContext& context);
using IpxFrontendMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);

struct IpxFrontendCallbacks {
    IpxFrontendBoolCallback is_ipx_mode_enabled = nullptr;
    IpxFrontendConnectionCallback initialize_ipx_connection = nullptr;
    IpxFrontendBoolCallback open_ipx_lobby = nullptr;
    IpxFrontendActionCallback open_connect_frontend = nullptr;
    IpxFrontendActionCallback write_setup_data = nullptr;
    IpxFrontendActionCallback play_click_sound = nullptr;
    IpxFrontendShutdownCallback shutdown_ipx_connection = nullptr;
    IpxFrontendMessageCallback show_message = nullptr;
};

struct IpxFrontendState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    AsyncComContext* async_context = nullptr;
    const DirectPlayConnectionRecord* ipx_connection = nullptr;

    BitmapMemoryResource background;
    IpxFrontendTextControl name_edit;
    LegacyImageButtonControl info_button;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<char, kIpxFrontendPlayerNameBytes> player_name{};
    std::array<char, kIpxFrontendPlayerNameBytes> saved_player_name{};
    std::vector<IpxFrontendLayoutRect> layout;
    std::string info_text;
    std::string last_message;
    bool ipx_mode_enabled = true;
    bool visible = false;
    IpxFrontendCallbacks callbacks{};
};

IpxFrontendState& ipx_frontend_state();

void InitializeIpxFrontendBackgroundResourceAndShutdown(IpxFrontendState& state);
void InitializeIpxFrontendBackgroundBitmap(IpxFrontendState& state);
void RegisterIpxFrontendBackgroundShutdown(IpxFrontendState& state);
void ShutdownIpxFrontendBackgroundBitmap(IpxFrontendState& state);
void InitializeIpxFrontendInfoButtonSupport(IpxFrontendState& state);
void InitializeIpxFrontendInfoButton(IpxFrontendState& state);
void RegisterIpxFrontendInfoButtonDestructor(IpxFrontendState& state);
void DestroyIpxFrontendInfoButton(IpxFrontendState& state);
void InitializeIpxFrontendOkButtonSupport(IpxFrontendState& state);
void InitializeIpxFrontendOkButton(IpxFrontendState& state);
void RegisterIpxFrontendOkButtonDestructor(IpxFrontendState& state);
void DestroyIpxFrontendOkButton(IpxFrontendState& state);
void InitializeIpxFrontendCancelButtonSupport(IpxFrontendState& state);
void InitializeIpxFrontendCancelButton(IpxFrontendState& state);
void RegisterIpxFrontendCancelButtonDestructor(IpxFrontendState& state);
void DestroyIpxFrontendCancelButton(IpxFrontendState& state);
void InitializeIpxFrontendControls(IpxFrontendState& state);
void ReleaseIpxFrontendControls(IpxFrontendState& state);

void InstallIpxFrontendAccelerators(IpxFrontendState& state);
void RestoreIpxFrontendAccelerators(IpxFrontendState& state);

bool OpenIpxFrontendWindow(IpxFrontendState& state);
bool CreateIpxFrontendWindow(IpxFrontendState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context, AsyncComContext* async_context);
bool SubmitIpxFrontendName(IpxFrontendState& state);
LRESULT HandleIpxFrontendWindowMessage(IpxFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleIpxFrontendControlMessage(IpxFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
