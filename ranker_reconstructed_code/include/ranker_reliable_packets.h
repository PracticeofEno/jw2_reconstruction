#pragma once

#include "ranker_types.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ranker {

constexpr u32 kMode1ReliableChannelCount = 8;
constexpr u32 kMode1ReliableWindowSlots = 0x800;
constexpr u32 kMode1ReliablePacketBytes = 0x24;
constexpr u32 kMode1Subtype10ValueSlots = 0x40;
constexpr u32 kMode1ChatColorWhite = 0x00ffffffu;
constexpr u32 kMode1WrappedCommandHeaderBytes = 0x0du;
constexpr u32 kMode1WrappedCommandSubtype = 0x2au;
constexpr u8 kMode1WrappedCommandDebugPad = 0xccu;

// FUN_004e7090 publishes ordinary in-game chat as a raw Mode-1 type-zero
// payload.  It is deliberately separate from the fixed 0x24-byte ordered
// gameplay packet stream.
struct Mode1ChatPayload {
    std::string primary_text;
    std::string secondary_text;
    u32 primary_color_ref = kMode1ChatColorWhite;
    u32 secondary_color_ref = kMode1ChatColorWhite;
};

inline std::vector<u8> BuildMode1ChatPayload(
    std::string_view sender, std::string_view text) {
    if (sender.find('\0') != std::string_view::npos ||
        text.find('\0') != std::string_view::npos ||
        sender.size() + 3u > 0xffu || text.size() + 1u > 0xffu) {
        return {};
    }

    const u8 primary_length = static_cast<u8>(sender.size() + 3u);
    const u8 secondary_length = static_cast<u8>(text.size() + 1u);
    std::vector<u8> packet(
        static_cast<std::size_t>(sender.size() + text.size() + 16u), 0);
    packet[4] = 0xff;
    packet[5] = 0xff;
    packet[6] = 0xff;
    packet[7] = primary_length;
    std::size_t cursor = 8;
    for (char value : sender) {
        packet[cursor++] = static_cast<u8>(value);
    }
    packet[cursor++] = static_cast<u8>('>');
    packet[cursor++] = static_cast<u8>(' ');
    packet[cursor++] = 0;
    packet[cursor++] = 0xff;
    packet[cursor++] = 0xff;
    packet[cursor++] = 0xff;
    packet[cursor++] = secondary_length;
    for (char value : text) {
        packet[cursor++] = static_cast<u8>(value);
    }
    packet[cursor] = 0;
    return packet;
}

inline bool ParseMode1ChatPayload(
    const void* data, u32 byte_count, Mode1ChatPayload& out_payload) {
    if (data == nullptr || byte_count < 16u) {
        return false;
    }
    const auto* bytes = static_cast<const u8*>(data);
    if (bytes[0] != 0 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0) {
        return false;
    }

    const u32 primary_length = bytes[7];
    if (primary_length == 0 || 8u + primary_length + 4u > byte_count ||
        bytes[8u + primary_length - 1u] != 0) {
        return false;
    }
    const u32 secondary_header = 8u + primary_length;
    const u32 secondary_length = bytes[secondary_header + 3u];
    const u32 secondary_text = secondary_header + 4u;
    if (secondary_length == 0 || secondary_text + secondary_length != byte_count ||
        bytes[secondary_text + secondary_length - 1u] != 0) {
        return false;
    }

    Mode1ChatPayload parsed{};
    parsed.primary_color_ref = static_cast<u32>(bytes[4]) |
        (static_cast<u32>(bytes[5]) << 8u) |
        (static_cast<u32>(bytes[6]) << 16u);
    parsed.secondary_color_ref = static_cast<u32>(bytes[secondary_header]) |
        (static_cast<u32>(bytes[secondary_header + 1u]) << 8u) |
        (static_cast<u32>(bytes[secondary_header + 2u]) << 16u);
    parsed.primary_text.assign(reinterpret_cast<const char*>(bytes + 8u),
        primary_length - 1u);
    parsed.secondary_text.assign(
        reinterpret_cast<const char*>(bytes + secondary_text),
        secondary_length - 1u);
    out_payload = std::move(parsed);
    return true;
}

constexpr u32 ResolveMode1ChatRecipientMask(
    u32 channel, u32 custom_mask, u32 relation_mask) {
    return channel == 0u ? (custom_mask & 0xffu) :
        channel == 1u ? (relation_mask & 0xffu) :
        channel == 2u ? ((~relation_mask) & 0xffu) : 0u;
}

constexpr u32 ResolveMode1DefaultChatChannel(bool local_player_is_observer) {
    return local_player_is_observer ? 0u : 3u;
}

inline std::vector<u8> BuildMode1WrappedCommandPacket(
    const void* payload, u32 payload_size) {
    if (payload == nullptr || payload_size < sizeof(u32) ||
        payload_size > 0xffffffffu -
            (kMode1WrappedCommandHeaderBytes - sizeof(u32))) {
        return {};
    }

    const u32 copied_payload_size = payload_size - sizeof(u32);
    const u32 wrapped_size = copied_payload_size +
        kMode1WrappedCommandHeaderBytes;
    std::vector<u8> wrapped(wrapped_size, 0);
    const auto write_le32 = [&wrapped](u32 offset, u32 value) {
        wrapped[offset] = static_cast<u8>(value);
        wrapped[offset + 1u] = static_cast<u8>(value >> 8u);
        wrapped[offset + 2u] = static_cast<u8>(value >> 16u);
        wrapped[offset + 3u] = static_cast<u8>(value >> 24u);
    };
    write_le32(0, 0);
    write_le32(4, kMode1WrappedCommandSubtype);
    write_le32(8, wrapped_size);
    wrapped[12] = kMode1WrappedCommandDebugPad;
    const auto* source = static_cast<const u8*>(payload) + sizeof(u32);
    for (u32 index = 0; index < copied_payload_size; ++index) {
        wrapped[kMode1WrappedCommandHeaderBytes + index] = source[index];
    }
    return wrapped;
}

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
    // Active participants are snapshotted when a synchronized pump round
    // opens.  The mask can only shrink while that round is pending; a player
    // that becomes active midway joins the following round instead.
    u32 sync_round_required_mask = 0;
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
    // Compatibility heartbeats may have to wait until the authoritative
    // peer's checksum with the same FIFO ordinal has arrived.  Keep the
    // snapshot gate open in that case instead of publishing a speculative
    // value that the original executable would treat as a desync.
    bool compatibility_checksum_snapshot_deferred = false;
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
u32 GetMode1ReliableChecksumAuthorityChannel();
bool TryGetMode1ReliableAuthoritativeSubtype10Value(
    u32 authority_channel, u32 local_channel, u32& out_value,
    u32* out_fifo_index = nullptr);

bool AcceptMode1OrderedPacket(const void* packet, u32 packet_size);
bool PopMode1OrderedPacket(u32 channel, Mode1ReliablePacket& out_packet);
void ShiftMode1Subtype10Values();
void ApplyMode1SyncTimeoutPenalty();
bool CheckMode1ReliableSync(u32 now_ms);
bool IsMode1ReliableSyncRoundPending();
u32 PumpMode1ReliablePackets(
    bool generic_ai_profile_mode, bool scenario_ai_profile_override, u32 now_ms);
void RequestMode1MissingRange(u32 channel, u32 start_sequence, u32 end_sequence);
i32 SendMode1GapAck(u32 target_player);
void ResendMode1PacketRange(u32 start_sequence, u32 end_sequence, u32 target_player);
void BroadcastMode1PacketRange(u32 start_sequence, u32 end_sequence);
i32 BroadcastMode1ReliablePayload(const void* packet, u32 packet_size);
i32 BroadcastMode1ReliablePayloadToAll(const void* packet, u32 packet_size);
bool WrapAndPublishMode1SlashCommandPacket(const void* payload, u32 payload_size);

}
