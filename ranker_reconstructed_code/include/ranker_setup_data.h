#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <cstddef>

namespace ranker {

#ifdef _WIN32

constexpr std::size_t kSetupDataBytes = 0x1ff0;
constexpr std::size_t kSetupSignatureBytes = 0x0f;
constexpr const char* kSetupSignatureText = "JW2 SETUP DATA ";
constexpr std::size_t kSetupLaunchCountOffset = 0x14;
constexpr std::size_t kSetupGameplaySpeedOffset = 0x1c;
constexpr std::size_t kSetupSecondaryTimingOffset = 0x20;
constexpr std::size_t kSetupVisibilityGateOffset = 0x24;
constexpr std::size_t kSetupScreenWidthOffset = 0x30;
constexpr std::size_t kSetupScreenHeightOffset = 0x34;
constexpr std::size_t kSetupUiFlagsOffset = 0x38;
constexpr std::size_t kSetupPrimaryMusicRawVolumeOffset = 0x3c;
constexpr std::size_t kSetupGameplaySoundListenerOffset = 0x40;
constexpr std::size_t kSetupScrollSpeedOffset = 0x48;
constexpr std::size_t kSetupWizardAccountOffset = 0x2d0;
constexpr std::size_t kSetupIpxPlayerNameOffset = 0x330;
constexpr std::size_t kSetupFigsEntryOffset = 0x3d0;
constexpr std::size_t kSetupFigsEntryStride = 0xc0;
constexpr std::size_t kSetupFigsNameOffset = 0x00;
constexpr std::size_t kSetupFigsAddressOffset = 0x30;

using SetupDataWriteFailureCallback = void (*)(const char* path, void* user_data);

void SetDefaultSetupDataWriteFailureCallback(
    SetupDataWriteFailureCallback callback, void* user_data);
void InitializeDefaultSetupDataBuffer();
bool LoadDefaultSetupDataBuffer();
bool WriteDefaultSetupDataBuffer();

u32 ImportSetupU32(std::size_t offset, u32 default_value = 0);
i32 ImportSetupI32(std::size_t offset, i32 default_value = 0);
void ExportSetupU32(std::size_t offset, u32 value);
void ExportSetupI32(std::size_t offset, i32 value);

void ImportSetupText(char* target, std::size_t target_size, std::size_t offset);
void ExportSetupText(std::size_t offset, const char* source,
    std::size_t source_size);

template <std::size_t N>
void ImportSetupText(std::array<char, N>& target, std::size_t offset) {
    ImportSetupText(target.data(), target.size(), offset);
}

template <std::size_t N>
void ExportSetupText(std::size_t offset, const std::array<char, N>& source) {
    ExportSetupText(offset, source.data(), source.size());
}

#endif

}
