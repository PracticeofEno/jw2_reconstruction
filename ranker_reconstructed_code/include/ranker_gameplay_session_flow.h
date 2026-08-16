#pragma once

#include "ranker_gameplay_menu_control.h"
#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

// A local surrender is a match/session exit request, not a Win32 process
// shutdown request. Only the gameplay loop's explicit shutdown edge (window
// close, fatal startup failure, or worker termination) may close ranker.
constexpr bool ShouldCloseApplicationAfterP2PMatch(
    bool loop_process_shutdown_requested, bool surrender_requested) {
    (void)surrender_requested;
    return loop_process_shutdown_requested;
}

// Frontend transitions are posted across the window/worker-thread boundary.
// A stale title request must not replace a live child frontend that a newer
// post-game transition has already restored.
constexpr bool ShouldHonorTitleFrontendRequest(bool active_child_frontend) {
    return !active_child_frontend;
}

enum class GameplayPostSessionFrontendRoute : u8 {
    none = 0,
    single_player,
    direct_p2p,
    wizardnet,
};

// Network transport mode zero is also used by replay playback.  The original
// returns a replay launched from the single-player menu to that same menu; it
// must not be mistaken for a completed WizardNet match merely because both
// paths share transport mode zero.
constexpr GameplayPostSessionFrontendRoute
ResolveGameplayPostSessionFrontendRoute(u32 transition_mode,
    bool single_player_return_after_gameplay, bool close_requested,
    bool process_shutdown_requested) {
    if (close_requested || process_shutdown_requested) {
        return GameplayPostSessionFrontendRoute::none;
    }
    if (single_player_return_after_gameplay) {
        return GameplayPostSessionFrontendRoute::single_player;
    }
    if (transition_mode == 1) {
        return GameplayPostSessionFrontendRoute::direct_p2p;
    }
    if (transition_mode == 0) {
        return GameplayPostSessionFrontendRoute::wizardnet;
    }
    return GameplayPostSessionFrontendRoute::none;
}

constexpr u32 kGameplaySaveSlotCount = 8;
constexpr u32 kGameplaySaveMagicJwar = 0x5241574a;
constexpr u32 kGameplaySaveVersion = 0x97;

enum class GameplaySaveSlotStatus : u8 {
    Empty = 0,
    Valid = 1,
    Invalid = 2,
};

enum class GameplayModalResult : u32 {
    ContinueLocal = 0,
    ContinueNetwork = 1,
    Cancel = 2,
};

struct GameplaySaveSlotRecord {
    std::string path;
    GameplaySaveSlotStatus status = GameplaySaveSlotStatus::Empty;
    std::string title;
};

struct GameplayMenuItemRectTemplate {
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
};

struct GameplaySessionFlowState;

using GameplaySessionFlowCallback = void (*)(GameplaySessionFlowState& state);
using GameplaySessionFlowBoolCallback = bool (*)(GameplaySessionFlowState& state);
using GameplaySessionFlowIntCallback = int (*)(GameplaySessionFlowState& state);
using GameplaySessionFlowDispatchCallback =
    void (*)(GameplaySessionFlowState& state, int result);
using GameplaySessionFlowMusicPolicyCallback =
    void (*)(GameplaySessionFlowState& state, u32 mode);
using GameplaySaveRecordLoader = bool (*)(
    const char* path, u32 record_index, std::vector<u8>& out);
using GameplayResourceSequenceLoader = bool (*)(
    GameplaySessionFlowState& state, u32 record_count, u32 first_record,
    u32& first_resource);
using GameplayPaletteResourceSequenceLoader = bool (*)(GameplaySessionFlowState& state,
    u32 record_count, u32 first_record, u32& first_resource, u32& palette_slot);

struct GameplaySessionFlowCallbacks {
    GameplaySessionFlowCallback reset_input = nullptr;
    GameplaySessionFlowCallback set_cursor = nullptr;
    GameplaySessionFlowCallback hide_cursor = nullptr;
    GameplaySessionFlowCallback show_cursor = nullptr;
    GameplaySessionFlowCallback frame_boundary = nullptr;
    GameplaySessionFlowCallback send_p2p_game_flow_modal = nullptr;
    GameplaySessionFlowCallback send_worker_pause_modal = nullptr;
    GameplaySessionFlowCallback configure_display = nullptr;
    GameplaySessionFlowBoolCallback import_session_bundle = nullptr;
    GameplaySessionFlowCallback start_session_from_slots = nullptr;
    GameplaySessionFlowCallback pre_session_runtime = nullptr;
    GameplaySessionFlowCallback post_session_runtime = nullptr;
    GameplaySessionFlowCallback enter_session_ui = nullptr;
    GameplaySessionFlowCallback process_session_loop = nullptr;
    GameplaySessionFlowCallback cleanup_after_close = nullptr;
    GameplaySessionFlowCallback send_main_close = nullptr;
    GameplaySessionFlowCallback final_worker_cleanup = nullptr;
    GameplaySessionFlowCallback release_loaded_resources = nullptr;
    GameplaySessionFlowCallback immediate_dialog_sound = nullptr;
    GameplaySessionFlowCallback shutdown_primary_music_policy = nullptr;
    GameplaySessionFlowMusicPolicyCallback set_primary_music_policy_mode = nullptr;
    GameplaySessionFlowIntCallback run_p2p_setup_menu = nullptr;
    GameplaySessionFlowIntCallback run_local_setup_menu = nullptr;
    GameplaySessionFlowDispatchCallback dispatch_p2p_setup_result = nullptr;
    GameplaySessionFlowDispatchCallback dispatch_local_setup_result = nullptr;
    GameplaySessionFlowDispatchCallback write_p2p_result_file = nullptr;
    GameplaySaveRecordLoader load_save_record = nullptr;
    GameplayResourceSequenceLoader load_resource_sequence = nullptr;
    GameplayPaletteResourceSequenceLoader load_palette_resource_sequence = nullptr;
};

struct GameplaySessionFlowState {
    GameplaySessionFlowCallbacks callbacks;
    std::array<GameplaySaveSlotRecord, kGameplaySaveSlotCount> save_slots;
    GameplayMenuControl save_menu;
    std::vector<GameplayMenuItemRectTemplate> menu_rect_templates;
    std::string save_slot_list_text;
    GameplayModalResult modal_result = GameplayModalResult::ContinueLocal;
    u32 p2p_ready = 0;
    u32 generic_ai_profile_mode = 0;
    u32 setup_mode_flag = 0;
    u32 local_setup_flag = 0;
    u32 worker_modal_pending = 0;
    u32 worker_modal_wait_iterations = 0;
    u32 loaded_resource_base = 0xffffffffu;
    u32 loaded_palette_slot = 0xffffffffu;
    u32 selected_save_slot = 0;
    u32 immediate_dialog_sound_selector = 0;
    u32 p2p_win_result = 0;
    i32 immediate_dialog_sound_world_delta = 0;
    u32 screen_width = 800;
    u32 screen_height = 600;
    i32 menu_anchor_x = 0;
    i32 menu_anchor_y = 0;
    std::size_t session_loop_iteration_budget = 0;
    bool close_requested = false;
    bool process_shutdown_requested = false;
    bool loaded_resource_base_owned = false;
    // The single-player frontend Load command has already imported a complete
    // saved runtime.  Its materialization uses the mode-5 preservation path,
    // not the fresh skirmish path that replaces authored player units.
    bool loaded_save_session = false;
};

void RunP2PGameplayFlow(GameplaySessionFlowState& state);
void RunLocalGameSetupLoop(GameplaySessionFlowState& state);
void RunSinglePlayerGameplayFlow(GameplaySessionFlowState& state);
void RunP2PGameplaySessionAfterModal(GameplaySessionFlowState& state);
GameplayModalResult ShowWorkerModalPauseDialog(GameplaySessionFlowState& state);
void ScanGameplaySaveSlotHeaders(GameplaySessionFlowState& state);
int ShowGameplaySaveSlotActionMenu(GameplaySessionFlowState& state, u32 selected_slot);
int ShowGameplaySaveSlotListMenu(GameplaySessionFlowState& state);
u32 LoadJw202ResourceSequenceFromCurrentBase(GameplaySessionFlowState& state,
    u32 record_count, u32 first_record);
u32 LoadJw202PaletteBoundResourceSequence(GameplaySessionFlowState& state,
    u32 record_count, u32 first_record);
void PrepareGameplaySaveSlotMenuLayout(GameplaySessionFlowState& state);

} // namespace ranker
