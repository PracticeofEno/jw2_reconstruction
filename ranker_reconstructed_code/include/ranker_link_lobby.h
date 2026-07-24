#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_raw_indexed_bitmap.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#endif

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr u32 kLinkLobbyLayoutTrcRecord = 0x16f;
constexpr u32 kLinkLobbyBackgroundBitmapRecord = 0x81;
constexpr u32 kLinkLobbyPanelBitmapRecord = 0x82;
constexpr u32 kLinkLobbyAvatarSelectedBitmapRecord = 0x112;
constexpr u32 kLinkLobbyAvatarAvailableBitmapRecord = 0x113;
constexpr u32 kLinkLobbyDownloadBitmapRecord = 0x97;
constexpr int kLinkLobbyListBoxId = 0x9e7;
constexpr int kLinkLobbyChatEditId = 0x9e8;
constexpr int kLinkLobbyStartButtonId = 0x9c6;
constexpr int kLinkLobbyCancelButtonId = IDCANCEL;
constexpr int kLinkLobbyHostResourceComboId = 0x9c4;
constexpr int kLinkLobbyStartResourceComboId = 0x9e9;
constexpr int kLinkLobbyScreenSizeComboId = 0x9eb;
constexpr int kLinkLobbyMapSelectionScrollId = 0x9ec;
constexpr int kLinkLobbyInfoPanelId = 0x9ed;
constexpr int kLinkLobbyGameListScrollId = 0x9ee;
constexpr int kLinkLobbyAvatarInfoId = 0x9fc;
constexpr int kLinkLobbyAvatarFirstId = 0x9f4;
constexpr int kLinkLobbyAvatarCount = 8;
constexpr int kLinkLobbyPlayerRoleComboFirstId = 0x9c7;
constexpr int kLinkLobbyTribeComboFirstId = 0x9cf;
constexpr int kLinkLobbyMapDownloadFirstId = 0x9d7;
constexpr int kLinkLobbyLatencyFirstId = 0x9df;
constexpr int kLinkLobbySendChatCommandId = 0x9ef;
constexpr int kLinkLobbyTabFirstId = 0x9f0;
constexpr int kLinkLobbyTabButtonCount = 4;
constexpr int kLinkLobbyLatencyBitmapCount = 6;
constexpr int kLinkLobbyAcceleratorResourceId = 0xfa;
constexpr UINT kLinkLobbyNetworkMessage = 0x465;
constexpr UINT kLinkLobbyDirectPlayStartMessage = 0x501;
constexpr UINT kLinkLobbyCancelStartMessage = 0x503;
constexpr UINT kLinkLobbyCopiedPayloadMessage = 0x504;
constexpr UINT kLinkLobbyStartDecisionMessage = 0x505;
constexpr UINT kLinkLobbyStartAcceptedMessage = 0x507;
constexpr UINT kLinkLobbyDownloadProgressMessage = 0x508;
constexpr UINT kLinkLobbyOwnerSyncMessage = 0x50e;
constexpr UINT kLinkLobbySocketMessage = 0x50f;
constexpr UINT kLinkLobbyStartSyncPumpMessage = 0x510;
constexpr std::size_t kLinkLobbyPlayerPayloadBytes = 0x3f4;
constexpr std::size_t kLinkLobbyPlayerPayloadBodyBytes = 0x3a0;
constexpr std::size_t kLinkLobbyPlayerPayloadBodyOffset = 0x10;
constexpr std::size_t kLinkLobbyAvatarPayloadBytes = 0x74;
constexpr std::size_t kLinkLobbyAvatarInvalidMarkerOffset = 0x14;
constexpr std::size_t kLinkLobbyAvatarPublishPacketBytes = 0x3b0;
constexpr u32 kLinkLobbyAvatarPublishOpcode = 0x29;

struct LinkLobbyState;
struct LegacyAsyncTcpSocket;

using LinkLobbyActionCallback = void (*)(LinkLobbyState& state);
using LinkLobbyBoolCallback = bool (*)(LinkLobbyState& state);
using LinkLobbyPlayerCallback = void (*)(LinkLobbyState& state, int player_index);
using LinkLobbyMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);
using LinkLobbyPacketCallback = void (*)(LinkLobbyState& state, const void* packet,
    i32 byte_count);
using LinkLobbyProgressCallback = bool (*)(LinkLobbyState& state, int player_index,
    int progress_value);
using LinkLobbyStartSyncCallback = bool (*)(LinkLobbyState& state, int from_player,
    int to_player, SOCKET target_socket);

struct LinkLobbyCallbacks {
    LinkLobbyActionCallback resume_connect_modal = nullptr;
    LinkLobbyActionCallback open_connect_frontend = nullptr;
    LinkLobbyActionCallback open_online_lobby = nullptr;
    LinkLobbyActionCallback open_p2p_lobby = nullptr;
    LinkLobbyActionCallback open_ipx_lobby = nullptr;
    LinkLobbyActionCallback open_replay_or_observer_lobby = nullptr;
    LinkLobbyActionCallback start_game = nullptr;
    LinkLobbyActionCallback shutdown_network = nullptr;
    LinkLobbyPlayerCallback report_timeout = nullptr;
    LinkLobbyMessageCallback show_message = nullptr;
    LinkLobbyPacketCallback queue_packet = nullptr;
    LinkLobbyProgressCallback send_map_download_progress = nullptr;
    LinkLobbyStartSyncCallback send_start_sync_retry = nullptr;
    LinkLobbyBoolCallback finalize_start_sync = nullptr;
};

struct LinkLobbyLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct LinkLobbyWindowControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct LinkLobbyMessageSegment {
    COLORREF color = 0;
    std::string text;
};

struct LinkLobbyMessageLine {
    std::array<LinkLobbyMessageSegment, 2> segments{};
    int segment_count = 0;
    std::string plain_text;
};

struct LinkLobbyPlayerSlot {
    bool occupied = false;
    bool selected = false;
    bool ready = false;
    bool human = false;
    u8 tribe = 0;
    u8 team = 0;
    u32 latency = 0;
    std::array<u8, kLinkLobbyPlayerPayloadBytes> raw_payload{};
    std::array<char, 0x40> name{};
};

struct LinkLobbyState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    void* session_context = nullptr;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    BitmapMemoryResource panel_background;
    BitmapMemoryResource avatar_selected_background;
    BitmapMemoryResource avatar_available_background;
    BitmapMemoryResource download_background;
    RawIndexedBitmapStrip avatar_strip;

    LinkLobbyWindowControl game_list;
    LinkLobbyWindowControl chat_edit;
    LegacyCustomScrollControl game_list_scroll;
    LegacyCustomScrollControl map_selection_scroll;
    LegacyImageButtonControl game_info_button;
    LegacyImageButtonControl start_button;
    LegacyImageButtonControl cancel_button;
    LegacyImageButtonControl avatar_info_button;
    LegacyImageComboBoxControl host_resource_combo;
    LegacyImageComboBoxControl start_resource_combo;
    LegacyImageComboBoxControl screen_size_combo;
    std::array<LegacyImageComboBoxControl, kLinkLobbyAvatarCount> player_role_combos{};
    std::array<LegacyImageComboBoxControl, kLinkLobbyAvatarCount> tribe_combos{};
    std::array<LegacyImageButtonControl, kLinkLobbyTabButtonCount> tab_buttons{};
    std::array<LegacyImageButtonControl, kLinkLobbyAvatarCount> avatar_buttons{};
    std::array<LegacyImageButtonControl, kLinkLobbyAvatarCount> latency_buttons{};
    std::array<LegacyImageButtonControl, kLinkLobbyAvatarCount> map_download_buttons{};
    std::array<BitmapMemoryResource, kLinkLobbyLatencyBitmapCount> latency_bitmaps{};
    std::array<std::array<u8, kLinkLobbyPlayerPayloadBytes>, kLinkLobbyAvatarCount>
        player_payloads{};
    std::array<std::array<u8, kLinkLobbyAvatarPayloadBytes>, kLinkLobbyAvatarCount>
        avatar_payloads{};

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::array<LinkLobbyPlayerSlot, kLinkLobbyAvatarCount> players{};
    std::array<u8, kLinkLobbyAvatarCount> randomized_slots{};
    std::array<u8, kLinkLobbyAvatarCount> start_states{};
    std::array<u8, kLinkLobbyAvatarCount> start_acknowledged{};
    std::array<u8, kLinkLobbyAvatarCount> secondary_start_acknowledged{};
    std::array<u8, kLinkLobbyAvatarCount> tribe_choices{};
    std::array<SOCKET, kLinkLobbyAvatarCount> player_sockets{};
    std::array<sockaddr_in, kLinkLobbyAvatarCount> udp_peer_addresses{};
    sockaddr_in local_udp_reflexive_address{};
    std::array<std::array<char, 0x10>, kLinkLobbyAvatarCount> primary_peer_hosts{};
    std::array<std::array<char, 0x10>, kLinkLobbyAvatarCount> secondary_peer_hosts{};
    std::array<u16, kLinkLobbyAvatarCount> primary_peer_ports{};
    std::array<u16, kLinkLobbyAvatarCount> secondary_peer_ports{};
    std::array<u32, kLinkLobbyAvatarCount> peer_route_acknowledged{};
    std::array<char, 0x100> default_peer_probe_host{};
    u16 default_tcp_port = 0;
    u16 default_udp_port = 0;
    u16 default_peer_probe_port = 0;
    int local_player_index = 0;
    int selected_avatar_index = -1;
    int mode = 0;
    int game_type = 0;
    int start_resource_index = 0;
    int host_resource_index = 0;
    int screen_size_index = 0;
    int map_selection_index = 0;
    int active_human_count = 0;
    int selected_avatar_count = 0;
    int tab_button_count = kLinkLobbyTabButtonCount;
    int countdown_value = -1;
    UINT_PTR countdown_timer = 0;
    UINT_PTR start_sync_timer = 0;
    UINT_PTR peer_route_timer = 0;
    UINT_PTR combo_refresh_timer = 0;
    std::array<int, kLinkLobbyTabButtonCount> tab_button_positions{};
    std::array<std::array<char, 0x20>, kLinkLobbyTabButtonCount> tab_button_labels{};
    std::array<COLORREF, kLinkLobbyTabButtonCount> tab_text_colors{};
    std::array<int, kLinkLobbyAvatarCount> player_row_y{};
    std::array<int, kLinkLobbyAvatarCount> latency_values{};
    std::array<int, kLinkLobbyAvatarCount> map_download_progress{};
    std::array<int, kLinkLobbyAvatarCount> player_role_values{};
    std::array<int, kLinkLobbyAvatarCount> player_team_values{};
    std::array<u32, kLinkLobbyAvatarCount> player_role_option_masks{};
    std::array<u32, kLinkLobbyAvatarCount> tribe_option_masks{};
    std::array<bool, kLinkLobbyAvatarCount> player_socket_connected{};
    bool visible = false;
    bool host_mode = false;
    bool start_locked = false;
    bool returned_to_connect = false;
    bool resources_ready = false;
    bool download_visible = false;
    bool join_accepted = false;
    bool join_request_pending = false;
    bool store_avatar_publish_locally = false;
    bool map_download_candidate_valid = false;
    bool expected_map_file_time_valid = false;
    bool socket_critical_section_initialized = false;
    bool start_sync_complete = false;
    bool secondary_start_sync_required = false;
    bool directplay_join_disabled = false;
    bool udp_probe_route_toggle = false;
    bool local_udp_reflexive_address_valid = false;
    int map_download_state = 0;
    int last_map_download_progress_value = -1;
    u32 map_download_received_bytes = 0;
    u32 last_transport_rate_time_ms = 0;
    u32 start_sync_retry_count = 0;
    u32 start_sync_retry_interval_ms = 300;
    u32 start_sync_retry_limit_ms = 0x1389;
    u32 expected_map_file_size = 0;
    SOCKET shared_peer_socket = INVALID_SOCKET;
    SOCKET pending_join_socket = INVALID_SOCKET;
    FILETIME expected_map_file_time{};
    CRITICAL_SECTION socket_critical_section{};
    std::array<u8, 0x2dc> map_descriptor{};
    std::array<u8, 0x196> session_seed_payload{};
    std::array<char, 10> password{};
    std::vector<LinkLobbyLayoutRect> layout;
    std::vector<u8> start_parameter_payload;
    std::vector<LinkLobbyMessageLine> message_lines;
    std::string map_file_name;
    std::string prepared_map_path;
    std::string last_message;
    LinkLobbyCallbacks callbacks{};
};

LinkLobbyState& link_lobby_state();

int CountLinkLobbySelectedAvatarSlots(const LinkLobbyState& state);
void InstallLinkLobbyAccelerators(LinkLobbyState& state);
void RestoreLinkLobbyAccelerators(LinkLobbyState& state);
void MarkLinkLobbyResourcesReady(LinkLobbyState& state);
void PrepareLinkLobbyStartParameters(LinkLobbyState& state);
void BeginLinkLobbyStartCountdown(LinkLobbyState& state);
bool SubmitLinkLobbyStartRequest(LinkLobbyState& state);
void ReportLinkLobbyPlayerStartTimeout(LinkLobbyState& state, int player_index);
void ReturnFromLinkLobby(LinkLobbyState& state);
void SwapLinkLobbyPlayerPayloads(LinkLobbyState& state, int left_player, int right_player);
bool CopyIncomingLinkLobbyPlayerPayload(LinkLobbyState& state, const void* message,
    std::size_t byte_count);
void PublishLinkLobbySelectedAvatarPayloads(LinkLobbyState& state, int player_index);
bool CreateLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index,
    int y);
void InitializeLinkLobbyPlayerRoleComboControl0(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor0(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl0(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboStatic1(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboControl1(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor1(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl1(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboStatic2(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboControl2(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor2(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl2(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboStatic3(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboControl3(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor3(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl3(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboStatic4(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboControl4(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor4(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl4(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboStatic5(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboControl5(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor5(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl5(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboStatic6(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboControl6(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor6(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl6(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboStatic7(LinkLobbyState& state);
void InitializeLinkLobbyPlayerRoleComboControl7(LinkLobbyState& state);
void RegisterLinkLobbyPlayerRoleComboDestructor7(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboControl7(LinkLobbyState& state);
void DestroyLinkLobbyPlayerRoleComboBoxes(LinkLobbyState& state);
void PopulateLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index,
    int role_value);
void EnableLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index);
void DisableLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index);
bool CreateLinkLobbyPlayerRoleControls(LinkLobbyState& state);
void SwapLinkLobbyPlayerSlots(LinkLobbyState& state, int left_player, int right_player);
void ResetLinkLobbyPlayerRoleToHuman(LinkLobbyState& state, int player_index);
bool CreateLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index);
void InitializeLinkLobbyTribeComboControl0(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor0(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl0(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboStatic1(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboControl1(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor1(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl1(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboStatic2(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboControl2(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor2(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl2(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboStatic3(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboControl3(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor3(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl3(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboStatic4(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboControl4(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor4(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl4(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboStatic5(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboControl5(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor5(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl5(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboStatic6(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboControl6(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor6(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl6(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboStatic7(LinkLobbyState& state);
void InitializeLinkLobbyTribeComboControl7(LinkLobbyState& state);
void RegisterLinkLobbyTribeComboDestructor7(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboControl7(LinkLobbyState& state);
void DestroyLinkLobbyTribeComboBoxes(LinkLobbyState& state);
void PopulateLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index);
void UpdateLinkLobbyTribeComboBoxState(LinkLobbyState& state, int player_index);
void EnableLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index);
void DisableLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index);
void ShowLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index);
void HideLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index);
void HandleLinkLobbyTribeComboChange(LinkLobbyState& state, int player_index);
void HandleLinkLobbyStartResult(LinkLobbyState& state, u32 player_index,
    int result_code);
void HandleLinkLobbyPlayerDisconnected(LinkLobbyState& state, u32 player_index);
void HandleLinkLobbyPlayerRoleComboChange(LinkLobbyState& state, int player_index);
int FindOpenLinkLobbyPlayerRoleSlot(const LinkLobbyState& state);
void ApplyLinkLobbyPlayerRolePacket(LinkLobbyState& state, u32 player_index,
    int role_value);
void ApplyLinkLobbyOpenRolePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyComputerRolePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyClosedRolePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void BroadcastLinkLobbyRoleSelections(LinkLobbyState& state);
void ForwardLinkLobbyRelayPacket(LinkLobbyState& state, SOCKET sender_socket,
    const void* packet, std::size_t byte_count);
void DispatchLinkLobbyStartResultPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void HandleLinkLobbyIncomingPlayerJoinRequest(LinkLobbyState& state, u32 sender,
    const void* packet, std::size_t byte_count);
void ApplyLinkLobbyPlayerRecordPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void HandleLinkLobbyPlayerDisconnectPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count);
void HandleLinkLobbyPlayerRemovalPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void HandleLinkLobbyHostClosedPacket(LinkLobbyState& state);
void HandleLinkLobbyAutoMoveOpenSlotPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyPlayerSlotSwapPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyTribeSelectionPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyStartResourceSelectionPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count);
void ApplyLinkLobbyHostResourceSelectionPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count);
void ApplyLinkLobbyScreenSizeSelectionPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count);
void ApplyLinkLobbyMapSelectionPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyMapDescriptorPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyMapDownloadChunkPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyMapDownloadProgressPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count);
void ApplyLinkLobbyMapDownloadRequestPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count);
void ApplyLinkLobbyStartParametersPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void HandleLinkLobbyStartTimeoutPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbySessionSeedPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyPlayerPresencePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void SetLinkLobbyLocalPlayerIdentity(LinkLobbyState& state, const char* player_name);
void BeginLinkLobbyPeerRouteSync(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyPeerRoutePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void HandleLinkLobbyUdpPeerProbeRequest(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbyUdpPeerProbeReply(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void ApplyLinkLobbySecondaryStartAckPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void StopLinkLobbyPeerRouteTimer(LinkLobbyState& state);
void DispatchLinkLobbyTransportPacket(LinkLobbyState& state, u32 sender,
    const void* packet, std::size_t byte_count);
bool SendLinkLobbyRawTransportPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* packet, std::size_t byte_count);
void BroadcastLinkLobbyTransportPacketExcept(LinkLobbyState& state,
    const void* packet, std::size_t byte_count, SOCKET excluded_socket);
bool SendLinkLobbyTransportPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* packet, std::size_t byte_count);
void SendLinkLobbyOpenRolePacket(LinkLobbyState& state, int player_index,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyComputerRolePacket(LinkLobbyState& state, int player_index,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyClosedRolePacket(LinkLobbyState& state, int player_index,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyRoleBroadcastRequest(LinkLobbyState& state);
void SendLinkLobbyStartResultPacket(LinkLobbyState& state, SOCKET target_socket,
    u32 player_index, u32 result_code);
void SendLinkLobbyJoinRequestPacket(LinkLobbyState& state, u32 player_index,
    const void* player_record, SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyPlayerRecordPacket(LinkLobbyState& state, u32 player_index,
    const void* player_record, SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyPlayerDisconnectPacket(LinkLobbyState& state, u32 player_index);
void SendLinkLobbyPlayerRemovalPacket(LinkLobbyState& state, u32 player_index,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyHostClosedPacket(LinkLobbyState& state, SOCKET target_socket);
void ResetLinkLobbyTransportRateTimer(LinkLobbyState& state);
bool IsLinkLobbyTransportRateElapsed(LinkLobbyState& state);
void SendLinkLobbyAutoMoveOpenSlotPacket(LinkLobbyState& state, u32 player_index,
    u32 group_index);
void SendLinkLobbySlotSwapPacket(LinkLobbyState& state, u32 left_player,
    u32 right_player);
void SendLinkLobbyRelayJoinPacket(LinkLobbyState& state, SOCKET target_socket,
    const char* player_name, const char* password);
void SendLinkLobbyTribeSelectionPacket(LinkLobbyState& state, u32 player_index,
    u32 selection, SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyHostResourceSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyStartResourceSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyScreenSizeSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyMapSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyMapDescriptorPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* descriptor, std::size_t byte_count);
void SendLinkLobbyMapChunkPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* packet, std::size_t byte_count);
void SendLinkLobbyMapProgressPacket(LinkLobbyState& state, u32 player_index,
    u32 progress);
void SendLinkLobbyMapRequestPacket(LinkLobbyState& state, u32 player_index,
    u32 requested_offset);
void SendLinkLobbyPacketToAll(LinkLobbyState& state, const void* packet,
    std::size_t byte_count);
void SendLinkLobbyReservedPacket0x14(LinkLobbyState& state, u32 value);
void SendLinkLobbyReservedSelectionPacket0x05(LinkLobbyState& state, u32 selection,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyReservedSelectionPacket0x06(LinkLobbyState& state, u32 selection,
    SOCKET target_socket = INVALID_SOCKET);
void SendLinkLobbyReservedPairPacket0x1a(LinkLobbyState& state, u32 first,
    u32 second);
void SendLinkLobbyReservedPairPacket0x1b(LinkLobbyState& state, u32 first,
    u32 second);
void SendLinkLobbyReservedOneValuePacket0x1c(LinkLobbyState& state, u32 value);
bool SendLinkLobbyStartParametersPacket(LinkLobbyState& state);
void SendLinkLobbyStartTimeoutPacket(LinkLobbyState& state, u32 player_index);
void SendLinkLobbyPlayerPresencePacket(LinkLobbyState& state, SOCKET target_socket,
    u32 player_index);
void DispatchLinkLobbyRelayPacket(LinkLobbyState& state, SOCKET sender_socket,
    const void* packet, std::size_t byte_count);
void SendLinkLobbyCurrentRoleStatePackets(LinkLobbyState& state);
bool SendLinkLobbyPeerRouteTablePacket(LinkLobbyState& state);
void SendLinkLobbyPeerRoutePacket(LinkLobbyState& state, u32 player_index,
    const char* primary_host, u16 primary_port, const char* secondary_host,
    u16 secondary_port);
void SendLinkLobbyUdpProbeRequestPacket(LinkLobbyState& state, u32 player_index,
    u32 target_player, SOCKET target_socket);
void SendLinkLobbyUdpProbeDatagram(LinkLobbyState& state, u32 player_index,
    const char* host, u16 port);
void SendLinkLobbySecondaryStartAckPacket(LinkLobbyState& state, u32 player_index);
void SendLinkLobbyStopPeerRouteTimerPacket(LinkLobbyState& state);
void InitializeLinkLobbyHostResourceComboControl(LinkLobbyState& state);
void RegisterLinkLobbyHostResourceComboShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyHostResourceComboControl(LinkLobbyState& state);
void InitializeLinkLobbyStartResourceComboSupport(LinkLobbyState& state);
void InitializeLinkLobbyStartResourceComboControl(LinkLobbyState& state);
void RegisterLinkLobbyStartResourceComboShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyStartResourceComboControl(LinkLobbyState& state);
void InitializeLinkLobbyScreenSizeComboSupport(LinkLobbyState& state);
void InitializeLinkLobbyScreenSizeComboControl(LinkLobbyState& state);
void RegisterLinkLobbyScreenSizeComboShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyScreenSizeComboControl(LinkLobbyState& state);
void InitializeLinkLobbyPrimaryScrollSupport(LinkLobbyState& state);
void InitializeLinkLobbyPrimaryScrollControl(LinkLobbyState& state);
void RegisterLinkLobbyPrimaryScrollShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyPrimaryScrollControl(LinkLobbyState& state);
void InitializeLinkLobbySecondaryScrollSupport(LinkLobbyState& state);
void InitializeLinkLobbySecondaryScrollControl(LinkLobbyState& state);
void RegisterLinkLobbySecondaryScrollShutdown(LinkLobbyState& state);
void ShutdownLinkLobbySecondaryScrollControl(LinkLobbyState& state);
void InitializeLinkLobbyGameInfoButtonSupport(LinkLobbyState& state);
void InitializeLinkLobbyGameInfoButton(LinkLobbyState& state);
void RegisterLinkLobbyGameInfoButtonShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyGameInfoButton(LinkLobbyState& state);
void InitializeLinkLobbyStartButtonSupport(LinkLobbyState& state);
void InitializeLinkLobbyStartButton(LinkLobbyState& state);
void RegisterLinkLobbyStartButtonShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyStartButton(LinkLobbyState& state);
void InitializeLinkLobbyCancelButtonSupport(LinkLobbyState& state);
void InitializeLinkLobbyCancelButton(LinkLobbyState& state);
void RegisterLinkLobbyCancelButtonShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyCancelButton(LinkLobbyState& state);
void InitializeLinkLobbyAvatarInfoButtonSupport(LinkLobbyState& state);
void InitializeLinkLobbyAvatarInfoButton(LinkLobbyState& state);
void RegisterLinkLobbyAvatarInfoButtonShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyAvatarInfoButton(LinkLobbyState& state);
void InitializeLinkLobbyBackgroundResourceAndShutdown(LinkLobbyState& state);
void InitializeLinkLobbyBackgroundBitmap(LinkLobbyState& state);
void RegisterLinkLobbyBackgroundShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyBackgroundBitmap(LinkLobbyState& state);
void InitializeLinkLobbyPanelResourceAndShutdown(LinkLobbyState& state);
void InitializeLinkLobbyPanelBitmap(LinkLobbyState& state);
void RegisterLinkLobbyPanelShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyPanelBitmap(LinkLobbyState& state);
void InitializeLinkLobbyAvatarStripResourceAndShutdown(LinkLobbyState& state);
void InitializeLinkLobbyAvatarStrip(LinkLobbyState& state);
void RegisterLinkLobbyAvatarStripShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyAvatarStrip(LinkLobbyState& state);
void InitializeLinkLobbyAvatarButtonArraySupport(LinkLobbyState& state);
void RegisterLinkLobbyAvatarButtonArrayShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyAvatarButtonArray(LinkLobbyState& state);
void InitializeLinkLobbyAvatarSelectedResourceAndShutdown(LinkLobbyState& state);
void InitializeLinkLobbyAvatarSelectedBitmap(LinkLobbyState& state);
void RegisterLinkLobbyAvatarSelectedShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyAvatarSelectedBitmap(LinkLobbyState& state);
void InitializeLinkLobbyAvatarAvailableResourceAndShutdown(LinkLobbyState& state);
void InitializeLinkLobbyAvatarAvailableBitmap(LinkLobbyState& state);
void RegisterLinkLobbyAvatarAvailableShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyAvatarAvailableBitmap(LinkLobbyState& state);
void InitializeLinkLobbyDownloadResourceAndShutdown(LinkLobbyState& state);
void InitializeLinkLobbyDownloadBitmap(LinkLobbyState& state);
void RegisterLinkLobbyDownloadShutdown(LinkLobbyState& state);
void ShutdownLinkLobbyDownloadBitmap(LinkLobbyState& state);
void InitializeLinkLobbyTabButtonControl0(LinkLobbyState& state);
void RegisterLinkLobbyTabButtonDestructor0(LinkLobbyState& state);
void DestroyLinkLobbyTabButtonControl0(LinkLobbyState& state);
void InitializeLinkLobbyTabButton1(LinkLobbyState& state);
void InitializeLinkLobbyTabButtonControl1(LinkLobbyState& state);
void RegisterLinkLobbyTabButtonDestructor1(LinkLobbyState& state);
void DestroyLinkLobbyTabButtonControl1(LinkLobbyState& state);
void InitializeLinkLobbyTabButton2(LinkLobbyState& state);
void InitializeLinkLobbyTabButtonControl2(LinkLobbyState& state);
void RegisterLinkLobbyTabButtonDestructor2(LinkLobbyState& state);
void DestroyLinkLobbyTabButtonControl2(LinkLobbyState& state);
void InitializeLinkLobbyTabButton3(LinkLobbyState& state);
void InitializeLinkLobbyTabButtonControl3(LinkLobbyState& state);
void RegisterLinkLobbyTabButtonDestructor3(LinkLobbyState& state);
void DestroyLinkLobbyTabButtonControl3(LinkLobbyState& state);
void InitializeLinkLobbyTabButtons(LinkLobbyState& state);
void DestroyLinkLobbyTabButtons(LinkLobbyState& state);
bool CreateLinkLobbyTabButton(LinkLobbyState& state, int tab_index, int position);
bool CreateLinkLobbyTabButtons(LinkLobbyState& state);
void DrawLinkLobbyTabButton(LinkLobbyState& state, int tab_index,
    const DRAWITEMSTRUCT& draw);
void NoOpLinkLobbyUnusedOwnerDrawControl(LinkLobbyState& state,
    const DRAWITEMSTRUCT& draw);
void InitializeLinkLobbyLatencyButtonControl0(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor0(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl0(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButton1(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtonControl1(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor1(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl1(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButton2(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtonControl2(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor2(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl2(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButton3(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtonControl3(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor3(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl3(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButton4(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtonControl4(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor4(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl4(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButton5(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtonControl5(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor5(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl5(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButton6(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtonControl6(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor6(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl6(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButton7(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtonControl7(LinkLobbyState& state);
void RegisterLinkLobbyLatencyButtonDestructor7(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtonControl7(LinkLobbyState& state);
void InitializeLinkLobbyLatencyButtons(LinkLobbyState& state);
void DestroyLinkLobbyLatencyButtons(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmapResource0(LinkLobbyState& state);
void RegisterLinkLobbyLatencyBitmapDestructor0(LinkLobbyState& state);
void DestroyLinkLobbyLatencyBitmapResource0(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmap1(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmapResource1(LinkLobbyState& state);
void RegisterLinkLobbyLatencyBitmapDestructor1(LinkLobbyState& state);
void DestroyLinkLobbyLatencyBitmapResource1(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmap2(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmapResource2(LinkLobbyState& state);
void RegisterLinkLobbyLatencyBitmapDestructor2(LinkLobbyState& state);
void DestroyLinkLobbyLatencyBitmapResource2(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmap3(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmapResource3(LinkLobbyState& state);
void RegisterLinkLobbyLatencyBitmapDestructor3(LinkLobbyState& state);
void DestroyLinkLobbyLatencyBitmapResource3(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmap4(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmapResource4(LinkLobbyState& state);
void RegisterLinkLobbyLatencyBitmapDestructor4(LinkLobbyState& state);
void DestroyLinkLobbyLatencyBitmapResource4(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmap5(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmapResource5(LinkLobbyState& state);
void RegisterLinkLobbyLatencyBitmapDestructor5(LinkLobbyState& state);
void DestroyLinkLobbyLatencyBitmapResource5(LinkLobbyState& state);
void InitializeLinkLobbyLatencyBitmaps(LinkLobbyState& state);
void LoadLinkLobbyLatencyBitmaps(LinkLobbyState& state);
void ReleaseLinkLobbyLatencyBitmaps(LinkLobbyState& state);
bool CreateLinkLobbyLatencyButton(LinkLobbyState& state, int player_index);
void ShowLinkLobbyLatencyButton(LinkLobbyState& state, int player_index);
void HideLinkLobbyLatencyButton(LinkLobbyState& state, int player_index);
void UpdateLinkLobbyLatencyButtonVisibility(LinkLobbyState& state, int player_index);
void DrawLinkLobbyLatencyButton(LinkLobbyState& state, int player_index,
    const DRAWITEMSTRUCT& draw);
void InitializeLinkLobbyMapDownloadButtonControl0(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor0(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl0(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButton1(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtonControl1(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor1(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl1(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButton2(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtonControl2(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor2(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl2(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButton3(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtonControl3(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor3(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl3(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButton4(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtonControl4(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor4(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl4(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButton5(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtonControl5(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor5(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl5(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButton6(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtonControl6(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor6(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl6(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButton7(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtonControl7(LinkLobbyState& state);
void RegisterLinkLobbyMapDownloadButtonDestructor7(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtonControl7(LinkLobbyState& state);
void InitializeLinkLobbyMapDownloadButtons(LinkLobbyState& state);
void DestroyLinkLobbyMapDownloadButtons(LinkLobbyState& state);
bool CreateLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index);
void ShowLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index);
void HideLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index);
void DrawLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index,
    const DRAWITEMSTRUCT& draw);
void RedrawLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index);
void UpdateLinkLobbyMapDownloadButtonVisibility(LinkLobbyState& state, int player_index);
bool CheckLinkLobbyMapFileMatchesExpected(LinkLobbyState& state, const char* path);
bool PrepareLinkLobbyMapDownload(LinkLobbyState& state);
bool ReportLinkLobbyMapDownloadWaiters(LinkLobbyState& state);
void InitializeLinkLobbySocketCriticalSection(LinkLobbyState& state);
void DeleteLinkLobbySocketCriticalSection(LinkLobbyState& state);
bool InitializeLinkLobbyNetworkRoute(LinkLobbyState& state);
void ShutdownLinkLobbyNetworkRoute(LinkLobbyState& state);
void HandleLinkLobbyListenSocketEvent(LinkLobbyState& state, WPARAM socket,
    LPARAM event);
void PumpLinkLobbySocketReceiveQueue(LinkLobbyState& state, SOCKET sender_socket,
    void* socket_record);
void HandleLinkLobbyPeerSocketEvent(LinkLobbyState& state, WPARAM socket,
    LPARAM event);
void HandleLinkLobbyAsyncTcpSocketEvent(LinkLobbyState& state, WPARAM socket,
    LPARAM event);
void PumpLinkLobbyUdpStartSync(LinkLobbyState& state);
bool AreLinkLobbyStartAcksComplete(const LinkLobbyState& state);
void ReportLinkLobbyStartAckWaiters(LinkLobbyState& state);
bool ProbeLinkLobbyUdpPeerAddress(LinkLobbyState& state, const char* host, u16 port,
    sockaddr_in& out_address);
bool SetLinkLobbyDirectPlayJoinDisabled(LinkLobbyState& state);
bool ClearLinkLobbyDirectPlayJoinDisabled(LinkLobbyState& state);
std::vector<u8> BuildLinkLobbyColoredTextPayload(COLORREF first_color,
    const char* first_text, COLORREF second_color, const char* second_text);
bool SendLinkLobbyChatEditText(LinkLobbyState& state);
bool SendLinkLobbyStatsCommand(LinkLobbyState& state, const char* target_name = nullptr);
bool CreateLinkLobbyWindow(LinkLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM player_slots, LPARAM lobby_payload, LPARAM session_context, int mode,
    LPARAM return_context = 0, int game_type = 0, int screen_size = 0);
LRESULT HandleLinkLobbyWindowMessage(LinkLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT HandleLinkLobbyControlMessage(LinkLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);

#endif

}
