#pragma once

#include "ranker_types.h"

#include <array>

namespace ranker {

constexpr u32 kPlayerSlotCount = 8;
constexpr u32 kPlayerOwnerResourceSlots = 0x14;
// P_PLAYE uses 9 while replay playback has no player-owned viewpoint.  It is
// not an array index: preserve it in the local-owner globals so camera,
// visibility and local-only world feedback follow the original observer path.
constexpr u32 kNoLocalPlayerSlot = 9;

constexpr bool IsPlayerOrNoLocalSlot(u32 slot) {
    return slot < kPlayerSlotCount || slot == kNoLocalPlayerSlot;
}

constexpr u32 NormalizePlayerOrNoLocalSlot(u32 slot) {
    return IsPlayerOrNoLocalSlot(slot) ? slot : kNoLocalPlayerSlot;
}

enum class PlayerSlotState : u8 {
    active = 0,
    player_controlled = 1,
    observer = 2,
    rotation_reserve = 3,
    disabled = 0x14,
};

struct PlayerSlotRuntimeState;

using OwnerSlotTransferCallback = void (*)(PlayerSlotRuntimeState& state, u32 from_slot,
    u32 to_slot);
using PlayerSlotTargetRefreshCallback = void (*)(PlayerSlotRuntimeState& state, u32 owner);
using PlayerInactiveBroadcastCallback =
    void (*)(PlayerSlotRuntimeState& state, u32 target_slot, u32 source_slot);

struct PlayerSlotRuntimeState {
    std::array<u8, kPlayerSlotCount> slot_states{};
    std::array<u32, kPlayerSlotCount> owner_relation_masks{};
    std::array<u32, kPlayerSlotCount> owner_visibility_masks{};
    std::array<i32, kPlayerSlotCount> owner_start_x{};
    std::array<i32, kPlayerSlotCount> owner_start_y{};
    std::array<u32, kPlayerSlotCount> nearest_hostile_slots{};
    std::array<u32, kPlayerOwnerResourceSlots> owner_primary_resources{};
    std::array<u32, kPlayerOwnerResourceSlots> owner_secondary_resources{};
    std::array<u32, kPlayerOwnerResourceSlots> owner_aux_resources{};
    std::array<u32, kPlayerSlotCount> owner_cooldown_timers{};
    u32 global_active_slot_mask = 0;
    u32 local_observer_slot_mask = 0;
    u32 active_slot_count = kPlayerSlotCount;
    u32 local_player_slot = 0;
    bool rotation_enabled = false;
    u32 rotation_countdown_ticks = 0;
    u32 rotation_reset_units = 0;
    u32 rotation_anchor_slot = 0;
    u32 rotation_control_value = 0;
    u32 inactive_target_slot = 0;
    u32 inactive_source_slot = 0;
    bool rotation_countdown_decrements = false;
    bool local_observer_interaction_enabled = true;
    bool inactive_broadcast_latched = false;
    std::array<u8, kPlayerSlotCount> inactive_publish_states{};
    OwnerSlotTransferCallback transfer_owner_slot = nullptr;
    PlayerSlotTargetRefreshCallback refresh_owner_target = nullptr;
    PlayerInactiveBroadcastCallback broadcast_player_inactive = nullptr;
    // DAT_012448F0 is the lobby/rotation state table.  It is distinct from
    // slot_states (DAT_007251F4), which remains the live gameplay state table.
    // 0xff is an internal compatibility sentinel for callers that only seed
    // the legacy slot_states member before entering the slot APIs.
    std::array<u8, kPlayerSlotCount> lobby_slot_states{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
};

constexpr u32 ResolveLocalOwnerRelationMask(
    const PlayerSlotRuntimeState& state, u32 local_owner) {
    // Replay owner 9 indexes the original raw relation table's ninth row,
    // which remains 0xffffffff. PlayerSlotRuntimeState intentionally stores
    // only the eight simulation-owner rows, so restore that sentinel row for
    // presentation/hover/chat consumers instead of treating it as zero.
    if (local_owner == kNoLocalPlayerSlot) {
        return 0xffffffffu;
    }
    return local_owner < state.owner_relation_masks.size()
        ? state.owner_relation_masks[local_owner]
        : 0u;
}

constexpr void ApplyReplayNoLocalPlayerVisibility(
    PlayerSlotRuntimeState& state) {
    // FUN_0044f2c0 changes the replay viewpoint owner to 9, then BTS-sets
    // that observer bit in the raw visibility rows for owners 0..7.  The
    // relation masks are deliberately unchanged.
    constexpr u32 observer_bit = 1u << kNoLocalPlayerSlot;
    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        state.owner_visibility_masks[owner] |= observer_bit;
    }
}

PlayerSlotRuntimeState& player_slot_state();
void BindPlayerSlotRuntimeState(PlayerSlotRuntimeState* state);
void ResetPlayerSlotRuntime(PlayerSlotRuntimeState& state);
void CopyRotationAnchorResourcesToLocalPlayer(PlayerSlotRuntimeState& state);
bool TransferPlayerSlotOwnershipAndState(PlayerSlotRuntimeState& state, u32 from_slot,
    u32 to_slot);
void RotateLowerTeamReservePlayerSlot(PlayerSlotRuntimeState& state);
void RotateUpperTeamReservePlayerSlot(PlayerSlotRuntimeState& state);
void ResetTeamReserveRotationCountdownAndSlots(PlayerSlotRuntimeState& state);
void MirrorTeamRotationResourcesForLocalPlayer(PlayerSlotRuntimeState& state);
void HandleTeamRotationPlayerSlotDisabled(PlayerSlotRuntimeState& state, u32 slot);
void CopyOwnerResourcesFromSlotZero(PlayerSlotRuntimeState& state);
void ConfigureTeamReserveRotation(PlayerSlotRuntimeState& state, u32 reset_units);
u32 TickTeamReserveRotationCountdown(PlayerSlotRuntimeState& state, u32 frame_counter);
void ResetPlayerSlotRelationMasks(PlayerSlotRuntimeState& state);
void BuildGameplaySessionPlayerRelationMasks(PlayerSlotRuntimeState& state,
    u32 session_mode);
void InitializeGameplaySessionPlayerSlotState(PlayerSlotRuntimeState& state,
    u32 session_mode, u32 rotation_reset_units);
void SelectNearestHostilePlayerSlots(PlayerSlotRuntimeState& state);
void MarkPlayerInactiveAndBroadcastIfLocal(PlayerSlotRuntimeState& state,
    u32 source_slot, u32 target_slot);

}
