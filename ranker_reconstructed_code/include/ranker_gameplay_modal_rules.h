#pragma once

#include "ranker_types.h"

namespace ranker {

// DAT_00725bf8 is zero for the original local/single-player profile.  Only
// that profile exposes the in-game Save and Load commands.
constexpr bool GameplayPauseMenuSaveLoadEnabled(bool generic_ai_profile_mode) {
    return !generic_ai_profile_mode;
}

// Disabled original controls retain their sprite/text draw flags; state -1
// selects the disabled sprite/color.  Clearing the flags hides the control
// completely instead of presenting an unavailable menu item.
constexpr u32 GameplayMenuEntryFlagsForEnabledState(
    u32 original_flags, bool enabled) {
    (void)enabled;
    return original_flags;
}

// FUN_0042d5f0 brackets every blocking local submenu with a back-surface
// snapshot.  The generic/P2P path polls its menus and must not use that stack.
constexpr bool GameplayPauseMenuUsesChildSnapshot(bool generic_ai_profile_mode) {
    return !generic_ai_profile_mode;
}

// The synchronized Pause/Resume command belongs only to the generic/P2P
// profile, is unavailable during replay playback, and becomes
// disabled when the local player has exhausted the original pause allowance.
constexpr bool GameplayPauseMenuModalPauseEnabled(
    bool replay_mode, bool generic_ai_profile_mode,
    bool modal_pause_suppressed, u8 pause_uses_remaining) {
    return !replay_mode && generic_ai_profile_mode &&
        (modal_pause_suppressed || pause_uses_remaining != 0);
}

// HandleUiScreenInputTick at 0x00504b10 restores every idle, non-disabled,
// non-pressed entry outside the pointer to state zero.  Restricting this to
// hover state two leaves alternate/button states visually stuck.
constexpr bool ShouldRestoreIdleUiScreenEntryState(
    i32 entry_state, bool pointer_inside) {
    return entry_state != -1 && entry_state != 1 && !pointer_inside;
}

} // namespace ranker
