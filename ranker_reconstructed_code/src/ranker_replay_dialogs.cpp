#include "ranker_replay_dialogs.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_reliable_packets.h"
#include "ranker_replay_archive.h"
#include "ranker_gameplay_sound.h"
#include "ranker_online_dialogs.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = WS_POPUP | WS_CLIPSIBLINGS;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kListStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_SORT | LBS_OWNERDRAWFIXED |
    LBS_HASSTRINGS;
constexpr DWORD kReadOnlyEditStyle = WS_CHILD | WS_VISIBLE | WS_DISABLED;
constexpr DWORD kSaveNameEditStyle = WS_CHILD | WS_VISIBLE;
constexpr COLORREF kReplayWhite = RGB(255, 255, 255);
constexpr COLORREF kReplayGray = RGB(210, 210, 210);
constexpr COLORREF kReplayWarning = RGB(250, 10, 10);
constexpr COLORREF kReplayGreen = RGB(10, 210, 210);
constexpr COLORREF kReplayBlack = RGB(0, 0, 0);
constexpr COLORREF kReplaySelectedBlue = RGB(0, 0, 255);
constexpr UINT kReplayInfoLineFlags =
    DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_MODIFYSTRING;
constexpr std::size_t kStartupReplayGameTypeRowBase = 109;
constexpr const char* kReplayGameTypeFallbacks[] = {
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
constexpr const char* kReplayArchiveName = "Jw2_19.trc";
constexpr const char* kReplayFileExtension = ".ply";
constexpr const char* kReplayMusicExtension = ".mp3";
constexpr const char* kReplayVposExtension = ".vpo";
constexpr const char* kReplayHeaderText = "Jwar2 Replay File.";

ReplayDialogState g_replay_load_dialog_state;
ReplayDialogState g_replay_save_dialog_state;
std::array<bool, 7> g_replay_load_bitmap_destructor_registered{};
std::array<bool, 3> g_replay_load_button_destructor_registered{};
bool g_replay_load_scroll_destructor_registered = false;
std::array<bool, 5> g_replay_save_bitmap_destructor_registered{};
std::array<bool, 3> g_replay_save_button_destructor_registered{};
bool g_replay_save_scroll_destructor_registered = false;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK replay_load_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleReplayLoadWindowMessage(g_replay_load_dialog_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK replay_load_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleReplayLoadControlMessage(g_replay_load_dialog_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK replay_save_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleReplaySaveWindowMessage(g_replay_save_dialog_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK replay_save_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleReplaySaveControlMessage(g_replay_save_dialog_state, hwnd, message,
        wparam, lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (registered) {
        return;
    }
    std::atexit(callback);
    registered = true;
}

void shutdown_replay_load_background() {
    DestroyReplayLoadBackground(g_replay_load_dialog_state);
}

void shutdown_replay_load_up_icon() {
    DestroyReplayLoadUpIcon(g_replay_load_dialog_state);
}

void shutdown_replay_load_folder_icon() {
    DestroyReplayLoadFolderIcon(g_replay_load_dialog_state);
}

void shutdown_replay_load_open_folder_icon() {
    DestroyReplayLoadOpenFolderIcon(g_replay_load_dialog_state);
}

void shutdown_replay_load_camera_icon() {
    DestroyReplayLoadCameraIcon(g_replay_load_dialog_state);
}

void shutdown_replay_load_speaker_icon() {
    DestroyReplayLoadSpeakerIcon(g_replay_load_dialog_state);
}

void shutdown_replay_load_vpos_icon() {
    DestroyReplayLoadVposIcon(g_replay_load_dialog_state);
}

void shutdown_replay_load_info_button() {
    DestroyReplayLoadInfoButton(g_replay_load_dialog_state);
}

void shutdown_replay_load_ok_button() {
    DestroyReplayLoadOkButton(g_replay_load_dialog_state);
}

void shutdown_replay_load_cancel_button() {
    DestroyReplayLoadCancelButton(g_replay_load_dialog_state);
}

void shutdown_replay_load_file_list_scroll() {
    DestroyReplayLoadFileListScroll(g_replay_load_dialog_state);
}

void shutdown_replay_save_background() {
    DestroyReplaySaveBackground(g_replay_save_dialog_state);
}

void shutdown_replay_save_up_icon() {
    DestroyReplaySaveUpIcon(g_replay_save_dialog_state);
}

void shutdown_replay_save_folder_icon() {
    DestroyReplaySaveFolderIcon(g_replay_save_dialog_state);
}

void shutdown_replay_save_open_folder_icon() {
    DestroyReplaySaveOpenFolderIcon(g_replay_save_dialog_state);
}

void shutdown_replay_save_camera_icon() {
    DestroyReplaySaveCameraIcon(g_replay_save_dialog_state);
}

void shutdown_replay_save_info_button() {
    DestroyReplaySaveInfoButton(g_replay_save_dialog_state);
}

void shutdown_replay_save_ok_button() {
    DestroyReplaySaveOkButton(g_replay_save_dialog_state);
}

void shutdown_replay_save_cancel_button() {
    DestroyReplaySaveCancelButton(g_replay_save_dialog_state);
}

void shutdown_replay_save_file_list_scroll() {
    DestroyReplaySaveFileListScroll(g_replay_save_dialog_state);
}

std::vector<ReplayDialogLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<ReplayDialogLayoutRect> rects;
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

ReplayDialogLayoutRect layout_at(const ReplayDialogState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return ReplayDialogLayoutRect{};
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& target, const char* source) {
    target.fill(0);
    if (source != nullptr) {
        std::strncpy(target.data(), source, target.size() - 1);
    }
}

u32 read_le_u32(const u8* bytes) {
    return static_cast<u32>(bytes[0]) |
        (static_cast<u32>(bytes[1]) << 8) |
        (static_cast<u32>(bytes[2]) << 16) |
        (static_cast<u32>(bytes[3]) << 24);
}

void write_packet_u32(std::array<u8, kReplayPacketBytes>& bytes,
    std::size_t offset, u32 value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        return;
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

bool starts_with_ascii(const std::vector<u8>& bytes, const char* text) {
    const std::size_t length = std::strlen(text);
    return bytes.size() >= length &&
        std::memcmp(bytes.data(), text, length) == 0;
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
    std::string result = base;
    if (!result.empty() && result.back() != '\\' && result.back() != '/') {
        result.push_back('\\');
    }
    if (leaf != nullptr) {
        result += leaf;
    }
    return result;
}

bool equals_ignore_case(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[i]);
        const char la = static_cast<char>(std::tolower(a));
        const char lb = static_cast<char>(std::tolower(b));
        if (la != lb) {
            return false;
        }
    }
    return true;
}

bool is_directory_path(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool file_exists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string extension_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

bool has_extension(const std::string& path, const char* extension) {
    return equals_ignore_case(extension_of(path), extension);
}

std::string replace_extension(const std::string& path, const char* extension) {
    const std::size_t slash = path.find_last_of("\\/");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path + extension;
    }
    return path.substr(0, dot) + extension;
}

bool should_skip_file(DWORD attributes) {
    constexpr DWORD kSkipped =
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY |
        FILE_ATTRIBUTE_OFFLINE;
    return (attributes & kSkipped) != 0;
}

std::string resolve_replays_directory() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    fs::path current = fs::current_path();
    for (int i = 0; i < 7 && !current.empty(); ++i) {
        roots.push_back(current);
        current = current.parent_path();
    }

    for (const fs::path& root : roots) {
        const fs::path direct = root / "Replays";
        if (fs::is_directory(direct)) {
            return full_path(direct.string());
        }
        const fs::path nested = root / "ranker" / "Replays";
        if (fs::is_directory(nested)) {
            return full_path(nested.string());
        }
    }

    const std::string fallback = full_path("Replays");
    CreateDirectoryA(fallback.c_str(), nullptr);
    return fallback;
}

void clear_text_control(ReplayDialogTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void destroy_text_control(ReplayDialogTextControl& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    clear_text_control(control);
}

void subclass_window(HWND hwnd, WNDPROC& original, WNDPROC replacement) {
    if (hwnd == nullptr) {
        return;
    }
    original = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC));
    SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(replacement));
}

bool create_text_control(ReplayDialogTextControl& control, HWND parent,
    HINSTANCE instance, const char* class_name, DWORD style, int id,
    const ReplayDialogLayoutRect& rect, WNDPROC control_proc) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_text_control(control);
        return false;
    }
    subclass_window(control.window, control.original_window_proc, control_proc);
    return true;
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const ReplayDialogLayoutRect& rect, u32 normal_record,
    u32 pressed_record, WNDPROC control_proc) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    SetWindowLongPtrA(button.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(control_proc));
    return true;
}

LegacyImageButtonControl* button_for_id(ReplayDialogState& state, int id) {
    if (state.save_dialog) {
        switch (id) {
        case kReplaySaveInfoButtonId:
            return &state.info_button;
        case kReplaySaveOkButtonId:
            return &state.ok_button;
        case kReplaySaveCancelButtonId:
            return &state.cancel_button;
        default:
            return nullptr;
        }
    }

    switch (id) {
    case kReplayLoadInfoButtonId:
        return &state.info_button;
    case kReplayLoadOkButtonId:
        return &state.ok_button;
    case kReplayLoadCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

WNDPROC original_proc_for_id(ReplayDialogState& state, int id) {
    if (id == state.name_edit.id) {
        return state.name_edit.original_window_proc;
    }
    if (id == state.directory_edit.id) {
        return state.directory_edit.original_window_proc;
    }
    if (id == state.file_list.id) {
        return state.file_list.original_window_proc;
    }
    if (id == (state.save_dialog ? kReplaySaveScrollControlId :
        kReplayLoadScrollControlId)) {
        return state.scroll.original_window_proc;
    }
    if (LegacyImageButtonControl* button = button_for_id(state, id)) {
        return button->original_window_proc;
    }
    return nullptr;
}

bool is_replay_load_control_id(int id) {
    switch (id) {
    case kReplayLoadInfoButtonId:
    case kReplayLoadListId:
    case kReplayLoadScrollControlId:
    case kReplayLoadDirectoryEditId:
    case kReplayLoadOkButtonId:
    case kReplayLoadCancelButtonId:
        return true;
    default:
        return false;
    }
}

bool is_replay_save_control_id(int id) {
    switch (id) {
    case kReplaySaveNameEditId:
    case kReplaySaveInfoButtonId:
    case kReplaySaveListId:
    case kReplaySaveScrollControlId:
    case kReplaySaveDirectoryEditId:
    case kReplaySaveOkButtonId:
    case kReplaySaveCancelButtonId:
        return true;
    default:
        return false;
    }
}

void initialize_resources(ReplayDialogState& state) {
    if (!state.save_dialog) {
        InitializeReplayLoadBackground(state);
        RegisterReplayLoadBackgroundDestructor(state);
        InitializeReplayLoadUpIconStatic(state);
        InitializeReplayLoadFolderIconStatic(state);
        InitializeReplayLoadOpenFolderIconStatic(state);
        InitializeReplayLoadCameraIconStatic(state);
        InitializeReplayLoadSpeakerIconStatic(state);
        InitializeReplayLoadVposIconStatic(state);
        InitializeReplayLoadFileListScrollStatic(state);
        InitializeReplayLoadInfoButtonStatic(state);
        InitializeReplayLoadOkButtonStatic(state);
        InitializeReplayLoadCancelButtonStatic(state);
    } else {
        InitializeReplaySaveBackgroundStatic(state);
        InitializeReplaySaveUpIconStatic(state);
        InitializeReplaySaveFolderIconStatic(state);
        InitializeReplaySaveOpenFolderIconStatic(state);
        InitializeReplaySaveCameraIconStatic(state);
        InitializeReplaySaveFileListScrollStatic(state);
        InitializeReplaySaveInfoButtonStatic(state);
        InitializeReplaySaveOkButtonStatic(state);
        InitializeReplaySaveCancelButtonStatic(state);
    }
    clear_text_control(state.name_edit);
    clear_text_control(state.directory_edit);
    clear_text_control(state.file_list);
}

void restore_accelerators(ReplayDialogState& state) {
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

void install_accelerators(ReplayDialogState& state, int resource_id) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators =
        LoadAcceleratorsA(state.instance, MAKEINTRESOURCEA(resource_id));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void destroy_dialog_resources(ReplayDialogState& state) {
    restore_accelerators(state);
    DestroyLegacyImageButtonControl(state.cancel_button);
    DestroyLegacyImageButtonControl(state.ok_button);
    DestroyLegacyImageButtonControl(state.info_button);
    DestroyLegacyCustomScrollControl(state.scroll);
    destroy_text_control(state.file_list);
    destroy_text_control(state.directory_edit);
    destroy_text_control(state.name_edit);
    ReleaseBitmapMemoryResource(state.vpos_icon);
    ReleaseBitmapMemoryResource(state.speaker_icon);
    ReleaseBitmapMemoryResource(state.camera_icon);
    ReleaseBitmapMemoryResource(state.open_folder_icon);
    ReleaseBitmapMemoryResource(state.folder_icon);
    ReleaseBitmapMemoryResource(state.up_icon);
    ReleaseBitmapMemoryResource(state.background);
    state.layout.clear();
    state.entries.clear();
    state.window = nullptr;
    state.visible = false;
}

void play_click_sound(ReplayDialogState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

const char* text_row(std::size_t index, const char* fallback) {
    const StartupTextTables& tables = startup_text_tables();
    if (index < tables.message_rows.rows.size()) {
        return tables.message_rows.rows[index].data();
    }
    return fallback;
}

const char* replay_game_type_label(u32 game_type) {
    if (game_type < std::size(kReplayGameTypeFallbacks)) {
        return text_row(kStartupReplayGameTypeRowBase + game_type,
            kReplayGameTypeFallbacks[game_type]);
    }
    return nullptr;
}

std::string version_text(u32 version) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%u-%u-%u",
        version & 0xffffu, (version >> 16) & 0xffu, (version >> 24) & 0xffu);
    return text;
}

void parse_descriptor_header(ReplayArchiveDescriptor& descriptor) {
    const std::vector<u8>& payload = descriptor.payload;
    if (payload.size() < kReplayHeaderBytes) {
        return;
    }

    descriptor.version = read_le_u32(payload.data() + 0x17);
    descriptor.mode = payload[0x1b];
    descriptor.local_player = payload[0x5f];
    descriptor.game_type = payload[0x87];
    descriptor.packet_count =
        static_cast<u32>((payload.size() - kReplayHeaderBytes) / kReplayPacketBytes);
    if (descriptor.packet_count != 0) {
        const std::size_t last_offset = kReplayHeaderBytes +
            static_cast<std::size_t>(descriptor.packet_count - 1) *
                kReplayPacketBytes;
        descriptor.last_frame_tick = read_le_u32(payload.data() + last_offset + 0x04);
    }

    copy_c_string(descriptor.date,
        reinterpret_cast<const char*>(payload.data() + 0x3f));
    copy_c_string(descriptor.time,
        reinterpret_cast<const char*>(payload.data() + 0x4f));

    for (std::size_t i = 0; i < descriptor.players.size(); ++i) {
        const std::size_t offset = 0x1fff + i * kReplayPlayerNameBytes;
        if (offset < payload.size()) {
            copy_c_string(descriptor.players[i],
                reinterpret_cast<const char*>(payload.data() + offset));
        }
    }
}

ReplayArchiveDescriptor descriptor_from_payload(const std::vector<u8>& payload,
    const char* source_path, u32 expected_version) {
    ReplayArchiveDescriptor descriptor;
    descriptor.source_path = source_path == nullptr ? "" : source_path;
    descriptor.payload = payload;

    if (payload.size() < kReplayHeaderBytes) {
        descriptor.status = ReplayValidationStatus::Invalid;
        return descriptor;
    }
    if (!starts_with_ascii(payload, kReplayHeaderText)) {
        descriptor.status = ReplayValidationStatus::Invalid;
        return descriptor;
    }

    parse_descriptor_header(descriptor);
    if (expected_version != 0 && descriptor.version != expected_version) {
        descriptor.status = ReplayValidationStatus::VersionMismatch;
    } else {
        descriptor.status = ReplayValidationStatus::Valid;
    }
    return descriptor;
}

void update_scroll(ReplayDialogState& state, int top_index = -1) {
    if (state.file_list.window == nullptr) {
        return;
    }
    const int count = static_cast<int>(
        SendMessageA(state.file_list.window, LB_GETCOUNT, 0, 0));
    if (top_index < 0) {
        top_index = static_cast<int>(
            SendMessageA(state.file_list.window, LB_GETTOPINDEX, 0, 0));
    }
    const int max_top = std::max(0, count - state.visible_rows);
    SetLegacyCustomScrollControlVisible(state.scroll, count > state.visible_rows);
    SetLegacyCustomScrollControlRange(state.scroll, 0, max_top, true);
    SetLegacyCustomScrollControlValue(state.scroll, std::min(top_index, max_top), true);
}

void add_file_entry(ReplayDialogState& state, const ReplayFileEntry& entry) {
    state.entries.push_back(entry);

    std::string display;
    if (entry.directory) {
        display = " ";
        display += entry.name.data();
    } else {
        display = entry.name.data();
    }

    const LRESULT row = SendMessageA(state.file_list.window, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(display.c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
        SendMessageA(state.file_list.window, LB_SETITEMDATA,
            static_cast<WPARAM>(row),
            static_cast<LPARAM>(state.entries.size() - 1));
    }
}

ReplayFileEntry* selected_entry(ReplayDialogState& state) {
    if (state.file_list.window == nullptr) {
        return nullptr;
    }
    const LRESULT selected = SendMessageA(state.file_list.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return nullptr;
    }
    const LRESULT data = SendMessageA(state.file_list.window, LB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    if (data == LB_ERR || data < 0 ||
        static_cast<std::size_t>(data) >= state.entries.size()) {
        return nullptr;
    }
    return &state.entries[static_cast<std::size_t>(data)];
}

void draw_replay_info_line(HDC dc, RECT& rect, const char* text) {
    char line[256]{};
    std::snprintf(line, sizeof(line), "%s", text != nullptr ? text : "");
    const int height = DrawTextA(dc, line, -1, &rect, kReplayInfoLineFlags);
    rect.top += 4 + std::max(height, 0);
}

std::string replay_info_display_name(const ReplayArchiveDescriptor& descriptor) {
    if (descriptor.source_path.empty()) {
        return {};
    }
    const std::size_t separator = descriptor.source_path.find_last_of("\\/");
    if (separator != std::string::npos &&
        separator + 1 < descriptor.source_path.size()) {
        return descriptor.source_path.substr(separator + 1);
    }
    return descriptor.source_path;
}

void draw_replay_info(ReplayDialogState& state, const ReplayArchiveDescriptor& descriptor,
    const DRAWITEMSTRUCT& item) {
    RECT rect = item.rcItem;
    SetBkColor(item.hDC, kReplayBlack);
    ExtTextOutA(item.hDC, rect.left, rect.top, ETO_OPAQUE, &item.rcItem, nullptr, 0,
        nullptr);
    rect.left += 5;
    rect.top += 8;
    rect.right -= 5;
    SetBkMode(item.hDC, TRANSPARENT);

    if (descriptor.status == ReplayValidationStatus::None) {
        SetTextColor(item.hDC, kReplayGray);
        DrawTextA(item.hDC, text_row(190, "Select a replay file."), -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    if (descriptor.status == ReplayValidationStatus::OpenFailed) {
        SetTextColor(item.hDC, kReplayWarning);
        DrawTextA(item.hDC, text_row(191, "Replay open failed."), -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    if (descriptor.status == ReplayValidationStatus::Invalid) {
        SetTextColor(item.hDC, kReplayWarning);
        DrawTextA(item.hDC, text_row(192, "Invalid replay file."), -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    if (descriptor.status == ReplayValidationStatus::SaveFailed) {
        SetTextColor(item.hDC, kReplayWarning);
        DrawTextA(item.hDC, "Replay save failed.", -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    SetTextColor(item.hDC, kReplayGray);
    char line[256]{};
    std::snprintf(line, sizeof(line), text_row(187, "Replay data ver %d-%d-%d"),
        descriptor.version & 0xffffu,
        (descriptor.version >> 16) & 0xffu,
        (descriptor.version >> 24) & 0xffu);
    draw_replay_info_line(item.hDC, rect, line);

    std::snprintf(line, sizeof(line), text_row(188, "Date %s %s"),
        descriptor.date.data(), descriptor.time.data());
    draw_replay_info_line(item.hDC, rect, line);

    const std::string replay_name = replay_info_display_name(descriptor);
    std::snprintf(line, sizeof(line), "File %s", replay_name.c_str());
    draw_replay_info_line(item.hDC, rect, line);

    const char* game_type_name = replay_game_type_label(descriptor.game_type);
    if (game_type_name != nullptr) {
        std::snprintf(line, sizeof(line), "%s%s",
            text_row(141, "Game type: "), game_type_name);
    } else {
        std::snprintf(line, sizeof(line), "%s%u",
            text_row(141, "Game type: "), descriptor.game_type);
    }
    draw_replay_info_line(item.hDC, rect, line);

    draw_replay_info_line(item.hDC, rect, text_row(189, "Player List:"));

    for (std::size_t i = 0; i < descriptor.players.size(); ++i) {
        std::snprintf(line, sizeof(line), "  %s", descriptor.players[i].data());
        draw_replay_info_line(item.hDC, rect, line);
    }

    if (descriptor.status == ReplayValidationStatus::VersionMismatch) {
        rect.top += 8;
        SetTextColor(item.hDC, kReplayGreen);
        DrawTextA(item.hDC, text_row(193, "Replay file version is too old."), -1,
            &rect, DT_WORDBREAK | DT_NOPREFIX);
    }
    (void)state;
}

void draw_file_entry(ReplayDialogState& state, const DRAWITEMSTRUCT& item) {
    if (item.itemID == static_cast<UINT>(-1) ||
        item.itemData >= state.entries.size()) {
        return;
    }

    const ReplayFileEntry& entry = state.entries[item.itemData];
    RECT fill = item.rcItem;
    SetBkColor(item.hDC,
        (item.itemState & ODS_SELECTED) != 0 ? kReplaySelectedBlue : kReplayBlack);
    ExtTextOutA(item.hDC, fill.left, fill.top, ETO_OPAQUE, &fill, nullptr, 0, nullptr);

    RECT rect = item.rcItem;
    rect.left += 4;
    rect.top += 2;
    if (entry.directory) {
        BitmapMemoryResource& icon = entry.parent ? state.up_icon : state.folder_icon;
        StretchBitmapMemoryResourceToDc(icon, item.hDC, rect.left, rect.top);
        rect.left += 20;
    } else {
        StretchBitmapMemoryResourceToDc(state.camera_icon, item.hDC, rect.left, rect.top);
        rect.left += 20;
        if (!state.save_dialog && entry.has_mp3) {
            StretchBitmapMemoryResourceToDc(state.speaker_icon, item.hDC, rect.left,
                rect.top);
            rect.left += 20;
        }
        if (!state.save_dialog && entry.has_vpos) {
            StretchBitmapMemoryResourceToDc(state.vpos_icon, item.hDC, rect.left,
                rect.top);
            rect.left += 20;
        }
    }

    SetTextColor(item.hDC, kReplayWhite);
    SetBkMode(item.hDC, TRANSPARENT);
    DrawTextA(item.hDC, entry.name.data(), -1, &rect,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

bool paint_background_if_current(ReplayDialogState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToDc(state.background, paint.hdc, 0, 0);
    EndPaint(hwnd, &paint);
    return true;
}

void refresh_selection_descriptor(ReplayDialogState& state) {
    if (state.save_dialog) {
        return;
    }

    state.selected_replay = ReplayArchiveDescriptor{};
    state.status = ReplayValidationStatus::None;
    ReplayFileEntry* entry = selected_entry(state);
    if (entry == nullptr) {
        InvalidateRect(state.info_button.window, nullptr, TRUE);
        return;
    }
    if (entry->directory) {
        // The original immediately attempts to validate the initially
        // selected directory and displays its open-failure message.  A double
        // click still browses the directory before submission.
        state.selected_replay.status = ReplayValidationStatus::OpenFailed;
        state.status = state.selected_replay.status;
        InvalidateRect(state.info_button.window, nullptr, TRUE);
        return;
    }

    const u32 expected_version = LoadTrcRecord9Value();
    LoadReplayArchiveDescriptor(entry->path.data(), state.selected_replay,
        expected_version);
    state.status = state.selected_replay.status;
    InvalidateRect(state.info_button.window, nullptr, TRUE);
}

bool read_binary_file(const std::string& path, std::vector<u8>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

std::vector<u8> build_replay_payload_from_recording(const ReplayRecordingState& recording) {
    std::vector<u8> payload;
    const bool header_valid =
        std::memcmp(recording.header.data(), kReplayHeaderText,
            std::strlen(kReplayHeaderText)) == 0;
    if (header_valid && !recording.packet_records.empty()) {
        payload.assign(recording.header.begin(), recording.header.end());
        for (const auto& record : recording.packet_records) {
            payload.insert(payload.end(), record.begin(), record.end());
        }
        return payload;
    }

    if (header_valid && recording.packet_count != 0) {
        payload.assign(recording.header.begin(), recording.header.end());
        const u32 packet_count = std::min<u32>(recording.packet_count,
            kReplayPacketRingSlots);
        const std::size_t packet_bytes =
            static_cast<std::size_t>(packet_count) * kReplayPacketBytes;
        payload.insert(payload.end(), recording.packet_scratch.begin(),
            recording.packet_scratch.begin() + static_cast<std::ptrdiff_t>(packet_bytes));
        return payload;
    }

    if (read_binary_file(recording.packet_temp_path, payload) &&
        payload.size() >= kReplayHeaderBytes &&
        starts_with_ascii(payload, kReplayHeaderText)) {
        return payload;
    }

    if (header_valid) {
        payload.assign(recording.header.begin(), recording.header.end());
        return payload;
    }
    return {};
}

void patch_replay_payload_player_names(std::vector<u8>& payload) {
    const std::size_t first_name_offset = 0x1fff;
    const std::size_t required_size = first_name_offset +
        kReplayPlayerCount * kReplayPlayerNameBytes;
    if (payload.size() < required_size) {
        return;
    }

    const auto names = RankerMainWindowReplayPlayerNames();
    for (std::size_t i = 0; i < kReplayPlayerCount && i < names.size(); ++i) {
        if (names[i].empty()) {
            continue;
        }
        const std::size_t offset = first_name_offset + i * kReplayPlayerNameBytes;
        std::fill_n(payload.begin() + static_cast<std::ptrdiff_t>(offset),
            kReplayPlayerNameBytes, 0);
        const std::size_t copy_bytes = std::min<std::size_t>(
            names[i].size(), kReplayPlayerNameBytes - 1);
        std::memcpy(payload.data() + offset, names[i].data(), copy_bytes);
    }
}

void append_replay_session_end_packet(ReplayRecordingState& recording) {
    const Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
    const u32 channel = reliable.local_player_index < kReplayChannelCount ?
        reliable.local_player_index :
        static_cast<u32>(recording.header[0x5f]);
    if (channel >= kReplayChannelCount) {
        return;
    }

    std::array<u8, kReplayPacketBytes> packet{};
    write_packet_u32(packet, 0x00, 1);
    write_packet_u32(packet, 0x04, kReplayPacketBytes);
    write_packet_u32(packet, 0x08, GetMode1ReliableExpectedSequence(channel));
    packet[0x0c] = static_cast<u8>(channel);
    packet[0x0f] = 0x13;
    AppendReplayPacketRecord(recording, packet.data(), kReplayPacketBytes,
        reliable.replay_frame_tick);
}

void notify_main_window_resume(ReplayDialogState& state, LPARAM reason) {
    if (state.main_window != nullptr) {
        SendMessageA(state.main_window, WM_USER + 9, 0, reason);
    }
    if (state.callbacks.on_close != nullptr) {
        state.callbacks.on_close(state);
    }
}

bool create_common_dialog_window(ReplayDialogState& state, HWND parent,
    HINSTANCE instance, const char* class_name, const char* title, WNDPROC window_proc,
    u32 layout_record) {
    if (state.window != nullptr) {
        return false;
    }

    initialize_resources(state);
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.layout.clear();
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table, layout_record)) {
        destroy_dialog_resources(state);
        return false;
    }
    state.layout = copy_layout_record(layout.table);
    state.base_replay_directory = resolve_replays_directory();
    state.current_directory = state.base_replay_directory;
    state.selected_replay = ReplayArchiveDescriptor{};
    state.current_recording = ReplayArchiveDescriptor{};
    state.status = ReplayValidationStatus::None;

    const ReplayDialogLayoutRect window_rect = layout_at(state, 0);
    // Keep the fixed 800x600 replay layout inside the game window.  Windowed
    // frontends use the same centered child-window placement as the P2P flow.
    const bool windowed = IsWindow(parent);
    const POINT origin = windowed
        ? RankerCenteredChildFrontendWindowOrigin(parent,
              window_rect.width, window_rect.height)
        : RankerCenteredFrontendWindowOrigin(
              window_rect.width, window_rect.height);
    const DWORD style = windowed ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, class_name, title, style,
        origin.x, origin.y, window_rect.width, window_rect.height, parent,
        nullptr, instance, nullptr);
    if (state.window == nullptr) {
        destroy_dialog_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(window_proc));
    return true;
}

bool create_load_controls(ReplayDialogState& state) {
    const HINSTANCE instance = state.instance;
    if (!create_text_control(state.file_list, state.window, instance, "listbox",
            kListStyle, kReplayLoadListId, layout_at(state, 2),
            replay_load_control_proc) ||
        !CreateLegacyCustomScrollControlWindow(state.scroll, state.window, "Replay",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplayLoadScrollControlId)),
            false, layout_at(state, 3).x, layout_at(state, 3).y,
            layout_at(state, 3).width, layout_at(state, 3).height) ||
        !create_text_control(state.directory_edit, state.window, instance, "edit",
            kReadOnlyEditStyle, kReplayLoadDirectoryEditId, layout_at(state, 1),
            replay_load_control_proc) ||
        !create_image_button(state.info_button, state.window, "Replay infos",
            kReplayLoadInfoButtonId, layout_at(state, 4), 0, 0,
            replay_load_control_proc) ||
        !create_image_button(state.ok_button, state.window, "Replay",
            kReplayLoadOkButtonId, layout_at(state, 5),
            kReplayLoadOkNormalBitmapRecord, kReplayLoadOkPressedBitmapRecord,
            replay_load_control_proc) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kReplayLoadCancelButtonId, layout_at(state, 6),
            kReplayLoadCancelNormalBitmapRecord, kReplayLoadCancelPressedBitmapRecord,
            replay_load_control_proc)) {
        return false;
    }

    state.scroll.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC));
    SetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(replay_load_control_proc));
    LoadLegacyCustomScrollControlBitmaps(state.scroll, kReplayLoadScrollUpBitmapRecord,
        kReplayLoadScrollUpBitmapRecord, kReplayLoadScrollDownBitmapRecord,
        kReplayLoadScrollDownBitmapRecord, kReplayLoadScrollThumbBitmapRecord,
        kReplayLoadScrollTrackBitmapRecord);
    return true;
}

bool create_save_controls(ReplayDialogState& state) {
    const HINSTANCE instance = state.instance;
    if (!create_text_control(state.name_edit, state.window, instance, "edit",
            kSaveNameEditStyle, kReplaySaveNameEditId, layout_at(state, 4),
            replay_save_control_proc) ||
        !create_text_control(state.file_list, state.window, instance, "listbox",
            kListStyle, kReplaySaveListId, layout_at(state, 2),
            replay_save_control_proc) ||
        !CreateLegacyCustomScrollControlWindow(state.scroll, state.window, "ReplaySave",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplaySaveScrollControlId)),
            false, layout_at(state, 3).x, layout_at(state, 3).y,
            layout_at(state, 3).width, layout_at(state, 3).height) ||
        !create_text_control(state.directory_edit, state.window, instance, "edit",
            kReadOnlyEditStyle, kReplaySaveDirectoryEditId, layout_at(state, 1),
            replay_save_control_proc) ||
        !create_image_button(state.info_button, state.window, "Replay infos",
            kReplaySaveInfoButtonId, layout_at(state, 5), 0, 0,
            replay_save_control_proc) ||
        !create_image_button(state.ok_button, state.window, "&Save",
            kReplaySaveOkButtonId, layout_at(state, 6),
            kReplaySaveOkNormalBitmapRecord, kReplaySaveOkPressedBitmapRecord,
            replay_save_control_proc) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kReplaySaveCancelButtonId, layout_at(state, 7),
            kReplaySaveCancelNormalBitmapRecord, kReplaySaveCancelPressedBitmapRecord,
            replay_save_control_proc)) {
        return false;
    }

    state.scroll.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC));
    SetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(replay_save_control_proc));
    LoadLegacyCustomScrollControlBitmaps(state.scroll, kReplaySaveScrollUpBitmapRecord,
        kReplaySaveScrollUpBitmapRecord, kReplaySaveScrollDownBitmapRecord,
        kReplaySaveScrollDownBitmapRecord, kReplaySaveScrollThumbBitmapRecord,
        kReplaySaveScrollTrackBitmapRecord);
    return true;
}

void load_common_bitmaps(ReplayDialogState& state) {
    LoadBitmapMemoryResourceFromTrcRecord(state.up_icon, kReplayArchiveName,
        kReplayUpIconBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.folder_icon, kReplayArchiveName,
        kReplayFolderIconBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.open_folder_icon, kReplayArchiveName,
        kReplayOpenFolderIconBitmapRecord);

    if (state.save_dialog) {
        LoadBitmapMemoryResourceFromTrcRecord(state.background, kReplayArchiveName,
            kReplaySaveBackgroundBitmapRecord);
        LoadBitmapMemoryResourceFromTrcRecord(state.camera_icon, kReplayArchiveName,
            kReplaySaveCameraBitmapRecord);
    } else {
        LoadBitmapMemoryResourceFromTrcRecord(state.background, kReplayArchiveName,
            kReplayLoadBackgroundBitmapRecord);
        LoadBitmapMemoryResourceFromTrcRecord(state.camera_icon, kReplayArchiveName,
            kReplayLoadCameraBitmapRecord);
        LoadBitmapMemoryResourceFromTrcRecord(state.speaker_icon, kReplayArchiveName,
            kReplayLoadSpeakerBitmapRecord);
        LoadBitmapMemoryResourceFromTrcRecord(state.vpos_icon, kReplayArchiveName,
            kReplayLoadVposBitmapRecord);
    }
}

void finish_dialog_creation(ReplayDialogState& state, int accelerator_resource) {
    SendMessageA(state.window, WM_SETFONT, reinterpret_cast<WPARAM>(
        GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageA(state.file_list.window, WM_SETFONT, reinterpret_cast<WPARAM>(
        GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    if (state.name_edit.window != nullptr) {
        SendMessageA(state.name_edit.window, WM_SETFONT, reinterpret_cast<WPARAM>(
            GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(state.name_edit.window, EM_LIMITTEXT, kReplaySaveNameBytes - 1, 0);
    }

    const int row_height = static_cast<int>(
        SendMessageA(state.file_list.window, LB_GETITEMHEIGHT, 0, 0));
    const int list_height = layout_at(state, 2).height;
    state.visible_rows = row_height > 0 ? list_height / row_height : 12;
    state.visible_rows = std::max(1, state.visible_rows);
    SetLegacyCustomScrollControlPageStep(state.scroll, state.visible_rows);

    load_common_bitmaps(state);
    if (state.save_dialog) {
        PopulateReplaySaveList(state);
    } else {
        PopulateReplayLoadList(state);
    }
    SetWindowTextA(state.directory_edit.window, state.current_directory.c_str());
    install_accelerators(state, accelerator_resource);
    ShowWindow(state.file_list.window, SW_SHOW);
    ShowWindow(state.window, SW_SHOW);
    SetFocus(state.save_dialog ? state.name_edit.window : state.file_list.window);
    state.visible = true;
}

} // namespace

ReplayDialogState& replay_load_dialog_state() {
    return g_replay_load_dialog_state;
}

ReplayDialogState& replay_save_dialog_state() {
    return g_replay_save_dialog_state;
}

#define DEFINE_REPLAY_LOAD_BITMAP_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(ReplayDialogState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(ReplayDialogState& state) { \
        InitializeBitmapMemoryResource(Member); \
    } \
    void RegisterName(ReplayDialogState&) { \
        register_atexit_once(g_replay_load_bitmap_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(ReplayDialogState& state) { \
        HandleBitmapMemoryResourceDestructor(Member); \
    }

void InitializeReplayLoadBackground(ReplayDialogState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterReplayLoadBackgroundDestructor(ReplayDialogState&) {
    register_atexit_once(g_replay_load_bitmap_destructor_registered[0],
        shutdown_replay_load_background);
}

void DestroyReplayLoadBackground(ReplayDialogState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

DEFINE_REPLAY_LOAD_BITMAP_LIFETIME(InitializeReplayLoadUpIconStatic,
    InitializeReplayLoadUpIcon,
    RegisterReplayLoadUpIconDestructor,
    DestroyReplayLoadUpIcon, state.up_icon, 1, shutdown_replay_load_up_icon)
DEFINE_REPLAY_LOAD_BITMAP_LIFETIME(InitializeReplayLoadFolderIconStatic,
    InitializeReplayLoadFolderIcon,
    RegisterReplayLoadFolderIconDestructor,
    DestroyReplayLoadFolderIcon, state.folder_icon, 2,
    shutdown_replay_load_folder_icon)
DEFINE_REPLAY_LOAD_BITMAP_LIFETIME(InitializeReplayLoadOpenFolderIconStatic,
    InitializeReplayLoadOpenFolderIcon,
    RegisterReplayLoadOpenFolderIconDestructor,
    DestroyReplayLoadOpenFolderIcon, state.open_folder_icon, 3,
    shutdown_replay_load_open_folder_icon)
DEFINE_REPLAY_LOAD_BITMAP_LIFETIME(InitializeReplayLoadCameraIconStatic,
    InitializeReplayLoadCameraIcon,
    RegisterReplayLoadCameraIconDestructor,
    DestroyReplayLoadCameraIcon, state.camera_icon, 4,
    shutdown_replay_load_camera_icon)
DEFINE_REPLAY_LOAD_BITMAP_LIFETIME(InitializeReplayLoadSpeakerIconStatic,
    InitializeReplayLoadSpeakerIcon,
    RegisterReplayLoadSpeakerIconDestructor,
    DestroyReplayLoadSpeakerIcon, state.speaker_icon, 5,
    shutdown_replay_load_speaker_icon)
DEFINE_REPLAY_LOAD_BITMAP_LIFETIME(InitializeReplayLoadVposIconStatic,
    InitializeReplayLoadVposIcon,
    RegisterReplayLoadVposIconDestructor,
    DestroyReplayLoadVposIcon, state.vpos_icon, 6,
    shutdown_replay_load_vpos_icon)

#undef DEFINE_REPLAY_LOAD_BITMAP_LIFETIME

#define DEFINE_REPLAY_LOAD_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(ReplayDialogState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(ReplayDialogState& state) { \
        InitializeLegacyImageButtonControl(Member); \
    } \
    void RegisterName(ReplayDialogState&) { \
        register_atexit_once(g_replay_load_button_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(ReplayDialogState& state) { \
        DestroyLegacyImageButtonControl(Member); \
    }

DEFINE_REPLAY_LOAD_BUTTON_LIFETIME(InitializeReplayLoadInfoButtonStatic,
    InitializeReplayLoadInfoButton,
    RegisterReplayLoadInfoButtonDestructor,
    DestroyReplayLoadInfoButton, state.info_button, 0,
    shutdown_replay_load_info_button)
DEFINE_REPLAY_LOAD_BUTTON_LIFETIME(InitializeReplayLoadOkButtonStatic,
    InitializeReplayLoadOkButton,
    RegisterReplayLoadOkButtonDestructor,
    DestroyReplayLoadOkButton, state.ok_button, 1,
    shutdown_replay_load_ok_button)
DEFINE_REPLAY_LOAD_BUTTON_LIFETIME(InitializeReplayLoadCancelButtonStatic,
    InitializeReplayLoadCancelButton,
    RegisterReplayLoadCancelButtonDestructor,
    DestroyReplayLoadCancelButton, state.cancel_button, 2,
    shutdown_replay_load_cancel_button)

#undef DEFINE_REPLAY_LOAD_BUTTON_LIFETIME

void InitializeReplayLoadFileListScrollStatic(ReplayDialogState& state) {
    InitializeReplayLoadFileListScroll(state);
    RegisterReplayLoadFileListScrollDestructor(state);
}

void InitializeReplayLoadFileListScroll(ReplayDialogState& state) {
    InitializeLegacyCustomScrollControl(state.scroll);
}

void RegisterReplayLoadFileListScrollDestructor(ReplayDialogState&) {
    register_atexit_once(g_replay_load_scroll_destructor_registered,
        shutdown_replay_load_file_list_scroll);
}

void DestroyReplayLoadFileListScroll(ReplayDialogState& state) {
    DestroyLegacyCustomScrollControl(state.scroll);
}

#define DEFINE_REPLAY_SAVE_BITMAP_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(ReplayDialogState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(ReplayDialogState& state) { \
        InitializeBitmapMemoryResource(Member); \
    } \
    void RegisterName(ReplayDialogState&) { \
        register_atexit_once(g_replay_save_bitmap_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(ReplayDialogState& state) { \
        HandleBitmapMemoryResourceDestructor(Member); \
    }

DEFINE_REPLAY_SAVE_BITMAP_LIFETIME(InitializeReplaySaveBackgroundStatic,
    InitializeReplaySaveBackground,
    RegisterReplaySaveBackgroundDestructor,
    DestroyReplaySaveBackground, state.background, 0,
    shutdown_replay_save_background)
DEFINE_REPLAY_SAVE_BITMAP_LIFETIME(InitializeReplaySaveUpIconStatic,
    InitializeReplaySaveUpIcon,
    RegisterReplaySaveUpIconDestructor,
    DestroyReplaySaveUpIcon, state.up_icon, 1, shutdown_replay_save_up_icon)
DEFINE_REPLAY_SAVE_BITMAP_LIFETIME(InitializeReplaySaveFolderIconStatic,
    InitializeReplaySaveFolderIcon,
    RegisterReplaySaveFolderIconDestructor,
    DestroyReplaySaveFolderIcon, state.folder_icon, 2,
    shutdown_replay_save_folder_icon)
DEFINE_REPLAY_SAVE_BITMAP_LIFETIME(InitializeReplaySaveOpenFolderIconStatic,
    InitializeReplaySaveOpenFolderIcon,
    RegisterReplaySaveOpenFolderIconDestructor,
    DestroyReplaySaveOpenFolderIcon, state.open_folder_icon, 3,
    shutdown_replay_save_open_folder_icon)
DEFINE_REPLAY_SAVE_BITMAP_LIFETIME(InitializeReplaySaveCameraIconStatic,
    InitializeReplaySaveCameraIcon,
    RegisterReplaySaveCameraIconDestructor,
    DestroyReplaySaveCameraIcon, state.camera_icon, 4,
    shutdown_replay_save_camera_icon)

#undef DEFINE_REPLAY_SAVE_BITMAP_LIFETIME

#define DEFINE_REPLAY_SAVE_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Member, Slot, Callback) \
    void StaticName(ReplayDialogState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(ReplayDialogState& state) { \
        InitializeLegacyImageButtonControl(Member); \
    } \
    void RegisterName(ReplayDialogState&) { \
        register_atexit_once(g_replay_save_button_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(ReplayDialogState& state) { \
        DestroyLegacyImageButtonControl(Member); \
    }

DEFINE_REPLAY_SAVE_BUTTON_LIFETIME(InitializeReplaySaveInfoButtonStatic,
    InitializeReplaySaveInfoButton,
    RegisterReplaySaveInfoButtonDestructor,
    DestroyReplaySaveInfoButton, state.info_button, 0,
    shutdown_replay_save_info_button)
DEFINE_REPLAY_SAVE_BUTTON_LIFETIME(InitializeReplaySaveOkButtonStatic,
    InitializeReplaySaveOkButton,
    RegisterReplaySaveOkButtonDestructor,
    DestroyReplaySaveOkButton, state.ok_button, 1, shutdown_replay_save_ok_button)
DEFINE_REPLAY_SAVE_BUTTON_LIFETIME(InitializeReplaySaveCancelButtonStatic,
    InitializeReplaySaveCancelButton,
    RegisterReplaySaveCancelButtonDestructor,
    DestroyReplaySaveCancelButton, state.cancel_button, 2,
    shutdown_replay_save_cancel_button)

#undef DEFINE_REPLAY_SAVE_BUTTON_LIFETIME

void InitializeReplaySaveFileListScrollStatic(ReplayDialogState& state) {
    InitializeReplaySaveFileListScroll(state);
    RegisterReplaySaveFileListScrollDestructor(state);
}

void InitializeReplaySaveFileListScroll(ReplayDialogState& state) {
    InitializeLegacyCustomScrollControl(state.scroll);
}

void RegisterReplaySaveFileListScrollDestructor(ReplayDialogState&) {
    register_atexit_once(g_replay_save_scroll_destructor_registered,
        shutdown_replay_save_file_list_scroll);
}

void DestroyReplaySaveFileListScroll(ReplayDialogState& state) {
    DestroyLegacyCustomScrollControl(state.scroll);
}

bool LoadReplayArchiveDescriptor(const char* path, ReplayArchiveDescriptor& descriptor,
    u32 expected_version) {
    descriptor = ReplayArchiveDescriptor{};
    descriptor.source_path = path == nullptr ? "" : path;
    if (path == nullptr || *path == '\0') {
        descriptor.status = ReplayValidationStatus::OpenFailed;
        return false;
    }

    u32 active_records = 0;
    if (!QueryTrcArchiveRecordCount(path, &active_records, nullptr) ||
        active_records == 0) {
        descriptor.status = ReplayValidationStatus::OpenFailed;
        return false;
    }

    std::vector<u8> payload;
    if (!LoadTrcRecordAlloc(path, active_records - 1, payload)) {
        descriptor.status = ReplayValidationStatus::OpenFailed;
        return false;
    }

    descriptor = descriptor_from_payload(payload, path, expected_version);
    return descriptor.status == ReplayValidationStatus::Valid;
}

bool BuildReplayDescriptorFromRecording(const ReplayRecordingState& recording,
    ReplayArchiveDescriptor& descriptor) {
    const std::vector<u8> payload = build_replay_payload_from_recording(recording);
    descriptor = descriptor_from_payload(payload, recording.last_output_path.c_str(), 0);
    if (descriptor.status == ReplayValidationStatus::VersionMismatch) {
        descriptor.status = ReplayValidationStatus::Valid;
    }
    return descriptor.status == ReplayValidationStatus::Valid;
}

bool SaveReplayRecordingArchive(const char* output_path,
    ReplayRecordingState& recording) {
    if (output_path == nullptr || *output_path == '\0') {
        return false;
    }

    append_replay_session_end_packet(recording);
    std::vector<u8> payload = build_replay_payload_from_recording(recording);
    if (payload.empty()) {
        return false;
    }
    patch_replay_payload_player_names(payload);

    if (!PersistReplayRecordingArchive(output_path, recording, payload)) {
        return false;
    }
    recording.packet_temp_open = false;
    recording.viewport_temp_open = false;
    recording.last_output_path = output_path;
    return true;
}

bool SaveReplayArchiveFromRecording(const char* output_path,
    ReplayRecordingState& recording) {
    return SaveReplayRecordingArchive(output_path, recording);
}

bool OpenReplayLoadDialog(ReplayDialogState& state, HWND parent, HINSTANCE instance) {
    state.save_dialog = false;
    if (!create_common_dialog_window(state, parent, instance, "Replay", "Replay",
            replay_load_window_proc, kReplayLoadLayoutTrcRecord)) {
        return false;
    }
    if (!create_load_controls(state)) {
        DestroyWindow(state.window);
        return false;
    }
    finish_dialog_creation(state, kReplayLoadAcceleratorResourceId);
    return true;
}

bool OpenReplaySaveDialog(ReplayDialogState& state, HWND parent, HINSTANCE instance) {
    state.save_dialog = true;
    if (!create_common_dialog_window(state, parent, instance, "ReplaySave",
            "ReplaySave", replay_save_window_proc, kReplaySaveLayoutTrcRecord)) {
        return false;
    }
    if (!create_save_controls(state)) {
        DestroyWindow(state.window);
        return false;
    }
    BuildReplayDescriptorFromRecording(replay_recording_state(), state.current_recording);
    SetWindowTextA(state.name_edit.window, "");
    finish_dialog_creation(state, kReplaySaveAcceleratorResourceId);
    InvalidateRect(state.info_button.window, nullptr, TRUE);
    return true;
}

void OpenReplayLoadDialog(HWND parent, HINSTANCE instance, void* user_data) {
    (void)user_data;
    OpenReplayLoadDialog(replay_load_dialog_state(), parent, instance);
}

void OpenReplaySaveDialog(HWND parent, HINSTANCE instance, void* user_data) {
    (void)user_data;
    OpenReplaySaveDialog(replay_save_dialog_state(), parent, instance);
}

void RefreshReplayDialogFileList(ReplayDialogState& state) {
    if (state.file_list.window == nullptr) {
        return;
    }

    SendMessageA(state.file_list.window, LB_RESETCONTENT, 0, 0);
    state.entries.clear();
    const std::string current = full_path(state.current_directory);
    const std::string root = full_path(state.base_replay_directory);
    state.current_directory = current;

    if (!equals_ignore_case(current, root)) {
        ReplayFileEntry parent;
        copy_c_string(parent.name, "..");
        copy_c_string(parent.path, full_path(append_path(current, "..")).c_str());
        parent.attributes = FILE_ATTRIBUTE_DIRECTORY;
        parent.directory = true;
        parent.parent = true;
        add_file_entry(state, parent);
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
                ReplayFileEntry entry;
                copy_c_string(entry.name, find_data.cFileName);
                copy_c_string(entry.path,
                    full_path(append_path(current, find_data.cFileName)).c_str());
                entry.attributes = find_data.dwFileAttributes;
                entry.directory = true;
                add_file_entry(state, entry);
                continue;
            }

            if (!has_extension(find_data.cFileName, kReplayFileExtension)) {
                continue;
            }

            ReplayFileEntry entry;
            copy_c_string(entry.name, find_data.cFileName);
            const std::string path = full_path(append_path(current, find_data.cFileName));
            copy_c_string(entry.path, path.c_str());
            entry.attributes = find_data.dwFileAttributes;
            entry.has_replay = true;
            entry.has_mp3 = file_exists(replace_extension(path, kReplayMusicExtension));
            entry.has_vpos = file_exists(replace_extension(path, kReplayVposExtension));
            add_file_entry(state, entry);
        } while (FindNextFileA(find, &find_data));
        FindClose(find);
    }

    SendMessageA(state.file_list.window, LB_SETCURSEL, 0, 0);
    SetWindowTextA(state.directory_edit.window, state.current_directory.c_str());
    update_scroll(state, 0);
    refresh_selection_descriptor(state);
}

void PopulateReplayLoadList(ReplayDialogState& state) {
    const bool was_save_dialog = state.save_dialog;
    state.save_dialog = false;
    RefreshReplayDialogFileList(state);
    state.save_dialog = was_save_dialog;
}

void PopulateReplaySaveList(ReplayDialogState& state) {
    const bool was_save_dialog = state.save_dialog;
    state.save_dialog = true;
    RefreshReplayDialogFileList(state);
    state.save_dialog = was_save_dialog;
}

bool BrowseReplayDialogSelectedDirectory(ReplayDialogState& state) {
    ReplayFileEntry* entry = selected_entry(state);
    if (entry == nullptr || !entry->directory) {
        return false;
    }
    if (!is_directory_path(entry->path.data())) {
        return false;
    }

    state.current_directory = full_path(entry->path.data());
    if (state.save_dialog) {
        PopulateReplaySaveList(state);
    } else {
        PopulateReplayLoadList(state);
    }
    return true;
}

bool StartReplayPlaybackFromSelection(ReplayDialogState&,
    const ReplayArchiveDescriptor& descriptor) {
    if (descriptor.status != ReplayValidationStatus::Valid ||
        descriptor.payload.size() < kReplayHeaderBytes) {
        return false;
    }

    ReplayRecordingState& recording = replay_recording_state();
    recording.playback_mode = true;
    recording.packet_temp_open = false;
    recording.viewport_temp_open = false;
    recording.scenario_ai_profile_override = true;
    recording.playback_archive_path = descriptor.source_path;
    recording.playback_payload = descriptor.payload;
    std::copy_n(descriptor.payload.begin(), kReplayHeaderBytes,
        recording.header.begin());
    recording.packet_count = descriptor.packet_count;
    recording.playback_last_frame_tick = descriptor.last_frame_tick;
    SetRankerMainWindowScenarioAiProfileOverride(true);
    return true;
}

bool SubmitReplayLoadSelection(ReplayDialogState& state) {
    ReplayFileEntry* entry = selected_entry(state);
    if (entry == nullptr || entry->directory) {
        state.selected_replay.status = ReplayValidationStatus::None;
        InvalidateRect(state.info_button.window, nullptr, TRUE);
        return false;
    }

    const u32 expected_version = LoadTrcRecord9Value();
    LoadReplayArchiveDescriptor(entry->path.data(), state.selected_replay,
        expected_version);
    state.status = state.selected_replay.status;
    InvalidateRect(state.info_button.window, nullptr, TRUE);
    if (state.selected_replay.status != ReplayValidationStatus::Valid) {
        return false;
    }

    bool accepted = true;
    if (state.callbacks.start_replay_playback != nullptr) {
        accepted = state.callbacks.start_replay_playback(state, state.selected_replay);
    } else {
        accepted = StartReplayPlaybackFromSelection(state, state.selected_replay);
    }
    if (!accepted) {
        ShowOnlineModalPrompt1(online_modal_prompt_state(), state.window,
            text_row(195, "Replay read error or not enough memory."),
            kReplayWarning);
        return false;
    }

    HWND owner = state.main_window;
    DestroyWindow(state.window);
    state.main_window = owner;
    notify_main_window_resume(state, 3);
    return true;
}

bool SubmitReplaySaveSelection(ReplayDialogState& state) {
    char name[kReplaySaveNameBytes]{};
    if (state.name_edit.window != nullptr) {
        GetWindowTextA(state.name_edit.window, name, static_cast<int>(sizeof(name)));
    }
    if (name[0] == '\0') {
        SetFocus(state.name_edit.window);
        return false;
    }

    std::string leaf = name;
    if (!has_extension(leaf, kReplayFileExtension)) {
        leaf += kReplayFileExtension;
    }
    const std::string output_path = full_path(append_path(state.current_directory,
        leaf.c_str()));
    state.last_output_path = output_path;

    bool saved = false;
    if (state.callbacks.save_replay != nullptr) {
        saved = state.callbacks.save_replay(state, output_path.c_str());
    } else {
        saved = SaveReplayRecordingArchive(output_path.c_str(),
            replay_recording_state());
    }
    if (!saved) {
        state.current_recording.status = ReplayValidationStatus::SaveFailed;
        InvalidateRect(state.info_button.window, nullptr, TRUE);
        return false;
    }

    HWND owner = state.main_window;
    DestroyWindow(state.window);
    state.main_window = owner;
    notify_main_window_resume(state, 1);
    return true;
}

LRESULT HandleReplayLoadWindowMessage(ReplayDialogState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        destroy_dialog_resources(state);
        return 0;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kReplayWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kReplayBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kReplayWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kReplayBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kReplayWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kReplayBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kReplayLoadListId) {
            draw_file_entry(state, *draw);
            return TRUE;
        }
        if (draw->CtlID == kReplayLoadInfoButtonId) {
            DrawReplayInfoPanel(state, state.selected_replay, *draw);
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
        const int notify = HIWORD(wparam);
        if (id == kReplayLoadOkButtonId) {
            play_click_sound(state);
            SubmitReplayLoadSelection(state);
            break;
        }
        if (id == kReplayLoadCancelButtonId) {
            play_click_sound(state);
            HWND owner = state.main_window;
            DestroyWindow(hwnd);
            state.main_window = owner;
            notify_main_window_resume(state, 3);
            break;
        }
        if (id == kReplayLoadListId) {
            if (notify == LBN_SELCHANGE) {
                refresh_selection_descriptor(state);
                const int top = static_cast<int>(
                    SendMessageA(state.file_list.window, LB_GETTOPINDEX, 0, 0));
                SetLegacyCustomScrollControlValue(state.scroll, top, true);
            } else if (notify == LBN_DBLCLK) {
                if (!BrowseReplayDialogSelectedDirectory(state)) {
                    SubmitReplayLoadSelection(state);
                }
            }
            break;
        }
        if (id == kReplayLoadFocusListCommandId) {
            SetFocus(state.file_list.window);
            break;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleReplayLoadControlMessage(ReplayDialogState& state, HWND hwnd,
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
    if (id == kReplayLoadScrollControlId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(state.scroll,
            message, wparam, lparam);
        if (changed && state.file_list.window != nullptr) {
            const int top = GetLegacyCustomScrollControlValue(state.scroll);
            SendMessageA(state.file_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(top), 0);
        }
    }
    if (is_replay_load_control_id(id)) {
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    }
    return 0;
}

LRESULT HandleReplaySaveWindowMessage(ReplayDialogState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        destroy_dialog_resources(state);
        return 0;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kReplayWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kReplayBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kReplayWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kReplayBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kReplayWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kReplayBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kReplaySaveListId) {
            draw_file_entry(state, *draw);
            return TRUE;
        }
        if (draw->CtlID == kReplaySaveInfoButtonId) {
            DrawReplayInfoPanel(state, state.current_recording, *draw);
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
        const int notify = HIWORD(wparam);
        if (id == kReplaySaveOkButtonId) {
            play_click_sound(state);
            SubmitReplaySaveSelection(state);
            break;
        }
        if (id == kReplaySaveCancelButtonId) {
            play_click_sound(state);
            HWND owner = state.main_window;
            DestroyWindow(hwnd);
            state.main_window = owner;
            notify_main_window_resume(state, 1);
            break;
        }
        if (id == kReplaySaveListId) {
            if (notify == LBN_SELCHANGE) {
                const int top = static_cast<int>(
                    SendMessageA(state.file_list.window, LB_GETTOPINDEX, 0, 0));
                SetLegacyCustomScrollControlValue(state.scroll, top, true);
                ReplayFileEntry* entry = selected_entry(state);
                if (entry != nullptr && !entry->directory) {
                    SetWindowTextA(state.name_edit.window, entry->name.data());
                }
            } else if (notify == LBN_DBLCLK) {
                BrowseReplayDialogSelectedDirectory(state);
            }
            break;
        }
        if (id == kReplaySaveFocusNameCommandId) {
            SetFocus(state.name_edit.window);
            break;
        }
        if (id == kReplaySaveFocusListCommandId) {
            SetFocus(state.file_list.window);
            break;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleReplaySaveControlMessage(ReplayDialogState& state, HWND hwnd,
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
    if (id == kReplaySaveScrollControlId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(state.scroll,
            message, wparam, lparam);
        if (changed && state.file_list.window != nullptr) {
            const int top = GetLegacyCustomScrollControlValue(state.scroll);
            SendMessageA(state.file_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(top), 0);
        }
    }
    if (is_replay_save_control_id(id)) {
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    }
    return 0;
}

void DrawReplayInfoPanel(ReplayDialogState& state,
    const ReplayArchiveDescriptor& descriptor, const DRAWITEMSTRUCT& item) {
    draw_replay_info(state, descriptor, item);
}

} // namespace ranker

#endif
