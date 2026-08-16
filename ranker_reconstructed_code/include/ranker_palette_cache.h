#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>

namespace ranker {

constexpr u32 kPaletteCacheSlotCount = 0x200;
constexpr u32 kPaletteRawBytesPerSlot = 0x400;
constexpr u32 kPalettePixelCount = 0x100;
constexpr u32 kInvalidPaletteCacheSlot = 0xffffffffu;

struct PaletteCacheState {
    std::array<std::array<u8, kPaletteRawBytesPerSlot>, kPaletteCacheSlotCount> raw_slots{};
    std::array<std::array<u16, kPalettePixelCount>, kPaletteCacheSlotCount> pixel_slots{};
    // Slots are stack-reused after ReleasePaletteCacheSlotsFrom.  Keep a
    // monotonic identity so persistent UI references can distinguish the
    // palette they loaded from an unrelated later allocation at the same
    // numeric slot.
    std::array<u64, kPaletteCacheSlotCount> allocation_serials{};
    u32 next_slot = 0;
    u64 next_allocation_serial = 1;
    u16 transparent_mask = 0;
};

u32 AllocatePaletteCacheSlot();
void ReleasePaletteCacheSlotsFrom(u32 first_slot);
void ResetPaletteCache();
bool IsPaletteCacheSlotActive(u32 slot_index);
u64 GetPaletteCacheSlotAllocationSerial(u32 slot_index);
bool PaletteCacheSlotAllocationMatches(u32 slot_index, u64 allocation_serial);

bool SetPaletteCacheRawSlot(u32 slot_index, const void* data, std::size_t byte_count);
void ConvertPaletteCacheSlot(u32 slot_index);
void RefreshPaletteTransparentMask();
bool ApplyPaletteCacheUnitRamp(u32 target_slot, u8 ramp);

u32 LoadPaletteCacheFile(const char* path);
u32 LoadPaletteCacheTrcRecord(const char* archive_name, u32 record_index);
bool LoadPaletteMappedBackBufferTrcPair(const char* archive_name, u32 record_index);
u32 BuildGrayscalePaletteAtNextSlot(u32 source_slot);
u32 BuildGrayscalePaletteVariant(u32 source_slot);
u32 BuildRedAdjustedPaletteAtNextSlot(u32 source_slot, i16 red_delta);
u32 BuildRedAdjustedPaletteVariant(u32 source_slot, i16 red_delta);
u32 BuildMaskedPaletteAtNextSlot(u32 source_slot);
u32 BuildMaskedPaletteVariant(u32 source_slot);

u16 PackPaletteRgbToSurfacePixel(u8 red, u8 green, u8 blue);
bool SurfacePixelMode555();
u16 SurfaceRedMask();

const PaletteCacheState& palette_cache_state();

}
