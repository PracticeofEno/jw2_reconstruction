#include "ranker_production_orders.h"
#include "ranker_gameplay_visibility.h"
#include "ranker_owner_ai.h"
#include "ranker_unit_action.h"
#include "ranker_unit_commands.h"
#include "ranker_unit_damage.h"
#include "ranker_unit_lifecycle.h"
#include "ranker_unit_spatial_index.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace ranker;

constexpr i32 kStartX = 650;
constexpr i32 kStartY = 650;
constexpr i32 kBaseDeltaX = 2;
constexpr i32 kBaseDeltaY = -3;
constexpr i32 kAdditionalModifier = 4;

UnitMovementUnit* g_guard_target = nullptr;
UnitMovementDefinition g_placed_definition{};
UnitMovementDefinition g_legacy_spawn_definition{};
u32 g_area_direct_damage_calls = 0;
u32 g_area_nearby_damage_calls = 0;
u32 g_attack_travel_range_calls = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "MOVEMENT_MODIFIER_COMMAND_FLAG_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool allow_enter(UnitMovementContext&, const UnitMovementUnit&, i32, i32) {
    return true;
}

bool allow_only_top_edge_fallback(UnitMovementContext&,
    const UnitMovementUnit&, i32 x, i32 y) {
    return (x >> 5) == 125 && (y >> 5) == 0;
}

UnitMovementUnit* find_same_guard_target(UnitCommandContext&,
    UnitMovementUnit&) {
    return g_guard_target;
}

bool allow_guard_attack(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return true;
}

bool reject_guard_attack(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return false;
}

bool reject_guard_range(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return false;
}

bool accept_guard_range(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return true;
}

bool accept_attack_travel_range_after_completed_step(UnitCommandContext&,
    UnitMovementUnit&, UnitMovementUnit&) {
    return ++g_attack_travel_range_calls >= 2;
}

UnitActionTargetValidation valid_out_of_range_action_target(UnitActionContext&,
    const UnitMovementUnit&, const UnitMovementUnit&) {
    UnitActionTargetValidation validation{};
    validation.valid = true;
    validation.in_range = false;
    validation.distance = 0x1234;
    return validation;
}

void ignore_attack_dispatch(UnitCommandContext&, UnitMovementUnit&) {}

const UnitMovementDefinition* find_placed_definition(
    UnitLifecycleContext&, u32) {
    return &g_placed_definition;
}

const UnitMovementDefinition* find_legacy_spawn_definition(
    UnitCommandContext&, u32) {
    return &g_legacy_spawn_definition;
}

bool accept_placed_position(UnitLifecycleContext&, UnitMovementUnit&,
    i32&, i32&) {
    return true;
}

u32 zero_placed_random(UnitLifecycleContext&, u32) {
    return 0;
}

u32 fixed_impact_damage(UnitEffectRuntimeState&, const UnitEffectRuntime&,
    UnitMovementUnit* source, UnitMovementUnit& target) {
    require(source != nullptr && source->id == 0x1d0u && target.id == 0x3a0u,
        "low-id impact damage did not use the live source/target pair");
    return 37;
}

u32 live_area_impact_damage(UnitEffectRuntimeState&, const UnitEffectRuntime&,
    UnitMovementUnit* source, UnitMovementUnit& target) {
    require(source != nullptr && source->id == 0x1d0u,
        "low-id area impact lost the live source unit");
    if (target.id == 0x3a0u) {
        ++g_area_direct_damage_calls;
        return 37;
    }
    require(target.id == 0x570u,
        "low-id area impact evaluated an ineligible candidate");
    ++g_area_nearby_damage_calls;
    return 40;
}

UnitMovementUnit make_unit(u32 owner_id, u32 command_flags, u32 runtime_flags) {
    UnitMovementUnit unit{};
    unit.owner_id = owner_id;
    unit.type_id = 0;
    unit.command_flags = command_flags;
    unit.runtime_flags = runtime_flags;
    unit.command_state = kUnitStateTransportDockSearch;
    unit.x = kStartX;
    unit.y = kStartY;
    unit.next_path_x = 1000;
    unit.next_path_y = 0;
    unit.path_target_x = unit.next_path_x;
    unit.path_target_y = unit.next_path_y;
    unit.direction = 1;
    unit.animation_frame = 0;
    unit.definition.movement_class = 2;
    unit.definition.animation_frame_count = 1;
    unit.definition.animation_timer_period = 1;
    unit.definition.bounds_width = 1;
    unit.definition.bounds_height = 1;
    unit.definition.frame_delta_by_direction[1][0] =
        UnitMovementPoint{kBaseDeltaX, kBaseDeltaY};
    return unit;
}

UnitMovementContext make_movement_context(
    const ProductionOrderRuntimeState& production_state) {
    UnitMovementContext context{};
    context.map.width = 64;
    context.map.height = 64;
    context.map.stride_tiles = 64;
    context.callbacks.can_enter_cell = allow_enter;
    context.production_state = &production_state;
    context.additional_movement_modifier = kAdditionalModifier;
    return context;
}

void require_position(const UnitMovementUnit& unit, i32 expected_x, i32 expected_y,
    const char* message) {
    require(unit.x == expected_x && unit.y == expected_y, message);
}

void check_all_unit_spatial_index_double_sort() {
    std::array<UnitMovementUnit, 8> units{};
    std::vector<UnitMovementUnit*> active_units;
    active_units.reserve(units.size());
    for (u32 i = 0; i < units.size(); ++i) {
        units[i].id = i;
        units[i].active = true;
        units[i].x = 77;
        units[i].command_state = 2;
        units[i].definition.movement_class = 0;
        active_units.push_back(&units[i]);
    }

    UnitSpatialIndex index{};
    InitializeUnitSpatialIndex(index, 16);
    RebuildUnitSpatialIndex(index, active_units,
        UnitSpatialIndexBuildMode::all_active_units, nullptr);
    require(index.sorted_units.size() == units.size(),
        "all-unit spatial index lost active units");
    for (u32 i = 0; i < units.size(); ++i) {
        require(index.sorted_units[i].unit == &units[i],
            "all-unit spatial index did not apply the original double sort");
    }

    const std::vector<u32> command_categories(3, 0);
    RebuildUnitSpatialIndex(index, active_units,
        UnitSpatialIndexBuildMode::non_structure_non_terminal_command_units,
        &command_categories);
    constexpr std::array<u32, 8> kSingleSortOrder{5, 4, 7, 6, 1, 0, 3, 2};
    for (u32 i = 0; i < kSingleSortOrder.size(); ++i) {
        require(index.sorted_units[i].unit == &units[kSingleSortOrder[i]],
            "non-all spatial index unexpectedly used the mode-0 double sort");
    }
}

void check_full_action_visibility_requires_owner_layer() {
    PlayerSlotRuntimeState players{};
    GameplayVisibilityGrid grid{};
    grid.width = 1;
    grid.height = 1;
    grid.current.assign(1, 0);
    grid.owner.assign(1, 1u << 2);

    GameplayVisibilityUnit target{};
    target.owner_id = 8;
    target.visibility_probe_x = 0;
    target.visibility_probe_y = 0;
    target.owner_layer_probe_x = 0;
    target.owner_layer_probe_y = 0;

    require(CheckUnitFullActionTargetVisibility(players, grid, target, 2),
        "ordinary action target lost a present owner-layer bit");

    // The paired map-23 divergence exposed raw owner cell 0x01000100: it has
    // no owner-2 bit and must invalidate the recovery target even though the
    // target remains active and class-compatible.
    grid.owner[0] = 0x01000100u;
    require(!CheckUnitFullActionTargetVisibility(players, grid, target, 2),
        "action target without the source owner-layer bit remained valid");

    grid.owner[0] = 1u << 2;
    target.command_flags = 0x40u;
    require(!CheckUnitFullActionTargetVisibility(players, grid, target, 2),
        "special action target bypassed its current-visibility gate");
    grid.current[0] = 1u << (2 + kGameplayVisibilityCurrentOwnerShift);
    require(CheckUnitFullActionTargetVisibility(players, grid, target, 2),
        "special action target rejected a present current-visibility bit");
}

void check_queued_primary_count_uses_raw_command_value() {
    UnitMovementUnit producer{};
    producer.active = true;
    producer.owner_id = 2;
    producer.type_id = 77;
    producer.command_state = kUnitStateCommand10;
    producer.command_value = 45;
    producer.queued_production_type_id = 37;

    UnitMovementContext movement{};
    movement.active_units = {&producer};
    UnitCommandContext commands{};
    commands.movement = &movement;
    std::array<u32, kOwnerUnitTypeCountSlots> producer_types{};
    producer_types.fill(kInvalidOwnerTransportQueueSlot);
    producer_types[37] = producer.type_id;
    producer_types[45] = producer.type_id;

    require(CountOwnerQueuedPrimaryProductionUnits(
                commands, producer.owner_id, 45, producer_types) == 1,
        "queued primary count ignored raw +0x68 command value");
    require(CountOwnerQueuedPrimaryProductionUnits(
                commands, producer.owner_id, 37, producer_types) == 0,
        "queued primary count used detached stale type cache");
}

void check_placed_unit_clears_raw_identity_flags() {
    g_placed_definition = UnitMovementDefinition{};
    g_placed_definition.lifecycle_class = 0;
    g_placed_definition.movement_class = 2;
    g_placed_definition.initial_max_health = 210;

    UnitLifecycleContext lifecycle{};
    lifecycle.callbacks.find_definition = find_placed_definition;
    lifecycle.callbacks.find_placement = accept_placed_position;
    lifecycle.callbacks.random_limit = zero_placed_random;

    UnitMovementUnit unit{};
    unit.scenario_string_slot = 91;
    unit.area_marker_flags = 0x8000001fu;
    require(InitializePlacedUnitFromMapSlot(
                lifecycle, unit, 17, 2, 1425, 3117),
        "placed-unit fixed-pool activation failed");
    require(unit.scenario_string_slot == 0,
        "placed unit retained raw +0x08 scenario string slot");
    require(unit.area_marker_flags == 0,
        "placed unit retained raw +0x0c lifecycle target-class bit");
}

void check_legacy_spawn_approach_uses_live_path_target() {
    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.callbacks.find_definition = find_legacy_spawn_definition;

    g_legacy_spawn_definition = UnitMovementDefinition{};
    g_legacy_spawn_definition.production_resource_cost = 1;
    g_legacy_spawn_definition.interaction_bounds_width = 512;
    g_legacy_spawn_definition.interaction_bounds_height = 512;

    UnitMovementUnit worker{};
    worker.owner_id = 2;
    worker.command_state = kUnitStateLegacySpawnPlacementApproach;
    worker.command_value = 0x96;
    worker.spawn_type_id = 0x96;
    worker.x = 100;
    worker.y = 100;
    worker.current_cell_x = 96;
    worker.current_cell_y = 96;
    worker.path_target_x = 128;
    worker.path_target_y = 100;
    worker.next_path_x = worker.path_target_x;
    worker.next_path_y = worker.path_target_y;
    worker.direction = 1;
    worker.animation_frame = 0;
    worker.definition.movement_class = 2;
    worker.definition.animation_frame_count = 2;
    worker.definition.frame_delta_by_direction[1][1] = {5, 0};
    // Deliberately unrelated to the centered path target retained by raw
    // +0x6c/+0x70. Recomputing from this tuple leaves state 0x25 active.
    worker.active_command_payload = {6, 54, 1000, 1000};

    HandleUnitLegacySpawnPlacementApproach(commands, worker);
    require(worker.x == 105 && worker.y == 100,
        "legacy placement approach did not move before its distance test");
    require(worker.command_state == kUnitStateRuntimeIdleAcquire &&
            worker.active_command_payload.state == 0,
        "legacy placement approach ignored live raw +0x6c/+0x70 path target");
    require(commands.owner_resources[worker.owner_id] == 0,
        "failed legacy placement unexpectedly changed owner resources");
}

void check_common_movement(UnitMovementContext& movement, u32 owner_id,
    u32 command_flags, u32 runtime_flags, i32 expected_x, i32 expected_y,
    const char* message) {
    UnitMovementUnit unit = make_unit(owner_id, command_flags, runtime_flags);
    require(ProcessUnitMovementStep(movement, unit), message);
    require_position(unit, expected_x, expected_y, message);
}

void check_transport_dock(UnitMovementContext& movement,
    const ProductionOrderRuntimeState& production_state, u32 owner_id,
    u32 command_flags, u32 runtime_flags, i32 expected_x, i32 expected_y,
    const char* message) {
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.production_state = &production_state;

    UnitMovementUnit unit = make_unit(owner_id, command_flags, runtime_flags);
    ProcessTransportDockSearch(commands, unit);
    require_position(unit, expected_x, expected_y, message);
}

void check_draw_feedback_is_independent_from_raw_command_bits() {
    UnitCommandContext context{};
    context.frame_counter = 0x10;

    UnitMovementUnit feedback{};
    feedback.draw_flags = 0x85u;
    feedback.command_flags = 0x40u;
    HandleUnitPassiveRecoveryAndTimedRemoval(context, feedback);
    require(feedback.draw_flags == 0x85u &&
            (feedback.command_flags & 0x40u) != 0,
        "raw +0xa4 red feedback was consumed as raw +0x5c command bit 0x80");

    UnitMovementUnit timed{};
    timed.draw_flags = 0x85u;
    timed.command_bits[0] = 0x80u;
    timed.command_flags = 0x40u;
    HandleUnitPassiveRecoveryAndTimedRemoval(context, timed);
    require(timed.draw_flags == 0x85u &&
            (timed.command_bits[0] & 0x80u) == 0 &&
            (timed.command_flags & 0x40u) == 0,
        "raw +0x5c timed bit did not clear independently from draw feedback");
}

void check_nearest_pathable_goal_uses_unsigned_edge_containment() {
    UnitMovementContext movement{};
    movement.map.width = 128;
    movement.map.height = 128;
    movement.map.stride_tiles = 128;
    movement.callbacks.can_enter_cell = allow_only_top_edge_fallback;

    UnitMovementUnit unit{};
    unit.definition.movement_class = 0;
    UnitMovementPoint resolved{};
    require(FindNearestPathableGoalTile(movement, unit,
                UnitMovementPoint{124, 1}, UnitMovementPoint{123, 0}, resolved),
        "top-edge nearest-goal search stopped at signed min_y containment");
    require(resolved.x == 125 && resolved.y == 0,
        "top-edge nearest-goal search did not select the original expanded-ring fallback");
}

void check_action_recovery_uses_growth_variant_not_cargo() {
    ProductionOrderRuntimeState production{};
    UnitMovementUnit unit{};
    unit.type_id = 33;
    unit.owner_id = 2;
    unit.production_variant = 2;
    unit.cargo_amount = 9;
    unit.definition.action_recovery_base_ticks = 14;
    unit.definition.action_recovery_scale_percent = 5;
    production.completion_effect_totals[kProductionEffectSlotUnitScaledValue]
        [unit.owner_id][unit.type_id] = 1;

    // Original 0x0040a830: floor(14 * raw[+0x54](2) * 5 / 100) + 1 = 2.
    // Recovery is therefore 14 - 2 = 12; raw +0x4c cargo is irrelevant.
    require(CalculateUnitActionRecoveryTicksWithProductionAndEquipmentEffects(
                production, unit, nullptr) == 12,
        "action recovery used raw +0x4c cargo instead of +0x54 growth variant");
}

void check_guard_pursue_same_target_advances_movement() {
    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;
    commands.callbacks.target_in_action_range = reject_guard_range;

    UnitMovementUnit target{};
    target.id = 0x2b60;
    target.x = 1000;
    target.y = 0;
    target.active = true;

    UnitMovementUnit unit = make_unit(2, 0x28u, 0x1u);
    unit.command_state = kUnitStateGuardPursueTarget;
    unit.definition.animation_frame_count = 8;
    unit.definition.frame_delta_by_direction[1][1] =
        UnitMovementPoint{kBaseDeltaX, kBaseDeltaY};
    unit.target = &target;
    unit.command_value = target.id;
    g_guard_target = &target;

    HandleUnitGuardPursueTarget(commands, unit);

    require(unit.target == &target &&
            unit.command_state == kUnitStateGuardPursueTarget,
        "same guard target changed the target or command state");
    require_position(unit, kStartX + kBaseDeltaX, kStartY + kBaseDeltaY,
        "same guard target rebuilt the path instead of moving this tick");
    require(unit.animation_frame == 1,
        "same guard target skipped the movement animation advance");
    g_guard_target = nullptr;
}

void check_action_geometry_gate_uses_raw_14c_for_reach_and_retarget() {
    UnitActionDamageProfile profile{};
    profile.render_class2_terrain_gate = 1;
    require(UnitActionProfileUsesCenterDistanceReach(profile),
        "raw +0x14c did not select center-distance reach");
    require(UnitActionProfileAllowsTransientTargetReplacement(profile),
        "raw +0x14c did not allow immediate transient-target replacement");

    profile.render_class2_terrain_gate = 0;
    require(!UnitActionProfileUsesCenterDistanceReach(profile),
        "zero raw +0x14c did not select footprint reach");
    require(!UnitActionProfileAllowsTransientTargetReplacement(profile),
        "zero raw +0x14c incorrectly allowed transient-target replacement");
}

void check_guard_pursue_range_transition_refreshes_target_path() {
    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;
    commands.callbacks.target_in_action_range = accept_guard_range;

    UnitMovementUnit target{};
    target.id = 0x2d30;
    target.x = 3200;
    target.y = 2912;
    target.active = true;

    UnitMovementUnit unit = make_unit(2, 0x28u, 0x1u);
    unit.command_state = kUnitStateGuardPursueTarget;
    unit.animation_frame = 3;
    unit.path_target_x = 3216;
    unit.path_target_y = 2896;
    unit.target = &target;
    unit.command_value = target.id;
    g_guard_target = &target;

    HandleUnitGuardPursueTarget(commands, unit);

    require(unit.command_state == kUnitStateGuardCombatCycle &&
            unit.animation_frame == 0 && (unit.command_flags & 8u) == 0,
        "in-range guard target did not enter the combat cycle");
    require(unit.path_target_x == target.x && unit.path_target_y == target.y,
        "guard combat transition retained the prior pursue path cell");
    g_guard_target = nullptr;
}

void check_guard_pursue_accepts_equal_priority_replacement() {
    const ProductionOrderRuntimeState production_state{};
    UnitCommandContext commands{};
    // The replacement branch commits the scanned target position before any
    // movement step.  A null movement context keeps this regression focused
    // on the state-0x22 equal-priority comparison proven at 0x004c9da0.
    commands.movement = nullptr;
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;
    commands.callbacks.target_in_action_range = reject_guard_range;

    UnitMovementUnit current{};
    current.id = 0x2f00u;
    current.x = 1000;
    current.y = 650;
    current.active = true;
    current.definition.target_selection_priority = 7;

    UnitMovementUnit replacement{};
    replacement.id = 0x30d0u;
    replacement.x = 1200;
    replacement.y = 650;
    replacement.active = true;
    replacement.definition.target_selection_priority = 7;

    UnitMovementUnit unit = make_unit(2, 0x20u, 0x1u);
    unit.command_state = kUnitStateGuardPursueTarget;
    unit.target = &current;
    unit.command_value = current.id;
    unit.path_target_x = current.x;
    unit.path_target_y = current.y;
    g_guard_target = &replacement;

    HandleUnitGuardPursueTarget(commands, unit);

    require(unit.target == &replacement && unit.command_value == replacement.id,
        "guard pursue rejected an equal-priority replacement target");
    require(unit.command_state == kUnitStateGuardPursueTarget,
        "guard pursue replacement changed the command state");
    require(unit.path_target_x == replacement.x &&
            unit.path_target_y == replacement.y,
        "guard pursue replacement retained the previous target path");
    require_position(unit, kStartX, kStartY,
        "guard pursue moved the old path after replacement replanning");
    g_guard_target = nullptr;
}

void check_guard_combat_carry_uses_scanned_target() {
    UnitMovementUnit saved{};
    saved.id = 0x2f00u;
    UnitMovementUnit scanned{};
    scanned.id = 0x30d0u;

    require(SelectGuardCombatCycleCarryTarget(&saved, &scanned) == &scanned,
        "guard combat carry retained the invalid saved target");
    require(SelectGuardCombatCycleCarryTarget(&saved, nullptr) == nullptr,
        "guard combat carry retained the saved target without a scanned gate");
}

void check_guard_combat_completion_replaces_equal_priority() {
    UnitMovementUnit completed{};
    completed.id = 0x2f00u;
    completed.definition.target_selection_priority = 7;
    UnitMovementUnit scanned{};
    scanned.id = 0x30d0u;
    scanned.definition.target_selection_priority = 7;

    require(SelectGuardCombatCycleCompletedTarget(&completed, &scanned) ==
            &scanned,
        "guard combat completion retained its target at equal priority");
    scanned.definition.target_selection_priority = 8;
    require(SelectGuardCombatCycleCompletedTarget(&completed, &scanned) ==
            &completed,
        "guard combat completion replaced its target at lower priority");
    scanned.definition.target_selection_priority = 6;
    require(SelectGuardCombatCycleCompletedTarget(&completed, &scanned) ==
            &scanned,
        "guard combat completion rejected a strictly higher-priority target");
    require(SelectGuardCombatCycleCompletedTarget(&completed, nullptr) ==
            &completed,
        "guard combat completion discarded its target without a scan result");
}

void check_damage_reaction_guard_jump_table_mapping() {
    require(ResolveUnitDamageReactionRetargetPolicy(
                kUnitStateGuardReturnCommand) ==
            UnitDamageReactionRetargetPolicy::force_guard,
        "damage reaction state 0x1f missed the unconditional guard handler");
    require(ResolveUnitDamageReactionRetargetPolicy(
                kUnitStateGuardCombatCycle) ==
            UnitDamageReactionRetargetPolicy::priority_distance,
        "damage reaction state 0x20 bypassed priority and distance");
    require(ResolveUnitDamageReactionRetargetPolicy(
                kUnitStateGuardReturnTravel) ==
            UnitDamageReactionRetargetPolicy::force_guard,
        "damage reaction state 0x21 missed the unconditional guard handler");
    require(ResolveUnitDamageReactionRetargetPolicy(
                kUnitStateGuardPursueTarget) ==
            UnitDamageReactionRetargetPolicy::priority_distance,
        "damage reaction state 0x22 bypassed priority and distance");
    require(ResolveUnitDamageReactionRetargetPolicy(
                kUnitStateCommand23) ==
            UnitDamageReactionRetargetPolicy::none,
        "damage reaction state 0x23 did not use the default no-op entry");
}

void check_damage_reaction_acquire_preserves_action_mode() {
    UnitMovementUnit attacker{};
    attacker.id = 0x2f00u;
    attacker.x = 1456;
    attacker.y = 1092;

    UnitMovementUnit neutral{};
    neutral.command_state = kUnitStateRuntimeIdleAcquire;
    neutral.command_flags = 0x28u;
    neutral.action_mode = 75;
    neutral.animation_frame = 14;
    ApplyUnitDamageReactionAcquireAttackerInRange(neutral, attacker);

    require(neutral.target == &attacker &&
            neutral.command_value == attacker.id &&
            neutral.path_target_x == attacker.x &&
            neutral.path_target_y == attacker.y &&
            neutral.command_state == kUnitStateAttackTarget &&
            neutral.command_flags == 0x20u &&
            neutral.animation_frame == 0 && neutral.action_mode == 75,
        "damage reaction confused raw +0x64 animation with raw +0x2c meat");

    neutral.animation_frame = 9;
    neutral.command_flags = 0x8u;
    ApplyUnitDamageReactionAcquireAttackerInRange(neutral, attacker);
    require(neutral.animation_frame == 9 && neutral.action_mode == 75 &&
            neutral.command_flags == 0,
        "same-state damage reaction did not preserve original state-four fields");
}

void check_attack_travel_accepts_equal_priority_replacement() {
    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    // The branch itself copies and commits the replacement path before any
    // movement step.  Keep the pathfinder out of this unit test so map-cell
    // adjustment cannot obscure the equal-priority selection contract.
    commands.movement = nullptr;
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;
    commands.callbacks.target_in_action_range = reject_guard_range;

    UnitMovementUnit current{};
    current.id = 0x2f00u;
    current.x = 1000;
    current.y = 650;
    current.active = true;
    current.definition.target_selection_priority = 7;

    UnitMovementUnit replacement{};
    replacement.id = 0x30d0u;
    replacement.x = 1200;
    replacement.y = 650;
    replacement.active = true;
    replacement.definition.target_selection_priority = 7;

    UnitMovementUnit unit = make_unit(8, 0x20u, 0x1u);
    unit.command_state = kUnitStateAttackTravel;
    unit.target = &current;
    unit.command_value = current.id;
    unit.path_target_x = current.x;
    unit.path_target_y = current.y;
    unit.next_path_x = current.x;
    unit.next_path_y = current.y;
    g_guard_target = &replacement;

    ProcessUnitAttackTravelCommand(commands, unit);

    require(unit.target == &replacement && unit.command_value == replacement.id,
        "attack travel rejected an equal-priority replacement target");
    require(unit.command_state == kUnitStateAttackTravel,
        "attack travel replacement changed the command state");
    require((unit.command_flags & 8u) != 0,
        "attack travel replacement did not set the path command flag");
    require(unit.path_target_x != current.x || unit.path_target_y != current.y,
        "attack travel replacement retained the previous target path");
    require_position(unit, kStartX, kStartY,
        "attack travel moved the old path after replacement replanning");
    g_guard_target = nullptr;
}

void check_attack_travel_completed_step_checks_range_before_repath() {
    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;
    commands.callbacks.target_in_action_range =
        accept_attack_travel_range_after_completed_step;
    commands.callbacks.dispatch_attack = ignore_attack_dispatch;

    UnitMovementUnit target{};
    target.id = 0x1d0u * 159u;
    target.x = 983;
    target.y = 1140;
    target.active = true;

    UnitMovementUnit unit = make_unit(8, 0x1020u, 1u);
    unit.command_state = kUnitStateAttackTravel;
    unit.definition.movement_class = 3;
    unit.x = 988;
    unit.y = 1135;
    unit.path_target_x = 989;
    unit.path_target_y = 1133;
    unit.next_path_x = unit.x;
    unit.next_path_y = unit.y;
    unit.movement_flags = kUnitMovementFlagInterpolatingTowardTarget;
    unit.movement_interpolation_x = 988.25f;
    unit.movement_interpolation_y = 1135.5f;
    unit.target = &target;
    unit.command_value = target.id;
    g_guard_target = nullptr;
    g_attack_travel_range_calls = 0;

    ProcessUnitAttackTravelCommand(commands, unit);

    require(g_attack_travel_range_calls == 2,
        "attack travel did not revalidate after the completed movement step");
    require(unit.command_state == kUnitStateAttackTarget,
        "completed attack travel did not enter the in-range action state");
    require(unit.next_path_x == 988 && unit.next_path_y == 1135,
        "in-range attack travel replanned before entering the action state");
    require(unit.movement_flags == kUnitMovementFlagInterpolatingTowardTarget &&
            unit.movement_interpolation_x == 988.25f &&
            unit.movement_interpolation_y == 1135.5f,
        "in-range attack travel cleared completed movement interpolation scratch");
}

void check_idle_out_of_range_attack_preserves_animation_frame() {
    const ProductionOrderRuntimeState production_state{};
    UnitCommandContext commands{};
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;
    commands.callbacks.target_in_action_range = reject_guard_range;

    UnitMovementUnit target{};
    target.id = 0x1d0u * 17u;
    target.owner_id = 1;
    target.active = true;
    target.x = 3359;
    target.y = 1846;

    UnitMovementUnit unit = make_unit(2, 0x20u, 1u);
    unit.type_flags = 0x20u;
    unit.command_state = kUnitStateRuntimeIdleAcquire;
    unit.animation_frame = 32;
    unit.x = 3520;
    unit.y = 2048;
    g_guard_target = &target;

    ProcessUnitIdleAcquireCommand(commands, unit);

    require(unit.command_state == kUnitStateAttackTravel,
        "idle out-of-range target did not enter attack travel directly");
    require(unit.animation_frame == 32,
        "idle out-of-range target cleared the metadata-preserved animation frame");
    require(unit.target == &target &&
            unit.path_target_x == target.x && unit.path_target_y == target.y,
        "idle out-of-range target did not publish the attack-travel path");
    g_guard_target = nullptr;
}

void check_idle_invalid_scan_does_not_publish_target_or_path() {
    const ProductionOrderRuntimeState production_state{};
    UnitCommandContext commands{};
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = reject_guard_attack;
    commands.callbacks.target_in_action_range = reject_guard_range;

    UnitMovementUnit target{};
    target.id = 0x1d0u * 153u;
    target.owner_id = 2;
    target.active = true;
    target.x = 1459;
    target.y = 1826;

    UnitMovementUnit unit = make_unit(8, 0x20u, 1u);
    unit.type_flags = 0x20u;
    unit.command_state = kUnitStateRuntimeIdleAcquire;
    unit.animation_frame = 0;
    unit.definition.animation_timer_period = 32;
    unit.path_target_x = 1664;
    unit.path_target_y = 1792;
    g_guard_target = &target;

    ProcessUnitIdleAcquireCommand(commands, unit);

    require(unit.command_state == kUnitStateRuntimeIdleAcquire,
        "idle invalid scan entered attack travel");
    require(unit.target == nullptr && unit.command_value == 0,
        "idle invalid scan published the rejected candidate");
    require(unit.path_target_x == 1664 && unit.path_target_y == 1792,
        "idle invalid scan overwrote the existing path tuple");
    require(unit.animation_frame == 1,
        "idle invalid scan did not continue the idle animation");
    g_guard_target = nullptr;
}

void check_high_runtime_idle_does_not_pursue_out_of_range_target() {
    const ProductionOrderRuntimeState production_state{};
    UnitCommandContext commands{};
    commands.production_state = &production_state;
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;
    commands.callbacks.target_in_action_range = reject_guard_range;
    commands.callbacks.dispatch_attack = ignore_attack_dispatch;

    UnitMovementUnit target{};
    target.id = 0x1d0u * 153u;
    target.owner_id = 2;
    target.active = true;
    target.x = 940;
    target.y = 441;

    UnitMovementUnit unit = make_unit(8, 0x20u, 1u);
    unit.type_id = 160;
    unit.type_flags = 0x20u;
    unit.command_state = kUnitStateRuntimeIdleAcquire;
    unit.animation_frame = 0;
    unit.path_target_x = 0;
    unit.path_target_y = 0;
    unit.definition.movement_animation_frame_count = 0;
    g_guard_target = &target;

    HandleUnitRuntimeIdleAcquireState(commands, unit);

    require(unit.command_state == kUnitStateRuntimeIdleAcquire,
        "high runtime idle pursued an out-of-range scanned target");
    require(unit.target == nullptr && unit.command_value == 0 &&
            unit.path_target_x == 0 && unit.path_target_y == 0,
        "high runtime idle published the out-of-range candidate");
    require(unit.animation_frame == 1,
        "high runtime idle did not continue its eight-frame idle cycle");

    commands.callbacks.target_in_action_range = accept_guard_range;
    unit.animation_frame = 0;
    HandleUnitRuntimeIdleAcquireState(commands, unit);
    require(unit.command_state == kUnitStateRuntimeAttackTarget &&
            unit.target == &target && unit.command_value == target.id &&
            unit.path_target_x == target.x && unit.path_target_y == target.y,
        "high runtime idle rejected an in-range target");
    g_guard_target = nullptr;
}

void check_completed_attack_target_selection_preserves_high_runtime_path() {
    UnitCommandContext commands{};
    commands.callbacks.find_target = find_same_guard_target;
    commands.callbacks.can_attack_target = allow_guard_attack;

    UnitMovementUnit target{};
    target.id = 0x1d0u * 153u;
    target.type_id = 33;
    target.active = true;
    target.x = 940;
    target.y = 339;
    g_guard_target = &target;

    UnitMovementUnit high = make_unit(8, 0, 1);
    high.type_id = 160;
    high.command_state = kUnitStateRuntimeAttackTarget;
    high.target = &target;
    high.command_value = target.id;
    high.path_target_x = 940;
    high.path_target_y = 345;
    HandleUnitAttackCycleCompleteTargetSelection(commands, high);
    require(high.target == &target && high.command_value == target.id &&
            high.path_target_x == 940 && high.path_target_y == 345,
        "extended attack cycle refreshed its moving target acquisition point");

    UnitMovementUnit low = make_unit(2, 0, 1);
    low.type_id = 33;
    low.command_state = kUnitStateAttackTarget;
    low.path_target_x = 940;
    low.path_target_y = 345;
    HandleUnitAttackCycleCompleteTargetSelection(commands, low);
    require(low.target == &target && low.command_value == target.id &&
            low.path_target_x == 940 && low.path_target_y == 339,
        "low attack cycle lost its original replacement-target refresh");
    g_guard_target = nullptr;
}

void check_action_validation_does_not_publish_out_of_range_target_path() {
    UnitMovementUnit target{};
    target.id = 0x1d0u * 153u;
    target.type_id = 33;
    target.active = true;
    target.x = 962;
    target.y = 113;

    UnitMovementUnit source = make_unit(8, 0, 1);
    source.type_id = 160;
    source.command_state = kUnitStateRuntimeAttackTarget;
    source.target = &target;
    source.command_value = target.id;
    source.path_target_x = 940;
    source.path_target_y = 345;

    UnitActionContext actions{};
    actions.callbacks.validate_target_reach =
        valid_out_of_range_action_target;
    const UnitActionTickResult result = ProcessUnitActionCycle(actions, source);
    require(result.code == UnitActionTickCode::lost_target &&
            result.valid_target && result.target == &target,
        "valid out-of-range action result was not returned to its caller");
    require(source.path_target_x == 940 && source.path_target_y == 345,
        "shared action validation published the moving target path");
}

void check_reserved_tile_wait_uses_raw_13d4_frame_period() {
    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.production_state = &production_state;

    UnitMovementUnit unit = make_unit(2, 0, 1);
    unit.command_state = kUnitStateReservedTileBlockedWait;
    unit.animation_frame = 15;
    unit.definition.animation_frame_count = 16;
    unit.definition.animation_timer_period = 32;
    HandleReservedTileWait(commands, unit);
    require(unit.animation_frame == 16,
        "reserved-tile wait wrapped at raw +0x13d8 instead of +0x13d4");

    unit.animation_frame = 1;
    unit.definition.animation_timer_period = 0;
    HandleReservedTileWait(commands, unit);
    require(unit.animation_frame == 1,
        "zero raw +0x13d4 period advanced the reserved-tile wait frame");
}

void check_low_id_effect_damage_is_calculated_at_impact() {
    UnitMovementUnit source{};
    source.id = 0x1d0u;
    source.active = true;
    UnitMovementUnit target{};
    target.id = 0x3a0u;
    target.active = true;

    UnitEffectRuntimeState state{};
    state.unit_refs = {&source, &target};
    state.callbacks.calculate_impact_damage = fixed_impact_damage;

    UnitEffectDefinition generic_definition{};
    generic_definition.id = 0x1cu;
    generic_definition.active_frames = 2;
    generic_definition.impact_render_ticks = 2;
    generic_definition.impact_frames = {0};
    generic_definition.action_area_damage_radius = 0x400u;
    state.definitions.push_back(generic_definition);

    UnitEffectRuntime generic{};
    generic.active = true;
    generic.effect_id = 0x1cu;
    generic.amount = 9;
    generic.source_unit_id = source.id;
    generic.target_unit_id = target.id;
    TickUnitEffectFrameAndApplyImpacts(state, generic);

    require(generic.amount == 9 && generic.tick == 1 && generic.frame == 0,
        "low-id generic impact overwrote the recycled raw +0x14 payload");
    require(state.events.size() == 1 &&
            state.events[0].kind == UnitEffectEventKind::impact &&
            state.events[0].target_id == target.id &&
            state.events[0].value == 37,
        "low-id generic impact used stored amount/area damage instead of live damage");

    state.events.clear();
    state.definitions.clear();
    UnitEffectDefinition fixed_point_definition{};
    fixed_point_definition.id = 0x22u;
    fixed_point_definition.active_frames = 2;
    fixed_point_definition.impact_render_ticks = 2;
    state.definitions.push_back(fixed_point_definition);

    UnitEffectRuntime fixed_point{};
    fixed_point.active = true;
    fixed_point.effect_id = 0x22u;
    fixed_point.flags = kUnitEffectFlagImpact;
    fixed_point.amount = 11;
    fixed_point.source_unit_id = source.id;
    fixed_point.target_unit_id = target.id;
    TickUnitEffectRuntime(state, fixed_point);

    require(fixed_point.amount == 11 && state.events.size() == 1 &&
            state.events[0].kind == UnitEffectEventKind::impact &&
            state.events[0].value == 37,
        "fixed-point low-id impact used the preserved slot amount as damage");

    state.events.clear();
    state.definitions.clear();
    UnitEffectDefinition wrapped_steps_definition{};
    wrapped_steps_definition.id = 0x2au;
    wrapped_steps_definition.active_frames = 4;
    wrapped_steps_definition.impact_render_ticks = 4;
    wrapped_steps_definition.active_step_iterations = 0;
    state.definitions.push_back(wrapped_steps_definition);

    source.x = 100;
    source.y = 100;
    target.x = 100;
    target.y = 100;
    UnitEffectRuntime wrapped_steps{};
    wrapped_steps.active = true;
    wrapped_steps.effect_id = 0x2au;
    wrapped_steps.amount = 13;
    InitializeUnitEffectPathToTarget(state, wrapped_steps, source, target);
    TickUnitEffectPathActive(state, wrapped_steps);

    require((wrapped_steps.flags & kUnitEffectFlagImpact) != 0 &&
            wrapped_steps.tick == 1 && wrapped_steps.frame == 0 &&
            wrapped_steps.amount == 13 && state.events.size() == 1 &&
            state.events[0].kind == UnitEffectEventKind::impact &&
            state.events[0].value == 37,
        "zero active-step count skipped the original do-while wrapped path");

    TickUnitEffectRuntime(state, wrapped_steps);
    require(wrapped_steps.active && wrapped_steps.tick == 2 &&
            wrapped_steps.frame == 0,
        "low-id impact advanced raw +0x10 instead of the original +0x0c frame");

    state.events.clear();
    state.definitions.clear();
    UnitEffectDefinition directional_definition{};
    directional_definition.id = 0x1cu;
    directional_definition.active_frames = 8;
    directional_definition.active_step_iterations = 1;
    directional_definition.directional_active_frames = true;
    state.definitions.push_back(directional_definition);

    source.x = 100;
    source.y = 100;
    target.x = 1000;
    target.y = 100;
    UnitEffectRuntime directional{};
    directional.active = true;
    directional.effect_id = 0x1cu;
    InitializeUnitEffectPathToTarget(state, directional, source, target);
    TickUnitEffectPathActive(state, directional);
    require(directional.active &&
            (directional.flags & kUnitEffectFlagImpact) == 0 &&
            directional.tick == 0,
        "directional low-id path skipped the original +0x224 divide-by-eight");
}

void check_projectile_first_bounds_entry_uses_inclusive_far_edge_center() {
    UnitMovementUnit target{};
    target.id = 0x3a0u;
    target.active = true;
    target.x = 959;
    target.y = 734;
    target.definition.center_bounds_left = -54;
    target.definition.center_bounds_top = -87;
    target.definition.center_bounds_width = 107;
    target.definition.center_bounds_height = 127;

    UnitEffectDefinition definition{};
    definition.id = 10;
    definition.behavior_flags = 2;

    UnitEffectRuntimeState state{};
    state.definitions.push_back(definition);
    state.unit_refs = {&target};

    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = definition.id;
    effect.flags = kUnitEffectFlagProjectileYMajor;
    effect.target_unit_id = target.id;
    effect.x = 971;
    effect.y = 776;
    effect.step_y = -2;

    AdvanceUnitEffectProjectileTowardTarget(state, effect);

    require(effect.x == 971 && effect.y == 774 &&
            (effect.flags & kUnitEffectFlagProjectileBoundsEntered) != 0 &&
            effect.closest_distance == 66,
        "first odd-sized bounds entry used the ordinary floor-half center "
        "instead of original inclusive-far-edge rounding");
}

void check_low_id_reach_uses_live_area_damage_and_preserves_impact_position() {
    UnitMovementUnit source{};
    source.id = 0x1d0u;
    source.owner_id = 2;
    source.active = true;
    source.x = 105;
    source.y = 94;

    UnitMovementUnit direct{};
    direct.id = 0x3a0u;
    direct.owner_id = 8;
    direct.active = true;
    direct.x = 100;
    direct.y = 100;
    direct.definition.render_class = 1;
    direct.definition.center_bounds_left = -100;
    direct.definition.center_bounds_top = -100;
    direct.definition.center_bounds_width = 200;
    direct.definition.center_bounds_height = 200;

    UnitMovementUnit nearby{};
    nearby.id = 0x570u;
    nearby.owner_id = 8;
    nearby.active = true;
    nearby.x = 120;
    nearby.y = 94;
    nearby.definition.render_class = 0;

    UnitEffectDefinition definition{};
    definition.id = 0x1fu;
    definition.active_frames = 4;
    definition.active_step_iterations = 4;
    definition.action_area_damage_radius = 100;
    definition.allowed_target_render_class_mask = 0xffffffffu;

    UnitEffectRuntimeState state{};
    state.definitions.push_back(definition);
    state.unit_refs = {&source, &direct, &nearby};
    state.callbacks.calculate_impact_damage = live_area_impact_damage;

    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = definition.id;
    InitializeUnitEffectPathToTarget(state, effect, source, direct);
    g_area_direct_damage_calls = 0;
    g_area_nearby_damage_calls = 0;
    TickUnitEffectPathActive(state, effect);

    const u32 nearby_distance = CalculateApproxUnitDistance(
        effect.x, effect.y, nearby.x, nearby.y);
    const u32 nearby_amount = 40u - static_cast<u32>(
        (static_cast<u64>(nearby_distance) * 40u) / 100u);
    require(state.events.size() == 2 &&
            state.events[0].target_id == direct.id &&
            state.events[0].value == 37 &&
            state.events[1].target_id == nearby.id &&
            state.events[1].value == nearby_amount &&
            g_area_direct_damage_calls == 2 &&
            g_area_nearby_damage_calls == 1 &&
            (effect.x != direct.x || effect.y != direct.y),
        "JW2_12 reach skipped raw +0x1f8 live area damage or snapped the "
        "preserved projectile impact position to target raw x/y");
}

void check_low_id_reach_transient_target_preserves_impact_state() {
    UnitMovementUnit source{};
    source.id = 0x1d0u;
    source.active = true;
    source.x = 100;
    source.y = 100;

    UnitMovementUnit target{};
    target.id = 0x3a0u;
    target.active = true;
    target.runtime_flags = kUnitActionTargetTransient;
    target.x = 100;
    target.y = 100;

    UnitEffectDefinition definition{};
    definition.id = 0x1fu;
    definition.active_frames = 4;
    definition.active_step_iterations = 1;

    UnitEffectRuntimeState state{};
    state.definitions.push_back(definition);
    state.unit_refs = {&source, &target};

    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = definition.id;
    InitializeUnitEffectPathToTarget(state, effect, source, target);

    // The first coincident step establishes closest_distance=0.  The second
    // enters original 0x004ec813 and then takes the transient-target branch at
    // 0x004ec823 without unlinking the effect.
    TickUnitEffectPathActive(state, effect);
    TickUnitEffectPathActive(state, effect);

    require(effect.active &&
            (effect.flags & kUnitEffectFlagImpact) != 0 &&
            state.events.empty(),
        "transient low-id reach unlinked the impact instead of skipping damage");
}

void check_flagged_low_id_impact_uses_command_entry_lockout() {
    UnitMovementUnit source{};
    source.id = 0x1d0u;
    source.active = true;
    source.x = 100;
    source.y = 100;

    UnitMovementUnit target{};
    target.id = 0x3a0u;
    target.active = true;
    target.x = 100;
    target.y = 100;
    target.command_state = kUnitStateAttackTarget;
    target.runtime_flags = 1;
    target.command_lockout_ticks = 6;
    target.animation_timer = 7;
    target.definition.action_effect_flags = 0x8u;

    UnitEffectDefinition definition{};
    definition.id = 0x14u;
    definition.active_frames = 4;
    definition.active_step_iterations = 1;

    UnitEffectRuntimeState state{};
    state.definitions.push_back(definition);
    state.unit_refs = {&source, &target};
    state.callbacks.calculate_impact_damage = fixed_impact_damage;

    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = definition.id;
    InitializeUnitEffectPathToTarget(state, effect, source, target);
    // The first coincident step records closest_distance=0; the second takes
    // the original non-improving-distance reach branch.
    TickUnitEffectPathActive(state, effect);
    TickUnitEffectPathActive(state, effect);

    require(state.events.size() == 2 &&
            state.events[0].kind == UnitEffectEventKind::impact &&
            state.events[0].target_id == target.id &&
            state.events[0].value == 37 &&
            state.events[1].kind == UnitEffectEventKind::target_lockout &&
            state.events[1].target_id == target.id &&
            state.events[1].value == 10,
        "flagged low-id reach did not queue damage before target lockout");
    require((target.runtime_flags & 0x40u) == 0 &&
            (target.command_state & 0x40000000u) == 0 &&
            target.command_entry_lockout_ticks == 0,
        "flagged low-id reach installed target lockout before queued damage");

    ApplyUnitActionEffectTargetLockoutIfFlagged(
        target, state.events[1].value);
    require((target.runtime_flags & 0x40u) != 0 &&
            (target.command_state & 0x40000000u) != 0 &&
            target.command_entry_lockout_ticks == 10 &&
            target.animation_timer == 0 &&
            target.command_lockout_ticks == 6,
        "flagged low-id impact replaced raw +0xf4 recovery instead of "
        "calling the raw +0x98 command-entry lockout helper");
}

void check_high_id_area_damage_uses_definition_bounds_probe() {
    UnitMovementUnit source{};
    source.id = 0x1d0u;
    source.owner_id = 2;
    source.active = true;

    UnitMovementUnit direct{};
    direct.id = 0x3a0u;
    direct.owner_id = 8;
    direct.active = true;
    direct.x = 100;
    direct.y = 100;
    direct.definition.render_class = 0;

    UnitMovementUnit nearby{};
    nearby.id = 0x570u;
    nearby.owner_id = 8;
    nearby.active = true;
    nearby.x = 130;
    nearby.y = 123;
    nearby.definition.render_class = 0;
    nearby.definition.center_bounds_left = -20;
    nearby.definition.center_bounds_top = -28;
    nearby.definition.center_bounds_width = 10;
    nearby.definition.center_bounds_height = 10;

    UnitEffectDefinition definition{};
    definition.id = 0x3du;
    definition.allowed_target_render_class_mask = 0xffffffffu;

    UnitEffectRuntimeState state{};
    state.definitions.push_back(definition);
    state.unit_refs = {&source, &direct, &nearby};

    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = definition.id;
    effect.source_unit_id = source.id;
    effect.target_unit_id = direct.id;
    effect.x = 100;
    effect.y = 100;
    ApplyUnitEffectAreaDamageByRenderClassMask(
        state, effect, 37, 20, 0xffffffffu);

    require(state.events.size() == 2 &&
            state.events[0].target_id == direct.id &&
            state.events[0].value == 37 &&
            state.events[1].target_id == nearby.id &&
            state.events[1].value == 10,
        "JW2_11 area damage measured candidates from raw x/y instead of "
        "definition +0x360/+0x364 bounds center");
}

void check_owner_ai_retarget_timer_precedes_transport_phase_gate() {
    OwnerAiSlotRuntime owner{};
    owner.build_budget = 303;
    owner.last_timing_frame = 0;

    require(AdvanceOwnerAiStrategicRetargetTimer(owner, 6688) &&
            owner.last_timing_frame == 6688,
        "strategic retarget timer did not publish the pre-phase-gate frame");
    require(!AdvanceOwnerAiStrategicRetargetTimer(owner, 6704) &&
            owner.last_timing_frame == 6688,
        "strategic retarget timer ignored its original 22-frame quotient");
}

void check_transport_queue_publishes_strategic_phase_gate() {
    ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    commands.movement = &movement;

    UnitMovementUnit unit{};
    unit.owner_id = 2;
    unit.type_id = 0x20;
    unit.area_marker_flags = 1;
    movement.active_units.push_back(&unit);

    OwnerTransportQueueState queue{};
    queue.slots[1].state = kOwnerTransportQueueStateStrategicTargetAlt;
    const OwnerTransportQueueMaintenanceScratch strategic =
        TickOwnerTransportQueueMaintenance(commands, queue, 2, {});
    require(strategic.owner_phase_state ==
            kOwnerTransportQueueStateStrategicTarget,
        "strategic transport slot did not publish raw DAT_01233568 value 0x16");

    queue.slots[1].state = kOwnerTransportQueueStateWorkTarget;
    const OwnerTransportQueueMaintenanceScratch ordinary =
        TickOwnerTransportQueueMaintenance(commands, queue, 2, {});
    require(ordinary.owner_phase_state == 1,
        "ordinary transport slot did not restore raw DAT_01233568 value 1");
}

void check_transport_queue_uses_primary_target_radius_threshold() {
    OwnerAiSlotRuntime owner{};
    owner.route_target_score = 100;
    owner.primary_target_radius = 70;
    require(ResolveOwnerTransportStrategicQueueLoadPercent(owner) == 70,
        "transport queue used command-52 route score instead of raw "
        "DAT_01238f28 command-71 radius");
}

void check_conditional_target_point_uses_raw_type_flags() {
    UnitMovementUnit target{};
    target.id = 0x1d0u;

    UnitMovementUnit type_enabled{};
    type_enabled.type_flags = 0x20u;
    type_enabled.command_flags = 0;
    require(SetOrQueueUnitConditionalTargetPointCommand05(
                &type_enabled, &target, 3449, 2335, false),
        "raw +0x58 type flag did not accept conditional target command");
    require(type_enabled.pending_command.state == 5u &&
            static_cast<u32>(type_enabled.pending_command.x) == target.id &&
            type_enabled.pending_command.y == 3449 &&
            type_enabled.pending_command.value == 2335u,
        "raw +0x58 type flag did not publish the original state-5 tuple");

    UnitMovementUnit command_only{};
    command_only.type_flags = 0;
    command_only.command_flags = 0x20u;
    require(SetOrQueueUnitConditionalTargetPointCommand05(
                &command_only, &target, 3449, 2335, false),
        "state-4 fallback rejected a command-flags-only unit");
    require(command_only.pending_command.state == 4u &&
            command_only.pending_command.x == 0 &&
            command_only.pending_command.y == 3449 &&
            command_only.pending_command.value == 2335u,
        "raw +0x9c command flag incorrectly selected state 5");
}

void check_area_stun_initializer_clears_recycled_lifetime() {
    UnitEffectRuntimeState state{};
    UnitEffectDefinition definition{};
    definition.id = 0x3du + 5u;
    definition.damage_amount = 12;
    state.definitions.push_back(definition);

    UnitMovementUnit source{};
    source.id = 0x1d0u;
    source.owner_id = 1;
    source.health = 100;
    source.max_health = 100;

    UnitEffectRuntime recycled{};
    recycled.abs_delta_x = 0xdeadbeefu;
    recycled.abs_delta_y = 0x12345678u;

    require(DispatchSelectedUnitActionEffect(
                state, recycled, 5, source, nullptr, 640, 704),
        "area-stun selected action did not initialize");
    require(recycled.effect_id == 0x42u &&
            recycled.flags == kUnitEffectFlagImpact &&
            recycled.x == 640 && recycled.y == 704,
        "area-stun selected action did not use the common point initializer");
    require(recycled.abs_delta_x == 0 &&
            recycled.abs_delta_y == 0x12345678u,
        "area-stun initializer did not clear only raw effect +0x30");
}

void check_follow_hold_advances_raw_animation_frame() {
    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.production_state = &production_state;

    UnitMovementUnit target = make_unit(1, 0, 1);
    target.id = 65u * 0x1d0u;
    target.active = true;
    target.health = 110;
    target.max_health = 110;
    target.x = 511;
    target.y = 2207;

    UnitMovementUnit follower = make_unit(1, 9, 1);
    follower.type_id = 48;
    follower.command_state = kUnitStateFollowHoldRange;
    follower.target = &target;
    follower.x = 511;
    follower.y = 2207;
    follower.animation_frame = 10;
    follower.animation_timer = 3;
    follower.definition.animation_frame_count = 32;
    follower.definition.animation_timer_period = 16;

    ProcessUnitFollowHoldRange(commands, follower);
    require(follower.animation_frame == 11 && follower.animation_timer == 3,
        "follow-hold advanced raw +0xec instead of raw +0x64");

    follower.animation_frame = 15;
    ProcessUnitFollowHoldRange(commands, follower);
    require(follower.animation_frame == 0 && follower.animation_timer == 3,
        "follow-hold did not wrap at raw definition +0x13d4");

    follower.animation_frame = 7;
    follower.definition.animation_timer_period = 0;
    ProcessUnitFollowHoldRange(commands, follower);
    require(follower.animation_frame == 7 && follower.animation_timer == 3,
        "zero raw definition +0x13d4 advanced the follow-hold frame");
}

} // namespace

int main() {
    using namespace ranker;

    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);

    constexpr i32 boosted_x = kStartX + kBaseDeltaX + kAdditionalModifier;
    constexpr i32 boosted_y = kStartY + kBaseDeltaY - kAdditionalModifier;
    constexpr i32 base_x = kStartX + kBaseDeltaX;
    constexpr i32 base_y = kStartY + kBaseDeltaY;

    check_common_movement(movement, 0, 0x10000u, 0, boosted_x, boosted_y,
        "owner<8 common movement must use command flag 0x10000");
    check_common_movement(movement, 0, 0, 0x10000u, base_x, base_y,
        "owner<8 common movement must ignore runtime flag 0x10000");
    check_transport_dock(movement, production_state, 0, 0x10000u, 0,
        boosted_x, boosted_y,
        "owner<8 transport dock must use command flag 0x10000");
    check_transport_dock(movement, production_state, 0, 0, 0x10000u,
        base_x, base_y,
        "owner<8 transport dock must ignore runtime flag 0x10000");

    check_common_movement(movement, 8, 0x10000u, 0, base_x, base_y,
        "owner>=8 common movement must bypass the modifier");
    check_common_movement(movement, 8, 0, 0x10000u, base_x, base_y,
        "owner>=8 common movement must ignore the runtime flag");
    check_transport_dock(movement, production_state, 8, 0x10000u, 0,
        base_x, base_y,
        "owner>=8 transport dock must bypass the modifier");
    check_transport_dock(movement, production_state, 8, 0, 0x10000u,
        base_x, base_y,
        "owner>=8 transport dock must ignore the runtime flag");
    check_draw_feedback_is_independent_from_raw_command_bits();
    check_nearest_pathable_goal_uses_unsigned_edge_containment();
    check_action_recovery_uses_growth_variant_not_cargo();
    check_guard_pursue_same_target_advances_movement();
    check_action_geometry_gate_uses_raw_14c_for_reach_and_retarget();
    check_guard_pursue_range_transition_refreshes_target_path();
    check_guard_pursue_accepts_equal_priority_replacement();
    check_guard_combat_carry_uses_scanned_target();
    check_guard_combat_completion_replaces_equal_priority();
    check_damage_reaction_guard_jump_table_mapping();
    check_damage_reaction_acquire_preserves_action_mode();
    check_attack_travel_accepts_equal_priority_replacement();
    check_attack_travel_completed_step_checks_range_before_repath();
    check_idle_out_of_range_attack_preserves_animation_frame();
    check_idle_invalid_scan_does_not_publish_target_or_path();
    check_high_runtime_idle_does_not_pursue_out_of_range_target();
    check_completed_attack_target_selection_preserves_high_runtime_path();
    check_action_validation_does_not_publish_out_of_range_target_path();
    check_reserved_tile_wait_uses_raw_13d4_frame_period();
    check_low_id_effect_damage_is_calculated_at_impact();
    check_projectile_first_bounds_entry_uses_inclusive_far_edge_center();
    check_low_id_reach_uses_live_area_damage_and_preserves_impact_position();
    check_low_id_reach_transient_target_preserves_impact_state();
    check_flagged_low_id_impact_uses_command_entry_lockout();
    check_high_id_area_damage_uses_definition_bounds_probe();
    check_owner_ai_retarget_timer_precedes_transport_phase_gate();
    check_transport_queue_publishes_strategic_phase_gate();
    check_transport_queue_uses_primary_target_radius_threshold();
    check_conditional_target_point_uses_raw_type_flags();
    check_area_stun_initializer_clears_recycled_lifetime();
    check_follow_hold_advances_raw_animation_frame();
    check_all_unit_spatial_index_double_sort();
    check_full_action_visibility_requires_owner_layer();
    check_queued_primary_count_uses_raw_command_value();
    check_placed_unit_clears_raw_identity_flags();
    check_legacy_spawn_approach_uses_live_path_target();

    std::cout << "MOVEMENT_MODIFIER_COMMAND_FLAG_PASS "
                 "owner0-command={6,-7} owner0-runtime={2,-3} "
                 "owner8-command/runtime={2,-3} paths=common+dock "
                 "draw-feedback=raw-a4 command-bit=raw-5c "
                 "nearest-goal-edge=unsigned-containment "
                 "guard-same-target=movement+animation "
                 "reach+retarget-gate=raw-14c guard-combat-path=target "
                 "guard-pursue=equal-priority-repath "
                 "guard-combat-carry=scanned-target "
                 "guard-combat-complete=equal-priority-replace "
                 "damage-reaction-guard-table=1f/20/21/22/23 "
                 "damage-reaction-acquire=preserve-raw-2c+clear-raw-64 "
                 "attack-travel=equal-priority-repath "
                 "attack-travel-complete=in-range-before-repath "
                 "idle-attack-travel=preserve-animation "
                 "high-idle=reject-out-of-range-pursuit "
                 "high-cycle=preserve-acquisition-path "
                 "action-range=caller-publishes-path "
                 "reserved-wait-frame=raw-13d4 "
                 "projectile-bounds-entry=inclusive-far-edge-center "
                 "low-id-impact=live-area-damage+transient-preserve+entry-lockout "
                 "high-id-area=bounds-probe "
                 "retarget-timer=pre-phase-gate "
                 "zero-steps=wrapped-do-while "
                 "transport-phase=raw-33568 spatial-mode0=double-sort "
                 "conditional-target=raw-58-type-flags "
                 "area-stun-init=clear-raw-30 "
                 "follow-hold-frame=raw-64/raw-def-13d4 "
                 "queued-primary=raw-68 placed-identity=raw-08+0c "
                 "legacy-placement=live-raw-6c+70\n";
    return EXIT_SUCCESS;
}
