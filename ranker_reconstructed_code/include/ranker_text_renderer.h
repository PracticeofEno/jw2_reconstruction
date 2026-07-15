#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <cstddef>

namespace ranker {

constexpr u32 kTextRendererFontCount = 8;
constexpr u32 kTextRendererColorCount = 0x100;
constexpr u32 kTextRendererScratchSize = 0x40;
constexpr u32 kTextRendererDbcsCompositeBufferSize = 0x200;

struct TextFontDefinition {
    u8 flags = 0;
    u32 max_width = 0;
    u32 height = 0;
    const u8* glyph_data = nullptr;
    std::size_t glyph_data_size = 0;
    u32 glyph_span = 0;
    u32 row_stride = 0;
    u32 bitmap_offset = 0;
};

struct TextRenderCursor {
    i32 x = 0;
    i32 y = 0;
    u8 foreground = 1;
    u8 background = 0;
    u8 shadow_foreground = 0;
    u8 style_flags = 0;
    i32 shadow_x = 0;
    i32 shadow_y = 0;
    i32 advance_extra = 0;
};

struct TextRendererState {
    std::array<TextFontDefinition, kTextRendererFontCount> fonts{};
    std::array<u16, kTextRendererColorCount> color_pixels{};
    // FUN_005077f0 indexes DAT_0086afec as three raw RGB bytes and passes
    // that little-endian value directly to GDI.  Keep it separate from the
    // 16-bit pixels used by bitmap glyphs so Win32 font colors do not make a
    // lossy 555/565 round trip.
    std::array<u32, kTextRendererColorCount> color_refs{};
    TextFontDefinition draw_font{};
    TextFontDefinition metric_font{};
    TextRenderCursor cursor{};
    u8 draw_font_index = 0xff;
    u8 metric_font_index = 0xff;
    u32 measured_width = 0;
    u32 measured_height = 0;
    // DirectDraw refuses GetDC while its surface is locked for the software
    // sprite pass.  These counters make the Win32-font memory-DC fallback
    // observable in focused regressions without changing draw semantics.
    u32 win32_memory_measure_fallback_count = 0;
    u32 win32_sprite_target_fallback_count = 0;
    i32 clip_left = 0;
    i32 clip_top = 0;
    i32 clip_right = 0x7fffffff;
    i32 clip_bottom = 0x7fffffff;
    std::array<char, kTextRendererScratchSize> scratch_buffer{};
    std::array<u8, kTextRendererDbcsCompositeBufferSize> dbcs_composite_buffer{};
    u32 scratch_radix = 10;
};

struct LegacyTickTimerState {
    u32 current_tick_ms = 0;
    u32 marked_tick_ms = 0;
    u32 wait_duration_ms = 0;
    u32 timer_interval_ms = 0x19;
    u32 timer_resolution_ms = 1;
    u32 timer_resolution_max_ms = 0;
    u32 timer_event_id = 0;
    u32 saved_timer_event_id = 0;
    u32 timer_counter = 0;
    u32* timer_counter_user = nullptr;
    bool periodic_timer_active = false;
};

TextRendererState& text_renderer_state();
LegacyTickTimerState& legacy_tick_timer_state();
void ResetTextRendererState();
TextFontDefinition BuildTextFontDefinition(
    u8 flags, u32 max_width, u32 height, const u8* glyph_data,
    std::size_t glyph_data_size = 0);
void RegisterTextFontDefinition(
    u32 index, u8 flags, u32 max_width, u32 height, const u8* glyph_data,
    std::size_t glyph_data_size = 0);
void SetTextFontDefinition(u32 index, const TextFontDefinition& font);
void SetTextColorPixel(u32 index, u16 pixel);
void SetTextColorRef(u32 index, u32 color_ref);
u32 ResolveTextColorRef(u8 index);
void SetTextClipRect(i32 left, i32 top, i32 right, i32 bottom);
void SetTextCursor(i32 x, i32 y, u8 foreground, u8 background = 0);

bool SelectTextDrawFont(u8 index);
bool SelectTextMetricFont(u8 index);
bool MeasureTextExtent(const char* text);
bool MeasureAsciiGlyphMetrics(u8 ch);
bool DrawTextGlyph(u8 ch);
bool RenderPackedBitmapGlyph(u8 ch);
bool RenderIndexedBitmapGlyph(u8 ch);
bool RenderWin32FontTextRun(const char* text);
bool RenderWin32FontCharacterAndAdvance(char ch);
bool RenderScratchWin32FontCharacter(char ch);
bool RenderScratchWin32FontGlyphRun(const char* text, std::size_t count);
bool RenderWin32FontCStringAndAdvance(const char* text);
bool DrawTextString(const char* text);
bool RenderAsciiOnlyTextLine(const char* text);
bool RenderDbcsCompositeGlyph(u16 code);
bool RenderDbcsGlyphCore(u16 code);
bool RenderDirectDbcsGlyph(u16 code);
void ClearDbcsCompositeBuffer();
void BuildPackedHangulComponentBitmap(const u8* component_bitmap);
void BuildIndexedHangulComponentBitmap(const u8* component_bitmap);
u8 BuildDbcsCompositePrefixBytes(u16 code, std::array<u8, 3>& out);
bool RenderPackedDbcsCompositeBuffer();
bool RenderIndexedDbcsCompositeBuffer();
bool DrawTextLineUntilCrLf(const char* text);
bool DrawCenterAlignedText(const char* text);
#ifdef _WIN32
bool RenderWin32FontRunAndAdvance(const char* text);
bool AdvanceWin32FontScratchExtentOnly();
bool DrawWin32FontTextAt(i32 x, i32 y, HFONT font, u8 foreground, u8 background,
    const char* text);
bool DrawCenteredWin32FontTextAt(i32 x, i32 y, HFONT font, u8 foreground, u8 background,
    const char* text);
bool RenderWin32FontTextShadowAndAdvance(const char* text);
void CALLBACK IncrementLegacyTimerCounterProc(
    UINT timer_id, UINT message, DWORD_PTR user, DWORD_PTR param1, DWORD_PTR param2);
bool QueryLegacyTimerResolution();
void StartLegacyTimerEvent();
void StopLegacyTimerEvent();
#endif
void SetTextScratchRadix(u32 radix);
const char* FormatUnsignedToScratchBuffer(u32 value);
bool AppendTextEditCharacterAndRender(char* text, std::size_t max_count, char ch);
u32 MeasureCStringLength(const char* text);
u32 RefreshLegacyTickTime();
void WaitLegacyTickDuration(u32 duration_ms);
void MarkLegacyTickStart();
void WaitUntilLegacyTickElapsed();
void StartLegacyPeriodicTickCounter(u32 interval_ms = 0x19);
void StopLegacyPeriodicTickCounter();
void RestoreLegacyPeriodicTickCounter();

}
