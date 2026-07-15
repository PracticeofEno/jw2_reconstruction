#include "ranker_gameplay_sound.h"
#include "ranker_directx.h"

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
        std::cerr << "PRODUCTION_SOUND_MATRIX_FAIL: " << message << '\n';
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
    require(offset + 4 <= bytes.size(), "test definition write overflow");
    bytes[offset + 0] = static_cast<u8>(value & 0xffu);
    bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xffu);
}

GameplaySoundState make_sound() {
    GameplaySoundState state{};
    state.bank_loaded = true;
    state.listener_position_offset = 20;
    // Both completion functions in the original bypass visibility.  Keep the
    // fixture explicitly hidden so an accidental visible-only rewrite is red.
    state.visibility_map.width = 1;
    state.visibility_map.height = 1;
    state.visibility_map.flags = {0};
    return state;
}

void test_original_seed_and_first_six_variants() {
    GameplaySoundState state{};
    require(state.variant_seed == 1,
        "gameplay sound variant seed must preserve original PE initializer one");

    static constexpr u32 expected[] = {1, 1, 2, 2, 1, 0};
    for (u32 variant : expected) {
        require(SelectGameplaySoundVariant(state, 3) == variant,
            "first six three-way variants differ from original selector sequence");
    }
    require(state.variant_seed == 0xc804e3d1u,
        "variant seed after first six three-way selections is wrong");
}

void test_bootstrap_session_seed_copy() {
    GameplaySoundState bootstrap{};
    require(SelectGameplaySoundVariant(bootstrap, 3) == 1,
        "bootstrap first sound did not start from original seed");
    const u32 seed_after_frontend_sound = bootstrap.variant_seed;

    GameplaySoundState session{};
    session = bootstrap;
    require(session.variant_seed == seed_after_frontend_sound,
        "session sound state did not preserve the bootstrap RNG seed");
    require(SelectGameplaySoundVariant(session, 3) == 1,
        "session restarted the sound variant sequence instead of continuing it");
}

void test_live_definition_profile_preserves_catalog_sound_slots() {
    constexpr std::size_t definition_bytes = 0x24bcu;
    constexpr std::size_t sound_count_base = 0x2424u;
    constexpr std::size_t special_healthy = 0x2448u;
    std::vector<u8> catalog(definition_bytes, 0);
    std::vector<u8> live(definition_bytes, 0);

    write_le32(catalog, sound_count_base + 7u * 4u, 1);
    write_le32(catalog, sound_count_base + 8u * 4u, 2);
    write_le32(catalog, special_healthy, 0x111u);
    write_le32(live, sound_count_base + 7u * 4u, 4);
    write_le32(live, sound_count_base + 8u * 4u, 5);
    write_le32(live, special_healthy, 0x222u);

    GameplayUnitSoundDefinition profile{};
    require(DecodeGameplayUnitSoundDefinition(
            catalog.data(), catalog.size(), profile),
        "catalog sound profile decode failed");
    std::array<u32, kGameplayUnitSoundGroupCount> resource_slots{};
    resource_slots[7] = 401;
    resource_slots[8] = 501;
    const GameplayUnitSoundBaseSlots slots =
        BuildGameplayUnitSoundBaseSlots(resource_slots);

    GameplayUnitSoundDefinition live_profile{};
    require(DecodeGameplayUnitSoundDefinition(
            live.data(), live.size(), live_profile),
        "live sound profile decode failed");
    profile = live_profile;

    require(profile.production_complete_count == 4 &&
            profile.spawn_complete_count == 5 &&
            profile.special_healthy_response_slot == 0x222u,
        "live US_UB sound definition fields were not selected");
    require(slots.production_complete == 401 && slots.spawn_complete == 501,
        "live definition override relocated catalog-owned sound slots");
}

void test_group_seven_production_completion() {
    GameplaySoundState state = make_sound();
    GameplayUnitSoundDefinition definition{};
    GameplayUnitSoundBaseSlots bases{};
    UnitMovementUnit unit{};
    unit.x = 0x400;
    unit.y = 0x500;

    bases.production_complete = 300;
    definition.sound_kind = kGameplayUnitSpecialSoundKind;
    definition.special_healthy_response_slot = 17;
    definition.special_construction_response_slot = 18;
    definition.production_complete_count = 2;
    state.variant_seed = 3; // original selector: 3 % 2 == variant 1

    require(HandleUnitProductionCompleteVoiceCue(
            state, unit, definition, bases, -120, 444),
        "hidden special-kind produced unit must still queue group seven");
    const GameplaySoundRequest& request = state.requests[301];
    require(request.pending && request.slot_index == 301,
        "group-seven base plus selected variant is wrong");
    require(request.volume == -100 && request.pan == 444,
        "group-seven positional arguments were not preserved");
    require(!state.requests[17].pending && !state.requests[18].pending,
        "production completion incorrectly used special response slots");

    state = make_sound();
    definition.production_complete_count = 0;
    state.variant_seed = 0x12345678u;
    require(!HandleUnitProductionCompleteVoiceCue(
            state, unit, definition, bases),
        "zero-count group seven must be silent");
    require(!any_pending(state) && state.variant_seed == 0x12345678u,
        "zero-count group seven consumed RNG or queued a request");
}

void test_group_eight_spawn_completion() {
    GameplaySoundState state = make_sound();
    GameplayUnitSoundDefinition definition{};
    GameplayUnitSoundBaseSlots bases{};
    UnitMovementUnit builder{};
    builder.x = 0x600;
    builder.y = 0x700;

    bases.spawn_complete = 500;
    definition.sound_kind = kGameplayUnitSpecialSoundKind;
    definition.special_construction_response_slot = 18;
    definition.spawn_complete_count = 3;
    state.variant_seed = 5; // original selector: 5 % 3 == variant 2

    require(HandleUnitSpawnCompleteVoiceCue(
            state, builder, definition, bases, 0, -333),
        "hidden special-kind builder must still queue group eight");
    const GameplaySoundRequest& request = state.requests[502];
    require(request.pending && request.slot_index == 502,
        "group-eight base plus selected variant is wrong");
    require(request.volume == -20 && request.pan == -333,
        "group-eight positional arguments were not preserved");
    require(!state.requests[18].pending,
        "spawn completion incorrectly used special construction slot");

    state = make_sound();
    definition.spawn_complete_count = 0;
    state.variant_seed = 0x87654321u;
    require(!HandleUnitSpawnCompleteVoiceCue(
            state, builder, definition, bases),
        "zero-count group eight must be silent");
    require(!any_pending(state) && state.variant_seed == 0x87654321u,
        "zero-count group eight consumed RNG or queued a request");
}

void test_bank_gate() {
    GameplaySoundState state = make_sound();
    state.bank_loaded = false;
    state.variant_seed = 0x10203040u;
    GameplayUnitSoundDefinition definition{};
    definition.production_complete_count = 2;
    definition.spawn_complete_count = 2;
    GameplayUnitSoundBaseSlots bases{};
    bases.production_complete = 300;
    bases.spawn_complete = 500;
    UnitMovementUnit unit{};

    require(!HandleUnitProductionCompleteVoiceCue(
            state, unit, definition, bases) &&
            !HandleUnitSpawnCompleteVoiceCue(
                state, unit, definition, bases),
        "unloaded sound bank must suppress both completion groups");
    require(!any_pending(state) && state.variant_seed == 0x10203040u,
        "bank gate consumed RNG or queued completion audio");
}

} // namespace

int main() {
    test_original_seed_and_first_six_variants();
    test_bootstrap_session_seed_copy();
    test_live_definition_profile_preserves_catalog_sound_slots();
    test_group_seven_production_completion();
    test_group_eight_spawn_completion();
    test_bank_gate();
    std::cout << "PRODUCTION_SOUND_MATRIX_PASS "
                 "seed=1 sequence6=preserved bootstrap=session "
                 "live_profile=override base_slots=catalog "
                 "group7=hidden+special+variant+zero "
                 "group8=hidden+special+variant+zero bank=gate\n";
    return EXIT_SUCCESS;
}
