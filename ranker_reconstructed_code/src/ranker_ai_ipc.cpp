#include "ranker_ai_ipc.h"

#include <winsock.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace ranker {
namespace {

struct AiIpcState {
    bool wsa_started = false;
    bool connected = false;
    SOCKET socket = INVALID_SOCKET;
    std::string recv_buffer;  // holds bytes read past a message boundary
};

AiIpcState& state() {
    static AiIpcState instance;
    return instance;
}

bool send_all(SOCKET sock, const char* data, int length) {
    int sent_total = 0;
    while (sent_total < length) {
        const int sent = send(sock, data + sent_total, length - sent_total, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }
        sent_total += sent;
    }
    return true;
}

// Read one newline-terminated line into `line` (without the newline).  Buffers
// any extra bytes for the next call.  Returns false on socket error/close.
bool recv_line(AiIpcState& st, std::string& line) {
    for (;;) {
        const std::size_t newline = st.recv_buffer.find('\n');
        if (newline != std::string::npos) {
            line.assign(st.recv_buffer, 0, newline);
            st.recv_buffer.erase(0, newline + 1);
            return true;
        }
        char chunk[1024];
        const int received = recv(st.socket, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return false;
        }
        st.recv_buffer.append(chunk, static_cast<std::size_t>(received));
    }
}

} // namespace

bool AiIpcConnect(unsigned short port) {
    AiIpcState& st = state();
    if (st.connected) {
        return true;
    }
    if (!st.wsa_started) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 0), &data) != 0) {
            return false;
        }
        st.wsa_started = true;
    }
    st.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (st.socket == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(st.socket, reinterpret_cast<const sockaddr*>(&target),
            sizeof(target)) == SOCKET_ERROR) {
        closesocket(st.socket);
        st.socket = INVALID_SOCKET;
        return false;
    }
    // The request/reply is a tiny newline-framed message every decision
    // frame; Nagle + delayed ACK on this loopback socket stalled the game
    // ~35% of each decision waiting for the policy reply (the P2P socket
    // already disables it — ranker_network.cpp).
    const int nodelay = 1;
    setsockopt(st.socket, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    st.connected = true;
    return true;
}

bool AiIpcConnected() {
    return state().connected;
}

int AiIpcRequestAction(unsigned owner, unsigned frame,
    const std::array<float, kAiRlFeatureCount>& features,
    const std::array<std::uint8_t, kAiRlActionCount>& mask,
    const std::array<std::uint8_t, kAiRlTargetCellCount>& target_mask,
    int* target_cell) {
    if (target_cell != nullptr) {
        *target_cell = -1;
    }
    AiIpcState& st = state();
    if (!st.connected) {
        return -1;
    }
    std::string message;
    message.reserve(512);
    char head[96];
    std::snprintf(head, sizeof(head),
        "{\"t\":\"act\",\"owner\":%u,\"frame\":%u,\"feat\":[", owner, frame);
    message += head;
    for (std::size_t i = 0; i < features.size(); ++i) {
        char num[24];
        std::snprintf(num, sizeof(num), "%s%.5f", i == 0 ? "" : ",",
            static_cast<double>(features[i]));
        message += num;
    }
    message += "],\"mask\":[";
    for (std::size_t i = 0; i < mask.size(); ++i) {
        char num[8];
        std::snprintf(num, sizeof(num), "%s%u", i == 0 ? "" : ",",
            static_cast<unsigned>(mask[i]));
        message += num;
    }
    message += "],\"tmask\":[";
    for (std::size_t i = 0; i < target_mask.size(); ++i) {
        char num[8];
        std::snprintf(num, sizeof(num), "%s%u", i == 0 ? "" : ",",
            static_cast<unsigned>(target_mask[i]));
        message += num;
    }
    message += "]}\n";

    if (!send_all(st.socket, message.data(),
            static_cast<int>(message.size()))) {
        AiIpcClose();
        return -1;
    }

    std::string reply;
    if (!recv_line(st, reply)) {
        AiIpcClose();
        return -1;
    }
    // Parse {"action":N[,"target":C]} — a minimal scan, no JSON library needed.
    const std::size_t key = reply.find("\"action\"");
    if (key == std::string::npos) {
        return -1;
    }
    const std::size_t colon = reply.find(':', key);
    if (colon == std::string::npos) {
        return -1;
    }
    const int action = std::atoi(reply.c_str() + colon + 1);
    if (action < 0 || static_cast<std::size_t>(action) >= kAiRlActionCount) {
        return -1;
    }
    if (target_cell != nullptr) {
        const std::size_t target_key = reply.find("\"target\"");
        if (target_key != std::string::npos) {
            const std::size_t target_colon = reply.find(':', target_key);
            if (target_colon != std::string::npos) {
                const int cell = std::atoi(reply.c_str() + target_colon + 1);
                if (cell >= 0 &&
                    static_cast<std::size_t>(cell) < kAiRlTargetCellCount) {
                    *target_cell = cell;
                }
            }
        }
    }
    return action;
}

void AiIpcSendEnd(const char* reason, unsigned frame) {
    AiIpcState& st = state();
    if (!st.connected) {
        return;
    }
    char message[160];
    const int length = std::snprintf(message, sizeof(message),
        "{\"t\":\"end\",\"reason\":\"%s\",\"frame\":%u}\n",
        reason != nullptr ? reason : "", frame);
    if (length > 0) {
        send_all(st.socket, message, length);
    }
}

void AiIpcClose() {
    AiIpcState& st = state();
    if (st.socket != INVALID_SOCKET) {
        closesocket(st.socket);
        st.socket = INVALID_SOCKET;
    }
    st.connected = false;
    st.recv_buffer.clear();
    if (st.wsa_started) {
        WSACleanup();
        st.wsa_started = false;
    }
}

// ---------------------------------------------------------------------------
// act2 entity-mode binary IPC (plan §11): bounded exact-read/exact-write.
// ---------------------------------------------------------------------------

namespace {

struct AiIpc2State {
    bool wsa_started = false;
    bool connected = false;
    SOCKET socket = INVALID_SOCKET;
};

AiIpc2State& ipc2_state() {
    static AiIpc2State instance;
    return instance;
}

// One whole-frame deadline: `start_ms` is the tick of the frame's first
// byte; partial progress never extends it (plan §11: no per-chunk renewal).
bool ipc2_exact_io(SOCKET sock, bool writing, u8* data, std::size_t length,
    unsigned long start_ms, unsigned timeout_ms) {
    std::size_t done = 0;
    while (done < length) {
        const unsigned long elapsed = GetTickCount() - start_ms;
        if (elapsed >= timeout_ms) {
            return false;   // deadline
        }
        const unsigned long remaining = timeout_ms - elapsed;
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);
        timeval tv{};
        tv.tv_sec = static_cast<long>(remaining / 1000ul);
        tv.tv_usec = static_cast<long>((remaining % 1000ul) * 1000ul);
        const int ready = select(0, writing ? nullptr : &set,
            writing ? &set : nullptr, nullptr, &tv);
        if (ready == 0) {
            return false;   // deadline
        }
        if (ready == SOCKET_ERROR) {
            return false;
        }
        const int transferred = writing ?
            send(sock, reinterpret_cast<const char*>(data) + done,
                static_cast<int>(length - done), 0) :
            recv(sock, reinterpret_cast<char*>(data) + done,
                static_cast<int>(length - done), 0);
        if (transferred == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                continue;   // WOULD_BLOCK is not a failure; re-poll
            }
            return false;
        }
        if (transferred == 0) {
            return false;   // peer close
        }
        done += static_cast<std::size_t>(transferred);
    }
    return true;
}

}  // namespace

bool AiIpc2Connect(unsigned short port, unsigned handshake_timeout_ms) {
    AiIpc2State& st = ipc2_state();
    if (st.connected) {
        return true;
    }
    if (!st.wsa_started) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 0), &data) != 0) {
            return false;
        }
        st.wsa_started = true;
    }
    st.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (st.socket == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(st.socket, reinterpret_cast<const sockaddr*>(&target),
            sizeof(target)) == SOCKET_ERROR) {
        closesocket(st.socket);
        st.socket = INVALID_SOCKET;
        return false;
    }
    const int nodelay = 1;
    setsockopt(st.socket, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    // Nonblocking + select: WOULD_BLOCK/deadline/peer-close are told apart
    // in ipc2_exact_io instead of trusting blocking-socket defaults.
    u_long nonblocking = 1;
    ioctlsocket(st.socket, FIONBIO, &nonblocking);
    (void)handshake_timeout_ms;   // the caller applies it to the HELLO frames
    st.connected = true;
    return true;
}

bool AiIpc2Connected() {
    return ipc2_state().connected;
}

void AiIpc2Close() {
    AiIpc2State& st = ipc2_state();
    if (st.socket != INVALID_SOCKET) {
        closesocket(st.socket);
        st.socket = INVALID_SOCKET;
    }
    st.connected = false;
    if (st.wsa_started) {
        WSACleanup();
        st.wsa_started = false;
    }
}

bool AiIpc2SendFrame(AiEntityWireHeader& header,
    const std::vector<u8>& payload, unsigned timeout_ms) {
    AiIpc2State& st = ipc2_state();
    if (!st.connected) {
        return false;
    }
    if (payload.size() > kAiEntityWireMaxPayloadBytes) {
        return false;
    }
    header.payload_bytes = static_cast<u32>(payload.size());
    header.payload_crc32 = AiEntityCrc32(payload.data(), payload.size());
    u8 header_bytes[kAiEntityWireHeaderBytes];
    AiEntityWriteWireHeader(header, header_bytes);
    const unsigned long start_ms = GetTickCount();
    if (!ipc2_exact_io(st.socket, true, header_bytes,
            kAiEntityWireHeaderBytes, start_ms, timeout_ms)) {
        return false;
    }
    if (!payload.empty() &&
        !ipc2_exact_io(st.socket, true,
            const_cast<u8*>(payload.data()), payload.size(), start_ms,
            timeout_ms)) {
        return false;
    }
    return true;
}

bool AiIpc2ReceiveFrame(AiEntityWireHeader& header, std::vector<u8>& payload,
    unsigned timeout_ms, std::string* error) {
    AiIpc2State& st = ipc2_state();
    if (!st.connected) {
        if (error != nullptr) {
            *error = "not connected";
        }
        return false;
    }
    u8 header_bytes[kAiEntityWireHeaderBytes];
    const unsigned long start_ms = GetTickCount();
    if (!ipc2_exact_io(st.socket, false, header_bytes,
            kAiEntityWireHeaderBytes, start_ms, timeout_ms)) {
        if (error != nullptr) {
            *error = "header read timeout/close";
        }
        return false;
    }
    if (!AiEntityParseWireHeader(header_bytes, kAiEntityWireHeaderBytes,
            header, error)) {
        return false;
    }
    payload.assign(header.payload_bytes, 0);
    if (header.payload_bytes != 0 &&
        !ipc2_exact_io(st.socket, false, payload.data(), payload.size(),
            start_ms, timeout_ms)) {
        if (error != nullptr) {
            *error = "payload read timeout/close";
        }
        return false;
    }
    if (AiEntityCrc32(payload.data(), payload.size()) !=
        header.payload_crc32) {
        if (error != nullptr) {
            *error = "payload CRC mismatch";
        }
        return false;
    }
    return true;
}

} // namespace ranker
