#include "ranker_network.h"

#ifdef _WIN32

#include <mmsystem.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ranker {
namespace {

LegacyNetworkRuntimeState g_network_state;
LegacyAsyncTcpSocket g_frontend_async_tcp_socket0;
LegacyAsyncTcpSocket g_frontend_async_tcp_socket1;
bool g_frontend_async_tcp_socket0_initialized = false;
bool g_frontend_async_tcp_socket1_initialized = false;
bool g_frontend_async_tcp_socket0_destructor_registered = false;
bool g_frontend_async_tcp_socket1_destructor_registered = false;

struct LegacyAsyncTcpTransferCounters {
    u32 interval_bytes = 0;
    u32 total_bytes = 0;
    u32 interval_tick = 0;
    u32 total_tick = 0;
};

LegacyAsyncTcpTransferCounters g_async_tcp_counters;

#ifndef SO_MAX_MSG_SIZE
constexpr int kSoMaxMessageSize = 0x2003;
#else
constexpr int kSoMaxMessageSize = SO_MAX_MSG_SIZE;
#endif

constexpr const char* kIpv4RouteProbeAddress = "8.8.8.8";

bool configured_bind_address(sockaddr_in& address) {
    const char* override_address =
        std::getenv("RANKER_RECONSTRUCTED_BIND_ADDRESS");
    if (override_address == nullptr || override_address[0] == '\0') {
        return false;
    }

    const unsigned long parsed = inet_addr(override_address);
    if (parsed == INADDR_NONE) {
        return false;
    }

    address = sockaddr_in{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = parsed;
    return true;
}

bool resolve_default_route_ipv4_address(sockaddr_in& address) {
    const SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(9);
    target.sin_addr.s_addr = inet_addr(kIpv4RouteProbeAddress);
    const bool connected = connect(probe,
        reinterpret_cast<const sockaddr*>(&target), sizeof(target)) != SOCKET_ERROR;

    sockaddr_in local{};
    int local_size = sizeof(local);
    const bool resolved = connected && getsockname(probe,
        reinterpret_cast<sockaddr*>(&local), &local_size) != SOCKET_ERROR &&
        local.sin_family == AF_INET && local.sin_addr.s_addr != INADDR_ANY &&
        local.sin_addr.s_addr != INADDR_NONE;
    closesocket(probe);
    if (!resolved) {
        return false;
    }

    address = local;
    return true;
}

void write_le32(u8* data, std::size_t offset, u32 value) {
    data[offset + 0] = static_cast<u8>(value & 0xffu);
    data[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
    data[offset + 2] = static_cast<u8>((value >> 16) & 0xffu);
    data[offset + 3] = static_cast<u8>((value >> 24) & 0xffu);
}

void clear_record(LegacySocketRecord& record) {
    record.active = false;
    record.socket = INVALID_SOCKET;
    record.peer_address = sockaddr_in{};
    record.receive_queue.clear();
    record.send_queue.clear();
}

void clear_socket_table() {
    for (auto& record : g_network_state.socket_records) {
        clear_record(record);
    }
    g_network_state.active_socket_count = 0;
}

LegacySocketRecord* first_free_socket_record() {
    for (auto& record : g_network_state.socket_records) {
        if (!record.active) {
            return &record;
        }
    }
    return nullptr;
}

void shutdown_send_and_close_socket(SOCKET socket) {
    shutdown(socket, 1);
    closesocket(socket);
}

void close_socket_handle(SOCKET socket) {
    if (socket != INVALID_SOCKET) {
        shutdown_send_and_close_socket(socket);
    }
}

void set_async_tcp_status(LegacyAsyncTcpSocket& socket_state,
    LegacyAsyncTcpStatus status) {
    socket_state.status = status;
}

void register_atexit_once(bool& registered, void (*callback)()) {
    if (registered) {
        return;
    }
    std::atexit(callback);
    registered = true;
}

void close_udp_socket_handle() {
    if (g_network_state.udp_socket == INVALID_SOCKET) {
        return;
    }

    shutdown(g_network_state.udp_socket, 2);
    closesocket(g_network_state.udp_socket);
    g_network_state.udp_socket = INVALID_SOCKET;
}

void close_udp_socket_start_failure_send_side_only() {
    if (g_network_state.udp_socket == INVALID_SOCKET) {
        return;
    }

    shutdown(g_network_state.udp_socket, 1);
    closesocket(g_network_state.udp_socket);
}

bool append_to_send_queue(LegacySocketRecord& record, const void* data,
    u32 byte_count) {
    if (byte_count == 0) {
        return true;
    }
    if (record.send_queue.size() + byte_count >= kLegacySocketQueueBytes) {
        return false;
    }

    const auto* bytes = static_cast<const u8*>(data);
    record.send_queue.insert(record.send_queue.end(), bytes, bytes + byte_count);
    return true;
}

bool flush_send_queue(LegacySocketRecord& record) {
    if (record.send_queue.empty()) {
        return true;
    }

    const int queued = static_cast<int>(record.send_queue.size());
    const int sent = send(record.socket,
        reinterpret_cast<const char*>(record.send_queue.data()), queued, 0);
    if (sent == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK;
    }

    if (sent > 0) {
        const auto erase_count = static_cast<std::size_t>(
            std::min(sent, queued));
        record.send_queue.erase(record.send_queue.begin(),
            record.send_queue.begin() + erase_count);
    }
    return true;
}

void close_record_without_lookup(LegacySocketRecord& record) {
    const SOCKET socket = record.socket;
    if (record.active && g_network_state.active_socket_count != 0) {
        --g_network_state.active_socket_count;
    }
    record.active = false;
    record.receive_queue.clear();
    record.send_queue.clear();
    shutdown_send_and_close_socket(socket);
}

bool select_async_socket_events(SOCKET socket, HWND notify_window,
    UINT notify_message, long events) {
    return WSAAsyncSelect(socket, notify_window, notify_message, events) !=
        SOCKET_ERROR;
}

void write_legacy_ipv4_sockaddr(sockaddr_in& address, const char* dotted_address,
    u16 port) {
    std::memset(&address, 0, 4);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr(dotted_address);
}

void write_debug_stack_ipv4_sockaddr(sockaddr_in& address,
    const char* dotted_address, u16 port) {
    std::memset(&address, 0xcc, sizeof(address));
    write_legacy_ipv4_sockaddr(address, dotted_address, port);
}

}

LegacyNetworkRuntimeState& legacy_network_state() {
    return g_network_state;
}

LegacyAsyncTcpSocket& FrontendAsyncTcpSocket0() {
    if (!g_frontend_async_tcp_socket0_initialized) {
        InitializeFrontendAsyncTcpSocket0Static();
    }
    return g_frontend_async_tcp_socket0;
}

LegacyAsyncTcpSocket& FrontendAsyncTcpSocket1() {
    if (!g_frontend_async_tcp_socket1_initialized) {
        InitializeFrontendAsyncTcpSocket1Static();
    }
    return g_frontend_async_tcp_socket1;
}

void InitializeFrontendAsyncTcpSocket0Static() {
    InitializeFrontendAsyncTcpSocket0();
    RegisterFrontendAsyncTcpSocket0Destructor();
}

void InitializeFrontendAsyncTcpSocket0() {
    if (g_frontend_async_tcp_socket0_initialized) {
        return;
    }
    InitializeFrontendAsyncTcpSocketObject(g_frontend_async_tcp_socket0);
    g_frontend_async_tcp_socket0_initialized = true;
}

void RegisterFrontendAsyncTcpSocket0Destructor() {
    register_atexit_once(g_frontend_async_tcp_socket0_destructor_registered,
        DestroyFrontendAsyncTcpSocket0);
}

void DestroyFrontendAsyncTcpSocket0() {
    if (!g_frontend_async_tcp_socket0_initialized) {
        return;
    }
    DestroyFrontendAsyncTcpSocketObject(g_frontend_async_tcp_socket0);
    g_frontend_async_tcp_socket0_initialized = false;
}

void DestroyFrontendAsyncTcpSocketObject(LegacyAsyncTcpSocket& socket_state) {
    DeleteLegacyAsyncTcpSocket(socket_state);
}

LegacyAsyncTcpSocket& InitializeFrontendAsyncTcpSocketObject(
    LegacyAsyncTcpSocket& socket_state) {
    InitializeLegacyAsyncTcpSocket(socket_state);
    return socket_state;
}

void DeleteFrontendAsyncTcpSocketObject(LegacyAsyncTcpSocket* socket_state,
    bool free_storage) {
    if (socket_state != nullptr) {
        DestroyFrontendAsyncTcpSocketObject(*socket_state);
    }
    if (free_storage) {
        ::operator delete(socket_state);
    }
}

void InitializeFrontendAsyncTcpSocket1Static() {
    InitializeFrontendAsyncTcpSocket1();
    RegisterFrontendAsyncTcpSocket1Destructor();
}

void InitializeFrontendAsyncTcpSocket1() {
    if (g_frontend_async_tcp_socket1_initialized) {
        return;
    }
    InitializeLegacyAsyncTcpSocket(g_frontend_async_tcp_socket1);
    g_frontend_async_tcp_socket1_initialized = true;
}

void RegisterFrontendAsyncTcpSocket1Destructor() {
    register_atexit_once(g_frontend_async_tcp_socket1_destructor_registered,
        DestroyFrontendAsyncTcpSocket1);
}

void DestroyFrontendAsyncTcpSocket1() {
    if (!g_frontend_async_tcp_socket1_initialized) {
        return;
    }
    DeleteLegacyAsyncTcpSocket(g_frontend_async_tcp_socket1);
    g_frontend_async_tcp_socket1_initialized = false;
}

bool EnsureLegacyWinSockStartup(u8 major, u8 minor) {
    if (g_network_state.winsock_started) {
        return true;
    }

    WSADATA data{};
    const WORD version = MAKEWORD(major, minor);
    const int result = WSAStartup(version, &data);
    if (result != 0) {
        return false;
    }

    g_network_state.winsock_started = true;
    return true;
}

void ShutdownLegacyWinSock() {
    if (!g_network_state.winsock_started) {
        return;
    }

    WSACleanup();
    g_network_state.winsock_started = false;
}

bool IsPrivateIpv4Address(const in_addr& address) {
    const auto* bytes = reinterpret_cast<const u8*>(&address);
    if (bytes[0] == 10) {
        return true;
    }
    if (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] < 32) {
        return true;
    }
    return bytes[0] == 192 && bytes[1] == 168;
}

bool ResolveLocalHostDisplayAddress(char* host_name_out, u32 host_name_size,
    char* address_out, u32 address_size) {
    if (host_name_out == nullptr || host_name_size == 0 ||
        address_out == nullptr || address_size == 0) {
        return false;
    }
    if (!EnsureLegacyWinSockStartup(2, 0)) {
        return false;
    }

    // Keep the original host-name selection in normal runs.  The companion
    // port-offset hook is used by the P2P regressions to run both peers on one
    // machine; allow those tests to bind the reconstructed peer to a distinct
    // loopback endpoint so its UDP socket and advertised route stay in the
    // same address family as the original process.
    sockaddr_in resolved_address{};
    if (configured_bind_address(resolved_address)) {
        const char* override_address = inet_ntoa(resolved_address.sin_addr);
        std::snprintf(host_name_out, host_name_size, "%s", override_address);
        std::snprintf(address_out, address_size, "%s", override_address);
        return true;
    }

    const int host_buffer_size = static_cast<int>(
        std::min<u32>(host_name_size, 0x80));
    if (gethostname(host_name_out, host_buffer_size) == SOCKET_ERROR) {
        return false;
    }
    host_name_out[host_name_size - 1] = '\0';

    hostent* host = gethostbyname(host_name_out);
    if (host == nullptr) {
        return false;
    }
    std::snprintf(host_name_out, host_name_size, "%s", host->h_name);

    if (!ResolveLocalHostIpv4Address(resolved_address)) {
        return false;
    }
    const char* text = inet_ntoa(resolved_address.sin_addr);
    if (text == nullptr) {
        return false;
    }
    std::snprintf(address_out, address_size, "%s", text);
    return true;
}

bool InitializeLegacyUdpNetworking() {
    if (g_network_state.udp_initialized) {
        return true;
    }
    if (!EnsureLegacyWinSockStartup(2, 0)) {
        return false;
    }

    g_network_state.udp_socket = INVALID_SOCKET;
    g_network_state.udp_initialized = true;
    return true;
}

void ShutdownLegacyUdpNetworking() {
    if (!g_network_state.udp_initialized) {
        return;
    }

    CloseLegacyUdpSocket();
    g_network_state.udp_initialized = false;
}

SOCKET StartLegacyUdpSocket(const char* bind_address, u16 port) {
    if (!InitializeLegacyUdpNetworking()) {
        return INVALID_SOCKET;
    }

    CloseLegacyUdpSocket();
    g_network_state.udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_network_state.udp_socket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    int buffer_bytes = kLegacyUdpSocketBufferBytes;
    if (setsockopt(g_network_state.udp_socket, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<const char*>(&buffer_bytes), sizeof(buffer_bytes)) ==
        SOCKET_ERROR) {
        close_udp_socket_start_failure_send_side_only();
        return INVALID_SOCKET;
    }
    if (setsockopt(g_network_state.udp_socket, SOL_SOCKET, SO_SNDBUF,
            reinterpret_cast<const char*>(&buffer_bytes), sizeof(buffer_bytes)) ==
        SOCKET_ERROR) {
        close_udp_socket_start_failure_send_side_only();
        return INVALID_SOCKET;
    }

    int payload_limit = 0;
    int option_size = sizeof(payload_limit);
    if (getsockopt(g_network_state.udp_socket, SOL_SOCKET, kSoMaxMessageSize,
            reinterpret_cast<char*>(&payload_limit), &option_size) == SOCKET_ERROR) {
        close_udp_socket_start_failure_send_side_only();
        return INVALID_SOCKET;
    }
    u32 udp_payload_limit = static_cast<u32>(payload_limit);
    if (udp_payload_limit > 0x20u) {
        udp_payload_limit -= 0x20u;
    }
    // SO_MAX_MSG_SIZE reports the largest legal UDP message (normally close
    // to 64 KiB), not a size that can cross the Internet without IP
    // fragmentation.  Reliable mode-1 catch-up ranges can grow large after a
    // short stall; sending the whole range at that limit makes one lost IP
    // fragment discard dozens of otherwise recoverable 0x24-byte packets.
    // Datagram boundaries are not semantic to the receiver, so split at a
    // conservative packet-aligned size instead.
    g_network_state.udp_payload_limit =
        udp_payload_limit == 0 ?
        kLegacyUdpSafePayloadBytes :
        std::min(udp_payload_limit, kLegacyUdpSafePayloadBytes);

    u_long nonblocking = 1;
    if (ioctlsocket(g_network_state.udp_socket, FIONBIO, &nonblocking) ==
        SOCKET_ERROR) {
        return INVALID_SOCKET;
    }

    write_legacy_ipv4_sockaddr(g_network_state.udp_bind_address, bind_address,
        port);
    if (bind(g_network_state.udp_socket,
            reinterpret_cast<sockaddr*>(&g_network_state.udp_bind_address),
            sizeof(g_network_state.udp_bind_address)) == SOCKET_ERROR) {
        return INVALID_SOCKET;
    }

    if (!RefreshLegacyUdpLocalAddress()) {
        return INVALID_SOCKET;
    }
    return g_network_state.udp_socket;
}

void CloseLegacyUdpSocket() {
    g_network_state.udp_receive_queue.clear();
    if (g_network_state.udp_socket != INVALID_SOCKET) {
        DrainLegacyUdpReceiveQueue();
        close_udp_socket_handle();
    }
}

bool RefreshLegacyUdpLocalAddress() {
    char host_name[0x200];
    gethostname(host_name, sizeof(host_name));
    host_name[sizeof(host_name) - 1] = '\0';

    int address_size = sizeof(g_network_state.udp_bind_address);
    if (getsockname(g_network_state.udp_socket,
            reinterpret_cast<sockaddr*>(&g_network_state.udp_bind_address),
            &address_size) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

void DrainLegacyUdpReceiveQueue() {
    g_network_state.udp_receive_queue.clear();
    while (true) {
        const i32 received = ReceiveLegacyUdpPacket();
        if (received <= 0) {
            break;
        }
        ConsumeLegacyUdpReceiveQueue(static_cast<u32>(received));
    }
    g_network_state.udp_receive_queue.clear();
}

i32 ReceiveLegacyUdpPacket() {
    if (g_network_state.udp_receive_queue.size() >= kLegacyUdpReceiveQueueBytes) {
        return static_cast<i32>(g_network_state.udp_receive_queue.size());
    }

    // recvfrom operates on one complete datagram.  Receiving directly into
    // the unused tail of the accumulated stream made the available buffer
    // smaller after every queued packet.  A large original-client catch-up
    // datagram would then fail with WSAEMSGSIZE and be discarded.  Receive at
    // the wire maximum first and append only after the datagram is complete.
    std::array<u8, kLegacyUdpDatagramBytes> datagram;
    int sender_size = sizeof(g_network_state.udp_last_sender);
    const int received = recvfrom(g_network_state.udp_socket,
        reinterpret_cast<char*>(datagram.data()),
        static_cast<int>(datagram.size()), 0,
        reinterpret_cast<sockaddr*>(&g_network_state.udp_last_sender), &sender_size);
    if (received == SOCKET_ERROR) {
        return static_cast<i32>(g_network_state.udp_receive_queue.size());
    }

    const std::size_t received_bytes = static_cast<std::size_t>(received);
    if (received_bytes > kLegacyUdpReceiveQueueBytes -
            g_network_state.udp_receive_queue.size()) {
        return static_cast<i32>(g_network_state.udp_receive_queue.size());
    }
    g_network_state.udp_receive_queue.insert(
        g_network_state.udp_receive_queue.end(), datagram.begin(),
        datagram.begin() + received_bytes);
    return static_cast<i32>(g_network_state.udp_receive_queue.size());
}

void ConsumeLegacyUdpReceiveQueue(u32 byte_count) {
    if (byte_count > g_network_state.udp_receive_queue.size()) {
        return;
    }

    g_network_state.udp_receive_queue.erase(g_network_state.udp_receive_queue.begin(),
        g_network_state.udp_receive_queue.begin() + byte_count);
}

bool SendLegacyUdpChunks(u32 byte_count, const void* data,
    const sockaddr_in& target_address) {
    auto* cursor = static_cast<const u8*>(data);
    u32 remaining = byte_count;
    int last_result = 0;
    while (remaining != 0) {
        u32 send_bytes = remaining;
        if (g_network_state.udp_payload_limit != 0 &&
            remaining >= g_network_state.udp_payload_limit) {
            send_bytes = g_network_state.udp_payload_limit -
                (g_network_state.udp_payload_limit % 0x24);
        }
        if (send_bytes == 0) {
            send_bytes = remaining;
        }

        u32 attempt = 0;
        for (;;) {
            last_result = sendto(g_network_state.udp_socket,
                reinterpret_cast<const char*>(cursor), static_cast<int>(send_bytes), 0,
                reinterpret_cast<const sockaddr*>(&target_address),
                sizeof(target_address));
            if (last_result != SOCKET_ERROR) {
                break;
            }

            const int error = WSAGetLastError();
            const bool transient =
                error == WSAEWOULDBLOCK || error == WSAENOBUFS;
            ++attempt;
            if (!transient || attempt >= kLegacyUdpSendAttemptCount) {
                return false;
            }

            // A nonblocking UDP socket can briefly reject a burst while its
            // kernel send queue drains.  Returning success here discarded this
            // chunk (and every later chunk) even though the reliable layer
            // advanced as if the range had been transmitted.
            Sleep(kLegacyUdpSendRetryDelayMs);
        }

        remaining -= send_bytes;
        cursor += send_bytes;
    }
    return true;
}

sockaddr_in BuildLegacyUdpSockaddr(const char* dotted_address, u16 port) {
    sockaddr_in address{};
    write_legacy_ipv4_sockaddr(address, dotted_address, port);
    return address;
}

bool InitializeLegacyTcpNetworking() {
    if (g_network_state.tcp_initialized) {
        return true;
    }
    if (!EnsureLegacyWinSockStartup(2, 0)) {
        return false;
    }

    g_network_state.listen_socket = INVALID_SOCKET;
    g_network_state.active_socket_count = 0;
    for (auto& record : g_network_state.socket_records) {
        record.active = false;
        record.socket = INVALID_SOCKET;
        record.receive_queue.clear();
        record.send_queue.clear();
    }
    g_network_state.tcp_initialized = true;
    return true;
}

void ShutdownLegacyTcpNetworking() {
    if (!g_network_state.tcp_initialized) {
        return;
    }

    CloseAllLegacySocketRecords();
    g_network_state.tcp_initialized = false;
}

LegacySocketRecord* FindLegacySocketRecord(SOCKET socket) {
    for (auto& record : g_network_state.socket_records) {
        if (record.active && record.socket == socket) {
            return &record;
        }
    }
    return nullptr;
}

void RegisterLegacySocketRecord(SOCKET socket,
    const sockaddr_in* peer_address) {
    // The original helper does not de-duplicate sockets before taking a slot.
    if (g_network_state.active_socket_count <=
        static_cast<i32>(kLegacySocketRecordCount)) {
        if (auto* record = first_free_socket_record()) {
            record->socket = socket;
            record->peer_address = sockaddr_in{};
            if (peer_address != nullptr) {
                record->peer_address = *peer_address;
            }
            else {
                int peer_size = sizeof(record->peer_address);
                if (getpeername(socket,
                        reinterpret_cast<sockaddr*>(&record->peer_address),
                        &peer_size) == SOCKET_ERROR) {
                    record->peer_address = sockaddr_in{};
                }
            }
            record->active = true;
            record->receive_queue.clear();
            record->send_queue.clear();
            ++g_network_state.active_socket_count;
            return;
        }
    }

    shutdown_send_and_close_socket(socket);
}

void CloseLegacySocketRecord(SOCKET socket) {
    for (auto& record : g_network_state.socket_records) {
        if (record.active && record.socket == socket) {
            record.active = false;
            record.receive_queue.clear();
            record.send_queue.clear();
            --g_network_state.active_socket_count;
        }
    }

    shutdown_send_and_close_socket(socket);
}

void CloseAllLegacySocketRecords() {
    for (auto& record : g_network_state.socket_records) {
        if (record.active) {
            close_record_without_lookup(record);
        }
    }

    g_network_state.active_socket_count = 0;
    if (g_network_state.listen_socket_active) {
        shutdown_send_and_close_socket(g_network_state.listen_socket);
    }
    g_network_state.listen_socket_active = false;
}

bool StartLegacyListenSocket(u16 port, HWND notify_window, UINT notify_message) {
    if (!InitializeLegacyTcpNetworking()) {
        return false;
    }

    clear_socket_table();
    g_network_state.listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_network_state.listen_socket == INVALID_SOCKET) {
        return false;
    }

    g_network_state.listen_address.sin_family = AF_INET;
    g_network_state.listen_address.sin_port = htons(port);
    g_network_state.listen_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_network_state.listen_socket,
            reinterpret_cast<sockaddr*>(&g_network_state.listen_address),
            sizeof(g_network_state.listen_address)) == SOCKET_ERROR) {
        return false;
    }

    if (listen(g_network_state.listen_socket, 5) == SOCKET_ERROR) {
        return false;
    }

    if (!select_async_socket_events(g_network_state.listen_socket, notify_window,
            notify_message, FD_ACCEPT)) {
        return false;
    }

    g_network_state.listen_socket_active = true;
    return true;
}

bool AcceptLegacySocketConnection(HWND notify_window, UINT notify_message) {
    sockaddr_in peer{};
    int peer_size = sizeof(peer);
    const SOCKET accepted = accept(g_network_state.listen_socket,
        reinterpret_cast<sockaddr*>(&peer), &peer_size);
    if (accepted == INVALID_SOCKET) {
        return false;
    }

    LegacySocketRecord* record = nullptr;
    if (g_network_state.active_socket_count <=
        static_cast<i32>(kLegacySocketRecordCount)) {
        record = first_free_socket_record();
    }
    if (record == nullptr) {
        shutdown_send_and_close_socket(accepted);
        return false;
    }

    // Accepted sockets intentionally do not increment active_socket_count in
    // the original table path; CloseLegacySocketRecord can skew it negative.
    record->socket = accepted;
    record->peer_address = peer;
    record->active = true;

    if (!select_async_socket_events(accepted, notify_window, notify_message,
            FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE)) {
        shutdown_send_and_close_socket(accepted);
        return false;
    }
    return true;
}

bool ResolveLocalHostIpv4Address(sockaddr_in& address) {
    address = sockaddr_in{};
    if (!EnsureLegacyWinSockStartup(2, 0)) {
        return false;
    }
    if (configured_bind_address(address) ||
        resolve_default_route_ipv4_address(address)) {
        return true;
    }

    char host_name[0x80]{};
    if (gethostname(host_name, sizeof(host_name) - 1) == SOCKET_ERROR) {
        return false;
    }

    hostent* host = gethostbyname(host_name);
    if (host == nullptr || host->h_addr_list == nullptr) {
        return false;
    }

    for (char** current = host->h_addr_list; *current != nullptr; ++current) {
        in_addr candidate{};
        std::memcpy(&candidate, *current, sizeof(candidate));
        const u32 host_order = ntohl(candidate.s_addr);
        const u8 first = static_cast<u8>(host_order >> 24);
        const u8 second = static_cast<u8>(host_order >> 16);
        if (candidate.s_addr == INADDR_ANY || candidate.s_addr == INADDR_NONE ||
            first == 127 || (first == 169 && second == 254)) {
            continue;
        }
        address.sin_family = AF_INET;
        address.sin_addr = candidate;
        return true;
    }
    return false;
}

bool StartLegacySocketConnect(SOCKET& out_socket, const char* remote_address,
    u16 port, HWND notify_window, UINT notify_message) {
    g_network_state.last_error = 0;
    if (!InitializeLegacyTcpNetworking()) {
        g_network_state.last_error = WSAGetLastError();
        return false;
    }

    const SOCKET connected = socket(AF_INET, SOCK_STREAM, 0);
    if (connected == INVALID_SOCKET) {
        g_network_state.last_error = WSAGetLastError();
        return false;
    }
    out_socket = connected;

    sockaddr_in target;
    write_debug_stack_ipv4_sockaddr(target, remote_address, port);
    if (!select_async_socket_events(connected, notify_window, notify_message,
            FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE)) {
        g_network_state.last_error = WSAGetLastError();
        shutdown_send_and_close_socket(connected);
        out_socket = INVALID_SOCKET;
        return false;
    }

    if (connect(connected, reinterpret_cast<const sockaddr*>(&target),
            sizeof(target)) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            g_network_state.last_error = error;
            shutdown_send_and_close_socket(connected);
            out_socket = INVALID_SOCKET;
            return false;
        }
    }

    g_network_state.listen_socket_active = false;
    return true;
}

bool CopyLegacySocketPeerAddress(SOCKET socket, sockaddr_in& out_address) {
    auto* record = FindLegacySocketRecord(socket);
    if (record == nullptr) {
        return false;
    }

    if (record->peer_address.sin_family != AF_INET ||
        record->peer_address.sin_addr.s_addr == INADDR_ANY ||
        record->peer_address.sin_addr.s_addr == INADDR_NONE) {
        sockaddr_in peer{};
        int peer_size = sizeof(peer);
        if (getpeername(socket, reinterpret_cast<sockaddr*>(&peer),
                &peer_size) == SOCKET_ERROR || peer.sin_family != AF_INET ||
            peer.sin_addr.s_addr == INADDR_ANY ||
            peer.sin_addr.s_addr == INADDR_NONE) {
            return false;
        }
        record->peer_address = peer;
    }

    out_address = record->peer_address;
    return true;
}

LegacySocketRecord* ReceiveIntoLegacySocketQueue(SOCKET socket) {
    auto* record = FindLegacySocketRecord(socket);
    if (record == nullptr) {
        shutdown_send_and_close_socket(socket);
        return nullptr;
    }

    const auto old_size = record->receive_queue.size();
    const auto remaining = kLegacySocketQueueBytes - old_size;
    record->receive_queue.resize(kLegacySocketQueueBytes);
    const int received = recv(record->socket,
        reinterpret_cast<char*>(record->receive_queue.data() + old_size),
        static_cast<int>(remaining), 0);
    if (received == SOCKET_ERROR) {
        record->receive_queue.resize(old_size);
        g_network_state.last_error = WSAGetLastError();
        if (g_network_state.last_error != WSAEWOULDBLOCK) {
            CloseLegacySocketRecord(socket);
            return nullptr;
        }
        return record;
    }

    record->receive_queue.resize(old_size + static_cast<std::size_t>(received));
    return record;
}

void ConsumeLegacySocketReceiveQueue(LegacySocketRecord& record, u32 byte_count) {
    if (byte_count > record.receive_queue.size()) {
        return;
    }

    record.receive_queue.erase(record.receive_queue.begin(),
        record.receive_queue.begin() + byte_count);
}

bool QueueAndFlushSocketSend(u32 byte_count, const void* data, SOCKET socket) {
    auto* record = FindLegacySocketRecord(socket);
    if (record == nullptr) {
        shutdown_send_and_close_socket(socket);
        return false;
    }

    if (!append_to_send_queue(*record, data, byte_count)) {
        CloseLegacySocketRecord(socket);
        return false;
    }

    if (!flush_send_queue(*record)) {
        CloseLegacySocketRecord(socket);
        return false;
    }

    return true;
}

void QueueAndFlushAllActiveSocketSends(u32 byte_count, const void* data) {
    for (const auto& record : g_network_state.socket_records) {
        if (record.active) {
            QueueAndFlushSocketSend(byte_count, data, record.socket);
        }
    }
}

bool RefreshSocketLocalAddress(SOCKET socket, sockaddr_in& out_address) {
    char host_name[0x200];
    gethostname(host_name, sizeof(host_name));
    host_name[sizeof(host_name) - 1] = '\0';

    int address_size = sizeof(out_address);
    const int result = getsockname(socket, reinterpret_cast<sockaddr*>(&out_address),
        &address_size);
    if (result == SOCKET_ERROR) {
        return false;
    }
    return true;
}

sockaddr_in BuildIpv4Sockaddr(const char* dotted_address, u16 port) {
    sockaddr_in address{};
    write_legacy_ipv4_sockaddr(address, dotted_address, port);
    return address;
}

void InitializeLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state) {
    socket_state.socket = INVALID_SOCKET;
    ResetLegacyAsyncTcpSocketQueues(socket_state);
}

void ResetLegacyAsyncTcpSocketQueues(LegacyAsyncTcpSocket& socket_state) {
    socket_state.status = LegacyAsyncTcpStatus::Ok;
    socket_state.receive_length = 0;
    socket_state.send_length = 0;
}

bool StartLegacyAsyncTcpWinSock() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 0), &data);
    if (result != 0) {
        return false;
    }
    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 0) {
        WSACleanup();
        return false;
    }
    return true;
}

void ShutdownLegacyAsyncTcpWinSock() {
    WSACleanup();
}

bool CreateLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state) {
    socket_state.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_state.socket == INVALID_SOCKET) {
        ShutdownLegacyAsyncTcpWinSock();
        return false;
    }
    return true;
}

void CloseLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state) {
    ResetLegacyAsyncTcpSocketQueues(socket_state);
    if (socket_state.socket == INVALID_SOCKET) {
        return;
    }
    shutdown(socket_state.socket, 1);
    closesocket(socket_state.socket);
    socket_state.socket = INVALID_SOCKET;
}

void DestroyLegacyAsyncTcpSocketBase(LegacyAsyncTcpSocket& socket_state) {
    ResetLegacyAsyncTcpSocketQueues(socket_state);
}

void DeleteLegacyAsyncTcpSocketBase(LegacyAsyncTcpSocket& socket_state) {
    DestroyLegacyAsyncTcpSocketBase(socket_state);
}

void DeleteLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state) {
    CloseLegacyAsyncTcpSocket(socket_state);
    ShutdownLegacyAsyncTcpWinSock();
    DestroyLegacyAsyncTcpSocketBase(socket_state);
}

bool RegisterLegacyAsyncTcpSocketEvents(LegacyAsyncTcpSocket& socket_state,
    HWND notify_window, UINT notify_message, long events) {
    return WSAAsyncSelect(socket_state.socket, notify_window, notify_message,
               events) != SOCKET_ERROR;
}

SOCKET GetLegacyAsyncTcpSocket(const LegacyAsyncTcpSocket& socket_state) {
    return socket_state.socket;
}

void SetLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state, SOCKET socket) {
    socket_state.socket = socket;
}

bool ConnectLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state,
    bool& winsock_started_flag, HWND notify_window, UINT notify_message,
    const char* remote_address, u16 port, int send_buffer_bytes,
    int receive_buffer_bytes) {
    if (!winsock_started_flag) {
        if (!StartLegacyAsyncTcpWinSock()) {
            return false;
        }
        winsock_started_flag = true;
    }

    if (!CreateLegacyAsyncTcpSocket(socket_state)) {
        return false;
    }

    SetLegacyAsyncTcpSendBufferSize(socket_state, send_buffer_bytes);
    SetLegacyAsyncTcpReceiveBufferSize(socket_state, receive_buffer_bytes);
    if (!RegisterLegacyAsyncTcpSocketEvents(socket_state, notify_window,
            notify_message, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE)) {
        return false;
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = inet_addr(remote_address);
    if (target.sin_addr.s_addr == INADDR_NONE) {
        hostent* host = gethostbyname(remote_address);
        if (host == nullptr) {
            return false;
        }
        std::memcpy(&target.sin_addr, host->h_addr_list[0], sizeof(target.sin_addr));
    }
    target.sin_port = htons(port);

    if (connect(socket_state.socket, reinterpret_cast<const sockaddr*>(&target),
            sizeof(target)) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK;
    }
    return true;
}

void PrepareAndQueueLegacyAsyncTcpSend(LegacyAsyncTcpSocket& socket_state,
    void* data, i32 byte_count, i32* sent_count) {
    UpdateLegacyAsyncTcpPacketChecksum(data, byte_count);
    QueueLegacyAsyncTcpSend(socket_state, data, byte_count, sent_count);
}

std::vector<u8> BuildAsyncTcpLocaleFlagRequest(u32 flag) {
    std::vector<u8> packet(kLegacyAsyncTcpLocaleRequestBytes, 0xcc);
    write_le32(packet.data(), 0x00, 3);
    write_le32(packet.data(), 0x04, 0x29);
    write_le32(packet.data(), 0x08, kLegacyAsyncTcpLocaleRequestBytes);
    write_le32(packet.data(), 0x0d, flag);
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTLANGUAGE,
        reinterpret_cast<char*>(packet.data() + 0x11), 0x80);
    return packet;
}

void QueueAsyncTcpLocaleFlagRequest(LegacyAsyncTcpSocket& socket_state,
    u32 flag, i32* sent_count) {
    std::vector<u8> packet = BuildAsyncTcpLocaleFlagRequest(flag);
    PrepareAndQueueLegacyAsyncTcpSend(socket_state, packet.data(),
        static_cast<i32>(packet.size()), sent_count);
}

void QueueAsyncTcpLocaleFlag0Request(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count) {
    QueueAsyncTcpLocaleFlagRequest(socket_state, 0, sent_count);
}

void QueueAsyncTcpLocaleFlag1Request(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count) {
    QueueAsyncTcpLocaleFlagRequest(socket_state, 1, sent_count);
}

void QueueAsyncTcpLocaleFlag2Request(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count) {
    QueueAsyncTcpLocaleFlagRequest(socket_state, 2, sent_count);
}

void UpdateLegacyAsyncTcpPacketChecksum(void* packet, i32 byte_count) {
    if (packet == nullptr || byte_count < 0x0d) {
        return;
    }
    auto* bytes = static_cast<u8*>(packet);
    bytes[0x0c] = 0;
    u8 checksum = 0;
    for (i32 index = 0x0d; index < byte_count; ++index) {
        checksum = static_cast<u8>(
            checksum + bytes[index] * static_cast<u8>((index % 9) + 1));
    }
    bytes[0x0c] = checksum;
}

bool VerifyLegacyAsyncTcpPacketChecksum(const void* packet, i32 byte_count) {
    if (packet == nullptr || byte_count < 0x0d) {
        return false;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    u8 checksum = 0;
    for (i32 index = 0x0d; index < byte_count; ++index) {
        checksum = static_cast<u8>(
            checksum + bytes[index] * static_cast<u8>((index % 9) + 1));
    }
    return bytes[0x0c] == checksum;
}

bool ResolveLegacyAsyncTcpLocalHost(char* host_name_out, u32 host_name_size,
    char* address_out, u32 address_size) {
    char host_name[0x80]{};
    if (gethostname(host_name, sizeof(host_name)) == SOCKET_ERROR) {
        return false;
    }
    hostent* host = gethostbyname(host_name);
    if (host == nullptr) {
        return false;
    }
    if (host_name_out != nullptr && host_name_size != 0) {
        std::snprintf(host_name_out, host_name_size, "%s", host_name);
    }
    if (address_out != nullptr && address_size != 0) {
        in_addr address{};
        std::memcpy(&address, host->h_addr_list[0], sizeof(address));
        const char* text = inet_ntoa(address);
        if (text != nullptr) {
            std::snprintf(address_out, address_size, "%s", text);
        }
    }
    return true;
}

bool CopyLegacyAsyncTcpLocalSockaddr(const LegacyAsyncTcpSocket& socket_state,
    sockaddr_in& out_address) {
    int length = sizeof(out_address);
    return getsockname(socket_state.socket, reinterpret_cast<sockaddr*>(&out_address),
               &length) != SOCKET_ERROR;
}

bool CopyLegacyAsyncTcpPeerInfo(const LegacyAsyncTcpSocket& socket_state,
    char* host_name_out, u32 host_name_size, char* address_out, u32 address_size,
    u32* port_out) {
    sockaddr_in peer{};
    int length = sizeof(peer);
    if (getpeername(socket_state.socket, reinterpret_cast<sockaddr*>(&peer),
            &length) == SOCKET_ERROR) {
        return false;
    }

    hostent* host = gethostbyaddr(reinterpret_cast<const char*>(&peer.sin_addr),
        sizeof(peer.sin_addr), AF_INET);
    if (host == nullptr) {
        return false;
    }

    if (host_name_out != nullptr && host_name_size != 0 && host->h_name != nullptr) {
        std::snprintf(host_name_out, host_name_size, "%s", host->h_name);
    }
    if (address_out != nullptr && address_size != 0) {
        const char* text = inet_ntoa(peer.sin_addr);
        if (text != nullptr) {
            std::snprintf(address_out, address_size, "%s", text);
        }
    }
    if (port_out != nullptr) {
        *port_out = ntohs(peer.sin_port);
    }
    return true;
}

bool ReceiveLegacyAsyncTcpQueue(LegacyAsyncTcpSocket& socket_state) {
    if (socket_state.receive_length < 0 ||
        socket_state.receive_length > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::ReceiveLengthInvalid);
        return false;
    }

    char* destination = reinterpret_cast<char*>(socket_state.receive_buffer.data() +
        socket_state.receive_length);
    const int remaining =
        static_cast<int>(kLegacyAsyncTcpQueueBytes - socket_state.receive_length);
    const int received = recv(socket_state.socket, destination, remaining, 0);
    if (received == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::ReceiveFailed);
            return false;
        }
        return true;
    }
    socket_state.receive_length += received;
    return true;
}

i32 GetLegacyAsyncTcpReceiveLength(const LegacyAsyncTcpSocket& socket_state) {
    return socket_state.receive_length;
}

const u8* GetLegacyAsyncTcpReceiveBuffer(const LegacyAsyncTcpSocket& socket_state) {
    return socket_state.receive_buffer.data();
}

void ConsumeLegacyAsyncTcpReceiveQueue(LegacyAsyncTcpSocket& socket_state,
    i32 byte_count) {
    if (byte_count < 0) {
        set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::ConsumeNegative);
        return;
    }
    if (byte_count == 0) {
        return;
    }

    socket_state.receive_length -= byte_count;
    if (socket_state.receive_length < 0 ||
        socket_state.receive_length > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::ConsumeLengthInvalid);
        return;
    }
    std::memmove(socket_state.receive_buffer.data(),
        socket_state.receive_buffer.data() + byte_count, socket_state.receive_length);
}

void QueueLegacyAsyncTcpSend(LegacyAsyncTcpSocket& socket_state, const void* data,
    i32 byte_count, i32* sent_count) {
    if (socket_state.send_length < 0 ||
        socket_state.send_length > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::SendLengthInvalid);
        return;
    }
    if (byte_count < 1 || byte_count > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::SendPayloadInvalid);
        return;
    }

    const i32 next_length = socket_state.send_length + byte_count;
    if (next_length < 0 || next_length > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::SendWouldOverflow);
        return;
    }

    std::memcpy(socket_state.send_buffer.data() + socket_state.send_length,
        data, byte_count);
    socket_state.send_length = next_length;
    FlushLegacyAsyncTcpSendQueue(socket_state, sent_count);
}

void FlushLegacyAsyncTcpSendQueue(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count) {
    i32 local_sent = 0;
    if (sent_count == nullptr) {
        sent_count = &local_sent;
    }

    if (socket_state.send_length == 0) {
        return;
    }
    if (socket_state.send_length < 0 ||
        socket_state.send_length > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::FlushLengthInvalid);
        return;
    }

    const int sent = send(socket_state.socket,
        reinterpret_cast<const char*>(socket_state.send_buffer.data()),
        socket_state.send_length, 0);
    *sent_count = sent;
    if (sent == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            set_async_tcp_status(socket_state, LegacyAsyncTcpStatus::FlushFailed);
        }
        return;
    }

    socket_state.send_length -= sent;
    std::memmove(socket_state.send_buffer.data(), socket_state.send_buffer.data() + sent,
        socket_state.send_length);

    g_async_tcp_counters.interval_bytes += static_cast<u32>(sent);
    g_async_tcp_counters.total_bytes += static_cast<u32>(sent);
    const u32 now = timeGetTime();
    socket_state.last_send_tick = now;

    const u32 interval_delta = now - g_async_tcp_counters.interval_tick;
    if (interval_delta > 999) {
        const u32 interval_divisor = (interval_delta + 1u) / 1000u;
        socket_state.interval_send_bytes_per_second =
            interval_divisor == 0 ? 0 :
            g_async_tcp_counters.interval_bytes / interval_divisor;
        const u32 total_delta = now - g_async_tcp_counters.total_tick;
        const u32 total_divisor = total_delta + 1u;
        socket_state.total_send_bytes_per_second =
            total_divisor == 0 ? 0 :
            (g_async_tcp_counters.total_bytes / total_divisor) / 1000u;
        g_async_tcp_counters.interval_tick = now;
        g_async_tcp_counters.interval_bytes = 0;
    }
}

void SetLegacyAsyncTcpReceiveBufferSize(LegacyAsyncTcpSocket& socket_state,
    int byte_count) {
    if (byte_count >= 0) {
        setsockopt(socket_state.socket, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<const char*>(&byte_count), sizeof(byte_count));
    }
}

void SetLegacyAsyncTcpSendBufferSize(LegacyAsyncTcpSocket& socket_state,
    int byte_count) {
    if (byte_count >= 0) {
        setsockopt(socket_state.socket, SOL_SOCKET, SO_SNDBUF,
            reinterpret_cast<const char*>(&byte_count), sizeof(byte_count));
    }
}

sockaddr_in BuildLegacyAsyncTcpSockaddr(const char* address, u16 port) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    result.sin_addr.s_addr = address == nullptr ? htonl(INADDR_ANY) : inet_addr(address);
    return result;
}

}

#endif
