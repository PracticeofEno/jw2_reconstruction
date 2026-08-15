#include "ranker_image_controls.h"

#ifdef _WIN32

#include "ranker_cursor.h"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cstdlib>

namespace ranker {
namespace {

constexpr UINT_PTR kFrontendComboListCursorSubclassId = 0x52434c43u;

LRESULT CALLBACK frontend_combo_list_cursor_subclass_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass_id, DWORD_PTR cursor_value) {
    if (message == WM_SETCURSOR) {
        SetCursor(reinterpret_cast<HCURSOR>(cursor_value));
        return TRUE;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, frontend_combo_list_cursor_subclass_proc,
            subclass_id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

HINSTANCE parent_instance(HWND parent) {
    return reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(parent, GWLP_HINSTANCE));
}

WNDPROC original_proc(HWND window) {
    if (window == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<WNDPROC>(GetWindowLongPtrA(window, GWLP_WNDPROC));
}

void release_control_bitmaps(BitmapMemoryResource& normal,
    BitmapMemoryResource& pressed) {
    ReleaseBitmapMemoryResource(normal);
    ReleaseBitmapMemoryResource(pressed);
}

void initialize_control_bitmaps(BitmapMemoryResource& normal,
    BitmapMemoryResource& pressed) {
    InitializeBitmapMemoryResource(normal);
    InitializeBitmapMemoryResource(pressed);
}

bool load_optional_bitmap(BitmapMemoryResource& resource, u32 record_index) {
    if (record_index == 0) {
        return true;
    }
    return LoadBitmapMemoryResourceFromTrcRecord(resource,
        kLegacyImageControlBitmapArchive, record_index);
}

void draw_bitmap(const BitmapMemoryResource& bitmap, HDC dc, const RECT& rect) {
    const BitmapDrawRect destination{
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    const BitmapDrawRect source{
        bitmap.source_x, bitmap.source_y, bitmap.width, bitmap.height};
    StretchBitmapMemoryResourceRectToDc(bitmap, dc, destination, source);
}

int combo_bitmap_scaled_height(const LegacyImageComboBoxControl& control) {
    const int bitmap_width = std::abs(control.normal_bitmap.width);
    const int bitmap_height = std::abs(control.normal_bitmap.height);
    if (bitmap_width == 0 || bitmap_height == 0 || control.width <= 0) {
        return 0;
    }
    return std::max(1, MulDiv(bitmap_height, control.width, bitmap_width));
}

void fit_combo_selection_field_to_bitmap(LegacyImageComboBoxControl& control) {
    if (control.window == nullptr) {
        return;
    }
    const int desired_outer_height = combo_bitmap_scaled_height(control);
    if (desired_outer_height <= 0) {
        return;
    }

    // CBS_DROPDOWNLIST keeps the closed selection field at the system-font
    // height even when its x/y/width came from a scaled TRC layout.  First set
    // a provisional height, then compensate for USER32's borders so the final
    // closed window has the same aspect ratio as its image resource.
    int selection_height = desired_outer_height;
    SendMessageA(control.window, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
        selection_height);
    RECT window_rect{};
    if (GetWindowRect(control.window, &window_rect)) {
        const int actual_outer_height = window_rect.bottom - window_rect.top;
        const int corrected_height = selection_height +
            (desired_outer_height - actual_outer_height);
        if (corrected_height > 0 &&
            actual_outer_height < desired_outer_height * 3) {
            selection_height = corrected_height;
            SendMessageA(control.window, CB_SETITEMHEIGHT,
                static_cast<WPARAM>(-1), selection_height);
        }
    }
    SendMessageA(control.window, CB_SETITEMHEIGHT, 0,
        std::max(1, selection_height));
}

} // namespace

LegacyImageButtonControl& InitializeLegacyImageButtonControl(
    LegacyImageButtonControl& control) {
    initialize_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    control.original_window_proc = nullptr;
    control.window = nullptr;
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    return control;
}

bool ConstructLegacyImageButtonControl(LegacyImageButtonControl& control, HWND parent,
    const char* text, HMENU id_or_menu, int x, int y, int width, int height) {
    initialize_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    return CreateLegacyImageButtonWindow(control, parent, text, id_or_menu, x, y,
        width, height);
}

bool CreateLegacyImageButtonWindow(LegacyImageButtonControl& control, HWND parent,
    const char* text, HMENU id_or_menu, int x, int y, int width, int height) {
    control.x = x;
    control.y = y;
    control.width = width;
    control.height = height;
    control.parent = parent;

    control.window = CreateWindowExA(0, "button", text,
        kLegacyImageButtonWindowStyle, x, y, width, height, parent, id_or_menu,
        parent_instance(parent), nullptr);
    control.original_window_proc = original_proc(control.window);
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    return control.window != nullptr;
}

void ReleaseLegacyImageButtonControlWindow(LegacyImageButtonControl& control) {
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    if (control.window != nullptr) {
        DestroyWindow(control.window);
        control.window = nullptr;
    }
}

void DestroyLegacyImageButtonControl(LegacyImageButtonControl& control) {
    ReleaseLegacyImageButtonControlWindow(control);
    HandleBitmapMemoryResourceDestructor(control.pressed_bitmap);
    HandleBitmapMemoryResourceDestructor(control.normal_bitmap);
}

HWND GetLegacyImageButtonWindow(const LegacyImageButtonControl& control) {
    return control.window;
}

void LoadLegacyImageButtonBitmaps(LegacyImageButtonControl& control,
    u32 normal_record, u32 pressed_record) {
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    load_optional_bitmap(control.normal_bitmap, normal_record);
    load_optional_bitmap(control.pressed_bitmap, pressed_record);
}

void DrawLegacyImageButtonItem(LegacyImageButtonControl& control,
    const DRAWITEMSTRUCT& item) {
    const bool use_pressed_bitmap =
        item.itemAction == ODA_SELECT && item.itemState != ODS_FOCUS;
    draw_bitmap(use_pressed_bitmap ? control.pressed_bitmap : control.normal_bitmap,
        item.hDC, item.rcItem);
}

LegacyImageComboBoxControl& InitializeLegacyImageComboBoxControl(
    LegacyImageComboBoxControl& control) {
    initialize_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    control.original_window_proc = nullptr;
    control.window = nullptr;
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    control.outer_border_color = kLegacyImageComboOuterBorderColor;
    control.inner_border_color = kLegacyImageComboInnerBorderColor;
    return control;
}

bool ConstructLegacyImageComboBoxControl(LegacyImageComboBoxControl& control,
    HWND parent, const char* text, HMENU id_or_menu, DWORD style, int x, int y,
    int width, int height) {
    initialize_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    return CreateLegacyImageComboBoxWindow(control, parent, text, id_or_menu, style,
        x, y, width, height);
}

bool CreateLegacyImageComboBoxWindow(LegacyImageComboBoxControl& control, HWND parent,
    const char* text, HMENU id_or_menu, DWORD style, int x, int y, int width,
    int height) {
    control.x = x;
    control.y = y;
    control.width = width;
    control.height = height;
    control.parent = parent;
    control.style = style;

    control.window = CreateWindowExA(0, "combobox", text,
        style, x, y, width, height, parent, id_or_menu, parent_instance(parent),
        nullptr);
    control.original_window_proc = original_proc(control.window);
    SendMessageA(control.window, CB_RESETCONTENT, 0, 0);
    ApplyFrontendGameCursorToComboBoxPopup(control.window);
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    return control.window != nullptr;
}

bool ApplyFrontendGameCursorToComboBoxPopup(HWND combo_box) {
    if (combo_box == nullptr || !IsWindow(combo_box)) {
        return false;
    }
    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (!GetComboBoxInfo(combo_box, &info) || info.hwndList == nullptr) {
        return false;
    }
    return SetWindowSubclass(info.hwndList,
        frontend_combo_list_cursor_subclass_proc,
        kFrontendComboListCursorSubclassId,
        reinterpret_cast<DWORD_PTR>(GetFrontendGameCursor())) != FALSE;
}

void DestroyLegacyImageComboBoxControl(LegacyImageComboBoxControl& control) {
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    if (control.window != nullptr) {
        DestroyWindow(control.window);
        control.window = nullptr;
    }
    HandleBitmapMemoryResourceDestructor(control.pressed_bitmap);
    HandleBitmapMemoryResourceDestructor(control.normal_bitmap);
}

void LoadLegacyImageComboBoxBitmaps(LegacyImageComboBoxControl& control,
    u32 normal_record, u32 pressed_record) {
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    load_optional_bitmap(control.normal_bitmap, normal_record);
    load_optional_bitmap(control.pressed_bitmap, pressed_record);
    fit_combo_selection_field_to_bitmap(control);
}

void SetLegacyImageComboBoxColors(LegacyImageComboBoxControl& control,
    COLORREF outer_border, COLORREF inner_border) {
    control.outer_border_color = outer_border;
    control.inner_border_color = inner_border;
}

void SetLegacyImageComboBoxFont(LegacyImageComboBoxControl& control, HFONT font,
    bool redraw) {
    if (control.window == nullptr) {
        return;
    }
    SendMessageA(control.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(font), redraw ? TRUE : FALSE);
    fit_combo_selection_field_to_bitmap(control);
}

void DrawLegacyImageComboBoxItem(LegacyImageComboBoxControl& control,
    const DRAWITEMSTRUCT& item) {
    if (SendMessageA(control.window, CB_GETDROPPEDSTATE, 0, 0) != 0) {
        const LRESULT top_index = SendMessageA(control.window, CB_GETTOPINDEX, 0, 0);
        if (static_cast<LRESULT>(item.itemID) == top_index &&
            (item.itemAction & ODA_DRAWENTIRE) != 0 &&
            item.itemState != ODS_COMBOBOXEDIT) {
            RECT frame{};
            GetClipBox(item.hDC, &frame);
            HBRUSH outer = CreateSolidBrush(control.outer_border_color);
            FrameRect(item.hDC, &frame, outer);
            DeleteObject(outer);

            ++frame.left;
            ++frame.top;
            --frame.right;
            --frame.bottom;
            HBRUSH inner = CreateSolidBrush(control.inner_border_color);
            FrameRect(item.hDC, &frame, inner);
            DeleteObject(inner);
        }
    }

    if (item.itemID == static_cast<UINT>(-1)) {
        return;
    }

    std::array<char, 256> text;
    text.fill(static_cast<char>(0xcc));
    if (SendMessageA(control.window, CB_GETLBTEXT, item.itemID,
            reinterpret_cast<LPARAM>(text.data())) == CB_ERR) {
        return;
    }

    RECT text_rect = item.rcItem;
    const int bitmap_width = std::abs(control.normal_bitmap.width);
    const int horizontal_padding = bitmap_width > 0 ?
        std::max(2, MulDiv(2, control.width, bitmap_width)) : 2;
    text_rect.left += horizontal_padding;
    text_rect.right -= horizontal_padding;
    SetTextColor(item.hDC, RGB(255, 255, 255));
    // Modern USER32 can report ODS_SELECTED for the closed selection field
    // while it owns focus. The original lobby only shows the highlight in the
    // expanded list; its closed ODS_COMBOBOXEDIT field remains black.
    const bool selected_list_item =
        (item.itemState & ODS_SELECTED) != 0 &&
        (item.itemState & ODS_COMBOBOXEDIT) == 0;
    const COLORREF background_color = selected_list_item ?
        GetSysColor(COLOR_HIGHLIGHT) : RGB(0, 0, 0);

    // On the reconstructed 64-bit presentation path, DrawTextA with an OPAQUE
    // background maps nominal black to RGB(2,2,2).  The original 32-bit game
    // writes RGB(0,0,0) across this complete owner-draw rectangle.  Fill it
    // explicitly and render the glyphs transparently so the selected field and
    // drop-down rows retain the original pixels, including the area after the
    // final character in strings such as "Random".
    HBRUSH background = CreateSolidBrush(background_color);
    FillRect(item.hDC, &text_rect, background);
    DeleteObject(background);
    SetBkMode(item.hDC, TRANSPARENT);
    DrawTextA(item.hDC, text.data(), -1, &text_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void PaintLegacyImageComboBoxBackground(LegacyImageComboBoxControl& control) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(control.window, &paint);
    if (SendMessageA(control.window, CB_GETDROPPEDSTATE, 0, 0) == 0) {
        RECT client{};
        GetClientRect(control.window, &client);
        draw_bitmap(control.normal_bitmap, dc, client);
    }
    control.saved_selection = static_cast<int>(
        SendMessageA(control.window, CB_GETCURSEL, 0, 0));
    SendMessageA(control.window, CB_SETCURSEL,
        static_cast<WPARAM>(control.saved_selection), 0);
    EndPaint(control.window, &paint);
}

}

#endif
