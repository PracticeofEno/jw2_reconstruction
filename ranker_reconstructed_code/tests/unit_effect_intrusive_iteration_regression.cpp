#include "ranker_unit_action.h"
#include "ranker_unit_damage.h"
#include "ranker_unit_lifecycle.h"
#include "ranker_unit_spatial_index.h"

#include <algorithm>
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

void test_action9_counts_down_raw_effect_30_accumulator() {
    UnitEffectRuntimeState state{};
    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = 0x46;
    effect.flags = kUnitEffectFlagImpact;
    effect.abs_delta_x = 62;
    effect.accumulator_x = 73;

    DispatchUnitActionEffectCommand(state, effect, 9);

    require(effect.accumulator_x == 72,
        "action 9 did not decrement original raw effect +0x30");
    require(effect.abs_delta_x == 62,
        "action 9 changed fixed raw effect +0x38 distance");
}

void test_area_stun_preserves_independent_unit_timers() {
    UnitEffectRuntimeState state{};

    UnitEffectDefinition definition{};
    definition.id = 0x42;
    definition.active_frames = 8;
    definition.damage_amount = 100;
    definition.action_area_damage_radius = 64;
    state.definitions.push_back(definition);

    UnitMovementUnit unit{};
    unit.id = 110;
    unit.active = true;
    unit.x = 100;
    unit.y = 100;
    unit.command_lockout_ticks = 59;
    unit.command_entry_lockout_ticks = 37;
    unit.animation_timer = 12;
    state.unit_refs.push_back(&unit);

    UnitEffectRuntime effect{};
    effect.active = true;
    effect.effect_id = definition.id;
    effect.flags = kUnitEffectFlagImpact;
    effect.x = unit.x;
    effect.y = unit.y;

    TickUnitEffectAreaStunFrames(state, effect);

    require(unit.command_state == kUnitStateRandomRelocation,
        "area stun did not enter raw command state 0x6c");
    require(unit.command_lockout_ticks == 59 &&
            unit.command_entry_lockout_ticks == 37 &&
            unit.animation_timer == 12,
        "area stun cleared independent raw +0xf4/+0x98/+0xec timers");
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
            BreakUnitRuntimeShield(state, *unit);
        }
    }
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
    state.units.front().linked_effect_slot_offset = 2u * 0xa8u;
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
    require((state.units.front().runtime_flags & 0x100u) == 0,
        "shield break did not clear the live runtime flag before reaction mirroring");
    require(state.effect_slots[3].amount == 2,
        "released shield did not continue through the free tail");
    require(state.effect_slots[2].frame == 8,
        "unlinked active successor was unexpectedly dispatched");
}

struct HurdleCreateProbe {
    u32 count = 0;
    u32 type_id = 0;
    i32 x = 0;
    i32 y = 0;
    UnitMovementUnit created;
};

HurdleCreateProbe g_hurdle_create_probe;

UnitMovementUnit* create_hurdle_probe(UnitEffectRuntimeState&,
    UnitMovementUnit&, u32 type_id, i32 x, i32 y) {
    ++g_hurdle_create_probe.count;
    g_hurdle_create_probe.type_id = type_id;
    g_hurdle_create_probe.x = x;
    g_hurdle_create_probe.y = y;
    g_hurdle_create_probe.created.max_health = 350;
    return &g_hurdle_create_probe.created;
}

void test_hurdle_impact_creates_type_125_unit() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 1;
    state.effect_slots.resize(1);
    state.active_effect_indices = {0};
    state.callbacks.create_unit = create_hurdle_probe;

    UnitEffectDefinition hurdle{};
    hurdle.id = 0x54u;
    hurdle.active_frames = 10;
    hurdle.impact_frames = {7};
    hurdle.action_create_unit_type_id = 125;
    hurdle.action_create_unit_secondary_value = 25;
    state.definitions = {hurdle};

    UnitMovementUnit source{};
    source.id = 0x111u;
    source.owner_id = 1;
    source.active = true;
    state.unit_refs = {&source};

    UnitEffectRuntime& effect = state.effect_slots.front();
    effect.active = true;
    effect.effect_id = 0x54u;
    effect.flags = kUnitEffectFlagImpact;
    effect.source_unit_id = source.id;
    effect.frame = 7;
    effect.tick = 7;
    effect.x = 2368;
    effect.y = 832;
    g_hurdle_create_probe = {};

    TickUnitEffectRuntimeList(state);

    require(g_hurdle_create_probe.count == 1,
        "Hurdle impact frame did not invoke placed-unit creation exactly once");
    require(g_hurdle_create_probe.type_id == 125 &&
            g_hurdle_create_probe.x == 2368 &&
            g_hurdle_create_probe.y == 832,
        "Hurdle impact did not create type 125 at the aligned target point");
    require(g_hurdle_create_probe.created.health == 350 &&
            g_hurdle_create_probe.created.max_secondary_value == 25 &&
            g_hurdle_create_probe.created.secondary_value == 25,
        "Hurdle impact did not initialize original health/resource values");
}

void test_final_path_budget_point_still_scans_projectile_collisions() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 1;
    state.effect_slots.resize(1);
    state.active_effect_indices = {0};

    UnitEffectDefinition projectile{};
    projectile.id = 4;
    // JW2_12 behavior bit four invokes FUN_004f30bd's per-point unit scan.
    projectile.behavior_flags = 0x04u;
    projectile.active_step_iterations = 1;
    projectile.allowed_target_render_class_mask = 1u;
    state.definitions = {projectile};

    UnitMovementUnit source{};
    source.id = 98832;
    source.owner_id = 0;
    source.active = true;

    UnitMovementUnit victim{};
    victim.id = 90480;
    victim.owner_id = 1;
    victim.active = true;
    victim.health = 370;
    victim.x = 1197;
    victim.y = 894;
    victim.definition.render_class = 0;
    victim.definition.center_bounds_left = -10;
    victim.definition.center_bounds_top = -44;
    victim.definition.center_bounds_width = 24;
    victim.definition.center_bounds_height = 54;

    UnitMovementUnit intended_target{};
    intended_target.id = 92336;
    intended_target.owner_id = 1;
    intended_target.active = true;
    intended_target.x = 1103;
    intended_target.y = 809;
    state.unit_refs = {&source, &victim, &intended_target};

    UnitEffectRuntime& effect = state.effect_slots.front();
    effect.active = true;
    effect.effect_id = 4;
    effect.amount = 51;
    effect.source_unit_id = source.id;
    effect.target_unit_id = intended_target.id;
    effect.x = 1211;
    effect.y = 904;
    effect.range = 1;

    TickUnitEffectRuntimeList(state);

    const auto impact = std::find_if(state.events.begin(), state.events.end(),
        [&](const UnitEffectEvent& event) {
            return event.kind == UnitEffectEventKind::impact &&
                event.target_id == victim.id && event.value == 51;
        });
    require(impact != state.events.end(),
        "final path-budget point skipped its intervening-unit collision");
    require(!effect.active,
        "final path-budget collision did not preserve projectile release");
}

void test_target_marker_impact_advances_raw_tick_word() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 1;
    state.effect_slots.resize(1);
    state.active_effect_indices = {0};

    UnitEffectDefinition marker{};
    marker.id = 0x27u;
    marker.active_frames = 1;
    marker.impact_render_ticks = 3;
    state.definitions = {marker};

    UnitMovementUnit target{};
    target.id = 0x222u;
    target.active = true;
    target.x = 1216;
    target.y = 3286;
    state.unit_refs = {&target};

    UnitEffectRuntime& effect = state.effect_slots.front();
    effect.active = true;
    effect.effect_id = marker.id;
    effect.flags = kUnitEffectFlagImpact;
    effect.target_unit_id = target.id;
    effect.tick = 0;
    effect.frame = 0;

    TickUnitEffectRuntimeList(state);

    require(effect.active && effect.tick == 1 && effect.frame == 0,
        "effect-0x27 advanced raw +0x10 instead of raw +0x0c");
    require(effect.x == target.x && effect.y == target.y,
        "effect-0x27 did not retain the linked target position");

    TickUnitEffectRuntimeList(state);
    TickUnitEffectRuntimeList(state);

    require(!effect.active && effect.tick == 3 && effect.frame == 0,
        "effect-0x27 lifetime did not use impact-render ticks with raw +0x0c");
}

void test_target_marker_releases_on_transient_target_reach() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 1;
    state.effect_slots.resize(1);
    state.active_effect_indices = {0};

    UnitEffectDefinition marker{};
    marker.id = 0x27u;
    marker.active_step_iterations = 1;
    state.definitions = {marker};

    UnitMovementUnit target{};
    target.id = 0x223u;
    target.active = true;
    target.runtime_flags = kUnitActionTargetTransient;
    target.x = 2828;
    target.y = 410;
    state.unit_refs = {&target};

    UnitEffectRuntime& effect = state.effect_slots.front();
    effect.active = true;
    effect.effect_id = marker.id;
    effect.target_unit_id = target.id;
    effect.x = target.x;
    effect.y = target.y;
    effect.target_x = target.x;
    effect.target_y = target.y;
    effect.previous_x = target.x;
    effect.previous_y = target.y;
    effect.closest_distance = 0;
    effect.range = 2;

    TickUnitEffectRuntimeList(state);

    require(!effect.active,
        "effect-0x27 retained a transient target after its dedicated reach route");
    require(state.active_effect_indices.empty() &&
            state.free_effect_indices == std::vector<std::size_t>({0}),
        "effect-0x27 transient reach did not restore the original pool topology");
}

void test_reserved_tile_transient_dropoff_reroute_uses_effect_offset_alias() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 1;
    state.effect_slots.resize(1);
    state.active_effect_indices = {0};

    UnitEffectDefinition berry{};
    berry.id = 0x63u;
    berry.action_projectile_loop_ticks = 1;
    state.definitions = {berry};

    UnitMovementUnit source{};
    source.id = 223u * 0x1d0u;
    source.owner_id = 1;
    source.active = true;
    source.command_state = kUnitStateReservedTileLinkedObject;
    source.x = 159;
    source.y = 279;

    UnitMovementUnit transient_dropoff{};
    transient_dropoff.id = 287u * 0x1d0u;
    transient_dropoff.owner_id = 1;
    // The linked transient may no longer be a valid dropoff definition by the
    // time the effect reroutes.  Keep it outside the fallback dropoff classes
    // so the test isolates the replacement-path write observed in replay 2.
    transient_dropoff.type_id = 0x71u;
    transient_dropoff.active = true;
    transient_dropoff.runtime_flags = kUnitActionTargetTransient;
    transient_dropoff.x = 320;
    transient_dropoff.y = 288;

    UnitMovementUnit replacement_dropoff{};
    replacement_dropoff.id = 145u * 0x1d0u;
    replacement_dropoff.owner_id = 1;
    replacement_dropoff.type_id = 0x70u;
    replacement_dropoff.active = true;
    replacement_dropoff.runtime_flags = 1;
    replacement_dropoff.x = 320;
    replacement_dropoff.y = 2976;

    state.unit_refs = {&source, &transient_dropoff, &replacement_dropoff};
    UnitEffectRuntime& effect = state.effect_slots.front();
    effect.active = true;
    effect.effect_id = 0x63u;
    effect.source_unit_id = source.id;
    effect.target_unit_id = transient_dropoff.id;
    effect.linked_unit_id = transient_dropoff.id;
    effect.x = 159;
    effect.y = 279;

    TickUnitEffectRuntimeList(state);

    require(effect.source_unit_id == 0xa8u,
        "Berry transient-dropoff reroute did not write its raw effect offset alias");
    require(effect.target_unit_id == replacement_dropoff.id,
        "Berry transient-dropoff reroute did not select the replacement dropoff");
}

void test_recharge_aura_persists_when_frame_reaches_limit() {
    UnitEffectRuntimeState state{};
    state.effect_slot_capacity = 1;
    state.effect_slots.resize(1);
    state.active_effect_indices = {0};

    UnitEffectDefinition recharge{};
    recharge.id = 0x57u;
    recharge.active_frames = 21;
    recharge.damage_amount = 100;
    recharge.action_aura_frame_limit = 170;
    recharge.action_aura_tick_reset_value = 4;
    recharge.action_aura_tick_reset_threshold = 16;
    state.definitions = {recharge};

    UnitEffectRuntime& effect = state.effect_slots.front();
    effect.active = true;
    effect.effect_id = 0x57u;
    effect.flags = kUnitEffectFlagImpact;
    effect.frame = 169;
    effect.tick = 14;

    TickUnitEffectRuntimeList(state);

    require(effect.active && effect.frame == 170 && effect.tick == 15,
        "Recharge aura was released when its frame reached the hold limit");

    effect.tick = 16;
    TickUnitEffectRuntimeList(state);
    require(effect.active && effect.frame == 170 && effect.tick == 17,
        "Recharge aura reset its tick after entering the held-frame path");
    TickUnitEffectRuntimeList(state);
    TickUnitEffectRuntimeList(state);
    TickUnitEffectRuntimeList(state);
    TickUnitEffectRuntimeList(state);
    require(!effect.active,
        "Recharge aura did not release at its held-frame tick limit");
}

UnitMovementDefinition g_recycled_slot_definition;

const UnitMovementDefinition* find_recycled_slot_definition(
    UnitLifecycleContext&, u32 type_id) {
    return type_id == 49 || type_id == 96
        ? &g_recycled_slot_definition
        : nullptr;
}

bool keep_recycled_slot_placement(UnitLifecycleContext&, UnitMovementUnit&,
    i32&, i32&) {
    return true;
}

void test_placed_unit_clears_recycled_elite_progress() {
    g_recycled_slot_definition = {};
    g_recycled_slot_definition.initial_max_health = 170;

    UnitLifecycleContext context{};
    context.callbacks.find_definition = find_recycled_slot_definition;
    context.callbacks.find_placement = keep_recycled_slot_placement;

    UnitMovementUnit unit{};
    unit.elite_progress_value = 11;

    require(InitializePlacedUnitFromMapSlot(context, unit, 49, 0, 3468, 2899),
        "recycled fixed-pool unit could not be initialized");
    require(unit.elite_progress_value == 0,
        "placed unit retained raw +0x50 from its previous pool generation");
}

void test_placed_unit_clears_recycled_movement_interpolation() {
    g_recycled_slot_definition = {};
    g_recycled_slot_definition.initial_max_health = 410;

    UnitLifecycleContext context{};
    context.callbacks.find_definition = find_recycled_slot_definition;
    context.callbacks.find_placement = keep_recycled_slot_placement;

    UnitMovementUnit unit{};
    unit.movement_interpolation_x = 2858.593f;
    unit.movement_interpolation_y = 1108.513f;

    require(InitializePlacedUnitFromMapSlot(context, unit, 49, 0, 3776, 768),
        "recycled interpolation unit could not be initialized");
    require(unit.movement_interpolation_x == 0.0f &&
            unit.movement_interpolation_y == 0.0f,
        "placed unit retained raw +0x11c/+0x120 from its previous generation");
}

void test_placed_building_preserves_recycled_destination_union() {
    g_recycled_slot_definition = {};
    g_recycled_slot_definition.initial_max_health = 3000;

    UnitLifecycleContext context{};
    context.callbacks.find_definition = find_recycled_slot_definition;
    context.callbacks.find_placement = keep_recycled_slot_placement;

    UnitMovementUnit unit{};
    unit.type_id = 16;
    unit.destination_x = 3711;
    unit.destination_y = 3983;
    unit.cell_channel_additive_frame = 0;
    unit.cell_flag40_animation_frame = 0;

    require(InitializePlacedUnitFromMapSlot(context, unit, 96, 0, 3616, 1280),
        "recycled building fixed-pool unit could not be initialized");
    require(unit.cell_channel_additive_frame == 3711 &&
            unit.cell_flag40_animation_frame == 3983,
        "mobile-to-building reuse lost raw +0x78/+0x7c residue");
}

void test_transient_lifecycle_uses_shared_jw211_period() {
    UnitLifecycleContext context{};
    SetUnitLifecycleTransientObjectPeriod(9);

    UnitMovementUnit unit{};
    unit.runtime_flags = 0x10;
    unit.work_timer = 0;
    // The original branch ignores this per-unit field and reads the shared
    // JW2_11 record-8 raw +0x20c value instead.
    unit.definition.production_cycle_period = 1;

    for (u32 tick = 0; tick < 8; ++tick) {
        HandleUnitLifecycleGrowthOrDecay(context, unit);
        require((unit.command_flags & 0x80u) == 0,
            "transient lifecycle used the per-unit cycle period");
    }
    HandleUnitLifecycleGrowthOrDecay(context, unit);
    require((unit.command_flags & 0x80u) != 0 && unit.work_timer == 0xdcu,
        "transient lifecycle did not use the shared JW2_11 period");
}

UnitMovementUnit g_in_range_guard_target;

UnitMovementUnit* find_in_range_guard_target(UnitCommandContext&,
    UnitMovementUnit&) {
    return &g_in_range_guard_target;
}

bool allow_in_range_guard_target(UnitCommandContext&, UnitMovementUnit&,
    UnitMovementUnit&) {
    return true;
}

void test_in_range_guard_acquisition_preserves_command_path() {
    g_in_range_guard_target = {};
    g_in_range_guard_target.id = 230u * 0x1d0u;
    g_in_range_guard_target.active = true;
    g_in_range_guard_target.x = 2329;
    g_in_range_guard_target.y = 2930;

    UnitCommandContext context{};
    context.callbacks.find_target = find_in_range_guard_target;
    context.callbacks.can_attack_target = allow_in_range_guard_target;
    context.callbacks.target_in_action_range = allow_in_range_guard_target;

    UnitMovementUnit anchor_guard{};
    anchor_guard.id = 174u * 0x1d0u;
    anchor_guard.command_value = 0;
    anchor_guard.path_target_x = 1711;
    anchor_guard.path_target_y = 2755;
    StartUnitGuardAnchorCommand(context, anchor_guard);

    require(anchor_guard.command_state == kUnitStateGuardCombatCycle &&
            anchor_guard.target == &g_in_range_guard_target &&
            anchor_guard.command_value == g_in_range_guard_target.id,
        "in-range guard command did not acquire its scanned target");
    require(anchor_guard.path_target_x == 1711 &&
            anchor_guard.path_target_y == 2755,
        "state-0x1c in-range acquisition overwrote the command path");

    UnitMovementUnit return_guard{};
    return_guard.id = 175u * 0x1d0u;
    return_guard.path_target_x = 1400;
    return_guard.path_target_y = 2600;
    StartUnitGuardReturnCommand(context, return_guard);

    require(return_guard.command_state == kUnitStateGuardCombatCycle &&
            return_guard.target == &g_in_range_guard_target,
        "in-range guard-return command did not acquire its scanned target");
    require(return_guard.path_target_x == 1400 &&
            return_guard.path_target_y == 2600,
        "state-0x1f in-range acquisition overwrote the return path");
}

void test_transport_unload_command_resolves_carrier_payload() {
    constexpr u32 kOriginalUnitPoolStride = 0x1d0u;

    UnitMovementContext movement{};
    UnitMovementUnit passenger{};
    passenger.id = 156u * kOriginalUnitPoolStride;
    passenger.runtime_slot_index = 156;
    passenger.active = true;
    passenger.command_state = kUnitStateTransportAttached;
    passenger.runtime_flags = 0x80u;

    UnitMovementUnit carrier{};
    carrier.id = 191u * kOriginalUnitPoolStride;
    carrier.runtime_slot_index = 191;
    carrier.active = true;
    carrier.runtime_flags = 1u;
    passenger.target = &carrier;

    movement.active_units = {&passenger, &carrier};
    UnitCommandContext context{};
    context.movement = &movement;

    // Replay 22 frame 7976 promotes a wire command whose mirror-clear flag
    // masks to selector 0x24.  Original 0x004cfe78..0x004cfec0 copies its raw
    // target word before entering state 0x41 at 0x004d02c5, so the attached
    // passenger must retain/resolve the carrier used by the unload tick.
    passenger.pending_command.state =
        kUnitCommandMirrorClearFlag | 0x24u;
    passenger.pending_command.x = static_cast<i32>(carrier.id);
    passenger.pending_command.y = 614;
    passenger.pending_command.value = 760;

    HandlePendingUnitCommandDispatch(context, passenger);

    require(passenger.target == &carrier &&
            passenger.command_state == kUnitStateTransportUnloadStart,
        "selector-0x24 cleared its carrier target before unload");
}

void test_relative_spatial_box_uses_frame_start_source_anchor() {
    UnitMovementUnit source{};
    source.id = 187;
    source.active = true;
    source.x = 3716;
    source.y = 3332;

    UnitMovementUnit candidate{};
    candidate.id = 202;
    candidate.active = true;
    candidate.x = 3617;
    candidate.y = 3579;

    UnitSpatialIndex index{};
    InitializeUnitSpatialIndex(index);
    RebuildUnitSpatialIndex(index, {&source, &candidate},
        UnitSpatialIndexBuildMode::all_active_units);

    // A same-frame carrier unload moves the source but deliberately leaves
    // the spatial index at its frame-start X/order.
    source.x = 3552;
    source.y = 3584;
    UnitMovementUnit* found = QueryUnitSpatialIndexRelativeBox(
        index, &source, -20, 20, -20, 20);

    require(found == &candidate,
        "relative spatial query discarded the frame-start anchored candidate");
}

void test_follow_command_preserves_zeroed_free_pool_target() {
    constexpr u32 kOriginalUnitPoolStride = 0x1d0u;

    UnitMovementContext movement{};
    UnitMovementUnit follower{};
    follower.id = 177u * kOriginalUnitPoolStride;
    follower.runtime_slot_index = 177;
    follower.active = true;
    follower.health = 100;

    UnitMovementUnit target{};
    target.id = 223u * kOriginalUnitPoolStride;
    target.runtime_slot_index = 223;
    target.type_id = 0;
    target.active = false;

    movement.active_units = {&follower};
    movement.free_units = {&target};
    UnitCommandContext context{};
    context.movement = &movement;

    follower.pending_command.state = 0x04;
    follower.pending_command.x = static_cast<i32>(target.id);
    follower.pending_command.y = target.x;
    follower.pending_command.value = static_cast<u32>(target.y);

    HandlePendingUnitCommandDispatch(context, follower);

    require(follower.target == &target &&
            follower.command_state == kUnitStateCommand14,
        "selector-0x04 discarded its zeroed free-pool wire target");

    // State 0x14 immediately dispatches the follow start in the same runtime
    // tick.  Original 0x004c9840 writes 0x16 and jumps to 0x004c987f; at zero
    // distance that handler reaches 0x004c990e and finishes in hold state
    // 0x17.  The free slot remains a valid target because its type is zero
    // and its raw +0xa0 flags do not contain 0x84.
    context.movement = nullptr;
    ProcessUnitFollowTargetStart(context, follower);
    require(follower.target == &target &&
            follower.command_state == kUnitStateFollowHoldRange,
        "follow start did not retain the zero-distance free-pool target");
}

void test_target_progress_uses_zeroed_free_pool_target_definition() {
    constexpr u32 kOriginalUnitPoolStride = 0x1d0u;

    UnitMovementContext movement{};
    UnitMovementUnit source{};
    source.id = 171u * kOriginalUnitPoolStride;
    source.runtime_slot_index = 171;
    source.active = true;
    source.x = 1264;
    source.y = 2515;
    source.active_command_payload.state = 0x03;
    source.active_command_payload.x =
        static_cast<i32>(241u * kOriginalUnitPoolStride);

    UnitMovementUnit target{};
    target.id = 241u * kOriginalUnitPoolStride;
    target.runtime_slot_index = 241;
    target.type_id = 0;
    target.active = false;
    target.definition.center_bounds_left = 16;
    target.definition.center_bounds_top = 240;

    movement.active_units = {&source};
    movement.free_units = {&target};
    UnitCommandContext context{};
    context.movement = &movement;

    StartUnitTargetProgressCommand(context, source);

    require(source.target == &target &&
            source.command_state == kUnitStateTargetProgressApproach,
        "state-0x10 rejected its retained free-pool target");
    require(source.path_target_x == 16 && source.path_target_y == 240,
        "free-pool target did not use its type-0 catalog center");
}

UnitMovementDefinition g_raw_pool_alias_definition;

const UnitMovementDefinition* find_raw_pool_alias_definition(
    UnitCommandContext&, u32 type_id) {
    return type_id == 0 ? &g_raw_pool_alias_definition : nullptr;
}

void test_spawn_cycle_preserves_misaligned_raw_pool_alias() {
    g_raw_pool_alias_definition = {};
    g_raw_pool_alias_definition.center_bounds_left = 16;
    g_raw_pool_alias_definition.center_bounds_top = 240;

    UnitCommandContext context{};
    context.callbacks.find_definition = find_raw_pool_alias_definition;

    UnitMovementUnit builder{};
    builder.type_id = 16;
    builder.x = 528;
    builder.y = 324;
    builder.command_state = kUnitStateSpawnCreateCycle;
    builder.command_value = 116;
    builder.spawn_type_id = 116;
    builder.animation_frame = 4;
    builder.definition.spawn_frame_count = 4;
    builder.active_command_payload.state = 0x06;
    builder.active_command_payload.x = 20;
    builder.active_command_payload.y = 448;
    builder.active_command_payload.value = 416;

    HandleUnitSpawnCreateCycle(context, builder);

    require(builder.command_state == kUnitStateSpawnCreateCycle &&
            builder.active_command_payload.state == 0x06 &&
            builder.animation_frame == 4 && builder.target == nullptr,
        "state-0x5b popped its misaligned raw fixed-pool alias");
}

bool access_dead_spawn_alias(UnitCommandContext&, u32 raw_unit_offset,
    UnitMovementUnit& alias, bool write_back) {
    require(raw_unit_offset == 116,
        "state-0x5b changed the raw alias offset before the +0xa0 read");
    if (!write_back) {
        alias.runtime_flags = 4;
    }
    return true;
}

void test_spawn_cycle_pops_dead_misaligned_raw_pool_alias() {
    UnitCommandContext context{};
    context.callbacks.access_spawn_alias = access_dead_spawn_alias;

    UnitMovementUnit builder{};
    builder.command_state = kUnitStateSpawnCreateCycle;
    builder.command_value = 116;
    builder.animation_frame = 4;
    builder.definition.spawn_frame_count = 4;

    HandleUnitSpawnCreateCycle(context, builder);

    // Original 0x004cb77f takes the bit-4 branch to the shared pop at
    // 0x004cb852.  The no-queue tail at 0x004cfdd0 clears raw +0x9c and
    // enters idle but deliberately leaves the raw +0x68 alias residue.
    require(builder.command_state == kUnitStateRuntimeIdleAcquire &&
            builder.command_value == 116,
        "state-0x5b did not pop while preserving the raw alias residue");
}

UnitMovementUnit g_persistent_spawn_alias;

bool access_persistent_spawn_alias(UnitCommandContext&, u32 raw_unit_offset,
    UnitMovementUnit& alias, bool write_back) {
    require(raw_unit_offset == 116,
        "state-0x5b changed its persistent raw alias offset");
    if (write_back) {
        g_persistent_spawn_alias.action_mode = alias.action_mode;
        g_persistent_spawn_alias.equipment_slots[1] = alias.equipment_slots[1];
        g_persistent_spawn_alias.health = alias.health;
        g_persistent_spawn_alias.previous_command_state =
            alias.previous_command_state;
    }
    else {
        alias = g_persistent_spawn_alias;
    }
    return true;
}

void test_spawn_cycle_accumulates_misaligned_raw_pool_alias() {
    g_persistent_spawn_alias = {};
    g_persistent_spawn_alias.definition.production_spawn_time = 2;

    UnitCommandContext context{};
    context.callbacks.access_spawn_alias = access_persistent_spawn_alias;

    UnitMovementUnit builder{};
    builder.command_state = kUnitStateSpawnCreateCycle;
    builder.command_value = 116;
    builder.animation_frame = 4;
    builder.definition.spawn_frame_count = 4;

    HandleUnitSpawnCreateCycle(context, builder);
    require(builder.command_state == kUnitStateSpawnCreateCycle &&
            g_persistent_spawn_alias.action_mode == 1,
        "first state-0x5b raw alias tick was not persisted");
    HandleUnitSpawnCreateCycle(context, builder);
    require(builder.command_state == kUnitStateSpawnCreateCycle &&
            g_persistent_spawn_alias.action_mode == 2,
        "second state-0x5b raw alias tick was not persisted");
    HandleUnitSpawnCreateCycle(context, builder);
    require(builder.command_state == kUnitStateRuntimeIdleAcquire &&
            g_persistent_spawn_alias.previous_command_state == 1,
        "completed state-0x5b raw alias did not pop the builder command");
}

void test_movement_flag_1000_enters_idle_as_completed_step() {
    UnitMovementContext context{};
    UnitMovementUnit unit{};
    unit.command_state = kUnitStateGuardPursueTarget;
    unit.command_flags = 0x1000u;
    unit.animation_frame = 14;
    unit.definition.animation_timer_period = 9;
    unit.path_target_x = 1059;
    unit.path_target_y = 1401;

    const bool completed = ProcessUnitMovementStep(context, unit);

    require(completed && unit.command_state == 1 && unit.animation_frame == 0,
        "movement flag 0x1000 did not mirror FUN_004d0067 idle completion");
    require(unit.path_target_x == 1059 && unit.path_target_y == 1401,
        "movement flag 0x1000 rewrote the existing path tuple");
}

void test_lifecycle_idle_reset_clamps_shared_raw_frame() {
    UnitMovementUnit unit{};
    unit.active = false;
    unit.command_state = 0x10000004u;
    unit.command_flags = 0x80u;
    unit.animation_frame = 0;
    unit.work_timer = 47;
    unit.definition.animation_timer_period = 30;

    UnitCommandContext command_context{};
    HandleUnitReturnToIdleState(command_context, unit);
    require(unit.command_state == kUnitStateRuntimeIdleAcquire &&
            unit.animation_frame == 0 && unit.work_timer == 0,
        "lifecycle idle reset did not clamp shared raw +0x64");

    UnitMovementContext movement{};
    movement.lifecycle_units.push_back(&unit);
    UnitLifecycleContext lifecycle_context{};
    lifecycle_context.movement = &movement;
    HandleUnitLifecycleDispatchListTick(lifecycle_context);
    require(unit.work_timer == 0xffffffffu,
        "lifecycle dispatch did not decrement the reset raw +0x64 timer");
}

void test_construction_backlink_releases_dead_spawn_worker_raw_state() {
    UnitCommandContext context{};

    UnitMovementUnit building{};
    building.type_id = 0x73;
    building.action_mode_gate = 1;
    building.command_state = kUnitStateRuntimeIdleAcquire;

    UnitMovementUnit worker{};
    worker.type_id = 0x10;
    worker.runtime_flags = 1;
    worker.command_state = kUnitCommandDead | kUnitStateSpawnCreateCycle;
    building.target = &worker;

    HandleUnitRuntimeDispatchTick(context, building);

    require(building.target == nullptr,
        "construction backlink masked the dead bit from raw state 0x1000005b");

    worker.command_state = kUnitStateSpawnCreateCycle;
    building.target = &worker;
    HandleUnitRuntimeDispatchTick(context, building);
    require(building.target == &worker,
        "construction backlink rejected a live state-0x5b worker");
}

void test_broken_shield_preserves_negative_raw_overshoot() {
    const UnitShieldDamageResult survives =
        ResolveUnitShieldDamageRaw(60, 53);
    require(!survives.broken && survives.shield_points == 7 &&
            survives.remaining_damage == 0,
        "surviving shield did not retain the positive raw residual");

    const UnitShieldDamageResult breaks =
        ResolveUnitShieldDamageRaw(15, 53);
    require(breaks.broken && breaks.shield_points == 0xffffffdau &&
            breaks.remaining_damage == 38,
        "broken shield discarded the negative raw +0x14 overshoot");

    const UnitShieldDamageResult exact =
        ResolveUnitShieldDamageRaw(53, 53);
    require(exact.broken && exact.shield_points == 0 &&
            exact.remaining_damage == 0,
        "exact shield break did not follow the original signed-JG branch");

    UnitDamageContext context{};
    UnitRecord target{};
    target.runtime_flags = kUnitRuntimeShielded;
    target.max_hit_points = 100;
    target.hit_points = 100;
    target.shield_points = 15;
    require(!ApplyUnitDamage(context, target, 53) &&
            target.hit_points == 62 &&
            target.shield_points == 0xffffffdau &&
            (target.runtime_flags & kUnitRuntimeShielded) == 0,
        "unit damage path did not retain the broken shield raw residual");
}

void test_movement_flag_10000_applies_context_frame_modifier() {
    UnitMovementContext context{};
    context.map.width = 100;
    context.map.height = 100;
    context.additional_movement_modifier = 1;

    UnitMovementUnit boosted{};
    boosted.owner_id = 0;
    boosted.x = 399;
    boosted.y = 939;
    boosted.next_path_x = 0;
    boosted.next_path_y = 0;
    boosted.direction = 8;
    boosted.command_flags = 0x10000u;
    boosted.definition.animation_frame_count = 32;
    boosted.definition.frame_delta_by_direction[8][1] = {-11, -10};

    require(ProcessUnitMovementStep(context, boosted) &&
            boosted.x == 387 && boosted.y == 928,
        "movement flag 0x10000 omitted the JW2_11 action-0x22 modifier");

    UnitMovementUnit ordinary{};
    ordinary.owner_id = 0;
    ordinary.x = 399;
    ordinary.y = 939;
    ordinary.next_path_x = 0;
    ordinary.next_path_y = 0;
    ordinary.direction = 8;
    ordinary.definition.animation_frame_count = 32;
    ordinary.definition.frame_delta_by_direction[8][1] = {-11, -10};

    require(ProcessUnitMovementStep(context, ordinary) &&
            ordinary.x == 388 && ordinary.y == 929,
        "JW2_11 action-0x22 modifier leaked to an unflagged movement step");
}

void test_linked_release_sums_parent_action_mode() {
    UnitMovementUnit merged{};
    merged.type_id = 35;
    merged.action_mode = 8;
    merged.definition.initial_max_health = 100;
    UnitRuntimeStatBlock merged_stats{};
    merged_stats.max_health = 100;
    merged_stats.health = 100;

    UnitMovementUnit parent{};
    parent.action_mode = 104;
    UnitRuntimeStatBlock parent_stats{};
    parent_stats.max_health = 100;
    parent_stats.health = 100;

    RebuildUnitRuntimeStatsFromDefinitionAndParents(merged, merged_stats,
        &parent, &parent_stats, nullptr, nullptr, nullptr);
    require(merged.action_mode == 112,
        "two-unit linked release did not sum raw +0x2c");

    UnitMovementUnit three_way{};
    three_way.type_id = 0x2b;
    three_way.action_mode = 7;
    three_way.definition.initial_max_health = 100;
    UnitRuntimeStatBlock three_way_stats{};
    three_way_stats.max_health = 100;
    three_way_stats.health = 100;

    UnitMovementUnit parent_b{};
    parent_b.action_mode = 13;
    UnitRuntimeStatBlock parent_b_stats{};
    parent_b_stats.max_health = 100;
    parent_b_stats.health = 100;
    parent.action_mode = 11;

    RebuildUnitRuntimeStatsFromDefinitionAndParents(three_way, three_way_stats,
        &parent, &parent_stats, &parent_b, &parent_b_stats, nullptr);
    require(three_way.action_mode == 31,
        "three-unit linked release did not sum every raw +0x2c value");
}

void test_value_transfer_uses_interaction_bounds() {
    UnitMovementUnit source{};
    source.x = 2467;
    source.y = 1300;
    source.definition.bounds_left = -10;
    source.definition.bounds_top = -35;
    source.definition.bounds_width = 27;
    source.definition.bounds_height = 47;
    source.definition.interaction_bounds_left = -5;
    source.definition.interaction_bounds_top = -9;
    source.definition.interaction_bounds_width = 17;
    source.definition.interaction_bounds_height = 16;

    UnitMovementUnit target = source;
    target.x = 2500;
    target.y = 1339;
    source.target = &target;

    require(CheckCurrentTargetFootprintSeparated(source),
        "value-transfer reach used display bounds instead of interaction bounds");
}

void test_value_transfer_entry_uses_raw_cargo_union() {
    UnitCommandContext context{};
    UnitMovementUnit source{};
    source.command_value = 0xdeadbeefu;

    StartUnitValueTransferEntry(context, source, 194);

    require(source.command_state == kUnitStateValueTransferStart &&
            source.cargo_amount == 194,
        "value-transfer entry did not store its value in raw +0x4c");
    require(source.command_value == 0xdeadbeefu,
        "value-transfer entry overwrote the target-reference union");
}

} // namespace

int main() {
    test_cached_next_continues_through_free_tail();
    test_unlinked_non_next_node_is_not_visited_from_entry_snapshot();
    test_action9_counts_down_raw_effect_30_accumulator();
    test_area_stun_preserves_independent_unit_timers();
    test_repeated_thunder_impacts_are_applied_during_list_walk();
    test_missing_thunder_target_keeps_natural_lifetime_and_payload();
    test_thunder_shield_break_double_release_is_idempotent();
    test_hurdle_impact_creates_type_125_unit();
    test_final_path_budget_point_still_scans_projectile_collisions();
    test_target_marker_impact_advances_raw_tick_word();
    test_target_marker_releases_on_transient_target_reach();
    test_reserved_tile_transient_dropoff_reroute_uses_effect_offset_alias();
    test_recharge_aura_persists_when_frame_reaches_limit();
    test_placed_unit_clears_recycled_elite_progress();
    test_placed_unit_clears_recycled_movement_interpolation();
    test_placed_building_preserves_recycled_destination_union();
    test_transient_lifecycle_uses_shared_jw211_period();
    test_in_range_guard_acquisition_preserves_command_path();
    test_transport_unload_command_resolves_carrier_payload();
    test_relative_spatial_box_uses_frame_start_source_anchor();
    test_lifecycle_idle_reset_clamps_shared_raw_frame();
    test_construction_backlink_releases_dead_spawn_worker_raw_state();
    test_broken_shield_preserves_negative_raw_overshoot();
    test_follow_command_preserves_zeroed_free_pool_target();
    test_target_progress_uses_zeroed_free_pool_target_definition();
    test_spawn_cycle_preserves_misaligned_raw_pool_alias();
    test_spawn_cycle_pops_dead_misaligned_raw_pool_alias();
    test_spawn_cycle_accumulates_misaligned_raw_pool_alias();
    test_movement_flag_1000_enters_idle_as_completed_step();
    test_movement_flag_10000_applies_context_frame_modifier();
    test_linked_release_sums_parent_action_mode();
    test_value_transfer_uses_interaction_bounds();
    test_value_transfer_entry_uses_raw_cargo_union();
    std::cout << "UNIT_EFFECT_INTRUSIVE_ITERATION_PASS\n";
    return EXIT_SUCCESS;
}
