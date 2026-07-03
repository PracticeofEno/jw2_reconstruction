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
};

SoftwareCursorState& software_cursor_state();
bool InitializeSoftwareCursorSurfaces();
void ShutdownSoftwareCursorSurfaces();
void SetGameCursorHotspot(u32 cursor_index, i32 x, i32 y);
void SetGameCursorPointerPosition(i32 x, i32 y);
void RestoreSystemCursorPosition();
void ShowGameCursor();
void HideGameCursor();
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
