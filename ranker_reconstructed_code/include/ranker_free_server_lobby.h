#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr UINT kFreeServerNetworkMessage = 0x465;
constexpr UINT kFreeServerSocketPayloadMessage = 0x50a;
constexpr UINT kFreeServerStartGameMessage = 0x50b;
constexpr UINT kFreeServerJoinErrorMessage = 0x50c;
constexpr UINT kFreeServerJoinStatusMessage = 0x50d;
constexpr UINT kFreeServerSocketNotifyMessage = 0x50f;

constexpr int kFreeServerNameEditId = 0x1b58;
constexpr int kFreeServerPasswordEditId = 0x1b59;
constexpr int kFreeServerInfoButtonId = 0x1b5a;
constexpr int kFreeServerGameTypeComboId = 0x1b5b;
constexpr int kFreeServerGameListId = 0x1b5c;
constexpr int kFreeServerScrollControlId = 0x1b5d;
constexpr int kFreeServerFocusNameCommandId = 0x1b5e;
constexpr int kFreeServerFocusPasswordCommandId = 0x1b5f;
constexpr int kFreeServerJoinButtonId = 0x1b60;
constexpr int kFreeServerCancelButtonId = IDCANCEL;
constexpr int kFreeServerAcceleratorResourceId = 0x2bc;

constexpr u32 kFreeServerLayoutTrcRecord = 0x168;
constexpr u32 kFreeServerBackgroundBitmapRecord = 0x77;
constexpr u32 kFreeServerJoinNormalBitmapRecord = 0x79;
constexpr u32 kFreeServerJoinPressedBitmapRecord = 0x78;
constexpr u32 kFreeServerCancelNormalBitmapRecord = 0x7a;
constexpr u32 kFreeServerCancelPressedBitmapRecord = 0x7b;
constexpr u32 kFreeServerIconRecord0 = 0x7c;
constexpr u32 kFreeServerIconRecord1 = 0x7f;
constexpr u32 kFreeServerIconRecord2 = 0x7d;
constexpr u32 kFreeServerIconRecord3 = 0x7e;
constexpr u32 kFreeServerComboBitmapRecord = 0x80;
constexpr UINT_PTR kFreeServerJoinTimerId = 1;
constexpr UINT kFreeServerJoinRetryMs = 2000;

enum class FreeServerInfoState : u32 {
    Empty = 0,
    Selected = 1,
    PasswordRequired = 2,
    BadSession = 3,
    VersionMismatch = 4,
};

enum class FreeServerJoinPhase : u32 {
    Idle = 0,
    Connecting = 1,
    WaitingForStart = 2,
};

struct FreeServerLobbyState;

struct FreeServerLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct FreeServerControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct FreeServerGameEntry {
    int id = -1;
    int game_type = 0;
    int icon_slot = 0;
    int width = 0;
    int height = 0;
    int display_mode = 0;
    u32 version = 0;
    u32 address = 0;
    u16 port = 0;
    bool password_required = false;
    bool joinable = true;
    std::string name;
    std::string host_name;
    std::string map_name;
    std::string description;
};

struct FreeServerSocketPayload {
    u32 type = 0;
    std::vector<u8> bytes;
};

struct FreeServerLobbyCallbacks {
    void (*open_connect_frontend)(FreeServerLobbyState& state) = nullptr;
    void (*start_game)(FreeServerLobbyState& state) = nullptr;
    void (*show_message)(HWND owner, const char* text, COLORREF color) = nullptr;
    void (*set_busy)(BOOL busy) = nullptr;
    void (*queue_server_packet)(FreeServerLobbyState& state, const void* packet,
        i32 byte_count) = nullptr;
    void (*handle_socket_payload)(FreeServerLobbyState& state,
        const FreeServerSocketPayload& payload) = nullptr;
    void (*handle_server_payload)(FreeServerLobbyState& state, const u8* payload,
        i32 byte_count) = nullptr;
};

struct FreeServerLobbyState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;
    SOCKET game_socket = INVALID_SOCKET;

    BitmapMemoryResource background;
    FreeServerControl name_edit;
    FreeServerControl password_edit;
    FreeServerControl game_list;
    LegacyCustomScrollControl scroll_control;
    LegacyImageButtonControl info_button;
    LegacyImageButtonControl join_button;
    LegacyImageButtonControl cancel_button;
    LegacyImageComboBoxControl game_type_combo;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<char, 0x14> player_name{};
    std::array<char, 10> password{};
    std::array<u8, 0x196> launch_context{};
    std::array<u32, 3> server_top_bottom_counts{};
    std::array<u32, 10> server_use_map_counts{};
    std::vector<FreeServerGameEntry> games;
    std::string last_message;
    std::string info_text;

    UINT_PTR join_timer = 0;
    int selected_index = -1;
    int selected_game_type = 0;
    int visible_count = 0;
    u32 expected_version = 0;
    u32 remote_version = 0;
    bool list_update_in_progress = false;
    bool game_start_requested = false;
    bool visible = false;
    FreeServerInfoState info_state = FreeServerInfoState::Empty;
    FreeServerJoinPhase join_phase = FreeServerJoinPhase::Idle;
    FreeServerLobbyCallbacks callbacks{};
};

FreeServerLobbyState& free_server_lobby_state();

void InitializeFreeServerBackgroundResourceAndShutdown(FreeServerLobbyState& state);
void InitializeFreeServerBackgroundBitmap(FreeServerLobbyState& state);
void RegisterFreeServerBackgroundShutdown(FreeServerLobbyState& state);
void ShutdownFreeServerBackgroundBitmap(FreeServerLobbyState& state);
void InitializeFreeServerInfoButtonSupport(FreeServerLobbyState& state);
void InitializeFreeServerInfoButton(FreeServerLobbyState& state);
void RegisterFreeServerInfoButtonShutdown(FreeServerLobbyState& state);
void ShutdownFreeServerInfoButton(FreeServerLobbyState& state);
void InitializeFreeServerJoinButtonSupport(FreeServerLobbyState& state);
void InitializeFreeServerJoinButton(FreeServerLobbyState& state);
void RegisterFreeServerJoinButtonShutdown(FreeServerLobbyState& state);
void ShutdownFreeServerJoinButton(FreeServerLobbyState& state);
void InitializeFreeServerCancelButtonSupport(FreeServerLobbyState& state);
void InitializeFreeServerCancelButton(FreeServerLobbyState& state);
void RegisterFreeServerCancelButtonShutdown(FreeServerLobbyState& state);
void ShutdownFreeServerCancelButton(FreeServerLobbyState& state);
void InitializeFreeServerScrollControlSupport(FreeServerLobbyState& state);
void InitializeFreeServerScrollControl(FreeServerLobbyState& state);
void RegisterFreeServerScrollControlShutdown(FreeServerLobbyState& state);
void ShutdownFreeServerScrollControl(FreeServerLobbyState& state);
void InitializeFreeServerGameTypeComboSupport(FreeServerLobbyState& state);
void InitializeFreeServerGameTypeCombo(FreeServerLobbyState& state);
void RegisterFreeServerGameTypeComboShutdown(FreeServerLobbyState& state);
void ShutdownFreeServerGameTypeCombo(FreeServerLobbyState& state);

void InstallFreeServerAccelerators(FreeServerLobbyState& state);
void RestoreFreeServerAccelerators(FreeServerLobbyState& state);
void ShowFreeServerSelectGameMessage(FreeServerLobbyState& state);
bool SubmitFreeServerJoinRequest(FreeServerLobbyState& state);
bool CreateFreeServerLobbyWindow(FreeServerLobbyState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context);
LRESULT HandleFreeServerLobbyWindowMessage(FreeServerLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
void DeleteFreeServerGameTypeComboBox(FreeServerLobbyState& state, bool free_storage);
LRESULT HandleFreeServerLobbyControlMessage(FreeServerLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
void ReportFreeServerJoinError(FreeServerLobbyState& state, const void* packet,
    std::size_t packet_size);
bool AddFreeServerLobbyEntry(FreeServerLobbyState& state, const char* name,
    const void* game_info, const void* display_info, u32 game_id,
    const void* map_descriptor);
bool RemoveFreeServerLobbyEntryById(FreeServerLobbyState& state, int id);
void SelectFreeServerLobbyEntry(FreeServerLobbyState& state, HWND listbox);
void ClearFreeServerLobbyEntries(FreeServerLobbyState& state, HWND listbox);
void DispatchFreeServerSocketPayload(FreeServerLobbyState& state, WPARAM sender,
    LPARAM payload);
void PumpFreeServerSocketReceiveQueue(FreeServerLobbyState& state,
    LegacySocketRecord& record);
void HandleFreeServerSocketMessage(FreeServerLobbyState& state, SOCKET socket,
    LPARAM event);
void DispatchFreeServerNetworkMessage(FreeServerLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
