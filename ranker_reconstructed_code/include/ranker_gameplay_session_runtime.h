#pragma once

#include "ranker_player_slots.h"
#include "ranker_types.h"
#include "ranker_unit_lifecycle.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

struct GameSessionUnitReferenceTables;
struct GameplayScriptDialogState;
struct GameplayScriptTriggerState;
struct OwnerSessionCounterTables;
struct PostInitTransitionSnapshot;
struct SessionRuntimeDefinitionTableSet;
struct SessionRuntimeImportState;

constexpr u32 kGameplayDefaultScreenWidth = 800;
constexpr u32 kGameplayDefaultScreenHeight = 600;
constexpr u32 kGameplaySessionSnapshotBytes = 0x374;
constexpr u32 kGameplayStartupUnitsPerSlot = 6;
constexpr u32 kGameplayOnlineAutoTransitionFrame = 0x708;

// Record 0's serialized unit-list roots are authoritative. Spatial duplicate
// suppression exists only for the headerless recovery scan, where membership
// was inferred rather than read from the archive.
constexpr bool ShouldRejectRecoveredScenarioDuplicate(
    bool serialized_unit_roots_available, bool duplicate_map_object) {
    return !serialized_unit_roots_available && duplicate_map_object;
}

struct GameplayLogicalSurfaceSize {
    u32 width = kGameplayDefaultScreenWidth;
    u32 height = kGameplayDefaultScreenHeight;
};

constexpr GameplayLogicalSurfaceSize ResolveGameplayLogicalSurfaceSize(
    bool direct_draw_active, u32 direct_draw_width, u32 direct_draw_height,
    u32 requested_width, u32 requested_height) {
    if (direct_draw_active && direct_draw_width != 0 && direct_draw_height != 0) {
        return {direct_draw_width, direct_draw_height};
    }
    if (requested_width != 0 && requested_height != 0) {
        return {requested_width, requested_height};
    }
    return {kGameplayDefaultScreenWidth, kGameplayDefaultScreenHeight};
}

struct GameplayDisplayCallbacks {
    void (*reset_runtime_overlay)() = nullptr;
    void (*frame_boundary)() = nullptr;
    void (*configure_surfaces)(u32 width, u32 height, u32 color_depth) = nullptr;
};

struct GameplayDisplayState {
    u32 width = kGameplayDefaultScreenWidth;
    u32 height = kGameplayDefaultScreenHeight;
    u32 color_depth = 16;
    u32 local_owner_id = 0;
    std::array<u32, kPlayerSlotCount> owner_faction_ids{};
    bool session_start_requested = false;
    GameplayDisplayCallbacks callbacks{};
};

struct GameplayScenarioOwnerSlot {
    u8 slot_state = static_cast<u8>(PlayerSlotState::disabled);
    u32 map_slot = 0;
    u32 faction_id = 0;
    u32 tribe_id = 0;
    i32 start_x = 0;
    i32 start_y = 0;
    u32 starting_unit_type = 0;
    u32 secondary_starting_unit_type = 0;
};

struct GameplaySessionStartupCallbacks {
    void (*reset_runtime_objects)() = nullptr;
    void (*reset_owner_ai)() = nullptr;
    void (*initialize_local_camera)(i32 x, i32 y) = nullptr;
    void (*on_unit_placed)(UnitMovementUnit& unit) = nullptr;
    void (*after_session_snapshot)(const std::vector<u8>& snapshot) = nullptr;
};

struct GameplaySessionRuntimeResetState;

using GameplaySessionRuntimeResetCallback =
    void (*)(GameplaySessionRuntimeResetState& state);
using GameplaySessionRuntimeResetUnitCallback =
    void (*)(GameplaySessionRuntimeResetState& state, UnitMovementUnit& unit);
using GameplaySessionRuntimeResetOwnerCallback =
    void (*)(GameplaySessionRuntimeResetState& state, u32 owner);

struct GameplaySessionRuntimeResetCallbacks {
    GameplaySessionRuntimeResetCallback reset_effect_runtime = nullptr;
    GameplaySessionRuntimeResetCallback reset_ui_runtime_flags = nullptr;
    GameplaySessionRuntimeResetCallback before_non_empty_runtime_tables_import = nullptr;
    GameplaySessionRuntimeResetUnitCallback on_unit_reset_or_removed = nullptr;
    GameplaySessionRuntimeResetOwnerCallback update_owner_display_name = nullptr;
};

struct GameplaySessionRuntimeResetState {
    SessionRuntimeImportState* import_state = nullptr;
    SessionRuntimeDefinitionTableSet* active_definitions = nullptr;
    const SessionRuntimeDefinitionTableSet* staged_definitions = nullptr;
    const SessionRuntimeDefinitionTableSet* non_empty_staged_definitions = nullptr;
    PlayerSlotRuntimeState* players = nullptr;
    UnitLifecycleContext* lifecycle = nullptr;
    OwnerSessionCounterTables* owner_counters = nullptr;
    GameplayScriptDialogState* script_dialog = nullptr;
    GameplayScriptTriggerState* script_triggers = nullptr;
    GameSessionUnitReferenceTables* unit_reference_tables = nullptr;
    PostInitTransitionSnapshot* post_init_snapshot = nullptr;
    std::array<std::string, kPlayerSlotCount> owner_display_names{};
    std::string default_player_name = "Player";
    GameplaySessionRuntimeResetCallbacks callbacks{};
    u32 session_mode = 0;
    u32 rotation_reset_units = 0;
    u32 command_mode_a = 0;
    u32 command_mode_b = 0;
    u32 units_removed = 0;
    u32 units_preserved = 0;
    bool definition_tables_imported = false;
    bool non_empty_runtime_tables_imported = false;
    bool owner_counters_reset = false;
    bool script_dialog_reset = false;
    bool script_trigger_initialized = false;
    bool post_init_transition_pending = false;
    bool post_init_snapshot_restored = false;
    bool unit_requirement_toggle_applied = false;
    bool reverse_reference_tables_rebuilt = false;
    bool effect_runtime_reset = false;
    bool ui_runtime_flags_reset = false;
};

struct GameplaySessionStartupState {
    PlayerSlotRuntimeState* players = nullptr;
    UnitLifecycleContext* lifecycle = nullptr;
    std::array<GameplayScenarioOwnerSlot, kPlayerSlotCount> owner_slots{};
    std::array<u8, kPlayerSlotCount> ready_flags{};
    std::array<u32, kPlayerSlotCount> owner_faction_ids{};
    std::array<u32, kPlayerSlotCount> owner_tribe_ids{};
    std::array<std::string, kPlayerSlotCount> owner_display_names{};
    std::vector<UnitMovementUnit> placed_units;
    std::vector<u8> snapshot;
    GameplaySessionStartupCallbacks callbacks{};
    u32 local_owner_id = 0;
    u32 session_mode = 0;
    u32 active_slot_count = kPlayerSlotCount;
    u32 ambient_map_effect_spawn_gate = 0;
    u32 scenario_victory_condition_mask = 0;
    u32 scenario_defeat_condition_mask = 0;
    u32 frame_interval_index = 8;
    u32 rotation_reset_units = 0;
    u32 frame_counter = 0;
    u32 requested_state = 0;
    u32 non_player_slot_count = 0;
    u32 high_cluster_transition_value = 0;
    u32 high_cluster_transition_timer = 0;
    bool high_cluster_transition_requested = false;
    bool local_scene_change_requested = false;
    bool fog_reveal_disabled = false;
    bool local_camera_initialized = false;
};

enum class GameplayOnlineRequest : u32 {
    locale_flag0 = 0,
    locale_flag1 = 1,
    locale_flag2 = 2,
};

struct GameplayOnlineTransitionCallbacks {
    void (*queue_request)(GameplayOnlineRequest request) = nullptr;
    void (*publish_state)(u32 state) = nullptr;
    void (*fade_to_black)() = nullptr;
    void (*frame_boundary)() = nullptr;
};

struct GameplayOnlineTransitionState {
    u32 current_state = 0;
    u32 previous_state = 0;
    u32 remote_player_count = 0;
    u32 frame_counter = 0;
    u8 local_slot_state = static_cast<u8>(PlayerSlotState::active);
    GameplayOnlineTransitionCallbacks callbacks{};
};

struct GameplayLeaveCallbacks {
    void (*set_music_policy_mode)(u32 mode) = nullptr;
    void (*set_receive_dispatch_mode)(u32 mode) = nullptr;
    void (*close_directplay_player)() = nullptr;
    void (*shutdown_directplay_session)() = nullptr;
    void (*reset_replay_state)(const char* name) = nullptr;
};

struct GameplayLeaveState {
    u32 result_state = 0;
    u32 return_mode = 0;
    u32 network_mode = 0;
    u32 receive_dispatch_mode = 0;
    bool leave_request_cleared = false;
    GameplayLeaveCallbacks callbacks{};
};

void ConfigureGameplayDisplay800x600(GameplayDisplayState& state);
void ResetGameplaySessionRuntimeUnits(GameplaySessionRuntimeResetState& state);
void InitializeGameplaySessionRuntimeState(GameplaySessionRuntimeResetState& state);
void StartGameplaySessionFromScenarioSlots(GameplaySessionStartupState& state);
void StartGameplaySessionFromScenarioSlotsIncludingObservers(
    GameplaySessionStartupState& state);
void StartGameplaySessionFromImportedScenarioUnits(
    GameplaySessionStartupState& state);
void ResetGameplayPlayerReadyFlags(GameplaySessionStartupState& state);
void RequestGameplayStateWhenNoPlayersReady(
    GameplaySessionStartupState& state, u32 requested_state = 2);
void UpdateGameplayOnlineTransitionState(GameplayOnlineTransitionState& state);
void LeaveGameplaySessionAndResetReplay(GameplayLeaveState& state);

} // namespace ranker
