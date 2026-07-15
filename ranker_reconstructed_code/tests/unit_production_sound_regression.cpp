#include "ranker_directx.h"
#include "ranker_gameplay_sound.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace ranker {

void SetCurrentDirectSoundBufferPlaybackState(u32, LONG, LONG) {}
void AdjustCurrentSoundBufferVolume() {}
void AdjustCurrentSoundBufferPan() {}
void DuplicateAndPlayReservedSoundBuffer() {}
void PlayCurrentSoundBufferSlot() {}
void ReleaseAllDirectSoundBufferSlots() {}
void SetNextSoundBufferStaticFlag() {}
u32 LoadTrcWaveRecordIntoSoundBufferSlot(const char*, u32 record_index) {
    return record_index;
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "UNIT_PRODUCTION_SOUND_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool any_pending(const GameplaySoundState& state) {
    for (const GameplaySoundRequest& request : state.requests) {
        if (request.pending) {
            return true;
        }
    }
    return false;
}

void write_le32(std::vector<u8>& bytes, std::size_t offset, u32 value) {
    require(offset + sizeof(value) <= bytes.size(), "definition write overflow");
    for (u32 shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8] = static_cast<u8>(value >> shift);
    }
}

GameplaySoundState hidden_loaded_sound_state() {
    GameplaySoundState state{};
    state.bank_loaded = true;
    state.listener_position_offset = 20;
    state.visibility_map.width = 1;
    state.visibility_map.height = 1;
    state.visibility_map.flags = {0};
    return state;
}

void test_original_group_offsets_and_base_slots() {
    constexpr std::size_t kDefinitionBytes = 0x24bc;
    constexpr std::size_t kSoundCountBase = 0x2424;
    std::vector<u8> bytes(kDefinitionBytes, 0);
    write_le32(bytes, kSoundCountBase + 7 * sizeof(u32), 2);
    write_le32(bytes, kSoundCountBase + 8 * sizeof(u32), 3);

    GameplayUnitSoundDefinition definition{};
    require(DecodeGameplayUnitSoundDefinition(
            bytes.data(), bytes.size(), definition),
        "unit definition sound profile did not decode");
    require(definition.production_complete_count == 2,
        "raw +0x2440 was not decoded as production-complete count");
    require(definition.spawn_complete_count == 3,
        "raw +0x2444 was not decoded as construction-complete count");

    std::array<u32, kGameplayUnitSoundGroupCount> first_slots{};
    first_slots[7] = 300;
    first_slots[8] = 500;
    const GameplayUnitSoundBaseSlots bases =
        BuildGameplayUnitSoundBaseSlots(first_slots);
    require(bases.production_complete == 300 && bases.spawn_complete == 500,
        "group seven/eight base slots were interchanged");
}

void test_production_complete_variant_and_silence_contract() {
    GameplaySoundState state = hidden_loaded_sound_state();
    GameplayUnitSoundDefinition definition{};
    definition.sound_kind = kGameplayUnitSpecialSoundKind;
    definition.production_complete_count = 2;
    definition.special_healthy_response_slot = 17;
    definition.special_construction_response_slot = 18;
    GameplayUnitSoundBaseSlots bases{};
    bases.production_complete = 300;
    UnitMovementUnit produced{};
    state.variant_seed = 3;

    require(HandleUnitProductionCompleteVoiceCue(
            state, produced, definition, bases, -120, 444),
        "hidden produced unit did not queue its completion voice");
    const GameplaySoundRequest& request = state.requests[301];
    require(request.pending && request.slot_index == 301,
        "production completion selected the wrong group-seven variant");
    require(request.volume == -100 && request.pan == 444,
        "production completion changed positional playback arguments");
    require(!state.requests[17].pending && !state.requests[18].pending,
        "production completion used special selection/construction slots");

    state = hidden_loaded_sound_state();
    definition.production_complete_count = 0;
    state.variant_seed = 0x12345678u;
    require(!HandleUnitProductionCompleteVoiceCue(
            state, produced, definition, bases),
        "zero-count production sound was not silent");
    require(!any_pending(state) && state.variant_seed == 0x12345678u,
        "silent production sound consumed RNG or queued playback");
}

void test_construction_complete_uses_group_eight() {
    GameplaySoundState state = hidden_loaded_sound_state();
    GameplayUnitSoundDefinition definition{};
    definition.sound_kind = kGameplayUnitSpecialSoundKind;
    definition.spawn_complete_count = 3;
    GameplayUnitSoundBaseSlots bases{};
    bases.spawn_complete = 500;
    UnitMovementUnit builder{};
    state.variant_seed = 5;

    require(HandleUnitSpawnCompleteVoiceCue(
            state, builder, definition, bases, 0, -333),
        "hidden builder did not queue construction-complete voice");
    const GameplaySoundRequest& request = state.requests[502];
    require(request.pending && request.slot_index == 502 &&
            request.volume == -20 && request.pan == -333,
        "construction completion did not use group-eight variant/position");
}

void test_produced_owner_feedback_gate() {
    require(UnitProductionCompleteFeedbackAllowed(1, 1),
        "local produced owner was rejected");
    require(!UnitProductionCompleteFeedbackAllowed(1, 2),
        "remote produced owner was accepted");
    require(UnitProductionCompleteFeedbackAllowed(2, 2),
        "feedback gate depends on a fixed player slot");
}

} // namespace

int main() {
    test_original_group_offsets_and_base_slots();
    test_production_complete_variant_and_silence_contract();
    test_construction_complete_uses_group_eight();
    test_produced_owner_feedback_gate();
    std::cout << "UNIT_PRODUCTION_SOUND_PASS "
                 "group7=production group8=construction hidden=audible "
                 "zero=silent+rng-preserved owner=produced-unit\n";
    return EXIT_SUCCESS;
}
