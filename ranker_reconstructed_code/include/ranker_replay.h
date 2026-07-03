#pragma once

#include "ranker_types.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kReplayPacketBytes = 0x24;
constexpr u32 kReplayPacketRingSlots = 0x800;
constexpr u32 kReplayPacketScratchBytes = kReplayPacketBytes * kReplayPacketRingSlots;
constexpr u32 kReplayChannelCount = 8;
constexpr u32 kReplayHeaderBytes = 0x20ff;
constexpr u32 kReplayViewportRecordCount = 400;
constexpr u32 kReplayViewportRecordBytes = 8;
constexpr u32 kReplayViewportScratchBytes =
    kReplayViewportRecordCount * kReplayViewportRecordBytes;

struct ReplayViewportRecord {
    u32 frame_tick = 0;
    i16 camera_x = 0;
    i16 camera_y = 0;
};

struct ReplayRecordingState {
    bool playback_mode = false;
    bool packet_temp_open = false;
    bool viewport_temp_open = false;
    std::string packet_temp_path = "Replay.tmp";
    std::string viewport_temp_path = "Replayvpo.tmp";
    std::string source_archive_path;
    std::string last_output_path;
    std::string playback_archive_path;
    std::array<u8, kReplayHeaderBytes> header{};
    std::array<u8, kReplayPacketScratchBytes> packet_scratch{};
    std::vector<std::array<u8, kReplayPacketBytes>> packet_records;
    std::array<u32, kReplayChannelCount> first_packet_by_channel{};
    u32 packet_count = 0;
    u32 playback_last_frame_tick = 0;
    u32 packet_flush_count = 0;
    std::vector<u8> playback_payload;
    std::array<ReplayViewportRecord, kReplayViewportRecordCount> viewport_scratch{};
    std::vector<ReplayViewportRecord> viewport_records;
    u32 viewport_count = 0;
    u32 viewport_total_count = 0;
    u32 viewport_flush_count = 0;
    u32 viewport_first_frame = 0xffffffffu;
    u32 viewport_last_frame = 0xffffffffu;
    i16 viewport_last_camera_x = 0;
    i16 viewport_last_camera_y = 0;
    bool viewport_has_last_camera = false;
    bool scenario_ai_profile_override = false;
};

ReplayRecordingState& replay_recording_state();

void InitializeReplayTempFiles(ReplayRecordingState& state,
    bool playback_mode = false, u32 game_version = 0, u8 reliable_mode = 0,
    bool forced_replay_mode = false, u8 local_player = 0,
    const std::vector<u8>& metadata = {},
    bool scenario_ai_profile_override = false);
bool FinalizeReplayRecordingDisabled(
    ReplayRecordingState& state, const char* output_path);
bool AppendReplayPacketRecord(ReplayRecordingState& state, const void* packet,
    u32 packet_size, u32 frame_tick);
bool AppendReplayViewportRecord(ReplayRecordingState& state, u32 frame_tick,
    i32 camera_x, i32 camera_y);

} // namespace ranker
