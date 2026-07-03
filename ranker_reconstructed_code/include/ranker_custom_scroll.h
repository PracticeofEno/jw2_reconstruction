#pragma once

#include "ranker_bitmap_resource.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {

#ifdef _WIN32

constexpr const char* kLegacyCustomScrollBitmapArchive = "Jw2_19.trc";

enum class LegacyCustomScrollHit : int {
    None = 0,
    BackArrow = 1,
    ForwardArrow = 2,
    PageBack = 3,
    PageForward = 4,
    Thumb = 5,
};

struct LegacyCustomScrollControl {
    WNDPROC original_window_proc = nullptr;
    BitmapMemoryResource start_bitmap;
    BitmapMemoryResource start_pressed_bitmap;
    BitmapMemoryResource end_bitmap;
    BitmapMemoryResource end_pressed_bitmap;
    BitmapMemoryResource thumb_bitmap;
    BitmapMemoryResource track_bitmap;
    bool horizontal = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int track_length = 0;
    int thumb_position = 0;
    bool visible = false;
    int value = 0;
    int min_value = 0;
    int max_value = 100;
    int page_step = 10;
    bool dragging = false;
    int horizontal_margin = 0x0e;
    int vertical_margin = 0x0e;
    int thumb_width = 0x0e;
    int thumb_height = 0x0e;
    HWND parent = nullptr;
    HWND window = nullptr;
};

LegacyCustomScrollControl& InitializeLegacyCustomScrollControl(
    LegacyCustomScrollControl& control);
bool ConstructLegacyCustomScrollControl(LegacyCustomScrollControl& control,
    HWND parent, const char* text, HMENU id_or_menu, bool horizontal, int x, int y,
    int width, int height);
bool CreateLegacyCustomScrollControlWindow(LegacyCustomScrollControl& control,
    HWND parent, const char* text, HMENU id_or_menu, bool horizontal, int x, int y,
    int width, int height);
void DestroyLegacyCustomScrollControl(LegacyCustomScrollControl& control);
HWND GetLegacyCustomScrollControlWindow(const LegacyCustomScrollControl& control);
void LoadLegacyCustomScrollControlBitmaps(LegacyCustomScrollControl& control,
    u32 start_record, u32 start_pressed_record, u32 end_record,
    u32 end_pressed_record, u32 thumb_record, u32 track_record);
void SetLegacyCustomScrollControlMetrics(LegacyCustomScrollControl& control,
    int horizontal_margin, int vertical_margin, int thumb_width, int thumb_height);
void DrawLegacyCustomScrollControl(LegacyCustomScrollControl& control, HDC dc);
void SetLegacyCustomScrollControlVisible(LegacyCustomScrollControl& control,
    bool visible);
int GetLegacyCustomScrollControlValue(const LegacyCustomScrollControl& control);
void SetLegacyCustomScrollControlValue(LegacyCustomScrollControl& control,
    int value, bool redraw);
void GetLegacyCustomScrollControlRange(const LegacyCustomScrollControl& control,
    int* min_value, int* max_value);
void SetLegacyCustomScrollControlRange(LegacyCustomScrollControl& control,
    int min_value, int max_value, bool redraw);
void SetLegacyCustomScrollControlPageStep(LegacyCustomScrollControl& control,
    int page_step);
LegacyCustomScrollHit HitTestLegacyCustomScrollControl(
    LegacyCustomScrollControl& control, int x, int y);
bool HandleLegacyCustomScrollControlMouseMessage(LegacyCustomScrollControl& control,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
