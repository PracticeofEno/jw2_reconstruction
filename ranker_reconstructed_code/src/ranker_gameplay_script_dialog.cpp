#include "ranker_gameplay_script.h"

#include "ranker_gameplay_frame_render.h"
#include "ranker_miles.h"

#include <cstring>

namespace ranker {

u32 CalculateGameplayScriptTextDurationFrames(const char* text) {
    const std::size_t length = text != nullptr ? std::strlen(text) : 0;
    return static_cast<u32>((length >> 1) + 0x50);
}

void HandleGameplayScriptEscapeRequest(GameplayScriptDialogState& state) {
    state.force_complete =
        state.active_cue_id != 0 && state.advance_flags[1] != 0;
}

void HandleGameplayScriptTextEffectCue(GameplayScriptDialogState& state,
    const GameplayScriptTextCueCommand& command, u32 frame_tick) {
    state.visible_text = command.text != nullptr ? command.text : "";
    state.text_x = command.use_custom_position ? command.x : 100;
    state.text_y = command.use_custom_position ? command.y : 300;
    state.last_duration_frames =
        CalculateGameplayScriptTextDurationFrames(state.visible_text.c_str());
    state.last_effect_entry = command.effect_entry_index;

    const bool continuing_cue = state.active_cue_id == command.cue_id;
    if (continuing_cue) {
        if (state.last_frame_tick != frame_tick) {
            state.last_frame_tick = frame_tick;
            ++state.elapsed_frames;
        }

        if (command.wait_for_effect && state.effect_playback_enabled) {
            const int status =
                GetMilesEffectPlaylistEntryStatus(command.effect_entry_index);
            state.elapsed_frames = status == 0 ? state.last_duration_frames :
                state.last_duration_frames - 1;
        }
    } else {
        state.active_cue_id = command.cue_id;
        state.elapsed_frames = 0;

        if (command.wait_for_effect && state.effect_playback_enabled &&
            GetMilesEffectPlaylistEntryStatus(command.effect_entry_index) == 0) {
            PlayMilesEffectPlaylistEntry(command.effect_entry_index);
        }
    }

    // Opcode 0x01 consumes DAT_00d11648 only in its continuing-cue branch
    // (0x004169e3).  Opcode 0x22 performs the same check after both cue
    // branches (0x00418653), so Escape can complete a newly activated effect
    // cue before the nonzero-phase reset clears the flag.
    if (state.force_complete &&
        (continuing_cue || command.extended_text_effect_opcode)) {
        state.elapsed_frames = state.last_duration_frames;
        if (command.extended_text_effect_opcode &&
            state.effect_playback_enabled &&
            GetMilesEffectPlaylistEntryStatus(command.effect_entry_index) != 0) {
            CloseMilesEffectPlaylistEntryDeferred(command.effect_entry_index);
        }
        state.force_complete = false;
    }

    if (state.elapsed_frames < state.last_duration_frames) {
        state.advance_flags[1] = 1;
    } else {
        state.advance_flags[1] = 0;
        state.advance_flags[2] = 0;
        state.active_cue_id = 0;
    }
}

void PublishGameplayScriptDialogFrame(const GameplayScriptDialogState& dialog,
    GameplayHudTextState& hud, u32 current_tick_ms) {
    // Opcode 0x01 draws its CRLF-delimited text directly while the trigger is
    // blocking (0x004168e5..0x00416932). It does not enqueue the text in the
    // ordinary five-second HUD notification slot. Keeping it frame-scoped also
    // removes the last narration frame as soon as the cue completes.
    // A small subset of shipped campaign maps repeats the briefing objective
    // as a tagged opcode-0x01 cue after their opening dialogue. Keep executing
    // the cue so trigger timing stays deterministic, but do not duplicate the
    // already-presented mission objective over live gameplay.
    PublishGameplayFrameMessage(hud,
        ShouldPublishGameplayScriptDialogFrame(dialog),
        dialog.visible_text.c_str(), dialog.text_x, dialog.text_y,
        current_tick_ms);
}

} // namespace ranker
