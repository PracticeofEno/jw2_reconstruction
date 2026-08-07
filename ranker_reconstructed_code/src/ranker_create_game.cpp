#include "ranker_create_game.h"

#ifdef _WIN32

#include "ranker_bitmap_icon_collection.h"
#include "ranker_connect_frontend.h"
#include "ranker_directplay.h"
#include "ranker_frontend_layout.h"
#include "ranker_game_session_tables.h"
#include "ranker_gameplay_sound.h"
#include "ranker_ipx_lobby.h"
#include "ranker_link_lobby.h"
#include "ranker_network.h"
#include "ranker_online_dialogs.h"
#include "ranker_p2p_lobby.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = WS_POPUP;
constexpr DWORD kWindowStyleWindowed = WS_CHILD | WS_CLIPCHILDREN |
    WS_CLIPSIBLINGS;
constexpr DWORD kEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kScenarioListStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_SORT | LBS_OWNERDRAWFIXED |
    LBS_HASSTRINGS;
constexpr DWORD kGameTypeComboStyle =
    WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
constexpr DWORD kScreenSizeComboStyle =
    WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
    WS_DISABLED;
constexpr COLORREF kCreateGameWhite = RGB(255, 255, 255);
constexpr COLORREF kCreateGameSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kCreateGameGray = RGB(210, 210, 210);
constexpr COLORREF kCreateGameWarning = RGB(10, 10, 250);
constexpr COLORREF kCreateGameBlack = RGB(0, 0, 0);
constexpr COLORREF kCreateGameSelectedBlue = RGB(0, 0, 255);
constexpr UINT kCreateGameInfoLineFlags =
    DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_MODIFYSTRING;
constexpr UINT kCreateGameInfoBlockFlags =
    DT_NOPREFIX | DT_END_ELLIPSIS | DT_MODIFYSTRING;
constexpr UINT kCreateGameInfoCenteredFlags =
    kCreateGameInfoLineFlags | DT_CENTER | DT_VCENTER;
constexpr std::size_t kCreateGameWizardHostRequestBytes = 0x409;
constexpr std::size_t kCreateGameWizardHostPrefixBytes = 0x12d;
constexpr std::size_t kCreateGameWizardHostNameOffset = 0x0d;
constexpr std::size_t kCreateGameWizardHostPasswordOffset = 0x8d;
constexpr std::size_t kCreateGameWizardHostSockaddrOffset = 0x10d;
constexpr std::size_t kCreateGameWizardHostGameTypeOffset = 0x11d;
constexpr std::size_t kCreateGameWizardHostMapSizeOffset = 0x129;
constexpr std::size_t kCreateGameMapDescriptorTitleOffset = 0x08;
constexpr std::size_t kCreateGameMapDescriptorTitleBytes = 0x20;
constexpr std::size_t kCreateGameMapDescriptorDescriptionOffset = 0x28;
constexpr std::size_t kCreateGameMapDescriptorDescriptionBytes = 0x140;
constexpr std::size_t kCreateGameMapDescriptorPlayerCountOffset = 0x168;
constexpr std::size_t kCreateGameMapDescriptorTerrainTypeOffset = 0x16c;
constexpr std::size_t kCreateGameMapDescriptorSessionGameTypeOffset = 0x170;
constexpr std::size_t kCreateGameMapDescriptorMapWidthOffset = 0x174;
constexpr std::size_t kCreateGameMapDescriptorMapHeightOffset = 0x178;
constexpr std::size_t kCreateGameMapDescriptorTerrainNameOffset = 0x17c;
constexpr std::size_t kCreateGameMapDescriptorTerrainNameBytes = 0x20;
constexpr std::size_t kCreateGameMapDescriptorFileNameOffset = 0x19c;
constexpr std::size_t kCreateGameMapDescriptorFileNameBytes = 0x100;
constexpr std::size_t kCreateGameMapDescriptorFileSizeOffset = 0x29c;
constexpr std::size_t kCreateGameMapDescriptorFileTimeOffset = 0x2a0;
constexpr std::size_t kCreateGameMapDescriptorGameTypeOffset = 0x2a8;
constexpr std::size_t kCreateGameMapDescriptorScreenSizeOffset = 0x2ac;
constexpr std::size_t kCreateGameMapDescriptorPlayerNameOffset = 0x2b0;
constexpr std::size_t kCreateGameMapDescriptorPlayerNameBytes = 0x20;
constexpr std::size_t kCreateGameMapDescriptorTimeTextOffset = 0x2d0;
constexpr std::size_t kCreateGameMapDescriptorTimeTextBytes = 0x0c;
constexpr std::size_t kCreateGameSeedLocalPlayerOffset = 0xde;
constexpr std::size_t kCreateGameSeedMapSelectionOffset = 0x36;
constexpr std::size_t kCreateGameSeedModeCountOffset = 0x3a;
constexpr std::size_t kCreateGameSeedGroupOffsetsOffset = 0x3e;
constexpr std::size_t kCreateGameSeedGroupNameOffset = 0x4e;
constexpr std::size_t kCreateGameSeedGroupColorOffset = 0xce;
constexpr std::size_t kCreateGameSeedMaxPlayersOffset = 0xe2;
constexpr std::size_t kCreateGameSeedRoleValuesOffset = 0xe6;
constexpr std::size_t kCreateGameSeedRoleMasksOffset = 0x106;
constexpr std::size_t kCreateGameSeedTeamValuesOffset = 0x126;
constexpr std::size_t kCreateGameSeedTribeValuesOffset = 0x146;
constexpr std::size_t kCreateGameSeedTribeMasksOffset = 0x166;
constexpr std::size_t kCreateGameSeedGroupNameBytes = 0x20;
constexpr std::size_t kCreateGameForcesRecordBytes = 0x44;
constexpr std::size_t kCreateGameForcesRecordOwnerMaskOffset = 0x20;
constexpr std::size_t kSessionPlayerRecordFactionBaseOffset = 0xa4;
constexpr std::size_t kSessionPlayerRecordSlotStateBaseOffset = 0xf4;
constexpr u32 kCreateGameSeedPlayerSlots = 8;
constexpr u32 kCreateGameSeedGroupCount = 4;
constexpr u32 kCreateGameSeedDefaultTextColor = 0x00fafafa;
constexpr u32 kCreateGameSeedPrimaryGroupY = 0x5e;
constexpr u32 kCreateGameSeedSecondaryGroupY = 0x86;
constexpr u32 kCreateGameSeedGroupRowStep = 0x1b;
constexpr u32 kLegacyTcpPacketType = 3;
constexpr u32 kCreateGameWizardHostRequestOpcode = 0x19;
constexpr u32 kCreateGameWizardHostResponseOpcode = 0x1a;
constexpr std::size_t kStartupCreateGameTypeRowBase = 109;

CreateGameState g_create_game_state;
bool g_background_destructor_registered = false;
std::array<bool, 5> g_bitmap_destructor_registered{};
std::array<bool, 2> g_combo_destructor_registered{};
std::array<bool, 4> g_button_destructor_registered{};
bool g_scroll_destructor_registered = false;
std::array<bool, 2> g_selector_destructor_registered{};
bool g_session_descriptor_destructor_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

const char* const kGameTypes[] = {
    "Top Vs Bottom",
    "Melee",
    "Rank",
    "Avatar Melee",
    "Avatar Rank",
    "Use Map Setting",
    "Melee Observer",
    "Avatar Observer",
    "Relay",
};

enum CreateGameType {
    kGameTypeTopVsBottom = 2,
    kGameTypeFreeForAll = 3,
    kGameTypeUseMapSetting = 4,
    kGameTypeTeamPlay = 5,
    kGameTypeScenario = 6,
    kGameTypeRankGame = 7,
    kGameTypeObserver = 8,
};

const char* const kTerrainTypeFallbacks[] = {
    "Jungle",
    "Desert",
    "Ruin",
    "Winter",
    "Canyon",
    "Swamp",
    "Res3",
    "Res4",
    "Res5",
    "Res6",
    "Res7",
    "Res8",
    "Res9",
};

LRESULT CALLBACK create_game_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleCreateGameWindowMessage(g_create_game_state, hwnd, message, wparam,
        lparam);
}

LRESULT CALLBACK create_game_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleCreateGameControlMessage(g_create_game_state, hwnd, message, wparam,
        lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (registered) {
        return;
    }
    std::atexit(callback);
    registered = true;
}

void shutdown_create_game_background() {
    DestroyCreateGameBackground(g_create_game_state);
}

void shutdown_create_game_up_icon() {
    DestroyCreateGameUpIcon(g_create_game_state);
}

void shutdown_create_game_folder_icon() {
    DestroyCreateGameFolderIcon(g_create_game_state);
}

void shutdown_create_game_open_folder_icon() {
    DestroyCreateGameOpenFolderIcon(g_create_game_state);
}

void shutdown_create_game_game_icon() {
    DestroyCreateGameGameIcon(g_create_game_state);
}

void shutdown_create_game_game_type_combo() {
    DestroyCreateGameGameTypeCombo(g_create_game_state);
}

void shutdown_create_game_screen_size_combo() {
    DestroyCreateGameScreenSizeCombo(g_create_game_state);
}

void shutdown_create_game_scenario_dir_button() {
    DestroyCreateGameScenarioDirButton(g_create_game_state);
}

void shutdown_create_game_map_info_button() {
    DestroyCreateGameMapInfoButton(g_create_game_state);
}

void shutdown_create_game_create_button() {
    DestroyCreateGameCreateButton(g_create_game_state);
}

void shutdown_create_game_scenario_list_scroll() {
    DestroyCreateGameScenarioListScroll(g_create_game_state);
}

void shutdown_create_game_cancel_button() {
    DestroyCreateGameCancelButton(g_create_game_state);
}

void shutdown_create_game_avatar_level_panel() {
    DestroyCreateGameAvatarLevelPanel(g_create_game_state);
}

void shutdown_create_game_avatar_level_start_selector() {
    DestroyCreateGameAvatarLevelStartSelector(g_create_game_state);
}

void shutdown_create_game_avatar_level_end_selector() {
    DestroyCreateGameAvatarLevelEndSelector(g_create_game_state);
}

void shutdown_create_game_session_descriptor() {
    DestroyCreateGameSessionDescriptor(g_create_game_state);
}

std::vector<CreateGameLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<CreateGameLayoutRect> rects;
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

CreateGameLayoutRect layout_at(const CreateGameState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return CreateGameLayoutRect{};
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& target, const char* source) {
    target.fill(0);
    if (source != nullptr) {
        std::strncpy(target.data(), source, target.size() - 1);
    }
}

void write_le32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset + sizeof(value) <= buffer.size()) {
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
    }
}

template <std::size_t N>
void write_le32(std::array<u8, N>& buffer, std::size_t offset, u32 value) {
    if (offset + sizeof(value) <= buffer.size()) {
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
    }
}

u32 read_le32(const u8* buffer, i32 byte_count, std::size_t offset) {
    if (buffer == nullptr || byte_count < 0 ||
        offset + sizeof(u32) > static_cast<std::size_t>(byte_count)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, buffer + offset, sizeof(value));
    return value;
}

template <std::size_t N>
u32 read_le32(const std::array<u8, N>& buffer, std::size_t offset) {
    if (offset + sizeof(u32) > buffer.size()) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    return value;
}

u32 read_le32(const std::vector<u8>& buffer, std::size_t offset) {
    if (offset + sizeof(u32) > buffer.size()) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    return value;
}

void copy_packet_text(std::vector<u8>& packet, std::size_t offset,
    std::size_t byte_count, const char* text) {
    if (offset >= packet.size() || byte_count == 0) {
        return;
    }
    const std::size_t available =
        std::min<std::size_t>(byte_count, packet.size() - offset);
    std::snprintf(reinterpret_cast<char*>(packet.data() + offset), available,
        "%s", text != nullptr ? text : "");
}

template <std::size_t N>
void copy_seed_text(std::array<u8, N>& payload, std::size_t offset,
    std::size_t byte_count, const char* text) {
    if (offset >= payload.size() || byte_count == 0) {
        return;
    }
    const std::size_t available =
        std::min<std::size_t>(byte_count, payload.size() - offset);
    std::memset(payload.data() + offset, 0, available);
    if (available > 1 && text != nullptr) {
        std::snprintf(reinterpret_cast<char*>(payload.data() + offset), available,
            "%s", text);
    }
}

template <std::size_t N>
void copy_seed_fixed_text(std::array<u8, N>& payload, std::size_t offset,
    std::size_t byte_count, const u8* text, std::size_t text_bytes,
    const char* fallback) {
    if (offset >= payload.size() || byte_count == 0) {
        return;
    }
    const std::size_t available =
        std::min<std::size_t>(byte_count, payload.size() - offset);
    std::memset(payload.data() + offset, 0, available);
    if (available <= 1) {
        return;
    }
    std::size_t copied = 0;
    if (text != nullptr) {
        const std::size_t limit = std::min<std::size_t>(available - 1, text_bytes);
        while (copied < limit && text[copied] != 0) {
            payload[offset + copied] = text[copied];
            ++copied;
        }
    }
    if (copied == 0 && fallback != nullptr) {
        std::snprintf(reinterpret_cast<char*>(payload.data() + offset), available,
            "%s", fallback);
    }
}

void set_seed_group_header(CreateGameState& state, u32 group, u32 y,
    const char* name) {
    if (group >= kCreateGameSeedGroupCount) {
        return;
    }
    write_le32(state.session_seed_payload,
        kCreateGameSeedGroupOffsetsOffset + group * sizeof(u32), y);
    write_le32(state.session_seed_payload,
        kCreateGameSeedGroupColorOffset + group * sizeof(u32),
        kCreateGameSeedDefaultTextColor);
    copy_seed_text(state.session_seed_payload,
        kCreateGameSeedGroupNameOffset + group * kCreateGameSeedGroupNameBytes,
        kCreateGameSeedGroupNameBytes, name);
}

void clear_seed_role_mask_bit(CreateGameState& state, u32 bit) {
    for (u32 slot = 0; slot < kCreateGameSeedPlayerSlots; ++slot) {
        const std::size_t offset =
            kCreateGameSeedRoleMasksOffset + slot * sizeof(u32);
        write_le32(state.session_seed_payload, offset,
            read_le32(state.session_seed_payload, offset) & ~bit);
    }
}

u32 selected_session_player_value(const CreateGameState& state,
    std::size_t base_offset, u32 slot) {
    return read_le32(state.selected_session.raw_player_record,
        base_offset + slot * sizeof(u32));
}

u32 selected_session_slot_state(const CreateGameState& state, u32 slot) {
    return selected_session_player_value(state,
        kSessionPlayerRecordSlotStateBaseOffset, slot);
}

u32 selected_session_faction(const CreateGameState& state, u32 slot) {
    const u32 faction = selected_session_player_value(state,
        kSessionPlayerRecordFactionBaseOffset, slot);
    return faction <= 4 ? faction : 4;
}

std::string selected_session_archive_tail(const CreateGameState& state) {
    std::string path = state.selected_session.archive_path.data();
    const std::string& base = state.base_maps_directory;
    if (!base.empty() && path.size() >= base.size() &&
        std::equal(base.begin(), base.end(), path.begin())) {
        return path.substr(base.size());
    }
    return path;
}

void copy_current_create_game_time_text(CreateGameState& state) {
    char time_text[kCreateGameMapDescriptorTimeTextBytes]{};
    GetTimeFormatA(0, TIME_NOSECONDS, nullptr, "h':'mm tt", time_text,
        static_cast<int>(sizeof(time_text)));
    copy_seed_text(state.map_descriptor_payload,
        kCreateGameMapDescriptorTimeTextOffset,
        kCreateGameMapDescriptorTimeTextBytes, time_text);
}

bool load_selected_session_forces_record(const CreateGameState& state,
    std::vector<u8>& fixed44_records) {
    fixed44_records.clear();
    if (state.selected_session.archive_path[0] == '\0') {
        return false;
    }
    if (LoadTrcRecordAlloc(state.selected_session.archive_path.data(),
            kSessionRuntimeUserRecordFirstIndex, fixed44_records) &&
        fixed44_records.size() >= kSessionRuntimeForcesFixedRecordBytes) {
        return true;
    }
    fixed44_records.clear();
    return LoadTrcRecordAlloc(state.selected_session.archive_path.data(),
        kSessionRuntimeForcesArchiveRecordIndex, fixed44_records) &&
        fixed44_records.size() >= kSessionRuntimeForcesFixedRecordBytes;
}

void clear_control(CreateGameTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void destroy_control(CreateGameTextControl& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    clear_control(control);
}

void subclass_window(HWND hwnd, WNDPROC& original) {
    if (hwnd == nullptr) {
        return;
    }
    original = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC));
    SetWindowLongPtrA(hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(create_game_control_proc));
}

bool create_text_control(CreateGameTextControl& control, HWND parent,
    HINSTANCE instance, const char* class_name, DWORD style, int id,
    const CreateGameLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_window(control.window, control.original_window_proc);
    return true;
}

void subclass_button(LegacyImageButtonControl& button) {
    if (button.window != nullptr) {
        SetWindowLongPtrA(button.window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(create_game_control_proc));
    }
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const CreateGameLayoutRect& rect, u32 normal_record,
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

bool create_combo(LegacyImageComboBoxControl& combo, HWND parent, const char* text,
    int id, DWORD style, const CreateGameLayoutRect& rect, u32 bitmap_record) {
    if (!CreateLegacyImageComboBoxWindow(combo, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), style, rect.x,
            rect.y, rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageComboBoxBitmaps(combo, bitmap_record, 0);
    SetWindowLongPtrA(combo.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(create_game_control_proc));
    return true;
}

bool create_selector(LegacyStringSelectorControl& selector, HWND parent,
    const char* text, int id, const CreateGameLayoutRect& rect) {
    InitializeLegacyStringSelectorControl(selector);
    if (!CreateLegacyStringSelectorWindow(selector, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height, 0x0f)) {
        return false;
    }
    LoadLegacyStringSelectorIncrementButtonBitmaps(selector, 0x114, 0x114);
    LoadLegacyStringSelectorDecrementButtonBitmaps(selector, 0x115, 0x115);
    SetWindowLongPtrA(selector.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(create_game_control_proc));
    return true;
}

LegacyImageButtonControl* button_for_id(CreateGameState& state, int id) {
    switch (id) {
    case kCreateGameScenarioDirButtonId:
        return &state.scenario_dir_button;
    case kCreateGameMapInfoButtonId:
        return &state.map_info_button;
    case kCreateGameCreateButtonId:
        return &state.create_button;
    case kCreateGameCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

WNDPROC original_proc_for_id(CreateGameState& state, int id) {
    switch (id) {
    case kCreateGameNameEditId:
        return state.name_edit.original_window_proc;
    case kCreateGamePasswordEditId:
        return state.password_edit.original_window_proc;
    case kCreateGameScenarioListId:
        return state.scenario_list.original_window_proc;
    case kCreateGameScrollControlId:
        return state.scroll.original_window_proc;
    case kCreateGameGameTypeComboId:
        return state.game_type_combo.original_window_proc;
    case kCreateGameScreenSizeComboId:
        return state.screen_size_combo.original_window_proc;
    default:
        if (LegacyImageButtonControl* button = button_for_id(state, id)) {
            return button->original_window_proc;
        }
        return nullptr;
    }
}

LegacyStringSelectorControl* selector_for_id(CreateGameState& state, int id) {
    if (id == kCreateGameAvatarLevelStartSelectorId) {
        return &state.avatar_level_start_selector;
    }
    if (id == kCreateGameAvatarLevelEndSelectorId) {
        return &state.avatar_level_end_selector;
    }
    return nullptr;
}

bool equals_ignore_case(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

std::string full_path(const std::string& path) {
    std::array<char, MAX_PATH> buffer{};
    if (GetFullPathNameA(path.c_str(), static_cast<DWORD>(buffer.size()),
            buffer.data(), nullptr) == 0) {
        return path;
    }
    return buffer.data();
}

std::string append_path(const std::string& base, const char* leaf) {
    if (base.empty()) {
        return leaf == nullptr ? std::string{} : std::string(leaf);
    }
    std::string result = base;
    if (result.back() != '\\' && result.back() != '/') {
        result.push_back('\\');
    }
    if (leaf != nullptr) {
        result += leaf;
    }
    return result;
}

bool is_directory(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string resolve_maps_directory() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    fs::path current = fs::current_path();
    for (int i = 0; i < 7 && !current.empty(); ++i) {
        roots.push_back(current);
        current = current.parent_path();
    }

    for (const fs::path& root : roots) {
        const fs::path direct = root / "Maps";
        if (fs::is_directory(direct)) {
            return full_path(direct.string());
        }
        const fs::path nested = root / "ranker" / "Maps";
        if (fs::is_directory(nested)) {
            return full_path(nested.string());
        }
    }
    return full_path("Maps");
}

bool is_trk_file(const char* name) {
    if (name == nullptr) {
        return false;
    }
    std::string value = name;
    const std::size_t dot = value.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = value.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return ext == ".TRK";
}

bool should_skip_file(DWORD attributes) {
    constexpr DWORD kSkipped =
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY |
        FILE_ATTRIBUTE_OFFLINE;
    return (attributes & kSkipped) != 0;
}

void add_scenario_entry(CreateGameState& state, const char* name,
    const std::string& path, DWORD attributes, bool directory, bool parent) {
    CreateGameScenarioEntry entry;
    copy_c_string(entry.name, name);
    copy_c_string(entry.path, path.c_str());
    entry.attributes = attributes;
    entry.directory = directory;
    entry.parent = parent;
    state.scenario_entries.push_back(entry);

    std::string display = directory && !parent ? "[" + std::string(name) + "]" :
        std::string(name);
    const LRESULT row = SendMessageA(state.scenario_list.window, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(display.c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
        SendMessageA(state.scenario_list.window, LB_SETITEMDATA,
            static_cast<WPARAM>(row),
            static_cast<LPARAM>(state.scenario_entries.size() - 1));
    }
}

void update_scroll(CreateGameState& state, int top_index = -1) {
    if (state.scenario_list.window == nullptr) {
        return;
    }
    const int count = static_cast<int>(
        SendMessageA(state.scenario_list.window, LB_GETCOUNT, 0, 0));
    if (top_index < 0) {
        top_index = static_cast<int>(
            SendMessageA(state.scenario_list.window, LB_GETTOPINDEX, 0, 0));
    }
    const int max_top = std::max(0, count - state.visible_rows);
    SetLegacyCustomScrollControlVisible(state.scroll, count > state.visible_rows);
    SetLegacyCustomScrollControlRange(state.scroll, 0, max_top, true);
    SetLegacyCustomScrollControlValue(state.scroll, std::min(top_index, max_top), true);
}

CreateGameScenarioEntry* selected_entry(CreateGameState& state) {
    if (state.scenario_list.window == nullptr) {
        return nullptr;
    }
    const LRESULT selected =
        SendMessageA(state.scenario_list.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return nullptr;
    }
    const LRESULT data = SendMessageA(state.scenario_list.window, LB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    if (data == LB_ERR || data < 0 ||
        static_cast<std::size_t>(data) >= state.scenario_entries.size()) {
        return nullptr;
    }
    return &state.scenario_entries[static_cast<std::size_t>(data)];
}

void set_message(CreateGameState& state, const char* message, COLORREF color) {
    state.last_message = message == nullptr ? "" : message;
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
    }
}

const char* startup_message_row(std::size_t index, const char* fallback) {
    const auto& rows = startup_text_tables().message_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

const char* terrain_type_name(u32 terrain_type) {
    if (terrain_type < std::size(kTerrainTypeFallbacks)) {
        return startup_message_row(161 + terrain_type,
            kTerrainTypeFallbacks[terrain_type]);
    }
    return startup_message_row(177, "Custom");
}

int draw_create_game_info_text(HDC dc, RECT& rect, const char* text, UINT flags) {
    char line[256]{};
    std::snprintf(line, sizeof(line), "%s", text != nullptr ? text : "");
    return std::max(DrawTextA(dc, line, -1, &rect, flags), 0);
}

void set_startup_message(CreateGameState& state, std::size_t index,
    const char* fallback, COLORREF color) {
    set_message(state, startup_message_row(index, fallback), color);
}

void play_click_sound(CreateGameState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

void populate_combo(HWND combo, const char* const* values, std::size_t count,
    int selected) {
    if (combo == nullptr) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < count; ++i) {
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(values[i]));
    }
    SendMessageA(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}

void populate_game_type_combo(HWND combo, int selected) {
    if (combo == nullptr) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    for (std::size_t index = 0; index < std::size(kGameTypes); ++index) {
        SendMessageA(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(startup_message_row(
                kStartupCreateGameTypeRowBase + index, kGameTypes[index])));
    }
    SendMessageA(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}

bool is_game_type_allowed_for_mode(int mode, int game_type) {
    switch (mode) {
    case 1:
        return game_type != kGameTypeTopVsBottom &&
            game_type != kGameTypeUseMapSetting &&
            game_type != kGameTypeFreeForAll &&
            game_type != kGameTypeRankGame;
    case 3:
    case 6:
        return game_type != kGameTypeTopVsBottom &&
            game_type != kGameTypeUseMapSetting &&
            game_type != kGameTypeFreeForAll &&
            game_type != kGameTypeRankGame &&
            game_type != kGameTypeObserver;
    default:
        return true;
    }
}

bool requires_avatar_production_gate(int game_type) {
    return game_type > 2 && (game_type < 5 || game_type == kGameTypeRankGame);
}

bool validate_selected_online_rank_map_type(CreateGameState& state) {
    if (state.selected_session.session_game_type == 2) {
        return true;
    }

    set_startup_message(state, 101,
        "The selected map cannot be used with this game type.",
        kCreateGameWarning);
    SetFocus(state.name_edit.window);
    return false;
}

bool validate_online_server_counts(CreateGameState& state) {
    if (state.mode != 0 && state.mode != 2) {
        return true;
    }

    if (state.game_type == kGameTypeTopVsBottom) {
        const u32 count =
            state.server_top_bottom_counts[0] + state.server_top_bottom_counts[1];
        if (count < 10) {
            set_startup_message(state, 0,
                "Rank games require at least ten normal games of this type.",
                kCreateGameSoftWhite);
            SetFocus(state.name_edit.window);
            return false;
        }
        if (!validate_selected_online_rank_map_type(state)) {
            return false;
        }
    }

    if (state.game_type == kGameTypeUseMapSetting) {
        const u32 count =
            state.server_use_map_counts[0] + state.server_use_map_counts[1];
        if (count < 10) {
            set_startup_message(state, 0,
                "Rank games require at least ten normal games of this type.",
                kCreateGameSoftWhite);
            SetFocus(state.name_edit.window);
            return false;
        }
        if (!validate_selected_online_rank_map_type(state)) {
            return false;
        }
    }

    return true;
}

u16 create_game_advertised_tcp_port() {
    ConnectFrontendState& connect = connect_frontend_state();
    LoadConnectFrontendConfiguration(connect);
    if (connect.configuration.p2p_tcp_port != 0) {
        return static_cast<u16>(connect.configuration.p2p_tcp_port);
    }
    return static_cast<u16>(connect.configuration.free_server_port);
}

void resolve_create_game_advertised_address(CreateGameState& state,
    char* address, std::size_t address_size) {
    if (address == nullptr || address_size == 0) {
        return;
    }
    address[0] = '\0';

    sockaddr_in local{};
    if (state.async_tcp_socket != nullptr &&
        CopyLegacyAsyncTcpLocalSockaddr(*state.async_tcp_socket, local)) {
        const char* text = inet_ntoa(local.sin_addr);
        if (text != nullptr) {
            std::snprintf(address, address_size, "%s", text);
            return;
        }
    }

    char host_name[0x100]{};
    if (ResolveLocalHostDisplayAddress(host_name, sizeof(host_name), address,
            static_cast<u32>(address_size))) {
        return;
    }
    std::snprintf(address, address_size, "0.0.0.0");
}

bool queue_create_game_packet(CreateGameState& state, std::vector<u8>& packet) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet.data(),
            static_cast<i32>(packet.size()));
        return true;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket, packet.data(),
            static_cast<i32>(packet.size()), nullptr);
        return true;
    }
    set_startup_message(state, 55, "Create game failed: TCP/IP error.",
        kCreateGameSoftWhite);
    return false;
}

bool SubmitCreateGameWizardHostRequest(CreateGameState& state) {
    std::vector<u8> packet(kCreateGameWizardHostRequestBytes, 0);
    write_le32(packet, 0, kLegacyTcpPacketType);
    write_le32(packet, 4, kCreateGameWizardHostRequestOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    copy_packet_text(packet, kCreateGameWizardHostNameOffset, 0x80,
        state.game_name.data());
    copy_packet_text(packet, kCreateGameWizardHostPasswordOffset, 0x80,
        state.password.data());

    char address[0x100]{};
    resolve_create_game_advertised_address(state, address, sizeof(address));
    const sockaddr_in advertised =
        BuildIpv4Sockaddr(address, create_game_advertised_tcp_port());
    std::memcpy(packet.data() + kCreateGameWizardHostSockaddrOffset, &advertised,
        sizeof(advertised));
    write_le32(packet, kCreateGameWizardHostGameTypeOffset,
        static_cast<u32>(state.game_type));
    write_le32(packet, kCreateGameWizardHostMapSizeOffset,
        kCreateGameMapDescriptorBytes);
    std::memcpy(packet.data() + kCreateGameWizardHostPrefixBytes,
        state.map_descriptor_payload.data(), state.map_descriptor_payload.size());

    return queue_create_game_packet(state, packet);
}

bool launch_create_game_link_lobby(CreateGameState& state) {
    if (state.callbacks.open_link_lobby != nullptr) {
        state.callbacks.open_link_lobby(state);
        return true;
    }

    HWND owner = state.main_window;
    HINSTANCE instance = state.instance;
    LPARAM return_context = state.return_context;
    const int mode = state.mode;
    HWND old_window = state.window;
    if (old_window != nullptr && IsWindow(old_window)) {
        DestroyWindow(old_window);
    }
    if (owner != nullptr && IsWindow(owner)) {
        RedrawWindow(owner, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
    CreateLinkLobbyWindow(link_lobby_state(), owner, instance, 0,
        reinterpret_cast<LPARAM>(state.map_descriptor_payload.data()),
        reinterpret_cast<LPARAM>(state.session_seed_payload.data()), mode,
        return_context, state.game_type, state.screen_size);
    return true;
}

void return_to_connect_after_server_close(CreateGameState& state, HWND hwnd) {
    const char* message = startup_message_row(5, "Disconnected from the server.");
    ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd, message,
        kCreateGameWarning);
    state.last_message = message;
    if (state.async_tcp_socket != nullptr) {
        CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
    }

    HWND parent = state.parent_window;
    const bool destroy_parent = state.mode == 0 || state.mode == 2;
    DestroyWindow(hwnd);
    if (destroy_parent && parent != nullptr && IsWindow(parent)) {
        DestroyWindow(parent);
    }

    if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    } else {
        CreateConnectFrontendWindow(connect_frontend_state(), state.main_window,
            state.instance, state.return_context);
    }
}

bool handle_create_game_server_packet(CreateGameState& state, HWND hwnd,
    WPARAM wparam, LPARAM lparam, const u8* payload, i32 packet_bytes,
    bool& stop_after_packet, bool& handled) {
    stop_after_packet = false;
    handled = true;
    const u32 opcode = read_le32(payload, packet_bytes, 4);
    switch (opcode) {
    case kCreateGameWizardHostResponseOpcode: {
        const u32 status = read_le32(payload, packet_bytes, 0x0d);
        stop_after_packet = true;
        if (status == 1) {
            return true;
        }
        if (status == 0) {
            PostMessageA(hwnd, kCreateGameDuplicateNamePromptMessage, 0, 0);
        }
        return false;
    }
    case 0x3e:
        state.server_top_bottom_counts[0] = read_le32(payload, packet_bytes, 0x0d);
        state.server_top_bottom_counts[1] = read_le32(payload, packet_bytes, 0x11);
        state.server_top_bottom_counts[2] = read_le32(payload, packet_bytes, 0x15);
        return false;
    case 0x46:
        state.server_game_type_counts[0] = read_le32(payload, packet_bytes, 0x0d);
        state.server_game_type_counts[1] = read_le32(payload, packet_bytes, 0x11);
        state.server_game_type_counts[2] = read_le32(payload, packet_bytes, 0x15);
        state.server_game_type_counts[3] = read_le32(payload, packet_bytes, 0x19);
        state.server_game_type_counts[4] = read_le32(payload, packet_bytes, 0x1d);
        return false;
    case 100:
        state.server_use_map_counts[0] = read_le32(payload, packet_bytes, 0x15);
        state.server_use_map_counts[1] = read_le32(payload, packet_bytes, 0x19);
        state.server_use_map_counts[2] = read_le32(payload, packet_bytes, 0x1d);
        state.server_use_map_counts[3] = read_le32(payload, packet_bytes, 0x0d);
        state.server_use_map_counts[4] = read_le32(payload, packet_bytes, 0x11);
        state.server_use_map_counts[5] = read_le32(payload, packet_bytes, 0x29);
        state.server_use_map_counts[6] = read_le32(payload, packet_bytes, 0x2d);
        state.server_use_map_counts[7] = read_le32(payload, packet_bytes, 0x31);
        state.server_use_map_counts[8] = read_le32(payload, packet_bytes, 0x21);
        state.server_use_map_counts[9] = read_le32(payload, packet_bytes, 0x25);
        return false;
    default:
        handled = false;
        if (state.parent_window != nullptr && state.parent_window != hwnd &&
            IsWindow(state.parent_window)) {
            PostMessageA(state.parent_window, kCreateGameNetworkMessage, wparam,
                lparam);
        }
        return false;
    }
}

void DispatchCreateGameNetworkMessage(CreateGameState& state, HWND hwnd,
    WPARAM wparam, LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        return_to_connect_after_server_close(state, hwnd);
        return;
    }
    if (event != FD_READ && event != 1) {
        return;
    }

    if (state.async_tcp_socket != nullptr) {
        ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
    }
    if (state.async_tcp_socket == nullptr) {
        return;
    }

    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= 0x0d) {
        const u32 packet_bytes = read_le32(payload, byte_count, 8);
        if (packet_bytes < 0x0d ||
            packet_bytes > static_cast<u32>(byte_count)) {
            break;
        }

        bool stop_after_packet = false;
        bool handled = false;
        const bool launch_link_lobby = handle_create_game_server_packet(state, hwnd,
            wparam, lparam, payload, static_cast<i32>(packet_bytes),
            stop_after_packet, handled);
        if (!handled) {
            return;
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
            static_cast<i32>(packet_bytes));
        if (launch_link_lobby) {
            launch_create_game_link_lobby(state);
            return;
        }
        if (stop_after_packet) {
            return;
        }

        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

void populate_selector(LegacyStringSelectorControl& selector, int start,
    int count, int step) {
    char text[32]{};
    for (int i = 0; i < count; ++i) {
        std::snprintf(text, sizeof(text), "%d", start + i * step);
        AddLegacyStringSelectorText(selector, text);
    }
    SetLegacyStringSelectorSelectedIndex(selector, 0);
}

void read_entry_selection(CreateGameState& state) {
    const CreateGameScenarioEntry* entry = selected_entry(state);
    state.selected_entry_index = -1;
    state.selected_session_valid = false;
    if (entry == nullptr || entry->directory) {
        DestroyCreateGameSessionDescriptor(state);
        InitializeCreateGameSessionDescriptor(state);
        return;
    }

    DestroyCreateGameSessionDescriptor(state);
    state.selected_session_valid =
        LoadWizardSessionArchiveDescriptor(state.selected_session, entry->path.data());
}

void draw_scenario_item(CreateGameState& state, const DRAWITEMSTRUCT& item) {
    if (item.itemID == static_cast<UINT>(-1)) {
        return;
    }
    if (item.itemData >= state.scenario_entries.size()) {
        return;
    }
    const CreateGameScenarioEntry& entry = state.scenario_entries[item.itemData];

    RECT fill = item.rcItem;
    SetBkColor(item.hDC,
        (item.itemState & ODS_SELECTED) != 0 ? kCreateGameSelectedBlue :
        kCreateGameBlack);
    ExtTextOutA(item.hDC, fill.left, fill.top, ETO_OPAQUE, &fill, nullptr, 0,
        nullptr);

    RECT rect = item.rcItem;
    rect.left += 4;
    rect.top += 2;
    BitmapMemoryResource* icon = nullptr;
    if (entry.directory) {
        icon = entry.parent ? &state.up_icon : &state.folder_icon;
    } else {
        icon = &state.game_icon;
    }
    if (icon != nullptr) {
        StretchBitmapMemoryResourceToDc(*icon, item.hDC, rect.left, rect.top);
        rect.left += 22;
    }

    SetTextColor(item.hDC, kCreateGameWhite);
    SetBkMode(item.hDC, TRANSPARENT);
    DrawTextA(item.hDC, entry.name.data(), -1, &rect,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

void draw_scenario_dir(CreateGameState& state, const DRAWITEMSTRUCT& item) {
    RECT rect = item.rcItem;
    SetBkColor(item.hDC, kCreateGameBlack);
    ExtTextOutA(item.hDC, rect.left, rect.top, ETO_OPAQUE, &item.rcItem, nullptr, 0,
        nullptr);
    StretchBitmapMemoryResourceToDc(state.open_folder_icon, item.hDC, 4, 2);
    rect.left += 24;
    SetTextColor(item.hDC, kCreateGameWhite);
    SetBkMode(item.hDC, TRANSPARENT);
    DrawTextA(item.hDC, state.current_directory.c_str(), -1, &rect,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void draw_map_info(CreateGameState& state, const DRAWITEMSTRUCT& item) {
    RECT rect = item.rcItem;
    SetBkColor(item.hDC, kCreateGameBlack);
    ExtTextOutA(item.hDC, rect.left, rect.top, ETO_OPAQUE, &item.rcItem, nullptr, 0,
        nullptr);
    rect.left += 5;
    rect.top += 8;
    rect.right -= 5;
    SetBkMode(item.hDC, TRANSPARENT);

    if (!state.selected_session_valid) {
        SetTextColor(item.hDC, kCreateGameWarning);
        draw_create_game_info_text(item.hDC, rect,
            startup_message_row(60, "No game map selected."),
            kCreateGameInfoCenteredFlags);
        return;
    }

    const WizardSessionArchiveDescriptor& descriptor = state.selected_session;
    if (descriptor.session_game_type != 0) {
        BitmapIconResourceCollection& icons = GlobalBitmapIconResourceCollection();
        if (icons.loaded_count == 0) {
            LoadDefaultIconBitmapSet(icons);
        }
        BitmapMemoryResource& icon = GetBitmapIconSlotOrDefault(icons,
            descriptor.session_game_type + 0x0e);
        StretchBitmapMemoryResourceToDc(icon, item.hDC, rect.left, rect.top - 3);
        rect.left += 34;
    }
    SetTextColor(item.hDC, kCreateGameGray);

    char line[256]{};
    std::snprintf(line, sizeof(line), "%s", descriptor.title.data());
    const int title_height = draw_create_game_info_text(item.hDC, rect, line,
        kCreateGameInfoLineFlags);
    if (descriptor.session_game_type != 0) {
        rect.left -= 34;
    }

    rect.top += 7 + title_height;
    draw_create_game_info_text(item.hDC, rect, descriptor.description.data(),
        kCreateGameInfoBlockFlags);

    RECT summary_rect = rect;
    summary_rect.top = rect.bottom - title_height - 4;
    std::snprintf(line, sizeof(line), "%s %ux%u %s  %s%u",
        startup_message_row(131, "Tileset: "), descriptor.map_width,
        descriptor.map_height, terrain_type_name(descriptor.terrain_type),
        startup_message_row(132, "Players:"), descriptor.player_count);
    draw_create_game_info_text(item.hDC, summary_rect, line,
        kCreateGameInfoLineFlags);
}

bool paint_background_if_current(CreateGameState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToDc(state.background, paint.hdc, 0, 0);
    if (state.avatar_level_controls_visible) {
        StretchBitmapMemoryResourceToDc(state.avatar_level_panel, paint.hdc, 538, 202);
    }
    EndPaint(hwnd, &paint);
    return true;
}

bool erase_background_if_current(CreateGameState& state, HWND hwnd, HDC dc) {
    if (hwnd != state.window || dc == nullptr) {
        return false;
    }
    StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
    if (state.avatar_level_controls_visible) {
        StretchBitmapMemoryResourceToDc(state.avatar_level_panel, dc, 538, 202);
    }
    return true;
}

void destroy_window_resources(CreateGameState& state) {
    RestoreCreateGameAccelerators(state);
    DestroyLegacyStringSelectorControl(state.avatar_level_end_selector);
    DestroyLegacyStringSelectorControl(state.avatar_level_start_selector);
    DestroyLegacyImageComboBoxControl(state.screen_size_combo);
    DestroyLegacyImageComboBoxControl(state.game_type_combo);
    DestroyLegacyImageButtonControl(state.cancel_button);
    DestroyLegacyImageButtonControl(state.create_button);
    DestroyLegacyImageButtonControl(state.map_info_button);
    DestroyLegacyImageButtonControl(state.scenario_dir_button);
    DestroyLegacyCustomScrollControl(state.scroll);
    destroy_control(state.scenario_list);
    destroy_control(state.password_edit);
    destroy_control(state.name_edit);
    ReleaseBitmapMemoryResource(state.avatar_level_panel);
    ReleaseBitmapMemoryResource(state.open_folder_icon);
    ReleaseBitmapMemoryResource(state.game_icon);
    ReleaseBitmapMemoryResource(state.folder_icon);
    ReleaseBitmapMemoryResource(state.up_icon);
    ReleaseBitmapMemoryResource(state.background);
    state.window = nullptr;
    state.visible = false;
}

void return_from_cancel(CreateGameState& state) {
    if (state.mode == 1) {
        if (state.callbacks.open_p2p_lobby != nullptr) {
            state.callbacks.open_p2p_lobby(state);
        } else {
            CreateP2PLobbyWindow(p2p_lobby_state(), state.main_window, state.instance,
                state.return_context);
        }
        return;
    }
    if (state.mode == 3) {
        if (state.callbacks.open_ipx_lobby != nullptr) {
            state.callbacks.open_ipx_lobby(state);
        } else {
            CreateIpxLobbyWindow(ipx_lobby_state(), state.main_window, state.instance,
                state.return_context, nullptr);
        }
        return;
    }
    if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    } else {
        CreateConnectFrontendWindow(connect_frontend_state(), state.main_window,
            state.instance, state.return_context);
    }
}

} // namespace

CreateGameState& create_game_state() {
    return g_create_game_state;
}

#define DEFINE_CREATE_GAME_BITMAP_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Registered, Callback) \
    void StaticName(CreateGameState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(CreateGameState& state) { \
        InitializeBitmapMemoryResource(Member); \
    } \
    void RegisterName(CreateGameState&) { \
        register_atexit_once(Registered, Callback); \
    } \
    void DestroyName(CreateGameState& state) { \
        HandleBitmapMemoryResourceDestructor(Member); \
    }

DEFINE_CREATE_GAME_BITMAP_LIFETIME(InitializeCreateGameBackgroundStatic,
    InitializeCreateGameBackground,
    RegisterCreateGameBackgroundDestructor,
    DestroyCreateGameBackground, state.background,
    g_background_destructor_registered, shutdown_create_game_background)
DEFINE_CREATE_GAME_BITMAP_LIFETIME(InitializeCreateGameUpIconStatic,
    InitializeCreateGameUpIcon,
    RegisterCreateGameUpIconDestructor,
    DestroyCreateGameUpIcon, state.up_icon,
    g_bitmap_destructor_registered[0], shutdown_create_game_up_icon)
DEFINE_CREATE_GAME_BITMAP_LIFETIME(InitializeCreateGameFolderIconStatic,
    InitializeCreateGameFolderIcon,
    RegisterCreateGameFolderIconDestructor,
    DestroyCreateGameFolderIcon, state.folder_icon,
    g_bitmap_destructor_registered[1], shutdown_create_game_folder_icon)
DEFINE_CREATE_GAME_BITMAP_LIFETIME(InitializeCreateGameOpenFolderIconStatic,
    InitializeCreateGameOpenFolderIcon,
    RegisterCreateGameOpenFolderIconDestructor,
    DestroyCreateGameOpenFolderIcon, state.open_folder_icon,
    g_bitmap_destructor_registered[2], shutdown_create_game_open_folder_icon)
DEFINE_CREATE_GAME_BITMAP_LIFETIME(InitializeCreateGameGameIconStatic,
    InitializeCreateGameGameIcon,
    RegisterCreateGameGameIconDestructor,
    DestroyCreateGameGameIcon, state.game_icon,
    g_bitmap_destructor_registered[3], shutdown_create_game_game_icon)
DEFINE_CREATE_GAME_BITMAP_LIFETIME(InitializeCreateGameAvatarLevelPanelStatic,
    InitializeCreateGameAvatarLevelPanel,
    RegisterCreateGameAvatarLevelPanelDestructor,
    DestroyCreateGameAvatarLevelPanel, state.avatar_level_panel,
    g_bitmap_destructor_registered[4], shutdown_create_game_avatar_level_panel)

#undef DEFINE_CREATE_GAME_BITMAP_LIFETIME

#define DEFINE_CREATE_GAME_COMBO_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(CreateGameState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(CreateGameState& state) { \
        InitializeLegacyImageComboBoxControl(Member); \
    } \
    void RegisterName(CreateGameState&) { \
        register_atexit_once(g_combo_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(CreateGameState& state) { \
        DestroyLegacyImageComboBoxControl(Member); \
    }

DEFINE_CREATE_GAME_COMBO_LIFETIME(InitializeCreateGameGameTypeComboStatic,
    InitializeCreateGameGameTypeCombo,
    RegisterCreateGameGameTypeComboDestructor,
    DestroyCreateGameGameTypeCombo, state.game_type_combo, 0,
    shutdown_create_game_game_type_combo)
DEFINE_CREATE_GAME_COMBO_LIFETIME(InitializeCreateGameScreenSizeComboStatic,
    InitializeCreateGameScreenSizeCombo,
    RegisterCreateGameScreenSizeComboDestructor,
    DestroyCreateGameScreenSizeCombo, state.screen_size_combo, 1,
    shutdown_create_game_screen_size_combo)

#undef DEFINE_CREATE_GAME_COMBO_LIFETIME

#define DEFINE_CREATE_GAME_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(CreateGameState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(CreateGameState& state) { \
        InitializeLegacyImageButtonControl(Member); \
    } \
    void RegisterName(CreateGameState&) { \
        register_atexit_once(g_button_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(CreateGameState& state) { \
        DestroyLegacyImageButtonControl(Member); \
    }

DEFINE_CREATE_GAME_BUTTON_LIFETIME(InitializeCreateGameScenarioDirButtonStatic,
    InitializeCreateGameScenarioDirButton,
    RegisterCreateGameScenarioDirButtonDestructor,
    DestroyCreateGameScenarioDirButton, state.scenario_dir_button, 0,
    shutdown_create_game_scenario_dir_button)
DEFINE_CREATE_GAME_BUTTON_LIFETIME(InitializeCreateGameMapInfoButtonStatic,
    InitializeCreateGameMapInfoButton,
    RegisterCreateGameMapInfoButtonDestructor,
    DestroyCreateGameMapInfoButton, state.map_info_button, 1,
    shutdown_create_game_map_info_button)
DEFINE_CREATE_GAME_BUTTON_LIFETIME(InitializeCreateGameCreateButtonStatic,
    InitializeCreateGameCreateButton,
    RegisterCreateGameCreateButtonDestructor,
    DestroyCreateGameCreateButton, state.create_button, 2,
    shutdown_create_game_create_button)
DEFINE_CREATE_GAME_BUTTON_LIFETIME(InitializeCreateGameCancelButtonStatic,
    InitializeCreateGameCancelButton,
    RegisterCreateGameCancelButtonDestructor,
    DestroyCreateGameCancelButton, state.cancel_button, 3,
    shutdown_create_game_cancel_button)

#undef DEFINE_CREATE_GAME_BUTTON_LIFETIME

void InitializeCreateGameScenarioListScrollStatic(CreateGameState& state) {
    InitializeCreateGameScenarioListScroll(state);
    RegisterCreateGameScenarioListScrollDestructor(state);
}

void InitializeCreateGameScenarioListScroll(CreateGameState& state) {
    InitializeLegacyCustomScrollControl(state.scroll);
}

void RegisterCreateGameScenarioListScrollDestructor(CreateGameState&) {
    register_atexit_once(g_scroll_destructor_registered,
        shutdown_create_game_scenario_list_scroll);
}

void DestroyCreateGameScenarioListScroll(CreateGameState& state) {
    DestroyLegacyCustomScrollControl(state.scroll);
}

#define DEFINE_CREATE_GAME_SELECTOR_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(CreateGameState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(CreateGameState& state) { \
        InitializeLegacyStringSelectorControl(Member); \
    } \
    void RegisterName(CreateGameState&) { \
        register_atexit_once(g_selector_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(CreateGameState& state) { \
        DestroyLegacyStringSelectorControl(Member); \
    }

DEFINE_CREATE_GAME_SELECTOR_LIFETIME(
    InitializeCreateGameAvatarLevelStartSelectorStatic,
    InitializeCreateGameAvatarLevelStartSelector,
    RegisterCreateGameAvatarLevelStartSelectorDestructor,
    DestroyCreateGameAvatarLevelStartSelector, state.avatar_level_start_selector, 0,
    shutdown_create_game_avatar_level_start_selector)
DEFINE_CREATE_GAME_SELECTOR_LIFETIME(
    InitializeCreateGameAvatarLevelEndSelectorStatic,
    InitializeCreateGameAvatarLevelEndSelector,
    RegisterCreateGameAvatarLevelEndSelectorDestructor,
    DestroyCreateGameAvatarLevelEndSelector, state.avatar_level_end_selector, 1,
    shutdown_create_game_avatar_level_end_selector)

#undef DEFINE_CREATE_GAME_SELECTOR_LIFETIME

void InitializeCreateGameSessionDescriptorStatic(CreateGameState& state) {
    InitializeCreateGameSessionDescriptor(state);
    RegisterCreateGameSessionDescriptorDestructor(state);
}

void InitializeCreateGameSessionDescriptor(CreateGameState& state) {
    InitializeCreateGameSessionArchiveDescriptor(state);
}

void RegisterCreateGameSessionDescriptorDestructor(CreateGameState&) {
    register_atexit_once(g_session_descriptor_destructor_registered,
        shutdown_create_game_session_descriptor);
}

void DestroyCreateGameSessionDescriptor(CreateGameState& state) {
    DestroyCreateGameSessionArchiveDescriptor(state);
}

void DestroyCreateGameSessionArchiveDescriptor(CreateGameState& state) {
    DestroyWizardSessionArchiveDescriptor(state.selected_session);
}

void InitializeCreateGameSessionArchiveDescriptor(CreateGameState& state) {
    InitializeWizardSessionArchiveDescriptor(state.selected_session);
}

void InitializeCreateGameResources(CreateGameState& state) {
    InitializeCreateGameBackgroundStatic(state);
    InitializeCreateGameUpIconStatic(state);
    InitializeCreateGameFolderIconStatic(state);
    InitializeCreateGameGameIconStatic(state);
    InitializeCreateGameOpenFolderIconStatic(state);
    InitializeCreateGameAvatarLevelPanelStatic(state);
    InitializeCreateGameScenarioListScrollStatic(state);
    InitializeCreateGameScenarioDirButtonStatic(state);
    InitializeCreateGameMapInfoButtonStatic(state);
    InitializeCreateGameCreateButtonStatic(state);
    InitializeCreateGameCancelButtonStatic(state);
    InitializeCreateGameGameTypeComboStatic(state);
    InitializeCreateGameScreenSizeComboStatic(state);
    InitializeCreateGameAvatarLevelStartSelectorStatic(state);
    InitializeCreateGameAvatarLevelEndSelectorStatic(state);
}

void ReleaseCreateGameResources(CreateGameState& state) {
    destroy_window_resources(state);
}

void InstallCreateGameAccelerators(CreateGameState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kCreateGameAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreCreateGameAccelerators(CreateGameState& state) {
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

bool CreateCreateGameWindow(CreateGameState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context, int mode) {
    if (state.window != nullptr) {
        return false;
    }

    InitializeCreateGameResources(state);
    DestroyCreateGameSessionDescriptor(state);
    InitializeCreateGameSessionDescriptorStatic(state);
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.mode = mode;
    SetActiveNetworkTransportMode(mode);
    state.layout.clear();
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kCreateGameLayoutTrcRecord)) {
        ReleaseCreateGameResources(state);
        return false;
    }
    state.layout = copy_layout_record(layout.table);
    state.base_maps_directory = resolve_maps_directory();
    state.current_directory = state.base_maps_directory;
    state.selected_session_valid = false;
    state.session_seed_payload = {};
    state.map_descriptor_payload = {};
    state.server_top_bottom_counts = {};
    state.server_game_type_counts = {};
    state.server_use_map_counts = {};

    const CreateGameLayoutRect window_rect = layout_at(state, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(parent,
              window_rect.width, window_rect.height)
        : RankerCenteredFrontendWindowOrigin(
              window_rect.width, window_rect.height);
    const DWORD style = parent != nullptr ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Create Game",
        "Create Game", style, origin.x, origin.y, window_rect.width,
        window_rect.height, parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(create_game_window_proc));

    if (!create_text_control(state.name_edit, state.window, instance, "edit",
            kEditStyle, kCreateGameNameEditId, layout_at(state, 1)) ||
        !create_text_control(state.password_edit, state.window, instance, "edit",
            kEditStyle, kCreateGamePasswordEditId, layout_at(state, 2)) ||
        !create_text_control(state.scenario_list, state.window, instance, "listbox",
            kScenarioListStyle, kCreateGameScenarioListId, layout_at(state, 3)) ||
        !CreateLegacyCustomScrollControlWindow(state.scroll, state.window,
            "Create Game",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCreateGameScrollControlId)),
            false, layout_at(state, 4).x, layout_at(state, 4).y,
            layout_at(state, 4).width, layout_at(state, 4).height) ||
        !create_image_button(state.scenario_dir_button, state.window,
            "Scenario Dir", kCreateGameScenarioDirButtonId, layout_at(state, 5),
            kCreateGameScenarioDirBitmapRecord, kCreateGameScenarioDirBitmapRecord) ||
        !create_combo(state.game_type_combo, state.window, "Game Type",
            kCreateGameGameTypeComboId, kGameTypeComboStyle, layout_at(state, 6),
            kCreateGameComboBitmapRecord) ||
        !create_combo(state.screen_size_combo, state.window, "Screen size",
            kCreateGameScreenSizeComboId, kScreenSizeComboStyle, layout_at(state, 7),
            kCreateGameComboBitmapRecord) ||
        !create_image_button(state.map_info_button, state.window, "Map infos",
            kCreateGameMapInfoButtonId, layout_at(state, 12), 0, 0) ||
        !create_image_button(state.create_button, state.window, "Create &Game",
            kCreateGameCreateButtonId, layout_at(state, 13),
            kCreateGameCreateNormalBitmapRecord, kCreateGameCreatePressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kCreateGameCancelButtonId, layout_at(state, 14),
            kCreateGameCancelNormalBitmapRecord, kCreateGameCancelPressedBitmapRecord) ||
        !create_selector(state.avatar_level_start_selector, state.window,
            "level_start", kCreateGameAvatarLevelStartSelectorId, layout_at(state, 9)) ||
        !create_selector(state.avatar_level_end_selector, state.window,
            "level_end", kCreateGameAvatarLevelEndSelectorId, layout_at(state, 10))) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }

    state.scroll.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC));
    SetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(create_game_control_proc));
    LoadLegacyCustomScrollControlBitmaps(state.scroll, kCreateGameScrollUpBitmapRecord,
        kCreateGameScrollUpBitmapRecord, kCreateGameScrollDownBitmapRecord,
        kCreateGameScrollDownBitmapRecord, kCreateGameScrollThumbBitmapRecord,
        kCreateGameScrollTrackBitmapRecord);

    populate_game_type_combo(state.game_type_combo.window, state.game_type);
    const char* screen_sizes[] = {
        "Free Size",
        "640x480",
        startup_message_row(129, "800x600 Only"),
        "1024x768",
    };
    populate_combo(state.screen_size_combo.window, screen_sizes, std::size(screen_sizes),
        state.screen_size);
    ShowWindow(state.game_type_combo.window, SW_SHOW);
    ShowWindow(state.screen_size_combo.window, SW_HIDE);
    populate_selector(state.avatar_level_start_selector, 10, 10, 10);
    populate_selector(state.avatar_level_end_selector, 1, 4, 1);
    SetWindowLongPtrA(state.map_info_button.window, GWL_STYLE, 0x5800000b);

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kCreateGameBackgroundBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.up_icon, "Jw2_19.trc",
        kCreateGameUpIconBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.folder_icon, "Jw2_19.trc",
        kCreateGameFolderIconBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.game_icon, "Jw2_19.trc",
        kCreateGameGameIconBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.open_folder_icon, "Jw2_19.trc",
        kCreateGameOpenFolderIconBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.avatar_level_panel, "Jw2_19.trc",
        kCreateGameAvatarLevelBitmapRecord);

    SendMessageA(state.name_edit.window, EM_LIMITTEXT, kCreateGameNameBytes - 1, 0);
    SendMessageA(state.password_edit.window, EM_LIMITTEXT, kCreateGamePasswordBytes - 1, 0);
    const int row_height = static_cast<int>(
        SendMessageA(state.scenario_list.window, LB_GETITEMHEIGHT, 0, 0));
    state.visible_rows = row_height > 0 ? layout_at(state, 3).height / row_height : 12;
    state.visible_rows = std::max(1, state.visible_rows);
    SetLegacyCustomScrollControlPageStep(state.scroll, state.visible_rows);

    RefreshCreateGameScenarioList(state);
    if (state.mode == 6) {
        SetWindowTextA(state.name_edit.window, "Replay");
    } else {
        SetWindowTextA(state.name_edit.window, "Player's Game");
    }
    UpdateCreateGameAvatarLevelVisibility(state,
        state.game_type >= 3 && state.game_type <= 4);
    InstallCreateGameAccelerators(state);
    ShowWindow(state.window, SW_SHOW);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE |
        RDW_UPDATENOW | RDW_ALLCHILDREN);
    SetFocus(state.name_edit.window);
    state.visible = true;
    return true;
}

void ClearCreateGameScenarioListData(CreateGameState& state) {
    if (state.scenario_list.window == nullptr) {
        return;
    }
    SendMessageA(state.scenario_list.window, LB_RESETCONTENT, 0, 0);
    state.scenario_entries.clear();
    state.selected_entry_index = -1;
    state.selected_session_valid = false;
}

void PopulateCreateGameScenarioList(CreateGameState& state) {
    if (state.scenario_list.window == nullptr) {
        return;
    }
    ClearCreateGameScenarioListData(state);

    const std::string current = full_path(state.current_directory);
    const std::string root = full_path(state.base_maps_directory);
    state.current_directory = current;
    if (!equals_ignore_case(current, root)) {
        add_scenario_entry(state, "..", full_path(append_path(current, "..")),
            FILE_ATTRIBUTE_DIRECTORY, true, true);
    }

    WIN32_FIND_DATAA find_data{};
    const std::string pattern = append_path(current, "*");
    HANDLE find = FindFirstFileA(pattern.c_str(), &find_data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (should_skip_file(find_data.dwFileAttributes)) {
                continue;
            }
            const bool directory =
                (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (directory) {
                if (std::strcmp(find_data.cFileName, ".") == 0 ||
                    std::strcmp(find_data.cFileName, "..") == 0) {
                    continue;
                }
                add_scenario_entry(state, find_data.cFileName,
                    full_path(append_path(current, find_data.cFileName)),
                    find_data.dwFileAttributes, true, false);
            } else if (is_trk_file(find_data.cFileName)) {
                add_scenario_entry(state, find_data.cFileName,
                    full_path(append_path(current, find_data.cFileName)),
                    find_data.dwFileAttributes, false, false);
            }
        } while (FindNextFileA(find, &find_data));
        FindClose(find);
    }

    SendMessageA(state.scenario_list.window, LB_SETCURSEL, 0, 0);
    read_entry_selection(state);
    update_scroll(state, 0);
    InvalidateRect(state.scenario_dir_button.window, nullptr, TRUE);
    InvalidateRect(state.map_info_button.window, nullptr, TRUE);
}

void RefreshCreateGameScenarioList(CreateGameState& state) {
    PopulateCreateGameScenarioList(state);
}

bool BrowseCreateGameSelectedDirectory(CreateGameState& state) {
    CreateGameScenarioEntry* entry = selected_entry(state);
    if (entry == nullptr || !entry->directory) {
        return false;
    }
    if (is_directory(entry->path.data())) {
        state.current_directory = full_path(entry->path.data());
        RefreshCreateGameScenarioList(state);
        return true;
    }
    return false;
}

void BuildCreateGameMapDescriptorPayload(CreateGameState& state) {
    state.map_descriptor_payload.fill(0);
    const WizardSessionArchiveDescriptor& descriptor = state.selected_session;
    write_le32(state.map_descriptor_payload, 0x00, descriptor.magic);
    write_le32(state.map_descriptor_payload, 0x04, descriptor.version);
    copy_seed_fixed_text(state.map_descriptor_payload,
        kCreateGameMapDescriptorTitleOffset, kCreateGameMapDescriptorTitleBytes,
        reinterpret_cast<const u8*>(descriptor.title.data()),
        descriptor.title.size(), "");
    copy_seed_fixed_text(state.map_descriptor_payload,
        kCreateGameMapDescriptorDescriptionOffset,
        kCreateGameMapDescriptorDescriptionBytes,
        reinterpret_cast<const u8*>(descriptor.description.data()),
        descriptor.description.size(), "");
    state.map_descriptor_payload[kCreateGameMapDescriptorPlayerCountOffset] =
        descriptor.player_count;
    write_le32(state.map_descriptor_payload,
        kCreateGameMapDescriptorTerrainTypeOffset, descriptor.terrain_type);
    write_le32(state.map_descriptor_payload,
        kCreateGameMapDescriptorSessionGameTypeOffset,
        descriptor.session_game_type);
    write_le32(state.map_descriptor_payload,
        kCreateGameMapDescriptorMapWidthOffset, descriptor.map_width);
    write_le32(state.map_descriptor_payload,
        kCreateGameMapDescriptorMapHeightOffset, descriptor.map_height);
    copy_seed_text(state.map_descriptor_payload,
        kCreateGameMapDescriptorTerrainNameOffset,
        kCreateGameMapDescriptorTerrainNameBytes,
        terrain_type_name(descriptor.terrain_type));
    const std::string archive_tail = selected_session_archive_tail(state);
    copy_seed_text(state.map_descriptor_payload,
        kCreateGameMapDescriptorFileNameOffset,
        kCreateGameMapDescriptorFileNameBytes, archive_tail.c_str());
    write_le32(state.map_descriptor_payload,
        kCreateGameMapDescriptorFileSizeOffset, descriptor.file_size);
    if (kCreateGameMapDescriptorFileTimeOffset + sizeof(FILETIME) <=
        state.map_descriptor_payload.size()) {
        std::memcpy(state.map_descriptor_payload.data() +
            kCreateGameMapDescriptorFileTimeOffset, &descriptor.file_time,
            sizeof(descriptor.file_time));
    }
    write_le32(state.map_descriptor_payload,
        kCreateGameMapDescriptorGameTypeOffset,
        static_cast<u32>(std::max(0, state.game_type)));
    write_le32(state.map_descriptor_payload,
        kCreateGameMapDescriptorScreenSizeOffset,
        static_cast<u32>(std::max(0, state.screen_size)));
    copy_seed_text(state.map_descriptor_payload,
        kCreateGameMapDescriptorPlayerNameOffset,
        kCreateGameMapDescriptorPlayerNameBytes,
        state.local_player_name[0] != '\0' ? state.local_player_name.data() :
            "Player");
    copy_current_create_game_time_text(state);
}

void BuildCreateGameSessionSeedPayload(CreateGameState& state,
    const char* game_name, const char* password) {
    state.session_seed_payload.fill(0);
    state.game_name.fill('\0');
    state.password.fill('\0');
    std::snprintf(state.game_name.data(), state.game_name.size(), "%s",
        game_name != nullptr ? game_name : "");
    std::snprintf(state.password.data(), state.password.size(), "%s",
        password != nullptr ? password : "");
    std::memcpy(state.session_seed_payload.data() + 0x0c, state.game_name.data(),
        std::min<std::size_t>(state.game_name.size(), 0x13));
    std::memcpy(state.session_seed_payload.data() + 0x20, state.password.data(),
        std::min<std::size_t>(state.password.size(), 9));
    write_le32(state.session_seed_payload, kCreateGameSeedMapSelectionOffset, 0);
    write_le32(state.session_seed_payload, kCreateGameSeedModeCountOffset, 1);
    set_seed_group_header(state, 0, kCreateGameSeedPrimaryGroupY,
        state.game_name.data());
    write_le32(state.session_seed_payload, kCreateGameSeedLocalPlayerOffset, 0);

    u32 selected_player_count = state.selected_session.player_count;
    if (selected_player_count == 0 ||
        selected_player_count > kCreateGameSeedPlayerSlots) {
        selected_player_count = kCreateGameSeedPlayerSlots;
    }
    u32 max_players = selected_player_count;
    if (state.game_type == kGameTypeScenario ||
        state.game_type == kGameTypeRankGame) {
        max_players = kCreateGameSeedPlayerSlots;
    }
    write_le32(state.session_seed_payload, kCreateGameSeedMaxPlayersOffset,
        max_players);
    for (u32 slot = 0; slot < kCreateGameSeedPlayerSlots; ++slot) {
        write_le32(state.session_seed_payload,
            kCreateGameSeedTeamValuesOffset + slot * sizeof(u32), 0);
        write_le32(state.session_seed_payload,
            kCreateGameSeedTribeValuesOffset + slot * sizeof(u32), 4);
        write_le32(state.session_seed_payload,
            kCreateGameSeedTribeMasksOffset + slot * sizeof(u32), 0x1f);
        if (state.mode == 6) {
            write_le32(state.session_seed_payload,
                kCreateGameSeedRoleMasksOffset + slot * sizeof(u32),
                slot == 0 ? 0x0d : 0x0c);
            write_le32(state.session_seed_payload,
                kCreateGameSeedRoleValuesOffset + slot * sizeof(u32),
                slot == 0 ? 1 : 3);
        } else {
            write_le32(state.session_seed_payload,
                kCreateGameSeedRoleMasksOffset + slot * sizeof(u32), 0x0f);
            write_le32(state.session_seed_payload,
                kCreateGameSeedRoleValuesOffset + slot * sizeof(u32), 1);
        }
    }
    switch (state.game_type) {
    case 0: {
        write_le32(state.session_seed_payload, kCreateGameSeedModeCountOffset, 2);
        set_seed_group_header(state, 0, kCreateGameSeedPrimaryGroupY,
            startup_message_row(103, "Team 1"));
        set_seed_group_header(state, 1,
            kCreateGameSeedSecondaryGroupY + 5 +
                ((selected_player_count >> 1) * kCreateGameSeedGroupRowStep),
            startup_message_row(104, "Team 2"));
        for (u32 slot = selected_player_count >> 1; slot < selected_player_count; ++slot) {
            write_le32(state.session_seed_payload,
                kCreateGameSeedTeamValuesOffset + slot * sizeof(u32), 1);
        }
        break;
    }
    case kGameTypeObserver:
        write_le32(state.session_seed_payload, kCreateGameSeedModeCountOffset, 2);
        set_seed_group_header(state, 0, kCreateGameSeedPrimaryGroupY,
            startup_message_row(103, "Team 1"));
        set_seed_group_header(state, 1,
            kCreateGameSeedSecondaryGroupY + 5 +
                ((selected_player_count >> 1) * kCreateGameSeedGroupRowStep),
            startup_message_row(104, "Team 2"));
        for (u32 slot = selected_player_count >> 1; slot < selected_player_count; ++slot) {
            write_le32(state.session_seed_payload,
                kCreateGameSeedTeamValuesOffset + slot * sizeof(u32), 1);
        }
        clear_seed_role_mask_bit(state, 0x08);
        break;
    case kGameTypeTopVsBottom:
    case kGameTypeUseMapSetting:
        clear_seed_role_mask_bit(state, 0x08);
        break;
    case kGameTypeTeamPlay: {
        std::vector<u8> fixed44_records;
        if (!load_selected_session_forces_record(state, fixed44_records)) {
            break;
        }
        write_le32(state.session_seed_payload, kCreateGameSeedModeCountOffset, 0);
        for (u32 slot = 0; slot < kCreateGameSeedPlayerSlots; ++slot) {
            if (selected_session_slot_state(state, slot) == 0) {
                write_le32(state.session_seed_payload,
                    kCreateGameSeedLocalPlayerOffset, slot);
                break;
            }
        }
        for (u32 slot = 0; slot < kCreateGameSeedPlayerSlots; ++slot) {
            if (selected_session_slot_state(state, slot) == 1) {
                write_le32(state.session_seed_payload,
                    kCreateGameSeedRoleValuesOffset + slot * sizeof(u32), 3);
                write_le32(state.session_seed_payload,
                    kCreateGameSeedRoleMasksOffset + slot * sizeof(u32), 8);
            } else {
                write_le32(state.session_seed_payload,
                    kCreateGameSeedRoleValuesOffset + slot * sizeof(u32), 1);
                write_le32(state.session_seed_payload,
                    kCreateGameSeedRoleMasksOffset + slot * sizeof(u32), 7);
            }
            const u32 faction = selected_session_faction(state, slot);
            write_le32(state.session_seed_payload,
                kCreateGameSeedTribeValuesOffset + slot * sizeof(u32), faction);
            write_le32(state.session_seed_payload,
                kCreateGameSeedTribeMasksOffset + slot * sizeof(u32),
                faction == 4 ? 0x1f : (1u << faction));
        }
        u32 group_count = 0;
        u32 next_group_y = kCreateGameSeedPrimaryGroupY;
        for (u32 group = 0; group < kCreateGameSeedGroupCount; ++group) {
            const u8* record =
                fixed44_records.data() + group * kCreateGameForcesRecordBytes;
            const u32 owner_mask = read_le32(record,
                static_cast<i32>(kCreateGameForcesRecordBytes),
                kCreateGameForcesRecordOwnerMaskOffset);
            u32 members = 0;
            for (u32 slot = 0; slot < kCreateGameSeedPlayerSlots; ++slot) {
                if ((owner_mask & (1u << slot)) == 0) {
                    continue;
                }
                ++members;
                write_le32(state.session_seed_payload,
                    kCreateGameSeedTeamValuesOffset + slot * sizeof(u32), group);
            }
            if (members != 0) {
                ++group_count;
                write_le32(state.session_seed_payload,
                    kCreateGameSeedGroupOffsetsOffset + group * sizeof(u32),
                    next_group_y);
                next_group_y += members * kCreateGameSeedGroupRowStep +
                    (kCreateGameSeedSecondaryGroupY - kCreateGameSeedPrimaryGroupY);
            }
            char fallback_name[16]{};
            std::snprintf(fallback_name, sizeof(fallback_name), "Force %u",
                group + 1);
            write_le32(state.session_seed_payload,
                kCreateGameSeedGroupColorOffset + group * sizeof(u32),
                kCreateGameSeedDefaultTextColor);
            copy_seed_fixed_text(state.session_seed_payload,
                kCreateGameSeedGroupNameOffset +
                    group * kCreateGameSeedGroupNameBytes,
                kCreateGameSeedGroupNameBytes, record,
                kCreateGameSeedGroupNameBytes, fallback_name);
        }
        write_le32(state.session_seed_payload, kCreateGameSeedModeCountOffset,
            group_count);
        break;
    }
    case kGameTypeScenario:
    case kGameTypeRankGame: {
        u32 playable_slots = selected_player_count;
        if (playable_slots > 7) {
            --playable_slots;
        }
        write_le32(state.session_seed_payload, kCreateGameSeedModeCountOffset, 2);
        set_seed_group_header(state, 1,
            kCreateGameSeedSecondaryGroupY + 5 +
                playable_slots * kCreateGameSeedGroupRowStep,
            startup_message_row(99, "Observer"));
        write_le32(state.session_seed_payload,
            kCreateGameSeedGroupColorOffset + sizeof(u32), 0x0000fafa);
        for (u32 slot = playable_slots; slot < kCreateGameSeedPlayerSlots; ++slot) {
            write_le32(state.session_seed_payload,
                kCreateGameSeedTeamValuesOffset + slot * sizeof(u32), 1);
            write_le32(state.session_seed_payload,
                kCreateGameSeedRoleValuesOffset + slot * sizeof(u32), 1);
            write_le32(state.session_seed_payload,
                kCreateGameSeedRoleMasksOffset + slot * sizeof(u32), 7);
            write_le32(state.session_seed_payload,
                kCreateGameSeedTribeValuesOffset + slot * sizeof(u32), 4);
            write_le32(state.session_seed_payload,
                kCreateGameSeedTribeMasksOffset + slot * sizeof(u32), 0x1f);
        }
        break;
    }
    default:
        break;
    }
    BuildCreateGameMapDescriptorPayload(state);
    state.avatar_level_start =
        (GetLegacyStringSelectorSelectedIndex(state.avatar_level_start_selector) + 1) * 10;
    state.avatar_level_end =
        GetLegacyStringSelectorSelectedIndex(state.avatar_level_end_selector) + 1;
}

void BuildCreateGameSessionSeedFromFields(CreateGameState& state,
    const char* game_name, const char* password) {
    BuildCreateGameSessionSeedPayload(state, game_name, password);
}

void BuildCreateGameSessionSeed(CreateGameState& state) {
    std::array<char, kCreateGameNameBytes> game_name{};
    std::array<char, kCreateGamePasswordBytes> password{};
    GetWindowTextA(state.name_edit.window, game_name.data(),
        static_cast<int>(game_name.size()));
    GetWindowTextA(state.password_edit.window, password.data(),
        static_cast<int>(password.size()));
    BuildCreateGameSessionSeedFromFields(state, game_name.data(), password.data());
}

bool LaunchCreateGameLinkLobby(CreateGameState& state) {
    return launch_create_game_link_lobby(state);
}

bool SubmitCreateGameSelection(CreateGameState& state) {
    play_click_sound(state);
    read_entry_selection(state);
    if (!state.selected_session_valid) {
        set_startup_message(state, 57, "Select a game map first.",
            kCreateGameSoftWhite);
        SetFocus(state.scenario_list.window);
        return false;
    }

    BuildCreateGameSessionSeed(state);
    if (state.game_name[0] == '\0') {
        set_startup_message(state, 58, "Enter a game name.",
            kCreateGameSoftWhite);
        SetFocus(state.name_edit.window);
        return false;
    }

    if (requires_avatar_production_gate(state.game_type) &&
        !IsGameSessionAvatarProductionAvailable()) {
        set_startup_message(state, 223,
            "Not enough avatars can join with the selected level limit.",
            kCreateGameWarning);
        SetFocus(state.name_edit.window);
        return false;
    }

    if (!is_game_type_allowed_for_mode(state.mode, state.game_type)) {
        set_startup_message(state, 105,
            "The selected game type requires a server connection.",
            kCreateGameWarning);
        SetFocus(state.name_edit.window);
        return false;
    }
    if (!validate_online_server_counts(state)) {
        return false;
    }

    if (state.mode == 0 || state.mode == 2) {
        return SubmitCreateGameWizardHostRequest(state);
    }

    return launch_create_game_link_lobby(state);
}

void SetCreateGameAvatarLevelControlsVisible(CreateGameState& state, bool visible) {
    state.avatar_level_controls_visible = visible;
    const int command = visible ? SW_SHOW : SW_HIDE;
    if (state.avatar_level_start_selector.window != nullptr) {
        ShowWindow(state.avatar_level_start_selector.window, command);
    }
    if (state.avatar_level_end_selector.window != nullptr) {
        ShowWindow(state.avatar_level_end_selector.window, command);
    }
    if (state.window != nullptr) {
        RECT rect{538, 202, 736, 272};
        InvalidateRect(state.window, &rect, TRUE);
    }
}

void UpdateCreateGameAvatarLevelVisibility(CreateGameState& state, bool visible) {
    SetCreateGameAvatarLevelControlsVisible(state, visible);
}

LRESULT HandleCreateGameWindowMessage(CreateGameState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        destroy_window_resources(state);
        return 0;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        if (erase_background_if_current(state, hwnd, reinterpret_cast<HDC>(wparam))) {
            return 1;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kCreateGameWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kCreateGameBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kCreateGameWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kCreateGameBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kCreateGameWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kCreateGameBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case kCreateGameNetworkMessage:
        DispatchCreateGameNetworkMessage(state, hwnd, wparam, lparam);
        break;
    case kCreateGameDuplicateNamePromptMessage:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            startup_message_row(59, "The game name is already in use."),
            kCreateGameSoftWhite);
        break;
    case kOnlinePromptStatusMessage0:
        ShowOnlineModalPrompt0(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage1:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage2:
        ShowOnlineModalPrompt2(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptStatusMessage3:
        ShowOnlineModalPrompt3(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kOnlinePromptEndMessage:
        EndOnlineModalPrompt(online_modal_prompt_state(), static_cast<INT_PTR>(wparam));
        break;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kCreateGameScenarioListId) {
            draw_scenario_item(state, *draw);
            return TRUE;
        }
        if (draw->CtlID == kCreateGameGameTypeComboId) {
            DrawLegacyImageComboBoxItem(state.game_type_combo, *draw);
            return TRUE;
        }
        if (draw->CtlID == kCreateGameScreenSizeComboId) {
            DrawLegacyImageComboBoxItem(state.screen_size_combo, *draw);
            return TRUE;
        }
        if (draw->CtlID == kCreateGameScenarioDirButtonId) {
            draw_scenario_dir(state, *draw);
            break;
        }
        if (draw->CtlID == kCreateGameMapInfoButtonId) {
            draw_map_info(state, *draw);
            break;
        }
        if (draw->CtlID == kCreateGameAvatarLevelStartSelectorId ||
            draw->CtlID == kCreateGameAvatarLevelEndSelectorId) {
            return TRUE;
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
        const int notify = HIWORD(wparam);
        switch (id) {
        case kCreateGameFocusNameCommandId:
            SetFocus(state.name_edit.window);
            break;
        case kCreateGameFocusPasswordCommandId:
            SetFocus(state.password_edit.window);
            break;
        case kCreateGameFocusGameTypeCommandId:
            SetFocus(state.game_type_combo.window);
            break;
        case kCreateGameFocusScreenSizeCommandId:
            SetFocus(state.screen_size_combo.window);
            break;
        case kCreateGameFocusScenarioListCommandId:
            SetFocus(state.scenario_list.window);
            break;
        case kCreateGameActivateScenarioListCommandId:
            if (GetFocus() == state.scenario_list.window) {
                if (!BrowseCreateGameSelectedDirectory(state)) {
                    SubmitCreateGameSelection(state);
                }
            }
            break;
        case kCreateGameCreateButtonId:
            SubmitCreateGameSelection(state);
            break;
        case kCreateGameCancelButtonId:
            play_click_sound(state);
            DestroyWindow(hwnd);
            return_from_cancel(state);
            break;
        case kCreateGameScenarioListId:
            if (notify == LBN_SELCHANGE) {
                read_entry_selection(state);
                const int top = static_cast<int>(
                    SendMessageA(state.scenario_list.window, LB_GETTOPINDEX, 0, 0));
                SetLegacyCustomScrollControlValue(state.scroll, top, true);
                InvalidateRect(state.map_info_button.window, nullptr, TRUE);
            } else if (notify == LBN_DBLCLK) {
                if (!BrowseCreateGameSelectedDirectory(state)) {
                    SubmitCreateGameSelection(state);
                }
            }
            break;
        case kCreateGameGameTypeComboId:
            if (notify == CBN_SELENDOK) {
                const int selection = static_cast<int>(
                    SendMessageA(state.game_type_combo.window, CB_GETCURSEL, 0, 0));
                if (selection != CB_ERR) {
                    state.game_type = selection;
                    UpdateCreateGameAvatarLevelVisibility(state,
                        selection >= 3 && selection <= 4);
                }
            }
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

LRESULT HandleCreateGameControlMessage(CreateGameState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));

    if (message == WM_PAINT && hwnd == state.scroll.window) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.scroll, paint.hdc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }
    if (LegacyStringSelectorControl* selector = selector_for_id(state, id)) {
        return HandleLegacyStringSelectorMessage(*selector, hwnd, message, wparam,
            lparam);
    }
    if (id == kCreateGameGameTypeComboId && message == WM_PAINT) {
        PaintLegacyImageComboBoxBackground(state.game_type_combo);
    } else if (id == kCreateGameScreenSizeComboId && message == WM_PAINT) {
        PaintLegacyImageComboBoxBackground(state.screen_size_combo);
    } else if (id == kCreateGameScrollControlId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.scroll, message, wparam, lparam);
        if (changed && state.scenario_list.window != nullptr) {
            const int top = GetLegacyCustomScrollControlValue(state.scroll);
            SendMessageA(state.scenario_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(top), 0);
        }
    }

    switch (id) {
    case kCreateGameNameEditId:
    case kCreateGamePasswordEditId:
    case kCreateGameGameTypeComboId:
    case kCreateGameScreenSizeComboId:
    case kCreateGameMapInfoButtonId:
    case kCreateGameScenarioListId:
    case kCreateGameScrollControlId:
    case kCreateGameScenarioDirButtonId:
    case kCreateGameCreateButtonId:
    case kCreateGameCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

} // namespace ranker

#endif
