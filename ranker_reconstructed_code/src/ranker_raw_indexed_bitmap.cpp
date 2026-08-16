#include "ranker_raw_indexed_bitmap.h"

#include "ranker_palette_cache.h"
#include "ranker_crt_runtime.h"

#include <cstring>

namespace ranker {
namespace {

constexpr std::size_t kBitmapInfoHeaderSize = 0x28;
constexpr std::size_t kPaletteEntries = 0x100;
constexpr std::size_t kDibPaletteBytes = kPaletteEntries * 4u;
constexpr std::size_t kBitmapInfoBytes = kBitmapInfoHeaderSize + kDibPaletteBytes;

void write_le_u16(u8* p, u16 value) {
    p[0] = static_cast<u8>(value & 0xffu);
    p[1] = static_cast<u8>((value >> 8) & 0xffu);
}

void write_le_u32(u8* p, u32 value) {
    p[0] = static_cast<u8>(value & 0xffu);
    p[1] = static_cast<u8>((value >> 8) & 0xffu);
    p[2] = static_cast<u8>((value >> 16) & 0xffu);
    p[3] = static_cast<u8>((value >> 24) & 0xffu);
}

void write_le_i32(u8* p, i32 value) {
    write_le_u32(p, static_cast<u32>(value));
}

i32 legacy_mod4(i32 value) {
    i32 remainder = value & 0x80000003;
    if (remainder < 0) {
        remainder = ((remainder - 1) | ~3) + 1;
    }
    return remainder;
}

i32 align_width_to_dword(i32 width) {
    if (legacy_mod4(width) == 0) {
        return width;
    }
    return (((width + ((width >> 31) & 3)) >> 2) * 4) + 4;
}

const u8* palette_source_or_default(const void* palette_rgba, std::size_t palette_bytes,
    std::array<u8, kDibPaletteBytes>& fallback) {
    if (palette_rgba != nullptr && palette_bytes >= kDibPaletteBytes) {
        return static_cast<const u8*>(palette_rgba);
    }

    for (u32 i = 0; i < kPaletteEntries; ++i) {
        fallback[i * 4u] = static_cast<u8>(i);
        fallback[i * 4u + 1u] = static_cast<u8>(i);
        fallback[i * 4u + 2u] = static_cast<u8>(i);
        fallback[i * 4u + 3u] = 0;
    }
    return fallback.data();
}

void fill_bitmap_info(RawIndexedBitmapStrip& strip, const u8* palette_rgba) {
    strip.bitmap_info_bytes.assign(kBitmapInfoBytes, 0);
    u8* info = strip.bitmap_info_bytes.data();
    write_le_u32(info, static_cast<u32>(kBitmapInfoHeaderSize));
    write_le_i32(info + 0x04, strip.aligned_width);
    write_le_i32(info + 0x08, strip.height);
    write_le_u16(info + 0x0c, 1);
    write_le_u16(info + 0x0e, 8);

    u8* colors = info + kBitmapInfoHeaderSize;
    for (u32 i = 0; i < kPaletteEntries; ++i) {
        const u8* source = palette_rgba + i * 4u;
        colors[i * 4u] = source[2];
        colors[i * 4u + 1u] = source[1];
        colors[i * 4u + 2u] = source[0];
        colors[i * 4u + 3u] = 0;
    }
}

#ifdef _WIN32
bool create_logical_palette(RawIndexedBitmapStrip& strip, const u8* palette_rgba) {
    const std::size_t log_palette_bytes =
        sizeof(LOGPALETTE) + (kPaletteEntries - 1u) * sizeof(PALETTEENTRY);
    std::vector<u8> storage(log_palette_bytes, 0);
    auto* logical_palette = reinterpret_cast<LOGPALETTE*>(storage.data());
    logical_palette->palVersion = 0x0300;
    logical_palette->palNumEntries = static_cast<WORD>(kPaletteEntries);
    std::memcpy(logical_palette->palPalEntry, palette_rgba, kDibPaletteBytes);
    strip.palette = CreatePalette(logical_palette);
    return strip.palette != nullptr;
}

HPALETTE select_strip_palette(const RawIndexedBitmapStrip& strip, HDC dc) {
    if (strip.palette == nullptr) {
        return nullptr;
    }
    return SelectPalette(dc, strip.palette, TRUE);
}

void restore_palette(HDC dc, HPALETTE old_palette) {
    if (old_palette != nullptr) {
        SelectPalette(dc, old_palette, TRUE);
    }
}
#else
bool create_logical_palette(RawIndexedBitmapStrip&, const u8*) {
    return true;
}
#endif

} // namespace

#ifdef _WIN32
const BITMAPINFO* RawIndexedBitmapStrip::bitmap_info() const {
    if (bitmap_info_bytes.empty()) {
        return nullptr;
    }
    return reinterpret_cast<const BITMAPINFO*>(bitmap_info_bytes.data());
}
#endif

RawIndexedBitmapStrip& InitializeRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip) {
    strip.loaded = false;
    strip.bitmap_info_bytes.clear();
    strip.palette = nullptr;
    strip.width = 0x20;
    strip.height = 0x20;
    strip.aligned_width = 0x20;
    strip.frame_count = 0;
    return strip;
}

RawIndexedBitmapStrip& InitializeRawIndexedBitmapStripAlias(
    RawIndexedBitmapStrip& strip) {
    return InitializeRawIndexedBitmapStrip(strip);
}

void HandleRawIndexedBitmapStripDestructor(RawIndexedBitmapStrip& strip) {
    ReleaseRawIndexedBitmapStrip(strip);
}

void ReleaseRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip) {
#ifdef _WIN32
    if (strip.palette != nullptr) {
        DeleteObject(strip.palette);
        strip.palette = nullptr;
    }
#endif
    std::vector<u8>{}.swap(strip.bitmap_info_bytes);
    strip.loaded = false;
    strip.palette = nullptr;
    strip.frame_count = 0;
}

bool LoadRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip, const void* pixel_frames,
    i32 width, i32 height, const void* palette_rgba, std::size_t palette_bytes) {
    ReleaseRawIndexedBitmapStrip(strip);
    strip.pixel_frames = static_cast<const u8*>(pixel_frames);
    strip.width = width;
    strip.height = height;
    strip.aligned_width = align_width_to_dword(width);

    std::array<u8, kDibPaletteBytes> fallback_palette{};
    const u8* palette =
        palette_source_or_default(palette_rgba, palette_bytes, fallback_palette);
    fill_bitmap_info(strip, palette);
    if (!create_logical_palette(strip, palette)) {
        ReleaseRawIndexedBitmapStrip(strip);
        return false;
    }

    strip.loaded = true;
    return true;
}

bool LoadRawIndexedBitmapStripFromPaletteSlot(RawIndexedBitmapStrip& strip,
    const void* pixel_frames, i32 width, i32 height, u32 palette_slot) {
    const PaletteCacheState& cache = palette_cache_state();
    if (!IsPaletteCacheSlotActive(palette_slot)) {
        return LoadRawIndexedBitmapStrip(strip, pixel_frames, width, height);
    }

    return LoadRawIndexedBitmapStrip(strip, pixel_frames, width, height,
        cache.raw_slots[palette_slot].data(), cache.raw_slots[palette_slot].size());
}

void HandleSecondaryRawIndexedBitmapStripDestructor(RawIndexedBitmapStrip& strip) {
    ReleaseRawIndexedBitmapStrip(strip);
}

void ReleaseSecondaryRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip) {
    ReleaseRawIndexedBitmapStrip(strip);
}

bool LoadSecondaryRawIndexedBitmapStrip(RawIndexedBitmapStrip& strip,
    const void* pixel_frames, i32 width, i32 height, const void* palette_rgba,
    std::size_t palette_bytes) {
    return LoadRawIndexedBitmapStrip(strip, pixel_frames, width, height, palette_rgba,
        palette_bytes);
}

#ifdef _WIN32
void DrawRawIndexedBitmapStripFrame(const RawIndexedBitmapStrip& strip, HDC dc,
    i32 x, i32 y, i32 frame_index) {
    if (!strip.loaded || dc == nullptr) {
        return;
    }

    const i32 frame_height = AbsInt32(strip.height);
    const std::size_t source_stride = static_cast<std::size_t>(strip.width);
    const std::size_t source_frame_bytes =
        source_stride * static_cast<std::size_t>(frame_height);
    const u8* source = strip.pixel_frames + source_frame_bytes * frame_index;
    const void* bits = source;
    void* padded_frame = nullptr;

    if (legacy_mod4(strip.width) != 0) {
        const i32 aligned_width = align_width_to_dword(strip.width);
        const std::size_t destination_stride = static_cast<std::size_t>(aligned_width);
        padded_frame = _malloc(destination_stride * static_cast<std::size_t>(frame_height));
        for (i32 row = 0; row < frame_height; ++row) {
            std::memcpy(static_cast<u8*>(padded_frame) +
                    destination_stride * static_cast<std::size_t>(row),
                source + source_stride * static_cast<std::size_t>(row), source_stride);
        }
        bits = padded_frame;
    }

    HPALETTE old_palette = select_strip_palette(strip, dc);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, strip.width, frame_height, 0, 0,
        strip.width, frame_height, bits, strip.bitmap_info(), DIB_RGB_COLORS, SRCCOPY);
    restore_palette(dc, old_palette);
    if (padded_frame != nullptr) {
        CrtFree(padded_frame);
    }
}

void DrawSecondaryRawIndexedBitmapStripFrame(const RawIndexedBitmapStrip& strip, HDC dc,
    i32 x, i32 y, i32 frame_index) {
    DrawRawIndexedBitmapStripFrame(strip, dc, x, y, frame_index);
}
#endif

i32 AbsInt32(i32 value) {
    return value < 0 ? -value : value;
}

}
