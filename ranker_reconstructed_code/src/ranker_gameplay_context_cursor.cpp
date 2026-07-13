#include "ranker_gameplay_context_cursor.h"

namespace ranker {
namespace {

constexpr u32 kMobileUnitTypeLimit = 0x60u;
constexpr u32 kBerryHoverKind = 0x0cu;
constexpr u32 kHarvestCapability = 0x80u;
constexpr u8 kHighModeAlternateCursor[24] = {
    1, 1, 0, 0, 0, 0, 1, 0,
    0, 1, 0, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 0,
};

GameplayContextCursorResolution preserve_cursor(
    const GameplayContextCursorState& state) {
    return {false, state.cursor_index};
}

GameplayContextCursorResolution set_fixed_cursor(
    GameplayContextCursorState& state, u32 cursor_index) {
    state.cursor_base_index = cursor_index;
    state.cursor_index = cursor_index;
    return {true, cursor_index};
}

GameplayContextCursorResolution set_mode_cursor(
    GameplayContextCursorState& state, u32 animation_mode, u32 cursor_index) {
    // The fixed original handlers only replace DAT_00869e00. They deliberately
    // preserve the animation frame and timestamp for a later animated mode.
    state.animation_mode = animation_mode;
    return set_fixed_cursor(state, cursor_index);
}

GameplayContextCursorResolution set_target_cursor(
    GameplayContextCursorState& state, u32 target_kind, u32 cursor_index) {
    state.animation_mode = 7u;
    state.target_kind = target_kind;
    return set_fixed_cursor(state, cursor_index);
}

GameplayContextCursorResolution animate_cursor(GameplayContextCursorState& state,
    u32 current_tick_ms, u32 animation_mode, u32 base_cursor,
    u32 frame_count, u32 period_ms) {
    if (state.animation_mode != animation_mode) {
        state.animation_mode = animation_mode;
        state.animation_frame = 0;
        state.animation_tick_ms = current_tick_ms;
    }

    // FUN_004e8ef0 and its sibling handlers advance at most one frame per
    // invocation. Their unsigned subtraction also makes the clock wrap-safe.
    const u32 elapsed = current_tick_ms - state.animation_tick_ms;
    if (elapsed >= period_ms) {
        state.animation_tick_ms += elapsed;
        ++state.animation_frame;
        if (state.animation_frame >= frame_count) {
            state.animation_frame = 0;
        }
    }

    state.cursor_base_index = base_cursor;
    state.cursor_index = base_cursor + state.animation_frame;
    return {true, state.cursor_index};
}

GameplayContextCursorResolution animate_high_mode_cursor(
    GameplayContextCursorState& state, u32 current_tick_ms,
    u32 current_mode, u32 base_cursor) {
    // FUN_004e941d writes the requested mode/target first, but 004e9359 and
    // 004e93bb reset animation by comparing DAT_00862ff8 (base cursor), not
    // DAT_00869e00 (mode). Two adjacent modes in the same cursor family thus
    // continue one animation instead of restarting it.
    state.animation_mode = current_mode;
    state.target_kind = current_mode;
    if (state.cursor_base_index != base_cursor) {
        state.animation_frame = 0;
        state.animation_tick_ms = current_tick_ms;
    }

    const u32 elapsed = current_tick_ms - state.animation_tick_ms;
    if (elapsed >= 100u) {
        state.animation_tick_ms += elapsed;
        ++state.animation_frame;
        if (state.animation_frame >= 8u) {
            state.animation_frame = 0;
        }
    }

    state.cursor_base_index = base_cursor;
    state.cursor_index = base_cursor + state.animation_frame;
    return {true, state.cursor_index};
}

bool hover_routes_to_context(u32 hover_kind, u32 current_mode) {
    // FUN_004e891c's 0..24 pre-table. Tooltip-only entries are handled by
    // UpdateGameplayHoverContextAndTooltip before this selector is invoked.
    switch (hover_kind) {
    case 0u:
    case 6u:
    case 7u:
    case 8u:
    case 9u:
    case 10u:
    case 11u:
    case 12u:
    case 21u:
    case 22u:
    case 23u:
    case 24u:
        return true;
    case 1u:
        return current_mode != 6u;
    default:
        return false;
    }
}

GameplayContextCursorResolution resolve_normal_mode(
    GameplayContextCursorState& state,
    const GameplayContextCursorInput& input) {
    const bool mobile = input.selected_unit_type < kMobileUnitTypeLimit;

    if (input.hover_kind == 6u) {
        if (!mobile) {
            return set_fixed_cursor(state, 0u);
        }
        if ((input.selected_unit_command_bit_mask & 0x08u) != 0u &&
            input.hover_target_repairable) {
            // 004e8c5a..004e8c8b -> 004e8f76: repair-capable selection over
            // a damaged definition-bit-0 target.
            return animate_cursor(state, input.current_tick_ms,
                3u, 0x48u, 1u, 100u);
        }
        if (input.hover_target_boarding_available) {
            // 004e8c91..004e8ce2 -> 004e9281: a selected boardable unit fits
            // in the hovered carrier's remaining production-adjusted space.
            return animate_cursor(state, input.current_tick_ms,
                10u, 0x5cu, 4u, 110u);
        }
        return animate_cursor(state, input.current_tick_ms,
            4u, 0x40u, 8u, 100u);
    }

    if (input.hover_kind == 7u) {
        // The complete 004e8ce7 handler schedules tooltip kind seven and
        // falls directly into the cursor-zero handler at 004e9447.
        return set_fixed_cursor(state, 0u);
    }

    if (input.hover_kind == 8u) {
        if ((input.selected_primary_unit_command_bit_mask & 0x100u) != 0u &&
            input.hover_target_matches_selected_cursor_type) {
            // 004e8d06..004e8d31 -> 004e91a9.
            return animate_cursor(state, input.current_tick_ms,
                8u, 0x50u, 4u, 110u);
        }
        if ((input.selected_unit_command_bit_mask & 0x20u) != 0u &&
            input.hover_target_action_profile_allowed) {
            // 004e8d37..004e8d66 -> 004e904e.
            return animate_cursor(state, input.current_tick_ms,
                5u, 0x28u, 8u, 100u);
        }
        return set_fixed_cursor(state, 0u);
    }

    if (input.hover_kind == 9u) {
        // FUN_00411230 includes primary capability bit one and the equipment
        // pickup/type filter.  Success enters the mode-one 0x50 family.
        if (input.hover_equipment_pickup_eligible) {
            return animate_cursor(state, input.current_tick_ms,
                1u, 0x50u, 4u, 110u);
        }
        return set_fixed_cursor(state, 0u);
    }

    if (input.hover_kind == 10u) {
        // Mode-0 table slot ten is the bare RET at 004e9457.
        return preserve_cursor(state);
    }

    if (input.hover_kind == 11u) {
        if (!mobile) {
            return set_fixed_cursor(state, 0u);
        }
        if ((input.selected_unit_command_bit_mask & kHarvestCapability) != 0u) {
            // 004e8d95..004e8da4 -> 004e9169 stores target kind 11 and uses
            // the fixed contextual cursor at index 0x10.
            return set_target_cursor(state, 11u, 0x10u);
        }
        return animate_cursor(state, input.current_tick_ms,
            4u, 0x40u, 8u, 100u);
    }

    if (input.hover_kind == kBerryHoverKind) {
        if (!mobile) {
            return set_fixed_cursor(state, 0u);
        }
        if ((input.selected_unit_command_bit_mask & kHarvestCapability) != 0) {
            // FUN_004e9189: harvest-capable worker, target kind 0x0c.
            return set_target_cursor(state, kBerryHoverKind, 0x24u);
        }
        // FUN_004e8fe2: a selected mobile without the harvest capability uses
        // the ordinary 0x40..0x47 contextual animation.
        return animate_cursor(state, input.current_tick_ms, 4u, 0x40u, 8u, 100u);
    }

    if (input.hover_kind == 0u ||
        (input.hover_kind >= 20u && input.hover_kind <= 24u)) {
        if (!mobile) {
            return set_fixed_cursor(state, 0u);
        }
        return animate_cursor(state, input.current_tick_ms, 4u, 0x40u, 8u, 100u);
    }

    if (input.hover_kind == 1u) {
        // Mode-0 table entry 004e8bc6 schedules tooltip kind 1 and explicitly
        // clears DAT_00869e00 before selecting cursor zero.
        return set_mode_cursor(state, 0u, 0u);
    }

    // Remaining routed mode-zero entries have no cursor write.
    return preserve_cursor(state);
}

}  // namespace

GameplayContextCursorResolution ResolveGameplayContextCursor(
    GameplayContextCursorState& state,
    const GameplayContextCursorInput& input) {
    if (input.hover_kind > 24u ||
        !hover_routes_to_context(input.hover_kind, input.current_mode)) {
        return set_fixed_cursor(state, 0u);
    }

    // FUN_004e89cb only reaches the mode table for the local player's current
    // selection. No selection and a remote selection take an explicit
    // cursor-zero branch before mode dispatch. Structure filtering is confined
    // to the normal-mode hover handlers, just as in the executable.
    if (!input.selected_unit_present || !input.selected_unit_local) {
        return set_fixed_cursor(state, 0u);
    }

    switch (input.current_mode) {
    case 0u:
        return resolve_normal_mode(state, input);
    case 1u:
        return animate_cursor(state, input.current_tick_ms, 1u, 0x50u, 4u, 110u);
    case 2u:
        return set_mode_cursor(state, 2u, 7u);
    case 3u:
        return animate_cursor(state, input.current_tick_ms, 3u, 0x48u, 1u, 100u);
    case 4u:
        return animate_cursor(state, input.current_tick_ms, 4u, 0x40u, 8u, 100u);
    case 5u:
    case 13u:
    case 14u:
        return animate_cursor(state, input.current_tick_ms, 5u, 0x28u, 8u, 100u);
    case 6u:
        return set_mode_cursor(state, 6u, 0u);
    case 7u:
        return set_target_cursor(state, 7u, 0x10u);
    case 8u:
        return animate_cursor(state, input.current_tick_ms, 8u, 0x50u, 4u, 110u);
    case 9u:
        return animate_cursor(state, input.current_tick_ms, 9u, 0x30u, 8u, 110u);
    case 10u:
        return animate_cursor(state, input.current_tick_ms, 10u, 0x5cu, 4u, 110u);
    case 31u:
        return animate_cursor(state, input.current_tick_ms, 4u, 0x40u, 8u, 100u);
    case 36u:
        return animate_cursor(state, input.current_tick_ms, 0x24u, 0x60u, 4u, 110u);
    default:
        if (input.current_mode >= 42u && input.current_mode <= 65u) {
            const u32 table_index = input.current_mode - 42u;
            const u32 base_cursor = kHighModeAlternateCursor[table_index] != 0 ?
                0x38u : 0x30u;
            return animate_high_mode_cursor(state, input.current_tick_ms,
                input.current_mode, base_cursor);
        }
        // All remaining original mode-table entries are RET and preserve the
        // cursor globals verbatim.
        return preserve_cursor(state);
    }
}

u32 ResolveGameplayUnitHoverKind(u32 local_owner, u32 target_owner,
    u32 local_owner_relation_mask) {
    if (target_owner == local_owner) {
        return 6u;
    }
    // BT at 004e954a uses the low five bits of its register bit index.
    return ((local_owner_relation_mask >> (target_owner & 0x1fu)) & 1u) != 0u
        ? 7u
        : 8u;
}

}  // namespace ranker
