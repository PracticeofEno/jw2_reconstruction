#include "ranker_custom_scroll.h"

#ifdef _WIN32

namespace ranker {
namespace {

constexpr DWORD kScrollWindowExStyle = WS_EX_CONTROLPARENT;
constexpr DWORD kVisibleScrollStyle = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
constexpr DWORD kHiddenScrollStyle = WS_CHILD | BS_OWNERDRAW;
constexpr UINT kScrollRedrawFlags = RDW_INVALIDATE | RDW_NOERASE;

HINSTANCE parent_instance(HWND parent) {
    return reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(parent, GWLP_HINSTANCE));
}

WNDPROC original_proc(HWND window) {
    if (window == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<WNDPROC>(GetWindowLongPtrA(window, GWLP_WNDPROC));
}

void release_bitmaps(LegacyCustomScrollControl& control) {
    ReleaseBitmapMemoryResource(control.start_bitmap);
    ReleaseBitmapMemoryResource(control.start_pressed_bitmap);
    ReleaseBitmapMemoryResource(control.end_bitmap);
    ReleaseBitmapMemoryResource(control.end_pressed_bitmap);
    ReleaseBitmapMemoryResource(control.thumb_bitmap);
    ReleaseBitmapMemoryResource(control.track_bitmap);
}

void load_optional_bitmap(BitmapMemoryResource& resource, u32 record_index) {
    if (record_index == 0) {
        return;
    }
    LoadBitmapMemoryResourceFromTrcRecord(resource, kLegacyCustomScrollBitmapArchive,
        record_index);
}

void recompute_track_length(LegacyCustomScrollControl& control) {
    if (control.horizontal) {
        control.track_length = control.width - control.horizontal_margin * 2;
    } else {
        control.track_length = control.height - control.vertical_margin * 2;
    }
}

int original_scroll_range(const LegacyCustomScrollControl& control) {
    const int range = control.max_value - control.min_value;
    return range == 0 ? 1 : range;
}

int thumb_travel(const LegacyCustomScrollControl& control) {
    const int thumb_size = control.horizontal ? control.thumb_width :
        control.thumb_height;
    return control.track_length - thumb_size;
}

void update_thumb_position(LegacyCustomScrollControl& control) {
    const int travel = thumb_travel(control);
    const int range = original_scroll_range(control);
    const int margin = control.horizontal ? control.horizontal_margin :
        control.vertical_margin;
    control.thumb_position = (travel * control.value) / range + margin;
}

void draw_bitmap_at(const BitmapMemoryResource& bitmap, HDC dc, int x, int y) {
    StretchBitmapMemoryResourceToDc(bitmap, dc, x, y);
}

void redraw_scroll_window(const LegacyCustomScrollControl& control) {
    RedrawWindow(control.window, nullptr, nullptr, kScrollRedrawFlags);
}

void draw_thumb_track(const LegacyCustomScrollControl& control, HDC dc) {
    const int bitmap_width = GetBitmapMemoryResourceWidth(control.track_bitmap);
    const int bitmap_height = GetBitmapMemoryResourceHeight(control.track_bitmap);

    HDC memory_dc = CreateCompatibleDC(dc);
    HBITMAP memory_bitmap = CreateCompatibleBitmap(dc, bitmap_width, bitmap_height);
    SelectObject(memory_dc, memory_bitmap);
    draw_bitmap_at(control.track_bitmap, memory_dc, 0, 0);
    if (control.horizontal) {
        draw_bitmap_at(control.thumb_bitmap, memory_dc,
            control.thumb_position - control.horizontal_margin, 0);
        BitBlt(dc, control.horizontal_margin, 0, bitmap_width, bitmap_height,
            memory_dc, 0, 0, SRCCOPY);
    } else {
        draw_bitmap_at(control.thumb_bitmap, memory_dc, 0,
            control.thumb_position - control.vertical_margin);
        BitBlt(dc, 0, control.vertical_margin, bitmap_width, bitmap_height,
            memory_dc, 0, 0, SRCCOPY);
    }
    DeleteDC(memory_dc);
    DeleteObject(memory_bitmap);
}

int lparam_x(LPARAM lparam) {
    return static_cast<int>(static_cast<short>(LOWORD(lparam)));
}

int lparam_y(LPARAM lparam) {
    return static_cast<int>(static_cast<short>(HIWORD(lparam)));
}

void assign_drag_value(LegacyCustomScrollControl& control, int x, int y) {
    const int travel = thumb_travel(control);
    int pixel = control.horizontal ? x - control.horizontal_margin :
        y - control.vertical_margin;
    if (pixel < 0) {
        pixel = 0;
    }
    if (travel < pixel) {
        pixel = travel;
    }
    control.value = (pixel * original_scroll_range(control)) / travel;
    redraw_scroll_window(control);
}

} // namespace

LegacyCustomScrollControl& InitializeLegacyCustomScrollControl(
    LegacyCustomScrollControl& control) {
    InitializeBitmapMemoryResource(control.start_bitmap);
    InitializeBitmapMemoryResource(control.start_pressed_bitmap);
    InitializeBitmapMemoryResource(control.end_bitmap);
    InitializeBitmapMemoryResource(control.end_pressed_bitmap);
    InitializeBitmapMemoryResource(control.thumb_bitmap);
    InitializeBitmapMemoryResource(control.track_bitmap);
    release_bitmaps(control);

    control.original_window_proc = nullptr;
    control.visible = false;
    control.dragging = false;
    control.horizontal_margin = 0x0e;
    control.vertical_margin = 0x0e;
    control.thumb_width = 0x0e;
    control.thumb_height = 0x0e;
    control.window = nullptr;
    return control;
}

bool ConstructLegacyCustomScrollControl(LegacyCustomScrollControl& control,
    HWND parent, const char* text, HMENU id_or_menu, bool horizontal, int x, int y,
    int width, int height) {
    InitializeLegacyCustomScrollControl(control);
    return CreateLegacyCustomScrollControlWindow(control, parent, text, id_or_menu,
        horizontal, x, y, width, height);
}

bool CreateLegacyCustomScrollControlWindow(LegacyCustomScrollControl& control,
    HWND parent, const char* text, HMENU id_or_menu, bool horizontal, int x, int y,
    int width, int height) {
    control.window = CreateWindowExA(kScrollWindowExStyle, "button",
        text, kVisibleScrollStyle, x, y, width, height, parent, id_or_menu,
        parent_instance(parent), nullptr);
    control.parent = parent;
    control.original_window_proc = original_proc(control.window);
    control.horizontal = horizontal;
    control.x = x;
    control.y = y;
    control.width = width;
    control.height = height;
    recompute_track_length(control);
    control.value = 0;
    control.page_step = 10;
    control.min_value = 0;
    control.max_value = 100;
    control.visible = false;
    control.dragging = false;
    release_bitmaps(control);
    return control.window != nullptr;
}

void DestroyLegacyCustomScrollControl(LegacyCustomScrollControl& control) {
    release_bitmaps(control);
    if (control.window != nullptr) {
        DestroyWindow(control.window);
        control.window = nullptr;
    }
    HandleBitmapMemoryResourceDestructor(control.track_bitmap);
    HandleBitmapMemoryResourceDestructor(control.thumb_bitmap);
    HandleBitmapMemoryResourceDestructor(control.end_pressed_bitmap);
    HandleBitmapMemoryResourceDestructor(control.end_bitmap);
    HandleBitmapMemoryResourceDestructor(control.start_pressed_bitmap);
    HandleBitmapMemoryResourceDestructor(control.start_bitmap);
}

HWND GetLegacyCustomScrollControlWindow(const LegacyCustomScrollControl& control) {
    return control.window;
}

void LoadLegacyCustomScrollControlBitmaps(LegacyCustomScrollControl& control,
    u32 start_record, u32 start_pressed_record, u32 end_record,
    u32 end_pressed_record, u32 thumb_record, u32 track_record) {
    load_optional_bitmap(control.start_bitmap, start_record);
    load_optional_bitmap(control.start_pressed_bitmap,
        start_pressed_record);
    load_optional_bitmap(control.end_bitmap, end_record);
    load_optional_bitmap(control.end_pressed_bitmap,
        end_pressed_record);
    load_optional_bitmap(control.thumb_bitmap, thumb_record);
    load_optional_bitmap(control.track_bitmap, track_record);
}

void SetLegacyCustomScrollControlMetrics(LegacyCustomScrollControl& control,
    int horizontal_margin, int vertical_margin, int thumb_width, int thumb_height) {
    control.horizontal_margin = horizontal_margin;
    control.vertical_margin = vertical_margin;
    control.thumb_width = thumb_width;
    control.thumb_height = thumb_height;
    recompute_track_length(control);
}

void DrawLegacyCustomScrollControl(LegacyCustomScrollControl& control, HDC dc) {
    if (!control.visible) {
        return;
    }

    update_thumb_position(control);
    if (control.horizontal) {
        draw_bitmap_at(control.start_bitmap, dc, 0, 0);
        draw_bitmap_at(control.end_bitmap, dc,
            control.width - control.horizontal_margin, 0);
    } else {
        draw_bitmap_at(control.start_bitmap, dc, 0, 0);
        draw_bitmap_at(control.end_bitmap, dc, 0,
            control.height - control.vertical_margin);
    }
    draw_thumb_track(control, dc);
}

void SetLegacyCustomScrollControlVisible(LegacyCustomScrollControl& control,
    bool visible) {
    if (control.visible == visible) {
        return;
    }
    control.visible = visible;

    if (!visible) {
        HDC dc = GetDC(control.window);
        BitBlt(dc, 0, 0, control.width, control.height, nullptr, 0, 0,
            BLACKNESS);
        SetWindowLongPtrA(control.window, GWL_STYLE, kHiddenScrollStyle);
        ReleaseDC(control.window, dc);
        return;
    }

    SetWindowLongPtrA(control.window, GWL_STYLE, kVisibleScrollStyle);
    redraw_scroll_window(control);
}

int GetLegacyCustomScrollControlValue(const LegacyCustomScrollControl& control) {
    return control.value;
}

void SetLegacyCustomScrollControlValue(LegacyCustomScrollControl& control,
    int value, bool redraw) {
    if (value < control.min_value || value > control.max_value) {
        return;
    }
    control.value = value;
    if (redraw) {
        redraw_scroll_window(control);
    }
}

void GetLegacyCustomScrollControlRange(const LegacyCustomScrollControl& control,
    int* min_value, int* max_value) {
    *min_value = control.min_value;
    *max_value = control.max_value;
}

void SetLegacyCustomScrollControlRange(LegacyCustomScrollControl& control,
    int min_value, int max_value, bool redraw) {
    control.min_value = min_value;
    control.max_value = max_value;
    if (control.value < control.min_value) {
        control.value = control.min_value;
    }
    if (control.value > control.max_value) {
        control.value = control.max_value;
    }
    if (redraw) {
        redraw_scroll_window(control);
    }
}

void SetLegacyCustomScrollControlPageStep(LegacyCustomScrollControl& control,
    int page_step) {
    control.page_step = page_step;
}

LegacyCustomScrollHit HitTestLegacyCustomScrollControl(
    LegacyCustomScrollControl& control, int x, int y) {
    if (!control.horizontal) {
        if (y < control.vertical_margin) {
            return LegacyCustomScrollHit::BackArrow;
        }
        if (y >= control.height - control.vertical_margin) {
            return LegacyCustomScrollHit::ForwardArrow;
        }
        if (y < control.thumb_position) {
            return LegacyCustomScrollHit::PageBack;
        }
        if (y < control.thumb_position + control.thumb_height) {
            return LegacyCustomScrollHit::Thumb;
        }
        return LegacyCustomScrollHit::PageForward;
    }

    if (x < control.horizontal_margin) {
        return LegacyCustomScrollHit::BackArrow;
    }
    if (x >= control.width - control.horizontal_margin) {
        return LegacyCustomScrollHit::ForwardArrow;
    }
    if (x < control.thumb_position) {
        return LegacyCustomScrollHit::PageBack;
    }
    if (x < control.thumb_position + control.thumb_width) {
        return LegacyCustomScrollHit::Thumb;
    }
    return LegacyCustomScrollHit::PageForward;
}

bool HandleLegacyCustomScrollControlMouseMessage(LegacyCustomScrollControl& control,
    UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_MOUSEMOVE:
        if ((wparam & MK_LBUTTON) == 0) {
            control.dragging = false;
        }
        if (control.dragging) {
            assign_drag_value(control, lparam_x(lparam), lparam_y(lparam));
            return true;
        }
        return false;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const LegacyCustomScrollHit hit = HitTestLegacyCustomScrollControl(control,
            lparam_x(lparam), lparam_y(lparam));
        switch (hit) {
        case LegacyCustomScrollHit::BackArrow:
            if (control.min_value < control.value) {
                --control.value;
            }
            redraw_scroll_window(control);
            return true;
        case LegacyCustomScrollHit::ForwardArrow:
            if (control.value < control.max_value) {
                ++control.value;
            }
            redraw_scroll_window(control);
            return true;
        case LegacyCustomScrollHit::PageBack:
            control.value -= control.page_step;
            if (control.value < control.min_value) {
                control.value = control.min_value;
            }
            redraw_scroll_window(control);
            return true;
        case LegacyCustomScrollHit::PageForward:
            control.value += control.page_step;
            if (control.value > control.max_value) {
                control.value = control.max_value;
            }
            redraw_scroll_window(control);
            return true;
        case LegacyCustomScrollHit::Thumb:
            control.dragging = true;
            return true;
        default:
            return false;
        }
    }
    case WM_LBUTTONUP:
        control.dragging = false;
        return false;
    default:
        return false;
    }
}

} // namespace ranker

#endif
