#include "ranker_replay.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace ranker {
namespace {

ReplayRecordingState g_replay_recording_state;

void copy_ascii(std::array<u8, kReplayHeaderBytes>& header,
    std::size_t offset, const char* text) {
    if (text == nullptr || offset >= header.size()) {
        return;
    }

    const std::size_t length =
        std::min<std::size_t>(std::strlen(text), header.size() - offset);
    std::copy_n(reinterpret_cast<const u8*>(text), length, header.begin() + offset);
}

void write_u32(std::array<u8, kReplayHeaderBytes>& header,
    std::size_t offset, u32 value) {
    if (offset > header.size() || header.size() - offset < sizeof(value)) {
        return;
    }
    std::memcpy(header.data() + offset, &value, sizeof(value));
}

void write_current_replay_timestamp(std::array<u8, kReplayHeaderBytes>& header) {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char date[16]{};
    char time[16]{};
    std::snprintf(date, sizeof(date), "%02d/%02d %04d",
        local.tm_mon + 1, local.tm_mday, local.tm_year + 1900);
    std::snprintf(time, sizeof(time), "%02d:%02d:%02d",
        local.tm_hour, local.tm_min, local.tm_sec);
    copy_ascii(header, 0x3f, date);
    copy_ascii(header, 0x4f, time);
}

u32 read_packet_u32(const u8* packet, u32 packet_size, u32 offset) {
    if (packet == nullptr || offset > packet_size ||
        packet_size - offset < sizeof(u32)) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, packet + offset, sizeof(value));
    return value;
}

void write_packet_u32(std::array<u8, kReplayPacketBytes>& packet,
    std::size_t offset, u32 value) {
    if (offset > packet.size() || packet.size() - offset < sizeof(value)) {
        return;
    }
    std::memcpy(packet.data() + offset, &value, sizeof(value));
}

} // namespace

ReplayRecordingState& replay_recording_state() {
    return g_replay_recording_state;
}

void InitializeReplayTempFiles(ReplayRecordingState& state, bool playback_mode,
    u32 game_version, u8 reliable_mode, bool forced_replay_mode, u8 local_player,
    const std::vector<u8>& metadata, bool scenario_ai_profile_override) {
    state.playback_mode = playback_mode;
    state.packet_temp_open = false;
    state.viewport_temp_open = false;
    state.scenario_ai_profile_override = scenario_ai_profile_override;
    state.last_output_path.clear();
    state.playback_archive_path.clear();
    state.playback_payload.clear();
    state.packet_count = 0;
    state.playback_last_frame_tick = 0;
    state.packet_scratch.fill(0);
    state.packet_records.clear();
    state.first_packet_by_channel.fill(0);
    state.packet_flush_count = 0;
    state.header.fill(0);
    state.viewport_scratch = {};
    state.viewport_records.clear();
    state.viewport_count = 0;
    state.viewport_total_count = 0;
    state.viewport_flush_count = 0;
    state.viewport_first_frame = 0xffffffffu;
    state.viewport_last_frame = 0xffffffffu;
    state.viewport_last_camera_x = 0;
    state.viewport_last_camera_y = 0;
    state.viewport_has_last_camera = false;

    if (playback_mode || scenario_ai_profile_override) {
        return;
    }

    state.packet_temp_open = true;
    copy_ascii(state.header, 0, "Jwar2 Replay File.");
    write_u32(state.header, 0x17, game_version);
    state.header[0x1b] = forced_replay_mode ? 2 : reliable_mode;
    write_current_replay_timestamp(state.header);
    state.header[0x5f] = local_player;
    const std::size_t metadata_copy =
        std::min<std::size_t>(metadata.size(), state.header.size() - 0x63);
    std::copy_n(metadata.begin(), metadata_copy, state.header.begin() + 0x63);
    state.viewport_temp_open = true;
}

bool FinalizeReplayRecordingDisabled(
    ReplayRecordingState& state, const char* output_path) {
    (void)state;
    (void)output_path;
    return true;
}

bool AppendReplayPacketRecord(ReplayRecordingState& state, const void* packet,
    u32 packet_size, u32 frame_tick) {
    if (state.playback_mode || !state.packet_temp_open || packet == nullptr ||
        packet_size < 0x10) {
        return false;
    }

    const auto* bytes = static_cast<const u8*>(packet);
    if (bytes[0x0f] == 0x10) {
        if (packet_size < 0x14) {
            return false;
        }
        const u32 channel = bytes[0x0c];
        if (channel < state.first_packet_by_channel.size()) {
            state.first_packet_by_channel[channel] =
                read_packet_u32(bytes, packet_size, 0x10);
            return true;
        }
        return false;
    }

    std::array<u8, kReplayPacketBytes> record{};
    const u32 copy_size = std::min<u32>(packet_size, kReplayPacketBytes);
    std::memcpy(record.data(), packet, copy_size);
    write_packet_u32(record, 4, frame_tick);

    const u32 slot = state.packet_count & (kReplayPacketRingSlots - 1);
    const std::size_t offset = static_cast<std::size_t>(slot) *
        kReplayPacketBytes;
    std::copy(record.begin(), record.end(),
        state.packet_scratch.begin() + static_cast<std::ptrdiff_t>(offset));
    state.packet_records.push_back(record);
    ++state.packet_count;
    if ((state.packet_count % kReplayPacketRingSlots) == 0) {
        ++state.packet_flush_count;
    }
    return true;
}

bool AppendReplayViewportRecord(ReplayRecordingState& state, u32 frame_tick,
    i32 camera_x, i32 camera_y) {
    if (state.playback_mode || !state.viewport_temp_open) {
        return false;
    }

    const i16 truncated_x = static_cast<i16>(camera_x);
    const i16 truncated_y = static_cast<i16>(camera_y);
    if (state.viewport_has_last_camera &&
        state.viewport_last_camera_x == truncated_x &&
        state.viewport_last_camera_y == truncated_y) {
        return false;
    }

    ReplayViewportRecord record;
    record.frame_tick = frame_tick;
    record.camera_x = truncated_x;
    record.camera_y = truncated_y;

    state.viewport_scratch[state.viewport_count] = record;
    ++state.viewport_count;
    ++state.viewport_total_count;

    state.viewport_records.push_back(record);
    if (state.viewport_first_frame == 0xffffffffu) {
        state.viewport_first_frame = frame_tick;
    }
    state.viewport_last_frame = frame_tick;
    state.viewport_last_camera_x = truncated_x;
    state.viewport_last_camera_y = truncated_y;
    state.viewport_has_last_camera = true;
    if (state.viewport_count >= state.viewport_scratch.size()) {
        ++state.viewport_flush_count;
        state.viewport_count = 0;
    }
    return true;
}

} // namespace ranker
