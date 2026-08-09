#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_bitmap_tile_sheet.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <richole.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr u32 kOnlineLobbyLayoutTrcRecord = 0x16e;
constexpr u32 kOnlineLobbyBackgroundBitmapRecord = 0x12;
constexpr int kOnlineLobbyNameButtonId = 3000;
constexpr int kOnlineLobbyGameListId = 0xbb9;
constexpr int kOnlineLobbyGameListScrollId = 0xbba;
constexpr int kOnlineLobbyChatListId = 0xbbb;
constexpr int kOnlineLobbyChatListScrollId = 0xbbc;
constexpr int kOnlineLobbyChatEditId = 0xbbd;
constexpr int kOnlineLobbySendButtonId = 0xbbe;
constexpr int kOnlineLobbyWhisperButtonId = 0xbbf;
constexpr int kOnlineLobbyEmoticonButtonId = 0xbc0;
constexpr int kOnlineLobbyMainTabButtonId = 0xbc2;
constexpr int kOnlineLobbyFriendsTabButtonId = 0xbc3;
constexpr int kOnlineLobbyGuildTabButtonId = 0xbc4;
constexpr int kOnlineLobbyPersonalTabButtonId = 0xbc5;
constexpr int kOnlineLobbyChangeLobbyButtonId = 0xbc6;
constexpr int kOnlineLobbyCreateGameButtonId = 0xbc7;
constexpr int kOnlineLobbyMyAvatarButtonId = 0xbc8;
constexpr int kOnlineLobbyJoinGameButtonId = 0xbc9;
constexpr int kOnlineLobbyViewRankButtonId = 0xbca;
constexpr int kOnlineLobbyFriendDisplayButtonId = 0xbcb;
constexpr int kOnlineLobbyFriendAddButtonId = 0xbcc;
constexpr int kOnlineLobbyFriendRemoveButtonId = 0xbcd;
constexpr int kOnlineLobbyFriendSendMessageButtonId = 0xbce;
constexpr int kOnlineLobbyFriendSendMemoButtonId = 0xbcf;
constexpr int kOnlineLobbyGuildDisplayButtonId = 0xbd0;
constexpr int kOnlineLobbyGuildSiteButtonId = 0xbd1;
constexpr int kOnlineLobbyGuildSendMessageButtonId = 0xbd2;
constexpr int kOnlineLobbyGuildSendMemoButtonId = 0xbd3;
constexpr int kOnlineLobbyGuildSubDisplayButtonId = 0xbd4;
constexpr int kOnlineLobbyGuildSubSiteButtonId = 0xbd5;
constexpr int kOnlineLobbyGuildSubSendMessageButtonId = 0xbd6;
constexpr int kOnlineLobbyGuildSubSendMemoButtonId = 0xbd7;
constexpr int kOnlineLobbyTabBackgroundButtonId = 0xbd8;
constexpr int kOnlineLobbyAcceleratorResourceId = 0x12c;
constexpr UINT kOnlineLobbyNetworkMessage = 0x465;
constexpr UINT kOnlineLobbyCopiedTextMessage = 0x501;
constexpr UINT kOnlineLobbyMakeSelectedIconBitmapMessage = 0x517;
constexpr std::size_t kOnlineLobbyButtonCount = 28;
// The reconstructed lobby exposes only the Main tab.  Keep the legacy button
// slots in the state layout for wire/UI compatibility, but present a real
// single-tab interface instead of reserving four visible tab cells.
constexpr std::size_t kOnlineLobbyTabCount = 1;
constexpr std::size_t kOnlineLobbyChatRowLimit = 300;

constexpr int OnlineLobbyButtonLayoutIndex(int button_spec_index) {
    return button_spec_index == 0 ? 1 : button_spec_index + 6;
}

enum class OnlineLobbyTab : int {
    Main = 0,
    Friends = 1,
    Guild = 2,
    Personal = 3,
};

struct LegacyAsyncTcpSocket;

struct OnlineLobbyLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct OnlineLobbyButtonSpec {
    int id = 0;
    const char* text = "";
    u32 normal_record = 0;
    u32 pressed_record = 0;
    bool hide_when_main_disabled = false;
};

struct OnlineLobbyScrollControl {
    LegacyCustomScrollControl control;
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct OnlineLobbyCallbacks {
    void (*return_to_connect_frontend)(HWND parent, HINSTANCE instance,
        LPARAM return_context, void* user_data) = nullptr;
    void (*open_change_lobby)(HWND parent, HINSTANCE instance,
        void* user_data) = nullptr;
    void (*open_search_lobby)(HWND parent, HINSTANCE instance,
        void* user_data) = nullptr;
    void (*open_create_game)(HWND parent, HINSTANCE instance,
        void* user_data) = nullptr;
    void (*open_free_server_lobby)(HWND parent, HINSTANCE instance,
        void* user_data) = nullptr;
    void (*open_view_rank)(HWND parent, HINSTANCE instance,
        void* user_data) = nullptr;
    void (*open_avatar)(HWND parent, HINSTANCE instance,
        void* user_data) = nullptr;
    void (*open_memo)(HWND parent, HINSTANCE instance,
        void* user_data) = nullptr;
    void (*open_emoticon_popup)(HWND parent, HINSTANCE instance,
        POINT screen_point, void* user_data) = nullptr;
    void (*send_async_packet)(const void* packet, std::size_t byte_count,
        void* user_data) = nullptr;
    void (*show_message)(HWND owner, const char* text, COLORREF color,
        void* user_data) = nullptr;
    void* user_data = nullptr;
};

struct OnlineLobbyState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    HWND game_list = nullptr;
    HWND chat_list = nullptr;
    HWND chat_edit = nullptr;
    WNDPROC game_list_original_proc = nullptr;
    WNDPROC chat_list_original_proc = nullptr;
    WNDPROC chat_edit_original_proc = nullptr;

    BitmapMemoryResource background;
    BitmapTileSheetSelector icon_sheet;
    std::array<LegacyImageButtonControl, kOnlineLobbyButtonCount> buttons{};
    std::array<OnlineLobbyScrollControl, 2> scroll_controls{};
    std::vector<OnlineLobbyLayoutRect> layout_rects;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    IRichEditOle* rich_edit_ole = nullptr;
    std::array<int, 0x20> inline_icon_offsets{};
    std::array<u32, 0x20> inline_icon_indices{};
    int active_tab = -1;
    int duplicate_chat_count = 0;
    u32 last_chat_tick = 0;
    u32 chat_silence_until_tick = 0;
    bool window_active = false;
    bool connected = true;
    bool create_join_disabled = false;
    bool resources_ready = false;
    std::string lobby_name;
    std::string local_player_name;
    std::string last_chat_line;
    std::array<u8, 0x40> pending_barter_summary{};
    std::array<char, 0x20> pending_prompt_name{};
    OnlineLobbyCallbacks callbacks{};
};

OnlineLobbyState& online_lobby_state();

void InitializeOnlineLobbyBackgroundBitmapStatic(OnlineLobbyState& state);
void InitializeOnlineLobbyBackgroundBitmap(OnlineLobbyState& state);
void RegisterOnlineLobbyBackgroundBitmapDestructor(OnlineLobbyState& state);
void DestroyOnlineLobbyBackgroundBitmap(OnlineLobbyState& state);

#define DECLARE_ONLINE_LOBBY_SCROLL_LIFETIME(StaticName, InitName, RegisterName, DestroyName) \
    void StaticName(OnlineLobbyState& state); \
    void InitName(OnlineLobbyState& state); \
    void RegisterName(OnlineLobbyState& state); \
    void DestroyName(OnlineLobbyState& state)

DECLARE_ONLINE_LOBBY_SCROLL_LIFETIME(InitializeOnlineLobbyGameListScrollStatic,
    InitializeOnlineLobbyGameListScrollControl,
    RegisterOnlineLobbyGameListScrollDestructor,
    DestroyOnlineLobbyGameListScrollControl);
DECLARE_ONLINE_LOBBY_SCROLL_LIFETIME(InitializeOnlineLobbyChatListScrollStatic,
    InitializeOnlineLobbyChatListScrollControl,
    RegisterOnlineLobbyChatListScrollDestructor,
    DestroyOnlineLobbyChatListScrollControl);

#undef DECLARE_ONLINE_LOBBY_SCROLL_LIFETIME

void InitializeOnlineLobbyScrollControls(OnlineLobbyState& state);
void DestroyOnlineLobbyScrollControls(OnlineLobbyState& state);

#define DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName) \
    void StaticName(OnlineLobbyState& state); \
    void InitName(OnlineLobbyState& state); \
    void RegisterName(OnlineLobbyState& state); \
    void DestroyName(OnlineLobbyState& state)

DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyLobbyNameButtonStatic,
    InitializeOnlineLobbyLobbyNameButton,
    RegisterOnlineLobbyLobbyNameButtonDestructor,
    DestroyOnlineLobbyLobbyNameButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbySendButtonStatic,
    InitializeOnlineLobbySendButton,
    RegisterOnlineLobbySendButtonDestructor,
    DestroyOnlineLobbySendButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyWhisperButtonStatic,
    InitializeOnlineLobbyWhisperButton,
    RegisterOnlineLobbyWhisperButtonDestructor,
    DestroyOnlineLobbyWhisperButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyEmoticonsButtonStatic,
    InitializeOnlineLobbyEmoticonsButton,
    RegisterOnlineLobbyEmoticonsButtonDestructor,
    DestroyOnlineLobbyEmoticonsButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyMainTabButtonStatic,
    InitializeOnlineLobbyMainTabButton,
    RegisterOnlineLobbyMainTabButtonDestructor,
    DestroyOnlineLobbyMainTabButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendsTabButtonStatic,
    InitializeOnlineLobbyFriendsTabButton,
    RegisterOnlineLobbyFriendsTabButtonDestructor,
    DestroyOnlineLobbyFriendsTabButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildTabButtonStatic,
    InitializeOnlineLobbyGuildTabButton,
    RegisterOnlineLobbyGuildTabButtonDestructor,
    DestroyOnlineLobbyGuildTabButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyPersonalTabButtonStatic,
    InitializeOnlineLobbyPersonalTabButton,
    RegisterOnlineLobbyPersonalTabButtonDestructor,
    DestroyOnlineLobbyPersonalTabButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyTabBackgroundButtonStatic,
    InitializeOnlineLobbyTabBackgroundButton,
    RegisterOnlineLobbyTabBackgroundButtonDestructor,
    DestroyOnlineLobbyTabBackgroundButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyChangeLobbyButtonStatic,
    InitializeOnlineLobbyChangeLobbyButton,
    RegisterOnlineLobbyChangeLobbyButtonDestructor,
    DestroyOnlineLobbyChangeLobbyButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyCreateGameButtonStatic,
    InitializeOnlineLobbyCreateGameButton,
    RegisterOnlineLobbyCreateGameButtonDestructor,
    DestroyOnlineLobbyCreateGameButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyJoinGameButtonStatic,
    InitializeOnlineLobbyJoinGameButton,
    RegisterOnlineLobbyJoinGameButtonDestructor,
    DestroyOnlineLobbyJoinGameButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyMyAvatarButtonStatic,
    InitializeOnlineLobbyMyAvatarButton,
    RegisterOnlineLobbyMyAvatarButtonDestructor,
    DestroyOnlineLobbyMyAvatarButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyViewRankButtonStatic,
    InitializeOnlineLobbyViewRankButton,
    RegisterOnlineLobbyViewRankButtonDestructor,
    DestroyOnlineLobbyViewRankButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyCancelButtonStatic,
    InitializeOnlineLobbyCancelButton,
    RegisterOnlineLobbyCancelButtonDestructor,
    DestroyOnlineLobbyCancelButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendDisplayButtonStatic,
    InitializeOnlineLobbyFriendDisplayButton,
    RegisterOnlineLobbyFriendDisplayButtonDestructor,
    DestroyOnlineLobbyFriendDisplayButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendAddButtonStatic,
    InitializeOnlineLobbyFriendAddButton,
    RegisterOnlineLobbyFriendAddButtonDestructor,
    DestroyOnlineLobbyFriendAddButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendRemoveButtonStatic,
    InitializeOnlineLobbyFriendRemoveButton,
    RegisterOnlineLobbyFriendRemoveButtonDestructor,
    DestroyOnlineLobbyFriendRemoveButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendSendMessageButtonStatic,
    InitializeOnlineLobbyFriendSendMessageButton,
    RegisterOnlineLobbyFriendSendMessageButtonDestructor,
    DestroyOnlineLobbyFriendSendMessageButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendSendMemoButtonStatic,
    InitializeOnlineLobbyFriendSendMemoButton,
    RegisterOnlineLobbyFriendSendMemoButtonDestructor,
    DestroyOnlineLobbyFriendSendMemoButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildDisplayButtonStatic,
    InitializeOnlineLobbyGuildDisplayButton,
    RegisterOnlineLobbyGuildDisplayButtonDestructor,
    DestroyOnlineLobbyGuildDisplayButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSiteButtonStatic,
    InitializeOnlineLobbyGuildSiteButton,
    RegisterOnlineLobbyGuildSiteButtonDestructor,
    DestroyOnlineLobbyGuildSiteButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSendMessageButtonStatic,
    InitializeOnlineLobbyGuildSendMessageButton,
    RegisterOnlineLobbyGuildSendMessageButtonDestructor,
    DestroyOnlineLobbyGuildSendMessageButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSendMemoButtonStatic,
    InitializeOnlineLobbyGuildSendMemoButton,
    RegisterOnlineLobbyGuildSendMemoButtonDestructor,
    DestroyOnlineLobbyGuildSendMemoButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubDisplayButtonStatic,
    InitializeOnlineLobbyGuildSubDisplayButton,
    RegisterOnlineLobbyGuildSubDisplayButtonDestructor,
    DestroyOnlineLobbyGuildSubDisplayButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubSiteButtonStatic,
    InitializeOnlineLobbyGuildSubSiteButton,
    RegisterOnlineLobbyGuildSubSiteButtonDestructor,
    DestroyOnlineLobbyGuildSubSiteButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubSendMessageButtonStatic,
    InitializeOnlineLobbyGuildSubSendMessageButton,
    RegisterOnlineLobbyGuildSubSendMessageButtonDestructor,
    DestroyOnlineLobbyGuildSubSendMessageButton);
DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubSendMemoButtonStatic,
    InitializeOnlineLobbyGuildSubSendMemoButton,
    RegisterOnlineLobbyGuildSubSendMemoButtonDestructor,
    DestroyOnlineLobbyGuildSubSendMemoButton);

#undef DECLARE_ONLINE_LOBBY_BUTTON_LIFETIME

void InitializeOnlineLobbyImageButtons(OnlineLobbyState& state);
void DestroyOnlineLobbyImageButtons(OnlineLobbyState& state);
void InitializeOnlineLobbyIconTileSheetStatic(OnlineLobbyState& state);
void InitializeOnlineLobbyIconTileSheet(OnlineLobbyState& state);
void RegisterOnlineLobbyIconTileSheetDestructor(OnlineLobbyState& state);
void DestroyOnlineLobbyIconTileSheet(OnlineLobbyState& state);
void InstallOnlineLobbyAccelerators(OnlineLobbyState& state);
void RestoreOnlineLobbyAccelerators(OnlineLobbyState& state);
void ShowOnlineLobbyControl(HWND window);
void HideOnlineLobbyControl(HWND window);
void SetOnlineLobbyTab(OnlineLobbyState& state, OnlineLobbyTab tab);
bool ResumeOnlineLobbyWindow(OnlineLobbyState& state);
bool CopySelectedOnlineLobbyGameListText(OnlineLobbyState& state, char* output,
    std::size_t output_size);
void* PostOnlineLobbyColoredTextPayload(OnlineLobbyState& state, const char* text);
void* PostOnlineLobbySingleColorText(HWND owner, const char* text,
    u8 red, u8 green, u8 blue);
bool AppendOnlineLobbyColoredListText(HWND owner, HWND list,
    const void* color_segment, const char* text);
int CopyOnlineLobbyTextThatFitsWidth(HDC dc, const char* prefix, char* output,
    std::size_t output_size, const char* source, int max_width);
void IgnoreOnlineLobbyReservedTextHelper();
void IgnoreOnlineLobbyMissingWhisperTarget();
int TrimOnlineLobbyIconMarkedTextToWidth(HDC dc, const char* text, int max_width);
void InsertOnlineLobbyInlineIconRun(OnlineLobbyState& state, int insertion_offset);
void CaptureOnlineLobbyInlineIconRanges(OnlineLobbyState& state);
bool ApplyOnlineLobbyChatFloodGuard(OnlineLobbyState& state, const char* text);
bool ReadOnlineLobbyRichEditTextWithInlineIcons(OnlineLobbyState& state,
    char* output, std::size_t output_size);
bool SendOnlineLobbyChatEditText(OnlineLobbyState& state, HWND owner,
    HWND edit, const char* whisper_target, COLORREF prefix_color,
    COLORREF text_color);
void AppendOnlineLobbyChatPayload(OnlineLobbyState& state, const void* locked_payload);
void FreeOnlineLobbyListPayloads(HWND list);
void FreeOnlineLobbyGameListPayloads(HWND list);
bool CreateOnlineLobbyWindow(OnlineLobbyState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context, bool reconnect_packet);
LRESULT HandleOnlineLobbyWindowMessage(OnlineLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleOnlineLobbyControlMessage(OnlineLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
