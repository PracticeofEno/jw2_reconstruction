#include "ranker_view_rank.h"

#ifdef _WIN32

#include "ranker_connect_frontend.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_online_dialogs.h"
#include "ranker_player_profile.h"
#include "ranker_system_ui.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kWindowStyleWindowed =
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD kListBoxStyle =
    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
constexpr DWORD kSearchEditStyle = WS_CHILD;
constexpr COLORREF kViewRankWhite = RGB(255, 255, 255);
constexpr COLORREF kViewRankSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kViewRankGray = RGB(200, 200, 200);
constexpr COLORREF kViewRankBlack = RGB(0, 0, 0);
constexpr COLORREF kViewRankSelectedBackground = RGB(49, 36, 20);
constexpr COLORREF kViewRankPanelBackground = RGB(12, 11, 9);
constexpr COLORREF kViewRankPageBackground = RGB(8, 8, 7);
constexpr COLORREF kViewRankBronze = RGB(151, 116, 66);
constexpr COLORREF kViewRankDarkBronze = RGB(47, 37, 24);
constexpr COLORREF kViewRankGold = RGB(221, 168, 61);
constexpr ViewRankLayoutRect kViewRankTitlePanel{270, 24, 260, 64};
constexpr ViewRankLayoutRect kViewRankCategoryPanel{48, 102, 704, 62};
constexpr ViewRankLayoutRect kViewRankHeaderPanel{70, 180, 665, 42};
constexpr ViewRankLayoutRect kViewRankBottomPanel{48, 510, 704, 70};
constexpr ViewRankLayoutRect kViewRankPngListRect{68, 138, 665, 366};
constexpr ViewRankLayoutRect kViewRankPngSearchRect{74, 528, 212, 25};
constexpr std::array<ViewRankLayoutRect, 5> kViewRankPngColumns = {{
    {68, 138, 127, 366},
    {195, 138, 143, 366},
    {338, 138, 145, 366},
    {483, 138, 134, 366},
    {617, 138, 116, 366},
}};
constexpr std::array<const char*, kViewRankThemeButtonVisualCount>
    kViewRankThemeButtonImagePaths = {
        "media\\ui\\tools\\button_medium_bronze.png",
        "media\\ui\\tools\\button_medium_gold.png",
        "media\\ui\\tools\\button_medium_pressed.png",
        "media\\ui\\tools\\button_medium_gray.png",
    };
ViewRankState g_view_rank_state;
bool g_background_destructor_registered = false;
std::array<bool, 8> g_button_destructor_registered{};

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

struct ViewRankButtonSpec {
    std::size_t slot = 0;
    int id = 0;
    const char* text = "";
    u32 normal_record = 0;
    u32 pressed_record = 0;
    std::size_t layout_index = 0;
};

const ViewRankButtonSpec kButtonSpecs[] = {
    {0, kViewRankUpButtonId, "&Up", 0xd1, 0xd2, 3},
    {1, kViewRankDownButtonId, "&Down", 0xd3, 0xd4, 4},
    {2, kViewRankSearchButtonId, "&Search", 0xd5, 0xd6, 5},
    {3, kViewRankCloseButtonId, "&Close", 0xd7, 0xd8, 6},
    {4, kViewRankGoSiteButtonId, "&Go To Rank Site", 0xd9, 0xda, 7},
    {5, kViewRankNormalTabButtonId, "&Nomal TAB", 0xdb, 0xdc, 8},
    {6, kViewRankAvatarTabButtonId, "&Avatar TAB", 0xdd, 0xde, 9},
    {7, kViewRankGuildTabButtonId, "&Guild TAB", 0xdf, 0xe0, 10},
};

LRESULT CALLBACK view_rank_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleViewRankWindowMessage(g_view_rank_state, hwnd, message, wparam,
        lparam);
}

LRESULT CALLBACK view_rank_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleViewRankControlMessage(g_view_rank_state, hwnd, message, wparam,
        lparam);
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void shutdown_global_background() {
    DestroyViewRankBackgroundBitmap(g_view_rank_state);
}

void destroy_view_rank_button_slot(ViewRankState& state, std::size_t slot) {
    if (slot < state.buttons.size()) {
        DestroyLegacyImageButtonControl(state.buttons[slot]);
    }
}

template <std::size_t Slot>
void shutdown_global_button_slot() {
    destroy_view_rank_button_slot(g_view_rank_state, Slot);
}

ViewRankLayoutRect layout_at(const ViewRankState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return ViewRankLayoutRect{};
}

void assign_layout(ViewRankState& state, const FrontendLayoutRectTable& table) {
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

void fill_view_rank_rect(HDC dc, const RECT& rect, COLORREF color) {
    if (dc == nullptr) {
        return;
    }
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

void frame_view_rank_rect(HDC dc, const RECT& rect, COLORREF color) {
    if (dc == nullptr) {
        return;
    }
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        FrameRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

ViewRankLayoutRect scale_view_rank_theme_rect(const ViewRankState& state,
    const ViewRankLayoutRect& source) {
    const ViewRankLayoutRect root = layout_at(state, 0);
    return {
        ScaleFrontendLayoutValue(source.x, 800, std::max(1, root.width)),
        ScaleFrontendLayoutValue(source.y, 600, std::max(1, root.height)),
        ScaleFrontendLayoutValue(source.width, 800, std::max(1, root.width)),
        ScaleFrontendLayoutValue(source.height, 600, std::max(1, root.height)),
    };
}

void apply_view_rank_png_layout(ViewRankState& state) {
    if (state.layout.size() < 16) {
        return;
    }
    state.layout[1] = scale_view_rank_theme_rect(state,
        kViewRankPngListRect);
    state.layout[2] = scale_view_rank_theme_rect(state,
        kViewRankPngSearchRect);
    state.layout[3] = scale_view_rank_theme_rect(state,
        ResolveViewRankPngButtonRect(kViewRankUpButtonId));
    state.layout[4] = scale_view_rank_theme_rect(state,
        ResolveViewRankPngButtonRect(kViewRankDownButtonId));
    state.layout[5] = scale_view_rank_theme_rect(state,
        ResolveViewRankPngButtonRect(kViewRankSearchButtonId));
    state.layout[6] = scale_view_rank_theme_rect(state,
        ResolveViewRankPngButtonRect(kViewRankCloseButtonId));
    for (std::size_t i = 0; i < kViewRankPngColumns.size(); ++i) {
        state.layout[11 + i] = scale_view_rank_theme_rect(state,
            kViewRankPngColumns[i]);
    }
}

RECT view_rank_theme_rect(const ViewRankState& state,
    const ViewRankLayoutRect& source) {
    const ViewRankLayoutRect scaled = scale_view_rank_theme_rect(state, source);
    return {scaled.x, scaled.y, scaled.x + scaled.width,
        scaled.y + scaled.height};
}

void draw_view_rank_panel(HDC dc, RECT rect, COLORREF fill) {
    fill_view_rank_rect(dc, rect, fill);
    frame_view_rank_rect(dc, rect, RGB(10, 8, 5));
    InflateRect(&rect, -1, -1);
    frame_view_rank_rect(dc, rect, kViewRankBronze);
    InflateRect(&rect, -1, -1);
    frame_view_rank_rect(dc, rect, kViewRankDarkBronze);
}

void draw_view_rank_centered_text(HDC dc, HFONT font, const wchar_t* text,
    RECT rect, COLORREF color) {
    HGDIOBJ old_font = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
            DT_NOPREFIX);
    if (old_font != nullptr) {
        SelectObject(dc, old_font);
    }
}

void paint_view_rank_theme(ViewRankState& state, HWND hwnd, HDC dc) {
    if (hwnd == nullptr || dc == nullptr) {
        return;
    }
    if (state.background.loaded) {
        StretchBitmapMemoryResourceToClient(state.background, dc, hwnd);
        return;
    }
    RECT client{};
    GetClientRect(hwnd, &client);
    fill_view_rank_rect(dc, client, kViewRankPageBackground);
    frame_view_rank_rect(dc, client, RGB(10, 8, 5));
    InflateRect(&client, -2, -2);
    frame_view_rank_rect(dc, client, kViewRankBronze);
    InflateRect(&client, -2, -2);
    frame_view_rank_rect(dc, client, kViewRankDarkBronze);

    draw_view_rank_panel(dc, view_rank_theme_rect(state, kViewRankTitlePanel),
        RGB(17, 16, 13));
    draw_view_rank_panel(dc, view_rank_theme_rect(state, kViewRankCategoryPanel),
        kViewRankPanelBackground);
    draw_view_rank_panel(dc, view_rank_theme_rect(state, kViewRankHeaderPanel),
        RGB(17, 16, 13));
    draw_view_rank_panel(dc, view_rank_theme_rect(state, kViewRankBottomPanel),
        kViewRankPanelBackground);

    const ViewRankLayoutRect list = layout_at(state, 1);
    RECT list_frame{list.x - 3, list.y - 3,
        list.x + list.width + 3, list.y + list.height + 3};
    draw_view_rank_panel(dc, list_frame, kViewRankBlack);
    const ViewRankLayoutRect search = layout_at(state, 2);
    RECT search_frame{search.x - 3, search.y - 3,
        search.x + search.width + 3, search.y + search.height + 3};
    draw_view_rank_panel(dc, search_frame, kViewRankBlack);

    RECT title = view_rank_theme_rect(state, kViewRankTitlePanel);
    RECT shadow = title;
    OffsetRect(&shadow, 1, 2);
    draw_view_rank_centered_text(dc, state.theme_title_font,
        L"\ub7ad\ud0b9 \ubcf4\uae30", shadow, RGB(0, 0, 0));
    draw_view_rank_centered_text(dc, state.theme_title_font,
        L"\ub7ad\ud0b9 \ubcf4\uae30", title, RGB(238, 224, 190));

    constexpr std::array<const wchar_t*, 5> headings = {
        L"\uc21c\uc704", L"\uc774\ub984", L"\uc810\uc218",
        L"\uc804\uc801", L"\uc2b9\ub960"};
    RECT header = view_rank_theme_rect(state, kViewRankHeaderPanel);
    for (std::size_t i = 0; i < headings.size(); ++i) {
        const ViewRankLayoutRect column = layout_at(state, 11 + i);
        RECT heading{column.x, header.top, column.x + column.width,
            header.bottom};
        draw_view_rank_centered_text(dc, state.theme_font, headings[i], heading,
            RGB(214, 196, 158));
    }
}

bool load_reconstructed_view_rank_background(ViewRankState& state) {
    return LoadPngBitmapMemoryResourceFromExecutableRelativeFile(
        state.background, "media\\lobby\\view_rank_base.png");
}

void write_le32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset + sizeof(value) > buffer.size()) {
        return;
    }
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}

u32 read_le32(const u8* payload, i32 byte_count, std::size_t offset) {
    if (payload == nullptr || byte_count < 0 ||
        offset + sizeof(u32) > static_cast<std::size_t>(byte_count)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, payload + offset, sizeof(value));
    return value;
}

void copy_fixed_string(std::vector<u8>& buffer, std::size_t offset,
    std::size_t field_size, const char* text) {
    if (offset >= buffer.size() || field_size == 0) {
        return;
    }
    const std::size_t available = std::min(field_size, buffer.size() - offset);
    std::memset(buffer.data() + offset, 0, available);
    if (text != nullptr) {
        std::strncpy(reinterpret_cast<char*>(buffer.data() + offset), text,
            available - 1);
    }
}

template <std::size_t N>
void copy_to_array(std::array<char, N>& target, const char* source) {
    target.fill(0);
    if (source != nullptr) {
        std::strncpy(target.data(), source, target.size() - 1);
    }
}

void queue_packet(ViewRankState& state, const void* packet, i32 byte_count) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet, byte_count);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket,
            const_cast<void*>(packet), byte_count, nullptr);
    }
}

void show_message(ViewRankState& state, const char* text,
    COLORREF color = kViewRankSoftWhite) {
    state.last_message = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
        return;
    }
    if (state.window != nullptr) {
        ShowOnlineModalPrompt1(online_modal_prompt_state(), state.window,
            state.last_message.c_str(), color);
    }
}

void play_click_sound(ViewRankState& state) {
    if (state.callbacks.play_click_sound != nullptr) {
        state.callbacks.play_click_sound(state);
        return;
    }
    HandleDefaultFrontendUiClickSound();
}

void close_async_socket(ViewRankState& state) {
    if (state.callbacks.close_async_socket != nullptr) {
        state.callbacks.close_async_socket(state);
        return;
    }
    if (state.async_tcp_socket != nullptr) {
        CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
    }
}

void forward_view_rank_network_message(ViewRankState& state, WPARAM wparam,
    LPARAM lparam, const u8* payload, i32 byte_count) {
    if (state.callbacks.forward_payload != nullptr) {
        state.callbacks.forward_payload(state, payload, byte_count);
        return;
    }
    if (state.parent_window != nullptr && IsWindow(state.parent_window)) {
        PostMessageA(state.parent_window, kViewRankNetworkMessage, wparam, lparam);
    }
}

void open_connect_frontend(ViewRankState& state) {
    if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
        return;
    }
    CreateConnectFrontendWindow(connect_frontend_state(), state.main_window,
        state.instance, state.return_context);
}

void open_url(ViewRankState& state, const char* url) {
    if (url == nullptr || url[0] == '\0') {
        return;
    }
    if (state.callbacks.open_url != nullptr) {
        state.callbacks.open_url(state, url);
        return;
    }
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOW);
}

void clear_control(ViewRankTextControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void destroy_control(ViewRankTextControl& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    clear_control(control);
}

void subclass_control(ViewRankTextControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(view_rank_control_proc));
}

bool create_text_control(ViewRankTextControl& control, HWND parent,
    HINSTANCE instance, const char* class_name, DWORD style, int id,
    const ViewRankLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, nullptr, style, rect.x, rect.y,
        rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_control(control);
    return true;
}

void subclass_button(LegacyImageButtonControl& button) {
    if (button.window == nullptr) {
        return;
    }
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(button.window, GWLP_WNDPROC));
    SetWindowLongPtrA(button.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(view_rank_control_proc));
}

bool create_image_button(ViewRankState& state, const ViewRankButtonSpec& spec) {
    ViewRankLayoutRect rect = layout_at(state, spec.layout_index);
    LegacyImageButtonControl& button = state.buttons[spec.slot];
    if (!CreateLegacyImageButtonWindow(button, state.window, spec.text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    LoadLegacyImageButtonBitmaps(button, spec.normal_record, spec.pressed_record);
    subclass_button(button);
    return true;
}

LegacyImageButtonControl* button_for_id(ViewRankState& state, int id) {
    for (const ViewRankButtonSpec& spec : kButtonSpecs) {
        if (spec.id == id) {
            return &state.buttons[spec.slot];
        }
    }
    return nullptr;
}

WNDPROC original_proc_for_id(ViewRankState& state, int id) {
    if (id == kViewRankSearchEditId) {
        return state.search_edit.original_window_proc;
    }
    if (id == kViewRankListBoxId) {
        return state.list_box.original_window_proc;
    }
    if (LegacyImageButtonControl* button = button_for_id(state, id)) {
        return button->original_window_proc;
    }
    return nullptr;
}

ViewRankListType type_for_tab_id(int id) {
    switch (id) {
    case kViewRankAvatarTabButtonId:
        return ViewRankListType::Avatar;
    case kViewRankGuildTabButtonId:
        return ViewRankListType::Guild;
    case kViewRankNormalTabButtonId:
    default:
        return ViewRankListType::Normal;
    }
}

bool is_tab_id(int id) {
    return id == kViewRankNormalTabButtonId ||
        id == kViewRankAvatarTabButtonId ||
        id == kViewRankGuildTabButtonId;
}

const wchar_t* view_rank_theme_button_label(int id) {
    switch (id) {
    case kViewRankUpButtonId:
        return L"\uc774\uc804";
    case kViewRankDownButtonId:
        return L"\ub2e4\uc74c";
    case kViewRankSearchButtonId:
        return L"\ucc3e\uae30";
    case kViewRankCloseButtonId:
        return L"\ub2eb\uae30";
    default:
        return nullptr;
    }
}

bool draw_view_rank_themed_button(ViewRankState& state,
    const DRAWITEMSTRUCT& draw) {
    const int id = static_cast<int>(draw.CtlID);
    const wchar_t* label = view_rank_theme_button_label(id);
    if (!state.theme_button_images_loaded || draw.hDC == nullptr ||
        label == nullptr || !IsViewRankThemedButtonId(id)) {
        return false;
    }

    const bool enabled = (draw.itemState & ODS_DISABLED) == 0 &&
        (draw.hwndItem == nullptr || IsWindowEnabled(draw.hwndItem));
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const bool hovered = state.theme_hot_button == draw.hwndItem;
    const ViewRankThemeButtonVisual visual =
        ResolveViewRankThemeButtonVisual(enabled, pressed, hovered);
    UiPngResource& image = state.theme_button_images[
        static_cast<std::size_t>(visual)];

    fill_view_rank_rect(draw.hDC, draw.rcItem, kViewRankPanelBackground);
    const UiPngRect destination{
        draw.rcItem.left, draw.rcItem.top,
        draw.rcItem.right - draw.rcItem.left,
        draw.rcItem.bottom - draw.rcItem.top};
    if (!DrawUiPngResourceToDc(image, draw.hDC, destination)) {
        return false;
    }

    RECT text = draw.rcItem;
    const int width = std::max<int>(1, text.right - text.left);
    const int height = std::max<int>(1, text.bottom - text.top);
    InflateRect(&text, -std::max(4, width / 14),
        -std::max(2, height / 9));
    if (pressed) {
        OffsetRect(&text, 1, 1);
    }
    RECT shadow = text;
    OffsetRect(&shadow, 1, 2);
    draw_view_rank_centered_text(draw.hDC, state.theme_font, label, shadow,
        RGB(0, 0, 0));
    const COLORREF color = !enabled ? RGB(142, 142, 136) :
        (pressed ? RGB(230, 187, 83) :
            (hovered ? RGB(255, 231, 151) : RGB(235, 224, 194)));
    draw_view_rank_centered_text(draw.hDC, state.theme_font, label, text, color);
    return true;
}

void compute_columns(ViewRankState& state) {
    const int list_left = layout_at(state, 1).x;
    for (std::size_t i = 0; i < state.columns.size(); ++i) {
        const ViewRankLayoutRect rect = layout_at(state, 11 + i);
        state.columns[i].left = rect.x - list_left;
        state.columns[i].width = rect.width;
    }
}

void sync_list_box(ViewRankState& state) {
    if (state.list_box.window == nullptr) {
        return;
    }
    SendMessageA(state.list_box.window, LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < state.entries.size(); ++i) {
        LRESULT row = SendMessageA(state.list_box.window, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(state.entries[i].name.data()));
        if (row != LB_ERR) {
            SendMessageA(state.list_box.window, LB_SETITEMDATA,
                static_cast<WPARAM>(row), static_cast<LPARAM>(i));
        }
    }
}

void draw_column_text(HDC dc, RECT row_rect, const ViewRankColumn& column,
    const char* text, UINT format) {
    row_rect.left += column.left;
    row_rect.right = row_rect.left + column.width;
    DrawTextA(dc, text == nullptr ? "" : text, -1, &row_rect, format);
}

bool draw_rank_entry(ViewRankState& state, const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1)) {
        return true;
    }
    if (draw.itemID >= state.entries.size()) {
        return false;
    }

    const ViewRankEntry& entry = state.entries[draw.itemID];
    RECT rect = draw.rcItem;
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    const bool alternate = (draw.itemID & 1u) != 0;
    HBRUSH brush = CreateSolidBrush(selected ? kViewRankSelectedBackground :
        (alternate ? RGB(10, 8, 5) : kViewRankBlack));
    if (brush != nullptr) {
        FillRect(draw.hDC, &rect, brush);
        DeleteObject(brush);
    }
    if (entry.name[0] == '\0') {
        return false;
    }

    if (selected) {
        RECT accent = rect;
        accent.right = accent.left + std::max<LONG>(3,
            (rect.bottom - rect.top) / 10);
        fill_view_rank_rect(draw.hDC, accent, kViewRankGold);
        frame_view_rank_rect(draw.hDC, rect, RGB(128, 91, 38));
    }
    RECT separator = rect;
    separator.top = std::max(separator.top, separator.bottom - 1);
    fill_view_rank_rect(draw.hDC, separator,
        selected ? RGB(112, 77, 31) : RGB(37, 29, 17));

    SetTextColor(draw.hDC, selected ? RGB(255, 220, 109) : kViewRankGray);
    SetBkMode(draw.hDC, TRANSPARENT);

    char text[128]{};
    std::snprintf(text, sizeof(text), "%u",
        static_cast<unsigned>(state.top_rank_offset + draw.itemID + 1));
    draw_column_text(draw.hDC, rect, state.columns[0], text,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_column_text(draw.hDC, rect, state.columns[1], entry.name.data(),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(entry.points));
    draw_column_text(draw.hDC, rect, state.columns[2], text,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_column_text(draw.hDC, rect, state.columns[3], entry.record.data(),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(entry.rating));
    draw_column_text(draw.hDC, rect, state.columns[4], text,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return true;
}

void draw_tab_button(ViewRankState& state, int id, const DRAWITEMSTRUCT& draw) {
    LegacyImageButtonControl* button = button_for_id(state, id);
    if (button == nullptr) {
        return;
    }
    if (id != kViewRankNormalTabButtonId) {
        const bool selected = type_for_tab_id(id) == state.selected_type;
        StretchBitmapMemoryResourceToDc(
            selected ? button->pressed_bitmap : button->normal_bitmap,
            draw.hDC, 0, 0);
        return;
    }

    // The legacy rank browser had separate Normal, Avatar, and Guild bitmap
    // labels.  Draw the reconstructed single category from scratch so none of
    // the old label or underline pixels can remain around its edge.
    RECT tab_rect = draw.rcItem;
    HBRUSH tab_brush = CreateSolidBrush(RGB(24, 21, 16));
    if (tab_brush != nullptr) {
        FillRect(draw.hDC, &tab_rect, tab_brush);
        DeleteObject(tab_brush);
    }
    for (COLORREF color : {
             RGB(9, 7, 5), RGB(133, 102, 58), RGB(48, 38, 25)}) {
        HBRUSH frame_brush = CreateSolidBrush(color);
        if (frame_brush != nullptr) {
            FrameRect(draw.hDC, &tab_rect, frame_brush);
            DeleteObject(frame_brush);
        }
        InflateRect(&tab_rect, -1, -1);
    }

    RECT label_rect = draw.rcItem;
    InflateRect(&label_rect, -4, -4);
    const int font_height = std::max(18, static_cast<int>(
        (draw.rcItem.bottom - draw.rcItem.top) / 2));
    HFONT font = CreateFontW(-font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, HANGEUL_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
    HGDIOBJ old_font = font != nullptr ? SelectObject(draw.hDC, font) : nullptr;
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, kViewRankSoftWhite);
    DrawTextW(draw.hDC, L"\ub7ad\ud0b9", -1, &label_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old_font != nullptr) {
        SelectObject(draw.hDC, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
}

bool paint_background_if_current(ViewRankState& state, HWND hwnd) {
    if (hwnd != state.window) {
        return false;
    }
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    paint_view_rank_theme(state, hwnd, dc);
    EndPaint(hwnd, &paint);
    return true;
}

void release_window_resources(ViewRankState& state) {
    RestoreViewRankAccelerators(state);
    DestroyViewRankBackgroundBitmap(state);
    DestroyViewRankImageButtons(state);
    destroy_control(state.search_edit);
    destroy_control(state.list_box);
    state.window = nullptr;
    state.visible = false;
}

void handle_connection_closed(ViewRankState& state) {
    show_message(state, startup_message_row(5, "Disconnected from the server."),
        RGB(10, 10, 250));
    close_async_socket(state);
    HWND old_window = state.window;
    HWND parent = state.parent_window;
    if (old_window != nullptr) {
        DestroyWindow(old_window);
    }
    if (parent != nullptr) {
        DestroyWindow(parent);
    }
    open_connect_frontend(state);
}

void apply_rank_payload(ViewRankState& state, const u8* payload, i32 byte_count) {
    state.top_rank_offset = read_le32(payload, byte_count, 0x0d);
    state.entries = {};
    const std::size_t available = byte_count > 0x11 ?
        static_cast<std::size_t>(byte_count - 0x11) : 0;
    const std::size_t copy_count = std::min(available,
        state.entries.size() * kViewRankEntryBytes);
    for (std::size_t i = 0; i < state.entries.size(); ++i) {
        const std::size_t offset = 0x11 + i * kViewRankEntryBytes;
        if (offset >= static_cast<std::size_t>(byte_count) ||
            offset - 0x11 >= copy_count) {
            break;
        }
        const u8* row = payload + offset;
        copy_to_array(state.entries[i].name, reinterpret_cast<const char*>(row));
        if (offset + 0x24 <= static_cast<std::size_t>(byte_count)) {
            state.entries[i].points = read_le32(payload, byte_count, offset + 0x20);
        }
        if (offset + 0x34 <= static_cast<std::size_t>(byte_count)) {
            copy_to_array(state.entries[i].record,
                reinterpret_cast<const char*>(payload + offset + 0x24));
        }
        if (offset + 0x38 <= static_cast<std::size_t>(byte_count)) {
            state.entries[i].rating = read_le32(payload, byte_count, offset + 0x34);
        }
    }
    state.has_next_page = state.entries.back().name[0] != '\0';
    sync_list_box(state);
    RedrawViewRankList(state);
}

} // namespace

ViewRankState& view_rank_state() {
    return g_view_rank_state;
}

void InitializeViewRankBackgroundStatic(ViewRankState& state) {
    InitializeViewRankBackgroundBitmap(state);
    RegisterViewRankBackgroundDestructor(state);
}

void InitializeViewRankBackgroundBitmap(ViewRankState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterViewRankBackgroundDestructor(ViewRankState&) {
    register_atexit_once(g_background_destructor_registered,
        shutdown_global_background);
}

void DestroyViewRankBackgroundBitmap(ViewRankState& state) {
    HandleBitmapMemoryResourceDestructor(state.background);
}

void ShutdownViewRankBackgroundBitmap(ViewRankState& state) {
    DestroyViewRankBackgroundBitmap(state);
}

#define DEFINE_VIEW_RANK_BUTTON_LIFETIME(StaticName, InitName, RegisterName, DestroyName, Slot) \
    void StaticName(ViewRankState& state) { \
        InitName(state); \
        RegisterName(state); \
    } \
    void InitName(ViewRankState& state) { \
        InitializeLegacyImageButtonControl(state.buttons[Slot]); \
    } \
    void RegisterName(ViewRankState&) { \
        register_atexit_once(g_button_destructor_registered[Slot], \
            shutdown_global_button_slot<Slot>); \
    } \
    void DestroyName(ViewRankState& state) { \
        destroy_view_rank_button_slot(state, Slot); \
    }

DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankUpButtonStatic,
    InitializeViewRankUpButton,
    RegisterViewRankUpButtonDestructor,
    DestroyViewRankUpButton, 0)
DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankDownButtonStatic,
    InitializeViewRankDownButton,
    RegisterViewRankDownButtonDestructor,
    DestroyViewRankDownButton, 1)
DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankSearchButtonStatic,
    InitializeViewRankSearchButton,
    RegisterViewRankSearchButtonDestructor,
    DestroyViewRankSearchButton, 2)
DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankCloseButtonStatic,
    InitializeViewRankCloseButton,
    RegisterViewRankCloseButtonDestructor,
    DestroyViewRankCloseButton, 3)
DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankGoSiteButtonStatic,
    InitializeViewRankGoSiteButton,
    RegisterViewRankGoSiteButtonDestructor,
    DestroyViewRankGoSiteButton, 4)
DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankNormalTabButtonStatic,
    InitializeViewRankNormalTabButton,
    RegisterViewRankNormalTabButtonDestructor,
    DestroyViewRankNormalTabButton, 5)
DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankAvatarTabButtonStatic,
    InitializeViewRankAvatarTabButton,
    RegisterViewRankAvatarTabButtonDestructor,
    DestroyViewRankAvatarTabButton, 6)
DEFINE_VIEW_RANK_BUTTON_LIFETIME(InitializeViewRankGuildTabButtonStatic,
    InitializeViewRankGuildTabButton,
    RegisterViewRankGuildTabButtonDestructor,
    DestroyViewRankGuildTabButton, 7)

#undef DEFINE_VIEW_RANK_BUTTON_LIFETIME

void InitializeViewRankImageButtons(ViewRankState& state) {
    InitializeViewRankUpButtonStatic(state);
    InitializeViewRankDownButtonStatic(state);
    InitializeViewRankSearchButtonStatic(state);
    InitializeViewRankCloseButtonStatic(state);
    InitializeViewRankGoSiteButtonStatic(state);
    InitializeViewRankNormalTabButtonStatic(state);
    InitializeViewRankAvatarTabButtonStatic(state);
    InitializeViewRankGuildTabButtonStatic(state);
    for (UiPngResource& image : state.theme_button_images) {
        InitializeUiPngResource(image);
    }
    state.theme_button_images_loaded = true;
    for (std::size_t i = 0; i < state.theme_button_images.size(); ++i) {
        if (!LoadUiPngResourceFromExecutableRelativeFile(
                state.theme_button_images[i],
                kViewRankThemeButtonImagePaths[i])) {
            state.theme_button_images_loaded = false;
            break;
        }
    }
    if (!state.theme_button_images_loaded) {
        for (UiPngResource& image : state.theme_button_images) {
            ReleaseUiPngResource(image);
        }
    }
    state.theme_font = CreateScaledFrontendUiFont(2);
    state.theme_search_font = CreateScaledFrontendUiFont(3);
    state.theme_title_font = CreateScaledFrontendUiFont(4);
    state.theme_hot_button = nullptr;
}

void DestroyViewRankImageButtons(ViewRankState& state) {
    DestroyViewRankUpButton(state);
    DestroyViewRankDownButton(state);
    DestroyViewRankSearchButton(state);
    DestroyViewRankCloseButton(state);
    DestroyViewRankGoSiteButton(state);
    DestroyViewRankNormalTabButton(state);
    DestroyViewRankAvatarTabButton(state);
    DestroyViewRankGuildTabButton(state);
    state.theme_hot_button = nullptr;
    state.theme_button_images_loaded = false;
    for (UiPngResource& image : state.theme_button_images) {
        ReleaseUiPngResource(image);
    }
    if (state.theme_font != nullptr) {
        DeleteObject(state.theme_font);
        state.theme_font = nullptr;
    }
    if (state.theme_search_font != nullptr) {
        DeleteObject(state.theme_search_font);
        state.theme_search_font = nullptr;
    }
    if (state.theme_title_font != nullptr) {
        DeleteObject(state.theme_title_font);
        state.theme_title_font = nullptr;
    }
}

void InstallViewRankAccelerators(ViewRankState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kViewRankAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreViewRankAccelerators(ViewRankState& state) {
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

void RedrawViewRankList(ViewRankState& state) {
    if (state.list_box.window != nullptr) {
        RedrawWindow(state.list_box.window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

void QueueViewRankListRequest(ViewRankState& state, ViewRankListType type,
    u32 top_rank_offset) {
    if (static_cast<u32>(type) > static_cast<u32>(ViewRankListType::Guild)) {
        return;
    }
    state.selected_type = type;
    state.top_rank_offset = top_rank_offset;

    std::vector<u8> packet(0x15, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x2f);
    write_le32(packet, 8, 0x15);
    write_le32(packet, 0x0d, top_rank_offset);
    write_le32(packet, 0x11, static_cast<u32>(type));
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));

    if (LegacyImageButtonControl* button =
            button_for_id(state, kViewRankNormalTabButtonId)) {
        if (button->window != nullptr) {
            RedrawWindow(button->window, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW);
        }
    }
    RedrawViewRankList(state);
}

void QueueViewRankSearchRequest(ViewRankState& state) {
    GetWindowTextA(state.search_edit.window, state.search_name.data(),
        static_cast<int>(state.search_name.size()));
    std::vector<u8> packet(0x31, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x33);
    write_le32(packet, 8, 0x31);
    write_le32(packet, 0x0d, static_cast<u32>(state.selected_type));
    copy_fixed_string(packet, 0x11, state.search_name.size(),
        state.search_name.data());
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void QueueViewRankSiteRequest(ViewRankState& state) {
    std::vector<u8> packet(0x0d, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x31);
    write_le32(packet, 8, 0x0d);
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

void QueueViewRankDetailRequest(ViewRankState& state, int visible_row) {
    if (visible_row < 0 ||
        static_cast<std::size_t>(visible_row) >= state.entries.size() ||
        state.selected_type == ViewRankListType::Guild) {
        return;
    }
    const ViewRankEntry& entry = state.entries[static_cast<std::size_t>(visible_row)];
    if (entry.name[0] == '\0') {
        return;
    }
    copy_to_array(state.selected_detail_name, entry.name.data());
    std::vector<u8> packet(0x2d, 0);
    write_le32(packet, 0, 3);
    write_le32(packet, 4, 0x37);
    write_le32(packet, 8, 0x2d);
    copy_fixed_string(packet, 0x0d, state.selected_detail_name.size(),
        state.selected_detail_name.data());
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

bool CreateViewRankWindow(ViewRankState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context, LegacyAsyncTcpSocket* async_tcp_socket) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.async_tcp_socket = async_tcp_socket;
    state.top_rank_offset = 0;
    state.selected_type = ViewRankListType::Normal;
    state.has_next_page = false;
    state.last_detail_payload.clear();
    state.entries = {};
    state.visible = false;

    InitializeViewRankBackgroundStatic(state);
    InitializeViewRankImageButtons(state);
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kViewRankLayoutTrcRecord)) {
        release_window_resources(state);
        return false;
    }
    assign_layout(state, layout.table);
    if (load_reconstructed_view_rank_background(state)) {
        apply_view_rank_png_layout(state);
    }
    compute_columns(state);

    const ViewRankLayoutRect window_rect = layout_at(state, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(
              parent, window_rect.width, window_rect.height)
        : RankerFrontendWindowOrigin();
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "ViewRank", "ViewRank",
        style, origin.x, origin.y, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        release_window_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(view_rank_window_proc));

    if (state.async_tcp_socket != nullptr &&
        !RegisterLegacyAsyncTcpSocketEvents(*state.async_tcp_socket, state.window,
            kViewRankNetworkMessage, FD_READ | FD_WRITE | FD_CLOSE)) {
        DestroyWindow(state.window);
        return false;
    }

    if (!create_text_control(state.list_box, state.window, instance, "listbox",
            kListBoxStyle, kViewRankListBoxId, layout_at(state, 1)) ||
        !create_text_control(state.search_edit, state.window, instance, "edit",
            kSearchEditStyle, kViewRankSearchEditId, layout_at(state, 2))) {
        DestroyWindow(state.window);
        return false;
    }
    SendMessageA(state.search_edit.window, EM_LIMITTEXT, 0x1f, 0);

    for (const ViewRankButtonSpec& spec : kButtonSpecs) {
        if (IsViewRankRemovedButtonId(spec.id)) {
            continue;
        }
        if (!create_image_button(state, spec)) {
            DestroyWindow(state.window);
            return false;
        }
    }

    HFONT theme_font = state.theme_font != nullptr ? state.theme_font :
        reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(theme_font), TRUE);
    SendMessageA(state.list_box.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(theme_font), TRUE);
    HFONT search_font = state.theme_search_font != nullptr ?
        state.theme_search_font : theme_font;
    SendMessageA(state.search_edit.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(search_font), TRUE);

    InstallViewRankAccelerators(state);
    ShowWindow(state.list_box.window, SW_SHOW);
    if (state.search_name[0] != '\0') {
        SetWindowTextA(state.search_edit.window, state.search_name.data());
    }
    ShowWindow(state.search_edit.window, SW_SHOW);
    SetFocus(state.search_edit.window);
    sync_list_box(state);
    QueueViewRankListRequest(state, ViewRankListType::Normal, 0);
    state.visible = true;
    return true;
}

void DispatchViewRankNetworkMessage(ViewRankState& state, WPARAM wparam,
    LPARAM lparam) {
    const u16 event = LOWORD(lparam);
    if (event == 0x20) {
        handle_connection_closed(state);
        return;
    }
    if (event != 1) {
        return;
    }

    auto handle_payload = [&](const u8* payload, i32 byte_count) -> bool {
        if (payload == nullptr || byte_count < 0x0d) {
            return true;
        }
        const u32 opcode = read_le32(payload, byte_count, 4);
        switch (opcode) {
        case 0x30:
            apply_rank_payload(state, payload, byte_count);
            break;
        case 0x32:
            open_url(state, reinterpret_cast<const char*>(payload + 0x0d));
            break;
        case 0x34:
        case 0x38:
            state.last_detail_payload.assign(payload, payload + byte_count);
            if (state.callbacks.open_detail != nullptr) {
                state.callbacks.open_detail(state, payload, byte_count);
            }
            else {
                CreatePlayerProfileWindow(player_profile_state(), state.window,
                    state.instance, payload, static_cast<std::size_t>(byte_count),
                    state.selected_detail_name.data(), "", state.async_tcp_socket);
            }
            break;
        default:
            forward_view_rank_network_message(state, wparam, lparam, payload,
                byte_count);
            return false;
        }
        return true;
    };

    if (state.callbacks.receive_payload != nullptr) {
        i32 byte_count = 0;
        const u8* payload = state.callbacks.receive_payload(state, byte_count);
        handle_payload(payload, byte_count);
        return;
    }
    if (state.async_tcp_socket == nullptr) {
        return;
    }

    ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    while (payload != nullptr && byte_count >= 0x0d) {
        const u32 packet_bytes = read_le32(payload, byte_count, 8);
        if (packet_bytes < 0x0d ||
            packet_bytes > static_cast<u32>(byte_count)) {
            break;
        }
        const auto packet_count = static_cast<i32>(packet_bytes);
        if (!handle_payload(payload, packet_count)) {
            return;
        }
        ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket, packet_count);
        payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
    }
}

LRESULT HandleViewRankWindowMessage(ViewRankState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        release_window_resources(state);
        return 0;
    case WM_ERASEBKGND:
        if (hwnd == state.window) {
            paint_view_rank_theme(state, hwnd, reinterpret_cast<HDC>(wparam));
            return TRUE;
        }
        break;
    case WM_PAINT:
        if (paint_background_if_current(state, hwnd)) {
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (draw->CtlID == kViewRankListBoxId) {
            if (draw_rank_entry(state, *draw)) {
                return TRUE;
            }
            break;
        }
        if (IsViewRankThemedButtonId(static_cast<int>(draw->CtlID)) &&
            draw_view_rank_themed_button(state, *draw)) {
            return TRUE;
        }
        if (is_tab_id(static_cast<int>(draw->CtlID))) {
            draw_tab_button(state, static_cast<int>(draw->CtlID), *draw);
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
        case kViewRankSearchButtonId:
            play_click_sound(state);
            QueueViewRankSearchRequest(state);
            break;
        case kViewRankUpButtonId: {
            play_click_sound(state);
            const u32 top = state.top_rank_offset <= kViewRankVisibleRows ?
                0 : state.top_rank_offset - static_cast<u32>(kViewRankVisibleRows);
            QueueViewRankListRequest(state, state.selected_type, top);
            break;
        }
        case kViewRankDownButtonId:
            play_click_sound(state);
            if (state.has_next_page) {
                QueueViewRankListRequest(state, state.selected_type,
                    state.top_rank_offset + static_cast<u32>(kViewRankVisibleRows));
            }
            break;
        case kViewRankCloseButtonId:
            play_click_sound(state);
            if (state.window != nullptr) {
                DestroyWindow(state.window);
            }
            // Closing a WizardNet-owned rank browser must also restore the
            // control socket's async notifications to the still-live lobby.
            // The callback falls back to the Connect frontend when this rank
            // window was opened outside WizardNet.
            open_connect_frontend(state);
            break;
        case kViewRankNormalTabButtonId:
            play_click_sound(state);
            QueueViewRankListRequest(state, ViewRankListType::Normal, 0);
            break;
        case kViewRankAvatarTabButtonId:
        case kViewRankGuildTabButtonId:
            // Retain the legacy IDs for layout/resource compatibility, but the
            // simplified browser has no hidden category navigation.
            break;
        case kViewRankListBoxId:
            if (notify == LBN_SELCHANGE || notify == LBN_SELCANCEL) {
                SetFocus(state.search_edit.window);
                break;
            }
            if (notify == LBN_DBLCLK) {
                LRESULT selected = SendMessageA(state.list_box.window,
                    LB_GETCURSEL, 0, 0);
                if (selected != LB_ERR) {
                    QueueViewRankDetailRequest(state, static_cast<int>(selected));
                }
                break;
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
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kViewRankWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kViewRankBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case kViewRankNetworkMessage:
        DispatchViewRankNetworkMessage(state, wparam, lparam);
        break;
    case kViewRankPromptMessage0:
        ShowOnlineModalPrompt0(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kViewRankPromptMessage1:
        ShowOnlineModalPrompt1(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kViewRankPromptMessage2:
        ShowOnlineModalPrompt2(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kViewRankPromptMessage3:
        ShowOnlineModalPrompt3(online_modal_prompt_state(), hwnd,
            reinterpret_cast<const char*>(wparam), static_cast<COLORREF>(lparam));
        break;
    case kViewRankPromptEndMessage:
        EndOnlineModalPrompt(online_modal_prompt_state(), static_cast<INT_PTR>(wparam));
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleViewRankControlMessage(ViewRankState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    if (IsViewRankThemedButtonId(id)) {
        switch (message) {
        case WM_MOUSEMOVE: {
            if (state.theme_hot_button != hwnd) {
                HWND previous = state.theme_hot_button;
                state.theme_hot_button = hwnd;
                if (previous != nullptr && IsWindow(previous)) {
                    RedrawWindow(previous, nullptr, nullptr,
                        RDW_INVALIDATE | RDW_NOERASE);
                }
                RedrawWindow(hwnd, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_NOERASE);
            }
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = hwnd;
            TrackMouseEvent(&tracking);
            break;
        }
        case WM_MOUSELEAVE:
            if (state.theme_hot_button == hwnd) {
                state.theme_hot_button = nullptr;
                RedrawWindow(hwnd, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_NOERASE);
            }
            break;
        case WM_ENABLE:
        case WM_CAPTURECHANGED:
            RedrawWindow(hwnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_NOERASE);
            break;
        case WM_NCDESTROY:
            if (state.theme_hot_button == hwnd) {
                state.theme_hot_button = nullptr;
            }
            break;
        default:
            break;
        }
    }
    switch (id) {
    case kViewRankSearchEditId:
    case kViewRankListBoxId:
    case kViewRankUpButtonId:
    case kViewRankDownButtonId:
    case kViewRankSearchButtonId:
    case kViewRankCloseButtonId:
    case kViewRankGoSiteButtonId:
    case kViewRankNormalTabButtonId:
    case kViewRankAvatarTabButtonId:
    case kViewRankGuildTabButtonId:
        return CallWindowProcA(original_proc_for_id(state, id), hwnd, message,
            wparam, lparam);
    default:
        return 0;
    }
}

} // namespace ranker

#endif
