#pragma once

#include "ranker_types.h"

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace ranker {

struct TrcDirectoryEntry {
    std::string name;
    u32 relative_offset = 0;
    u32 original_size = 0;
    u32 stored_size = 0;
    u16 check_value = 0;
    u16 method = 0;
    u32 reserved = 0;
};

struct TrcWriteRecord {
    std::string name;
    std::vector<u8> payload;
    std::vector<u8> stored_payload;
    u16 method = 0;
    u32 reserved = 0;
    u32 original_size = 0;
    u16 check_value = 0;
    bool has_stored_payload = false;
    bool has_original_size = false;
    bool has_check_value = false;
};

struct TrcOutputBuilder {
    std::string archive_name;
    std::vector<TrcWriteRecord> records;
    TrcWriteRecord active_record;
    u32 directory_slots = 0;
    u32 directory_growth = 0x14;
    bool active = false;
    bool record_initialized = false;
    bool payload_written = false;
};

struct TrcBuilderInputFile {
    std::string path;
    std::vector<u8> payload;
    bool open = false;
};

struct TrcRecordReader {
    std::string archive_name;
    std::string archive_path;
    u32 record_index = 0;
    u32 data_offset = 0;
    u32 payload_offset = 0;
    TrcDirectoryEntry entry{};
    std::shared_ptr<const std::vector<u8>> archive_data_ref;
    std::vector<u8> archive_data;
    std::vector<u8> payload;
    std::size_t cursor = 0;
    bool directory_open = false;
    bool payload_open = false;
};

bool OpenTrcRecordDirectoryEntry(TrcRecordReader& reader, const char* archive_name,
    u32 record_index);
bool OpenTrcRecordPayload(TrcRecordReader& reader);
bool ReadOpenTrcRecordBytes(TrcRecordReader& reader, void* out, std::size_t byte_count);
void CloseTrcRecordReader(TrcRecordReader& reader);
bool LoadTrcRecordIntoBuffer(const char* archive_name, u32 record_index, void* out,
    std::size_t out_capacity, std::size_t* bytes_read = nullptr,
    TrcDirectoryEntry* entry = nullptr);

bool read_trc_record(const char* archive_name, u32 record_index, std::vector<u8>& out,
    TrcDirectoryEntry* entry = nullptr);

bool read_trc_record_bytes(const char* archive_name, u32 record_index, void* out,
    std::size_t out_capacity, std::size_t* bytes_read = nullptr,
    TrcDirectoryEntry* entry = nullptr);
u32 LoadTrcRecord9Value();
bool BuildTrcRecord10Key(char (&out_key)[16]);
void SwapDecimalDigit(u32& lhs, u32 lhs_position, u32& rhs, u32 rhs_position);
void SwapBitBetweenValues(u32& lhs, u32 lhs_bit, u32& rhs, u32 rhs_bit);
bool ValidateEncodedTrcKey(char (&group1)[5], char (&group2)[5], char (&group3)[6]);
bool ValidateTrcRecord10Key();
u32 PassThroughTrcKeyValidationValue(u32 value);
u32 QueryTrcRecordOriginalSize(const char* archive_name, u32 record_index);
bool QueryTrcRecordSizes(const char* archive_name, u32 record_index, u32* stored_size,
    u32* original_size, TrcDirectoryEntry* entry = nullptr);
bool QueryTrcArchiveRecordCount(const char* archive_name, u32* active_records,
    u32* directory_slots = nullptr);
bool LoadTrcRecordAlloc(const char* archive_name, u32 record_index, std::vector<u8>& out,
    std::size_t extra_bytes = 0, TrcDirectoryEntry* entry = nullptr);
bool ExtractTrcRecordToFile(const char* archive_name, u32 record_index,
    const char* destination_path);
bool HandleTrcRecordRangeReplacement(const char* destination_archive, u32 destination_start,
    const char* source_archive, u32 source_start, u32 record_count);
bool ReplaceTrcRecordPayloadFromFile(const char* archive_name, u32 record_index,
    const char* file_path);
bool PatchTrcRecordPayloadFromFile(const char* archive_name, u32 record_index,
    const char* file_path, u32 destination_offset);
bool PatchTrcRecordPayloadFromMemory(const char* archive_name, u32 record_index,
    const void* payload, std::size_t payload_size, u32 destination_offset);
bool AppendFilePayloadToTrcBuilder(const char* archive_name, const char* file_path,
    u16 storage_method);
bool AppendArchiveRecordToTrcBuilder(const char* destination_archive,
    const char* source_archive, u32 source_record_index, u16 storage_method);
bool WriteEmptyTrcBuilderHeader(const char* archive_name, u32 directory_slots = 0x14);
bool CheckTrcBuilderDirectorySlots(TrcOutputBuilder& builder, u32 required_records);
bool SetTrcBuilderDirectoryCapacity(TrcOutputBuilder& builder, u32 directory_slots);
bool OpenTrcBuilderInputFile(TrcBuilderInputFile& input, const char* file_path);
void HandleTrcBuilderInputHandleRelease(TrcBuilderInputFile& input);
bool InitializeTrcBuilderMemoryRecord(TrcOutputBuilder& builder, const char* archive_name,
    const char* record_name, u32 directory_growth, u16 storage_method);
bool HandleTrcBuilderPayloadWrite(TrcOutputBuilder& builder, const void* payload,
    std::size_t payload_size);
bool HandleTrcBuilderRecordCommit(TrcOutputBuilder& builder);
void HandleTrcOutputBuilderRelease(TrcOutputBuilder& builder);
bool HandleTrcMemoryRecordAppend(const char* archive_name, const char* record_name,
    const void* payload, std::size_t payload_size, u32 directory_growth, u16 storage_method);

bool CopyTrcBytesWithOptionalChecksum(std::istream& input, std::ostream& output,
    std::size_t byte_count, u16* checksum = nullptr);
bool WriteTrcRecords(const char* archive_name, const std::vector<TrcWriteRecord>& records,
    u32 directory_slots = 0);

bool IsZlibRuntimeAvailable();
u32 ZlibCompressBound113(u32 source_len);
int ZlibUncompress113(void* destination, u32* destination_len, const void* source,
    u32 source_len);
int ZlibCompress113WithLevel(void* destination, u32* destination_len, const void* source,
    u32 source_len, int compression_level);
int ZlibCompress113(void* destination, u32* destination_len, const void* source,
    u32 source_len);

}
