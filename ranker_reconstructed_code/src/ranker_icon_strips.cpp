#include "ranker_icon_strips.h"

#include "ranker_palette_cache.h"
#include "ranker_runtime_resources.h"
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

const u8* palette_bytes_for_slot(u32 slot) {
    if (!IsPaletteCacheSlotActive(slot)) {
        return nullptr;
    }
    return palette_cache_state().raw_slots[slot].data();
}

u32 acquire_strip_palette(CommandPaletteKind kind, u32 record_index,
    bool& temporary) {
    temporary = false;
    const CommandThemeResourceState& command = command_theme_resource_state();
    const PaletteSlotRef& theme_palette =
        command.palettes[static_cast<std::size_t>(kind)];
    if (command.loaded && PaletteCacheSlotAllocationMatches(
            theme_palette.slot, theme_palette.allocation_serial)) {
        return theme_palette.slot;
    }

    temporary = true;
    return LoadPaletteCacheTrcRecord(kIconArchive, record_index);
}

void release_temporary_palette(u32 slot, bool temporary) {
    if (temporary && IsPaletteCacheSlotActive(slot)) {
        ReleasePaletteCacheSlotsFrom(slot);
    }
}

bool load_primary_strip(RawIndexedBitmapStrip& strip, std::vector<u8>& frames) {
    bool temporary = false;
    const u32 slot = acquire_strip_palette(
        CommandPaletteKind::SmallCharacter, kPrimaryPaletteRecord, temporary);
    const u8* palette = palette_bytes_for_slot(slot);
    const bool loaded = palette != nullptr ?
        LoadRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight, palette, kPaletteRawBytesPerSlot) :
        LoadRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight);
    release_temporary_palette(slot, temporary);
    if (loaded) {
        strip.frame_count = static_cast<u32>(
            frames.size() / (static_cast<std::size_t>(kIconFrameWidth) *
                static_cast<std::size_t>(-kIconFrameHeight)));
    }
    return loaded;
}

bool load_secondary_strip(RawIndexedBitmapStrip& strip, std::vector<u8>& frames) {
    bool temporary = false;
    const u32 slot = acquire_strip_palette(
        CommandPaletteKind::Item, kSecondaryPaletteRecord, temporary);
    const u8* palette = palette_bytes_for_slot(slot);
    const bool loaded = palette != nullptr ?
        LoadSecondaryRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight, palette, kPaletteRawBytesPerSlot) :
        LoadSecondaryRawIndexedBitmapStrip(strip, frames.data(), kIconFrameWidth,
            kIconFrameHeight);
    release_temporary_palette(slot, temporary);
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
