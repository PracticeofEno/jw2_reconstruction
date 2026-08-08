#include "ranker_barter_window.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_indexed_text_table.h"
#include "ranker_icon_strips.h"
#include "ranker_gameplay_sound.h"
#include "ranker_miles.h"
#include "ranker_online_dialogs.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed = 0x10cf0000;
constexpr DWORD kReadOnlyEditStyle = WS_CHILD | WS_VISIBLE | WS_DISABLED;
constexpr DWORD kEditStyle = WS_CHILD | WS_VISIBLE | ES_NUMBER;
constexpr COLORREF kBarterWhite = RGB(255, 255, 255);
constexpr COLORREF kBarterSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kBarterYellow = RGB(255, 255, 0);
constexpr COLORREF kBarterGray = RGB(200, 200, 200);
constexpr COLORREF kBarterBlack = RGB(0, 0, 0);
constexpr int kBarterFrameWidth = 0x26;
constexpr int kBarterFrameHeight = 0x26;
constexpr int kBarterFrameCount = 192;
constexpr int kItemDefinitionRecordBytes = 0x28c;
constexpr UINT_PTR kBarterOfferTimerId = 1;
constexpr int kBarterForwardFocusCommandId = 0x9c41;

BarterWindowState g_barter_window_state;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK barter_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleBarterWindowMessage(g_barter_window_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK barter_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleBarterControlMessage(g_barter_window_state, hwnd, message,
        wparam, lparam);
}

std::vector<BarterLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<BarterLayoutRect> rects;
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

BarterLayoutRect layout_at(const BarterWindowState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return BarterLayoutRect{};
}

i32 read_le_i32(const u8* p) {
    i32 value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
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

i32 read_i32(const u8* payload, i32 byte_count, std::size_t offset) {
    return WrappedU32ToI32(read_u32(payload, byte_count, offset));
}

void write_le32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset + sizeof(value) > buffer.size()) {
        return;
    }
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}

std::string fixed_string(const u8* data, std::size_t byte_count) {
    const char* begin = reinterpret_cast<const char*>(data);
    const char* end = begin + byte_count;
    const char* nul = std::find(begin, end, '\0');
    return std::string(begin, nul);
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& target, const char* source) {
    target.fill(0);
    if (source != nullptr) {
        std::strncpy(target.data(), source, target.size() - 1);
    }
}

template <std::size_t N>
void copy_payload_string(std::array<char, N>& target, const void* source,
    std::size_t source_size, std::size_t offset) {
    target.fill(0);
    if (source == nullptr || offset >= source_size) {
        return;
    }
    const auto* bytes = static_cast<const char*>(source);
    const std::size_t copy_size =
        std::min<std::size_t>(target.size() - 1, source_size - offset);
    std::memcpy(target.data(), bytes + offset, copy_size);
    target[target.size() - 1] = '\0';
}

void set_text(HWND window, const char* text) {
    if (window != nullptr) {
        SetWindowTextA(window, text == nullptr ? "" : text);
    }
}

void set_text_int(HWND window, int value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%d", value);
    set_text(window, text);
}

void redraw_window(HWND window) {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

void subclass_window(HWND window) {
    if (window != nullptr) {
        SetWindowLongPtrA(window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(barter_control_proc));
    }
}

void subclass_text_control(BarterTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    subclass_window(control.window);
}

bool create_text_control(BarterTextControl& control, HWND parent, HINSTANCE instance,
    DWORD style, int id, const BarterLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, "edit", nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        return false;
    }
    subclass_text_control(control);
    return true;
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const BarterLayoutRect& rect, u32 normal_record,
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

bool load_item_strip(BarterWindowState& state) {
    return LoadItemIconStrip(state.item_strip);
}

const BarterItemDefinition* find_item_definition(
    const BarterWindowState& state, int item_id) {
    if (item_id < 0) {
        return nullptr;
    }
    const auto it = std::find_if(state.item_definitions.begin(),
        state.item_definitions.end(), [item_id](const BarterItemDefinition& item) {
            return item.item_id == item_id;
        });
    return it == state.item_definitions.end() ? nullptr : &*it;
}

std::string startup_item_text(u32 slot, int item_id) {
    if (item_id < 0) {
        return {};
    }
    const std::string_view text = GetIndexedTextTableRow(
        StartupAuxiliaryIndexedTextTable(slot), static_cast<u32>(item_id));
    return std::string(text.begin(), text.end());
}

std::string item_name(const BarterWindowState& state, int item_id) {
    const BarterItemDefinition* item = find_item_definition(state, item_id);
    if (item != nullptr && !item->name.empty()) {
        return item->name;
    }
    if (std::string text = startup_item_text(5, item_id); !text.empty()) {
        return text;
    }
    char fallback[32]{};
    std::snprintf(fallback, sizeof(fallback), "Item %d", item_id);
    return fallback;
}

int item_icon_frame(const BarterWindowState& state, int item_id) {
    const BarterItemDefinition* item = find_item_definition(state, item_id);
    if (item == nullptr) {
        return std::max(0, item_id);
    }
    return std::max(0, item->icon_frame);
}

bool inventory_slot_is_offered(const BarterWindowState& state, int slot) {
    return IsBarterInventorySlotAlreadyOffered(state, slot);
}

int item_for_inventory_slot(const BarterWindowState& state, int slot) {
    if (slot < 0 || slot >= kBarterInventorySlotCount) {
        return 0;
    }
    return state.inventory[static_cast<std::size_t>(slot)];
}

int item_for_local_offer_slot(const BarterWindowState& state, int slot) {
    if (slot < 0 || slot >= kBarterLocalOfferSlotCount) {
        return 0;
    }
    const int inventory_slot =
        state.local_offer_slots[static_cast<std::size_t>(slot)];
    return item_for_inventory_slot(state, inventory_slot);
}

void draw_slot_background(BarterWindowState& state, HDC dc, bool selected) {
    StretchBitmapMemoryResourceToDc(
        selected ? state.selected_slot_bitmap : state.normal_slot_bitmap, dc, 0, 0);
}

void draw_item_frame(BarterWindowState& state, HDC dc, int item_id) {
    if (item_id <= 0) {
        return;
    }
    const u32 frame = static_cast<u32>(item_icon_frame(state, item_id));
    if (frame < kBarterFrameCount) {
        DrawSecondaryRawIndexedBitmapStripFrame(state.item_strip, dc, 1, 1, frame);
    }
}

void set_current_item(BarterWindowState& state, int item_id) {
    SetBarterCurrentItem(state, item_id);
}

void draw_inventory_slot(BarterWindowState& state, const DRAWITEMSTRUCT& draw,
    int slot) {
    draw_slot_background(state, draw.hDC, slot == state.selected_inventory_slot);
    const int item_id = item_for_inventory_slot(state, slot);
    if (item_id > 0 && !inventory_slot_is_offered(state, slot)) {
        draw_item_frame(state, draw.hDC, item_id);
    }
}

void draw_offer_slot(BarterWindowState& state, const DRAWITEMSTRUCT& draw,
    int slot) {
    const bool local_slot = slot < kBarterLocalOfferSlotCount;
    const int local_index = slot;
    const int remote_index = slot - kBarterLocalOfferSlotCount;
    draw_slot_background(state, draw.hDC,
        local_slot && local_index == state.selected_local_offer_slot);
    const int item_id = local_slot ? item_for_local_offer_slot(state, local_index) :
        state.remote_offer_items[static_cast<std::size_t>(remote_index)];
    draw_item_frame(state, draw.hDC, item_id);
}

void draw_item_info_panel(BarterWindowState& state, const DRAWITEMSTRUCT& draw) {
    BitBlt(draw.hDC, 0, 0, draw.rcItem.right - draw.rcItem.left,
        draw.rcItem.bottom - draw.rcItem.top, nullptr, 0, 0, BLACKNESS);
    const BarterItemDefinition* item =
        find_item_definition(state, state.current_item_id);
    if (item == nullptr) {
        return;
    }

    RECT rect = draw.rcItem;
    rect.left = 2;
    rect.top = 2;
    rect.right -= 2;
    SetTextColor(draw.hDC, kBarterGray);
    SetBkMode(draw.hDC, TRANSPARENT);

    char text[256]{};
    std::snprintf(text, sizeof(text), startup_message_row(248, "NAME : %s"),
        item_name(state, item->item_id).c_str());
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), startup_message_row(249, "DESC : %s"),
        item->detail_text.c_str());
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
}

void draw_friend_info_panel(BarterWindowState& state, const DRAWITEMSTRUCT& draw) {
    BitBlt(draw.hDC, 0, 0, draw.rcItem.right - draw.rcItem.left,
        draw.rcItem.bottom - draw.rcItem.top, nullptr, 0, 0, BLACKNESS);
    RECT rect = draw.rcItem;
    rect.left = 2;
    rect.top = 2;
    rect.right -= 2;
    SetTextColor(draw.hDC, kBarterGray);
    SetBkMode(draw.hDC, TRANSPARENT);

    char text[128]{};
    std::snprintf(text, sizeof(text), startup_message_row(250, "ID : %s"),
        state.remote_name.data());
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), startup_message_row(251, "GUILD : %s"),
        state.remote_guild.data());
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
}

WNDPROC original_proc_for_id(BarterWindowState& state, int id) {
    switch (id) {
    case kBarterLocalNameEditId:
        return state.local_name_edit.original_window_proc;
    case kBarterLocalJemEditId:
        return state.local_jem_edit.original_window_proc;
    case kBarterLocalOfferJemEditId:
        return state.local_offer_jem_edit.original_window_proc;
    case kBarterRemoteOfferJemEditId:
        return state.remote_offer_jem_edit.original_window_proc;
    default:
        break;
    }
    if (id >= kBarterInventoryFirstButtonId &&
        id < kBarterInventoryFirstButtonId + kBarterInventorySlotCount) {
        return state.inventory_slots[
            static_cast<std::size_t>(id - kBarterInventoryFirstButtonId)]
            .original_window_proc;
    }
    if (id >= kBarterOfferFirstButtonId &&
        id < kBarterOfferFirstButtonId + static_cast<int>(state.offer_slots.size())) {
        return state.offer_slots[
            static_cast<std::size_t>(id - kBarterOfferFirstButtonId)]
            .original_window_proc;
    }
    const std::array<std::pair<int, LegacyImageButtonControl*>, 8> buttons{{
        {kBarterInsertButtonId, &state.insert_button},
        {kBarterDeleteButtonId, &state.delete_button},
        {kBarterOkButtonId, &state.ok_button},
        {kBarterCancelButtonId, &state.cancel_button},
        {kBarterReadyIndicatorButtonId, &state.ready_indicator_button},
        {kBarterCloseButtonId, &state.close_button},
        {kBarterItemInfoPanelId, &state.item_info_panel},
        {kBarterFriendInfoPanelId, &state.friend_info_panel},
    }};
    for (const auto& entry : buttons) {
        if (entry.first == id) {
            return entry.second->original_window_proc;
        }
    }
    return nullptr;
}

void play_click_sound(BarterWindowState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

void show_barter_message(BarterWindowState& state, const char* text,
    COLORREF color = kBarterSoftWhite) {
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(state.window, text, color, state.callbacks.user_data);
    } else {
        CreateOnlineModelessPrompt(online_modeless_prompt_state(), state.window,
            state.instance, text == nullptr ? "" : text, color, false, 0, 0);
    }
}

void forward_barter_network_message(BarterWindowState& state, WPARAM wparam,
    LPARAM lparam) {
    if (state.callbacks.forward_network_message != nullptr) {
        state.callbacks.forward_network_message(state, wparam, lparam);
        return;
    }
    if (state.parent_window != nullptr && IsWindow(state.parent_window)) {
        PostMessageA(state.parent_window, kBarterNetworkMessage, wparam, lparam);
    }
}

void queue_packet(BarterWindowState& state, std::vector<u8>& packet) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet.data(),
            static_cast<i32>(packet.size()));
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket, packet.data(),
            static_cast<i32>(packet.size()));
    }
}

int ReadBarterLocalOfferJemEdit(BarterWindowState& state) {
    char text[32]{};
    if (state.local_offer_jem_edit.window != nullptr) {
        GetWindowTextA(state.local_offer_jem_edit.window, text,
            static_cast<int>(sizeof(text)));
    }
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text || value < 0) {
        value = 0;
    }
    if (value > state.local_jem) {
        value = state.local_jem;
    }
    return static_cast<int>(value);
}

void update_text_fields(BarterWindowState& state) {
    set_text(state.local_name_edit.window, state.local_name.data());
    set_text_int(state.local_jem_edit.window, state.local_jem);
    set_text_int(state.local_offer_jem_edit.window, state.local_offer_jem);
    set_text_int(state.remote_offer_jem_edit.window, state.remote_offer_jem);
}

void redraw_slots(BarterWindowState& state) {
    for (auto& slot : state.inventory_slots) {
        redraw_window(slot.window);
    }
    for (auto& slot : state.offer_slots) {
        redraw_window(slot.window);
    }
    redraw_window(state.item_info_panel.window);
    redraw_window(state.friend_info_panel.window);
}

void refresh_ready_indicator(BarterWindowState& state) {
    if (state.remote_ready) {
        LoadLegacyImageButtonBitmaps(state.ready_indicator_button,
            kBarterReadyNormalBitmapRecord, kBarterReadyNormalBitmapRecord);
    } else {
        LoadLegacyImageButtonBitmaps(state.ready_indicator_button,
            kBarterReadyPressedBitmapRecord, kBarterReadyPressedBitmapRecord);
    }
    redraw_window(state.ready_indicator_button.window);
}

void QueueBarterOfferUpdate(BarterWindowState& state) {
    const int offer_jem = ReadBarterLocalOfferJemEdit(state);
    if (offer_jem < 0 || offer_jem > state.local_jem) {
        show_barter_message(state,
            startup_message_row(203, "The barter JEM value is invalid."));
        set_text_int(state.local_offer_jem_edit.window, state.local_offer_jem);
        return;
    }

    state.local_offer_jem = offer_jem;
    std::vector<u8> packet(0x21, 0);
    write_le32(packet, 0x00, 3);
    write_le32(packet, 0x04, 0x65);
    write_le32(packet, 0x08, 0x21);
    for (int i = 0; i < kBarterLocalOfferSlotCount; ++i) {
        write_le32(packet, 0x0d + static_cast<std::size_t>(i) * 4,
            static_cast<u32>(state.local_offer_slots[static_cast<std::size_t>(i)]));
    }
    write_le32(packet, 0x1d, static_cast<u32>(state.local_offer_jem));
    state.remote_ready = false;
    refresh_ready_indicator(state);
    queue_packet(state, packet);
}

void send_accept_packet(BarterWindowState& state) {
    std::vector<u8> packet(0x11, 0);
    write_le32(packet, 0x00, 3);
    write_le32(packet, 0x04, 0x67);
    write_le32(packet, 0x08, 0x11);
    write_le32(packet, 0x0d, 1);
    queue_packet(state, packet);
}

void send_close_packet(BarterWindowState& state) {
    std::vector<u8> packet(0x11, 0);
    write_le32(packet, 0x00, 3);
    write_le32(packet, 0x04, 0x6b);
    write_le32(packet, 0x08, 0x11);
    write_le32(packet, 0x0d, 2);
    queue_packet(state, packet);
}

void insert_selected_inventory_item(BarterWindowState& state) {
    const int slot = state.selected_inventory_slot;
    if (item_for_inventory_slot(state, slot) <= 0 ||
        inventory_slot_is_offered(state, slot)) {
        return;
    }
    auto it = std::find(state.local_offer_slots.begin(),
        state.local_offer_slots.end(), -1);
    if (it == state.local_offer_slots.end()) {
        return;
    }
    *it = slot;
    SelectBarterInventorySlot(state, slot);
    SelectBarterOfferSlot(state,
        static_cast<i32>(std::distance(state.local_offer_slots.begin(), it)));
    QueueBarterOfferUpdate(state);
}

void delete_selected_offer_item(BarterWindowState& state) {
    if (state.selected_local_offer_slot < 0 ||
        state.selected_local_offer_slot >= kBarterLocalOfferSlotCount) {
        return;
    }
    state.local_offer_slots[
        static_cast<std::size_t>(state.selected_local_offer_slot)] = -1;
    QueueBarterOfferUpdate(state);
    redraw_slots(state);
}

void close_barter_window(BarterWindowState& state, HWND hwnd) {
    HWND parent = state.parent_window;
    if (hwnd != nullptr) {
        DestroyWindow(hwnd);
    }
    if (state.callbacks.return_to_parent != nullptr) {
        state.callbacks.return_to_parent(state);
    } else if (parent != nullptr && IsWindow(parent)) {
        ShowWindow(parent, SW_SHOW);
        SetForegroundWindow(parent);
        SetFocus(parent);
    }
}

bool paint_background_if_current(BarterWindowState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToClient(state.background, dc, state.window);
    EndPaint(hwnd, &paint);
    return true;
}

bool create_barter_controls(BarterWindowState& state, HINSTANCE instance) {
    if (!create_text_control(state.local_name_edit, state.window, instance,
            kReadOnlyEditStyle, kBarterLocalNameEditId, layout_at(state, 1)) ||
        !create_text_control(state.local_jem_edit, state.window, instance,
            kReadOnlyEditStyle, kBarterLocalJemEditId, layout_at(state, 2)) ||
        !create_text_control(state.local_offer_jem_edit, state.window, instance,
            kEditStyle, kBarterLocalOfferJemEditId, layout_at(state, 22)) ||
        !create_text_control(state.remote_offer_jem_edit, state.window, instance,
            kReadOnlyEditStyle, kBarterRemoteOfferJemEditId, layout_at(state, 23))) {
        return false;
    }

    if (!create_image_button(state.item_info_panel, state.window, "iteminfo",
            kBarterItemInfoPanelId, layout_at(state, 28), 0, 0) ||
        !create_image_button(state.friend_info_panel, state.window, "iteminfo",
            kBarterFriendInfoPanelId, layout_at(state, 29), 0, 0)) {
        return false;
    }

    for (int i = 0; i < kBarterInventorySlotCount; ++i) {
        if (!create_image_button(state.inventory_slots[static_cast<std::size_t>(i)],
                state.window, "inventory", kBarterInventoryFirstButtonId + i,
                layout_at(state, 3 + static_cast<std::size_t>(i)), 0, 0)) {
            return false;
        }
    }
    for (int i = 0; i < static_cast<int>(state.offer_slots.size()); ++i) {
        if (!create_image_button(state.offer_slots[static_cast<std::size_t>(i)],
                state.window, "barter", kBarterOfferFirstButtonId + i,
                layout_at(state, 12 + static_cast<std::size_t>(i)), 0, 0)) {
            return false;
        }
    }

    SendMessageA(state.local_offer_jem_edit.window, EM_LIMITTEXT, 10, 0);

    return create_image_button(state.insert_button, state.window, "insert",
            kBarterInsertButtonId, layout_at(state, 20),
            kBarterInsertNormalBitmapRecord, kBarterInsertPressedBitmapRecord) &&
        create_image_button(state.delete_button, state.window, "delete",
            kBarterDeleteButtonId, layout_at(state, 21),
            kBarterDeleteNormalBitmapRecord, kBarterDeletePressedBitmapRecord) &&
        create_image_button(state.ok_button, state.window, "",
            kBarterOkButtonId, layout_at(state, 24),
            kBarterOkNormalBitmapRecord, kBarterOkPressedBitmapRecord) &&
        create_image_button(state.cancel_button, state.window, "cancel",
            kBarterCancelButtonId, layout_at(state, 25),
            kBarterCancelNormalBitmapRecord, kBarterCancelPressedBitmapRecord) &&
        create_image_button(state.ready_indicator_button, state.window, "readyok",
            kBarterReadyIndicatorButtonId, layout_at(state, 26),
            kBarterReadyPressedBitmapRecord, kBarterReadyPressedBitmapRecord) &&
        create_image_button(state.close_button, state.window, "close",
            kBarterCloseButtonId, layout_at(state, 27),
            kBarterCloseNormalBitmapRecord, kBarterClosePressedBitmapRecord);
}

void destroy_text_control(BarterTextControl& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    control.window = nullptr;
    control.original_window_proc = nullptr;
}

void release_window_resources(BarterWindowState& state) {
    RestoreBarterWindowAccelerators(state);
    if (state.timer_id != 0 && state.window != nullptr) {
        KillTimer(state.window, state.timer_id);
    }
    state.timer_id = 0;
    destroy_text_control(state.local_name_edit);
    destroy_text_control(state.local_jem_edit);
    destroy_text_control(state.local_offer_jem_edit);
    destroy_text_control(state.remote_offer_jem_edit);
    for (auto& slot : state.inventory_slots) {
        DestroyLegacyImageButtonControl(slot);
    }
    for (auto& slot : state.offer_slots) {
        DestroyLegacyImageButtonControl(slot);
    }
    DestroyLegacyImageButtonControl(state.insert_button);
    DestroyLegacyImageButtonControl(state.delete_button);
    DestroyLegacyImageButtonControl(state.ok_button);
    DestroyLegacyImageButtonControl(state.cancel_button);
    DestroyLegacyImageButtonControl(state.ready_indicator_button);
    DestroyLegacyImageButtonControl(state.close_button);
    DestroyLegacyImageButtonControl(state.item_info_panel);
    DestroyLegacyImageButtonControl(state.friend_info_panel);
    ReleaseBitmapMemoryResource(state.background);
    ReleaseBitmapMemoryResource(state.selected_slot_bitmap);
    ReleaseBitmapMemoryResource(state.normal_slot_bitmap);
    ReleaseSecondaryRawIndexedBitmapStrip(state.item_strip);
    state.window = nullptr;
}

} // namespace

BarterWindowState& barter_window_state() {
    return g_barter_window_state;
}

void BarterStaticResourceWrapperNN() {
    InitializeBarterWindowResources(g_barter_window_state);
}

#define DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(Name) \
    void Name() { BarterStaticResourceWrapperNN(); }
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper00)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper01)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper02)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper03)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper04)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper05)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper06)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper07)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper08)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper09)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper10)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper11)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper12)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper13)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper14)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper15)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper16)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper17)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper18)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper19)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper20)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper21)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper22)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper23)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper24)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper25)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper26)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper27)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper28)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper29)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper30)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper31)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper32)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper33)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper34)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper35)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper36)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper37)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper38)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper39)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper40)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper41)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper42)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper43)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper44)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper45)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper46)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper47)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper48)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper49)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper50)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper51)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper52)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper53)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper54)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper55)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper56)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper57)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper58)
DEFINE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper59)
#undef DEFINE_BARTER_STATIC_RESOURCE_WRAPPER

void SetBarterCurrentItem(BarterWindowState& state, i32 item_id) {
    if (item_id <= 0 || item_id >= kBarterFrameCount) {
        return;
    }
    state.current_item_id = item_id;
    redraw_window(state.item_info_panel.window);
}

bool IsBarterInventorySlotAlreadyOffered(const BarterWindowState& state,
    i32 slot) {
    return std::find(state.local_offer_slots.begin(), state.local_offer_slots.end(),
        slot) != state.local_offer_slots.end();
}

void InitializeBarterWindowResources(BarterWindowState& state) {
    InitializeBitmapMemoryResource(state.background);
    InitializeBitmapMemoryResource(state.selected_slot_bitmap);
    InitializeBitmapMemoryResource(state.normal_slot_bitmap);
    InitializeRawIndexedBitmapStrip(state.item_strip);
    InitializeLegacyImageButtonControl(state.insert_button);
    InitializeLegacyImageButtonControl(state.delete_button);
    InitializeLegacyImageButtonControl(state.ok_button);
    InitializeLegacyImageButtonControl(state.cancel_button);
    InitializeLegacyImageButtonControl(state.ready_indicator_button);
    InitializeLegacyImageButtonControl(state.close_button);
    InitializeLegacyImageButtonControl(state.item_info_panel);
    InitializeLegacyImageButtonControl(state.friend_info_panel);
    for (auto& slot : state.inventory_slots) {
        InitializeLegacyImageButtonControl(slot);
    }
    for (auto& slot : state.offer_slots) {
        InitializeLegacyImageButtonControl(slot);
    }
}

void ReleaseBarterWindowResources(BarterWindowState& state) {
    release_window_resources(state);
}

void InstallBarterWindowAccelerators(BarterWindowState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kBarterAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreBarterWindowAccelerators(BarterWindowState& state) {
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

bool LoadBarterItemDefinitionsFromJw210Trc(BarterWindowState& state,
    const char* archive_name, u32 record_index) {
    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    std::array<u8, 8> header{};
    if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size())) {
        CloseTrcRecordReader(reader);
        return false;
    }
    const u32 version = read_u32(header.data(), static_cast<i32>(header.size()), 0);
    const u32 count = read_u32(header.data(), static_cast<i32>(header.size()), 4);
    if (version != 0x65 || count >= 0x97) {
        CloseTrcRecordReader(reader);
        return false;
    }

    ServeMilesSound();
    std::vector<u8> records(static_cast<std::size_t>(count) *
        kItemDefinitionRecordBytes);
    if (!ReadOpenTrcRecordBytes(reader, records.data(), records.size())) {
        CloseTrcRecordReader(reader);
        return false;
    }
    CloseTrcRecordReader(reader);

    state.item_definitions.clear();
    state.item_definitions.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const u8* record = records.data() +
            static_cast<std::size_t>(i) * kItemDefinitionRecordBytes;
        BarterItemDefinition item{};
        item.item_id = static_cast<i32>(i);
        item.name = fixed_string(record, 0x40);
        item.detail_text = fixed_string(record + 0x40, 0x40);
        if (std::string text = startup_item_text(5, item.item_id); !text.empty()) {
            item.name = std::move(text);
        }
        if (std::string text = startup_item_text(6, item.item_id); !text.empty()) {
            item.detail_text = std::move(text);
        }
        item.category = read_le_i32(record + 0x84);
        item.icon_frame = read_le_i32(record + 0x88);
        item.tooltip_primary_cost = read_le_i32(record + 0xcc);
        item.tooltip_secondary_cost = read_le_i32(record + 0xd4);
        item.fallback_price = read_le_i32(record + 0x20c);
        item.hp = read_le_i32(record + 0x218);
        item.mp = read_le_i32(record + 0x21c);
        item.op = read_le_i32(record + 0x220);
        item.dp = read_le_i32(record + 0x224);
        state.item_definitions.push_back(std::move(item));
    }
    return true;
}

bool CreateBarterWindow(BarterWindowState& state, HWND parent, HINSTANCE instance,
    const char* local_name, const void* remote_summary, std::size_t remote_summary_size,
    LegacyAsyncTcpSocket* async_tcp_socket) {
    if (state.window != nullptr) {
        return false;
    }

    InitializeBarterWindowResources(state);
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.async_tcp_socket = async_tcp_socket;
    load_item_strip(state);

    state.layout.clear();
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kBarterLayoutTrcRecord)) {
        release_window_resources(state);
        return false;
    }
    state.layout = copy_layout_record(layout.table);
    state.inventory.fill(0);
    state.local_offer_slots.fill(-1);
    state.remote_offer_items.fill(-1);
    state.local_jem = 0;
    state.local_offer_jem = 0;
    state.remote_offer_jem = 0;
    state.selected_inventory_slot = 0;
    state.selected_local_offer_slot = 0;
    state.current_item_id = -1;
    state.remote_ready = false;
    state.resend_offer_on_timer = false;
    copy_c_string(state.local_name, local_name);
    copy_payload_string(state.remote_name, remote_summary, remote_summary_size, 0);
    copy_payload_string(state.remote_guild, remote_summary, remote_summary_size, 0x20);
    LoadBarterItemDefinitionsFromJw210Trc(state);

    const DWORD style =
        IsWindow(parent) && GetWindowLongPtrA(parent, GWL_STYLE) != 0 ?
        kWindowStyleWindowed : kWindowStyleFullscreen;
    const BarterLayoutRect window_rect = layout_at(state, 0);
    const POINT origin =
        RankerCenteredFrontendWindowOrigin(window_rect.width, window_rect.height);
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Barter", "Barter",
        style, origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        release_window_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(barter_window_proc));

    if (!create_barter_controls(state, instance)) {
        DestroyWindow(state.window);
        return false;
    }

    SendMessageA(state.local_name_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.local_jem_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.local_offer_jem_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.remote_offer_jem_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    InstallBarterWindowAccelerators(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kBarterBackgroundBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.selected_slot_bitmap, "Jw2_19.trc",
        kBarterSelectedSlotBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.normal_slot_bitmap, "Jw2_19.trc",
        kBarterNormalSlotBitmapRecord);
    update_text_fields(state);
    state.timer_id = SetTimer(state.window, kBarterOfferTimerId, 1000, nullptr);
    return true;
}

void ResetBarterSelection(BarterWindowState& state) {
    state.local_offer_slots.fill(-1);
    state.remote_offer_items.fill(-1);
    state.selected_inventory_slot = 0;
    state.selected_local_offer_slot = 0;
    state.local_offer_jem = 0;
    state.remote_offer_jem = 0;
    state.remote_ready = false;
    state.current_item_id = item_for_inventory_slot(state, 0);
    update_text_fields(state);
    refresh_ready_indicator(state);
    redraw_slots(state);
}

void SelectBarterInventorySlot(BarterWindowState& state, i32 slot) {
    if (slot < 0 || slot >= kBarterInventorySlotCount) {
        return;
    }
    state.selected_inventory_slot = slot;
    set_current_item(state, item_for_inventory_slot(state, slot));
    for (auto& control : state.inventory_slots) {
        redraw_window(control.window);
    }
}

void SelectBarterOfferSlot(BarterWindowState& state, i32 slot) {
    if (slot < 0 ||
        slot >= kBarterLocalOfferSlotCount + kBarterRemoteOfferSlotCount) {
        return;
    }
    if (slot < kBarterLocalOfferSlotCount) {
        state.selected_local_offer_slot = slot;
        set_current_item(state, item_for_local_offer_slot(state, slot));
    } else {
        set_current_item(state, state.remote_offer_items[
            static_cast<std::size_t>(slot - kBarterLocalOfferSlotCount)]);
    }
    for (auto& control : state.offer_slots) {
        redraw_window(control.window);
    }
}

void DispatchBarterNetworkMessage(BarterWindowState& state, WPARAM wparam,
    LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        show_barter_message(state,
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
        if (payload == nullptr || byte_count < 0x0d) {
            return true;
        }
        const u32 opcode = read_u32(payload, byte_count, 4);
        switch (opcode) {
        case 0x5e:
            state.local_jem = read_i32(payload, byte_count, 0x11);
            ResetBarterSelection(state);
            break;
        case 0x66:
            for (int i = 0; i < kBarterRemoteOfferSlotCount; ++i) {
                state.remote_offer_items[static_cast<std::size_t>(i)] =
                    read_i32(payload, byte_count,
                        0x0d + static_cast<std::size_t>(i) * 4);
            }
            state.remote_offer_jem = read_i32(payload, byte_count, 0x1d);
            set_text_int(state.remote_offer_jem_edit.window, state.remote_offer_jem);
            state.remote_ready = false;
            refresh_ready_indicator(state);
            redraw_slots(state);
            break;
        case 0x67:
            state.remote_ready = read_i32(payload, byte_count, 0x0d) != 0;
            refresh_ready_indicator(state);
            break;
        case 0x68:
            for (int i = 0; i < kBarterInventorySlotCount; ++i) {
                state.inventory[static_cast<std::size_t>(i)] =
                    read_i32(payload, byte_count,
                        0x0d + static_cast<std::size_t>(i) * 4);
            }
            state.local_jem = read_i32(payload, byte_count, 0x31);
            ResetBarterSelection(state);
            break;
        case 0x6a: {
            const int result = read_i32(payload, byte_count, 0x0d);
            if (result == 1) {
                show_barter_message(state,
                    startup_message_row(205, "The barter was rejected."));
                close_barter_window(state, state.window);
            } else if (result == 2) {
                show_barter_message(state,
                    startup_message_row(206, "The requested user was not found."));
                close_barter_window(state, state.window);
            } else if (result == 3) {
                show_barter_message(state,
                    startup_message_row(207, "The barter user has left."));
                close_barter_window(state, state.window);
            } else if (result == 4) {
                char text[128]{};
                std::snprintf(text, sizeof(text),
                    startup_message_row(208,
                        "%s does not have enough inventory space."),
                    state.remote_name.data());
                show_barter_message(state, text);
            } else if (result == 5) {
                char text[128]{};
                std::snprintf(text, sizeof(text),
                    startup_message_row(208,
                        "%s does not have enough inventory space."),
                    state.local_name.data());
                show_barter_message(state, text);
            }
            break;
        }
        default:
            forward_barter_network_message(state, wparam, lparam);
            return false;
        }
        return true;
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
    while (payload != nullptr && byte_count >= 0x0d) {
        const u32 packet_bytes = read_u32(payload, byte_count, 8);
        if (packet_bytes < 0x0d ||
            packet_bytes > static_cast<u32>(byte_count)) {
            break;
        }
        const auto packet_count = static_cast<i32>(packet_bytes);
        if (!handle_payload(payload, packet_count)) {
            return;
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket, packet_count);
        if (state.window == nullptr) {
            return;
        }
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

LRESULT HandleBarterWindowMessage(BarterWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        release_window_resources(state);
        return 0;
    case WM_TIMER:
        if (wparam == kBarterOfferTimerId && state.resend_offer_on_timer) {
            state.resend_offer_on_timer = false;
            QueueBarterOfferUpdate(state);
        }
        break;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            break;
        }
        if (draw->CtlID >= kBarterInventoryFirstButtonId &&
            draw->CtlID < kBarterInventoryFirstButtonId + kBarterInventorySlotCount) {
            draw_inventory_slot(state, *draw,
                static_cast<int>(draw->CtlID - kBarterInventoryFirstButtonId));
            break;
        }
        if (draw->CtlID >= kBarterOfferFirstButtonId &&
            draw->CtlID < kBarterOfferFirstButtonId +
                static_cast<int>(state.offer_slots.size())) {
            draw_offer_slot(state, *draw,
                static_cast<int>(draw->CtlID - kBarterOfferFirstButtonId));
            break;
        }
        if (draw->CtlID == kBarterItemInfoPanelId) {
            draw_item_info_panel(state, *draw);
            break;
        }
        if (draw->CtlID == kBarterFriendInfoPanelId) {
            draw_friend_info_panel(state, *draw);
            break;
        }
        const std::array<LegacyImageButtonControl*, 6> buttons{{
            &state.insert_button, &state.delete_button, &state.ok_button,
            &state.cancel_button, &state.ready_indicator_button, &state.close_button,
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
        if (id == kBarterLocalOfferJemEditId && notify == EN_CHANGE) {
            state.resend_offer_on_timer = true;
        }
        else if (id >= kBarterInventoryFirstButtonId &&
            id < kBarterInventoryFirstButtonId + kBarterInventorySlotCount) {
            SelectBarterInventorySlot(state, id - kBarterInventoryFirstButtonId);
        }
        else if (id >= kBarterOfferFirstButtonId &&
            id < kBarterOfferFirstButtonId +
                static_cast<int>(state.offer_slots.size())) {
            SelectBarterOfferSlot(state, id - kBarterOfferFirstButtonId);
        }
        else {
            switch (id) {
            case kBarterInsertButtonId:
                play_click_sound(state);
                insert_selected_inventory_item(state);
                break;
            case kBarterDeleteButtonId:
                play_click_sound(state);
                delete_selected_offer_item(state);
                break;
            case kBarterOkButtonId:
                play_click_sound(state);
                send_accept_packet(state);
                break;
            case kBarterCancelButtonId:
                play_click_sound(state);
                QueueBarterOfferUpdate(state);
                break;
            case kBarterCloseButtonId:
                play_click_sound(state);
                send_close_packet(state);
                close_barter_window(state, hwnd);
                break;
            case kBarterForwardFocusCommandId: {
                HWND focus = GetFocus();
                if (focus != nullptr &&
                    GetWindowLongPtrA(focus, GWLP_ID) == kBarterLocalNameEditId) {
                    SetFocus(state.local_jem_edit.window);
                    return 0;
                }
                break;
            }
            default:
                break;
            }
        }
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    }
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kBarterSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kBarterBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kBarterSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kBarterBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kBarterYellow);
        SetBkColor(reinterpret_cast<HDC>(wparam), kBarterBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kBarterWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kBarterBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case kBarterNetworkMessage:
        DispatchBarterNetworkMessage(state, wparam, lparam);
        break;
    case kBarterPromptMessage0:
        ShowOnlineModalPrompt0(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kBarterPromptMessage1:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kBarterPromptMessage2:
        ShowOnlineModalPrompt2(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kBarterPromptMessage3:
        ShowOnlineModalPrompt3(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kBarterPromptEndMessage:
        EndOnlineModalPrompt(online_modal_prompt_state(),
            static_cast<INT_PTR>(wparam));
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleBarterControlMessage(BarterWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (id >= kBarterLocalNameEditId && id <= kBarterFriendInfoPanelId) {
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    }
    return 0;
}

} // namespace ranker

#endif
