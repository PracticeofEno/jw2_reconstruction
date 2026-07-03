#include "ranker_account_profile.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_icon_strips.h"
#include "ranker_online_dialogs.h"
#include "ranker_setup_data.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed = 0x10cf0000;
constexpr DWORD kAccountEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kPasswordEditStyle = WS_CHILD | WS_VISIBLE | ES_PASSWORD;
constexpr DWORD kStatusEditStyle = WS_CHILD | ES_MULTILINE | ES_READONLY;
constexpr DWORD kIntroEditStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
constexpr DWORD kImageComboStyle = WS_CHILD | CBS_DROPDOWNLIST |
    CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
constexpr COLORREF kAccountWhite = RGB(255, 255, 255);
constexpr COLORREF kAccountSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kAccountYellow = RGB(255, 255, 0);
constexpr COLORREF kAccountBlack = RGB(0, 0, 0);
constexpr COLORREF kAccountErrorBlue = RGB(10, 10, 250);
constexpr int kSelectorButtonWidth = 0x0f;
constexpr int kAvatarFrameWidth = 0x26;
constexpr int kAvatarFrameHeight = 0x26;
constexpr int kAvatarFrameCount = 64;

AccountProfileState g_account_profile_state;
std::array<bool, 10> g_ui_destructor_registered{};
bool g_background_bitmap_destructor_registered = false;
bool g_text_record_parser_destructor_registered = false;
bool g_avatar_strip_destructor_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK account_profile_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleAccountProfileWindowMessage(g_account_profile_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK account_profile_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleAccountProfileControlMessage(g_account_profile_state, hwnd, message,
        wparam, lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void destroy_account_profile_ui_slot(AccountProfileState& state, std::size_t slot) {
    switch (slot) {
    case 0:
        DestroyAccountProfileOkButton(state);
        break;
    case 1:
        DestroyAccountProfileCancelButton(state);
        break;
    case 2:
        DestroyAccountProfileLocationSelector(state);
        break;
    case 3:
        DestroyAccountProfileBirthYearSelector(state);
        break;
    case 4:
        DestroyAccountProfileSexComboBox(state);
        break;
    case 5:
        DestroyAccountProfilePowerManButton(state);
        break;
    case 6:
        DestroyAccountProfileVelocisButton(state);
        break;
    case 7:
        DestroyAccountProfileRedElfButton(state);
        break;
    case 8:
        DestroyAccountProfileSkeletonButton(state);
        break;
    case 9:
        DestroyAccountProfileAvatarIconButton(state);
        break;
    default:
        break;
    }
}

template <std::size_t Slot>
void shutdown_account_profile_ui_slot() {
    destroy_account_profile_ui_slot(g_account_profile_state, Slot);
}

void shutdown_account_profile_background_bitmap() {
    DestroyAccountProfileBackgroundBitmap(g_account_profile_state);
}

void shutdown_account_profile_text_record_parser() {
    DestroyAccountProfileTextRecordParser(g_account_profile_state);
}

void shutdown_account_profile_avatar_strip() {
    DestroyAccountProfileAvatarStrip(g_account_profile_state);
}

void write_le32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset + 4 > buffer.size()) {
        return;
    }
    buffer[offset] = static_cast<u8>(value & 0xff);
    buffer[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
    buffer[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
    buffer[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

u32 read_le32(const u8* buffer, i32 byte_count, std::size_t offset) {
    if (buffer == nullptr || byte_count < 0 ||
        offset + 4 > static_cast<std::size_t>(byte_count)) {
        return 0;
    }
    return static_cast<u32>(buffer[offset]) |
        (static_cast<u32>(buffer[offset + 1]) << 8) |
        (static_cast<u32>(buffer[offset + 2]) << 16) |
        (static_cast<u32>(buffer[offset + 3]) << 24);
}

void copy_c_string(std::vector<u8>& buffer, std::size_t offset, std::size_t field_size,
    const char* text) {
    if (offset >= buffer.size() || field_size == 0) {
        return;
    }
    const std::size_t available = std::min(field_size, buffer.size() - offset);
    std::memset(buffer.data() + offset, 0, available);
    if (text == nullptr) {
        return;
    }
    std::strncpy(reinterpret_cast<char*>(buffer.data() + offset), text,
        available - 1);
}

void copy_fixed_string(std::vector<u8>& buffer, std::size_t offset,
    std::size_t field_size, const char* text) {
    if (offset >= buffer.size() || field_size == 0) {
        return;
    }
    const std::size_t available = std::min(field_size, buffer.size() - offset);
    std::memset(buffer.data() + offset, 0, available);
    if (text == nullptr) {
        return;
    }
    std::strncpy(reinterpret_cast<char*>(buffer.data() + offset), text, available);
}

void read_window_text(HWND window, char* target, int target_size) {
    if (target == nullptr || target_size <= 0) {
        return;
    }
    target[0] = '\0';
    if (window != nullptr) {
        GetWindowTextA(window, target, target_size);
    }
}

const char* startup_message_row(std::size_t index, const char* fallback) {
    const auto& rows = startup_text_tables().message_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

const char* account_status_message(u32 status) {
    switch (status) {
    case 1:
        return startup_message_row(82, "This account is already in use.");
    case 2:
        return startup_message_row(8, "The CD key is invalid.");
    case 3:
        return startup_message_row(9, "The CD key cannot be used.");
    case 4:
        return startup_message_row(10, "The CD key is already in use.");
    case 5:
        return startup_message_row(11, "Too many users are connected.");
    case 6:
        return startup_message_row(83, "The account contains invalid characters.");
    case 7:
        return startup_message_row(84, "The password contains invalid characters.");
    default:
        return startup_message_row(13, "An unknown error occurred.");
    }
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
        if (first_quote == std::string::npos) {
            continue;
        }
        const std::size_t second_quote = line.find('"', first_quote + 1);
        if (second_quote == std::string::npos || second_quote <= first_quote) {
            continue;
        }
        values[static_cast<int>(key)] =
            line.substr(first_quote + 1, second_quote - first_quote - 1);
    }
    return values;
}

std::vector<AccountProfileLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<AccountProfileLayoutRect> rects;
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

AccountProfileLayoutRect layout_at(const std::vector<AccountProfileLayoutRect>& rects,
    std::size_t index) {
    if (index < rects.size()) {
        return rects[index];
    }
    return AccountProfileLayoutRect{};
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

std::vector<std::string> read_location_items() {
    const char* fallback[] = {"Select"};
    return sorted_values_or_fallback(
        read_keyed_string_record(kAccountProfileLocationTextTrcRecord), fallback,
        std::size(fallback));
}

std::vector<std::string> read_birth_year_items() {
    const std::map<int, std::string> values =
        read_keyed_string_record(kAccountProfileBirthYearTextTrcRecord);
    std::vector<std::string> items;
    const auto select = values.find(0);
    const auto begin = values.find(1);
    const auto end = values.find(2);
    if (select != values.end() && begin != values.end() && end != values.end()) {
        items.push_back(select->second);
        const int begin_year = std::atoi(begin->second.c_str());
        const int end_year = std::atoi(end->second.c_str());
        for (int year = begin_year; year < end_year; ++year) {
            items.push_back(std::to_string(year));
        }
    }
    if (items.empty()) {
        items = {"Select", "1951", "1952", "1953", "1954", "1955"};
    }
    return items;
}

std::vector<std::string> read_sex_items() {
    const char* fallback[] = {"Select", "M", "F"};
    return sorted_values_or_fallback(
        read_keyed_string_record(kAccountProfileSexTextTrcRecord), fallback,
        std::size(fallback));
}

void read_avatar_metadata(AccountProfileState& state) {
    const std::map<int, std::string> values =
        read_keyed_string_record(kAccountProfileAvatarTextTrcRecord);
    for (int i = 0; i < 4; ++i) {
        const auto found = values.find(i);
        if (found != values.end()) {
            state.avatar_record_ids[static_cast<std::size_t>(i)] =
                static_cast<u32>(std::strtoul(found->second.c_str(), nullptr, 10));
        }
    }
    for (int i = 0; i < 4; ++i) {
        const auto found = values.find(10 + i);
        if (found != values.end()) {
            state.avatar_stat_labels[static_cast<std::size_t>(i)] = found->second;
        }
    }
}

bool load_avatar_strip(AccountProfileState& state) {
    return LoadAvatarIconStrip(state.avatar_strip);
}

void clear_text_control(AccountProfileTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_text_control(AccountProfileTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(account_profile_control_proc));
}

bool create_text_control(AccountProfileTextControl& control, HWND parent,
    HINSTANCE instance, DWORD style, int id, const AccountProfileLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, "edit", nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_text_control(control);
        return false;
    }
    subclass_text_control(control);
    return true;
}

void subclass_image_button(LegacyImageButtonControl& control) {
    if (control.window != nullptr) {
        SetWindowLongPtrA(control.window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(account_profile_control_proc));
    }
}

bool create_image_button(LegacyImageButtonControl& control, HWND parent,
    const char* text, int id, const AccountProfileLayoutRect& rect,
    u32 normal_record, u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(control, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageButtonBitmaps(control, normal_record, pressed_record);
    subclass_image_button(control);
    return true;
}

bool create_selector(LegacyStringSelectorControl& control, HWND parent,
    const char* text, int id, const AccountProfileLayoutRect& rect) {
    if (!CreateLegacyStringSelectorWindow(control, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height, kSelectorButtonWidth)) {
        return false;
    }
    LoadLegacyStringSelectorIncrementButtonBitmaps(control, 0x114, 0x114);
    LoadLegacyStringSelectorDecrementButtonBitmaps(control, 0x115, 0x115);
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(account_profile_control_proc));
    return true;
}

bool create_sex_combo(AccountProfileState& state, HWND parent, HINSTANCE instance,
    const AccountProfileLayoutRect& rect) {
    if (!CreateLegacyImageComboBoxWindow(state.sex_combo, parent, "",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAccountProfileSexComboId)),
            kImageComboStyle, rect.x, rect.y, rect.width, rect.height + 0x96)) {
        return false;
    }
    LoadLegacyImageComboBoxBitmaps(state.sex_combo, 0xb9, 0);
    SetWindowLongPtrA(state.sex_combo.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(account_profile_control_proc));
    return true;
}

void add_selector_strings(LegacyStringSelectorControl& selector,
    const std::vector<std::string>& items) {
    for (const std::string& item : items) {
        AddLegacyStringSelectorText(selector, item.c_str());
    }
}

void add_combo_strings(HWND combo, const std::vector<std::string>& items) {
    if (combo == nullptr) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    for (const std::string& item : items) {
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    if (!items.empty()) {
        SendMessageA(combo, CB_SETCURSEL, 0, 0);
    }
}

LegacyImageButtonControl* button_for_id(AccountProfileState& state, int id) {
    switch (id) {
    case kAccountProfileOkButtonId:
        return &state.ok_button;
    case kAccountProfileCancelButtonId:
        return &state.cancel_button;
    case kAccountProfileAvatarPowerManButtonId:
    case kAccountProfileAvatarVelocisButtonId:
    case kAccountProfileAvatarRedElfButtonId:
    case kAccountProfileAvatarSkeletonButtonId:
        return &state.avatar_buttons[static_cast<std::size_t>(
            id - kAccountProfileAvatarPowerManButtonId)];
    case kAccountProfileAvatarIconButtonId:
        return &state.avatar_icon_button;
    default:
        return nullptr;
    }
}

WNDPROC original_proc_for_id(AccountProfileState& state, int id) {
    switch (id) {
    case kAccountProfileAccountEditId:
        return state.account_edit.original_window_proc;
    case kAccountProfilePasswordEditId:
        return state.password_edit.original_window_proc;
    case kAccountProfileConfirmPasswordEditId:
        return state.confirm_password_edit.original_window_proc;
    case kAccountProfileStatusEditId:
        return state.status_edit.original_window_proc;
    case kAccountProfileIntroEditId:
        return state.intro_edit.original_window_proc;
    case kAccountProfileStatsEditId:
        return state.stats_edit.original_window_proc;
    case kAccountProfileLocationSelectorId:
        return state.location_selector.original_window_proc;
    case kAccountProfileBirthYearSelectorId:
        return state.birth_year_selector.original_window_proc;
    case kAccountProfileSexComboId:
        return state.sex_combo.original_window_proc;
    default:
        if (LegacyImageButtonControl* button = button_for_id(state, id)) {
            return button->original_window_proc;
        }
        return nullptr;
    }
}

LegacyStringSelectorControl* selector_for_id(AccountProfileState& state, int id) {
    if (id == kAccountProfileLocationSelectorId) {
        return &state.location_selector;
    }
    if (id == kAccountProfileBirthYearSelectorId) {
        return &state.birth_year_selector;
    }
    return nullptr;
}

bool paint_background_if_current(AccountProfileState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
    EndPaint(hwnd, &paint);
    return true;
}

void queue_packet(AccountProfileState& state, const void* packet, i32 byte_count) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet, byte_count);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket,
            const_cast<void*>(packet), byte_count, nullptr);
    }
}

void show_account_message(AccountProfileState& state, const char* text,
    COLORREF color = kAccountSoftWhite) {
    state.last_message = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
        return;
    }
    if (state.window != nullptr) {
        ShowOnlineModalPrompt1(online_modal_prompt_state(), state.window,
            state.last_message.c_str(), color);
    }
}

void close_async_socket(AccountProfileState& state) {
    if (state.callbacks.close_async_socket != nullptr) {
        state.callbacks.close_async_socket(state);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
    }
}

void destroy_account_window(AccountProfileState& state) {
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
}

AccountProfileAvatarStats lookup_avatar_stats(AccountProfileState& state,
    u32 avatar_record_id) {
    AccountProfileAvatarStats stats{};
    if (state.callbacks.lookup_avatar_stats != nullptr &&
        state.callbacks.lookup_avatar_stats(state, avatar_record_id, stats)) {
        return stats;
    }
    return stats;
}

void redraw_avatar_buttons(AccountProfileState& state) {
    for (LegacyImageButtonControl& button : state.avatar_buttons) {
        if (button.window != nullptr) {
            RedrawWindow(button.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
    }
    if (state.avatar_icon_button.window != nullptr) {
        RedrawWindow(state.avatar_icon_button.window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

int selected_combo_index(HWND combo) {
    if (combo == nullptr) {
        return CB_ERR;
    }
    return static_cast<int>(SendMessageA(combo, CB_GETCURSEL, 0, 0));
}

void focus_forward(AccountProfileState& state) {
    HWND focus = GetFocus();
    const int id = static_cast<int>(GetWindowLongPtrA(focus, GWLP_ID));
    switch (id - kAccountProfileAccountEditId) {
    case 0:
        SetFocus(state.password_edit.window);
        break;
    case 1:
        SetFocus(state.confirm_password_edit.window);
        break;
    case 2:
        SetFocus(state.intro_edit.window);
        break;
    case 7:
        SetFocus(state.location_selector.window);
        break;
    case 8:
        SetFocus(state.birth_year_selector.window);
        break;
    case 9:
        SetFocus(state.sex_combo.window);
        break;
    case 10:
    default:
        SetFocus(state.account_edit.window);
        break;
    }
}

void focus_backward(AccountProfileState& state) {
    HWND focus = GetFocus();
    const int id = static_cast<int>(GetWindowLongPtrA(focus, GWLP_ID));
    switch (id - kAccountProfileAccountEditId) {
    case 0:
        SetFocus(state.sex_combo.window);
        break;
    case 1:
        SetFocus(state.account_edit.window);
        break;
    case 2:
        SetFocus(state.password_edit.window);
        break;
    case 7:
        SetFocus(state.confirm_password_edit.window);
        break;
    case 8:
        SetFocus(state.intro_edit.window);
        break;
    case 9:
        SetFocus(state.location_selector.window);
        break;
    case 10:
        SetFocus(state.birth_year_selector.window);
        break;
    default:
        break;
    }
}

void route_to_connect(AccountProfileState& state) {
    close_async_socket(state);
    destroy_account_window(state);
    if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    }
}

void route_to_wizard_login(AccountProfileState& state) {
    destroy_account_window(state);
    if (state.callbacks.open_wizard_login != nullptr) {
        state.callbacks.open_wizard_login(state);
    }
}

void write_setup_data(AccountProfileState& state) {
    if (state.callbacks.write_setup_data != nullptr) {
        state.callbacks.write_setup_data(state);
        return;
    }
    ExportSetupText(kSetupWizardAccountOffset, state.submitted_account);
    WriteDefaultSetupDataBuffer();
}

void handle_account_success(AccountProfileState& state) {
    read_window_text(state.account_edit.window, state.submitted_account.data(),
        static_cast<int>(state.submitted_account.size()));
    write_setup_data(state);
    destroy_account_window(state);
    if (state.callbacks.open_online_lobby != nullptr) {
        state.callbacks.open_online_lobby(state);
    }
}

void handle_status_code(AccountProfileState& state, u32 status) {
    switch (status) {
    case 0:
        handle_account_success(state);
        break;
    default:
        show_account_message(state, account_status_message(status), kAccountErrorBlue);
        break;
    }
}

void release_window_resources(AccountProfileState& state) {
    DestroyAccountProfileBackgroundBitmap(state);
    DestroyAccountProfileAvatarStrip(state);
    DestroyAccountProfileOkButton(state);
    DestroyAccountProfileCancelButton(state);
    DestroyAccountProfileLocationSelector(state);
    DestroyAccountProfileBirthYearSelector(state);
    DestroyAccountProfileSexComboBox(state);
    DestroyAccountProfilePowerManButton(state);
    DestroyAccountProfileVelocisButton(state);
    DestroyAccountProfileRedElfButton(state);
    DestroyAccountProfileSkeletonButton(state);
    DestroyAccountProfileAvatarIconButton(state);
    DestroyAccountProfileTextRecordParser(state);
    clear_text_control(state.account_edit);
    clear_text_control(state.password_edit);
    clear_text_control(state.confirm_password_edit);
    clear_text_control(state.status_edit);
    clear_text_control(state.intro_edit);
    clear_text_control(state.stats_edit);
}

void load_window_data(AccountProfileState& state) {
    state.location_items = read_location_items();
    state.birth_year_items = read_birth_year_items();
    state.sex_items = read_sex_items();
    state.avatar_record_ids = {1, 34, 17, 49};
    state.avatar_stat_labels = {"HP: ", "MP: ", "OP: ", "DP: "};
    read_avatar_metadata(state);
}

void set_initial_text(HWND window, LPARAM text) {
    if (window != nullptr && text != 0) {
        SetWindowTextA(window, reinterpret_cast<LPCSTR>(text));
    }
}

} // namespace

AccountProfileState& account_profile_state() {
    return g_account_profile_state;
}

#define DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Control, Slot) \
    void StaticName(AccountProfileState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(AccountProfileState& state) { \
        InitializeLegacyImageButtonControl(Control); \
    } \
    void RegisterName(AccountProfileState&) { \
        register_atexit_once(g_ui_destructor_registered[Slot], \
            shutdown_account_profile_ui_slot<Slot>); \
    } \
    void DestroyName(AccountProfileState& state) { \
        DestroyLegacyImageButtonControl(Control); \
    }

DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(InitializeAccountProfileOkButtonStatic,
    InitializeAccountProfileOkButton,
    RegisterAccountProfileOkButtonDestructor,
    DestroyAccountProfileOkButton, state.ok_button, 0)
DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(InitializeAccountProfileCancelButtonStatic,
    InitializeAccountProfileCancelButton,
    RegisterAccountProfileCancelButtonDestructor,
    DestroyAccountProfileCancelButton, state.cancel_button, 1)
DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(InitializeAccountProfilePowerManButtonStatic,
    InitializeAccountProfilePowerManButton,
    RegisterAccountProfilePowerManButtonDestructor,
    DestroyAccountProfilePowerManButton, state.avatar_buttons[0], 5)
DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(InitializeAccountProfileVelocisButtonStatic,
    InitializeAccountProfileVelocisButton,
    RegisterAccountProfileVelocisButtonDestructor,
    DestroyAccountProfileVelocisButton, state.avatar_buttons[1], 6)
DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(InitializeAccountProfileRedElfButtonStatic,
    InitializeAccountProfileRedElfButton,
    RegisterAccountProfileRedElfButtonDestructor,
    DestroyAccountProfileRedElfButton, state.avatar_buttons[2], 7)
DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(InitializeAccountProfileSkeletonButtonStatic,
    InitializeAccountProfileSkeletonButton,
    RegisterAccountProfileSkeletonButtonDestructor,
    DestroyAccountProfileSkeletonButton, state.avatar_buttons[3], 8)
DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME(InitializeAccountProfileAvatarIconButtonStatic,
    InitializeAccountProfileAvatarIconButton,
    RegisterAccountProfileAvatarIconButtonDestructor,
    DestroyAccountProfileAvatarIconButton, state.avatar_icon_button, 9)

#undef DEFINE_ACCOUNT_PROFILE_BUTTON_LIFETIME

#define DEFINE_ACCOUNT_PROFILE_SELECTOR_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Control, Slot) \
    void StaticName(AccountProfileState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(AccountProfileState& state) { \
        InitializeLegacyStringSelectorControl(Control); \
    } \
    void RegisterName(AccountProfileState&) { \
        register_atexit_once(g_ui_destructor_registered[Slot], \
            shutdown_account_profile_ui_slot<Slot>); \
    } \
    void DestroyName(AccountProfileState& state) { \
        DestroyAccountProfileStringSelector(Control); \
    }

DEFINE_ACCOUNT_PROFILE_SELECTOR_LIFETIME(
    InitializeAccountProfileLocationSelectorStatic,
    InitializeAccountProfileLocationSelector,
    RegisterAccountProfileLocationSelectorDestructor,
    DestroyAccountProfileLocationSelector, state.location_selector, 2)
DEFINE_ACCOUNT_PROFILE_SELECTOR_LIFETIME(
    InitializeAccountProfileBirthYearSelectorStatic,
    InitializeAccountProfileBirthYearSelector,
    RegisterAccountProfileBirthYearSelectorDestructor,
    DestroyAccountProfileBirthYearSelector, state.birth_year_selector, 3)

#undef DEFINE_ACCOUNT_PROFILE_SELECTOR_LIFETIME

void InitializeAccountProfileSexComboBoxStatic(AccountProfileState& state) {
    InitializeAccountProfileSexComboBox(state);
    RegisterAccountProfileSexComboBoxDestructor(state);
}

void InitializeAccountProfileSexComboBox(AccountProfileState& state) {
    InitializeLegacyImageComboBoxControl(state.sex_combo);
}

void RegisterAccountProfileSexComboBoxDestructor(AccountProfileState&) {
    register_atexit_once(g_ui_destructor_registered[4],
        shutdown_account_profile_ui_slot<4>);
}

void DestroyAccountProfileSexComboBox(AccountProfileState& state) {
    DestroyLegacyImageComboBoxControl(state.sex_combo);
}

void DestroyAccountProfileStringSelector(LegacyStringSelectorControl& selector) {
    DestroyLegacyStringSelectorControl(selector);
}

void InitializeAccountProfileBackgroundBitmapStatic(AccountProfileState& state) {
    InitializeAccountProfileBackgroundBitmap(state);
    RegisterAccountProfileBackgroundBitmapDestructor(state);
}

void InitializeAccountProfileBackgroundBitmap(AccountProfileState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterAccountProfileBackgroundBitmapDestructor(AccountProfileState&) {
    register_atexit_once(g_background_bitmap_destructor_registered,
        shutdown_account_profile_background_bitmap);
}

void DestroyAccountProfileBackgroundBitmap(AccountProfileState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

void ShutdownAccountProfileBackgroundBitmap(AccountProfileState& state) {
    DestroyAccountProfileBackgroundBitmap(state);
}

void InitializeAccountProfileTextRecordParserStatic(AccountProfileState& state) {
    InitializeAccountProfileTextRecordParser(state);
    RegisterAccountProfileTextRecordParserDestructor(state);
}

void InitializeAccountProfileTextRecordParser(AccountProfileState&) {
}

void RegisterAccountProfileTextRecordParserDestructor(AccountProfileState&) {
    register_atexit_once(g_text_record_parser_destructor_registered,
        shutdown_account_profile_text_record_parser);
}

void DestroyAccountProfileTextRecordParser(AccountProfileState&) {
}

void InitializeAccountProfileAvatarStripStatic(AccountProfileState& state) {
    InitializeAccountProfileAvatarStrip(state);
    RegisterAccountProfileAvatarStripDestructor(state);
}

void InitializeAccountProfileAvatarStrip(AccountProfileState& state) {
    InitializeRawIndexedBitmapStrip(state.avatar_strip);
}

void RegisterAccountProfileAvatarStripDestructor(AccountProfileState&) {
    register_atexit_once(g_avatar_strip_destructor_registered,
        shutdown_account_profile_avatar_strip);
}

void DestroyAccountProfileAvatarStrip(AccountProfileState& state) {
    HandleRawIndexedBitmapStripDestructor(state.avatar_strip);
}

void InitializeAccountProfileControls(AccountProfileState& state) {
    InitializeAccountProfileOkButtonStatic(state);
    InitializeAccountProfileCancelButtonStatic(state);
    InitializeAccountProfileLocationSelectorStatic(state);
    InitializeAccountProfileBirthYearSelectorStatic(state);
    InitializeAccountProfileSexComboBoxStatic(state);
    InitializeAccountProfilePowerManButtonStatic(state);
    InitializeAccountProfileVelocisButtonStatic(state);
    InitializeAccountProfileRedElfButtonStatic(state);
    InitializeAccountProfileSkeletonButtonStatic(state);
    InitializeAccountProfileAvatarIconButtonStatic(state);
    InitializeAccountProfileBackgroundBitmapStatic(state);
    InitializeAccountProfileTextRecordParserStatic(state);
    InitializeAccountProfileAvatarStripStatic(state);
}

void ReleaseAccountProfileControls(AccountProfileState& state) {
    release_window_resources(state);
}

void InstallAccountProfileAccelerators(AccountProfileState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kAccountProfileAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreAccountProfileAccelerators(AccountProfileState& state) {
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

bool CreateAccountProfileWindow(AccountProfileState& state, HWND parent,
    HINSTANCE instance, LPARAM account_text, LPARAM password_text) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = 0;
    state.selected_avatar_index = 0;
    state.last_player_payload.clear();
    state.last_message.clear();
    state.submitted_account.fill(0);
    state.submitted_password.fill(0);
    state.submitted_confirm_password.fill(0);
    state.submitted_intro.fill(0);

    InitializeAccountProfileControls(state);
    load_window_data(state);
    load_avatar_strip(state);

    FrontendLayoutTableOwner layout_table;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout_table.table,
            kAccountProfileLayoutTrcRecord)) {
        release_window_resources(state);
        return false;
    }
    const std::vector<AccountProfileLayoutRect> layout =
        copy_layout_record(layout_table.table);
    const AccountProfileLayoutRect window_rect = layout_at(layout, 0);
    const POINT origin = RankerFrontendWindowOrigin();
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Account", "Account",
        style, origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        release_window_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(account_profile_window_proc));

    if (!create_text_control(state.account_edit, state.window, instance,
            kAccountEditStyle, kAccountProfileAccountEditId, layout_at(layout, 1)) ||
        !create_text_control(state.password_edit, state.window, instance,
            kPasswordEditStyle, kAccountProfilePasswordEditId, layout_at(layout, 2)) ||
        !create_text_control(state.confirm_password_edit, state.window, instance,
            kPasswordEditStyle, kAccountProfileConfirmPasswordEditId,
            layout_at(layout, 3)) ||
        !create_text_control(state.status_edit, state.window, instance,
            kStatusEditStyle, kAccountProfileStatusEditId, layout_at(layout, 4)) ||
        !create_image_button(state.ok_button, state.window, "",
            kAccountProfileOkButtonId, layout_at(layout, 5), 0xb6, 0xb5) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kAccountProfileCancelButtonId, layout_at(layout, 6), 0xb4, 0xb3) ||
        !create_text_control(state.intro_edit, state.window, instance,
            kIntroEditStyle, kAccountProfileIntroEditId, layout_at(layout, 7)) ||
        !create_selector(state.location_selector, state.window, "Location",
            kAccountProfileLocationSelectorId, layout_at(layout, 8)) ||
        !create_selector(state.birth_year_selector, state.window, "Year of brith",
            kAccountProfileBirthYearSelectorId, layout_at(layout, 9)) ||
        !create_sex_combo(state, state.window, instance, layout_at(layout, 10)) ||
        !create_image_button(state.avatar_buttons[0], state.window, "Power Man",
            kAccountProfileAvatarPowerManButtonId, layout_at(layout, 11), 0xba, 0xbb) ||
        !create_image_button(state.avatar_buttons[1], state.window, "Velocis",
            kAccountProfileAvatarVelocisButtonId, layout_at(layout, 12), 0xbc, 0xbd) ||
        !create_image_button(state.avatar_buttons[2], state.window, "Red Elf",
            kAccountProfileAvatarRedElfButtonId, layout_at(layout, 13), 0xbe, 0xbf) ||
        !create_image_button(state.avatar_buttons[3], state.window, "Skeleton",
            kAccountProfileAvatarSkeletonButtonId, layout_at(layout, 14), 0xc0, 0xc1) ||
        !create_image_button(state.avatar_icon_button, state.window, "Avatar icon",
            kAccountProfileAvatarIconButtonId, layout_at(layout, 15), 0, 0) ||
        !create_text_control(state.stats_edit, state.window, instance,
            kStatusEditStyle, kAccountProfileStatsEditId, layout_at(layout, 16))) {
        destroy_account_window(state);
        return false;
    }

    SendMessageA(state.account_edit.window, EM_LIMITTEXT, 0x1f, 0);
    SendMessageA(state.password_edit.window, EM_LIMITTEXT, 9, 0);
    SendMessageA(state.confirm_password_edit.window, EM_LIMITTEXT, 9, 0);
    SendMessageA(state.intro_edit.window, EM_LIMITTEXT, 0x7f, 0);
    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.status_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.location_selector.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.birth_year_selector.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.sex_combo.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.stats_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    InstallAccountProfileAccelerators(state);
    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kAccountProfileBackgroundBitmapRecord);

    ShowWindow(state.status_edit.window, SW_SHOW);
    ShowWindow(state.stats_edit.window, SW_SHOW);
    set_initial_text(state.account_edit.window, account_text);
    set_initial_text(state.password_edit.window, password_text);

    add_selector_strings(state.location_selector, state.location_items);
    ShowWindow(state.location_selector.window, SW_SHOW);
    add_selector_strings(state.birth_year_selector, state.birth_year_items);
    ShowWindow(state.birth_year_selector.window, SW_SHOW);
    add_combo_strings(state.sex_combo.window, state.sex_items);
    ShowWindow(state.sex_combo.window, SW_SHOW);

    RefreshAccountProfileAvatarStats(state);

    std::vector<u8> status_request(0x0d, 0);
    write_le32(status_request, 0, 3);
    write_le32(status_request, 4, 0x39);
    write_le32(status_request, 8, 0x0d);
    queue_packet(state, status_request.data(), static_cast<i32>(status_request.size()));
    state.visible = true;
    return true;
}

void RefreshAccountProfileAvatarStats(AccountProfileState& state) {
    if (state.selected_avatar_index >= state.avatar_record_ids.size()) {
        state.selected_avatar_index = 0;
    }

    const u32 avatar_record_id =
        state.avatar_record_ids[static_cast<std::size_t>(state.selected_avatar_index)];
    const AccountProfileAvatarStats stats =
        lookup_avatar_stats(state, avatar_record_id);
    char text[256]{};
    std::snprintf(text, sizeof(text), "%s%d\r\n%s%d\r\n%s%d\r\n%s%d",
        state.avatar_stat_labels[0].c_str(), stats.hp,
        state.avatar_stat_labels[1].c_str(), stats.mp,
        state.avatar_stat_labels[2].c_str(), stats.op,
        state.avatar_stat_labels[3].c_str(), stats.dp);
    if (state.stats_edit.window != nullptr) {
        SetWindowTextA(state.stats_edit.window, text);
    }
    redraw_avatar_buttons(state);
}

bool SubmitAccountProfileRequest(AccountProfileState& state) {
    read_window_text(state.account_edit.window, state.submitted_account.data(),
        static_cast<int>(state.submitted_account.size()));
    read_window_text(state.password_edit.window, state.submitted_password.data(),
        10);
    read_window_text(state.confirm_password_edit.window,
        state.submitted_confirm_password.data(), 10);
    read_window_text(state.intro_edit.window, state.submitted_intro.data(),
        static_cast<int>(state.submitted_intro.size()));
    const int sex_index = selected_combo_index(state.sex_combo.window);
    const int location_index =
        GetLegacyStringSelectorSelectedIndex(state.location_selector);
    const int birth_year_index =
        GetLegacyStringSelectorSelectedIndex(state.birth_year_selector);

    if (std::strlen(state.submitted_account.data()) == 0 ||
        std::strlen(state.submitted_password.data()) == 0 ||
        std::strlen(state.submitted_confirm_password.data()) == 0 ||
        std::strlen(state.submitted_intro.data()) == 0 ||
        location_index < 1 || birth_year_index < 1 || sex_index < 1) {
        show_account_message(state,
            startup_message_row(67, "Fill in every required account field."));
        return false;
    }

    if (std::strlen(state.submitted_account.data()) < 4) {
        show_account_message(state,
            startup_message_row(268, "Player name must be at least 4 characters."));
        return false;
    }
    if (std::strlen(state.submitted_password.data()) < 4 ||
        std::strlen(state.submitted_confirm_password.data()) < 4) {
        show_account_message(state,
            startup_message_row(68, "Password must be at least 4 characters."));
        return false;
    }
    if (std::strlen(state.submitted_account.data()) >= 0x21) {
        char text[256]{};
        std::snprintf(text, sizeof(text),
            startup_message_row(85, "Account ID must be %d characters or fewer."),
            0x20);
        show_account_message(state, text);
        return false;
    }
    if (std::strlen(state.submitted_password.data()) >= 0x0b ||
        std::strlen(state.submitted_confirm_password.data()) >= 0x0b) {
        char text[256]{};
        std::snprintf(text, sizeof(text),
            startup_message_row(69, "Password must be %d characters or fewer."), 10);
        show_account_message(state, text);
        return false;
    }
    if (std::strcmp(state.submitted_password.data(),
            state.submitted_confirm_password.data()) != 0) {
        show_account_message(state,
            startup_message_row(70, "Password confirmation does not match."));
        return false;
    }

    char trc_key[16]{};
    if (!BuildTrcRecord10Key(trc_key)) {
        show_account_message(state,
            startup_message_row(21, "Unable to build setup verification key."));
        return false;
    }

    const char* birth_year_text =
        GetLegacyStringSelectorSelectedText(state.birth_year_selector);
    const int birth_year =
        birth_year_text == nullptr ? 0 : std::atoi(birth_year_text);
    const u32 avatar_record_id =
        state.avatar_record_ids[static_cast<std::size_t>(state.selected_avatar_index)];
    const AccountProfileAvatarStats stats =
        lookup_avatar_stats(state, avatar_record_id);

    std::vector<u8> packet(kAccountProfileSubmitPacketBytes, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 3);
    write_le32(packet, 8, kAccountProfileSubmitPacketBytes);
    copy_c_string(packet, 0x0d, 0x20, state.submitted_account.data());
    copy_c_string(packet, 0x2d, 0x20, state.submitted_password.data());
    copy_c_string(packet, 0x4d, 0x10, trc_key);
    copy_fixed_string(packet, 0x5d, 0x20, state.submitted_intro.data());
    write_le32(packet, 0x7d, avatar_record_id);
    write_le32(packet, 0x81, static_cast<u32>(location_index - 1));
    write_le32(packet, 0x85, static_cast<u32>(birth_year));
    write_le32(packet, 0x89, static_cast<u32>(sex_index - 1));
    write_le32(packet, 0x8d, static_cast<u32>(stats.hp));
    write_le32(packet, 0x91, static_cast<u32>(stats.mp));
    write_le32(packet, 0x95, static_cast<u32>(stats.op));
    write_le32(packet, 0x99, static_cast<u32>(stats.dp));
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
    return true;
}

void DispatchAccountProfileNetworkMessage(AccountProfileState& state, WPARAM,
    LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        show_account_message(state,
            startup_message_row(5, "Disconnected from the server."),
            kAccountErrorBlue);
        route_to_connect(state);
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
        const u32 packet_length = read_le32(payload, byte_count, 4);
        if (packet_length == 4 && byte_count >= 0x11) {
            handle_status_code(state, read_le32(payload, byte_count, 0x0d));
        } else if (packet_length == 0x3a && byte_count > 0x0d) {
            SetWindowTextA(state.status_edit.window,
                reinterpret_cast<const char*>(payload + 0x0d));
        } else if (packet_length == 0x59 && byte_count > 0x0d) {
            state.last_player_payload.assign(payload + 0x0d, payload + byte_count);
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
        const u32 packet_bytes = read_le32(payload, byte_count, 8);
        if (packet_bytes < 0x0d ||
            packet_bytes > static_cast<u32>(byte_count)) {
            break;
        }
        const auto packet_count = static_cast<i32>(packet_bytes);
        const u32 packet_length = read_le32(payload, packet_count, 4);
        if (packet_length == 4 && packet_bytes >= 0x11) {
            handle_status_code(state, read_le32(payload, packet_count, 0x0d));
        } else if (packet_length == 0x3a && packet_bytes > 0x0d) {
            SetWindowTextA(state.status_edit.window,
                reinterpret_cast<const char*>(payload + 0x0d));
        } else if (packet_length == 0x59 && packet_bytes > 0x0d) {
            state.last_player_payload.assign(payload + 0x0d, payload + packet_bytes);
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket, packet_count);
        if (state.window == nullptr) {
            return;
        }
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

LRESULT HandleAccountProfileWindowMessage(AccountProfileState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        RestoreAccountProfileAccelerators(state);
        release_window_resources(state);
        state.window = nullptr;
        state.visible = false;
        return 0;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAccountWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAccountBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAccountSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAccountBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAccountSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAccountBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kAccountYellow);
        SetBkColor(reinterpret_cast<HDC>(wparam), kAccountBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kAccountProfileSexComboId) {
            DrawLegacyImageComboBoxItem(state.sex_combo, *draw);
            return TRUE;
        }
        if (draw->CtlID == kAccountProfileOkButtonId ||
            draw->CtlID == kAccountProfileCancelButtonId) {
            if (LegacyImageButtonControl* button =
                    button_for_id(state, static_cast<int>(draw->CtlID))) {
                DrawLegacyImageButtonItem(*button, *draw);
                break;
            }
        }
        if (draw->CtlID >= kAccountProfileAvatarPowerManButtonId &&
            draw->CtlID <= kAccountProfileAvatarSkeletonButtonId) {
            const std::size_t index = static_cast<std::size_t>(
                draw->CtlID - kAccountProfileAvatarPowerManButtonId);
            LegacyImageButtonControl& button = state.avatar_buttons[index];
            const BitmapMemoryResource& bitmap =
                index == state.selected_avatar_index ? button.pressed_bitmap :
                button.normal_bitmap;
            StretchBitmapMemoryResourceToDc(bitmap, draw->hDC, 0, 0);
            break;
        }
        if (draw->CtlID == kAccountProfileAvatarIconButtonId) {
            const u32 frame = state.avatar_record_ids[static_cast<std::size_t>(
                state.selected_avatar_index)];
            DrawRawIndexedBitmapStripFrame(state.avatar_strip, draw->hDC, 0, 0, frame);
            break;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        switch (id) {
        case kAccountProfileOkButtonId:
            HandleDefaultFrontendUiClickSound();
            SubmitAccountProfileRequest(state);
            break;
        case kAccountProfileCancelButtonId:
            HandleDefaultFrontendUiClickSound();
            route_to_wizard_login(state);
            break;
        case kAccountProfileFocusAccountCommandId:
            SetFocus(state.account_edit.window);
            break;
        case kAccountProfileFocusPasswordCommandId:
            SetFocus(state.password_edit.window);
            break;
        case kAccountProfileFocusConfirmCommandId:
            SetFocus(state.confirm_password_edit.window);
            break;
        case kAccountProfileForwardFocusCommandId:
            focus_forward(state);
            return 0;
        case kAccountProfileBackwardFocusCommandId:
            focus_backward(state);
            return 0;
        case kAccountProfileAvatarPowerManButtonId:
        case kAccountProfileAvatarVelocisButtonId:
        case kAccountProfileAvatarRedElfButtonId:
        case kAccountProfileAvatarSkeletonButtonId:
            HandleDefaultFrontendUiClickSound();
            state.selected_avatar_index = static_cast<u32>(
                id - kAccountProfileAvatarPowerManButtonId);
            RefreshAccountProfileAvatarStats(state);
            break;
        default:
            break;
        }
        break;
    }
    case kAccountProfileNetworkMessage:
        DispatchAccountProfileNetworkMessage(state, wparam, lparam);
        break;
    case kAccountProfileReturnMessage:
        route_to_connect(state);
        break;
    case kAccountProfilePromptMessage0:
        ShowOnlineModalPrompt0(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAccountProfilePromptMessage1:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAccountProfilePromptMessage2:
        ShowOnlineModalPrompt2(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAccountProfilePromptMessage3:
        ShowOnlineModalPrompt3(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kAccountProfilePromptEndMessage:
        EndOnlineModalPrompt(online_modal_prompt_state(), static_cast<INT_PTR>(wparam));
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleAccountProfileControlMessage(AccountProfileState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (id == kAccountProfileSexComboId && message == WM_PAINT) {
        PaintLegacyImageComboBoxBackground(state.sex_combo);
    }

    switch (id) {
    case kAccountProfileAccountEditId:
    case kAccountProfilePasswordEditId:
    case kAccountProfileConfirmPasswordEditId:
    case kAccountProfileStatusEditId:
    case kAccountProfileIntroEditId:
    case kAccountProfileSexComboId:
    case kAccountProfileAvatarPowerManButtonId:
    case kAccountProfileAvatarVelocisButtonId:
    case kAccountProfileAvatarRedElfButtonId:
    case kAccountProfileAvatarSkeletonButtonId:
    case kAccountProfileAvatarIconButtonId:
    case kAccountProfileStatsEditId:
    case kAccountProfileOkButtonId:
    case kAccountProfileCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    case kAccountProfileLocationSelectorId:
    case kAccountProfileBirthYearSelectorId:
        if (LegacyStringSelectorControl* selector = selector_for_id(state, id)) {
            return HandleLegacyStringSelectorMessage(*selector, hwnd, message,
                wparam, lparam);
        }
        return 0;
    default:
        return 0;
    }
}

} // namespace ranker

#endif
