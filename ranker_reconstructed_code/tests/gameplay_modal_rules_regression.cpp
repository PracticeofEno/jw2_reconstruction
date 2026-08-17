#include "ranker_gameplay_modal_rules.h"
#include "ranker_gameplay_script.h"
#include "ranker_ui_screen.h"

#include <cassert>
#include <cstring>
#include <memory>

int main() {
    using namespace ranker;

    assert(GameplayPauseMenuSaveLoadEnabled(false));
    assert(!GameplayPauseMenuSaveLoadEnabled(true));
    assert(GameplayMenuEntryFlagsForEnabledState(0x1004u, false) == 0x1004u);
    assert(GameplayMenuEntryFlagsForEnabledState(0x1004u, true) == 0x1004u);
    assert(GameplayPauseMenuUsesChildSnapshot(false));
    assert(!GameplayPauseMenuUsesChildSnapshot(true));

    assert(!ShouldClearGameplayScriptWaitBreakAfterPhase(0, false));
    assert(!ShouldClearGameplayScriptWaitBreakAfterPhase(1, true));
    assert(ShouldClearGameplayScriptWaitBreakAfterPhase(1, false));

    assert(GameplayScenarioObjectiveUsesDefaultText(true, false, 5u));
    assert(GameplayScenarioObjectiveUsesDefaultText(false, true, 4u));
    assert(!GameplayScenarioObjectiveUsesDefaultText(false, true, 5u));
    assert(!GameplayScenarioObjectiveUsesDefaultText(false, false, 0u));

    assert(!GameplayPauseMenuModalPauseEnabled(false, false, false, 4));
    assert(!GameplayPauseMenuModalPauseEnabled(true, true, false, 4));
    assert(!GameplayPauseMenuModalPauseEnabled(false, true, false, 0));
    assert(GameplayPauseMenuModalPauseEnabled(false, true, false, 1));
    assert(GameplayPauseMenuModalPauseEnabled(false, true, true, 0));

    assert(!ShouldRestoreIdleUiScreenEntryState(-1, false));
    assert(!ShouldRestoreIdleUiScreenEntryState(1, false));
    assert(!ShouldRestoreIdleUiScreenEntryState(2, true));
    assert(ShouldRestoreIdleUiScreenEntryState(2, false));
    assert(ShouldRestoreIdleUiScreenEntryState(3, false));

    auto script = std::make_unique<GameplayScriptTriggerState>();
    GameplayScriptTriggerRuntimeRecord& initial = script->triggers[3];
    initial.condition_enabled = true;
    initial.condition_words[0] = 0;
    initial.command_words[0] = 0x0d;
    constexpr char kInitialObjective[] = "Initial objective";
    std::memcpy(initial.command_words.data() + 1, kInitialObjective,
        sizeof(kInitialObjective));
    assert(RecoverGameplayScenarioObjectiveText(*script) == kInitialObjective);

    GameplayScriptTriggerRuntimeRecord& latest = script->triggers[7];
    latest.state = 1;
    latest.last_fired_tick = 42;
    latest.command_words[0] = 0x0d;
    constexpr char kLatestObjective[] = "Updated objective";
    std::memcpy(latest.command_words.data() + 1, kLatestObjective,
        sizeof(kLatestObjective));
    assert(RecoverGameplayScenarioObjectiveText(*script) == kLatestObjective);

    // Original P_SCENA is mapped at 0x00722868 and stores DAT_00722870 at +8.
    // Campaign stages ship their initial objective here even when their
    // TRIGGERS record contains no opcode-0x0d row.
    std::vector<u8> scenario_record(0x2898, 0);
    constexpr char kStoredObjective[] = "Stored campaign objective";
    std::memcpy(scenario_record.data() + kGameplayScenarioObjectiveTextOffset,
        kStoredObjective, sizeof(kStoredObjective));
    assert(ReadGameplayScenarioObjectiveText(scenario_record) == kStoredObjective);
    assert(WriteGameplayScenarioObjectiveText(
        scenario_record, kLatestObjective));
    assert(ReadGameplayScenarioObjectiveText(scenario_record) == kLatestObjective);

    // Shipped P_SCENA objectives begin with a blank CRLF line.  The modal
    // renderer must advance to each following line instead of returning after
    // that first empty segment.
    constexpr char kMultilineObjective[] =
        "\r\nFirst objective line\r\nSecond objective line";
    const char* objective_line = NextUiScreenCrLfTextLine(kMultilineObjective);
    assert(objective_line != nullptr);
    assert(std::strcmp(objective_line,
        "First objective line\r\nSecond objective line") == 0);
    objective_line = NextUiScreenCrLfTextLine(objective_line);
    assert(objective_line != nullptr);
    assert(std::strcmp(objective_line, "Second objective line") == 0);
    assert(NextUiScreenCrLfTextLine(objective_line) == nullptr);
    return 0;
}
