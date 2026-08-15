#include "ranker_directplay.h"
#include "ranker_network.h"
#include "ranker_player_slots.h"
#include "ranker_reliable_packets.h"
#include "ranker_wizardnet_relay.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#ifdef _WIN32

namespace ranker {

bool DispatchMode1GameplayPacket(const Mode1ReliablePacket&) {
    return false;
}

PlayerSlotRuntimeState& player_slot_state() {
    static PlayerSlotRuntimeState state{};
    return state;
}

void MarkPlayerInactiveAndBroadcastIfLocal(PlayerSlotRuntimeState&, u32, u32) {}

}

namespace {

int fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

unsigned read_u32(const void* data, std::size_t offset) {
    unsigned value = 0;
    std::memcpy(&value, static_cast<const unsigned char*>(data) + offset,
        sizeof(value));
    return value;
}

void write_u32(std::vector<unsigned char>& data, std::size_t offset,
    unsigned value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::vector<unsigned char> build_async_packet(unsigned opcode,
    const void* payload, std::size_t byte_count) {
    std::vector<unsigned char> packet(0x0d + byte_count, 0);
    write_u32(packet, 0, 3);
    write_u32(packet, 4, opcode);
    write_u32(packet, 8, static_cast<unsigned>(packet.size()));
    if (payload != nullptr && byte_count != 0) {
        std::memcpy(packet.data() + 0x0d, payload, byte_count);
    }
    ranker::UpdateLegacyAsyncTcpPacketChecksum(packet.data(),
        static_cast<int>(packet.size()));
    return packet;
}

struct TcpPair {
    SOCKET client = INVALID_SOCKET;
    SOCKET server = INVALID_SOCKET;
};

void close_socket_if_open(SOCKET& socket) {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

void close_tcp_pair(TcpPair& pair) {
    close_socket_if_open(pair.client);
    close_socket_if_open(pair.server);
}

bool make_loopback_tcp_pair(TcpPair& pair) {
    if (!ranker::EnsureLegacyWinSockStartup(2, 0)) {
        return false;
    }

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
        SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR) {
        closesocket(listener);
        return false;
    }

    int address_size = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address),
            &address_size) == SOCKET_ERROR) {
        closesocket(listener);
        return false;
    }

    pair.client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (pair.client == INVALID_SOCKET) {
        closesocket(listener);
        return false;
    }
    if (connect(pair.client, reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR) {
        close_tcp_pair(pair);
        closesocket(listener);
        return false;
    }

    pair.server = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (pair.server == INVALID_SOCKET) {
        close_tcp_pair(pair);
        return false;
    }

    u_long nonblocking = 1;
    if (ioctlsocket(pair.client, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        close_tcp_pair(pair);
        return false;
    }
    return true;
}

int expect_packet(u32 opcode, u32 byte_count) {
    const ranker::LegacyAsyncTcpSocket& socket = ranker::FrontendAsyncTcpSocket0();
    if (socket.send_length != static_cast<i32>(byte_count)) {
        return fail("unexpected queued packet size");
    }
    if (read_u32(socket.send_buffer.data(), 0) != 3) {
        return fail("unexpected queued packet type");
    }
    if (read_u32(socket.send_buffer.data(), 4) != opcode) {
        return fail("unexpected queued packet opcode");
    }
    if (read_u32(socket.send_buffer.data(), 8) != byte_count) {
        return fail("unexpected queued packet length");
    }
    if (!ranker::VerifyLegacyAsyncTcpPacketChecksum(socket.send_buffer.data(),
            socket.send_length)) {
        return fail("queued packet checksum mismatch");
    }
    return 0;
}

int expect_relay_frame_queue(u32 game_id, u32 target_member, u32 stream_id,
    const void* payload, u32 payload_bytes, unsigned expected_packets) {
    const ranker::LegacyAsyncTcpSocket& socket = ranker::FrontendAsyncTcpSocket0();
    constexpr std::size_t relay_cipher_header_bytes = 28;
    const std::size_t expected_packet_bytes =
        0x0d + 12 + relay_cipher_header_bytes + payload_bytes;
    const std::size_t expected_total = expected_packet_bytes * expected_packets;
    if (socket.send_length != static_cast<i32>(expected_total)) {
        return fail("unexpected concurrent relay queue size");
    }

    std::size_t offset = 0;
    unsigned packet_count = 0;
    while (offset < expected_total) {
        if (expected_total - offset < 0x19) {
            return fail("concurrent relay queue ended inside a header");
        }
        const unsigned packet_type = read_u32(socket.send_buffer.data(), offset);
        const unsigned opcode = read_u32(socket.send_buffer.data(), offset + 4);
        const unsigned packet_bytes = read_u32(socket.send_buffer.data(), offset + 8);
        if (packet_type != 3 ||
            opcode != ranker::kWizardNetRelayFrameRequestOpcode ||
            packet_bytes != expected_packet_bytes) {
            return fail("concurrent relay packet header was corrupted");
        }
        if (!ranker::VerifyLegacyAsyncTcpPacketChecksum(
                socket.send_buffer.data() + offset,
                static_cast<int>(packet_bytes))) {
            return fail("concurrent relay packet checksum mismatch");
        }
        if (read_u32(socket.send_buffer.data(), offset + 0x0d) != game_id ||
            read_u32(socket.send_buffer.data(), offset + 0x11) != target_member ||
            read_u32(socket.send_buffer.data(), offset + 0x15) != stream_id) {
            return fail("concurrent relay frame metadata was corrupted");
        }
        const void* wire_payload = socket.send_buffer.data() + offset + 0x19;
        const u32 wire_payload_bytes =
            static_cast<u32>(packet_bytes - 0x19);
        if (!ranker::WizardNetRelayPayloadIsEncrypted(wire_payload,
                wire_payload_bytes)) {
            return fail("concurrent relay payload was not encrypted");
        }
        std::vector<unsigned char> decoded;
        if (!ranker::DecodeWizardNetRelayPayload(game_id, wire_payload,
                wire_payload_bytes, decoded) ||
            decoded.size() != payload_bytes ||
            std::memcmp(decoded.data(), payload, payload_bytes) != 0) {
            return fail("concurrent relay encrypted payload did not decode");
        }
        offset += packet_bytes;
        ++packet_count;
    }
    if (packet_count != expected_packets) {
        return fail("unexpected concurrent relay packet count");
    }
    return 0;
}

int expect_relay_frame_targets(u32 game_id, const std::vector<u32>& targets,
    u32 stream_id, const void* payload, u32 payload_bytes) {
    const ranker::LegacyAsyncTcpSocket& socket = ranker::FrontendAsyncTcpSocket0();
    constexpr std::size_t relay_cipher_header_bytes = 28;
    const std::size_t packet_bytes =
        0x0d + 12 + relay_cipher_header_bytes + payload_bytes;
    const std::size_t expected_total = packet_bytes * targets.size();
    if (socket.send_length != static_cast<i32>(expected_total)) {
        return fail("unexpected mode1 relay queue size");
    }

    for (std::size_t index = 0; index < targets.size(); ++index) {
        const std::size_t offset = index * packet_bytes;
        if (read_u32(socket.send_buffer.data(), offset) != 3 ||
            read_u32(socket.send_buffer.data(), offset + 4) !=
                ranker::kWizardNetRelayFrameRequestOpcode ||
            read_u32(socket.send_buffer.data(), offset + 8) != packet_bytes) {
            return fail("mode1 relay packet header was corrupted");
        }
        if (!ranker::VerifyLegacyAsyncTcpPacketChecksum(
                socket.send_buffer.data() + offset,
                static_cast<int>(packet_bytes))) {
            return fail("mode1 relay packet checksum mismatch");
        }
        if (read_u32(socket.send_buffer.data(), offset + 0x0d) != game_id ||
            read_u32(socket.send_buffer.data(), offset + 0x11) != targets[index] ||
            read_u32(socket.send_buffer.data(), offset + 0x15) != stream_id) {
            return fail("mode1 relay target metadata was corrupted");
        }
        const void* wire_payload = socket.send_buffer.data() + offset + 0x19;
        const u32 wire_payload_bytes =
            static_cast<u32>(packet_bytes - 0x19);
        if (!ranker::WizardNetRelayPayloadIsEncrypted(wire_payload,
                wire_payload_bytes)) {
            return fail("mode1 relay payload was not encrypted");
        }
        std::vector<unsigned char> decoded;
        if (!ranker::DecodeWizardNetRelayPayload(game_id, wire_payload,
                wire_payload_bytes, decoded) ||
            decoded.size() != payload_bytes ||
            std::memcmp(decoded.data(), payload, payload_bytes) != 0) {
            return fail("mode1 relay encrypted payload was corrupted");
        }
    }
    return 0;
}

}

int main() {
    ranker::LegacyAsyncTcpSocket& socket = ranker::FrontendAsyncTcpSocket0();
    ranker::LegacyNetworkRuntimeState& network = ranker::legacy_network_state();
    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    network.udp_receive_queue.clear();
    ranker::ResetWizardNetRelayState();

    if (ranker::WizardNetRelayEnabled()) {
        return fail("relay starts enabled");
    }
    const SOCKET member_socket = ranker::WizardNetRelaySocketForMember(7);
    if (!ranker::WizardNetRelaySocketIsMember(member_socket) ||
        ranker::WizardNetRelayMemberForSocket(member_socket) != 7) {
        return fail("relay member socket mapping failed");
    }
    if (ranker::WizardNetRelaySocketForMember(0) != INVALID_SOCKET ||
        ranker::WizardNetRelayMemberForSocket(INVALID_SOCKET) != 0) {
        return fail("invalid relay socket mapping was accepted");
    }
    auto option_socket = std::make_unique<ranker::LegacyAsyncTcpSocket>();
    ranker::InitializeLegacyAsyncTcpSocket(*option_socket);
    if (!ranker::EnsureLegacyWinSockStartup(2, 0) ||
        !ranker::CreateLegacyAsyncTcpSocket(*option_socket)) {
        return fail("async tcp option socket was not created");
    }
    int no_delay = 0;
    int no_delay_size = sizeof(no_delay);
    if (getsockopt(option_socket->socket, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<char*>(&no_delay), &no_delay_size) == SOCKET_ERROR ||
        no_delay == 0) {
        ranker::CloseLegacyAsyncTcpSocket(*option_socket);
        return fail("async tcp socket did not enable TCP_NODELAY");
    }
    ranker::SetLegacyAsyncTcpSendBufferSize(*option_socket, -1);
    ranker::SetLegacyAsyncTcpReceiveBufferSize(*option_socket, -1);
    ranker::CloseLegacyAsyncTcpSocket(*option_socket);
    if (!ranker::WizardNetRelayCanDiscardStaleAsyncOpcode(0x27) ||
        ranker::WizardNetRelayCanDiscardStaleAsyncOpcode(0x2a)) {
        return fail("stale async opcode filter was not selective");
    }
    if (!ranker::FlushWizardNetRelayAsyncSendQueue()) {
        return fail("empty relay async send queue did not flush");
    }

    constexpr u32 game_id = 0x1234;
    const unsigned char join_payload[] = {0x41, 0x42, 0x43};
    std::vector<unsigned char> decoded_plain;
    if (!ranker::DecodeWizardNetRelayPayload(game_id, join_payload,
            sizeof(join_payload), decoded_plain) ||
        decoded_plain.size() != sizeof(join_payload) ||
        std::memcmp(decoded_plain.data(), join_payload, sizeof(join_payload)) != 0) {
        return fail("relay plaintext payload fallback decode failed");
    }
    if (!ranker::QueueWizardNetRelayJoin(game_id, join_payload,
            sizeof(join_payload))) {
        return fail("relay join packet was rejected");
    }
    if (const int result = expect_packet(ranker::kWizardNetRelayJoinRequestOpcode,
            0x0d + 4 + 28 + sizeof(join_payload));
        result != 0) {
        return result;
    }
    std::vector<unsigned char> decoded_join;
    if (read_u32(socket.send_buffer.data(), 0x0d) != game_id ||
        !ranker::WizardNetRelayPayloadIsEncrypted(socket.send_buffer.data() + 0x11,
            28 + sizeof(join_payload)) ||
        !ranker::DecodeWizardNetRelayPayload(game_id,
            socket.send_buffer.data() + 0x11, 28 + sizeof(join_payload),
        decoded_join) ||
        decoded_join.size() != sizeof(join_payload) ||
        std::memcmp(decoded_join.data(), join_payload, sizeof(join_payload)) != 0) {
        return fail("relay join payload was not encoded");
    }
    std::vector<unsigned char> fallback_join_wire(
        socket.send_buffer.data() + 0x11,
        socket.send_buffer.data() + 0x11 + 28 + sizeof(join_payload));
    std::vector<unsigned char> tampered_join(
        fallback_join_wire.begin(), fallback_join_wire.end());
    tampered_join.back() ^= 0x01;
    std::vector<unsigned char> decoded_tampered;
    if (ranker::DecodeWizardNetRelayPayload(game_id, tampered_join.data(),
            static_cast<u32>(tampered_join.size()), decoded_tampered)) {
        return fail("relay encrypted payload accepted tampering");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    if (ranker::QueueWizardNetRelayFrame(1, ranker::kWizardNetRelayStreamMode1,
            join_payload, sizeof(join_payload))) {
        return fail("relay frame was accepted while disabled");
    }
    if (socket.send_length != 0) {
        return fail("disabled relay frame queued bytes");
    }

    ranker::ConfigureWizardNetRelayState(game_id, 2, false);
    ranker::SetWizardNetRelayPlayerMember(3, 8);
    ranker::SetWizardNetRelayPlayerMember(4, 5);
    if (!ranker::WizardNetRelayEnabled() ||
        !ranker::WizardNetRelayReadyForGame(game_id) ||
        ranker::WizardNetRelayMemberForPlayer(3) != 8 ||
        ranker::WizardNetRelayPlayerForMember(5) != 4 ||
        ranker::WizardNetRelayDefaultTargetMember() !=
            ranker::kWizardNetRelayHostMember) {
        return fail("relay state was not configured");
    }
    if (ranker::wizardnet_relay_state().relay_secret_available) {
        return fail("relay state unexpectedly has a room secret");
    }
    ranker::ConfigureWizardNetRelayState(game_id, 1, true);
    if (ranker::WizardNetRelayDefaultTargetMember() !=
        ranker::kWizardNetRelayBroadcastMember) {
        return fail("relay host default target was not broadcast");
    }
    ranker::ConfigureWizardNetRelayState(game_id, 2, false);
    ranker::SetWizardNetRelayPlayerMember(3, 8);
    ranker::SetWizardNetRelayPlayerMember(4, 5);

    if (!ranker::QueueWizardNetRelayFrame(8, ranker::kWizardNetRelayStreamMode1,
            join_payload, sizeof(join_payload))) {
        return fail("relay frame packet was rejected");
    }
    if (const int result = expect_packet(ranker::kWizardNetRelayFrameRequestOpcode,
            0x0d + 12 + 28 + sizeof(join_payload));
        result != 0) {
        return result;
    }
    if (read_u32(socket.send_buffer.data(), 0x0d) != game_id ||
        read_u32(socket.send_buffer.data(), 0x11) != 8 ||
        read_u32(socket.send_buffer.data(), 0x15) !=
            ranker::kWizardNetRelayStreamMode1) {
        return fail("relay frame header was not encoded");
    }
    std::vector<unsigned char> decoded_frame;
    if (!ranker::WizardNetRelayPayloadIsEncrypted(socket.send_buffer.data() + 0x19,
            28 + sizeof(join_payload)) ||
        !ranker::DecodeWizardNetRelayPayload(game_id,
            socket.send_buffer.data() + 0x19, 28 + sizeof(join_payload),
            decoded_frame) ||
        decoded_frame.size() != sizeof(join_payload) ||
        std::memcmp(decoded_frame.data(), join_payload, sizeof(join_payload)) != 0) {
        return fail("relay frame encrypted payload was not encoded");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    std::array<unsigned char, ranker::kWizardNetRelaySecretBytes> room_secret{};
    for (std::size_t index = 0; index < room_secret.size(); ++index) {
        room_secret[index] = static_cast<unsigned char>(index * 7 + 3);
    }
    ranker::ConfigureWizardNetRelayState(game_id, 2, false,
        room_secret.data(), static_cast<u32>(room_secret.size()));
    if (!ranker::wizardnet_relay_state().relay_secret_available) {
        return fail("relay room secret was not configured");
    }
    if (!ranker::QueueWizardNetRelayFrame(8, ranker::kWizardNetRelayStreamMode1,
            join_payload, sizeof(join_payload))) {
        return fail("relay room-secret frame packet was rejected");
    }
    if (const int result = expect_packet(ranker::kWizardNetRelayFrameRequestOpcode,
            0x0d + 12 + 28 + sizeof(join_payload));
        result != 0) {
        return result;
    }
    std::vector<unsigned char> room_secret_wire(
        socket.send_buffer.data() + 0x19,
        socket.send_buffer.data() + 0x19 + 28 + sizeof(join_payload));
    std::vector<unsigned char> decoded_room_secret;
    if (!ranker::DecodeWizardNetRelayPayload(game_id, room_secret_wire.data(),
            static_cast<u32>(room_secret_wire.size()), decoded_room_secret) ||
        decoded_room_secret.size() != sizeof(join_payload) ||
        std::memcmp(decoded_room_secret.data(), join_payload,
            sizeof(join_payload)) != 0) {
        return fail("relay room-secret payload did not decode");
    }
    ranker::ResetWizardNetRelayState();
    std::vector<unsigned char> decoded_without_secret;
    if (ranker::DecodeWizardNetRelayPayload(game_id, room_secret_wire.data(),
            static_cast<u32>(room_secret_wire.size()), decoded_without_secret)) {
        return fail("relay room-secret payload decoded without the room secret");
    }
    ranker::ConfigureWizardNetRelayState(game_id, 2, false);
    ranker::SetWizardNetRelayPlayerMember(3, 8);
    ranker::SetWizardNetRelayPlayerMember(4, 5);

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    const std::vector<unsigned char> large_payload(0x5000, 0x5a);
    if (!ranker::QueueWizardNetRelayFrame(8, ranker::kWizardNetRelayStreamMode1,
            large_payload.data(), static_cast<u32>(large_payload.size()))) {
        return fail("large relay frame packet was rejected");
    }
    if (const int result = expect_packet(ranker::kWizardNetRelayFrameRequestOpcode,
            0x0d + 12 + 28 + static_cast<u32>(large_payload.size()));
        result != 0) {
        return result;
    }
    std::vector<unsigned char> decoded_large;
    if (!ranker::DecodeWizardNetRelayPayload(game_id,
            socket.send_buffer.data() + 0x19,
            28 + static_cast<u32>(large_payload.size()), decoded_large) ||
        decoded_large.size() != large_payload.size() ||
        std::memcmp(decoded_large.data(), large_payload.data(),
            large_payload.size()) != 0) {
        return fail("large relay frame payload was not encoded");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    socket.send_length = static_cast<i32>(ranker::kLegacyAsyncTcpQueueBytes - 4);
    if (ranker::QueueWizardNetRelayFrame(8, ranker::kWizardNetRelayStreamMode1,
            join_payload, sizeof(join_payload))) {
        return fail("relay frame accepted with no async send queue room");
    }
    if (socket.send_length != static_cast<i32>(ranker::kLegacyAsyncTcpQueueBytes - 4)) {
        return fail("overflowing relay frame changed the async send queue");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    TcpPair pair{};
    if (!make_loopback_tcp_pair(pair)) {
        return fail("loopback relay flush socket pair was not created");
    }
    const SOCKET saved_socket = socket.socket;
    socket.socket = pair.client;
    pair.client = INVALID_SOCKET;
    std::memset(socket.send_buffer.data(), 0xa5, socket.send_buffer.size());
    socket.send_length = static_cast<i32>(ranker::kLegacyAsyncTcpQueueBytes - 4);
    if (!ranker::QueueWizardNetRelayFrame(8, ranker::kWizardNetRelayStreamMode1,
            join_payload, sizeof(join_payload))) {
        close_socket_if_open(socket.socket);
        socket.socket = saved_socket;
        close_tcp_pair(pair);
        return fail("relay frame did not preflush a writable async queue");
    }
    if (socket.send_length < 0 ||
        socket.send_length > static_cast<i32>(ranker::kLegacyAsyncTcpQueueBytes)) {
        close_socket_if_open(socket.socket);
        socket.socket = saved_socket;
        close_tcp_pair(pair);
        return fail("relay preflush left an invalid async queue length");
    }
    close_socket_if_open(socket.socket);
    socket.socket = saved_socket;
    close_tcp_pair(pair);

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    ranker::ConfigureWizardNetRelayState(game_id, 2, false);
    constexpr int concurrent_thread_count = 4;
    constexpr int concurrent_frames_per_thread = 64;
    constexpr u32 concurrent_target_member = 8;
    const std::array<unsigned char, 4> concurrent_payload = {
        0xa0, 0xb1, 0xc2, 0xd3,
    };
    std::atomic<int> concurrent_failures{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < concurrent_thread_count; ++thread) {
        threads.emplace_back([&]() {
            for (int frame = 0; frame < concurrent_frames_per_thread; ++frame) {
                if (!ranker::QueueWizardNetRelayFrame(concurrent_target_member,
                        ranker::kWizardNetRelayStreamMode1,
                        concurrent_payload.data(),
                        static_cast<u32>(concurrent_payload.size()))) {
                    ++concurrent_failures;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    if (concurrent_failures.load() != 0) {
        return fail("concurrent relay frame queueing failed");
    }
    if (const int result = expect_relay_frame_queue(game_id,
            concurrent_target_member, ranker::kWizardNetRelayStreamMode1,
            concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size()),
            concurrent_thread_count * concurrent_frames_per_thread);
        result != 0) {
        return result;
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    ranker::ResetMode1ReliablePacketState();
    ranker::SetActiveNetworkTransportMode(0);
    ranker::SetMode1ReliableLocalPlayerIndex(2);
    for (u32 slot = 0; slot < ranker::kPlayerSlotCount; ++slot) {
        ranker::SetMode1ReliablePlayerStatus(slot,
            static_cast<u8>(ranker::PlayerSlotState::disabled));
    }
    ranker::SetMode1ReliablePlayerStatus(0,
        static_cast<u8>(ranker::PlayerSlotState::active));
    ranker::SetMode1ReliablePlayerStatus(2,
        static_cast<u8>(ranker::PlayerSlotState::active));
    ranker::SetMode1ReliablePlayerStatus(3,
        static_cast<u8>(ranker::PlayerSlotState::active));
    ranker::ConfigureWizardNetRelayState(game_id, 2, false);
    ranker::ClearWizardNetRelayPlayerMembers();
    ranker::SetWizardNetRelayPlayerMember(0, 1);
    ranker::SetWizardNetRelayPlayerMember(2, 2);
    ranker::SetWizardNetRelayPlayerMember(3, 8);
    if (ranker::SendMode1ReliablePayloadToPlayerDefault(
            concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size()), 2) != 0 ||
        socket.send_length != 0) {
        return fail("mode1 relay self-send queued bytes");
    }
    if (ranker::SendMode1ReliablePayloadToPlayerDefault(
            concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size()), 3) != 0) {
        return fail("mode1 relay targeted send failed");
    }
    if (const int result = expect_relay_frame_targets(game_id, {8},
            ranker::kWizardNetRelayStreamMode1, concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size()));
        result != 0) {
        return result;
    }
    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    if (ranker::SendMode1ReliablePayloadToPlayerDefault(
            concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size()), 4) != -1 ||
        socket.send_length != 0) {
        return fail("mode1 relay accepted an unmapped player target");
    }
    if (ranker::BroadcastMode1ReliablePayloadDefault(concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size())) != 0) {
        return fail("mode1 relay broadcast failed");
    }
    if (const int result = expect_relay_frame_targets(game_id, {1, 8},
            ranker::kWizardNetRelayStreamMode1, concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size()));
        result != 0) {
        return result;
    }
    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    ranker::SetMode1ReliableLocalPlayerIndex(0);
    for (u32 slot = 0; slot < ranker::kPlayerSlotCount; ++slot) {
        ranker::SetMode1ReliablePlayerStatus(slot,
            static_cast<u8>(ranker::PlayerSlotState::disabled));
    }
    ranker::SetMode1ReliablePlayerStatus(0,
        static_cast<u8>(ranker::PlayerSlotState::active));
    ranker::SetMode1ReliablePlayerStatus(1,
        static_cast<u8>(ranker::PlayerSlotState::active));
    ranker::SetMode1ReliablePlayerStatus(3,
        static_cast<u8>(ranker::PlayerSlotState::active));
    ranker::ConfigureWizardNetRelayState(game_id, 1, true);
    ranker::ClearWizardNetRelayPlayerMembers();
    ranker::SetWizardNetRelayPlayerMember(0, 1);
    ranker::SetWizardNetRelayPlayerMember(1, 2);
    ranker::SetWizardNetRelayPlayerMember(3, 8);
    if (ranker::BroadcastMode1ReliablePayloadDefault(concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size())) != 0) {
        return fail("mode1 host relay broadcast failed");
    }
    if (const int result = expect_relay_frame_targets(game_id, {2, 8},
            ranker::kWizardNetRelayStreamMode1, concurrent_payload.data(),
            static_cast<u32>(concurrent_payload.size()));
        result != 0) {
        return result;
    }
    ranker::SetActiveNetworkTransportMode(-1);

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    constexpr u32 stale_game_id = game_id + 0x100;
    if (!ranker::QueueWizardNetRelayLeaveForGame(stale_game_id)) {
        return fail("targeted stale relay leave packet was rejected");
    }
    if (!ranker::WizardNetRelayEnabled() ||
        !ranker::WizardNetRelayReadyForGame(game_id)) {
        return fail("targeted stale relay leave reset the active state");
    }
    if (const int result = expect_packet(ranker::kWizardNetRelayLeaveRequestOpcode,
            0x0d + 4);
        result != 0) {
        return result;
    }
    if (read_u32(socket.send_buffer.data(), 0x0d) != stale_game_id) {
        return fail("targeted stale relay leave game id was not encoded");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    if (!ranker::QueueWizardNetRelayLeaveForGame(game_id)) {
        return fail("targeted relay leave packet was rejected");
    }
    if (ranker::WizardNetRelayEnabled()) {
        return fail("targeted relay leave did not reset local state");
    }
    if (const int result = expect_packet(ranker::kWizardNetRelayLeaveRequestOpcode,
            0x0d + 4);
        result != 0) {
        return result;
    }
    if (read_u32(socket.send_buffer.data(), 0x0d) != game_id) {
        return fail("targeted relay leave game id was not encoded");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    if (ranker::QueueWizardNetRelayLeave()) {
        return fail("disabled relay leave queued a zero-game packet");
    }
    if (socket.send_length != 0) {
        return fail("disabled relay leave changed the async send queue");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    network.udp_receive_queue.clear();
    ranker::ConfigureWizardNetRelayState(game_id, 2, false,
        room_secret.data(), static_cast<u32>(room_secret.size()));
    std::vector<unsigned char> fallback_mode1_body(
        12 + fallback_join_wire.size(), 0);
    write_u32(fallback_mode1_body, 0, game_id);
    write_u32(fallback_mode1_body, 4, 5);
    write_u32(fallback_mode1_body, 8, ranker::kWizardNetRelayStreamMode1);
    std::memcpy(fallback_mode1_body.data() + 12, fallback_join_wire.data(),
        fallback_join_wire.size());
    const std::vector<unsigned char> fallback_mode1_frame = build_async_packet(
        ranker::kWizardNetRelayFrameOpcode, fallback_mode1_body.data(),
        fallback_mode1_body.size());
    std::memcpy(socket.receive_buffer.data(), fallback_mode1_frame.data(),
        fallback_mode1_frame.size());
    socket.receive_length = static_cast<i32>(fallback_mode1_frame.size());
    if (!ranker::PumpWizardNetRelayMode1Frames()) {
        return fail("relay mode1 pump did not consume fallback-encrypted frame");
    }
    if (socket.receive_length != 0) {
        return fail("relay mode1 fallback rejection left bytes in the queue");
    }
    if (!network.udp_receive_queue.empty()) {
        return fail("relay mode1 accepted fallback payload with room secret");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    network.udp_receive_queue.clear();
    ranker::ConfigureWizardNetRelayState(game_id, 2, false);
    const unsigned char mode1_payload[] = {0x10, 0x20, 0x30, 0x40};
    if (!ranker::QueueWizardNetRelayFrame(5, ranker::kWizardNetRelayStreamMode1,
            mode1_payload, sizeof(mode1_payload))) {
        return fail("encrypted mode1 relay payload was not queued");
    }
    const u32 encrypted_mode1_bytes =
        read_u32(socket.send_buffer.data(), 8) - 0x19;
    std::vector<unsigned char> encrypted_mode1_payload(encrypted_mode1_bytes);
    std::memcpy(encrypted_mode1_payload.data(), socket.send_buffer.data() + 0x19,
        encrypted_mode1_payload.size());
    ranker::ResetLegacyAsyncTcpSocketQueues(socket);

    std::vector<unsigned char> frame_body(12 + encrypted_mode1_payload.size(), 0);
    write_u32(frame_body, 0, game_id);
    write_u32(frame_body, 4, 5);
    write_u32(frame_body, 8, ranker::kWizardNetRelayStreamMode1);
    std::memcpy(frame_body.data() + 12, encrypted_mode1_payload.data(),
        encrypted_mode1_payload.size());
    const std::vector<unsigned char> relay_frame = build_async_packet(
        ranker::kWizardNetRelayFrameOpcode, frame_body.data(), frame_body.size());
    std::memcpy(socket.receive_buffer.data(), relay_frame.data(),
        relay_frame.size());
    socket.receive_length = static_cast<i32>(relay_frame.size());
    if (!ranker::PumpWizardNetRelayMode1Frames()) {
        return fail("relay mode1 pump did not consume a frame");
    }
    if (socket.receive_length != 0) {
        return fail("relay mode1 pump left bytes in the async queue");
    }
    if (network.udp_receive_queue.size() != sizeof(mode1_payload) ||
        std::memcmp(network.udp_receive_queue.data(), mode1_payload,
            sizeof(mode1_payload)) != 0) {
        return fail("relay mode1 pump did not enqueue the payload");
    }
    if (ntohl(network.udp_last_sender.sin_addr.s_addr) != 0x0a640005u ||
        ntohs(network.udp_last_sender.sin_port) != 0x7005u) {
        return fail("relay mode1 pump did not synthesize sender address");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    std::vector<unsigned char> member_left_body(8, 0);
    write_u32(member_left_body, 0, game_id);
    write_u32(member_left_body, 4, 5);
    const std::vector<unsigned char> member_left = build_async_packet(
        ranker::kWizardNetRelayMemberLeftOpcode, member_left_body.data(),
        member_left_body.size());
    std::memcpy(socket.receive_buffer.data(), member_left.data(),
        member_left.size());
    socket.receive_length = static_cast<i32>(member_left.size());
    if (!ranker::PumpWizardNetRelayMode1Frames()) {
        return fail("relay mode1 pump did not consume member-left");
    }
    unsigned left_game = 0;
    unsigned left_member = 0;
    if (!ranker::TakeWizardNetRelayMemberLeft(left_game, left_member) ||
        left_game != game_id || left_member != 5) {
        return fail("relay member-left event was not queued");
    }
    if (ranker::TakeWizardNetRelayMemberLeft(left_game, left_member)) {
        return fail("relay member-left event was queued more than once");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    network.udp_receive_queue.clear();
    const std::vector<unsigned char> stale_game_update =
        build_async_packet(0x27, mode1_payload, sizeof(mode1_payload));
    std::memcpy(socket.receive_buffer.data(), stale_game_update.data(),
        stale_game_update.size());
    std::memcpy(socket.receive_buffer.data() + stale_game_update.size(),
        relay_frame.data(), relay_frame.size());
    socket.receive_length = static_cast<i32>(
        stale_game_update.size() + relay_frame.size());
    if (!ranker::PumpWizardNetRelayMode1Frames()) {
        return fail("relay pump did not consume stale async packet");
    }
    if (socket.receive_length != 0) {
        return fail("relay pump left bytes behind stale async packet");
    }
    if (network.udp_receive_queue.size() != sizeof(mode1_payload) ||
        std::memcmp(network.udp_receive_queue.data(), mode1_payload,
            sizeof(mode1_payload)) != 0) {
        return fail("relay pump did not process frame after stale async packet");
    }

    ranker::ResetLegacyAsyncTcpSocketQueues(socket);
    network.udp_receive_queue.clear();
    const std::vector<unsigned char> join_status = build_async_packet(
        ranker::kWizardNetRelayJoinStatusOpcode, frame_body.data(), 12);
    const std::vector<unsigned char> unrelated =
        build_async_packet(0x2a, mode1_payload, sizeof(mode1_payload));
    std::memcpy(socket.receive_buffer.data(), join_status.data(),
        join_status.size());
    std::memcpy(socket.receive_buffer.data() + join_status.size(),
        unrelated.data(), unrelated.size());
    socket.receive_length = static_cast<i32>(
        join_status.size() + unrelated.size());
    if (!ranker::PumpWizardNetRelayMode1Frames()) {
        return fail("relay pump did not consume relay status");
    }
    if (socket.receive_length != static_cast<i32>(unrelated.size()) ||
        read_u32(socket.receive_buffer.data(), 4) != 0x2a) {
        return fail("relay pump consumed unrelated async packet");
    }

    std::printf("wizardnet relay regression passed\n");
    return 0;
}

#else

int main() {
    return 0;
}

#endif
