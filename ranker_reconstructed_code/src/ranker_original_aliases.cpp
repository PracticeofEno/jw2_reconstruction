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

extern "C" {
#include "zutil.h"
#include "deflate.h"
#include "inftrees.h"
#include "infblock.h"
#include "infcodes.h"
#include "inffast.h"
extern int inflate_flush OF((inflate_blocks_statef *, z_streamp, int));
}
#ifdef local
#undef local
#endif

struct static_tree_desc_s {
    const ct_data* static_tree;
    const intf* extra_bits;
    int extra_base;
    int elems;
    int max_length;
};

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

const char* GetZlibVersion113() {
    return "1.1.3";
}

const char* GetZlibErrorMessage113(int code) {
    switch (code) {
    case 0:
        return "";
    case 1:
        return "stream end";
    case 2:
        return "need dictionary";
    case -1:
        return "file error";
    case -2:
        return "stream error";
    case -3:
        return "data error";
    case -4:
        return "insufficient memory";
    case -5:
        return "buffer error";
    case -6:
        return "incompatible version";
    default:
        return "unknown zlib error";
    }
}

void* AllocateZlibMemoryWithCalloc113(void*, u32 items, u32 item_size) {
    return std::calloc(items, item_size);
}

void FreeZlibMemory113(void*, void* address) {
    std::free(address);
}

u32 ZlibAdler32_113(u32 adler, const u8* data, u32 byte_count) {
    constexpr u32 kAdlerMod = 65521;
    if (data == nullptr) {
        return 1;
    }

    u32 low = adler & 0xffffu;
    u32 high = (adler >> 16) & 0xffffu;
    for (u32 index = 0; index < byte_count; ++index) {
        low = (low + data[index]) % kAdlerMod;
        high = (high + low) % kAdlerMod;
    }
    return (high << 16) | low;
}

int HandleZlibCompress113MinusOneLevel(void* destination, u32* destination_len,
    const void* source, u32 source_len) {
    return ZlibCompress113WithLevel(destination, destination_len, source, source_len,
        -1);
}

#ifdef _WIN32
HMODULE ZlibRuntime113Module() {
    static HMODULE module = []() -> HMODULE {
        for (const char* name : {"zlib1.dll", "libz.dll", "zlib.dll"}) {
            if (HMODULE loaded = LoadLibraryA(name)) {
                return loaded;
            }
        }
        return nullptr;
    }();
    return module;
}

template <typename Proc>
Proc ZlibRuntime113Proc(const char* name) {
    HMODULE module = ZlibRuntime113Module();
    return module != nullptr
        ? reinterpret_cast<Proc>(GetProcAddress(module, name))
        : nullptr;
}
#endif

constexpr int kZlib113StreamSizeCompat = sizeof(z_stream);

int ZlibInflateReset113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateReset")) {
        return proc(stream);
    }
#endif
    return ::inflateReset(static_cast<z_streamp>(stream));
}

int ZlibInflateEnd113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateEnd")) {
        return proc(stream);
    }
#endif
    return ::inflateEnd(static_cast<z_streamp>(stream));
}

int ZlibInflateInit2_113(void* stream, int window_bits) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateInit2_")) {
        return proc(stream, window_bits, GetZlibVersion113(),
            kZlib113StreamSizeCompat);
    }
#endif
    return ::inflateInit2_(static_cast<z_streamp>(stream), window_bits,
        GetZlibVersion113(), kZlib113StreamSizeCompat);
}

int ZlibInflateInit_113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateInit_")) {
        return proc(stream, GetZlibVersion113(), kZlib113StreamSizeCompat);
    }
#endif
    return ::inflateInit_(static_cast<z_streamp>(stream),
        GetZlibVersion113(), kZlib113StreamSizeCompat);
}

int ZlibInflate113(void* stream, int flush) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflate")) {
        return proc(stream, flush);
    }
#endif
    return ::inflate(static_cast<z_streamp>(stream), flush);
}

int ZlibInflateSetDictionary113(void* stream, const u8* dictionary,
    u32 dictionary_len) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, const u8*, u32);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateSetDictionary")) {
        return proc(stream, dictionary, dictionary_len);
    }
#endif
    return ::inflateSetDictionary(static_cast<z_streamp>(stream),
        reinterpret_cast<const Bytef*>(dictionary),
        static_cast<uInt>(dictionary_len));
}

int ZlibInflateSync113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateSync")) {
        return proc(stream);
    }
#endif
    return ::inflateSync(static_cast<z_streamp>(stream));
}

int ZlibInflateSyncPoint113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateSyncPoint")) {
        return proc(stream);
    }
#endif
    return ::inflateSyncPoint(static_cast<z_streamp>(stream));
}

int ZlibDeflateInit_113(void* stream, int level) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateInit_")) {
        return proc(stream, level, GetZlibVersion113(),
            kZlib113StreamSizeCompat);
    }
#endif
    return ::deflateInit_(static_cast<z_streamp>(stream), level,
        GetZlibVersion113(), kZlib113StreamSizeCompat);
}

int ZlibDeflateInit2_113(void* stream, int level, int method, int window_bits,
    int mem_level, int strategy) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, int, int, int, int, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateInit2_")) {
        return proc(stream, level, method, window_bits, mem_level, strategy,
            GetZlibVersion113(), kZlib113StreamSizeCompat);
    }
#endif
    return ::deflateInit2_(static_cast<z_streamp>(stream), level, method,
        window_bits, mem_level, strategy, GetZlibVersion113(),
        kZlib113StreamSizeCompat);
}

int ZlibDeflateSetDictionary113(void* stream, const u8* dictionary,
    u32 dictionary_len) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, const u8*, u32);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateSetDictionary")) {
        return proc(stream, dictionary, dictionary_len);
    }
#endif
    return ::deflateSetDictionary(static_cast<z_streamp>(stream),
        reinterpret_cast<const Bytef*>(dictionary),
        static_cast<uInt>(dictionary_len));
}

int ZlibDeflateReset113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateReset")) {
        return proc(stream);
    }
#endif
    return ::deflateReset(static_cast<z_streamp>(stream));
}

int ZlibDeflateParams113(void* stream, int level, int strategy) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateParams")) {
        return proc(stream, level, strategy);
    }
#endif
    return ::deflateParams(static_cast<z_streamp>(stream), level, strategy);
}

int ZlibDeflate113(void* stream, int flush) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflate")) {
        return proc(stream, flush);
    }
#endif
    return ::deflate(static_cast<z_streamp>(stream), flush);
}

int ZlibDeflateEnd113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateEnd")) {
        return proc(stream);
    }
#endif
    return ::deflateEnd(static_cast<z_streamp>(stream));
}

int ZlibDeflateCopy113(void* destination_stream, void* source_stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateCopy")) {
        return proc(destination_stream, source_stream);
    }
#endif
    return ::deflateCopy(static_cast<z_streamp>(destination_stream),
        static_cast<z_streamp>(source_stream));
}

constexpr int kZlibEndBlockCode = 256;
constexpr u32 kZlibNil = 0;
constexpr u32 kZlibMinLookahead = MAX_MATCH + MIN_MATCH + 1;
constexpr u32 kZlibTooFar = 4096;
constexpr int kZlibRep3To6 = 16;
constexpr int kZlibRepZero3To10 = 17;
constexpr int kZlibRepZero11To138 = 18;
constexpr int kZlibSmallestHeapIndex = 1;
constexpr int kZlibBitBufferSize = 8 * 2 * sizeof(char);

constexpr std::array<int, LENGTH_CODES> kZlibExtraLengthBits{{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
}};
constexpr std::array<int, D_CODES> kZlibExtraDistanceBits{{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
}};
constexpr std::array<u8, BL_CODES> kZlibBitLengthOrder{{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
}};
constexpr std::array<int, LENGTH_CODES> kZlibBaseLength{{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24,
    28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 0,
}};
constexpr std::array<int, D_CODES> kZlibBaseDistance{{
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128,
    192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096,
    6144, 8192, 12288, 16384, 24576,
}};

enum ZlibDeflateBlockState {
    kZlibNeedMore = 0,
    kZlibBlockDone = 1,
    kZlibFinishStarted = 2,
    kZlibFinishDone = 3,
};

u32 ReadDeflateBuffer(void* stream, void* buffer, u32 size);
u32 ReverseDeflateBits(u32 value, u32 bit_count);

struct ZlibDeflateLevelConfig {
    u16 good_length;
    u16 max_lazy;
    u16 nice_length;
    u16 max_chain;
};

constexpr std::array<ZlibDeflateLevelConfig, 10> kZlibDeflateLevelConfigs = {{
    {0, 0, 0, 0},
    {4, 4, 8, 4},
    {4, 5, 16, 8},
    {4, 6, 32, 32},
    {4, 4, 16, 16},
    {8, 16, 32, 32},
    {8, 16, 128, 128},
    {8, 32, 128, 256},
    {32, 128, 258, 1024},
    {32, 258, 258, 4096},
}};

void WriteDeflatePendingWordMsbFirst(void* state, u32 value) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value >> 8);
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value);
}
void FlushDeflatePendingOutput(void* stream) {
    auto* zstream = static_cast<z_streamp>(stream);
    if (zstream == nullptr || zstream->state == nullptr) {
        return;
    }
    auto* deflate = reinterpret_cast<deflate_state*>(zstream->state);
    unsigned len = static_cast<unsigned>(deflate->pending);
    if (len > zstream->avail_out) {
        len = zstream->avail_out;
    }
    if (len == 0) {
        return;
    }
    std::memcpy(zstream->next_out, deflate->pending_out, len);
    zstream->next_out += len;
    deflate->pending_out += len;
    zstream->total_out += len;
    zstream->avail_out -= len;
    deflate->pending -= static_cast<int>(len);
    if (deflate->pending == 0) {
        deflate->pending_out = deflate->pending_buf;
    }
}
void InitializeDeflateLongestMatchState(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr) {
        return;
    }
    deflate->window_size = static_cast<ulg>(2U) * deflate->w_size;
    if (deflate->head != nullptr && deflate->hash_size != 0) {
        deflate->head[deflate->hash_size - 1] = 0;
        std::memset(deflate->head, 0,
            (deflate->hash_size - 1) * sizeof(*deflate->head));
    }
    const int level = std::clamp(deflate->level, 0, 9);
    const ZlibDeflateLevelConfig& config =
        kZlibDeflateLevelConfigs[static_cast<std::size_t>(level)];
    deflate->max_lazy_match = config.max_lazy;
    deflate->good_match = config.good_length;
    deflate->nice_match = config.nice_length;
    deflate->max_chain_length = config.max_chain;
    deflate->strstart = 0;
    deflate->block_start = 0;
    deflate->lookahead = 0;
    deflate->match_length = MIN_MATCH - 1;
    deflate->prev_length = MIN_MATCH - 1;
    deflate->match_available = 0;
    deflate->ins_h = 0;
}

void UpdateDeflateHashCompat(deflate_state* deflate, u32& hash, u32 value) {
    hash = ((hash << deflate->hash_shift) ^ value) & deflate->hash_mask;
}

void InsertDeflateStringCompat(deflate_state* deflate, u32 strstart,
    IPos& match_head) {
    UpdateDeflateHashCompat(deflate, deflate->ins_h,
        deflate->window[strstart + (MIN_MATCH - 1)]);
    match_head = deflate->head[deflate->ins_h];
    deflate->prev[strstart & deflate->w_mask] = static_cast<Pos>(match_head);
    deflate->head[deflate->ins_h] = static_cast<Pos>(strstart);
}

void SeedDeflateHashAtCurrentStart(deflate_state* deflate) {
    if (deflate->lookahead >= MIN_MATCH) {
        deflate->ins_h = deflate->window[deflate->strstart];
        UpdateDeflateHashCompat(deflate, deflate->ins_h,
            deflate->window[deflate->strstart + 1]);
    }
}

u32 LongestDeflateMatchCompat(deflate_state* deflate, IPos cur_match) {
    if (deflate == nullptr || deflate->window == nullptr ||
        deflate->prev == nullptr || cur_match == kZlibNil) {
        return 0;
    }

    unsigned chain_length = deflate->max_chain_length;
    int best_len = static_cast<int>(deflate->prev_length);
    int nice_match = deflate->nice_match;
    const IPos limit = deflate->strstart > static_cast<IPos>(MAX_DIST(deflate))
        ? deflate->strstart - static_cast<IPos>(MAX_DIST(deflate))
        : kZlibNil;
    const u32 max_scan = std::min<u32>(MAX_MATCH, deflate->lookahead);
    if (max_scan == 0) {
        return 0;
    }
    if (best_len >= static_cast<int>(max_scan)) {
        return max_scan;
    }
    if (deflate->prev_length >= deflate->good_match) {
        chain_length >>= 2;
    }
    if (nice_match > static_cast<int>(deflate->lookahead)) {
        nice_match = static_cast<int>(deflate->lookahead);
    }

    const Bytef* scan = deflate->window + deflate->strstart;
    do {
        if (cur_match >= deflate->strstart) {
            break;
        }
        const Bytef* match = deflate->window + cur_match;
        if (best_len >= 1 &&
            match[best_len - 1] != scan[best_len - 1]) {
            cur_match = deflate->prev[cur_match & deflate->w_mask];
            continue;
        }
        if (best_len < static_cast<int>(max_scan) &&
            match[best_len] != scan[best_len]) {
            cur_match = deflate->prev[cur_match & deflate->w_mask];
            continue;
        }
        if (match[0] != scan[0] || match[1] != scan[1]) {
            cur_match = deflate->prev[cur_match & deflate->w_mask];
            continue;
        }

        u32 len = 2;
        while (len < max_scan && scan[len] == match[len]) {
            ++len;
        }
        if (len > static_cast<u32>(best_len)) {
            deflate->match_start = cur_match;
            best_len = static_cast<int>(len);
            if (best_len >= nice_match) {
                break;
            }
        }
        cur_match = deflate->prev[cur_match & deflate->w_mask];
    } while (cur_match > limit && --chain_length != 0);

    return std::min<u32>(static_cast<u32>(best_len), deflate->lookahead);
}

void FillDeflateWindowCompat(deflate_state* deflate) {
    if (deflate == nullptr || deflate->strm == nullptr ||
        deflate->window == nullptr) {
        return;
    }

    const uInt wsize = deflate->w_size;
    do {
        unsigned more = static_cast<unsigned>(
            deflate->window_size - static_cast<ulg>(deflate->lookahead) -
            static_cast<ulg>(deflate->strstart));

        if (more == 0 && deflate->strstart == 0 && deflate->lookahead == 0) {
            more = wsize;
        } else if (more == static_cast<unsigned>(-1)) {
            --more;
        } else if (deflate->strstart >= wsize + MAX_DIST(deflate)) {
            zmemcpy(deflate->window, deflate->window + wsize,
                static_cast<unsigned>(wsize));
            deflate->match_start -= wsize;
            deflate->strstart -= wsize;
            deflate->block_start -= static_cast<long>(wsize);

            u32 count = deflate->hash_size;
            Posf* head = &deflate->head[count];
            do {
                const unsigned value = *--head;
                *head = static_cast<Pos>(value >= wsize ? value - wsize : kZlibNil);
            } while (--count != 0);

            count = wsize;
            Posf* prev = &deflate->prev[count];
            do {
                const unsigned value = *--prev;
                *prev = static_cast<Pos>(value >= wsize ? value - wsize : kZlibNil);
            } while (--count != 0);
            more += wsize;
        }

        if (deflate->strm->avail_in == 0) {
            return;
        }

        const u32 read = ReadDeflateBuffer(deflate->strm,
            deflate->window + deflate->strstart + deflate->lookahead, more);
        deflate->lookahead += read;
        SeedDeflateHashAtCurrentStart(deflate);
    } while (deflate->lookahead < kZlibMinLookahead &&
        deflate->strm->avail_in != 0);
}

void FlushDeflateBlockOnlyCompat(deflate_state* deflate, bool eof) {
    _tr_flush_block(deflate,
        deflate->block_start >= 0
            ? reinterpret_cast<charf*>(
                &deflate->window[static_cast<unsigned>(deflate->block_start)])
            : static_cast<charf*>(Z_NULL),
        static_cast<ulg>(static_cast<long>(deflate->strstart) -
            deflate->block_start),
        eof ? 1 : 0);
    deflate->block_start = deflate->strstart;
    FlushDeflatePendingOutput(deflate->strm);
}

int FlushDeflateBlockCompat(deflate_state* deflate, bool eof) {
    FlushDeflateBlockOnlyCompat(deflate, eof);
    if (deflate->strm != nullptr && deflate->strm->avail_out == 0) {
        return eof ? kZlibFinishStarted : kZlibNeedMore;
    }
    return -1;
}

int DeflateStoredBlockMode(void* state, int flush) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->strm == nullptr) {
        return Z_STREAM_ERROR;
    }

    ulg max_block_size = 0xffff;
    if (max_block_size > deflate->pending_buf_size - 5) {
        max_block_size = deflate->pending_buf_size - 5;
    }

    for (;;) {
        if (deflate->lookahead <= 1) {
            FillDeflateWindowCompat(deflate);
            if (deflate->lookahead == 0 && flush == Z_NO_FLUSH) {
                return kZlibNeedMore;
            }
            if (deflate->lookahead == 0) {
                break;
            }
        }

        deflate->strstart += deflate->lookahead;
        deflate->lookahead = 0;

        const ulg max_start = static_cast<ulg>(deflate->block_start) +
            max_block_size;
        if (deflate->strstart == 0 ||
            static_cast<ulg>(deflate->strstart) >= max_start) {
            deflate->lookahead = static_cast<uInt>(
                static_cast<ulg>(deflate->strstart) - max_start);
            deflate->strstart = static_cast<uInt>(max_start);
            const int result = FlushDeflateBlockCompat(deflate, false);
            if (result >= 0) {
                return result;
            }
        }
        if (deflate->strstart - static_cast<uInt>(deflate->block_start) >=
            MAX_DIST(deflate)) {
            const int result = FlushDeflateBlockCompat(deflate, false);
            if (result >= 0) {
                return result;
            }
        }
    }

    const bool finish = flush == Z_FINISH;
    const int result = FlushDeflateBlockCompat(deflate, finish);
    if (result >= 0) {
        return result;
    }
    return finish ? kZlibFinishDone : kZlibBlockDone;
}

void FillDeflateWindow(void* state) {
    FillDeflateWindowCompat(static_cast<deflate_state*>(state));
}
u32 ReadDeflateBuffer(void* stream, void* buffer, u32 size) {
    auto* zstream = static_cast<z_streamp>(stream);
    if (zstream == nullptr || zstream->state == nullptr || buffer == nullptr) {
        return 0;
    }
    auto* deflate = reinterpret_cast<deflate_state*>(zstream->state);
    u32 len = zstream->avail_in;
    if (len > size) {
        len = size;
    }
    if (len == 0) {
        return 0;
    }
    zstream->avail_in -= len;
    if (!deflate->noheader) {
        zstream->adler = adler32(zstream->adler, zstream->next_in, len);
    }
    std::memcpy(buffer, zstream->next_in, len);
    zstream->next_in += len;
    zstream->total_in += len;
    return len;
}
int DeflateFastBlockMode(void* state, int flush) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->strm == nullptr) {
        return Z_STREAM_ERROR;
    }

    IPos hash_head = kZlibNil;
    int should_flush = 0;
    for (;;) {
        if (deflate->lookahead < kZlibMinLookahead) {
            FillDeflateWindowCompat(deflate);
            if (deflate->lookahead < kZlibMinLookahead &&
                flush == Z_NO_FLUSH) {
                return kZlibNeedMore;
            }
            if (deflate->lookahead == 0) {
                break;
            }
        }

        if (deflate->lookahead >= MIN_MATCH) {
            InsertDeflateStringCompat(deflate, deflate->strstart, hash_head);
        }

        if (hash_head != kZlibNil &&
            deflate->strstart - hash_head <= MAX_DIST(deflate) &&
            deflate->strategy != Z_HUFFMAN_ONLY) {
            deflate->match_length = LongestDeflateMatchCompat(deflate, hash_head);
        }

        if (deflate->match_length >= MIN_MATCH) {
            _tr_tally_dist(deflate, deflate->strstart - deflate->match_start,
                deflate->match_length - MIN_MATCH, should_flush);
            deflate->lookahead -= deflate->match_length;
            if (deflate->match_length <= deflate->max_insert_length &&
                deflate->lookahead >= MIN_MATCH) {
                --deflate->match_length;
                do {
                    ++deflate->strstart;
                    InsertDeflateStringCompat(deflate, deflate->strstart,
                        hash_head);
                } while (--deflate->match_length != 0);
                ++deflate->strstart;
            } else {
                deflate->strstart += deflate->match_length;
                deflate->match_length = 0;
                SeedDeflateHashAtCurrentStart(deflate);
            }
        } else {
            _tr_tally_lit(deflate, deflate->window[deflate->strstart],
                should_flush);
            --deflate->lookahead;
            ++deflate->strstart;
        }

        if (should_flush != 0) {
            const int result = FlushDeflateBlockCompat(deflate, false);
            if (result >= 0) {
                return result;
            }
        }
    }

    const bool finish = flush == Z_FINISH;
    const int result = FlushDeflateBlockCompat(deflate, finish);
    if (result >= 0) {
        return result;
    }
    return finish ? kZlibFinishDone : kZlibBlockDone;
}

u32 LongestDeflateMatch(void* state, u32 cur_match) {
    return LongestDeflateMatchCompat(static_cast<deflate_state*>(state),
        static_cast<IPos>(cur_match));
}

int DeflateSlowBlockMode(void* state, int flush) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->strm == nullptr) {
        return Z_STREAM_ERROR;
    }

    IPos hash_head = kZlibNil;
    int should_flush = 0;
    for (;;) {
        if (deflate->lookahead < kZlibMinLookahead) {
            FillDeflateWindowCompat(deflate);
            if (deflate->lookahead < kZlibMinLookahead &&
                flush == Z_NO_FLUSH) {
                return kZlibNeedMore;
            }
            if (deflate->lookahead == 0) {
                break;
            }
        }

        if (deflate->lookahead >= MIN_MATCH) {
            InsertDeflateStringCompat(deflate, deflate->strstart, hash_head);
        }

        deflate->prev_length = deflate->match_length;
        deflate->prev_match = deflate->match_start;
        deflate->match_length = MIN_MATCH - 1;

        if (hash_head != kZlibNil &&
            deflate->prev_length < deflate->max_lazy_match &&
            deflate->strstart - hash_head <= MAX_DIST(deflate)) {
            if (deflate->strategy != Z_HUFFMAN_ONLY) {
                deflate->match_length =
                    LongestDeflateMatchCompat(deflate, hash_head);
            }
            if (deflate->match_length <= 5 &&
                (deflate->strategy == Z_FILTERED ||
                    (deflate->match_length == MIN_MATCH &&
                        deflate->strstart - deflate->match_start >
                            kZlibTooFar))) {
                deflate->match_length = MIN_MATCH - 1;
            }
        }

        if (deflate->prev_length >= MIN_MATCH &&
            deflate->match_length <= deflate->prev_length) {
            const uInt max_insert =
                deflate->strstart + deflate->lookahead - MIN_MATCH;
            _tr_tally_dist(deflate, deflate->strstart - 1 - deflate->prev_match,
                deflate->prev_length - MIN_MATCH, should_flush);
            deflate->lookahead -= deflate->prev_length - 1;
            deflate->prev_length -= 2;
            do {
                if (++deflate->strstart <= max_insert) {
                    InsertDeflateStringCompat(deflate, deflate->strstart,
                        hash_head);
                }
            } while (--deflate->prev_length != 0);
            deflate->match_available = 0;
            deflate->match_length = MIN_MATCH - 1;
            ++deflate->strstart;

            if (should_flush != 0) {
                const int result = FlushDeflateBlockCompat(deflate, false);
                if (result >= 0) {
                    return result;
                }
            }
        } else if (deflate->match_available != 0) {
            _tr_tally_lit(deflate, deflate->window[deflate->strstart - 1],
                should_flush);
            if (should_flush != 0) {
                FlushDeflateBlockOnlyCompat(deflate, false);
            }
            ++deflate->strstart;
            --deflate->lookahead;
            if (deflate->strm->avail_out == 0) {
                return kZlibNeedMore;
            }
        } else {
            deflate->match_available = 1;
            ++deflate->strstart;
            --deflate->lookahead;
        }
    }

    if (deflate->match_available != 0) {
        _tr_tally_lit(deflate, deflate->window[deflate->strstart - 1],
            should_flush);
        deflate->match_available = 0;
    }

    const bool finish = flush == Z_FINISH;
    const int result = FlushDeflateBlockCompat(deflate, finish);
    if (result >= 0) {
        return result;
    }
    return finish ? kZlibFinishDone : kZlibBlockDone;
}
void ResetInflateBlocksState(void* blocks_state, void* stream, u32* check_value) {
    ::inflate_blocks_reset(
        static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream),
        reinterpret_cast<uLongf*>(check_value));
}
void* CreateInflateBlocksState(void* stream, void* check_function, u32 window_size) {
    return ::inflate_blocks_new(static_cast<z_streamp>(stream),
        reinterpret_cast<check_func>(check_function),
        static_cast<uInt>(window_size));
}
int ProcessInflateBlocks(void* blocks_state, void* stream, int result) {
    return ::inflate_blocks(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream), result);
}
void SetInflateBlocksDictionaryWindow(
    void* blocks_state, const u8* dictionary, u32 dictionary_len) {
    ::inflate_set_dictionary(static_cast<inflate_blocks_statef*>(blocks_state),
        reinterpret_cast<const Bytef*>(dictionary),
        static_cast<uInt>(dictionary_len));
}
int CheckInflateBlocksSyncPoint(void* blocks_state) {
    return ::inflate_blocks_sync_point(
        static_cast<inflate_blocks_statef*>(blocks_state));
}
void InitializeDeflateTreeState(void* state) {
    if (state != nullptr) {
        _tr_init(static_cast<deflate_state*>(state));
    }
}
void InitializeStaticDeflateTrees() {}
void ResetDeflateTreeBlockState(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr) {
        return;
    }
    for (int index = 0; index < L_CODES; ++index) {
        deflate->dyn_ltree[index].Freq = 0;
    }
    for (int index = 0; index < D_CODES; ++index) {
        deflate->dyn_dtree[index].Freq = 0;
    }
    for (int index = 0; index < BL_CODES; ++index) {
        deflate->bl_tree[index].Freq = 0;
    }
    deflate->dyn_ltree[kZlibEndBlockCode].Freq = 1;
    deflate->opt_len = 0;
    deflate->static_len = 0;
    deflate->last_lit = 0;
    deflate->matches = 0;
}
void EmitDeflateStoredBlock(void* state, const u8* buffer, u32 stored_len,
    bool last_block) {
    if (state != nullptr) {
        _tr_stored_block(static_cast<deflate_state*>(state),
            reinterpret_cast<charf*>(const_cast<u8*>(buffer)),
            static_cast<ulg>(stored_len), last_block ? 1 : 0);
    }
}
void EmitDeflateAlignmentBlock(void* state) {
    if (state != nullptr) {
        _tr_align(static_cast<deflate_state*>(state));
    }
}
int FlushDeflateBlock(void* state, const u8* buffer, u32 stored_len,
    bool last_block) {
    if (state == nullptr) {
        return Z_STREAM_ERROR;
    }
    _tr_flush_block(static_cast<deflate_state*>(state),
        reinterpret_cast<charf*>(const_cast<u8*>(buffer)),
        static_cast<ulg>(stored_len), last_block ? 1 : 0);
    return Z_OK;
}
bool DeflateTreeNodeIsSmaller(const ct_data* tree, int lhs, int rhs,
    const uch* depth) {
    return tree[lhs].Freq < tree[rhs].Freq ||
        (tree[lhs].Freq == tree[rhs].Freq && depth[lhs] <= depth[rhs]);
}

void DeflateTreePriorityDownHeapCompat(deflate_state* deflate, ct_data* tree,
    int heap_index) {
    if (deflate == nullptr || tree == nullptr || heap_index <= 0) {
        return;
    }
    const int value = deflate->heap[heap_index];
    int child = heap_index << 1;
    while (child <= deflate->heap_len) {
        if (child < deflate->heap_len &&
            DeflateTreeNodeIsSmaller(tree, deflate->heap[child + 1],
                deflate->heap[child], deflate->depth)) {
            ++child;
        }
        if (DeflateTreeNodeIsSmaller(tree, value, deflate->heap[child],
            deflate->depth)) {
            break;
        }
        deflate->heap[heap_index] = deflate->heap[child];
        heap_index = child;
        child <<= 1;
    }
    deflate->heap[heap_index] = value;
}

void GenerateDeflateBitLengthsCompat(deflate_state* deflate, tree_desc* desc) {
    if (deflate == nullptr || desc == nullptr || desc->dyn_tree == nullptr ||
        desc->stat_desc == nullptr) {
        return;
    }

    ct_data* tree = desc->dyn_tree;
    const int max_code = desc->max_code;
    const ct_data* static_tree = desc->stat_desc->static_tree;
    const intf* extra_bits = desc->stat_desc->extra_bits;
    const int extra_base = desc->stat_desc->extra_base;
    const int max_length = desc->stat_desc->max_length;
    int overflow = 0;

    for (int bits = 0; bits <= MAX_BITS; ++bits) {
        deflate->bl_count[bits] = 0;
    }

    tree[deflate->heap[deflate->heap_max]].Len = 0;
    int heap = deflate->heap_max + 1;
    for (; heap < HEAP_SIZE; ++heap) {
        const int node = deflate->heap[heap];
        int bits = tree[tree[node].Dad].Len + 1;
        if (bits > max_length) {
            bits = max_length;
            ++overflow;
        }
        tree[node].Len = static_cast<ush>(bits);
        if (node > max_code) {
            continue;
        }

        ++deflate->bl_count[bits];
        const int extra = node >= extra_base && extra_bits != nullptr
            ? extra_bits[node - extra_base]
            : 0;
        const ush frequency = tree[node].Freq;
        deflate->opt_len += static_cast<ulg>(frequency) *
            static_cast<ulg>(bits + extra);
        if (static_tree != nullptr) {
            deflate->static_len += static_cast<ulg>(frequency) *
                static_cast<ulg>(static_tree[node].Len + extra);
        }
    }

    if (overflow == 0) {
        return;
    }

    do {
        int bits = max_length - 1;
        while (bits > 0 && deflate->bl_count[bits] == 0) {
            --bits;
        }
        --deflate->bl_count[bits];
        deflate->bl_count[bits + 1] += 2;
        --deflate->bl_count[max_length];
        overflow -= 2;
    } while (overflow > 0);

    for (int bits = max_length; bits != 0; --bits) {
        int count = deflate->bl_count[bits];
        while (count != 0) {
            const int node = deflate->heap[--heap];
            if (node > max_code) {
                continue;
            }
            if (tree[node].Len != static_cast<unsigned>(bits)) {
                deflate->opt_len +=
                    static_cast<long>(bits - tree[node].Len) *
                    static_cast<long>(tree[node].Freq);
                tree[node].Len = static_cast<ush>(bits);
            }
            --count;
        }
    }
}

void GenerateDeflateCodesCompat(ct_data* tree, int max_code, ushf* bl_count) {
    if (tree == nullptr || bl_count == nullptr) {
        return;
    }

    std::array<ush, MAX_BITS + 1> next_code{};
    ush code = 0;
    for (int bits = 1; bits <= MAX_BITS; ++bits) {
        code = static_cast<ush>((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }
    for (int node = 0; node <= max_code; ++node) {
        const int len = tree[node].Len;
        if (len == 0) {
            continue;
        }
        tree[node].Code = static_cast<ush>(
            ReverseDeflateBits(next_code[len]++, static_cast<u32>(len)));
    }
}

void BuildDeflateTreeCompat(deflate_state* deflate, tree_desc* desc) {
    if (deflate == nullptr || desc == nullptr || desc->dyn_tree == nullptr ||
        desc->stat_desc == nullptr) {
        return;
    }

    ct_data* tree = desc->dyn_tree;
    const ct_data* static_tree = desc->stat_desc->static_tree;
    const int elems = desc->stat_desc->elems;
    int max_code = -1;

    deflate->heap_len = 0;
    deflate->heap_max = HEAP_SIZE;
    for (int node = 0; node < elems; ++node) {
        if (tree[node].Freq != 0) {
            deflate->heap[++deflate->heap_len] = max_code = node;
            deflate->depth[node] = 0;
        } else {
            tree[node].Len = 0;
        }
    }

    while (deflate->heap_len < 2) {
        const int node = deflate->heap[++deflate->heap_len] =
            max_code < 2 ? ++max_code : 0;
        tree[node].Freq = 1;
        deflate->depth[node] = 0;
        --deflate->opt_len;
        if (static_tree != nullptr) {
            deflate->static_len -= static_tree[node].Len;
        }
    }
    desc->max_code = max_code;

    for (int node = deflate->heap_len / 2; node >= 1; --node) {
        DeflateTreePriorityDownHeapCompat(deflate, tree, node);
    }

    int next_node = elems;
    do {
        const int least = deflate->heap[kZlibSmallestHeapIndex];
        deflate->heap[kZlibSmallestHeapIndex] =
            deflate->heap[deflate->heap_len--];
        DeflateTreePriorityDownHeapCompat(deflate, tree,
            kZlibSmallestHeapIndex);
        const int second = deflate->heap[kZlibSmallestHeapIndex];

        deflate->heap[--deflate->heap_max] = least;
        deflate->heap[--deflate->heap_max] = second;

        tree[next_node].Freq = static_cast<ush>(
            tree[least].Freq + tree[second].Freq);
        deflate->depth[next_node] = static_cast<uch>(
            std::max(deflate->depth[least], deflate->depth[second]) + 1);
        tree[least].Dad = tree[second].Dad = static_cast<ush>(next_node);
        deflate->heap[kZlibSmallestHeapIndex] = next_node++;
        DeflateTreePriorityDownHeapCompat(deflate, tree,
            kZlibSmallestHeapIndex);
    } while (deflate->heap_len >= 2);

    deflate->heap[--deflate->heap_max] =
        deflate->heap[kZlibSmallestHeapIndex];
    GenerateDeflateBitLengthsCompat(deflate, desc);
    GenerateDeflateCodesCompat(tree, max_code, deflate->bl_count);
}

void ScanDeflateTreeCompat(deflate_state* deflate, ct_data* tree, int max_code) {
    if (deflate == nullptr || tree == nullptr || max_code < 0) {
        return;
    }

    int previous_length = -1;
    int next_length = tree[0].Len;
    int count = 0;
    int max_count = next_length == 0 ? 138 : 7;
    int min_count = next_length == 0 ? 3 : 4;
    tree[max_code + 1].Len = static_cast<ush>(0xffff);

    for (int node = 0; node <= max_code; ++node) {
        const int current_length = next_length;
        next_length = tree[node + 1].Len;
        if (++count < max_count && current_length == next_length) {
            continue;
        }
        if (count < min_count) {
            deflate->bl_tree[current_length].Freq += static_cast<ush>(count);
        } else if (current_length != 0) {
            if (current_length != previous_length) {
                ++deflate->bl_tree[current_length].Freq;
            }
            ++deflate->bl_tree[kZlibRep3To6].Freq;
        } else if (count <= 10) {
            ++deflate->bl_tree[kZlibRepZero3To10].Freq;
        } else {
            ++deflate->bl_tree[kZlibRepZero11To138].Freq;
        }

        count = 0;
        previous_length = current_length;
        if (next_length == 0) {
            max_count = 138;
            min_count = 3;
        } else if (current_length == next_length) {
            max_count = 6;
            min_count = 3;
        } else {
            max_count = 7;
            min_count = 4;
        }
    }
}

void PutDeflatePendingShortLsbFirst(deflate_state* deflate, ush value) {
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value);
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value >> 8);
}

void SendDeflateBitsCompat(deflate_state* deflate, int value, int length) {
    if (deflate == nullptr || deflate->pending_buf == nullptr ||
        length <= 0) {
        return;
    }

    if (deflate->bi_valid > kZlibBitBufferSize - length) {
        const int val = value;
        deflate->bi_buf = static_cast<ush>(
            deflate->bi_buf | (val << deflate->bi_valid));
        PutDeflatePendingShortLsbFirst(deflate, deflate->bi_buf);
        deflate->bi_buf = static_cast<ush>(
            static_cast<unsigned>(val) >>
            (kZlibBitBufferSize - deflate->bi_valid));
        deflate->bi_valid += length - kZlibBitBufferSize;
    } else {
        deflate->bi_buf = static_cast<ush>(
            deflate->bi_buf | (value << deflate->bi_valid));
        deflate->bi_valid += length;
    }
}

void SendDeflateCodeCompat(deflate_state* deflate, int code,
    const ct_data* tree) {
    if (tree == nullptr) {
        return;
    }
    SendDeflateBitsCompat(deflate, tree[code].Code, tree[code].Len);
}

void SendDeflateTreeCompat(deflate_state* deflate, ct_data* tree,
    int max_code) {
    if (deflate == nullptr || tree == nullptr || max_code < 0) {
        return;
    }

    int previous_length = -1;
    int next_length = tree[0].Len;
    int count = 0;
    int max_count = next_length == 0 ? 138 : 7;
    int min_count = next_length == 0 ? 3 : 4;

    for (int node = 0; node <= max_code; ++node) {
        const int current_length = next_length;
        next_length = tree[node + 1].Len;
        if (++count < max_count && current_length == next_length) {
            continue;
        }
        if (count < min_count) {
            do {
                SendDeflateCodeCompat(deflate, current_length,
                    deflate->bl_tree);
            } while (--count != 0);
        } else if (current_length != 0) {
            if (current_length != previous_length) {
                SendDeflateCodeCompat(deflate, current_length,
                    deflate->bl_tree);
                --count;
            }
            SendDeflateCodeCompat(deflate, kZlibRep3To6, deflate->bl_tree);
            SendDeflateBitsCompat(deflate, count - 3, 2);
        } else if (count <= 10) {
            SendDeflateCodeCompat(deflate, kZlibRepZero3To10,
                deflate->bl_tree);
            SendDeflateBitsCompat(deflate, count - 3, 3);
        } else {
            SendDeflateCodeCompat(deflate, kZlibRepZero11To138,
                deflate->bl_tree);
            SendDeflateBitsCompat(deflate, count - 11, 7);
        }

        count = 0;
        previous_length = current_length;
        if (next_length == 0) {
            max_count = 138;
            min_count = 3;
        } else if (current_length == next_length) {
            max_count = 6;
            min_count = 3;
        } else {
            max_count = 7;
            min_count = 4;
        }
    }
}

int BuildDeflateBitLengthTreeCompat(deflate_state* deflate) {
    if (deflate == nullptr) {
        return Z_STREAM_ERROR;
    }
    ScanDeflateTreeCompat(deflate, deflate->dyn_ltree, deflate->l_desc.max_code);
    ScanDeflateTreeCompat(deflate, deflate->dyn_dtree, deflate->d_desc.max_code);
    BuildDeflateTreeCompat(deflate, &deflate->bl_desc);

    int max_blindex = BL_CODES - 1;
    for (; max_blindex >= 3; --max_blindex) {
        if (deflate->bl_tree[kZlibBitLengthOrder[max_blindex]].Len != 0) {
            break;
        }
    }
    deflate->opt_len += 3 * (max_blindex + 1) + 5 + 5 + 4;
    return max_blindex;
}

void SendAllDeflateTreesCompat(deflate_state* deflate, int literal_codes,
    int distance_codes, int bit_length_codes) {
    if (deflate == nullptr) {
        return;
    }
    SendDeflateBitsCompat(deflate, literal_codes - 257, 5);
    SendDeflateBitsCompat(deflate, distance_codes - 1, 5);
    SendDeflateBitsCompat(deflate, bit_length_codes - 4, 4);
    for (int rank = 0; rank < bit_length_codes; ++rank) {
        SendDeflateBitsCompat(deflate,
            deflate->bl_tree[kZlibBitLengthOrder[rank]].Len, 3);
    }
    SendDeflateTreeCompat(deflate, deflate->dyn_ltree, literal_codes - 1);
    SendDeflateTreeCompat(deflate, deflate->dyn_dtree, distance_codes - 1);
}

void CompressDeflateBlockCompat(deflate_state* deflate, const ct_data* literal_tree,
    const ct_data* distance_tree) {
    if (deflate == nullptr || literal_tree == nullptr ||
        distance_tree == nullptr) {
        return;
    }

    unsigned index = 0;
    while (index < deflate->last_lit) {
        unsigned distance = deflate->d_buf[index];
        int literal_or_length = deflate->l_buf[index++];
        if (distance == 0) {
            SendDeflateCodeCompat(deflate, literal_or_length, literal_tree);
            continue;
        }

        const unsigned length_code = _length_code[literal_or_length];
        SendDeflateCodeCompat(deflate,
            static_cast<int>(length_code + LITERALS + 1), literal_tree);
        int extra = kZlibExtraLengthBits[length_code];
        if (extra != 0) {
            literal_or_length -= kZlibBaseLength[length_code];
            SendDeflateBitsCompat(deflate, literal_or_length, extra);
        }

        --distance;
        const unsigned distance_code = d_code(distance);
        SendDeflateCodeCompat(deflate, distance_code, distance_tree);
        extra = kZlibExtraDistanceBits[distance_code];
        if (extra != 0) {
            distance -= kZlibBaseDistance[distance_code];
            SendDeflateBitsCompat(deflate, static_cast<int>(distance), extra);
        }
    }

    SendDeflateCodeCompat(deflate, kZlibEndBlockCode, literal_tree);
    deflate->last_eob_len = literal_tree[kZlibEndBlockCode].Len;
}

void BuildDeflateTree(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate != nullptr) {
        BuildDeflateTreeCompat(deflate, &deflate->l_desc);
    }
}

void BuildDeflateTree(void* state, void* desc) {
    BuildDeflateTreeCompat(static_cast<deflate_state*>(state),
        static_cast<tree_desc*>(desc));
}

void DeflateTreePriorityDownHeap(void* state, void* tree, u32 heap_index) {
    DeflateTreePriorityDownHeapCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(tree), static_cast<int>(heap_index));
}

void GenerateDeflateBitLengths(void* state, void* desc) {
    GenerateDeflateBitLengthsCompat(static_cast<deflate_state*>(state),
        static_cast<tree_desc*>(desc));
}

void GenerateDeflateCodes(void* tree, u32 max_code, void* bl_count) {
    GenerateDeflateCodesCompat(static_cast<ct_data*>(tree),
        static_cast<int>(max_code), static_cast<ushf*>(bl_count));
}

int BuildDeflateBitLengthTree(void* state) {
    return BuildDeflateBitLengthTreeCompat(static_cast<deflate_state*>(state));
}

void ScanDeflateTree(void* state, void* tree, int max_code) {
    ScanDeflateTreeCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(tree), max_code);
}

void SendDeflateTree(void* state, void* tree, int max_code) {
    SendDeflateTreeCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(tree), max_code);
}

void SendAllDeflateTrees(void* state, int literal_codes, int distance_codes,
    int bit_length_codes) {
    SendAllDeflateTreesCompat(static_cast<deflate_state*>(state),
        literal_codes, distance_codes, bit_length_codes);
}
bool TallyDeflateLiteralOrDistance(void* state, u32 distance, u32 literal_or_length) {
    return state != nullptr &&
        _tr_tally(static_cast<deflate_state*>(state), distance,
            literal_or_length) != 0;
}
void CompressDeflateBlock(void* state, void* literal_tree, void* distance_tree) {
    CompressDeflateBlockCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(literal_tree),
        static_cast<ct_data*>(distance_tree));
}
void SetDeflateDataType(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr) {
        return;
    }
    unsigned ascii_freq = 0;
    unsigned binary_freq = 0;
    int index = 0;
    while (index < 7) {
        binary_freq += deflate->dyn_ltree[index++].Freq;
    }
    while (index < 128) {
        ascii_freq += deflate->dyn_ltree[index++].Freq;
    }
    while (index < LITERALS) {
        binary_freq += deflate->dyn_ltree[index++].Freq;
    }
    deflate->data_type =
        static_cast<Byte>(binary_freq > (ascii_freq >> 2) ? Z_BINARY : Z_ASCII);
}
u32 ReverseDeflateBits(u32 value, u32 bit_count) {
    u32 reversed = 0;
    for (u32 index = 0; index < bit_count; ++index) {
        reversed = (reversed << 1) | (value & 1u);
        value >>= 1;
    }
    return reversed;
}
void FlushDeflateBitBuffer(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    if (deflate->bi_valid == 16) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf >> 8);
        deflate->bi_buf = 0;
        deflate->bi_valid = 0;
    } else if (deflate->bi_valid >= 8) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
        deflate->bi_buf >>= 8;
        deflate->bi_valid -= 8;
    }
}
void WindUpDeflateBitBuffer(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    if (deflate->bi_valid > 8) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf >> 8);
    } else if (deflate->bi_valid > 0) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
    }
    deflate->bi_buf = 0;
    deflate->bi_valid = 0;
}
void CopyDeflateStoredBlock(void* state, const u8* buffer, u32 len, bool header) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    WindUpDeflateBitBuffer(state);
    deflate->last_eob_len = 8;
    if (header) {
        const auto length = static_cast<ush>(len);
        deflate->pending_buf[deflate->pending++] = static_cast<Byte>(length);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(length >> 8);
        const auto inverse = static_cast<ush>(~length);
        deflate->pending_buf[deflate->pending++] = static_cast<Byte>(inverse);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(inverse >> 8);
    }
    if (buffer != nullptr && len != 0) {
        std::memcpy(deflate->pending_buf + deflate->pending, buffer, len);
        deflate->pending += static_cast<int>(len);
    }
}
void* CreateInflateCodesState(
    u32 literal_bits, u32 distance_bits, void* literal_tree, void* distance_tree,
    void* stream) {
    return ::inflate_codes_new(static_cast<uInt>(literal_bits),
        static_cast<uInt>(distance_bits),
        static_cast<inflate_huft*>(literal_tree),
        static_cast<inflate_huft*>(distance_tree),
        static_cast<z_streamp>(stream));
}
int ProcessInflateCodes(void* blocks_state, void* stream, int result) {
    return ::inflate_codes(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream), result);
}
void FreeInflateCodesState(void* codes_state, void* stream) {
    ::inflate_codes_free(static_cast<inflate_codes_statef*>(codes_state),
        static_cast<z_streamp>(stream));
}
int BuildInflateCodeLengthTree(void* lengths, void* bits, void* table,
    void* hufts, void* stream) {
    return ::inflate_trees_bits(static_cast<uIntf*>(lengths),
        static_cast<uIntf*>(bits),
        reinterpret_cast<inflate_huft**>(table),
        static_cast<inflate_huft*>(hufts),
        static_cast<z_streamp>(stream));
}
int BuildInflateHuffmanTree(void* lengths, u32 code_count, u32 simple_count,
    const void* base_values, const void* extra_bits, void* table_out,
    void* max_bits, void* huft_space, void* hufts_used, void* work_values) {
    constexpr u32 kInflateMaxCodeBits = 15;

    auto* bit_lengths = static_cast<uIntf*>(lengths);
    auto* table = reinterpret_cast<inflate_huft**>(table_out);
    auto* requested_bits = static_cast<uIntf*>(max_bits);
    auto* hufts = static_cast<inflate_huft*>(huft_space);
    auto* used = static_cast<uInt*>(hufts_used);
    auto* values = static_cast<uIntf*>(work_values);
    const auto* bases = static_cast<const uIntf*>(base_values);
    const auto* extras = static_cast<const uIntf*>(extra_bits);
    if (bit_lengths == nullptr || table == nullptr || requested_bits == nullptr ||
        hufts == nullptr || used == nullptr || values == nullptr) {
        return Z_STREAM_ERROR;
    }

    std::array<uInt, kInflateMaxCodeBits + 1> counts{};
    for (u32 index = 0; index < code_count; ++index) {
        if (bit_lengths[index] > kInflateMaxCodeBits) {
            return Z_DATA_ERROR;
        }
        ++counts[bit_lengths[index]];
    }
    if (counts[0] == code_count) {
        *table = static_cast<inflate_huft*>(Z_NULL);
        *requested_bits = 0;
        return Z_OK;
    }

    int table_bits = static_cast<int>(*requested_bits);
    uInt min_length = 1;
    while (min_length <= kInflateMaxCodeBits && counts[min_length] == 0) {
        ++min_length;
    }
    if (static_cast<uInt>(table_bits) < min_length) {
        table_bits = static_cast<int>(min_length);
    }
    uInt max_length = kInflateMaxCodeBits;
    while (max_length != 0 && counts[max_length] == 0) {
        --max_length;
    }
    if (static_cast<uInt>(table_bits) > max_length) {
        table_bits = static_cast<int>(max_length);
    }
    *requested_bits = static_cast<uInt>(table_bits);

    int remaining = 1 << min_length;
    uInt length = min_length;
    for (; length < max_length; ++length, remaining <<= 1) {
        remaining -= static_cast<int>(counts[length]);
        if (remaining < 0) {
            return Z_DATA_ERROR;
        }
    }
    remaining -= static_cast<int>(counts[max_length]);
    if (remaining < 0) {
        return Z_DATA_ERROR;
    }
    counts[max_length] += static_cast<uInt>(remaining);

    std::array<uInt, kInflateMaxCodeBits + 1> offsets{};
    uInt offset = 0;
    offsets[1] = 0;
    for (uInt bits = 1; bits < max_length; ++bits) {
        offset += counts[bits];
        offsets[bits + 1] = offset;
    }

    for (u32 index = 0; index < code_count; ++index) {
        const uInt bits = bit_lengths[index];
        if (bits != 0) {
            values[offsets[bits]++] = index;
        }
    }
    const uInt value_count = offsets[max_length];

    std::array<uInt, kInflateMaxCodeBits + 1> code_stack{};
    std::array<inflate_huft*, kInflateMaxCodeBits> table_stack{};
    uInt huffman_code = 0;
    offsets[0] = 0;
    uIntf* value = values;
    int table_level = -1;
    int decoded_bits = -table_bits;
    table_stack[0] = static_cast<inflate_huft*>(Z_NULL);
    inflate_huft* current_table = static_cast<inflate_huft*>(Z_NULL);
    uInt table_size = 0;

    for (int current_bits = static_cast<int>(min_length);
         current_bits <= static_cast<int>(max_length); ++current_bits) {
        uInt length_count = counts[current_bits];
        while (length_count-- != 0) {
            while (current_bits > decoded_bits + table_bits) {
                ++table_level;
                decoded_bits += table_bits;

                table_size = static_cast<uInt>(
                    static_cast<int>(max_length) - decoded_bits);
                table_size = table_size > static_cast<uInt>(table_bits)
                    ? static_cast<uInt>(table_bits)
                    : table_size;
                uInt entry_bits = static_cast<uInt>(current_bits - decoded_bits);
                uInt patterns = 1u << entry_bits;
                if (patterns > length_count + 1) {
                    patterns -= length_count + 1;
                    uIntf* count_probe = counts.data() + current_bits;
                    if (entry_bits < table_size) {
                        while (++entry_bits < table_size) {
                            patterns <<= 1;
                            if (patterns <= *++count_probe) {
                                break;
                            }
                            patterns -= *count_probe;
                        }
                    }
                }
                table_size = 1u << entry_bits;
                if (*used + table_size > MANY) {
                    return Z_MEM_ERROR;
                }

                current_table = hufts + *used;
                table_stack[table_level] = current_table;
                *used += table_size;
                if (table_level != 0) {
                    code_stack[table_level] = huffman_code;
                    inflate_huft link{};
                    link.word.what.Bits = static_cast<Byte>(table_bits);
                    link.word.what.Exop = static_cast<Byte>(entry_bits);
                    const uInt parent_index =
                        huffman_code >> (decoded_bits - table_bits);
                    link.base = static_cast<uInt>(
                        current_table - table_stack[table_level - 1] -
                        parent_index);
                    table_stack[table_level - 1][parent_index] = link;
                } else {
                    *table = current_table;
                }
            }

            inflate_huft entry{};
            entry.word.what.Bits = static_cast<Byte>(
                current_bits - decoded_bits);
            if (value >= values + value_count) {
                entry.word.what.Exop = 128 + 64;
            } else if (*value < simple_count) {
                entry.word.what.Exop =
                    static_cast<Byte>(*value < 256 ? 0 : 32 + 64);
                entry.base = *value++;
            } else {
                if (bases == nullptr || extras == nullptr) {
                    return Z_DATA_ERROR;
                }
                const uInt complex_index = *value++ - simple_count;
                entry.word.what.Exop = static_cast<Byte>(
                    extras[complex_index] + 16 + 64);
                entry.base = bases[complex_index];
            }

            const uInt fill_step = 1u << (current_bits - decoded_bits);
            for (uInt index = huffman_code >> decoded_bits; index < table_size;
                 index += fill_step) {
                current_table[index] = entry;
            }

            uInt bit = 1u << (current_bits - 1);
            while ((huffman_code & bit) != 0) {
                huffman_code ^= bit;
                bit >>= 1;
            }
            huffman_code ^= bit;

            uInt mask = (1u << decoded_bits) - 1;
            while ((huffman_code & mask) != code_stack[table_level]) {
                --table_level;
                decoded_bits -= table_bits;
                mask = (1u << decoded_bits) - 1;
            }
        }
    }

    return remaining != 0 && max_length != 1 ? Z_BUF_ERROR : Z_OK;
}
int BuildInflateDynamicTrees(u32 literal_count, u32 distance_count, void* lengths,
    void* literal_bits, void* distance_bits, void* literal_tree,
    void* distance_tree, void* hufts, void* stream) {
    return ::inflate_trees_dynamic(static_cast<uInt>(literal_count),
        static_cast<uInt>(distance_count),
        static_cast<uIntf*>(lengths),
        static_cast<uIntf*>(literal_bits),
        static_cast<uIntf*>(distance_bits),
        reinterpret_cast<inflate_huft**>(literal_tree),
        reinterpret_cast<inflate_huft**>(distance_tree),
        static_cast<inflate_huft*>(hufts),
        static_cast<z_streamp>(stream));
}
void GetInflateFixedTrees(void* literal_bits, void* distance_bits,
    void* literal_tree, void* distance_tree, void* stream) {
    ::inflate_trees_fixed(static_cast<uIntf*>(literal_bits),
        static_cast<uIntf*>(distance_bits),
        reinterpret_cast<inflate_huft**>(literal_tree),
        reinterpret_cast<inflate_huft**>(distance_tree),
        static_cast<z_streamp>(stream));
}
int FlushInflateWindow(void* blocks_state, void* stream, int result) {
    return ::inflate_flush(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream), result);
}
int ProcessInflateFast(u32 literal_bits, u32 distance_bits, void* literal_tree,
    void* distance_tree, void* blocks_state, void* stream) {
    return ::inflate_fast(static_cast<uInt>(literal_bits),
        static_cast<uInt>(distance_bits),
        static_cast<inflate_huft*>(literal_tree),
        static_cast<inflate_huft*>(distance_tree),
        static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream));
}

int ZlibInflateBlocksFree113(void* blocks_state, void* stream) {
    return ::inflate_blocks_free(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream));
}

void RegisterMilesShutdownAtExit(void (*callback)()) {
    if (callback != nullptr) {
        std::atexit(callback);
    }
}

wchar_t* ocscpy(wchar_t* destination, const wchar_t* source) {
    if (destination == nullptr || source == nullptr) {
        return nullptr;
    }
    const std::size_t bytes = (std::wcslen(source) + 1) * sizeof(wchar_t);
    return static_cast<wchar_t*>(CrtMemMove(destination, source, bytes));
}

void* ConstructMfcObjectBase(void* object) {
    if (object != nullptr) {
        ConstructCObject(*static_cast<MfcObjectCompat*>(object));
    }
    return object;
}

void* DeleteMfcObjectBase(void* object, unsigned flags) {
    ConstructMfcObjectBase(object);
    if ((flags & 1U) != 0U) {
#ifdef _WIN32
        MfcThreadSlotRuntime_005ead15(static_cast<HLOCAL>(object));
#else
        MfcDebugDeleteClientBlock(object);
#endif
    }
    return object;
}

void DestroyMfcObjectBase(void* object) {
    if (object != nullptr) {
        DestroyCObject(*static_cast<MfcObjectCompat*>(object));
    }
}

bool AfxAssertFailedLine(const char*, int);

void* CSimpleList(void* list, int element_size) {
    if (list != nullptr) {
        const std::uint32_t head = 0;
        auto* bytes = static_cast<unsigned char*>(list);
        std::memcpy(bytes, &head, sizeof(head));
        std::memcpy(bytes + 4, &element_size, sizeof(element_size));
    }
    return list;
}

void* InitializeMfcSimpleListElementSize(void* list, int element_size) {
    if (list == nullptr) {
        return nullptr;
    }
    std::uint32_t head = 0;
    auto* bytes = static_cast<unsigned char*>(list);
    std::memcpy(&head, bytes, sizeof(head));
    if (head != 0 &&
        AfxAssertFailedLine("E:\\8665\\vc98\\mfc\\mfc\\include\\afxtls_.h",
            0x3c)) {
        CrtDebugBreak();
    }
    std::memcpy(bytes + 4, &element_size, sizeof(element_size));
    return list;
}

void* CTypeLibCache(void* cache) {
    if (cache != nullptr) {
        const std::int32_t minus_one = -1;
        const std::uint32_t zero = 0;
        auto* bytes = static_cast<unsigned char*>(cache);
        std::memcpy(bytes + 4, &minus_one, sizeof(minus_one));
        std::memcpy(bytes + 8, &zero, sizeof(zero));
        std::memcpy(bytes + 0x1c, &zero, sizeof(zero));
        std::memcpy(bytes + 0x20, &zero, sizeof(zero));
    }
    return cache;
}

void* GetMfcObjectVtable(const void* object) {
    return object != nullptr ? *static_cast<void* const*>(object) : nullptr;
}

int CompareMbcsCollationCaseSensitive(const char* lhs, const char* rhs) {
    return std::strcmp(lhs == nullptr ? "" : lhs, rhs == nullptr ? "" : rhs);
}

int CompareMbcsCollationCaseInsensitive(const char* lhs, const char* rhs) {
#ifdef _WIN32
    return _stricmp(lhs == nullptr ? "" : lhs, rhs == nullptr ? "" : rhs);
#else
    return CompareMbcsCollationCaseSensitive(lhs, rhs);
#endif
}

void DestroyMfcExceptionBase(void* exception);

void* DeleteMfcExceptionBase(void* exception, unsigned flags) {
    DestroyMfcExceptionBase(exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteNormalBlock(exception);
    }
    return exception;
}

void DestroyMfcExceptionBase(void* exception) {
    if (exception != nullptr) {
        DestroyExceptionBase(*static_cast<MfcSimpleExceptionCompat*>(exception));
    }
}
MfcRuntimeClassCompat* AfxClassInit(MfcRuntimeClassCompat* runtime_class);
void* GetMfcSimpleListNodeData(const void* owner, void* node) {
    if (node == nullptr &&
        AfxAssertFailedLine("E:\\8665\\vc98\\mfc\\mfc\\include\\afxtls_.h", 0x40)) {
        CrtDebugBreak();
    }
    int data_offset = 0;
    if (owner != nullptr) {
        const auto* bytes = static_cast<const unsigned char*>(owner);
        std::memcpy(&data_offset, bytes + 4, sizeof(data_offset));
    }
    const auto address = reinterpret_cast<std::uintptr_t>(node) +
        static_cast<std::uintptr_t>(data_offset);
    return reinterpret_cast<void*>(address);
}

void* GetMfcSimpleListNodeData(void* node) {
    return GetMfcSimpleListNodeData(nullptr, node);
}
void* ReturnSecondArgument(void*, void* second) { return second; }
void* AFX_CLASSINIT(void* initializer, MfcRuntimeClassCompat* runtime_class) {
    AfxClassInit(runtime_class);
    return initializer;
}

void AFX_CLASSINIT(MfcRuntimeClassCompat* runtime_class) {
    AfxClassInit(runtime_class);
}
void* ReturnInputPointer(void* value) { return value; }
int GetMfcSimpleArrayCount(const void* array) {
    if (array == nullptr) {
        return 0;
    }
    int count = 0;
    const auto* bytes = static_cast<const unsigned char*>(array);
    std::memcpy(&count, bytes + 4, sizeof(count));
    return count;
}

void* GetMfcSimpleArrayElementSlot(void* array, int index) {
    if (array == nullptr) {
        return nullptr;
    }
    const int count = GetMfcSimpleArrayCount(array);
    if (count <= index &&
        AfxAssertFailedLine("E:\\8665\\vc98\\mfc\\mfc\\include\\afxcoll.inl", 0x7b)) {
        CrtDebugBreak();
    }
    void* data_field = nullptr;
    const auto* bytes = static_cast<const unsigned char*>(array);
    std::memcpy(&data_field, bytes + 8, sizeof(data_field));
    auto* data = static_cast<unsigned char*>(data_field);
    return data != nullptr ? data + index * 4 : nullptr;
}

#ifdef _WIN32
namespace {

BSTR AllocateOleBstrFromAnsi(const char* text) {
    if (text == nullptr) {
        return nullptr;
    }

    const int wide_chars = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (wide_chars <= 0) {
        return SysAllocString(L"");
    }

    std::vector<wchar_t> wide(static_cast<std::size_t>(wide_chars));
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), wide_chars) <= 0) {
        return SysAllocString(L"");
    }
    return SysAllocString(wide.data());
}

SAFEARRAY* CreateOleByteSafeArray(const unsigned char* bytes,
    std::size_t byte_count) {
    SAFEARRAY* array = SafeArrayCreateVector(
        VT_UI1, 0, static_cast<ULONG>(byte_count));
    if (array == nullptr) {
        return nullptr;
    }

    if (byte_count == 0) {
        return array;
    }

    void* data = nullptr;
    if (FAILED(SafeArrayAccessData(array, &data))) {
        SafeArrayDestroy(array);
        return nullptr;
    }
    if (bytes != nullptr) {
        std::memcpy(data, bytes, byte_count);
    } else {
        std::memset(data, 0, byte_count);
    }
    SafeArrayUnaccessData(array);
    return array;
}

} // namespace

int DrawState(HDC dc, HBRUSH brush, DRAWSTATEPROC callback, LPARAM data,
    WPARAM data_length, int x, int y, int width, int height, UINT flags) {
    return DrawStateA(
        dc, brush, callback, data, data_length, x, y, width, height, flags);
}

VARIANTARG& Attach(VARIANTARG& target, VARIANTARG& source) {
    VariantClear(&target);
    target = source;
    VariantInit(&source);
    return target;
}

VARIANTARG& ConstructOleVariantFromCStringPointer(VARIANTARG& variant,
    const char* text) {
    VariantClear(&variant);
    V_VT(&variant) = VT_BSTR;
    V_BSTR(&variant) = AllocateOleBstrFromAnsi(text);
    return variant;
}

VARIANTARG& ConstructOleVariantFromCStringObject(VARIANTARG& variant,
    const MfcCStringCompat& text) {
    return ConstructOleVariantFromCStringPointer(
        variant, CStringGetStringPtr(text));
}

VARIANTARG& ConstructOleVariantFromI2OrBool(VARIANTARG& variant, short value,
    VARTYPE type) {
    VariantInit(&variant);
    V_VT(&variant) = type;
    if (type == VT_BOOL) {
        V_BOOL(&variant) = value != 0 ? VARIANT_TRUE : VARIANT_FALSE;
    } else {
        V_I2(&variant) = value;
    }
    return variant;
}

VARIANTARG& ConstructOleVariantFromI4ErrorOrBool(VARIANTARG& variant, LONG value,
    VARTYPE type) {
    VariantInit(&variant);
    V_VT(&variant) = type;
    if (type == VT_BOOL) {
        V_BOOL(&variant) = value != 0 ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (type == VT_ERROR) {
        V_ERROR(&variant) = value;
    } else {
        V_I4(&variant) = value;
    }
    return variant;
}

OleCurrencyCompat FinishOleCurrencyFromVariant(const VARIANTARG& variant) {
    return ConstructOleCurrencyFromVariant(variant);
}

// Ghidra original symbol: FID_conflict:COleCurrency.
OleCurrencyCompat FID_conflict_COleCurrency(CY value) {
    OleCurrencyCompat result{};
    result.value = value;
    result.status = OleDateStatus::Valid;
    return result;
}

OleCurrencyCompat COleCurrency(LONG units, LONG fractional_10000) {
    return SetOleCurrencyParts(units, fractional_10000);
}

// Ghidra original symbol at 005f7788: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_add_assign(
    OleCurrencyCompat& value, const OleCurrencyCompat& rhs) {
    value = AddOleCurrency(value, rhs);
    return value;
}

// Ghidra original symbol at 005f77b3: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_subtract_assign(
    OleCurrencyCompat& value, const OleCurrencyCompat& rhs) {
    value = SubtractOleCurrency(value, rhs);
    return value;
}

// Ghidra original symbol at 005f77de: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_multiply_assign(
    OleCurrencyCompat& value, LONG factor) {
    value = MultiplyOleCurrencyByLong(value, factor);
    return value;
}

// Ghidra original symbol at 005f7809: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_divide_assign(
    OleCurrencyCompat& value, LONG divisor) {
    value = DivideOleCurrencyByLong(value, divisor);
    return value;
}

bool OleCurrencyRawEquals(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return lhs.status == rhs.status && lhs.value.int64 == rhs.value.int64;
}

bool OleCurrencyRawNotEquals(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return !OleCurrencyRawEquals(lhs, rhs);
}

CY operator_union_tagCY(const OleCurrencyCompat& value) {
    return value.value;
}

bool CompareOleCurrencyLess(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) < 0;
}

bool CompareOleCurrencyGreater(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) > 0;
}

bool CompareOleCurrencyLessEqual(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) <= 0;
}

bool CompareOleCurrencyGreaterEqual(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) >= 0;
}

std::tm AdjustDecodedDateTimeForTm(const std::tm& value) { return value; }
double NormalizeNegativeOleDateIntegral(double value) { return value; }
double NormalizeNegativeOleDateFraction(double value) { return value; }

OleDateTimeCompat ConstructOleDateTimeFromTimeT(std::time_t value);

OleDateTimeCompat GetCurrentOleDateTime() {
    return ConstructOleDateTimeFromTimeT(CrtTime(nullptr));
}

bool GetOleDateTimeAsSystemTime(const OleDateTimeCompat& value,
    SYSTEMTIME& out) {
    return DecodeOleDateTime(value, out);
}

int GetOleDateTimeSecond(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wSecond : 0;
}

int GetOleDateTimeMinute(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wMinute : 0;
}

int GetOleDateTimeHour(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wHour : 0;
}

int GetOleDateTimeDay(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wDay : 0;
}

int GetOleDateTimeMonth(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wMonth : 0;
}

int GetOleDateTimeYear(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wYear : 0;
}

int GetOleDateTimeDayOfWeek(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wDayOfWeek : 0;
}

int GetOleDateTimeDayOfYear(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    if (!DecodeOleDateTime(value, out)) {
        return 0;
    }
    static constexpr int days_before_month[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const bool leap = (out.wYear % 4 == 0 && out.wYear % 100 != 0) ||
        out.wYear % 400 == 0;
    return days_before_month[out.wMonth] + out.wDay +
        (leap && out.wMonth > 2 ? 1 : 0);
}

OleDateTimeCompat FinishOleDateTimeFromVariant(const VARIANTARG& variant) {
    return ConstructOleDateTimeFromVariant(variant);
}

OleDateTimeCompat ConstructOleDateTimeFromTimeT(std::time_t value) {
    const std::tm* decoded = std::localtime(&value);
    if (decoded == nullptr) {
        return OleDateTimeCompat{0.0, OleDateStatus::Invalid};
    }
    return ConstructOleDateTimeFromFields(
        static_cast<WORD>(decoded->tm_year + 1900),
        static_cast<WORD>(decoded->tm_mon + 1),
        static_cast<WORD>(decoded->tm_mday),
        static_cast<WORD>(decoded->tm_hour),
        static_cast<WORD>(decoded->tm_min),
        static_cast<WORD>(decoded->tm_sec));
}

bool CompareOleDateTimeLess(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) < 0;
}

bool CompareOleDateTimeGreater(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) > 0;
}

bool CompareOleDateTimeLessEqual(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) <= 0;
}

bool CompareOleDateTimeGreaterEqual(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) >= 0;
}

OleDateTimeSpanCompat SubtractOleDateTimeSpan(
    const OleDateTimeSpanCompat& lhs, const OleDateTimeSpanCompat& rhs) {
    return SubtractOleDateTimeSpans(lhs, rhs);
}

OleDateTimeCompat SetOleDateTime(WORD year, WORD month, WORD day, WORD hour,
    WORD minute, WORD second) {
    return ConstructOleDateTimeFromFields(year, month, day, hour, minute, second);
}

int SetDate(OleDateTimeCompat& value, int year, int month, int day) {
    value = ConstructOleDateTimeFromFields(static_cast<WORD>(year),
        static_cast<WORD>(month), static_cast<WORD>(day), 0, 0, 0);
    return static_cast<int>(value.status);
}

int SetTime(OleDateTimeCompat& value, int hour, int minute, int second) {
    value = ConstructOleDateTimeFromFields(1899, 12, 30,
        static_cast<WORD>(hour), static_cast<WORD>(minute),
        static_cast<WORD>(second));
    return static_cast<int>(value.status);
}

bool CheckOleDateTimeRange(const OleDateTimeCompat& value) {
    return value.status == OleDateStatus::Valid;
}

void SerializeOleDateTimeToArchive(const OleDateTimeCompat& value, void* archive) {
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    ArchiveWriteDWordInline(*typed_archive, static_cast<DWORD>(value.status));
    ArchiveWriteDoubleInline(*typed_archive, value.value);
}

OleDateTimeSpanCompat& AssignOleDateTimeSpan(OleDateTimeSpanCompat& target,
    const OleDateTimeSpanCompat& source) {
    target = source;
    return target;
}
bool CheckOleDateTimeSpanRange(const OleDateTimeSpanCompat& value) {
    return value.status == OleDateStatus::Valid;
}
void SerializeOleDateTimeSpanToArchive(const OleDateTimeSpanCompat& value,
    void* archive) {
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    ArchiveWriteDWordInline(*typed_archive, static_cast<DWORD>(value.status));
    ArchiveWriteDoubleInline(*typed_archive, value.days);
}

VARIANTARG& AssignOleSafeArrayFromArray(VARIANTARG& target,
    const VARIANTARG& source) {
    VariantCopy(&target, const_cast<VARIANTARG*>(&source));
    return target;
}
SAFEARRAY* OleVariantArrayPointer(const VARIANTARG& value) {
    VARIANTARG* mutable_value = const_cast<VARIANTARG*>(&value);
    return (V_VT(mutable_value) & VT_ARRAY) != 0
        ? V_ARRAY(mutable_value) : nullptr;
}

bool CompareOleSafeArrayVariantRef(const VARIANTARG& lhs,
    const VARIANTARG& rhs) {
    return CompareSafeArrays(OleVariantArrayPointer(lhs),
        OleVariantArrayPointer(rhs));
}
bool CompareOleSafeArrayVariantPtr(const VARIANTARG* lhs,
    const VARIANTARG* rhs) {
    return lhs == rhs || (lhs != nullptr && rhs != nullptr &&
        CompareOleSafeArrayVariantRef(*lhs, *rhs));
}
bool CompareOleSafeArrayObjectRef(const VARIANTARG& lhs,
    const VARIANTARG& rhs) {
    return CompareOleSafeArrayVariantRef(lhs, rhs);
}
bool CompareOleSafeArrayObjectPtr(const VARIANTARG* lhs,
    const VARIANTARG* rhs) {
    return CompareOleSafeArrayVariantPtr(lhs, rhs);
}

// Ghidra original symbol: COleSafeArray::operator==.
bool COleSafeArray_operator_equal(const VARIANTARG& lhs,
    const VARIANTARG& rhs) {
    return CompareOleSafeArrayVariantRef(lhs, rhs);
}

// Ghidra original symbol: FID_conflict:GetDim.
UINT FID_conflict_GetDim(SAFEARRAY* array) {
    return SafeArrayGetDim(array);
}

// Ghidra original symbol at 005f81d8: FID_conflict:GetDim.
UINT FID_conflict_GetElemSize(SAFEARRAY* array) {
    return SafeArrayGetElemsize(array);
}

UINT FID_conflict_GetDim(const VARIANTARG& variant) {
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    return (V_VT(mutable_variant) & VT_ARRAY) != 0 &&
            V_ARRAY(mutable_variant) != nullptr
        ? SafeArrayGetDim(V_ARRAY(mutable_variant))
        : 0;
}

UINT FID_conflict_GetElemSize(const VARIANTARG& variant) {
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    return (V_VT(mutable_variant) & VT_ARRAY) != 0 &&
            V_ARRAY(mutable_variant) != nullptr
        ? SafeArrayGetElemsize(V_ARRAY(mutable_variant))
        : 0;
}

// Ghidra original symbol at 0051d30c: FID_conflict:GetUBound.
HRESULT FID_conflict_GetLBound(SAFEARRAY* array, UINT dimension,
    LONG* lower_bound) {
    return array == nullptr ? E_POINTER
        : SafeArrayGetLBound(array, dimension, lower_bound);
}

// Ghidra original symbol: FID_conflict:GetUBound.
LONG FID_conflict_GetUBound(SAFEARRAY* array, UINT dimension) {
    LONG upper_bound = 0;
    return array != nullptr &&
            SUCCEEDED(SafeArrayGetUBound(array, dimension, &upper_bound))
        ? upper_bound
        : 0;
}

// Ghidra original symbol at 0051d334: FID_conflict:GetUBound.
HRESULT FID_conflict_GetUBound(SAFEARRAY* array, UINT dimension,
    LONG* upper_bound) {
    return array == nullptr ? E_POINTER
        : SafeArrayGetUBound(array, dimension, upper_bound);
}

LONG FID_conflict_GetUBound(const VARIANTARG& variant, UINT dimension) {
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    return (V_VT(mutable_variant) & VT_ARRAY) != 0
        ? FID_conflict_GetUBound(V_ARRAY(mutable_variant), dimension)
        : 0;
}

// Ghidra original symbol at 0051d35c: FID_conflict:GetUBound.
HRESULT FID_conflict_GetElement(SAFEARRAY* array, LONG* indices, void* out) {
    return array == nullptr ? E_POINTER
        : SafeArrayGetElement(array, indices, out);
}

// Ghidra original symbol at 0051d384: FID_conflict:GetUBound.
HRESULT FID_conflict_PtrOfIndex(SAFEARRAY* array, LONG* indices, void** out) {
    return array == nullptr ? E_POINTER
        : SafeArrayPtrOfIndex(array, indices, out);
}

// Ghidra original symbol at 0051d3ac: FID_conflict:GetUBound.
HRESULT FID_conflict_PutElement(SAFEARRAY* array, LONG* indices, void* value) {
    return array == nullptr ? E_POINTER
        : SafeArrayPutElement(array, indices, value);
}

SAFEARRAY* CreateMultiDimOleSafeArrayFromCounts(VARTYPE type,
    const std::vector<LONG>& counts) {
    if (counts.empty()) {
        return nullptr;
    }
    std::vector<SAFEARRAYBOUND> bounds(counts.size());
    for (std::size_t index = 0; index < counts.size(); ++index) {
        bounds[index].lLbound = 0;
        bounds[index].cElements = static_cast<ULONG>(std::max<LONG>(counts[index], 0));
    }
    return SafeArrayCreate(type, static_cast<UINT>(bounds.size()), bounds.data());
}
SAFEARRAY* FinishCreateMultiDimOleSafeArray(SAFEARRAY* array) { return array; }
HRESULT AllocDescriptor(UINT dimensions, SAFEARRAY** array) {
    return SafeArrayAllocDescriptor(dimensions, array);
}

VARIANTARG& AssignOleVariantFromByteArray(VARIANTARG& variant,
    const MfcByteArrayCompat& array) {
    SAFEARRAY* bytes = CreateOleByteSafeArray(
        array.values.empty() ? nullptr : array.values.data(),
        array.values.size());
    VariantClear(&variant);
    V_VT(&variant) = VT_ARRAY | VT_UI1;
    V_ARRAY(&variant) = bytes;
    return variant;
}

void SerializeOleVariantToArchive(const VARIANTARG& variant, void* archive) {
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    const VARTYPE type = V_VT(mutable_variant);
    ArchiveWriteWordInline(*typed_archive, type);
    if ((type & (VT_ARRAY | VT_BYREF)) != 0) {
        return;
    }
    switch (type) {
    case VT_EMPTY:
    case VT_NULL:
        break;
    case VT_I2:
        ArchiveWriteWordInline(*typed_archive,
            static_cast<unsigned short>(V_I2(mutable_variant)));
        break;
    case VT_I4:
        ArchiveWriteDWordInline(*typed_archive,
            static_cast<unsigned>(V_I4(mutable_variant)));
        break;
    case VT_R4:
        ArchiveWriteFloatInline(*typed_archive, V_R4(mutable_variant));
        break;
    case VT_R8:
    case VT_DATE:
        ArchiveWriteDoubleInline(*typed_archive, V_R8(mutable_variant));
        break;
    case VT_CY: {
        const auto raw = static_cast<unsigned long long>(
            V_CY(mutable_variant).int64);
        ArchiveWriteLongInline(*typed_archive,
            static_cast<long>(raw & 0xffffffffULL));
        ArchiveWriteDWordInline(*typed_archive,
            static_cast<unsigned>(raw >> 32));
        break;
    }
    case VT_BSTR: {
        BSTR text = V_BSTR(mutable_variant);
        const unsigned bytes = text != nullptr ? SysStringByteLen(text) : 0;
        ArchiveWriteLongInline(*typed_archive, static_cast<long>(bytes));
        if (bytes != 0) {
            ArchiveWrite(*typed_archive, text, bytes);
        }
        break;
    }
    case VT_ERROR:
        ArchiveWriteDWordInline(*typed_archive,
            static_cast<unsigned>(V_ERROR(mutable_variant)));
        break;
    case VT_BOOL:
        ArchiveWriteWordInline(*typed_archive,
            static_cast<unsigned short>(V_BOOL(mutable_variant)));
        break;
    case VT_UI1:
        ArchiveWriteByteInline(*typed_archive, V_UI1(mutable_variant));
        break;
    default:
        break;
    }
}

void FinishSerializeOleVariantStream(void*) {}

void DeserializeOleVariantFromArchive(VARIANTARG& variant, void* archive) {
    VariantClear(&variant);
    VariantInit(&variant);
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    unsigned short type = VT_EMPTY;
    ArchiveReadWordInline(*typed_archive, type);
    V_VT(&variant) = type;
    if ((type & (VT_ARRAY | VT_BYREF)) != 0) {
        return;
    }
    switch (type) {
    case VT_EMPTY:
    case VT_NULL:
        break;
    case VT_I2: {
        unsigned short value = 0;
        ArchiveReadWordInline(*typed_archive, value);
        V_I2(&variant) = static_cast<SHORT>(value);
        break;
    }
    case VT_I4: {
        unsigned value = 0;
        ArchiveReadDWordInline(*typed_archive, value);
        V_I4(&variant) = static_cast<LONG>(value);
        break;
    }
    case VT_R4:
        ArchiveReadFloatInline(*typed_archive, V_R4(&variant));
        break;
    case VT_R8:
    case VT_DATE:
        ArchiveReadDoubleInline(*typed_archive, V_R8(&variant));
        break;
    case VT_CY: {
        long low = 0;
        unsigned high = 0;
        ArchiveReadLongInline(*typed_archive, low);
        ArchiveReadDWordInline(*typed_archive, high);
        const unsigned long long raw =
            (static_cast<unsigned long long>(high) << 32) |
            static_cast<unsigned long>(low);
        V_CY(&variant).int64 = static_cast<long long>(raw);
        break;
    }
    case VT_BSTR: {
        long byte_count = 0;
        ArchiveReadLongInline(*typed_archive, byte_count);
        if (byte_count > 0) {
            BSTR text = SysAllocStringByteLen(nullptr,
                static_cast<unsigned>(byte_count));
            if (text != nullptr) {
                ArchiveRead(*typed_archive, text,
                    static_cast<unsigned>(byte_count));
                V_BSTR(&variant) = text;
            } else {
                std::array<char, 256> discard{};
                long remaining = byte_count;
                while (remaining > 0) {
                    const unsigned chunk = static_cast<unsigned>(
                        std::min<long>(remaining,
                            static_cast<long>(discard.size())));
                    ArchiveRead(*typed_archive, discard.data(), chunk);
                    remaining -= static_cast<long>(chunk);
                }
            }
        }
        break;
    }
    case VT_ERROR: {
        unsigned value = 0;
        ArchiveReadDWordInline(*typed_archive, value);
        V_ERROR(&variant) = static_cast<SCODE>(value);
        break;
    }
    case VT_BOOL: {
        unsigned short value = 0;
        ArchiveReadWordInline(*typed_archive, value);
        V_BOOL(&variant) = static_cast<VARIANT_BOOL>(value);
        break;
    }
    case VT_UI1:
        ArchiveReadByteInline(*typed_archive, V_UI1(&variant));
        break;
    default:
        break;
    }
}
void FinishDeserializeOleVariantStream(void*) {}

VARIANTARG* ConstructOleVariantArray(VARIANTARG* values, int count) {
    if (values == nullptr || count <= 0) {
        return values;
    }
    for (int index = 0; index < count; ++index) {
        VariantInit(&values[index]);
    }
    return values;
}

void DestructOleVariantArray(VARIANTARG* values, int count) {
    if (values == nullptr || count <= 0) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        VariantClear(&values[index]);
    }
}

VARIANTARG* CopyOleVariantArray(VARIANTARG* destination,
    const VARIANTARG* source, int count) {
    if (destination == nullptr || source == nullptr || count <= 0) {
        return destination;
    }
    for (int index = 0; index < count; ++index) {
        VariantInit(&destination[index]);
        VariantCopy(&destination[index],
            const_cast<VARIANTARG*>(&source[index]));
    }
    return destination;
}

void SerializeOleVariantArray(VARIANTARG* values, int count, void* archive) {
    if (values == nullptr || count <= 0) {
        return;
    }
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        if (ArchiveIsLoading(*typed_archive)) {
            DeserializeOleVariantFromArchive(values[index], archive);
        } else {
            SerializeOleVariantToArchive(values[index], archive);
        }
    }
}

void DeleteOleVariantObject(VARIANTARG& variant) { VariantClear(&variant); }

void AfxCheckError(HRESULT result);

// Ghidra original symbol: COleVariant::operator=.
VARIANTARG& COleVariant_operator_assign(VARIANTARG& target,
    const VARIANTARG& source) {
    const HRESULT result = VariantCopy(&target, const_cast<VARIANTARG*>(&source));
    AfxCheckError(result);
    return target;
}

void* COleDispatchDriver(void* driver) { return driver; }

void InvokeHelper(MfcCWndCompat& window, LONG dispatch_id, WORD flags,
    unsigned short return_type, void* return_value,
    const unsigned char* param_info = nullptr) {
    CWndInvokeHelper(window, dispatch_id, flags, return_type, return_value,
        param_info);
}

void GetProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, void* value) {
    CWndGetProperty(window, dispatch_id, value_type, value);
}

int IsResultExpected(MfcCommandTargetCompat& target) {
    const int result = target.result_expected ? TRUE : FALSE;
    target.result_expected = true;
    return result;
}

u32 HashPointerShift4(const void* value) {
    return static_cast<u32>(
        reinterpret_cast<std::uintptr_t>(value) >> 4);
}

void* ReturnSecondArgumentThunk(void*, void* second) { return second; }
void NoopRuntimeThunk() {}

void* InstallMfcThreadStateSlotC4(void*, void* replacement) {
    return replacement;
}

void* RestoreMfcThreadStateSlotC4(void* value) { return value; }

std::size_t SpanMbcsStringIncludingThunk(const char* text,
    const char* characters) {
    return SpanMbcsStringIncluding(text, characters);
}

std::size_t SpanMbcsStringExcludingThunk(const char* text,
    const char* characters) {
    return SpanMbcsStringExcluding(text, characters);
}

int CompareMbcsStringNThunk(const char* lhs, const char* rhs,
    std::size_t count) {
    return CompareMbcsStringN(lhs, rhs, count);
}

int GetMbcsCharacterLengthThunk(const char* text) {
    return GetMbcsCharacterLength(text);
}

void* InstallMfcThreadStateSlotC0(void*, void* replacement) {
    return replacement;
}

void* RestoreMfcThreadStateSlotC0(void* value) { return value; }

bool CompareGuidBytesNotEqual(const void* lhs, const void* rhs) {
    return CompareGuidBytes(lhs, rhs) == 0;
}

int GetFileTitleAThunk(LPCSTR path, LPSTR title, WORD title_chars) {
    return GetFileTitleA(path, title, title_chars);
}

const char* PreviousMbcsStringPointerThunk(const char* start,
    const char* current) {
    return PreviousMbcsStringPointer(start, current);
}

void* LockTypeLibCacheEntry(void* entry) { return entry; }
MfcMapPtrToPtrCompat& CTypeLibCacheMap(MfcMapPtrToPtrCompat& map) {
    return ConstructMapPtrToPtr(map, 10);
}

void* CTypeLibCacheMap(void* map) {
    return map == nullptr ? nullptr
        : &CTypeLibCacheMap(*static_cast<MfcMapPtrToPtrCompat*>(map));
}

void InitString(MfcSimpleExceptionCompat& exception) {
    exception.string_initialized = true;
    exception.has_message = exception.message[0] != '\0';
    if (exception.has_message || exception.help_context == 0) {
        return;
    }
    char buffer[128]{};
    if (AfxLoadStringCompat(exception.help_context, buffer,
        static_cast<int>(sizeof(buffer))) != 0) {
        std::strncpy(exception.message.data(), buffer,
            exception.message.size() - 1);
        exception.has_message = exception.message[0] != '\0';
    }
}

MfcSimpleExceptionCompat& ConstructSimpleExceptionWithFlags(
    MfcSimpleExceptionCompat& exception, int auto_delete,
    unsigned help_context) {
    return ConstructSimpleException(
        exception, auto_delete != 0, help_context);
}

// Ghidra original symbol: FID_conflict:CNotSupportedException.
MfcSimpleExceptionCompat& FID_conflict_CNotSupportedException(
    MfcSimpleExceptionCompat& exception, int auto_delete,
    unsigned help_context) {
    ConstructNotSupportedException(exception);
    exception.auto_delete = auto_delete != 0;
    exception.help_context = help_context;
    return exception;
}

std::tm* GetLocalTm(const MfcTimeCompat& value, std::tm* out) {
    std::tm* source = std::localtime(&value.value);
    if (source == nullptr) {
        return nullptr;
    }
    if (out == nullptr) {
        return source;
    }
    *out = *source;
    return out;
}

// Ghidra original symbol: FID_conflict:GetLocalTm.
std::tm* FID_conflict_GetLocalTm(const MfcTimeCompat& value, std::tm* out) {
    return GetLocalTm(value, out);
}

COLORREF* GetSavedCustomColors() {
    return GetSavedCustomColorsCompat();
}

int FindCStringSubstringFrom(const MfcCStringCompat& text, const char* needle,
    int start_index) {
    return FindCStringSubstring(text, needle, start_index);
}

// Ghidra original symbol: HashKey<char_const*>.
unsigned HashKeyCharConst(const char* key) {
    unsigned hash = 0;
    if (key == nullptr) {
        return hash;
    }
    for (; *key != '\0'; ++key) {
        hash = hash * 0x21u + static_cast<unsigned>(static_cast<int>(*key));
    }
    return hash;
}

int BeginDrag(MfcDragListBoxCompat& box, POINT point) {
    return BeginDragListBox(box, point);
}

int InsertColumn(MfcListCtrlCompat& control, int column, const char* heading,
    int format, int width, int subitem) {
    return ListCtrlInsertColumn(control, column, heading, format, width, subitem);
}

void OnNcDestroy(MfcListCtrlCompat& control) {
    ListCtrlOnNcDestroy(control);
}

void OnDestroy(MfcTreeCtrlCompat& control) {
    TreeCtrlOnDestroy(control);
}

// Ghidra original symbol: FID_conflict:~CDragListBox.
void FID_conflict_DestructCDragListBox(MfcDragListBoxCompat& box) {
    if (box.window != nullptr) {
        DestroyWindow(box.window);
    }
    box.window = nullptr;
    box.style = 0;
    box.last_insert = -1;
}

void SetRange(MfcSliderCtrlCompat& control, int lower, int upper, int redraw) {
    SliderSetRange(control, lower, upper, redraw != 0);
}

// Ghidra original symbol: FID_conflict:CImageList.
MfcImageListCompat& FID_conflict_CImageList(MfcImageListCompat& image_list) {
    image_list.handle = nullptr;
    image_list.owns_handle = true;
    return image_list;
}

MfcMenuCompat& FID_conflict_CImageList(MfcMenuCompat& menu) {
    return ConstructMenuCompat(menu);
}

// Ghidra original symbol: FID_conflict:~CMenu.
void FID_conflict_DestructCMenu(MfcMenuCompat& menu) {
    DestroyMenuCompat(menu);
}

void FID_conflict_DestructImageList(MfcImageListCompat& image_list) {
    if (image_list.handle != nullptr && image_list.owns_handle) {
        ImageList_Destroy(image_list.handle);
    }
    image_list.handle = nullptr;
    image_list.owns_handle = true;
}

void FID_conflict_DestructGdiObject(MfcGdiObjectCompat& object) {
    DestroyGdiObjectCompat(object);
}

// Ghidra original symbol: operator_void*.
void* operator_void(const MfcGdiObjectCompat* object) {
    return object == nullptr ? nullptr : object->object;
}

// Ghidra original symbol: operator_void*.
void* operator_void(const MfcWinThreadCompat* thread) {
    return thread == nullptr ? nullptr : thread->thread;
}

// Ghidra original symbol: FID_conflict:GetSafeHdc.
void* FID_conflict_GetSafeHdc(const MfcGdiObjectCompat* object) {
    return object == nullptr ? nullptr : object->object;
}

HDC FID_conflict_GetSafeHdc(const MfcCDCCompat* dc) {
    return CDCGetSafeHdc(dc);
}

int CreateStockObject(MfcGdiObjectCompat& object, int stock_object) {
    object.object = GetStockObject(stock_object);
    object.temporary = false;
    return object.object != nullptr ? TRUE : FALSE;
}

bool operator==(const MfcGdiObjectCompat& left,
    const MfcGdiObjectCompat& right) {
    return left.object == right.object;
}

bool operator!=(const MfcGdiObjectCompat& left,
    const MfcGdiObjectCompat& right) {
    return !(left == right);
}

MfcGdiObjectCompat* FromHandle(HFONT font) {
    return FontFromHandle(font);
}

// Ghidra original symbol: FID_conflict:LookupTemporary.
MfcCWndCompat* FID_conflict_LookupTemporary(HWND handle) {
    return CWndFromHandle(handle);
}

MfcMenuCompat* FID_conflict_LookupTemporary(HMENU handle) {
    return CMenuFromHandle(handle);
}

MfcImageListCompat* FID_conflict_LookupTemporary(HIMAGELIST handle) {
    return LookupTemporaryImageList(handle);
}

MfcGdiObjectCompat* FID_conflict_LookupTemporary(HGDIOBJ handle) {
    return GdiObjectFromHandleCompat(handle);
}

// Ghidra original symbol: FID_conflict:DeleteObject.
BOOL FID_conflict_DeleteObject(MfcImageListCompat& image_list) {
    HIMAGELIST handle = image_list.handle;
    image_list.handle = nullptr;
    image_list.owns_handle = true;
    return handle == nullptr ? FALSE : ImageList_Destroy(handle);
}

BOOL FID_conflict_DeleteObject(MfcMenuCompat& menu) {
    if (menu.menu == nullptr) {
        return FALSE;
    }
    HMENU handle = menu.menu;
    menu.menu = nullptr;
    menu.temporary = false;
    return DestroyMenu(handle);
}

BOOL FID_conflict_DeleteObject(MfcGdiObjectCompat& object) {
    return GdiObjectDeleteObject(object);
}

int DestroyToolTipCtrl(MfcToolTipCtrlCompat& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    control.window = nullptr;
    control.style = 0;
    return TRUE;
}

long OnDisableModal(MfcToolTipCtrlCompat& control, UINT, long) {
    if (control.window != nullptr) {
        SendMessageA(control.window, TTM_ACTIVATE, FALSE, 0);
    }
    return 0;
}

void OnEnable(MfcToolTipCtrlCompat& control, int enabled) {
    if (control.window != nullptr) {
        SendMessageA(control.window, TTM_ACTIVATE, enabled ? TRUE : FALSE, 0);
    }
}

void ToolTipFilterRelayMessageThunk(MfcToolTipCtrlCompat& control,
    const MSG& message) {
    ToolTipFilterRelayMessage(control, message);
}

MfcArchiveStreamCompat& CArchiveStream(MfcArchiveStreamCompat& stream,
    void* archive) {
    stream.archive = archive;
    stream.buffer.clear();
    stream.position = 0;
    return stream;
}

MfcPtrListNodeCompat* AddHead(MfcPtrListCompat& list, void* value) {
    return PtrListAddHead(list, value);
}

MfcPtrListNodeCompat* AddTail(MfcPtrListCompat& list, void* value) {
    return PtrListAddTail(list, value);
}

// Ghidra original symbol: FID_conflict:CDWordArray.
MfcByteArrayCompat& FID_conflict_CDWordArray(MfcByteArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcWordArrayCompat& FID_conflict_CDWordArray(MfcWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcDWordArrayCompat& FID_conflict_CDWordArray(MfcDWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcObArrayCompat& FID_conflict_CDWordArray(MfcObArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcCStringArrayCompat& FID_conflict_CDWordArray(
    MfcCStringArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

// Ghidra original symbol: FID_conflict:CArray<int,int_const&>.
MfcUIntArrayCompat& FID_conflict_CArrayInt(MfcUIntArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcPtrArrayCompat& FID_conflict_CArrayInt(MfcPtrArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

void DeleteValues(MfcPtrArrayCompat& array) {
    array.values.clear();
}

void DeleteValues(MfcObArrayCompat& array) {
    array.values.clear();
}

void DeleteValues(std::vector<void*>& values) {
    values.clear();
}

// Ghidra original symbol: FID_conflict:~CPtrArray.
void FID_conflict_DestructCPtrArray(MfcByteArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcDWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcUIntArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcPtrArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcObArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcCStringArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

int Difference(CrtMemState& diff, const CrtMemState& old_state,
    const CrtMemState& new_state) {
    return CrtMemDifference(diff, old_state, new_state, false) ? 1 : 0;
}

void Checkpoint(CrtMemState& state) {
    CrtMemCheckpoint(state);
}

int GetErrorMessage(char* destination, unsigned destination_chars,
    unsigned* help_context) {
    if (help_context != nullptr) {
        *help_context = 0;
    }
    if (destination != nullptr && destination_chars != 0) {
        destination[0] = '\0';
    }
    return FALSE;
}

int GetErrorMessage(MfcSimpleExceptionCompat& exception, char* destination,
    unsigned destination_chars, unsigned* help_context) {
    return GetSimpleExceptionErrorMessage(exception, destination,
        static_cast<int>(destination_chars), help_context) ? TRUE : FALSE;
}

AfxExceptionLinkCompat& AFX_EXCEPTION_LINK(AfxExceptionLinkCompat& link) {
    AfxExceptionContextCompat& context = AfxExceptionContextCompatState();
    link.previous = context.current;
    link.exception = nullptr;
    context.current = &link;
    return link;
}

int IsSerializable(const MfcObjectCompat& object) {
    return object.runtime_class != nullptr &&
        object.runtime_class->schema != 0xffff ? TRUE : FALSE;
}

MfcCWndCompat* GetMainWnd(MfcWinThreadCompat& thread) {
    if (thread.main_window != nullptr) {
        return CWndFromHandle(thread.main_window);
    }
    if (thread.active_window != nullptr) {
        return CWndFromHandle(thread.active_window);
    }
    return CWndFromHandle(GetActiveWindow());
}

int IsEnterKey(const MSG* message) {
    return message != nullptr && message->message == WM_KEYDOWN &&
        message->wParam == VK_RETURN ? TRUE : FALSE;
}

int ModifyStyle(HWND window, DWORD remove_bits, DWORD add_bits, UINT flags) {
    return ModifyWindowLongStyle(window, GWL_STYLE, remove_bits, add_bits,
        flags) ? TRUE : FALSE;
}

int ModifyStyleEx(HWND window, DWORD remove_bits, DWORD add_bits, UINT flags) {
    return ModifyWindowLongStyle(window, GWL_EXSTYLE, remove_bits, add_bits,
        flags) ? TRUE : FALSE;
}

const MSG* GetCurrentMessage() {
    static MSG fallback{};
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    MSG& message = thread != nullptr ? thread->current_message : fallback;
    message.time = GetMessageTime();
    const DWORD pos = GetMessagePos();
    message.pt.x = static_cast<short>(LOWORD(pos));
    message.pt.y = static_cast<short>(HIWORD(pos));
    return &message;
}

// Ghidra original symbol: FID_conflict:DefWindowProcA.
LRESULT FID_conflict_DefWindowProcA(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    HWND hwnd = window.window;
    if (hwnd == nullptr) {
        return 0;
    }
    if (window.original_wnd_proc != nullptr) {
        return CallWindowProcA(window.original_wnd_proc, hwnd, message, wparam,
            lparam);
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT Default(MfcCWndCompat& window) {
    const MSG* message = GetCurrentMessage();
    HWND hwnd = message->hwnd != nullptr ? message->hwnd : window.window;
    if (hwnd == nullptr) {
        return 0;
    }
    if (window.original_wnd_proc != nullptr) {
        return CallWindowProcA(window.original_wnd_proc, hwnd,
            message->message, message->wParam, message->lParam);
    }
    return DefWindowProcA(hwnd, message->message, message->wParam,
        message->lParam);
}

int OnCompareItem(MfcCWndCompat& window, int, COMPAREITEMSTRUCT* compare) {
    LRESULT result = 0;
    if (compare != nullptr &&
        CWndSendChildNotifyLastMsgByHandle(compare->hwndItem, &result)) {
        return static_cast<int>(result);
    }
    return static_cast<int>(Default(window));
}

void OnDeleteItem(MfcCWndCompat& window, int, DELETEITEMSTRUCT* item) {
    if (item == nullptr ||
        !CWndSendChildNotifyLastMsgByHandle(item->hwndItem, nullptr)) {
        Default(window);
    }
}

// Ghidra original symbol: FID_conflict:OnCharToItem.
long FID_conflict_OnCharToItem(MfcCWndCompat& window, UINT,
    MfcListBoxCompat* list_box, UINT) {
    LRESULT result = 0;
    if (list_box != nullptr &&
        CWndSendChildNotifyLastMsgByHandle(list_box->window, &result)) {
        return result;
    }
    return Default(window);
}

long FID_conflict_OnCharToItem(MfcCWndCompat& window, UINT,
    MfcCWndCompat* child, UINT) {
    LRESULT result = 0;
    if (child != nullptr &&
        CWndSendChildNotifyLastMsgByHandle(child->window, &result)) {
        return result;
    }
    return Default(window);
}

long WindowProc(MfcCWndCompat& window, UINT message, WPARAM wparam,
    LPARAM lparam) {
    LRESULT result = 0;
    if (CWndOnWndMsg(window, message, wparam, lparam, &result)) {
        return result;
    }
    return FID_conflict_DefWindowProcA(window, message, wparam, lparam);
}

// Ghidra original symbol: FID_conflict:CTestCmdUI.
MfcCmdUICompat& FID_conflict_CTestCmdUI(MfcCmdUICompat& cmd_ui) {
    ConstructCmdUI(cmd_ui);
    cmd_ui.kind = MfcCmdUIKind::Test;
    cmd_ui.flags = 1;
    return cmd_ui;
}

void Enable(MfcCmdUICompat& cmd_ui, int enabled) {
    cmd_ui.flags = enabled ? 1U : 0U;
    cmd_ui.changed = true;
}

void SetPermanent(MfcHandleMapCompat& map, void* handle, void* object) {
    if (handle == nullptr) {
        return;
    }
    map.temporary.erase(handle);
    map.permanent[handle] = object;
}

IUnknown* GetControlUnknown(MfcCWndCompat& window) {
    auto* site = static_cast<MfcOleControlSiteCompat*>(window.control_site);
    return site == nullptr ? nullptr : site->control_unknown;
}

HRESULT InternalQueryInterface(IUnknown* unknown, REFIID iid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    return unknown == nullptr ? E_NOINTERFACE
        : unknown->QueryInterface(iid, object);
}

HRESULT InternalQueryInterface(MfcCommandTargetCompat&, IUnknown* outer_unknown,
    REFIID iid, void** object) {
    return InternalQueryInterface(outer_unknown, iid, object);
}

IUnknown* GetControllingUnknown(IUnknown* unknown) {
    return unknown;
}

MfcCmdUICompat& CCmdUI(MfcCmdUICompat& cmd_ui) {
    return ConstructCmdUI(cmd_ui);
}

void ThrowOsError(long os_error, const char* file_name) {
    if (os_error != 0) {
        ThrowFileException(FileExceptionCauseFromOsError(os_error), os_error,
            file_name);
    }
}

void ThrowErrno(int error, const char* file_name) {
    if (error != 0) {
        ThrowFileException(FileExceptionCauseFromErrno(error),
            static_cast<long>(*CrtDosErrnoPointer()), file_name);
    }
}

bool CreateIndirect(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent,
    void* init_param) {
    return DialogCreateIndirectCore(dialog, dialog_template, parent,
        init_param, nullptr);
}

void PostModal(MfcDialogCompat& dialog) {
    DialogPostModal(dialog);
}

long HandleSetFont(MfcDialogCompat& dialog, WPARAM font_handle, LPARAM lparam) {
    (void)lparam;
    MfcGdiObjectCompat* font = FontFromHandle(reinterpret_cast<HFONT>(font_handle));
    CWndSetFontInline(dialog, font, TRUE);
    return Default(dialog);
}

// Ghidra original symbol: Catch@00589a0f.
long ProcessWndProcException(MfcWinAppCompat& app, void* exception,
    const MSG* message) {
    if (message == nullptr) {
        return 0;
    }
    if (message->message == WM_CREATE || message->message == WM_PAINT) {
        MSG copy = *message;
        return WinThreadProcessWndProcException(app, exception, &copy);
    }
    return message->message == WM_COMMAND ? 1 : 0;
}

bool SetTemplate(MfcDialogTemplateCompat& dialog_template,
    const DLGTEMPLATE* source, unsigned size) {
    DestroyDialogTemplate(dialog_template);
    if (source == nullptr || size == 0) {
        return false;
    }

    HGLOBAL handle = GlobalAlloc(GMEM_ZEROINIT, size + 0x40);
    if (handle == nullptr) {
        return false;
    }
    void* destination = GlobalLock(handle);
    if (destination == nullptr) {
        GlobalFree(handle);
        return false;
    }
    std::memcpy(destination, source, size);
    GlobalUnlock(handle);

    MfcCStringCompat face_name;
    WORD point_size = 0;
    dialog_template.handle = handle;
    dialog_template.size = size;
    dialog_template.no_font =
        !DialogTemplateGetFont(source, face_name, point_size);
    return true;
}

int IsDirSep(char value) {
    return value == '\\' || value == '/' ? TRUE : FALSE;
}

// Ghidra original symbol: ~CFile.
void CFileDestructor(MfcFileCompat& file) {
    FileDestructor(file);
}

void Abort(MfcFileCompat& file) {
    FileAbort(file);
}

MfcDumpContext& operator<<(MfcDumpContext& context, POINT point) {
    DumpContextWriteString(context, "point(x = ");
    DumpContextWriteLong(context, point.x);
    DumpContextWriteString(context, ", y = ");
    DumpContextWriteLong(context, point.y);
    return DumpContextWriteString(context, ")");
}

MfcDumpContext& operator<<(MfcDumpContext& context, SIZE size) {
    DumpContextWriteString(context, "size(cx = ");
    DumpContextWriteLong(context, size.cx);
    DumpContextWriteString(context, ", cy = ");
    DumpContextWriteLong(context, size.cy);
    return DumpContextWriteString(context, ")");
}

MfcDumpContext& operator<<(MfcDumpContext& context, const RECT& rect) {
    DumpContextWriteString(context, "rect(left = ");
    DumpContextWriteLong(context, rect.left);
    DumpContextWriteString(context, ", top = ");
    DumpContextWriteLong(context, rect.top);
    DumpContextWriteString(context, ", right = ");
    DumpContextWriteLong(context, rect.right);
    DumpContextWriteString(context, ", bottom = ");
    DumpContextWriteLong(context, rect.bottom);
    return DumpContextWriteString(context, ")");
}

MfcDumpContext& operator<<(MfcDumpContext& context,
    const OleCurrencyCompat& value) {
    DumpContextWriteString(context, "COleCurrency Object ");
    DumpContextWriteString(context, "m_status = ");
    DumpContextWriteLong(context, static_cast<long>(value.status));
    DumpContextWriteString(context, " Currency = ");
    std::string text;
    if (FormatOleCurrencyString(value, 0, LOCALE_USER_DEFAULT, text)) {
        DumpContextWriteString(context, text.c_str());
    } else {
        DumpContextWriteLong(context,
            static_cast<long>(value.value.int64 / 10000));
    }
    return context;
}

MfcDumpContext& operator<<(MfcDumpContext& context,
    const OleDateTimeCompat& value) {
    DumpContextWriteString(context, "COleDateTime Object ");
    DumpContextWriteString(context, "m_status = ");
    DumpContextWriteLong(context, static_cast<long>(value.status));
    DumpContextWriteString(context, " DateTime = ");
    std::string text;
    if (FormatOleDateTimeString(value, 0, LOCALE_USER_DEFAULT, text)) {
        DumpContextWriteString(context, text.c_str());
    } else {
        DumpContextWriteDouble(context, value.value);
    }
    return context;
}

MfcArchiveCompat& operator<<(MfcArchiveCompat& archive,
    const OleCurrencyCompat& value) {
    const auto raw = static_cast<unsigned long long>(value.value.int64);
    ArchiveWriteDWordInline(archive, static_cast<DWORD>(value.status));
    ArchiveWriteDWordInline(archive, static_cast<DWORD>(raw >> 32));
    ArchiveWriteLongInline(archive, static_cast<long>(raw & 0xffffffffULL));
    return archive;
}

MfcArchiveCompat& operator>>(MfcArchiveCompat& archive,
    OleCurrencyCompat& value) {
    unsigned status = 0;
    unsigned high = 0;
    long low = 0;
    ArchiveReadDWordInline(archive, status);
    ArchiveReadDWordInline(archive, high);
    ArchiveReadLongInline(archive, low);
    value.status = static_cast<OleDateStatus>(status);
    const unsigned long long raw =
        (static_cast<unsigned long long>(high) << 32) |
        static_cast<unsigned long>(low);
    value.value.int64 = static_cast<long long>(raw);
    return archive;
}

MfcArchiveCompat& operator<<(MfcArchiveCompat& archive,
    const MfcObjectCompat* object) {
    ArchiveWriteObject(archive, const_cast<MfcObjectCompat*>(object));
    return archive;
}

// Ghidra original symbol: ~CDC.
void CDCDestructorOriginal(MfcCDCCompat& dc) {
    CDCDestructor(dc);
}

int PlayMetaFile(MfcCDCCompat& dc, HMETAFILE metafile) {
    if (dc.output_dc == nullptr || metafile == nullptr) {
        return FALSE;
    }
    if (GetDeviceCaps(dc.output_dc, TECHNOLOGY) == DT_METAFILE) {
        return ::PlayMetaFile(dc.output_dc, metafile);
    }
    return EnumMetaFile(dc.output_dc, metafile, CDCMetaFileEnumProc,
        reinterpret_cast<LPARAM>(&dc));
}

// Ghidra original symbol: FID_conflict:CloseEnhanced.
HMETAFILE FID_conflict_CloseEnhanced(MfcCDCCompat& dc) {
    return MetaFileDCClose(dc);
}

HENHMETAFILE CloseEnhanced(MfcCDCCompat& dc) {
    HDC handle = dc.output_dc;
    dc.output_dc = nullptr;
    dc.attribute_dc = nullptr;
    return handle == nullptr ? nullptr : CloseEnhMetaFile(handle);
}

bool CreateEnhanced(MfcCDCCompat& dc, MfcCDCCompat* reference_dc,
    const char* file_name, const RECT* bounds, const char* description) {
    HDC handle = CreateEnhMetaFileA(
        CDCGetSafeHdc(reference_dc), file_name, bounds, description);
    return CDCAttach(dc, handle);
}

HDC GetSafeHdc(const MfcCDCCompat* dc) {
    return CDCGetSafeHdc(dc);
}

MfcCWndCompat* CDCGetWindowInline(MfcCDCCompat& dc) {
    return CDCGetWindow(dc);
}

int CDCIsPrintingInline(const MfcCDCCompat& dc) {
    return CDCIsPrinting(dc) ? TRUE : FALSE;
}

bool CDCCreateCompatibleDCInline(MfcCDCCompat& dc,
    const MfcCDCCompat* source) {
    return CDCCreateCompatibleDC(dc, source);
}

int CDCGetStretchBltModeInline(const MfcCDCCompat& dc) {
    return dc.attribute_dc == nullptr ? 0 : GetStretchBltMode(dc.attribute_dc);
}

void WriteCount(MfcArchiveCompat& archive, unsigned count) {
    ArchiveWriteCount(archive, count);
}

int ReadString(MfcArchiveCompat& archive, MfcCStringCompat& text) {
    return ArchiveReadStringLine(archive, text) ? TRUE : FALSE;
}

void SerializeClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat* runtime_class) {
    if (ArchiveIsStoring(archive)) {
        if (runtime_class != nullptr) {
            ArchiveWriteClass(archive, *runtime_class);
        }
        return;
    }
    (void)ArchiveReadClass(archive, runtime_class);
}

int GetDocString(const MfcDocTemplateCompat& templ, MfcCStringCompat& out,
    int index) {
    std::string value;
    if (!DocTemplateGetDocString(templ, value, index)) {
        out.text.clear();
        return FALSE;
    }
    out.text = value;
    return TRUE;
}

void InitialUpdateFrame(MfcDocTemplateCompat& templ, MfcFrameWndCompat* frame,
    MfcDocumentCompat* document, int make_visible) {
    DocTemplateInitialUpdateFrame(templ, frame, document, make_visible);
}

int SaveAllModified(MfcDocTemplateCompat& templ) {
    return DocTemplateSaveAllModified(templ) ? TRUE : FALSE;
}

void CloseAllDocuments(MfcDocTemplateCompat& templ, int end_session) {
    DocTemplateCloseAllDocuments(templ, end_session != 0);
}

int OnCmdMsg(MfcDocTemplateCompat& templ, UINT id, int code, void* extra,
    MfcCommandHandlerInfoCompat* handler_info) {
    return DocTemplateOnCmdMsg(templ, id, code, extra, handler_info)
        ? TRUE : FALSE;
}

void SetTitle(MfcDocumentCompat& document, const char* title) {
    DocumentSetTitle(document, title);
}

void OnChangedViewList(MfcDocumentCompat& document) {
    DocumentOnChangedViewList(document);
}

int CanCloseFrame(MfcDocumentCompat& document, MfcFrameWndCompat* frame) {
    return DocumentCanCloseFrame(document, frame) ? TRUE : FALSE;
}

void OnFileClose(MfcDocumentCompat& document) {
    DocumentOnFileClose(document);
}

std::size_t GetFirstViewPosition(const MfcDocumentCompat&) {
    return 0;
}

int GetOpenDocumentCount(const MfcDocManagerCompat& manager) {
    return DocManagerGetOpenDocumentCount(&manager);
}

unsigned GetTypeInfoCount(MfcCommandTargetCompat& target) {
    (void)target;
    return CmdTargetGetTypeInfoCountDefault();
}

void WinAppSetCurrentHandles(MfcWinAppCompat& app) {
    app.thread = GetCurrentThread();
    app.thread_id = GetCurrentThreadId();
    if (app.instance == nullptr) {
        app.instance = GetModuleHandleA(nullptr);
    }
    if (app.command_line.empty()) {
        app.command_line = GetCommandLineA();
    }
    AfxSetResourceHandleCompat(app.instance);
}

MfcRuntimeClassCompat* GetCFileRuntimeClassThunk() {
    static MfcRuntimeClassCompat runtime_class{
        "CFile",
        static_cast<int>(sizeof(MfcFileCompat)),
        0xffff,
        nullptr,
        nullptr,
        nullptr};
    runtime_class.base_class = GetCObjectRuntimeClass();
    return &runtime_class;
}

bool DocManagerDeleteShellRegistrationKey(const char* key) {
    return key != nullptr &&
        RegDeleteKeyA(HKEY_CLASSES_ROOT, key) == ERROR_SUCCESS;
}

bool DocManagerSetShellRegistryValue(const char* key,
    const char* value_name, const char* value) {
    if (key == nullptr) {
        return false;
    }
    HKEY opened = nullptr;
    const LSTATUS create_status = RegCreateKeyExA(HKEY_CLASSES_ROOT, key, 0,
        nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr);
    if (create_status != ERROR_SUCCESS) {
        return false;
    }
    const char* text = value == nullptr ? "" : value;
    const DWORD bytes = static_cast<DWORD>(std::strlen(text) + 1);
    const LSTATUS set_status = RegSetValueExA(opened, value_name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(text), bytes);
    RegCloseKey(opened);
    return set_status == ERROR_SUCCESS;
}

void DocManagerUnregisterShellFileTypes(MfcDocManagerCompat& manager) {
    for (MfcDocTemplateCompat* templ : manager.templates) {
        if (templ == nullptr || templ->doc_strings.empty()) {
            continue;
        }
        DocManagerDeleteShellRegistrationKey(templ->doc_strings.c_str());
    }
}

void DocManagerRegisterShellFileTypes(MfcDocManagerCompat& manager,
    BOOL compatibility = TRUE) {
    (void)compatibility;
    for (MfcDocTemplateCompat* templ : manager.templates) {
        if (templ == nullptr || templ->doc_strings.empty()) {
            continue;
        }
        DocManagerSetShellRegistryValue(
            templ->doc_strings.c_str(), nullptr, templ->doc_strings.c_str());
    }
}

int Process(MfcWinAppCompat& app, const char* command_line) {
    if (command_line != nullptr) {
        app.command_line = command_line;
    }
    return TRUE;
}

DWORD GetScrollStyle(const MfcSplitterWndCompat& splitter) {
    return SplitterGetScrollStyle(splitter);
}

void StopTracking(MfcSplitterWndCompat& splitter, int accept) {
    if (accept != 0) {
        SplitterStopTrackingAccept(splitter);
    } else {
        SplitterStopTrackingCancel(splitter);
    }
}

void OnDisplayChange(MfcSplitterWndCompat& splitter) {
    if (CWndIsIconicInline(splitter) == FALSE &&
        CWndIsWindowVisibleInline(splitter) != FALSE) {
        SplitterRecalcLayout(splitter);
    }
}

void OnLButtonDown(MfcSplitterWndCompat& splitter, UINT flags, POINT point) {
    (void)flags;
    if (!splitter.tracking) {
        StartTracking(splitter, SplitterHitTest(splitter, point));
    }
}

HBRUSH OnCtlColor(MfcControlBarCompat& bar, MfcCDCCompat* dc,
    MfcCWndCompat* child, UINT type) {
    HDC handle = dc == nullptr ? nullptr : dc->output_dc;
    HWND child_handle = child == nullptr ? nullptr : child->window;
    return reinterpret_cast<HBRUSH>(
        CWndOnCtlColor(bar, handle, child_handle, type));
}

// Ghidra original symbol: FID_conflict:CMFCToolBarCmdUI.
MfcCmdUICompat& FID_conflict_CMFCToolBarCmdUI(MfcCmdUICompat& cmd_ui) {
    CCmdUI(cmd_ui);
    cmd_ui.kind = MfcCmdUIKind::ToolBar;
    return cmd_ui;
}

// Ghidra original symbol: ~CDialogBar.
void CDialogBarDestructorOriginal(MfcDialogBarCompat& bar) {
    DestroyDialogBar(bar);
}

void HideApplication(MfcWinAppCompat& app) {
    MfcCWndCompat* main = app.main_window == nullptr
        ? nullptr : CWndFromHandle(app.main_window);
    if (main == nullptr) {
        return;
    }
    CWndShowWindow(*main, SW_HIDE);
    CWndShowOwnedPopupsInline(*main, FALSE);
    CWndSetWindowPos(*main, HWND_BOTTOM, 0, 0, 0, 0,
        SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
}

long OnSizeParent(MfcDockBarCompat& dock_bar, UINT message,
    MfcSizeParentParamsCompat& layout) {
    return ControlBarOnSizeParent(dock_bar, message, layout);
}

// Ghidra original symbol: ~CMiniDockFrameWnd.
void CMiniDockFrameWndDestructorOriginal(MfcMiniDockFrameWndCompat& frame) {
    DestroyMiniDockFrameWnd(frame);
}

MfcNewTypeDlgCompat& CNewTypeDlg(MfcNewTypeDlgCompat& dialog,
    const std::vector<MfcDocTemplateCompat*>* templates) {
    return ConstructNewTypeDlg(dialog, templates);
}

void Cleanup(MfcPropertyPageCompat& page) {
    PropertyPageCleanup(page);
}

int OnApply(MfcPropertyPageCompat& page) {
    return PropertyPageOnApply(page);
}

int OnSetActive(MfcPropertyPageCompat& page) {
    return PropertyPageOnSetActive(page);
}

int IsButtonEnabled(MfcPropertyPageCompat& page, int button_id) {
    return PropertyPageIsButtonEnabled(page, button_id) ? TRUE : FALSE;
}

MfcPropertyPageCompat* GetActivePage(MfcPropertySheetCompat& sheet) {
    return PropertySheetGetActivePage(sheet);
}

int PrintSelection(const MfcPrintDialogExCompat& dialog) {
    return (dialog.result_action & 1U) != 0 ? TRUE : FALSE;
}

void ContinueRouting(MfcCmdUICompat& cmd_ui) {
    cmd_ui.continue_routing = true;
}

MfcImageListCompat* DeleteImageListOrMenuScalarDtor(
    MfcImageListCompat* image_list, unsigned flags) {
    if (image_list == nullptr) {
        return nullptr;
    }
    FID_conflict_DestructImageList(*image_list);
    if ((flags & 1U) != 0U) {
        delete image_list;
    }
    return image_list;
}

MfcMenuCompat* DeleteImageListOrMenuScalarDtor(MfcMenuCompat* menu,
    unsigned flags) {
    if (menu == nullptr) {
        return nullptr;
    }
    DestroyMenuCompat(*menu);
    if ((flags & 1U) != 0U) {
        delete menu;
    }
    return menu;
}

// Ghidra original symbol: operator_struct_HWND__*.
HWND operator_struct_HWND(const MfcCWndCompat* window) {
    return window == nullptr ? nullptr : window->window;
}

MfcCWndCompat* GetOwner(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND owner = GetWindow(window.window, GW_OWNER);
    if (owner != nullptr) {
        return CWndFromHandle(owner);
    }
    return CWndGetParentInline(window);
}

void SetOwner(MfcCWndCompat& window, MfcCWndCompat* owner) {
    if (window.window == nullptr) {
        return;
    }
    SetWindowLongPtrA(window.window, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(owner == nullptr ? nullptr : owner->window));
}

POINT GetCaretPos() {
    POINT point{};
    ::GetCaretPos(&point);
    return point;
}

int OnEraseBkgnd(MfcCWndCompat& window, MfcCDCCompat*) {
    return static_cast<int>(Default(window));
}

void OnGetMinMaxInfo(MfcCWndCompat& window, MINMAXINFO*) {
    (void)Default(window);
}

void CWaitCursor() {
    AfxBeginWaitCursor();
}

MfcCDCCompat& CMetaFileDC(MfcCDCCompat& dc) {
    dc = MfcCDCCompat{};
    dc.runtime_class = MfcExceptionRuntimeThunk_005e8ca8();
    return dc;
}

MfcFrameWndCompat& CMDIChildWnd(MfcFrameWndCompat& frame) {
    return ConstructFrameWnd(frame);
}

void _AFX_BASE_MODULE_STATE() {
    InitializeMfcBaseModuleStateCompat(true);
}
void* _AFX_COLOR_STATE(void* state) {
    if (state != nullptr) {
        auto* bytes = static_cast<unsigned char*>(state);
        const std::uint32_t white = 0x00ffffff;
        for (std::size_t index = 0; index < 16; ++index) {
            std::memcpy(bytes + 4 + index * sizeof(white), &white,
                sizeof(white));
        }
    }
    return state;
}

// Ghidra original symbol: ~_AFX_DEBUG_STATE.
void AfxDebugStateDestructorOriginal(AfxDebugState* state) {
    DestroyProcessLocalAfxDebugState(state);
}

// Ghidra original symbol: FID_conflict:_AFXCTL_AMBIENT_CACHE.
void* FID_conflict_AFXCTL_AMBIENT_CACHE(void* cache) {
    if (cache != nullptr) {
        ConstructCObject(*static_cast<MfcObjectCompat*>(cache));
    }
    return cache;
}

// Ghidra original symbol: ~CThreadLocalObject.
void CThreadLocalObjectDestructorOriginal(void* local) {
    if (local == nullptr) {
        return;
    }
    int slot = 0;
    std::memcpy(&slot, local, sizeof(slot));
    MfcThreadSlotRuntimeDeleteSlot(slot);
    slot = 0;
    std::memcpy(local, &slot, sizeof(slot));
}

// Ghidra original symbol: ~_AFX_PROPPAGEFONTINFO.
void AfxPropPageFontInfoDestructorOriginal(void* info) {
    DestroyPropPageFontInfoCompat(info);
}

// Ghidra original symbol: ~CWinThread.
void CWinThreadDestructorOriginal(MfcWinThreadCompat& thread) {
    DestroyWinThreadCompat(thread);
}

// Ghidra original symbol: ~AUX_DATA.
void AuxDataDestructorOriginal(void*) {}

void MfcGdiObjectRuntimeTail_005ffe50(MfcGdiObjectCompat& object) {
    DestroyGdiObjectCompat(object);
}

// Ghidra original symbol: ~CDocManager.
void CDocManagerDestructorOriginal(MfcDocManagerCompat& manager) {
    DestroyDocManager(manager);
}

MfcPrintInfoCompat* PrintInfoIdentityInline(MfcPrintInfoCompat* info) {
    return info;
}

void DestroyDialogInlineThunk(MfcDialogCompat& dialog) {
    DestroyDialog(dialog);
}

MfcDialogCompat* DeleteDialogScalarDtorThunk(MfcDialogCompat* dialog,
    unsigned flags) {
    return DeleteDialogScalarDtor(dialog, flags);
}

// Ghidra original symbol: ~CBitmapButton.
void CBitmapButtonDestructorOriginal(MfcBitmapButtonCompat& button) {
    CBitmapButtonDestructor(button);
}

// Ghidra original symbol: ~CToolBar.
void ToolBarDestructorOriginal(MfcToolBarCompat& toolbar) {
    if (toolbar.image_well != nullptr) {
        DeleteObject(toolbar.image_well);
        toolbar.image_well = nullptr;
    }
    DestroyControlBar(toolbar);
}

MfcCWndCompat* SplitterVirtualCallE8Inline(MfcSplitterWndCompat& splitter,
    int* row, int* column) {
    return SplitterGetActivePane(splitter, row, column);
}

RECT GetBorders(const MfcControlBarCompat& bar) {
    return RECT{bar.cx_left_border, bar.cy_top_border,
        bar.cx_right_border, bar.cy_bottom_border};
}

bool ToolBarLoadBitmapResourceIdInline(MfcToolBarCompat& toolbar,
    UINT resource_id) {
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(LoadImageA(
        AfxGetResourceHandleCompat(), MAKEINTRESOURCEA(resource_id & 0xffffU),
        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (bitmap == nullptr) {
        bitmap = LoadBitmapA(GetModuleHandleA(nullptr),
            MAKEINTRESOURCEA(resource_id & 0xffffU));
    }
    if (bitmap == nullptr) {
        return false;
    }
    ToolBarSetBitmapHandle(toolbar, bitmap);
    return true;
}

bool ToolBarLoadToolBarResourceIdInline(MfcToolBarCompat& toolbar,
    UINT resource_id) {
    HINSTANCE instance = AfxGetResourceHandleCompat();
    toolbar.image_instance = instance;
    toolbar.image_resource = FindResourceA(instance,
        MAKEINTRESOURCEA(resource_id & 0xffffU), MAKEINTRESOURCEA(241));
    return ToolBarLoadBitmapResourceIdInline(toolbar, resource_id) ||
        toolbar.image_resource != nullptr;
}

bool ToolBarLoadBitmapResource(MfcToolBarCompat& toolbar,
    LPCSTR resource_name) {
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(LoadImageA(
        AfxGetResourceHandleCompat(), resource_name, IMAGE_BITMAP, 0, 0,
        LR_CREATEDIBSECTION));
    if (bitmap == nullptr) {
        bitmap = LoadBitmapA(GetModuleHandleA(nullptr), resource_name);
    }
    if (bitmap == nullptr) {
        return false;
    }
    ToolBarSetBitmapHandle(toolbar, bitmap);
    return true;
}

bool ToolBarLoadBitmapResource(MfcToolBarCompat& toolbar, UINT resource_id) {
    return ToolBarLoadBitmapResourceIdInline(toolbar, resource_id);
}

bool ToolBarLoadToolBarResource(MfcToolBarCompat& toolbar,
    LPCSTR resource_name) {
    HINSTANCE instance = AfxGetResourceHandleCompat();
    toolbar.image_instance = instance;
    toolbar.image_resource =
        FindResourceA(instance, resource_name, MAKEINTRESOURCEA(241));
    return ToolBarLoadBitmapResource(toolbar, resource_name) ||
        toolbar.image_resource != nullptr;
}

bool ToolBarLoadToolBarResource(MfcToolBarCompat& toolbar, UINT resource_id) {
    return ToolBarLoadToolBarResourceIdInline(toolbar, resource_id);
}

SIZE CSize() {
    return SIZE{};
}

// Ghidra original symbols: FID_conflict:operator+, FID_conflict:operator-,
// operator-.
POINT GeometryAdd(POINT point, SIZE size) {
    return POINT{point.x + size.cx, point.y + size.cy};
}

POINT GeometrySubtract(POINT point, SIZE size) {
    return POINT{point.x - size.cx, point.y - size.cy};
}

SIZE GeometryDifference(POINT left, POINT right) {
    return SIZE{left.x - right.x, left.y - right.y};
}

void Offset(POINT& point, POINT delta) {
    point.x += delta.x;
    point.y += delta.y;
}

void FrameWndOnUpdateContextHelp(MfcFrameWndCompat& frame,
    MfcCmdUICompat& cmd_ui) {
    if (AfxGetMainWndCompat() == &frame) {
        CmdUIEnable(cmd_ui, frame.in_help_mode);
        return;
    }
    cmd_ui.continue_routing = true;
}

void BringToTop(MfcFrameWndCompat& frame, int show_command) {
    if (show_command == SW_HIDE || show_command == SW_MINIMIZE ||
        show_command == SW_SHOWMINNOACTIVE ||
        show_command == SW_SHOWNOACTIVATE || show_command == SW_SHOWNA) {
        return;
    }
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return;
    }
    HWND popup = GetLastActivePopup(frame.window);
    BringWindowToTop(popup == nullptr ? frame.window : popup);
}

void ActivateFrame(MfcFrameWndCompat& frame, int show_command) {
    if (show_command == -1) {
        if (CWndIsWindowVisibleInline(frame) == FALSE) {
            show_command = SW_SHOWNORMAL;
        } else if (CWndIsIconicInline(frame) != FALSE) {
            show_command = SW_RESTORE;
        }
    }
    BringToTop(frame, show_command);
    if (show_command != -1) {
        CWndShowWindow(frame, show_command);
        BringToTop(frame, show_command);
    }
}

int GetPageIndex(MfcPropertySheetCompat& sheet, MfcPropertyPageCompat* page) {
    const int count = PropertySheetGetPageCount(sheet);
    for (int index = 0; index < count; ++index) {
        if (PropertySheetGetPageAt(sheet, index) == page) {
            return index;
        }
    }
    return -1;
}

void OnClose(MfcPropertySheetCompat& sheet) {
    if (sheet.modeless) {
        CWndDestroyWindow(sheet);
        return;
    }
    CWndEndModalLoop(sheet, IDCANCEL);
    if (sheet.window != nullptr && IsWindow(sheet.window)) {
        SendMessageA(sheet.window, PSM_PRESSBUTTON, PSBTN_CANCEL, 0);
    }
}

int Track(MfcRectTrackerCompat& tracker, MfcCWndCompat& window, POINT point,
    BOOL allow_invert, MfcCWndCompat* clip_window) {
    (void)allow_invert;
    const int hit = RectTrackerHitTestHandles(tracker, point);
    if (hit < 0) {
        return FALSE;
    }
    return RectTrackerTrackHandle(tracker, hit, window, point, clip_window)
        ? TRUE
        : FALSE;
}

int TrackRubberBand(MfcRectTrackerCompat& tracker, MfcCWndCompat& window,
    POINT point, BOOL allow_invert) {
    (void)allow_invert;
    SetRect(&tracker.rect, point.x, point.y, point.x, point.y);
    return RectTrackerTrackHandle(tracker, 2, window, point, nullptr)
        ? TRUE
        : FALSE;
}

void CheckListBoxMeasureItemDefault(MfcCheckListBoxCompat& control) {
    if (control.window == nullptr || !IsWindow(control.window)) {
        return;
    }
    const DWORD style = static_cast<DWORD>(
        GetWindowLongA(control.window, GWL_STYLE));
    if ((style & (LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)) !=
        (LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)) {
        AfxTraceOutput("CCheckListBox default MeasureItem requires "
            "owner-draw fixed strings.\n");
    }
}

int FrameWndOnCreateThunk(MfcFrameWndCompat& frame, CREATESTRUCTA* create) {
    auto* context = create == nullptr ? nullptr :
        static_cast<MfcCreateContextCompat*>(create->lpCreateParams);
    return FrameWndOnCreate(frame, create, context);
}

MfcFrameWndCompat* GetParentFrame(MfcCWndCompat& window) {
    for (HWND parent = window.window == nullptr ? nullptr : GetParent(window.window);
         parent != nullptr; parent = GetParent(parent)) {
        MfcCWndCompat* candidate = CWndFromHandlePermanent(parent);
        if (candidate == nullptr || candidate->runtime_class == nullptr) {
            continue;
        }
        const char* name = candidate->runtime_class->class_name;
        if (name != nullptr && std::strstr(name, "Frame") != nullptr) {
            return static_cast<MfcFrameWndCompat*>(candidate);
        }
    }
    return nullptr;
}

MfcCWndCompat* GetSafeOwner(MfcCWndCompat* parent, HWND* disabled_owner) {
    HWND parent_handle = parent == nullptr ? nullptr : parent->window;
    return CWndFromHandle(AfxGetSafeOwnerCompat(parent_handle, disabled_owner));
}

MfcCWndCompat* GetDescendantWindow(HWND parent, int control_id,
    int only_permanent) {
    if (parent == nullptr) {
        return nullptr;
    }
    if (HWND child = GetDlgItem(parent, control_id)) {
        if (MfcCWndCompat* nested =
            GetDescendantWindow(child, control_id, only_permanent)) {
            return nested;
        }
        return only_permanent != 0 ? CWndFromHandlePermanent(child)
            : CWndFromHandle(child);
    }
    for (HWND child = GetTopWindow(parent); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (MfcCWndCompat* nested =
            GetDescendantWindow(child, control_id, only_permanent)) {
            return nested;
        }
    }
    return nullptr;
}

void SendMessageToDescendants(HWND parent, UINT message, WPARAM wparam,
    LPARAM lparam, int deep, int only_permanent) {
    for (HWND child = GetTopWindow(parent); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (only_permanent != 0) {
            if (MfcCWndCompat* wrapper = CWndFromHandlePermanent(child)) {
                AfxCallWndProc(*wrapper, child, message, wparam, lparam);
            }
        } else {
            SendMessageA(child, message, wparam, lparam);
        }
        if (deep != 0 && GetTopWindow(child) != nullptr) {
            SendMessageToDescendants(child, message, wparam, lparam, deep,
                only_permanent);
        }
    }
}

// Ghidra original symbol: FID_conflict:SetScrollPos.
int FID_conflict_SetScrollPos(MfcCWndCompat& window, int bar, int position,
    BOOL redraw) {
    return CWndSetScrollPosCompat(window, bar, position, redraw);
}

BOOL FID_conflict_SetScrollPos(MfcCWndCompat& window, int bar,
    int* min_position, int* max_position) {
    CWndGetScrollRangeCompat(window, bar, min_position, max_position);
    return window.window != nullptr ? TRUE : FALSE;
}

int SendChildNotifyLastMsg(MfcCWndCompat& window, LRESULT* result) {
    return CWndSendChildNotifyLastMsgByHandle(window.window, result) ? TRUE : FALSE;
}

// Ghidra original symbol: FID_conflict:OnHScroll.
void FID_conflict_OnHScroll(MfcCWndCompat& window, UINT, UINT,
    MfcScrollBarCompat* scroll_bar) {
    if (scroll_bar == nullptr ||
        !CWndSendChildNotifyLastMsgByHandle(scroll_bar->window, nullptr)) {
        Default(window);
    }
}

void FID_conflict_OnHScroll(MfcCWndCompat& window, UINT, UINT,
    MfcCWndCompat* scroll_bar) {
    if (scroll_bar == nullptr ||
        !CWndSendChildNotifyLastMsgByHandle(scroll_bar->window, nullptr)) {
        Default(window);
    }
}

void OnEnterIdle(MfcCWndCompat& window, UINT, MfcCWndCompat*) {
    MSG message{};
    while (PeekMessageA(&message, nullptr, WM_ENTERIDLE, WM_ENTERIDLE,
        PM_REMOVE) != FALSE) {
        DispatchMessageA(&message);
    }
    Default(window);
}
#endif

void InitializeLegacyAsyncTcpSocketBase(LegacyAsyncTcpSocket& socket_state) {
    InitializeLegacyAsyncTcpSocket(socket_state);
}

void DestroyLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state) {
    DeleteLegacyAsyncTcpSocket(socket_state);
}

#ifdef _WIN32
void MemoStaticResourceWrapper00() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper01() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper02() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper03() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper04() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper05() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper06() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper07() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper08() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper09() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper10() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper11() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper12() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper13() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper14() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper15() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper16() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper17() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper18() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper19() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper20() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper21() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper22() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper23() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper24() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper25() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper26() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper27() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper28() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper29() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper30() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper31() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper32() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper33() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper34() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper35() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper36() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper37() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper38() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper39() { MemoStaticResourceWrapperNN(); }
#endif

int CrtFcloseUnlocked(FILE* stream) {
    return stream != nullptr ? std::fclose(stream) : EOF;
}

void LockCrtExit() {
    LockCrtRuntime(0x0d);
}

void UnlockCrtExit() {
    UnlockCrtRuntime(0x0d);
}

void* CrtMallocWithHeapContext(std::size_t size) {
    return CrtMallocRetry(size);
}

void UnlockCrtHeapAfterMalloc() {
    UnlockCrtRuntime(9);
}
void FinishCrtMallocRetry() {}

void* CrtDebugAllocBlock(std::size_t size) {
    return CrtDebugHeapAlloc(size);
}

void UnlockCrtHeapAfterExpand() {
    UnlockCrtRuntime(9);
}
void FinishCrtExpand() {}

void* CrtDebugHeapReallocOrExpand(void* memory, std::size_t new_size,
    bool allow_move) {
    return CrtReallocOrExpand(memory, new_size, allow_move);
}

void UnlockCrtHeapAfterRealloc() {
    UnlockCrtRuntime(9);
}
void FinishCrtRealloc() {}

void CrtDebugFreeNormal(void* memory) {
    CrtDebugHeapFree(memory);
}

void UnlockCrtHeapAfterFree() {
    UnlockCrtRuntime(9);
}
void FinishCrtFree() {}
void UnlockCrtHeapAfterMemorySize() {
    UnlockCrtRuntime(9);
}
void FinishCrtMemorySize() {}

long __CrtSetBreakAlloc(long allocation) {
    return CrtSetBreakAlloc(allocation);
}

void UnlockCrtHeapAfterSetBlockType() {
    UnlockCrtRuntime(9);
}
void FinishCrtSetBlockType() {}

void* __CrtSetAllocHook(void* hook) {
    return reinterpret_cast<void*>(CrtSetAllocHook(
        reinterpret_cast<CrtAllocHookCallback>(hook)));
}

void UnlockCrtHeapAfterCheckMemory() {
    UnlockCrtRuntime(9);
}
void FinishCrtCheckMemory() {}
void UnlockCrtHeapAfterClientObjects() {
    UnlockCrtRuntime(9);
}
void FinishCrtClientObjects() {}
void UnlockCrtHeapAfterIsMemoryBlock() {
    UnlockCrtRuntime(9);
}
void FinishCrtIsMemoryBlock() {}
void UnlockCrtHeapAfterMemCheckpoint() {
    UnlockCrtRuntime(9);
}
void FinishCrtMemCheckpoint() {}
void UnlockCrtHeapAfterDumpObjects() {
    UnlockCrtRuntime(9);
}
void FinishCrtDumpObjects() {
    if (CrtDbgReport(0, nullptr, 0, nullptr, "Object dump complete.\n") == 1) {
        CrtDebugBreak();
    }
}

void NoopCrtFloatingPointInit() {}

double CrtSinEntry(double value) { return CrtSin(value); }
double CrtSinFloadFallback(double value) { return CrtSin(value); }
double CrtSinCore(double value) { return CrtSin(value); }
double CrtCosEntry(double value) { return CrtCos(value); }
double CrtCosFloadFallback(double value) { return CrtCos(value); }
double CrtCosCore(double value) { return CrtCos(value); }
double CrtAtanEntry(double value) { return CrtAtan(value); }
double CrtAtanFloadFallback(double value) { return CrtAtan(value); }
double CrtAtanCore(double value) { return CrtAtan(value); }

int HandleCrtRenamePath(const char* old_path, const char* new_path) {
#ifdef _WIN32
    const BOOL moved = MoveFileA(old_path, new_path);
    const DWORD error = moved != FALSE ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_SUCCESS) {
        return 0;
    }
    __dosmaperr(error);
    return -1;
#else
    return std::rename(old_path, new_path);
#endif
}

void* __CrtSetReportFile(int report_type, void* file) {
    return CrtSetReportFile(report_type, file);
}

void CheckStackPointerAlias() {}
// Ghidra original symbol: __chkesp.

void store_dt(std::string& out, const std::tm& value) {
    FormatLocaleDateTimePattern("%x", value, out, LocaleTimeData{});
}

// Ghidra original symbol: FID_conflict:__expand.
void* FID_conflict___expand(void* memory, std::size_t new_size) {
    return CrtExpand(memory, new_size);
}

// Ghidra original symbol: FID_conflict:__realloc_dbg.
void* FID_conflict___realloc_dbg(void* memory, std::size_t size,
    int block_type = 1, const char* file_name = nullptr,
    int line_number = 0) {
    (void)block_type;
    (void)file_name;
    (void)line_number;
    return __realloc_dbg(memory, size);
}

// Ghidra original symbol: FID_conflict:__CrtSetDumpClient.
void* FID_conflict___CrtSetDumpClient(void* client) {
    return reinterpret_cast<void*>(CrtSetDumpClient(
        reinterpret_cast<CrtDumpClientCallback>(client)));
}

// Ghidra emitted the report-hook setter under the same FID-conflict name at 005260a0.
void* FID_conflict___CrtSetReportHook(void* hook) {
    return reinterpret_cast<void*>(CrtSetReportHook(
        reinterpret_cast<CrtReportHookCallback>(hook)));
}

// Ghidra original symbol: `eh_vector_constructor_iterator'.
void eh_vector_constructor_iterator(void* first, std::size_t element_size,
    int element_count, EhObjectCallback constructor,
    EhObjectCallback destructor) {
    EhVectorConstructorIterator(
        first, element_size, element_count, constructor, destructor);
}

// Ghidra original symbol: FID_conflict:___CxxFrameHandler3.
int FID_conflict____CxxFrameHandler3(void* exception_record,
    void* registration, void* context, void* dispatcher_context) {
    return CxxFrameHandler(exception_record, registration, context,
        dispatcher_context, nullptr, 0, nullptr, false);
}

// Ghidra original symbol: ___CxxLongjmpUnwind@4.
void CxxLongjmpUnwindOriginal(void*) {}

// Ghidra original symbol: FID_conflict:_remove.
int FID_conflict__remove(const char* path) {
    return CrtRemovePath(path);
}

// Ghidra original symbol: FID_conflict:__utime.
int FID_conflict___utime(const char* path, const void* times) {
#ifdef _WIN32
    return path == nullptr ? -1
        : _utime(path, const_cast<_utimbuf*>(
              static_cast<const _utimbuf*>(times)));
#else
    (void)path;
    (void)times;
    return 0;
#endif
}

// Ghidra original symbol: __seh_longjmp_unwind@4.
void SehLongjmpUnwindOriginal(void*) {}

// Ghidra original symbol: type_info::operator!=.
bool type_info_operator_not_equal(const void* lhs, const void* rhs) {
    return lhs != rhs;
}

// Ghidra original symbol: __CxxThrowException@8.
void CxxThrowExceptionOriginal(void*, void*) {
    std::abort();
}

void CrtEndThreadExAlias(unsigned exit_code) {
    // Ghidra original symbol: __endthreadex.
#ifdef _WIN32
    AfxEndThreadCompat(exit_code, true);
#else
    (void)exit_code;
#endif
}

// Ghidra original symbol: FID_conflict:__ctime64.
char* FID_conflict___ctime64(const long long* value) {
    if (value == nullptr) {
        return nullptr;
    }
    const std::time_t converted = static_cast<std::time_t>(*value);
    return std::ctime(&converted);
}

// Ghidra original symbol: FID_conflict:_store_str.
void FID_conflict__store_str(std::string& out, const char* text) {
    if (text != nullptr) {
        out += text;
    }
}

// Ghidra original symbol: FID_conflict:_store_number.
void FID_conflict__store_number(std::string& out, int value,
    unsigned width, bool suppress_padding) {
    StoreStrftimePaddedNumber(value, width, out, suppress_padding);
}

// Ghidra original symbol: FID_conflict:__set_errno_from_matherr.
int FID_conflict___set_errno_from_matherr(int math_type) {
    int* err = CrtErrnoPointer();
    if (math_type == 1) {
        *err = EDOM;
    } else if (math_type > 1 && math_type < 4) {
        *err = ERANGE;
    }
    return *err;
}

// Ghidra original symbol: FID_conflict:___AdjustPointer.
void* FID_conflict____AdjustPointer(void* object, long displacement) {
    return object == nullptr ? nullptr
        : static_cast<void*>(static_cast<char*>(object) + displacement);
}

// Ghidra original symbol: __CallSettingFrame@12.
void CallSettingFrameOriginal(void*, void*, void*) {}

// Ghidra original symbol: FID_conflict:__open.
int FID_conflict___open(const char* path, int open_flags,
    int permission = 0) {
    return CrtOpenFileDescriptor(path, static_cast<unsigned>(open_flags), 0,
        static_cast<unsigned>(permission));
}

// Ghidra original symbol: FID_conflict:__getenv_lk.
char* FID_conflict___getenv_lk(const char* name) {
    return __getenv_lk(name);
}

#ifdef _WIN32
void AfxCheckError(HRESULT result) {
    if (FAILED(result)) {
        if (result == E_OUTOFMEMORY) {
            ThrowMfcMemoryException();
        }
        MfcOleRuntime_005f4b9a(result);
    }
}
#else
void AfxCheckError(long result) {
    if (result < 0) {
        if (result == static_cast<long>(
                static_cast<std::int32_t>(0x8007000eUL))) {
            ThrowMfcMemoryException();
        }
        MfcOleRuntime_005f4b9a(static_cast<SCODE>(result));
    }
}
#endif

namespace {

using AfxAllocHookCallback = int (*)(unsigned size, int client_block,
    long request_number);

struct AfxDoForAllObjectsProxyContext {
    CrtClientObjectCallback callback = nullptr;
    void* context = nullptr;
};

AfxAllocHookCallback g_afx_alloc_hook = nullptr;
CrtAllocHookCallback g_previous_crt_alloc_hook = nullptr;
bool g_afx_alloc_hook_installed = false;
thread_local AfxExceptionContextCompat g_afx_exception_context;

int AfxAllocHookBridgeCompat(int alloc_type, void* user_data,
    std::size_t size, int block_type, long request_number,
    const char* file_name, int line_number) {
    if (alloc_type == 1 && g_afx_alloc_hook != nullptr &&
        g_afx_alloc_hook(static_cast<unsigned>(size),
            (block_type & 0xffff) == 4, request_number) == 0) {
        return 0;
    }
    if (g_previous_crt_alloc_hook != nullptr) {
        return g_previous_crt_alloc_hook(alloc_type, user_data, size,
            block_type, request_number, file_name, line_number);
    }
    return 1;
}

} // namespace

AfxExceptionContextCompat& AfxExceptionContextCompatState() {
    return g_afx_exception_context;
}

void* AfxSetAllocHook(void* hook) {
    if (!g_afx_alloc_hook_installed) {
        g_previous_crt_alloc_hook =
            CrtSetAllocHook(AfxAllocHookBridgeCompat);
        g_afx_alloc_hook_installed = true;
    }
    AfxAllocHookCallback previous = g_afx_alloc_hook;
    g_afx_alloc_hook = reinterpret_cast<AfxAllocHookCallback>(hook);
    return reinterpret_cast<void*>(previous);
}

void AfxSetAllocStop(long allocation) {
    __CrtSetBreakAlloc(allocation);
}

bool AfxIsMemoryBlock(const void* memory, std::size_t size,
    long* request_number) {
    return __CrtIsMemoryBlock(memory, size, request_number, nullptr, nullptr);
}

bool AfxIsMemoryBlock(const void* memory, std::size_t size) {
    return AfxIsMemoryBlock(memory, size, nullptr);
}

void* CMemoryState(void* state) {
    if (state != nullptr) {
        std::memset(state, 0, 100);
    }
    return state;
}

void _AfxDoForAllObjectsProxy(void* memory, void* context) {
    auto* proxy = static_cast<AfxDoForAllObjectsProxyContext*>(context);
    if (proxy != nullptr && proxy->callback != nullptr) {
        proxy->callback(memory, proxy->context);
    }
}

void AfxThrowLastCleanup() {
    AfxExceptionContextCompat& context = AfxExceptionContextCompatState();
    if (context.current != nullptr) {
        context.current->exception = nullptr;
    }
}

void AfxThrowResourceException() {
    ThrowMfcResourceException();
}

void* AfxGetExceptionContext() {
    return &AfxExceptionContextCompatState();
}

MfcRuntimeClassCompat* AfxClassInit(MfcRuntimeClassCompat* runtime_class) {
    return AfxClassInitObject(runtime_class);
}

int _AfxHandleSetCursor(MfcCWndCompat* window, UINT hit_test, UINT message) {
    return AfxHandleSetCursor(window, hit_test, message);
}

MfcMenuCompat* _AfxFindPopupMenuFromID(MfcMenuCompat* menu, UINT command_id) {
    return AfxFindPopupMenuFromID(menu, command_id);
}

void AfxLockTempMaps() {
    AfxLockTempMapsCompat();
}

#ifdef _WIN32
INT_PTR CALLBACK AfxDlgProc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return AfxDlgProcCompat(hwnd, message, wparam, lparam);
}
#endif

int AfxMessageBox(const char* prompt, UINT type, UINT id_help) {
    return AfxMessageBoxCompat(prompt, type, id_help);
}

void _AfxAdjustRectangle(RECT& rect, POINT point) {
    AfxAdjustRectangle(rect, point);
}

bool AfxAssertFailedLine(const char* file, int line) {
#ifdef _WIN32
    MSG quit_message{};
    const BOOL had_quit = PeekMessageA(&quit_message, nullptr, WM_QUIT,
        WM_QUIT, PM_REMOVE);
#endif
    const int result = CrtDbgReport(2, file, line, nullptr, nullptr);
#ifdef _WIN32
    if (had_quit != FALSE) {
        PostQuitMessage(static_cast<int>(quit_message.wParam));
    }
#endif
    return result != 0;
}

BOOL AfxExtCDCVirtualCall5CInline(MfcCDCCompat& dc, int x, int y) {
    if (dc.output_dc == nullptr && AfxAssertFailedLine("afxext.inl", 0x30)) {
        CrtDebugBreak();
        return FALSE;
    }
    return CDCPtVisible(dc, x, y);
}

void AfxExtCDCVirtualCall5CInline() {}
void* AfxExtIdentityInline005d50d4(void* value) { return value; }
void* AfxExtIdentityInline005d510d(void* value) { return value; }
void AfxExtAssertUnsupportedLine60() {
    if (AfxAssertFailedLine("afxext.inl", 0x60)) {
        CrtDebugBreak();
    }
}
void* AfxExtIdentityInline005d51c6(void* value) { return value; }
void AfxExtAssertUnsupportedLine69() {
    if (AfxAssertFailedLine("afxext.inl", 0x69)) {
        CrtDebugBreak();
    }
}

MfcWinAppCompat* AfxGetApp() {
    return AfxGetAppCompat();
}

MfcCWndCompat* AfxGetMainWnd() {
    return AfxGetMainWndCompat();
}

#ifdef _WIN32
void AfxTermLocalData(HINSTANCE instance, int process_terminating) {
    AfxTermLocalDataCompat(instance, process_terminating != 0);
}
#else
void AfxTermLocalData(void*, int) {}
#endif

#ifdef _WIN32
bool AfxWinInitCompat(HINSTANCE instance, HINSTANCE previous_instance,
    LPSTR command_line, int command_show) {
    if (previous_instance != nullptr &&
        AfxAssertFailedLine("appinit.cpp", 0x1b)) {
        CrtDebugBreak();
    }
    const UINT old_error_mode = SetErrorMode(0);
    SetErrorMode(old_error_mode | SEM_FAILCRITICALERRORS |
        SEM_NOOPENFILEERRORBOX);

    if (instance == nullptr) {
        instance = GetModuleHandleA(nullptr);
    }
    AfxSetResourceHandleCompat(instance);

    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr) {
        app->instance = instance;
        app->previous_instance = previous_instance;
        app->command_line = command_line == nullptr ? "" : command_line;
        app->command_show = command_show;
        WinAppSetCurrentHandles(*app);
        AfxWinInitThread(*app);
    }
    return true;
}
#endif

std::string AfxGetFileNameCompat(MfcFileCompat& file) {
    return FileGetFileName(file);
}

int AfxGetFileNameCompat(const char* path, char* output, int output_chars) {
    if (path == nullptr) {
        if (AfxAssertFailedLine("appinit.cpp", 0x8d)) {
            CrtDebugBreak();
        }
        if (output != nullptr && output_chars > 0) {
            output[0] = '\0';
        }
        return 0;
    }

    const char* file_name = path;
    for (const char* cursor = path; *cursor != '\0';
         cursor = AdvanceMbcsStringPointer(cursor)) {
        if (*cursor == '\\' || *cursor == '/' || *cursor == ':') {
            file_name = AdvanceMbcsStringPointer(cursor);
        }
    }

    if (output == nullptr) {
#ifdef _WIN32
        return lstrlenA(file_name) + 1;
#else
        return static_cast<int>(std::strlen(file_name) + 1);
#endif
    }

    if (output_chars <= 0) {
        return 0;
    }
#ifdef _WIN32
    lstrcpynA(output, file_name, output_chars);
#else
    std::strncpy(output, file_name, static_cast<std::size_t>(output_chars));
    output[output_chars - 1] = '\0';
#endif
    return 0;
}

#ifdef _WIN32
void AfxPostQuitMessage(int exit_code) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->ole_term_or_free_lib != nullptr) {
        thread->ole_term_or_free_lib(TRUE, TRUE);
    }
    PostQuitMessage(exit_code);
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
