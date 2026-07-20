#include "ranker_replay.h"
#include "ranker_replay_archive.h"
#include "ranker_trc.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "REPLAY_SAVE_ROUNDTRIP_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void write_u32(std::array<u8, ranker::kReplayPacketBytes>& packet,
    std::size_t offset, u32 value) {
    std::memcpy(packet.data() + offset, &value, sizeof(value));
}

std::vector<u8> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    using namespace ranker;

    const std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        "ranker_replay_save_roundtrip_regression.ply";
    const std::filesystem::path source =
        std::filesystem::temp_directory_path() /
        "ranker_replay_save_roundtrip_source.trk";
    const std::filesystem::path no_camera_base =
        std::filesystem::temp_directory_path() /
        "ranker_replay_save_roundtrip_no_camera.ply";
    const std::filesystem::path stale_vpos_temp =
        std::filesystem::temp_directory_path() /
        "ranker_replay_save_roundtrip_stale.tmp";
    std::filesystem::path vpos = base;
    vpos.replace_extension(".vpo");
    std::filesystem::path no_camera_vpos = no_camera_base;
    no_camera_vpos.replace_extension(".vpo");
    std::error_code ignored;
    std::filesystem::remove(base, ignored);
    std::filesystem::remove(vpos, ignored);
    std::filesystem::remove(source, ignored);
    std::filesystem::remove(no_camera_base, ignored);
    std::filesystem::remove(no_camera_vpos, ignored);
    std::filesystem::remove(stale_vpos_temp, ignored);

    TrcWriteRecord map_record;
    map_record.name = "Map";
    map_record.payload = {1, 2, 3, 4};
    map_record.method = IsZlibRuntimeAvailable() ? 2 : 0;
    require(WriteTrcRecords(source.string().c_str(), {map_record}, 0x32),
        "source TRC creation");

    ReplayRecordingState recording;
    std::vector<u8> metadata(kReplayHeaderBytes - 0x63, 0);
    metadata[0] = 1;
    InitializeReplayTempFiles(recording, false, 0x120907e5u, 1, false, 0,
        metadata, false);

    recording.source_archive_path = source.string();

    std::array<u8, kReplayPacketBytes> command{};
    write_u32(command, 0x00, 1);
    write_u32(command, 0x04, kReplayPacketBytes);
    write_u32(command, 0x08, 3);
    command[0x0c] = 0;
    command[0x0f] = 0x0b;
    require(AppendReplayPacketRecord(recording, command.data(), command.size(), 77),
        "command packet recording");
    command[0x0f] = 0x13;
    require(AppendReplayPacketRecord(recording, command.data(), command.size(), 91),
        "terminal packet recording");
    require(AppendReplayViewportRecord(recording, 77, 123, 456),
        "camera recording");

    std::vector<u8> replay_payload(recording.header.begin(), recording.header.end());
    for (const auto& packet : recording.packet_records) {
        replay_payload.insert(replay_payload.end(), packet.begin(), packet.end());
    }
    require(PersistReplayRecordingArchive(base.string().c_str(), recording,
        replay_payload), "replay persistence");
    require(std::filesystem::is_regular_file(base), "missing replay archive");
    require(std::filesystem::is_regular_file(vpos), "missing VPOS sidecar");
    require(std::filesystem::file_size(vpos) ==
        0x40 + kReplayViewportRecordBytes, "VPOS size");

    u32 active_records = 0;
    require(QueryTrcArchiveRecordCount(base.string().c_str(), &active_records, nullptr),
        "saved TRC directory");
    require(active_records == 2, "saved TRC record count");
    std::vector<u8> loaded_map;
    std::vector<u8> loaded_replay;
    require(LoadTrcRecordAlloc(base.string().c_str(), 0, loaded_map),
        "map record load");
    require(LoadTrcRecordAlloc(base.string().c_str(), 1, loaded_replay),
        "replay record load");
    require(loaded_map == map_record.payload, "source map preservation");
    require(loaded_replay == replay_payload, "replay payload roundtrip");

    const std::vector<u8> vpos_bytes = read_file(vpos);
    require(vpos_bytes.size() == 0x40 + kReplayViewportRecordBytes,
        "VPOS readback size");
    require(std::memcmp(vpos_bytes.data(), "Jwar2 Replay Vpos File.", 23) == 0,
        "VPOS signature");
    u32 recorded_frame = 0;
    std::memcpy(&recorded_frame, vpos_bytes.data() + 0x40, sizeof(recorded_frame));
    require(recorded_frame == 77, "VPOS frame roundtrip");

    ReplayRecordingState no_camera_recording = recording;
    no_camera_recording.viewport_records.clear();
    no_camera_recording.viewport_scratch = {};
    no_camera_recording.viewport_count = 0;
    no_camera_recording.viewport_total_count = 0;
    no_camera_recording.viewport_has_last_camera = false;
    no_camera_recording.viewport_temp_path = stale_vpos_temp.string();
    {
        std::ofstream stale(stale_vpos_temp, std::ios::binary | std::ios::trunc);
        stale.write("stale", 5);
    }
    require(PersistReplayRecordingArchive(no_camera_base.string().c_str(),
        no_camera_recording, replay_payload), "no-camera replay persistence");
    require(std::filesystem::is_regular_file(no_camera_vpos),
        "missing no-camera VPOS sidecar");
    const std::vector<u8> no_camera_vpos_bytes = read_file(no_camera_vpos);
    require(no_camera_vpos_bytes.size() == 0x40, "no-camera VPOS size");
    require(std::memcmp(no_camera_vpos_bytes.data(),
        "Jwar2 Replay Vpos File.", 23) == 0,
        "no-camera VPOS signature");

    std::filesystem::remove(base, ignored);
    std::filesystem::remove(vpos, ignored);
    std::filesystem::remove(source, ignored);
    std::filesystem::remove(no_camera_base, ignored);
    std::filesystem::remove(no_camera_vpos, ignored);
    std::filesystem::remove(stale_vpos_temp, ignored);
    return 0;
}
