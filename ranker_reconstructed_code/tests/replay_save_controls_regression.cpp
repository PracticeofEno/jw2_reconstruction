#include "ranker_replay.h"
#include "ranker_replay_dialogs.h"

#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace ranker;

    static_assert(std::is_same_v<
        decltype(&SaveReplayRecordingArchiveSnapshot),
        bool (*)(const char*, const ReplayRecordingState&)>);
    static_assert(std::is_same_v<
        decltype(&AutoSaveReplayRecordingArchive),
        bool (*)(const ReplayRecordingState&,
            const std::array<std::string, kReplayChannelCount>&,
            std::string&)>);

    std::array<std::string, kReplayChannelCount> names{};
    names[0] = "Alice";
    names[1] = "Bob";
    require(BuildAutomaticReplayFilename(2026, 8, 12, 3, 4, 5, names) ==
            "2026-08-12-03-04-05_AlicevsBob.ply",
        "automatic replay names must use timestamp_player-vs-player format");
    names[0] = "A:li/ce. ";
    names[1] = "B*ob?";
    names[2] = "Ignored";
    require(BuildAutomaticReplayFilename(2026, 12, 31, 23, 59, 58, names) ==
            "2026-12-31-23-59-58_A_li_cevsB_ob_.ply",
        "automatic replay names must sanitize Windows filename characters");
    names = {};
    names[0] = "CON";
    require(BuildAutomaticReplayFilename(2026, 1, 2, 0, 0, 0, names) ==
            "2026-01-02-00-00-00__CONvsPlayer.ply",
        "automatic replay names must avoid reserved Windows device names");

    ReplayRecordingState recording{};
    require(!ReplayRecordingHasSaveControls(recording),
        "a closed packet recorder must not expose replay save controls");

    recording.packet_temp_open = true;
    recording.packet_count = 1;
    require(ReplayRecordingHasSaveControls(recording),
        "an open non-empty packet recorder must expose replay save controls");

    recording.viewport_temp_open = false;
    require(ReplayRecordingHasSaveControls(recording),
        "the optional viewport sidecar must not hide replay save controls");
    recording.automatic_save_attempted = true;
    recording.automatic_save_succeeded = true;
    recording.automatic_output_path = "automatic.ply";
    require(ReplayRecordingHasSaveControls(recording),
        "automatic snapshot saving must leave manual save controls available");

    recording.playback_mode = true;
    require(!ReplayRecordingHasSaveControls(recording),
        "replay playback must not expose replay recording save controls");
    recording.scenario_ai_profile_override = true;
    recording.playback_archive_path = "sample.ply";
    recording.playback_payload.assign(kReplayHeaderBytes, 1);
    recording.playback_last_frame_tick = 42;
    recording.automatic_output_path = "old.ply";
    ClearReplayPlaybackState(recording);
    require(!recording.playback_mode &&
            !recording.scenario_ai_profile_override &&
            recording.playback_archive_path.empty() &&
            recording.playback_payload.empty() &&
            recording.playback_last_frame_tick == 0,
        "replay teardown must clear state before the next ordinary match");

    InitializeReplayTempFiles(recording, false, 1, 1, false, 0, {}, false);
    require(!recording.automatic_save_attempted &&
            !recording.automatic_save_succeeded &&
            recording.automatic_output_path.empty(),
        "a new match must reset automatic replay save state");

    std::cout << "replay save controls regression: PASS\n";
    return EXIT_SUCCESS;
}
