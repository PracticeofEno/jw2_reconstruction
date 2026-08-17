#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <cstddef>
#include <vector>

namespace ranker {

constexpr u32 kResourceStoreCapacity = 23000;
constexpr u32 kInvalidResourceEntry = 0xffffffffu;

struct ResourceStoreEntry {
    std::array<u32, 6> metadata{};
    u32 palette_slot = 0;
    std::vector<u8> payload;
    // Resource indices are stack-reused after ReleaseResourceEntriesFrom.
    // Preserve a monotonic identity so long-lived catalogs can distinguish a
    // still-live image from an unrelated later allocation at the same index.
    u64 allocation_serial = 0;
};

struct ResourceStoreState {
    std::array<ResourceStoreEntry, kResourceStoreCapacity> entries{};
    u32 next_entry = 0;
    u64 next_allocation_serial = 1;
};

void ResetResourceStore();
bool AllocateResourceEntry(std::size_t byte_count, u32* entry_index, void** payload_out);
void ReleaseResourceEntriesFrom(u32 first_entry);
bool ConfigureResourceEntry(
    u32 entry_index, const std::array<u32, 6>& metadata, u32 palette_slot);
// Replaces the serialized 0x20 resource body while retaining the entry's
// stable index, palette binding, and allocation identity.  The original
// Change Death path updates animation resources in place for exactly this
// reason: live render state already refers to their resource indices.
bool ReplaceResourceEntryPayload(u32 entry_index,
    const std::array<u32, 6>& metadata, const void* payload,
    std::size_t payload_size);

#ifdef _WIN32
u32 LoadResourceHandle(HANDLE file);
u32 LoadResourceFile(const char* path);
#endif

u32 LoadResourceMemory(const void* data, std::size_t byte_count,
    std::size_t* consumed_bytes = nullptr);
u32 LoadResourceTrcRecord(const char* archive_name, u32 record_index);
u32 LoadImageResourceTrcRecord(const char* archive_name, u32 record_index);

const ResourceStoreEntry* GetResourceEntry(u32 entry_index);
const ResourceStoreState& resource_store_state();
u64 GetResourceEntryAllocationSerial(u32 entry_index);
bool ResourceEntryRangeAllocationMatches(
    u32 first_entry, u32 end_entry, u64 tail_allocation_serial);
bool SetResourceEntryPaletteSlot(u32 entry_index, u32 palette_slot);

}
