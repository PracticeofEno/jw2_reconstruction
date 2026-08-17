#include "ranker_resource_store.h"

#include "ranker_trc.h"

#include <algorithm>
#include <cstring>

namespace ranker {
namespace {

ResourceStoreState g_resource_store_state;

u16 read_le_u16(const u8* p) {
    return static_cast<u16>(p[0]) | static_cast<u16>(p[1] << 8);
}

u32 read_le_u32(const u8* p) {
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8) |
        (static_cast<u32>(p[2]) << 16) |
        (static_cast<u32>(p[3]) << 24);
}

bool valid_entry(u32 entry_index) {
    return entry_index < g_resource_store_state.next_entry &&
        entry_index < kResourceStoreCapacity;
}

bool copy_header_resource(const u8* header, const u8* payload, std::size_t payload_size,
    u32* entry_index) {
    if (header == nullptr || (payload == nullptr && payload_size != 0)) {
        return false;
    }

    void* target = nullptr;
    u32 index = kInvalidResourceEntry;
    if (!AllocateResourceEntry(payload_size, &index, &target)) {
        return false;
    }

    auto& entry = g_resource_store_state.entries[index];
    for (std::size_t i = 0; i < entry.metadata.size(); ++i) {
        entry.metadata[i] = read_le_u32(header + i * 4);
    }
    entry.palette_slot = read_le_u32(header + 0x1c);
    if (payload_size != 0) {
        std::memcpy(target, payload, payload_size);
    }
    if (entry_index != nullptr) {
        *entry_index = index;
    }
    return true;
}

u32 load_header_resource_from_trc_reader(TrcRecordReader& reader) {
    std::array<u8, 0x20> header{};
    if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size())) {
        return kInvalidResourceEntry;
    }

    const u32 payload_size = read_le_u32(header.data() + 0x18);
    void* target = nullptr;
    u32 index = kInvalidResourceEntry;
    if (!AllocateResourceEntry(payload_size, &index, &target)) {
        return kInvalidResourceEntry;
    }

    if (!ReadOpenTrcRecordBytes(reader, target, payload_size)) {
        ReleaseResourceEntriesFrom(index);
        return kInvalidResourceEntry;
    }

    auto& entry = g_resource_store_state.entries[index];
    for (std::size_t i = 0; i < entry.metadata.size(); ++i) {
        entry.metadata[i] = read_le_u32(header.data() + i * 4);
    }
    entry.palette_slot = read_le_u32(header.data() + 0x1c);
    return index;
}

u32 load_image_resource_from_trc_reader(TrcRecordReader& reader) {
    std::array<u8, 0x0c> header{};
    if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size())) {
        return kInvalidResourceEntry;
    }

    const u16 width = read_le_u16(header.data() + 0);
    const u16 height = read_le_u16(header.data() + 2);
    const std::size_t payload_size = static_cast<std::size_t>(width) * height;
    void* target = nullptr;
    u32 index = kInvalidResourceEntry;
    if (!AllocateResourceEntry(payload_size, &index, &target)) {
        return kInvalidResourceEntry;
    }

    if (!ReadOpenTrcRecordBytes(reader, target, payload_size)) {
        ReleaseResourceEntriesFrom(index);
        return kInvalidResourceEntry;
    }

    auto& entry = g_resource_store_state.entries[index];
    for (std::size_t i = 0; i < entry.metadata.size(); ++i) {
        entry.metadata[i] = read_le_u16(header.data() + i * 2);
    }
    return index;
}

#ifdef _WIN32
bool read_exact_file(HANDLE file, void* out, DWORD byte_count) {
    if (byte_count == 0) {
        return true;
    }
    DWORD bytes_read = 0;
    return ReadFile(file, out, byte_count, &bytes_read, nullptr) != FALSE &&
        bytes_read == byte_count;
}
#endif

} // namespace

void ResetResourceStore() {
    for (auto& entry : g_resource_store_state.entries) {
        entry.payload.clear();
        for (std::size_t i = 0; i < 4; ++i) {
            entry.metadata[i] = 0;
        }
    }
    g_resource_store_state.next_entry = 0;
}

bool AllocateResourceEntry(std::size_t byte_count, u32* entry_index, void** payload_out) {
    if (g_resource_store_state.next_entry >= kResourceStoreCapacity) {
        return false;
    }

    const u32 index = g_resource_store_state.next_entry;
    auto& entry = g_resource_store_state.entries[index];
    entry.payload.assign(byte_count, 0);
    if (byte_count != 0 && entry.payload.empty()) {
        return false;
    }
    entry.allocation_serial = g_resource_store_state.next_allocation_serial++;
    if (g_resource_store_state.next_allocation_serial == 0) {
        ++g_resource_store_state.next_allocation_serial;
    }

    if (entry_index != nullptr) {
        *entry_index = index;
    }
    if (payload_out != nullptr) {
        *payload_out = entry.payload.data();
    }
    ++g_resource_store_state.next_entry;
    return true;
}

void ReleaseResourceEntriesFrom(u32 first_entry) {
    while (first_entry < g_resource_store_state.next_entry) {
        --g_resource_store_state.next_entry;
        auto& entry = g_resource_store_state.entries[g_resource_store_state.next_entry];
        entry.payload.clear();
        entry.payload.shrink_to_fit();
    }
}

bool ConfigureResourceEntry(
    u32 entry_index, const std::array<u32, 6>& metadata, u32 palette_slot) {
    if (!valid_entry(entry_index)) {
        return false;
    }

    auto& entry = g_resource_store_state.entries[entry_index];
    entry.metadata = metadata;
    entry.palette_slot = palette_slot;
    return true;
}

bool ReplaceResourceEntryPayload(u32 entry_index,
    const std::array<u32, 6>& metadata, const void* payload,
    std::size_t payload_size) {
    if (!valid_entry(entry_index) || (payload == nullptr && payload_size != 0)) {
        return false;
    }

    std::vector<u8> replacement(payload_size);
    if (payload_size != 0) {
        std::memcpy(replacement.data(), payload, payload_size);
    }

    ResourceStoreEntry& entry = g_resource_store_state.entries[entry_index];
    entry.metadata = metadata;
    entry.payload.swap(replacement);
    return true;
}

#ifdef _WIN32
u32 LoadResourceHandle(HANDLE file) {
    if (file == INVALID_HANDLE_VALUE) {
        return kInvalidResourceEntry;
    }

    std::array<u8, 0x20> header{};
    if (!read_exact_file(file, header.data(), static_cast<DWORD>(header.size()))) {
        return kInvalidResourceEntry;
    }

    const u32 payload_size = read_le_u32(header.data() + 0x18);
    void* target = nullptr;
    u32 index = kInvalidResourceEntry;
    if (!AllocateResourceEntry(payload_size, &index, &target)) {
        return kInvalidResourceEntry;
    }

    if (!read_exact_file(file, target, payload_size)) {
        ReleaseResourceEntriesFrom(index);
        return kInvalidResourceEntry;
    }

    auto& entry = g_resource_store_state.entries[index];
    for (std::size_t i = 0; i < entry.metadata.size(); ++i) {
        entry.metadata[i] = read_le_u32(header.data() + i * 4);
    }
    entry.palette_slot = read_le_u32(header.data() + 0x1c);
    return index;
}

u32 LoadResourceFile(const char* path) {
    if (path == nullptr) {
        return kInvalidResourceEntry;
    }

    HANDLE file = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return kInvalidResourceEntry;
    }

    const u32 index = LoadResourceHandle(file);
    CloseHandle(file);
    return index;
}
#endif

u32 LoadResourceMemory(const void* data, std::size_t byte_count, std::size_t* consumed_bytes) {
    if (consumed_bytes != nullptr) {
        *consumed_bytes = 0;
    }
    if (data == nullptr || byte_count < 0x20) {
        return kInvalidResourceEntry;
    }

    const auto* bytes = static_cast<const u8*>(data);
    const u32 payload_size = read_le_u32(bytes + 0x18);
    const std::size_t total_size = 0x20u + static_cast<std::size_t>(payload_size);
    if (byte_count < total_size) {
        return kInvalidResourceEntry;
    }

    u32 index = kInvalidResourceEntry;
    if (!copy_header_resource(bytes, bytes + 0x20, payload_size, &index)) {
        return kInvalidResourceEntry;
    }
    if (consumed_bytes != nullptr) {
        *consumed_bytes = total_size;
    }
    return index;
}

u32 LoadResourceTrcRecord(const char* archive_name, u32 record_index) {
    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return kInvalidResourceEntry;
    }

    const u32 index = load_header_resource_from_trc_reader(reader);
    CloseTrcRecordReader(reader);
    return index;
}

u32 LoadImageResourceTrcRecord(const char* archive_name, u32 record_index) {
    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return kInvalidResourceEntry;
    }

    const u32 index = load_image_resource_from_trc_reader(reader);
    CloseTrcRecordReader(reader);
    return index;
}

const ResourceStoreEntry* GetResourceEntry(u32 entry_index) {
    if (!valid_entry(entry_index)) {
        return nullptr;
    }
    return &g_resource_store_state.entries[entry_index];
}

const ResourceStoreState& resource_store_state() {
    return g_resource_store_state;
}

u64 GetResourceEntryAllocationSerial(u32 entry_index) {
    const ResourceStoreEntry* entry = GetResourceEntry(entry_index);
    return entry != nullptr ? entry->allocation_serial : 0;
}

bool ResourceEntryRangeAllocationMatches(
    u32 first_entry, u32 end_entry, u64 tail_allocation_serial) {
    return tail_allocation_serial != 0 &&
        first_entry != kInvalidResourceEntry &&
        end_entry > first_entry &&
        end_entry <= g_resource_store_state.next_entry &&
        g_resource_store_state.entries[end_entry - 1u].allocation_serial ==
            tail_allocation_serial;
}

bool SetResourceEntryPaletteSlot(u32 entry_index, u32 palette_slot) {
    if (!valid_entry(entry_index)) {
        return false;
    }

    g_resource_store_state.entries[entry_index].palette_slot = palette_slot;
    return true;
}

}
