#include "ranker_legacy_file.h"

#ifdef _WIN32
#include "ranker_crt_runtime.h"

namespace ranker {
namespace {

LegacyFileState g_legacy_file_state;
constexpr char kSetupDataPath[] = "_SETUP.DAT";

const char* active_path() {
    auto& state = g_legacy_file_state;
    return state.path_pointer != nullptr ? state.path_pointer : state.path.c_str();
}

void set_owned_active_path(const std::string& path) {
    auto& state = g_legacy_file_state;
    state.path = path;
    state.path_pointer = state.path.c_str();
}

void record_last_error(bool) {
}

void close_handle(HFILE handle) {
    _lclose(handle);
}

void set_file_handle(HFILE handle) {
    auto& state = g_legacy_file_state;
    state.file_handle = handle;
    state.operation_result = handle;
    record_last_error(handle != HFILE_ERROR);
}

void ensure_transfer_buffer() {
    auto& state = g_legacy_file_state;
    if (state.transfer_chunk_size <= 0) {
        state.transfer_chunk_size = 0x8000;
    }
    if (state.transfer_buffer.size() < static_cast<std::size_t>(state.transfer_chunk_size)) {
        state.transfer_buffer.resize(static_cast<std::size_t>(state.transfer_chunk_size));
    }
}

} // namespace

LegacyFileState& legacy_file_state() {
    return g_legacy_file_state;
}

void SetLegacyFilePath(const char* path) {
    set_owned_active_path(path == nullptr ? "" : path);
}

void SetLegacyFilePathPointer(const char* path) {
    auto& state = g_legacy_file_state;
    state.path = path == nullptr ? "" : path;
    state.path_pointer = path;
}

void SetLegacyFileMovePaths(const char* source, const char* destination) {
    auto& state = g_legacy_file_state;
    state.move_source = source == nullptr ? "" : source;
    state.move_destination = destination == nullptr ? "" : destination;
}

void SetLegacyFileCopyPaths(const char* source, const char* destination) {
    auto& state = g_legacy_file_state;
    state.copy_source = source == nullptr ? "" : source;
    state.copy_destination = destination == nullptr ? "" : destination;
}

void SetLegacyFileReadRequest(void* buffer, LONG byte_count) {
    auto& state = g_legacy_file_state;
    state.read_buffer = buffer;
    state.read_size = byte_count;
}

void SetLegacyFileWriteRequest(const void* buffer, LONG byte_count) {
    auto& state = g_legacy_file_state;
    state.write_buffer = buffer;
    state.write_size = byte_count;
}

void SetLegacyFileSeekRequest(LONG offset, int origin) {
    auto& state = g_legacy_file_state;
    state.seek_offset = offset;
    state.position = offset;
    state.seek_origin = origin;
}

void SetLegacyFileTransferChunkSize(LONG byte_count) {
    g_legacy_file_state.transfer_chunk_size = byte_count;
}

void HandleLegacyFileNoopA() {
}

void HandleLegacyFileNoopB() {
}

void HandleLegacyFileReadOnlyOpen() {
    const char* path = active_path();
    set_file_handle(_lopen(path, OF_READ));
}

void HandleLegacyFileWriteOnlyOpen() {
    const char* path = active_path();
    set_file_handle(_lopen(path, OF_WRITE));
    if (g_legacy_file_state.file_handle == HFILE_ERROR) {
        HandleLegacyFileTruncatePath();
    }
}

void HandleLegacyFileReadWriteOpen() {
    const char* path = active_path();
    set_file_handle(_lopen(path, OF_READWRITE));
}

void HandleLegacyFileTruncatePath() {
    const char* path = active_path();
    set_file_handle(_lcreat(path, 0));
}

void HandleLegacyFileRemovePath() {
    const char* path = active_path();
    const BOOL ok = DeleteFileA(path);
    g_legacy_file_state.operation_result = ok;
    g_legacy_file_state.file_handle = static_cast<HFILE>(ok);
    record_last_error(ok != FALSE);
}

void HandleLegacyFileRenamePath() {
    const char* source = g_legacy_file_state.move_source.c_str();
    const char* destination = g_legacy_file_state.move_destination.c_str();
    const int result = HandleCrtRenamePath(source, destination);
    g_legacy_file_state.operation_result = result;
    g_legacy_file_state.file_handle =
        static_cast<HFILE>(g_legacy_file_state.operation_result);
    record_last_error(result == 0);
}

void HandleLegacyFileReleaseHandle() {
    close_handle(g_legacy_file_state.file_handle);
}

void HandleLegacyFileSeekHandle() {
    auto& state = g_legacy_file_state;
    state.position = _llseek(state.file_handle, state.position, state.seek_origin);
    state.seek_offset = state.position;
    record_last_error(state.position != HFILE_ERROR);
}

void GetLegacyFileCurrentPosition() {
    auto& state = g_legacy_file_state;
    state.position = _llseek(state.file_handle, 0, FILE_CURRENT);
    record_last_error(state.position != HFILE_ERROR);
}

void GetLegacyFileEndPosition() {
    auto& state = g_legacy_file_state;
    state.position = _llseek(state.file_handle, 0, FILE_END);
    record_last_error(state.position != HFILE_ERROR);
}

void CalculateLegacyFileByteLength() {
    auto& state = g_legacy_file_state;
    GetLegacyFileCurrentPosition();
    const LONG original_position = state.position;
    GetLegacyFileEndPosition();
    state.byte_result = state.position;
    const LONG restored = _llseek(state.file_handle, original_position, FILE_BEGIN);
    record_last_error(restored != HFILE_ERROR);
}

void HandleLegacyFileMeasureAfterOpen() {
    HandleLegacyFileReadWriteOpen();
    if (g_legacy_file_state.file_handle != HFILE_ERROR) {
        CalculateLegacyFileByteLength();
    }
}

void HandleLegacyFileReadBlock() {
    auto& state = g_legacy_file_state;
    state.byte_result = _hread(state.file_handle, state.read_buffer, state.read_size);
    record_last_error(state.byte_result != -1);
}

void HandleLegacyFileWriteBlock() {
    auto& state = g_legacy_file_state;
    state.byte_result = _hwrite(state.file_handle,
        static_cast<const char*>(state.write_buffer), state.write_size);
    record_last_error(state.byte_result != -1);
}

void HandleLegacyFileConfiguredRead() {
    HandleLegacyFileReadWriteOpen();
    if (g_legacy_file_state.file_handle != HFILE_ERROR) {
        HandleLegacyFileReadBlock();
        HandleLegacyFileReleaseHandle();
    }
}

void HandleLegacyFileConfiguredWrite() {
    HandleLegacyFileWriteOnlyOpen();
    if (g_legacy_file_state.file_handle != HFILE_ERROR) {
        HandleLegacyFileWriteBlock();
        HandleLegacyFileReleaseHandle();
    }
}

bool HandleSetupDataConfiguredRead(void* buffer, LONG byte_count) {
    if (buffer == nullptr || byte_count < 0) {
        g_legacy_file_state.byte_result = -1;
        g_legacy_file_state.last_error = ERROR_INVALID_PARAMETER;
        return false;
    }
    SetLegacyFilePath(kSetupDataPath);
    SetLegacyFileReadRequest(buffer, byte_count);
    HandleLegacyFileConfiguredRead();
    return g_legacy_file_state.byte_result == byte_count;
}

bool HandleSetupDataConfiguredWrite(const void* buffer, LONG byte_count) {
    if (buffer == nullptr || byte_count < 0) {
        g_legacy_file_state.byte_result = -1;
        g_legacy_file_state.last_error = ERROR_INVALID_PARAMETER;
        return false;
    }
    SetLegacyFilePath(kSetupDataPath);
    SetLegacyFileWriteRequest(buffer, byte_count);
    HandleLegacyFileConfiguredWrite();
    return g_legacy_file_state.byte_result == byte_count;
}

bool HandleLegacyFileChunkCopy() {
    auto& state = g_legacy_file_state;

    state.copy_phase = LegacyFileCopyPhase::SourceOpen;
    set_owned_active_path(state.copy_source);
    HandleLegacyFileMeasureAfterOpen();
    if (state.file_handle == HFILE_ERROR) {
        return false;
    }

    const HFILE source = state.file_handle;
    LONG remaining = state.byte_result;

    state.copy_phase = LegacyFileCopyPhase::DestinationOpen;
    set_owned_active_path(state.copy_destination);
    HandleLegacyFileReadWriteOpen();
    if (state.file_handle == HFILE_ERROR) {
        close_handle(source);
        return false;
    }

    const HFILE destination = state.file_handle;
    ensure_transfer_buffer();
    state.read_buffer = state.transfer_buffer.data();
    state.write_buffer = state.transfer_buffer.data();

    while (remaining != 0) {
        if (state.transfer_chunk_size <= 0) {
            state.copy_phase = LegacyFileCopyPhase::Read;
            close_handle(destination);
            close_handle(source);
            return false;
        }

        if (remaining < state.transfer_chunk_size) {
            state.transfer_chunk_size = remaining;
        }

        state.copy_phase = LegacyFileCopyPhase::Read;
        state.file_handle = source;
        state.read_size = state.transfer_chunk_size;
        HandleLegacyFileReadBlock();
        if (state.byte_result != state.read_size) {
            close_handle(destination);
            close_handle(source);
            return false;
        }

        state.copy_phase = LegacyFileCopyPhase::Write;
        state.file_handle = destination;
        state.write_size = state.transfer_chunk_size;
        HandleLegacyFileWriteBlock();
        if (state.byte_result != state.write_size) {
            close_handle(destination);
            close_handle(source);
            return false;
        }

        remaining -= state.transfer_chunk_size;
    }

    state.copy_phase = LegacyFileCopyPhase::Idle;
    state.file_handle = destination;
    HandleLegacyFileReleaseHandle();
    state.file_handle = source;
    HandleLegacyFileReleaseHandle();
    return true;
}

}
#endif
