#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
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

constexpr UINT kIpxLobbyDirectPlayPayloadMessage = 0x509;
constexpr UINT kIpxLobbyStartGameMessage = 0x50b;
constexpr UINT kIpxLobbyJoinErrorMessage = 0x50c;
constexpr UINT kIpxLobbyJoinStatusMessage = 0x50d;
constexpr UINT kIpxLobbyConnectionLostMessage = 0x502;
constexpr UINT kIpxLobbySessionLostMessage = 0x503;

constexpr int kIpxLobbySessionNameEditId = 0x0dac;
constexpr int kIpxLobbyPasswordEditId = 0x0dad;
constexpr int kIpxLobbyFocusSessionNameCommandId = 0x0dae;
constexpr int kIpxLobbyFocusPasswordCommandId = 0x0daf;
constexpr int kIpxLobbyCreateButtonId = 0x0db0;
constexpr int kIpxLobbyJoinButtonId = 0x0db1;
constexpr int kIpxLobbySessionListId = 0x0db2;
constexpr int kIpxLobbyInfoButtonId = 0x0db3;
constexpr int kIpxLobbyScrollControlId = 0x0db4;
constexpr int kIpxLobbyCancelButtonId = IDCANCEL;
constexpr int kIpxLobbyAcceleratorResourceId = 0x15e;

constexpr u32 kIpxLobbySessionMagic = 0x5241574a;
constexpr u32 kIpxLobbyLayoutTrcRecord = 0x167;
constexpr u32 kIpxLobbyBackgroundBitmapRecord = 0x0a2;
constexpr u32 kIpxLobbyCreateNormalBitmapRecord = 0x0a3;
constexpr u32 kIpxLobbyCreatePressedBitmapRecord = 0x0a4;
constexpr u32 kIpxLobbyJoinNormalBitmapRecord = 0x0a5;
constexpr u32 kIpxLobbyJoinPressedBitmapRecord = 0x0a6;
constexpr u32 kIpxLobbyCancelNormalBitmapRecord = 0x0a7;
constexpr u32 kIpxLobbyCancelPressedBitmapRecord = 0x0a8;
constexpr u32 kIpxLobbyIconRecord0 = 0x0a9;
constexpr u32 kIpxLobbyIconRecord1 = 0x0aa;
constexpr u32 kIpxLobbyIconRecord2 = 0x0ab;
constexpr u32 kIpxLobbyIconRecord3 = 0x0ac;
constexpr UINT_PTR kIpxLobbyRefreshTimerId = 1;
constexpr UINT kIpxLobbyRefreshTimerMs = 2000;
constexpr std::size_t kIpxLobbyPlayerDataBytes = 0x18a;
constexpr std::size_t kIpxLobbyPlayerNameOffset = 0x60;
constexpr std::size_t kIpxLobbyPlayerNameBytes = 0x20;
constexpr std::size_t kIpxLobbyHostPlayerDataBytes = 0x2dc;

enum class IpxLobbyJoinPhase : u32 {
    Idle = 0,
    JoinRequested = 1,
    WaitingForStart = 2,
    Busy = 3,
};

enum class IpxLobbySessionStatus : u32 {
    None = 0,
    Joinable = 1,
    Blocked = 2,
    BadMagic = 3,
    VersionMismatch = 4,
};

struct IpxLobbyState;

struct IpxLobbyLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct IpxLobbyControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct IpxLobbySessionListItem {
    std::string name;
    GUID instance{};
    GUID application{};
    u32 flags = 0;
    u32 magic = 0;
    u32 version = 0;
    DPID host_player_id = 0;
    bool password_required = false;
    bool joinable = false;
};

struct IpxLobbyDirectPlayMessage {
    u32 type = 0;
    std::vector<u8> bytes;
};

struct IpxLobbyCallbacks {
    bool (*initialize_browser_connection)(IpxLobbyState& state,
        AsyncComContext& browser_context) = nullptr;
    void (*open_create_game)(IpxLobbyState& state) = nullptr;
    void (*open_connect_frontend)(IpxLobbyState& state) = nullptr;
    void (*start_game)(IpxLobbyState& state) = nullptr;
    void (*show_message)(HWND owner, const char* text, COLORREF color) = nullptr;
    void (*set_busy)(BOOL busy) = nullptr;
    HRESULT (*join_session)(IpxLobbyState& state,
        const IpxLobbySessionListItem& session) = nullptr;
    void (*handle_payload)(IpxLobbyState& state,
        const IpxLobbyDirectPlayMessage& message) = nullptr;
};

struct IpxLobbyState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    AsyncComContext* async_context = nullptr;
    AsyncComContext browser_context{};

    BitmapMemoryResource background;
    IpxLobbyControl session_name_edit;
    IpxLobbyControl password_edit;
    IpxLobbyControl session_list;
    LegacyCustomScrollControl scroll_control;
    LegacyImageButtonControl info_button;
    LegacyImageButtonControl create_button;
    LegacyImageButtonControl join_button;
    LegacyImageButtonControl cancel_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<char, 0x14> session_name{};
    std::array<char, 10> password{};
    std::array<char, kIpxLobbyPlayerNameBytes> local_player_name{};
    std::array<u8, kIpxLobbyPlayerDataBytes> local_player_data{};
    std::array<u8, kIpxLobbyHostPlayerDataBytes> selected_game_data{};
    std::vector<IpxLobbySessionListItem> sessions;
    std::string info_text;
    std::string last_message;

    UINT_PTR refresh_timer = 0;
    int selected_session = -1;
    int visible_count = 0;
    u32 expected_version = 0;
    u32 remote_version = 0;
    IpxLobbyJoinPhase join_phase = IpxLobbyJoinPhase::Idle;
    IpxLobbySessionStatus selected_status = IpxLobbySessionStatus::None;
    bool refresh_in_progress = false;
    bool start_game_requested = false;
    bool selected_game_data_valid = false;
    bool visible = false;
    IpxLobbyCallbacks callbacks{};
};

IpxLobbyState& ipx_lobby_state();

void InitializeIpxLobbyBackgroundResourceAndShutdown(IpxLobbyState& state);
void InitializeIpxLobbyBackgroundBitmap(IpxLobbyState& state);
void RegisterIpxLobbyBackgroundShutdown(IpxLobbyState& state);
void ShutdownIpxLobbyBackgroundBitmap(IpxLobbyState& state);
void InitializeIpxLobbyInfoButtonSupport(IpxLobbyState& state);
void InitializeIpxLobbyInfoButton(IpxLobbyState& state);
void RegisterIpxLobbyInfoButtonShutdown(IpxLobbyState& state);
void ShutdownIpxLobbyInfoButton(IpxLobbyState& state);
void InitializeIpxLobbyCreateButtonSupport(IpxLobbyState& state);
void InitializeIpxLobbyCreateButton(IpxLobbyState& state);
void RegisterIpxLobbyCreateButtonShutdown(IpxLobbyState& state);
void ShutdownIpxLobbyCreateButton(IpxLobbyState& state);
void InitializeIpxLobbyJoinButtonSupport(IpxLobbyState& state);
void InitializeIpxLobbyJoinButton(IpxLobbyState& state);
void RegisterIpxLobbyJoinButtonShutdown(IpxLobbyState& state);
void ShutdownIpxLobbyJoinButton(IpxLobbyState& state);
void InitializeIpxLobbyCancelButtonSupport(IpxLobbyState& state);
void InitializeIpxLobbyCancelButton(IpxLobbyState& state);
void RegisterIpxLobbyCancelButtonShutdown(IpxLobbyState& state);
void ShutdownIpxLobbyCancelButton(IpxLobbyState& state);
void InitializeIpxLobbyScrollControlSupport(IpxLobbyState& state);
void InitializeIpxLobbyScrollControl(IpxLobbyState& state);
void RegisterIpxLobbyScrollControlShutdown(IpxLobbyState& state);
void ShutdownIpxLobbyScrollControl(IpxLobbyState& state);

void InstallIpxLobbyAccelerators(IpxLobbyState& state);
void RestoreIpxLobbyAccelerators(IpxLobbyState& state);
void ShowIpxLobbyStatusMessage(IpxLobbyState& state, const char* text,
    COLORREF color = RGB(250, 250, 250));
bool SubmitIpxLobbySelectedSession(IpxLobbyState& state);
bool CreateIpxLobbyWindow(IpxLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context, AsyncComContext* async_context);
LRESULT HandleIpxLobbyWindowMessage(IpxLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
void ReleaseIpxLobbyButton(LegacyImageButtonControl& button, bool free_storage);
void DeleteIpxLobbyScrollControl(IpxLobbyState& state, bool free_storage);
LRESULT HandleIpxLobbyControlMessage(IpxLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
void RefreshIpxLobbySessionList(IpxLobbyState& state);
IpxLobbySessionStatus ValidateIpxLobbySelectedSession(IpxLobbyState& state,
    int list_index);
void ReportIpxLobbyDirectPlayJoinError(IpxLobbyState& state, const void* packet,
    std::size_t packet_size);
void DispatchIpxLobbyDirectPlayPayload(IpxLobbyState& state, WPARAM sender,
    LPARAM payload);

#endif

}
