#pragma once

#include "ranker_types.h"

#include <string>

namespace ranker {

constexpr u32 kFrontendStageArchiveStride = 0x0c;
constexpr i32 kFrontendStageFactionCount = 4;
constexpr i32 kFrontendStageMissionCount = 8;

constexpr u32 FrontendStageArchiveRecordIndex(i32 column, i32 row) {
    return static_cast<u32>(column) * kFrontendStageArchiveStride +
        static_cast<u32>(row);
}

static_assert(FrontendStageArchiveRecordIndex(0, 0) == 0);
static_assert(FrontendStageArchiveRecordIndex(1, 0) == 12);
static_assert(FrontendStageArchiveRecordIndex(2, 0) == 24);
static_assert(FrontendStageArchiveRecordIndex(3, 0) == 36);
static_assert(FrontendStageArchiveRecordIndex(3, 7) == 43);

struct FrontendStageFlowState {
    i32 column = 0;
    i32 row = 0;
    u32 record_index = 0;
    u32 current_mode = 0;
    u32 previous_mode = 0;
    u32 saved_cursor_index = 0;
    u32 selected_stage_result = 0;
    u32 stage_completion_count = 0;
    u32 selected_faction_id = 0;
    u32 active_player_slot_count = 0;
    bool stage_bundle_loaded = false;
    bool stage_started = false;
    bool start_briefing_played = false;
    bool end_briefing_played = false;
    bool stage_transition_latched = false;
    bool runtime_tables_imported = false;
    bool non_empty_runtime_tables_imported = false;
    bool player_slot_masks_rebuilt = false;
    bool nearest_hostile_slots_selected = false;
    bool render_state_reset = false;
    bool frontend_refreshed_after_stage = false;
    std::string active_stage_archive;
    std::string temp_stage_archive;
};

using FrontendStageCallback = void (*)(FrontendStageFlowState& state);
using FrontendStageResultCallback = void (*)(FrontendStageFlowState& state, u32 result);
using FrontendStageResourceLoadCallback = bool (*)();

struct FrontendStageFlowCallbacks {
    FrontendStageResourceLoadCallback load_session_terrain_resources = nullptr;
    FrontendStageCallback reset_runtime_before_stage = nullptr;
    FrontendStageCallback reset_render_state_before_stage = nullptr;
    FrontendStageCallback refresh_frontend_after_stage = nullptr;
    FrontendStageResultCallback handle_stage_result = nullptr;
};

FrontendStageFlowState& frontend_stage_flow_state();
bool ExtractFrontendStageRecordToTempArchive(FrontendStageFlowState& state,
    const char* archive_name = "JW2_06.TRC");
bool LoadFrontendStageSessionBundle(FrontendStageFlowState& state,
    i32 column, i32 row, const char* archive_name = "JW2_06.TRC",
    FrontendStageResourceLoadCallback load_session_terrain_resources = nullptr);
bool StartFrontendStageFromMenu(FrontendStageFlowState& state, i32 column, i32 row,
    const FrontendStageFlowCallbacks& callbacks = {});
void HandleFrontendStageCompletion(FrontendStageFlowState& state, u32 result,
    u32 next_mode, const FrontendStageFlowCallbacks& callbacks = {});

}
