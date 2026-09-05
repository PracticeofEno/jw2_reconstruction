#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_image_controls.h"

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

struct GameSessionUnitReferenceTables;
struct LegacySocketRecord;
struct SessionRuntimeDefinitionTableSet;
struct SessionRuntimeImportState;

#ifdef _WIN32

constexpr UINT kP2PLobbyNetworkPayloadMessage = 0x50a;
constexpr UINT kP2PLobbyStartGameMessage = 0x50b;
constexpr UINT kP2PLobbyFormatStatusMessage = 0x50c;
constexpr UINT kP2PLobbyStaticStatusMessage = 0x50d;
constexpr UINT kP2PLobbySocketNotifyMessage = 0x50f;

constexpr int kP2PLobbyInfoControlId = 0x1964;
constexpr int kP2PLobbyHostButtonId = 0x1965;
constexpr int kP2PLobbyJoinButtonId = 0x1966;
constexpr int kP2PLobbyNameEditId = 0x1967;
constexpr int kP2PLobbyLocalAddressEditId = 0x1968;
constexpr int kP2PLobbyRemoteAddressEditId = 0x1969;
constexpr int kP2PLobbyCancelButtonId = IDCANCEL;
constexpr int kP2PLobbyAcceleratorResourceId = 0x28a;
constexpr UINT_PTR kP2PLobbyJoinTimerId = 1;
constexpr UINT kP2PLobbyJoinRetryMs = 5000;
constexpr u32 kP2PLobbyLayoutTrcRecord = 0x16b;
constexpr u32 kP2PLobbyInstructionTextTrcRecord = 0x15b;
constexpr u32 kP2PLobbyBackgroundBitmapTrcRecord = 0x53;
constexpr u32 kP2PLobbyInfoNormalBitmapRecord = 0;
constexpr u32 kP2PLobbyInfoPressedBitmapRecord = 0;
constexpr u32 kP2PLobbyCancelNormalBitmapRecord = 0x54;
constexpr u32 kP2PLobbyCancelPressedBitmapRecord = 0x55;
constexpr u32 kP2PLobbyHostNormalBitmapRecord = 0x56;
constexpr u32 kP2PLobbyHostPressedBitmapRecord = 0x57;
constexpr u32 kP2PLobbyJoinNormalBitmapRecord = 0x58;
constexpr u32 kP2PLobbyJoinPressedBitmapRecord = 0x59;
constexpr std::size_t kP2PNetworkLaunchPlayerNameBytes = 0x40;
constexpr std::size_t kP2PNetworkLaunchPasswordBytes = 0x40;
constexpr std::size_t kP2PNetworkLaunchMapPathBytes = 0x104;
constexpr std::size_t kP2PNetworkLaunchRemoteAddressBytes = 0x20;
constexpr std::size_t kP2PStartParameterPayloadBytes = 0x1f9c;
constexpr std::size_t kP2PGameResultMaxPlayers = 8;
constexpr std::size_t kP2PGameResultNameBytes = 0x14;
constexpr std::size_t kP2PGameResultNetworkNameBytes = 0x80;
constexpr std::size_t kP2PGameSessionPlayerNameBytes = 0x20;
constexpr std::size_t kP2PGameSessionRuntimeFlagSlots = 10;
constexpr std::size_t kP2PGameSessionReadyBytes = 0x20;

struct P2PLobbyState;

struct P2PNetworkLaunchParameters {
    std::array<char, kP2PNetworkLaunchPlayerNameBytes> player_name{};
    std::array<char, kP2PNetworkLaunchPasswordBytes> password{};
    std::array<char, kP2PNetworkLaunchMapPathBytes> map_path{};
    std::array<char, kP2PNetworkLaunchRemoteAddressBytes> remote_address{};
    bool uses_map_file = false;
    bool valid = false;
    // Local AI self-play autostart requested via -AISELF.  When set, the
    // command-line host flow fills the opponent slot with Computer(AI)
    // (Link role 4) and auto-starts the lobby without waiting for a peer so a
    // headless harness can run unattended matches.
    bool self_play = false;
    // -AIDRAW: force DirectDraw/DirectSound init in self-play (debug escape
    // hatch).  Default self-play is NO-DRAW: headless training instances do
    // not touch graphics/audio devices, so dozens can run beside an
    // interactive viewer without starving it (black-screen contention).
    bool self_play_draw = false;
    // Optional -MAXFRAMES:N cap for a self-play match; 0 means use the default.
    u32 self_play_max_frames = 0;
    // -AIOUT:DIR — redirect the self-play output files (result / reward trace /
    // episode / observation JSON) into this directory instead of the CWD, so
    // many parallel rollout workers sharing one game-data CWD do not collide on
    // them.  Empty = write to CWD (default).  Must be whitespace-free.
    std::array<char, kP2PNetworkLaunchMapPathBytes> self_play_output_dir{};
    // -AINET:N — shift this instance's P2P TCP/UDP ports by N so parallel
    // self-play workers do not collide on the host listen port.  0 = default.
    u16 self_play_net_offset = 0;
    // -AIVS — policy-vs-policy self-play: owner 2 becomes a second Computer(AI)
    // (packet-controlled) instead of a built-in Computer, so two RL policies
    // play each other head-to-head (the self-play league building block).
    bool self_play_versus = false;
    // -AISCRIPT: drive the Computer(AI) owner with the scripted packet policy
    // instead of the built-in Owner AI baseline (for A/B strength comparison).
    bool self_play_scripted = false;
    // -AIRANDOM: RL plumbing validation.  The owner is handed to the packet
    // controller (implies self_play_scripted) but instead of the scripted
    // policy's decision it samples a uniformly-random *legal* high-level action
    // each cycle and translates it through the hierarchical micro executor.
    bool self_play_random = false;
    // -AIIMITATE: keep the Computer(AI) owner on the built-in Owner AI but log
    // its per-decision observations to ai_rl_observe.jsonl for imitation
    // learning (behavior-cloning the built-in AI into the high-level policy).
    bool self_play_imitate = false;
    // -AISHADOW: entity-command RL shadow dataset (plan section 13.1): the
    // scripted micro keeps controlling the match while its pre-dedupe desired
    // orders are recorded as per-unit KEEP/ISSUE labels at the 8-frame entity
    // cadence (ai_entity_shadow_<owner>.bin under -AIOUT).
    bool self_play_shadow = false;
    // -AISHADOW2 — ENTCMD02 SHD2 teacher dataset (same live scripted micro,
    // labels = economy + combat teacher orders on the v2 snapshot).
    bool self_play_shadow2 = false;
    // -AIIMITOWNER:N — with -AIREPLAY + -AIIMITATE, the recorded owner slot to
    // log (a human player's replay).  0xff = the replay's own local player.
    u32 self_play_imitate_owner = 0xffu;
    // -AIENTITY:PORT — entity-command RL controller (plan section 11): the
    // controlled owners speak the binary act2 protocol to a Python policy
    // server on 127.0.0.1:PORT.  Direct per-unit control; the old fighter
    // objective executor is forced OFF and the autopilot runs economy-only
    // (scout guard disabled — plan section 4.2).  0 = disabled.
    u16 self_play_entity_port = 0;
    // -AIACT3:PORT — ENTCMD02 (act3) direct economy + combat entity-command
    // controller on 127.0.0.1:PORT (docs/AI_PLAY_ENTCMD02_DIRECT_ECONOMY_PLAN.md).
    // The scripted macro stage and the worker executor are fully OFF for the
    // controlled owners.  0 = disabled.  Mutually exclusive with -AIENTITY.
    u16 self_play_act3_port = 0;
    // -AIWORKERFLOOR:N — economy A/B lever: override the autopilot's
    // worker-floor rule target (how many workers it keeps alive).  0 = the
    // AiAutopilotConfig default.  Applied in the entity transaction only.
    u32 self_play_worker_floor = 0;
    // -AIOPPSLOW:N — training curriculum lever: the built-in Computer
    // owners think only every Nth simulation frame (their maintenance tick
    // is skipped otherwise), smoothly weakening them so a learning policy
    // sees winnable games; anneal N toward 1 as the win rate rises.
    // 0/1 = full strength.  Self-play sessions only.
    u32 self_play_opponent_slow = 0;
    // -AIREVEALBASE — training curriculum lever: seed each hostile owner's
    // START tile into the fog building-memory while it is still unexplored
    // (real sightings take over once the tile is lit).  Removes the
    // scout-survival lottery that otherwise gates every knowledge-based
    // behavior; anneal OFF once the policy closes reliably.
    bool self_play_reveal_base = false;
    // -AIIPC:PORT — online policy-in-the-loop: the Computer(AI) owner asks a
    // Python policy server on 127.0.0.1:PORT for each high-level action instead
    // of sampling randomly.  0 = disabled.
    u16 self_play_ipc_port = 0;
    // -AI1V1: clean 1v1 evaluation.  The local owner (0) runs the scripted
    // policy against a single built-in Computer (owner 1) with all other starts
    // closed, so the match result is a direct scripted-vs-built-in win/loss.
    bool self_play_1v1 = false;
    // -SEED:N deterministic RNG seed for self-play (reproducible matches; the
    // normal path seeds from wall-clock time).  0 means use the default.
    u32 self_play_seed = 0;
    // -AIREPLAY:PATH — headless replay playback verification: load this .ply
    // and play it back under the self-play harness (fast_uncapped, result
    // JSON at the end) so recorded games can be machine-checked for playback
    // fidelity instead of eyeballing them in the client.
    std::array<char, kP2PNetworkLaunchMapPathBytes> self_play_replay_path{};
    // -AITRIBE:N — tribe of the built-in Computer opponent in self-play/1v1
    // (0=Primitive 1=Elf 2=Tyrano 3=Demon; 4=derive from -SEED so a seed sweep
    // rotates opponents).  Default 2 (Tyrano mirror, the historic behavior).
    // The Computer(AI) owners themselves stay Tyrano — the executor only
    // speaks that tech tree.
    u32 self_play_opponent_tribe = 2;
    // v9 runtime switches: -AIAUTOPILOT:0/1 macro
    // autopilot (worker floor / pop guard / idle-producer guard),
    // -AIREFLEX:0/1 executor base-defense reflex, -AIGATE:0/1 event decision
    // gate (0 = decide every min_interval like the pre-v9 behavior).  All
    // default ON; the 0 settings are the A/B measurement lever.
    bool self_play_autopilot = true;
    bool self_play_reflex = true;
    bool self_play_gate = true;
};

enum class P2PGameWinResult : u32 {
    Win = 0,
    Lose = 1,
    Draw = 2,
    Disconnect = 3,
};

enum class P2PGameEndReason : u32 {
    NoError = 0,
    NoGameplayAbort = 1,
    GameCanceledByHost = 2,
    MapFileError = 3,
    ConnectCancelFailClient = 4,
    NetworkError = 5,
};

struct P2PGameResultPlayer {
    std::array<char, kP2PGameResultNameBytes> result_name{};
    std::array<char, kP2PGameResultNetworkNameBytes> network_name{};
    u32 faction = 0;
    u32 selected_tribe = 0;
    u32 ally_mask = 0;
    bool playing = false;
};

struct P2PGameResultFileInput {
    std::array<P2PGameResultPlayer, kP2PGameResultMaxPlayers> players{};
    const char* client_player_name = "";
    u32 version_packed = 0;
    u32 local_player_slot = 0;
    u32 active_player_count = 0;
    u32 connected_player_count = 0;
};

struct P2PGameSessionPlayerSetup {
    std::array<char, kP2PGameSessionPlayerNameBytes> name{};
    u8 owner_slot = 0xff;
};

struct P2PReplayVposRecord {
    u32 frame = 0;
    u16 camera_x = 0;
    u16 camera_y = 0;
};

struct P2PDelayedGameplayPacket {
    std::array<u8, 0x24> bytes{};
    u32 due_frame = 0;
    u32 serial = 0;
    u32 flags1 = 0;
    u32 flags2 = 0;
    u8 owner_slot = 0;
    u8 subtype = 0;
};

using P2PGameSessionSimpleCallback = void (*)(void* user_data);
using P2PGameSessionPlayMusicCallback = bool (*)(const char* path, void* user_data);
using P2PGameSessionSetMusicVolumeCallback = void (*)(int volume_percent, void* user_data);
using P2PGameSessionGetMusicMsCallback = int (*)(void* user_data);
using P2PDelayedGameplayPacketCallback = void (*)(P2PDelayedGameplayPacket& packet,
    void* user_data);

struct P2PGameSessionStartCallbacks {
    P2PGameSessionSimpleCallback apply_ai_profile_mode1 = nullptr;
    P2PGameSessionSimpleCallback apply_ai_profile_mode2 = nullptr;
    P2PGameSessionPlayMusicCallback play_direct_music = nullptr;
    P2PGameSessionSetMusicVolumeCallback set_direct_music_volume = nullptr;
    P2PGameSessionGetMusicMsCallback get_direct_music_current_ms = nullptr;
    void* user_data = nullptr;
};

struct P2PGameSessionStartInput {
    SessionRuntimeImportState* import_state = nullptr;
    SessionRuntimeDefinitionTableSet* active_definitions = nullptr;
    const SessionRuntimeDefinitionTableSet* staged_definitions = nullptr;
    GameSessionUnitReferenceTables* reference_tables = nullptr;
    std::array<P2PGameSessionPlayerSetup, kP2PGameResultMaxPlayers> players{};
    std::array<u8, kP2PStartParameterPayloadBytes> start_parameter_payload{};
    std::string map_path;
    u32 network_player_count = 0;
    u32 copied_runtime_local_player = 0;
    u32 ai_profile_mode = 0;
    bool import_nonempty_staged_definitions = false;
    bool start_parameter_payload_present = false;
    bool scenario_ai_profile_override = false;
};

struct P2PGameSessionStartState {
    std::array<std::array<char, kP2PGameSessionPlayerNameBytes>, kP2PGameResultMaxPlayers>
        player_names{};
    std::array<u32, kP2PGameResultMaxPlayers> network_index_by_owner{};
    std::array<u32, kP2PGameResultMaxPlayers> delayed_packet_serial_by_owner{};
    std::array<u32, kP2PGameSessionRuntimeFlagSlots> slot_flags{};
    std::array<u8, kP2PGameSessionReadyBytes> ready_bytes{};
    std::array<u8, 8> replay_vpos_seed{};
    std::array<u8, kP2PStartParameterPayloadBytes> start_parameter_payload{};
    std::vector<P2PReplayVposRecord> replay_vpos_records;
    std::vector<P2PDelayedGameplayPacket> delayed_packets;
    std::string replay_vpos_path;
    std::string direct_music_path;
    u32 network_player_count = 0;
    u32 copied_runtime_local_player = 0;
    u32 local_player_slot = 9;
    u32 route_state = 4;
    u32 setup_flags = 0;
    u32 all_players_mask = 0xffffffffu;
    u32 ai_profile_mode = 0;
    u32 replay_vpos_count = 0;
    u32 replay_vpos_cursor = 0xffffffffu;
    u32 replay_target_frame_count = 0;
    u32 direct_music_idle_counter = 0;
    u32 camera_x = 0;
    u32 camera_y = 0;
    int direct_music_current_ms = 0;
    u32 direct_music_frame_offset = 0;
    bool generic_ai_profile_mode = false;
    bool network_ai_profile_override = false;
    bool scenario_ai_profile_override = false;
    bool replay_vpos_loaded = false;
    bool apply_replay_vpos_camera = true;
    bool replay_vpos_camera_dirty = false;
    bool direct_music_started = false;
    bool direct_music_paused = false;
    bool direct_music_volume_enabled = true;
    bool game_end_requested = false;
    bool music_phase_toggle = false;
    bool start_parameter_payload_present = false;
};

struct P2PLobbyCallbacks {
    bool (*initialize_network)(P2PLobbyState& state) = nullptr;
    void (*shutdown_network)(P2PLobbyState& state) = nullptr;
    bool (*start_join)(P2PLobbyState& state, const char* remote_address,
        DWORD app_context, HWND notify_window, UINT notify_message) = nullptr;
    bool (*continue_join)(P2PLobbyState& state) = nullptr;
    bool (*start_host)(P2PLobbyState& state) = nullptr;
    void (*cancel_connection)(P2PLobbyState& state) = nullptr;
    void (*handle_socket_event)(P2PLobbyState& state, WPARAM socket,
        LPARAM event) = nullptr;
    void (*handle_network_payload)(P2PLobbyState& state, WPARAM sender,
        LPARAM payload) = nullptr;
    void (*handle_prompt_result)(P2PLobbyState& state, UINT message) = nullptr;
    void (*show_message)(HWND owner, const char* text, COLORREF color) = nullptr;
    void (*return_to_parent)(HWND parent, HINSTANCE instance, LPARAM context) = nullptr;
    void (*start_game)(HWND parent, HINSTANCE instance, LPARAM context) = nullptr;
};

struct P2PLobbyLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct P2PLobbyState {
    BitmapMemoryResource background;
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;

    HWND name_edit = nullptr;
    HWND local_address_edit = nullptr;
    HWND remote_address_edit = nullptr;
    LegacyImageButtonControl info_control;
    LegacyImageButtonControl cancel_button;
    LegacyImageButtonControl host_button;
    LegacyImageButtonControl join_button;

    WNDPROC original_name_edit_proc = nullptr;
    WNDPROC original_local_address_proc = nullptr;
    WNDPROC original_remote_address_proc = nullptr;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;
    HFONT ui_font = nullptr;

    std::array<char, 0x20> player_name{};
    std::array<char, 0x80> local_address{};
    std::array<char, 0x80> remote_address{};
    std::string instruction_text;
    std::string last_status_text;
    std::vector<u8> network_receive_buffer;

    SOCKET join_socket = INVALID_SOCKET;
    u16 default_tcp_port = 0;
    UINT_PTR join_timer = 0;
    bool join_pending = false;
    bool start_game_requested = false;
    bool visible = false;
    P2PLobbyCallbacks callbacks;
};

P2PLobbyState& p2p_lobby_state();
P2PNetworkLaunchParameters& p2p_network_launch_parameters();

void InitializeP2PLobbySupport(P2PLobbyState& state);
void InitializeP2PLobbyBackgroundBitmap(P2PLobbyState& state);
void RegisterP2PLobbyBackgroundShutdown(P2PLobbyState& state);
void ShutdownP2PLobbyBackgroundBitmap(P2PLobbyState& state);
void InitializeP2PLobbyImageButtonSlot0();
void ConstructP2PLobbyImageButtonSlot0();
void RegisterP2PLobbyImageButtonSlot0Shutdown();
void DestroyP2PLobbyImageButtonSlot0();
void InitializeP2PLobbyImageButtonSlot1();
void ConstructP2PLobbyImageButtonSlot1();
void RegisterP2PLobbyImageButtonSlot1Shutdown();
void DestroyP2PLobbyImageButtonSlot1();
void InitializeP2PLobbyImageButtonSlot2();
void ConstructP2PLobbyImageButtonSlot2();
void RegisterP2PLobbyImageButtonSlot2Shutdown();
void DestroyP2PLobbyImageButtonSlot2();
void InitializeP2PLobbyImageButtonSlot3();
void ConstructP2PLobbyImageButtonSlot3();
void RegisterP2PLobbyImageButtonSlot3Shutdown();
void DestroyP2PLobbyImageButtonSlot3();

void InstallP2PLobbyAccelerators(P2PLobbyState& state);
void RestoreP2PLobbyAccelerators(P2PLobbyState& state);

HRESULT StartP2PLobbyJoinAttempt(P2PLobbyState& state, const char* display_name,
    const char* remote_address, DWORD app_context);
bool CreateP2PLobbyWindow(P2PLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context);
LRESULT HandleP2PLobbyWindowMessage(P2PLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT HandleP2PLobbyControlMessage(P2PLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
void HandleP2PLobbyConnectResult(P2PLobbyState& state, WPARAM sender, LPARAM result);
void DispatchP2PLobbyNetworkPayload(P2PLobbyState& state, WPARAM sender,
    LPARAM payload);
void PumpP2PLobbyNetworkBuffer(P2PLobbyState& state, WPARAM sender,
    LegacySocketRecord& network_buffer);
void ApplyP2PLobbySocketEvent(P2PLobbyState& state, WPARAM socket,
    LPARAM event);
void HandleP2PLobbySocketNotification(P2PLobbyState& state, WPARAM socket,
    LPARAM event);
void ResetP2PNetworkLaunchParameters(P2PNetworkLaunchParameters& parameters);
bool ParseP2PNetworkCommandLine(P2PNetworkLaunchParameters& parameters,
    const char* command_line);
bool ParseP2PNetworkCommandLine(const char* command_line);
bool WriteP2PGameResultFile(const P2PGameResultFileInput& input,
    P2PGameWinResult win_result, P2PGameEndReason end_reason,
    const char* path = "Result.txt");
bool PrepareP2PGameSessionStart(P2PGameSessionStartState& state,
    const P2PGameSessionStartInput& input,
    const P2PGameSessionStartCallbacks& callbacks = {});
bool TickP2PDirectMusicSync(P2PGameSessionStartState& state, u32 gameplay_frame,
    u32 target_frame_count, u32 music_length_ms, u32 music_status);
u8 GetBoundedP2PSetupValue(u8 value);
std::string FormatP2PGameplayClockText(u32 gameplay_frame, u32 target_frame_count,
    const char* format);
std::string FormatP2PRouteStateText(u32 route_state, const char* format,
    const char* const* labels, std::size_t label_count);
void TickP2PReplayVposCamera(P2PGameSessionStartState& state, u32 gameplay_frame);
u32 PumpP2PDelayedGameplayPackets(P2PGameSessionStartState& state, u32 gameplay_frame,
    P2PDelayedGameplayPacketCallback dispatch_packet = nullptr,
    void* user_data = nullptr);

#endif

}
