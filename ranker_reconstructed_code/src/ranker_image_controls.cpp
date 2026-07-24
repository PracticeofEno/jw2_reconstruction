#include "ranker_image_controls.h"

#ifdef _WIN32

#include <array>

namespace ranker {
namespace {

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

void draw_bitmap(const BitmapMemoryResource& bitmap, HDC dc) {
    StretchBitmapMemoryResourceToDc(bitmap, dc, 0, 0);
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
        item.hDC);
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
    release_control_bitmaps(control.normal_bitmap, control.pressed_bitmap);
    return control.window != nullptr;
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
}

void SetLegacyImageComboBoxColors(LegacyImageComboBoxControl& control,
    COLORREF outer_border, COLORREF inner_border) {
    control.outer_border_color = outer_border;
    control.inner_border_color = inner_border;
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

    DRAWITEMSTRUCT& mutable_item = const_cast<DRAWITEMSTRUCT&>(item);
    mutable_item.rcItem.left += 2;
    mutable_item.rcItem.top += 2;
    mutable_item.rcItem.right -= 2;
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
    FillRect(item.hDC, &mutable_item.rcItem, background);
    DeleteObject(background);
    SetBkMode(item.hDC, TRANSPARENT);
    DrawTextA(item.hDC, text.data(), -1, &mutable_item.rcItem, DT_NOPREFIX);
}

void PaintLegacyImageComboBoxBackground(LegacyImageComboBoxControl& control) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(control.window, &paint);
    if (SendMessageA(control.window, CB_GETDROPPEDSTATE, 0, 0) == 0) {
        draw_bitmap(control.normal_bitmap, dc);
    }
    control.saved_selection = static_cast<int>(
        SendMessageA(control.window, CB_GETCURSEL, 0, 0));
    SendMessageA(control.window, CB_SETCURSEL,
        static_cast<WPARAM>(control.saved_selection), 0);
    EndPaint(control.window, &paint);
}

}

#endif
