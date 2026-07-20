#include "ranker_replay_archive.h"

#include "ranker_trc.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace ranker {
namespace {

constexpr const char* kReplayVposHeaderText = "Jwar2 Replay Vpos File.";

void write_le_u32(std::vector<u8>& bytes, std::size_t offset, u32 value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        return;
    }
    bytes[offset] = static_cast<u8>(value & 0xffu);
    bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xffu);
}

void write_le_u16(std::vector<u8>& bytes, std::size_t offset, u16 value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        return;
    }
    bytes[offset] = static_cast<u8>(value & 0xffu);
    bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
}

std::string replace_extension(const std::string& path, const char* extension) {
    std::filesystem::path value(path);
    value.replace_extension(extension == nullptr ? "" : extension);
    return value.string();
}

bool write_binary_file(const std::string& path, const std::vector<u8>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

bool write_vpos_file_from_recording(const std::string& replay_path,
    const ReplayRecordingState& recording) {
    const std::string output_path = replace_extension(replay_path, ".vpo");
    // The reconstructed recorder keeps viewport samples in memory.  Rebuild
    // the sidecar even when there were no camera changes so an unrelated or
    // stale Replayvpo.tmp can never be attached to the newly saved replay.
    std::vector<u8> payload(0x40, 0);
    std::memcpy(payload.data(), kReplayVposHeaderText,
        std::strlen(kReplayVposHeaderText));
    write_le_u32(payload, 0x18, 1);

    const std::size_t count = !recording.viewport_records.empty() ?
        recording.viewport_records.size() :
        static_cast<std::size_t>(std::min<u32>(recording.viewport_count,
            kReplayViewportRecordCount));
    payload.resize(0x40 + count * kReplayViewportRecordBytes);
    for (std::size_t i = 0; i < count; ++i) {
        const ReplayViewportRecord& record = !recording.viewport_records.empty() ?
            recording.viewport_records[i] : recording.viewport_scratch[i];
        const std::size_t offset = 0x40 + i * kReplayViewportRecordBytes;
        write_le_u32(payload, offset, record.frame_tick);
        write_le_u16(payload, offset + 4, static_cast<u16>(record.camera_x));
        write_le_u16(payload, offset + 6, static_cast<u16>(record.camera_y));
    }
    return write_binary_file(output_path, payload);
}

bool copy_source_archive_for_save(const ReplayRecordingState& recording,
    const char* output_path) {
    if (recording.source_archive_path.empty()) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(recording.source_archive_path, error)) {
        return false;
    }
    std::filesystem::copy_file(recording.source_archive_path, output_path,
        std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

} // namespace

bool PersistReplayRecordingArchive(const char* output_path,
    const ReplayRecordingState& recording, const std::vector<u8>& payload) {
    if (output_path == nullptr || *output_path == '\0' || payload.empty()) {
        return false;
    }

    const u16 method = IsZlibRuntimeAvailable() ? 2 : 0;
    bool archive_ready = copy_source_archive_for_save(recording, output_path);
    if (archive_ready) {
        archive_ready = HandleTrcMemoryRecordAppend(output_path, "Replay",
            payload.data(), payload.size(), 0x14, method);
    } else {
        TrcWriteRecord record;
        record.name = "Replay";
        record.payload = payload;
        record.method = method;
        archive_ready = WriteTrcRecords(output_path, {record}, 0x32);
    }
    if (!archive_ready) {
        return false;
    }

    if (recording.viewport_temp_open) {
        // The original replay save path does not discard an otherwise valid
        // .ply when the optional camera-position sidecar cannot be written.
        write_vpos_file_from_recording(output_path, recording);
    }
    return true;
}

} // namespace ranker
