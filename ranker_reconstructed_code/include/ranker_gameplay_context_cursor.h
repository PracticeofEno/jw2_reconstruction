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
    u32 selected_unit_command_bit_mask = 0;
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

}  // namespace ranker
