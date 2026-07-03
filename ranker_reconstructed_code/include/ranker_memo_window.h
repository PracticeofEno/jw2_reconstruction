#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"
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

constexpr UINT kMemoNetworkMessage = 0x465;
constexpr UINT kMemoReconnectMessage = 0x503;
constexpr UINT kMemoPromptMessage0 = 0x512;
constexpr UINT kMemoPromptMessage1 = 0x513;
constexpr UINT kMemoPromptMessage2 = 0x514;
constexpr UINT kMemoPromptMessage3 = 0x515;
constexpr UINT kMemoPromptEndMessage = 0x516;

constexpr int kMemoTabBackgroundButtonId = 0x27d9;
constexpr int kMemoFriendTabButtonId = 0x27da;
constexpr int kMemoGuildTabButtonId = 0x27db;
constexpr int kMemoInboxListId = 0x27dc;
constexpr int kMemoInboxScrollId = 0x27dd;
constexpr int kMemoReadEditId = 0x27de;
constexpr int kMemoRecipientListId = 0x27df;
constexpr int kMemoRecipientScrollId = 0x27e0;
constexpr int kMemoWriteEditId = 0x27e1;
constexpr int kMemoDeleteReceivedButtonId = 0x27e2;
constexpr int kMemoDeleteFriendButtonId = 0x27e3;
constexpr int kMemoSendButtonId = 0x27e4;
constexpr int kMemoCloseButtonId = 0x27e5;

constexpr u32 kMemoLayoutTrcRecord = 0x172;
constexpr u32 kMemoBackgroundBitmapRecord = 0x139;
constexpr u32 kMemoFriendTabBitmapRecord = 0x13a;
constexpr u32 kMemoGuildTabBitmapRecord = 0x13b;
constexpr u32 kMemoScrollUpBitmapRecord = 0x13c;
constexpr u32 kMemoScrollDownBitmapRecord = 0x13d;
constexpr u32 kMemoScrollTrackBitmapRecord = 0x13e;
constexpr u32 kMemoScrollThumbBitmapRecord = 0x13f;
constexpr u32 kMemoDeleteReceivedNormalBitmapRecord = 0x140;
constexpr u32 kMemoDeleteReceivedPressedBitmapRecord = 0x141;
constexpr u32 kMemoDeleteFriendNormalBitmapRecord = 0x142;
constexpr u32 kMemoDeleteFriendPressedBitmapRecord = 0x143;
constexpr u32 kMemoSendNormalBitmapRecord = 0x144;
constexpr u32 kMemoSendPressedBitmapRecord = 0x145;
constexpr u32 kMemoCloseNormalBitmapRecord = 0x146;
constexpr u32 kMemoClosePressedBitmapRecord = 0x147;
constexpr int kMemoAcceleratorResourceId = 0x3fc;

struct MemoWindowState;

struct MemoLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct MemoTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct MemoInboxEntry {
    u32 memo_id = 0;
    std::array<char, 0x20> sender{};
    std::array<char, 0x14> date{};
    bool read = false;
};

using MemoPacketCallback = void (*)(MemoWindowState& state,
    const void* packet, i32 byte_count);
using MemoPayloadCallback = const u8* (*)(MemoWindowState& state,
    i32& byte_count);
using MemoActionCallback = void (*)(MemoWindowState& state);
using MemoMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color, void* user_data);
using MemoPayloadRouteCallback = void (*)(MemoWindowState& state,
    WPARAM wparam, LPARAM lparam);

struct MemoWindowCallbacks {
    MemoPacketCallback queue_packet = nullptr;
    MemoPayloadCallback receive_payload = nullptr;
    MemoActionCallback play_click_sound = nullptr;
    MemoActionCallback return_to_online_lobby = nullptr;
    MemoActionCallback close_async_socket = nullptr;
    MemoMessageCallback show_message = nullptr;
    MemoPayloadRouteCallback forward_network_message = nullptr;
    void* user_data = nullptr;
};

struct MemoWindowState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    BitmapMemoryResource active_tab_bitmap;
    LegacyCustomScrollControl inbox_scroll;
    LegacyCustomScrollControl recipient_scroll;

    MemoTextControl inbox_list;
    MemoTextControl read_edit;
    MemoTextControl recipient_list;
    MemoTextControl write_edit;

    LegacyImageButtonControl tab_background_button;
    LegacyImageButtonControl friend_tab_button;
    LegacyImageButtonControl guild_tab_button;
    LegacyImageButtonControl delete_received_button;
    LegacyImageButtonControl delete_friend_button;
    LegacyImageButtonControl send_button;
    LegacyImageButtonControl close_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<MemoLayoutRect> layout;
    std::vector<MemoInboxEntry> inbox_entries;
    std::vector<std::array<char, 0x20>> friend_recipients;
    std::vector<std::array<char, 0x20>> guild_recipients;
    int current_recipient_tab = 0;
    int inbox_visible_rows = 1;
    int recipient_visible_rows = 1;
    MemoWindowCallbacks callbacks{};
};

MemoWindowState& memo_window_state();

void MemoStaticResourceWrapperNN();
void InitializeMemoWindowResources(MemoWindowState& state);
void ReleaseMemoWindowResources(MemoWindowState& state);
void InstallMemoWindowAccelerators(MemoWindowState& state);
void RestoreMemoWindowAccelerators(MemoWindowState& state);
bool CreateMemoWindow(MemoWindowState& state, HWND parent, HINSTANCE instance,
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr);
void PopulateMemoInboxList(MemoWindowState& state);
void PopulateMemoRecipientList(MemoWindowState& state, i32 tab);
void DispatchMemoNetworkMessage(MemoWindowState& state, WPARAM wparam,
    LPARAM lparam);
LRESULT HandleMemoWindowMessage(MemoWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleMemoControlMessage(MemoWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
