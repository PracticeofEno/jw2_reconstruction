#include "ranker_gameplay_script.h"

#include <cstdlib>
#include <iostream>

namespace {

int g_effect_status = 0;
int g_play_calls = 0;
int g_close_calls = 0;
i32 g_last_effect_entry = -1;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "gameplay script Escape regression failed at line " << line
              << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fail(#expression, __LINE__); \
        } \
    } while (false)

void reset_effect_probe(int status = 0) {
    g_effect_status = status;
    g_play_calls = 0;
    g_close_calls = 0;
    g_last_effect_entry = -1;
}

} // namespace

namespace ranker {

void PlayMilesEffectPlaylistEntry(i32 entry_index) {
    ++g_play_calls;
    g_last_effect_entry = entry_index;
}

void CloseMilesEffectPlaylistEntry(i32 entry_index) {
    ++g_close_calls;
    g_last_effect_entry = entry_index;
}

int GetMilesEffectPlaylistEntryStatus(i32 entry_index) {
    g_last_effect_entry = entry_index;
    return g_effect_status;
}

} // namespace ranker

int main() {
    using ranker::CalculateGameplayScriptTextDurationFrames;
    using ranker::GameplayScriptDialogState;
    using ranker::GameplayScriptTextCueCommand;
    using ranker::HandleGameplayScriptTextEffectCue;

    GameplayScriptDialogState extended{};
    extended.force_complete = true;
    GameplayScriptTextCueCommand opcode22{};
    opcode22.cue_id = 7;
    opcode22.extended_text_effect_opcode = true;
    opcode22.effect_entry_index = 19;
    opcode22.text = "new extended cue";
    reset_effect_probe(1);

    HandleGameplayScriptTextEffectCue(extended, opcode22, 100);

    REQUIRE(!extended.force_complete);
    REQUIRE(extended.elapsed_frames ==
        CalculateGameplayScriptTextDurationFrames(opcode22.text));
    REQUIRE(extended.active_cue_id == 0);
    REQUIRE(extended.advance_flags[1] == 0);
    REQUIRE(g_close_calls == 1);
    REQUIRE(g_last_effect_entry == 19);

    GameplayScriptDialogState ordinary{};
    ordinary.force_complete = true;
    GameplayScriptTextCueCommand opcode1{};
    opcode1.cue_id = 11;
    opcode1.effect_entry_index = 23;
    opcode1.text = "ordinary cue";
    reset_effect_probe(1);

    HandleGameplayScriptTextEffectCue(ordinary, opcode1, 200);

    REQUIRE(ordinary.force_complete);
    REQUIRE(ordinary.active_cue_id == 11);
    REQUIRE(ordinary.elapsed_frames == 0);
    REQUIRE(ordinary.advance_flags[1] == 1);
    REQUIRE(g_close_calls == 0);

    HandleGameplayScriptTextEffectCue(ordinary, opcode1, 201);

    REQUIRE(!ordinary.force_complete);
    REQUIRE(ordinary.elapsed_frames ==
        CalculateGameplayScriptTextDurationFrames(opcode1.text));
    REQUIRE(ordinary.active_cue_id == 0);
    REQUIRE(ordinary.advance_flags[1] == 0);
    REQUIRE(g_close_calls == 0);

    std::cout << "GAMEPLAY_SCRIPT_ESCAPE_PASS "
                 "opcode22=new-cue-complete opcode1=continuing-only\n";
    return EXIT_SUCCESS;
}
