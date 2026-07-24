#include "ranker_link_lobby.h"

#ifdef _WIN32

#include "ranker_cursor.h"
#include "ranker_directplay.h"
#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_network.h"
#include "ranker_online_dialogs.h"
#include "ranker_p2p_lobby.h"
#include "ranker_system_ui.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <iterator>
#include <utility>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = 0x90000000;
constexpr DWORD kListBoxStyle =
    WS_CHILD | WS_VISIBLE | WS_DISABLED | LBS_NOTIFY | LBS_OWNERDRAWFIXED;
constexpr DWORD kChatEditStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NOHIDESEL;
constexpr DWORD kVisibleComboStyle =
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
    CBS_HASSTRINGS;
constexpr DWORD kHiddenDisabledComboStyle =
    WS_CHILD | WS_DISABLED | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
    CBS_HASSTRINGS;
constexpr DWORD kPlayerRoleComboStyle =
    WS_CHILD | WS_VISIBLE | WS_DISABLED | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
    CBS_HASSTRINGS;
constexpr DWORD kTribeComboStyle =
    WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
constexpr DWORD kHiddenOwnerDrawButtonStyle = WS_CHILD | BS_OWNERDRAW;
constexpr COLORREF kLinkSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kLinkErrorBlue = RGB(10, 10, 250);
constexpr COLORREF kLinkStartFailureRed = RGB(255, 20, 20);
constexpr COLORREF kLinkMapFailureRed = RGB(250, 10, 10);
constexpr COLORREF kLinkHostCancelRed = RGB(250, 20, 20);
constexpr COLORREF kLinkDisconnectYellow = RGB(250, 250, 0);
constexpr COLORREF kLinkMapWaiterYellow = RGB(250, 250, 10);
constexpr COLORREF kLinkBlack = RGB(0, 0, 0);
constexpr UINT_PTR kLinkLobbyComboRefreshTimerId = 3;
constexpr UINT kLinkLobbyTabTextFlags =
    DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_MODIFYSTRING;

HFONT link_lobby_ui_font() {
    // The original lobby uses DAT_0162ec38, the second font created by
    // InitializeUiFontHandles (LOGFONT height -12), for its Win32 controls.
    HFONT font = GetUiFontHandle(1);
    return font != nullptr ? font :
        reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

constexpr long kLinkLobbyJoinSocketEvents = FD_READ | FD_WRITE | FD_CLOSE;
constexpr std::size_t kLinkLobbySessionSeedMapScrollOffset = 0x36;
constexpr std::size_t kLinkLobbySessionSeedGroupCountOffset = 0x3a;
constexpr std::size_t kLinkLobbySessionSeedGroupOffsetsOffset = 0x3e;
constexpr std::size_t kLinkLobbySessionSeedGroupNameOffset = 0x4e;
constexpr std::size_t kLinkLobbySessionSeedGroupNameBytes = 0x20;
constexpr std::size_t kLinkLobbySessionSeedGroupColorOffset = 0xce;
constexpr std::size_t kLinkLobbySessionSeedPasswordOffset = 0x20;
constexpr std::size_t kLinkLobbySessionSeedPasswordBytes = 10;
constexpr std::size_t kLinkLobbySessionSeedLocalPlayerOffset = 0xde;
constexpr std::size_t kLinkLobbySessionSeedMaxPlayersOffset = 0xe2;
constexpr std::size_t kLinkLobbySessionSeedRoleValuesOffset = 0xe6;
constexpr std::size_t kLinkLobbySessionSeedRoleMasksOffset = 0x106;
constexpr std::size_t kLinkLobbySessionSeedTeamValuesOffset = 0x126;
constexpr std::size_t kLinkLobbySessionSeedTribeValuesOffset = 0x146;
constexpr std::size_t kLinkLobbySessionSeedTribeMasksOffset = 0x166;
constexpr std::size_t kLinkLobbyMapDescriptorTitleOffset = 0x08;
constexpr std::size_t kLinkLobbyMapDescriptorTitleBytes = 0x20;
constexpr std::size_t kLinkLobbyMapDescriptorPlayerCountOffset = 0x168;
constexpr std::size_t kLinkLobbyMapDescriptorMapWidthOffset = 0x174;
constexpr std::size_t kLinkLobbyMapDescriptorMapHeightOffset = 0x178;
constexpr std::size_t kLinkLobbyMapDescriptorTerrainNameOffset = 0x17c;
constexpr std::size_t kLinkLobbyMapDescriptorTerrainNameBytes = 0x20;

void append_link_lobby_log(const char* format, ...) {
    if (format == nullptr) {
        return;
    }

    FILE* file = std::fopen("Jw2.log", "a");
    if (file == nullptr) {
        return;
    }

    std::fputs("[rebuild] ", file);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}
constexpr std::size_t kLinkLobbyMapDescriptorFileNameOffset = 0x19c;
constexpr std::size_t kLinkLobbyMapDescriptorFileNameBytes = 0x100;
constexpr std::size_t kLinkLobbyMapDescriptorFileSizeOffset = 0x29c;
constexpr std::size_t kLinkLobbyMapDescriptorFileTimeOffset = 0x2a0;
constexpr std::size_t kLinkLobbyMapDescriptorGameTypeOffset = 0x2a8;
constexpr std::size_t kLinkLobbyStartParameterPacketBytes = 0x1f9c;
constexpr std::size_t kLinkLobbyStartParameterStartResourceOffset = 0x0c;
constexpr std::size_t kLinkLobbyStartParameterHostResourceOffset = 0x10;
constexpr std::size_t kLinkLobbyStartParameterScreenSizeOffset = 0x18;
constexpr std::size_t kLinkLobbyStartParameterScreenSizeFixedOffset = 0x1c;
constexpr std::size_t kLinkLobbyStartParameterMapSelectionOffset = 0x20;
constexpr std::size_t kLinkLobbyStartParameterGameTypeOffset = 0x24;
constexpr std::size_t kLinkLobbyStartParameterRandomSlotsOffset = 0x28;
constexpr std::size_t kLinkLobbyStartParameterStartStatesOffset = 0x48;
constexpr std::size_t kLinkLobbyStartParameterTribeChoicesOffset = 0x50;
constexpr std::size_t kLinkLobbyStartParameterPrimaryHostsOffset = 0x58;
constexpr std::size_t kLinkLobbyStartParameterPrimaryPortsOffset = 0xd8;
constexpr std::size_t kLinkLobbyStartParameterSecondaryHostsOffset = 0xf8;
constexpr std::size_t kLinkLobbyStartParameterSecondaryPortsOffset = 0x178;
constexpr std::size_t kLinkLobbyStartParameterPlayerPayloadsOffset = 0x298;
constexpr std::size_t kLinkLobbyPlayerRecordNameOffset = 0x60;
constexpr std::size_t kLinkLobbyPlayerRecordNameBytes = 0x20;
constexpr std::size_t kLinkLobbyPlayerRecordSocketOffset = 0x18a;
constexpr COLORREF kLinkChatPromptColor = RGB(0, 200, 200);
constexpr COLORREF kLinkChatTextColor = RGB(200, 200, 200);
constexpr COLORREF kLinkLocalPromptColor = RGB(20, 200, 20);
constexpr u32 kLinkLobbyHandshakeMagic = 0x5241574a;
constexpr u32 kLinkLobbyTransportPacketType = 2;
constexpr u32 kLinkLobbyUdpProbeOpcode = 0x23;
constexpr u32 kLinkLobbyStartSyncRetryOpcode = 0x25;
constexpr u32 kLinkLobbyUdpStartAckOpcode = 0x26;
constexpr u32 kLinkLobbyStartSyncStopOpcode = 0x27;
constexpr u32 kLinkLobbyAsyncCommandOpcode = 0x2a;
constexpr u32 kLinkLobbyRoleOpenOpcode = 0x01;
constexpr u32 kLinkLobbyRoleComputerOpcode = 0x02;
constexpr u32 kLinkLobbyRoleClosedOpcode = 0x03;
constexpr u32 kLinkLobbyStartResourceOpcode = 0x04;
constexpr u32 kLinkLobbyReservedSelectionOpcode0x05 = 0x05;
constexpr u32 kLinkLobbyReservedSelectionOpcode0x06 = 0x06;
constexpr u32 kLinkLobbyScreenSizeOpcode = 0x07;
constexpr u32 kLinkLobbyMapSelectionOpcode = 0x08;
constexpr u32 kLinkLobbyRelayJoinOpcode = 0x09;
constexpr u32 kLinkLobbySessionSeedOpcode = 0x0a;
constexpr u32 kLinkLobbyStartResultOpcode = 0x0b;
constexpr u32 kLinkLobbyJoinRequestOpcode = 0x0c;
constexpr u32 kLinkLobbyBroadcastRolesOpcode = 0x0d;
constexpr u32 kLinkLobbyPlayerDisconnectOpcode = 0x0e;
constexpr u32 kLinkLobbyPlayerRemovalOpcode = 0x0f;
constexpr u32 kLinkLobbyHostClosedOpcode = 0x10;
constexpr u32 kLinkLobbyAutoMoveOpenSlotOpcode = 0x11;
constexpr u32 kLinkLobbySlotSwapOpcode = 0x12;
constexpr u32 kLinkLobbyTribeSelectionOpcode = 0x13;
constexpr u32 kLinkLobbyReservedOneValueOpcode0x14 = 0x14;
constexpr u32 kLinkLobbyMapDescriptorOpcode = 0x15;
constexpr u32 kLinkLobbyMapChunkOpcode = 0x16;
constexpr u32 kLinkLobbyMapProgressOpcode = 0x17;
constexpr u32 kLinkLobbyMapRequestOpcode = 0x18;
constexpr u32 kLinkLobbyReservedPairOpcode0x1a = 0x1a;
constexpr u32 kLinkLobbyReservedPairOpcode0x1b = 0x1b;
constexpr u32 kLinkLobbyReservedOneValueOpcode0x1c = 0x1c;
constexpr u32 kLinkLobbyStartParametersOpcode = 0x1d;
constexpr u32 kLinkLobbyStartTimeoutOpcode = 0x1e;
constexpr u32 kLinkLobbyPlayerPresenceOpcode = 0x20;
constexpr u32 kLinkLobbyPlayerRecordOpcode = 0x21;
constexpr u32 kLinkLobbyPeerRouteSyncOpcode = 0x22;
constexpr u32 kLinkLobbyPeerRouteOpcode = 0x24;
constexpr u32 kLinkLobbyUdpProbeRequestOpcode = 0x25;
constexpr u32 kLinkLobbyUdpProbeReplyOpcode = 0x26;
constexpr u32 kLinkLobbySecondaryStartAckOpcode = 0x27;
constexpr u32 kLinkLobbyStopPeerRouteTimerOpcode = 0x28;
constexpr u32 kLinkLobbyHostResourceOpcode = 0x2b;
constexpr u32 kLinkLobbyRelayJoinPacketBytes = 0x46;
constexpr u32 kLinkLobbyUdpProbePacketBytes = 0x1c;
constexpr u32 kLinkLobbyStartSyncPacketBytes = 0x14;
constexpr u32 kLinkLobbyPlayerRoleEnabledBitmapRecord = 0x83;
constexpr u32 kLinkLobbyPlayerRoleDisabledBitmapRecord = 0x84;
constexpr u32 kLinkLobbyTribeEnabledBitmapRecord = 0x85;
constexpr u32 kLinkLobbyTribeDisabledBitmapRecord = 0x86;
constexpr u32 kLinkLobbyGameListScrollStartBitmapRecord = 0x8d;
constexpr u32 kLinkLobbyGameListScrollEndBitmapRecord = 0x8e;
constexpr u32 kLinkLobbyGameListScrollThumbBitmapRecord = 0x8f;
constexpr u32 kLinkLobbyGameListScrollTrackBitmapRecord = 0x90;
constexpr u32 kLinkLobbyMapScrollStartBitmapRecord = 0x91;
constexpr u32 kLinkLobbyMapScrollEndBitmapRecord = 0x92;
constexpr u32 kLinkLobbyMapScrollThumbBitmapRecord = 0x93;
constexpr u32 kLinkLobbyMapScrollTrackBitmapRecord = 0x94;
constexpr std::size_t kLinkLobbyAsyncCommandHeaderBytes = 0x0d;
constexpr COLORREF kLinkTabColors[kLinkLobbyTabButtonCount] = {
    RGB(250, 250, 250),
    RGB(10, 250, 250),
    RGB(250, 250, 10),
    RGB(250, 120, 120),
};
constexpr u32 kLinkLobbyLatencyBitmapRecords[kLinkLobbyLatencyBitmapCount] = {
    0x9c,
    0x9d,
    0x9e,
    0x9f,
    0xa0,
    0xa1,
};

LinkLobbyState g_link_lobby_state;
bool g_host_resource_combo_shutdown_registered = false;
bool g_start_resource_combo_shutdown_registered = false;
bool g_screen_size_combo_shutdown_registered = false;
bool g_primary_scroll_shutdown_registered = false;
bool g_secondary_scroll_shutdown_registered = false;
bool g_game_info_button_shutdown_registered = false;
bool g_start_button_shutdown_registered = false;
bool g_cancel_button_shutdown_registered = false;
bool g_avatar_info_button_shutdown_registered = false;
bool g_background_shutdown_registered = false;
bool g_panel_shutdown_registered = false;
bool g_avatar_strip_shutdown_registered = false;
bool g_avatar_button_array_shutdown_registered = false;
bool g_avatar_selected_shutdown_registered = false;
bool g_avatar_available_shutdown_registered = false;
bool g_download_shutdown_registered = false;
std::array<bool, kLinkLobbyTabButtonCount> g_tab_button_shutdown_registered{};
std::array<bool, kLinkLobbyAvatarCount> g_latency_button_shutdown_registered{};
std::array<bool, kLinkLobbyLatencyBitmapCount> g_latency_bitmap_shutdown_registered{};
std::array<bool, kLinkLobbyAvatarCount> g_map_download_button_shutdown_registered{};
std::array<bool, kLinkLobbyAvatarCount> g_player_role_combo_shutdown_registered{};
std::array<bool, kLinkLobbyAvatarCount> g_tribe_combo_shutdown_registered{};

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

const char* kFallbackTabLabels[kLinkLobbyTabButtonCount] = {
    "avatar_tab",
    "guild_tab",
    "friend_tab",
    "block_tab",
};

const char* kFallbackPlayerRoleLabels[] = {
    "Player",
    "Open",
    "Closed",
    "Computer",
};

const char* kFallbackTribeLabels[] = {
    "Primitive",
    "Elf",
    "Tyrano",
    "Demon",
    "Random",
};

const char* startup_message_row(std::size_t index, const char* fallback);

LRESULT CALLBACK link_lobby_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleLinkLobbyWindowMessage(g_link_lobby_state, hwnd, message, wparam,
        lparam);
}

LRESULT CALLBACK link_lobby_control_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleLinkLobbyControlMessage(g_link_lobby_state, hwnd, message, wparam,
        lparam);
}

void register_link_lobby_shutdown(bool& flag, void (*callback)()) {
    if (!flag) {
        std::atexit(callback);
        flag = true;
    }
}

void shutdown_global_game_info_button() {
    ShutdownLinkLobbyGameInfoButton(g_link_lobby_state);
}

void shutdown_global_start_button() {
    ShutdownLinkLobbyStartButton(g_link_lobby_state);
}

void shutdown_global_cancel_button() {
    ShutdownLinkLobbyCancelButton(g_link_lobby_state);
}

void shutdown_global_avatar_info_button() {
    ShutdownLinkLobbyAvatarInfoButton(g_link_lobby_state);
}

void shutdown_global_background() {
    ShutdownLinkLobbyBackgroundBitmap(g_link_lobby_state);
}

void shutdown_global_panel() {
    ShutdownLinkLobbyPanelBitmap(g_link_lobby_state);
}

void shutdown_global_avatar_selected() {
    ShutdownLinkLobbyAvatarSelectedBitmap(g_link_lobby_state);
}

void shutdown_global_avatar_available() {
    ShutdownLinkLobbyAvatarAvailableBitmap(g_link_lobby_state);
}

void shutdown_global_download() {
    ShutdownLinkLobbyDownloadBitmap(g_link_lobby_state);
}

void shutdown_global_host_resource_combo() {
    ShutdownLinkLobbyHostResourceComboControl(g_link_lobby_state);
}

void shutdown_global_start_resource_combo() {
    ShutdownLinkLobbyStartResourceComboControl(g_link_lobby_state);
}

void shutdown_global_screen_size_combo() {
    ShutdownLinkLobbyScreenSizeComboControl(g_link_lobby_state);
}

void shutdown_global_primary_scroll() {
    ShutdownLinkLobbyPrimaryScrollControl(g_link_lobby_state);
}

void shutdown_global_secondary_scroll() {
    ShutdownLinkLobbySecondaryScrollControl(g_link_lobby_state);
}

void shutdown_global_avatar_strip() {
    ShutdownLinkLobbyAvatarStrip(g_link_lobby_state);
}

void shutdown_global_avatar_button_array() {
    ShutdownLinkLobbyAvatarButtonArray(g_link_lobby_state);
}

template <std::size_t Count>
void register_link_lobby_indexed_shutdown(std::array<bool, Count>& flags,
    int index, void (*callback)()) {
    if (index >= 0 && static_cast<std::size_t>(index) < flags.size() &&
        !flags[static_cast<std::size_t>(index)]) {
        std::atexit(callback);
        flags[static_cast<std::size_t>(index)] = true;
    }
}

void initialize_link_lobby_tab_button_control(LinkLobbyState& state, int index) {
    if (index < 0 || index >= kLinkLobbyTabButtonCount) {
        return;
    }
    InitializeLegacyImageButtonControl(state.tab_buttons[index]);
    std::snprintf(state.tab_button_labels[index].data(),
        state.tab_button_labels[index].size(), "%s", kFallbackTabLabels[index]);
    state.tab_text_colors[index] = kLinkTabColors[index];
}

void destroy_link_lobby_tab_button_control(LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyTabButtonCount) {
        DestroyLegacyImageButtonControl(state.tab_buttons[index]);
    }
}

void initialize_link_lobby_latency_button_control(
    LinkLobbyState& state, int index) {
    if (index < 0 || index >= kLinkLobbyAvatarCount) {
        return;
    }
    InitializeLegacyImageButtonControl(state.latency_buttons[index]);
    state.player_row_y[index] = 0x20 + index * 0x24;
    state.latency_values[index] = 0;
}

void destroy_link_lobby_latency_button_control(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyAvatarCount) {
        DestroyLegacyImageButtonControl(state.latency_buttons[index]);
    }
}

void initialize_link_lobby_latency_bitmap_resource(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyLatencyBitmapCount) {
        InitializeBitmapMemoryResource(state.latency_bitmaps[index]);
    }
}

void destroy_link_lobby_latency_bitmap_resource(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyLatencyBitmapCount) {
        ReleaseBitmapMemoryResource(state.latency_bitmaps[index]);
    }
}

void initialize_link_lobby_map_download_button_control(
    LinkLobbyState& state, int index) {
    if (index < 0 || index >= kLinkLobbyAvatarCount) {
        return;
    }
    InitializeLegacyImageButtonControl(state.map_download_buttons[index]);
    state.map_download_progress[index] = 100;
}

void destroy_link_lobby_map_download_button_control(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyAvatarCount) {
        DestroyLegacyImageButtonControl(state.map_download_buttons[index]);
    }
}

void initialize_link_lobby_player_role_combo_control(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyAvatarCount) {
        InitializeLegacyImageComboBoxControl(state.player_role_combos[index]);
    }
}

void destroy_link_lobby_player_role_combo_control(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyAvatarCount) {
        DestroyLegacyImageComboBoxControl(state.player_role_combos[index]);
    }
}

void initialize_link_lobby_tribe_combo_control(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyAvatarCount) {
        InitializeLegacyImageComboBoxControl(state.tribe_combos[index]);
    }
}

void destroy_link_lobby_tribe_combo_control(
    LinkLobbyState& state, int index) {
    if (index >= 0 && index < kLinkLobbyAvatarCount) {
        DestroyLegacyImageComboBoxControl(state.tribe_combos[index]);
    }
}

template <int Index>
void shutdown_global_tab_button() {
    destroy_link_lobby_tab_button_control(g_link_lobby_state, Index);
}

template <int Index>
void shutdown_global_latency_button() {
    destroy_link_lobby_latency_button_control(g_link_lobby_state, Index);
}

template <int Index>
void shutdown_global_latency_bitmap() {
    destroy_link_lobby_latency_bitmap_resource(g_link_lobby_state, Index);
}

template <int Index>
void shutdown_global_map_download_button() {
    destroy_link_lobby_map_download_button_control(g_link_lobby_state, Index);
}

template <int Index>
void shutdown_global_player_role_combo() {
    destroy_link_lobby_player_role_combo_control(g_link_lobby_state, Index);
}

template <int Index>
void shutdown_global_tribe_combo() {
    destroy_link_lobby_tribe_combo_control(g_link_lobby_state, Index);
}

std::vector<LinkLobbyLayoutRect> copy_layout_record(
    const FrontendLayoutRectTable& table) {
    std::vector<LinkLobbyLayoutRect> rects;
    if (table.rects == nullptr || table.count == 0) {
        return rects;
    }
    rects.reserve(table.count);
    for (u32 i = 0; i < table.count; ++i) {
        const FrontendLayoutRect& rect = table.rects[i];
        rects.push_back({rect.x, rect.y, rect.width, rect.height});
    }
    return rects;
}

LinkLobbyLayoutRect layout_at(const LinkLobbyState& state, std::size_t index) {
    if (index < state.layout.size()) {
        return state.layout[index];
    }
    return LinkLobbyLayoutRect{};
}

template <std::size_t N>
void copy_c_string(std::array<char, N>& target, const char* text) {
    target.fill(0);
    if (text != nullptr) {
        std::strncpy(target.data(), text, N - 1);
    }
}

std::string bounded_c_string(const u8* text, std::size_t byte_count) {
    if (text == nullptr || byte_count == 0) {
        return {};
    }
    const u8* end = std::find(text, text + byte_count, 0);
    return std::string(reinterpret_cast<const char*>(text),
        reinterpret_cast<const char*>(end));
}

void initialize_host_player_slots(LinkLobbyState& state) {
    state.local_player_index = 0;
    state.player_role_values.fill(1);
    state.player_role_values[0] = 0;

    LinkLobbyPlayerSlot& local = state.players[0];
    local.occupied = true;
    local.selected = true;
    local.ready = true;
    local.human = true;
    local.tribe = 0;
    copy_c_string(local.name, "Player");
    std::strncpy(reinterpret_cast<char*>(local.raw_payload.data() + 0x60),
        local.name.data(), 0x1f);

    std::memcpy(state.player_payloads[0].data(), local.raw_payload.data(),
        state.player_payloads[0].size());
}

void write_le32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset + 4 > buffer.size()) {
        return;
    }
    buffer[offset] = static_cast<u8>(value & 0xff);
    buffer[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
    buffer[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
    buffer[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

template <std::size_t N>
void write_le32(std::array<u8, N>& buffer, std::size_t offset, u32 value) {
    if (offset + 4 > buffer.size()) {
        return;
    }
    buffer[offset] = static_cast<u8>(value & 0xff);
    buffer[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
    buffer[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
    buffer[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

u32 read_le32(const u8* buffer) {
    return static_cast<u32>(buffer[0]) |
        (static_cast<u32>(buffer[1]) << 8) |
        (static_cast<u32>(buffer[2]) << 16) |
        (static_cast<u32>(buffer[3]) << 24);
}

u32 link_lobby_map_descriptor_u32(const LinkLobbyState& state, std::size_t offset);
bool link_lobby_session_seed_present(const LinkLobbyState& state);

u32 link_lobby_seed_max_players(const LinkLobbyState& state) {
    const u32 value =
        read_le32(state.session_seed_payload.data() + kLinkLobbySessionSeedMaxPlayersOffset);
    if (value == 0 || value > kLinkLobbyAvatarCount) {
        const u32 descriptor_players = link_lobby_map_descriptor_u32(
            state, kLinkLobbyMapDescriptorPlayerCountOffset) & 0xff;
        if (descriptor_players != 0 && descriptor_players <= kLinkLobbyAvatarCount) {
            return descriptor_players;
        }
        return kLinkLobbyAvatarCount;
    }
    return value;
}

bool link_lobby_uses_single_group_room_layout(const LinkLobbyState& state) {
    return link_lobby_session_seed_present(state) && state.tab_button_count <= 1;
}

const char* link_lobby_game_type_name(int game_type) {
    static constexpr const char* kFallbacks[] = {
        "Top Vs Bottom",
        "Melee",
        "Rank",
        "Avatar Melee",
        "Avatar Rank",
        "Use Map Setting",
        "Melee Observer",
        "Avatar Observer",
        "Relay",
    };
    const int index = std::clamp(game_type, 0,
        static_cast<int>(std::size(kFallbacks)) - 1);
    return startup_message_row(109 + static_cast<std::size_t>(index),
        kFallbacks[index]);
}

std::string link_lobby_map_info_text(const LinkLobbyState& state) {
    const std::string title = bounded_c_string(
        state.map_descriptor.data() + kLinkLobbyMapDescriptorTitleOffset,
        kLinkLobbyMapDescriptorTitleBytes);
    const std::string terrain = bounded_c_string(
        state.map_descriptor.data() + kLinkLobbyMapDescriptorTerrainNameOffset,
        kLinkLobbyMapDescriptorTerrainNameBytes);
    const u32 width =
        link_lobby_map_descriptor_u32(state, kLinkLobbyMapDescriptorMapWidthOffset);
    const u32 height =
        link_lobby_map_descriptor_u32(state, kLinkLobbyMapDescriptorMapHeightOffset);

    if (title.empty() && terrain.empty() && width == 0 && height == 0) {
        return "Game infos";
    }

    char text[0x180]{};
    std::snprintf(text, sizeof(text),
        "Title: %s\r\nGame type: %s\r\nMap: %ux%u %s",
        title.empty() ? "" : title.c_str(),
        link_lobby_game_type_name(state.game_type),
        width, height, terrain.empty() ? "" : terrain.c_str());
    return text;
}

int link_lobby_seed_map_scroll_value(const LinkLobbyState& state) {
    const u32 value =
        read_le32(state.session_seed_payload.data() + kLinkLobbySessionSeedMapScrollOffset);
    return static_cast<int>(std::min<u32>(value, 0x0f));
}

bool link_lobby_session_seed_present(const LinkLobbyState& state) {
    return std::any_of(state.session_seed_payload.begin(),
        state.session_seed_payload.end(), [](u8 value) { return value != 0; });
}

u32 link_lobby_seed_u32(const LinkLobbyState& state, std::size_t offset) {
    if (offset + sizeof(u32) > state.session_seed_payload.size()) {
        return 0;
    }
    return read_le32(state.session_seed_payload.data() + offset);
}

u32 link_lobby_map_descriptor_u32(const LinkLobbyState& state, std::size_t offset) {
    if (offset + sizeof(u32) > state.map_descriptor.size()) {
        return 0;
    }
    return read_le32(state.map_descriptor.data() + offset);
}

int link_lobby_active_start_slot_count(const LinkLobbyState& state) {
    const int max_players = std::clamp(
        static_cast<int>(link_lobby_seed_max_players(state)), 0,
        kLinkLobbyAvatarCount);
    int count = 0;
    for (int slot = 0; slot < max_players; ++slot) {
        const int role = state.player_role_values[slot];
        if (role == 0 || role == 3) {
            ++count;
        }
    }
    return count;
}

void apply_link_lobby_map_descriptor_fields(LinkLobbyState& state,
    bool update_game_type) {
    if (kLinkLobbyMapDescriptorFileNameOffset < state.map_descriptor.size()) {
        const std::size_t available = std::min<std::size_t>(
            kLinkLobbyMapDescriptorFileNameBytes,
            state.map_descriptor.size() - kLinkLobbyMapDescriptorFileNameOffset);
        state.map_file_name = bounded_c_string(
            state.map_descriptor.data() + kLinkLobbyMapDescriptorFileNameOffset,
            available);
    }

    state.expected_map_file_size =
        link_lobby_map_descriptor_u32(state, kLinkLobbyMapDescriptorFileSizeOffset);
    if (kLinkLobbyMapDescriptorFileTimeOffset + sizeof(FILETIME) <=
        state.map_descriptor.size()) {
        std::memcpy(&state.expected_map_file_time,
            state.map_descriptor.data() + kLinkLobbyMapDescriptorFileTimeOffset,
            sizeof(FILETIME));
        state.expected_map_file_time_valid =
            state.expected_map_file_time.dwLowDateTime != 0 ||
            state.expected_map_file_time.dwHighDateTime != 0;
    } else {
        state.expected_map_file_time = FILETIME{};
        state.expected_map_file_time_valid = false;
    }

    if (update_game_type) {
        state.game_type = std::clamp(static_cast<int>(link_lobby_map_descriptor_u32(
            state, kLinkLobbyMapDescriptorGameTypeOffset)), 0, 8);
    }
}

void apply_link_lobby_session_seed_fields(LinkLobbyState& state) {
    if (!link_lobby_session_seed_present(state)) {
        return;
    }

    state.password.fill('\0');
    if (kLinkLobbySessionSeedPasswordOffset < state.session_seed_payload.size()) {
        const std::size_t available = std::min<std::size_t>(
            kLinkLobbySessionSeedPasswordBytes,
            state.session_seed_payload.size() - kLinkLobbySessionSeedPasswordOffset);
        std::memcpy(state.password.data(),
            state.session_seed_payload.data() + kLinkLobbySessionSeedPasswordOffset,
            available);
        state.password.back() = '\0';
    }

    state.local_player_index = std::clamp(
        static_cast<int>(link_lobby_seed_u32(state, kLinkLobbySessionSeedLocalPlayerOffset)),
        0, kLinkLobbyAvatarCount - 1);

    state.tab_button_count = std::clamp(static_cast<int>(link_lobby_seed_u32(
        state, kLinkLobbySessionSeedGroupCountOffset)), 0,
        kLinkLobbyTabButtonCount);
    for (int group = 0; group < kLinkLobbyTabButtonCount; ++group) {
        const std::size_t stride = static_cast<std::size_t>(group) * sizeof(u32);
        state.tab_button_positions[group] = static_cast<int>(link_lobby_seed_u32(
            state, kLinkLobbySessionSeedGroupOffsetsOffset + stride));
        const std::size_t name_offset = kLinkLobbySessionSeedGroupNameOffset +
            static_cast<std::size_t>(group) * kLinkLobbySessionSeedGroupNameBytes;
        if (name_offset < state.session_seed_payload.size()) {
            const std::size_t available = std::min<std::size_t>(
                kLinkLobbySessionSeedGroupNameBytes,
                state.session_seed_payload.size() - name_offset);
            const std::string label = bounded_c_string(
                state.session_seed_payload.data() + name_offset, available);
            if (!label.empty()) {
                copy_c_string(state.tab_button_labels[group], label.c_str());
            }
        }
        state.tab_text_colors[group] = static_cast<COLORREF>(link_lobby_seed_u32(
            state, kLinkLobbySessionSeedGroupColorOffset + stride));
    }

    for (int slot = 0; slot < kLinkLobbyAvatarCount; ++slot) {
        const std::size_t stride = static_cast<std::size_t>(slot) * sizeof(u32);
        state.player_role_values[slot] = std::clamp(
            static_cast<int>(link_lobby_seed_u32(
                state, kLinkLobbySessionSeedRoleValuesOffset + stride)),
            0, 3);
        state.player_team_values[slot] = static_cast<int>(link_lobby_seed_u32(
            state, kLinkLobbySessionSeedTeamValuesOffset + stride));
        state.player_role_option_masks[slot] =
            link_lobby_seed_u32(state, kLinkLobbySessionSeedRoleMasksOffset + stride);
        if (state.player_role_option_masks[slot] == 0) {
            state.player_role_option_masks[slot] = 0x0f;
        }
        state.tribe_choices[slot] = static_cast<u8>(std::min<u32>(
            link_lobby_seed_u32(state, kLinkLobbySessionSeedTribeValuesOffset + stride), 4));
        state.tribe_option_masks[slot] =
            link_lobby_seed_u32(state, kLinkLobbySessionSeedTribeMasksOffset + stride);
        if (state.tribe_option_masks[slot] == 0) {
            state.tribe_option_masks[slot] = 0x1f;
        }
    }

    state.player_role_values[state.local_player_index] = 0;
}

bool link_lobby_start_team_requirements_met(const LinkLobbyState& state) {
    if (!link_lobby_session_seed_present(state)) {
        return true;
    }

    std::array<int, kLinkLobbyAvatarCount> team_slots{};
    std::array<int, kLinkLobbyAvatarCount> human_team_slots{};
    const int max_players = static_cast<int>(link_lobby_seed_max_players(state));
    for (int slot = 0; slot < max_players && slot < kLinkLobbyAvatarCount; ++slot) {
        const int team = std::clamp(state.player_team_values[slot], 0,
            kLinkLobbyAvatarCount - 1);
        const int role = state.player_role_values[slot];
        if (role == 0 || role == 3) {
            ++team_slots[team];
        }
        if (role == 0) {
            ++human_team_slots[team];
        }
    }

    switch (state.game_type) {
    case 0:
    case 8:
        return team_slots[0] != 0 && team_slots[1] != 0;
    case 1:
    case 2:
    case 3:
    case 4:
        return team_slots[0] >= 2;
    case 6:
    case 7:
        return team_slots[0] >= 2 && human_team_slots[0] != 0;
    default:
        return true;
    }
}

bool link_lobby_directplay_ready() {
    const AsyncComContext* context = async_com_state().active_context;
    return context != nullptr && context->system_message_101_seen;
}

void write_color_bytes(std::vector<u8>& buffer, std::size_t offset,
    COLORREF color) {
    if (offset + 3 > buffer.size()) {
        return;
    }
    buffer[offset] = static_cast<u8>(color & 0xff);
    buffer[offset + 1] = static_cast<u8>((color >> 8) & 0xff);
    buffer[offset + 2] = static_cast<u8>((color >> 16) & 0xff);
}

COLORREF read_color_bytes(const u8* bytes) {
    return static_cast<COLORREF>(bytes[0]) |
        (static_cast<COLORREF>(bytes[1]) << 8) |
        (static_cast<COLORREF>(bytes[2]) << 16);
}

bool player_index_valid(int player_index) {
    return player_index >= 0 && player_index < kLinkLobbyAvatarCount;
}

void set_local_player_transport_handle(LinkLobbyState& state, SOCKET socket) {
    if (!player_index_valid(state.local_player_index)) {
        return;
    }

    const int slot = state.local_player_index;
    const u32 handle = socket == INVALID_SOCKET || socket == 0 ? 0u :
        static_cast<u32>(socket);
    write_le32(state.players[slot].raw_payload,
        kLinkLobbyPlayerRecordSocketOffset, handle);
    state.player_payloads[slot] = state.players[slot].raw_payload;
}

bool udp_endpoint_ready(const sockaddr_in& address) {
    return address.sin_family == AF_INET && address.sin_addr.s_addr != 0 &&
        address.sin_port != 0;
}

void copy_udp_endpoint_to_route(std::array<char, 0x10>& host, u16& port,
    const sockaddr_in& address) {
    host.fill(0);
    port = 0;
    if (!udp_endpoint_ready(address)) {
        return;
    }

    const char* text = inet_ntoa(address.sin_addr);
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    std::strncpy(host.data(), text, host.size() - 1);
    port = ntohs(address.sin_port);
}

bool route_endpoint_ready(const std::array<char, 0x10>& host, u16 port) {
    if (host[0] == '\0' || port == 0) {
        return false;
    }
    const sockaddr_in address = BuildLegacyUdpSockaddr(host.data(), port);
    return udp_endpoint_ready(address);
}

bool player_has_advertised_udp_route(const LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return false;
    }
    return route_endpoint_ready(state.primary_peer_hosts[player_index],
               state.primary_peer_ports[player_index]) ||
        route_endpoint_ready(state.secondary_peer_hosts[player_index],
            state.secondary_peer_ports[player_index]);
}

bool link_lobby_player_needs_start_sync(const LinkLobbyState& state,
    int player_index);

bool all_remote_human_routes_advertised(const LinkLobbyState& state) {
    for (int slot = 0; slot < kLinkLobbyAvatarCount; ++slot) {
        if (slot == state.local_player_index ||
            !link_lobby_player_needs_start_sync(state, slot)) {
            continue;
        }
        if (!player_has_advertised_udp_route(state, slot)) {
            return false;
        }
    }
    return true;
}

void register_local_udp_route(LinkLobbyState& state,
    const sockaddr_in* secondary_address = nullptr) {
    if (!player_index_valid(state.local_player_index)) {
        return;
    }

    const int slot = state.local_player_index;
    const sockaddr_in local_address = legacy_network_state().udp_bind_address;
    copy_udp_endpoint_to_route(state.primary_peer_hosts[slot],
        state.primary_peer_ports[slot], local_address);
    if (secondary_address != nullptr && udp_endpoint_ready(*secondary_address)) {
        state.local_udp_reflexive_address = *secondary_address;
        state.local_udp_reflexive_address_valid = true;
    }
    if (state.local_udp_reflexive_address_valid) {
        copy_udp_endpoint_to_route(state.secondary_peer_hosts[slot],
            state.secondary_peer_ports[slot],
            state.local_udp_reflexive_address);
    }
    state.udp_peer_addresses[slot] = local_address;
    SetDirectPlayMode1UdpPeerAddress(slot, local_address);
    append_link_lobby_log(
        "link udp local route slot=%ld primary=%s:%u secondary=%s:%u",
        static_cast<long>(slot), state.primary_peer_hosts[slot].data(),
        static_cast<unsigned>(state.primary_peer_ports[slot]),
        state.secondary_peer_hosts[slot].data(),
        static_cast<unsigned>(state.secondary_peer_ports[slot]));
}

bool link_lobby_player_needs_start_sync(const LinkLobbyState& state,
    int player_index) {
    return player_index_valid(player_index) &&
        state.players[player_index].occupied &&
        (state.player_socket_connected[player_index] ||
            state.players[player_index].human);
}

const char* link_lobby_player_name(const LinkLobbyState& state, int player_index) {
    if (player_index_valid(player_index) &&
        state.players[player_index].name[0] != '\0') {
        return state.players[player_index].name.data();
    }
    return "player";
}

std::vector<int> player_role_option_values(const LinkLobbyState& state,
    int player_index) {
    std::vector<int> values;
    if (!player_index_valid(player_index)) {
        return values;
    }
    const u32 mask = state.player_role_option_masks[player_index] == 0 ?
        0x0fu : state.player_role_option_masks[player_index];
    if ((mask & 1) != 0 && state.player_role_values[player_index] == 0) {
        values.push_back(0);
    }
    if ((mask & 2) != 0) {
        values.push_back(1);
    }
    if ((mask & 8) != 0) {
        values.push_back(3);
    }
    if ((mask & 4) != 0) {
        values.push_back(2);
    }
    return values;
}

int selected_player_role_value(const LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index) ||
        state.player_role_combos[player_index].window == nullptr) {
        return 0;
    }
    const int selected = static_cast<int>(SendMessageA(
        state.player_role_combos[player_index].window, CB_GETCURSEL, 0, 0));
    const std::vector<int> values = player_role_option_values(state, player_index);
    if (selected < 0 || selected >= static_cast<int>(values.size())) {
        return state.player_role_values[player_index];
    }
    return values[static_cast<std::size_t>(selected)];
}

bool confirm_connected_player_role_change(LinkLobbyState& state, int player_index,
    int) {
    if (state.window == nullptr) {
        return true;
    }
    int selected_index = -1;
    if (player_index_valid(player_index) &&
        state.player_role_combos[player_index].window != nullptr) {
        selected_index = static_cast<int>(SendMessageA(
            state.player_role_combos[player_index].window, CB_GETCURSEL, 0, 0));
    }
    const bool original_tail_case = selected_index == 4;
    char text[0x100]{};
    std::snprintf(text, sizeof(text),
        startup_message_row(original_tail_case ? 80 : 79,
            original_tail_case ? "%s will no longer be allowed to join the game." :
                "%s will not be allowed to join the game."),
        link_lobby_player_name(state, player_index));
    return ShowOnlineModalPrompt3(online_modal_prompt_state(), state.window, text,
        RGB(200, 200, 200)) == 1;
}

SOCKET start_sync_target_socket(const LinkLobbyState& state, int player_index) {
    if (player_index_valid(player_index) &&
        state.player_sockets[player_index] != INVALID_SOCKET) {
        return state.player_sockets[player_index];
    }
    return state.shared_peer_socket;
}

const void* local_player_record_payload(const LinkLobbyState& state) {
    if (!player_index_valid(state.local_player_index)) {
        return nullptr;
    }
    return state.players[state.local_player_index].raw_payload.data();
}

void send_local_join_request(LinkLobbyState& state, SOCKET target_socket) {
    const u32 player_index = static_cast<u32>(std::max(0, state.local_player_index));
    SendLinkLobbyJoinRequestPacket(state, player_index,
        local_player_record_payload(state), target_socket);
}

void accept_link_lobby_join_request_slot(LinkLobbyState& state, int slot,
    SOCKET sender_socket, const u8* bytes, std::size_t byte_count) {
    if (!player_index_valid(slot)) {
        return;
    }

    state.player_socket_connected[slot] = true;
    state.player_sockets[slot] = sender_socket;
    state.players[slot].occupied = true;
    state.players[slot].human = true;
    state.players[slot].ready = true;
    if (byte_count >= 0x10 + 0x19e) {
        std::memcpy(state.players[slot].raw_payload.data(), bytes + 0x10,
            std::min<std::size_t>(0x19e, state.players[slot].raw_payload.size()));
        const std::string incoming_name = bounded_c_string(
            state.players[slot].raw_payload.data() +
                kLinkLobbyPlayerRecordNameOffset,
            kLinkLobbyPlayerRecordNameBytes);
        copy_c_string(state.players[slot].name, incoming_name.c_str());
    }
    write_le32(state.players[slot].raw_payload, kLinkLobbyPlayerRecordSocketOffset,
        static_cast<u32>(sender_socket));
    state.player_payloads[slot] = state.players[slot].raw_payload;
    ResetLinkLobbyPlayerRoleToHuman(state, slot);
}

SOCKET link_lobby_player_record_socket(const LinkLobbyState& state, int slot) {
    if (!player_index_valid(slot) ||
        kLinkLobbyPlayerRecordSocketOffset + sizeof(u32) >
            state.players[slot].raw_payload.size()) {
        return INVALID_SOCKET;
    }
    return static_cast<SOCKET>(read_le32(
        state.players[slot].raw_payload.data() + kLinkLobbyPlayerRecordSocketOffset));
}

void stop_start_sync_timer(LinkLobbyState& state) {
    if (state.start_sync_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.start_sync_timer);
        state.start_sync_timer = 0;
    }
}

class LinkLobbySocketCriticalSectionScope {
public:
    explicit LinkLobbySocketCriticalSectionScope(LinkLobbyState& state)
        : state_(state) {
        if (!state_.socket_critical_section_initialized) {
            InitializeLinkLobbySocketCriticalSection(state_);
        }
        if (state_.socket_critical_section_initialized) {
            EnterCriticalSection(&state_.socket_critical_section);
            locked_ = true;
        }
    }

    ~LinkLobbySocketCriticalSectionScope() {
        if (locked_) {
            LeaveCriticalSection(&state_.socket_critical_section);
        }
    }

    LinkLobbySocketCriticalSectionScope(
        const LinkLobbySocketCriticalSectionScope&) = delete;
    LinkLobbySocketCriticalSectionScope& operator=(
        const LinkLobbySocketCriticalSectionScope&) = delete;

private:
    LinkLobbyState& state_;
    bool locked_ = false;
};

bool send_lobby_transport_payload(LinkLobbyState& state, const void* packet,
    i32 byte_count, SOCKET target_socket = INVALID_SOCKET) {
    if (packet == nullptr || byte_count <= 0) {
        return false;
    }

    LinkLobbySocketCriticalSectionScope lock(state);
    bool sent = false;
    if (state.mode >= 0 && state.mode <= 2) {
        if (target_socket != INVALID_SOCKET && target_socket != 0) {
            sent = QueueAndFlushSocketSend(static_cast<u32>(byte_count), packet,
                target_socket);
        } else {
            QueueAndFlushAllActiveSocketSends(static_cast<u32>(byte_count), packet);
            sent = true;
        }
    }
    if (!sent || state.callbacks.queue_packet != nullptr) {
        if (state.callbacks.queue_packet != nullptr) {
            state.callbacks.queue_packet(state, packet, byte_count);
        }
    }
    return sent || state.callbacks.queue_packet != nullptr;
}

bool send_start_sync_packet(LinkLobbyState& state, u32 opcode, int from_player,
    int to_player, SOCKET target_socket) {
    std::array<u8, kLinkLobbyStartSyncPacketBytes> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, opcode);
    write_le32(packet, 8, kLinkLobbyStartSyncPacketBytes);
    write_le32(packet, 0x0c, static_cast<u32>(from_player));
    write_le32(packet, 0x10, static_cast<u32>(to_player));

    if (opcode == kLinkLobbyStartSyncRetryOpcode &&
        state.callbacks.send_start_sync_retry != nullptr) {
        return state.callbacks.send_start_sync_retry(state, from_player, to_player,
            target_socket);
    }
    return send_lobby_transport_payload(state, packet.data(),
        static_cast<i32>(packet.size()), target_socket);
}

bool send_player_role_packet(LinkLobbyState& state, int player_index,
    int role_value) {
    std::array<u8, 0x10> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, static_cast<u32>(role_value));
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, static_cast<u32>(player_index));
    return send_lobby_transport_payload(state, packet.data(),
        static_cast<i32>(packet.size()), state.shared_peer_socket);
}

u32 packet_u32(const void* packet, std::size_t byte_count, std::size_t offset) {
    if (packet == nullptr || offset + sizeof(u32) > byte_count) {
        return 0;
    }
    return read_le32(static_cast<const u8*>(packet) + offset);
}

bool packet_player_index(const void* packet, std::size_t byte_count, int& out_index) {
    if (byte_count < 0x10) {
        return false;
    }
    out_index = static_cast<int>(packet_u32(packet, byte_count, 0x0c));
    return player_index_valid(out_index);
}

bool send_link_lobby_two_value_packet(LinkLobbyState& state, u32 opcode,
    u32 first, u32 second, SOCKET target_socket = INVALID_SOCKET) {
    std::array<u8, 0x14> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, opcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, first);
    write_le32(packet, 0x10, second);
    return send_lobby_transport_payload(state, packet.data(),
        static_cast<i32>(packet.size()), target_socket);
}

bool send_link_lobby_one_value_packet(LinkLobbyState& state, u32 opcode,
    u32 value, SOCKET target_socket = INVALID_SOCKET) {
    std::array<u8, 0x10> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, opcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, value);
    return send_lobby_transport_payload(state, packet.data(),
        static_cast<i32>(packet.size()), target_socket);
}

void set_combo_selection(const LegacyImageComboBoxControl& combo, int selection) {
    if (combo.window != nullptr) {
        SendMessageA(combo.window, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
    }
}

void build_link_lobby_start_parameter_payload(LinkLobbyState& state) {
    std::vector<u8> packet(kLinkLobbyStartParameterPacketBytes, 0);
    if (!state.start_parameter_payload.empty()) {
        const std::size_t copy_count = std::min<std::size_t>(
            state.start_parameter_payload.size(), packet.size());
        std::memcpy(packet.data(), state.start_parameter_payload.data(), copy_count);
    }

    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyStartParametersOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, kLinkLobbyStartParameterStartResourceOffset,
        static_cast<u32>(std::max(0, state.start_resource_index)));
    write_le32(packet, kLinkLobbyStartParameterHostResourceOffset,
        state.game_type == 8 ? static_cast<u32>(std::max(0,
            state.host_resource_index) + 1) : 0);
    write_le32(packet, kLinkLobbyStartParameterScreenSizeOffset,
        static_cast<u32>(std::max(0, state.screen_size_index)));
    write_le32(packet, kLinkLobbyStartParameterScreenSizeFixedOffset, 0);
    write_le32(packet, kLinkLobbyStartParameterMapSelectionOffset,
        static_cast<u32>(std::clamp(state.map_selection_index, 0, 0x0f)));
    write_le32(packet, kLinkLobbyStartParameterGameTypeOffset,
        static_cast<u32>(std::max(0, state.game_type)));

    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        // The original start packet stores the randomized map-slot permutation
        // as eight DWORDs.  The following start-state and tribe tables are byte
        // arrays, but this table spans 0x20 bytes (0x28..0x47).  Treating it as
        // eight adjacent bytes makes an original-host packet such as
        // {0, 1, 2, ...} appear as {0, 0, 0, 0, 1, ...} on the client, causing
        // multiple owners to be placed at the same start point.
        write_le32(packet, kLinkLobbyStartParameterRandomSlotsOffset + i * 4,
            state.randomized_slots[i]);
        packet[kLinkLobbyStartParameterStartStatesOffset + i] =
            state.start_states[i];
        packet[kLinkLobbyStartParameterTribeChoicesOffset + i] =
            state.tribe_choices[i];
        std::memcpy(packet.data() + kLinkLobbyStartParameterPrimaryHostsOffset +
                i * 0x10,
            state.primary_peer_hosts[i].data(), 0x10);
        write_le32(packet, kLinkLobbyStartParameterPrimaryPortsOffset + i * 4,
            state.primary_peer_ports[i]);
        std::memcpy(packet.data() + kLinkLobbyStartParameterSecondaryHostsOffset +
                i * 0x10,
            state.secondary_peer_hosts[i].data(), 0x10);
        write_le32(packet, kLinkLobbyStartParameterSecondaryPortsOffset + i * 4,
            state.secondary_peer_ports[i]);

        const std::size_t payload_offset =
            kLinkLobbyStartParameterPlayerPayloadsOffset +
            static_cast<std::size_t>(i) * kLinkLobbyPlayerPayloadBodyBytes;
        if (payload_offset + kLinkLobbyPlayerPayloadBodyBytes <= packet.size()) {
            std::memcpy(packet.data() + payload_offset,
                state.player_payloads[i].data(), kLinkLobbyPlayerPayloadBodyBytes);
        }
    }

    state.start_parameter_payload = std::move(packet);
}

void apply_link_lobby_start_parameter_payload_fields(LinkLobbyState& state,
    const u8* bytes, std::size_t byte_count) {
    if (bytes == nullptr || byte_count < kLinkLobbyStartParameterPacketBytes) {
        return;
    }

    state.start_resource_index = static_cast<int>(
        read_le32(bytes + kLinkLobbyStartParameterStartResourceOffset));
    state.screen_size_index = static_cast<int>(
        read_le32(bytes + kLinkLobbyStartParameterScreenSizeOffset));
    const u32 host_resource = read_le32(
        bytes + kLinkLobbyStartParameterHostResourceOffset);
    state.host_resource_index = host_resource == 0 ? 0 :
        static_cast<int>(host_resource - 1);
    state.map_selection_index = static_cast<int>(
        read_le32(bytes + kLinkLobbyStartParameterMapSelectionOffset));
    state.game_type = static_cast<int>(
        read_le32(bytes + kLinkLobbyStartParameterGameTypeOffset));
    set_combo_selection(state.start_resource_combo, state.start_resource_index);
    set_combo_selection(state.host_resource_combo, state.host_resource_index);
    set_combo_selection(state.screen_size_combo, state.screen_size_index);

    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        state.randomized_slots[i] = static_cast<u8>(std::min<u32>(
            read_le32(bytes + kLinkLobbyStartParameterRandomSlotsOffset + i * 4),
            kLinkLobbyAvatarCount - 1));
        state.start_states[i] =
            bytes[kLinkLobbyStartParameterStartStatesOffset + i];
        state.tribe_choices[i] =
            bytes[kLinkLobbyStartParameterTribeChoicesOffset + i];
        state.primary_peer_hosts[i].fill(0);
        std::memcpy(state.primary_peer_hosts[i].data(),
            bytes + kLinkLobbyStartParameterPrimaryHostsOffset + i * 0x10,
            state.primary_peer_hosts[i].size());
        state.primary_peer_ports[i] = static_cast<u16>(
            read_le32(bytes + kLinkLobbyStartParameterPrimaryPortsOffset + i * 4));
        state.secondary_peer_hosts[i].fill(0);
        std::memcpy(state.secondary_peer_hosts[i].data(),
            bytes + kLinkLobbyStartParameterSecondaryHostsOffset + i * 0x10,
            state.secondary_peer_hosts[i].size());
        state.secondary_peer_ports[i] = static_cast<u16>(
            read_le32(bytes + kLinkLobbyStartParameterSecondaryPortsOffset + i * 4));

        const std::size_t payload_offset =
            kLinkLobbyStartParameterPlayerPayloadsOffset +
            static_cast<std::size_t>(i) * kLinkLobbyPlayerPayloadBodyBytes;
        if (payload_offset + kLinkLobbyPlayerPayloadBodyBytes <= byte_count) {
            std::memcpy(state.player_payloads[i].data(), bytes + payload_offset,
                kLinkLobbyPlayerPayloadBodyBytes);
            std::memcpy(state.players[i].raw_payload.data(),
                state.player_payloads[i].data(), state.player_payloads[i].size());
        }
    }
}

template <std::size_t N>
void copy_fixed_string(std::array<char, N>& target, const void* source,
    std::size_t byte_count) {
    target.fill(0);
    if (source == nullptr || byte_count == 0) {
        return;
    }
    const std::size_t copy_count = std::min<std::size_t>(N - 1, byte_count);
    std::memcpy(target.data(), source, copy_count);
}

void record_udp_start_ack(LinkLobbyState& state, const u8* packet,
    std::size_t byte_count) {
    if (packet == nullptr || byte_count < kLinkLobbyStartSyncPacketBytes) {
        return;
    }
    const u32 player_index = read_le32(packet + 0x0c);
    if (player_index >= kLinkLobbyAvatarCount) {
        return;
    }
    state.udp_peer_addresses[player_index] = legacy_network_state().udp_last_sender;
    copy_udp_endpoint_to_route(state.secondary_peer_hosts[player_index],
        state.secondary_peer_ports[player_index],
        state.udp_peer_addresses[player_index]);
    SetDirectPlayMode1UdpPeerAddress(player_index, state.udp_peer_addresses[player_index]);
    state.start_acknowledged[player_index] = 1;
    append_link_lobby_log("link udp probe ack slot=%lu endpoint=%s:%u",
        static_cast<unsigned long>(player_index),
        inet_ntoa(state.udp_peer_addresses[player_index].sin_addr),
        static_cast<unsigned>(ntohs(
            state.udp_peer_addresses[player_index].sin_port)));
}

std::string colored_payload_text_segment(const char* text, std::size_t length) {
    if (text == nullptr || length == 0) {
        return {};
    }
    const std::size_t count = text[length - 1] == '\0' ? length - 1 : length;
    return std::string(text, count);
}

void append_message_segment(LinkLobbyMessageLine& line, COLORREF color,
    std::string text) {
    if (text.empty() || line.segment_count >= static_cast<int>(line.segments.size())) {
        return;
    }
    LinkLobbyMessageSegment& segment = line.segments[line.segment_count++];
    segment.color = color;
    segment.text = std::move(text);
    line.plain_text += segment.text;
}

bool parse_colored_text_payload(const void* payload, std::size_t byte_count,
    LinkLobbyMessageLine& line) {
    if (payload == nullptr || byte_count < 8) {
        return false;
    }
    const auto* bytes = static_cast<const u8*>(payload);
    if (bytes[0] != 0 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0) {
        return false;
    }

    const std::size_t first_length = bytes[7];
    const char* first_text = reinterpret_cast<const char*>(bytes + 8);
    const std::size_t second_length_offset = first_length + 0x0b;
    if (second_length_offset >= byte_count) {
        return false;
    }
    const std::size_t second_length = bytes[second_length_offset];
    const std::size_t packet_size = first_length + 0x0c + second_length;
    if (packet_size > byte_count) {
        return false;
    }
    const char* second_text =
        reinterpret_cast<const char*>(bytes + second_length_offset + 1);
    const std::size_t second_color_offset = first_length + 8;

    line = LinkLobbyMessageLine{};
    append_message_segment(line, read_color_bytes(bytes + 4),
        colored_payload_text_segment(first_text, first_length));
    append_message_segment(line, read_color_bytes(bytes + second_color_offset),
        colored_payload_text_segment(second_text, second_length));
    return true;
}

std::vector<u8> build_single_color_text_payload(COLORREF color, const char* text) {
    const char* safe_text = text == nullptr ? "" : text;
    const std::size_t length = std::strlen(safe_text);
    std::vector<u8> packet(length + 0x0c, 0);
    write_le32(packet, 0, 0);
    write_color_bytes(packet, 4, color);
    packet[7] = static_cast<u8>(length);
    if (length != 0) {
        std::memcpy(packet.data() + 8, safe_text, length);
    }
    return packet;
}

bool queue_async_command_payload(LinkLobbyState& state,
    const std::vector<u8>& colored_payload) {
    if (state.async_tcp_socket == nullptr ||
        colored_payload.size() <= sizeof(u32)) {
        return false;
    }

    std::vector<u8> packet(colored_payload.size() +
        kLinkLobbyAsyncCommandHeaderBytes - sizeof(u32), 0);
    write_le32(packet, 0, 0);
    write_le32(packet, 4, kLinkLobbyAsyncCommandOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    std::memcpy(packet.data() + kLinkLobbyAsyncCommandHeaderBytes,
        colored_payload.data() + sizeof(u32), colored_payload.size() - sizeof(u32));
    PrepareAndQueueLegacyAsyncTcpSend(*state.async_tcp_socket, packet.data(),
        static_cast<i32>(packet.size()));
    return true;
}

bool avatar_record_valid(const std::array<u8, kLinkLobbyAvatarPayloadBytes>& record) {
    return read_le32(record.data() + kLinkLobbyAvatarInvalidMarkerOffset) !=
        0xffffffffu;
}

void invalidate_avatar_record(std::array<u8, kLinkLobbyAvatarPayloadBytes>& record) {
    record.fill(0);
    write_le32(record, kLinkLobbyAvatarInvalidMarkerOffset, 0xffffffffu);
}

std::array<u8, kLinkLobbyAvatarPayloadBytes> published_avatar_record(
    const LinkLobbyState& state, int slot) {
    std::array<u8, kLinkLobbyAvatarPayloadBytes> record{};
    if (!player_index_valid(slot) || !state.players[slot].selected ||
        !state.players[slot].occupied || !avatar_record_valid(state.avatar_payloads[slot])) {
        invalidate_avatar_record(record);
        return record;
    }
    record = state.avatar_payloads[slot];
    return record;
}

void store_avatar_record_in_payload(
    std::array<u8, kLinkLobbyPlayerPayloadBytes>& player_payload, int slot,
    const std::array<u8, kLinkLobbyAvatarPayloadBytes>& record) {
    const std::size_t offset =
        static_cast<std::size_t>(slot) * kLinkLobbyAvatarPayloadBytes;
    if (offset + record.size() > player_payload.size()) {
        return;
    }
    std::memcpy(player_payload.data() + offset, record.data(), record.size());
}

std::string current_directory_path() {
    char buffer[MAX_PATH]{};
    DWORD length = GetCurrentDirectoryA(static_cast<DWORD>(sizeof(buffer)), buffer);
    if (length == 0 || length >= sizeof(buffer)) {
        return {};
    }
    return buffer;
}

std::string join_path(std::string base, const char* child) {
    if (base.empty()) {
        return child == nullptr ? "" : child;
    }
    if (base.back() != '\\' && base.back() != '/') {
        base.push_back('\\');
    }
    if (child != nullptr) {
        base += child;
    }
    return base;
}

std::string maps_directory_path() {
    return join_path(current_directory_path(), "Maps");
}

std::string download_directory_path() {
    return join_path(maps_directory_path(), "DownLoad");
}

std::string basename_from_path(const std::string& path) {
    const std::size_t pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

int game_list_visible_rows(const LinkLobbyState& state) {
    if (state.game_list.window == nullptr) {
        return 1;
    }
    RECT client{};
    GetClientRect(state.game_list.window, &client);
    int item_height = static_cast<int>(
        SendMessageA(state.game_list.window, LB_GETITEMHEIGHT, 0, 0));
    if (item_height <= 0 || item_height == LB_ERR) {
        item_height = 1;
    }
    const int height = static_cast<int>(client.bottom - client.top);
    return std::max(1, height / item_height);
}

void sync_game_list_scroll(LinkLobbyState& state, bool scroll_to_bottom) {
    if (state.game_list.window == nullptr ||
        state.game_list_scroll.window == nullptr) {
        return;
    }

    const int count = static_cast<int>(
        SendMessageA(state.game_list.window, LB_GETCOUNT, 0, 0));
    const int rows = game_list_visible_rows(state);
    const int max_top = std::max(0, count - rows);
    int top = scroll_to_bottom ? max_top :
        static_cast<int>(SendMessageA(state.game_list.window, LB_GETTOPINDEX, 0, 0));
    top = std::clamp(top, 0, max_top);

    SetLegacyCustomScrollControlPageStep(state.game_list_scroll, rows);
    SetLegacyCustomScrollControlRange(state.game_list_scroll, 0, max_top, false);
    SetLegacyCustomScrollControlValue(state.game_list_scroll, top, false);
    SetLegacyCustomScrollControlVisible(state.game_list_scroll, max_top > 0);
    SendMessageA(state.game_list.window, LB_SETTOPINDEX,
        static_cast<WPARAM>(top), 0);
}

LinkLobbyMessageLine make_single_segment_message(const char* text, COLORREF color) {
    LinkLobbyMessageLine line{};
    append_message_segment(line, color, text == nullptr ? std::string{} :
        std::string(text));
    return line;
}

void append_game_list_message(LinkLobbyState& state, const LinkLobbyMessageLine& line) {
    if (state.game_list.window == nullptr || line.plain_text.empty()) {
        return;
    }
    const std::size_t line_index = state.message_lines.size();
    state.message_lines.push_back(line);
    const LRESULT index = SendMessageA(state.game_list.window, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(line.plain_text.c_str()));
    if (index != LB_ERR && index != LB_ERRSPACE) {
        SendMessageA(state.game_list.window, LB_SETITEMDATA,
            static_cast<WPARAM>(index), static_cast<LPARAM>(line_index));
    } else {
        state.message_lines.pop_back();
    }
    sync_game_list_scroll(state, true);
}

void append_game_list_message(LinkLobbyState& state, const char* text,
    COLORREF color) {
    append_game_list_message(state, make_single_segment_message(text, color));
}

void show_message_line(LinkLobbyState& state, const LinkLobbyMessageLine& line) {
    state.last_message = line.plain_text;
    append_game_list_message(state, line);
    const COLORREF callback_color = line.segment_count > 0 ?
        line.segments[0].color : kLinkSoftWhite;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(),
            callback_color);
    }
}

void show_message(LinkLobbyState& state, const char* text,
    COLORREF color = kLinkSoftWhite) {
    show_message_line(state, make_single_segment_message(text, color));
}

const char* startup_message_row(std::size_t index, const char* fallback) {
    const auto& rows = startup_text_tables().message_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

void show_startup_message(LinkLobbyState& state, std::size_t index,
    const char* fallback, COLORREF color) {
    show_message(state, startup_message_row(index, fallback), color);
}

const char* link_lobby_tribe_label(std::size_t index) {
    if (index < std::size(kFallbackTribeLabels)) {
        return startup_message_row(146 + index, kFallbackTribeLabels[index]);
    }
    return "";
}

const char* link_lobby_role_label(int role_value) {
    switch (role_value) {
    case 1:
        return startup_message_row(179, kFallbackPlayerRoleLabels[1]);
    case 2:
        return startup_message_row(180, kFallbackPlayerRoleLabels[2]);
    case 3:
        return startup_message_row(181, kFallbackPlayerRoleLabels[3]);
    default:
        return kFallbackPlayerRoleLabels[0];
    }
}

std::string format_link_version_mismatch(u32 remote_version) {
    const u32 local_version = LoadTrcRecord9Value();
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer),
        startup_message_row(49,
            "Connection failed - game version mismatch. (my version = %d-%d-%d, host version = %d-%d-%d)"),
        local_version & 0xffffu, (local_version >> 16) & 0xffu,
        (local_version >> 24) & 0xffu, remote_version & 0xffffu,
        (remote_version >> 16) & 0xffu, (remote_version >> 24) & 0xffu);
    return buffer;
}

u32 active_link_lobby_connection_mode(const LinkLobbyState& state) {
    // The original relay handshake reads the process-wide DAT_014b9e58.
    // A joining client sends this packet before its Link window is created,
    // so LinkLobbyState::mode can still contain its default/stale value here.
    const i32 active_mode = async_com_state().active_network_transport_mode;
    return static_cast<u32>(std::max(active_mode >= 0 ? active_mode : state.mode, 0));
}

std::string format_countdown_message(int countdown_value) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer),
        startup_message_row(22, "Game starts in %d"), countdown_value);
    return buffer;
}

std::string format_start_cancel_message(const char* player_name) {
    char buffer[160]{};
    std::snprintf(buffer, sizeof(buffer),
        startup_message_row(23, "%s stopped the game start."),
        player_name != nullptr && player_name[0] != '\0' ? player_name : "player");
    return buffer;
}

void queue_packet(LinkLobbyState& state, const void* packet, i32 byte_count) {
    if (state.callbacks.queue_packet != nullptr) {
        state.callbacks.queue_packet(state, packet, byte_count);
    }
}

bool post_copied_window_payload(HWND hwnd, UINT message, WPARAM wparam,
    const void* payload, std::size_t byte_count) {
    if (hwnd == nullptr || payload == nullptr || byte_count == 0) {
        return false;
    }
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byte_count);
    void* target = global != nullptr ? GlobalLock(global) : nullptr;
    if (target == nullptr) {
        if (global != nullptr) {
            GlobalFree(global);
        }
        return false;
    }
    std::memcpy(target, payload, byte_count);
    if (PostMessageA(hwnd, message, wparam, reinterpret_cast<LPARAM>(target)) != 0) {
        return true;
    }
    GlobalUnlock(global);
    GlobalFree(global);
    return false;
}

void free_locked_window_payload(LPARAM payload) {
    if (payload == 0) {
        return;
    }
    HGLOBAL global = GlobalHandle(reinterpret_cast<LPCVOID>(payload));
    if (global == nullptr) {
        return;
    }
    GlobalUnlock(global);
    GlobalFree(global);
}

std::size_t locked_window_payload_size(LPARAM payload) {
    if (payload == 0) {
        return 0;
    }
    HGLOBAL global = GlobalHandle(reinterpret_cast<LPCVOID>(payload));
    if (global == nullptr) {
        return 0;
    }
    return static_cast<std::size_t>(GlobalSize(global));
}

void clear_control(LinkLobbyWindowControl& control) {
    control.window = nullptr;
    control.original_window_proc = nullptr;
    control.id = 0;
}

void subclass_control(LinkLobbyWindowControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(link_lobby_control_proc));
}

void subclass_button(LegacyImageButtonControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(link_lobby_control_proc));
}

void subclass_combo(LegacyImageComboBoxControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(link_lobby_control_proc));
}

void subclass_scroll(LegacyCustomScrollControl& control) {
    if (control.window == nullptr) {
        return;
    }
    control.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(control.window, GWLP_WNDPROC));
    SetWindowLongPtrA(control.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(link_lobby_control_proc));
}

bool create_window_control(LinkLobbyWindowControl& control, HWND parent,
    HINSTANCE instance, const char* class_name, const char* text, DWORD style, int id,
    const LinkLobbyLayoutRect& rect) {
    control.id = id;
    control.window = CreateWindowExA(0, class_name, text == nullptr ? "" : text,
        style, rect.x, rect.y, rect.width, rect.height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (control.window == nullptr) {
        clear_control(control);
        return false;
    }
    subclass_control(control);
    return true;
}

bool create_button(LegacyImageButtonControl& control, HWND parent, const char* text,
    int id, const LinkLobbyLayoutRect& rect, u32 normal_record, u32 pressed_record) {
    if (!CreateLegacyImageButtonWindow(control, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), rect.x, rect.y,
            rect.width, rect.height)) {
        return false;
    }
    subclass_button(control);
    LoadLegacyImageButtonBitmaps(control, normal_record, pressed_record);
    return true;
}

bool create_scroll(LegacyCustomScrollControl& control, HWND parent, const char* text,
    int id, bool horizontal, const LinkLobbyLayoutRect& rect) {
    if (!CreateLegacyCustomScrollControlWindow(control, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), horizontal,
            rect.x, rect.y, rect.width, rect.height)) {
        return false;
    }
    subclass_scroll(control);
    return true;
}

bool create_combo(LegacyImageComboBoxControl& control, HWND parent, const char* text,
    int id, DWORD style, const LinkLobbyLayoutRect& rect, u32 bitmap_record) {
    if (!CreateLegacyImageComboBoxWindow(control, parent, text,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), style, rect.x,
            rect.y, rect.width, rect.height)) {
        return false;
    }
    subclass_combo(control);
    LoadLegacyImageComboBoxBitmaps(control, bitmap_record, 0);
    return true;
}

LegacyImageButtonControl* button_for_id(LinkLobbyState& state, int id) {
    if (id == kLinkLobbyStartButtonId) {
        return &state.start_button;
    }
    if (id == kLinkLobbyCancelButtonId) {
        return &state.cancel_button;
    }
    if (id == kLinkLobbyInfoPanelId) {
        return &state.game_info_button;
    }
    if (id == kLinkLobbyAvatarInfoId) {
        return &state.avatar_info_button;
    }
    if (id >= kLinkLobbyTabFirstId &&
        id < kLinkLobbyTabFirstId + kLinkLobbyTabButtonCount) {
        return &state.tab_buttons[id - kLinkLobbyTabFirstId];
    }
    if (id >= kLinkLobbyAvatarFirstId &&
        id < kLinkLobbyAvatarFirstId + kLinkLobbyAvatarCount) {
        return &state.avatar_buttons[id - kLinkLobbyAvatarFirstId];
    }
    if (id >= kLinkLobbyLatencyFirstId &&
        id < kLinkLobbyLatencyFirstId + kLinkLobbyAvatarCount) {
        return &state.latency_buttons[id - kLinkLobbyLatencyFirstId];
    }
    if (id >= kLinkLobbyMapDownloadFirstId &&
        id < kLinkLobbyMapDownloadFirstId + kLinkLobbyAvatarCount) {
        return &state.map_download_buttons[id - kLinkLobbyMapDownloadFirstId];
    }
    return nullptr;
}

bool has_link_lobby_saved_proc_id(int id) {
    switch (id) {
    case kLinkLobbyListBoxId:
    case kLinkLobbyChatEditId:
    case kLinkLobbyStartButtonId:
    case kLinkLobbyCancelButtonId:
    case kLinkLobbyInfoPanelId:
    case kLinkLobbyAvatarInfoId:
    case kLinkLobbyHostResourceComboId:
    case kLinkLobbyStartResourceComboId:
    case kLinkLobbyScreenSizeComboId:
    case kLinkLobbyMapSelectionScrollId:
    case kLinkLobbyGameListScrollId:
        return true;
    default:
        break;
    }
    return (id >= kLinkLobbyPlayerRoleComboFirstId &&
               id < kLinkLobbyPlayerRoleComboFirstId + kLinkLobbyAvatarCount) ||
        (id >= kLinkLobbyTribeComboFirstId &&
            id < kLinkLobbyTribeComboFirstId + kLinkLobbyAvatarCount) ||
        (id >= kLinkLobbyTabFirstId &&
            id < kLinkLobbyTabFirstId + kLinkLobbyTabButtonCount) ||
        (id >= kLinkLobbyAvatarFirstId &&
            id < kLinkLobbyAvatarFirstId + kLinkLobbyAvatarCount) ||
        (id >= kLinkLobbyLatencyFirstId &&
            id < kLinkLobbyLatencyFirstId + kLinkLobbyAvatarCount) ||
        (id >= kLinkLobbyMapDownloadFirstId &&
            id < kLinkLobbyMapDownloadFirstId + kLinkLobbyAvatarCount);
}

WNDPROC original_proc_for_id(LinkLobbyState& state, int id) {
    if (id == kLinkLobbyListBoxId) {
        return state.game_list.original_window_proc;
    }
    if (id == kLinkLobbyChatEditId) {
        return state.chat_edit.original_window_proc;
    }
    if (id == kLinkLobbyStartButtonId) {
        return state.start_button.original_window_proc;
    }
    if (id == kLinkLobbyCancelButtonId) {
        return state.cancel_button.original_window_proc;
    }
    if (id == kLinkLobbyInfoPanelId) {
        return state.game_info_button.original_window_proc;
    }
    if (id == kLinkLobbyAvatarInfoId) {
        return state.avatar_info_button.original_window_proc;
    }
    if (id == kLinkLobbyHostResourceComboId) {
        return state.host_resource_combo.original_window_proc;
    }
    if (id == kLinkLobbyStartResourceComboId) {
        return state.start_resource_combo.original_window_proc;
    }
    if (id == kLinkLobbyScreenSizeComboId) {
        return state.screen_size_combo.original_window_proc;
    }
    if (id == kLinkLobbyMapSelectionScrollId) {
        return state.map_selection_scroll.original_window_proc;
    }
    if (id == kLinkLobbyGameListScrollId) {
        return state.game_list_scroll.original_window_proc;
    }
    if (id >= kLinkLobbyPlayerRoleComboFirstId &&
        id < kLinkLobbyPlayerRoleComboFirstId + kLinkLobbyAvatarCount) {
        return state.player_role_combos[id - kLinkLobbyPlayerRoleComboFirstId]
            .original_window_proc;
    }
    if (id >= kLinkLobbyTribeComboFirstId &&
        id < kLinkLobbyTribeComboFirstId + kLinkLobbyAvatarCount) {
        return state.tribe_combos[id - kLinkLobbyTribeComboFirstId]
            .original_window_proc;
    }
    if (id >= kLinkLobbyTabFirstId &&
        id < kLinkLobbyTabFirstId + kLinkLobbyTabButtonCount) {
        return state.tab_buttons[id - kLinkLobbyTabFirstId].original_window_proc;
    }
    if (id >= kLinkLobbyAvatarFirstId &&
        id < kLinkLobbyAvatarFirstId + kLinkLobbyAvatarCount) {
        return state.avatar_buttons[id - kLinkLobbyAvatarFirstId].original_window_proc;
    }
    if (id >= kLinkLobbyLatencyFirstId &&
        id < kLinkLobbyLatencyFirstId + kLinkLobbyAvatarCount) {
        return state.latency_buttons[id - kLinkLobbyLatencyFirstId].original_window_proc;
    }
    if (id >= kLinkLobbyMapDownloadFirstId &&
        id < kLinkLobbyMapDownloadFirstId + kLinkLobbyAvatarCount) {
        return state.map_download_buttons[id - kLinkLobbyMapDownloadFirstId]
            .original_window_proc;
    }
    return nullptr;
}

void fill_combo(HWND combo, const char* const* values, std::size_t count,
    int selection) {
    if (combo == nullptr) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < count; ++i) {
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(values[i]));
    }
    SendMessageA(combo, CB_SETCURSEL, selection, 0);
}

void add_child_rect_to_parent(HWND parent, HWND child, RECT& bounds, bool& has_bounds) {
    if (parent == nullptr || child == nullptr) {
        return;
    }

    RECT child_rect{};
    if (!GetWindowRect(child, &child_rect)) {
        return;
    }
    MapWindowPoints(HWND_DESKTOP, parent,
        reinterpret_cast<POINT*>(&child_rect), 2);

    if (!has_bounds) {
        bounds = child_rect;
        has_bounds = true;
        return;
    }
    UnionRect(&bounds, &bounds, &child_rect);
}

void schedule_link_lobby_combo_refresh(LinkLobbyState& state) {
    if (state.window != nullptr) {
        const UINT_PTR timer = SetTimer(state.window,
            kLinkLobbyComboRefreshTimerId, 100, nullptr);
        if (timer != 0) {
            state.combo_refresh_timer = timer;
        }
    }
}

void redraw_link_lobby_player_row(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index) || state.window == nullptr) {
        return;
    }

    RECT row_rect{};
    bool has_bounds = false;
    add_child_rect_to_parent(state.window,
        state.player_role_combos[player_index].window, row_rect, has_bounds);
    add_child_rect_to_parent(state.window,
        state.tribe_combos[player_index].window, row_rect, has_bounds);
    add_child_rect_to_parent(state.window,
        state.latency_buttons[player_index].window, row_rect, has_bounds);
    add_child_rect_to_parent(state.window,
        state.map_download_buttons[player_index].window, row_rect, has_bounds);
    if (!has_bounds) {
        return;
    }

    InflateRect(&row_rect, 4, 4);
    RedrawWindow(state.window, &row_rect, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    if (state.player_role_combos[player_index].window != nullptr) {
        RedrawWindow(state.player_role_combos[player_index].window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }
    if (state.tribe_combos[player_index].window != nullptr &&
        IsWindowVisible(state.tribe_combos[player_index].window)) {
        RedrawWindow(state.tribe_combos[player_index].window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }
    schedule_link_lobby_combo_refresh(state);
}

void redraw_link_lobby_image_combos(LinkLobbyState& state) {
    const auto redraw_combo = [](const LegacyImageComboBoxControl& combo) {
        if (combo.window != nullptr && IsWindowVisible(combo.window)) {
            RedrawWindow(combo.window, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    };

    redraw_combo(state.host_resource_combo);
    redraw_combo(state.start_resource_combo);
    redraw_combo(state.screen_size_combo);
    for (int player_index = 0; player_index < kLinkLobbyAvatarCount;
         ++player_index) {
        redraw_combo(state.player_role_combos[player_index]);
        redraw_combo(state.tribe_combos[player_index]);
    }
}

void release_resources(LinkLobbyState& state) {
    DeleteLinkLobbySocketCriticalSection(state);
    ShutdownLinkLobbyBackgroundBitmap(state);
    ShutdownLinkLobbyPanelBitmap(state);
    ShutdownLinkLobbyAvatarSelectedBitmap(state);
    ShutdownLinkLobbyAvatarAvailableBitmap(state);
    ShutdownLinkLobbyDownloadBitmap(state);
    ShutdownLinkLobbyAvatarStrip(state);
    ShutdownLinkLobbyGameInfoButton(state);
    ShutdownLinkLobbyStartButton(state);
    ShutdownLinkLobbyCancelButton(state);
    ShutdownLinkLobbyAvatarInfoButton(state);
    ShutdownLinkLobbyPrimaryScrollControl(state);
    ShutdownLinkLobbySecondaryScrollControl(state);
    ShutdownLinkLobbyHostResourceComboControl(state);
    ShutdownLinkLobbyStartResourceComboControl(state);
    ShutdownLinkLobbyScreenSizeComboControl(state);
    DestroyLinkLobbyPlayerRoleComboBoxes(state);
    DestroyLinkLobbyTribeComboBoxes(state);
    DestroyLinkLobbyTabButtons(state);
    ShutdownLinkLobbyAvatarButtonArray(state);
    DestroyLinkLobbyLatencyButtons(state);
    DestroyLinkLobbyMapDownloadButtons(state);
    ReleaseLinkLobbyLatencyBitmaps(state);
    state.layout.clear();
    state.message_lines.clear();
    state.game_list.window = nullptr;
    state.chat_edit.window = nullptr;
}

void draw_avatar_button(LinkLobbyState& state, const DRAWITEMSTRUCT& draw) {
    const int index = static_cast<int>(draw.CtlID) - kLinkLobbyAvatarFirstId;
    if (index < 0 || index >= kLinkLobbyAvatarCount) {
        return;
    }
    const BitmapMemoryResource& base = state.players[index].selected ?
        state.avatar_selected_background : state.avatar_available_background;
    StretchBitmapMemoryResourceToDc(base, draw.hDC, 0, 0);
    if (state.players[index].occupied) {
        DrawRawIndexedBitmapStripFrame(state.avatar_strip, draw.hDC, 1, 1,
            state.players[index].tribe);
    }
}

void draw_game_list_item(LinkLobbyState& state, const DRAWITEMSTRUCT& draw) {
    if (draw.itemID == static_cast<UINT>(-1) || draw.hDC == nullptr ||
        state.game_list.window == nullptr) {
        return;
    }

    std::array<char, 512> text{};
    if (SendMessageA(state.game_list.window, LB_GETTEXT, draw.itemID,
            reinterpret_cast<LPARAM>(text.data())) == LB_ERR) {
        return;
    }

    RECT rect = draw.rcItem;
    HBRUSH brush = CreateSolidBrush(kLinkBlack);
    if (brush != nullptr) {
        FillRect(draw.hDC, &rect, brush);
        DeleteObject(brush);
    }

    const LRESULT item_data = SendMessageA(state.game_list.window, LB_GETITEMDATA,
        draw.itemID, 0);
    const LinkLobbyMessageLine* line = nullptr;
    if (item_data != LB_ERR && item_data >= 0 &&
        static_cast<std::size_t>(item_data) < state.message_lines.size()) {
        line = &state.message_lines[static_cast<std::size_t>(item_data)];
    }

    SetBkColor(draw.hDC, kLinkBlack);
    SetBkMode(draw.hDC, TRANSPARENT);
    if (line == nullptr || line->segment_count == 0) {
        SetTextColor(draw.hDC, kLinkSoftWhite);
        DrawTextA(draw.hDC, text.data(), -1, &rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    int x = rect.left;
    for (int i = 0; i < line->segment_count; ++i) {
        const LinkLobbyMessageSegment& segment = line->segments[i];
        if (segment.text.empty()) {
            continue;
        }
        RECT segment_rect = rect;
        segment_rect.left = x;
        SetTextColor(draw.hDC, segment.color);
        DrawTextA(draw.hDC, segment.text.c_str(), -1, &segment_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SIZE extent{};
        if (GetTextExtentPoint32A(draw.hDC, segment.text.c_str(),
                static_cast<int>(segment.text.size()), &extent) != 0) {
            x += extent.cx;
        }
    }
}

bool erase_game_list_background_if_current(LinkLobbyState& state, HWND hwnd, HDC dc) {
    if (hwnd == nullptr || dc == nullptr || hwnd != state.game_list.window) {
        return false;
    }
    RECT rect{};
    GetClientRect(hwnd, &rect);
    FillRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    return true;
}

void draw_info_panel(LinkLobbyState& state, const DRAWITEMSTRUCT& draw) {
    RECT rect = draw.rcItem;
    HBRUSH brush = CreateSolidBrush(kLinkBlack);
    if (brush != nullptr) {
        FillRect(draw.hDC, &rect, brush);
        DeleteObject(brush);
    }
    rect.left += 6;
    rect.top += 6;
    rect.right -= 6;
    rect.bottom -= 6;
    SetBkColor(draw.hDC, kLinkBlack);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, kLinkSoftWhite);
    // Link messages belong to the owner-drawn message list.  The original
    // info panel remains the title/game-type/map summary after peers join or
    // chat; using `last_message` here replaced that panel with the most recent
    // notification.
    const std::string text = link_lobby_map_info_text(state);
    DrawTextA(draw.hDC, text.c_str(), -1, &rect, DT_LEFT | DT_WORDBREAK);
}

void destroy_window(LinkLobbyState& state) {
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
}

} // namespace

LinkLobbyState& link_lobby_state() {
    return g_link_lobby_state;
}

void SetLinkLobbyLocalPlayerIdentity(LinkLobbyState& state,
    const char* player_name) {
    if (!player_index_valid(state.local_player_index)) {
        return;
    }

    LinkLobbyPlayerSlot& player = state.players[state.local_player_index];
    player.occupied = true;
    player.selected = true;
    player.ready = true;
    player.human = true;
    copy_c_string(player.name,
        player_name != nullptr && player_name[0] != '\0' ? player_name : "Player");
    std::memset(player.raw_payload.data() + kLinkLobbyPlayerRecordNameOffset, 0,
        kLinkLobbyPlayerRecordNameBytes);
    std::strncpy(reinterpret_cast<char*>(player.raw_payload.data() +
            kLinkLobbyPlayerRecordNameOffset), player.name.data(),
        kLinkLobbyPlayerRecordNameBytes - 1);
    state.player_payloads[state.local_player_index] = player.raw_payload;
    state.player_role_values[state.local_player_index] = 0;
}

void InitializeLinkLobbyHostResourceComboControl(LinkLobbyState& state) {
    InitializeLegacyImageComboBoxControl(state.host_resource_combo);
}

void RegisterLinkLobbyHostResourceComboShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_host_resource_combo_shutdown_registered, shutdown_global_host_resource_combo);
}

void ShutdownLinkLobbyHostResourceComboControl(LinkLobbyState& state) {
    DestroyLegacyImageComboBoxControl(state.host_resource_combo);
}

void InitializeLinkLobbyStartResourceComboSupport(LinkLobbyState& state) {
    InitializeLinkLobbyStartResourceComboControl(state);
    RegisterLinkLobbyStartResourceComboShutdown(state);
}

void InitializeLinkLobbyStartResourceComboControl(LinkLobbyState& state) {
    InitializeLegacyImageComboBoxControl(state.start_resource_combo);
}

void RegisterLinkLobbyStartResourceComboShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_start_resource_combo_shutdown_registered, shutdown_global_start_resource_combo);
}

void ShutdownLinkLobbyStartResourceComboControl(LinkLobbyState& state) {
    DestroyLegacyImageComboBoxControl(state.start_resource_combo);
}

void InitializeLinkLobbyScreenSizeComboSupport(LinkLobbyState& state) {
    InitializeLinkLobbyScreenSizeComboControl(state);
    RegisterLinkLobbyScreenSizeComboShutdown(state);
}

void InitializeLinkLobbyScreenSizeComboControl(LinkLobbyState& state) {
    InitializeLegacyImageComboBoxControl(state.screen_size_combo);
}

void RegisterLinkLobbyScreenSizeComboShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_screen_size_combo_shutdown_registered, shutdown_global_screen_size_combo);
}

void ShutdownLinkLobbyScreenSizeComboControl(LinkLobbyState& state) {
    DestroyLegacyImageComboBoxControl(state.screen_size_combo);
}

void InitializeLinkLobbyPrimaryScrollSupport(LinkLobbyState& state) {
    InitializeLinkLobbyPrimaryScrollControl(state);
    RegisterLinkLobbyPrimaryScrollShutdown(state);
}

void InitializeLinkLobbyPrimaryScrollControl(LinkLobbyState& state) {
    InitializeLegacyCustomScrollControl(state.game_list_scroll);
}

void RegisterLinkLobbyPrimaryScrollShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_primary_scroll_shutdown_registered, shutdown_global_primary_scroll);
}

void ShutdownLinkLobbyPrimaryScrollControl(LinkLobbyState& state) {
    DestroyLegacyCustomScrollControl(state.game_list_scroll);
}

void InitializeLinkLobbySecondaryScrollSupport(LinkLobbyState& state) {
    InitializeLinkLobbySecondaryScrollControl(state);
    RegisterLinkLobbySecondaryScrollShutdown(state);
}

void InitializeLinkLobbySecondaryScrollControl(LinkLobbyState& state) {
    InitializeLegacyCustomScrollControl(state.map_selection_scroll);
}

void RegisterLinkLobbySecondaryScrollShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_secondary_scroll_shutdown_registered, shutdown_global_secondary_scroll);
}

void ShutdownLinkLobbySecondaryScrollControl(LinkLobbyState& state) {
    DestroyLegacyCustomScrollControl(state.map_selection_scroll);
}

void InitializeLinkLobbyGameInfoButtonSupport(LinkLobbyState& state) {
    InitializeLinkLobbyGameInfoButton(state);
    RegisterLinkLobbyGameInfoButtonShutdown(state);
}

void InitializeLinkLobbyGameInfoButton(LinkLobbyState& state) {
    InitializeLegacyImageButtonControl(state.game_info_button);
}

void RegisterLinkLobbyGameInfoButtonShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_game_info_button_shutdown_registered, shutdown_global_game_info_button);
}

void ShutdownLinkLobbyGameInfoButton(LinkLobbyState& state) {
    DestroyLegacyImageButtonControl(state.game_info_button);
}

void InitializeLinkLobbyStartButtonSupport(LinkLobbyState& state) {
    InitializeLinkLobbyStartButton(state);
    RegisterLinkLobbyStartButtonShutdown(state);
}

void InitializeLinkLobbyStartButton(LinkLobbyState& state) {
    InitializeLegacyImageButtonControl(state.start_button);
}

void RegisterLinkLobbyStartButtonShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_start_button_shutdown_registered, shutdown_global_start_button);
}

void ShutdownLinkLobbyStartButton(LinkLobbyState& state) {
    DestroyLegacyImageButtonControl(state.start_button);
}

void InitializeLinkLobbyCancelButtonSupport(LinkLobbyState& state) {
    InitializeLinkLobbyCancelButton(state);
    RegisterLinkLobbyCancelButtonShutdown(state);
}

void InitializeLinkLobbyCancelButton(LinkLobbyState& state) {
    InitializeLegacyImageButtonControl(state.cancel_button);
}

void RegisterLinkLobbyCancelButtonShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_cancel_button_shutdown_registered, shutdown_global_cancel_button);
}

void ShutdownLinkLobbyCancelButton(LinkLobbyState& state) {
    DestroyLegacyImageButtonControl(state.cancel_button);
}

void InitializeLinkLobbyAvatarInfoButtonSupport(LinkLobbyState& state) {
    InitializeLinkLobbyAvatarInfoButton(state);
    RegisterLinkLobbyAvatarInfoButtonShutdown(state);
}

void InitializeLinkLobbyAvatarInfoButton(LinkLobbyState& state) {
    InitializeLegacyImageButtonControl(state.avatar_info_button);
}

void RegisterLinkLobbyAvatarInfoButtonShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_avatar_info_button_shutdown_registered, shutdown_global_avatar_info_button);
}

void ShutdownLinkLobbyAvatarInfoButton(LinkLobbyState& state) {
    DestroyLegacyImageButtonControl(state.avatar_info_button);
}

void InitializeLinkLobbyBackgroundResourceAndShutdown(LinkLobbyState& state) {
    InitializeLinkLobbyBackgroundBitmap(state);
    RegisterLinkLobbyBackgroundShutdown(state);
}

void InitializeLinkLobbyBackgroundBitmap(LinkLobbyState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterLinkLobbyBackgroundShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_background_shutdown_registered, shutdown_global_background);
}

void ShutdownLinkLobbyBackgroundBitmap(LinkLobbyState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeLinkLobbyPanelResourceAndShutdown(LinkLobbyState& state) {
    InitializeLinkLobbyPanelBitmap(state);
    RegisterLinkLobbyPanelShutdown(state);
}

void InitializeLinkLobbyPanelBitmap(LinkLobbyState& state) {
    InitializeBitmapMemoryResource(state.panel_background);
}

void RegisterLinkLobbyPanelShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(g_panel_shutdown_registered, shutdown_global_panel);
}

void ShutdownLinkLobbyPanelBitmap(LinkLobbyState& state) {
    ReleaseBitmapMemoryResource(state.panel_background);
}

void InitializeLinkLobbyAvatarStripResourceAndShutdown(LinkLobbyState& state) {
    InitializeLinkLobbyAvatarStrip(state);
    RegisterLinkLobbyAvatarStripShutdown(state);
}

void InitializeLinkLobbyAvatarStrip(LinkLobbyState& state) {
    InitializeRawIndexedBitmapStrip(state.avatar_strip);
}

void RegisterLinkLobbyAvatarStripShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_avatar_strip_shutdown_registered, shutdown_global_avatar_strip);
}

void ShutdownLinkLobbyAvatarStrip(LinkLobbyState& state) {
    HandleRawIndexedBitmapStripDestructor(state.avatar_strip);
}

void InitializeLinkLobbyAvatarButtonArraySupport(LinkLobbyState& state) {
    for (LegacyImageButtonControl& button : state.avatar_buttons) {
        InitializeLegacyImageButtonControl(button);
    }
    RegisterLinkLobbyAvatarButtonArrayShutdown(state);
}

void RegisterLinkLobbyAvatarButtonArrayShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_avatar_button_array_shutdown_registered, shutdown_global_avatar_button_array);
}

void ShutdownLinkLobbyAvatarButtonArray(LinkLobbyState& state) {
    for (LegacyImageButtonControl& button : state.avatar_buttons) {
        DestroyLegacyImageButtonControl(button);
    }
}

void InitializeLinkLobbyAvatarSelectedResourceAndShutdown(LinkLobbyState& state) {
    InitializeLinkLobbyAvatarSelectedBitmap(state);
    RegisterLinkLobbyAvatarSelectedShutdown(state);
}

void InitializeLinkLobbyAvatarSelectedBitmap(LinkLobbyState& state) {
    InitializeBitmapMemoryResource(state.avatar_selected_background);
}

void RegisterLinkLobbyAvatarSelectedShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_avatar_selected_shutdown_registered, shutdown_global_avatar_selected);
}

void ShutdownLinkLobbyAvatarSelectedBitmap(LinkLobbyState& state) {
    ReleaseBitmapMemoryResource(state.avatar_selected_background);
}

void InitializeLinkLobbyAvatarAvailableResourceAndShutdown(LinkLobbyState& state) {
    InitializeLinkLobbyAvatarAvailableBitmap(state);
    RegisterLinkLobbyAvatarAvailableShutdown(state);
}

void InitializeLinkLobbyAvatarAvailableBitmap(LinkLobbyState& state) {
    InitializeBitmapMemoryResource(state.avatar_available_background);
}

void RegisterLinkLobbyAvatarAvailableShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(
        g_avatar_available_shutdown_registered, shutdown_global_avatar_available);
}

void ShutdownLinkLobbyAvatarAvailableBitmap(LinkLobbyState& state) {
    ReleaseBitmapMemoryResource(state.avatar_available_background);
}

void InitializeLinkLobbyDownloadResourceAndShutdown(LinkLobbyState& state) {
    InitializeLinkLobbyDownloadBitmap(state);
    RegisterLinkLobbyDownloadShutdown(state);
}

void InitializeLinkLobbyDownloadBitmap(LinkLobbyState& state) {
    InitializeBitmapMemoryResource(state.download_background);
}

void RegisterLinkLobbyDownloadShutdown(LinkLobbyState&) {
    register_link_lobby_shutdown(g_download_shutdown_registered, shutdown_global_download);
}

void ShutdownLinkLobbyDownloadBitmap(LinkLobbyState& state) {
    ReleaseBitmapMemoryResource(state.download_background);
}

int CountLinkLobbySelectedAvatarSlots(const LinkLobbyState& state) {
    int count = 0;
    for (const LinkLobbyPlayerSlot& player : state.players) {
        if (player.selected) {
            ++count;
        }
    }
    if (count != 0) {
        return count;
    }
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (state.player_role_values[i] == 0 || state.player_role_values[i] == 3) {
            ++count;
        }
    }
    return count;
}

void InstallLinkLobbyAccelerators(LinkLobbyState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kLinkLobbyAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreLinkLobbyAccelerators(LinkLobbyState& state) {
    if (RankerMainWindowState().active_accelerator_window != state.window) {
        return;
    }
    SetActiveAcceleratorState(nullptr, state.active_accelerators);
    DestroyAcceleratorTable(state.active_accelerators);
    state.active_accelerators = state.saved_accelerators;
    state.active_accelerator_window = state.saved_accelerator_window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void MarkLinkLobbyResourcesReady(LinkLobbyState& state) {
    state.resources_ready = true;
}

void PrepareLinkLobbyStartParameters(LinkLobbyState& state) {
    state.start_resource_index =
        static_cast<int>(SendMessageA(state.start_resource_combo.window, CB_GETCURSEL, 0, 0));
    state.screen_size_index =
        static_cast<int>(SendMessageA(state.screen_size_combo.window, CB_GETCURSEL, 0, 0));
    if (state.host_resource_combo.window != nullptr) {
        state.host_resource_index =
            static_cast<int>(SendMessageA(state.host_resource_combo.window, CB_GETCURSEL, 0, 0));
    }
    if (state.map_selection_scroll.window != nullptr) {
        state.map_selection_index = 0x0f - std::clamp(
            GetLegacyCustomScrollControlValue(state.map_selection_scroll), 0, 0x0f);
    }
    state.active_human_count = 0;
    const int max_players = std::clamp(
        static_cast<int>(link_lobby_seed_max_players(state)), 0,
        kLinkLobbyAvatarCount);
    state.selected_avatar_count = std::clamp(
        CountLinkLobbySelectedAvatarSlots(state), 0, kLinkLobbyAvatarCount);
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        state.randomized_slots[i] = static_cast<u8>(i);
    }
    if (state.selected_avatar_count > 0) {
        for (int i = 0; i < max_players; ++i) {
            if (i < state.selected_avatar_count) {
                const int target = std::rand() % state.selected_avatar_count;
                std::swap(state.randomized_slots[i], state.randomized_slots[target]);
            }
        }
    }
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (i >= max_players) {
            state.start_states[i] = 0x14;
        } else if (state.player_role_values[i] == 0) {
            state.start_states[i] = 0;
            if ((state.game_type == 6 || state.game_type == 7) &&
                state.player_team_values[i] == 1) {
                state.start_states[i] = 2;
            }
        } else if (state.player_role_values[i] == 3) {
            state.start_states[i] = 1;
        } else {
            state.start_states[i] = 0x14;
        }
        if (state.start_states[i] == 0) {
            ++state.active_human_count;
        }
        if (state.start_states[i] < 3 && state.tribe_combos[i].window != nullptr) {
            int tribe = static_cast<int>(SendMessageA(state.tribe_combos[i].window,
                CB_GETCURSEL, 0, 0));
            if (tribe == CB_ERR) {
                tribe = 4;
            }
            if (tribe > 3) {
                tribe = std::rand() % 4;
            }
            state.tribe_choices[i] = static_cast<u8>(std::clamp(tribe, 0, 4));
        }
        UpdateLinkLobbyLatencyButtonVisibility(state, i);
        UpdateLinkLobbyMapDownloadButtonVisibility(state, i);
    }
    if (state.game_type == 8 && max_players > 1) {
        const int split = max_players >> 1;
        auto normalize_half_tribe = [&](int first, int last) {
            int tribe = -1;
            for (int i = first; i < last; ++i) {
                if (state.start_states[i] == 0) {
                    tribe = state.tribe_choices[i];
                    break;
                }
            }
            if (tribe < 0) {
                return;
            }
            for (int i = first; i < last; ++i) {
                if (state.start_states[i] == 0) {
                    state.tribe_choices[i] = static_cast<u8>(tribe);
                }
            }
        };
        normalize_half_tribe(0, split);
        normalize_half_tribe(split, max_players);
    }
    build_link_lobby_start_parameter_payload(state);
}

void BeginLinkLobbyStartCountdown(LinkLobbyState& state) {
    append_link_lobby_log(
        "link countdown begin selected=%lu active_humans=%lu host=%s mode=%ld game_type=%ld active_slots=%ld",
        static_cast<unsigned long>(state.selected_avatar_count),
        static_cast<unsigned long>(state.active_human_count),
        state.host_mode ? "yes" : "no",
        static_cast<long>(state.mode),
        static_cast<long>(state.game_type),
        static_cast<long>(link_lobby_active_start_slot_count(state)));
    state.selected_avatar_count = CountLinkLobbySelectedAvatarSlots(state);
    state.active_human_count = 0;
    for (u8 start_state : state.start_states) {
        if (start_state == 0) {
            ++state.active_human_count;
        }
    }
    if (state.selected_avatar_count == 0) {
        append_link_lobby_log("link countdown blocked no selected avatars");
        show_startup_message(state, 28, "Another player is required.",
            kLinkMapFailureRed);
        return;
    }
    if (!link_lobby_start_team_requirements_met(state) &&
        !(state.host_mode && link_lobby_active_start_slot_count(state) >= 2)) {
        append_link_lobby_log(
            "link countdown blocked team requirements active_slots=%ld",
            static_cast<long>(link_lobby_active_start_slot_count(state)));
        show_startup_message(state, 28,
            "The selected game type requires more players.",
            kLinkMapFailureRed);
        return;
    }
    if (state.mode == 6) {
        state.start_locked = true;
        if (state.callbacks.start_game != nullptr) {
            append_link_lobby_log("link countdown immediate start mode=6");
            state.callbacks.start_game(state);
        }
        destroy_window(state);
        return;
    }
    state.countdown_value = 6;
    state.start_locked = true;
    if (state.window != nullptr) {
        state.countdown_timer = SetTimer(state.window, 1, 1000, nullptr);
    }
    append_link_lobby_log("link countdown timer set value=%ld timer=%lu",
        static_cast<long>(state.countdown_value),
        static_cast<unsigned long>(state.countdown_timer));
    const std::string message = format_countdown_message(state.countdown_value);
    show_message(state, message.c_str());
    // Clicking Start moves focus away from the last edited owner-draw combo.
    // USER32 then repaints only its former focus rectangle, leaving a white
    // block after short values such as "Elf".  The original Link window
    // restores the complete image-backed selection field before countdown
    // messages remain on screen.
    schedule_link_lobby_combo_refresh(state);
}

void ReportLinkLobbyPlayerStartTimeout(LinkLobbyState& state, int player_index) {
    if (state.callbacks.report_timeout != nullptr) {
        state.callbacks.report_timeout(state, player_index);
        return;
    }
    if (state.countdown_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.countdown_timer);
    }
    state.countdown_timer = 0;
    state.countdown_value = -1;
    if (state.directplay_join_disabled) {
        ClearLinkLobbyDirectPlayJoinDisabled(state);
    }
    const char* name = player_index >= 0 && player_index < kLinkLobbyAvatarCount ?
        state.players[player_index].name.data() : "player";
    const std::string message = format_start_cancel_message(name);
    const std::vector<u8> payload =
        build_single_color_text_payload(kLinkMapFailureRed, message.c_str());
    if (!payload.empty()) {
        post_copied_window_payload(state.window, kLinkLobbyDirectPlayStartMessage,
            0, payload.data(), payload.size());
    }
}

void ReturnFromLinkLobby(LinkLobbyState& state) {
    state.returned_to_connect = true;
    state.start_locked = false;
    state.join_accepted = false;
    if (state.countdown_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.countdown_timer);
    }
    state.countdown_timer = 0;
    stop_start_sync_timer(state);
    if (state.callbacks.shutdown_network != nullptr) {
        state.callbacks.shutdown_network(state);
    }
    destroy_window(state);
    if (state.mode == 1 && state.callbacks.open_p2p_lobby != nullptr) {
        state.callbacks.open_p2p_lobby(state);
    } else if (state.mode == 3 && state.callbacks.open_ipx_lobby != nullptr) {
        state.callbacks.open_ipx_lobby(state);
    } else if (state.callbacks.open_connect_frontend != nullptr) {
        state.callbacks.open_connect_frontend(state);
    }
}

#define DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(N) \
void InitializeLinkLobbyPlayerRoleComboControl##N(LinkLobbyState& state) { \
    initialize_link_lobby_player_role_combo_control(state, N); \
} \
void RegisterLinkLobbyPlayerRoleComboDestructor##N(LinkLobbyState&) { \
    register_link_lobby_indexed_shutdown(g_player_role_combo_shutdown_registered, \
        N, shutdown_global_player_role_combo<N>); \
}

#define DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(N) \
void DestroyLinkLobbyPlayerRoleComboControl##N(LinkLobbyState& state) { \
    destroy_link_lobby_player_role_combo_control(state, N); \
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(0)
DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(0)

void InitializeLinkLobbyPlayerRoleComboStatic1(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl1(state);
    RegisterLinkLobbyPlayerRoleComboDestructor1(state);
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(1)
DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(1)

void InitializeLinkLobbyPlayerRoleComboStatic2(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl2(state);
    RegisterLinkLobbyPlayerRoleComboDestructor2(state);
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(2)
DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(2)

void InitializeLinkLobbyPlayerRoleComboStatic3(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl3(state);
    RegisterLinkLobbyPlayerRoleComboDestructor3(state);
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(3)
DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(3)

void InitializeLinkLobbyPlayerRoleComboStatic4(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl4(state);
    RegisterLinkLobbyPlayerRoleComboDestructor4(state);
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(4)
DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(4)

void InitializeLinkLobbyPlayerRoleComboStatic5(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl5(state);
    RegisterLinkLobbyPlayerRoleComboDestructor5(state);
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(5)
DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(5)

void InitializeLinkLobbyPlayerRoleComboStatic6(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl6(state);
    RegisterLinkLobbyPlayerRoleComboDestructor6(state);
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(6)
DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY(6)

void InitializeLinkLobbyPlayerRoleComboStatic7(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl7(state);
    RegisterLinkLobbyPlayerRoleComboDestructor7(state);
}

DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL(7)

#undef DEFINE_LINK_PLAYER_ROLE_COMBO_DESTROY
#undef DEFINE_LINK_PLAYER_ROLE_COMBO_CONTROL

bool CreateLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index,
    int y) {
    if (!player_index_valid(player_index) || state.window == nullptr) {
        return false;
    }

    LegacyImageComboBoxControl& combo = state.player_role_combos[player_index];
    InitializeLegacyImageComboBoxControl(combo);
    int x = 0x20;
    int width = 0x78;
    int height = 0x14 + 0x96;
    if (link_lobby_session_seed_present(state)) {
        const LinkLobbyLayoutRect rect = layout_at(state, 12);
        x = rect.x;
        width = rect.width;
        height = rect.height + 0x96;
    }
    if (!CreateLegacyImageComboBoxWindow(combo, state.window, "Player",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                kLinkLobbyPlayerRoleComboFirstId + player_index)),
            kPlayerRoleComboStyle, x, y, width, height)) {
        return false;
    }
    state.player_row_y[player_index] = y;
    subclass_combo(combo);
    LoadLegacyImageComboBoxBitmaps(combo, kLinkLobbyPlayerRoleEnabledBitmapRecord, 0);
    SetLegacyImageComboBoxColors(combo, RGB(176, 178, 171), 0);
    SendMessageA(combo.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(link_lobby_ui_font()), TRUE);
    return true;
}

void DestroyLinkLobbyPlayerRoleComboBoxes(LinkLobbyState& state) {
    DestroyLinkLobbyPlayerRoleComboControl0(state);
    DestroyLinkLobbyPlayerRoleComboControl1(state);
    DestroyLinkLobbyPlayerRoleComboControl2(state);
    DestroyLinkLobbyPlayerRoleComboControl3(state);
    DestroyLinkLobbyPlayerRoleComboControl4(state);
    DestroyLinkLobbyPlayerRoleComboControl5(state);
    DestroyLinkLobbyPlayerRoleComboControl6(state);
    DestroyLinkLobbyPlayerRoleComboControl7(state);
}

void PopulateLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index,
    int role_value) {
    if (!player_index_valid(player_index)) {
        return;
    }
    HWND combo = state.player_role_combos[player_index].window;
    if (combo == nullptr) {
        return;
    }

    state.player_role_values[player_index] = role_value;
    const u32 mask = state.player_role_option_masks[player_index] == 0 ?
        0x0fu : state.player_role_option_masks[player_index];
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);

    int item_count = 0;
    int selected_item = 0;
    const auto add_option = [&](u32 bit, int value, const char* label) {
        if ((mask & bit) == 0) {
            return;
        }
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (role_value == value) {
            selected_item = item_count;
        }
        ++item_count;
    };

    if ((mask & 1) != 0 && role_value == 0) {
        add_option(1, 0, link_lobby_player_name(state, player_index));
    }
    add_option(2, 1, link_lobby_role_label(1));
    add_option(8, 3, link_lobby_role_label(3));
    add_option(4, 2, link_lobby_role_label(2));
    if (item_count == 0) {
        SendMessageA(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(kFallbackPlayerRoleLabels[0]));
        item_count = 1;
    }
    SendMessageA(combo, CB_SETCURSEL, selected_item, 0);

    if (item_count < 2 || !state.host_mode || player_index == state.local_player_index) {
        DisableLinkLobbyPlayerRoleComboBox(state, player_index);
    } else {
        EnableLinkLobbyPlayerRoleComboBox(state, player_index);
    }
    redraw_link_lobby_player_row(state, player_index);
}

void EnableLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    LegacyImageComboBoxControl& combo = state.player_role_combos[player_index];
    if (combo.window == nullptr) {
        return;
    }
    EnableWindow(combo.window, TRUE);
    LoadLegacyImageComboBoxBitmaps(combo, kLinkLobbyPlayerRoleEnabledBitmapRecord, 0);
}

void DisableLinkLobbyPlayerRoleComboBox(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    LegacyImageComboBoxControl& combo = state.player_role_combos[player_index];
    if (combo.window == nullptr) {
        return;
    }
    EnableWindow(combo.window, FALSE);
    LoadLegacyImageComboBoxBitmaps(combo, kLinkLobbyPlayerRoleDisabledBitmapRecord, 0);
}

bool CreateLinkLobbyPlayerRoleControls(LinkLobbyState& state) {
    if (!link_lobby_session_seed_present(state)) {
        if (!CreateLinkLobbyTabButtons(state)) {
            return false;
        }
        const int first_y = 0x64;
        const int step_y = 0x18;
        for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
            const int y = first_y + i * step_y;
            if (!CreateLinkLobbyPlayerRoleComboBox(state, i, y) ||
                !CreateLinkLobbyTribeComboBox(state, i) ||
                !CreateLinkLobbyMapDownloadButton(state, i) ||
                !CreateLinkLobbyLatencyButton(state, i)) {
                return false;
            }
            PopulateLinkLobbyPlayerRoleComboBox(state, i, state.player_role_values[i]);
            PopulateLinkLobbyTribeComboBox(state, i);
            UpdateLinkLobbyTribeComboBoxState(state, i);
        }
        return true;
    }

    const LinkLobbyLayoutRect tab_rect = layout_at(state, 11);
    const LinkLobbyLayoutRect role_rect = layout_at(state, 12);
    const int row_step = role_rect.height + 4;
    // FUN_004714f0 places the first player row at group_y + tab_height + 3.
    // Deriving it from the two independent TRC y values is one pixel shorter
    // and shifts a host-created tribe combo from y=135 to y=134.
    const int first_row_offset = tab_rect.height + 3;
    const int player_count = std::clamp(
        static_cast<int>(link_lobby_seed_max_players(state)), 0,
        kLinkLobbyAvatarCount);
    if (link_lobby_uses_single_group_room_layout(state)) {
        state.tab_button_count = 1;
        state.tab_button_positions[0] = tab_rect.y;
        if (state.tab_button_labels[0][0] == '\0') {
            copy_c_string(state.tab_button_labels[0], "Player's Game");
        }
        if (!CreateLinkLobbyTabButtons(state)) {
            return false;
        }
        int y = state.tab_button_positions[0] + first_row_offset;
        for (int player = 0; player < player_count; ++player) {
            state.player_team_values[player] = 0;
            if (!CreateLinkLobbyPlayerRoleComboBox(state, player, y) ||
                !CreateLinkLobbyTribeComboBox(state, player) ||
                !CreateLinkLobbyMapDownloadButton(state, player) ||
                !CreateLinkLobbyLatencyButton(state, player)) {
                return false;
            }
            PopulateLinkLobbyPlayerRoleComboBox(state, player,
                state.player_role_values[player]);
            PopulateLinkLobbyTribeComboBox(state, player);
            UpdateLinkLobbyTribeComboBoxState(state, player);
            y += row_step;
        }
        return true;
    }

    int next_group_y = tab_rect.y;
    for (int group = 0; group < state.tab_button_count; ++group) {
        state.tab_button_positions[group] = next_group_y;
        int group_players = 0;
        for (int player = 0; player < player_count; ++player) {
            if (state.player_team_values[player] == group) {
                ++group_players;
            }
        }
        next_group_y += tab_rect.height +
            std::max(1, group_players) * row_step + 8;
    }
    if (!CreateLinkLobbyTabButtons(state)) {
        return false;
    }

    for (int group = 0; group < state.tab_button_count; ++group) {
        int y = state.tab_button_positions[group] + first_row_offset;
        for (int player = 0; player < player_count; ++player) {
            if (state.player_team_values[player] != group) {
                continue;
            }
            if (!CreateLinkLobbyPlayerRoleComboBox(state, player, y) ||
                !CreateLinkLobbyTribeComboBox(state, player) ||
                !CreateLinkLobbyMapDownloadButton(state, player) ||
                !CreateLinkLobbyLatencyButton(state, player)) {
                return false;
            }
            PopulateLinkLobbyPlayerRoleComboBox(state, player,
                state.player_role_values[player]);
            PopulateLinkLobbyTribeComboBox(state, player);
            UpdateLinkLobbyTribeComboBoxState(state, player);
            y += row_step;
        }
    }
    return true;
}

void SwapLinkLobbyPlayerSlots(LinkLobbyState& state, int left_player,
    int right_player) {
    if (!player_index_valid(left_player) || !player_index_valid(right_player) ||
        left_player == right_player) {
        return;
    }

    std::swap(state.players[left_player], state.players[right_player]);
    std::swap(state.player_role_values[left_player],
        state.player_role_values[right_player]);
    std::swap(state.player_team_values[left_player],
        state.player_team_values[right_player]);
    std::swap(state.player_role_option_masks[left_player],
        state.player_role_option_masks[right_player]);
    std::swap(state.tribe_choices[left_player], state.tribe_choices[right_player]);
    std::swap(state.tribe_option_masks[left_player],
        state.tribe_option_masks[right_player]);
    std::swap(state.latency_values[left_player], state.latency_values[right_player]);
    std::swap(state.map_download_progress[left_player],
        state.map_download_progress[right_player]);
    std::swap(state.player_socket_connected[left_player],
        state.player_socket_connected[right_player]);
    std::swap(state.start_acknowledged[left_player],
        state.start_acknowledged[right_player]);
    std::swap(state.secondary_start_acknowledged[left_player],
        state.secondary_start_acknowledged[right_player]);
    std::swap(state.player_sockets[left_player], state.player_sockets[right_player]);
    std::swap(state.udp_peer_addresses[left_player],
        state.udp_peer_addresses[right_player]);
    SetDirectPlayMode1UdpPeerAddress(left_player, state.udp_peer_addresses[left_player]);
    SetDirectPlayMode1UdpPeerAddress(right_player, state.udp_peer_addresses[right_player]);
    SwapLinkLobbyPlayerPayloads(state, left_player, right_player);

    if (state.local_player_index == left_player) {
        state.local_player_index = right_player;
    } else if (state.local_player_index == right_player) {
        state.local_player_index = left_player;
    }

    PopulateLinkLobbyPlayerRoleComboBox(state, left_player,
        state.player_role_values[left_player]);
    PopulateLinkLobbyPlayerRoleComboBox(state, right_player,
        state.player_role_values[right_player]);
    UpdateLinkLobbyTribeComboBoxState(state, left_player);
    UpdateLinkLobbyTribeComboBoxState(state, right_player);
    UpdateLinkLobbyMapDownloadButtonVisibility(state, left_player);
    UpdateLinkLobbyMapDownloadButtonVisibility(state, right_player);
    UpdateLinkLobbyLatencyButtonVisibility(state, left_player);
    UpdateLinkLobbyLatencyButtonVisibility(state, right_player);
}

void ResetLinkLobbyPlayerRoleToHuman(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    state.player_role_values[player_index] = 0;
    state.players[player_index].human = true;
    state.players[player_index].occupied = true;
    PopulateLinkLobbyPlayerRoleComboBox(state, player_index, 0);
    UpdateLinkLobbyTribeComboBoxState(state, player_index);
    UpdateLinkLobbyLatencyButtonVisibility(state, player_index);
}

#define DEFINE_LINK_TRIBE_COMBO_CONTROL(N) \
void InitializeLinkLobbyTribeComboControl##N(LinkLobbyState& state) { \
    initialize_link_lobby_tribe_combo_control(state, N); \
} \
void RegisterLinkLobbyTribeComboDestructor##N(LinkLobbyState&) { \
    register_link_lobby_indexed_shutdown(g_tribe_combo_shutdown_registered, \
        N, shutdown_global_tribe_combo<N>); \
}

#define DEFINE_LINK_TRIBE_COMBO_DESTROY(N) \
void DestroyLinkLobbyTribeComboControl##N(LinkLobbyState& state) { \
    destroy_link_lobby_tribe_combo_control(state, N); \
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(0)
DEFINE_LINK_TRIBE_COMBO_DESTROY(0)

void InitializeLinkLobbyTribeComboStatic1(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl1(state);
    RegisterLinkLobbyTribeComboDestructor1(state);
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(1)
DEFINE_LINK_TRIBE_COMBO_DESTROY(1)

void InitializeLinkLobbyTribeComboStatic2(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl2(state);
    RegisterLinkLobbyTribeComboDestructor2(state);
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(2)
DEFINE_LINK_TRIBE_COMBO_DESTROY(2)

void InitializeLinkLobbyTribeComboStatic3(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl3(state);
    RegisterLinkLobbyTribeComboDestructor3(state);
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(3)
DEFINE_LINK_TRIBE_COMBO_DESTROY(3)

void InitializeLinkLobbyTribeComboStatic4(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl4(state);
    RegisterLinkLobbyTribeComboDestructor4(state);
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(4)
DEFINE_LINK_TRIBE_COMBO_DESTROY(4)

void InitializeLinkLobbyTribeComboStatic5(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl5(state);
    RegisterLinkLobbyTribeComboDestructor5(state);
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(5)
DEFINE_LINK_TRIBE_COMBO_DESTROY(5)

void InitializeLinkLobbyTribeComboStatic6(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl6(state);
    RegisterLinkLobbyTribeComboDestructor6(state);
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(6)
DEFINE_LINK_TRIBE_COMBO_DESTROY(6)

void InitializeLinkLobbyTribeComboStatic7(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl7(state);
    RegisterLinkLobbyTribeComboDestructor7(state);
}

DEFINE_LINK_TRIBE_COMBO_CONTROL(7)

#undef DEFINE_LINK_TRIBE_COMBO_DESTROY
#undef DEFINE_LINK_TRIBE_COMBO_CONTROL

bool CreateLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index) || state.window == nullptr) {
        return false;
    }
    const int y = state.player_row_y[player_index] == 0 ?
        0x64 + player_index * 0x18 : state.player_row_y[player_index];
    int x = 0x234;
    int width = 0x5c;
    int height = 0x96;
    if (link_lobby_session_seed_present(state)) {
        const LinkLobbyLayoutRect rect = layout_at(state, 13);
        x = rect.x;
        width = rect.width;
        height = rect.height + 0x96;
    }
    LegacyImageComboBoxControl& combo = state.tribe_combos[player_index];
    if (!CreateLegacyImageComboBoxWindow(combo, state.window, "Tribe",
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                kLinkLobbyTribeComboFirstId + player_index)),
            kTribeComboStyle, x, y, width, height)) {
        return false;
    }
    subclass_combo(combo);
    LoadLegacyImageComboBoxBitmaps(combo, kLinkLobbyTribeDisabledBitmapRecord, 0);
    SendMessageA(combo.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(link_lobby_ui_font()), TRUE);
    return true;
}

void DestroyLinkLobbyTribeComboBoxes(LinkLobbyState& state) {
    DestroyLinkLobbyTribeComboControl0(state);
    DestroyLinkLobbyTribeComboControl1(state);
    DestroyLinkLobbyTribeComboControl2(state);
    DestroyLinkLobbyTribeComboControl3(state);
    DestroyLinkLobbyTribeComboControl4(state);
    DestroyLinkLobbyTribeComboControl5(state);
    DestroyLinkLobbyTribeComboControl6(state);
    DestroyLinkLobbyTribeComboControl7(state);
}

void PopulateLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    HWND combo = state.tribe_combos[player_index].window;
    if (combo == nullptr) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    const u32 mask = state.tribe_option_masks[player_index] == 0 ?
        0x1fu : state.tribe_option_masks[player_index];
    const bool random_enabled = (mask & 0x10) != 0;
    int item_index = 0;
    int selected_index = 0;
    for (int i = 0; i < 4; ++i) {
        if ((mask & (1u << i)) != 0 || random_enabled) {
            SendMessageA(combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(link_lobby_tribe_label(i)));
            if (state.tribe_choices[player_index] == i) {
                selected_index = item_index;
            }
            ++item_index;
        }
    }
    if (random_enabled) {
        SendMessageA(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(link_lobby_tribe_label(4)));
        if (state.tribe_choices[player_index] >= 4) {
            selected_index = item_index;
        }
    }
    SendMessageA(combo, CB_SETCURSEL, selected_index, 0);
}

void UpdateLinkLobbyTribeComboBoxState(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    const int role = state.player_role_values[player_index];
    if (role == 0 || role == 3) {
        ShowLinkLobbyTribeComboBox(state, player_index);
    } else {
        HideLinkLobbyTribeComboBox(state, player_index);
    }
    const bool random_enabled = (state.tribe_option_masks[player_index] & 0x10) != 0;
    const bool host_random_ai_slot = state.host_mode && role == 3;
    const bool can_edit = random_enabled &&
        (player_index == state.local_player_index || host_random_ai_slot);
    if (can_edit) {
        EnableLinkLobbyTribeComboBox(state, player_index);
    } else {
        DisableLinkLobbyTribeComboBox(state, player_index);
    }
    redraw_link_lobby_player_row(state, player_index);
}

void EnableLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    LegacyImageComboBoxControl& combo = state.tribe_combos[player_index];
    if (combo.window != nullptr) {
        EnableWindow(combo.window, TRUE);
        LoadLegacyImageComboBoxBitmaps(combo, kLinkLobbyTribeEnabledBitmapRecord, 0);
    }
}

void DisableLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    LegacyImageComboBoxControl& combo = state.tribe_combos[player_index];
    if (combo.window != nullptr) {
        EnableWindow(combo.window, FALSE);
        LoadLegacyImageComboBoxBitmaps(combo, kLinkLobbyTribeDisabledBitmapRecord, 0);
    }
}

void ShowLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index) {
    if (player_index_valid(player_index) &&
        state.tribe_combos[player_index].window != nullptr) {
        ShowWindow(state.tribe_combos[player_index].window, SW_SHOW);
    }
}

void HideLinkLobbyTribeComboBox(LinkLobbyState& state, int player_index) {
    if (player_index_valid(player_index) &&
        state.tribe_combos[player_index].window != nullptr) {
        ShowWindow(state.tribe_combos[player_index].window, SW_HIDE);
    }
}

void HandleLinkLobbyTribeComboChange(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index) ||
        state.tribe_combos[player_index].window == nullptr) {
        return;
    }
    const int selection = static_cast<int>(SendMessageA(
        state.tribe_combos[player_index].window, CB_GETCURSEL, 0, 0));
    if (selection == CB_ERR) {
        return;
    }
    state.tribe_choices[player_index] = static_cast<u8>(selection);
    SendLinkLobbyTribeSelectionPacket(state, static_cast<u32>(player_index),
        static_cast<u32>(selection));
}

void HandleLinkLobbyStartResult(LinkLobbyState& state, u32 player_index,
    int result_code) {
    if (result_code != 10 && state.mode >= 0 && state.mode < 3) {
        ShutdownLegacyTcpNetworking();
    }

    switch (result_code) {
    case 0:
        show_startup_message(state, 42, "Connection failed - general error.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 1:
        show_startup_message(state, 43, "Connection failed - map file send error.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 2:
        show_startup_message(state, 44,
            "Connection failed - failed to get connected player info.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 3:
        show_startup_message(state, 45, "Connection failed - game is full.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 4:
        show_startup_message(state, 46,
            "Connection failed - player information send error.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 5:
        show_startup_message(state, 47, "Connection failed - game already started.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 6:
        show_startup_message(state, 48, "Connection failed - different game type.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 7: {
        const std::string text = format_link_version_mismatch(player_index);
        show_message(state, text.c_str(), kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    }
    case 8:
        show_startup_message(state, 50,
            "Connection failed - different connection mode.",
            kLinkStartFailureRed);
        ReturnFromLinkLobby(state);
        return;
    case 9:
        show_startup_message(state, 51, "Connection failed - wrong password.",
            kLinkStartFailureRed);
        return;
    default:
        break;
    }

    if (result_code != 10 || player_index >= kLinkLobbyAvatarCount) {
        return;
    }

    const int previous_local_player = state.local_player_index;
    SOCKET assigned_peer_socket = state.player_sockets[player_index];
    if (assigned_peer_socket == INVALID_SOCKET) {
        assigned_peer_socket = state.shared_peer_socket;
    }
    if (assigned_peer_socket == INVALID_SOCKET &&
        player_index_valid(previous_local_player)) {
        assigned_peer_socket = state.player_sockets[previous_local_player];
    }
    state.local_player_index = static_cast<int>(player_index);
    state.join_accepted = true;
    state.start_locked = true;
    state.start_sync_complete = false;

    if (previous_local_player != state.local_player_index &&
        player_index_valid(previous_local_player)) {
        DisableLinkLobbyPlayerRoleComboBox(state, previous_local_player);
        state.primary_peer_hosts[previous_local_player] = {};
        state.secondary_peer_hosts[previous_local_player] = {};
        state.primary_peer_ports[previous_local_player] = 0;
        state.secondary_peer_ports[previous_local_player] = 0;
        state.udp_peer_addresses[previous_local_player] = {};
        state.player_sockets[previous_local_player] = INVALID_SOCKET;
        state.player_socket_connected[previous_local_player] = false;
        SetDirectPlayMode1UdpPeerAddress(
            static_cast<u32>(previous_local_player), sockaddr_in{});
    }

    if (state.mode >= 0 && state.mode < 3) {
        state.player_sockets[state.local_player_index] = assigned_peer_socket;
        state.shared_peer_socket = assigned_peer_socket;
        set_local_player_transport_handle(state, assigned_peer_socket);
        register_local_udp_route(state);
        if (state.mode == 1 && p2p_lobby_state().player_name[0] != '\0') {
            SetLinkLobbyLocalPlayerIdentity(state,
                p2p_lobby_state().player_name.data());
        }
        const u32 local = static_cast<u32>(state.local_player_index);
        SendLinkLobbyPeerRoutePacket(state, local,
            state.primary_peer_hosts[local].data(),
            state.primary_peer_ports[local],
            state.secondary_peer_hosts[local].data(),
            state.secondary_peer_ports[local]);
        if (state.shared_peer_socket != INVALID_SOCKET) {
            state.player_socket_connected[state.local_player_index] = true;
        }
        append_link_lobby_log(
            "link join accepted slot=%lu advertised=%s:%u",
            static_cast<unsigned long>(local),
            state.primary_peer_hosts[local].data(),
            static_cast<unsigned>(state.primary_peer_ports[local]));
    } else if (state.mode == 3) {
        state.secondary_start_sync_required = true;
    }

    char status_text[0x80]{};
    std::snprintf(status_text, sizeof(status_text),
        startup_message_row(73, "%s joined the game."),
        link_lobby_player_name(state, state.local_player_index));
    std::vector<u8> status_payload =
        build_single_color_text_payload(RGB(250, 250, 0), status_text);
    if (!status_payload.empty()) {
        send_lobby_transport_payload(state, status_payload.data(),
            static_cast<i32>(status_payload.size()), state.shared_peer_socket);
        post_copied_window_payload(state.window, kLinkLobbyDirectPlayStartMessage, 0,
            status_payload.data(), status_payload.size());
    }

    if ((state.mode == 0 || state.mode == 2) && !state.map_file_name.empty()) {
        char map_text[0x100]{};
        std::snprintf(map_text, sizeof(map_text), "%s%s",
            startup_message_row(142, "Map: "),
            basename_from_path(state.map_file_name).c_str());
        std::vector<u8> map_payload =
            build_single_color_text_payload(RGB(250, 250, 0), map_text);
        send_lobby_transport_payload(state, map_payload.data(),
            static_cast<i32>(map_payload.size()), state.shared_peer_socket);
    }

    if (state.window != nullptr) {
        SendMessageA(state.window, kLinkLobbyStartAcceptedMessage, 0, 0);
    }
    if (!PrepareLinkLobbyMapDownload(state)) {
        show_startup_message(state, 26, "Unable to build game map file.",
            kLinkMapFailureRed);
        ReturnFromLinkLobby(state);
    }
}

void HandleLinkLobbyPlayerDisconnected(LinkLobbyState& state, u32 player_index) {
    if (player_index >= kLinkLobbyAvatarCount ||
        !state.player_socket_connected[player_index]) {
        return;
    }

    state.player_socket_connected[player_index] = false;
    state.player_sockets[player_index] = INVALID_SOCKET;
    if (static_cast<int>(player_index) == state.local_player_index) {
        ShutdownLinkLobbyNetworkRoute(state);
        show_startup_message(state, 75, "The host rejected you.",
            kLinkDisconnectYellow);
        ReturnFromLinkLobby(state);
        return;
    }

    char text[0x80]{};
    std::snprintf(text, sizeof(text),
        startup_message_row(74, "%s left the game."),
        link_lobby_player_name(state, static_cast<int>(player_index)));
    std::vector<u8> payload = build_single_color_text_payload(kLinkSoftWhite, text);
    post_copied_window_payload(state.window, kLinkLobbyDirectPlayStartMessage, 0,
        payload.data(), payload.size());

    state.players[player_index] = LinkLobbyPlayerSlot{};
    state.player_role_values[player_index] = 1;
    state.map_download_progress[player_index] = 100;
    state.latency_values[player_index] = 0;
    state.start_acknowledged[player_index] = 0;
    state.secondary_start_acknowledged[player_index] = 0;
    PopulateLinkLobbyPlayerRoleComboBox(state, static_cast<int>(player_index), 1);
    UpdateLinkLobbyTribeComboBoxState(state, static_cast<int>(player_index));
    UpdateLinkLobbyMapDownloadButtonVisibility(state, static_cast<int>(player_index));
    UpdateLinkLobbyLatencyButtonVisibility(state, static_cast<int>(player_index));
}

void HandleLinkLobbyPlayerRoleComboChange(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }

    const int next_role = selected_player_role_value(state, player_index);
    if (!state.player_socket_connected[player_index]) {
        ApplyLinkLobbyPlayerRolePacket(state, static_cast<u32>(player_index),
            next_role);
        send_player_role_packet(state, player_index, next_role);
        return;
    }

    if (next_role == 0) {
        set_combo_selection(state.player_role_combos[player_index], 0);
        return;
    }
    if (!confirm_connected_player_role_change(state, player_index, next_role)) {
        set_combo_selection(state.player_role_combos[player_index], 0);
        return;
    }

    HandleLinkLobbyPlayerDisconnected(state, static_cast<u32>(player_index));
    SendLinkLobbyPlayerDisconnectPacket(state, static_cast<u32>(player_index));
    ApplyLinkLobbyPlayerRolePacket(state, static_cast<u32>(player_index),
        next_role);
    send_player_role_packet(state, player_index, state.player_role_values[player_index]);
}

int FindOpenLinkLobbyPlayerRoleSlot(const LinkLobbyState& state) {
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (state.player_role_values[i] == 1) {
            return i;
        }
    }
    return -1;
}

void ApplyLinkLobbyPlayerRolePacket(LinkLobbyState& state, u32 player_index,
    int role_value) {
    if (player_index >= kLinkLobbyAvatarCount) {
        return;
    }
    const int index = static_cast<int>(player_index);
    state.player_role_values[index] = std::clamp(role_value, 0, 3);
    state.players[index].human = state.player_role_values[index] == 0;
    state.players[index].occupied =
        state.player_role_values[index] == 0 || state.player_role_values[index] == 3;
    state.players[index].ready = state.players[index].occupied;
    PopulateLinkLobbyPlayerRoleComboBox(state, index, state.player_role_values[index]);
    UpdateLinkLobbyTribeComboBoxState(state, index);
    UpdateLinkLobbyMapDownloadButtonVisibility(state, index);
    UpdateLinkLobbyLatencyButtonVisibility(state, index);
}

void ApplyLinkLobbyOpenRolePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    ApplyLinkLobbyPlayerRolePacket(state, packet_u32(packet, byte_count, 0x0c), 1);
}

void ApplyLinkLobbyComputerRolePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    ApplyLinkLobbyPlayerRolePacket(state, packet_u32(packet, byte_count, 0x0c), 2);
}

void ApplyLinkLobbyClosedRolePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    ApplyLinkLobbyPlayerRolePacket(state, packet_u32(packet, byte_count, 0x0c), 3);
}

void BroadcastLinkLobbyRoleSelections(LinkLobbyState& state) {
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        send_player_role_packet(state, i, state.player_role_values[i]);
        send_link_lobby_two_value_packet(state, kLinkLobbyTribeSelectionOpcode,
            static_cast<u32>(i), state.tribe_choices[i]);
    }
    send_link_lobby_two_value_packet(state, kLinkLobbyStartResourceOpcode,
        static_cast<u32>(state.start_resource_index), 0);
    send_link_lobby_two_value_packet(state, kLinkLobbyScreenSizeOpcode,
        static_cast<u32>(state.screen_size_index), 0);
    send_link_lobby_two_value_packet(state, kLinkLobbyHostResourceOpcode,
        static_cast<u32>(state.host_resource_index), 0);
}

void DispatchLinkLobbyStartResultPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x14) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    HandleLinkLobbyStartResult(state, read_le32(bytes + 0x0c),
        static_cast<int>(read_le32(bytes + 0x10)));
}

void HandleLinkLobbyIncomingPlayerJoinRequest(LinkLobbyState& state, u32 sender,
    const void* packet, std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x10) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    const int requested_slot = static_cast<int>(read_le32(bytes + 0x0c));
    const SOCKET sender_socket = static_cast<SOCKET>(sender);

    // The host rebroadcasts an accepted player's opcode-0x0c record to every
    // connected peer.  On an original client this is only a player-table
    // update (FUN_00473100's non-host branch); it must not be treated as a new
    // join request or answered with another result-10 packet.  Doing so makes
    // the original host consume that reply as its own join result and changes
    // its local player slot.
    if (!state.host_mode) {
        if (player_index_valid(requested_slot)) {
            accept_link_lobby_join_request_slot(state, requested_slot,
                sender_socket, bytes, byte_count);
            append_link_lobby_log(
                "link join record applied slot=%ld sender_socket=%llu",
                static_cast<long>(requested_slot),
                static_cast<unsigned long long>(sender_socket));
        }
        return;
    }

    const AsyncComContext* context = async_com_state().active_context;
    const bool directplay_ready =
        context != nullptr && context->system_message_101_seen;

    if (!directplay_ready) {
        int assigned_slot = requested_slot;
        if (!player_index_valid(assigned_slot) ||
            state.player_role_values[assigned_slot] != 1) {
            assigned_slot = FindOpenLinkLobbyPlayerRoleSlot(state);
        }
        if (!player_index_valid(assigned_slot)) {
            SendLinkLobbyStartResultPacket(state, sender_socket, 0, 3);
            return;
        }
        accept_link_lobby_join_request_slot(state, assigned_slot, sender_socket,
            bytes, byte_count);
        SendLinkLobbyStartResultPacket(state, sender_socket,
            static_cast<u32>(assigned_slot), 10);
        if (player_has_advertised_udp_route(state,
                state.local_player_index)) {
            const u32 local = static_cast<u32>(state.local_player_index);
            SendLinkLobbyPeerRoutePacket(state, local,
                state.primary_peer_hosts[local].data(),
                state.primary_peer_ports[local],
                state.secondary_peer_hosts[local].data(),
                state.secondary_peer_ports[local]);
        }
        SendLinkLobbyCurrentRoleStatePackets(state);
        return;
    }

    if (state.map_download_received_bytes != 0xffffffffu ||
        !state.resources_ready) {
        SendLinkLobbyStartResultPacket(state, sender_socket, 0, 5);
        return;
    }

    if (player_index_valid(requested_slot) &&
        state.player_role_values[requested_slot] == 1) {
        accept_link_lobby_join_request_slot(state, requested_slot, sender_socket,
            bytes, byte_count);
        SendLinkLobbyStartResultPacket(state, sender_socket,
            static_cast<u32>(requested_slot), 10);
        if (state.mode >= 0 && state.mode < 3) {
            SendLinkLobbyJoinRequestPacket(state, static_cast<u32>(requested_slot),
                state.players[requested_slot].raw_payload.data(), INVALID_SOCKET);
        }
        return;
    }

    if (state.mode < 0 || state.mode >= 3) {
        if (player_index_valid(requested_slot) &&
            link_lobby_player_record_socket(state, requested_slot) == sender_socket) {
            return;
        }
    }

    const int slot = FindOpenLinkLobbyPlayerRoleSlot(state);
    if (player_index_valid(slot)) {
        accept_link_lobby_join_request_slot(state, slot, sender_socket, bytes,
            byte_count);
        SendLinkLobbyStartResultPacket(state, sender_socket,
            static_cast<u32>(slot), 10);
        if (state.mode >= 0 && state.mode < 3) {
            SendLinkLobbyJoinRequestPacket(state, static_cast<u32>(slot),
                state.players[slot].raw_payload.data(), INVALID_SOCKET);
            SendLinkLobbyCurrentRoleStatePackets(state);
        }
        return;
    }

    SendLinkLobbyStartResultPacket(state, sender_socket, 0, 3);
}

void ApplyLinkLobbyPlayerRecordPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index) ||
        byte_count < 0x10 + 0x19e) {
        return;
    }

    const auto* bytes = static_cast<const u8*>(packet);
    std::memcpy(state.players[player_index].raw_payload.data(), bytes + 0x10,
        std::min<std::size_t>(0x19e, state.players[player_index].raw_payload.size()));
    state.player_payloads[player_index] = state.players[player_index].raw_payload;
    state.players[player_index].occupied = true;
    state.players[player_index].ready = true;
    ResetLinkLobbyPlayerRoleToHuman(state, player_index);
}

void HandleLinkLobbyPlayerDisconnectPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index)) {
        return;
    }

    const AsyncComContext* context = async_com_state().active_context;
    const bool directplay_ready =
        context != nullptr && context->system_message_101_seen;
    if (state.mode == 3 &&
        link_lobby_player_record_socket(state, player_index) == state.shared_peer_socket &&
        !directplay_ready) {
        show_startup_message(state, 30, "The game host canceled the game.",
            kLinkHostCancelRed);
        ReturnFromLinkLobby(state);
        return;
    }

    HandleLinkLobbyPlayerDisconnected(state, static_cast<u32>(player_index));
    state.player_role_values[player_index] = 1;
    PopulateLinkLobbyPlayerRoleComboBox(state, player_index, 1);
    UpdateLinkLobbyTribeComboBoxState(state, player_index);
    UpdateLinkLobbyMapDownloadButtonVisibility(state, player_index);
    UpdateLinkLobbyLatencyButtonVisibility(state, player_index);
}

void HandleLinkLobbyPlayerRemovalPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index)) {
        return;
    }
    if (link_lobby_player_record_socket(state, player_index) == 0) {
        return;
    }
    HandleLinkLobbyPlayerDisconnected(state, static_cast<u32>(player_index));
    SendLinkLobbyPlayerDisconnectPacket(state, static_cast<u32>(player_index));
    state.player_role_values[player_index] = 1;
    PopulateLinkLobbyPlayerRoleComboBox(state, player_index, 1);
    UpdateLinkLobbyTribeComboBoxState(state, player_index);
    UpdateLinkLobbyMapDownloadButtonVisibility(state, player_index);
    UpdateLinkLobbyLatencyButtonVisibility(state, player_index);
}

void HandleLinkLobbyHostClosedPacket(LinkLobbyState& state) {
    if (state.join_accepted) {
        if (state.countdown_value >= 0) {
            append_link_lobby_log(
                "link host-close packet accepted during udp start countdown value=%ld",
                static_cast<long>(state.countdown_value));
            return;
        }
        ReturnFromLinkLobby(state);
    }
}

void HandleLinkLobbyAutoMoveOpenSlotPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int from_player = 0;
    if (!packet_player_index(packet, byte_count, from_player) || byte_count < 0x14) {
        return;
    }
    const int group_index = static_cast<int>(packet_u32(packet, byte_count, 0x10));
    const int max_players = std::clamp(
        static_cast<int>(link_lobby_seed_max_players(state)), 0,
        kLinkLobbyAvatarCount);
    if (max_players <= 0) {
        return;
    }

    int target_player = -1;
    const auto slot_matches = [&](int candidate) {
        return candidate >= 0 && candidate < max_players &&
            state.player_team_values[candidate] == group_index &&
            state.player_role_values[candidate] == 1;
    };
    for (int candidate = from_player + 1; candidate < max_players; ++candidate) {
        if (slot_matches(candidate)) {
            target_player = candidate;
            break;
        }
    }
    for (int candidate = 0; target_player < 0 && candidate < max_players; ++candidate) {
        if (slot_matches(candidate)) {
            target_player = candidate;
        }
    }
    if (target_player < 0) {
        return;
    }

    const AsyncComContext* context = async_com_state().active_context;
    if (context != nullptr && context->system_message_101_seen) {
        SwapLinkLobbyPlayerSlots(state, from_player, target_player);
    }
    SendLinkLobbySlotSwapPacket(state, static_cast<u32>(from_player),
        static_cast<u32>(target_player));
}

void ApplyLinkLobbyPlayerSlotSwapPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (byte_count < 0x14) {
        return;
    }
    const int left_player = static_cast<int>(packet_u32(packet, byte_count, 0x0c));
    const int right_player = static_cast<int>(packet_u32(packet, byte_count, 0x10));
    SwapLinkLobbyPlayerSlots(state, left_player, right_player);
}

void ApplyLinkLobbyTribeSelectionPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index) || byte_count < 0x14) {
        return;
    }
    const int selection = static_cast<int>(packet_u32(packet, byte_count, 0x10));
    state.tribe_choices[player_index] = static_cast<u8>(std::clamp(selection, 0, 0xff));
    set_combo_selection(state.tribe_combos[player_index], selection);
    const AsyncComContext* context = async_com_state().active_context;
    if (context != nullptr && context->system_message_101_seen) {
        send_link_lobby_two_value_packet(state, kLinkLobbyTribeSelectionOpcode,
            static_cast<u32>(player_index), static_cast<u32>(selection));
    }
}

void ApplyLinkLobbyStartResourceSelectionPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count) {
    if (byte_count < 0x10) {
        return;
    }
    state.start_resource_index =
        static_cast<int>(packet_u32(packet, byte_count, 0x0c));
    set_combo_selection(state.start_resource_combo, state.start_resource_index);
}

void ApplyLinkLobbyHostResourceSelectionPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count) {
    if (byte_count < 0x10) {
        return;
    }
    state.host_resource_index =
        static_cast<int>(packet_u32(packet, byte_count, 0x0c));
    set_combo_selection(state.host_resource_combo, state.host_resource_index);
}

void ApplyLinkLobbyScreenSizeSelectionPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count) {
    if (byte_count < 0x10) {
        return;
    }
    state.screen_size_index = static_cast<int>(packet_u32(packet, byte_count, 0x0c));
    set_combo_selection(state.screen_size_combo, state.screen_size_index);
}

void ApplyLinkLobbyMapSelectionPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (byte_count < 0x10) {
        return;
    }
    const int scroll_value = std::clamp(
        static_cast<int>(packet_u32(packet, byte_count, 0x0c)), 0, 0x0f);
    state.map_selection_index = 0x0f - scroll_value;
    if (state.map_selection_scroll.window != nullptr) {
        SetLegacyCustomScrollControlValue(state.map_selection_scroll, scroll_value, true);
    }
}

void ApplyLinkLobbyMapDescriptorPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (packet == nullptr || byte_count <= 0x0c) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    const std::size_t copy_count =
        std::min<std::size_t>(state.map_descriptor.size(), byte_count - 0x0c);
    state.map_descriptor = {};
    std::memcpy(state.map_descriptor.data(), bytes + 0x0c, copy_count);
    apply_link_lobby_map_descriptor_fields(state, true);
    state.map_download_candidate_valid = true;
}

void ApplyLinkLobbyMapDownloadChunkPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x14) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    const u32 offset = read_le32(bytes + 0x0c);
    const u32 chunk_size = read_le32(bytes + 0x10);
    if (byte_count < 0x14 + chunk_size ||
        offset != state.map_download_received_bytes) {
        return;
    }

    const std::string target_path = state.prepared_map_path.empty() ?
        join_path(download_directory_path(), basename_from_path(state.map_file_name).c_str()) :
        state.prepared_map_path;
    FILE* file = target_path.empty() ? nullptr :
        std::fopen(target_path.c_str(), offset == 0 ? "wb" : "ab");
    if (file == nullptr) {
        SendLinkLobbyPlayerDisconnectPacket(state,
            static_cast<u32>(state.local_player_index));
        show_startup_message(state, 76,
            "A file error occurred while downloading the game map.",
            kLinkMapFailureRed);
        ReturnFromLinkLobby(state);
        return;
    }
    std::fwrite(bytes + 0x14, 1, chunk_size, file);
    std::fclose(file);

    state.map_download_received_bytes += chunk_size;
    const u32 expected_size = state.expected_map_file_size == 0 ?
        state.map_download_received_bytes : state.expected_map_file_size;
    if (expected_size != 0) {
        state.map_download_progress[state.local_player_index] =
            static_cast<int>(std::min<u32>(
                100, offset * 100 / expected_size));
    }
    RedrawLinkLobbyMapDownloadButton(state, state.local_player_index);
    if (state.map_download_received_bytes >= expected_size) {
        state.map_download_progress[state.local_player_index] = 100;
        state.map_download_received_bytes = 0xffffffffu;
        SendLinkLobbyMapRequestPacket(state, static_cast<u32>(state.local_player_index),
            0xffffffffu);
        SendLinkLobbyMapProgressPacket(state, static_cast<u32>(state.local_player_index),
            100);
        if (state.expected_map_file_time_valid && !target_path.empty()) {
            HANDLE file = CreateFileA(target_path.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                SetFileTime(file, nullptr, nullptr, &state.expected_map_file_time);
                CloseHandle(file);
            }
        }
        UpdateLinkLobbyMapDownloadButtonVisibility(state, state.local_player_index);
        RedrawWindow(state.game_info_button.window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    } else {
        SendLinkLobbyMapRequestPacket(state, static_cast<u32>(state.local_player_index),
            state.map_download_received_bytes);
        SendLinkLobbyMapProgressPacket(state, static_cast<u32>(state.local_player_index),
            static_cast<u32>(state.map_download_progress[state.local_player_index]));
    }
}

void ApplyLinkLobbyMapDownloadProgressPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index) || byte_count < 0x14) {
        return;
    }
    state.map_download_progress[player_index] =
        static_cast<int>(packet_u32(packet, byte_count, 0x10));
    state.map_download_progress[player_index] =
        std::clamp(state.map_download_progress[player_index], 0, 100);
    const AsyncComContext* context = async_com_state().active_context;
    if (context != nullptr && context->system_message_101_seen) {
        SendLinkLobbyMapProgressPacket(state, static_cast<u32>(player_index),
            static_cast<u32>(state.map_download_progress[player_index]));
    }
    if (state.map_download_progress[player_index] == 100) {
        UpdateLinkLobbyMapDownloadButtonVisibility(state, player_index);
    } else {
        UpdateLinkLobbyMapDownloadButtonVisibility(state, player_index);
        RedrawLinkLobbyMapDownloadButton(state, player_index);
    }
}

void ApplyLinkLobbyMapDownloadRequestPacket(LinkLobbyState& state,
    const void* packet, std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index) || byte_count < 0x14) {
        return;
    }
    const u32 requested_offset = packet_u32(packet, byte_count, 0x10);
    if (requested_offset == 0xffffffffu) {
        state.map_download_progress[player_index] = 100;
        return;
    }
    if (state.window != nullptr) {
        PostMessageA(state.window, kLinkLobbyDownloadProgressMessage,
            static_cast<WPARAM>(player_index), static_cast<LPARAM>(requested_offset));
    }
}

void ApplyLinkLobbyStartParametersPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x0c) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    state.start_parameter_payload.assign(bytes, bytes + byte_count);
    apply_link_lobby_start_parameter_payload_fields(state, bytes, byte_count);
    state.start_locked = true;
    if (state.window != nullptr) {
        PostMessageA(state.window, kLinkLobbyStartDecisionMessage, 0, 0);
    }
}

void HandleLinkLobbyStartTimeoutPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (byte_count < 0x10) {
        return;
    }
    const u32 player_index = packet_u32(packet, byte_count, 0x0c);
    const AsyncComContext* context = async_com_state().active_context;
    if (context != nullptr && context->system_message_101_seen) {
        SendLinkLobbyStartTimeoutPacket(state, player_index);
    }
    if (state.window != nullptr) {
        PostMessageA(state.window, kLinkLobbyStartDecisionMessage, 1,
            static_cast<LPARAM>(player_index));
    }
}

void ApplyLinkLobbySessionSeedPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (packet == nullptr) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    const bool wrapped_seed =
        byte_count >= 0x0c + state.session_seed_payload.size() &&
        read_le32(bytes + 8) >= 0x0c + state.session_seed_payload.size();
    if (wrapped_seed) {
        std::memcpy(state.session_seed_payload.data(), bytes + 0x0c,
            state.session_seed_payload.size());
    } else if (byte_count >= state.session_seed_payload.size()) {
        std::memcpy(state.session_seed_payload.data(), bytes,
            state.session_seed_payload.size());
    } else {
        return;
    }
    apply_link_lobby_session_seed_fields(state);
}

void ApplyLinkLobbyPlayerPresencePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index) ||
        byte_count < 0x10 + 0x19e) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    std::memcpy(state.players[player_index].raw_payload.data(), bytes + 0x10,
        std::min<std::size_t>(0x19e, state.players[player_index].raw_payload.size()));
    state.player_payloads[player_index] = state.players[player_index].raw_payload;

    const bool present = byte_count <= 0x19a || packet_u32(packet, byte_count, 0x19a) != 0;
    if (present) {
        state.players[player_index].occupied = true;
        state.players[player_index].ready = true;
        state.players[player_index].human = true;
        copy_fixed_string(state.players[player_index].name,
            state.players[player_index].raw_payload.data() + kLinkLobbyPlayerRecordNameOffset,
            kLinkLobbyPlayerRecordNameBytes);
    } else {
        state.players[player_index].occupied = false;
        state.players[player_index].ready = false;
        state.players[player_index].human = false;
    }
    if (!state.join_accepted) {
        return;
    }

    if (!present) {
        HandleLinkLobbyPlayerDisconnected(state, static_cast<u32>(player_index));
        state.player_role_values[player_index] = 1;
        PopulateLinkLobbyPlayerRoleComboBox(state, player_index, 1);
        UpdateLinkLobbyMapDownloadButtonVisibility(state, player_index);
        UpdateLinkLobbyLatencyButtonVisibility(state, player_index);
    } else {
        ResetLinkLobbyPlayerRoleToHuman(state, player_index);
    }
}

void BeginLinkLobbyPeerRouteSync(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    if (packet != nullptr && byte_count >= 0x14c) {
        const auto* bytes = static_cast<const u8*>(packet);
        for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
            copy_fixed_string(state.primary_peer_hosts[i], bytes + 0x0c + i * 0x10,
                0x10);
            state.primary_peer_ports[i] =
                static_cast<u16>(read_le32(bytes + 0x8c + i * 4));
            copy_fixed_string(state.secondary_peer_hosts[i], bytes + 0xac + i * 0x10,
                0x10);
            state.secondary_peer_ports[i] =
                static_cast<u16>(read_le32(bytes + 0x12c + i * 4));
        }
    }
    state.peer_route_acknowledged = {};
    state.start_acknowledged = {};
    state.start_sync_complete = false;
    state.start_sync_retry_count = 0;
    if (player_index_valid(state.local_player_index)) {
        state.peer_route_acknowledged[state.local_player_index] = 1;
        state.start_acknowledged[state.local_player_index] = 1;
    }
    if (state.window != nullptr) {
        StopLinkLobbyPeerRouteTimer(state);
        state.peer_route_timer = SetTimer(state.window, 2, 300, nullptr);
    }
}

void ApplyLinkLobbyPeerRoutePacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index) || byte_count < 0x38) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    copy_fixed_string(state.primary_peer_hosts[player_index], bytes + 0x10, 0x10);
    state.primary_peer_ports[player_index] =
        static_cast<u16>(packet_u32(packet, byte_count, 0x20));
    copy_fixed_string(state.secondary_peer_hosts[player_index], bytes + 0x24, 0x10);
    state.secondary_peer_ports[player_index] =
        static_cast<u16>(packet_u32(packet, byte_count, 0x34));
    append_link_lobby_log(
        "link udp route received slot=%ld primary=%s:%u secondary=%s:%u",
        static_cast<long>(player_index),
        state.primary_peer_hosts[player_index].data(),
        static_cast<unsigned>(state.primary_peer_ports[player_index]),
        state.secondary_peer_hosts[player_index].data(),
        static_cast<unsigned>(state.secondary_peer_ports[player_index]));
}

void HandleLinkLobbyUdpPeerProbeRequest(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int probe_player = 0;
    if (!packet_player_index(packet, byte_count, probe_player) || byte_count < 0x14) {
        return;
    }
    const int target_player = static_cast<int>(packet_u32(packet, byte_count, 0x10));
    if (target_player == state.local_player_index && player_index_valid(probe_player)) {
        const bool primary_ready = route_endpoint_ready(
            state.primary_peer_hosts[probe_player],
            state.primary_peer_ports[probe_player]);
        const bool secondary_ready = route_endpoint_ready(
            state.secondary_peer_hosts[probe_player],
            state.secondary_peer_ports[probe_player]);
        const bool routes_differ = primary_ready && secondary_ready &&
            (state.primary_peer_ports[probe_player] !=
                    state.secondary_peer_ports[probe_player] ||
                std::strcmp(state.primary_peer_hosts[probe_player].data(),
                    state.secondary_peer_hosts[probe_player].data()) != 0);
        bool use_secondary = !primary_ready && secondary_ready;
        if (routes_differ) {
            state.udp_probe_route_toggle = !state.udp_probe_route_toggle;
            use_secondary = !state.udp_probe_route_toggle;
        }

        const char* host = state.primary_peer_hosts[probe_player].data();
        u16 port = state.primary_peer_ports[probe_player];
        if (use_secondary) {
            host = state.secondary_peer_hosts[probe_player].data();
            port = state.secondary_peer_ports[probe_player];
        }
        if (host[0] != '\0' && port != 0) {
            SendLinkLobbyUdpProbeDatagram(state,
                static_cast<u32>(state.local_player_index), host, port);
            append_link_lobby_log(
                "link udp probe sent from=%ld to=%ld endpoint=%s:%u",
                static_cast<long>(state.local_player_index),
                static_cast<long>(probe_player), host,
                static_cast<unsigned>(port));
        }
    } else if (player_index_valid(target_player)) {
        if (state.host_mode || link_lobby_directplay_ready()) {
            send_link_lobby_two_value_packet(state, kLinkLobbyUdpProbeRequestOpcode,
                static_cast<u32>(probe_player), static_cast<u32>(target_player),
                start_sync_target_socket(state, target_player));
        }
    }
}

void ApplyLinkLobbyUdpPeerProbeReply(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index)) {
        return;
    }
    state.udp_peer_addresses[player_index] = legacy_network_state().udp_last_sender;
    SetDirectPlayMode1UdpPeerAddress(player_index, state.udp_peer_addresses[player_index]);
    state.peer_route_acknowledged[player_index] = 1;
    state.start_acknowledged[player_index] = 1;
}

void ApplyLinkLobbySecondaryStartAckPacket(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    int player_index = 0;
    if (!packet_player_index(packet, byte_count, player_index) || byte_count < 0x14) {
        return;
    }
    state.secondary_start_acknowledged[player_index] =
        static_cast<u8>(packet_u32(packet, byte_count, 0x10) != 0);
}

void StopLinkLobbyPeerRouteTimer(LinkLobbyState& state) {
    if (state.peer_route_timer != 0 && state.window != nullptr) {
        KillTimer(state.window, state.peer_route_timer);
        state.peer_route_timer = 0;
    }
}

void DispatchLinkLobbyTransportPacket(LinkLobbyState& state, u32 sender,
    const void* packet, std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x0c) {
        return;
    }
    const auto* bytes = static_cast<const u8*>(packet);
    if (read_le32(bytes) != kLinkLobbyTransportPacketType) {
        return;
    }

    const u32 opcode = read_le32(bytes + 4);
    const u32 packet_size = read_le32(bytes + 8);
    const std::size_t effective_size =
        packet_size == 0 ? byte_count : std::min<std::size_t>(packet_size, byte_count);
    switch (opcode) {
    case kLinkLobbyRoleOpenOpcode:
        ApplyLinkLobbyOpenRolePacket(state, packet, effective_size);
        break;
    case kLinkLobbyRoleComputerOpcode:
        ApplyLinkLobbyComputerRolePacket(state, packet, effective_size);
        break;
    case kLinkLobbyRoleClosedOpcode:
        ApplyLinkLobbyClosedRolePacket(state, packet, effective_size);
        break;
    case kLinkLobbyStartResourceOpcode:
        ApplyLinkLobbyStartResourceSelectionPacket(state, packet, effective_size);
        break;
    case kLinkLobbyScreenSizeOpcode:
        ApplyLinkLobbyScreenSizeSelectionPacket(state, packet, effective_size);
        break;
    case kLinkLobbyMapSelectionOpcode:
        ApplyLinkLobbyMapSelectionPacket(state, packet, effective_size);
        break;
    case kLinkLobbyRelayJoinOpcode:
        ForwardLinkLobbyRelayPacket(state, static_cast<SOCKET>(sender), packet,
            effective_size);
        break;
    case kLinkLobbySessionSeedOpcode:
        ApplyLinkLobbySessionSeedPacket(state, packet, effective_size);
        break;
    case kLinkLobbyStartResultOpcode:
        DispatchLinkLobbyStartResultPacket(state, packet, effective_size);
        break;
    case kLinkLobbyJoinRequestOpcode:
        HandleLinkLobbyIncomingPlayerJoinRequest(state, sender, packet, effective_size);
        break;
    case kLinkLobbyBroadcastRolesOpcode:
        BroadcastLinkLobbyRoleSelections(state);
        break;
    case kLinkLobbyPlayerDisconnectOpcode:
        HandleLinkLobbyPlayerDisconnectPacket(state, packet, effective_size);
        break;
    case kLinkLobbyPlayerRemovalOpcode:
        HandleLinkLobbyPlayerRemovalPacket(state, packet, effective_size);
        break;
    case kLinkLobbyHostClosedOpcode:
        HandleLinkLobbyHostClosedPacket(state);
        break;
    case kLinkLobbyAutoMoveOpenSlotOpcode:
        HandleLinkLobbyAutoMoveOpenSlotPacket(state, packet, effective_size);
        break;
    case kLinkLobbySlotSwapOpcode:
        ApplyLinkLobbyPlayerSlotSwapPacket(state, packet, effective_size);
        break;
    case kLinkLobbyTribeSelectionOpcode:
        ApplyLinkLobbyTribeSelectionPacket(state, packet, effective_size);
        break;
    case kLinkLobbyMapDescriptorOpcode:
        ApplyLinkLobbyMapDescriptorPacket(state, packet, effective_size);
        break;
    case kLinkLobbyMapChunkOpcode:
        ApplyLinkLobbyMapDownloadChunkPacket(state, packet, effective_size);
        break;
    case kLinkLobbyMapProgressOpcode:
        ApplyLinkLobbyMapDownloadProgressPacket(state, packet, effective_size);
        break;
    case kLinkLobbyMapRequestOpcode:
        ApplyLinkLobbyMapDownloadRequestPacket(state, packet, effective_size);
        break;
    case kLinkLobbyStartParametersOpcode:
        if (state.join_accepted) {
            ApplyLinkLobbyStartParametersPacket(state, packet, effective_size);
        } else {
            HandleLinkLobbyStartResult(state, 0, 5);
        }
        break;
    case kLinkLobbyStartTimeoutOpcode:
        HandleLinkLobbyStartTimeoutPacket(state, packet, effective_size);
        break;
    case kLinkLobbyPlayerPresenceOpcode:
        ApplyLinkLobbyPlayerPresencePacket(state, packet, effective_size);
        break;
    case kLinkLobbyPlayerRecordOpcode:
        ApplyLinkLobbyPlayerRecordPacket(state, packet, effective_size);
        break;
    case kLinkLobbyPeerRouteSyncOpcode:
        BeginLinkLobbyPeerRouteSync(state, packet, effective_size);
        break;
    case kLinkLobbyPeerRouteOpcode:
        ApplyLinkLobbyPeerRoutePacket(state, packet, effective_size);
        if (state.host_mode) {
            BroadcastLinkLobbyTransportPacketExcept(state, packet, effective_size,
                static_cast<SOCKET>(sender));
        }
        break;
    case kLinkLobbyUdpProbeRequestOpcode:
        HandleLinkLobbyUdpPeerProbeRequest(state, packet, effective_size);
        break;
    case kLinkLobbyUdpProbeReplyOpcode:
        ApplyLinkLobbyUdpPeerProbeReply(state, packet, effective_size);
        break;
    case kLinkLobbySecondaryStartAckOpcode:
        ApplyLinkLobbySecondaryStartAckPacket(state, packet, effective_size);
        break;
    case kLinkLobbyStopPeerRouteTimerOpcode:
        StopLinkLobbyPeerRouteTimer(state);
        break;
    case kLinkLobbyAvatarPublishOpcode:
        CopyIncomingLinkLobbyPlayerPayload(state, packet, effective_size);
        break;
    case kLinkLobbyHostResourceOpcode:
        ApplyLinkLobbyHostResourceSelectionPacket(state, packet, effective_size);
        break;
    default:
        break;
    }
}

void ForwardLinkLobbyRelayPacket(LinkLobbyState& state, SOCKET sender_socket,
    const void* packet, std::size_t byte_count) {
    DispatchLinkLobbyRelayPacket(state, sender_socket, packet, byte_count);
}

bool SendLinkLobbyRawTransportPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* packet, std::size_t byte_count) {
    if (packet == nullptr || byte_count == 0) {
        return false;
    }
    return send_lobby_transport_payload(state, packet,
        static_cast<i32>(std::min<std::size_t>(byte_count, 0x7fffffff)),
        target_socket);
}

void BroadcastLinkLobbyTransportPacketExcept(LinkLobbyState& state,
    const void* packet, std::size_t byte_count, SOCKET excluded_socket) {
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        const SOCKET socket = state.player_sockets[i];
        if (state.player_socket_connected[i] && socket != INVALID_SOCKET &&
            socket != excluded_socket && socket != state.shared_peer_socket) {
            SendLinkLobbyRawTransportPacket(state, socket, packet, byte_count);
        }
    }
}

bool SendLinkLobbyTransportPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* packet, std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x0c) {
        return false;
    }
    std::vector<u8> payload(static_cast<const u8*>(packet),
        static_cast<const u8*>(packet) + byte_count);
    write_le32(payload, 0, kLinkLobbyTransportPacketType);
    write_le32(payload, 8, static_cast<u32>(payload.size()));
    return SendLinkLobbyRawTransportPacket(state, target_socket, payload.data(),
        payload.size());
}

void SendLinkLobbyOpenRolePacket(LinkLobbyState& state, int player_index,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyRoleOpenOpcode,
        static_cast<u32>(player_index), target_socket);
}

void SendLinkLobbyComputerRolePacket(LinkLobbyState& state, int player_index,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyRoleComputerOpcode,
        static_cast<u32>(player_index), target_socket);
}

void SendLinkLobbyClosedRolePacket(LinkLobbyState& state, int player_index,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyRoleClosedOpcode,
        static_cast<u32>(player_index), target_socket);
}

void SendLinkLobbyRoleBroadcastRequest(LinkLobbyState& state) {
    send_link_lobby_one_value_packet(state, kLinkLobbyBroadcastRolesOpcode,
        static_cast<u32>(state.local_player_index), state.shared_peer_socket);
}

void SendLinkLobbyStartResultPacket(LinkLobbyState& state, SOCKET target_socket,
    u32 player_index, u32 result_code) {
    send_link_lobby_two_value_packet(state, kLinkLobbyStartResultOpcode,
        player_index, result_code, target_socket);
}

void SendLinkLobbyJoinRequestPacket(LinkLobbyState& state, u32 player_index,
    const void* player_record, SOCKET target_socket) {
    std::vector<u8> packet(0x1ae, 0);
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyJoinRequestOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, player_index);
    if (player_record != nullptr) {
        std::memcpy(packet.data() + 0x10, player_record, 0x19e);
    }
    SendLinkLobbyRawTransportPacket(state, target_socket, packet.data(),
        packet.size());
}

void SendLinkLobbyPlayerRecordPacket(LinkLobbyState& state, u32 player_index,
    const void* player_record, SOCKET target_socket) {
    std::vector<u8> packet(0x1ae, 0);
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyPlayerRecordOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, player_index);
    if (player_record != nullptr) {
        std::memcpy(packet.data() + 0x10, player_record, 0x19e);
    }
    SendLinkLobbyRawTransportPacket(state, target_socket, packet.data(),
        packet.size());
}

void SendLinkLobbyPlayerDisconnectPacket(LinkLobbyState& state, u32 player_index) {
    send_link_lobby_one_value_packet(state, kLinkLobbyPlayerDisconnectOpcode,
        player_index, 0);
}

void SendLinkLobbyPlayerRemovalPacket(LinkLobbyState& state, u32 player_index,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyPlayerRemovalOpcode,
        player_index, target_socket);
}

void SendLinkLobbyHostClosedPacket(LinkLobbyState& state, SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyHostClosedOpcode,
        static_cast<u32>(state.local_player_index), target_socket);
}

void ResetLinkLobbyTransportRateTimer(LinkLobbyState& state) {
    state.last_transport_rate_time_ms = GetTickCount();
}

bool IsLinkLobbyTransportRateElapsed(LinkLobbyState& state) {
    const u32 now = GetTickCount();
    if (state.last_transport_rate_time_ms + 1000 < now) {
        state.last_transport_rate_time_ms = now;
        return true;
    }
    return false;
}

void SendLinkLobbyAutoMoveOpenSlotPacket(LinkLobbyState& state, u32 player_index,
    u32 group_index) {
    if (state.mode != 6 && IsLinkLobbyTransportRateElapsed(state)) {
        std::array<u8, 0x14> packet{};
        write_le32(packet, 0, kLinkLobbyTransportPacketType);
        write_le32(packet, 4, kLinkLobbyAutoMoveOpenSlotOpcode);
        write_le32(packet, 8, static_cast<u32>(packet.size()));
        write_le32(packet, 0x0c, player_index);
        write_le32(packet, 0x10, group_index);
        const AsyncComContext* context = async_com_state().active_context;
        if (context != nullptr && context->system_message_101_seen) {
            HandleLinkLobbyAutoMoveOpenSlotPacket(state, packet.data(), packet.size());
        } else {
            SendLinkLobbyRawTransportPacket(state, state.shared_peer_socket,
                packet.data(), packet.size());
        }
    }
}

void SendLinkLobbySlotSwapPacket(LinkLobbyState& state, u32 left_player,
    u32 right_player) {
    send_link_lobby_two_value_packet(state, kLinkLobbySlotSwapOpcode,
        left_player, right_player);
}

void SendLinkLobbyRelayJoinPacket(LinkLobbyState& state, SOCKET target_socket,
    const char* player_name, const char* password) {
    std::vector<u8> packet(kLinkLobbyRelayJoinPacketBytes, 0);
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyRelayJoinOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, static_cast<u32>(state.local_player_index));
    write_le32(packet, 0x10, kLinkLobbyHandshakeMagic);
    write_le32(packet, 0x14, LoadTrcRecord9Value());
    write_le32(packet, 0x18, active_link_lobby_connection_mode(state));
    if (player_name != nullptr) {
        std::strncpy(reinterpret_cast<char*>(packet.data() + 0x1c), player_name, 0x20);
    }
    if (password != nullptr) {
        std::strncpy(reinterpret_cast<char*>(packet.data() + 0x3c), password, 10);
    }
    SendLinkLobbyRawTransportPacket(state, target_socket, packet.data(),
        packet.size());
}

void SendLinkLobbyTribeSelectionPacket(LinkLobbyState& state, u32 player_index,
    u32 selection, SOCKET target_socket) {
    send_link_lobby_two_value_packet(state, kLinkLobbyTribeSelectionOpcode,
        player_index, selection, target_socket);
}

void SendLinkLobbyHostResourceSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyHostResourceOpcode,
        selection, target_socket);
}

void SendLinkLobbyStartResourceSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyStartResourceOpcode,
        selection, target_socket);
}

void SendLinkLobbyScreenSizeSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyScreenSizeOpcode,
        selection, target_socket);
}

void SendLinkLobbyMapSelectionPacket(LinkLobbyState& state, u32 selection,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyMapSelectionOpcode,
        selection, target_socket);
}

void SendLinkLobbyMapDescriptorPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* descriptor, std::size_t byte_count) {
    std::vector<u8> packet(0x0c + state.map_descriptor.size(), 0);
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyMapDescriptorOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    if (descriptor != nullptr && byte_count != 0) {
        std::memcpy(packet.data() + 0x0c, descriptor,
            std::min<std::size_t>(byte_count, state.map_descriptor.size()));
    }
    SendLinkLobbyRawTransportPacket(state, target_socket, packet.data(),
        packet.size());
}

void SendLinkLobbyMapChunkPacket(LinkLobbyState& state, SOCKET target_socket,
    const void* packet, std::size_t byte_count) {
    if (packet == nullptr || byte_count < 0x0c) {
        return;
    }
    std::vector<u8> payload(static_cast<const u8*>(packet),
        static_cast<const u8*>(packet) + byte_count);
    write_le32(payload, 0, kLinkLobbyTransportPacketType);
    write_le32(payload, 4, kLinkLobbyMapChunkOpcode);
    write_le32(payload, 8, static_cast<u32>(payload.size()));
    SendLinkLobbyRawTransportPacket(state, target_socket, payload.data(),
        payload.size());
}

void SendLinkLobbyMapProgressPacket(LinkLobbyState& state, u32 player_index,
    u32 progress) {
    send_link_lobby_two_value_packet(state, kLinkLobbyMapProgressOpcode,
        player_index, progress);
}

void SendLinkLobbyMapRequestPacket(LinkLobbyState& state, u32 player_index,
    u32 requested_offset) {
    send_link_lobby_two_value_packet(state, kLinkLobbyMapRequestOpcode,
        player_index, requested_offset, state.shared_peer_socket);
}

bool SendLinkLobbyRequestedMapChunk(LinkLobbyState& state, int player_index,
    u32 requested_offset) {
    if (!player_index_valid(player_index)) {
        return false;
    }

    std::string source_path = state.prepared_map_path;
    if (source_path.empty()) {
        const std::string map_name = basename_from_path(state.map_file_name);
        if (!map_name.empty()) {
            const std::string installed_path =
                join_path(maps_directory_path(), map_name.c_str());
            WIN32_FILE_ATTRIBUTE_DATA data{};
            source_path = GetFileAttributesExA(installed_path.c_str(),
                GetFileExInfoStandard, &data) ? installed_path :
                join_path(download_directory_path(), map_name.c_str());
        }
    }
    if (source_path.empty()) {
        return false;
    }

    u32 chunk_limit = (state.mode >= 0 && state.mode < 3) ? 0x400u : 0x1000u;
    if (state.expected_map_file_size != 0) {
        if (requested_offset >= state.expected_map_file_size) {
            return false;
        }
        chunk_limit = std::min(chunk_limit,
            state.expected_map_file_size - requested_offset);
    }

    FILE* file = std::fopen(source_path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    if (std::fseek(file, static_cast<long>(requested_offset), SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }

    std::vector<u8> packet(0x14 + chunk_limit, 0);
    const std::size_t read_count =
        std::fread(packet.data() + 0x14, 1, chunk_limit, file);
    std::fclose(file);
    if (read_count == 0) {
        return false;
    }

    packet.resize(0x14 + read_count);
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyMapChunkOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, requested_offset);
    write_le32(packet, 0x10, static_cast<u32>(read_count));

    SOCKET target_socket = state.player_sockets[player_index];
    if (target_socket == INVALID_SOCKET || target_socket == 0) {
        target_socket = state.shared_peer_socket;
    }
    SendLinkLobbyMapChunkPacket(state, target_socket, packet.data(), packet.size());
    return true;
}

void SendLinkLobbyPacketToAll(LinkLobbyState& state, const void* packet,
    std::size_t byte_count) {
    SendLinkLobbyTransportPacket(state, INVALID_SOCKET, packet, byte_count);
}

void SendLinkLobbyReservedPacket0x14(LinkLobbyState& state, u32 value) {
    send_link_lobby_one_value_packet(state, kLinkLobbyReservedOneValueOpcode0x14,
        value);
}

void SendLinkLobbyReservedSelectionPacket0x05(LinkLobbyState& state, u32 selection,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyReservedSelectionOpcode0x05,
        selection, target_socket);
}

void SendLinkLobbyReservedSelectionPacket0x06(LinkLobbyState& state, u32 selection,
    SOCKET target_socket) {
    send_link_lobby_one_value_packet(state, kLinkLobbyReservedSelectionOpcode0x06,
        selection, target_socket);
}

void SendLinkLobbyReservedPairPacket0x1a(LinkLobbyState& state, u32 first,
    u32 second) {
    send_link_lobby_two_value_packet(state, kLinkLobbyReservedPairOpcode0x1a,
        first, second);
}

void SendLinkLobbyReservedPairPacket0x1b(LinkLobbyState& state, u32 first,
    u32 second) {
    send_link_lobby_two_value_packet(state, kLinkLobbyReservedPairOpcode0x1b,
        first, second);
}

void SendLinkLobbyReservedOneValuePacket0x1c(LinkLobbyState& state, u32 value) {
    send_link_lobby_one_value_packet(state, kLinkLobbyReservedOneValueOpcode0x1c,
        value);
}

bool SendLinkLobbyStartParametersPacket(LinkLobbyState& state) {
    PrepareLinkLobbyStartParameters(state);
    if (state.mode == 6) {
        return true;
    }

    std::vector<u8> packet = state.start_parameter_payload;
    if (packet.size() < kLinkLobbyStartParameterPacketBytes) {
        packet.assign(kLinkLobbyStartParameterPacketBytes, 0);
    }
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyStartParametersOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    return SendLinkLobbyTransportPacket(state, INVALID_SOCKET, packet.data(), packet.size());
}

void SendLinkLobbyStartTimeoutPacket(LinkLobbyState& state, u32 player_index) {
    send_link_lobby_one_value_packet(state, kLinkLobbyStartTimeoutOpcode,
        player_index);
}

void SendLinkLobbyPlayerPresencePacket(LinkLobbyState& state, SOCKET target_socket,
    u32 player_index) {
    if (player_index >= kLinkLobbyAvatarCount) {
        return;
    }
    std::vector<u8> packet(0x1ae, 0);
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyPlayerPresenceOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, player_index);
    std::memcpy(packet.data() + 0x10,
        state.players[player_index].raw_payload.data(), 0x19e);
    SendLinkLobbyRawTransportPacket(state, target_socket, packet.data(),
        packet.size());
}

void SendLinkLobbySessionSeedPacket(LinkLobbyState& state, SOCKET target_socket,
    u32 assigned_player_index) {
    std::vector<u8> packet(state.session_seed_payload.begin(),
        state.session_seed_payload.end());
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbySessionSeedOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, kLinkLobbySessionSeedLocalPlayerOffset, assigned_player_index);
    SendLinkLobbyRawTransportPacket(state, target_socket, packet.data(),
        packet.size());
}

void DispatchLinkLobbyRelayPacket(LinkLobbyState& state, SOCKET sender_socket,
    const void* packet, std::size_t byte_count) {
    if (packet == nullptr || byte_count < kLinkLobbyRelayJoinPacketBytes) {
        SendLinkLobbyStartResultPacket(state, sender_socket, 0, 5);
        return;
    }
    if (state.map_download_received_bytes != 0xffffffffu ||
        !state.resources_ready) {
        SendLinkLobbyStartResultPacket(state, sender_socket, 0, 5);
        return;
    }

    const u32 magic = packet_u32(packet, byte_count, 0x10);
    if (magic != kLinkLobbyHandshakeMagic) {
        SendLinkLobbyStartResultPacket(state, sender_socket, 0, 6);
        return;
    }
    const u32 local_version = LoadTrcRecord9Value();
    const u32 remote_version = packet_u32(packet, byte_count, 0x14);
    if (remote_version != local_version) {
        SendLinkLobbyStartResultPacket(state, sender_socket, local_version, 7);
        return;
    }
    const u32 remote_mode = packet_u32(packet, byte_count, 0x18);
    if (remote_mode != active_link_lobby_connection_mode(state)) {
        SendLinkLobbyStartResultPacket(state, sender_socket, 0, 8);
        return;
    }
    const char* remote_password =
        reinterpret_cast<const char*>(static_cast<const u8*>(packet) + 0x3c);
    if (state.password[0] != '\0' &&
        std::strncmp(remote_password, state.password.data(), 10) != 0) {
        SendLinkLobbyStartResultPacket(state, sender_socket, 0, 9);
        return;
    }

    const int slot = FindOpenLinkLobbyPlayerRoleSlot(state);
    if (slot < 0) {
        SendLinkLobbyStartResultPacket(state, sender_socket, 0, 3);
        return;
    }

    SendLinkLobbyMapDescriptorPacket(state, sender_socket, state.map_descriptor.data(),
        state.map_descriptor.size());
    const int max_players = std::clamp(
        static_cast<int>(link_lobby_seed_max_players(state)), 0,
        kLinkLobbyAvatarCount);
    for (int i = 0; i < max_players; ++i) {
        SendLinkLobbyPlayerPresencePacket(state, sender_socket, static_cast<u32>(i));
    }
    SendLinkLobbySessionSeedPacket(state, sender_socket, static_cast<u32>(slot));
}

void SendLinkLobbyCurrentRoleStatePackets(LinkLobbyState& state) {
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        switch (state.player_role_values[i]) {
        case 0:
            SendLinkLobbyPlayerPresencePacket(state, INVALID_SOCKET, static_cast<u32>(i));
            break;
        case 1:
            SendLinkLobbyOpenRolePacket(state, i);
            break;
        case 2:
            SendLinkLobbyComputerRolePacket(state, i);
            break;
        case 3:
            SendLinkLobbyClosedRolePacket(state, i);
            break;
        default:
            break;
        }
        SendLinkLobbyTribeSelectionPacket(state, static_cast<u32>(i),
            state.tribe_choices[i]);
    }
}

bool SendLinkLobbyPeerRouteTablePacket(LinkLobbyState& state) {
    std::vector<u8> packet(0x14c, 0);
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyPeerRouteSyncOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        std::memcpy(packet.data() + 0x0c + i * 0x10,
            state.primary_peer_hosts[i].data(), 0x10);
        write_le32(packet, 0x8c + i * 4, state.primary_peer_ports[i]);
        std::memcpy(packet.data() + 0xac + i * 0x10,
            state.secondary_peer_hosts[i].data(), 0x10);
        write_le32(packet, 0x12c + i * 4, state.secondary_peer_ports[i]);
    }
    return SendLinkLobbyTransportPacket(state, INVALID_SOCKET, packet.data(),
        packet.size());
}

void SendLinkLobbyPeerRoutePacket(LinkLobbyState& state, u32 player_index,
    const char* primary_host, u16 primary_port, const char* secondary_host,
    u16 secondary_port) {
    std::array<u8, 0x38> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyPeerRouteOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, player_index);
    if (primary_host != nullptr) {
        std::strncpy(reinterpret_cast<char*>(packet.data() + 0x10), primary_host,
            0x0f);
    }
    write_le32(packet, 0x20, primary_port);
    if (secondary_host != nullptr) {
        std::strncpy(reinterpret_cast<char*>(packet.data() + 0x24), secondary_host,
            0x0f);
    }
    write_le32(packet, 0x34, secondary_port);
    SendLinkLobbyTransportPacket(state, state.shared_peer_socket, packet.data(),
        packet.size());
}

void SendLinkLobbyUdpProbeRequestPacket(LinkLobbyState& state, u32 player_index,
    u32 target_player, SOCKET target_socket) {
    send_link_lobby_two_value_packet(state, kLinkLobbyUdpProbeRequestOpcode,
        player_index, target_player, target_socket);
}

void SendLinkLobbyUdpProbeDatagram(LinkLobbyState&, u32 player_index,
    const char* host, u16 port) {
    if (host == nullptr || *host == '\0') {
        return;
    }
    std::array<u8, 0x24> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyUdpProbeReplyOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    write_le32(packet, 0x0c, player_index);
    std::strncpy(reinterpret_cast<char*>(packet.data() + 0x10), host, 0x0f);
    write_le32(packet, 0x20, port);
    sockaddr_in target_address = BuildLegacyUdpSockaddr(host, port);
    SendLegacyUdpChunks(static_cast<u32>(packet.size()), packet.data(), target_address);
}

void SendLinkLobbySecondaryStartAckPacket(LinkLobbyState& state, u32 player_index) {
    send_link_lobby_two_value_packet(state, kLinkLobbySecondaryStartAckOpcode,
        static_cast<u32>(state.local_player_index), player_index,
        state.shared_peer_socket);
}

void SendLinkLobbyStopPeerRouteTimerPacket(LinkLobbyState& state) {
    std::array<u8, 0x0c> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyStopPeerRouteTimerOpcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    SendLinkLobbyTransportPacket(state, INVALID_SOCKET, packet.data(), packet.size());
}

#define DEFINE_LINK_TAB_BUTTON_CONTROL(N) \
void InitializeLinkLobbyTabButtonControl##N(LinkLobbyState& state) { \
    initialize_link_lobby_tab_button_control(state, N); \
} \
void RegisterLinkLobbyTabButtonDestructor##N(LinkLobbyState&) { \
    register_link_lobby_indexed_shutdown( \
        g_tab_button_shutdown_registered, N, shutdown_global_tab_button<N>); \
}

#define DEFINE_LINK_TAB_BUTTON_DESTROY(N) \
void DestroyLinkLobbyTabButtonControl##N(LinkLobbyState& state) { \
    destroy_link_lobby_tab_button_control(state, N); \
}

DEFINE_LINK_TAB_BUTTON_CONTROL(0)
DEFINE_LINK_TAB_BUTTON_DESTROY(0)

void InitializeLinkLobbyTabButton1(LinkLobbyState& state) {
    InitializeLinkLobbyTabButtonControl1(state);
    RegisterLinkLobbyTabButtonDestructor1(state);
}

DEFINE_LINK_TAB_BUTTON_CONTROL(1)
DEFINE_LINK_TAB_BUTTON_DESTROY(1)

void InitializeLinkLobbyTabButton2(LinkLobbyState& state) {
    InitializeLinkLobbyTabButtonControl2(state);
    RegisterLinkLobbyTabButtonDestructor2(state);
}

DEFINE_LINK_TAB_BUTTON_CONTROL(2)
DEFINE_LINK_TAB_BUTTON_DESTROY(2)

void InitializeLinkLobbyTabButton3(LinkLobbyState& state) {
    InitializeLinkLobbyTabButtonControl3(state);
    RegisterLinkLobbyTabButtonDestructor3(state);
}

DEFINE_LINK_TAB_BUTTON_CONTROL(3)

#undef DEFINE_LINK_TAB_BUTTON_DESTROY
#undef DEFINE_LINK_TAB_BUTTON_CONTROL

void InitializeLinkLobbyTabButtons(LinkLobbyState& state) {
    state.tab_button_count = kLinkLobbyTabButtonCount;
    state.tab_button_positions = {430, 484, 538, 592};
    InitializeLinkLobbyTabButtonControl0(state);
    RegisterLinkLobbyTabButtonDestructor0(state);
    InitializeLinkLobbyTabButton1(state);
    InitializeLinkLobbyTabButton2(state);
    InitializeLinkLobbyTabButton3(state);
}

void DestroyLinkLobbyTabButtons(LinkLobbyState& state) {
    DestroyLinkLobbyTabButtonControl0(state);
    DestroyLinkLobbyTabButtonControl1(state);
    DestroyLinkLobbyTabButtonControl2(state);
    DestroyLinkLobbyTabButtonControl3(state);
}

bool CreateLinkLobbyTabButton(LinkLobbyState& state, int tab_index, int position) {
    if (tab_index < 0 || tab_index >= kLinkLobbyTabButtonCount ||
        state.window == nullptr) {
        return false;
    }
    LegacyImageButtonControl& button = state.tab_buttons[tab_index];
    int x = position;
    int y = 0x24;
    int width = 0x54;
    int height = 0x18;
    if (link_lobby_session_seed_present(state)) {
        const LinkLobbyLayoutRect rect = layout_at(state, 11);
        x = rect.x;
        y = position;
        width = rect.width;
        height = rect.height;
    }
    if (!CreateLegacyImageButtonWindow(button, state.window,
            state.tab_button_labels[tab_index].data(),
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                kLinkLobbyTabFirstId + tab_index)),
            x, y, width, height)) {
        return false;
    }
    subclass_button(button);
    LoadLegacyImageButtonBitmaps(button, kLinkLobbyPanelBitmapRecord, 0);
    SendMessageA(button.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(link_lobby_ui_font()), TRUE);
    SetWindowTextA(button.window, state.tab_button_labels[tab_index].data());
    return true;
}

bool CreateLinkLobbyTabButtons(LinkLobbyState& state) {
    for (int i = 0; i < std::min(state.tab_button_count,
             kLinkLobbyTabButtonCount); ++i) {
        if (!CreateLinkLobbyTabButton(state, i, state.tab_button_positions[i])) {
            return false;
        }
    }
    return true;
}

void DrawLinkLobbyTabButton(LinkLobbyState& state, int tab_index,
    const DRAWITEMSTRUCT& draw) {
    if (tab_index < 0 || tab_index >= kLinkLobbyTabButtonCount) {
        return;
    }
    const LegacyImageButtonControl& button = state.tab_buttons[tab_index];
    StretchBitmapMemoryResourceToDc(button.normal_bitmap, draw.hDC, 0, 0);

    std::array<char, 0x80> text{};
    if (draw.hwndItem != nullptr) {
        GetWindowTextA(draw.hwndItem, text.data(), static_cast<int>(text.size()));
    }
    if (text[0] == '\0') {
        copy_c_string(text, state.tab_button_labels[tab_index].data());
    }

    RECT rect = draw.rcItem;
    rect.left += 0x0e;
    rect.top += 0x0e;
    rect.right -= 0x0e;
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, state.tab_text_colors[tab_index]);
    DrawTextA(draw.hDC, text.data(), -1, &rect, kLinkLobbyTabTextFlags);
}

void NoOpLinkLobbyUnusedOwnerDrawControl(LinkLobbyState&, const DRAWITEMSTRUCT&) {
}

#define DEFINE_LINK_LATENCY_BUTTON_CONTROL(N) \
void InitializeLinkLobbyLatencyButtonControl##N(LinkLobbyState& state) { \
    initialize_link_lobby_latency_button_control(state, N); \
} \
void RegisterLinkLobbyLatencyButtonDestructor##N(LinkLobbyState&) { \
    register_link_lobby_indexed_shutdown(g_latency_button_shutdown_registered, \
        N, shutdown_global_latency_button<N>); \
}

#define DEFINE_LINK_LATENCY_BUTTON_DESTROY(N) \
void DestroyLinkLobbyLatencyButtonControl##N(LinkLobbyState& state) { \
    destroy_link_lobby_latency_button_control(state, N); \
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(0)
DEFINE_LINK_LATENCY_BUTTON_DESTROY(0)

void InitializeLinkLobbyLatencyButton1(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl1(state);
    RegisterLinkLobbyLatencyButtonDestructor1(state);
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(1)
DEFINE_LINK_LATENCY_BUTTON_DESTROY(1)

void InitializeLinkLobbyLatencyButton2(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl2(state);
    RegisterLinkLobbyLatencyButtonDestructor2(state);
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(2)
DEFINE_LINK_LATENCY_BUTTON_DESTROY(2)

void InitializeLinkLobbyLatencyButton3(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl3(state);
    RegisterLinkLobbyLatencyButtonDestructor3(state);
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(3)
DEFINE_LINK_LATENCY_BUTTON_DESTROY(3)

void InitializeLinkLobbyLatencyButton4(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl4(state);
    RegisterLinkLobbyLatencyButtonDestructor4(state);
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(4)
DEFINE_LINK_LATENCY_BUTTON_DESTROY(4)

void InitializeLinkLobbyLatencyButton5(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl5(state);
    RegisterLinkLobbyLatencyButtonDestructor5(state);
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(5)
DEFINE_LINK_LATENCY_BUTTON_DESTROY(5)

void InitializeLinkLobbyLatencyButton6(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl6(state);
    RegisterLinkLobbyLatencyButtonDestructor6(state);
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(6)
DEFINE_LINK_LATENCY_BUTTON_DESTROY(6)

void InitializeLinkLobbyLatencyButton7(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl7(state);
    RegisterLinkLobbyLatencyButtonDestructor7(state);
}

DEFINE_LINK_LATENCY_BUTTON_CONTROL(7)

#undef DEFINE_LINK_LATENCY_BUTTON_DESTROY
#undef DEFINE_LINK_LATENCY_BUTTON_CONTROL

void InitializeLinkLobbyLatencyButtons(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl0(state);
    RegisterLinkLobbyLatencyButtonDestructor0(state);
    InitializeLinkLobbyLatencyButton1(state);
    InitializeLinkLobbyLatencyButton2(state);
    InitializeLinkLobbyLatencyButton3(state);
    InitializeLinkLobbyLatencyButton4(state);
    InitializeLinkLobbyLatencyButton5(state);
    InitializeLinkLobbyLatencyButton6(state);
    InitializeLinkLobbyLatencyButton7(state);
}

void DestroyLinkLobbyLatencyButtons(LinkLobbyState& state) {
    DestroyLinkLobbyLatencyButtonControl0(state);
    DestroyLinkLobbyLatencyButtonControl1(state);
    DestroyLinkLobbyLatencyButtonControl2(state);
    DestroyLinkLobbyLatencyButtonControl3(state);
    DestroyLinkLobbyLatencyButtonControl4(state);
    DestroyLinkLobbyLatencyButtonControl5(state);
    DestroyLinkLobbyLatencyButtonControl6(state);
    DestroyLinkLobbyLatencyButtonControl7(state);
}

#define DEFINE_LINK_LATENCY_BITMAP_RESOURCE(N) \
void InitializeLinkLobbyLatencyBitmapResource##N(LinkLobbyState& state) { \
    initialize_link_lobby_latency_bitmap_resource(state, N); \
} \
void RegisterLinkLobbyLatencyBitmapDestructor##N(LinkLobbyState&) { \
    register_link_lobby_indexed_shutdown(g_latency_bitmap_shutdown_registered, \
        N, shutdown_global_latency_bitmap<N>); \
}

#define DEFINE_LINK_LATENCY_BITMAP_DESTROY(N) \
void DestroyLinkLobbyLatencyBitmapResource##N(LinkLobbyState& state) { \
    destroy_link_lobby_latency_bitmap_resource(state, N); \
}

DEFINE_LINK_LATENCY_BITMAP_RESOURCE(0)
DEFINE_LINK_LATENCY_BITMAP_DESTROY(0)

void InitializeLinkLobbyLatencyBitmap1(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyBitmapResource1(state);
    RegisterLinkLobbyLatencyBitmapDestructor1(state);
}

DEFINE_LINK_LATENCY_BITMAP_RESOURCE(1)
DEFINE_LINK_LATENCY_BITMAP_DESTROY(1)

void InitializeLinkLobbyLatencyBitmap2(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyBitmapResource2(state);
    RegisterLinkLobbyLatencyBitmapDestructor2(state);
}

DEFINE_LINK_LATENCY_BITMAP_RESOURCE(2)
DEFINE_LINK_LATENCY_BITMAP_DESTROY(2)

void InitializeLinkLobbyLatencyBitmap3(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyBitmapResource3(state);
    RegisterLinkLobbyLatencyBitmapDestructor3(state);
}

DEFINE_LINK_LATENCY_BITMAP_RESOURCE(3)
DEFINE_LINK_LATENCY_BITMAP_DESTROY(3)

void InitializeLinkLobbyLatencyBitmap4(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyBitmapResource4(state);
    RegisterLinkLobbyLatencyBitmapDestructor4(state);
}

DEFINE_LINK_LATENCY_BITMAP_RESOURCE(4)
DEFINE_LINK_LATENCY_BITMAP_DESTROY(4)

void InitializeLinkLobbyLatencyBitmap5(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyBitmapResource5(state);
    RegisterLinkLobbyLatencyBitmapDestructor5(state);
}

DEFINE_LINK_LATENCY_BITMAP_RESOURCE(5)

#undef DEFINE_LINK_LATENCY_BITMAP_DESTROY
#undef DEFINE_LINK_LATENCY_BITMAP_RESOURCE

void InitializeLinkLobbyLatencyBitmaps(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyBitmapResource0(state);
    RegisterLinkLobbyLatencyBitmapDestructor0(state);
    InitializeLinkLobbyLatencyBitmap1(state);
    InitializeLinkLobbyLatencyBitmap2(state);
    InitializeLinkLobbyLatencyBitmap3(state);
    InitializeLinkLobbyLatencyBitmap4(state);
    InitializeLinkLobbyLatencyBitmap5(state);
}

void LoadLinkLobbyLatencyBitmaps(LinkLobbyState& state) {
    for (int i = 0; i < kLinkLobbyLatencyBitmapCount; ++i) {
        LoadBitmapMemoryResourceFromTrcRecord(state.latency_bitmaps[i],
            "Jw2_19.trc", kLinkLobbyLatencyBitmapRecords[i]);
    }
}

void ReleaseLinkLobbyLatencyBitmaps(LinkLobbyState& state) {
    DestroyLinkLobbyLatencyBitmapResource0(state);
    DestroyLinkLobbyLatencyBitmapResource1(state);
    DestroyLinkLobbyLatencyBitmapResource2(state);
    DestroyLinkLobbyLatencyBitmapResource3(state);
    DestroyLinkLobbyLatencyBitmapResource4(state);
    DestroyLinkLobbyLatencyBitmapResource5(state);
}

bool CreateLinkLobbyLatencyButton(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index) || state.window == nullptr) {
        return false;
    }
    LegacyImageButtonControl& button = state.latency_buttons[player_index];
    InitializeLegacyImageButtonControl(button);
    button.parent = state.window;
    button.x = 0x110;
    button.y = state.player_row_y[player_index] + 2;
    button.width = 0x1d;
    button.height = 0x14;
    button.window = CreateWindowExA(0, "button", "Latency",
        kHiddenOwnerDrawButtonStyle, button.x, button.y, button.width, button.height,
        state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(
            kLinkLobbyLatencyFirstId + player_index)),
        state.instance, nullptr);
    if (button.window == nullptr) {
        return false;
    }
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(button.window, GWLP_WNDPROC));
    subclass_button(button);
    ShowWindow(button.window, SW_HIDE);
    return true;
}

void ShowLinkLobbyLatencyButton(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    HWND window = state.latency_buttons[player_index].window;
    if (window == nullptr) {
        return;
    }
    ShowWindow(window, SW_SHOW);
}

void HideLinkLobbyLatencyButton(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    HWND window = state.latency_buttons[player_index].window;
    if (window == nullptr) {
        return;
    }
    ShowWindow(window, SW_HIDE);
}

void UpdateLinkLobbyLatencyButtonVisibility(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    const int role = state.player_role_values[player_index];
    if (role == 0) {
        ShowLinkLobbyLatencyButton(state, player_index);
    }
    else if (role > 0 && role < 4) {
        HideLinkLobbyLatencyButton(state, player_index);
    }
}

void DrawLinkLobbyLatencyButton(LinkLobbyState& state, int player_index,
    const DRAWITEMSTRUCT& draw) {
    if (!player_index_valid(player_index)) {
        return;
    }
    const int bitmap_index = std::clamp(state.latency_values[player_index], 0,
        kLinkLobbyLatencyBitmapCount - 1);
    StretchBitmapMemoryResourceToDc(state.latency_bitmaps[bitmap_index],
        draw.hDC, 0, 0);
}

#define DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(N) \
void InitializeLinkLobbyMapDownloadButtonControl##N(LinkLobbyState& state) { \
    initialize_link_lobby_map_download_button_control(state, N); \
} \
void RegisterLinkLobbyMapDownloadButtonDestructor##N(LinkLobbyState&) { \
    register_link_lobby_indexed_shutdown( \
        g_map_download_button_shutdown_registered, N, \
        shutdown_global_map_download_button<N>); \
}

#define DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(N) \
void DestroyLinkLobbyMapDownloadButtonControl##N(LinkLobbyState& state) { \
    destroy_link_lobby_map_download_button_control(state, N); \
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(0)
DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(0)

void InitializeLinkLobbyMapDownloadButton1(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl1(state);
    RegisterLinkLobbyMapDownloadButtonDestructor1(state);
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(1)
DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(1)

void InitializeLinkLobbyMapDownloadButton2(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl2(state);
    RegisterLinkLobbyMapDownloadButtonDestructor2(state);
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(2)
DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(2)

void InitializeLinkLobbyMapDownloadButton3(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl3(state);
    RegisterLinkLobbyMapDownloadButtonDestructor3(state);
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(3)
DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(3)

void InitializeLinkLobbyMapDownloadButton4(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl4(state);
    RegisterLinkLobbyMapDownloadButtonDestructor4(state);
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(4)
DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(4)

void InitializeLinkLobbyMapDownloadButton5(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl5(state);
    RegisterLinkLobbyMapDownloadButtonDestructor5(state);
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(5)
DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(5)

void InitializeLinkLobbyMapDownloadButton6(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl6(state);
    RegisterLinkLobbyMapDownloadButtonDestructor6(state);
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(6)
DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY(6)

void InitializeLinkLobbyMapDownloadButton7(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl7(state);
    RegisterLinkLobbyMapDownloadButtonDestructor7(state);
}

DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL(7)

#undef DEFINE_LINK_MAP_DOWNLOAD_BUTTON_DESTROY
#undef DEFINE_LINK_MAP_DOWNLOAD_BUTTON_CONTROL

void InitializeLinkLobbyMapDownloadButtons(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl0(state);
    RegisterLinkLobbyMapDownloadButtonDestructor0(state);
    InitializeLinkLobbyMapDownloadButton1(state);
    InitializeLinkLobbyMapDownloadButton2(state);
    InitializeLinkLobbyMapDownloadButton3(state);
    InitializeLinkLobbyMapDownloadButton4(state);
    InitializeLinkLobbyMapDownloadButton5(state);
    InitializeLinkLobbyMapDownloadButton6(state);
    InitializeLinkLobbyMapDownloadButton7(state);
}

void DestroyLinkLobbyMapDownloadButtons(LinkLobbyState& state) {
    DestroyLinkLobbyMapDownloadButtonControl0(state);
    DestroyLinkLobbyMapDownloadButtonControl1(state);
    DestroyLinkLobbyMapDownloadButtonControl2(state);
    DestroyLinkLobbyMapDownloadButtonControl3(state);
    DestroyLinkLobbyMapDownloadButtonControl4(state);
    DestroyLinkLobbyMapDownloadButtonControl5(state);
    DestroyLinkLobbyMapDownloadButtonControl6(state);
    DestroyLinkLobbyMapDownloadButtonControl7(state);
}

bool CreateLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index) || state.window == nullptr) {
        return false;
    }
    state.map_download_progress[player_index] = 100;
    LegacyImageButtonControl& button = state.map_download_buttons[player_index];
    InitializeLegacyImageButtonControl(button);
    button.parent = state.window;
    button.x = 0x110;
    button.y = state.player_row_y[player_index];
    button.width = 0x14;
    button.height = 0x14;
    if (link_lobby_session_seed_present(state)) {
        const LinkLobbyLayoutRect role_rect = layout_at(state, 12);
        button.x = role_rect.x - 0x1e;
        button.height = role_rect.height;
    }
    button.window = CreateWindowExA(0, "button", "Map DownLoad",
        kHiddenOwnerDrawButtonStyle, button.x, button.y, button.width,
        button.height, state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(
            kLinkLobbyMapDownloadFirstId + player_index)),
        state.instance, nullptr);
    if (button.window == nullptr) {
        return false;
    }
    button.original_window_proc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrA(button.window, GWLP_WNDPROC));
    subclass_button(button);
    SendMessageA(button.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(link_lobby_ui_font()), TRUE);
    ShowWindow(button.window, SW_HIDE);
    return true;
}

void DrawLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index,
    const DRAWITEMSTRUCT& draw) {
    if (!player_index_valid(player_index)) {
        return;
    }
    if (state.map_download_progress[player_index] == 100) {
        ShowWindow(state.map_download_buttons[player_index].window, SW_HIDE);
        return;
    }
    ShowWindow(state.map_download_buttons[player_index].window, SW_SHOW);
    SetBkColor(draw.hDC, kLinkBlack);
    SetBkMode(draw.hDC, OPAQUE);
    SetTextColor(draw.hDC, kLinkSoftWhite);
    char text[32]{};
    std::snprintf(text, sizeof(text), "%d", state.map_download_progress[player_index]);
    RECT rect = draw.rcItem;
    DrawTextA(draw.hDC, text, -1, &rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void RedrawLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    HWND window = state.map_download_buttons[player_index].window;
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

void ShowLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    HWND window = state.map_download_buttons[player_index].window;
    if (window != nullptr) {
        ShowWindow(window, SW_SHOW);
    }
}

void HideLinkLobbyMapDownloadButton(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    HWND window = state.map_download_buttons[player_index].window;
    if (window != nullptr) {
        ShowWindow(window, SW_HIDE);
    }
}

void UpdateLinkLobbyMapDownloadButtonVisibility(LinkLobbyState& state,
    int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }
    const int role = state.player_role_values[player_index];
    if (role == 0) {
        if (state.map_download_progress[player_index] == 100) {
            HideLinkLobbyMapDownloadButton(state, player_index);
        } else {
            ShowLinkLobbyMapDownloadButton(state, player_index);
        }
    } else if (role > 0 && role < 4) {
        HideLinkLobbyMapDownloadButton(state, player_index);
    }
}

bool CheckLinkLobbyMapFileMatchesExpected(LinkLobbyState& state, const char* path) {
    if (path == nullptr || *path == '\0') {
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return false;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    if (state.expected_map_file_size != 0 &&
        size.QuadPart != state.expected_map_file_size) {
        return false;
    }
    if (state.expected_map_file_time_valid &&
        CompareFileTime(&data.ftLastWriteTime, &state.expected_map_file_time) != 0) {
        return false;
    }
    state.prepared_map_path = path;
    return true;
}

bool PrepareLinkLobbyMapDownload(LinkLobbyState& state) {
    if (state.map_download_state == 1) {
        return true;
    }

    const std::string map_name = basename_from_path(state.map_file_name);
    if (map_name.empty()) {
        state.map_download_state = 0;
        return false;
    }
    const int local_player = player_index_valid(state.local_player_index) ?
        state.local_player_index : 0;

    const std::string installed_path = join_path(maps_directory_path(), map_name.c_str());
    if (CheckLinkLobbyMapFileMatchesExpected(state, installed_path.c_str())) {
        state.map_download_state = 1;
        state.map_download_received_bytes = 0xffffffffu;
        state.last_map_download_progress_value = -1;
        state.map_download_progress[local_player] = 100;
        if (!state.host_mode) {
            SendLinkLobbyMapRequestPacket(state, static_cast<u32>(local_player),
                0xffffffffu);
        }
        if (state.callbacks.send_map_download_progress == nullptr ||
            state.callbacks.send_map_download_progress(state, local_player, -1)) {
            RedrawWindow(state.game_info_button.window, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW);
        }
        return true;
    }

    CreateDirectoryA(maps_directory_path().c_str(), nullptr);
    CreateDirectoryA(download_directory_path().c_str(), nullptr);
    const std::string download_path = join_path(download_directory_path(), map_name.c_str());
    if (CheckLinkLobbyMapFileMatchesExpected(state, download_path.c_str())) {
        state.map_download_state = 1;
        state.map_download_received_bytes = 0xffffffffu;
        state.last_map_download_progress_value = -1;
        state.map_download_progress[local_player] = 100;
        if (!state.host_mode) {
            SendLinkLobbyMapRequestPacket(state, static_cast<u32>(local_player),
                0xffffffffu);
        }
        return true;
    }

    DeleteFileA(download_path.c_str());
    state.prepared_map_path = download_path;
    state.map_download_state = 2;
    state.map_download_received_bytes = 0;
    state.last_map_download_progress_value = 0;
    state.map_download_progress[local_player] = 0;
    HWND progress_window = state.map_download_buttons[local_player].window;
    if (progress_window != nullptr) {
        ShowWindow(progress_window, SW_SHOW);
        RedrawWindow(progress_window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    if (state.callbacks.send_map_download_progress != nullptr) {
        state.callbacks.send_map_download_progress(state, local_player, 0);
    }
    SendLinkLobbyMapRequestPacket(state, static_cast<u32>(local_player),
        state.map_download_received_bytes);
    return true;
}

bool ReportLinkLobbyMapDownloadWaiters(LinkLobbyState& state) {
    std::string message = startup_message_row(24, "Player ");
    int waiting = 0;
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (!state.player_socket_connected[i] || state.map_download_progress[i] == 100) {
            continue;
        }
        if (waiting != 0) {
            message += ", ";
        }
        const char* name = state.players[i].name[0] == '\0' ? "player" :
            state.players[i].name.data();
        message += name;
        ++waiting;
    }
    if (waiting != 0) {
        message += startup_message_row(27, " is downloading the game map.");
        show_message(state, message.c_str(), kLinkMapWaiterYellow);
    }
    return waiting != 0;
}

void InitializeLinkLobbySocketCriticalSection(LinkLobbyState& state) {
    if (!state.socket_critical_section_initialized) {
        InitializeCriticalSection(&state.socket_critical_section);
        state.socket_critical_section_initialized = true;
    }
    ConfigureDirectPlayMode6DispatchLock(&state.socket_critical_section);
}

void DeleteLinkLobbySocketCriticalSection(LinkLobbyState& state) {
    if (state.socket_critical_section_initialized) {
        ConfigureDirectPlayMode6DispatchLock(nullptr);
        DeleteCriticalSection(&state.socket_critical_section);
        state.socket_critical_section_initialized = false;
    }
}

bool InitializeLinkLobbyNetworkRoute(LinkLobbyState& state) {
    InitializeLinkLobbySocketCriticalSection(state);
    if (state.callbacks.resume_connect_modal != nullptr) {
        state.callbacks.resume_connect_modal(state);
    }
    if (state.mode < 0 || state.mode > 2) {
        return true;
    }

    if (state.default_udp_port != 0) {
        char host_name[0x100]{};
        char local_address[0x100]{};
        if (!ResolveLocalHostDisplayAddress(host_name, sizeof(host_name),
                local_address, sizeof(local_address))) {
            show_startup_message(state, 25, " UDP initialization error.",
                RGB(255, 10, 10));
            return false;
        }

        SOCKET udp_socket = StartLegacyUdpSocket(local_address,
            state.default_udp_port);
        if (udp_socket == INVALID_SOCKET) {
            const int requested_port_error = WSAGetLastError();
            udp_socket = StartLegacyUdpSocket(local_address, 0);
            append_link_lobby_log(
                "link udp requested port unavailable requested=%u error=%ld fallback=%s",
                static_cast<unsigned>(state.default_udp_port),
                static_cast<long>(requested_port_error),
                udp_socket != INVALID_SOCKET ? "ephemeral" : "failed");
        }
        if (udp_socket == INVALID_SOCKET) {
            show_startup_message(state, 25, " UDP initialization error.",
                RGB(255, 10, 10));
            return false;
        }

        sockaddr_in probed_address{};
        bool probed_address_valid = false;

        if (state.default_peer_probe_host[0] != '\0' &&
            state.default_peer_probe_port != 0) {
            probed_address_valid = ProbeLinkLobbyUdpPeerAddress(state,
                    state.default_peer_probe_host.data(),
                    state.default_peer_probe_port, probed_address);
            if (probed_address_valid &&
                IsPrivateIpv4Address(probed_address.sin_addr)) {
                show_startup_message(state, 98,
                    "Your UDP connection is using a private IP address that "
                    "cannot be translated to a real address.",
                    RGB(255, 10, 10));
                return false;
            }
        }
        register_local_udp_route(state,
            probed_address_valid ? &probed_address : nullptr);
    }

    if (state.host_mode) {
        if (state.default_tcp_port == 0 ||
            !StartLegacyListenSocket(state.default_tcp_port, state.window,
                kLinkLobbyOwnerSyncMessage)) {
            show_startup_message(state, 25, " UDP initialization error.",
                RGB(255, 10, 10));
            return false;
        }
        // Original FUN_0046e030 stores DAT_014b9a58 (the TCP listen socket)
        // in the local 0x19e-byte player record at +0x18a before that record
        // is published.  Peers use nonzero +0x18a as the active-player gate
        // for the reciprocal UDP probe loop, even though the handle value is
        // meaningful only in the process that created it.
        set_local_player_transport_handle(state,
            legacy_network_state().listen_socket);
    }
    return true;
}

void ShutdownLinkLobbyNetworkRoute(LinkLobbyState& state) {
    if (state.callbacks.shutdown_network != nullptr) {
        state.callbacks.shutdown_network(state);
    }
    else {
        CloseAllLegacySocketRecords();
    }
    if (state.map_download_state == 2 && !state.prepared_map_path.empty()) {
        DeleteFileA(state.prepared_map_path.c_str());
        state.map_download_state = 0;
    }
}

void HandleLinkLobbyListenSocketEvent(LinkLobbyState& state, WPARAM, LPARAM event) {
    if (LOWORD(event) == FD_ACCEPT) {
        AcceptLegacySocketConnection(state.window, kLinkLobbySocketMessage);
    }
}

void PumpLinkLobbySocketReceiveQueue(LinkLobbyState& state, SOCKET sender_socket,
    void* socket_record) {
    auto* record = static_cast<LegacySocketRecord*>(socket_record);
    if (record == nullptr) {
        return;
    }

    while (record->receive_queue.size() > 7) {
        const u8* bytes = record->receive_queue.data();
        const u32 packet_type = read_le32(bytes);
        if (packet_type == 0) {
            const std::size_t first_text_length = bytes[7];
            const std::size_t length_offset = first_text_length + 0x0b;
            if (record->receive_queue.size() <= length_offset) {
                break;
            }
            const std::size_t second_text_length = bytes[length_offset];
            const std::size_t packet_size =
                first_text_length + 0x0c + second_text_length;
            if (record->receive_queue.size() < packet_size) {
                break;
            }
            if (AsyncComContext* context = async_com_state().active_context;
                context != nullptr && context->system_message_101_seen) {
                BroadcastLinkLobbyTransportPacketExcept(state, bytes, packet_size,
                    sender_socket);
            }
            post_copied_window_payload(state.window, kLinkLobbyDirectPlayStartMessage,
                static_cast<WPARAM>(sender_socket), bytes, packet_size);
            ConsumeLegacySocketReceiveQueue(*record, static_cast<u32>(packet_size));
            continue;
        }

        if (packet_type == 2) {
            if (record->receive_queue.size() < 0x0c) {
                break;
            }
            const u32 packet_size = read_le32(bytes + 8);
            if (packet_size == 0 || record->receive_queue.size() < packet_size) {
                break;
            }
            post_copied_window_payload(state.window, kLinkLobbyCopiedPayloadMessage,
                static_cast<WPARAM>(sender_socket), bytes, packet_size);
            ConsumeLegacySocketReceiveQueue(*record, packet_size);
            continue;
        }
        break;
    }
}

void HandleLinkLobbyPeerSocketEvent(LinkLobbyState& state, WPARAM socket,
    LPARAM event) {
    const WORD network_event = LOWORD(event);
    switch (network_event) {
    case FD_READ:
        if (LegacySocketRecord* record =
                ReceiveIntoLegacySocketQueue(static_cast<SOCKET>(socket))) {
            PumpLinkLobbySocketReceiveQueue(state, static_cast<SOCKET>(socket),
                record);
        }
        break;
    case FD_WRITE:
        if (state.join_request_pending) {
            state.join_request_pending = false;
            send_local_join_request(state, static_cast<SOCKET>(socket));
        }
        break;
    case FD_CONNECT:
        RegisterLegacySocketRecord(static_cast<SOCKET>(socket));
        break;
    case FD_CLOSE:
        CloseLegacySocketRecord(static_cast<SOCKET>(socket));
        if (state.map_download_received_bytes == 0xffffffffu) {
            const AsyncComContext* context = async_com_state().active_context;
            const bool directplay_ready =
                context != nullptr && context->system_message_101_seen;
            if (!directplay_ready) {
                if (state.join_accepted) {
                    // The original client switches from the lobby TCP route to
                    // DirectPlay while the synchronized start countdown is in
                    // progress.  The reconstructed client uses its raw UDP
                    // fallback when the legacy DirectPlay COM runtime is not
                    // available.  A host therefore closes this TCP
                    // socket as part of a successful start, not as a lobby
                    // cancellation.  Keep the countdown alive once the start
                    // parameters have already been accepted.
                    if (state.countdown_value >= 0) {
                        append_link_lobby_log(
                            "link peer tcp close accepted during udp start countdown value=%ld",
                            static_cast<long>(state.countdown_value));
                        break;
                    }
                    if (state.start_sync_timer != 0 && state.window != nullptr) {
                        KillTimer(state.window, state.start_sync_timer);
                    }
                    state.start_sync_timer = 0;
                    show_startup_message(state, 30,
                        "The game host canceled the game.", kLinkMapFailureRed);
                    ReturnFromLinkLobby(state);
                }
            } else {
                const SOCKET closed_socket = static_cast<SOCKET>(socket);
                for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
                    if (state.player_sockets[i] == closed_socket) {
                        SendLinkLobbyPlayerDisconnectPacket(state, static_cast<u32>(i));
                        HandleLinkLobbyPlayerDisconnected(state, static_cast<u32>(i));
                        SendLinkLobbyOpenRolePacket(state, i);
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

void HandleLinkLobbyAsyncTcpSocketEvent(LinkLobbyState& state, WPARAM,
    LPARAM event) {
    const WORD network_event = LOWORD(event);
    if (network_event == FD_READ) {
        if (state.async_tcp_socket == nullptr) {
            return;
        }
        ReceiveLegacyAsyncTcpQueue(*state.async_tcp_socket);
        const u8* payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
        i32 byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
        while (payload != nullptr && byte_count >= 4) {
            const u32 packet_type = read_le32(payload);
            if (packet_type == 0) {
                if (byte_count < 8) {
                    break;
                }
                const u32 first_text_length = payload[7];
                const u32 second_length_offset = first_text_length + 0x0b;
                if (static_cast<u32>(byte_count) <= second_length_offset) {
                    break;
                }
                const u32 second_text_length = payload[second_length_offset];
                const u32 packet_count =
                    first_text_length + 0x0c + second_text_length;
                if (packet_count == 0 ||
                    static_cast<u32>(byte_count) < packet_count) {
                    break;
                }
                post_copied_window_payload(state.window,
                    kLinkLobbyDirectPlayStartMessage, 0, payload, packet_count);
                ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
                    static_cast<i32>(packet_count));
                payload = GetLegacyAsyncTcpReceiveBuffer(*state.async_tcp_socket);
                byte_count = GetLegacyAsyncTcpReceiveLength(*state.async_tcp_socket);
                continue;
            }

            if (byte_count < 0x0c) {
                break;
            }
            const u32 opcode = read_le32(payload + 4);
            const u32 packet_count = read_le32(payload + 8);
            if (packet_count == 0 ||
                static_cast<u32>(byte_count) < packet_count) {
                break;
            }
            if (opcode != kLinkLobbyPeerRouteSyncOpcode) {
                break;
            }

            ConsumeLegacyAsyncTcpReceiveQueue(*state.async_tcp_socket,
                static_cast<i32>(packet_count));
            destroy_window(state);
            if (state.callbacks.open_online_lobby != nullptr) {
                state.callbacks.open_online_lobby(state);
            }
            return;
        }
        return;
    }
    if (network_event == FD_CLOSE) {
        show_startup_message(state, 5, "Disconnected from the server.",
            kLinkMapFailureRed);
        if (state.async_tcp_socket != nullptr) {
            CloseLegacyAsyncTcpSocket(*state.async_tcp_socket);
        }
        ReturnFromLinkLobby(state);
    }
}

std::vector<u8> BuildLinkLobbyColoredTextPayload(COLORREF first_color,
    const char* first_text, COLORREF second_color, const char* second_text) {
    const char* safe_first = first_text == nullptr ? "" : first_text;
    const char* safe_second = second_text == nullptr ? "" : second_text;
    const std::size_t first_length = std::strlen(safe_first);
    const std::size_t second_length = std::strlen(safe_second);
    if (first_length + second_length == 0) {
        return {};
    }

    std::vector<u8> packet(first_length + second_length + 0x0e, 0);
    write_le32(packet, 0, 0);
    write_color_bytes(packet, 4, first_color);
    packet[7] = static_cast<u8>(first_length + 1);
    std::memcpy(packet.data() + 8, safe_first, first_length + 1);

    const std::size_t second_color_offset = first_length + 9;
    write_color_bytes(packet, second_color_offset, second_color);
    packet[second_color_offset + 3] = static_cast<u8>(second_length + 1);
    std::memcpy(packet.data() + second_color_offset + 4, safe_second,
        second_length + 1);
    return packet;
}

bool SendLinkLobbyChatEditText(LinkLobbyState& state) {
    if (state.chat_edit.window == nullptr) {
        return false;
    }

    char text[0x100]{};
    SendMessageA(state.chat_edit.window, WM_GETTEXT, sizeof(text),
        reinterpret_cast<LPARAM>(text));
    if (std::strlen(text) == 0) {
        return false;
    }

    char prefix[0x80]{};
    std::snprintf(prefix, sizeof(prefix), "%s> ",
        link_lobby_player_name(state, state.local_player_index));
    std::vector<u8> payload = BuildLinkLobbyColoredTextPayload(
        kLinkChatPromptColor, prefix, kLinkChatTextColor, text);
    if (payload.empty()) {
        return false;
    }

    const bool command = text[0] == '/' && state.mode != 6;
    if (command) {
        queue_async_command_payload(state, payload);
    } else {
        send_lobby_transport_payload(state, payload.data(),
            static_cast<i32>(payload.size()));
    }

    std::vector<u8> local_echo = payload;
    const std::size_t first_length = std::strlen(prefix);
    write_color_bytes(local_echo, 4, kLinkLocalPromptColor);
    write_color_bytes(local_echo, first_length + 9, kLinkChatTextColor);
    post_copied_window_payload(state.window, kLinkLobbyDirectPlayStartMessage, 0,
        local_echo.data(), local_echo.size());
    SendMessageA(state.chat_edit.window, WM_SETTEXT, 0,
        reinterpret_cast<LPARAM>(""));
    return true;
}

bool SendLinkLobbyStatsCommand(LinkLobbyState& state, const char* target_name) {
    char prefix[0x80]{};
    char command[0x100]{};
    const char* local_name = link_lobby_player_name(state, state.local_player_index);
    const char* command_name = target_name != nullptr && target_name[0] != '\0' ?
        target_name : link_lobby_player_name(state, state.local_player_index);
    std::snprintf(prefix, sizeof(prefix), "%s> ", local_name);
    std::snprintf(command, sizeof(command), "/stats %s", command_name);

    std::vector<u8> payload = BuildLinkLobbyColoredTextPayload(
        kLinkChatPromptColor, prefix, kLinkChatTextColor, command);
    if (payload.empty()) {
        return false;
    }
    return queue_async_command_payload(state, payload);
}

bool SubmitLinkLobbyChatCommand(LinkLobbyState& state) {
    if (!state.resources_ready || state.chat_edit.window == nullptr) {
        return false;
    }

    char text[0x100]{};
    SendMessageA(state.chat_edit.window, WM_GETTEXT, sizeof(text),
        reinterpret_cast<LPARAM>(text));
    if ((state.mode == 0 || state.mode == 2) && text[0] == '/' &&
        text[1] >= '1' && text[1] <= '8') {
        const int player_index = text[1] - '1';
        if (state.player_role_values[player_index] == 0) {
            SendLinkLobbyStatsCommand(state,
                link_lobby_player_name(state, player_index));
            SendMessageA(state.chat_edit.window, WM_SETTEXT, 0,
                reinterpret_cast<LPARAM>(""));
            return true;
        }
    }
    return SendLinkLobbyChatEditText(state);
}

bool AreLinkLobbyStartAcksComplete(const LinkLobbyState& state) {
    if (state.mode < 0 || state.mode > 2) {
        return true;
    }
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (link_lobby_player_needs_start_sync(state, i) &&
            state.start_acknowledged[i] != 1) {
            return false;
        }
    }
    return true;
}

bool LinkLobbyHostHasRemoteStartSyncPlayers(const LinkLobbyState& state) {
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (i == state.local_player_index) {
            continue;
        }
        if (link_lobby_player_needs_start_sync(state, i)) {
            return true;
        }
    }
    return false;
}

void ReportLinkLobbyStartAckWaiters(LinkLobbyState& state) {
    std::string message = startup_message_row(24, "Player ");
    int waiting = 0;
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (!link_lobby_player_needs_start_sync(state, i) ||
            state.start_acknowledged[i] == 1) {
            continue;
        }
        if (waiting != 0) {
            message += ", ";
        }
        message += link_lobby_player_name(state, i);
        ++waiting;
    }
    if (waiting == 0) {
        return;
    }

    message += startup_message_row(25, " UDP initialization error.");
    std::vector<u8> payload = BuildLinkLobbyColoredTextPayload(
        kLinkMapFailureRed, "", kLinkMapFailureRed, message.c_str());
    if (!payload.empty()) {
        send_lobby_transport_payload(state, payload.data(),
            static_cast<i32>(payload.size()));
        post_copied_window_payload(state.window, kLinkLobbyDirectPlayStartMessage, 0,
            payload.data(), payload.size());
    }
}

bool ProbeLinkLobbyUdpPeerAddress(LinkLobbyState&, const char* host, u16 port,
    sockaddr_in& out_address) {
    out_address = sockaddr_in{};
    if (host == nullptr || *host == '\0') {
        return false;
    }

    char resolved_address[0x100]{};
    const u_long direct_address = inet_addr(host);
    if (direct_address == INADDR_NONE) {
        hostent* entry = gethostbyname(host);
        if (entry == nullptr || entry->h_addr_list == nullptr ||
            entry->h_addr_list[0] == nullptr) {
            return false;
        }
        in_addr address{};
        std::memcpy(&address, entry->h_addr_list[0], sizeof(address));
        const char* dotted = inet_ntoa(address);
        if (dotted == nullptr) {
            return false;
        }
        std::snprintf(resolved_address, sizeof(resolved_address), "%s", dotted);
    } else {
        std::snprintf(resolved_address, sizeof(resolved_address), "%s", host);
    }

    sockaddr_in target_address = BuildLegacyUdpSockaddr(resolved_address, port);
    std::array<u8, kLinkLobbyUdpProbePacketBytes> packet{};
    write_le32(packet, 0, kLinkLobbyTransportPacketType);
    write_le32(packet, 4, kLinkLobbyUdpProbeOpcode);
    write_le32(packet, 8, kLinkLobbyUdpProbePacketBytes);

    for (int send_attempt = 0; send_attempt < 3; ++send_attempt) {
        if (!SendLegacyUdpChunks(kLinkLobbyUdpProbePacketBytes, packet.data(),
                target_address)) {
            return false;
        }
        for (int receive_attempt = 0; receive_attempt < 3; ++receive_attempt) {
            const i32 received = ReceiveLegacyUdpPacket();
            if (received == -1) {
                return false;
            }
            auto& queue = legacy_network_state().udp_receive_queue;
            if (queue.size() >= kLinkLobbyUdpProbePacketBytes &&
                read_le32(queue.data()) == kLinkLobbyTransportPacketType &&
                read_le32(queue.data() + 4) == kLinkLobbyUdpProbeOpcode &&
                read_le32(queue.data() + 8) == kLinkLobbyUdpProbePacketBytes) {
                std::memcpy(&out_address, queue.data() + 0x0c,
                    sizeof(out_address));
                ConsumeLegacyUdpReceiveQueue(kLinkLobbyUdpProbePacketBytes);
                return true;
            }
            Sleep(200);
        }
    }
    return false;
}

bool SetLinkLobbyDirectPlayJoinDisabled(LinkLobbyState& state) {
    state.directplay_join_disabled = true;
    const HRESULT result = SetCurrentDirectPlaySessionJoinDisabled(true,
        link_lobby_seed_max_players(state));
    if (SUCCEEDED(result)) {
        return true;
    }
    return state.host_mode;
}

bool ClearLinkLobbyDirectPlayJoinDisabled(LinkLobbyState& state) {
    state.directplay_join_disabled = false;
    return SUCCEEDED(SetCurrentDirectPlaySessionJoinDisabled(false,
        link_lobby_seed_max_players(state)));
}

bool SubmitLinkLobbyStartRequest(LinkLobbyState& state) {
    append_link_lobby_log(
        "link submit start begin host=%s mode=%ld game_type=%ld max_players=%lu local=%ld",
        state.host_mode ? "yes" : "no",
        static_cast<long>(state.mode),
        static_cast<long>(state.game_type),
        static_cast<unsigned long>(link_lobby_seed_max_players(state)),
        static_cast<long>(state.local_player_index));
    PrepareLinkLobbyStartParameters(state);
    append_link_lobby_log(
        "link submit prepared selected=%ld active_slots=%ld humans=%lu start_resource=%ld screen=%ld",
        static_cast<long>(CountLinkLobbySelectedAvatarSlots(state)),
        static_cast<long>(link_lobby_active_start_slot_count(state)),
        static_cast<unsigned long>(state.active_human_count),
        static_cast<long>(state.start_resource_index),
        static_cast<long>(state.screen_size_index));
    if (ReportLinkLobbyMapDownloadWaiters(state)) {
        append_link_lobby_log("link submit blocked map download waiters");
        return false;
    }
    state.selected_avatar_count = CountLinkLobbySelectedAvatarSlots(state);
    if (state.selected_avatar_count == 0) {
        append_link_lobby_log("link submit blocked no selected avatars");
        show_startup_message(state, 28, "Another player is required.",
            kLinkMapFailureRed);
        return false;
    }
    if (!link_lobby_start_team_requirements_met(state) &&
        !(state.host_mode && link_lobby_active_start_slot_count(state) >= 2)) {
        append_link_lobby_log(
            "link submit blocked team requirements active_slots=%ld",
            static_cast<long>(link_lobby_active_start_slot_count(state)));
        show_startup_message(state, 28,
            "The selected game type requires more players.",
            kLinkMapFailureRed);
        return false;
    }
    if (!SetLinkLobbyDirectPlayJoinDisabled(state)) {
        append_link_lobby_log("link submit blocked directplay join disable");
        show_startup_message(state, 28, "Another player is required.",
            kLinkMapFailureRed);
        return false;
    }

    if (state.mode < 0 || state.mode > 2) {
        if (!SendLinkLobbyStartParametersPacket(state)) {
            ClearLinkLobbyDirectPlayJoinDisabled(state);
            append_link_lobby_log("link submit blocked start parameters send");
            show_startup_message(state, 28, "Another player is required.",
                kLinkMapFailureRed);
            return false;
        }
        if (state.window != nullptr) {
            PostMessageA(state.window, kLinkLobbyStartDecisionMessage, 0, 0);
        }
        append_link_lobby_log("link submit posted start decision non-mode1");
        return true;
    }

    const bool has_remote_start_players =
        LinkLobbyHostHasRemoteStartSyncPlayers(state);
    if (!player_has_advertised_udp_route(state, state.local_player_index) ||
        (has_remote_start_players &&
            !all_remote_human_routes_advertised(state))) {
        ClearLinkLobbyDirectPlayJoinDisabled(state);
        append_link_lobby_log(
            "link submit blocked missing udp route local_ready=%s remote_ready=%s",
            player_has_advertised_udp_route(state, state.local_player_index) ?
                "yes" : "no",
            all_remote_human_routes_advertised(state) ? "yes" : "no");
        show_startup_message(state, 29,
            "Unable to resolve the local player IP address.",
            kLinkMapFailureRed);
        return false;
    }

    if (!SendLinkLobbyPeerRouteTablePacket(state)) {
        ClearLinkLobbyDirectPlayJoinDisabled(state);
        append_link_lobby_log("link submit blocked peer route send");
        show_startup_message(state, 28, "Another player is required.",
            kLinkMapFailureRed);
        return false;
    }
    state.secondary_start_sync_required = true;
    if (state.host_mode && !has_remote_start_players) {
        if (state.window != nullptr) {
            PostMessageA(state.window, kLinkLobbyStartDecisionMessage, 0, 0);
        }
        append_link_lobby_log("link submit posted start decision host local-only");
        return true;
    }
    BeginLinkLobbyPeerRouteSync(state, nullptr, 0);
    append_link_lobby_log("link submit began peer route sync");
    return true;
}

void PumpLinkLobbyUdpStartSync(LinkLobbyState& state) {
    while (true) {
        const i32 received = ReceiveLegacyUdpPacket();
        if (received == -1) {
            break;
        }
        auto& queue = legacy_network_state().udp_receive_queue;
        if (received < 0x0c || queue.size() < 0x0c) {
            break;
        }

        const u32 packet_size = read_le32(queue.data() + 8);
        if (packet_size == 0 || static_cast<u32>(received) < packet_size ||
            queue.size() < packet_size) {
            break;
        }
        if (read_le32(queue.data() + 4) == kLinkLobbyUdpStartAckOpcode) {
            record_udp_start_ack(state, queue.data(), packet_size);
        }
        ConsumeLegacyUdpReceiveQueue(packet_size);
    }

    if (AreLinkLobbyStartAcksComplete(state) && !state.start_sync_complete) {
        state.start_sync_complete = true;
        if (!state.secondary_start_sync_required) {
            stop_start_sync_timer(state);
            StopLinkLobbyPeerRouteTimer(state);
            send_start_sync_packet(state, kLinkLobbyStartSyncStopOpcode,
                state.local_player_index, 1, state.shared_peer_socket);
            return;
        }
        if (player_index_valid(state.local_player_index)) {
            state.secondary_start_acknowledged[state.local_player_index] = 1;
            if (!state.host_mode) {
                stop_start_sync_timer(state);
                StopLinkLobbyPeerRouteTimer(state);
                SendLinkLobbySecondaryStartAckPacket(state, 1);
                append_link_lobby_log(
                    "link udp primary sync complete client=%ld host ack sent",
                    static_cast<long>(state.local_player_index));
                return;
            }
            append_link_lobby_log(
                "link udp primary sync complete host=%ld waiting client acks",
                static_cast<long>(state.local_player_index));
        }
    }

    if (state.secondary_start_sync_required && state.host_mode) {
        bool secondary_complete = true;
        for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
            if (link_lobby_player_needs_start_sync(state, i) &&
                state.secondary_start_acknowledged[i] != 1) {
                secondary_complete = false;
                break;
            }
        }
        if (secondary_complete) {
            stop_start_sync_timer(state);
            StopLinkLobbyPeerRouteTimer(state);
            if (state.host_mode) {
                const bool finalized =
                    state.callbacks.finalize_start_sync == nullptr ||
                    state.callbacks.finalize_start_sync(state);
                if (!finalized) {
                    show_startup_message(state, 29,
                        "Unable to resolve the local player IP address.",
                        kLinkMapFailureRed);
                } else if (state.window != nullptr) {
                    PostMessageA(state.window, kLinkLobbyStartDecisionMessage, 0, 0);
                }
            }
            append_link_lobby_log(
                "link udp secondary sync complete local=%ld host=%s",
                static_cast<long>(state.local_player_index),
                state.host_mode ? "yes" : "no");
            return;
        }
    }

    if (state.start_sync_complete) {
        return;
    }

    ++state.start_sync_retry_count;
    if (state.start_sync_retry_count * state.start_sync_retry_interval_ms <
        state.start_sync_retry_limit_ms) {
        for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
            if (link_lobby_player_needs_start_sync(state, i) &&
                state.start_acknowledged[i] != 1) {
                send_start_sync_packet(state, kLinkLobbyStartSyncRetryOpcode,
                    state.local_player_index, i, start_sync_target_socket(state, i));
            }
        }
        return;
    }

    stop_start_sync_timer(state);
    StopLinkLobbyPeerRouteTimer(state);
    if (state.secondary_start_sync_required) {
        show_startup_message(state, 95,
            "The game cannot start because UDP peer connections failed.",
            kLinkMapFailureRed);
        ClearLinkLobbyDirectPlayJoinDisabled(state);
    }
    ReportLinkLobbyStartAckWaiters(state);
}

void SwapLinkLobbyPlayerPayloads(LinkLobbyState& state, int left_player, int right_player) {
    if (!player_index_valid(left_player) || !player_index_valid(right_player) ||
        left_player == right_player) {
        return;
    }
    std::swap(state.player_payloads[left_player], state.player_payloads[right_player]);
    std::swap(state.players[left_player].raw_payload,
        state.players[right_player].raw_payload);
}

bool CopyIncomingLinkLobbyPlayerPayload(LinkLobbyState& state, const void* message,
    std::size_t byte_count) {
    if (message == nullptr ||
        byte_count < kLinkLobbyPlayerPayloadBodyOffset + kLinkLobbyPlayerPayloadBodyBytes) {
        return false;
    }

    const auto* bytes = static_cast<const u8*>(message);
    const u32 slot = read_le32(bytes + 0x0c);
    if (slot >= kLinkLobbyAvatarCount) {
        return false;
    }

    std::memcpy(state.player_payloads[slot].data(),
        bytes + kLinkLobbyPlayerPayloadBodyOffset, kLinkLobbyPlayerPayloadBodyBytes);
    std::memcpy(state.players[slot].raw_payload.data(),
        state.player_payloads[slot].data(), state.player_payloads[slot].size());
    return true;
}

void PublishLinkLobbySelectedAvatarPayloads(LinkLobbyState& state, int player_index) {
    if (!player_index_valid(player_index)) {
        return;
    }

    if (state.store_avatar_publish_locally) {
        for (int slot = 0; slot < kLinkLobbyAvatarCount; ++slot) {
            const auto record = published_avatar_record(state, slot);
            store_avatar_record_in_payload(state.player_payloads[player_index], slot,
                record);
            store_avatar_record_in_payload(state.players[player_index].raw_payload, slot,
                record);
        }
        return;
    }

    std::vector<u8> packet(kLinkLobbyAvatarPublishPacketBytes, 0);
    write_le32(packet, 4, kLinkLobbyAvatarPublishOpcode);
    write_le32(packet, 0x0c, static_cast<u32>(player_index));
    for (int slot = 0; slot < kLinkLobbyAvatarCount; ++slot) {
        const auto record = published_avatar_record(state, slot);
        const std::size_t offset = kLinkLobbyPlayerPayloadBodyOffset +
            static_cast<std::size_t>(slot) * kLinkLobbyAvatarPayloadBytes;
        if (offset + record.size() <= packet.size()) {
            std::memcpy(packet.data() + offset, record.data(), record.size());
        }
    }
    queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
}

bool CreateLinkLobbyWindow(LinkLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM player_slots, LPARAM lobby_payload, LPARAM session_context, int mode,
    LPARAM return_context, int game_type, int screen_size) {
    const bool join_existing_lobby = player_slots != 0;
    const auto preserved_players = state.players;
    const auto preserved_player_payloads = state.player_payloads;
    const auto preserved_avatar_payloads = state.avatar_payloads;
    const SOCKET pending_join_socket = state.pending_join_socket;

    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    state.session_context = reinterpret_cast<void*>(session_context);
    state.mode = mode;
    state.game_type = game_type;
    state.screen_size_index = std::clamp(screen_size, 0, 3);
    SetActiveNetworkTransportMode(mode);
    state.visible = false;
    state.host_mode = !join_existing_lobby;
    state.start_locked = false;
    state.resources_ready = false;
    state.join_accepted = join_existing_lobby;
    state.join_request_pending = false;
    state.countdown_value = -1;
    state.selected_avatar_index = -1;
    state.players = {};
    state.player_payloads = {};
    state.avatar_payloads = {};
    state.start_acknowledged = {};
    state.secondary_start_acknowledged = {};
    state.player_sockets.fill(INVALID_SOCKET);
    state.udp_peer_addresses = {};
    state.local_udp_reflexive_address = {};
    state.local_udp_reflexive_address_valid = false;
    ClearDirectPlayMode1UdpPeerAddresses();
    state.primary_peer_hosts = {};
    state.secondary_peer_hosts = {};
    state.primary_peer_ports = {};
    state.secondary_peer_ports = {};
    state.peer_route_acknowledged = {};
    state.start_sync_timer = 0;
    state.peer_route_timer = 0;
    state.start_sync_retry_count = 0;
    state.start_sync_complete = false;
    state.secondary_start_sync_required = false;
    state.directplay_join_disabled = false;
    state.udp_probe_route_toggle = false;
    state.store_avatar_publish_locally = state.host_mode;
    state.shared_peer_socket = INVALID_SOCKET;
    state.pending_join_socket = INVALID_SOCKET;
    state.player_role_values = {};
    state.player_team_values = {};
    state.player_role_option_masks.fill(0x0f);
    state.tribe_option_masks.fill(0x1f);
    state.map_descriptor = {};
    state.session_seed_payload = {};
    state.password.fill('\0');
    if (lobby_payload != 0) {
        std::memcpy(state.map_descriptor.data(),
            reinterpret_cast<const void*>(lobby_payload), state.map_descriptor.size());
        apply_link_lobby_map_descriptor_fields(state, join_existing_lobby);
    }
    if (session_context != 0) {
        std::memcpy(state.session_seed_payload.data(),
            reinterpret_cast<const void*>(session_context),
            state.session_seed_payload.size());
    }
    state.start_parameter_payload.clear();
    state.map_download_received_bytes = 0;
    state.last_transport_rate_time_ms = 0;
    if (join_existing_lobby) {
        state.players = preserved_players;
        state.player_payloads = preserved_player_payloads;
        state.avatar_payloads = preserved_avatar_payloads;
    } else if (state.host_mode) {
        initialize_host_player_slots(state);
    }
    apply_link_lobby_session_seed_fields(state);
    if (state.mode == 1 && p2p_lobby_state().player_name[0] != '\0') {
        SetLinkLobbyLocalPlayerIdentity(state,
            p2p_lobby_state().player_name.data());
    }
    if (join_existing_lobby && pending_join_socket != INVALID_SOCKET) {
        state.shared_peer_socket = pending_join_socket;
        if (player_index_valid(state.local_player_index)) {
            state.player_sockets[state.local_player_index] = pending_join_socket;
        }
    }

    InitializeLinkLobbyBackgroundResourceAndShutdown(state);
    InitializeLinkLobbyPanelResourceAndShutdown(state);
    InitializeLinkLobbyAvatarSelectedResourceAndShutdown(state);
    InitializeLinkLobbyAvatarAvailableResourceAndShutdown(state);
    InitializeLinkLobbyDownloadResourceAndShutdown(state);
    InitializeLinkLobbyAvatarStripResourceAndShutdown(state);
    InitializeLinkLobbyGameInfoButtonSupport(state);
    InitializeLinkLobbyStartButtonSupport(state);
    InitializeLinkLobbyCancelButtonSupport(state);
    InitializeLinkLobbyAvatarInfoButtonSupport(state);
    InitializeLinkLobbyPrimaryScrollSupport(state);
    InitializeLinkLobbySecondaryScrollSupport(state);
    InitializeLinkLobbyHostResourceComboControl(state);
    RegisterLinkLobbyHostResourceComboShutdown(state);
    InitializeLinkLobbyStartResourceComboSupport(state);
    InitializeLinkLobbyScreenSizeComboSupport(state);
    InitializeLinkLobbyPlayerRoleComboControl0(state);
    RegisterLinkLobbyPlayerRoleComboDestructor0(state);
    InitializeLinkLobbyPlayerRoleComboStatic1(state);
    InitializeLinkLobbyPlayerRoleComboStatic2(state);
    InitializeLinkLobbyPlayerRoleComboStatic3(state);
    InitializeLinkLobbyPlayerRoleComboStatic4(state);
    InitializeLinkLobbyPlayerRoleComboStatic5(state);
    InitializeLinkLobbyPlayerRoleComboStatic6(state);
    InitializeLinkLobbyPlayerRoleComboStatic7(state);
    InitializeLinkLobbyTribeComboControl0(state);
    RegisterLinkLobbyTribeComboDestructor0(state);
    InitializeLinkLobbyTribeComboStatic1(state);
    InitializeLinkLobbyTribeComboStatic2(state);
    InitializeLinkLobbyTribeComboStatic3(state);
    InitializeLinkLobbyTribeComboStatic4(state);
    InitializeLinkLobbyTribeComboStatic5(state);
    InitializeLinkLobbyTribeComboStatic6(state);
    InitializeLinkLobbyTribeComboStatic7(state);
    InitializeLinkLobbyTabButtons(state);
    InitializeLinkLobbyAvatarButtonArraySupport(state);
    InitializeLinkLobbyLatencyButtons(state);
    InitializeLinkLobbyLatencyBitmaps(state);
    InitializeLinkLobbyMapDownloadButtons(state);
    apply_link_lobby_session_seed_fields(state);

    state.layout.clear();
    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(layout.table,
            kLinkLobbyLayoutTrcRecord)) {
        release_resources(state);
        return false;
    }
    state.layout = copy_layout_record(layout.table);

    const LinkLobbyLayoutRect window_rect = layout_at(state, 0);
    // The original fullscreen frontend path creates this lobby as an owned
    // top-level popup. Parent validity is not the legacy windowed-mode flag;
    // using it here incorrectly turned every reconstructed lobby into a child
    // window. Keep the observed popup style and legacy coordinates.
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Link", "Link",
        kWindowStyleFullscreen, 0, 0, window_rect.width, window_rect.height,
        parent, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        release_resources(state);
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(link_lobby_window_proc));

    if (!create_window_control(state.game_list, state.window, instance, "listbox",
            nullptr, kListBoxStyle, kLinkLobbyListBoxId, layout_at(state, 1)) ||
        !create_scroll(state.game_list_scroll, state.window, "Link List",
            kLinkLobbyGameListScrollId, false, layout_at(state, 2)) ||
        !create_window_control(state.chat_edit, state.window, instance, "edit", nullptr,
            kChatEditStyle, kLinkLobbyChatEditId, layout_at(state, 3)) ||
        !create_button(state.game_info_button, state.window, "Game infos",
            kLinkLobbyInfoPanelId, layout_at(state, 4), 0, 0) ||
        !create_button(state.start_button, state.window, "Start &Game",
            kLinkLobbyStartButtonId, layout_at(state, 5), 0x87, 0x88) ||
        !create_button(state.cancel_button, state.window, "&Cancel",
            kLinkLobbyCancelButtonId, layout_at(state, 6), 0x8a, 0x8b) ||
        !create_combo(state.start_resource_combo, state.window, "Start resource",
            kLinkLobbyStartResourceComboId, kVisibleComboStyle, layout_at(state, 7),
            0x95) ||
        !create_combo(state.screen_size_combo, state.window, "Screen size",
            kLinkLobbyScreenSizeComboId, kHiddenDisabledComboStyle,
            layout_at(state, 8), 0x96) ||
        !create_button(state.avatar_info_button, state.window, "avatar info",
            kLinkLobbyAvatarInfoId, layout_at(state, 22), 0, 0) ||
        !create_scroll(state.map_selection_scroll, state.window, "Map Select",
            kLinkLobbyMapSelectionScrollId, true, layout_at(state, 9))) {
        release_resources(state);
        return false;
    }
    const LinkLobbyLayoutRect game_scroll_rect = layout_at(state, 2);
    const LinkLobbyLayoutRect map_scroll_rect = layout_at(state, 9);
    LoadLegacyCustomScrollControlBitmaps(state.game_list_scroll,
        kLinkLobbyGameListScrollStartBitmapRecord,
        kLinkLobbyGameListScrollStartBitmapRecord,
        kLinkLobbyGameListScrollEndBitmapRecord,
        kLinkLobbyGameListScrollEndBitmapRecord,
        kLinkLobbyGameListScrollThumbBitmapRecord,
        kLinkLobbyGameListScrollTrackBitmapRecord);
    SetLegacyCustomScrollControlMetrics(state.game_list_scroll,
        game_scroll_rect.width, game_scroll_rect.width, game_scroll_rect.width,
        game_scroll_rect.width);
    LoadLegacyCustomScrollControlBitmaps(state.map_selection_scroll,
        kLinkLobbyMapScrollStartBitmapRecord, 0,
        kLinkLobbyMapScrollEndBitmapRecord, 0,
        kLinkLobbyMapScrollThumbBitmapRecord,
        kLinkLobbyMapScrollTrackBitmapRecord);
    SetLegacyCustomScrollControlRange(state.map_selection_scroll, 0, 0x0f, false);
    SetLegacyCustomScrollControlPageStep(state.map_selection_scroll, 4);
    SetLegacyCustomScrollControlMetrics(state.map_selection_scroll,
        map_scroll_rect.height, map_scroll_rect.height, map_scroll_rect.height,
        map_scroll_rect.height);
    const int initial_map_scroll = link_lobby_seed_map_scroll_value(state);
    SetLegacyCustomScrollControlValue(state.map_selection_scroll, initial_map_scroll, false);
    SetLegacyCustomScrollControlVisible(state.map_selection_scroll, true);
    state.map_selection_index = 0x0f - initial_map_scroll;
    if (state.game_type == 8) {
        if (!create_combo(state.host_resource_combo, state.window, "Start resource",
                kLinkLobbyHostResourceComboId, kVisibleComboStyle,
                layout_at(state, 10), 0x98)) {
            release_resources(state);
            return false;
        }
    }
    if (!CreateLinkLobbyPlayerRoleControls(state)) {
        release_resources(state);
        return false;
    }
    for (int i = 0; i < kLinkLobbyAvatarCount; ++i) {
        if (!create_button(state.avatar_buttons[i], state.window, "AvataSlot",
                kLinkLobbyAvatarFirstId + i, layout_at(state, 14 + i), 0, 0)) {
            release_resources(state);
            return false;
        }
    }
    if (link_lobby_uses_single_group_room_layout(state)) {
        ShowWindow(state.avatar_info_button.window, SW_HIDE);
        for (LegacyImageButtonControl& button : state.avatar_buttons) {
            ShowWindow(button.window, SW_HIDE);
        }
    }

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kLinkLobbyBackgroundBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.panel_background, "Jw2_19.trc",
        kLinkLobbyPanelBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.avatar_selected_background,
        "Jw2_19.trc", kLinkLobbyAvatarSelectedBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.avatar_available_background,
        "Jw2_19.trc", kLinkLobbyAvatarAvailableBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.download_background, "Jw2_19.trc",
        kLinkLobbyDownloadBitmapRecord);
    LoadLinkLobbyLatencyBitmaps(state);

    const char* resources[] = {
        startup_message_row(154, "Map default"),
        startup_message_row(155, "Low"),
        startup_message_row(156, "Middle"),
        startup_message_row(157, "High"),
    };
    const char* screen_sizes[] = {"640x480", "800x600", "1024x768", "Free Size"};
    fill_combo(state.start_resource_combo.window, resources, std::size(resources),
        state.start_resource_index);
    fill_combo(state.screen_size_combo.window, screen_sizes, std::size(screen_sizes),
        state.screen_size_index);
    if (state.host_resource_combo.window != nullptr) {
        const char* host_resources[] = {"30", "60", "90", "120", "150", "180",
            "210", "240", "270", "300"};
        fill_combo(state.host_resource_combo.window, host_resources,
            std::size(host_resources), state.host_resource_index);
    }

    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(link_lobby_ui_font()), TRUE);
    SendMessageA(state.game_list.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(link_lobby_ui_font()), TRUE);
    RedrawWindow(state.game_list.window, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    SendMessageA(state.chat_edit.window, EM_LIMITTEXT, 200, 0);
    sync_game_list_scroll(state, false);
    InstallLinkLobbyAccelerators(state);
    if (!InitializeLinkLobbyNetworkRoute(state)) {
        ReturnFromLinkLobby(state);
        return false;
    }
    MarkLinkLobbyResourcesReady(state);
    if (join_existing_lobby) {
        if (state.mode >= 0 && state.mode <= 2) {
            if (state.shared_peer_socket != INVALID_SOCKET) {
                state.join_request_pending = true;
                WSAAsyncSelect(state.shared_peer_socket, state.window,
                    kLinkLobbySocketMessage, kLinkLobbyJoinSocketEvents);
            }
        } else {
            send_local_join_request(state, state.shared_peer_socket);
        }
    }
    ConfigureDirectPlayMode6WindowDispatch(state.window, state.host_mode);
    SetDirectPlayMessageDispatchMode(6);
    // Link is a native GDI window layered over the DirectDraw presentation.
    // Restore and retire the software cursor before its child controls paint;
    // otherwise a later combo repaint can cover only the middle of the
    // 32-by-32 cursor and leave its previous/current pixels across "Random".
    SetGameCursorPresentationSuppressed(true);
    SetFocus(state.chat_edit.window);
    ShowWindow(state.window, SW_SHOW);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE |
        RDW_UPDATENOW | RDW_ALLCHILDREN);
    schedule_link_lobby_combo_refresh(state);
    state.visible = true;
    return true;
}

LRESULT HandleLinkLobbyWindowMessage(LinkLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE) {
            schedule_link_lobby_combo_refresh(state);
        }
        break;
    case WM_DESTROY:
        if (state.combo_refresh_timer != 0) {
            KillTimer(hwnd, state.combo_refresh_timer);
            state.combo_refresh_timer = 0;
        }
        if (state.countdown_timer != 0) {
            KillTimer(hwnd, state.countdown_timer);
            state.countdown_timer = 0;
        }
        stop_start_sync_timer(state);
        StopLinkLobbyPeerRouteTimer(state);
        ConfigureDirectPlayMode6WindowDispatch(nullptr, false);
        SetDirectPlayMessageDispatchMode(0);
        RestoreLinkLobbyAccelerators(state);
        release_resources(state);
        state.window = nullptr;
        state.visible = false;
        return 0;
    case WM_PAINT:
        if (hwnd == state.window) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
            if (state.game_type == 8) {
                StretchBitmapMemoryResourceToDc(state.download_background, dc, 600, 277);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        if (hwnd == state.window) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
            if (state.game_type == 8) {
                StretchBitmapMemoryResourceToDc(state.download_background, dc, 600, 277);
            }
            return 1;
        }
        break;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (draw.CtlID == kLinkLobbyListBoxId) {
            draw_game_list_item(state, draw);
            return TRUE;
        }
        if (draw.CtlID == kLinkLobbyInfoPanelId) {
            draw_info_panel(state, draw);
            return TRUE;
        }
        if (draw.CtlID >= kLinkLobbyTabFirstId &&
            draw.CtlID < kLinkLobbyTabFirstId + kLinkLobbyTabButtonCount) {
            DrawLinkLobbyTabButton(state, draw.CtlID - kLinkLobbyTabFirstId, draw);
            return TRUE;
        }
        if (draw.CtlID >= kLinkLobbyAvatarFirstId &&
            draw.CtlID < kLinkLobbyAvatarFirstId + kLinkLobbyAvatarCount) {
            draw_avatar_button(state, draw);
            return TRUE;
        }
        if (draw.CtlID >= kLinkLobbyLatencyFirstId &&
            draw.CtlID < kLinkLobbyLatencyFirstId + kLinkLobbyAvatarCount) {
            DrawLinkLobbyLatencyButton(state,
                draw.CtlID - kLinkLobbyLatencyFirstId, draw);
            return TRUE;
        }
        if (draw.CtlID >= kLinkLobbyMapDownloadFirstId &&
            draw.CtlID < kLinkLobbyMapDownloadFirstId + kLinkLobbyAvatarCount) {
            DrawLinkLobbyMapDownloadButton(state,
                draw.CtlID - kLinkLobbyMapDownloadFirstId, draw);
            return TRUE;
        }
        if (draw.CtlID == kLinkLobbyStartResourceComboId ||
            draw.CtlID == kLinkLobbyScreenSizeComboId ||
            draw.CtlID == kLinkLobbyHostResourceComboId ||
            (draw.CtlID >= kLinkLobbyPlayerRoleComboFirstId &&
                draw.CtlID < kLinkLobbyPlayerRoleComboFirstId +
                    kLinkLobbyAvatarCount) ||
            (draw.CtlID >= kLinkLobbyTribeComboFirstId &&
                draw.CtlID < kLinkLobbyTribeComboFirstId + kLinkLobbyAvatarCount)) {
            LegacyImageComboBoxControl* combo =
                draw.CtlID == kLinkLobbyStartResourceComboId ? &state.start_resource_combo :
                draw.CtlID == kLinkLobbyScreenSizeComboId ? &state.screen_size_combo :
                draw.CtlID == kLinkLobbyHostResourceComboId ? &state.host_resource_combo :
                draw.CtlID >= kLinkLobbyPlayerRoleComboFirstId &&
                    draw.CtlID < kLinkLobbyPlayerRoleComboFirstId +
                        kLinkLobbyAvatarCount ?
                    &state.player_role_combos[
                        draw.CtlID - kLinkLobbyPlayerRoleComboFirstId] :
                    &state.tribe_combos[draw.CtlID - kLinkLobbyTribeComboFirstId];
            DrawLegacyImageComboBoxItem(*combo, draw);
            return TRUE;
        }
        if (LegacyImageButtonControl* button = button_for_id(state, draw.CtlID)) {
            DrawLegacyImageButtonItem(*button, draw);
            return TRUE;
        }
        NoOpLinkLobbyUnusedOwnerDrawControl(state, draw);
        break;
    }
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wparam), kLinkSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kLinkBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kLinkSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kLinkBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_TIMER:
        if (state.combo_refresh_timer != 0 &&
            wparam == static_cast<WPARAM>(state.combo_refresh_timer)) {
            KillTimer(hwnd, state.combo_refresh_timer);
            state.combo_refresh_timer = 0;
            redraw_link_lobby_image_combos(state);
            return 0;
        }
        if (state.start_sync_timer != 0 &&
            wparam == static_cast<WPARAM>(state.start_sync_timer)) {
            PumpLinkLobbyUdpStartSync(state);
            return 0;
        }
        if (state.peer_route_timer != 0 &&
            wparam == static_cast<WPARAM>(state.peer_route_timer)) {
            PostMessageA(hwnd, kLinkLobbyStartSyncPumpMessage, 0, 0);
            return 0;
        }
        if (state.countdown_value > 0) {
            --state.countdown_value;
            const std::string message_text =
                format_countdown_message(state.countdown_value);
            show_message(state, message_text.c_str());
            if (state.countdown_value == 0) {
                if (state.countdown_timer != 0) {
                    KillTimer(hwnd, state.countdown_timer);
                    state.countdown_timer = 0;
                }
                std::vector<u8> packet(0x2d, 0);
                write_le32(packet, 0, 3);
                write_le32(packet, 4, 0x28);
                write_le32(packet, 8, 0x2d);
                for (int i = 0; i < kLinkLobbyAvatarCount &&
                        0x0d + i < static_cast<int>(packet.size()); ++i) {
                    packet[0x0d + i] = state.tribe_choices[i];
                }
                queue_packet(state, packet.data(), static_cast<i32>(packet.size()));
                if (state.callbacks.start_game != nullptr) {
                    append_link_lobby_log("link countdown complete calling start_game");
                    state.callbacks.start_game(state);
                }
                destroy_window(state);
            }
        }
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (state.main_window != nullptr) {
            SendMessageA(state.main_window, message, wparam, lparam);
        }
        break;
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        if (id == kLinkLobbyCancelButtonId) {
            HandleDefaultFrontendUiClickSound();
            ReturnFromLinkLobby(state);
            return 0;
        }
        if (id == kLinkLobbyStartButtonId) {
            HandleDefaultFrontendUiClickSound();
            if (link_lobby_directplay_ready() || state.host_mode) {
                SubmitLinkLobbyStartRequest(state);
            }
            return 0;
        }
        if (id == kLinkLobbyHostResourceComboId && notify == CBN_SELCHANGE) {
            PrepareLinkLobbyStartParameters(state);
            if (link_lobby_directplay_ready() && state.game_type == 8) {
                SendLinkLobbyHostResourceSelectionPacket(state,
                    static_cast<u32>(std::max(0, state.host_resource_index)));
            }
            return 0;
        }
        if (id == kLinkLobbyStartResourceComboId && notify == CBN_SELCHANGE) {
            PrepareLinkLobbyStartParameters(state);
            if (link_lobby_directplay_ready()) {
                SendLinkLobbyStartResourceSelectionPacket(state,
                    static_cast<u32>(std::max(0, state.start_resource_index)));
            }
            return 0;
        }
        if (id == kLinkLobbyScreenSizeComboId && notify == CBN_SELCHANGE) {
            PrepareLinkLobbyStartParameters(state);
            return 0;
        }
        if (id >= kLinkLobbyPlayerRoleComboFirstId &&
            id < kLinkLobbyPlayerRoleComboFirstId + kLinkLobbyAvatarCount &&
            notify == CBN_SELCHANGE) {
            const int index = id - kLinkLobbyPlayerRoleComboFirstId;
            HandleLinkLobbyPlayerRoleComboChange(state, index);
            return 0;
        }
        if (id >= kLinkLobbyTribeComboFirstId &&
            id < kLinkLobbyTribeComboFirstId + kLinkLobbyAvatarCount &&
            notify == CBN_SELCHANGE) {
            const int index = id - kLinkLobbyTribeComboFirstId;
            HandleLinkLobbyTribeComboChange(state, index);
            return 0;
        }
        if (id == kLinkLobbySendChatCommandId) {
            SubmitLinkLobbyChatCommand(state);
            return 0;
        }
        if (id >= kLinkLobbyTabFirstId &&
            id < kLinkLobbyTabFirstId + kLinkLobbyTabButtonCount) {
            SendLinkLobbyAutoMoveOpenSlotPacket(state,
                static_cast<u32>(std::max(0, state.local_player_index)),
                static_cast<u32>(id - kLinkLobbyTabFirstId));
            return 0;
        }
        if (id >= kLinkLobbyAvatarFirstId &&
            id < kLinkLobbyAvatarFirstId + kLinkLobbyAvatarCount) {
            const int index = id - kLinkLobbyAvatarFirstId;
            HandleDefaultFrontendUiClickSound();
            if (state.players[index].occupied) {
                state.players[index].selected = !state.players[index].selected;
                state.selected_avatar_index = state.players[index].selected ? index : -1;
                PublishLinkLobbySelectedAvatarPayloads(state,
                    state.local_player_index);
                RedrawWindow(state.avatar_buttons[index].window, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_UPDATENOW);
            }
            return 0;
        }
        break;
    }
    case kLinkLobbyDirectPlayStartMessage:
        if (lparam != 0) {
            const std::size_t payload_size = locked_window_payload_size(lparam);
            LinkLobbyMessageLine line{};
            if (parse_colored_text_payload(reinterpret_cast<const void*>(lparam),
                    payload_size, line) && !line.plain_text.empty()) {
                show_message_line(state, line);
            } else if (payload_size != 0) {
                const char* text = reinterpret_cast<const char*>(lparam);
                const void* terminator = std::memchr(text, '\0', payload_size);
                if (terminator != nullptr && terminator != text) {
                    const std::string bounded_text(text,
                        static_cast<const char*>(terminator) - text);
                    show_message(state, bounded_text.c_str(), kLinkErrorBlue);
                }
            }
            free_locked_window_payload(lparam);
        }
        return 0;
    case kLinkLobbyCancelStartMessage:
        state.start_locked = false;
        if (state.countdown_value == -1) {
            if (state.countdown_timer != 0) {
                KillTimer(hwnd, state.countdown_timer);
                state.countdown_timer = 0;
            }
            stop_start_sync_timer(state);
            StopLinkLobbyPeerRouteTimer(state);
            SendLinkLobbyStopPeerRouteTimerPacket(state);
            if (state.directplay_join_disabled) {
                ClearLinkLobbyDirectPlayJoinDisabled(state);
            }
            return 0;
        }
        SendLinkLobbyStartTimeoutPacket(state,
            static_cast<u32>(std::max(0, state.local_player_index)));
        ReportLinkLobbyPlayerStartTimeout(state, state.local_player_index);
        if (state.countdown_timer != 0) {
            KillTimer(hwnd, state.countdown_timer);
            state.countdown_timer = 0;
        }
        state.countdown_value = -1;
        return 0;
    case kLinkLobbyStartDecisionMessage:
        if (wparam == 0) {
            BeginLinkLobbyStartCountdown(state);
        } else {
            ReportLinkLobbyPlayerStartTimeout(state, static_cast<int>(wparam));
        }
        return 0;
    case kLinkLobbyStartAcceptedMessage:
        return 0;
    case kLinkLobbyStartSyncPumpMessage:
        PumpLinkLobbyUdpStartSync(state);
        return 0;
    case kLinkLobbyNetworkMessage:
        HandleLinkLobbyAsyncTcpSocketEvent(state, wparam, lparam);
        return 0;
    case kLinkLobbyCopiedPayloadMessage:
        if (lparam != 0) {
            const void* payload = reinterpret_cast<const void*>(lparam);
            std::size_t byte_count = kLinkLobbyPlayerPayloadBodyOffset +
                kLinkLobbyPlayerPayloadBodyBytes;
            const auto* bytes = static_cast<const u8*>(payload);
            if (read_le32(bytes) == kLinkLobbyTransportPacketType) {
                const u32 declared_size = read_le32(bytes + 8);
                if (declared_size != 0) {
                    byte_count = declared_size;
                }
            }
            DispatchLinkLobbyTransportPacket(state, static_cast<u32>(wparam),
                payload, byte_count);
            free_locked_window_payload(lparam);
        }
        return 0;
    case kLinkLobbyDownloadProgressMessage:
        SendLinkLobbyRequestedMapChunk(state, static_cast<int>(wparam),
            static_cast<u32>(lparam));
        return 0;
    case kLinkLobbyOwnerSyncMessage:
        HandleLinkLobbyListenSocketEvent(state, wparam, lparam);
        return 0;
    case kLinkLobbySocketMessage:
        HandleLinkLobbyPeerSocketEvent(state, wparam, lparam);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleLinkLobbyControlMessage(LinkLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));

    if (message == WM_ERASEBKGND &&
        erase_game_list_background_if_current(
            state, hwnd, reinterpret_cast<HDC>(wparam))) {
        return 1;
    }
    if (message == WM_PAINT && hwnd == state.game_list_scroll.window) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.game_list_scroll, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_PAINT && hwnd == state.map_selection_scroll.window) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        DrawLegacyCustomScrollControl(state.map_selection_scroll, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }

    bool map_scroll_changed = false;
    int map_scroll_before = 0;
    int map_scroll_value = 0;
    if (id == kLinkLobbyGameListScrollId) {
        const bool changed = HandleLegacyCustomScrollControlMouseMessage(
            state.game_list_scroll, message, wparam, lparam);
        if (changed && state.game_list.window != nullptr) {
            const int top = GetLegacyCustomScrollControlValue(state.game_list_scroll);
            SendMessageA(state.game_list.window, LB_SETTOPINDEX,
                static_cast<WPARAM>(top), 0);
        }
    } else if (id == kLinkLobbyMapSelectionScrollId) {
        map_scroll_before =
            GetLegacyCustomScrollControlValue(state.map_selection_scroll);
        map_scroll_changed = HandleLegacyCustomScrollControlMouseMessage(
            state.map_selection_scroll, message, wparam, lparam);
        if (map_scroll_changed) {
            map_scroll_value =
                GetLegacyCustomScrollControlValue(state.map_selection_scroll);
            state.map_selection_index = 0x0f -
                std::clamp(map_scroll_value, 0, 0x0f);
        }
    }

    if (message == WM_PAINT) {
        if (id == kLinkLobbyStartResourceComboId) {
            PaintLegacyImageComboBoxBackground(state.start_resource_combo);
        } else if (id == kLinkLobbyScreenSizeComboId) {
            PaintLegacyImageComboBoxBackground(state.screen_size_combo);
        } else if (id == kLinkLobbyHostResourceComboId && state.game_type == 8) {
            PaintLegacyImageComboBoxBackground(state.host_resource_combo);
        } else if (id >= kLinkLobbyPlayerRoleComboFirstId &&
            id < kLinkLobbyPlayerRoleComboFirstId + kLinkLobbyAvatarCount) {
            PaintLegacyImageComboBoxBackground(
                state.player_role_combos[id - kLinkLobbyPlayerRoleComboFirstId]);
        } else if (id >= kLinkLobbyTribeComboFirstId &&
            id < kLinkLobbyTribeComboFirstId + kLinkLobbyAvatarCount) {
            PaintLegacyImageComboBoxBackground(
                state.tribe_combos[id - kLinkLobbyTribeComboFirstId]);
        }
    }
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }
    if (has_link_lobby_saved_proc_id(id)) {
        const LRESULT result = CallWindowProcA(
            original_proc_for_id(state, id), hwnd, message, wparam, lparam);
        if (id == kLinkLobbyMapSelectionScrollId && map_scroll_changed &&
            link_lobby_directplay_ready() && state.join_accepted &&
            map_scroll_value != map_scroll_before) {
            SendLinkLobbyMapSelectionPacket(state,
                static_cast<u32>(map_scroll_value), 0);
        }
        if (id == kLinkLobbyListBoxId &&
            (message == WM_MOUSEWHEEL || message == WM_VSCROLL ||
                message == WM_KEYDOWN || message == WM_LBUTTONUP)) {
            sync_game_list_scroll(state, false);
        }
        return result;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

} // namespace ranker

#endif
