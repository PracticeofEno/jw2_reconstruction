#pragma once

#include "ranker_types.h"
#include "ranker_unit_movement.h"

#include <array>
#include <cstddef>
#include <vector>

namespace ranker {

constexpr u32 kGameplaySoundRequestSlots = 0x32a;
constexpr u32 kGameplaySoundBankRecordCount = 0x21;
constexpr u32 kInvalidGameplaySoundSlot = 0xffffffffu;
constexpr u32 kDefaultUiClickSoundSlot = 0;
// Command validation failures use the original queued error cue, which is
// distinct from the immediate slot-zero button click played before dispatch.
constexpr u32 kGameplayCommandFailureSoundSlot = 2;
constexpr i32 kDefaultUiClickWorldDelta = 0;
constexpr i32 kGameplaySoundMinimumAudiblePosition = -10000;
constexpr u32 kGameplaySoundResponseThrottleTicks = 1000;
constexpr u32 kGameplayUnitSpecialSoundKind = 2;
constexpr u32 kGameplayUnitSoundGroupCount = 9;
constexpr u32 kGameplayUnitAttackSoundFrameSlots = 8;

enum class GameplayUnitSoundGroup : u32 {
    SelectedResponse = 0,
    CommandResponse = 1,
    Attack = 2,
    Unused3 = 3,
    Death = 4,
    HarvestWork = 5,
    Unused6 = 6,
    ProductionComplete = 7,
    SpawnComplete = 8,
};

struct GameplaySoundRequest {
    bool pending = false;
    u32 slot_index = 0;
    i32 volume = 0;
    i32 pan = 0;
};

struct GameplaySoundTileMap {
    u32 width = 0;
    u32 height = 0;
    u32 visible_flag = 0x08000000;
    std::vector<u32> flags;
};

struct GameplaySoundState {
    bool direct_sound_available = true;
    bool bank_loaded = false;
    u32 loaded_record_count = 0;
    i32 listener_position_offset = 0;
    i32 camera_x = 0;
    i32 camera_y = 0;
    i32 viewport_center_x = 400;
    i32 viewport_center_y = 300;
    u32 current_tick = 0;
    u32 variant_seed = 0;
    std::array<u32, 16> variant_scramble{
        0xa075a321u, 0xb304323cu, 0xc43a2059u, 0xd3d4f745u,
        0xe9999996u, 0xf124e654u, 0x158c6670u, 0x28374832u,
        0x3a576385u, 0x4748e52du, 0x54302323u, 0x696d376fu,
        0x7a323d17u, 0x81c97674u, 0x99a213eeu, 0xa4020f23u};
    u32 selected_response_last_tick = 0;
    u32 command_response_last_tick = 0;
    const UnitMovementUnit* selected_response_unit = nullptr;
    const UnitMovementUnit* command_response_unit = nullptr;
    bool selected_response_throttle_valid = false;
    bool command_response_throttle_valid = false;
    std::vector<i32> attenuation_by_bucket;
    std::array<GameplaySoundRequest, kGameplaySoundRequestSlots> requests{};
    GameplaySoundTileMap visibility_map;
};

struct GameplayUnitSoundBaseSlots {
    u32 selected_response = 0;
    u32 command_response = 0;
    u32 attack = 0;
    u32 death = 0;
    u32 harvest_work = 0;
    u32 production_complete = 0;
    u32 spawn_complete = 0;
};

struct GameplayUnitSoundDefinition {
    u32 sound_kind = 0;
    u32 selected_response_count = 0;
    u32 command_response_count = 0;
    u32 attack_count = 0;
    u32 death_count = 0;
    u32 harvest_work_count = 0;
    u32 production_complete_count = 0;
    u32 spawn_complete_count = 0;
    u32 attack_probability_gate = 0;
    u32 special_damaged_response_slot = kInvalidGameplaySoundSlot;
    u32 special_construction_response_slot = kInvalidGameplaySoundSlot;
    u32 special_healthy_response_slot = kInvalidGameplaySoundSlot;
    u32 special_death_slot = kInvalidGameplaySoundSlot;
    u32 harvest_sound_frame = kInvalidGameplaySoundSlot;
    std::array<i32, kGameplayUnitAttackSoundFrameSlots> attack_sound_frames{
        -1, -1, -1, -1, -1, -1, -1, -1};
};

bool DecodeGameplayUnitSoundDefinition(const u8* definition_bytes,
    std::size_t definition_size, GameplayUnitSoundDefinition& definition);
GameplayUnitSoundBaseSlots BuildGameplayUnitSoundBaseSlots(
    const std::array<u32, kGameplayUnitSoundGroupCount>& first_sound_slots);
void InitializeDefaultGameplaySoundAttenuation(GameplaySoundState& state);
bool InitializeGameplaySoundEffectBank(GameplaySoundState& state,
    const char* archive_name = "JW2_05.TRC");
void SetDefaultFrontendGameplaySoundState(GameplaySoundState* state);
GameplaySoundState* DefaultFrontendGameplaySoundState();
u32 SelectGameplaySoundVariant(GameplaySoundState& state, u32 variant_count);
void HandleQueuedGameplaySoundPlayback(GameplaySoundState& state);
bool HandlePositionalGameplaySoundQueueRequest(GameplaySoundState& state,
    u32 slot_index, i32 world_delta, i32 pan);
bool HandleImmediatePositionalGameplaySound(GameplaySoundState& state,
    u32 slot_index, i32 world_delta, i32 pan);
bool CheckCurrentUnitSoundTileVisible(const GameplaySoundState& state, i32 x, i32 y);
bool CheckWorldPointSoundTileVisible(const GameplaySoundState& state, i32 x, i32 y);
bool HandleCurrentGameplaySoundQueued(GameplaySoundState& state, u32 slot_index,
    i32 world_delta, i32 pan);
bool HandleCurrentGameplaySoundImmediate(GameplaySoundState& state, u32 slot_index,
    i32 world_delta, i32 pan);
void HandleVisibleCurrentTileGameplaySoundQueued(GameplaySoundState& state,
    u32 slot_index, i32 x, i32 y, i32 world_delta, i32 pan);
void HandleVisibleWorldPointGameplaySoundQueued(GameplaySoundState& state,
    u32 slot_index, i32 x, i32 y, i32 world_delta, i32 pan);
bool HandleImmediateUiGameplaySound(GameplaySoundState& state, u32 slot_index,
    i32 world_delta);
bool HandleUiClickSoundMirror(GameplaySoundState& state,
    u32 slot_index = kDefaultUiClickSoundSlot,
    i32 world_delta = kDefaultUiClickWorldDelta);
bool HandleDefaultFrontendUiClickSound(u32 slot_index = kDefaultUiClickSoundSlot,
    i32 world_delta = kDefaultUiClickWorldDelta);
bool HandleImmediateMenuGameplaySound(GameplaySoundState& state, u32 slot_index,
    i32 world_delta);
bool HandleImmediateLobbyMessageGameplaySound(GameplaySoundState& state,
    u32 slot_index, i32 world_delta);
bool HandleImmediateLobbyTimerGameplaySound(GameplaySoundState& state,
    u32 slot_index, i32 world_delta);
bool HandleImmediateDialogGameplaySound(GameplaySoundState& state, u32 slot_index,
    i32 world_delta);
bool HandleSelectedUnitVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleCommandAcknowledgementVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleUnitProductionCompleteVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleUnitDeathVoiceCue(GameplaySoundState& state, const UnitMovementUnit& unit,
    const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleUnitSpawnCompleteVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleUnitAttackFrameVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleUnitHitReactionVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleSilentDeathStateSoundCue(GameplaySoundState& state,
    const UnitMovementUnit& unit);
bool HandleSilentUnitSoundCue(GameplaySoundState& state,
    const UnitMovementUnit& unit);
bool HandleWorkerHarvestFrameVoiceCue(GameplaySoundState& state,
    const UnitMovementUnit& unit, const GameplayUnitSoundDefinition& definition,
    const GameplayUnitSoundBaseSlots& base_slots, i32 world_delta = 0, i32 pan = 0);
bool HandleBuildRepairProgressSoundCue(GameplaySoundState& state,
    const UnitMovementUnit& unit);
bool HandleIndexedTwoVariantGameplaySoundCue(GameplaySoundState& state,
    u32 base_index, i32 world_delta);
bool HandleImmediateGameplaySoundSlotWithPan(GameplaySoundState& state,
    u32 slot_index, i32 world_delta, i32 pan);

} // namespace ranker
