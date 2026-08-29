#pragma once

#include "ranker_player_slots.h"
#include "ranker_types.h"

#include <array>
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

constexpr bool ShouldRefreshGameplayEndConditionSnapshot(
    u32 frame_counter, bool scenario_ai_profile_override) {
    // FUN_004d55c0 tests the 64-frame cadence and the scenario override before
    // it walks the active-unit list.  Keeping the same gate outside the typed
    // snapshot builder avoids copying every Use Map Setting unit on the other
    // 63 frames while preserving the monitor's original decisions.
    return (frame_counter & 0x3fu) == 0 && !scenario_ai_profile_override;
}

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
    // Building-elimination rule (reconstruction addition, user-requested):
    // an owner that has held at least one building and now holds none is
    // eliminated — its surviving mobile units no longer keep it alive for
    // anyone's victory or defeat test.  Use Map Setting maps import unit-based
    // masks (0x1 = any unit), which let a razed Computer stall the game on a
    // stray unit.  Derived from the simulation snapshot on the same 64-frame
    // cadence on every peer (deterministic).  Owners that never had a
    // building (unit-only scenario players) are never eliminated this way.
    std::array<bool, kPlayerSlotCount> owner_had_building{};
    std::array<bool, kPlayerSlotCount> owner_eliminated{};
};

void ApplyGameplayEndConditionSessionModeDefaults(
    GameplayEndConditionState& state);
void TickGameplayEndConditionMonitor(GameplayEndConditionState& state);
// Recompute owner_had_building / owner_eliminated from the active-unit
// snapshot (called by the monitor tick; exposed for tests).
void RefreshGameplayEndOwnerElimination(GameplayEndConditionState& state);
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
