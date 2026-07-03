#pragma once

#include "ranker_image_controls.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>

namespace ranker {

#ifdef _WIN32

constexpr std::size_t kLegacyStringSelectorMaxItems = 100;
constexpr std::size_t kLegacyStringSelectorTextBytes = 0xff;

struct LegacyStringSelectorControl {
    WNDPROC original_window_proc = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int button_width = 0;
    int id = 0;
    int min_index = 0;
    int item_count = 0;
    int selected_index = 0;
    LegacyImageButtonControl increment_button;
    LegacyImageButtonControl decrement_button;
    HWND parent = nullptr;
    HWND window = nullptr;
    std::array<std::array<char, kLegacyStringSelectorTextBytes>,
        kLegacyStringSelectorMaxItems> items{};
    bool buttons_hidden = false;
};

LegacyStringSelectorControl& InitializeLegacyStringSelectorControl(
    LegacyStringSelectorControl& control);
void ResetLegacyStringSelectorControl(LegacyStringSelectorControl& control);
void SetLegacyStringSelectorButtonsHidden(LegacyStringSelectorControl& control,
    bool hidden);
bool ConstructLegacyStringSelectorControl(LegacyStringSelectorControl& control,
    HWND parent, const char* text, HMENU id_or_menu, int x, int y, int width,
    int height, int button_width);
void DestroyLegacyStringSelectorControl(LegacyStringSelectorControl& control);
bool CreateLegacyStringSelectorWindow(LegacyStringSelectorControl& control,
    HWND parent, const char* text, HMENU id_or_menu, int x, int y, int width,
    int height, int button_width);
HWND GetLegacyStringSelectorWindow(const LegacyStringSelectorControl& control);
void LoadLegacyStringSelectorIncrementButtonBitmaps(
    LegacyStringSelectorControl& control, u32 normal_record, u32 pressed_record);
void LoadLegacyStringSelectorDecrementButtonBitmaps(
    LegacyStringSelectorControl& control, u32 normal_record, u32 pressed_record);
void SetLegacyStringSelectorBounds(LegacyStringSelectorControl& control,
    int min_index, int item_count);
void SetLegacyStringSelectorSelectedIndex(LegacyStringSelectorControl& control,
    int selected_index);
int GetLegacyStringSelectorSelectedIndex(const LegacyStringSelectorControl& control);
const char* GetLegacyStringSelectorSelectedText(
    const LegacyStringSelectorControl& control);
void IgnoreLegacyStringSelectorReservedMessage(LPARAM payload);
void AddLegacyStringSelectorText(LegacyStringSelectorControl& control,
    const char* text);
LRESULT HandleLegacyStringSelectorMessage(LegacyStringSelectorControl& control,
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
