#pragma once

#include "ranker_types.h"

#include <array>
#include <vector>

namespace ranker {

constexpr u32 kMode1ReliableChannelCount = 8;
constexpr u32 kMode1ReliableWindowSlots = 0x800;
constexpr u32 kMode1ReliablePacketBytes = 0x24;
constexpr u32 kMode1Subtype10ValueSlots = 0x40;

struct Mode1ReliablePacket {
    std::array<u8, kMode1ReliablePacketBytes> bytes{};
    u32 size = 0;
    u32 sequence = 0;
    u8 channel = 0;
    u8 subtype = 0;
};

struct Mode1ReliableChannelState {
    std::array<std::array<u8, kMode1ReliablePacketBytes>, kMode1ReliableWindowSlots>
        packet_bytes{};
    std::array<u32, kMode1ReliableWindowSlots> packet_sizes{};
    std::array<u32, kMode1ReliableWindowSlots> packet_flags{};
    u32 expected_sequence = 0;
    bool missing_range_requested = false;
};

using Mode1ReliablePacketHook = void (*)(const Mode1ReliablePacket& packet, void* user_data);
using Mode1ReliableRangeHook = void (*)(u32 channel, u32 start_sequence,
    u32 end_sequence, u32 target_player, void* user_data);
using Mode1ReliableSendCallback = i32 (*)(const void* data, u32 byte_count,
    u32 target_player, void* user_data);
using Mode1ReliableSimpleHook = void (*)(void* user_data);
using Mode1ReliablePayloadHook = void (*)(const void* data, u32 byte_count,
    void* user_data);

struct Mode1ReliableCallbacks {
    Mode1ReliablePacketHook packet_advanced = nullptr;
    Mode1ReliablePacketHook packet_consumed = nullptr;
    Mode1ReliableRangeHook missing_range_requested = nullptr;
    Mode1ReliableRangeHook range_resent = nullptr;
    Mode1ReliableRangeHook range_broadcast = nullptr;
    Mode1ReliableRangeHook gap_ack_sent = nullptr;
    Mode1ReliableSimpleHook poll_transport_receive = nullptr;
    Mode1ReliableSimpleHook snapshot_local_checksum = nullptr;
    Mode1ReliableSimpleHook sync_ready = nullptr;
    Mode1ReliableSimpleHook sync_timeout = nullptr;
    Mode1ReliablePayloadHook wrapped_packet_published = nullptr;
    Mode1ReliableSendCallback send_to_player = nullptr;
    Mode1ReliableSendCallback broadcast = nullptr;
};

struct Mode1ReliableRuntimeState {
    std::array<Mode1ReliableChannelState, kMode1ReliableChannelCount> channels{};
    std::array<std::array<u32, kMode1Subtype10ValueSlots>, kMode1ReliableChannelCount>
        subtype10_values{};
    std::array<u32, kMode1ReliableChannelCount> subtype10_counts{};
    std::array<u32, kMode1ReliableChannelCount> read_sequences{};
    std::array<u32, kMode1ReliableChannelCount> last_missing_request_time{};
    std::array<u32, kMode1ReliableChannelCount> last_progress_time{};
    std::array<u32, kMode1ReliableChannelCount> wait_budget{};
    std::array<u32, kMode1ReliableChannelCount> runtime_reset_counters{};
    std::array<u32, kMode1ReliableChannelCount> missing_range_request_counts{};
    std::array<u32, kMode1ReliableChannelCount> sync_consumed_flags{};
    std::array<u32, kMode1ReliableChannelCount> sync_recovery_budget{};
    std::array<u8, kMode1ReliableChannelCount> player_status{};
    std::vector<u8> last_wrapped_command_packet;
    u32 sync_processed_mask = 0;
    u32 local_player_index = 0;
    u32 initial_sequence_count = 6;
    u32 local_broadcast_start = 6;
    u32 local_broadcast_end = 6;
    u32 replay_frame_tick = 0;
    u32 current_time_ms = 0;
    u32 sync_start_time = 0;
    u32 sync_threshold = 3;
    bool subtype10_collection_enabled = true;
    bool sync_gate_open = true;
    bool local_broadcast_gate_open = false;
    bool corrective_packet_pending = false;
    bool initialized = false;
    i32 last_send_status = 0;
    bool last_send_succeeded = true;
    Mode1ReliableCallbacks callbacks{};
    void* callback_user_data = nullptr;
};

Mode1ReliableRuntimeState& mode1_reliable_state();

void ClearMode1ReliablePacketRings();
void ResetMode1ReliableRuntimeAndReplayState(
    bool scenario_ai_profile_override = false,
    u32 game_version = 0,
    u8 reliable_mode = 0,
    bool forced_replay_mode = false,
    u8 local_player = 0,
    const std::vector<u8>& replay_metadata = {});
void InitializeMode1ReliableSyncRuntime(u32 now_ms);
void ResetMode1ReliablePacketState(u32 initial_sequence_count = 6);
void SetMode1ReliableCallbacks(const Mode1ReliableCallbacks& callbacks,
    void* user_data = nullptr);
void SetMode1ReliableLocalPlayerIndex(u32 local_player_index);
void SetMode1ReliableSubtype10CollectionEnabled(bool enabled);
void SetMode1ReliablePlayerStatus(u32 player, u8 status);
void SetMode1ReliableReplayFrameTick(u32 frame_tick);
void SetMode1ReliableExpectedSequence(u32 channel, u32 sequence);
u32 GetMode1ReliableExpectedSequence(u32 channel);
void ClearMode1ReliableMissingRangeRequest(u32 channel);
void MarkMode1ReliableLocalBroadcastEnd(u32 end_sequence);
void CollectMode1Subtype10Value(const Mode1ReliablePacket& packet);

bool AcceptMode1OrderedPacket(const void* packet, u32 packet_size);
bool PopMode1OrderedPacket(u32 channel, Mode1ReliablePacket& out_packet);
void ShiftMode1Subtype10Values();
void ApplyMode1SyncTimeoutPenalty();
bool CheckMode1ReliableSync(u32 now_ms);
u32 PumpMode1ReliablePackets(
    bool generic_ai_profile_mode, bool scenario_ai_profile_override, u32 now_ms);
void RequestMode1MissingRange(u32 channel, u32 start_sequence, u32 end_sequence);
i32 SendMode1GapAck(u32 target_player);
void ResendMode1PacketRange(u32 start_sequence, u32 end_sequence, u32 target_player);
void BroadcastMode1PacketRange(u32 start_sequence, u32 end_sequence);
i32 BroadcastMode1ReliablePayload(const void* packet, u32 packet_size);
i32 BroadcastMode1ReliablePayloadToAll(const void* packet, u32 packet_size);
std::vector<u8> BuildMode1WrappedCommandPacket(const void* payload, u32 payload_size);
bool WrapAndPublishMode1SlashCommandPacket(const void* payload, u32 payload_size);

}
