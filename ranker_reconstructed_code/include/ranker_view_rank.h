#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"
#include "ranker_ui_png_resource.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr UINT kViewRankNetworkMessage = 0x465;
constexpr UINT kViewRankPromptMessage0 = 0x512;
constexpr UINT kViewRankPromptMessage1 = 0x513;
constexpr UINT kViewRankPromptMessage2 = 0x514;
constexpr UINT kViewRankPromptMessage3 = 0x515;
constexpr UINT kViewRankPromptEndMessage = 0x516;

constexpr int kViewRankSearchEditId = 0x1d4c;
constexpr int kViewRankListBoxId = 0x1d4d;
constexpr int kViewRankUpButtonId = 0x1d4e;
constexpr int kViewRankDownButtonId = 0x1d4f;
constexpr int kViewRankSearchButtonId = 0x1d50;
constexpr int kViewRankCloseButtonId = IDCANCEL;
constexpr int kViewRankGoSiteButtonId = 0x1d51;
constexpr int kViewRankNormalTabButtonId = 0x1d52;
constexpr int kViewRankAvatarTabButtonId = 0x1d53;
constexpr int kViewRankGuildTabButtonId = 0x1d54;
constexpr int kViewRankAcceleratorResourceId = 0x2ee;

constexpr bool IsViewRankRemovedButtonId(int control_id) {
    return control_id == kViewRankGoSiteButtonId ||
        control_id == kViewRankNormalTabButtonId ||
        control_id == kViewRankAvatarTabButtonId ||
        control_id == kViewRankGuildTabButtonId;
}

constexpr u32 kViewRankLayoutTrcRecord = 0x16c;
constexpr u32 kViewRankBackgroundBitmapRecord = 0xd0;
constexpr std::size_t kViewRankVisibleRows = 16;
constexpr std::size_t kViewRankEntryBytes = 0x38;
constexpr std::size_t kViewRankSearchNameBytes = 0x20;
constexpr std::size_t kViewRankThemeButtonVisualCount = 4;

enum class ViewRankThemeButtonVisual : std::size_t {
    Normal = 0,
    Hot = 1,
    Pressed = 2,
    Disabled = 3,
};

constexpr bool IsViewRankThemedButtonId(int control_id) {
    return control_id == kViewRankUpButtonId ||
        control_id == kViewRankDownButtonId ||
        control_id == kViewRankSearchButtonId ||
        control_id == kViewRankCloseButtonId;
}

constexpr ViewRankThemeButtonVisual ResolveViewRankThemeButtonVisual(
    bool enabled, bool pressed, bool hovered) {
    if (!enabled) {
        return ViewRankThemeButtonVisual::Disabled;
    }
    if (pressed) {
        return ViewRankThemeButtonVisual::Pressed;
    }
    return hovered ? ViewRankThemeButtonVisual::Hot :
        ViewRankThemeButtonVisual::Normal;
}

enum class ViewRankListType : u32 {
    Normal = 0,
    Avatar = 1,
    Guild = 2,
};

struct ViewRankState;

struct ViewRankLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

constexpr ViewRankLayoutRect ResolveViewRankPngButtonRect(int control_id) {
    switch (control_id) {
    case kViewRankSearchButtonId:
        return {294, 528, 84, 27};
    case kViewRankUpButtonId:
        return {414, 528, 84, 27};
    case kViewRankDownButtonId:
        return {504, 528, 84, 27};
    case kViewRankCloseButtonId:
        return {648, 528, 84, 27};
    default:
        return {};
    }
}

struct ViewRankColumn {
    int left = 0;
    int width = 0;
};

struct ViewRankEntry {
    std::array<char, 0x20> name{};
    u32 points = 0;
    std::array<char, 0x10> record{};
    u32 rating = 0;
};

struct ViewRankTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

using ViewRankActionCallback = void (*)(ViewRankState& state);
using ViewRankPacketCallback = void (*)(ViewRankState& state, const void* packet,
    i32 byte_count);
using ViewRankPayloadCallback = const u8* (*)(ViewRankState& state,
    i32& byte_count);
using ViewRankMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);
using ViewRankUrlCallback = void (*)(ViewRankState& state, const char* url);
using ViewRankPayloadRouteCallback = void (*)(ViewRankState& state,
    const u8* payload, i32 byte_count);

struct ViewRankCallbacks {
    ViewRankPacketCallback queue_packet = nullptr;
    ViewRankPayloadCallback receive_payload = nullptr;
    ViewRankActionCallback close_async_socket = nullptr;
    ViewRankActionCallback open_connect_frontend = nullptr;
    ViewRankActionCallback play_click_sound = nullptr;
    ViewRankMessageCallback show_message = nullptr;
    ViewRankUrlCallback open_url = nullptr;
    ViewRankPayloadRouteCallback open_detail = nullptr;
    ViewRankPayloadRouteCallback forward_payload = nullptr;
};

struct ViewRankState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    ViewRankTextControl search_edit;
    ViewRankTextControl list_box;
    std::array<LegacyImageButtonControl, 8> buttons{};
    std::array<UiPngResource,
        kViewRankThemeButtonVisualCount> theme_button_images{};
    std::vector<ViewRankLayoutRect> layout;
    std::array<ViewRankColumn, 5> columns{};
    std::array<ViewRankEntry, kViewRankVisibleRows> entries{};

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<char, kViewRankSearchNameBytes> search_name{};
    std::array<char, kViewRankSearchNameBytes> selected_detail_name{};
    std::vector<u8> last_detail_payload;
    std::string last_message;
    u32 top_rank_offset = 0;
    ViewRankListType selected_type = ViewRankListType::Normal;
    HFONT theme_font = nullptr;
    HFONT theme_search_font = nullptr;
    HFONT theme_title_font = nullptr;
    HWND theme_hot_button = nullptr;
    bool theme_button_images_loaded = false;
    bool has_next_page = false;
    bool visible = false;
    ViewRankCallbacks callbacks{};
};

ViewRankState& view_rank_state();

void InitializeViewRankBackgroundStatic(ViewRankState& state);
void InitializeViewRankBackgroundBitmap(ViewRankState& state);
void RegisterViewRankBackgroundDestructor(ViewRankState& state);
void DestroyViewRankBackgroundBitmap(ViewRankState& state);
void ShutdownViewRankBackgroundBitmap(ViewRankState& state);
void InitializeViewRankUpButtonStatic(ViewRankState& state);
void InitializeViewRankUpButton(ViewRankState& state);
void RegisterViewRankUpButtonDestructor(ViewRankState& state);
void DestroyViewRankUpButton(ViewRankState& state);
void InitializeViewRankDownButtonStatic(ViewRankState& state);
void InitializeViewRankDownButton(ViewRankState& state);
void RegisterViewRankDownButtonDestructor(ViewRankState& state);
void DestroyViewRankDownButton(ViewRankState& state);
void InitializeViewRankSearchButtonStatic(ViewRankState& state);
void InitializeViewRankSearchButton(ViewRankState& state);
void RegisterViewRankSearchButtonDestructor(ViewRankState& state);
void DestroyViewRankSearchButton(ViewRankState& state);
void InitializeViewRankCloseButtonStatic(ViewRankState& state);
void InitializeViewRankCloseButton(ViewRankState& state);
void RegisterViewRankCloseButtonDestructor(ViewRankState& state);
void DestroyViewRankCloseButton(ViewRankState& state);
void InitializeViewRankGoSiteButtonStatic(ViewRankState& state);
void InitializeViewRankGoSiteButton(ViewRankState& state);
void RegisterViewRankGoSiteButtonDestructor(ViewRankState& state);
void DestroyViewRankGoSiteButton(ViewRankState& state);
void InitializeViewRankNormalTabButtonStatic(ViewRankState& state);
void InitializeViewRankNormalTabButton(ViewRankState& state);
void RegisterViewRankNormalTabButtonDestructor(ViewRankState& state);
void DestroyViewRankNormalTabButton(ViewRankState& state);
void InitializeViewRankAvatarTabButtonStatic(ViewRankState& state);
void InitializeViewRankAvatarTabButton(ViewRankState& state);
void RegisterViewRankAvatarTabButtonDestructor(ViewRankState& state);
void DestroyViewRankAvatarTabButton(ViewRankState& state);
void InitializeViewRankGuildTabButtonStatic(ViewRankState& state);
void InitializeViewRankGuildTabButton(ViewRankState& state);
void RegisterViewRankGuildTabButtonDestructor(ViewRankState& state);
void DestroyViewRankGuildTabButton(ViewRankState& state);
void InitializeViewRankImageButtons(ViewRankState& state);
void DestroyViewRankImageButtons(ViewRankState& state);
void InstallViewRankAccelerators(ViewRankState& state);
void RestoreViewRankAccelerators(ViewRankState& state);
void RedrawViewRankList(ViewRankState& state);

void QueueViewRankListRequest(ViewRankState& state, ViewRankListType type,
    u32 top_rank_offset);
void QueueViewRankSearchRequest(ViewRankState& state);
void QueueViewRankSiteRequest(ViewRankState& state);
void QueueViewRankDetailRequest(ViewRankState& state, int visible_row);

bool CreateViewRankWindow(ViewRankState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context, LegacyAsyncTcpSocket* async_tcp_socket);
void DispatchViewRankNetworkMessage(ViewRankState& state, WPARAM wparam,
    LPARAM lparam);
LRESULT HandleViewRankWindowMessage(ViewRankState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT HandleViewRankControlMessage(ViewRankState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);

#endif

}
