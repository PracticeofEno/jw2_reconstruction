#pragma once

#include "ranker_bitmap_resource.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {

#ifdef _WIN32

constexpr DWORD kLegacyImageButtonWindowStyle = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
constexpr const char* kLegacyImageControlBitmapArchive = "Jw2_19.trc";
constexpr COLORREF kLegacyImageComboOuterBorderColor = 0x003d5463;
constexpr COLORREF kLegacyImageComboInnerBorderColor = 0x002b4150;

struct LegacyImageButtonControl {
    WNDPROC original_window_proc = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    BitmapMemoryResource normal_bitmap;
    BitmapMemoryResource pressed_bitmap;
    HWND parent = nullptr;
    HWND window = nullptr;
};

struct LegacyImageComboBoxControl {
    WNDPROC original_window_proc = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    BitmapMemoryResource normal_bitmap;
    BitmapMemoryResource pressed_bitmap;
    HWND parent = nullptr;
    HWND window = nullptr;
    DWORD style = 0;
    int saved_selection = CB_ERR;
    COLORREF outer_border_color = kLegacyImageComboOuterBorderColor;
    COLORREF inner_border_color = kLegacyImageComboInnerBorderColor;
};

LegacyImageButtonControl& InitializeLegacyImageButtonControl(
    LegacyImageButtonControl& control);
bool ConstructLegacyImageButtonControl(LegacyImageButtonControl& control, HWND parent,
    const char* text, HMENU id_or_menu, int x, int y, int width, int height);
bool CreateLegacyImageButtonWindow(LegacyImageButtonControl& control, HWND parent,
    const char* text, HMENU id_or_menu, int x, int y, int width, int height);
void DestroyLegacyImageButtonControl(LegacyImageButtonControl& control);
void ReleaseLegacyImageButtonControlWindow(LegacyImageButtonControl& control);
HWND GetLegacyImageButtonWindow(const LegacyImageButtonControl& control);
void LoadLegacyImageButtonBitmaps(LegacyImageButtonControl& control,
    u32 normal_record, u32 pressed_record);
void DrawLegacyImageButtonItem(LegacyImageButtonControl& control,
    const DRAWITEMSTRUCT& item);

LegacyImageComboBoxControl& InitializeLegacyImageComboBoxControl(
    LegacyImageComboBoxControl& control);
bool ConstructLegacyImageComboBoxControl(LegacyImageComboBoxControl& control,
    HWND parent, const char* text, HMENU id_or_menu, DWORD style, int x, int y,
    int width, int height);
bool CreateLegacyImageComboBoxWindow(LegacyImageComboBoxControl& control, HWND parent,
    const char* text, HMENU id_or_menu, DWORD style, int x, int y, int width,
    int height);
void DestroyLegacyImageComboBoxControl(LegacyImageComboBoxControl& control);
void LoadLegacyImageComboBoxBitmaps(LegacyImageComboBoxControl& control,
    u32 normal_record, u32 pressed_record);
void SetLegacyImageComboBoxColors(LegacyImageComboBoxControl& control,
    COLORREF outer_border, COLORREF inner_border);
void DrawLegacyImageComboBoxItem(LegacyImageComboBoxControl& control,
    const DRAWITEMSTRUCT& item);
void PaintLegacyImageComboBoxBackground(LegacyImageComboBoxControl& control);

#endif

}
