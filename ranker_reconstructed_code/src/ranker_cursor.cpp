#include "ranker_cursor.h"

#ifdef _WIN32
#include "ranker_directx.h"
#include "ranker_screenshot.h"

#include <algorithm>

namespace ranker {
namespace {

SoftwareCursorState g_cursor_state;

template <typename T>
void release_com(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

bool cursor_index_valid(u32 cursor_index) {
    return cursor_index < kSoftwareCursorSurfaceCount;
}

void update_cursor_draw_position() {
    if (!cursor_index_valid(g_cursor_state.cursor_index)) {
        g_cursor_state.cursor_x = g_cursor_state.pointer_x;
        g_cursor_state.cursor_y = g_cursor_state.pointer_y;
        return;
    }

    g_cursor_state.cursor_x =
        g_cursor_state.pointer_x - g_cursor_state.hotspot_x[g_cursor_state.cursor_index];
    g_cursor_state.cursor_y =
        g_cursor_state.pointer_y - g_cursor_state.hotspot_y[g_cursor_state.cursor_index];
}

POINT clipped_destination_point(i32 x, i32 y) {
    POINT point{};
    point.x = std::max<i32>(x, 0);
    point.y = std::max<i32>(y, 0);
    return point;
}

RECT backup_source_rect_for_restore(i32 x, i32 y) {
    const auto& dd = direct_draw_state();
    const POINT dest = clipped_destination_point(x, y);
    RECT rect{};
    rect.left = 0;
    rect.top = 0;
    rect.right = static_cast<LONG>(
        dd.width < static_cast<u32>(dest.x + kSoftwareCursorSize) ?
            static_cast<i32>(dd.width) - dest.x :
            static_cast<i32>(kSoftwareCursorSize));
    rect.bottom = static_cast<LONG>(
        dd.height < static_cast<u32>(dest.y + kSoftwareCursorSize) ?
            static_cast<i32>(dd.height) - dest.y :
            static_cast<i32>(kSoftwareCursorSize));
    return rect;
}

RECT screen_source_rect_for_backup(i32 x, i32 y) {
    const auto& dd = direct_draw_state();
    RECT rect{};
    rect.left = std::max<LONG>(x, 0);
    rect.top = std::max<LONG>(y, 0);
    rect.right = static_cast<LONG>(
        static_cast<u32>(x) + kSoftwareCursorSize < dd.width ?
            rect.left + static_cast<LONG>(kSoftwareCursorSize) :
            static_cast<LONG>(dd.width));
    rect.bottom = static_cast<LONG>(
        static_cast<u32>(y) + kSoftwareCursorSize < dd.height ?
            rect.top + static_cast<LONG>(kSoftwareCursorSize) :
            static_cast<LONG>(dd.height));
    return rect;
}

RECT cursor_source_rect(i32 x, i32 y) {
    const auto& dd = direct_draw_state();
    RECT rect{};
    rect.left = x < 0 ? static_cast<LONG>(-x) : 0;
    rect.top = y < 0 ? static_cast<LONG>(-y) : 0;
    rect.right = static_cast<LONG>(
        dd.width < static_cast<u32>(x) + kSoftwareCursorSize ?
            static_cast<i32>(dd.width) - x :
            static_cast<i32>(kSoftwareCursorSize));
    rect.bottom = static_cast<LONG>(
        dd.height < static_cast<u32>(y) + kSoftwareCursorSize ?
            static_cast<i32>(dd.height) - y :
            static_cast<i32>(kSoftwareCursorSize));
    return rect;
}

HRESULT blt_fast(LPDIRECTDRAWSURFACE7 dest, DWORD x, DWORD y, LPDIRECTDRAWSURFACE7 source,
    RECT* source_rect, DWORD flags) {
    if (dest == nullptr || source == nullptr || source_rect == nullptr) {
        g_cursor_state.last_result = DDERR_GENERIC;
        return g_cursor_state.last_result;
    }

    g_cursor_state.last_result = dest->BltFast(x, y, source, source_rect, flags);
    return g_cursor_state.last_result;
}

HRESULT copy_backup_to_surface(LPDIRECTDRAWSURFACE7 dest, LPDIRECTDRAWSURFACE7 backup,
    i32 x, i32 y) {
    RECT rect = backup_source_rect_for_restore(x, y);
    const POINT dest_point = clipped_destination_point(x, y);
    return blt_fast(dest, static_cast<DWORD>(dest_point.x), static_cast<DWORD>(dest_point.y),
        backup, &rect, DDBLTFAST_NOCOLORKEY);
}

HRESULT copy_surface_to_backup(LPDIRECTDRAWSURFACE7 backup, LPDIRECTDRAWSURFACE7 source,
    i32 x, i32 y) {
    RECT rect = screen_source_rect_for_backup(x, y);
    return blt_fast(backup, 0, 0, source, &rect, DDBLTFAST_NOCOLORKEY);
}

HRESULT draw_cursor_surface(LPDIRECTDRAWSURFACE7 dest, i32 x, i32 y) {
    if (!cursor_index_valid(g_cursor_state.cursor_index)) {
        g_cursor_state.last_result = DD_OK;
        return g_cursor_state.last_result;
    }
    RECT rect = cursor_source_rect(x, y);
    const POINT dest_point = clipped_destination_point(x, y);
    return blt_fast(dest, static_cast<DWORD>(dest_point.x), static_cast<DWORD>(dest_point.y),
        g_cursor_state.cursor_surfaces[g_cursor_state.cursor_index], &rect,
        DDBLTFAST_SRCCOLORKEY);
}

bool create_cursor_surface(LPDIRECTDRAW7 direct_draw, LPDIRECTDRAWSURFACE7& surface) {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = kSoftwareCursorSize;
    desc.dwHeight = kSoftwareCursorSize;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

    g_cursor_state.last_result = direct_draw->CreateSurface(&desc, &surface, nullptr);
    return SUCCEEDED(g_cursor_state.last_result);
}

} // namespace

SoftwareCursorState& software_cursor_state() {
    return g_cursor_state;
}

bool InitializeSoftwareCursorSurfaces() {
    if (g_cursor_state.surfaces_initialized) {
        return true;
    }

    auto* direct_draw = direct_draw_state().direct_draw;
    if (direct_draw == nullptr) {
        g_cursor_state.last_result = DDERR_GENERIC;
        return false;
    }

    if (!create_cursor_surface(direct_draw, g_cursor_state.primary_backup_surface) ||
        !create_cursor_surface(direct_draw, g_cursor_state.back_backup_surface)) {
        ShutdownSoftwareCursorSurfaces();
        return false;
    }

    DDCOLORKEY color_key{};
    for (auto*& surface : g_cursor_state.cursor_surfaces) {
        if (!create_cursor_surface(direct_draw, surface)) {
            ShutdownSoftwareCursorSurfaces();
            return false;
        }
        g_cursor_state.last_result = surface->SetColorKey(DDCKEY_SRCBLT, &color_key);
        if (FAILED(g_cursor_state.last_result)) {
            ShutdownSoftwareCursorSurfaces();
            return false;
        }
    }

    g_cursor_state.surfaces_initialized = true;
    return true;
}

void ShutdownSoftwareCursorSurfaces() {
    release_com(g_cursor_state.primary_backup_surface);
    release_com(g_cursor_state.back_backup_surface);
    for (auto*& surface : g_cursor_state.cursor_surfaces) {
        release_com(surface);
    }
    g_cursor_state.surfaces_initialized = false;
    g_cursor_state.visible = false;
}

void SetGameCursorHotspot(u32 cursor_index, i32 x, i32 y) {
    if (!cursor_index_valid(cursor_index)) {
        return;
    }
    g_cursor_state.hotspot_x[cursor_index] = x;
    g_cursor_state.hotspot_y[cursor_index] = y;
    if (cursor_index == g_cursor_state.cursor_index) {
        update_cursor_draw_position();
    }
}

void SetGameCursorPointerPosition(i32 x, i32 y) {
    if (g_cursor_state.cursor_change_depth != 0) {
        ++g_cursor_state.cursor_change_depth;
        return;
    }
    if (g_cursor_state.pointer_updates_suppressed) {
        return;
    }

    g_cursor_state.pointer_x = x;
    g_cursor_state.pointer_y = y;
    g_cursor_state.previous_cursor_x = g_cursor_state.cursor_x;
    g_cursor_state.previous_cursor_y = g_cursor_state.cursor_y;
    update_cursor_draw_position();

    if (g_cursor_state.visible) {
        g_cursor_state.pointer_motion_locked = true;
        if (!g_cursor_state.presentation_locked) {
            HandlePrimaryCursorBackgroundRestore();
            HandlePrimaryCursorBackgroundSave();
            HandleCurrentCursorDrawOnPrimary();
        }
        g_cursor_state.pointer_motion_locked = false;
    }
}

void RestoreSystemCursorPosition() {
    SetCursorPos(g_cursor_state.pointer_x, g_cursor_state.pointer_y);
}

void ShowGameCursor() {
    if (g_cursor_state.visible) {
        return;
    }
    update_cursor_draw_position();
    g_cursor_state.visible = true;
    HandlePrimaryCursorBackgroundSave();
    HandleCurrentCursorDrawOnPrimary();
}

void HideGameCursor() {
    if (!g_cursor_state.visible) {
        return;
    }
    g_cursor_state.previous_cursor_x = g_cursor_state.cursor_x;
    g_cursor_state.previous_cursor_y = g_cursor_state.cursor_y;
    HandlePrimaryCursorBackgroundRestore();
    g_cursor_state.visible = false;
}

void SetGameCursorIndex(u32 cursor_index) {
    if (!cursor_index_valid(cursor_index) || g_cursor_state.cursor_index == cursor_index) {
        return;
    }

    ++g_cursor_state.cursor_change_depth;
    g_cursor_state.previous_cursor_x = g_cursor_state.cursor_x;
    g_cursor_state.previous_cursor_y = g_cursor_state.cursor_y;
    if (g_cursor_state.visible) {
        HandlePrimaryCursorBackgroundRestore();
    }

    g_cursor_state.cursor_index = cursor_index;
    update_cursor_draw_position();
    if (g_cursor_state.visible) {
        HandlePrimaryCursorBackgroundSave();
        HandleCurrentCursorDrawOnPrimary();
    }

    if (g_cursor_state.cursor_change_depth != 0) {
        --g_cursor_state.cursor_change_depth;
        if (g_cursor_state.cursor_change_depth != 0) {
            g_cursor_state.cursor_change_depth = 0;
        }
    }
}

HRESULT LockAndUnlockSoftwareCursorSurface(u32 cursor_index) {
    if (!cursor_index_valid(cursor_index)) {
        g_cursor_state.last_result = DDERR_GENERIC;
        return g_cursor_state.last_result;
    }
    if (!g_cursor_state.surfaces_initialized && !InitializeSoftwareCursorSurfaces()) {
        return g_cursor_state.last_result;
    }

    LPDIRECTDRAWSURFACE7 surface = g_cursor_state.cursor_surfaces[cursor_index];
    if (surface == nullptr) {
        g_cursor_state.last_result = DDERR_GENERIC;
        return g_cursor_state.last_result;
    }

    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = kSoftwareCursorSize;
    desc.dwHeight = kSoftwareCursorSize;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    g_cursor_state.last_result = surface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
    if (SUCCEEDED(g_cursor_state.last_result)) {
        g_cursor_state.last_result = surface->Unlock(nullptr);
    }
    return g_cursor_state.last_result;
}

HRESULT HandlePrimaryCursorBackgroundRestore() {
    return copy_backup_to_surface(direct_draw_state().primary_surface,
        g_cursor_state.primary_backup_surface, g_cursor_state.previous_cursor_x,
        g_cursor_state.previous_cursor_y);
}

HRESULT HandlePrimaryCursorBackgroundSave() {
    return copy_surface_to_backup(g_cursor_state.primary_backup_surface,
        direct_draw_state().primary_surface, g_cursor_state.cursor_x, g_cursor_state.cursor_y);
}

HRESULT HandlePrimaryCursorBackupRefreshUsingBackBuffer() {
    return copy_surface_to_backup(g_cursor_state.primary_backup_surface,
        direct_draw_state().back_surface, g_cursor_state.presented_cursor_x,
        g_cursor_state.presented_cursor_y);
}

HRESULT HandlePrimaryRefreshUsingPresentedBackBackup() {
    return copy_backup_to_surface(direct_draw_state().primary_surface,
        g_cursor_state.back_backup_surface, g_cursor_state.presented_cursor_x,
        g_cursor_state.presented_cursor_y);
}

HRESULT HandleBackCursorBackgroundSaveForPresent() {
    return copy_surface_to_backup(g_cursor_state.back_backup_surface,
        direct_draw_state().back_surface, g_cursor_state.presented_cursor_x,
        g_cursor_state.presented_cursor_y);
}

HRESULT HandleBackCursorBackgroundRestoreAfterPresent() {
    return copy_backup_to_surface(direct_draw_state().back_surface,
        g_cursor_state.back_backup_surface, g_cursor_state.presented_cursor_x,
        g_cursor_state.presented_cursor_y);
}

HRESULT HandleCurrentCursorDrawOnPrimary() {
    return draw_cursor_surface(direct_draw_state().primary_surface, g_cursor_state.cursor_x,
        g_cursor_state.cursor_y);
}

HRESULT HandlePresentCursorDrawOnBack() {
    return draw_cursor_surface(direct_draw_state().back_surface,
        g_cursor_state.presented_cursor_x, g_cursor_state.presented_cursor_y);
}

HRESULT HandleGameCursorPresentation() {
    if (!g_cursor_state.visible) {
        HRESULT result = PresentBackBufferToPrimary();
        if (ShouldCaptureScreenshot()) {
            HandleNextScreenshotCapture();
        }
        return result;
    }

    if (!direct_draw_state().active) {
        return direct_draw_state().last_result;
    }

    while (g_cursor_state.pointer_motion_locked) {
        Sleep(0);
    }

    g_cursor_state.presentation_locked = true;
    g_cursor_state.presented_cursor_x = g_cursor_state.cursor_x;
    g_cursor_state.presented_cursor_y = g_cursor_state.cursor_y;

    HandleBackCursorBackgroundSaveForPresent();
    HandlePresentCursorDrawOnBack();
    HRESULT result = PresentBackBufferToPrimary();
    if (ShouldCaptureScreenshot()) {
        HandleNextScreenshotCapture();
    }
    HandleBackCursorBackgroundRestoreAfterPresent();

    if (g_cursor_state.presented_cursor_x == g_cursor_state.cursor_x &&
        g_cursor_state.presented_cursor_y == g_cursor_state.cursor_y) {
        HandlePrimaryCursorBackupRefreshUsingBackBuffer();
    }
    else {
        HandlePrimaryRefreshUsingPresentedBackBackup();
        HandlePrimaryCursorBackgroundSave();
        HandleCurrentCursorDrawOnPrimary();
    }

    g_cursor_state.presentation_locked = false;
    g_cursor_state.last_result = result;
    return result;
}

HRESULT HandleCursorAwarePresentForwarder() {
    return HandleGameCursorPresentation();
}

}
#endif
