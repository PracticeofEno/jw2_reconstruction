#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_replay.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr u32 kReplayLoadLayoutTrcRecord = 0x173;
constexpr u32 kReplayLoadBackgroundBitmapRecord = 0xfb;
constexpr u32 kReplayLoadOkNormalBitmapRecord = 0xfc;
constexpr u32 kReplayLoadOkPressedBitmapRecord = 0xfd;
constexpr u32 kReplayLoadCancelNormalBitmapRecord = 0xfe;
constexpr u32 kReplayLoadCancelPressedBitmapRecord = 0xff;
constexpr u32 kReplayLoadCameraBitmapRecord = 0x100;
constexpr u32 kReplayLoadScrollUpBitmapRecord = 0x101;
constexpr u32 kReplayLoadScrollDownBitmapRecord = 0x102;
constexpr u32 kReplayLoadScrollThumbBitmapRecord = 0x103;
constexpr u32 kReplayLoadScrollTrackBitmapRecord = 0x104;
constexpr u32 kReplayLoadSpeakerBitmapRecord = 0x105;
constexpr u32 kReplayLoadVposBitmapRecord = 0x106;
constexpr int kReplayLoadAcceleratorResourceId = 0x0b4;

constexpr u32 kReplaySaveLayoutTrcRecord = 0x174;
constexpr u32 kReplaySaveBackgroundBitmapRecord = 0x107;
constexpr u32 kReplaySaveOkNormalBitmapRecord = 0x108;
constexpr u32 kReplaySaveOkPressedBitmapRecord = 0x109;
constexpr u32 kReplaySaveCancelNormalBitmapRecord = 0x10a;
constexpr u32 kReplaySaveCancelPressedBitmapRecord = 0x10b;
constexpr u32 kReplaySaveCameraBitmapRecord = 0x10c;
constexpr u32 kReplaySaveScrollUpBitmapRecord = 0x10d;
constexpr u32 kReplaySaveScrollDownBitmapRecord = 0x10e;
constexpr u32 kReplaySaveScrollThumbBitmapRecord = 0x10f;
constexpr u32 kReplaySaveScrollTrackBitmapRecord = 0x110;
constexpr int kReplaySaveAcceleratorResourceId = 0x0aa;

constexpr u32 kReplayUpIconBitmapRecord = 0x6f;
constexpr u32 kReplayFolderIconBitmapRecord = 0x70;
constexpr u32 kReplayOpenFolderIconBitmapRecord = 0x72;

constexpr int kReplayLoadInfoButtonId = 0x709;
constexpr int kReplayLoadListId = 0x70a;
constexpr int kReplayLoadScrollControlId = 0x70b;
constexpr int kReplayLoadDirectoryEditId = 0x70c;
constexpr int kReplayLoadOkButtonId = 0x70d;
constexpr int kReplayLoadFocusListCommandId = 0x70e;
constexpr int kReplayLoadCancelButtonId = IDCANCEL;

constexpr int kReplaySaveNameEditId = 0x6a4;
constexpr int kReplaySaveInfoButtonId = 0x6a5;
constexpr int kReplaySaveListId = 0x6a6;
constexpr int kReplaySaveScrollControlId = 0x6a7;
constexpr int kReplaySaveDirectoryEditId = 0x6a8;
constexpr int kReplaySaveOkButtonId = 0x6a9;
constexpr int kReplaySaveFocusNameCommandId = 0x6aa;
constexpr int kReplaySaveFocusListCommandId = 0x6ac;
constexpr int kReplaySaveCancelButtonId = IDCANCEL;

constexpr std::size_t kReplaySaveNameBytes = 0x14;
constexpr std::size_t kReplayDateBytes = 0x10;
constexpr std::size_t kReplayTimeBytes = 0x10;
constexpr std::size_t kReplayPlayerNameBytes = 0x20;
constexpr std::size_t kReplayPlayerCount = 8;

struct ReplayDialogState;

enum class ReplayValidationStatus {
    None = 0,
    Valid = 1,
    OpenFailed = 2,
    Invalid = 3,
    VersionMismatch = 4,
    SaveFailed = 5,
};

struct ReplayDialogLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ReplayDialogTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct ReplayArchiveDescriptor {
    ReplayValidationStatus status = ReplayValidationStatus::None;
    std::string source_path;
    u32 version = 0;
    u8 mode = 0;
    u8 local_player = 0;
    u8 game_type = 0;
    u32 packet_count = 0;
    u32 last_frame_tick = 0;
    std::array<char, kReplayDateBytes> date{};
    std::array<char, kReplayTimeBytes> time{};
    std::array<char, MAX_PATH> map_path{};
    std::array<std::array<char, kReplayPlayerNameBytes>, kReplayPlayerCount> players{};
    std::vector<u8> payload;
};

struct ReplayFileEntry {
    std::array<char, MAX_PATH> name{};
    std::array<char, MAX_PATH> path{};
    DWORD attributes = 0;
    bool directory = false;
    bool parent = false;
    bool has_replay = false;
    bool has_mp3 = false;
    bool has_vpos = false;
};

using ReplayDialogActionCallback = void (*)(ReplayDialogState& state);
using ReplayDialogSaveCallback = bool (*)(ReplayDialogState& state, const char* path);
using ReplayDialogLoadCallback = bool (*)(ReplayDialogState& state,
    const ReplayArchiveDescriptor& descriptor);

struct ReplayDialogCallbacks {
    ReplayDialogActionCallback play_click_sound = nullptr;
    ReplayDialogActionCallback on_close = nullptr;
    ReplayDialogLoadCallback start_replay_playback = nullptr;
    ReplayDialogSaveCallback save_replay = nullptr;
};

struct ReplayDialogState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    HFONT info_font = nullptr;
    HFONT list_font = nullptr;
    bool save_dialog = false;
    bool visible = false;

    BitmapMemoryResource background;
    BitmapMemoryResource up_icon;
    BitmapMemoryResource folder_icon;
    BitmapMemoryResource open_folder_icon;
    BitmapMemoryResource camera_icon;
    BitmapMemoryResource speaker_icon;
    BitmapMemoryResource vpos_icon;
    ReplayDialogTextControl name_edit;
    ReplayDialogTextControl directory_edit;
    ReplayDialogTextControl file_list;
    LegacyCustomScrollControl scroll;
    LegacyImageButtonControl info_button;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<ReplayDialogLayoutRect> layout;
    std::vector<ReplayFileEntry> entries;
    ReplayArchiveDescriptor selected_replay;
    ReplayArchiveDescriptor current_recording;
    std::string base_replay_directory;
    std::string current_directory;
    std::string last_output_path;
    ReplayValidationStatus status = ReplayValidationStatus::None;
    int visible_rows = 12;
    ReplayDialogCallbacks callbacks{};
};

ReplayDialogState& replay_load_dialog_state();
ReplayDialogState& replay_save_dialog_state();

void InitializeReplayLoadBackground(ReplayDialogState& state);
void RegisterReplayLoadBackgroundDestructor(ReplayDialogState& state);
void DestroyReplayLoadBackground(ReplayDialogState& state);
void InitializeReplayLoadUpIconStatic(ReplayDialogState& state);
void InitializeReplayLoadUpIcon(ReplayDialogState& state);
void RegisterReplayLoadUpIconDestructor(ReplayDialogState& state);
void DestroyReplayLoadUpIcon(ReplayDialogState& state);
void InitializeReplayLoadFolderIconStatic(ReplayDialogState& state);
void InitializeReplayLoadFolderIcon(ReplayDialogState& state);
void RegisterReplayLoadFolderIconDestructor(ReplayDialogState& state);
void DestroyReplayLoadFolderIcon(ReplayDialogState& state);
void InitializeReplayLoadOpenFolderIconStatic(ReplayDialogState& state);
void InitializeReplayLoadOpenFolderIcon(ReplayDialogState& state);
void RegisterReplayLoadOpenFolderIconDestructor(ReplayDialogState& state);
void DestroyReplayLoadOpenFolderIcon(ReplayDialogState& state);
void InitializeReplayLoadCameraIconStatic(ReplayDialogState& state);
void InitializeReplayLoadCameraIcon(ReplayDialogState& state);
void RegisterReplayLoadCameraIconDestructor(ReplayDialogState& state);
void DestroyReplayLoadCameraIcon(ReplayDialogState& state);
void InitializeReplayLoadSpeakerIconStatic(ReplayDialogState& state);
void InitializeReplayLoadSpeakerIcon(ReplayDialogState& state);
void RegisterReplayLoadSpeakerIconDestructor(ReplayDialogState& state);
void DestroyReplayLoadSpeakerIcon(ReplayDialogState& state);
void InitializeReplayLoadVposIconStatic(ReplayDialogState& state);
void InitializeReplayLoadVposIcon(ReplayDialogState& state);
void RegisterReplayLoadVposIconDestructor(ReplayDialogState& state);
void DestroyReplayLoadVposIcon(ReplayDialogState& state);
void InitializeReplayLoadInfoButtonStatic(ReplayDialogState& state);
void InitializeReplayLoadInfoButton(ReplayDialogState& state);
void RegisterReplayLoadInfoButtonDestructor(ReplayDialogState& state);
void DestroyReplayLoadInfoButton(ReplayDialogState& state);
void InitializeReplayLoadOkButtonStatic(ReplayDialogState& state);
void InitializeReplayLoadOkButton(ReplayDialogState& state);
void RegisterReplayLoadOkButtonDestructor(ReplayDialogState& state);
void DestroyReplayLoadOkButton(ReplayDialogState& state);
void InitializeReplayLoadCancelButtonStatic(ReplayDialogState& state);
void InitializeReplayLoadCancelButton(ReplayDialogState& state);
void RegisterReplayLoadCancelButtonDestructor(ReplayDialogState& state);
void DestroyReplayLoadCancelButton(ReplayDialogState& state);
void InitializeReplayLoadFileListScrollStatic(ReplayDialogState& state);
void InitializeReplayLoadFileListScroll(ReplayDialogState& state);
void RegisterReplayLoadFileListScrollDestructor(ReplayDialogState& state);
void DestroyReplayLoadFileListScroll(ReplayDialogState& state);
void InitializeReplaySaveBackgroundStatic(ReplayDialogState& state);
void InitializeReplaySaveBackground(ReplayDialogState& state);
void RegisterReplaySaveBackgroundDestructor(ReplayDialogState& state);
void DestroyReplaySaveBackground(ReplayDialogState& state);
void InitializeReplaySaveUpIconStatic(ReplayDialogState& state);
void InitializeReplaySaveUpIcon(ReplayDialogState& state);
void RegisterReplaySaveUpIconDestructor(ReplayDialogState& state);
void DestroyReplaySaveUpIcon(ReplayDialogState& state);
void InitializeReplaySaveFolderIconStatic(ReplayDialogState& state);
void InitializeReplaySaveFolderIcon(ReplayDialogState& state);
void RegisterReplaySaveFolderIconDestructor(ReplayDialogState& state);
void DestroyReplaySaveFolderIcon(ReplayDialogState& state);
void InitializeReplaySaveOpenFolderIconStatic(ReplayDialogState& state);
void InitializeReplaySaveOpenFolderIcon(ReplayDialogState& state);
void RegisterReplaySaveOpenFolderIconDestructor(ReplayDialogState& state);
void DestroyReplaySaveOpenFolderIcon(ReplayDialogState& state);
void InitializeReplaySaveCameraIconStatic(ReplayDialogState& state);
void InitializeReplaySaveCameraIcon(ReplayDialogState& state);
void RegisterReplaySaveCameraIconDestructor(ReplayDialogState& state);
void DestroyReplaySaveCameraIcon(ReplayDialogState& state);
void InitializeReplaySaveInfoButtonStatic(ReplayDialogState& state);
void InitializeReplaySaveInfoButton(ReplayDialogState& state);
void RegisterReplaySaveInfoButtonDestructor(ReplayDialogState& state);
void DestroyReplaySaveInfoButton(ReplayDialogState& state);
void InitializeReplaySaveOkButtonStatic(ReplayDialogState& state);
void InitializeReplaySaveOkButton(ReplayDialogState& state);
void RegisterReplaySaveOkButtonDestructor(ReplayDialogState& state);
void DestroyReplaySaveOkButton(ReplayDialogState& state);
void InitializeReplaySaveCancelButtonStatic(ReplayDialogState& state);
void InitializeReplaySaveCancelButton(ReplayDialogState& state);
void RegisterReplaySaveCancelButtonDestructor(ReplayDialogState& state);
void DestroyReplaySaveCancelButton(ReplayDialogState& state);
void InitializeReplaySaveFileListScrollStatic(ReplayDialogState& state);
void InitializeReplaySaveFileListScroll(ReplayDialogState& state);
void RegisterReplaySaveFileListScrollDestructor(ReplayDialogState& state);
void DestroyReplaySaveFileListScroll(ReplayDialogState& state);

bool LoadReplayArchiveDescriptor(const char* path, ReplayArchiveDescriptor& descriptor,
    u32 expected_version = 0);
bool BuildReplayDescriptorFromRecording(const ReplayRecordingState& recording,
    ReplayArchiveDescriptor& descriptor);
bool SaveReplayRecordingArchive(const char* output_path,
    ReplayRecordingState& recording);
bool SaveReplayRecordingArchiveSnapshot(const char* output_path,
    const ReplayRecordingState& recording);
bool AutoSaveReplayRecordingArchive(const ReplayRecordingState& recording,
    const std::array<std::string, kReplayChannelCount>& player_names,
    std::string& output_path);
bool SaveReplayArchiveFromRecording(const char* output_path,
    ReplayRecordingState& recording);

bool OpenReplayLoadDialog(ReplayDialogState& state, HWND parent, HINSTANCE instance);
bool OpenReplaySaveDialog(ReplayDialogState& state, HWND parent, HINSTANCE instance);
void OpenReplayLoadDialog(HWND parent, HINSTANCE instance, void* user_data = nullptr);
void OpenReplaySaveDialog(HWND parent, HINSTANCE instance, void* user_data = nullptr);

void RefreshReplayDialogFileList(ReplayDialogState& state);
void PopulateReplayLoadList(ReplayDialogState& state);
void PopulateReplaySaveList(ReplayDialogState& state);
bool BrowseReplayDialogSelectedDirectory(ReplayDialogState& state);
bool StartReplayPlaybackFromSelection(ReplayDialogState& state,
    const ReplayArchiveDescriptor& descriptor);
bool SubmitReplayLoadSelection(ReplayDialogState& state);
bool SubmitReplaySaveSelection(ReplayDialogState& state);
void DrawReplayInfoPanel(ReplayDialogState& state,
    const ReplayArchiveDescriptor& descriptor, const DRAWITEMSTRUCT& item);

LRESULT HandleReplayLoadWindowMessage(ReplayDialogState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleReplayLoadControlMessage(ReplayDialogState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleReplaySaveWindowMessage(ReplayDialogState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleReplaySaveControlMessage(ReplayDialogState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

} // namespace ranker
