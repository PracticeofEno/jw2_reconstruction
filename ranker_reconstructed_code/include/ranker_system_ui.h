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
constexpr u32 kRankerUiFontCount = 5;

struct RankerDiskFreeSpaceState {
    std::string root_path;
    const char* root_path_pointer = nullptr;
    DWORD sectors_per_cluster = 0;
    DWORD bytes_per_sector = 0;
    DWORD free_clusters = 0;
    DWORD total_clusters = 0;
    bool query_ok = false;
};

struct RankerDirectoryEnsureState {
    std::string path;
    const char* path_pointer = nullptr;
    bool failed = false;
};

struct RankerUiFontState {
    std::array<LOGFONTA, kRankerUiFontCount> definitions{};
    std::array<HFONT, kRankerUiFontCount> handles{};
    HFONT selected = nullptr;
    u32 selected_height = 12;
    i32 average_char_width = 0;
    i32 font_height = 0;
    i32 last_char_width = 0;
    bool metrics_initialized = false;
};

struct RankerImeState {
    bool saved_conversion_open = false;
    bool target_conversion_open = false;
    bool composition_key_active = false;
    BYTE lead_byte = 0;
    BYTE trail_byte = 0;
};

struct RankerSystemUiState {
    RankerDiskFreeSpaceState disk{};
    RankerDirectoryEnsureState directory{};
    RankerUiFontState fonts{};
    RankerImeState ime{};
    SIZE last_text_extent{};
    DWORD last_error = ERROR_SUCCESS;
};

RankerSystemUiState& system_ui_state();

void SetDiskFreeSpaceRoot(const char* root_path);
void SetDiskFreeSpaceRootPointer(const char* root_path);
bool GetLegacyDiskFreeSpace();
void SetDirectoryEnsurePath(const char* path);
void SetDirectoryEnsurePathPointer(const char* path);
bool HandleDirectoryTreeEnsure();
bool DrawCompatibleBitmap(HDC target_dc, HBITMAP bitmap, i16 x, i16 y);
int MeasureGdiTextWidth(HDC dc, const char* text);
void InitializeUiFontHandles();
void ShutdownUiFontHandles();
HFONT GetUiFontHandle(u32 index);
void SetWin32UiFontByHeight(u32 height);
bool InitializeWin32UiFontMetrics(HDC dc);
int MeasureWin32FontCharacterWidth(HDC dc, UINT ch);
int CalculateWin32FontDbcsRunWidth(HDC dc, const char* text);
SIZE CalculateWin32FontRunExtent(HDC dc, const char* text);
void HandleDefaultMessageBeep();
bool RefreshImeConversionOpenStatus(HWND window);
void SetImeConversionOpenTarget(bool open);
bool RestoreImeConversionOpenStatus(HWND window);
void RecordImeCompositionKeyStatus(WPARAM wparam, LPARAM lparam);
bool CheckDbcsLeadByte(UINT code_page, BYTE value);
#endif

}
