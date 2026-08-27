#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <cstddef>

namespace ranker {

constexpr std::array<u32, 16> kGameplayFrameIntervalsMs = {
    45, 47, 50, 52, 55, 58, 62, 66, 71, 76, 83, 90, 100, 111, 125, 142,
};
constexpr std::array<u32, 7> kGameplayFixedStepIntervalsMs = {
    2, 5, 11, 22, 45, 71, 142,
};
constexpr std::array<u32, 7> kGameplayFixedStepRepeatCounts = {
    200, 120, 60, 30, 22, 14, 7,
};
constexpr u32 kGameplayCatchupPresentationMaxGapMs = 50;
constexpr std::size_t kGameplaySimulationPhaseCount = 17;

constexpr bool ShouldPresentGameplayCatchupFrame(u32 repeat_count,
    u32 repeat_limit, u32 current_tick_ms, u32 last_present_tick_ms) {
    return repeat_count >= repeat_limit || last_present_tick_ms == 0 ||
        static_cast<u32>(current_tick_ms - last_present_tick_ms) >=
            kGameplayCatchupPresentationMaxGapMs;
}

struct GameplayLoopState;

using GameplayLoopCallback = void (*)(GameplayLoopState& state);
using GameplayLoopGateCallback = bool (*)(GameplayLoopState& state);
using GameplayLoopTickCallback = u32 (*)(GameplayLoopState& state);

struct GameplayFramePhaseFlags {
    bool pre_update = false;
    bool gate_update = false;
    bool simulation_update = false;
    bool present_update = false;
};

struct GameplayLoopCallbacks {
    GameplayLoopCallback initialize_worker_runtime = nullptr;
    GameplayLoopCallback enter_frontend_flow = nullptr;
    GameplayLoopCallback shutdown_runtime_phase = nullptr;
    GameplayLoopCallback release_worker_runtime = nullptr;
    GameplayLoopCallback finish_worker_exit = nullptr;

    GameplayLoopTickCallback read_tick_ms = nullptr;
    GameplayLoopGateCallback sync_replay_direct_music = nullptr;
    GameplayLoopGateCallback external_turn_wait = nullptr;
    GameplayLoopGateCallback frame_gate = nullptr;
    GameplayLoopGateCallback try_restart_session = nullptr;

    GameplayLoopCallback catchup_mode_changed = nullptr;
    GameplayLoopCallback reset_catchup_status_message = nullptr;
    GameplayLoopCallback pre_update_phase = nullptr;
    GameplayLoopCallback present_phase = nullptr;
    GameplayLoopCallback end_frame_phase = nullptr;
    std::array<GameplayLoopCallback, kGameplaySimulationPhaseCount> simulation_phases{};

    GameplayLoopCallback enter_session_mode = nullptr;
    GameplayLoopCallback initialize_session_resources = nullptr;
    GameplayLoopCallback handle_restart_request = nullptr;
    GameplayLoopCallback handle_session_abort = nullptr;
    GameplayLoopCallback handle_replay_session_leave = nullptr;
    GameplayLoopCallback handle_post_victory_loop = nullptr;
    GameplayLoopCallback leave_session_cleanup = nullptr;
    GameplayLoopCallback restore_render_surfaces = nullptr;
};

struct GameplayLoopState {
    GameplayLoopCallbacks callbacks;
    std::array<u32, kGameplayFrameIntervalsMs.size()> frame_intervals =
        kGameplayFrameIntervalsMs;
    std::array<u32, kGameplayFixedStepIntervalsMs.size()> fixed_step_intervals =
        kGameplayFixedStepIntervalsMs;
    std::array<u32, kGameplayFixedStepRepeatCounts.size()> fixed_step_repeat_counts =
        kGameplayFixedStepRepeatCounts;

    GameplayFramePhaseFlags phase_flags;
    u32 current_tick_ms = 0;
    u32 frame_time_anchor = 0;
    u32 frame_interval_index = 8;
    u32 fixed_step_mode = 0;
    u32 fixed_step_repeat_counter = 0;
    u32 catchup_repeat_counter = 0;
    u32 catchup_status_counter0 = 0;
    u32 catchup_status_counter1 = 0;
    u32 catchup_last_present_tick_ms = 0;
    u32 simulation_frame_counter = 0;
    u32 present_frame_counter = 0;
    u32 exit_context = 3;
    u32 current_cursor_index = 0;
    u8 catchup_status_mode = 0;

    bool catchup_enabled = false;
    bool fixed_step_initialized = false;
    // Uncapped headless self-play: advance one simulation frame every loop
    // iteration instead of pacing to wall-clock, and present only occasionally.
    // Simulation and packet ordering are unchanged; only the frame timing is.
    bool fast_uncapped = false;
    u32 fast_present_counter = 0;
    bool external_timing_enabled = false;
    bool external_turn_wait_enabled = false;
    bool external_single_step = false;
    bool generic_ai_profile_mode = false;
    bool replay_timing_enabled = false;
    bool replay_direct_music_started = false;
    bool replay_direct_music_paused = false;
    bool replay_direct_music_status_active = false;
    bool modal_pause_suppressed = false;
    bool replay_simulation_suppressed = false;
    bool modal_subloop_active = false;
    bool session_active = false;
    bool pause_loop_requested = false;
    bool restart_requested = false;
    bool reenter_session_requested = false;
    bool leave_requested = false;
    bool process_shutdown_requested = false;
    bool special_exit_mode = false;
};

GameplayLoopState& gameplay_loop_state();

void RefreshGameplayLoopTick(GameplayLoopState& state);
void UpdateGameplayFramePhaseFlags(GameplayLoopState& state);
void ProcessGameplayFrameTick(GameplayLoopState& state);
void ProcessGameplaySessionLoop(GameplayLoopState& state, std::size_t iteration_budget = 0);
void ToggleGameplayCatchupMode(GameplayLoopState& state);
void ApplyGameplayCatchupTargetState(GameplayLoopState& state);
void EnableGameplayCatchupTargetState(GameplayLoopState& state);
void DisableGameplayCatchupTargetState(GameplayLoopState& state);
void ResetGameplayCatchupStatusMessage(GameplayLoopState& state);
void UpdateGameplayCatchupTargetIfActive(GameplayLoopState& state);
void UpdateGameplayCatchupTarget(GameplayLoopState& state);
void ExitBackgroundWorkerThreadProc(GameplayLoopState& state);

#ifdef _WIN32
DWORD WINAPI BackgroundWorkerThreadProc(LPVOID parameter);
#endif

}
