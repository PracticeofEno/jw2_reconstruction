#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstddef>
#include <vector>

namespace ranker {

struct RawIndexedBitmapStrip {
    bool loaded = false;
#ifdef _WIN32
    HPALETTE palette = nullptr;
#else
    void* palette = nullptr;
#endif
    std::vector<u8> bitmap_info_bytes;
    const u8* pixel_frames = nullptr;
    i32 width = 0x20;
    i32 height = 0x20;
    i32 aligned_width = 0x20;
    u32 frame_count = 0;

#ifdef _WIN32
    const BITMAPINFO* bitmap_info() const;
#endif
};

RawIndexedBitmapStrip& InitializeRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip);
RawIndexedBitmapStrip& InitializeRawIndexedBitmapStripAlias(
    RawIndexedBitmapStrip& strip);
void HandleRawIndexedBitmapStripDestructor(RawIndexedBitmapStrip& strip);
void ReleaseRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip);

bool LoadRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip, const void* pixel_frames,
    i32 width, i32 height, const void* palette_rgba = nullptr,
    std::size_t palette_bytes = 0);
bool LoadRawIndexedBitmapStripFromPaletteSlot(RawIndexedBitmapStrip& strip,
    const void* pixel_frames, i32 width, i32 height, u32 palette_slot);
void HandleSecondaryRawIndexedBitmapStripDestructor(RawIndexedBitmapStrip& strip);
void ReleaseSecondaryRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip);
bool LoadSecondaryRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip,
    const void* pixel_frames, i32 width, i32 height, const void* palette_rgba = nullptr,
    std::size_t palette_bytes = 0);

#ifdef _WIN32
void DrawRawIndexedBitmapStripFrame(const RawIndexedBitmapStrip& strip, HDC dc,
    i32 x, i32 y, i32 frame_index);
void DrawSecondaryRawIndexedBitmapStripFrame(const RawIndexedBitmapStrip& strip, HDC dc,
    i32 x, i32 y, i32 frame_index);
#endif

i32 AbsInt32(i32 value);

}
