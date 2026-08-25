#include "ranker_ai_actions.h"
#include "ranker_ai_live_validator.h"
#include "ranker_ai_observation.h"
#include "ranker_ai_scripted_bot.h"
#include "ranker_ai_slot_role.h"
#include "ranker_unit_commands.h"

#include <algorithm>
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
    test_action_validation_and_packet_planning();
    test_live_validation_adapter();
    test_tyrano_scripted_bot();
    test_tyrano_replay_derived_build_order();
    test_ai_play_lobby_role_compatibility();
    std::cout << "ai_play_interface_regression: passed\n";
    return 0;
}
