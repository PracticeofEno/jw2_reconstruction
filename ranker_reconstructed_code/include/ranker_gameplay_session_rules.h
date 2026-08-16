#pragma once

#include "ranker_types.h"

namespace ranker {

enum class GameplayModalSessionRequest : u8 {
    None = 0,
    Restart,
    Leave,
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
