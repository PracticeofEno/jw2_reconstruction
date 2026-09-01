#include "ranker_gameplay_packets.h"
#include "ranker_reliable_packets.h"
#include "ranker_replay.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace ranker {

namespace {

ReplayRecordingState g_test_replay;
u32 g_poll_count = 0;
std::array<u32, kMode1ReliableChannelCount> g_dispatch_counts{};

void write_u32(std::array<u8, kMode1ReliablePacketBytes>& bytes,
    std::size_t offset, u32 value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void inject_waited_for_packet(void*) {
    ++g_poll_count;
    if (g_poll_count != 1) {
        return;
    }

    std::array<u8, kMode1ReliablePacketBytes> packet{};
    write_u32(packet, 0x00, 1);
    write_u32(packet, 0x04, kMode1ReliablePacketBytes);
    write_u32(packet, 0x08, 6);
    packet[0x0c] = 1;
    packet[0x0f] = 0x10;
    write_u32(packet, 0x14, 0x12345678u);
    AcceptMode1OrderedPacket(packet.data(), kMode1ReliablePacketBytes);
}

} // namespace

ReplayRecordingState& replay_recording_state() {
    return g_test_replay;
}

void InitializeReplayTempFiles(ReplayRecordingState&, bool, u32, u8, bool, u8,
    const std::vector<u8>&, bool) {
}

bool AppendReplayPacketRecord(ReplayRecordingState&, const void*, u32, u32) {
    return true;
}

bool DispatchMode1GameplayPacket(const Mode1ReliablePacket& packet) {
    if (packet.channel < g_dispatch_counts.size()) {
        ++g_dispatch_counts[packet.channel];
    }
    if (packet.subtype == 0x10 && packet.channel < kMode1ReliableChannelCount) {
        mode1_reliable_state().sync_consumed_flags[packet.channel] = 1;
    }
    return true;
}

} // namespace ranker

int main() {
    using namespace ranker;

    Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
    reliable = Mode1ReliableRuntimeState{};
    reliable.player_status.fill(0x14);
    reliable.player_status[1] = 0;
    reliable.local_player_index = 0;
    reliable.initialized = true;
    reliable.channels[1].expected_sequence = 6;
    reliable.read_sequences[1] = 6;
    reliable.sync_processed_mask = 0x80000000u;
    reliable.sync_round_required_mask = 1u << 1;

    Mode1ReliableCallbacks callbacks{};
    callbacks.poll_transport_receive = inject_waited_for_packet;
    SetMode1ReliableCallbacks(callbacks);

    const u32 result = PumpMode1ReliablePackets(true, false, 1000);
    if (g_poll_count != 1 || result != 1 || reliable.read_sequences[1] != 7 ||
        reliable.sync_processed_mask != 0 ||
        reliable.sync_round_required_mask != 0) {
        std::fprintf(stderr,
            "partial round did not receive and consume its awaited packet: "
            "polls=%lu result=%lu read=%lu processed=%08lx required=%08lx\n",
            static_cast<unsigned long>(g_poll_count),
            static_cast<unsigned long>(result),
            static_cast<unsigned long>(reliable.read_sequences[1]),
            static_cast<unsigned long>(reliable.sync_processed_mask),
            static_cast<unsigned long>(reliable.sync_round_required_mask));
        return 1;
    }

    reliable = Mode1ReliableRuntimeState{};
    reliable.player_status.fill(0x14);
    reliable.player_status[0] = 0;
    reliable.local_player_index = 0;
    reliable.initialized = true;
    reliable.channels[0].expected_sequence = 6;
    reliable.read_sequences[0] = 6;
    reliable.channels[1].expected_sequence = 6;
    reliable.read_sequences[1] = 6;
    g_dispatch_counts = {};
    SetMode1ReliableCallbacks(Mode1ReliableCallbacks{});
    SetMode1ReliableLocallySimulatedChannelMask(1u << 1);

    std::array<u8, kMode1ReliablePacketBytes> ai_packet{};
    write_u32(ai_packet, 0x00, 1);
    write_u32(ai_packet, 0x04, kMode1ReliablePacketBytes);
    write_u32(ai_packet, 0x08, 6);
    ai_packet[0x0c] = 1;
    ai_packet[0x0f] = 0x02;
    if (!AcceptMode1OrderedPacket(ai_packet.data(), kMode1ReliablePacketBytes) ||
        PumpMode1ReliablePackets(false, false, 1001) != 1 ||
        reliable.read_sequences[1] != 7 || g_dispatch_counts[1] != 1) {
        std::fprintf(stderr,
            "locally simulated Computer(AI) channel was not consumed: "
            "read=%lu dispatches=%lu\n",
            static_cast<unsigned long>(reliable.read_sequences[1]),
            static_cast<unsigned long>(g_dispatch_counts[1]));
        return 1;
    }

    // ---- entity-RL atomic AI batch publish (plan sections 2 / 17.18) ----
    {
        // Fresh cursors on channels 2 and 3.
        auto& state = mode1_reliable_state();
        state.channels[2] = Mode1ReliableChannelState{};
        state.channels[3] = Mode1ReliableChannelState{};
        state.read_sequences[2] = 0;
        state.read_sequences[3] = 0;

        // Success hook runs exactly once per successful transaction.
        static u32 hook_calls = 0;
        hook_calls = 0;
        const auto hook = [](void*) { ++hook_calls; };

        // Multi-channel batch: consecutive per-channel sequences, both
        // committed in one transaction.
        std::vector<Mode1AiBatchPacketRequest> batch;
        Mode1AiBatchPacketRequest request{};
        request.packed_opcode = (0x02u << 24) | 2u;   // subtype 2, channel 2
        batch.push_back(request);
        batch.push_back(request);
        request.packed_opcode = (0x02u << 24) | 3u;   // channel 3
        batch.push_back(request);
        if (AcceptMode1AiOrderedPacketBatch(batch, hook, nullptr) !=
                Mode1AiBatchCode::accepted ||
            hook_calls != 1 ||
            batch[0].assigned_channel != 2 || batch[0].assigned_sequence != 0 ||
            batch[1].assigned_sequence != 1 ||
            batch[2].assigned_channel != 3 || batch[2].assigned_sequence != 0 ||
            state.channels[2].expected_sequence != 2 ||
            state.channels[3].expected_sequence != 1) {
            std::fprintf(stderr, "AI batch commit did not assign per-channel "
                "consecutive sequences\n");
            return 1;
        }

        // Empty batch: metadata-only success, hook still runs.
        std::vector<Mode1AiBatchPacketRequest> empty;
        if (AcceptMode1AiOrderedPacketBatch(empty, hook, nullptr) !=
                Mode1AiBatchCode::empty || hook_calls != 2) {
            std::fprintf(stderr, "empty AI batch did not commit metadata\n");
            return 1;
        }

        // All-or-none: an invalid channel in the SECOND packet aborts the
        // whole batch without touching the first packet's channel and
        // without running the hook.
        std::vector<Mode1AiBatchPacketRequest> bad;
        request.packed_opcode = (0x02u << 24) | 2u;
        bad.push_back(request);
        request.packed_opcode = (0x02u << 24) | 9u;   // invalid channel
        bad.push_back(request);
        if (AcceptMode1AiOrderedPacketBatch(bad, hook, nullptr) !=
                Mode1AiBatchCode::invalid_channel ||
            hook_calls != 2 || state.channels[2].expected_sequence != 2) {
            std::fprintf(stderr, "AI batch abort was not all-or-none\n");
            return 1;
        }

        // Per-channel window boundary: unread 0x7ff + 1 planned hits the
        // strict less-than rule; unread 0x7fe + 1 passes.  Channel 4 is
        // synthetic (cursors only).
        state.channels[4] = Mode1ReliableChannelState{};
        state.channels[4].expected_sequence = 0x7ffu;
        state.read_sequences[4] = 0;
        std::vector<Mode1AiBatchPacketRequest> brim;
        request.packed_opcode = (0x02u << 24) | 4u;
        brim.push_back(request);
        if (AcceptMode1AiOrderedPacketBatch(brim, nullptr, nullptr) !=
                Mode1AiBatchCode::capacity ||
            state.channels[4].expected_sequence != 0x7ffu) {
            std::fprintf(stderr,
                "0x7ff unread + 1 planned was not rejected\n");
            return 1;
        }
        state.read_sequences[4] = 1;   // one consumed: 0x7fe unread
        // The wrap slot (sequence 0x7ff -> slot 0x7ff) is empty here.
        if (AcceptMode1AiOrderedPacketBatch(brim, nullptr, nullptr) !=
                Mode1AiBatchCode::accepted ||
            brim[0].assigned_sequence != 0x7ffu ||
            state.channels[4].expected_sequence != 0x800u) {
            std::fprintf(stderr,
                "0x7fe unread + 1 planned was wrongly rejected\n");
            return 1;
        }

        // Occupied wrap-around slot: an unread occupant in the target slot
        // rejects the batch even when the count check passes.
        state.channels[5] = Mode1ReliableChannelState{};
        state.channels[5].expected_sequence = 0x800u;   // next slot = 0
        state.read_sequences[5] = 0x7f0u;               // 0x10 unread
        state.channels[5].packet_flags[0] |= 1;         // slot 0 occupant:
        write_u32(state.channels[5].packet_bytes[0], 0x08, 0x7f5u);  // unread
        std::vector<Mode1AiBatchPacketRequest> wrap;
        request.packed_opcode = (0x02u << 24) | 5u;
        wrap.push_back(request);
        if (AcceptMode1AiOrderedPacketBatch(wrap, nullptr, nullptr) !=
                Mode1AiBatchCode::slot_occupied) {
            std::fprintf(stderr, "occupied wrap slot was not rejected\n");
            return 1;
        }
        // Once the occupant is consumed the same batch commits.
        write_u32(state.channels[5].packet_bytes[0], 0x08, 0x700u);
        if (AcceptMode1AiOrderedPacketBatch(wrap, nullptr, nullptr) !=
                Mode1AiBatchCode::accepted) {
            std::fprintf(stderr, "consumed wrap slot stayed rejected\n");
            return 1;
        }

        // Producer behind consumer: transport-fatal underflow.
        state.channels[6] = Mode1ReliableChannelState{};
        state.channels[6].expected_sequence = 3;
        state.read_sequences[6] = 5;
        std::vector<Mode1AiBatchPacketRequest> under;
        request.packed_opcode = (0x02u << 24) | 6u;
        under.push_back(request);
        if (AcceptMode1AiOrderedPacketBatch(under, nullptr, nullptr) !=
                Mode1AiBatchCode::sequence_underflow) {
            std::fprintf(stderr, "producer<consumer was not fatal\n");
            return 1;
        }
    }

    std::puts("mode-1 partial round receive regression passed");
    return 0;
}
