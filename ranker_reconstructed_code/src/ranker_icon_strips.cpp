#include "ranker_icon_strips.h"

#include "ranker_palette_cache.h"
#include "ranker_trc.h"

#include <vector>

namespace ranker {
namespace {

constexpr char kIconArchive[] = "JW2_02.TRC";
constexpr u32 kAvatarIconRecord = 1;
constexpr u32 kItemIconRecord = 0x0c;
constexpr u32 kPrimaryPaletteRecord = 0;
constexpr u32 kSecondaryPaletteRecord = 0x0b;
constexpr i32 kIconFrameWidth = 0x26;
constexpr i32 kIconFrameHeight = -0x26;

std::vector<u8> g_avatar_icon_frames;
std::vector<u8> g_item_icon_frames;
u32 g_primary_palette_slot = kInvalidPaletteCacheSlot;
u32 g_secondary_palette_slot = kInvalidPaletteCacheSlot;

bool load_record_once(std::vector<u8>& storage, u32 record_index) {
    if (!storage.empty()) {
        return true;
    }

    const u32 byte_count = QueryTrcRecordOriginalSize(kIconArchive, record_index);
    if (byte_count == 0) {
        return false;
    }

    storage.assign(byte_count, 0);
    if (!LoadTrcRecordIntoBuffer(kIconArchive, record_index, storage.data(),
            storage.size())) {
        storage.clear();
        storage.shrink_to_fit();
        return false;
    }
    return true;
}

u32 load_palette_once(u32& slot, u32 record_index) {
    if (slot == kInvalidPaletteCacheSlot) {
        slot = LoadPaletteCacheTrcRecord(kIconArchive, record_index);
    }
    return slot;
}

const u8* palette_bytes_for_slot(u32 slot) {
    if (slot >= kPaletteCacheSlotCount) {
        return nullptr;
    }
    return palette_cache_state().raw_slots[slot].data();
}

bool load_primary_strip(RawIndexedBitmapStrip& strip, std::vector<u8>& frames) {
    const u32 slot = load_palette_once(g_primary_palette_slot, kPrimaryPaletteRecord);
    const u8* palette = palette_bytes_for_slot(slot);
    const bool loaded = palette != nullptr ?
        LoadRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight, palette, kPaletteRawBytesPerSlot) :
        LoadRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight);
    if (loaded) {
        strip.frame_count = static_cast<u32>(
            frames.size() / (static_cast<std::size_t>(kIconFrameWidth) *
                static_cast<std::size_t>(-kIconFrameHeight)));
    }
    return loaded;
}

bool load_secondary_strip(RawIndexedBitmapStrip& strip, std::vector<u8>& frames) {
    const u32 slot = load_palette_once(g_secondary_palette_slot, kSecondaryPaletteRecord);
    const u8* palette = palette_bytes_for_slot(slot);
    const bool loaded = palette != nullptr ?
        LoadSecondaryRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight, palette, kPaletteRawBytesPerSlot) :
        LoadSecondaryRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight);
    if (loaded) {
        strip.frame_count = static_cast<u32>(
            frames.size() / (static_cast<std::size_t>(kIconFrameWidth) *
                static_cast<std::size_t>(-kIconFrameHeight)));
    }
    return loaded;
}

} // namespace

bool LoadAvatarIconStrip(RawIndexedBitmapStrip& strip) {
    if (!load_record_once(g_avatar_icon_frames, kAvatarIconRecord)) {
        return false;
    }
    return load_primary_strip(strip, g_avatar_icon_frames);
}

bool LoadItemIconStrip(RawIndexedBitmapStrip& strip) {
    if (!load_record_once(g_item_icon_frames, kItemIconRecord)) {
        return false;
    }
    return load_secondary_strip(strip, g_item_icon_frames);
}

}
