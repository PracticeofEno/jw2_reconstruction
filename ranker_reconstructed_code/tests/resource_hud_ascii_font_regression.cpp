#include "ranker_directx.h"
#include "ranker_sprite_renderer.h"
#include "ranker_system_ui.h"
#include "ranker_text_renderer.h"

#include <cstdlib>
#include <iostream>

namespace ranker {

const DirectDrawRuntimeState& direct_draw_state() {
    static const DirectDrawRuntimeState state{};
    return state;
}

const SpriteRenderState& sprite_render_state() {
    static const SpriteRenderState state{};
    return state;
}

bool SurfacePixelMode555() {
    return false;
}

RankerSystemUiState& system_ui_state() {
    static RankerSystemUiState state{};
    return state;
}

void InitializeUiFontHandles() {}

bool InitializeWin32UiFontMetrics(HDC) {
    return false;
}

} // namespace ranker

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "RESOURCE_HUD_ASCII_FONT_FAIL " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    using namespace ranker;

    ResetTextRendererState();
    RegisterTextFontDefinition(1, 3, 7, 11, nullptr, 0);
    RegisterTextFontDefinition(4, 8, 16, 16, nullptr, 0);
    require(SelectTextDrawFont(1), "bitmap draw-font selection failed");
    require(SelectTextMetricFont(4), "Win32 metric-font selection failed");

    require(MeasureAsciiOnlyTextExtent("123"),
        "ASCII-only extent rejected the bitmap draw font");
    const TextRendererState& measured = text_renderer_state();
    require(measured.measured_width == 21 && measured.measured_height == 11,
        "Win32 metric font changed bitmap resource-counter extent");

    SetTextCursor(3, 5, 1);
    require(RenderAsciiOnlyTextLine("123"),
        "ASCII-only draw incorrectly entered the Win32 font path");
    require(text_renderer_state().cursor.x == 24,
        "bitmap resource-counter cursor advance changed");

    std::cout << "RESOURCE_HUD_ASCII_FONT_PASS\n";
    return EXIT_SUCCESS;
}
