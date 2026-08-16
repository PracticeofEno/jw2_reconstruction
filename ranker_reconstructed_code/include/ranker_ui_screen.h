#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

struct SpriteRenderTarget;

constexpr u32 kUiScreenEntryBytes = 0x2a4;
constexpr u32 kUiScreenBlobSlots = 10;
constexpr u32 kInvalidUiScreenIndex = 0xffffffffu;
constexpr u32 kJw204StageMissionStride = 0x50;
constexpr u32 kJw204StageFactionStride = 0x14;

// JW2_04.TRC is mission-major (P, Elf, Tyrano, Demon inside each row),
// unlike the faction-major JW2_06.TRC session archive.
constexpr u32 Jw204StageScreenRecordIndex(i32 faction, i32 mission) {
    return static_cast<u32>(mission) * kJw204StageMissionStride +
        static_cast<u32>(faction) * kJw204StageFactionStride;
}

static_assert(Jw204StageScreenRecordIndex(0, 0) == 0);
static_assert(Jw204StageScreenRecordIndex(1, 0) == 20);
static_assert(Jw204StageScreenRecordIndex(2, 0) == 40);
static_assert(Jw204StageScreenRecordIndex(3, 0) == 60);
static_assert(Jw204StageScreenRecordIndex(0, 1) == 80);
static_assert(Jw204StageScreenRecordIndex(3, 7) == 620);

struct UiScreenEntry {
    std::array<u8, kUiScreenEntryBytes> bytes{};
};

struct UiScreenBinkEntryState {
    void* handle = nullptr;
    const void* source = nullptr;
    u64 fallback_started_tick_ms = 0;
#ifdef _WIN32
    HWND fallback_window = nullptr;
    HWND fallback_surface_window = nullptr;
    void* fallback_player = nullptr;
    void* fallback_callback = nullptr;
    std::wstring fallback_temp_path;
    bool fallback_com_initialized = false;
    bool fallback_media_foundation_started = false;
#endif
    bool paused = false;
};

struct UiScreenDefinition {
    std::string source_archive_name;
    u32 source_record_index = kInvalidUiScreenIndex;
    u32 operation_count = 0;
    u32 entry_count = 0;
    std::vector<UiScreenEntry> entries;
    i32 selected_index = -1;
    bool scroll_tracking = false;
    u32 active_scroll_entry = kInvalidUiScreenIndex;
    u32 last_scroll_tick = 0;
    bool text_edit_active = false;
    u32 text_edit_entry_index = 0;
    u32 palette_mark = kInvalidUiScreenIndex;
    u32 resource_mark = kInvalidUiScreenIndex;
    u32 sound_mark = kInvalidUiScreenIndex;
    u32 scroll_flags = 0;
    bool bink_initialized = false;
    u32 bink_surface_type = 0;
    std::vector<UiScreenBinkEntryState> bink_entries;
    std::array<std::vector<u8>, kUiScreenBlobSlots> embedded_blobs;
    std::array<u32, kUiScreenBlobSlots> embedded_blob_sizes{};
    u32 embedded_blob_count = 0;
    bool use_custom_text_renderer = false;
    bool loaded = false;
    u32 skipped_bink_entries = 0;
    u32 skipped_rect_entries = 0;
};

constexpr std::size_t kGlobalUiScreenSlotCount = 15;
constexpr std::size_t kGlobalUiScreenModalFlagCount = 7;

struct GlobalUiScreenSlotsState {
    std::array<UiScreenDefinition, kGlobalUiScreenSlotCount> screens;
    std::array<bool, kGlobalUiScreenSlotCount> shutdown_registered{};
    std::array<bool, kGlobalUiScreenModalFlagCount> modal_resource_active{};
};

constexpr std::size_t kGameplayModalPlayerSlots = 8;
constexpr std::size_t kGameplayModalSaveSlotCount = 8;

enum class GameplayModalSaveSlotState : u8 {
    Empty = 0,
    Occupied = 1,
    Invalid = 2,
};

enum class GameplayModalUiActionKind : u32 {
    None = 0,
    ScreenLoaded,
    ScreenReleased,
    DrawActiveScreen,
    CorrectiveChecksumPublished,
    WaitConsensusMaskPublished,
    SessionImportRequested,
    SessionExportRequested,
    RuntimeTablesImported,
    ModalPausePublished,
    PlayerInactivePublished,
    RelationMaskPublished,
    SetupDataApplied,
    CatchupToggled,
    UnitResourcePackToggled,
    QuitRequested,
    SurrenderRequested,
    WorkerExitRequested,
    MessageShown,
};

struct GameplayModalUiState;

using GameplayModalUiCallback = void (*)(GameplayModalUiState& state);
using GameplayModalUiBoolCallback = bool (*)(GameplayModalUiState& state);
using GameplayModalUiScreenCallback =
    bool (*)(GameplayModalUiState& state, UiScreenDefinition& screen, u32 record_index);
using GameplayModalUiScreenVoidCallback =
    void (*)(GameplayModalUiState& state, UiScreenDefinition& screen);
using GameplayModalUiPumpCallback =
    u32 (*)(GameplayModalUiState& state, UiScreenDefinition& screen, int& entry_state);
using GameplayModalUiSlotCallback = bool (*)(GameplayModalUiState& state, u32 slot_index);
using GameplayModalUiMessageCallback =
    void (*)(GameplayModalUiState& state, const char* message);
using GameplayModalUiMaskCallback =
    void (*)(GameplayModalUiState& state, u32 relation_mask, u32 visibility_mask,
        bool observer_flag);
using GameplayModalUiValueCallback = void (*)(GameplayModalUiState& state, u32 value);

struct GameplayModalUiCallbacks {
    GameplayModalUiScreenCallback load_screen = nullptr;
    GameplayModalUiScreenVoidCallback release_screen = nullptr;
    GameplayModalUiScreenVoidCallback center_screen = nullptr;
    GameplayModalUiScreenVoidCallback draw_screen = nullptr;
    GameplayModalUiPumpCallback run_modal = nullptr;
    GameplayModalUiPumpCallback poll_modal = nullptr;
    GameplayModalUiCallback scan_save_slot_headers = nullptr;
    GameplayModalUiSlotCallback import_session_bundle = nullptr;
    GameplayModalUiSlotCallback export_session_bundle = nullptr;
    GameplayModalUiCallback reload_skirmish_profiles = nullptr;
    GameplayModalUiCallback clear_reliable_packet_rings = nullptr;
    GameplayModalUiCallback import_runtime_tables = nullptr;
    GameplayModalUiCallback import_non_empty_runtime_tables = nullptr;
    GameplayModalUiCallback rebuild_unit_type_references = nullptr;
    GameplayModalUiCallback publish_corrective_checksum = nullptr;
    GameplayModalUiCallback publish_modal_pause = nullptr;
    GameplayModalUiCallback reset_and_publish_inactive = nullptr;
    GameplayModalUiMaskCallback publish_relation_mask = nullptr;
    GameplayModalUiCallback exit_worker_thread = nullptr;
    GameplayModalUiCallback pause_music = nullptr;
    GameplayModalUiCallback resume_music = nullptr;
    GameplayModalUiCallback apply_music_volume = nullptr;
    GameplayModalUiCallback apply_setup_data = nullptr;
    GameplayModalUiCallback update_catchup_target = nullptr;
    GameplayModalUiCallback toggle_catchup = nullptr;
    GameplayModalUiCallback toggle_unit_resource_pack = nullptr;
    GameplayModalUiBoolCallback pump_wait_dialog = nullptr;
    GameplayModalUiMessageCallback show_message = nullptr;
    GameplayModalUiValueCallback send_replay_modal_action = nullptr;
    GameplayModalUiCallback trc_fatal_error = nullptr;
};

struct GameplayModalSaveSlot {
    GameplayModalSaveSlotState state = GameplayModalSaveSlotState::Empty;
    std::string label;
};

struct GameplayModalPlayerSlot {
    u8 slot_state = 0;
    u8 modal_pause_uses_remaining = 4;
    std::string display_name;
    u32 relation_mask = 0;
    u32 visibility_mask = 0;
    bool wait_ready = false;
};

struct GameplayModalUiAction {
    GameplayModalUiActionKind kind = GameplayModalUiActionKind::None;
    u32 value0 = 0;
    u32 value1 = 0;
    std::string text;
};

struct GameplayModalUiControlState {
    u32 entry_index = 0;
    i32 state = 0;
    u32 flags = 0;
    std::string text;
    bool enabled = true;
};

struct GameplayModalUiState {
    GameplayModalUiCallbacks callbacks;
    std::array<GameplayModalSaveSlot, kGameplayModalSaveSlotCount> save_slots;
    std::array<GameplayModalPlayerSlot, kGameplayModalPlayerSlots> players;
    std::vector<GameplayModalUiAction> action_log;
    std::vector<GameplayModalUiControlState> relation_controls;
    std::vector<GameplayModalUiControlState> observer_controls;
    std::vector<GameplayModalUiControlState> wait_controls;

    u32 selected_faction_id = 0;
    u32 selected_save_slot = 0;
    u32 local_player_index = 0;
    u32 local_network_address = 0;
    u32 center_width = 800;
    u32 center_height = 600;
    u32 fallback_center_height = 600;
    u32 gameplay_frame_counter = 0;
    u32 transport_mode = 0;
    u32 session_mode = 0;

    u32 pending_relation_mask = 0;
    u32 pending_visibility_mask = 0;
    u32 pending_observer_mode = 0;
    u32 pending_observer_mask = 0;
    u32 committed_observer_mode = 0;
    u32 committed_observer_mask = 0;
    u32 compact_observer_flags = 0;
    u32 stage_hover_hint = 0x25;
    u32 wait_threshold_ms = 0;
    u32 wait_required_packet_count = 0;
    std::array<u32, kGameplayModalPlayerSlots> wait_packet_counts{};
    std::array<u32, kGameplayModalPlayerSlots> wait_elapsed_ms{};
    u32 last_loaded_record = 0;
    u32 last_activated_entry = 0;
    int last_entry_state = 0;

    int music_volume_left = 0;
    int music_volume_right = 0;
    int music_volume_raw = 0;
    int sound_volume_raw = 0;
    int scroll_speed = 0;

    std::string empty_save_label = "Empty";
    std::string invalid_save_label = "Invalid";
    std::string overwrite_marker = "(*)";
    std::string import_error_message = "Unable to load saved game.";
    std::string cancel_confirm_message = "Cancel load?";
    std::string export_error_message = "Unable to save game.";
    std::string default_message_text;
    std::string default_objective_text = "Destroy all enemy buildings.";
    std::string scenario_message_text;
    std::string fallback_scenario_message_text;
    std::string network_address_text;
    std::string network_address_format = "Ver %d-%d-%d";
    std::string wait_remaining_format = "%s - %d Sec remain.";
    std::string post_result_text;
    std::string fatal_archive_name;
    u32 fatal_record_index = 0;

    bool main_menu_active = false;
    bool scenario_message_active = false;
    bool exit_surrender_active = false;
    bool options_active = false;
    bool relation_mask_active = false;
    bool observer_mask_active = false;
    bool wait_dialog_active = false;
    bool generic_ai_profile_mode = false;
    bool network_ai_profile_override = false;
    bool scenario_ai_profile_override = false;
    bool stage_archive_present = true;
    bool centered_for_replay = false;
    bool replay_mode = false;
    bool modal_pause_suppressed = false;
    bool require_cancel_confirmation = false;
    bool previous_music_paused = false;
    bool music_paused = false;
    bool catchup_enabled = false;
    bool relation_observer_flag = false;
    bool sound_options_available = true;
    bool quit_to_frontend_requested = false;
    bool surrender_requested = false;
    bool worker_exit_requested = false;
    bool end_session_requested = false;
    bool non_empty_runtime_tables_available = false;
    bool local_player_waiting = false;
    bool unit_resource_pack_variant = false;
    bool replay_modal_pending = false;
};

using UiScreenModalPumpCallback = bool (*)(
    UiScreenDefinition& screen, int& result_code, int& auxiliary_code, void* user_data);

GlobalUiScreenSlotsState& global_ui_screen_slots_state();
UiScreenDefinition& GlobalUiScreenSlot(std::size_t slot);
void InitializeGlobalUiScreenSlotDefinition(std::size_t slot);
void RegisterGlobalUiScreenSlotShutdown(std::size_t slot);
void ReleaseGlobalUiScreenSlotDefinition(std::size_t slot);
void InitializeGlobalUiScreenSlotSupport(std::size_t slot);
void SetGlobalUiScreenModalResourceActive(std::size_t flag, bool active);
void ReleaseActiveGlobalUiScreenModalResources();

GameplayModalUiState& gameplay_modal_ui_state();
void PumpActiveGameplayModalUiFlow(GameplayModalUiState& state);
void PumpActiveGameplayModalUiFlow();
void DrawActiveGameplayModalUiScreen(GameplayModalUiState& state);
void DrawActiveGameplayModalUiScreen();
void OpenLocalNetworkAddressDialogWithInputReset(GameplayModalUiState& state);
void OpenLocalNetworkAddressDialogWithInputReset();
void OpenLocalNetworkAddressDialog(GameplayModalUiState& state);
void OpenLocalNetworkAddressDialog();
void OpenStageAvailabilityDialog(
    GameplayModalUiState& state, bool keep_loaded = false);
void OpenStageAvailabilityDialog();
void CloseStageAvailabilityDialog(GameplayModalUiState& state);
bool OpenSkirmishLoadSessionDialog(GameplayModalUiState& state);
bool OpenSkirmishLoadSessionDialog();
void OpenGameplaySimpleInfoDialog(GameplayModalUiState& state);
void OpenGameplaySimpleInfoDialog();
void OpenGameplayIndexedSelectionDialog(GameplayModalUiState& state, u32 selected_index);
void OpenGameplayIndexedSelectionDialog(u32 selected_index);
bool OpenGameplayPauseMenu(GameplayModalUiState& state);
bool OpenGameplayPauseMenu();
bool OpenGameplayLoadSessionDialog(GameplayModalUiState& state);
bool OpenGameplayLoadSessionDialog();
void RefreshGameplaySaveSlotLabels(GameplayModalUiState& state, UiScreenDefinition& screen);
void RefreshGameplaySaveSlotLabels(GameplayModalUiState& state);
void RefreshGameplaySaveSlotLabels();
bool OpenGameplaySaveSessionDialog(GameplayModalUiState& state);
bool OpenGameplaySaveSessionDialog();
bool OpenGameplayExitSurrenderDialog(GameplayModalUiState& state);
bool OpenGameplayExitSurrenderDialog();
void OpenGameplayMessageDialog(GameplayModalUiState& state, const char* message);
void OpenGameplayMessageDialog(const char* message);
bool OpenGameplayScenarioMessageDialog(GameplayModalUiState& state);
bool OpenGameplayScenarioMessageDialog();
bool OpenGameplayOptionsDialog(GameplayModalUiState& state);
bool OpenGameplayOptionsDialog();
void RefreshGameplayRelationMaskDialogControls(GameplayModalUiState& state,
    u32 relation_mask, u32 visibility_mask, bool observer_flag);
void RefreshGameplayRelationMaskDialogControls(u32 relation_mask, u32 visibility_mask,
    bool observer_flag);
bool OpenGameplayRelationMaskDialog(GameplayModalUiState& state);
bool OpenGameplayRelationMaskDialog();
void RefreshGameplayObserverMaskDialogControls(GameplayModalUiState& state, u32 mode,
    u32 mask);
void RefreshGameplayObserverMaskDialogControls(u32 mode, u32 mask);
bool OpenGameplayObserverMaskDialog(GameplayModalUiState& state);
bool OpenGameplayObserverMaskDialog();
void RefreshGameplayWaitDialogControls(GameplayModalUiState& state);
void RefreshGameplayWaitDialogControls();
inline u32 BuildGameplayWaitConsensusMask(const GameplayModalUiState& state) {
    u32 mask = 0;
    for (u32 player = 0; player < kGameplayModalPlayerSlots; ++player) {
        // FUN_0042c8b0 votes only for entries whose original
        // DAT_011b5a3c wait budget reached zero.
        if (state.wait_elapsed_ms[player] == 0) {
            mask |= 1u << player;
        }
    }
    return mask;
}
bool OpenGameplayWaitDialog(GameplayModalUiState& state);
bool OpenGameplayWaitDialog();
bool PollGameplayWaitDialog(GameplayModalUiState& state);
bool PollGameplayWaitDialog();
void CloseGameplayWaitDialog(GameplayModalUiState& state);
void CloseGameplayWaitDialog();
void OpenGameplayPostResultDialog(GameplayModalUiState& state);
void OpenGameplayPostResultDialog();
void OpenReplayControlModalDialog(GameplayModalUiState& state);
void OpenReplayControlModalDialog();
void SendReplayModalAction2AndWait(GameplayModalUiState& state);
void SendReplayModalAction2AndWait();
void OpenGameplayResultTextDialog(GameplayModalUiState& state, const char* text = nullptr);
void OpenGameplayResultTextDialog(const char* text = nullptr);
void DispatchActiveTrcRecordFatalErrorMirror(GameplayModalUiState& state);
void HandleGameplayTrcFatalLoadError(GameplayModalUiState& state);
void HandleGameplayTrcFatalLoadError();

void InitializeGlobalUiScreenSlot00Support();
void InitializeGlobalUiScreenSlot00Definition();
void RegisterGlobalUiScreenSlot00Shutdown();
void ReleaseGlobalUiScreenSlot00Definition();
void InitializeGlobalUiScreenSlot01Support();
void InitializeGlobalUiScreenSlot01Definition();
void RegisterGlobalUiScreenSlot01Shutdown();
void ReleaseGlobalUiScreenSlot01Definition();
void InitializeGlobalUiScreenSlot02Support();
void InitializeGlobalUiScreenSlot02Definition();
void RegisterGlobalUiScreenSlot02Shutdown();
void ReleaseGlobalUiScreenSlot02Definition();
void InitializeGlobalUiScreenSlot03Support();
void InitializeGlobalUiScreenSlot03Definition();
void RegisterGlobalUiScreenSlot03Shutdown();
void ReleaseGlobalUiScreenSlot03Definition();
void InitializeGlobalUiScreenSlot04Support();
void InitializeGlobalUiScreenSlot04Definition();
void RegisterGlobalUiScreenSlot04Shutdown();
void ReleaseGlobalUiScreenSlot04Definition();
void InitializeGlobalUiScreenSlot05Support();
void InitializeGlobalUiScreenSlot05Definition();
void RegisterGlobalUiScreenSlot05Shutdown();
void ReleaseGlobalUiScreenSlot05Definition();
void InitializeGlobalUiScreenSlot06Support();
void InitializeGlobalUiScreenSlot06Definition();
void RegisterGlobalUiScreenSlot06Shutdown();
void ReleaseGlobalUiScreenSlot06Definition();
void InitializeGlobalUiScreenSlot07Support();
void InitializeGlobalUiScreenSlot07Definition();
void RegisterGlobalUiScreenSlot07Shutdown();
void ReleaseGlobalUiScreenSlot07Definition();
void InitializeGlobalUiScreenSlot08Support();
void InitializeGlobalUiScreenSlot08Definition();
void RegisterGlobalUiScreenSlot08Shutdown();
void ReleaseGlobalUiScreenSlot08Definition();
void InitializeGlobalUiScreenSlot09Support();
void InitializeGlobalUiScreenSlot09Definition();
void RegisterGlobalUiScreenSlot09Shutdown();
void ReleaseGlobalUiScreenSlot09Definition();
void InitializeGlobalUiScreenSlot10Support();
void InitializeGlobalUiScreenSlot10Definition();
void RegisterGlobalUiScreenSlot10Shutdown();
void ReleaseGlobalUiScreenSlot10Definition();
void InitializeGlobalUiScreenSlot11Support();
void InitializeGlobalUiScreenSlot11Definition();
void RegisterGlobalUiScreenSlot11Shutdown();
void ReleaseGlobalUiScreenSlot11Definition();
void InitializeGlobalUiScreenSlot12Support();
void InitializeGlobalUiScreenSlot12Definition();
void RegisterGlobalUiScreenSlot12Shutdown();
void ReleaseGlobalUiScreenSlot12Definition();
void InitializeGlobalUiScreenSlot13Support();
void InitializeGlobalUiScreenSlot13Definition();
void RegisterGlobalUiScreenSlot13Shutdown();
void ReleaseGlobalUiScreenSlot13Definition();
void InitializeGlobalUiScreenSlot14Support();
void InitializeGlobalUiScreenSlot14Definition();
void RegisterGlobalUiScreenSlot14Shutdown();
void ReleaseGlobalUiScreenSlot14Definition();

void InitializeUiScreenDefinition(UiScreenDefinition& screen);
void HandleUiScreenDefinitionReleaseWrapper(UiScreenDefinition& screen);

#ifdef _WIN32
bool HandleUiScreenDefinitionFileImport(UiScreenDefinition& screen, const char* path);
bool HandleUiScreenEmbeddedBlobRead(UiScreenDefinition& screen, HANDLE file, u32 byte_count);
#endif

bool HandleUiScreenDefinitionTrcImport(UiScreenDefinition& screen, const char* archive_name,
    u32 record_index);
void HandleUiScreenDefinitionResourceRelease(UiScreenDefinition& screen);
void HandleUiScreenEntriesCentering(UiScreenDefinition& screen, i32 width, i32 height);
bool HandleUiScreenDefinitionDraw(UiScreenDefinition& screen, HDC dc = nullptr);
bool HandleUiScreenInputTick(UiScreenDefinition& screen, u32& activated_entry_index,
    int& entry_state);
bool RunUiScreenModalPump(UiScreenDefinition& screen, u32& activated_entry_index,
    int& entry_state);
bool DrawUiScreenResourceSprite(u32 resource_index, i32 x, i32 y);
bool DrawUiScreenResourceSprite(
    const UiScreenDefinition& screen, u32 resource_index, i32 x, i32 y);
bool HandleUiScreenBinkEntryRender(UiScreenDefinition& screen, u32 entry_index);
bool RestartUiScreenFlaggedBinkEntries(UiScreenDefinition& screen);
bool PlayJw204BinkMenuScreen(i32 column, i32 row,
    UiScreenModalPumpCallback pump_callback = nullptr, void* user_data = nullptr);
bool DrawBackBufferRectangleOutline16(i32 left, i32 top, i32 width, i32 height,
    u16 color = 0xffffu);
bool PutBackBufferPixel16Clipped(i32 x, i32 y, u16 color);
bool BlitBackBufferPixels16(const u16* pixels, u32 source_pitch_pixels,
    u32 width, u32 height, i32 destination_x, i32 destination_y);
bool DrawBackBufferFilledRectangle16(i32 left, i32 top, i32 right, i32 bottom,
    u16 color = 0xffffu);
bool DrawBackBufferStippledRectangle16(i32 left, i32 top, i32 right, i32 bottom,
    u16 color = 0xffffu);
bool DarkenBackBufferRectangle16(i32 left, i32 top, i32 right, i32 bottom);
bool DrawSpriteRenderTargetLine16(const SpriteRenderTarget& target,
    i32 x0, i32 y0, i32 x1, i32 y1, u16 color = 0xffffu);
bool DrawBackBufferLine16(i32 x0, i32 y0, i32 x1, i32 y1, u16 color = 0xffffu);
bool OrBackBufferMask32x32(i32 left, i32 top, u16 mask);
bool OrBackBufferHighRedMask32x32(i32 left, i32 top);
bool OrBackBufferLowBlueMask32x32(i32 left, i32 top);
bool DrawUiScreenRectangleOutline(const UiScreenEntry& entry, u16 color = 0xffffu);
bool HandleUiScreenStateSound(const UiScreenDefinition& screen, u32 entry_index);
bool DrawUiScreenStatusSprite(const UiScreenDefinition& screen, const UiScreenEntry& entry);
bool DrawUiScreenStatusSprite(const UiScreenEntry& entry);
bool DrawUiScreenScrollBar(const UiScreenDefinition& screen, const UiScreenEntry& entry);
bool DrawUiScreenScrollBar(const UiScreenEntry& entry);
bool HandleUiScreenScrollPress(UiScreenDefinition& screen, u32 entry_index,
    i32 mouse_x, i32 mouse_y, u32 tick_ms = 0);
void HandleUiScreenHoverNoop(const UiScreenEntry& entry);

#ifdef _WIN32
bool DrawUiScreenTextEntry(const UiScreenDefinition& screen, HDC dc,
    const UiScreenEntry& entry);
#endif

i32 UiScreenEntryI32(const UiScreenEntry& entry, std::size_t offset);
void SetUiScreenEntryI32(UiScreenEntry& entry, std::size_t offset, i32 value);

}
