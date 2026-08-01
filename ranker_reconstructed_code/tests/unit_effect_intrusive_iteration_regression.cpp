#include "ranker_unit_action.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "UNIT_EFFECT_INTRUSIVE_ITERATION_FAIL " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void seed_definitions(UnitEffectRuntimeState& state) {
    UnitEffectDefinition blessing{};
    blessing.id = 0x4e;
    blessing.active_frames = 32;
    blessing.impact_frames = {0};

    UnitEffectDefinition shield{};
    shield.id = 0x3f;
    shield.active_frames = 32;

    UnitEffectDefinition linked_drain{};
    linked_drain.id = 0x3d;
    linked_drain.active_frames = 32;
    state.definitions = {blessing, shield, linked_drain};
}

void seed_linked_unit(UnitEffectRuntimeState& state) {
    UnitMovementUnit linked{};
    linked.id = 0x1234;
    linked.active = true;
    linked.runtime_flags = 0x100;
    linked.health = 100;
    linked.x = 91;
    linked.y = 117;
    state.units.push_back(linked);
}

void seed_blessing(UnitEffectRuntime& effect) {
    effect.active = true;
    effect.effect_id = 0x4e;
    effect.flags = kUnitEffectFlagImpact;
    effect.linked_unit_id = 0x1234;
}

void seed_shield(UnitEffectRuntime& effect, bool active, u32 amount) {
    effect.active = active;
    effect.effect_id = 0x3f;
    effect.flags = kUnitEffectFlagImpact;
    effect.linked_unit_id = 0x1234;
    effect.amount = amount;
}

void seed_linked_drain(UnitEffectRuntime& effect, bool active, u32 frame) {
    effect.active = active;
    effect.effect_id = 0x3d;
    effect.flags = kUnitEffectFlagImpact;
    effect.linked_unit_id = 0x1234;
    effect.frame = frame;
}

void test_cached_next_continues_through_free_tail() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 4;
    state.effect_slots.resize(4);
    state.active_effect_indices = {0, 1};
    state.free_effect_indices = {2, 3};
    seed_definitions(state);
    seed_linked_unit(state);
    seed_blessing(state.effect_slots[0]);
    seed_shield(state.effect_slots[1], true, 6);
    seed_shield(state.effect_slots[2], false, 8);
    seed_shield(state.effect_slots[3], false, 10);

    TickUnitEffectRuntimeList(state);

    require(state.effect_slots[1].amount == 5,
        "cached released node did not receive its stale tick");
    require(state.effect_slots[2].amount == 7 &&
            state.effect_slots[3].amount == 9,
        "cached node did not continue through its updated free-list next chain");
    require(state.active_effect_indices.size() == 1 &&
            state.active_effect_indices.front() == 0,
        "surviving active-list topology changed");
    require(state.free_effect_indices == std::vector<std::size_t>({1, 2, 3}),
        "free-list head-to-tail order changed");
}

void test_unlinked_non_next_node_is_not_visited_from_entry_snapshot() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 4;
    state.effect_slots.resize(4);
    state.active_effect_indices = {0, 1, 2};
    state.free_effect_indices = {3};
    seed_definitions(state);
    seed_linked_unit(state);
    seed_blessing(state.effect_slots[0]);
    seed_linked_drain(state.effect_slots[1], true, 6);
    seed_linked_drain(state.effect_slots[2], true, 8);
    seed_linked_drain(state.effect_slots[3], false, 10);

    TickUnitEffectRuntimeList(state);

    require(state.effect_slots[1].frame == 7,
        "saved immediate-next node did not receive its stale tick");
    require(state.effect_slots[2].frame == 8,
        "unlinked non-next node was incorrectly visited from an entry snapshot");
    require(state.effect_slots[3].frame == 11,
        "saved node did not follow its current free-list successor");
    require(state.free_effect_indices == std::vector<std::size_t>({2, 1, 3}),
        "multi-release free-list order changed");
}

struct ThunderImpactProbe {
    u32 count = 0;
    std::array<i32, 2> x{};
    std::array<i32, 2> y{};
};

ThunderImpactProbe g_thunder_impact_probe;

bool apply_thunder_impact_synchronously(UnitEffectRuntimeState& state,
    const UnitEffectEvent& event) {
    if (event.kind != UnitEffectEventKind::impact) {
        return false;
    }
    require(g_thunder_impact_probe.count < 2,
        "unexpected extra thunder impact");
    const u32 index = g_thunder_impact_probe.count++;
    g_thunder_impact_probe.x[index] = event.x;
    g_thunder_impact_probe.y[index] = event.y;
    if (index == 0) {
        for (UnitMovementUnit* unit : state.unit_refs) {
            if (unit != nullptr && unit->id == event.target_id) {
                unit->x = 300;
                unit->y = 400;
            }
        }
    }
    return true;
}

UnitEffectDefinition thunder_definition() {
    UnitEffectDefinition thunder{};
    thunder.id = 0x3e;
    thunder.active_frames = 10;
    thunder.damage_amount = 150;
    thunder.action_area_damage_radius = 0;
    thunder.impact_frames = {4};
    return thunder;
}

void seed_thunder(UnitEffectRuntime& effect, u32 source_id, u32 target_id) {
    effect.active = true;
    effect.effect_id = 0x3e;
    effect.flags = kUnitEffectFlagImpact;
    effect.tick = 4;
    effect.frame = 4;
    effect.amount = 150;
    effect.source_unit_id = source_id;
    effect.target_unit_id = target_id;
    effect.linked_unit_id = target_id;
}

void test_repeated_thunder_impacts_are_applied_during_list_walk() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 2;
    state.effect_slots.resize(2);
    state.active_effect_indices = {0, 1};
    state.definitions = {thunder_definition()};
    state.callbacks.apply_simulation_event = apply_thunder_impact_synchronously;

    UnitMovementUnit source{};
    source.id = 0x111;
    source.active = true;
    UnitMovementUnit target{};
    target.id = 0x222;
    target.active = true;
    target.x = 100;
    target.y = 200;
    target.definition.render_class = 1;
    target.definition.center_bounds_left = 2;
    target.definition.center_bounds_top = 4;
    target.definition.center_bounds_width = 20;
    target.definition.center_bounds_height = 10;
    state.unit_refs = {&source, &target};

    seed_thunder(state.effect_slots[0], source.id, target.id);
    seed_thunder(state.effect_slots[1], source.id, target.id);
    g_thunder_impact_probe = {};

    TickUnitEffectRuntimeList(state);

    require(g_thunder_impact_probe.count == 2,
        "repeated thunder impacts were deferred until after list traversal");
    require(g_thunder_impact_probe.x[0] == 112 &&
            g_thunder_impact_probe.y[0] == 209,
        "first thunder impact did not use the initial target center");
    require(g_thunder_impact_probe.x[1] == 312 &&
            g_thunder_impact_probe.y[1] == 409,
        "second thunder impact did not observe first-impact mutation");
    require(state.events.empty(),
        "synchronously consumed thunder impacts leaked into the deferred queue");
}

void test_missing_thunder_target_keeps_natural_lifetime_and_payload() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 1;
    state.effect_slots.resize(1);
    state.active_effect_indices = {0};
    state.definitions = {thunder_definition()};
    seed_thunder(state.effect_slots[0], 0x111, 0x999);
    state.effect_slots[0].x = 77;
    state.effect_slots[0].y = 88;

    TickUnitEffectRuntimeList(state);

    require(state.effect_slots[0].active &&
            state.effect_slots[0].frame == 5 &&
            state.effect_slots[0].tick == 5,
        "missing thunder target released before its natural lifetime");
    require(state.effect_slots[0].x == 77 && state.effect_slots[0].y == 88,
        "missing thunder target discarded its last impact point");

    for (u32 tick = 0; tick < 5; ++tick) {
        TickUnitEffectRuntimeList(state);
    }

    require(!state.effect_slots[0].active &&
            state.active_effect_indices.empty() &&
            state.free_effect_indices == std::vector<std::size_t>({0}),
        "thunder effect did not enter the free list at frame ten");
    require(state.effect_slots[0].frame == 10 &&
            state.effect_slots[0].tick == 10,
        "thunder release cleared the original final frame/tick payload");
}

bool apply_thunder_shield_break_synchronously(UnitEffectRuntimeState& state,
    const UnitEffectEvent& event) {
    if (event.kind != UnitEffectEventKind::impact) {
        return false;
    }
    require(state.effect_slots.size() >= 2,
        "shield-break probe has no shield slot");
    for (UnitMovementUnit* unit : state.unit_refs) {
        if (unit != nullptr && unit->id == event.target_id) {
            unit->runtime_flags &= ~0x100u;
        }
    }
    ReleaseUnitEffectSlot(state, state.effect_slots[1]);
    return true;
}

void test_thunder_shield_break_double_release_is_idempotent() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 4;
    state.effect_slots.resize(4);
    state.active_effect_indices = {0, 1, 2};
    state.free_effect_indices = {3};
    seed_definitions(state);
    state.definitions.push_back(thunder_definition());
    seed_linked_unit(state);
    state.unit_refs = {&state.units.front()};
    seed_thunder(state.effect_slots[0], 0x111, 0x1234);
    seed_shield(state.effect_slots[1], true, 1);
    seed_linked_drain(state.effect_slots[2], true, 8);
    seed_shield(state.effect_slots[3], false, 3);
    state.callbacks.apply_simulation_event =
        apply_thunder_shield_break_synchronously;
    state.frame_counter = 0;

    TickUnitEffectRuntimeList(state);

    require(state.active_effect_indices ==
            std::vector<std::size_t>({0, 2}),
        "thunder shield break corrupted the surviving active list");
    require(state.free_effect_indices ==
            std::vector<std::size_t>({1, 3}),
        "stale shield double release duplicated or looped the free list");
    require(state.effect_slots[1].amount == 0,
        "released shield did not receive the original stale dispatch");
    require(state.effect_slots[3].amount == 2,
        "released shield did not continue through the free tail");
    require(state.effect_slots[2].frame == 8,
        "unlinked active successor was unexpectedly dispatched");
}

} // namespace

int main() {
    test_cached_next_continues_through_free_tail();
    test_unlinked_non_next_node_is_not_visited_from_entry_snapshot();
    test_repeated_thunder_impacts_are_applied_during_list_walk();
    test_missing_thunder_target_keeps_natural_lifetime_and_payload();
    test_thunder_shield_break_double_release_is_idempotent();
    std::cout << "UNIT_EFFECT_INTRUSIVE_ITERATION_PASS\n";
    return EXIT_SUCCESS;
}
