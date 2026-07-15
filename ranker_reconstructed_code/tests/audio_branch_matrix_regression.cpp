#include "ranker_gameplay_sound.h"
#include "ranker_directx.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace ranker {

namespace audio_probe {
std::vector<u32> played_slots;
u32 current_slot = 0;
i32 current_volume = 0;
i32 current_pan = 0;
u32 direct_play_count = 0;
u32 duplicate_play_count = 0;
u32 next_load_slot = 0;
}

void SetCurrentDirectSoundBufferPlaybackState(u32 slot_index, LONG volume, LONG pan) {
    audio_probe::current_slot = slot_index;
    audio_probe::current_volume = static_cast<i32>(volume);
    audio_probe::current_pan = static_cast<i32>(pan);
}
void AdjustCurrentSoundBufferVolume() {}
void AdjustCurrentSoundBufferPan() {}
void PlayCurrentSoundBufferSlot() {
    ++audio_probe::direct_play_count;
    audio_probe::played_slots.push_back(audio_probe::current_slot);
}
void DuplicateAndPlayReservedSoundBuffer() {
    ++audio_probe::duplicate_play_count;
    audio_probe::played_slots.push_back(audio_probe::current_slot);
}
void ReleaseAllDirectSoundBufferSlots() {
    audio_probe::next_load_slot = 0;
}
void SetNextSoundBufferStaticFlag() {}
u32 LoadTrcWaveRecordIntoSoundBufferSlot(const char*, u32) {
    return audio_probe::next_load_slot++;
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "AUDIO_BRANCH_MATRIX_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

GameplaySoundState make_sound(bool visible = true) {
    GameplaySoundState sound{};
    sound.bank_loaded = true;
    sound.camera_x = 0;
    sound.camera_y = 0;
    sound.viewport_center_x = 0;
    sound.viewport_center_y = 0;
    sound.visibility_map.width = 64;
    sound.visibility_map.height = 64;
    sound.visibility_map.flags.assign(64 * 64, visible ? 0x08000000u : 0u);
    InitializeDefaultGameplaySoundAttenuation(sound);
    return sound;
}

GameplayUnitSoundDefinition make_definition() {
    GameplayUnitSoundDefinition definition{};
    definition.selected_response_count = 2;
    definition.command_response_count = 2;
    definition.attack_count = 2;
    definition.death_count = 2;
    definition.harvest_work_count = 2;
    definition.production_complete_count = 2;
    definition.spawn_complete_count = 2;
    definition.attack_probability_gate = 1;
    definition.attack_sound_frames = {3, 7, -1, 99, 99, 99, 99, 99};
    definition.harvest_sound_frame = 5;
    return definition;
}

GameplayUnitSoundBaseSlots make_slots() {
    GameplayUnitSoundBaseSlots slots{};
    slots.selected_response = 100;
    slots.command_response = 110;
    slots.attack = 120;
    slots.death = 130;
    slots.harvest_work = 140;
    slots.production_complete = 150;
    slots.spawn_complete = 160;
    return slots;
}

void test_special_selection_matrix() {
    GameplaySoundState sound = make_sound(false);
    GameplayUnitSoundDefinition definition = make_definition();
    definition.sound_kind = kGameplayUnitSpecialSoundKind;
    definition.special_construction_response_slot = 20;
    definition.special_damaged_response_slot = 21;
    definition.special_healthy_response_slot = 22;
    UnitMovementUnit unit{};
    unit.max_health = 100;
    unit.health = 75;

    unit.action_mode_gate = 1;
    require(HandleSelectedUnitVoiceCue(sound, unit, definition, make_slots()),
        "special construction selection did not queue");
    require(sound.requests[20].pending, "special construction slot mismatch");

    unit.action_mode_gate = 0;
    unit.health = 74;
    require(HandleSelectedUnitVoiceCue(sound, unit, definition, make_slots()),
        "special damaged selection did not queue");
    require(sound.requests[21].pending, "special damaged threshold/slot mismatch");

    unit.health = 75;
    require(HandleSelectedUnitVoiceCue(sound, unit, definition, make_slots()),
        "special healthy selection did not queue");
    require(sound.requests[22].pending, "special healthy threshold/slot mismatch");
}

void test_visibility_and_frame_gates() {
    GameplayUnitSoundDefinition definition = make_definition();
    GameplayUnitSoundBaseSlots slots = make_slots();
    UnitMovementUnit unit{};
    unit.x = 0x200;
    unit.y = 0x100;

    GameplaySoundState hidden = make_sound(false);
    unit.animation_frame = 3;
    require(!HandleUnitAttackFrameVoiceCue(hidden, unit, definition, slots),
        "hidden attack voice became audible");
    require(!HandleUnitDeathVoiceCue(hidden, unit, definition, slots),
        "hidden death voice became audible");
    require(!HandleUnitHitReactionVoiceCue(hidden, unit, definition, slots),
        "hidden hit reaction became audible");
    unit.animation_frame = 5;
    require(!HandleWorkerHarvestFrameVoiceCue(hidden, unit, definition, slots),
        "hidden harvest voice became audible");

    GameplaySoundState visible = make_sound(true);
    unit.animation_frame = 2;
    require(!HandleUnitAttackFrameVoiceCue(visible, unit, definition, slots),
        "attack voice ignored its exact frame table");
    unit.animation_frame = 99;
    require(!HandleUnitAttackFrameVoiceCue(visible, unit, definition, slots),
        "attack frame scan continued after -1 sentinel");
    unit.animation_frame = 3;
    require(HandleUnitAttackFrameVoiceCue(visible, unit, definition, slots),
        "visible matching attack voice did not queue");
    require(visible.requests[120].pending || visible.requests[121].pending,
        "attack group base/variant mismatch");

    require(HandleUnitHitReactionVoiceCue(visible, unit, definition, slots),
        "visible hit reaction did not queue");
    require(visible.requests[100].pending || visible.requests[101].pending,
        "hit reaction did not reuse selected-response group zero variants");

    require(HandleUnitDeathVoiceCue(visible, unit, definition, slots),
        "visible death did not queue");
    require(visible.requests[130].pending || visible.requests[131].pending,
        "death group base/variant mismatch");

    unit.animation_frame = 4;
    require(!HandleWorkerHarvestFrameVoiceCue(visible, unit, definition, slots),
        "harvest voice ignored exact frame gate");
    unit.animation_frame = 5;
    require(HandleWorkerHarvestFrameVoiceCue(visible, unit, definition, slots),
        "visible harvest voice did not queue");
    require(visible.requests[140].pending || visible.requests[141].pending,
        "harvest group base/variant mismatch");
}

void test_probability_rng_and_completion_visibility_bypass() {
    GameplayUnitSoundDefinition definition = make_definition();
    GameplayUnitSoundBaseSlots slots = make_slots();
    UnitMovementUnit unit{};
    unit.animation_frame = 3;

    GameplaySoundState probability = make_sound(true);
    definition.attack_probability_gate = 2;
    probability.variant_seed = 1;
    require(!HandleUnitAttackFrameVoiceCue(probability, unit, definition, slots),
        "nonzero attack probability roll was accepted");
    require(!probability.requests[120].pending,
        "rejected probability roll queued an attack sound");

    GameplaySoundState hidden = make_sound(false);
    hidden.variant_seed = 0;
    require(HandleUnitProductionCompleteVoiceCue(hidden, unit, definition, slots),
        "production-complete group incorrectly used visibility gate");
    require(hidden.requests[150].pending || hidden.requests[151].pending,
        "production-complete group mismatch");
    require(HandleUnitSpawnCompleteVoiceCue(hidden, unit, definition, slots),
        "spawn-complete group incorrectly used visibility gate");
    require(hidden.requests[160].pending || hidden.requests[161].pending,
        "spawn-complete group mismatch");
}

void test_special_death_and_silent_state() {
    GameplayUnitSoundDefinition definition = make_definition();
    definition.sound_kind = kGameplayUnitSpecialSoundKind;
    definition.special_death_slot = 77;
    UnitMovementUnit unit{};
    unit.x = 64;
    unit.y = 64;

    GameplaySoundState visible = make_sound(true);
    require(HandleUnitDeathVoiceCue(visible, unit, definition, make_slots()),
        "visible special death did not queue");
    require(visible.requests[77].pending, "special death slot mismatch");
    require(!HandleSilentDeathStateSoundCue(visible, unit),
        "original silent death state emitted a sound");
}

void test_same_slot_arbitration_and_concurrency() {
    GameplaySoundState sound = make_sound(true);
    require(HandlePositionalGameplaySoundQueueRequest(sound, 200, -8000, -50),
        "first same-slot request failed");
    const i32 quiet_volume = sound.requests[200].volume;
    require(HandlePositionalGameplaySoundQueueRequest(sound, 200, -1000, 75),
        "louder same-slot request failed");
    require(sound.requests[200].volume > quiet_volume &&
            sound.requests[200].pan == 75,
        "same-slot loudest request did not replace quieter request");
    const i32 loud_volume = sound.requests[200].volume;
    require(HandlePositionalGameplaySoundQueueRequest(sound, 200, -9000, -99),
        "quiet same-slot arbitration request failed");
    require(sound.requests[200].volume == loud_volume &&
            sound.requests[200].pan == 75,
        "quieter same-slot request replaced louder pending request");
    require(HandlePositionalGameplaySoundQueueRequest(sound, 201, 0, 0),
        "different-slot concurrent request failed");

    audio_probe::played_slots.clear();
    audio_probe::duplicate_play_count = 0;
    HandleQueuedGameplaySoundPlayback(sound);
    require(audio_probe::duplicate_play_count == 2,
        "queued playback did not play both concurrent slots");
    require(audio_probe::played_slots.size() == 2 &&
            audio_probe::played_slots[0] == 200 &&
            audio_probe::played_slots[1] == 201,
        "queued playback slot order/index mismatch");
    require(!sound.requests[200].pending && !sound.requests[201].pending,
        "queued playback did not clear pending entries");
}

} // namespace

int main() {
    test_special_selection_matrix();
    test_visibility_and_frame_gates();
    test_probability_rng_and_completion_visibility_bypass();
    test_special_death_and_silent_state();
    test_same_slot_arbitration_and_concurrency();
    std::cout << "AUDIO_BRANCH_MATRIX_PASS branches=selection/attack/hit/death/harvest/production/spawn arbitration=concurrent+loudest\n";
    return EXIT_SUCCESS;
}
