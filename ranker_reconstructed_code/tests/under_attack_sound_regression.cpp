#include "ranker_directx.h"
#include "ranker_gameplay_sound.h"

#include <cstdlib>
#include <iostream>

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
        std::cerr << "UNDER_ATTACK_SOUND_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

GameplaySoundState loaded_sound_state(u32 variant_seed) {
    GameplaySoundState state{};
    state.bank_loaded = true;
    state.variant_seed = variant_seed;
    return state;
}

void test_raw_owner_faction_is_not_clamped() {
    // HandleIndexedTwoVariantSoundMirror (0x004d0d16) resolves
    // raw_faction * 2 + 9 + random(2).  NotifyLocalPlayerUnitUnderAttack
    // loads raw_faction from [owner * 4 + 0x7251a4] without a range clamp.
    GameplaySoundState state = loaded_sound_state(0);
    require(HandleLocalUnderAttackGameplaySoundCue(state, 7),
        "raw faction seven cue was rejected");
    require(state.requests[23].pending &&
            state.requests[23].slot_index == 23,
        "raw faction seven variant zero did not select slot 23");
    require(!state.requests[15].pending && !state.requests[16].pending,
        "raw faction seven was clamped to lobby faction three");

    state = loaded_sound_state(1);
    require(HandleLocalUnderAttackGameplaySoundCue(state, 7),
        "raw faction seven variant-one cue was rejected");
    require(state.requests[24].pending &&
            state.requests[24].slot_index == 24,
        "raw faction seven variant one did not select slot 24");
}

void test_normal_lobby_faction_mapping_is_unchanged() {
    GameplaySoundState state = loaded_sound_state(0);
    require(HandleLocalUnderAttackGameplaySoundCue(state, 3),
        "normal faction three cue was rejected");
    require(state.requests[15].pending &&
            state.requests[15].slot_index == 15,
        "normal faction three did not retain slot 15 mapping");
}

} // namespace

int main() {
    test_raw_owner_faction_is_not_clamped();
    test_normal_lobby_faction_mapping_is_unchanged();
    std::cout << "UNDER_ATTACK_SOUND_PASS raw_faction=preserved "
                 "slots=faction*2+9+variant normal_mapping=unchanged\n";
    return EXIT_SUCCESS;
}
