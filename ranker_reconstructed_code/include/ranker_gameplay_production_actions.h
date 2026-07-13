#pragma once

#include "ranker_gameplay_input_actions.h"
#include "ranker_types.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

struct UnitMovementContext;
struct UnitMovementMap;

// FUN_004db0f7 indexes the 32-entry handler table at 0x004db238.  These are
// the command-panel items 0xd4..0xf3; they are not the 42-entry world-action
// table used by FUN_004da02c and they are not the 64 production-order slots.
constexpr u32 kGameplayProductionSelectorCount = 0x20;
constexpr u32 kGameplayProductionOrderCount = 0x40;
constexpr u32 kGameplayProductionAttachmentSlots = 4;
constexpr u32 kGameplayProductionEquipmentSlots = 6;
constexpr u32 kGameplayProductionQueuedRecordBytes = 0x24;

enum class GameplayProductionGateFailure : u32 {
    none = 0,
    owner_requirement = 1,
    queued_limit = 2,
    active_limit = 3,
    resource_limit = 4,
    missing_attachment = 5,
};

struct GameplayProductionActionDefinition {
    u32 mode = 0;
    u32 allowed_movement_class_mask = 0xffffffffu;
    // JW2_11 +0x15c: required production-order id, or -1.  Despite the
    // legacy field name this is not a player/relation owner id.
    u32 owner_requirement = 0xffffffffu;
    u32 active_limit = 0xffffffffu; // JW2_11 +0x160, status_timer + 1 gate.
    u32 queued_limit = 0xffffffffu; // JW2_11 +0x1e4, secondary_value + 1 gate.
    u32 resource_limit = 0xffffffffu; // JW2_11 +0x1e0, health/action cost gate.
    u32 status_recharge_amount = 1; // JW2_11 +0x16c, selected-action recharge.
    u32 unit_type = 0;
    u32 placement_width = 1;
    u32 placement_height = 1;
    u32 placement_offset_x = 0;
    u32 placement_offset_y = 0;
    u32 placement_class = 0;
    u32 command_bit = 0;
    u32 icon_marker_code = 0;
    u8 result_state = 0;
    bool requires_capability_bit = false;
};

struct GameplayProductionUnitFootprintDefinition {
    u32 unit_type = 0;
    u32 width = 1;
    u32 height = 1;
    i32 layout_offset_x = 0;
    i32 layout_offset_y = 0;
    i32 layout_width = 1;
    i32 layout_height = 1;
};

struct GameplayProductionUnitState {
    u32 offset = 0;
    u32 type = 0;
    u32 owner = 0;
    u32 runtime_state = 0;
    u32 command_state = 0;
    // Original raw unit +0xa4.  A successful target-resolved production
    // action writes DAT_00862a14[selector] here; raw +0x60/command_state is a
    // different field and must not be overwritten by the UI dispatcher.
    u32 result_state = 0;
    u32 command_flags = 0;
    // Original unit raw +0x0c, used by the subtype-0x0b status-mask toggle.
    u32 area_marker_flags = 0;
    u32 status_flags = 0;
    u32 runtime_flags = 0;
    // Original unit raw +0x58: action capability mask copied from the unit
    // definition at creation time.
    u32 command_bits = 0;
    // Original unit raw +0x5c: mutable subtype-06 runtime command-bit mask.
    u32 runtime_command_bits = 0;
    u32 definition_action_flags = 0;
    u32 movement_class = 0;
    u64 production_bits = 0;
    u32 active_count_metric = 0;
    u32 queued_count_metric = 0;
    u32 resource_metric = 0;
    u32 linked_object_id = 0;
    i32 saved_path_target_x = 0;
    i32 saved_path_target_y = 0;
    u32 linked_unit_offset = 0;
    // Original linked unit +0xa040ac action recovery/lockout tick gate.
    u32 linked_unit_runtime_state = 0;
    u32 action_mode_gate = 0;
    // Original raw unit +0x124 deferred-command count.
    u32 deferred_command_count = 0;
    i32 x = 0;
    i32 y = 0;
    i32 bounds_left = 0;
    i32 bounds_top = 0;
    i32 bounds_width = 0;
    i32 bounds_height = 0;
    std::array<u32, kGameplayProductionEquipmentSlots> equipment_slots{};
    std::array<u32, kGameplayProductionAttachmentSlots> attachment_slots{};
    std::array<u32, kGameplayProductionAttachmentSlots> attachment_definition_ids{};
    std::vector<u32> equipment_type_filter;
    std::vector<u32> production_cost_actions;
    bool selected = false;
    bool active = true;
    bool visible = true;
};

struct GameplayProductionPlacementCell {
    // Original placement-preview layers used by FUN_004dbae2:
    // E59E74 (last-visible terrain), F19E74 (authoritative terrain),
    // E99E74 (terrain class), 798D40 (remembered route/object state), and
    // 758D40 (current visibility/occupancy).
    u32 terrain_flags = 0;
    u32 live_terrain_flags = 0;
    u32 owner_flags = 0;
    u32 route_flags = 0;
    u32 current_visibility_flags = 0;
};

struct GameplayProductionPlacementMap {
    u32 width = 0;
    u32 height = 0;
    std::vector<GameplayProductionPlacementCell> cells;
};

struct GameplayProductionQueuedCommand {
    u32 player_opcode = 0;
    u32 selector = 0;
    u32 unit_offset = 0;
    u32 aux = 0;
    i32 world_x = 0;
    i32 world_y = 0;
    u32 unit_type = 0;
    u32 footprint_width = 1;
    u32 footprint_height = 1;
    i32 unit_x = 0;
    i32 unit_y = 0;
};

struct GameplayProductionLayoutMetrics {
    i32 min_x = 0;
    i32 min_y = 0;
    i32 max_x = 0;
    i32 max_y = 0;
    u32 footprint_area = 0;
    u32 bounding_area = 0;
};

struct GameplayProductionActionState;

using GameplayProductionBoolCallback = bool (*)(GameplayProductionActionState& state);
using GameplayProductionSimpleCallback = void (*)(GameplayProductionActionState& state);
using GameplayProductionUnitCallback =
    void (*)(GameplayProductionActionState& state, u32 unit_offset);
using GameplayProductionPublishCallback =
    bool (*)(GameplayProductionActionState& state, const GameplayPublishedAction& action);
using GameplayProductionValidateCallback = bool (*)(
    GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y);
using GameplayProductionSelectCallback =
    u32 (*)(GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y);
using GameplayProductionPointCallback =
    void (*)(GameplayProductionActionState& state, i32 world_x, i32 world_y);
using GameplayProductionQueuedCommandCallback = void (*)(
    GameplayProductionActionState& state, const GameplayProductionQueuedCommand& command);
using GameplayProductionRangeCallback =
    void (*)(GameplayProductionActionState& state, u32 start_sequence, u32 end_sequence);
using GameplayProductionSessionExportCallback = bool (*)(
    GameplayProductionActionState& state, const char* archive_name,
    const char* mirror_archive_name);

struct GameplayProductionActionCallbacks {
    GameplayProductionPublishCallback publish_action = nullptr;
    GameplayProductionValidateCallback validate_low_action = nullptr;
    GameplayProductionValidateCallback validate_high_action = nullptr;
    GameplayProductionSelectCallback select_action_index = nullptr;
    GameplayProductionPointCallback start_hud_pulse = nullptr;
    GameplayProductionSimpleCallback stop_hud_pulse = nullptr;
    GameplayProductionUnitCallback accepted_action_feedback = nullptr;
    GameplayProductionBoolCallback rejected_action_feedback = nullptr;
    GameplayProductionQueuedCommandCallback dispatch_queued_command = nullptr;
    GameplayProductionRangeCallback broadcast_reliable_range = nullptr;
    GameplayProductionSessionExportCallback export_session_bundle = nullptr;
};

struct GameplayProductionActionState {
    GameplayProductionActionCallbacks callbacks;
    std::array<u8, kGameplayProductionSelectorCount> selector_redirect_flags{};
    std::array<u32, kGameplayProductionSelectorCount> selector_definition_indices{};
    std::array<u8, kGameplayProductionSelectorCount> selector_result_states{};
    std::array<u32, 8> owner_relation_masks{};
    std::array<u32, 8> owner_visibility_masks{};
    std::array<std::array<u8, kGameplayProductionOrderCount>, 8>
        owner_production_order_variant_counts{};
    std::vector<GameplayProductionActionDefinition> definitions;
    std::vector<GameplayProductionUnitFootprintDefinition> unit_footprints;
    std::vector<GameplayProductionUnitState> units;
    std::vector<GameplayProductionQueuedCommand> queued_commands;
    std::vector<GameplayPublishedAction> published_actions;
    std::string session_export_archive_name;
    std::string session_export_mirror_archive_name;
    GameplayProductionPlacementMap placement_map;
    GameplayProductionLayoutMetrics layout_metrics;
    u32 local_player_index = 0;
    u32 current_unit_offset = 0;
    u32 selected_unit_offset = 0;
    u32 map_origin_x = 0;
    u32 map_origin_y = 0;
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
    u32 current_snapshot_field2 = 0;
    u32 selected_count = 0;
    u32 selected_attachment_slot = 0;
    u32 selected_action_selector = 0;
    u32 preview_placement_terrain_class = 3;
    u32 last_action_index = 0;
    u32 last_world_x = 0;
    u32 last_world_y = 0;
    u32 reliable_range_start = 0;
    u32 reliable_range_end = 0;
    u32 last_validation_flags = 0;
    u32 last_validation_unit_offset = 0;
    u32 last_validation_blocking_unit_offset = 0;
    u32 last_drop_path_symbol = 0;
    GameplayProductionGateFailure last_gate_failure =
        GameplayProductionGateFailure::none;
    bool last_dispatch_failed = false;
    bool shift_modifier_down = false;
    bool session_export_requested = false;
    bool session_export_succeeded = false;
};

GameplayProductionActionState& gameplay_production_action_state();
void InitializeOriginalGameplayProductionSelectorTables(
    GameplayProductionActionState& state);

bool DefaultValidateLowGameplayProductionAction(
    GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y);
bool DefaultValidateHighGameplayProductionAction(
    GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y);
u32 DefaultSelectGameplayProductionActionIndex(
    GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y);
void DefaultDispatchGameplayProductionQueuedCommand(
    GameplayProductionActionState& state,
    const GameplayProductionQueuedCommand& command);
void DefaultBroadcastGameplayProductionReliableRange(
    GameplayProductionActionState& state, u32 start_sequence, u32 end_sequence);
bool DefaultExportGameplayProductionSessionBundle(
    GameplayProductionActionState& state, const char* archive_name,
    const char* mirror_archive_name);

u32 DispatchOwnerProductionActionCommand(GameplayProductionActionState& state,
    u32 selector, i32 screen_x, i32 screen_y, u32 unit_offset);
bool PublishLinkedUnitCommand24IfIdle(GameplayProductionActionState& state);
bool PublishSelectedUnitEquipmentCommand(GameplayProductionActionState& state,
    u32 equipment_type);
bool DispatchCurrentEquipmentSlotAtPoint(GameplayProductionActionState& state,
    i32 screen_x, i32 screen_y);
void MirrorGameplayProductionPlacementMapFromTerrainFlags(
    GameplayProductionActionState& state, u32 width_tiles, u32 height_tiles,
    const std::vector<u32>& tile_flags, u32 stride_tiles = 0);
void MirrorGameplayProductionPlacementMapFromMovementMap(
    GameplayProductionActionState& state, const UnitMovementContext& movement);
bool PublishSelectedUnitEquipmentSlotToggle(GameplayProductionActionState& state,
    u32 slot);
bool PublishSelectedUnitProductionCostAction(GameplayProductionActionState& state,
    u32 production);
bool PublishSelectedUnitProductionCostCancel(GameplayProductionActionState& state);
bool PublishSelectedUnitProductionCostCancel(GameplayProductionActionState& state,
    u32 command, u32 logical_index);
bool PublishSelectedUnitsStatusMaskToggle(GameplayProductionActionState& state,
    u32 required_high_flag);
bool PublishSelectedUnitAuxStateAction(GameplayProductionActionState& state);
void NoOpProductionActionHandler(GameplayProductionActionState& state);
bool PublishVoteCompletionAndFlushReliableRange(GameplayProductionActionState& state,
    u32 marker_player = 0xffffffffu);
bool FindSelectedUnitMatchingAttachmentSlot(GameplayProductionActionState& state,
    u32 definition_id);
bool CheckSelectedUnitCommandBit(GameplayProductionActionState& state, u32 bit_index);
bool CheckSelectedUnitProductionActionGate(GameplayProductionActionState& state,
    u32 selector);
bool CheckProductionOwnerRequirementGate(GameplayProductionActionState& state,
    u32 selector);
bool CheckPreviewProductionPlacementFootprintGateCells(
    GameplayProductionActionState& state, u32 unit_type, i32 world_x, i32 world_y,
    u32 source_unit_offset);
bool CheckPreviewProductionPlacementGateCell(GameplayProductionActionState& state,
    i32 tile_x, i32 tile_y, u32 source_unit_offset, bool allow_nearby_probe);
void ResetQueuedProductionPlacementCommands(GameplayProductionActionState& state);
void QueueProductionPlacementCommand(GameplayProductionActionState& state,
    const GameplayProductionQueuedCommand& command);
void FlushQueuedProductionPlacementCommands(GameplayProductionActionState& state);
void ExportLastDropSessionBundleMirror(GameplayProductionActionState& state);

}
