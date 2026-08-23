#pragma once

#include "ranker_types.h"

namespace ranker {

enum class GameplayModalSessionRequest : u8 {
    None = 0,
    Restart,
    Leave,
};

enum class GameplayRestartMaterialization : u8 {
    Unavailable = 0,
    LoadedSession,
    NetworkAiPractice,
    FrontendStage,
};

// ProcessGameplaySessionLoop checks DAT_00725c0c (restart) before
// DAT_00725c09 (leave).
constexpr GameplayModalSessionRequest ResolveGameplayModalSessionRequest(
    bool restart_requested, bool leave_requested) {
    if (restart_requested) {
        return GameplayModalSessionRequest::Restart;
    }
    return leave_requested
        ? GameplayModalSessionRequest::Leave
        : GameplayModalSessionRequest::None;
}

// ProcessGameplaySessionLoop's restart edge has three distinct original
// continuations.  An active Load replaces the current runtime, mode-6
// practice returns to the outer loop at 0x004d947a and reimports the retained
// map, while campaign/scenario play calls FUN_00415ad0 for the current stage.
constexpr GameplayRestartMaterialization ResolveGameplayRestartMaterialization(
    bool loaded_session_pending, bool network_ai_profile_override,
    bool frontend_stage_started) {
    if (loaded_session_pending) {
        return GameplayRestartMaterialization::LoadedSession;
    }
    if (network_ai_profile_override) {
        return GameplayRestartMaterialization::NetworkAiPractice;
    }
    return frontend_stage_started
        ? GameplayRestartMaterialization::FrontendStage
        : GameplayRestartMaterialization::Unavailable;
}

// Index zero is the 45 ms (fastest) original simulation interval.
constexpr u32 ResolveGameplayInitialSpeedIndex(bool generic_ai_profile_mode,
    bool replay_mode, u32 requested_index) {
    return !generic_ai_profile_mode && !replay_mode ? 0u : requested_index;
}

// Both original Load entry points materialize the imported records with the
// saved-session (mode 5) reset policy.  In particular, the frontend caller at
// 0x004d94a9 enters ProcessGameplaySessionLoop directly after FUN_0042cfc0;
// it does not run the fresh-skirmish unit replacement path first.
constexpr u32 ResolveGameplayMaterializationMode(u32 archive_mode,
    bool frontend_loaded_save, bool active_session_load,
    bool campaign_stage_session) {
    return frontend_loaded_save || active_session_load || campaign_stage_session
        ? 5u
        : archive_mode;
}

// Use Map Setting archives own their Computer slots.  The network participant
// count describes connected humans, not the highest authored simulation owner.
// Collapsing the archive to that count removes scripted Computer units before
// the first trigger pass (FUN_00426770 only removes raw state 0x14 owners).
constexpr u32 ResolveGameplayStartupActiveSlotCount(
    u32 archive_count, u32 lobby_count, u32 session_mode) {
    return session_mode == 5u
        ? (archive_count > lobby_count ? archive_count : lobby_count)
        : lobby_count;
}

constexpr bool IsAuthoredUseMapComputerSlot(
    u8 archive_state, u32 session_mode) {
    return session_mode == 5u && archive_state == 1u;
}

constexpr u8 ResolveGameplayStartupSlotState(u8 archive_state,
    u8 lobby_state, u32 owner, u32 lobby_count, u32 session_mode) {
    if (IsAuthoredUseMapComputerSlot(archive_state, session_mode) &&
        (owner >= lobby_count || lobby_state == 0x14u)) {
        return archive_state;
    }
    return owner < lobby_count ? lobby_state : 0x14u;
}

// FUN_004d71d7 maps a manual generic/P2P leave to result 2 before frame
// 0x708 (or while the local ready/status byte is 2), otherwise to result 1.
// It never leaves the initial victory result zero active.
constexpr u32 ResolveGameplayManualLeaveResult(
    u32 simulation_frame, bool local_player_forces_early_result) {
    return simulation_frame < 0x708u || local_player_forces_early_result
        ? 2u
        : 1u;
}

constexpr bool ResolveGameplayPauseOverlayVisible(
    bool ordered_packet_pause_visible) {
    return ordered_packet_pause_visible;
}

} // namespace ranker
