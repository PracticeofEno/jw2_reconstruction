#pragma once

#include "ranker_types.h"

namespace ranker {

// Inputs consumed by ranker.exe's FUN_004e88f0 contextual cursor selector.
// Keep this independent of the Win32/DirectDraw cursor so the original branch
// contract and animation clock can be regression-tested without a game window.
struct GameplayContextCursorInput {
    u32 current_tick_ms = 0;
    u32 hover_kind = 0;
    u32 current_mode = 0;
    bool selected_unit_present = false;
    bool selected_unit_local = false;
    u32 selected_unit_type = 0;
    // DAT_00864b7c is the aggregate mask built from every selected mobile;
    // the primary selected unit's raw +0x58 remains distinct for kind 8.
    u32 selected_unit_command_bit_mask = 0;
    u32 selected_primary_unit_command_bit_mask = 0;
    // Predicates recovered from the mode-0 hover table.  The Win32/runtime
    // adapter derives these from the hovered unit/equipment definition; the
    // pure selector retains only the branch inputs it actually consumes.
    bool hover_target_repairable = false;
    bool hover_target_boarding_available = false;
    bool hover_target_matches_selected_cursor_type = false;
    bool hover_target_action_profile_allowed = false;
    bool hover_equipment_pickup_eligible = false;
};

struct GameplayContextCursorState {
    u32 animation_mode = 0;
    u32 animation_frame = 0;
    u32 animation_tick_ms = 0;
    u32 target_kind = 0;
    u32 cursor_base_index = 0;
    u32 cursor_index = 0;
};

struct GameplayContextCursorResolution {
    // Some original table entries return without writing the cursor globals.
    // An unapplied result therefore means "preserve the last cursor".
    bool applied = false;
    u32 cursor_index = 0;
};

GameplayContextCursorResolution ResolveGameplayContextCursor(
    GameplayContextCursorState& state,
    const GameplayContextCursorInput& input);

// FUN_004e9458 classifies a unit under the pointer as local (6), related (7),
// or unrelated (8) using the local owner's directed relationship mask.
u32 ResolveGameplayUnitHoverKind(u32 local_owner, u32 target_owner,
    u32 local_owner_relation_mask);

}  // namespace ranker
