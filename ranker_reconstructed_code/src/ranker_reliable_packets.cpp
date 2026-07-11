#include "ranker_reliable_packets.h"
#include "ranker_gameplay_packets.h"
#include "ranker_replay.h"

#include <algorithm>
#include <cstring>

namespace ranker {
namespace {

Mode1ReliableRuntimeState g_mode1_reliable_state;
constexpr u32 kMode1WrappedCommandHeaderBytes = 0x0d;
constexpr u32 kMode1WrappedCommandSubtype = 0x2a;
constexpr u8 kMode1WrappedCommandDebugPad = 0xcc;

u32 read_u32(const void* data, u32 size, u32 offset) {
    if (data == nullptr || offset > size || size - offset < sizeof(u32)) {
        return 0;
    }

    u32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(data) + offset, sizeof(value));
    return value;
}

u8 read_u8(const void* data, u32 size, u32 offset) {
    if (data == nullptr || offset >= size) {
        return 0;
    }
    return *(static_cast<const u8*>(data) + offset);
}

void write_u32(void* data, u32 offset, u32 value) {
    std::memcpy(static_cast<u8*>(data) + offset, &value, sizeof(value));
}

void write_vector_u32(std::vector<u8>& data, u32 offset, u32 value) {
    if (offset > data.size() || data.size() - offset < sizeof(value)) {
        return;
    }
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

void invoke_packet_hook(Mode1ReliablePacketHook hook, const Mode1ReliablePacket& packet) {
    if (hook != nullptr) {
        hook(packet, g_mode1_reliable_state.callback_user_data);
    }
}

void invoke_range_hook(Mode1ReliableRangeHook hook, u32 channel, u32 start_sequence,
    u32 end_sequence, u32 target_player) {
    if (hook != nullptr) {
        hook(channel, start_sequence, end_sequence, target_player,
            g_mode1_reliable_state.callback_user_data);
    }
}

void invoke_simple_hook(Mode1ReliableSimpleHook hook) {
    if (hook != nullptr) {
        hook(g_mode1_reliable_state.callback_user_data);
    }
}

bool is_active_player(u32 player) {
    if (player >= kMode1ReliableChannelCount) {
        return false;
    }
    const u8 status = g_mode1_reliable_state.player_status[player];
    return status != 1 && status != 0x14;
}

i32 remember_send_status(i32 status) {
    g_mode1_reliable_state.last_send_status = status;
    g_mode1_reliable_state.last_send_succeeded = status >= 0;
    return status;
}

i32 send_to_player(const void* data, u32 byte_count, u32 target_player) {
    if (g_mode1_reliable_state.callbacks.send_to_player == nullptr) {
        return remember_send_status(-1);
    }

    const i32 status = g_mode1_reliable_state.callbacks.send_to_player(
        data, byte_count, target_player, g_mode1_reliable_state.callback_user_data);
    return remember_send_status(status);
}

Mode1ReliablePacket make_packet_view(const Mode1ReliableChannelState& channel,
    u32 sequence) {
    const u32 slot = sequence & (kMode1ReliableWindowSlots - 1);
    Mode1ReliablePacket packet{};
    packet.bytes = channel.packet_bytes[slot];
    packet.size = channel.packet_sizes[slot];
    packet.sequence = read_u32(packet.bytes.data(), kMode1ReliablePacketBytes, 8);
    packet.channel = read_u8(packet.bytes.data(), kMode1ReliablePacketBytes, 12);
    packet.subtype = read_u8(packet.bytes.data(), kMode1ReliablePacketBytes, 15);
    return packet;
}

void build_control_packet(std::array<u8, kMode1ReliablePacketBytes>& packet,
    u8 channel, u8 subtype, u32 start_sequence, u32 sender_slot, u32 end_sequence) {
    packet.fill(0);
    write_u32(packet.data(), 0, 1);
    write_u32(packet.data(), 4, kMode1ReliablePacketBytes);
    write_u32(packet.data(), 8, start_sequence);
    packet[12] = channel;
    packet[15] = subtype;
    write_u32(packet.data(), 16, sender_slot);
    write_u32(packet.data(), 20, end_sequence);
}

void seed_initial_channel_packets(u32 channel, u32 initial_sequence_count) {
    for (u32 sequence = 0; sequence < initial_sequence_count; ++sequence) {
        std::array<u8, kMode1ReliablePacketBytes> packet{};
        build_control_packet(packet, static_cast<u8>(channel), 0x10, sequence,
            sequence + 1, 0);
        AcceptMode1OrderedPacket(packet.data(), static_cast<u32>(packet.size()));
    }
}

}

Mode1ReliableRuntimeState& mode1_reliable_state() {
    return g_mode1_reliable_state;
}

void ClearMode1ReliablePacketRings() {
    for (Mode1ReliableChannelState& channel : g_mode1_reliable_state.channels) {
        channel = Mode1ReliableChannelState{};
    }
    g_mode1_reliable_state.read_sequences.fill(0);
}

void ResetMode1ReliableRuntimeAndReplayState(bool scenario_ai_profile_override,
    u32 game_version, u8 reliable_mode, bool forced_replay_mode, u8 local_player,
    const std::vector<u8>& replay_metadata) {
    ClearMode1ReliablePacketRings();
    g_mode1_reliable_state.runtime_reset_counters.fill(0);
    g_mode1_reliable_state.missing_range_request_counts.fill(0);
    ReplayRecordingState& replay = replay_recording_state();
    if (!replay.playback_mode) {
        InitializeReplayTempFiles(replay, false, game_version, reliable_mode,
            forced_replay_mode, local_player, replay_metadata,
            scenario_ai_profile_override);
    }
}

void InitializeMode1ReliableSyncRuntime(u32 now_ms) {
    g_mode1_reliable_state.current_time_ms = now_ms;
    g_mode1_reliable_state.sync_processed_mask = 0;
    g_mode1_reliable_state.local_broadcast_gate_open = true;
    g_mode1_reliable_state.corrective_packet_pending = false;
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        g_mode1_reliable_state.last_progress_time[channel] = now_ms;
        g_mode1_reliable_state.wait_budget[channel] = 25000;
        g_mode1_reliable_state.sync_consumed_flags[channel] = 0;
        g_mode1_reliable_state.sync_recovery_budget[channel] = 4;
    }
}

void ResetMode1ReliablePacketState(u32 initial_sequence_count) {
    const Mode1ReliableCallbacks callbacks = g_mode1_reliable_state.callbacks;
    void* const user_data = g_mode1_reliable_state.callback_user_data;
    const u32 local_player = g_mode1_reliable_state.local_player_index;
    const u32 replay_frame_tick = g_mode1_reliable_state.replay_frame_tick;
    const bool collect_subtype10 = g_mode1_reliable_state.subtype10_collection_enabled;

    g_mode1_reliable_state = Mode1ReliableRuntimeState{};
    g_mode1_reliable_state.callbacks = callbacks;
    g_mode1_reliable_state.callback_user_data = user_data;
    g_mode1_reliable_state.local_player_index =
        local_player < kMode1ReliableChannelCount ? local_player : 0;
    g_mode1_reliable_state.initial_sequence_count = initial_sequence_count;
    g_mode1_reliable_state.local_broadcast_start = initial_sequence_count;
    g_mode1_reliable_state.local_broadcast_end = initial_sequence_count;
    g_mode1_reliable_state.replay_frame_tick = replay_frame_tick;
    g_mode1_reliable_state.sync_threshold = initial_sequence_count >> 1;
    g_mode1_reliable_state.subtype10_collection_enabled = collect_subtype10;
    g_mode1_reliable_state.initialized = true;

    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        seed_initial_channel_packets(channel, initial_sequence_count);
    }
}

void SetMode1ReliableCallbacks(const Mode1ReliableCallbacks& callbacks, void* user_data) {
    g_mode1_reliable_state.callbacks = callbacks;
    g_mode1_reliable_state.callback_user_data = user_data;
}

void SetMode1ReliableLocalPlayerIndex(u32 local_player_index) {
    if (local_player_index < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.local_player_index = local_player_index;
    }
}

void SetMode1ReliableSubtype10CollectionEnabled(bool enabled) {
    g_mode1_reliable_state.subtype10_collection_enabled = enabled;
}

void SetMode1ReliablePlayerStatus(u32 player, u8 status) {
    if (player < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.player_status[player] = status;
    }
}

void SetMode1ReliableReplayFrameTick(u32 frame_tick) {
    g_mode1_reliable_state.replay_frame_tick = frame_tick;
}

void SetMode1ReliableExpectedSequence(u32 channel, u32 sequence) {
    if (channel < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.channels[channel].expected_sequence = sequence;
    }
}

u32 GetMode1ReliableExpectedSequence(u32 channel) {
    if (channel >= kMode1ReliableChannelCount) {
        return 0;
    }
    return g_mode1_reliable_state.channels[channel].expected_sequence;
}

void ClearMode1ReliableMissingRangeRequest(u32 channel) {
    if (channel < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.channels[channel].missing_range_requested = false;
    }
}

void MarkMode1ReliableLocalBroadcastEnd(u32 end_sequence) {
    g_mode1_reliable_state.local_broadcast_end = end_sequence;
}

void CollectMode1Subtype10Value(const Mode1ReliablePacket& packet) {
    if (!g_mode1_reliable_state.subtype10_collection_enabled ||
        packet.subtype != 0x10 ||
        packet.channel >= kMode1ReliableChannelCount) {
        return;
    }

    const u32 count = g_mode1_reliable_state.subtype10_counts[packet.channel];
    if (count >= kMode1Subtype10ValueSlots) {
        return;
    }

    g_mode1_reliable_state.subtype10_values[packet.channel][count] =
        read_u32(packet.bytes.data(), kMode1ReliablePacketBytes, 20);
    g_mode1_reliable_state.subtype10_counts[packet.channel] = count + 1;
}

u32 GetMode1ReliableChecksumAuthorityChannel() {
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if (is_active_player(channel)) {
            return channel;
        }
    }
    return g_mode1_reliable_state.local_player_index;
}

bool TryGetMode1ReliableAuthoritativeSubtype10Value(
    u32 authority_channel, u32 local_channel, u32& out_value,
    u32* out_fifo_index) {
    if (authority_channel >= kMode1ReliableChannelCount ||
        local_channel >= kMode1ReliableChannelCount ||
        authority_channel == local_channel ||
        !is_active_player(authority_channel)) {
        return false;
    }

    // CollectMode1Subtype10Value and ShiftMode1Subtype10Values maintain one
    // FIFO per player.  The original comparison at 0x004296F0 compares only
    // FIFO[0], not the packet sequence or the frame field.  Before appending
    // our next heartbeat, the current local count is therefore the exact
    // ordinal at which the authority's corresponding value must be found.
    const u32 fifo_index = g_mode1_reliable_state.subtype10_counts[local_channel];
    if (fifo_index >= kMode1Subtype10ValueSlots ||
        g_mode1_reliable_state.subtype10_counts[authority_channel] <= fifo_index) {
        return false;
    }

    out_value =
        g_mode1_reliable_state.subtype10_values[authority_channel][fifo_index];
    if (out_fifo_index != nullptr) {
        *out_fifo_index = fifo_index;
    }
    return true;
}

bool AcceptMode1OrderedPacket(const void* packet, u32 packet_size) {
    if (packet == nullptr || packet_size < 16) {
        return false;
    }

    const u32 channel_index = read_u8(packet, packet_size, 12);
    if (channel_index >= kMode1ReliableChannelCount) {
        return false;
    }

    auto& channel = g_mode1_reliable_state.channels[channel_index];
    const u32 sequence = read_u32(packet, packet_size, 8);
    const u32 slot = sequence & (kMode1ReliableWindowSlots - 1);
    const u32 copy_size = std::min<u32>(packet_size, kMode1ReliablePacketBytes);
    channel.packet_bytes[slot].fill(0);
    std::memcpy(channel.packet_bytes[slot].data(), packet, copy_size);
    channel.packet_sizes[slot] = copy_size;
    channel.packet_flags[slot] |= 1;

    u32 current_sequence = sequence;
    while (channel.expected_sequence == current_sequence) {
        Mode1ReliablePacket advanced = make_packet_view(channel, current_sequence);
        channel.expected_sequence = current_sequence + 1;
        CollectMode1Subtype10Value(advanced);
        invoke_packet_hook(g_mode1_reliable_state.callbacks.packet_advanced, advanced);

        const u32 next_sequence = current_sequence + 1;
        const u32 next_slot = next_sequence & (kMode1ReliableWindowSlots - 1);
        if ((channel.packet_flags[next_slot] & 1) == 0) {
            break;
        }

        current_sequence = read_u32(
            channel.packet_bytes[next_slot].data(), kMode1ReliablePacketBytes, 8);
        channel.missing_range_requested = false;
    }

    if (channel.expected_sequence < sequence && !channel.missing_range_requested) {
        channel.missing_range_requested = true;
        RequestMode1MissingRange(channel_index, channel.expected_sequence, sequence - 1);
    }
    return true;
}

bool PopMode1OrderedPacket(u32 channel_index, Mode1ReliablePacket& out_packet) {
    if (channel_index >= kMode1ReliableChannelCount) {
        return false;
    }

    auto& channel = g_mode1_reliable_state.channels[channel_index];
    u32& read_sequence = g_mode1_reliable_state.read_sequences[channel_index];
    if (read_sequence >= channel.expected_sequence) {
        return false;
    }

    const u32 slot = read_sequence & (kMode1ReliableWindowSlots - 1);
    if ((channel.packet_flags[slot] & 1) == 0) {
        RequestMode1MissingRange(channel_index, read_sequence,
            read_sequence + g_mode1_reliable_state.initial_sequence_count);
        return false;
    }

    out_packet = make_packet_view(channel, read_sequence);
    AppendReplayPacketRecord(replay_recording_state(), out_packet.bytes.data(),
        kMode1ReliablePacketBytes, g_mode1_reliable_state.replay_frame_tick);
    channel.packet_flags[slot] &= ~1u;
    ++read_sequence;
    return true;
}

void ShiftMode1Subtype10Values() {
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if (!is_active_player(channel)) {
            continue;
        }

        u32& count = g_mode1_reliable_state.subtype10_counts[channel];
        if (count == 0) {
            continue;
        }

        for (u32 index = 1; index < count; ++index) {
            g_mode1_reliable_state.subtype10_values[channel][index - 1] =
                g_mode1_reliable_state.subtype10_values[channel][index];
        }
        --count;
    }
}

void ApplyMode1SyncTimeoutPenalty() {
    const u32 now_ms = g_mode1_reliable_state.current_time_ms;
    const u32 threshold = g_mode1_reliable_state.sync_threshold;
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if (is_active_player(channel) &&
            g_mode1_reliable_state.subtype10_counts[channel] < threshold) {
            const u32 elapsed =
                now_ms - g_mode1_reliable_state.last_progress_time[channel];
            g_mode1_reliable_state.wait_budget[channel] =
                elapsed < g_mode1_reliable_state.wait_budget[channel]
                    ? g_mode1_reliable_state.wait_budget[channel] - elapsed
                    : 0;
            g_mode1_reliable_state.last_progress_time[channel] = now_ms;
        }
    }
    invoke_simple_hook(g_mode1_reliable_state.callbacks.sync_timeout);
}

bool CheckMode1ReliableSync(u32 now_ms) {
    g_mode1_reliable_state.current_time_ms = now_ms;
    invoke_simple_hook(g_mode1_reliable_state.callbacks.poll_transport_receive);
    if (g_mode1_reliable_state.sync_gate_open) {
        g_mode1_reliable_state.sync_start_time = now_ms;
        for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
            g_mode1_reliable_state.last_missing_request_time[channel] = now_ms;
        }
        g_mode1_reliable_state.sync_gate_open = false;
    }

    bool waiting = false;
    const u32 threshold = g_mode1_reliable_state.sync_threshold;
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if (!is_active_player(channel)) {
            g_mode1_reliable_state.last_progress_time[channel] =
                g_mode1_reliable_state.sync_start_time;
            continue;
        }

        if (threshold <= g_mode1_reliable_state.subtype10_counts[channel]) {
            g_mode1_reliable_state.last_progress_time[channel] =
                g_mode1_reliable_state.sync_start_time;
            continue;
        }

        waiting = true;
        if (g_mode1_reliable_state.last_missing_request_time[channel] + 700U < now_ms) {
            RequestMode1MissingRange(channel,
                g_mode1_reliable_state.channels[channel].expected_sequence, 0xffffffffu);
            g_mode1_reliable_state.last_missing_request_time[channel] = now_ms;
        }
    }

    if (!waiting) {
        invoke_simple_hook(g_mode1_reliable_state.callbacks.sync_ready);
        g_mode1_reliable_state.sync_gate_open = true;
        return true;
    }

    if (1000U < now_ms - g_mode1_reliable_state.sync_start_time) {
        ApplyMode1SyncTimeoutPenalty();
    }
    return false;
}

u32 PumpMode1ReliablePackets(
    bool generic_ai_profile_mode, bool scenario_ai_profile_override, u32 now_ms) {
    g_mode1_reliable_state.current_time_ms = now_ms;
    u32 consumed = 0;
    if (scenario_ai_profile_override) {
        return 0;
    }
    if (!generic_ai_profile_mode) {
        if (g_mode1_reliable_state.local_player_index >= kMode1ReliableChannelCount) {
            return 0;
        }
        invoke_simple_hook(g_mode1_reliable_state.callbacks.snapshot_local_checksum);
        g_mode1_reliable_state.sync_consumed_flags[g_mode1_reliable_state.local_player_index] = 0;
        Mode1ReliablePacket packet{};
        while (g_mode1_reliable_state
                   .sync_consumed_flags[g_mode1_reliable_state.local_player_index] == 0 &&
               PopMode1OrderedPacket(g_mode1_reliable_state.local_player_index, packet)) {
            DispatchMode1GameplayPacket(packet);
            invoke_packet_hook(g_mode1_reliable_state.callbacks.packet_consumed, packet);
            ++consumed;
        }
        return consumed;
    }

    if (g_mode1_reliable_state.local_broadcast_gate_open) {
        g_mode1_reliable_state.compatibility_checksum_snapshot_deferred = false;
        invoke_simple_hook(g_mode1_reliable_state.callbacks.snapshot_local_checksum);
        if (g_mode1_reliable_state.local_broadcast_start <
            g_mode1_reliable_state.local_broadcast_end) {
            BroadcastMode1PacketRange(g_mode1_reliable_state.local_broadcast_start,
                g_mode1_reliable_state.local_broadcast_end - 1);
            g_mode1_reliable_state.local_broadcast_start =
                g_mode1_reliable_state.local_broadcast_end;
        }
        // A compatibility client can defer its heartbeat while the
        // authoritative peer's matching FIFO value is still in flight.  It
        // may still have published other packets above, so flush those, then
        // leave this gate open to retry the checksum on the next pump.
        g_mode1_reliable_state.local_broadcast_gate_open =
            g_mode1_reliable_state.compatibility_checksum_snapshot_deferred;
    }

    if (!CheckMode1ReliableSync(now_ms)) {
        return 0;
    }

    g_mode1_reliable_state.sync_consumed_flags.fill(0);
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if (!is_active_player(channel)) {
            continue;
        }

        while (g_mode1_reliable_state.sync_consumed_flags[channel] == 0 &&
               is_active_player(channel)) {
            Mode1ReliablePacket packet{};
            if (!PopMode1OrderedPacket(channel, packet)) {
                break;
            }
            DispatchMode1GameplayPacket(packet);
            invoke_packet_hook(g_mode1_reliable_state.callbacks.packet_consumed, packet);
            ++consumed;
        }
    }

    ShiftMode1Subtype10Values();
    g_mode1_reliable_state.sync_gate_open = true;
    g_mode1_reliable_state.local_broadcast_gate_open = true;
    return consumed;
}

void RequestMode1MissingRange(u32 channel, u32 start_sequence, u32 end_sequence) {
    if (channel >= kMode1ReliableChannelCount) {
        remember_send_status(-1);
        return;
    }

    std::array<u8, kMode1ReliablePacketBytes> packet{};
    build_control_packet(packet, static_cast<u8>(channel), 0x17, start_sequence,
        g_mode1_reliable_state.local_player_index, end_sequence);
    send_to_player(packet.data(), kMode1ReliablePacketBytes, channel);
    invoke_range_hook(g_mode1_reliable_state.callbacks.missing_range_requested, channel,
        start_sequence, end_sequence, channel);
}

i32 SendMode1GapAck(u32 target_player) {
    std::array<u8, kMode1ReliablePacketBytes> packet{};
    build_control_packet(packet,
        static_cast<u8>(g_mode1_reliable_state.local_player_index), 0x1c, 0, 0, 0);
    const i32 status =
        send_to_player(packet.data(), kMode1ReliablePacketBytes, target_player);
    invoke_range_hook(g_mode1_reliable_state.callbacks.gap_ack_sent,
        g_mode1_reliable_state.local_player_index, 0, 0, target_player);
    return status;
}

void ResendMode1PacketRange(u32 start_sequence, u32 end_sequence, u32 target_player) {
    if (g_mode1_reliable_state.local_player_index >= kMode1ReliableChannelCount) {
        remember_send_status(-1);
        return;
    }

    const auto& channel =
        g_mode1_reliable_state.channels[g_mode1_reliable_state.local_player_index];
    const u32 start_slot = start_sequence & (kMode1ReliableWindowSlots - 1);
    const u32 end_slot = end_sequence & (kMode1ReliableWindowSlots - 1);

    i32 status = 0;
    if (end_slot < start_slot) {
        const u32 first_count = kMode1ReliableWindowSlots - start_slot;
        status = send_to_player(channel.packet_bytes[start_slot].data(),
            first_count * kMode1ReliablePacketBytes, target_player);
        if (status >= 0) {
            status = send_to_player(channel.packet_bytes[0].data(),
                (end_slot + 1) * kMode1ReliablePacketBytes, target_player);
        }
    }
    else {
        status = send_to_player(channel.packet_bytes[start_slot].data(),
            (end_slot + 1 - start_slot) * kMode1ReliablePacketBytes, target_player);
    }

    invoke_range_hook(g_mode1_reliable_state.callbacks.range_resent,
        g_mode1_reliable_state.local_player_index, start_sequence, end_sequence,
        target_player);
}

void BroadcastMode1PacketRange(u32 start_sequence, u32 end_sequence) {
    if (g_mode1_reliable_state.local_player_index >= kMode1ReliableChannelCount ||
        g_mode1_reliable_state.callbacks.broadcast == nullptr) {
        remember_send_status(-1);
        return;
    }

    const auto& channel =
        g_mode1_reliable_state.channels[g_mode1_reliable_state.local_player_index];
    const u32 start_slot = start_sequence & (kMode1ReliableWindowSlots - 1);
    const u32 end_slot = end_sequence & (kMode1ReliableWindowSlots - 1);

    i32 status = 0;
    if (end_slot < start_slot) {
        const u32 first_count = kMode1ReliableWindowSlots - start_slot;
        status = g_mode1_reliable_state.callbacks.broadcast(
            channel.packet_bytes[start_slot].data(), first_count * kMode1ReliablePacketBytes,
            0, g_mode1_reliable_state.callback_user_data);
        if (status >= 0) {
            status = g_mode1_reliable_state.callbacks.broadcast(
                channel.packet_bytes[0].data(), (end_slot + 1) * kMode1ReliablePacketBytes,
                0, g_mode1_reliable_state.callback_user_data);
        }
    }
    else {
        status = g_mode1_reliable_state.callbacks.broadcast(
            channel.packet_bytes[start_slot].data(),
            (end_slot + 1 - start_slot) * kMode1ReliablePacketBytes, 0,
            g_mode1_reliable_state.callback_user_data);
    }

    remember_send_status(status);
    invoke_range_hook(g_mode1_reliable_state.callbacks.range_broadcast,
        g_mode1_reliable_state.local_player_index, start_sequence, end_sequence, 0);
}

i32 BroadcastMode1ReliablePayload(const void* packet, u32 packet_size) {
    if (g_mode1_reliable_state.callbacks.broadcast == nullptr) {
        return remember_send_status(-1);
    }

    const i32 status = g_mode1_reliable_state.callbacks.broadcast(
        packet, packet_size, 0, g_mode1_reliable_state.callback_user_data);
    return remember_send_status(status);
}

i32 BroadcastMode1ReliablePayloadToAll(const void* packet, u32 packet_size) {
    return BroadcastMode1ReliablePayload(packet, packet_size);
}

std::vector<u8> BuildMode1WrappedCommandPacket(const void* payload, u32 payload_size) {
    if (payload == nullptr || payload_size < sizeof(u32)) {
        return {};
    }

    const u32 copied_payload_size = payload_size - sizeof(u32);
    const u32 wrapped_size = copied_payload_size + kMode1WrappedCommandHeaderBytes;
    std::vector<u8> wrapped(wrapped_size, 0);
    write_vector_u32(wrapped, 0, 0);
    write_vector_u32(wrapped, 4, kMode1WrappedCommandSubtype);
    write_vector_u32(wrapped, 8, wrapped_size);
    wrapped[12] = kMode1WrappedCommandDebugPad;

    const auto* bytes = static_cast<const u8*>(payload);
    std::copy_n(bytes + sizeof(u32), copied_payload_size,
        wrapped.begin() + kMode1WrappedCommandHeaderBytes);
    return wrapped;
}

bool WrapAndPublishMode1SlashCommandPacket(const void* payload, u32 payload_size) {
    std::vector<u8> wrapped = BuildMode1WrappedCommandPacket(payload, payload_size);
    if (wrapped.empty()) {
        return false;
    }

    g_mode1_reliable_state.last_wrapped_command_packet = std::move(wrapped);
    if (g_mode1_reliable_state.callbacks.wrapped_packet_published != nullptr) {
        g_mode1_reliable_state.callbacks.wrapped_packet_published(
            g_mode1_reliable_state.last_wrapped_command_packet.data(),
            static_cast<u32>(g_mode1_reliable_state.last_wrapped_command_packet.size()),
            g_mode1_reliable_state.callback_user_data);
    }
    return true;
}

}
