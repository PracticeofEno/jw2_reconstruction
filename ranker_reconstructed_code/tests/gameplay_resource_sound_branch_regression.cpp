#include "ranker_directx.h"
#include "ranker_gameplay_frame_render.h"
#include "ranker_gameplay_sound.h"
#include "ranker_map_effects.h"
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

} // namespace

int main() {
    for (u32 id = 0; id < g_map_effect_definitions.size(); ++id) {
        g_map_effect_definitions[id].id = id;
        g_map_effect_definitions[id].frame_period = 3;
        g_map_effect_definitions[id].default_repeat_count = 1;
    }

    test_meat_pipeline_is_silent();
    test_normal_and_reserved_harvest_frame_order();
    test_neutral_hit_rng_order();
    std::cout <<
        "GAMEPLAY_RESOURCE_SOUND_BRANCH_PASS "
        "meat=silent spawn/pickup/right-click "
        "harvest=normal-post/reserved-pre "
        "neutral-hit=sim-Y/X/1-in-4-then-group0\n";
    return EXIT_SUCCESS;
}
