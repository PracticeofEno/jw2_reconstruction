#include "ranker_frontend_stage_flow.h"

#include "ranker_cursor.h"
#include "ranker_miles.h"
#include "ranker_runtime_resources.h"
#include "ranker_trc.h"
#include "ranker_ui_screen.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>
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
    i32 column, i32 row, const char* archive_name) {
    state.column = column;
    state.row = row;
    state.record_index =
        static_cast<u32>(row) * kFrontendStageArchiveStride + static_cast<u32>(column);
    state.active_stage_archive = archive_name != nullptr ? archive_name : "";
    state.stage_bundle_loaded = false;
    state.temp_stage_archive.clear();

    if (!ExtractFrontendStageRecordToTempArchive(state, archive_name)) {
        return false;
    }

    state.stage_bundle_loaded =
        HandleGameplaySessionBundleImport(state.active_stage_archive.c_str(), state.record_index);
    return state.stage_bundle_loaded;
}

bool StartFrontendStageFromMenu(FrontendStageFlowState& state, i32 column, i32 row,
    const FrontendStageFlowCallbacks& callbacks) {
    state.previous_mode = state.current_mode;
    state.saved_cursor_index = software_cursor_state().cursor_index;
    SetGameCursorIndex(0x0d);

    if (!LoadFrontendStageSessionBundle(state, column, row)) {
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

    SetPrimaryMilesMusicPolicyMode(1);
    PlayBriefingStartBinkSource();
    state.start_briefing_played = true;

    SetGameCursorIndex(0);
    PlayJw204BinkMenuScreen(column, row);
    return true;
}

void HandleFrontendStageCompletion(FrontendStageFlowState& state, u32 result,
    u32 next_mode, const FrontendStageFlowCallbacks& callbacks) {
    state.selected_stage_result = result;
    state.stage_transition_latched = false;
    call_stage_result_callback(callbacks.handle_stage_result, state, result);

    if (result == 0) {
        PlayBriefingEndBinkSource();
        state.end_briefing_played = true;
        ++state.stage_completion_count;
        state.current_mode = next_mode;
    }

    call_stage_callback(callbacks.refresh_frontend_after_stage, state);
}

}
