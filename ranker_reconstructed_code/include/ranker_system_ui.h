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
    // CP949 bytes currently owned by the IME composition window.  They are
    // presentation-only until Windows emits the corresponding WM_CHAR result.
    std::string composition_text;
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

// Converts an editable UTF-8 source literal (for example, u8"한글") into the
// CP949 byte string expected by the original game's Win32 ANSI UI controls.
inline std::string Utf8ToCp949(const char* utf8_text) {
    if (utf8_text == nullptr || *utf8_text == '\0') {
        return {};
    }

    const int wide_chars = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8_text, -1, nullptr, 0);
    if (wide_chars <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(wide_chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_text, -1,
            wide.data(), wide_chars) <= 0) {
        return {};
    }

    constexpr UINT kCp949 = 949;
    BOOL used_default_character = FALSE;
    const int cp949_bytes = WideCharToMultiByte(kCp949, WC_NO_BEST_FIT_CHARS,
        wide.c_str(), -1, nullptr, 0, nullptr, &used_default_character);
    if (cp949_bytes <= 0 || used_default_character != FALSE) {
        return {};
    }

    std::string result(static_cast<std::size_t>(cp949_bytes), '\0');
    used_default_character = FALSE;
    if (WideCharToMultiByte(kCp949, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1,
            result.data(), cp949_bytes, nullptr, &used_default_character) <= 0 ||
        used_default_character != FALSE) {
        return {};
    }
    result.pop_back();
    return result;
}

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
// Creates a caller-owned copy of one of the legacy UI fonts whose pixel
// height follows the configured frontend presentation height.
HFONT CreateScaledFrontendUiFont(u32 index);
void SetWin32UiFontByHeight(u32 height);
bool InitializeWin32UiFontMetrics(HDC dc);
int MeasureWin32FontCharacterWidth(HDC dc, UINT ch);
int CalculateWin32FontDbcsRunWidth(HDC dc, const char* text);
SIZE CalculateWin32FontRunExtent(HDC dc, const char* text);
void HandleDefaultMessageBeep();
bool RefreshImeConversionOpenStatus(HWND window);
void SetImeConversionOpenTarget(bool open);
bool RestoreImeConversionOpenStatus(HWND window);
void RecordImeCompositionKeyStatus(HWND window, WPARAM wparam, LPARAM lparam);
std::string CurrentImeCompositionText();
void ClearImeCompositionText();

inline std::string BuildImeCompositionPresentationText(
    const char* committed_text, const std::string& composition_text) {
    std::string display = committed_text != nullptr ? committed_text : "";
    const std::size_t cursor =
        !display.empty() && display.back() == '_' ?
        display.size() - 1u : display.size();
    display.insert(cursor, composition_text);
    return display;
}

bool CheckDbcsLeadByte(UINT code_page, BYTE value);
#endif

}
