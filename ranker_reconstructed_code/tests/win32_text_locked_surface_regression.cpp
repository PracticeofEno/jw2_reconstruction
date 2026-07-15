#include "ranker_directx.h"
#include "ranker_palette_cache.h"
#include "ranker_sprite_renderer.h"
#include "ranker_system_ui.h"
#include "ranker_text_renderer.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace ranker {
namespace {

DirectDrawRuntimeState g_test_direct_draw;
SpriteRenderState g_test_sprite_render;
RankerSystemUiState g_test_system_ui;

} // namespace

const DirectDrawRuntimeState& direct_draw_state() {
    return g_test_direct_draw;
}

const SpriteRenderState& sprite_render_state() {
    return g_test_sprite_render;
}

bool SurfacePixelMode555() {
    return false;
}

RankerSystemUiState& system_ui_state() {
    return g_test_system_ui;
}

void InitializeUiFontHandles() {
}

bool InitializeWin32UiFontMetrics(HDC) {
    g_test_system_ui.fonts.metrics_initialized =
        g_test_system_ui.fonts.selected != nullptr;
    return g_test_system_ui.fonts.metrics_initialized;
}

} // namespace ranker

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace ranker;

    constexpr u32 kWidth = 160;
    constexpr u32 kHeight = 48;
    constexpr u16 kBackground = 0x001f;
    std::vector<u16> pixels(kWidth * kHeight, kBackground);
    g_test_sprite_render.active = true;
    g_test_sprite_render.target = {pixels.data(), kWidth, kHeight, kWidth};
    g_test_direct_draw.active = false;
    g_test_direct_draw.width = kWidth;
    g_test_direct_draw.height = kHeight;

    HFONT font = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
    expect(font != nullptr, "failed to create focused regression font");

    ResetTextRendererState();
    g_test_system_ui.fonts.handles[1] = font;
    g_test_system_ui.fonts.selected = font;
    g_test_system_ui.fonts.metrics_initialized = true;
    RegisterTextFontDefinition(4, 8, 16, 16, nullptr, 0);
    expect(SelectTextDrawFont(4), "font-4 draw selection failed");
    expect(SelectTextMetricFont(4), "font-4 metric selection failed");
    SetTextColorPixel(1, 0xf800);
    SetTextColorPixel(0xe9, 0x0000);
    expect(ResolveTextColorRef(1) == 0x000000f8u,
        "16-bit-only text color fallback changed");
    // JW2_01.TRC record 4 entries used by selected names and dynamic world
    // names.  The original forwards these three raw bytes to SetTextColor;
    // it does not expand the already-quantized surface pixel.
    SetTextColorRef(1, 0x00f2f2f2u);
    expect(ResolveTextColorRef(1) == 0x00f2f2f2u,
        "selected-name raw RGB was quantized through the 16-bit palette");
    SetTextColorRef(0xff, 0x00fffffcu);
    expect(ResolveTextColorRef(0xff) == 0x00fffffcu,
        "world-name raw RGB was quantized through the 16-bit palette");
    expect(text_renderer_state().color_pixels[1] == 0xf800u,
        "raw Win32 color publication changed bitmap glyph pixels");

    expect(MeasureTextExtent("Need more berries"),
        "memory-DC text measurement failed without a DirectDraw DC");
    TextRendererState& renderer = text_renderer_state();
    expect(renderer.measured_width > 0 && renderer.measured_height > 0,
        "memory-DC text measurement returned an empty extent");
    expect(renderer.win32_memory_measure_fallback_count == 1,
        "font-4 measurement did not use the no-surface memory DC");

    SetTextCursor(4, 4, 1);
    renderer.cursor.shadow_foreground = 0xe9;
    const i32 initial_x = renderer.cursor.x;
    expect(RenderWin32FontTextShadowAndAdvance("Need more berries"),
        "font-4 shadow text failed on the locked sprite-target path");
    expect(renderer.win32_sprite_target_fallback_count == 1,
        "font-4 draw did not use the locked sprite-target fallback");
    expect(renderer.cursor.x > initial_x,
        "font-4 fallback did not preserve cursor advance semantics");
    expect(std::any_of(pixels.begin(), pixels.end(), [](u16 pixel) {
        return pixel != kBackground;
    }), "font-4 fallback did not modify the locked 16-bit target");
    expect(pixels.back() == kBackground,
        "small DIB fallback modified pixels outside the text rectangle");

    const u32 fallback_count = renderer.win32_sprite_target_fallback_count;
    const SpriteRenderTarget saved_target = g_test_sprite_render.target;
    g_test_sprite_render.target = {};
    SetTextCursor(2, 2, 1);
    expect(!RenderWin32FontTextRun("X"),
        "font-4 draw unexpectedly succeeded without any render target");
    expect(renderer.win32_sprite_target_fallback_count == fallback_count,
        "unavailable-target draw was counted as a fallback render");
    g_test_sprite_render.target = saved_target;

    g_test_system_ui.fonts.handles[1] = nullptr;
    g_test_system_ui.fonts.selected = nullptr;
    DeleteObject(font);
    std::cout << "WIN32_TEXT_LOCKED_SURFACE_PASS fallback_draws="
              << fallback_count << '\n';
    return 0;
}
