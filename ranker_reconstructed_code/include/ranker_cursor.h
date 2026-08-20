#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#endif

#include <array>

namespace ranker {

#ifdef _WIN32
constexpr u32 kSoftwareCursorSize = 0x20;
constexpr u32 kSoftwareCursorSurfaceCount = 100;

constexpr bool ShouldPresentGameCursorImmediatelyForPointerMotion(
    bool d3d9_cubic_active, bool continuous_gameplay_presentation_active) {
    return d3d9_cubic_active && !continuous_gameplay_presentation_active;
}

constexpr u32 FrontendCursorArgbFromRgb565(u16 pixel) {
    if (pixel == 0) {
        return 0;
    }
    const u32 red5 = (pixel >> 11) & 0x1fu;
    const u32 green6 = (pixel >> 5) & 0x3fu;
    const u32 blue5 = pixel & 0x1fu;
    const u32 red8 = (red5 << 3) | (red5 >> 2);
    const u32 green8 = (green6 << 2) | (green6 >> 4);
    const u32 blue8 = (blue5 << 3) | (blue5 >> 2);
    return 0xff000000u | (red8 << 16) | (green8 << 8) | blue8;
}

struct SoftwareCursorState {
    LPDIRECTDRAWSURFACE7 primary_backup_surface = nullptr;
    LPDIRECTDRAWSURFACE7 back_backup_surface = nullptr;
    std::array<LPDIRECTDRAWSURFACE7, kSoftwareCursorSurfaceCount> cursor_surfaces{};
    std::array<i32, kSoftwareCursorSurfaceCount> hotspot_x{};
    std::array<i32, kSoftwareCursorSurfaceCount> hotspot_y{};
    HRESULT last_result = DD_OK;
    i32 pointer_x = 0;
    i32 pointer_y = 0;
    i32 cursor_x = 0;
    i32 cursor_y = 0;
    i32 previous_cursor_x = 0;
    i32 previous_cursor_y = 0;
    i32 presented_cursor_x = 0;
    i32 presented_cursor_y = 0;
    u32 cursor_index = 0;
    u8 cursor_change_depth = 0;
    bool surfaces_initialized = false;
    bool visible = false;
    bool pointer_motion_locked = false;
    bool presentation_locked = false;
    bool pointer_updates_suppressed = false;
    // DirectDraw's primary surface belongs to the foreground application.
    // Keep the requested cursor visibility while another application owns
    // the desktop, but do not restore/capture/draw primary pixels there.
    bool application_active = true;
    bool restore_visible_on_activate = false;
    bool resources_loaded = false;
};

SoftwareCursorState& software_cursor_state();
HCURSOR GetFrontendGameCursor();
bool IsFrontendGameCursorResourceLoaded();
bool InitializeSoftwareCursorSurfaces();
bool LoadSoftwareCursorResourcesFromJw201Trc();
void ShutdownSoftwareCursorSurfaces();
void SetGameCursorHotspot(u32 cursor_index, i32 x, i32 y);
void SetGameCursorPointerPosition(i32 x, i32 y);
void SetContinuousGameplayCursorPresentationActive(bool active);
void RestoreSystemCursorPosition();
void ShowGameCursor();
void HideGameCursor();
void SetGameCursorPresentationSuppressed(bool suppressed);
void SetGameCursorApplicationActive(bool active);
void SetGameCursorIndex(u32 cursor_index);
HRESULT LockAndUnlockSoftwareCursorSurface(u32 cursor_index);
HRESULT HandlePrimaryCursorBackgroundRestore();
HRESULT HandlePrimaryCursorBackgroundSave();
HRESULT HandlePrimaryCursorBackupRefreshUsingBackBuffer();
HRESULT HandlePrimaryRefreshUsingPresentedBackBackup();
HRESULT HandleBackCursorBackgroundSaveForPresent();
HRESULT HandleBackCursorBackgroundRestoreAfterPresent();
HRESULT HandleCurrentCursorDrawOnPrimary();
HRESULT HandlePresentCursorDrawOnBack();
HRESULT HandleGameCursorPresentation();
HRESULT HandleCursorAwarePresentForwarder();
#endif

}
