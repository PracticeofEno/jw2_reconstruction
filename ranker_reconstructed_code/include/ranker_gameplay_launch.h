#pragma once

#include "ranker_types.h"

#include <cstddef>

namespace ranker {

// The original front-end enters gameplay through one synchronous route and
// consumes that route's globals immediately.  Keep the reconstructed launch
// source explicit so stale state owned by another retained window cannot win
// an archive/theme fallback search.
enum class GameplayLaunchSource : u8 {
    none = 0,
    replay,
    command_line_p2p,
    link_lobby,
    wizard_session,
};

constexpr bool GameplayLaunchUsesReplay(GameplayLaunchSource source) {
    return source == GameplayLaunchSource::replay;
}

constexpr bool GameplayLaunchUsesCommandLineP2P(GameplayLaunchSource source) {
    return source == GameplayLaunchSource::command_line_p2p;
}

constexpr bool GameplayLaunchUsesLinkLobby(GameplayLaunchSource source) {
    return source == GameplayLaunchSource::link_lobby ||
        source == GameplayLaunchSource::command_line_p2p;
}

constexpr bool GameplayLaunchUsesWizardSession(GameplayLaunchSource source) {
    return source == GameplayLaunchSource::wizard_session;
}

// Live peer launches spend a comparatively long interval between destroying
// the native Link room and producing the first gameplay composite. Replays and
// local frontend stages do not use that network handoff.
constexpr bool GameplayLaunchUsesLivePeerConnection(GameplayLaunchSource source) {
    return GameplayLaunchUsesLinkLobby(source) ||
        GameplayLaunchUsesWizardSession(source);
}

constexpr u32 ResolveReplayGameplayTheme(const u8* payload,
    std::size_t byte_count, u32 fallback = 0) {
    constexpr std::size_t kReplayLocalPlayerOffset = 0x5f;
    constexpr std::size_t kReplayMetadataOffset = 0x63;
    constexpr std::size_t kReplayTribeChoicesOffset = 0x50;
    if (payload == nullptr || byte_count <= kReplayLocalPlayerOffset) {
        return fallback;
    }
    const std::size_t local_player = payload[kReplayLocalPlayerOffset];
    const std::size_t tribe_offset = kReplayMetadataOffset +
        kReplayTribeChoicesOffset + local_player;
    if (local_player >= 8 || tribe_offset >= byte_count) {
        return fallback;
    }
    const u32 theme = payload[tribe_offset];
    return theme <= 3 ? theme : fallback;
}

} // namespace ranker
