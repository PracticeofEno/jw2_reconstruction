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

    std::puts("mode-1 partial round receive regression passed");
    return 0;
}
