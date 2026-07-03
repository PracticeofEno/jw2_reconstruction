#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr UINT kChangeLobbyNetworkMessage = 0x465;
constexpr UINT kChangeLobbyStatusMessage0 = 0x512;
constexpr UINT kChangeLobbyStatusMessage1 = 0x513;
constexpr UINT kChangeLobbyStatusMessage2 = 0x514;
constexpr UINT kChangeLobbyStatusMessage3 = 0x515;
constexpr UINT kChangeLobbyBusyMessage = 0x516;

constexpr int kChangeLobbyNameEditId = 0x5dc;
constexpr int kChangeLobbyListId = 0x5de;
constexpr int kChangeLobbyScrollControlId = 0x5df;
constexpr int kChangeLobbyFocusNameCommandId = 0x5e0;
constexpr int kChangeLobbyChangeButtonId = 0x5e1;
constexpr int kChangeLobbyPasswordEditId = 0x5e2;
constexpr int kChangeLobbyCancelButtonId = IDCANCEL;
constexpr int kChangeLobbyAcceleratorResourceId = 0x96;

constexpr u32 kChangeLobbyLayoutTrcRecord = 0x161;
constexpr u32 kChangeLobbyBackgroundBitmapTrcRecord = 0xc2;
constexpr u32 kChangeLobbyChangeNormalBitmapRecord = 0xc6;
constexpr u32 kChangeLobbyChangePressedBitmapRecord = 0xc5;
constexpr u32 kChangeLobbyCancelNormalBitmapRecord = 0xc3;
constexpr u32 kChangeLobbyCancelPressedBitmapRecord = 0xc4;
constexpr std::size_t kChangeLobbyListRequestPacketBytes = 0x11;
constexpr std::size_t kChangeLobbyJoinPacketBytes = 0x31;
constexpr std::size_t kChangeLobbyCreatePacketBytes = 0xad;

struct ChangeLobbyState;

struct ChangeLobbyListItem {
    std::string name;
    int lobby_id = -1;
    int icon_slot = 0;
};

using ChangeLobbyPacketCallback = void (*)(ChangeLobbyState& state,
    const void* packet, i32 byte_count);

struct ChangeLobbyCallbacks {
    void (*request_lobby_list)(ChangeLobbyState& state, int start_index) = nullptr;
    void (*change_lobby)(ChangeLobbyState& state, const char* name,
        const char* password, bool existing_lobby, int lobby_id) = nullptr;
    ChangeLobbyPacketCallback queue_packet = nullptr;
    void (*change_succeeded)(ChangeLobbyState& state) = nullptr;
    void (*handle_network_message)(ChangeLobbyState& state, WPARAM wparam,
        LPARAM lparam) = nullptr;
    void (*forward_network_message)(HWND parent, UINT message, WPARAM wparam,
        LPARAM lparam) = nullptr;
    void (*show_message)(HWND owner, const char* text, COLORREF color) = nullptr;
    void (*set_busy)(BOOL busy) = nullptr;
    void (*return_to_parent)(HWND parent, HINSTANCE instance, LPARAM context) = nullptr;
    void (*focus_parent_control)(ChangeLobbyState& state) = nullptr;
};

struct ChangeLobbyControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct ChangeLobbyLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ChangeLobbyState {
    BitmapMemoryResource background;
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;

    ChangeLobbyControl name_edit;
    ChangeLobbyControl password_edit;
    ChangeLobbyControl lobby_list;
    LegacyCustomScrollControl scroll_control;
    LegacyImageButtonControl change_button;
    LegacyImageButtonControl cancel_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<char, 0x80> pending_lobby_name{};
    std::array<char, 0x20> pending_password{};
    std::vector<ChangeLobbyListItem> items;
    std::string last_status_text;

    int visible_item_capacity = 0;
    bool visible = false;
    ChangeLobbyCallbacks callbacks;
};

ChangeLobbyState& change_lobby_state();

void InitializeChangeLobbySupport(ChangeLobbyState& state);
void InitializeChangeLobbyBackgroundBitmapSupport();
void InitializeChangeLobbyBackgroundBitmap(ChangeLobbyState& state);
void RegisterChangeLobbyBackgroundBitmapShutdown(ChangeLobbyState& state);
void RegisterChangeLobbyBackgroundShutdown(ChangeLobbyState& state);
void ShutdownChangeLobbyBackgroundBitmap(ChangeLobbyState& state);
void InitializeChangeLobbyChangeButtonSupport();
void InitializeChangeLobbyChangeButton(ChangeLobbyState& state);
void RegisterChangeLobbyChangeButtonShutdown(ChangeLobbyState& state);
void ShutdownChangeLobbyChangeButton(ChangeLobbyState& state);
void InitializeChangeLobbyCancelButtonSupport();
void InitializeChangeLobbyCancelButton(ChangeLobbyState& state);
void RegisterChangeLobbyCancelButtonShutdown(ChangeLobbyState& state);
void ShutdownChangeLobbyCancelButton(ChangeLobbyState& state);
void InitializeChangeLobbyScrollControlSupport();
void InitializeChangeLobbyScrollControl(ChangeLobbyState& state);
void RegisterChangeLobbyScrollControlShutdown(ChangeLobbyState& state);
void ShutdownChangeLobbyScrollControl(ChangeLobbyState& state);

void InstallChangeLobbyAccelerators(ChangeLobbyState& state);
void RestoreChangeLobbyAccelerators(ChangeLobbyState& state);

bool CreateChangeLobbyWindow(ChangeLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context);
LRESULT HandleChangeLobbyWindowMessage(ChangeLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT HandleChangeLobbyControlMessage(ChangeLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);

void ShowChangeLobbyNameRequiredMessage(ChangeLobbyState& state);
std::array<u8, kChangeLobbyListRequestPacketBytes> BuildChangeLobbyListRequestPacket(
    int start_index);
std::array<u8, kChangeLobbyJoinPacketBytes> BuildChangeLobbyJoinPacket(
    const char* password, int lobby_id);
std::array<u8, kChangeLobbyCreatePacketBytes> BuildChangeLobbyCreatePacket(
    const char* name, const char* password);
void QueueChangeLobbyListRequest(ChangeLobbyState& state, int start_index);
void SubmitChangeLobbySelection(ChangeLobbyState& state);
void AddChangeLobbyListEntry(ChangeLobbyState& state, const char* name, int lobby_id,
    int icon_slot);
void RemoveChangeLobbyListEntryById(ChangeLobbyState& state, int lobby_id);
void ClearChangeLobbyListItemData(HWND listbox);
void ClearChangeLobbyListEntries(ChangeLobbyState& state);
bool ApplyChangeLobbyListPacket(ChangeLobbyState& state, const void* packet,
    std::size_t byte_count);
bool DispatchChangeLobbyServerPacket(ChangeLobbyState& state, const void* packet,
    std::size_t byte_count);
void DispatchChangeLobbyNetworkMessage(ChangeLobbyState& state, WPARAM wparam,
    LPARAM lparam);

#endif

}
