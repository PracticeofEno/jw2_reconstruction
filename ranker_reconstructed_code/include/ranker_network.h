#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>

#include <array>
#include <vector>

namespace ranker {

constexpr u32 kLegacySocketRecordCount = 10;
constexpr u32 kLegacySocketQueueBytes = 0x4000;
constexpr u32 kLegacyUdpReceiveQueueBytes = 0x10000;
constexpr u32 kLegacyAsyncTcpQueueBytes = 0x4000;
constexpr u32 kLegacyAsyncTcpLocaleRequestBytes = 0x91;

enum class LegacyAsyncTcpStatus : u32 {
    Ok = 0,
    ReceiveLengthInvalid = 1,
    ReceiveFailed = 2,
    ConsumeNegative = 3,
    ConsumeLengthInvalid = 4,
    SendLengthInvalid = 5,
    SendPayloadInvalid = 6,
    SendWouldOverflow = 7,
    FlushLengthInvalid = 8,
    FlushFailed = 9,
};

struct LegacySocketRecord {
    bool active = false;
    SOCKET socket = INVALID_SOCKET;
    sockaddr_in peer_address{};
    std::vector<u8> receive_queue;
    std::vector<u8> send_queue;
};

struct LegacyNetworkRuntimeState {
    std::array<LegacySocketRecord, kLegacySocketRecordCount> socket_records{};
    SOCKET listen_socket = INVALID_SOCKET;
    sockaddr_in listen_address{};
    bool listen_socket_active = false;
    bool winsock_started = false;
    bool tcp_initialized = false;
    bool udp_initialized = false;
    SOCKET udp_socket = INVALID_SOCKET;
    sockaddr_in udp_bind_address{};
    sockaddr_in udp_last_sender{};
    std::vector<u8> udp_receive_queue;
    u32 udp_payload_limit = 0;
    i32 active_socket_count = 0;
    int last_error = 0;
    char host_name[0x200]{};
};

struct LegacyAsyncTcpSocket {
    LegacyAsyncTcpStatus status = LegacyAsyncTcpStatus::Ok;
    SOCKET socket = INVALID_SOCKET;
    std::array<u8, kLegacyAsyncTcpQueueBytes> receive_buffer{};
    i32 receive_length = 0;
    std::array<u8, kLegacyAsyncTcpQueueBytes> send_buffer{};
    i32 send_length = 0;
    u32 interval_send_bytes_per_second = 0;
    u32 total_send_bytes_per_second = 0;
    u32 last_send_tick = 0;
};

LegacyNetworkRuntimeState& legacy_network_state();

bool EnsureLegacyWinSockStartup(u8 major, u8 minor);
void ShutdownLegacyWinSock();
bool IsPrivateIpv4Address(const in_addr& address);
bool ResolveLocalHostDisplayAddress(char* host_name_out, u32 host_name_size,
    char* address_out, u32 address_size);

bool InitializeLegacyUdpNetworking();
void ShutdownLegacyUdpNetworking();
SOCKET StartLegacyUdpSocket(const char* bind_address, u16 port);
void CloseLegacyUdpSocket();
bool RefreshLegacyUdpLocalAddress();
void DrainLegacyUdpReceiveQueue();
i32 ReceiveLegacyUdpPacket();
void ConsumeLegacyUdpReceiveQueue(u32 byte_count);
bool SendLegacyUdpChunks(u32 byte_count, const void* data,
    const sockaddr_in& target_address);
sockaddr_in BuildLegacyUdpSockaddr(const char* dotted_address, u16 port);

bool InitializeLegacyTcpNetworking();
void ShutdownLegacyTcpNetworking();

LegacySocketRecord* FindLegacySocketRecord(SOCKET socket);
void RegisterLegacySocketRecord(SOCKET socket,
    const sockaddr_in* peer_address = nullptr);
void CloseLegacySocketRecord(SOCKET socket);
void CloseAllLegacySocketRecords();

bool StartLegacyListenSocket(u16 port, HWND notify_window, UINT notify_message);
bool AcceptLegacySocketConnection(HWND notify_window, UINT notify_message);
bool ResolveLocalHostIpv4Address(sockaddr_in& address);
bool StartLegacySocketConnect(SOCKET& out_socket, const char* remote_address,
    u16 port, HWND notify_window, UINT notify_message);
bool CopyLegacySocketPeerAddress(SOCKET socket, sockaddr_in& out_address);
LegacySocketRecord* ReceiveIntoLegacySocketQueue(SOCKET socket);
void ConsumeLegacySocketReceiveQueue(LegacySocketRecord& record, u32 byte_count);
bool QueueAndFlushSocketSend(u32 byte_count, const void* data, SOCKET socket);
void QueueAndFlushAllActiveSocketSends(u32 byte_count, const void* data);
bool RefreshSocketLocalAddress(SOCKET socket, sockaddr_in& out_address);
sockaddr_in BuildIpv4Sockaddr(const char* dotted_address, u16 port);

void InitializeLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state);
void ResetLegacyAsyncTcpSocketQueues(LegacyAsyncTcpSocket& socket_state);
bool StartLegacyAsyncTcpWinSock();
void ShutdownLegacyAsyncTcpWinSock();
bool CreateLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state);
void CloseLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state);
void DeleteLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state);
void DeleteLegacyAsyncTcpSocketBase(LegacyAsyncTcpSocket& socket_state);
void DestroyLegacyAsyncTcpSocketBase(LegacyAsyncTcpSocket& socket_state);
bool RegisterLegacyAsyncTcpSocketEvents(LegacyAsyncTcpSocket& socket_state,
    HWND notify_window, UINT notify_message, long events);
SOCKET GetLegacyAsyncTcpSocket(const LegacyAsyncTcpSocket& socket_state);
void SetLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state, SOCKET socket);
bool ConnectLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state,
    bool& winsock_started_flag, HWND notify_window, UINT notify_message,
    const char* remote_address, u16 port, int send_buffer_bytes,
    int receive_buffer_bytes);
void PrepareAndQueueLegacyAsyncTcpSend(LegacyAsyncTcpSocket& socket_state,
    void* data, i32 byte_count, i32* sent_count = nullptr);
std::vector<u8> BuildAsyncTcpLocaleFlagRequest(u32 flag);
void QueueAsyncTcpLocaleFlagRequest(LegacyAsyncTcpSocket& socket_state,
    u32 flag, i32* sent_count = nullptr);
void QueueAsyncTcpLocaleFlag0Request(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count = nullptr);
void QueueAsyncTcpLocaleFlag1Request(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count = nullptr);
void QueueAsyncTcpLocaleFlag2Request(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count = nullptr);
void UpdateLegacyAsyncTcpPacketChecksum(void* packet, i32 byte_count);
bool VerifyLegacyAsyncTcpPacketChecksum(const void* packet, i32 byte_count);
bool ResolveLegacyAsyncTcpLocalHost(char* host_name_out, u32 host_name_size,
    char* address_out, u32 address_size);
bool CopyLegacyAsyncTcpLocalSockaddr(const LegacyAsyncTcpSocket& socket_state,
    sockaddr_in& out_address);
bool CopyLegacyAsyncTcpPeerInfo(const LegacyAsyncTcpSocket& socket_state,
    char* host_name_out, u32 host_name_size, char* address_out, u32 address_size,
    u32* port_out);
bool ReceiveLegacyAsyncTcpQueue(LegacyAsyncTcpSocket& socket_state);
i32 GetLegacyAsyncTcpReceiveLength(const LegacyAsyncTcpSocket& socket_state);
const u8* GetLegacyAsyncTcpReceiveBuffer(const LegacyAsyncTcpSocket& socket_state);
void ConsumeLegacyAsyncTcpReceiveQueue(LegacyAsyncTcpSocket& socket_state,
    i32 byte_count);
void QueueLegacyAsyncTcpSend(LegacyAsyncTcpSocket& socket_state, const void* data,
    i32 byte_count, i32* sent_count = nullptr);
void FlushLegacyAsyncTcpSendQueue(LegacyAsyncTcpSocket& socket_state,
    i32* sent_count = nullptr);
void SetLegacyAsyncTcpReceiveBufferSize(LegacyAsyncTcpSocket& socket_state,
    int byte_count);
void SetLegacyAsyncTcpSendBufferSize(LegacyAsyncTcpSocket& socket_state,
    int byte_count);
sockaddr_in BuildLegacyAsyncTcpSockaddr(const char* address, u16 port);

LegacyAsyncTcpSocket& FrontendAsyncTcpSocket0();
LegacyAsyncTcpSocket& FrontendAsyncTcpSocket1();
void InitializeFrontendAsyncTcpSocket0Static();
void InitializeFrontendAsyncTcpSocket0();
void RegisterFrontendAsyncTcpSocket0Destructor();
void DestroyFrontendAsyncTcpSocket0();
void DestroyFrontendAsyncTcpSocketObject(LegacyAsyncTcpSocket& socket_state);
LegacyAsyncTcpSocket& InitializeFrontendAsyncTcpSocketObject(
    LegacyAsyncTcpSocket& socket_state);
void DeleteFrontendAsyncTcpSocketObject(LegacyAsyncTcpSocket* socket_state,
    bool free_storage);
void InitializeFrontendAsyncTcpSocket1Static();
void InitializeFrontendAsyncTcpSocket1();
void RegisterFrontendAsyncTcpSocket1Destructor();
void DestroyFrontendAsyncTcpSocket1();

}

#endif
