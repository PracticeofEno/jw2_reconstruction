#include "ranker_unit_action.h"
#include "ranker_unit_commands.h"

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

} // namespace

int main() {
    test_zero_damage_unit_flag_area_preserves_reactions();
    test_successful_transport_unload_detaches_visible_passenger();
    std::cout << "TRANSPORT_NOXIOUS_REGRESSION_PASS\n";
    return EXIT_SUCCESS;
}
