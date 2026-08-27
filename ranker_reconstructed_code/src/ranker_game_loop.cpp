#include "ranker_game_loop.h"

#include "ranker_cursor.h"
#include "ranker_input.h"

#include <algorithm>

namespace ranker {
namespace {

GameplayLoopState g_gameplay_loop_state;

u32 interval_at(const std::array<u32, kGameplayFrameIntervalsMs.size()>& intervals,
    u32 index) {
    if (index >= intervals.size()) {
        index = static_cast<u32>(intervals.size() - 1);
    }
    return std::max<u32>(1, intervals[index]);
}

u32 fixed_interval_at(const GameplayLoopState& state) {
    const std::size_t index = std::min<std::size_t>(
        state.fixed_step_mode, state.fixed_step_intervals.size() - 1);
    return std::max<u32>(1, state.fixed_step_intervals[index]);
}

u32 fixed_repeat_count_at(const GameplayLoopState& state) {
    const std::size_t index = std::min<std::size_t>(
        state.fixed_step_mode, state.fixed_step_repeat_counts.size() - 1);
    return state.fixed_step_repeat_counts[index];
}

void set_phase_flags(GameplayLoopState& state, bool pre_update, bool gate_update,
    bool simulation_update, bool present_update) {
    state.phase_flags.pre_update = pre_update;
    state.phase_flags.gate_update = gate_update;
    state.phase_flags.simulation_update = simulation_update;
    state.phase_flags.present_update = present_update;
}

void update_bucketed_frame_flags(GameplayLoopState& state, u32 bucket) {
    if (state.frame_time_anchor == bucket) {
        set_phase_flags(state, false, false, false, true);
        return;
    }

    state.frame_time_anchor = bucket;
    if (!state.external_single_step && !state.replay_simulation_suppressed) {
        set_phase_flags(state, true, true, true, true);
    }
    else {
        set_phase_flags(state, true, true, false, true);
    }
}

bool update_fixed_step_frame_flags(GameplayLoopState& state,
    bool honor_replay_pause) {
    if (state.fixed_step_mode > 3) {
        state.fixed_step_initialized = false;
        state.frame_interval_index = (state.fixed_step_mode - 4) * 8;
        const u32 bucket = state.current_tick_ms /
            interval_at(state.frame_intervals, state.frame_interval_index);
        update_bucketed_frame_flags(state, bucket);
        return true;
    }

    const u32 fixed_interval = fixed_interval_at(state);
    if (!state.fixed_step_initialized) {
        state.fixed_step_initialized = true;
        state.frame_time_anchor = state.current_tick_ms + fixed_interval;
    }
    if (state.current_tick_ms < state.frame_time_anchor + fixed_interval) {
        set_phase_flags(state, false, false, false, true);
        return true;
    }

    state.frame_time_anchor += fixed_interval;
    ++state.fixed_step_repeat_counter;
    set_phase_flags(state, true, true, true, false);

    if (honor_replay_pause && state.replay_direct_music_paused) {
        state.fixed_step_initialized = false;
        set_phase_flags(state, true, false, false, true);
        return true;
    }

    if (state.external_single_step) {
        state.fixed_step_initialized = false;
        set_phase_flags(state, true, false, false, true);
        return true;
    }

    if (state.replay_simulation_suppressed) {
        state.fixed_step_repeat_counter = 0;
        set_phase_flags(state, true, true, false, true);
        return true;
    }

    const bool force_present =
        state.fixed_step_repeat_counter >= fixed_repeat_count_at(state);
    state.phase_flags.present_update = force_present;
    if (force_present) {
        state.fixed_step_repeat_counter = 0;
    }
    return true;
}

bool update_replay_frame_flags(GameplayLoopState& state) {
    if (!state.replay_timing_enabled) {
        return false;
    }

    state.replay_simulation_suppressed =
        state.modal_pause_suppressed || state.replay_direct_music_paused;
    if (state.replay_direct_music_started) {
        bool hold_frame = state.replay_direct_music_status_active;
        if (state.callbacks.sync_replay_direct_music != nullptr) {
            hold_frame = state.callbacks.sync_replay_direct_music(state);
            state.replay_direct_music_status_active = hold_frame;
        }
        if (hold_frame) {
            set_phase_flags(state, false, false, false, true);
            return true;
        }

        if (state.replay_direct_music_paused) {
            state.fixed_step_initialized = false;
            set_phase_flags(state, true, false, false, true);
            return true;
        }

        set_phase_flags(state, true, true, true, false);
        return true;
    }

    return update_fixed_step_frame_flags(state, true);
}

void leave_session_cleanup(GameplayLoopState& state);

void reset_session_transient_loop_state(GameplayLoopState& state) {
    // ProcessGameplaySessionLoop is entered again after a completed direct-P2P
    // match without recreating the process-global loop object.  The original
    // outer loop clears these per-session BSS fields before the next map
    // enters.  Keeping the previous result/modal and fixed-step bookkeeping
    // can leave the replacement session in a present-only or modal subloop
    // state even though its simulation was initialized successfully.
    state.phase_flags = GameplayFramePhaseFlags{};
    state.fixed_step_repeat_counter = 0;
    state.catchup_repeat_counter = 0;
    state.catchup_status_counter0 = 0;
    state.catchup_status_counter1 = 0;
    state.catchup_last_present_tick_ms = 0;
    state.catchup_status_mode = 0;
    state.fixed_step_initialized = false;
    state.external_single_step = false;
    state.replay_direct_music_started = false;
    state.replay_direct_music_paused = false;
    state.replay_direct_music_status_active = false;
    state.modal_pause_suppressed = false;
    state.replay_simulation_suppressed = false;
    state.modal_subloop_active = false;
    state.session_active = false;
    state.pause_loop_requested = false;
    state.restart_requested = false;
    state.reenter_session_requested = false;
    state.leave_requested = false;
    state.special_exit_mode = false;
}

void process_restart_or_leave(GameplayLoopState& state) {
    state.external_single_step = false;
    state.pause_loop_requested = false;
    state.modal_pause_suppressed = false;
    state.replay_simulation_suppressed = false;
    state.session_active = false;

    if (state.replay_timing_enabled) {
        if (state.callbacks.handle_replay_session_leave != nullptr) {
            state.callbacks.handle_replay_session_leave(state);
        }
        else {
            leave_session_cleanup(state);
        }
        return;
    }

    if (state.external_timing_enabled) {
        if (state.callbacks.handle_session_abort != nullptr) {
            state.callbacks.handle_session_abort(state);
        }
        if (state.callbacks.shutdown_runtime_phase != nullptr) {
            state.callbacks.shutdown_runtime_phase(state);
        }
        leave_session_cleanup(state);
        return;
    }

    if (state.modal_subloop_active) {
        if (state.callbacks.handle_post_victory_loop != nullptr) {
            state.callbacks.handle_post_victory_loop(state);
        }
        leave_session_cleanup(state);
        return;
    }

    if (state.process_shutdown_requested) {
        state.exit_context = 1;
        leave_session_cleanup(state);
        return;
    }

    if (state.callbacks.restore_render_surfaces != nullptr) {
        state.callbacks.restore_render_surfaces(state);
    }
}

void enter_session_mode(GameplayLoopState& state) {
    if (state.callbacks.enter_session_mode != nullptr) {
        state.callbacks.enter_session_mode(state);
        return;
    }
    state.modal_subloop_active = false;
    state.special_exit_mode = false;
}

void leave_session_cleanup(GameplayLoopState& state) {
    if (state.callbacks.leave_session_cleanup != nullptr) {
        state.callbacks.leave_session_cleanup(state);
        return;
    }
    ResetInputState();
}

} // namespace

GameplayLoopState& gameplay_loop_state() {
    return g_gameplay_loop_state;
}

void RefreshGameplayLoopTick(GameplayLoopState& state) {
    if (state.callbacks.read_tick_ms != nullptr) {
        state.current_tick_ms = state.callbacks.read_tick_ms(state);
        return;
    }
#ifdef _WIN32
    state.current_tick_ms = GetTickCount();
#endif
}

void UpdateGameplayFramePhaseFlags(GameplayLoopState& state) {
    set_phase_flags(state, false, false, false, false);
    RefreshGameplayLoopTick(state);

    if (update_replay_frame_flags(state)) {
        return;
    }

    if (state.fast_uncapped) {
        // Simulate every iteration with no wall-clock wait; present sparsely so
        // the headless window keeps pumping messages without paying the D3D
        // present cost each frame.
        ++state.fast_present_counter;
        const bool present = (state.fast_present_counter & 0x3fu) == 0u;
        set_phase_flags(state, true, true, true, present);
        return;
    }

    const u32 frame_interval = interval_at(state.frame_intervals, state.frame_interval_index);
    if (!state.external_timing_enabled) {
        if (state.catchup_enabled) {
            const u32 next_tick = state.frame_time_anchor + frame_interval;
            if (state.current_tick_ms < next_tick) {
                set_phase_flags(state, false, false, false, true);
                state.catchup_repeat_counter = 0;
                state.catchup_last_present_tick_ms = state.current_tick_ms;
                return;
            }

            ++state.catchup_repeat_counter;
            state.frame_time_anchor = next_tick;
            if (!state.external_single_step && !state.replay_simulation_suppressed) {
                // The original forces one presentation every 50 catch-up
                // simulations. Reconstructed Use Map Setting ticks are more
                // expensive than the original's raw in-place object loop, so
                // frame-count-only pacing can leave the display unchanged for
                // seconds on the Fast Computer setting. Preserve that rule
                // while also bounding the wall-clock display gap. Simulation
                // and packet ordering are unchanged.
                const bool force_present = ShouldPresentGameplayCatchupFrame(
                    state.catchup_repeat_counter, 0x32,
                    state.current_tick_ms,
                    state.catchup_last_present_tick_ms);
                set_phase_flags(state, true, true, true, force_present);
                if (force_present) {
                    state.catchup_repeat_counter = 0;
                    state.catchup_last_present_tick_ms = state.current_tick_ms;
                }
                return;
            }

            state.catchup_repeat_counter = 0;
            set_phase_flags(state, true, true, false, true);
            return;
        }

        const u32 bucket = state.current_tick_ms / frame_interval;
        update_bucketed_frame_flags(state, bucket);
        return;
    }

    if (!state.external_turn_wait_enabled) {
        update_fixed_step_frame_flags(state, false);
        return;
    }
    else if (state.callbacks.external_turn_wait != nullptr &&
        state.callbacks.external_turn_wait(state)) {
        set_phase_flags(state, false, false, false, true);
        return;
    }
    else if (!state.external_single_step) {
        set_phase_flags(state, true, true, true, false);
        return;
    }

    state.fixed_step_initialized = false;
    set_phase_flags(state, true, false, false, true);
}

void ProcessGameplayFrameTick(GameplayLoopState& state) {
    UpdateGameplayFramePhaseFlags(state);

    if (state.phase_flags.pre_update && state.callbacks.pre_update_phase != nullptr) {
        state.callbacks.pre_update_phase(state);
    }

    if (state.phase_flags.gate_update && state.callbacks.frame_gate != nullptr &&
        !state.callbacks.frame_gate(state)) {
        if (state.phase_flags.present_update && state.callbacks.present_phase != nullptr) {
            ++state.present_frame_counter;
            state.callbacks.present_phase(state);
        }
        if (state.callbacks.end_frame_phase != nullptr) {
            state.callbacks.end_frame_phase(state);
        }
        return;
    }

    if (state.phase_flags.simulation_update) {
        ++state.simulation_frame_counter;
        for (GameplayLoopCallback callback : state.callbacks.simulation_phases) {
            if (callback != nullptr) {
                callback(state);
            }
        }
    }

    if (state.phase_flags.present_update) {
        ++state.present_frame_counter;
        if (state.callbacks.present_phase != nullptr) {
            state.callbacks.present_phase(state);
        }
    }

    if (state.callbacks.end_frame_phase != nullptr) {
        state.callbacks.end_frame_phase(state);
    }
}

void ProcessGameplaySessionLoop(GameplayLoopState& state, std::size_t iteration_budget) {
    std::size_t iteration = 0;
    for (;;) {
        reset_session_transient_loop_state(state);
        ResetInputState();
        state.session_active = true;
        state.exit_context = 3;
        enter_session_mode(state);
        if (state.callbacks.initialize_session_resources != nullptr) {
            state.callbacks.initialize_session_resources(state);
        }
        RefreshGameplayLoopTick(state);
        state.frame_time_anchor = state.current_tick_ms;

        while (state.session_active) {
            ProcessGameplayFrameTick(state);

            if (state.leave_requested) {
                state.external_single_step = false;
                state.modal_pause_suppressed = false;
                state.replay_simulation_suppressed = false;
                state.leave_requested = false;
                if (state.modal_subloop_active) {
                    if (state.callbacks.handle_post_victory_loop != nullptr) {
                        state.callbacks.handle_post_victory_loop(state);
                    }
                    state.leave_requested = false;
                    continue;
                }
                if (state.process_shutdown_requested) {
                    if (state.callbacks.handle_session_abort != nullptr) {
                        state.callbacks.handle_session_abort(state);
                    }
                    leave_session_cleanup(state);
                    return;
                }
                break;
            }

            if (state.restart_requested) {
                state.external_single_step = false;
                state.modal_pause_suppressed = false;
                state.replay_simulation_suppressed = false;
                state.restart_requested = false;
                state.session_active = false;
                if (state.process_shutdown_requested) {
                    return;
                }
                if (state.callbacks.try_restart_session != nullptr) {
                    const bool restarted = state.callbacks.try_restart_session(state);
                    if (state.reenter_session_requested) {
                        break;
                    }
                    if (restarted) {
                        if (state.callbacks.restore_render_surfaces != nullptr) {
                            state.callbacks.restore_render_surfaces(state);
                        }
                        state.session_active = true;
                        continue;
                    }
                }
                break;
            }

            if (state.pause_loop_requested) {
                process_restart_or_leave(state);
                return;
            }

            if (iteration_budget != 0 && ++iteration >= iteration_budget) {
                return;
            }
        }

        if (state.process_shutdown_requested) {
            if (state.callbacks.handle_session_abort != nullptr) {
                state.callbacks.handle_session_abort(state);
            }
            leave_session_cleanup(state);
            return;
        }

        if (!state.reenter_session_requested &&
            state.callbacks.handle_restart_request != nullptr) {
            state.callbacks.handle_restart_request(state);
        }

        if (state.reenter_session_requested) {
            continue;
        }

        if (state.callbacks.restore_render_surfaces != nullptr) {
            state.callbacks.restore_render_surfaces(state);
        }
        return;
    }
}

void ToggleGameplayCatchupMode(GameplayLoopState& state) {
    state.catchup_enabled = !state.catchup_enabled;
    if (state.callbacks.catchup_mode_changed != nullptr) {
        state.callbacks.catchup_mode_changed(state);
    }
}

void ApplyGameplayCatchupTargetState(GameplayLoopState& state) {
    if (state.catchup_enabled) {
        EnableGameplayCatchupTargetState(state);
        return;
    }
    DisableGameplayCatchupTargetState(state);
}

void EnableGameplayCatchupTargetState(GameplayLoopState& state) {
    state.catchup_enabled = true;
    ResetGameplayCatchupStatusMessage(state);
    UpdateGameplayCatchupTarget(state);
}

void DisableGameplayCatchupTargetState(GameplayLoopState& state) {
    state.catchup_enabled = false;
    state.catchup_last_present_tick_ms = 0;
    ResetGameplayCatchupStatusMessage(state);
}

void ResetGameplayCatchupStatusMessage(GameplayLoopState& state) {
    state.catchup_repeat_counter = 0;
    state.fixed_step_repeat_counter = 0;
    if (state.callbacks.reset_catchup_status_message != nullptr) {
        state.callbacks.reset_catchup_status_message(state);
    }
}

void UpdateGameplayCatchupTargetIfActive(GameplayLoopState& state) {
    if (!state.generic_ai_profile_mode && state.catchup_enabled) {
        UpdateGameplayCatchupTarget(state);
    }
}

void UpdateGameplayCatchupTarget(GameplayLoopState& state) {
    RefreshGameplayLoopTick(state);
    state.catchup_last_present_tick_ms = state.current_tick_ms;
    state.frame_time_anchor = state.current_tick_ms +
        interval_at(state.frame_intervals, state.frame_interval_index);
}

void ExitBackgroundWorkerThreadProc(GameplayLoopState& state) {
    if (state.callbacks.shutdown_runtime_phase != nullptr) {
        state.callbacks.shutdown_runtime_phase(state);
    }
    if (state.callbacks.release_worker_runtime != nullptr) {
        state.callbacks.release_worker_runtime(state);
    }
    if (state.callbacks.finish_worker_exit != nullptr) {
        state.callbacks.finish_worker_exit(state);
    }
}

#ifdef _WIN32
DWORD WINAPI BackgroundWorkerThreadProc(LPVOID parameter) {
    auto* supplied_state = static_cast<GameplayLoopState*>(parameter);
    GameplayLoopState& state = supplied_state != nullptr ? *supplied_state : gameplay_loop_state();
    if (state.callbacks.initialize_worker_runtime != nullptr) {
        state.callbacks.initialize_worker_runtime(state);
    }
    SetGameCursorIndex(state.current_cursor_index);
    ShowGameCursor();
    if (state.callbacks.enter_frontend_flow != nullptr) {
        state.callbacks.enter_frontend_flow(state);
    }
    if (state.callbacks.shutdown_runtime_phase != nullptr) {
        state.callbacks.shutdown_runtime_phase(state);
    }
    if (state.callbacks.release_worker_runtime != nullptr) {
        state.callbacks.release_worker_runtime(state);
    }
    return 0;
}
#endif

} // namespace ranker
