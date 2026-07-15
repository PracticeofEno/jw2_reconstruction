#include "ranker_map_effects.h"
#include "ranker_meat_pipeline.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace ranker {

// The focused target links the production map-effect implementation without
// the movement engine.  This is the exact row-major helper used by that engine.
u32 UnitMovementMapTileIndex(const UnitMovementMap& map, u32 tile_x,
    u32 tile_y) {
    return tile_y * map.stride_tiles + tile_x;
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "MEAT_PIPELINE_REGRESSION_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

MapEffectDefinition g_effect_definitions[5]{};
u32 g_random_call_count = 0;
u32 g_random_limit = 0;
u32 g_random_result = 0;

const MapEffectDefinition* find_effect_definition(
    const MapEffectContext&, u32 effect_id) {
    return effect_id < 5 ? &g_effect_definitions[effect_id] : nullptr;
}

u32 random_limit(MapEffectContext&, u32 limit) {
    ++g_random_call_count;
    g_random_limit = limit;
    return g_random_result;
}

void reset_random_probe() {
    g_random_call_count = 0;
    g_random_limit = 0;
    g_random_result = 0;
}

struct Fixture {
    UnitMovementMap map;
    MapEffectContext effects;

    Fixture() {
        map.width = 8;
        map.height = 8;
        map.stride_tiles = 8;
        map.cells.resize(64);
        for (UnitMovementCell& cell : map.cells) {
            cell.alternate_flags = kMapEffectBlockedTileFlag;
        }

        effects.map = &map;
        effects.effects.resize(4);
        effects.free_effect_indices = {1, 2, 3};
        effects.callbacks.find_definition = find_effect_definition;
        effects.callbacks.random_limit = random_limit;
    }
};

const MapEffectInstance* active_food_effect(const MapEffectContext& effects) {
    for (u32 index : effects.active_effect_indices) {
        if (index >= effects.effects.size()) {
            continue;
        }
        const MapEffectInstance& effect = effects.effects[index];
        if (effect.active && effect.effect_id >= 1 && effect.effect_id <= 4) {
            return &effect;
        }
    }
    return nullptr;
}

void test_drop_plan_boundaries_and_randomization() {
    struct TierCase {
        u32 reserve;
        u32 effect_id;
    };
    constexpr TierCase tiers[] = {
        {100, 1}, {101, 2}, {500, 2},
        {501, 3}, {1000, 3}, {1001, 4},
    };
    for (const TierCase& tier : tiers) {
        require(SelectUnitMeatMapEffectId(tier.reserve) == tier.effect_id,
            "meat effect tier boundary diverged");
    }

    const UnitMeatDropPlan empty = PlanUnitMeatDrop(0, 0, 99);
    require(!empty.valid && empty.effect_id == 0 && empty.repeat_count == 0,
        "zero reserve produced a meat drop plan");

    const UnitMeatDropPlan fixed = PlanUnitMeatDrop(118, 2, 29);
    require(fixed.valid && fixed.effect_id == 2 && fixed.repeat_count == 118,
        "passive flag bit one did not suppress the random bonus");

    const UnitMeatDropPlan randomized = PlanUnitMeatDrop(118, 0, 29);
    require(randomized.valid && randomized.effect_id == 2 &&
            randomized.repeat_count == 147,
        "randomized neutral meat plan did not add the supplied RNG result");

    const UnitMeatDropPlan wrapped = PlanUnitMeatDrop(
        std::numeric_limits<u32>::max(), 0, 1);
    require(wrapped.valid && wrapped.effect_id == 4 &&
            wrapped.repeat_count == 0,
        "neutral meat repeat-count addition did not preserve u32 wrap");

    UnitMovementUnit untouched{};
    untouched.action_mode = 7;
    MapEffectInstance untouched_effect{};
    untouched_effect.repeat_count = 9;
    CommitUnitMeatDrop(untouched, untouched_effect, empty);
    require(untouched.action_mode == 7 && untouched_effect.repeat_count == 9,
        "invalid meat plan mutated the unit or effect");
}

void test_spawn_gates_retain_reserve() {
    Fixture owner_fixture;
    UnitMovementUnit player_owned{};
    player_owned.owner_id = 7;
    player_owned.x = 0x40;
    player_owned.y = 0x40;
    player_owned.action_mode = 118;
    player_owned.definition.passive_recovery_flags = 2;
    SpawnUnitPassiveMapEffects(owner_fixture.effects, player_owned);
    require(player_owned.action_mode == 118 &&
            active_food_effect(owner_fixture.effects) == nullptr,
        "owner <= 7 bypass did not retain the meat reserve");

    Fixture exhausted_fixture;
    exhausted_fixture.effects.free_effect_indices.clear();
    UnitMovementUnit neutral{};
    neutral.owner_id = 8;
    neutral.x = 0x40;
    neutral.y = 0x40;
    neutral.action_mode = 118;
    neutral.definition.passive_recovery_flags = 2;
    SpawnUnitPassiveMapEffects(exhausted_fixture.effects, neutral);
    require(neutral.action_mode == 118 &&
            active_food_effect(exhausted_fixture.effects) == nullptr,
        "empty map-effect pool consumed the neutral reserve");

    // Original 0x004d1593 calls the RNG helper for flag-bit-1-clear drops
    // even when reserve >> 2 is zero.  The zero limit preserves RNG state,
    // but skipping the callback changes the observable call sequence.
    reset_random_probe();
    Fixture zero_limit_fixture;
    UnitMovementUnit one_meat{};
    one_meat.owner_id = 8;
    one_meat.x = 0x40;
    one_meat.y = 0x40;
    one_meat.action_mode = 1;
    one_meat.definition.passive_recovery_flags = 0;
    SpawnUnitPassiveMapEffects(zero_limit_fixture.effects, one_meat);
    const MapEffectInstance* one_drop =
        active_food_effect(zero_limit_fixture.effects);
    require(g_random_call_count == 1 && g_random_limit == 0 &&
            one_drop != nullptr && one_drop->repeat_count == 1,
        "zero-limit neutral meat RNG callback was skipped or changed the drop");
}

void require_progress_split(u32 reserve, u32 expected_drop,
    u32 expected_held) {
    Fixture fixture;
    UnitMovementUnit unit{};
    unit.path_target_x = 0x40;
    unit.path_target_y = 0x40;
    unit.action_mode = reserve;
    require(StartUnitProgressMapEffect(fixture.effects, unit, 1),
        "right-click meat split did not start");
    const MapEffectInstance* effect = active_food_effect(fixture.effects);
    require(effect != nullptr && effect->repeat_count == expected_drop &&
            unit.action_mode == expected_held,
        "right-click meat split boundary diverged");
}

void test_right_click_split_boundaries() {
    Fixture empty_fixture;
    UnitMovementUnit empty{};
    empty.path_target_x = 0x40;
    empty.path_target_y = 0x40;
    require(!StartUnitProgressMapEffect(empty_fixture.effects, empty, 1) &&
            active_food_effect(empty_fixture.effects) == nullptr,
        "zero held reserve created a right-click map effect");

    require_progress_split(1, 1, 0);
    require_progress_split(50, 50, 0);
    require_progress_split(51, 1, 50);
    require_progress_split(117, 67, 50);
}

void test_recovery_rejection_gates() {
    UnitMovementUnit unit{};
    unit.max_health = 100;
    unit.health = 100;
    unit.action_mode = 4;
    unit.definition.passive_recovery_enabled = 1;

    require(!TryConsumeUnitMeatReserveForRecovery(unit),
        "runtime-full unit consumed meat");
    require(unit.action_mode == 4 && unit.health == 100,
        "runtime-full rejection mutated meat or HP");

    unit.health = 99;
    unit.definition.passive_recovery_enabled = 0;
    require(!TryConsumeUnitMeatReserveForRecovery(unit),
        "passive-recovery-disabled unit consumed meat");

    unit.definition.passive_recovery_enabled = 1;
    unit.command_flags = 0x2000;
    require(!TryConsumeUnitMeatReserveForRecovery(unit),
        "command blocker 0x2000 did not suppress meat recovery");

    unit.command_flags = 0;
    unit.action_mode = 0;
    require(!TryConsumeUnitMeatReserveForRecovery(unit),
        "zero reserve reported meat recovery");
    unit.max_health = 0;
    require(!TryConsumeUnitMeatReserveForRecovery(unit),
        "zero runtime max reported meat recovery");
}

void test_spawn_pickup_and_low_health_consumption() {
    Fixture fixture;

    UnitMovementUnit neutral{};
    neutral.owner_id = 8;
    neutral.x = 0x40;
    neutral.y = 0x40;
    neutral.path_target_x = neutral.x;
    neutral.path_target_y = neutral.y;
    neutral.action_mode = 118;
    neutral.definition.passive_recovery_flags = 2;

    // SpawnUnitPassiveMapEffects is original 0x004d14d8.  The 118-count
    // reserve selects effect tier two, moves the count to raw effect +0x30,
    // and clears raw unit +0x2c.
    SpawnUnitPassiveMapEffects(fixture.effects, neutral);
    const MapEffectInstance* drop = active_food_effect(fixture.effects);
    require(drop != nullptr && drop->effect_id == 2 &&
            drop->repeat_count == 118 && neutral.action_mode == 0,
        "neutral meat spawn did not transfer the +0x2c reserve");

    UnitMovementUnit collector{};
    collector.owner_id = 0;
    collector.x = 0x40;
    collector.y = 0x40;
    collector.path_target_x = collector.x;
    collector.path_target_y = collector.y;
    collector.cargo_amount = 12;
    // Original 0x004cc67c reads raw unit +0x10 directly as the recovery cap.
    collector.max_health = 120;
    collector.health = 100;
    collector.definition.passive_recovery_enabled = 1;
    collector.definition.passive_recovery_flags = 2;

    // FUN_00411350/FUN_00411750 add effect +0x30 to unit +0x2c and
    // release the effect.  Worker cargo at raw +0x4c must remain unrelated.
    MapEffectInstance* collected = nullptr;
    for (u32 index : fixture.effects.active_effect_indices) {
        if (index < fixture.effects.effects.size() &&
            fixture.effects.effects[index].active) {
            collected = &fixture.effects.effects[index];
            break;
        }
    }
    require(collected != nullptr, "collector did not find the meat effect");
    AddUnitMeatReserve(collector, collected->repeat_count);
    ClearMapEffectTileOccupied(fixture.effects, *collected);
    ReleaseMapEffect(fixture.effects, *collected);
    require(collector.action_mode == 118 && collector.cargo_amount == 12 &&
            active_food_effect(fixture.effects) == nullptr,
        "pickup did not use action_mode or contaminated worker cargo");

    // HandleUnitPassiveRecoveryAndTimedRemoval is original 0x004cc66c.
    // At low HP, every tick consumes one unit +0x2c count and restores one HP.
    require(TryConsumeUnitMeatReserveForRecovery(collector),
        "low-health meat recovery was not accepted");
    require(collector.action_mode == 117 && collector.health == 101 &&
            collector.cargo_amount == 12,
        "low-health recovery did not consume one meat count for one HP");

    // A full-health collector retains its reserve.  This is why a live probe
    // that reaches the drop after healing to max cannot observe consumption.
    collector.health = collector.max_health;
    require(!TryConsumeUnitMeatReserveForRecovery(collector),
        "full-health meat recovery was incorrectly accepted");
    require(collector.action_mode == 117 && collector.health == 120 &&
            collector.cargo_amount == 12,
        "full health incorrectly consumed the meat reserve");

    // Original FUN_00411890 slot zero stages the held reserve back onto the
    // map.  It is a split/drop action, not the healing tick itself: from 117,
    // the original leaves 50 held and drops the excess 67 as a map effect.
    require(StartUnitProgressMapEffect(fixture.effects, collector, 1),
        "right-click reserve action did not start a progress map effect");
    const MapEffectInstance* split = active_food_effect(fixture.effects);
    require(split != nullptr && split->effect_id == 1 &&
            split->repeat_count == 67 && collector.action_mode == 50 &&
            collector.cargo_amount == 12,
        "right-click reserve split diverged from the original 50 remainder");
}

} // namespace

int main() {
    for (u32 id = 0; id < 5; ++id) {
        g_effect_definitions[id].id = id;
        g_effect_definitions[id].frame_period = 3;
        g_effect_definitions[id].default_repeat_count = 1;
    }

    test_drop_plan_boundaries_and_randomization();
    test_spawn_gates_retain_reserve();
    test_right_click_split_boundaries();
    test_recovery_rejection_gates();
    test_spawn_pickup_and_low_health_consumption();
    std::cout <<
        "MEAT_PIPELINE_REGRESSION_PASS "
        "spawn=118/tier2 pickup=action_mode cargo=unchanged "
        "consume=117/health+1 full-health=retained right-click=67+50\n";
    return EXIT_SUCCESS;
}
