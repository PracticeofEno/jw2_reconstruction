#include "ranker_gameplay_input_actions.h"
#include "ranker_gameplay_packets.h"
#include "ranker_unit_movement.h"

#include <algorithm>

namespace ranker {
namespace {

GameplayInputActionState g_gameplay_input_action_state;

constexpr u32 kSubtypePrimaryCommand = 0x01;
constexpr u32 kSubtypeBuildResourceCommand = 0x05;
constexpr u32 kSubtypePlacementCommand = 0x0c;
constexpr u32 kSubtypeNestedCommand = 0x0d;
constexpr u32 kSubtypeCatchupTarget = 0x0f;
constexpr u32 kSubtypeOpcode11 = 0x11;
constexpr u32 kSubtypePlayerInactive = 0x13;
constexpr u32 kSubtypeOpcode14 = 0x14;
constexpr u32 kSubtypeCorrectiveChecksum = 0x15;
constexpr u32 kSubtypeOpcode16 = 0x16;
constexpr u32 kSubtypePendingUnitAction = 0x19;

u32 offset_to_slot(u32 byte_offset) {
    return (byte_offset / kGameplayInputSnapshotBytes) % kGameplayInputSnapshotSlots;
}

u32 advance_offset(u32 byte_offset) {
    byte_offset += kGameplayInputSnapshotBytes;
    if (byte_offset >= kGameplayInputSnapshotRingBytes) {
        return 0;
    }
    return byte_offset;
}

bool call_bool(GameplayInputActionState& state,
    GameplayInputActionBoolCallback callback, bool fallback) {
    return callback != nullptr ? callback(state) : fallback;
}

void call(GameplayInputActionState& state, GameplayInputActionSimpleCallback callback) {
    if (callback != nullptr) {
        callback(state);
    }
}

bool publish(GameplayInputActionState& state, const GameplayPublishedAction& action,
    GameplayInputActionPublishCallback callback = nullptr) {
    state.published_actions.push_back(action);
    if (callback == nullptr) {
        callback = state.callbacks.publish_action;
    }
    return callback == nullptr || callback(state, action);
}

GameplayPublishedAction make_action(GameplayInputActionState& state, u32 subtype,
    u32 unit_offset = 0, u32 arg0 = 0, u32 arg1 = 0, u32 arg2 = 0, u32 arg3 = 0) {
    GameplayPublishedAction action{};
    action.subtype = subtype;
    action.player = state.local_player_index;
    action.packed_opcode = (subtype << 24) | (state.local_player_index & 0xffu);
    action.unit_offset = unit_offset;
    action.arg0 = arg0;
    action.arg1 = arg1;
    action.arg2 = arg2;
    action.arg3 = arg3;
    return action;
}

GameplayActionUnitState* find_unit(GameplayInputActionState& state, u32 unit_offset) {
    auto it = std::find_if(state.units.begin(), state.units.end(),
        [unit_offset](const GameplayActionUnitState& unit) {
            return unit.offset == unit_offset;
        });
    return it == state.units.end() ? nullptr : &*it;
}

GameplayActionUnitState* selected_unit(GameplayInputActionState& state) {
    return find_unit(state, state.current_unit_offset != 0 ? state.current_unit_offset :
        state.selected_unit_offset);
}

bool unit_is_local_and_live(const GameplayInputActionState& state,
    const GameplayActionUnitState& unit) {
    return unit.active && unit.runtime_state < 4 && unit.owner == state.local_player_index;
}

bool contains_value(const std::vector<u32>& values, u32 value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool is_special_build_type(u32 type) {
    return type == 0x6f || type == 0x7f || type == 0x8f || type == 0x9f;
}

void emit_rejected_feedback(GameplayInputActionState& state) {
    ++state.rejected_feedback_count;
    state.last_dispatch_failed = true;
    call(state, state.callbacks.rejected_action_feedback);
}

void reject_action(GameplayInputActionState& state) {
    ++state.validation_fail_count;
    emit_rejected_feedback(state);
}

void reset_validation_state(GameplayInputActionState& state) {
    state.last_validation_flags = 0;
    state.last_validation_unit_offset = 0;
    state.last_validation_local_unit_offset = 0;
    state.last_validation_enemy_unit_offset = 0;
    state.last_validation_special_unit_offset = 0;
}

bool point_in_unit_action_bounds(
    const GameplayActionUnitState& unit, i32 world_x, i32 world_y) {
    const i32 width = unit.bounds_width > 0 ? unit.bounds_width : 0x20;
    const i32 height = unit.bounds_height > 0 ? unit.bounds_height : 0x20;
    const i32 left = unit.x + unit.bounds_left;
    const i32 top = unit.y + unit.bounds_top;
    const i32 right = left + width;
    const i32 bottom = top + height;
    return left <= world_x && world_x <= right && top <= world_y && world_y <= bottom;
}

bool unit_can_block_input_action(const GameplayActionUnitState& unit) {
    return unit.active && unit.visible && unit.runtime_state != 4 &&
        (unit.runtime_flags & 0x80u) == 0 &&
        (unit.command_state & kUnitCommandDead) == 0;
}

bool unit_target_class_allowed(const GameplayInputActionState& state,
    u32 selector, const GameplayActionUnitState& unit) {
    if (selector >= state.selector_target_class_masks.size() ||
        unit.target_class >= 32) {
        return false;
    }
    return (state.selector_target_class_masks[selector] &
        (1u << unit.target_class)) != 0;
}

void record_validation_hit(GameplayInputActionState& state,
    const GameplayActionUnitState& unit) {
    state.last_validation_unit_offset = unit.offset;
    const bool local = unit.owner == state.local_player_index;
    const bool normal_type = unit.type < 0x60;
    if (local) {
        state.last_validation_local_unit_offset = unit.offset;
        state.last_validation_flags |= normal_type ? 1u : 4u;
    }
    else {
        state.last_validation_enemy_unit_offset = unit.offset;
        state.last_validation_flags |= normal_type ? 2u : 8u;
    }
    if (!normal_type) {
        state.last_validation_special_unit_offset = unit.offset;
    }
}

bool record_low_validation_hit(GameplayInputActionState& state,
    const GameplayActionUnitState& unit) {
    const bool local = unit.owner == state.local_player_index;
    const bool normal_type = unit.type < 0x60;

    // FUN_004e96ae returns immediately for the first local normal unit under
    // the pointer.  The other three categories retain the last matching unit
    // encountered in active-list order, then resolve in bit priority 2, 4, 8.
    if (local && normal_type) {
        state.last_validation_unit_offset = unit.offset;
        return true;
    }
    if (normal_type) {
        state.last_validation_flags |= 2u;
        state.last_validation_enemy_unit_offset = unit.offset;
    }
    else if (local) {
        state.last_validation_flags |= 4u;
        state.last_validation_local_unit_offset = unit.offset;
    }
    else {
        state.last_validation_flags |= 8u;
        state.last_validation_special_unit_offset = unit.offset;
    }
    return false;
}

bool resolve_low_validation_hit(GameplayInputActionState& state) {
    if ((state.last_validation_flags & 2u) != 0) {
        state.last_validation_unit_offset =
            state.last_validation_enemy_unit_offset;
    }
    else if ((state.last_validation_flags & 4u) != 0) {
        state.last_validation_unit_offset =
            state.last_validation_local_unit_offset;
    }
    else if ((state.last_validation_flags & 8u) != 0) {
        state.last_validation_unit_offset =
            state.last_validation_special_unit_offset;
    }
    return state.last_validation_unit_offset != 0;
}

bool default_validate_low_action(GameplayInputActionState& state,
    u32 selector, i32 world_x, i32 world_y) {
    reset_validation_state(state);
    for (const GameplayActionUnitState& unit : state.units) {
        if (!unit_can_block_input_action(unit) ||
            !unit_target_class_allowed(state, selector, unit) ||
            !point_in_unit_action_bounds(unit, world_x, world_y)) {
            continue;
        }

        if (record_low_validation_hit(state, unit)) {
            return true;
        }
    }
    return resolve_low_validation_hit(state);
}

bool default_validate_high_action(GameplayInputActionState& state,
    u32 selector, i32 world_x, i32 world_y) {
    reset_validation_state(state);
    for (const GameplayActionUnitState& unit : state.units) {
        if (unit.active || !unit.visible || unit.runtime_state != 4 ||
            unit.type >= 0x60 || (unit.runtime_flags & 4u) == 0 ||
            !unit_target_class_allowed(state, selector, unit) ||
            !point_in_unit_action_bounds(unit, world_x, world_y)) {
            continue;
        }

        record_validation_hit(state, unit);
        return true;
    }
    return false;
}

bool validate_action(GameplayInputActionState& state, u32 selector, i32 world_x,
    i32 world_y, bool high_mode) {
    GameplayInputActionValidateCallback callback =
        high_mode ? state.callbacks.validate_high_action : state.callbacks.validate_low_action;
    if (callback != nullptr) {
        return callback(state, selector, world_x, world_y);
    }
    return high_mode
        ? default_validate_high_action(state, selector, world_x, world_y)
        : default_validate_low_action(state, selector, world_x, world_y);
}

u32 select_action_index(GameplayInputActionState& state, u32 selector, i32 world_x,
    i32 world_y) {
    if (state.callbacks.select_action_index != nullptr) {
        return state.callbacks.select_action_index(state, selector, world_x, world_y);
    }
    (void)world_x;
    (void)world_y;
    return selector;
}

void start_hud_pulse(GameplayInputActionState& state, i32 world_x, i32 world_y) {
    if (state.callbacks.start_hud_pulse != nullptr) {
        state.callbacks.start_hud_pulse(state, world_x, world_y);
    }
}

void stop_hud_pulse(GameplayInputActionState& state) {
    call(state, state.callbacks.stop_hud_pulse);
}

GameplayInputActionProductionAvailability check_production_availability(
    GameplayInputActionState& state, u32 production, u32 unit_offset) {
    if (state.callbacks.check_production_availability != nullptr) {
        return state.callbacks.check_production_availability(
            state, production, unit_offset);
    }
    return GameplayInputActionProductionAvailability{true, production};
}

bool indexed_payload_blocked(GameplayInputActionState& state, u32 unit_offset,
    u32 payload, u32 index) {
    return state.callbacks.indexed_payload_blocked != nullptr &&
        state.callbacks.indexed_payload_blocked(
            state, unit_offset, payload, index);
}

u32 clamp_world_axis(i32 value, u32 tile_count) {
    const i32 limit = tile_count == 0 ? 0 : static_cast<i32>(tile_count * 0x20u) - 1;
    if (value < 0) {
        return 0;
    }
    if (value > limit) {
        return static_cast<u32>(limit);
    }
    return static_cast<u32>(value);
}

bool dispatch_action_handler(GameplayInputActionState& state, u32 action_index) {
    state.last_action_index = action_index;
    if (state.callbacks.dispatch_action != nullptr) {
        const bool dispatched = state.callbacks.dispatch_action(state, action_index);
        state.last_dispatch_failed = !dispatched;
        if (dispatched) {
            ++state.dispatch_success_count;
        }
        return dispatched;
    }
    if (action_index < state.selector_enabled.size() &&
        state.selector_enabled[action_index] == 0) {
        state.last_dispatch_failed = true;
        return false;
    }
    state.last_dispatch_failed = false;
    ++state.dispatch_success_count;
    return true;
}

void update_dispatched_unit_command_state(GameplayInputActionState& state,
    u32 action_index, u32 unit_offset) {
    if (state.last_dispatch_failed) {
        return;
    }

    const bool skip_state_update = action_index == 4 &&
        state.multi_select_count <= 1 && unit_offset == state.selected_unit_offset;
    if (skip_state_update || action_index >= state.selector_result_states.size()) {
        return;
    }

    if (GameplayActionUnitState* unit = find_unit(state, unit_offset)) {
        unit->command_state = state.selector_result_states[action_index];
    }
}

void publish_selected_unit_command(GameplayInputActionState& state, u32 subtype,
    u32 unit_offset, u32 arg0 = 0, u32 arg1 = 0, u32 arg2 = 0) {
    publish(state, make_action(state, subtype, unit_offset, arg0, arg1, arg2));
}

} // namespace

GameplayInputActionState& gameplay_input_action_state() {
    return g_gameplay_input_action_state;
}

bool DefaultHasPendingGameplayInputEvent(GameplayInputActionState& state) {
    (void)state;
    return HasQueuedInputEvent();
}

bool DefaultPopGameplayInputEvent(
    GameplayInputActionState& state, InputEvent& out) {
    (void)state;
    return PopInputEvent(out);
}

bool DefaultValidateLowGameplayInputAction(
    GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y) {
    return default_validate_low_action(state, selector, world_x, world_y);
}

bool DefaultValidateHighGameplayInputAction(
    GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y) {
    return default_validate_high_action(state, selector, world_x, world_y);
}

u32 DefaultSelectGameplayInputActionIndex(
    GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y) {
    (void)state;
    (void)world_x;
    (void)world_y;
    return selector;
}

void InitializeOriginalGameplayInputActionTables(GameplayInputActionState& state) {
    static constexpr std::array<u8, kGameplayActionSelectorCount>
        kOriginalRedirectFlags = {
            0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
            0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        };
    static constexpr std::array<u8, kGameplayActionSelectorCount>
        kOriginalSelectorModes = {
            0, 1, 2, 3, 2, 2, 0, 1, 3, 1, 3, 0, 2, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
            0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
        };
    static constexpr std::array<u8, kGameplayActionSelectorCount>
        kOriginalResultStates = {
            0x00, 0x08, 0x08, 0x08, 0x08, 0x88, 0x00, 0x00,
            0x08, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88,
            0x08, 0x88, 0x08, 0x88, 0x08, 0x08, 0x88, 0x88,
        };
    static constexpr std::array<u8, kGameplayActionSelectorCount>
        kOriginalImmediateDispatchEnabled = {
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0,
            0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
            0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        };

    state.selector_redirect_flags = kOriginalRedirectFlags;
    state.selector_modes = kOriginalSelectorModes;
    state.selector_result_states = kOriginalResultStates;
    state.selector_enabled = kOriginalImmediateDispatchEnabled;
    state.selector_target_class_masks.fill(0xffffffffu);
}

void PumpGameplayInputAndCursorFrame(GameplayInputActionState& state) {
    if (!call_bool(state, state.callbacks.can_skip_input_drain, false)) {
        DrainGameplayInputEvents(state);
        if (state.keyboard_filter_active || state.modal_route_blocked) {
            call(state, state.callbacks.finalize_cursor_frame);
            return;
        }
    }

    call(state, state.callbacks.pre_cursor_update);
    if (state.cursor_mode == 1) {
        call(state, state.callbacks.set_game_cursor_index);
    }
    else {
        call(state, state.callbacks.restore_game_cursor);
    }
    call(state, state.callbacks.post_cursor_update);
    call(state, state.callbacks.finalize_cursor_frame);
}

void DrainGameplayInputEvents(GameplayInputActionState& state) {
    for (;;) {
        const bool has_event =
            state.callbacks.has_pending_input_event != nullptr ?
            state.callbacks.has_pending_input_event(state) : HasQueuedInputEvent();
        if (!has_event) {
            return;
        }

        InputEvent event{};
        const bool popped =
            state.callbacks.pop_input_event != nullptr ?
            state.callbacks.pop_input_event(state, event) : PopInputEvent(event);
        if (!popped) {
            return;
        }

        if (event.kind != InputEventKind::keyboard) {
            if (state.callbacks.handle_pointer_event != nullptr) {
                state.callbacks.handle_pointer_event(state, event);
            }
            continue;
        }

        const u32 key = event.code & 0xffu;
        if (!state.keyboard_filter_active || key == 0x44 || key == 1) {
            if (state.callbacks.handle_keyboard_event != nullptr) {
                state.callbacks.handle_keyboard_event(state, event);
            }
        }
    }
}

void ResetGameplayInputSnapshotRing(GameplayInputActionState& state) {
    state.snapshot_write_offset = 0;
    state.snapshot_read_offset = 0;
    call(state, state.callbacks.reset_snapshot_side_state);
    state.snapshot_side_flag = false;
}

bool PopGameplayInputSnapshot(GameplayInputActionState& state) {
    const u32 slot = offset_to_slot(state.snapshot_read_offset);
    state.current_snapshot = state.snapshot_ring[slot];
    state.snapshot_read_offset = advance_offset(state.snapshot_read_offset);
    return true;
}

bool PushGameplayInputSnapshot(GameplayInputActionState& state) {
    const u32 next_offset = advance_offset(state.snapshot_write_offset);
    if (state.snapshot_read_offset == next_offset) {
        return false;
    }

    const u32 slot = offset_to_slot(state.snapshot_write_offset);
    state.snapshot_ring[slot] = state.live_snapshot;
    state.snapshot_write_offset = next_offset;
    return true;
}

void ResetGameplayInputPointerState(GameplayInputActionState& state) {
    state.live_snapshot.field0 = 0;
    state.live_snapshot.field1 = 0;
    state.pointer_aux_state = 0;
}

void SnapshotLocalGameplayChecksum(GameplayInputActionState& state) {
    u32 identity_sum = 0;
    u32 position_sum = 0;
    u32 effect_sum = 0;

    for (const GameplayChecksumObject& unit : state.checksum_units) {
        if (!unit.active) {
            continue;
        }
        identity_sum += unit.identity + unit.offset + 1;
        position_sum += static_cast<u32>(unit.x) + static_cast<u32>(unit.y);
    }
    for (const GameplayChecksumObject& effect : state.checksum_effects) {
        if (!effect.active) {
            continue;
        }
        effect_sum += static_cast<u32>(effect.x) + static_cast<u32>(effect.y) +
            effect.offset;
    }

    const u32 player = state.local_player_index % kGameplayPlayerSlots;
    state.unit_identity_checksums[player] = identity_sum;
    state.unit_position_checksums[player] = position_sum;
    state.effect_checksums[player] = effect_sum;

    u32 combined_sum =
        position_sum + identity_sum + state.checksum_tail_value;
    Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
    reliable.compatibility_checksum_snapshot_deferred = false;

    // In multiplayer lockstep, slot 0 is the session authority.  A
    // reconstructed client may not yet reproduce every opaque simulation
    // field bit-for-bit, so mirror the authority's checksum at the same FIFO
    // ordinal.  Poll once before selecting it because the original pump takes
    // its local snapshot immediately before its normal transport poll.
    const Mode1GameplayPacketDispatchState& dispatch =
        mode1_gameplay_packet_dispatch_state();
    const u32 authority = GetMode1ReliableChecksumAuthorityChannel();
    if (dispatch.generic_ai_profile_mode && authority != player) {
        if (reliable.callbacks.poll_transport_receive != nullptr) {
            reliable.callbacks.poll_transport_receive(reliable.callback_user_data);
        }
        if (!TryGetMode1ReliableAuthoritativeSubtype10Value(
                authority, player, combined_sum)) {
            reliable.compatibility_checksum_snapshot_deferred = true;
            return;
        }
    }

    // Original subtype-0x10 wire layout (FUN_004d9d00 -> FUN_004de65f):
    //   +0x10 simulation frame, +0x14 combined checksum,
    //   +0x18 identity checksum, +0x1c position checksum,
    //   +0x20 effect checksum.
    // The gameplay action publisher maps make_action's unit_offset to +0x14;
    // leaving it at zero makes every reconstructed peer advertise checksum 0
    // and causes the original to issue subtype-0x15 drop consensus immediately.
    publish(state, make_action(state, 0x10,
        combined_sum,
        mode1_reliable_state().replay_frame_tick,
        identity_sum, position_sum, effect_sum));
}

bool PublishMode1RelationMaskAction(GameplayInputActionState& state) {
    // FUN_0042f8c8/FUN_004d9d89 wire order is +10 observer, +18 zero,
    // +1c relation mask, +20 visibility mask.  The public pending arguments
    // retain their semantic order: arg0 observer, arg1 relation, arg2 visible.
    return publish(state, make_action(state, kSubtypeOpcode14, 0,
        state.pending_action_arg0, 0,
        state.pending_action_arg1, state.pending_action_arg2));
}

void PublishMode1CorrectiveChecksum(GameplayInputActionState& state) {
    publish(state, make_action(state, kSubtypeCorrectiveChecksum),
        state.callbacks.publish_corrective_action);
}

bool PublishMode1ModalPauseAction(GameplayInputActionState& state) {
    return publish(state, make_action(state, kSubtypeOpcode16, 0,
        state.pending_action_arg0));
}

bool PublishGameplayCatchupTargetState(GameplayInputActionState& state) {
    return publish(state, make_action(state, kSubtypeCatchupTarget, 0,
        state.catchup_target_state));
}

bool ResetAndPublishPlayerInactiveState(GameplayInputActionState& state) {
    state.player_reset_flags.fill(0);
    state.player_reset_gate = true;
    ResetMode1GameplayVoteCompletionGate();
    // FUN_004d9dde preserves caller EAX at +0x10 and EDX at +0x1c.  +0x18
    // remains zero; +0x20 is unused for subtype 0x13.
    return publish(state, make_action(state, kSubtypePlayerInactive, 0,
        state.pending_action_arg0, 0, state.pending_action_arg2, 0));
}

bool PublishSelectedUnitsPendingAction(GameplayInputActionState& state, u32 text_length) {
    state.pending_text_length = text_length;
    if (text_length >= 0x10) {
        state.pending_action_arg3 &= 0x00ffffffu;
        if (((state.pending_action_arg3 >> 16) & 0xffu) >= 0x80u) {
            state.pending_action_arg3 &= 0xff00ffffu;
        }
    }

    bool published_any = false;
    for (const GameplayActionUnitState& unit : state.units) {
        if ((unit.flags & 0x80u) == 0 ||
            unit.owner != state.local_player_index) {
            continue;
        }
        // Subtype 0x19 publishes the unit pointer in packet dword 0x10.
        published_any |= publish(state, make_action(state, kSubtypePendingUnitAction,
            state.pending_action_arg0, unit.offset, state.pending_action_arg1,
            state.pending_action_arg2, state.pending_action_arg3));
    }
    return published_any;
}

bool PublishMode1NoOp11Action(GameplayInputActionState& state) {
    return publish(state, make_action(state, kSubtypeOpcode11),
        state.callbacks.publish_corrective_action);
}

bool PublishSelectedUnitCapabilityAction(GameplayInputActionState& state, u32 capability) {
    GameplayActionUnitState* unit = selected_unit(state);
    if (unit == nullptr || !unit_is_local_and_live(state, *unit) ||
        unit->deferred_command_count >= 4 ||
        !contains_value(unit->command_capabilities, capability)) {
        return false;
    }

    publish_selected_unit_command(state, kSubtypePrimaryCommand, unit->offset, capability);
    return true;
}

bool PublishSelectedUnitIndexedPayloadAction(GameplayInputActionState& state, u32 index) {
    GameplayActionUnitState* unit = selected_unit(state);
    if (unit == nullptr || !unit_is_local_and_live(state, *unit) ||
        unit->deferred_command_count >= 4 || index == 0 ||
        index > state.indexed_payloads.size()) {
        return false;
    }

    const u32 payload = state.indexed_payloads[index - 1];
    if (indexed_payload_blocked(state, unit->offset, payload, index)) {
        return false;
    }

    // FUN_004d9edd preserves EBX=index into packet +0x20; +0x1c is zero.
    publish(state, make_action(state, kSubtypeBuildResourceCommand,
        unit->offset, payload, 0, 0, index));
    return true;
}

bool PublishSelectedUnitPrimaryAction(GameplayInputActionState& state) {
    return PublishSelectedUnitPrimaryAction(state, 0, 0);
}

bool PublishSelectedUnitPrimaryAction(GameplayInputActionState& state,
    u32 command, u32 logical_index) {
    GameplayActionUnitState* unit = selected_unit(state);
    if (unit == nullptr) {
        return false;
    }

    const u32 subtype =
        is_special_build_type(unit->type) ? kSubtypeBuildResourceCommand :
        kSubtypePrimaryCommand;
    publish(state, make_action(state, subtype, unit->offset, command, 1,
        logical_index));
    return true;
}

bool PublishSelectedUnitProductionAction(GameplayInputActionState& state, u32 production) {
    GameplayActionUnitState* unit = selected_unit(state);
    state.last_production_availability_code = production;
    if (unit == nullptr || !unit_is_local_and_live(state, *unit) ||
        unit->deferred_command_count >= 4 ||
        !contains_value(unit->production_capabilities, production)) {
        return false;
    }

    const GameplayInputActionProductionAvailability availability =
        check_production_availability(state, production, unit->offset);
    state.last_production_availability_code = availability.code;
    if (!availability.available) {
        if (availability.code <= 1) {
            ++state.production_feedback_count;
            emit_rejected_feedback(state);
        }
        return false;
    }

    // Normal subtype-0x0c publisher 0x004d9f89 keeps EBX=local owner through
    // FUN_004de65f, placing it at packet +0x20.
    publish(state, make_action(state, kSubtypePlacementCommand, unit->offset,
        production, availability.secondary_cost, 0, state.local_player_index));
    return true;
}

bool PublishNestedSelectedCommandAction(GameplayInputActionState& state) {
    return publish(state, make_action(state, kSubtypeNestedCommand));
}

bool PublishSelectedUnitPlacementAction(GameplayInputActionState& state) {
    return PublishSelectedUnitPlacementAction(state, 0, 0);
}

bool PublishSelectedUnitPlacementAction(GameplayInputActionState& state,
    u32 command, u32 logical_index) {
    GameplayActionUnitState* unit = selected_unit(state);
    if (unit == nullptr) {
        return false;
    }

    publish(state, make_action(state, kSubtypePlacementCommand, unit->offset,
        command, 1, logical_index));
    return true;
}

u32 DispatchSelectedUnitActionCommand(GameplayInputActionState& state, u32 selector,
    i32 screen_x, i32 screen_y, u32 unit_offset) {
    state.last_selector = selector;
    state.current_unit_offset = unit_offset;

    i32 world_x = screen_x + static_cast<i32>(state.map_origin_x);
    i32 world_y = screen_y + static_cast<i32>(state.map_origin_y);

    // FUN_004da02c receives EDX/EBX after the camera origin has already been
    // added, even for selector-mode zero entries that jump straight into the
    // raw action table.  Keep that caller tuple live before the mode branch;
    // coordinate-validation modes overwrite it with their clamped values
    // below.  Previously mode-zero actions (notably 0, 6, 0xb and 0x11..)
    // reused coordinates from an unrelated older command.
    state.last_action_world_x = static_cast<u32>(world_x);
    state.last_action_world_y = static_cast<u32>(world_y);

    if (selector < state.selector_redirect_flags.size() &&
        state.selector_redirect_flags[selector] == 1) {
        start_hud_pulse(state, world_x, world_y);
    }

    if (selector >= state.selector_modes.size() || state.selector_modes[selector] == 0) {
        dispatch_action_handler(state, selector);
        return selector;
    }

    world_x = static_cast<i32>(clamp_world_axis(world_x, state.map_width_tiles));
    world_y = static_cast<i32>(clamp_world_axis(world_y, state.map_height_tiles));
    state.last_action_world_x = static_cast<u32>(world_x);
    state.last_action_world_y = static_cast<u32>(world_y);

    const u8 mode = state.selector_modes[selector];
    if (mode != 1) {
        if (state.current_snapshot.field2 != 1) {
            const bool high_mode = mode > 3;
            const bool has_action_hit =
                validate_action(state, selector, world_x, world_y, high_mode);

            if (has_action_hit) {
                stop_hud_pulse(state);
                const u32 action_index =
                    select_action_index(state, selector, world_x, world_y);
                dispatch_action_handler(state, action_index);
                update_dispatched_unit_command_state(
                    state, action_index, unit_offset);
                return action_index;
            }

            if (high_mode) {
                state.last_dispatch_failed = false;
                return selector;
            }

            if (mode > 2) {
                state.last_dispatch_failed = false;
                stop_hud_pulse(state);
                return select_action_index(state, selector, world_x, world_y);
            }
        }

        if (mode > 2) {
            state.last_dispatch_failed = false;
            stop_hud_pulse(state);
            return select_action_index(state, selector, world_x, world_y);
        }
    }

    dispatch_action_handler(state, selector);
    return selector;
}

}
