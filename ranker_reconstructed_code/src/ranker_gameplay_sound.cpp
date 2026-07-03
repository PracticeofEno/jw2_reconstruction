#include "ranker_gameplay_sound.h"

#include "ranker_directx.h"
#include "ranker_rng.h"

#include <algorithm>
#include <cstdlib>

namespace ranker {
namespace {

constexpr std::size_t kUnitSoundKindOffset = 0x014c;
constexpr std::size_t kUnitSoundAttackProbabilityOffset = 0x2420;
constexpr std::size_t kUnitSoundCountBaseOffset = 0x2424;
constexpr std::size_t kUnitSoundSpecialHealthyOffset = 0x2448;
constexpr std::size_t kUnitSoundSpecialConstructionOffset = 0x244c;
constexpr std::size_t kUnitSoundSpecialDamagedOffset = 0x2454;
constexpr std::size_t kUnitSoundSpecialDeathOffset = 0x2458;
constexpr std::size_t kUnitSoundHarvestFrameOffset = 0x245c;
constexpr std::size_t kUnitSoundAttackFrameBaseOffset = 0x249c;

u32 tile_index(const GameplaySoundTileMap& map, u32 tile_x, u32 tile_y) {
    return tile_y * map.width + tile_x;
}

u32 read_le_u32(const u8* bytes, std::size_t size, std::size_t offset) {
    if (bytes == nullptr || offset + 4 > size) {
        return 0;
    }
    return static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

u32 read_sound_count(const u8* bytes, std::size_t size, GameplayUnitSoundGroup group) {
    return read_le_u32(bytes, size,
        kUnitSoundCountBaseOffset + static_cast<u32>(group) * 4u);
}

u32 normalize_sound_slot(u32 slot) {
    return slot == kInvalidGameplaySoundSlot ? kInvalidGameplaySoundSlot : slot;
}

u32 sound_slot_group(const std::array<u32, kGameplayUnitSoundGroupCount>& slots,
    GameplayUnitSoundGroup group) {
    return slots[static_cast<std::size_t>(group)];
}

bool get_tile_flags(const GameplaySoundTileMap& map, i32 x, i32 y, u32& flags) {
    if (x < 0 || y < 0 || map.width == 0 || map.height == 0) {
        return false;
    }
    if (map.width == 1 && map.height == 1 && map.flags.size() == 1) {
        flags = map.flags[0];
        return true;
    }
    const u32 tile_x = static_cast<u32>(x) >> 5;
    const u32 tile_y = static_cast<u32>(y) >> 5;
    if (tile_x >= map.width || tile_y >= map.height) {
        return false;
    }
    const u32 index = tile_index(map, tile_x, tile_y);
    if (index >= map.flags.size()) {
        return false;
    }
    flags = map.flags[index];
    return true;
}

bool calculate_request_volume(const GameplaySoundState& state, i32 world_delta,
    i32& volume) {
    const i32 adjusted = world_delta + state.listener_position_offset;
    if (adjusted <= kGameplaySoundMinimumAudiblePosition) {
        return false;
    }

    if (!state.attenuation_by_bucket.empty()) {
        const u32 bucket = static_cast<u32>(adjusted - kGameplaySoundMinimumAudiblePosition) /
            0x271u;
        volume = state.attenuation_by_bucket[
            std::min<u32>(bucket, static_cast<u32>(state.attenuation_by_bucket.size() - 1))];
        return true;
    }

    const i32 distance = std::min<i32>(std::abs(adjusted), 10000);
    volume = -distance;
    return true;
}

struct GameplaySoundSpatialCue {
    i32 world_delta = kGameplaySoundMinimumAudiblePosition;
    i32 pan = 0;
};

GameplaySoundSpatialCue calculate_visible_sound_spatial_cue(
    const GameplaySoundState& state, i32 x, i32 y) {
    const i32 centered_x = x - state.camera_x - state.viewport_center_x;
    const i32 centered_y = y - state.camera_y - state.viewport_center_y;
    const i32 distance = std::max(std::abs(centered_x), std::abs(centered_y));
    const long long scaled = -static_cast<long long>(distance) * 8;
    return {
        static_cast<i32>(std::max<long long>(
            scaled, kGameplaySoundMinimumAudiblePosition)),
        centered_x,
    };
}

GameplaySoundSpatialCue visible_sound_spatial_cue_or_silent(
    const GameplaySoundState& state, i32 x, i32 y, bool visible) {
    if (!visible) {
        return {kGameplaySoundMinimumAudiblePosition, 0};
    }
    return calculate_visible_sound_spatial_cue(state, x, y);
}

bool is_valid_sound_slot(u32 slot_index) {
    return slot_index != kInvalidGameplaySoundSlot &&
        slot_index < kGameplaySoundRequestSlots;
}

bool throttle_allows(GameplaySoundState& state, const UnitMovementUnit& unit,
    bool& valid, u32& last_tick, const UnitMovementUnit*& tracked_unit) {
    if (valid && state.current_tick - last_tick < kGameplaySoundResponseThrottleTicks) {
        return false;
    }
    valid = true;
    last_tick = state.current_tick;
    tracked_unit = &unit;
    return true;
}

u32 resolve_variant_slot(GameplaySoundState& state, u32 base_slot, u32 variant_count) {
    if (variant_count == 0 || !is_valid_sound_slot(base_slot)) {
        return kInvalidGameplaySoundSlot;
    }
    return base_slot + SelectGameplaySoundVariant(state, variant_count);
}

bool queue_sound_slot(GameplaySoundState& state, u32 slot_index, i32 world_delta = 0,
    i32 pan = 0) {
    if (!is_valid_sound_slot(slot_index)) {
        return false;
    }
    return HandlePositionalGameplaySoundQueueRequest(state, slot_index, world_delta, pan);
}

bool queue_visible_sound_slot(GameplaySoundState& state, const UnitMovementUnit& unit,
    u32 slot_index, i32 world_delta = 0, i32 pan = 0) {
    (void)world_delta;
    (void)pan;
    if (!is_valid_sound_slot(slot_index)) {
        return false;
    }
    const GameplaySoundSpatialCue spatial =
        visible_sound_spatial_cue_or_silent(state, unit.x, unit.y,
            CheckCurrentUnitSoundTileVisible(state, unit.x, unit.y));
    return HandlePositionalGameplaySoundQueueRequest(state, slot_index,
        spatial.world_delta, spatial.pan);
}

bool attack_frame_matches(const GameplayUnitSoundDefinition& definition, u32 frame) {
    for (i32 candidate : definition.attack_sound_frames) {
        if (candidate == -1) {
            return false;
        }
        if (candidate == static_cast<i32>(frame)) {
            return true;
        }
    }
    return false;
}

void play_sound_request(const GameplaySoundRequest& request) {
#ifdef _WIN32
    SetCurrentDirectSoundBufferPlaybackState(
        request.slot_index, request.volume, request.pan);
    AdjustCurrentSoundBufferVolume();
    AdjustCurrentSoundBufferPan();
    DuplicateAndPlayReservedSoundBuffer();
#else
    (void)request;
#endif
}

void play_sound_slot_direct(const GameplaySoundRequest& request) {
#ifdef _WIN32
    SetCurrentDirectSoundBufferPlaybackState(
        request.slot_index, request.volume, request.pan);
    AdjustCurrentSoundBufferVolume();
    AdjustCurrentSoundBufferPan();
    PlayCurrentSoundBufferSlot();
#else
    (void)request;
#endif
}

GameplaySoundState* g_default_frontend_sound_state = nullptr;

} // namespace

bool DecodeGameplayUnitSoundDefinition(const u8* definition_bytes,
    std::size_t definition_size, GameplayUnitSoundDefinition& definition) {
    if (definition_bytes == nullptr ||
        definition_size < kUnitSoundHarvestFrameOffset + sizeof(u32)) {
        return false;
    }

    definition = GameplayUnitSoundDefinition{};
    definition.sound_kind = read_le_u32(definition_bytes, definition_size,
        kUnitSoundKindOffset);
    definition.selected_response_count = read_sound_count(definition_bytes, definition_size,
        GameplayUnitSoundGroup::SelectedResponse);
    definition.command_response_count = read_sound_count(definition_bytes, definition_size,
        GameplayUnitSoundGroup::CommandResponse);
    definition.attack_count = read_sound_count(definition_bytes, definition_size,
        GameplayUnitSoundGroup::Attack);
    definition.death_count = read_sound_count(definition_bytes, definition_size,
        GameplayUnitSoundGroup::Death);
    definition.harvest_work_count = read_sound_count(definition_bytes, definition_size,
        GameplayUnitSoundGroup::HarvestWork);
    definition.production_complete_count = read_sound_count(definition_bytes,
        definition_size, GameplayUnitSoundGroup::ProductionComplete);
    definition.spawn_complete_count = read_sound_count(definition_bytes, definition_size,
        GameplayUnitSoundGroup::SpawnComplete);
    definition.attack_probability_gate = read_le_u32(definition_bytes, definition_size,
        kUnitSoundAttackProbabilityOffset);
    definition.special_healthy_response_slot = normalize_sound_slot(
        read_le_u32(definition_bytes, definition_size, kUnitSoundSpecialHealthyOffset));
    definition.special_construction_response_slot = normalize_sound_slot(
        read_le_u32(definition_bytes, definition_size, kUnitSoundSpecialConstructionOffset));
    definition.special_damaged_response_slot = normalize_sound_slot(
        read_le_u32(definition_bytes, definition_size, kUnitSoundSpecialDamagedOffset));
    definition.special_death_slot = normalize_sound_slot(
        read_le_u32(definition_bytes, definition_size, kUnitSoundSpecialDeathOffset));
    definition.harvest_sound_frame = normalize_sound_slot(
        read_le_u32(definition_bytes, definition_size, kUnitSoundHarvestFrameOffset));

    for (std::size_t i = 0; i < definition.attack_sound_frames.size(); ++i) {
        const u32 raw = read_le_u32(definition_bytes, definition_size,
            kUnitSoundAttackFrameBaseOffset + i * sizeof(u32));
        definition.attack_sound_frames[i] =
            raw == kInvalidGameplaySoundSlot ? -1 : static_cast<i32>(raw);
    }
    return true;
}

GameplayUnitSoundBaseSlots BuildGameplayUnitSoundBaseSlots(
    const std::array<u32, kGameplayUnitSoundGroupCount>& first_sound_slots) {
    GameplayUnitSoundBaseSlots base_slots{};
    base_slots.selected_response = sound_slot_group(first_sound_slots,
        GameplayUnitSoundGroup::SelectedResponse);
    base_slots.command_response = sound_slot_group(first_sound_slots,
        GameplayUnitSoundGroup::CommandResponse);
    base_slots.attack = sound_slot_group(first_sound_slots, GameplayUnitSoundGroup::Attack);
    base_slots.death = sound_slot_group(first_sound_slots, GameplayUnitSoundGroup::Death);
    base_slots.harvest_work = sound_slot_group(first_sound_slots,
        GameplayUnitSoundGroup::HarvestWork);
    base_slots.production_complete = sound_slot_group(first_sound_slots,
        GameplayUnitSoundGroup::ProductionComplete);
    base_slots.spawn_complete = sound_slot_group(first_sound_slots,
        GameplayUnitSoundGroup::SpawnComplete);
    return base_slots;
}

void InitializeDefaultGameplaySoundAttenuation(GameplaySoundState& state) {
    static constexpr std::array<i32, 17> kOriginalAttenuationTable = {
        -10000, -4000, -3500, -2800, -2200, -1600, -1300, -1100, -900,
        -700, -550, -400, -300, -200, -100, 0, 0,
    };
    state.attenuation_by_bucket.assign(
        kOriginalAttenuationTable.begin(), kOriginalAttenuationTable.end());
}

bool InitializeGameplaySoundEffectBank(GameplaySoundState& state,
    const char* archive_name) {
    if (!state.direct_sound_available) {
        return true;
    }
    if (archive_name == nullptr) {
        return false;
    }

#ifdef _WIN32
    ReleaseAllDirectSoundBufferSlots();
    state.bank_loaded = false;
    state.loaded_record_count = 0;
    for (u32 record = 0; record < kGameplaySoundBankRecordCount; ++record) {
        SetNextSoundBufferStaticFlag();
        const u32 slot = LoadTrcWaveRecordIntoSoundBufferSlot(archive_name, record);
        if (slot == kInvalidGameplaySoundSlot) {
            return false;
        }
        ++state.loaded_record_count;
    }
    state.bank_loaded = true;
    return true;
#else
    (void)archive_name;
    state.bank_loaded = false;
    return false;
#endif
}

void SetDefaultFrontendGameplaySoundState(GameplaySoundState* state) {
    g_default_frontend_sound_state = state;
}

GameplaySoundState* DefaultFrontendGameplaySoundState() {
    return g_default_frontend_sound_state;
}

u32 SelectGameplaySoundVariant(GameplaySoundState& state, u32 variant_count) {
    if (variant_count == 0) {
        return 0;
    }

    const u32 quotient = state.variant_seed / variant_count;
    const u32 variant = state.variant_seed % variant_count;
    state.variant_seed += quotient;
    state.variant_seed ^=
        ReadOriginalRandomScrambleDword(state.variant_scramble,
            (quotient >> 28) & 0x0f) + 1u;
    return variant;
}

void HandleQueuedGameplaySoundPlayback(GameplaySoundState& state) {
    for (u32 slot = 0; slot < state.requests.size(); ++slot) {
        GameplaySoundRequest& request = state.requests[slot];
        if (!request.pending) {
            continue;
        }
        request.pending = false;
        request.slot_index = slot;
        play_sound_request(request);
    }
}

bool HandlePositionalGameplaySoundQueueRequest(GameplaySoundState& state,
    u32 slot_index, i32 world_delta, i32 pan) {
    if (!state.bank_loaded || slot_index >= state.requests.size()) {
        return false;
    }

    i32 volume = 0;
    if (!calculate_request_volume(state, world_delta, volume)) {
        return false;
    }

    GameplaySoundRequest& request = state.requests[slot_index];
    if (!request.pending || request.volume < volume) {
        request.pending = true;
        request.slot_index = slot_index;
        request.volume = volume;
        request.pan = pan;
    }
    return true;
}

bool HandleImmediatePositionalGameplaySound(GameplaySoundState& state,
    u32 slot_index, i32 world_delta, i32 pan) {
    if (!state.bank_loaded || slot_index >= kGameplaySoundRequestSlots) {
        return false;
    }

    i32 volume = 0;
    if (!calculate_request_volume(state, world_delta, volume)) {
        return false;
    }

    play_sound_request(GameplaySoundRequest{true, slot_index, volume, pan});
    return true;
}

bool CheckCurrentUnitSoundTileVisible(const GameplaySoundState& state, i32 x, i32 y) {
    u32 flags = 0;
    return get_tile_flags(state.visibility_map, x, y, flags) &&
        (flags & state.visibility_map.visible_flag) != 0;
}

bool CheckWorldPointSoundTileVisible(const GameplaySoundState& state, i32 x, i32 y) {
    return CheckCurrentUnitSoundTileVisible(state, x, y);
}

bool HandleCurrentGameplaySoundQueued(GameplaySoundState& state, u32 slot_index,
    i32 world_delta, i32 pan) {
    return HandlePositionalGameplaySoundQueueRequest(state, slot_index, world_delta, pan);
}

bool HandleCurrentGameplaySoundImmediate(GameplaySoundState& state, u32 slot_index,
    i32 world_delta, i32 pan) {
    return HandleImmediatePositionalGameplaySound(state, slot_index, world_delta, pan);
}

void HandleVisibleCurrentTileGameplaySoundQueued(GameplaySoundState& state,
    u32 slot_index, i32 x, i32 y, i32 world_delta, i32 pan) {
    (void)world_delta;
    (void)pan;
    const GameplaySoundSpatialCue spatial =
        visible_sound_spatial_cue_or_silent(state, x, y,
            CheckCurrentUnitSoundTileVisible(state, x, y));
    HandlePositionalGameplaySoundQueueRequest(
        state, slot_index, spatial.world_delta, spatial.pan);
}

void HandleVisibleWorldPointGameplaySoundQueued(GameplaySoundState& state,
    u32 slot_index, i32 x, i32 y, i32 world_delta, i32 pan) {
    (void)world_delta;
    (void)pan;
    const GameplaySoundSpatialCue spatial =
        visible_sound_spatial_cue_or_silent(state, x, y,
            CheckWorldPointSoundTileVisible(state, x, y));
    HandlePositionalGameplaySoundQueueRequest(
        state, slot_index, spatial.world_delta, spatial.pan);
}

bool HandleImmediateUiGameplaySound(GameplaySoundState& state, u32 slot_index,
    i32 world_delta) {
    (void)slot_index;
    return HandleImmediatePositionalGameplaySound(state, 0, world_delta, 0);
}

bool HandleUiClickSoundMirror(GameplaySoundState& state, u32 slot_index,
    i32 world_delta) {
    return HandleImmediateUiGameplaySound(state, slot_index, world_delta);
}

bool HandleDefaultFrontendUiClickSound(u32 slot_index, i32 world_delta) {
    GameplaySoundState* state = DefaultFrontendGameplaySoundState();
    return state != nullptr && HandleUiClickSoundMirror(*state, slot_index, world_delta);
}

bool HandleImmediateMenuGameplaySound(GameplaySoundState& state, u32 slot_index,
    i32 world_delta) {
    (void)slot_index;
    return HandleImmediatePositionalGameplaySound(state, 1, world_delta, 0);
}

bool HandleImmediateLobbyMessageGameplaySound(GameplaySoundState& state,
    u32 slot_index, i32 world_delta) {
    (void)slot_index;
    return HandleImmediatePositionalGameplaySound(state, 4, world_delta, 0);
}

bool HandleImmediateLobbyTimerGameplaySound(GameplaySoundState& state,
    u32 slot_index, i32 world_delta) {
    (void)slot_index;
    return HandleImmediatePositionalGameplaySound(state, 5, world_delta, 0);
}

bool HandleImmediateDialogGameplaySound(GameplaySoundState& state, u32 slot_index,
    i32 world_delta) {
    return HandleImmediatePositionalGameplaySound(
        state, slot_index + 6u, world_delta, 0);
}

bool HandleSelectedUnitVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    if (!state.bank_loaded) {
        return false;
    }

    if (definition.sound_kind == kGameplayUnitSpecialSoundKind) {
        u32 slot = definition.special_construction_response_slot;
        if (unit.action_mode_gate != 1) {
            const u32 healthy_threshold = unit.max_health - (unit.max_health >> 2);
            slot = unit.health < healthy_threshold ?
                definition.special_damaged_response_slot :
                definition.special_healthy_response_slot;
        }
        return queue_sound_slot(state, slot, world_delta, pan);
    }

    if (definition.selected_response_count == 0 ||
        !throttle_allows(state, unit, state.selected_response_throttle_valid,
            state.selected_response_last_tick, state.selected_response_unit)) {
        return false;
    }

    return queue_sound_slot(state, resolve_variant_slot(state,
        base_slots.selected_response, definition.selected_response_count), world_delta, pan);
}

bool HandleCommandAcknowledgementVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    if (!state.bank_loaded || definition.command_response_count == 0 ||
        !throttle_allows(state, unit, state.command_response_throttle_valid,
            state.command_response_last_tick, state.command_response_unit)) {
        return false;
    }

    return queue_sound_slot(state, resolve_variant_slot(state,
        base_slots.command_response, definition.command_response_count), world_delta, pan);
}

bool HandleUnitProductionCompleteVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    (void)unit;
    if (!state.bank_loaded) {
        return false;
    }
    return queue_sound_slot(state, resolve_variant_slot(state,
        base_slots.production_complete, definition.production_complete_count), world_delta, pan);
}

bool HandleUnitDeathVoiceCue(GameplaySoundState& state, const UnitMovementUnit& unit,
    const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    if (!state.bank_loaded) {
        return false;
    }

    if (definition.sound_kind == kGameplayUnitSpecialSoundKind) {
        return queue_visible_sound_slot(state, unit, definition.special_death_slot,
            world_delta, pan);
    }

    return queue_visible_sound_slot(state, unit, resolve_variant_slot(state,
        base_slots.death, definition.death_count), world_delta, pan);
}

bool HandleUnitSpawnCompleteVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    (void)unit;
    if (!state.bank_loaded) {
        return false;
    }
    return queue_sound_slot(state, resolve_variant_slot(state,
        base_slots.spawn_complete, definition.spawn_complete_count), world_delta, pan);
}

bool HandleUnitAttackFrameVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    if (!state.bank_loaded || definition.attack_count == 0 ||
        !attack_frame_matches(definition, unit.animation_frame)) {
        return false;
    }

    if (definition.attack_probability_gate > 1 &&
        SelectGameplaySoundVariant(state, definition.attack_probability_gate) != 0) {
        return false;
    }

    return queue_visible_sound_slot(state, unit, resolve_variant_slot(state,
        base_slots.attack, definition.attack_count), world_delta, pan);
}

bool HandleUnitHitReactionVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    if (!state.bank_loaded) {
        return false;
    }
    return queue_visible_sound_slot(state, unit, resolve_variant_slot(state,
        base_slots.selected_response, definition.selected_response_count), world_delta, pan);
}

bool HandleSilentDeathStateSoundCue(GameplaySoundState& state,
    const UnitMovementUnit& unit) {
    (void)state;
    (void)unit;
    return false;
}

bool HandleSilentUnitSoundCue(GameplaySoundState& state,
    const UnitMovementUnit& unit) {
    (void)state;
    (void)unit;
    return false;
}

bool HandleWorkerHarvestFrameVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta, i32 pan) {
    if (!state.bank_loaded || unit.animation_frame != definition.harvest_sound_frame) {
        return false;
    }
    return queue_visible_sound_slot(state, unit, resolve_variant_slot(state,
        base_slots.harvest_work, definition.harvest_work_count), world_delta, pan);
}

bool HandleBuildRepairProgressSoundCue(GameplaySoundState& state,
    const UnitMovementUnit& unit) {
    (void)state;
    (void)unit;
    return false;
}

bool HandleIndexedTwoVariantGameplaySoundCue(GameplaySoundState& state,
    u32 base_index, i32 world_delta) {
    (void)world_delta;
    if (!state.bank_loaded) {
        return false;
    }
    const u32 slot = base_index * 2 + 9 + SelectGameplaySoundVariant(state, 2);
    return HandlePositionalGameplaySoundQueueRequest(state, slot, 0, 0);
}

bool HandleImmediateGameplaySoundSlotWithPan(GameplaySoundState& state,
    u32 slot_index, i32 world_delta, i32 pan) {
    if (!state.bank_loaded || slot_index == kInvalidGameplaySoundSlot) {
        return false;
    }

    i32 volume = 0;
    if (!calculate_request_volume(state, world_delta, volume)) {
        return false;
    }

    play_sound_slot_direct(GameplaySoundRequest{true, slot_index, volume, pan});
    return true;
}

} // namespace ranker
