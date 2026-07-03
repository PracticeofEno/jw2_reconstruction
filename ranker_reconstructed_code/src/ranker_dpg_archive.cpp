#include "ranker_dpg_archive.h"
#include "ranker_win32_compat.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace ranker {
namespace {

constexpr std::size_t kDpgHeaderNameOffset = 0x4;
constexpr std::size_t kDpgHeaderNameBytes = 0x103;
constexpr std::size_t kDpgHeaderMetadataOffset = 0x108;
constexpr std::size_t kDpgHeaderDataOffsetLowOffset = 0x10c;
constexpr std::size_t kDpgHeaderPayloadSizeOffset = 0x114;
constexpr std::size_t kDpgHeaderFileTimeOffset = 0x124;

u32 read_le_u32(const u8* p) {
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8) |
        (static_cast<u32>(p[2]) << 16) |
        (static_cast<u32>(p[3]) << 24);
}

void write_le_u32(u8* p, u32 value) {
    p[0] = static_cast<u8>(value & 0xffu);
    p[1] = static_cast<u8>((value >> 8) & 0xffu);
    p[2] = static_cast<u8>((value >> 16) & 0xffu);
    p[3] = static_cast<u8>((value >> 24) & 0xffu);
}

u32 rotate_left_one(u32 value) {
    return (value << 1) | (value >> 31);
}

void serialize_header(const DpgRecordHeader& header, std::array<u8, kDpgRecordHeaderBytes>& out) {
    out.fill(0);
    write_le_u32(out.data(), static_cast<u32>(header.kind));
    std::memcpy(out.data() + sizeof(u32), header.payload.data(), header.payload.size());
}

void deserialize_header(const std::array<u8, kDpgRecordHeaderBytes>& bytes,
    DpgRecordHeader& header) {
    header.kind = static_cast<DpgRecordKind>(read_le_u32(bytes.data()));
    std::memcpy(header.payload.data(), bytes.data() + sizeof(u32), header.payload.size());
}

u32 header_payload_u32(const DpgRecordHeader& header, std::size_t payload_offset) {
    if (payload_offset + sizeof(u32) > header.payload.size()) {
        return 0;
    }
    return read_le_u32(header.payload.data() + payload_offset);
}

void set_header_payload_u32(DpgRecordHeader& header, std::size_t payload_offset, u32 value) {
    if (payload_offset + sizeof(u32) <= header.payload.size()) {
        write_le_u32(header.payload.data() + payload_offset, value);
    }
}

std::string header_name(const DpgRecordHeader& header) {
    const char* text = reinterpret_cast<const char*>(
        header.payload.data() + kDpgHeaderNameOffset);
    const std::size_t capacity = std::min<std::size_t>(kDpgHeaderNameBytes,
        header.payload.size() - kDpgHeaderNameOffset);
    return std::string(text, strnlen(text, capacity));
}

void set_header_name(DpgRecordHeader& header, const char* name) {
    const std::size_t capacity = std::min<std::size_t>(kDpgHeaderNameBytes,
        header.payload.size() - kDpgHeaderNameOffset);
    std::memset(header.payload.data() + kDpgHeaderNameOffset, 0, capacity);
    if (name == nullptr || capacity == 0) {
        return;
    }
    const std::size_t copy_size = std::min<std::size_t>(std::strlen(name), capacity - 1);
    std::memcpy(header.payload.data() + kDpgHeaderNameOffset, name, copy_size);
}

std::size_t record_payload_size(const DpgRecordHeader& header) {
    return header_payload_u32(header, kDpgHeaderPayloadSizeOffset);
}

void set_record_payload_size(DpgRecordHeader& header, u32 byte_count) {
    set_header_payload_u32(header, kDpgHeaderPayloadSizeOffset, byte_count);
}

void set_record_timestamp_now(DpgRecordHeader& header) {
    set_header_payload_u32(header, kDpgHeaderFileTimeOffset, 1);
    set_header_payload_u32(header, kDpgHeaderFileTimeOffset + sizeof(u32), 0);
}

u32 record_metadata(const DpgRecordHeader& header) {
    return header_payload_u32(header, kDpgHeaderMetadataOffset);
}

bool record_payload_cipher_enabled(const DpgRecordHeader& header) {
    return (header_payload_u32(header, 0) & 2u) != 0;
}

bool is_dpg_payload_record(DpgRecordKind kind) {
    return kind == DpgRecordKind::Payload;
}

bool active_dpg_record(const DpgArchiveContext& archive) {
    return archive.active_record_index < archive.directory.size() &&
        is_dpg_payload_record(archive.directory[archive.active_record_index].header.kind);
}

u32 find_free_dpg_record_index(const DpgArchiveContext& archive) {
    for (std::size_t i = 0; i < archive.directory.size(); ++i) {
        if (archive.directory[i].header.kind == DpgRecordKind::Free) {
            return static_cast<u32>(i);
        }
    }
    return 0xffffffffu;
}

bool create_empty_dpg_record(DpgArchiveContext& archive, const char* path) {
    u32 record_index = find_free_dpg_record_index(archive);
    if (record_index == 0xffffffffu) {
        AppendDpgArchivePaddingRecords(archive, std::max<u32>(archive.directory_capacity, 1));
        record_index = find_free_dpg_record_index(archive);
    }
    if (record_index == 0xffffffffu || record_index >= archive.directory.size()) {
        return false;
    }

    DpgDirectoryEntry& entry = archive.directory[record_index];
    entry.header.kind = DpgRecordKind::Payload;
    set_header_name(entry.header, path);
    set_record_payload_size(entry.header, 0);
    set_record_timestamp_now(entry.header);
    archive.active_record_index = record_index;
    archive.active_record_offset = entry.data_offset;
    archive.active_record_size = 0;
    archive.cursor = entry.data_offset;
    return RewriteDpgArchiveDirectoryRecord(archive, record_index);
}

bool load_dpg_file_storage(DpgArchiveContext& archive, const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    archive.storage.assign(std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    return !file.bad();
}

bool flush_dpg_file_storage(DpgArchiveContext& archive) {
    if (archive.backing_kind != DpgBackingKind::File || !archive.dirty ||
        archive.archive_name.empty()) {
        return true;
    }

    std::ofstream file(archive.archive_name, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    if (!archive.storage.empty()) {
        file.write(reinterpret_cast<const char*>(archive.storage.data()),
            static_cast<std::streamsize>(archive.storage.size()));
    }
    return static_cast<bool>(file);
}

} // namespace

void InitializeDpgArchiveContext(DpgArchiveContext& archive) {
    archive.header_seed = 0;
    archive.cipher_seed = 0;
    archive.directory_offset = 0;
    ResetDpgArchiveContext(archive);
}

void ResetDpgArchiveContext(DpgArchiveContext& archive) {
    archive.storage.clear();
    archive.directory.clear();
    archive.cursor = 0;
    archive.directory_count = 0;
    archive.directory_capacity = 0;
    archive.open = false;
    archive.dirty = false;
    archive.cipher_enabled = false;
    archive.active_record_index = 0xffffffffu;
    archive.active_record_offset = 0;
    archive.active_record_size = 0;
}

void CloseDpgArchiveWrapper(DpgArchiveContext& archive) {
    CloseDpgArchive(archive);
}

std::size_t DpgArchiveReadBytes(DpgArchiveContext& archive, void* out, std::size_t byte_count) {
    if (out == nullptr || byte_count == 0 || archive.cursor >= archive.storage.size()) {
        return 0;
    }
    const std::size_t readable = std::min(byte_count, archive.storage.size() - archive.cursor);
    std::memcpy(out, archive.storage.data() + archive.cursor, readable);
    archive.cursor += readable;
    return readable;
}

std::size_t DpgArchiveWriteBytes(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count) {
    if (data == nullptr || byte_count == 0) {
        return 0;
    }
    if (archive.cursor > archive.storage.size()) {
        archive.storage.resize(archive.cursor, 0);
    }
    const std::size_t end = archive.cursor + byte_count;
    if (end > archive.storage.size()) {
        archive.storage.resize(end, 0);
    }
    std::memcpy(archive.storage.data() + archive.cursor, data, byte_count);
    archive.cursor = end;
    archive.open = true;
    if (archive.backing_kind == DpgBackingKind::File) {
        archive.dirty = true;
    }
    return byte_count;
}

bool DpgArchiveSeek(DpgArchiveContext& archive, i32 offset, u32 origin) {
    i64 base = 0;
    if (origin == 1) {
        base = static_cast<i64>(archive.cursor);
    }
    else if (origin == 2) {
        base = static_cast<i64>(archive.storage.size());
    }
    else if (origin != 0) {
        return false;
    }

    const i64 target = base + offset;
    if (target < 0) {
        return false;
    }
    archive.cursor = static_cast<std::size_t>(target);
    return true;
}

u32 DpgArchiveTell(DpgArchiveContext& archive) {
    return static_cast<u32>(archive.cursor);
}

void CloseDpgArchiveBackingStore(DpgArchiveContext& archive) {
    if (flush_dpg_file_storage(archive)) {
        archive.dirty = false;
    }
    archive.open = false;
}

void DecodeDpgRecordHeader(DpgArchiveContext& archive, DpgRecordHeader& header) {
    std::array<u8, kDpgRecordHeaderBytes> bytes{};
    serialize_header(header, bytes);
    u32 seed = archive.cipher_seed;
    for (u32 i = 0; i < kDpgHeaderCipherDwordCount; ++i) {
        const std::size_t offset = 8 + static_cast<std::size_t>(i) * sizeof(u32);
        u32 value = read_le_u32(bytes.data() + offset);
        value ^= seed ^ kDpgHeaderCipherConstant;
        write_le_u32(bytes.data() + offset, value);
        seed = rotate_left_one(seed);
    }
    deserialize_header(bytes, header);
}

void EncodeDpgRecordHeader(DpgArchiveContext& archive, DpgRecordHeader& header) {
    DecodeDpgRecordHeader(archive, header);
}

void DecodeDpgPayloadBytes(const DpgArchiveContext& archive, void* data,
    std::size_t byte_count) {
    const u8 key = static_cast<u8>(archive.cipher_seed) ^
        static_cast<u8>(archive.cipher_seed >> 8) ^
        static_cast<u8>(archive.cipher_seed >> 16) ^
        static_cast<u8>(archive.cipher_seed >> 24);
    u8* bytes = static_cast<u8*>(data);
    for (std::size_t i = 0; i < byte_count; ++i) {
        bytes[i] ^= key;
    }
}

void EncodeDpgPayloadBytes(const DpgArchiveContext& archive, void* data,
    std::size_t byte_count) {
    DecodeDpgPayloadBytes(archive, data, byte_count);
}

void ConfigureDpgArchiveCipher(DpgArchiveContext& archive, u32 header_seed,
    u32 cipher_seed, u32 directory_offset) {
    archive.header_seed = header_seed;
    archive.cipher_seed = cipher_seed;
    archive.directory_offset = directory_offset;
    archive.cipher_enabled = header_seed != 0;
}

bool ReadDpgRecordHeader(DpgArchiveContext& archive, DpgRecordHeader& header) {
    std::array<u8, kDpgRecordHeaderBytes> bytes{};
    if (DpgArchiveReadBytes(archive, bytes.data(), bytes.size()) != bytes.size()) {
        return false;
    }
    deserialize_header(bytes, header);
    if (archive.cipher_enabled) {
        DecodeDpgRecordHeader(archive, header);
    }
    return true;
}

bool WriteDpgRecordHeader(DpgArchiveContext& archive, const DpgRecordHeader& header) {
    DpgRecordHeader temp = header;
    if (archive.cipher_enabled) {
        EncodeDpgRecordHeader(archive, temp);
    }
    std::array<u8, kDpgRecordHeaderBytes> bytes{};
    serialize_header(temp, bytes);
    return DpgArchiveWriteBytes(archive, bytes.data(), bytes.size()) == bytes.size();
}

void ScanDpgArchiveDirectory(DpgArchiveContext& archive) {
    const std::size_t saved_cursor = archive.cursor;
    DpgArchiveSeek(archive, static_cast<i32>(archive.directory_offset), 0);
    archive.directory.clear();

    while (true) {
        const u32 header_offset = DpgArchiveTell(archive);
        DpgRecordHeader header{};
        if (!ReadDpgRecordHeader(archive, header)) {
            break;
        }

        if (header.kind == DpgRecordKind::Directory) {
            const u32 capacity = header_payload_u32(header, kDpgHeaderMetadataOffset);
            if (capacity != archive.directory_capacity) {
                archive.directory_capacity = capacity;
                archive.directory.reserve(capacity);
            }
        }

        DpgDirectoryEntry entry{};
        entry.header_offset = header_offset;
        entry.data_offset = DpgArchiveTell(archive);
        entry.header = header;
        archive.directory.push_back(entry);
        if (header.kind == DpgRecordKind::End ||
            archive.directory.size() > archive.directory_capacity + 1) {
            break;
        }

        if (header.kind == DpgRecordKind::Redirect) {
            const u32 redirect_offset = header_payload_u32(header,
                kDpgHeaderMetadataOffset);
            DpgArchiveSeek(archive, static_cast<i32>(archive.directory_offset + redirect_offset),
                0);
        }
    }

    archive.directory_count = static_cast<u32>(archive.directory.size());
    archive.cursor = saved_cursor;
}

void EnsureDpgArchiveTerminatorRecord(DpgArchiveContext& archive) {
    const auto found = std::find_if(archive.directory.begin(), archive.directory.end(),
        [](const DpgDirectoryEntry& entry) {
            return entry.header.kind == DpgRecordKind::Free;
        });
    if (found == archive.directory.end()) {
        AppendDpgArchivePaddingRecords(archive, archive.directory_capacity);
    }
}

void AppendDpgArchivePaddingRecords(DpgArchiveContext& archive, u32 count) {
    if (!archive.directory.empty() &&
        archive.directory.back().header.kind == DpgRecordKind::End) {
        DpgArchiveSeek(archive, static_cast<i32>(archive.directory.back().header_offset), 0);
        archive.directory.pop_back();
    } else {
        DpgArchiveSeek(archive, 0, 2);
    }
    for (u32 i = 0; i < count; ++i) {
        DpgRecordHeader header{};
        header.kind = DpgRecordKind::Free;
        const u32 offset = DpgArchiveTell(archive);
        if (!WriteDpgRecordHeader(archive, header)) {
            return;
        }
        archive.directory.push_back(DpgDirectoryEntry{ offset, DpgArchiveTell(archive), header });
    }

    DpgRecordHeader end_header{};
    end_header.kind = DpgRecordKind::End;
    const u32 end_offset = DpgArchiveTell(archive);
    if (WriteDpgRecordHeader(archive, end_header)) {
        archive.directory.push_back(
            DpgDirectoryEntry{ end_offset, DpgArchiveTell(archive), end_header });
    }
    archive.directory_count = static_cast<u32>(archive.directory.size());
    archive.directory_capacity = std::max(archive.directory_capacity, archive.directory_count);
    if (!archive.directory.empty()) {
        set_header_payload_u32(archive.directory.front().header, kDpgHeaderMetadataOffset,
            archive.directory_count);
        RewriteDpgArchiveDirectoryRecord(archive, 0);
    }
}

bool RewriteDpgArchiveDirectoryRecord(DpgArchiveContext& archive, u32 index) {
    if (index >= archive.directory.size()) {
        return false;
    }
    const std::size_t saved_cursor = archive.cursor;
    DpgArchiveSeek(archive, static_cast<i32>(archive.directory[index].header_offset), 0);
    const bool ok = WriteDpgRecordHeader(archive, archive.directory[index].header);
    archive.cursor = saved_cursor;
    return ok;
}

std::size_t DpgArchiveReadBackingBytes(DpgArchiveContext& archive, void* out,
    std::size_t byte_count) {
    return DpgArchiveReadBytes(archive, out, byte_count);
}

std::size_t DpgArchiveWriteBackingBytes(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count) {
    return DpgArchiveWriteBytes(archive, data, byte_count);
}

void UpdateDpgRecordSizeAndTimestamp(DpgArchiveContext& archive, u32 record_index,
    u32 byte_count) {
    if (record_index >= archive.directory.size()) {
        return;
    }
    DpgRecordHeader& header = archive.directory[record_index].header;
    const u32 current = static_cast<u32>(record_payload_size(header));
    set_record_payload_size(header, current + byte_count);
    set_record_timestamp_now(header);
}

i32 FindDpgRecordByName(const DpgArchiveContext& archive, const char* name) {
    for (std::size_t i = 0; i < archive.directory.size(); ++i) {
        const DpgDirectoryEntry& entry = archive.directory[i];
        if (is_dpg_payload_record(entry.header.kind)) {
#ifdef _WIN32
            if (CompareMbcsCaseInsensitive(header_name(entry.header).c_str(), name) == 0) {
#else
            if (std::strcmp(header_name(entry.header).c_str(), name) == 0) {
#endif
                return static_cast<i32>(i);
            }
        }
    }
    return -1;
}

bool MarkDpgRecordDeleted(DpgArchiveContext& archive, u32 record_index) {
    if (record_index >= archive.directory.size()) {
        return false;
    }
    DpgRecordHeader& header = archive.directory[record_index].header;
    if (header.kind == DpgRecordKind::Payload) {
        header.kind = DpgRecordKind::Deleted;
        return RewriteDpgArchiveDirectoryRecord(archive, record_index);
    }
    return false;
}

bool DeleteDpgRecordByName(DpgArchiveContext& archive, const char* name) {
    const i32 index = FindDpgRecordByName(archive, name);
    return index >= 0 && MarkDpgRecordDeleted(archive, static_cast<u32>(index));
}

bool OpenDpgArchive(DpgArchiveContext& archive, const char* path, bool memory_backed) {
    CloseDpgArchive(archive);
    archive.backing_kind = memory_backed ? DpgBackingKind::Memory : DpgBackingKind::File;
    archive.archive_name = path != nullptr ? path : "";
    if (!memory_backed && !load_dpg_file_storage(archive, path)) {
        CloseDpgArchive(archive);
        return false;
    }
    archive.dirty = false;
    archive.open = true;
    archive.cursor = 0;
    ScanDpgArchiveDirectory(archive);
    if (archive.directory.empty()) {
        CloseDpgArchive(archive);
        return false;
    }
    return true;
}

void CopyDpgArchiveString(char* destination, const char* source, std::size_t capacity) {
    std::strncpy(destination, source, capacity);
}

bool OpenDpgArchiveByFileHandle(DpgArchiveContext& archive, u32 handle) {
    CloseDpgArchive(archive);
    archive.backing_kind = DpgBackingKind::File;
    archive.open = handle != 0 && handle != 0xffffffffu;
    archive.archive_name = "Open By Handle";
    archive.cursor = 0;
    if (archive.open) {
        ScanDpgArchiveDirectory(archive);
    }
    return archive.open && !archive.directory.empty();
}

bool OpenDpgArchiveByMemoryPointer(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count) {
    CloseDpgArchive(archive);
    if (data == nullptr || byte_count == 0) {
        return false;
    }
    archive.backing_kind = DpgBackingKind::Memory;
    archive.storage.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + byte_count);
    archive.open = true;
    archive.archive_name = "Open By File Pointer";
    archive.cursor = 0;
    ScanDpgArchiveDirectory(archive);
    return !archive.directory.empty();
}

bool CreateDpgArchive(DpgArchiveContext& archive, const char* path, const char* label,
    u32 data_record_count, bool memory_backed) {
    CloseDpgArchive(archive);
    archive.backing_kind = memory_backed ? DpgBackingKind::Memory : DpgBackingKind::File;
    archive.archive_name = path != nullptr ? path : "";
    archive.open = true;
    archive.directory_capacity = data_record_count + 2;
    archive.directory.reserve(archive.directory_capacity);

    DpgRecordHeader directory_header{};
    directory_header.kind = DpgRecordKind::Directory;
    set_header_name(directory_header, label);
    set_header_payload_u32(directory_header, kDpgHeaderMetadataOffset,
        archive.directory_capacity);
    set_header_payload_u32(directory_header, kDpgHeaderDataOffsetLowOffset,
        data_record_count + 1);

    archive.cursor = 0;
    if (!WriteDpgRecordHeader(archive, directory_header)) {
        CloseDpgArchive(archive);
        return false;
    }
    archive.directory.push_back(DpgDirectoryEntry{ 0, kDpgRecordHeaderBytes, directory_header });

    for (u32 i = 1; i <= data_record_count; ++i) {
        DpgRecordHeader header{};
        header.kind = DpgRecordKind::Free;
        const u32 offset = DpgArchiveTell(archive);
        if (!WriteDpgRecordHeader(archive, header)) {
            CloseDpgArchive(archive);
            return false;
        }
        archive.directory.push_back(DpgDirectoryEntry{ offset, DpgArchiveTell(archive), header });
    }

    DpgRecordHeader end_header{};
    end_header.kind = DpgRecordKind::End;
    const u32 end_offset = DpgArchiveTell(archive);
    if (!WriteDpgRecordHeader(archive, end_header)) {
        CloseDpgArchive(archive);
        return false;
    }
    archive.directory.push_back(DpgDirectoryEntry{ end_offset, DpgArchiveTell(archive),
        end_header });
    archive.directory_count = static_cast<u32>(archive.directory.size());
    return true;
}

void CloseDpgArchive(DpgArchiveContext& archive) {
    CloseDpgArchiveBackingStore(archive);
    archive.storage.clear();
    archive.directory.clear();
    ResetDpgArchiveContext(archive);
}

bool OpenDpgRecordByIndex(DpgArchiveContext& archive, u32 record_index) {
    if (!archive.open || record_index >= archive.directory.size()) {
        return false;
    }
    DpgDirectoryEntry& entry = archive.directory[record_index];
    if (entry.header.kind != DpgRecordKind::Payload) {
        return false;
    }
    archive.active_record_index = record_index;
    archive.active_record_offset = entry.data_offset;
    archive.active_record_size = static_cast<u32>(record_payload_size(entry.header));
    archive.cursor = entry.data_offset;
    return true;
}

bool OpenDpgRecordByName(DpgArchiveContext& archive, const char* name) {
    const i32 index = FindDpgRecordByName(archive, name);
    return index >= 0 && OpenDpgRecordByIndex(archive, static_cast<u32>(index));
}

bool ImportFileIntoDpgRecord(DpgArchiveContext& archive, const char* path, u32 metadata) {
    if (archive.active_record_index >= archive.directory.size()) {
        return false;
    }
    const std::string payload = path != nullptr ? path : "";
    DpgDirectoryEntry& entry = archive.directory[archive.active_record_index];
    set_record_payload_size(entry.header, static_cast<u32>(payload.size()));
    set_header_payload_u32(entry.header, kDpgHeaderMetadataOffset, metadata);
    archive.cursor = entry.data_offset;
    DpgArchiveWriteBytes(archive, payload.data(), payload.size());
    RewriteDpgArchiveDirectoryRecord(archive, archive.active_record_index);
    return true;
}

bool WriteBufferToDpgRecordPath(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count, const char* path, u32 metadata) {
    if (archive.active_record_index >= archive.directory.size() || data == nullptr) {
        return false;
    }
    DpgDirectoryEntry& entry = archive.directory[archive.active_record_index];
    entry.header.kind = DpgRecordKind::Payload;
    set_header_name(entry.header, path);
    set_record_payload_size(entry.header, static_cast<u32>(byte_count));
    set_header_payload_u32(entry.header, kDpgHeaderMetadataOffset, metadata);
    archive.cursor = entry.data_offset;
    DpgArchiveWriteBytes(archive, data, byte_count);
    RewriteDpgArchiveDirectoryRecord(archive, archive.active_record_index);
    return true;
}

bool ExportDpgRecordByNameToFile(DpgArchiveContext& archive, const char* name,
    const char* path) {
    const i32 index = FindDpgRecordByName(archive, name);
    return index >= 0 && ExportDpgRecordToFile(archive, static_cast<u32>(index), path, false);
}

bool ExportDpgRecordToFile(DpgArchiveContext& archive, u32 record_index, const char*,
    bool) {
    return OpenDpgRecordByIndex(archive, record_index);
}

bool ExportDpgRecordsToDirectory(DpgArchiveContext& archive, const char* directory_path,
    bool overwrite) {
    if (!archive.open) {
        return false;
    }

    bool exported_any = false;
    for (std::size_t i = 0; i < archive.directory.size(); ++i) {
        const DpgDirectoryEntry& entry = archive.directory[i];
        if (!is_dpg_payload_record(entry.header.kind)) {
            continue;
        }

        std::string output_path = header_name(entry.header);
        if (directory_path != nullptr && directory_path[0] != '\0') {
            output_path = directory_path;
            const char last = output_path.empty() ? '\0' : output_path.back();
            if (last != '\\' && last != '/') {
                output_path += '\\';
            }
            output_path += header_name(entry.header);
        }

        if (!ExportDpgRecordToFile(archive, static_cast<u32>(i), output_path.c_str(),
                overwrite)) {
            return exported_any;
        }
        exported_any = true;
    }
    return exported_any;
}

bool ExportDpgRecordByNameToMemory(DpgArchiveContext& archive, const char* name,
    std::vector<u8>& out) {
    const i32 index = FindDpgRecordByName(archive, name);
    if (index < 0 || !OpenDpgRecordByIndex(archive, static_cast<u32>(index))) {
        return false;
    }
    out.resize(archive.active_record_size);
    archive.cursor = archive.active_record_offset;
    return DpgArchiveReadBytes(archive, out.data(), out.size()) == out.size();
}

bool WriteDpgRecordByName(DpgArchiveContext& archive, const char* name, const void* data,
    std::size_t byte_count) {
    if (!OpenDpgRecordByName(archive, name)) {
        return false;
    }
    return WriteBufferToDpgRecordPath(archive, data, byte_count, name, 0);
}

bool ReadDpgRecordToBuffer(DpgArchiveContext& archive, u32 record_index, void* out,
    std::size_t* byte_count) {
    if (!OpenDpgRecordByIndex(archive, record_index)) {
        return false;
    }
    if (byte_count != nullptr) {
        *byte_count = archive.active_record_size;
    }
    if (out == nullptr) {
        return true;
    }
    archive.cursor = archive.active_record_offset;
    return DpgArchiveReadBytes(archive, out, archive.active_record_size) ==
        archive.active_record_size;
}

bool OpenFileOrDpgRecord(DpgArchiveContext& archive, const char* path, u32 desired_access,
    u32, u32 creation_disposition) {
    if (!archive.open) {
        return false;
    }
    if (creation_disposition == 3) {
        return OpenDpgRecordByName(archive, path);
    }
    if ((desired_access & 0x40000000u) != 0) {
        const i32 existing = FindDpgRecordByName(archive, path);
        if (existing >= 0) {
            return OpenDpgRecordByIndex(archive, static_cast<u32>(existing));
        }
        return create_empty_dpg_record(archive, path);
    }
    return false;
}

bool CloseArchiveAwareFile(DpgArchiveContext& archive) {
    if (!archive.open) {
        return true;
    }
    if (active_dpg_record(archive)) {
        RewriteDpgArchiveDirectoryRecord(archive, archive.active_record_index);
    }
    archive.active_record_index = 0xffffffffu;
    archive.active_record_offset = 0;
    archive.active_record_size = 0;
    return true;
}

std::size_t ReadArchiveAwareFile(DpgArchiveContext& archive, void* out,
    std::size_t byte_count) {
    if (!active_dpg_record(archive)) {
        return 0;
    }
    const DpgRecordHeader& header = archive.directory[archive.active_record_index].header;
    if (record_metadata(header) != 0) {
        return 0;
    }
    const std::size_t read = DpgArchiveReadBytes(archive, out, byte_count);
    if (read != 0 && record_payload_cipher_enabled(header)) {
        DecodeDpgPayloadBytes(archive, out, read);
    }
    return read;
}

std::size_t ReadArchiveAwareFileEx(DpgArchiveContext& archive, void* out,
    std::size_t byte_count) {
    return ReadArchiveAwareFile(archive, out, byte_count);
}

std::size_t WriteArchiveAwareFile(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count) {
    if (!active_dpg_record(archive) || data == nullptr || byte_count == 0) {
        return 0;
    }
    const DpgRecordHeader& header = archive.directory[archive.active_record_index].header;
    if (record_metadata(header) != 0) {
        return 0;
    }

    std::vector<u8> encoded(static_cast<const u8*>(data),
        static_cast<const u8*>(data) + byte_count);
    if (record_payload_cipher_enabled(header)) {
        EncodeDpgPayloadBytes(archive, encoded.data(), encoded.size());
    }

    const std::size_t written = DpgArchiveWriteBytes(archive, encoded.data(), encoded.size());
    if (written != 0) {
        UpdateDpgRecordSizeAndTimestamp(archive, archive.active_record_index,
            static_cast<u32>(written));
        archive.active_record_size = static_cast<u32>(
            record_payload_size(archive.directory[archive.active_record_index].header));
        RewriteDpgArchiveDirectoryRecord(archive, archive.active_record_index);
    }
    return written;
}

std::size_t WriteArchiveAwareFileEx(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count) {
    return WriteArchiveAwareFile(archive, data, byte_count);
}

u32 GetArchiveAwareFileSize(const DpgArchiveContext& archive, u32* high_dword) {
    if (high_dword != nullptr) {
        *high_dword = 0;
    }
    return active_dpg_record(archive) ? archive.active_record_size : 0xffffffffu;
}

u32 SeekArchiveAwareFile(DpgArchiveContext& archive, i32 offset, u32 origin) {
    if (!active_dpg_record(archive)) {
        return 0xffffffffu;
    }

    i64 base = 0;
    if (origin == 1) {
        base = static_cast<i64>(archive.cursor) - archive.active_record_offset;
    }
    else if (origin == 2) {
        base = archive.active_record_size;
    }
    else if (origin != 0) {
        return 0xffffffffu;
    }

    const i64 relative = base + offset;
    if (relative < 0) {
        return 0xffffffffu;
    }
    archive.cursor = archive.active_record_offset + static_cast<std::size_t>(relative);
    return static_cast<u32>(relative);
}

bool DeleteArchiveAwareFile(DpgArchiveContext& archive, const char* path) {
    const i32 index = FindDpgRecordByName(archive, path);
    return index >= 0 && DeleteDpgRecordByIndexPreservingCursor(archive, static_cast<u32>(index));
}

bool DeleteDpgRecordByIndexPreservingCursor(DpgArchiveContext& archive, u32 record_index) {
    if (record_index >= archive.directory.size()) {
        return false;
    }
    if (archive.active_record_index == record_index) {
        CloseArchiveAwareFile(archive);
    }

    const std::size_t saved_cursor = archive.cursor;
    DpgRecordHeader& header = archive.directory[record_index].header;
    header.kind = DpgRecordKind::Deleted;
    const bool ok = RewriteDpgArchiveDirectoryRecord(archive, record_index);
    archive.cursor = saved_cursor;
    return ok;
}

bool OpenArchiveAwareCrtStream(DpgArchiveContext& archive, const char* path, const char* mode) {
    if (!archive.open || path == nullptr || mode == nullptr) {
        return false;
    }

    const bool write_mode = FindArchiveModeCharacter(mode, 'w') != nullptr ||
        FindArchiveModeCharacter(mode, 'a') != nullptr ||
        FindArchiveModeCharacter(mode, '+') != nullptr;
    const bool append_mode = FindArchiveModeCharacter(mode, 'a') != nullptr;
    const bool truncate_mode = FindArchiveModeCharacter(mode, 'w') != nullptr;

    const i32 existing = FindDpgRecordByName(archive, path);
    if (!write_mode) {
        return existing >= 0 && OpenDpgRecordByIndex(archive, static_cast<u32>(existing));
    }

    if (truncate_mode && existing >= 0) {
        DeleteDpgRecordByIndexPreservingCursor(archive, static_cast<u32>(existing));
    }
    else if (existing >= 0 && OpenDpgRecordByIndex(archive, static_cast<u32>(existing))) {
        if (append_mode) {
            SeekArchiveAwareFile(archive, 0, 2);
        }
        return true;
    }

    if (!create_empty_dpg_record(archive, path)) {
        return false;
    }
    if (append_mode) {
        SeekArchiveAwareFile(archive, 0, 2);
    }
    return true;
}

const char* FindArchiveModeCharacter(const char* mode, char value) {
#ifdef _WIN32
    return FindMbcsCharacter(mode, static_cast<unsigned char>(value));
#else
    return std::strchr(mode, value);
#endif
}

bool CloseArchiveAwareCrtStream(DpgArchiveContext& archive) {
    return CloseArchiveAwareFile(archive);
}

std::size_t ReadArchiveAwareCrtStream(DpgArchiveContext& archive, void* out,
    std::size_t element_size, std::size_t element_count) {
    if (element_size == 0 || element_count == 0) {
        return 0;
    }
    return ReadArchiveAwareFile(archive, out, element_size * element_count) / element_size;
}

std::size_t WriteArchiveAwareCrtStream(DpgArchiveContext& archive, const void* data,
    std::size_t element_size, std::size_t element_count) {
    if (element_size == 0 || element_count == 0) {
        return 0;
    }
    return WriteArchiveAwareFile(archive, data, element_size * element_count) / element_size;
}

bool SeekArchiveAwareCrtStream(DpgArchiveContext& archive, i32 offset, u32 origin) {
    return SeekArchiveAwareFile(archive, offset, origin) != 0xffffffffu;
}

u32 TellArchiveAwareCrtStream(DpgArchiveContext& archive) {
    if (!active_dpg_record(archive) || archive.cursor < archive.active_record_offset) {
        return 0xffffffffu;
    }
    return static_cast<u32>(archive.cursor - archive.active_record_offset);
}

std::size_t CountFilesInDirectoryTree(const char* path, bool recursive) {
    namespace fs = std::filesystem;
    if (path == nullptr) {
        return 0;
    }

    std::error_code ec;
    const fs::path root(path);
    if (!fs::is_directory(root, ec)) {
        return 0;
    }

    std::size_t count = 0;
    if (recursive) {
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root,
                 fs::directory_options::skip_permission_denied, ec)) {
            if (!ec && entry.is_regular_file(ec)) {
                ++count;
            }
        }
    }
    else {
        for (const fs::directory_entry& entry : fs::directory_iterator(root,
                 fs::directory_options::skip_permission_denied, ec)) {
            if (!ec) {
                ++count;
            }
        }
    }
    return count;
}

std::vector<std::string> ListFilesInDirectoryTree(const char* path, bool recursive) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    if (path == nullptr) {
        return files;
    }

    std::error_code ec;
    const fs::path root(path);
    if (!fs::is_directory(root, ec)) {
        return files;
    }

    if (recursive) {
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root,
                 fs::directory_options::skip_permission_denied, ec)) {
            if (!ec && entry.is_regular_file(ec)) {
                files.push_back(entry.path().string());
            }
        }
    }
    else {
        for (const fs::directory_entry& entry : fs::directory_iterator(root,
                 fs::directory_options::skip_permission_denied, ec)) {
            if (!ec && entry.is_regular_file(ec)) {
                files.push_back(entry.path().string());
            }
        }
    }
    return files;
}

int CompareDirectoryTraversalName(const char* lhs, const char* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return lhs == rhs ? 0 : (lhs == nullptr ? -1 : 1);
    }
    return std::strcmp(lhs, rhs);
}

bool EnsureDirectoryPathExists(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    std::error_code ec;
    return std::filesystem::create_directories(path, ec) || std::filesystem::is_directory(path, ec);
}

}
