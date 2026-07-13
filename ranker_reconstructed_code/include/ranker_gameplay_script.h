#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

struct UnitMovementUnit;

struct GameplayScriptCommandPayload {
    u32 state = 0;
    i32 command_value = 0;
    i32 x_payload = 0;
    u32 y_payload = 0;
};

struct GameplayScriptDialogState {
    u32 active_cue_id = 0;
    u32 last_frame_tick = 0;
    u32 elapsed_frames = 0;
    u32 last_duration_frames = 0;
    u32 last_effect_entry = 0xffffffffu;
    i32 text_x = 100;
    i32 text_y = 300;
    bool force_complete = false;
    bool effect_playback_enabled = true;
    std::array<u8, 3> advance_flags{};
    u8* previous_advance_flag = nullptr;
    std::string visible_text;
};

struct GameplayScriptTextCueCommand {
    u32 cue_id = 0;
    bool use_custom_position = false;
    i32 x = 100;
    i32 y = 300;
    bool wait_for_effect = false;
    u32 effect_entry_index = 0;
    const char* text = "";
};

constexpr u32 kGameplayScriptTriggerGroupCount = 0x40;
constexpr u32 kGameplayScriptAreaCount = 0x40;
constexpr u32 kGameplayScriptTriggerRuntimeCount = 0x400;
constexpr u32 kGameplayScriptTriggerReferencesPerGroup = 0x40;
constexpr u32 kGameplayScriptAreaRecordSize = 0x50;
constexpr u32 kGameplayScriptTriggerRuntimeRecordSize = 0x608;
constexpr u32 kGameplayScriptTriggerGroupRecordSize = 0x14c;
constexpr u32 kGameplayScriptAreaOffset = 0x5394;
constexpr u32 kGameplayScriptTriggerGroupOffset = 0x70;
constexpr u32 kGameplayScriptTriggerRuntimeOffset = 0x6774;
constexpr u32 kGameplayScriptOwnerCount = 8;
constexpr u32 kGameplayScriptOwnerInactiveStatus = 0x14;
constexpr u32 kGameplayScriptOwnerScriptValueCount = 0xaa;
constexpr u32 kGameplayScriptOwnerUnitTypeCount = 0x40;
constexpr u32 kGameplayScriptObjectEquipmentSlots = 6;
constexpr u32 kGameplayScriptCopiedOwnerTableWords = 8;
constexpr u32 kGameplayScriptCopiedCommandTableWords = 0x200;

struct GameplayScriptArea {
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
    bool active = false;
};

struct GameplayScriptObjectBounds {
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
};

struct GameplayScriptTriggerObjectState {
    UnitMovementUnit* unit = nullptr;
    UnitMovementUnit* route_assigned_unit = nullptr;
    const void* object_pointer = nullptr;
    u32 scenario_object_index = 0;
    bool remove_from_triggers = false;
    bool script_removal_requested = false;
    u32 type_id = 0;
    u32 owner_id = 0;
    u32 flags = 0;
    u32 area_marker_flags = 0;
    u32 command_flags = 0;
    u32 command_bit_mask = 0;
    u32 script_bit_flags = 0;
    u32 linked_effect_slot_offset = 0;
    u32 command_state_raw = 0;
    u32 script_state = 0;
    u32 definition_class = 0;
    u32 string_slot = 0;
    u32 dynamic_string_slot = 0;
    std::string script_text;
    i32 x = 0;
    i32 y = 0;
    u32 command_value = 0;
    u32 current_payload_value = 0;
    i32 scripted_target_x = 0;
    i32 scripted_target_y = 0;
    u32 previous_command_state = 0;
    u32 scripted_movement_mode = 0;
    bool scripted_target_updated = false;
    i32 destination_x = 0;
    i32 destination_y = 0;
    u32 destination_aux_state = 0;
    i32 current_cell_x = 0;
    i32 current_cell_y = 0;
    i32 next_path_x = 0;
    i32 next_path_y = 0;
    i32 saved_path_target_x = 0;
    i32 saved_path_target_y = 0;
    i32 anchor_x = 0;
    i32 anchor_y = 0;
    GameplayScriptCommandPayload pending_command;
    GameplayScriptCommandPayload active_command_payload;
    std::array<GameplayScriptCommandPayload, 10> deferred_commands{};
    u32 deferred_command_count = 0;
    u32 linked_object_id = 0;
    u32 previous_object_index = 0;
    u32 next_object_index = 0;
    i32 stat_2c = 0;
    i32 stat_30 = 0;
    u32 stat_18 = 0;
    u32 stat_1c = 0;
    u32 stat_20 = 0;
    u32 stat_24 = 0;
    u32 stat_28 = 0;
    u32 stat_50 = 0;
    u32 stat_54 = 0;
    u32 stat_secondary_max = 0;
    u32 stat_secondary_current = 0;
    u32 type_flags = 0;
    u32 saved_type_flags = 0;
    u32 draw_flags = 0;
    u32 direction = 0;
    u32 movement_flags = 0;
    u32 movement_state = 0;
    u32 movement_turn_ticks = 0;       // OBC raw +0xb4
    u32 movement_step_accumulator = 0; // OBC raw +0x110
    u32 placement_reset_scratch = 0;
    i32 movement_residual_x = 0;       // OBC raw +0x114
    i32 movement_residual_y = 0;       // OBC raw +0x118
    float movement_interpolation_x = 0.0f; // OBC raw +0x11c
    float movement_interpolation_y = 0.0f; // OBC raw +0x120
    u32 animation_frame = 0;
    u32 animation_timer = 0;
    u32 command_entry_lockout_ticks = 0;
    u32 command_lockout_ticks = 0;
    u32 distance_check_mode = 0;
    bool stat_recompute_required = false;
    GameplayScriptObjectBounds bounds;
    std::array<u32, kGameplayScriptObjectEquipmentSlots> equipment_slots{};
};

using GameplayScriptStrictPlacementCallback = bool (*)(
    UnitMovementUnit& unit, i32& x, i32& y, void* user);

struct GameplayScriptSpawnRequest {
    u32 opcode = 0;
    u32 owner_id = 0;
    u32 type_or_effect_id = 0;
    i32 x = 0;
    i32 y = 0;
    i32 area_left = 0;
    i32 area_top = 0;
    i32 area_right = 0;
    i32 area_bottom = 0;
    bool map_effect = false;
    bool remove_from_area = false;
    bool has_area_bounds = false;
};

struct GameplayScriptDefinitionPatchRequest {
    u32 type_id = 0;
    std::array<u32, 0x155> words{};
};

struct GameplayScriptOwnerConditionState {
    u32 status = kGameplayScriptOwnerInactiveStatus;
    i32 score = 0;
    i32 metric = 0;
    i32 resource_a = 0;
    i32 resource_b = 0;
    i32 secondary_score = 0;
    u32 blocked_relation_mask = 0;
    u32 trigger_counter = 0;
    std::array<u32, kGameplayScriptOwnerScriptValueCount> script_values{};
    std::array<u8, kGameplayScriptOwnerUnitTypeCount> unit_type_counts{};
};

struct GameplayScriptConditionContext {
    bool enabled = false;
    std::array<GameplayScriptOwnerConditionState, kGameplayScriptOwnerCount> owners{};
    std::array<i32, kGameplayScriptOwnerCount> relation_a_counts{};
    std::array<i32, kGameplayScriptOwnerCount> relation_b_counts{};
    std::vector<u32> active_object_order;
};

struct GameplayScriptOpcodeContext {
    bool enabled = false;
    u32 resource_hud_flags = 0;
    i32 countdown_x = 400;
    i32 countdown_y = 12;
    u32 countdown_color = 1;
    u32 game_clock_ticks = 0;
    u32 local_owner_id = 0;
    u32 stage_result = 0;
    bool stage_result_pending = false;
    bool global_flag_22344 = false;
    bool global_flag_22348 = false;
    bool global_flag_22358 = false;
    bool game_clock_decrements = false;
    bool camera_request_active = false;
    i32 camera_x = 0;
    i32 camera_y = 0;
    bool selection_request_active = false;
    u32 selected_object_index = 0;
    bool text_overlay_active = false;
    i32 text_x = 100;
    i32 text_y = 300;
    u32 text_counter_owner = 0;
    std::string text_overlay;
    i32 resource_hud_start_x = 10;
    i32 resource_hud_start_y = 10;
    std::array<u32, kGameplayScriptCopiedOwnerTableWords> copied_owner_table_a{};
    std::array<u32, kGameplayScriptCopiedOwnerTableWords> copied_owner_table_b{};
    std::array<u32, kGameplayScriptCopiedCommandTableWords> copied_command_table{};
    std::array<u8, kGameplayScriptOwnerCount> owner_script_flags{};
    std::array<u32, kGameplayScriptOwnerCount> owner_external_values{};
    GameplayScriptStrictPlacementCallback find_strict_placement = nullptr;
    void* strict_placement_user = nullptr;
    std::vector<GameplayScriptSpawnRequest> spawn_requests;
    std::vector<GameplayScriptDefinitionPatchRequest> definition_patch_requests;
};

struct GameplayScriptTriggerGroup {
    bool active = false;
    u32 reference_count = 0;
    u32 timestamp_tick = 0;
    std::array<u32, kGameplayScriptTriggerReferencesPerGroup> object_indices{};
};

struct GameplayScriptTriggerRuntimeRecord {
    u8 state = 0;
    u8 blocked = 0;
    bool condition_enabled = false;
    bool trigger_enabled = true;
    u32 last_fired_tick = 0;
    u32 owner_phase_lookup = 0;
    std::array<u32, 9> condition_words{};
    std::array<u32, 0x155> command_words{};
};

struct GameplayScriptTriggerState {
    u32 current_tick = 0;
    u32 serialized_capacity = 0;
    u32 loaded_byte_count = 0;
    u32 post_nonzero_phase_resets = 0;
    std::vector<u8> serialized_triggers;
    std::vector<u32> owner_phase_lookup;
    std::vector<GameplayScriptTriggerObjectState> objects;
    GameplayScriptConditionContext condition_context;
    GameplayScriptOpcodeContext opcode_context;
    std::array<GameplayScriptArea, kGameplayScriptAreaCount> areas{};
    std::array<GameplayScriptTriggerGroup, kGameplayScriptTriggerGroupCount> groups{};
    std::array<GameplayScriptTriggerRuntimeRecord, kGameplayScriptTriggerRuntimeCount>
        triggers{};
};

using GameplayScriptConditionCallback =
    bool (*)(const GameplayScriptTriggerRuntimeRecord& trigger, void* user);
using GameplayScriptCommandCallback =
    bool (*)(const GameplayScriptTriggerRuntimeRecord& trigger, void* user);
using GameplayScriptObjectRemovedCallback = bool (*)(u32 object_index, void* user);

struct GameplayScriptTriggerCallbacks {
    GameplayScriptConditionCallback check_condition = nullptr;
    GameplayScriptCommandCallback dispatch_command = nullptr;
    GameplayScriptObjectRemovedCallback is_object_removed = nullptr;
    void* user = nullptr;
};

u32 CalculateGameplayScriptTextDurationFrames(const char* text);
void ResetGameplayScriptDialogRuntimeState(GameplayScriptDialogState& state);
void HandleGameplayScriptTextEffectCue(GameplayScriptDialogState& state,
    const GameplayScriptTextCueCommand& command, u32 frame_tick);
void HandleGameplayScriptImmediateEffectCue(GameplayScriptDialogState& state,
    bool trigger_enabled, u32 effect_entry_index);
void InitializeGameplayScriptTriggerState(GameplayScriptTriggerState& state,
    u32 serialized_capacity = 0);
bool LoadGameplayScriptRecord5ExactSize(const char* archive_name, void* destination,
    u32 byte_count);
bool LoadGameplayScriptTriggersRecord(GameplayScriptTriggerState& state,
    const char* archive_name, u32 record_index, u32 serialized_capacity = 0);
bool SaveGameplayScriptTriggersRecord(const GameplayScriptTriggerState& state,
    const char* archive_name);
u32 CountActiveGameplayScriptAreasBefore(const GameplayScriptTriggerState& state,
    u32 limit);
i32 FindNthActiveGameplayScriptArea(const GameplayScriptTriggerState& state, u32 ordinal);
i32 FindFreeGameplayScriptArea(const GameplayScriptTriggerState& state);
u32 CountActiveGameplayScriptGroupsBefore(const GameplayScriptTriggerState& state,
    u32 limit);
i32 FindNthActiveGameplayScriptGroup(const GameplayScriptTriggerState& state, u32 ordinal);
i32 FindFreeGameplayScriptGroup(const GameplayScriptTriggerState& state);
u32 CountActiveGameplayScriptRuntimeTriggersBefore(const GameplayScriptTriggerState& state,
    u32 limit);
i32 FindNthActiveGameplayScriptRuntimeTrigger(const GameplayScriptTriggerState& state,
    u32 ordinal);
i32 FindFreeGameplayScriptRuntimeTrigger(const GameplayScriptTriggerState& state);
void ProcessGameplayScriptTriggers(GameplayScriptTriggerState& state, u32 phase,
    const GameplayScriptTriggerCallbacks& callbacks = {});
bool EvaluateGameplayScriptTriggerCondition(GameplayScriptTriggerState& state,
    GameplayScriptTriggerRuntimeRecord& trigger);
bool DispatchGameplayScriptOpcode(GameplayScriptTriggerState& state,
    GameplayScriptTriggerRuntimeRecord& trigger);
bool DispatchGameplayScriptUnitCommand(u32 command_kind, UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred);
u32 ClassifyGameplayScriptUnitRuntimeState(const UnitMovementUnit& unit,
    const std::vector<u32>* command_state_table = nullptr);
i32 FindGameplayScriptTriggerGroupForObject(const GameplayScriptTriggerState& state,
    const void* object_pointer);
u32 GetGameplayScriptTriggerOwnerPhase(const GameplayScriptTriggerState& state,
    u32 lookup_index);

GameplayScriptDialogState& gameplay_script_dialog_state();
GameplayScriptTriggerState& gameplay_script_trigger_state();

}
