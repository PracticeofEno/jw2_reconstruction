#include "ranker_cursor.h"

#ifdef _WIN32
#include "ranker_directx.h"
#include "ranker_screenshot.h"
#include "ranker_trc.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ranker {
namespace {

SoftwareCursorState g_cursor_state;

constexpr char kSoftwareCursorArchiveName[] = "JW2_01.TRC";
constexpr u32 kSoftwareCursorArchiveRecord = 0x0b;
constexpr std::size_t kSoftwareCursorHeaderBytes = 0x10;
constexpr std::size_t kSoftwareCursorFrameHeaderBytes = 0x10;

u32 read_cursor_u32(const u8* bytes) {
    return static_cast<u32>(bytes[0]) |
        (static_cast<u32>(bytes[1]) << 8) |
        (static_cast<u32>(bytes[2]) << 16) |
        (static_cast<u32>(bytes[3]) << 24);
}

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

POINT primary_destination_point(i32 x, i32 y) {
    POINT point = clipped_destination_point(x, y);
    const DirectDrawRuntimeState& dd = direct_draw_state();
    if (dd.windowed) {
        point.x += dd.screen_rect.left;
        point.y += dd.screen_rect.top;
    }
    return point;
}

RECT primary_source_rect_for_backup(i32 x, i32 y);

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
    if (FAILED(g_cursor_state.last_result)) {
        // In the original exclusive 16-bit display all four cursor/backup
        // surfaces share one format, so BltFast is sufficient. The windowed
        // reconstruction presents a 16-bit back buffer to a desktop-format
        // primary surface. IDirectDrawSurface7::Blt performs the required
        // conversion for the two primary-surface paths.
        const LONG width = source_rect->right - source_rect->left;
        const LONG height = source_rect->bottom - source_rect->top;
        if (width <= 0 || height <= 0) {
            g_cursor_state.last_result = DD_OK;
            return g_cursor_state.last_result;
        }
        RECT destination_rect{static_cast<LONG>(x), static_cast<LONG>(y),
            static_cast<LONG>(x) + width, static_cast<LONG>(y) + height};
        DWORD blt_flags = DDBLT_WAIT;
        if ((flags & DDBLTFAST_SRCCOLORKEY) != 0) {
            blt_flags |= DDBLT_KEYSRC;
        }
        if ((flags & DDBLTFAST_DESTCOLORKEY) != 0) {
            blt_flags |= DDBLT_KEYDEST;
        }
        g_cursor_state.last_result =
            dest->Blt(&destination_rect, source, source_rect, blt_flags, nullptr);
    }
    return g_cursor_state.last_result;
}

HRESULT copy_backup_to_surface(LPDIRECTDRAWSURFACE7 dest, LPDIRECTDRAWSURFACE7 backup,
    i32 x, i32 y, bool primary_destination = false) {
    RECT rect = backup_source_rect_for_restore(x, y);
    const POINT dest_point = primary_destination ?
        primary_destination_point(x, y) : clipped_destination_point(x, y);
    return blt_fast(dest, static_cast<DWORD>(dest_point.x), static_cast<DWORD>(dest_point.y),
        backup, &rect, DDBLTFAST_NOCOLORKEY);
}

HRESULT copy_surface_to_backup(LPDIRECTDRAWSURFACE7 backup, LPDIRECTDRAWSURFACE7 source,
    i32 x, i32 y, bool primary_source = false) {
    RECT rect = primary_source ?
        primary_source_rect_for_backup(x, y) : screen_source_rect_for_backup(x, y);
    return blt_fast(backup, 0, 0, source, &rect, DDBLTFAST_NOCOLORKEY);
}

HRESULT draw_cursor_surface(LPDIRECTDRAWSURFACE7 dest, i32 x, i32 y,
    bool primary_destination = false) {
    if (!cursor_index_valid(g_cursor_state.cursor_index)) {
        g_cursor_state.last_result = DD_OK;
        return g_cursor_state.last_result;
    }
    RECT rect = cursor_source_rect(x, y);
    const POINT dest_point = primary_destination ?
        primary_destination_point(x, y) : clipped_destination_point(x, y);
    return blt_fast(dest, static_cast<DWORD>(dest_point.x), static_cast<DWORD>(dest_point.y),
        g_cursor_state.cursor_surfaces[g_cursor_state.cursor_index], &rect,
        DDBLTFAST_SRCCOLORKEY);
}

RECT primary_source_rect_for_backup(i32 x, i32 y) {
    RECT rect = screen_source_rect_for_backup(x, y);
    const DirectDrawRuntimeState& dd = direct_draw_state();
    if (dd.windowed) {
        OffsetRect(&rect, dd.screen_rect.left, dd.screen_rect.top);
    }
    return rect;
}

bool create_cursor_surface(LPDIRECTDRAW7 direct_draw, LPDIRECTDRAWSURFACE7& surface) {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = kSoftwareCursorSize;
    desc.dwHeight = kSoftwareCursorSize;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    const DirectDrawRuntimeState& dd = direct_draw_state();
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 16;
    desc.ddpfPixelFormat.dwRBitMask =
        dd.red_mask != 0 ? dd.red_mask : (dd.pixel_mode_555 != 0 ? 0x7c00u : 0xf800u);
    desc.ddpfPixelFormat.dwGBitMask =
        dd.green_mask != 0 ? dd.green_mask : (dd.pixel_mode_555 != 0 ? 0x03e0u : 0x07e0u);
    desc.ddpfPixelFormat.dwBBitMask = dd.blue_mask != 0 ? dd.blue_mask : 0x001fu;

    g_cursor_state.last_result = direct_draw->CreateSurface(&desc, &surface, nullptr);
    return SUCCEEDED(g_cursor_state.last_result);
}

bool lock_cursor_surface(LPDIRECTDRAWSURFACE7 surface, DDSURFACEDESC2& desc) {
    if (surface == nullptr) {
        g_cursor_state.last_result = DDERR_GENERIC;
        return false;
    }
    desc = {};
    desc.dwSize = sizeof(desc);
    g_cursor_state.last_result = surface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
    if (FAILED(g_cursor_state.last_result)) {
        return false;
    }
    if (desc.lpSurface == nullptr || desc.lPitch <
            static_cast<LONG>(kSoftwareCursorSize * sizeof(u16)) ||
        desc.dwWidth < kSoftwareCursorSize || desc.dwHeight < kSoftwareCursorSize) {
        surface->Unlock(nullptr);
        g_cursor_state.last_result = DDERR_GENERIC;
        return false;
    }
    return true;
}

bool clear_cursor_surface(LPDIRECTDRAWSURFACE7 surface) {
    DDSURFACEDESC2 desc{};
    if (!lock_cursor_surface(surface, desc)) {
        return false;
    }
    auto* pixels = static_cast<u8*>(desc.lpSurface);
    for (u32 y = 0; y < kSoftwareCursorSize; ++y) {
        std::memset(pixels + static_cast<std::size_t>(y) * desc.lPitch, 0,
            kSoftwareCursorSize * sizeof(u16));
    }
    g_cursor_state.last_result = surface->Unlock(nullptr);
    return SUCCEEDED(g_cursor_state.last_result);
}

u16 normalize_cursor_pixel(u16 pixel) {
    const DirectDrawRuntimeState& dd = direct_draw_state();
    if (dd.pixel_mode_555 == 0) {
        return pixel;
    }
    // mouse100.mc is stored as RGB565. FUN_004153b0 converts it to the
    // active RGB555 masks when DAT_01450834 selects that surface format.
    const u16 red_mask = static_cast<u16>(dd.red_mask != 0 ? dd.red_mask : 0x7c00u);
    const u16 green_mask = static_cast<u16>(dd.green_mask != 0 ? dd.green_mask : 0x03e0u);
    const u16 blue_mask = static_cast<u16>(dd.blue_mask != 0 ? dd.blue_mask : 0x001fu);
    return static_cast<u16>(((pixel >> 1) & red_mask) |
        ((pixel >> 1) & green_mask) | (pixel & blue_mask));
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
        // DirectDraw does not guarantee newly allocated system-memory surface
        // contents. Keep unavailable cursor frames transparent instead of
        // presenting allocator garbage.
        if (!clear_cursor_surface(surface)) {
            ShutdownSoftwareCursorSurfaces();
            return false;
        }
    }

    g_cursor_state.surfaces_initialized = true;
    return true;
}

bool LoadSoftwareCursorResourcesFromJw201Trc() {
    if (g_cursor_state.resources_loaded) {
        return true;
    }
    if (!g_cursor_state.surfaces_initialized && !InitializeSoftwareCursorSurfaces()) {
        return false;
    }

    // Original FUN_004153b0 loads JW2_01 record 0x0b (mouse100.mc): a
    // 16-byte file header followed by 100 {hotspot header, 32x32 RGB565}
    // frames. The previous reconstruction created the DirectDraw surfaces
    // but never uploaded this record, so the software cursor was rendered as
    // a large white/black/red block over whichever HUD control was under it.
    std::vector<u8> payload;
    if (!read_trc_record(kSoftwareCursorArchiveName,
            kSoftwareCursorArchiveRecord, payload) ||
        payload.size() < kSoftwareCursorHeaderBytes) {
        return false;
    }

    const u32 bits_per_pixel = read_cursor_u32(payload.data());
    const u32 frame_count = read_cursor_u32(payload.data() + 4);
    const u32 width = read_cursor_u32(payload.data() + 8);
    const u32 height = read_cursor_u32(payload.data() + 12);
    if (bits_per_pixel != 16 || frame_count == 0 ||
        frame_count > kSoftwareCursorSurfaceCount || width == 0 || height == 0 ||
        width > kSoftwareCursorSize || height > kSoftwareCursorSize) {
        return false;
    }

    const std::size_t source_row_bytes =
        static_cast<std::size_t>(width) * sizeof(u16);
    const std::size_t frame_pixel_bytes = source_row_bytes * height;
    std::size_t cursor = kSoftwareCursorHeaderBytes;
    for (u32 frame = 0; frame < frame_count; ++frame) {
        if (cursor + kSoftwareCursorFrameHeaderBytes > payload.size()) {
            return false;
        }
        const i32 hotspot_x = static_cast<i32>(read_cursor_u32(payload.data() + cursor));
        const i32 hotspot_y = static_cast<i32>(read_cursor_u32(payload.data() + cursor + 4));
        cursor += kSoftwareCursorFrameHeaderBytes;
        if (cursor + frame_pixel_bytes > payload.size()) {
            return false;
        }

        LPDIRECTDRAWSURFACE7 surface = g_cursor_state.cursor_surfaces[frame];
        DDSURFACEDESC2 desc{};
        if (!lock_cursor_surface(surface, desc)) {
            return false;
        }
        auto* destination = static_cast<u8*>(desc.lpSurface);
        for (u32 y = 0; y < kSoftwareCursorSize; ++y) {
            std::memset(destination + static_cast<std::size_t>(y) * desc.lPitch,
                0, kSoftwareCursorSize * sizeof(u16));
        }
        for (u32 y = 0; y < height; ++y) {
            const u8* source_row = payload.data() + cursor +
                static_cast<std::size_t>(y) * source_row_bytes;
            auto* destination_row = reinterpret_cast<u16*>(destination +
                static_cast<std::size_t>(y) * desc.lPitch);
            for (u32 x = 0; x < width; ++x) {
                const u16 source_pixel = static_cast<u16>(source_row[x * 2]) |
                    static_cast<u16>(source_row[x * 2 + 1] << 8);
                destination_row[x] = normalize_cursor_pixel(source_pixel);
            }
        }
        g_cursor_state.last_result = surface->Unlock(nullptr);
        if (FAILED(g_cursor_state.last_result)) {
            return false;
        }

        g_cursor_state.hotspot_x[frame] = hotspot_x;
        g_cursor_state.hotspot_y[frame] = hotspot_y;
        cursor += frame_pixel_bytes;
    }

    g_cursor_state.resources_loaded = true;
    update_cursor_draw_position();
    return true;
}

void ShutdownSoftwareCursorSurfaces() {
    release_com(g_cursor_state.primary_backup_surface);
    release_com(g_cursor_state.back_backup_surface);
    for (auto*& surface : g_cursor_state.cursor_surfaces) {
        release_com(surface);
    }
    g_cursor_state.surfaces_initialized = false;
    g_cursor_state.resources_loaded = false;
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
    // The original receives this path for actual pointer motion.  Reconstructed
    // gameplay input also samples the pointer every frame, so ignore identical
    // samples instead of repeatedly restoring and recapturing the same pixels.
    if (g_cursor_state.pointer_x == x && g_cursor_state.pointer_y == y) {
        return;
    }

    g_cursor_state.pointer_x = x;
    g_cursor_state.pointer_y = y;
    if (!g_cursor_state.visible) {
        return;
    }

    // ranker.exe sets the motion lock before publishing either the previous or
    // current draw coordinates.  Presentation waits on this flag; updating the
    // coordinates first lets it pair a new position with the old backup and can
    // leave a cursor image behind at the previous click location.
    g_cursor_state.pointer_motion_locked = true;
    g_cursor_state.previous_cursor_x = g_cursor_state.cursor_x;
    g_cursor_state.previous_cursor_y = g_cursor_state.cursor_y;
    update_cursor_draw_position();
    if (!g_cursor_state.presentation_locked) {
        HandlePrimaryCursorBackgroundRestore();
        HandlePrimaryCursorBackgroundSave();
        HandleCurrentCursorDrawOnPrimary();
    }
    g_cursor_state.pointer_motion_locked = false;
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
        g_cursor_state.previous_cursor_y, true);
}

HRESULT HandlePrimaryCursorBackgroundSave() {
    return copy_surface_to_backup(g_cursor_state.primary_backup_surface,
        direct_draw_state().primary_surface, g_cursor_state.cursor_x,
        g_cursor_state.cursor_y, true);
}

HRESULT HandlePrimaryCursorBackupRefreshUsingBackBuffer() {
    return copy_surface_to_backup(g_cursor_state.primary_backup_surface,
        direct_draw_state().back_surface, g_cursor_state.presented_cursor_x,
        g_cursor_state.presented_cursor_y);
}

HRESULT HandlePrimaryRefreshUsingPresentedBackBackup() {
    return copy_backup_to_surface(direct_draw_state().primary_surface,
        g_cursor_state.back_backup_surface, g_cursor_state.presented_cursor_x,
        g_cursor_state.presented_cursor_y, true);
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
        g_cursor_state.cursor_y, true);
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
