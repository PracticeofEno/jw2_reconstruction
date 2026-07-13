#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace ranker {

struct ProductionOrderRuntimeState;
struct UnitEquipmentCatalog;
struct UnitEffectRuntime;

constexpr u32 kMapCellTerrainMask = 0x00000700;
constexpr u32 kMapCellPassableTerrain = 0x00000100;
constexpr u32 kMapCellBlockedTerrain = 0x00000800;
constexpr u32 kMapCellReservedByUnit = 0x10000000;
constexpr u32 kMapCellHarvestAmountMask = 0x0ffff000;
constexpr u32 kMapCellHarvestAmountShift = 12;
constexpr u32 kReservedMapTileSlots = 400;
constexpr u32 kReservedTileCommandA = 0x29;
constexpr u32 kReservedTileCommandBAlt = 0x2a;
constexpr u32 kReservedTileCommandB = 0x54;
constexpr u32 kUnitCommandDead = 0x10000000;
constexpr u32 kUnitMovementSkipMask = 0x080200e0;
constexpr u32 kUnitStringSlotCount = 0x100;
constexpr u32 kUnitStringSlotBytes = 0x14;
constexpr u32 kInvalidUnitStringSlot = 0xffffffffu;
constexpr u32 kInvalidUnitRuntimeSlotIndex = 0xffffffffu;
constexpr u32 kUnitMovementFlagInterpolatingTowardTarget = 0x00000002;
constexpr u32 kUnitCommandMetadataPreserveAnimationFrame = 0x00000002;
constexpr u32 kUnitDirectionLookupWidth = 0xa0;
constexpr u32 kUnitDirectionLookupHeight = 0x78;
constexpr u32 kUnitDirectionLookupSize =
    kUnitDirectionLookupWidth * kUnitDirectionLookupHeight;
constexpr u32 kProductionEffectSlotRuntimeMaxHealth = 0;
constexpr u32 kProductionEffectSlotRuntimeMaxSecondaryValue = 1;
constexpr u32 kProductionEffectSlotUnitMaxHealth = 2;
constexpr u32 kProductionEffectSlotUnitMaxSecondaryValue = 3;
constexpr u32 kProductionEffectSlotUnitScaledValue = 5;
constexpr u32 kProductionEffectSlotUnitActionRange = 6;
constexpr u32 kProductionEffectSlotUnitMovementDelta = 7;
constexpr u32 kProductionEffectSlotUnitInteractionRange = 8;
constexpr u32 kProductionEffectSlotTransportCapacity = 9;
constexpr u32 kProductionEffectSlotHarvestAmount = 10;
constexpr u32 kProductionEffectSlotUnitCommandGate = 12;

struct UnitMovementPoint {
    i32 x = 0;
    i32 y = 0;
};

struct UnitDirectionLookupTable {
    std::array<u8, kUnitDirectionLookupSize> values{};
    std::array<u32, 8> mirror_negative_x{0, 0, 6, 5, 4, 0, 0, 0};
    std::array<u32, 8> mirror_negative_y{0, 0, 2, 1, 0, 7, 6, 0};
};

struct UnitMovementCell {
    u32 flags = 0;
    u32 visibility_flags = 0;
    u32 alternate_flags = 0;
};

struct UnitMovementMap {
    u32 width = 0;
    u32 height = 0;
    u32 stride_tiles = 0;
    // Session records 10/12/13 are authoritative even when an individual
    // tile stores zero in every layer.  A zero-valued tile is not evidence
    // that the legacy layers are absent.
    bool legacy_entry_layers_present = false;
    std::vector<UnitMovementCell> cells;
    std::vector<UnitMovementPoint> nearby_tile_offsets;
};

struct UnitMovementDefinition {
    u32 movement_class = 0;
    u32 movement_step_limit = 0;
    u32 movement_period = 1;
    bool use_16_direction_lookup = false;
    u32 overlay_class = 0;
    u32 lifecycle_class = 0;
    u32 placement_class = 0;
    u32 footprint_flags = 0;
    u32 range_threshold = 0;
    u32 animation_frame_count = 1;
    u32 animation_timer_period = 0;
    u32 initial_max_health = 0;
    u32 initial_max_secondary_value = 0;
    u32 initial_secondary_value = 0;
    u32 profile_offense_value = 0;
    u32 profile_defense_value = 0;
    u32 avatar_next_exp_per_level = 0;
    u32 avatar_next_exp_base = 1;
    std::array<u8, 32> initial_command_bits{};
    u32 passive_recovery_enabled = 0;
    u32 passive_recovery_flags = 0;
    u32 passive_map_effect_seed = 0;
    u32 effect_adjusted_interaction_range_base = 0;
    u32 effect_command_distance_gate = 0;
    u32 target_acquisition_range = 0;
    u32 cell_render_flags = 0;
    u32 action_effect_flags = 0;
    u32 render_class = 0;
    u32 projectile_impact_class = 0;
    u32 type_flags = 0;
    u32 support_source_flags = 0;
    u32 support_target_flags = 0;
    u32 support_range = 0;
    u32 support_priority = 0;
    u32 target_selection_priority = 0xffffffffu;
    u32 spawn_frame_count = 0;
    u32 transport_capacity = 0;
    u32 transport_size = 0;
    u32 transport_flags = 0;
    u32 footprint_width_tiles = 0;
    u32 footprint_height_tiles = 0;
    i32 bounds_left = 0;
    i32 bounds_top = 0;
    i32 bounds_width = 0;
    i32 bounds_height = 0;
    i32 center_bounds_left = 0;
    i32 center_bounds_top = 0;
    i32 center_bounds_width = 0;
    i32 center_bounds_height = 0;
    i32 interaction_bounds_left = 0;
    i32 interaction_bounds_top = 0;
    i32 interaction_bounds_width = 0;
    i32 interaction_bounds_height = 0;
    i32 owner_strategic_target_offset_x = 0;
    i32 owner_strategic_target_offset_y = 0;
    i32 owner_transport_route_metric_offset = 0;
    i32 completion_effect_offset_x = 0;
    i32 completion_effect_offset_y = 0;
    i32 transport_offset_x = 0;
    i32 transport_offset_y = 0;
    std::array<UnitMovementPoint, 9> effect_source_offset_by_direction{};
    u32 alternate_type_id = 0;
    u32 morph_type_id = 0;
    u32 ability_timer_period = 0;
    u32 timed_flag_phase_a_period = 0;
    u32 timed_flag_phase_b_period = 0;
    u32 completion_effect_period = 0;
    u32 completion_announcement_period = 0;
    u32 lifecycle_lockout_period = 0;
    u32 lifecycle_growth_period = 0;
    u32 lifecycle_decay_mode = 0;
    u32 lifecycle_decay_lockout_gate = 0;
    u32 production_cycle_period = 0;
    u32 production_spawn_time = 0;
    u32 production_resource_cost = 0;
    u32 production_secondary_cost = 0;
    u32 production_population_cost = 0;
    u32 prerequisite_count = 0;
    i32 spatial_query_left = 0;
    i32 spatial_query_top = 0;
    i32 spatial_query_right = 0;
    i32 spatial_query_bottom = 0;
    // Original definition +0x2218: RNG limit used whenever a path replan
    // starts a new movement animation (ProcessUnitPathToDestination 0x004c7483).
    u32 movement_animation_frame_count = 0;
    u32 action_recovery_base_ticks = 0;
    u32 action_recovery_scale_percent = 0;
    u32 action_range_base = 0;
    u32 action_range_base_vs_class3 = 0;
    u32 action_range_bonus_per_count = 0;
    u32 action_range_bonus_cap = 0;
    u32 action_profile_index = 0;
    u32 action_profile_index_vs_class3 = 0;
    u32 action_cycle_ticks = 0;
    u32 action_impact_frame_count = 0;
    u32 target_progress_primary_cost = 0;
    u32 target_progress_secondary_cost = 0;
    u32 target_progress_elite_value = 0;
    u32 variant_progress_base_cost = 1;
    u32 variant_progress_cost_per_level = 0;
    u32 variant_step_mode = 0;
    u32 variant_step_base = 0;
    u32 variant_step_linear = 0;
    u32 variant_step_extra = 0;
    u32 variant_growth_health_weight = 0;
    u32 variant_growth_secondary_weight = 0;
    u32 variant_growth_stat1c_weight = 0;
    u32 variant_growth_stat20_weight = 0;
    u32 variant_growth_stat28_weight = 0;
    u32 variant_health_delta_percent = 0;
    u32 variant_secondary_delta_percent = 0;
    u32 variant_scaled_bonus61a_per_level = 0;
    u32 variant_scaled_bonus61a_cap = 0;
    u32 variant_scaled_bonus61b_per_level = 0;
    u32 variant_scaled_bonus61b_cap = 0;
    u32 variant_scaled_bonus61c_per_level = 0;
    u32 variant_scaled_bonus61c_cap = 0;
    u32 variant_scaled_bonus61d_per_level = 0;
    u32 variant_scaled_bonus61d_cap = 0;
    UnitMovementPoint startup_followup_offset{};
    i32 startup_secondary_step_x = 0;
    std::array<u32, 8> action_impact_frames{};
    std::array<u32, 8> prerequisite_type_ids{};
    std::array<std::array<UnitMovementPoint, 32>, 9> frame_delta_by_direction{};
    // Original catalog +0x1f0 initializes mutable unit raw +0xe8.  It is
    // distinct from immutable support/footprint flags at catalog +0x1f8.
    u32 initial_script_bit_flags = 0;
    // Original catalog +0x1d4: type selected by direct linked-unit release.
    // This is not the morph/owner-accounting alternate at catalog +0x1e4.
    u32 linked_release_type_id = 0;
    // Original catalog +0x13f8: animation wrap used by target-progress state
    // 0x11.  The idle/reservation timer at +0x13d4 is a different field.
    u32 target_progress_animation_period = 0;
    // Original catalog +0x284/+0x288 and u8 +0x30f drive construction
    // placement path probes.  They are not the prerequisite list at +0x1fc.
    u32 placement_path_reference_count = 0;
    std::array<u32, 16> placement_path_reference_type_ids{};
    u32 placement_small_reference_count = 0;
};

struct UnitQueuedCommand {
    u32 state = 0;
    // Original tuple order is state, command-value/target, x payload, y payload.
    // Names are retained for compatibility with older reconstruction code.
    i32 x = 0;
    i32 y = 0;
    u32 value = 0;
};

struct UnitMovementUnit {
    u32 id = 0;
    u32 runtime_slot_index = kInvalidUnitRuntimeSlotIndex;
    u32 type_id = 0;
    u32 string_slot = 0;
    u32 scenario_string_slot = 0;
    u32 area_marker_flags = 0;
    u32 owner_id = 0;
    u32 type_flags = 0;
    u32 saved_type_id = 0;
    u32 saved_type_flags = 0;
    u32 command_state = 0;
    u32 command_flags = 0;
    std::array<u8, 32> command_bits{};
    u32 runtime_flags = 0;
    u32 draw_flags = 0;
    u32 previous_command_state = 0;
    u32 deferred_command_state = 0;
    u32 command_entry_lockout_ticks = 0;
    u32 command_lockout_ticks = 0;
    u32 ability_id = 0;
    u32 action_mode = 0;
    u32 action_mode_gate = 0;
    u32 command_value = 0;
    u32 spawn_type_id = 0;
    u32 production_variant = 0;
    u32 variant_reduction_count = 0;
    u32 elite_progress_count = 0;
    u32 elite_progress_value = 0;
    u32 queued_production_type_id = 0;
    u32 extended_production_type_index = 0;
    u32 transfer_limit = 0;
    i32 x = 0;
    i32 y = 0;
    i32 destination_x = 0;
    i32 destination_y = 0;
    // Raw +0x78/+0x7c are movement destinations for mobile units, but the
    // cell renderer overloads them as an additive-ramp index and image frame.
    // Keep the typed runtime values separate so placed structures do not feed
    // their world destination coordinates into sprite-frame selection.
    u32 cell_channel_additive_frame = 0;
    u32 cell_flag40_animation_frame = 0;
    u32 destination_aux_state = 0;
    i32 current_cell_x = 0;
    i32 current_cell_y = 0;
    i32 path_target_x = 0;
    i32 path_target_y = 0;
    i32 saved_path_target_x = 0;
    i32 saved_path_target_y = 0;
    i32 next_path_x = 0;
    i32 next_path_y = 0;
    i32 anchor_x = 0;
    i32 anchor_y = 0;
    u32 movement_flags = 0;
    u32 direction = 0;
    u32 animation_frame = 0;
    u32 animation_timer = 0;
    u32 movement_state = 0;
    // Original raw +0xb4.  ProcessUnitPathToDestination seeds this to four;
    // the movement core increments it while rotating and times out at ten.
    u32 movement_turn_ticks = 0;
    u32 placement_reset_scratch = 0;
    // Reserved typed-runtime padding.  The corresponding raw OBC words at
    // +0x114..+0x120 are represented by the movement fields below; retaining
    // this padding keeps the public runtime layout stable for live probes.
    std::array<u32, 4> movement_runtime_padding{};
    // Original raw +0x110.  This is the movement-speed accumulator and is
    // temporarily reused as the rotating obstacle-probe counter.
    u32 movement_step_accumulator = 0;
    i32 movement_residual_x = 0;
    i32 movement_residual_y = 0;
    // Original raw +0x11c/+0x120 are IEEE-754 accumulators.  Integer x/y are
    // derived only after the fractional movement has been accumulated.
    float movement_interpolation_x = 0.0f;
    float movement_interpolation_y = 0.0f;
    u32 cargo_amount = 0;
    u32 cargo_capacity = 0;
    u32 harvest_tile_index = 0;
    u32 work_timer = 0;
    u32 linked_effect_slot_offset = 0;
    UnitEffectRuntime* reserved_tile_effect = nullptr;
    u32 effect_timer = 0;
    u32 distance_check_mode = 0;
    u32 linked_object_id = 0;
    u32 max_health = 0;
    u32 health = 0;
    u32 runtime_stat_1c = 0;
    u32 runtime_stat_20 = 0;
    u32 max_secondary_value = 0;
    u32 secondary_value = 0;
    u32 runtime_stat_28 = 0;
    u32 status_timer = 0;
    std::array<u32, 4> item_slots{};
    std::array<u32, 6> equipment_slots{};
    u32 equipment_flags = 0;
    UnitQueuedCommand pending_command;
    UnitQueuedCommand active_command_payload;
    std::array<UnitQueuedCommand, 10> deferred_commands{};
    u32 deferred_command_count = 0;
    UnitMovementUnit* target = nullptr;
    UnitMovementUnit* linked_unit = nullptr;
    bool active = true;
    bool attached_to_parent = false;
    bool under_construction = false;
    bool footprint_registered = false;
    bool production_reserved = false;
    UnitMovementDefinition definition;
    // Mutable original unit raw +0xe8, changed by scenario bit opcodes.
    u32 script_bit_flags = 0;
};

struct UnitRuntimeStatBlock {
    u32 max_health = 0;
    u32 max_secondary_value = 0;
    u32 health = 0;
    u32 stat_1c = 0;
    u32 stat_20 = 0;
    u32 secondary_value = 0;
    u32 stat_28 = 0;
};

struct UnitVariantGrowthResult {
    u32 health_steps = 0;
    u32 secondary_steps = 0;
    u32 stat_1c_steps = 0;
    u32 stat_20_steps = 0;
    u32 stat_28_steps = 0;
};

using UnitVariantRandomLimitCallback = u32 (*)(u32 limit);

struct UnitActionDamageProfile {
    u32 allowed_target_render_class_mask = 0xffffffffu;
    u32 render_class2_terrain_gate = 1;
    u32 target_distance_gate = 0;
    bool area_damage_allows_related_targets = false;
    std::array<i32, 5> render_class_percent{{100, 100, 100, 100, 100}};
    std::array<i32, 3> projectile_impact_class_percent{{100, 100, 100}};
};

struct UnitActionDamageProfileTable {
    std::vector<UnitActionDamageProfile> profiles;
};

struct UnitReservedMapTile {
    bool active = false;
    u32 unit_id = 0;
    u32 tile_index = 0;
};

struct UnitReservedTileReleaseResult {
    bool released = false;
    u32 tile_index = 0;
};

struct UnitArrivalCheck {
    bool direction_ready = false;
    u32 status = 0;
    u32 direction = 0;
};

struct UnitTargetBoundsMovementStatus {
    bool reached = false;
    u32 status = 0;
    u32 direction = 0;
    i32 suggested_path_x = 0;
    i32 suggested_path_y = 0;
};

struct UnitTileSearchResult {
    bool found = false;
    i32 x = 0;
    i32 y = 0;
};

struct UnitDropoffSearchResult {
    UnitMovementUnit* unit = nullptr;
    u32 distance = 0xffffffff;
};

struct UnitLinkedTargetCheck {
    u32 status = 0;
    u32 distance = 0;
};

struct UnitTerrainClassProbeResult {
    u32 status = 1;
    UnitMovementUnit* blocking_unit = nullptr;
};

struct UnitMovementDirectionTurnResult {
    u32 target_direction = 0;
    u32 direction_count = 0;
    i32 turn_step = 0;
    bool interpolation_active = false;
    bool turned = false;
    bool can_advance = false;
    bool turn_timeout = false;
};

struct UnitMovementStepAdvanceResult {
    u32 before_distance = 0xffffffffu;
    u32 after_distance = 0xffffffffu;
    bool used_interpolation = false;
    bool moved = false;
    bool reset_progress = false;
};

struct UnitMovementCoreUpdateConfig {
    bool use_16_direction_lookup = false;
    const UnitDirectionLookupTable* direction_lookup_8 = nullptr;
    const UnitDirectionLookupTable* direction_lookup_16 = nullptr;
    u32 movement_period = 1;
    u32 base_step_limit = 0;
    i32 additional_movement_modifier = 0;
    const UnitEquipmentCatalog* equipment_catalog = nullptr;
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
};

struct UnitMovementCoreUpdateResult {
    u32 target_direction = 0;
    u32 direction_count = 0;
    u32 movement_period = 1;
    u32 base_step_limit = 0;
    u32 resolved_step_limit = 0;
    UnitMovementDirectionTurnResult turn;
    UnitMovementStepAdvanceResult advance;
    bool accumulator_active = false;
};

struct UnitMovementContext;
using UnitRuntimePreTerrainCallback = bool (*)(UnitMovementContext& context,
    UnitMovementUnit& unit, void* user_data);

using UnitPathfinderCallback = bool (*)(UnitMovementContext& context,
    UnitMovementUnit& unit);
using UnitCanEnterCallback = bool (*)(UnitMovementContext& context,
    const UnitMovementUnit& unit, i32 x, i32 y);
using UnitMovementCallback = void (*)(UnitMovementContext& context,
    UnitMovementUnit& unit);
using UnitMovementRandomLimitCallback = u32 (*)(UnitMovementContext& context,
    u32 limit);
using UnitMovementDistanceCallback = u32 (*)(UnitMovementContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
using UnitMovementCommandMetadataFlagsCallback = u32 (*)(UnitMovementContext& context,
    const UnitMovementUnit& unit);
using UnitMovementSpatialTargetCallback = UnitMovementUnit* (*)(
    UnitMovementContext& context, UnitMovementUnit& unit);
using UnitMovementAttachmentReleaseCallback = bool (*)(UnitMovementContext& context,
    UnitMovementUnit& parent, UnitMovementUnit& child);
using UnitMovementPixelPresentCallback = void (*)(UnitMovementContext& context,
    i32 x, i32 y, u16 color);

struct UnitMovementCallbacks {
    UnitPathfinderCallback run_pathfinder = nullptr;
    UnitCanEnterCallback can_enter_cell = nullptr;
    UnitMovementCallback on_path_replanned = nullptr;
    UnitMovementCallback on_reached_destination = nullptr;
    UnitMovementCallback on_unit_marked_dead = nullptr;
    UnitMovementRandomLimitCallback random_limit = nullptr;
    UnitMovementDistanceCallback distance_to_unit = nullptr;
    UnitMovementCommandMetadataFlagsCallback command_metadata_flags = nullptr;
    UnitMovementSpatialTargetCallback query_ground_separation_target = nullptr;
    UnitMovementSpatialTargetCallback query_air_separation_target = nullptr;
    UnitMovementAttachmentReleaseCallback on_attached_child_parent_death = nullptr;
    UnitMovementPixelPresentCallback on_debug_pixel_present = nullptr;
};

struct UnitMovementVisibilityLayers {
    const std::vector<u32>* previous_flags = nullptr;
    const std::vector<u32>* terrain_backup_flags = nullptr;
    u32 width = 0;
    u32 height = 0;
    u32 stride_tiles = 0;
};

struct UnitMovementContext {
    UnitMovementMap map;
    UnitMovementCallbacks callbacks;
    UnitMovementVisibilityLayers visibility_layers;
    const ProductionOrderRuntimeState* production_state = nullptr;
    const UnitEquipmentCatalog* equipment_catalog = nullptr;
    const UnitDirectionLookupTable* direction_lookup_8 = nullptr;
    const UnitDirectionLookupTable* direction_lookup_16 = nullptr;
    i32 additional_movement_modifier = 0;
    std::vector<UnitMovementUnit*> active_units;
    std::vector<UnitMovementUnit*> free_units;
    std::vector<UnitMovementUnit*> lifecycle_units;
    std::array<std::array<char, kUnitStringSlotBytes>, kUnitStringSlotCount> string_slots{};
    std::array<UnitReservedMapTile, kReservedMapTileSlots> reserved_tiles{};
    u32 reserved_tile_count = 0;
};

struct LegacyPathfinderScratchState {
    i32 width_tiles = 0;
    i32 height_tiles = 0;
    u32 visit_budget = 0;
    u32 checked_tile_count = 0;
    UnitMovementPoint neighbor_cursor{};
    u32 neighbor_direction = 0;
};

u32 UnitMovementMapStrideTiles(const UnitMovementMap& map);
u32 UnitMovementMapTileIndex(const UnitMovementMap& map, u32 tile_x, u32 tile_y);
UnitMovementCell* GetMovementCell(UnitMovementMap& map, u32 tile_x, u32 tile_y);
const UnitMovementCell* GetMovementCell(const UnitMovementMap& map, u32 tile_x,
    u32 tile_y);
bool IsPassableTerrainCell(const UnitMovementCell& cell);
bool IsUnoccupiedPassableTerrainCell(const UnitMovementCell& cell);
bool LoadUnitDirectionLookupTableFromTrcRecord(UnitDirectionLookupTable& lookup,
    const char* archive_name, u32 record_index);
bool LoadJw207DirectionLookupRecords(UnitDirectionLookupTable& record0_lookup,
    UnitDirectionLookupTable& record1_lookup,
    const char* archive_name = "JW2_07.TRC");
u32 CalculatePointDirectionFromLookup(UnitMovementPoint source,
    UnitMovementPoint target, const UnitDirectionLookupTable& lookup);
u32 CalculatePointDirection16FromLookup(UnitMovementPoint source,
    UnitMovementPoint target, const UnitDirectionLookupTable& lookup);
UnitMovementPoint GetUnitMovementDirection8Delta(u32 direction);
UnitMovementPoint GetUnitMovementDirection16Delta(u32 direction);
i32 LookupUnitMovementDirectionTurnStep(u32 current_direction,
    u32 target_direction, u32 direction_count);
u32 ApplyUnitMovementDirectionTurnStep(u32 current_direction, i32 turn_step,
    u32 direction_count);
UnitMovementDirectionTurnResult PrepareUnitMovementDirectionForStep(
    UnitMovementUnit& unit, u32 target_direction, u32 direction_count);
u32 CalculateUnitDirectionToPoint(const UnitMovementUnit& unit, i32 target_x,
    i32 target_y);
UnitLinkedTargetCheck CheckLinkedTargetStatusRejectingDeadLink(
    UnitMovementContext& context, UnitMovementUnit& unit);
bool CheckTerrainCellAlternateFlag40000000(const UnitMovementContext& context,
    i32 x, i32 y);
UnitLinkedTargetCheck CheckLinkedTargetStatus(UnitMovementContext& context,
    UnitMovementUnit& unit);
UnitArrivalCheck CheckUnitDestinationArrivalStatus(UnitMovementContext& context,
    const UnitMovementUnit& unit);
UnitArrivalCheck CheckUnitRangeDestinationStatus(UnitMovementContext& context,
    const UnitMovementUnit& unit);
UnitTargetBoundsMovementStatus CheckUnitTargetBoundsMovementStatus(
    const UnitMovementUnit& unit);
UnitTargetBoundsMovementStatus CalculateUnitTargetBoundsScratch(
    const UnitMovementUnit& unit);
UnitMovementPoint CalculateUnitMovementCenterPoint(const UnitMovementUnit& unit);
UnitTileSearchResult FindPassableVerticalNudgeTile(const UnitMovementContext& context,
    i32 x, i32 y);
UnitTileSearchResult FindNearestPassableTerrainTile(const UnitMovementContext& context,
    i32 x, i32 y);
UnitTileSearchResult FindNearestUnoccupiedTerrainTile(const UnitMovementContext& context,
    i32 x, i32 y);
u32 ProcessHarvestableTileAmount(UnitMovementMap& map, u32 tile_index, u32 amount);
void RegisterUnitReservedMapTile(UnitMovementContext& context, const UnitMovementUnit& unit);
UnitReservedTileReleaseResult ReleaseUnitReservedMapTileWithIndex(
    UnitMovementContext& context, const UnitMovementUnit& unit);
bool ReleaseUnitReservedMapTile(UnitMovementContext& context, const UnitMovementUnit& unit);
void ProcessInvalidUnitReservedTiles(UnitMovementContext& context);
UnitMovementPoint FindNearestPointInTargetFootprint(const UnitMovementUnit& source,
    const UnitMovementUnit& target);
UnitDropoffSearchResult FindNearestOwnedDropoffBuilding(UnitMovementContext& context,
    const UnitMovementUnit& source);
i32 GetUnitProductionCompletionEffect(const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 effect_index);
u32 AddSignedUnitStatDelta(u32 base_value, i32 delta, u32 minimum_value = 0);
UnitMovementPoint ApplyUnitMovementDeltaModifier(UnitMovementPoint delta, i32 modifier);
u32 CalculateUnitRuntimeMaxHealthWithProductionEffect00(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit);
u32 CalculateUnitHitPointBarFillWithProductionEffect00(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 scale = 4);
bool CheckUnitBelowRuntimeMaxHealthWithProductionEffect00(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit);
void AddUnitHealthClampedToProductionEffect00(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    u32 amount);
u32 CalculateUnitRuntimeMaxSecondaryValueWithProductionEffect01(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit);
bool CheckUnitBelowRuntimeMaxSecondaryValueWithProductionEffect01(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit);
void AddUnitSecondaryValueClampedToProductionEffect01(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    u32 amount);
void AddUnitRuntimeHealthClamped(UnitRuntimeStatBlock& stats, u32 amount);
void AddUnitRuntimeSecondaryClamped(UnitRuntimeStatBlock& stats, u32 amount);
void SubtractUnitRuntimeHealthClamped(UnitRuntimeStatBlock& stats, u32 amount);
void SubtractUnitRuntimeSecondaryClamped(UnitRuntimeStatBlock& stats, u32 amount);
void ReduceUnitRuntimeMaxHealthClamped(UnitRuntimeStatBlock& stats, u32 amount);
void ReduceUnitRuntimeMaxSecondaryClamped(UnitRuntimeStatBlock& stats, u32 amount);
void SubtractUnitRuntimeStat1cClamped(UnitRuntimeStatBlock& stats, u32 amount);
void SubtractUnitRuntimeStat20Clamped(UnitRuntimeStatBlock& stats, u32 amount);
void SubtractUnitRuntimeStat28Clamped(UnitRuntimeStatBlock& stats, u32 amount);
u32 CalculateUnitVariantStepBudget(const UnitMovementUnit& unit);
u32 CalculateUnitVariantScaledBonus61a(const UnitMovementUnit& unit);
u32 CalculateUnitVariantScaledBonus61b(const UnitMovementUnit& unit);
u32 CalculateUnitVariantScaledBonus61c(const UnitMovementUnit& unit);
u32 CalculateUnitVariantScaledBonus61d(const UnitMovementUnit& unit);
UnitVariantGrowthResult CalculateUnitVariantGrowthRoll(const UnitMovementUnit& unit,
    UnitVariantRandomLimitCallback random_limit = nullptr);
void IncreaseUnitVariantStats(UnitMovementUnit& unit, UnitRuntimeStatBlock& stats,
    UnitVariantRandomLimitCallback random_limit = nullptr);
bool DecreaseUnitVariantStats(UnitMovementUnit& unit, UnitRuntimeStatBlock& stats,
    UnitVariantRandomLimitCallback random_limit = nullptr);
bool ApplyUnitVariantProgressFromStoredValue(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    UnitRuntimeStatBlock& stats, bool* attachment_effect_started = nullptr,
    UnitVariantRandomLimitCallback random_limit = nullptr);
void RebuildUnitRuntimeStatsFromDefinitionAndParents(UnitMovementUnit& unit,
    UnitRuntimeStatBlock& stats, const UnitMovementUnit* parent_a = nullptr,
    const UnitRuntimeStatBlock* parent_a_stats = nullptr,
    const UnitMovementUnit* parent_b = nullptr,
    const UnitRuntimeStatBlock* parent_b_stats = nullptr,
    UnitVariantRandomLimitCallback random_limit = nullptr);
u32 ResolveUnitActionProfileIndexForTarget(const UnitMovementUnit& source,
    u32 target_render_class);
u32 CalculateUnitMaxHealthWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit);
u32 CalculateUnitMaxSecondaryValueWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit);
u32 CalculateUnitActionDamageWithDefinitionModifiers(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& source, const UnitMovementUnit& target,
    const UnitActionDamageProfileTable* damage_profiles = nullptr,
    UnitVariantRandomLimitCallback random_limit = nullptr);
u32 CalculateOrder2bAdjustedUnitValue(const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 base_value, u32 per_variant_delta,
    u32 minimum_value = 1);
u32 CalculateUnitActionRecoveryReductionWithProductionEffect05(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit);
u32 CalculateUnitActionRecoveryTicksWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    const UnitEquipmentCatalog* equipment_catalog = nullptr);
u32 CalculateUnitActionRangeScaledBonus(const UnitMovementUnit& unit);
u32 CalculateUnitActionRangeWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 target_render_class, const UnitEquipmentCatalog* equipment_catalog = nullptr);
UnitMovementPoint CalculateUnitMovementFrameDeltaWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    i32 additional_modifier = 0, const UnitEquipmentCatalog* equipment_catalog = nullptr);
UnitMovementPoint CalculateUnitMovementFrameDeltaForDirectionWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 direction, i32 additional_modifier = 0,
    const UnitEquipmentCatalog* equipment_catalog = nullptr);
u32 LookupConfiguredPointDirection(UnitMovementPoint source, UnitMovementPoint target,
    const UnitDirectionLookupTable& lookup, bool use_16_direction_lookup = false);
u32 CalculateUnitMovementStepLimitWithProductionEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 base_step_limit, i32 additional_modifier = 0,
    const UnitEquipmentCatalog* equipment_catalog = nullptr);
u32 CalculateMovementStepDistanceThreshold(u32 step_limit, u32 movement_period);
u32 CalculateMovementStepFromDistance(u32 distance, u32 movement_period);
void ResetUnitMovementInterpolationState(UnitMovementUnit& unit,
    u32 command_metadata_flags = 0,
    std::optional<u32> random_animation_frame = std::nullopt);
void ResetUnitMovementStepProgress(UnitMovementUnit& unit);
UnitMovementStepAdvanceResult AdvanceUnitMovementPositionTowardTarget(
    UnitMovementUnit& unit, u32 direction_count, u32 movement_period,
    u32 map_width_tiles = 0, u32 map_height_tiles = 0);
u32 ResolveMovementStepLimitForDistance(u32 distance, u32 movement_period,
    u32 base_step_limit);
bool AdjustUnitMovementStepAccumulatorTowardLimit(UnitMovementUnit& unit,
    u32 movement_period, u32 step_limit);
UnitMovementCoreUpdateResult UpdateUnitMovementTowardPathTarget(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    const UnitMovementCoreUpdateConfig& config);
u32 CalculateUnitInteractionRangeWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 base_range, const UnitEquipmentCatalog* equipment_catalog = nullptr);
u32 CalculateUnitTransportCapacityWithProductionEffect09(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    u32 base_capacity);
bool CheckUnitCommandGateWithProductionEffect12(
    const ProductionOrderRuntimeState& production_state, const UnitMovementUnit& unit,
    bool helper_allows, u32 definition_flags, u32 command_metadata_flags,
    const UnitEquipmentCatalog* equipment_catalog = nullptr);
void ProcessUnitPathfinderToDestination(UnitMovementContext& context,
    UnitMovementUnit& unit);
bool CheckStraightUnitPathTiles(UnitMovementContext& context, const UnitMovementUnit& unit,
    UnitMovementPoint start_tile, UnitMovementPoint goal_tile);
bool FindNearestPathableGoalTile(UnitMovementContext& context, const UnitMovementUnit& unit,
    UnitMovementPoint start_tile, UnitMovementPoint desired_goal_tile,
    UnitMovementPoint& resolved_goal_tile);
LegacyPathfinderScratchState& GetLegacyPathfinderScratchState();
void ConfigureLegacyPathfinderGrid(i32 width_tiles, i32 height_tiles);
UnitMovementPoint AdvanceLegacyPathfinderNeighborCursor();
bool CheckLegacyPathfinderTileCanEnter(UnitMovementContext& context,
    const UnitMovementUnit& unit, i32 tile_x, i32 tile_y);
bool RunLegacyUnitPathfinder(UnitMovementContext& context, UnitMovementUnit& unit,
    std::vector<UnitMovementPoint>* out_path_tiles = nullptr);
u32 ProcessUnitPathToDestination(UnitMovementContext& context, UnitMovementUnit& unit);
UnitMovementPoint CalculateUnitMovementFrameDelta(const UnitMovementUnit& unit);
bool CheckUnitCanEnterTerrainCell(UnitMovementContext& context, const UnitMovementUnit& unit,
    i32 x, i32 y);
u32 ResolvePathDirectionOrReplan(UnitMovementContext& context, UnitMovementUnit& unit);
u32 FindPassableDirectionRotatingLeft(UnitMovementContext& context,
    UnitMovementUnit& unit);
u32 FindPassableDirectionRotatingRight(UnitMovementContext& context,
    UnitMovementUnit& unit);
bool CheckCurrentDirectionNextPathCell(UnitMovementContext& context,
    const UnitMovementUnit& unit);
bool DispatchTerrainProbeJumpTableEntry(UnitMovementContext& context,
    const UnitMovementUnit& unit, u32 movement_class, i32 x, i32 y);
bool DispatchTerrainClassEntryProbe(UnitMovementContext& context,
    const UnitMovementUnit& unit, i32 x, i32 y);
UnitTerrainClassProbeResult DispatchTerrainClassEntryProbeDetailed(
    UnitMovementContext& context, const UnitMovementUnit& unit, i32 x, i32 y);
void ProcessGroundUnitTerrainStep(UnitMovementContext& context, UnitMovementUnit& unit);
void ProcessAirUnitTerrainStep(UnitMovementContext& context, UnitMovementUnit& unit);
void DrawMovementProbePixelAndPresent(UnitMovementContext& context,
    i32 x, i32 y, u16 color);
bool ProcessUnitMovementStep(UnitMovementContext& context, UnitMovementUnit& unit);
void ProcessUnitAnimationTimer(UnitMovementUnit& unit);
UnitMovementPoint CalculateUnitMovementFrameDeltaForDirection(
    const UnitMovementUnit& unit, u32 direction);
void StoreMovementFrameDeltaScratch(const UnitMovementUnit& unit, u32 direction,
    UnitMovementPoint& out_delta, u32& out_direction);
void HandleUnitRuntimeDeathState(UnitMovementContext& context, UnitMovementUnit& unit);
void HandleAttachedUnitParentDeath(UnitMovementContext& context, UnitMovementUnit& parent);
UnitMovementUnit* HandleFreeUnitActivation(UnitMovementContext& context);
void HandleActiveUnitFreeListMove(UnitMovementContext& context, UnitMovementUnit& unit);
void HandleActiveUnitLifecycleListMove(UnitMovementContext& context,
    UnitMovementUnit& unit);
void HandleLifecycleUnitFreeListMove(UnitMovementContext& context,
    UnitMovementUnit& unit);
void HandleLifecycleUnitActiveListMove(UnitMovementContext& context,
    UnitMovementUnit& unit);
void CopyCString(char* destination, const char* source);
void CopyUnitStringSlotText(char* destination, std::size_t max_count,
    const char* source);
bool CompareUnitStringSlotText(const char* source, const char* destination);
void AppendCString(char* destination, const char* source);
void AppendBoundedCharacter(char* text, std::size_t max_count, char ch);
void RemoveTrailingCharacterRespectingDbcs(char* text, std::size_t max_count);
u32 FindUnitStringSlot(const UnitMovementContext& context, const char* text);
u32 InternUnitStringSlot(UnitMovementContext& context, const char* text);
void ClearUnitStringSlotIfUnused(UnitMovementContext& context, u32 slot_index);
bool ProcessUnitRuntimeStateTick(UnitMovementContext& context, UnitMovementUnit& unit,
    UnitRuntimePreTerrainCallback pre_terrain = nullptr, void* user_data = nullptr);

}
