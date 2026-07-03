#include "ranker_player_slots.h"

#include "ranker_unit_damage.h"

#include <algorithm>
#include <optional>

namespace ranker {
namespace {

PlayerSlotRuntimeState g_player_slot_state;

bool active_slot_valid(const PlayerSlotRuntimeState& state, u32 slot) {
    return slot < std::min<u32>(state.active_slot_count, kPlayerSlotCount);
}

bool owner_resource_slot_valid(u32 slot) {
    return slot < kPlayerOwnerResourceSlots;
}

u32 active_slot_limit(const PlayerSlotRuntimeState& state) {
    return std::min<u32>(state.active_slot_count, kPlayerSlotCount);
}

u32 slot_bit(u32 slot) {
    return slot < 32 ? (1u << slot) : 0;
}

bool slot_has_state(const PlayerSlotRuntimeState& state, u32 slot, PlayerSlotState value) {
    return active_slot_valid(state, slot) &&
        state.slot_states[slot] == static_cast<u8>(value);
}

std::optional<u32> find_last_active_slot_in_range(
    const PlayerSlotRuntimeState& state, u32 begin, u32 end) {
    std::optional<u32> active_slot;
    const u32 limit = std::min<u32>(end, std::min<u32>(state.active_slot_count,
        kPlayerSlotCount));
    for (u32 slot = begin; slot < limit; ++slot) {
        if (slot_has_state(state, slot, PlayerSlotState::active)) {
            active_slot = slot;
        }
    }
    return active_slot;
}

u32 find_next_reserve_slot_wrapped(
    const PlayerSlotRuntimeState& state, u32 begin, u32 end, u32 source_slot) {
    const u32 limit = std::min<u32>(end, std::min<u32>(state.active_slot_count,
        kPlayerSlotCount));
    for (u32 slot = source_slot + 1; slot < limit; ++slot) {
        if (slot_has_state(state, slot, PlayerSlotState::rotation_reserve)) {
            return slot;
        }
    }
    for (u32 slot = begin; slot < source_slot; ++slot) {
        if (slot_has_state(state, slot, PlayerSlotState::rotation_reserve)) {
            return slot;
        }
    }
    return source_slot;
}

void rotate_reserve_slot_range(PlayerSlotRuntimeState& state, u32 begin, u32 end) {
    const std::optional<u32> source_slot = find_last_active_slot_in_range(state, begin, end);
    if (!source_slot.has_value()) {
        return;
    }
    const u32 reserve_slot =
        find_next_reserve_slot_wrapped(state, begin, end, *source_slot);
    TransferPlayerSlotOwnershipAndState(state, *source_slot, reserve_slot);
}

void add_owner_slot_relation(PlayerSlotRuntimeState& state, u32 owner, u32 slot) {
    if (owner >= kPlayerSlotCount || slot >= kPlayerSlotCount) {
        return;
    }
    const u32 bit = slot_bit(slot);
    state.owner_relation_masks[owner] |= bit;
    state.owner_visibility_masks[owner] |= bit;
}

void build_half_team_relation_masks(PlayerSlotRuntimeState& state) {
    const u32 limit = active_slot_limit(state);
    const u32 half = limit >> 1;
    if (half == 0) {
        return;
    }

    for (u32 slot = 0; slot < limit; ++slot) {
        for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
            if ((slot / half) == (owner / half)) {
                add_owner_slot_relation(state, owner, slot);
            }
        }
        state.global_active_slot_mask |= slot_bit(slot);
    }
}

void build_observer_relation_masks(PlayerSlotRuntimeState& state) {
    u32 observer_mask = 0;
    for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
        if (!slot_has_state(state, slot, PlayerSlotState::observer)) {
            continue;
        }
        for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
            add_owner_slot_relation(state, owner, slot);
        }
        const u32 bit = slot_bit(slot);
        state.global_active_slot_mask |= bit;
        observer_mask |= bit;
    }

    if (state.local_player_slot < kPlayerSlotCount &&
        slot_has_state(state, state.local_player_slot, PlayerSlotState::observer)) {
        state.local_observer_interaction_enabled = false;
        state.local_observer_slot_mask = observer_mask;
    }
}

void build_player_controlled_relation_masks(PlayerSlotRuntimeState& state) {
    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        if (!slot_has_state(state, owner, PlayerSlotState::player_controlled)) {
            continue;
        }
        for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
            if (slot_has_state(state, slot, PlayerSlotState::player_controlled)) {
                add_owner_slot_relation(state, owner, slot);
            }
        }
    }
}

bool mode_uses_half_team_masks(u32 session_mode) {
    return session_mode == 0 || session_mode == 8;
}

bool mode_uses_player_controlled_masks(u32 session_mode) {
    return session_mode == 1 || session_mode == 5 || session_mode == 6;
}

}

PlayerSlotRuntimeState& player_slot_state() {
    return g_player_slot_state;
}

void ResetPlayerSlotRuntime(PlayerSlotRuntimeState& state) {
    state = PlayerSlotRuntimeState{};
}

void CopyOwnerResourcesFromSlotZero(PlayerSlotRuntimeState& state) {
    for (u32 slot = 0; slot < kPlayerOwnerResourceSlots; ++slot) {
        state.owner_primary_resources[slot] = state.owner_primary_resources[0];
        state.owner_secondary_resources[slot] = state.owner_secondary_resources[0];
        state.owner_aux_resources[slot] = state.owner_aux_resources[0];
    }
}

void CopyRotationAnchorResourcesToLocalPlayer(PlayerSlotRuntimeState& state) {
    const u32 local_slot = state.local_player_slot;
    const u32 anchor_slot = state.rotation_anchor_slot;
    if (!owner_resource_slot_valid(local_slot) || !owner_resource_slot_valid(anchor_slot)) {
        return;
    }

    state.owner_primary_resources[local_slot] = state.owner_primary_resources[anchor_slot];
    state.owner_secondary_resources[local_slot] =
        state.owner_secondary_resources[anchor_slot];
    state.owner_aux_resources[local_slot] = state.owner_aux_resources[anchor_slot];
}

bool TransferPlayerSlotOwnershipAndState(PlayerSlotRuntimeState& state, u32 from_slot,
    u32 to_slot) {
    if (!active_slot_valid(state, from_slot) || !active_slot_valid(state, to_slot)) {
        return false;
    }
    if (from_slot == to_slot) {
        return true;
    }

    state.slot_states[from_slot] = static_cast<u8>(PlayerSlotState::rotation_reserve);
    state.slot_states[to_slot] = static_cast<u8>(PlayerSlotState::active);
    if (state.rotation_anchor_slot == from_slot) {
        state.rotation_anchor_slot = to_slot;
    }

    if (state.transfer_owner_slot != nullptr) {
        state.transfer_owner_slot(state, from_slot, to_slot);
    }
    return true;
}

void RotateLowerTeamReservePlayerSlot(PlayerSlotRuntimeState& state) {
    rotate_reserve_slot_range(state, 0, state.active_slot_count >> 1);
}

void RotateUpperTeamReservePlayerSlot(PlayerSlotRuntimeState& state) {
    rotate_reserve_slot_range(state, state.active_slot_count >> 1, state.active_slot_count);
}

void ResetTeamReserveRotationCountdownAndSlots(PlayerSlotRuntimeState& state) {
    state.rotation_countdown_ticks = state.rotation_reset_units * 0x1e;
    RotateLowerTeamReservePlayerSlot(state);
    RotateUpperTeamReservePlayerSlot(state);
}

void MirrorTeamRotationResourcesForLocalPlayer(PlayerSlotRuntimeState& state) {
    if (!state.rotation_enabled) {
        return;
    }
    if (state.rotation_countdown_ticks == 0) {
        ResetTeamReserveRotationCountdownAndSlots(state);
    }
    CopyRotationAnchorResourcesToLocalPlayer(state);
}

void HandleTeamRotationPlayerSlotDisabled(PlayerSlotRuntimeState& state, u32 slot) {
    if (!state.rotation_enabled || !active_slot_valid(state, slot)) {
        return;
    }

    if (slot_has_state(state, slot, PlayerSlotState::active)) {
        if (slot < (state.active_slot_count >> 1)) {
            RotateLowerTeamReservePlayerSlot(state);
        } else {
            RotateUpperTeamReservePlayerSlot(state);
        }
    }

    if (slot_has_state(state, slot, PlayerSlotState::rotation_reserve)) {
        state.slot_states[slot] = static_cast<u8>(PlayerSlotState::disabled);
    }
}

void ConfigureTeamReserveRotation(PlayerSlotRuntimeState& state, u32 reset_units) {
    const u32 limit = active_slot_limit(state);
    const u32 half = limit >> 1;
    if (limit == 0 || half == 0) {
        return;
    }

    state.rotation_enabled = true;
    state.rotation_reset_units = reset_units;
    state.rotation_anchor_slot = (state.local_player_slot / half) * half;

    for (u32 slot = 0; slot < limit; ++slot) {
        if (state.slot_states[slot] == static_cast<u8>(PlayerSlotState::active)) {
            state.slot_states[slot] = static_cast<u8>(PlayerSlotState::rotation_reserve);
        }
    }

    for (u32 slot = 0; slot < half; ++slot) {
        if (state.slot_states[slot] == static_cast<u8>(PlayerSlotState::rotation_reserve)) {
            state.slot_states[slot] = static_cast<u8>(PlayerSlotState::active);
            break;
        }
    }
    for (u32 slot = half; slot < limit; ++slot) {
        if (state.slot_states[slot] == static_cast<u8>(PlayerSlotState::rotation_reserve)) {
            state.slot_states[slot] = static_cast<u8>(PlayerSlotState::active);
            break;
        }
    }
    for (u32 slot = 0; slot < limit; ++slot) {
        if (state.slot_states[slot] == static_cast<u8>(PlayerSlotState::rotation_reserve)) {
            state.owner_primary_resources[slot] = 0;
        }
    }

    state.rotation_control_value = 0x40;
    state.rotation_countdown_decrements = true;
    state.rotation_countdown_ticks = state.rotation_reset_units * 0x1e;
}

u32 TickTeamReserveRotationCountdown(PlayerSlotRuntimeState& state, u32 frame_counter) {
    u32 result = frame_counter / 0x16;
    if ((frame_counter % 0x16) == 0) {
        if (state.rotation_countdown_decrements) {
            result = state.rotation_countdown_ticks - 1;
            state.rotation_countdown_ticks = result;
        } else {
            ++state.rotation_countdown_ticks;
        }
    }
    return result;
}

void ResetPlayerSlotRelationMasks(PlayerSlotRuntimeState& state) {
    state.owner_relation_masks.fill(0);
    state.owner_visibility_masks.fill(0);
    state.global_active_slot_mask = 0;
    state.local_observer_slot_mask = 0;
    state.local_observer_interaction_enabled = true;
}

void BuildGameplaySessionPlayerRelationMasks(PlayerSlotRuntimeState& state,
    u32 session_mode) {
    ResetPlayerSlotRelationMasks(state);

    if (mode_uses_half_team_masks(session_mode)) {
        build_half_team_relation_masks(state);
    }

    build_observer_relation_masks(state);

    if (mode_uses_player_controlled_masks(session_mode)) {
        build_player_controlled_relation_masks(state);
    }

    for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
        if (slot_has_state(state, slot, PlayerSlotState::player_controlled)) {
            state.global_active_slot_mask |= slot_bit(slot);
        }
    }
}

void InitializeGameplaySessionPlayerSlotState(PlayerSlotRuntimeState& state,
    u32 session_mode, u32 rotation_reset_units) {
    state.rotation_control_value = 0;
    if (session_mode != 5) {
        CopyOwnerResourcesFromSlotZero(state);
    }
    if (session_mode == 8) {
        ConfigureTeamReserveRotation(state, rotation_reset_units);
    }
    BuildGameplaySessionPlayerRelationMasks(state, session_mode);
}

void SelectNearestHostilePlayerSlots(PlayerSlotRuntimeState& state) {
    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        if (!slot_has_state(state, owner, PlayerSlotState::player_controlled)) {
            continue;
        }

        u32 best_distance = 1000000;
        u32 target_slot = 0;
        for (u32 candidate = 0; candidate < kPlayerSlotCount; ++candidate) {
            const bool related = (state.owner_relation_masks[owner] & slot_bit(candidate)) != 0;
            if (related ||
                slot_has_state(state, candidate, PlayerSlotState::disabled) ||
                slot_has_state(state, candidate, PlayerSlotState::observer)) {
                continue;
            }

            const u32 distance = CalculateApproxUnitDistance(state.owner_start_x[owner],
                state.owner_start_y[owner], state.owner_start_x[candidate],
                state.owner_start_y[candidate]);
            if (distance < best_distance) {
                target_slot = candidate;
                best_distance = distance;
            }
        }

        state.nearest_hostile_slots[owner] = target_slot;
        if (state.refresh_owner_target != nullptr) {
            state.refresh_owner_target(state, owner);
        }
    }
}

void MarkPlayerInactiveAndBroadcastIfLocal(PlayerSlotRuntimeState& state,
    u32 source_slot, u32 target_slot) {
    if (!active_slot_valid(state, target_slot) ||
        target_slot != state.local_player_slot ||
        slot_has_state(state, target_slot, PlayerSlotState::disabled) ||
        slot_has_state(state, target_slot, PlayerSlotState::player_controlled)) {
        return;
    }

    state.inactive_target_slot = target_slot;
    state.inactive_source_slot = source_slot;
    auto publish = [&state, source_slot](u32 slot, bool mark_latched_state) {
        if (state.broadcast_player_inactive != nullptr) {
            state.broadcast_player_inactive(state, slot, source_slot);
        }
        if (mark_latched_state && slot < state.inactive_publish_states.size()) {
            state.inactive_publish_states[slot] = 4;
        }
    };

    if (source_slot == state.local_player_slot) {
        for (u32 slot = 0; slot < active_slot_limit(state); ++slot) {
            if (slot == state.local_player_slot ||
                slot_has_state(state, slot, PlayerSlotState::disabled) ||
                slot_has_state(state, slot, PlayerSlotState::player_controlled)) {
                continue;
            }
            publish(slot, true);
        }
        state.inactive_broadcast_latched = true;
        return;
    }

    publish(target_slot, false);
}

}
