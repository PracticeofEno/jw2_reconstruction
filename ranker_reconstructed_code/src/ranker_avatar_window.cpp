#include "ranker_avatar_window.h"

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
#include <iterator>
#include <new>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed = 0x10cf0000;
constexpr DWORD kReadOnlyEditStyle =
    WS_CHILD | WS_VISIBLE | WS_DISABLED;
constexpr DWORD kEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kListStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr COLORREF kAvatarWhite = RGB(255, 255, 255);
constexpr COLORREF kAvatarSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kAvatarYellow = RGB(255, 255, 0);
constexpr COLORREF kAvatarGray = RGB(200, 200, 200);
constexpr COLORREF kAvatarBlack = RGB(0, 0, 0);
constexpr COLORREF kAvatarSelectedBlue = RGB(0, 0, 255);
constexpr int kAvatarForwardFocusCommandId = 0x9c41;
constexpr const char* kAvatarArchiveName = "Jw2_19.trc";
constexpr int kAvatarFrameWidth = 0x26;
constexpr int kAvatarFrameHeight = 0x26;
constexpr int kAvatarFrameCount = 192;
constexpr int kAvatarListRowHeight = 0x2b;
constexpr int kItemDefinitionRecordBytes = 0x28c;

AvatarWindowState g_avatar_window_state;
IndexedTextTableContext g_avatar_text_table;
AvatarEquipmentRuntimeVector g_avatar_equipment_runtime;
std::array<bool, 14> g_avatar_button_destructor_registered{};
std::array<bool, 3> g_avatar_button_array_destructor_registered{};
std::array<bool, 3> g_avatar_bitmap_destructor_registered{};
std::array<bool, 2> g_avatar_strip_destructor_registered{};
bool g_avatar_scroll_destructor_registered = false;
bool g_avatar_text_table_destructor_registered = false;
bool g_avatar_equipment_runtime_destructor_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

struct AvatarButtonSpec {
    LegacyImageButtonControl AvatarWindowState::*member;
    int id;
    const char* text;
    int layout_index;
    u32 normal_record;
    u32 pressed_record;
};

const AvatarButtonSpec kButtonSpecs[] = {
    {&AvatarWindowState::delete_button, kAvatarDeleteButtonId, "Delete", 1,
        kAvatarDeleteNormalBitmapRecord, kAvatarDeletePressedBitmapRecord},
    {&AvatarWindowState::exp_up_button, kAvatarExpUpButtonId, "Delete", 15,
        kAvatarExpUpNormalBitmapRecord, kAvatarExpUpPressedBitmapRecord},
    {&AvatarWindowState::buy_button, kAvatarBuyButtonId, "", 2,
        kAvatarBuyNormalBitmapRecord, kAvatarBuyPressedBitmapRecord},
    {&AvatarWindowState::sell_button, kAvatarSellButtonId, "", 3,
        kAvatarSellNormalBitmapRecord, kAvatarSellPressedBitmapRecord},
    {&AvatarWindowState::equip_button, kAvatarEquipButtonId, "Equip", 4,
        kAvatarEquipNormalBitmapRecord, kAvatarEquipPressedBitmapRecord},
    {&AvatarWindowState::unequip_button, kAvatarUnequipButtonId, "UnEquip", 5,
        kAvatarUnequipNormalBitmapRecord, kAvatarUnequipPressedBitmapRecord},
    {&AvatarWindowState::close_button, kAvatarCloseButtonId, "", 6,
        kAvatarCloseNormalBitmapRecord, kAvatarClosePressedBitmapRecord},
    {&AvatarWindowState::avatar_info_panel, kAvatarInfoPanelId, "AvataInfo", 19, 0, 0},
    {&AvatarWindowState::item_info_panel, kAvatarItemInfoPanelId, "ItemInfo", 26, 0, 0},
    {&AvatarWindowState::avatar_tab_button, kAvatarAvatarTabButtonId,
        "avatar_tab", 37, 0, 0},
    {&AvatarWindowState::weapon_tab_button, kAvatarWeaponTabButtonId,
        "Weapon_tab", 38, 0, 0},
    {&AvatarWindowState::armor_tab_button, kAvatarArmorTabButtonId,
        "Armor_tab", 39, 0, 0},
    {&AvatarWindowState::item_tab_button, kAvatarItemTabButtonId,
        "Weapon_tab", 40, 0, 0},
    {&AvatarWindowState::tab_background_button, kAvatarTabBackgroundButtonId,
        "avatar_tab", 36, kAvatarAvatarTabBitmapRecord, kAvatarAvatarTabBitmapRecord},
};

LRESULT CALLBACK avatar_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleAvatarWindowMessage(g_avatar_window_state, hwnd, message, wparam,
        lparam);
}

LRESULT CALLBACK avatar_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleAvatarControlMessage(g_avatar_window_state, hwnd, message, wparam,
        lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (registered) {
        return;
    }
    std::atexit(callback);
    registered = true;
}

#define DEFINE_AVATAR_SHUTDOWN(CallbackName, DestroyName) \
    void CallbackName() { \
        DestroyName(g_avatar_window_state); \
    }

DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_delete_button, DestroyAvatarDeleteButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_buy_button, DestroyAvatarBuyButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_sell_button, DestroyAvatarSellButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_equip_button, DestroyAvatarEquipButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_unequip_button, DestroyAvatarUnequipButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_close_button, DestroyAvatarCloseButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_exp_up_button, DestroyAvatarExpUpButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_info_panel_button,
    DestroyAvatarInfoPanelButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_item_info_panel_button,
    DestroyAvatarItemInfoPanelButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_slot_button_array,
    DestroyAvatarSlotButtonArray)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_equipment_slot_button_array,
    DestroyAvatarEquipmentSlotButtonArray)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_inventory_slot_button_array,
    DestroyAvatarInventorySlotButtonArray)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_tab_background_button,
    DestroyAvatarTabBackgroundButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_avatar_tab_button,
    DestroyAvatarAvatarTabButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_weapon_tab_button,
    DestroyAvatarWeaponTabButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_armor_tab_button,
    DestroyAvatarArmorTabButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_item_tab_button, DestroyAvatarItemTabButton)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_list_scroll, DestroyAvatarListScroll)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_active_tab, DestroyAvatarActiveTab)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_selected_slot_frame,
    DestroyAvatarSelectedSlotFrame)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_normal_slot_frame,
    DestroyAvatarNormalSlotFrame)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_icon_strip, DestroyAvatarIconStrip)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_item_icon_strip, DestroyAvatarItemIconStrip)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_text_table, DestroyAvatarTextTable)
DEFINE_AVATAR_SHUTDOWN(shutdown_avatar_equipment_runtime,
    DestroyAvatarEquipmentRuntime)

#undef DEFINE_AVATAR_SHUTDOWN

std::vector<AvatarLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<AvatarLayoutRect> rects;
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

AvatarLayoutRect layout_at(const AvatarWindowState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return AvatarLayoutRect{};
}

u32 read_le_u32(const u8* bytes) {
    return static_cast<u32>(bytes[0]) |
        (static_cast<u32>(bytes[1]) << 8) |
        (static_cast<u32>(bytes[2]) << 16) |
        (static_cast<u32>(bytes[3]) << 24);
}

i32 read_le_i32(const u8* bytes) {
    return WrappedU32ToI32(read_le_u32(bytes));
}

u32 read_u32(const u8* bytes, std::size_t byte_count, std::size_t offset) {
    if (bytes == nullptr || offset > byte_count || byte_count - offset < 4) {
        return 0;
    }
    return read_le_u32(bytes + offset);
}

i32 read_i32(const u8* bytes, std::size_t byte_count, std::size_t offset) {
    return WrappedU32ToI32(read_u32(bytes, byte_count, offset));
}

void write_le_u32(std::vector<u8>& packet, std::size_t offset, u32 value) {
    if (offset > packet.size() || packet.size() - offset < sizeof(value)) {
        return;
    }
    std::memcpy(packet.data() + offset, &value, sizeof(value));
}

void copy_fixed_string(std::vector<u8>& packet, std::size_t offset,
    std::size_t field_size, const char* text) {
    if (offset > packet.size() || field_size == 0) {
        return;
    }
    const std::size_t available = std::min(field_size, packet.size() - offset);
    std::memset(packet.data() + offset, 0, available);
    if (text != nullptr && available > 0) {
        std::strncpy(reinterpret_cast<char*>(packet.data() + offset), text,
            available - 1);
    }
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& target, const char* source) {
    target.fill(0);
    if (source != nullptr) {
        std::strncpy(target.data(), source, target.size() - 1);
    }
}

std::string fixed_string(const u8* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }
    std::size_t length = 0;
    while (length < size && data[length] != 0) {
        ++length;
    }
    return std::string(reinterpret_cast<const char*>(data), length);
}

std::string read_window_text(HWND window, int limit = 256) {
    if (window == nullptr || limit <= 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(limit), '\0');
    const int copied = GetWindowTextA(window, text.data(), limit);
    if (copied < 0) {
        return {};
    }
    text.resize(static_cast<std::size_t>(copied));
    return text;
}

void set_text(HWND window, const char* text) {
    if (window != nullptr) {
        SetWindowTextA(window, text == nullptr ? "" : text);
    }
}

std::vector<u8> make_packet(u32 opcode, u32 byte_count) {
    std::vector<u8> packet(byte_count, 0);
    write_le_u32(packet, 0, 3);
    write_le_u32(packet, 4, opcode);
    write_le_u32(packet, 8, byte_count);
    return packet;
}

void queue_packet(AvatarWindowState& state, const std::vector<u8>& packet) {
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
            const_cast<u8*>(packet.data()), static_cast<i32>(packet.size()), nullptr);
    }
}

void play_click_sound(AvatarWindowState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

void show_avatar_message(AvatarWindowState& state, const char* text,
    COLORREF color = kAvatarSoftWhite) {
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(state.window, text, color,
            state.callbacks.user_data);
        return;
    }
    MessageBoxA(state.window, text == nullptr ? "" : text,
        "Avatar", MB_OK | MB_ICONINFORMATION);
}

void forward_avatar_network_message(AvatarWindowState& state, WPARAM wparam,
    LPARAM lparam) {
    if (state.callbacks.forward_network_message != nullptr) {
        state.callbacks.forward_network_message(state, wparam, lparam);
        return;
    }
    if (state.parent_window != nullptr && IsWindow(state.parent_window)) {
        PostMessageA(state.parent_window, kAvatarNetworkMessage, wparam, lparam);
    }
}

LegacyImageButtonControl* button_for_id(AvatarWindowState& state, int id) {
    for (const AvatarButtonSpec& spec : kButtonSpecs) {
        if (spec.id == id) {
            return &(state.*(spec.member));
        }
    }
    if (id >= kAvatarSlotFirstButtonId &&
        id < kAvatarSlotFirstButtonId + kAvatarSlotCount) {
        return &state.avatar_slots[static_cast<std::size_t>(
            id - kAvatarSlotFirstButtonId)];
    }
    if (id >= kAvatarEquipmentSlotFirstButtonId &&
        id < kAvatarEquipmentSlotFirstButtonId + kAvatarEquipmentSlotCount) {
        return &state.equipment_slots[static_cast<std::size_t>(
            id - kAvatarEquipmentSlotFirstButtonId)];
    }
    if (id >= kAvatarInventorySlotFirstButtonId &&
        id < kAvatarInventorySlotFirstButtonId + kAvatarInventorySlotCount) {
        return &state.inventory_slots[static_cast<std::size_t>(
            id - kAvatarInventorySlotFirstButtonId)];
    }
    return nullptr;
}

AvatarTextControl* text_control_for_id(AvatarWindowState& state, int id) {
    switch (id) {
    case kAvatarJemEditId:
        return &state.jem_edit;
    case kAvatarExpPointEditId:
        return &state.exp_point_edit;
    case kAvatarNameEditId:
        return &state.avatar_name_edit;
    case kAvatarBuyNameEditId:
        return &state.buy_name_edit;
    case kAvatarListBoxId:
        return &state.list_box;
    default:
        return nullptr;
    }
}

WNDPROC original_proc_for_id(AvatarWindowState& state, int id) {
    if (id == kAvatarScrollBarId) {
        return state.scroll_bar.original_window_proc;
    }
    if (AvatarTextControl* control = text_control_for_id(state, id)) {
        return control->original_window_proc;
    }
    if (LegacyImageButtonControl* button = button_for_id(state, id)) {
        return button->original_window_proc;
    }
    return nullptr;
}

void subclass_window(HWND window) {
    if (window != nullptr) {
        SetWindowLongPtrA(window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(avatar_control_proc));
    }
}

void subclass_text_control(AvatarTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    subclass_window(control.window);
}

bool create_text_control(AvatarTextControl& control, HWND parent, HINSTANCE instance,
    const char* class_name, DWORD style, int id, const AvatarLayoutRect& rect) {
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
    const char* text, int id, const AvatarLayoutRect& rect, u32 normal_record,
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

bool create_button_from_spec(AvatarWindowState& state, const AvatarButtonSpec& spec) {
    return create_image_button(state.*(spec.member), state.window, spec.text, spec.id,
        layout_at(state, static_cast<std::size_t>(spec.layout_index)),
        spec.normal_record, spec.pressed_record);
}

bool load_icon_strips(AvatarWindowState& state) {
    const bool avatars_ok = LoadAvatarIconStrip(state.avatar_strip);
    const bool items_ok = LoadItemIconStrip(state.item_strip);
    return avatars_ok && items_ok;
}

void load_avatar_id_list_from_text_table(AvatarWindowState& state) {
    state.purchasable_avatar_ids.clear();
    for (const std::string& value : g_avatar_text_table.rows) {
        const int id = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
        if (id > 0) {
            state.purchasable_avatar_ids.push_back(id);
        }
    }
}

bool LoadAvatarTextTableFromJw219Trc(AvatarWindowState& state) {
    std::vector<u8> record;
    if (!LoadTrcRecordAlloc(kAvatarArchiveName, kAvatarTextTrcRecord, record, 1)) {
        state.purchasable_avatar_ids.clear();
        ResetIndexedTextTableContext(g_avatar_text_table);
        return false;
    }

    const bool loaded = LoadIndexedTextTableFromMemory(g_avatar_text_table,
        reinterpret_cast<const char*>(record.data()));
    if (loaded) {
        load_avatar_id_list_from_text_table(state);
    }
    else {
        state.purchasable_avatar_ids.clear();
    }
    return loaded;
}

void update_currency_text(AvatarWindowState& state) {
    UpdateAvatarJemText(state);
    UpdateAvatarExpPointText(state);
}

const AvatarItemDefinition* find_item_definition(
    const AvatarWindowState& state, i32 item_id) {
    if (item_id < 0) {
        return nullptr;
    }
    const auto found = std::find_if(state.item_definitions.begin(),
        state.item_definitions.end(), [item_id](const AvatarItemDefinition& item) {
            return item.item_id == item_id;
        });
    return found == state.item_definitions.end() ? nullptr : &*found;
}

std::string startup_item_name(i32 item_id) {
    if (item_id < 0) {
        return {};
    }
    const std::string_view text = GetIndexedTextTableRow(
        StartupAuxiliaryIndexedTextTable(5), static_cast<u32>(item_id));
    return std::string(text.begin(), text.end());
}

std::string startup_avatar_name(i32 avatar_id) {
    if (avatar_id < 0) {
        return {};
    }
    const std::string_view text = GetIndexedTextTableRow(
        StartupAuxiliaryIndexedTextTable(0), static_cast<u32>(avatar_id));
    return std::string(text.begin(), text.end());
}

void set_avatar_display_name(AvatarDefinitionStats& stats, i32 avatar_id) {
    if (std::string text = startup_avatar_name(avatar_id); !text.empty()) {
        copy_c_string(stats.display_name, text.c_str());
        return;
    }
    char text[32]{};
    std::snprintf(text, sizeof(text), "Avatar %d", avatar_id);
    copy_c_string(stats.display_name, text);
}

AvatarItemOffer item_offer_or_fallback(const AvatarWindowState& state, i32 item_id) {
    if (item_id >= 0 &&
        static_cast<std::size_t>(item_id) < state.item_offers.size()) {
        return state.item_offers[static_cast<std::size_t>(item_id)];
    }
    AvatarItemOffer offer{};
    if (const AvatarItemDefinition* definition = find_item_definition(state, item_id)) {
        offer.available = item_id > 0;
        offer.price = definition->fallback_price;
    }
    return offer;
}

i32 item_price(const AvatarWindowState& state, i32 item_id) {
    AvatarItemOffer offer = item_offer_or_fallback(state, item_id);
    if (offer.price != 0) {
        return offer.price;
    }
    if (const AvatarItemDefinition* definition = find_item_definition(state, item_id)) {
        return definition->fallback_price;
    }
    return 0;
}

std::string item_name(const AvatarWindowState& state, i32 item_id) {
    if (const AvatarItemDefinition* definition = find_item_definition(state, item_id)) {
        if (!definition->name.empty()) {
            return definition->name;
        }
    }
    if (std::string text = startup_item_name(item_id); !text.empty()) {
        return text;
    }
    char text[32]{};
    std::snprintf(text, sizeof(text), "Item %d", item_id);
    return text;
}

const char* item_category_name(i32 category) {
    switch (category) {
    case 0:
        return startup_message_row(260, "Common");
    case 1:
        return startup_message_row(261, "Weapon");
    case 2:
        return startup_message_row(262, "Armor");
    case 3:
        return startup_message_row(263, "Food");
    default:
        return "Misc";
    }
}

const char* item_mode_name(i32 mode) {
    static constexpr const char* kFallbacks[] = {
        "Carry effect",
        "Carry count effect",
        "Use once",
        "Use count",
        "Pickup consume",
        "Pickup keep",
        "Attach",
    };
    if (mode >= 0 && mode < static_cast<i32>(std::size(kFallbacks))) {
        return startup_message_row(253 + static_cast<std::size_t>(mode),
            kFallbacks[static_cast<std::size_t>(mode)]);
    }
    return "";
}

std::string item_stat_text(const AvatarItemDefinition& item) {
    char line[96]{};
    std::string out;
    auto append = [&out, &line](std::size_t row, const char* fallback, i32 value) {
        if (value <= 0) {
            return;
        }
        std::snprintf(line, sizeof(line), startup_message_row(row, fallback), value);
        out += line;
    };
    auto append_flag = [&out](std::size_t row, const char* fallback, i32 value) {
        if (value > 0) {
            out += startup_message_row(row, fallback);
        }
    };
    append(269, ",LEV:%d", item.level_bonus);
    append(270, ",EXP:%d", item.exp_bonus);
    append(271, ",HP:%d", item.hp);
    append(272, ",MP:%d", item.mp);
    append(273, ",OP:%d", item.op);
    append(274, ",DP:%d", item.dp);
    append(275, ",SS:%d", item.attack);
    append(276, ",SR:%d", item.defense);
    append(277, ",MS:%d", item.range);
    append(278, ",ES:%d", item.movement);
    append_flag(279, ",CLOAK", item.cloak);
    append_flag(280, ",DETECT", item.detect);
    if (!out.empty()) {
        out.erase(out.begin());
    }
    return out;
}

AvatarDefinitionStats lookup_avatar_stats(AvatarWindowState& state, i32 avatar_id) {
    AvatarDefinitionStats stats{};
    if (state.callbacks.lookup_avatar_definition != nullptr &&
        state.callbacks.lookup_avatar_definition(state, avatar_id, stats)) {
        return stats;
    }
    set_avatar_display_name(stats, avatar_id);
    stats.hp = 0;
    stats.mp = 0;
    stats.op = 0;
    stats.dp = 0;
    stats.next_exp_base = 1;
    stats.next_exp_per_level = 1;
    return stats;
}

std::string avatar_catalog_name(AvatarWindowState& state, i32 avatar_id) {
    AvatarDefinitionStats stats = lookup_avatar_stats(state, avatar_id);
    if (stats.display_name[0] != '\0') {
        return stats.display_name.data();
    }
    if (std::string text = startup_avatar_name(avatar_id); !text.empty()) {
        return text;
    }
    char text[32]{};
    std::snprintf(text, sizeof(text), "Avatar %d", avatar_id);
    return text;
}

int avatar_next_exp(AvatarWindowState& state, const AvatarOwnedSlot& slot) {
    return CalculateAvatarNextExpThreshold(state, slot);
}

void set_list_top_from_scroll(AvatarWindowState& state) {
    if (state.list_box.window == nullptr) {
        return;
    }
    SendMessageA(state.list_box.window, LB_SETTOPINDEX,
        static_cast<WPARAM>(GetLegacyCustomScrollControlValue(state.scroll_bar)), 0);
}

int visible_list_rows(const AvatarWindowState& state) {
    const AvatarLayoutRect rect = layout_at(state, 41);
    if (rect.height <= 0) {
        return 1;
    }
    return std::max(1, rect.height / kAvatarListRowHeight);
}

void sync_scroll_bar(AvatarWindowState& state, int item_count) {
    const int rows = visible_list_rows(state);
    const int max_top = std::max(0, item_count - rows);
    SetLegacyCustomScrollControlPageStep(state.scroll_bar, rows);
    SetLegacyCustomScrollControlRange(state.scroll_bar, 0, max_top, false);
    SetLegacyCustomScrollControlVisible(state.scroll_bar, max_top > 0);
    SetLegacyCustomScrollControlValue(state.scroll_bar,
        std::min(GetLegacyCustomScrollControlValue(state.scroll_bar), max_top),
        true);
}

void redraw_window(HWND window) {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
}

void redraw_avatar_slots(AvatarWindowState& state) {
    for (const LegacyImageButtonControl& button : state.avatar_slots) {
        redraw_window(button.window);
    }
    redraw_window(state.delete_button.window);
    redraw_window(state.avatar_info_panel.window);
}

void redraw_equipment_slots(AvatarWindowState& state) {
    for (const LegacyImageButtonControl& button : state.equipment_slots) {
        redraw_window(button.window);
    }
    redraw_window(state.avatar_info_panel.window);
    redraw_window(state.item_info_panel.window);
}

void redraw_inventory_slots(AvatarWindowState& state) {
    for (const LegacyImageButtonControl& button : state.inventory_slots) {
        redraw_window(button.window);
    }
    redraw_window(state.item_info_panel.window);
}

bool item_visible_on_tab(const AvatarWindowState& state,
    const AvatarItemDefinition& item, i32 tab) {
    const AvatarItemOffer offer = item_offer_or_fallback(state, item.item_id);
    if (!offer.available && !state.item_offers.empty()) {
        return false;
    }
    int category = 0;
    if (tab == 1) {
        category = 1;
    }
    else if (tab == 2) {
        category = 2;
    }
    else if (tab == 3) {
        category = 0;
    }
    return item.category == category;
}

int first_free_avatar_slot(const AvatarWindowState& state) {
    for (int i = 0; i < kAvatarSlotCount; ++i) {
        if (state.owned_avatars[static_cast<std::size_t>(i)].avatar_id < 0) {
            return i;
        }
    }
    return -1;
}

int first_free_inventory_slot(const AvatarWindowState& state) {
    for (int i = 0; i < kAvatarInventorySlotCount; ++i) {
        if (state.inventory[static_cast<std::size_t>(i)] < 1) {
            return i;
        }
    }
    return -1;
}

int selected_list_item_data(const AvatarWindowState& state) {
    if (state.list_box.window == nullptr) {
        return -1;
    }
    const LRESULT selected = SendMessageA(state.list_box.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return -1;
    }
    const LRESULT data = SendMessageA(state.list_box.window, LB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    return data == LB_ERR ? -1 : static_cast<int>(data);
}

void draw_slot_background(AvatarWindowState& state, HDC dc, bool selected) {
    StretchBitmapMemoryResourceToDc(
        selected ? state.selected_slot_bitmap : state.normal_slot_bitmap, dc, 0, 0);
}

void draw_item_frame(AvatarWindowState& state, HDC dc, int item_id) {
    const AvatarItemDefinition* definition = find_item_definition(state, item_id);
    const u32 frame = definition == nullptr ? static_cast<u32>(std::max(0, item_id)) :
        static_cast<u32>(std::max(0, definition->icon_frame));
    if (frame < kAvatarFrameCount) {
        DrawSecondaryRawIndexedBitmapStripFrame(state.item_strip, dc, 1, 1, frame);
    }
}

void draw_avatar_slot(AvatarWindowState& state, const DRAWITEMSTRUCT& draw,
    int slot_index) {
    if (slot_index < 0 || slot_index >= kAvatarSlotCount) {
        return;
    }
    const bool selected = slot_index == state.selected_avatar_slot;
    draw_slot_background(state, draw.hDC, selected);
    const AvatarOwnedSlot& slot = state.owned_avatars[static_cast<std::size_t>(slot_index)];
    if (slot.avatar_id < 1) {
        return;
    }
    const u32 frame = static_cast<u32>(std::max(0, slot.avatar_id));
    if (frame < kAvatarFrameCount) {
        DrawRawIndexedBitmapStripFrame(state.avatar_strip, draw.hDC, 1, 1, frame);
    }
    RECT text_rect = draw.rcItem;
    text_rect.left += 1;
    text_rect.top += 0x2f;
    text_rect.right -= 1;
    SetTextColor(draw.hDC, kAvatarSoftWhite);
    SetBkMode(draw.hDC, TRANSPARENT);
    char text[32]{};
    std::snprintf(text, sizeof(text), startup_message_row(240, "Lev%3d"),
        slot.level + 1);
    DrawTextA(draw.hDC, text, -1, &text_rect, DT_CENTER | DT_SINGLELINE);
}

void draw_item_slot(AvatarWindowState& state, const DRAWITEMSTRUCT& draw,
    int slot_index, bool equipment) {
    const int selected_slot = equipment ? state.selected_equipment_slot :
        state.selected_inventory_slot;
    draw_slot_background(state, draw.hDC, slot_index == selected_slot);
    int item_id = 0;
    if (equipment) {
        if (slot_index >= 0 && slot_index < kAvatarEquipmentSlotCount) {
            const AvatarOwnedSlot& avatar =
                state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
            item_id = avatar.equipment[static_cast<std::size_t>(slot_index)];
        }
    } else if (slot_index >= 0 && slot_index < kAvatarInventorySlotCount) {
        item_id = state.inventory[static_cast<std::size_t>(slot_index)];
    }
    if (item_id > 0) {
        draw_item_frame(state, draw.hDC, item_id);
    }
}

void draw_avatar_info_panel(AvatarWindowState& state, const DRAWITEMSTRUCT& draw) {
    BitBlt(draw.hDC, 0, 0, draw.rcItem.right - draw.rcItem.left,
        draw.rcItem.bottom - draw.rcItem.top, nullptr, 0, 0, BLACKNESS);
    const AvatarOwnedSlot& slot =
        state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
    if (slot.avatar_id < 0) {
        return;
    }

    int hp_bonus = 0;
    int mp_bonus = 0;
    int op_bonus = 0;
    int dp_bonus = 0;
    for (int i = 0; i < kAvatarEquipmentSlotCount; ++i) {
        const int item_id = slot.equipment[static_cast<std::size_t>(i)];
        const AvatarItemDefinition* item = find_item_definition(state, item_id);
        if (item == nullptr) {
            continue;
        }
        if ((item->category == 0 && item->mode == 0) || i < 2) {
            hp_bonus += item->hp;
            mp_bonus += item->mp;
            op_bonus += item->op;
            dp_bonus += item->dp;
        }
    }

    RECT rect = draw.rcItem;
    rect.left = 2;
    rect.top = 2;
    rect.right -= 2;
    SetTextColor(draw.hDC, kAvatarGray);
    SetBkColor(draw.hDC, kAvatarBlack);
    SetBkMode(draw.hDC, TRANSPARENT);

    char text[128]{};
    std::snprintf(text, sizeof(text), startup_message_row(227, "Exp:%3d/%3d"),
        slot.exp_progress,
        avatar_next_exp(state, slot));
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), startup_message_row(228, "HP :%3d(%d)"),
        slot.hp, hp_bonus);
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), startup_message_row(229, "MP :%3d(%d)"),
        slot.mp, mp_bonus);
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), startup_message_row(230, "OP :%3d(%d)"),
        slot.op, op_bonus);
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), startup_message_row(231, "DP :%3d(%d)"),
        slot.dp, dp_bonus);
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
}

void draw_item_info_panel(AvatarWindowState& state, const DRAWITEMSTRUCT& draw) {
    BitBlt(draw.hDC, 0, 0, draw.rcItem.right - draw.rcItem.left,
        draw.rcItem.bottom - draw.rcItem.top, nullptr, 0, 0, BLACKNESS);
    const AvatarItemDefinition* item =
        find_item_definition(state, state.current_item_id);
    if (item == nullptr || state.current_item_id <= 0) {
        return;
    }

    RECT rect = draw.rcItem;
    rect.left = 2;
    rect.top = 2;
    rect.right -= 2;
    SetTextColor(draw.hDC, kAvatarGray);
    SetBkColor(draw.hDC, kAvatarBlack);
    SetBkMode(draw.hDC, TRANSPARENT);

    char text[256]{};
    std::snprintf(text, sizeof(text), startup_message_row(232, "NAME : %s"),
        item->name.c_str());
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), "(%s)%s",
        item_category_name(item->category), item_mode_name(item->mode));
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    std::snprintf(text, sizeof(text), "%s", BuildAvatarItemStatText(*item).c_str());
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
    rect.top += 14;
    const int price = item_price(state, state.current_item_id);
    std::snprintf(text, sizeof(text),
        startup_message_row(235, "BUY  :%5d   SELL :%5d"), price,
        (price * 70) / 100);
    DrawTextA(draw.hDC, text, -1, &rect, DT_SINGLELINE);
}

void draw_list_row(AvatarWindowState& state, const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return;
    }
    char text[256]{};
    SendMessageA(draw.hwndItem, LB_GETTEXT, draw.itemID,
        reinterpret_cast<LPARAM>(text));
    const int data = static_cast<int>(SendMessageA(draw.hwndItem, LB_GETITEMDATA,
        draw.itemID, 0));

    RECT fill = draw.rcItem;
    HBRUSH brush = CreateSolidBrush((draw.itemState & ODS_SELECTED) != 0 ?
        kAvatarSelectedBlue : kAvatarBlack);
    if (brush != nullptr) {
        FillRect(draw.hDC, &fill, brush);
        DeleteObject(brush);
    }

    RECT icon_rect = draw.rcItem;
    icon_rect.left += 3;
    icon_rect.top += 4;
    if (state.current_tab == 0) {
        if (data >= 0 && static_cast<std::size_t>(data) < state.avatar_catalog.size()) {
            const AvatarCatalogEntry& entry =
                state.avatar_catalog[static_cast<std::size_t>(data)];
            if (entry.avatar_id >= 0 &&
                static_cast<u32>(entry.avatar_id) < kAvatarFrameCount) {
                DrawRawIndexedBitmapStripFrame(state.avatar_strip, draw.hDC,
                    icon_rect.left, icon_rect.top, static_cast<u32>(entry.avatar_id));
            }
        }
    } else {
        draw_item_frame(state, draw.hDC, data);
    }

    RECT text_rect = draw.rcItem;
    text_rect.left += 0x2d;
    text_rect.top += 2;
    SetTextColor(draw.hDC, kAvatarGray);
    SetBkColor(draw.hDC, (draw.itemState & ODS_SELECTED) != 0 ?
        kAvatarSelectedBlue : kAvatarBlack);
    SetBkMode(draw.hDC, OPAQUE);
    DrawTextA(draw.hDC, text, -1, &text_rect, DT_SINGLELINE);

    text_rect.top += 14;
    char line[256]{};
    if (state.current_tab == 0 &&
        data >= 0 && static_cast<std::size_t>(data) < state.avatar_catalog.size()) {
        const AvatarCatalogEntry& entry =
            state.avatar_catalog[static_cast<std::size_t>(data)];
        std::snprintf(line, sizeof(line), "%s",
            BuildAvatarCatalogStatText(entry.hp, entry.mp, entry.op,
                entry.dp).c_str());
        DrawTextA(draw.hDC, line, -1, &text_rect, DT_SINGLELINE);
        text_rect.top += 14;
        std::snprintf(line, sizeof(line), startup_message_row(237, "BUY:%5d"),
            entry.price);
        DrawTextA(draw.hDC, line, -1, &text_rect, DT_SINGLELINE);
    } else if (const AvatarItemDefinition* item = find_item_definition(state, data)) {
        std::snprintf(line, sizeof(line), "%s",
            BuildAvatarItemStatText(*item).c_str());
        DrawTextA(draw.hDC, line, -1, &text_rect, DT_SINGLELINE);
        text_rect.top += 14;
        const int price = item_price(state, data);
        std::snprintf(line, sizeof(line),
            startup_message_row(239, "BUY:%5d   SELL:%5d"), price,
            (price * 70) / 100);
        DrawTextA(draw.hDC, line, -1, &text_rect, DT_SINGLELINE);
    }
}

bool paint_background_if_current(AvatarWindowState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
    EndPaint(hwnd, &paint);
    return true;
}

void release_window_resources(AvatarWindowState& state) {
    RestoreAvatarWindowAccelerators(state);
    DestroyLegacyImageButtonControl(state.delete_button);
    DestroyLegacyImageButtonControl(state.buy_button);
    DestroyLegacyImageButtonControl(state.sell_button);
    DestroyLegacyImageButtonControl(state.equip_button);
    DestroyLegacyImageButtonControl(state.unequip_button);
    DestroyLegacyImageButtonControl(state.close_button);
    DestroyLegacyImageButtonControl(state.exp_up_button);
    DestroyLegacyImageButtonControl(state.avatar_info_panel);
    DestroyLegacyImageButtonControl(state.item_info_panel);
    DestroyLegacyImageButtonControl(state.tab_background_button);
    DestroyLegacyImageButtonControl(state.avatar_tab_button);
    DestroyLegacyImageButtonControl(state.weapon_tab_button);
    DestroyLegacyImageButtonControl(state.armor_tab_button);
    DestroyLegacyImageButtonControl(state.item_tab_button);
    for (LegacyImageButtonControl& button : state.avatar_slots) {
        DestroyLegacyImageButtonControl(button);
    }
    for (LegacyImageButtonControl& button : state.equipment_slots) {
        DestroyLegacyImageButtonControl(button);
    }
    for (LegacyImageButtonControl& button : state.inventory_slots) {
        DestroyLegacyImageButtonControl(button);
    }
    DestroyLegacyCustomScrollControl(state.scroll_bar);
    state.jem_edit = AvatarTextControl{};
    state.exp_point_edit = AvatarTextControl{};
    state.avatar_name_edit = AvatarTextControl{};
    state.buy_name_edit = AvatarTextControl{};
    state.list_box = AvatarTextControl{};
    ReleaseBitmapMemoryResource(state.background);
    ReleaseBitmapMemoryResource(state.selected_slot_bitmap);
    ReleaseBitmapMemoryResource(state.normal_slot_bitmap);
    ReleaseBitmapMemoryResource(state.active_tab_bitmap);
    ReleaseRawIndexedBitmapStrip(state.avatar_strip);
    ReleaseSecondaryRawIndexedBitmapStrip(state.item_strip);
    state.window = nullptr;
    state.visible = false;
}

void queue_avatar_catalog_request(AvatarWindowState& state) {
    std::vector<u8> packet = make_packet(0x49, 0x15);
    write_le_u32(packet, 0x0d, static_cast<u32>(GetAvatarCatalogVersion(state)));
    write_le_u32(packet, 0x11,
        static_cast<u32>(GetAvatarItemCatalogVersion(state)));
    queue_packet(state, packet);
}

void fill_avatar_catalog_stats(AvatarWindowState& state, AvatarCatalogEntry& entry) {
    AvatarDefinitionStats stats = lookup_avatar_stats(state, entry.avatar_id);
    if (entry.hp == 0) {
        entry.hp = stats.hp;
    }
    if (entry.mp == 0) {
        entry.mp = stats.mp;
    }
    if (entry.op == 0) {
        entry.op = stats.op;
    }
    if (entry.dp == 0) {
        entry.dp = stats.dp;
    }
    if (entry.display_name.empty()) {
        entry.display_name = avatar_catalog_name(state, entry.avatar_id);
    }
}

void parse_avatar_catalog_packet(AvatarWindowState& state, const u8* payload,
    i32 byte_count) {
    state.avatar_catalog_version = read_i32(payload, byte_count, 0x0d);
    const int count = std::max(0, read_i32(payload, byte_count, 0x11));
    state.avatar_catalog.clear();
    state.avatar_catalog.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t offset = 0x15 + static_cast<std::size_t>(i) * 0x18;
        if (offset > static_cast<std::size_t>(byte_count) ||
            static_cast<std::size_t>(byte_count) - offset < 0x18) {
            break;
        }
        AvatarCatalogEntry entry{};
        entry.avatar_id = read_i32(payload, byte_count, offset + 0);
        entry.price = read_i32(payload, byte_count, offset + 4);
        entry.hp = read_i32(payload, byte_count, offset + 8);
        entry.mp = read_i32(payload, byte_count, offset + 0x0c);
        entry.op = read_i32(payload, byte_count, offset + 0x10);
        entry.dp = read_i32(payload, byte_count, offset + 0x14);
        fill_avatar_catalog_stats(state, entry);
        state.avatar_catalog.push_back(std::move(entry));
    }
}

void parse_item_offer_packet(AvatarWindowState& state, const u8* payload,
    i32 byte_count) {
    state.item_catalog_version = read_i32(payload, byte_count, 0x0d);
    const int count = std::max(0, read_i32(payload, byte_count, 0x11));
    state.item_offers.assign(static_cast<std::size_t>(count), AvatarItemOffer{});
    for (int i = 0; i < count; ++i) {
        const std::size_t offset = 0x15 + static_cast<std::size_t>(i) * 8;
        if (offset > static_cast<std::size_t>(byte_count) ||
            static_cast<std::size_t>(byte_count) - offset < 8) {
            break;
        }
        AvatarItemOffer offer{};
        offer.available = read_i32(payload, byte_count, offset) != 0;
        offer.price = read_i32(payload, byte_count, offset + 4);
        state.item_offers[static_cast<std::size_t>(i)] = offer;
    }
}

void buy_avatar(AvatarWindowState& state, const AvatarCatalogEntry& entry) {
    const std::string name = read_window_text(state.buy_name_edit.window, 0x14);
    if (name.empty()) {
        show_avatar_message(state,
            startup_message_row(201, "Enter a name for the avatar to buy."));
        SetFocus(state.buy_name_edit.window);
        return;
    }
    if (entry.price > state.jem) {
        show_avatar_message(state, startup_message_row(197, "Not enough JEM."));
        return;
    }
    const int slot_index = first_free_avatar_slot(state);
    if (slot_index < 0) {
        show_avatar_message(state,
            startup_message_row(202, "There is no empty avatar slot."));
        return;
    }

    AvatarOwnedSlot& slot = state.owned_avatars[static_cast<std::size_t>(slot_index)];
    slot = AvatarOwnedSlot{};
    copy_c_string(slot.name, name.c_str());
    slot.avatar_id = entry.avatar_id;
    slot.hp = entry.hp;
    slot.mp = entry.mp;
    slot.op = entry.op;
    slot.dp = entry.dp;
    slot.level = 0;
    slot.exp_progress = 0;
    slot.exp_total = 0;
    slot.equipment.fill(0);
    state.jem -= entry.price;
    update_currency_text(state);

    std::vector<u8> packet = make_packet(0x54, 0x35);
    write_le_u32(packet, 0x0d, static_cast<u32>(entry.avatar_id));
    write_le_u32(packet, 0x11, static_cast<u32>(entry.hp));
    write_le_u32(packet, 0x15, static_cast<u32>(entry.mp));
    write_le_u32(packet, 0x19, static_cast<u32>(entry.op));
    write_le_u32(packet, 0x1d, static_cast<u32>(entry.dp));
    copy_fixed_string(packet, 0x21, 0x14, name.c_str());
    queue_packet(state, packet);

    set_text(state.buy_name_edit.window, " ");
    SelectAvatarSlot(state, slot_index);
}

void buy_item(AvatarWindowState& state, int item_id) {
    const int price = item_price(state, item_id);
    if (price > state.jem) {
        show_avatar_message(state, startup_message_row(197, "Not enough JEM."));
        return;
    }
    const int slot = first_free_inventory_slot(state);
    if (slot < 0) {
        show_avatar_message(state,
            startup_message_row(200, "The inventory is full."));
        return;
    }
    state.inventory[static_cast<std::size_t>(slot)] = item_id;
    state.jem -= price;
    update_currency_text(state);

    std::vector<u8> packet = make_packet(0x4c, 0x11);
    write_le_u32(packet, 0x0d, static_cast<u32>(item_id));
    queue_packet(state, packet);
    SelectAvatarInventorySlot(state, slot);
}

void handle_buy_command(AvatarWindowState& state) {
    const int data = selected_list_item_data(state);
    if (data < 0) {
        show_avatar_message(state,
            startup_message_row(196, "Select a target to buy."));
        return;
    }
    if (state.current_tab == 0) {
        if (static_cast<std::size_t>(data) >= state.avatar_catalog.size()) {
            return;
        }
        buy_avatar(state, state.avatar_catalog[static_cast<std::size_t>(data)]);
    } else {
        buy_item(state, data);
    }
}

void handle_sell_command(AvatarWindowState& state) {
    const int slot = state.selected_inventory_slot;
    if (slot < 0 || slot >= kAvatarInventorySlotCount) {
        return;
    }
    const int item_id = state.inventory[static_cast<std::size_t>(slot)];
    if (item_id <= 0) {
        return;
    }
    state.jem += (item_price(state, item_id) * 70) / 100;
    update_currency_text(state);

    std::vector<u8> packet = make_packet(0x4e, 0x11);
    write_le_u32(packet, 0x0d, static_cast<u32>(slot));
    queue_packet(state, packet);

    state.inventory[static_cast<std::size_t>(slot)] = 0;
    SelectAvatarInventorySlot(state, slot);
}

bool item_compatible(AvatarWindowState& state, int item_id) {
    return CheckAvatarItemCompatibleWithSelectedAvatar(state, item_id);
}

int target_equipment_slot(AvatarWindowState& state, const AvatarItemDefinition& item) {
    AvatarOwnedSlot& avatar =
        state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
    if (item.category == 1 && avatar.equipment[0] < 1) {
        return 0;
    }
    if (item.category == 2 && avatar.equipment[1] < 1) {
        return 1;
    }
    for (int i = 2; i < kAvatarEquipmentSlotCount; ++i) {
        if (avatar.equipment[static_cast<std::size_t>(i)] < 1) {
            return i;
        }
    }
    if (state.selected_equipment_slot < 2) {
        state.selected_equipment_slot = 2;
    }
    return state.selected_equipment_slot;
}

void handle_equip_command(AvatarWindowState& state) {
    const int inventory_slot = state.selected_inventory_slot;
    if (inventory_slot < 0 || inventory_slot >= kAvatarInventorySlotCount) {
        return;
    }
    const int item_id = state.inventory[static_cast<std::size_t>(inventory_slot)];
    if (item_id <= 0) {
        return;
    }
    AvatarOwnedSlot& avatar =
        state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
    if (avatar.avatar_id < 0) {
        show_avatar_message(state,
            startup_message_row(199, "Select an avatar first."));
        return;
    }
    const AvatarItemDefinition* item = find_item_definition(state, item_id);
    if (item == nullptr || !item_compatible(state, item_id)) {
        show_avatar_message(state,
            startup_message_row(252,
                "This item cannot be equipped by the selected avatar."));
        return;
    }

    const int equipment_slot = target_equipment_slot(state, *item);
    std::swap(avatar.equipment[static_cast<std::size_t>(equipment_slot)],
        state.inventory[static_cast<std::size_t>(inventory_slot)]);

    std::vector<u8> packet = make_packet(0x50, 0x19);
    write_le_u32(packet, 0x0d, static_cast<u32>(state.selected_avatar_slot));
    write_le_u32(packet, 0x11, static_cast<u32>(equipment_slot));
    write_le_u32(packet, 0x15, static_cast<u32>(inventory_slot));
    queue_packet(state, packet);
    SelectAvatarEquipmentSlot(state, equipment_slot);
    SelectAvatarInventorySlot(state, inventory_slot);
}

void handle_unequip_command(AvatarWindowState& state) {
    AvatarOwnedSlot& avatar =
        state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
    const int equipment_slot = state.selected_equipment_slot;
    if (equipment_slot < 0 || equipment_slot >= kAvatarEquipmentSlotCount ||
        avatar.equipment[static_cast<std::size_t>(equipment_slot)] <= 0) {
        return;
    }
    const int inventory_slot = first_free_inventory_slot(state);
    if (inventory_slot < 0) {
        show_avatar_message(state,
            startup_message_row(200, "The inventory is full."));
        return;
    }
    state.inventory[static_cast<std::size_t>(inventory_slot)] =
        avatar.equipment[static_cast<std::size_t>(equipment_slot)];
    avatar.equipment[static_cast<std::size_t>(equipment_slot)] = 0;

    std::vector<u8> packet = make_packet(0x52, 0x15);
    write_le_u32(packet, 0x0d, static_cast<u32>(state.selected_avatar_slot));
    write_le_u32(packet, 0x11, static_cast<u32>(equipment_slot));
    queue_packet(state, packet);
    SelectAvatarEquipmentSlot(state, equipment_slot);
    SelectAvatarInventorySlot(state, inventory_slot);
}

void handle_delete_accept(AvatarWindowState& state) {
    const int slot = state.selected_avatar_slot;
    if (slot < 0 || slot >= kAvatarSlotCount) {
        return;
    }
    AvatarOwnedSlot& avatar = state.owned_avatars[static_cast<std::size_t>(slot)];
    avatar.avatar_id = -1;
    avatar.name.fill(0);
    avatar.equipment.fill(0);

    std::vector<u8> packet = make_packet(0x56, 0x11);
    write_le_u32(packet, 0x0d, static_cast<u32>(slot));
    queue_packet(state, packet);
    SelectAvatarSlot(state, slot);
}

void ApplyAvatarExperiencePoint(AvatarWindowState& state) {
    AvatarOwnedSlot& slot =
        state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
    if (slot.avatar_id < 0 || state.exp_points <= 0) {
        return;
    }
    --state.exp_points;
    ++slot.exp_progress;
    ++slot.exp_total;
    const int next = avatar_next_exp(state, slot);
    if (slot.exp_progress >= next) {
        slot.exp_progress = 0;
        ++slot.level;
        AvatarDefinitionStats stats = lookup_avatar_stats(state, slot.avatar_id);
        if (stats.hp != 0 || stats.mp != 0 || stats.op != 0 || stats.dp != 0) {
            slot.hp = stats.hp;
            slot.mp = stats.mp;
            slot.op = stats.op;
            slot.dp = stats.dp;
        }
    }
    update_currency_text(state);
    std::vector<u8> packet = make_packet(0x5f, 0x29);
    write_le_u32(packet, 0x0d, static_cast<u32>(state.selected_avatar_slot));
    write_le_u32(packet, 0x11, static_cast<u32>(slot.level));
    write_le_u32(packet, 0x15, static_cast<u32>(slot.exp_progress));
    write_le_u32(packet, 0x19, static_cast<u32>(slot.hp));
    write_le_u32(packet, 0x1d, static_cast<u32>(slot.mp));
    write_le_u32(packet, 0x21, static_cast<u32>(slot.op));
    write_le_u32(packet, 0x25, static_cast<u32>(slot.dp));
    queue_packet(state, packet);
    SelectAvatarSlot(state, state.selected_avatar_slot);
}

void close_avatar_window(AvatarWindowState& state, HWND hwnd) {
    SetRankerMainWindowFrontendRouteWindow(state.parent_window);
    if (hwnd != nullptr) {
        DestroyWindow(hwnd);
    }
    if (state.callbacks.return_to_online_lobby != nullptr) {
        state.callbacks.return_to_online_lobby(state);
    }
}

} // namespace

AvatarWindowState& avatar_window_state() {
    return g_avatar_window_state;
}

std::string BuildAvatarItemStatText(const AvatarItemDefinition& item) {
    return item_stat_text(item);
}

std::string BuildAvatarCatalogStatText(i32 hp, i32 mp, i32 op, i32 dp) {
    char line[96]{};
    std::string out;
    auto append = [&out, &line](const char* label, i32 value) {
        if (value <= 0) {
            return;
        }
        std::snprintf(line, sizeof(line), "%s%s:%d", out.empty() ? "" : ",",
            label, value);
        out += line;
    };
    append("HP", hp);
    append("MP", mp);
    append("OP", op);
    append("DP", dp);
    return out;
}

void UpdateAvatarJemText(AvatarWindowState& state) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%d", state.jem);
    set_text(state.jem_edit.window, text);
}

void UpdateAvatarExpPointText(AvatarWindowState& state) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%d", state.exp_points);
    set_text(state.exp_point_edit.window, text);
}

int CalculateAvatarNextExpThreshold(AvatarWindowState& state,
    const AvatarOwnedSlot& slot) {
    AvatarDefinitionStats stats = lookup_avatar_stats(state, slot.avatar_id);
    const int next = stats.next_exp_base + slot.level * stats.next_exp_per_level;
    return std::max(1, next);
}

bool CheckAvatarItemCompatibleWithSelectedAvatar(AvatarWindowState& state,
    int item_id) {
    if (state.callbacks.item_compatible_with_avatar != nullptr) {
        return state.callbacks.item_compatible_with_avatar(
            state, state.selected_avatar_slot, item_id);
    }
    return true;
}

i32 GetAvatarCatalogVersion(const AvatarWindowState& state) {
    return state.avatar_catalog_version;
}

i32 GetAvatarItemCatalogVersion(const AvatarWindowState& state) {
    return state.item_catalog_version;
}

void ParseAvatarCatalogPacket(AvatarWindowState& state, const u8* payload,
    i32 byte_count) {
    parse_avatar_catalog_packet(state, payload, byte_count);
}

void ParseAvatarItemOfferPacket(AvatarWindowState& state, const u8* payload,
    i32 byte_count) {
    parse_item_offer_packet(state, payload, byte_count);
}

AvatarEquipmentRuntimeVector& ConstructAvatarEquipmentRuntimeVector(
    AvatarEquipmentRuntimeVector& vector) {
    vector.vtable_marker = nullptr;
    vector.primary_storage = nullptr;
    vector.secondary_storage = nullptr;
    vector.primary_count = 0;
    vector.secondary_count = 0;
    vector.primary_capacity = 0;
    vector.secondary_capacity = 0;
    return vector;
}

void DestroyAvatarEquipmentRuntimeVector(AvatarEquipmentRuntimeVector& vector) {
    if (vector.primary_storage != nullptr) {
        std::free(vector.primary_storage);
        vector.primary_storage = nullptr;
    }
    if (vector.secondary_storage != nullptr) {
        std::free(vector.secondary_storage);
        vector.secondary_storage = nullptr;
    }
    vector.primary_count = 0;
    vector.secondary_count = 0;
    vector.primary_capacity = 0;
    vector.secondary_capacity = 0;
}

void DeleteAvatarEquipmentRuntimeVector(AvatarEquipmentRuntimeVector* vector,
    bool free_storage) {
    if (vector != nullptr) {
        DestroyAvatarEquipmentRuntimeVector(*vector);
    }
    if (free_storage) {
        ::operator delete(vector);
    }
}

#define DEFINE_AVATAR_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(AvatarWindowState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(AvatarWindowState& state) { \
        InitializeLegacyImageButtonControl(Member); \
    } \
    void RegisterName(AvatarWindowState&) { \
        register_atexit_once(g_avatar_button_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(AvatarWindowState& state) { \
        DestroyLegacyImageButtonControl(Member); \
    }

DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarDeleteButtonStatic,
    InitializeAvatarDeleteButton,
    RegisterAvatarDeleteButtonDestructor,
    DestroyAvatarDeleteButton, state.delete_button, 0,
    shutdown_avatar_delete_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarBuyButtonStatic,
    InitializeAvatarBuyButton,
    RegisterAvatarBuyButtonDestructor,
    DestroyAvatarBuyButton, state.buy_button, 1, shutdown_avatar_buy_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarSellButtonStatic,
    InitializeAvatarSellButton,
    RegisterAvatarSellButtonDestructor,
    DestroyAvatarSellButton, state.sell_button, 2, shutdown_avatar_sell_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarEquipButtonStatic,
    InitializeAvatarEquipButton,
    RegisterAvatarEquipButtonDestructor,
    DestroyAvatarEquipButton, state.equip_button, 3, shutdown_avatar_equip_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarUnequipButtonStatic,
    InitializeAvatarUnequipButton,
    RegisterAvatarUnequipButtonDestructor,
    DestroyAvatarUnequipButton, state.unequip_button, 4,
    shutdown_avatar_unequip_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarCloseButtonStatic,
    InitializeAvatarCloseButton,
    RegisterAvatarCloseButtonDestructor,
    DestroyAvatarCloseButton, state.close_button, 5, shutdown_avatar_close_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarExpUpButtonStatic,
    InitializeAvatarExpUpButton,
    RegisterAvatarExpUpButtonDestructor,
    DestroyAvatarExpUpButton, state.exp_up_button, 6,
    shutdown_avatar_exp_up_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarInfoPanelButtonStatic,
    InitializeAvatarInfoPanelButton,
    RegisterAvatarInfoPanelButtonDestructor,
    DestroyAvatarInfoPanelButton, state.avatar_info_panel, 7,
    shutdown_avatar_info_panel_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarItemInfoPanelButtonStatic,
    InitializeAvatarItemInfoPanelButton,
    RegisterAvatarItemInfoPanelButtonDestructor,
    DestroyAvatarItemInfoPanelButton, state.item_info_panel, 8,
    shutdown_avatar_item_info_panel_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarTabBackgroundButtonStatic,
    InitializeAvatarTabBackgroundButton,
    RegisterAvatarTabBackgroundButtonDestructor,
    DestroyAvatarTabBackgroundButton, state.tab_background_button, 9,
    shutdown_avatar_tab_background_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarAvatarTabButtonStatic,
    InitializeAvatarAvatarTabButton,
    RegisterAvatarAvatarTabButtonDestructor,
    DestroyAvatarAvatarTabButton, state.avatar_tab_button, 10,
    shutdown_avatar_avatar_tab_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarWeaponTabButtonStatic,
    InitializeAvatarWeaponTabButton,
    RegisterAvatarWeaponTabButtonDestructor,
    DestroyAvatarWeaponTabButton, state.weapon_tab_button, 11,
    shutdown_avatar_weapon_tab_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarArmorTabButtonStatic,
    InitializeAvatarArmorTabButton,
    RegisterAvatarArmorTabButtonDestructor,
    DestroyAvatarArmorTabButton, state.armor_tab_button, 12,
    shutdown_avatar_armor_tab_button)
DEFINE_AVATAR_BUTTON_LIFETIME(InitializeAvatarItemTabButtonStatic,
    InitializeAvatarItemTabButton,
    RegisterAvatarItemTabButtonDestructor,
    DestroyAvatarItemTabButton, state.item_tab_button, 13,
    shutdown_avatar_item_tab_button)

#undef DEFINE_AVATAR_BUTTON_LIFETIME

#define DEFINE_AVATAR_BUTTON_ARRAY_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(AvatarWindowState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(AvatarWindowState& state) { \
        for (LegacyImageButtonControl& button : Member) { \
            InitializeLegacyImageButtonControl(button); \
        } \
    } \
    void RegisterName(AvatarWindowState&) { \
        register_atexit_once(g_avatar_button_array_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(AvatarWindowState& state) { \
        for (LegacyImageButtonControl& button : Member) { \
            DestroyLegacyImageButtonControl(button); \
        } \
    }

DEFINE_AVATAR_BUTTON_ARRAY_LIFETIME(InitializeAvatarSlotButtonArrayStatic,
    InitializeAvatarSlotButtonArray,
    RegisterAvatarSlotButtonArrayDestructor,
    DestroyAvatarSlotButtonArray, state.avatar_slots, 0,
    shutdown_avatar_slot_button_array)
DEFINE_AVATAR_BUTTON_ARRAY_LIFETIME(
    InitializeAvatarEquipmentSlotButtonArrayStatic,
    InitializeAvatarEquipmentSlotButtonArray,
    RegisterAvatarEquipmentSlotButtonArrayDestructor,
    DestroyAvatarEquipmentSlotButtonArray, state.equipment_slots, 1,
    shutdown_avatar_equipment_slot_button_array)
DEFINE_AVATAR_BUTTON_ARRAY_LIFETIME(
    InitializeAvatarInventorySlotButtonArrayStatic,
    InitializeAvatarInventorySlotButtonArray,
    RegisterAvatarInventorySlotButtonArrayDestructor,
    DestroyAvatarInventorySlotButtonArray, state.inventory_slots, 2,
    shutdown_avatar_inventory_slot_button_array)

#undef DEFINE_AVATAR_BUTTON_ARRAY_LIFETIME

void InitializeAvatarListScrollStatic(AvatarWindowState& state) {
    InitializeAvatarListScroll(state);
    RegisterAvatarListScrollDestructor(state);
}

void InitializeAvatarListScroll(AvatarWindowState& state) {
    InitializeLegacyCustomScrollControl(state.scroll_bar);
}

void RegisterAvatarListScrollDestructor(AvatarWindowState&) {
    register_atexit_once(g_avatar_scroll_destructor_registered,
        shutdown_avatar_list_scroll);
}

void DestroyAvatarListScroll(AvatarWindowState& state) {
    DestroyLegacyCustomScrollControl(state.scroll_bar);
}

#define DEFINE_AVATAR_BITMAP_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(AvatarWindowState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(AvatarWindowState& state) { \
        InitializeBitmapMemoryResource(Member); \
    } \
    void RegisterName(AvatarWindowState&) { \
        register_atexit_once(g_avatar_bitmap_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(AvatarWindowState& state) { \
        HandleBitmapMemoryResourceDestructor(Member); \
    }

DEFINE_AVATAR_BITMAP_LIFETIME(InitializeAvatarActiveTabStatic,
    InitializeAvatarActiveTab,
    RegisterAvatarActiveTabDestructor,
    DestroyAvatarActiveTab, state.active_tab_bitmap, 0,
    shutdown_avatar_active_tab)
DEFINE_AVATAR_BITMAP_LIFETIME(InitializeAvatarSelectedSlotFrameStatic,
    InitializeAvatarSelectedSlotFrame,
    RegisterAvatarSelectedSlotFrameDestructor,
    DestroyAvatarSelectedSlotFrame, state.selected_slot_bitmap, 1,
    shutdown_avatar_selected_slot_frame)
DEFINE_AVATAR_BITMAP_LIFETIME(InitializeAvatarNormalSlotFrameStatic,
    InitializeAvatarNormalSlotFrame,
    RegisterAvatarNormalSlotFrameDestructor,
    DestroyAvatarNormalSlotFrame, state.normal_slot_bitmap, 2,
    shutdown_avatar_normal_slot_frame)

#undef DEFINE_AVATAR_BITMAP_LIFETIME

void InitializeAvatarIconStripStatic(AvatarWindowState& state) {
    InitializeAvatarIconStrip(state);
    RegisterAvatarIconStripDestructor(state);
}

void InitializeAvatarIconStrip(AvatarWindowState& state) {
    InitializeRawIndexedBitmapStrip(state.avatar_strip);
}

void RegisterAvatarIconStripDestructor(AvatarWindowState&) {
    register_atexit_once(g_avatar_strip_destructor_registered[0],
        shutdown_avatar_icon_strip);
}

void DestroyAvatarIconStrip(AvatarWindowState& state) {
    HandleRawIndexedBitmapStripDestructor(state.avatar_strip);
}

void InitializeAvatarItemIconStripStatic(AvatarWindowState& state) {
    InitializeAvatarItemIconStrip(state);
    RegisterAvatarItemIconStripDestructor(state);
}

void InitializeAvatarItemIconStrip(AvatarWindowState& state) {
    InitializeRawIndexedBitmapStrip(state.item_strip);
}

void RegisterAvatarItemIconStripDestructor(AvatarWindowState&) {
    register_atexit_once(g_avatar_strip_destructor_registered[1],
        shutdown_avatar_item_icon_strip);
}

void DestroyAvatarItemIconStrip(AvatarWindowState& state) {
    HandleSecondaryRawIndexedBitmapStripDestructor(state.item_strip);
}

void InitializeAvatarTextTableStatic(AvatarWindowState& state) {
    InitializeAvatarTextTable(state);
    RegisterAvatarTextTableDestructor(state);
}

void InitializeAvatarTextTable(AvatarWindowState&) {
    InitializeIndexedTextTableContext(g_avatar_text_table);
}

void RegisterAvatarTextTableDestructor(AvatarWindowState&) {
    register_atexit_once(g_avatar_text_table_destructor_registered,
        shutdown_avatar_text_table);
}

void DestroyAvatarTextTable(AvatarWindowState&) {
    DestroyIndexedTextTableContext(g_avatar_text_table);
}

void InitializeAvatarEquipmentRuntimeStatic(AvatarWindowState& state) {
    InitializeAvatarEquipmentRuntime(state);
    RegisterAvatarEquipmentRuntimeDestructor(state);
}

void InitializeAvatarEquipmentRuntime(AvatarWindowState&) {
    ConstructAvatarEquipmentRuntimeVector(g_avatar_equipment_runtime);
}

void RegisterAvatarEquipmentRuntimeDestructor(AvatarWindowState&) {
    register_atexit_once(g_avatar_equipment_runtime_destructor_registered,
        shutdown_avatar_equipment_runtime);
}

void DestroyAvatarEquipmentRuntime(AvatarWindowState&) {
    DestroyAvatarEquipmentRuntimeVector(g_avatar_equipment_runtime);
}

void InitializeAvatarWindowResources(AvatarWindowState& state) {
    InitializeBitmapMemoryResource(state.background);
    InitializeAvatarDeleteButtonStatic(state);
    InitializeAvatarBuyButtonStatic(state);
    InitializeAvatarSellButtonStatic(state);
    InitializeAvatarEquipButtonStatic(state);
    InitializeAvatarUnequipButtonStatic(state);
    InitializeAvatarCloseButtonStatic(state);
    InitializeAvatarExpUpButtonStatic(state);
    InitializeAvatarInfoPanelButtonStatic(state);
    InitializeAvatarItemInfoPanelButtonStatic(state);
    InitializeAvatarSlotButtonArrayStatic(state);
    InitializeAvatarEquipmentSlotButtonArrayStatic(state);
    InitializeAvatarInventorySlotButtonArrayStatic(state);
    InitializeAvatarTabBackgroundButtonStatic(state);
    InitializeAvatarAvatarTabButtonStatic(state);
    InitializeAvatarWeaponTabButtonStatic(state);
    InitializeAvatarArmorTabButtonStatic(state);
    InitializeAvatarItemTabButtonStatic(state);
    InitializeAvatarListScrollStatic(state);
    InitializeAvatarActiveTabStatic(state);
    InitializeAvatarSelectedSlotFrameStatic(state);
    InitializeAvatarNormalSlotFrameStatic(state);
    InitializeAvatarIconStripStatic(state);
    InitializeAvatarItemIconStripStatic(state);
    InitializeAvatarTextTableStatic(state);
    InitializeAvatarEquipmentRuntimeStatic(state);
    for (AvatarOwnedSlot& slot : state.owned_avatars) {
        slot.avatar_id = -1;
        slot.equipment.fill(0);
    }
    state.inventory.fill(0);
}

void ReleaseAvatarWindowResources(AvatarWindowState& state) {
    release_window_resources(state);
}

void InstallAvatarWindowAccelerators(AvatarWindowState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kAvatarAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreAvatarWindowAccelerators(AvatarWindowState& state) {
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

bool LoadAvatarItemDefinitionsFromJw210Trc(AvatarWindowState& state,
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
    const u32 version = read_le_u32(header.data());
    const u32 count = read_le_u32(header.data() + 4);
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
        AvatarItemDefinition item{};
        item.item_id = static_cast<i32>(i);
        item.name = fixed_string(record, 0x40);
        if (std::string text = startup_item_name(item.item_id); !text.empty()) {
            item.name = std::move(text);
        }
        item.category = read_le_i32(record + 0x84);
        item.icon_frame = read_le_i32(record + 0x88);
        item.mode = read_le_i32(record + 0x208);
        item.max_hp = read_le_i32(record + 0x210);
        item.max_mp = read_le_i32(record + 0x214);
        item.hp = read_le_i32(record + 0x218);
        item.mp = read_le_i32(record + 0x21c);
        item.op = read_le_i32(record + 0x220);
        item.dp = read_le_i32(record + 0x224);
        item.attack = read_le_i32(record + 0x22c);
        item.defense = read_le_i32(record + 0x230);
        item.range = read_le_i32(record + 0x234);
        item.movement = read_le_i32(record + 0x238);
        item.detect = read_le_i32(record + 0x244);
        item.cloak = read_le_i32(record + 0x248);
        item.exp_bonus = read_le_i32(record + 0x24c);
        item.level_bonus = read_le_i32(record + 0x250);
        item.owner_resource = read_le_i32(record + 0x254);
        item.command_value = read_le_i32(record + 0x258);
        item.fallback_price = read_le_i32(record + 0x20c);
        state.item_definitions.push_back(std::move(item));
    }
    return true;
}

bool CreateAvatarWindow(AvatarWindowState& state, HWND parent, HINSTANCE instance,
    LegacyAsyncTcpSocket* async_tcp_socket) {
    if (state.window != nullptr) {
        return false;
    }

    InitializeAvatarWindowResources(state);
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.async_tcp_socket = async_tcp_socket;
    LoadAvatarItemDefinitionsFromJw210Trc(state);
    load_icon_strips(state);

    state.layout.clear();
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kAvatarLayoutTrcRecord)) {
        release_window_resources(state);
        return false;
    }
    state.layout = copy_layout_record(layout.table);
    state.selected_avatar_slot = 0;
    state.selected_equipment_slot = 0;
    state.selected_inventory_slot = 0;
    state.current_item_id = 0;
    state.current_tab = 0;

    const DWORD style =
        IsWindow(parent) && GetWindowLongPtrA(parent, GWL_STYLE) != 0 ?
        kWindowStyleWindowed : kWindowStyleFullscreen;
    const AvatarLayoutRect window_rect = layout_at(state, 0);
    const POINT origin = RankerFrontendWindowOrigin();
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Avatar", "Avatar",
        style, origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        release_window_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(avatar_window_proc));

    if (!create_text_control(state.jem_edit, state.window, instance, "edit",
            kReadOnlyEditStyle, kAvatarJemEditId, layout_at(state, 17)) ||
        !create_text_control(state.exp_point_edit, state.window, instance, "edit",
            kReadOnlyEditStyle, kAvatarExpPointEditId, layout_at(state, 16)) ||
        !create_text_control(state.avatar_name_edit, state.window, instance, "edit",
            kReadOnlyEditStyle, kAvatarNameEditId, layout_at(state, 18)) ||
        !create_text_control(state.buy_name_edit, state.window, instance, "edit",
            kEditStyle, kAvatarBuyNameEditId, layout_at(state, 43)) ||
        !create_text_control(state.list_box, state.window, instance, "listbox",
            kListStyle, kAvatarListBoxId, layout_at(state, 41))) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }

    SendMessageA(state.jem_edit.window, EM_LIMITTEXT, 10, 0);
    SendMessageA(state.avatar_name_edit.window, EM_LIMITTEXT, 0x13, 0);
    SendMessageA(state.buy_name_edit.window, EM_LIMITTEXT, 0x13, 0);

    const AvatarLayoutRect scroll_rect = layout_at(state, 42);
    if (!CreateLegacyCustomScrollControlWindow(state.scroll_bar, state.window,
            "AvatarScroll", reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kAvatarScrollBarId)), false,
            scroll_rect.x, scroll_rect.y, scroll_rect.width, scroll_rect.height)) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }
    LoadLegacyCustomScrollControlBitmaps(state.scroll_bar,
        kAvatarScrollUpBitmapRecord, 0, kAvatarScrollDownBitmapRecord, 0,
        kAvatarScrollThumbBitmapRecord, kAvatarScrollTrackBitmapRecord);
    subclass_window(state.scroll_bar.window);

    for (const AvatarButtonSpec& spec : kButtonSpecs) {
        if (!create_button_from_spec(state, spec)) {
            DestroyWindow(state.window);
            state.window = nullptr;
            return false;
        }
    }

    for (int i = 0; i < kAvatarSlotCount; ++i) {
        if (!create_image_button(state.avatar_slots[static_cast<std::size_t>(i)],
                state.window, "AvataSlot", kAvatarSlotFirstButtonId + i,
                layout_at(state, 7 + i), 0, 0)) {
            DestroyWindow(state.window);
            state.window = nullptr;
            return false;
        }
    }
    for (int i = 0; i < kAvatarEquipmentSlotCount; ++i) {
        if (!create_image_button(state.equipment_slots[static_cast<std::size_t>(i)],
                state.window, "AvataItemSlot",
                kAvatarEquipmentSlotFirstButtonId + i, layout_at(state, 20 + i),
                0, 0)) {
            DestroyWindow(state.window);
            state.window = nullptr;
            return false;
        }
    }
    for (int i = 0; i < kAvatarInventorySlotCount; ++i) {
        if (!create_image_button(state.inventory_slots[static_cast<std::size_t>(i)],
                state.window, "AvataItemSlot", kAvatarInventorySlotFirstButtonId + i,
                layout_at(state, 27 + i), 0, 0)) {
            DestroyWindow(state.window);
            state.window = nullptr;
            return false;
        }
    }

    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.jem_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.exp_point_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.avatar_name_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.buy_name_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.list_box.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    LoadAvatarTextTableFromJw219Trc(state);
    InstallAvatarWindowAccelerators(state);

    LoadBitmapMemoryResourceFromTrcRecord(state.background, kAvatarArchiveName,
        kAvatarBackgroundBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.selected_slot_bitmap,
        kAvatarArchiveName, kAvatarSelectedSlotBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.normal_slot_bitmap,
        kAvatarArchiveName, kAvatarNormalSlotBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.active_tab_bitmap,
        kAvatarArchiveName, kAvatarAvatarTabBitmapRecord);

    SetRankerMainWindowFrontendRouteWindow(state.window);
    update_currency_text(state);
    set_text(state.buy_name_edit.window, " ");
    SelectAvatarSlot(state, state.selected_avatar_slot);
    PopulateAvatarList(state, 0);
    queue_avatar_catalog_request(state);

    ShowWindow(state.window, SW_SHOW);
    SetFocus(state.buy_name_edit.window);
    state.visible = true;
    return true;
}

void PopulateAvatarList(AvatarWindowState& state, i32 tab) {
    state.current_tab = tab;
    if (state.list_box.window == nullptr) {
        return;
    }
    SendMessageA(state.list_box.window, LB_RESETCONTENT, 0, 0);
    int count = 0;
    if (tab == 0) {
        if (state.avatar_catalog.empty()) {
            for (i32 avatar_id : state.purchasable_avatar_ids) {
                AvatarCatalogEntry entry{};
                entry.avatar_id = avatar_id;
                fill_avatar_catalog_stats(state, entry);
                state.avatar_catalog.push_back(std::move(entry));
            }
        }
        for (std::size_t i = 0; i < state.avatar_catalog.size(); ++i) {
            AvatarCatalogEntry& entry = state.avatar_catalog[i];
            if (entry.display_name.empty()) {
                entry.display_name = avatar_catalog_name(state, entry.avatar_id);
            }
            LRESULT row = SendMessageA(state.list_box.window, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(entry.display_name.c_str()));
            if (row != LB_ERR) {
                SendMessageA(state.list_box.window, LB_SETITEMDATA,
                    static_cast<WPARAM>(row), static_cast<LPARAM>(i));
                ++count;
            }
        }
    } else {
        for (const AvatarItemDefinition& item : state.item_definitions) {
            if (item.item_id <= 0 || !item_visible_on_tab(state, item, tab)) {
                continue;
            }
            LRESULT row = SendMessageA(state.list_box.window, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(item.name.c_str()));
            if (row != LB_ERR) {
                SendMessageA(state.list_box.window, LB_SETITEMDATA,
                    static_cast<WPARAM>(row), static_cast<LPARAM>(item.item_id));
                ++count;
            }
        }
    }
    sync_scroll_bar(state, count);
    set_list_top_from_scroll(state);
    redraw_window(state.list_box.window);
}

void SelectAvatarSlot(AvatarWindowState& state, i32 slot) {
    state.selected_avatar_slot = std::clamp(slot, 0, kAvatarSlotCount - 1);
    AvatarOwnedSlot& avatar =
        state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
    if (avatar.avatar_id < 1) {
        set_text(state.avatar_name_edit.window, " ");
        ShowWindow(state.delete_button.window, SW_HIDE);
    } else {
        set_text(state.avatar_name_edit.window, avatar.name.data());
        ShowWindow(state.delete_button.window, SW_SHOW);
    }
    redraw_avatar_slots(state);
    SelectAvatarEquipmentSlot(state, state.selected_equipment_slot);
}

void SelectAvatarEquipmentSlot(AvatarWindowState& state, i32 slot) {
    state.selected_equipment_slot =
        std::clamp(slot, 0, kAvatarEquipmentSlotCount - 1);
    const AvatarOwnedSlot& avatar =
        state.owned_avatars[static_cast<std::size_t>(state.selected_avatar_slot)];
    SetAvatarCurrentItem(state,
        avatar.equipment[static_cast<std::size_t>(state.selected_equipment_slot)]);
    redraw_equipment_slots(state);
}

void SelectAvatarInventorySlot(AvatarWindowState& state, i32 slot) {
    state.selected_inventory_slot =
        std::clamp(slot, 0, kAvatarInventorySlotCount - 1);
    SetAvatarCurrentItem(state,
        state.inventory[static_cast<std::size_t>(state.selected_inventory_slot)]);
    redraw_inventory_slots(state);
}

void SetAvatarCurrentItem(AvatarWindowState& state, i32 item_id) {
    state.current_item_id = item_id;
    redraw_window(state.item_info_panel.window);
}

void DispatchAvatarNetworkMessage(AvatarWindowState& state, WPARAM wparam,
    LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        show_avatar_message(state,
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
    if (state.callbacks.receive_payload != nullptr) {
        i32 byte_count = 0;
        const u8* payload = nullptr;
        payload = state.callbacks.receive_payload(state, byte_count);
        if (payload == nullptr || byte_count < 0x0d) {
            return;
        }
        const u32 opcode = read_u32(payload, byte_count, 4);
        switch (opcode) {
        case 0x4a:
            ParseAvatarItemOfferPacket(state, payload, byte_count);
            PopulateAvatarList(state, state.current_tab);
            break;
        case 0x4b:
            ParseAvatarCatalogPacket(state, payload, byte_count);
            PopulateAvatarList(state, 0);
            break;
        case 0x5e:
            state.exp_points = read_i32(payload, byte_count, 0x0d);
            state.jem = read_i32(payload, byte_count, 0x11);
            update_currency_text(state);
            break;
        default:
            forward_avatar_network_message(state, wparam, lparam);
            break;
        }
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
        const u32 opcode = read_u32(payload, packet_count, 4);
        bool handled = true;
        switch (opcode) {
        case 0x4a:
            ParseAvatarItemOfferPacket(state, payload, packet_count);
            PopulateAvatarList(state, state.current_tab);
            break;
        case 0x4b:
            ParseAvatarCatalogPacket(state, payload, packet_count);
            PopulateAvatarList(state, 0);
            break;
        case 0x5e:
            state.exp_points = read_i32(payload, packet_count, 0x0d);
            state.jem = read_i32(payload, packet_count, 0x11);
            update_currency_text(state);
            break;
        default:
            handled = false;
            forward_avatar_network_message(state, wparam, lparam);
            break;
        }
        if (!handled) {
            return;
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket, packet_count);
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

LRESULT HandleAvatarWindowMessage(AvatarWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        release_window_resources(state);
        return 0;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAvatarYellow);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAvatarBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAvatarWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAvatarBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAvatarSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAvatarBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAvatarSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAvatarBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        const int id = static_cast<int>(draw->CtlID);
        if (id == kAvatarInfoPanelId) {
            draw_avatar_info_panel(state, *draw);
            break;
        }
        if (id == kAvatarItemInfoPanelId) {
            draw_item_info_panel(state, *draw);
            break;
        }
        if (id == kAvatarListBoxId) {
            draw_list_row(state, *draw);
            return TRUE;
        }
        if (id >= kAvatarSlotFirstButtonId &&
            id < kAvatarSlotFirstButtonId + kAvatarSlotCount) {
            draw_avatar_slot(state, *draw, id - kAvatarSlotFirstButtonId);
            break;
        }
        if (id >= kAvatarEquipmentSlotFirstButtonId &&
            id < kAvatarEquipmentSlotFirstButtonId + kAvatarEquipmentSlotCount) {
            draw_item_slot(state, *draw, id - kAvatarEquipmentSlotFirstButtonId, true);
            break;
        }
        if (id >= kAvatarInventorySlotFirstButtonId &&
            id < kAvatarInventorySlotFirstButtonId + kAvatarInventorySlotCount) {
            draw_item_slot(state, *draw, id - kAvatarInventorySlotFirstButtonId, false);
            break;
        }
        if (id == kAvatarTabBackgroundButtonId) {
            StretchBitmapMemoryResourceToDc(state.active_tab_bitmap, draw->hDC, 0, 0);
            break;
        }
        if (LegacyImageButtonControl* button = button_for_id(state, id)) {
            DrawLegacyImageButtonItem(*button, *draw);
            break;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        if (id == kAvatarDeleteButtonId) {
            play_click_sound(state);
            AvatarOwnedSlot& avatar = state.owned_avatars[
                static_cast<std::size_t>(state.selected_avatar_slot)];
            if (avatar.avatar_id >= 0) {
                char text[128]{};
                std::snprintf(text, sizeof(text),
                    startup_message_row(221, "Delete %s (Lev %d)?"),
                    avatar.name.data(), avatar.level + 1);
                CreateOnlineModelessPrompt(online_modeless_prompt_state(), hwnd,
                    state.instance, text, kAvatarSoftWhite, true,
                    kAvatarDeleteButtonId, 0);
            }
        }
        else if (id == kAvatarBuyButtonId) {
            play_click_sound(state);
            handle_buy_command(state);
        }
        else if (id == kAvatarSellButtonId) {
            play_click_sound(state);
            handle_sell_command(state);
        }
        else if (id == kAvatarEquipButtonId) {
            play_click_sound(state);
            handle_equip_command(state);
        }
        else if (id == kAvatarUnequipButtonId) {
            play_click_sound(state);
            handle_unequip_command(state);
        }
        else if (id == kAvatarCloseButtonId) {
            play_click_sound(state);
            close_avatar_window(state, hwnd);
        }
        else if (id == kAvatarExpUpButtonId) {
            ApplyAvatarExperiencePoint(state);
        }
        else if (id == kAvatarForwardFocusCommandId) {
            HWND focus = GetFocus();
            const int focus_id = focus == nullptr ? 0 :
                static_cast<int>(GetWindowLongPtrA(focus, GWLP_ID));
            if (focus_id == kAvatarNameEditId) {
                SetFocus(state.buy_name_edit.window);
                return 0;
            }
            ApplyAvatarExperiencePoint(state);
        }
        else if (id == kAvatarAvatarTabButtonId) {
            play_click_sound(state);
            LoadBitmapMemoryResourceFromTrcRecord(state.active_tab_bitmap,
                kAvatarArchiveName, kAvatarAvatarTabBitmapRecord);
            redraw_window(state.tab_background_button.window);
            PopulateAvatarList(state, 0);
        }
        else if (id == kAvatarWeaponTabButtonId) {
            play_click_sound(state);
            LoadBitmapMemoryResourceFromTrcRecord(state.active_tab_bitmap,
                kAvatarArchiveName, kAvatarWeaponTabBitmapRecord);
            redraw_window(state.tab_background_button.window);
            PopulateAvatarList(state, 1);
        }
        else if (id == kAvatarArmorTabButtonId) {
            play_click_sound(state);
            LoadBitmapMemoryResourceFromTrcRecord(state.active_tab_bitmap,
                kAvatarArchiveName, kAvatarArmorTabBitmapRecord);
            redraw_window(state.tab_background_button.window);
            PopulateAvatarList(state, 2);
        }
        else if (id == kAvatarItemTabButtonId) {
            play_click_sound(state);
            LoadBitmapMemoryResourceFromTrcRecord(state.active_tab_bitmap,
                kAvatarArchiveName, kAvatarItemTabBitmapRecord);
            redraw_window(state.tab_background_button.window);
            PopulateAvatarList(state, 3);
        }
        else if (id == kAvatarListBoxId &&
            notify == LBN_SELCHANGE && state.current_tab != 0) {
            play_click_sound(state);
            const int item_id = selected_list_item_data(state);
            if (item_id >= 0) {
                SetAvatarCurrentItem(state, item_id);
            }
        }
        else if (id >= kAvatarSlotFirstButtonId &&
            id < kAvatarSlotFirstButtonId + kAvatarSlotCount) {
            play_click_sound(state);
            SelectAvatarSlot(state, id - kAvatarSlotFirstButtonId);
        }
        else if (id >= kAvatarEquipmentSlotFirstButtonId &&
            id < kAvatarEquipmentSlotFirstButtonId + kAvatarEquipmentSlotCount) {
            play_click_sound(state);
            SelectAvatarEquipmentSlot(state, id - kAvatarEquipmentSlotFirstButtonId);
        }
        else if (id >= kAvatarInventorySlotFirstButtonId &&
            id < kAvatarInventorySlotFirstButtonId + kAvatarInventorySlotCount) {
            play_click_sound(state);
            SelectAvatarInventorySlot(state, id - kAvatarInventorySlotFirstButtonId);
        }
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    }
    case kAvatarNetworkMessage:
        DispatchAvatarNetworkMessage(state, wparam, lparam);
        break;
    case kAvatarDeleteAcceptMessage:
        if (wparam == kAvatarDeleteButtonId) {
            handle_delete_accept(state);
        }
        break;
    case kAvatarPromptMessage0:
        ShowOnlineModalPrompt0(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAvatarPromptMessage1:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAvatarPromptMessage2:
        ShowOnlineModalPrompt2(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAvatarPromptMessage3:
        ShowOnlineModalPrompt3(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAvatarPromptEndMessage:
        EndOnlineModalPrompt(online_modal_prompt_state(), static_cast<INT_PTR>(wparam));
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleAvatarControlMessage(AvatarWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (message == WM_PAINT && hwnd == state.scroll_bar.window) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.scroll_bar, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (id == kAvatarScrollBarId) {
        const bool handled =
            HandleLegacyCustomScrollControlMouseMessage(state.scroll_bar, message,
                wparam, lparam);
        if (handled) {
            set_list_top_from_scroll(state);
        }
    }

    constexpr int kAvatarUnusedControlId = kAvatarNameEditId + 1;
    if (id >= kAvatarDeleteButtonId && id <= kAvatarExpPointEditId &&
        id != kAvatarUnusedControlId && id != kAvatarTabBackgroundButtonId) {
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    }
    return 0;
}

}

#endif
