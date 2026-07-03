#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <dplay.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32
struct DirectPlayConnectionRecord {
    GUID service_provider{};
    std::vector<u8> connection_data;
    std::string short_name;
    DWORD flags = 0;
};

struct DirectPlaySessionRecord {
    DPSESSIONDESC2 descriptor{};
    std::string session_name;
};

struct DirectPlayPlayerRecord {
    DPID player_id = 0;
    DWORD player_type = 0;
    DWORD flags = 0;
    void* callback_context = nullptr;
    std::string short_name;
    std::string long_name;
};

struct AsyncComContext;

using DirectPlayMessageHandler = void (*)(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player, void* user_data);

constexpr UINT kDirectPlayMode6Type0WindowMessage = 0x501;
constexpr UINT kDirectPlayMode6Type2WindowMessage = 0x504;
constexpr UINT kDirectPlayMode7Type2WindowMessage = 0x509;

struct DirectPlayMode1RangePacket {
    u32 start_sequence = 0;
    u32 end_sequence = 0;
    u8 channel = 0;
    u32 slot = 0;
    DPID from_player = 0;
    DPID to_player = 0;
    bool requested_missing_range = false;
};

struct DirectPlayMessageDispatchCallbacks {
    DirectPlayMessageHandler mode1_system_message = nullptr;
    DirectPlayMessageHandler mode1_player_message = nullptr;
    DirectPlayMessageHandler mode6_system_message = nullptr;
    DirectPlayMessageHandler mode6_player_message = nullptr;
    DirectPlayMessageHandler mode7_player_message = nullptr;

    DirectPlayMessageHandler mode1_system_type5 = nullptr;
    DirectPlayMessageHandler mode1_system_ready = nullptr;
    DirectPlayMessageHandler mode1_player_type0 = nullptr;
    DirectPlayMessageHandler mode1_player_subtype15 = nullptr;
    DirectPlayMessageHandler mode1_player_range_gap = nullptr;
    DirectPlayMessageHandler mode1_player_range_ready = nullptr;
    DirectPlayMessageHandler mode1_player_subtype1c = nullptr;
    DirectPlayMessageHandler mode1_player_fallback = nullptr;
    DirectPlayMessageHandler mode6_system_ready = nullptr;
    DirectPlayMessageHandler mode6_player_type0_post = nullptr;
    DirectPlayMessageHandler mode6_player_type2_post = nullptr;
    DirectPlayMessageHandler mode7_player_type2_post = nullptr;
};

struct AsyncComContext {
    LPDIRECTPLAY4A direct_play = nullptr;
    HANDLE receive_event = nullptr;
    const DirectPlayConnectionRecord* selected_connection = nullptr;
    std::vector<u8> session_descriptor_data;
    const DPSESSIONDESC2* session_descriptor = nullptr;
    DPID local_player = 0;
    bool is_host = false;
    bool system_message_101_seen = false;
};

struct AsyncComRuntimeState {
    HWND window = nullptr;
    AsyncComContext* active_context = nullptr;
    HANDLE worker_thread = nullptr;
    DWORD worker_thread_id = 0;
    HANDLE shutdown_event = nullptr;
    bool critical_section_initialized = false;
    bool com_initialized = false;
    bool initialized = false;
    bool direct_play_protocol_enabled = false;
    bool mode7_dispatch_enabled = false;
    bool mode6_type0_window_post_enabled = false;
    bool mode1_system_ready_seen = false;
    bool mode6_system_ready_seen = false;
    HRESULT last_result = S_OK;
    i32 mode1_last_send_status = 0;
    u32 received_message_count = 0;
    u32 receive_dispatch_mode = 0;
    i32 active_network_transport_mode = -1;
    u32 mode1_last_system_type5_value = 0;
    u32 mode1_player_type0_count = 0;
    u8 mode1_player_last_subtype = 0;
    DirectPlayMode1RangePacket mode1_last_range_packet{};
    std::array<u32, 8> mode1_expected_sequences{};
    std::array<u32, 8> mode1_range_slot_counts{};
    std::array<bool, 8> mode1_missing_range_requested{};
    std::array<sockaddr_in, 8> mode1_udp_peer_addresses{};
    std::array<bool, 8> mode1_udp_peer_address_valid{};
    HWND mode6_dispatch_window = nullptr;
    HWND mode7_dispatch_window = nullptr;
    CRITICAL_SECTION* mode6_dispatch_lock = nullptr;
    HANDLE legacy_udp_mode1_thread = nullptr;
    DWORD legacy_udp_mode1_thread_id = 0;
    bool legacy_udp_mode1_stop_requested = false;
    GUID ipx_provider{};
    GUID tcpip_provider{};
    GUID serial_provider{};
    GUID modem_provider{};
    std::vector<DirectPlayConnectionRecord> connections;
    std::vector<DirectPlaySessionRecord> sessions;
    std::vector<DirectPlayPlayerRecord> players;
    DirectPlayMessageDispatchCallbacks message_callbacks{};
    void* message_callback_user_data = nullptr;
};

HRESULT InitAsyncComSubsystem(HINSTANCE instance, AsyncComContext* context);
HRESULT InitAsyncDirectPlayWorker(AsyncComContext* context);
DWORD WINAPI DirectPlayReceiveThreadProc(void* parameter);
void PumpDirectPlayMessages(AsyncComContext* context);
HRESULT RegisterAsyncComWindowCallback(HWND window);
void ShutdownAsyncComSubsystem(AsyncComContext* context);
void ShutdownAsyncDirectPlayContext(AsyncComContext* context);
HRESULT AcquireDirectPlay4AComObject(LPDIRECTPLAY4A& direct_play);
void ReleaseDirectPlay4AComObject(LPDIRECTPLAY4A& direct_play);
BOOL CALLBACK EnumDirectPlayConnectionsCallback(LPCGUID service_provider, LPVOID connection,
    DWORD connection_size, LPCDPNAME name, DWORD flags, LPVOID context);
void ClearDirectPlayConnectionRecords();
void ShowDirectPlayConnectionMessages(HWND window);
BOOL CALLBACK SelectDirectPlayProviderGuid(REFGUID address_type_guid, LPVOID context,
    DWORD flags);
void ResolveDirectPlayServiceProviderGuids();
void ResetDirectPlayConnectionRecords();
HRESULT InitializeDirectPlayConnection(const DirectPlayConnectionRecord& connection,
    AsyncComContext* context);
void SelectDirectPlayConnectionRecordForContext(u32 connection_index, AsyncComContext* context);
HRESULT BuildDirectPlayTcpIpAddressBlob(const char* host_name, std::vector<u8>& out_address);
void ClearDirectPlaySessionRecords();
BOOL CALLBACK EnumDirectPlaySessionsCallback(LPCDPSESSIONDESC2 session_desc, LPDWORD timeout,
    DWORD flags, LPVOID context);
void RefreshDirectPlaySessionRecords(AsyncComContext* context);
HRESULT RefreshCurrentDirectPlaySessionDescriptor(AsyncComContext* context);
HRESULT SetCurrentDirectPlaySessionJoinDisabled(bool disabled, DWORD max_players = 0);
HRESULT HostDirectPlaySessionWithPlayer(const char* session_name, bool password_required,
    const char* password, const char* player_name, const void* player_data,
    DWORD player_data_size, DWORD flags, AsyncComContext* context);
HRESULT HostDirectPlayJwarSessionRecord(const char* session_name,
    const char* password, DWORD max_players, u32 version, const char* player_name,
    const void* player_data, DWORD player_data_size, DWORD flags,
    AsyncComContext* context);
HRESULT JoinDirectPlaySessionWithPlayer(const GUID& session_guid, const char* session_name,
    const char* player_name, const void* player_data, DWORD player_data_size, DWORD flags,
    AsyncComContext* context);
HRESULT JoinDirectPlaySessionRecord(DirectPlaySessionRecord& session_record,
    const char* player_name, const void* player_data, DWORD player_data_size, DWORD flags,
    AsyncComContext* context);
void ShutdownGlobalDirectPlaySession();
void DestroyGlobalDirectPlayPlayer();
void ClearDirectPlayPlayerRecords();
BOOL CALLBACK EnumDirectPlayPlayersCallback(DPID player_id, DWORD player_type,
    LPCDPNAME name, DWORD flags, LPVOID context);
bool ClearDirectPlayPlayerSlotId(u32 player_slot, bool notify_inactive = false);
bool ClearDirectPlayPlayerId(DPID player_id);
void RefreshDirectPlayPlayerRecords(AsyncComContext* context);
void SetDirectPlayMessageDispatchMode(u32 mode);
void SetActiveNetworkTransportMode(i32 mode);
void SetDirectPlayMode7DispatchEnabled(bool enabled);
void SetDirectPlayMessageDispatchCallbacks(
    const DirectPlayMessageDispatchCallbacks& callbacks, void* user_data = nullptr);
void ConfigureDirectPlayMode6DispatchLock(CRITICAL_SECTION* critical_section);
void SetDirectPlayMode1ExpectedSequence(u32 channel, u32 sequence);
void ClearDirectPlayMode1UdpPeerAddresses();
void SetDirectPlayMode1UdpPeerAddress(u32 player_slot, const sockaddr_in& address);
i32 SendMode1ReliablePayloadToPlayerDefault(const void* payload, u32 byte_count,
    u32 target_player);
i32 BroadcastMode1ReliablePayloadDefault(const void* payload, u32 byte_count);
void InstallDefaultMode1ReliableTransportCallbacks();
void PollLegacyUdpMode1MessagesForActiveTransport();
void PumpLegacyUdpMode1Messages(AsyncComContext* context);
DWORD WINAPI LegacyUdpMode1ReceiveThreadProc(LPVOID parameter);
void StartLegacyUdpMode1ReceiveThread();
void StopLegacyUdpMode1ReceiveThread();
void ConfigureDirectPlayMode6WindowDispatch(HWND window, bool post_type0_messages);
void ConfigureDirectPlayMode7WindowDispatch(HWND window);
i32 SendMode1ReliablePayloadToPlayer(const void* payload, u32 byte_count,
    u32 target_player);
void DispatchMode1DirectPlaySystemMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player, DPID to_player);
void DispatchMode1DirectPlayPlayerMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player, DPID to_player);
void DispatchMode1DirectPlayMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player, DPID to_player);
void DispatchMode6DirectPlayPlayerMessage(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player);
void DispatchMode6DirectPlaySystemMessage(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player);
void DispatchMode6DirectPlayMessageWithLock(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player);
void DispatchMode7DirectPlayPlayerMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player, DPID to_player);
void DispatchMode7DirectPlayMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player, DPID to_player);
void DispatchDirectPlayReceivedMessage(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player);

const AsyncComRuntimeState& async_com_state();
#endif

}
