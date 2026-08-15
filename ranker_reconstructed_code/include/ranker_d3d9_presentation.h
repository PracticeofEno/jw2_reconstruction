#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#endif

namespace ranker {

#ifdef _WIN32
constexpr UINT kD3D9CubicPresentationOwnerThreadMessage = WM_APP + 0x317;

struct D3D9CubicPresentationTelemetry {
    HRESULT last_lifecycle_result = S_FALSE;
    HRESULT last_present_result = S_FALSE;
    u32 lifecycle_failure_count = 0;
    u32 successful_present_count = 0;
    u32 fallback_present_count = 0;
};

void ConfigureD3D9CubicPresentation(HWND window, u32 logical_width,
    u32 logical_height, u32 color_depth, bool windowed, u32 red_mask,
    u32 green_mask, u32 blue_mask);
void ShutdownD3D9CubicPresentation();
bool IsD3D9CubicPresentationRequested();
bool IsD3D9CubicPresentationActive();
bool HandleD3D9CubicPresentationOwnerThreadRequest(
    HWND window, WPARAM generation);
D3D9CubicPresentationTelemetry GetD3D9CubicPresentationTelemetry();
HRESULT TryPresentBackBufferWithD3D9Cubic(LPDIRECTDRAWSURFACE7 back_surface);
HRESULT TryPresentBackBufferWithD3D9CubicCursor(
    LPDIRECTDRAWSURFACE7 back_surface,
    LPDIRECTDRAWSURFACE7 cursor_surface,
    i32 cursor_x, i32 cursor_y, i32 hotspot_x, i32 hotspot_y,
    bool reuse_uploaded_background);
#endif

} // namespace ranker
