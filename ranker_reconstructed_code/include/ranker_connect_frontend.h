#pragma once

#include "ranker_bitmap_icon_collection.h"
#include "ranker_bitmap_resource.h"
#include "ranker_directplay.h"
#include "ranker_image_controls.h"
#include "ranker_indexed_text_table.h"
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

constexpr std::size_t kConnectFrontendPlayerSlotCount = 8;
constexpr std::size_t kConnectFrontendPlayerSlotBytes = 0x19e;
constexpr std::size_t kConnectFrontendPlayerNameOffset = 0x60;
constexpr std::size_t kConnectFrontendPlayerHandleOffset = 0x18a;

constexpr u32 kConnectFrontendMagicJwar = 0x5241574a;
constexpr u32 kConnectFrontendConfigTrcRecord = 0x153;
constexpr u32 kConnectFrontendLayoutTrcRecord = 0x163;
constexpr u32 kConnectFrontendBackgroundBitmapRecord = 0;
constexpr u32 kConnectFrontendDescriptionBitmapRecord = 0x0d;

constexpr int kConnectFrontendWizardButtonId = 2000;
constexpr int kConnectFrontendP2PButtonId = 0x7d7;
constexpr int kConnectFrontendFreeServerButtonId = 0x7d4;
constexpr int kConnectFrontendIpxButtonId = 0x7d1;
constexpr int kConnectFrontendDescriptionButtonId = 0x7d5;
constexpr int kConnectFrontendOkButtonId = IDOK;
constexpr int kConnectFrontendCancelButtonId = IDCANCEL;
constexpr int kConnectFrontendAcceleratorResourceId = 0xc8;

enum class ConnectFrontendMode : u32 {
    WizardSoftNet = 0,
    PeerToPeer = 1,
    FreeInternetServer = 2,
    IpxLan = 3,
};

struct ConnectFrontendLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ConnectFrontendPlayerSlot {
    std::array<u8, kConnectFrontendPlayerSlotBytes> bytes{};
};

struct ConnectFrontendHandshakeHeader {
    u32 magic = kConnectFrontendMagicJwar;
    u32 version = 0;
};

struct ConnectFrontendConfiguration {
    std::vector<std::string> rows;
    std::string wizard_title;
    std::string p2p_title;
    std::string selected_mode_title;
    std::string default_server_address;
    std::string patcher_executable_name;
    std::string patcher_data_name;
    u32 session_version = 0;
    u32 server_port = 0;
    u32 p2p_tcp_port = 0;
    u32 free_server_port = 0;
    u32 p2p_udp_port = 0;
    u32 max_players = 0;
    bool loaded = false;
};

struct ConnectFrontendState;

using ConnectFrontendActionCallback = void (*)(ConnectFrontendState& state);
using ConnectFrontendMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);

struct ConnectFrontendCallbacks {
    ConnectFrontendActionCallback resume_worker_modal = nullptr;
    ConnectFrontendActionCallback open_wizard_soft_net = nullptr;
    ConnectFrontendActionCallback open_p2p_lobby = nullptr;
    ConnectFrontendActionCallback open_free_server = nullptr;
    ConnectFrontendActionCallback open_ipx_lobby = nullptr;
    ConnectFrontendActionCallback shutdown_network_mode = nullptr;
    ConnectFrontendMessageCallback show_message = nullptr;
};

struct ConnectFrontendState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    AsyncComContext* async_context = nullptr;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;
    BitmapIconResourceCollection* icon_collection = nullptr;

    IndexedTextTableContext layout_table;
    BitmapMemoryResource background;
    BitmapMemoryResource description_background;
    std::array<LegacyImageButtonControl, 7> buttons{};
    ConnectFrontendLayoutRect window_rect{};

    std::array<ConnectFrontendPlayerSlot, kConnectFrontendPlayerSlotCount> player_slots{};
    ConnectFrontendHandshakeHeader handshake{};
    ConnectFrontendConfiguration configuration{};
    std::array<std::string, 4> mode_descriptions{};
    std::array<std::array<u32, 4>, 4> mode_signatures{};
    std::array<std::array<u32, 4>, 4> enabled_signature{};

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    ConnectFrontendMode selected_mode = ConnectFrontendMode::WizardSoftNet;
    bool visible = false;
    bool modal_wait_active = false;
    bool p2p_udp_failed = false;
    std::string last_message;
    ConnectFrontendCallbacks callbacks{};
};

ConnectFrontendState& connect_frontend_state();

void ConstructConnectFrontendLayoutTable(ConnectFrontendState& state);
void InitializeConnectFrontendLayoutTable(ConnectFrontendState& state);
void RegisterConnectFrontendLayoutTableShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendLayoutTable(ConnectFrontendState& state);
void InitializeConnectFrontendBackgroundResourceAndShutdown(
    ConnectFrontendState& state);
void InitializeConnectFrontendBackgroundBitmap(ConnectFrontendState& state);
void RegisterConnectFrontendBackgroundShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendBackgroundBitmap(ConnectFrontendState& state);
void InitializeConnectFrontendDescriptionResourceAndShutdown(
    ConnectFrontendState& state);
void InitializeConnectFrontendDescriptionBitmap(ConnectFrontendState& state);
void RegisterConnectFrontendDescriptionShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendDescriptionBitmap(ConnectFrontendState& state);
void InitializeConnectFrontendWizardButtonSupport(ConnectFrontendState& state);
void InitializeConnectFrontendWizardButton(ConnectFrontendState& state);
void RegisterConnectFrontendWizardButtonShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendWizardButton(ConnectFrontendState& state);
void InitializeConnectFrontendP2PButtonSupport(ConnectFrontendState& state);
void InitializeConnectFrontendP2PButton(ConnectFrontendState& state);
void RegisterConnectFrontendP2PButtonShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendP2PButton(ConnectFrontendState& state);
void InitializeConnectFrontendFreeServerButtonSupport(ConnectFrontendState& state);
void InitializeConnectFrontendFreeServerButton(ConnectFrontendState& state);
void RegisterConnectFrontendFreeServerButtonShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendFreeServerButton(ConnectFrontendState& state);
void InitializeConnectFrontendIpxButtonSupport(ConnectFrontendState& state);
void InitializeConnectFrontendIpxButton(ConnectFrontendState& state);
void RegisterConnectFrontendIpxButtonShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendIpxButton(ConnectFrontendState& state);
void InitializeConnectFrontendDescriptionButtonSupport(ConnectFrontendState& state);
void InitializeConnectFrontendDescriptionButton(ConnectFrontendState& state);
void RegisterConnectFrontendDescriptionButtonShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendDescriptionButton(ConnectFrontendState& state);
void InitializeConnectFrontendOkButtonSupport(ConnectFrontendState& state);
void InitializeConnectFrontendOkButton(ConnectFrontendState& state);
void RegisterConnectFrontendOkButtonShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendOkButton(ConnectFrontendState& state);
void InitializeConnectFrontendCancelButtonSupport(ConnectFrontendState& state);
void InitializeConnectFrontendCancelButton(ConnectFrontendState& state);
void RegisterConnectFrontendCancelButtonShutdown(ConnectFrontendState& state);
void ShutdownConnectFrontendCancelButton(ConnectFrontendState& state);
void ResumeConnectFrontendModalWorker(ConnectFrontendState& state);
void ResetConnectFrontendPlayerSlots(ConnectFrontendState& state);
void InitializeConnectFrontendHandshakeHeader(ConnectFrontendState& state,
    u32 version);
void WaitLegacyConnectFrontendTicks(u32 milliseconds);
bool CreateConnectFrontendWindow(ConnectFrontendState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context);
int CompareConnectFrontendModeSignature(const std::array<u32, 4>& left,
    const std::array<u32, 4>& right);
bool IsConnectFrontendModeEnabled(const ConnectFrontendState& state,
    ConnectFrontendMode mode);
void RedrawConnectFrontendModeButtons(ConnectFrontendState& state);
LRESULT HandleConnectFrontendWindowMessage(ConnectFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleConnectFrontendButtonMessage(ConnectFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
bool LoadConnectFrontendConfiguration(ConnectFrontendState& state);

#endif

}
