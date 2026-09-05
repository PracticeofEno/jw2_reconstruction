#include "ranker_ai_autopilot.h"
#include "ranker_ai_entity_control.h"
#include "ranker_ai_entity_economy.h"
#include "ranker_ai_decision_gate.h"
#include "ranker_ai_expansion.h"
#include "ranker_ai_actions.h"
#include "ranker_ai_live_validator.h"
#include "ranker_ai_observation.h"
#include "ranker_ai_rl_reward.h"
#include "ranker_ai_scripted_bot.h"
#include "ranker_ai_slot_role.h"
#include "ranker_unit_commands.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace ranker;

struct VisibilityFixture {
    std::vector<u32> visible_ids;
};

bool visible_from_fixture(const UnitMovementUnit& unit, u32,
    void* user_data) {
    const auto* fixture = static_cast<const VisibilityFixture*>(user_data);
    if (fixture == nullptr) {
        return false;
    }
    return std::find(fixture->visible_ids.begin(),
        fixture->visible_ids.end(), unit.id) != fixture->visible_ids.end();
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "ai_play_interface_regression: " << message << '\n';
        std::exit(1);
    }
}

struct ProductionAvailabilityFixture {
    AiProductionRequestKind expected_kind = AiProductionRequestKind::unit;
    u32 expected_source_id = 0;
    u32 expected_production_id = 0;
    i32 expected_x = 0;
    i32 expected_y = 0;
    AiProductionAvailability result{};
    u32 calls = 0;
    bool arguments_matched = false;
};

AiProductionAvailability production_from_fixture(
    AiProductionRequestKind kind, const UnitMovementUnit& source,
    u32 production_id, i32 world_x, i32 world_y, u32 local_owner,
    void* user_data) {
    auto* fixture = static_cast<ProductionAvailabilityFixture*>(user_data);
    if (fixture == nullptr) {
        return {};
    }
    ++fixture->calls;
    fixture->arguments_matched = kind == fixture->expected_kind &&
        source.id == fixture->expected_source_id &&
        production_id == fixture->expected_production_id &&
        world_x == fixture->expected_x && world_y == fixture->expected_y &&
        local_owner == 0;
    return fixture->result;
}

struct LiveValidationFixture {
    AiProductionAvailability unit_result{};
    AiProductionAvailability research_result{};
    bool placement_result = false;
    u32 unit_calls = 0;
    u32 research_calls = 0;
    u32 placement_calls = 0;
};

AiProductionAvailability live_unit_requirement(u32, u32, void* user_data) {
    auto* fixture = static_cast<LiveValidationFixture*>(user_data);
    if (fixture == nullptr) {
        return {};
    }
    ++fixture->unit_calls;
    return fixture->unit_result;
}

AiProductionAvailability live_research_requirement(u32, u32,
    void* user_data) {
    auto* fixture = static_cast<LiveValidationFixture*>(user_data);
    if (fixture == nullptr) {
        return {};
    }
    ++fixture->research_calls;
    return fixture->research_result;
}

bool live_placement(const UnitMovementUnit&, u32, i32, i32, u32,
    void* user_data) {
    auto* fixture = static_cast<LiveValidationFixture*>(user_data);
    if (fixture == nullptr) {
        return false;
    }
    ++fixture->placement_calls;
    return fixture->placement_result;
}

UnitMovementUnit make_unit(u32 id, u32 slot, u32 owner, u32 type,
    u32 capabilities, i32 x, i32 y) {
    UnitMovementUnit unit{};
    unit.id = id;
    unit.runtime_slot_index = slot;
    unit.owner_id = owner;
    unit.type_id = type;
    unit.type_flags = capabilities;
    unit.x = x;
    unit.y = y;
    unit.destination_x = x + 7;
    unit.destination_y = y + 9;
    unit.path_target_x = x + 11;
    unit.path_target_y = y + 13;
    unit.health = 75;
    unit.max_health = 100;
    unit.secondary_value = 20;
    unit.max_secondary_value = 30;
    unit.command_state = 4;
    unit.command_flags = 0x44;
    unit.action_mode = 3;
    unit.movement_state = 2;
    unit.cargo_amount = 5;
    unit.cargo_capacity = 10;
    unit.queued_production_type_id = 0x2a;
    unit.production_variant = 1;
    unit.deferred_command_count = 2;
    unit.direction = 6;
    unit.animation_frame = 3;
    unit.animation_timer = 12;
    unit.status_timer = 2;          // avatar level (HUD shows level + 1)
    unit.elite_progress_value = 45; // avatar experience
    unit.command_entry_lockout_ticks = 8;
    unit.command_lockout_ticks = 15;
    unit.effect_timer = 21;
    unit.equipment_flags = 0x3;
    unit.item_slots[0] = 0x51;
    unit.equipment_slots[1] = 0x62;
    unit.active = true;
    return unit;
}

void seed_map(UnitMovementContext& movement) {
    movement.map.width = 4;
    movement.map.height = 3;
    movement.map.stride_tiles = 5;
    movement.map.cells.resize(15);
    for (u32 y = 0; y < movement.map.height; ++y) {
        for (u32 x = 0; x < movement.map.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * movement.map.stride_tiles + x;
            // Walkable ground per the engine's class-0 entry rule: terrain
            // class 0 plus the decoration-layer entry bits; blocked cells
            // carry the blocked flag and no entry bits.
            const bool walkable = (x + y) % 2 == 0;
            movement.map.cells[index].flags =
                walkable ? 0u : kMapCellBlockedTerrain;
            movement.map.cells[index].alternate_flags =
                walkable ? 0x60000000u : 0u;
        }
    }
}

void test_observation_visibility_and_determinism() {
    constexpr u32 kMove = 1u << 4;
    constexpr u32 kAttack = 1u << 5;
    constexpr u32 kHarvest = 1u << 7;

    PlayerSlotRuntimeState players{};
    players.global_active_slot_mask = 0x07;
    players.owner_relation_masks[0] = 0x05;
    players.owner_primary_resources[0] = 1234;
    players.owner_secondary_resources[0] = 567;
    players.owner_aux_resources[0] = 89;
    players.owner_start_x[0] = 32;
    players.owner_start_y[0] = 64;

    UnitMovementUnit own_later = make_unit(
        0x3a0, 2, 0, 0x30, kMove | kAttack | kHarvest, 64, 96);
    UnitMovementUnit own_first = make_unit(
        0x1d0, 1, 0, 0x31, kMove | kHarvest, 32, 64);
    UnitMovementUnit visible_enemy = make_unit(
        0x570, 3, 1, 0x40, kMove | kAttack, 90, 110);
    UnitMovementUnit hidden_enemy = make_unit(
        0x740, 4, 1, 0x41, kMove | kAttack, 120, 124);
    own_later.target = &hidden_enemy;
    own_later.deferred_commands[0] = UnitQueuedCommand{0x10, 0x20, 0, 0};
    own_later.deferred_commands[1] = UnitQueuedCommand{0x17, 0x14, 0, 0};

    UnitMovementContext movement{};
    seed_map(movement);
    movement.map.cells[1].flags = kMapCellPassableTerrain |
        (4000u << kMapCellHarvestAmountShift);
    movement.map.cells[2].flags = kMapCellPassableTerrain |
        (2000u << kMapCellHarvestAmountShift);
    movement.active_units = {
        &hidden_enemy, &own_later, &visible_enemy, &own_first};

    std::vector<u8> explored(12, 1);
    std::vector<u8> visible(12, 0);
    visible[1] = 1;
    explored[2] = 0;
    VisibilityFixture visibility{{visible_enemy.id}};

    AiObservationBuildInput input{};
    input.simulation_frame = 777;
    input.map_relative_path = "Maps/Rank Maps/(4) Python Jurassic v0.1.trk";
    input.map_sha256 = "C2D4E81F";
    input.local_owner = 0;
    input.local_faction = 2;
    input.population_used = 9;
    input.population_reserved = 1;
    input.population_limit = 20;
    input.players = &players;
    input.movement = &movement;
    input.explored_tiles = &explored;
    input.visible_tiles = &visible;
    input.unit_visible = visible_from_fixture;
    input.unit_visibility_user_data = &visibility;

    const AiObservationBuildResult first = BuildAiObservationV1(input);
    require(first.code == AiObservationBuildCode::okay,
        "valid observation was rejected");
    require(first.observation.units.size() == 3,
        "hidden opponent escaped the visibility gate");
    require(first.observation.units[0].id == own_first.id &&
        first.observation.units[1].id == own_later.id &&
        first.observation.units[2].id == visible_enemy.id,
        "units were not sorted by stable runtime identity");
    require(first.observation.units[1].target_id == 0,
        "controlled-unit target leaked a hidden opponent");
    require(first.observation.units[1].deferred_commands.size() == 2 &&
        first.observation.units[1].deferred_commands[0].state == 0x10 &&
        first.observation.units[1].deferred_commands[0]
            .command_value_or_target == 0x20,
        "controlled production queue was not copied");
    require(first.observation.units[2].destination_x == 0 &&
        first.observation.units[2].path_target_x == 0 &&
        first.observation.units[2].command_state == 0,
        "visible opponent exposed private command state");
    require(first.observation.units[1].command_entry_lockout_ticks == 8 &&
        first.observation.units[1].command_lockout_ticks == 15 &&
        first.observation.units[1].effect_timer == 21 &&
        first.observation.units[1].equipment_flags == 0x3 &&
        first.observation.units[1].item_slots[0] == 0x51 &&
        first.observation.units[1].equipment_slots[1] == 0x62,
        "controlled unit lost its v4 lockout/equipment state");
    require(first.observation.units[2].command_entry_lockout_ticks == 0 &&
        first.observation.units[2].command_lockout_ticks == 0 &&
        first.observation.units[2].effect_timer == 0 &&
        first.observation.units[2].equipment_flags == 0 &&
        first.observation.units[2].item_slots[0] == 0 &&
        first.observation.units[2].equipment_slots[1] == 0,
        "visible opponent exposed private lockout/equipment state");
    require(first.observation.units[2].direction == 6 &&
        first.observation.units[2].animation_frame == 3 &&
        first.observation.units[2].animation_timer == 12 &&
        first.observation.units[2].level == 2 &&
        first.observation.units[2].experience == 45,
        "visible opponent lost its public rendered/level state");
    require(first.observation.primary_resources == 1234 &&
        first.observation.secondary_resources == 567 &&
        first.observation.auxiliary_resources == 89,
        "local resources were not copied");
    require(first.observation.tiles.size() == 12 &&
        first.observation.tiles[0].passable &&
        first.observation.tiles[1].visible &&
        !first.observation.tiles[2].explored,
        "map visibility or stride projection is incorrect");
    require(first.observation.tiles[1].resource_amount == 4000 &&
        first.observation.tiles[2].resource_amount == 0,
        "resource observation exposed hidden terrain or lost visible amount");

    const u64 first_hash = HashAiObservationV1(first.observation);
    movement.active_units = {
        &own_first, &visible_enemy, &hidden_enemy, &own_later};
    const AiObservationBuildResult reordered = BuildAiObservationV1(input);
    require(reordered &&
        HashAiObservationV1(reordered.observation) == first_hash,
        "active-list order changed the observation hash");

    visibility.visible_ids.push_back(hidden_enemy.id);
    const AiObservationBuildResult revealed = BuildAiObservationV1(input);
    require(revealed && revealed.observation.units.size() == 4 &&
        revealed.observation.units[1].target_id == hidden_enemy.id &&
        HashAiObservationV1(revealed.observation) != first_hash,
        "revealed target was not added to the observation");

    input.unit_visible = nullptr;
    const AiObservationBuildResult no_callback = BuildAiObservationV1(input);
    require(no_callback && no_callback.observation.units.size() == 2,
        "missing visibility callback did not fail closed");

    std::vector<u8> bad_visible(11, 0);
    input.visible_tiles = &bad_visible;
    require(BuildAiObservationV1(input).code ==
            AiObservationBuildCode::invalid_visible_tile_count,
        "invalid visibility mask size was accepted");
}

// Fog-honest resource observation: with a per-owner resource memory the
// reported harvestable amount is the last-seen snapshot (updated only while
// the tile is in active vision), never the live value through fog.
void test_observation_resource_memory() {
    PlayerSlotRuntimeState players{};
    players.global_active_slot_mask = 0x01;

    UnitMovementUnit own = make_unit(0x1d0, 1, 0, 0x20, 1u << 4, 32, 64);
    UnitMovementContext movement{};
    seed_map(movement);
    const auto set_amount = [&](std::size_t map_index, u32 amount) {
        movement.map.cells[map_index].flags = kMapCellPassableTerrain |
            (amount << kMapCellHarvestAmountShift);
    };
    set_amount(1, 4000);  // tile 1: starts in active vision
    set_amount(2, 2000);  // tile 2: explored but never actually seen
    movement.active_units = {&own};

    std::vector<u8> explored(12, 1);
    std::vector<u8> visible(12, 0);
    visible[1] = 1;

    AiObservationBuildInput input{};
    input.local_owner = 0;
    input.players = &players;
    input.movement = &movement;
    input.explored_tiles = &explored;
    input.visible_tiles = &visible;
    std::vector<u32> memory;
    input.resource_memory = &memory;

    const AiObservationBuildResult seen = BuildAiObservationV1(input);
    require(seen && seen.observation.tiles[1].resource_amount == 4000,
        "visible tile did not report the live amount");
    // Berry positions / initial amounts are public map data: a never-seen
    // tile reports the map's initial amount (the memory is seeded from it),
    // not 0.  Only depletion is fog-honest (checked below).
    require(seen.observation.tiles[2].resource_amount == 2000,
        "never-seen tile did not report the public initial amount");

    set_amount(1, 1500);  // depletes while still watched
    const AiObservationBuildResult watched = BuildAiObservationV1(input);
    require(watched && watched.observation.tiles[1].resource_amount == 1500,
        "watched depletion was not observed live");

    visible[1] = 0;       // vision lost; further depletion must stay hidden
    set_amount(1, 700);
    const AiObservationBuildResult fogged = BuildAiObservationV1(input);
    require(fogged && fogged.observation.tiles[1].resource_amount == 1500,
        "fogged tile leaked the live amount instead of the snapshot");

    visible[1] = 1;       // re-lighting the tile corrects the memory
    const AiObservationBuildResult relit = BuildAiObservationV1(input);
    require(relit && relit.observation.tiles[1].resource_amount == 700,
        "re-lit tile did not refresh the remembered amount");

    input.resource_memory = nullptr;  // legacy callers keep explored=live
    visible[1] = 0;
    const AiObservationBuildResult legacy = BuildAiObservationV1(input);
    require(legacy && legacy.observation.tiles[1].resource_amount == 700 &&
        legacy.observation.tiles[2].resource_amount == 2000,
        "legacy relaxed behavior changed without a resource memory");
}

void test_action_validation_and_packet_planning() {
    constexpr u32 kMove = 1u << 4;
    constexpr u32 kAttack = 1u << 5;
    constexpr u32 kHarvest = 1u << 7;

    PlayerSlotRuntimeState players{};
    players.owner_relation_masks[0] = 0x05;

    UnitMovementUnit worker = make_unit(
        0x1d0, 1, 0, 0x30, kMove | kHarvest, 32, 64);
    UnitMovementUnit fighter = make_unit(
        0x3a0, 2, 0, 0x31, kMove | kAttack | kHarvest, 64, 96);
    UnitMovementUnit enemy = make_unit(
        0x570, 3, 1, 0x40, kMove | kAttack, 90, 70);
    UnitMovementUnit hidden_enemy = make_unit(
        0x740, 4, 1, 0x41, kMove | kAttack, 120, 82);
    UnitMovementUnit ally = make_unit(
        0x910, 5, 2, 0x42, kMove | kAttack, 80, 84);
    UnitMovementUnit incapable = make_unit(
        0xae0, 6, 0, 0x43, 0, 48, 52);
    UnitMovementUnit tyrano_nest = make_unit(
        0xcb0, 7, 0, 128, 0, 40, 44);
    UnitMovementUnit egg_nest = make_unit(
        0xe80, 8, 0, 132, 0, 72, 76);
    tyrano_nest.definition.placement_path_reference_count = 2;
    egg_nest.definition.placement_path_reference_count = 7;

    UnitMovementContext movement{};
    seed_map(movement);
    constexpr std::size_t kHarvestCompactIndex = 2u * 4u + 2u;
    constexpr std::size_t kHarvestStorageIndex = 2u * 5u + 2u;
    movement.map.cells[kHarvestStorageIndex].flags =
        kMapCellPassableTerrain |
        (4000u << kMapCellHarvestAmountShift);
    movement.active_units = {
        &enemy, &fighter, &ally, &worker, &hidden_enemy, &incapable,
        &tyrano_nest, &egg_nest};
    VisibilityFixture visibility{{enemy.id, ally.id}};
    ProductionAvailabilityFixture production{};

    AiActionPlanInput input{};
    input.local_owner = 0;
    input.players = &players;
    input.movement = &movement;
    input.unit_visible = visible_from_fixture;
    input.unit_visibility_user_data = &visibility;
    std::vector<u8> visible_tiles(12, 0);
    visible_tiles[kHarvestCompactIndex] = 1;
    input.visible_tiles = &visible_tiles;
    input.production_available = production_from_fixture;
    input.production_availability_user_data = &production;

    AiSemanticAction no_op{};
    require(static_cast<bool>(PlanAiSemanticActionV1(input, no_op)),
        "NoOp was rejected");

    AiSemanticAction move{};
    move.kind = AiSemanticActionKind::move;
    move.unit_ids = {fighter.id, worker.id};
    move.target_x = 100;
    move.target_y = 80;
    const AiActionPlanResult move_plan = PlanAiSemanticActionV1(input, move);
    require(move_plan && move_plan.packets.size() == 2,
        "valid Move was rejected");
    require(move_plan.packets[0].unit_offset == worker.id &&
        move_plan.packets[1].unit_offset == fighter.id,
        "Move packets were not deterministically ordered");
    require(move_plan.packets[0].subtype == 0x02 &&
        move_plan.packets[0].packed_opcode == 0x02000000 &&
        move_plan.packets[0].arg0 == 0x04 &&
        move_plan.packets[0].arg1 == 0 &&
        move_plan.packets[0].arg2 == 100 &&
        move_plan.packets[0].arg3 == 80,
        "Move packet fields do not match subtype 0x02");

    AiSemanticAction attack_move = move;
    attack_move.kind = AiSemanticActionKind::attack_move;
    attack_move.queued = true;
    const AiActionPlanResult attack_move_plan =
        PlanAiSemanticActionV1(input, attack_move);
    require(attack_move_plan &&
        attack_move_plan.packets[0].arg0 == 0x80000004u &&
        attack_move_plan.packets[1].arg0 == 0x80000005u,
        "AttackMove did not mirror human attack/move capability fallback");

    AiSemanticAction attack{};
    attack.kind = AiSemanticActionKind::attack_unit;
    attack.unit_ids = {fighter.id};
    attack.target_unit_id = enemy.id;
    const AiActionPlanResult attack_plan =
        PlanAiSemanticActionV1(input, attack);
    require(attack_plan && attack_plan.packets.size() == 1 &&
        attack_plan.packets[0].arg0 == 0x05 &&
        attack_plan.packets[0].arg1 == enemy.id &&
        attack_plan.packets[0].arg2 == static_cast<u32>(enemy.x) &&
        attack_plan.packets[0].arg3 == static_cast<u32>(enemy.y),
        "AttackUnit packet did not use the visible target state");

    attack.target_unit_id = hidden_enemy.id;
    require(PlanAiSemanticActionV1(input, attack).code ==
            AiActionPlanCode::target_not_visible,
        "AttackUnit accepted a hidden target");
    attack.target_unit_id = ally.id;
    require(PlanAiSemanticActionV1(input, attack).code ==
            AiActionPlanCode::target_is_friendly,
        "AttackUnit accepted an allied target");

    AiSemanticAction harvest{};
    harvest.kind = AiSemanticActionKind::harvest;
    harvest.unit_ids = {worker.id};
    harvest.target_x = 70;
    harvest.target_y = 80;
    require(PlanAiSemanticActionV1(input, harvest) &&
        PlanAiSemanticActionV1(input, harvest).packets[0].arg0 == 0x07 &&
        PlanAiSemanticActionV1(input, harvest).packets[0].arg1 == 0 &&
        PlanAiSemanticActionV1(input, harvest).packets[0].arg2 == 70 &&
        PlanAiSemanticActionV1(input, harvest).packets[0].arg3 == 80,
        "valid Harvest point was rejected");
    visible_tiles[kHarvestCompactIndex] = 0;
    require(PlanAiSemanticActionV1(input, harvest).code ==
            AiActionPlanCode::target_not_visible,
        "Harvest accepted a resource tile hidden by fog of war");
    visible_tiles[kHarvestCompactIndex] = 1;
    movement.map.cells[kHarvestStorageIndex].flags = kMapCellPassableTerrain;
    require(PlanAiSemanticActionV1(input, harvest).code ==
            AiActionPlanCode::target_not_harvestable,
        "Harvest accepted a visible tile without resources");
    movement.map.cells[kHarvestStorageIndex].flags =
        kMapCellPassableTerrain |
        (4000u << kMapCellHarvestAmountShift);
    input.visible_tiles = nullptr;
    require(PlanAiSemanticActionV1(input, harvest).code ==
            AiActionPlanCode::invalid_visible_tile_count,
        "Harvest did not fail closed without an owner visibility mask");
    input.visible_tiles = &visible_tiles;

    production = {};
    production.expected_kind = AiProductionRequestKind::unit;
    production.expected_source_id = tyrano_nest.id;
    production.expected_production_id = 32;
    production.result.available = true;
    AiSemanticAction produce{};
    produce.kind = AiSemanticActionKind::produce_unit;
    produce.unit_ids = {tyrano_nest.id};
    produce.production_id = 32;
    const AiActionPlanResult produce_plan =
        PlanAiSemanticActionV1(input, produce);
    require(produce_plan && production.arguments_matched &&
        produce_plan.packets.size() == 1 &&
        produce_plan.packets[0].subtype == 0x01 &&
        produce_plan.packets[0].packed_opcode == 0x01000000 &&
        produce_plan.packets[0].unit_offset == tyrano_nest.id &&
        produce_plan.packets[0].arg0 == 32 &&
        produce_plan.packets[0].arg1 == 0 &&
        produce_plan.packets[0].arg2 == 0 &&
        produce_plan.packets[0].arg3 == 0,
        "ProduceUnit did not match gameplay_1 subtype-01 Dinos packet");

    input.production_available = nullptr;
    require(PlanAiSemanticActionV1(input, produce).code ==
            AiActionPlanCode::missing_production_validator,
        "ProduceUnit did not fail closed without its live validator");
    input.production_available = production_from_fixture;
    production.result.available = false;
    require(PlanAiSemanticActionV1(input, produce).code ==
            AiActionPlanCode::production_unavailable,
        "ProduceUnit ignored a live validator rejection");
    production.result.available = true;
    tyrano_nest.deferred_command_count = 4;
    require(PlanAiSemanticActionV1(input, produce).code ==
            AiActionPlanCode::production_queue_full,
        "ProduceUnit accepted a full production queue");
    tyrano_nest.deferred_command_count = 2;

    production = {};
    production.expected_kind = AiProductionRequestKind::research;
    production.expected_source_id = tyrano_nest.id;
    production.expected_production_id = 20;
    production.result.available = true;
    production.result.secondary_cost = 0;
    AiSemanticAction research{};
    research.kind = AiSemanticActionKind::research;
    research.unit_ids = {tyrano_nest.id};
    research.production_id = 20;
    const AiActionPlanResult research_plan =
        PlanAiSemanticActionV1(input, research);
    require(research_plan && production.arguments_matched &&
        research_plan.packets[0].subtype == 0x0c &&
        research_plan.packets[0].arg0 == 20 &&
        research_plan.packets[0].arg1 == 0,
        "Research did not match gameplay_1 subtype-0c order-20 packet");

    production = {};
    production.expected_kind = AiProductionRequestKind::building;
    production.expected_source_id = worker.id;
    production.expected_production_id = 128;
    production.expected_x = 96;
    production.expected_y = 64;
    production.result.available = true;
    AiSemanticAction build{};
    build.kind = AiSemanticActionKind::build;
    build.unit_ids = {worker.id};
    build.production_id = 128;
    build.target_x = 100;
    build.target_y = 80;
    // Fog rule: the placement tile (96, 64) -> compact (3, 2) must be
    // explored (or visible), else the build is refused before the validator
    // - an unexplored site has to be scouted first.
    require(PlanAiSemanticActionV1(input, build).code ==
            AiActionPlanCode::target_not_visible,
        "Build was accepted on a fog tile");
    visible_tiles[2u * 4u + 3u] = 1;
    const AiActionPlanResult build_plan =
        PlanAiSemanticActionV1(input, build);
    require(build_plan && production.arguments_matched &&
        build_plan.packets[0].subtype == 0x02 &&
        build_plan.packets[0].arg0 == 0x06 &&
        build_plan.packets[0].arg1 == 0x20 &&
        build_plan.packets[0].arg2 == 96 &&
        build_plan.packets[0].arg3 == 64,
        "Build did not encode the type-minus-0x60 aligned placement tuple");

    AiSemanticAction rally{};
    rally.kind = AiSemanticActionKind::set_rally;
    rally.unit_ids = {egg_nest.id, tyrano_nest.id};
    rally.target_x = 88;
    rally.target_y = 72;
    const AiActionPlanResult rally_plan =
        PlanAiSemanticActionV1(input, rally);
    require(rally_plan && rally_plan.packets.size() == 2 &&
        rally_plan.packets[0].unit_offset == tyrano_nest.id &&
        rally_plan.packets[0].subtype == 0x08 &&
        rally_plan.packets[0].arg0 == 0x1f &&
        rally_plan.packets[0].arg1 == 0 &&
        rally_plan.packets[0].arg2 == 88 &&
        rally_plan.packets[0].arg3 == 72,
        "SetRally did not match gameplay_1 subtype-08 packet fields");

    tyrano_nest.deferred_command_count = 1;
    tyrano_nest.deferred_commands[0] =
        UnitQueuedCommand{0x10, 32, 0, 0};
    AiSemanticAction cancel{};
    cancel.kind = AiSemanticActionKind::cancel_production;
    cancel.unit_ids = {tyrano_nest.id};
    const AiActionPlanResult cancel_plan =
        PlanAiSemanticActionV1(input, cancel);
    require(cancel_plan && cancel_plan.packets[0].subtype == 0x01 &&
        cancel_plan.packets[0].arg0 == 0 &&
        cancel_plan.packets[0].arg1 == 1 &&
        cancel_plan.packets[0].arg2 == 0xffffffffu &&
        cancel_plan.packets[0].arg3 == 0,
        "CancelProduction did not match gameplay_1 latest-queue packet");
    cancel.queue_index = 0;
    require(PlanAiSemanticActionV1(input, cancel).code ==
            AiActionPlanCode::unsupported_queue_index,
        "unverified indexed cancellation was exposed");
    tyrano_nest.deferred_command_count = 0;
    tyrano_nest.command_state = 4;
    cancel.queue_index = kAiLatestQueueIndex;
    require(PlanAiSemanticActionV1(input, cancel).code ==
            AiActionPlanCode::nothing_to_cancel,
        "CancelProduction accepted an idle source");

    move.unit_ids = {worker.id, worker.id};
    require(PlanAiSemanticActionV1(input, move).code ==
            AiActionPlanCode::duplicate_unit_id,
        "duplicate source unit was accepted");
    move.unit_ids = {enemy.id};
    require(PlanAiSemanticActionV1(input, move).code ==
            AiActionPlanCode::unit_not_owned,
        "foreign source unit was accepted");
    move.unit_ids = {incapable.id};
    require(PlanAiSemanticActionV1(input, move).code ==
            AiActionPlanCode::unit_action_unsupported,
        "incapable source unit was accepted");
    move.unit_ids = {worker.id};
    move.target_x = 128;
    move.target_y = 20;
    require(PlanAiSemanticActionV1(input, move).code ==
            AiActionPlanCode::point_out_of_bounds,
        "out-of-map point was accepted");
}

struct AbilityAvailabilityFixture {
    bool available = true;
    u32 expected_ability_id = 0;
};

AiAbilityAvailability ability_from_fixture(const UnitMovementUnit& source,
    u32 ability_id, u32 target_unit_id, i32, i32, u32 local_owner,
    void* user_data) {
    const auto* fixture =
        static_cast<const AbilityAvailabilityFixture*>(user_data);
    AiAbilityAvailability availability{};
    availability.available = fixture != nullptr && fixture->available &&
        ability_id == fixture->expected_ability_id && local_owner == 0 &&
        source.owner_id == 0 && target_unit_id != source.id;
    availability.secondary_cost = 60;
    return availability;
}

void test_semantic_action_v2_planning() {
    constexpr u32 kMove = 1u << 4;
    constexpr u32 kAttack = 1u << 5;
    constexpr u32 kHarvest = 1u << 7;
    constexpr u32 kPatrol = 1u << 9;
    constexpr u32 kCarry = 1u << 0xa;
    constexpr u32 kMerge = 1u << 0xb;
    constexpr u32 kMorphEnter = 1u << 0x11;
    constexpr u32 kStance2 = 1u << 0x14;
    constexpr u32 kTransfer = 1u << 1;
    constexpr u32 kMorphGate = 0x20000u;

    PlayerSlotRuntimeState players{};
    players.owner_relation_masks[0] = 0x01;

    // Audited Tyrano shapes (ai_techtree_audit caps rows): 벨로시스 pair-merges
    // into 트윈 벨로시스 and morphs into wild type 0x45; the mutant triad is
    // 딜로포스+프테라스+트리세스; 둥가리 is the pure carrier.
    UnitMovementUnit velocis_a = make_unit(
        0x100, 1, 0, 0x22, kMove | kAttack | kPatrol | kMerge | kMorphEnter |
            kMorphGate | kStance2 | kTransfer, 32, 32);
    UnitMovementUnit velocis_b = make_unit(
        0x110, 2, 0, 0x22, velocis_a.type_flags, 40, 32);
    velocis_a.definition.linked_release_type_id = 0x23;
    velocis_b.definition.linked_release_type_id = 0x23;
    velocis_a.definition.morph_type_id = 0x45;
    velocis_b.definition.morph_type_id = 0x45;
    UnitMovementUnit dilophos = make_unit(
        0x120, 3, 0, 0x24, kMove | kAttack | kMerge, 48, 40);
    UnitMovementUnit pteras = make_unit(
        0x130, 4, 0, 0x27, kMove | kAttack | kMerge, 56, 40);
    UnitMovementUnit triceps = make_unit(
        0x140, 5, 0, 0x28, kMove | kAttack | kMerge, 64, 40);
    UnitMovementUnit carrier = make_unit(
        0x150, 6, 0, 0x29, kMove | kCarry, 72, 48);
    UnitMovementUnit worker = make_unit(
        0x160, 7, 0, 0x20, kMove | kHarvest, 80, 48);
    worker.definition.action_effect_flags = 0x4;
    worker.definition.transport_flags = 0x4;
    UnitMovementUnit enemy = make_unit(
        0x170, 8, 1, 0x10, kMove | kAttack, 88, 56);

    UnitMovementContext movement{};
    seed_map(movement);
    movement.active_units = {&velocis_a, &velocis_b, &dilophos, &pteras,
        &triceps, &carrier, &worker, &enemy};
    VisibilityFixture visibility{{enemy.id}};

    AiActionPlanInput input{};
    input.local_owner = 0;
    input.players = &players;
    input.movement = &movement;
    input.unit_visible = visible_from_fixture;
    input.unit_visibility_user_data = &visibility;

    // Cross-field hygiene: v2 payload fields stay rejected on v1 kinds.
    AiSemanticAction stray{};
    stray.kind = AiSemanticActionKind::move;
    stray.unit_ids = {velocis_a.id};
    stray.target_x = 30;
    stray.target_y = 30;
    stray.ability_id = 3;
    require(PlanAiSemanticActionV1(input, stray).code ==
            AiActionPlanCode::unexpected_ability,
        "ability_id leaked into a non-ability action");
    stray.ability_id = kAiNoAbilityId;
    stray.stance_id = 1;
    require(PlanAiSemanticActionV1(input, stray).code ==
            AiActionPlanCode::invalid_stance,
        "stance_id leaked into a non-stance action");

    // Stop / hold position.
    AiSemanticAction stop{};
    stop.kind = AiSemanticActionKind::stop;
    stop.unit_ids = {velocis_a.id, velocis_b.id};
    const AiActionPlanResult stop_plan = PlanAiSemanticActionV1(input, stop);
    require(stop_plan && stop_plan.packets.size() == 2 &&
        stop_plan.packets[0].subtype == 0x02 &&
        stop_plan.packets[0].arg0 == 0x00 &&
        stop_plan.packets[0].arg1 == 0,
        "Stop did not plan bare command-0x00 orders");
    stop.queued = true;
    require(PlanAiSemanticActionV1(input, stop).code ==
            AiActionPlanCode::queued_flag_unsupported,
        "queued Stop was accepted");

    AiSemanticAction hold{};
    hold.kind = AiSemanticActionKind::hold_position;
    hold.unit_ids = {velocis_a.id};
    const AiActionPlanResult hold_plan = PlanAiSemanticActionV1(input, hold);
    require(hold_plan && hold_plan.packets.size() == 1 &&
        hold_plan.packets[0].subtype == 0x0a &&
        hold_plan.packets[0].packed_opcode == 0x0a000000 &&
        hold_plan.packets[0].arg0 == 0x21 &&
        hold_plan.packets[0].arg2 == static_cast<u32>(velocis_a.x),
        "HoldPosition did not mirror the subtype-0x0a forced order");

    // Patrol.
    AiSemanticAction patrol{};
    patrol.kind = AiSemanticActionKind::patrol;
    patrol.unit_ids = {velocis_a.id};
    patrol.target_x = 100;
    patrol.target_y = 80;
    const AiActionPlanResult patrol_plan =
        PlanAiSemanticActionV1(input, patrol);
    require(patrol_plan && patrol_plan.packets[0].arg0 == 0x09 &&
        patrol_plan.packets[0].arg2 == 100 &&
        patrol_plan.packets[0].arg3 == 80,
        "Patrol did not plan command 0x09 to the point");
    patrol.unit_ids = {worker.id};
    require(PlanAiSemanticActionV1(input, patrol).code ==
            AiActionPlanCode::unit_action_unsupported,
        "Patrol accepted a unit without the capability bit");

    // Ability cast (subtype 0x09, command byte = ability id).
    AbilityAvailabilityFixture ability_fixture{};
    ability_fixture.expected_ability_id = 0x03;
    AiSemanticAction cast{};
    cast.kind = AiSemanticActionKind::use_ability;
    cast.unit_ids = {velocis_a.id};
    cast.ability_id = 0x03;
    cast.target_unit_id = enemy.id;
    require(PlanAiSemanticActionV1(input, cast).code ==
            AiActionPlanCode::missing_ability_validator,
        "UseAbility did not fail closed without its live validator");
    input.ability_available = ability_from_fixture;
    input.ability_availability_user_data = &ability_fixture;
    const AiActionPlanResult cast_plan = PlanAiSemanticActionV1(input, cast);
    require(cast_plan && cast_plan.packets.size() == 1 &&
        cast_plan.packets[0].subtype == 0x09 &&
        cast_plan.packets[0].packed_opcode == 0x09000000 &&
        cast_plan.packets[0].arg0 == 0x03 &&
        cast_plan.packets[0].arg1 == enemy.id &&
        cast_plan.packets[0].arg2 == static_cast<u32>(enemy.x),
        "UseAbility did not plan the subtype-0x09 ability packet");
    ability_fixture.available = false;
    require(PlanAiSemanticActionV1(input, cast).code ==
            AiActionPlanCode::ability_unavailable,
        "UseAbility ignored a live validator rejection");
    ability_fixture.available = true;
    cast.ability_id = 0x2e;
    require(PlanAiSemanticActionV1(input, cast).code ==
            AiActionPlanCode::invalid_ability_id,
        "UseAbility accepted an out-of-range ability id");

    // Morph enter / exit.
    AiSemanticAction morph{};
    morph.kind = AiSemanticActionKind::morph_enter;
    morph.unit_ids = {velocis_a.id};
    const AiActionPlanResult morph_plan = PlanAiSemanticActionV1(input, morph);
    require(morph_plan && morph_plan.packets[0].arg0 == 0x11,
        "MorphEnter did not plan command 0x11");
    velocis_a.runtime_flags |= 0x40000u;
    require(PlanAiSemanticActionV1(input, morph).code ==
            AiActionPlanCode::morph_unavailable,
        "MorphEnter accepted an already-morphed unit");
    morph.kind = AiSemanticActionKind::morph_exit;
    require(PlanAiSemanticActionV1(input, morph).code ==
            AiActionPlanCode::not_morphed,
        "MorphExit accepted a unit without the post-morph type flag");
    velocis_a.type_flags |= 0x08000000u;
    const AiActionPlanResult morph_exit_plan =
        PlanAiSemanticActionV1(input, morph);
    require(morph_exit_plan && morph_exit_plan.packets[0].arg0 == 0x1b,
        "MorphExit did not plan command 0x1b");
    velocis_a.type_flags &= ~0x08000000u;
    velocis_a.runtime_flags &= ~0x40000u;

    // Merge: mirrored pair, then the hard-coded mutant triad ring.
    AiSemanticAction merge{};
    merge.kind = AiSemanticActionKind::merge_units;
    merge.unit_ids = {velocis_b.id, velocis_a.id};
    const AiActionPlanResult pair_plan = PlanAiSemanticActionV1(input, merge);
    require(pair_plan && pair_plan.packets.size() == 2 &&
        pair_plan.packets[0].arg0 == 0x0b &&
        pair_plan.packets[0].unit_offset == velocis_a.id &&
        pair_plan.packets[0].arg1 == velocis_b.id &&
        pair_plan.packets[1].unit_offset == velocis_b.id &&
        pair_plan.packets[1].arg1 == velocis_a.id,
        "pair merge did not plan the mirrored 0x0b orders");
    merge.unit_ids = {velocis_a.id, dilophos.id};
    require(PlanAiSemanticActionV1(input, merge).code ==
            AiActionPlanCode::merge_recipe_invalid,
        "pair merge accepted mismatched types");
    merge.unit_ids = {dilophos.id, pteras.id, triceps.id};
    const AiActionPlanResult triad_plan = PlanAiSemanticActionV1(input, merge);
    require(triad_plan && triad_plan.packets.size() == 3 &&
        triad_plan.packets[0].unit_offset == triceps.id &&
        triad_plan.packets[0].arg1 == dilophos.id &&
        triad_plan.packets[1].unit_offset == pteras.id &&
        triad_plan.packets[1].arg1 == triceps.id &&
        triad_plan.packets[2].unit_offset == dilophos.id &&
        triad_plan.packets[2].arg1 == pteras.id,
        "mutant triad did not plan the three-packet ring");
    merge.unit_ids = {dilophos.id, pteras.id, velocis_a.id};
    require(PlanAiSemanticActionV1(input, merge).code ==
            AiActionPlanCode::merge_recipe_invalid,
        "triad merge accepted a wrong member set");
    merge.unit_ids = {velocis_a.id};
    require(PlanAiSemanticActionV1(input, merge).code ==
            AiActionPlanCode::merge_arity_invalid,
        "single-unit merge was accepted");

    // Transport board / unload.
    AiSemanticAction board{};
    board.kind = AiSemanticActionKind::board_transport;
    board.unit_ids = {worker.id};
    board.target_unit_id = carrier.id;
    const AiActionPlanResult board_plan = PlanAiSemanticActionV1(input, board);
    require(board_plan && board_plan.packets.size() == 1 &&
        board_plan.packets[0].arg0 == 0x0a &&
        board_plan.packets[0].arg1 == carrier.id &&
        board_plan.packets[0].arg2 == static_cast<u32>(carrier.x),
        "BoardTransport did not plan command 0x0a onto the carrier");
    board.target_unit_id = velocis_a.id;
    require(PlanAiSemanticActionV1(input, board).code ==
            AiActionPlanCode::target_not_carrier,
        "BoardTransport accepted a non-carrier target");
    board.target_unit_id = carrier.id;
    board.unit_ids = {velocis_a.id};
    require(PlanAiSemanticActionV1(input, board).code ==
            AiActionPlanCode::passenger_cannot_board,
        "BoardTransport accepted a non-boardable passenger");

    AiSemanticAction unload{};
    unload.kind = AiSemanticActionKind::unload_transport;
    unload.unit_ids = {carrier.id};
    unload.target_x = 96;
    unload.target_y = 64;
    const AiActionPlanResult unload_plan =
        PlanAiSemanticActionV1(input, unload);
    require(unload_plan && unload_plan.packets[0].arg0 == 0x24 &&
        unload_plan.packets[0].arg2 == 96 &&
        unload_plan.packets[0].arg3 == 64,
        "UnloadTransport did not plan command 0x24 to the point");
    unload.unit_ids = {worker.id};
    require(PlanAiSemanticActionV1(input, unload).code ==
            AiActionPlanCode::target_not_carrier,
        "UnloadTransport accepted a non-carrier source");

    // Secondary-value balance.
    velocis_a.runtime_flags |= 1u;
    velocis_b.runtime_flags |= 1u;
    velocis_a.action_mode = 10;
    velocis_b.action_mode = 0;
    AiSemanticAction transfer{};
    transfer.kind = AiSemanticActionKind::transfer_secondary;
    transfer.unit_ids = {velocis_a.id, velocis_b.id};
    const AiActionPlanResult transfer_plan =
        PlanAiSemanticActionV1(input, transfer);
    require(transfer_plan && transfer_plan.packets.size() == 1 &&
        transfer_plan.packets[0].arg0 == 0x23 &&
        transfer_plan.packets[0].unit_offset == velocis_a.id + 0x10 &&
        transfer_plan.packets[0].arg1 == velocis_a.id &&
        transfer_plan.packets[0].arg2 == 5,
        "TransferSecondary did not pair donor/recipient at the mean");
    velocis_b.action_mode = 10;
    require(PlanAiSemanticActionV1(input, transfer).code ==
            AiActionPlanCode::nothing_to_transfer,
        "TransferSecondary planned packets for a balanced group");
    velocis_a.action_mode = 3;
    velocis_b.action_mode = 3;

    // Stance toggle (Tyrano ships stance #2 = command 0x14 / flag 0x10000).
    AiSemanticAction stance{};
    stance.kind = AiSemanticActionKind::set_stance;
    stance.unit_ids = {velocis_a.id};
    stance.stance_id = 2;
    stance.stance_on = true;
    const AiActionPlanResult stance_on_plan =
        PlanAiSemanticActionV1(input, stance);
    require(stance_on_plan && stance_on_plan.packets[0].arg0 == 0x14 &&
        stance_on_plan.packets[0].arg1 == 0,
        "stance-on did not plan command 0x14");
    stance.stance_on = false;
    velocis_a.command_flags |= 0x10000u;
    const AiActionPlanResult stance_off_plan =
        PlanAiSemanticActionV1(input, stance);
    require(stance_off_plan && stance_off_plan.packets[0].arg0 == 0x14 &&
        stance_off_plan.packets[0].arg1 == 1 &&
        stance_off_plan.packets[0].arg2 == 0x10000,
        "stance-off did not plan the cancel form");
    velocis_a.command_flags &= ~0x10000u;
    require(PlanAiSemanticActionV1(input, stance).code ==
            AiActionPlanCode::stance_inactive,
        "stance-off was planned without the active flag");
    stance.stance_id = 7;
    require(PlanAiSemanticActionV1(input, stance).code ==
            AiActionPlanCode::invalid_stance,
        "out-of-range stance id was accepted");

    // Return cargo.
    AiSemanticAction return_cargo{};
    return_cargo.kind = AiSemanticActionKind::return_cargo;
    return_cargo.unit_ids = {worker.id};
    const AiActionPlanResult return_plan =
        PlanAiSemanticActionV1(input, return_cargo);
    require(return_plan && return_plan.packets[0].arg0 == 0x07 &&
        return_plan.packets[0].arg1 == 0x80000000u,
        "ReturnCargo did not plan the synthetic dropoff harvest");
    return_cargo.unit_ids = {carrier.id};
    require(PlanAiSemanticActionV1(input, return_cargo).code ==
            AiActionPlanCode::nothing_to_return,
        "ReturnCargo planned for a unit without cargo");

    // Item use.
    AiSemanticAction item{};
    item.kind = AiSemanticActionKind::use_item;
    item.unit_ids = {velocis_a.id};
    item.target_x = 40;
    item.target_y = 40;
    require(PlanAiSemanticActionV1(input, item).code ==
            AiActionPlanCode::missing_item,
        "UseItem planned without a usable item slot");
    velocis_a.item_slots[1] = 0x1b;
    const AiActionPlanResult item_plan = PlanAiSemanticActionV1(input, item);
    require(item_plan && item_plan.packets[0].arg0 == 0x16,
        "UseItem did not plan command 0x16");
    velocis_a.item_slots[1] = 0;
}

void test_live_validation_adapter() {
    GameSessionUnitReferenceTables references{};
    UnitTypeSessionDefinition& worker_references = references.definitions[32];
    worker_references.present = true;
    worker_references.primary_reference_count = 1;
    worker_references.primary_references[0] = 128;
    UnitTypeSessionDefinition& nest_references = references.definitions[128];
    nest_references.present = true;
    nest_references.alternate_reference_count = 1;
    nest_references.alternate_references[0] = 32;
    nest_references.completion_reference_count = 1;
    nest_references.completion_references[0] = 20;

    LiveValidationFixture fixture{};
    fixture.unit_result.available = true;
    fixture.unit_result.primary_cost = 75;
    fixture.research_result.available = true;
    fixture.research_result.primary_cost = 100;
    fixture.research_result.secondary_cost = 3;
    AiLiveProductionValidationContext context{};
    context.unit_references = &references;
    context.check_unit_requirements = live_unit_requirement;
    context.check_research_requirements = live_research_requirement;
    context.check_placement = live_placement;
    context.callback_user_data = &fixture;

    UnitMovementUnit nest = make_unit(0xfbf0, 10, 0, 128, 0, 320, 320);
    AiProductionAvailability result = CheckAiLiveProductionAvailability(
        AiProductionRequestKind::unit, nest, 32, 0, 0, 0, &context);
    require(result.available && result.primary_cost == 75 &&
        fixture.unit_calls == 1,
        "live validator rejected a catalog-backed TyranoNest production");

    result = CheckAiLiveProductionAvailability(
        AiProductionRequestKind::unit, nest, 33, 0, 0, 0, &context);
    require(!result.available &&
        result.code == kAiLiveValidationMissingReference &&
        fixture.unit_calls == 1,
        "live validator bypassed the producer reference list");

    result = CheckAiLiveProductionAvailability(
        AiProductionRequestKind::research, nest, 20, 0, 0, 0, &context);
    require(result.available && result.secondary_cost == 3 &&
        fixture.research_calls == 1,
        "live validator rejected a catalog-backed Tyrano research order");

    UnitMovementUnit worker = make_unit(0x1000, 2, 0, 32, 0, 256, 256);
    result = CheckAiLiveProductionAvailability(
        AiProductionRequestKind::building, worker, 128, 96, 64, 0, &context);
    require(!result.available &&
        result.code == kAiLiveValidationInvalidPlacement &&
        fixture.placement_calls == 1,
        "live validator accepted a placement predicate rejection");
    fixture.placement_result = true;
    result = CheckAiLiveProductionAvailability(
        AiProductionRequestKind::building, worker, 128, 96, 64, 0, &context);
    require(result.available && fixture.placement_calls == 2,
        "live validator rejected an authoritative valid placement");
}

AiObservedUnit observed_unit(u32 id, u32 owner, u32 type, u32 flags,
    i32 x, i32 y, bool controlled) {
    AiObservedUnit unit{};
    unit.id = id;
    unit.owner_id = owner;
    unit.type_id = type;
    unit.type_flags = flags;
    unit.x = x;
    unit.y = y;
    unit.controlled = controlled;
    unit.visible = true;
    unit.alive = true;
    return unit;
}

void test_tyrano_scripted_bot() {
    require(IsAiPlayCommandLineEnabled("--ai-play") &&
        IsAiPlayCommandLineEnabled("/AI-PLAY") &&
        IsAiPlayCommandLineEnabled("\"map name.trk\" --ai-play") &&
        !IsAiPlayCommandLineEnabled("--ai-player"),
        "AI play command-line opt-in parsing was not token exact");

    AiObservation observation{};
    observation.simulation_frame = 0;
    observation.map_width_tiles = 64;
    observation.map_height_tiles = 64;
    observation.local_owner = 0;
    observation.local_faction = kTyranoFactionId;
    observation.active_owner_mask = 3;
    observation.local_relation_mask = 1;
    observation.start_x = 320;
    observation.start_y = 320;
    observation.tiles.resize(
        observation.map_width_tiles * observation.map_height_tiles);
    constexpr u32 kResourceTileX = 12;
    constexpr u32 kResourceTileY = 9;
    AiObservedMapTile& resource = observation.tiles[
        kResourceTileY * observation.map_width_tiles + kResourceTileX];
    resource.passable = true;
    resource.explored = true;
    resource.visible = true;
    resource.resource_amount = 4000;
    constexpr u32 kWorkerCapabilities =
        (1u << 4) | (1u << 5) | (1u << 7);
    observation.units.push_back(observed_unit(
        0x1000, 0, kTyranoWorkerType, kWorkerCapabilities,
        300, 300, true));
    observation.units.push_back(observed_unit(
        0x1010, 0, kTyranoWorkerType, kWorkerCapabilities,
        332, 300, true));
    observation.units.push_back(observed_unit(
        0xfbf0, 0, kTyranoNestType, 0, 320, 320, true));

    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 8;
    config.desired_worker_count = 2;
    config.desired_harvester_count = 1;
    TyranoScriptedBotState state{};
    TyranoScriptedBotDecision decision = DecideTyranoScriptedBotAction(
        state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::set_starting_rally &&
        decision.action.kind == AiSemanticActionKind::set_rally,
        "scripted bot did not configure the starting TyranoNest rally");
    CommitTyranoScriptedBotDecision(state, decision, true);
    require(state.rally_configured && state.actions_committed == 1,
        "scripted bot did not commit a published rally action");

    observation.simulation_frame = 4;
    require(DecideTyranoScriptedBotAction(state, observation, config).code ==
            TyranoScriptedBotDecisionCode::not_due,
        "scripted bot ignored its deterministic decision interval");

    observation.simulation_frame = 8;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::harvest_visible_resource &&
        decision.action.kind == AiSemanticActionKind::harvest &&
        decision.action.unit_ids.size() == 1 &&
        decision.action.unit_ids[0] == 0x1010 &&
        decision.action.target_x ==
            static_cast<i32>(kResourceTileX * 32u + 16u) &&
        decision.action.target_y ==
            static_cast<i32>(kResourceTileY * 32u + 16u),
        "scripted bot did not assign the nearest worker to visible resources");
    CommitTyranoScriptedBotDecision(state, decision, true);
    observation.units[1].command_state = kUnitStateWorkerApproachHarvest;

    observation.simulation_frame = 16;
    observation.units.push_back(observed_unit(
        0x2200, 1, 10, 0, 900, 900, false));
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::attack_visible_enemy &&
        decision.action.kind == AiSemanticActionKind::attack_unit &&
        decision.action.target_unit_id == 0x2200 &&
        decision.action.unit_ids.size() == 1 &&
        decision.action.unit_ids[0] == 0x1000,
        "scripted bot interrupted its harvester when forming a combat group");

    ResetTyranoScriptedBot(state);
    observation.units.erase(observation.units.begin() + 2,
        observation.units.end());
    observation.simulation_frame = 0;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::build_starting_nest &&
        decision.action.kind == AiSemanticActionKind::build &&
        decision.action.production_id == kTyranoNestType,
        "scripted bot did not recover a missing starting TyranoNest");

    ResetTyranoScriptedBot(state);
    state.rally_configured = true;
    observation.units.push_back(observed_unit(
        0xfbf0, 0, kTyranoNestType, 0, 320, 320, true));
    observation.primary_resources = config.worker_primary_resource_cost;
    observation.simulation_frame = 0;
    config.desired_worker_count = 3;
    config.desired_harvester_count = 0;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::produce_worker &&
        decision.action.kind == AiSemanticActionKind::produce_unit &&
        decision.action.production_id == kTyranoWorkerType,
        "scripted bot did not resume worker production after funding it");
}

// The idle set is defined positively: only runtime states 0x00 (no command)
// and 0x01 (idle/auto-acquire) with an empty command queue count.  Anything
// else -- travelling, harvesting, carrying, or holding a queued order -- is
// busy, and a worker that merely harvested at some point in the past must not
// stay excluded forever because of the stale cargo_amount union.
void test_idle_worker_detection() {
    AiObservation observation{};
    observation.map_width_tiles = 64;
    observation.map_height_tiles = 64;
    observation.local_owner = 0;
    observation.local_faction = kTyranoFactionId;
    observation.active_owner_mask = 1;
    observation.local_relation_mask = 1;
    observation.start_x = 320;
    observation.start_y = 320;
    observation.tiles.resize(
        observation.map_width_tiles * observation.map_height_tiles);
    constexpr u32 kResourceTileX = 12;
    constexpr u32 kResourceTileY = 9;
    AiObservedMapTile& resource = observation.tiles[
        kResourceTileY * observation.map_width_tiles + kResourceTileX];
    resource.passable = true;
    resource.explored = true;
    resource.visible = true;
    resource.resource_amount = 4000;
    constexpr u32 kWorkerCapabilities = (1u << 4) | (1u << 5) | (1u << 7);

    const auto worker = [&](u32 id) {
        return observed_unit(id, 0, kTyranoWorkerType, kWorkerCapabilities,
            300, 300, true);
    };

    // 0x00 (no command) and 0x01 (idle/auto-acquire) are the only idle states.
    AiObservedUnit no_command = worker(0x1000);
    no_command.command_state = 0;
    AiObservedUnit idle_acquire = worker(0x1001);
    idle_acquire.command_state = kUnitStateRuntimeIdleAcquire;
    // Deposited its cargo long ago: command_flags bit 2 is clear, but the
    // cargo_amount union still holds the last harvested amount.
    AiObservedUnit deposited = worker(0x1002);
    deposited.command_state = kUnitStateRuntimeIdleAcquire;
    deposited.cargo_amount = 8;
    // Idle-looking state, but a queued command resumes work next tick.
    AiObservedUnit queued = worker(0x1003);
    queued.command_state = kUnitStateRuntimeIdleAcquire;
    queued.deferred_command_count = 1;
    queued.deferred_commands.resize(1);
    // Walking somewhere on a move order.
    AiObservedUnit travelling = worker(0x1004);
    travelling.command_state = kUnitStateTravel;
    // Inside the harvest cycle.
    AiObservedUnit approaching = worker(0x1005);
    approaching.command_state = kUnitStateWorkerApproachHarvest;
    AiObservedUnit mining = worker(0x1006);
    mining.command_state = kUnitStateWorkerHarvestTile;
    // Knocked out of the harvest states but still carrying.
    AiObservedUnit carrying = worker(0x1007);
    carrying.command_state = kUnitStateRuntimeAttackTarget;
    carrying.command_flags = 4u;
    // The lockout/deferred-entry high bits must not hide the real state.
    AiObservedUnit locked_mining = worker(0x1008);
    locked_mining.command_state = kUnitStateWorkerHarvestTile | 0x40000000u;

    observation.units = {no_command, idle_acquire, deposited, queued,
        travelling, approaching, mining, carrying, locked_mining};

    const std::vector<AiSemanticAction> actions =
        PlanTyranoIdleWorkerHarvest(observation, observation.units.size(), {});
    std::vector<u32> assigned;
    for (const AiSemanticAction& action : actions) {
        require(action.kind == AiSemanticActionKind::harvest &&
            action.unit_ids.size() == 1,
            "idle worker autopilot emitted a malformed harvest action");
        assigned.push_back(action.unit_ids[0]);
    }
    std::sort(assigned.begin(), assigned.end());
    const std::vector<u32> expected{0x1000, 0x1001, 0x1002};
    require(assigned == expected,
        "idle worker autopilot did not select exactly the standing-still workers");

    // Excluding a unit still has to work on top of the idle filter.
    const std::vector<AiSemanticAction> excluded =
        PlanTyranoIdleWorkerHarvest(observation, observation.units.size(),
            {0x1001, 0x1002});
    require(excluded.size() == 1 && excluded[0].unit_ids.size() == 1 &&
        excluded[0].unit_ids[0] == 0x1000,
        "idle worker autopilot ignored its exclusion list");

    // A dead or still-building worker is never idle.
    observation.units = {no_command, idle_acquire};
    observation.units[0].alive = false;
    observation.units[1].under_construction = true;
    require(PlanTyranoIdleWorkerHarvest(observation, 4, {}).empty(),
        "idle worker autopilot ordered a dead or incomplete worker to harvest");
}

void add_observed_type(AiObservation& observation, u32 type_id, u32 count,
    u32& next_id) {
    for (u32 index = 0; index < count; ++index) {
        observation.units.push_back(observed_unit(
            next_id++, observation.local_owner, type_id, 0,
            observation.start_x + static_cast<i32>(index * 32u),
            observation.start_y + static_cast<i32>(type_id), true));
    }
}

AiObservation tyrano_build_order_observation() {
    AiObservation observation{};
    observation.map_width_tiles = 64;
    observation.map_height_tiles = 64;
    observation.local_owner = 0;
    observation.local_faction = kTyranoFactionId;
    observation.active_owner_mask = 3;
    observation.local_relation_mask = 1;
    observation.primary_resources = 10000;
    observation.population_limit = 200;
    observation.start_x = 320;
    observation.start_y = 320;
    observation.units.push_back(observed_unit(
        0x1000, 0, kTyranoWorkerType,
        (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7),
        300, 300, true));
    observation.units.push_back(observed_unit(
        0x2000, 0, kTyranoNestType, 0, 320, 320, true));
    return observation;
}

void test_tyrano_replay_derived_build_order() {
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    config.desired_worker_count = 1;
    config.desired_harvester_count = 0;

    TyranoScriptedBotState state{};
    state.rally_configured = true;
    AiObservation observation = tyrano_build_order_observation();

    TyranoScriptedBotDecision decision = DecideTyranoScriptedBotAction(
        state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::build_population_nest &&
        decision.action.kind == AiSemanticActionKind::build &&
        decision.action.production_id == kTyranoPopulationNestType,
        "replay-derived build order did not start with a population Nest");

    CommitTyranoScriptedBotDecision(state, decision, false,
        AiActionPlanCode::production_unavailable);
    observation.simulation_frame = 1;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision && decision.intent == TyranoScriptedBotIntent::explore,
        "failed build intent blocked all useful actions during retry backoff");

    observation.simulation_frame = 64;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::build_population_nest,
        "failed build intent was not retried after its deterministic backoff");
    CommitTyranoScriptedBotDecision(state, decision, true);

    u32 next_id = 0x3000;
    add_observed_type(observation, kTyranoPopulationNestType, 1, next_id);
    observation.simulation_frame = 65;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::build_egg_nest &&
        decision.action.production_id == kTyranoEggNestType,
        "build order did not advance from Nest to the first EggNest");

    add_observed_type(observation, kTyranoEggNestType, 1, next_id);
    observation.simulation_frame = 66;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::build_egg_nest &&
        decision.action.production_id == kTyranoEggNestType,
        "build order did not request the replay's second EggNest");

    add_observed_type(observation, kTyranoEggNestType, 1, next_id);
    observation.simulation_frame = 67;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::produce_masos &&
        decision.action.kind == AiSemanticActionKind::produce_unit &&
        decision.action.production_id == kTyranoMasosType,
        "build order did not begin Masos production after two EggNests");

    state = TyranoScriptedBotState{};
    state.rally_configured = true;
    observation = tyrano_build_order_observation();
    next_id = 0x4000;
    add_observed_type(observation, kTyranoPopulationNestType, 3, next_id);
    add_observed_type(observation, kTyranoEggNestType, 3, next_id);
    add_observed_type(observation, kTyranoMasosType, 9, next_id);
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent ==
            TyranoScriptedBotIntent::research_harvest_upgrade &&
        decision.action.kind == AiSemanticActionKind::research &&
        decision.action.production_id == kTyranoHarvestUpgradeOrder &&
        decision.action.unit_ids[0] == 0x2000,
        "build order did not schedule the replay's berry harvest upgrade");
    CommitTyranoScriptedBotDecision(state, decision, true);
    require(state.harvest_upgrade_requested,
        "published harvest research did not advance macro state");

    observation.simulation_frame = 1;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::produce_masos &&
        decision.action.production_id == kTyranoMasosType,
        "build order did not resume Masos production after harvest research");

    state = TyranoScriptedBotState{};
    state.rally_configured = true;
    state.harvest_upgrade_requested = true;
    observation = tyrano_build_order_observation();
    next_id = 0x5000;
    add_observed_type(observation, kTyranoPopulationNestType, 5, next_id);
    add_observed_type(observation, kTyranoEggNestType, 3, next_id);
    add_observed_type(observation, kTyranoLandNestType, 1, next_id);
    add_observed_type(observation, kTyranoMasosType,
        config.desired_masos_count, next_id);
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::research_ground_attack &&
        decision.action.production_id == kTyranoGroundAttackUpgradeOrder,
        "build order did not schedule the LandNest ground attack upgrade");
    CommitTyranoScriptedBotDecision(state, decision, true);

    add_observed_type(observation, kTyranoNestType, 1, next_id);
    add_observed_type(observation, kTyranoPopulationNestType, 1, next_id);
    observation.simulation_frame = 1;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::research_movement_upgrade &&
        decision.action.production_id == kTyranoMovementUpgradeOrder,
        "build order did not schedule the late movement upgrade");
    CommitTyranoScriptedBotDecision(state, decision, true);

    observation.simulation_frame = 2;
    decision = DecideTyranoScriptedBotAction(state, observation, config);
    require(decision &&
        decision.intent == TyranoScriptedBotIntent::produce_dilophos &&
        decision.action.production_id == kTyranoDilophosType,
        "build order did not transition from upgrades to Dilophos production");
}

void test_ai_rl_reward() {
    AiRlRewardConfig config{};

    // Base state: economy + one nest, no army, no visible enemy.
    AiObservation base = tyrano_build_order_observation();
    const float phi_base = AiRlPotential(base, config);
    require(phi_base > 0.0f,
        "RL potential of a developed base was not positive");

    // Adding own army must raise the potential.
    AiObservation with_army = base;
    u32 next_id = 0x7000;
    add_observed_type(with_army, kTyranoMasosType, 5, next_id);
    const float phi_army = AiRlPotential(with_army, config);
    require(phi_army > phi_base, "own army did not increase the RL potential");

    // A visible enemy army must lower the potential relative to no enemy.
    AiObservation with_enemy = base;
    with_enemy.units.push_back(
        observed_unit(0x9001, 1, kTyranoMasosType, 0, 900, 900, false));
    const float phi_enemy = AiRlPotential(with_enemy, config);
    require(phi_enemy < phi_base,
        "visible enemy army did not decrease the RL potential");

    // Potential-based shaping telescopes exactly to Phi(cur) - Phi(prev) at
    // gamma = 1 for a non-terminal step.
    AiRlRewardConfig undisc = config;
    undisc.discount = 1.0f;
    AiRlStepReward grow = ComputeAiRlStepReward(base, with_army, undisc);
    require(grow.outcome == AiRlTerminalOutcome::ongoing &&
        grow.terminal == 0.0f,
        "non-terminal step produced a terminal reward");
    require(std::fabs(grow.shaping - (phi_army - phi_base)) < 1e-5f &&
        grow.total == grow.shaping,
        "shaping did not equal gamma*Phi(cur) - Phi(prev) at gamma=1");
    require(grow.shaping > 0.0f,
        "developing an army yielded non-positive shaping");

    // Terminal win: own units survive, no hostile visible -> +win, Phi_next = 0
    // so the shaping telescopes to -Phi(prev).
    AiObservation win = with_army;
    win.game_ended = true;
    require(ClassifyAiRlTerminal(win) == AiRlTerminalOutcome::win,
        "surviving-with-no-enemy end state was not classified a win");
    AiRlStepReward win_step = ComputeAiRlStepReward(base, win, config);
    require(win_step.outcome == AiRlTerminalOutcome::win &&
        win_step.terminal == config.win_reward,
        "terminal win did not yield the win reward");
    require(std::fabs(win_step.shaping + phi_base) < 1e-5f,
        "terminal shaping did not drop Phi(next) to zero");

    // Terminal loss: no controlled units remain.
    AiObservation loss{};
    loss.local_faction = kTyranoFactionId;
    loss.game_ended = true;
    require(ClassifyAiRlTerminal(loss) == AiRlTerminalOutcome::loss,
        "eliminated end state was not classified a loss");
    AiRlStepReward loss_step = ComputeAiRlStepReward(base, loss, config);
    require(loss_step.terminal == config.loss_reward,
        "terminal loss did not yield the loss reward");

    // BUILDING-based judgment (melee elimination rule): stray mobile units
    // without a single building are still a LOSS...
    AiObservation building_loss{};
    building_loss.local_faction = kTyranoFactionId;
    building_loss.game_ended = true;
    u32 stray_id = 0x7800;
    add_observed_type(building_loss, kTyranoMasosType, 3, stray_id);
    require(ClassifyAiRlTerminal(building_loss) == AiRlTerminalOutcome::loss,
        "buildingless survivors were not classified a loss");
    // ...and visible enemy MOBILES without any enemy building do not block a
    // win while our base stands (the old unit-count rule called this a draw).
    AiObservation building_win = with_enemy;
    building_win.game_ended = true;
    require(ClassifyAiRlTerminal(building_win) == AiRlTerminalOutcome::win,
        "own-base-standing vs building-less enemy was not a win");

    // Determinism: identical inputs -> byte-identical reward.
    AiRlStepReward again = ComputeAiRlStepReward(base, with_army, undisc);
    require(again.total == grow.total && again.shaping == grow.shaping &&
        again.potential_cur == grow.potential_cur,
        "reward computation was not deterministic");
}

void test_ai_rl_trace() {
    AiRlRewardConfig config{};

    AiObservation base = tyrano_build_order_observation();
    AiObservation with_army = base;
    u32 next_id = 0x8000;
    add_observed_type(with_army, kTyranoMasosType, 5, next_id);

    const AiRlStepEncoding enc_base = EncodeAiObservationForRl(base);
    const AiRlStepEncoding enc_army = EncodeAiObservationForRl(with_army);

    AiRlOwnerTrace trace;
    // First decision: no reward emitted yet (nothing precedes it).
    AiRlTraceRecordDecision(trace, 0, 3 /*produce_dilophos*/, enc_base, config);
    require(trace.steps.empty() && trace.has_prev,
        "first decision should buffer, not emit a reward step");

    // Second decision: emits the reward for the first action, carrying the first
    // state's features (aligned s_t, a_t, r_t).
    AiRlTraceRecordDecision(trace, 8, 2 /*produce_masos*/, enc_army, config);
    require(trace.steps.size() == 1, "second decision did not emit one step");
    const AiRlTraceStep& first = trace.steps[0];
    require(first.frame == 0 && first.action == 3 && !first.done,
        "emitted step did not describe the first action");
    require(first.features == enc_base.features &&
        first.legal_mask == enc_base.legal_mask,
        "emitted step did not carry the decision state s_t");
    const float expected_shaping =
        config.discount * AiRlPotentialFromFeatures(enc_army.features, config) -
        AiRlPotentialFromFeatures(enc_base.features, config);
    require(std::fabs(first.shaping - expected_shaping) < 1e-5f &&
        first.terminal == 0.0f,
        "emitted step reward did not match gamma*Phi(s')-Phi(s)");

    // Terminal: flush the last pending action as a win, done=true.
    AiRlTraceFinalize(trace, AiRlTerminalOutcome::win, config);
    require(trace.steps.size() == 2 && trace.steps[1].done &&
        trace.steps[1].action == 2 &&
        trace.steps[1].terminal == config.win_reward &&
        trace.steps[1].features == enc_army.features,
        "terminal flush did not emit the final win transition");
    require(!trace.has_prev &&
        trace.final_outcome == AiRlTerminalOutcome::win,
        "trace was not marked finalized");

    float sum = 0.0f;
    for (const AiRlTraceStep& step : trace.steps) {
        sum += step.total;
    }
    require(std::fabs(sum - trace.return_sum) < 1e-5f,
        "return_sum did not equal the sum of per-step rewards");
}

void test_ai_rl_producer_queue_capacity_mask() {
    // A produce action is legal only while some producer of that kind still has
    // room in its queue.  The planner rejects an order once every candidate is
    // at kUnitProductionQueueLimit (AiActionPlanCode::production_queue_full),
    // and a masked-legal-but-rejected pick costs the owner a whole decision
    // cycle -- self-play logs showed ~81% of produce picks failing this way.
    AiObservation obs = tyrano_build_order_observation();
    // population_used is the SUPPLY the nests provide; leave headroom so the
    // pop gate never masks what this test is measuring.
    obs.population_used = 40;
    obs.population_reserved = 4;
    obs.units.push_back(observed_unit(0x4000, 0, kTyranoEggNestType, 0,
        352, 352, true));
    AiObservedUnit& egg = obs.units.back();
    AiObservedUnit& base = obs.units[1];

    const auto masos_legal = [](const AiObservation& o) {
        return EncodeAiObservationForRl(o).legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::produce_masos)] == 1;
    };
    const auto worker_legal = [](const AiObservation& o) {
        return EncodeAiObservationForRl(o).legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::produce_worker)] == 1;
    };

    require(masos_legal(obs), "produce_masos was not legal with an idle egg nest");
    egg.deferred_command_count = kUnitProductionQueueLimit - 1;
    require(masos_legal(obs),
        "produce_masos went illegal while the egg nest still had queue room");
    egg.deferred_command_count = kUnitProductionQueueLimit;
    require(!masos_legal(obs),
        "produce_masos stayed legal with the only egg nest queue-full");

    // A second, idle egg nest restores it: the bot picks the least-busy
    // producer, so one free nest is enough.
    obs.units.push_back(observed_unit(0x4001, 0, kTyranoEggNestType, 0,
        384, 384, true));
    require(masos_legal(obs),
        "produce_masos stayed illegal despite a second idle egg nest");

    // An under-construction nest is not a producer, full or not.
    obs.units.back().under_construction = true;
    require(!masos_legal(obs),
        "an under-construction egg nest was counted as free producer capacity");

    // The base nest gates workers independently of the egg nest.
    require(worker_legal(obs), "produce_worker was not legal with an idle base");
    base.deferred_command_count = kUnitProductionQueueLimit;
    require(!worker_legal(obs),
        "produce_worker stayed legal with the only base nest queue-full");
}

void test_ai_rl_hunt_and_research() {
    AiObservation obs = tyrano_build_order_observation();
    // A completed Masos that can attack (command bit 5).
    obs.units.push_back(observed_unit(0x3000, 0, kTyranoMasosType,
        (1u << 5), 400, 400, true));
    // A neutral monster (owner 8, mobile) in view.
    obs.units.push_back(observed_unit(0x9100, kNeutralMonsterOwnerId,
        0x30, 0, 500, 500, false));
    // Research: harvest upgrade level 2, ground-attack level 1, movement none.
    obs.research_levels = {2, 0, 1};

    // The encoding surfaces research + neutral state and legalizes hunting.
    const AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
        AiRlHighLevelAction::hunt_neutral_monster)] == 1,
        "hunt was not legal with an army and a visible neutral monster");
    require(enc.features[41] > 0.5f && enc.features[39] > 0.0f,
        "neutral-monster features (count/has) were not encoded");
    require(enc.features[36] > 0.6f && enc.features[37] == 0.0f &&
        enc.features[38] > 0.3f,
        "research-level features did not reflect research_levels");

    // The translator turns hunt into an attack on the nearest neutral monster.
    TyranoScriptedBotState state{};
    state.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    const TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(state, obs,
            AiRlHighLevelAction::hunt_neutral_monster, config);
    const AiMicroObjective& hunt =
        AiMicroObjectiveOf(state.micro, AiMicroGroup::army);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        hunt.kind == AiMicroObjectiveKind::attack &&
        hunt.tactic == AiMicroAttackTactic::neutral_only,
        "hunt did not set a neutral_only attack objective");
    // ...and the micro executor picks the monster as the group target and
    // sends the fighter at it.
    obs.simulation_frame = 5;
    const std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(state.micro, obs);
    require(hunt.target_unit_id == 0x9100,
        "executor did not derive the neutral monster as the group target");
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9100 &&
        orders[0].unit_ids == std::vector<u32>{0x3000},
        "executor did not attack the hunted neutral monster");
}

void test_ai_rl_research_tree_walk() {
    // Per-order research: the policy names the order, the executor routes it
    // to an IDLE researcher of the audited building type, refuses capped
    // orders and busy researchers (restart-drain), and the mask agrees.
    AiObservation obs = tyrano_build_order_observation();
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;

    // Harvest upgrade (0x14) at the idle base nest.
    {
        TyranoScriptedBotState state{};
        const TyranoScriptedBotDecision decision =
            DecideTyranoScriptedBotForHighLevelAction(state, obs,
                AiRlHighLevelAction::research_harvest, config);
        require(decision &&
            decision.action.kind == AiSemanticActionKind::research &&
            decision.action.production_id == kTyranoHarvestUpgradeOrder &&
            decision.action.unit_ids == std::vector<u32>{0x2000},
            "research_harvest did not start 0x14 at the idle base");
        const AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
        require(enc.legal_mask[static_cast<std::size_t>(
                    AiRlHighLevelAction::research_harvest)] == 1 &&
            enc.legal_mask[static_cast<std::size_t>(
                    AiRlHighLevelAction::research_morph)] == 1 &&
            enc.legal_mask[static_cast<std::size_t>(
                    AiRlHighLevelAction::research_ground_attack)] == 0 &&
            enc.legal_mask[static_cast<std::size_t>(
                    AiRlHighLevelAction::research_air_attack)] == 0,
            "research mask did not follow researcher-building availability");
    }
    // Capped order -> no decision and masked out.
    obs.research_order_levels[kTyranoHarvestUpgradeOrder] = 1;
    {
        TyranoScriptedBotState state{};
        require(!DecideTyranoScriptedBotForHighLevelAction(state, obs,
                    AiRlHighLevelAction::research_harvest, config),
            "research_harvest restarted a completed order");
        require(EncodeAiObservationForRl(obs).legal_mask[static_cast<std::size_t>(
                    AiRlHighLevelAction::research_harvest)] == 0,
            "completed research stayed legal");
        // Another base order (morph 0x2a) still works at the idle base...
        TyranoScriptedBotState morph_state{};
        const TyranoScriptedBotDecision morph =
            DecideTyranoScriptedBotForHighLevelAction(morph_state, obs,
                AiRlHighLevelAction::research_morph, config);
        require(morph && morph.action.production_id == 0x2au,
            "research_morph did not start 0x2a at the idle base");
        // ...but a BUSY base (queued production) yields nothing.
        for (AiObservedUnit& unit : obs.units) {
            if (unit.type_id == kTyranoNestType) {
                unit.queued_production_type_id = kTyranoWorkerType;
            }
        }
        TyranoScriptedBotState busy_state{};
        require(!DecideTyranoScriptedBotForHighLevelAction(busy_state, obs,
                    AiRlHighLevelAction::research_morph, config) &&
            EncodeAiObservationForRl(obs).legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::research_morph)] == 0,
            "research targeted a BUSY researcher (restart-drain risk)");
    }
    // Land orders route to an idle completed land nest even while the base
    // stays busy; level-scaled cost gates the mask (0x19 lv2 costs 600).
    obs.units.push_back(observed_unit(0x4100, 0, kTyranoLandNestType, 0,
        600, 600, true));
    {
        TyranoScriptedBotState state{};
        const TyranoScriptedBotDecision decision =
            DecideTyranoScriptedBotForHighLevelAction(state, obs,
                AiRlHighLevelAction::research_ground_attack, config);
        require(decision &&
            decision.action.production_id == kTyranoGroundAttackUpgradeOrder &&
            decision.action.unit_ids == std::vector<u32>{0x4100},
            "research_ground_attack was not routed to the idle land nest");
        obs.research_order_levels[kTyranoGroundAttackUpgradeOrder] = 2;
        obs.primary_resources = 500;
        require(EncodeAiObservationForRl(obs).legal_mask[static_cast<std::size_t>(
                    AiRlHighLevelAction::research_ground_attack)] == 0,
            "research mask ignored the level-scaled cost");
        obs.primary_resources = 600;
        require(EncodeAiObservationForRl(obs).legal_mask[static_cast<std::size_t>(
                    AiRlHighLevelAction::research_ground_attack)] == 1,
            "research mask refused an affordable level-2 order");
    }
}

// Micro-executor fixture: the build-order observation (worker 0x1000 at
// 300,300 with harvest/attack bits, nest 0x2000 at 320,320) plus a fully
// passable explored map with one berry tile at (12, 9) -> world (400, 304).
AiObservation micro_observation() {
    AiObservation obs = tyrano_build_order_observation();
    obs.tiles.resize(obs.map_width_tiles * obs.map_height_tiles);
    for (AiObservedMapTile& tile : obs.tiles) {
        tile.passable = true;
        tile.buildable = true;
        tile.explored = true;
    }
    obs.tiles[9 * obs.map_width_tiles + 12].resource_amount = 500;
    obs.tiles[9 * obs.map_width_tiles + 12].terrain_flags = 0x100;  // berry terrain
    return obs;
}

AiObservedUnit fighter_unit(u32 id, u32 owner, i32 x, i32 y, bool controlled) {
    AiObservedUnit unit = observed_unit(id, owner, kTyranoMasosType, (1u << 5),
        x, y, controlled);
    unit.health = 100;
    unit.max_health = 100;
    unit.attack_range = 50;  // audited melee range (마소스)
    unit.attack_range_base = 50;
    unit.attack_range_vs_air = 50;
    return unit;
}

// Audited ranged Tyrano type (람포스, range 250).  Ranged units are the only
// ones the executor pulls out of a fight, so the pull-back tests need one.
AiObservedUnit ranged_fighter_unit(u32 id, u32 owner, i32 x, i32 y,
    bool controlled) {
    AiObservedUnit unit = fighter_unit(id, owner, x, y, controlled);
    unit.attack_range = 250;
    unit.attack_range_base = 250;
    unit.attack_range_vs_air = 250;
    return unit;
}

// A flying target: render class 3.  The engine resolves a separate range and
// damage profile against it, and a ground-only weapon cannot touch it at all.
AiObservedUnit flying_unit(u32 id, u32 owner, i32 x, i32 y, bool controlled) {
    AiObservedUnit unit = fighter_unit(id, owner, x, y, controlled);
    unit.render_class = 3;
    return unit;
}

void test_ai_micro_executor_harvest_and_flee() {
    AiObservation obs = micro_observation();
    TyranoScriptedBotState state{};
    obs.simulation_frame = 10;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::harvest &&
        orders[0].unit_ids == std::vector<u32>{0x1000} &&
        orders[0].target_x == 400 && orders[0].target_y == 304,
        "idle worker was not sent to the berry nearest its nest");
    require(AiMicroObjectiveOf(state.micro, AiMicroGroup::economy).kind ==
            AiMicroObjectiveKind::harvest &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::army).kind ==
            AiMicroObjectiveKind::defend,
        "default group objectives were not harvest / defend");
    // Already told it: no re-issue the next frame.
    obs.simulation_frame = 11;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "harvest order was re-issued while the unit was still tasked");
    // Working: nothing to do.
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.simulation_frame = 30;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "executor interfered with a harvesting worker");
    // A hostile fighter inside the worker's sight -> flee to the nest.
    obs.units.push_back(fighter_unit(0x9000, 1, 340, 300, false));
    obs.simulation_frame = 31;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move &&
        orders[0].unit_ids == std::vector<u32>{0x1000} &&
        orders[0].target_x == 320 && orders[0].target_y == 320,
        "threatened worker did not flee to the nest");
    // Threat gone and the worker idle again -> back to harvesting.
    obs.units.pop_back();
    obs.units[0].command_state = 0;
    obs.simulation_frame = 40;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::harvest,
        "worker did not resume harvesting after the threat left");
    // No berries anywhere -> the economy group falls back to defend.
    AiObservation barren = micro_observation();
    barren.tiles[9 * barren.map_width_tiles + 12].resource_amount = 0;
    TyranoScriptedBotState fresh{};
    AiMicroExecutorStep(fresh.micro, barren);
    require(AiMicroObjectiveOf(fresh.micro, AiMicroGroup::economy).kind ==
            AiMicroObjectiveKind::defend,
        "economy did not switch to defend with no berries left");
}

void test_ai_micro_executor_defend_bubble() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;  // busy
    obs.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    TyranoScriptedBotState state{};
    obs.simulation_frame = 100;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "defender moved with nothing to do");
    // Hostile seen far outside the nest bubble (one screen) -> ignored.
    obs.units.push_back(fighter_unit(0x9300, 1, 1800, 1800, false));
    obs.simulation_frame = 101;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "defender chased a hostile outside the defense bubble");
    obs.units.pop_back();
    // Hostile inside the bubble and within reach -> attack it (once).
    obs.units.push_back(fighter_unit(0x9200, 1, 520, 320, false));
    obs.simulation_frame = 102;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9200 &&
        orders[0].unit_ids == std::vector<u32>{0x3100},
        "defender did not engage the intruder");
    obs.units[2].command_state = kUnitStateAttackTarget;
    obs.simulation_frame = 103;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "attack order was re-issued to an engaged unit");
    // Engine dropped the order (unit idle) after the re-issue interval ->
    // issued again.
    obs.units[2].command_state = 0;
    obs.simulation_frame = 110;
    require(AiMicroExecutorStep(state.micro, obs).size() == 1,
        "dropped attack order was not re-issued to the idle unit");
    // Melee spread: four fighters on one target -> at most three attack it
    // (the first keeps its standing order, so two new orders go out).
    obs.units[2].command_state = kUnitStateAttackTarget;
    for (u32 i = 0; i < 3; ++i) {
        obs.units.push_back(fighter_unit(0x3200 + i, 0, 410 + static_cast<i32>(i),
            400, true));
    }
    obs.simulation_frame = 120;
    orders = AiMicroExecutorStep(state.micro, obs);
    std::size_t attackers = 0;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::attack_unit &&
            order.target_unit_id == 0x9200) {
            attackers += order.unit_ids.size();
        }
    }
    require(attackers == 2,
        "melee spread cap did not limit attackers per target");
    // Leash: a defender one screen away from every nest returns to the post.
    AiObservation far = micro_observation();
    far.units[0].command_state = kUnitStateWorkerApproachHarvest;
    far.units.push_back(fighter_unit(0x3100, 0, 1300, 400, true));
    TyranoScriptedBotState fresh{};
    far.simulation_frame = 200;
    orders = AiMicroExecutorStep(fresh.micro, far);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move &&
        orders[0].target_x == 320 && orders[0].target_y == 320,
        "defender outside the leash did not return to the post");
}

void test_ai_micro_executor_attack_retarget_retreat_and_pullback() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));
    TyranoScriptedBotState state{};
    obs.simulation_frame = 1;
    AiMicroExecutorStep(state.micro, obs);  // initialize records
    obs.units.push_back(fighter_unit(0x9400, 1, 450, 400, false));  // A
    obs.units.back().health = 50;
    obs.units.push_back(fighter_unit(0x9500, 1, 460, 420, false));  // B
    AiMicroObjective attack;
    attack.kind = AiMicroObjectiveKind::attack;
    attack.target_unit_id = 0x9500;
    AiMicroSetObjective(state.micro, AiMicroGroup::army, attack);
    obs.simulation_frame = 2;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    // Focus fire: the lowest-health reachable hostile (A) over the objective's
    // own target (B).
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9400,
        "attack did not focus the lowest-health hostile in reach");
    // A dies -> the fighter switches to B; the objective keeps attacking.
    obs.units[3].alive = false;
    obs.units[2].command_state = kUnitStateAttackTarget;
    obs.simulation_frame = 3;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9500 &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::army).kind ==
            AiMicroObjectiveKind::attack,
        "fighter did not move on to the next hostile after its target died");
    // B dies too, nothing else in sight -> stand still in attack (no order),
    // objective target cleared, still attack.
    obs.units[4].alive = false;
    obs.units[2].command_state = 0;
    obs.simulation_frame = 4;
    require(AiMicroExecutorStep(state.micro, obs).empty() &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::army).kind ==
            AiMicroObjectiveKind::attack &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::army).target_unit_id == 0,
        "attack objective did not stand still with nothing in sight");
    // Policy retreat: everyone moves to the nest, no engagement even with a
    // hostile in reach.
    obs.units[4].alive = true;
    AiMicroObjective retreat;
    retreat.kind = AiMicroObjectiveKind::retreat;
    retreat.target_x = 320;
    retreat.target_y = 320;
    AiMicroSetObjective(state.micro, AiMicroGroup::army, retreat);
    obs.simulation_frame = 5;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move &&
        orders[0].target_x == 320 && orders[0].target_y == 320,
        "retreat did not move the army to the nest");
    // Arrival flips the objective to defend(nest).
    obs.units[2].x = 330;
    obs.units[2].y = 330;
    obs.simulation_frame = 6;
    AiMicroExecutorStep(state.micro, obs);
    const AiMicroObjective& after = AiMicroObjectiveOf(state.micro,
        AiMicroGroup::army);
    require(after.kind == AiMicroObjectiveKind::defend &&
        after.target_x == 320 && after.target_y == 320,
        "retreat arrival did not switch the army to defend");

    // Low-health pull-back: a RANGED fighter under 30% hp in contact leaves
    // toward the nest instead of attacking.
    AiObservation hurt = micro_observation();
    hurt.units[0].command_state = kUnitStateWorkerApproachHarvest;
    hurt.units.push_back(ranged_fighter_unit(0x3300, 0, 400, 400, true));
    hurt.units.back().health = 20;
    hurt.units.push_back(fighter_unit(0x9600, 1, 430, 400, false));
    TyranoScriptedBotState fresh{};
    hurt.simulation_frame = 50;
    orders = AiMicroExecutorStep(fresh.micro, hurt);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move &&
        orders[0].unit_ids == std::vector<u32>{0x3300} &&
        orders[0].target_x == 320 && orders[0].target_y == 320,
        "low-health ranged fighter did not pull back to the nest");

    // A MELEE fighter in the same spot keeps fighting: breaking contact
    // inside a melee envelope only trades free hits.
    AiObservation hurt_melee = micro_observation();
    hurt_melee.units[0].command_state = kUnitStateWorkerApproachHarvest;
    hurt_melee.units.push_back(fighter_unit(0x3400, 0, 400, 400, true));
    hurt_melee.units.back().health = 20;
    hurt_melee.units.push_back(fighter_unit(0x9700, 1, 430, 400, false));
    TyranoScriptedBotState melee_state{};
    hurt_melee.simulation_frame = 50;
    orders = AiMicroExecutorStep(melee_state.micro, hurt_melee);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9700,
        "low-health melee fighter disengaged instead of fighting");

    // Pull-back cooldown: once the ranged fighter is out of contact it turns
    // normal, and it may not immediately re-enter the state on the next
    // contact - that oscillation is what pinned wounded units in place.
    hurt.units[3].x = 3000;  // hostile walks away -> contact broken
    hurt.simulation_frame = 51;
    AiMicroExecutorStep(fresh.micro, hurt);
    hurt.units[3].x = 430;   // hostile returns immediately
    hurt.simulation_frame = 52;
    orders = AiMicroExecutorStep(fresh.micro, hurt);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit,
        "pull-back cooldown did not stop the wounded fighter oscillating");
}

// Item 1 - the engine rejects an attack whose target render class is missing
// from the attacker's damage-profile mask, and a rejected order leaves the
// unit idle.  The executor must never pick such a target.
void test_ai_micro_executor_target_class_gate() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    // Ground-only weapon: classes 0..2 and 4, never class 3.
    obs.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    obs.units.back().attackable_class_mask = ~(1u << 3);
    // The only hostile in the defense bubble is a flyer.
    obs.units.push_back(flying_unit(0x9200, 1, 430, 400, false));
    TyranoScriptedBotState state{};
    obs.simulation_frame = 100;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "ground-only defender was ordered onto a flyer it cannot attack");
    require(state.micro.unattackable_targets_skipped != 0,
        "the unattackable target was not accounted for");

    // The same fighter with an anti-air weapon engages it.  (units[0] is the
    // worker, units[1] the nest, units[2] the fighter.)
    obs.units[2].attackable_class_mask = 0xffffffffu;
    TyranoScriptedBotState anti_air{};
    obs.simulation_frame = 101;
    std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(anti_air.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9200,
        "anti-air defender did not engage the flyer");

    // A policy attack objective naming an unattackable target must be
    // retargeted, not left parked on an order the engine rejects.
    AiObservation parked = micro_observation();
    parked.units[0].command_state = kUnitStateWorkerApproachHarvest;
    parked.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    parked.units.back().attackable_class_mask = ~(1u << 3);
    TyranoScriptedBotState parked_state{};
    parked.simulation_frame = 200;
    AiMicroExecutorStep(parked_state.micro, parked);  // register, no hostiles
    parked.units.push_back(flying_unit(0x9200, 1, 430, 400, false));
    parked.units.push_back(fighter_unit(0x9300, 1, 450, 400, false));
    AiMicroObjective attack;
    attack.kind = AiMicroObjectiveKind::attack;
    attack.target_unit_id = 0x9200;  // the flyer
    AiMicroSetObjective(parked_state.micro, AiMicroGroup::army, attack);
    parked.simulation_frame = 201;
    orders = AiMicroExecutorStep(parked_state.micro, parked);
    require(AiMicroObjectiveOf(parked_state.micro,
                AiMicroGroup::army).target_unit_id == 0x9300,
        "army stayed on a target no member can engage");
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9300,
        "army did not retarget to the hostile it can attack");
}

// Item 2 - the melee/ranged split must come from the raw definition range, so
// a range upgrade cannot reclassify a unit mid-match.
void test_ai_micro_executor_effective_range() {
    AiObservedUnit melee = fighter_unit(0x3100, 0, 400, 400, true);
    require(AiMicroRoleOf(melee) == AiMicroRole::melee,
        "audited melee unit was not classified as melee");
    // Range research raises the effective range well past the threshold; the
    // role must not move with it.
    melee.attack_range = 300;
    require(AiMicroRoleOf(melee) == AiMicroRole::melee,
        "a range upgrade reclassified a melee unit as ranged");

    // The upgraded range is what decides reach: a hostile out of BASE range
    // but inside the upgraded envelope is engaged.
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    AiObservedUnit shooter = ranged_fighter_unit(0x3100, 0, 400, 400, true);
    shooter.attack_range_base = 250;
    shooter.attack_range = 250;
    obs.units.push_back(shooter);
    // 350 px away: outside 250 + contact margin, inside 400 + contact margin.
    obs.units.push_back(fighter_unit(0x9200, 1, 750, 400, false));
    TyranoScriptedBotState base_state{};
    obs.simulation_frame = 300;
    // v10.5: the reflex now advances the defender AT the intruder (move
    // orders are fine); an ATTACK order beyond effective range is still
    // forbidden.
    for (const AiSemanticAction& order :
         AiMicroExecutorStep(base_state.micro, obs)) {
        require(order.kind != AiSemanticActionKind::attack_unit,
            "defender attacked a hostile outside its effective range");
    }
    obs.units[2].attack_range = 400;  // range research completed
    TyranoScriptedBotState upgraded{};
    obs.simulation_frame = 301;
    std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(upgraded.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9200,
        "upgraded range was not used to reach the hostile");

    // Against a flyer the engine reads a different stat.
    AiObservation air = micro_observation();
    air.units[0].command_state = kUnitStateWorkerApproachHarvest;
    AiObservedUnit anti_air = ranged_fighter_unit(0x3100, 0, 400, 400, true);
    anti_air.attack_range = 400;
    anti_air.attack_range_vs_air = 100;  // short anti-air reach
    air.units.push_back(anti_air);
    air.units.push_back(flying_unit(0x9200, 1, 700, 400, false));
    TyranoScriptedBotState air_state{};
    air.simulation_frame = 310;
    // v10.5: reflex advance produces move orders; only an ATTACK past the
    // anti-air reach would prove the wrong range stat was read.
    for (const AiSemanticAction& order :
         AiMicroExecutorStep(air_state.micro, air)) {
        require(order.kind != AiSemanticActionKind::attack_unit,
            "anti-air reach used the ground range stat");
    }
}

// Item 4 - a fighter far ahead of its group regroups instead of arriving
// alone; the laggards keep advancing so the gate cannot deadlock.
void test_ai_micro_executor_attack_cohesion() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3100, 0, 1400, 400, true));  // leader
    obs.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));   // laggard
    obs.units.push_back(fighter_unit(0x3300, 0, 410, 400, true));   // laggard
    TyranoScriptedBotState state{};
    // buildings_first with nothing in sight marches on the remembered enemy
    // building: tile (62, 12) -> world (2000, 400).
    obs.enemy_building_memory.assign(obs.tiles.size(), 0);
    obs.enemy_building_memory[12 * obs.map_width_tiles + 62] = 1;
    AiMicroObjective attack;
    attack.kind = AiMicroObjectiveKind::attack;
    attack.tactic = AiMicroAttackTactic::buildings_first;
    AiMicroSetObjective(state.micro, AiMicroGroup::army, attack);
    obs.simulation_frame = 400;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    require(AiMicroObjectiveOf(state.micro, AiMicroGroup::army).target_x == 2000 &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::army).target_y == 400,
        "buildings_first did not march on the remembered enemy building");
    bool leader_regrouped = false;
    bool laggards_advanced = false;
    for (const AiSemanticAction& order : orders) {
        for (u32 id : order.unit_ids) {
            if (id == 0x3100) {
                // v10: the leader waits IN PLACE (move to its own spot) -
                // walking back to the centroid was the forward-backward army
                // surge the user reported.
                leader_regrouped =
                    order.kind == AiSemanticActionKind::move &&
                    order.target_x == 1400 && order.target_y == 400;
            } else if (id == 0x3200 || id == 0x3300) {
                laggards_advanced =
                    order.kind == AiSemanticActionKind::attack_move &&
                    order.target_x == 2000;
            }
        }
    }
    require(leader_regrouped, "the leading fighter did not wait for its group");
    require(laggards_advanced,
        "the cohesion gate stopped the whole group instead of the leaders");
    require(state.micro.cohesion_holds != 0,
        "cohesion hold was not accounted for");

    // Contact disables the gate: a leader already fighting keeps fighting.
    obs.units.push_back(fighter_unit(0x9400, 1, 1420, 400, false));
    TyranoScriptedBotState engaged{};
    AiMicroSetObjective(engaged.micro, AiMicroGroup::army, attack);
    obs.simulation_frame = 401;
    orders = AiMicroExecutorStep(engaged.micro, obs);
    bool leader_attacked = false;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::attack_unit &&
            order.target_unit_id == 0x9400) {
            leader_attacked = true;
        }
    }
    require(leader_attacked, "cohesion gate pulled a fighter out of contact");
}

// 2026-08-31 (user replay review: fog-edge trembling): the cohesion gate
// needs its own hysteresis band.  One shared threshold flipped a boundary
// leader between advance and return every frame — and a changed order is
// issued immediately, so the unit vibrated in place.  Enter the return state
// only beyond cohesion_engage_radius (320), hold it until back inside
// cohesion_radius (256).
void test_ai_micro_executor_cohesion_hysteresis() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3100, 0, 1000, 400, true));  // leader
    obs.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));   // laggard
    obs.enemy_building_memory.assign(obs.tiles.size(), 0);
    obs.enemy_building_memory[12 * obs.map_width_tiles + 62] = 1;  // (2000,400)
    TyranoScriptedBotState state{};
    AiMicroObjective attack;
    attack.kind = AiMicroObjectiveKind::attack;
    attack.tactic = AiMicroAttackTactic::buildings_first;
    AiMicroSetObjective(state.micro, AiMicroGroup::army, attack);

    const auto leader_returning = [&state]() -> u32 {
        for (const AiMicroUnitRecord& record : state.micro.units) {
            if (record.unit_id == 0x3100) {
                return record.cohesion_returning;
            }
        }
        return 0xffu;
    };
    const auto leader_order = [](const std::vector<AiSemanticAction>& orders)
        -> const AiSemanticAction* {
        for (const AiSemanticAction& order : orders) {
            for (u32 id : order.unit_ids) {
                if (id == 0x3100) {
                    return &order;
                }
            }
        }
        return nullptr;
    };

    // Leader gap 300 = inside the 256..320 band, fresh state: advance.
    obs.simulation_frame = 500;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    const AiSemanticAction* order = leader_order(orders);
    require(order != nullptr &&
        order->kind == AiSemanticActionKind::attack_move &&
        order->target_x == 2000,
        "cohesion band: a fresh leader inside the band must keep advancing");
    require(leader_returning() == 0,
        "cohesion band: fresh in-band leader must not be returning");

    // Gap 330 > engage 320: the return state enters, the leader turns back.
    obs.units[2].x = 1060;
    obs.simulation_frame = 501;
    orders = AiMicroExecutorStep(state.micro, obs);
    order = leader_order(orders);
    require(order != nullptr && order->kind == AiSemanticActionKind::move &&
        order->target_x == 1060,
        "cohesion band: beyond the engage radius the leader must wait in place");
    require(leader_returning() == 1,
        "cohesion band: the return state did not enter past engage");

    // Back to gap 300 (inside the band) while returning: the state HOLDS —
    // this is exactly where the old single threshold flipped every frame.
    obs.units[2].x = 1000;
    obs.simulation_frame = 502;
    AiMicroExecutorStep(state.micro, obs);
    require(leader_returning() == 1,
        "cohesion band: the return state must hold inside the band");

    // Gap 250 < release 256: released, the leader advances again.
    obs.units[2].x = 900;
    obs.simulation_frame = 503;
    orders = AiMicroExecutorStep(state.micro, obs);
    order = leader_order(orders);
    require(order != nullptr &&
        order->kind == AiSemanticActionKind::attack_move &&
        order->target_x == 2000,
        "cohesion band: back inside the release radius the leader must advance");
    require(leader_returning() == 0,
        "cohesion band: the return state did not release");
}

// Item 5 - the defend leash needs hysteresis: sharing one threshold between
// "leave" and "re-engage" made a unit on the boundary flip every frame.
void test_ai_micro_executor_leash_hysteresis() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    // Nest is at (320, 320) and the default bubble is 800 px.
    obs.units.push_back(fighter_unit(0x3100, 0, 1140, 320, true));  // outside
    TyranoScriptedBotState state{};
    obs.simulation_frame = 500;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move &&
        orders[0].target_x == 320 && orders[0].target_y == 320,
        "defender outside the leash did not head back to the post");

    // Just back inside the leave threshold but not yet inside the rejoin
    // band: it must keep walking home, not flip back to holding.
    obs.units[2].x = 1100;
    obs.units[2].command_state = kUnitStateWorkerApproachHarvest;  // moving
    obs.simulation_frame = 505;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "returning defender flipped state on the bubble boundary");

    // Inside the rejoin band -> back to normal, and a hostile in the bubble
    // is engaged again.
    obs.units[2].x = 800;
    obs.units.push_back(fighter_unit(0x9200, 1, 830, 320, false));
    obs.simulation_frame = 510;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_unit &&
        orders[0].target_unit_id == 0x9200,
        "defender did not re-engage after returning inside the leash");
}

// Item 6 - an order the engine dropped in a NON-idle state never reaches the
// idle re-issue path, so the unit stalls forever without this recovery.
void test_ai_micro_executor_stuck_recovery() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    // Busy (not idle) for the whole test: the idle path must not be the one
    // that rescues this unit.
    obs.units.back().command_state = kUnitStateWorkerApproachHarvest;
    TyranoScriptedBotState state{};
    AiMicroObjective retreat;
    retreat.kind = AiMicroObjectiveKind::retreat;
    retreat.target_x = 2000;
    retreat.target_y = 400;
    AiMicroSetObjective(state.micro, AiMicroGroup::army, retreat);
    obs.simulation_frame = 600;
    require(AiMicroExecutorStep(state.micro, obs).size() == 1,
        "retreat order was not issued");
    // Frozen in place, still busy: nothing happens until the stuck window.
    obs.simulation_frame = 620;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "stuck recovery fired before the stuck window elapsed");
    obs.simulation_frame = 700;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move,
        "stuck unit was never re-issued its order");
    require(orders[0].target_x != 2000 || orders[0].target_y != 400,
        "stuck re-issue repeated the identical destination");
    require(state.micro.stuck_reissues == 1,
        "stuck re-issue was not accounted for");
    // Moving again clears the state: no further recovery orders.
    obs.units[2].x = 500;
    obs.simulation_frame = 701;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(state.micro.stuck_reissues == 1,
        "stuck recovery kept firing after the unit started moving");
}

// Item 7 - idle workers must spread across berry tiles instead of all being
// sent to the single nearest one.
void test_ai_micro_executor_harvest_spread() {
    AiObservation obs = micro_observation();
    // A second berry tile at (14, 9) -> world (464, 304).
    obs.tiles[9 * obs.map_width_tiles + 14].resource_amount = 500;
    // Four idle workers next to the nest.
    for (u32 i = 0; i < 3; ++i) {
        AiObservedUnit worker = obs.units[0];
        worker.id = 0x1100 + i;
        obs.units.push_back(worker);
    }
    TyranoScriptedBotState state{};
    obs.simulation_frame = 800;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    std::size_t first_tile = 0;
    std::size_t second_tile = 0;
    std::size_t assigned = 0;
    for (const AiSemanticAction& order : orders) {
        require(order.kind == AiSemanticActionKind::harvest,
            "an idle worker was given something other than a harvest order");
        assigned += order.unit_ids.size();
        if (order.target_x == 400) {
            first_tile += order.unit_ids.size();
        } else if (order.target_x == 464) {
            second_tile += order.unit_ids.size();
        }
    }
    require(assigned == 4, "not every idle worker was sent to harvest");
    require(first_tile == 3 && second_tile == 1,
        "workers did not spread across berry tiles at the saturation cap");
    // Harvest stays one action per worker (per-tile assignment).
    require(orders.size() == 4, "harvest orders were batched");
}

// Item 8 - once the policy's post is reached the scout keeps exploring
// instead of standing on it.
void test_ai_micro_executor_scout_picket() {
    // The scout is an early-warning picket: it stands between home and the
    // nearest KNOWN enemy building (forward of the midpoint, one screen short
    // of the building), holds the post, and never sweeps off it.
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    // Remembered enemy building on a fogged tile (40, 40) -> (1296, 1296);
    // home nest at (320, 320): distance 1380, 66% forward = 911 but capped at
    // 1380 - 800 = 580 -> post (730, 730).
    obs.enemy_building_memory.assign(obs.tiles.size(), 0);
    obs.enemy_building_memory[40 * obs.map_width_tiles + 40] = 1;
    TyranoScriptedBotState state{};
    AiMicroAssignGroup(state.micro, 0x3100, AiMicroGroup::scout);
    AiMicroObjective scout;
    scout.kind = AiMicroObjectiveKind::scout;
    AiMicroSetObjective(state.micro, AiMicroGroup::scout, scout);
    obs.simulation_frame = 900;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    const AiMicroObjective& post = AiMicroObjectiveOf(state.micro,
        AiMicroGroup::scout);
    require(post.target_x >= 728 && post.target_x <= 732 &&
        post.target_y >= 728 && post.target_y <= 732,
        "picket post was not placed between home and the remembered building");
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move &&
        orders[0].unit_ids == std::vector<u32>{0x3100} &&
        orders[0].target_x == post.target_x,
        "scout did not walk to its picket post");
    // On the post: hold it - no sweep, no order.
    obs.units[2].x = post.target_x;
    obs.units[2].y = post.target_y;
    obs.simulation_frame = 901;
    require(AiMicroExecutorStep(state.micro, obs).empty(),
        "scout wandered off its picket post");
    // A visible enemy building nearer than the memory moves the post:
    // distance 1000 -> forward min(660, 1000 - 800) = 200 -> (520, 320).
    obs.units.push_back(observed_unit(0x9400, 1, 0x84, 0, 1320, 320, false));
    obs.simulation_frame = 902;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(post.target_x == 520 && post.target_y == 320 &&
        orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move,
        "picket post did not follow the nearest known enemy building");
    // Without a known enemy building the translator refuses scout_map.
    AiObservation blind = micro_observation();
    blind.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));
    TyranoScriptedBotState bot{};
    bot.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    blind.simulation_frame = 1;
    require(DecideTyranoScriptedBotForHighLevelAction(bot, blind,
                AiRlHighLevelAction::scout_map, config).code !=
            TyranoScriptedBotDecisionCode::objective_updated &&
        EncodeAiObservationForRl(blind).legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::scout_map)] == 0,
        "scout_map was accepted with no enemy building known");
}


void test_ai_micro_executor_translator_objectives() {
    // Army actions set group objectives (objective_updated) instead of
    // emitting semantic actions; attack_enemy_base prefers the enemy BUILDING
    // over a closer mobile unit.
    AiObservation obs = micro_observation();
    obs.units.push_back(observed_unit(0x9400, 1, 0x84, 0, 1500, 1500, false));
    obs.units.push_back(fighter_unit(0x9500, 1, 500, 500, false));
    obs.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));
    TyranoScriptedBotState state{};
    state.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    obs.simulation_frame = 1;
    TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(state, obs,
            AiRlHighLevelAction::attack_enemy_base, config);
    const AiMicroObjective* army = &AiMicroObjectiveOf(state.micro,
        AiMicroGroup::army);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        army->kind == AiMicroObjectiveKind::attack &&
        army->tactic == AiMicroAttackTactic::buildings_first &&
        army->target_unit_id == 0,
        "attack_enemy_base did not set a buildings_first attack objective");
    // The executor derives the group target from the tactic: the building,
    // although the enemy fighter is closer.
    AiMicroExecutorStep(state.micro, obs);
    require(army->target_unit_id == 0x9400,
        "buildings_first did not pick the enemy building over a closer unit");
    obs.simulation_frame = 2;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::retreat, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        army->kind == AiMicroObjectiveKind::retreat &&
        army->target_x == 320 && army->target_y == 320,
        "retreat did not set a retreat objective to the nearest nest");
    obs.simulation_frame = 3;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::defend_base, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        army->kind == AiMicroObjectiveKind::defend && army->radius == 800,
        "defend_base did not set a one-screen defend objective");
    obs.simulation_frame = 4;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::hold_army, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        army->kind == AiMicroObjectiveKind::defend && army->radius < 800 &&
        army->target_x == 400 && army->target_y == 400,
        "hold_army did not set a small defend bubble at the army's spot");
    // scout_map splits the fighter into the scout group with a post.
    obs.simulation_frame = 5;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::scout_map, config);
    const AiMicroObjective& scout = AiMicroObjectiveOf(state.micro,
        AiMicroGroup::scout);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        scout.kind == AiMicroObjectiveKind::scout &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::scout).size() == 1 &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::scout)[0]->id ==
            0x3200,
        "scout_map did not split one unit into the scout group");
    // Scout executor: a hostile in sight -> step away; the post is far, so
    // without the hostile it would have walked there instead.
    obs.units[4].sight_range = 200;  // the scout (0x3200)
    obs.simulation_frame = 6;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    bool evaded = false;
    for (const AiSemanticAction& order : orders) {
        if (order.unit_ids == std::vector<u32>{0x3200} &&
            order.kind == AiSemanticActionKind::move && order.target_x < 400 &&
            order.target_y < 400) {
            evaded = true;  // away from the hostile at 500,500
        }
    }
    require(evaded, "scout did not step away from the sighted hostile");
    // v4 features: engaged fraction / local force ratio / objective one-hot /
    // pulling-back fraction, and the raid perimeter = the executor's 800 px
    // defend bubble.
    {
        AiObservation v4 = micro_observation();
        v4.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));  // hp 100
        AiRlStepEncoding calm = EncodeAiObservationForRl(v4);
        require(calm.features[80] == 0.0f && calm.features[81] == 0.5f &&
            calm.features[82] == 0.0f && calm.features[85] == 0.0f,
            "v4 features were not neutral with no hostile in sight");
        v4.units.push_back(fighter_unit(0x9700, 1, 460, 400, false));  // hp 100
        v4.units.back().health = 300;
        v4.army_objective_kind = 2;  // attack
        v4.army_pulling_back = 1;
        AiRlStepEncoding hot = EncodeAiObservationForRl(v4);
        require(hot.features[80] == 1.0f &&
            hot.features[81] > 0.24f && hot.features[81] < 0.26f &&
            hot.features[82] == 1.0f && hot.features[83] == 0.0f &&
            hot.features[85] == 1.0f && hot.features[76] > 0.0f,
            "v4 engagement/objective features did not encode the fight");
        // A hostile 700 px from the nest is inside the one-screen raid
        // perimeter (was 384 px).
        AiObservation raid = micro_observation();
        raid.units.push_back(fighter_unit(0x9800, 1, 320 + 700, 320, false));
        require(EncodeAiObservationForRl(raid).features[76] > 0.0f,
            "raid perimeter feature did not use the 800 px defend bubble");
        require(kAiRlActionCount == 80 && kAiRlFeatureCount == 802,
            "v10 action/feature counts");
    }
    // v5 features: spatial grid, directions, enemy composition, own state,
    // production pipeline, scout.  64x64-tile map (2048 px) -> 8x8 cells of
    // 256 px; the nest at (320,320) sits in cell (1,1) = index 9.
    {
        AiObservation v5 = micro_observation();
        v5.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));
        AiObservedUnit ranged_enemy = fighter_unit(0x9900, 1, 1500, 400, false);
        ranged_enemy.type_id = 0x34;  // Demon-range mobile type
        ranged_enemy.attack_range = 250;
        v5.units.push_back(ranged_enemy);
        AiObservedUnit enemy_nest = observed_unit(0x9a00, 1, 0x94, 0, 1700, 1700, false);  // Demon building
        enemy_nest.under_construction = true;
        v5.units.push_back(enemy_nest);
        // Remembered enemy building on an unlit tile at (40, 40) -> (1296,1296).
        v5.enemy_building_memory.assign(v5.tiles.size(), 0);
        v5.enemy_building_memory[40 * v5.map_width_tiles + 40] = 1;
        v5.scout_unit_id = 0x3200;
        v5.units[0].queued_production_type_id = 0;  // worker
        v5.units[1].queued_production_type_id = 0x20;  // nest producing a worker
        const AiRlStepEncoding e = EncodeAiObservationForRl(v5);
        require(e.features[86 + 9] > 0.19f && e.features[86 + 9] < 0.21f,
            "own-building grid cell was not encoded");
        require(e.features[150 + 9] > 0.0f, "own-army grid cell was not encoded");
        require(e.features[214 + (1 * 8 + 5)] > 0.0f,
            "enemy-mobile grid cell was not encoded");
        require(e.features[278 + (6 * 8 + 6)] > 0.0f &&
            e.features[278 + (5 * 8 + 5)] > 0.0f,
            "enemy-building grid (visible + remembered) was not encoded");
        require(e.features[406 + 9] == 1.0f, "explored fraction was not 1");
        require(e.features[470] > 0.14f && e.features[470] < 0.15f,
            "own start cell x was not encoded");
        // army -> nearest enemy: due east (dx=+1 -> 1.0, dy=0 -> 0.5), has=1
        require(e.features[472] > 0.99f && e.features[473] > 0.49f &&
            e.features[473] < 0.51f && e.features[475] == 1.0f,
            "army->enemy direction vector was wrong");
        require(e.features[479] == 1.0f, "enemy building vector has-flag");
        require(e.features[483] == 0.0f, "no start candidates -> no unexplored flag");
        // enemy composition: one ranged mobile, tribe 3 (Demon), one building uc
        require(e.features[489] > 0.0f && e.features[494] == 1.0f &&
            e.features[496] > 0.0f && e.features[497] > 0.49f,
            "enemy composition / tribe / stats were not encoded");
        // own army: one healthy fighter, idle, near home, melee, range 50
        require(e.features[500] == 1.0f && e.features[502] == 1.0f &&
            e.features[504] == 0.0f && e.features[505] > 0.0f &&
            e.features[508] > 0.09f && e.features[508] < 0.11f,
            "own-army state features were wrong");
        // production pipeline: one worker in production
        require(e.features[509] > 0.19f && e.features[509] < 0.21f &&
            e.features[510] == 0.0f, "production pipeline was not encoded");
        // scout: alive, east/south of home, sees no enemy (1100 px away)
        require(e.features[526] == 1.0f && e.features[527] > 0.5f &&
            e.features[529] == 0.0f && e.features[530] == 1.0f,
            "scout features were wrong");
    }
    // Role derivation from data: the RAW definition range decides, so a range
    // upgrade on `attack_range` cannot reclassify a unit mid-match.
    AiObservedUnit ranged = fighter_unit(0x7000, 0, 0, 0, true);
    ranged.attack_range_base = 200;
    ranged.attack_range = 200;
    AiObservedUnit unknown_weapon = fighter_unit(0x7002, 0, 0, 0, true);
    // 트윈 람포스: no range in the definition -> treated as ranged.
    unknown_weapon.attack_range_base = 0;
    unknown_weapon.attack_range = 0;
    require(AiMicroRoleOf(fighter_unit(0x7001, 0, 0, 0, true)) ==
            AiMicroRole::melee &&
        AiMicroRoleOf(ranged) == AiMicroRole::ranged &&
        AiMicroRoleOf(unknown_weapon) == AiMicroRole::ranged &&
        AiMicroRoleOf(obs.units[0]) == AiMicroRole::worker,
        "unit roles were not derived from the observed stats");
}

void test_ai_play_lobby_role_compatibility() {
    require(IsAiPlayLinkLobbyRole(kAiPlayLinkLobbyRoleValue) &&
        IsComputerLikeLinkLobbyRole(kOriginalComputerLinkLobbyRoleValue) &&
        IsComputerLikeLinkLobbyRole(kAiPlayLinkLobbyRoleValue),
        "Computer(AI) was not classified as a computer-like lobby role");
    require(SerializeAiPlayLinkLobbyRole(kAiPlayLinkLobbyRoleValue) ==
            kOriginalComputerLinkLobbyRoleValue &&
        SerializeAiPlayLinkLobbyRole(2) == 2,
        "Computer(AI) did not serialize as the compatible Computer role");
}

} // namespace

// v6 - the attack objective carries a TACTIC the executor re-applies every
// frame, the army can SEARCH the map for the enemy base, and the two are
// masked disjointly on "is an enemy building known".
void test_ai_micro_executor_tactics_and_search() {
    // ---- units_first / buildings_first pick by class, not distance --------
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    // Enemy building CLOSER than the enemy fighter (both beyond the melee
    // reach of 50 + 160 px, so the per-unit in-reach pick stays out of it).
    obs.units.push_back(observed_unit(0x9400, 1, 0x84, 0, 700, 400, false));
    obs.units.push_back(fighter_unit(0x9500, 1, 1200, 400, false));
    TyranoScriptedBotState state{};
    AiMicroObjective attack;
    attack.kind = AiMicroObjectiveKind::attack;
    attack.tactic = AiMicroAttackTactic::units_first;
    AiMicroSetObjective(state.micro, AiMicroGroup::army, attack);
    obs.simulation_frame = 1;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(state.micro, obs);
    const AiMicroObjective& army = AiMicroObjectiveOf(state.micro,
        AiMicroGroup::army);
    // Out of reach the army ADVANCES (attack_move) on where that unit is; it
    // does not chase it by name.
    require(army.target_unit_id == 0x9500 && army.target_x == 1200 &&
        orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::attack_move &&
        orders[0].target_x == 1200 && orders[0].target_y == 400,
        "units_first did not advance on the farther enemy unit over the building");
    // The unit dies: fall back to the building under the SAME tactic.
    obs.units[4].alive = false;
    obs.units[2].command_state = 0;
    obs.simulation_frame = 2;
    orders = AiMicroExecutorStep(state.micro, obs);
    require(army.kind == AiMicroObjectiveKind::attack &&
        army.tactic == AiMicroAttackTactic::units_first &&
        army.target_unit_id == 0x9400,
        "units_first did not fall back to the building once no unit was left");
    // A unit appears again: the tactic upgrades back to it.
    obs.units[4].alive = true;
    obs.units[2].command_state = kUnitStateAttackTarget;
    obs.simulation_frame = 3;
    AiMicroExecutorStep(state.micro, obs);
    require(army.target_unit_id == 0x9500,
        "units_first kept the fallback building after a unit reappeared");

    // buildings_first mirrors it: building while one is visible, then unit,
    // then the remembered building tile once nothing is in sight.
    TyranoScriptedBotState siege{};
    attack.tactic = AiMicroAttackTactic::buildings_first;
    AiMicroSetObjective(siege.micro, AiMicroGroup::army, attack);
    obs.simulation_frame = 10;
    AiMicroExecutorStep(siege.micro, obs);
    const AiMicroObjective& siege_army = AiMicroObjectiveOf(siege.micro,
        AiMicroGroup::army);
    require(siege_army.target_unit_id == 0x9400,
        "buildings_first did not pick the enemy building");
    obs.units[3].alive = false;  // building razed
    obs.simulation_frame = 11;
    AiMicroExecutorStep(siege.micro, obs);
    require(siege_army.target_unit_id == 0x9500,
        "buildings_first did not fall back to the enemy unit");
    obs.units[4].alive = false;  // nothing in sight
    obs.enemy_building_memory.assign(obs.tiles.size(), 0);
    obs.enemy_building_memory[40 * obs.map_width_tiles + 40] = 1;
    obs.simulation_frame = 12;
    orders = AiMicroExecutorStep(siege.micro, obs);
    require(siege_army.target_unit_id == 0 &&
        siege_army.target_x == 40 * 32 + 16 && siege_army.target_y == 40 * 32 + 16 &&
        orders.size() == 1 && orders[0].kind == AiSemanticActionKind::attack_move &&
        orders[0].target_x == 40 * 32 + 16,
        "buildings_first did not march on the remembered enemy building");

    // ---- search: sweep the unexplored start candidate, then the frontier --
    AiObservation fog = micro_observation();
    fog.units[0].command_state = kUnitStateWorkerApproachHarvest;
    fog.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    for (u32 tile_y = 0; tile_y < fog.map_height_tiles; ++tile_y) {
        for (u32 tile_x = 16; tile_x < fog.map_width_tiles; ++tile_x) {
            fog.tiles[tile_y * fog.map_width_tiles + tile_x].explored = false;
        }
    }
    fog.start_candidate_mask = (1u << 0) | (1u << 1);
    fog.start_candidate_x[0] = fog.start_x;
    fog.start_candidate_y[0] = fog.start_y;
    fog.start_candidate_x[1] = 30 * 32 + 16;
    fog.start_candidate_y[1] = 9 * 32 + 16;
    TyranoScriptedBotState searcher{};
    AiMicroObjective search;
    search.kind = AiMicroObjectiveKind::search;
    AiMicroSetObjective(searcher.micro, AiMicroGroup::army, search);
    fog.simulation_frame = 100;
    orders = AiMicroExecutorStep(searcher.micro, fog);
    const AiMicroObjective& sweep = AiMicroObjectiveOf(searcher.micro,
        AiMicroGroup::army);
    require(sweep.kind == AiMicroObjectiveKind::search &&
        sweep.target_x == 30 * 32 + 16 && sweep.target_y == 9 * 32 + 16 &&
        orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move &&
        orders[0].target_x == 30 * 32 + 16,
        "search did not sweep (plain move) toward the unexplored start candidate");
    require(searcher.micro.search_sweep_picks == 1,
        "search sweep pick was not accounted for");
    // The candidate's tile gets revealed (empty): no start candidate is left,
    // so the army STANDS (v7 split - the frontier is explore_frontier's job)
    // and the mask closes search_enemy_base.
    fog.tiles[9 * fog.map_width_tiles + 30].explored = true;
    fog.simulation_frame = 100 + 8;
    orders = AiMicroExecutorStep(searcher.micro, fog);
    require(sweep.target_x < 0 && searcher.micro.search_sweep_picks == 1,
        "search kept sweeping after every start candidate was checked");
    // A hostile met on the way is NOT engaged: search only moves (whether
    // to fight is the policy's next decision), so the sweep order stands.
    // (Reflex disabled here: this hostile stands inside the base bubble, and
    // with the v9 reflex on the army would rightly fight it - this assertion
    // pins the SEARCH semantics, not the reflex.)
    AiMicroExecutorConfig search_no_reflex{};
    search_no_reflex.reflex_enabled = false;
    fog.units.push_back(fighter_unit(0x9600, 1, 430, 400, false));
    fog.simulation_frame = 100 + 9;
    orders = AiMicroExecutorStep(searcher.micro, fog, search_no_reflex);
    bool attacked = false;
    for (const AiSemanticAction& order : orders) {
        attacked = attacked || order.kind == AiSemanticActionKind::attack_unit ||
            order.kind == AiSemanticActionKind::attack_move;
    }
    require(!attacked && sweep.kind == AiMicroObjectiveKind::search,
        "search engaged a hostile instead of just moving");

    // ---- translator + mask: search XOR attack_enemy_base ------------------
    AiObservation unknown = micro_observation();
    unknown.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));
    AiRlStepEncoding enc = EncodeAiObservationForRl(unknown);
    // (search_enemy_base is gated on unexplored start candidates since the
    // v7 split - see test_ai_search_split; here none are declared.)
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::attack_enemy_base)] == 0,
        "with no enemy location known, attack_enemy_base must not be legal");
    TyranoScriptedBotState bot{};
    bot.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    unknown.simulation_frame = 1;
    TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(bot, unknown,
            AiRlHighLevelAction::attack_enemy_base, config);
    require(decision.code != TyranoScriptedBotDecisionCode::objective_updated,
        "attack_enemy_base was accepted with no enemy building known");
    // v7 split: search needs an unexplored start candidate to march on.
    unknown.start_candidate_mask = (1u << 0) | (1u << 1);
    unknown.start_candidate_x[0] = unknown.start_x;
    unknown.start_candidate_y[0] = unknown.start_y;
    unknown.start_candidate_x[1] = 50 * 32 + 16;
    unknown.start_candidate_y[1] = 50 * 32 + 16;
    unknown.tiles[50 * unknown.map_width_tiles + 50].explored = false;
    unknown.simulation_frame = 2;
    decision = DecideTyranoScriptedBotForHighLevelAction(bot, unknown,
        AiRlHighLevelAction::search_enemy_base, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        AiMicroObjectiveOf(bot.micro, AiMicroGroup::army).kind ==
            AiMicroObjectiveKind::search,
        "search_enemy_base did not set the search objective");
    // A remembered (fog) building flips the mask, and attack_enemy_base
    // is accepted on memory alone.
    unknown.enemy_building_memory.assign(unknown.tiles.size(), 0);
    unknown.enemy_building_memory[40 * unknown.map_width_tiles + 40] = 1;
    enc = EncodeAiObservationForRl(unknown);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::attack_enemy_base)] == 1,
        "with a remembered enemy building, attack_enemy_base must be legal");
    unknown.simulation_frame = 3;
    decision = DecideTyranoScriptedBotForHighLevelAction(bot, unknown,
        AiRlHighLevelAction::attack_enemy_base, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        AiMicroObjectiveOf(bot.micro, AiMicroGroup::army).tactic ==
            AiMicroAttackTactic::buildings_first,
        "attack_enemy_base did not set buildings_first from memory alone");
    // v6 features: search one-hot and the attack tactic.
    unknown.army_objective_kind = 6;  // search + 1
    require(EncodeAiObservationForRl(unknown).features[531] == 1.0f,
        "search objective feature was not encoded");
    unknown.army_objective_kind = 2;  // attack + 1
    unknown.army_attack_tactic = 1;   // buildings_first
    enc = EncodeAiObservationForRl(unknown);
    require(enc.features[82] == 1.0f && enc.features[531] == 0.0f &&
        enc.features[532] == 1.0f && enc.features[533] == 0.0f,
        "attack tactic features were not encoded");
}

// 2026-08-31 (user replay review): visually-adjacent berry patches split by
// 1-2 empty tiles must count as ONE cluster, so the nest site minimises the
// amount-weighted distance over the whole group instead of hugging only one
// patch while ignoring the berries next door.
void test_ai_expansion_cluster_merge() {
    AiObservation obs = micro_observation();   // home berry at tile (12,9)
    const u32 w = obs.map_width_tiles;
    // Two 2-tile patches with a 2-empty-tile gap (Chebyshev distance 3
    // between the nearest members) in an open area.
    for (const u32 index : {40 * w + 40, 40 * w + 41,
                            40 * w + 44, 40 * w + 45}) {
        obs.tiles[index].resource_amount = 1000;
        obs.tiles[index].terrain_flags = 0x100;  // berry terrain
    }
    AiExpansionPlan merged = ComputeAiExpansionPlan(obs);
    require(merged.clusters.size() == 2,
        "gap<=merge patches did not form one cluster (with the home berry)");
    bool merged_found = false;
    for (const AiBerryCluster& cluster : merged.clusters) {
        if (cluster.tile_count == 4) {
            merged_found = true;
        }
    }
    require(merged_found, "merged cluster did not contain all four tiles");
    AiExpansionConfig strict{};
    strict.cluster_merge_gap_tiles = 1;
    AiExpansionPlan split = ComputeAiExpansionPlan(obs, strict);
    require(split.clusters.size() == 3,
        "merge gap 1 did not keep the patches separate");
}

// v7 - expansion: berry clusters are public map data, the next expansion
// site is the nearest undeveloped cluster's best tile, and the mask teaches
// the chain: scout_berry while that site is dark, expand_base_nest once lit.
// Resources a walking build will pay on arrival are reserved.
void test_ai_expansion_plan_and_chain() {
    AiObservation obs = micro_observation();   // nest (320,320), berry (12,9)
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3200, 0, 400, 400, true));
    // A second, far cluster of three tiles around (40, 40), still in the fog
    // (with its surroundings) - public position/amount, unexplored ground.
    const u32 w = obs.map_width_tiles;
    for (const u32 index : {40 * w + 40, 40 * w + 41, 41 * w + 40}) {
        obs.tiles[index].resource_amount = 1000;
        obs.tiles[index].terrain_flags = 0x100;  // berry terrain
    }
    for (u32 y = 34; y <= 47; ++y) {
        for (u32 x = 34; x <= 47; ++x) {
            obs.tiles[y * w + x].explored = false;
        }
    }
    AiExpansionPlan plan = ComputeAiExpansionPlan(obs);
    require(plan.clusters.size() == 2, "berry clusters were not found");
    const AiBerryCluster* home_cluster = nullptr;
    const AiBerryCluster* far_cluster = nullptr;
    for (const AiBerryCluster& cluster : plan.clusters) {
        (cluster.tile_count == 1 ? home_cluster : far_cluster) = &cluster;
    }
    require(home_cluster != nullptr && far_cluster != nullptr &&
        home_cluster->developed && !far_cluster->developed,
        "developed test did not separate the home cluster from the far one");
    // Site: inside the search box, outside the engine's +-4 tile berry
    // clearance of a 6x4 footprint (so at least 5 tiles from the cluster),
    // on a free tile.
    const i32 site_tx = far_cluster->site_x >> 5;
    const i32 site_ty = far_cluster->site_y >> 5;
    require(far_cluster->site_x >= 0 &&
        std::abs(site_tx - 40) <= 12 && std::abs(site_ty - 40) <= 12 &&
        (site_tx >= 46 || site_tx + 5 <= 35 || site_ty >= 46 || site_ty + 3 <= 35) &&
        obs.tiles[site_ty * w + site_tx].resource_amount == 0,
        "cluster site was not a clear tile near the far cluster");
    require(plan.has_target && plan.target_x == far_cluster->site_x &&
        !plan.target_explored,
        "next expansion target was not the dark far cluster");

    // Mask: scout_berry open, expand closed while the site is dark.
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::scout_berry)] == 1 &&
        enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::expand_base_nest)] == 0 &&
        enc.features[537] == 0.0f,
        "dark site did not open scout_berry / close expand_base_nest");

    // scout_berry: the fighter joins berry_scout and walks to the site.
    TyranoScriptedBotState bot{};
    bot.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    obs.simulation_frame = 1;
    TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(bot, obs,
            AiRlHighLevelAction::scout_berry, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        AiMicroGroupMembers(bot.micro, obs, AiMicroGroup::berry_scout).size() == 1 &&
        AiMicroObjectiveOf(bot.micro, AiMicroGroup::berry_scout).target_x ==
            plan.target_x,
        "scout_berry did not post a berry scout at the expansion site");
    obs.simulation_frame = 2;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(bot.micro, obs);
    require(orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move &&
        orders[0].unit_ids == std::vector<u32>{0x3200} &&
        orders[0].target_x == plan.target_x,
        "berry scout did not walk to the expansion site");

    // The site gets lit: the scout is released, expand opens, scout closes.
    for (u32 y = 34; y <= 47; ++y) {
        for (u32 x = 34; x <= 47; ++x) {
            obs.tiles[y * w + x].explored = true;
        }
    }
    obs.simulation_frame = 3;
    AiMicroExecutorStep(bot.micro, obs);
    require(AiMicroGroupMembers(bot.micro, obs, AiMicroGroup::berry_scout).empty(),
        "berry scout was not released once the site was explored");
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::scout_berry)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::expand_base_nest)] == 1 &&
        enc.features[537] == 1.0f,
        "lit site did not open expand_base_nest / close scout_berry");
    // A neutral monster standing on the footprint blocks the placement (the
    // engine gate refuses it): expand closes, feature 541 reports it, and the
    // translator refuses too.  Gone -> open again.
    obs.units.push_back(observed_unit(0x9700, kNeutralMonsterOwnerId, 0x49, 0,
        plan.target_x + 40, plan.target_y + 40, false));
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::expand_base_nest)] == 0 &&
        enc.features[541] == 1.0f &&
        DecideTyranoScriptedBotForHighLevelAction(bot, obs,
            AiRlHighLevelAction::expand_base_nest, config).code !=
            TyranoScriptedBotDecisionCode::action_ready,
        "a unit on the expansion footprint did not block expand_base_nest");
    obs.units.pop_back();
    obs.simulation_frame = 4;
    decision = DecideTyranoScriptedBotForHighLevelAction(bot, obs,
        AiRlHighLevelAction::expand_base_nest, config);
    require(decision && decision.action.kind == AiSemanticActionKind::build &&
        decision.action.production_id == 0x80 &&
        decision.action.unit_ids == std::vector<u32>{obs.units[0].id} &&
        decision.action.target_x == (plan.target_x & ~0x1f) &&
        decision.action.target_y == (plan.target_y & ~0x1f),
        "expand_base_nest did not build the nest at the expansion site");
    // Placement retries spiral around the SITE, not the main base.
    const UnitMovementPoint retry = TyranoScriptedBotNextBuildPoint(bot, obs);
    require(std::abs(retry.x - plan.target_x) <= 64 &&
        std::abs(retry.y - plan.target_y) <= 64,
        "placement retry did not stay around the expansion site");

    // Reservation: the worker walking to build the nest (state 0x25, type
    // 0x80) reserves 1000, counts as the cluster's nest (developed), and
    // blocks a second walking nest build.
    obs.units[0].command_state = 0x25;
    obs.units[0].command_value = 0x80;
    obs.units[0].path_target_x = plan.target_x;
    obs.units[0].path_target_y = plan.target_y;
    obs.primary_resources = 1500;
    plan = ComputeAiExpansionPlan(obs);
    require(plan.nest_walkers == 1 && !plan.has_target,
        "walking nest builder did not develop its cluster");
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::expand_base_nest)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::scout_berry)] == 0 &&
        enc.features[539] == 1.0f && enc.features[540] > 0.19f &&
        enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_land_nest)] == 0,
        "walking nest build did not reserve its cost in the mask/features");
}

// v7 - shared placement rule: every structure's mask is open only when a
// statically valid, explored, unblocked site for its footprint exists in the
// base area, and the translator builds on that same site.
void test_ai_shared_build_placement() {
    AiObservation obs = micro_observation();   // all tiles buildable/explored
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    const AiBuildSite pop = FindAiBuildSite(obs, 0x82u, obs.start_x, obs.start_y, 16);
    require(pop.found && std::abs((pop.x >> 5) - (obs.start_x >> 5)) <= 16 &&
        std::abs((pop.y >> 5) - (obs.start_y >> 5)) <= 16,
        "no base-area site was found for a population nest");
    // The site keeps the 3x2 footprint clear of the berry at tile (12, 9).
    const i32 sx = pop.x >> 5;
    const i32 sy = pop.y >> 5;
    require(!(12 >= sx && 12 < sx + 3 && 9 >= sy && 9 < sy + 2),
        "population nest site overlaps a berry tile");
    // ...and clear of the home nest's whole 6x4 footprint (anchored at tile
    // (10, 10)), not just its centre tile.
    require(sx + 3 <= 10 || sx >= 16 || sy + 2 <= 10 || sy >= 14,
        "population nest site overlaps the home nest footprint");
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_population_nest)] == 1,
        "population nest was not legal with a valid base-area site");
    TyranoScriptedBotState bot{};
    bot.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    obs.simulation_frame = 1;
    TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(bot, obs,
            AiRlHighLevelAction::build_population_nest, config);
    require(decision && decision.action.kind == AiSemanticActionKind::build &&
        decision.action.production_id == 0x82 &&
        decision.action.target_x == (pop.x & ~0x1f) &&
        decision.action.target_y == (pop.y & ~0x1f),
        "translator did not build the population nest on the shared site");

    // A unit standing on that site: the next unblocked site is used instead.
    obs.units.push_back(fighter_unit(0x9700, 1, pop.x + 16, pop.y + 16, false));
    const AiBuildSite next = FindAiBuildSite(obs, 0x82u, obs.start_x, obs.start_y, 16);
    require(next.found && (next.x != pop.x || next.y != pop.y),
        "blocked base-area site was not skipped");
    obs.units.pop_back();

    // No buildable ground in the base area: mask closes, translator refuses.
    for (AiObservedMapTile& tile : obs.tiles) {
        tile.buildable = false;
    }
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_population_nest)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_egg_nest)] == 0,
        "build masks stayed open with no buildable site");
    obs.simulation_frame = 2;
    require(!DecideTyranoScriptedBotForHighLevelAction(bot, obs,
            AiRlHighLevelAction::build_population_nest, config),
        "translator built with no buildable site");
}

// v7 - the search split by purpose: search_enemy_base sweeps only the
// unexplored start candidates (off when none is left), explore_frontier
// walks one unit's reachable frontier (air first, released when exhausted),
// roam_scout keeps one unit wandering outside the active vision.
void test_ai_search_split() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    const u32 w = obs.map_width_tiles;
    // Fog past column 16; one unexplored start candidate at tile (30, 9).
    for (u32 y = 0; y < obs.map_height_tiles; ++y) {
        for (u32 x = 16; x < w; ++x) {
            obs.tiles[y * w + x].explored = false;
        }
    }
    obs.start_candidate_mask = (1u << 0) | (1u << 1);
    obs.start_candidate_x[0] = obs.start_x;
    obs.start_candidate_y[0] = obs.start_y;
    obs.start_candidate_x[1] = 30 * 32 + 16;
    obs.start_candidate_y[1] = 9 * 32 + 16;

    // ---- masks: all three open; search closes once the start is explored.
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    const auto legal = [&](AiRlHighLevelAction a) {
        return enc.legal_mask[static_cast<std::size_t>(a)] == 1;
    };
    require(legal(AiRlHighLevelAction::search_enemy_base) &&
        legal(AiRlHighLevelAction::explore_frontier) &&
        legal(AiRlHighLevelAction::roam_scout),
        "split search actions were not legal with fog and an unexplored start");
    AiObservation checked = obs;
    checked.tiles[9 * w + 30].explored = true;
    enc = EncodeAiObservationForRl(checked);
    require(!legal(AiRlHighLevelAction::search_enemy_base) &&
        legal(AiRlHighLevelAction::explore_frontier),
        "search_enemy_base stayed legal with every start candidate explored");

    // ---- search: army marches on the start candidate, stands when none.
    TyranoScriptedBotState bot{};
    AiMicroObjective search;
    search.kind = AiMicroObjectiveKind::search;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, search);
    obs.simulation_frame = 100;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(bot.micro, obs);
    require(orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move &&
        orders[0].target_x == 30 * 32 + 16 && orders[0].target_y == 9 * 32 + 16,
        "search did not march on the unexplored start candidate");
    checked.simulation_frame = 100 + 8;
    TyranoScriptedBotState done{};
    AiMicroSetObjective(done.micro, AiMicroGroup::army, search);
    require(AiMicroExecutorStep(done.micro, checked).empty() &&
        AiMicroObjectiveOf(done.micro, AiMicroGroup::army).target_x < 0,
        "search kept walking (frontier / cycle) after every start was checked");

    // ---- explore: the fighter joins explorer, walks to the nearest
    // reachable frontier tile (column 16), and is released once nothing
    // reachable is left.
    // Fresh state: `bot` already stepped at frame 100, and frames must stay
    // monotonic (the stuck detector compares them).
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    TyranoScriptedBotState explorer_bot{};
    explorer_bot.rally_configured = true;
    obs.simulation_frame = 200;
    TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(explorer_bot, obs,
            AiRlHighLevelAction::explore_frontier, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        AiMicroGroupMembers(explorer_bot.micro, obs, AiMicroGroup::explorer).size() == 1,
        "explore_frontier did not split one unit into the explorer group");
    obs.simulation_frame = 201;
    orders = AiMicroExecutorStep(explorer_bot.micro, obs);
    // v9: the explorer visits the UNEXPLORED START CANDIDATE first (that is
    // where an enemy base can be), and only then sweeps the frontier.
    require(orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move &&
        orders[0].unit_ids == std::vector<u32>{0x3100} &&
        orders[0].target_x == 30 * 32 + 16 && orders[0].target_y == 9 * 32 + 16,
        "explorer did not visit the unexplored start candidate first");
    obs.tiles[9 * obs.map_width_tiles + 30].explored = true;
    obs.simulation_frame = 209;
    orders = AiMicroExecutorStep(explorer_bot.micro, obs);
    require(orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move &&
        (orders[0].target_x >> 5) == 16,
        "explorer did not walk to the nearest frontier column");
    // Wall off the fog with impassable ground: nothing reachable -> released.
    AiObservation walled = obs;
    for (u32 y = 0; y < walled.map_height_tiles; ++y) {
        walled.tiles[y * w + 15].passable = false;
    }
    walled.simulation_frame = 201 + 8;
    AiMicroExecutorStep(explorer_bot.micro, walled);
    walled.simulation_frame = 201 + 16;
    AiMicroExecutorStep(explorer_bot.micro, walled);
    require(AiMicroGroupMembers(explorer_bot.micro, walled, AiMicroGroup::explorer).empty(),
        "explorer was not released with no reachable frontier");

    // ---- explore prefers an AIR unit over a ground one.
    AiObservation air = obs;
    AiObservedUnit flyer = fighter_unit(0x3300, 0, 420, 420, true);
    flyer.render_class = 3;
    air.units.push_back(flyer);
    TyranoScriptedBotState air_bot{};
    air_bot.rally_configured = true;
    air.simulation_frame = 1;
    DecideTyranoScriptedBotForHighLevelAction(air_bot, air,
        AiRlHighLevelAction::explore_frontier, config);
    require(AiMicroGroupMembers(air_bot.micro, air, AiMicroGroup::explorer).size() == 1 &&
        AiMicroGroupMembers(air_bot.micro, air, AiMicroGroup::explorer)[0]->id == 0x3300,
        "explore_frontier did not prefer the air unit");

    // ---- roam: a target outside the active vision, re-picked on arrival.
    TyranoScriptedBotState roam_bot{};
    roam_bot.rally_configured = true;
    AiObservation patrol = micro_observation();
    patrol.units[0].command_state = kUnitStateWorkerApproachHarvest;
    patrol.units.push_back(fighter_unit(0x3100, 0, 400, 400, true));
    for (u32 y = 8; y < 16; ++y) {
        for (u32 x = 8; x < 16; ++x) {
            patrol.tiles[y * w + x].visible = true;  // active vision block
        }
    }
    patrol.simulation_frame = 1;
    decision = DecideTyranoScriptedBotForHighLevelAction(roam_bot, patrol,
        AiRlHighLevelAction::roam_scout, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated,
        "roam_scout did not post a roamer");
    patrol.simulation_frame = 2;
    orders = AiMicroExecutorStep(roam_bot.micro, patrol);
    require(orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move &&
        !patrol.tiles[(orders[0].target_y >> 5) * w + (orders[0].target_x >> 5)].visible,
        "roamer did not head for ground outside the active vision");
    const i32 first_x = orders[0].target_x;
    const i32 first_y = orders[0].target_y;
    patrol.units[2].x = first_x;   // arrive
    patrol.units[2].y = first_y;
    patrol.simulation_frame = 3;
    orders = AiMicroExecutorStep(roam_bot.micro, patrol);
    require(orders.size() == 1 &&
        (orders[0].target_x != first_x || orders[0].target_y != first_y),
        "roamer did not re-pick a target on arrival");
}

// v8 - the detachable raid group (docs/1순위.md): detach splits a mobility-
// first share off the main army, the raid runs its OWN objective through the
// same executor machinery, merge folds it back; and the spatial-target head:
// legal cells encode owner knowledge, a cell on attack becomes the strike
// zone (targets hunted around it, march to it when nothing is known there),
// a cell on defend becomes the post.
void test_ai_raid_group_and_target_cell() {
    AiObservation obs = micro_observation();
    for (u32 i = 0; i < 8; ++i) {  // 8 own fighters near (400, 400)
        obs.units.push_back(
            fighter_unit(0x4000 + i, 0, 380 + static_cast<i32>(i) * 10, 400,
                true));
    }
    // Enemy egg nest far away at (1500, 1500) - grid cell (5,5) = 45 on the
    // 64x64-tile (2048 px, 256 px/cell) test map.
    obs.units.push_back(observed_unit(0x9400, 1, 0x84, 0, 1500, 1500, false));
    // v10.2: defend actions are threat-gated - a hostile fighter near the
    // nest keeps them open for the mask assertions below.
    obs.units.push_back(fighter_unit(0x9401, 1, 900, 320, false));

    // Mask: detach needs the pump-filled army-group size and no raid; raid
    // actions stay closed while no raid exists.
    obs.army_group_unit_count = 8;
    obs.raid_unit_count = 0;
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::detach_raid)] == 1 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::merge_raid)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::raid_attack_base)] == 0,
        "raid mask did not gate on detach preconditions");
    obs.raid_unit_count = 3;
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::detach_raid)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::merge_raid)] == 1 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::raid_attack_base)] == 1 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::raid_defend_base)] == 1,
        "raid mask did not open with a live raid");
    obs.units.pop_back();  // drop the threat fighter before the translator part
    obs.raid_unit_count = 0;

    // Target-cell mask: every cell of the fully explored test map is legal;
    // darkening the (7,7) cell block (tiles 56..63 square, no memory there)
    // closes exactly that cell.
    require(enc.target_mask[45] == 1 && enc.target_mask[63] == 1,
        "explored cells were not legal target cells");
    AiObservation dark = obs;
    for (u32 ty = 56; ty < 64; ++ty) {
        for (u32 tx = 56; tx < 64; ++tx) {
            dark.tiles[ty * dark.map_width_tiles + tx].explored = false;
        }
    }
    require(EncodeAiObservationForRl(dark).target_mask[63] == 0,
        "an unexplored, memory-free cell stayed a legal target");

    // Translator: detach forms the raid (8 fighters * 30% -> floor 3), the
    // raid holds where it stands, and the main army group keeps the rest.
    TyranoScriptedBotState state{};
    state.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    obs.simulation_frame = 1;
    TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(state, obs,
            AiRlHighLevelAction::detach_raid, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::raid).size() == 3 &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::raid).kind ==
            AiMicroObjectiveKind::defend,
        "detach_raid did not split a holding 3-unit raid off the army");

    // raid_attack_base with target cell 9 (centre (384,384)): buildings_first
    // attack on the raid with the strike zone set; the enemy building is
    // OUTSIDE that zone, so the executor marches on the zone with no target.
    obs.simulation_frame = 2;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::raid_attack_base, config, 9);
    const AiMicroObjective* raid = &AiMicroObjectiveOf(state.micro,
        AiMicroGroup::raid);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        raid->kind == AiMicroObjectiveKind::attack &&
        raid->tactic == AiMicroAttackTactic::buildings_first &&
        raid->preferred_x == 384 && raid->preferred_y == 384,
        "raid_attack_base did not set the strike zone from the target cell");
    AiMicroExecutorStep(state.micro, obs);
    require(raid->target_unit_id == 0 && raid->target_x == 384 &&
        raid->target_y == 384,
        "strike zone with nothing inside did not march on the zone point");
    // Same attack aimed at the building's cell (45): the zone contains it,
    // so the executor locks onto it.
    obs.simulation_frame = 3;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::raid_attack_base, config, 45);
    AiMicroExecutorStep(state.micro, obs);
    require(raid->target_unit_id == 0x9400 && raid->target_x == 1500,
        "strike zone containing the enemy building did not target it");
    // The MAIN army objective was never touched by any of the raid actions.
    require(AiMicroObjectiveOf(state.micro, AiMicroGroup::army).kind !=
            AiMicroObjectiveKind::attack,
        "raid actions leaked into the main army objective");

    // defend_base with a target cell: the post is the cell centre, not the
    // nest; without a cell it falls back to the nest (v7 behavior).
    obs.simulation_frame = 4;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::defend_base, config, 18);  // cell (2,2) = (640,640)
    const AiMicroObjective* army = &AiMicroObjectiveOf(state.micro,
        AiMicroGroup::army);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        army->kind == AiMicroObjectiveKind::defend &&
        army->target_x == 640 && army->target_y == 640,
        "defend_base did not post at the target cell centre");
    obs.simulation_frame = 5;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::defend_base, config);
    require(army->target_x == 320 && army->target_y == 320,
        "cell-less defend_base did not fall back to the nearest nest");

    // merge_raid folds every raid member back into the main army.
    obs.simulation_frame = 6;
    decision = DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::merge_raid, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::raid).empty() &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::army).size() == 8,
        "merge_raid did not fold the raid back into the army");
}

// v8 - observation/encoder expansion (docs/2순위.md): enemy composition by
// tribe-relative type slot, OP-DP firepower sums and the power force ratio,
// enemy-army fog-memory channel + last-sighting scalars, terrain channels,
// and the raid-state features.
void test_ai_v8_observation_features() {
    AiObservation obs = micro_observation();  // 64x64 tiles, fully explored
    const auto near_value = [](float actual, float expected) {
        return actual > expected - 0.001f && actual < expected + 0.001f;
    };
    // Own melee (마소스, base range 50, OP 120) + own ranged (base range 250,
    // OP 300, anti-air capable via mask bit 3 + vs-air range).
    AiObservedUnit melee = fighter_unit(0x4100, 0, 400, 400, true);
    melee.attack_power = 120;
    obs.units.push_back(melee);
    AiObservedUnit ranged = ranged_fighter_unit(0x4200, 0, 420, 400, true);
    ranged.attack_power = 300;
    obs.units.push_back(ranged);
    // Two visible enemy masos-slot mobiles (0x21 -> slot 1), OP 100 each, no
    // anti-air (mask misses bit 3).
    for (u32 k = 0; k < 2; ++k) {
        AiObservedUnit foe = fighter_unit(0x9600 + k, 1, 900, 900, false);
        foe.attack_power = 100;
        foe.attackable_class_mask = 0x7;
        foe.attack_range_vs_air = 0;
        obs.units.push_back(foe);
    }
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(near_value(enc.features[545 + 1], 2.0f / 20.0f) &&
        enc.features[545] == 0.0f,
        "enemy type-slot counts did not encode the masos slot");
    require(near_value(enc.features[561], 120.0f / 5000.0f) &&
        near_value(enc.features[562], 300.0f / 5000.0f) &&
        enc.features[563] == 1.0f &&
        near_value(enc.features[564], 200.0f / 5000.0f) &&
        enc.features[565] == 0.0f && enc.features[566] == 0.0f,
        "OP-DP firepower sums / anti-air flags were wrong");
    require(near_value(enc.features[567], 420.0f / 620.0f),
        "power-based force ratio was wrong");
    // Enemy-army memory: 5 remembered hostiles at tile (40,40) -> cell (5,5)
    // = 45; last sighting 100 frames ago at (1300,1300), size 7.
    obs.enemy_army_memory.assign(obs.tiles.size(), 0u);
    obs.enemy_army_memory[40 * obs.map_width_tiles + 40] = 5u;
    obs.enemy_army_seen_frames_ago = 100;
    obs.enemy_army_seen_x = 1300;
    obs.enemy_army_seen_y = 1300;
    obs.enemy_army_seen_count = 7;
    enc = EncodeAiObservationForRl(obs);
    require(near_value(enc.features[568 + 45], 5.0f / 20.0f) &&
        enc.features[568] == 0.0f,
        "enemy-army memory channel did not fold into the right cell");
    require(near_value(enc.features[632], 100.0f / 60000.0f) &&
        enc.features[633] > 0.8f && enc.features[634] > 0.8f &&
        near_value(enc.features[635], 7.0f / 50.0f),
        "enemy-army sighting scalars were wrong");
    // Never seen -> frames-ago saturates to 1, direction neutral.
    AiObservation never = micro_observation();
    enc = EncodeAiObservationForRl(never);
    require(enc.features[632] == 1.0f && enc.features[633] == 0.5f,
        "never-seen enemy army did not encode as 1.0 / neutral");
    // Terrain channels: the fixture map is fully passable/buildable; blocking
    // the (0,0) cell's tiles zeroes exactly that cell of channel 8.
    require(enc.features[636] == 1.0f && enc.features[700] == 1.0f,
        "terrain channels did not report the open map");
    for (u32 ty = 0; ty < 8; ++ty) {
        for (u32 tx = 0; tx < 8; ++tx) {
            never.tiles[ty * never.map_width_tiles + tx].passable = false;
        }
    }
    enc = EncodeAiObservationForRl(never);
    require(enc.features[636] == 0.0f && enc.features[637] == 1.0f,
        "passable channel did not localize the blocked cell");
    // Raid-state features (pump-filled group summary).
    never.raid_unit_count = 3;
    never.raid_objective_kind = 2;   // attack
    never.raid_attack_tactic = 1;    // buildings_first
    never.army_group_unit_count = 10;
    enc = EncodeAiObservationForRl(never);
    require(near_value(enc.features[764], 3.0f / 14.0f) &&
        enc.features[765] == 1.0f && enc.features[766] == 1.0f &&
        enc.features[767] == 0.0f && enc.features[769] == 1.0f &&
        near_value(enc.features[770], 10.0f / 50.0f),
        "raid-state features were wrong");
}

// v9 - the event-based decision gate:
// quiet stretches decide only at max_interval, decision-relevant events wake
// the policy at the next check, the snapshot refreshes only on due, and the
// decision-context features patch into [772..787].
void test_ai_decision_gate() {
    AiObservation obs = micro_observation();
    obs.primary_resources = 0;  // every produce/build action illegal
    obs.population_used = 9;    // base-nest supply (pop_free 9)
    AiDecisionGateState gate{};
    const AiDecisionGateConfig config{};
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::produce_worker)] == 0,
        "fixture unexpectedly affords produce_worker with 0 resources");

    // First decision fires unconditionally.
    AiDecisionGateResult r =
        AiDecisionGateEvaluate(gate, obs, enc, {}, false, 100, config);
    require(r.due && (r.triggers & trigger_first) != 0,
        "first decision did not fire");
    // Quiet game: checks at +8..+56 stay silent, +64 fires max_interval.
    for (u32 f = 108; f <= 156; f += 8) {
        r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, f, config);
        require(!r.due, "quiet gate fired before max_interval");
    }
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 164, config);
    require(r.due && (r.triggers & trigger_max_interval) != 0 &&
        r.frames_since_last == 64,
        "max_interval did not fire at 64 frames");

    // Production opening: resources arrive -> produce_worker flips legal ->
    // the next check fires trigger_production_open.
    obs.primary_resources = 1000;
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::produce_worker)] == 1,
        "produce_worker did not open with resources");
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 172, config);
    require(r.due && (r.triggers & trigger_production_open) != 0,
        "production opening did not wake the policy");

    // Enemy sighted (far from the base: no base-threat bit), then lost.
    obs.units.push_back(fighter_unit(0x9a00, 1, 1900, 1900, false));
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 180, config);
    require(r.due && (r.triggers & trigger_enemy_sighted) != 0 &&
        (r.triggers & trigger_base_threat) == 0,
        "enemy sighting did not wake the policy (or mis-flagged base threat)");
    obs.units.pop_back();
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 188, config);
    require(r.due && (r.triggers & trigger_enemy_lost) != 0,
        "enemy loss did not wake the policy");

    // Base threat: a hostile fighter 280 px from the nest (edge-triggered:
    // the second check with the same standing threat stays silent until
    // max_interval).
    obs.units.push_back(fighter_unit(0x9b00, 1, 600, 320, false));
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 196, config);
    require(r.due && (r.triggers & trigger_base_threat) != 0,
        "base threat did not wake the policy");
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 204, config);
    require(!r.due, "standing base threat re-fired every check");
    obs.units.pop_back();

    // Snapshot updates ONLY on due: with the gate quiet, mask changes since
    // the last DECISION still compare against that decision's snapshot.
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 212, config);  // enemy_lost fires (due)
    obs.primary_resources = 0;
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 220, config);
    require(!r.due, "losing affordability alone woke the policy");
    obs.primary_resources = 1000;
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {}, false, 228, config);
    require(!r.due,
        "re-opening an action that was legal at the last decision re-fired");

    // Own-loss and owner-packet (imitation) triggers.
    r = AiDecisionGateEvaluate(gate, obs, enc, {100, 0, 0, 0}, false, 236,
        config);
    require(r.due && (r.triggers & trigger_own_loss) != 0,
        "own loss did not wake the policy");
    r = AiDecisionGateEvaluate(gate, obs, enc, {100, 0, 0, 0}, true, 244,
        config);
    require(r.due && (r.triggers & trigger_owner_packet) != 0,
        "owner packets did not force an imitation sample");

    // Objective completion via the pump-mirrored fields: a raid that existed
    // at the last decision died out.
    obs.raid_unit_count = 3;
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {100, 0, 0, 0}, false, 252,
        config);  // raid_attack legal-flip fires production? no - raid not production; completion? no. may be quiet
    AiDecisionGateSnapshotObjectives(gate, 0, false, 3, 0, 0, 0, 0);
    obs.raid_unit_count = 0;
    enc = EncodeAiObservationForRl(obs);
    r = AiDecisionGateEvaluate(gate, obs, enc, {100, 0, 0, 0}, false, 260,
        config);
    require(r.due && (r.triggers & trigger_objective_done) != 0,
        "raid wipe did not wake the policy");

    // Decision-context feature patch [772..787].
    ApplyAiRlDecisionContext(enc, 32,
        trigger_max_interval | trigger_base_threat, {2, 1, 8});
    require(enc.features[772] == 0.5f && enc.features[773] == 1.0f &&
        enc.features[779] == 1.0f && enc.features[774] == 0.0f &&
        enc.features[785] == 0.25f && enc.features[786] == 0.125f &&
        enc.features[787] == 1.0f,
        "decision-context features mis-patched");
}

// v9 - the base-defense reflex: an intruder near an own building makes the
// army fight under a temporary overlay WITHOUT touching the policy's
// objectives; the overlay clears after threat_clear_frames; a policy retreat
// is respected; reflex_enabled=false disables everything.
void test_ai_defense_reflex() {
    AiObservation obs = micro_observation();
    obs.units.push_back(fighter_unit(0x4100, 0, 500, 400, true));  // own
    TyranoScriptedBotState bot{};
    // Policy objective: a small hold bubble at the fighter's own spot - the
    // intruder at the nest is OUTSIDE it, so only the reflex can engage.
    AiMicroObjective hold{};
    hold.kind = AiMicroObjectiveKind::defend;
    hold.target_x = 500;
    hold.target_y = 400;
    hold.radius = 128;
    hold.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, hold);
    obs.units.push_back(fighter_unit(0x9c00, 1, 360, 330, false));  // intruder
    obs.simulation_frame = 10;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(bot.micro, obs);
    bool attacked = false;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::attack_unit &&
            order.target_unit_id == 0x9c00) {
            attacked = true;
        }
    }
    require(attacked && bot.micro.threat.active &&
        bot.micro.reflex_activations == 1,
        "reflex did not engage the intruder at the nest");
    require(AiMicroObjectiveOf(bot.micro, AiMicroGroup::army).radius == 128 &&
        AiMicroObjectiveOf(bot.micro, AiMicroGroup::army).target_x == 500,
        "reflex overwrote the policy objective");
    // Threat gone: the overlay stays for threat_clear_frames then clears.
    obs.units.pop_back();
    obs.simulation_frame = 60;
    AiMicroExecutorStep(bot.micro, obs);
    require(bot.micro.threat.active, "overlay dropped before clear window");
    obs.simulation_frame = 10 + 120 + 8;
    AiMicroExecutorStep(bot.micro, obs);
    require(!bot.micro.threat.active, "overlay did not clear");
    // Policy retreat is respected: the fighter walks home, intruder or not.
    AiMicroObjective retreat{};
    retreat.kind = AiMicroObjectiveKind::retreat;
    retreat.target_x = 320;
    retreat.target_y = 320;
    retreat.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, retreat);
    obs.units.push_back(fighter_unit(0x9c01, 1, 360, 330, false));
    obs.simulation_frame = 200;
    orders = AiMicroExecutorStep(bot.micro, obs);
    bool attacked_in_retreat = false;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::attack_unit) {
            attacked_in_retreat = true;
        }
    }
    require(!attacked_in_retreat && bot.micro.threat.active,
        "reflex overrode a policy retreat");
    // reflex_enabled = false: no overlay at all.
    TyranoScriptedBotState off{};
    AiMicroExecutorConfig no_reflex{};
    no_reflex.reflex_enabled = false;
    AiMicroSetObjective(off.micro, AiMicroGroup::army, hold);
    AiMicroExecutorStep(off.micro, obs, no_reflex);
    require(!off.micro.threat.active && off.micro.reflex_activations == 0,
        "reflex fired while disabled");
}

// v9 - the macro autopilot rules: worker floor, pop guard, idle-producer
// guard; producer-conflict
// skips; policy fighter picks steer the idle guard.
void test_ai_macro_autopilot() {
    AiObservation obs = micro_observation();
    obs.population_used = 20;       // supply
    obs.population_reserved = 5;    // demand (healthy headroom)
    obs.primary_resources = 900;    // below the v10 tech-guard bank threshold
                                    // so the older guards are tested alone
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::produce_worker)] == 1,
        "fixture cannot produce a worker");
    AiAutopilotState state{};
    // 1 worker < floor 10 -> produce_worker; skipped when the policy already
    // used the base producer this frame.
    std::vector<AiRlHighLevelAction> plan = AiAutopilotPlan(state, obs, enc,
        AiRlHighLevelAction::no_op, 100);
    require(plan.size() == 1 && plan[0] == AiRlHighLevelAction::produce_worker,
        "worker floor did not fire");
    plan = AiAutopilotPlan(state, obs, enc,
        AiRlHighLevelAction::produce_worker, 108);
    require(plan.empty(), "worker floor collided with the policy's producer");
    // Pop guard: demand at the supply edge fires build_population_nest; a pop
    // nest already under construction suppresses it.
    AiObservation crowded = obs;
    crowded.population_reserved = 19;   // 19 + margin 2 >= supply 20
    for (u32 i = 0; i < 12; ++i) {      // satisfy the worker floor
        crowded.units.push_back(observed_unit(0x5000 + i, 0, 0x20,
            (1u << 7), 300, 300, true));
    }
    enc = EncodeAiObservationForRl(crowded);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_population_nest)] == 1,
        "fixture cannot build a population nest");
    plan = AiAutopilotPlan(state, crowded, enc, AiRlHighLevelAction::no_op,
        116);
    require(plan.size() == 1 &&
        plan[0] == AiRlHighLevelAction::build_population_nest,
        "pop guard did not fire");
    AiObservation building = crowded;
    AiObservedUnit pop_uc = observed_unit(0x6000, 0, 0x82u, 0, 500, 500, true);
    pop_uc.under_construction = true;
    building.units.push_back(pop_uc);
    plan = AiAutopilotPlan(state, building, EncodeAiObservationForRl(building),
        AiRlHighLevelAction::no_op, 124);
    require(plan.empty(), "pop guard fired with a pop nest in flight");
    // Idle-producer guard: a completed idle egg nest for >= 96 frames with a
    // bank_threshold+ bank produces the policy's last fighter type (masos
    // default).
    AiObservation eggs = crowded;
    eggs.population_reserved = 5;
    eggs.primary_resources = 10000;  // idle guard needs its 1500 bank back
    eggs.units.push_back(observed_unit(0x7000, 0, 0x84u, 0, 700, 700, true));
    enc = EncodeAiObservationForRl(eggs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::produce_masos)] == 1,
        "fixture cannot produce masos");
    AiAutopilotState idle_state{};
    plan = AiAutopilotPlan(idle_state, eggs, enc,
        AiRlHighLevelAction::produce_unit_x22, 200);  // starts the idle clock
    require(idle_state.last_fighter_action ==
            AiRlHighLevelAction::produce_unit_x22,
        "policy fighter pick did not steer the idle guard");
    bool fired_fighter = false;
    plan = AiAutopilotPlan(idle_state, eggs, enc, AiRlHighLevelAction::no_op,
        200 + 96);
    for (const AiRlHighLevelAction action : plan) {
        if (action == AiRlHighLevelAction::produce_unit_x22) {
            fired_fighter = true;
        }
    }
    require(fired_fighter, "idle-producer guard did not fire");
    // Low bank suppresses the guard.
    AiObservation broke = eggs;
    broke.primary_resources = 500;
    AiAutopilotState broke_state{};
    AiAutopilotPlan(broke_state, broke, EncodeAiObservationForRl(broke),
        AiRlHighLevelAction::no_op, 300);
    plan = AiAutopilotPlan(broke_state, broke, EncodeAiObservationForRl(broke),
        AiRlHighLevelAction::no_op, 300 + 96);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::produce_masos &&
            action != broke_state.last_fighter_action,
            "idle-producer guard ignored the bank threshold");
    }
}

// v9 follow-ups (user replay reports): a building must not BRIDGE two
// obstacles (sealing a berry/movement corridor), and workers/scouts must not
// flee from non-combat hostiles (an enemy worker scouting past).
void test_ai_corridor_guard_and_noncombat_flee() {
    // Corridor: two impassable walls with an exactly-footprint-wide gap; a
    // population nest (3x2) in the gap would bridge them -> refused.  The
    // same site with one wall removed is fine (walkable around).
    AiObservation obs = micro_observation();
    const u32 w = obs.map_width_tiles;
    for (u32 ty = 10; ty <= 20; ++ty) {
        obs.tiles[ty * w + 20].passable = false;
        obs.tiles[ty * w + 24].passable = false;
    }
    require(!AiBuildSiteCandidateOk(obs, 0x82u, 21, 14, true, nullptr),
        "corridor-bridging placement was accepted");
    for (u32 ty = 10; ty <= 20; ++ty) {
        obs.tiles[ty * w + 24].passable = true;
    }
    require(AiBuildSiteCandidateOk(obs, 0x82u, 21, 14, true, nullptr),
        "placement beside a single wall was refused");
    require(AiBuildSiteCandidateOk(obs, 0x82u, 40, 40, true, nullptr),
        "open-field placement was refused");

    // Non-combat flee: an enemy WORKER next to our harvesting worker and our
    // scout picket moves neither; a masos does scatter the worker.
    AiObservation calm = micro_observation();
    calm.units.push_back(fighter_unit(0x4200, 0, 500, 400, true));  // picket
    TyranoScriptedBotState bot{};
    AiMicroAssignGroup(bot.micro, 0x4200, AiMicroGroup::scout);
    AiMicroObjective post{};
    post.kind = AiMicroObjectiveKind::scout;
    post.target_x = 500;
    post.target_y = 400;
    post.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::scout, post);
    AiMicroExecutorConfig no_reflex{};
    no_reflex.reflex_enabled = false;  // isolate the flee/evade rules
    // Enemy WORKER (harvest-capable, non-combat role) near both.
    AiObservedUnit enemy_worker = observed_unit(0x9d00, 1, 0x20u,
        (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7), 400, 380, false);
    enemy_worker.health = 100;
    enemy_worker.max_health = 100;
    enemy_worker.attack_range = 50;
    enemy_worker.attack_range_base = 50;
    calm.units.push_back(enemy_worker);
    calm.simulation_frame = 10;
    AiMicroExecutorStep(bot.micro, calm, no_reflex);
    bool any_flee = false;
    for (const AiMicroUnitRecord& record : bot.micro.units) {
        if (record.state == AiMicroUnitState::fleeing ||
            record.state == AiMicroUnitState::evading) {
            any_flee = true;
        }
    }
    require(!any_flee, "a scouting enemy worker scattered worker/picket");
    // A real fighter does scatter the harvest worker.
    calm.units.pop_back();
    calm.units.push_back(fighter_unit(0x9d01, 1, 320, 300, false));
    calm.simulation_frame = 18;
    AiMicroExecutorStep(bot.micro, calm, no_reflex);
    bool worker_fled = false;
    for (std::size_t index = 0; index < bot.micro.units.size(); ++index) {
        if (bot.micro.units[index].state == AiMicroUnitState::fleeing) {
            worker_fled = true;
        }
    }
    require(worker_fled, "a combat hostile no longer scatters workers");

    // Meat pickup (user directive): a hunter with nothing left to fight
    // walks onto the nearest dropped meat; one collector per drop; the
    // objective/tactic stays the policy's.
    AiObservation hunt = micro_observation();
    hunt.units.push_back(fighter_unit(0x4300, 0, 500, 400, true));
    hunt.units.push_back(fighter_unit(0x4301, 0, 520, 400, true));
    // Pickup capability bit (kUnitEquipmentPickupEnabledFlag) - definition
    // type_flags in the real game.
    hunt.units[hunt.units.size() - 2].type_flags |= 0x2u;
    hunt.units[hunt.units.size() - 1].type_flags |= 0x2u;
    AiObservedMapEffect meat{};
    meat.id = 7;
    meat.effect_id = 2;
    meat.x = 600;
    meat.y = 430;
    meat.amount = 150;
    hunt.map_effects.push_back(meat);
    TyranoScriptedBotState hunter{};
    AiMicroObjective hunt_objective{};
    hunt_objective.kind = AiMicroObjectiveKind::attack;
    hunt_objective.tactic = AiMicroAttackTactic::neutral_only;
    hunt_objective.assigned = true;
    AiMicroSetObjective(hunter.micro, AiMicroGroup::army, hunt_objective);
    hunt.simulation_frame = 30;
    std::vector<AiSemanticAction> hunt_orders =
        AiMicroExecutorStep(hunter.micro, hunt, no_reflex);
    u32 meat_moves = 0;
    for (const AiSemanticAction& order : hunt_orders) {
        if (order.kind == AiSemanticActionKind::pickup_move &&
            order.target_x == 600 && order.target_y == 430) {
            meat_moves += static_cast<u32>(order.unit_ids.size());
        }
    }
    require(meat_moves == 1 && hunter.micro.meat_pickup_orders == 1,
        "meat drop was not collected by exactly one hunter");
    require(AiMicroObjectiveOf(hunter.micro, AiMicroGroup::army).tactic ==
            AiMicroAttackTactic::neutral_only,
        "meat pickup disturbed the hunt objective");
}

// v9 - replay-review follow-ups #2: local path connectivity of chosen build
// sites (U-yard closure refused), expansion sites must REACH their berries
// (hill/cliff guard), the explorer visits unexplored start candidates before
// the frontier, and the autopilot scout guard fires exactly while the enemy
// base is unknown.
void test_ai_local_paths_and_scout_guard() {
    // --- U-yard closure: impassable walls on three sides of a pocket; a pop
    // nest (3x2) closing the fourth side splits inside from outside.
    AiObservation obs = micro_observation();
    const u32 w = obs.map_width_tiles;
    for (u32 tx = 28; tx <= 32; ++tx) {          // top wall y=10
        obs.tiles[10 * w + tx].passable = false;
    }
    for (u32 ty = 10; ty <= 17; ++ty) {          // side walls x=28 / x=32
        obs.tiles[ty * w + 28].passable = false;
        obs.tiles[ty * w + 32].passable = false;
    }
    // Pocket interior = x29..31 (exactly a pop nest wide), opening at y17.
    const std::vector<u8> occupancy = AiBuildOccupancyGrid(obs);
    require(!AiBuildSiteKeepsLocalPaths(obs, occupancy, 0x82u, 29, 17, false),
        "U-yard closing placement passed the connectivity check");
    require(AiBuildSiteKeepsLocalPaths(obs, occupancy, 0x82u, 44, 30, false),
        "open-field placement failed the connectivity check");

    // --- berry reach: a wall between the site and the berries fails the
    // expansion verification; the same site with a gap passes.
    AiObservation berry_obs = micro_observation();
    for (const u32 index : {20u * w + 46u, 20u * w + 47u, 21u * w + 46u}) {
        berry_obs.tiles[index].resource_amount = 800;
        berry_obs.tiles[index].terrain_flags = 0x100;
        berry_obs.tiles[index].passable = false;  // berry terrain is unwalkable
    }
    for (u32 ty = 8; ty <= 30; ++ty) {            // cliff at x=40 (spans the
        berry_obs.tiles[ty * w + 40].passable = false;  // whole BFS window)
    }
    const std::vector<u8> berry_occupancy = AiBuildOccupancyGrid(berry_obs);
    require(!AiBuildSiteKeepsLocalPaths(berry_obs, berry_occupancy, 0x80u,
                32, 18, true),
        "a nest site across a cliff from its berries was accepted");
    berry_obs.tiles[21 * w + 40].passable = true;  // a ramp through the cliff
    require(AiBuildSiteKeepsLocalPaths(berry_obs, berry_occupancy, 0x80u,
                32, 18, true),
        "a nest site with a ramp to its berries was refused");

    // --- explorer start-candidate priority: with an unexplored candidate the
    // explorer walks THERE; once it is explored, the frontier takes over.
    AiObservation fog = micro_observation();
    fog.units.push_back(fighter_unit(0x3300, 0, 400, 400, true));
    for (u32 ty = 0; ty < fog.map_height_tiles; ++ty) {
        for (u32 tx = 40; tx < fog.map_width_tiles; ++tx) {
            fog.tiles[ty * fog.map_width_tiles + tx].explored = false;
        }
    }
    fog.start_candidate_mask = (1u << 0) | (1u << 1);
    fog.start_candidate_x[0] = fog.start_x;
    fog.start_candidate_y[0] = fog.start_y;
    fog.start_candidate_x[1] = 50 * 32 + 16;
    fog.start_candidate_y[1] = 20 * 32 + 16;
    TyranoScriptedBotState explorer_bot{};
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    fog.simulation_frame = 1;
    TyranoScriptedBotDecision decision =
        DecideTyranoScriptedBotForHighLevelAction(explorer_bot, fog,
            AiRlHighLevelAction::explore_frontier, config);
    require(decision.code == TyranoScriptedBotDecisionCode::objective_updated,
        "explore_frontier was refused");
    AiMicroExecutorStep(explorer_bot.micro, fog);
    const AiMicroObjective& explore = AiMicroObjectiveOf(explorer_bot.micro,
        AiMicroGroup::explorer);
    require(explore.target_x == 50 * 32 + 16 && explore.target_y == 20 * 32 + 16,
        "explorer did not go to the unexplored start candidate first");
    fog.tiles[20 * fog.map_width_tiles + 50].explored = true;
    fog.simulation_frame = 1 + 9;
    AiMicroExecutorStep(explorer_bot.micro, fog);
    require(explore.target_x >= 0 &&
        !(explore.target_x == 50 * 32 + 16 && explore.target_y == 20 * 32 + 16),
        "explorer did not fall back to the frontier after the candidate");

    // --- scout guard: fires only while the enemy base is unknown.
    AiObservation guard_obs = micro_observation();
    guard_obs.units.push_back(fighter_unit(0x3400, 0, 400, 400, true));
    for (u32 ty = 0; ty < guard_obs.map_height_tiles; ++ty) {
        guard_obs.tiles[ty * guard_obs.map_width_tiles + 60].explored = false;
    }
    AiRlStepEncoding enc = EncodeAiObservationForRl(guard_obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::explore_frontier)] == 1,
        "guard fixture cannot explore");
    AiAutopilotState guard{};
    std::vector<AiRlHighLevelAction> plan = AiAutopilotPlan(guard, guard_obs,
        enc, AiRlHighLevelAction::no_op, 1300);
    bool scouted = false;
    for (const AiRlHighLevelAction action : plan) {
        scouted = scouted || action == AiRlHighLevelAction::explore_frontier;
    }
    require(scouted, "scout guard did not fire with the enemy base unknown");
    // Cooldown suppresses an immediate re-fire.
    plan = AiAutopilotPlan(guard, guard_obs, enc, AiRlHighLevelAction::no_op,
        1308);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::explore_frontier,
            "scout guard ignored its cooldown");
    }
    // An explorer already out suppresses it.
    AiAutopilotState guard2{};
    AiObservation busy = guard_obs;
    busy.explorer_unit_id = 0x3400;
    plan = AiAutopilotPlan(guard2, busy, enc, AiRlHighLevelAction::no_op, 1300);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::explore_frontier,
            "scout guard fired with an explorer already out");
    }
    // A known (visible) enemy building deactivates it.
    AiAutopilotState guard3{};
    AiObservation known = guard_obs;
    known.units.push_back(observed_unit(0x9e00, 1, 0x84u, 0, 1500, 1500,
        false));
    plan = AiAutopilotPlan(guard3, known, EncodeAiObservationForRl(known),
        AiRlHighLevelAction::no_op, 1300);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::explore_frontier,
            "scout guard fired although the enemy base is known");
    }
}

// v7 - a worker already walking to build reserves its site in the engine
// (placement TemporaryBlock), so the planner must not hand the same site to
// the next order; and a refused build order backs its structure type off.
void test_ai_pending_site_and_reject_backoff() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    const AiBuildSite first = FindAiBuildSite(obs, 0x82u, obs.start_x, obs.start_y, 16);
    require(first.found, "no base-area site for the pending-site test");
    // A second worker walking to build a population nest exactly there.
    AiObservedUnit walker = observed_unit(0x3400, 0, 0x20, (1u << 7), 300, 300, true);
    walker.command_state = 0x25;
    walker.command_value = 0x82;
    walker.path_target_x = (first.x & ~0x1f) + 115 / 2;   // site + interaction/2
    walker.path_target_y = (first.y & ~0x1f) + 86 / 2;
    obs.units.push_back(walker);
    const AiBuildSite next = FindAiBuildSite(obs, 0x82u, obs.start_x, obs.start_y, 16);
    require(next.found && (next.x != first.x || next.y != first.y),
        "pending build site was handed out again");
    const std::vector<u8> occupancy = AiBuildOccupancyGrid(obs);
    require(occupancy[(first.y >> 5) * obs.map_width_tiles + (first.x >> 5)] != 0,
        "pending site was not marked occupied");
    obs.units.pop_back();

    // Refused order back-off: the same structure type is illegal for a while
    // and feature 544 reports it; other types stay open.
    obs.last_build_reject_type = 0x82;
    obs.last_build_reject_frames_ago = 10;
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_population_nest)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_egg_nest)] == 1 &&
        enc.features[544] == 1.0f,
        "refused population nest was not backed off");
    obs.last_build_reject_frames_ago = 100;
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_population_nest)] == 1 &&
        enc.features[544] == 0.0f,
        "back-off did not expire");
}

// v10 - threat-proportional defense reflex (user replay review: one harasser
// recalled the whole main army).  A measured threat details only enough
// fighters (nearest-first, power >= threat * margin); the rest keep the
// policy's objective.  An unmeasurable threat (building health drop, no
// visible attacker) still triggers the full-army response.
void test_ai_reflex_proportional_detail() {
    AiObservation obs = micro_observation();
    // Outlying second nest - the threat anchor, far from the worker so no
    // flee orders mix into the assertion.
    obs.units.push_back(observed_unit(0x2001, 0, 0x80u, 0, 1600, 320, true));
    for (u32 i = 0; i < 6; ++i) {
        obs.units.push_back(fighter_unit(0x4100 + i, 0, 1500 + 8 * i, 1500,
            true));
    }
    TyranoScriptedBotState bot{};
    AiMicroObjective hold{};
    hold.kind = AiMicroObjectiveKind::defend;
    hold.target_x = 1504;
    hold.target_y = 1500;
    hold.radius = 128;
    hold.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, hold);
    // One intruder (power 100hp * 10 = 1000) at the outlying nest: needed =
    // 1000 * 150% = 1500 -> two 1000-power defenders (also the minimum).
    obs.units.push_back(fighter_unit(0x9c00, 1, 1640, 350, false));
    obs.simulation_frame = 10;
    const std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(bot.micro, obs);
    require(bot.micro.threat.active &&
        bot.micro.reflex_defenders.size() == 2,
        "measured threat did not produce a two-fighter detail");
    // Nearest-first to the anchor: the two highest-x fighters.
    require(std::find(bot.micro.reflex_defenders.begin(),
            bot.micro.reflex_defenders.end(), 0x4105u) !=
            bot.micro.reflex_defenders.end() &&
        std::find(bot.micro.reflex_defenders.begin(),
            bot.micro.reflex_defenders.end(), 0x4104u) !=
            bot.micro.reflex_defenders.end(),
        "detail was not picked nearest-first to the anchor");
    for (const AiSemanticAction& order : orders) {
        for (const u32 unit_id : order.unit_ids) {
            require(unit_id < 0x4100u || unit_id > 0x4103u,
                "a non-detail fighter was pulled off the policy objective");
        }
    }
    bool detail_moved = false;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::move &&
            order.target_x == 1640 && order.target_y == 350) {
            detail_moved = true;  // v10.5: anchor = the intruder, not the nest
        }
    }
    require(detail_moved, "the detail was not sent at the enemy force");
    require(AiMicroCombatPower(obs.units.back()) == 1000u,
        "combat power formula changed unexpectedly");
    // Unmeasurable threat: an own building losing health with NO visible
    // attacker sends a small investigation picket (user directive: at most
    // reflex_unseen_defenders = 3 fighters), never the whole army.
    AiObservation hurt = micro_observation();
    hurt.units.push_back(observed_unit(0x2001, 0, 0x80u, 0, 1600, 320, true));
    hurt.units.back().health = 500;
    hurt.units.back().max_health = 500;
    for (u32 i = 0; i < 6; ++i) {
        hurt.units.push_back(fighter_unit(0x4100 + i, 0, 1500 + 8 * i, 1500,
            true));
    }
    TyranoScriptedBotState bot2{};
    AiMicroSetObjective(bot2.micro, AiMicroGroup::army, hold);
    hurt.simulation_frame = 10;
    AiMicroExecutorStep(bot2.micro, hurt);
    require(!bot2.micro.threat.active, "reflex fired without a threat");
    hurt.units[hurt.units.size() - 7].health = 450;  // the nest lost health
    hurt.simulation_frame = 14;
    const std::vector<AiSemanticAction> picket_orders =
        AiMicroExecutorStep(bot2.micro, hurt);
    require(bot2.micro.threat.active &&
        bot2.micro.reflex_defenders.size() == 3,
        "an unseen-attacker threat did not send a 3-fighter picket");
    // Anchor falls back to the FIRST own building (the home nest at 320,320):
    // the three lowest-x fighters are nearest and form the picket.
    for (const u32 unit_id : {0x4100u, 0x4101u, 0x4102u}) {
        require(std::find(bot2.micro.reflex_defenders.begin(),
                bot2.micro.reflex_defenders.end(), unit_id) !=
                bot2.micro.reflex_defenders.end(),
            "the unseen-attacker picket was not picked nearest-first");
    }
    for (const AiSemanticAction& order : picket_orders) {
        for (const u32 unit_id : order.unit_ids) {
            require(unit_id < 0x4103u || unit_id > 0x4105u,
                "an unseen-attacker threat pulled more than the picket");
        }
    }
}

// v10 - hunt reachability guard (user replay report: the army parked at a
// cliff hunting a monster on a walkable island it could never reach).  The
// executor drops unreachable monsters from the hunt pick, and the RL mask
// closes the hunt actions when no reachable monster is in sight.
void test_ai_hunt_reachability_guard() {
    AiObservation obs = micro_observation();
    // A full-height impassable wall at tile x = 20 splits the map; the
    // monster stands on the far side.
    for (u32 y = 0; y < obs.map_height_tiles; ++y) {
        obs.tiles[y * obs.map_width_tiles + 20].passable = false;
    }
    obs.units.push_back(fighter_unit(0x4100, 0, 400, 400, true));
    obs.units.push_back(observed_unit(0x9100, kNeutralMonsterOwnerId, 0x30, 0,
        976, 400, false));
    const AiRlStepEncoding walled = EncodeAiObservationForRl(obs);
    require(walled.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::hunt_neutral_monster)] == 0,
        "hunt stayed legal with only an unreachable monster in sight");
    TyranoScriptedBotState bot{};
    AiMicroObjective hunt{};
    hunt.kind = AiMicroObjectiveKind::attack;
    hunt.tactic = AiMicroAttackTactic::neutral_only;
    hunt.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, hunt);
    obs.simulation_frame = 10;
    std::vector<AiSemanticAction> orders = AiMicroExecutorStep(bot.micro, obs);
    for (const AiSemanticAction& order : orders) {
        require(order.kind != AiSemanticActionKind::attack_unit &&
            order.kind != AiSemanticActionKind::attack_move,
            "the executor hunted an unreachable monster");
    }
    require(bot.micro.hunt_unreachable_skipped >= 1 &&
        AiMicroObjectiveOf(bot.micro, AiMicroGroup::army).target_unit_id == 0,
        "the unreachable monster was not dropped from the hunt pick");
    // Punch a gap into the wall on the fighter's row: the monster becomes
    // reachable, the mask opens and the hunt names it again.
    obs.tiles[12 * obs.map_width_tiles + 20].passable = true;
    require(EncodeAiObservationForRl(obs).legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::hunt_neutral_monster)] == 1,
        "hunt did not reopen once the monster became reachable");
    TyranoScriptedBotState bot2{};
    AiMicroSetObjective(bot2.micro, AiMicroGroup::army, hunt);
    orders = AiMicroExecutorStep(bot2.micro, obs);
    bool hunted = false;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::attack_unit &&
            order.target_unit_id == 0x9100) {
            hunted = true;
        }
    }
    require(hunted, "a reachable monster was not hunted");
}

// v10 - meat-pickup priority (user directive: fighters WITHOUT a held meat
// reserve collect first) and pickup under a defend objective, so drops near
// the base no longer rot once the hunt objective ends.
void test_ai_meat_priority_and_defend_pickup() {
    AiObservation obs = micro_observation();
    obs.units.push_back(fighter_unit(0x4400, 0, 560, 400, true));  // empty
    obs.units.push_back(fighter_unit(0x4401, 0, 580, 400, true));  // holds meat
    obs.units[obs.units.size() - 2].type_flags |= 0x2u;
    obs.units[obs.units.size() - 1].type_flags |= 0x2u;
    obs.units.back().action_mode = 500;  // held meat reserve (+0x2c)
    AiObservedMapEffect meat{};
    meat.id = 9;
    meat.effect_id = 2;
    meat.x = 620;
    meat.y = 400;
    meat.amount = 100;
    obs.map_effects.push_back(meat);
    // A monster far away keeps the named chase alive - pre-v10 both fighters
    // walked past the drop to it.
    obs.units.push_back(observed_unit(0x9100, kNeutralMonsterOwnerId, 0x30, 0,
        1400, 400, false));
    TyranoScriptedBotState bot{};
    AiMicroObjective hunt{};
    hunt.kind = AiMicroObjectiveKind::attack;
    hunt.tactic = AiMicroAttackTactic::neutral_only;
    hunt.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, hunt);
    obs.simulation_frame = 10;
    const std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(bot.micro, obs);
    bool empty_collects = false;
    bool full_chases = false;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::pickup_move &&
            order.target_x == 620 &&
            order.unit_ids == std::vector<u32>{0x4400}) {
            empty_collects = true;
        }
        if (order.kind == AiSemanticActionKind::attack_unit &&
            order.target_unit_id == 0x9100 &&
            order.unit_ids == std::vector<u32>{0x4401}) {
            full_chases = true;
        }
    }
    require(empty_collects,
        "the reserve-free fighter did not collect the drop first");
    require(full_chases, "the full fighter did not continue the hunt");
    // Defend objective: a defender with no bubble target collects a drop
    // INSIDE the bubble...
    AiObservation base = micro_observation();
    base.units.push_back(fighter_unit(0x4500, 0, 500, 400, true));
    base.units.back().type_flags |= 0x2u;
    AiObservedMapEffect drop{};
    drop.id = 11;
    drop.effect_id = 1;
    drop.x = 600;
    drop.y = 430;
    drop.amount = 80;
    base.map_effects.push_back(drop);
    TyranoScriptedBotState bot2{};
    AiMicroObjective guard{};
    guard.kind = AiMicroObjectiveKind::defend;
    guard.target_x = 500;
    guard.target_y = 400;
    guard.radius = 800;
    guard.assigned = true;
    AiMicroSetObjective(bot2.micro, AiMicroGroup::army, guard);
    base.simulation_frame = 10;
    std::vector<AiSemanticAction> base_orders =
        AiMicroExecutorStep(bot2.micro, base);
    bool defender_collects = false;
    for (const AiSemanticAction& order : base_orders) {
        if (order.kind == AiSemanticActionKind::pickup_move &&
            order.target_x == 600 &&
            order.unit_ids == std::vector<u32>{0x4500}) {
            defender_collects = true;
        }
    }
    require(defender_collects, "a defender left the drop in its bubble to rot");
    // v10.1 sticky assignment (user replay report: two collectors flapped and
    // trembled next to the drop): once 0x4500 owns the drop, a NEARER empty
    // fighter appearing must not steal it.
    base.units.push_back(fighter_unit(0x4501, 0, 590, 428, true));
    base.units.back().type_flags |= 0x2u;
    base.simulation_frame = 18;
    base_orders = AiMicroExecutorStep(bot2.micro, base);
    for (const AiSemanticAction& order : base_orders) {
        if (order.kind == AiSemanticActionKind::pickup_move) {
            require(order.unit_ids == std::vector<u32>{0x4500},
                "a nearer collector stole a sticky meat assignment");
        }
    }
    require(bot2.micro.meat_assignments.size() == 1 &&
        bot2.micro.meat_assignments[0].second == 0x4500u,
        "the meat assignment did not stay sticky in the executor state");
    // ...but never leaves the bubble for one (the leash would fight it).
    AiObservation tight = base;
    tight.map_effects[0].x = 900;
    TyranoScriptedBotState bot3{};
    AiMicroObjective narrow = guard;
    narrow.radius = 128;
    AiMicroSetObjective(bot3.micro, AiMicroGroup::army, narrow);
    for (const AiSemanticAction& order :
         AiMicroExecutorStep(bot3.micro, tight)) {
        require(order.kind != AiSemanticActionKind::pickup_move,
            "a defender chased a drop outside its bubble");
    }
}

// v10 - open-ring placement preference (user replay report: structures placed
// flush against each other grew into walls units could not pass).  With room
// available, the chosen site keeps its whole one-tile ring walkable - no cell
// of the ring may overlap a standing structure's footprint.
void test_ai_placement_open_ring_preference() {
    AiObservation obs = micro_observation();
    const AiBuildSite site = FindAiBuildSite(obs, 0x84u, obs.start_x,
        obs.start_y, 12);
    require(site.found, "no site found for the open-ring test");
    const AiBuildingFootprint egg = AiBuildingFootprintOf(0x84u);
    const AiBuildingFootprint nest = AiBuildingFootprintOf(0x80u);
    const i32 tx = site.x >> 5;
    const i32 ty = site.y >> 5;
    const i32 nest_tx = 320 >> 5;
    const i32 nest_ty = 320 >> 5;
    // The ring [tx-1, tx+fw] x [ty-1, ty+fh] must not touch the nest's
    // footprint - the rectangles must be separated on at least one axis.
    const bool separated =
        tx > nest_tx + static_cast<i32>(nest.width) ||
        tx + static_cast<i32>(egg.width) < nest_tx ||
        ty > nest_ty + static_cast<i32>(nest.height) ||
        ty + static_cast<i32>(egg.height) < nest_ty;
    require(separated,
        "the chosen site sits flush against the nest despite open ground");
}

// v10 - four fighting bodies (user directive): the two extra raid slots carry
// the exact raid semantics - own detach/merge, own objective, own mask gates -
// so main army + three detachments can act independently.
void test_ai_four_squads() {
    AiObservation obs = micro_observation();
    for (u32 i = 0; i < 12; ++i) {
        obs.units.push_back(fighter_unit(0x4000 + i, 0,
            380 + static_cast<i32>(i) * 10, 400, true));
    }
    obs.units.push_back(observed_unit(0x9400, 1, 0x84, 0, 1500, 1500, false));
    obs.army_group_unit_count = 12;
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::detach_raid_b)] == 1 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::raid_b_attack_units)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::merge_raid_b)] == 0,
        "raid_b mask did not gate on detach preconditions");
    obs.raid_b_unit_count = 3;
    enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::detach_raid_b)] == 0 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::raid_b_attack_units)] == 1 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::merge_raid_b)] == 1 &&
        enc.legal_mask[static_cast<std::size_t>(
            AiRlHighLevelAction::raid_c_attack_units)] == 0,
        "raid_b mask did not open independently of raid_c");
    require(enc.features[788] > 0.0f && enc.features[789] == 1.0f &&
        enc.features[795] == 0.0f,
        "raid_b/raid_c group features were not encoded");
    obs.raid_b_unit_count = 0;
    // Translator: three successive detaches carve three disjoint 3-unit
    // squads off the 12-fighter army (30%, floor 3, half cap).
    TyranoScriptedBotState state{};
    state.rally_configured = true;
    TyranoScriptedBotConfig config{};
    config.decision_interval_frames = 1;
    obs.simulation_frame = 1;
    DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::detach_raid, config);
    obs.simulation_frame = 2;
    DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::detach_raid_b, config);
    obs.simulation_frame = 3;
    DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::detach_raid_c, config);
    require(AiMicroGroupMembers(state.micro, obs, AiMicroGroup::raid).size()
            == 3 &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::raid_b).size()
            == 3 &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::raid_c).size()
            == 3 &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::army).size() == 3,
        "three detaches did not carve three disjoint squads");
    // Independent objectives per squad; the untouched raid keeps its hold.
    obs.simulation_frame = 4;
    DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::raid_b_attack_units, config);
    obs.simulation_frame = 5;
    DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::raid_c_retreat, config);
    require(AiMicroObjectiveOf(state.micro, AiMicroGroup::raid_b).kind ==
            AiMicroObjectiveKind::attack &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::raid_b).tactic ==
            AiMicroAttackTactic::units_first &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::raid_c).kind ==
            AiMicroObjectiveKind::retreat &&
        AiMicroObjectiveOf(state.micro, AiMicroGroup::raid).kind ==
            AiMicroObjectiveKind::defend,
        "squad objectives were not independent");
    // Merging one squad returns exactly its members to the army.
    obs.simulation_frame = 6;
    DecideTyranoScriptedBotForHighLevelAction(state, obs,
        AiRlHighLevelAction::merge_raid_b, config);
    require(AiMicroGroupMembers(state.micro, obs,
            AiMicroGroup::raid_b).empty() &&
        AiMicroGroupMembers(state.micro, obs, AiMicroGroup::army).size() == 6,
        "merge_raid_b did not fold the squad back into the army");
}

// v10 - attack commit + hunt distance gate (user directive: an ordered attack
// must at least meet the enemy; the main army does not trek across the map
// for monsters - that is the raid slots' job).
void test_ai_attack_commit_and_hunt_range() {
    const auto legal = [](const AiRlStepEncoding& enc,
        AiRlHighLevelAction action) {
        return enc.legal_mask[static_cast<std::size_t>(action)] != 0;
    };
    AiObservation obs = micro_observation();
    obs.population_used = 20;      // supply so produce_worker stays open
    obs.population_reserved = 5;
    obs.units.push_back(fighter_unit(0x4100, 0, 400, 400, true));
    obs.units.push_back(observed_unit(0x9400, 1, 0x84, 0, 1500, 1500, false));
    obs.army_group_unit_count = 1;
    obs.army_objective_kind = 2;   // attack, marching, un-engaged
    obs.army_attack_has_target = 1;
    obs.army_engaged_since_set = 0;
    obs.army_objective_age = 100;
    // v10.6: retreat needs combat proximity - a hostile fighter near ours.
    obs.units.push_back(fighter_unit(0x9c20, 1, 700, 400, false));
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(!legal(enc, AiRlHighLevelAction::attack_nearest_enemy) &&
        !legal(enc, AiRlHighLevelAction::defend_base) &&
        !legal(enc, AiRlHighLevelAction::hunt_neutral_monster) &&
        !legal(enc, AiRlHighLevelAction::search_enemy_base) &&
        legal(enc, AiRlHighLevelAction::retreat) &&
        legal(enc, AiRlHighLevelAction::produce_worker),
        "attack commit did not lock army re-tasking (retreat/macro open)");
    obs.army_engaged_since_set = 1;   // first contact releases
    enc = EncodeAiObservationForRl(obs);
    require(legal(enc, AiRlHighLevelAction::attack_nearest_enemy),
        "first contact did not release the commit lock");
    obs.army_engaged_since_set = 0;
    obs.army_objective_age = 3000;    // timeout releases
    enc = EncodeAiObservationForRl(obs);
    require(legal(enc, AiRlHighLevelAction::attack_nearest_enemy),
        "commit timeout did not release the lock");
    obs.units.pop_back();  // drop the proximity fighter for the defend gate
    enc = EncodeAiObservationForRl(obs);
    // v10.2 defend gate: no hostile COMBAT mobile near an own building (the
    // far enemy nest is a building) -> defend stays closed; a fighter near
    // the base opens it.
    require(!legal(enc, AiRlHighLevelAction::defend_base),
        "defend was legal with no visible threat near the base");
    obs.units.push_back(fighter_unit(0x9c10, 1, 800, 320, false));
    enc = EncodeAiObservationForRl(obs);
    require(legal(enc, AiRlHighLevelAction::defend_base),
        "defend did not open with a hostile fighter near the base");
    obs.units.pop_back();
    enc = EncodeAiObservationForRl(obs);
    // v10.2: the threat CLEARING while the army defends fires objective_done
    // so the policy re-decides instead of defending an empty base forever.
    {
        AiObservation guard_obs = micro_observation();
        guard_obs.units.push_back(fighter_unit(0x4100, 0, 400, 400, true));
        guard_obs.units.push_back(fighter_unit(0x9c11, 1, 800, 320, false));
        guard_obs.army_objective_kind = 3;  // defend
        AiDecisionGateState gate{};
        AiRlStepEncoding guard_enc = EncodeAiObservationForRl(guard_obs);
        AiDecisionGateResult r = AiDecisionGateEvaluate(gate, guard_obs,
            guard_enc, {}, false, 100);
        require(r.due, "defend-clear fixture: first decision did not fire");
        guard_obs.units.pop_back();  // the threat leaves
        guard_enc = EncodeAiObservationForRl(guard_obs);
        r = AiDecisionGateEvaluate(gate, guard_obs, guard_enc, {}, false, 200);
        require(r.due && (r.triggers & trigger_objective_done) != 0,
            "threat clearing under a defend objective did not re-decide");
    }
    // A marching raid slot locks its own actions (incl. merge) only.
    obs.raid_b_unit_count = 3;
    obs.raid_b_objective_kind = 2;
    obs.raid_b_attack_has_target = 1;
    obs.raid_b_engaged_since_set = 0;
    obs.raid_b_objective_age = 50;
    enc = EncodeAiObservationForRl(obs);
    obs.units.push_back(fighter_unit(0x9c21, 1, 700, 400, false));
    enc = EncodeAiObservationForRl(obs);
    require(!legal(enc, AiRlHighLevelAction::raid_b_attack_units) &&
        !legal(enc, AiRlHighLevelAction::merge_raid_b) &&
        legal(enc, AiRlHighLevelAction::raid_b_retreat) &&
        legal(enc, AiRlHighLevelAction::attack_nearest_enemy),
        "raid_b commit lock leaked or missed");
    obs.units.pop_back();
    enc = EncodeAiObservationForRl(obs);
    require(!legal(enc, AiRlHighLevelAction::retreat),
        "retreat stayed legal with no hostile near any fighter");

    // Hunt distance gate: a monster 1400 px from the army centroid closes the
    // ARMY hunt but leaves the raid hunt open; a near monster opens both.
    AiObservation hunt = micro_observation();
    hunt.units.push_back(fighter_unit(0x4100, 0, 400, 400, true));
    hunt.units.push_back(observed_unit(0x9100, kNeutralMonsterOwnerId, 0x30,
        0, 1800, 400, false));
    hunt.army_group_unit_count = 1;
    hunt.army_centroid_x = 400;
    hunt.army_centroid_y = 400;
    hunt.raid_unit_count = 3;
    enc = EncodeAiObservationForRl(hunt);
    require(!legal(enc, AiRlHighLevelAction::hunt_neutral_monster) &&
        legal(enc, AiRlHighLevelAction::raid_hunt_neutral),
        "far monster did not close the army hunt (raid stays open)");
    hunt.units.back().x = 900;
    enc = EncodeAiObservationForRl(hunt);
    require(legal(enc, AiRlHighLevelAction::hunt_neutral_monster),
        "near monster did not open the army hunt");
    // Executor: under an army hunt objective the far monster is not named.
    hunt.units.back().x = 1800;
    TyranoScriptedBotState bot{};
    AiMicroObjective hunt_objective{};
    hunt_objective.kind = AiMicroObjectiveKind::attack;
    hunt_objective.tactic = AiMicroAttackTactic::neutral_only;
    hunt_objective.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, hunt_objective);
    hunt.simulation_frame = 10;
    AiMicroExecutorStep(bot.micro, hunt);
    require(AiMicroObjectiveOf(bot.micro, AiMicroGroup::army).target_unit_id
            == 0,
        "the army hunted a monster beyond its hunt radius");
    hunt.units.back().x = 900;
    hunt.simulation_frame = 18;
    AiMicroExecutorStep(bot.micro, hunt);
    require(AiMicroObjectiveOf(bot.micro, AiMicroGroup::army).target_unit_id
            == 0x9100,
        "the army did not hunt a monster inside its hunt radius");
}

// v10 - replay-review fixes: a stale harvest command flag (0x4) must not park
// an idle worker, and a hostile in SIGHT (not only weapon contact) disables
// the cohesion return so units stop trembling next to enemy soldiers.
void test_ai_idle_worker_stale_flag_and_cohesion_sight() {
    AiObservation obs = micro_observation();
    obs.units[0].command_flags = 0x4u;  // stale harvest flag, unit fully idle
    TyranoScriptedBotState bot{};
    obs.simulation_frame = 10;
    const std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(bot.micro, obs);
    bool harvests = false;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::harvest &&
            order.unit_ids == std::vector<u32>{0x1000}) {
            harvests = true;
        }
    }
    require(harvests, "a stale harvest flag parked an idle worker");

    // Cohesion (v10): a fast leader far ahead of its group WAITS IN PLACE -
    // it must never walk backward (the army surge) and never charge the
    // nearby enemy alone; weapon contact still releases the gate.
    AiObservation march = micro_observation();
    march.units.push_back(fighter_unit(0x4100, 0, 1200, 400, true)); // leader
    march.units.push_back(fighter_unit(0x4101, 0, 400, 400, true));  // laggards
    march.units.push_back(fighter_unit(0x4102, 0, 420, 400, true));
    march.units.push_back(fighter_unit(0x9c00, 1, 1500, 400, false)); // enemy near, out of reach
    TyranoScriptedBotState bot2{};
    AiMicroExecutorConfig no_reflex{};
    no_reflex.reflex_enabled = false;
    AiMicroObjective attack{};
    attack.kind = AiMicroObjectiveKind::attack;
    attack.tactic = AiMicroAttackTactic::units_first;
    attack.assigned = true;
    AiMicroSetObjective(bot2.micro, AiMicroGroup::army, attack);
    march.simulation_frame = 10;
    bool leader_waits = false;
    for (const AiSemanticAction& order :
         AiMicroExecutorStep(bot2.micro, march, no_reflex)) {
        for (const u32 unit_id : order.unit_ids) {
            if (unit_id == 0x4100u) {
                leader_waits = order.kind == AiSemanticActionKind::move &&
                    order.target_x == 1200 && order.target_y == 400;
            }
        }
    }
    require(leader_waits && bot2.micro.cohesion_holds != 0,
        "a far-ahead fast leader did not wait in place for its group");
}

// v10.3 - the cohesion anchor is the group MEDIAN, not the mean: a tail of
// freshly produced units walking up from the base must not stop the marching
// front (2026-09-01 user replay report: the army kept stop-and-going as
// reinforcements spawned).
void test_ai_cohesion_median_anchor() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    // Front mass: three fighters together mid-march; tail: two fresh spawns
    // at the base.  Mean x = 968 (front gap 432 -> would stop); median 1400.
    obs.units.push_back(fighter_unit(0x4100, 0, 1400, 400, true));
    obs.units.push_back(fighter_unit(0x4101, 0, 1390, 400, true));
    obs.units.push_back(fighter_unit(0x4102, 0, 1410, 400, true));
    obs.units.push_back(fighter_unit(0x4103, 0, 320, 320, true));
    obs.units.push_back(fighter_unit(0x4104, 0, 330, 320, true));
    obs.enemy_building_memory.assign(obs.tiles.size(), 0);
    obs.enemy_building_memory[12 * obs.map_width_tiles + 62] = 1;  // (2000,400)
    TyranoScriptedBotState bot{};
    AiMicroExecutorConfig no_reflex{};
    no_reflex.reflex_enabled = false;
    AiMicroObjective attack{};
    attack.kind = AiMicroObjectiveKind::attack;
    attack.tactic = AiMicroAttackTactic::buildings_first;
    attack.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, attack);
    obs.simulation_frame = 10;
    bool front_advances = false;
    for (const AiSemanticAction& order :
         AiMicroExecutorStep(bot.micro, obs, no_reflex)) {
        for (const u32 unit_id : order.unit_ids) {
            if (unit_id >= 0x4100u && unit_id <= 0x4102u) {
                front_advances =
                    order.kind == AiSemanticActionKind::attack_move &&
                    order.target_x == 2000;
            }
        }
    }
    require(front_advances && bot.micro.cohesion_holds == 0,
        "the reinforcement tail stopped the marching front (mean anchor)");
}

// v10.4 - attack waves (user design): members present at the attack order
// form the first wave and march regardless of later spawns; fighters
// produced afterwards STAGE at the base and leave only once
// attack_wave_minimum of them gather, as their own wave.
void test_ai_attack_waves() {
    AiObservation obs = micro_observation();
    obs.units[0].command_state = kUnitStateWorkerApproachHarvest;
    obs.units.push_back(fighter_unit(0x4100, 0, 700, 400, true));
    obs.units.push_back(fighter_unit(0x4101, 0, 710, 400, true));
    obs.units.push_back(fighter_unit(0x4102, 0, 720, 400, true));
    obs.enemy_building_memory.assign(obs.tiles.size(), 0);
    obs.enemy_building_memory[12 * obs.map_width_tiles + 62] = 1;  // (2000,400)
    TyranoScriptedBotState bot{};
    AiMicroExecutorConfig no_reflex{};
    no_reflex.reflex_enabled = false;
    AiMicroObjective attack{};
    attack.kind = AiMicroObjectiveKind::attack;
    attack.tactic = AiMicroAttackTactic::buildings_first;
    attack.assigned = true;
    AiMicroSetObjective(bot.micro, AiMicroGroup::army, attack);
    obs.simulation_frame = 10;
    std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(bot.micro, obs, no_reflex);
    u32 first_wave_marchers = 0;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::attack_move &&
            order.target_x == 2000) {
            first_wave_marchers += static_cast<u32>(order.unit_ids.size());
        }
    }
    require(first_wave_marchers == 3,
        "the first wave did not march together");
    // Two fresh spawns at the base: they must STAGE, not join the assault.
    obs.units.push_back(fighter_unit(0x4200, 0, 340, 320, true));
    obs.units.push_back(fighter_unit(0x4201, 0, 350, 320, true));
    obs.simulation_frame = 20;
    orders = AiMicroExecutorStep(bot.micro, obs, no_reflex);
    for (const AiSemanticAction& order : orders) {
        if (order.kind != AiSemanticActionKind::attack_move) {
            continue;
        }
        for (const u32 unit_id : order.unit_ids) {
            require(unit_id < 0x4200u,
                "a staging spawn trickled into the ongoing assault");
        }
    }
    for (const AiMicroUnitRecord& record : bot.micro.units) {
        if (record.unit_id == 0x4200u || record.unit_id == 0x4201u) {
            require(record.attack_wave == 0,
                "a late spawn was enrolled into the first wave");
        }
    }
    // Four more gather (6 staging >= attack_wave_minimum): they leave as
    // wave 2 together.
    for (u32 i = 0; i < 4; ++i) {
        obs.units.push_back(fighter_unit(0x4202 + i, 0,
            360 + static_cast<i32>(i) * 10, 320, true));
    }
    obs.simulation_frame = 30;
    orders = AiMicroExecutorStep(bot.micro, obs, no_reflex);
    u32 second_wave_marchers = 0;
    for (const AiSemanticAction& order : orders) {
        if (order.kind == AiSemanticActionKind::attack_move &&
            order.target_x == 2000) {
            for (const u32 unit_id : order.unit_ids) {
                if (unit_id >= 0x4200u) {
                    ++second_wave_marchers;
                }
            }
        }
    }
    require(second_wave_marchers == 6,
        "the gathered staging fighters did not leave as their own wave");
    for (const AiMicroUnitRecord& record : bot.micro.units) {
        if (record.unit_id >= 0x4200u && record.unit_id <= 0x4205u) {
            require(record.attack_wave == 2,
                "the second wave did not get its own wave id");
        }
    }
}

// v10 - autopilot tech guard (2026-09-01 user replay report: no tech
// buildings were ever built): a fat bank builds the first missing building
// of the audited chain; cooldown and policy-build collisions respected.
void test_ai_autopilot_tech_guard() {
    AiObservation obs = micro_observation();
    obs.population_used = 20;
    obs.population_reserved = 5;
    obs.primary_resources = 5000;
    // Completed egg nest -> the first missing chain slot is the land nest.
    obs.units.push_back(observed_unit(0x7000, 0, 0x84u, 0, 700, 700, true));
    AiRlStepEncoding enc = EncodeAiObservationForRl(obs);
    require(enc.legal_mask[static_cast<std::size_t>(
                AiRlHighLevelAction::build_land_nest)] == 1,
        "tech-guard fixture cannot build a land nest");
    AiAutopilotState state{};
    AiAutopilotConfig tech_on{};
    tech_on.tech_guard_enabled = true;  // default OFF (policy learns tech)
    std::vector<AiRlHighLevelAction> plan = AiAutopilotPlan(state, obs, enc,
        AiRlHighLevelAction::no_op, 500, tech_on);
    bool teched = false;
    for (const AiRlHighLevelAction action : plan) {
        teched = teched || action == AiRlHighLevelAction::build_land_nest;
    }
    require(teched, "tech guard did not build the first missing tech building");
    // Cooldown suppresses an immediate refire.
    plan = AiAutopilotPlan(state, obs, enc, AiRlHighLevelAction::no_op, 508,
        tech_on);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::build_land_nest,
            "tech guard ignored its cooldown");
    }
    // A land nest under construction fills the slot: next slot is 0x86.
    AiObservation building = obs;
    AiObservedUnit land_uc = observed_unit(0x7100, 0, 0x85u, 0, 800, 700, true);
    land_uc.under_construction = true;
    building.units.push_back(land_uc);
    AiAutopilotState state2{};
    plan = AiAutopilotPlan(state2, building, EncodeAiObservationForRl(building),
        AiRlHighLevelAction::no_op, 900, tech_on);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::build_land_nest,
            "tech guard rebuilt a slot already under construction");
    }
    // A policy build this frame suppresses the guard entirely.
    AiAutopilotState state3{};
    plan = AiAutopilotPlan(state3, obs, enc,
        AiRlHighLevelAction::build_egg_nest, 900, tech_on);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::build_land_nest,
            "tech guard collided with a policy build");
    }
    // Default config: the guard stays OFF (the policy owns tech timing).
    AiAutopilotState state4{};
    plan = AiAutopilotPlan(state4, obs, enc, AiRlHighLevelAction::no_op, 900);
    for (const AiRlHighLevelAction action : plan) {
        require(action != AiRlHighLevelAction::build_land_nest,
            "tech guard collided with a policy build");
    }
}


// ===========================================================================
// Entity-command RL Phase A (docs/AI_PLAY_ENTITY_COMMAND_RL_PLAN.md):
// activation registry, entity snapshot encoding, point geometry v1,
// authoritative attack pair predicate and the act2 wire contract.
// ===========================================================================

UnitMovementUnit make_entity_live_unit(u32 slot, u32 owner, u32 type_id,
    u32 type_flags) {
    UnitMovementUnit unit{};
    unit.id = slot * 0x1d0u;
    unit.runtime_slot_index = slot;
    unit.owner_id = owner;
    unit.type_id = type_id;
    unit.type_flags = type_flags;
    unit.active = true;
    unit.definition.action_range_base = 50;
    unit.definition.movement_class = 0;
    return unit;
}

void test_ai_entity_registry() {
    AiEntityRegistry registry;
    AiEntityRegistryReset(registry);
    UnitMovementUnit a = make_entity_live_unit(1, 0, 5, 1u << 5);
    std::vector<UnitMovementUnit*> active{&a};

    // First audit commits the missed activation as generation 1 / epoch 1.
    AiEntityRegistryAuditFrame(registry, active);
    const AiEntityRegistryRecord* record =
        AiEntityRegistryFindByUnit(registry, a);
    require(record != nullptr && record->generation == 1 &&
        record->control_epoch == 1 && record->engine_active,
        "initial audit did not register generation 1");

    // Overlapping helper calls of one activation stay at one commit.
    AiEntityRegistryCommitActivation(registry, a);
    AiEntityRegistryCommitActivation(registry, a);
    require(record->generation == 1,
        "nested activation helper calls double-committed the generation");

    // Deactivate + reactivate the same slot/id: new generation, epoch reset.
    AiEntityRegistryMarkDeactivated(registry, a);
    require(!record->engine_active, "deactivation was not marked immediately");
    AiEntityRegistryCommitActivation(registry, a);
    require(record->generation == 2 && record->control_epoch == 1,
        "same slot/id reactivation did not advance the generation");

    // Owner change without deactivation: control epoch bump, generation kept.
    a.owner_id = 3;
    AiEntityRegistryAuditFrame(registry, active);
    require(record->generation == 2 && record->control_epoch == 2,
        "owner transfer did not bump the control epoch");
    // A -> B -> A round trip is one more distinct epoch (stale replies from
    // the first ownership must stay rejectable).
    a.owner_id = 0;
    AiEntityRegistryAuditFrame(registry, active);
    require(record->control_epoch == 3,
        "ownership round trip reused an old control epoch");
    // Capability-signature change (attack bit lost) also bumps the epoch.
    a.type_flags = 0;
    AiEntityRegistryAuditFrame(registry, active);
    require(record->control_epoch == 4,
        "capability change did not bump the control epoch");

    // Audit-detected deactivation (a path the hooks missed).
    std::vector<UnitMovementUnit*> empty_active;
    AiEntityRegistryAuditFrame(registry, empty_active);
    require(!record->engine_active,
        "audit did not sweep a vanished active record");

    // Detached record (free list exhausted): pointer-keyed sparse registry.
    UnitMovementUnit detached{};
    detached.id = 0x80000001u;
    detached.runtime_slot_index = kInvalidUnitRuntimeSlotIndex;
    detached.owner_id = 0;
    detached.type_id = 5;
    detached.type_flags = 1u << 5;
    detached.active = true;
    detached.definition.action_range_base = 50;
    std::vector<UnitMovementUnit*> with_detached{&detached};
    AiEntityRegistryAuditFrame(registry, with_detached);
    const AiEntityRegistryRecord* detached_record =
        AiEntityRegistryFindByObserved(registry, 0x80000001u,
            kInvalidUnitRuntimeSlotIndex);
    require(detached_record != nullptr && detached_record->generation == 1 &&
        detached_record->engine_active,
        "detached spawn was not tracked in the sparse registry");

    // Two simultaneously active records with one runtime id: contract-fatal,
    // never a silent tie-break.
    UnitMovementUnit dup = make_entity_live_unit(2, 0, 5, 1u << 5);
    dup.id = detached.id;
    dup.runtime_slot_index = 2;
    std::vector<UnitMovementUnit*> duplicated{&detached, &dup};
    AiEntityRegistryAuditFrame(registry, duplicated);
    require(registry.contract_fatal,
        "duplicate active runtime id was not contract-fatal");
    AiEntityRegistryReset(registry);
    require(!registry.contract_fatal,
        "registry reset did not clear the contract-fatal latch");
}

UnitMovementMap make_entity_legacy_map(u32 width, u32 height) {
    UnitMovementMap map;
    map.width = width;
    map.height = height;
    map.stride_tiles = width;
    map.legacy_entry_layers_present = true;
    UnitMovementCell open{};
    open.alternate_flags = 0x60000000u;   // classes 0/2/4 may enter
    map.cells.assign(static_cast<std::size_t>(width) * height, open);
    return map;
}

void block_entity_map_column(UnitMovementMap& map, u32 tile_x) {
    for (u32 ty = 0; ty < map.height; ++ty) {
        map.cells[static_cast<std::size_t>(ty) * map.width + tile_x]
            .alternate_flags = 0;
    }
}

void test_ai_entity_point_geometry() {
    // 8x8 tiles: one global cell = one tile.  A blocked column at x=4 splits
    // the map into two static components.
    UnitMovementMap map = make_entity_legacy_map(8, 8);
    block_entity_map_column(map, 4);
    const AiEntityReachability ground = BuildAiEntityReachability(map, 0);
    std::array<u32, kAiEntityPointMaskWords> mask{};
    // Unit at tile (1,1), world (48,48).
    BuildAiEntityPointMask(map, ground, 48, 48, mask);
    auto bit = [&mask](u32 token) {
        return (mask[token >> 5] >> (token & 31u)) & 1u;
    };
    require(bit(0) == 1 && bit(3) == 1 && bit(4) == 0 && bit(7) == 0,
        "global tokens did not follow the static component split");
    // Row 2 (tokens 16..23): same split.
    require(bit(16 + 3) == 1 && bit(16 + 5) == 0,
        "global token row 2 ignored the wall");
    // Air (class 3) crosses the wall; class 1 can never enter anywhere.
    const AiEntityReachability air = BuildAiEntityReachability(map, 3);
    std::array<u32, kAiEntityPointMaskWords> air_mask{};
    BuildAiEntityPointMask(map, air, 48, 48, air_mask);
    require(((air_mask[0] >> 4) & 1u) == 1 && ((air_mask[1] >> 31) & 1u) == 1,
        "air reachability did not ignore the ground wall");
    const AiEntityReachability none = BuildAiEntityReachability(map, 1);
    std::array<u32, kAiEntityPointMaskWords> none_mask{};
    BuildAiEntityPointMask(map, none, 48, 48, none_mask);
    require(none_mask[0] == 0 && none_mask[1] == 0 && none_mask[2] == 0,
        "movement class 1 gained point tokens");

    // Local tokens: E radius 64 stays left of the wall (bit set); E radius
    // 256 crosses it (cleared); E radius 512 leaves the 256px map (cleared).
    const u32 east_64 = 64 + 0 * 8 + 0;
    const u32 east_256 = 64 + 2 * 8 + 0;
    const u32 east_512 = 64 + 3 * 8 + 0;
    require(bit(east_64) == 1 && bit(east_256) == 0 && bit(east_512) == 0,
        "local east tokens ignored the wall or map bounds");

    // Token resolution: global cell 0 resolves to the tile-0 world center.
    const AiEntityPointResolveResult cell0 =
        ResolveAiEntityPointToken(map, ground, 48, 48, 0);
    require(cell0.valid && cell0.x == 16 && cell0.y == 16,
        "global token 0 did not resolve to the tile center");
    const AiEntityPointResolveResult east =
        ResolveAiEntityPointToken(map, ground, 48, 48, east_64);
    require(east.valid && east.x == 48 + 64 && east.y == 48,
        "local east token did not resolve to the absolute offset point");
    require(!ResolveAiEntityPointToken(map, ground, 48, 48, 4).valid,
        "a wall cell resolved to a point");

    // Tie-break on a 16x16 map (2x2 tiles per cell): all four tile centers
    // are equidistant from the rational cell center -> smallest row-major
    // tile index wins.
    UnitMovementMap wide = make_entity_legacy_map(16, 16);
    const AiEntityReachability wide_reach = BuildAiEntityReachability(wide, 0);
    const AiEntityPointResolveResult tie =
        ResolveAiEntityPointToken(wide, wide_reach, 48, 48, 0);
    require(tie.valid && tie.x == 16 && tie.y == 16,
        "global-cell tie-break did not pick the smallest row-major tile");

    // The runtime building-footprint bit is cleared before the static
    // judgment: an occupied tile stays point-legal.
    UnitMovementMap footprint = make_entity_legacy_map(8, 8);
    footprint.cells[0].visibility_flags = 0x20000000u;
    const AiEntityReachability foot_reach =
        BuildAiEntityReachability(footprint, 0);
    require(ResolveAiEntityPointToken(footprint, foot_reach, 48, 48, 0).valid,
        "a building footprint leaked into the static point mask");

    // Virtual flood seed: a unit standing on a statically invalid tile keeps
    // the tokens of the neighboring component.
    UnitMovementMap seed_map = make_entity_legacy_map(8, 8);
    seed_map.cells[static_cast<std::size_t>(1) * 8 + 1].alternate_flags = 0;
    const AiEntityReachability seed_reach =
        BuildAiEntityReachability(seed_map, 0);
    std::array<u32, kAiEntityPointMaskWords> seed_mask{};
    BuildAiEntityPointMask(seed_map, seed_reach, 48, 48, seed_mask);
    require(((seed_mask[0] >> 0) & 1u) == 1,
        "virtual flood seed did not join the neighboring component");

    // Non-legacy map: passable terrain only; static blocked rejects, the
    // dynamic reserved-by-unit bit is ignored.
    UnitMovementMap plain;
    plain.width = 8;
    plain.height = 8;
    plain.stride_tiles = 8;
    plain.legacy_entry_layers_present = false;
    UnitMovementCell plain_open{};
    plain_open.flags = kMapCellPassableTerrain;
    plain.cells.assign(64, plain_open);
    plain.cells[1].flags |= kMapCellBlockedTerrain;
    plain.cells[2].flags |= kMapCellReservedByUnit;
    require(!AiEntityStaticCellEnterable(plain, 0, 1, 0) &&
        AiEntityStaticCellEnterable(plain, 0, 2, 0) &&
        AiEntityStaticCellEnterable(plain, 0, 3, 0),
        "non-legacy static entry rule mishandled blocked/reserved bits");
}

void test_ai_entity_attack_pair_predicate() {
    AiEntityPairSource source;
    source.runtime_id = 0x1d0;
    source.active_owned_alive = true;
    source.has_attack_capability = true;
    source.distance_check_mode = 0;
    source.attackable_class_mask = 1u << 1;
    AiEntityPairTarget target;
    target.runtime_id = 0x3a0;
    target.active_alive = true;
    target.visible = true;
    target.non_friendly = true;
    target.render_class = 1;
    AiEntityLiveHooks hooks{};
    require(AiEntityEvaluateAttackPair(source, target, hooks).legal,
        "a plain legal pair was rejected");

    // Source-side gates.
    AiEntityPairSource s2 = source;
    s2.distance_check_mode = 1;
    require(!AiEntityEvaluateAttackPair(s2, target, hooks).legal,
        "distance_check_mode==1 source was not early-rejected");
    s2 = source;
    s2.has_attack_capability = false;
    require(!AiEntityEvaluateAttackPair(s2, target, hooks).legal,
        "attack-incapable source was not rejected (planner downgrade leak)");

    // Target runtime flags.
    AiEntityPairTarget t2 = target;
    t2.runtime_flags = kAiEntityTargetFlagTransient;
    require(!AiEntityEvaluateAttackPair(source, t2, hooks).legal,
        "transient target flag was ignored");
    t2 = target;
    t2.runtime_flags = kAiEntityTargetFlagInactive;
    require(!AiEntityEvaluateAttackPair(source, t2, hooks).legal,
        "inactive target flag was ignored");
    t2 = target;
    t2.runtime_flags = kAiEntityTargetFlagClassBlocked;
    require(!AiEntityEvaluateAttackPair(source, t2, hooks).legal,
        "class-blocked target flag was ignored");

    // Class mask below 32; permissive at 32 and above.
    t2 = target;
    t2.render_class = 3;
    require(!AiEntityEvaluateAttackPair(source, t2, hooks).legal,
        "class mask did not gate render class 3");
    t2.render_class = 40;
    require(AiEntityEvaluateAttackPair(source, t2, hooks).legal,
        "render_class >= 32 was not engine-permissive");

    // Class-2 terrain gate: only when the profile gate is 0, judged at the
    // target's 32px-aligned cell through the live hook.
    struct TerrainFixture {
        u32 calls = 0;
        i32 x = 0;
        i32 y = 0;
        bool result = false;
    };
    static TerrainFixture fixture;
    fixture = TerrainFixture{};
    AiEntityLiveHooks terrain_hooks{};
    terrain_hooks.ctx = &fixture;
    terrain_hooks.source_can_enter_cell = [](void* ctx, u32, i32 x, i32 y) {
        auto* f = static_cast<TerrainFixture*>(ctx);
        ++f->calls;
        f->x = x;
        f->y = y;
        return f->result;
    };
    AiEntityPairSource class2_source = source;
    class2_source.attackable_class_mask = 1u << 2;
    class2_source.render_class2_terrain_gate = 0;
    AiEntityPairTarget class2_target = target;
    class2_target.render_class = 2;
    class2_target.x = 100;
    class2_target.y = 77;
    require(!AiEntityEvaluateAttackPair(class2_source, class2_target,
            terrain_hooks).legal &&
        fixture.calls == 1 && fixture.x == 96 && fixture.y == 64,
        "class-2 terrain gate did not query the aligned target cell");
    fixture.result = true;
    require(AiEntityEvaluateAttackPair(class2_source, class2_target,
            terrain_hooks).legal,
        "class-2 pair stayed illegal though the terrain check passed");
    class2_source.render_class2_terrain_gate = 1;
    fixture.calls = 0;
    require(AiEntityEvaluateAttackPair(class2_source, class2_target,
            terrain_hooks).legal && fixture.calls == 0,
        "nonzero profile gate still invoked the terrain check");
}

AiObservation make_entity_snapshot_observation() {
    AiObservation obs{};
    obs.simulation_frame = 1232;
    obs.map_width_tiles = 8;
    obs.map_height_tiles = 8;
    obs.local_owner = 0;
    obs.active_owner_mask = 0x3;
    obs.local_relation_mask = 0x1;
    obs.start_x = 48;
    obs.start_y = 48;
    // Own fighter (move/attack/patrol capable).
    AiObservedUnit own = fighter_unit(1 * 0x1d0, 0, 48, 48, true);
    own.runtime_slot_index = 1;
    own.type_flags = (1u << 4) | (1u << 5) | (1u << 9);
    own.attackable_class_mask = 0xffffffffu;
    obs.units.push_back(own);
    // Own worker (harvest bit): excluded from the entity actor.
    AiObservedUnit worker = observed_unit(2 * 0x1d0, 0, 0x10, 1u << 7,
        60, 60, true);
    worker.runtime_slot_index = 2;
    worker.alive = true;
    obs.units.push_back(worker);
    // Own under-construction building: excluded.
    AiObservedUnit building = observed_unit(3 * 0x1d0, 0, 0x80, 0, 90, 90,
        true);
    building.runtime_slot_index = 3;
    building.under_construction = true;
    obs.units.push_back(building);
    // Visible hostile fighter: target row.
    AiObservedUnit hostile = fighter_unit(5 * 0x1d0, 1, 200, 48, false);
    hostile.runtime_slot_index = 5;
    obs.units.push_back(hostile);
    // Hidden hostile: no row.
    AiObservedUnit hidden = fighter_unit(6 * 0x1d0, 1, 220, 60, false);
    hidden.runtime_slot_index = 6;
    hidden.visible = false;
    obs.units.push_back(hidden);
    // Neutral monster: target row.
    AiObservedUnit monster = fighter_unit(7 * 0x1d0, 8, 100, 200, false);
    monster.runtime_slot_index = 7;
    obs.units.push_back(monster);
    return obs;
}

void test_ai_entity_snapshot_and_wire() {
    AiObservation obs = make_entity_snapshot_observation();
    AiEntityRegistry registry;
    AiEntityRegistryReset(registry);
    // Live twins of the observed units so the registry carries generations.
    UnitMovementUnit live_own = make_entity_live_unit(1, 0,
        obs.units[0].type_id, obs.units[0].type_flags);
    UnitMovementUnit live_worker = make_entity_live_unit(2, 0, 0x10, 1u << 7);
    UnitMovementUnit live_building = make_entity_live_unit(3, 0, 0x80, 0);
    UnitMovementUnit live_hostile = make_entity_live_unit(5, 1,
        obs.units[3].type_id, obs.units[3].type_flags);
    UnitMovementUnit live_hidden = make_entity_live_unit(6, 1, 5, 1u << 5);
    UnitMovementUnit live_monster = make_entity_live_unit(7, 8,
        obs.units[5].type_id, obs.units[5].type_flags);
    std::vector<UnitMovementUnit*> live{&live_own, &live_worker,
        &live_building, &live_hostile, &live_hidden, &live_monster};
    AiEntityRegistryAuditFrame(registry, live);

    UnitMovementMap map = make_entity_legacy_map(8, 8);
    AiEntitySnapshotInput input;
    input.observation = &obs;
    input.registry = &registry;
    input.movement_map = &map;
    const AiEntitySnapshot snapshot = BuildAiEntitySnapshot(input);
    require(!snapshot.contract_error, "snapshot build failed");
    require(snapshot.own.size() == 1 && snapshot.targets.size() == 2,
        "entity row selection did not match plan section 5");
    require(snapshot.own[0].key.runtime_id == 1 * 0x1d0 &&
        snapshot.own[0].key.activation_generation == 1 &&
        snapshot.own[0].control_epoch == 1,
        "own row did not carry the registry key");
    require(snapshot.targets[0].key.runtime_id == 5 * 0x1d0 &&
        snapshot.targets[1].key.runtime_id == 7 * 0x1d0,
        "target rows were not EntityKey-ordered");
    require((snapshot.targets[1].kind_bits & kAiEntityTargetKindNeutral) != 0,
        "neutral monster kind bit missing");
    // Full command mask: KEEP/HOLD/STOP always; point commands via bits
    // 4/5/9 with a nonempty point mask; ATTACK_UNIT via the pair row.
    require(snapshot.own[0].command_mask == 0x7fu,
        "command mask did not open all seven commands");
    require(snapshot.attack_pair_mask.size() == 1 &&
        snapshot.attack_pair_mask[0] == 0x3u,
        "attack pair bitset did not mark both visible targets");

    // Deterministic bytes: permuting the observation unit order must not
    // change the encoded request.
    AiEntityActRequestBody body;
    body.snapshot = snapshot;
    const std::vector<u8> bytes = EncodeAiEntityActRequestPayload(body);
    require(bytes.size() == AiEntityActRequestPayloadBytes(1, 2, false),
        "ACT_REQ payload size formula mismatch");
    require(bytes.size() == 3260u + 207u + 76u * 2u + 4u,
        "ACT_REQ payload size formula constant drifted");
    // SoA layout anchors shared with tools/ai/ranker_entity_contract.py:
    // own arrays start at 3260 (own_id[0] = 0x1d0), the target arrays at
    // 3260+207 (target_id[0] = 0x910) and the pair bitset at the tail (0x3).
    require(bytes[3260] == 0xd0 && bytes[3261] == 0x01 &&
        bytes[3467] == 0x10 && bytes[3468] == 0x09 &&
        bytes[3619] == 0x03,
        "ACT_REQ SoA byte anchors moved");
    AiObservation shuffled = obs;
    std::reverse(shuffled.units.begin(), shuffled.units.end());
    AiEntitySnapshotInput shuffled_input = input;
    shuffled_input.observation = &shuffled;
    AiEntityActRequestBody shuffled_body;
    shuffled_body.snapshot = BuildAiEntitySnapshot(shuffled_input);
    require(EncodeAiEntityActRequestPayload(shuffled_body) == bytes,
        "entity bytes changed with the active-list input order");

    // A worker-only observation yields U=0 (still a valid request).
    AiObservation workers_only = obs;
    workers_only.units.erase(workers_only.units.begin());
    AiEntitySnapshotInput workers_input = input;
    workers_input.observation = &workers_only;
    const AiEntitySnapshot no_own = BuildAiEntitySnapshot(workers_input);
    require(!no_own.contract_error && no_own.own.empty() &&
        no_own.targets.size() == 2,
        "U=0 snapshot was not a normal request");

    // TERMINAL prepends exactly the u32 outcome.
    const std::vector<u8> terminal =
        EncodeAiEntityTerminalPayload(body, 1);
    require(terminal.size() == bytes.size() + 4 && terminal[0] == 1 &&
        std::equal(bytes.begin(), bytes.end(), terminal.begin() + 4),
        "TERMINAL payload did not prepend the outcome word");
}

void test_ai_entity_wire_contract() {
    // CRC32 check value (IEEE reflected).
    const char* check = "123456789";
    require(AiEntityCrc32(reinterpret_cast<const u8*>(check), 9) ==
        0xcbf43926u, "CRC32 does not match the IEEE reflected polynomial");

    AiEntityWireHeader header;
    header.kind = static_cast<u16>(AiEntityWireKind::act_req);
    header.flags = kAiEntityWireFlagMacroDue;
    header.payload_bytes = 3260;
    header.owner = 1;
    header.episode = 37;
    header.frame = 1232;
    header.sequence = 154;
    header.reply_to_sequence = 153;
    header.own_rows = 2;
    header.target_rows = 3;
    header.payload_crc32 = 0x12345678;
    header.entity_policy_version = 21;
    header.macro_policy_version = 8;
    u8 raw[kAiEntityWireHeaderBytes];
    AiEntityWriteWireHeader(header, raw);
    // Golden byte anchors from the plan section 11.1 offset table.
    require(raw[0] == 'R' && raw[1] == 'A' && raw[2] == 'I' && raw[3] == '2',
        "header magic bytes wrong");
    require(raw[4] == 96 && raw[5] == 0 && raw[6] == 2 && raw[7] == 0,
        "header size/protocol bytes wrong");
    require(raw[16] == 'E' && raw[23] == '1',
        "contract id bytes wrong");
    require(raw[24] == 5 && raw[26] == 10 && raw[28] == 1 && raw[30] == 1 &&
        raw[32] == 2 && raw[34] == 1,
        "contract version words wrong");
    require(raw[64] == 0x22 && raw[65] == 0x03 && raw[68] == 80 &&
        raw[72] == 7 && raw[76] == 96,
        "fixed count words wrong");
    AiEntityWireHeader parsed;
    std::string error;
    require(AiEntityParseWireHeader(raw, sizeof raw, parsed, &error),
        "round-trip header parse failed");
    require(parsed.owner == 1 && parsed.episode == 37 &&
        parsed.frame == 1232 && parsed.sequence == 154 &&
        parsed.reply_to_sequence == 153 && parsed.own_rows == 2 &&
        parsed.target_rows == 3 && parsed.payload_crc32 == 0x12345678 &&
        parsed.entity_policy_version == 21 &&
        parsed.macro_policy_version == 8 &&
        parsed.flags == kAiEntityWireFlagMacroDue,
        "header fields did not round-trip");

    // Hard failures: magic, version word, reserved field, both terminal
    // flags, undefined flag bits.
    u8 bad[kAiEntityWireHeaderBytes];
    std::memcpy(bad, raw, sizeof bad);
    bad[0] = 'X';
    require(!AiEntityParseWireHeader(bad, sizeof bad, parsed, &error),
        "bad magic was accepted");
    std::memcpy(bad, raw, sizeof bad);
    bad[26] = 9;   // global feature version
    require(!AiEntityParseWireHeader(bad, sizeof bad, parsed, &error),
        "version mismatch was accepted");
    std::memcpy(bad, raw, sizeof bad);
    bad[92] = 1;
    require(!AiEntityParseWireHeader(bad, sizeof bad, parsed, &error),
        "nonzero reserved field was accepted");
    std::memcpy(bad, raw, sizeof bad);
    bad[10] = kAiEntityWireFlagTerminated | kAiEntityWireFlagTruncated;
    require(!AiEntityParseWireHeader(bad, sizeof bad, parsed, &error),
        "terminated+truncated was accepted");
    std::memcpy(bad, raw, sizeof bad);
    bad[10] = 0x08;
    require(!AiEntityParseWireHeader(bad, sizeof bad, parsed, &error),
        "undefined flag bit was accepted");

    // Reply body round-trip and range validation.
    AiEntityReplyBody reply;
    reply.macro = 0;
    reply.macro_target = -1;
    reply.command = {0, 4};
    reply.point = {-1, -1};
    reply.target = {-1, 0};
    const std::vector<u8> reply_bytes = EncodeAiEntityReplyPayload(reply);
    require(reply_bytes.size() == 8 + 2 * 9,
        "reply payload size wrong");
    AiEntityReplyBody reply_parsed;
    require(DecodeAiEntityReplyPayload(reply_bytes.data(), reply_bytes.size(),
            2, reply_parsed, &error) &&
        reply_parsed.command == reply.command &&
        reply_parsed.point == reply.point &&
        reply_parsed.target == reply.target,
        "reply payload did not round-trip");
    require(!DecodeAiEntityReplyPayload(reply_bytes.data(),
            reply_bytes.size(), 3, reply_parsed, &error),
        "reply row-count mismatch was accepted");
    std::vector<u8> bad_reply = reply_bytes;
    bad_reply[8] = 7;   // command out of range
    require(!DecodeAiEntityReplyPayload(bad_reply.data(), bad_reply.size(), 2,
            reply_parsed, &error),
        "out-of-range command was accepted");

    // Outcome round-trip.
    AiEntityOutcomeBody outcome;
    outcome.macro_result = static_cast<u16>(AiEntityAttemptResult::not_due);
    outcome.entity_result = {
        static_cast<u16>(AiEntityAttemptResult::kept),
        static_cast<u16>(AiEntityAttemptResult::published)};
    outcome.entity_reject_code = {0, 0};
    outcome.trainable_mask = {0x3u};
    const std::vector<u8> outcome_bytes =
        EncodeAiEntityOutcomePayload(outcome);
    AiEntityOutcomeBody outcome_parsed;
    require(DecodeAiEntityOutcomePayload(outcome_bytes.data(),
            outcome_bytes.size(), 2, outcome_parsed, &error) &&
        outcome_parsed.entity_result == outcome.entity_result &&
        outcome_parsed.trainable_mask == outcome.trainable_mask,
        "outcome payload did not round-trip");

    // HELLO owner records: ascending, mask-consistent.
    AiEntityHelloBody hello;
    hello.controlled_owner_mask = (1u << 0) | (1u << 1);
    AiEntityHelloOwnerRecord r0;
    r0.owner = 0;
    r0.frozen_hostile_owner_mask = 1u << 1;
    AiEntityHelloOwnerRecord r1;
    r1.owner = 1;
    r1.frozen_hostile_owner_mask = 1u << 0;
    hello.owners = {r0, r1};
    const std::vector<u8> hello_bytes = EncodeAiEntityHelloPayload(hello);
    require(hello_bytes.size() == 16 + 2 * 48, "hello payload size wrong");
    AiEntityHelloBody hello_parsed;
    require(DecodeAiEntityHelloPayload(hello_bytes.data(), hello_bytes.size(),
            hello_parsed, &error) &&
        hello_parsed.owners.size() == 2 &&
        hello_parsed.owners[1].frozen_hostile_owner_mask == 1u,
        "hello payload did not round-trip");
    AiEntityHelloBody hello_bad = hello;
    std::swap(hello_bad.owners[0], hello_bad.owners[1]);
    const std::vector<u8> hello_bad_bytes =
        EncodeAiEntityHelloPayload(hello_bad);
    require(!DecodeAiEntityHelloPayload(hello_bad_bytes.data(),
            hello_bad_bytes.size(), hello_parsed, &error),
        "non-ascending hello owner records were accepted");

    // Internal semantic enum -> wire category is a fixed switch; unknown
    // kinds encode as EXTERNAL_UNKNOWN only.
    require(AiEntityWireSemanticOrderOf(AiSemanticActionKind::move) ==
            AiEntityWireSemanticOrder::move &&
        AiEntityWireSemanticOrderOf(AiSemanticActionKind::hold_position) ==
            AiEntityWireSemanticOrder::hold &&
        AiEntityWireSemanticOrderOf(AiSemanticActionKind::pickup_move) ==
            AiEntityWireSemanticOrder::external_unknown &&
        AiEntityWireSemanticOrderOf(AiSemanticActionKind::harvest) ==
            AiEntityWireSemanticOrder::external_unknown,
        "semantic-order wire translation drifted");
}


void test_ai_entity_shadow_labels() {
    AiObservation obs = make_entity_snapshot_observation();
    AiEntityRegistry registry;
    AiEntityRegistryReset(registry);
    UnitMovementUnit live_own = make_entity_live_unit(1, 0,
        obs.units[0].type_id, obs.units[0].type_flags);
    UnitMovementUnit live_hostile = make_entity_live_unit(5, 1,
        obs.units[3].type_id, obs.units[3].type_flags);
    UnitMovementUnit live_monster = make_entity_live_unit(7, 8,
        obs.units[5].type_id, obs.units[5].type_flags);
    std::vector<UnitMovementUnit*> live{&live_own, &live_hostile,
        &live_monster};
    AiEntityRegistryAuditFrame(registry, live);
    UnitMovementMap map = make_entity_legacy_map(8, 8);
    AiEntitySnapshotInput input;
    input.observation = &obs;
    input.registry = &registry;
    input.movement_map = &map;
    const AiEntitySnapshot snapshot = BuildAiEntitySnapshot(input);
    require(!snapshot.contract_error && snapshot.own.size() == 1,
        "shadow fixture snapshot broken");

    AiEntityShadowState state;
    const u32 own_id = 1 * 0x1d0;
    const u32 hostile_id = 5 * 0x1d0;

    // First ATTACK_UNIT: ISSUE with the pointer row.
    std::vector<AiEntityShadowDesiredOrder> desired{
        {own_id, AiSemanticActionKind::attack_unit, hostile_id, 200, 48}};
    std::vector<AiEntityShadowLabel> labels =
        BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels.size() == 1 && labels[0].label == kAiEntityShadowIssue &&
        labels[0].command ==
            static_cast<u8>(AiEntityCommand::attack_unit) &&
        labels[0].target == 0 && labels[0].point == -1,
        "first attack desired order was not an ISSUE label");
    // Same desired order at the next tick: KEEP (teacher latch).
    labels = BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels[0].label == kAiEntityShadowKeep && labels[0].target == -1,
        "unchanged attack was not labeled KEEP");
    // No desired order at all: KEEP.
    labels = BuildAiEntityShadowLabels(snapshot, &map, {}, state);
    require(labels[0].label == kAiEntityShadowKeep,
        "absent desired order was not labeled KEEP");
    // A MOVE to the tile-0 center: ISSUE with the exact global token 0.
    desired = {{own_id, AiSemanticActionKind::move, 0, 16, 16}};
    labels = BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels[0].label == kAiEntityShadowIssue &&
        labels[0].command == static_cast<u8>(AiEntityCommand::move) &&
        labels[0].point == 0,
        "move desired order did not resolve to the nearest token");
    // Same MOVE again: KEEP (absolute resolved point matches the latch).
    labels = BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels[0].label == kAiEntityShadowKeep,
        "unchanged move was not labeled KEEP");
    // A point no token approximates within 64px: excluded, not rewritten.
    desired = {{own_id, AiSemanticActionKind::move, 0, 1000, 48}};
    labels = BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels[0].label == kAiEntityShadowExcluded &&
        labels[0].exclude_reason ==
            static_cast<u16>(AiEntityShadowExcludeReason::point_error),
        "out-of-vocabulary point was not excluded with point_error");
    // Unsupported kind (harvest on a fighter): excluded.
    desired = {{own_id, AiSemanticActionKind::harvest, 0, 48, 48}};
    labels = BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels[0].label == kAiEntityShadowExcluded &&
        labels[0].exclude_reason ==
            static_cast<u16>(AiEntityShadowExcludeReason::unsupported_kind),
        "unsupported kind was not excluded");
    // Stale target pointer: excluded.
    desired = {{own_id, AiSemanticActionKind::attack_unit, 0xdead, 0, 0}};
    labels = BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels[0].label == kAiEntityShadowExcluded &&
        labels[0].exclude_reason ==
            static_cast<u16>(AiEntityShadowExcludeReason::stale_target),
        "stale target pointer was not excluded");
    // Excluded rows must not disturb the latch: the old MOVE still KEEPs.
    desired = {{own_id, AiSemanticActionKind::move, 0, 16, 16}};
    labels = BuildAiEntityShadowLabels(snapshot, &map, desired, state);
    require(labels[0].label == kAiEntityShadowKeep,
        "an excluded row corrupted the shadow latch");

    // Record framing: SHD1 magic + size + header + payload + labels.
    AiEntityActRequestBody body;
    body.snapshot = snapshot;
    const std::vector<u8> payload = EncodeAiEntityActRequestPayload(body);
    AiEntityWireHeader header;
    header.kind = static_cast<u16>(AiEntityWireKind::act_req);
    header.own_rows = 1;
    header.target_rows = 2;
    header.payload_bytes = static_cast<u32>(payload.size());
    header.payload_crc32 = AiEntityCrc32(payload.data(), payload.size());
    const std::vector<u8> record =
        EncodeAiEntityShadowRecord(header, payload, labels);
    require(record.size() == 8 + 96 + payload.size() + 4 + 16 &&
        record[0] == 'S' && record[1] == 'H' && record[2] == 'D' &&
        record[3] == '1',
        "shadow record framing wrong");
}


void test_ai_entity_order_latch() {
    // Baseline view: valid source, engine mirrors the order (MATCH), moving.
    AiEntityOrderFrameView view{};
    view.source_alive_active = true;
    view.control_epoch_matches = true;
    view.target_valid = true;
    view.idle = false;
    view.engine_order_match = kAiEntityEngineOrderMatch;

    // ---- AWAITING_APPLY: timers frozen, exact-origin ACK activates ----
    AiEntityActiveOrder order{};
    order.source = {0x1d0, 1};
    order.command = static_cast<u8>(AiEntityCommand::move);
    order.target_x = 500;
    order.target_y = 0;
    order.issued_frame = 100;
    order.status = AiEntityOrderStatus::awaiting_apply;
    order.delivery_seen_frame = 0xffffffffu;
    AiEntityOrderFrameView awaiting = view;
    awaiting.unit_x = 500;   // even standing AT the goal:
    awaiting.idle = true;    // no completion while AWAITING
    awaiting.engine_order_match = kAiEntityEngineOrderDifferent;  // old active Y
    require(AiEntityOrderTrackFrame(order, awaiting, 101) &&
        order.status == AiEntityOrderStatus::awaiting_apply,
        "AWAITING ran completion/mismatch rules");
    // Same-content ACK with a wrong origin must NOT activate (the view's
    // acknowledged_matching models the exact origin+payload comparison).
    awaiting.acknowledged_matching = false;
    awaiting.delivery_origin_seen = true;
    require(AiEntityOrderTrackFrame(order, awaiting, 102) &&
        order.status == AiEntityOrderStatus::awaiting_apply,
        "non-matching ACK activated the order");
    // Exact ACK: ACTIVE, idle/progress baselines seeded at the ACK frame.
    awaiting.acknowledged_matching = true;
    awaiting.unit_x = 40;
    awaiting.unit_y = 8;
    require(AiEntityOrderTrackFrame(order, awaiting, 110) &&
        order.status == AiEntityOrderStatus::active &&
        order.applied_frame == 110 && order.last_progress_x == 40 &&
        order.last_progress_frame == 110 && order.idle_candidate_frames == 0,
        "matching ACK did not seed the ACTIVE baselines");
    // ACK frame itself runs no ACTIVE rule (frame <= applied_frame).
    AiEntityOrderFrameView active_view = view;
    active_view.unit_x = 40;
    active_view.unit_y = 8;
    require(AiEntityOrderTrackFrame(order, active_view, 110) &&
        order.status == AiEntityOrderStatus::active,
        "ACTIVE rules ran on the ACK frame");

    // ---- delivery escape: consumer passed without the exact origin ----
    AiEntityActiveOrder waiting{};
    waiting.command = static_cast<u8>(AiEntityCommand::move);
    waiting.issued_frame = 200;
    waiting.status = AiEntityOrderStatus::awaiting_apply;
    waiting.delivery_seen_frame = 0xffffffffu;
    AiEntityOrderFrameView passed = view;
    passed.consumer_passed_sequence = true;
    passed.delivery_origin_seen = false;
    for (u32 frame = 201; frame <= 208; ++frame) {
        AiEntityOrderTrackFrame(waiting, passed, frame);
    }
    require(waiting.status == AiEntityOrderStatus::interrupted,
        "consumer-passed-without-origin did not escape after 8 frames");

    // ---- absolute 256-frame apply timeout ----
    AiEntityActiveOrder timed{};
    timed.command = static_cast<u8>(AiEntityCommand::move);
    timed.issued_frame = 300;
    timed.status = AiEntityOrderStatus::awaiting_apply;
    timed.delivery_seen_frame = 0xffffffffu;
    require(AiEntityOrderTrackFrame(timed, view, 555) &&
        timed.status == AiEntityOrderStatus::awaiting_apply,
        "absolute timeout fired early");
    require(AiEntityOrderTrackFrame(timed, view, 556) &&
        timed.status == AiEntityOrderStatus::interrupted,
        "absolute 256-frame apply timeout did not fire");

    // ---- ACTIVE: completion, DIFFERENT, CLEARED, MATCH idle, stall ----
    auto make_active_move = []() {
        AiEntityActiveOrder o{};
        o.command = static_cast<u8>(AiEntityCommand::move);
        o.target_x = 500;
        o.target_y = 0;
        o.status = AiEntityOrderStatus::active;
        o.applied_frame = 100;
        o.last_progress_frame = 100;
        return o;
    };
    // MOVE completion: within 32px and idle.
    AiEntityActiveOrder done = make_active_move();
    AiEntityOrderFrameView at_goal = view;
    at_goal.unit_x = 490;
    at_goal.idle = true;
    require(AiEntityOrderTrackFrame(done, at_goal, 120) &&
        done.status == AiEntityOrderStatus::completed,
        "MOVE at the goal did not complete");
    // DIFFERENT payload: immediate INTERRUPTED.
    AiEntityActiveOrder overridden = make_active_move();
    AiEntityOrderFrameView different = view;
    different.engine_order_match = kAiEntityEngineOrderDifferent;
    require(AiEntityOrderTrackFrame(overridden, different, 120) &&
        overridden.status == AiEntityOrderStatus::interrupted,
        "DIFFERENT payload did not interrupt");
    // CLEARED: strictly 4 CONSECUTIVE idle frames.
    AiEntityActiveOrder cleared = make_active_move();
    AiEntityOrderFrameView cleared_idle = view;
    cleared_idle.engine_order_match = kAiEntityEngineOrderCleared;
    cleared_idle.idle = true;
    AiEntityOrderFrameView cleared_busy = cleared_idle;
    cleared_busy.idle = false;
    AiEntityOrderTrackFrame(cleared, cleared_idle, 121);
    AiEntityOrderTrackFrame(cleared, cleared_idle, 122);
    AiEntityOrderTrackFrame(cleared, cleared_idle, 123);
    AiEntityOrderTrackFrame(cleared, cleared_busy, 124);   // reset
    AiEntityOrderTrackFrame(cleared, cleared_idle, 125);
    AiEntityOrderTrackFrame(cleared, cleared_idle, 126);
    AiEntityOrderTrackFrame(cleared, cleared_idle, 127);
    require(cleared.status == AiEntityOrderStatus::active,
        "nonconsecutive idle frames accumulated");
    AiEntityOrderTrackFrame(cleared, cleared_idle, 128);
    require(cleared.status == AiEntityOrderStatus::interrupted,
        "CLEARED + 4 idle frames did not interrupt");
    // MATCH MOVE stall: no 8px progress for 48 progress-required frames;
    // freeze frames (attack recovery) advance the baseline instead.
    AiEntityActiveOrder stalled = make_active_move();
    AiEntityOrderFrameView stuck = view;
    stuck.unit_x = 100;
    AiEntityOrderFrameView frozen = stuck;
    frozen.attack_recovery = 5;
    u32 frame = 101;
    for (u32 i = 0; i < 47; ++i, ++frame) {
        AiEntityOrderTrackFrame(stalled, stuck, frame);
    }
    require(stalled.status == AiEntityOrderStatus::active,
        "stall fired before 48 frames");
    AiEntityOrderTrackFrame(stalled, frozen, frame++);   // freeze resets base
    for (u32 i = 0; i < 47; ++i, ++frame) {
        AiEntityOrderTrackFrame(stalled, stuck, frame);
    }
    require(stalled.status == AiEntityOrderStatus::active,
        "freeze frame did not reset the progress baseline");
    AiEntityOrderTrackFrame(stalled, stuck, frame++);
    require(stalled.status == AiEntityOrderStatus::stalled,
        "48 progress-required frames without 8px did not stall");

    // ---- ATTACK_UNIT: target loss + terminal states stay latched ----
    AiEntityActiveOrder attack{};
    attack.command = static_cast<u8>(AiEntityCommand::attack_unit);
    attack.target = {0x910, 3};
    attack.status = AiEntityOrderStatus::active;
    attack.applied_frame = 100;
    attack.last_progress_frame = 100;
    AiEntityOrderFrameView lost = view;
    lost.target_valid = false;
    require(AiEntityOrderTrackFrame(attack, lost, 130) &&
        attack.status == AiEntityOrderStatus::target_lost,
        "dead target did not close as TARGET_LOST");
    require(AiEntityOrderTrackFrame(attack, view, 131) &&
        attack.status == AiEntityOrderStatus::target_lost,
        "terminal state auto-resumed");
    // Source purge.
    AiEntityOrderFrameView gone = view;
    gone.source_alive_active = false;
    require(!AiEntityOrderTrackFrame(attack, gone, 132),
        "invalid source did not purge the record");

    // ---- decision rows: KEEP / dedupe / satisfied-terminal / re-ISSUE ----
    AiEntityDecisionRowInput keep{};
    keep.command = static_cast<u8>(AiEntityCommand::keep_current_order);
    require(AiEntityEvaluateDecisionRow(nullptr, keep).result ==
            AiEntityAttemptResult::kept &&
        !AiEntityEvaluateDecisionRow(nullptr, keep).needs_packet,
        "KEEP produced a packet");
    AiEntityActiveOrder pending_move = make_active_move();
    pending_move.status = AiEntityOrderStatus::awaiting_apply;
    AiEntityDecisionRowInput same_move{};
    same_move.command = static_cast<u8>(AiEntityCommand::move);
    same_move.point_x = 500;
    same_move.point_y = 0;
    require(AiEntityEvaluateDecisionRow(&pending_move, same_move).result ==
        AiEntityAttemptResult::deduped,
        "same ISSUE against AWAITING was not deduped");
    // Completed MOVE, still inside the 32px radius: suppressed.
    AiEntityActiveOrder done_move = make_active_move();
    done_move.status = AiEntityOrderStatus::completed;
    AiEntityDecisionRowInput near_goal = same_move;
    near_goal.unit_x = 490;
    require(AiEntityEvaluateDecisionRow(&done_move, near_goal).result ==
        AiEntityAttemptResult::deduped,
        "satisfied completed MOVE was re-published");
    // Drifted away from the completed point: a fresh ISSUE again.
    AiEntityDecisionRowInput drifted = same_move;
    drifted.unit_x = 100;
    require(AiEntityEvaluateDecisionRow(&done_move, drifted).needs_packet,
        "unsatisfied completed MOVE stayed suppressed");
    // INTERRUPTED: the same ISSUE is a fresh policy choice.
    AiEntityActiveOrder broken = make_active_move();
    broken.status = AiEntityOrderStatus::interrupted;
    require(AiEntityEvaluateDecisionRow(&broken, same_move).needs_packet,
        "same ISSUE after INTERRUPTED stayed suppressed");
    // A different point is always a new packet.
    AiEntityDecisionRowInput moved = same_move;
    moved.point_x = 132;
    require(AiEntityEvaluateDecisionRow(&pending_move, moved).needs_packet,
        "changed point did not publish");
}

// ===========================================================================
// ENTCMD02 / act3 (docs/AI_PLAY_ENTCMD02_DIRECT_ECONOMY_PLAN.md)
// ===========================================================================

namespace {

// 8x8 map: base nest 0x80 (6x4) at tile (0,0), a worker 0x10 at tile (6,5),
// two berry tiles at (7,6)/(7,7), one visible hostile fighter at tile (6,0).
AiObservation make_entity2_observation() {
    AiObservation obs{};
    obs.simulation_frame = 9600;
    obs.map_width_tiles = 8;
    obs.map_height_tiles = 8;
    obs.local_owner = 0;
    obs.active_owner_mask = 0x3;
    obs.local_relation_mask = 0x1;
    obs.start_x = 48;
    obs.start_y = 48;
    obs.primary_resources = 400;
    obs.population_used = 10;      // supply
    obs.population_limit = 100;
    obs.population_reserved = 7;   // demand
    AiObservedMapTile open{};
    open.passable = true;
    open.explored = true;
    open.visible = true;
    open.buildable = true;
    obs.tiles.assign(64, open);
    for (u32 index : {7u * 8u + 6u, 7u * 8u + 7u, 6u * 8u + 7u}) {
        obs.tiles[index].terrain_flags = 0x100u;
        obs.tiles[index].passable = false;
        obs.tiles[index].buildable = false;
        obs.tiles[index].resource_amount = index == 6u * 8u + 7u ? 1800u : 900u;
    }
    obs.tiles[7u * 8u + 6u].resource_amount = 0;   // berry terrain, harvested out
    AiObservedUnit worker = observed_unit(1 * 0x1d0, 0, 0x10,
        (1u << 4) | (1u << 6) | (1u << 7), 6 * 32 + 16, 5 * 32 + 16, true);
    worker.runtime_slot_index = 1;
    worker.alive = true;
    worker.cargo_capacity = 8;
    obs.units.push_back(worker);
    AiObservedUnit base = observed_unit(2 * 0x1d0, 0, 0x80, 0, 0, 0, true);
    base.runtime_slot_index = 2;
    base.alive = true;
    obs.units.push_back(base);
    AiObservedUnit hostile = fighter_unit(5 * 0x1d0, 1, 6 * 32 + 16, 16, false);
    hostile.runtime_slot_index = 5;
    obs.units.push_back(hostile);
    return obs;
}

GameSessionUnitReferenceTables make_entity2_references() {
    GameSessionUnitReferenceTables tables{};
    UnitTypeSessionDefinition& worker = tables.definitions[0x10];
    worker.present = true;
    worker.primary_reference_count = 2;
    worker.primary_references[0] = 0x82;
    worker.primary_references[1] = 0x80;
    UnitTypeSessionDefinition& base = tables.definitions[0x80];
    base.present = true;
    base.alternate_reference_count = 1;
    base.alternate_references[0] = 0x20;
    base.completion_reference_count = 1;
    base.completion_references[0] = 0x19;
    return tables;
}

bool entity2_unit_catalog(void*, u32, u32 type_id,
    AiEntity2UnitCatalogEntry* out) {
    *out = AiEntity2UnitCatalogEntry{};
    switch (type_id) {
    case 0x10: out->primary_cost = 50; out->population_cost = 1; return true;
    case 0x20: out->primary_cost = 50; out->population_cost = 1; return true;
    case 0x80: out->primary_cost = 500; return true;
    case 0x82: out->primary_cost = 300; return true;
    default: return false;
    }
}

bool entity2_research_catalog(void*, u32, u32 order_id,
    AiEntity2ResearchCatalogEntry* out) {
    if (order_id != 0x19) {
        return false;
    }
    *out = AiEntity2ResearchCatalogEntry{};
    out->next_level = 1;
    out->max_level = 3;
    out->primary_cost = 400;
    return true;
}

AiEntity2Snapshot build_entity2_fixture(AiObservation& obs,
    AiEntityRegistry& registry, UnitMovementMap& map,
    const GameSessionUnitReferenceTables& tables,
    const AiEntity2OrderStore* store = nullptr) {
    AiEntityRegistryReset(registry);
    UnitMovementUnit live_worker = make_entity_live_unit(1, 0, 0x10,
        (1u << 4) | (1u << 6) | (1u << 7));
    UnitMovementUnit live_base = make_entity_live_unit(2, 0, 0x80, 0);
    UnitMovementUnit live_hostile = make_entity_live_unit(5, 1, 5, 1u << 5);
    std::vector<UnitMovementUnit*> live{&live_worker, &live_base, &live_hostile};
    AiEntityRegistryAuditFrame(registry, live);
    AiEntity2SnapshotInput input;
    input.observation = &obs;
    input.registry = &registry;
    input.movement_map = &map;
    input.catalog.unit_references = &tables;
    input.catalog.unit_catalog = entity2_unit_catalog;
    input.catalog.research_catalog = entity2_research_catalog;
    input.orders = store;
    return BuildAiEntity2Snapshot(input);
}

}  // namespace

void test_ai_entity2_wire_contract() {
    AiEntity2WireHeader header;
    header.kind = static_cast<u16>(AiEntityWireKind::act_req);
    header.payload_bytes = 3336;
    header.owner = 1;
    header.episode = 37;
    header.frame = 1232;
    header.sequence = 154;
    header.reply_to_sequence = 153;
    header.own_rows = 2;
    header.target_rows = 3;
    header.resource_rows = 4;
    header.build_rows = 5;
    header.produce_rows = 6;
    header.research_rows = 7;
    header.payload_crc32 = 0x12345678;
    header.policy_version = 21;
    u8 raw[kAiEntity2WireHeaderBytes];
    AiEntity2WriteWireHeader(header, raw);
    // Golden anchors shared with tools/ai/ranker_entity2_contract.py.
    require(raw[0] == 'R' && raw[1] == 'A' && raw[2] == 'I' && raw[3] == '3',
        "entity2 header magic wrong");
    require(raw[4] == 128 && raw[6] == 3, "entity2 header size/protocol wrong");
    require(raw[16] == 'E' && raw[23] == '2', "entity2 contract id wrong");
    require(raw[24] == 5 && raw[26] == 10 && raw[28] == 3 && raw[30] == 5 &&
        raw[32] == 3 && raw[34] == 1 && raw[36] == 1 && raw[38] == 3,
        "entity2 version tuple wrong");
    require(raw[40] == 1 && raw[44] == 37 && raw[52] == 154 && raw[60] == 2 &&
        raw[64] == 3 && raw[68] == 4 && raw[72] == 5 && raw[76] == 6 &&
        raw[80] == 7, "entity2 header count words wrong");
    require(raw[84] == 0x22 && raw[85] == 0x03 &&
        raw[88] == kAiEntity2PolicyCommandCount &&
        raw[92] == 96 && raw[96] == 0x78 && raw[100] == 21,
        "entity2 fixed count / crc / policy words wrong");
    for (u32 i = 104; i < 128; ++i) {
        require(raw[i] == 0, "entity2 reserved bytes nonzero");
    }
    AiEntity2WireHeader parsed;
    std::string error;
    require(AiEntity2ParseWireHeader(raw, sizeof raw, parsed, &error),
        "entity2 header round trip failed");
    require(parsed.build_rows == 5 && parsed.research_rows == 7 &&
        parsed.policy_version == 21 && parsed.candidate_rows() == 22,
        "entity2 header fields did not round-trip");
    u8 bad[kAiEntity2WireHeaderBytes];
    std::memcpy(bad, raw, sizeof bad);
    bad[3] = '2';   // RAI2
    require(!AiEntity2ParseWireHeader(bad, sizeof bad, parsed, &error),
        "RAI2 magic accepted by the ENTCMD02 parser");
    std::memcpy(bad, raw, sizeof bad);
    bad[23] = '1';  // ENTCMD01
    require(!AiEntity2ParseWireHeader(bad, sizeof bad, parsed, &error),
        "ENTCMD01 contract id accepted");
    std::memcpy(bad, raw, sizeof bad);
    bad[104] = 1;
    require(!AiEntity2ParseWireHeader(bad, sizeof bad, parsed, &error),
        "nonzero reserved word accepted");
    std::memcpy(bad, raw, sizeof bad);
    bad[10] = kAiEntity2WireFlagTerminated | kAiEntity2WireFlagTruncated;
    require(!AiEntity2ParseWireHeader(bad, sizeof bad, parsed, &error),
        "terminated+truncated accepted");
    std::memcpy(bad, raw, sizeof bad);
    bad[88] = 7;
    require(!AiEntity2ParseWireHeader(bad, sizeof bad, parsed, &error),
        "wrong command count accepted");
    // ENTCMD01 parser must reject RAI3 too.
    AiEntityWireHeader old_parsed;
    require(!AiEntityParseWireHeader(raw, kAiEntityWireHeaderBytes, old_parsed,
            &error), "ENTCMD01 parser accepted a RAI3 header");

    // Payload size formula anchors (plan 12.2 + slot extension: prefix 3624,
    // row 329).
    require(AiEntity2ActRequestPayloadBytes(0, 0, 0, false) == 3624 &&
        AiEntity2ActRequestPayloadBytes(1, 0, 0, false) == 3624 + 329 &&
        AiEntity2ActRequestPayloadBytes(1, 2, 5, false) ==
            3624 + 329 + 152 + 320 + 8 &&
        AiEntity2ActRequestPayloadBytes(2, 33, 65, true) ==
            4 + 3624 + 658 + 76 * 33 + 64 * 65 + 4 * 2 * (2 + 3) &&
        AiEntity2ActRequestPayloadBytes(2049, 0, 0, false) == 0,
        "entity2 payload size formula drifted");

    // Reply / outcome / hello round trips and domain rejects.
    AiEntity2WireHeader ctx;
    ctx.own_rows = 2;
    ctx.target_rows = 1;
    ctx.resource_rows = 1;
    ctx.build_rows = 2;
    ctx.produce_rows = 1;
    ctx.research_rows = 1;
    AiEntity2ReplyBody reply;
    reply.command = {static_cast<u8>(AiEntity2PolicyCommand::build), 0};
    reply.argument = {1, -1};
    reply.assign = {0, 2};
    reply.slot_command[0] = static_cast<u8>(AiEntity2SlotCommand::attack_move);
    reply.slot_cell[0] = 58;
    const std::vector<u8> reply_bytes = EncodeAiEntity2ReplyPayload(reply);
    require(reply_bytes.size() == 2 * 6 + 4 + 16, "entity2 reply size wrong");
    AiEntity2ReplyBody reply_parsed;
    require(DecodeAiEntity2ReplyPayload(reply_bytes.data(), reply_bytes.size(),
            ctx, reply_parsed, &error) &&
        reply_parsed.command == reply.command &&
        reply_parsed.argument == reply.argument &&
        reply_parsed.assign == reply.assign &&
        reply_parsed.slot_command == reply.slot_command &&
        reply_parsed.slot_cell == reply.slot_cell,
        "entity2 reply did not round-trip");
    {
        AiEntity2ReplyBody bad_slot = reply;
        bad_slot.slot_cell[1] = 3;   // KEEP with a cell
        const std::vector<u8> bytes = EncodeAiEntity2ReplyPayload(bad_slot);
        require(!DecodeAiEntity2ReplyPayload(bytes.data(), bytes.size(), ctx,
                reply_parsed, &error), "slot cell on KEEP accepted");
        bad_slot = reply;
        bad_slot.slot_command[2] = static_cast<u8>(kAiEntity2SlotCommandCount);
        const std::vector<u8> bytes2 = EncodeAiEntity2ReplyPayload(bad_slot);
        require(!DecodeAiEntity2ReplyPayload(bytes2.data(), bytes2.size(), ctx,
                reply_parsed, &error), "out-of-range slot command accepted");
        bad_slot = reply;
        bad_slot.assign = {5, 0};
        const std::vector<u8> bytes3 = EncodeAiEntity2ReplyPayload(bad_slot);
        require(!DecodeAiEntity2ReplyPayload(bytes3.data(), bytes3.size(), ctx,
                reply_parsed, &error), "assign 5 accepted");
    }
    for (const auto& bad_reply : std::vector<std::pair<u8, i32>>{
            {0, 0}, {1, 96}, {4, 1}, {8, 5}, {11, -1}, {7, -1}}) {
        AiEntity2ReplyBody r;
        r.command = {bad_reply.first, 0};
        r.argument = {bad_reply.second, -1};
        const std::vector<u8> bytes = EncodeAiEntity2ReplyPayload(r);
        require(!DecodeAiEntity2ReplyPayload(bytes.data(), bytes.size(), ctx,
                reply_parsed, &error), "out-of-domain entity2 reply accepted");
    }
    AiEntity2OutcomeBody outcome;
    outcome.result = {static_cast<u16>(AiEntity2AttemptResult::published),
        static_cast<u16>(AiEntity2AttemptResult::kept)};
    outcome.reject_code = {0, 0};
    outcome.trainable_mask = {0x1u};
    outcome.slot_result[0] = static_cast<u16>(AiEntity2AttemptResult::published);
    outcome.slot_trainable_bits = 0xfu;
    outcome.assign_trainable_mask = {0x2u};
    const std::vector<u8> outcome_bytes = EncodeAiEntity2OutcomePayload(outcome);
    require(outcome_bytes.size() == 12 + 8 + 8 + 4 + 4, "entity2 outcome size wrong");
    AiEntity2OutcomeBody outcome_parsed;
    require(DecodeAiEntity2OutcomePayload(outcome_bytes.data(),
            outcome_bytes.size(), 2, outcome_parsed, &error) &&
        outcome_parsed.result == outcome.result &&
        outcome_parsed.trainable_mask == outcome.trainable_mask &&
        outcome_parsed.slot_result == outcome.slot_result &&
        outcome_parsed.slot_trainable_bits == 0xfu &&
        outcome_parsed.assign_trainable_mask == outcome.assign_trainable_mask,
        "entity2 outcome did not round-trip");
    AiEntity2OutcomeBody bad_outcome = outcome;
    bad_outcome.result[0] = static_cast<u16>(AiEntity2AttemptResult::rejected_conflict);
    bad_outcome.reject_code[0] = static_cast<u16>(AiEntity2RejectCode::site_conflict);
    const std::vector<u8> bad_outcome_bytes = EncodeAiEntity2OutcomePayload(bad_outcome);
    require(!DecodeAiEntity2OutcomePayload(bad_outcome_bytes.data(),
            bad_outcome_bytes.size(), 2, outcome_parsed, &error),
        "trainable bit on a failed entity2 row accepted");
    AiEntity2HelloBody hello;
    hello.controlled_owner_mask = 0x3;
    AiEntity2HelloOwnerRecord r0;
    r0.owner = 0;
    r0.frozen_hostile_owner_mask = 2;
    AiEntity2HelloOwnerRecord r1;
    r1.owner = 1;
    r1.frozen_hostile_owner_mask = 1;
    hello.owners = {r0, r1};
    const std::vector<u8> hello_bytes = EncodeAiEntity2HelloPayload(hello);
    require(hello_bytes.size() == 16 + 2 * 48, "entity2 hello size wrong");
    AiEntity2HelloBody hello_parsed;
    require(DecodeAiEntity2HelloPayload(hello_bytes.data(), hello_bytes.size(),
            hello_parsed, &error) && hello_parsed.owners.size() == 2 &&
        hello_parsed.owners[1].frozen_hostile_owner_mask == 1,
        "entity2 hello did not round-trip");

    // Semantic vocabulary v3: economy kinds map, return_cargo does not.
    require(AiEntity2WireSemanticOrderOf(AiSemanticActionKind::harvest) ==
            AiEntity2WireSemanticOrder::harvest &&
        AiEntity2WireSemanticOrderOf(AiSemanticActionKind::research) ==
            AiEntity2WireSemanticOrder::research_upgrade &&
        AiEntity2WireSemanticOrderOf(AiSemanticActionKind::return_cargo) ==
            AiEntity2WireSemanticOrder::external_unknown,
        "entity2 semantic vocabulary drifted");
}

void test_ai_entity2_snapshot_and_ledger() {
    AiObservation obs = make_entity2_observation();
    AiEntityRegistry registry;
    UnitMovementMap map = make_entity_legacy_map(8, 8);
    const GameSessionUnitReferenceTables tables = make_entity2_references();
    const AiEntity2Snapshot snapshot =
        build_entity2_fixture(obs, registry, map, tables);
    require(!snapshot.contract_error, snapshot.error.c_str());
    require(snapshot.own.size() == 2 && snapshot.targets.size() == 1,
        "entity2 row selection: every controlled unit is a row");
    require(snapshot.own[0].role == static_cast<u8>(AiEntity2Role::worker) &&
        snapshot.own[1].role == static_cast<u8>(AiEntity2Role::building),
        "entity2 roles wrong");
    require(snapshot.spendable_primary == 400 &&
        snapshot.spendable_population == 3,
        "entity2 spendable budget wrong");
    // R: two harvestable berries (the harvested-out berry tile is not one).
    require(snapshot.resource_rows == 2 &&
        snapshot.candidates[0].key == 6 * 8 + 7 &&
        snapshot.candidates[1].key == 7 * 8 + 7 &&
        snapshot.candidates[0].raw1 == 1800,
        "entity2 resource candidates wrong");
    // B: 0x82 sites below the nest (rows 4..6), canonical (type,ty,tx) order;
    // expansion (0x80) has no undeveloped cluster here.
    require(snapshot.build_rows > 0, "entity2 build candidates missing");
    for (u32 c = snapshot.resource_rows;
        c + 1 < snapshot.resource_rows + snapshot.build_rows; ++c) {
        require(snapshot.candidates[c].key < snapshot.candidates[c + 1].key,
            "entity2 build candidates not in canonical order");
    }
    const AiEntity2Candidate& first_site = snapshot.candidates[snapshot.resource_rows];
    require(first_site.object_id == 0x82 && first_site.raw0 == 300 &&
        first_site.footprint_width() == 3 && first_site.footprint_height() == 2 &&
        (first_site.flags & kAiEntity2CandidateFlagExplored) != 0 &&
        (first_site.y >> 5) >= 4,
        "entity2 build candidate fields wrong");
    // P / Q.
    const u32 produce_row = snapshot.resource_rows + snapshot.build_rows;
    const u32 research_row = produce_row + snapshot.produce_rows;
    require(snapshot.produce_rows == 1 && snapshot.research_rows == 1 &&
        snapshot.candidates[produce_row].object_id == 0x20 &&
        snapshot.candidates[produce_row].raw2 == 1 &&
        snapshot.candidates[research_row].key == ((0x19ull << 32) | 1) &&
        snapshot.candidates[research_row].raw0 == 400,
        "entity2 produce/research candidates wrong");
    // Pair masks: worker -> every resource + every site, base -> P + Q.
    require(snapshot.economy_pair_bit(0, 0) && snapshot.economy_pair_bit(0, 1) &&
        snapshot.economy_pair_bit(0, snapshot.resource_rows) &&
        !snapshot.economy_pair_bit(0, produce_row) &&
        snapshot.economy_pair_bit(1, produce_row) &&
        snapshot.economy_pair_bit(1, research_row) &&
        !snapshot.economy_pair_bit(1, 0),
        "entity2 economy pair mask wrong");
    // Action v4 role table: an idle calm worker offers KEEP/HARVEST/BUILD
    // only (no point moves, no STOP); the building PRODUCE/RESEARCH.
    require(snapshot.own[0].command_mask ==
            ((1u << 0) | (1u << static_cast<u32>(AiEntity2PolicyCommand::harvest)) |
                (1u << static_cast<u32>(AiEntity2PolicyCommand::build))) &&
        snapshot.own[1].command_mask ==
            ((1u << 0) | (1u << static_cast<u32>(AiEntity2PolicyCommand::produce_unit)) |
                (1u << static_cast<u32>(AiEntity2PolicyCommand::research_upgrade))),
        "entity2 command masks wrong");
    require(snapshot.own_appendix[0].capability_bits ==
            (kAiEntity2CapMove | kAiEntity2CapHold | kAiEntity2CapHarvest |
                kAiEntity2CapBuild) &&
        snapshot.own_appendix[1].capability_bits ==
            (kAiEntity2CapProduce | kAiEntity2CapResearch) &&
        snapshot.own_appendix[1].queued_production_type_id == kAiEntity2TypeSentinel &&
        snapshot.own_appendix[0].source_state_bits == kAiEntity2StateCompleted,
        "entity2 appendix wrong");
    require((snapshot.candidates[produce_row].flags &
            kAiEntity2CandidateFlagAnySourceAvailable) != 0 &&
        snapshot.candidates[produce_row].feature[6] == 1.0f,
        "entity2 any-source availability flag wrong");
    require(snapshot.economy_reward_material[0] == 400 &&
        snapshot.economy_reward_material[2] == 50 &&
        snapshot.economy_reward_material[4] == 500 &&
        snapshot.economy_reward_material[9] == 10,
        "entity2 reward material wrong");

    // Wire round trip of the whole snapshot.
    AiEntity2ActRequestBody body;
    body.snapshot = snapshot;
    body.cumulative_losses = {1, 2, 3, 4};
    const std::vector<u8> bytes = EncodeAiEntity2ActRequestPayload(body);
    require(bytes.size() == AiEntity2ActRequestPayloadBytes(2, 1,
            snapshot.candidate_rows(), false),
        "entity2 ACT_REQ size formula mismatch");
    AiEntity2WireHeader header;
    header.own_rows = 2;
    header.target_rows = 1;
    header.resource_rows = snapshot.resource_rows;
    header.build_rows = snapshot.build_rows;
    header.produce_rows = snapshot.produce_rows;
    header.research_rows = snapshot.research_rows;
    header.owner = 0;
    header.frame = 9600;
    AiEntity2ActRequestBody decoded;
    std::string error;
    require(DecodeAiEntity2ActRequestPayload(bytes.data(), bytes.size(), header,
            false, decoded, nullptr, &error), error.c_str());
    require(decoded.snapshot.candidates.size() == snapshot.candidates.size() &&
        decoded.snapshot.candidates[research_row].key ==
            snapshot.candidates[research_row].key &&
        decoded.snapshot.economy_pair_mask == snapshot.economy_pair_mask &&
        decoded.snapshot.own_appendix[1].capability_bits ==
            snapshot.own_appendix[1].capability_bits &&
        decoded.cumulative_losses[3] == 4 &&
        decoded.snapshot.spendable_primary == 400,
        "entity2 ACT_REQ did not round-trip");
    require(EncodeAiEntity2ActRequestPayload(decoded) == bytes,
        "entity2 ACT_REQ re-encode is not byte-identical");
    const std::vector<u8> terminal = EncodeAiEntity2TerminalPayload(body, 1);
    require(terminal.size() == bytes.size() + 4 && terminal[0] == 1,
        "entity2 TERMINAL prefix wrong");

    // Ledger replay: BUILD (300) at the worker leaves 100 -> the base can
    // still PRODUCE (50) but no longer RESEARCH (400).
    const u32 site = snapshot.resource_rows;
    std::vector<u8> commands{static_cast<u8>(AiEntity2PolicyCommand::build), 0};
    std::vector<i32> arguments{static_cast<i32>(site), -1};
    AiEntity2LedgerReplay replay = AiEntity2ReplayLedger(snapshot, commands,
        arguments);
    require(replay.remaining_budget[0] == (std::array<u32, 3>{400, 0, 3}) &&
        replay.remaining_budget[1] == (std::array<u32, 3>{100, 0, 3}),
        "entity2 ledger budget replay wrong");
    require(replay.dynamic_command_mask[0] == snapshot.own[0].command_mask &&
        replay.dynamic_command_mask[1] ==
            ((1u << 0) | (1u << static_cast<u32>(AiEntity2PolicyCommand::produce_unit))) &&
        replay.choice_legal[0] == 1,
        "entity2 dynamic command mask wrong");
    const u32 words = snapshot.economy_words_per_row();
    require((replay.dynamic_economy_pair_mask[words + (produce_row >> 5)] >>
            (produce_row & 31u) & 1u) == 1u &&
        (replay.dynamic_economy_pair_mask[words + (research_row >> 5)] >>
            (research_row & 31u) & 1u) == 0u,
        "entity2 dynamic pair mask wrong");
    require(AiEntity2RowStochastic(replay.dynamic_command_mask[1]) &&
        !AiEntity2RowStochastic(1u), "entity2 stochastic-row rule wrong");
    // An illegal choice consumes nothing; PREFIX_UNRESOLVED zeroes the rest.
    commands[0] = static_cast<u8>(AiEntity2PolicyCommand::build);
    arguments[0] = static_cast<i32>(produce_row);
    replay = AiEntity2ReplayLedger(snapshot, commands, arguments);
    require(replay.choice_legal[0] == 0 &&
        replay.remaining_budget[1] == (std::array<u32, 3>{400, 0, 3}),
        "entity2 illegal choice consumed budget");
    replay = AiEntity2ReplayLedger(snapshot, commands, arguments, 0);
    require(replay.dynamic_command_mask[1] == 1u &&
        replay.dynamic_economy_pair_mask[words] == 0,
        "entity2 unresolved prefix did not zero the economy masks");
    // Site overlap: a second worker choosing an overlapping site conflicts.
    {
        AiEntity2Ledger ledger;
        AiEntity2LedgerInit(ledger, snapshot);
        AiEntity2LedgerReserve(ledger, snapshot, AiEntity2Command::build,
            static_cast<i32>(site));
        require(!AiEntity2LedgerCandidateAvailable(ledger, snapshot, site) &&
            AiEntity2LedgerConflictOf(ledger, snapshot, site) ==
                AiEntity2RejectCode::site_conflict,
            "entity2 same-site reservation not detected");
        require(AiEntity2LedgerConflictOf(ledger, snapshot, research_row) ==
                AiEntity2RejectCode::resource_conflict,
            "entity2 resource conflict code wrong");
    }

    // A worker with an active HARVEST latch is KEEP-only, and its candidate
    // row is exposed for attention.
    AiEntity2OrderStore store;
    AiEntity2EconomyOrder latch;
    latch.source = snapshot.own[0].key;
    latch.controller_owner = 0;
    latch.control_epoch = 1;
    latch.command = AiEntity2Command::harvest;
    latch.candidate_kind = static_cast<u8>(AiEntity2CandidateKind::resource);
    latch.candidate_key = 7 * 8 + 7;
    latch.object_id = 7 * 8 + 7;
    latch.x = 7 * 32 + 16;
    latch.y = 7 * 32 + 16;
    latch.status = AiEntityOrderStatus::active;
    latch.issued_frame = 9500;
    store.economy[AiEntityPackKey(latch.source)] = latch;
    const AiEntity2Snapshot latched =
        build_entity2_fixture(obs, registry, map, tables, &store);
    // A harvesting worker keeps BUILD (carrying or not); no STOP in the
    // policy vocabulary (action v4).
    require(!latched.contract_error &&
        latched.own[0].command_mask ==
            ((1u << 0) | (1u << static_cast<u32>(AiEntity2PolicyCommand::build))) &&
        latched.own_appendix[0].active_economy_candidate_row == 1 &&
        latched.own[0].semantic_order ==
            static_cast<u8>(AiEntity2WireSemanticOrder::harvest) &&
        latched.own[0].order_status ==
            static_cast<u8>(AiEntityOrderStatus::active) &&
        (latched.own_appendix[0].source_state_bits &
            kAiEntity2StateActiveEconomyOrder) != 0,
        "entity2 economy latch not reflected in the row");
    // Permuting the observation must not change the bytes.
    AiObservation shuffled = obs;
    std::reverse(shuffled.units.begin(), shuffled.units.end());
    const AiEntity2Snapshot shuffled_snapshot =
        build_entity2_fixture(shuffled, registry, map, tables);
    AiEntity2ActRequestBody shuffled_body;
    shuffled_body.snapshot = shuffled_snapshot;
    shuffled_body.cumulative_losses = {1, 2, 3, 4};
    require(EncodeAiEntity2ActRequestPayload(shuffled_body) == bytes,
        "entity2 bytes changed with the observation unit order");
}

void test_ai_entity2_economy_tracking() {
    // HARVEST: awaiting -> active on ACK; the automatic return / deposit
    // states stay ACTIVE; depleted + not carrying + out of family = COMPLETED.
    AiEntity2EconomyOrder order;
    order.command = AiEntity2Command::harvest;
    order.issued_frame = 100;
    AiEntity2EconomyOrderFrameView view;
    view.source_alive_active = true;
    view.control_epoch_matches = true;
    view.command_base_state = 0x28;
    require(AiEntity2TrackEconomyOrderFrame(order, view, 101) &&
        order.status == AiEntityOrderStatus::awaiting_apply,
        "harvest order activated without an ACK");
    view.acknowledged_matching = true;
    require(AiEntity2TrackEconomyOrderFrame(order, view, 102) &&
        order.status == AiEntityOrderStatus::active,
        "harvest order did not activate on the ACK");
    view.acknowledged_matching = false;
    for (u32 state : {0x29u, 0x2au, 0x2bu, 0x2cu}) {
        view.command_base_state = state;
        require(AiEntity2TrackEconomyOrderFrame(order, view, 103 + state) &&
            order.status == AiEntityOrderStatus::active,
            "harvest family state interrupted the order");
    }
    view.command_base_state = 0;
    view.carrying = true;
    view.resource_depleted = true;
    for (u32 f = 200; f < 210; ++f) {
        require(AiEntity2TrackEconomyOrderFrame(order, view, f) &&
            order.status == AiEntityOrderStatus::active,
            "carrying cargo did not keep the harvest order active");
    }
    view.carrying = false;
    for (u32 f = 210; f < 213; ++f) {
        AiEntity2TrackEconomyOrderFrame(order, view, f);
    }
    require(order.status == AiEntityOrderStatus::active,
        "harvest order closed before four idle frames");
    AiEntity2TrackEconomyOrderFrame(order, view, 213);
    require(order.status == AiEntityOrderStatus::completed,
        "depleted harvest order did not complete");
    // Source death purges.
    view.source_alive_active = false;
    require(!AiEntity2TrackEconomyOrderFrame(order, view, 214),
        "dead source kept its economy order");

    // BUILD: walk -> spawned (claims released) -> completed.
    AiEntity2EconomyOrder build;
    build.command = AiEntity2Command::build;
    build.issued_frame = 300;
    build.cost_claimed = true;
    build.site_claimed = true;
    AiEntity2EconomyOrderFrameView bview;
    bview.source_alive_active = true;
    bview.control_epoch_matches = true;
    bview.acknowledged_matching = true;
    bview.command_base_state = 0x23;
    AiEntity2TrackEconomyOrderFrame(build, bview, 301);
    bview.acknowledged_matching = false;
    bview.command_base_state = 0x25;
    require(AiEntity2TrackEconomyOrderFrame(build, bview, 302) &&
        build.status == AiEntityOrderStatus::active && build.cost_claimed,
        "build approach did not keep the claims");
    bview.command_base_state = 0;
    bview.spawned_present = true;
    bview.spawned_key = AiEntityKey{0x900, 3};
    require(AiEntity2TrackEconomyOrderFrame(build, bview, 303) &&
        build.status == AiEntityOrderStatus::active && !build.cost_claimed &&
        !build.site_claimed && build.spawned_building.runtime_id == 0x900,
        "spawned structure did not release the claims");
    bview.spawned_completed = true;
    AiEntity2TrackEconomyOrderFrame(build, bview, 304);
    require(build.status == AiEntityOrderStatus::completed,
        "completed structure did not complete the build order");
    // BUILD interrupted when the walk ends without a spawn.
    AiEntity2EconomyOrder lost = build;
    lost.status = AiEntityOrderStatus::active;
    lost.applied_frame = 300;
    lost.spawned_building = AiEntityKey{};
    lost.cost_claimed = true;
    bview.spawned_present = false;
    bview.spawned_completed = false;
    for (u32 f = 305; f < 309; ++f) {
        AiEntity2TrackEconomyOrderFrame(lost, bview, f);
    }
    require(lost.status == AiEntityOrderStatus::interrupted && !lost.cost_claimed,
        "abandoned build walk did not interrupt / release");

    // PRODUCE event: awaiting -> queued (resource+queue claims released) ->
    // completed when it leaves the queue; handler-missing bounded timeout.
    AiEntity2EconomyEvent event;
    event.command = AiEntity2Command::produce_unit;
    event.issued_frame = 400;
    event.resource_claimed = true;
    event.population_claimed = true;
    event.queue_claimed = true;
    AiEntity2EventFrameView eview;
    eview.source_alive_active = true;
    eview.control_epoch_matches = true;
    eview.origin_in_deferred = true;
    require(AiEntity2TrackEventFrame(event, eview, 401) &&
        event.status == AiEntity2EventStatus::engine_queued &&
        !event.resource_claimed && !event.queue_claimed && event.population_claimed,
        "queued produce event kept the resource/queue claim");
    eview.origin_in_deferred = false;
    eview.origin_in_active = true;
    eview.population_reserved_by_engine = true;
    AiEntity2TrackEventFrame(event, eview, 402);
    require(!event.population_claimed, "active produce kept the population claim");
    eview.origin_in_active = false;
    AiEntity2TrackEventFrame(event, eview, 403);
    require(event.status == AiEntity2EventStatus::completed,
        "produce event leaving the queue did not complete");
    AiEntity2EconomyEvent missing;
    missing.command = AiEntity2Command::research_upgrade;
    missing.issued_frame = 500;
    missing.research_claimed = true;
    AiEntity2EventFrameView mview;
    mview.source_alive_active = true;
    mview.control_epoch_matches = true;
    mview.consumer_passed_sequence = true;
    for (u32 f = 501; f < 508; ++f) {
        AiEntity2TrackEventFrame(missing, mview, f);
    }
    require(missing.status == AiEntity2EventStatus::awaiting_apply,
        "research event rejected before eight missing frames");
    AiEntity2TrackEventFrame(missing, mview, 508);
    require(missing.status == AiEntity2EventStatus::handler_rejected &&
        !missing.research_claimed,
        "handler-missing research event not rejected / released");
    AiEntity2EconomyEvent done;
    done.command = AiEntity2Command::research_upgrade;
    done.level_at_issue = 1;
    done.issued_frame = 600;
    AiEntity2EventFrameView dview;
    dview.source_alive_active = true;
    dview.control_epoch_matches = true;
    dview.owner_research_level = 2;
    AiEntity2TrackEventFrame(done, dview, 601);
    require(done.status == AiEntity2EventStatus::completed,
        "level increase did not complete the research event");

    // Decision rows: HARVEST/BUILD dedupe against the latch, PRODUCE never,
    // RESEARCH against an awaiting event of the same order.
    AiEntity2OrderStore store;
    AiEntityKey source{0x1d0, 1};
    AiEntity2EconomyOrder latch;
    latch.source = source;
    latch.command = AiEntity2Command::harvest;
    latch.candidate_kind = 0;
    latch.candidate_key = 55;
    latch.status = AiEntityOrderStatus::active;
    store.economy[AiEntityPackKey(source)] = latch;
    AiEntity2EconomyDecisionInput same;
    same.command = AiEntity2Command::harvest;
    same.candidate_kind = 0;
    same.candidate_key = 55;
    require(AiEntity2EvaluateEconomyRow(store, source, same).result ==
            AiEntity2AttemptResult::deduped,
        "same harvest ISSUE was not deduped");
    same.candidate_key = 56;
    require(AiEntity2EvaluateEconomyRow(store, source, same).needs_packet,
        "different resource was deduped");
    AiEntity2EconomyEvent awaiting;
    awaiting.source = source;
    awaiting.command = AiEntity2Command::research_upgrade;
    awaiting.object_id = 0x19;
    store.events.push_back(awaiting);
    AiEntity2EconomyDecisionInput research;
    research.command = AiEntity2Command::research_upgrade;
    research.object_id = 0x19;
    require(AiEntity2EvaluateEconomyRow(store, source, research).result ==
            AiEntity2AttemptResult::deduped,
        "research re-issue while awaiting was not deduped");
    AiEntity2EconomyDecisionInput produce;
    produce.command = AiEntity2Command::produce_unit;
    produce.object_id = 0x20;
    require(AiEntity2EvaluateEconomyRow(store, source, produce).needs_packet,
        "produce enqueue was deduped");
}

void test_ai_entity2_shadow_labels() {
    AiObservation obs = make_entity2_observation();
    AiEntityRegistry registry;
    UnitMovementMap map = make_entity_legacy_map(8, 8);
    const GameSessionUnitReferenceTables tables = make_entity2_references();
    const AiEntity2Snapshot snapshot =
        build_entity2_fixture(obs, registry, map, tables);
    require(!snapshot.contract_error, snapshot.error.c_str());
    const AiEntity2Candidate& site = snapshot.candidates[snapshot.resource_rows];
    const u32 produce_row = snapshot.resource_rows + snapshot.build_rows;
    const u32 research_row = produce_row + snapshot.produce_rows;

    // Teacher: worker builds 0x82 at the first site, base researches 0x19.
    std::vector<AiEntity2ShadowDesiredOrder> desired;
    desired.push_back({0x1d0, AiSemanticActionKind::build, 0, site.x, site.y, 0x82});
    desired.push_back({2 * 0x1d0, AiSemanticActionKind::research, 0, 0, 0, 0x19});
    AiEntity2ShadowState state;
    AiEntity2LedgerReplay replay;
    std::vector<AiEntity2ShadowLabel> labels = BuildAiEntity2ShadowLabels(
        snapshot, &map, desired, state, replay);
    require(labels.size() == 2 && labels[0].label == kAiEntityShadowIssue &&
        labels[0].command == static_cast<u8>(AiEntity2PolicyCommand::build) &&
        labels[0].argument == static_cast<i32>(snapshot.resource_rows),
        "shadow BUILD label wrong");
    // The research (400) no longer fits after the build (300): the teacher
    // event consumed budget we cannot account for, so that row (and every
    // later economy row) is PREFIX_UNRESOLVED (plan 15.1).
    require(labels[1].label == kAiEntityShadowExcluded &&
        labels[1].exclude_reason ==
            static_cast<u16>(AiEntity2ShadowExcludeReason::prefix_unresolved),
        "shadow research label after the build was not PREFIX_UNRESOLVED");
    require(replay.remaining_budget[1] == (std::array<u32, 3>{100, 0, 3}),
        "shadow replay budget wrong");
    // Same teacher order next tick = KEEP.
    labels = BuildAiEntity2ShadowLabels(snapshot, &map, desired, state, replay);
    require(labels[0].label == kAiEntityShadowKeep && labels[0].argument == -1,
        "repeated shadow BUILD was not KEEP");
    // Unmappable produce (type not in the table) -> PREFIX_UNRESOLVED from
    // that row on, with zeroed economy masks.
    AiEntity2ShadowState fresh;
    std::vector<AiEntity2ShadowDesiredOrder> bad;
    bad.push_back({0x1d0, AiSemanticActionKind::produce_unit, 0, 0, 0, 0x33});
    bad.push_back({2 * 0x1d0, AiSemanticActionKind::produce_unit, 0, 0, 0, 0x20});
    labels = BuildAiEntity2ShadowLabels(snapshot, &map, bad, fresh, replay);
    require(labels[0].exclude_reason ==
            static_cast<u16>(AiEntity2ShadowExcludeReason::prefix_unresolved) &&
        labels[1].label == kAiEntityShadowExcluded &&
        labels[1].exclude_reason ==
            static_cast<u16>(AiEntity2ShadowExcludeReason::prefix_unresolved) &&
        replay.dynamic_economy_pair_mask[snapshot.economy_words_per_row()] == 0,
        "unresolved shadow prefix handling wrong");
    // return_cargo is never a label; a resource ordered by target id is
    // unmappable; two orders for one unit are MULTIPLE_DESIRED.
    std::vector<AiEntity2ShadowDesiredOrder> misc;
    misc.push_back({0x1d0, AiSemanticActionKind::return_cargo, 0, 0, 0, 0});
    labels = BuildAiEntity2ShadowLabels(snapshot, &map, misc, fresh, replay);
    require(labels[0].exclude_reason ==
            static_cast<u16>(AiEntity2ShadowExcludeReason::return_cargo),
        "return_cargo teacher event not excluded");
    misc.clear();
    misc.push_back({0x1d0, AiSemanticActionKind::harvest, 0, 7 * 32 + 16, 7 * 32 + 16, 0});
    misc.push_back({0x1d0, AiSemanticActionKind::stop, 0, 0, 0, 0});
    labels = BuildAiEntity2ShadowLabels(snapshot, &map, misc, fresh, replay);
    require(labels[0].exclude_reason ==
            static_cast<u16>(AiEntity2ShadowExcludeReason::multiple_desired),
        "multiple desired orders not excluded");
    misc.clear();
    misc.push_back({0x1d0, AiSemanticActionKind::harvest, 0, 7 * 32 + 16, 7 * 32 + 16, 0});
    labels = BuildAiEntity2ShadowLabels(snapshot, &map, misc, fresh, replay);
    require(labels[0].label == kAiEntityShadowIssue && labels[0].argument == 1,
        "shadow HARVEST label wrong");
    // Record encoding: SHD2 magic, exact size.
    AiEntity2ActRequestBody body;
    body.snapshot = snapshot;
    const std::vector<u8> payload = EncodeAiEntity2ActRequestPayload(body);
    AiEntity2WireHeader header;
    header.kind = static_cast<u16>(AiEntityWireKind::act_req);
    header.own_rows = 2;
    header.target_rows = 1;
    header.resource_rows = snapshot.resource_rows;
    header.build_rows = snapshot.build_rows;
    header.produce_rows = snapshot.produce_rows;
    header.research_rows = snapshot.research_rows;
    header.payload_bytes = static_cast<u32>(payload.size());
    header.payload_crc32 = AiEntityCrc32(payload.data(), payload.size());
    std::array<AiEntity2ShadowSlotLabel, kAiEntity2SlotCount> slot_labels{};
    const std::vector<u8> record = EncodeAiEntity2ShadowRecord(header, payload,
        labels, replay, slot_labels);
    const std::size_t words = snapshot.economy_words_per_row();
    require(record.size() == 8 + 128 + payload.size() + 4 + 2 * 16 + 2 * 4 +
            2 * 12 + 2 * words * 4 + 2 * 4 + 4 * 8 &&
        record[0] == 'S' && record[3] == '5',
        "SHD5 record framing wrong");
    (void)research_row;
}

void test_ai_entity2_slots() {
    // Two fighters join the economy fixture: ids 3 and 4 (rows after the
    // worker 1 and the base 2), both attack/move/patrol capable.
    AiObservation obs = make_entity2_observation();
    AiObservedUnit fighter_a = fighter_unit(3 * 0x1d0, 0, 3 * 32 + 16, 6 * 32 + 16, true);
    fighter_a.runtime_slot_index = 3;
    fighter_a.type_flags = (1u << 4) | (1u << 5) | (1u << 9);
    obs.units.push_back(fighter_a);
    AiObservedUnit fighter_b = fighter_unit(4 * 0x1d0, 0, 4 * 32 + 16, 6 * 32 + 16, true);
    fighter_b.runtime_slot_index = 4;
    fighter_b.type_flags = (1u << 4) | (1u << 5) | (1u << 9);
    obs.units.push_back(fighter_b);
    // Map start candidates: our own (48,48) and one at tile (6,6).
    obs.start_candidate_mask = 0x3;
    obs.start_candidate_x = {48, 6 * 32 + 16, 0, 0, 0, 0, 0, 0};
    obs.start_candidate_y = {48, 6 * 32 + 16, 0, 0, 0, 0, 0, 0};
    AiEntityRegistry registry;
    AiEntityRegistryReset(registry);
    UnitMovementUnit live_worker = make_entity_live_unit(1, 0, 0x10,
        (1u << 4) | (1u << 6) | (1u << 7));
    UnitMovementUnit live_base = make_entity_live_unit(2, 0, 0x80, 0);
    UnitMovementUnit live_a = make_entity_live_unit(3, 0, 5,
        (1u << 4) | (1u << 5) | (1u << 9));
    UnitMovementUnit live_b = make_entity_live_unit(4, 0, 5,
        (1u << 4) | (1u << 5) | (1u << 9));
    UnitMovementUnit live_hostile = make_entity_live_unit(5, 1, 5, 1u << 5);
    std::vector<UnitMovementUnit*> live{&live_worker, &live_base, &live_a, &live_b,
        &live_hostile};
    AiEntityRegistryAuditFrame(registry, live);
    UnitMovementMap map = make_entity_legacy_map(8, 8);
    const GameSessionUnitReferenceTables tables = make_entity2_references();

    // Slot state: B was assigned to SCOUT a while ago (past the assign
    // cooldown); MAIN marches to cell 63.
    AiEntity2SlotState slots;
    const AiEntityKey key_a{3 * 0x1d0, 1};
    const AiEntityKey key_b{4 * 0x1d0, 1};
    slots.membership[AiEntityPackKey(key_b)] = kAiEntity2SlotScout;
    slots.assigned_frame[AiEntityPackKey(key_b)] = 9000;
    slots.orders[kAiEntity2SlotMain].active = true;
    slots.orders[kAiEntity2SlotMain].command = AiEntity2SlotCommand::attack_move;
    slots.orders[kAiEntity2SlotMain].cell = 63;
    slots.orders[kAiEntity2SlotMain].issued_frame = 9500;
    // A already carries the derived order (origin MAIN, active).
    AiEntity2OrderStore store;
    AiEntityActiveOrder derived;
    derived.source = key_a;
    derived.controller_owner = 0;
    derived.control_epoch = 1;
    derived.command = static_cast<u8>(AiEntity2Command::attack_move);
    derived.target_x = 7 * 32 + 16;
    derived.target_y = 7 * 32 + 16;
    derived.status = AiEntityOrderStatus::active;
    derived.issued_frame = 9500;
    derived.origin_slot = kAiEntity2SlotMain;
    store.combat[AiEntityPackKey(key_a)] = derived;

    AiEntity2SnapshotInput input;
    input.observation = &obs;
    input.registry = &registry;
    input.movement_map = &map;
    input.catalog.unit_references = &tables;
    input.catalog.unit_catalog = entity2_unit_catalog;
    input.catalog.research_catalog = entity2_research_catalog;
    input.orders = &store;
    input.slots = &slots;
    const AiEntity2Snapshot snapshot = BuildAiEntity2Snapshot(input);
    require(!snapshot.contract_error, snapshot.error.c_str());
    require(snapshot.own.size() == 4, "slot fixture row count");
    const AiEntity2OwnAppendix& worker = snapshot.own_appendix[0];
    const AiEntity2OwnAppendix& base = snapshot.own_appendix[1];
    const AiEntity2OwnAppendix& a = snapshot.own_appendix[2];
    const AiEntity2OwnAppendix& b = snapshot.own_appendix[3];
    require(worker.slot_id == kAiEntity2SlotNone && base.slot_id == kAiEntity2SlotNone &&
        worker.assign_mask == 0 && base.assign_mask == 0,
        "non-combat rows got slot fields");
    require(a.slot_id == kAiEntity2SlotMain && b.slot_id == kAiEntity2SlotScout,
        "slot membership wrong");
    // SCOUT is full: A may move to RAID_A/RAID_B only; B may go anywhere else.
    require(a.assign_mask == ((1u << 1) | (1u << 2)) &&
        b.assign_mask == ((1u << 0) | (1u << 1) | (1u << 2)) &&
        snapshot.scout_free_at_snapshot == 0,
        "assign masks wrong");
    // Assign cooldown: a member assigned last tick cannot be moved again.
    {
        AiEntity2SlotState cooling = slots;
        cooling.assigned_frame[AiEntityPackKey(key_b)] = 9596;
        AiEntity2SnapshotInput cooling_input = input;
        cooling_input.slots = &cooling;
        const AiEntity2Snapshot cooling_snapshot = BuildAiEntity2Snapshot(cooling_input);
        require(!cooling_snapshot.contract_error &&
            cooling_snapshot.own_appendix[3].assign_mask == 0 &&
            cooling_snapshot.own_appendix[2].assign_mask == ((1u << 1) | (1u << 2)),
            "assign cooldown mask wrong");
    }
    require(a.slot_order_relation == kAiEntity2SlotRelationMatch &&
        b.slot_order_relation == kAiEntity2SlotRelationNone,
        "slot order relation wrong");
    // Disobedience mask: A cannot take a personal MOVE/ATTACK_MOVE/PATROL
    // while MAIN marches; HOLD (and ATTACK_UNIT) stay; B (SCOUT, no order)
    // is free.  No STOP bit exists in the policy vocabulary (action v4).
    require((snapshot.own[2].command_mask & ((1u << 1) | (1u << 2) | (1u << 3))) == 0 &&
        (snapshot.own[2].command_mask & (1u << 5)) != 0 &&
        (snapshot.own[2].command_mask & (1u << 6)) == 0 &&
        (snapshot.own[3].command_mask & (1u << 1)) != 0,
        "disobedience mask wrong");
    // Slot blocks.
    const AiEntity2SlotBlock& main = snapshot.slots[kAiEntity2SlotMain];
    const AiEntity2SlotBlock& scout = snapshot.slots[kAiEntity2SlotScout];
    require(main.member_count == 1 && main.active == 1 &&
        main.command == static_cast<u8>(AiEntity2SlotCommand::attack_move) &&
        main.cell == 63 && main.age_frames == 100 && main.pursuing == 1 &&
        main.centroid_x == 3 * 32 + 16,
        "MAIN slot block wrong");
    require(scout.member_count == 1 && scout.active == 0 && scout.cell == -1 &&
        snapshot.slots[kAiEntity2SlotRaidA].member_count == 0,
        "SCOUT/RAID slot block wrong");
    // Slot masks: MAIN (active) may STOP; RAID_A (empty) may still be
    // commanded ahead (cell union of all fighters); HUNT takes no cell.
    require((snapshot.slot_command_mask[kAiEntity2SlotMain] &
            (1u << static_cast<u32>(AiEntity2SlotCommand::stop))) != 0 &&
        (snapshot.slot_command_mask[kAiEntity2SlotRaidA] &
            (1u << static_cast<u32>(AiEntity2SlotCommand::stop))) == 0 &&
        (snapshot.slot_command_mask[kAiEntity2SlotRaidA] &
            (1u << static_cast<u32>(AiEntity2SlotCommand::attack_move))) != 0 &&
        snapshot.slot_cell_mask[kAiEntity2SlotRaidA] ==
            snapshot.slot_cell_mask[kAiEntity2SlotMain] &&
        AiEntity2SlotChoiceLegal(snapshot, kAiEntity2SlotRaidA,
            static_cast<u8>(AiEntity2SlotCommand::attack_move), 63) &&
        !AiEntity2SlotChoiceLegal(snapshot, kAiEntity2SlotRaidA,
            static_cast<u8>(AiEntity2SlotCommand::stop), -1) &&
        !AiEntity2SlotChoiceLegal(snapshot, kAiEntity2SlotMain,
            static_cast<u8>(AiEntity2SlotCommand::hunt_neutral), 5) &&
        !AiEntity2SlotChoiceLegal(snapshot, kAiEntity2SlotMain,
            static_cast<u8>(AiEntity2SlotCommand::hold), 5),
        "slot command/cell masks wrong");
    // Start candidates: own (tile 1,1 -> cell 9) and (6,6) -> cell 54, both
    // explored; the other six absent.
    require(snapshot.start_candidates[0].cell == 9 &&
        snapshot.start_candidates[0].is_own == 1 &&
        snapshot.start_candidates[1].cell == 54 &&
        snapshot.start_candidates[1].explored == 1 &&
        snapshot.start_candidates[2].cell == -1 &&
        snapshot.intent_reward_material[0] == 2 &&
        snapshot.intent_reward_material[1] == 2 &&
        snapshot.intent_reward_material[2] == 0,
        "start candidates / intent material wrong");
    // Wire round trip carries every slot field.
    AiEntity2ActRequestBody body;
    body.snapshot = snapshot;
    const std::vector<u8> bytes = EncodeAiEntity2ActRequestPayload(body);
    AiEntity2WireHeader header;
    header.own_rows = 4;
    header.target_rows = 1;
    header.resource_rows = snapshot.resource_rows;
    header.build_rows = snapshot.build_rows;
    header.produce_rows = snapshot.produce_rows;
    header.research_rows = snapshot.research_rows;
    AiEntity2ActRequestBody decoded;
    std::string error;
    require(DecodeAiEntity2ActRequestPayload(bytes.data(), bytes.size(), header,
            false, decoded, nullptr, &error), error.c_str());
    require(decoded.snapshot.slots[kAiEntity2SlotMain].cell == 63 &&
        decoded.snapshot.start_candidates[1].cell == 54 &&
        decoded.snapshot.own_appendix[3].slot_id == kAiEntity2SlotScout &&
        decoded.snapshot.own_appendix[2].assign_mask == a.assign_mask &&
        decoded.snapshot.slot_command_mask == snapshot.slot_command_mask &&
        decoded.snapshot.intent_reward_material[3] == 0xffffffffull &&
        EncodeAiEntity2ActRequestPayload(decoded) == bytes,
        "slot fields did not round-trip");

    // Assign ledger: with SCOUT full nobody may join; with it free the first
    // canonical row wins and the second is closed.
    std::vector<u8> commands(4, 0);
    std::vector<i32> arguments(4, -1);
    std::vector<u8> assigns{0, 0, 4, 0};
    AiEntity2LedgerReplay replay = AiEntity2ReplayLedger(snapshot, commands,
        arguments, kAiEntity2NoUnresolvedRow, &assigns);
    require(replay.assign_legal[2] == 0 &&
        (replay.dynamic_assign_mask[2] & (1u << kAiEntity2SlotScout)) == 0,
        "full SCOUT accepted an assign");
    AiEntity2SlotState free_slots = slots;
    free_slots.membership.clear();
    input.slots = &free_slots;
    const AiEntity2Snapshot free_snapshot = BuildAiEntity2Snapshot(input);
    require(free_snapshot.scout_free_at_snapshot == 1 &&
        (free_snapshot.own_appendix[2].assign_mask & (1u << kAiEntity2SlotScout)) != 0,
        "free SCOUT bit missing");
    assigns = {0, 0, 4, 4};
    replay = AiEntity2ReplayLedger(free_snapshot, commands, arguments,
        kAiEntity2NoUnresolvedRow, &assigns);
    require(replay.assign_legal[2] == 1 && replay.assign_legal[3] == 0 &&
        (replay.dynamic_assign_mask[3] & (1u << kAiEntity2SlotScout)) == 0 &&
        (replay.dynamic_assign_mask[2] & (1u << kAiEntity2SlotScout)) != 0,
        "assign ledger did not close SCOUT after the first taker");

    // Derivation rule (EASY §2 ⑤ / §4 (i)).
    AiEntity2SlotOrder march;
    march.active = true;
    march.command = AiEntity2SlotCommand::attack_move;
    march.cell = 63;
    AiEntity2SlotMemberView view;
    view.slot_changed = true;
    require(AiEntity2SlotMemberNeedsOrder(march, view), "changed slot not derived");
    view = AiEntity2SlotMemberView{};
    view.latch_matches_slot = true;
    require(!AiEntity2SlotMemberNeedsOrder(march, view), "matching latch re-derived");
    view = AiEntity2SlotMemberView{};
    view.has_latch = true;
    view.latch_terminal = true;
    require(AiEntity2SlotMemberNeedsOrder(march, view), "stopped member not re-guided");
    view.arrived = true;
    require(!AiEntity2SlotMemberNeedsOrder(march, view), "arrived member re-guided");
    view = AiEntity2SlotMemberView{};
    view.has_personal_issue = true;
    view.slot_changed = true;
    require(!AiEntity2SlotMemberNeedsOrder(march, view), "personal order overridden");
    AiEntity2SlotOrder hold;
    hold.active = true;
    hold.command = AiEntity2SlotCommand::hold;
    view = AiEntity2SlotMemberView{};
    view.just_assigned = true;
    require(AiEntity2SlotMemberNeedsOrder(hold, view), "hold not derived on join");
    view.latch_matches_slot = true;
    require(!AiEntity2SlotMemberNeedsOrder(hold, view), "hold re-derived");

    // Teacher intent -> slot / assign labels (SHD3).
    AiEntity2ShadowTeacherIntent intent;
    intent.desired[kAiEntity2SlotMain].active = true;
    intent.desired[kAiEntity2SlotMain].command = AiEntity2SlotCommand::attack_move;
    intent.desired[kAiEntity2SlotMain].cell = 63;          // same as current -> KEEP
    intent.desired[kAiEntity2SlotRaidA].active = true;
    intent.desired[kAiEntity2SlotRaidA].command = AiEntity2SlotCommand::move;
    intent.desired[kAiEntity2SlotRaidA].cell = 9;          // new -> ISSUE
    intent.desired_slot[4 * 0x1d0] = kAiEntity2SlotRaidA;  // B leaves SCOUT
    intent.desired_slot[3 * 0x1d0] = kAiEntity2SlotScout;  // A wants SCOUT (full)
    AiEntity2ShadowState shadow_state;
    AiEntity2LedgerReplay shadow_replay;
    std::array<AiEntity2ShadowSlotLabel, kAiEntity2SlotCount> slot_labels{};
    std::vector<AiEntity2ShadowDesiredOrder> none;
    const std::vector<AiEntity2ShadowLabel> labels = BuildAiEntity2ShadowLabels(
        snapshot, &map, none, shadow_state, shadow_replay,
        kAiEntityShadowPointMaxErrorPx, &intent, &slot_labels);
    require(slot_labels[kAiEntity2SlotMain].label == kAiEntityShadowKeep &&
        slot_labels[kAiEntity2SlotRaidA].label == kAiEntityShadowIssue &&
        slot_labels[kAiEntity2SlotRaidA].command ==
            static_cast<u8>(AiEntity2SlotCommand::move) &&
        slot_labels[kAiEntity2SlotRaidA].cell == 9,
        "commander teacher labels wrong");
    require(labels[3].assign_label == kAiEntityShadowIssue &&
        labels[3].assign == kAiEntity2SlotRaidA + 1 &&
        labels[2].assign_label == kAiEntityShadowExcluded,
        "assign teacher labels wrong");
}

int main() {
    test_observation_visibility_and_determinism();
    test_observation_resource_memory();
    test_action_validation_and_packet_planning();
    test_semantic_action_v2_planning();
    test_live_validation_adapter();
    test_tyrano_scripted_bot();
    test_idle_worker_detection();
    test_tyrano_replay_derived_build_order();
    test_ai_rl_reward();
    test_ai_rl_trace();
    test_ai_rl_producer_queue_capacity_mask();
    test_ai_rl_hunt_and_research();
    test_ai_rl_research_tree_walk();
    test_ai_micro_executor_harvest_and_flee();
    test_ai_micro_executor_defend_bubble();
    test_ai_micro_executor_attack_retarget_retreat_and_pullback();
    test_ai_micro_executor_target_class_gate();
    test_ai_micro_executor_effective_range();
    test_ai_micro_executor_attack_cohesion();
    test_ai_micro_executor_cohesion_hysteresis();
    test_ai_micro_executor_leash_hysteresis();
    test_ai_micro_executor_stuck_recovery();
    test_ai_micro_executor_harvest_spread();
    test_ai_micro_executor_scout_picket();
    test_ai_micro_executor_translator_objectives();
    test_ai_micro_executor_tactics_and_search();
    test_ai_expansion_cluster_merge();
    test_ai_expansion_plan_and_chain();
    test_ai_shared_build_placement();
    test_ai_search_split();
    test_ai_raid_group_and_target_cell();
    test_ai_v8_observation_features();
    test_ai_decision_gate();
    test_ai_defense_reflex();
    test_ai_macro_autopilot();
    test_ai_corridor_guard_and_noncombat_flee();
    test_ai_local_paths_and_scout_guard();
    test_ai_pending_site_and_reject_backoff();
    test_ai_reflex_proportional_detail();
    test_ai_hunt_reachability_guard();
    test_ai_meat_priority_and_defend_pickup();
    test_ai_placement_open_ring_preference();
    test_ai_four_squads();
    test_ai_attack_commit_and_hunt_range();
    test_ai_idle_worker_stale_flag_and_cohesion_sight();
    test_ai_autopilot_tech_guard();
    test_ai_cohesion_median_anchor();
    test_ai_attack_waves();
    test_ai_play_lobby_role_compatibility();
    test_ai_entity_registry();
    test_ai_entity_point_geometry();
    test_ai_entity_attack_pair_predicate();
    test_ai_entity_snapshot_and_wire();
    test_ai_entity_wire_contract();
    test_ai_entity_shadow_labels();
    test_ai_entity_order_latch();
    test_ai_entity2_wire_contract();
    test_ai_entity2_snapshot_and_ledger();
    test_ai_entity2_economy_tracking();
    test_ai_entity2_shadow_labels();
    test_ai_entity2_slots();
    std::cout << "ai_play_interface_regression: passed\n";
    return 0;
}
