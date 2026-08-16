#include "ranker_palette_cache.h"

#include "ranker_crt_runtime.h"
#include "ranker_directx.h"
#include "ranker_sprite_renderer.h"
#include "ranker_trc.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace ranker {
namespace {

PaletteCacheState g_palette_cache_state;

constexpr u32 kUnitRampSourceSlot = 0;
constexpr u32 kUnitRampSourceStride = 0x10;
constexpr u32 kUnitRampTargetFirstIndex = 0xf5;
constexpr u32 kUnitRampColorCount = 10;

bool valid_slot(u32 slot_index) {
    return slot_index < g_palette_cache_state.next_slot &&
        slot_index < kPaletteCacheSlotCount;
}

bool surface_uses_565() {
    return !SurfacePixelMode555();
}

u32 next_slot_or_invalid() {
    if (g_palette_cache_state.next_slot >= kPaletteCacheSlotCount) {
        return kInvalidPaletteCacheSlot;
    }
    return g_palette_cache_state.next_slot;
}

u32 commit_next_slot() {
    const u32 slot = next_slot_or_invalid();
    if (slot != kInvalidPaletteCacheSlot) {
        u64 serial = g_palette_cache_state.next_allocation_serial++;
        if (serial == 0) {
            serial = g_palette_cache_state.next_allocation_serial++;
        }
        if (g_palette_cache_state.next_allocation_serial == 0) {
            ++g_palette_cache_state.next_allocation_serial;
        }
        g_palette_cache_state.allocation_serials[slot] = serial;
        ++g_palette_cache_state.next_slot;
    }
    return slot;
}

} // namespace

u16 SurfaceRedMask() {
#ifdef _WIN32
    const u32 red_mask = direct_draw_state().red_mask;
    if (red_mask != 0) {
        return static_cast<u16>(red_mask);
    }
#endif
    return surface_uses_565() ? 0xf800u : 0x7c00u;
}

bool SurfacePixelMode555() {
#ifdef _WIN32
    const DirectDrawRuntimeState& draw = direct_draw_state();
    if (draw.green_mask != 0) {
        return draw.green_mask != 0x07e0u;
    }
    if (draw.pixel_mode_555 != 0) {
        return true;
    }
    return draw.red_mask == 0x7c00u;
#else
    return false;
#endif
}

u16 PackPaletteRgbToSurfacePixel(u8 red, u8 green, u8 blue) {
    if (surface_uses_565()) {
        return static_cast<u16>(((red >> 3) << 11) + ((green >> 2) << 5) + (blue >> 3));
    }
    return static_cast<u16>(((red >> 3) << 10) + ((green >> 3) << 5) + (blue >> 3));
}

u32 AllocatePaletteCacheSlot() {
    return commit_next_slot();
}

void ReleasePaletteCacheSlotsFrom(u32 first_slot) {
    if (first_slot < g_palette_cache_state.next_slot) {
        g_palette_cache_state.next_slot = first_slot;
    }
}

void ResetPaletteCache() {
    g_palette_cache_state.next_slot = 0;
    g_palette_cache_state.transparent_mask = 0;
}

bool IsPaletteCacheSlotActive(u32 slot_index) {
    return valid_slot(slot_index);
}

u64 GetPaletteCacheSlotAllocationSerial(u32 slot_index) {
    return valid_slot(slot_index) ?
        g_palette_cache_state.allocation_serials[slot_index] : 0;
}

bool PaletteCacheSlotAllocationMatches(u32 slot_index, u64 allocation_serial) {
    return allocation_serial != 0 && valid_slot(slot_index) &&
        g_palette_cache_state.allocation_serials[slot_index] == allocation_serial;
}

bool SetPaletteCacheRawSlot(u32 slot_index, const void* data, std::size_t byte_count) {
    if (!valid_slot(slot_index) || data == nullptr || byte_count != kPaletteRawBytesPerSlot) {
        return false;
    }

    std::memcpy(g_palette_cache_state.raw_slots[slot_index].data(), data,
        kPaletteRawBytesPerSlot);
    return true;
}

void ConvertPaletteCacheSlot(u32 slot_index) {
    if (!valid_slot(slot_index)) {
        return;
    }

    const auto& raw = g_palette_cache_state.raw_slots[slot_index];
    auto& pixels = g_palette_cache_state.pixel_slots[slot_index];
    for (u32 i = 0; i < kPalettePixelCount; ++i) {
        const u32 base = i * 4;
        pixels[i] = PackPaletteRgbToSurfacePixel(raw[base], raw[base + 1], raw[base + 2]);
    }
}

void RefreshPaletteTransparentMask() {
    g_palette_cache_state.transparent_mask = surface_uses_565() ? 0xf7deu : 0x7bdeu;
}

bool ApplyPaletteCacheUnitRamp(u32 target_slot, u8 ramp) {
    if (!valid_slot(target_slot)) {
        return false;
    }

    const u32 source_first = static_cast<u32>(ramp & 0x0f) * kUnitRampSourceStride;
    if (source_first + kUnitRampColorCount > kPalettePixelCount ||
        kUnitRampTargetFirstIndex + kUnitRampColorCount > kPalettePixelCount) {
        return false;
    }

    const auto& source = g_palette_cache_state.pixel_slots[kUnitRampSourceSlot];
    auto& target = g_palette_cache_state.pixel_slots[target_slot];
    for (u32 offset = 0; offset < kUnitRampColorCount; offset += 2) {
        target[kUnitRampTargetFirstIndex + offset] = source[source_first + offset];
        target[kUnitRampTargetFirstIndex + offset + 1] =
            source[source_first + offset + 1];
    }
    return true;
}

u32 LoadPaletteCacheFile(const char* path) {
    if (path == nullptr) {
        return kInvalidPaletteCacheSlot;
    }

    FILE* file = CrtFopen(path, "rb");
    if (file == nullptr) {
        return kInvalidPaletteCacheSlot;
    }

    const u32 slot = AllocatePaletteCacheSlot();
    if (slot == kInvalidPaletteCacheSlot) {
        return kInvalidPaletteCacheSlot;
    }

    auto& raw = g_palette_cache_state.raw_slots[slot];
    CrtFread(raw.data(), kPaletteRawBytesPerSlot, 1, file);
    CrtFclose(file);
    ConvertPaletteCacheSlot(slot);
    return slot;
}

u32 LoadPaletteCacheTrcRecord(const char* archive_name, u32 record_index) {
    if (archive_name == nullptr) {
        return kInvalidPaletteCacheSlot;
    }

    const u32 slot = AllocatePaletteCacheSlot();
    if (slot == kInvalidPaletteCacheSlot) {
        return kInvalidPaletteCacheSlot;
    }

    auto& raw = g_palette_cache_state.raw_slots[slot];
    if (!LoadTrcRecordIntoBuffer(archive_name, record_index, raw.data(), raw.size())) {
        ReleasePaletteCacheSlotsFrom(slot);
        return kInvalidPaletteCacheSlot;
    }

    ConvertPaletteCacheSlot(slot);
    return slot;
}

bool LoadPaletteMappedBackBufferTrcPair(const char* archive_name, u32 record_index) {
#ifdef _WIN32
    const u32 slot = LoadPaletteCacheTrcRecord(archive_name, record_index);
    if (slot == kInvalidPaletteCacheSlot) {
        return false;
    }

    const auto& dd = direct_draw_state();
    const std::size_t pixel_count =
        static_cast<std::size_t>(dd.width) * static_cast<std::size_t>(dd.height);
    if (dd.width == 0 || dd.height == 0 ||
        pixel_count > (std::numeric_limits<std::size_t>::max() / sizeof(u16))) {
        ReleasePaletteCacheSlotsFrom(slot);
        return false;
    }

    std::vector<u8> indexed(pixel_count * sizeof(u16), 0);
    std::size_t bytes_read = 0;
    if (!read_trc_record_bytes(archive_name, record_index + 1, indexed.data(),
            indexed.size(), &bytes_read) ||
        bytes_read < pixel_count) {
        ReleasePaletteCacheSlotsFrom(slot);
        return false;
    }

    SpriteRenderTarget target{};
    if (FAILED(LockBackBufferSpriteRenderTarget(target))) {
        ReleasePaletteCacheSlotsFrom(slot);
        return false;
    }

    const auto& palette = g_palette_cache_state.pixel_slots[slot];
    for (u32 y = 0; y < dd.height; ++y) {
        u16* dest = target.pixels + static_cast<std::size_t>(y) * target.stride_words;
        const u8* src = indexed.data() + static_cast<std::size_t>(y) * dd.width;
        for (u32 x = 0; x < dd.width; ++x) {
            dest[x] = palette[src[x]];
        }
    }

    ReleasePaletteCacheSlotsFrom(slot);
    const HRESULT unlock_result = UnlockBackBufferSpriteRenderTarget();
    if (FAILED(unlock_result)) {
        return false;
    }
    return SUCCEEDED(PresentBackBufferToPrimary());
#else
    (void)archive_name;
    (void)record_index;
    return false;
#endif
}

u32 BuildGrayscalePaletteAtNextSlot(u32 source_slot) {
    const u32 target_slot = next_slot_or_invalid();
    if (!valid_slot(source_slot) || target_slot == kInvalidPaletteCacheSlot) {
        return kInvalidPaletteCacheSlot;
    }

    const auto& raw = g_palette_cache_state.raw_slots[source_slot];
    auto& target = g_palette_cache_state.pixel_slots[target_slot];
    for (u32 i = 0; i < kPalettePixelCount; ++i) {
        const u32 base = i * 4;
        const u32 gray = (static_cast<u32>(raw[base]) + raw[base + 1] + raw[base + 2]) / 3;
        target[i] = PackPaletteRgbToSurfacePixel(static_cast<u8>(gray), static_cast<u8>(gray),
            static_cast<u8>(gray));
    }
    return target_slot;
}

u32 BuildGrayscalePaletteVariant(u32 source_slot) {
    const u32 slot = BuildGrayscalePaletteAtNextSlot(source_slot);
    if (slot != kInvalidPaletteCacheSlot) {
        commit_next_slot();
    }
    return slot;
}

u32 BuildRedAdjustedPaletteAtNextSlot(u32 source_slot, i16 red_delta) {
    const u32 target_slot = next_slot_or_invalid();
    if (!valid_slot(source_slot) || target_slot == kInvalidPaletteCacheSlot) {
        return kInvalidPaletteCacheSlot;
    }

    const auto& raw = g_palette_cache_state.raw_slots[source_slot];
    auto& target = g_palette_cache_state.pixel_slots[target_slot];
    for (u32 i = 0; i < kPalettePixelCount; ++i) {
        const u32 base = i * 4;
        u32 red5 = static_cast<u16>(static_cast<i16>(raw[base] >> 3) + red_delta);
        if (red5 > 0x1f) {
            red5 = 0x1f;
        }

        const u32 green = raw[base + 1];
        const u32 blue = raw[base + 2];
        if (surface_uses_565()) {
            target[i] = static_cast<u16>((red5 << 11) + ((green >> 2) << 5) + (blue >> 3));
        }
        else {
            target[i] = static_cast<u16>((red5 << 10) + ((green >> 3) << 5) + (blue >> 3));
        }
    }
    return target_slot;
}

u32 BuildRedAdjustedPaletteVariant(u32 source_slot, i16 red_delta) {
    const u32 slot = BuildRedAdjustedPaletteAtNextSlot(source_slot, red_delta);
    if (slot != kInvalidPaletteCacheSlot) {
        commit_next_slot();
    }
    return slot;
}

u32 BuildMaskedPaletteAtNextSlot(u32 source_slot) {
    const u32 target_slot = next_slot_or_invalid();
    if (!valid_slot(source_slot) || target_slot == kInvalidPaletteCacheSlot) {
        return kInvalidPaletteCacheSlot;
    }

    auto& target = g_palette_cache_state.pixel_slots[target_slot];
    const auto& source = g_palette_cache_state.pixel_slots[source_slot];
    std::copy(source.begin(), source.end(), target.begin());
    const u16 mask = SurfaceRedMask();
    for (auto& pixel : target) {
        pixel = static_cast<u16>(pixel | mask);
    }
    return target_slot;
}

u32 BuildMaskedPaletteVariant(u32 source_slot) {
    const u32 slot = BuildMaskedPaletteAtNextSlot(source_slot);
    if (slot != kInvalidPaletteCacheSlot) {
        commit_next_slot();
    }
    return slot;
}

const PaletteCacheState& palette_cache_state() {
    return g_palette_cache_state;
}

}
