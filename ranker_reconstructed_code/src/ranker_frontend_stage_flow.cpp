#include "ranker_frontend_stage_flow.h"

#include "ranker_cursor.h"
#include "ranker_input.h"
#include "ranker_miles.h"
#include "ranker_runtime_resources.h"
#include "ranker_trc.h"
#include "ranker_ui_screen.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>
#include <thread>
#include <utility>

namespace ranker {
namespace {

FrontendStageFlowState g_frontend_stage_flow_state;

void call_stage_callback(FrontendStageCallback callback, FrontendStageFlowState& state) {
    if (callback != nullptr) {
        callback(state);
    }
}

void call_stage_result_callback(FrontendStageResultCallback callback,
    FrontendStageFlowState& state, u32 result) {
    if (callback != nullptr) {
        callback(state, result);
    }
}

std::string make_stage_temp_path(u32 record_index) {
#ifdef _WIN32
    char temp_path[MAX_PATH]{};
    if (GetTempPathA(static_cast<DWORD>(sizeof(temp_path)), temp_path) == 0) {
        return {};
    }

    char temp_file[MAX_PATH]{};
    if (GetTempFileNameA(temp_path, "Jw2Stage", record_index, temp_file) == 0) {
        return {};
    }
    return temp_file;
#else
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "Jw2Stage_%08x.trc", record_index);
    return buffer;
#endif
}

} // namespace

FrontendStageFlowState& frontend_stage_flow_state() {
    return g_frontend_stage_flow_state;
}

bool ExtractFrontendStageRecordToTempArchive(FrontendStageFlowState& state,
    const char* archive_name) {
    if (archive_name == nullptr) {
        return false;
    }

    std::string temp_path = make_stage_temp_path(state.record_index);
    if (temp_path.empty()) {
        return false;
    }

    if (!ExtractTrcRecordToFile(archive_name, state.record_index, temp_path.c_str())) {
        return false;
    }

    state.record_index = 0;
    state.active_stage_archive = temp_path;
    state.temp_stage_archive = std::move(temp_path);
    return true;
}

bool LoadFrontendStageSessionBundle(FrontendStageFlowState& state,
    i32 column, i32 row, const char* archive_name,
    FrontendStageResourceLoadCallback load_session_terrain_resources) {
    if (column < 0 || column >= kFrontendStageFactionCount ||
        row < 0 || row >= kFrontendStageMissionCount) {
        return false;
    }

    state.column = column;
    state.row = row;
    // JW2_06.TRC stores a 12-record block per faction: P at 0, Elf at 12,
    // Tyrano at 24, and Demon at 36.  Only the first eight records in each
    // block are playable missions; the final four are non-TRC placeholders.
    state.record_index = FrontendStageArchiveRecordIndex(column, row);
    state.active_stage_archive = archive_name != nullptr ? archive_name : "";
    state.stage_bundle_loaded = false;
    state.temp_stage_archive.clear();

    if (!ExtractFrontendStageRecordToTempArchive(state, archive_name)) {
        return false;
    }

    state.stage_bundle_loaded =
        HandleGameplaySessionBundleImport(state.active_stage_archive.c_str(),
            state.record_index, load_session_terrain_resources);
    return state.stage_bundle_loaded;
}

bool StartFrontendStageFromMenu(FrontendStageFlowState& state, i32 column, i32 row,
    const FrontendStageFlowCallbacks& callbacks) {
    state.previous_mode = state.current_mode;
    state.saved_cursor_index = software_cursor_state().cursor_index;
    state.briefing_screen_preloaded = false;
    state.briefing_music_preloaded = false;
    SetGameCursorIndex(0x0d);

    if (!LoadFrontendStageSessionBundle(state, column, row, "JW2_06.TRC",
            callbacks.load_session_terrain_resources)) {
        SetGameCursorIndex(0);
        return false;
    }

    state.stage_started = true;
    state.stage_transition_latched = true;
    state.current_mode = 1;
    state.selected_stage_result = 0;
    state.frontend_refreshed_after_stage = false;
    call_stage_callback(callbacks.reset_runtime_before_stage, state);
    call_stage_callback(callbacks.reset_render_state_before_stage, state);

    SetBriefingBinkActiveGameMode(state.current_mode);
    SetPrimaryMilesMusicPolicyMode(1);

    // The story card remains on screen until Escape, so use that otherwise
    // idle interval to load the large JW2_04 briefing record and open the
    // faction music decoder without starting playback. Both objects stay
    // local to their worker until the threads have joined below.
    Jw204BinkMenuPreload screen_preload;
    PreparedPrimaryMilesMusicRecord music_preload;
    bool music_preload_ready = false;
    std::thread screen_preload_thread;
    std::thread music_preload_thread;
    try {
        screen_preload_thread = std::thread([&screen_preload, column, row]() {
            (void)PreloadJw204BinkMenuScreen(screen_preload, column, row);
        });
    }
    catch (...) {
        screen_preload = Jw204BinkMenuPreload{};
    }
    const u32 briefing_music_record = PrimaryMilesBriefingMusicRecordForFaction(
        miles_music_state().primary_policy_faction_index);
    try {
        music_preload_thread = std::thread(
            [&music_preload, &music_preload_ready, briefing_music_record]() {
                music_preload_ready = PreparePrimaryMilesMusicRecord(
                    music_preload, briefing_music_record);
            });
    }
    catch (...) {
        music_preload_ready = false;
    }

    PlayBriefingStartBinkSource();
    state.start_briefing_played = true;

    if (screen_preload_thread.joinable()) {
        screen_preload_thread.join();
    }
    if (music_preload_thread.joinable()) {
        music_preload_thread.join();
    }
    state.briefing_screen_preloaded = screen_preload.ready;
    state.briefing_music_preloaded = music_preload_ready;
    if (music_preload_ready) {
        InstallPreparedPrimaryMilesMusicRecord(music_preload);
    }
    else {
        ReleasePreparedPrimaryMilesMusicRecord(music_preload);
    }

    // The story-card player advances on Escape. Establish a fresh input edge
    // before opening the mission briefing screen immediately below.
    WaitForPressedMouseButtonsReleasedThenResetInput();

    SetGameCursorIndex(0);
    const bool mission_started = PlayJw204BinkMenuScreen(
        column, row, nullptr, nullptr, &screen_preload);
    if (ShouldRestoreFrontendStageSelectionMusic(mission_started)) {
        // Unlike the original outer-loop return, this reconstructed path keeps
        // the existing campaign-selection screen alive. Restore its silent
        // policy explicitly so no briefing or title music leaks into it.
        SetPrimaryMilesMusicPolicyMode(kFrontendStageSelectionMusicPolicyMode);
        state.stage_started = false;
        state.stage_transition_latched = false;
        state.current_mode = state.previous_mode;
        return false;
    }
    return mission_started;
}

void HandleFrontendStageCompletion(FrontendStageFlowState& state, u32 result,
    u32 next_mode, const FrontendStageFlowCallbacks& callbacks) {
    state.selected_stage_result = result;
    state.stage_transition_latched = false;
    call_stage_result_callback(callbacks.handle_stage_result, state, result);

    if (result == 0) {
        SetBriefingBinkActiveGameMode(state.current_mode);
        PlayBriefingEndBinkSource();
        state.end_briefing_played = true;
        ++state.stage_completion_count;
        ++state.row;
        state.current_mode = next_mode;
    }

    call_stage_callback(callbacks.refresh_frontend_after_stage, state);
}

}
