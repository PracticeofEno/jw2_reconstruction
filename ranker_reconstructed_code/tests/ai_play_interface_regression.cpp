#include "ranker_ai_autopilot.h"
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
    require(AiMicroExecutorStep(base_state.micro, obs).empty(),
        "defender engaged a hostile outside its effective range");
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
    require(AiMicroExecutorStep(air_state.micro, air).empty(),
        "anti-air reach used the ground range stat");
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
                leader_regrouped =
                    order.kind == AiSemanticActionKind::move &&
                    order.target_x < 1400;
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
        require(kAiRlActionCount == 64 && kAiRlFeatureCount == 788,
            "v9 action/feature counts");
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
    require(orders.size() == 1 && orders[0].kind == AiSemanticActionKind::move &&
        orders[0].unit_ids == std::vector<u32>{0x3100} &&
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

// v9 - the event-based decision gate (docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md):
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

// v9 - the macro autopilot rules (docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md
// §3.2): worker floor, pop guard, idle-producer guard; producer-conflict
// skips; policy fighter picks steer the idle guard.
void test_ai_macro_autopilot() {
    AiObservation obs = micro_observation();
    obs.population_used = 20;       // supply
    obs.population_reserved = 5;    // demand (healthy headroom)
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
    test_ai_micro_executor_leash_hysteresis();
    test_ai_micro_executor_stuck_recovery();
    test_ai_micro_executor_harvest_spread();
    test_ai_micro_executor_scout_picket();
    test_ai_micro_executor_translator_objectives();
    test_ai_micro_executor_tactics_and_search();
    test_ai_expansion_plan_and_chain();
    test_ai_shared_build_placement();
    test_ai_search_split();
    test_ai_raid_group_and_target_cell();
    test_ai_v8_observation_features();
    test_ai_decision_gate();
    test_ai_defense_reflex();
    test_ai_macro_autopilot();
    test_ai_corridor_guard_and_noncombat_flee();
    test_ai_pending_site_and_reject_backoff();
    test_ai_play_lobby_role_compatibility();
    std::cout << "ai_play_interface_regression: passed\n";
    return 0;
}
