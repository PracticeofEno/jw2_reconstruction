#include "ranker_replay.h"
#include "ranker_replay_dialogs.h"
#include "ranker_gameplay_session_flow.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void verify_replay_frontend_return_route() {
    using namespace ranker;

    require(ResolveGameplayPostSessionFrontendRoute(
            0, true, false, false) ==
            GameplayPostSessionFrontendRoute::single_player,
        "single-player replay must return to the single-player frontend");
    require(ResolveGameplayPostSessionFrontendRoute(
            0, false, false, false) ==
            GameplayPostSessionFrontendRoute::wizardnet,
        "an ordinary mode-zero match must still return to WizardNet");
    require(ResolveGameplayPostSessionFrontendRoute(
            1, false, false, false) ==
            GameplayPostSessionFrontendRoute::direct_p2p,
        "a direct-P2P match must still return to its P2P frontend");
    require(ResolveGameplayPostSessionFrontendRoute(
            0, true, true, false) ==
            GameplayPostSessionFrontendRoute::none,
        "application close must suppress every frontend return");
}

void verify_replay_live_game_boundary() {
    using namespace ranker;

    ReplayRecordingState recording{};
    recording.playback_mode = true;
    recording.scenario_ai_profile_override = true;
    recording.playback_archive_path = "Replays/Debug_replays/11.ply";
    recording.playback_payload.assign(kReplayHeaderBytes, 1);
    recording.playback_last_frame_tick = 900;

    ClearReplayPlaybackState(recording);
    require(!recording.playback_mode &&
            !recording.scenario_ai_profile_override &&
            recording.playback_archive_path.empty() &&
            recording.playback_payload.empty() &&
            recording.playback_last_frame_tick == 0,
        "a live game boundary must not inherit a prior replay archive or timing state");
}

} // namespace

int main(int argc, char** argv) {
    using namespace ranker;

    if (argc == 2 &&
        std::strcmp(argv[1], "replay_frontend_return") == 0) {
        verify_replay_frontend_return_route();
        std::cout << "replay frontend return regression: PASS\n";
        return EXIT_SUCCESS;
    }
    if (argc == 2 &&
        std::strcmp(argv[1], "replay_live_game_boundary") == 0) {
        verify_replay_live_game_boundary();
        std::cout << "replay live-game boundary regression: PASS\n";
        return EXIT_SUCCESS;
    }

    verify_replay_frontend_return_route();
    verify_replay_live_game_boundary();

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
