#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32
enum class LegacyFileCopyPhase : i32 {
    Idle = 0,
    SourceOpen = 1,
    DestinationOpen = 2,
    Read = 3,
    Write = 4,
};

struct LegacyFileState {
    std::string path;
    const char* path_pointer = nullptr;
    std::string move_source;
    std::string move_destination;
    std::string copy_source;
    std::string copy_destination;
    HFILE file_handle = HFILE_ERROR;
    LONG operation_result = HFILE_ERROR;
    LONG byte_result = 0;
    LONG position = 0;
    LONG seek_offset = 0;
    int seek_origin = FILE_BEGIN;
    void* read_buffer = nullptr;
    const void* write_buffer = nullptr;
    LONG read_size = 0;
    LONG write_size = 0;
    std::vector<u8> transfer_buffer;
    LONG transfer_chunk_size = 0x8000;
    LegacyFileCopyPhase copy_phase = LegacyFileCopyPhase::Idle;
    DWORD last_error = ERROR_SUCCESS;
};

LegacyFileState& legacy_file_state();

void SetLegacyFilePath(const char* path);
void SetLegacyFilePathPointer(const char* path);
void SetLegacyFileMovePaths(const char* source, const char* destination);
void SetLegacyFileCopyPaths(const char* source, const char* destination);
void SetLegacyFileReadRequest(void* buffer, LONG byte_count);
void SetLegacyFileWriteRequest(const void* buffer, LONG byte_count);
void SetLegacyFileSeekRequest(LONG offset, int origin);
void SetLegacyFileTransferChunkSize(LONG byte_count);

void HandleLegacyFileNoopA();
void HandleLegacyFileNoopB();
void HandleLegacyFileReadOnlyOpen();
void HandleLegacyFileWriteOnlyOpen();
void HandleLegacyFileReadWriteOpen();
void HandleLegacyFileTruncatePath();
void HandleLegacyFileRemovePath();
void HandleLegacyFileRenamePath();
void HandleLegacyFileReleaseHandle();
void HandleLegacyFileSeekHandle();
void GetLegacyFileCurrentPosition();
void GetLegacyFileEndPosition();
void CalculateLegacyFileByteLength();
void HandleLegacyFileMeasureAfterOpen();
void HandleLegacyFileReadBlock();
void HandleLegacyFileWriteBlock();
void HandleLegacyFileConfiguredRead();
void HandleLegacyFileConfiguredWrite();
bool HandleSetupDataConfiguredRead(void* buffer, LONG byte_count = 0x1ff0);
bool HandleSetupDataConfiguredWrite(const void* buffer, LONG byte_count = 0x1ff0);
bool HandleLegacyFileChunkCopy();
#endif

}
