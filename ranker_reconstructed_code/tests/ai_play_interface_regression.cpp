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
            movement.map.cells[index].flags = (x + y) % 2 == 0
                ? kMapCellPassableTerrain
                : kMapCellBlockedTerrain;
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
    require(seen.observation.tiles[2].resource_amount == 0,
        "never-seen explored tile leaked the live amount");

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
        hunt.target_unit_id == 0x9100 && hunt.include_neutral,
        "hunt did not set an attack objective on the neutral monster");
    // ...and the micro executor sends the fighter at it.
    obs.simulation_frame = 5;
    const std::vector<AiSemanticAction> orders =
        AiMicroExecutorStep(state.micro, obs);
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
        tile.explored = true;
    }
    obs.tiles[9 * obs.map_width_tiles + 12].resource_amount = 500;
    return obs;
}

AiObservedUnit fighter_unit(u32 id, u32 owner, i32 x, i32 y, bool controlled) {
    AiObservedUnit unit = observed_unit(id, owner, kTyranoMasosType, (1u << 5),
        x, y, controlled);
    unit.health = 100;
    unit.max_health = 100;
    unit.attack_range = 50;  // audited melee range (마소스)
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

    // Low-health pull-back: a fighter under 30% hp in contact leaves toward
    // the nest instead of attacking.
    AiObservation hurt = micro_observation();
    hurt.units[0].command_state = kUnitStateWorkerApproachHarvest;
    hurt.units.push_back(fighter_unit(0x3300, 0, 400, 400, true));
    hurt.units.back().health = 20;
    hurt.units.push_back(fighter_unit(0x9600, 1, 430, 400, false));
    TyranoScriptedBotState fresh{};
    hurt.simulation_frame = 50;
    orders = AiMicroExecutorStep(fresh.micro, hurt);
    require(orders.size() == 1 &&
        orders[0].kind == AiSemanticActionKind::move &&
        orders[0].unit_ids == std::vector<u32>{0x3300} &&
        orders[0].target_x == 320 && orders[0].target_y == 320,
        "low-health fighter did not pull back to the nest");
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
        army->target_unit_id == 0x9400,
        "attack_enemy_base did not set an attack objective on the building");
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
        scout.kind == AiMicroObjectiveKind::scout && scout.target_x >= 0 &&
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
        require(kAiRlActionCount == 52 && kAiRlFeatureCount == 531,
            "v5 action/feature counts");
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
    // Role derivation from data: range 0 = melee, long range = ranged.
    AiObservedUnit ranged = fighter_unit(0x7000, 0, 0, 0, true);
    ranged.attack_range = 200;
    AiObservedUnit unknown_weapon = fighter_unit(0x7002, 0, 0, 0, true);
    unknown_weapon.attack_range = 0;  // 트윈 람포스: no range in the definition
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
    test_ai_micro_executor_translator_objectives();
    test_ai_play_lobby_role_compatibility();
    std::cout << "ai_play_interface_regression: passed\n";
    return 0;
}
