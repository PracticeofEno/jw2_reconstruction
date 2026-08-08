#include "ranker_figs_address_book.h"

#ifdef _WIN32

#include "ranker_connect_frontend.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_setup_data.h"
#include "ranker_trc.h"
#include "ranker_wizard_login.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kListBoxStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED;
constexpr DWORD kEditStyle = WS_CHILD | ES_AUTOHSCROLL;
constexpr COLORREF kFigsWhite = RGB(255, 255, 255);
constexpr COLORREF kFigsGray = RGB(200, 200, 200);
constexpr COLORREF kFigsBlack = RGB(0, 0, 0);
constexpr COLORREF kFigsSelectedBlue = RGB(0, 0, 255);

FigsState g_figs_state;
bool g_background_destructor_registered = false;
bool g_scroll_destructor_registered = false;
std::array<bool, 4> g_button_destructor_registered{};

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK figs_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleFigsWindowMessage(g_figs_state, hwnd, message, wparam, lparam);
}

LRESULT CALLBACK figs_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleFigsControlMessage(g_figs_state, hwnd, message, wparam, lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_global_background() {
    DestroyFigsBackgroundBitmap(g_figs_state);
}

void shutdown_global_scroll() {
    DestroyFigsScrollControl(g_figs_state);
}

void shutdown_global_add_button() {
    DestroyFigsAddButton(g_figs_state);
}

void shutdown_global_delete_button() {
    DestroyFigsDeleteButton(g_figs_state);
}

void shutdown_global_connect_button() {
    DestroyFigsConnectButton(g_figs_state);
}

void shutdown_global_exit_button() {
    DestroyFigsExitButton(g_figs_state);
}

FigsLayoutRect layout_at(const FigsState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return FigsLayoutRect{};
}

void assign_layout(FigsState& state, const FrontendLayoutRectTable& table) {
    state.layout.clear();
    if (table.rects == nullptr || table.count == 0) {
        return;
    }
    state.layout.reserve(table.count);
    for (u32 i = 0; i < table.count; ++i) {
        const FrontendLayoutRect& rect = table.rects[i];
        state.layout.push_back({rect.x, rect.y, rect.width, rect.height});
    }
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& target, const char* source) {
    target.fill(0);
    if (source != nullptr) {
        std::strncpy(target.data(), source, target.size() - 1);
    }
}

void clear_control(FigsTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void destroy_control(FigsTextControl& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    clear_control(control);
}

void subclass_text_control(FigsTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(figs_control_proc));
}

bool create_text_control(FigsTextControl& control, HWND parent, HINSTANCE instance,
    const char* class_name, DWORD style, int id, const FigsLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_text_control(control);
    return true;
}

void subclass_button(LegacyImageButtonControl& button) {
    if (button.window == nullptr) {
        return;
    }
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(button.window, GWLP_WNDPROC));
    SetWindowLongPtrA(button.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(figs_control_proc));
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const FigsLayoutRect& rect, u32 normal_record,
    u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    subclass_button(button);
    return true;
}

LegacyImageButtonControl* button_for_id(FigsState& state, int id) {
    switch (id) {
    case kFigsAddButtonId:
        return &state.add_button;
    case kFigsDeleteButtonId:
        return &state.delete_button;
    case kFigsConnectButtonId:
        return &state.connect_button;
    case kFigsCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

WNDPROC original_proc_for_id(FigsState& state, int id) {
    switch (id) {
    case kFigsListBoxId:
        return state.list_box.original_window_proc;
    case kFigsNameEditId:
        return state.name_edit.original_window_proc;
    case kFigsAddressEditId:
        return state.address_edit.original_window_proc;
    case kFigsScrollControlId:
        return state.scroll.original_window_proc;
    default:
        if (LegacyImageButtonControl* button = button_for_id(state, id)) {
            return button->original_window_proc;
        }
        return nullptr;
    }
}

bool entry_used(const FigsEntry& entry) {
    return entry.name[0] != '\0';
}

void update_scroll(FigsState& state, int top_index = -1) {
    if (state.list_box.window == nullptr) {
        return;
    }
    const int count = static_cast<int>(
        SendMessageA(state.list_box.window, LB_GETCOUNT, 0, 0));
    if (top_index < 0) {
        top_index = static_cast<int>(
            SendMessageA(state.list_box.window, LB_GETTOPINDEX, 0, 0));
    }
    const int max_top = std::max(0, count - state.visible_rows);
    SetLegacyCustomScrollControlVisible(state.scroll, count > state.visible_rows);
    SetLegacyCustomScrollControlRange(state.scroll, 0, max_top, true);
    SetLegacyCustomScrollControlValue(state.scroll, std::min(top_index, max_top), true);
}

void add_listbox_entry(FigsState& state, FigsEntry& entry) {
    if (state.list_box.window == nullptr) {
        return;
    }
    const LRESULT index = SendMessageA(state.list_box.window, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(entry.name.data()));
    if (index != LB_ERR && index != LB_ERRSPACE) {
        SendMessageA(state.list_box.window, LB_SETITEMDATA,
            static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&entry));
    }
}

FigsEntry* selected_entry(FigsState& state) {
    if (state.list_box.window == nullptr) {
        return nullptr;
    }
    const LRESULT selected = SendMessageA(state.list_box.window, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return nullptr;
    }
    const LRESULT data = SendMessageA(state.list_box.window, LB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    if (data == LB_ERR || data == 0) {
        return nullptr;
    }
    return reinterpret_cast<FigsEntry*>(data);
}

void play_click_sound(FigsState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

void import_setup_entries(FigsState& state) {
    LoadDefaultSetupDataBuffer();
    for (std::size_t index = 0; index < state.entries.size(); ++index) {
        FigsEntry& entry = state.entries[index];
        entry = FigsEntry{};
        const std::size_t base =
            kSetupFigsEntryOffset + index * kSetupFigsEntryStride;
        ImportSetupText(entry.name, base + kSetupFigsNameOffset);
        ImportSetupText(entry.address, base + kSetupFigsAddressOffset);
    }
}

void export_setup_entries(const FigsState& state) {
    LoadDefaultSetupDataBuffer();
    for (std::size_t index = 0; index < state.entries.size(); ++index) {
        const FigsEntry& entry = state.entries[index];
        const std::size_t base =
            kSetupFigsEntryOffset + index * kSetupFigsEntryStride;
        ExportSetupText(base + kSetupFigsNameOffset, entry.name);
        ExportSetupText(base + kSetupFigsAddressOffset, entry.address);
    }
}

void write_setup_data(FigsState& state) {
    if (state.callbacks.write_setup_data != nullptr) {
        state.callbacks.write_setup_data(state);
        return;
    }
    export_setup_entries(state);
    WriteDefaultSetupDataBuffer();
}

void open_connect_frontend(FigsState& state) {
    if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
        return;
    }
    CreateConnectFrontendWindow(connect_frontend_state(), state.main_window,
        state.instance, state.return_context);
}

bool connect_to_selected_entry(FigsState& state, HWND hwnd) {
    FigsEntry* entry = selected_entry(state);
    if (entry == nullptr) {
        return false;
    }
    state.selected_entry = *entry;
    DestroyWindow(hwnd);

    const FigsEntry& selected = state.selected_entry;
    if (state.callbacks.connect_to_entry != nullptr) {
        state.callbacks.connect_to_entry(state, selected);
        return true;
    }
    WizardLoginState& wizard = wizard_login_state();
    std::strncpy(wizard.server_address.data(), selected.address.data(),
        wizard.server_address.size() - 1);
    wizard.server_address[wizard.server_address.size() - 1] = '\0';
    CreateWizardLoginWindow(wizard, state.main_window, state.instance,
        state.return_context);
    return true;
}

void draw_list_item(FigsState& state, const DRAWITEMSTRUCT& item) {
    if (item.itemID == static_cast<UINT>(-1)) {
        return;
    }
    const auto* entry = reinterpret_cast<const FigsEntry*>(item.itemData);
    if (entry == nullptr || !entry_used(*entry)) {
        return;
    }

    RECT rect = item.rcItem;
    rect.left += 2;
    rect.top += 2;
    rect.right -= 2;
    SetTextColor(item.hDC, kFigsGray);
    SetBkMode(item.hDC, OPAQUE);
    SetBkColor(item.hDC,
        (item.itemState & ODS_SELECTED) != 0 ? kFigsSelectedBlue : kFigsBlack);
    ExtTextOutA(item.hDC, rect.left, rect.top, ETO_OPAQUE, &item.rcItem, nullptr, 0,
        nullptr);
    DrawTextA(item.hDC, entry->name.data(), -1, &rect, DT_SINGLELINE);
    rect.left = item.rcItem.left + 0x100;
    DrawTextA(item.hDC, entry->address.data(), -1, &rect, DT_SINGLELINE);
}

void release_window_resources(FigsState& state) {
    RestoreFigsAccelerators(state);
    DestroyFigsBackgroundBitmap(state);
    DestroyFigsAddButton(state);
    DestroyFigsDeleteButton(state);
    DestroyFigsConnectButton(state);
    DestroyFigsExitButton(state);
    DestroyFigsScrollControl(state);
    destroy_control(state.list_box);
    destroy_control(state.name_edit);
    destroy_control(state.address_edit);
    state.window = nullptr;
    state.visible = false;
}

} // namespace

FigsState& figs_state() {
    return g_figs_state;
}

void InitializeFigsBackgroundStatic(FigsState& state) {
    InitializeFigsBackgroundBitmap(state);
    RegisterFigsBackgroundDestructor(state);
}

void InitializeFigsBackgroundBitmap(FigsState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterFigsBackgroundDestructor(FigsState&) {
    register_atexit_once(g_background_destructor_registered,
        shutdown_global_background);
}

void DestroyFigsBackgroundBitmap(FigsState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

void InitializeFigsScrollStatic(FigsState& state) {
    InitializeFigsScrollControl(state);
    RegisterFigsScrollDestructor(state);
}

void InitializeFigsScrollControl(FigsState& state) {
    InitializeLegacyCustomScrollControl(state.scroll);
}

void RegisterFigsScrollDestructor(FigsState&) {
    register_atexit_once(g_scroll_destructor_registered,
        shutdown_global_scroll);
}

void DestroyFigsScrollControl(FigsState& state) {
    DestroyLegacyCustomScrollControl(state.scroll);
}

#define DEFINE_FIGS_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Control, Slot, Callback) \
    void StaticName(FigsState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(FigsState& state) { \
        InitializeLegacyImageButtonControl(Control); \
    } \
    void RegisterName(FigsState&) { \
        register_atexit_once(g_button_destructor_registered[Slot], Callback); \
    } \
    void DestroyName(FigsState& state) { \
        DestroyLegacyImageButtonControl(Control); \
    }

DEFINE_FIGS_BUTTON_LIFETIME(InitializeFigsAddButtonStatic,
    InitializeFigsAddButton,
    RegisterFigsAddButtonDestructor,
    DestroyFigsAddButton, state.add_button, 0, shutdown_global_add_button)
DEFINE_FIGS_BUTTON_LIFETIME(InitializeFigsDeleteButtonStatic,
    InitializeFigsDeleteButton,
    RegisterFigsDeleteButtonDestructor,
    DestroyFigsDeleteButton, state.delete_button, 1, shutdown_global_delete_button)
DEFINE_FIGS_BUTTON_LIFETIME(InitializeFigsConnectButtonStatic,
    InitializeFigsConnectButton,
    RegisterFigsConnectButtonDestructor,
    DestroyFigsConnectButton, state.connect_button, 2, shutdown_global_connect_button)
DEFINE_FIGS_BUTTON_LIFETIME(InitializeFigsExitButtonStatic,
    InitializeFigsExitButton,
    RegisterFigsExitButtonDestructor,
    DestroyFigsExitButton, state.cancel_button, 3, shutdown_global_exit_button)

#undef DEFINE_FIGS_BUTTON_LIFETIME

void InitializeFigsResources(FigsState& state) {
    InitializeFigsBackgroundStatic(state);
    InitializeFigsScrollStatic(state);
    InitializeFigsAddButtonStatic(state);
    InitializeFigsDeleteButtonStatic(state);
    InitializeFigsConnectButtonStatic(state);
    InitializeFigsExitButtonStatic(state);
}

void ReleaseFigsResources(FigsState& state) {
    release_window_resources(state);
}

void InstallFigsAccelerators(FigsState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kFigsAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreFigsAccelerators(FigsState& state) {
    if (RankerMainWindowState().active_accelerator_window != state.window) {
        return;
    }
    SetActiveAcceleratorState(nullptr, state.active_accelerators);
    DestroyAcceleratorTable(state.active_accelerators);
    state.active_accelerators = state.saved_accelerators;
    state.active_accelerator_window = state.saved_accelerator_window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

bool CreateFigsWindow(FigsState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context) {
    if (state.window != nullptr) {
        return false;
    }
    InitializeFigsResources(state);
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table, kFigsLayoutTrcRecord)) {
        return false;
    }
    assign_layout(state, layout.table);
    import_setup_entries(state);

    const FigsLayoutRect window_rect = layout_at(state, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(
              parent, window_rect.width, window_rect.height)
        : RankerFrontendWindowOrigin();
    const DWORD style = parent != nullptr ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "FIGS", "FIGS", style,
        origin.x, origin.y, window_rect.width, window_rect.height, parent,
        nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(figs_window_proc));

    if (!create_text_control(state.list_box, state.window, instance, "listbox",
            kListBoxStyle, kFigsListBoxId, layout_at(state, 1)) ||
        !CreateLegacyCustomScrollControlWindow(state.scroll, state.window, "FIGS",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFigsScrollControlId)),
            false, layout_at(state, 2).x, layout_at(state, 2).y,
            layout_at(state, 2).width, layout_at(state, 2).height) ||
        !create_text_control(state.name_edit, state.window, instance, "edit",
            kEditStyle, kFigsNameEditId, layout_at(state, 3)) ||
        !create_text_control(state.address_edit, state.window, instance, "edit",
            kEditStyle, kFigsAddressEditId, layout_at(state, 4)) ||
        !create_image_button(state.add_button, state.window, "&Add", kFigsAddButtonId,
            layout_at(state, 5), kFigsAddNormalBitmapRecord,
            kFigsAddPressedBitmapRecord) ||
        !create_image_button(state.delete_button, state.window, "&Delete",
            kFigsDeleteButtonId, layout_at(state, 6), kFigsDeleteNormalBitmapRecord,
            kFigsDeletePressedBitmapRecord) ||
        !create_image_button(state.connect_button, state.window, "&Connect",
            kFigsConnectButtonId, layout_at(state, 7), kFigsConnectNormalBitmapRecord,
            kFigsConnectPressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kFigsCancelButtonId, layout_at(state, 8), kFigsExitNormalBitmapRecord,
            kFigsExitPressedBitmapRecord)) {
        DestroyWindow(state.window);
        state.window = nullptr;
        return false;
    }

    state.scroll.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC));
    SetWindowLongPtrA(state.scroll.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(figs_control_proc));
    LoadLegacyCustomScrollControlBitmaps(state.scroll, 0xf7, 0, 0xfa, 0, 0xf8, 0xf9);

    SendMessageA(state.name_edit.window, EM_LIMITTEXT, 0x30, 0);
    SendMessageA(state.address_edit.window, EM_LIMITTEXT, 0x50, 0);
    const int row_height = static_cast<int>(
        SendMessageA(state.list_box.window, LB_GETITEMHEIGHT, 0, 0));
    state.visible_rows = row_height > 0 ? layout_at(state, 1).height / row_height : 12;
    state.visible_rows = std::max(1, state.visible_rows);
    SetLegacyCustomScrollControlPageStep(state.scroll, state.visible_rows);

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kFigsBackgroundBitmapRecord);
    InstallFigsAccelerators(state);
    RefreshFigsList(state);
    ShowWindow(state.list_box.window, SW_SHOW);
    ShowWindow(state.name_edit.window, SW_SHOW);
    ShowWindow(state.address_edit.window, SW_SHOW);
    ShowWindow(state.window, SW_SHOW);
    SetFocus(state.name_edit.window);
    state.visible = true;
    return true;
}

bool AddFigsEntry(FigsState& state, const char* name, const char* address) {
    return AddFigsAddressEntry(state, name, address);
}

void DeleteSelectedFigsEntry(FigsState& state) {
    DeleteSelectedFigsAddressEntry(state);
}

void AppendFigsEntryToListBox(FigsState& state, FigsEntry& entry) {
    add_listbox_entry(state, entry);
    update_scroll(state);
}

void DeleteFigsListBoxEntry(FigsState& state, i32 list_index) {
    if (state.list_box.window == nullptr || list_index < 0) {
        return;
    }
    SendMessageA(state.list_box.window, LB_DELETESTRING,
        static_cast<WPARAM>(list_index), 0);
    update_scroll(state, list_index);
}

bool AddFigsAddressEntry(FigsState& state, const char* name, const char* address) {
    if (name == nullptr || address == nullptr || name[0] == '\0' ||
        address[0] == '\0') {
        return false;
    }
    for (FigsEntry& entry : state.entries) {
        if (!entry_used(entry)) {
            copy_c_string(entry.name, name);
            copy_c_string(entry.address, address);
            AppendFigsEntryToListBox(state, entry);
            return true;
        }
    }
    return false;
}

void DeleteSelectedFigsAddressEntry(FigsState& state) {
    FigsEntry* entry = selected_entry(state);
    if (entry == nullptr) {
        return;
    }
    const int selected = static_cast<int>(
        SendMessageA(state.list_box.window, LB_GETCURSEL, 0, 0));
    entry->name.fill(0);
    entry->address.fill(0);
    if (selected != LB_ERR) {
        DeleteFigsListBoxEntry(state, selected);
    }
}

void RefreshFigsList(FigsState& state) {
    if (state.list_box.window == nullptr) {
        return;
    }
    SendMessageA(state.list_box.window, LB_RESETCONTENT, 0, 0);
    for (FigsEntry& entry : state.entries) {
        if (entry_used(entry)) {
            add_listbox_entry(state, entry);
        }
    }
    update_scroll(state, 0);
}

bool paint_background_if_current(FigsState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    StretchBitmapMemoryResourceToClient(state.background, paint.hdc, state.window);
    EndPaint(hwnd, &paint);
    return true;
}

LRESULT HandleFigsWindowMessage(FigsState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        release_window_resources(state);
        return 0;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kFigsWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kFigsBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kFigsListBoxId) {
            draw_list_item(state, *draw);
            break;
        }
        if (LegacyImageButtonControl* button =
                button_for_id(state, static_cast<int>(draw->CtlID))) {
            DrawLegacyImageButtonItem(*button, *draw);
            break;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        switch (id) {
        case kFigsAddButtonId: {
            play_click_sound(state);
            char name[kFigsNameBytes]{};
            char address[kFigsAddressBytes]{};
            GetWindowTextA(state.name_edit.window, name, static_cast<int>(sizeof(name)));
            GetWindowTextA(state.address_edit.window, address,
                static_cast<int>(sizeof(address)));
            if (AddFigsEntry(state, name, address)) {
                SetWindowTextA(state.name_edit.window, "");
                SetWindowTextA(state.address_edit.window, "");
            }
            break;
        }
        case kFigsDeleteButtonId:
            play_click_sound(state);
            DeleteSelectedFigsEntry(state);
            break;
        case kFigsConnectButtonId:
            play_click_sound(state);
            if (!connect_to_selected_entry(state, hwnd)) {
                return 0;
            }
            break;
        case kFigsCancelButtonId:
            play_click_sound(state);
            write_setup_data(state);
            DestroyWindow(hwnd);
            open_connect_frontend(state);
            break;
        case kFigsFocusNameCommandId:
            SetFocus(state.name_edit.window);
            break;
        case kFigsFocusAddressCommandId:
            SetFocus(state.address_edit.window);
            break;
        case kFigsForwardFocusCommandId: {
            HWND focus = GetFocus();
            if (focus == state.name_edit.window) {
                SetFocus(state.address_edit.window);
                return 0;
            }
            if (focus == state.address_edit.window) {
                SetFocus(state.name_edit.window);
                return 0;
            }
            SetFocus(state.name_edit.window);
            break;
        }
        case kFigsListBoxId:
            if (notify == LBN_SELCHANGE) {
                const int top = static_cast<int>(
                    SendMessageA(state.list_box.window, LB_GETTOPINDEX, 0, 0));
                SetLegacyCustomScrollControlValue(state.scroll, top, true);
            }
            else if (notify == LBN_DBLCLK) {
                if (!connect_to_selected_entry(state, hwnd)) {
                    return 0;
                }
            }
            break;
        default:
            break;
        }
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    }
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleFigsControlMessage(FigsState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT && hwnd == state.scroll.window) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.scroll, paint.hdc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (id == kFigsScrollControlId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.scroll, message, wparam, lparam);
        if (changed && state.list_box.window != nullptr) {
            SendMessageA(state.list_box.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(GetLegacyCustomScrollControlValue(state.scroll)), 0);
        }
    }

    switch (id) {
    case kFigsListBoxId:
    case kFigsNameEditId:
    case kFigsAddressEditId:
    case kFigsAddButtonId:
    case kFigsDeleteButtonId:
    case kFigsConnectButtonId:
    case kFigsCancelButtonId:
    case kFigsScrollControlId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

}

#endif
