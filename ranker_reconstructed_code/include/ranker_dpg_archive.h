#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kDpgArchiveMagic = 0x1a504447u;
constexpr u32 kDpgRecordHeaderBytes = 0x13c;
constexpr u32 kDpgDirectoryRecordBytes = 0x144;
constexpr u32 kDpgHeaderCipherDwordCount = 0x4d;
constexpr u32 kDpgHeaderCipherConstant = 0x36175a5au;

enum class DpgBackingKind : u32 {
    File = 0,
    Memory = 1,
};

enum class DpgRecordKind : u32 {
    Empty = 0,
    Redirect = 1,
    Payload = 2,
    Deleted = 3,
    Free = 4,
    End = 5,
    Directory = kDpgArchiveMagic,
};

struct DpgRecordHeader {
    DpgRecordKind kind = DpgRecordKind::Empty;
    std::array<u8, kDpgRecordHeaderBytes - sizeof(u32)> payload{};
};

struct DpgDirectoryEntry {
    u32 header_offset = 0;
    u32 data_offset = 0;
    DpgRecordHeader header;
};

struct DpgArchiveContext {
    DpgBackingKind backing_kind = DpgBackingKind::File;
    std::vector<u8> storage;
    std::vector<DpgDirectoryEntry> directory;
    std::size_t cursor = 0;
    u32 header_seed = 0;
    u32 cipher_seed = 0;
    u32 directory_offset = 0;
    u32 directory_count = 0;
    u32 directory_capacity = 0;
    bool open = false;
    bool dirty = false;
    bool cipher_enabled = false;
    u32 active_record_index = 0xffffffffu;
    u32 active_record_offset = 0;
    u32 active_record_size = 0;
    std::string archive_name;
};

void InitializeDpgArchiveContext(DpgArchiveContext& archive);
void ResetDpgArchiveContext(DpgArchiveContext& archive);
void CloseDpgArchiveWrapper(DpgArchiveContext& archive);
std::size_t DpgArchiveReadBytes(DpgArchiveContext& archive, void* out, std::size_t byte_count);
std::size_t DpgArchiveWriteBytes(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count);
bool DpgArchiveSeek(DpgArchiveContext& archive, i32 offset, u32 origin);
u32 DpgArchiveTell(DpgArchiveContext& archive);
void CloseDpgArchiveBackingStore(DpgArchiveContext& archive);
void DecodeDpgRecordHeader(DpgArchiveContext& archive, DpgRecordHeader& header);
void EncodeDpgRecordHeader(DpgArchiveContext& archive, DpgRecordHeader& header);
void DecodeDpgPayloadBytes(const DpgArchiveContext& archive, void* data,
    std::size_t byte_count);
void EncodeDpgPayloadBytes(const DpgArchiveContext& archive, void* data,
    std::size_t byte_count);
void ConfigureDpgArchiveCipher(DpgArchiveContext& archive, u32 header_seed,
    u32 cipher_seed, u32 directory_offset);
bool ReadDpgRecordHeader(DpgArchiveContext& archive, DpgRecordHeader& header);
bool WriteDpgRecordHeader(DpgArchiveContext& archive, const DpgRecordHeader& header);
void ScanDpgArchiveDirectory(DpgArchiveContext& archive);
void EnsureDpgArchiveTerminatorRecord(DpgArchiveContext& archive);
void AppendDpgArchivePaddingRecords(DpgArchiveContext& archive, u32 count);
bool RewriteDpgArchiveDirectoryRecord(DpgArchiveContext& archive, u32 index);
std::size_t DpgArchiveReadBackingBytes(DpgArchiveContext& archive, void* out,
    std::size_t byte_count);
std::size_t DpgArchiveWriteBackingBytes(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count);
void UpdateDpgRecordSizeAndTimestamp(DpgArchiveContext& archive, u32 record_index,
    u32 byte_count);
i32 FindDpgRecordByName(const DpgArchiveContext& archive, const char* name);
bool MarkDpgRecordDeleted(DpgArchiveContext& archive, u32 record_index);
bool DeleteDpgRecordByName(DpgArchiveContext& archive, const char* name);
bool OpenDpgArchive(DpgArchiveContext& archive, const char* path, bool memory_backed = false);
void CopyDpgArchiveString(char* destination, const char* source, std::size_t capacity);
bool OpenDpgArchiveByFileHandle(DpgArchiveContext& archive, u32 handle);
bool OpenDpgArchiveByMemoryPointer(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count);
bool CreateDpgArchive(DpgArchiveContext& archive, const char* path, const char* label,
    u32 data_record_count, bool memory_backed = false);
void CloseDpgArchive(DpgArchiveContext& archive);
bool OpenDpgRecordByIndex(DpgArchiveContext& archive, u32 record_index);
bool OpenDpgRecordByName(DpgArchiveContext& archive, const char* name);
bool ImportFileIntoDpgRecord(DpgArchiveContext& archive, const char* path, u32 metadata);
bool WriteBufferToDpgRecordPath(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count, const char* path, u32 metadata);
bool ExportDpgRecordByNameToFile(DpgArchiveContext& archive, const char* name,
    const char* path);
bool ExportDpgRecordToFile(DpgArchiveContext& archive, u32 record_index, const char* path,
    bool overwrite);
bool ExportDpgRecordsToDirectory(DpgArchiveContext& archive, const char* directory_path,
    bool overwrite);
bool ExportDpgRecordByNameToMemory(DpgArchiveContext& archive, const char* name,
    std::vector<u8>& out);
bool WriteDpgRecordByName(DpgArchiveContext& archive, const char* name, const void* data,
    std::size_t byte_count);
bool ReadDpgRecordToBuffer(DpgArchiveContext& archive, u32 record_index, void* out,
    std::size_t* byte_count);
bool OpenFileOrDpgRecord(DpgArchiveContext& archive, const char* path, u32 desired_access,
    u32 share_mode, u32 creation_disposition);
bool CloseArchiveAwareFile(DpgArchiveContext& archive);
std::size_t ReadArchiveAwareFile(DpgArchiveContext& archive, void* out,
    std::size_t byte_count);
std::size_t ReadArchiveAwareFileEx(DpgArchiveContext& archive, void* out,
    std::size_t byte_count);
std::size_t WriteArchiveAwareFile(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count);
std::size_t WriteArchiveAwareFileEx(DpgArchiveContext& archive, const void* data,
    std::size_t byte_count);
u32 GetArchiveAwareFileSize(const DpgArchiveContext& archive, u32* high_dword = nullptr);
u32 SeekArchiveAwareFile(DpgArchiveContext& archive, i32 offset, u32 origin);
bool DeleteArchiveAwareFile(DpgArchiveContext& archive, const char* path);
bool DeleteDpgRecordByIndexPreservingCursor(DpgArchiveContext& archive, u32 record_index);
bool OpenArchiveAwareCrtStream(DpgArchiveContext& archive, const char* path, const char* mode);
const char* FindArchiveModeCharacter(const char* mode, char value);
bool CloseArchiveAwareCrtStream(DpgArchiveContext& archive);
std::size_t ReadArchiveAwareCrtStream(DpgArchiveContext& archive, void* out,
    std::size_t element_size, std::size_t element_count);
std::size_t WriteArchiveAwareCrtStream(DpgArchiveContext& archive, const void* data,
    std::size_t element_size, std::size_t element_count);
bool SeekArchiveAwareCrtStream(DpgArchiveContext& archive, i32 offset, u32 origin);
u32 TellArchiveAwareCrtStream(DpgArchiveContext& archive);
std::size_t CountFilesInDirectoryTree(const char* path, bool recursive);
std::vector<std::string> ListFilesInDirectoryTree(const char* path, bool recursive);
int CompareDirectoryTraversalName(const char* lhs, const char* rhs);
bool EnsureDirectoryPathExists(const char* path);

}
