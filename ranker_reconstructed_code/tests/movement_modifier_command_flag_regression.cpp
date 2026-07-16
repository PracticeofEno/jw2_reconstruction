#include "ranker_production_orders.h"
#include "ranker_owner_ai.h"
#include "ranker_unit_action.h"
#include "ranker_unit_commands.h"
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

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "MOVEMENT_MODIFIER_COMMAND_FLAG_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool allow_enter(UnitMovementContext&, const UnitMovementUnit&, i32, i32) {
    return true;
}

UnitMovementUnit* find_same_guard_target(UnitCommandContext&,
    UnitMovementUnit&) {
    return g_guard_target;
}

bool allow_guard_attack(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return true;
}

bool reject_guard_range(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return false;
}

bool accept_guard_range(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return true;
}

const UnitMovementDefinition* find_placed_definition(
    UnitLifecycleContext&, u32) {
    return &g_placed_definition;
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

void check_action_reach_gate_uses_raw_14c_field() {
    UnitActionDamageProfile profile{};
    profile.render_class2_terrain_gate = 1;
    profile.target_distance_gate = 0;
    require(UnitActionProfileUsesCenterDistanceReach(profile),
        "raw +0x14c distance-reach gate was confused with raw +0x240");

    profile.render_class2_terrain_gate = 0;
    profile.target_distance_gate = 1;
    require(!UnitActionProfileUsesCenterDistanceReach(profile),
        "raw +0x240 replacement gate incorrectly selected center distance");
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

void check_guard_combat_carry_restores_saved_target() {
    UnitMovementUnit saved{};
    saved.id = 0x2f00u;
    UnitMovementUnit scanned{};
    scanned.id = 0x30d0u;

    require(SelectGuardCombatCycleCarryTarget(&saved, &scanned) == &saved,
        "guard combat carry replaced the saved target with the scanned gate");
    require(SelectGuardCombatCycleCarryTarget(&saved, nullptr) == nullptr,
        "guard combat carry retained the saved target without a scanned gate");
}

void check_guard_combat_completion_accepts_scanned_target() {
    UnitMovementUnit completed{};
    completed.id = 0x2f00u;
    UnitMovementUnit scanned{};
    scanned.id = 0x30d0u;

    require(SelectGuardCombatCycleCompletedTarget(&completed, &scanned) ==
            &scanned,
        "guard combat completion retained the target killed by its impact");
    require(SelectGuardCombatCycleCompletedTarget(&completed, nullptr) ==
            &completed,
        "guard combat completion discarded its target without a scan result");
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
    check_guard_pursue_same_target_advances_movement();
    check_action_reach_gate_uses_raw_14c_field();
    check_guard_pursue_range_transition_refreshes_target_path();
    check_guard_pursue_accepts_equal_priority_replacement();
    check_guard_combat_carry_restores_saved_target();
    check_guard_combat_completion_accepts_scanned_target();
    check_attack_travel_accepts_equal_priority_replacement();
    check_reserved_tile_wait_uses_raw_13d4_frame_period();
    check_low_id_effect_damage_is_calculated_at_impact();
    check_transport_queue_publishes_strategic_phase_gate();
    check_transport_queue_uses_primary_target_radius_threshold();
    check_all_unit_spatial_index_double_sort();
    check_queued_primary_count_uses_raw_command_value();
    check_placed_unit_clears_raw_identity_flags();

    std::cout << "MOVEMENT_MODIFIER_COMMAND_FLAG_PASS "
                 "owner0-command={6,-7} owner0-runtime={2,-3} "
                 "owner8-command/runtime={2,-3} paths=common+dock "
                 "draw-feedback=raw-a4 command-bit=raw-5c "
                 "guard-same-target=movement+animation "
                 "reach-gate=raw-14c guard-combat-path=target "
                 "guard-pursue=equal-priority-repath "
                 "guard-combat-carry=saved-target "
                 "guard-combat-complete=scanned-target "
                 "attack-travel=equal-priority-repath "
                 "reserved-wait-frame=raw-13d4 "
                 "low-id-impact=live-damage zero-steps=wrapped-do-while "
                 "transport-phase=raw-33568 spatial-mode0=double-sort "
                 "queued-primary=raw-68 placed-identity=raw-08+0c\n";
    return EXIT_SUCCESS;
}
