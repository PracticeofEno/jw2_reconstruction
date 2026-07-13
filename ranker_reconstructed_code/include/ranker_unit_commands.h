#pragma once

#include "ranker_unit_movement.h"

#include <array>
#include <vector>

namespace ranker {

struct MapEffectContext;

constexpr std::size_t kUnitCommandRuntimeStateTableCount = 138;
constexpr u32 kUnitStateTravel = 0x02;
constexpr u32 kUnitStateAttackTravel = 0x03;
constexpr u32 kUnitStateAttackTarget = 0x04;
constexpr u32 kUnitStateRuntimeIdleAcquire = 0x01;
constexpr u32 kUnitStateRuntimeAttackTarget = kUnitStateAttackTarget;
constexpr u32 kUnitStateAssistTarget = 0x05;
constexpr u32 kUnitStateEquipmentPointTravel = 0x06;
constexpr u32 kUnitStateCommand08 = 0x08;
constexpr u32 kUnitStatePointActionReady = 0x09;
constexpr u32 kUnitStatePointActionTravel = 0x0a;
constexpr u32 kUnitStateTargetInteractionCycle = 0x0d;
constexpr u32 kUnitStateTargetInteractionApproach = 0x0e;
constexpr u32 kUnitStateTargetOrPointCommand = 0x0c;
constexpr u32 kUnitStateCommand10 = 0x10;
constexpr u32 kUnitStateTargetProgressStart = kUnitStateCommand10;
constexpr u32 kUnitStateTargetProgressCycle = 0x11;
constexpr u32 kUnitStateTargetProgressApproach = 0x12;
constexpr u32 kUnitStateCommand14 = 0x14;
constexpr u32 kUnitStateFollowPoint = 0x15;
constexpr u32 kUnitStateFollowMovingTarget = 0x16;
constexpr u32 kUnitStateFollowHoldRange = 0x17;
constexpr u32 kUnitStateGuardAnchorStart = kUnitStateFollowHoldRange;
constexpr u32 kUnitStateGuardReturnStart = 0x1b;
constexpr u32 kUnitStateRuntimeTargetValidationStart = 0x1c;
constexpr u32 kUnitStateRuntimeTargetValidation = 0x1d;
constexpr u32 kUnitStateGuardAnchorAction = kUnitStateRuntimeTargetValidation;
constexpr u32 kUnitStateGuardAnchorApproach = 0x1e;
constexpr u32 kUnitStateGuardReturnCommand = 0x1f;
constexpr u32 kUnitStateGuardCombatCycle = 0x20;
constexpr u32 kUnitStateGuardReturnTravel = 0x21;
constexpr u32 kUnitStateGuardPursueTarget = 0x22;
constexpr u32 kUnitStateCommand23 = 0x23;
constexpr u32 kUnitStateLegacySpawnPlacementStart = kUnitStateCommand23;
constexpr u32 kUnitStateLegacySpawnConstruction = 0x24;
constexpr u32 kUnitStateLegacySpawnPlacementApproach = 0x25;
constexpr u32 kUnitStateWorkerApproachHarvest = 0x28;
constexpr u32 kUnitStateWorkerReservedHarvest = 0x29;
constexpr u32 kUnitStateWorkerHarvestTile = kUnitStateWorkerReservedHarvest;
constexpr u32 kUnitStateWorkerReturnToDropoff = 0x2a;
constexpr u32 kUnitStateWorkerDepositCargo = 0x2b;
constexpr u32 kUnitStateWorkerApproachDropoff = 0x2c;
constexpr u32 kUnitStateWorkerHarvestFailed = 0x2d;
constexpr u32 kUnitStatePatrolStart = 0x35;
constexpr u32 kUnitStatePatrolNoop = 0x36;
constexpr u32 kUnitStatePatrolReturnLeg = 0x37;
constexpr u32 kUnitStatePatrolOutboundLeg = 0x38;
constexpr u32 kUnitStatePatrolReturnCombat = 0x39;
constexpr u32 kUnitStatePatrolOutboundCombat = 0x3a;
constexpr u32 kUnitStateCommand3c = 0x3c;
constexpr u32 kUnitStateCarrierImmediateBoarding = 0x3d;
constexpr u32 kUnitStateCarrierApproachBoarding = 0x3e;
constexpr u32 kUnitStatePassengerApproachCarrier = 0x3f;
constexpr u32 kUnitStateTransportUnloadStart = 0x41;
constexpr u32 kUnitStateTransportUnloadChildren = 0x42;
constexpr u32 kUnitStateTransportUnloadWait = 0x43;
constexpr u32 kUnitStateTransportAttached = 0x45;
constexpr u32 kUnitStateCarryAttached = kUnitStateTransportAttached;
constexpr u32 kUnitStateTransportDockStart = 0x48;
constexpr u32 kUnitStateTransportDockSearch = 0x49;
constexpr u32 kUnitStateCompletionAnnouncementStart = 0x4d;
constexpr u32 kUnitStateCompletionAnnouncementTimer = 0x4e;
constexpr u32 kUnitStateProductionSpawnStart = 0x50;
constexpr u32 kUnitStateProductionSpawnCycle = 0x51;
constexpr u32 kUnitStateReservedTileStart = 0x53;
constexpr u32 kUnitStateReservedTileWork = 0x54;
constexpr u32 kUnitStateReservedTileApproach = 0x55;
constexpr u32 kUnitStateReservedTileBlockedWait = 0x56;
constexpr u32 kUnitStateReservedTileRetryDelay = 0x57;
constexpr u32 kUnitStateReservedTileLinkedObject = 0x58;
constexpr u32 kUnitStateSpawnPlacementStart = 0x5a;
constexpr u32 kUnitStateSpawnCreateCycle = 0x5b;
constexpr u32 kUnitStateSpawnPlacementWait = 0x5c;
constexpr u32 kUnitStateLinkedUnitReleaseStart = 0x5f;
constexpr u32 kUnitStateLinkedUnitReleaseCycle = 0x60;
constexpr u32 kUnitStateLinkedUnitReleaseApproach = 0x61;
constexpr u32 kUnitStateSpecialAbilityStart = 0x64;
constexpr u32 kUnitStateSpecialAbilityTimer = 0x65;
constexpr u32 kUnitStateSpecialAbilityApproach = 0x66;
constexpr u32 kUnitStateRestoreTargetCycle = 0x69;
constexpr u32 kUnitStateRestoreTargetApproach = 0x6a;
constexpr u32 kUnitStateRandomRelocation = 0x6c;
constexpr u32 kUnitStateMorphEnterTimer = 0x6e;
constexpr u32 kUnitStateMorphExitTimer = 0x6f;
constexpr u32 kUnitStateValueTransferStart = 0x73;
constexpr u32 kUnitStateValueTransferCycle = 0x74;
constexpr u32 kUnitStateValueTransferApproach = 0x75;
constexpr u32 kUnitStateAnimationTimerOnly = 0x77;
constexpr u32 kUnitStateTimedFlagPhaseA = 0x78;
constexpr u32 kUnitStateTimedFlagPhaseB = 0x79;
constexpr u32 kUnitStateTargetedSpawnPlacementStart = 0x7d;
constexpr u32 kUnitStateTargetedSpawnCycle = 0x7e;
constexpr u32 kUnitStateTargetedSpawnApproach = 0x7f;
constexpr u32 kUnitStateCompletionEffectStart = 0x82;
constexpr u32 kUnitStateCompletionEffectTimer = 0x83;
constexpr u32 kUnitStateItemSlotUseStart = 0x87;
constexpr u32 kUnitStateItemSlotUseAction = 0x88;
constexpr u32 kUnitStateItemSlotUseApproach = 0x89;
constexpr u32 kUnitCommandStateMask = 0x00ffffff;
constexpr u32 kUnitCommandMirrorClearFlag = 0x01000000;
constexpr u32 kUnitCommandNoInterruptFlag = 0x10000000;
constexpr u32 kUnitCommandDeferredEntryFlag = 0x20000000;
constexpr u32 kOwnerTransportQueueSlotCount = 0x20;
constexpr u32 kOwnerTransportQueueStateWorkTarget = 0x02;
constexpr u32 kOwnerTransportQueueStateRouteHelperPending = 0x04;
constexpr u32 kOwnerTransportQueueStateRouteHelperActive = 0x05;
constexpr u32 kOwnerTransportQueueStateRelay0aPending = 0x09;
constexpr u32 kOwnerTransportQueueStateLinkedGroup0a = 0x0a;
constexpr u32 kOwnerTransportQueueStateFallback0b = 0x0b;
constexpr u32 kOwnerTransportQueueStateRelay0ePending = 0x0d;
constexpr u32 kOwnerTransportQueueStateLinkedGroup = 0x0e;
constexpr u32 kOwnerTransportQueueStateDropoff = 0x12;
constexpr u32 kOwnerTransportQueueStatePendingA = 0x13;
constexpr u32 kOwnerTransportQueueStatePendingB = 0x14;
constexpr u32 kOwnerTransportQueueStatePendingC = 0x15;
constexpr u32 kOwnerTransportQueueStateStrategicTarget = 0x16;
constexpr u32 kOwnerTransportQueueStateStrategicTargetHold = 0x18;
constexpr u32 kOwnerTransportQueueStateStrategicTargetAlt = 0x1b;
constexpr u32 kOwnerTransportQueueStateThreatResponse = 0x1f;
constexpr u32 kInvalidOwnerTransportQueueSlot = 0xffffffffu;
constexpr u32 kOwnerUnitTypeCountSlots = 0xaa;
constexpr u32 kOwnerTransportRouteTargetCount = 6;
constexpr u32 kOwnerRouteHelperCandidateClusterCapacity = 0x34;
constexpr u32 kOwnerRouteHelperCandidateClusterTileCapacity = 0x14;
constexpr u32 kOwnerRouteHelperCandidateClusterMergeAxisDistance = 0x12;
constexpr u32 kOwnerRouteHelperExistingUnitBlockDistance = 0x13;
constexpr u32 kOwnerRouteHelperPlacementRetryLimit = 3;
constexpr u32 kOwnerStrategicPointSlotCount = 4;
constexpr u32 kOwnerStrategicPointMergeDistance = 0x100;
constexpr u32 kOwnerNeutralRouteProbeOwnerId = 8;
constexpr u32 kOwnerExtendedProductionUnitTypeBase = 0x60;
constexpr u32 kOwnerProductionDependencyListSlots = 8;
constexpr u32 kOwnerProductionAuxDependencySlotCount = 0x1c;
constexpr u32 kOwnerProductionPlacementTemporaryBlockFlag = 0x20000000;
constexpr u32 kOwnerProductionPlacementMaxFootprintWidth = 10;
constexpr u32 kOwnerProductionPlacementMaxFootprintHeight = 10;
constexpr u32 kOwnerProductionPlacementMaxFootprintTiles =
    kOwnerProductionPlacementMaxFootprintWidth *
    kOwnerProductionPlacementMaxFootprintHeight;
constexpr std::array<u32, 5> kOwnerPrimaryDemandTrackedUnitTypes{
    0x32, 0x16, 0x33, 0x1d, 0x5b};
constexpr std::array<u32, 4> kOwnerTransportRouteTargetUnitTypes{
    0x60, 0x70, 0x80, 0x90};

struct UnitCommandContext;
struct UnitTargetHelperContext;
struct GameSessionAvatarRuntime;
struct OwnerProductionRouteObjectCandidate;
struct OwnerProductionPlacementAnchorSet;

using UnitCommandCallback = void (*)(UnitCommandContext& context, UnitMovementUnit& unit);
using UnitCommandPairCallback = void (*)(UnitCommandContext& context,
    UnitMovementUnit& first, UnitMovementUnit& second);
using UnitCommandTypeReplacementCallback = void (*)(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 old_type, u32 new_type);
using UnitCommandTargetCallback = UnitMovementUnit* (*)(UnitCommandContext& context,
    UnitMovementUnit& unit);
using UnitCommandTargetPredicate = bool (*)(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementUnit& target);
using UnitCommandPairPredicate = bool (*)(UnitCommandContext& context,
    UnitMovementUnit& first, UnitMovementUnit& second);
using UnitCommandAmountCallback = u32 (*)(UnitCommandContext& context,
    UnitMovementUnit& unit);
using UnitCommandRandomLimitCallback = u32 (*)(UnitCommandContext& context,
    u32 limit);
using UnitCommandSpawnCallback = UnitMovementUnit* (*)(UnitCommandContext& context,
    UnitMovementUnit& source, u32 type_id, i32 x, i32 y);
using UnitCommandAbilityPredicate = bool (*)(UnitCommandContext& context,
    UnitMovementUnit& source, UnitMovementUnit* target, u32 ability_id);
using UnitCommandAbilityCallback = void (*)(UnitCommandContext& context,
    UnitMovementUnit& source, UnitMovementUnit* target, u32 ability_id);
using UnitCommandAbilityStartCallback = bool (*)(UnitCommandContext& context,
    UnitMovementUnit& source, UnitMovementUnit* target, u32 ability_id);
using UnitCommandTargetedSpawnEffectCallback = bool (*)(
    UnitCommandContext& context, UnitMovementUnit& source,
    UnitMovementUnit& target, u32 effect_mode);
enum class UnitCommandAbilityGateResult : u32 {
    ready = 0,
    approach = 1,
    fail = 2,
};
using UnitCommandAbilityGateCallback = UnitCommandAbilityGateResult (*)(
    UnitCommandContext& context, UnitMovementUnit& source,
    UnitMovementUnit* target, u32 ability_id);
using UnitCommandAbilityAmountCallback = u32 (*)(UnitCommandContext& context,
    UnitMovementUnit& source, UnitMovementUnit* target, u32 ability_id);
using UnitCommandAbilitySignedAmountCallback = i32 (*)(UnitCommandContext& context,
    UnitMovementUnit& source, UnitMovementUnit* target, u32 ability_id);
using UnitCommandPointCallback = bool (*)(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementPoint& point);
using UnitCommandItemUseCallback = bool (*)(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementUnit* target, u32 item_id, i32 x, i32 y);
using UnitCommandEquipmentProgressEffectCallback = bool (*)(
    UnitCommandContext& context, UnitMovementUnit& unit, u32 effect_id);
using UnitCommandPredicate = bool (*)(UnitCommandContext& context,
    UnitMovementUnit& unit);
using UnitCommandQueuedCallback = void (*)(UnitCommandContext& context,
    UnitMovementUnit& unit, const UnitQueuedCommand& command);
using UnitCommandDefinitionLookupCallback = const UnitMovementDefinition* (*)(
    UnitCommandContext& context, u32 unit_type);
using UnitCommandMetadataFlagsCallback = u32 (*)(
    UnitCommandContext& context, const UnitMovementUnit& unit);
using UnitCommandOversizedTransportPassengerCallback = void (*)(
    const UnitCommandContext& context, const UnitMovementUnit& unit, u32 owner_id,
    u32 group_id, u32 carrier_capacity);
using UnitCommandTargetProgressCallback = void (*)(UnitTargetHelperContext& context,
    UnitMovementUnit& unit);
using UnitCommandTargetProgressPairCallback = void (*)(UnitTargetHelperContext& context,
    UnitMovementUnit& source, UnitMovementUnit& target);
enum class UnitProductionStartFailure : u32 {
    none = 0,
    resources = 1,
    population_limit = 2,
    population_capacity = 3,
    primary_resources = 4,
    secondary_resources = 5,
    placement_failed = 6,
};
using UnitCommandProductionStartFailedCallback = void (*)(
    UnitCommandContext& context, UnitMovementUnit& unit,
    UnitProductionStartFailure failure);
using OwnerProductionPlacementCandidatePredicate = bool (*)(
    UnitMovementPoint world_point, void* user_data);
using OwnerProductionPlacementProducerPredicate = bool (*)(
    const UnitMovementUnit& producer, UnitMovementPoint placement_point,
    void* user_data);
struct OwnerProductionPlacementPathProbeRequest;
struct OwnerProductionPlacementTargetRefreshRequest;
using OwnerProductionPlacementPathProbeCallback = bool (*)(
    const UnitCommandContext& context, const UnitMovementUnit& producer,
    const OwnerProductionPlacementPathProbeRequest& request, void* user_data);
using OwnerProductionPlacementTargetRefreshCallback = bool (*)(
    const UnitCommandContext& context, const UnitMovementUnit& producer,
    const OwnerProductionPlacementTargetRefreshRequest& request,
    UnitMovementPoint& refreshed_target_point, void* user_data);
using OwnerProductionPlacementNearbyProbePredicate = bool (*)(
    const UnitMovementUnit& unit, void* user_data);
using OwnerRouteHelperPathScoreCallback = bool (*)(
    UnitMovementPoint origin_world, UnitMovementPoint candidate_world,
    u32& score, void* user_data);
using OwnerRouteHelperPlacementPredicate = bool (*)(
    const UnitMovementMap& map, UnitMovementPoint tile,
    const UnitMovementDefinition& helper_definition, u32 terrain_group,
    void* user_data);
using OwnerProductionDefinitionLookupCallback = const UnitMovementDefinition* (*)(
    u32 unit_type, void* user_data);

struct UnitCommandCallbacks {
    UnitCommandTargetCallback find_target = nullptr;
    UnitCommandTargetCallback find_source_bounds_target = nullptr;
    UnitCommandTargetCallback find_nearby_follow_target = nullptr;
    UnitCommandTargetCallback find_dropoff = nullptr;
    UnitCommandTargetPredicate can_attack_target = nullptr;
    UnitCommandTargetPredicate target_in_action_range = nullptr;
    UnitCommandTargetPredicate can_follow_target = nullptr;
    UnitCommandPairPredicate can_board_transport = nullptr;
    UnitCommandTargetPredicate validate_action_target = nullptr;
    UnitCommandCallback dispatch_attack = nullptr;
    UnitCommandCallback reset_to_idle = nullptr;
    UnitCommandCallback on_target_validation_failed = nullptr;
    UnitCommandCallback on_cargo_deposited = nullptr;
    UnitCommandPairCallback on_unit_boarded = nullptr;
    UnitCommandPairCallback on_unit_unloaded = nullptr;
    UnitCommandCallback on_command_acknowledged = nullptr;
    UnitCommandCallback on_harvest_frame = nullptr;
    UnitCommandCallback on_equipment_slots_changed = nullptr;
    UnitCommandTargetProgressPairCallback on_target_progress = nullptr;
    UnitCommandTargetProgressPairCallback on_target_progress_complete = nullptr;
    UnitCommandTargetProgressCallback on_local_target_progress_blocked = nullptr;
    UnitCommandCallback on_reserved_tile_impact_frame = nullptr;
    UnitCommandPredicate on_reserved_tile_work_complete = nullptr;
    UnitCommandAmountCallback harvest_amount = nullptr;
    UnitCommandAmountCallback status_secondary_recharge_amount = nullptr;
    UnitCommandRandomLimitCallback random_limit = nullptr;
    UnitVariantRandomLimitCallback variant_random_limit = nullptr;
    UnitCommandSpawnCallback create_unit = nullptr;
    UnitCommandDefinitionLookupCallback find_definition = nullptr;
    UnitCommandMetadataFlagsCallback command_metadata_flags = nullptr;
    UnitCommandAbilityGateCallback ability_gate = nullptr;
    UnitCommandAbilityPredicate can_use_ability = nullptr;
    UnitCommandAbilityCallback execute_ability = nullptr;
    UnitCommandAbilityStartCallback start_ability_attachment = nullptr;
    UnitCommandTargetedSpawnEffectCallback start_targeted_spawn_effect = nullptr;
    UnitCommandCallback on_spawn_cycle_started = nullptr;
    UnitCommandCallback on_spawn_cycle_failed = nullptr;
    UnitCommandPairCallback on_targeted_spawn_linked = nullptr;
    UnitCommandAbilityAmountCallback ability_secondary_cost = nullptr;
    UnitCommandAbilitySignedAmountCallback ability_target_health_delta = nullptr;
    UnitCommandAbilityPredicate ability_updates_direction = nullptr;
    UnitCommandPointCallback find_relocation_point = nullptr;
    UnitCommandPointCallback find_matching_terrain_placement_point = nullptr;
    UnitCommandPointCallback find_strict_placement_point = nullptr;
    UnitCommandCallback clear_footprint = nullptr;
    UnitCommandCallback set_footprint = nullptr;
    UnitCommandItemUseCallback use_item_slot = nullptr;
    UnitCommandEquipmentProgressEffectCallback start_equipment_progress_effect =
        nullptr;
    UnitCommandPairCallback on_unit_spawned = nullptr;
    UnitCommandCallback on_construction_completed = nullptr;
    UnitCommandPairCallback on_linked_unit_released = nullptr;
    UnitCommandPairCallback on_linked_unit_attached = nullptr;
    UnitCommandPairCallback on_linked_unit_detached = nullptr;
    UnitCommandTypeReplacementCallback on_unit_type_replaced = nullptr;
    UnitCommandProductionStartFailedCallback on_linked_release_population_blocked =
        nullptr;
    UnitCommandAmountCallback production_type_id = nullptr;
    UnitCommandAmountCallback production_resource_cost = nullptr;
    UnitCommandAmountCallback production_secondary_cost = nullptr;
    UnitCommandAmountCallback production_population_cost = nullptr;
    UnitCommandAmountCallback production_spawn_duration = nullptr;
    UnitCommandPredicate can_start_completion_announcement = nullptr;
    UnitCommandPredicate advance_completion_effect = nullptr;
    UnitCommandPredicate advance_completion_announcement = nullptr;
    UnitCommandCallback on_completion_effect = nullptr;
    UnitCommandCallback on_completion_announcement = nullptr;
    UnitCommandCallback on_regen_error = nullptr;
    UnitCommandCallback on_production_started = nullptr;
    UnitCommandProductionStartFailedCallback on_production_start_failed_reason =
        nullptr;
    UnitCommandCallback on_production_start_failed = nullptr;
    UnitCommandPairCallback on_production_completed = nullptr;
    UnitCommandCallback on_production_refunded = nullptr;
    UnitCommandCallback on_runtime_death_marked = nullptr;
    UnitCommandCallback on_runtime_death_sound = nullptr;
    UnitCommandCallback on_runtime_death_accounting = nullptr;
    UnitCommandQueuedCallback on_deferred_death_command_refund = nullptr;
    UnitCommandOversizedTransportPassengerCallback
        on_oversized_transport_passenger = nullptr;
};

struct UnitCommandContext {
    UnitMovementContext* movement = nullptr;
    MapEffectContext* map_effects = nullptr;
    const ProductionOrderRuntimeState* production_state = nullptr;
    const UnitEquipmentCatalog* equipment_catalog = nullptr;
    UnitCommandCallbacks callbacks;
    const std::array<u32, kUnitCommandRuntimeStateTableCount>*
        command_runtime_state_table = nullptr;
    std::array<u32, 16> owner_resources{};
    std::array<u32, 16> owner_secondary_resources{};
    // Original DAT_007072ac: resource/session counter included in result totals.
    std::array<u32, 16> owner_resource_score{};
    // Original DAT_007259c4: population capacity supplied by class-2 units.
    std::array<u32, 16> owner_population_used{};
    std::array<u32, 16> owner_population_limit{};
    // Original DAT_00725a14: live or reserved population demand.
    std::array<u32, 16> owner_population_reserved{};
    std::array<u32, 16> owner_relation_masks{};
    u32 local_owner_id = 0;
    u32 frame_counter = 0;
};

struct OwnerTransportQueueSlot {
    u32 count = 0;
    u32 state = 0;
    u32 completed_count = 0;
    u32 phase_ticks = 0;
    u32 aux_value = 0;
    u32 match_value = 0;
    i32 target_x = 0;
    i32 target_y = 0;
    u32 route_index = 0;
    u32 linked_group = 0;
};

struct OwnerTransportQueueState {
    std::array<OwnerTransportQueueSlot, kOwnerTransportQueueSlotCount> slots{};
};

struct OwnerThreatPointCrossOwnerResponseQueues {
    std::array<OwnerTransportQueueState*, 8> queues{};
    std::array<bool, 8> active_owner_slots{};
};

struct OwnerStrategicTargetState {
    u32 target_owner_id = kInvalidOwnerTransportQueueSlot;
    u32 blocked_owner_mask = 0;
    UnitMovementUnit* preferred_target = nullptr;
    UnitMovementPoint preferred_target_point{-1, -1};
    UnitMovementPoint strategic_point{-1, -1};
    bool has_preferred_target = false;
    bool has_strategic_point = false;
};

struct OwnerStrategicPointList {
    std::array<UnitMovementPoint, kOwnerStrategicPointSlotCount> points{
        UnitMovementPoint{-1, -1}, UnitMovementPoint{-1, -1},
        UnitMovementPoint{-1, -1}, UnitMovementPoint{-1, -1}};
};

struct OwnerPathWindowSelection {
    UnitMovementPoint tile{-1, -1};
    UnitMovementPoint world_point{-1, -1};
    u32 score = 0;
    bool has_point = false;
};

struct OwnerTransportQueueTargetSnapshot {
    u32 match_value = 0;
    i32 target_x = 0;
    i32 target_y = 0;
    u32 route_index = 0;
};

struct OwnerTransportRouteTarget {
    UnitMovementUnit* unit = nullptr;
    OwnerTransportQueueTargetSnapshot target;
    u32 desired_count_base = 0;
    u32 secondary_count_base = 0;
    u32 priority = 0;
    u32 flags = 0;
};

struct OwnerTransportRouteState {
    std::array<OwnerTransportRouteTarget, kOwnerTransportRouteTargetCount> targets{};
    u32 route_count = 0;
    u32 load_percent = 0;
};

struct OwnerTransportRouteMetrics {
    u32 passable_tile_count = 0;
    u32 harvest_amount_sum = 0;
    UnitMovementPoint nearest_tile{-1, -1};
    bool found_nearest_tile = false;
};

struct OwnerRouteTargetProbe {
    UnitMovementUnit* unit = nullptr;
    UnitMovementPoint point{-1, -1};
    bool has_point = false;
};

struct OwnerRouteHelperQueueSummary {
    u32 active_slot_count = 0;
    u32 reserved_resource_cost = 0;
};

struct OwnerRouteHelperProductionSummary {
    u32 scanned_slot_count = 0;
    u32 underloaded_slot_count = 0;
    u32 metadata_blocked_count = 0;
    u32 queued_command_count = 0;
    u32 resource_spent = 0;
    bool stopped_for_resource_limit = false;
};

struct OwnerRouteHelperDispatchResult {
    bool assigned = false;
    u32 slot_index = kInvalidOwnerTransportQueueSlot;
    UnitMovementUnit* producer_unit = nullptr;
    UnitMovementPoint target_tile{-1, -1};
    UnitMovementPoint target_world_point{-1, -1};
};

struct OwnerRouteHelperCandidateCluster {
    bool blocked = false;
    u32 terrain_group = 0;
    u32 resource_amount_sum = 0;
    UnitMovementPoint center_tile{-1, -1};
    std::array<UnitMovementPoint, kOwnerRouteHelperCandidateClusterTileCapacity> tiles{};
    u32 tile_count = 0;
};

struct OwnerRouteHelperCandidateSet {
    std::array<OwnerRouteHelperCandidateCluster, kOwnerRouteHelperCandidateClusterCapacity>
        clusters{};
    u32 cluster_count = 0;
};

struct OwnerRouteHelperPlacementSearchResult {
    bool found = false;
    UnitMovementPoint tile{-1, -1};
    u32 cluster_index = kInvalidOwnerTransportQueueSlot;
    u32 rejected_cluster_count = 0;
    u32 score = 0xffffffff;
};

struct OwnerTransportRouteTargetMaintenanceResult {
    u32 route_count = 0;
    u32 depleted_target_count = 0;
    u32 overflow_action_count = 0;
    u32 overflow_moved_count = 0;
    OwnerRouteHelperProductionSummary helper_production_summary{};
    OwnerRouteHelperQueueSummary helper_queue_summary{};
    OwnerRouteHelperPlacementSearchResult helper_placement{};
    OwnerRouteHelperDispatchResult helper_dispatch{};
};

struct OwnerUnitTypeCounts {
    std::array<u32, kOwnerUnitTypeCountSlots> counts{};
};

struct OwnerProductionDemandRule {
    std::array<u32, 3> unit_types{
        kInvalidOwnerTransportQueueSlot, kInvalidOwnerTransportQueueSlot,
        kInvalidOwnerTransportQueueSlot};
    std::array<u32, 3> percents{};
};

struct OwnerProductionDemandState {
    OwnerUnitTypeCounts base_demand;
    OwnerUnitTypeCounts bonus_demand;
};

struct OwnerProductionDemandAlias {
    u32 target_unit_type = kInvalidOwnerTransportQueueSlot;
    u32 source_unit_type = kInvalidOwnerTransportQueueSlot;
    u32 multiplier = 1;
};

enum class OwnerProductionBuildAction : u32 {
    no_producer_available = 0,
    blocked_by_resource_budget = 1,
    missing_producer_unit = 2,
    build_missing_prerequisite = 3,
    unlock_missing_dependency = 4,
    run_special_pairing = 5,
    resolve_transport_dependency = 6,
    use_producer_unit = 7,
};

struct OwnerProductionDependencyRequest {
    u32 unit_type = kInvalidOwnerTransportQueueSlot;
    u32 producer_unit_type = kInvalidOwnerTransportQueueSlot;
    u32 resource_cost = 0;
    u32 direct_dependency_unit_type = kInvalidOwnerTransportQueueSlot;
    u32 unlock_dependency_unit_type = kInvalidOwnerTransportQueueSlot;
    bool unlock_dependency_available = true;
    std::array<u32, kOwnerProductionDependencyListSlots> prerequisite_unit_types{};
    u32 prerequisite_count = 0;
    std::array<u32, kOwnerProductionDependencyListSlots> special_dependency_unit_types{};
    u32 special_dependency_count = 0;
};

struct OwnerProductionBuildActionResult {
    OwnerProductionBuildAction action =
        OwnerProductionBuildAction::no_producer_available;
    UnitMovementUnit* producer_unit = nullptr;
    u32 dependency_unit_type = kInvalidOwnerTransportQueueSlot;
};

struct OwnerProductionRouteObjectRequirement {
    u32 required_unit_type = kInvalidOwnerTransportQueueSlot;
    u32 object_type = 0;
};

struct OwnerProductionDemandBuildPlanInput {
    const OwnerUnitTypeCounts* owner_unit_counts = nullptr;
    const OwnerUnitTypeCounts* target_owner_counts = nullptr;
    const std::array<u32, kOwnerUnitTypeCountSlots>* producer_unit_types = nullptr;
    const std::array<OwnerProductionRouteObjectRequirement,
        kOwnerUnitTypeCountSlots>* route_object_requirements = nullptr;
    const std::array<u32, kOwnerProductionAuxDependencySlotCount>*
        aux_dependency_producer_unit_types = nullptr;
    const std::array<OwnerProductionDemandRule, kOwnerUnitTypeCountSlots>*
        target_composition_rules = nullptr;
    const OwnerTransportRouteState* route_state = nullptr;
    const OwnerTransportQueueState* transport_queue = nullptr;
    std::vector<OwnerProductionRouteObjectCandidate>* route_objects = nullptr;
    const OwnerProductionPlacementAnchorSet* placement_anchors = nullptr;
    UnitMovementPoint owner_target_point{-1, -1};
    OwnerProductionPlacementPathProbeCallback placement_path_probe = nullptr;
    void* placement_path_probe_user_data = nullptr;
    OwnerProductionPlacementTargetRefreshCallback placement_target_refresh =
        nullptr;
    void* placement_target_refresh_user_data = nullptr;
    OwnerProductionPlacementNearbyProbePredicate placement_nearby_probe = nullptr;
    OwnerProductionPlacementProducerPredicate placement_producer_predicate = nullptr;
    void* placement_producer_user_data = nullptr;
    OwnerProductionDefinitionLookupCallback definition_lookup = nullptr;
    void* definition_lookup_user_data = nullptr;
    u32* owner_shared_dependency_flags = nullptr;
    u32 owner_shared_dependency_flag_count = 0;
    u32 owner_faction = 0;
    std::array<u32, 4> faction_resource_budget_unit_types{
        0x62, 0x72, 0x82, 0x92};
    std::array<u32, 4> faction_primary_unit_types{
        0x00, 0x10, 0x20, 0x30};
    std::array<u32, 4> faction_carrier_unit_types{
        0x05, 0x18, 0x29, 0x34};
    std::array<u32, 4> faction_primary_combat_unit_types{
        0x05, 0x16, 0x26, 0x33};
    u32 resource_budget_base = 0;
    u32 resource_budget_percent = 0;
    u32 resource_budget_cap_base = 0;
    u32 target_composition_percent_bonus = 0;
    u32 carrier_deficit = 0;
    u32 reserved_resource_cost = 0;
};

struct OwnerProductionDemandBuildPlanResult {
    OwnerProductionDemandState demand_state;
    u32 reserved_resource_cost = 0;
    u32 issued_primary_count = 0;
    u32 raised_producer_demand_count = 0;
    u32 raised_dependency_demand_count = 0;
    u32 blocked_by_resource_count = 0;
    u32 unavailable_producer_count = 0;
    u32 special_pairing_count = 0;
    u32 marked_unlock_dependency_count = 0;
    u32 route_object_assigned_count = 0;
    u32 route_object_unhandled_count = 0;
    u32 raised_aux_dependency_demand_count = 0;
    u32 issued_aux_dependency_count = 0;
    u32 aux_blocked_by_resource_count = 0;
    u32 issued_extended_count = 0;
    u32 extended_missing_placement_count = 0;
    u32 extended_blocked_by_resource_count = 0;
    u32 raised_extended_dependency_demand_count = 0;
};

struct OwnerProductionPlacementFootprintOverlay {
    UnitMovementPoint origin_tile{};
    u32 width = 0;
    u32 height = 0;
    bool applied = false;
    std::array<u32, kOwnerProductionPlacementMaxFootprintTiles>
        original_visibility_flags{};
};

struct OwnerProductionPlacementSearchResult {
    UnitMovementPoint point{-1, -1};
    bool found = false;
};

struct OwnerProductionPlacementClearanceResult {
    UnitMovementPoint point{-1, -1};
    u32 score = 0;
    bool found = false;
};

struct OwnerProductionPlacementAnchorSet {
    UnitMovementPoint base_tile{-1, -1};
    UnitMovementPoint source_center_tile{-1, -1};
    std::array<UnitMovementPoint, 2> placement_class_1_points{
        UnitMovementPoint{-1, -1}, UnitMovementPoint{-1, -1}};
    std::array<UnitMovementPoint, 3> placement_class_2_points{
        UnitMovementPoint{-1, -1}, UnitMovementPoint{-1, -1},
        UnitMovementPoint{-1, -1}};
    std::array<UnitMovementPoint, 2> placement_class_3_points{
        UnitMovementPoint{-1, -1}, UnitMovementPoint{-1, -1}};
    std::array<UnitMovementPoint, 3> placement_class_4_points{
        UnitMovementPoint{-1, -1}, UnitMovementPoint{-1, -1},
        UnitMovementPoint{-1, -1}};
    UnitMovementPoint placement_class_5_point{-1, -1};
};

enum class OwnerProductionPlacementGateBlockReason : u32 {
    open = 0,
    out_of_bounds = 1,
    missing_definition = 2,
    blocked_cell = 3,
    terrain_class_mismatch = 4,
    route_target_near_passable_tile = 5,
    active_unit_collision = 6,
};

struct OwnerProductionPlacementGateResult {
    bool blocked = false;
    OwnerProductionPlacementGateBlockReason reason =
        OwnerProductionPlacementGateBlockReason::open;
    UnitMovementPoint tile{-1, -1};
};

struct OwnerProductionPlacementPathProbeRequest {
    const UnitMovementUnit* related_unit = nullptr;
    UnitMovementPoint start_tile{-1, -1};
    // Original DAT_01239c68/6c stores the owner production target in tile units.
    UnitMovementPoint target_point{-1, -1};
    u32 distance = 0xffffffff;
};

struct OwnerProductionPlacementTargetRefreshRequest {
    u32 owner_id = 0;
    const OwnerTransportRouteState* route_state = nullptr;
    // Original DAT_01239c68/6c stores the owner production target in tile units.
    UnitMovementPoint current_target_point{-1, -1};
};

struct OwnerProductionPlacementPathAvailabilityResult {
    bool available = false;
    bool special_type_bypass = false;
    bool footprint_overlay_applied = false;
    bool target_refresh_checked = false;
    bool target_refreshed = false;
    bool direct_probe_checked = false;
    u32 nearby_probe_count = 0;
    const UnitMovementUnit* failed_unit = nullptr;
    UnitMovementPoint failed_start_tile{-1, -1};
    UnitMovementPoint target_point{-1, -1};
};

struct OwnerProductionRouteObjectCandidate {
    u32 object_type = 0;
    u32 flags = 0;
    UnitMovementPoint point{};
    u32 owner_visibility_mask = 0;
    u32 source_object_index = kInvalidOwnerTransportQueueSlot;
    bool source_is_map_effect_object = false;
    UnitMovementUnit* assigned_unit = nullptr;
};

enum class OwnerProductionRouteObjectAssignmentCode : u32 {
    assigned = 0,
    missing_required_unit = 1,
    missing_object_type = 2,
    no_visible_object = 3,
};

struct OwnerProductionRouteObjectAssignmentResult {
    OwnerProductionRouteObjectAssignmentCode code =
        OwnerProductionRouteObjectAssignmentCode::missing_required_unit;
    UnitMovementUnit* worker = nullptr;
    OwnerProductionRouteObjectCandidate* object = nullptr;
};

struct OwnerUnitCountAndWeightSummary {
    u32 count = 0;
    u32 weight = 0;
};

struct OwnerThreatPointPressureSummary {
    u32 count = 0;
    u32 weight = 0;
};

struct OwnerThreatPointResponseResult {
    u32 slot_index = kInvalidOwnerTransportQueueSlot;
    OwnerThreatPointPressureSummary pressure{};
    u32 requested_weight = 0;
    u32 moved_count = 0;
    u32 moved_weight = 0;
    bool allocated_slot = false;
    bool cleared = false;
};

struct OwnerTransportQueueLoadSummary {
    u32 state02_03_unit_count = 0;
    u32 state06_07_weight = 0;
    u32 state08_weight = 0;
    u32 state16_1b_weight = 0;
    u32 state1c_1e_weight = 0;
    u32 state1f_21_weight = 0;
};

struct OwnerTransportQueueRetargetResult {
    u32 moved_count = 0;
    u32 destination_slot = kInvalidOwnerTransportQueueSlot;
};

struct OwnerTransportQueueLimitAction {
    u32 slot_index = kInvalidOwnerTransportQueueSlot;
    u32 desired_count = 0;
};

struct OwnerTransportQueueLimitPlan {
    std::array<OwnerTransportQueueLimitAction, kOwnerTransportQueueSlotCount> actions{};
    u32 action_count = 0;
};

struct OwnerTransportQueueOverflowTarget {
    u32 slot_index = kInvalidOwnerTransportQueueSlot;
    u32 route_index = kInvalidOwnerTransportQueueSlot;
    u32 move_count = 0;
};

using OwnerTransportQueueUnitWeightCallback = u32 (*)(const UnitMovementUnit& unit);
using OwnerUnitEligibilityCallback = bool (*)(const UnitMovementUnit& unit);
using OwnerStrategicUnitPredicate = bool (*)(const UnitMovementUnit& unit);
using OwnerStrategicTilePredicate = bool (*)(const UnitMovementCell& cell);
using OwnerThreatPointHandler = void (*)(u32 owner_id,
    UnitMovementPoint& threat_point, void* user_data);
using OwnerTransportQueueAssignUnitCallback = void (*)(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementUnit& unit, void* user_data);
using OwnerTransportQueueUnitTickCallback = void (*)(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementUnit& unit, void* user_data);
using OwnerTransportStrategicPointRefreshCallback = bool (*)(
    UnitCommandContext& context, u32 owner_id,
    OwnerStrategicTargetState& strategic_target, void* user_data);
using OwnerTransportCarrierCapacityCallback = u32 (*)(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 carrier_slot_index, u32 passenger_group, void* user_data);
using OwnerTransportUnitMetadataFlagsCallback = u32 (*)(
    const UnitMovementUnit& unit, void* user_data);

struct OwnerTransportQueueAssignmentInput {
    const OwnerUnitTypeCounts* owner_unit_counts = nullptr;
    const OwnerUnitTypeCounts* target_owner_counts = nullptr;
    const OwnerTransportRouteState* route_state = nullptr;
    const OwnerStrategicTargetState* strategic_target = nullptr;
    bool script_trigger_gate_open = false;
    bool unit_in_active_script_trigger_group = false;
    bool strategic_queue_target_enabled = false;
    UnitMovementPoint strategic_queue_target_point{-1, -1};
    u32 strategic_queue_load_percent = 0;
    u32 owner_faction = 0;
    std::array<u32, 4> faction_carrier_unit_types{
        0x05, 0x18, 0x29, 0x34};
    std::array<u32, 4> faction_primary_combat_unit_types{
        0x05, 0x16, 0x26, 0x33};
    u32 carrier_capacity = 0;
    OwnerTransportQueueUnitWeightCallback unit_weight = nullptr;
};

struct OwnerTransportQueueAssignmentResult {
    u32 slot_index = kInvalidOwnerTransportQueueSlot;
    u32 queue_state = 0;
    u32 overflow_moved_count = 0;
    bool assigned = false;
    bool used_script_trigger_group = false;
};

struct OwnerTransportQueueMaintenanceCallbacks {
    OwnerTransportQueueAssignUnitCallback assign_queue_slot = nullptr;
    OwnerTransportQueueUnitTickCallback tick_unit = nullptr;
    OwnerTransportStrategicPointRefreshCallback refresh_strategic_point = nullptr;
    OwnerTransportCarrierCapacityCallback carrier_capacity = nullptr;
    OwnerTransportUnitMetadataFlagsCallback unit_metadata_flags = nullptr;
};

struct OwnerTransportQueueMaintenanceInput {
    OwnerTransportRouteState* route_state = nullptr;
    OwnerStrategicTargetState* strategic_target = nullptr;
    OwnerTransportQueueMaintenanceCallbacks callbacks;
    void* user_data = nullptr;
    bool carrier_population_gate_open = true;
};

struct OwnerTransportQueueMaintenanceScratch {
    std::array<u32, kOwnerTransportQueueSlotCount> active_slot_counts{};
    u32 owner_phase_state = 1;
    u32 assigned_unit_count = 0;
    u32 ticked_unit_count = 0;
    u32 cleared_slot_count = 0;
    bool aborted_for_missing_strategic_point = false;
};

u32 GetUnitCommandIdLow24(const UnitMovementUnit& unit);
u32 ResolveUnitRuntimeStateFromCommandTable(
    const UnitMovementUnit& unit,
    const std::vector<u32>* command_state_table = nullptr);
u32 GetUnitCommandMetadataFlags(const UnitMovementUnit& unit,
    const std::vector<u32>* command_metadata_table = nullptr);
void SetUnitCommandTarget(UnitMovementUnit& unit, UnitMovementUnit* target);
void SetUnitPathTarget(UnitMovementUnit& unit, i32 x, i32 y);
bool CheckPathTargetWithinAxisTile(const UnitMovementUnit& unit);
bool CheckCurrentTargetFootprintSeparated(const UnitMovementUnit& unit);
bool CheckCurrentTargetFootprintSeparated(UnitCommandContext& context,
    UnitMovementUnit& unit);
bool CheckNearbyFollowCommandTarget(UnitCommandContext& context, UnitMovementUnit& unit);
bool CheckOwnerHasUnitProductionCosts(const UnitCommandContext& context,
    u32 owner_id, u32 primary_cost, u32 secondary_cost);
void HandleOwnerUnitProductionCostDebit(UnitCommandContext& context,
    u32 owner_id, u32 primary_cost, u32 secondary_cost);
void HandleOwnerUnitProductionCostRefund(UnitCommandContext& context,
    u32 owner_id, u32 primary_cost, u32 secondary_cost);
void ResetUnitCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void PopDeferredUnitCommandOrReturnIdle(UnitCommandContext& context,
    UnitMovementUnit& unit);
bool FilterPendingUnitCommandInterrupt(UnitMovementUnit& unit);
void HandlePendingUnitCommandDispatch(UnitCommandContext& context,
    UnitMovementUnit& unit);
void DispatchUnitCommandStateEntry(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 command_state);
void ClearUnitRuntimeFlagAndReturnIdle(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitReturnToIdleState(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitValueTransferEntry(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 command_value);
void StartUnitCommandState08Entry(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitTargetOrPointCommandEntry(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitState10OrTargetedSpawnEntry(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitState14AndClearRuntimeFlag(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartTargetValidationClearingTargetFlag(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartTargetValidationSettingTargetFlag(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitItemSlotUseEntry(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitMorphEnterIfAvailable(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitMorphExitIfFlagged(UnitCommandContext& context,
    UnitMovementUnit& unit);
bool SetUnitTargetReservationFlag(UnitMovementUnit* unit);
bool ClearUnitTargetReservationFlag(UnitMovementUnit* unit);
void StartState23OrSpawnPlacementEntry(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartHarvestOrReservedWorkEntry(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartPatrolCommandEntry(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitCommandState3cEntry(UnitCommandContext& context, UnitMovementUnit& unit);
void StartTransportUnloadCommandEntry(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartLinkedReleaseOrPopNextCommand(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitProductionSpawnEntry(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitCompletionAnnouncementEntry(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartCompletionEffectCommandEntry(UnitCommandContext& context,
    UnitMovementUnit& unit);
void MarkUnitCommandDeadEntry(UnitCommandContext& context, UnitMovementUnit& unit);
void MarkUnitCommandDeferredEntry(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitCommandLockoutTimer(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 ticks);
void DispatchUnitRuntimeCommandState(UnitCommandContext& context,
    UnitMovementUnit& unit);
void DispatchExtendedUnitRuntimeCommandState(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitRuntimeDispatchTick(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitRuntimeIdleAcquireState(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitRuntimeAttackTargetState(UnitCommandContext& context,
    UnitMovementUnit& unit);
void TickUnitRuntimeAuxTimerReset(UnitMovementUnit& unit);
void StartUnitRuntimeTargetValidationState(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitRuntimeTargetValidationState(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitDeathCommandQueueSideEffects(UnitCommandContext& context,
    UnitMovementUnit& unit);
bool PushDeferredUnitCommand(UnitMovementUnit& unit,
    const UnitQueuedCommand& command, u32 max_deferred_count = 10);
using UnitDeferredCommandCommitCallback = bool (*)(UnitMovementUnit& unit,
    u32 payload, void* user_data);
bool CommitThenPushDeferredUnitCommand(UnitMovementUnit& unit,
    const UnitQueuedCommand& command, u32 max_deferred_count,
    UnitDeferredCommandCommitCallback commit, void* user_data = nullptr);
bool IsProductionOrderCancelLogicalIndexAllowed(u32 logical_index);
bool SetOrQueueUnitCommandPayload(UnitMovementUnit* unit,
    const UnitQueuedCommand& command, bool enqueue_deferred,
    u32 max_deferred_count = 10);
bool SetOrQueueUnitPointCommand01(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred);
bool SetOrQueueUnitCommand00(UnitMovementUnit* unit, bool enqueue_deferred);
bool SetOrQueueUnitTargetCommand03(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred);
bool SetOrQueueUnitCommand02(UnitMovementUnit* unit, u32 fallback_command_value,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred);
bool SetOrQueueUnitTargetPointCommand04(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred);
bool SetOrQueueUnitConditionalTargetPointCommand05(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred);
bool SetOrQueueUnitAlignedPointCommand06(UnitMovementUnit* unit, u32 command_value,
    i32 x, i32 y, bool enqueue_deferred);
bool SetOrQueueUnitPointCommand07(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred);
bool SetOrQueueUnitPointCommand09(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred);
bool SetOrQueueUnitTargetCommand0a(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred);
bool SetOrQueueUnitTargetCommand0b(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred);
bool SetOrQueueUnitCommand10(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred);
bool SetOrQueueUnitCommand11(UnitMovementUnit* unit, bool enqueue_deferred);
bool SetOrQueueUnitCommand17(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred);
bool SetOrQueueUnitCommand1b(UnitMovementUnit* unit, bool enqueue_deferred);
bool SetUnitCommandTargetReferencePoint(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y);
bool SetOrQueueUnitCommand21AndSetRuntimeFlag(UnitMovementUnit* unit,
    bool enqueue_deferred);
bool SetOrQueueUnitCommand22(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred);
bool SetOrQueueUnitTargetCommand23(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, bool enqueue_deferred);
bool SetOrQueueUnitPointCommand24(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred);
void HandleNoOpUnitCommandEntry();
bool SetOrQueueUnitExtendedStateCommand(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, u32 state_index,
    bool enqueue_deferred);
void WriteOrQueueUnitExtendedCommandPayload(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, u32 state_index,
    bool enqueue_deferred);
void StartUnitCompletionEffectSequence(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitCompletionEffectTimer(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitCompletionAnnouncementCommand(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitCompletionAnnouncementTimer(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitProductionSpawnCommand(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitProductionSpawnCycle(UnitCommandContext& context,
    UnitMovementUnit& unit);
bool ApplyGameSessionAvatarProductionRecord(UnitCommandContext& context,
    UnitMovementUnit& produced, const GameSessionAvatarRuntime& runtime,
    u32 player_index, u32 slot_id);
void ProcessUnitIdleAcquireCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessUnitTravelCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessUnitAttackTravelCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessUnitAttackTargetCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitEquipmentPointCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitEquipmentPointTravel(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitPointActionCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitPointActionCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitPointActionTravel(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitTargetInteractionCommand(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitTargetInteractionCycle(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitTargetInteractionApproach(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitTargetProgressCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitTargetProgressCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitTargetProgressApproach(UnitCommandContext& context,
    UnitMovementUnit& unit);
void StartUnitGuardAnchorCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitGuardAnchorAction(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitGuardAnchorApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitGuardReturnCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitGuardCombatCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitGuardReturnTravel(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitGuardPursueTarget(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitLegacySpawnPlacementCommand(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleLegacySpawnedConstructionRelease(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitLegacySpawnPlacementApproach(UnitCommandContext& context,
    UnitMovementUnit& unit);
void ProcessUnitFollowTargetStart(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessUnitFollowPointTravel(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessUnitFollowMovingTarget(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessUnitFollowHoldRange(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessWorkerReturnToDropoff(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessWorkerHarvestTile(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessWorkerApproachHarvestTile(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessWorkerDepositCargo(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessWorkerApproachDropoff(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessWorkerReservedHarvestWait(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitPatrolRouteCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitPatrolReturnLeg(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitPatrolOutboundLeg(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitPatrolReturnCombatTarget(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitPatrolOutboundCombatTarget(UnitCommandContext& context,
    UnitMovementUnit& unit);
void BeginUnitCarrierBoardingCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleCarrierImmediateBoarding(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleCarrierApproachBoardingTarget(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandlePassengerApproachCarrier(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessTransportUnloadStart(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessTransportUnloadChildren(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessTransportUnloadWait(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessTransportDockStart(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessTransportDockSearch(UnitCommandContext& context, UnitMovementUnit& unit);
void ProcessTransportTargetRelease(UnitCommandContext& context,
    UnitMovementUnit& unit, bool release_failed);
bool ProcessTransportValidateTarget(UnitCommandContext& context,
    UnitMovementUnit& unit, bool progress_ready);
void StartReservedTileWorkCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleReservedTileWorkCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleReservedTileLinkedObject(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleReservedTileApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleReservedTileWait(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleReservedTileRetryDelay(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitSpawnPlacementCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitSpawnCreateCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitSpawnPlacementWait(UnitCommandContext& context, UnitMovementUnit& unit);
void StartTargetedUnitSpawnPlacement(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleTargetedUnitSpawnCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleTargetedUnitSpawnApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void StartLinkedUnitReleaseCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleLinkedUnitReleaseCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleLinkedUnitReleaseApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitSpecialAbilityCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitSpecialAbilityTimer(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitSpecialAbilityApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitRestoreTargetCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitRestoreTargetApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitRandomRelocation(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitMorphEnterTimer(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitMorphExitTimer(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitValueTransferCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitValueTransferCycle(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitValueTransferApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitTimedFlagPhaseA(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitTimedFlagPhaseB(UnitCommandContext& context, UnitMovementUnit& unit);
void StartUnitItemSlotUseCommand(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitItemSlotUseAction(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitItemSlotUseApproach(UnitCommandContext& context, UnitMovementUnit& unit);
void HandleUnitPassiveRecoveryAndTimedRemoval(UnitCommandContext& context,
    UnitMovementUnit& unit);
void HandleUnitStatusEffectTimer(UnitCommandContext& context, UnitMovementUnit& unit);
bool CheckUnitDistanceAtLeastOneTile(const UnitMovementUnit& unit, i32 x, i32 y);
bool CheckUnitSpawnDistanceThreshold(const UnitCommandContext& context,
    const UnitMovementUnit& unit, i32 x, i32 y);
bool CheckLinkedUnitReleaseReady(UnitCommandContext& context, UnitMovementUnit& unit);
bool CheckSavedCommandPointWithinOneTile(const UnitMovementUnit& unit);
u32 CalculateOwnerTransportGroupRequiredCarrierCount(const UnitCommandContext& context,
    u32 owner_id, u32 group_id, u32 carrier_capacity);
u32 CountOwnerTransportGroupReservedCarriers(const OwnerTransportQueueState& queue,
    u32 group_id);
u32 CalculateOwnerTransportCarrierDeficit(const UnitCommandContext& context,
    const OwnerTransportQueueState& queue, u32 owner_id, u32 group_id,
    u32 carrier_capacity);
u32 CalculateOwnerTransportCarrierDeficitTotal(const UnitCommandContext& context,
    const OwnerTransportQueueState& queue, u32 owner_id, u32 carrier_capacity);
u32 FindOwnerTransportLinkedState0eSlot(const OwnerTransportQueueState& queue,
    u32 linked_group);
u32 FindOwnerTransportLinkedState0aSlot(const OwnerTransportQueueState& queue,
    u32 linked_group);
u32 FindOwnerTransportRelayCandidateWithoutLinkedState0a(
    const OwnerTransportQueueState& queue);
u32 FindOwnerTransportRelayCandidateWithoutLinkedState0e(
    const OwnerTransportQueueState& queue);
void ReassignOwnerTransportQueueSlotReferences(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, u32 from_slot, u32 to_slot);
void PrepareOwnerTransportQueueSlotAsState0b(OwnerTransportQueueState& queue,
    u32 slot_index, const UnitMovementUnit* fallback_target);
u32 SelectOwnerTransportState0aRelaySlotOrPrepareState0b(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 source_slot, const UnitMovementUnit* fallback_target = nullptr);
u32 SelectOwnerTransportState0eRelaySlotOrMerge(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 source_slot);
void ResetOwnerTransportQueueMaintenanceScratch(OwnerTransportQueueState& queue,
    OwnerTransportQueueMaintenanceScratch& scratch);
void RebuildOwnerTransportQueueActiveSlotCounts(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceCallbacks& callbacks,
    void* user_data, OwnerTransportQueueMaintenanceScratch& scratch);
void DispatchOwnerTransportQueueUnitTicks(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceCallbacks& callbacks,
    void* user_data, OwnerTransportQueueMaintenanceScratch& scratch);
OwnerTransportQueueMaintenanceScratch TickOwnerTransportQueueMaintenance(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceInput& input = {});
OwnerTransportQueueLoadSummary CalculateOwnerTransportQueueLoadSummary(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, OwnerTransportQueueUnitWeightCallback weight_callback);
u32 CalculateOwnerTransportUnitWeight(const UnitMovementUnit& unit);
OwnerTransportQueueAssignmentResult AssignOwnerTransportQueueSlotForUnit(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementUnit& unit, const OwnerTransportQueueAssignmentInput& input);
OwnerUnitCountAndWeightSummary CalculateOwnerUnitCountAndWeightSummary(
    const UnitCommandContext& context, u32 owner_id,
    OwnerUnitEligibilityCallback eligibility_callback,
    OwnerTransportQueueUnitWeightCallback weight_callback);
bool CheckOwnerEligibleRetargetUnit(const UnitMovementUnit& unit);
bool CheckOwnerEligibleRetargetUnitWithScriptGate(const UnitMovementUnit& unit,
    bool owner_script_trigger_gate_open, bool unit_in_active_script_trigger_group);
bool CheckOwnerCanTargetOwner(const OwnerStrategicTargetState& state,
    u32 owner_id);
UnitMovementUnit* FindOwnerRouteOrBuildingTargetForCurrentTargetOwner(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    OwnerStrategicUnitPredicate route_target_predicate,
    OwnerStrategicUnitPredicate building_target_predicate);
UnitMovementUnit* FindOwnerFallbackTargetForCurrentTargetOwner(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    OwnerStrategicUnitPredicate fallback_predicate);
bool SelectNearestAttackableOwnerForStrategicTarget(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    UnitMovementPoint reference_point, u32& selected_owner);
void SetOwnerStrategicPointFromUnit(OwnerStrategicTargetState& state,
    const UnitMovementUnit& target);
bool CheckOwnerStrategicPathWindowTileOpen(const UnitMovementCell& cell);
u32 CalculateOwnerStrategicPathWindowOpenScore(const UnitMovementMap& map,
    UnitMovementPoint start_tile, u32 max_score = 0x50,
    OwnerStrategicTilePredicate open_predicate = nullptr);
OwnerPathWindowSelection SelectOwnerBestOpenPathWindowPoint(
    const UnitMovementMap& map, const std::vector<UnitMovementPoint>& path_tiles,
    u32 progress_percent, u32 window_count = 10, u32 max_score = 0x50,
    OwnerStrategicTilePredicate open_predicate = nullptr);
bool CheckUnitRecordsOwnerThreatPoint(const UnitMovementUnit& unit);
void RecordOwnerThreatPointForUnit(OwnerStrategicPointList& point_list,
    const UnitMovementUnit& threat_unit,
    u32 merge_distance = kOwnerStrategicPointMergeDistance);
void RecordOwnerThreatPointIfStrategicTarget(OwnerStrategicPointList& point_list,
    const UnitMovementUnit& strategic_target_unit,
    const UnitMovementUnit& threat_unit,
    u32 merge_distance = kOwnerStrategicPointMergeDistance);
u32 ProcessOwnerThreatPointList(OwnerStrategicPointList& point_list,
    u32 owner_id, OwnerThreatPointHandler handler, void* user_data = nullptr);
u32 FindOwnerThreatResponseQueueSlotNearPoint(
    const OwnerTransportQueueState& queue, UnitMovementPoint point,
    u32 max_distance = kOwnerStrategicPointMergeDistance);
u32 PrepareOwnerThreatResponseQueueSlot(OwnerTransportQueueState& queue,
    UnitMovementPoint point);
void ClearOwnerThreatPointAndQueueSlot(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, UnitMovementPoint& point,
    u32 slot_index);
OwnerThreatPointPressureSummary CalculateOwnerThreatPointPressureSummary(
    const UnitCommandContext& context, UnitMovementPoint point,
    OwnerUnitEligibilityCallback eligibility_callback,
    OwnerTransportQueueUnitWeightCallback weight_callback,
    u32 max_distance = kOwnerStrategicPointMergeDistance);
OwnerThreatPointResponseResult HandleOwnerThreatPointResponseQueue(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementPoint& point,
    OwnerUnitEligibilityCallback hostile_pressure_eligibility = nullptr,
    OwnerUnitEligibilityCallback response_unit_eligibility = nullptr,
    OwnerTransportQueueUnitWeightCallback weight_callback = nullptr,
    u32 max_distance = kOwnerStrategicPointMergeDistance,
    u32 response_weight_multiplier = 2,
    const OwnerThreatPointCrossOwnerResponseQueues* cross_owner_response = nullptr);
void SetOwnerTransportQueueSlotTarget(OwnerTransportQueueSlot& slot,
    const OwnerTransportQueueTargetSnapshot& target);
u32 FindOrAllocateOwnerTransportQueueSlotByStateAndValue(
    OwnerTransportQueueState& queue, u32 state,
    const OwnerTransportQueueTargetSnapshot& target);
void AssignUnitToOwnerTransportQueueSlot(OwnerTransportQueueState& queue,
    UnitMovementUnit& unit, u32 slot_index);
u32 CalculateOwnerTransportQueueTargetDistanceThreshold(
    const OwnerTransportQueueSlot& slot, u32 base_tiles);
bool CheckOwnerTransportQueueUnitWithinTargetThreshold(
    const UnitMovementUnit& unit, const OwnerTransportQueueSlot& slot,
    u32 base_tiles);
bool AdvanceOwnerTransportQueueProgressNearTarget(
    const UnitMovementUnit& unit, OwnerTransportQueueSlot& slot, u32 base_tiles);
OwnerTransportQueueRetargetResult ReassignOwnerTransportUnitsInTransitRangeToTarget(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 max_units, u32 destination_state,
    const OwnerTransportQueueTargetSnapshot& target);
u32 ReassignOwnerTransportUnitsBetweenQueueSlots(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, u32 source_slot,
    u32 destination_slot, u32 max_units, bool skip_suppressed_units = false);
u32 AssignEmptyCarrierToOwnerDropoffQueueSlot(OwnerTransportQueueState& queue,
    UnitMovementUnit& carrier, const OwnerTransportQueueTargetSnapshot& target);
u32 CalculateOwnerTransportRouteDesiredSlotCount(
    const OwnerTransportRouteState& route_state, u32 route_index);
u32 CalculateOwnerTransportRouteDesiredSlotCountWithPercent(
    const OwnerTransportRouteState& route_state, u32 route_index, u32 percent);
bool CheckOwnerTransportRouteMetricTile(const UnitMovementCell& cell);
OwnerTransportRouteMetrics CalculateOwnerTransportRouteMetricsAroundTile(
    const UnitMovementMap& map, UnitMovementPoint center_tile, u32 radius = 15);
OwnerTransportRouteMetrics CalculateOwnerTransportRouteMetricsForUnit(
    const UnitMovementMap& map, const UnitMovementUnit& unit, u32 radius = 15);
u32 RebuildOwnerTransportRouteTargetsForOwnedUnits(
    const UnitCommandContext& context, OwnerTransportRouteState& route_state,
    u32 owner_id);
u32 FindAndAppendOwnerTransportRouteTargetUnit(
    const UnitCommandContext& context, OwnerTransportRouteState& route_state,
    u32 owner_id);
OwnerTransportRouteMetrics CalculateOwnerTransportRouteMetricsForOwnedTargets(
    const UnitCommandContext& context, u32 owner_id);
void RefreshOwnerTransportRouteTargetMetrics(
    const UnitCommandContext& context, OwnerTransportRouteState& route_state);
OwnerTransportRouteTargetMaintenanceResult HandleOwnerTransportRouteTargetMaintenance(
    UnitCommandContext& context, OwnerTransportQueueState& queue,
    OwnerTransportRouteState& route_state, u32 owner_id,
    u32 overflow_percent = 100, u32 production_load_percent = 100,
    u32 helper_load_percent = 50,
    u32 helper_unit_type = kInvalidOwnerTransportQueueSlot,
    const UnitMovementDefinition* helper_definition = nullptr,
    u32 helper_unit_resource_cost = 0,
    const std::array<u32, 4>* route_helper_unit_types = nullptr,
    OwnerRouteHelperPathScoreCallback path_score = nullptr,
    OwnerRouteHelperPlacementPredicate placement_predicate = nullptr,
    void* user_data = nullptr, u32 owner_profile_age = 0,
    u32 route_helper_producer_unit_type = kInvalidOwnerTransportQueueSlot,
    bool route_helper_dispatch_prerequisites_met = true,
    u32 route_helper_producer_resource_cost = 0);
OwnerRouteTargetProbe FindNearestNeutralUnitToPrimaryRouteTarget(
    const UnitCommandContext& context, const OwnerTransportRouteState& route_state,
    u32 neutral_owner_id = kOwnerNeutralRouteProbeOwnerId);
OwnerRouteHelperQueueSummary SummarizeOwnerRouteHelperQueueSlots(
    const OwnerTransportQueueState& queue, u32 helper_unit_resource_cost);
OwnerRouteHelperProductionSummary
QueueOwnerRouteHelperProductionForUnderloadedTargets(
    UnitCommandContext& context, const OwnerTransportQueueState& queue,
    const OwnerTransportRouteState& route_state, u32 owner_id,
    u32 helper_load_percent, u32 production_unit_type,
    u32 production_resource_cost);
bool CheckOwnerRouteTargetBelowHelperThreshold(
    const OwnerTransportRouteTarget& target,
    const OwnerTransportRouteMetrics& current_metrics, u32 helper_load_percent);
u32 GetOwnerRouteHelperTerrainGroup(const UnitMovementCell& cell);
u32 GetOwnerRouteHelperResourceAmount(const UnitMovementCell& cell);
bool CheckOwnerRouteHelperClusterSourceTile(const UnitMovementCell& cell);
bool AppendOwnerRouteHelperCandidateCluster(
    OwnerRouteHelperCandidateSet& candidate_set, UnitMovementPoint tile,
    u32 terrain_group, u32 resource_amount,
    UnitMovementPoint replacement_reference_tile = {-1, -1});
OwnerRouteHelperCandidateSet BuildOwnerRouteHelperCandidateClusters(
    const UnitMovementMap& map, UnitMovementPoint replacement_reference_tile = {-1, -1});
void MarkOwnerRouteHelperCandidateClustersOccupiedByUnits(
    OwnerRouteHelperCandidateSet& candidate_set, const UnitCommandContext& context,
    const std::array<u32, 4>& route_helper_unit_types);
bool CheckOwnerRouteHelperPlacementFootprint(const UnitMovementMap& map,
    UnitMovementPoint tile, const UnitMovementDefinition& helper_definition,
    u32 terrain_group);
OwnerRouteHelperPlacementSearchResult FindOwnerRouteHelperPlacementNearTile(
    const UnitMovementMap& map, UnitMovementPoint center_tile, u32 terrain_group,
    const UnitMovementDefinition& helper_definition, u32 max_spiral_steps = 0x40,
    OwnerRouteHelperPlacementPredicate predicate = nullptr, void* user_data = nullptr);
OwnerRouteHelperPlacementSearchResult SelectOwnerRouteHelperCandidateTile(
    const UnitMovementMap& map, OwnerRouteHelperCandidateSet& candidate_set,
    UnitMovementPoint origin_world, const UnitMovementDefinition& helper_definition,
    OwnerRouteHelperPathScoreCallback path_score = nullptr,
    OwnerRouteHelperPlacementPredicate placement_predicate = nullptr,
    void* user_data = nullptr);
UnitMovementUnit* FindNearestOwnerRouteHelperProducer(
    const UnitCommandContext& context, u32 owner_id, u32 producer_unit_type,
    UnitMovementPoint target_tile);
OwnerRouteHelperDispatchResult DispatchOwnerRouteHelperProducer(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 helper_unit_type, UnitMovementPoint target_tile, u32 next_route_index,
    u32 producer_unit_type = kInvalidOwnerTransportQueueSlot);
bool CheckOwnerTransportRouteTargetUnitType(u32 type_id);
u32 FindOwnerTransportRouteTargetIndex(
    const OwnerTransportRouteState& route_state, u32 match_value);
u32 AppendOwnerTransportRouteTargetIfMissing(
    OwnerTransportRouteState& route_state,
    const OwnerTransportRouteTarget& route_target);
bool PromoteOwnerTransportQueueSlotToRouteWorkTarget(
    OwnerTransportQueueState& queue, u32 slot_index, u32 route_index,
    const OwnerTransportRouteState& route_state,
    const OwnerUnitTypeCounts& owner_unit_counts, u32 aux_unit_type,
    bool route_gate_open);
OwnerTransportQueueLimitPlan BuildOwnerTransportWorkTargetLimitPlan(
    const OwnerTransportQueueState& queue,
    const OwnerTransportRouteState& route_state);
u32 CalculateOwnerTransportQueueSlotOverflow(
    const OwnerTransportQueueState& queue, u32 slot_index, u32 desired_count);
OwnerTransportQueueOverflowTarget FindOwnerTransportUnderfilledWorkTargetSlot(
    const OwnerTransportQueueState& queue,
    const OwnerTransportRouteState& route_state, u32 overflow_count,
    u32 overflow_percent);
u32 FindOwnerTransportHighestPriorityOpenRouteTarget(
    const OwnerTransportRouteState& route_state);
u32 RedistributeOwnerTransportWorkTargetOverflow(
    UnitCommandContext& context, OwnerTransportQueueState& queue,
    OwnerTransportRouteState& route_state, u32 owner_id, u32 source_slot,
    u32 desired_count, u32 overflow_percent);
u32 BoardOwnerTransportPassengersFromLinkedGroup(UnitCommandContext& context,
    const OwnerTransportQueueState& queue, UnitMovementUnit& carrier,
    u32 carrier_slot_index);
u32 FindOwnerTransportQueueSlotByStateAndValue(const OwnerTransportQueueState& queue,
    u32 state, u32 value);
u32 AllocateOwnerTransportQueueSlot(OwnerTransportQueueState& queue, u32 state);
void ReleaseOwnerTransportQueueSlotReference(OwnerTransportQueueState& queue,
    u32 slot_index);
void ReleaseUnitOwnerTransportQueueSlotReference(OwnerTransportQueueState& queue,
    const UnitMovementUnit& unit);
UnitMovementUnit* FindOwnerTransportQueueAssignedUnit(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 slot_index);
void AddOwnerProductionDemand(OwnerUnitTypeCounts& demand, u32 unit_type,
    u32 amount);
u32 CalculateOwnerResourceBudgetUnitDemand(u32 resource_budget,
    u32 build_percent, u32 cap_base, u32 unit_cost);
void AddOwnerTargetCompositionDemand(OwnerUnitTypeCounts& bonus_demand,
    const OwnerUnitTypeCounts& target_owner_counts,
    const std::array<OwnerProductionDemandRule, kOwnerUnitTypeCountSlots>& rules,
    u32 percent_bonus);
void AddOwnerPrimaryAndCarrierDemand(OwnerProductionDemandState& demand_state,
    u32 primary_unit_type, const OwnerUnitTypeCounts& target_owner_counts,
    u32 carrier_unit_type, u32 carrier_deficit, u32 existing_carrier_count);
void ApplyOwnerProductionDemandAliases(OwnerUnitTypeCounts& demand);
void RemoveOwnerProductionDemandAliases(OwnerUnitTypeCounts& demand);
u32 ResolveQueuedOwnerProductionUnitType(const UnitMovementUnit& unit);
u32 CountOwnerQueuedPrimaryProductionUnits(
    const UnitCommandContext& context, u32 owner_id, u32 unit_type,
    const std::array<u32, kOwnerUnitTypeCountSlots>& producer_unit_types);
u32 CountOwnerQueuedExtendedProductionUnits(
    const UnitCommandContext& context, u32 owner_id, u32 unit_type,
    const std::array<u32, kOwnerUnitTypeCountSlots>& producer_unit_types,
    u32 extended_type_base = kOwnerExtendedProductionUnitTypeBase);
OwnerProductionBuildActionResult SelectOwnerProductionDependencyBuildAction(
    UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts,
    const OwnerProductionDependencyRequest& request,
    UnitMovementUnit* producer_cursor = nullptr,
    u32 reserved_resource_cost = 0);
void QueueOwnerProductionLinkCommand(UnitMovementUnit& unit,
    UnitMovementUnit& target);
bool LinkOwnerProductionPairUnits(
    UnitCommandContext& context, u32 owner_id, u32 pair_unit_type);
bool LinkOwnerProductionTriadUnits(UnitCommandContext& context, u32 owner_id,
    u32 source_unit_type, u32 first_partner_type, u32 second_partner_type);
bool RunOwnerProductionSpecialPairing(
    UnitCommandContext& context, u32 owner_id, u32 demanded_unit_type);
i32 ConvertOwnerProductionWorldToTile(i32 world_coord);
i32 ConvertOwnerProductionWorldToTileSar(i32 world_coord);
bool ApplyOwnerProductionPlacementFootprintOverlay(UnitMovementMap& map,
    OwnerProductionPlacementFootprintOverlay& overlay,
    UnitMovementPoint origin_world_point, u32 width, u32 height);
void RestoreOwnerProductionPlacementFootprintOverlay(UnitMovementMap& map,
    OwnerProductionPlacementFootprintOverlay& overlay);
u32 GetOwnerProductionPlacementTerrainClass(const UnitMovementMap& map,
    UnitMovementPoint tile);
OwnerProductionPlacementGateResult CheckOwnerProductionPlacementGateCell(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    UnitMovementPoint tile, u32 terrain_class, bool route_target_type);
OwnerProductionPlacementGateResult CheckOwnerProductionPlacementCandidateGate(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint world_point);
OwnerProductionPlacementGateResult CheckOwnerProductionPlacementFootprintGateCells(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint world_point);
UnitMovementPoint CalculateOwnerProductionPlacementPathProbeTile(
    UnitMovementPoint world_point, const UnitMovementDefinition& definition);
OwnerProductionPlacementPathAvailabilityResult
CheckOwnerProductionPlacementPathProbeAvailability(
    const UnitCommandContext& context, u32 owner_id,
    const UnitMovementUnit& producer_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint origin_world_point, UnitMovementPoint owner_target_point,
    bool direct_probe_required,
    OwnerProductionPlacementPathProbeCallback path_probe = nullptr,
    void* user_data = nullptr,
    OwnerProductionPlacementNearbyProbePredicate nearby_probe_predicate = nullptr,
    const std::vector<const UnitMovementUnit*>* ignored_route_units = nullptr,
    OwnerProductionPlacementTargetRefreshCallback target_refresh = nullptr,
    void* target_refresh_user_data = nullptr,
    const OwnerTransportRouteState* route_state = nullptr);
OwnerProductionPlacementSearchResult FindOwnerProductionPlacementPointSpiral(
    UnitMovementPoint start_tile,
    OwnerProductionPlacementCandidatePredicate predicate,
    void* user_data = nullptr, u32 max_rings = 0x1e);
u32 MeasureOwnerProductionPlacementBlockedSpiralScore(
    UnitMovementPoint start_tile,
    OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data = nullptr, u32 max_spiral_sides = 0xffffffffu);
OwnerProductionPlacementClearanceResult
SelectOwnerProductionForwardClearancePoint(UnitMovementPoint start_tile,
    u32 direction, OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data = nullptr, u32 forward_steps = 0x0f,
    u32 max_spiral_sides = 0xffffffffu);
OwnerProductionPlacementAnchorSet BuildOwnerProductionPlacementAnchorSet(
    UnitMovementPoint base_tile, UnitMovementPoint source_center_tile,
    u32 direction, OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data = nullptr);
UnitMovementPoint SelectOwnerProductionPlacementAnchorPoint(
    const OwnerProductionPlacementAnchorSet& anchors, u32 placement_class,
    u32 produced_unit_count);
UnitMovementUnit* FindOwnerProductionPlacementProducer(
    const UnitCommandContext& context, u32 owner_id, u32 producer_unit_type,
    UnitMovementPoint placement_point, UnitMovementUnit* producer_cursor = nullptr,
    OwnerProductionPlacementProducerPredicate predicate = nullptr,
    void* user_data = nullptr);
OwnerProductionBuildActionResult SelectOwnerProductionPlacementBuildAction(
    const UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts,
    const OwnerProductionDependencyRequest& request,
    UnitMovementPoint placement_point,
    UnitMovementUnit* producer_cursor = nullptr,
    u32 reserved_resource_cost = 0,
    OwnerProductionPlacementProducerPredicate predicate = nullptr,
    void* user_data = nullptr);
u32 CalculateOwnerProductionAuxDependencyProducerDemand(u32 dependency_demand);
OwnerProductionBuildActionResult SelectOwnerProductionAuxDependencyBuildAction(
    UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts, u32 producer_unit_type,
    u32 resource_cost, UnitMovementUnit* producer_cursor = nullptr,
    u32 reserved_resource_cost = 0);
void ProcessOwnerProductionAuxDependencyDemand(UnitCommandContext& context,
    u32 owner_id, const OwnerUnitTypeCounts& owner_unit_counts,
    OwnerProductionDemandBuildPlanResult& result,
    const OwnerProductionDemandBuildPlanInput& input,
    const std::array<u32, kOwnerProductionAuxDependencySlotCount>&
        dependency_demand);
OwnerProductionDemandBuildPlanResult
ProcessOwnerProductionDemandAndBuildPlan(UnitCommandContext& context,
    u32 owner_id, OwnerProductionDemandState demand_state,
    const OwnerProductionDemandBuildPlanInput& input);
bool CheckOwnerProductionRouteWorkerNeedsObject(
    const UnitMovementUnit& unit, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type);
UnitMovementUnit* FindOwnerProductionRouteWorker(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type);
OwnerProductionRouteObjectCandidate*
FindNearestOwnerProductionRouteObjectCandidate(
    std::vector<OwnerProductionRouteObjectCandidate>& objects,
    const UnitMovementUnit& worker, u32 owner_id, u32 object_type);
OwnerProductionRouteObjectAssignmentResult AssignOwnerProductionRouteObject(
    UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type, u32 object_type,
    std::vector<OwnerProductionRouteObjectCandidate>& objects);
u32 CalculateOwnerPrimaryUnitDesiredCountFromTargetOwner(
    const OwnerUnitTypeCounts& target_owner_counts);
u32 FindOwnerTransportQueueSlotNeedingCarrier(const UnitCommandContext& context,
    const OwnerTransportQueueState& queue, u32 owner_id, u32 carrier_capacity);
u32 FindOwnerTransportPassengerGroupWithoutCarrierReservation(
    const OwnerTransportQueueState& queue);
u32 CalculateUnitTransportCapacity(const UnitMovementUnit& unit,
    const ProductionOrderRuntimeState* production_state = nullptr);

}
