#include "ranker_game_session_tables.h"
#include "ranker_ui_overlay.h"
#include "ranker_unit_commands.h"
#include "ranker_unit_equipment.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

using namespace ranker;

UnitMovementDefinition g_produced_definition{};
UnitMovementUnit g_produced{};
bool g_spawn_fails = false;
u32 g_create_calls = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void write_u32(GameSessionAvatarRuntime& runtime, std::size_t offset, u32 value) {
    runtime.bytes[offset + 0] = static_cast<u8>(value & 0xffu);
    runtime.bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
    runtime.bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xffu);
    runtime.bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xffu);
}

std::size_t record_offset(u32 owner, u32 slot_index) {
    return static_cast<std::size_t>(owner) * kGameSessionAvatarPlayerBytes +
        static_cast<std::size_t>(slot_index) * kGameSessionAvatarRecordBytes;
}

const UnitMovementDefinition* find_definition(UnitCommandContext&, u32 type_id) {
    return type_id == 0x23 ? &g_produced_definition : nullptr;
}

u32 production_type(UnitCommandContext&, UnitMovementUnit&) {
    return 0x23;
}

u32 duration_340(UnitCommandContext&, UnitMovementUnit&) {
    return 340;
}

u32 primary_140(UnitCommandContext&, UnitMovementUnit&) {
    return 140;
}

u32 secondary_zero(UnitCommandContext&, UnitMovementUnit&) {
    return 0;
}

u32 population_five(UnitCommandContext&, UnitMovementUnit&) {
    return 5;
}

UnitMovementUnit* create_unit(UnitCommandContext&, UnitMovementUnit&, u32,
    i32, i32) {
    ++g_create_calls;
    return g_spawn_fails ? nullptr : &g_produced;
}

UnitEquipmentEffectDefinition make_effect(u32 id) {
    UnitEquipmentEffectDefinition effect{};
    effect.id = id;
    effect.category = UnitEquipmentCategory::Generic;
    effect.mode = 0;
    effect.max_health_delta = 1;
    effect.health_delta = 1;
    effect.runtime_stat_1c_delta = 1;
    return effect;
}

void test_record_application() {
    constexpr u32 owner = 2;
    constexpr u32 slot_id = 3;
    GameSessionAvatarRuntime runtime{};
    const std::size_t offset = record_offset(owner, slot_id - 1);
    std::memcpy(runtime.bytes.data() + offset, "Rex", 4);
    write_u32(runtime, offset + kGameSessionAvatarInvalidMarkerOffset, 0x23);
    write_u32(runtime, offset + kGameSessionAvatarMaxHealthOffset, 100);
    write_u32(runtime, offset + kGameSessionAvatarMaxSecondaryOffset, 80);
    write_u32(runtime, offset + kGameSessionAvatarStat1cOffset, 20);
    write_u32(runtime, offset + kGameSessionAvatarStat20Offset, 30);
    write_u32(runtime, offset + kGameSessionAvatarLevelOffset, 4);
    write_u32(runtime, offset + kGameSessionAvatarProgressOffset, 55);
    write_u32(runtime, offset + kGameSessionAvatarPrimaryEquipmentOffset, 11);
    write_u32(runtime, offset + kGameSessionAvatarSecondaryEquipmentOffset, 12);
    for (u32 index = 0; index < 4; ++index) {
        write_u32(runtime, offset + kGameSessionAvatarPickupEffectOffset +
            index * sizeof(u32), 21 + index);
    }

    GameSessionAvatarRecord decoded;
    require(ReadGameSessionAvatarRecord(runtime, owner, slot_id - 1, decoded),
        "valid avatar record must decode");
    require(decoded.name == "Rex" && decoded.level == 4 &&
            decoded.pickup_effects[3] == 24,
        "decoded avatar record fields");

    UnitEquipmentCatalog catalog;
    for (u32 id : {11u, 12u, 21u, 22u, 23u, 24u}) {
        catalog.effects.push_back(make_effect(id));
    }
    UnitMovementContext movement{};
    UnitCommandContext context{};
    context.movement = &movement;
    context.equipment_catalog = &catalog;
    UnitMovementUnit produced{};
    produced.type_id = 0x23;
    produced.type_flags = kUnitEquipmentPickupEnabledFlag;
    produced.owner_id = owner;
    produced.command_flags = 0xffffffffu;

    require(ApplyGameSessionAvatarProductionRecord(
            context, produced, runtime, owner, slot_id),
        "avatar record must apply");
    require((produced.command_flags & 0x003c0000u) == (slot_id << 18),
        "slot occupancy bits");
    require(MatchesUiOverlayAvatarAttachmentSlot(produced, owner, slot_id),
        "applied avatar must block duplicate slot production");
    require(produced.max_health == 106 && produced.health == 106,
        "base health then six effects");
    require(produced.max_secondary_value == 80 && produced.secondary_value == 30,
        "secondary starts at signed 3/8 maximum");
    require(produced.runtime_stat_1c == 26 && produced.runtime_stat_20 == 30,
        "combat stats and effect sequence");
    require(produced.status_timer == 4 && produced.production_variant == 4 &&
            produced.elite_progress_value == 55,
        "level and progress mirrors");
    require(produced.equipment_slots[4] == 11 &&
            produced.equipment_slots[5] == 12,
        "dedicated equipment slots");
    for (u32 index = 0; index < 4; ++index) {
        require(produced.equipment_slots[index] == 21 + index,
            "pickup effects preserve record order");
    }
    require(produced.string_slot != 0 &&
            std::strcmp(movement.string_slots[produced.string_slot].data(), "Rex") == 0,
        "raw avatar name is interned");
}

void test_spawn_boundary_and_failure_refund() {
    UnitCommandContext context{};
    context.callbacks.find_definition = find_definition;
    context.callbacks.production_type_id = production_type;
    context.callbacks.production_spawn_duration = duration_340;
    context.callbacks.production_resource_cost = primary_140;
    context.callbacks.production_secondary_cost = secondary_zero;
    context.callbacks.production_population_cost = population_five;
    context.callbacks.create_unit = create_unit;
    g_produced_definition.production_spawn_time = 100;

    UnitMovementUnit producer{};
    producer.id = 1;
    producer.command_state = kUnitStateProductionSpawnCycle;
    producer.animation_frame = 338;
    producer.definition.production_cycle_period = 4;
    producer.production_reserved = true;
    producer.linked_object_id = 0x1d0;
    producer.next_path_x = 111;
    producer.next_path_y = 222;
    producer.saved_path_target_x = 333;
    producer.saved_path_target_y = 444;
    g_create_calls = 0;
    g_spawn_fails = false;
    g_produced = UnitMovementUnit{};
    g_produced.runtime_flags = 8;
    HandleUnitProductionSpawnCycle(context, producer);
    require(g_create_calls == 0 && producer.animation_frame == 339,
        "special avatar must not spawn one tick early");
    HandleUnitProductionSpawnCycle(context, producer);
    require(g_create_calls == 1,
        "special avatar spawns exactly at callback duration");
    require(g_produced.command_value == 0x1d0 &&
            g_produced.path_target_x == 111 &&
            g_produced.path_target_y == 222,
        "production rally copies raw target reference and +c8/+cc point");
    require((g_produced.command_state & kUnitCommandStateMask) == 0x14 &&
            (g_produced.runtime_flags & 8u) == 0,
        "production rally enters state14 and clears runtime bit8");

    producer = UnitMovementUnit{};
    producer.id = 1;
    producer.owner_id = 2;
    producer.command_state = kUnitStateProductionSpawnCycle;
    producer.animation_frame = 339;
    producer.definition.production_cycle_period = 4;
    producer.production_reserved = true;
    context.owner_resources[2] = 7;
    context.owner_secondary_resources[2] = 11;
    context.owner_population_reserved[2] = 5;
    g_spawn_fails = true;
    HandleUnitProductionSpawnCycle(context, producer);
    require(context.owner_resources[2] == 147,
        "avatar placement failure refunds base plus level primary cost");
    require(context.owner_secondary_resources[2] == 11,
        "avatar placement failure does not refund secondary resource");
    require(context.owner_population_reserved[2] == 5 &&
            !producer.production_reserved,
        "failed avatar preserves the original immediate population total");
}

} // namespace

int main() {
    require(CalculateGameSessionAvatarBuildTicks(0x22, 4, 100) == 140,
        "ordinary avatar duration formula");
    require(CalculateGameSessionAvatarBuildTicks(0x23, 4, 100) == 340,
        "special avatar duration formula");
    test_record_application();
    test_spawn_boundary_and_failure_refund();
    std::cout << "AVATAR_SPAWN_RUNTIME_PASS\n";
    return EXIT_SUCCESS;
}
