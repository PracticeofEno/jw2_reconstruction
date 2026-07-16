#include "ranker_directx.h"
#include "ranker_gameplay_frame_render.h"
#include "ranker_gameplay_sound.h"
#include "ranker_map_effects.h"
#include "ranker_production_orders.h"
#include "ranker_unit_commands.h"
#include "ranker_unit_damage.h"
#include "ranker_unit_equipment.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

// ranker_gameplay_sound.cpp reaches the DirectSound bridge only when queued
// requests are drained.  This regression only audits queue construction, so
// inert test endpoints keep the executable independent from the Win32 audio
// device and the JW2_05 archive.
namespace ranker {

void SetCurrentDirectSoundBufferPlaybackState(u32, LONG, LONG) {}
void AdjustCurrentSoundBufferVolume() {}
void AdjustCurrentSoundBufferPan() {}
void PlayCurrentSoundBufferSlot() {}
void DuplicateAndPlayReservedSoundBuffer() {}
void ReleaseAllDirectSoundBufferSlots() {}
void SetNextSoundBufferStaticFlag() {}
u32 LoadTrcWaveRecordIntoSoundBufferSlot(const char*, u32) { return 0; }

} // namespace ranker

namespace {

using namespace ranker;

constexpr u32 kSentinelSoundSlot = 700;
constexpr u32 kHarvestSoundBaseSlot = 200;
constexpr u32 kHitSoundBaseSlot = 300;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "GAMEPLAY_RESOURCE_SOUND_BRANCH_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

GameplaySoundState make_visible_sound_state(u32 seed = 1) {
    GameplaySoundState state{};
    state.bank_loaded = true;
    state.variant_seed = seed;
    state.camera_x = 0;
    state.camera_y = 0;
    state.viewport_center_x = 0;
    state.viewport_center_y = 0;
    state.visibility_map.width = 1;
    state.visibility_map.height = 1;
    state.visibility_map.flags = {state.visibility_map.visible_flag};
    InitializeDefaultGameplaySoundAttenuation(state);
    return state;
}

bool same_request(const GameplaySoundRequest& lhs,
    const GameplaySoundRequest& rhs) {
    return lhs.pending == rhs.pending &&
        lhs.slot_index == rhs.slot_index &&
        lhs.volume == rhs.volume && lhs.pan == rhs.pan;
}

void require_sound_snapshot(const GameplaySoundState& sound, u32 seed,
    const std::array<GameplaySoundRequest, kGameplaySoundRequestSlots>& requests,
    const char* message) {
    require(sound.variant_seed == seed, message);
    for (std::size_t slot = 0; slot < requests.size(); ++slot) {
        if (!same_request(sound.requests[slot], requests[slot])) {
            require(false, message);
        }
    }
}

std::array<MapEffectDefinition, 8> g_map_effect_definitions{};

const MapEffectDefinition* find_map_effect_definition(
    const MapEffectContext&, u32 effect_id) {
    if (effect_id >= g_map_effect_definitions.size()) {
        return nullptr;
    }
    return &g_map_effect_definitions[effect_id];
}

bool start_equipment_progress_effect(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 effect_id) {
    return context.map_effects != nullptr &&
        StartUnitProgressMapEffect(*context.map_effects, unit, effect_id);
}

struct MeatFixture {
    UnitMovementMap map;
    MapEffectContext effects;
    UnitCommandContext commands;
    UnitEquipmentCatalog catalog;

    MeatFixture() {
        map.width = 8;
        map.height = 8;
        map.stride_tiles = 8;
        map.cells.resize(64);
        for (UnitMovementCell& cell : map.cells) {
            cell.alternate_flags = kMapEffectBlockedTileFlag;
        }

        effects.map = &map;
        effects.effects.resize(4);
        for (u32 index = 0; index < effects.effects.size(); ++index) {
            effects.effects[index].id = index;
        }
        effects.free_effect_indices = {1, 2, 3};
        effects.callbacks.find_definition = find_map_effect_definition;

        commands.map_effects = &effects;
        commands.callbacks.start_equipment_progress_effect =
            start_equipment_progress_effect;

        UnitEquipmentEffectDefinition amount{};
        amount.id = 1;
        amount.category = UnitEquipmentCategory::Amount;
        amount.pickup_filter_mode = 2;
        catalog.effects.push_back(amount);
        commands.equipment_catalog = &catalog;
    }
};

const MapEffectInstance* active_counter_effect(const MapEffectContext& effects) {
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

void test_meat_pipeline_is_silent() {
    GameplaySoundState sound = make_visible_sound_state(0x13579bdfu);
    sound.requests[kSentinelSoundSlot] = {
        true, kSentinelSoundSlot, -321, 77};
    SetDefaultFrontendGameplaySoundState(&sound);
    const u32 initial_seed = sound.variant_seed;
    const auto initial_requests = sound.requests;

    MeatFixture fixture;
    UnitMovementUnit neutral{};
    neutral.owner_id = 8;
    neutral.x = 0x40;
    neutral.y = 0x40;
    neutral.path_target_x = neutral.x;
    neutral.path_target_y = neutral.y;
    neutral.action_mode = 100;
    neutral.definition.lifecycle_class = 0;
    neutral.definition.passive_recovery_flags = 2;

    // SpawnUnitPassiveMapEffects (original 0x004d14d8) creates the neutral
    // meat map effect without touching the gameplay sound RNG or queue.
    SpawnUnitPassiveMapEffects(fixture.effects, neutral);
    const MapEffectInstance* drop = active_counter_effect(fixture.effects);
    require(drop != nullptr && drop->effect_id == 1 &&
            drop->repeat_count == 100 && neutral.action_mode == 0,
        "neutral death did not create the expected meat counter effect");
    require_sound_snapshot(sound, initial_seed, initial_requests,
        "meat creation changed the gameplay sound state");

    // FUN_00411350/FUN_00411750 category-three pickup adds raw +0x2c and
    // releases the map effect.  It is deliberately silent in the original.
    UnitMovementUnit collector{};
    collector.owner_id = 0;
    collector.x = 0x40;
    collector.y = 0x40;
    collector.path_target_x = collector.x;
    collector.path_target_y = collector.y;
    collector.type_flags = kUnitEquipmentPickupEnabledFlag;
    require(HandleUnitEquipmentMapEffectCollect(fixture.commands,
            fixture.effects, collector, collector.x, collector.y,
            fixture.catalog) && collector.action_mode == 100,
        "meat pickup did not transfer the counter to the collector");
    require_sound_snapshot(sound, initial_seed, initial_requests,
        "meat pickup changed the gameplay sound state");

    // Original slot zero in FUN_00411890 starts effect id one.  A reserve over
    // 50 emits a fifty-count progress effect and leaves fifty on the unit; no
    // pickup/eat cue or sound-RNG draw is made.
    require(ClearUnitEquipmentSlot(
            fixture.commands, collector, 0, fixture.catalog),
        "right-click meat consumption failed to start its progress effect");
    const MapEffectInstance* consumed = active_counter_effect(fixture.effects);
    require(consumed != nullptr && consumed->effect_id == 1 &&
            consumed->repeat_count == 50 && collector.action_mode == 50,
        "right-click meat consumption did not preserve the original 50 split");
    require_sound_snapshot(sound, initial_seed, initial_requests,
        "right-click meat consumption changed the gameplay sound state");
    SetDefaultFrontendGameplaySoundState(nullptr);
}

void test_idle_map_effect_claim_raw_offset_contract() {
    constexpr i32 kActivePayloadSentinel = 0x13579;

    MeatFixture success_fixture;
    MapEffectInstance* success_effect = HandleMapEffectNearestTileSpawn(
        success_fixture.effects, 1, 0x40, 0x40);
    require(success_effect != nullptr && success_effect->id == 3,
        "idle claim fixture did not allocate the expected map-effect slot");

    UnitMovementUnit success{};
    success.id = 0x1d0;
    success.x = 0x40;
    success.y = 0x40;
    success.command_state = kUnitStateRuntimeIdleAcquire;
    success.area_marker_flags = 0x80000000u;
    success.type_flags = 0x22u;
    success.command_value = 0xfeedu;
    success.active_command_payload.x = kActivePayloadSentinel;
    UnitMovementUnit previous_success_target{};
    success.target = &previous_success_target;
    ProcessUnitIdleAcquireCommand(success_fixture.commands, success);

    require(success.command_value ==
                success_effect->id * kMapEffectRawRecordSize &&
            success.target == nullptr &&
            success.active_command_payload.x == kActivePayloadSentinel &&
            success.path_target_x == success_effect->x &&
            success.path_target_y == success_effect->y &&
            success.command_state == kUnitStateAssistTarget &&
            success_effect->flags == kMapEffectLinkedFlag &&
            success_effect->linked_unit == &success,
        "successful idle map-effect claim did not preserve raw +0x68/+0xd8");

    MeatFixture rejected_fixture;
    MapEffectInstance* rejected_effect = HandleMapEffectNearestTileSpawn(
        rejected_fixture.effects, 1, 0x80, 0x20);
    require(rejected_effect != nullptr && rejected_effect->id == 3,
        "rejected idle claim fixture did not allocate the expected slot");

    UnitMovementUnit rejected{};
    rejected.id = 0x3a0;
    rejected.x = 0x20;
    rejected.y = 0x20;
    rejected.command_state = kUnitStateRuntimeIdleAcquire;
    rejected.area_marker_flags = 0x80000000u;
    rejected.type_flags = 0x22u;
    rejected.runtime_flags = 8u;
    rejected.command_value = 0xbeefu;
    rejected.active_command_payload.x = kActivePayloadSentinel;
    UnitMovementUnit previous_rejected_target{};
    rejected.target = &previous_rejected_target;
    ProcessUnitIdleAcquireCommand(rejected_fixture.commands, rejected);

    require(rejected.command_value ==
                rejected_effect->id * kMapEffectRawRecordSize &&
            rejected.target == nullptr &&
            rejected.active_command_payload.x == kActivePayloadSentinel &&
            rejected.path_target_x == rejected_effect->x &&
            rejected.path_target_y == rejected_effect->y &&
            rejected.command_state == kUnitStateRuntimeIdleAcquire &&
            rejected_effect->flags == 0 &&
            rejected_effect->linked_unit == nullptr,
        "rejected idle map-effect claim did not retain the original raw residue");
}

struct HarvestProbe {
    GameplaySoundState* sound = nullptr;
    std::vector<u32> observed_frames;
};

HarvestProbe* g_harvest_probe = nullptr;

void record_harvest_frame_and_sound(UnitCommandContext&,
    UnitMovementUnit& unit) {
    require(g_harvest_probe != nullptr && g_harvest_probe->sound != nullptr,
        "harvest probe was not installed");
    g_harvest_probe->observed_frames.push_back(unit.animation_frame);

    GameplayUnitSoundDefinition definition{};
    definition.harvest_work_count = 2;
    definition.harvest_sound_frame = 5;
    GameplayUnitSoundBaseSlots slots{};
    slots.harvest_work = kHarvestSoundBaseSlot;
    HandleWorkerHarvestFrameVoiceCue(
        *g_harvest_probe->sound, unit, definition, slots);
}

void test_normal_and_reserved_harvest_frame_order() {
    UnitCommandContext context{};
    context.callbacks.on_harvest_frame = record_harvest_frame_and_sound;

    GameplaySoundState normal_sound = make_visible_sound_state(1);
    HarvestProbe normal_probe{&normal_sound};
    g_harvest_probe = &normal_probe;
    UnitMovementUnit normal{};
    normal.x = 0x40;
    normal.y = 0x40;
    normal.animation_frame = 4;
    normal.definition.timed_flag_phase_b_period = 10;
    ProcessWorkerHarvestTile(context, normal);
    require(normal_probe.observed_frames.size() == 1 &&
            normal_probe.observed_frames[0] == 5 &&
            normal.animation_frame == 5,
        "normal berry harvest callback did not observe the post-increment frame");
    require(normal_sound.requests[kHarvestSoundBaseSlot + 1].pending,
        "normal berry harvest did not queue group-five sound on frame five");

    GameplaySoundState reserved_sound = make_visible_sound_state(1);
    HarvestProbe reserved_probe{&reserved_sound};
    g_harvest_probe = &reserved_probe;
    UnitMovementUnit reserved{};
    reserved.x = 0x40;
    reserved.y = 0x40;
    reserved.command_state = kUnitStateReservedTileWork;
    reserved.animation_frame = 5;
    reserved.cargo_amount = 1;
    reserved.work_timer = 1;
    reserved.definition.spawn_frame_count = 0xffffffffu;
    HandleReservedTileWorkCycle(context, reserved);
    require(reserved_probe.observed_frames.size() == 1 &&
            reserved_probe.observed_frames[0] == 5 &&
            reserved.animation_frame == 6,
        "reserved berry harvest callback did not observe the pre-increment frame");
    require(reserved_sound.requests[kHarvestSoundBaseSlot + 1].pending,
        "reserved berry harvest did not queue group-five sound on frame five");
    g_harvest_probe = nullptr;
}

UnitMovementUnit* g_reserved_dropoff = nullptr;
u32 g_reserved_dropoff_calls = 0;
u32 g_reserved_completion_calls = 0;

UnitMovementUnit* find_reserved_dropoff(UnitCommandContext&,
    UnitMovementUnit&) {
    ++g_reserved_dropoff_calls;
    return g_reserved_dropoff;
}

bool hold_reserved_completion(UnitCommandContext&, UnitMovementUnit&) {
    ++g_reserved_completion_calls;
    return false;
}

UnitMovementContext make_reserved_tile_movement(u32 width, u32 height) {
    UnitMovementContext movement{};
    movement.map.width = width;
    movement.map.height = height;
    movement.map.stride_tiles = width;
    movement.map.cells.resize(static_cast<std::size_t>(width) * height);
    for (UnitMovementCell& cell : movement.map.cells) {
        cell.flags = kMapCellPassableTerrain;
    }
    return movement;
}

void test_reserved_tile_raw_dropoff_contract() {
    // StartReservedTileWorkCommand 0x004cb1ed clears raw +0x74 and preserves
    // the raw +0x80 dropoff override.
    UnitMovementContext start_movement = make_reserved_tile_movement(4, 4);
    UnitCommandContext start_context{};
    start_context.movement = &start_movement;
    UnitMovementUnit start{};
    start.id = 0x1d0;
    start.type_id = 0x10;
    start.x = 63;
    start.y = 47;
    start.path_target_x = 32;
    start.path_target_y = 32;
    start.definition.range_threshold = 128;
    start.previous_command_state = 1;
    start.destination_aux_state = 0x5a0;
    StartReservedTileWorkCommand(start_context, start);
    require(start.previous_command_state == 0,
        "reserved work start did not clear raw +0x74 previous state");
    require(start.destination_aux_state == 0x5a0,
        "reserved work start cleared the raw +0x80 dropoff override");

    const auto run_completion = [](u32 override_id,
                                    u32 expected_search_calls) {
        UnitMovementContext movement = make_reserved_tile_movement(1, 1);
        movement.map.cells[0].flags |=
            100u << kMapCellHarvestAmountShift;

        UnitMovementUnit stale{};
        stale.id = 0x3a0;
        stale.active = true;
        UnitMovementUnit selected{};
        selected.id = 0x570;
        selected.active = true;
        UnitMovementUnit worker{};
        worker.id = 0x1d0;
        worker.type_id = 0x10;
        worker.active = true;
        worker.command_state = kUnitStateReservedTileWork;
        worker.animation_frame = 7;
        worker.cargo_amount = 61;
        worker.work_timer = 61;
        worker.definition.spawn_frame_count = 0xffffffffu;
        worker.destination_x = 0;
        worker.destination_y = 0;
        worker.target = &stale;
        worker.command_value = stale.id;
        worker.destination_aux_state = override_id;
        movement.active_units = {&worker, &stale, &selected};
        RegisterUnitReservedMapTile(movement, worker);

        UnitCommandContext context{};
        context.movement = &movement;
        context.callbacks.find_dropoff = find_reserved_dropoff;
        context.callbacks.on_reserved_tile_work_complete =
            hold_reserved_completion;
        g_reserved_dropoff = &selected;
        g_reserved_dropoff_calls = 0;
        g_reserved_completion_calls = 0;

        HandleReservedTileWorkCycle(context, worker);
        require(g_reserved_dropoff_calls == expected_search_calls,
            "reserved completion used the wrong dropoff selection branch");
        require(g_reserved_completion_calls == 1,
            "reserved completion did not reach its completion callback");
        require(worker.target == &selected &&
                worker.command_value == selected.id,
            "reserved completion did not persist selected dropoff at raw +0x68");
        require(worker.command_state == kUnitStateReservedTileWork,
            "completion-allocation failure did not retain state 0x54");
    };

    // A zero +0x80 must ignore the stale +0x68 target and search again.
    run_completion(0, 1);
    // A nonzero +0x80 resolves that unit without running nearest search, and
    // still writes the selected offset back to raw +0x68.
    run_completion(0x570, 0);
    g_reserved_dropoff = nullptr;
}

void test_spawn_cancel_clears_structure_reverse_link() {
    UnitMovementContext movement{};
    UnitCommandContext context{};
    context.movement = &movement;

    UnitMovementUnit builder{};
    builder.id = 0x1d0;
    builder.type_id = 0x10;
    builder.active = true;
    builder.command_state = kUnitStateSpawnCreateCycle;
    UnitMovementUnit structure{};
    structure.id = 0x3a0;
    structure.active = true;
    builder.target = &structure;
    builder.command_value = structure.id;
    structure.target = &builder;
    structure.command_value = builder.id;
    movement.active_units = {&builder, &structure};

    // The state-0x5b c6/aux6 button publishes command 0x06 with y == -1 and
    // carries the linked structure offset as its command value.
    builder.pending_command = UnitQueuedCommand{
        0x06, static_cast<i32>(structure.id), 0x1234, 0xffffffffu};
    HandlePendingUnitCommandDispatch(context, builder);
    require(builder.command_state == kUnitStateSpawnPlacementStart &&
            builder.target == nullptr &&
            builder.command_value == structure.id &&
            builder.path_target_y == -1,
        "spawn cancel promotion did not reproduce the raw 0x06 tuple");

    StartUnitSpawnPlacementCommand(context, builder);
    require(structure.target == nullptr && structure.command_value == 0,
        "spawn cancel left the structure's reciprocal raw +0x68 link intact");
    require(builder.command_state == kUnitStateRuntimeIdleAcquire,
        "spawn cancel did not return the builder to idle");
}

enum class HitTraceEvent {
    SimRandomY,
    SimRandomX,
    SimRandomOneInFour,
    SoundGroup0,
};

struct NeutralHitProbe {
    GameplayFrameRandomState simulation_rng;
    GameplaySoundState sound;
    UnitMovementUnit voice_unit;
    std::vector<HitTraceEvent> trace;
    std::vector<u32> simulation_results;
};

NeutralHitProbe* g_hit_probe = nullptr;

u32 roll_hit_simulation(HitTraceEvent event, u32 limit) {
    require(g_hit_probe != nullptr, "neutral hit probe was not installed");
    g_hit_probe->trace.push_back(event);
    g_hit_probe->simulation_rng.limit = limit;
    const GameplayFrameRandomResult result = SelectGameplayFrameRandomLimit(
        g_hit_probe->simulation_rng, 0, 0);
    g_hit_probe->simulation_results.push_back(result.selected_value);
    return result.selected_value;
}

void run_neutral_hit_reaction(UnitDamageContext&, UnitRecord&,
    UnitRecord& target) {
    require(target.owner_id == 8,
        "neutral hit branch was dispatched for a non-neutral target");

    // HandleUnitDamageReaction 0x004c26fd..0x004c27b0 consumes the shared
    // simulation RNG in Y, X, then one-in-four order.  Only result zero reaches
    // HandleUnitHitSoundMirror and its independent group-zero sound RNG.
    (void)roll_hit_simulation(HitTraceEvent::SimRandomY, 0x80);
    (void)roll_hit_simulation(HitTraceEvent::SimRandomX, 0x80);
    if (roll_hit_simulation(HitTraceEvent::SimRandomOneInFour, 4) != 0) {
        return;
    }

    g_hit_probe->trace.push_back(HitTraceEvent::SoundGroup0);
    GameplayUnitSoundDefinition definition{};
    definition.selected_response_count = 2;
    GameplayUnitSoundBaseSlots slots{};
    slots.selected_response = kHitSoundBaseSlot;
    HandleUnitHitReactionVoiceCue(g_hit_probe->sound,
        g_hit_probe->voice_unit, definition, slots);
}

NeutralHitProbe make_neutral_hit_probe(u32 simulation_seed) {
    NeutralHitProbe probe{};
    probe.simulation_rng.seed = simulation_seed;
    probe.sound = make_visible_sound_state(1);
    probe.voice_unit.owner_id = 8;
    probe.voice_unit.x = 0x40;
    probe.voice_unit.y = 0x40;
    return probe;
}

void dispatch_neutral_hit(NeutralHitProbe& probe) {
    UnitRecord attacker{};
    attacker.owner_id = 0;
    UnitRecord neutral{};
    neutral.owner_id = 8;
    UnitDamageContext damage{};
    damage.attacker = &attacker;
    damage.callbacks.on_damage_reaction = run_neutral_hit_reaction;
    g_hit_probe = &probe;
    HandleUnitDamageReaction(damage, neutral);
    g_hit_probe = nullptr;
}

void test_neutral_hit_rng_order() {
    NeutralHitProbe accepted = make_neutral_hit_probe(2);
    dispatch_neutral_hit(accepted);
    const std::array<HitTraceEvent, 4> accepted_trace{
        HitTraceEvent::SimRandomY,
        HitTraceEvent::SimRandomX,
        HitTraceEvent::SimRandomOneInFour,
        HitTraceEvent::SoundGroup0,
    };
    require(accepted.trace.size() == accepted_trace.size(),
        "accepted neutral hit consumed the wrong number of RNG stages");
    for (std::size_t index = 0; index < accepted_trace.size(); ++index) {
        require(accepted.trace[index] == accepted_trace[index],
            "accepted neutral hit RNG stages were reordered");
    }
    require(accepted.simulation_results == std::vector<u32>({2, 32, 0}) &&
            accepted.simulation_rng.call_count == 3,
        "neutral hit simulation RNG values/call count diverged");
    require(accepted.sound.requests[kHitSoundBaseSlot + 1].pending,
        "accepted neutral hit did not consume group-zero sound RNG last");

    NeutralHitProbe rejected = make_neutral_hit_probe(0);
    const u32 rejected_sound_seed = rejected.sound.variant_seed;
    dispatch_neutral_hit(rejected);
    require(rejected.trace.size() == 3 &&
            rejected.trace[0] == HitTraceEvent::SimRandomY &&
            rejected.trace[1] == HitTraceEvent::SimRandomX &&
            rejected.trace[2] == HitTraceEvent::SimRandomOneInFour,
        "rejected neutral hit did not stop after the one-in-four roll");
    require(rejected.simulation_results == std::vector<u32>({0, 34, 2}) &&
            rejected.sound.variant_seed == rejected_sound_seed,
        "rejected neutral hit consumed group-zero sound RNG");
}

void test_damage_reaction_ally_range_uses_raw_198_base() {
    ProductionOrderRuntimeState production{};
    UnitMovementUnit damaged{};
    damaged.owner_id = 1;
    damaged.type_id = 32;
    // JW2_09.TRC type 32: archive +0x198 is 300, while +0x19c is 150 and
    // action range +0x1b0 is 50.  Only +0x198 feeds 0x004c369a.
    damaged.definition.effect_adjusted_interaction_range_base = 300;
    damaged.definition.support_range = 150;
    damaged.definition.range_threshold = 50;
    production.completion_effect_totals
        [kProductionEffectSlotUnitInteractionRange][damaged.owner_id]
        [damaged.type_id] = 20;

    require(CalculateUnitDamageReactionAllyRange(
                production, damaged, nullptr) == 160,
        "damage-reaction ally radius did not use (raw +0x198 + effect 8) / 2");
}

UnitMovementUnit* g_patrol_candidate = nullptr;

UnitMovementUnit* find_patrol_candidate(UnitCommandContext&,
    UnitMovementUnit&) {
    return g_patrol_candidate;
}

bool reject_patrol_action_target(UnitCommandContext&,
    UnitMovementUnit&, UnitMovementUnit&) {
    return false;
}

void test_patrol_route_payload_and_saved_origin() {
    UnitCommandContext context{};
    UnitMovementUnit unit{};
    UnitMovementUnit stale_target{};
    unit.x = 100;
    unit.y = 120;
    unit.destination_x = 17;
    unit.destination_y = 19;
    unit.anchor_x = 31;
    unit.anchor_y = 37;
    unit.command_value = 0xfeedu;
    unit.active_command_payload.y = 500;
    unit.active_command_payload.value = static_cast<u32>(-600);

    StartUnitPatrolRouteCommand(context, unit);
    require(unit.destination_x == 100 && unit.destination_y == 120,
        "patrol start did not save raw +0x78/+0x7c route origin");
    require(unit.anchor_x == 31 && unit.anchor_y == 37,
        "patrol start overwrote unrelated raw +0xd0/+0xd4 anchors");
    require(unit.path_target_x == 500 && unit.path_target_y == -600 &&
            unit.command_state == kUnitStatePatrolOutboundLeg,
        "patrol start did not path to active +0xdc/+0xe0 payload");
    require(unit.target == nullptr && unit.command_value == 0,
        "patrol point route did not clear raw +0x68 target mirror");

    unit.command_value = 0xbeefu;
    HandleUnitPatrolReturnLeg(context, unit);
    require(unit.path_target_x == 500 && unit.path_target_y == -600 &&
            unit.command_state == kUnitStatePatrolOutboundLeg &&
            unit.command_value == 0,
        "patrol return leg did not alternate to active command payload");

    unit.command_value = 0xbeefu;
    HandleUnitPatrolOutboundLeg(context, unit);
    require(unit.path_target_x == 100 && unit.path_target_y == 120 &&
            unit.command_state == kUnitStatePatrolReturnLeg &&
            unit.command_value == 0,
        "patrol outbound leg did not alternate to saved route origin");

    unit.command_flags = 0;
    unit.command_value = 0xbeefu;
    unit.target = &stale_target;
    ResumeUnitPatrolRouteAfterCombatTargetLoss(
        context, unit, kUnitStatePatrolReturnCombat);
    require(unit.path_target_x == 100 && unit.path_target_y == 120 &&
            unit.command_state == kUnitStatePatrolReturnLeg &&
            (unit.command_flags & 8u) != 0 && unit.target == nullptr &&
            unit.command_value == 0,
        "product patrol return fallback did not restore the saved origin");

    unit.command_flags = 0;
    unit.command_value = 0xbeefu;
    unit.target = &stale_target;
    ResumeUnitPatrolRouteAfterCombatTargetLoss(
        context, unit, kUnitStatePatrolOutboundCombat);
    require(unit.path_target_x == 500 && unit.path_target_y == -600 &&
            unit.command_state == kUnitStatePatrolOutboundLeg &&
            (unit.command_flags & 8u) != 0 && unit.target == nullptr &&
            unit.command_value == 0,
        "product patrol outbound fallback did not restore command payload");
}

void test_patrol_distance_mode_preserves_raw_candidate() {
    UnitMovementUnit candidate{};
    candidate.id = 0x1234u;
    candidate.x = 240;
    candidate.y = 260;

    UnitCommandContext context{};
    context.callbacks.find_target = find_patrol_candidate;
    context.callbacks.can_attack_target = reject_patrol_action_target;
    context.callbacks.target_in_action_range = reject_patrol_action_target;
    g_patrol_candidate = &candidate;

    UnitMovementUnit unit{};
    unit.x = 100;
    unit.y = 120;
    unit.type_flags = 0x20u;
    unit.distance_check_mode = 1;
    unit.active_command_payload.y = 500;
    unit.active_command_payload.value = 600;
    StartUnitPatrolRouteCommand(context, unit);
    require(unit.target == &candidate && unit.command_value == candidate.id,
        "distance mode one discarded the acquired raw +0x68 candidate");
    require(unit.path_target_x == candidate.x &&
            unit.path_target_y == candidate.y &&
            unit.command_state == kUnitStatePatrolOutboundLeg,
        "distance mode one did not approach its acquired patrol candidate");

    unit.target = nullptr;
    unit.command_value = 0;
    unit.command_flags = 0;
    HandleUnitPatrolReturnCombatTarget(context, unit);
    require(unit.target == &candidate && unit.command_value == candidate.id &&
            unit.path_target_x == candidate.x &&
            unit.path_target_y == candidate.y &&
            unit.command_state == kUnitStatePatrolReturnLeg &&
            (unit.command_flags & 8u) == 0,
        "patrol combat replacement filtered the raw distance-mode candidate");
    g_patrol_candidate = nullptr;
}

} // namespace

int main() {
    for (u32 id = 0; id < g_map_effect_definitions.size(); ++id) {
        g_map_effect_definitions[id].id = id;
        g_map_effect_definitions[id].frame_period = 3;
        g_map_effect_definitions[id].default_repeat_count = 1;
    }

    test_meat_pipeline_is_silent();
    test_idle_map_effect_claim_raw_offset_contract();
    test_normal_and_reserved_harvest_frame_order();
    test_reserved_tile_raw_dropoff_contract();
    test_spawn_cancel_clears_structure_reverse_link();
    test_neutral_hit_rng_order();
    test_damage_reaction_ally_range_uses_raw_198_base();
    test_patrol_route_payload_and_saved_origin();
    test_patrol_distance_mode_preserves_raw_candidate();
    std::cout <<
        "GAMEPLAY_RESOURCE_SOUND_BRANCH_PASS "
        "meat=silent spawn/pickup/right-click idle-claim=raw-offset "
        "harvest=normal-post/reserved-pre "
        "reserved=raw74/raw80/dropoff68 "
        "spawn-cancel=reverse-link-clear "
        "neutral-hit=sim-Y/X/1-in-4-then-group0/ally-range-raw198 "
        "patrol=active-payload/origin/raw-candidate\n";
    return EXIT_SUCCESS;
}
