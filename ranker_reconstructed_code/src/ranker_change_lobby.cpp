#include "ranker_change_lobby.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kEditStyle = WS_CHILD | WS_VISIBLE;
constexpr DWORD kListBoxStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr COLORREF kChangeWhite = RGB(255, 255, 255);
constexpr COLORREF kChangeSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kChangeBlack = RGB(0, 0, 0);
constexpr int kChangeLobbyOriginalNameReadChars = 0x14;

ChangeLobbyState g_change_lobby_state;
std::array<bool, 3> g_control_shutdown_registered{};
bool g_background_shutdown_registered = false;

struct ChangeLobbyListItemPayload {
    int lobby_id = -1;
    int icon_slot = 0;
};

std::vector<ChangeLobbyListItemPayload*> g_list_item_payloads;

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

LRESULT CALLBACK change_lobby_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleChangeLobbyWindowMessage(g_change_lobby_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK change_lobby_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleChangeLobbyControlMessage(g_change_lobby_state, hwnd, message,
        wparam, lparam);
}

void shutdown_global_background() {
    ShutdownChangeLobbyBackgroundBitmap(g_change_lobby_state);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_change_button() {
    ShutdownChangeLobbyChangeButton(g_change_lobby_state);
}

void shutdown_cancel_button() {
    ShutdownChangeLobbyCancelButton(g_change_lobby_state);
}

void shutdown_scroll_control() {
    ShutdownChangeLobbyScrollControl(g_change_lobby_state);
}

ChangeLobbyLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return ChangeLobbyLayoutRect{};
}

void clear_control(ChangeLobbyControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_control(ChangeLobbyControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_lobby_control_proc));
}

bool create_control(ChangeLobbyControl& control, HWND parent, HINSTANCE instance,
    const char* class_name, const char* text, DWORD style, int id,
    const ChangeLobbyLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, text, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_control(control);
    return true;
}

bool create_image_button(LegacyImageButtonControl& button, HWND parent,
    const char* text, int id, const ChangeLobbyLayoutRect& rect,
    u32 normal_record, u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(button, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    if (normal_record != 0 || pressed_record != 0) {
        LoadLegacyImageButtonBitmaps(button, normal_record, pressed_record);
    }
    HWND window = GetLegacyImageButtonWindow(button);
    SetWindowLongPtrA(window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_lobby_control_proc));
    return true;
}

void show_change_message(ChangeLobbyState& state, const char* text, COLORREF color) {
    state.last_status_text = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_status_text.c_str(), color);
    }
}

void write_packet_u32(u8* packet, std::size_t offset, u32 value) {
    packet[offset] = static_cast<u8>(value & 0xff);
    packet[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
    packet[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
    packet[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

void write_packet_text(u8* packet, std::size_t offset, std::size_t field_size,
    const char* text) {
    if (field_size == 0 || text == nullptr) {
        return;
    }
    const std::size_t copied = std::min(field_size - 1, std::strlen(text));
    std::memcpy(packet + offset, text, copied);
}

u32 read_packet_u32(const u8* packet, std::size_t byte_count,
    std::size_t offset) {
    if (packet == nullptr || offset + sizeof(u32) > byte_count) {
        return 0;
    }
    return static_cast<u32>(packet[offset]) |
        (static_cast<u32>(packet[offset + 1]) << 8) |
        (static_cast<u32>(packet[offset + 2]) << 16) |
        (static_cast<u32>(packet[offset + 3]) << 24);
}

std::string fixed_packet_string(const u8* packet, std::size_t byte_count,
    std::size_t offset, std::size_t field_size) {
    if (packet == nullptr || offset >= byte_count || field_size == 0) {
        return {};
    }
    const char* text = reinterpret_cast<const char*>(packet + offset);
    const std::size_t limit = std::min(field_size, byte_count - offset);
    std::size_t length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return std::string(text, length);
}

template <std::size_t N>
void initialize_change_packet(std::array<u8, N>& packet, u32 opcode) {
    write_packet_u32(packet.data(), 0, 3);
    write_packet_u32(packet.data(), 4, opcode);
    write_packet_u32(packet.data(), 8, static_cast<u32>(packet.size()));
}

void queue_change_packet(ChangeLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (state.callbacks.queue_packet != nullptr && packet != nullptr &&
        byte_count <= 0x7fffffffu) {
        state.callbacks.queue_packet(state, packet, static_cast<i32>(byte_count));
    }
}

int selected_list_index(HWND listbox) {
    if (listbox == nullptr) {
        return LB_ERR;
    }
    return static_cast<int>(SendMessageA(listbox, LB_GETCURSEL, 0, 0));
}

auto tracked_payload(ChangeLobbyListItemPayload* payload) {
    return std::find(g_list_item_payloads.begin(), g_list_item_payloads.end(),
        payload);
}

ChangeLobbyListItemPayload* payload_from_item_data(LRESULT item_data) {
    if (item_data == LB_ERR || item_data == 0) {
        return nullptr;
    }
    auto* payload = reinterpret_cast<ChangeLobbyListItemPayload*>(item_data);
    return tracked_payload(payload) == g_list_item_payloads.end() ? nullptr : payload;
}

int lobby_id_from_item_data(LRESULT item_data) {
    ChangeLobbyListItemPayload* payload = payload_from_item_data(item_data);
    return payload != nullptr ? payload->lobby_id : static_cast<int>(item_data);
}

void release_item_data_payload(LRESULT item_data) {
    ChangeLobbyListItemPayload* payload = payload_from_item_data(item_data);
    if (payload == nullptr) {
        return;
    }
    auto it = tracked_payload(payload);
    if (it != g_list_item_payloads.end()) {
        g_list_item_payloads.erase(it);
    }
    delete payload;
}

LPARAM make_item_data_payload(int lobby_id, int icon_slot) {
    auto* payload = new (std::nothrow) ChangeLobbyListItemPayload{lobby_id, icon_slot};
    if (payload == nullptr) {
        return static_cast<LPARAM>(lobby_id);
    }
    g_list_item_payloads.push_back(payload);
    return reinterpret_cast<LPARAM>(payload);
}

void sync_listbox_from_items(ChangeLobbyState& state) {
    if (state.lobby_list.window == nullptr) {
        return;
    }
    int top_index = static_cast<int>(
        SendMessageA(state.lobby_list.window, LB_GETTOPINDEX, 0, 0));
    if (top_index < 0) {
        top_index = 0;
    }
    ClearChangeLobbyListItemData(state.lobby_list.window);
    SendMessageA(state.lobby_list.window, LB_RESETCONTENT, 0, 0);
    for (const ChangeLobbyListItem& item : state.items) {
        LRESULT index = SendMessageA(state.lobby_list.window, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(item.name.c_str()));
        if (index != LB_ERR) {
            SendMessageA(state.lobby_list.window, LB_SETITEMDATA,
                static_cast<WPARAM>(index),
                make_item_data_payload(item.lobby_id, item.icon_slot));
        }
    }

    const int count = static_cast<int>(
        SendMessageA(state.lobby_list.window, LB_GETCOUNT, 0, 0));
    const int visible_rows = std::max(1, state.visible_item_capacity);
    const int max_top = std::max(0, count - visible_rows);
    const int clamped_top = std::min(top_index, max_top);
    SetLegacyCustomScrollControlRange(state.scroll_control, 0, max_top, false);
    SetLegacyCustomScrollControlValue(state.scroll_control, clamped_top, false);
    SetLegacyCustomScrollControlVisible(state.scroll_control, max_top > 0);
    SendMessageA(state.lobby_list.window, LB_SETTOPINDEX,
        static_cast<WPARAM>(clamped_top), 0);
}

void close_change_lobby(ChangeLobbyState& state, bool return_to_parent) {
    state.visible = false;
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
    if (return_to_parent && state.callbacks.return_to_parent != nullptr) {
        state.callbacks.return_to_parent(state.parent_window, state.instance,
            state.return_context);
    }
}

WNDPROC original_proc_for_id(ChangeLobbyState& state, int id) {
    switch (id) {
    case kChangeLobbyNameEditId:
        return state.name_edit.original_window_proc;
    case kChangeLobbyPasswordEditId:
        return state.password_edit.original_window_proc;
    case kChangeLobbyListId:
        return state.lobby_list.original_window_proc;
    case kChangeLobbyScrollControlId:
        return state.scroll_control.original_window_proc;
    case kChangeLobbyChangeButtonId:
        return state.change_button.original_window_proc;
    case kChangeLobbyCancelButtonId:
        return state.cancel_button.original_window_proc;
    default:
        return nullptr;
    }
}

LegacyImageButtonControl* button_for_id(ChangeLobbyState& state, int id) {
    switch (id) {
    case kChangeLobbyChangeButtonId:
        return &state.change_button;
    case kChangeLobbyCancelButtonId:
        return &state.cancel_button;
    default:
        return nullptr;
    }
}

void draw_lobby_list_item(ChangeLobbyState& state, const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return;
    }
    char text[256]{};
    SendMessageA(draw.hwndItem, LB_GETTEXT, draw.itemID, reinterpret_cast<LPARAM>(text));
    const int lobby_id = lobby_id_from_item_data(
        SendMessageA(draw.hwndItem, LB_GETITEMDATA, draw.itemID, 0));

    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(draw.hDC, kChangeWhite);
    SetBkMode(draw.hDC, TRANSPARENT);
    if ((draw.itemState & ODS_SELECTED) != 0) {
        SetBkMode(draw.hDC, OPAQUE);
        SetBkColor(draw.hDC, RGB(0, 0, 255));
    }

    char line[320]{};
    std::snprintf(line, sizeof(line), "%s (%d)", text, lobby_id);
    rect.left += 8;
    DrawTextA(draw.hDC, line, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    (void)state;
}

} // namespace

ChangeLobbyState& change_lobby_state() {
    return g_change_lobby_state;
}

void InitializeChangeLobbySupport(ChangeLobbyState& state) {
    InitializeChangeLobbyBackgroundBitmap(state);
    RegisterChangeLobbyBackgroundBitmapShutdown(state);
    InitializeChangeLobbyChangeButton(state);
    RegisterChangeLobbyChangeButtonShutdown(state);
    InitializeChangeLobbyCancelButton(state);
    RegisterChangeLobbyCancelButtonShutdown(state);
    InitializeChangeLobbyScrollControl(state);
    RegisterChangeLobbyScrollControlShutdown(state);
}

void InitializeChangeLobbyBackgroundBitmapSupport() {
    InitializeChangeLobbyBackgroundBitmap(g_change_lobby_state);
    RegisterChangeLobbyBackgroundBitmapShutdown(g_change_lobby_state);
}

void InitializeChangeLobbyBackgroundBitmap(ChangeLobbyState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterChangeLobbyBackgroundBitmapShutdown(ChangeLobbyState&) {
    register_atexit_once(g_background_shutdown_registered, shutdown_global_background);
}

void RegisterChangeLobbyBackgroundShutdown(ChangeLobbyState& state) {
    RegisterChangeLobbyBackgroundBitmapShutdown(state);
}

void ShutdownChangeLobbyBackgroundBitmap(ChangeLobbyState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeChangeLobbyChangeButtonSupport() {
    InitializeChangeLobbyChangeButton(g_change_lobby_state);
    RegisterChangeLobbyChangeButtonShutdown(g_change_lobby_state);
}

void InitializeChangeLobbyChangeButton(ChangeLobbyState& state) {
    InitializeLegacyImageButtonControl(state.change_button);
}

void RegisterChangeLobbyChangeButtonShutdown(ChangeLobbyState&) {
    register_atexit_once(g_control_shutdown_registered[0], shutdown_change_button);
}

void ShutdownChangeLobbyChangeButton(ChangeLobbyState& state) {
    DestroyLegacyImageButtonControl(state.change_button);
}

void InitializeChangeLobbyCancelButtonSupport() {
    InitializeChangeLobbyCancelButton(g_change_lobby_state);
    RegisterChangeLobbyCancelButtonShutdown(g_change_lobby_state);
}

void InitializeChangeLobbyCancelButton(ChangeLobbyState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterChangeLobbyCancelButtonShutdown(ChangeLobbyState&) {
    register_atexit_once(g_control_shutdown_registered[1], shutdown_cancel_button);
}

void ShutdownChangeLobbyCancelButton(ChangeLobbyState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InitializeChangeLobbyScrollControlSupport() {
    InitializeChangeLobbyScrollControl(g_change_lobby_state);
    RegisterChangeLobbyScrollControlShutdown(g_change_lobby_state);
}

void InitializeChangeLobbyScrollControl(ChangeLobbyState& state) {
    InitializeLegacyCustomScrollControl(state.scroll_control);
}

void RegisterChangeLobbyScrollControlShutdown(ChangeLobbyState&) {
    register_atexit_once(g_control_shutdown_registered[2], shutdown_scroll_control);
}

void ShutdownChangeLobbyScrollControl(ChangeLobbyState& state) {
    DestroyLegacyCustomScrollControl(state.scroll_control);
}

void InstallChangeLobbyAccelerators(ChangeLobbyState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kChangeLobbyAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreChangeLobbyAccelerators(ChangeLobbyState& state) {
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

bool CreateChangeLobbyWindow(ChangeLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.items.clear();
    state.pending_lobby_name.fill(0);
    state.pending_password.fill(0);
    InitializeChangeLobbySupport(state);

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kChangeLobbyLayoutTrcRecord)) {
        return false;
    }

    const ChangeLobbyLayoutRect window_rect = layout_at(layout.table, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(
              parent, window_rect.width, window_rect.height)
        : RankerFrontendWindowOrigin();
    const DWORD window_style =
        IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Change Lobby",
        "Change Lobby", window_style, origin.x, origin.y,
        window_rect.width, window_rect.height, parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_lobby_window_proc));

    if (!create_control(state.name_edit, state.window, instance, "edit", nullptr,
            kEditStyle, kChangeLobbyNameEditId, layout_at(layout.table, 1)) ||
        !create_control(state.password_edit, state.window, instance, "edit", nullptr,
            kEditStyle, kChangeLobbyPasswordEditId, layout_at(layout.table, 2)) ||
        !create_control(state.lobby_list, state.window, instance, "listbox", nullptr,
            kListBoxStyle, kChangeLobbyListId, layout_at(layout.table, 3))) {
        return false;
    }
    const ChangeLobbyLayoutRect scroll_rect = layout_at(layout.table, 4);
    if (!CreateLegacyCustomScrollControlWindow(state.scroll_control, state.window,
            "Change Lobby",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChangeLobbyScrollControlId)),
            false, scroll_rect.x, scroll_rect.y, scroll_rect.width,
            scroll_rect.height)) {
        return false;
    }
    state.scroll_control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(state.scroll_control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(state.scroll_control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(change_lobby_control_proc));
    LoadLegacyCustomScrollControlBitmaps(state.scroll_control, 0xc7, 0, 0xc8, 0,
        0xc9, 0xca);
    const LRESULT item_height =
        SendMessageA(state.lobby_list.window, LB_GETITEMHEIGHT, 0, 0);
    const int list_item_height = item_height > 0 ? static_cast<int>(item_height) : 1;
    state.visible_item_capacity =
        std::max(1, layout_at(layout.table, 3).height / list_item_height);
    SetLegacyCustomScrollControlPageStep(state.scroll_control,
        state.visible_item_capacity);
    if (!create_image_button(state.change_button, state.window, "Change",
            kChangeLobbyChangeButtonId, layout_at(layout.table, 5),
            kChangeLobbyChangeNormalBitmapRecord,
            kChangeLobbyChangePressedBitmapRecord) ||
        !create_image_button(state.cancel_button, state.window, "&Cancel",
            kChangeLobbyCancelButtonId, layout_at(layout.table, 6),
            kChangeLobbyCancelNormalBitmapRecord,
            kChangeLobbyCancelPressedBitmapRecord)) {
        return false;
    }

    SendMessageA(state.name_edit.window, EM_LIMITTEXT, 0x7f, 0);
    SendMessageA(state.password_edit.window, EM_LIMITTEXT, 0x1f, 0);
    HFONT default_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageA(state.window, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.lobby_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.name_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageA(state.password_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(default_font), TRUE);
    SetWindowTextA(state.password_edit.window, "");
    ShowWindow(state.lobby_list.window, SW_SHOW);
    SetFocus(state.name_edit.window);

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kChangeLobbyBackgroundBitmapTrcRecord);
    InstallChangeLobbyAccelerators(state);
    state.visible = true;
    QueueChangeLobbyListRequest(state, 0);
    return true;
}

LRESULT HandleChangeLobbyWindowMessage(ChangeLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        ClearChangeLobbyListEntries(state);
        RestoreChangeLobbyAccelerators(state);
        ShutdownChangeLobbyBackgroundBitmap(state);
        clear_control(state.name_edit);
        clear_control(state.password_edit);
        clear_control(state.lobby_list);
        ShutdownChangeLobbyScrollControl(state);
        ShutdownChangeLobbyChangeButton(state);
        ShutdownChangeLobbyCancelButton(state);
        state.window = nullptr;
        state.visible = false;
        return 0;
    case WM_PAINT:
        if (hwnd == state.window) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            StretchBitmapMemoryResourceToClient(state.background, dc, state.window);
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kChangeSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kChangeBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kChangeSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kChangeBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kChangeLobbyListId) {
            draw_lobby_list_item(state, *draw);
            return TRUE;
        }
        LegacyImageButtonControl* button = button_for_id(state, draw->CtlID);
        if (button != nullptr) {
            DrawLegacyImageButtonItem(*button, *draw);
            break;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        switch (id) {
        case kChangeLobbyFocusNameCommandId:
            SetFocus(state.name_edit.window);
            return 0;
        case kChangeLobbyCancelButtonId:
            HandleDefaultFrontendUiClickSound();
            if (state.callbacks.focus_parent_control != nullptr) {
                state.callbacks.focus_parent_control(state);
            }
            close_change_lobby(state, false);
            return 0;
        case kChangeLobbyListId:
            if (notify == LBN_SELCHANGE) {
                int selected = selected_list_index(state.lobby_list.window);
                if (selected != LB_ERR) {
                    char name[0x80]{};
                    SendMessageA(state.lobby_list.window, LB_GETTEXT,
                        static_cast<WPARAM>(selected), reinterpret_cast<LPARAM>(name));
                    SetWindowTextA(state.name_edit.window, name);
                }
                return 0;
            }
            if (notify == LBN_DBLCLK) {
                PostMessageA(hwnd, WM_COMMAND,
                    static_cast<WPARAM>(kChangeLobbyChangeButtonId), 0);
                return 0;
            }
            break;
        case kChangeLobbyChangeButtonId:
            SubmitChangeLobbySelection(state);
            return 0;
        default:
            if (id > 40000 && id < 0x9c43) {
                HWND focus = GetFocus();
                int focused_id = static_cast<int>(GetWindowLongPtrA(focus, GWLP_ID));
                if (focused_id == kChangeLobbyNameEditId) {
                    SetFocus(state.password_edit.window);
                } else if (focused_id == kChangeLobbyPasswordEditId) {
                    SetFocus(state.name_edit.window);
                }
                return 0;
            }
            break;
        }
        break;
    }
    case kChangeLobbyNetworkMessage:
        if (LOWORD(lparam) == 1) {
            DispatchChangeLobbyNetworkMessage(state, wparam, lparam);
            return 0;
        }
        if (LOWORD(lparam) == 0x20) {
            show_change_message(state,
                startup_message_row(5, "Disconnected from the server."),
                RGB(10, 10, 250));
            if (state.callbacks.set_busy != nullptr) {
                state.callbacks.set_busy(FALSE);
            }
            close_change_lobby(state, true);
            return 0;
        }
        break;
    case kChangeLobbyStatusMessage0:
    case kChangeLobbyStatusMessage1:
    case kChangeLobbyStatusMessage2:
    case kChangeLobbyStatusMessage3:
        show_change_message(state, reinterpret_cast<const char*>(wparam),
            static_cast<COLORREF>(lparam));
        return 0;
    case kChangeLobbyBusyMessage:
        if (state.callbacks.set_busy != nullptr) {
            state.callbacks.set_busy(static_cast<BOOL>(wparam));
        }
        return 0;
    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleChangeLobbyControlMessage(ChangeLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (message == WM_PAINT && hwnd == state.scroll_control.window) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.scroll_control, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }

    if (id == kChangeLobbyScrollControlId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.scroll_control, message, wparam, lparam);
        if (changed && state.lobby_list.window != nullptr) {
            const int top = GetLegacyCustomScrollControlValue(state.scroll_control);
            SendMessageA(state.lobby_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(top), 0);
        }
    }

    switch (id) {
    case kChangeLobbyNameEditId:
    case kChangeLobbyListId:
    case kChangeLobbyScrollControlId:
    case kChangeLobbyChangeButtonId:
    case kChangeLobbyPasswordEditId:
    case kChangeLobbyCancelButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

void ShowChangeLobbyNameRequiredMessage(ChangeLobbyState& state) {
    show_change_message(state,
        startup_message_row(62, "Enter the lobby name to move to."),
        kChangeSoftWhite);
}

std::array<u8, kChangeLobbyListRequestPacketBytes> BuildChangeLobbyListRequestPacket(
    int start_index) {
    std::array<u8, kChangeLobbyListRequestPacketBytes> packet{};
    initialize_change_packet(packet, 0x14);
    write_packet_u32(packet.data(), 0x0d, static_cast<u32>(start_index));
    return packet;
}

std::array<u8, kChangeLobbyJoinPacketBytes> BuildChangeLobbyJoinPacket(
    const char* password, int lobby_id) {
    std::array<u8, kChangeLobbyJoinPacketBytes> packet{};
    initialize_change_packet(packet, 5);
    write_packet_u32(packet.data(), 0x0d, static_cast<u32>(lobby_id));
    write_packet_text(packet.data(), 0x11, 0x20, password);
    return packet;
}

std::array<u8, kChangeLobbyCreatePacketBytes> BuildChangeLobbyCreatePacket(
    const char* name, const char* password) {
    std::array<u8, kChangeLobbyCreatePacketBytes> packet{};
    initialize_change_packet(packet, 8);
    write_packet_text(packet.data(), 0x0d, 0x80, name);
    write_packet_text(packet.data(), 0x8d, 0x20, password);
    return packet;
}

void QueueChangeLobbyListRequest(ChangeLobbyState& state, int start_index) {
    if (state.callbacks.queue_packet != nullptr) {
        const auto packet = BuildChangeLobbyListRequestPacket(start_index);
        queue_change_packet(state, packet.data(), packet.size());
        return;
    }
    if (state.callbacks.request_lobby_list != nullptr) {
        state.callbacks.request_lobby_list(state, start_index);
    }
}

void SubmitChangeLobbySelection(ChangeLobbyState& state) {
    state.pending_lobby_name.fill(0);
    state.pending_password.fill(0);
    GetWindowTextA(state.name_edit.window, state.pending_lobby_name.data(),
        kChangeLobbyOriginalNameReadChars);
    if (state.pending_lobby_name[0] == '\0') {
        ShowChangeLobbyNameRequiredMessage(state);
        return;
    }

    GetWindowTextA(state.password_edit.window, state.pending_password.data(),
        static_cast<int>(state.pending_password.size()));

    bool existing = false;
    int lobby_id = -1;
    for (const ChangeLobbyListItem& item : state.items) {
        if (item.name == state.pending_lobby_name.data()) {
            existing = true;
            lobby_id = item.lobby_id;
            break;
        }
    }

    if (state.callbacks.change_lobby != nullptr) {
        state.callbacks.change_lobby(state, state.pending_lobby_name.data(),
            state.pending_password.data(), existing, lobby_id);
        return;
    }

    if (state.callbacks.queue_packet != nullptr) {
        if (existing) {
            const auto packet =
                BuildChangeLobbyJoinPacket(state.pending_password.data(), lobby_id);
            queue_change_packet(state, packet.data(), packet.size());
        } else {
            const auto packet = BuildChangeLobbyCreatePacket(
                state.pending_lobby_name.data(), state.pending_password.data());
            queue_change_packet(state, packet.data(), packet.size());
        }
    }
}

void AddChangeLobbyListEntry(ChangeLobbyState& state, const char* name, int lobby_id,
    int icon_slot) {
    if (name == nullptr || *name == '\0') {
        return;
    }
    auto existing = std::find_if(state.items.begin(), state.items.end(),
        [name](const ChangeLobbyListItem& item) { return item.name == name; });
    if (existing != state.items.end()) {
        return;
    }

    state.items.push_back(ChangeLobbyListItem{name, lobby_id, icon_slot});
    sync_listbox_from_items(state);
}

void RemoveChangeLobbyListEntryById(ChangeLobbyState& state, int lobby_id) {
    auto it = std::find_if(state.items.begin(), state.items.end(),
        [lobby_id](const ChangeLobbyListItem& item) { return item.lobby_id == lobby_id; });
    if (it == state.items.end()) {
        return;
    }
    state.items.erase(it);
    sync_listbox_from_items(state);
}

void ClearChangeLobbyListItemData(HWND listbox) {
    if (listbox == nullptr) {
        return;
    }

    const LRESULT count = SendMessageA(listbox, LB_GETCOUNT, 0, 0);
    if (count == LB_ERR) {
        return;
    }

    for (LRESULT index = 0; index < count; ++index) {
        const LRESULT item_data = SendMessageA(listbox, LB_GETITEMDATA,
            static_cast<WPARAM>(index), 0);
        release_item_data_payload(item_data);
        SendMessageA(listbox, LB_SETITEMDATA, static_cast<WPARAM>(index), 0);
    }
}

void ClearChangeLobbyListEntries(ChangeLobbyState& state) {
    state.items.clear();
    if (state.lobby_list.window != nullptr) {
        ClearChangeLobbyListItemData(state.lobby_list.window);
        SendMessageA(state.lobby_list.window, LB_RESETCONTENT, 0, 0);
    }
    SetLegacyCustomScrollControlRange(state.scroll_control, 0, 0, false);
    SetLegacyCustomScrollControlValue(state.scroll_control, 0, false);
    SetLegacyCustomScrollControlVisible(state.scroll_control, false);
}

bool ApplyChangeLobbyListPacket(ChangeLobbyState& state, const void* packet,
    std::size_t byte_count) {
    const auto* bytes = static_cast<const u8*>(packet);
    if (bytes == nullptr || byte_count < 0x0d) {
        return false;
    }

    const u32 opcode = read_packet_u32(bytes, byte_count, 4);
    switch (opcode) {
    case 10: {
        const std::string name = fixed_packet_string(bytes, byte_count, 0x0d, 0x80);
        const int lobby_id = static_cast<int>(
            read_packet_u32(bytes, byte_count, 0x8d));
        AddChangeLobbyListEntry(state, name.c_str(), lobby_id, 1);
        return true;
    }
    case 0x0b:
        RemoveChangeLobbyListEntryById(state,
            static_cast<int>(read_packet_u32(bytes, byte_count, 0x0d)));
        return true;
    case 0x15: {
        const i32 next_index = static_cast<i32>(
            read_packet_u32(bytes, byte_count, 0x91));
        if (next_index >= 0) {
            const std::string name = fixed_packet_string(bytes, byte_count,
                0x0d, 0x80);
            const int icon_slot = static_cast<int>(
                read_packet_u32(bytes, byte_count, 0x8d));
            const int lobby_id = static_cast<int>(
                read_packet_u32(bytes, byte_count, 0x95));
            AddChangeLobbyListEntry(state, name.c_str(), lobby_id, icon_slot);
            QueueChangeLobbyListRequest(state, next_index + 1);
        }
        return true;
    }
    default:
        return false;
    }
}

bool DispatchChangeLobbyServerPacket(ChangeLobbyState& state, const void* packet,
    std::size_t byte_count) {
    const auto* bytes = static_cast<const u8*>(packet);
    if (bytes == nullptr || byte_count < 0x0d) {
        return false;
    }
    if (ApplyChangeLobbyListPacket(state, packet, byte_count)) {
        return true;
    }

    const u32 opcode = read_packet_u32(bytes, byte_count, 4);
    switch (opcode) {
    case 6: {
        const u32 status = read_packet_u32(bytes, byte_count, 0x0d);
        switch (status) {
        case 0:
            show_change_message(state,
                startup_message_row(63, "The lobby does not exist."),
                kChangeSoftWhite);
            break;
        case 1:
            show_change_message(state,
                startup_message_row(64, "The lobby is full."),
                kChangeSoftWhite);
            break;
        case 2:
            show_change_message(state,
                startup_message_row(7, "The password is incorrect."),
                kChangeSoftWhite);
            break;
        case 3:
            if (state.callbacks.change_succeeded != nullptr) {
                state.callbacks.change_succeeded(state);
            }
            break;
        default:
            show_change_message(state,
                startup_message_row(13, "An unknown error occurred."),
                kChangeSoftWhite);
            break;
        }
        return true;
    }
    case 9: {
        const u32 status = read_packet_u32(bytes, byte_count, 0x0d);
        if (status == 1) {
            if (state.callbacks.change_succeeded != nullptr) {
                state.callbacks.change_succeeded(state);
            }
        } else {
            show_change_message(state,
                status == 0 ?
                    startup_message_row(66, "The lobby name is already in use.") :
                    startup_message_row(13, "An unknown error occurred."),
                kChangeSoftWhite);
        }
        return true;
    }
    default:
        return false;
    }
}

void DispatchChangeLobbyNetworkMessage(ChangeLobbyState& state, WPARAM wparam,
    LPARAM lparam) {
    if (state.callbacks.handle_network_message != nullptr) {
        state.callbacks.handle_network_message(state, wparam, lparam);
        return;
    }
    if (state.callbacks.forward_network_message != nullptr) {
        state.callbacks.forward_network_message(state.parent_window,
            kChangeLobbyNetworkMessage, wparam, lparam);
        return;
    }
    if (state.parent_window != nullptr && IsWindow(state.parent_window)) {
        PostMessageA(state.parent_window, kChangeLobbyNetworkMessage, wparam, lparam);
    }
}

} // namespace ranker

#endif
