#include "ranker_legacy_environment.h"

#ifdef _WIN32

#include "ranker_crt_runtime.h"
#include "ranker_input.h"
#include "ranker_legacy_file.h"

#include <cstring>
#include <ctime>

namespace ranker {
namespace {

LegacyEnvironmentState g_environment_state;
constexpr std::size_t kLegacyMediaPathBufferSize = 0x100;

void build_selected_cdrom_stack_path(
    char (&path)[kLegacyMediaPathBufferSize],
    const std::string& relative_path) {
    std::memset(path, 0xcc, sizeof(path));
    path[0] = '\0';
    CrtStrCat(path, g_environment_state.selected_cdrom_root.c_str());
    CrtStrCat(path, relative_path.c_str());
}

bool has_pending_input() {
    const auto& state = input_state();
    return state.head != state.tail;
}

}

LegacyEnvironmentState& legacy_environment_state() {
    return g_environment_state;
}

u32 GetLegacyCurrentTimeSeconds() {
    const std::time_t now = std::time(nullptr);
    if (now < 0) {
        return 0;
    }
    return static_cast<u32>(now);
}

void SetLegacyWaitSeconds(u32 seconds) {
    g_environment_state.wait_seconds = seconds;
}

void WaitLegacyDelayInterval() {
    const u32 end_time = GetLegacyCurrentTimeSeconds() +
        g_environment_state.wait_seconds;
    while (GetLegacyCurrentTimeSeconds() < end_time) {
    }
}

void WaitLegacyDelayOrInputEvent() {
    auto& state = g_environment_state;
    const u32 end_time = GetLegacyCurrentTimeSeconds() + state.wait_seconds;
    state.wait_interrupted_by_input = false;

    while (GetLegacyCurrentTimeSeconds() < end_time) {
        if (has_pending_input()) {
            state.wait_interrupted_by_input = true;
            ResetInputState();
            break;
        }
    }
}

void EnumerateLogicalDriveStrings() {
    auto& state = g_environment_state;
    state.logical_drive_mask = GetLogicalDrives();
    state.logical_drive_string_length = GetLogicalDriveStringsA(
        static_cast<DWORD>(state.logical_drive_strings.size()),
        state.logical_drive_strings.data());
}

bool SelectNextCdRomDrive() {
    auto& state = g_environment_state;
    while (state.drive_scan_offset < state.logical_drive_strings.size()) {
        const char* drive = state.logical_drive_strings.data() + state.drive_scan_offset;
        if (*drive == '\0') {
            state.selected_cdrom_root.clear();
            return false;
        }

        const std::size_t length = std::strlen(drive);
        state.drive_scan_offset += length + 1;
        if (GetDriveTypeA(drive) == DRIVE_CDROM) {
            state.selected_cdrom_root = drive;
            return true;
        }
    }

    state.selected_cdrom_root.clear();
    return false;
}

bool InitializeCdRomDriveScan() {
    EnumerateLogicalDriveStrings();
    auto& state = g_environment_state;
    state.selected_cdrom_root.clear();
    state.drive_scan_offset = 0;
    return SelectNextCdRomDrive();
}

bool CheckSelectedCdRomRequiredFile() {
    auto& state = g_environment_state;
    char path[kLegacyMediaPathBufferSize];
    build_selected_cdrom_stack_path(path, state.required_media_relative_path);

    CrtFindDataA find_data;
    std::memset(&find_data, 0xcc, sizeof(find_data));
    const HANDLE handle = CrtFindFirstFile(path, find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        state.required_media_missing = true;
        return false;
    }

    CrtFindClose(handle);
    state.required_media_missing = false;
    return true;
}

bool CheckSelectedCdRomWritableFile() {
    auto& state = g_environment_state;
    char path[kLegacyMediaPathBufferSize];
    build_selected_cdrom_stack_path(path, state.writable_probe_relative_path);
    SetLegacyFilePathPointer(path);
    HandleLegacyFileTruncatePath();
    if (legacy_file_state().file_handle == HFILE_ERROR) {
        state.writable_probe_failed = true;
        return false;
    }

    state.writable_probe_failed = false;
    HandleLegacyFileReleaseHandle();
    return true;
}

void PrefixSelectedCdRomPath() {
    auto& state = g_environment_state;
    char path[kLegacyMediaPathBufferSize];
    build_selected_cdrom_stack_path(path, state.prefix_target_path);
    state.prefix_target_path = path;
}

void SetRequiredMediaRelativePath(const char* path) {
    g_environment_state.required_media_relative_path = path == nullptr ? "" : path;
}

void SetWritableProbeRelativePath(const char* path) {
    g_environment_state.writable_probe_relative_path = path == nullptr ? "" : path;
}

void SetPrefixTargetPath(const char* path) {
    g_environment_state.prefix_target_path = path == nullptr ? "" : path;
}

}

#endif
