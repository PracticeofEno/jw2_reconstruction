#include "ranker_text_renderer.h"

#include "ranker_directx.h"
#include "ranker_palette_cache.h"
#include "ranker_sprite_renderer.h"
#include "ranker_system_ui.h"

#ifdef _WIN32
#include <mmsystem.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>

namespace ranker {
namespace {

TextRendererState g_text_renderer_state;
LegacyTickTimerState g_legacy_tick_timer_state;

#ifdef _WIN32
void CALLBACK increment_legacy_timer_counter_proc(
    UINT, UINT, DWORD_PTR user, DWORD_PTR, DWORD_PTR) {
    auto* counter = reinterpret_cast<u32*>(user);
    ++*counter;
}

bool query_legacy_timer_resolution() {
    const MMRESULT result = timeGetDevCaps(
        reinterpret_cast<LPTIMECAPS>(&g_legacy_tick_timer_state.timer_resolution_ms),
        sizeof(TIMECAPS));
    return result == MMSYSERR_NOERROR;
}

void start_legacy_timer_event() {
    g_legacy_tick_timer_state.timer_event_id = 0;
    if (!query_legacy_timer_resolution()) {
        g_legacy_tick_timer_state.periodic_timer_active = false;
        return;
    }

    g_legacy_tick_timer_state.timer_event_id = timeSetEvent(
        g_legacy_tick_timer_state.timer_interval_ms,
        g_legacy_tick_timer_state.timer_resolution_ms,
        increment_legacy_timer_counter_proc,
        reinterpret_cast<DWORD_PTR>(g_legacy_tick_timer_state.timer_counter_user),
        TIME_PERIODIC);
    g_legacy_tick_timer_state.periodic_timer_active =
        g_legacy_tick_timer_state.timer_event_id != 0;
}

void stop_legacy_timer_event() {
    timeKillEvent(g_legacy_tick_timer_state.timer_event_id);
    g_legacy_tick_timer_state.periodic_timer_active = false;
}
#endif

constexpr std::array<u8, 32> kHangulInitialComponentTable{
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0};

constexpr std::array<u8, 32> kHangulMedialComponentTable{
    0, 0, 0, 1, 2, 3, 4, 5,
    0, 0, 6, 7, 8, 9, 10, 11,
    0, 0, 12, 13, 14, 15, 16, 17,
    0, 0, 18, 19, 20, 21, 0, 0};

constexpr std::array<u8, 32> kHangulFinalComponentTable{
    0, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 0, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 0, 0};

constexpr std::array<u8, 20> kHangulInitialVariantNoFinal{
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 1, 1, 1};

constexpr std::array<u8, 20> kHangulInitialVariantWithFinal{
    0, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 2, 3, 3, 3};

constexpr std::array<u8, 22> kHangulFinalVariantByMedial{
    0, 0, 2, 0, 2, 1, 2, 1, 2, 3, 0,
    2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1};

constexpr std::array<u8, 22> kHangulMedialVariantNoFinal{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3,
    3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0};

constexpr std::array<u8, 22> kHangulMedialVariantWithFinal{
    0, 5, 5, 5, 5, 5, 5, 5, 5, 6, 7,
    7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5};

bool can_select_draw_font(const TextFontDefinition& font) {
    return (font.flags & 8u) != 0 || font.flags == 0x15 || (font.flags & 2u) != 0;
}

bool can_select_metric_font(const TextFontDefinition& font) {
    return (font.flags & 8u) != 0 || font.flags == 0x15 || (font.flags & 1u) != 0;
}

bool bitmap_font_available(const TextFontDefinition& font) {
    return font.glyph_data != nullptr && font.height != 0 && font.row_stride != 0;
}

const u8* font_data_at(const TextFontDefinition& font, std::size_t offset,
    std::size_t required_size) {
    if (font.glyph_data == nullptr || offset > font.glyph_data_size ||
        required_size > font.glyph_data_size - offset) {
        return nullptr;
    }
    return font.glyph_data + offset;
}

u32 glyph_source_width(const TextFontDefinition& font, u8 ch) {
    if (font.flags == 2 || font.flags == 6) {
        return font.max_width;
    }
    if (font.glyph_data == nullptr) {
        return font.max_width;
    }
    return font.glyph_data[ch];
}

u32 glyph_draw_width(const TextFontDefinition& font, u8 ch) {
    const u32 width = glyph_source_width(font, ch);
    if ((font.flags & 4u) != 0 && font.flags != 6) {
        return std::min(width, font.max_width);
    }
    return width;
}

u32 glyph_width(const TextFontDefinition& font, u8 ch) {
    return glyph_source_width(font, ch);
}

const u8* glyph_bitmap(const TextFontDefinition& font, u8 ch) {
    if (font.glyph_data == nullptr) {
        return nullptr;
    }
    return font.glyph_data + static_cast<std::size_t>(ch) * font.glyph_span +
        font.bitmap_offset;
}

u16 color_pixel(u8 index) {
    return g_text_renderer_state.color_pixels[index];
}

bool target_pixel_visible(const SpriteRenderTarget& target, i32 x, i32 y) {
    return x >= g_text_renderer_state.clip_left && x <= g_text_renderer_state.clip_right &&
        y >= g_text_renderer_state.clip_top && y <= g_text_renderer_state.clip_bottom &&
        x >= 0 && y >= 0 && x < static_cast<i32>(target.width) &&
        y < static_cast<i32>(target.height);
}

void write_text_pixel(const SpriteRenderTarget& target, i32 x, i32 y, u8 color_index) {
    target.pixels[static_cast<std::size_t>(y) * target.stride_words +
        static_cast<std::size_t>(x)] = color_pixel(color_index);
}

bool draw_indexed_bitmap_glyph(u8 ch, u8 foreground, u8 background, i32 x, i32 y) {
    const auto& font = g_text_renderer_state.draw_font;
    const auto& target = sprite_render_state().target;
    if (target.pixels == nullptr || target.width == 0 || target.height == 0 ||
        target.stride_words == 0) {
        return false;
    }

    const u32 source_width = glyph_source_width(font, ch);
    const u32 draw_width = glyph_draw_width(font, ch);
    const u8* row_ptr = glyph_bitmap(font, ch);
    if (row_ptr == nullptr) {
        return false;
    }

    for (u32 row = 0; row < font.height; ++row) {
        const i32 py = y + static_cast<i32>(row);
        if (py >= g_text_renderer_state.clip_top && py <= g_text_renderer_state.clip_bottom &&
            py >= 0 && py < static_cast<i32>(target.height)) {
            const u8* src = row_ptr;
            for (u32 col = 0; col < draw_width; ++col) {
                const i32 px = x + static_cast<i32>(col);
                const u8 value = src[col];
                if (target_pixel_visible(target, px, py)) {
                    if (value != 0) {
                        write_text_pixel(target, px, py, static_cast<u8>(value + foreground - 1));
                    }
                    else if (background != 0) {
                        write_text_pixel(target, px, py, background);
                    }
                }
            }
        }
        row_ptr += font.row_stride;
    }

    g_text_renderer_state.cursor.x +=
        static_cast<i32>(source_width) + g_text_renderer_state.cursor.advance_extra;
    return true;
}

bool draw_packed_bitmap_glyph(u8 ch, u8 foreground, u8 background, i32 x, i32 y) {
    const auto& font = g_text_renderer_state.draw_font;
    const auto& target = sprite_render_state().target;
    if (target.pixels == nullptr || target.width == 0 || target.height == 0 ||
        target.stride_words == 0) {
        return false;
    }

    const u32 source_width = glyph_source_width(font, ch);
    const u32 total_width = source_width + static_cast<u32>(g_text_renderer_state.cursor.advance_extra);
    const u8* row_ptr = glyph_bitmap(font, ch);
    if (row_ptr == nullptr) {
        return false;
    }

    for (u32 row = 0; row < font.height; ++row) {
        const i32 py = y + static_cast<i32>(row);
        if (py >= g_text_renderer_state.clip_top && py <= g_text_renderer_state.clip_bottom &&
            py >= 0 && py < static_cast<i32>(target.height)) {
            for (u32 col = 0; col < total_width; ++col) {
                const bool glyph_bit =
                    col < source_width && (row_ptr[col >> 3] & (0x80u >> (col & 7u))) != 0;
                const i32 px = x + static_cast<i32>(col);
                if (target_pixel_visible(target, px, py)) {
                    if (glyph_bit) {
                        write_text_pixel(target, px, py, foreground);
                    }
                    else if (background != 0) {
                        write_text_pixel(target, px, py, background);
                    }
                }
            }
        }
        row_ptr += font.row_stride;
    }

    g_text_renderer_state.cursor.x += static_cast<i32>(total_width);
    return true;
}

bool draw_indexed_bitmap_block(const u8* row_ptr, u32 width, u32 height, u32 row_stride,
    u8 foreground, u8 background, i32 x, i32 y) {
    const auto& target = sprite_render_state().target;
    if (target.pixels == nullptr || target.width == 0 || target.height == 0 ||
        target.stride_words == 0 || row_ptr == nullptr) {
        return false;
    }

    for (u32 row = 0; row < height; ++row) {
        const i32 py = y + static_cast<i32>(row);
        if (py >= g_text_renderer_state.clip_top && py <= g_text_renderer_state.clip_bottom &&
            py >= 0 && py < static_cast<i32>(target.height)) {
            for (u32 col = 0; col < width; ++col) {
                const i32 px = x + static_cast<i32>(col);
                const u8 value = row_ptr[col];
                if (target_pixel_visible(target, px, py)) {
                    if (value != 0) {
                        write_text_pixel(target, px, py, static_cast<u8>(value + foreground - 1));
                    }
                    else if (background != 0) {
                        write_text_pixel(target, px, py, background);
                    }
                }
            }
        }
        row_ptr += row_stride;
    }
    return true;
}

bool draw_packed_bitmap_block(const u8* row_ptr, u32 width, u32 height, u32 row_stride,
    u8 foreground, u8 background, i32 x, i32 y) {
    const auto& target = sprite_render_state().target;
    if (target.pixels == nullptr || target.width == 0 || target.height == 0 ||
        target.stride_words == 0 || row_ptr == nullptr) {
        return false;
    }

    for (u32 row = 0; row < height; ++row) {
        const i32 py = y + static_cast<i32>(row);
        if (py >= g_text_renderer_state.clip_top && py <= g_text_renderer_state.clip_bottom &&
            py >= 0 && py < static_cast<i32>(target.height)) {
            for (u32 col = 0; col < width; ++col) {
                const bool glyph_bit =
                    (row_ptr[col >> 3] & (0x80u >> (col & 7u))) != 0;
                const i32 px = x + static_cast<i32>(col);
                if (target_pixel_visible(target, px, py)) {
                    if (glyph_bit) {
                        write_text_pixel(target, px, py, foreground);
                    }
                    else if (background != 0) {
                        write_text_pixel(target, px, py, background);
                    }
                }
            }
        }
        row_ptr += row_stride;
    }
    return true;
}

const u8* direct_dbcs_bitmap(const TextFontDefinition& font, u16 code) {
    if (font.glyph_data == nullptr) {
        return nullptr;
    }

    const u8 lead = static_cast<u8>(code >> 8);
    const u8 trail = static_cast<u8>(code);
    if (lead > 0xaf && lead < 0xc9 && trail > 0x9f) {
        const u16 adjusted = static_cast<u16>(code + 0x4f60);
        const u32 index = static_cast<u32>(adjusted >> 8) * 0x60u + (adjusted & 0xffu);
        return font.glyph_data + static_cast<std::size_t>(index) * font.glyph_span;
    }

    return font.glyph_data;
}

const u8* packed_hangul_initial_component(const TextFontDefinition& font, u8 initial,
    u8 medial_variant) {
    return font.glyph_data + static_cast<std::size_t>(medial_variant) * 0x280u +
        static_cast<std::size_t>(initial) * 0x20u;
}

const u8* packed_hangul_medial_component(const TextFontDefinition& font, u8 medial,
    u8 initial_variant) {
    return font.glyph_data + 0x1680u +
        static_cast<std::size_t>(initial_variant) * 0x2c0u +
        static_cast<std::size_t>(medial) * 0x20u;
}

const u8* packed_hangul_final_component(const TextFontDefinition& font, u8 final,
    u8 final_variant) {
    return font.glyph_data + 0x22e0u +
        static_cast<std::size_t>(final_variant) * 0x380u +
        static_cast<std::size_t>(final) * 0x20u;
}

const u8* indexed_hangul_initial_component(const TextFontDefinition& font, u8 initial,
    u8 medial_variant) {
    const std::size_t offset =
        static_cast<std::size_t>(medial_variant) * 0x1680u +
        static_cast<std::size_t>(initial) * 0x120u;
    return font_data_at(font, offset, 0x120u);
}

const u8* indexed_hangul_medial_component(const TextFontDefinition& font, u8 medial,
    u8 initial_variant) {
    const std::size_t offset = 0xb400u +
        static_cast<std::size_t>(initial_variant) * 0x18c0u +
        static_cast<std::size_t>(medial) * 0x120u;
    return font_data_at(font, offset, 0x120u);
}

const u8* indexed_hangul_final_component(const TextFontDefinition& font, u8 final,
    u8 final_variant) {
    const std::size_t offset = 0x11700u +
        static_cast<std::size_t>(final_variant) * 0x1f80u +
        static_cast<std::size_t>(final) * 0x120u;
    return font_data_at(font, offset, 0x120u);
}

bool draw_bitmap_glyph(u8 ch, u8 foreground, u8 background, i32 x, i32 y) {
    const auto& font = g_text_renderer_state.draw_font;
    if (!bitmap_font_available(font)) {
        g_text_renderer_state.cursor.x +=
            static_cast<i32>(glyph_width(font, ch)) + g_text_renderer_state.cursor.advance_extra;
        return true;
    }

    if ((font.flags & 4u) != 0) {
        return draw_indexed_bitmap_glyph(ch, foreground, background, x, y);
    }

    return draw_packed_bitmap_glyph(ch, foreground, background, x, y);
}

bool draw_ascii_string(const char* text, bool allow_dbcs_skip) {
    if (text == nullptr) {
        return false;
    }

    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p != 0) {
        const u8 ch = *p++;
        if ((ch & 0x80u) != 0) {
            if (!allow_dbcs_skip || *p == 0) {
                return false;
            }
            const u16 code = static_cast<u16>((static_cast<u16>(ch) << 8) | *p++);
            if (!RenderDbcsCompositeGlyph(code)) {
                return false;
            }
            continue;
        }
        if (!DrawTextGlyph(ch)) {
            return false;
        }
    }
    return true;
}

bool uses_win32_font_path() {
    return (g_text_renderer_state.metric_font.flags & 8u) != 0 ||
        (g_text_renderer_state.draw_font.flags & 8u) != 0;
}

#ifdef _WIN32
COLORREF color_ref_from_text_index(u8 index) {
    return static_cast<COLORREF>(ResolveTextColorRef(index));
}

void ensure_win32_text_font(HDC dc) {
    auto& fonts = system_ui_state().fonts;
    if (fonts.handles[1] == nullptr) {
        InitializeUiFontHandles();
    }
    // RenderWin32FontRunAndAdvance (0x005077f0) does not select an HFONT from
    // the current text-definition height on each draw.  Its sole setup path,
    // InitializeWin32UiFontMetrics (0x005071c0), selects height 12 once and
    // leaves that handle active.  Treating slot 4's metadata height (16) as a
    // per-run HFONT request made selected unit/building names visibly larger
    // than the original.
    if (!fonts.metrics_initialized || fonts.selected == nullptr) {
        InitializeWin32UiFontMetrics(dc);
    }
}

bool with_back_buffer_dc(const std::function<bool(HDC)>& callback) {
    const auto& dd = direct_draw_state();
    if (!dd.active || dd.back_surface == nullptr) {
        return false;
    }

    // A live software sprite target means the DirectDraw back surface is
    // already locked by the caller.  GetDC cannot succeed during that lock
    // and some DirectDraw implementations wait for their internal timeout
    // before returning DDERR_SURFACEBUSY.  Modal screens with eight dynamic
    // save-slot labels paid that delay eight times before their first frame.
    // Use the locked-target memory-DC fallback immediately instead.
    const SpriteRenderState& sprite = sprite_render_state();
    if (sprite.active && sprite.target.pixels != nullptr) {
        return false;
    }

    HDC dc = nullptr;
    if (FAILED(dd.back_surface->GetDC(&dc)) || dc == nullptr) {
        return false;
    }
    const bool ok = callback(dc);
    dd.back_surface->ReleaseDC(dc);
    return ok;
}

bool with_memory_text_dc(const std::function<bool(HDC)>& callback) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
        return false;
    }
    const bool ok = callback(dc);
    DeleteDC(dc);
    return ok;
}

DWORD surface_pixel_to_dib_pixel(u16 pixel, bool pixel_mode_555) {
    const u32 red = pixel_mode_555 ?
        ((pixel >> 10) & 0x1fu) << 3 : ((pixel >> 11) & 0x1fu) << 3;
    const u32 green = pixel_mode_555 ?
        ((pixel >> 5) & 0x1fu) << 3 : ((pixel >> 5) & 0x3fu) << 2;
    const u32 blue = (pixel & 0x1fu) << 3;
    return (red << 16) | (green << 8) | blue;
}

u16 dib_pixel_to_surface_pixel(DWORD pixel, bool pixel_mode_555) {
    const u16 red = static_cast<u16>((pixel >> 16) & 0xffu);
    const u16 green = static_cast<u16>((pixel >> 8) & 0xffu);
    const u16 blue = static_cast<u16>(pixel & 0xffu);
    if (pixel_mode_555) {
        return static_cast<u16>(((red >> 3) << 10) |
            ((green >> 3) << 5) | (blue >> 3));
    }
    return static_cast<u16>(((red >> 3) << 11) |
        ((green >> 2) << 5) | (blue >> 3));
}

bool measure_win32_text_with_memory_dc(const char* text, std::size_t length,
    HFONT font, SIZE& extent) {
    return with_memory_text_dc([&](HDC dc) {
        if (font == nullptr) {
            ensure_win32_text_font(dc);
            font = system_ui_state().fonts.selected;
        }
        const HGDIOBJ previous_font = SelectObject(dc, font);
        const BOOL ok = GetTextExtentPoint32A(
            dc, text, static_cast<int>(length), &extent);
        if (previous_font != nullptr && previous_font != HGDI_ERROR) {
            SelectObject(dc, previous_font);
        }
        return ok != FALSE;
    });
}

// The gameplay software compositor keeps the DirectDraw back surface locked
// while it draws terrain, units, HUD text and the bottom panel.  Font slot 4
// is a Win32/GDI font, and IDirectDrawSurface7::GetDC is invalid during that
// lock.  Render only the affected rectangle through a top-down memory DIB and
// copy it back to the already-locked 16-bit sprite target.  Drawing happens at
// the original point in the composite, so later panel sprites still cover the
// queued message while it scrolls in from the full-screen baseline.
bool with_locked_sprite_target_text_dc(i32 x, i32 y, i32 width, i32 height,
    const std::function<bool(HDC)>& callback) {
    const SpriteRenderTarget& target = sprite_render_state().target;
    if (target.pixels == nullptr || target.width == 0 || target.height == 0 ||
        target.stride_words < target.width) {
        return false;
    }

    const i64 requested_right = static_cast<i64>(x) + std::max(width, 0);
    const i64 requested_bottom = static_cast<i64>(y) + std::max(height, 0);
    const i32 left = std::max<i32>(x, 0);
    const i32 top = std::max<i32>(y, 0);
    const i32 right = static_cast<i32>(std::min<i64>(
        requested_right, static_cast<i64>(target.width)));
    const i32 bottom = static_cast<i32>(std::min<i64>(
        requested_bottom, static_cast<i64>(target.height)));
    if (right <= left || bottom <= top) {
        // TextOut succeeds even when the surface clips the entire run.
        return true;
    }

    const i32 dib_width = right - left;
    const i32 dib_height = bottom - top;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = dib_width;
    info.bmiHeader.biHeight = -dib_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
        return false;
    }
    void* raw_pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        dc, &info, DIB_RGB_COLORS, &raw_pixels, nullptr, 0);
    if (bitmap == nullptr || raw_pixels == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        DeleteDC(dc);
        return false;
    }

    const HGDIOBJ previous_bitmap = SelectObject(dc, bitmap);
    auto* dib_pixels = static_cast<DWORD*>(raw_pixels);
    const bool pixel_mode_555 = SurfacePixelMode555();
    for (i32 row = 0; row < dib_height; ++row) {
        const u16* source = target.pixels +
            static_cast<std::size_t>(top + row) * target.stride_words + left;
        DWORD* destination = dib_pixels +
            static_cast<std::size_t>(row) * static_cast<std::size_t>(dib_width);
        for (i32 column = 0; column < dib_width; ++column) {
            destination[column] =
                surface_pixel_to_dib_pixel(source[column], pixel_mode_555);
        }
    }

    SetViewportOrgEx(dc, -left, -top, nullptr);
    const bool ok = callback(dc);
    if (ok) {
        for (i32 row = 0; row < dib_height; ++row) {
            u16* destination = target.pixels +
                static_cast<std::size_t>(top + row) * target.stride_words + left;
            const DWORD* source = dib_pixels +
                static_cast<std::size_t>(row) * static_cast<std::size_t>(dib_width);
            for (i32 column = 0; column < dib_width; ++column) {
                destination[column] =
                    dib_pixel_to_surface_pixel(source[column], pixel_mode_555);
            }
        }
        ++g_text_renderer_state.win32_sprite_target_fallback_count;
    }

    if (previous_bitmap != nullptr && previous_bitmap != HGDI_ERROR) {
        SelectObject(dc, previous_bitmap);
    }
    DeleteObject(bitmap);
    DeleteDC(dc);
    return ok;
}

void advance_win32_text_cursor(const SIZE& extent) {
    g_text_renderer_state.cursor.x += extent.cx;
    const auto& dd = direct_draw_state();
    if (g_text_renderer_state.cursor.x >= static_cast<i32>(dd.width)) {
        g_text_renderer_state.cursor.x = 0;
        g_text_renderer_state.cursor.y += extent.cy;
        if (g_text_renderer_state.cursor.y >= static_cast<i32>(dd.height)) {
            g_text_renderer_state.cursor.y = 0;
        }
    }
}

bool measure_win32_text_run(const char* text, std::size_t length) {
    g_text_renderer_state.measured_width = 1;
    g_text_renderer_state.measured_height = 1;
    if (text == nullptr) {
        return false;
    }
    if (length == 0) {
        return true;
    }

    const bool surface_ok = with_back_buffer_dc([&](HDC dc) {
        ensure_win32_text_font(dc);
        SelectObject(dc, system_ui_state().fonts.selected);
        SIZE extent{1, 1};
        const BOOL ok = GetTextExtentPoint32A(dc, text, static_cast<int>(length), &extent);
        if (ok == FALSE) {
            return false;
        }
        g_text_renderer_state.measured_width = static_cast<u32>(extent.cx);
        g_text_renderer_state.measured_height = static_cast<u32>(extent.cy);
        return true;
    });
    if (surface_ok) {
        return true;
    }

    SIZE extent{1, 1};
    if (!measure_win32_text_with_memory_dc(text, length, nullptr, extent)) {
        return false;
    }
    g_text_renderer_state.measured_width = static_cast<u32>(extent.cx);
    g_text_renderer_state.measured_height = static_cast<u32>(extent.cy);
    ++g_text_renderer_state.win32_memory_measure_fallback_count;
    return true;
}

bool render_win32_text_run(const char* text, std::size_t length) {
    if (text == nullptr) {
        return false;
    }
    if (length == 0) {
        return true;
    }

    SIZE extent{};
    const auto render = [&](HDC dc) {
        ensure_win32_text_font(dc);
        if (g_text_renderer_state.cursor.background == 0) {
            SetBkMode(dc, TRANSPARENT);
        }
        else {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, color_ref_from_text_index(g_text_renderer_state.cursor.background));
        }

        SelectObject(dc, system_ui_state().fonts.selected);
        SetTextColor(dc, color_ref_from_text_index(g_text_renderer_state.cursor.foreground));
        const int count = static_cast<int>(length);
        const BOOL out_ok = TextOutA(dc, g_text_renderer_state.cursor.x,
            g_text_renderer_state.cursor.y, text, count);
        const BOOL extent_ok = GetTextExtentPoint32A(dc, text, count, &extent);
        if (out_ok == FALSE || extent_ok == FALSE) {
            return false;
        }
        return true;
    };
    if (with_back_buffer_dc(render)) {
        advance_win32_text_cursor(extent);
        return true;
    }

    if (!measure_win32_text_with_memory_dc(text, length, nullptr, extent)) {
        return false;
    }
    const TextRenderCursor cursor = g_text_renderer_state.cursor;
    if (!with_locked_sprite_target_text_dc(cursor.x, cursor.y,
            extent.cx, extent.cy, render)) {
        return false;
    }
    advance_win32_text_cursor(extent);
    return true;
}

bool draw_win32_text_at(i32 x, i32 y, HFONT font, u8 foreground, u8 background,
    const char* text, bool centered) {
    if (text == nullptr) {
        return false;
    }

    const std::size_t length = std::strlen(text);
    SIZE extent{};
    const auto render = [&](HDC dc) {
        if (font == nullptr) {
            ensure_win32_text_font(dc);
            font = system_ui_state().fonts.selected;
        }

        SelectObject(dc, font);
        if (background == 0) {
            SetBkMode(dc, TRANSPARENT);
        }
        else {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, color_ref_from_text_index(background));
        }
        SetTextColor(dc, color_ref_from_text_index(foreground));

        const int count = static_cast<int>(length);
        i32 draw_x = x;
        if (centered) {
            if (GetTextExtentPoint32A(dc, text, count, &extent) != FALSE) {
                draw_x -= extent.cx / 2;
            }
        }
        const BOOL ok = TextOutA(dc, draw_x, y, text, count);
        return ok != FALSE;
    };
    if (with_back_buffer_dc(render)) {
        return true;
    }

    if (!measure_win32_text_with_memory_dc(text, length, font, extent)) {
        return false;
    }
    const i32 draw_x = centered ? x - extent.cx / 2 : x;
    return with_locked_sprite_target_text_dc(
        draw_x, y, extent.cx, extent.cy, render);
}

bool render_win32_text_shadow_and_advance(const char* text) {
    if (text == nullptr) {
        return false;
    }

    SIZE extent{};
    const auto render = [&](HDC dc) {
        ensure_win32_text_font(dc);
        SelectObject(dc, system_ui_state().fonts.selected);
        SetBkMode(dc, TRANSPARENT);

        const int count = static_cast<int>(std::strlen(text));
        const TextRenderCursor& cursor = g_text_renderer_state.cursor;
        const u8 shadow = cursor.shadow_foreground != 0 ? cursor.shadow_foreground : 0;
        SetTextColor(dc, color_ref_from_text_index(shadow));
        const BOOL shadow_ok = TextOutA(dc, cursor.x + 1, cursor.y + 1, text, count);
        SetTextColor(dc, color_ref_from_text_index(cursor.foreground));
        const BOOL text_ok = TextOutA(dc, cursor.x, cursor.y, text, count);
        const BOOL extent_ok = GetTextExtentPoint32A(dc, text, count, &extent);

        if (shadow_ok == FALSE || text_ok == FALSE || extent_ok == FALSE) {
            return false;
        }
        return true;
    };
    if (with_back_buffer_dc(render)) {
        g_text_renderer_state.cursor.x += extent.cx;
        return true;
    }

    if (!measure_win32_text_with_memory_dc(
            text, std::strlen(text), nullptr, extent)) {
        return false;
    }
    const TextRenderCursor cursor = g_text_renderer_state.cursor;
    if (!with_locked_sprite_target_text_dc(cursor.x, cursor.y,
            extent.cx + 1, extent.cy + 1, render)) {
        return false;
    }
    g_text_renderer_state.cursor.x += extent.cx;
    return true;
}
#endif

} // namespace

TextRendererState& text_renderer_state() {
    return g_text_renderer_state;
}

LegacyTickTimerState& legacy_tick_timer_state() {
    return g_legacy_tick_timer_state;
}

void ResetTextRendererState() {
    g_text_renderer_state = {};
    g_text_renderer_state.draw_font_index = 0xff;
    g_text_renderer_state.metric_font_index = 0xff;
    g_text_renderer_state.clip_right = 0x7fffffff;
    g_text_renderer_state.clip_bottom = 0x7fffffff;
    g_text_renderer_state.scratch_radix = 10;
    g_legacy_tick_timer_state = {};
    g_legacy_tick_timer_state.timer_interval_ms = 0x19;
    g_legacy_tick_timer_state.timer_resolution_ms = 1;
}

TextFontDefinition BuildTextFontDefinition(
    u8 flags, u32 max_width, u32 height, const u8* glyph_data,
    std::size_t glyph_data_size) {
    TextFontDefinition font{};
    font.flags = flags;
    font.max_width = max_width;
    font.height = height;
    font.glyph_data = glyph_data;
    font.glyph_data_size = glyph_data_size;

    if ((flags & 8u) != 0) {
        return font;
    }

    if (flags == 0x15) {
        font.row_stride = max_width;
        font.glyph_span = max_width * height;
        return font;
    }

    if ((flags & 4u) == 0) {
        font.row_stride = max_width == 0 ? 0 : ((max_width - 1u) >> 3u) + 1u;
        font.glyph_span = font.row_stride * height;
        if (flags == 3) {
            font.bitmap_offset = 0x100;
        }
        return font;
    }

    font.row_stride = max_width;
    font.glyph_span = max_width * height;
    if (flags == 7) {
        font.bitmap_offset = 0x100;
    }
    return font;
}

void RegisterTextFontDefinition(
    u32 index, u8 flags, u32 max_width, u32 height, const u8* glyph_data,
    std::size_t glyph_data_size) {
    SetTextFontDefinition(index, BuildTextFontDefinition(
        flags, max_width, height, glyph_data, glyph_data_size));
}

void SetTextFontDefinition(u32 index, const TextFontDefinition& font) {
    if (index < kTextRendererFontCount) {
        g_text_renderer_state.fonts[index] = font;
    }
}

void SetTextColorPixel(u32 index, u16 pixel) {
    if (index < kTextRendererColorCount) {
        g_text_renderer_state.color_pixels[index] = pixel;
        u32 red = 0;
        u32 green = 0;
        u32 blue = 0;
        if (SurfacePixelMode555()) {
            red = ((pixel >> 10) & 0x1fu) << 3;
            green = ((pixel >> 5) & 0x1fu) << 3;
            blue = (pixel & 0x1fu) << 3;
        }
        else {
            red = ((pixel >> 11) & 0x1fu) << 3;
            green = ((pixel >> 5) & 0x3fu) << 2;
            blue = (pixel & 0x1fu) << 3;
        }
        // COLORREF is 0x00bbggrr.  This fallback preserves the old setter's
        // standalone behavior until a raw palette entry is published.
        g_text_renderer_state.color_refs[index] =
            red | (green << 8) | (blue << 16);
    }
}

void SetTextColorRef(u32 index, u32 color_ref) {
    if (index < kTextRendererColorCount) {
        g_text_renderer_state.color_refs[index] = color_ref & 0x00ffffffu;
    }
}

u32 ResolveTextColorRef(u8 index) {
    return g_text_renderer_state.color_refs[index];
}

void SetTextClipRect(i32 left, i32 top, i32 right, i32 bottom) {
    g_text_renderer_state.clip_left = left;
    g_text_renderer_state.clip_top = top;
    g_text_renderer_state.clip_right = right;
    g_text_renderer_state.clip_bottom = bottom;
}

void SetTextCursor(i32 x, i32 y, u8 foreground, u8 background) {
    g_text_renderer_state.cursor.x = x;
    g_text_renderer_state.cursor.y = y;
    g_text_renderer_state.cursor.foreground = foreground;
    g_text_renderer_state.cursor.background = background;
}

bool SelectTextDrawFont(u8 index) {
    if (index >= kTextRendererFontCount) {
        return false;
    }
    if (g_text_renderer_state.draw_font_index == index) {
        return true;
    }

    const auto& font = g_text_renderer_state.fonts[index];
    if (!can_select_draw_font(font)) {
        return false;
    }

    g_text_renderer_state.draw_font_index = index;
    g_text_renderer_state.draw_font = font;
    return true;
}

bool SelectTextMetricFont(u8 index) {
    if (index >= kTextRendererFontCount) {
        return false;
    }
    if (g_text_renderer_state.metric_font_index == index) {
        return true;
    }

    const auto& font = g_text_renderer_state.fonts[index];
    if (!can_select_metric_font(font)) {
        return false;
    }

    g_text_renderer_state.metric_font_index = index;
    g_text_renderer_state.metric_font = font;
    return true;
}

bool MeasureTextExtent(const char* text) {
    if (text == nullptr) {
        return false;
    }

    if (uses_win32_font_path()) {
#ifdef _WIN32
        return measure_win32_text_run(text, std::strlen(text));
#else
        return false;
#endif
    }

    g_text_renderer_state.measured_width = 0;
    g_text_renderer_state.measured_height = 0;
    const auto& draw_font = g_text_renderer_state.draw_font;
    const auto& metric_font = g_text_renderer_state.metric_font;

    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p != 0) {
        const u8 ch = *p++;
        u32 width = 0;
        u32 height = 0;
        if ((ch & 0x80u) != 0) {
            if (*p == 0) {
                break;
            }
            ++p;
            width = metric_font.max_width;
            height = metric_font.height;
        }
        else {
            // MeasureTextExtent (0x005021af) delegates ASCII glyphs to
            // MeasureAsciiGlyphMetrics (0x0050222e), which reads the draw-font
            // width table/height.  Only DBCS extents come from the metric font.
            width = glyph_width(draw_font, ch);
            height = draw_font.height;
        }
        g_text_renderer_state.measured_width += width;
        g_text_renderer_state.measured_height =
            std::max(g_text_renderer_state.measured_height, height);
    }
    return true;
}

bool MeasureAsciiOnlyTextExtent(const char* text) {
    if (text == nullptr) {
        return false;
    }

    // Match RenderAsciiOnlyTextLine (original 0x00502269): this path is
    // selected solely by the draw font.  A leaked Win32 metric font must not
    // change either the glyphs or the population-counter cursor advance.
    if ((g_text_renderer_state.draw_font.flags & 8u) != 0) {
#ifdef _WIN32
        return measure_win32_text_run(text, std::strlen(text));
#else
        return false;
#endif
    }

    g_text_renderer_state.measured_width = 0;
    g_text_renderer_state.measured_height = 0;
    const TextFontDefinition& font = g_text_renderer_state.draw_font;
    for (const u8* p = reinterpret_cast<const u8*>(text); *p != 0; ++p) {
        g_text_renderer_state.measured_width += glyph_width(font, *p);
        g_text_renderer_state.measured_height =
            std::max(g_text_renderer_state.measured_height, font.height);
    }
    return true;
}

bool MeasureAsciiGlyphMetrics(u8 ch) {
    // Original 0x0050222e reads DAT_0086ad9c/ada0/ada4 (draw-font state).
    const auto& font = g_text_renderer_state.draw_font;
    g_text_renderer_state.measured_width = glyph_width(font, ch);
    g_text_renderer_state.measured_height = font.height;
    return true;
}

bool DrawTextGlyph(u8 ch) {
    const TextRenderCursor saved = g_text_renderer_state.cursor;
    if ((saved.style_flags & 1u) != 0) {
        if (!draw_bitmap_glyph(ch, saved.shadow_foreground, saved.background,
                saved.x + saved.shadow_x, saved.y + saved.shadow_y)) {
            g_text_renderer_state.cursor = saved;
            return false;
        }
        g_text_renderer_state.cursor = saved;
    }

    return draw_bitmap_glyph(ch, saved.foreground,
        (saved.style_flags & 1u) != 0 ? 0 : saved.background, saved.x, saved.y);
}

bool RenderPackedBitmapGlyph(u8 ch) {
    const TextRenderCursor& cursor = g_text_renderer_state.cursor;
    return draw_packed_bitmap_glyph(ch, cursor.foreground, cursor.background,
        cursor.x, cursor.y);
}

bool RenderIndexedBitmapGlyph(u8 ch) {
    const TextRenderCursor& cursor = g_text_renderer_state.cursor;
    return draw_indexed_bitmap_glyph(ch, cursor.foreground, cursor.background,
        cursor.x, cursor.y);
}

bool RenderWin32FontTextRun(const char* text) {
#ifdef _WIN32
    return render_win32_text_run(text, text == nullptr ? 0 : std::strlen(text));
#else
    (void)text;
    return false;
#endif
}

bool RenderWin32FontCharacterAndAdvance(char ch) {
#ifdef _WIN32
    return render_win32_text_run(&ch, 1);
#else
    (void)ch;
    return false;
#endif
}

bool RenderScratchWin32FontCharacter(char ch) {
    return RenderWin32FontCharacterAndAdvance(ch);
}

bool RenderScratchWin32FontGlyphRun(const char* text, std::size_t count) {
    if (text == nullptr) {
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < count; ++i) {
        ok = RenderScratchWin32FontCharacter(text[i]) && ok;
    }
    return ok;
}

bool RenderWin32FontCStringAndAdvance(const char* text) {
    return RenderWin32FontTextRun(text);
}

bool DrawTextString(const char* text) {
    if (uses_win32_font_path()) {
        return RenderWin32FontTextRun(text);
    }
    return draw_ascii_string(text, true);
}

bool RenderAsciiOnlyTextLine(const char* text) {
    // Original 0x00502269 tests only DAT_0086ad98 (the draw-font flags).
    // The generic DBCS renderer also tests the metric font at 0x0086adc8,
    // but this ASCII-only entry point deliberately ignores it.
    if ((g_text_renderer_state.draw_font.flags & 8u) != 0) {
        return RenderWin32FontTextRun(text);
    }
    if (text == nullptr) {
        return false;
    }

    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p != 0) {
        if (!DrawTextGlyph(*p++)) {
            return false;
        }
    }
    return true;
}

void ClearDbcsCompositeBuffer() {
    const auto& font = g_text_renderer_state.metric_font;
    const std::size_t size = std::min<std::size_t>(font.glyph_span,
        g_text_renderer_state.dbcs_composite_buffer.size());
    std::fill(g_text_renderer_state.dbcs_composite_buffer.begin(),
        g_text_renderer_state.dbcs_composite_buffer.begin() + size, 0);
}

void BuildPackedHangulComponentBitmap(const u8* component_bitmap) {
    if (component_bitmap == nullptr) {
        return;
    }
    constexpr std::size_t kPackedComponentBytes = 0x20;
    const std::size_t size = std::min(kPackedComponentBytes,
        g_text_renderer_state.dbcs_composite_buffer.size());
    for (std::size_t i = 0; i < size; ++i) {
        g_text_renderer_state.dbcs_composite_buffer[i] |= component_bitmap[i];
    }
}

void BuildIndexedHangulComponentBitmap(const u8* component_bitmap) {
    if (component_bitmap == nullptr) {
        return;
    }
    constexpr std::size_t kIndexedComponentBytes = 0x120;
    const std::size_t size = std::min(kIndexedComponentBytes,
        g_text_renderer_state.dbcs_composite_buffer.size());
    for (std::size_t i = 0; i < size; ++i) {
        g_text_renderer_state.dbcs_composite_buffer[i] |= component_bitmap[i];
    }
}

u8 BuildDbcsCompositePrefixBytes(u16 code, std::array<u8, 3>& out) {
    out[0] = static_cast<u8>(code >> 8);
    out[1] = static_cast<u8>(code);
    out[2] = static_cast<u8>((code >> 8) & 0xffu);
    return 0xfe;
}

bool RenderPackedDbcsCompositeBuffer() {
    const auto& font = g_text_renderer_state.metric_font;
    return draw_packed_bitmap_block(g_text_renderer_state.dbcs_composite_buffer.data(),
        font.max_width, font.height, font.row_stride,
        g_text_renderer_state.cursor.foreground, g_text_renderer_state.cursor.background,
        g_text_renderer_state.cursor.x, g_text_renderer_state.cursor.y);
}

bool RenderIndexedDbcsCompositeBuffer() {
    const auto& font = g_text_renderer_state.metric_font;
    return draw_indexed_bitmap_block(g_text_renderer_state.dbcs_composite_buffer.data(),
        font.max_width, font.height, font.row_stride,
        g_text_renderer_state.cursor.foreground, g_text_renderer_state.cursor.background,
        g_text_renderer_state.cursor.x, g_text_renderer_state.cursor.y);
}

bool RenderDbcsCompositeGlyph(u16 code) {
    const TextRenderCursor saved = g_text_renderer_state.cursor;

    auto render_once = [&](u8 foreground, u8 background, i32 x, i32 y) {
        g_text_renderer_state.cursor.foreground = foreground;
        g_text_renderer_state.cursor.background = background;
        g_text_renderer_state.cursor.x = x;
        g_text_renderer_state.cursor.y = y;

        const auto& font = g_text_renderer_state.metric_font;
        if (!bitmap_font_available(font)) {
            g_text_renderer_state.cursor.x +=
                static_cast<i32>(font.max_width) + g_text_renderer_state.cursor.advance_extra;
            return true;
        }

        if (font.flags == 0x15) {
            const u8* bitmap = direct_dbcs_bitmap(font, code);
            const bool ok = draw_indexed_bitmap_block(bitmap, font.max_width, font.height,
                font.row_stride, foreground, background, x, y);
            if (ok) {
                g_text_renderer_state.cursor.x +=
                    static_cast<i32>(font.max_width) + g_text_renderer_state.cursor.advance_extra;
            }
            return ok;
        }

        ClearDbcsCompositeBuffer();
        const u8 lead = static_cast<u8>(code >> 8);
        const u8 trail = static_cast<u8>(code);
        const u8 initial = kHangulInitialComponentTable[(lead & 0x7cu) >> 2u];
        const u8 medial =
            kHangulMedialComponentTable[((static_cast<u32>(code) << 3u) >> 8u) & 0x1fu];
        const u8 final = kHangulFinalComponentTable[trail & 0x1fu];
        const u8 final_variant = kHangulFinalVariantByMedial[medial];
        const u8 initial_variant = final == 0 ?
            kHangulInitialVariantNoFinal[initial] :
            kHangulInitialVariantWithFinal[initial];
        const u8 medial_variant = final == 0 ?
            kHangulMedialVariantNoFinal[medial] :
            kHangulMedialVariantWithFinal[medial];

        if ((font.flags & 4u) == 0) {
            if (initial != 0) {
                BuildPackedHangulComponentBitmap(
                    packed_hangul_initial_component(font, initial, medial_variant));
            }
            if (medial != 0) {
                BuildPackedHangulComponentBitmap(
                    packed_hangul_medial_component(font, medial, initial_variant));
            }
            if (final != 0) {
                BuildPackedHangulComponentBitmap(
                    packed_hangul_final_component(font, final, final_variant));
            }
            if (!RenderPackedDbcsCompositeBuffer()) {
                return false;
            }
        } else {
            if (initial != 0) {
                BuildIndexedHangulComponentBitmap(
                    indexed_hangul_initial_component(font, initial, medial_variant));
            }
            if (medial != 0) {
                BuildIndexedHangulComponentBitmap(
                    indexed_hangul_medial_component(font, medial, initial_variant));
            }
            if (final != 0) {
                BuildIndexedHangulComponentBitmap(
                    indexed_hangul_final_component(font, final, final_variant));
            }
            if (!RenderIndexedDbcsCompositeBuffer()) {
                return false;
            }
        }

        g_text_renderer_state.cursor.x +=
            static_cast<i32>(font.max_width) + g_text_renderer_state.cursor.advance_extra;
        return true;
    };

    if ((saved.style_flags & 1u) != 0) {
        if (!render_once(saved.shadow_foreground, saved.background, saved.x + saved.shadow_x,
                saved.y + saved.shadow_y)) {
            g_text_renderer_state.cursor = saved;
            return false;
        }
        g_text_renderer_state.cursor = saved;

        const bool ok = render_once(saved.foreground, 0, saved.x, saved.y);
        g_text_renderer_state.cursor.foreground = saved.foreground;
        g_text_renderer_state.cursor.background = saved.background;
        return ok;
    }

    return render_once(saved.foreground, saved.background, saved.x, saved.y);
}

bool RenderDbcsGlyphCore(u16 code) {
    return RenderDbcsCompositeGlyph(code);
}

bool RenderDirectDbcsGlyph(u16 code) {
    const auto& font = g_text_renderer_state.metric_font;
    if (font.flags != 0x15) {
        return RenderDbcsCompositeGlyph(code);
    }

    const TextRenderCursor& cursor = g_text_renderer_state.cursor;
    const u8* bitmap = direct_dbcs_bitmap(font, code);
    const bool ok = draw_indexed_bitmap_block(bitmap, font.max_width, font.height,
        font.row_stride, cursor.foreground, cursor.background, cursor.x, cursor.y);
    if (ok) {
        g_text_renderer_state.cursor.x +=
            static_cast<i32>(font.max_width) + g_text_renderer_state.cursor.advance_extra;
    }
    return ok;
}

bool DrawTextLineUntilCrLf(const char* text) {
    if (text == nullptr) {
        return false;
    }

    if (uses_win32_font_path()) {
#ifdef _WIN32
        std::size_t length = 0;
        while (text[length] != '\0') {
            if (text[length] == '\r' && text[length + 1] == '\n') {
                break;
            }
            ++length;
        }
        return render_win32_text_run(text, length);
#else
        return false;
#endif
    }

    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p != 0) {
        if (p[0] == '\r' && p[1] == '\n') {
            return true;
        }
        const u8 ch = *p++;
        if ((ch & 0x80u) == 0) {
            if (!DrawTextGlyph(ch)) {
                return false;
            }
            continue;
        }
        if (*p == 0) {
            return false;
        }
        const u16 code = static_cast<u16>((static_cast<u16>(ch) << 8) | *p++);
        if (!RenderDbcsCompositeGlyph(code)) {
            return false;
        }
    }
    return true;
}

bool DrawCenterAlignedText(const char* text) {
    if (!MeasureTextExtent(text)) {
        return false;
    }
    g_text_renderer_state.cursor.x -= static_cast<i32>(g_text_renderer_state.measured_width >> 1);
    g_text_renderer_state.cursor.y -= static_cast<i32>(g_text_renderer_state.measured_height >> 1);
    return DrawTextString(text);
}

#ifdef _WIN32
bool RenderWin32FontRunAndAdvance(const char* text) {
    return RenderWin32FontTextRun(text);
}

bool AdvanceWin32FontScratchExtentOnly() {
    const char* text = g_text_renderer_state.scratch_buffer.data();
    if (!measure_win32_text_run(text, std::strlen(text))) {
        return false;
    }
    g_text_renderer_state.cursor.x += static_cast<i32>(g_text_renderer_state.measured_width);
    return true;
}

bool DrawWin32FontTextAt(i32 x, i32 y, HFONT font, u8 foreground, u8 background,
    const char* text) {
    return draw_win32_text_at(x, y, font, foreground, background, text, false);
}

bool DrawCenteredWin32FontTextAt(i32 x, i32 y, HFONT font, u8 foreground, u8 background,
    const char* text) {
    return draw_win32_text_at(x, y, font, foreground, background, text, true);
}

bool RenderWin32FontTextShadowAndAdvance(const char* text) {
    return render_win32_text_shadow_and_advance(text);
}

void CALLBACK IncrementLegacyTimerCounterProc(
    UINT timer_id, UINT message, DWORD_PTR user, DWORD_PTR param1, DWORD_PTR param2) {
    increment_legacy_timer_counter_proc(timer_id, message, user, param1, param2);
}

bool QueryLegacyTimerResolution() {
    return query_legacy_timer_resolution();
}

void StartLegacyTimerEvent() {
    start_legacy_timer_event();
}

void StopLegacyTimerEvent() {
    stop_legacy_timer_event();
}
#endif

void SetTextScratchRadix(u32 radix) {
    if (radix >= 2 && radix <= 10) {
        g_text_renderer_state.scratch_radix = radix;
    }
}

const char* FormatUnsignedToScratchBuffer(u32 value) {
    auto& buffer = g_text_renderer_state.scratch_buffer;

    char* out = buffer.data() + buffer.size() - 1;
    const u32 radix = g_text_renderer_state.scratch_radix;
    do {
        --out;
        const u32 digit = value % radix;
        *out = static_cast<char>('0' + digit);
        value /= radix;
    } while (value != 0);

    return out;
}

bool AppendTextEditCharacterAndRender(char* text, std::size_t max_count, char ch) {
    const auto value = static_cast<unsigned char>(ch);
    if (text == nullptr || max_count == 0 || value == 0 || value >= 0x80u) {
        return false;
    }

    std::size_t offset = 0;
    std::size_t remaining = max_count;
    while (remaining != 0 && text[offset] != '\0') {
        --remaining;
        if (remaining == 0) {
            break;
        }
        ++offset;
    }

    text[offset] = ch;
    text[offset + 1] = '\0';
    return RenderWin32FontTextRun(text);
}

u32 MeasureCStringLength(const char* text) {
    return static_cast<u32>(std::strlen(text));
}

u32 RefreshLegacyTickTime() {
#ifdef _WIN32
    g_legacy_tick_timer_state.current_tick_ms = timeGetTime();
#else
    using clock = std::chrono::steady_clock;
    const auto now = clock::now().time_since_epoch();
    g_legacy_tick_timer_state.current_tick_ms =
        static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
#endif
    return g_legacy_tick_timer_state.current_tick_ms;
}

void WaitLegacyTickDuration(u32 duration_ms) {
    const u32 start = RefreshLegacyTickTime();
    g_legacy_tick_timer_state.wait_duration_ms = duration_ms;
    while (static_cast<u32>(RefreshLegacyTickTime() - start) <= duration_ms) {
    }
}

void MarkLegacyTickStart() {
    g_legacy_tick_timer_state.marked_tick_ms = RefreshLegacyTickTime();
}

void WaitUntilLegacyTickElapsed() {
    while (static_cast<u32>(RefreshLegacyTickTime() -
               g_legacy_tick_timer_state.marked_tick_ms) <=
        g_legacy_tick_timer_state.wait_duration_ms) {
    }
}

void StartLegacyPeriodicTickCounter(u32 interval_ms) {
    (void)interval_ms;
    g_legacy_tick_timer_state.timer_interval_ms = 0x19;
    g_legacy_tick_timer_state.timer_counter_user = &g_legacy_tick_timer_state.timer_counter;
#ifdef _WIN32
    start_legacy_timer_event();
    if (g_legacy_tick_timer_state.timer_event_id == 0) {
        g_legacy_tick_timer_state.saved_timer_event_id =
            g_legacy_tick_timer_state.timer_event_id;
    }
#else
    g_legacy_tick_timer_state.timer_resolution_ms = 1;
    g_legacy_tick_timer_state.timer_event_id = 1;
    g_legacy_tick_timer_state.periodic_timer_active = true;
#endif
}

void StopLegacyPeriodicTickCounter() {
    if (g_legacy_tick_timer_state.saved_timer_event_id != 0) {
        g_legacy_tick_timer_state.timer_event_id =
            g_legacy_tick_timer_state.saved_timer_event_id;
    }
#ifdef _WIN32
    stop_legacy_timer_event();
#else
    g_legacy_tick_timer_state.periodic_timer_active = false;
#endif
    g_legacy_tick_timer_state.saved_timer_event_id = 0;
}

void RestoreLegacyPeriodicTickCounter() {
    if (!g_legacy_tick_timer_state.periodic_timer_active) {
        StartLegacyPeriodicTickCounter(g_legacy_tick_timer_state.timer_interval_ms);
    }
}

}
