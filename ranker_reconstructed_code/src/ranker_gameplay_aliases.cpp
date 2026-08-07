// Compatibility entry points for reconstructed gameplay and frontend code.
// Keep these wrappers thin so domain behavior remains in its owning module.

#include "ranker_unit_commands.h"
#include "ranker_unit_damage.h"
#include "ranker_directx.h"
#include "ranker_gameplay_sound.h"
#include "ranker_link_lobby.h"
#include "ranker_frontend_layout.h"
#include "ranker_map_effects.h"
#include "ranker_memo_window.h"
#include "ranker_crt_runtime.h"
#include "ranker_mfc_runtime.h"
#include "ranker_network.h"
#include "ranker_ole_datetime.h"
#include "ranker_ole_image_data.h"
#include "ranker_ole_variant.h"
#include "ranker_owner_ai.h"
#include "ranker_production_orders.h"
#include "ranker_reliable_packets.h"
#include "ranker_replay_dialogs.h"
#include "ranker_runtime_resources.h"
#include "ranker_setup_data.h"
#include "ranker_sprite_renderer.h"
#include "ranker_trc.h"
#include "ranker_unit_animation.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_movement.h"
#include "ranker_unit_spatial_index.h"
#include "ranker_win32_compat.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>

#ifdef _WIN32
#include <sys/utime.h>
#endif

#ifdef _WIN32
#ifdef DrawState
#undef DrawState
#endif
#endif

namespace ranker {

u32 CalculateApproxUnitDistanceThunk(i32 x0, i32 y0, i32 x1, i32 y1) {
    return CalculateApproxUnitDistance(x0, y0, x1, y1);
}

u32 PointDirectionLookupLowThunk(UnitMovementPoint source,
    UnitMovementPoint target, const UnitDirectionLookupTable& lookup) {
    return CalculatePointDirectionFromLookup(source, target, lookup);
}

u32 HandlePointDirection16LookupEntry(UnitMovementPoint source,
    UnitMovementPoint target, const UnitDirectionLookupTable& lookup) {
    return CalculatePointDirection16FromLookup(source, target, lookup);
}

u32 HandleMovementStepDistanceThresholdEntry(u32 step_limit,
    u32 movement_period) {
    return CalculateMovementStepDistanceThreshold(step_limit, movement_period);
}

u32 ResolveMovementDistanceStepEntry(u32 distance, u32 movement_period) {
    return CalculateMovementStepFromDistance(distance, movement_period);
}

void HandleUnitMovementInterpolationResetEntry(UnitMovementUnit& unit,
    u32 command_metadata_flags, std::optional<u32> random_animation_frame) {
    ResetUnitMovementInterpolationState(
        unit, command_metadata_flags, random_animation_frame);
}

UnitMovementCoreUpdateResult HandleUnitMovementTargetStepEntry(
    const ProductionOrderRuntimeState& production_state, UnitMovementUnit& unit,
    const UnitMovementCoreUpdateConfig& config) {
    return UpdateUnitMovementTowardPathTarget(production_state, unit, config);
}

u32 GetUnitCommandIdLow24Thunk(const UnitMovementUnit& unit) {
    return GetUnitCommandIdLow24(unit);
}

u32 ResolveUnitRuntimeStateFromCommandTableThunk(
    const UnitMovementUnit& unit, const std::vector<u32>* command_state_table) {
    return ResolveUnitRuntimeStateFromCommandTable(unit, command_state_table);
}

u32 HandleUnitCommandMetadataFlagsEntry(
    const UnitMovementUnit& unit, const std::vector<u32>* command_metadata_table) {
    return GetUnitCommandMetadataFlags(unit, command_metadata_table);
}

bool MarkUnitTargetHighBitQueueEntry(UnitMovementUnit* unit) {
    return SetUnitTargetReservationFlag(unit);
}

bool ClearUnitTargetHighBitQueueEntry(UnitMovementUnit* unit) {
    return ClearUnitTargetReservationFlag(unit);
}

bool PushDeferredUnitCommandThunk(UnitMovementUnit& unit,
    const UnitQueuedCommand& command, u32 max_deferred_count) {
    return PushDeferredUnitCommand(unit, command, max_deferred_count);
}

bool SetOrQueueUnitPointCommand01Thunk(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    return SetOrQueueUnitPointCommand01(unit, x, y, enqueue_deferred);
}

bool HandleUnitCommand00QueueEntry(UnitMovementUnit* unit,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand00(unit, enqueue_deferred);
}

bool HandleUnitTargetCommand03QueueEntry(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred) {
    return SetOrQueueUnitTargetCommand03(unit, target_unit, enqueue_deferred);
}

bool HandleUnitCommand02QueueEntry(UnitMovementUnit* unit,
    u32 fallback_command_value, UnitMovementUnit* target_unit, i32 x, i32 y,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand02(unit, fallback_command_value, target_unit, x, y,
        enqueue_deferred);
}

bool SetOrQueueUnitTargetPointCommand04Thunk(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred) {
    return SetOrQueueUnitTargetPointCommand04(
        unit, target_unit, x, y, enqueue_deferred);
}

bool SetOrQueueUnitConditionalTargetPointCommand05Thunk(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred) {
    return SetOrQueueUnitConditionalTargetPointCommand05(
        unit, target_unit, x, y, enqueue_deferred);
}

bool SetOrQueueUnitAlignedPointCommand06Thunk(UnitMovementUnit* unit,
    u32 command_value, i32 x, i32 y, bool enqueue_deferred) {
    return SetOrQueueUnitAlignedPointCommand06(
        unit, command_value, x, y, enqueue_deferred);
}

bool SetOrQueueUnitPointCommand07Thunk(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    return SetOrQueueUnitPointCommand07(unit, x, y, enqueue_deferred);
}

bool SetOrQueueUnitPointCommand09Thunk(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    return SetOrQueueUnitPointCommand09(unit, x, y, enqueue_deferred);
}

bool SetOrQueueUnitTargetCommand0aThunk(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred) {
    return SetOrQueueUnitTargetCommand0a(unit, target_unit, enqueue_deferred);
}

bool SetOrQueueUnitTargetCommand0bThunk(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred) {
    return SetOrQueueUnitTargetCommand0b(unit, target_unit, enqueue_deferred);
}

bool HandleUnitCommand10QueueEntry(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand10(unit, command_value, enqueue_deferred);
}

bool HandleUnitCommand11QueueEntry(UnitMovementUnit* unit,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand11(unit, enqueue_deferred);
}

bool HandleUnitCommand17QueueEntry(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand17(unit, command_value, enqueue_deferred);
}

bool HandleUnitCommand1bQueueEntry(UnitMovementUnit* unit,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand1b(unit, enqueue_deferred);
}

bool HandleUnitTargetReferencePointEntry(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y) {
    return SetUnitCommandTargetReferencePoint(unit, target_unit, x, y);
}

bool HandleUnitCommand21RuntimeFlagEntry(UnitMovementUnit* unit,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand21AndSetRuntimeFlag(unit, enqueue_deferred);
}

bool SetOrQueueUnitCommand22Thunk(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred) {
    return SetOrQueueUnitCommand22(unit, command_value, enqueue_deferred);
}

bool SetOrQueueUnitPointCommand24Thunk(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    return SetOrQueueUnitPointCommand24(unit, x, y, enqueue_deferred);
}

void HandleUnitExtendedCommandPayloadEntry(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, u32 state_index,
    bool enqueue_deferred) {
    WriteOrQueueUnitExtendedCommandPayload(
        unit, target_unit, x, y, state_index, enqueue_deferred);
}

void OwnerThreatPointListUpdateThunk(OwnerStrategicPointList& point_list,
    const UnitMovementUnit& threat_unit, u32 merge_distance) {
    RecordOwnerThreatPointForUnit(point_list, threat_unit, merge_distance);
}

void DamageReactionOwnerThreatPointGateThunk(
    OwnerStrategicPointList& point_list,
    const UnitMovementUnit& strategic_target_unit,
    const UnitMovementUnit& threat_unit, u32 merge_distance) {
    RecordOwnerThreatPointIfStrategicTarget(
        point_list, strategic_target_unit, threat_unit, merge_distance);
}

OwnerThreatPointResponseResult OwnerThreatPointResponseQueueThunk(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementPoint& point,
    OwnerUnitEligibilityCallback hostile_pressure_eligibility,
    OwnerUnitEligibilityCallback response_unit_eligibility,
    OwnerTransportQueueUnitWeightCallback weight_callback, u32 max_distance,
    u32 response_weight_multiplier,
    const OwnerThreatPointCrossOwnerResponseQueues* cross_owner_response) {
    return HandleOwnerThreatPointResponseQueue(context, queue, owner_id, point,
        hostile_pressure_eligibility, response_unit_eligibility, weight_callback,
        max_distance, response_weight_multiplier, cross_owner_response);
}

u32 OwnerRouteTargetTableRebuildThunk(const UnitCommandContext& context,
    OwnerTransportRouteState& route_state, u32 owner_id) {
    return RebuildOwnerTransportRouteTargetsForOwnedUnits(
        context, route_state, owner_id);
}

OwnerTransportRouteTargetMaintenanceResult
OwnerTransportRouteTargetMaintenanceThunk(UnitCommandContext& context,
    OwnerTransportQueueState& queue, OwnerTransportRouteState& route_state,
    u32 owner_id, u32 overflow_percent, u32 production_load_percent,
    u32 helper_load_percent, u32 helper_unit_type,
    const UnitMovementDefinition* helper_definition,
    u32 helper_unit_resource_cost,
    const std::array<u32, 4>* route_helper_unit_types,
    OwnerRouteHelperPathScoreCallback path_score,
    OwnerRouteHelperPlacementPredicate placement_predicate, void* user_data,
    u32 owner_profile_age, u32 route_helper_producer_unit_type,
    bool route_helper_dispatch_prerequisites_met,
    u32 route_helper_producer_resource_cost) {
    return HandleOwnerTransportRouteTargetMaintenance(context, queue, route_state,
        owner_id, overflow_percent, production_load_percent, helper_load_percent,
        helper_unit_type, helper_definition, helper_unit_resource_cost,
        route_helper_unit_types, path_score, placement_predicate, user_data,
        owner_profile_age, route_helper_producer_unit_type,
        route_helper_dispatch_prerequisites_met,
        route_helper_producer_resource_cost);
}

u32 CountOwnerQueuedPrimaryProductionUnitsThunk(
    const UnitCommandContext& context, u32 owner_id, u32 unit_type,
    const std::array<u32, kOwnerUnitTypeCountSlots>& producer_unit_types) {
    return CountOwnerQueuedPrimaryProductionUnits(
        context, owner_id, unit_type, producer_unit_types);
}

u32 CountOwnerQueuedExtendedProductionUnitsThunk(
    const UnitCommandContext& context, u32 owner_id, u32 unit_type,
    const std::array<u32, kOwnerUnitTypeCountSlots>& producer_unit_types,
    u32 extended_type_base) {
    return CountOwnerQueuedExtendedProductionUnits(
        context, owner_id, unit_type, producer_unit_types, extended_type_base);
}

OwnerProductionBuildActionResult
SelectOwnerProductionDependencyBuildActionThunk(UnitCommandContext& context,
    u32 owner_id, const OwnerUnitTypeCounts& owner_unit_counts,
    const OwnerProductionDependencyRequest& request,
    UnitMovementUnit* producer_cursor, u32 reserved_resource_cost) {
    return SelectOwnerProductionDependencyBuildAction(context, owner_id,
        owner_unit_counts, request, producer_cursor, reserved_resource_cost);
}

OwnerProductionPlacementGateResult
CheckOwnerProductionPlacementCandidateGateThunk(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint world_point) {
    return CheckOwnerProductionPlacementCandidateGate(
        context, source_unit, placement_definition, unit_type, world_point);
}

OwnerProductionPlacementGateResult
CheckOwnerProductionPlacementFootprintGateCellsThunk(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint world_point) {
    return CheckOwnerProductionPlacementFootprintGateCells(
        context, source_unit, placement_definition, unit_type, world_point);
}

u32 OwnerPlacementBlockedSpiralScoreLowThunk(UnitMovementPoint start_tile,
    OwnerProductionPlacementCandidatePredicate blocked_predicate, void* user_data,
    u32 max_spiral_sides) {
    return MeasureOwnerProductionPlacementBlockedSpiralScore(
        start_tile, blocked_predicate, user_data, max_spiral_sides);
}

OwnerProductionPlacementClearanceResult
FindOwnerPlacementForwardClearanceLowThunk(UnitMovementPoint start_tile,
    u32 direction, OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data, u32 forward_steps, u32 max_spiral_sides) {
    return SelectOwnerProductionForwardClearancePoint(start_tile, direction,
        blocked_predicate, user_data, forward_steps, max_spiral_sides);
}

OwnerProductionBuildActionResult
SelectOwnerProductionPlacementBuildActionThunk(
    const UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts,
    const OwnerProductionDependencyRequest& request,
    UnitMovementPoint placement_point, UnitMovementUnit* producer_cursor,
    u32 reserved_resource_cost,
    OwnerProductionPlacementProducerPredicate predicate, void* user_data) {
    return SelectOwnerProductionPlacementBuildAction(context, owner_id,
        owner_unit_counts, request, placement_point, producer_cursor,
        reserved_resource_cost, predicate, user_data);
}

OwnerProductionBuildActionResult
SelectOwnerProductionAuxDependencyBuildActionThunk(UnitCommandContext& context,
    u32 owner_id, const OwnerUnitTypeCounts& owner_unit_counts,
    u32 producer_unit_type, u32 resource_cost, UnitMovementUnit* producer_cursor,
    u32 reserved_resource_cost) {
    return SelectOwnerProductionAuxDependencyBuildAction(context, owner_id,
        owner_unit_counts, producer_unit_type, resource_cost, producer_cursor,
        reserved_resource_cost);
}

void ProcessOwnerProductionAuxDependencyDemandThunk(
    UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts,
    OwnerProductionDemandBuildPlanResult& result,
    const OwnerProductionDemandBuildPlanInput& input,
    const std::array<u32, kOwnerProductionAuxDependencySlotCount>&
        dependency_demand) {
    ProcessOwnerProductionAuxDependencyDemand(context, owner_id, owner_unit_counts,
        result, input, dependency_demand);
}

u32 ThunkCalculateUnitInteractionRangeWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 base_range,
    const UnitEquipmentCatalog* equipment_catalog) {
    return CalculateUnitInteractionRangeWithProductionAndEquipmentEffects(
        production_state, unit, base_range, equipment_catalog);
}

u32 HandleUnitMovementStepLimitEffectsEntry(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 base_step_limit, i32 additional_modifier,
    const UnitEquipmentCatalog* equipment_catalog) {
    return CalculateUnitMovementStepLimitWithProductionEffects(production_state, unit,
        base_step_limit, additional_modifier, equipment_catalog);
}

u32 ThunkCalculateUnitMaxHealthWithProductionEffects(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit) {
    return CalculateUnitMaxHealthWithProductionEffects(production_state, unit);
}

u32 ThunkCalculateUnitMaxSecondaryValueWithProductionEffects(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit) {
    return CalculateUnitMaxSecondaryValueWithProductionEffects(production_state, unit);
}

u32 ThunkCalculateUnitActionRecoveryReductionWithProductionEffect05(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit) {
    return CalculateUnitActionRecoveryReductionWithProductionEffect05(
        production_state, unit);
}

u32 ThunkCalculateUnitActionRecoveryTicksWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, const UnitEquipmentCatalog* equipment_catalog) {
    return CalculateUnitActionRecoveryTicksWithProductionAndEquipmentEffects(
        production_state, unit, equipment_catalog);
}

u32 ThunkCalculateUnitActionRangeWithProductionAndEquipmentEffects(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 target_render_class,
    const UnitEquipmentCatalog* equipment_catalog) {
    return CalculateUnitActionRangeWithProductionAndEquipmentEffects(
        production_state, unit, target_render_class, equipment_catalog);
}

u32 ThunkCalculateUnitActionRangeScaledBonus(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 target_render_class) {
    (void)production_state;
    (void)target_render_class;
    return CalculateUnitActionRangeScaledBonus(unit);
}

u32 ThunkCalculateOrder2bAdjustedUnitValue(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 base_value, u32 per_variant_delta,
    u32 minimum_value) {
    return CalculateOrder2bAdjustedUnitValue(
        production_state, unit, base_value, per_variant_delta, minimum_value);
}

bool ThunkCheckUnitCommandGateWithProductionEffect12(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, bool helper_allows, u32 definition_flags,
    u32 command_metadata_flags, const UnitEquipmentCatalog* equipment_catalog) {
    return CheckUnitCommandGateWithProductionEffect12(production_state, unit,
        helper_allows, definition_flags, command_metadata_flags, equipment_catalog);
}

i32 ThunkCalculateUnitEquipmentModifier22c(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentModifier22c(unit, catalog);
}

i32 ThunkCalculateUnitEquipmentModifier230(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentModifier230(unit, catalog);
}

i32 ThunkCalculateUnitEquipmentMovementFrameModifier(
    const UnitMovementUnit& unit, const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentMovementFrameModifier(unit, catalog);
}

i32 ThunkCalculateUnitEquipmentInteractionRangeModifier(
    const UnitMovementUnit& unit, const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentInteractionRangeModifier(unit, catalog);
}

i32 ThunkCalculateUnitEquipmentCommandGateModifier(
    const UnitMovementUnit& unit, const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentCommandGateModifier(unit, catalog);
}

i32 ThunkCalculateUnitEquipmentCommandFlagModifier(
    const UnitMovementUnit& unit, const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentCommandFlagModifier(unit, catalog);
}

bool ThunkCheckUnitEquipmentGenericModifierEligible(
    const UnitMovementUnit& unit, const UnitEquipmentCatalog& catalog,
    u32 effect_id) {
    const UnitEquipmentEffectDefinition* effect =
        FindUnitEquipmentEffect(catalog, effect_id);
    return effect != nullptr && IsUnitEquipmentEffectActiveForUnit(unit, *effect);
}

bool ThunkCheckUnitEquipmentTypeFilter(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect) {
    return UnitEquipmentEffectAllowsUnitType(unit, effect);
}

bool Thunk_LoadUnitEquipmentCatalogFromJw210TrcRecord2(
    UnitEquipmentCatalog& catalog, u32* version) {
    return LoadUnitEquipmentCatalogFromJw210TrcRecord2(catalog, version);
}

u32 ThunkCalculateProductionOrderCostRule(
    const ProductionOrderCostRule& rule, u32 variant) {
    return CalculateProductionOrderCost(rule, variant);
}

u32 ThunkCalculateProductionOrderDurationTicks(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderDuration(definition, variant);
}

u32 ThunkCalculateProductionOrderPrimaryCost(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCost(definition.primary_cost, variant);
}

u32 ThunkCalculateProductionOrderSecondaryCost(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCost(definition.secondary_cost, variant);
}

std::array<i32, kProductionOrderCompletionEffectCount>
ThunkApplyProductionOrderCompletionEffects(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffects(definition, variant);
}

ProductionOrderCompletionResult ThunkHandleProductionOrderProgressCompletion(
    ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner) {
    return CompleteProductionOrder(state, definition, owner);
}

ProductionOrderCompletionResult ThunkProcessProductionOrderProgressTick(
    ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner, u32& progress_ticks) {
    return AdvanceProductionOrderProgress(state, definition, owner, progress_ticks);
}

bool ThunkDebitProductionOrderPrimaryCost(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner) {
    return DebitProductionOrderPrimaryCost(state, definition, owner);
}

void ThunkRefundProductionOrderCosts(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner,
    bool secondary_uses_primary_formula) {
    RefundProductionOrderCosts(
        state, definition, owner, secondary_uses_primary_formula);
}

void ThunkClearProductionOrderLockFlags(ProductionOrderRuntimeState& state,
    u32 order_id, u32 owner) {
    ClearProductionOrderLockFlags(state, order_id, owner);
}

ProductionOrderCheckResult ThunkCheckProductionOrderAvailability(
    const ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner) {
    return CheckProductionOrderAvailability(state, definition, owner);
}

bool ThunkLoadProductionOrderCatalogFromJw210TrcRecord0(
    ProductionOrderCatalog& catalog) {
    return LoadProductionOrderCatalogFromJw210TrcRecord0(catalog);
}

i32 ThunkCalculateProductionOrderCompletionEffect00(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect00(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect01(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect01(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect02(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect02(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect03(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect03(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect04(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect04(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect05(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect05(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect06(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect06(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect07(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect07(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect08(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect08(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect09(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect09(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect10(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect10(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect11(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect11(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect12(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect12(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect13(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect13(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect14(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect14(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect15(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect15(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect16(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect16(definition, variant);
}

i32 ThunkCalculateProductionOrderCompletionEffect17(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffect17(definition, variant);
}

bool ThunkHandleOwnerSlotTransferAndStateSwap(
    ProductionOrderRuntimeState& state, u32 first_owner, u32 second_owner) {
    return SwapProductionOrderOwnerState(state, first_owner, second_owner);
}

void HandleUnitOwnerTransportQueueSlotReleaseAlias(
    OwnerTransportQueueState& queue, const UnitMovementUnit& unit) {
    ReleaseUnitOwnerTransportQueueSlotReference(queue, unit);
}

OwnerPathWindowSelection OwnerBestOpenPathWindowPointRefreshThunk(
    const UnitMovementMap& map, const std::vector<UnitMovementPoint>& path_tiles,
    u32 progress_percent, u32 window_count, u32 max_score,
    OwnerStrategicTilePredicate open_predicate) {
    return SelectOwnerBestOpenPathWindowPoint(map, path_tiles, progress_percent,
        window_count, max_score, open_predicate);
}

u32 OwnerThreatPointListDispatchThunk(OwnerStrategicPointList& point_list,
    u32 owner_id, OwnerThreatPointHandler handler, void* user_data) {
    return ProcessOwnerThreatPointList(point_list, owner_id, handler, user_data);
}

OwnerRouteTargetProbe OwnerNeutralRouteProbeRefreshThunk(
    const UnitCommandContext& context,
    const OwnerTransportRouteState& route_state, u32 neutral_owner_id) {
    return FindNearestNeutralUnitToPrimaryRouteTarget(
        context, route_state, neutral_owner_id);
}

OwnerProductionDemandBuildPlanResult OwnerProductionDemandAndBuildPlanThunk(
    UnitCommandContext& context, u32 owner_id,
    OwnerProductionDemandState demand_state,
    const OwnerProductionDemandBuildPlanInput& input) {
    return ProcessOwnerProductionDemandAndBuildPlan(
        context, owner_id, demand_state, input);
}

OwnerProductionPlacementAnchorSet OwnerPlacementAnchorRefreshLowThunk(
    UnitMovementPoint base_tile, UnitMovementPoint source_center_tile,
    u32 direction, OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data) {
    return BuildOwnerProductionPlacementAnchorSet(
        base_tile, source_center_tile, direction, blocked_predicate, user_data);
}

bool HandleUnitDeathSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleUnitDeathVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

bool HandleUnitHitSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleUnitHitReactionVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

bool HandleUnitSpawnSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleUnitSpawnCompleteVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

bool HandleUnitAttackSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleUnitAttackFrameVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

bool HandleUnitSelectionSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleSelectedUnitVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

bool HandleUnitCommandAckSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleCommandAcknowledgementVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

void InitializeAllUnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    InitializeUnitSpatialIndex(indexes.all_units);
}

void AllocateAllUnitSpatialIndexStorage(UnitSpatialIndexSet& indexes) {
    InitializeAllUnitSpatialIndex(indexes);
}

void RegisterAllUnitSpatialIndexShutdown(UnitSpatialIndexSet& indexes) {
    (void)indexes;
}

void ShutdownAllUnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    ShutdownUnitSpatialIndex(indexes.all_units);
}

void InitializeNonTerminalUnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    AllocateNonTerminalUnitSpatialIndexStorage(indexes);
}

void InitializeActiveCommandUnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    AllocateActiveCommandUnitSpatialIndexStorage(indexes);
}

void InitializeLifecycleClass3UnitSpatialIndex(UnitSpatialIndexSet& indexes) {
    AllocateLifecycleClass3UnitSpatialIndexStorage(indexes);
}

void InitializeUnitSpatialIndexThunk(UnitSpatialIndex& index, u32 capacity) {
    InitializeUnitSpatialIndex(index, capacity);
}

void ShutdownUnitSpatialIndexThunk(UnitSpatialIndex& index) {
    ShutdownUnitSpatialIndex(index);
}

void RebuildUnitSpatialIndexThunk(UnitSpatialIndex& index,
    const std::vector<UnitMovementUnit*>& active_units,
    UnitSpatialIndexBuildMode mode,
    const std::vector<u32>* command_state_categories) {
    RebuildUnitSpatialIndex(index, active_units, mode, command_state_categories);
}

UnitMovementUnit* QueryUnitSpatialIndexRadiusThunk(UnitSpatialIndex& index,
    UnitMovementUnit* source_unit, i32 x, i32 y, u32 radius) {
    return QueryUnitSpatialIndexRadius(index, source_unit, x, y, radius);
}

UnitMovementUnit* QueryUnitSpatialIndexRelativeBoxThunk(UnitSpatialIndex& index,
    UnitMovementUnit* source_unit, i32 left, i32 right, i32 top, i32 bottom) {
    return QueryUnitSpatialIndexRelativeBox(
        index, source_unit, left, right, top, bottom);
}

UnitMovementUnit* NextUnitSpatialIndexQueryResultThunk(UnitSpatialIndex& index) {
    return NextUnitSpatialIndexQueryResult(index);
}

void SortUnitSpatialIndexEntriesByXThunk(
    std::vector<UnitSpatialIndexEntry>& entries) {
    SortUnitSpatialIndexEntriesByX(entries);
}

UnitMovementPoint GetUnitMovementDirection16DeltaPtr(u32 direction) {
    return GetUnitMovementDirection16Delta(direction);
}

UnitMovementPoint GetUnitMovementDirection8DeltaPtr(u32 direction) {
    return GetUnitMovementDirection8Delta(direction);
}

UnitMovementPoint HandleDirection16DeltaLookupEntry(u32 direction) {
    return GetUnitMovementDirection16Delta(direction);
}

UnitMovementPoint HandleDirection8DeltaLookupEntry(u32 direction) {
    return GetUnitMovementDirection8Delta(direction);
}

bool InitializeJw207DirectionLookupTables(UnitDirectionLookupTable& record0_lookup,
    UnitDirectionLookupTable& record1_lookup, const char* archive_name) {
    return LoadJw207DirectionLookupRecords(
        record0_lookup, record1_lookup, archive_name);
}

bool HandleJw207DirectionLookupInitializerEntry(
    UnitDirectionLookupTable& record0_lookup,
    UnitDirectionLookupTable& record1_lookup, const char* archive_name) {
    return LoadJw207DirectionLookupRecords(
        record0_lookup, record1_lookup, archive_name);
}

UnitMovementPoint CalculateUnitMovementFrameDeltaWithProductionEffectsLowThunk(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, i32 additional_modifier,
    const UnitEquipmentCatalog* equipment_catalog) {
    return CalculateUnitMovementFrameDeltaWithProductionEffects(
        production_state, unit, additional_modifier, equipment_catalog);
}

u32 CalculateUnitMovementStepLimitWithEffects(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 base_step_limit, i32 additional_modifier,
    const UnitEquipmentCatalog* equipment_catalog) {
    return CalculateUnitMovementStepLimitWithProductionEffects(production_state, unit,
        base_step_limit, additional_modifier, equipment_catalog);
}

u32 CalculateUnitActionRangeScaledBonus(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit, u32 target_render_class) {
    (void)production_state;
    (void)target_render_class;
    return CalculateUnitActionRangeScaledBonus(unit);
}

void HandleLowThunkUnitStringSlotClear(UnitMovementContext& context,
    u32 slot_index) {
    ClearUnitStringSlotIfUnused(context, slot_index);
}

u32 HandleLowThunkUnitStringIntern(UnitMovementContext& context,
    const char* text) {
    return InternUnitStringSlot(context, text);
}

#ifdef _WIN32
bool HandleSetupDataWriteMirror() {
    return WriteDefaultSetupDataBuffer();
}

bool HandleSetupDataReadMirror() {
    return LoadDefaultSetupDataBuffer();
}

bool HandleBinkFrameSurfaceMirror() {
    return ConfigureBinkFrameSurface();
}

void HandleRecord3VideoTransitionMirror(HWND window) {
    HandleJw208Record3VideoTransition(window);
}

void HandleIntroVideoSequenceMirror(HWND window) {
    HandleJw208IntroVideoSequence(window);
}
#endif

bool HandleSessionBundleImportMirror(const char* archive_name,
    u32 base_record_index) {
    return HandleGameplaySessionBundleImport(archive_name, base_record_index);
}

bool HandleSessionBundleExportMirror(const char* archive_name,
    const std::vector<TrcWriteRecord>& records, u16 requested_method,
    u32 directory_growth) {
    return HandleGameplaySessionBundleExport(
        archive_name, records, requested_method, directory_growth);
}

u32 HandleGameplaySoundVariantMirror(GameplaySoundState& state,
    u32 variant_count) {
    return SelectGameplaySoundVariant(state, variant_count);
}

void HandleMirrorGameplaySoundPlaybackLoop(GameplaySoundState& state) {
    HandleQueuedGameplaySoundPlayback(state);
}

bool HandleMirrorGameplaySoundQueueByPosition(GameplaySoundState& state,
    u32 slot_index, i32 world_delta, i32 pan) {
    return HandlePositionalGameplaySoundQueueRequest(
        state, slot_index, world_delta, pan);
}

bool HandleIndexedTwoVariantSoundMirror(GameplaySoundState& state,
    u32 base_index, i32 world_delta) {
    return HandleIndexedTwoVariantGameplaySoundCue(state, base_index, world_delta);
}

bool HandleLobbyMessageSoundMirror(GameplaySoundState& state,
    u32 slot_index, i32 world_delta) {
    return HandleImmediateLobbyMessageGameplaySound(
        state, slot_index, world_delta);
}

bool HandleLobbyTimerSoundMirror(GameplaySoundState& state,
    u32 slot_index, i32 world_delta) {
    return HandleImmediateLobbyTimerGameplaySound(state, slot_index, world_delta);
}

bool HandleProductionCompleteSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleUnitProductionCompleteVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

bool HandleSilentDeathSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit) {
    return HandleSilentDeathStateSoundCue(state, unit);
}

bool HandleHarvestFrameSoundMirror(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    return HandleWorkerHarvestFrameVoiceCue(
        state, unit, definition, base_slots, world_delta, pan);
}

void HandleVisibleMapEffectQueueMirror(MapEffectContext& context) {
    HandleVisibleMapEffectSpriteQueue(context);
}

MapEffectInstance* HandleMapEffectAllocationMirror(MapEffectContext& context) {
    return AllocateMapEffect(context);
}

void HandleEffectFreeListMirror(MapEffectContext& context,
    MapEffectInstance& effect) {
    ReleaseMapEffect(context, effect);
}

MapEffectInstance* HandleMapEffectSpawnMirror(MapEffectContext& context,
    u32 effect_id, i32 x, i32 y, UnitMovementUnit* linked_unit) {
    return HandleMapEffectNearestTileSpawn(
        context, effect_id, x, y, linked_unit);
}

MapEffectInstance* HandleNearbyMapEffectSearchMirror(MapEffectContext& context,
    const UnitMovementUnit& unit, u32 max_distance) {
    return FindNearbyInteractableMapEffectForUnit(context, unit, max_distance);
}

bool HandleCompletionTerrainEffectMirror(MapEffectContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 period) {
    return HandleCompletionTerrainEffectSpawnTick(
        context, unit, effect_id, period);
}

void HandleMapEffectTimerMirror(MapEffectContext& context) {
    HandleMapEffectTimerTick(context);
}

bool CheckOwnerProductionCostMirror(const UnitCommandContext& context,
    u32 owner_id, u32 primary_cost, u32 secondary_cost) {
    return CheckOwnerHasUnitProductionCosts(
        context, owner_id, primary_cost, secondary_cost);
}

bool HandleProductionCostDebitMirror(UnitCommandContext& context, u32 owner_id,
    u32 primary_cost, u32 secondary_cost) {
    if (!CheckOwnerHasUnitProductionCosts(
            context, owner_id, primary_cost, secondary_cost)) {
        return false;
    }
    HandleOwnerUnitProductionCostDebit(
        context, owner_id, primary_cost, secondary_cost);
    return true;
}

void HandleProductionCostRefundMirror(UnitCommandContext& context, u32 owner_id,
    u32 primary_cost, u32 secondary_cost) {
    HandleOwnerUnitProductionCostRefund(
        context, owner_id, primary_cost, secondary_cost);
}

u32 HandleOwnerTransportRequiredCarrierCountAlias(
    const UnitCommandContext& context, u32 owner_id, u32 group_id,
    u32 carrier_capacity) {
    return CalculateOwnerTransportGroupRequiredCarrierCount(
        context, owner_id, group_id, carrier_capacity);
}

u32 HandleOwnerTransportReservedCarrierCountAlias(
    const OwnerTransportQueueState& queue, u32 group_id) {
    return CountOwnerTransportGroupReservedCarriers(queue, group_id);
}

u32 HandleOwnerTransportCarrierDeficitTotalAlias(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 carrier_capacity) {
    return CalculateOwnerTransportCarrierDeficitTotal(
        context, queue, owner_id, carrier_capacity);
}

u32 HandleOwnerTransportLinkedState0eSlotAlias(
    const OwnerTransportQueueState& queue, u32 linked_group) {
    return FindOwnerTransportLinkedState0eSlot(queue, linked_group);
}

u32 HandleOwnerTransportLinkedState0aSlotAlias(
    const OwnerTransportQueueState& queue, u32 linked_group) {
    return FindOwnerTransportLinkedState0aSlot(queue, linked_group);
}

u32 HandleOwnerTransportState0aRelaySlotAlias(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 source_slot, const UnitMovementUnit* fallback_target) {
    return SelectOwnerTransportState0aRelaySlotOrPrepareState0b(
        context, queue, owner_id, source_slot, fallback_target);
}

u32 HandleOwnerTransportState0eRelaySlotAlias(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 source_slot) {
    return SelectOwnerTransportState0eRelaySlotOrMerge(
        context, queue, owner_id, source_slot);
}

u32 HandleOwnerTransportQueueSlotByStateAndValueAlias(
    const OwnerTransportQueueState& queue, u32 state, u32 value) {
    return FindOwnerTransportQueueSlotByStateAndValue(queue, state, value);
}

u32 HandleOwnerTransportQueueSlotAllocateAlias(
    OwnerTransportQueueState& queue, u32 state) {
    return AllocateOwnerTransportQueueSlot(queue, state);
}

UnitMovementUnit* HandleOwnerTransportQueueAssignedUnitAlias(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 slot_index) {
    return FindOwnerTransportQueueAssignedUnit(
        context, queue, owner_id, slot_index);
}

u32 HandleOwnerTransportPassengerGroupWithoutCarrierReservationAlias(
    const OwnerTransportQueueState& queue) {
    return FindOwnerTransportPassengerGroupWithoutCarrierReservation(queue);
}

u32 HandleOwnerTransportSlotNeedsCarrierAlias(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 carrier_capacity) {
    return FindOwnerTransportQueueSlotNeedingCarrier(
        context, queue, owner_id, carrier_capacity);
}

OwnerTransportQueueAssignmentResult HandleOwnerTransportQueueSlotAssignmentAlias(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementUnit& unit, const OwnerTransportQueueAssignmentInput& input) {
    return AssignOwnerTransportQueueSlotForUnit(
        context, queue, owner_id, unit, input);
}

OwnerTransportQueueLoadSummary HandleOwnerTransportQueueLoadSummaryAlias(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, OwnerTransportQueueUnitWeightCallback weight_callback) {
    return CalculateOwnerTransportQueueLoadSummary(
        context, queue, owner_id, weight_callback);
}

void HandleOwnerTransportQueueUnitTickThunk(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceCallbacks& callbacks, void* user_data,
    OwnerTransportQueueMaintenanceScratch& scratch) {
    DispatchOwnerTransportQueueUnitTicks(
        context, queue, owner_id, callbacks, user_data, scratch);
}

OwnerTransportRouteMetrics HandleOwnerRouteMetricsForUnitThunk(
    const UnitMovementMap& map, const UnitMovementUnit& unit, u32 radius) {
    return CalculateOwnerTransportRouteMetricsForUnit(map, unit, radius);
}

OwnerTransportRouteMetrics HandleOwnerRouteMetricsAroundTileThunk(
    const UnitMovementMap& map, UnitMovementPoint center_tile, u32 radius) {
    return CalculateOwnerTransportRouteMetricsAroundTile(map, center_tile, radius);
}

u32 HandleOwnerTransportRouteTargetAppendThunk(
    const UnitCommandContext& context, OwnerTransportRouteState& route_state,
    u32 owner_id) {
    return FindAndAppendOwnerTransportRouteTargetUnit(
        context, route_state, owner_id);
}

OwnerUnitCountAndWeightSummary HandleOwnerEligibleUnitCountWeightThunk(
    const UnitCommandContext& context, u32 owner_id,
    OwnerUnitEligibilityCallback eligibility_callback,
    OwnerTransportQueueUnitWeightCallback weight_callback) {
    return CalculateOwnerUnitCountAndWeightSummary(
        context, owner_id, eligibility_callback, weight_callback);
}

bool CheckUnitEquipmentTypeFilter(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect) {
    return UnitEquipmentEffectAllowsUnitType(unit, effect);
}

bool CheckUnitEquipmentGenericModifierEligible(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog, u32 effect_id) {
    return ThunkCheckUnitEquipmentGenericModifierEligible(unit, catalog, effect_id);
}

bool HandleOwnerSlotTransferAndStateSwap(
    ProductionOrderRuntimeState& state, u32 first_owner, u32 second_owner) {
    return SwapProductionOrderOwnerState(state, first_owner, second_owner);
}

bool HandleMode1ConsumedPacketAck(u32 target_player) {
    return SendMode1GapAck(target_player) >= 0;
}

ProductionOrderCompletionResult ProcessProductionOrderProgressTick(
    ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner, u32& progress_ticks) {
    return AdvanceProductionOrderProgress(state, definition, owner, progress_ticks);
}

ProductionOrderCompletionResult HandleProductionOrderProgressCompletion(
    ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner) {
    return CompleteProductionOrder(state, definition, owner);
}

std::array<i32, kProductionOrderCompletionEffectCount>
ApplyProductionOrderCompletionEffects(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffects(definition, variant);
}

u32 CalculateProductionOrderDurationTicks(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderDuration(definition, variant);
}

u32 CalculateProductionOrderPrimaryCost(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCost(definition.primary_cost, variant);
}

u32 CalculateProductionOrderAuxiliaryCost(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCost(definition.auxiliary_cost, variant);
}

u32 CalculateProductionOrderSecondaryCost(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCost(definition.secondary_cost, variant);
}

u32 CalculateProductionOrderCostRule(
    const ProductionOrderCostRule& rule, u32 variant) {
    return CalculateProductionOrderCost(rule, variant);
}

OwnerUnitCountAndWeightSummary CalculateOwnerEligibleUnitCountAndWeightSummary(
    const UnitCommandContext& context, u32 owner_id,
    OwnerUnitEligibilityCallback eligibility_callback,
    OwnerTransportQueueUnitWeightCallback weight_callback) {
    return CalculateOwnerUnitCountAndWeightSummary(
        context, owner_id, eligibility_callback, weight_callback);
}

OwnerRouteTargetProbe HandleOwnerNearestNeutralRouteProbeRefresh(
    const UnitCommandContext& context,
    const OwnerTransportRouteState& route_state, u32 neutral_owner_id) {
    return FindNearestNeutralUnitToPrimaryRouteTarget(
        context, route_state, neutral_owner_id);
}

u32 HandleOwnerThreatPointListDispatch(OwnerStrategicPointList& point_list,
    u32 owner_id, OwnerThreatPointHandler handler, void* user_data) {
    return ProcessOwnerThreatPointList(point_list, owner_id, handler, user_data);
}

OwnerPathWindowSelection HandleOwnerBestOpenPathWindowPointRefresh(
    const UnitMovementMap& map, const std::vector<UnitMovementPoint>& path_tiles,
    u32 progress_percent, u32 window_count, u32 max_score,
    OwnerStrategicTilePredicate open_predicate) {
    return SelectOwnerBestOpenPathWindowPoint(map, path_tiles, progress_percent,
        window_count, max_score, open_predicate);
}

OwnerProductionDemandBuildPlanResult HandleOwnerProductionDemandAndBuildPlan(
    UnitCommandContext& context, u32 owner_id,
    OwnerProductionDemandState demand_state,
    const OwnerProductionDemandBuildPlanInput& input) {
    return ProcessOwnerProductionDemandAndBuildPlan(
        context, owner_id, demand_state, input);
}

OwnerProductionPlacementAnchorSet RefreshOwnerProductionPlacementAnchors(
    UnitMovementPoint base_tile, UnitMovementPoint source_center_tile,
    u32 direction, OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data) {
    return BuildOwnerProductionPlacementAnchorSet(
        base_tile, source_center_tile, direction, blocked_predicate, user_data);
}

u32 HandleOwnerPrimaryUnitDesiredCountAlias(
    const OwnerUnitTypeCounts& target_owner_counts) {
    return CalculateOwnerPrimaryUnitDesiredCountFromTargetOwner(
        target_owner_counts);
}

OwnerProductionRouteObjectAssignmentResult
AssignOwnerProductionRouteObjectTarget(
    UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type, u32 object_type,
    std::vector<OwnerProductionRouteObjectCandidate>& objects) {
    return AssignOwnerProductionRouteObject(
        context, queue, owner_id, required_unit_type, object_type, objects);
}

OwnerProductionRouteObjectAssignmentResult
AssignOwnerProductionRouteObjectTargetThunk(
    UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type, u32 object_type,
    std::vector<OwnerProductionRouteObjectCandidate>& objects) {
    return AssignOwnerProductionRouteObjectTarget(
        context, queue, owner_id, required_unit_type, object_type, objects);
}

#ifdef _WIN32
void InitializeLinkLobbyHostResourceComboSupport(LinkLobbyState& state) {
    InitializeLinkLobbyHostResourceComboControl(state);
    RegisterLinkLobbyHostResourceComboShutdown(state);
}

void InitializeLinkLobbyAvatarButtonArray(LinkLobbyState& state) {
    InitializeLinkLobbyAvatarButtonArraySupport(state);
    RegisterLinkLobbyAvatarButtonArrayShutdown(state);
}

void InitializeLinkLobbyTabButton0(LinkLobbyState& state) {
    InitializeLinkLobbyTabButtonControl0(state);
    RegisterLinkLobbyTabButtonDestructor0(state);
}

void DestroyLinkLobbyTabButtonControl3(LinkLobbyState& state) {
    DestroyLegacyImageButtonControl(state.tab_buttons[3]);
}

void InitializeLinkLobbyLatencyButton0(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyButtonControl0(state);
    RegisterLinkLobbyLatencyButtonDestructor0(state);
}

void DestroyLinkLobbyLatencyButtonControl7(LinkLobbyState& state) {
    DestroyLegacyImageButtonControl(state.latency_buttons[7]);
}

void InitializeLinkLobbyLatencyBitmap0(LinkLobbyState& state) {
    InitializeLinkLobbyLatencyBitmapResource0(state);
    RegisterLinkLobbyLatencyBitmapDestructor0(state);
}

void DestroyLinkLobbyLatencyBitmapResource5(LinkLobbyState& state) {
    ReleaseBitmapMemoryResource(state.latency_bitmaps[5]);
}

void InitializeLinkLobbyMapDownloadButton0(LinkLobbyState& state) {
    InitializeLinkLobbyMapDownloadButtonControl0(state);
    RegisterLinkLobbyMapDownloadButtonDestructor0(state);
}

void DestroyLinkLobbyMapDownloadButtonControl7(LinkLobbyState& state) {
    DestroyLegacyImageButtonControl(state.map_download_buttons[7]);
}

void InitializeLinkLobbyPlayerRoleComboStatic0(LinkLobbyState& state) {
    InitializeLinkLobbyPlayerRoleComboControl0(state);
    RegisterLinkLobbyPlayerRoleComboDestructor0(state);
}

void DestroyLinkLobbyPlayerRoleComboControl7(LinkLobbyState& state) {
    DestroyLegacyImageComboBoxControl(state.player_role_combos[7]);
}

void InitializeLinkLobbyTribeComboStatic0(LinkLobbyState& state) {
    InitializeLinkLobbyTribeComboControl0(state);
    RegisterLinkLobbyTribeComboDestructor0(state);
}

void DestroyLinkLobbyTribeComboControl7(LinkLobbyState& state) {
    DestroyLegacyImageComboBoxControl(state.tribe_combos[7]);
}

void IgnoreLinkLobbyReservedPacket0x14(LinkLobbyState&, const void*,
    std::size_t) {
}

void IgnoreLinkLobbyReservedPacket0x05(LinkLobbyState&, const void*,
    std::size_t) {
}

void IgnoreLinkLobbyReservedPacket0x06(LinkLobbyState&, const void*,
    std::size_t) {
}

void IgnoreLinkLobbyReservedPacket0x19(LinkLobbyState&, const void*,
    std::size_t) {
}

void IgnoreLinkLobbyReservedPacket0x1a(LinkLobbyState&, const void*,
    std::size_t) {
}

void IgnoreLinkLobbyReservedPacket0x1b(LinkLobbyState&, const void*,
    std::size_t) {
}

void IgnoreLinkLobbyReservedPacket0x1c(LinkLobbyState&, const void*,
    std::size_t) {
}

void InitializeReplayLoadBackgroundStatic(ReplayDialogState& state) {
    InitializeReplayLoadBackground(state);
    RegisterReplayLoadBackgroundDestructor(state);
}

void InstallReplayLoadAccelerators(ReplayDialogState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators =
        LoadAcceleratorsA(state.instance,
            MAKEINTRESOURCEA(kReplayLoadAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreReplayLoadAccelerators(ReplayDialogState& state) {
    if (RankerMainWindowState().active_accelerator_window != state.window) {
        return;
    }
    SetActiveAcceleratorState(nullptr, state.active_accelerators);
    DestroyAcceleratorTable(state.active_accelerators);
    state.active_accelerators = state.saved_accelerators;
    state.active_accelerator_window = state.saved_accelerator_window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void ClearReplayLoadListData(ReplayDialogState& state) {
    state.entries.clear();
    state.selected_replay = ReplayArchiveDescriptor{};
}

const char* ExtractReplayFileExtension(const char* path) {
    if (path == nullptr) {
        return "";
    }
    const char* last_dot = std::strrchr(path, '.');
    const char* last_slash = std::max(std::strrchr(path, '\\'),
        std::strrchr(path, '/'));
    return last_dot != nullptr && (last_slash == nullptr || last_slash < last_dot)
        ? last_dot
        : "";
}

bool BrowseReplayLoadSelectedDirectory(ReplayDialogState& state) {
    return BrowseReplayDialogSelectedDirectory(state);
}

void InstallReplaySaveAccelerators(ReplayDialogState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    state.saved_accelerators = active.active_accelerators;
    state.saved_accelerator_window = active.active_accelerator_window;
    state.active_accelerators =
        LoadAcceleratorsA(state.instance,
            MAKEINTRESOURCEA(kReplaySaveAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void RestoreReplaySaveAccelerators(ReplayDialogState& state) {
    RestoreReplayLoadAccelerators(state);
}

void ClearReplaySaveListData(ReplayDialogState& state) {
    state.entries.clear();
    state.selected_replay = ReplayArchiveDescriptor{};
}

bool HasReplayFileExtension(const char* path) {
    const char* extension = ExtractReplayFileExtension(path);
    return _stricmp(extension, ".ply") == 0;
}

bool BrowseReplaySaveSelectedDirectory(ReplayDialogState& state) {
    return BrowseReplayDialogSelectedDirectory(state);
}
#endif

void InitializeFrontendLayoutRectTable(FrontendLayoutRectTable& table) {
    table = FrontendLayoutRectTable{};
}

void ReleaseFrontendLayoutRectTableStatic(FrontendLayoutRectTable& table) {
    ReleaseFrontendLayoutRectTable(table);
}

FrontendLayoutRect GetFrontendLayoutRect(const FrontendLayoutRectTable& table,
    std::size_t index) {
    return index < table.count && table.rects != nullptr ?
        table.rects[index] : FrontendLayoutRect{};
}

void HandleLowThunkSpritePixelMaskConstants(bool pixel_mode_555) {
    ConfigureSpritePixelMaskConstants(pixel_mode_555);
}

void HandleStartupUnitRampInitThunk(UnitAnimationDrawContext& context) {
    InitializeUnitRenderColorRamps(context);
}

bool HandleTargetFootprintCheckMirror(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    return CheckCurrentTargetFootprintSeparated(context, unit);
}

bool HandlePathTargetAxisCheckMirror(const UnitMovementUnit& unit) {
    return CheckPathTargetWithinAxisTile(unit);
}

void HandleOwnerStrategyPointRefreshThunk(OwnerStrategicTargetState& state,
    const UnitMovementUnit& target) {
    SetOwnerStrategicPointFromUnit(state, target);
}

bool HandleOwnerTargetOwnerReselectThunk(const UnitCommandContext& context,
    const OwnerStrategicTargetState& state, UnitMovementPoint reference_point,
    u32& selected_owner) {
    return SelectNearestAttackableOwnerForStrategicTarget(
        context, state, reference_point, selected_owner);
}

UnitMovementUnit* HandleOwnerFallbackTargetLookupThunk(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    OwnerStrategicUnitPredicate fallback_predicate) {
    return FindOwnerFallbackTargetForCurrentTargetOwner(
        context, state, fallback_predicate);
}

UnitMovementUnit* HandleOwnerPreferredTargetLookupThunk(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    OwnerStrategicUnitPredicate route_target_predicate,
    OwnerStrategicUnitPredicate building_target_predicate) {
    return FindOwnerRouteOrBuildingTargetForCurrentTargetOwner(
        context, state, route_target_predicate, building_target_predicate);
}

u32 HandleOwnerPathWindowOpenScoreThunk(const UnitMovementMap& map,
    UnitMovementPoint start_tile, u32 max_score,
    OwnerStrategicTilePredicate open_predicate) {
    return CalculateOwnerStrategicPathWindowOpenScore(
        map, start_tile, max_score, open_predicate);
}

OwnerProductionPlacementPathAvailabilityResult
CheckOwnerProductionPlacementPathAvailability(
    const UnitCommandContext& context, u32 owner_id,
    const UnitMovementUnit& producer_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint origin_world_point, UnitMovementPoint owner_target_point,
    bool direct_probe_required,
    OwnerProductionPlacementPathProbeCallback path_probe, void* user_data,
    OwnerProductionPlacementNearbyProbePredicate nearby_probe_predicate,
    const std::vector<const UnitMovementUnit*>* ignored_route_units,
    OwnerProductionPlacementTargetRefreshCallback target_refresh,
    void* target_refresh_user_data,
    const OwnerTransportRouteState* route_state) {
    return CheckOwnerProductionPlacementPathProbeAvailability(context, owner_id,
        producer_unit, placement_definition, unit_type, origin_world_point,
        owner_target_point, direct_probe_required, path_probe, user_data,
        nearby_probe_predicate, ignored_route_units, target_refresh,
        target_refresh_user_data, route_state);
}

OwnerProductionPlacementPathAvailabilityResult
CheckOwnerProductionPlacementPathAvailabilityThunk(
    const UnitCommandContext& context, u32 owner_id,
    const UnitMovementUnit& producer_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint origin_world_point, UnitMovementPoint owner_target_point,
    bool direct_probe_required,
    OwnerProductionPlacementPathProbeCallback path_probe, void* user_data,
    OwnerProductionPlacementNearbyProbePredicate nearby_probe_predicate,
    const std::vector<const UnitMovementUnit*>* ignored_route_units,
    OwnerProductionPlacementTargetRefreshCallback target_refresh,
    void* target_refresh_user_data,
    const OwnerTransportRouteState* route_state) {
    return CheckOwnerProductionPlacementPathAvailability(context, owner_id,
        producer_unit, placement_definition, unit_type, origin_world_point,
        owner_target_point, direct_probe_required, path_probe, user_data,
        nearby_probe_predicate, ignored_route_units, target_refresh,
        target_refresh_user_data, route_state);
}

OwnerProductionPlacementSearchResult FindOwnerProductionPlacementPoint(
    UnitMovementPoint start_tile,
    OwnerProductionPlacementCandidatePredicate predicate, void* user_data,
    u32 max_rings) {
    return FindOwnerProductionPlacementPointSpiral(
        start_tile, predicate, user_data, max_rings);
}

OwnerProductionPlacementSearchResult FindOwnerProductionPlacementPointThunk(
    UnitMovementPoint start_tile,
    OwnerProductionPlacementCandidatePredicate predicate, void* user_data,
    u32 max_rings) {
    return FindOwnerProductionPlacementPoint(
        start_tile, predicate, user_data, max_rings);
}

OwnerRouteHelperDispatchResult HandleOwnerRouteHelperDispatch(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 helper_unit_type, UnitMovementPoint target_tile, u32 next_route_index,
    u32 producer_unit_type) {
    return DispatchOwnerRouteHelperProducer(context, queue, owner_id,
        helper_unit_type, target_tile, next_route_index, producer_unit_type);
}

OwnerRouteHelperDispatchResult RunOwnerRouteHelperDispatchLowThunk(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 helper_unit_type, UnitMovementPoint target_tile, u32 next_route_index,
    u32 producer_unit_type) {
    return HandleOwnerRouteHelperDispatch(context, queue, owner_id,
        helper_unit_type, target_tile, next_route_index, producer_unit_type);
}

OwnerTransportQueueRetargetResult ReassignOwnerTransportUnitsToStrategicTargetQueue(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 max_units, u32 destination_state,
    const OwnerTransportQueueTargetSnapshot& target) {
    return ReassignOwnerTransportUnitsInTransitRangeToTarget(
        context, queue, owner_id, max_units, destination_state, target);
}

OwnerTransportQueueRetargetResult
HandleOwnerStrategicTargetQueueRetargetThunk(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, u32 max_units,
    u32 destination_state, const OwnerTransportQueueTargetSnapshot& target) {
    return ReassignOwnerTransportUnitsToStrategicTargetQueue(
        context, queue, owner_id, max_units, destination_state, target);
}

void HandleOwnerTransportQueueUnitTickDispatcher(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceCallbacks& callbacks, void* user_data,
    OwnerTransportQueueMaintenanceScratch& scratch) {
    DispatchOwnerTransportQueueUnitTicks(
        context, queue, owner_id, callbacks, user_data, scratch);
}

u32 HandleOwnerTransportWorkTargetOverflowThunk(UnitCommandContext& context,
    OwnerTransportQueueState& queue, OwnerTransportRouteState& route_state,
    u32 owner_id, u32 source_slot, u32 desired_count, u32 overflow_percent) {
    return RedistributeOwnerTransportWorkTargetOverflow(context, queue,
        route_state, owner_id, source_slot, desired_count, overflow_percent);
}

void HandleRandomAmbientMapEffectSpawnTick(MapEffectContext& context,
    UnitMovementUnit& unit) {
    SpawnUnitPassiveMapEffects(context, unit);
}

void thunk_HandleRandomAmbientMapEffectSpawnTick(MapEffectContext& context,
    UnitMovementUnit& unit) {
    HandleRandomAmbientMapEffectSpawnTick(context, unit);
}

u32 CalculateSelectedUnitHitPointBarFillWithProductionEffect00(
    const ProductionOrderRuntimeState& production_state,
    const UnitMovementUnit& unit) {
    return CalculateUnitHitPointBarFillWithProductionEffect00(
        production_state, unit);
}

}  // namespace ranker
