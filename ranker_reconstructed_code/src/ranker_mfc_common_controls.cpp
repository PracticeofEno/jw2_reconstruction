#include "ranker_mfc_runtime.h"

#ifdef _WIN32

#include <array>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ranker {
namespace {

constexpr UINT kMfcStatusGetText = 0x0402;
constexpr UINT kMfcStatusGetTextLength = 0x0403;
constexpr UINT kMfcStatusGetBorders = 0x0407;
constexpr UINT kMfcStatusGetTipText = 0x0412;
constexpr UINT kMfcToolbarAddBitmap = 0x0413;
constexpr UINT kMfcToolbarReplaceBitmap = 0x041c;
constexpr UINT kMfcToolbarButtonStructSize = 0x041e;
constexpr UINT kMfcListGetItemPositionCompat = 0x1035;
constexpr UINT kMfcListGetSubItemRectCompat = 0x1038;

struct ImageListHandleMap {
    std::unordered_map<HIMAGELIST, MfcImageListCompat*> permanent;
    std::unordered_map<HIMAGELIST, MfcImageListCompat> temporary;
    std::vector<std::unique_ptr<MfcImageListCompat>> returned_wrappers;
};

ImageListHandleMap* g_image_list_map = nullptr;
std::unordered_set<MfcCheckDataCompat*> g_check_data_allocations;

bool ensure_common_controls(DWORD flags) {
    INITCOMMONCONTROLSEX init{};
    init.dwSize = sizeof(init);
    init.dwICC = flags;
    return InitCommonControlsEx(&init) != FALSE;
}

bool has_window(const MfcControlCompat& control) {
    return control.window != nullptr && IsWindow(control.window) != FALSE;
}

HWND create_common_control(MfcControlCompat& control, DWORD icc,
    const char* class_name, DWORD style, const RECT& bounds, HWND parent,
    UINT id) {
    ensure_common_controls(icc);
    control.style = style;
    control.window = CreateWindowExA(0, class_name, nullptr, style | WS_CHILD,
        bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, parent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        GetModuleHandleA(nullptr), nullptr);
    return control.window;
}

HWND create_standard_control(MfcControlCompat& control, const char* class_name,
    const char* caption, DWORD style, const RECT& bounds, HWND parent,
    UINT id) {
    control.style = style;
    control.window = CreateWindowExA(0, class_name,
        caption == nullptr ? "" : caption, style | WS_CHILD,
        bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, parent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        GetModuleHandleA(nullptr), nullptr);
    return control.window;
}

void destroy_control(MfcControlCompat& control) {
    if (has_window(control)) {
        DestroyWindow(control.window);
    }
    control.window = nullptr;
}

void detach_control_handle(MfcControlCompat& control) {
    control.window = nullptr;
}

MfcImageListCompat attach_returned_image_list_handle(HIMAGELIST handle) {
    if (handle == nullptr) {
        return MfcImageListCompat{};
    }
    auto* map = static_cast<ImageListHandleMap*>(
        EnsureTempImageListHandleMap(true));
    if (map == nullptr) {
        return MfcImageListCompat{};
    }
    auto wrapper = std::make_unique<MfcImageListCompat>();
    if (!AttachImageListHandle(*wrapper, handle)) {
        return MfcImageListCompat{};
    }
    MfcImageListCompat result = *wrapper;
    map->returned_wrappers.push_back(std::move(wrapper));
    return result;
}

template <typename Control, typename DestroyFn>
Control* destroy_scalar_dtor(Control* control, unsigned flags,
    DestroyFn destroy_fn) {
    if (control != nullptr) {
        destroy_fn(*control);
        if ((flags & 1) != 0) {
            delete control;
        }
    }
    return control;
}

DWORD control_style(const MfcControlCompat& control) {
    if (has_window(control)) {
        return static_cast<DWORD>(GetWindowLongPtrA(control.window, GWL_STYLE));
    }
    return control.style;
}

LRESULT send_control_message(const MfcControlCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (!has_window(control)) {
        return 0;
    }
    return SendMessageA(control.window, message, wparam, lparam);
}

LPARAM make_lparam(int low, int high) {
    return static_cast<LPARAM>(
        MAKELONG(static_cast<WORD>(low), static_cast<WORD>(high)));
}

std::string read_window_text_from_buffer(const char* buffer) {
    return buffer == nullptr ? std::string{} : std::string(buffer);
}

void trace_default_control_assert(const char* function_name, int line) {
    AfxTraceOutput("winctrl1.cpp(%d): %s default handler reached.\n",
        line, function_name);
}

MfcCheckDataCompat* allocate_check_data(LPARAM item_data = 0) {
    auto* data = new MfcCheckDataCompat();
    ConstructAfxCheckData(*data);
    data->item_data = item_data;
    g_check_data_allocations.insert(data);
    return data;
}

bool is_check_data(LPARAM value) {
    auto* data = reinterpret_cast<MfcCheckDataCompat*>(value);
    return data != nullptr && g_check_data_allocations.count(data) != 0;
}

void release_check_data(LPARAM value) {
    auto* data = reinterpret_cast<MfcCheckDataCompat*>(value);
    auto found = g_check_data_allocations.find(data);
    if (found == g_check_data_allocations.end()) {
        return;
    }
    g_check_data_allocations.erase(found);
    delete data;
}

MfcCheckDataCompat* check_data_for_item(MfcCheckListBoxCompat& control,
    int item, bool create) {
    if (!has_window(control) || item < 0) {
        return nullptr;
    }
    const LRESULT current = SendMessageA(control.window, LB_GETITEMDATA,
        static_cast<WPARAM>(item), 0);
    if (current == LB_ERR) {
        return nullptr;
    }
    if (is_check_data(current)) {
        return reinterpret_cast<MfcCheckDataCompat*>(current);
    }
    if (!create) {
        return nullptr;
    }
    MfcCheckDataCompat* data = allocate_check_data(current);
    if (SendMessageA(control.window, LB_SETITEMDATA,
            static_cast<WPARAM>(item), reinterpret_cast<LPARAM>(data)) ==
        LB_ERR) {
        release_check_data(reinterpret_cast<LPARAM>(data));
        return nullptr;
    }
    return data;
}

void release_all_check_data(MfcCheckListBoxCompat& control) {
    if (!has_window(control)) {
        return;
    }
    const LRESULT count = SendMessageA(control.window, LB_GETCOUNT, 0, 0);
    if (count == LB_ERR) {
        return;
    }
    for (int item = 0; item < static_cast<int>(count); ++item) {
        const LRESULT data = SendMessageA(control.window, LB_GETITEMDATA,
            static_cast<WPARAM>(item), 0);
        release_check_data(data);
    }
}

void notify_check_list_box_changed(MfcCheckListBoxCompat& control) {
    if (!has_window(control)) {
        return;
    }
    HWND parent = GetParent(control.window);
    if (parent == nullptr) {
        return;
    }
    constexpr WORD kCheckChanged = 0x28;
    SendMessageA(parent, WM_COMMAND,
        MAKEWPARAM(static_cast<WORD>(GetDlgCtrlID(control.window)),
            kCheckChanged),
        reinterpret_cast<LPARAM>(control.window));
}

int next_check_value(const MfcCheckListBoxCompat& control, int current) {
    const int states = control.check_style == 6 ? 3 : 2;
    if (current == states) {
        --current;
    }
    return (current + 1) % states;
}

} // namespace

MfcImageListCompat RemoveImageList(MfcListCtrlCompat& control,
    int image_list_type);
MfcImageListCompat RemoveImageList(MfcTreeCtrlCompat& control,
    int image_list_type);

void RemoveListCtrlTemporaryImageLists(MfcListCtrlCompat& control) {
    RemoveImageList(control, LVSIL_NORMAL);
    RemoveImageList(control, LVSIL_SMALL);
    RemoveImageList(control, LVSIL_STATE);
}

void RemoveTreeCtrlTemporaryImageLists(MfcTreeCtrlCompat& control) {
    RemoveImageList(control, TVSIL_NORMAL);
    RemoveImageList(control, TVSIL_STATE);
}

bool CreateStaticControl(MfcStaticCompat& control, const char* caption,
    DWORD style, const RECT& bounds, HWND parent, UINT id) {
    return create_standard_control(control, "STATIC", caption, style, bounds,
        parent, id) != nullptr;
}

void DestroyStaticControl(MfcStaticCompat& control) {
    destroy_control(control);
}

bool CreateButtonControl(MfcButtonCompat& control, const char* caption,
    DWORD style, const RECT& bounds, HWND parent, UINT id) {
    return create_standard_control(control, "BUTTON", caption, style, bounds,
        parent, id) != nullptr;
}

void DestroyButtonControl(MfcButtonCompat& control) {
    destroy_control(control);
}

int CWndGetCheckedRadioButton(MfcCWndCompat& parent, int first_id, int last_id) {
    if (parent.window == nullptr || !IsWindow(parent.window)) {
        return 0;
    }
    for (int id = first_id; id <= last_id; ++id) {
        if (IsDlgButtonChecked(parent.window, id) != 0) {
            return id;
        }
    }
    return 0;
}

void ButtonDrawItemDefault(DRAWITEMSTRUCT* draw_item) {
    (void)draw_item;
    trace_default_control_assert("CButton::DrawItem", 0x48);
}

bool ButtonOnChildNotify(MfcButtonCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    (void)control;
    (void)wparam;
    (void)result;
    if (message != WM_DRAWITEM) {
        return false;
    }
    ButtonDrawItemDefault(reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
    return true;
}

bool CreateListBoxControl(MfcListBoxCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_standard_control(control, "LISTBOX", nullptr, style, bounds,
        parent, id) != nullptr;
}

void DestroyListBoxControl(MfcListBoxCompat& control) {
    destroy_control(control);
}

void ListBoxDrawItemDefault(DRAWITEMSTRUCT* draw_item) {
    (void)draw_item;
    trace_default_control_assert("CListBox::DrawItem", 0x69);
}

void ListBoxMeasureItemDefault(MEASUREITEMSTRUCT* measure_item) {
    (void)measure_item;
    trace_default_control_assert("CListBox::MeasureItem", 0x6b);
}

int ListBoxCompareItemDefault(COMPAREITEMSTRUCT* compare_item) {
    (void)compare_item;
    trace_default_control_assert("CListBox::CompareItem", 0x6d);
    return 0;
}

void ListBoxDeleteItemDefault(DELETEITEMSTRUCT* delete_item) {
    (void)delete_item;
}

int ListBoxVKeyToItemDefault(MfcListBoxCompat& control, UINT key, UINT index) {
    (void)control;
    (void)key;
    (void)index;
    return -1;
}

int ListBoxCharToItemDefault(MfcListBoxCompat& control, UINT ch, UINT index) {
    (void)control;
    (void)ch;
    (void)index;
    return -1;
}

bool ListBoxOnChildNotify(MfcListBoxCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    switch (message) {
    case WM_DRAWITEM:
        ListBoxDrawItemDefault(reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
        return true;
    case WM_MEASUREITEM:
        ListBoxMeasureItemDefault(
            reinterpret_cast<MEASUREITEMSTRUCT*>(lparam));
        return true;
    case WM_DELETEITEM:
        ListBoxDeleteItemDefault(reinterpret_cast<DELETEITEMSTRUCT*>(lparam));
        return true;
    case WM_COMPAREITEM:
        if (result == nullptr) {
            trace_default_control_assert("CListBox::OnChildNotify", 0x83);
            return true;
        }
        *result = ListBoxCompareItemDefault(
            reinterpret_cast<COMPAREITEMSTRUCT*>(lparam));
        return true;
    case WM_VKEYTOITEM:
        if (result == nullptr) {
            trace_default_control_assert("CListBox::OnChildNotify", 0x87);
            return true;
        }
        *result = ListBoxVKeyToItemDefault(control, LOWORD(wparam),
            HIWORD(wparam));
        return true;
    case WM_CHARTOITEM:
        if (result == nullptr) {
            trace_default_control_assert("CListBox::OnChildNotify", 0x87);
            return true;
        }
        *result = ListBoxCharToItemDefault(control, LOWORD(wparam),
            HIWORD(wparam));
        return true;
    default:
        return false;
    }
}

std::string ListBoxGetText(MfcListBoxCompat& control, int index) {
    if (!has_window(control)) {
        return {};
    }
    const LRESULT length = SendMessageA(control.window, LB_GETTEXTLEN,
        static_cast<WPARAM>(index), 0);
    if (length == LB_ERR) {
        return {};
    }
    std::vector<char> text(static_cast<std::size_t>(length) + 1U);
    SendMessageA(control.window, LB_GETTEXT, static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(text.data()));
    return read_window_text_from_buffer(text.data());
}

void ListBoxGetTextCString(MfcListBoxCompat& control, int index,
    MfcCStringCompat& text) {
    const std::string value = ListBoxGetText(control, index);
    AssignCStringAnsi(text, value.c_str());
}

int ListBoxItemFromPoint(MfcListBoxCompat& control, POINT point, bool& outside) {
    outside = true;
    if (!has_window(control)) {
        return -1;
    }
    const LRESULT value = SendMessageA(control.window, LB_ITEMFROMPOINT, 0,
        MAKELPARAM(static_cast<WORD>(point.x), static_cast<WORD>(point.y)));
    outside = HIWORD(value) != 0;
    return static_cast<int>(LOWORD(value));
}

MfcCheckListStateCompat& ConstructAfxCheckListState(
    MfcCheckListStateCompat& state) {
    state = MfcCheckListStateCompat{};
    state.bitmap = LoadBitmapA(GetModuleHandleA(nullptr),
        MAKEINTRESOURCEA(0x7913));
    if (state.bitmap == nullptr) {
        state.bitmap = LoadBitmapA(GetModuleHandleA(nullptr),
            MAKEINTRESOURCEA(0x7914));
    }
    if (state.bitmap != nullptr) {
        BITMAP bitmap{};
        if (GetObjectA(state.bitmap, sizeof(bitmap), &bitmap) != 0) {
            state.check_size.cx = bitmap.bmWidth / 3;
            state.check_size.cy = bitmap.bmHeight;
        }
    }
    if (state.check_size.cx <= 0) {
        state.check_size.cx = GetSystemMetrics(SM_CXMENUCHECK);
    }
    if (state.check_size.cy <= 0) {
        state.check_size.cy = GetSystemMetrics(SM_CYMENUCHECK);
    }
    return state;
}

void DestroyAfxCheckListState(MfcCheckListStateCompat& state) {
    if (state.bitmap != nullptr) {
        DeleteObject(state.bitmap);
        state.bitmap = nullptr;
    }
    state.check_size = SIZE{};
}

MfcCheckListStateCompat* DeleteAfxCheckListStateScalarDtor(
    MfcCheckListStateCompat* state, unsigned flags) {
    if (state != nullptr) {
        DestroyAfxCheckListState(*state);
        if ((flags & 1U) != 0) {
            delete state;
        }
    }
    return state;
}

MfcCheckDataCompat& ConstructAfxCheckData(MfcCheckDataCompat& data) {
    data.check = 0;
    data.enabled = true;
    data.item_data = 0;
    return data;
}

MfcCheckListStateCompat& GetAfxCheckListState() {
    static MfcCheckListStateCompat state = [] {
        MfcCheckListStateCompat value;
        ConstructAfxCheckListState(value);
        return value;
    }();
    return state;
}

const char* GetCheckListBoxRuntimeClassName() {
    return "CCheckListBox";
}

bool CreateCheckListBox(MfcCheckListBoxCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    if ((style & LBS_OWNERDRAWFIXED) == 0) {
        style |= LBS_OWNERDRAWFIXED;
    }
    return CreateListBoxControl(control, style, bounds, parent, id);
}

void DestroyCheckListBox(MfcCheckListBoxCompat& control) {
    release_all_check_data(control);
    DestroyListBoxControl(control);
}

void CheckListBoxSetCheckStyle(MfcCheckListBoxCompat& control, int style) {
    if (style != 0 && style != BS_CHECKBOX && style != BS_AUTOCHECKBOX &&
        style != BS_3STATE && style != BS_AUTO3STATE) {
        AfxTraceOutput("winctrl3.cpp(0x74): invalid CCheckListBox style.\n");
        return;
    }
    control.check_style = style;
}

void CheckListBoxSetCheck(MfcCheckListBoxCompat& control, int item, int check) {
    if (check == 2 &&
        (control.check_style == BS_CHECKBOX ||
            control.check_style == BS_AUTOCHECKBOX)) {
        return;
    }
    MfcCheckDataCompat* data = check_data_for_item(control, item, true);
    if (data == nullptr) {
        return;
    }
    data->check = check;
    CheckListBoxInvalidateCheck(control, item);
}

int CheckListBoxGetCheck(MfcCheckListBoxCompat& control, int item) {
    MfcCheckDataCompat* data = check_data_for_item(control, item, false);
    return data == nullptr ? 0 : data->check;
}

void CheckListBoxEnable(MfcCheckListBoxCompat& control, int item, bool enabled) {
    MfcCheckDataCompat* data = check_data_for_item(control, item, true);
    if (data == nullptr) {
        return;
    }
    data->enabled = enabled;
    CheckListBoxInvalidateItem(control, item);
}

bool CheckListBoxIsEnabled(MfcCheckListBoxCompat& control, int item) {
    MfcCheckDataCompat* data = check_data_for_item(control, item, false);
    return data == nullptr ? true : data->enabled;
}

RECT CheckListBoxOnGetCheckPosition(MfcCheckListBoxCompat& control,
    RECT item_rect, RECT check_rect) {
    (void)control;
    return check_rect.left == check_rect.right ? item_rect : check_rect;
}

void CheckListBoxDrawItem(MfcCheckListBoxCompat& control,
    DRAWITEMSTRUCT& draw_item) {
    if (draw_item.itemID == static_cast<UINT>(-1)) {
        if ((draw_item.itemAction & ODA_FOCUS) != 0) {
            DrawFocusRect(draw_item.hDC, &draw_item.rcItem);
        }
        return;
    }

    const bool selected = (draw_item.itemState & ODS_SELECTED) != 0;
    const bool enabled = CheckListBoxIsEnabled(control,
        static_cast<int>(draw_item.itemID)) && has_window(control) &&
        IsWindowEnabled(control.window);
    const COLORREF background = GetSysColor(selected && enabled
        ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    const COLORREF text_color = GetSysColor(enabled
        ? (selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
        : COLOR_GRAYTEXT);
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(draw_item.hDC, &draw_item.rcItem, brush);
    DeleteObject(brush);

    MfcCheckListStateCompat& state = GetAfxCheckListState();
    RECT check_rect = draw_item.rcItem;
    check_rect.left += 1;
    check_rect.top += std::max<LONG>(
        0, ((check_rect.bottom - check_rect.top) - state.check_size.cy) / 2);
    check_rect.right = check_rect.left + state.check_size.cx;
    check_rect.bottom = check_rect.top + state.check_size.cy;
    check_rect = CheckListBoxOnGetCheckPosition(control, draw_item.rcItem,
        check_rect);

    UINT frame_state = DFCS_BUTTONCHECK;
    const int check = CheckListBoxGetCheck(control,
        static_cast<int>(draw_item.itemID));
    if (check == 1) {
        frame_state |= DFCS_CHECKED;
    } else if (check == 2) {
        frame_state |= DFCS_BUTTON3STATE | DFCS_CHECKED;
    }
    if (!enabled) {
        frame_state |= DFCS_INACTIVE;
    }
    DrawFrameControl(draw_item.hDC, &check_rect, DFC_BUTTON, frame_state);

    RECT text_rect = draw_item.rcItem;
    text_rect.left += state.check_size.cx + 3;
    CheckListBoxDrawItemText(control, draw_item, text_rect, text_color,
        background);

    if ((draw_item.itemState & ODS_FOCUS) != 0) {
        DrawFocusRect(draw_item.hDC, &draw_item.rcItem);
    }
}

void CheckListBoxDrawItemText(MfcCheckListBoxCompat& control,
    DRAWITEMSTRUCT& draw_item, const RECT& text_rect, COLORREF text_color,
    COLORREF background) {
    std::string text = ListBoxGetText(control,
        static_cast<int>(draw_item.itemID));
    const COLORREF old_text = SetTextColor(draw_item.hDC, text_color);
    const COLORREF old_bk = SetBkColor(draw_item.hDC, background);
    RECT draw_rect = text_rect;
    DrawTextA(draw_item.hDC, text.c_str(), -1, &draw_rect,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(draw_item.hDC, old_text);
    SetBkColor(draw_item.hDC, old_bk);
}

void CheckListBoxMeasureItem(MfcCheckListBoxCompat& control,
    MEASUREITEMSTRUCT& measure_item) {
    const UINT minimum = static_cast<UINT>(
        CheckListBoxCalcMinimumItemHeight(control));
    if (measure_item.itemHeight < minimum) {
        measure_item.itemHeight = minimum;
    }
    control.item_height = static_cast<int>(measure_item.itemHeight);
}

int CheckListBoxCompareItem(MfcCheckListBoxCompat& control,
    COMPAREITEMSTRUCT& compare_item) {
    (void)control;
    const LPARAM left = is_check_data(compare_item.itemData1)
        ? reinterpret_cast<MfcCheckDataCompat*>(
            compare_item.itemData1)->item_data
        : compare_item.itemData1;
    const LPARAM right = is_check_data(compare_item.itemData2)
        ? reinterpret_cast<MfcCheckDataCompat*>(
            compare_item.itemData2)->item_data
        : compare_item.itemData2;
    if (left == right) {
        return 0;
    }
    return left < right ? -1 : 1;
}

void CheckListBoxDeleteItem(MfcCheckListBoxCompat& control,
    DELETEITEMSTRUCT& delete_item) {
    (void)control;
    release_check_data(delete_item.itemData);
}

bool CheckListBoxOnChildNotify(MfcCheckListBoxCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    switch (message) {
    case WM_DRAWITEM:
        CheckListBoxDrawItem(control,
            *reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
        return true;
    case WM_MEASUREITEM:
        CheckListBoxMeasureItem(control,
            *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam));
        return true;
    case WM_DELETEITEM:
        CheckListBoxDeleteItem(control,
            *reinterpret_cast<DELETEITEMSTRUCT*>(lparam));
        return true;
    case WM_COMPAREITEM:
        if (result == nullptr) {
            return true;
        }
        *result = CheckListBoxCompareItem(control,
            *reinterpret_cast<COMPAREITEMSTRUCT*>(lparam));
        return true;
    default:
        return ListBoxOnChildNotify(control, message, wparam, lparam, result);
    }
}

void CheckListBoxPreSubclassWindow(MfcCheckListBoxCompat& control) {
    if ((control_style(control) &
            (LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)) == 0) {
        AfxTraceOutput("winctrl3.cpp(0x1ac): CCheckListBox requires "
            "owner-draw style.\n");
    }
    CheckListBoxSetItemHeight(control, 0,
        static_cast<UINT>(CheckListBoxCalcMinimumItemHeight(control)));
}

int CheckListBoxCalcMinimumItemHeight(MfcCheckListBoxCompat& control) {
    MfcCheckListStateCompat& state = GetAfxCheckListState();
    int minimum = state.check_size.cy + 1;
    if (has_window(control) &&
        (control_style(control) & (LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)) ==
            (LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)) {
        HDC dc = GetDC(control.window);
        if (dc != nullptr) {
            TEXTMETRICA tm{};
            if (GetTextMetricsA(dc, &tm)) {
                minimum = std::max(minimum, static_cast<int>(tm.tmHeight));
            }
            ReleaseDC(control.window, dc);
        }
    }
    control.item_height = minimum;
    return minimum;
}

void CheckListBoxInvalidateCheck(MfcCheckListBoxCompat& control, int item) {
    CheckListBoxInvalidateItem(control, item);
}

void CheckListBoxInvalidateItem(MfcCheckListBoxCompat& control, int item) {
    if (!has_window(control) || item < 0) {
        return;
    }
    RECT rect{};
    if (SendMessageA(control.window, LB_GETITEMRECT,
            static_cast<WPARAM>(item), reinterpret_cast<LPARAM>(&rect)) !=
        LB_ERR) {
        InvalidateRect(control.window, &rect, FALSE);
    }
}

int CheckListBoxCheckFromPoint(MfcCheckListBoxCompat& control, POINT point,
    bool& check_area) {
    check_area = false;
    if (!has_window(control)) {
        return -1;
    }
    bool outside = false;
    const int item = ListBoxItemFromPoint(control, point, outside);
    if (item < 0 || outside) {
        return -1;
    }
    RECT rect{};
    if (SendMessageA(control.window, LB_GETITEMRECT,
            static_cast<WPARAM>(item), reinterpret_cast<LPARAM>(&rect)) ==
        LB_ERR) {
        return item;
    }
    MfcCheckListStateCompat& state = GetAfxCheckListState();
    check_area = point.x < rect.left + state.check_size.cx + 2;
    return item;
}

void CheckListBoxSetSelectionCheck(MfcCheckListBoxCompat& control, int check) {
    if (!has_window(control)) {
        return;
    }
    const LRESULT selected_count = SendMessageA(control.window, LB_GETSELCOUNT,
        0, 0);
    if (selected_count <= 0) {
        return;
    }
    std::vector<int> selected(static_cast<std::size_t>(selected_count));
    SendMessageA(control.window, LB_GETSELITEMS, selected_count,
        reinterpret_cast<LPARAM>(selected.data()));
    for (int item : selected) {
        if (CheckListBoxIsEnabled(control, item)) {
            CheckListBoxSetCheck(control, item, check);
        }
    }
}

void CheckListBoxOnLButtonDown(MfcCheckListBoxCompat& control, UINT flags,
    POINT point) {
    (void)flags;
    if (has_window(control)) {
        SetFocus(control.window);
    }
    bool check_area = false;
    const int item = CheckListBoxCheckFromPoint(control, point, check_area);
    if (item < 0 || !CheckListBoxIsEnabled(control, item)) {
        return;
    }
    if (!check_area ||
        control.check_style == BS_CHECKBOX ||
        control.check_style == BS_3STATE) {
        if ((control_style(control) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0) {
            SendMessageA(control.window, LB_SETSEL, TRUE, item);
        } else {
            SendMessageA(control.window, LB_SETCURSEL, item, 0);
        }
        return;
    }
    const int check = next_check_value(control, CheckListBoxGetCheck(control, item));
    CheckListBoxSetCheck(control, item, check);
    if ((control_style(control) & LBS_EXTENDEDSEL) != 0 &&
        SendMessageA(control.window, LB_GETSEL, item, 0) != 0) {
        CheckListBoxSetSelectionCheck(control, check);
    }
    notify_check_list_box_changed(control);
}

void CheckListBoxOnLButtonDblClk(MfcCheckListBoxCompat& control, UINT flags,
    POINT point) {
    bool check_area = false;
    CheckListBoxCheckFromPoint(control, point, check_area);
    if (check_area) {
        CheckListBoxOnLButtonDown(control, flags, point);
    }
}

void CheckListBoxOnKeyDown(MfcCheckListBoxCompat& control, UINT key,
    UINT repeat, UINT flags) {
    (void)repeat;
    (void)flags;
    if (key != VK_SPACE || !has_window(control)) {
        return;
    }
    const int item = static_cast<int>(
        SendMessageA(control.window, LB_GETCURSEL, 0, 0));
    if (item == LB_ERR || control.check_style == BS_CHECKBOX ||
        control.check_style == BS_3STATE ||
        !CheckListBoxIsEnabled(control, item)) {
        return;
    }
    const int check = next_check_value(control, CheckListBoxGetCheck(control, item));
    CheckListBoxSetCheck(control, item, check);
    if ((control_style(control) & LBS_EXTENDEDSEL) != 0) {
        CheckListBoxSetSelectionCheck(control, check);
    }
    notify_check_list_box_changed(control);
}

int CheckListBoxOnCreate(MfcCheckListBoxCompat& control) {
    CheckListBoxSetItemHeight(control, 0,
        static_cast<UINT>(CheckListBoxCalcMinimumItemHeight(control)));
    return 0;
}

LRESULT CheckListBoxOnSetFont(MfcCheckListBoxCompat& control, WPARAM font,
    LPARAM redraw) {
    if (has_window(control)) {
        SendMessageA(control.window, WM_SETFONT, font, redraw);
        CheckListBoxSetItemHeight(control, 0,
            static_cast<UINT>(CheckListBoxCalcMinimumItemHeight(control)));
    }
    return 0;
}

int CheckListBoxAddString(MfcCheckListBoxCompat& control, const char* text,
    LPARAM item_data) {
    LPARAM payload = reinterpret_cast<LPARAM>(text);
    MfcCheckDataCompat* allocated = nullptr;
    if ((control_style(control) & LBS_HASSTRINGS) == 0) {
        allocated = allocate_check_data(item_data);
        payload = reinterpret_cast<LPARAM>(allocated);
    }
    const LRESULT result = send_control_message(control, LB_ADDSTRING, 0,
        payload);
    if (result == LB_ERR && allocated != nullptr) {
        release_check_data(reinterpret_cast<LPARAM>(allocated));
    }
    return static_cast<int>(result);
}

int CheckListBoxFindString(MfcCheckListBoxCompat& control, int start_after,
    LPARAM item_data) {
    if ((control_style(control) & LBS_HASSTRINGS) != 0) {
        return static_cast<int>(send_control_message(control, LB_FINDSTRING,
            start_after, item_data));
    }
    const int count = static_cast<int>(send_control_message(control,
        LB_GETCOUNT, 0, 0));
    int item = start_after == -1 ? 0 : start_after;
    for (; item < count; ++item) {
        if (CheckListBoxOnLBGetItemData(control, item, 0) == item_data) {
            return item;
        }
    }
    return LB_ERR;
}

int CheckListBoxFindStringExact(MfcCheckListBoxCompat& control, int start_after,
    LPARAM item_data) {
    if ((control_style(control) & (LBS_HASSTRINGS | LBS_SORT)) != 0) {
        return static_cast<int>(send_control_message(control,
            LB_FINDSTRINGEXACT, start_after, item_data));
    }
    return CheckListBoxFindString(control, start_after, item_data);
}

LRESULT CheckListBoxOnLBGetItemData(MfcCheckListBoxCompat& control,
    int item, LPARAM fallback) {
    const LRESULT value = send_control_message(control, LB_GETITEMDATA, item,
        fallback);
    if (value == LB_ERR || value == 0) {
        return value;
    }
    if (is_check_data(value)) {
        return reinterpret_cast<MfcCheckDataCompat*>(value)->item_data;
    }
    return value;
}

LRESULT CheckListBoxGetTextRaw(MfcCheckListBoxCompat& control, int item,
    char* text) {
    return send_control_message(control, LB_GETTEXT, item,
        reinterpret_cast<LPARAM>(text));
}

int CheckListBoxInsertString(MfcCheckListBoxCompat& control, int index,
    const char* text, LPARAM item_data) {
    LPARAM payload = reinterpret_cast<LPARAM>(text);
    MfcCheckDataCompat* allocated = nullptr;
    if ((control_style(control) & LBS_HASSTRINGS) == 0) {
        allocated = allocate_check_data(item_data);
        payload = reinterpret_cast<LPARAM>(allocated);
    }
    const LRESULT result = send_control_message(control, LB_INSERTSTRING,
        index, payload);
    if (result == LB_ERR && allocated != nullptr) {
        release_check_data(reinterpret_cast<LPARAM>(allocated));
    }
    return static_cast<int>(result);
}

int CheckListBoxSelectString(MfcCheckListBoxCompat& control, int start_after,
    LPARAM item_data) {
    if ((control_style(control) & LBS_HASSTRINGS) != 0) {
        return static_cast<int>(send_control_message(control, LB_SELECTSTRING,
            start_after, item_data));
    }
    const int item = CheckListBoxFindString(control, start_after, item_data);
    if (item != LB_ERR) {
        send_control_message(control, LB_SETCURSEL, item, 0);
    }
    return item;
}

LRESULT CheckListBoxOnLBSetItemData(MfcCheckListBoxCompat& control,
    int item, LPARAM item_data) {
    MfcCheckDataCompat* data = check_data_for_item(control, item, true);
    if (data == nullptr) {
        return LB_ERR;
    }
    data->item_data = item_data;
    return send_control_message(control, LB_SETITEMDATA, item,
        reinterpret_cast<LPARAM>(data));
}

void CheckListBoxSetItemHeight(MfcCheckListBoxCompat& control, int item,
    UINT height) {
    const UINT minimum = static_cast<UINT>(
        CheckListBoxCalcMinimumItemHeight(control));
    if (height < minimum) {
        height = minimum;
    }
    control.item_height = static_cast<int>(height);
    send_control_message(control, LB_SETITEMHEIGHT, item, height);
}

bool CreateComboBoxControl(MfcComboBoxCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_standard_control(control, "COMBOBOX", nullptr, style, bounds,
        parent, id) != nullptr;
}

void DestroyComboBoxControl(MfcComboBoxCompat& control) {
    destroy_control(control);
}

void ComboBoxDrawItemDefault(DRAWITEMSTRUCT* draw_item) {
    (void)draw_item;
    trace_default_control_assert("CComboBox::DrawItem", 0xb8);
}

void ComboBoxMeasureItemDefault(MEASUREITEMSTRUCT* measure_item) {
    (void)measure_item;
    trace_default_control_assert("CComboBox::MeasureItem", 0xba);
}

int ComboBoxCompareItemDefault(COMPAREITEMSTRUCT* compare_item) {
    (void)compare_item;
    trace_default_control_assert("CComboBox::CompareItem", 0xbc);
    return 0;
}

void ComboBoxDeleteItemDefault(DELETEITEMSTRUCT* delete_item) {
    (void)delete_item;
}

bool ComboBoxOnChildNotify(MfcComboBoxCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    (void)control;
    (void)wparam;
    switch (message) {
    case WM_DRAWITEM:
        ComboBoxDrawItemDefault(reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
        return true;
    case WM_MEASUREITEM:
        ComboBoxMeasureItemDefault(
            reinterpret_cast<MEASUREITEMSTRUCT*>(lparam));
        return true;
    case WM_DELETEITEM:
        ComboBoxDeleteItemDefault(reinterpret_cast<DELETEITEMSTRUCT*>(lparam));
        return true;
    case WM_COMPAREITEM:
        if (result == nullptr) {
            trace_default_control_assert("CComboBox::OnChildNotify", 0xce);
            return true;
        }
        *result = ComboBoxCompareItemDefault(
            reinterpret_cast<COMPAREITEMSTRUCT*>(lparam));
        return true;
    default:
        return false;
    }
}

std::string ComboBoxGetLBText(MfcComboBoxCompat& control, int index) {
    if (!has_window(control)) {
        return {};
    }
    const LRESULT length = SendMessageA(control.window, CB_GETLBTEXTLEN,
        static_cast<WPARAM>(index), 0);
    if (length == CB_ERR) {
        return {};
    }
    std::vector<char> text(static_cast<std::size_t>(length) + 1U);
    SendMessageA(control.window, CB_GETLBTEXT, static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(text.data()));
    return read_window_text_from_buffer(text.data());
}

void ComboBoxGetLBTextCString(MfcComboBoxCompat& control, int index,
    MfcCStringCompat& text) {
    const std::string value = ComboBoxGetLBText(control, index);
    AssignCStringAnsi(text, value.c_str());
}

bool CreateEditControl(MfcEditCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id) {
    return create_standard_control(control, "EDIT", nullptr, style, bounds,
        parent, id) != nullptr;
}

void DestroyEditControl(MfcEditCompat& control) {
    destroy_control(control);
}

bool CreateScrollBarControl(MfcScrollBarCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_standard_control(control, "SCROLLBAR", nullptr, style, bounds,
        parent, id) != nullptr;
}

void DestroyScrollBarControl(MfcScrollBarCompat& control) {
    destroy_control(control);
}

UINT DragListBoxRegisteredMessage() {
    static UINT message = RegisterWindowMessageA(DRAGLISTMSGSTRING);
    return message;
}

void PreSubclassDragListBox(MfcDragListBoxCompat& box) {
    if (!has_window(box)) {
        return;
    }
    box.style = control_style(box);
    if ((box.style & (LBS_SORT | LBS_MULTIPLESEL)) == 0) {
        MakeDragList(box.window);
    }
}

int BeginDragListBox(MfcDragListBoxCompat& box, POINT point) {
    box.last_insert = -1;
    const int item = has_window(box)
        ? LBItemFromPt(box.window, point, TRUE) : -1;
    DrawDragListBoxInsert(box, item);
    return TRUE;
}

void CancelDragListBoxDrag(MfcDragListBoxCompat& box) {
    DrawDragListBoxInsert(box, -1);
}

unsigned DragListBoxDragging(MfcDragListBoxCompat& box, POINT point) {
    const int item = has_window(box)
        ? LBItemFromPt(box.window, point, FALSE) : -1;
    DrawDragListBoxInsert(box, item);
    if (has_window(box)) {
        LBItemFromPt(box.window, point, TRUE);
    }
    return item == -1 ? DL_STOPCURSOR : DL_MOVECURSOR;
}

void DropDragListBoxItem(MfcDragListBoxCompat& box, int source_index, POINT point) {
    CancelDragListBoxDrag(box);
    if (!has_window(box) || source_index < 0) {
        return;
    }
    int target = LBItemFromPt(box.window, point, TRUE);
    if (target < 0 || target == source_index || target == source_index + 1) {
        return;
    }

    const LRESULT length = SendMessageA(box.window, LB_GETTEXTLEN, source_index, 0);
    if (length < 0) {
        return;
    }
    std::vector<char> text(static_cast<std::size_t>(length) + 1);
    SendMessageA(box.window, LB_GETTEXT, source_index,
        reinterpret_cast<LPARAM>(text.data()));
    const LRESULT data = SendMessageA(box.window, LB_GETITEMDATA, source_index, 0);
    SendMessageA(box.window, LB_DELETESTRING, source_index, 0);
    if (source_index < target) {
        --target;
    }
    const LRESULT inserted = SendMessageA(box.window, LB_INSERTSTRING, target,
        reinterpret_cast<LPARAM>(text.data()));
    if (inserted >= 0) {
        SendMessageA(box.window, LB_SETITEMDATA, inserted, data);
        SendMessageA(box.window, LB_SETCURSEL, inserted, 0);
    }
}

void DrawSingle(MfcDragListBoxCompat& box, int item) {
    DrawDragListBoxInsert(box, item);
}

void DrawDragListBoxInsert(MfcDragListBoxCompat& box, int item) {
    if (!has_window(box) || box.last_insert == item) {
        return;
    }
    if (box.last_insert >= 0) {
        DrawInsert(box.window, box.window, box.last_insert);
    }
    if (item >= 0) {
        DrawInsert(box.window, box.window, item);
    }
    box.last_insert = item;
}

LRESULT RouteDragListBoxChildNotify(MfcDragListBoxCompat& box, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    if (message != DragListBoxRegisteredMessage()) {
        return FALSE;
    }
    if (result == nullptr || lparam == 0) {
        return FALSE;
    }

    auto* info = reinterpret_cast<DRAGLISTINFO*>(lparam);
    switch (wparam) {
    case DL_BEGINDRAG:
        *result = BeginDragListBox(box, info->ptCursor);
        break;
    case DL_DRAGGING:
        *result = DragListBoxDragging(box, info->ptCursor);
        break;
    case DL_DROPPED:
        DropDragListBoxItem(box, static_cast<int>(info->uNotification),
            info->ptCursor);
        *result = TRUE;
        break;
    case DL_CANCELDRAG:
        CancelDragListBoxDrag(box);
        *result = TRUE;
        break;
    default:
        *result = FALSE;
        break;
    }
    return TRUE;
}

const char* GetToolbarCtrlRuntimeClassName() {
    return "CToolBarCtrl";
}

bool CreateToolbarCtrl(MfcToolbarCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_BAR_CLASSES, TOOLBARCLASSNAMEA,
        style, bounds, parent, id) != nullptr;
}

void DestroyToolbarCtrl(MfcToolbarCtrlCompat& control) {
    destroy_control(control);
}

LRESULT ToolbarAddBitmapResource(MfcToolbarCtrlCompat& control, int count,
    UINT bitmap_id) {
    TBADDBITMAP bitmap{};
    bitmap.hInst = GetModuleHandleA(nullptr);
    bitmap.nID = bitmap_id;
    return send_control_message(control, kMfcToolbarAddBitmap, count,
        reinterpret_cast<LPARAM>(&bitmap));
}

LRESULT ToolbarAddBitmapHandle(MfcToolbarCtrlCompat& control, int count,
    HBITMAP bitmap) {
    TBADDBITMAP add{};
    add.hInst = nullptr;
    add.nID = reinterpret_cast<UINT_PTR>(bitmap);
    return send_control_message(control, kMfcToolbarAddBitmap, count,
        reinterpret_cast<LPARAM>(&add));
}

LRESULT ToolbarAddButtons(MfcToolbarCtrlCompat& control, int count,
    const TBBUTTON* buttons) {
    return send_control_message(control, TB_ADDBUTTONSA, count,
        reinterpret_cast<LPARAM>(buttons));
}

LRESULT ToolbarInsertButton(MfcToolbarCtrlCompat& control, int index,
    const TBBUTTON& button) {
    return send_control_message(control, TB_INSERTBUTTONA, index,
        reinterpret_cast<LPARAM>(&button));
}

void ToolbarSetBitmapSize(MfcToolbarCtrlCompat& control, int width, int height) {
    send_control_message(control, TB_SETBITMAPSIZE, 0, make_lparam(width, height));
}

void ToolbarSetButtonSize(MfcToolbarCtrlCompat& control, int width, int height) {
    send_control_message(control, TB_SETBUTTONSIZE, 0, make_lparam(width, height));
}

void ToolbarReplaceBitmap(MfcToolbarCtrlCompat& control,
    const TBREPLACEBITMAP& bitmap) {
    send_control_message(control, kMfcToolbarReplaceBitmap,
        reinterpret_cast<WPARAM>(bitmap.hInstOld),
        reinterpret_cast<LPARAM>(&bitmap));
}

void ToolbarSetButtonStructSize(MfcToolbarCtrlCompat& control) {
    send_control_message(control, kMfcToolbarButtonStructSize,
        sizeof(TBBUTTON), 0);
}

void ToolbarSetOwner(MfcToolbarCtrlCompat& control, HWND owner) {
    send_control_message(control, TB_SETPARENT,
        reinterpret_cast<WPARAM>(owner), 0);
}

bool CreateStatusBarCtrl(MfcStatusBarCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_BAR_CLASSES, STATUSCLASSNAMEA,
        style, bounds, parent, id) != nullptr;
}

void DestroyStatusBarCtrl(MfcStatusBarCtrlCompat& control) {
    destroy_control(control);
}

unsigned StatusBarGetTextRaw(MfcStatusBarCtrlCompat& control, int pane,
    char* buffer, int buffer_chars, unsigned* text_type) {
    if (buffer != nullptr && buffer_chars > 0) {
        buffer[0] = '\0';
    }
    const LRESULT value = send_control_message(control, kMfcStatusGetText,
        pane, reinterpret_cast<LPARAM>(buffer));
    if (text_type != nullptr) {
        *text_type = HIWORD(value);
    }
    return LOWORD(value);
}

std::string StatusBarGetText(MfcStatusBarCtrlCompat& control, int pane,
    unsigned* text_type) {
    const unsigned length = StatusBarGetTextLength(control, pane, text_type);
    std::vector<char> text(length + 1);
    StatusBarGetTextRaw(control, pane, text.data(),
        static_cast<int>(text.size()), text_type);
    return read_window_text_from_buffer(text.data());
}

unsigned StatusBarGetTextLength(MfcStatusBarCtrlCompat& control, int pane,
    unsigned* text_type) {
    const LRESULT value = send_control_message(control,
        kMfcStatusGetTextLength, pane, 0);
    if (text_type != nullptr) {
        *text_type = HIWORD(value);
    }
    return LOWORD(value);
}

std::string StatusBarGetTipText(MfcStatusBarCtrlCompat& control, int pane) {
    std::array<char, 256> text{};
    send_control_message(control, kMfcStatusGetTipText,
        MAKEWPARAM(static_cast<WORD>(pane), text.size()),
        reinterpret_cast<LPARAM>(text.data()));
    return read_window_text_from_buffer(text.data());
}

bool StatusBarGetBorders(MfcStatusBarCtrlCompat& control, int& horizontal,
    int& vertical, int& border) {
    std::array<int, 3> values{};
    const LRESULT ok = send_control_message(control, kMfcStatusGetBorders, 0,
        reinterpret_cast<LPARAM>(values.data()));
    if (ok != 0) {
        horizontal = values[0];
        vertical = values[1];
        border = values[2];
    }
    return ok != 0;
}

void StatusBarDefaultDebugAssert() {
    AfxTraceOutput("CStatusBarCtrl default branch reached.\n");
}

bool StatusBarOnChildNotify(MfcStatusBarCtrlCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    (void)control;
    (void)wparam;
    if (message == WM_DRAWITEM) {
        if (result != nullptr) {
            *result = 1;
        }
        return true;
    }
    if (result != nullptr) {
        *result = lparam;
    }
    return false;
}

const char* GetListCtrlRuntimeClassName() {
    return "CListCtrl";
}

bool CreateListCtrl(MfcListCtrlCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id) {
    return create_common_control(control, ICC_LISTVIEW_CLASSES, WC_LISTVIEWA,
        style, bounds, parent, id) != nullptr;
}

void DestroyListCtrl(MfcListCtrlCompat& control) {
    RemoveListCtrlTemporaryImageLists(control);
    destroy_control(control);
}

void ListCtrlSetItemText(MfcListCtrlCompat& control, int item, int subitem,
    const char* text) {
    LVITEMA value{};
    value.iItem = item;
    value.iSubItem = subitem;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    send_control_message(control, LVM_SETITEMTEXTA, item,
        reinterpret_cast<LPARAM>(&value));
}

void ListCtrlSetItemTextAlt(MfcListCtrlCompat& control, int item, int subitem,
    const char* text) {
    ListCtrlSetItemText(control, item, subitem, text);
}

void ListCtrlSetItemState(MfcListCtrlCompat& control, int item, UINT state,
    UINT mask) {
    LVITEMA value{};
    value.state = state;
    value.stateMask = mask;
    send_control_message(control, LVM_SETITEMSTATE, item,
        reinterpret_cast<LPARAM>(&value));
}

bool ListCtrlGetItemPosition(MfcListCtrlCompat& control, int item, POINT& point) {
    return send_control_message(control, kMfcListGetItemPositionCompat, item,
        reinterpret_cast<LPARAM>(&point)) != 0;
}

void ListCtrlSetItemPosition(MfcListCtrlCompat& control, int item, int x, int y) {
    send_control_message(control, LVM_SETITEMPOSITION, item, make_lparam(x, y));
}

bool ListCtrlGetSubItemRect(MfcListCtrlCompat& control, int item, int subitem,
    int code, RECT& rect) {
    rect.top = subitem;
    rect.left = code;
    return send_control_message(control, kMfcListGetSubItemRectCompat, item,
        reinterpret_cast<LPARAM>(&rect)) != 0;
}

int ListCtrlInsertColumn(MfcListCtrlCompat& control, int column,
    const char* heading, int format, int width, int subitem) {
    LVCOLUMNA value{};
    value.mask = LVCF_TEXT | LVCF_FMT;
    value.pszText = const_cast<char*>(heading == nullptr ? "" : heading);
    value.fmt = format;
    if (width != -1) {
        value.mask |= LVCF_WIDTH;
        value.cx = width;
    }
    if (subitem != -1) {
        value.mask |= LVCF_SUBITEM;
        value.iSubItem = subitem;
    }
    return static_cast<int>(send_control_message(control, LVM_INSERTCOLUMNA,
        column, reinterpret_cast<LPARAM>(&value)));
}

LRESULT ListCtrlInsertItemFull(MfcListCtrlCompat& control, UINT mask, int item,
    int subitem, const char* text, int image, UINT state, UINT state_mask,
    LPARAM data) {
    LVITEMA value{};
    value.mask = mask;
    value.iItem = item;
    value.iSubItem = subitem;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    value.iImage = image;
    value.state = state;
    value.stateMask = state_mask;
    value.lParam = data;
    return send_control_message(control, LVM_INSERTITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
}

bool ListCtrlGetColumnOrderArray(MfcListCtrlCompat& control, int count,
    int* order) {
    return send_control_message(control, LVM_GETCOLUMNORDERARRAY, count,
        reinterpret_cast<LPARAM>(order)) != 0;
}

LRESULT ListCtrlSetItemFull(MfcListCtrlCompat& control, int item, int subitem,
    UINT mask, const char* text, int image, UINT state, UINT state_mask,
    LPARAM data) {
    LVITEMA value{};
    value.mask = mask;
    value.iItem = item;
    value.iSubItem = subitem;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    value.iImage = image;
    value.state = state;
    value.stateMask = state_mask;
    value.lParam = data;
    return send_control_message(control, LVM_SETITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
}

std::string ListCtrlGetItemText(MfcListCtrlCompat& control, int item,
    int subitem) {
    std::vector<char> text(256);
    LVITEMA value{};
    value.iItem = item;
    value.iSubItem = subitem;
    value.pszText = text.data();
    value.cchTextMax = static_cast<int>(text.size());
    while (send_control_message(control, LVM_GETITEMTEXTA, item,
        reinterpret_cast<LPARAM>(&value)) ==
        static_cast<LRESULT>(text.size() - 1)) {
        text.resize(text.size() * 2);
        value.pszText = text.data();
        value.cchTextMax = static_cast<int>(text.size());
    }
    return read_window_text_from_buffer(text.data());
}

void ListCtrlSetColumn(MfcListCtrlCompat& control, int column, int format,
    int width, const char* heading) {
    LVCOLUMNA value{};
    value.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
    value.fmt = format;
    value.cx = width;
    value.pszText = const_cast<char*>(heading == nullptr ? "" : heading);
    send_control_message(control, LVM_SETCOLUMNA, column,
        reinterpret_cast<LPARAM>(&value));
}

void ListCtrlSetColumnFull(MfcListCtrlCompat& control, int column, int format,
    int width, const char* heading) {
    ListCtrlSetColumn(control, column, format, width, heading);
}

int ListCtrlGetColumnWidth(MfcListCtrlCompat& control, int column) {
    LVCOLUMNA value{};
    value.mask = LVCF_WIDTH;
    const LRESULT ok = send_control_message(control, LVM_GETCOLUMNA, column,
        reinterpret_cast<LPARAM>(&value));
    return ok == 0 ? 0 : value.cx;
}

void ListCtrlDebugAssert() {
    AfxTraceOutput("CListCtrl default branch reached.\n");
}

bool ListCtrlOnChildNotify(MfcListCtrlCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    (void)control;
    (void)wparam;
    if (message == WM_DRAWITEM) {
        if (result != nullptr) {
            *result = 1;
        }
        return true;
    }
    if (result != nullptr) {
        *result = lparam;
    }
    return false;
}

MfcImageListCompat ListCtrlSetImageList(MfcListCtrlCompat& control,
    HIMAGELIST image_list, int image_list_type) {
    HIMAGELIST old = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, LVM_SETIMAGELIST, image_list_type,
            reinterpret_cast<LPARAM>(image_list)));
    return attach_returned_image_list_handle(old);
}

MfcImageListCompat RemoveImageList(MfcListCtrlCompat& control,
    int image_list_type) {
    HIMAGELIST current = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, LVM_GETIMAGELIST, image_list_type, 0));
    if (LookupTemporaryImageList(current) == nullptr) {
        return MfcImageListCompat{};
    }
    return ListCtrlSetImageList(control, nullptr, image_list_type);
}

void ListCtrlOnNcDestroy(MfcListCtrlCompat& control) {
    RemoveListCtrlTemporaryImageLists(control);
    detach_control_handle(control);
}

const char* GetTreeCtrlRuntimeClassName() {
    return "CTreeCtrl";
}

bool CreateTreeCtrl(MfcTreeCtrlCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id) {
    return create_common_control(control, ICC_TREEVIEW_CLASSES, WC_TREEVIEWA,
        style, bounds, parent, id) != nullptr;
}

void DestroyTreeCtrl(MfcTreeCtrlCompat& control) {
    RemoveTreeCtrlTemporaryImageLists(control);
    destroy_control(control);
}

void TreeCtrlSetItemText(MfcTreeCtrlCompat& control, HTREEITEM item,
    const char* text) {
    TVITEMA value{};
    value.mask = TVIF_TEXT;
    value.hItem = item;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    send_control_message(control, TVM_SETITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
}

std::string TreeCtrlGetItemText(MfcTreeCtrlCompat& control, HTREEITEM item) {
    std::vector<char> text(256);
    TVITEMA value{};
    value.mask = TVIF_TEXT;
    value.hItem = item;
    value.pszText = text.data();
    value.cchTextMax = static_cast<int>(text.size());
    for (;;) {
        send_control_message(control, TVM_GETITEMA, 0,
            reinterpret_cast<LPARAM>(&value));
        if (lstrlenA(text.data()) != static_cast<int>(text.size() - 1)) {
            break;
        }
        text.resize(text.size() * 2);
        value.pszText = text.data();
        value.cchTextMax = static_cast<int>(text.size());
    }
    return read_window_text_from_buffer(text.data());
}

bool TreeCtrlGetItemImage(MfcTreeCtrlCompat& control, HTREEITEM item,
    int& image, int& selected_image) {
    TVITEMA value{};
    value.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    value.hItem = item;
    const LRESULT ok = send_control_message(control, TVM_GETITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
    if (ok != 0) {
        image = value.iImage;
        selected_image = value.iSelectedImage;
    }
    return ok != 0;
}

UINT TreeCtrlGetItemState(MfcTreeCtrlCompat& control, HTREEITEM item, UINT mask) {
    TVITEMA value{};
    value.mask = TVIF_STATE;
    value.hItem = item;
    value.stateMask = mask;
    const LRESULT ok = send_control_message(control, TVM_GETITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
    return ok == 0 ? 0 : value.state;
}

UINT TreeCtrlGetItemStateAlt(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT mask) {
    return TreeCtrlGetItemState(control, item, mask);
}

LPARAM TreeCtrlGetItemData(MfcTreeCtrlCompat& control, HTREEITEM item) {
    TVITEMA value{};
    value.mask = TVIF_PARAM;
    value.hItem = item;
    send_control_message(control, TVM_GETITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
    return value.lParam;
}

void TreeCtrlSetItemFull(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT mask, const char* text, int image, int selected_image, UINT state,
    UINT state_mask, LPARAM data) {
    TVITEMA value{};
    value.mask = mask;
    value.hItem = item;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    value.iImage = image;
    value.iSelectedImage = selected_image;
    value.state = state;
    value.stateMask = state_mask;
    value.lParam = data;
    send_control_message(control, TVM_SETITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
}

HTREEITEM TreeCtrlInsertItemFull(MfcTreeCtrlCompat& control, HTREEITEM parent,
    HTREEITEM insert_after, UINT mask, const char* text, int image,
    int selected_image, UINT state, UINT state_mask, LPARAM data) {
    TVINSERTSTRUCTA insert{};
    insert.hParent = parent;
    insert.hInsertAfter = insert_after;
    insert.item.mask = mask;
    insert.item.pszText = const_cast<char*>(text == nullptr ? "" : text);
    insert.item.iImage = image;
    insert.item.iSelectedImage = selected_image;
    insert.item.state = state;
    insert.item.stateMask = state_mask;
    insert.item.lParam = data;
    return reinterpret_cast<HTREEITEM>(send_control_message(control,
        TVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&insert)));
}

bool TreeCtrlSortChildrenCB(MfcTreeCtrlCompat& control, HTREEITEM parent,
    PFNTVCOMPARE compare, LPARAM data, bool recurse) {
    TVSORTCB sort{};
    sort.hParent = parent;
    sort.lpfnCompare = compare;
    sort.lParam = data;
    return send_control_message(control, TVM_SORTCHILDRENCB, recurse,
        reinterpret_cast<LPARAM>(&sort)) != 0;
}

bool TreeCtrlGetCheck(MfcTreeCtrlCompat& control, HTREEITEM item) {
    const UINT state = TreeCtrlGetItemState(control, item, TVIS_STATEIMAGEMASK);
    return ((state >> 12) - 1) != 0;
}

void TreeCtrlSetCheck(MfcTreeCtrlCompat& control, HTREEITEM item, bool checked) {
    TVITEMA value{};
    value.mask = TVIF_STATE;
    value.hItem = item;
    value.stateMask = TVIS_STATEIMAGEMASK;
    value.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
    send_control_message(control, TVM_SETITEMA, 0,
        reinterpret_cast<LPARAM>(&value));
}

MfcImageListCompat TreeCtrlSetImageList(MfcTreeCtrlCompat& control,
    HIMAGELIST image_list, int image_list_type) {
    HIMAGELIST old = reinterpret_cast<HIMAGELIST>(send_control_message(control,
        TVM_SETIMAGELIST, image_list_type, reinterpret_cast<LPARAM>(image_list)));
    return attach_returned_image_list_handle(old);
}

MfcImageListCompat RemoveImageList(MfcTreeCtrlCompat& control,
    int image_list_type) {
    HIMAGELIST current = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, TVM_GETIMAGELIST, image_list_type, 0));
    if (LookupTemporaryImageList(current) == nullptr) {
        return MfcImageListCompat{};
    }
    return TreeCtrlSetImageList(control, nullptr, image_list_type);
}

void TreeCtrlOnDestroy(MfcTreeCtrlCompat& control) {
    RemoveTreeCtrlTemporaryImageLists(control);
}

MfcTreeCtrlCompat& ConstructTreeCtrl(MfcTreeCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

HTREEITEM TreeCtrlInsertItemRawInline(MfcTreeCtrlCompat& control,
    const TVINSERTSTRUCTA& insert) {
    return reinterpret_cast<HTREEITEM>(send_control_message(control,
        TVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&insert)));
}

HTREEITEM TreeCtrlInsertItemTextImagesInline(MfcTreeCtrlCompat& control,
    const char* text, int image, int selected_image, HTREEITEM parent,
    HTREEITEM insert_after) {
    return TreeCtrlInsertItemFull(control, parent, insert_after,
        TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE, text, image,
        selected_image, 0, 0, 0);
}

HTREEITEM TreeCtrlInsertItemTextInline(MfcTreeCtrlCompat& control,
    const char* text, HTREEITEM parent, HTREEITEM insert_after) {
    return TreeCtrlInsertItemFull(control, parent, insert_after, TVIF_TEXT,
        text, 0, 0, 0, 0, 0);
}

bool TreeCtrlDeleteItemInline(MfcTreeCtrlCompat& control, HTREEITEM item) {
    return send_control_message(control, TVM_DELETEITEM, 0,
        reinterpret_cast<LPARAM>(item)) != 0;
}

bool TreeCtrlDeleteAllItemsInline(MfcTreeCtrlCompat& control) {
    return send_control_message(control, TVM_DELETEITEM, 0,
        reinterpret_cast<LPARAM>(TVI_ROOT)) != 0;
}

bool TreeCtrlExpandInline(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT code) {
    return send_control_message(control, TVM_EXPAND, code,
        reinterpret_cast<LPARAM>(item)) != 0;
}

UINT TreeCtrlGetCountInline(MfcTreeCtrlCompat& control) {
    return static_cast<UINT>(send_control_message(control, TVM_GETCOUNT, 0, 0));
}

UINT TreeCtrlGetIndentInline(MfcTreeCtrlCompat& control) {
    return static_cast<UINT>(send_control_message(control, TVM_GETINDENT, 0, 0));
}

void TreeCtrlSetIndentInline(MfcTreeCtrlCompat& control, UINT indent) {
    send_control_message(control, TVM_SETINDENT, indent, 0);
}

MfcImageListCompat* TreeCtrlGetImageListInline(MfcTreeCtrlCompat& control,
    int image_list_type) {
    HIMAGELIST handle = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, TVM_GETIMAGELIST, image_list_type, 0));
    return LookupPermanentImageList(handle);
}

MfcImageListCompat* TreeCtrlSetImageListInline(MfcTreeCtrlCompat& control,
    HIMAGELIST image_list, int image_list_type) {
    HIMAGELIST old = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, TVM_SETIMAGELIST, image_list_type,
            reinterpret_cast<LPARAM>(image_list)));
    return LookupPermanentImageList(old);
}

HTREEITEM TreeCtrlGetNextItemInline(MfcTreeCtrlCompat& control,
    HTREEITEM item, UINT code) {
    return reinterpret_cast<HTREEITEM>(send_control_message(control,
        TVM_GETNEXTITEM, code, reinterpret_cast<LPARAM>(item)));
}

HTREEITEM TreeCtrlGetChildItemInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlGetNextItemInline(control, item, TVGN_CHILD);
}

HTREEITEM TreeCtrlGetNextSiblingItemInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlGetNextItemInline(control, item, TVGN_NEXT);
}

HTREEITEM TreeCtrlGetPrevSiblingItemInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlGetNextItemInline(control, item, TVGN_PREVIOUS);
}

HTREEITEM TreeCtrlGetParentItemInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlGetNextItemInline(control, item, TVGN_PARENT);
}

HTREEITEM TreeCtrlGetFirstVisibleItemInline(MfcTreeCtrlCompat& control) {
    return TreeCtrlGetNextItemInline(control, nullptr, TVGN_FIRSTVISIBLE);
}

HTREEITEM TreeCtrlGetNextVisibleItemInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlGetNextItemInline(control, item, TVGN_NEXTVISIBLE);
}

HTREEITEM TreeCtrlGetPrevVisibleItemInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlGetNextItemInline(control, item, TVGN_PREVIOUSVISIBLE);
}

HTREEITEM TreeCtrlGetSelectedItemInline(MfcTreeCtrlCompat& control) {
    return TreeCtrlGetNextItemInline(control, nullptr, TVGN_CARET);
}

HTREEITEM TreeCtrlGetDropHilightItemInline(MfcTreeCtrlCompat& control) {
    return TreeCtrlGetNextItemInline(control, nullptr, TVGN_DROPHILITE);
}

HTREEITEM TreeCtrlGetRootItemInline(MfcTreeCtrlCompat& control) {
    return TreeCtrlGetNextItemInline(control, nullptr, TVGN_ROOT);
}

bool TreeCtrlSelectItemByCodeInline(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT code) {
    return send_control_message(control, TVM_SELECTITEM, code,
        reinterpret_cast<LPARAM>(item)) != 0;
}

bool TreeCtrlSelectItemInline(MfcTreeCtrlCompat& control, HTREEITEM item) {
    return TreeCtrlSelectItemByCodeInline(control, item, TVGN_CARET);
}

bool TreeCtrlSelectDropTargetInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlSelectItemByCodeInline(control, item, TVGN_DROPHILITE);
}

bool TreeCtrlSelectSetFirstVisibleInline(MfcTreeCtrlCompat& control,
    HTREEITEM item) {
    return TreeCtrlSelectItemByCodeInline(control, item, TVGN_FIRSTVISIBLE);
}

bool TreeCtrlGetItemRawInline(MfcTreeCtrlCompat& control, TVITEMA& item) {
    return send_control_message(control, TVM_GETITEMA, 0,
        reinterpret_cast<LPARAM>(&item)) != 0;
}

bool TreeCtrlSetItemRawInline(MfcTreeCtrlCompat& control, const TVITEMA& item) {
    return send_control_message(control, TVM_SETITEMA, 0,
        reinterpret_cast<LPARAM>(&item)) != 0;
}

void TreeCtrlSetItemTextInline(MfcTreeCtrlCompat& control, HTREEITEM item,
    const char* text) {
    TreeCtrlSetItemFull(control, item, TVIF_TEXT, text, 0, 0, 0, 0, 0);
}

void TreeCtrlSetItemImageInline(MfcTreeCtrlCompat& control, HTREEITEM item,
    int image, int selected_image) {
    TreeCtrlSetItemFull(control, item, TVIF_IMAGE | TVIF_SELECTEDIMAGE,
        nullptr, image, selected_image, 0, 0, 0);
}

void TreeCtrlSetItemStateInline(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT state, UINT state_mask) {
    TreeCtrlSetItemFull(control, item, TVIF_STATE, nullptr, 0, 0, state,
        state_mask, 0);
}

void TreeCtrlSetItemDataInline(MfcTreeCtrlCompat& control, HTREEITEM item,
    LPARAM data) {
    TreeCtrlSetItemFull(control, item, TVIF_PARAM, nullptr, 0, 0, 0, 0, data);
}

HWND TreeCtrlEditLabelInline(MfcTreeCtrlCompat& control, HTREEITEM item) {
    return reinterpret_cast<HWND>(send_control_message(control,
        TVM_EDITLABELA, 0, reinterpret_cast<LPARAM>(item)));
}

HTREEITEM TreeCtrlHitTestInline(MfcTreeCtrlCompat& control,
    TVHITTESTINFO& hit_test) {
    return reinterpret_cast<HTREEITEM>(send_control_message(control,
        TVM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit_test)));
}

HWND TreeCtrlGetEditControlInline(MfcTreeCtrlCompat& control) {
    return reinterpret_cast<HWND>(send_control_message(control,
        TVM_GETEDITCONTROL, 0, 0));
}

UINT TreeCtrlGetVisibleCountInline(MfcTreeCtrlCompat& control) {
    return static_cast<UINT>(send_control_message(control,
        TVM_GETVISIBLECOUNT, 0, 0));
}

bool TreeCtrlSortChildrenInline(MfcTreeCtrlCompat& control, HTREEITEM item) {
    return send_control_message(control, TVM_SORTCHILDREN, FALSE,
        reinterpret_cast<LPARAM>(item)) != 0;
}

bool TreeCtrlEnsureVisibleInline(MfcTreeCtrlCompat& control, HTREEITEM item) {
    return send_control_message(control, TVM_ENSUREVISIBLE, 0,
        reinterpret_cast<LPARAM>(item)) != 0;
}

bool TreeCtrlSortChildrenCBRawInline(MfcTreeCtrlCompat& control,
    TVSORTCB& sort) {
    return send_control_message(control, TVM_SORTCHILDRENCB, FALSE,
        reinterpret_cast<LPARAM>(&sort)) != 0;
}

HWND TreeCtrlGetToolTipsInline(MfcTreeCtrlCompat& control) {
    return reinterpret_cast<HWND>(send_control_message(control,
        TVM_GETTOOLTIPS, 0, 0));
}

HWND TreeCtrlSetToolTipsInline(MfcTreeCtrlCompat& control, HWND tooltips) {
    return reinterpret_cast<HWND>(send_control_message(control,
        TVM_SETTOOLTIPS, reinterpret_cast<WPARAM>(tooltips), 0));
}

COLORREF TreeCtrlGetBkColorInline(MfcTreeCtrlCompat& control) {
    return static_cast<COLORREF>(send_control_message(control,
        TVM_GETBKCOLOR, 0, 0));
}

COLORREF TreeCtrlSetBkColorInline(MfcTreeCtrlCompat& control, COLORREF color) {
    return static_cast<COLORREF>(send_control_message(control,
        TVM_SETBKCOLOR, 0, color));
}

int TreeCtrlGetItemHeightInline(MfcTreeCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        TVM_GETITEMHEIGHT, 0, 0));
}

int TreeCtrlSetItemHeightInline(MfcTreeCtrlCompat& control, int height) {
    return static_cast<int>(send_control_message(control,
        TVM_SETITEMHEIGHT, height, 0));
}

COLORREF TreeCtrlGetTextColorInline(MfcTreeCtrlCompat& control) {
    return static_cast<COLORREF>(send_control_message(control,
        TVM_GETTEXTCOLOR, 0, 0));
}

COLORREF TreeCtrlSetTextColorInline(MfcTreeCtrlCompat& control,
    COLORREF color) {
    return static_cast<COLORREF>(send_control_message(control,
        TVM_SETTEXTCOLOR, 0, color));
}

bool TreeCtrlSetInsertMarkInline(MfcTreeCtrlCompat& control, HTREEITEM item,
    bool after) {
    return send_control_message(control, TVM_SETINSERTMARK,
        after ? TRUE : FALSE, reinterpret_cast<LPARAM>(item)) != 0;
}

COLORREF TreeCtrlGetInsertMarkColorInline(MfcTreeCtrlCompat& control) {
    return static_cast<COLORREF>(send_control_message(control,
        TVM_GETINSERTMARKCOLOR, 0, 0));
}

COLORREF TreeCtrlSetInsertMarkColorInline(MfcTreeCtrlCompat& control,
    COLORREF color) {
    return static_cast<COLORREF>(send_control_message(control,
        TVM_SETINSERTMARKCOLOR, 0, color));
}

MfcHotKeyCtrlCompat& ConstructHotKeyCtrl(MfcHotKeyCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

void HotKeySetHotKeyPartsInline(MfcHotKeyCtrlCompat& control,
    BYTE virtual_key, BYTE modifiers) {
    send_control_message(control, HKM_SETHOTKEY, MAKEWORD(virtual_key, modifiers),
        0);
}

WORD HotKeyGetHotKeyInline(MfcHotKeyCtrlCompat& control) {
    return static_cast<WORD>(send_control_message(control, HKM_GETHOTKEY, 0, 0));
}

void HotKeySetRulesInline(MfcHotKeyCtrlCompat& control,
    WORD invalid_combinations, WORD modifiers) {
    send_control_message(control, HKM_SETRULES, invalid_combinations, modifiers);
}

void HotKeySetHotKeyInline(MfcHotKeyCtrlCompat& control, WORD hot_key) {
    send_control_message(control, HKM_SETHOTKEY, hot_key, 0);
}

void ToolTipSetToolInfoInline(MfcToolTipCtrlCompat& control,
    const TOOLINFOA& tool_info) {
    send_control_message(control, TTM_SETTOOLINFOA, 0,
        reinterpret_cast<LPARAM>(&tool_info));
}

void ToolTipRelayEventInline(MfcToolTipCtrlCompat& control, const MSG& msg) {
    send_control_message(control, TTM_RELAYEVENT, 0,
        reinterpret_cast<LPARAM>(&msg));
}

int ToolTipGetToolCountInline(MfcToolTipCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        TTM_GETTOOLCOUNT, 0, 0));
}

const char* GetToolTipCtrlRuntimeClassName() {
    return "CToolTipCtrl";
}

MfcToolTipCtrlCompat& ConstructToolTipCtrl(MfcToolTipCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

bool CreateToolTipCtrl(MfcToolTipCtrlCompat& control, HWND owner,
    DWORD style) {
    ensure_common_controls(ICC_WIN95_CLASSES);
    control.style = style | WS_POPUP;
    control.window = CreateWindowExA(0, TOOLTIPS_CLASSA, nullptr,
        control.style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, owner, nullptr, GetModuleHandleA(nullptr), nullptr);
    return control.window != nullptr;
}

void DestructToolTipCtrl(MfcToolTipCtrlCompat& control) {
    destroy_control(control);
}

bool ToolTipAddToolRaw(MfcToolTipCtrlCompat& control,
    const TOOLINFOA& tool_info) {
    return send_control_message(control, TTM_ADDTOOLA, 0,
        reinterpret_cast<LPARAM>(&tool_info)) != 0;
}

HWND ToolTipWindowFromPoint(POINT point) {
    HWND window = WindowFromPoint(point);
    if (window == nullptr) {
        return nullptr;
    }
    HWND parent = GetParent(window);
    if (parent != nullptr) {
        return parent;
    }
    ScreenToClient(window, &point);
    HWND child = ChildWindowFromPoint(window, point);
    return child != nullptr && !IsWindowEnabled(child) ? child : window;
}

bool ToolTipAddToolTextRect(MfcToolTipCtrlCompat& control, HWND tool_window,
    const char* text, const RECT* rect, UINT_PTR id) {
    TOOLINFOA info{};
    info.cbSize = sizeof(info);
    info.uFlags = rect == nullptr ? TTF_IDISHWND : 0;
    info.hwnd = rect == nullptr ? GetParent(tool_window) : tool_window;
    info.uId = rect == nullptr ? reinterpret_cast<UINT_PTR>(tool_window) : id;
    info.lpszText = const_cast<char*>(text == nullptr ? "" : text);
    if (rect != nullptr) {
        info.rect = *rect;
    }
    return ToolTipAddToolRaw(control, info);
}

bool ToolTipAddToolResourceRect(MfcToolTipCtrlCompat& control,
    HWND tool_window, UINT text_resource_id, const RECT* rect, UINT_PTR id) {
    return ToolTipAddToolTextRect(control, tool_window,
        MAKEINTRESOURCEA(text_resource_id), rect, id);
}

bool ToolTipDeleteTool(MfcToolTipCtrlCompat& control, HWND tool_window,
    UINT_PTR id) {
    TOOLINFOA info{};
    info.cbSize = sizeof(info);
    info.hwnd = tool_window;
    info.uId = id;
    return send_control_message(control, TTM_DELTOOLA, 0,
        reinterpret_cast<LPARAM>(&info)) != 0;
}

bool ToolTipGetText(MfcToolTipCtrlCompat& control, HWND tool_window,
    UINT_PTR id, char* buffer) {
    TOOLINFOA info{};
    info.cbSize = sizeof(info);
    info.hwnd = tool_window;
    info.uId = id;
    info.lpszText = buffer;
    return send_control_message(control, TTM_GETTEXTA, 0,
        reinterpret_cast<LPARAM>(&info)) != 0;
}

bool ToolTipGetToolInfo(MfcToolTipCtrlCompat& control, HWND tool_window,
    UINT_PTR id, TOOLINFOA& info) {
    info.cbSize = sizeof(info);
    info.hwnd = tool_window;
    info.uId = id;
    return send_control_message(control, TTM_GETTOOLINFOA, 0,
        reinterpret_cast<LPARAM>(&info)) != 0;
}

bool ToolTipHitTest(MfcToolTipCtrlCompat& control, HWND window, POINT point,
    TOOLINFOA& info) {
    TTHITTESTINFOA hit{};
    hit.hwnd = window;
    hit.pt = point;
    hit.ti.cbSize = sizeof(hit.ti);
    const bool ok = send_control_message(control, TTM_HITTESTA, 0,
        reinterpret_cast<LPARAM>(&hit)) != 0;
    if (ok) {
        info = hit.ti;
    }
    return ok;
}

bool ToolTipNewToolRect(MfcToolTipCtrlCompat& control, HWND tool_window,
    UINT_PTR id, const RECT& rect) {
    TOOLINFOA info{};
    info.cbSize = sizeof(info);
    info.hwnd = tool_window;
    info.uId = id;
    info.rect = rect;
    return send_control_message(control, TTM_NEWTOOLRECTA, 0,
        reinterpret_cast<LPARAM>(&info)) != 0;
}

bool ToolTipUpdateTipText(MfcToolTipCtrlCompat& control, HWND tool_window,
    UINT_PTR id, const char* text) {
    TOOLINFOA info{};
    info.cbSize = sizeof(info);
    info.hwnd = tool_window;
    info.uId = id;
    info.lpszText = const_cast<char*>(text == nullptr ? "" : text);
    return send_control_message(control, TTM_UPDATETIPTEXTA, 0,
        reinterpret_cast<LPARAM>(&info)) != 0;
}

bool ToolTipUpdateTipTextResource(MfcToolTipCtrlCompat& control,
    HWND tool_window, UINT_PTR id, UINT text_resource_id) {
    return ToolTipUpdateTipText(control, tool_window, id,
        MAKEINTRESOURCEA(text_resource_id));
}

bool ToolTipSetRoutingFlag(MfcToolTipCtrlCompat& control, bool enable,
    UINT flag) {
    if (enable) {
        control.style |= flag;
    } else {
        control.style &= ~flag;
    }
    return true;
}

bool EnableToolTipsFlag(MfcToolTipCtrlCompat& control, bool enable) {
    return ToolTipSetRoutingFlag(control, enable, 0x0001);
}

bool EnableTrackingToolTipsFlag(MfcToolTipCtrlCompat& control, bool enable) {
    return ToolTipSetRoutingFlag(control, enable, 0x0400);
}

void ToolTipRelayMouseEvent(MfcToolTipCtrlCompat& control, const MSG& message) {
    MSG relay = message;
    relay.hwnd = reinterpret_cast<HWND>(send_control_message(control,
        TTM_WINDOWFROMPOINT, 0, reinterpret_cast<LPARAM>(&relay.pt)));
    send_control_message(control, TTM_RELAYEVENT, 0,
        reinterpret_cast<LPARAM>(&relay));
}

void ToolTipFilterRelayMessage(MfcToolTipCtrlCompat& control,
    const MSG& message) {
    ToolTipRelayMouseEvent(control, message);
}

MfcToolTipCtrlCompat* DeleteToolTipCtrlScalarDtor(
    MfcToolTipCtrlCompat* control, unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestructToolTipCtrl);
}

MfcSpinButtonCtrlCompat& ConstructSpinButtonCtrl(
    MfcSpinButtonCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

UINT SpinButtonGetAccelInline(MfcSpinButtonCtrlCompat& control, int count,
    UDACCEL* accel) {
    return static_cast<UINT>(send_control_message(control, UDM_GETACCEL,
        count, reinterpret_cast<LPARAM>(accel)) & 0xffff);
}

UINT SpinButtonGetBaseInline(MfcSpinButtonCtrlCompat& control) {
    return static_cast<UINT>(send_control_message(control, UDM_GETBASE, 0, 0) &
        0xffff);
}

HWND SpinButtonGetBuddyInline(MfcSpinButtonCtrlCompat& control) {
    return reinterpret_cast<HWND>(send_control_message(control,
        UDM_GETBUDDY, 0, 0));
}

int SpinButtonGetPosInline(MfcSpinButtonCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, UDM_GETPOS, 0, 0));
}

DWORD SpinButtonGetRangeRawInline(MfcSpinButtonCtrlCompat& control) {
    return static_cast<DWORD>(send_control_message(control, UDM_GETRANGE, 0, 0));
}

bool SpinButtonSetAccelInline(MfcSpinButtonCtrlCompat& control, int count,
    const UDACCEL* accel) {
    return (send_control_message(control, UDM_SETACCEL, count,
        reinterpret_cast<LPARAM>(accel)) & 0xffff) != 0;
}

int SpinButtonSetBaseInline(MfcSpinButtonCtrlCompat& control, int base) {
    return static_cast<int>(send_control_message(control, UDM_SETBASE, base, 0));
}

HWND SpinButtonSetBuddyInline(MfcSpinButtonCtrlCompat& control, HWND buddy) {
    return reinterpret_cast<HWND>(send_control_message(control, UDM_SETBUDDY,
        reinterpret_cast<WPARAM>(buddy), 0));
}

int SpinButtonSetPosInline(MfcSpinButtonCtrlCompat& control, int position) {
    LRESULT previous = send_control_message(control, UDM_SETPOS, 0,
        MAKELPARAM(static_cast<WORD>(position), 0));
    return static_cast<short>(previous);
}

void SpinButtonSetRangeInline(MfcSpinButtonCtrlCompat& control, int lower,
    int upper) {
    send_control_message(control, UDM_SETRANGE, 0,
        MAKELPARAM(static_cast<WORD>(upper), static_cast<WORD>(lower)));
}

MfcSliderCtrlCompat& ConstructSliderCtrl(MfcSliderCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

int SliderGetLineSizeInline(MfcSliderCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TBM_GETLINESIZE, 0, 0));
}

int SliderSetLineSizeInline(MfcSliderCtrlCompat& control, int line_size) {
    return static_cast<int>(send_control_message(control, TBM_SETLINESIZE, 0,
        line_size));
}

int SliderGetPageSizeInline(MfcSliderCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TBM_GETPAGESIZE, 0, 0));
}

int SliderSetPageSizeInline(MfcSliderCtrlCompat& control, int page_size) {
    return static_cast<int>(send_control_message(control, TBM_SETPAGESIZE, 0,
        page_size));
}

int SliderGetRangeMaxInline(MfcSliderCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TBM_GETRANGEMAX, 0, 0));
}

int SliderGetRangeMinInline(MfcSliderCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TBM_GETRANGEMIN, 0, 0));
}

void SliderSetRangeMinInline(MfcSliderCtrlCompat& control, int minimum,
    bool redraw) {
    send_control_message(control, TBM_SETRANGEMIN, redraw ? TRUE : FALSE,
        minimum);
}

void SliderSetRangeMaxInline(MfcSliderCtrlCompat& control, int maximum,
    bool redraw) {
    send_control_message(control, TBM_SETRANGEMAX, redraw ? TRUE : FALSE,
        maximum);
}

void SliderClearSelectionInline(MfcSliderCtrlCompat& control, bool redraw) {
    send_control_message(control, TBM_CLEARSEL, redraw ? TRUE : FALSE, 0);
}

void SliderGetChannelRectInline(MfcSliderCtrlCompat& control, RECT& rect) {
    send_control_message(control, TBM_GETCHANNELRECT, 0,
        reinterpret_cast<LPARAM>(&rect));
}

void SliderGetThumbRectInline(MfcSliderCtrlCompat& control, RECT& rect) {
    send_control_message(control, TBM_GETTHUMBRECT, 0,
        reinterpret_cast<LPARAM>(&rect));
}

int SliderGetPosInline(MfcSliderCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TBM_GETPOS, 0, 0));
}

void SliderSetPosInline(MfcSliderCtrlCompat& control, int position) {
    send_control_message(control, TBM_SETPOS, TRUE, position);
}

void SliderSetZeroPosNoRedrawInline(MfcSliderCtrlCompat& control) {
    send_control_message(control, TBM_SETPOS, FALSE, 0);
}

void SliderClearTicsInline(MfcSliderCtrlCompat& control, bool redraw) {
    send_control_message(control, TBM_CLEARTICS, redraw ? TRUE : FALSE, 0);
}

int SliderGetNumTicsInline(MfcSliderCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TBM_GETNUMTICS, 0, 0));
}

DWORD* SliderGetTicArrayInline(MfcSliderCtrlCompat& control) {
    return reinterpret_cast<DWORD*>(send_control_message(control,
        TBM_GETPTICS, 0, 0));
}

int SliderGetTicInline(MfcSliderCtrlCompat& control, int tic) {
    return static_cast<int>(send_control_message(control, TBM_GETTIC, tic, 0));
}

int SliderGetTicPosInline(MfcSliderCtrlCompat& control, int tic) {
    return static_cast<int>(send_control_message(control, TBM_GETTICPOS, tic, 0));
}

bool SliderSetTicInline(MfcSliderCtrlCompat& control, int position) {
    return send_control_message(control, TBM_SETTIC, 0, position) != 0;
}

void SliderSetTicFreqInline(MfcSliderCtrlCompat& control, int frequency) {
    send_control_message(control, TBM_SETTICFREQ, frequency, 0);
}

HWND SliderGetBuddyInline(MfcSliderCtrlCompat& control, bool left_or_top) {
    return reinterpret_cast<HWND>(send_control_message(control, TBM_GETBUDDY,
        left_or_top ? TRUE : FALSE, 0));
}

HWND SliderSetBuddyInline(MfcSliderCtrlCompat& control, HWND buddy,
    bool left_or_top) {
    return reinterpret_cast<HWND>(send_control_message(control, TBM_SETBUDDY,
        left_or_top ? TRUE : FALSE, reinterpret_cast<LPARAM>(buddy)));
}

HWND SliderGetToolTipsInline(MfcSliderCtrlCompat& control) {
    return reinterpret_cast<HWND>(send_control_message(control,
        TBM_GETTOOLTIPS, 0, 0));
}

void SliderSetToolTipsInline(MfcSliderCtrlCompat& control, HWND tooltips) {
    send_control_message(control, TBM_SETTOOLTIPS,
        reinterpret_cast<WPARAM>(tooltips), 0);
}

int SliderSetTipSideInline(MfcSliderCtrlCompat& control, int location) {
    return static_cast<int>(send_control_message(control, TBM_SETTIPSIDE,
        location, 0));
}

MfcProgressCtrlCompat& ConstructProgressCtrl(MfcProgressCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

void ProgressSetRangeInline(MfcProgressCtrlCompat& control, int lower,
    int upper) {
    send_control_message(control, PBM_SETRANGE, 0,
        MAKELPARAM(static_cast<WORD>(lower), static_cast<WORD>(upper)));
}

void ProgressSetRange32Inline(MfcProgressCtrlCompat& control, int lower,
    int upper) {
    send_control_message(control, PBM_SETRANGE32, lower, upper);
}

int ProgressGetPosInline(MfcProgressCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, PBM_GETPOS, 0, 0));
}

int ProgressOffsetPosInline(MfcProgressCtrlCompat& control, int offset) {
    return static_cast<int>(send_control_message(control, PBM_DELTAPOS,
        offset, 0));
}

int ProgressSetStepInline(MfcProgressCtrlCompat& control, int step) {
    return static_cast<int>(send_control_message(control, PBM_SETSTEP, step, 0));
}

int ProgressStepItInline(MfcProgressCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, PBM_STEPIT, 0, 0));
}

MfcHeaderCtrlCompat& ConstructHeaderCtrl(MfcHeaderCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

int HeaderGetItemCountInline(MfcHeaderCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        HDM_GETITEMCOUNT, 0, 0));
}

int HeaderInsertItemInline(MfcHeaderCtrlCompat& control, int item,
    const HDITEMA& header_item) {
    return static_cast<int>(send_control_message(control, HDM_INSERTITEMA,
        item, reinterpret_cast<LPARAM>(&header_item)));
}

bool HeaderDeleteItemInline(MfcHeaderCtrlCompat& control, int item) {
    return send_control_message(control, HDM_DELETEITEM, item, 0) != 0;
}

bool HeaderGetItemInline(MfcHeaderCtrlCompat& control, int item,
    HDITEMA& header_item) {
    return send_control_message(control, HDM_GETITEMA, item,
        reinterpret_cast<LPARAM>(&header_item)) != 0;
}

bool HeaderSetItemInline(MfcHeaderCtrlCompat& control, int item,
    const HDITEMA& header_item) {
    return send_control_message(control, HDM_SETITEMA, item,
        reinterpret_cast<LPARAM>(&header_item)) != 0;
}

bool HeaderLayoutInline(MfcHeaderCtrlCompat& control, HDLAYOUT& layout) {
    return send_control_message(control, HDM_LAYOUT, 0,
        reinterpret_cast<LPARAM>(&layout)) != 0;
}

HIMAGELIST ImageListGetHandleInline(const MfcImageListCompat& image_list) {
    return image_list.handle;
}

HIMAGELIST ImageListGetSafeHandleInline(const MfcImageListCompat* image_list) {
    return image_list == nullptr ? nullptr : image_list->handle;
}

int ImageListGetImageCountInline(const MfcImageListCompat& image_list) {
    return image_list.handle == nullptr ? 0 : ImageList_GetImageCount(image_list.handle);
}

int ImageListAddBitmapsInline(const MfcImageListCompat& image_list,
    HBITMAP image, HBITMAP mask) {
    return image_list.handle == nullptr ? -1 :
        ImageList_Add(image_list.handle, image, mask);
}

int ImageListAddMaskedInline(const MfcImageListCompat& image_list,
    HBITMAP image, COLORREF mask_color) {
    return image_list.handle == nullptr ? -1 :
        ImageList_AddMasked(image_list.handle, image, mask_color);
}

bool ImageListRemoveInline(const MfcImageListCompat& image_list, int image) {
    return image_list.handle != nullptr &&
        ImageList_Remove(image_list.handle, image) != FALSE;
}

bool ImageListReplaceBitmapsInline(const MfcImageListCompat& image_list,
    int image, HBITMAP replacement, HBITMAP mask) {
    return image_list.handle != nullptr &&
        ImageList_Replace(image_list.handle, image, replacement, mask) != FALSE;
}

int ImageListAddIconInline(const MfcImageListCompat& image_list, HICON icon) {
    return image_list.handle == nullptr ? -1 :
        ImageList_ReplaceIcon(image_list.handle, -1, icon);
}

int ImageListReplaceIconInline(const MfcImageListCompat& image_list,
    int image, HICON icon) {
    return image_list.handle == nullptr ? -1 :
        ImageList_ReplaceIcon(image_list.handle, image, icon);
}

HICON ImageListExtractIconInline(const MfcImageListCompat& image_list,
    int image) {
    return image_list.handle == nullptr ? nullptr :
        ImageList_GetIcon(image_list.handle, image, 0);
}

bool ImageListDrawInline(const MfcImageListCompat& image_list, int image,
    HDC destination, int x, int y, UINT style) {
    return image_list.handle != nullptr && destination != nullptr &&
        ImageList_Draw(image_list.handle, image, destination, x, y, style) != FALSE;
}

COLORREF ImageListSetBkColorInline(const MfcImageListCompat& image_list,
    COLORREF color) {
    return image_list.handle == nullptr ? CLR_NONE :
        ImageList_SetBkColor(image_list.handle, color);
}

COLORREF ImageListGetBkColorInline(const MfcImageListCompat& image_list) {
    return image_list.handle == nullptr ? CLR_NONE :
        ImageList_GetBkColor(image_list.handle);
}

bool ImageListSetOverlayImageInline(const MfcImageListCompat& image_list,
    int image, int overlay) {
    return image_list.handle != nullptr &&
        ImageList_SetOverlayImage(image_list.handle, image, overlay) != FALSE;
}

bool ImageListGetImageInfoInline(const MfcImageListCompat& image_list,
    int image, IMAGEINFO& info) {
    return image_list.handle != nullptr &&
        ImageList_GetImageInfo(image_list.handle, image, &info) != FALSE;
}

bool ImageListBeginDragInline(const MfcImageListCompat& image_list,
    int image, int hotspot_x, int hotspot_y) {
    return image_list.handle != nullptr &&
        ImageList_BeginDrag(image_list.handle, image, hotspot_x, hotspot_y) != FALSE;
}

void ImageListEndDragInline() {
    ImageList_EndDrag();
}

bool ImageListDragMoveInline(int x, int y) {
    return ImageList_DragMove(x, y) != FALSE;
}

bool ImageListSetDragCursorImageInline(const MfcImageListCompat& image_list,
    int image, int hotspot_x, int hotspot_y) {
    return image_list.handle != nullptr &&
        ImageList_SetDragCursorImage(image_list.handle, image, hotspot_x,
            hotspot_y) != FALSE;
}

bool ImageListDragShowNoLockInline(bool show) {
    return ImageList_DragShowNolock(show ? TRUE : FALSE) != FALSE;
}

MfcImageListCompat* ImageListGetDragImageInline(POINT* drag_position,
    POINT* hotspot) {
    HIMAGELIST handle = ImageList_GetDragImage(drag_position, hotspot);
    return LookupPermanentImageList(handle);
}

bool ImageListDragEnterInline(HWND lock_window, int x, int y) {
    return ImageList_DragEnter(lock_window, x, y) != FALSE;
}

bool ImageListDragLeaveInline(HWND lock_window) {
    return ImageList_DragLeave(lock_window) != FALSE;
}

bool CreateSpinButtonCtrl(MfcSpinButtonCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_UPDOWN_CLASS, UPDOWN_CLASSA,
        style, bounds, parent, id) != nullptr;
}

void DestroySpinButtonCtrl(MfcSpinButtonCtrlCompat& control) {
    destroy_control(control);
}

void SpinButtonGetRange(MfcSpinButtonCtrlCompat& control, int& lower,
    int& upper) {
    const LRESULT range = send_control_message(control, UDM_GETRANGE, 0, 0);
    upper = static_cast<short>(HIWORD(range));
    lower = static_cast<short>(LOWORD(range));
}

bool CreateSliderCtrl(MfcSliderCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_BAR_CLASSES, TRACKBAR_CLASSA,
        style, bounds, parent, id) != nullptr;
}

void DestroySliderCtrl(MfcSliderCtrlCompat& control) {
    destroy_control(control);
}

void SliderGetRange(MfcSliderCtrlCompat& control, int& lower, int& upper) {
    lower = static_cast<int>(send_control_message(control, TBM_GETRANGEMIN, 0, 0));
    upper = static_cast<int>(send_control_message(control, TBM_GETRANGEMAX, 0, 0));
}

void SliderSetRange(MfcSliderCtrlCompat& control, int lower, int upper,
    bool redraw) {
    send_control_message(control, TBM_SETRANGEMIN, redraw, lower);
    send_control_message(control, TBM_SETRANGEMAX, redraw, upper);
}

void SliderGetSelection(MfcSliderCtrlCompat& control, LRESULT& start,
    LRESULT& end) {
    start = send_control_message(control, TBM_GETSELSTART, 0, 0);
    end = send_control_message(control, TBM_GETSELEND, 0, 0);
}

void SliderSetSelection(MfcSliderCtrlCompat& control, LPARAM start, LPARAM end) {
    send_control_message(control, TBM_SETSELSTART, 0, start);
    send_control_message(control, TBM_SETSELEND, 0, end);
}

bool CreateProgressCtrl(MfcProgressCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_PROGRESS_CLASS, PROGRESS_CLASSA,
        style, bounds, parent, id) != nullptr;
}

void DestroyProgressCtrl(MfcProgressCtrlCompat& control) {
    destroy_control(control);
}

bool CreateHeaderCtrl(MfcHeaderCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_LISTVIEW_CLASSES, WC_HEADERA,
        style, bounds, parent, id) != nullptr;
}

void DestroyHeaderCtrl(MfcHeaderCtrlCompat& control) {
    destroy_control(control);
}

void HeaderDebugAssert() {
    AfxTraceOutput("CHeaderCtrl default branch reached.\n");
}

bool HeaderOnChildNotify(MfcHeaderCtrlCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result) {
    (void)control;
    (void)wparam;
    if (message == WM_DRAWITEM) {
        if (result != nullptr) {
            *result = 1;
        }
        return true;
    }
    if (result != nullptr) {
        *result = lparam;
    }
    return false;
}

bool CreateHotKeyCtrl(MfcHotKeyCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_HOTKEY_CLASS, HOTKEY_CLASSA,
        style, bounds, parent, id) != nullptr;
}

void DestroyHotKeyCtrl(MfcHotKeyCtrlCompat& control) {
    destroy_control(control);
}

void HotKeyGetHotKey(MfcHotKeyCtrlCompat& control, WORD& virtual_key,
    WORD& modifiers) {
    const LRESULT value = send_control_message(control, HKM_GETHOTKEY, 0, 0);
    virtual_key = LOBYTE(LOWORD(value));
    modifiers = HIBYTE(LOWORD(value));
}

const char* GetTabCtrlRuntimeClassName() {
    return "CTabCtrl";
}

bool CreateTabCtrl(MfcTabCtrlCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id) {
    return create_common_control(control, ICC_TAB_CLASSES, WC_TABCONTROLA,
        style, bounds, parent, id) != nullptr;
}

void DestroyTabCtrl(MfcTabCtrlCompat& control) {
    send_control_message(control, TCM_SETIMAGELIST, 0, 0);
    destroy_control(control);
}

void TabDebugAssert() {
    AfxTraceOutput("CTabCtrl default branch reached.\n");
}

bool TabOnChildNotify(MfcTabCtrlCompat& control, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT* result) {
    (void)control;
    (void)wparam;
    if (message == WM_DRAWITEM) {
        if (result != nullptr) {
            *result = 1;
        }
        return true;
    }
    if (result != nullptr) {
        *result = lparam;
    }
    return false;
}

int TabGetItemImage(MfcTabCtrlCompat& control, int item, const char* text) {
    TCITEMA value{};
    value.mask = TCIF_IMAGE;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    const LRESULT ok = send_control_message(control, TCM_GETITEMA, item,
        reinterpret_cast<LPARAM>(&value));
    return ok == 0 ? 0 : value.iImage;
}

void TabSetItemImage(MfcTabCtrlCompat& control, int item, const char* text,
    int image) {
    TCITEMA value{};
    value.mask = TCIF_IMAGE;
    value.iImage = image;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    send_control_message(control, TCM_SETITEMA, item,
        reinterpret_cast<LPARAM>(&value));
}

void TabInsertItemFull(MfcTabCtrlCompat& control, int item, UINT mask,
    const char* text, int image, LPARAM data) {
    TCITEMA value{};
    value.mask = mask;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    value.iImage = image;
    value.lParam = data;
    send_control_message(control, TCM_INSERTITEMA, item,
        reinterpret_cast<LPARAM>(&value));
}

void TabSetItemFull(MfcTabCtrlCompat& control, int item, UINT mask,
    const char* text, int image, LPARAM data, UINT state, UINT state_mask) {
    TCITEMA value{};
    value.mask = mask;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    value.iImage = image;
    value.lParam = data;
    value.dwState = state;
    value.dwStateMask = state_mask;
    send_control_message(control, TCM_SETITEMA, item,
        reinterpret_cast<LPARAM>(&value));
}

MfcTabCtrlCompat& ConstructTabCtrl(MfcTabCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

MfcImageListCompat* TabGetImageListInline(MfcTabCtrlCompat& control) {
    HIMAGELIST handle = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, TCM_GETIMAGELIST, 0, 0));
    return LookupPermanentImageList(handle);
}

MfcImageListCompat* TabSetImageListInline(MfcTabCtrlCompat& control,
    const MfcImageListCompat* image_list) {
    HIMAGELIST handle = image_list == nullptr ? nullptr : image_list->handle;
    HIMAGELIST old = reinterpret_cast<HIMAGELIST>(send_control_message(control,
        TCM_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(handle)));
    return LookupPermanentImageList(old);
}

int TabGetItemCountInline(MfcTabCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        TCM_GETITEMCOUNT, 0, 0));
}

bool TabGetItemRawInline(MfcTabCtrlCompat& control, int item,
    TCITEMA& tab_item) {
    return send_control_message(control, TCM_GETITEMA, item,
        reinterpret_cast<LPARAM>(&tab_item)) != 0;
}

bool TabSetItemRawInline(MfcTabCtrlCompat& control, int item,
    const TCITEMA& tab_item) {
    return send_control_message(control, TCM_SETITEMA, item,
        reinterpret_cast<LPARAM>(&tab_item)) != 0;
}

bool TabSetItemExtraInline(MfcTabCtrlCompat& control, int bytes) {
    return send_control_message(control, TCM_SETITEMEXTRA, bytes, 0) != 0;
}

void TabSetCurFocusInline(MfcTabCtrlCompat& control, int item) {
    send_control_message(control, TCM_SETCURFOCUS, item, 0);
}

int TabInsertItemRawInline(MfcTabCtrlCompat& control, int item,
    const TCITEMA& tab_item) {
    return static_cast<int>(send_control_message(control, TCM_INSERTITEMA,
        item, reinterpret_cast<LPARAM>(&tab_item)));
}

void TabInsertItemTextInline(MfcTabCtrlCompat& control, int item,
    const char* text) {
    TabInsertItemFull(control, item, TCIF_TEXT, text, 0, 0);
}

void TabInsertItemTextImageInline(MfcTabCtrlCompat& control, int item,
    const char* text, int image) {
    TabInsertItemFull(control, item, TCIF_TEXT | TCIF_IMAGE, text, image, 0);
}

bool TabDeleteItemInline(MfcTabCtrlCompat& control, int item) {
    return send_control_message(control, TCM_DELETEITEM, item, 0) != 0;
}

bool TabDeleteAllItemsInline(MfcTabCtrlCompat& control) {
    return send_control_message(control, TCM_DELETEALLITEMS, 0, 0) != 0;
}

bool TabGetItemRectInline(MfcTabCtrlCompat& control, int item, RECT& rect) {
    return send_control_message(control, TCM_GETITEMRECT, item,
        reinterpret_cast<LPARAM>(&rect)) != 0;
}

int TabGetCurSelInline(MfcTabCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TCM_GETCURSEL, 0, 0));
}

int TabSetCurSelInline(MfcTabCtrlCompat& control, int item) {
    return static_cast<int>(send_control_message(control, TCM_SETCURSEL, item, 0));
}

int TabHitTestInline(MfcTabCtrlCompat& control, TCHITTESTINFO& hit_test) {
    return static_cast<int>(send_control_message(control, TCM_HITTEST, 0,
        reinterpret_cast<LPARAM>(&hit_test)));
}

void TabAdjustRectInline(MfcTabCtrlCompat& control, bool larger, RECT& rect) {
    send_control_message(control, TCM_ADJUSTRECT, larger ? TRUE : FALSE,
        reinterpret_cast<LPARAM>(&rect));
}

SIZE TabSetItemSizeInline(MfcTabCtrlCompat& control, int width, int height) {
    const LRESULT previous = send_control_message(control, TCM_SETITEMSIZE, 0,
        MAKELPARAM(static_cast<WORD>(width), static_cast<WORD>(height)));
    SIZE size{};
    size.cx = LOWORD(previous);
    size.cy = HIWORD(previous);
    return size;
}

void TabRemoveImageInline(MfcTabCtrlCompat& control, int image) {
    send_control_message(control, TCM_REMOVEIMAGE, image, 0);
}

void TabSetPaddingInline(MfcTabCtrlCompat& control, int x, int y) {
    send_control_message(control, TCM_SETPADDING, 0,
        MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y)));
}

int TabGetRowCountInline(MfcTabCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TCM_GETROWCOUNT, 0, 0));
}

HWND TabGetToolTipsInline(MfcTabCtrlCompat& control) {
    return reinterpret_cast<HWND>(send_control_message(control,
        TCM_GETTOOLTIPS, 0, 0));
}

void TabSetToolTipsInline(MfcTabCtrlCompat& control, HWND tooltips) {
    send_control_message(control, TCM_SETTOOLTIPS,
        reinterpret_cast<WPARAM>(tooltips), 0);
}

int TabGetCurFocusInline(MfcTabCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TCM_GETCURFOCUS, 0, 0));
}

int TabSetMinTabWidthInline(MfcTabCtrlCompat& control, int width) {
    return static_cast<int>(send_control_message(control, TCM_SETMINTABWIDTH,
        0, width));
}

void TabDeselectAllInline(MfcTabCtrlCompat& control, bool exclude_focus) {
    send_control_message(control, TCM_DESELECTALL,
        exclude_focus ? TRUE : FALSE, 0);
}

bool TabHighlightItemInline(MfcTabCtrlCompat& control, int item,
    bool highlight) {
    return send_control_message(control, TCM_HIGHLIGHTITEM, item,
        MAKELPARAM(highlight ? TRUE : FALSE, 0)) != 0;
}

DWORD TabGetExtendedStyleInline(MfcTabCtrlCompat& control) {
    return static_cast<DWORD>(send_control_message(control,
        TCM_GETEXTENDEDSTYLE, 0, 0));
}

DWORD TabSetExtendedStyleInline(MfcTabCtrlCompat& control, DWORD mask,
    DWORD style) {
    return static_cast<DWORD>(send_control_message(control,
        TCM_SETEXTENDEDSTYLE, mask, style));
}

bool CreateAnimateCtrl(MfcAnimateCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id) {
    return create_common_control(control, ICC_ANIMATE_CLASS, ANIMATE_CLASSA,
        style, bounds, parent, id) != nullptr;
}

void DestroyAnimateCtrl(MfcAnimateCtrlCompat& control) {
    destroy_control(control);
}

MfcAnimateCtrlCompat& ConstructAnimateCtrl(MfcAnimateCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

bool AnimateOpenFileInline(MfcAnimateCtrlCompat& control, const char* path) {
    return send_control_message(control, ACM_OPENA, 0,
        reinterpret_cast<LPARAM>(path)) != 0;
}

bool AnimateOpenResourceInline(MfcAnimateCtrlCompat& control,
    UINT resource_id) {
    return send_control_message(control, ACM_OPENA, 0,
        reinterpret_cast<LPARAM>(MAKEINTRESOURCEA(resource_id))) != 0;
}

bool AnimatePlayInline(MfcAnimateCtrlCompat& control, UINT repeat_count,
    UINT from_frame, UINT to_frame) {
    return send_control_message(control, ACM_PLAY, repeat_count,
        MAKELPARAM(static_cast<WORD>(from_frame),
            static_cast<WORD>(to_frame))) != 0;
}

bool AnimateStopInline(MfcAnimateCtrlCompat& control) {
    return send_control_message(control, ACM_STOP, 0, 0) != 0;
}

bool AnimateCloseInline(MfcAnimateCtrlCompat& control) {
    return send_control_message(control, ACM_OPENA, 0, 0) != 0;
}

bool AnimateSeekInline(MfcAnimateCtrlCompat& control, UINT frame) {
    return send_control_message(control, ACM_PLAY, 0,
        MAKELPARAM(static_cast<WORD>(frame), static_cast<WORD>(frame))) != 0;
}

void DestroyRichEditCtrl(MfcRichEditCtrlCompat& control) {
    destroy_control(control);
}

MfcRichEditCtrlCompat& ConstructRichEditCtrl(MfcRichEditCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

bool RichEditCanUndoInline(MfcRichEditCtrlCompat& control) {
    return send_control_message(control, EM_CANUNDO, 0, 0) != 0;
}

int RichEditGetLineCountInline(MfcRichEditCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, EM_GETLINECOUNT, 0, 0));
}

bool RichEditGetModifyInline(MfcRichEditCtrlCompat& control) {
    return send_control_message(control, EM_GETMODIFY, 0, 0) != 0;
}

void RichEditSetModifyInline(MfcRichEditCtrlCompat& control, bool modified) {
    send_control_message(control, EM_SETMODIFY, modified ? TRUE : FALSE, 0);
}

void RichEditGetRectInline(MfcRichEditCtrlCompat& control, RECT& rect) {
    send_control_message(control, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rect));
}

POINT RichEditPosFromCharInline(MfcRichEditCtrlCompat& control,
    LONG char_index) {
    POINT point{};
    send_control_message(control, EM_POSFROMCHAR,
        reinterpret_cast<WPARAM>(&point), char_index);
    return point;
}

DWORD RichEditSetOptionsInline(MfcRichEditCtrlCompat& control,
    WORD operation, DWORD options) {
    return static_cast<DWORD>(send_control_message(control, EM_SETOPTIONS,
        operation, options));
}

void RichEditEmptyUndoBufferInline(MfcRichEditCtrlCompat& control) {
    send_control_message(control, EM_EMPTYUNDOBUFFER, 0, 0);
}

void RichEditReplaceSelectionInline(MfcRichEditCtrlCompat& control,
    const char* text, bool can_undo) {
    send_control_message(control, EM_REPLACESEL, can_undo ? TRUE : FALSE,
        reinterpret_cast<LPARAM>(text == nullptr ? "" : text));
}

void RichEditSetRectInline(MfcRichEditCtrlCompat& control, const RECT& rect) {
    send_control_message(control, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rect));
}

bool RichEditUndoInline(MfcRichEditCtrlCompat& control) {
    return send_control_message(control, EM_UNDO, 0, 0) != 0;
}

void RichEditClearInline(MfcRichEditCtrlCompat& control) {
    send_control_message(control, WM_CLEAR, 0, 0);
}

void RichEditCopyInline(MfcRichEditCtrlCompat& control) {
    send_control_message(control, WM_COPY, 0, 0);
}

void RichEditCutInline(MfcRichEditCtrlCompat& control) {
    send_control_message(control, WM_CUT, 0, 0);
}

void RichEditPasteInline(MfcRichEditCtrlCompat& control) {
    send_control_message(control, WM_PASTE, 0, 0);
}

bool RichEditSetReadOnlyInline(MfcRichEditCtrlCompat& control, bool read_only) {
    return send_control_message(control, EM_SETREADONLY,
        read_only ? TRUE : FALSE, 0) != 0;
}

int RichEditGetFirstVisibleLineInline(MfcRichEditCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        EM_GETFIRSTVISIBLELINE, 0, 0));
}

bool RichEditDisplayBandInline(MfcRichEditCtrlCompat& control, RECT& rect) {
    return send_control_message(control, EM_DISPLAYBAND, 0,
        reinterpret_cast<LPARAM>(&rect)) != 0;
}

void RichEditExGetSelInline(MfcRichEditCtrlCompat& control,
    CHARRANGE& range) {
    send_control_message(control, EM_EXGETSEL, 0,
        reinterpret_cast<LPARAM>(&range));
}

void RichEditExLimitTextInline(MfcRichEditCtrlCompat& control, DWORD limit) {
    send_control_message(control, EM_EXLIMITTEXT, 0, limit);
}

LONG RichEditExLineFromCharInline(MfcRichEditCtrlCompat& control,
    LONG char_index) {
    return static_cast<LONG>(send_control_message(control,
        EM_EXLINEFROMCHAR, 0, char_index));
}

void RichEditExSetSelInline(MfcRichEditCtrlCompat& control,
    const CHARRANGE& range) {
    send_control_message(control, EM_EXSETSEL, 0,
        reinterpret_cast<LPARAM>(&range));
}

LONG RichEditFindTextExInline(MfcRichEditCtrlCompat& control, DWORD flags,
    FINDTEXTEXA& find) {
    return static_cast<LONG>(send_control_message(control, EM_FINDTEXTEX,
        flags, reinterpret_cast<LPARAM>(&find)));
}

LONG RichEditFormatRangeInline(MfcRichEditCtrlCompat& control, bool render,
    FORMATRANGE& format_range) {
    return static_cast<LONG>(send_control_message(control, EM_FORMATRANGE,
        render ? TRUE : FALSE, reinterpret_cast<LPARAM>(&format_range)));
}

DWORD RichEditGetEventMaskInline(MfcRichEditCtrlCompat& control) {
    return static_cast<DWORD>(send_control_message(control,
        EM_GETEVENTMASK, 0, 0));
}

DWORD RichEditGetLimitTextInline(MfcRichEditCtrlCompat& control) {
    return static_cast<DWORD>(send_control_message(control,
        EM_GETLIMITTEXT, 0, 0));
}

int RichEditGetSelTextInline(MfcRichEditCtrlCompat& control, char* text) {
    return static_cast<int>(send_control_message(control, EM_GETSELTEXT, 0,
        reinterpret_cast<LPARAM>(text)));
}

void RichEditHideSelectionInline(MfcRichEditCtrlCompat& control, bool hide,
    bool change_style) {
    send_control_message(control, EM_HIDESELECTION, hide ? TRUE : FALSE,
        change_style ? TRUE : FALSE);
}

void RichEditRequestResizeInline(MfcRichEditCtrlCompat& control) {
    send_control_message(control, EM_REQUESTRESIZE, 0, 0);
}

WORD RichEditSelectionTypeInline(MfcRichEditCtrlCompat& control) {
    return static_cast<WORD>(send_control_message(control,
        EM_SELECTIONTYPE, 0, 0));
}

COLORREF RichEditSetBackgroundColorInline(MfcRichEditCtrlCompat& control,
    bool use_system, COLORREF color) {
    return static_cast<COLORREF>(send_control_message(control,
        EM_SETBKGNDCOLOR, use_system ? TRUE : FALSE, color));
}

DWORD RichEditSetEventMaskInline(MfcRichEditCtrlCompat& control, DWORD mask) {
    return static_cast<DWORD>(send_control_message(control, EM_SETEVENTMASK,
        0, mask));
}

bool RichEditSetOleCallbackInline(MfcRichEditCtrlCompat& control,
    void* callback) {
    return send_control_message(control, EM_SETOLECALLBACK, 0,
        reinterpret_cast<LPARAM>(callback)) != 0;
}

void RichEditSetTargetDeviceInline(MfcRichEditCtrlCompat& control,
    HDC target, LONG line_width) {
    send_control_message(control, EM_SETTARGETDEVICE,
        reinterpret_cast<WPARAM>(target), line_width);
}

void RichEditSetTargetDeviceFromHandleInline(MfcRichEditCtrlCompat& control,
    HDC target, LONG line_width) {
    RichEditSetTargetDeviceInline(control, target, line_width);
}

DWORD RichEditStreamInInline(MfcRichEditCtrlCompat& control, DWORD format,
    EDITSTREAM& stream) {
    return static_cast<DWORD>(send_control_message(control, EM_STREAMIN,
        format, reinterpret_cast<LPARAM>(&stream)));
}

DWORD RichEditStreamOutInline(MfcRichEditCtrlCompat& control, DWORD format,
    EDITSTREAM& stream) {
    return static_cast<DWORD>(send_control_message(control, EM_STREAMOUT,
        format, reinterpret_cast<LPARAM>(&stream)));
}

int RichEditGetWindowTextLengthInline(MfcRichEditCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        WM_GETTEXTLENGTH, 0, 0));
}

MfcDragListBoxCompat* DeleteDragListBoxScalarDtor(MfcDragListBoxCompat* box,
    unsigned flags) {
    return destroy_scalar_dtor(box, flags,
        [](MfcDragListBoxCompat& value) { destroy_control(value); });
}

MfcToolbarCtrlCompat* DeleteToolbarCtrlScalarDtor(MfcToolbarCtrlCompat* control,
    unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyToolbarCtrl);
}

MfcStatusBarCtrlCompat* DeleteStatusBarCtrlScalarDtor(
    MfcStatusBarCtrlCompat* control, unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyStatusBarCtrl);
}

MfcListCtrlCompat* DeleteListCtrlScalarDtor(MfcListCtrlCompat* control,
    unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyListCtrl);
}

MfcTreeCtrlCompat* DeleteTreeCtrlScalarDtor(MfcTreeCtrlCompat* control,
    unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyTreeCtrl);
}

MfcSpinButtonCtrlCompat* DeleteSpinButtonCtrlScalarDtor(
    MfcSpinButtonCtrlCompat* control, unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroySpinButtonCtrl);
}

MfcSliderCtrlCompat* DeleteSliderCtrlScalarDtor(MfcSliderCtrlCompat* control,
    unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroySliderCtrl);
}

MfcProgressCtrlCompat* DeleteProgressCtrlScalarDtor(
    MfcProgressCtrlCompat* control, unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyProgressCtrl);
}

MfcHeaderCtrlCompat* DeleteHeaderCtrlScalarDtor(MfcHeaderCtrlCompat* control,
    unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyHeaderCtrl);
}

MfcHotKeyCtrlCompat* DeleteHotKeyCtrlScalarDtor(MfcHotKeyCtrlCompat* control,
    unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyHotKeyCtrl);
}

MfcTabCtrlCompat* DeleteTabCtrlScalarDtor(MfcTabCtrlCompat* control,
    unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyTabCtrl);
}

MfcAnimateCtrlCompat* DeleteAnimateCtrlScalarDtor(
    MfcAnimateCtrlCompat* control, unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyAnimateCtrl);
}

MfcRichEditCtrlCompat* DeleteRichEditCtrlScalarDtor(
    MfcRichEditCtrlCompat* control, unsigned flags) {
    return destroy_scalar_dtor(control, flags, DestroyRichEditCtrl);
}

void* DeleteMenuScalarDtor(void* menu, unsigned flags) {
    if ((flags & 1) != 0) {
        ::operator delete(menu);
    }
    return menu;
}

void* EnsureTempImageListHandleMap(bool create) {
    if (g_image_list_map == nullptr && create) {
        g_image_list_map = new ImageListHandleMap();
    }
    return g_image_list_map;
}

MfcImageListCompat* LookupPermanentImageList(HIMAGELIST handle) {
    auto* map = static_cast<ImageListHandleMap*>(EnsureTempImageListHandleMap(true));
    if (map == nullptr || handle == nullptr) {
        return nullptr;
    }
    const auto found = map->permanent.find(handle);
    if (found == map->permanent.end()) {
        return nullptr;
    }
    return found->second;
}

MfcImageListCompat* LookupTemporaryImageList(HIMAGELIST handle) {
    auto* map = static_cast<ImageListHandleMap*>(EnsureTempImageListHandleMap(false));
    if (map == nullptr || handle == nullptr) {
        return nullptr;
    }
    const auto found = map->temporary.find(handle);
    if (found == map->temporary.end()) {
        return nullptr;
    }
    return &found->second;
}

void DeleteTempImageListHandleMapEntries() {
    auto* map = static_cast<ImageListHandleMap*>(
        EnsureTempImageListHandleMap(false));
    if (map == nullptr) {
        return;
    }
    map->temporary.clear();
}

bool AttachImageListHandle(MfcImageListCompat& image_list, HIMAGELIST handle) {
    if (image_list.handle != nullptr || handle == nullptr) {
        return false;
    }
    if (LookupTemporaryImageList(handle) != nullptr) {
        return false;
    }
    auto* map = static_cast<ImageListHandleMap*>(EnsureTempImageListHandleMap(true));
    if (map == nullptr) {
        return false;
    }
    image_list.handle = handle;
    image_list.owns_handle = true;
    map->permanent[handle] = &image_list;
    return true;
}

bool CreateImageListCompat(MfcImageListCompat& image_list, int width,
    int height, UINT flags, int initial_count, int grow_count) {
    return AttachImageListHandle(image_list,
        ImageList_Create(width, height, flags, initial_count, grow_count));
}

bool LoadImageListResourceId(MfcImageListCompat& image_list, UINT resource_id,
    int width, int grow_count, COLORREF mask) {
    HINSTANCE module = GetModuleHandleA(nullptr);
    return AttachImageListHandle(image_list, ImageList_LoadImageA(module,
        MAKEINTRESOURCEA(resource_id), width, grow_count, mask, IMAGE_BITMAP, 0));
}

bool LoadImageListResourceName(MfcImageListCompat& image_list,
    const char* resource_name, int width, int grow_count, COLORREF mask) {
    HINSTANCE module = GetModuleHandleA(nullptr);
    return AttachImageListHandle(image_list, ImageList_LoadImageA(module,
        resource_name, width, grow_count, mask, IMAGE_BITMAP, 0));
}

bool ReadImageListFromArchive(MfcImageListCompat& image_list, IStream* stream) {
    if (image_list.handle != nullptr || stream == nullptr) {
        return false;
    }
    using ImageListReadFn = HIMAGELIST (WINAPI*)(IStream*);
    HMODULE module = GetModuleHandleA("comctl32.dll");
    if (module == nullptr) {
        module = LoadLibraryA("comctl32.dll");
    }
    auto* image_list_read = module == nullptr ? nullptr
        : reinterpret_cast<ImageListReadFn>(
            GetProcAddress(module, "ImageList_Read"));
    if (image_list_read == nullptr) {
        return false;
    }
    HIMAGELIST handle = image_list_read(stream);
    return AttachImageListHandle(image_list, handle);
}

bool WriteImageListToArchive(const MfcImageListCompat& image_list,
    IStream* stream) {
    if (image_list.handle == nullptr || stream == nullptr) {
        return false;
    }
    using ImageListWriteFn = BOOL (WINAPI*)(HIMAGELIST, IStream*);
    HMODULE module = GetModuleHandleA("comctl32.dll");
    if (module == nullptr) {
        module = LoadLibraryA("comctl32.dll");
    }
    auto* image_list_write = module == nullptr ? nullptr
        : reinterpret_cast<ImageListWriteFn>(
            GetProcAddress(module, "ImageList_Write"));
    return image_list_write != nullptr &&
        image_list_write(image_list.handle, stream) != FALSE;
}

void AssertValidImageListWrapper(const MfcImageListCompat& image_list) {
    if (image_list.handle != nullptr &&
        LookupPermanentImageList(image_list.handle) != &image_list &&
        LookupTemporaryImageList(image_list.handle) == nullptr) {
        AfxTraceOutput("CImageList handle is not registered in the handle map.\n");
    }
}

int DragListBoxItemFromPoint(MfcDragListBoxCompat& box, POINT point,
    bool allow_outside) {
    if (!has_window(box)) {
        return -1;
    }
    return LBItemFromPt(box.window, point, allow_outside ? TRUE : FALSE);
}

MfcToolbarCtrlCompat& ConstructToolbarCtrl(MfcToolbarCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

bool ToolbarEnableButton(MfcToolbarCtrlCompat& control, int command_id,
    bool enable) {
    return send_control_message(control, TB_ENABLEBUTTON, command_id,
        MAKELPARAM(enable ? TRUE : FALSE, 0)) != 0;
}

bool ToolbarCheckButton(MfcToolbarCtrlCompat& control, int command_id,
    bool check) {
    return send_control_message(control, TB_CHECKBUTTON, command_id,
        MAKELPARAM(check ? TRUE : FALSE, 0)) != 0;
}

bool ToolbarPressButton(MfcToolbarCtrlCompat& control, int command_id,
    bool press) {
    return send_control_message(control, TB_PRESSBUTTON, command_id,
        MAKELPARAM(press ? TRUE : FALSE, 0)) != 0;
}

bool ToolbarHideButton(MfcToolbarCtrlCompat& control, int command_id,
    bool hide) {
    return send_control_message(control, TB_HIDEBUTTON, command_id,
        MAKELPARAM(hide ? TRUE : FALSE, 0)) != 0;
}

bool ToolbarIndeterminateButton(MfcToolbarCtrlCompat& control, int command_id,
    bool indeterminate) {
    return send_control_message(control, TB_INDETERMINATE, command_id,
        MAKELPARAM(indeterminate ? TRUE : FALSE, 0)) != 0;
}

bool ToolbarIsButtonEnabled(MfcToolbarCtrlCompat& control, int command_id) {
    return send_control_message(control, TB_ISBUTTONENABLED, command_id, 0) != 0;
}

bool ToolbarIsButtonChecked(MfcToolbarCtrlCompat& control, int command_id) {
    return send_control_message(control, TB_ISBUTTONCHECKED, command_id, 0) != 0;
}

bool ToolbarIsButtonPressed(MfcToolbarCtrlCompat& control, int command_id) {
    return send_control_message(control, TB_ISBUTTONPRESSED, command_id, 0) != 0;
}

bool ToolbarIsButtonHidden(MfcToolbarCtrlCompat& control, int command_id) {
    return send_control_message(control, TB_ISBUTTONHIDDEN, command_id, 0) != 0;
}

bool ToolbarIsButtonIndeterminate(MfcToolbarCtrlCompat& control, int command_id) {
    return send_control_message(control, TB_ISBUTTONINDETERMINATE,
        command_id, 0) != 0;
}

void ToolbarSetState(MfcToolbarCtrlCompat& control, int command_id, UINT state) {
    send_control_message(control, TB_SETSTATE, command_id,
        MAKELPARAM(static_cast<WORD>(state), 0));
}

UINT ToolbarGetState(MfcToolbarCtrlCompat& control, int command_id) {
    return static_cast<UINT>(send_control_message(control, TB_GETSTATE,
        command_id, 0));
}

LRESULT ToolbarAddButtonsInline(MfcToolbarCtrlCompat& control, int count,
    const TBBUTTON* buttons) {
    return ToolbarAddButtons(control, count, buttons);
}

LRESULT ToolbarInsertButtonInline(MfcToolbarCtrlCompat& control, int index,
    const TBBUTTON& button) {
    return ToolbarInsertButton(control, index, button);
}

bool ToolbarDeleteButton(MfcToolbarCtrlCompat& control, int index) {
    return send_control_message(control, TB_DELETEBUTTON, index, 0) != 0;
}

bool ToolbarGetButton(MfcToolbarCtrlCompat& control, int index,
    TBBUTTON& button) {
    return send_control_message(control, TB_GETBUTTON, index,
        reinterpret_cast<LPARAM>(&button)) != 0;
}

int ToolbarGetButtonCount(MfcToolbarCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TB_BUTTONCOUNT, 0, 0));
}

int ToolbarCommandToIndex(MfcToolbarCtrlCompat& control, int command_id) {
    return static_cast<int>(send_control_message(control, TB_COMMANDTOINDEX,
        command_id, 0));
}

void ToolbarCustomize(MfcToolbarCtrlCompat& control) {
    send_control_message(control, TB_CUSTOMIZE, 0, 0);
}

int ToolbarAddString(MfcToolbarCtrlCompat& control, const char* text) {
    return static_cast<int>(send_control_message(control, TB_ADDSTRINGA, 0,
        reinterpret_cast<LPARAM>(text == nullptr ? "" : text)));
}

bool ToolbarGetItemRect(MfcToolbarCtrlCompat& control, int index, RECT& rect) {
    return send_control_message(control, TB_GETITEMRECT, index,
        reinterpret_cast<LPARAM>(&rect)) != 0;
}

void ToolbarButtonStructSizeInline(MfcToolbarCtrlCompat& control,
    std::size_t bytes) {
    send_control_message(control, TB_BUTTONSTRUCTSIZE, bytes, 0);
}

void ToolbarSetButtonSizeInline(MfcToolbarCtrlCompat& control, int width,
    int height) {
    ToolbarSetButtonSize(control, width, height);
}

void ToolbarSetBitmapSizeInline(MfcToolbarCtrlCompat& control, int width,
    int height) {
    ToolbarSetBitmapSize(control, width, height);
}

void ToolbarAutoSize(MfcToolbarCtrlCompat& control) {
    send_control_message(control, TB_AUTOSIZE, 0, 0);
}

HWND ToolbarGetToolTips(MfcToolbarCtrlCompat& control) {
    return reinterpret_cast<HWND>(send_control_message(control,
        TB_GETTOOLTIPS, 0, 0));
}

void ToolbarSetToolTips(MfcToolbarCtrlCompat& control, HWND tooltip) {
    send_control_message(control, TB_SETTOOLTIPS,
        reinterpret_cast<WPARAM>(tooltip), 0);
}

void ToolbarSetParentWindow(MfcToolbarCtrlCompat& control, HWND parent) {
    send_control_message(control, TB_SETPARENT,
        reinterpret_cast<WPARAM>(parent), 0);
}

void ToolbarSetRows(MfcToolbarCtrlCompat& control, int rows,
    bool larger, RECT* result) {
    send_control_message(control, TB_SETROWS,
        MAKEWPARAM(static_cast<WORD>(rows), larger ? TRUE : FALSE),
        reinterpret_cast<LPARAM>(result));
}

int ToolbarGetRows(MfcToolbarCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, TB_GETROWS, 0, 0));
}

bool ToolbarSetCommandId(MfcToolbarCtrlCompat& control, int index,
    int command_id) {
    return send_control_message(control, TB_SETCMDID, index, command_id) != 0;
}

DWORD ToolbarGetBitmapFlags(MfcToolbarCtrlCompat& control) {
    return static_cast<DWORD>(send_control_message(control,
        TB_GETBITMAPFLAGS, 0, 0));
}

int ToolbarHitTest(MfcToolbarCtrlCompat& control, POINT& point) {
    return static_cast<int>(send_control_message(control, TB_HITTEST, 0,
        reinterpret_cast<LPARAM>(&point)));
}

DWORD ToolbarGetExtendedStyle(MfcToolbarCtrlCompat& control) {
    return static_cast<DWORD>(send_control_message(control,
        TB_GETEXTENDEDSTYLE, 0, 0));
}

DWORD ToolbarSetExtendedStyle(MfcToolbarCtrlCompat& control, DWORD style) {
    return static_cast<DWORD>(send_control_message(control,
        TB_SETEXTENDEDSTYLE, 0, style));
}

COLORREF ToolbarGetInsertMarkColor(MfcToolbarCtrlCompat& control) {
    return static_cast<COLORREF>(send_control_message(control,
        TB_GETINSERTMARKCOLOR, 0, 0));
}

COLORREF ToolbarSetInsertMarkColor(MfcToolbarCtrlCompat& control,
    COLORREF color) {
    return static_cast<COLORREF>(send_control_message(control,
        TB_SETINSERTMARKCOLOR, 0, color));
}

MfcStatusBarCtrlCompat& ConstructStatusBarCtrl(MfcStatusBarCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

bool StatusBarSetText(MfcStatusBarCtrlCompat& control, int pane, UINT type,
    const char* text) {
    return send_control_message(control, SB_SETTEXTA,
        static_cast<WPARAM>(pane) | type,
        reinterpret_cast<LPARAM>(text == nullptr ? "" : text)) != 0;
}

bool StatusBarSetParts(MfcStatusBarCtrlCompat& control, int part_count,
    const int* right_edges) {
    return send_control_message(control, SB_SETPARTS, part_count,
        reinterpret_cast<LPARAM>(right_edges)) != 0;
}

int StatusBarGetParts(MfcStatusBarCtrlCompat& control, int part_count,
    int* right_edges) {
    return static_cast<int>(send_control_message(control, SB_GETPARTS,
        part_count, reinterpret_cast<LPARAM>(right_edges)));
}

bool StatusBarGetBordersRaw(MfcStatusBarCtrlCompat& control, int* values) {
    return send_control_message(control, SB_GETBORDERS, 0,
        reinterpret_cast<LPARAM>(values)) != 0;
}

void StatusBarSetMinHeight(MfcStatusBarCtrlCompat& control, int height) {
    send_control_message(control, SB_SETMINHEIGHT, height, 0);
}

void StatusBarSetSimple(MfcStatusBarCtrlCompat& control, bool simple) {
    send_control_message(control, SB_SIMPLE, simple ? TRUE : FALSE, 0);
}

bool StatusBarGetRect(MfcStatusBarCtrlCompat& control, int pane, RECT& rect) {
    return send_control_message(control, SB_GETRECT, pane,
        reinterpret_cast<LPARAM>(&rect)) != 0;
}

MfcListCtrlCompat& ConstructListCtrl(MfcListCtrlCompat& control) {
    control.window = nullptr;
    control.style = 0;
    return control;
}

COLORREF ListCtrlGetBkColor(MfcListCtrlCompat& control) {
    return static_cast<COLORREF>(send_control_message(control,
        LVM_GETBKCOLOR, 0, 0));
}

bool ListCtrlSetBkColor(MfcListCtrlCompat& control, COLORREF color) {
    return send_control_message(control, LVM_SETBKCOLOR, 0, color) != 0;
}

MfcImageListCompat* ListCtrlGetImageList(MfcListCtrlCompat& control,
    int image_list_type) {
    HIMAGELIST handle = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, LVM_GETIMAGELIST, image_list_type, 0));
    return LookupPermanentImageList(handle);
}

MfcImageListCompat* ListCtrlSetImageListInline(MfcListCtrlCompat& control,
    HIMAGELIST image_list, int image_list_type) {
    HIMAGELIST old = reinterpret_cast<HIMAGELIST>(
        send_control_message(control, LVM_SETIMAGELIST, image_list_type,
            reinterpret_cast<LPARAM>(image_list)));
    return LookupPermanentImageList(old);
}

int ListCtrlGetItemCount(MfcListCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        LVM_GETITEMCOUNT, 0, 0));
}

bool ListCtrlGetItemRaw(MfcListCtrlCompat& control, LVITEMA& item) {
    return send_control_message(control, LVM_GETITEMA, 0,
        reinterpret_cast<LPARAM>(&item)) != 0;
}

bool ListCtrlSetItemRaw(MfcListCtrlCompat& control, const LVITEMA& item) {
    return send_control_message(control, LVM_SETITEMA, 0,
        reinterpret_cast<LPARAM>(&item)) != 0;
}

bool ListCtrlSetItemDataInline(MfcListCtrlCompat& control, int item,
    LPARAM data) {
    LVITEMA value{};
    value.mask = LVIF_PARAM;
    value.iItem = item;
    value.lParam = data;
    return ListCtrlSetItemRaw(control, value);
}

int ListCtrlInsertItemRaw(MfcListCtrlCompat& control, const LVITEMA& item) {
    return static_cast<int>(send_control_message(control, LVM_INSERTITEMA, 0,
        reinterpret_cast<LPARAM>(&item)));
}

int ListCtrlInsertItemTextInline(MfcListCtrlCompat& control, int item,
    const char* text) {
    LVITEMA value{};
    value.mask = LVIF_TEXT;
    value.iItem = item;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    return ListCtrlInsertItemRaw(control, value);
}

int ListCtrlInsertItemImageInline(MfcListCtrlCompat& control, int item,
    const char* text, int image) {
    LVITEMA value{};
    value.mask = LVIF_TEXT | LVIF_IMAGE;
    value.iItem = item;
    value.pszText = const_cast<char*>(text == nullptr ? "" : text);
    value.iImage = image;
    return ListCtrlInsertItemRaw(control, value);
}

bool ListCtrlDeleteItem(MfcListCtrlCompat& control, int item) {
    return send_control_message(control, LVM_DELETEITEM, item, 0) != 0;
}

bool ListCtrlDeleteAllItems(MfcListCtrlCompat& control) {
    return send_control_message(control, LVM_DELETEALLITEMS, 0, 0) != 0;
}

UINT ListCtrlGetCallbackMask(MfcListCtrlCompat& control) {
    return static_cast<UINT>(send_control_message(control,
        LVM_GETCALLBACKMASK, 0, 0));
}

bool ListCtrlSetCallbackMask(MfcListCtrlCompat& control, UINT mask) {
    return send_control_message(control, LVM_SETCALLBACKMASK, mask, 0) != 0;
}

int ListCtrlGetNextItem(MfcListCtrlCompat& control, int item, UINT flags) {
    return static_cast<int>(send_control_message(control, LVM_GETNEXTITEM,
        item, MAKELPARAM(static_cast<WORD>(flags), 0)));
}

int ListCtrlGetFirstSelectedItemPosition(MfcListCtrlCompat& control) {
    return ListCtrlGetNextItem(control, -1, LVNI_SELECTED) + 1;
}

int ListCtrlGetNextSelectedItem(MfcListCtrlCompat& control, int& position) {
    const int current = position;
    const int next = ListCtrlGetNextItem(control, current - 1, LVNI_SELECTED);
    position = next + 1;
    return current - 1;
}

int ListCtrlFindItem(MfcListCtrlCompat& control, int start_after,
    const LVFINDINFOA& find_info) {
    return static_cast<int>(send_control_message(control, LVM_FINDITEMA,
        start_after, reinterpret_cast<LPARAM>(&find_info)));
}

int ListCtrlHitTest(MfcListCtrlCompat& control, LVHITTESTINFO& hit_test) {
    return static_cast<int>(send_control_message(control, LVM_HITTEST, 0,
        reinterpret_cast<LPARAM>(&hit_test)));
}

bool ListCtrlSetItemPosition32Inline(MfcListCtrlCompat& control, int item,
    POINT point) {
    return send_control_message(control, LVM_SETITEMPOSITION32, item,
        reinterpret_cast<LPARAM>(&point)) != 0;
}

bool ListCtrlGetItemPositionInline(MfcListCtrlCompat& control, int item,
    POINT& point) {
    return send_control_message(control, LVM_GETITEMPOSITION, item,
        reinterpret_cast<LPARAM>(&point)) != 0;
}

int ListCtrlGetStringWidth(MfcListCtrlCompat& control, const char* text) {
    return static_cast<int>(send_control_message(control, LVM_GETSTRINGWIDTHA,
        0, reinterpret_cast<LPARAM>(text == nullptr ? "" : text)));
}

bool ListCtrlEnsureVisible(MfcListCtrlCompat& control, int item,
    bool partial_ok) {
    return send_control_message(control, LVM_ENSUREVISIBLE, item,
        MAKELPARAM(partial_ok ? TRUE : FALSE, 0)) != 0;
}

bool ListCtrlScroll(MfcListCtrlCompat& control, int dx, int dy) {
    return send_control_message(control, LVM_SCROLL, dx, dy) != 0;
}

bool ListCtrlRedrawItems(MfcListCtrlCompat& control, int first, int last) {
    return send_control_message(control, LVM_REDRAWITEMS, first, last) != 0;
}

bool ListCtrlArrange(MfcListCtrlCompat& control, UINT code) {
    return send_control_message(control, LVM_ARRANGE, code, 0) != 0;
}

HWND ListCtrlEditLabel(MfcListCtrlCompat& control, int item) {
    return reinterpret_cast<HWND>(send_control_message(control,
        LVM_EDITLABELA, item, 0));
}

HWND ListCtrlGetEditControl(MfcListCtrlCompat& control) {
    return reinterpret_cast<HWND>(send_control_message(control,
        LVM_GETEDITCONTROL, 0, 0));
}

bool ListCtrlGetColumnInline(MfcListCtrlCompat& control, int column,
    LVCOLUMNA& value) {
    return send_control_message(control, LVM_GETCOLUMNA, column,
        reinterpret_cast<LPARAM>(&value)) != 0;
}

bool ListCtrlSetColumnInline(MfcListCtrlCompat& control, int column,
    const LVCOLUMNA& value) {
    return send_control_message(control, LVM_SETCOLUMNA, column,
        reinterpret_cast<LPARAM>(&value)) != 0;
}

int ListCtrlInsertColumnInline(MfcListCtrlCompat& control, int column,
    const LVCOLUMNA& value) {
    return static_cast<int>(send_control_message(control, LVM_INSERTCOLUMNA,
        column, reinterpret_cast<LPARAM>(&value)));
}

bool ListCtrlDeleteColumnInline(MfcListCtrlCompat& control, int column) {
    return send_control_message(control, LVM_DELETECOLUMN, column, 0) != 0;
}

int ListCtrlGetColumnWidthInline(MfcListCtrlCompat& control, int column) {
    return static_cast<int>(send_control_message(control, LVM_GETCOLUMNWIDTH,
        column, 0));
}

bool ListCtrlSetColumnWidthInline(MfcListCtrlCompat& control, int column,
    int width) {
    return send_control_message(control, LVM_SETCOLUMNWIDTH, column,
        MAKELPARAM(static_cast<WORD>(width), 0)) != 0;
}

bool ListCtrlGetViewRectInline(MfcListCtrlCompat& control, RECT& rect) {
    return send_control_message(control, LVM_GETVIEWRECT, 0,
        reinterpret_cast<LPARAM>(&rect)) != 0;
}

COLORREF ListCtrlGetTextColorInline(MfcListCtrlCompat& control) {
    return static_cast<COLORREF>(send_control_message(control,
        LVM_GETTEXTCOLOR, 0, 0));
}

bool ListCtrlSetTextColorInline(MfcListCtrlCompat& control, COLORREF color) {
    return send_control_message(control, LVM_SETTEXTCOLOR, 0, color) != 0;
}

COLORREF ListCtrlGetTextBkColorInline(MfcListCtrlCompat& control) {
    return static_cast<COLORREF>(send_control_message(control,
        LVM_GETTEXTBKCOLOR, 0, 0));
}

bool ListCtrlSetTextBkColorInline(MfcListCtrlCompat& control, COLORREF color) {
    return send_control_message(control, LVM_SETTEXTBKCOLOR, 0, color) != 0;
}

int ListCtrlGetTopIndexInline(MfcListCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, LVM_GETTOPINDEX,
        0, 0));
}

int ListCtrlGetCountPerPageInline(MfcListCtrlCompat& control) {
    return static_cast<int>(send_control_message(control, LVM_GETCOUNTPERPAGE,
        0, 0));
}

bool ListCtrlGetOriginInline(MfcListCtrlCompat& control, POINT& point) {
    return send_control_message(control, LVM_GETORIGIN, 0,
        reinterpret_cast<LPARAM>(&point)) != 0;
}

bool ListCtrlUpdateInline(MfcListCtrlCompat& control, int item) {
    return send_control_message(control, LVM_UPDATE, item, 0) != 0;
}

bool ListCtrlSetItemStateRawInline(MfcListCtrlCompat& control, int item,
    LVITEMA& item_state) {
    return send_control_message(control, LVM_SETITEMSTATE, item,
        reinterpret_cast<LPARAM>(&item_state)) != 0;
}

UINT ListCtrlGetItemStateInline(MfcListCtrlCompat& control, int item,
    UINT mask) {
    return static_cast<UINT>(send_control_message(control, LVM_GETITEMSTATE,
        item, mask));
}

void ListCtrlSetItemCountInline(MfcListCtrlCompat& control, int count) {
    send_control_message(control, LVM_SETITEMCOUNT, count, 0);
}

bool ListCtrlSortItemsInline(MfcListCtrlCompat& control, PFNLVCOMPARE compare,
    LPARAM data) {
    return send_control_message(control, LVM_SORTITEMS,
        static_cast<WPARAM>(data), reinterpret_cast<LPARAM>(compare)) != 0;
}

int ListCtrlGetSelectedCountInline(MfcListCtrlCompat& control) {
    return static_cast<int>(send_control_message(control,
        LVM_GETSELECTEDCOUNT, 0, 0));
}

} // namespace ranker

#endif
