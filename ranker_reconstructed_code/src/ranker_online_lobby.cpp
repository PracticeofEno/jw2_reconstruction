#include "ranker_online_lobby.h"

#ifdef _WIN32

#include "ranker_avatar_window.h"
#include "ranker_barter_window.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_icon_marked_text.h"
#include "ranker_memo_window.h"
#include "ranker_network.h"
#include "ranker_ole_image_data.h"
#include "ranker_online_dialogs.h"
#include "ranker_player_profile.h"
#include "ranker_system_ui.h"
#include "ranker_text_tables.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <shellapi.h>

#ifndef EM_GETOLEINTERFACE
#define EM_GETOLEINTERFACE (WM_USER + 60)
#endif
#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR (WM_USER + 67)
#endif
#ifndef EM_SETCHARFORMAT
#define EM_SETCHARFORMAT (WM_USER + 68)
#endif
#ifndef EM_EXGETSEL
#define EM_EXGETSEL (WM_USER + 52)
#endif
#ifndef EM_EXSETSEL
#define EM_EXSETSEL (WM_USER + 55)
#endif

namespace ranker {
namespace {

constexpr DWORD kListBoxGameStyle = WS_CHILD | WS_VISIBLE | LBS_NOTIFY |
    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr DWORD kChatListStyle = WS_CHILD | WS_VISIBLE | WS_DISABLED |
    LBS_NOTIFY | LBS_OWNERDRAWFIXED;
constexpr DWORD kRichEditStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL |
    ES_NOHIDESEL;
// Several of the original owner-draw controls overlap by design.  In
// particular, TAB BG occupies the same rectangle as the main action buttons.
// Without sibling clipping a delayed repaint of that decorative control
// erases the Create/Join/Rank pixels even though the buttons remain clickable.
constexpr DWORD kSendButtonStyle =
    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_DEFPUSHBUTTON;
constexpr DWORD kOwnerDrawStyle =
    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW;
constexpr COLORREF kOnlineLobbyWhite = RGB(255, 255, 255);
constexpr COLORREF kOnlineLobbyMuted = RGB(200, 200, 200);
constexpr COLORREF kOnlineLobbySystemText = RGB(0, 250, 0);
constexpr COLORREF kOnlineLobbyLocalPrompt = RGB(20, 200, 20);
constexpr COLORREF kOnlineLobbyLocalText = RGB(200, 200, 200);
constexpr COLORREF kOnlineLobbyNormalChatPrompt = RGB(0, 200, 200);
constexpr COLORREF kOnlineLobbyWhisperPrompt = RGB(80, 80, 200);
constexpr COLORREF kOnlineLobbyWhisperText = RGB(120, 120, 120);
constexpr int kOnlineLobbyChatWrapGap = 10;
constexpr int kOnlineLobbyChatContinuationMargin = 20;
constexpr int kOnlineLobbyInsertSelectedNameCommandId = 0xbc1;
constexpr int kOnlineLobbyRichEditCopyCommandId = 0x9c44;
constexpr int kOnlineLobbyRichEditPasteCommandId = 0x9c45;
constexpr int kOnlineLobbyRichEditCutCommandId = 0x9c46;
constexpr std::size_t kOnlineLobbyChatBufferSize = 0x100;
constexpr char kOnlineLobbyWrapDelimiter = ' ';
constexpr std::size_t kOnlineLobbyGamePayloadBytes = 0x19e;
constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr std::size_t kStartupFriendAddTargetPromptRow = 209;
constexpr std::size_t kStartupFriendRemoveTargetPromptRow = 210;
constexpr int kOnlineLobbyReconstructedBackgroundResourceId = 2002;
constexpr u32 kOnlineLobbyChatFontIndex = 3;

constexpr OnlineLobbyButtonSpec kButtonSpecs[kOnlineLobbyButtonCount] = {
    {kOnlineLobbyNameButtonId, "Lobby Name", 0, 0, false},
    {kOnlineLobbySendButtonId, ">", 0, 0, false},
    {kOnlineLobbyWhisperButtonId, "&Whisper", 0x18, 0x19, false},
    {kOnlineLobbyEmoticonButtonId, "Emoticons", 0x1a, 0x1b, false},
    {kOnlineLobbyMainTabButtonId, "Main TAB", 0x1c, 0x1d, false},
    {kOnlineLobbyFriendsTabButtonId, "Friends TAB", 0x1e, 0x1f, false},
    {kOnlineLobbyGuildTabButtonId, "Guild TAB", 0x20, 0x21, false},
    {kOnlineLobbyPersonalTabButtonId, "Personal TAB", 0x22, 0x23, false},
    {kOnlineLobbyTabBackgroundButtonId, "TAB BG", 0x24, 0, false},
    {kOnlineLobbyChangeLobbyButtonId, "Change &Lobby", 0x25, 0x26, false},
    {kOnlineLobbyCreateGameButtonId, "Create Game", 0x27, 0x28, true},
    {kOnlineLobbyJoinGameButtonId, "Join Game", 0x2a, 0x2b, true},
    {kOnlineLobbyMyAvatarButtonId, "My Avatar", 0x2d, 0x2e, false},
    {kOnlineLobbyViewRankButtonId, "View Rank", 0x2f, 0x30, false},
    {IDCANCEL, "Cancel", 0x31, 0x32, false},
    {kOnlineLobbyFriendDisplayButtonId, "F_DISPLAY", 0x33, 0x34, false},
    {kOnlineLobbyFriendAddButtonId, "F_ADD", 0x35, 0x36, false},
    {kOnlineLobbyFriendRemoveButtonId, "F_REMOVE", 0x37, 0x38, false},
    {kOnlineLobbyFriendSendMessageButtonId, "F_SENDMSG", 0x39, 0x3a, false},
    {kOnlineLobbyFriendSendMemoButtonId, "F_SENDMEMO", 0x3b, 0x3c, false},
    {kOnlineLobbyGuildDisplayButtonId, "G_DISPLAY", 0x3d, 0x3e, false},
    {kOnlineLobbyGuildSiteButtonId, "G_SITE", 0x3f, 0x40, false},
    {kOnlineLobbyGuildSendMessageButtonId, "G_SENDMSG", 0x41, 0x42, false},
    {kOnlineLobbyGuildSendMemoButtonId, "G_SENDMEMO", 0x43, 0x44, false},
    {kOnlineLobbyGuildSubDisplayButtonId, "G_DISPLAY", 0x45, 0x46, false},
    {kOnlineLobbyGuildSubSiteButtonId, "G_SITE", 0x47, 0x48, false},
    {kOnlineLobbyGuildSubSendMessageButtonId, "G_SENDMSG", 0x49, 0x4a, false},
    {kOnlineLobbyGuildSubSendMemoButtonId, "G_SENDMEMO", 0x4b, 0x4c, false},
};

constexpr std::array<int, 5> kMainTabControls = {
    kOnlineLobbyChangeLobbyButtonId,
    kOnlineLobbyCreateGameButtonId,
    kOnlineLobbyJoinGameButtonId,
    kOnlineLobbyMyAvatarButtonId,
    kOnlineLobbyViewRankButtonId,
};

constexpr std::array<int, 3> kSimplifiedMainTabControls = {
    kOnlineLobbyCreateGameButtonId,
    kOnlineLobbyJoinGameButtonId,
    kOnlineLobbyViewRankButtonId,
};

constexpr std::array<int, 5> kFriendsTabControls = {
    kOnlineLobbyFriendDisplayButtonId,
    kOnlineLobbyFriendAddButtonId,
    kOnlineLobbyFriendRemoveButtonId,
    kOnlineLobbyFriendSendMessageButtonId,
    kOnlineLobbyFriendSendMemoButtonId,
};

constexpr std::array<int, 4> kGuildTabControls = {
    kOnlineLobbyGuildDisplayButtonId,
    kOnlineLobbyGuildSiteButtonId,
    kOnlineLobbyGuildSendMessageButtonId,
    kOnlineLobbyGuildSendMemoButtonId,
};

constexpr std::array<int, 4> kPersonalTabControls = {
    kOnlineLobbyGuildSubDisplayButtonId,
    kOnlineLobbyGuildSubSiteButtonId,
    kOnlineLobbyGuildSubSendMessageButtonId,
    kOnlineLobbyGuildSubSendMemoButtonId,
};

const char* local_player_chat_name(const OnlineLobbyState& state);

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

bool load_reconstructed_lobby_background(OnlineLobbyState& state) {
    HMODULE module = state.instance != nullptr ? state.instance :
        GetModuleHandleA(nullptr);
    HRSRC info = FindResourceA(module,
        MAKEINTRESOURCEA(kOnlineLobbyReconstructedBackgroundResourceId),
        RT_RCDATA);
    if (info == nullptr) {
        return false;
    }
    HGLOBAL loaded = LoadResource(module, info);
    const DWORD byte_count = SizeofResource(module, info);
    const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
    return bytes != nullptr && byte_count != 0 &&
        LoadBitmapMemoryResourceFromMemory(
            state.background, bytes, static_cast<std::size_t>(byte_count));
}

std::vector<OnlineLobbyLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<OnlineLobbyLayoutRect> rects;
    if (table.rects == nullptr || table.count == 0) {
        return rects;
    }
    rects.reserve(table.count);
    for (u32 i = 0; i < table.count; ++i) {
        const FrontendLayoutRect& rect = table.rects[i];
        rects.push_back({rect.x, rect.y, rect.width, rect.height});
    }
    return rects;
}

OnlineLobbyLayoutRect layout_at(const OnlineLobbyState& state,
    std::size_t index) {
    if (index < state.layout_rects.size()) {
        return state.layout_rects[index];
    }
    return OnlineLobbyLayoutRect{};
}

OnlineLobbyLayoutRect simplified_main_panel_rect(
    const OnlineLobbyState& state) {
    OnlineLobbyLayoutRect panel = layout_at(state, 14);
    const OnlineLobbyLayoutRect root = layout_at(state, 0);
    const OnlineLobbyLayoutRect create = layout_at(state, 16);
    const OnlineLobbyLayoutRect join = layout_at(state, 17);
    const OnlineLobbyLayoutRect rank = layout_at(state, 19);
    const i32 gap = ScaleFrontendLayoutValue(12, 1024,
        std::max<i32>(1, root.width));
    const i32 inset = ScaleFrontendLayoutValue(12, 1024,
        std::max<i32>(1, root.width));

    // The old content background extended under all four legacy tabs.  Its
    // reconstructed Main page contains only three actions, so terminate the
    // panel immediately after Rank instead of leaving three empty tab cells'
    // worth of framed space on the right.
    const i32 content_width = inset + create.width + gap + join.width + gap +
        rank.width + inset;
    panel.width = std::clamp<i32>(content_width, 1,
        std::max<i32>(1, panel.width));
    return panel;
}

OnlineLobbyLayoutRect arrange_simplified_main_action(
    const OnlineLobbyState& state, int control_id, OnlineLobbyLayoutRect rect) {
    if (control_id != kOnlineLobbyCreateGameButtonId &&
        control_id != kOnlineLobbyJoinGameButtonId &&
        control_id != kOnlineLobbyViewRankButtonId) {
        return rect;
    }

    const OnlineLobbyLayoutRect root = layout_at(state, 0);
    const OnlineLobbyLayoutRect create = layout_at(state, 16);
    const OnlineLobbyLayoutRect join = layout_at(state, 17);
    const OnlineLobbyLayoutRect rank = layout_at(state, 19);
    const OnlineLobbyLayoutRect action_panel = simplified_main_panel_rect(state);
    const i32 gap = ScaleFrontendLayoutValue(12, 1024,
        std::max<i32>(1, root.width));
    const i32 panel_inset = ScaleFrontendLayoutValue(12, 1024,
        std::max<i32>(1, root.width));
    const i32 vertical_inset = ScaleFrontendLayoutValue(12, 600,
        std::max<i32>(1, root.height));
    const i32 first_x = std::max<i32>(0, action_panel.x + panel_inset);

    // The reconstructed background has a compact framed group for these
    // three actions.  Center their live windows inside that frame so the
    // painted buttons and their hit targets stay aligned.
    rect.y = std::max<i32>(0, action_panel.y + vertical_inset);

    if (control_id == kOnlineLobbyCreateGameButtonId) {
        rect.x = first_x;
    } else if (control_id == kOnlineLobbyJoinGameButtonId) {
        rect.x = first_x + create.width + gap;
    } else {
        rect.x = first_x + create.width + gap + join.width + gap;
    }
    return rect;
}

OnlineLobbyLayoutRect arrange_chat_composer_control(
    const OnlineLobbyState& state, OnlineLobbyLayoutRect rect) {
    const OnlineLobbyLayoutRect root = layout_at(state, 0);
    const i32 separation = ScaleFrontendLayoutValue(8, 768,
        std::max<i32>(1, root.height));
    rect.y += separation;
    return rect;
}

HFONT online_lobby_chat_font() {
    // The -16 UI font fills the 22-pixel RichEdit comfortably while leaving
    // enough top/bottom breathing room for Korean glyphs and inline icons.
    HFONT font = GetUiFontHandle(kOnlineLobbyChatFontIndex);
    return font != nullptr ? font :
        reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

int online_lobby_chat_row_height(const OnlineLobbyState& state) {
    const OnlineLobbyLayoutRect edit =
        arrange_chat_composer_control(state, layout_at(state, 6));
    return std::max(18, edit.height - 2);
}

void frame_online_lobby_rect(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        FrameRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

void fill_online_lobby_rect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

void paint_online_lobby_dynamic_chrome(
    const OnlineLobbyState& state, HDC dc) {
    const OnlineLobbyLayoutRect root = layout_at(state, 0);
    const OnlineLobbyLayoutRect edit =
        arrange_chat_composer_control(state, layout_at(state, 6));
    const OnlineLobbyLayoutRect send =
        arrange_chat_composer_control(state, layout_at(state, 7));
    const OnlineLobbyLayoutRect whisper =
        arrange_chat_composer_control(state, layout_at(state, 8));
    if (dc == nullptr || root.width <= 0 || edit.width <= 0 ||
        edit.height <= 0 || send.height <= 0 || whisper.x <= edit.x) {
        return;
    }

    const int horizontal_padding = ScaleFrontendLayoutValue(8, 1024,
        std::max(1, root.width));
    const int vertical_padding = ScaleFrontendLayoutValue(5, 768,
        std::max(1, root.height));
    RECT frame{
        std::max(0, edit.x - horizontal_padding),
        std::max(0, std::min(send.y, edit.y - vertical_padding)),
        std::min(root.width, std::min(
            whisper.x - horizontal_padding,
            edit.x + edit.width + horizontal_padding)),
        std::min(root.height, std::max(
            send.y + send.height,
            edit.y + edit.height + vertical_padding)),
    };
    if (frame.right <= frame.left || frame.bottom <= frame.top) {
        return;
    }

    // The background contains only the static lobby masonry.  Build the chat
    // composer around the real RichEdit rectangle so its field and the three
    // live icon buttons share one baseline instead of overlapping baked art.
    fill_online_lobby_rect(dc, frame, RGB(0, 0, 0));
    frame_online_lobby_rect(dc, frame, RGB(10, 8, 5));
    InflateRect(&frame, -1, -1);
    frame_online_lobby_rect(dc, frame, RGB(151, 116, 66));
    InflateRect(&frame, -1, -1);
    frame_online_lobby_rect(dc, frame, RGB(47, 37, 24));
}

void write_color_segment(u8* segment, COLORREF color, std::size_t text_length) {
    segment[0] = static_cast<u8>(GetRValue(color));
    segment[1] = static_cast<u8>(GetGValue(color));
    segment[2] = static_cast<u8>(GetBValue(color));
    segment[3] = static_cast<u8>(text_length);
}

COLORREF color_from_segment(const u8* segment) {
    if (segment == nullptr) {
        return kOnlineLobbyWhite;
    }
    return RGB(segment[0], segment[1], segment[2]);
}

const char* text_from_segment(const u8* segment) {
    return segment == nullptr ? "" : reinterpret_cast<const char*>(segment + 4);
}

const u8* next_segment(const u8* segment) {
    return segment == nullptr ? nullptr : segment + 4 + segment[3];
}

void append_capped(char* destination, std::size_t destination_size,
    const char* source) {
    if (destination == nullptr || destination_size == 0 || source == nullptr) {
        return;
    }
    const std::size_t used = std::strlen(destination);
    if (used >= destination_size - 1) {
        return;
    }
    const std::size_t available = destination_size - used - 1;
    const std::size_t copied = std::min(available, std::strlen(source));
    std::memcpy(destination + used, source, copied);
    destination[used + copied] = '\0';
}

u32 read_le32(const u8* buffer) {
    return static_cast<u32>(buffer[0]) |
        (static_cast<u32>(buffer[1]) << 8) |
        (static_cast<u32>(buffer[2]) << 16) |
        (static_cast<u32>(buffer[3]) << 24);
}

void write_le32(u8* buffer, u32 value) {
    buffer[0] = static_cast<u8>(value & 0xff);
    buffer[1] = static_cast<u8>((value >> 8) & 0xff);
    buffer[2] = static_cast<u8>((value >> 16) & 0xff);
    buffer[3] = static_cast<u8>((value >> 24) & 0xff);
}

u32 packet_u32(const u8* packet, std::size_t byte_count, std::size_t offset) {
    if (packet == nullptr || offset + sizeof(u32) > byte_count) {
        return 0;
    }
    return read_le32(packet + offset);
}

std::string packet_string(const u8* packet, std::size_t byte_count,
    std::size_t offset) {
    if (packet == nullptr || offset >= byte_count) {
        return {};
    }
    const char* text = reinterpret_cast<const char*>(packet + offset);
    const std::size_t limit = byte_count - offset;
    std::size_t length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return std::string(text, length);
}

std::string packet_fixed_string(const u8* packet, std::size_t byte_count,
    std::size_t offset, std::size_t field_size) {
    if (packet == nullptr || offset >= byte_count || field_size == 0) {
        return {};
    }
    const char* text = reinterpret_cast<const char*>(packet + offset);
    const std::size_t limit = std::min(field_size, byte_count - offset);
    std::size_t length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return std::string(text, length);
}

const char* online_lobby_status_message(u32 code) {
    switch (code) {
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

void* allocate_locked_global(std::size_t byte_count) {
    if (byte_count == 0) {
        return nullptr;
    }
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byte_count);
    if (handle == nullptr) {
        return nullptr;
    }
    void* pointer = GlobalLock(handle);
    if (pointer == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    std::memset(pointer, 0, byte_count);
    return pointer;
}

void free_locked_global_pointer(const void* pointer) {
    if (pointer == nullptr ||
        reinterpret_cast<LPARAM>(pointer) == static_cast<LPARAM>(LB_ERR)) {
        return;
    }
    HGLOBAL handle = GlobalHandle(pointer);
    if (handle == nullptr) {
        return;
    }
    GlobalUnlock(handle);
    GlobalFree(handle);
}

bool post_locked_payload(HWND owner, UINT message, const void* payload,
    std::size_t byte_count) {
    if (owner == nullptr || payload == nullptr || byte_count == 0) {
        return false;
    }
    void* copy = allocate_locked_global(byte_count);
    if (copy == nullptr) {
        return false;
    }
    std::memcpy(copy, payload, byte_count);
    if (PostMessageA(owner, message, 0, reinterpret_cast<LPARAM>(copy)) != 0) {
        return true;
    }
    free_locked_global_pointer(copy);
    return false;
}

bool add_locked_list_row(HWND list, void* row_payload) {
    if (list == nullptr || row_payload == nullptr) {
        free_locked_global_pointer(row_payload);
        return false;
    }
    LRESULT index = SendMessageA(list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(row_payload));
    if (index == LB_ERR) {
        free_locked_global_pointer(row_payload);
        return false;
    }
    return true;
}

bool append_chat_row_bytes(HWND list, const void* source, std::size_t byte_count,
    std::size_t copy_count) {
    if (source == nullptr || byte_count == 0) {
        return false;
    }
    void* row = allocate_locked_global(byte_count);
    if (row == nullptr) {
        return false;
    }
    std::memcpy(row, source, std::min(byte_count, copy_count));
    return add_locked_list_row(list, row);
}

int chat_list_width(OnlineLobbyState& state) {
    RECT rect{};
    if (state.chat_list != nullptr && GetClientRect(state.chat_list, &rect)) {
        const int width = rect.right - rect.left;
        if (width > 0) {
            return width;
        }
    }
    return std::max(1, layout_at(state, 4).width);
}

int visible_list_rows(HWND list) {
    RECT rect{};
    if (list == nullptr || !GetClientRect(list, &rect)) {
        return 0;
    }
    LRESULT item_height = SendMessageA(list, LB_GETITEMHEIGHT, 0, 0);
    if (item_height <= 0) {
        item_height = 1;
    }
    const int height = static_cast<int>(rect.bottom - rect.top);
    return std::max(1, height / static_cast<int>(item_height));
}

void configure_scroll_for_list(OnlineLobbyScrollControl& scroll, HWND list,
    int item_count, int top_index, bool redraw) {
    if (scroll.window == nullptr || list == nullptr) {
        return;
    }

    const int visible_rows = visible_list_rows(list);
    const int max_top_index = std::max(0, item_count - visible_rows);
    top_index = std::clamp(top_index, 0, max_top_index);

    SetLegacyCustomScrollControlPageStep(scroll.control, visible_rows);
    SetLegacyCustomScrollControlRange(scroll.control, 0, max_top_index, false);
    SetLegacyCustomScrollControlValue(scroll.control, top_index, false);
    SetLegacyCustomScrollControlVisible(scroll.control, max_top_index > 0);
    if (redraw && max_top_index > 0) {
        SetLegacyCustomScrollControlValue(scroll.control, top_index, true);
    }
}

void sync_chat_scrollbar(OnlineLobbyState& state, int item_count) {
    const int visible_rows = visible_list_rows(state.chat_list);
    const int top_index = std::max(0, item_count - visible_rows);
    if (state.chat_list != nullptr) {
        SendMessageA(state.chat_list, LB_SETTOPINDEX,
            static_cast<WPARAM>(top_index), 0);
    }
    configure_scroll_for_list(state.scroll_controls[1], state.chat_list,
        item_count, top_index, true);
}

void trim_chat_rows(HWND list) {
    if (list == nullptr) {
        return;
    }
    int count = static_cast<int>(SendMessageA(list, LB_GETCOUNT, 0, 0));
    while (count > static_cast<int>(kOnlineLobbyChatRowLimit)) {
        LRESULT item_data = SendMessageA(list, LB_GETITEMDATA, 0, 0);
        SendMessageA(list, LB_DELETESTRING, 0, 0);
        if (item_data != LB_ERR && item_data != 0) {
            free_locked_global_pointer(reinterpret_cast<const void*>(item_data));
        }
        --count;
    }
}

void draw_segment_text(HDC dc, RECT& rect, const BitmapTileSheetSelector& icon_sheet,
    const u8* segment, bool parse_inline_icons) {
    if (dc == nullptr || segment == nullptr || segment[3] == 0) {
        return;
    }

    SetTextColor(dc, color_from_segment(segment));
    SetBkColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);

    const char* text = text_from_segment(segment);
    if (!parse_inline_icons) {
        DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
        rect.left += MeasureGdiTextWidth(dc, text);
        return;
    }

    int offset = 0;
    for (;;) {
        const char ch = text[offset];
        if (ch == '\0' || ch == '\n' || ch == '\r') {
            return;
        }
        const int marker = FindInlineIconMarker(text + offset);
        if (marker < 0) {
            DrawTextA(dc, text + offset, -1, &rect,
                DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
            return;
        }
        if (marker == 0) {
            const u32 cell_index =
                static_cast<u8>(text[offset + 1]) - static_cast<u8>('a');
            DrawBitmapTileSheetCellByIndex(icon_sheet, dc, rect.left, rect.top,
                cell_index);
            rect.left += kInlineIconMarkerWidth;
            offset += 3;
            continue;
        }

        char plain[kOnlineLobbyChatBufferSize]{};
        const std::size_t copied = std::min<std::size_t>(
            static_cast<std::size_t>(marker), sizeof(plain) - 1);
        std::memcpy(plain, text + offset, copied);
        DrawTextA(dc, plain, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
        rect.left += MeasureGdiTextWidth(dc, plain);
        offset += marker;
    }
}

void draw_online_lobby_chat_item(OnlineLobbyState& state,
    const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return;
    }

    HGDIOBJ previous_font = SelectObject(draw.hDC, online_lobby_chat_font());
    RECT fill = draw.rcItem;
    FillRect(draw.hDC, &fill, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const auto* first =
        reinterpret_cast<const u8*>(draw.itemData);
    if (first == nullptr ||
        reinterpret_cast<LPARAM>(first) == static_cast<LPARAM>(LB_ERR)) {
        if (previous_font != nullptr && previous_font != HGDI_ERROR) {
            SelectObject(draw.hDC, previous_font);
        }
        return;
    }

    RECT text_rect = draw.rcItem;
    text_rect.left += 2;
    text_rect.top += 2;
    const u8* second = next_segment(first);
    int prefix_width = kOnlineLobbyChatContinuationMargin;
    if (first[3] != 0) {
        RECT prefix_rect = text_rect;
        draw_segment_text(draw.hDC, prefix_rect, state.icon_sheet, first, false);
        prefix_width = MeasureGdiTextWidth(draw.hDC, text_from_segment(first));
    }
    text_rect.left += prefix_width;
    draw_segment_text(draw.hDC, text_rect, state.icon_sheet, second, true);
    if (previous_font != nullptr && previous_font != HGDI_ERROR) {
        SelectObject(draw.hDC, previous_font);
    }
}

void draw_online_lobby_game_item(const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return;
    }
    char text[256]{};
    SendMessageA(draw.hwndItem, LB_GETTEXT, draw.itemID,
        reinterpret_cast<LPARAM>(text));
    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(draw.hDC, kOnlineLobbyMuted);
    SetBkColor(draw.hDC, (draw.itemState & ODS_SELECTED) != 0 ?
        RGB(0, 0, 255) : RGB(0, 0, 0));
    SetBkMode(draw.hDC, (draw.itemState & ODS_SELECTED) != 0 ?
        OPAQUE : TRANSPARENT);
    rect.left += 0x2a;
    DrawTextA(draw.hDC, text, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
}

void queue_online_lobby_async_bytes(OnlineLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (state.callbacks.send_async_packet != nullptr && packet != nullptr &&
        byte_count != 0) {
        state.callbacks.send_async_packet(packet, byte_count, state.callbacks.user_data);
    }
}

void play_online_lobby_click_sound() {
    HandleDefaultFrontendUiClickSound();
}

void queue_online_lobby_game_list_reset_request(OnlineLobbyState& state) {
    std::array<u8, 0x0d> packet{};
    write_le32(packet.data(), 3);
    write_le32(packet.data() + 4, 0x10);
    write_le32(packet.data() + 8, static_cast<u32>(packet.size()));
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
}

void queue_online_lobby_game_page_request(OnlineLobbyState& state, u32 page) {
    std::array<u8, 0x11> packet{};
    write_le32(packet.data(), 3);
    write_le32(packet.data() + 4, 0x12);
    write_le32(packet.data() + 8, static_cast<u32>(packet.size()));
    write_le32(packet.data() + 0x0d, page);
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
}

std::vector<u8> make_online_lobby_command_packet(u32 opcode,
    std::size_t byte_count) {
    std::vector<u8> packet(std::max<std::size_t>(byte_count, 0x0d), 0);
    write_le32(packet.data(), 3);
    write_le32(packet.data() + 4, opcode);
    write_le32(packet.data() + 8, static_cast<u32>(packet.size()));
    return packet;
}

void queue_online_lobby_simple_command(OnlineLobbyState& state, u32 opcode) {
    std::vector<u8> packet = make_online_lobby_command_packet(opcode, 0x0d);
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
}

void show_select_game_prompt(OnlineLobbyState& state, std::size_t message_row,
    const char* fallback) {
    const char* message = startup_message_row(message_row, fallback);
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(state.window, message,
            kOnlineLobbySystemText, state.callbacks.user_data);
        return;
    }
    PostOnlineLobbySingleColorText(state.window, message, 250, 250, 0);
}

bool queue_online_lobby_selected_game_command(OnlineLobbyState& state,
    u32 opcode, std::size_t missing_selection_row,
    const char* missing_selection_fallback) {
    char game_name[0x20]{};
    if (!CopySelectedOnlineLobbyGameListText(state, game_name, sizeof(game_name))) {
        IgnoreOnlineLobbyMissingWhisperTarget();
        show_select_game_prompt(state, missing_selection_row,
            missing_selection_fallback);
        return false;
    }

    std::vector<u8> packet = make_online_lobby_command_packet(opcode, 0x2d);
    std::snprintf(reinterpret_cast<char*>(packet.data() + 0x0d),
        packet.size() - 0x0d, "%s", game_name);
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
    return true;
}

bool queue_online_lobby_text_command(OnlineLobbyState& state, HWND owner,
    u32 opcode) {
    char text[200]{};
    if (!ReadOnlineLobbyRichEditTextWithInlineIcons(state, text, sizeof(text)) ||
        !ApplyOnlineLobbyChatFloodGuard(state, text)) {
        return false;
    }

    std::vector<u8> packet = make_online_lobby_command_packet(opcode, 0xd5);
    std::snprintf(reinterpret_cast<char*>(packet.data() + 0x0d),
        packet.size() - 0x0d, "%s", text);
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
    if (state.chat_edit != nullptr) {
        SendMessageA(state.chat_edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(""));
        SetFocus(state.chat_edit);
    } else if (owner != nullptr) {
        SetFocus(owner);
    }
    return true;
}

void focus_online_lobby_chat_edit(OnlineLobbyState& state) {
    if (state.chat_edit != nullptr) {
        SetFocus(state.chat_edit);
    }
}

bool queue_online_lobby_named_game_command(OnlineLobbyState& state,
    const char* game_name) {
    if (game_name == nullptr || *game_name == '\0') {
        return false;
    }
    std::vector<u8> packet = make_online_lobby_command_packet(0x37, 0x2d);
    std::snprintf(reinterpret_cast<char*>(packet.data() + 0x0d),
        packet.size() - 0x0d, "%s", game_name);
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
    return true;
}

bool queue_online_lobby_selected_game_detail_command(OnlineLobbyState& state) {
    if (state.game_list == nullptr) {
        return false;
    }
    const LRESULT selected = SendMessageA(state.game_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return false;
    }
    const LRESULT item_data = SendMessageA(state.game_list, LB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    if (item_data == LB_ERR || item_data == 0) {
        return false;
    }

    char game_name[0x20]{};
    SendMessageA(state.game_list, LB_GETTEXT, static_cast<WPARAM>(selected),
        reinterpret_cast<LPARAM>(game_name));
    game_name[sizeof(game_name) - 1] = '\0';
    return queue_online_lobby_named_game_command(state, game_name);
}

bool copy_selected_online_lobby_game_name(OnlineLobbyState& state,
    char* output, std::size_t output_size) {
    if (state.game_list == nullptr || output == nullptr || output_size == 0) {
        return false;
    }
    output[0] = '\0';
    const LRESULT selected = SendMessageA(state.game_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return false;
    }
    const LRESULT copied = SendMessageA(state.game_list, LB_GETTEXT,
        static_cast<WPARAM>(selected), reinterpret_cast<LPARAM>(output));
    output[output_size - 1] = '\0';
    return copied != LB_ERR && output[0] != '\0';
}

bool append_selected_online_lobby_game_name_to_edit(OnlineLobbyState& state) {
    if (state.chat_edit == nullptr) {
        return false;
    }
    char game_name[MAX_PATH]{};
    if (!copy_selected_online_lobby_game_name(state, game_name,
            sizeof(game_name))) {
        focus_online_lobby_chat_edit(state);
        return false;
    }
    std::strncat(game_name, " ", sizeof(game_name) - std::strlen(game_name) - 1);
    const LRESULT length = SendMessageA(state.chat_edit, WM_GETTEXTLENGTH, 0, 0);
    SendMessageA(state.chat_edit, EM_SETSEL, length, length);
    SendMessageA(state.chat_edit, EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(game_name));
    focus_online_lobby_chat_edit(state);
    return true;
}

bool send_online_lobby_whisper_to_selected_game(OnlineLobbyState& state,
    HWND owner) {
    char target[MAX_PATH]{};
    if (!copy_selected_online_lobby_game_name(state, target, sizeof(target))) {
        IgnoreOnlineLobbyMissingWhisperTarget();
        focus_online_lobby_chat_edit(state);
        return false;
    }
    const bool sent = SendOnlineLobbyChatEditText(state, owner, state.chat_edit,
        target, kOnlineLobbyWhisperPrompt, kOnlineLobbyWhisperText);
    focus_online_lobby_chat_edit(state);
    return sent;
}

void handle_online_lobby_rich_edit_clipboard_command(OnlineLobbyState& state,
    int command_id) {
    if (state.chat_edit == nullptr) {
        return;
    }
    switch (command_id) {
    case kOnlineLobbyRichEditCopyCommandId:
        SendMessageA(state.chat_edit, WM_COPY, 0, 0);
        CaptureOnlineLobbyInlineIconRanges(state);
        break;
    case kOnlineLobbyRichEditPasteCommandId: {
        CHARRANGE range{};
        SendMessageA(state.chat_edit, EM_EXGETSEL, 0,
            reinterpret_cast<LPARAM>(&range));
        SendMessageA(state.chat_edit, WM_PASTE, 0, 0);
        InsertOnlineLobbyInlineIconRun(state, range.cpMin);
        break;
    }
    case kOnlineLobbyRichEditCutCommandId:
        CaptureOnlineLobbyInlineIconRanges(state);
        SendMessageA(state.chat_edit, WM_CUT, 0, 0);
        break;
    default:
        break;
    }
}

bool send_online_lobby_help_chat(OnlineLobbyState& state, HWND owner) {
    if (state.chat_edit == nullptr) {
        return false;
    }
    SendMessageA(state.chat_edit, WM_SETTEXT, 0,
        reinterpret_cast<LPARAM>("/help"));
    return SendOnlineLobbyChatEditText(state, owner, state.chat_edit, nullptr,
        kOnlineLobbyNormalChatPrompt, kOnlineLobbyLocalText);
}

void copy_capped_text(char* destination, std::size_t destination_size,
    const char* source) {
    if (destination == nullptr || destination_size == 0) {
        return;
    }
    std::snprintf(destination, destination_size, "%s",
        source != nullptr ? source : "");
}

void queue_online_lobby_barter_decision(OnlineLobbyState& state, bool rejected) {
    std::vector<u8> packet = make_online_lobby_command_packet(0x6b, 0x11);
    packet[0x0d] = rejected ? 1 : 0;
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
}

void queue_online_lobby_named_decision(OnlineLobbyState& state, bool rejected) {
    std::vector<u8> packet = make_online_lobby_command_packet(0x85, 0x31);
    copy_capped_text(reinterpret_cast<char*>(packet.data() + 0x0d), 0x20,
        state.pending_prompt_name.data());
    packet[0x2d] = rejected ? 1 : 0;
    queue_online_lobby_async_bytes(state, packet.data(), packet.size());
}

void show_online_lobby_modal_message(OnlineLobbyState& state, const char* text,
    COLORREF color = kOnlineLobbySystemText) {
    if (state.window == nullptr || text == nullptr || *text == '\0') {
        return;
    }
    ShowOnlineModalPrompt1(online_modal_prompt_state(), state.window, text, color);
}

void open_online_lobby_modeless_prompt(OnlineLobbyState& state, const char* text,
    WPARAM accept_wparam, LPARAM accept_lparam = 0,
    COLORREF color = kOnlineLobbySystemText) {
    if (state.window == nullptr || text == nullptr || *text == '\0') {
        return;
    }
    CreateOnlineModelessPrompt(online_modeless_prompt_state(), state.window,
        state.instance, text, color, true, accept_wparam, accept_lparam);
}

void open_online_lobby_player_profile(OnlineLobbyState& state, const u8* packet,
    std::size_t byte_count) {
    if (packet == nullptr || byte_count == 0) {
        return;
    }
    PlayerProfileState& profile = player_profile_state();
    if (profile.window != nullptr && IsWindow(profile.window)) {
        DestroyWindow(profile.window);
    }
    const std::string requested_name = packet_string(packet, byte_count, 0x0d);
    CreatePlayerProfileWindow(profile, state.window, state.instance, packet,
        byte_count, requested_name.c_str(), local_player_chat_name(state),
        state.async_tcp_socket);
}

void open_online_lobby_barter_window(OnlineLobbyState& state) {
    BarterWindowState& barter = barter_window_state();
    if (barter.window != nullptr && IsWindow(barter.window)) {
        DestroyWindow(barter.window);
    }
    CreateBarterWindow(barter, state.window, state.instance,
        local_player_chat_name(state), state.pending_barter_summary.data(),
        state.pending_barter_summary.size(), state.async_tcp_socket);
}

void prepare_barter_prompt_from_packet(OnlineLobbyState& state, const u8* packet,
    std::size_t byte_count) {
    state.pending_barter_summary.fill(0);
    if (packet != nullptr && byte_count > 0x0d) {
        const std::size_t copied = std::min<std::size_t>(
            state.pending_barter_summary.size(), byte_count - 0x0d);
        std::memcpy(state.pending_barter_summary.data(), packet + 0x0d, copied);
    }
    const char* requester =
        reinterpret_cast<const char*>(state.pending_barter_summary.data());
    char prompt[128]{};
    std::snprintf(prompt, sizeof(prompt),
        startup_message_row(204, "%s requested an item trade."),
        requester != nullptr && requester[0] != '\0' ? requester : "this player");
    open_online_lobby_modeless_prompt(state, prompt, 1, 0,
        RGB(0xfa, 0xfa, 0xfa));
}

void prepare_named_prompt_from_packet(OnlineLobbyState& state, const u8* packet,
    std::size_t byte_count) {
    state.pending_prompt_name.fill(0);
    const std::string name = packet_string(packet, byte_count, 0x0d);
    copy_capped_text(state.pending_prompt_name.data(),
        state.pending_prompt_name.size(), name.c_str());
    char prompt[128]{};
    std::snprintf(prompt, sizeof(prompt),
        startup_message_row(264, "%s wants to add you as a friend."),
        state.pending_prompt_name[0] != '\0' ? state.pending_prompt_name.data() :
        "this player");
    open_online_lobby_modeless_prompt(state, prompt, 2, 0,
        RGB(0xfa, 0xfa, 0xfa));
}

void HandleOnlineLobbyPromptResult(OnlineLobbyState& state, WPARAM route,
    bool rejected) {
    switch (route) {
    case 1:
        if (!rejected) {
            open_online_lobby_barter_window(state);
        }
        queue_online_lobby_barter_decision(state, rejected);
        break;
    case 2:
        queue_online_lobby_named_decision(state, rejected);
        break;
    default:
        break;
    }
}

bool online_lobby_game_name_exists(OnlineLobbyState& state, const char* name) {
    if (state.game_list == nullptr || name == nullptr || *name == '\0') {
        return false;
    }
    const int count = static_cast<int>(SendMessageA(state.game_list, LB_GETCOUNT, 0, 0));
    char row_text[0x100]{};
    for (int i = 0; i < count; ++i) {
        row_text[0] = '\0';
        SendMessageA(state.game_list, LB_GETTEXT, static_cast<WPARAM>(i),
            reinterpret_cast<LPARAM>(row_text));
        if (std::strcmp(row_text, name) == 0) {
            return true;
        }
    }
    return false;
}

void update_online_lobby_game_count_label(OnlineLobbyState& state) {
    if (state.window == nullptr || state.game_list == nullptr) {
        return;
    }
    const int count = static_cast<int>(SendMessageA(state.game_list, LB_GETCOUNT, 0, 0));
    char text[0x80]{};
    std::snprintf(text, sizeof(text), "Lobby (%d)", count);
    state.lobby_name = text;
    SetDlgItemTextA(state.window, kOnlineLobbyNameButtonId, text);
}

void clear_online_lobby_game_list(OnlineLobbyState& state) {
    if (state.game_list == nullptr) {
        return;
    }
    FreeOnlineLobbyGameListPayloads(state.game_list);
    SendMessageA(state.game_list, LB_RESETCONTENT, 0, 0);
    update_online_lobby_game_count_label(state);
}

void add_online_lobby_game_row(OnlineLobbyState& state, const char* name,
    const u8* packet, std::size_t byte_count, bool paged_record) {
    if (state.game_list == nullptr || name == nullptr || *name == '\0' ||
        online_lobby_game_name_exists(state, name)) {
        return;
    }
    const LRESULT index = SendMessageA(state.game_list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(name));
    if (index == LB_ERR) {
        return;
    }

    auto* payload = static_cast<u8*>(::operator new(kOnlineLobbyGamePayloadBytes));
    std::memset(payload, 0, kOnlineLobbyGamePayloadBytes);
    if (paged_record) {
        write_le32(payload, packet_u32(packet, byte_count, 0x55));
        write_le32(payload + 4, packet_u32(packet, byte_count, 0x59));
        write_le32(payload + 0x2c, packet_u32(packet, byte_count, 0x5d));
        write_le32(payload + 0x5c, packet_u32(packet, byte_count, 0x61));
        write_le32(payload + 0x18a, packet_u32(packet, byte_count, 0x11));
    } else {
        write_le32(payload, packet_u32(packet, byte_count, 0x51));
        write_le32(payload + 4, packet_u32(packet, byte_count, 0x55));
        write_le32(payload + 0x2c, packet_u32(packet, byte_count, 0x59));
        write_le32(payload + 0x5c, packet_u32(packet, byte_count, 0x5d));
        write_le32(payload + 0x18a, packet_u32(packet, byte_count, 0x4d));
    }
    SendMessageA(state.game_list, LB_SETITEMDATA, static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(payload));
    update_online_lobby_game_count_label(state);
}

void remove_online_lobby_game_by_id(OnlineLobbyState& state, u32 game_id) {
    if (state.game_list == nullptr) {
        return;
    }
    const int count = static_cast<int>(SendMessageA(state.game_list, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        LRESULT item_data = SendMessageA(state.game_list, LB_GETITEMDATA,
            static_cast<WPARAM>(i), 0);
        if (item_data == LB_ERR || item_data == 0) {
            continue;
        }
        auto* payload = reinterpret_cast<u8*>(item_data);
        if (read_le32(payload + 0x18a) != game_id) {
            continue;
        }
        SendMessageA(state.game_list, LB_DELETESTRING, static_cast<WPARAM>(i), 0);
        ::operator delete(payload);
        update_online_lobby_game_count_label(state);
        break;
    }
}

OnlineLobbyState g_online_lobby_state;
bool g_background_bitmap_destructor_registered = false;
std::array<bool, 2> g_scroll_destructor_registered{};
std::array<bool, kOnlineLobbyButtonCount> g_button_destructor_registered{};
bool g_icon_tile_sheet_destructor_registered = false;

LRESULT CALLBACK online_lobby_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    auto* state = reinterpret_cast<OnlineLobbyState*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (state == nullptr) {
        state = &g_online_lobby_state;
    }
    return HandleOnlineLobbyWindowMessage(*state, hwnd, message, wparam, lparam);
}

LRESULT CALLBACK online_lobby_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleOnlineLobbyControlMessage(g_online_lobby_state, hwnd, message,
        wparam, lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_global_background_bitmap() {
    DestroyOnlineLobbyBackgroundBitmap(g_online_lobby_state);
}

void shutdown_global_icon_tile_sheet() {
    DestroyOnlineLobbyIconTileSheet(g_online_lobby_state);
}

void initialize_online_lobby_scroll_slot(OnlineLobbyState& state,
    std::size_t slot, int id) {
    if (slot >= state.scroll_controls.size()) {
        return;
    }
    OnlineLobbyScrollControl& scroll = state.scroll_controls[slot];
    InitializeLegacyCustomScrollControl(scroll.control);
    scroll.window = nullptr;
    scroll.original_window_proc = nullptr;
    scroll.id = id;
}

void destroy_online_lobby_scroll_slot(OnlineLobbyState& state, std::size_t slot) {
    if (slot >= state.scroll_controls.size()) {
        return;
    }
    OnlineLobbyScrollControl& scroll = state.scroll_controls[slot];
    DestroyLegacyCustomScrollControl(scroll.control);
    scroll.window = nullptr;
    scroll.original_window_proc = nullptr;
}

void initialize_online_lobby_button_slot(OnlineLobbyState& state, std::size_t slot) {
    if (slot < state.buttons.size()) {
        InitializeLegacyImageButtonControl(state.buttons[slot]);
    }
}

void destroy_online_lobby_button_slot(OnlineLobbyState& state, std::size_t slot) {
    if (slot < state.buttons.size()) {
        DestroyLegacyImageButtonControl(state.buttons[slot]);
    }
}

template <std::size_t Slot>
void shutdown_global_scroll_slot() {
    destroy_online_lobby_scroll_slot(g_online_lobby_state, Slot);
}

template <std::size_t Slot>
void shutdown_global_button_slot() {
    destroy_online_lobby_button_slot(g_online_lobby_state, Slot);
}

int button_index_by_id(int id) {
    for (int i = 0; i < static_cast<int>(kOnlineLobbyButtonCount); ++i) {
        if (kButtonSpecs[i].id == id) {
            return i;
        }
    }
    return -1;
}

LegacyImageButtonControl* button_by_id(OnlineLobbyState& state, int id) {
    const int index = button_index_by_id(id);
    return index >= 0 ? &state.buttons[static_cast<std::size_t>(index)] : nullptr;
}

OnlineLobbyScrollControl* scroll_by_id(OnlineLobbyState& state, int id) {
    if (id == kOnlineLobbyGameListScrollId) {
        return &state.scroll_controls[0];
    }
    if (id == kOnlineLobbyChatListScrollId) {
        return &state.scroll_controls[1];
    }
    return nullptr;
}

bool has_original_proc_for_id(int id) {
    switch (id) {
    case kOnlineLobbyGameListId:
    case kOnlineLobbyGameListScrollId:
    case kOnlineLobbyChatListId:
    case kOnlineLobbyChatListScrollId:
    case kOnlineLobbyChatEditId:
        return true;
    default:
        return button_index_by_id(id) >= 0;
    }
}

WNDPROC original_proc_for_id(OnlineLobbyState& state, int id) {
    if (id == kOnlineLobbyGameListId) {
        return state.game_list_original_proc;
    }
    if (id == kOnlineLobbyChatListId) {
        return state.chat_list_original_proc;
    }
    if (id == kOnlineLobbyChatEditId) {
        return state.chat_edit_original_proc;
    }
    if (OnlineLobbyScrollControl* scroll = scroll_by_id(state, id)) {
        return scroll->original_window_proc;
    }
    if (LegacyImageButtonControl* button = button_by_id(state, id)) {
        return button->original_window_proc;
    }
    return nullptr;
}

OnlineLobbyScrollControl* scroll_by_window(OnlineLobbyState& state, HWND window) {
    for (auto& scroll : state.scroll_controls) {
        if (scroll.window == window) {
            return &scroll;
        }
    }
    return nullptr;
}

HWND list_for_scroll(OnlineLobbyState& state,
    const OnlineLobbyScrollControl& scroll) {
    if (&scroll == &state.scroll_controls[0]) {
        return state.game_list;
    }
    if (&scroll == &state.scroll_controls[1]) {
        return state.chat_list;
    }
    return nullptr;
}

void subclass_control(HWND window) {
    if (window != nullptr) {
        SetWindowLongPtrA(window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&online_lobby_control_proc));
    }
}

bool create_button_from_spec(OnlineLobbyState& state, int spec_index,
    int layout_index) {
    OnlineLobbyButtonSpec spec = kButtonSpecs[spec_index];
    OnlineLobbyLayoutRect rect = layout_at(state,
        static_cast<std::size_t>(layout_index));
    rect = arrange_simplified_main_action(state, spec.id, rect);
    if (spec.id == kOnlineLobbySendButtonId ||
        spec.id == kOnlineLobbyWhisperButtonId ||
        spec.id == kOnlineLobbyEmoticonButtonId) {
        rect = arrange_chat_composer_control(state, rect);
    }
    LegacyImageButtonControl& button =
        state.buttons[static_cast<std::size_t>(spec_index)];
    if (!CreateLegacyImageButtonWindow(button, state.window, spec.text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }

    if (spec.id == kOnlineLobbySendButtonId) {
        SetWindowLongPtrA(button.window, GWL_STYLE, kSendButtonStyle);
    } else {
        SetWindowLongPtrA(button.window, GWL_STYLE, kOwnerDrawStyle);
    }

    if (spec.hide_when_main_disabled && state.create_join_disabled) {
        const u32 disabled_record =
            spec.id == kOnlineLobbyCreateGameButtonId ? 0x29 : 0x2c;
        LoadLegacyImageButtonBitmaps(button, disabled_record, disabled_record);
        EnableWindow(button.window, FALSE);
    } else {
        LoadLegacyImageButtonBitmaps(button, spec.normal_record,
            spec.pressed_record);
    }
    return true;
}

void show_id(OnlineLobbyState& state, int id, int command) {
    if (LegacyImageButtonControl* button = button_by_id(state, id)) {
        if (button->window != nullptr) {
            ShowWindow(button->window, command);
        }
    }
}

template <typename T>
void show_group(OnlineLobbyState& state, const T& ids, int command) {
    for (int id : ids) {
        show_id(state, id, command);
    }
}

void send_small_async_packet(OnlineLobbyState& state, u32 code, u32 packet_bytes) {
    if (packet_bytes < 0x0d) {
        return;
    }
    std::vector<u8> packet(packet_bytes, 0);
    const u32 packet_type = 3;
    std::memcpy(packet.data(), &packet_type, sizeof(packet_type));
    std::memcpy(packet.data() + 4, &code, sizeof(code));
    std::memcpy(packet.data() + 8, &packet_bytes, sizeof(packet_bytes));
    if (state.callbacks.send_async_packet != nullptr) {
        state.callbacks.send_async_packet(packet.data(), packet.size(),
            state.callbacks.user_data);
    }
}

const char* local_player_chat_name(const OnlineLobbyState& state) {
    if (!state.local_player_name.empty()) {
        return state.local_player_name.c_str();
    }
    if (!state.lobby_name.empty()) {
        return state.lobby_name.c_str();
    }
    return "";
}

void queue_memo_packet(MemoWindowState& memo_state, const void* packet,
    i32 byte_count) {
    auto* lobby_state =
        static_cast<OnlineLobbyState*>(memo_state.callbacks.user_data);
    if (lobby_state == nullptr || lobby_state->callbacks.send_async_packet == nullptr ||
        packet == nullptr || byte_count <= 0) {
        return;
    }
    lobby_state->callbacks.send_async_packet(packet,
        static_cast<std::size_t>(byte_count),
        lobby_state->callbacks.user_data);
}

void open_memo_from_lobby(OnlineLobbyState& state, HWND owner, int recipient_tab) {
    MemoWindowState& memo = memo_window_state();
    memo.current_recipient_tab = recipient_tab == 0 ? 0 : 1;
    if (state.callbacks.open_memo != nullptr) {
        state.callbacks.open_memo(owner, state.instance, state.callbacks.user_data);
        return;
    }
    memo.callbacks.queue_packet = queue_memo_packet;
    memo.callbacks.user_data = &state;
    CreateMemoWindow(memo, owner, state.instance);
}

void destroy_child_windows(OnlineLobbyState& state) {
    if (state.rich_edit_ole != nullptr) {
        state.rich_edit_ole->Release();
        state.rich_edit_ole = nullptr;
    }
    if (state.game_list != nullptr) {
        FreeOnlineLobbyGameListPayloads(state.game_list);
        DestroyWindow(state.game_list);
        state.game_list = nullptr;
    }
    if (state.chat_list != nullptr) {
        FreeOnlineLobbyListPayloads(state.chat_list);
        DestroyWindow(state.chat_list);
        state.chat_list = nullptr;
    }
    if (state.chat_edit != nullptr) {
        DestroyWindow(state.chat_edit);
        state.chat_edit = nullptr;
    }
    DestroyOnlineLobbyScrollControls(state);
    DestroyOnlineLobbyImageButtons(state);
}

void release_resources(OnlineLobbyState& state) {
    // Clear this first so a transition callback cannot mistake a window whose
    // controls are currently being destroyed for a resumable lobby.
    state.resources_ready = false;
    RestoreOnlineLobbyAccelerators(state);
    DestroyOnlineLobbyBackgroundBitmap(state);
    DestroyOnlineLobbyIconTileSheet(state);
    destroy_child_windows(state);
    state.layout_rects.clear();
}

void synchronize_scroll_to_list(OnlineLobbyState& state,
    OnlineLobbyScrollControl& scroll, HWND list) {
    if (scroll.window == nullptr || list == nullptr) {
        return;
    }
    const int top_index = GetLegacyCustomScrollControlValue(scroll.control);
    SendMessageA(list, LB_SETTOPINDEX, static_cast<WPARAM>(top_index), 0);
    if (GetFocus() != state.chat_edit && state.chat_edit != nullptr) {
        SetFocus(state.chat_edit);
    }
}

} // namespace

OnlineLobbyState& online_lobby_state() {
    return g_online_lobby_state;
}

void InitializeOnlineLobbyBackgroundBitmapStatic(OnlineLobbyState& state) {
    InitializeOnlineLobbyBackgroundBitmap(state);
    RegisterOnlineLobbyBackgroundBitmapDestructor(state);
}

void InitializeOnlineLobbyBackgroundBitmap(OnlineLobbyState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterOnlineLobbyBackgroundBitmapDestructor(OnlineLobbyState&) {
    register_atexit_once(g_background_bitmap_destructor_registered,
        shutdown_global_background_bitmap);
}

void DestroyOnlineLobbyBackgroundBitmap(OnlineLobbyState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

#define DEFINE_ONLINE_LOBBY_SCROLL_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Slot, Id) \
    void StaticName(OnlineLobbyState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(OnlineLobbyState& state) { \
        initialize_online_lobby_scroll_slot(state, Slot, Id); \
    } \
    void RegisterName(OnlineLobbyState&) { \
        register_atexit_once(g_scroll_destructor_registered[Slot], \
            shutdown_global_scroll_slot<Slot>); \
    } \
    void DestroyName(OnlineLobbyState& state) { \
        destroy_online_lobby_scroll_slot(state, Slot); \
    }

DEFINE_ONLINE_LOBBY_SCROLL_LIFETIME(InitializeOnlineLobbyGameListScrollStatic,
    InitializeOnlineLobbyGameListScrollControl,
    RegisterOnlineLobbyGameListScrollDestructor,
    DestroyOnlineLobbyGameListScrollControl, 0, kOnlineLobbyGameListScrollId)
DEFINE_ONLINE_LOBBY_SCROLL_LIFETIME(InitializeOnlineLobbyChatListScrollStatic,
    InitializeOnlineLobbyChatListScrollControl,
    RegisterOnlineLobbyChatListScrollDestructor,
    DestroyOnlineLobbyChatListScrollControl, 1, kOnlineLobbyChatListScrollId)

#undef DEFINE_ONLINE_LOBBY_SCROLL_LIFETIME

void InitializeOnlineLobbyScrollControls(OnlineLobbyState& state) {
    InitializeOnlineLobbyGameListScrollStatic(state);
    InitializeOnlineLobbyChatListScrollStatic(state);
}

void DestroyOnlineLobbyScrollControls(OnlineLobbyState& state) {
    DestroyOnlineLobbyGameListScrollControl(state);
    DestroyOnlineLobbyChatListScrollControl(state);
}

#define DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Slot) \
    void StaticName(OnlineLobbyState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(OnlineLobbyState& state) { \
        initialize_online_lobby_button_slot(state, Slot); \
    } \
    void RegisterName(OnlineLobbyState&) { \
        register_atexit_once(g_button_destructor_registered[Slot], \
            shutdown_global_button_slot<Slot>); \
    } \
    void DestroyName(OnlineLobbyState& state) { \
        destroy_online_lobby_button_slot(state, Slot); \
    }

DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyLobbyNameButtonStatic,
    InitializeOnlineLobbyLobbyNameButton,
    RegisterOnlineLobbyLobbyNameButtonDestructor,
    DestroyOnlineLobbyLobbyNameButton, 0)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbySendButtonStatic,
    InitializeOnlineLobbySendButton,
    RegisterOnlineLobbySendButtonDestructor,
    DestroyOnlineLobbySendButton, 1)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyWhisperButtonStatic,
    InitializeOnlineLobbyWhisperButton,
    RegisterOnlineLobbyWhisperButtonDestructor,
    DestroyOnlineLobbyWhisperButton, 2)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyEmoticonsButtonStatic,
    InitializeOnlineLobbyEmoticonsButton,
    RegisterOnlineLobbyEmoticonsButtonDestructor,
    DestroyOnlineLobbyEmoticonsButton, 3)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyMainTabButtonStatic,
    InitializeOnlineLobbyMainTabButton,
    RegisterOnlineLobbyMainTabButtonDestructor,
    DestroyOnlineLobbyMainTabButton, 4)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendsTabButtonStatic,
    InitializeOnlineLobbyFriendsTabButton,
    RegisterOnlineLobbyFriendsTabButtonDestructor,
    DestroyOnlineLobbyFriendsTabButton, 5)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildTabButtonStatic,
    InitializeOnlineLobbyGuildTabButton,
    RegisterOnlineLobbyGuildTabButtonDestructor,
    DestroyOnlineLobbyGuildTabButton, 6)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyPersonalTabButtonStatic,
    InitializeOnlineLobbyPersonalTabButton,
    RegisterOnlineLobbyPersonalTabButtonDestructor,
    DestroyOnlineLobbyPersonalTabButton, 7)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyTabBackgroundButtonStatic,
    InitializeOnlineLobbyTabBackgroundButton,
    RegisterOnlineLobbyTabBackgroundButtonDestructor,
    DestroyOnlineLobbyTabBackgroundButton, 8)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyChangeLobbyButtonStatic,
    InitializeOnlineLobbyChangeLobbyButton,
    RegisterOnlineLobbyChangeLobbyButtonDestructor,
    DestroyOnlineLobbyChangeLobbyButton, 9)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyCreateGameButtonStatic,
    InitializeOnlineLobbyCreateGameButton,
    RegisterOnlineLobbyCreateGameButtonDestructor,
    DestroyOnlineLobbyCreateGameButton, 10)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyJoinGameButtonStatic,
    InitializeOnlineLobbyJoinGameButton,
    RegisterOnlineLobbyJoinGameButtonDestructor,
    DestroyOnlineLobbyJoinGameButton, 11)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyMyAvatarButtonStatic,
    InitializeOnlineLobbyMyAvatarButton,
    RegisterOnlineLobbyMyAvatarButtonDestructor,
    DestroyOnlineLobbyMyAvatarButton, 12)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyViewRankButtonStatic,
    InitializeOnlineLobbyViewRankButton,
    RegisterOnlineLobbyViewRankButtonDestructor,
    DestroyOnlineLobbyViewRankButton, 13)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyCancelButtonStatic,
    InitializeOnlineLobbyCancelButton,
    RegisterOnlineLobbyCancelButtonDestructor,
    DestroyOnlineLobbyCancelButton, 14)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendDisplayButtonStatic,
    InitializeOnlineLobbyFriendDisplayButton,
    RegisterOnlineLobbyFriendDisplayButtonDestructor,
    DestroyOnlineLobbyFriendDisplayButton, 15)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendAddButtonStatic,
    InitializeOnlineLobbyFriendAddButton,
    RegisterOnlineLobbyFriendAddButtonDestructor,
    DestroyOnlineLobbyFriendAddButton, 16)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendRemoveButtonStatic,
    InitializeOnlineLobbyFriendRemoveButton,
    RegisterOnlineLobbyFriendRemoveButtonDestructor,
    DestroyOnlineLobbyFriendRemoveButton, 17)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendSendMessageButtonStatic,
    InitializeOnlineLobbyFriendSendMessageButton,
    RegisterOnlineLobbyFriendSendMessageButtonDestructor,
    DestroyOnlineLobbyFriendSendMessageButton, 18)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyFriendSendMemoButtonStatic,
    InitializeOnlineLobbyFriendSendMemoButton,
    RegisterOnlineLobbyFriendSendMemoButtonDestructor,
    DestroyOnlineLobbyFriendSendMemoButton, 19)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildDisplayButtonStatic,
    InitializeOnlineLobbyGuildDisplayButton,
    RegisterOnlineLobbyGuildDisplayButtonDestructor,
    DestroyOnlineLobbyGuildDisplayButton, 20)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSiteButtonStatic,
    InitializeOnlineLobbyGuildSiteButton,
    RegisterOnlineLobbyGuildSiteButtonDestructor,
    DestroyOnlineLobbyGuildSiteButton, 21)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSendMessageButtonStatic,
    InitializeOnlineLobbyGuildSendMessageButton,
    RegisterOnlineLobbyGuildSendMessageButtonDestructor,
    DestroyOnlineLobbyGuildSendMessageButton, 22)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSendMemoButtonStatic,
    InitializeOnlineLobbyGuildSendMemoButton,
    RegisterOnlineLobbyGuildSendMemoButtonDestructor,
    DestroyOnlineLobbyGuildSendMemoButton, 23)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubDisplayButtonStatic,
    InitializeOnlineLobbyGuildSubDisplayButton,
    RegisterOnlineLobbyGuildSubDisplayButtonDestructor,
    DestroyOnlineLobbyGuildSubDisplayButton, 24)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubSiteButtonStatic,
    InitializeOnlineLobbyGuildSubSiteButton,
    RegisterOnlineLobbyGuildSubSiteButtonDestructor,
    DestroyOnlineLobbyGuildSubSiteButton, 25)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubSendMessageButtonStatic,
    InitializeOnlineLobbyGuildSubSendMessageButton,
    RegisterOnlineLobbyGuildSubSendMessageButtonDestructor,
    DestroyOnlineLobbyGuildSubSendMessageButton, 26)
DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME(InitializeOnlineLobbyGuildSubSendMemoButtonStatic,
    InitializeOnlineLobbyGuildSubSendMemoButton,
    RegisterOnlineLobbyGuildSubSendMemoButtonDestructor,
    DestroyOnlineLobbyGuildSubSendMemoButton, 27)

#undef DEFINE_ONLINE_LOBBY_BUTTON_LIFETIME

void InitializeOnlineLobbyImageButtons(OnlineLobbyState& state) {
    InitializeOnlineLobbyLobbyNameButtonStatic(state);
    InitializeOnlineLobbySendButtonStatic(state);
    InitializeOnlineLobbyWhisperButtonStatic(state);
    InitializeOnlineLobbyEmoticonsButtonStatic(state);
    InitializeOnlineLobbyMainTabButtonStatic(state);
    InitializeOnlineLobbyFriendsTabButtonStatic(state);
    InitializeOnlineLobbyGuildTabButtonStatic(state);
    InitializeOnlineLobbyPersonalTabButtonStatic(state);
    InitializeOnlineLobbyTabBackgroundButtonStatic(state);
    InitializeOnlineLobbyChangeLobbyButtonStatic(state);
    InitializeOnlineLobbyCreateGameButtonStatic(state);
    InitializeOnlineLobbyJoinGameButtonStatic(state);
    InitializeOnlineLobbyMyAvatarButtonStatic(state);
    InitializeOnlineLobbyViewRankButtonStatic(state);
    InitializeOnlineLobbyCancelButtonStatic(state);
    InitializeOnlineLobbyFriendDisplayButtonStatic(state);
    InitializeOnlineLobbyFriendAddButtonStatic(state);
    InitializeOnlineLobbyFriendRemoveButtonStatic(state);
    InitializeOnlineLobbyFriendSendMessageButtonStatic(state);
    InitializeOnlineLobbyFriendSendMemoButtonStatic(state);
    InitializeOnlineLobbyGuildDisplayButtonStatic(state);
    InitializeOnlineLobbyGuildSiteButtonStatic(state);
    InitializeOnlineLobbyGuildSendMessageButtonStatic(state);
    InitializeOnlineLobbyGuildSendMemoButtonStatic(state);
    InitializeOnlineLobbyGuildSubDisplayButtonStatic(state);
    InitializeOnlineLobbyGuildSubSiteButtonStatic(state);
    InitializeOnlineLobbyGuildSubSendMessageButtonStatic(state);
    InitializeOnlineLobbyGuildSubSendMemoButtonStatic(state);
}

void DestroyOnlineLobbyImageButtons(OnlineLobbyState& state) {
    DestroyOnlineLobbyLobbyNameButton(state);
    DestroyOnlineLobbySendButton(state);
    DestroyOnlineLobbyWhisperButton(state);
    DestroyOnlineLobbyEmoticonsButton(state);
    DestroyOnlineLobbyMainTabButton(state);
    DestroyOnlineLobbyFriendsTabButton(state);
    DestroyOnlineLobbyGuildTabButton(state);
    DestroyOnlineLobbyPersonalTabButton(state);
    DestroyOnlineLobbyTabBackgroundButton(state);
    DestroyOnlineLobbyChangeLobbyButton(state);
    DestroyOnlineLobbyCreateGameButton(state);
    DestroyOnlineLobbyJoinGameButton(state);
    DestroyOnlineLobbyMyAvatarButton(state);
    DestroyOnlineLobbyViewRankButton(state);
    DestroyOnlineLobbyCancelButton(state);
    DestroyOnlineLobbyFriendDisplayButton(state);
    DestroyOnlineLobbyFriendAddButton(state);
    DestroyOnlineLobbyFriendRemoveButton(state);
    DestroyOnlineLobbyFriendSendMessageButton(state);
    DestroyOnlineLobbyFriendSendMemoButton(state);
    DestroyOnlineLobbyGuildDisplayButton(state);
    DestroyOnlineLobbyGuildSiteButton(state);
    DestroyOnlineLobbyGuildSendMessageButton(state);
    DestroyOnlineLobbyGuildSendMemoButton(state);
    DestroyOnlineLobbyGuildSubDisplayButton(state);
    DestroyOnlineLobbyGuildSubSiteButton(state);
    DestroyOnlineLobbyGuildSubSendMessageButton(state);
    DestroyOnlineLobbyGuildSubSendMemoButton(state);
}

void InitializeOnlineLobbyIconTileSheetStatic(OnlineLobbyState& state) {
    InitializeOnlineLobbyIconTileSheet(state);
    RegisterOnlineLobbyIconTileSheetDestructor(state);
}

void InitializeOnlineLobbyIconTileSheet(OnlineLobbyState& state) {
    InitializeBitmapTileSheetSelector(state.icon_sheet);
}

void RegisterOnlineLobbyIconTileSheetDestructor(OnlineLobbyState&) {
    register_atexit_once(g_icon_tile_sheet_destructor_registered,
        shutdown_global_icon_tile_sheet);
}

void DestroyOnlineLobbyIconTileSheet(OnlineLobbyState& state) {
    HandleBitmapTileSheetSelectorDestructor(state.icon_sheet);
}

void InstallOnlineLobbyAccelerators(OnlineLobbyState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kOnlineLobbyAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreOnlineLobbyAccelerators(OnlineLobbyState& state) {
    if (RankerMainWindowState().active_accelerator_window == state.window) {
        SetActiveAcceleratorState(nullptr, state.active_accelerators);
        DestroyAcceleratorTable(state.active_accelerators);
        state.active_accelerators = state.saved_accelerators;
        state.active_accelerator_window = state.saved_accelerator_window;
        SetActiveAcceleratorState(state.active_accelerator_window,
            state.active_accelerators);
    }
}

void ShowOnlineLobbyControl(HWND window) {
    if (window != nullptr) {
        ShowWindow(window, SW_SHOW);
    }
}

void HideOnlineLobbyControl(HWND window) {
    if (window != nullptr) {
        ShowWindow(window, SW_HIDE);
    }
}

void SetOnlineLobbyTab(OnlineLobbyState& state, OnlineLobbyTab tab) {
    (void)tab;
    state.active_tab = static_cast<int>(OnlineLobbyTab::Main);

    show_id(state, kOnlineLobbyMainTabButtonId, SW_SHOW);
    show_id(state, kOnlineLobbyFriendsTabButtonId, SW_HIDE);
    show_id(state, kOnlineLobbyGuildTabButtonId, SW_HIDE);
    show_id(state, kOnlineLobbyPersonalTabButtonId, SW_HIDE);
    show_id(state, kOnlineLobbyTabBackgroundButtonId, SW_HIDE);

    show_group(state, kMainTabControls, SW_HIDE);
    show_group(state, kFriendsTabControls, SW_HIDE);
    show_group(state, kGuildTabControls, SW_HIDE);
    show_group(state, kPersonalTabControls, SW_HIDE);
    show_group(state, kSimplifiedMainTabControls, SW_SHOW);
    show_id(state, IDCANCEL, SW_HIDE);

    for (int tab_id : {kOnlineLobbyMainTabButtonId}) {
        if (LegacyImageButtonControl* button = button_by_id(state, tab_id)) {
            if (button->window != nullptr) {
                RedrawWindow(button->window, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_UPDATENOW);
            }
        }
    }
}

bool ResumeOnlineLobbyWindow(OnlineLobbyState& state) {
    if (state.window == nullptr || !IsWindow(state.window) ||
        !state.resources_ready || !state.background.loaded) {
        return false;
    }
    if (state.async_tcp_socket != nullptr &&
        !RegisterLegacyAsyncTcpSocketEvents(*state.async_tcp_socket, state.window,
            kOnlineLobbyNetworkMessage, FD_READ | FD_WRITE | FD_CLOSE)) {
        return false;
    }

    // The online lobby stays alive underneath the owned Link-room popup.  Put
    // the control socket back on that existing HWND instead of destroying and
    // rebuilding the full resource tree during the room's cancel callback.
    // Refresh the advertised-game page after the server retires our room so a
    // stale self-owned entry cannot remain in the browser.
    clear_online_lobby_game_list(state);
    SetOnlineLobbyTab(state, OnlineLobbyTab::Main);
    send_small_async_packet(state, 0x0e, 0x15);
    queue_online_lobby_game_page_request(state, 0);
    ShowWindow(state.window, SW_SHOW);
    RedrawWindow(state.window, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (state.chat_edit != nullptr) {
        SetFocus(state.chat_edit);
    }
    return true;
}

bool CopySelectedOnlineLobbyGameListText(OnlineLobbyState& state, char* output,
    std::size_t output_size) {
    if (state.game_list == nullptr || output == nullptr || output_size == 0) {
        return false;
    }
    output[0] = '\0';
    LRESULT selected = SendMessageA(state.game_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return false;
    }
    SendMessageA(state.game_list, LB_GETTEXT, static_cast<WPARAM>(selected),
        reinterpret_cast<LPARAM>(output));
    output[output_size - 1] = '\0';
    return output[0] != '\0';
}

void* PostOnlineLobbyColoredTextPayload(OnlineLobbyState& state, const char* text) {
    if (state.window == nullptr || text == nullptr || *text == '\0') {
        return nullptr;
    }

    const std::size_t text_size = std::strlen(text) + 1;
    const std::size_t prefix_size = 1;
    const std::size_t byte_count = text_size + 0x0e;
    auto* bytes = static_cast<u8*>(allocate_locked_global(byte_count));
    if (bytes == nullptr) {
        return nullptr;
    }

    write_color_segment(bytes + 4, kOnlineLobbySystemText, prefix_size);
    bytes[7] = static_cast<u8>(prefix_size);
    bytes[8 + prefix_size] = 0;
    write_color_segment(bytes + 8 + prefix_size, kOnlineLobbySystemText,
        text_size);
    std::memcpy(bytes + 0x0c + prefix_size, text, text_size);
    PostMessageA(state.window, kOnlineLobbyCopiedTextMessage, 0,
        reinterpret_cast<LPARAM>(bytes));
    return bytes;
}

void* PostOnlineLobbySingleColorText(HWND owner, const char* text,
    u8 red, u8 green, u8 blue) {
    if (owner == nullptr || text == nullptr) {
        return nullptr;
    }
    const std::size_t text_size = std::strlen(text) + 1;
    auto* bytes = static_cast<u8*>(allocate_locked_global(text_size + 0x0d));
    if (bytes == nullptr) {
        return nullptr;
    }
    write_color_segment(bytes + 4, RGB(red, green, blue), text_size);
    std::memcpy(bytes + 8, text, text_size);
    PostMessageA(owner, kOnlineLobbyCopiedTextMessage, 0,
        reinterpret_cast<LPARAM>(bytes));
    return bytes;
}

bool AppendOnlineLobbyColoredListText(HWND owner, HWND list,
    const void* color_segment, const char* text) {
    (void)owner;
    if (list == nullptr || color_segment == nullptr || text == nullptr) {
        return false;
    }

    const auto* segment = static_cast<const u8*>(color_segment);
    const std::size_t text_size = std::strlen(text);
    auto* row = static_cast<u8*>(allocate_locked_global(text_size + 9));
    if (row == nullptr) {
        return false;
    }

    write_color_segment(row + 4, color_from_segment(segment), text_size);
    std::memcpy(row + 8, text, text_size + 1);
    return add_locked_list_row(list, row);
}

int CopyOnlineLobbyTextThatFitsWidth(HDC dc, const char* prefix, char* output,
    std::size_t output_size, const char* source, int max_width) {
    if (output == nullptr || output_size == 0) {
        return 0;
    }
    output[0] = '\0';
    if (dc == nullptr || source == nullptr || max_width <= 0) {
        IgnoreOnlineLobbyReservedTextHelper();
        return 0;
    }

    const int prefix_width = MeasureGdiTextWidth(dc, prefix == nullptr ? "" : prefix);
    int copied = 0;
    while (source[copied] != '\0' &&
        static_cast<std::size_t>(copied + 1) < output_size) {
        const int output_width = MeasureGdiTextWidth(dc, output);
        if (prefix_width + output_width + kOnlineLobbyChatContinuationMargin >=
            max_width) {
            break;
        }
        output[copied] = source[copied];
        ++copied;
        output[copied] = '\0';
    }

    return copied;
}

void IgnoreOnlineLobbyReservedTextHelper() {
}

void IgnoreOnlineLobbyMissingWhisperTarget() {
}

int TrimOnlineLobbyIconMarkedTextToWidth(HDC dc, const char* text, int max_width) {
    if (dc == nullptr || text == nullptr || max_width <= 0) {
        return 0;
    }
    int offset = 0;
    while (text[offset] != '\0' &&
        MeasureIconMarkedTextWidth(dc, text + offset) >= max_width) {
        const int marker = FindInlineIconMarker(text + offset);
        offset += marker == 0 ? 3 : 1;
    }
    return offset;
}

void InsertOnlineLobbyInlineIconRun(OnlineLobbyState& state, int insertion_offset) {
    if (state.rich_edit_ole == nullptr || state.chat_edit == nullptr) {
        return;
    }

    CHARRANGE range{};
    SendMessageA(state.chat_edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    int processed_count = 0;
    for (int i = 0; i < static_cast<int>(state.inline_icon_offsets.size()) &&
         state.inline_icon_offsets[static_cast<std::size_t>(i)] != -1; ++i) {
        const int offset = insertion_offset +
            state.inline_icon_offsets[static_cast<std::size_t>(i)];
        CHARRANGE icon_range{offset, offset};
        SendMessageA(state.chat_edit, EM_EXSETSEL, 0,
            reinterpret_cast<LPARAM>(&icon_range));
        HBITMAP bitmap = CreateBitmapTileSheetCellBitmapByIndex(state.icon_sheet,
            state.inline_icon_indices[static_cast<std::size_t>(i)]);
        if (bitmap != nullptr) {
            InsertBitmapAsRichEditOleObject(state.rich_edit_ole, bitmap,
                state.inline_icon_indices[static_cast<std::size_t>(i)]);
        }
        ++processed_count;
    }
    range.cpMin += processed_count;
    range.cpMax += processed_count;
    SendMessageA(state.chat_edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
}

void CaptureOnlineLobbyInlineIconRanges(OnlineLobbyState& state) {
    if (state.rich_edit_ole == nullptr || state.chat_edit == nullptr) {
        return;
    }

    CHARRANGE range{};
    SendMessageA(state.chat_edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    const int begin = range.cpMin;
    const int end = range.cpMax;
    if (begin == end) {
        state.inline_icon_offsets[0] = -1;
        return;
    }

    int out = 0;
    const LONG object_count = state.rich_edit_ole->GetObjectCount();
    for (LONG index = 0; index < object_count &&
         out < static_cast<int>(state.inline_icon_offsets.size()) - 1; ++index) {
        REOBJECT object{};
        object.cbStruct = sizeof(object);
        HRESULT result = state.rich_edit_ole->GetObject(index, &object,
            REO_GETOBJ_POLEOBJ | REO_GETOBJ_POLESITE);
        if (FAILED(result)) {
            break;
        }

        if (begin <= object.cp && object.cp < end) {
            state.inline_icon_offsets[static_cast<std::size_t>(out)] =
                object.cp - begin;
            state.inline_icon_indices[static_cast<std::size_t>(out)] =
                object.dwUser;
            ++out;
        }
        if (object.poleobj != nullptr) {
            object.poleobj->Release();
        }
        if (object.polesite != nullptr) {
            object.polesite->Release();
        }
    }
    state.inline_icon_offsets[static_cast<std::size_t>(out)] = -1;
}

bool ApplyOnlineLobbyChatFloodGuard(OnlineLobbyState& state, const char* text) {
    // The reconstructed WizardNet server does not impose a chat rate limit.
    // Keep this compatibility entry point for callers, but accept every
    // non-empty message instead of applying the legacy one-minute mute.
    (void)state;
    return text != nullptr && *text != '\0';
}

bool ReadOnlineLobbyRichEditTextWithInlineIcons(OnlineLobbyState& state,
    char* output, std::size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return false;
    }
    output[0] = '\0';
    if (state.chat_edit == nullptr) {
        return false;
    }

    if (state.rich_edit_ole != nullptr) {
        const LONG object_count = state.rich_edit_ole->GetObjectCount();
        for (LONG index = object_count - 1; index >= 0; --index) {
            REOBJECT object{};
            object.cbStruct = sizeof(object);
            HRESULT result = state.rich_edit_ole->GetObject(index, &object,
                REO_GETOBJ_POLEOBJ | REO_GETOBJ_POLESITE);
            if (FAILED(result)) {
                break;
            }

            const DWORD icon_index = object.dwUser & 0xffU;
            char marker[4] = {'(', static_cast<char>('a' + icon_index), ')', '\0'};
            SendMessageA(state.chat_edit, EM_SETSEL, object.cp, object.cp + 1);
            SendMessageA(state.chat_edit, EM_REPLACESEL, FALSE,
                reinterpret_cast<LPARAM>(marker));
            if (object.poleobj != nullptr) {
                object.poleobj->Release();
            }
            if (object.polesite != nullptr) {
                object.polesite->Release();
            }
        }
    }

    const int limit = static_cast<int>(std::min<std::size_t>(output_size, 199));
    if (state.chat_edit_original_proc != nullptr) {
        CallWindowProcA(state.chat_edit_original_proc, state.chat_edit, WM_GETTEXT,
            static_cast<WPARAM>(limit), reinterpret_cast<LPARAM>(output));
    } else {
        GetWindowTextA(state.chat_edit, output, limit);
    }
    output[output_size - 1] = '\0';
    return output[0] != '\0';
}

bool SendOnlineLobbyChatEditText(OnlineLobbyState& state, HWND owner,
    HWND edit, const char* whisper_target, COLORREF prefix_color,
    COLORREF text_color) {
    if (edit == nullptr) {
        return false;
    }

    char text[200]{};
    if (!ReadOnlineLobbyRichEditTextWithInlineIcons(state, text, sizeof(text))) {
        return false;
    }
    if (!ApplyOnlineLobbyChatFloodGuard(state, text)) {
        return false;
    }

    char prefix[160]{};
    const char* chat_name = local_player_chat_name(state);
    if (chat_name[0] != '\0') {
        std::snprintf(prefix, sizeof(prefix), "%s> ", chat_name);
    }

    char outbound_text[240]{};
    if (whisper_target != nullptr && *whisper_target != '\0') {
        std::snprintf(outbound_text, sizeof(outbound_text), "/w %s %s",
            whisper_target, text);
    }
    else {
        std::snprintf(outbound_text, sizeof(outbound_text), "%s", text);
    }

    const std::size_t prefix_size = std::strlen(prefix) + 1;
    const std::size_t text_size = std::strlen(outbound_text) + 1;
    const std::size_t payload_size = prefix_size + text_size + 8;
    std::vector<u8> payload(payload_size, 0);
    write_color_segment(payload.data(), prefix_color, prefix_size);
    std::memcpy(payload.data() + 4, prefix, prefix_size);
    write_color_segment(payload.data() + 4 + prefix_size, text_color, text_size);
    std::memcpy(payload.data() + 8 + prefix_size, outbound_text, text_size);

    if (state.callbacks.send_async_packet != nullptr) {
        constexpr std::size_t kLegacyHeaderBytes = 0x0d;
        std::vector<u8> packet(kLegacyHeaderBytes + payload.size(), 0);
        const u32 header0 = 0;
        const u32 opcode = 0x2a;
        const u32 packet_size = static_cast<u32>(packet.size());
        std::memcpy(packet.data(), &header0, sizeof(header0));
        std::memcpy(packet.data() + 4, &opcode, sizeof(opcode));
        std::memcpy(packet.data() + 8, &packet_size, sizeof(packet_size));
        std::memcpy(packet.data() + kLegacyHeaderBytes, payload.data(), payload.size());
        state.callbacks.send_async_packet(packet.data(), packet.size(),
            state.callbacks.user_data);
    }

    const std::size_t local_size = payload.size() + 4;
    auto* local_echo = static_cast<u8*>(allocate_locked_global(local_size));
    if (local_echo != nullptr) {
        write_color_segment(local_echo + 4, kOnlineLobbyLocalPrompt, prefix_size);
        std::memcpy(local_echo + 8, prefix, prefix_size);
        write_color_segment(local_echo + 8 + prefix_size, kOnlineLobbyLocalText,
            text_size);
        std::memcpy(local_echo + 12 + prefix_size, outbound_text, text_size);
        PostMessageA(owner, kOnlineLobbyCopiedTextMessage, 0,
            reinterpret_cast<LPARAM>(local_echo));
    }

    SendMessageA(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(""));
    return true;
}

void AppendOnlineLobbyChatPayload(OnlineLobbyState& state, const void* locked_payload) {
    if (locked_payload == nullptr || state.chat_list == nullptr) {
        free_locked_global_pointer(locked_payload);
        return;
    }

    const auto* payload = static_cast<const u8*>(locked_payload);
    const u8* first = payload + 4;
    const u8* second = next_segment(first);
    const std::size_t first_length = first[3];
    const std::size_t second_length = second == nullptr ? 0 : second[3];
    const std::size_t row_size = first_length + second_length + 8;

    HDC dc = state.window != nullptr ? GetDC(state.window) : nullptr;
    HGDIOBJ previous_font = dc != nullptr ?
        SelectObject(dc, online_lobby_chat_font()) : nullptr;
    const int width = chat_list_width(state);
    const int first_width = first_length != 0 ?
        MeasureGdiTextWidth(dc, text_from_segment(first)) : 0;
    const int second_width = second_length != 0 ?
        MeasureGdiTextWidth(dc, text_from_segment(second)) : 0;

    if (width < first_width + second_width + kOnlineLobbyChatWrapGap &&
        second != nullptr && second_length != 0) {
        append_chat_row_bytes(state.chat_list, first, first_length + 8,
            first_length + 4);

        char measure_line[kOnlineLobbyChatBufferSize]{};
        char output_line[kOnlineLobbyChatBufferSize]{};
        const char* source = text_from_segment(second);
        const char* cursor = source;
        while (*cursor != '\0') {
            while (*cursor == kOnlineLobbyWrapDelimiter) {
                ++cursor;
            }
            if (*cursor == '\0') {
                break;
            }
            const char* token_begin = cursor;
            while (*cursor != '\0' && *cursor != kOnlineLobbyWrapDelimiter) {
                ++cursor;
            }
            std::string token(token_begin, cursor - token_begin);
            const int token_width = MeasureGdiTextWidth(dc, token.c_str());
            if (width < token_width + kOnlineLobbyChatContinuationMargin) {
                const char* long_cursor = token.c_str();
                while (*long_cursor != '\0' &&
                    width < MeasureGdiTextWidth(dc, long_cursor) +
                        kOnlineLobbyChatContinuationMargin) {
                    char piece[kOnlineLobbyChatBufferSize]{};
                    const int copied = CopyOnlineLobbyTextThatFitsWidth(dc,
                        output_line, piece, sizeof(piece), long_cursor, width);
                    if (copied <= 0) {
                        break;
                    }
                    append_capped(output_line, sizeof(output_line), piece);
                    AppendOnlineLobbyColoredListText(state.window, state.chat_list,
                        second, output_line);
                    long_cursor += std::strlen(piece);
                    measure_line[0] = '\0';
                    output_line[0] = '\0';
                }
                append_capped(measure_line, sizeof(measure_line), long_cursor);
                append_capped(measure_line, sizeof(measure_line), " ");
                append_capped(output_line, sizeof(output_line), long_cursor);
                append_capped(output_line, sizeof(output_line), " ");
                continue;
            }

            append_capped(measure_line, sizeof(measure_line), token.c_str());
            append_capped(measure_line, sizeof(measure_line), " ");
            if (MeasureGdiTextWidth(dc, measure_line) +
                    kOnlineLobbyChatContinuationMargin < width) {
                append_capped(output_line, sizeof(output_line), token.c_str());
                append_capped(output_line, sizeof(output_line), " ");
            } else {
                if (output_line[0] != '\0') {
                    AppendOnlineLobbyColoredListText(state.window, state.chat_list,
                        second, output_line);
                }
                measure_line[0] = '\0';
                output_line[0] = '\0';
                append_capped(measure_line, sizeof(measure_line), token.c_str());
                append_capped(measure_line, sizeof(measure_line), " ");
                append_capped(output_line, sizeof(output_line), token.c_str());
                append_capped(output_line, sizeof(output_line), " ");
            }
        }
        if (std::strlen(output_line) > 1) {
            AppendOnlineLobbyColoredListText(state.window, state.chat_list, second,
                output_line);
        }
    } else {
        append_chat_row_bytes(state.chat_list, payload + 4, row_size, row_size);
    }

    trim_chat_rows(state.chat_list);
    const int count = static_cast<int>(SendMessageA(state.chat_list, LB_GETCOUNT, 0, 0));
    sync_chat_scrollbar(state, count);
    if (dc != nullptr) {
        if (previous_font != nullptr && previous_font != HGDI_ERROR) {
            SelectObject(dc, previous_font);
        }
        ReleaseDC(state.window, dc);
    }
    free_locked_global_pointer(locked_payload);
}

void FreeOnlineLobbyListPayloads(HWND list) {
    if (list == nullptr) {
        return;
    }
    const int count = static_cast<int>(SendMessageA(list, LB_GETCOUNT, 0, 0));
    for (int index = 0; index < count; ++index) {
        LRESULT item_data = SendMessageA(list, LB_GETITEMDATA,
            static_cast<WPARAM>(index), 0);
        if (item_data != LB_ERR && item_data != 0) {
            free_locked_global_pointer(reinterpret_cast<const void*>(item_data));
        }
    }
}

void FreeOnlineLobbyGameListPayloads(HWND list) {
    if (list == nullptr) {
        return;
    }
    const int count = static_cast<int>(SendMessageA(list, LB_GETCOUNT, 0, 0));
    for (int index = 0; index < count; ++index) {
        LRESULT item_data = SendMessageA(list, LB_GETITEMDATA,
            static_cast<WPARAM>(index), 0);
        if (item_data != LB_ERR && item_data != 0) {
            ::operator delete(reinterpret_cast<void*>(item_data));
        }
    }
}

void post_online_lobby_user_record_list(OnlineLobbyState& state,
    const u8* packet, std::size_t byte_count) {
    constexpr std::size_t kCountOffset = 0x0d;
    constexpr std::size_t kRecordOffset = 0x11;
    constexpr std::size_t kRecordStride = 0x44;
    constexpr std::size_t kRecordNameOffset = 0x00;
    constexpr std::size_t kRecordNameBytes = 0x20;
    constexpr std::size_t kRecordStatusOffset = 0x20;
    constexpr std::size_t kRecordLocationOffset = 0x24;
    constexpr std::size_t kRecordLocationBytes = 0x20;
    constexpr const char* kSeparator =
        "---------------------------------------------";

    const i32 count =
        WrappedU32ToI32(packet_u32(packet, byte_count, kCountOffset));
    if (count < 0) {
        PostOnlineLobbyColoredTextPayload(state,
            startup_message_row(219, "The selected player is not a guild member."));
        return;
    }

    char text[256]{};
    std::snprintf(text, sizeof(text),
        startup_message_row(212, "Total %d player(s)"), count);
    PostOnlineLobbyColoredTextPayload(state, text);
    if (count <= 0) {
        return;
    }

    PostOnlineLobbyColoredTextPayload(state, kSeparator);
    std::size_t record_offset = kRecordOffset;
    for (i32 index = 0; index < count; ++index) {
        if (record_offset + kRecordStride > byte_count) {
            break;
        }

        const std::string name = packet_fixed_string(packet, byte_count,
            record_offset + kRecordNameOffset, kRecordNameBytes);
        const std::string location = packet_fixed_string(packet, byte_count,
            record_offset + kRecordLocationOffset, kRecordLocationBytes);
        const u32 status = packet_u32(packet, byte_count,
            record_offset + kRecordStatusOffset);

        switch (status) {
        case 0:
            std::snprintf(text, sizeof(text),
                startup_message_row(213, "%s : offline"), name.c_str());
            PostOnlineLobbyColoredTextPayload(state, text);
            break;
        case 1:
            std::snprintf(text, sizeof(text),
                startup_message_row(214, "%s : in lobby channel %s"),
                name.c_str(), location.c_str());
            PostOnlineLobbyColoredTextPayload(state, text);
            break;
        case 2:
            std::snprintf(text, sizeof(text),
                startup_message_row(215, "%s : joined %s's game"),
                name.c_str(), location.c_str());
            PostOnlineLobbyColoredTextPayload(state, text);
            break;
        default:
            break;
        }

        record_offset += kRecordStride;
    }
    PostOnlineLobbyColoredTextPayload(state, kSeparator);
}

bool DispatchOnlineLobbyServerPacket(OnlineLobbyState& state, const u8* packet,
    std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x0c) {
        return false;
    }
    const u32 opcode = read_le32(packet + 4);
    switch (opcode) {
    case 2: {
        const u32 code = packet_u32(packet, byte_count, 0x0d);
        show_online_lobby_modal_message(state, online_lobby_status_message(code),
            RGB(0xfa, 0x0a, 0x0a));
        return true;
    }
    case 7: {
        const std::string name = packet_string(packet, byte_count, 0x0d);
        add_online_lobby_game_row(state, name.c_str(), packet, byte_count, false);
        return true;
    }
    case 0x0f:
        clear_online_lobby_game_list(state);
        queue_online_lobby_game_list_reset_request(state);
        return true;
    case 0x11:
        queue_online_lobby_game_page_request(state, 0);
        return true;
    case 0x13: {
        const i32 page =
            WrappedU32ToI32(packet_u32(packet, byte_count, 0x0d));
        if (page < 0) {
            update_online_lobby_game_count_label(state);
            return true;
        }
        const std::string name = packet_string(packet, byte_count, 0x15);
        add_online_lobby_game_row(state, name.c_str(), packet, byte_count, true);
        queue_online_lobby_game_page_request(state, static_cast<u32>(page + 1));
        return true;
    }
    case 0x23:
        remove_online_lobby_game_by_id(state, packet_u32(packet, byte_count, 0x0d));
        return true;
    case 0x38:
        open_online_lobby_player_profile(state, packet, byte_count);
        return true;
    case 0x42:
        show_online_lobby_modal_message(state,
            startup_message_row(102,
                "A user with the same CD key has connected."),
            RGB(0xfa, 0x0a, 0x0a));
        return true;
    case 0x6c:
        prepare_barter_prompt_from_packet(state, packet, byte_count);
        return true;
    case 0x76:
    case 0x7e:
        post_online_lobby_user_record_list(state, packet, byte_count);
        return true;
    case 0x78:
    case 0x7a: {
        const u32 code = packet_u32(packet, byte_count, 0x0d);
        const char* text = nullptr;
        if (opcode == 0x78) {
            text = code == 0 ?
                startup_message_row(216, "Added.") :
                code == 1 ?
                startup_message_row(211, "Already in the friend list.") :
                nullptr;
        }
        else if (opcode == 0x7a) {
            text = code == 0 ?
                startup_message_row(217, "Removed.") :
                code == 1 ?
                startup_message_row(218, "Not in the friend list.") :
                nullptr;
        }
        if (text != nullptr) {
            PostOnlineLobbyColoredTextPayload(state, text);
        }
        return true;
    }
    case 0x80: {
        const std::string target = packet_string(packet, byte_count, 0x0d);
        if (!target.empty()) {
            ShellExecuteA(nullptr, "open", target.c_str(), nullptr, nullptr, SW_SHOW);
        }
        return true;
    }
    case 0x86:
        prepare_named_prompt_from_packet(state, packet, byte_count);
        return true;
    case 0x88:
        PostOnlineLobbyColoredTextPayload(state,
            startup_message_row(266, "A memo has arrived."));
        return true;
    default:
        return false;
    }
}

void DispatchOnlineLobbyNetworkMessage(OnlineLobbyState& state, LPARAM event) {
    const WORD network_event = LOWORD(event);
    if (network_event == FD_CLOSE) {
        state.connected = false;
        if (state.callbacks.show_message != nullptr) {
            state.callbacks.show_message(state.window,
                "Online lobby connection closed.", RGB(10, 10, 250),
                state.callbacks.user_data);
        }
        ShutdownLegacyUdpNetworking();
        if (state.async_tcp_socket != nullptr) {
            CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
        }
        HWND parent = state.parent_window;
        HINSTANCE instance = state.instance;
        LPARAM return_context = state.return_context;
        DestroyWindow(state.window);
        if (state.callbacks.return_to_connect_frontend != nullptr) {
            state.callbacks.return_to_connect_frontend(parent, instance,
                return_context, state.callbacks.user_data);
        }
        return;
    }
    if (network_event != FD_READ || state.async_tcp_socket == nullptr) {
        return;
    }

    ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= 4) {
        const u32 packet_type = read_le32(payload);
        if (packet_type == 0) {
            if (byte_count < 8) {
                break;
            }
            const u32 first_text_length = payload[7];
            const u32 second_length_offset = first_text_length + 0x0b;
            if (static_cast<u32>(byte_count) <= second_length_offset) {
                break;
            }
            const u32 second_text_length = payload[second_length_offset];
            const u32 packet_count = first_text_length + 0x0c + second_text_length;
            if (packet_count == 0 || static_cast<u32>(byte_count) < packet_count) {
                break;
            }
            post_locked_payload(state.window, kOnlineLobbyCopiedTextMessage,
                payload, packet_count);
            ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
                static_cast<i32>(packet_count));
            payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
            byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
            continue;
        }

        if (byte_count < 0x0c) {
            break;
        }
        const u32 packet_count = read_le32(payload + 8);
        if (packet_count == 0 || static_cast<u32>(byte_count) < packet_count) {
            break;
        }
        if (!DispatchOnlineLobbyServerPacket(state, payload, packet_count)) {
            break;
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
            static_cast<i32>(packet_count));
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

bool CreateOnlineLobbyWindow(OnlineLobbyState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context, bool reconnect_packet) {
    state.parent_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.resources_ready = false;
    InitializeOnlineLobbyBackgroundBitmapStatic(state);
    InitializeOnlineLobbyIconTileSheetStatic(state);
    InitializeOnlineLobbyScrollControls(state);
    InitializeOnlineLobbyImageButtons(state);

    state.layout_rects.clear();
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kOnlineLobbyLayoutTrcRecord)) {
        release_resources(state);
        return false;
    }
    state.layout_rects = copy_layout_record(layout.table);

    LoadBitmapTileSheetSelectorResource(state.icon_sheet);

    OnlineLobbyLayoutRect root = layout_at(state, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(
              parent, root.width, root.height)
        : RankerFrontendWindowOrigin();
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Lobby", "Lobby",
        style, origin.x, origin.y, root.width, root.height, parent,
        nullptr, instance, nullptr);
    if (state.window == nullptr) {
        release_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&online_lobby_window_proc));
    if (state.async_tcp_socket != nullptr &&
        !RegisterLegacyAsyncTcpSocketEvents(*state.async_tcp_socket, state.window,
            kOnlineLobbyNetworkMessage, FD_READ | FD_WRITE | FD_CLOSE)) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }

    for (int i = 0; i < static_cast<int>(kOnlineLobbyButtonCount); ++i) {
        // Layout entries 2..6 belong to the two lists, their scroll bars,
        // and the chat edit.  Every button after the lobby-name control
        // therefore begins at entry 7.
        const int layout_index = OnlineLobbyButtonLayoutIndex(i);
        if (!create_button_from_spec(state, i, layout_index)) {
            DestroyWindow(state.window);
            state.window = nullptr;
            return false;
        }
    }

    OnlineLobbyLayoutRect game_rect = layout_at(state, 2);
    state.game_list = CreateWindowExA(0, "listbox", "", kListBoxGameStyle,
        game_rect.x, game_rect.y, game_rect.width, game_rect.height, state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOnlineLobbyGameListId)),
        instance, nullptr);
    OnlineLobbyLayoutRect chat_rect = layout_at(state, 4);
    state.chat_list = CreateWindowExA(0, "listbox", "", kChatListStyle,
        chat_rect.x, chat_rect.y, chat_rect.width, chat_rect.height, state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOnlineLobbyChatListId)),
        instance, nullptr);
    OnlineLobbyLayoutRect game_scroll_rect = layout_at(state, 3);
    OnlineLobbyScrollControl& game_scroll = state.scroll_controls[0];
    if (CreateLegacyCustomScrollControlWindow(game_scroll.control, state.window, "",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOnlineLobbyGameListScrollId)),
            false, game_scroll_rect.x, game_scroll_rect.y,
            game_scroll_rect.width, game_scroll_rect.height)) {
        game_scroll.window = GetLegacyCustomScrollControlWindow(game_scroll.control);
        game_scroll.original_window_proc = game_scroll.control.original_window_proc;
        LoadLegacyCustomScrollControlBitmaps(game_scroll.control, 0x13, 0x13,
            0x14, 0x14, 0x15, 0x16);
        SetLegacyCustomScrollControlMetrics(game_scroll.control,
            game_scroll_rect.width, game_scroll_rect.width,
            game_scroll_rect.width, game_scroll_rect.width);
        configure_scroll_for_list(game_scroll, state.game_list, 0, 0, false);
    }

    OnlineLobbyLayoutRect chat_scroll_rect = layout_at(state, 5);
    OnlineLobbyScrollControl& chat_scroll = state.scroll_controls[1];
    if (CreateLegacyCustomScrollControlWindow(chat_scroll.control, state.window, "",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOnlineLobbyChatListScrollId)),
            false, chat_scroll_rect.x, chat_scroll_rect.y,
            chat_scroll_rect.width, chat_scroll_rect.height)) {
        chat_scroll.window = GetLegacyCustomScrollControlWindow(chat_scroll.control);
        chat_scroll.original_window_proc = chat_scroll.control.original_window_proc;
        LoadLegacyCustomScrollControlBitmaps(chat_scroll.control, 0x13, 0x13,
            0x14, 0x14, 0x15, 0x17);
        SetLegacyCustomScrollControlMetrics(chat_scroll.control,
            chat_scroll_rect.width, chat_scroll_rect.width,
            chat_scroll_rect.width, chat_scroll_rect.width);
        configure_scroll_for_list(chat_scroll, state.chat_list, 0, 0, false);
    }
    OnlineLobbyLayoutRect edit_rect =
        arrange_chat_composer_control(state, layout_at(state, 6));
    state.chat_edit = CreateWindowExA(0, "RICHEDIT", "", kRichEditStyle,
        edit_rect.x, edit_rect.y, edit_rect.width, edit_rect.height, state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOnlineLobbyChatEditId)),
        instance, nullptr);
    if (state.game_list != nullptr) {
        state.game_list_original_proc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrA(state.game_list, GWLP_WNDPROC));
        subclass_control(state.game_list);
    }
    if (state.chat_list != nullptr) {
        SendMessageA(state.chat_list, WM_SETFONT,
            reinterpret_cast<WPARAM>(online_lobby_chat_font()), FALSE);
        SendMessageA(state.chat_list, LB_SETITEMHEIGHT, 0,
            static_cast<LPARAM>(online_lobby_chat_row_height(state)));
        state.chat_list_original_proc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrA(state.chat_list, GWLP_WNDPROC));
        subclass_control(state.chat_list);
    }
    for (auto& scroll : state.scroll_controls) {
        if (scroll.window != nullptr) {
            scroll.original_window_proc = reinterpret_cast<WNDPROC>(
                GetWindowLongPtrA(scroll.window, GWLP_WNDPROC));
            subclass_control(scroll.window);
        }
    }
    if (state.chat_edit != nullptr) {
        state.chat_edit_original_proc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrA(state.chat_edit, GWLP_WNDPROC));
        SendMessageA(state.chat_edit, EM_GETOLEINTERFACE, 0,
            reinterpret_cast<LPARAM>(&state.rich_edit_ole));
        SendMessageA(state.chat_edit, EM_LIMITTEXT, 200, 0);
        SendMessageA(state.chat_edit, WM_SETFONT,
            reinterpret_cast<WPARAM>(online_lobby_chat_font()), TRUE);
        SendMessageA(state.chat_edit, EM_SETBKGNDCOLOR, FALSE,
            static_cast<LPARAM>(RGB(0, 0, 0)));
        CHARFORMATA chat_format{};
        chat_format.cbSize = sizeof(chat_format);
        chat_format.dwMask = CFM_COLOR | CFM_SIZE;
        chat_format.crTextColor = kOnlineLobbyWhite;
        // RichEdit 1.0 does not reliably retain WM_SETFONT.  Set the matching
        // 12-point (240 twip) character height explicitly as well.
        chat_format.yHeight = 240;
        SendMessageA(state.chat_edit, EM_SETCHARFORMAT, SCF_DEFAULT,
            reinterpret_cast<LPARAM>(&chat_format));
        SendMessageA(state.chat_edit, EM_SETCHARFORMAT, SCF_ALL,
            reinterpret_cast<LPARAM>(&chat_format));
        subclass_control(state.chat_edit);
    }

    if (!load_reconstructed_lobby_background(state) &&
        !LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
            kOnlineLobbyBackgroundBitmapRecord)) {
        DestroyWindow(state.window);
        return false;
    }
    InstallOnlineLobbyAccelerators(state);
    SetOnlineLobbyTab(state, OnlineLobbyTab::Main);
    if (state.chat_edit != nullptr) {
        SetFocus(state.chat_edit);
    }
    state.resources_ready = true;
    state.connected = true;

    if (reconnect_packet) {
        send_small_async_packet(state, 0x0e, 0x15);
    } else {
        send_small_async_packet(state, 0x12, 0x11);
    }
    return true;
}

LRESULT HandleOnlineLobbyWindowMessage(OnlineLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.parent_window != nullptr) {
        SendMessageA(state.parent_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_ERASEBKGND:
        if (hwnd == state.window && state.background.loaded) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            StretchBitmapMemoryResourceToClient(state.background, dc, state.window);
            paint_online_lobby_dynamic_chrome(state, dc);
            return TRUE;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kOnlineLobbyWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), RGB(0, 0, 0));
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DESTROY:
        release_resources(state);
        state.window = nullptr;
        return 0;
    case WM_PAINT: {
        if (hwnd == state.window) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            StretchBitmapMemoryResourceToClient(state.background, dc, state.window);
            paint_online_lobby_dynamic_chrome(state, dc);
            EndPaint(hwnd, &paint);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            break;
        }
        if (draw->CtlID == kOnlineLobbyNameButtonId) {
            SetTextColor(draw->hDC, RGB(0, 255, 0));
            SetBkMode(draw->hDC, TRANSPARENT);
            RECT rect = draw->rcItem;
            DrawTextA(draw->hDC, state.lobby_name.c_str(), -1, &rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return TRUE;
        }
        if (draw->CtlID == kOnlineLobbyGameListId) {
            draw_online_lobby_game_item(*draw);
            return TRUE;
        }
        if (draw->CtlID == kOnlineLobbyChatListId) {
            draw_online_lobby_chat_item(state, *draw);
            return TRUE;
        }
        if (draw->CtlID >= kOnlineLobbyMainTabButtonId &&
            draw->CtlID <= kOnlineLobbyPersonalTabButtonId) {
            if (LegacyImageButtonControl* button = button_by_id(state, draw->CtlID)) {
                DrawLegacyImageButtonItem(*button, *draw);
                return TRUE;
            }
        }
        if (LegacyImageButtonControl* button = button_by_id(state, draw->CtlID)) {
            DrawLegacyImageButtonItem(*button, *draw);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify_code = HIWORD(wparam);
        switch (id) {
        case IDCANCEL:
            play_online_lobby_click_sound();
            if (state.callbacks.return_to_connect_frontend != nullptr) {
                state.callbacks.return_to_connect_frontend(state.parent_window,
                    state.instance, state.return_context, state.callbacks.user_data);
            }
            DestroyWindow(hwnd);
            return 0;
        case kOnlineLobbyGameListId:
            if (notify_code == LBN_SELCHANGE || notify_code == LBN_SELCANCEL) {
                focus_online_lobby_chat_edit(state);
            }
            if (notify_code == LBN_DBLCLK) {
                queue_online_lobby_selected_game_detail_command(state);
            }
            return 0;
        case kOnlineLobbyChatListId:
            if (notify_code == 0x100) {
                focus_online_lobby_chat_edit(state);
            }
            return 0;
        case kOnlineLobbyInsertSelectedNameCommandId:
            append_selected_online_lobby_game_name_to_edit(state);
            return 0;
        case kOnlineLobbyRichEditCopyCommandId:
        case kOnlineLobbyRichEditPasteCommandId:
        case kOnlineLobbyRichEditCutCommandId:
            handle_online_lobby_rich_edit_clipboard_command(state, id);
            return 0;
        case kOnlineLobbyMainTabButtonId:
            play_online_lobby_click_sound();
            SetOnlineLobbyTab(state, OnlineLobbyTab::Main);
            return 0;
        case kOnlineLobbyFriendsTabButtonId:
            play_online_lobby_click_sound();
            SetOnlineLobbyTab(state, OnlineLobbyTab::Friends);
            return 0;
        case kOnlineLobbyGuildTabButtonId:
            play_online_lobby_click_sound();
            SetOnlineLobbyTab(state, OnlineLobbyTab::Guild);
            return 0;
        case kOnlineLobbyPersonalTabButtonId:
            play_online_lobby_click_sound();
            SetOnlineLobbyTab(state, OnlineLobbyTab::Personal);
            return 0;
        case kOnlineLobbyChangeLobbyButtonId:
            play_online_lobby_click_sound();
            if (state.callbacks.open_change_lobby != nullptr) {
                state.callbacks.open_change_lobby(hwnd, state.instance,
                    state.callbacks.user_data);
            }
            return 0;
        case kOnlineLobbyCreateGameButtonId:
            play_online_lobby_click_sound();
            if (state.callbacks.open_create_game != nullptr) {
                state.callbacks.open_create_game(hwnd, state.instance,
                    state.callbacks.user_data);
            }
            return 0;
        case kOnlineLobbyJoinGameButtonId:
            play_online_lobby_click_sound();
            if (state.callbacks.open_free_server_lobby != nullptr) {
                state.callbacks.open_free_server_lobby(hwnd, state.instance,
                    state.callbacks.user_data);
            }
            return 0;
        case kOnlineLobbyEmoticonButtonId:
            play_online_lobby_click_sound();
            if (state.callbacks.open_emoticon_popup != nullptr) {
                POINT point{};
                GetCursorPos(&point);
                state.callbacks.open_emoticon_popup(hwnd, state.instance, point,
                    state.callbacks.user_data);
            }
            return 0;
        case kOnlineLobbySendButtonId:
            SendOnlineLobbyChatEditText(state, hwnd, state.chat_edit, nullptr,
                kOnlineLobbyNormalChatPrompt, kOnlineLobbyLocalText);
            return 0;
        case kOnlineLobbyWhisperButtonId:
            play_online_lobby_click_sound();
            send_online_lobby_whisper_to_selected_game(state, hwnd);
            return 0;
        case kOnlineLobbyViewRankButtonId:
            play_online_lobby_click_sound();
            if (state.active_tab == static_cast<int>(OnlineLobbyTab::Friends)) {
                PostMessageA(hwnd, WM_COMMAND, kOnlineLobbyFriendRemoveButtonId,
                    lparam);
            } else if (state.callbacks.open_view_rank != nullptr) {
                state.callbacks.open_view_rank(hwnd, state.instance,
                    state.callbacks.user_data);
            }
            return 0;
        case kOnlineLobbyMyAvatarButtonId:
            play_online_lobby_click_sound();
            if (state.active_tab == static_cast<int>(OnlineLobbyTab::Friends)) {
                PostMessageA(hwnd, WM_COMMAND, kOnlineLobbyFriendAddButtonId,
                    lparam);
                return 0;
            }
            if (state.callbacks.open_avatar != nullptr) {
                state.callbacks.open_avatar(hwnd, state.instance,
                    state.callbacks.user_data);
            }
            else {
                CreateAvatarWindow(avatar_window_state(), hwnd, state.instance);
            }
            return 0;
        case kOnlineLobbyFriendDisplayButtonId:
            play_online_lobby_click_sound();
            if (state.active_tab == static_cast<int>(OnlineLobbyTab::Guild)) {
                PostMessageA(hwnd, WM_COMMAND, kOnlineLobbyGuildDisplayButtonId,
                    lparam);
            } else {
                queue_online_lobby_simple_command(state, 0x75);
            }
            return 0;
        case kOnlineLobbyFriendAddButtonId:
            play_online_lobby_click_sound();
            queue_online_lobby_selected_game_command(state, 0x77,
                kStartupFriendAddTargetPromptRow,
                "Select a user to add as a friend.");
            return 0;
        case kOnlineLobbyFriendRemoveButtonId:
            play_online_lobby_click_sound();
            queue_online_lobby_selected_game_command(state, 0x79,
                kStartupFriendRemoveTargetPromptRow,
                "Select a user to remove from friends.");
            return 0;
        case kOnlineLobbyFriendSendMessageButtonId:
            play_online_lobby_click_sound();
            if (state.active_tab == static_cast<int>(OnlineLobbyTab::Guild)) {
                PostMessageA(hwnd, WM_COMMAND, kOnlineLobbyGuildSendMessageButtonId,
                    lparam);
            } else if (state.active_tab == static_cast<int>(OnlineLobbyTab::Personal)) {
                PostMessageA(hwnd, WM_COMMAND, kOnlineLobbyGuildSubSiteButtonId,
                    lparam);
            } else {
                queue_online_lobby_text_command(state, hwnd, 0x7b);
            }
            return 0;
        case kOnlineLobbyGuildDisplayButtonId:
            play_online_lobby_click_sound();
            queue_online_lobby_simple_command(state, 0x7d);
            return 0;
        case kOnlineLobbyGuildSiteButtonId:
            play_online_lobby_click_sound();
            queue_online_lobby_simple_command(state, 0x7f);
            return 0;
        case kOnlineLobbyGuildSendMessageButtonId:
            play_online_lobby_click_sound();
            queue_online_lobby_text_command(state, hwnd, 0x81);
            return 0;
        case kOnlineLobbyGuildSubDisplayButtonId:
            play_online_lobby_click_sound();
            queue_online_lobby_named_game_command(state,
                local_player_chat_name(state));
            return 0;
        case kOnlineLobbyGuildSubSiteButtonId:
            play_online_lobby_click_sound();
            if (state.callbacks.open_search_lobby != nullptr) {
                state.callbacks.open_search_lobby(hwnd, state.instance,
                    state.callbacks.user_data);
            }
            return 0;
        case kOnlineLobbyFriendSendMemoButtonId:
            open_memo_from_lobby(state, hwnd, 0);
            play_online_lobby_click_sound();
            return 0;
        case kOnlineLobbyGuildSendMemoButtonId:
            play_online_lobby_click_sound();
            open_memo_from_lobby(state, hwnd, 1);
            return 0;
        case kOnlineLobbyGuildSubSendMessageButtonId:
            play_online_lobby_click_sound();
            open_memo_from_lobby(state, hwnd, 0);
            return 0;
        case kOnlineLobbyGuildSubSendMemoButtonId:
            play_online_lobby_click_sound();
            send_online_lobby_help_chat(state, hwnd);
            return 0;
        default:
            break;
        }
        break;
    }
    case kOnlineLobbyNetworkMessage:
        DispatchOnlineLobbyNetworkMessage(state, lparam);
        return 0;
    case kOnlineLobbyCopiedTextMessage:
        if (lparam != 0) {
            AppendOnlineLobbyChatPayload(state,
                reinterpret_cast<const void*>(lparam));
        }
        return 0;
    case kOnlinePromptAcceptMessage:
        HandleOnlineLobbyPromptResult(state, wparam, false);
        return 0;
    case kOnlinePromptCancelMessage:
        HandleOnlineLobbyPromptResult(state, wparam, true);
        return 0;
    case kOnlineLobbyMakeSelectedIconBitmapMessage: {
        const int selected_index =
            NotifyBitmapTileSheetSelectionIfValid(state.icon_sheet);
        if (selected_index >= 0 && state.rich_edit_ole != nullptr) {
            HBITMAP bitmap =
                CreateSelectedBitmapTileSheetCellBitmap(state.icon_sheet);
            if (bitmap != nullptr) {
                InsertBitmapAsRichEditOleObject(state.rich_edit_ole, bitmap,
                    static_cast<u32>(selected_index));
            }
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleOnlineLobbyControlMessage(OnlineLobbyState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (message == WM_PAINT) {
        OnlineLobbyScrollControl* scroll = scroll_by_window(state, hwnd);
        if (scroll != nullptr) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            DrawLegacyCustomScrollControl(scroll->control, dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
    }

    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.parent_window != nullptr) {
        SendMessageA(state.parent_window, message, wparam, lparam);
    }

    if (OnlineLobbyScrollControl* scroll = scroll_by_id(state, id)) {
        const bool scroll_handled = HandleLegacyCustomScrollControlMouseMessage(
            scroll->control, message, wparam, lparam);
        if (scroll_handled) {
            synchronize_scroll_to_list(state, *scroll,
                list_for_scroll(state, *scroll));
        }
        return CallWindowProcA(scroll->original_window_proc, hwnd, message,
            wparam, lparam);
    }
    if (has_original_proc_for_id(id)) {
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    }
    return 0;
}

} // namespace ranker

#endif
