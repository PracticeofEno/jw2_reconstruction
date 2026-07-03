#pragma once

#include "ranker_bitmap_resource.h"

#include <array>
#include <cstddef>
#include <ctime>

namespace ranker {

constexpr u32 kBitmapIconSlotCount = 0xb4;
constexpr u32 kBitmapIconDefaultSlot = 0x0d;
constexpr u32 kGuildBitmapIconSlotBase = 0x1e;
constexpr u32 kGuildBitmapIconSlotCount = 0x96;

struct BitmapIconResourceCollection {
    u32 loaded_count = 0;
    std::array<bool, kBitmapIconSlotCount> loaded{};
    std::array<BitmapMemoryResource, kBitmapIconSlotCount> slots{};
    u32 guild_loaded_count = 0;
    std::array<std::time_t, kGuildBitmapIconSlotCount> guild_timestamps{};
};

BitmapIconResourceCollection& GlobalBitmapIconResourceCollection();
void InitializeGlobalBitmapIconCollectionStatic();
void InitializeGlobalBitmapIconCollection();
void RegisterGlobalBitmapIconCollectionDestructor();
void DestroyGlobalBitmapIconCollection();
void InitializeBitmapIconResourceCollection(BitmapIconResourceCollection& icons);
void HandleBitmapIconResourceCollectionDestructor(BitmapIconResourceCollection& icons);
void ReleaseAllBitmapIconSlots(BitmapIconResourceCollection& icons);

bool LoadBitmapIconSlotFromJw219Trc(BitmapIconResourceCollection& icons, u32 slot,
    u32 record_index);
bool LoadBitmapIconSlotFromFile(BitmapIconResourceCollection& icons, u32 slot,
    const char* path);
void ReleaseBitmapIconSlot(BitmapIconResourceCollection& icons, u32 slot);

void LoadDefaultIconBitmapSet(BitmapIconResourceCollection& icons);
void WriteGuildIconBitmapFile(u32 guild_index, const void* data, std::size_t byte_count,
    std::time_t modified_time);
bool LoadGuildIconBitmapSlot(BitmapIconResourceCollection& icons, u32 guild_index);
void ReleaseGuildIconBitmapSlot(BitmapIconResourceCollection& icons, u32 guild_index);

BitmapMemoryResource& GetBitmapIconSlotOrDefault(BitmapIconResourceCollection& icons,
    u32 slot);
const BitmapMemoryResource& GetBitmapIconSlotOrDefault(
    const BitmapIconResourceCollection& icons, u32 slot);

}
