#pragma once

#include "ranker_player_slots.h"
#include "ranker_types.h"

#include <vector>

namespace ranker {

constexpr u32 kGameplayEndConditionAnyUnit = 0x01;
constexpr u32 kGameplayEndConditionNonEliteUnit = 0x02;
constexpr u32 kGameplayEndConditionEliteUnit = 0x04;
constexpr u32 kGameplayEndResultVictory = 0;
constexpr u32 kGameplayEndResultDefeat = 1;
constexpr u32 kGameplayEndResultAlliedOrEarlyVictory = 2;
constexpr u32 kGameplayEndEliteTypeThreshold = 0x5f;
constexpr u32 kGameplayEndExcludedEliteType = 0x6a;
constexpr u32 kGameplayEndDeadUnitFlag = 0x10000000;

struct GameplayEndUnit {
    u32 owner_id = 0;
    u32 type_id = 0;
    u32 state_flags = 0;
};

struct GameplayEndConditionState {
    PlayerSlotRuntimeState* players = nullptr;
    std::vector<const GameplayEndUnit*> active_units;
    u32 frame_counter = 0;
    u32 session_mode = 0;
    u32 generic_ai_profile_mode = 0;
    u32 local_player_slot = 0;
    u32 observer_owner_slot = 0;
    u32 team_slot_rotation_anchor_slot = 0;
    u32 scenario_defeat_condition_mask = 0;
    u32 scenario_victory_condition_mask = 0;
    u32 active_non_observer_slot_count = 0;
    u32 result_code = kGameplayEndResultVictory;
    bool scenario_ai_profile_override = false;
    bool observer_owner_override = false;
    bool team_slot_rotation_enabled = false;
    bool end_requested = false;
};

void TickGameplayEndConditionMonitor(GameplayEndConditionState& state);
void CheckLocalDefeatCondition(GameplayEndConditionState& state);
void CheckNoLocalEliteUnitDefeat(GameplayEndConditionState& state);
void CheckNoLocalNonEliteUnitDefeat(GameplayEndConditionState& state);
void CheckLocalVictoryCondition(GameplayEndConditionState& state);
void CheckAllActivePlayersAlliedVictory(GameplayEndConditionState& state);
void CheckNoHostileEliteUnitVictory(GameplayEndConditionState& state);
void CheckNoHostileUnitVictory(GameplayEndConditionState& state);
void CheckScenarioDefeatCondition(GameplayEndConditionState& state);
bool CheckLocalHasAnyLiveUnit(const GameplayEndConditionState& state);
bool CheckLocalHasAnyLiveNonEliteUnit(const GameplayEndConditionState& state);
bool CheckLocalHasAnyLiveEliteUnit(const GameplayEndConditionState& state);
void CheckScenarioVictoryCondition(GameplayEndConditionState& state);
bool CheckHostileUnitExists(const GameplayEndConditionState& state);
bool CheckHostileNonEliteUnitExists(const GameplayEndConditionState& state);
bool CheckHostileEliteUnitExists(const GameplayEndConditionState& state);
u32 CountActiveGameplayPlayerSlots(GameplayEndConditionState& state);

}
