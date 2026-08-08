#include "ranker_memo_window.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_online_dialogs.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kListStyle = WS_CHILD | WS_VISIBLE | LBS_NOTIFY |
    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr DWORD kReadEditStyle = WS_CHILD | WS_VISIBLE | ES_MULTILINE |
    ES_READONLY | ES_WANTRETURN;
constexpr DWORD kWriteEditStyle = WS_CHILD | WS_VISIBLE | ES_MULTILINE |
    ES_WANTRETURN;
constexpr COLORREF kMemoWhite = RGB(255, 255, 255);
constexpr COLORREF kMemoSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kMemoYellow = RGB(255, 255, 0);
constexpr COLORREF kMemoGray = RGB(200, 200, 200);
constexpr COLORREF kMemoBlack = RGB(0, 0, 0);
constexpr COLORREF kMemoSelectedBlue = static_cast<COLORREF>(0x00ff0000);
constexpr int kMemoForwardFocusCommandId = 0x9c41;
constexpr std::size_t kMemoPacketHeaderSize = 0x0d;
constexpr std::size_t kMemoRecipientNameBytes = 0x20;
constexpr std::size_t kMemoBodyBytes = 0xff;
constexpr std::size_t kMemoInboxEntryBytes = 0x3c;

MemoWindowState g_memo_window_state;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK memo_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleMemoWindowMessage(g_memo_window_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK memo_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleMemoControlMessage(g_memo_window_state, hwnd, message,
        wparam, lparam);
}

std::vector<MemoLayoutRect> copy_layout_record(const FrontendLayoutRectTable& table) {
    std::vector<MemoLayoutRect> rects;
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

MemoLayoutRect layout_at(const MemoWindowState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return MemoLayoutRect{};
}

u32 read_u32(const u8* payload, i32 byte_count, std::size_t offset) {
    if (payload == nullptr || byte_count < 0 ||
        offset + sizeof(u32) > static_cast<std::size_t>(byte_count)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, payload + offset, sizeof(value));
    return value;
}

void write_u32(std::vector<u8>& packet, std::size_t offset, u32 value) {
    if (offset + sizeof(value) > packet.size()) {
        return;
    }
    std::memcpy(packet.data() + offset, &value, sizeof(value));
}

std::vector<u8> make_packet(u32 opcode, u32 byte_count) {
    std::vector<u8> packet(byte_count, 0);
    write_u32(packet, 0, 3);
    write_u32(packet, 4, opcode);
    write_u32(packet, 8, byte_count);
    return packet;
}

std::string fixed_string(const u8* data, std::size_t byte_count) {
    if (data == nullptr || byte_count == 0) {
        return {};
    }
    const char* begin = reinterpret_cast<const char*>(data);
    const char* end = begin + byte_count;
    const char* nul = std::find(begin, end, '\0');
    return std::string(begin, nul);
}

template <std::size_t N>
void copy_fixed_string(std::array<char, N>& target, const u8* data,
    std::size_t byte_count) {
    target.fill(0);
    if (data == nullptr || byte_count == 0) {
        return;
    }
    const std::size_t copied = std::min<std::size_t>(N - 1, byte_count);
    std::memcpy(target.data(), data, copied);
    target[N - 1] = '\0';
}

void copy_packet_string(std::vector<u8>& packet, std::size_t offset,
    std::size_t byte_count, const char* text) {
    if (offset >= packet.size()) {
        return;
    }
    const std::size_t available = std::min(byte_count, packet.size() - offset);
    std::memset(packet.data() + offset, 0, available);
    if (text == nullptr || available == 0) {
        return;
    }
    const std::size_t copied = std::min(available - 1, std::strlen(text));
    std::memcpy(packet.data() + offset, text, copied);
}

void set_text(HWND window, const char* text) {
    if (window != nullptr) {
        SetWindowTextA(window, text == nullptr ? "" : text);
    }
}

void redraw_window(HWND window) {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
}

void subclass_window(HWND window) {
    if (window != nullptr) {
        SetWindowLongPtrA(window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(memo_control_proc));
    }
}

void subclass_text_control(MemoTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    subclass_window(control.window);
}

bool create_text_control(MemoTextControl& control, HWND parent, HINSTANCE instance,
    const char* class_name, DWORD style, int id, const MemoLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        return false;
    }
    subclass_text_control(control);
    return true;
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const MemoLayoutRect& rect, u32 normal_record,
    u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    if (normal_record != 0 || pressed_record != 0) {
        LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    }
    subclass_window(button.window);
    return true;
}

void queue_packet(MemoWindowState& state, const std::vector<u8>& packet) {
    if (packet.empty()) {
        return;
    }
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet.data(),
            static_cast<i32>(packet.size()));
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket,
            const_cast<u8*>(packet.data()), static_cast<i32>(packet.size()),
            nullptr);
    }
}

void play_click_sound(MemoWindowState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

void show_memo_message(MemoWindowState& state, const char* text,
    COLORREF color = kMemoSoftWhite) {
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(state.window, text, color,
            state.callbacks.user_data);
        return;
    }
    CreateOnlineModelessPrompt(online_modeless_prompt_state(), state.window,
        state.instance, text == nullptr ? "" : text, color, false, 0, 0);
}

void forward_memo_network_message(MemoWindowState& state, WPARAM wparam,
    LPARAM lparam) {
    if (state.callbacks.forward_network_message != nullptr) {
        state.callbacks.forward_network_message(state, wparam, lparam);
        return;
    }
    if (state.parent_window != nullptr && IsWindow(state.parent_window)) {
        PostMessageA(state.parent_window, kMemoNetworkMessage, wparam, lparam);
    }
}

void configure_scroll_for_list(LegacyCustomScrollControl& scroll, HWND list,
    int item_count, int visible_rows) {
    const int rows = std::max(1, visible_rows);
    const int max_top = std::max(0, item_count - rows);
    SetLegacyCustomScrollControlPageStep(scroll, rows);
    SetLegacyCustomScrollControlRange(scroll, 0, max_top, false);
    SetLegacyCustomScrollControlVisible(scroll, max_top > 0);
    SetLegacyCustomScrollControlValue(scroll,
        std::min(GetLegacyCustomScrollControlValue(scroll), max_top), true);
    if (list != nullptr) {
        SendMessageA(list, LB_SETTOPINDEX,
            static_cast<WPARAM>(GetLegacyCustomScrollControlValue(scroll)), 0);
    }
}

std::vector<std::array<char, 0x20>>& active_recipients(MemoWindowState& state) {
    return state.current_recipient_tab == 0 ? state.friend_recipients :
        state.guild_recipients;
}

const std::vector<std::array<char, 0x20>>& active_recipients(
    const MemoWindowState& state) {
    return state.current_recipient_tab == 0 ? state.friend_recipients :
        state.guild_recipients;
}

int selected_list_index(HWND list) {
    if (list == nullptr) {
        return -1;
    }
    const LRESULT selected = SendMessageA(list, LB_GETCURSEL, 0, 0);
    return selected == LB_ERR ? -1 : static_cast<int>(selected);
}

int selected_memo_id(const MemoWindowState& state) {
    const int selected = selected_list_index(state.inbox_list.window);
    if (selected < 0) {
        return -1;
    }
    const LRESULT data = SendMessageA(state.inbox_list.window, LB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    return data == LB_ERR ? -1 : static_cast<int>(data);
}

bool selected_recipient_name(const MemoWindowState& state, char* out,
    std::size_t out_size) {
    if (out == nullptr || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    const int selected = selected_list_index(state.recipient_list.window);
    if (selected < 0) {
        return false;
    }
    return SendMessageA(state.recipient_list.window, LB_GETTEXT,
        static_cast<WPARAM>(selected), reinterpret_cast<LPARAM>(out)) != LB_ERR;
}

void send_initial_memo_request(MemoWindowState& state) {
    queue_packet(state, make_packet(0x6d, 0x0d));
}

void send_read_memo_request(MemoWindowState& state) {
    const int memo_id = selected_memo_id(state);
    if (memo_id < 0) {
        return;
    }
    std::vector<u8> packet = make_packet(0x73, 0x11);
    write_u32(packet, kMemoPacketHeaderSize, static_cast<u32>(memo_id));
    queue_packet(state, packet);
}

void delete_selected_received_memo(MemoWindowState& state) {
    const int selected = selected_list_index(state.inbox_list.window);
    const int memo_id = selected_memo_id(state);
    if (selected < 0 || memo_id < 0) {
        return;
    }
    std::vector<u8> packet = make_packet(0x71, 0x11);
    write_u32(packet, kMemoPacketHeaderSize, static_cast<u32>(memo_id));
    queue_packet(state, packet);

    set_text(state.read_edit.window, "");
    state.inbox_entries.erase(std::remove_if(state.inbox_entries.begin(),
                                  state.inbox_entries.end(),
                                  [memo_id](const MemoInboxEntry& entry) {
                                      return entry.memo_id ==
                                          static_cast<u32>(memo_id);
                                  }),
        state.inbox_entries.end());
    PopulateMemoInboxList(state);
}

void delete_selected_friend_recipient(MemoWindowState& state) {
    if (state.current_recipient_tab != 0) {
        show_memo_message(state,
            startup_message_row(265, "Select a friend name to delete first."));
        return;
    }
    const int selected = selected_list_index(state.recipient_list.window);
    char name[0x20]{};
    if (selected < 0 ||
        !selected_recipient_name(state, name, sizeof(name))) {
        show_memo_message(state,
            startup_message_row(265, "Select a friend name to delete first."));
        return;
    }
    std::vector<u8> packet = make_packet(0x79, 0x2d);
    copy_packet_string(packet, kMemoPacketHeaderSize, kMemoRecipientNameBytes, name);
    queue_packet(state, packet);

    if (static_cast<std::size_t>(selected) < state.friend_recipients.size()) {
        state.friend_recipients.erase(state.friend_recipients.begin() + selected);
        PopulateMemoRecipientList(state, 0);
    }
}

void send_selected_memo(MemoWindowState& state) {
    char recipient[0x20]{};
    if (!selected_recipient_name(state, recipient, sizeof(recipient))) {
        return;
    }

    char body[kMemoBodyBytes]{};
    if (state.write_edit.window != nullptr) {
        GetWindowTextA(state.write_edit.window, body, static_cast<int>(sizeof(body)));
    }
    if (body[0] == '\0') {
        return;
    }

    std::vector<u8> packet = make_packet(0x6f, 300);
    copy_packet_string(packet, kMemoPacketHeaderSize, kMemoRecipientNameBytes,
        recipient);
    copy_packet_string(packet, 0x2d, kMemoBodyBytes, body);
    queue_packet(state, packet);
    set_text(state.write_edit.window, "");
}

void close_memo_window(MemoWindowState& state, HWND hwnd) {
    if (state.parent_window != nullptr) {
        SetRankerMainWindowFrontendRouteWindow(state.parent_window);
    }
    if (hwnd != nullptr) {
        DestroyWindow(hwnd);
    }
    if (state.callbacks.return_to_online_lobby != nullptr) {
        state.callbacks.return_to_online_lobby(state);
    }
}

void parse_memo_list_packet(MemoWindowState& state, const u8* payload,
    i32 byte_count) {
    const u32 inbox_count = read_u32(payload, byte_count, 0x0d);
    const u32 friend_count = read_u32(payload, byte_count, 0x11);
    const u32 guild_count = read_u32(payload, byte_count, 0x15);
    std::size_t offset = 0x19;

    state.inbox_entries.clear();
    state.friend_recipients.clear();
    state.guild_recipients.clear();

    const std::size_t payload_size = byte_count < 0 ? 0 :
        static_cast<std::size_t>(byte_count);
    for (u32 i = 0; i < inbox_count; ++i) {
        if (offset + kMemoInboxEntryBytes > payload_size) {
            break;
        }
        const u8* record = payload + offset;
        MemoInboxEntry entry{};
        entry.memo_id = read_u32(payload, byte_count, offset);
        copy_fixed_string(entry.sender, record + 4, entry.sender.size());
        copy_fixed_string(entry.date, record + 0x24, entry.date.size());
        entry.read = read_u32(payload, byte_count, offset + 0x38) != 0;
        state.inbox_entries.push_back(entry);
        offset += kMemoInboxEntryBytes;
    }

    for (u32 i = 0; i < friend_count; ++i) {
        if (offset + kMemoRecipientNameBytes > payload_size) {
            break;
        }
        std::array<char, 0x20> name{};
        copy_fixed_string(name, payload + offset, name.size());
        state.friend_recipients.push_back(name);
        offset += kMemoRecipientNameBytes;
    }

    for (u32 i = 0; i < guild_count; ++i) {
        if (offset + kMemoRecipientNameBytes > payload_size) {
            break;
        }
        std::array<char, 0x20> name{};
        copy_fixed_string(name, payload + offset, name.size());
        state.guild_recipients.push_back(name);
        offset += kMemoRecipientNameBytes;
    }

    PopulateMemoRecipientList(state, state.current_recipient_tab);
    PopulateMemoInboxList(state);
}

void draw_list_item(HWND list, const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1) || list == nullptr) {
        return;
    }
    char text[256]{};
    if (SendMessageA(list, LB_GETTEXT, draw.itemID,
            reinterpret_cast<LPARAM>(text)) == LB_ERR) {
        return;
    }

    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    rect.left += 4;
    rect.top += 1;
    rect.right -= 2;
    rect.bottom -= 1;
    SetTextColor(draw.hDC, kMemoGray);
    SetBkColor(draw.hDC,
        (draw.itemState & ODS_SELECTED) != 0 ? kMemoSelectedBlue : kMemoBlack);
    SetBkMode(draw.hDC, OPAQUE);
    DrawTextA(draw.hDC, text, -1, &rect,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

bool paint_background_if_current(MemoWindowState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToClient(state.background, dc, state.window);
    EndPaint(hwnd, &paint);
    return true;
}

void release_window_resources(MemoWindowState& state) {
    RestoreMemoWindowAccelerators(state);
    DestroyLegacyImageButtonControl(state.tab_background_button);
    DestroyLegacyImageButtonControl(state.friend_tab_button);
    DestroyLegacyImageButtonControl(state.guild_tab_button);
    DestroyLegacyImageButtonControl(state.delete_received_button);
    DestroyLegacyImageButtonControl(state.delete_friend_button);
    DestroyLegacyImageButtonControl(state.send_button);
    DestroyLegacyImageButtonControl(state.close_button);
    DestroyLegacyCustomScrollControl(state.inbox_scroll);
    DestroyLegacyCustomScrollControl(state.recipient_scroll);
    if (state.inbox_list.window != nullptr) {
        DestroyWindow(state.inbox_list.window);
    }
    if (state.read_edit.window != nullptr) {
        DestroyWindow(state.read_edit.window);
    }
    if (state.recipient_list.window != nullptr) {
        DestroyWindow(state.recipient_list.window);
    }
    if (state.write_edit.window != nullptr) {
        DestroyWindow(state.write_edit.window);
    }
    state.inbox_list = MemoTextControl{};
    state.read_edit = MemoTextControl{};
    state.recipient_list = MemoTextControl{};
    state.write_edit = MemoTextControl{};
    ReleaseBitmapMemoryResource(state.background);
    ReleaseBitmapMemoryResource(state.active_tab_bitmap);
    state.inbox_entries.clear();
    state.friend_recipients.clear();
    state.guild_recipients.clear();
    state.window = nullptr;
}

bool create_memo_controls(MemoWindowState& state, HINSTANCE instance) {
    if (!create_text_control(state.inbox_list, state.window, instance, "listbox",
            kListStyle, kMemoInboxListId, layout_at(state, 4)) ||
        !create_text_control(state.read_edit, state.window, instance, "edit",
            kReadEditStyle, kMemoReadEditId, layout_at(state, 6)) ||
        !create_text_control(state.recipient_list, state.window, instance, "listbox",
            kListStyle, kMemoRecipientListId, layout_at(state, 7)) ||
        !create_text_control(state.write_edit, state.window, instance, "edit",
            kWriteEditStyle, kMemoWriteEditId, layout_at(state, 9))) {
        return false;
    }

    SendMessageA(state.read_edit.window, EM_LIMITTEXT, 0xfe, 0);
    SendMessageA(state.write_edit.window, EM_LIMITTEXT, 0xfe, 0);

    const MemoLayoutRect inbox_scroll_rect = layout_at(state, 5);
    if (!CreateLegacyCustomScrollControlWindow(state.inbox_scroll, state.window,
            "MemoInboxScroll",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMemoInboxScrollId)),
            false, inbox_scroll_rect.x, inbox_scroll_rect.y,
            inbox_scroll_rect.width, inbox_scroll_rect.height)) {
        return false;
    }
    LoadLegacyCustomScrollControlBitmaps(state.inbox_scroll,
        kMemoScrollUpBitmapRecord, 0, kMemoScrollDownBitmapRecord, 0,
        kMemoScrollThumbBitmapRecord, kMemoScrollTrackBitmapRecord);
    subclass_window(state.inbox_scroll.window);

    const MemoLayoutRect recipient_scroll_rect = layout_at(state, 8);
    if (!CreateLegacyCustomScrollControlWindow(state.recipient_scroll, state.window,
            "MemoRecipientScroll",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMemoRecipientScrollId)),
            false, recipient_scroll_rect.x, recipient_scroll_rect.y,
            recipient_scroll_rect.width, recipient_scroll_rect.height)) {
        return false;
    }
    LoadLegacyCustomScrollControlBitmaps(state.recipient_scroll,
        kMemoScrollUpBitmapRecord, 0, kMemoScrollDownBitmapRecord, 0,
        kMemoScrollThumbBitmapRecord, kMemoScrollTrackBitmapRecord);
    subclass_window(state.recipient_scroll.window);

    const int inbox_item_height = std::max(1, static_cast<int>(
        SendMessageA(state.inbox_list.window, LB_GETITEMHEIGHT, 0, 0)));
    const int recipient_item_height = std::max(1, static_cast<int>(
        SendMessageA(state.recipient_list.window, LB_GETITEMHEIGHT, 0, 0)));
    state.inbox_visible_rows = std::max(1,
        layout_at(state, 4).height / inbox_item_height);
    state.recipient_visible_rows = std::max(1,
        layout_at(state, 7).height / recipient_item_height);

    return create_image_button(state.delete_received_button, state.window, "Delete",
               kMemoDeleteReceivedButtonId, layout_at(state, 10),
               kMemoDeleteReceivedNormalBitmapRecord,
               kMemoDeleteReceivedPressedBitmapRecord) &&
        create_image_button(state.delete_friend_button, state.window, "Delete",
            kMemoDeleteFriendButtonId, layout_at(state, 11),
            kMemoDeleteFriendNormalBitmapRecord,
            kMemoDeleteFriendPressedBitmapRecord) &&
        create_image_button(state.send_button, state.window, "Send",
            kMemoSendButtonId, layout_at(state, 12), kMemoSendNormalBitmapRecord,
            kMemoSendPressedBitmapRecord) &&
        create_image_button(state.close_button, state.window, "Close",
            kMemoCloseButtonId, layout_at(state, 13), kMemoCloseNormalBitmapRecord,
            kMemoClosePressedBitmapRecord) &&
        create_image_button(state.friend_tab_button, state.window, "avatar_tab",
            kMemoFriendTabButtonId, layout_at(state, 2), 0, 0) &&
        create_image_button(state.guild_tab_button, state.window, "avatar_tab",
            kMemoGuildTabButtonId, layout_at(state, 3), 0, 0) &&
        create_image_button(state.tab_background_button, state.window, "avatar_tab",
            kMemoTabBackgroundButtonId, layout_at(state, 1),
            kMemoFriendTabBitmapRecord, kMemoFriendTabBitmapRecord);
}

WNDPROC original_proc_for_id(const MemoWindowState& state, int id) {
    switch (id) {
    case kMemoFriendTabButtonId:
        return state.friend_tab_button.original_window_proc;
    case kMemoGuildTabButtonId:
        return state.guild_tab_button.original_window_proc;
    case kMemoInboxListId:
        return state.inbox_list.original_window_proc;
    case kMemoInboxScrollId:
        return state.inbox_scroll.original_window_proc;
    case kMemoReadEditId:
        return state.read_edit.original_window_proc;
    case kMemoRecipientListId:
        return state.recipient_list.original_window_proc;
    case kMemoRecipientScrollId:
        return state.recipient_scroll.original_window_proc;
    case kMemoWriteEditId:
        return state.write_edit.original_window_proc;
    case kMemoDeleteReceivedButtonId:
        return state.delete_received_button.original_window_proc;
    case kMemoDeleteFriendButtonId:
        return state.delete_friend_button.original_window_proc;
    case kMemoSendButtonId:
        return state.send_button.original_window_proc;
    case kMemoCloseButtonId:
        return state.close_button.original_window_proc;
    default:
        return nullptr;
    }
}

} // namespace

MemoWindowState& memo_window_state() {
    return g_memo_window_state;
}

void MemoStaticResourceWrapperNN() {
    InitializeMemoWindowResources(g_memo_window_state);
}

void InitializeMemoWindowResources(MemoWindowState& state) {
    InitializeBitmapMemoryResource(state.background);
    InitializeBitmapMemoryResource(state.active_tab_bitmap);
    InitializeLegacyCustomScrollControl(state.inbox_scroll);
    InitializeLegacyCustomScrollControl(state.recipient_scroll);
    InitializeLegacyImageButtonControl(state.tab_background_button);
    InitializeLegacyImageButtonControl(state.friend_tab_button);
    InitializeLegacyImageButtonControl(state.guild_tab_button);
    InitializeLegacyImageButtonControl(state.delete_received_button);
    InitializeLegacyImageButtonControl(state.delete_friend_button);
    InitializeLegacyImageButtonControl(state.send_button);
    InitializeLegacyImageButtonControl(state.close_button);
}

void ReleaseMemoWindowResources(MemoWindowState& state) {
    release_window_resources(state);
}

void InstallMemoWindowAccelerators(MemoWindowState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kMemoAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreMemoWindowAccelerators(MemoWindowState& state) {
    if (RankerMainWindowState().active_accelerator_window != state.window) {
        return;
    }
    SetActiveAcceleratorState(nullptr, state.active_accelerators);
    DestroyAcceleratorTable(state.active_accelerators);
    state.active_accelerators = state.saved_accelerators;
    state.active_accelerator_window = state.saved_accelerator_window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

bool CreateMemoWindow(MemoWindowState& state, HWND parent, HINSTANCE instance,
    LegacyAsyncTcpSocket* async_tcp_socket) {
    if (state.window != nullptr) {
        return false;
    }
    const int initial_recipient_tab = state.current_recipient_tab == 0 ? 0 : 1;

    InitializeMemoWindowResources(state);
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance != nullptr ? instance : GetModuleHandleA(nullptr);
    state.async_tcp_socket = async_tcp_socket;
    FrontendLayoutTableOwner layout_table;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout_table.table,
            kMemoLayoutTrcRecord)) {
        release_window_resources(state);
        return false;
    }
    state.layout = copy_layout_record(layout_table.table);
    state.current_recipient_tab = initial_recipient_tab;
    state.inbox_visible_rows = 1;
    state.recipient_visible_rows = 1;

    const DWORD style =
        IsWindow(parent) && GetWindowLongPtrA(parent, GWL_STYLE) != 0 ?
        kWindowStyleWindowed : kWindowStyleFullscreen;
    const MemoLayoutRect window_rect = layout_at(state, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(
              parent, window_rect.width, window_rect.height)
        : RankerFrontendWindowOrigin();
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Memo", "Memo", style,
        origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, state.instance, nullptr);
    if (state.window == nullptr) {
        release_window_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(memo_window_proc));

    if (!create_memo_controls(state, state.instance)) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }

    const std::array<HWND, 5> font_windows{{
        state.window,
        state.inbox_list.window,
        state.read_edit.window,
        state.recipient_list.window,
        state.write_edit.window,
    }};
    for (HWND window : font_windows) {
        SendMessageA(window, WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kMemoBackgroundBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.active_tab_bitmap, "Jw2_19.trc",
        state.current_recipient_tab == 0 ?
            kMemoFriendTabBitmapRecord : kMemoGuildTabBitmapRecord);
    InstallMemoWindowAccelerators(state);
    SetRankerMainWindowFrontendRouteWindow(state.window);
    PopulateMemoRecipientList(state, state.current_recipient_tab);
    PopulateMemoInboxList(state);
    send_initial_memo_request(state);
    ShowWindow(state.window, SW_SHOW);
    SetFocus(state.write_edit.window);
    return true;
}

void PopulateMemoInboxList(MemoWindowState& state) {
    if (state.inbox_list.window == nullptr) {
        return;
    }
    const LRESULT previous_top =
        SendMessageA(state.inbox_list.window, LB_GETTOPINDEX, 0, 0);
    SendMessageA(state.inbox_list.window, LB_RESETCONTENT, 0, 0);

    int added = 0;
    for (const MemoInboxEntry& entry : state.inbox_entries) {
        char line[256]{};
        std::snprintf(line, sizeof(line), "%-16s  [%s]    READ(%c)",
            entry.sender.data(), entry.date.data(), entry.read ? 'O' : 'X');
        const LRESULT index = SendMessageA(state.inbox_list.window, LB_ADDSTRING,
            0, reinterpret_cast<LPARAM>(line));
        if (index != LB_ERR) {
            ++added;
            SendMessageA(state.inbox_list.window, LB_SETITEMDATA,
                static_cast<WPARAM>(index), static_cast<LPARAM>(entry.memo_id));
        }
    }

    int top = previous_top == LB_ERR ? 0 : static_cast<int>(previous_top);
    top = std::clamp(top, 0, std::max(0, added - state.inbox_visible_rows));
    SetLegacyCustomScrollControlValue(state.inbox_scroll, top, false);
    configure_scroll_for_list(state.inbox_scroll, state.inbox_list.window, added,
        state.inbox_visible_rows);
    redraw_window(state.inbox_list.window);
}

void PopulateMemoRecipientList(MemoWindowState& state, i32 tab) {
    state.current_recipient_tab = tab == 0 ? 0 : 1;
    if (state.current_recipient_tab == 0) {
        LoadLegacyImageButtonBitmaps(state.tab_background_button,
            kMemoFriendTabBitmapRecord, kMemoFriendTabBitmapRecord);
    } else {
        LoadLegacyImageButtonBitmaps(state.tab_background_button,
            kMemoGuildTabBitmapRecord, kMemoGuildTabBitmapRecord);
    }
    redraw_window(state.tab_background_button.window);

    if (state.recipient_list.window == nullptr) {
        return;
    }
    const LRESULT previous_top =
        SendMessageA(state.recipient_list.window, LB_GETTOPINDEX, 0, 0);
    SendMessageA(state.recipient_list.window, LB_RESETCONTENT, 0, 0);

    int added = 0;
    for (const auto& name : active_recipients(state)) {
        const LRESULT index = SendMessageA(state.recipient_list.window, LB_ADDSTRING,
            0, reinterpret_cast<LPARAM>(name.data()));
        if (index != LB_ERR) {
            ++added;
            SendMessageA(state.recipient_list.window, LB_SETITEMDATA,
                static_cast<WPARAM>(index), 0);
        }
    }

    int top = previous_top == LB_ERR ? 0 : static_cast<int>(previous_top);
    top = std::clamp(top, 0, std::max(0, added - state.recipient_visible_rows));
    SetLegacyCustomScrollControlValue(state.recipient_scroll, top, false);
    configure_scroll_for_list(state.recipient_scroll, state.recipient_list.window,
        added, state.recipient_visible_rows);
    redraw_window(state.recipient_list.window);
}

void DispatchMemoNetworkMessage(MemoWindowState& state, WPARAM wparam,
    LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        show_memo_message(state,
            startup_message_row(5, "Disconnected from the server."),
            RGB(10, 10, 250));
        if (state.callbacks.close_async_socket != nullptr) {
            state.callbacks.close_async_socket(state);
        }
        if (state.async_tcp_socket != nullptr) {
            CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
        }
        if (state.window != nullptr) {
            DestroyWindow(state.window);
        }
        return;
    }
    if (event != 1) {
        return;
    }

    auto handle_payload = [&](const u8* payload, i32 byte_count) -> bool {
        if (payload == nullptr ||
            byte_count < static_cast<i32>(kMemoPacketHeaderSize)) {
            return true;
        }
        const u32 opcode = read_u32(payload, byte_count, 4);
        if (opcode == 0x6e) {
            parse_memo_list_packet(state, payload, byte_count);
            return true;
        }
        if (opcode == 0x74) {
            if (byte_count > static_cast<i32>(kMemoPacketHeaderSize)) {
                set_text(state.read_edit.window,
                    reinterpret_cast<const char*>(payload + kMemoPacketHeaderSize));
            }
            return true;
        }
        forward_memo_network_message(state, wparam, lparam);
        return false;
    };

    if (state.callbacks.receive_payload != nullptr) {
        i32 byte_count = 0;
        const u8* payload = state.callbacks.receive_payload(state, byte_count);
        handle_payload(payload, byte_count);
        return;
    }
    if (state.async_tcp_socket == nullptr) {
        return;
    }

    ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= static_cast<i32>(kMemoPacketHeaderSize)) {
        const u32 packet_bytes = read_u32(payload, byte_count, 8);
        if (packet_bytes < kMemoPacketHeaderSize ||
            packet_bytes > static_cast<u32>(byte_count)) {
            break;
        }
        const auto packet_count = static_cast<i32>(packet_bytes);
        if (!handle_payload(payload, packet_count)) {
            return;
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket, packet_count);
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

LRESULT HandleMemoWindowMessage(MemoWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        if (state.parent_window != nullptr) {
            SetRankerMainWindowFrontendRouteWindow(state.parent_window);
        }
        release_window_resources(state);
        return 0;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            break;
        }
        if (draw->CtlID == kMemoInboxListId) {
            draw_list_item(state.inbox_list.window, *draw);
            return TRUE;
        }
        if (draw->CtlID == kMemoRecipientListId) {
            draw_list_item(state.recipient_list.window, *draw);
            return TRUE;
        }
        const std::array<LegacyImageButtonControl*, 7> buttons{{
            &state.tab_background_button,
            &state.friend_tab_button,
            &state.guild_tab_button,
            &state.delete_received_button,
            &state.delete_friend_button,
            &state.send_button,
            &state.close_button,
        }};
        for (LegacyImageButtonControl* button : buttons) {
            if (button != nullptr && button->window == draw->hwndItem) {
                DrawLegacyImageButtonItem(*button, *draw);
                break;
            }
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        switch (id) {
        case kMemoFriendTabButtonId:
            play_click_sound(state);
            PopulateMemoRecipientList(state, 0);
            break;
        case kMemoGuildTabButtonId:
            play_click_sound(state);
            PopulateMemoRecipientList(state, 1);
            break;
        case kMemoInboxListId:
            if (notify == LBN_SELCHANGE) {
                send_read_memo_request(state);
            }
            break;
        case kMemoDeleteReceivedButtonId:
            play_click_sound(state);
            delete_selected_received_memo(state);
            break;
        case kMemoDeleteFriendButtonId:
            play_click_sound(state);
            delete_selected_friend_recipient(state);
            break;
        case kMemoSendButtonId:
            play_click_sound(state);
            send_selected_memo(state);
            break;
        case kMemoCloseButtonId:
            play_click_sound(state);
            close_memo_window(state, hwnd);
            break;
        case kMemoForwardFocusCommandId: {
            HWND focus = GetFocus();
            if (focus != nullptr) {
                (void)GetWindowLongPtrA(focus, GWLP_ID);
            }
            play_click_sound(state);
            PopulateMemoRecipientList(state, 0);
            break;
        }
        default:
            break;
        }
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    }
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kMemoSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kMemoBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kMemoSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kMemoBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kMemoYellow);
        SetBkColor(reinterpret_cast<HDC>(wparam), kMemoBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kMemoWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kMemoBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case kMemoNetworkMessage:
        DispatchMemoNetworkMessage(state, wparam, lparam);
        break;
    case kMemoReconnectMessage:
        close_memo_window(state, hwnd);
        break;
    case kMemoPromptMessage0:
        ShowOnlineModalPrompt0(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kMemoPromptMessage1:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kMemoPromptMessage2:
        ShowOnlineModalPrompt2(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kMemoPromptMessage3:
        ShowOnlineModalPrompt3(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kMemoPromptEndMessage:
        EndOnlineModalPrompt(online_modal_prompt_state(),
            static_cast<INT_PTR>(wparam));
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleMemoControlMessage(MemoWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    if (message == WM_PAINT && hwnd == state.inbox_scroll.window) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.inbox_scroll, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_PAINT && hwnd == state.recipient_scroll.window) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.recipient_scroll, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (id == kMemoInboxScrollId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.inbox_scroll, message, wparam, lparam);
        if (changed && state.inbox_list.window != nullptr) {
            SendMessageA(state.inbox_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(
                    GetLegacyCustomScrollControlValue(state.inbox_scroll)),
                0);
        }
    } else if (id == kMemoRecipientScrollId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.recipient_scroll, message, wparam, lparam);
        if (changed && state.recipient_list.window != nullptr) {
            SendMessageA(state.recipient_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(
                    GetLegacyCustomScrollControlValue(state.recipient_scroll)),
                0);
        }
    }

    if (id >= kMemoFriendTabButtonId && id <= kMemoCloseButtonId) {
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    }
    return 0;
}

} // namespace ranker

#endif
