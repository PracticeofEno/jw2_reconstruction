#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>

namespace ranker {

#ifdef _WIN32

struct LegacyEnvironmentState {
    u32 wait_seconds = 0;
    bool wait_interrupted_by_input = false;
    DWORD logical_drive_mask = 0;
    DWORD logical_drive_string_length = 0;
    std::array<char, 0x200> logical_drive_strings{};
    std::size_t drive_scan_offset = 0;
    std::string selected_cdrom_root;
    std::string required_media_relative_path;
    std::string writable_probe_relative_path;
    std::string prefix_target_path;
    bool required_media_missing = false;
    bool writable_probe_failed = false;
};

LegacyEnvironmentState& legacy_environment_state();

u32 GetLegacyCurrentTimeSeconds();
void SetLegacyWaitSeconds(u32 seconds);
void WaitLegacyDelayInterval();
void WaitLegacyDelayOrInputEvent();

void EnumerateLogicalDriveStrings();
bool SelectNextCdRomDrive();
bool InitializeCdRomDriveScan();
bool CheckSelectedCdRomRequiredFile();
bool CheckSelectedCdRomWritableFile();
void PrefixSelectedCdRomPath();

void SetRequiredMediaRelativePath(const char* path);
void SetWritableProbeRelativePath(const char* path);
void SetPrefixTargetPath(const char* path);

#endif

}
