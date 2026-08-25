#include "ranker_reliable_packets.h"
#include "ranker_gameplay_packets.h"
#include "ranker_replay.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>

namespace ranker {
namespace {

Mode1ReliableRuntimeState g_mode1_reliable_state;
std::recursive_mutex g_mode1_reliable_mutex;
constexpr u32 kMode1SyncRoundActive = 0x80000000u;
constexpr u32 kMode1SyncChannelMask =
    (1u << kMode1ReliableChannelCount) - 1u;

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

void invoke_packet_hook(Mode1ReliablePacketHook hook, const Mode1ReliablePacket& packet) {
    if (hook != nullptr) {
        void* user_data = nullptr;
        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            user_data = g_mode1_reliable_state.callback_user_data;
        }
        hook(packet, user_data);
    }
}

void invoke_range_hook(Mode1ReliableRangeHook hook, u32 channel, u32 start_sequence,
    u32 end_sequence, u32 target_player) {
    if (hook != nullptr) {
        void* user_data = nullptr;
        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            user_data = g_mode1_reliable_state.callback_user_data;
        }
        hook(channel, start_sequence, end_sequence, target_player, user_data);
    }
}

void invoke_simple_hook(Mode1ReliableSimpleHook hook) {
    if (hook != nullptr) {
        void* user_data = nullptr;
        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            user_data = g_mode1_reliable_state.callback_user_data;
        }
        hook(user_data);
    }
}

void poll_mode1_transport_receive() {
    Mode1ReliableSimpleHook poll_hook = nullptr;
    void* poll_user_data = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        poll_hook = g_mode1_reliable_state.callbacks.poll_transport_receive;
        poll_user_data = g_mode1_reliable_state.callback_user_data;
    }
    if (poll_hook != nullptr) {
        poll_hook(poll_user_data);
    }
}

bool is_active_player_locked(u32 player) {
    if (player >= kMode1ReliableChannelCount) {
        return false;
    }
    const u8 status = g_mode1_reliable_state.player_status[player];
    return status != 1 && status != 0x14;
}

bool is_active_player(u32 player) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    return is_active_player_locked(player);
}

u32 active_player_mask_locked() {
    u32 mask = 0;
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if (is_active_player_locked(channel)) {
            mask |= 1u << channel;
        }
    }
    return mask;
}

i32 remember_send_status(i32 status) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    g_mode1_reliable_state.last_send_status = status;
    g_mode1_reliable_state.last_send_succeeded = status >= 0;
    return status;
}

i32 send_to_player(const void* data, u32 byte_count, u32 target_player) {
    Mode1ReliableSendCallback callback = nullptr;
    void* user_data = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        callback = g_mode1_reliable_state.callbacks.send_to_player;
        user_data = g_mode1_reliable_state.callback_user_data;
    }
    if (callback == nullptr) {
        return remember_send_status(-1);
    }

    const i32 status = callback(data, byte_count, target_player, user_data);
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

void collect_mode1_subtype10_value_for_state(
    Mode1ReliableRuntimeState& state, const Mode1ReliablePacket& packet) {
    if (!state.subtype10_collection_enabled ||
        packet.subtype != 0x10 ||
        packet.channel >= kMode1ReliableChannelCount) {
        return;
    }

    const u32 count = state.subtype10_counts[packet.channel];
    if (count >= kMode1Subtype10ValueSlots) {
        return;
    }

    state.subtype10_values[packet.channel][count] =
        read_u32(packet.bytes.data(), kMode1ReliablePacketBytes, 20);
    state.subtype10_counts[packet.channel] = count + 1;
}

void collect_mode1_subtype10_value_locked(const Mode1ReliablePacket& packet) {
    collect_mode1_subtype10_value_for_state(g_mode1_reliable_state, packet);
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

std::vector<u8> copy_packet_slots_locked(
    const Mode1ReliableChannelState& channel, u32 first_slot, u32 slot_count) {
    std::vector<u8> bytes;
    bytes.reserve(static_cast<std::size_t>(slot_count) * kMode1ReliablePacketBytes);
    for (u32 index = 0; index < slot_count; ++index) {
        const auto& packet = channel.packet_bytes[first_slot + index];
        bytes.insert(bytes.end(), packet.begin(), packet.end());
    }
    return bytes;
}

void seed_initial_channel_packets_in_state(Mode1ReliableRuntimeState& state,
    u32 channel_index, u32 initial_sequence_count,
    std::vector<Mode1ReliablePacket>& advanced_packets) {
    Mode1ReliableChannelState& channel = state.channels[channel_index];
    for (u32 sequence = 0; sequence < initial_sequence_count; ++sequence) {
        std::array<u8, kMode1ReliablePacketBytes> packet{};
        build_control_packet(packet, static_cast<u8>(channel_index), 0x10, sequence,
            sequence + 1, 0);

        const u32 slot = sequence & (kMode1ReliableWindowSlots - 1);
        channel.packet_bytes[slot] = packet;
        channel.packet_sizes[slot] = kMode1ReliablePacketBytes;
        channel.packet_flags[slot] |= 1;
        channel.expected_sequence = sequence + 1;

        Mode1ReliablePacket advanced = make_packet_view(channel, sequence);
        collect_mode1_subtype10_value_for_state(state, advanced);
        advanced_packets.push_back(advanced);
    }
}

void shift_mode1_subtype10_values_locked(u32 channel_mask) {
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if ((channel_mask & (1u << channel)) == 0) {
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

void preserve_mode1_runtime_configuration(Mode1ReliableRuntimeState& target,
    const Mode1ReliableRuntimeState& source, bool preserve_player_status) {
    target.callbacks = source.callbacks;
    target.callback_user_data = source.callback_user_data;
    target.local_player_index =
        source.local_player_index < kMode1ReliableChannelCount ?
        source.local_player_index : 0;
    target.replay_frame_tick = source.replay_frame_tick;
    target.subtype10_collection_enabled = source.subtype10_collection_enabled;
    if (preserve_player_status) {
        target.player_status = source.player_status;
        target.locally_simulated_channel_mask =
            source.locally_simulated_channel_mask;
    }
}

void configure_mode1_seeded_state(Mode1ReliableRuntimeState& state,
    u32 initial_sequence_count, bool open_local_broadcast_gate,
    std::vector<Mode1ReliablePacket>& advanced_packets) {
    state.initial_sequence_count = initial_sequence_count;
    state.local_broadcast_start = initial_sequence_count;
    state.local_broadcast_end = initial_sequence_count;
    state.sync_threshold = initial_sequence_count >> 1;
    state.sync_gate_open = true;
    state.local_broadcast_gate_open = open_local_broadcast_gate;

    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        seed_initial_channel_packets_in_state(
            state, channel, initial_sequence_count, advanced_packets);
    }

    // Publish this flag only after every ring cursor and subtype FIFO has been
    // seeded.  A pump can therefore never observe threshold=3 with empty
    // queues, which was the load/reset self-deadlock.
    state.initialized = true;
}

void initialize_mode1_sync_timing_for_reseed(
    Mode1ReliableRuntimeState& state, u32 now_ms) {
    state.current_time_ms = now_ms;
    state.sync_start_time = now_ms;
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        state.last_missing_request_time[channel] = now_ms;
        state.last_progress_time[channel] = now_ms;
        state.wait_budget[channel] = 25000;
        state.sync_recovery_budget[channel] = 4;
    }
}

void invoke_seeded_packet_hooks(const std::vector<Mode1ReliablePacket>& packets,
    Mode1ReliablePacketHook hook, void* user_data) {
    if (hook == nullptr) {
        return;
    }
    for (const Mode1ReliablePacket& packet : packets) {
        hook(packet, user_data);
    }
}

}

Mode1ReliableRuntimeState& mode1_reliable_state() {
    return g_mode1_reliable_state;
}

void ClearMode1ReliablePacketRings() {
    auto fresh_state = std::make_unique<Mode1ReliableRuntimeState>();
    std::vector<Mode1ReliablePacket> advanced_packets;
    Mode1ReliablePacketHook advanced_hook = nullptr;
    void* advanced_user_data = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        const u32 initial_sequence_count =
            g_mode1_reliable_state.initial_sequence_count;
        const u32 now_ms = g_mode1_reliable_state.current_time_ms;
        preserve_mode1_runtime_configuration(
            *fresh_state, g_mode1_reliable_state, true);
        configure_mode1_seeded_state(
            *fresh_state, initial_sequence_count, true, advanced_packets);
        // A modal/load clear occurs inside a live session.  Reset the timeout
        // epoch and budgets together with the newly seeded rings so the next
        // wait check cannot inherit zero/default budgets and immediately
        // penalize or disconnect an otherwise healthy peer.
        initialize_mode1_sync_timing_for_reseed(*fresh_state, now_ms);
        advanced_hook = fresh_state->callbacks.packet_advanced;
        advanced_user_data = fresh_state->callback_user_data;
        g_mode1_reliable_state = std::move(*fresh_state);
    }
    invoke_seeded_packet_hooks(
        advanced_packets, advanced_hook, advanced_user_data);
}

void ResetMode1ReliableRuntimeAndReplayState(bool scenario_ai_profile_override,
    u32 game_version, u8 reliable_mode, bool forced_replay_mode, u8 local_player,
    const std::vector<u8>& replay_metadata) {
    {
        // Session teardown/reset keeps the historical empty-ring semantics.
        // Unlike the public modal/load clear, no initialized seeded state is
        // needed here because startup will call ResetMode1ReliablePacketState.
        auto fresh_state = std::make_unique<Mode1ReliableRuntimeState>();
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        preserve_mode1_runtime_configuration(
            *fresh_state, g_mode1_reliable_state, true);
        fresh_state->initial_sequence_count =
            g_mode1_reliable_state.initial_sequence_count;
        fresh_state->local_broadcast_start =
            fresh_state->initial_sequence_count;
        fresh_state->local_broadcast_end =
            fresh_state->initial_sequence_count;
        fresh_state->sync_threshold =
            fresh_state->initial_sequence_count >> 1;
        fresh_state->sync_gate_open = true;
        fresh_state->local_broadcast_gate_open = false;
        fresh_state->initialized = false;
        g_mode1_reliable_state = std::move(*fresh_state);
    }
    ReplayRecordingState& replay = replay_recording_state();
    if (!replay.playback_mode) {
        InitializeReplayTempFiles(replay, false, game_version, reliable_mode,
            forced_replay_mode, local_player, replay_metadata,
            scenario_ai_profile_override);
    }
}

void InitializeMode1ReliableSyncRuntime(u32 now_ms) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    g_mode1_reliable_state.current_time_ms = now_ms;
    g_mode1_reliable_state.sync_processed_mask = 0;
    g_mode1_reliable_state.sync_round_required_mask = 0;
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
    auto fresh_state = std::make_unique<Mode1ReliableRuntimeState>();
    std::vector<Mode1ReliablePacket> advanced_packets;
    Mode1ReliablePacketHook advanced_hook = nullptr;
    void* advanced_user_data = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        preserve_mode1_runtime_configuration(
            *fresh_state, g_mode1_reliable_state, false);
        configure_mode1_seeded_state(
            *fresh_state, initial_sequence_count, false, advanced_packets);
        advanced_hook = fresh_state->callbacks.packet_advanced;
        advanced_user_data = fresh_state->callback_user_data;
        g_mode1_reliable_state = std::move(*fresh_state);
    }
    invoke_seeded_packet_hooks(
        advanced_packets, advanced_hook, advanced_user_data);
}

void SetMode1ReliableCallbacks(const Mode1ReliableCallbacks& callbacks, void* user_data) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    g_mode1_reliable_state.callbacks = callbacks;
    g_mode1_reliable_state.callback_user_data = user_data;
}

void SetMode1ReliableLocalPlayerIndex(u32 local_player_index) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    if (local_player_index < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.local_player_index = local_player_index;
    }
}

void SetMode1ReliableLocallySimulatedChannelMask(u32 channel_mask) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    g_mode1_reliable_state.locally_simulated_channel_mask =
        channel_mask & ((1u << kMode1ReliableChannelCount) - 1u);
}

void SetMode1ReliableSubtype10CollectionEnabled(bool enabled) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    g_mode1_reliable_state.subtype10_collection_enabled = enabled;
}

void SetMode1ReliablePlayerStatus(u32 player, u8 status) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    if (player < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.player_status[player] = status;
        if ((g_mode1_reliable_state.sync_processed_mask &
                kMode1SyncRoundActive) != 0 &&
            (status == 1 || status == 0x14)) {
            // Required membership is monotonic within a round.  An inactive
            // participant stops blocking immediately, but reactivation is
            // deliberately deferred until the next CheckSync snapshot.
            g_mode1_reliable_state.sync_round_required_mask &= ~(1u << player);
        }
    }
}

void SetMode1ReliableReplayFrameTick(u32 frame_tick) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    g_mode1_reliable_state.replay_frame_tick = frame_tick;
}

void SetMode1ReliableExpectedSequence(u32 channel, u32 sequence) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    if (channel < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.channels[channel].expected_sequence = sequence;
    }
}

u32 GetMode1ReliableExpectedSequence(u32 channel) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    if (channel >= kMode1ReliableChannelCount) {
        return 0;
    }
    return g_mode1_reliable_state.channels[channel].expected_sequence;
}

void ClearMode1ReliableMissingRangeRequest(u32 channel) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    if (channel < kMode1ReliableChannelCount) {
        g_mode1_reliable_state.channels[channel].missing_range_requested = false;
    }
}

void MarkMode1ReliableLocalBroadcastEnd(u32 end_sequence) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    g_mode1_reliable_state.local_broadcast_end = end_sequence;
}

void CollectMode1Subtype10Value(const Mode1ReliablePacket& packet) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    collect_mode1_subtype10_value_locked(packet);
}

u32 GetMode1ReliableChecksumAuthorityChannel() {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        if (is_active_player_locked(channel)) {
            return channel;
        }
    }
    return g_mode1_reliable_state.local_player_index;
}

bool TryGetMode1ReliableAuthoritativeSubtype10Value(
    u32 authority_channel, u32 local_channel, u32& out_value,
    u32* out_fifo_index) {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    if (authority_channel >= kMode1ReliableChannelCount ||
        local_channel >= kMode1ReliableChannelCount ||
        authority_channel == local_channel ||
        !is_active_player_locked(authority_channel)) {
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

    std::vector<Mode1ReliablePacket> advanced_packets;
    Mode1ReliablePacketHook advanced_hook = nullptr;
    void* advanced_user_data = nullptr;
    bool request_missing_range = false;
    u32 missing_start = 0;
    u32 missing_end = 0;
    {
        // Original AcceptMode1OrderedPacket (0x00426440) serializes this ring,
        // expected cursor and subtype-10 FIFO with CRITICAL_SECTION 0x014B9DA8.
        // Keep callbacks and transport sends outside the lock: both may
        // synchronously re-enter the reliable layer in the reconstruction.
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
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
            collect_mode1_subtype10_value_locked(advanced);
            advanced_packets.push_back(advanced);

            const u32 next_sequence = current_sequence + 1;
            const u32 next_slot = next_sequence & (kMode1ReliableWindowSlots - 1);
            if ((channel.packet_flags[next_slot] & 1) == 0) {
                break;
            }

            current_sequence = read_u32(
                channel.packet_bytes[next_slot].data(), kMode1ReliablePacketBytes, 8);
            channel.missing_range_requested = false;
        }

        if (channel.expected_sequence < sequence &&
            !channel.missing_range_requested) {
            channel.missing_range_requested = true;
            request_missing_range = true;
            missing_start = channel.expected_sequence;
            missing_end = sequence - 1;
        }
        advanced_hook = g_mode1_reliable_state.callbacks.packet_advanced;
        advanced_user_data = g_mode1_reliable_state.callback_user_data;
    }

    for (const Mode1ReliablePacket& advanced : advanced_packets) {
        if (advanced_hook != nullptr) {
            advanced_hook(advanced, advanced_user_data);
        }
    }
    if (request_missing_range) {
        RequestMode1MissingRange(channel_index, missing_start, missing_end);
    }
    return true;
}

bool PopMode1OrderedPacket(u32 channel_index, Mode1ReliablePacket& out_packet) {
    if (channel_index >= kMode1ReliableChannelCount) {
        return false;
    }

    bool request_missing_range = false;
    u32 missing_start = 0;
    u32 missing_end = 0;
    u32 replay_frame_tick = 0;
    bool popped = false;
    {
        // Mirrors PopMode1OrderedPacket's 0x014B9DA8 critical section at
        // 0x00426600 while keeping replay I/O and range sends outside it.
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        auto& channel = g_mode1_reliable_state.channels[channel_index];
        u32& read_sequence = g_mode1_reliable_state.read_sequences[channel_index];
        if (read_sequence < channel.expected_sequence) {
            const u32 slot = read_sequence & (kMode1ReliableWindowSlots - 1);
            if ((channel.packet_flags[slot] & 1) == 0) {
                request_missing_range = true;
                missing_start = read_sequence;
                missing_end = read_sequence +
                    g_mode1_reliable_state.initial_sequence_count;
            }
            else {
                out_packet = make_packet_view(channel, read_sequence);
                replay_frame_tick = g_mode1_reliable_state.replay_frame_tick;
                channel.packet_flags[slot] &= ~1u;
                ++read_sequence;
                popped = true;
            }
        }
    }

    if (request_missing_range) {
        RequestMode1MissingRange(channel_index, missing_start, missing_end);
    }
    if (!popped) {
        return false;
    }
    AppendReplayPacketRecord(replay_recording_state(), out_packet.bytes.data(),
        kMode1ReliablePacketBytes, replay_frame_tick);
    return true;
}

void ShiftMode1Subtype10Values() {
    // Original ShiftMode1Subtype10Values takes the same 0x014B9DA8 critical
    // section as Accept and Pop (0x00429340).
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    shift_mode1_subtype10_values_locked(active_player_mask_locked());
}

void ApplyMode1SyncTimeoutPenalty() {
    Mode1ReliableSimpleHook timeout_hook = nullptr;
    void* timeout_user_data = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        const u32 now_ms = g_mode1_reliable_state.current_time_ms;
        const u32 threshold = g_mode1_reliable_state.sync_threshold;
        for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
            if (is_active_player_locked(channel) &&
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
        timeout_hook = g_mode1_reliable_state.callbacks.sync_timeout;
        timeout_user_data = g_mode1_reliable_state.callback_user_data;
    }
    if (timeout_hook != nullptr) {
        timeout_hook(timeout_user_data);
    }
}

bool CheckMode1ReliableSync(u32 now_ms) {
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        g_mode1_reliable_state.current_time_ms = now_ms;
    }
    poll_mode1_transport_receive();

    struct MissingRequest {
        u32 channel = 0;
        u32 start_sequence = 0;
    };
    std::vector<MissingRequest> missing_requests;
    bool waiting = false;
    bool apply_timeout_penalty = false;
    Mode1ReliableSimpleHook ready_hook = nullptr;
    void* ready_user_data = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        if (g_mode1_reliable_state.sync_gate_open) {
            g_mode1_reliable_state.sync_start_time = now_ms;
            for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
                g_mode1_reliable_state.last_missing_request_time[channel] = now_ms;
            }
            g_mode1_reliable_state.sync_gate_open = false;
        }

        const u32 threshold = g_mode1_reliable_state.sync_threshold;
        for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
            if (!is_active_player_locked(channel)) {
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
            if (g_mode1_reliable_state.last_missing_request_time[channel] + 700U <
                now_ms) {
                missing_requests.push_back(MissingRequest{
                    channel,
                    g_mode1_reliable_state.channels[channel].expected_sequence});
                g_mode1_reliable_state.last_missing_request_time[channel] = now_ms;
            }
        }

        if (!waiting) {
            ready_hook = g_mode1_reliable_state.callbacks.sync_ready;
            ready_user_data = g_mode1_reliable_state.callback_user_data;
            g_mode1_reliable_state.sync_gate_open = true;
        }
        else {
            apply_timeout_penalty =
                1000U < now_ms - g_mode1_reliable_state.sync_start_time;
        }
    }

    for (const MissingRequest& request : missing_requests) {
        RequestMode1MissingRange(
            request.channel, request.start_sequence, 0xffffffffu);
    }
    if (!waiting) {
        if (ready_hook != nullptr) {
            ready_hook(ready_user_data);
        }
        return true;
    }

    if (apply_timeout_penalty) {
        ApplyMode1SyncTimeoutPenalty();
    }
    return false;
}

bool IsMode1ReliableSyncRoundPending() {
    const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
    return (g_mode1_reliable_state.sync_processed_mask &
        kMode1SyncRoundActive) != 0;
}

u32 PumpMode1ReliablePackets(
    bool generic_ai_profile_mode, bool scenario_ai_profile_override, u32 now_ms) {
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        g_mode1_reliable_state.current_time_ms = now_ms;
    }

    // PumpMode1ReliablePackets is the boolean simulation-frame gate at
    // 0x004C1161..0x004C1168.  Its EAX is not a packet count: every completed
    // normal/replay round returns one, while only an incomplete synchronized
    // multiplayer round returns zero.
    if (scenario_ai_profile_override) {
        return 1;
    }
    if (!generic_ai_profile_mode) {
        u32 local_player = 0;
        Mode1ReliableSimpleHook snapshot_hook = nullptr;
        void* snapshot_user_data = nullptr;
        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            local_player = g_mode1_reliable_state.local_player_index;
            snapshot_hook = g_mode1_reliable_state.callbacks.snapshot_local_checksum;
            snapshot_user_data = g_mode1_reliable_state.callback_user_data;
        }
        if (local_player >= kMode1ReliableChannelCount) {
            return 1;
        }
        if (snapshot_hook != nullptr) {
            snapshot_hook(snapshot_user_data);
        }
        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            g_mode1_reliable_state.sync_consumed_flags[local_player] = 0;
        }
        for (;;) {
            {
                const std::lock_guard<std::recursive_mutex> lock(
                    g_mode1_reliable_mutex);
                if (g_mode1_reliable_state.sync_consumed_flags[local_player] != 0) {
                    break;
                }
            }
            Mode1ReliablePacket packet{};
            if (!PopMode1OrderedPacket(local_player, packet)) {
                break;
            }
            DispatchMode1GameplayPacket(packet);
            invoke_packet_hook(g_mode1_reliable_state.callbacks.packet_consumed, packet);
        }
        u32 locally_simulated_mask = 0;
        {
            const std::lock_guard<std::recursive_mutex> lock(
                g_mode1_reliable_mutex);
            locally_simulated_mask =
                g_mode1_reliable_state.locally_simulated_channel_mask &
                ~(1u << local_player);
        }
        for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
            if ((locally_simulated_mask & (1u << channel)) == 0) {
                continue;
            }
            for (;;) {
                Mode1ReliablePacket packet{};
                if (!PopMode1OrderedPacket(channel, packet)) {
                    break;
                }
                DispatchMode1GameplayPacket(packet);
                invoke_packet_hook(
                    g_mode1_reliable_state.callbacks.packet_consumed, packet);
            }
        }
        return 1;
    }

    bool round_active = false;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        round_active =
            (g_mode1_reliable_state.sync_processed_mask & kMode1SyncRoundActive) != 0;
    }

    if (!round_active) {
        bool publish_snapshot = false;
        Mode1ReliableSimpleHook snapshot_hook = nullptr;
        void* snapshot_user_data = nullptr;
        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            publish_snapshot = g_mode1_reliable_state.local_broadcast_gate_open;
            if (publish_snapshot) {
                g_mode1_reliable_state.compatibility_checksum_snapshot_deferred = false;
                snapshot_hook =
                    g_mode1_reliable_state.callbacks.snapshot_local_checksum;
                snapshot_user_data = g_mode1_reliable_state.callback_user_data;
            }
        }

        if (publish_snapshot) {
            if (snapshot_hook != nullptr) {
                snapshot_hook(snapshot_user_data);
            }

            u32 broadcast_start = 0;
            u32 broadcast_end = 0;
            {
                const std::lock_guard<std::recursive_mutex> lock(
                    g_mode1_reliable_mutex);
                broadcast_start = g_mode1_reliable_state.local_broadcast_start;
                broadcast_end = g_mode1_reliable_state.local_broadcast_end;
                if (broadcast_start < broadcast_end) {
                    g_mode1_reliable_state.local_broadcast_start = broadcast_end;
                }
                // A compatibility client can defer its heartbeat while the
                // authoritative peer's matching FIFO value is still in flight.
                g_mode1_reliable_state.local_broadcast_gate_open =
                    g_mode1_reliable_state.compatibility_checksum_snapshot_deferred;
            }
            if (broadcast_start < broadcast_end) {
                BroadcastMode1PacketRange(broadcast_start, broadcast_end - 1);
            }
        }

        if (!CheckMode1ReliableSync(now_ms)) {
            return 0;
        }

        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            g_mode1_reliable_state.sync_consumed_flags.fill(0);
            g_mode1_reliable_state.sync_processed_mask = kMode1SyncRoundActive;
            // This is the original loop's participant set for the whole
            // synchronized round.  New active slots cannot retroactively
            // become prerequisites after earlier channels were consumed.
            g_mode1_reliable_state.sync_round_required_mask =
                active_player_mask_locked();
        }
    }
    else {
        // The original remains inside its channel loop while the dedicated
        // UDP worker continues receiving packets.  The reconstruction yields
        // to the UI when PopMode1OrderedPacket has to wait, but its default
        // legacy-UDP path has no continuously running receive worker.  Poll
        // again whenever that partial round is resumed; otherwise the packet
        // that would close the channel can sit in the socket forever because
        // CheckMode1ReliableSync is skipped while the round-active bit is set.
        poll_mode1_transport_receive();
    }

    // Original 0x00429955..0x004299A9 does not leave a channel until its
    // subtype-10 handler has set DAT_011B5A5C[channel].  A null Pop merely
    // waits for more transport data.  Returning to the UI loop is friendlier
    // than busy-spinning, but the partial round must remain open and channels
    // already closed in this round must never be consumed twice.
    for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
        bool required = false;
        bool active = false;
        bool processed = false;
        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            required =
                (g_mode1_reliable_state.sync_round_required_mask &
                    (1u << channel)) != 0;
            active = is_active_player_locked(channel);
            processed =
                (g_mode1_reliable_state.sync_processed_mask & (1u << channel)) != 0;
            if (required && !active) {
                g_mode1_reliable_state.sync_round_required_mask &=
                    ~(1u << channel);
                required = false;
            }
        }
        if (!required || !active || processed) {
            continue;
        }

        for (;;) {
            bool channel_closed = false;
            {
                const std::lock_guard<std::recursive_mutex> lock(
                    g_mode1_reliable_mutex);
                if (!is_active_player_locked(channel)) {
                    g_mode1_reliable_state.sync_round_required_mask &=
                        ~(1u << channel);
                    channel_closed = true;
                }
                else {
                    channel_closed =
                        g_mode1_reliable_state.sync_consumed_flags[channel] != 0;
                }
            }
            if (channel_closed) {
                break;
            }

            Mode1ReliablePacket packet{};
            if (!PopMode1OrderedPacket(channel, packet)) {
                return 0;
            }
            DispatchMode1GameplayPacket(packet);
            invoke_packet_hook(g_mode1_reliable_state.callbacks.packet_consumed, packet);
        }

        {
            const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
            const u32 channel_bit = 1u << channel;
            if ((g_mode1_reliable_state.sync_round_required_mask &
                    channel_bit) == 0) {
                continue;
            }
            if (!is_active_player_locked(channel)) {
                g_mode1_reliable_state.sync_round_required_mask &= ~channel_bit;
                continue;
            }
            if (g_mode1_reliable_state.sync_consumed_flags[channel] == 0) {
                return 0;
            }
            g_mode1_reliable_state.sync_processed_mask |= channel_bit;
        }
    }

    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        // SetMode1ReliablePlayerStatus normally removes inactive participants
        // at transition time.  Intersect once more for legacy callers that
        // still edit the public runtime state directly.
        g_mode1_reliable_state.sync_round_required_mask &=
            active_player_mask_locked();
        const u32 required_mask =
            g_mode1_reliable_state.sync_round_required_mask &
            kMode1SyncChannelMask;
        const u32 processed_mask =
            g_mode1_reliable_state.sync_processed_mask & kMode1SyncChannelMask;
        if ((processed_mask & required_mask) != required_mask) {
            return 0;
        }
        for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
            if ((required_mask & (1u << channel)) != 0 &&
                g_mode1_reliable_state.sync_consumed_flags[channel] == 0) {
                return 0;
            }
        }

        u32 consumed_mask = 0;
        for (u32 channel = 0; channel < kMode1ReliableChannelCount; ++channel) {
            if (g_mode1_reliable_state.sync_consumed_flags[channel] != 0) {
                consumed_mask |= 1u << channel;
            }
        }
        // Required membership controls who may block completion; FIFO
        // advancement instead follows packets actually consumed.  Keeping
        // these masks separate is essential when a channel closes, leaves,
        // and rejoins during a partial round: its read cursor and checksum
        // FIFO head must advance together.
        shift_mode1_subtype10_values_locked(consumed_mask);
        g_mode1_reliable_state.sync_processed_mask = 0;
        g_mode1_reliable_state.sync_round_required_mask = 0;
        g_mode1_reliable_state.sync_gate_open = true;
        g_mode1_reliable_state.local_broadcast_gate_open = true;
    }
    return 1;
}

void RequestMode1MissingRange(u32 channel, u32 start_sequence, u32 end_sequence) {
    if (channel >= kMode1ReliableChannelCount) {
        remember_send_status(-1);
        return;
    }

    std::array<u8, kMode1ReliablePacketBytes> packet{};
    u32 local_player = 0;
    Mode1ReliableRangeHook missing_hook = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        local_player = g_mode1_reliable_state.local_player_index;
        missing_hook = g_mode1_reliable_state.callbacks.missing_range_requested;
    }
    build_control_packet(packet, static_cast<u8>(channel), 0x17, start_sequence,
        local_player, end_sequence);
    send_to_player(packet.data(), kMode1ReliablePacketBytes, channel);
    invoke_range_hook(missing_hook, channel, start_sequence, end_sequence, channel);
}

i32 SendMode1GapAck(u32 target_player) {
    std::array<u8, kMode1ReliablePacketBytes> packet{};
    u32 local_player = 0;
    Mode1ReliableRangeHook gap_hook = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        local_player = g_mode1_reliable_state.local_player_index;
        gap_hook = g_mode1_reliable_state.callbacks.gap_ack_sent;
    }
    build_control_packet(packet,
        static_cast<u8>(local_player), 0x1c, 0, 0, 0);
    const i32 status =
        send_to_player(packet.data(), kMode1ReliablePacketBytes, target_player);
    invoke_range_hook(gap_hook, local_player, 0, 0, target_player);
    return status;
}

void ResendMode1PacketRange(u32 start_sequence, u32 end_sequence, u32 target_player) {
    u32 local_player = 0;
    Mode1ReliableRangeHook range_hook = nullptr;
    std::vector<u8> first_payload;
    std::vector<u8> second_payload;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        local_player = g_mode1_reliable_state.local_player_index;
        range_hook = g_mode1_reliable_state.callbacks.range_resent;
        if (local_player < kMode1ReliableChannelCount) {
            const auto& channel = g_mode1_reliable_state.channels[local_player];
            const u32 start_slot = start_sequence & (kMode1ReliableWindowSlots - 1);
            const u32 end_slot = end_sequence & (kMode1ReliableWindowSlots - 1);
            if (end_slot < start_slot) {
                first_payload = copy_packet_slots_locked(
                    channel, start_slot, kMode1ReliableWindowSlots - start_slot);
                second_payload = copy_packet_slots_locked(channel, 0, end_slot + 1);
            }
            else {
                first_payload = copy_packet_slots_locked(
                    channel, start_slot, end_slot + 1 - start_slot);
            }
        }
    }
    if (local_player >= kMode1ReliableChannelCount) {
        remember_send_status(-1);
        return;
    }

    i32 status = send_to_player(first_payload.data(),
        static_cast<u32>(first_payload.size()), target_player);
    if (status >= 0 && !second_payload.empty()) {
        status = send_to_player(second_payload.data(),
            static_cast<u32>(second_payload.size()), target_player);
    }

    invoke_range_hook(range_hook, local_player, start_sequence, end_sequence, target_player);
}

void BroadcastMode1PacketRange(u32 start_sequence, u32 end_sequence) {
    u32 local_player = 0;
    Mode1ReliableSendCallback broadcast_callback = nullptr;
    Mode1ReliableRangeHook range_hook = nullptr;
    void* callback_user_data = nullptr;
    std::vector<u8> first_payload;
    std::vector<u8> second_payload;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        local_player = g_mode1_reliable_state.local_player_index;
        broadcast_callback = g_mode1_reliable_state.callbacks.broadcast;
        range_hook = g_mode1_reliable_state.callbacks.range_broadcast;
        callback_user_data = g_mode1_reliable_state.callback_user_data;
        if (local_player < kMode1ReliableChannelCount &&
            broadcast_callback != nullptr) {
            const auto& channel = g_mode1_reliable_state.channels[local_player];
            const u32 start_slot = start_sequence & (kMode1ReliableWindowSlots - 1);
            const u32 end_slot = end_sequence & (kMode1ReliableWindowSlots - 1);
            if (end_slot < start_slot) {
                first_payload = copy_packet_slots_locked(
                    channel, start_slot, kMode1ReliableWindowSlots - start_slot);
                second_payload = copy_packet_slots_locked(channel, 0, end_slot + 1);
            }
            else {
                first_payload = copy_packet_slots_locked(
                    channel, start_slot, end_slot + 1 - start_slot);
            }
        }
    }
    if (local_player >= kMode1ReliableChannelCount ||
        broadcast_callback == nullptr) {
        remember_send_status(-1);
        return;
    }

    i32 status = broadcast_callback(first_payload.data(),
        static_cast<u32>(first_payload.size()), 0, callback_user_data);
    if (status >= 0 && !second_payload.empty()) {
        status = broadcast_callback(second_payload.data(),
            static_cast<u32>(second_payload.size()), 0, callback_user_data);
    }

    remember_send_status(status);
    invoke_range_hook(range_hook, local_player, start_sequence, end_sequence, 0);
}

i32 BroadcastMode1ReliablePayload(const void* packet, u32 packet_size) {
    Mode1ReliableSendCallback callback = nullptr;
    void* user_data = nullptr;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        callback = g_mode1_reliable_state.callbacks.broadcast;
        user_data = g_mode1_reliable_state.callback_user_data;
    }
    if (callback == nullptr) {
        return remember_send_status(-1);
    }

    const i32 status = callback(packet, packet_size, 0, user_data);
    return remember_send_status(status);
}

i32 BroadcastMode1ReliablePayloadToAll(const void* packet, u32 packet_size) {
    return BroadcastMode1ReliablePayload(packet, packet_size);
}

bool WrapAndPublishMode1SlashCommandPacket(const void* payload, u32 payload_size) {
    std::vector<u8> wrapped = BuildMode1WrappedCommandPacket(payload, payload_size);
    if (wrapped.empty()) {
        return false;
    }

    Mode1ReliablePayloadHook publish = nullptr;
    void* user_data = nullptr;
    std::vector<u8> callback_packet;
    {
        const std::lock_guard<std::recursive_mutex> lock(g_mode1_reliable_mutex);
        g_mode1_reliable_state.last_wrapped_command_packet = std::move(wrapped);
        publish = g_mode1_reliable_state.callbacks.wrapped_packet_published;
        user_data = g_mode1_reliable_state.callback_user_data;
        if (publish != nullptr) {
            callback_packet = g_mode1_reliable_state.last_wrapped_command_packet;
        }
    }
    if (publish != nullptr) {
        publish(callback_packet.data(),
            static_cast<u32>(callback_packet.size()), user_data);
    }
    return true;
}

}
