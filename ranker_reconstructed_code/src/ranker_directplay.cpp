#include "ranker_directplay.h"
#include "ranker_network.h"
#include "ranker_player_slots.h"
#include "ranker_reliable_packets.h"
#include "ranker_wizardnet_relay.h"

#ifdef _WIN32
#include <dplobby.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ranker {
namespace {

constexpr GUID kNullGuid =
    {0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
constexpr GUID kRankerApplicationGuid =
    {0x177b8b7e, 0xea3d, 0x453d, {0x9d, 0x78, 0x6f, 0x65, 0xda, 0x26, 0x9f, 0x96}};
constexpr GUID kClsidDirectPlay =
    {0xd1eb6d20, 0x8923, 0x11d0, {0x9d, 0x97, 0x00, 0xa0, 0xc9, 0x0a, 0x43, 0xcb}};
constexpr GUID kIidDirectPlay4A =
    {0x0ab1c531, 0x4745, 0x11d1, {0xa7, 0xa1, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc}};
constexpr GUID kClsidDirectPlayLobby =
    {0x2fe8f810, 0xb2a5, 0x11d0, {0xa7, 0x87, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc}};
constexpr GUID kIidDirectPlayLobby3A =
    {0x2db72491, 0x652c, 0x11d1, {0xa7, 0xa8, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc}};
constexpr GUID kDpspIpx =
    {0x685bc400, 0x9d2c, 0x11cf, {0xa9, 0xcd, 0x00, 0xaa, 0x00, 0x68, 0x86, 0xe3}};
constexpr GUID kDpspTcpIp =
    {0x36e95ee0, 0x8577, 0x11cf, {0x96, 0x0c, 0x00, 0x80, 0xc7, 0x53, 0x4e, 0x82}};
constexpr GUID kDpspSerial =
    {0x0f1d6860, 0x88d9, 0x11cf, {0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e}};
constexpr GUID kDpspModem =
    {0x44eaa760, 0xcb68, 0x11cf, {0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e}};
constexpr GUID kDpaidServiceProvider =
    {0x07d916c0, 0xe0af, 0x11cf, {0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e}};
constexpr GUID kDpaidPhone =
    {0x78ec89a0, 0xe0af, 0x11cf, {0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e}};
constexpr GUID kDpaidInet =
    {0xc4a54da0, 0xe0af, 0x11cf, {0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e}};
constexpr GUID kDpaidComPort =
    {0xf2f0ce00, 0xe0af, 0x11cf, {0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e}};
constexpr DWORD kOriginalHostSessionFlags = DPSESSION_MIGRATEHOST | DPSESSION_KEEPALIVE;
constexpr DWORD kOriginalEnumSessionsFlags = DPENUMSESSIONS_AVAILABLE |
    DPENUMSESSIONS_ASYNC | DPENUMSESSIONS_PASSWORDREQUIRED;
constexpr DWORD kDirectPlaySessionJoinDisabledFlag = 0x20;
constexpr DWORD kDirectPlayJwarHostUser1 = 8;
constexpr DWORD kDirectPlayJwarMagic = 0x5241574a;
constexpr u32 kDirectPlaySystemReadyMessage = 0x101;
constexpr HRESULT kOriginalDirectPlaySendRetryResult =
    static_cast<HRESULT>(0x8877010eu);
constexpr HRESULT kOriginalDirectPlaySendStatus0b =
    static_cast<HRESULT>(0x80070057u);
constexpr HRESULT kOriginalDirectPlaySendStatus0c =
    static_cast<HRESULT>(0x88770096u);
constexpr HRESULT kOriginalDirectPlaySendStatus0d =
    static_cast<HRESULT>(0x88770816u);
constexpr HRESULT kOriginalDirectPlaySendStatus0e =
    static_cast<HRESULT>(0x887700e6u);
constexpr HRESULT kOriginalDirectPlaySendStatus0f =
    static_cast<HRESULT>(0x88770168u);

AsyncComRuntimeState g_async_com_state;
CRITICAL_SECTION g_async_com_lock;

struct DirectPlayDispatchSnapshot {
    DirectPlayMessageDispatchCallbacks callbacks{};
    void* user_data = nullptr;
    HWND mode6_window = nullptr;
    HWND mode7_window = nullptr;
    CRITICAL_SECTION* mode6_dispatch_lock = nullptr;
    bool mode6_type0_post_enabled = false;
};

u32 read_queue_u32(const std::vector<u8>& queue, std::size_t offset) {
    if (offset > queue.size() || queue.size() - offset < sizeof(u32)) {
        return 0;
    }

    u32 value = 0;
    std::memcpy(&value, queue.data() + offset, sizeof(value));
    return value;
}

u8 read_queue_u8(const std::vector<u8>& queue, std::size_t offset) {
    if (offset >= queue.size()) {
        return 0;
    }
    return queue[offset];
}

bool same_guid(const GUID& lhs, const GUID& rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(GUID)) == 0;
}

void lock_async_state() {
    if (g_async_com_state.critical_section_initialized) {
        EnterCriticalSection(&g_async_com_lock);
    }
}

void unlock_async_state() {
    if (g_async_com_state.critical_section_initialized) {
        LeaveCriticalSection(&g_async_com_lock);
    }
}

DirectPlayDispatchSnapshot snapshot_directplay_dispatch() {
    DirectPlayDispatchSnapshot snapshot{};
    lock_async_state();
    snapshot.callbacks = g_async_com_state.message_callbacks;
    snapshot.user_data = g_async_com_state.message_callback_user_data;
    snapshot.mode6_window = g_async_com_state.mode6_dispatch_window;
    snapshot.mode7_window = g_async_com_state.mode7_dispatch_window;
    snapshot.mode6_dispatch_lock = g_async_com_state.mode6_dispatch_lock;
    snapshot.mode6_type0_post_enabled = g_async_com_state.mode6_type0_window_post_enabled;
    unlock_async_state();
    return snapshot;
}

u32 read_message_u32(const void* message, DWORD message_size, DWORD offset) {
    if (message == nullptr || offset > message_size || message_size - offset < sizeof(u32)) {
        return 0;
    }

    u32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(message) + offset, sizeof(value));
    return value;
}

u8 read_message_u8(const void* message, DWORD message_size, DWORD offset) {
    if (message == nullptr || offset >= message_size) {
        return 0;
    }
    return *(static_cast<const u8*>(message) + offset);
}

void invoke_message_handler(const DirectPlayDispatchSnapshot& snapshot,
    DirectPlayMessageHandler handler, AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player) {
    if (handler != nullptr) {
        handler(context, message, message_size, from_player, to_player, snapshot.user_data);
    }
}

bool post_copied_directplay_message(HWND window, UINT message_id, WPARAM wparam,
    const void* message, DWORD message_size) {
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, message_size);
    if (global == nullptr) {
        return false;
    }

    void* copy = GlobalLock(global);
    if (copy == nullptr) {
        return false;
    }

    std::memcpy(copy, message, message_size);
    // Original code posts the locked pointer and leaves ownership to the receiver.
    return PostMessageA(window, message_id, wparam, reinterpret_cast<LPARAM>(copy)) !=
        FALSE;
}

void close_handle(HANDLE& handle) {
    if (handle != nullptr) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

DWORD WINAPI legacy_udp_mode1_receive_thread(LPVOID) {
    while (true) {
        lock_async_state();
        const bool stop_requested = g_async_com_state.legacy_udp_mode1_stop_requested;
        AsyncComContext* context = g_async_com_state.active_context;
        unlock_async_state();
        if (stop_requested) {
            break;
        }
        PumpLegacyUdpMode1Messages(context);
    }
    return 0;
}

void reset_provider_guids() {
    g_async_com_state.ipx_provider = kNullGuid;
    g_async_com_state.tcpip_provider = kNullGuid;
    g_async_com_state.serial_provider = kNullGuid;
    g_async_com_state.modem_provider = kNullGuid;
}

std::string directplay_name_from(LPCDPNAME name) {
    if (name == nullptr) {
        return {};
    }
    if (name->lpszShortNameA != nullptr) {
        return name->lpszShortNameA;
    }
    if (name->lpszLongNameA != nullptr) {
        return name->lpszLongNameA;
    }
    return {};
}

std::string directplay_long_name_from(LPCDPNAME name) {
    if (name == nullptr) {
        return {};
    }
    if (name->lpszLongNameA != nullptr) {
        return name->lpszLongNameA;
    }
    if (name->lpszShortNameA != nullptr) {
        return name->lpszShortNameA;
    }
    return {};
}

HRESULT acquire_direct_play4a(LPDIRECTPLAY4A& direct_play) {
    direct_play = nullptr;
    HRESULT result = CoCreateInstance(kClsidDirectPlay, nullptr,
        CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
        kIidDirectPlay4A, reinterpret_cast<void**>(&direct_play));
    g_async_com_state.last_result = result;
    return result;
}

void release_direct_play4a(LPDIRECTPLAY4A& direct_play) {
    if (direct_play == nullptr) {
        return;
    }

    direct_play->Close();
    direct_play->Release();
    direct_play = nullptr;
}

void notify_player_slot_inactive(u32 cleared_slot) {
    PlayerSlotRuntimeState& slots = player_slot_state();
    // DirectPlay player records retain the original raw 0..7 owner index.
    // active_slot_count only partitions lobby/team UI and must not suppress a
    // valid tail-slot departure notification.
    if (cleared_slot < kPlayerSlotCount) {
        MarkPlayerInactiveAndBroadcastIfLocal(slots, cleared_slot, slots.local_player_slot);
    }
}

void update_selected_provider(const GUID& provider) {
    if (same_guid(provider, kDpspIpx)) {
        g_async_com_state.ipx_provider = provider;
    }
    else if (same_guid(provider, kDpspTcpIp)) {
        g_async_com_state.tcpip_provider = provider;
    }
    else if (same_guid(provider, kDpspSerial)) {
        g_async_com_state.serial_provider = provider;
    }
    else if (same_guid(provider, kDpspModem)) {
        g_async_com_state.modem_provider = provider;
    }
}

bool string_contains_ipx(const std::string& text) {
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return upper.find("IPX") != std::string::npos;
}

BOOL CALLBACK enum_connections_callback(LPCGUID service_provider, LPVOID connection,
    DWORD connection_size, LPCDPNAME name, DWORD flags, LPVOID context) {
    (void)context;

    DirectPlayConnectionRecord record;
    if (service_provider != nullptr) {
        record.service_provider = *service_provider;
        update_selected_provider(record.service_provider);
    }
    record.flags = flags;
    record.short_name = directplay_name_from(name);

    if (connection != nullptr && connection_size != 0) {
        const auto* first = static_cast<const u8*>(connection);
        record.connection_data.assign(first, first + connection_size);
    }

    lock_async_state();
    g_async_com_state.connections.push_back(std::move(record));
    unlock_async_state();
    return TRUE;
}

void bind_session_record_pointers(DirectPlaySessionRecord& record) {
    record.descriptor.lpszSessionNameA =
        record.session_name.empty() ? nullptr : const_cast<char*>(record.session_name.c_str());
}

void rebind_all_session_record_pointers() {
    for (auto& record : g_async_com_state.sessions) {
        bind_session_record_pointers(record);
    }
}

DPSESSIONDESC2 descriptor_for_session_record(DirectPlaySessionRecord& record) {
    bind_session_record_pointers(record);
    return record.descriptor;
}

BOOL CALLBACK enum_sessions_callback(LPCDPSESSIONDESC2 session_desc, LPDWORD timeout,
    DWORD flags, LPVOID context) {
    (void)timeout;
    if ((flags & DPESC_TIMEDOUT) != 0 || session_desc == nullptr) {
        return FALSE;
    }

    DirectPlaySessionRecord record;
    record.descriptor = *session_desc;
    if (session_desc->lpszSessionNameA != nullptr) {
        record.session_name = session_desc->lpszSessionNameA;
    }
    record.descriptor.dwSize = sizeof(DPSESSIONDESC2);

    lock_async_state();
    g_async_com_state.sessions.push_back(std::move(record));
    rebind_all_session_record_pointers();
    unlock_async_state();
    return TRUE;
}

BOOL CALLBACK enum_players_callback(DPID player_id, DWORD player_type, LPCDPNAME name,
    DWORD flags, LPVOID context) {
    DirectPlayPlayerRecord record;
    record.player_id = player_id;
    record.player_type = player_type;
    record.flags = flags;
    record.callback_context = context;
    record.short_name = directplay_name_from(name);
    record.long_name = directplay_long_name_from(name);

    lock_async_state();
    g_async_com_state.players.push_back(std::move(record));
    unlock_async_state();
    return TRUE;
}

DPNAME make_directplay_name(const char* name) {
    DPNAME directplay_name{};
    directplay_name.dwSize = sizeof(directplay_name);
    directplay_name.lpszShortNameA = const_cast<char*>(name != nullptr ? name : "");
    directplay_name.lpszLongNameA = directplay_name.lpszShortNameA;
    return directplay_name;
}

HRESULT create_local_directplay_player(AsyncComContext* context, const char* player_name,
    const void* player_data, DWORD player_data_size, DWORD flags) {
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = E_POINTER;
        return g_async_com_state.last_result;
    }

    DPNAME name = make_directplay_name(player_name);
    DPID player_id = 0;
    HRESULT result = context->direct_play->CreatePlayer(&player_id, &name,
        context->receive_event, const_cast<void*>(player_data), player_data_size, flags);
    g_async_com_state.last_result = result;
    if (SUCCEEDED(result)) {
        context->local_player = player_id;
    }
    return result;
}

void destroy_local_player(AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr || context->local_player == 0) {
        return;
    }

    context->direct_play->DestroyPlayer(context->local_player);
    context->local_player = 0;
}

void release_direct_play_context(AsyncComContext* context) {
    if (context == nullptr) {
        return;
    }

    if (context->direct_play != nullptr) {
        if (context->local_player != 0) {
            context->direct_play->DestroyPlayer(context->local_player);
            context->local_player = 0;
        }
        context->direct_play->Close();
        context->direct_play->Release();
        context->direct_play = nullptr;
    }

    close_handle(context->receive_event);
    std::vector<u8>().swap(context->session_descriptor_data);
}

void handle_mode1_system_directplay_message(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player,
    const DirectPlayDispatchSnapshot& snapshot) {
    const u32 message_type = read_message_u32(message, message_size, 0);
    if (message_type < 0x32) {
        if (message_type == 5) {
            const u32 value = read_message_u32(message, message_size, 8);
            ClearDirectPlayPlayerId(static_cast<DPID>(value));
            lock_async_state();
            g_async_com_state.mode1_last_system_type5_value = value;
            unlock_async_state();
            invoke_message_handler(snapshot, snapshot.callbacks.mode1_system_type5, context,
                message, message_size, from_player, to_player);
        }
        return;
    }

    if (message_type == kDirectPlaySystemReadyMessage) {
        if (context != nullptr) {
            context->system_message_101_seen = true;
        }
        lock_async_state();
        g_async_com_state.mode1_system_ready_seen = true;
        unlock_async_state();
        invoke_message_handler(snapshot, snapshot.callbacks.mode1_system_ready, context,
            message, message_size, from_player, to_player);
    }
}

void handle_mode1_player_directplay_message(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player,
    const DirectPlayDispatchSnapshot& snapshot) {
    const u32 message_type = read_message_u32(message, message_size, 0);
    if (message_type == 0) {
        lock_async_state();
        ++g_async_com_state.mode1_player_type0_count;
        unlock_async_state();
        invoke_message_handler(snapshot, snapshot.callbacks.mode1_player_type0, context,
            message, message_size, from_player, to_player);
        return;
    }

    if (message_type != 1) {
        return;
    }

    const u8 subtype = read_message_u8(message, message_size, 0x0f);
    lock_async_state();
    g_async_com_state.mode1_player_last_subtype = subtype;
    unlock_async_state();

    if (subtype == 0x15) {
        invoke_message_handler(snapshot, snapshot.callbacks.mode1_player_subtype15, context,
            message, message_size, from_player, to_player);
        return;
    }

    if (subtype == 0x17) {
        DirectPlayMode1RangePacket packet{};
        packet.start_sequence = read_message_u32(message, message_size, 8);
        packet.end_sequence = read_message_u32(message, message_size, 20);
        packet.channel = read_message_u8(message, message_size, 12);
        packet.slot = read_message_u32(message, message_size, 16);
        packet.from_player = from_player;
        packet.to_player = to_player;

        bool valid_channel = false;
        bool valid_slot = false;
        u32 expected_sequence = 0;
        lock_async_state();
        valid_channel = packet.channel < g_async_com_state.mode1_expected_sequences.size();
        valid_slot = packet.slot < g_async_com_state.mode1_range_slot_counts.size();
        if (valid_slot) {
            ++g_async_com_state.mode1_range_slot_counts[packet.slot];
            ++mode1_reliable_state().missing_range_request_counts[packet.slot];
        }
        if (valid_channel) {
            expected_sequence = GetMode1ReliableExpectedSequence(packet.channel);
            g_async_com_state.mode1_expected_sequences[packet.channel] = expected_sequence;
            packet.requested_missing_range = expected_sequence < packet.start_sequence;
            if (!packet.requested_missing_range &&
                expected_sequence < packet.end_sequence) {
                packet.end_sequence = expected_sequence - 1;
            }
        }
        g_async_com_state.mode1_last_range_packet = packet;
        unlock_async_state();

        if (packet.requested_missing_range) {
            SendMode1GapAck(packet.slot);
        }
        else {
            ResendMode1PacketRange(packet.start_sequence, packet.end_sequence, packet.slot);
        }
        invoke_message_handler(snapshot,
            packet.requested_missing_range ? snapshot.callbacks.mode1_player_range_gap :
                snapshot.callbacks.mode1_player_range_ready,
            context, message, message_size, from_player, to_player);
        return;
    }

    if (subtype == 0x1c) {
        const u8 channel = read_message_u8(message, message_size, 12);
        lock_async_state();
        if (channel < g_async_com_state.mode1_missing_range_requested.size()) {
            g_async_com_state.mode1_missing_range_requested[channel] = false;
        }
        unlock_async_state();
        ClearMode1ReliableMissingRangeRequest(channel);
        invoke_message_handler(snapshot, snapshot.callbacks.mode1_player_subtype1c, context,
            message, message_size, from_player, to_player);
        return;
    }

    AcceptMode1OrderedPacket(message, message_size);
    invoke_message_handler(snapshot, snapshot.callbacks.mode1_player_fallback, context,
        message, message_size, from_player, to_player);
}

void handle_mode6_system_directplay_message(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player,
    const DirectPlayDispatchSnapshot& snapshot) {
    if (read_message_u32(message, message_size, 0) != kDirectPlaySystemReadyMessage) {
        return;
    }

    if (context != nullptr) {
        context->system_message_101_seen = true;
    }
    lock_async_state();
    g_async_com_state.mode6_system_ready_seen = true;
    unlock_async_state();
    invoke_message_handler(snapshot, snapshot.callbacks.mode6_system_ready, context,
        message, message_size, from_player, to_player);
}

void handle_mode6_player_directplay_message(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player,
    const DirectPlayDispatchSnapshot& snapshot) {
    const u32 message_type = read_message_u32(message, message_size, 0);
    if (message_type == 0) {
        if (snapshot.mode6_type0_post_enabled) {
            post_copied_directplay_message(snapshot.mode6_window,
                kDirectPlayMode6Type0WindowMessage, static_cast<WPARAM>(from_player),
                message, message_size);
        }
        invoke_message_handler(snapshot, snapshot.callbacks.mode6_player_type0_post,
            context, message, message_size, from_player, to_player);
    }
    else if (message_type == 2) {
        post_copied_directplay_message(snapshot.mode6_window,
            kDirectPlayMode6Type2WindowMessage, static_cast<WPARAM>(from_player),
            message, message_size);
        invoke_message_handler(snapshot, snapshot.callbacks.mode6_player_type2_post,
            context, message, message_size, from_player, to_player);
    }
}

void handle_mode7_player_directplay_message(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player,
    const DirectPlayDispatchSnapshot& snapshot) {
    if (read_message_u32(message, message_size, 0) != 2) {
        return;
    }

    post_copied_directplay_message(snapshot.mode7_window, kDirectPlayMode7Type2WindowMessage,
        0, message, message_size);
    invoke_message_handler(snapshot, snapshot.callbacks.mode7_player_type2_post, context,
        message, message_size, from_player, to_player);
}

void pump_direct_play_messages(AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr) {
        return;
    }

    std::vector<u8> message;
    DWORD receive_size = 0;
    for (;;) {
        DPID from = 0;
        DPID to = 0;
        HRESULT result = S_OK;
        do {
            from = 0;
            to = 0;
            DWORD size = receive_size;
            result = context->direct_play->Receive(
                &from, &to, DPRECEIVE_ALL,
                message.empty() ? nullptr : message.data(), &size);
            receive_size = size;
            if (result == DPERR_BUFFERTOOSMALL) {
                if (receive_size == 0) {
                    result = E_OUTOFMEMORY;
                    break;
                }
                message.assign(receive_size, 0);
            }
        } while (result == DPERR_BUFFERTOOSMALL);

        if (FAILED(result)) {
            break;
        }

        lock_async_state();
        ++g_async_com_state.received_message_count;
        unlock_async_state();

        if (receive_size > 3) {
            DispatchDirectPlayReceivedMessage(
                context, message.data(), receive_size, from, to);
        }
    }
}

DWORD WINAPI direct_play_receive_thread(void* parameter) {
    auto* context = static_cast<AsyncComContext*>(parameter);
    if (context == nullptr || context->receive_event == nullptr ||
        g_async_com_state.shutdown_event == nullptr) {
        return 0;
    }

    HANDLE handles[2] = {context->receive_event, g_async_com_state.shutdown_event};
    for (;;) {
        const DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0) {
            pump_direct_play_messages(context);
        }
        else {
            break;
        }
    }
    return 0;
}

bool mode1_reliable_uses_legacy_udp_transport(i32 transport_mode) {
    return transport_mode >= 0 && transport_mode <= 2;
}

bool directplay_session_descriptor_mutation_skipped_for_transport(i32 transport_mode) {
    return transport_mode >= 0 && (transport_mode <= 2 || transport_mode == 6);
}

bool mode1_udp_address_ready(const sockaddr_in& address) {
    return address.sin_family == AF_INET && address.sin_addr.s_addr != 0 &&
        address.sin_port != 0;
}

bool mode1_slot_active_for_udp_send(
    u32 slot, const Mode1ReliableRuntimeState& reliable) {
    if (slot >= reliable.player_status.size()) {
        return false;
    }

    const u8 state = reliable.player_status[slot];
    return state != static_cast<u8>(PlayerSlotState::disabled) &&
        state != static_cast<u8>(PlayerSlotState::player_controlled);
}

void set_directplay_last_result(HRESULT result) {
    lock_async_state();
    g_async_com_state.last_result = result;
    unlock_async_state();
}

void set_mode1_send_status(i32 status) {
    lock_async_state();
    g_async_com_state.mode1_last_send_status = status;
    unlock_async_state();
}

void record_mode1_udp_send_result(bool succeeded) {
    lock_async_state();
    ++g_async_com_state.mode1_udp_send_attempt_count;
    if (succeeded) {
        ++g_async_com_state.mode1_udp_send_success_count;
    }
    else {
        ++g_async_com_state.mode1_udp_send_failure_count;
    }
    unlock_async_state();
}

void record_mode1_udp_receive_packet(u32 byte_count) {
    const sockaddr_in sender = legacy_network_state().udp_last_sender;
    lock_async_state();
    ++g_async_com_state.mode1_udp_receive_packet_count;
    g_async_com_state.mode1_udp_receive_byte_count += byte_count;
    g_async_com_state.mode1_udp_last_sender_address = sender.sin_addr.s_addr;
    g_async_com_state.mode1_udp_last_sender_port = ntohs(sender.sin_port);
    unlock_async_state();
}

i32 normalize_mode1_directplay_send_status(HRESULT result) {
    if (result == S_OK) {
        return 0;
    }
    if (result == kOriginalDirectPlaySendStatus0b) {
        return 0x0b;
    }
    if (result == kOriginalDirectPlaySendStatus0c) {
        return 0x0c;
    }
    if (result == kOriginalDirectPlaySendStatus0d) {
        return 0x0d;
    }
    if (result == kOriginalDirectPlaySendStatus0e) {
        return 0x0e;
    }
    if (result == kOriginalDirectPlaySendStatus0f) {
        return 0x0f;
    }
    return -1;
}

HRESULT send_directplay_mode1_payload_with_original_retry(LPDIRECTPLAY4A direct_play,
    DPID from_player, DPID to_player, const void* payload, u32 byte_count) {
    HRESULT result = S_OK;
    do {
        result = direct_play->Send(from_player, to_player, DPSEND_GUARANTEED,
            const_cast<void*>(payload), byte_count);
    } while (result == kOriginalDirectPlaySendRetryResult);
    return result;
}

i32 default_mode1_reliable_send_callback(const void* data, u32 byte_count,
    u32 target_player, void*) {
    return SendMode1ReliablePayloadToPlayerDefault(data, byte_count, target_player);
}

i32 default_mode1_reliable_broadcast_callback(const void* data, u32 byte_count,
    u32, void*) {
    return BroadcastMode1ReliablePayloadDefault(data, byte_count);
}

void default_mode1_reliable_poll_transport_receive_callback(void*) {
    PollLegacyUdpMode1MessagesForActiveTransport();
}

} // namespace

HRESULT AcquireDirectPlay4AComObject(LPDIRECTPLAY4A& direct_play) {
    return acquire_direct_play4a(direct_play);
}

void ReleaseDirectPlay4AComObject(LPDIRECTPLAY4A& direct_play) {
    release_direct_play4a(direct_play);
}

HRESULT DirectPlayLobbyCreateA(const GUID*, void** lobby, IUnknown* outer,
    void*, DWORD) {
    if (lobby == nullptr) {
        return E_POINTER;
    }
    *lobby = nullptr;
    return CoCreateInstance(kClsidDirectPlayLobby, outer, CLSCTX_INPROC_SERVER,
        kIidDirectPlayLobby3A, lobby);
}

BOOL CALLBACK EnumDirectPlayConnectionsCallback(LPCGUID service_provider, LPVOID connection,
    DWORD connection_size, LPCDPNAME name, DWORD flags, LPVOID context) {
    return enum_connections_callback(service_provider, connection, connection_size, name, flags,
        context);
}

void ShowDirectPlayConnectionMessages(HWND window) {
    std::vector<std::string> names;
    lock_async_state();
    for (const auto& connection : g_async_com_state.connections) {
        names.push_back(connection.short_name);
    }
    unlock_async_state();

    for (const auto& name : names) {
        char message[256]{};
        lstrcpynA(message, name.c_str(), static_cast<int>(sizeof(message)));
        MessageBoxA(window, message, name.c_str(), 0);
    }
}

BOOL CALLBACK SelectDirectPlayProviderGuid(REFGUID address_type_guid, LPVOID context,
    DWORD flags) {
    (void)flags;
    if (context == nullptr) {
        return TRUE;
    }

    const auto* connection = static_cast<const DirectPlayConnectionRecord*>(context);
    if (same_guid(address_type_guid, kDpaidPhone)) {
        g_async_com_state.modem_provider = connection->service_provider;
        return FALSE;
    }
    if (same_guid(address_type_guid, kDpaidInet)) {
        g_async_com_state.tcpip_provider = connection->service_provider;
        return FALSE;
    }
    if (same_guid(address_type_guid, kDpaidComPort)) {
        g_async_com_state.serial_provider = connection->service_provider;
        return FALSE;
    }
    return TRUE;
}

void ResolveDirectPlayServiceProviderGuids() {
    reset_provider_guids();

    std::vector<DirectPlayConnectionRecord> connections;
    lock_async_state();
    connections = g_async_com_state.connections;
    unlock_async_state();

    LPDIRECTPLAYLOBBY3A lobby = nullptr;
    HRESULT result = CoCreateInstance(kClsidDirectPlayLobby, nullptr, CLSCTX_INPROC_SERVER,
        kIidDirectPlayLobby3A, reinterpret_cast<void**>(&lobby));
    g_async_com_state.last_result = result;
    if (FAILED(result)) {
        return;
    }

    for (const auto& connection : connections) {
        result = lobby->EnumAddressTypes(SelectDirectPlayProviderGuid,
            connection.service_provider,
            const_cast<DirectPlayConnectionRecord*>(&connection), 0);
        g_async_com_state.last_result = result;
    }

    for (const auto& connection : connections) {
        if (string_contains_ipx(connection.short_name) &&
            !same_guid(connection.service_provider, kNullGuid)) {
            g_async_com_state.ipx_provider = connection.service_provider;
            break;
        }
    }

    lobby->Release();
}

BOOL CALLBACK EnumDirectPlaySessionsCallback(LPCDPSESSIONDESC2 session_desc, LPDWORD timeout,
    DWORD flags, LPVOID context) {
    return enum_sessions_callback(session_desc, timeout, flags, context);
}

BOOL CALLBACK EnumDirectPlayPlayersCallback(DPID player_id, DWORD player_type,
    LPCDPNAME name, DWORD flags, LPVOID context) {
    return enum_players_callback(player_id, player_type, name, flags, context);
}

void PumpDirectPlayMessages(AsyncComContext* context) {
    pump_direct_play_messages(context);
}

DWORD WINAPI DirectPlayReceiveThreadProc(void* parameter) {
    return direct_play_receive_thread(parameter);
}

HRESULT InitAsyncComSubsystem(HINSTANCE instance, AsyncComContext* context) {
    (void)instance;
    if (g_async_com_state.initialized) {
        return S_OK;
    }
    if (context == nullptr) {
        g_async_com_state.last_result = E_POINTER;
        return g_async_com_state.last_result;
    }

    *context = AsyncComContext{};
    InitializeCriticalSection(&g_async_com_lock);
    g_async_com_state.critical_section_initialized = true;

    HRESULT result = CoInitialize(nullptr);
    g_async_com_state.last_result = result;
    if (FAILED(result)) {
        ShutdownAsyncDirectPlayContext(context);
        CoUninitialize();
        return result;
    }
    g_async_com_state.com_initialized = true;

    result = InitAsyncDirectPlayWorker(context);
    if (FAILED(result)) {
        ShutdownAsyncDirectPlayContext(context);
        CoUninitialize();
        g_async_com_state.com_initialized = false;
        return result;
    }

    g_async_com_state.initialized = true;
    g_async_com_state.last_result = S_OK;
    return S_OK;
}

HRESULT InitAsyncDirectPlayWorker(AsyncComContext* context) {
    if (context == nullptr) {
        g_async_com_state.last_result = E_POINTER;
        return g_async_com_state.last_result;
    }

    *context = AsyncComContext{};
    context->receive_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (context->receive_event == nullptr) {
        g_async_com_state.last_result = E_OUTOFMEMORY;
        ShutdownAsyncDirectPlayContext(context);
        return g_async_com_state.last_result;
    }

    g_async_com_state.shutdown_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (g_async_com_state.shutdown_event == nullptr) {
        g_async_com_state.last_result = E_OUTOFMEMORY;
        ShutdownAsyncDirectPlayContext(context);
        return g_async_com_state.last_result;
    }

    g_async_com_state.worker_thread = CreateThread(nullptr, 0, DirectPlayReceiveThreadProc,
        context, 0, &g_async_com_state.worker_thread_id);
    if (g_async_com_state.worker_thread == nullptr) {
        g_async_com_state.last_result = E_OUTOFMEMORY;
        ShutdownAsyncDirectPlayContext(context);
        return g_async_com_state.last_result;
    }

    SetThreadPriority(g_async_com_state.worker_thread, THREAD_PRIORITY_ABOVE_NORMAL);
    g_async_com_state.active_context = context;
    g_async_com_state.last_result = S_OK;
    return S_OK;
}

HRESULT RegisterAsyncComWindowCallback(HWND window) {
    g_async_com_state.window = window;
    ClearDirectPlayConnectionRecords();
    reset_provider_guids();

    LPDIRECTPLAY4A direct_play = nullptr;
    HRESULT result = AcquireDirectPlay4AComObject(direct_play);
    if (FAILED(result)) {
        return result;
    }

    result = direct_play->EnumConnections(
        &kRankerApplicationGuid, EnumDirectPlayConnectionsCallback, window, 0);
    g_async_com_state.last_result = result;
    ReleaseDirectPlay4AComObject(direct_play);
    return result;
}

void ShutdownAsyncComSubsystem(AsyncComContext* context) {
    if (!g_async_com_state.initialized) {
        return;
    }

    ShutdownAsyncDirectPlayContext(context);
    ClearDirectPlayPlayerRecords();
    ClearDirectPlaySessionRecords();
    ResetDirectPlayConnectionRecords();

    if (g_async_com_state.com_initialized) {
        CoUninitialize();
        g_async_com_state.com_initialized = false;
    }

    if (g_async_com_state.critical_section_initialized) {
        DeleteCriticalSection(&g_async_com_lock);
        g_async_com_state.critical_section_initialized = false;
    }

    g_async_com_state.initialized = false;
}

void ShutdownAsyncDirectPlayContext(AsyncComContext* context) {
    if (g_async_com_state.worker_thread != nullptr) {
        if (!TerminateThread(g_async_com_state.worker_thread, 0)) {
            if (g_async_com_state.shutdown_event != nullptr) {
                SetEvent(g_async_com_state.shutdown_event);
            }
            WaitForSingleObject(g_async_com_state.worker_thread, 4000);
        }
        close_handle(g_async_com_state.worker_thread);
        g_async_com_state.worker_thread_id = 0;
    }

    close_handle(g_async_com_state.shutdown_event);
    release_direct_play_context(context);
}

void ClearDirectPlayConnectionRecords() {
    lock_async_state();
    g_async_com_state.connections.clear();
    unlock_async_state();
}

void ResetDirectPlayConnectionRecords() {
    ClearDirectPlayConnectionRecords();
}

HRESULT InitializeDirectPlayConnection(const DirectPlayConnectionRecord& connection,
    AsyncComContext* context) {
    if (context == nullptr) {
        g_async_com_state.last_result = E_POINTER;
        return g_async_com_state.last_result;
    }

    ReleaseDirectPlay4AComObject(context->direct_play);
    context->selected_connection = nullptr;
    context->session_descriptor_data.clear();
    context->session_descriptor = nullptr;

    HRESULT result = AcquireDirectPlay4AComObject(context->direct_play);
    if (FAILED(result)) {
        return result;
    }

    if (connection.connection_data.empty()) {
        g_async_com_state.last_result = DPERR_INVALIDPARAMS;
        ReleaseDirectPlay4AComObject(context->direct_play);
        return g_async_com_state.last_result;
    }

    result = context->direct_play->InitializeConnection(
        const_cast<u8*>(connection.connection_data.data()), 0);
    g_async_com_state.last_result = result;
    if (SUCCEEDED(result)) {
        context->selected_connection = &connection;
    }
    else {
        ReleaseDirectPlay4AComObject(context->direct_play);
    }
    return result;
}

void SelectDirectPlayConnectionRecordForContext(u32 connection_index, AsyncComContext* context) {
    if (connection_index >= g_async_com_state.connections.size()) {
        return;
    }
    InitializeDirectPlayConnection(
        g_async_com_state.connections[static_cast<std::size_t>(connection_index)], context);
}

HRESULT BuildDirectPlayTcpIpAddressBlob(const char* host_name, std::vector<u8>& out_address) {
    out_address.clear();

    LPDIRECTPLAYLOBBY3A lobby = nullptr;
    HRESULT result = CoCreateInstance(kClsidDirectPlayLobby, nullptr, CLSCTX_INPROC_SERVER,
        kIidDirectPlayLobby3A, reinterpret_cast<void**>(&lobby));
    g_async_com_state.last_result = result;
    if (FAILED(result)) {
        return result;
    }

    char host_buffer[200]{};
    if (host_name != nullptr) {
        lstrcpynA(host_buffer, host_name, static_cast<int>(sizeof(host_buffer)));
    }

    DPCOMPOUNDADDRESSELEMENT elements[2]{};
    elements[0].guidDataType = kDpaidServiceProvider;
    elements[0].dwDataSize = sizeof(kDpspTcpIp);
    elements[0].lpData = const_cast<GUID*>(&kDpspTcpIp);

    elements[1].guidDataType = kDpaidInet;
    elements[1].dwDataSize = static_cast<DWORD>(lstrlenA(host_buffer) + 1);
    elements[1].lpData = host_buffer;

    DWORD address_size = 0;
    result = lobby->CreateCompoundAddress(elements, 2, nullptr, &address_size);
    if (result != DPERR_BUFFERTOOSMALL) {
        lobby->Release();
        g_async_com_state.last_result = result;
        return result;
    }

    HGLOBAL address_handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, address_size);
    if (address_handle == nullptr) {
        lobby->Release();
        g_async_com_state.last_result = E_OUTOFMEMORY;
        return g_async_com_state.last_result;
    }

    void* address_memory = GlobalLock(address_handle);
    if (address_memory == nullptr) {
        GlobalFree(address_handle);
        lobby->Release();
        g_async_com_state.last_result = E_OUTOFMEMORY;
        return g_async_com_state.last_result;
    }

    result = lobby->CreateCompoundAddress(elements, 2, address_memory, &address_size);
    if (SUCCEEDED(result)) {
        const auto* first = static_cast<const u8*>(address_memory);
        out_address.assign(first, first + address_size);
    }

    GlobalUnlock(address_handle);
    GlobalFree(address_handle);
    lobby->Release();
    g_async_com_state.last_result = result;
    return result;
}

void ClearDirectPlaySessionRecords() {
    lock_async_state();
    g_async_com_state.sessions.clear();
    unlock_async_state();
}

void RefreshDirectPlaySessionRecords(AsyncComContext* context) {
    ClearDirectPlaySessionRecords();
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = DPERR_UNINITIALIZED;
        return;
    }

    DPSESSIONDESC2 descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.guidApplication = kRankerApplicationGuid;

    const HRESULT result = context->direct_play->EnumSessions(&descriptor, 0,
        EnumDirectPlaySessionsCallback, context, kOriginalEnumSessionsFlags);
    g_async_com_state.last_result = result;
}

HRESULT RefreshCurrentDirectPlaySessionDescriptor(AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = E_POINTER;
        return g_async_com_state.last_result;
    }

    context->session_descriptor_data.clear();
    context->session_descriptor = nullptr;

    DWORD size = 0;
    HRESULT result = context->direct_play->GetSessionDesc(nullptr, &size);
    if (result != DPERR_BUFFERTOOSMALL || size == 0) {
        g_async_com_state.last_result = result;
        return result;
    }

    context->session_descriptor_data.resize(size);
    result = context->direct_play->GetSessionDesc(context->session_descriptor_data.data(), &size);
    g_async_com_state.last_result = result;
    if (SUCCEEDED(result)) {
        context->session_descriptor =
            reinterpret_cast<const DPSESSIONDESC2*>(context->session_descriptor_data.data());
    }
    else {
        context->session_descriptor_data.clear();
    }
    return result;
}

HRESULT SetCurrentDirectPlaySessionJoinDisabled(bool disabled, DWORD max_players) {
    i32 transport_mode = -1;
    AsyncComContext* context = nullptr;
    lock_async_state();
    transport_mode = g_async_com_state.active_network_transport_mode;
    context = g_async_com_state.active_context;
    unlock_async_state();

    if (directplay_session_descriptor_mutation_skipped_for_transport(transport_mode)) {
        set_directplay_last_result(S_OK);
        return S_OK;
    }
    if (context == nullptr || context->direct_play == nullptr) {
        set_directplay_last_result(E_POINTER);
        return E_POINTER;
    }

    HRESULT result = RefreshCurrentDirectPlaySessionDescriptor(context);
    if (FAILED(result) || context->session_descriptor == nullptr) {
        return result;
    }

    DPSESSIONDESC2 updated = *context->session_descriptor;
    if (max_players != 0) {
        updated.dwMaxPlayers = max_players;
    }
    if (disabled) {
        updated.dwFlags |= kDirectPlaySessionJoinDisabledFlag;
    } else {
        updated.dwFlags &= ~kDirectPlaySessionJoinDisabledFlag;
    }

    result = context->direct_play->SetSessionDesc(&updated, 0);
    set_directplay_last_result(result);
    if (SUCCEEDED(result)) {
        result = RefreshCurrentDirectPlaySessionDescriptor(context);
    }
    return result;
}

HRESULT HostDirectPlaySessionWithPlayer(const char* session_name, bool password_required,
    const char* password, const char* player_name, const void* player_data,
    DWORD player_data_size, DWORD flags, AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = DPERR_UNINITIALIZED;
        return g_async_com_state.last_result;
    }

    DPSESSIONDESC2 descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.dwFlags = kOriginalHostSessionFlags;
    if (g_async_com_state.direct_play_protocol_enabled) {
        descriptor.dwFlags |= DPSESSION_DIRECTPLAYPROTOCOL;
    }
    if (password_required) {
        descriptor.dwFlags |= DPSESSION_PASSWORDREQUIRED;
    }
    descriptor.guidApplication = kRankerApplicationGuid;
    descriptor.lpszSessionNameA = const_cast<char*>(session_name);
    descriptor.lpszPasswordA = const_cast<char*>(password);
    descriptor.dwUser4 = password_required ? 1 : 0;

    HRESULT result = context->direct_play->Open(&descriptor, DPOPEN_CREATE);
    g_async_com_state.last_result = result;
    if (FAILED(result)) {
        context->direct_play->Close();
        return result;
    }

    result = create_local_directplay_player(context, player_name, player_data,
        player_data_size, flags);
    if (SUCCEEDED(result)) {
        context->is_host = true;
        result = RefreshCurrentDirectPlaySessionDescriptor(context);
    }
    if (FAILED(result)) {
        destroy_local_player(context);
        context->direct_play->Close();
    }
    return result;
}

HRESULT HostDirectPlayJwarSessionRecord(const char* session_name,
    const char* password, DWORD max_players, u32 version, const char* player_name,
    const void* player_data, DWORD player_data_size, DWORD flags,
    AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = E_POINTER;
        return g_async_com_state.last_result;
    }

    DirectPlaySessionRecord record{};
    record.session_name = session_name != nullptr ? session_name : "";
    record.descriptor.dwSize = sizeof(DPSESSIONDESC2);
    record.descriptor.dwFlags = kOriginalHostSessionFlags;
    if (password != nullptr) {
        record.descriptor.dwFlags |= DPSESSION_PASSWORDREQUIRED;
    }
    record.descriptor.guidApplication = kRankerApplicationGuid;
    record.descriptor.dwMaxPlayers = max_players;
    record.descriptor.lpszPasswordA = const_cast<char*>(password);
    record.descriptor.dwUser1 = kDirectPlayJwarHostUser1;
    record.descriptor.dwUser2 = kDirectPlayJwarMagic;
    record.descriptor.dwUser3 = version;

    return JoinDirectPlaySessionRecord(record, player_name, player_data,
        player_data_size, flags, context);
}

HRESULT JoinDirectPlaySessionWithPlayer(const GUID& session_guid, const char* session_name,
    const char* player_name, const void* player_data, DWORD player_data_size, DWORD flags,
    AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = DPERR_UNINITIALIZED;
        return g_async_com_state.last_result;
    }

    DPSESSIONDESC2 descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    if (g_async_com_state.direct_play_protocol_enabled) {
        descriptor.dwFlags |= DPSESSION_DIRECTPLAYPROTOCOL;
    }
    descriptor.guidInstance = session_guid;
    descriptor.lpszSessionNameA = const_cast<char*>(session_name);

    HRESULT result = context->direct_play->Open(&descriptor, DPOPEN_JOIN);
    g_async_com_state.last_result = result;
    if (FAILED(result)) {
        context->direct_play->Close();
        return result;
    }

    result = create_local_directplay_player(context, player_name, player_data,
        player_data_size, flags);
    if (SUCCEEDED(result)) {
        context->is_host = false;
        result = RefreshCurrentDirectPlaySessionDescriptor(context);
    }
    if (FAILED(result)) {
        destroy_local_player(context);
        context->direct_play->Close();
    }
    return result;
}

HRESULT JoinDirectPlaySessionRecord(DirectPlaySessionRecord& session_record,
    const char* player_name, const void* player_data, DWORD player_data_size, DWORD flags,
    AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = DPERR_UNINITIALIZED;
        return g_async_com_state.last_result;
    }

    DPSESSIONDESC2 descriptor = descriptor_for_session_record(session_record);
    HRESULT result = context->direct_play->Open(&descriptor, DPOPEN_CREATE);
    g_async_com_state.last_result = result;
    if (FAILED(result)) {
        context->direct_play->Close();
        return result;
    }

    result = create_local_directplay_player(context, player_name, player_data,
        player_data_size, flags);
    if (SUCCEEDED(result)) {
        context->is_host = true;
        session_record.descriptor.dwUser4 = context->local_player;
        DPSESSIONDESC2 updated = descriptor_for_session_record(session_record);
        result = context->direct_play->SetSessionDesc(&updated, 0);
        g_async_com_state.last_result = result;
    }
    if (SUCCEEDED(result)) {
        result = RefreshCurrentDirectPlaySessionDescriptor(context);
    }
    if (FAILED(result)) {
        destroy_local_player(context);
        context->direct_play->Close();
    }
    return result;
}

void ShutdownGlobalDirectPlaySession() {
    AsyncComContext* context = g_async_com_state.active_context;
    if (context == nullptr || context->direct_play == nullptr) {
        return;
    }
    g_async_com_state.last_result = context->direct_play->Close();
}

void DestroyGlobalDirectPlayPlayer() {
    AsyncComContext* context = g_async_com_state.active_context;
    if (context == nullptr || context->direct_play == nullptr) {
        return;
    }

    context->direct_play->DestroyPlayer(context->local_player);
    context->local_player = 0;
}

void ClearDirectPlayPlayerRecords() {
    lock_async_state();
    g_async_com_state.players.clear();
    unlock_async_state();
}

bool ClearDirectPlayPlayerId(DPID player_id) {
    bool cleared = false;
    u32 cleared_slot = 0;

    lock_async_state();
    for (std::size_t index = 0; index < g_async_com_state.players.size(); ++index) {
        DirectPlayPlayerRecord& player = g_async_com_state.players[index];
        if (player.player_id == player_id) {
            player.player_id = 0;
            cleared = true;
            cleared_slot = static_cast<u32>(index);
            break;
        }
    }
    unlock_async_state();

    if (cleared) {
        notify_player_slot_inactive(cleared_slot);
    }

    return cleared;
}

bool ClearDirectPlayPlayerSlotId(u32 player_slot, bool notify_inactive) {
    bool cleared = false;
    const bool valid_player_slot = player_slot < kPlayerSlotCount;

    lock_async_state();
    if (player_slot < g_async_com_state.players.size()) {
        g_async_com_state.players[player_slot].player_id = 0;
        cleared = true;
    }
    unlock_async_state();

    if ((cleared || valid_player_slot) && notify_inactive) {
        notify_player_slot_inactive(player_slot);
    }

    return cleared;
}

void RefreshDirectPlayPlayerRecords(AsyncComContext* context) {
    if (context == nullptr || context->direct_play == nullptr) {
        g_async_com_state.last_result = DPERR_UNINITIALIZED;
        return;
    }
    if (context->session_descriptor == nullptr) {
        return;
    }

    ClearDirectPlayPlayerRecords();
    GUID instance = context->session_descriptor->guidInstance;
    const HRESULT result = context->direct_play->EnumPlayers(&instance,
        EnumDirectPlayPlayersCallback, context, DPENUMPLAYERS_ALL);
    g_async_com_state.last_result = result;
}

void SetDirectPlayMessageDispatchMode(u32 mode) {
    lock_async_state();
    g_async_com_state.receive_dispatch_mode = mode;
    unlock_async_state();
}

void SetActiveNetworkTransportMode(i32 mode) {
    lock_async_state();
    g_async_com_state.active_network_transport_mode = mode;
    unlock_async_state();
}

void SetDirectPlayMode7DispatchEnabled(bool enabled) {
    lock_async_state();
    g_async_com_state.mode7_dispatch_enabled = enabled;
    unlock_async_state();
}

void SetDirectPlayMessageDispatchCallbacks(
    const DirectPlayMessageDispatchCallbacks& callbacks, void* user_data) {
    lock_async_state();
    g_async_com_state.message_callbacks = callbacks;
    g_async_com_state.message_callback_user_data = user_data;
    unlock_async_state();
}

void SetDirectPlayMode1ExpectedSequence(u32 channel, u32 sequence) {
    lock_async_state();
    if (channel < g_async_com_state.mode1_expected_sequences.size()) {
        g_async_com_state.mode1_expected_sequences[channel] = sequence;
    }
    unlock_async_state();
    SetMode1ReliableExpectedSequence(channel, sequence);
}

void ClearDirectPlayMode1UdpPeerAddresses() {
    lock_async_state();
    g_async_com_state.mode1_udp_peer_addresses = {};
    g_async_com_state.mode1_udp_peer_address_valid.fill(false);
    unlock_async_state();
}

void SetDirectPlayMode1UdpPeerAddress(u32 player_slot, const sockaddr_in& address) {
    lock_async_state();
    if (player_slot < g_async_com_state.mode1_udp_peer_addresses.size()) {
        g_async_com_state.mode1_udp_peer_addresses[player_slot] = address;
        g_async_com_state.mode1_udp_peer_address_valid[player_slot] =
            mode1_udp_address_ready(address);
    }
    unlock_async_state();
}

i32 SendMode1ReliablePayloadToPlayerDefault(const void* payload, u32 byte_count,
    u32 target_player) {
    if (payload == nullptr || byte_count == 0 || target_player >= kPlayerSlotCount) {
        set_mode1_send_status(-1);
        return -1;
    }

    i32 transport_mode = -1;
    lock_async_state();
    transport_mode = g_async_com_state.active_network_transport_mode;
    unlock_async_state();

    if (mode1_reliable_uses_legacy_udp_transport(transport_mode)) {
        const Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
        if (target_player == reliable.local_player_index) {
            set_mode1_send_status(0);
            return 0;
        }

        if (transport_mode == 0 && WizardNetRelayEnabled()) {
            const u32 target_member = WizardNetRelayMemberForPlayer(target_player);
            const bool sent = target_member != 0 &&
                QueueWizardNetRelayFrame(target_member,
                    kWizardNetRelayStreamMode1, payload, byte_count);
            record_mode1_udp_send_result(sent);
            const i32 status = sent ? 0 : -1;
            set_mode1_send_status(status);
            return status;
        }

        sockaddr_in target_address{};
        bool address_valid = false;
        lock_async_state();
        target_address = g_async_com_state.mode1_udp_peer_addresses[target_player];
        address_valid = g_async_com_state.mode1_udp_peer_address_valid[target_player];
        unlock_async_state();
        const bool sent = address_valid &&
            SendLegacyUdpChunks(byte_count, payload, target_address);
        record_mode1_udp_send_result(sent);
        const i32 status = sent ? 0 : -1;
        set_mode1_send_status(status);
        return status;
    }

    LPDIRECTPLAY4A direct_play = nullptr;
    DPID local_player = 0;
    DPID remote_player = 0;
    lock_async_state();
    AsyncComContext* context = g_async_com_state.active_context;
    if (context != nullptr && context->direct_play != nullptr &&
        target_player < g_async_com_state.players.size()) {
        direct_play = context->direct_play;
        direct_play->AddRef();
        local_player = context->local_player;
        remote_player = g_async_com_state.players[target_player].player_id;
    }
    unlock_async_state();

    if (direct_play == nullptr || remote_player == 0) {
        if (direct_play != nullptr) {
            direct_play->Release();
        }
        set_mode1_send_status(-1);
        return -1;
    }

    const HRESULT result = send_directplay_mode1_payload_with_original_retry(
        direct_play, local_player, remote_player, payload, byte_count);
    direct_play->Release();
    set_directplay_last_result(result);
    const i32 status = normalize_mode1_directplay_send_status(result);
    set_mode1_send_status(status);
    return status;
}

i32 BroadcastMode1ReliablePayloadDefault(const void* payload, u32 byte_count) {
    if (payload == nullptr || byte_count == 0) {
        set_mode1_send_status(-1);
        return -1;
    }

    i32 transport_mode = -1;
    lock_async_state();
    transport_mode = g_async_com_state.active_network_transport_mode;
    unlock_async_state();

    if (mode1_reliable_uses_legacy_udp_transport(transport_mode)) {
        const Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
        i32 status = 0;
        if (transport_mode == 0 && WizardNetRelayEnabled()) {
            for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
                if (slot == reliable.local_player_index ||
                    !mode1_slot_active_for_udp_send(slot, reliable)) {
                    continue;
                }
                const u32 target_member = WizardNetRelayMemberForPlayer(slot);
                const bool sent = target_member != 0 &&
                    QueueWizardNetRelayFrame(target_member,
                        kWizardNetRelayStreamMode1, payload, byte_count);
                record_mode1_udp_send_result(sent);
                if (!sent) {
                    status = -1;
                }
            }
            set_mode1_send_status(status);
            return status;
        }

        for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
            if (slot == reliable.local_player_index ||
                !mode1_slot_active_for_udp_send(slot, reliable)) {
                continue;
            }

            sockaddr_in target_address{};
            bool address_valid = false;
            lock_async_state();
            target_address = g_async_com_state.mode1_udp_peer_addresses[slot];
            address_valid = g_async_com_state.mode1_udp_peer_address_valid[slot];
            unlock_async_state();
            const bool sent = address_valid &&
                SendLegacyUdpChunks(byte_count, payload, target_address);
            record_mode1_udp_send_result(sent);
            if (!sent) {
                status = -1;
            }
        }
        set_mode1_send_status(status);
        return status;
    }

    LPDIRECTPLAY4A direct_play = nullptr;
    DPID local_player = 0;
    lock_async_state();
    AsyncComContext* context = g_async_com_state.active_context;
    if (context != nullptr && context->direct_play != nullptr) {
        direct_play = context->direct_play;
        direct_play->AddRef();
        local_player = context->local_player;
    }
    unlock_async_state();

    if (direct_play == nullptr) {
        set_mode1_send_status(-1);
        return -1;
    }

    const HRESULT result = send_directplay_mode1_payload_with_original_retry(
        direct_play, local_player, 0, payload, byte_count);
    direct_play->Release();
    set_directplay_last_result(result);
    const i32 status = normalize_mode1_directplay_send_status(result);
    set_mode1_send_status(status);
    return status;
}

void InstallDefaultMode1ReliableTransportCallbacks() {
    Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
    Mode1ReliableCallbacks callbacks = reliable.callbacks;
    callbacks.send_to_player = default_mode1_reliable_send_callback;
    callbacks.broadcast = default_mode1_reliable_broadcast_callback;
    callbacks.poll_transport_receive =
        default_mode1_reliable_poll_transport_receive_callback;
    SetMode1ReliableCallbacks(callbacks, reliable.callback_user_data);
}

void ProcessWizardNetRelayMemberLeftEvents() {
    u32 game_id = 0;
    u32 member_id = 0;
    while (TakeWizardNetRelayMemberLeft(game_id, member_id)) {
        const WizardNetRelayState relay = wizardnet_relay_state();
        u32 player_slot = WizardNetRelayPlayerForMember(member_id);
        if (player_slot >= kPlayerSlotCount && member_id == 1 &&
            !relay.host_mode) {
            player_slot = 0;
        }
        if (player_slot < kPlayerSlotCount) {
            SetWizardNetRelayPlayerMember(player_slot, 0);
            ClearDirectPlayPlayerSlotId(player_slot, true);
        }
        if (member_id == 1 && !relay.host_mode &&
            WizardNetRelayReadyForGame(game_id)) {
            ResetWizardNetRelayState();
            break;
        }
    }
}

void PumpLegacyUdpMode1Messages(AsyncComContext* context) {
    auto& network = legacy_network_state();
    const bool relay_enabled = WizardNetRelayEnabled();
    for (;;) {
        const std::size_t queued_before_receive =
            network.udp_receive_queue.size();
        if (relay_enabled) {
            PumpWizardNetRelayMode1Frames();
            ProcessWizardNetRelayMemberLeftEvents();
        } else {
            ReceiveLegacyUdpPacket();
        }
        const bool received_datagram =
            network.udp_receive_queue.size() > queued_before_receive;
        bool consumed_message = false;

        // One mode-1 datagram normally contains many 0x24-byte packets.  Drain
        // every complete message already in userspace before another recvfrom.
        // The previous one-recv-per-one-message loop grew the queue by up to
        // 38 packets while consuming only one, filled the 64 KiB queue, then
        // left the 32 KiB kernel buffer to overflow during combat bursts.
        while (network.udp_receive_queue.size() > 7) {
            const std::vector<u8>& queue = network.udp_receive_queue;
            const u32 packet_type = read_queue_u32(queue, 0);
            u32 message_size = 0;
            bool dispatch_message = false;

            if (packet_type == 0) {
                const u32 header_bytes = read_queue_u32(queue, 4) >> 24;
                if (queue.size() < header_bytes + 0x0b) {
                    break;
                }
                const u32 payload_bytes =
                    read_queue_u8(queue, header_bytes + 0x0b);
                message_size = header_bytes + 0x0c + payload_bytes;
                if (queue.size() < message_size) {
                    break;
                }
                dispatch_message = true;
            }
            else if (packet_type == 1) {
                message_size = read_queue_u32(queue, 4);
                if (queue.size() < message_size) {
                    break;
                }
                dispatch_message = true;
            }
            else {
                if (queue.size() < 0x0c) {
                    break;
                }
                message_size = read_queue_u32(queue, 8);
                if (queue.size() < message_size) {
                    break;
                }
            }
            if (message_size == 0) {
                break;
            }

            if (dispatch_message) {
                record_mode1_udp_receive_packet(message_size);
                const DirectPlayDispatchSnapshot snapshot =
                    snapshot_directplay_dispatch();
                AsyncComContext* target_context = context;
                if (target_context == nullptr) {
                    lock_async_state();
                    target_context = g_async_com_state.active_context;
                    unlock_async_state();
                }
                handle_mode1_player_directplay_message(target_context,
                    queue.data(), message_size, 0, 0, snapshot);
                invoke_message_handler(snapshot,
                    snapshot.callbacks.mode1_player_message, target_context,
                    queue.data(), message_size, 0, 0);
            }
            ConsumeLegacyUdpReceiveQueue(message_size);
            consumed_message = true;
        }

        if (!received_datagram && !consumed_message) {
            break;
        }
    }
}

DWORD WINAPI LegacyUdpMode1ReceiveThreadProc(LPVOID parameter) {
    return legacy_udp_mode1_receive_thread(parameter);
}

void PollLegacyUdpMode1MessagesForActiveTransport() {
    i32 mode = -1;
    AsyncComContext* context = nullptr;
    lock_async_state();
    mode = g_async_com_state.active_network_transport_mode;
    context = g_async_com_state.active_context;
    unlock_async_state();
    if (mode >= 0 && mode < 3) {
        PumpLegacyUdpMode1Messages(context);
    }
}

void StartLegacyUdpMode1ReceiveThread() {
    lock_async_state();
    g_async_com_state.legacy_udp_mode1_stop_requested = false;
    HANDLE thread = CreateThread(nullptr, 0, legacy_udp_mode1_receive_thread,
        nullptr, 0, &g_async_com_state.legacy_udp_mode1_thread_id);
    g_async_com_state.legacy_udp_mode1_thread = thread;
    unlock_async_state();
}

void StopLegacyUdpMode1ReceiveThread() {
    lock_async_state();
    g_async_com_state.legacy_udp_mode1_stop_requested = true;
    unlock_async_state();
}

void ConfigureDirectPlayMode6WindowDispatch(HWND window, bool post_type0_messages) {
    lock_async_state();
    g_async_com_state.mode6_dispatch_window = window;
    g_async_com_state.mode6_type0_window_post_enabled = post_type0_messages;
    unlock_async_state();
}

void ConfigureDirectPlayMode6DispatchLock(CRITICAL_SECTION* critical_section) {
    lock_async_state();
    g_async_com_state.mode6_dispatch_lock = critical_section;
    unlock_async_state();
}

void ConfigureDirectPlayMode7WindowDispatch(HWND window) {
    lock_async_state();
    g_async_com_state.mode7_dispatch_window = window;
    unlock_async_state();
}

i32 SendMode1ReliablePayloadToPlayer(const void* payload, u32 byte_count,
    u32 target_player) {
    return SendMode1ReliablePayloadToPlayerDefault(payload, byte_count,
        target_player);
}

void DispatchMode1DirectPlaySystemMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player,
    DPID to_player) {
    const DirectPlayDispatchSnapshot snapshot = snapshot_directplay_dispatch();
    handle_mode1_system_directplay_message(context, message, message_size,
        from_player, to_player, snapshot);
    invoke_message_handler(snapshot, snapshot.callbacks.mode1_system_message,
        context, message, message_size, from_player, to_player);
}

void DispatchMode1DirectPlayPlayerMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player,
    DPID to_player) {
    const DirectPlayDispatchSnapshot snapshot = snapshot_directplay_dispatch();
    handle_mode1_player_directplay_message(context, message, message_size,
        from_player, to_player, snapshot);
    invoke_message_handler(snapshot, snapshot.callbacks.mode1_player_message,
        context, message, message_size, from_player, to_player);
}

void DispatchMode1DirectPlayMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player,
    DPID to_player) {
    if (from_player == 0) {
        DispatchMode1DirectPlaySystemMessage(context, message, message_size,
            from_player, to_player);
    }
    else {
        DispatchMode1DirectPlayPlayerMessage(context, message, message_size,
            from_player, to_player);
    }
}

void DispatchMode6DirectPlayPlayerMessage(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player) {
    const DirectPlayDispatchSnapshot snapshot = snapshot_directplay_dispatch();
    handle_mode6_player_directplay_message(context, message, message_size, from_player,
        to_player, snapshot);
    invoke_message_handler(snapshot, snapshot.callbacks.mode6_player_message, context,
        message, message_size, from_player, to_player);
}

void DispatchMode6DirectPlaySystemMessage(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player) {
    const DirectPlayDispatchSnapshot snapshot = snapshot_directplay_dispatch();
    handle_mode6_system_directplay_message(context, message, message_size, from_player,
        to_player, snapshot);
    invoke_message_handler(snapshot, snapshot.callbacks.mode6_system_message, context,
        message, message_size, from_player, to_player);
}

void DispatchMode6DirectPlayMessageWithLock(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player) {
    const DirectPlayDispatchSnapshot snapshot = snapshot_directplay_dispatch();
    if (snapshot.mode6_dispatch_lock != nullptr) {
        EnterCriticalSection(snapshot.mode6_dispatch_lock);
    }
    if (from_player == 0) {
        DispatchMode6DirectPlaySystemMessage(context, message, message_size, from_player,
            to_player);
    }
    else {
        DispatchMode6DirectPlayPlayerMessage(context, message, message_size, from_player,
            to_player);
    }
    if (snapshot.mode6_dispatch_lock != nullptr) {
        LeaveCriticalSection(snapshot.mode6_dispatch_lock);
    }
}

void DispatchMode7DirectPlayPlayerMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player,
    DPID to_player) {
    const DirectPlayDispatchSnapshot snapshot = snapshot_directplay_dispatch();
    handle_mode7_player_directplay_message(context, message, message_size,
        from_player, to_player, snapshot);
    invoke_message_handler(snapshot, snapshot.callbacks.mode7_player_message,
        context, message, message_size, from_player, to_player);
}

void DispatchMode7DirectPlayMessage(AsyncComContext* context,
    const void* message, DWORD message_size, DPID from_player,
    DPID to_player) {
    if (from_player != 0) {
        DispatchMode7DirectPlayPlayerMessage(context, message, message_size,
            from_player, to_player);
    }
}

void DispatchDirectPlayReceivedMessage(AsyncComContext* context, const void* message,
    DWORD message_size, DPID from_player, DPID to_player) {
    if (context == nullptr || message == nullptr || message_size <= 3) {
        return;
    }

    DirectPlayDispatchSnapshot snapshot{};
    u32 mode = 0;
    bool mode7_enabled = false;
    lock_async_state();
    snapshot.callbacks = g_async_com_state.message_callbacks;
    snapshot.user_data = g_async_com_state.message_callback_user_data;
    snapshot.mode6_window = g_async_com_state.mode6_dispatch_window;
    snapshot.mode7_window = g_async_com_state.mode7_dispatch_window;
    snapshot.mode6_dispatch_lock = g_async_com_state.mode6_dispatch_lock;
    snapshot.mode6_type0_post_enabled = g_async_com_state.mode6_type0_window_post_enabled;
    mode = g_async_com_state.receive_dispatch_mode;
    mode7_enabled = g_async_com_state.mode7_dispatch_enabled;
    unlock_async_state();

    if (mode == 1) {
        DispatchMode1DirectPlayMessage(context, message, message_size,
            from_player, to_player);
    } else if (mode == 6) {
        DispatchMode6DirectPlayMessageWithLock(context, message, message_size, from_player,
            to_player);
    } else if (mode == 7 && mode7_enabled && snapshot.mode7_window != nullptr &&
        from_player != 0) {
        DispatchMode7DirectPlayPlayerMessage(context, message, message_size,
            from_player, to_player);
    }
}

const AsyncComRuntimeState& async_com_state() {
    return g_async_com_state;
}

}
#endif
