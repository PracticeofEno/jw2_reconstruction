#include "ranker_player_profile.h"

#ifdef _WIN32

#include "ranker_barter_window.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_icon_strips.h"
#include "ranker_online_lobby.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iterator>
#include <map>
#include <sstream>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed = 0x10cf0000;
constexpr DWORD kEditStyle = WS_CHILD;
constexpr DWORD kMultilineEditStyle = WS_CHILD | ES_MULTILINE | ES_WANTRETURN;
constexpr COLORREF kProfileWhite = RGB(255, 255, 255);
constexpr COLORREF kProfileSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kProfileBlack = RGB(0, 0, 0);
constexpr int kSelectorButtonWidth = 0x0f;
constexpr int kAvatarFrameWidth = 0x26;
constexpr int kAvatarFrameHeight = 0x26;
constexpr int kAvatarFrameCount = 64;

constexpr std::size_t kPayloadNameOffset = 0x0d;
constexpr std::size_t kPayloadGuildIconOffset = 0x2d;
constexpr std::size_t kPayloadGuildNameOffset = 0x31;
constexpr std::size_t kPayloadBirthYearOffset = 0x51;
constexpr std::size_t kPayloadSexOffset = 0x55;
constexpr std::size_t kPayloadLocationOffset = 0x59;
constexpr std::size_t kPayloadDescriptionOffset = 0x5d;
constexpr std::size_t kPayloadNormalMeleeOffset = 0x15f;
constexpr std::size_t kPayloadNormalRankOffset = 0x16b;
constexpr std::size_t kPayloadNormalPointsOffset = 0x16f;
constexpr std::size_t kPayloadNormalRankRecordOffset = 0x173;
constexpr std::size_t kPayloadAvatarMeleeOffset = 0x187;
constexpr std::size_t kPayloadAvatarRankOffset = 0x193;
constexpr std::size_t kPayloadAvatarPointsOffset = 0x197;
constexpr std::size_t kPayloadAvatarRankRecordOffset = 0x19b;
constexpr std::size_t kPayloadAvatarIdsOffset = 0x1a7;
constexpr std::size_t kPayloadAvatarLevelsOffset = 0x1c7;

PlayerProfileState g_player_profile_state;
bool g_background_destructor_registered = false;
std::array<bool, 5> g_button_destructor_registered{};
bool g_location_selector_destructor_registered = false;
bool g_avatar_button_vector_destructor_registered = false;
bool g_text_record_parser0_destructor_registered = false;
bool g_text_record_parser1_destructor_registered = false;
bool g_avatar_strip_destructor_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK player_profile_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandlePlayerProfileWindowMessage(g_player_profile_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK player_profile_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandlePlayerProfileControlMessage(g_player_profile_state, hwnd, message,
        wparam, lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_global_background() {
    DestroyPlayerProfileBackgroundBitmap(g_player_profile_state);
}

void shutdown_global_ok_button() {
    DestroyPlayerProfileOkButton(g_player_profile_state);
}

void shutdown_global_cancel_button() {
    DestroyPlayerProfileCancelButton(g_player_profile_state);
}

void shutdown_global_item_deal_button() {
    DestroyPlayerProfileItemDealButton(g_player_profile_state);
}

void shutdown_global_memo_button() {
    DestroyPlayerProfileMemoButton(g_player_profile_state);
}

void shutdown_global_guild_icon_button() {
    DestroyPlayerProfileGuildIconButton(g_player_profile_state);
}

void shutdown_global_location_selector() {
    DestroyPlayerProfileLocationSelector(g_player_profile_state);
}

void shutdown_global_avatar_button_vector() {
    DestroyPlayerProfileAvatarButtonVector(g_player_profile_state);
}

void shutdown_global_text_record_parser0() {
    DestroyPlayerProfileTextRecordParser0(g_player_profile_state);
}

void shutdown_global_text_record_parser1() {
    DestroyPlayerProfileTextRecordParser1(g_player_profile_state);
}

void shutdown_global_avatar_strip() {
    DestroyPlayerProfileAvatarStrip(g_player_profile_state);
}

std::string strip_comment(std::string line) {
    const std::size_t semicolon = line.find(';');
    if (semicolon != std::string::npos) {
        line.resize(semicolon);
    }
    return line;
}

std::map<int, std::string> read_keyed_string_record(u32 record) {
    std::vector<u8> bytes;
    std::map<int, std::string> values;
    if (!LoadTrcRecordAlloc("Jw2_19.trc", record, bytes, 1)) {
        return values;
    }

    std::istringstream input(reinterpret_cast<const char*>(bytes.data()));
    std::string line;
    while (std::getline(input, line)) {
        line = strip_comment(line);
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        char* end = nullptr;
        const long key = std::strtol(line.c_str(), &end, 10);
        if (end == line.c_str()) {
            continue;
        }

        const std::size_t first_quote = line.find('"', equals + 1);
        const std::size_t second_quote =
            first_quote == std::string::npos ? std::string::npos :
            line.find('"', first_quote + 1);
        if (first_quote == std::string::npos || second_quote == std::string::npos) {
            continue;
        }
        values[static_cast<int>(key)] =
            line.substr(first_quote + 1, second_quote - first_quote - 1);
    }
    return values;
}

std::vector<PlayerProfileLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<PlayerProfileLayoutRect> rects;
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

PlayerProfileLayoutRect layout_at(const PlayerProfileState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return PlayerProfileLayoutRect{};
}

i32 read_i32(const std::vector<u8>& payload, std::size_t offset) {
    if (offset + sizeof(i32) > payload.size()) {
        return 0;
    }
    i32 value = 0;
    std::memcpy(&value, payload.data() + offset, sizeof(value));
    return value;
}

void write_le32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset + sizeof(value) > buffer.size()) {
        return;
    }
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}

void copy_fixed_string(std::vector<u8>& buffer, std::size_t offset,
    std::size_t field_size, const char* text) {
    if (offset >= buffer.size() || field_size == 0) {
        return;
    }
    const std::size_t available = std::min(field_size, buffer.size() - offset);
    std::memset(buffer.data() + offset, 0, available);
    if (text != nullptr) {
        std::strncpy(reinterpret_cast<char*>(buffer.data() + offset), text,
            available - 1);
    }
}

template <std::size_t N>
void copy_payload_string(std::array<char, N>& out, const std::vector<u8>& payload,
    std::size_t offset) {
    out.fill(0);
    if (offset >= payload.size()) {
        return;
    }
    const std::size_t count = std::min<std::size_t>(N - 1, payload.size() - offset);
    std::memcpy(out.data(), payload.data() + offset, count);
    out[N - 1] = '\0';
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& target, const char* source) {
    target.fill(0);
    if (source != nullptr) {
        std::strncpy(target.data(), source, target.size() - 1);
    }
}

std::string upper_ascii(const char* text) {
    std::string out = text == nullptr ? "" : text;
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }
    return out;
}

void parse_payload(PlayerProfileState& state) {
    PlayerProfilePayload payload{};
    payload.guild_icon_slot = -1;
    payload.sex_index = -1;
    payload.location_index = -1;
    payload.normal_rank = -1;
    payload.avatar_rank = -1;
    payload.avatar_ids.fill(-1);
    payload.avatar_levels.fill(0);

    copy_payload_string(payload.name, state.raw_payload, kPayloadNameOffset);
    payload.guild_icon_slot = read_i32(state.raw_payload, kPayloadGuildIconOffset);
    copy_payload_string(payload.guild_name, state.raw_payload, kPayloadGuildNameOffset);
    payload.birth_year = read_i32(state.raw_payload, kPayloadBirthYearOffset);
    payload.sex_index = read_i32(state.raw_payload, kPayloadSexOffset);
    payload.location_index = read_i32(state.raw_payload, kPayloadLocationOffset);
    copy_payload_string(payload.description, state.raw_payload, kPayloadDescriptionOffset);
    for (int i = 0; i < 3; ++i) {
        payload.normal_melee[static_cast<std::size_t>(i)] =
            read_i32(state.raw_payload, kPayloadNormalMeleeOffset + i * 4);
        payload.normal_rank_record[static_cast<std::size_t>(i)] =
            read_i32(state.raw_payload, kPayloadNormalRankRecordOffset + i * 4);
        payload.avatar_melee[static_cast<std::size_t>(i)] =
            read_i32(state.raw_payload, kPayloadAvatarMeleeOffset + i * 4);
        payload.avatar_rank_record[static_cast<std::size_t>(i)] =
            read_i32(state.raw_payload, kPayloadAvatarRankRecordOffset + i * 4);
    }
    payload.normal_rank = read_i32(state.raw_payload, kPayloadNormalRankOffset);
    payload.normal_points = read_i32(state.raw_payload, kPayloadNormalPointsOffset);
    payload.avatar_rank = read_i32(state.raw_payload, kPayloadAvatarRankOffset);
    payload.avatar_points = read_i32(state.raw_payload, kPayloadAvatarPointsOffset);
    for (int i = 0; i < kPlayerProfileAvatarButtonCount; ++i) {
        payload.avatar_ids[static_cast<std::size_t>(i)] =
            read_i32(state.raw_payload, kPayloadAvatarIdsOffset + i * 4);
        payload.avatar_levels[static_cast<std::size_t>(i)] =
            read_i32(state.raw_payload, kPayloadAvatarLevelsOffset + i * 4);
    }

    state.profile = payload;
}

std::vector<std::string> sorted_values_or_fallback(
    const std::map<int, std::string>& values, const char* const* fallback,
    std::size_t fallback_count) {
    std::vector<std::string> items;
    if (!values.empty()) {
        items.reserve(values.size());
        for (const auto& entry : values) {
            items.push_back(entry.second);
        }
        return items;
    }
    items.reserve(fallback_count);
    for (std::size_t i = 0; i < fallback_count; ++i) {
        items.emplace_back(fallback[i]);
    }
    return items;
}

void load_text_tables(PlayerProfileState& state) {
    const char* location_fallback[] = {"Select"};
    state.location_items = sorted_values_or_fallback(
        read_keyed_string_record(kPlayerProfileLocationTextTrcRecord),
        location_fallback, std::size(location_fallback));

    const char* sex_fallback[] = {"Select", "M", "F"};
    state.sex_items = sorted_values_or_fallback(
        read_keyed_string_record(kPlayerProfileSexTextTrcRecord), sex_fallback,
        std::size(sex_fallback));

    const std::map<int, std::string> avatar_info =
        read_keyed_string_record(kPlayerProfileAvatarInfoTextTrcRecord);
    const auto level = avatar_info.find(14);
    if (level != avatar_info.end()) {
        state.avatar_level_label = level->second;
    }

    state.avatar_id_count = 0;
    state.avatar_id_list.fill(-1);
    const std::map<int, std::string> avatar_ids =
        read_keyed_string_record(kPlayerProfileAvatarIdTextTrcRecord);
    for (const auto& entry : avatar_ids) {
        if (state.avatar_id_count >= state.avatar_id_list.size()) {
            break;
        }
        state.avatar_id_list[state.avatar_id_count++] =
            static_cast<i32>(std::strtol(entry.second.c_str(), nullptr, 10));
    }
}

void add_selector_strings(LegacyStringSelectorControl& selector,
    const std::vector<std::string>& items) {
    for (const std::string& item : items) {
        AddLegacyStringSelectorText(selector, item.c_str());
    }
}

bool load_avatar_strip(PlayerProfileState& state) {
    return LoadAvatarIconStrip(state.avatar_strip);
}

void clear_control(PlayerProfileTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void destroy_control(PlayerProfileTextControl& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    clear_control(control);
}

void subclass_text_control(PlayerProfileTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(player_profile_control_proc));
}

bool create_text_control(PlayerProfileTextControl& control, HWND parent,
    HINSTANCE instance, DWORD style, int id, const PlayerProfileLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, "edit", nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_text_control(control);
    return true;
}

void subclass_button(LegacyImageButtonControl& button) {
    if (button.window == nullptr) {
        return;
    }
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(button.window, GWLP_WNDPROC));
    SetWindowLongPtrA(button.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(player_profile_control_proc));
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const PlayerProfileLayoutRect& rect, u32 normal_record,
    u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    subclass_button(button);
    return true;
}

bool create_location_selector(PlayerProfileState& state) {
    const PlayerProfileLayoutRect rect = layout_at(state, 13);
    if (!CreateLegacyStringSelectorWindow(state.location_selector, state.window,
            "Location", reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kPlayerProfileLocationSelectorId)),
            rect.x, rect.y, rect.width, rect.height, kSelectorButtonWidth)) {
        return false;
    }
    LoadLegacyStringSelectorIncrementButtonBitmaps(state.location_selector, 0x114, 0x114);
    LoadLegacyStringSelectorDecrementButtonBitmaps(state.location_selector, 0x115, 0x115);
    SetWindowLongPtrA(state.location_selector.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(player_profile_control_proc));
    add_selector_strings(state.location_selector, state.location_items);
    return true;
}

LegacyImageButtonControl* button_for_id(PlayerProfileState& state, int id) {
    switch (id) {
    case kPlayerProfileOkButtonId:
        return &state.ok_button;
    case kPlayerProfileCancelButtonId:
        return &state.cancel_button;
    case kPlayerProfileItemDealButtonId:
        return &state.item_deal_button;
    case kPlayerProfileMemoButtonId:
        return &state.memo_button;
    case kPlayerProfileGuildIconButtonId:
        return &state.guild_icon_button;
    default:
        if (id >= kPlayerProfileAvatarFirstButtonId &&
            id < kPlayerProfileAvatarFirstButtonId + kPlayerProfileAvatarButtonCount) {
            return &state.avatar_buttons[static_cast<std::size_t>(
                id - kPlayerProfileAvatarFirstButtonId)];
        }
        return nullptr;
    }
}

WNDPROC original_proc_for_id(PlayerProfileState& state, int id) {
    switch (id) {
    case kPlayerProfileNameEditId:
        return state.name_edit.original_window_proc;
    case kPlayerProfileSexEditId:
        return state.sex_edit.original_window_proc;
    case kPlayerProfileAgeEditId:
        return state.age_edit.original_window_proc;
    case kPlayerProfileNormalMeleeEditId:
        return state.normal_melee_edit.original_window_proc;
    case kPlayerProfileNormalRankEditId:
        return state.normal_rank_edit.original_window_proc;
    case kPlayerProfileDescriptionEditId:
        return state.description_edit.original_window_proc;
    case kPlayerProfileGuildNameEditId:
        return state.guild_name_edit.original_window_proc;
    case kPlayerProfileAvatarMeleeEditId:
        return state.avatar_melee_edit.original_window_proc;
    case kPlayerProfileAvatarRankEditId:
        return state.avatar_rank_edit.original_window_proc;
    case kPlayerProfileLocationSelectorId:
        return state.location_selector.original_window_proc;
    default:
        if (LegacyImageButtonControl* button = button_for_id(state, id)) {
            return button->original_window_proc;
        }
        return nullptr;
    }
}

PlayerProfileTextControl* text_control_for_id(PlayerProfileState& state, int id) {
    switch (id) {
    case kPlayerProfileNameEditId:
        return &state.name_edit;
    case kPlayerProfileSexEditId:
        return &state.sex_edit;
    case kPlayerProfileAgeEditId:
        return &state.age_edit;
    case kPlayerProfileNormalMeleeEditId:
        return &state.normal_melee_edit;
    case kPlayerProfileNormalRankEditId:
        return &state.normal_rank_edit;
    case kPlayerProfileDescriptionEditId:
        return &state.description_edit;
    case kPlayerProfileGuildNameEditId:
        return &state.guild_name_edit;
    case kPlayerProfileAvatarMeleeEditId:
        return &state.avatar_melee_edit;
    case kPlayerProfileAvatarRankEditId:
        return &state.avatar_rank_edit;
    default:
        return nullptr;
    }
}

void queue_packet(PlayerProfileState& state, const void* packet, i32 byte_count) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet, byte_count);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket,
            const_cast<void*>(packet), byte_count, nullptr);
    }
}

void play_click_sound(PlayerProfileState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

HWND find_parent_online_chat_edit(const PlayerProfileState& state) {
    for (HWND candidate = state.parent_window; candidate != nullptr;
         candidate = GetParent(candidate)) {
        if (!IsWindow(candidate)) {
            break;
        }
        HWND edit = GetDlgItem(candidate, kOnlineLobbyChatEditId);
        if (edit != nullptr && IsWindow(edit)) {
            return edit;
        }
    }
    return nullptr;
}

void focus_online_chat(PlayerProfileState& state) {
    if (state.callbacks.focus_online_chat != nullptr) {
        state.callbacks.focus_online_chat(state);
        return;
    }
    if (HWND edit = find_parent_online_chat_edit(state)) {
        SetFocus(edit);
    }
}

void set_text(HWND window, const char* text) {
    if (window != nullptr) {
        SetWindowTextA(window, text == nullptr ? "" : text);
    }
}

std::string format_record(const std::array<i32, 3>& values, i32 rank, i32 points) {
    char text[128]{};
    std::snprintf(text, sizeof(text), "%d-%d-%d", values[0], values[1], values[2]);
    std::string out{text};
    if (rank >= 0) {
        std::snprintf(text, sizeof(text), " (%d #%d)", points, rank);
        out += text;
    }
    return out;
}

int current_year() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    const std::tm* local_tm = std::localtime(&time);
    return local_tm == nullptr ? 1900 : local_tm->tm_year + 1900;
}

void enable_window_style(HWND window, bool enabled) {
    if (window == nullptr) {
        return;
    }
    EnableWindow(window, enabled ? TRUE : FALSE);
}

void apply_profile_to_controls(PlayerProfileState& state) {
    const PlayerProfilePayload& profile = state.profile;
    set_text(state.name_edit.window, profile.name.data());

    const int sex_display_index = profile.sex_index + 1;
    if (sex_display_index >= 0 &&
        sex_display_index < static_cast<int>(state.sex_items.size())) {
        set_text(state.sex_edit.window,
            state.sex_items[static_cast<std::size_t>(sex_display_index)].c_str());
    }
    else {
        set_text(state.sex_edit.window, "");
    }

    char text[256]{};
    const int age = profile.birth_year > 0 ?
        std::max(0, current_year() - profile.birth_year) : profile.birth_year;
    std::snprintf(text, sizeof(text), "%d", age);
    set_text(state.age_edit.window, text);

    set_text(state.guild_name_edit.window, profile.guild_name.data());
    set_text(state.description_edit.window, profile.description.data());
    set_text(state.normal_melee_edit.window,
        format_record(profile.normal_melee, -1, 0).c_str());
    set_text(state.normal_rank_edit.window,
        format_record(profile.normal_rank_record, profile.normal_rank,
            profile.normal_points).c_str());
    set_text(state.avatar_melee_edit.window,
        format_record(profile.avatar_melee, -1, 0).c_str());
    set_text(state.avatar_rank_edit.window,
        format_record(profile.avatar_rank_record, profile.avatar_rank,
            profile.avatar_points).c_str());

    SetLegacyStringSelectorSelectedIndex(state.location_selector,
        profile.location_index + 1);

    state.own_profile =
        upper_ascii(state.requested_name.data()) == upper_ascii(state.local_account_name.data());
    if (state.requested_name[0] == '\0') {
        state.own_profile =
            upper_ascii(profile.name.data()) == upper_ascii(state.local_account_name.data());
    }

    SetLegacyStringSelectorButtonsHidden(state.location_selector, !state.own_profile);
    ShowWindow(GetLegacyImageButtonWindow(state.item_deal_button),
        state.own_profile ? SW_HIDE : SW_SHOW);
    enable_window_style(state.name_edit.window, state.own_profile);
    enable_window_style(state.description_edit.window, state.own_profile);
    enable_window_style(GetLegacyStringSelectorWindow(state.location_selector),
        state.own_profile);

    enable_window_style(state.sex_edit.window, false);
    enable_window_style(state.age_edit.window, false);
    enable_window_style(state.normal_melee_edit.window, false);
    enable_window_style(state.normal_rank_edit.window, false);
    enable_window_style(state.guild_name_edit.window, false);
    enable_window_style(state.avatar_melee_edit.window, false);
    enable_window_style(state.avatar_rank_edit.window, false);

    for (int i = 0; i < kPlayerProfileAvatarButtonCount; ++i) {
        const bool visible = profile.avatar_ids[static_cast<std::size_t>(i)] >= 0;
        ShowWindow(GetLegacyImageButtonWindow(state.avatar_buttons[static_cast<std::size_t>(i)]),
            visible ? SW_SHOW : SW_HIDE);
    }
}

void show_child_controls(PlayerProfileState& state) {
    for (int id : {
             kPlayerProfileNameEditId,
             kPlayerProfileSexEditId,
             kPlayerProfileAgeEditId,
             kPlayerProfileNormalMeleeEditId,
             kPlayerProfileNormalRankEditId,
             kPlayerProfileDescriptionEditId,
             kPlayerProfileGuildNameEditId,
             kPlayerProfileAvatarMeleeEditId,
             kPlayerProfileAvatarRankEditId,
         }) {
        if (PlayerProfileTextControl* control = text_control_for_id(state, id)) {
            ShowWindow(control->window, SW_SHOW);
        }
    }
    ShowWindow(GetLegacyStringSelectorWindow(state.location_selector), SW_SHOW);
}

void destroy_buttons(PlayerProfileState& state) {
    DestroyPlayerProfileOkButton(state);
    DestroyPlayerProfileCancelButton(state);
    DestroyPlayerProfileItemDealButton(state);
    DestroyPlayerProfileMemoButton(state);
    DestroyPlayerProfileGuildIconButton(state);
    DestroyPlayerProfileAvatarButtonVector(state);
}

void release_window_resources(PlayerProfileState& state) {
    RestorePlayerProfileAccelerators(state);
    DestroyPlayerProfileBackgroundBitmap(state);
    destroy_buttons(state);
    DestroyPlayerProfileLocationSelector(state);
    destroy_control(state.name_edit);
    destroy_control(state.sex_edit);
    destroy_control(state.age_edit);
    destroy_control(state.normal_melee_edit);
    destroy_control(state.normal_rank_edit);
    destroy_control(state.description_edit);
    destroy_control(state.guild_name_edit);
    destroy_control(state.avatar_melee_edit);
    destroy_control(state.avatar_rank_edit);
    DestroyPlayerProfileTextRecordParser0(state);
    DestroyPlayerProfileTextRecordParser1(state);
    DestroyPlayerProfileAvatarStrip(state);
    state.window = nullptr;
    state.visible = false;
}

void submit_profile_update(PlayerProfileState& state) {
    if (!state.own_profile) {
        return;
    }

    std::vector<u8> packet(0x133, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x35);
    write_le32(packet, 8, 0x133);

    char name[0x20]{};
    char description[0x100]{};
    GetWindowTextA(state.name_edit.window, name, static_cast<int>(sizeof(name)));
    GetWindowTextA(state.description_edit.window, description,
        static_cast<int>(sizeof(description)));

    copy_fixed_string(packet, 0x0d, 0x20, name);
    const int location_index =
        GetLegacyStringSelectorSelectedIndex(state.location_selector);
    write_le32(packet, 0x2d, static_cast<u32>(state.profile.birth_year));
    write_le32(packet, 0x31, static_cast<u32>(state.profile.sex_index));
    write_le32(packet, 0x35, static_cast<u32>(location_index));
    copy_fixed_string(packet, 0x39, 0xfa, description);
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void queue_item_deal_request(PlayerProfileState& state) {
    std::vector<u8> packet(0x2d, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x69);
    write_le32(packet, 8, 0x2d);
    copy_fixed_string(packet, 0x0d, 0x20, state.profile.name.data());
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void open_item_deal_window(PlayerProfileState& state) {
    if (state.callbacks.open_item_deal != nullptr) {
        state.callbacks.open_item_deal(state);
    } else {
        std::array<u8, 0x40> remote_summary{};
        std::memcpy(remote_summary.data(), state.profile.name.data(),
            std::min<std::size_t>(state.profile.name.size(), 0x20));
        std::memcpy(remote_summary.data() + 0x20, state.profile.guild_name.data(),
            std::min<std::size_t>(state.profile.guild_name.size(), 0x20));
        const char* local_name = state.local_account_name[0] != '\0' ?
            state.local_account_name.data() : state.requested_name.data();
        if (local_name == nullptr || *local_name == '\0') {
            local_name = state.profile.name.data();
        }
        CreateBarterWindow(barter_window_state(), state.parent_window,
            state.instance, local_name, remote_summary.data(), remote_summary.size(),
            state.async_tcp_socket);
    }
}

bool paint_background_if_current(PlayerProfileState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToDc(state.background, paint.hdc, 0, 0);
    EndPaint(hwnd, &paint);
    return true;
}

void write_memo_command(PlayerProfileState& state) {
    char text[256]{};
    std::snprintf(text, sizeof(text), "/memo %s ", state.profile.name.data());
    if (state.callbacks.write_online_chat_text != nullptr) {
        state.callbacks.write_online_chat_text(state, text);
        return;
    }
    if (HWND edit = find_parent_online_chat_edit(state)) {
        SetWindowTextA(edit, text);
        const auto length = static_cast<WPARAM>(std::strlen(text));
        SendMessageA(edit, EM_SETSEL, length, length);
        SetFocus(edit);
    }
}

} // namespace

PlayerProfileState& player_profile_state() {
    return g_player_profile_state;
}

void InitializePlayerProfileBackgroundStatic(PlayerProfileState& state) {
    InitializePlayerProfileBackgroundBitmap(state);
    RegisterPlayerProfileBackgroundDestructor(state);
}

void InitializePlayerProfileBackgroundBitmap(PlayerProfileState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterPlayerProfileBackgroundDestructor(PlayerProfileState&) {
    register_atexit_once(g_background_destructor_registered,
        shutdown_global_background);
}

void DestroyPlayerProfileBackgroundBitmap(PlayerProfileState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

#define DEFINE_PLAYER_PROFILE_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Control, Slot, Callback) \
    void StaticName(PlayerProfileState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(PlayerProfileState& state) { \
        InitializeLegacyImageButtonControl(Control); \
    } \
    void RegisterName(PlayerProfileState&) { \
        register_atexit_once(g_button_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(PlayerProfileState& state) { \
        DestroyLegacyImageButtonControl(Control); \
    }

DEFINE_PLAYER_PROFILE_BUTTON_LIFETIME(InitializePlayerProfileOkButtonStatic,
    InitializePlayerProfileOkButton,
    RegisterPlayerProfileOkButtonDestructor,
    DestroyPlayerProfileOkButton, state.ok_button, 0, shutdown_global_ok_button)
DEFINE_PLAYER_PROFILE_BUTTON_LIFETIME(InitializePlayerProfileCancelButtonStatic,
    InitializePlayerProfileCancelButton,
    RegisterPlayerProfileCancelButtonDestructor,
    DestroyPlayerProfileCancelButton, state.cancel_button, 1,
    shutdown_global_cancel_button)
DEFINE_PLAYER_PROFILE_BUTTON_LIFETIME(InitializePlayerProfileItemDealButtonStatic,
    InitializePlayerProfileItemDealButton,
    RegisterPlayerProfileItemDealButtonDestructor,
    DestroyPlayerProfileItemDealButton, state.item_deal_button, 2,
    shutdown_global_item_deal_button)
DEFINE_PLAYER_PROFILE_BUTTON_LIFETIME(InitializePlayerProfileMemoButtonStatic,
    InitializePlayerProfileMemoButton,
    RegisterPlayerProfileMemoButtonDestructor,
    DestroyPlayerProfileMemoButton, state.memo_button, 3,
    shutdown_global_memo_button)
DEFINE_PLAYER_PROFILE_BUTTON_LIFETIME(InitializePlayerProfileGuildIconButtonStatic,
    InitializePlayerProfileGuildIconButton,
    RegisterPlayerProfileGuildIconButtonDestructor,
    DestroyPlayerProfileGuildIconButton, state.guild_icon_button, 4,
    shutdown_global_guild_icon_button)

#undef DEFINE_PLAYER_PROFILE_BUTTON_LIFETIME

void InitializePlayerProfileLocationSelectorStatic(PlayerProfileState& state) {
    InitializePlayerProfileLocationSelector(state);
    RegisterPlayerProfileLocationSelectorDestructor(state);
}

void InitializePlayerProfileLocationSelector(PlayerProfileState& state) {
    InitializeLegacyStringSelectorControl(state.location_selector);
}

void RegisterPlayerProfileLocationSelectorDestructor(PlayerProfileState&) {
    register_atexit_once(g_location_selector_destructor_registered,
        shutdown_global_location_selector);
}

void DestroyPlayerProfileLocationSelector(PlayerProfileState& state) {
    DestroyLegacyStringSelectorControl(state.location_selector);
}

void InitializePlayerProfileAvatarButtonVectorStatic(PlayerProfileState& state) {
    InitializePlayerProfileAvatarButtonVector(state);
    RegisterPlayerProfileAvatarButtonVectorDestructor(state);
}

void InitializePlayerProfileAvatarButtonVector(PlayerProfileState& state) {
    for (LegacyImageButtonControl& button : state.avatar_buttons) {
        InitializeLegacyImageButtonControl(button);
    }
}

void RegisterPlayerProfileAvatarButtonVectorDestructor(PlayerProfileState&) {
    register_atexit_once(g_avatar_button_vector_destructor_registered,
        shutdown_global_avatar_button_vector);
}

void DestroyPlayerProfileAvatarButtonVector(PlayerProfileState& state) {
    for (LegacyImageButtonControl& button : state.avatar_buttons) {
        DestroyLegacyImageButtonControl(button);
    }
}

void InitializePlayerProfileTextRecordParser0Static(PlayerProfileState& state) {
    InitializePlayerProfileTextRecordParser0(state);
    RegisterPlayerProfileTextRecordParser0Destructor(state);
}

void InitializePlayerProfileTextRecordParser0(PlayerProfileState&) {
}

void RegisterPlayerProfileTextRecordParser0Destructor(PlayerProfileState&) {
    register_atexit_once(g_text_record_parser0_destructor_registered,
        shutdown_global_text_record_parser0);
}

void DestroyPlayerProfileTextRecordParser0(PlayerProfileState&) {
}

void InitializePlayerProfileTextRecordParser1Static(PlayerProfileState& state) {
    InitializePlayerProfileTextRecordParser1(state);
    RegisterPlayerProfileTextRecordParser1Destructor(state);
}

void InitializePlayerProfileTextRecordParser1(PlayerProfileState&) {
}

void RegisterPlayerProfileTextRecordParser1Destructor(PlayerProfileState&) {
    register_atexit_once(g_text_record_parser1_destructor_registered,
        shutdown_global_text_record_parser1);
}

void DestroyPlayerProfileTextRecordParser1(PlayerProfileState&) {
}

void InitializePlayerProfileAvatarStripStatic(PlayerProfileState& state) {
    InitializePlayerProfileAvatarStrip(state);
    RegisterPlayerProfileAvatarStripDestructor(state);
}

void InitializePlayerProfileAvatarStrip(PlayerProfileState& state) {
    InitializeRawIndexedBitmapStrip(state.avatar_strip);
}

void RegisterPlayerProfileAvatarStripDestructor(PlayerProfileState&) {
    register_atexit_once(g_avatar_strip_destructor_registered,
        shutdown_global_avatar_strip);
}

void DestroyPlayerProfileAvatarStrip(PlayerProfileState& state) {
    HandleRawIndexedBitmapStripDestructor(state.avatar_strip);
}

void InitializePlayerProfileResources(PlayerProfileState& state) {
    InitializePlayerProfileBackgroundStatic(state);
    InitializeBitmapIconResourceCollection(state.local_icons);
    if (state.icon_collection == nullptr) {
        state.icon_collection = &GlobalBitmapIconResourceCollection();
    }
    InitializePlayerProfileOkButtonStatic(state);
    InitializePlayerProfileCancelButtonStatic(state);
    InitializePlayerProfileItemDealButtonStatic(state);
    InitializePlayerProfileMemoButtonStatic(state);
    InitializePlayerProfileGuildIconButtonStatic(state);
    InitializePlayerProfileLocationSelectorStatic(state);
    InitializePlayerProfileAvatarButtonVectorStatic(state);
    InitializePlayerProfileTextRecordParser0Static(state);
    InitializePlayerProfileTextRecordParser1Static(state);
    InitializePlayerProfileAvatarStripStatic(state);
}

void ReleasePlayerProfileResources(PlayerProfileState& state) {
    release_window_resources(state);
    HandleBitmapIconResourceCollectionDestructor(state.local_icons);
}

void InstallPlayerProfileAccelerators(PlayerProfileState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kPlayerProfileAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestorePlayerProfileAccelerators(PlayerProfileState& state) {
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

bool CreatePlayerProfileWindow(PlayerProfileState& state, HWND parent,
    HINSTANCE instance, const void* profile_payload, std::size_t profile_payload_size,
    const char* requested_name, const char* local_account_name,
    LegacyAsyncTcpSocket* async_tcp_socket) {
    if (state.window != nullptr) {
        return false;
    }

    InitializePlayerProfileResources(state);
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.async_tcp_socket = async_tcp_socket;
    copy_c_string(state.requested_name, requested_name);
    copy_c_string(state.local_account_name, local_account_name);
    FrontendLayoutTableOwner layout_table;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout_table.table,
            kPlayerProfileLayoutTrcRecord)) {
        release_window_resources(state);
        return false;
    }
    state.layout = copy_layout_record(layout_table.table);
    load_text_tables(state);

    state.raw_payload.assign(kPlayerProfilePayloadBytes, 0);
    if (profile_payload != nullptr && profile_payload_size != 0) {
        const std::size_t copy_size =
            std::min<std::size_t>(profile_payload_size, state.raw_payload.size());
        std::memcpy(state.raw_payload.data(), profile_payload, copy_size);
    }
    parse_payload(state);
    load_avatar_strip(state);

    const DWORD style =
        IsWindow(parent) && GetWindowLongPtrA(parent, GWL_STYLE) != 0 ?
        kWindowStyleWindowed : kWindowStyleFullscreen;
    const PlayerProfileLayoutRect window_rect = layout_at(state, 0);
    const POINT origin =
        RankerCenteredFrontendWindowOrigin(window_rect.width, window_rect.height);
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Player Profile",
        "Player Profile", style, origin.x, origin.y, window_rect.width,
        window_rect.height, parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(player_profile_window_proc));

    PlayerProfileLayoutRect guild_name_rect = layout_at(state, 12);
    guild_name_rect.y += 3;
    guild_name_rect.height -= 3;

    if (!create_text_control(state.name_edit, state.window, instance, kEditStyle,
            kPlayerProfileNameEditId, layout_at(state, 1)) ||
        !create_text_control(state.sex_edit, state.window, instance, kEditStyle,
            kPlayerProfileSexEditId, layout_at(state, 2)) ||
        !create_text_control(state.age_edit, state.window, instance, kEditStyle,
            kPlayerProfileAgeEditId, layout_at(state, 3)) ||
        !create_text_control(state.normal_melee_edit, state.window, instance,
            kEditStyle, kPlayerProfileNormalMeleeEditId, layout_at(state, 4)) ||
        !create_text_control(state.normal_rank_edit, state.window, instance,
            kEditStyle, kPlayerProfileNormalRankEditId, layout_at(state, 5)) ||
        !create_text_control(state.description_edit, state.window, instance,
            kMultilineEditStyle, kPlayerProfileDescriptionEditId, layout_at(state, 6)) ||
        !create_text_control(state.guild_name_edit, state.window, instance,
            kMultilineEditStyle, kPlayerProfileGuildNameEditId, guild_name_rect) ||
        !create_text_control(state.avatar_melee_edit, state.window, instance,
            kMultilineEditStyle, kPlayerProfileAvatarMeleeEditId, layout_at(state, 14)) ||
        !create_text_control(state.avatar_rank_edit, state.window, instance,
            kMultilineEditStyle, kPlayerProfileAvatarRankEditId, layout_at(state, 15))) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }

    SendMessageA(state.name_edit.window, EM_LIMITTEXT, 0x20, 0);
    SendMessageA(state.sex_edit.window, EM_LIMITTEXT, 8, 0);
    SendMessageA(state.age_edit.window, EM_LIMITTEXT, 8, 0);
    SendMessageA(state.normal_melee_edit.window, EM_LIMITTEXT, 0x10, 0);
    SendMessageA(state.normal_rank_edit.window, EM_LIMITTEXT, 0x10, 0);
    SendMessageA(state.description_edit.window, EM_LIMITTEXT, 0xfa, 0);
    SendMessageA(state.guild_name_edit.window, EM_LIMITTEXT, 0x20, 0);
    SendMessageA(state.avatar_melee_edit.window, EM_LIMITTEXT, 0x10, 0);
    SendMessageA(state.avatar_rank_edit.window, EM_LIMITTEXT, 0x10, 0);

    if (!create_image_button(state.ok_button, state.window, "OK",
            kPlayerProfileOkButtonId, layout_at(state, 7),
            kPlayerProfileOkNormalBitmapRecord, kPlayerProfileOkPressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kPlayerProfileCancelButtonId, layout_at(state, 8),
            kPlayerProfileCancelNormalBitmapRecord, kPlayerProfileCancelPressedBitmapRecord) ||
        !create_image_button(state.item_deal_button, state.window, "ItemDeal",
            kPlayerProfileItemDealButtonId, layout_at(state, 9),
            kPlayerProfileItemDealNormalBitmapRecord,
            kPlayerProfileItemDealPressedBitmapRecord) ||
        !create_image_button(state.memo_button, state.window, "Memo",
            kPlayerProfileMemoButtonId, layout_at(state, 10),
            kPlayerProfileMemoNormalBitmapRecord, kPlayerProfileMemoPressedBitmapRecord) ||
        !create_image_button(state.guild_icon_button, state.window, "GuildIcon",
            kPlayerProfileGuildIconButtonId, layout_at(state, 11), 0, 0) ||
        !create_location_selector(state)) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }

    for (int i = 0; i < kPlayerProfileAvatarButtonCount; ++i) {
        if (!create_image_button(state.avatar_buttons[static_cast<std::size_t>(i)],
                state.window, "Avatar Icon 1", kPlayerProfileAvatarFirstButtonId + i,
                layout_at(state, 16 + i), 0, 0)) {
            DestroyWindow(state.window);
            state.window = nullptr;
            return false;
        }
    }

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kPlayerProfileBackgroundBitmapRecord);
    InstallPlayerProfileAccelerators(state);
    apply_profile_to_controls(state);
    show_child_controls(state);
    ShowWindow(state.window, SW_SHOW);
    SetFocus(state.name_edit.window);
    state.visible = true;
    return true;
}

void DrawPlayerProfileAvatarButton(PlayerProfileState& state,
    const DRAWITEMSTRUCT& item, int avatar_index) {
    if (avatar_index < 0 || avatar_index >= kPlayerProfileAvatarButtonCount) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(avatar_index);
    const i32 avatar_id = state.profile.avatar_ids[index];
    if (avatar_id < 0) {
        return;
    }
    DrawRawIndexedBitmapStripFrame(state.avatar_strip, item.hDC, 6, 4,
        static_cast<u32>(avatar_id));

    RECT label_rect = item.rcItem;
    label_rect.left += 3;
    label_rect.top += 0x34;
    label_rect.right -= 3;
    label_rect.bottom -= 4;

    char text[64]{};
    std::snprintf(text, sizeof(text), "%s%d", state.avatar_level_label.c_str(),
        state.profile.avatar_levels[index] + 1);
    SetTextColor(item.hDC, kProfileSoftWhite);
    SetBkMode(item.hDC, TRANSPARENT);
    DrawTextA(item.hDC, text, -1, &label_rect, DT_CENTER);
}

LRESULT HandlePlayerProfileWindowMessage(PlayerProfileState& state, HWND hwnd,
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
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kProfileWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kProfileBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kPlayerProfileGuildIconButtonId) {
            BitmapIconResourceCollection& icons =
                state.icon_collection != nullptr ? *state.icon_collection :
                state.local_icons;
            StretchBitmapMemoryResourceToDc(
                GetBitmapIconSlotOrDefault(icons,
                    static_cast<u32>(std::max(0, state.profile.guild_icon_slot))),
                draw->hDC, 0, 0);
            break;
        }
        if (draw->CtlID >= kPlayerProfileAvatarFirstButtonId &&
            draw->CtlID < kPlayerProfileAvatarFirstButtonId +
                kPlayerProfileAvatarButtonCount) {
            DrawPlayerProfileAvatarButton(state, *draw,
                static_cast<int>(draw->CtlID - kPlayerProfileAvatarFirstButtonId));
            break;
        }
        if (LegacyImageButtonControl* button =
                button_for_id(state, static_cast<int>(draw->CtlID))) {
            DrawLegacyImageButtonItem(*button, *draw);
            break;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        switch (id) {
        case kPlayerProfileItemDealButtonId:
            play_click_sound(state);
            queue_item_deal_request(state);
            DestroyWindow(hwnd);
            open_item_deal_window(state);
            break;
        case kPlayerProfileOkButtonId:
            play_click_sound(state);
            submit_profile_update(state);
            DestroyWindow(hwnd);
            focus_online_chat(state);
            break;
        case kPlayerProfileCancelButtonId:
            play_click_sound(state);
            DestroyWindow(hwnd);
            focus_online_chat(state);
            break;
        case kPlayerProfileMemoButtonId:
            play_click_sound(state);
            write_memo_command(state);
            DestroyWindow(hwnd);
            focus_online_chat(state);
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandlePlayerProfileControlMessage(PlayerProfileState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    switch (id) {
    case kPlayerProfileNameEditId:
    case kPlayerProfileSexEditId:
    case kPlayerProfileAgeEditId:
    case kPlayerProfileNormalMeleeEditId:
    case kPlayerProfileNormalRankEditId:
    case kPlayerProfileDescriptionEditId:
    case kPlayerProfileOkButtonId:
    case kPlayerProfileCancelButtonId:
    case kPlayerProfileItemDealButtonId:
    case kPlayerProfileMemoButtonId:
    case kPlayerProfileGuildIconButtonId:
    case kPlayerProfileGuildNameEditId:
    case kPlayerProfileAvatarMeleeEditId:
    case kPlayerProfileAvatarRankEditId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    case kPlayerProfileLocationSelectorId:
        return HandleLegacyStringSelectorMessage(state.location_selector, hwnd,
            message, wparam, lparam);
    default:
        if (id >= kPlayerProfileAvatarFirstButtonId &&
            id < kPlayerProfileAvatarFirstButtonId + kPlayerProfileAvatarButtonCount) {
            return CallWindowProcA(
                state.avatar_buttons[static_cast<std::size_t>(
                    id - kPlayerProfileAvatarFirstButtonId)]
                    .original_window_proc,
                hwnd, message, wparam, lparam);
        }
        return 0;
    }
}

}

#endif
