#include "ranker_search_lobby.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_indexed_text_table.h"
#include "ranker_player_profile.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <iterator>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kComboStyle =
    WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
constexpr DWORD kListBoxStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr COLORREF kSearchWhite = RGB(255, 255, 255);
constexpr COLORREF kSearchSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kSearchYellow = RGB(255, 255, 0);
constexpr COLORREF kSearchListText = RGB(200, 200, 200);
constexpr COLORREF kSearchBlack = RGB(0, 0, 0);

SearchLobbyState g_search_lobby_state;
std::array<bool, 10> g_control_shutdown_registered{};
bool g_background_shutdown_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK search_lobby_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleSearchLobbyWindowMessage(g_search_lobby_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK search_lobby_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleSearchLobbyControlMessage(g_search_lobby_state, hwnd, message,
        wparam, lparam);
}

void shutdown_global_background() {
    ShutdownSearchLobbyBackgroundBitmap(g_search_lobby_state);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_search_lobby_control_slot(SearchLobbyState& state, std::size_t slot) {
    switch (slot) {
    case 0:
        ShutdownSearchLobbyCategoryCombo(state);
        break;
    case 1:
        ShutdownSearchLobbyBirthYearFromSelector(state);
        break;
    case 2:
        ShutdownSearchLobbyBirthYearToSelector(state);
        break;
    case 3:
        ShutdownSearchLobbyRegionSelector(state);
        break;
    case 4:
        ShutdownSearchLobbyFirstPageButton(state);
        break;
    case 5:
        ShutdownSearchLobbyPreviousPageButton(state);
        break;
    case 6:
        ShutdownSearchLobbyNextPageButton(state);
        break;
    case 7:
        ShutdownSearchLobbyCloseButton(state);
        break;
    case 8:
        ShutdownSearchLobbyAuxiliaryObject0(state);
        break;
    case 9:
        ShutdownSearchLobbyAuxiliaryObject1(state);
        break;
    default:
        break;
    }
}

template <std::size_t Slot>
void shutdown_search_lobby_control_slot() {
    shutdown_search_lobby_control_slot(g_search_lobby_state, Slot);
}

SearchLobbyLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return SearchLobbyLayoutRect{};
}

bool load_search_text_table(u32 record, IndexedTextTableContext& table,
    std::vector<u8>& storage) {
    storage.clear();
    if (!LoadTrcRecordAlloc("Jw2_19.trc", record, storage, 1) || storage.empty()) {
        return false;
    }
    return LoadIndexedTextTableFromMemory(table,
        reinterpret_cast<const char*>(storage.data()));
}

std::vector<std::string> fallback_lines(const char* const* fallback,
    std::size_t fallback_count) {
    std::vector<std::string> lines;
    lines.reserve(fallback_count);
    for (std::size_t i = 0; i < fallback_count; ++i) {
        lines.emplace_back(fallback[i]);
    }
    return lines;
}

std::vector<std::string> read_search_indexed_text_rows(u32 record,
    const char* const* fallback, std::size_t fallback_count) {
    std::vector<u8> bytes;
    IndexedTextTableContext table;
    InitializeIndexedTextTableContext(table);
    if (load_search_text_table(record, table, bytes)) {
        std::vector<std::string> lines;
        lines.reserve(table.rows.size());
        for (const std::string& row : table.rows) {
            if (!row.empty()) {
                lines.push_back(row);
            }
        }
        DestroyIndexedTextTableContext(table);
        if (!lines.empty()) {
            return lines;
        }
    } else {
        DestroyIndexedTextTableContext(table);
    }

    return fallback_lines(fallback, fallback_count);
}

std::vector<std::string> read_search_birth_year_rows(u32 record,
    const char* const* fallback, std::size_t fallback_count) {
    std::vector<u8> bytes;
    IndexedTextTableContext table;
    InitializeIndexedTextTableContext(table);
    if (load_search_text_table(record, table, bytes)) {
        const int first_age = std::atoi(
            std::string(GetIndexedTextTableRow(table, 1)).c_str());
        const int last_age = std::atoi(
            std::string(GetIndexedTextTableRow(table, 2)).c_str());
        DestroyIndexedTextTableContext(table);
        if (first_age <= last_age) {
            std::time_t now = std::time(nullptr);
            std::tm local_time{};
#if defined(_WIN32)
            localtime_s(&local_time, &now);
#else
            std::tm* decoded = std::localtime(&now);
            if (decoded != nullptr) {
                local_time = *decoded;
            }
#endif
            const int current_year = local_time.tm_year + 1900;
            std::vector<std::string> years;
            years.reserve(static_cast<std::size_t>(last_age - first_age + 1));
            char text[16]{};
            for (int age = last_age; age >= first_age; --age) {
                std::snprintf(text, sizeof(text), "%d", current_year - age);
                years.emplace_back(text);
            }
            if (!years.empty()) {
                return years;
            }
        }
    } else {
        DestroyIndexedTextTableContext(table);
    }

    return fallback_lines(fallback, fallback_count);
}

void clear_control(SearchLobbyControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_control(SearchLobbyControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(search_lobby_control_proc));
}

HWND create_child(HWND parent, HINSTANCE instance, const char* class_name,
    const char* text, DWORD style, int id, const SearchLobbyLayoutRect& rect) {
    return CreateWindowExA(0, class_name, text, style, rect.x, rect.y, rect.width,
        rect.height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance, nullptr);
}

bool create_control(SearchLobbyControl& control, HWND parent, HINSTANCE instance,
    const char* class_name, const char* text, DWORD style, int id,
    const SearchLobbyLayoutRect& rect) {
    control.id = id;
    control.window = create_child(parent, instance, class_name, text, style, id, rect);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_control(control);
    return true;
}

bool create_selector(LegacyStringSelectorControl& control, HWND parent,
    const char* text, int id, const SearchLobbyLayoutRect& rect) {
    if (!CreateLegacyStringSelectorWindow(control, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height, 0x0f)) {
        return false;
    }
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(search_lobby_control_proc));
    LoadLegacyStringSelectorIncrementButtonBitmaps(control, 0x114, 0x114);
    LoadLegacyStringSelectorDecrementButtonBitmaps(control, 0x115, 0x115);
    return true;
}

bool create_image_combo(LegacyImageComboBoxControl& combo, HWND parent,
    const char* text, int id, const SearchLobbyLayoutRect& rect,
    u32 normal_record) {
    if (!CreateLegacyImageComboBoxWindow(combo, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), kComboStyle,
            rect.x, rect.y, rect.width, rect.height + 0x96)) {
        return false;
    }
    SetWindowLongPtrA(combo.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(search_lobby_control_proc));
    LoadLegacyImageComboBoxBitmaps(combo, normal_record, 0);
    return true;
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const SearchLobbyLayoutRect& rect,
    u32 normal_record, u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    if (normal_record != 0 || pressed_record != 0) {
        LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    }
    HWND window = GetLegacyImageButtonWindow(button);
    SetWindowLongPtrA(window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(search_lobby_control_proc));
    return true;
}

void add_combo_strings(HWND combo, const std::vector<std::string>& strings) {
    if (combo == nullptr) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    for (const std::string& item : strings) {
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    if (!strings.empty()) {
        SendMessageA(combo, CB_SETCURSEL, 0, 0);
    }
}

void add_selector_strings(LegacyStringSelectorControl& control,
    const std::vector<std::string>& strings) {
    for (const std::string& item : strings) {
        AddLegacyStringSelectorText(control, item.c_str());
    }
}

void add_list_string(HWND list, const std::string& value) {
    if (list != nullptr) {
        SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    }
}

std::string get_combo_string(HWND combo) {
    if (combo == nullptr) {
        return {};
    }
    LRESULT selected = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) {
        char buffer[128]{};
        GetWindowTextA(combo, buffer, static_cast<int>(sizeof(buffer)));
        return buffer;
    }

    char buffer[128]{};
    SendMessageA(combo, CB_GETLBTEXT, static_cast<WPARAM>(selected),
        reinterpret_cast<LPARAM>(buffer));
    return buffer;
}

int get_combo_int(HWND combo) {
    std::string text = get_combo_string(combo);
    if (text.empty()) {
        return 0;
    }
    return std::atoi(text.c_str());
}

int get_selector_int(const LegacyStringSelectorControl& selector) {
    const char* text = GetLegacyStringSelectorSelectedText(selector);
    return text == nullptr ? 0 : std::atoi(text);
}

int get_combo_index(HWND combo) {
    if (combo == nullptr) {
        return 0;
    }
    LRESULT selected = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    return selected == CB_ERR ? 0 : static_cast<int>(selected);
}

int get_selector_index(const LegacyStringSelectorControl& selector) {
    return GetLegacyStringSelectorSelectedIndex(selector);
}

template <std::size_t N>
void write_packet_u32(std::array<u8, N>& packet, std::size_t offset, u32 value) {
    if (offset + sizeof(value) <= packet.size()) {
        std::memcpy(packet.data() + offset, &value, sizeof(value));
    }
}

template <std::size_t N>
void write_packet_text(std::array<u8, N>& packet, std::size_t offset,
    std::size_t byte_count, const char* text) {
    if (offset >= packet.size() || byte_count == 0) {
        return;
    }

    const std::size_t available = std::min<std::size_t>(byte_count,
        packet.size() - offset);
    std::snprintf(reinterpret_cast<char*>(packet.data() + offset), available,
        "%s", text != nullptr ? text : "");
}

template <std::size_t N>
void initialize_search_packet(std::array<u8, N>& packet, u32 opcode) {
    packet.fill(0);
    write_packet_u32(packet, 0, 3);
    write_packet_u32(packet, 4, opcode);
    write_packet_u32(packet, 8, static_cast<u32>(packet.size()));
}

u32 read_packet_u32(const u8* packet, std::size_t byte_count,
    std::size_t offset) {
    if (packet == nullptr || offset + sizeof(u32) > byte_count) {
        return 0;
    }
    return static_cast<u32>(packet[offset]) |
        (static_cast<u32>(packet[offset + 1]) << 8) |
        (static_cast<u32>(packet[offset + 2]) << 16) |
        (static_cast<u32>(packet[offset + 3]) << 24);
}

std::string fixed_packet_string(const u8* packet, std::size_t byte_count,
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

void show_search_message(SearchLobbyState& state, const char* text, COLORREF color) {
    state.last_status_text = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_status_text.c_str(), color);
    }
}

void send_query(SearchLobbyState& state) {
    const SearchLobbyQuery query = BuildSearchLobbyQuery(state);
    if (state.callbacks.queue_packet != nullptr) {
        const auto packet = BuildSearchLobbyQueryPacket(query);
        state.callbacks.queue_packet(state, packet.data(),
            static_cast<i32>(packet.size()));
        return;
    }
    if (state.callbacks.send_query != nullptr) {
        state.callbacks.send_query(state, query);
    }
}

void close_search_window(SearchLobbyState& state, bool return_to_parent) {
    state.visible = false;
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
    if (return_to_parent && state.callbacks.return_to_parent != nullptr) {
        state.callbacks.return_to_parent(state.parent_window, state.instance,
            state.return_context);
    }
}

void handle_result_double_click(SearchLobbyState& state) {
    if (state.name_list.window == nullptr) {
        return;
    }
    LRESULT selected = SendMessageA(state.name_list.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return;
    }

    state.selected_name.fill(0);
    LRESULT copied = SendMessageA(state.name_list.window, LB_GETTEXT,
        static_cast<WPARAM>(selected),
        reinterpret_cast<LPARAM>(state.selected_name.data()));
    if (copied == LB_ERR || state.selected_name[0] == '\0') {
        return;
    }

    if (state.callbacks.queue_packet != nullptr) {
        const auto packet =
            BuildSearchLobbySelectedNamePacket(state.selected_name.data());
        state.callbacks.queue_packet(state, packet.data(),
            static_cast<i32>(packet.size()));
    } else if (state.callbacks.send_selected_name != nullptr) {
        state.callbacks.send_selected_name(state, state.selected_name.data());
    }
}

LegacyStringSelectorControl* selector_for_id(SearchLobbyState& state, int id) {
    if (id == kSearchLobbyBirthYearFromControlId) {
        return &state.birth_year_from_control;
    }
    if (id == kSearchLobbyBirthYearToControlId) {
        return &state.birth_year_to_control;
    }
    if (id == kSearchLobbyRegionControlId) {
        return &state.region_control;
    }
    return nullptr;
}

WNDPROC original_proc_for_id(SearchLobbyState& state, int id) {
    switch (id) {
    case kSearchLobbyCategoryControlId:
        return state.category_control.original_window_proc;
    case kSearchLobbyBirthYearFromControlId:
        return state.birth_year_from_control.original_window_proc;
    case kSearchLobbyBirthYearToControlId:
        return state.birth_year_to_control.original_window_proc;
    case kSearchLobbyRegionControlId:
        return state.region_control.original_window_proc;
    case kSearchLobbyNameListId:
        return state.name_list.original_window_proc;
    case kSearchLobbyCategoryListId:
        return state.category_list.original_window_proc;
    case kSearchLobbyBirthYearListId:
        return state.birth_year_list.original_window_proc;
    case kSearchLobbyRegionListId:
        return state.region_list.original_window_proc;
    case kSearchLobbyFirstPageButtonId:
        return state.first_page_button.original_window_proc;
    case kSearchLobbyPreviousPageButtonId:
        return state.previous_page_button.original_window_proc;
    case kSearchLobbyNextPageButtonId:
        return state.next_page_button.original_window_proc;
    case kSearchLobbyCloseButtonId:
        return state.close_button.original_window_proc;
    default:
        return nullptr;
    }
}

LegacyImageButtonControl* button_for_id(SearchLobbyState& state, int id) {
    switch (id) {
    case kSearchLobbyFirstPageButtonId:
        return &state.first_page_button;
    case kSearchLobbyPreviousPageButtonId:
        return &state.previous_page_button;
    case kSearchLobbyNextPageButtonId:
        return &state.next_page_button;
    case kSearchLobbyCloseButtonId:
        return &state.close_button;
    default:
        return nullptr;
    }
}

void draw_owner_list_item(const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return;
    }

    char text[256]{};
    SendMessageA(draw.hwndItem, LB_GETTEXT, draw.itemID, reinterpret_cast<LPARAM>(text));

    COLORREF background =
        (draw.itemState & ODS_SELECTED) != 0 ? RGB(0, 0, 255) : kSearchBlack;
    FillRect(draw.hDC, &draw.rcItem,
        reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(draw.hDC, kSearchListText);
    SetBkMode(draw.hDC, OPAQUE);
    SetBkColor(draw.hDC, background);
    DrawTextA(draw.hDC, text, -1, const_cast<RECT*>(&draw.rcItem),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

} // namespace

SearchLobbyState& search_lobby_state() {
    return g_search_lobby_state;
}

void InitializeSearchLobbySupport(SearchLobbyState& state) {
    InitializeSearchLobbyCategoryCombo(state);
    RegisterSearchLobbyCategoryComboShutdown(state);
    InitializeSearchLobbyBirthYearFromSelector(state);
    RegisterSearchLobbyBirthYearFromSelectorShutdown(state);
    InitializeSearchLobbyBirthYearToSelector(state);
    RegisterSearchLobbyBirthYearToSelectorShutdown(state);
    InitializeSearchLobbyRegionSelector(state);
    RegisterSearchLobbyRegionSelectorShutdown(state);
    InitializeSearchLobbyFirstPageButton(state);
    RegisterSearchLobbyFirstPageButtonShutdown(state);
    InitializeSearchLobbyPreviousPageButton(state);
    RegisterSearchLobbyPreviousPageButtonShutdown(state);
    InitializeSearchLobbyNextPageButton(state);
    RegisterSearchLobbyNextPageButtonShutdown(state);
    InitializeSearchLobbyCloseButton(state);
    RegisterSearchLobbyCloseButtonShutdown(state);
    InitializeSearchLobbyBackgroundBitmap(state);
    RegisterSearchLobbyBackgroundShutdown(state);
    InitializeSearchLobbyAuxiliaryObject0(state);
    RegisterSearchLobbyAuxiliaryObject0Shutdown(state);
    InitializeSearchLobbyAuxiliaryObject1(state);
    RegisterSearchLobbyAuxiliaryObject1Shutdown(state);
}

#define DEFINE_SEARCH_LOBBY_COMBO_LIFETIME(SupportName, InitName, RegisterName, ShutdownName, Member, Slot) \
    void SupportName() { \
        InitName(g_search_lobby_state); \
        RegisterName(g_search_lobby_state); \
    } \
    void InitName(SearchLobbyState& state) { \
        InitializeLegacyImageComboBoxControl(Member); \
    } \
    void RegisterName(SearchLobbyState&) { \
        register_atexit_once(g_control_shutdown_registered[Slot], \
            shutdown_search_lobby_control_slot<Slot>); \
    } \
    void ShutdownName(SearchLobbyState& state) { \
        DestroyLegacyImageComboBoxControl(Member); \
    }

DEFINE_SEARCH_LOBBY_COMBO_LIFETIME(InitializeSearchLobbyCategoryComboSupport,
    InitializeSearchLobbyCategoryCombo, RegisterSearchLobbyCategoryComboShutdown,
    ShutdownSearchLobbyCategoryCombo, state.category_control, 0)

#undef DEFINE_SEARCH_LOBBY_COMBO_LIFETIME

#define DEFINE_SEARCH_LOBBY_SELECTOR_LIFETIME(SupportName, InitName, RegisterName, ShutdownName, Member, Slot) \
    void SupportName() { \
        InitName(g_search_lobby_state); \
        RegisterName(g_search_lobby_state); \
    } \
    void InitName(SearchLobbyState& state) { \
        InitializeLegacyStringSelectorControl(Member); \
    } \
    void RegisterName(SearchLobbyState&) { \
        register_atexit_once(g_control_shutdown_registered[Slot], \
            shutdown_search_lobby_control_slot<Slot>); \
    } \
    void ShutdownName(SearchLobbyState& state) { \
        DestroyLegacyStringSelectorControl(Member); \
    }

DEFINE_SEARCH_LOBBY_SELECTOR_LIFETIME(
    InitializeSearchLobbyBirthYearFromSelectorSupport,
    InitializeSearchLobbyBirthYearFromSelector,
    RegisterSearchLobbyBirthYearFromSelectorShutdown,
    ShutdownSearchLobbyBirthYearFromSelector, state.birth_year_from_control, 1)
DEFINE_SEARCH_LOBBY_SELECTOR_LIFETIME(
    InitializeSearchLobbyBirthYearToSelectorSupport,
    InitializeSearchLobbyBirthYearToSelector,
    RegisterSearchLobbyBirthYearToSelectorShutdown,
    ShutdownSearchLobbyBirthYearToSelector, state.birth_year_to_control, 2)
DEFINE_SEARCH_LOBBY_SELECTOR_LIFETIME(InitializeSearchLobbyRegionSelectorSupport,
    InitializeSearchLobbyRegionSelector, RegisterSearchLobbyRegionSelectorShutdown,
    ShutdownSearchLobbyRegionSelector, state.region_control, 3)

#undef DEFINE_SEARCH_LOBBY_SELECTOR_LIFETIME

#define DEFINE_SEARCH_LOBBY_BUTTON_LIFETIME(SupportName, InitName, RegisterName, ShutdownName, Member, Slot) \
    void SupportName() { \
        InitName(g_search_lobby_state); \
        RegisterName(g_search_lobby_state); \
    } \
    void InitName(SearchLobbyState& state) { \
        InitializeLegacyImageButtonControl(Member); \
    } \
    void RegisterName(SearchLobbyState&) { \
        register_atexit_once(g_control_shutdown_registered[Slot], \
            shutdown_search_lobby_control_slot<Slot>); \
    } \
    void ShutdownName(SearchLobbyState& state) { \
        DestroyLegacyImageButtonControl(Member); \
    }

DEFINE_SEARCH_LOBBY_BUTTON_LIFETIME(InitializeSearchLobbyFirstPageButtonSupport,
    InitializeSearchLobbyFirstPageButton,
    RegisterSearchLobbyFirstPageButtonShutdown,
    ShutdownSearchLobbyFirstPageButton, state.first_page_button, 4)
DEFINE_SEARCH_LOBBY_BUTTON_LIFETIME(
    InitializeSearchLobbyPreviousPageButtonSupport,
    InitializeSearchLobbyPreviousPageButton,
    RegisterSearchLobbyPreviousPageButtonShutdown,
    ShutdownSearchLobbyPreviousPageButton, state.previous_page_button, 5)
DEFINE_SEARCH_LOBBY_BUTTON_LIFETIME(InitializeSearchLobbyNextPageButtonSupport,
    InitializeSearchLobbyNextPageButton, RegisterSearchLobbyNextPageButtonShutdown,
    ShutdownSearchLobbyNextPageButton, state.next_page_button, 6)
DEFINE_SEARCH_LOBBY_BUTTON_LIFETIME(InitializeSearchLobbyCloseButtonSupport,
    InitializeSearchLobbyCloseButton, RegisterSearchLobbyCloseButtonShutdown,
    ShutdownSearchLobbyCloseButton, state.close_button, 7)

#undef DEFINE_SEARCH_LOBBY_BUTTON_LIFETIME

void InitializeSearchLobbyBackgroundSupport() {
    InitializeSearchLobbyBackgroundBitmap(g_search_lobby_state);
    RegisterSearchLobbyBackgroundShutdown(g_search_lobby_state);
}

void InitializeSearchLobbyBackgroundBitmap(SearchLobbyState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterSearchLobbyBackgroundShutdown(SearchLobbyState&) {
    register_atexit_once(g_background_shutdown_registered, shutdown_global_background);
}

void ShutdownSearchLobbyBackgroundBitmap(SearchLobbyState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

#define DEFINE_SEARCH_LOBBY_AUXILIARY_LIFETIME(SupportName, InitName, RegisterName, ShutdownName, Slot) \
    void SupportName() { \
        InitName(g_search_lobby_state); \
        RegisterName(g_search_lobby_state); \
    } \
    void InitName(SearchLobbyState&) { \
    } \
    void RegisterName(SearchLobbyState&) { \
        register_atexit_once(g_control_shutdown_registered[Slot], \
            shutdown_search_lobby_control_slot<Slot>); \
    } \
    void ShutdownName(SearchLobbyState&) { \
    }

DEFINE_SEARCH_LOBBY_AUXILIARY_LIFETIME(
    InitializeSearchLobbyAuxiliaryObject0Support,
    InitializeSearchLobbyAuxiliaryObject0,
    RegisterSearchLobbyAuxiliaryObject0Shutdown,
    ShutdownSearchLobbyAuxiliaryObject0, 8)
DEFINE_SEARCH_LOBBY_AUXILIARY_LIFETIME(
    InitializeSearchLobbyAuxiliaryObject1Support,
    InitializeSearchLobbyAuxiliaryObject1,
    RegisterSearchLobbyAuxiliaryObject1Shutdown,
    ShutdownSearchLobbyAuxiliaryObject1, 9)

#undef DEFINE_SEARCH_LOBBY_AUXILIARY_LIFETIME

void HandleSearchLobbyNoop() {
}

void InstallSearchLobbyAccelerators(SearchLobbyState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kSearchLobbyAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreSearchLobbyAccelerators(SearchLobbyState& state) {
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

bool CreateSearchLobbyWindow(SearchLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.page = 0;
    state.results.clear();
    state.selected_name.fill(0);
    InitializeSearchLobbySupport(state);

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kSearchLobbyLayoutTrcRecord)) {
        return false;
    }

    const char* fallback_years[] = {"1990", "1995", "2000", "2005"};
    const char* fallback_regions[] = {"All"};
    const char* fallback_categories[] = {"All"};
    state.birth_year_items = read_search_birth_year_rows(
        kSearchLobbyBirthYearTextTrcRecord,
        fallback_years, std::size(fallback_years));
    state.region_items = read_search_indexed_text_rows(kSearchLobbyRegionTextTrcRecord,
        fallback_regions, std::size(fallback_regions));
    state.category_items = read_search_indexed_text_rows(kSearchLobbyCategoryTextTrcRecord,
        fallback_categories, std::size(fallback_categories));

    const SearchLobbyLayoutRect window_rect = layout_at(layout.table, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(
              parent, window_rect.width, window_rect.height)
        : RankerFrontendWindowOrigin();
    const DWORD window_style =
        IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Search", "Search",
        window_style, origin.x, origin.y, window_rect.width,
        window_rect.height, parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(search_lobby_window_proc));

    if (!create_image_combo(state.category_control, state.window, "",
            kSearchLobbyCategoryControlId, layout_at(layout.table, 1),
            kSearchLobbyCategoryComboBitmapRecord) ||
        !create_selector(state.birth_year_from_control, state.window,
            "Year of brith", kSearchLobbyBirthYearFromControlId,
            layout_at(layout.table, 2)) ||
        !create_selector(state.birth_year_to_control, state.window,
            "Year of brith", kSearchLobbyBirthYearToControlId,
            layout_at(layout.table, 3)) ||
        !create_selector(state.region_control, state.window,
            "Year of brith", kSearchLobbyRegionControlId,
            layout_at(layout.table, 4)) ||
        !create_control(state.name_list, state.window, instance, "listbox", nullptr,
            kListBoxStyle, kSearchLobbyNameListId, layout_at(layout.table, 5)) ||
        !create_control(state.category_list, state.window, instance, "listbox",
            nullptr, kListBoxStyle, kSearchLobbyCategoryListId,
            layout_at(layout.table, 6)) ||
        !create_control(state.birth_year_list, state.window, instance, "listbox",
            nullptr, kListBoxStyle, kSearchLobbyBirthYearListId,
            layout_at(layout.table, 7)) ||
        !create_control(state.region_list, state.window, instance, "listbox",
            nullptr, kListBoxStyle, kSearchLobbyRegionListId,
            layout_at(layout.table, 8)) ||
        !create_image_button(state.first_page_button, state.window, "avatar_tab",
            kSearchLobbyFirstPageButtonId, layout_at(layout.table, 9),
            kSearchLobbyFirstPageNormalBitmapRecord,
            kSearchLobbyFirstPagePressedBitmapRecord) ||
        !create_image_button(state.previous_page_button, state.window, "avatar_tab",
            kSearchLobbyPreviousPageButtonId, layout_at(layout.table, 10),
            kSearchLobbyPreviousPageNormalBitmapRecord,
            kSearchLobbyPreviousPagePressedBitmapRecord) ||
        !create_image_button(state.next_page_button, state.window, "avatar_tab",
            kSearchLobbyNextPageButtonId, layout_at(layout.table, 11),
            kSearchLobbyNextPageNormalBitmapRecord,
            kSearchLobbyNextPagePressedBitmapRecord) ||
        !create_image_button(state.close_button, state.window, "avatar_tab",
            kSearchLobbyCloseButtonId, layout_at(layout.table, 12),
            kSearchLobbyCloseNormalBitmapRecord,
            kSearchLobbyClosePressedBitmapRecord)) {
        return false;
    }

    add_combo_strings(state.category_control.window, state.category_items);
    add_selector_strings(state.birth_year_from_control, state.birth_year_items);
    add_selector_strings(state.birth_year_to_control, state.birth_year_items);
    add_selector_strings(state.region_control, state.region_items);
    if (!state.birth_year_items.empty()) {
        SetLegacyStringSelectorSelectedIndex(state.birth_year_to_control,
            static_cast<int>(state.birth_year_items.size() - 1));
    }

    HFONT default_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageA(state.category_control.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.birth_year_from_control.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.birth_year_to_control.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.region_control.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.name_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.category_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.birth_year_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.region_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kSearchLobbyBackgroundBitmapTrcRecord);
    InstallSearchLobbyAccelerators(state);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    state.visible = true;
    return true;
}

LRESULT HandleSearchLobbyWindowMessage(SearchLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        RestoreSearchLobbyAccelerators(state);
        if (state.callbacks.set_busy != nullptr) {
            state.callbacks.set_busy(FALSE);
        }
        ShutdownSearchLobbyBackgroundBitmap(state);
        ShutdownSearchLobbyCategoryCombo(state);
        ShutdownSearchLobbyBirthYearFromSelector(state);
        ShutdownSearchLobbyBirthYearToSelector(state);
        ShutdownSearchLobbyRegionSelector(state);
        clear_control(state.name_list);
        clear_control(state.category_list);
        clear_control(state.birth_year_list);
        clear_control(state.region_list);
        ShutdownSearchLobbyFirstPageButton(state);
        ShutdownSearchLobbyPreviousPageButton(state);
        ShutdownSearchLobbyNextPageButton(state);
        ShutdownSearchLobbyCloseButton(state);
        ShutdownSearchLobbyAuxiliaryObject0(state);
        ShutdownSearchLobbyAuxiliaryObject1(state);
        state.window = nullptr;
        state.visible = false;
        return 0;
    case WM_PAINT:
        if (hwnd == state.window) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            StretchBitmapMemoryResourceToClient(state.background, dc, state.window);
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kSearchSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kSearchBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kSearchWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kSearchBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kSearchYellow);
        SetBkColor(reinterpret_cast<HDC>(wparam), kSearchBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam), kSearchSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kSearchBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID >= kSearchLobbyNameListId &&
            draw->CtlID <= kSearchLobbyRegionListId) {
            draw_owner_list_item(*draw);
            return TRUE;
        }
        if (draw->CtlID == kSearchLobbyCategoryControlId) {
            DrawLegacyImageComboBoxItem(state.category_control, *draw);
            return TRUE;
        }
        LegacyImageButtonControl* button = button_for_id(state, draw->CtlID);
        if (button != nullptr) {
            DrawLegacyImageButtonItem(*button, *draw);
            break;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        switch (id) {
        case kSearchLobbyFirstPageButtonId:
            HandleDefaultFrontendUiClickSound();
            state.page = 0;
            send_query(state);
            return 0;
        case kSearchLobbyPreviousPageButtonId:
            HandleDefaultFrontendUiClickSound();
            if (state.page > 0) {
                --state.page;
                send_query(state);
            }
            return 0;
        case kSearchLobbyNextPageButtonId:
            HandleDefaultFrontendUiClickSound();
            ++state.page;
            send_query(state);
            return 0;
        case kSearchLobbyCloseButtonId:
            HandleDefaultFrontendUiClickSound();
            if (state.callbacks.focus_parent_control != nullptr) {
                state.callbacks.focus_parent_control(state);
            }
            close_search_window(state, false);
            return 0;
        case kSearchLobbyNameListId:
            if (notify == LBN_DBLCLK) {
                handle_result_double_click(state);
                return 0;
            }
            break;
        case kSearchLobbyFocusCommandId:
            return 0;
        default:
            break;
        }
        break;
    }
    case kSearchLobbyNetworkMessage:
        if (LOWORD(lparam) == 1) {
            if (state.callbacks.handle_network_message != nullptr) {
                state.callbacks.handle_network_message(state, wparam, lparam);
            } else if (state.callbacks.forward_network_message != nullptr) {
                state.callbacks.forward_network_message(state.parent_window, message, wparam,
                    lparam);
            } else if (state.parent_window != nullptr && IsWindow(state.parent_window)) {
                PostMessageA(state.parent_window, message, wparam, lparam);
            }
            return 0;
        }
        if (LOWORD(lparam) == 0x20) {
            show_search_message(state,
                startup_message_row(5, "Disconnected from the server."),
                RGB(10, 10, 250));
            close_search_window(state, true);
            return 0;
        }
        break;
    case kSearchLobbyCancelMessage:
        close_search_window(state, true);
        return 0;
    case kSearchLobbyStatusMessage0:
    case kSearchLobbyStatusMessage1:
    case kSearchLobbyStatusMessage2:
    case kSearchLobbyStatusMessage3:
        show_search_message(state, reinterpret_cast<const char*>(wparam),
            static_cast<COLORREF>(lparam));
        return 0;
    case kSearchLobbyBusyMessage:
        if (state.callbacks.set_busy != nullptr) {
            state.callbacks.set_busy(static_cast<BOOL>(wparam));
        }
        return 0;
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleSearchLobbyControlMessage(SearchLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    if (id == kSearchLobbyCategoryControlId && message == WM_PAINT) {
        PaintLegacyImageComboBoxBackground(state.category_control);
    }

    if (LegacyStringSelectorControl* selector = selector_for_id(state, id)) {
        return HandleLegacyStringSelectorMessage(*selector, hwnd, message, wparam,
            lparam);
    }

    switch (id) {
    case kSearchLobbyCategoryControlId:
    case kSearchLobbyNameListId:
    case kSearchLobbyCategoryListId:
    case kSearchLobbyBirthYearListId:
    case kSearchLobbyRegionListId:
    case kSearchLobbyFirstPageButtonId:
    case kSearchLobbyPreviousPageButtonId:
    case kSearchLobbyNextPageButtonId:
    case kSearchLobbyCloseButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

SearchLobbyQuery BuildSearchLobbyQuery(const SearchLobbyState& state) {
    SearchLobbyQuery query;
    query.page = state.page;
    query.category_index = get_combo_index(state.category_control.window);
    query.birth_year_from = get_selector_int(state.birth_year_from_control);
    query.birth_year_to = get_selector_int(state.birth_year_to_control);
    query.region_index = get_selector_index(state.region_control);
    return query;
}

std::array<u8, kSearchLobbyQueryPacketBytes> BuildSearchLobbyQueryPacket(
    const SearchLobbyQuery& query) {
    std::array<u8, kSearchLobbyQueryPacketBytes> packet{};
    initialize_search_packet(packet, 0x83);
    write_packet_u32(packet, 0x0d, static_cast<u32>(query.category_index));
    write_packet_u32(packet, 0x11, static_cast<u32>(query.birth_year_from));
    write_packet_u32(packet, 0x15, static_cast<u32>(query.birth_year_to));
    write_packet_u32(packet, 0x19, static_cast<u32>(query.region_index));
    write_packet_u32(packet, 0x1d, static_cast<u32>(query.page));
    return packet;
}

std::array<u8, kSearchLobbySelectedNamePacketBytes>
BuildSearchLobbySelectedNamePacket(const char* name) {
    std::array<u8, kSearchLobbySelectedNamePacketBytes> packet{};
    initialize_search_packet(packet, 0x37);
    write_packet_text(packet, 0x0d, 0x20, name);
    return packet;
}

void ApplySearchLobbyResults(SearchLobbyState& state,
    const std::vector<SearchLobbyResult>& results) {
    state.results = results;
    if (state.name_list.window != nullptr) {
        SendMessageA(state.name_list.window, LB_RESETCONTENT, 0, 0);
    }
    if (state.category_list.window != nullptr) {
        SendMessageA(state.category_list.window, LB_RESETCONTENT, 0, 0);
    }
    if (state.birth_year_list.window != nullptr) {
        SendMessageA(state.birth_year_list.window, LB_RESETCONTENT, 0, 0);
    }
    if (state.region_list.window != nullptr) {
        SendMessageA(state.region_list.window, LB_RESETCONTENT, 0, 0);
    }

    for (const SearchLobbyResult& result : results) {
        add_list_string(state.name_list.window, result.name);
        add_list_string(state.category_list.window, result.category);
        add_list_string(state.birth_year_list.window, result.birth_year);
        add_list_string(state.region_list.window, result.region);
    }
}

bool ApplySearchLobbyResultPacket(SearchLobbyState& state, const void* packet,
    std::size_t byte_count) {
    const auto* bytes = static_cast<const u8*>(packet);
    if (bytes == nullptr || byte_count < 0x0d ||
        read_packet_u32(bytes, byte_count, 4) != 0x84) {
        return false;
    }

    const u32 count = read_packet_u32(bytes, byte_count, 0x0d);
    if (count == 0) {
        if (state.page > 0) {
            --state.page;
        }
        return true;
    }

    std::vector<SearchLobbyResult> results;
    const std::size_t max_records =
        byte_count > 0x11 ? (byte_count - 0x11) / 0x2c : 0;
    const std::size_t record_count =
        std::min<std::size_t>(count, max_records);
    results.reserve(record_count);

    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
#else
    local_time = *std::localtime(&now);
#endif
    const int base_year = local_time.tm_year + 1901;

    for (std::size_t i = 0; i < record_count; ++i) {
        const std::size_t record = 0x11 + i * 0x2c;
        SearchLobbyResult result;
        result.name = fixed_packet_string(bytes, byte_count, record, 0x20);

        const u32 category_index = read_packet_u32(bytes, byte_count,
            record + 0x20);
        const std::size_t category_row =
            static_cast<std::size_t>(category_index) + 1;
        if (category_row < state.category_items.size()) {
            result.category = state.category_items[category_row];
        } else if (category_index < state.category_items.size()) {
            result.category =
                state.category_items[static_cast<std::size_t>(category_index)];
        }

        const u32 birth_value = read_packet_u32(bytes, byte_count,
            record + 0x24);
        char year_text[16]{};
        std::snprintf(year_text, sizeof(year_text), "%d",
            base_year - static_cast<int>(birth_value));
        result.birth_year = year_text;

        const u32 region_index = read_packet_u32(bytes, byte_count,
            record + 0x28);
        const std::size_t region_row =
            static_cast<std::size_t>(region_index) + 1;
        if (region_row < state.region_items.size()) {
            result.region = state.region_items[region_row];
        } else if (region_index < state.region_items.size()) {
            result.region =
                state.region_items[static_cast<std::size_t>(region_index)];
        }

        results.push_back(std::move(result));
    }

    ApplySearchLobbyResults(state, results);
    return true;
}

bool DispatchSearchLobbyServerPacket(SearchLobbyState& state, const void* packet,
    std::size_t byte_count) {
    const auto* bytes = static_cast<const u8*>(packet);
    if (bytes == nullptr || byte_count < 0x0d) {
        return false;
    }

    const u32 opcode = read_packet_u32(bytes, byte_count, 4);
    switch (opcode) {
    case 0x38: {
        PlayerProfileState& profile = player_profile_state();
        if (profile.window != nullptr && IsWindow(profile.window)) {
            DestroyWindow(profile.window);
        }
        CreatePlayerProfileWindow(profile, state.window, state.instance, packet,
            byte_count, state.selected_name.data(), state.local_account_name.data(),
            state.async_tcp_socket);
        return true;
    }
    case 0x84:
        return ApplySearchLobbyResultPacket(state, packet, byte_count);
    default:
        return false;
    }
}

} // namespace ranker

#endif
