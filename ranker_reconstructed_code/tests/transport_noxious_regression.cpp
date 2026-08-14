#include "ranker_unit_action.h"
#include "ranker_unit_commands.h"
#include "ranker_unit_lifecycle.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_target_helpers.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "TRANSPORT_NOXIOUS_REGRESSION_FAIL " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct ZeroDamageAreaProbe {
    u32 count = 0;
    std::array<u32, 5> target_ids{};
};

ZeroDamageAreaProbe g_zero_damage_area_probe;

bool apply_zero_damage_area_impact_synchronously(UnitEffectRuntimeState&,
    const UnitEffectEvent& event) {
    if (event.kind != UnitEffectEventKind::impact) {
        return false;
    }
    require(event.value == 0,
        "zero-damage area probe received a nonzero impact");
    require(g_zero_damage_area_probe.count <
            g_zero_damage_area_probe.target_ids.size(),
        "zero-damage area probe received an extra impact");
    g_zero_damage_area_probe.target_ids[g_zero_damage_area_probe.count++] =
        event.target_id;
    return true;
}

void test_zero_damage_unit_flag_area_preserves_reactions() {
    UnitEffectRuntimeState state{};
    UnitEffectDefinition noxious{};
    noxious.id = 0x4a;
    noxious.action_area_damage_radius = 128;
    noxious.area_damage_allows_related_targets = true;
    state.definitions.push_back(noxious);
    state.callbacks.apply_simulation_event =
        apply_zero_damage_area_impact_synchronously;

    UnitMovementUnit source{};
    source.id = 0x100;
    source.active = true;
    source.runtime_flags = 1;
    source.x = 100;
    source.y = 100;

    std::array<UnitMovementUnit, 5> targets{};
    state.unit_refs.push_back(&source);
    for (u32 index = 0; index < targets.size(); ++index) {
        UnitMovementUnit& target = targets[index];
        target.id = 0x200 + index;
        target.active = true;
        target.runtime_flags = 1;
        // Original FUN_004f0629 gates on definition +0x49c (catalog +0x1f4),
        // not the adjacent catalog +0x1ec type flags.  Keep the two values
        // deliberately opposed so this regression catches that exact mixup.
        target.type_flags = 0x2233;
        target.definition.support_target_flags = 0x100;
        target.x = 105 + static_cast<i32>(index);
        target.y = 100;
        state.unit_refs.push_back(&target);
    }

    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = noxious.id;
    effect.source_unit_id = source.id;
    effect.x = source.x;
    effect.y = source.y;

    g_zero_damage_area_probe = {};
    ApplyUnitEffectAreaDamageByUnitFlagMask(state, effect, 0,
        noxious.action_area_damage_radius, 0x100);

    require(g_zero_damage_area_probe.count == targets.size(),
        "Noxious skipped zero-damage target reactions");
    for (u32 index = 0; index < targets.size(); ++index) {
        require(g_zero_damage_area_probe.target_ids[index] == targets[index].id,
            "Noxious changed active-list impact order");
    }
}

void test_successful_transport_unload_detaches_visible_passenger() {
    UnitMovementContext movement{};
    UnitMovementUnit passenger{};
    passenger.id = 156u * 0x1d0u;
    passenger.active = true;
    passenger.command_state = kUnitStateTransportUnloadStart;
    passenger.runtime_flags = 0x80u;
    passenger.attached_to_parent = true;
    passenger.definition.transport_size = 2;

    UnitMovementUnit carrier{};
    carrier.id = 191u * 0x1d0u;
    carrier.active = true;
    carrier.runtime_flags = 1u;
    carrier.x = 320;
    carrier.y = 640;
    carrier.definition.transport_offset_x = 16;
    carrier.definition.transport_offset_y = 24;
    carrier.cargo_amount = passenger.definition.transport_size;
    passenger.target = &carrier;

    movement.active_units = {&passenger, &carrier};
    UnitCommandContext context{};
    context.movement = &movement;
    context.callbacks.find_strict_placement_point =
        [](UnitCommandContext&, UnitMovementUnit&, UnitMovementPoint&) {
            return true;
        };

    ProcessTransportUnloadStart(context, passenger);

    require(passenger.command_state == kUnitStateRuntimeIdleAcquire &&
            (passenger.runtime_flags & 0x80u) == 0 &&
            (passenger.runtime_flags & 1u) != 0,
        "successful transport unload left the passenger hidden");
    require(!passenger.attached_to_parent,
        "successful transport unload retained reconstructed parent attachment");
    require(passenger.target == &carrier,
        "transport unload cleared the original raw carrier target residue");
    require(passenger.x == 336 && passenger.y == 664 &&
            carrier.cargo_amount == 0,
        "transport unload did not publish the placed passenger state");
}

void test_linked_release_count_rebuild_keeps_source_bucket_during_cycle() {
    UnitMovementContext movement{};
    UnitMovementUnit parent{};
    parent.active = true;
    parent.owner_id = 0;
    parent.type_id = 35;
    parent.command_state = kUnitStateLinkedUnitReleaseCycle;
    parent.cargo_amount = 34;
    UnitMovementUnit child{};
    child.active = true;
    child.owner_id = 0;
    child.type_id = 34;
    child.command_state = kUnitStateLinkedUnitReleaseCycle;
    child.runtime_flags = 0x80;
    movement.active_units = {&parent, &child};

    UnitLifecycleContext lifecycle{};
    lifecycle.movement = &movement;
    HandleOwnerUnitTypeCountRebuild(lifecycle);

    require(lifecycle.owner_unit_type_counts[0][34] == 2,
        "linked release moved the parent out of its source count bucket");
    require(lifecycle.owner_unit_type_counts[0][35] == 0,
        "linked release published its result count before cycle completion");
}

void test_unfinished_building_death_preserves_completed_type_count() {
    UnitLifecycleContext lifecycle{};
    lifecycle.owner_unit_type_counts[1][114] = 5;

    UnitMovementUnit unfinished{};
    unfinished.owner_id = 1;
    unfinished.type_id = 114;
    unfinished.action_mode_gate = 1;
    HandleUnitDeathOwnerCounters(lifecycle, unfinished);

    require(lifecycle.owner_building_lost_count[1] == 1,
        "unfinished building death did not update the building-loss counter");
    require(lifecycle.owner_unit_type_counts[1][114] == 5,
        "unfinished building death changed the completed-type count");

    UnitMovementUnit completed = unfinished;
    completed.action_mode_gate = 0;
    HandleUnitDeathOwnerCounters(lifecycle, completed);

    require(lifecycle.owner_building_lost_count[1] == 2,
        "completed building death did not update the building-loss counter");
    require(lifecycle.owner_unit_type_counts[1][114] == 4,
        "completed building death did not decrement the completed-type count");
}

void test_target_interaction_preserves_original_payload_unions() {
    UnitMovementUnit source{};
    source.id = 1;
    source.command_value = 2;
    source.path_target_x = 3;
    source.path_target_y = 900;
    source.active_command_payload.x = 2;
    UnitMovementUnit target{};
    target.id = 2;
    target.x = 640;
    target.y = 704;

    UnitMovementContext movement{};
    movement.active_units = {&source, &target};
    UnitCommandContext context{};
    context.movement = &movement;
    StartUnitTargetOrPointCommandEntry(context, source);

    require(source.cargo_amount == 3,
        "target interaction did not copy its slot payload to raw +0x4c");
    require(source.command_value == target.id,
        "target interaction overwrote its raw +0x68 target reference");
    require(source.target == &target && source.path_target_x == target.x &&
            source.path_target_y == target.y,
        "target interaction did not resolve the synchronized target point");
}

void test_special_target_interaction_uses_effect_adjusted_range() {
    UnitMovementUnit source{};
    source.type_id = kUnitTargetHelperSpecialSpawnType;
    source.owner_id = 0;
    source.x = 856;
    source.y = 2056;
    source.definition.effect_adjusted_interaction_range_base = 260;

    UnitMovementUnit target{};
    target.type_id = 0x60;
    target.x = 888;
    target.y = 2056;
    target.definition.interaction_bounds_width = 192;
    target.definition.interaction_bounds_height = 138;
    source.target = &target;

    ProductionOrderRuntimeState production{};
    production.completion_effect_totals[
        kProductionEffectSlotUnitInteractionRange][0][source.type_id] = 40;

    require(!CheckTargetInteractionNeedsApproach(source, &production, nullptr),
        "special target interaction ignored its adjusted interaction range");
    require(source.path_target_x == 984 && source.path_target_y == 2125,
        "special target interaction did not retain the target footprint center");
    require(CheckTargetInteractionNeedsApproach(source, nullptr, nullptr),
        "special target interaction did not apply the half-range distance gate");
}

void test_target_interaction_approach_updates_payload_point_union() {
    UnitMovementUnit source{};
    source.type_id = 17;
    source.command_state = kUnitStateTargetInteractionApproach;
    source.active_command_payload.state = kUnitStateTargetOrPointCommand;
    source.id = 1;
    source.active = true;
    source.active_command_payload.x = 2;
    source.active_command_payload.y = 1281;
    source.active_command_payload.value = 2056;
    source.x = 100;
    source.y = 100;

    UnitMovementUnit target{};
    target.id = 2;
    target.active = true;
    target.type_id = 1;
    target.x = 600;
    target.y = 700;
    source.target = &target;

    UnitMovementContext movement{};
    movement.active_units = {&source, &target};
    UnitCommandContext context{};
    context.movement = &movement;
    HandleUnitTargetInteractionApproach(context, source);

    require(source.active_command_payload.y == target.x &&
            source.active_command_payload.value == static_cast<u32>(target.y),
        "target approach did not update the active payload point union");
    require(source.saved_path_target_x == target.x &&
            source.saved_path_target_y == target.y,
        "target approach did not update the retained target point");
}

void test_equipment_remove_uses_definition_command_flag_gate() {
    UnitEquipmentCatalog catalog{};
    UnitEquipmentEffectDefinition effect{};
    effect.id = 88;
    effect.replacement_type_id = kInvalidUnitEquipmentType;
    effect.category = UnitEquipmentCategory::Generic;
    effect.generic_modifiers[kUnitEquipmentGenericModifierCommandFlag] = 1;
    catalog.effects.push_back(effect);

    UnitMovementUnit unit{};
    unit.type_flags = 0x2;
    unit.definition.footprint_flags = 0;
    unit.command_flags = 0x40;
    unit.equipment_slots[0] = effect.id;
    UnitCommandContext context{};

    require(RemoveUnitEquipmentEffect(context, unit, effect, &catalog),
        "equipment effect removal was rejected");
    require((unit.command_flags & 0x40u) == 0,
        "equipment removal tested mutable type flags instead of definition flags");
}

} // namespace

int main() {
    test_zero_damage_unit_flag_area_preserves_reactions();
    test_successful_transport_unload_detaches_visible_passenger();
    test_linked_release_count_rebuild_keeps_source_bucket_during_cycle();
    test_unfinished_building_death_preserves_completed_type_count();
    test_target_interaction_preserves_original_payload_unions();
    test_special_target_interaction_uses_effect_adjusted_range();
    test_target_interaction_approach_updates_payload_point_union();
    test_equipment_remove_uses_definition_command_flag_gate();
    std::cout << "TRANSPORT_NOXIOUS_REGRESSION_PASS\n";
    return EXIT_SUCCESS;
}
