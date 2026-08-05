#include "ranker_network.h"

#ifdef _WIN32

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

bool wait_for_queue(std::size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        ranker::ReceiveLegacyUdpPacket();
        if (ranker::legacy_network_state().udp_receive_queue.size() >= expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

int main() {
    using namespace ranker;

    if (StartLegacyUdpSocket("127.0.0.1", 0) == INVALID_SOCKET) {
        std::fprintf(stderr, "failed to start receiver: %d\n", WSAGetLastError());
        return 1;
    }

    sockaddr_in target = legacy_network_state().udp_bind_address;
    target.sin_addr.s_addr = inet_addr("127.0.0.1");
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender == INVALID_SOCKET) {
        ShutdownLegacyUdpNetworking();
        return 2;
    }

    std::array<u8, 0x24> prefix{};
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        prefix[index] = static_cast<u8>(0x40u + index);
    }
    if (sendto(sender, reinterpret_cast<const char*>(prefix.data()),
            static_cast<int>(prefix.size()), 0,
            reinterpret_cast<const sockaddr*>(&target), sizeof(target)) ==
            SOCKET_ERROR || !wait_for_queue(prefix.size())) {
        closesocket(sender);
        ShutdownLegacyUdpNetworking();
        return 3;
    }

    // IPv4's largest legal UDP payload.  The historical direct-to-queue
    // recvfrom had only 65500 bytes left after the prefix and discarded this
    // datagram with WSAEMSGSIZE.
    std::vector<u8> large(kLegacyUdpDatagramBytes - 29u);
    for (std::size_t index = 0; index < large.size(); ++index) {
        large[index] = static_cast<u8>((index * 37u + 11u) & 0xffu);
    }
    if (sendto(sender, reinterpret_cast<const char*>(large.data()),
            static_cast<int>(large.size()), 0,
            reinterpret_cast<const sockaddr*>(&target), sizeof(target)) ==
            SOCKET_ERROR || !wait_for_queue(prefix.size() + large.size())) {
        closesocket(sender);
        ShutdownLegacyUdpNetworking();
        return 4;
    }

    const std::vector<u8>& queue = legacy_network_state().udp_receive_queue;
    const bool prefix_ok = std::equal(prefix.begin(), prefix.end(), queue.begin());
    const bool large_ok = std::equal(large.begin(), large.end(),
        queue.begin() + static_cast<std::ptrdiff_t>(prefix.size()));

    closesocket(sender);
    ShutdownLegacyUdpNetworking();
    if (!prefix_ok || !large_ok) {
        return 5;
    }
    std::puts("legacy UDP burst regression passed");
    return 0;
}

#else

int main() {
    return 0;
}

#endif
