#include "ranker_gameplay_end_conditions.h"

#include <algorithm>

namespace ranker {
namespace {

u32 slot_bit(u32 slot) {
    return slot < 32 ? (1u << slot) : 0;
}

u32 local_slot_for_defeat(const GameplayEndConditionState& state) {
    return state.observer_owner_override ? state.observer_owner_slot : state.local_player_slot;
}

u32 normal_defeat_owner_slot(const GameplayEndConditionState& state) {
    return state.team_slot_rotation_enabled ?
        state.team_slot_rotation_anchor_slot : local_slot_for_defeat(state);
}

bool valid_player_slot(u32 slot) {
    return slot < kPlayerSlotCount;
}

bool local_player_is_observer(const GameplayEndConditionState& state) {
    if (state.players == nullptr || !valid_player_slot(state.local_player_slot)) {
        return false;
    }
    return state.players->slot_states[state.local_player_slot] ==
        static_cast<u8>(PlayerSlotState::observer);
}

bool active_competing_slot(const GameplayEndConditionState& state, u32 slot) {
    if (state.players == nullptr || !valid_player_slot(slot)) {
        return false;
    }
    const u8 slot_state = state.players->slot_states[slot];
    return slot_state != static_cast<u8>(PlayerSlotState::observer) &&
        slot_state != static_cast<u8>(PlayerSlotState::disabled);
}

bool live_unit(const GameplayEndUnit& unit) {
    return (unit.state_flags & kGameplayEndDeadUnitFlag) == 0;
}

bool non_elite_unit_type(u32 type_id) {
    return type_id < 0x60;
}

bool elite_unit_type(u32 type_id) {
    return type_id > kGameplayEndEliteTypeThreshold;
}

bool victory_elite_unit_type(u32 type_id) {
    return elite_unit_type(type_id) && type_id != kGameplayEndExcludedEliteType;
}

bool unit_owned_by(const GameplayEndUnit& unit, u32 owner) {
    return unit.owner_id == owner;
}

bool unit_owner_active_slot(u32 owner) {
    return owner < kPlayerSlotCount;
}

bool unit_matches_owner(const GameplayEndConditionState& state, u32 owner,
    bool (*type_predicate)(u32), bool exclude_special_elite) {
    for (const GameplayEndUnit* unit : state.active_units) {
        if (unit == nullptr || !live_unit(*unit) || !unit_owned_by(*unit, owner)) {
            continue;
        }
        if (exclude_special_elite && unit->type_id == kGameplayEndExcludedEliteType) {
            continue;
        }
        if (type_predicate == nullptr || type_predicate(unit->type_id)) {
            return true;
        }
    }
    return false;
}

bool active_list_has_normal_defeat_elite(
    const GameplayEndConditionState& state, u32 owner) {
    for (const GameplayEndUnit* unit : state.active_units) {
        // CheckNoLocalEliteUnitDefeat (0x004d561b) deliberately treats a
        // dead-flagged elite as present until the simulation list moves it
        // out of the active chain.  Scenario presence checks below use the
        // separate live-unit matcher.
        if (unit != nullptr && unit_owned_by(*unit, owner) &&
            victory_elite_unit_type(unit->type_id)) {
            return true;
        }
    }
    return false;
}

bool unit_matches_local(const GameplayEndConditionState& state,
    bool (*type_predicate)(u32), bool exclude_special_elite) {
    return unit_matches_owner(state, local_slot_for_defeat(state), type_predicate,
        exclude_special_elite);
}

bool relation_bit_set(const PlayerSlotRuntimeState& players, u32 owner, u32 slot) {
    if (!valid_player_slot(owner) || !valid_player_slot(slot)) {
        return false;
    }
    return (players.owner_relation_masks[owner] & slot_bit(slot)) != 0;
}

bool global_slot_active(const PlayerSlotRuntimeState& players, u32 slot) {
    return valid_player_slot(slot) && (players.global_active_slot_mask & slot_bit(slot)) != 0;
}

bool mutually_related_active_pair(const PlayerSlotRuntimeState& players, u32 a, u32 b) {
    return relation_bit_set(players, a, b) && relation_bit_set(players, b, a) &&
        global_slot_active(players, a) && global_slot_active(players, b);
}

bool hostile_to_local_strict(const GameplayEndConditionState& state, u32 owner) {
    if (state.players == nullptr || !unit_owner_active_slot(owner)) {
        return false;
    }
    const u32 local = state.local_player_slot;
    return !mutually_related_active_pair(*state.players, local, owner);
}

bool hostile_to_local_relation_only(const GameplayEndConditionState& state, u32 owner) {
    if (state.players == nullptr || !unit_owner_active_slot(owner)) {
        return false;
    }
    return !relation_bit_set(*state.players, owner, state.local_player_slot);
}

bool hostile_unit_exists(const GameplayEndConditionState& state,
    bool (*type_predicate)(u32), bool strict_relation) {
    for (const GameplayEndUnit* unit : state.active_units) {
        if (unit == nullptr || !unit_owner_active_slot(unit->owner_id)) {
            continue;
        }
        const bool hostile = strict_relation ?
            hostile_to_local_strict(state, unit->owner_id) :
            hostile_to_local_relation_only(state, unit->owner_id);
        if (hostile && (type_predicate == nullptr || type_predicate(unit->type_id))) {
            return true;
        }
    }
    return false;
}

bool hostile_unit_exists_for_elimination_victory(GameplayEndConditionState& state,
    bool (*type_predicate)(u32)) {
    if (CountActiveGameplayPlayerSlots(state) <= 1) {
        return false;
    }
    return hostile_unit_exists(state, type_predicate, true);
}

bool all_competing_slots_mutually_related(const GameplayEndConditionState& state) {
    if (state.players == nullptr) {
        return false;
    }

    u32 active_count = 0;
    for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
        if (active_competing_slot(state, slot)) {
            ++active_count;
        }
    }
    if (active_count <= 1) {
        return true;
    }

    for (u32 a = 0; a < kPlayerSlotCount; ++a) {
        if (!active_competing_slot(state, a)) {
            continue;
        }
        for (u32 b = 0; b < kPlayerSlotCount; ++b) {
            if (!active_competing_slot(state, b)) {
                continue;
            }
            if (!mutually_related_active_pair(*state.players, a, b)) {
                return false;
            }
        }
    }
    return true;
}

void mark_game_end(GameplayEndConditionState& state, u32 result_code) {
    state.end_requested = true;
    state.result_code = result_code;
}

void apply_scenario_defeat_mask(GameplayEndConditionState& state, u32 mask) {
    if ((mask & kGameplayEndConditionAnyUnit) != 0 && !CheckLocalHasAnyLiveUnit(state)) {
        mark_game_end(state, kGameplayEndResultDefeat);
        return;
    }
    if ((mask & kGameplayEndConditionNonEliteUnit) != 0 &&
        !CheckLocalHasAnyLiveNonEliteUnit(state)) {
        mark_game_end(state, kGameplayEndResultDefeat);
        return;
    }
    if ((mask & kGameplayEndConditionEliteUnit) != 0 &&
        !CheckLocalHasAnyLiveEliteUnit(state)) {
        mark_game_end(state, kGameplayEndResultDefeat);
    }
}

void apply_scenario_victory_mask(GameplayEndConditionState& state, u32 mask) {
    if ((mask & kGameplayEndConditionAnyUnit) != 0 && !CheckHostileUnitExists(state)) {
        mark_game_end(state, kGameplayEndResultVictory);
        return;
    }
    if ((mask & kGameplayEndConditionNonEliteUnit) != 0 &&
        !CheckHostileNonEliteUnitExists(state)) {
        mark_game_end(state, kGameplayEndResultVictory);
        return;
    }
    if ((mask & kGameplayEndConditionEliteUnit) != 0 &&
        !CheckHostileEliteUnitExists(state)) {
        mark_game_end(state, kGameplayEndResultVictory);
    }
}

} // namespace

void ApplyGameplayEndConditionSessionModeDefaults(
    GameplayEndConditionState& state) {
    // FUN_00426770 writes the ordinary building-elimination masks for every
    // built-in game type, but deliberately leaves mode 5's P_SCENA masks
    // untouched.  A zero mask in Use Map Setting is meaningful: maps such as
    // Garsian 2 finish exclusively through their trigger commands.
    switch (state.session_mode) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 7:
    case 8:
        state.scenario_defeat_condition_mask = kGameplayEndConditionEliteUnit;
        state.scenario_victory_condition_mask = kGameplayEndConditionEliteUnit;
        break;
    default:
        break;
    }
}

void TickGameplayEndConditionMonitor(GameplayEndConditionState& state) {
    if ((state.frame_counter & 0x3f) != 0 || state.scenario_ai_profile_override) {
        return;
    }

    if (state.generic_ai_profile_mode == 1) {
        CheckLocalDefeatCondition(state);
        // FUN_004d55c0 branches directly to the defeat handler when
        // FUN_004d55f8 raises DAT_00725c0b; it does not run the victory pass
        // in that tick.  Without this gate a disconnected final opponent can
        // make both predicates true and overwrite a local defeat with result
        // code 2 immediately before the result screen.
        if (state.end_requested) {
            return;
        }
        CheckLocalVictoryCondition(state);
        return;
    }

    CheckScenarioDefeatCondition(state);
    if (state.generic_ai_profile_mode != 0) {
        CheckScenarioVictoryCondition(state);
    }
}

void CheckLocalDefeatCondition(GameplayEndConditionState& state) {
    if (local_player_is_observer(state)) {
        return;
    }
    if (state.session_mode != 5) {
        CheckNoLocalEliteUnitDefeat(state);
        return;
    }
    CheckScenarioDefeatCondition(state);
}

void CheckNoLocalEliteUnitDefeat(GameplayEndConditionState& state) {
    if (!active_list_has_normal_defeat_elite(
            state, normal_defeat_owner_slot(state))) {
        mark_game_end(state, kGameplayEndResultDefeat);
    }
}

void CheckNoLocalNonEliteUnitDefeat(GameplayEndConditionState& state) {
    if (!unit_matches_local(state, non_elite_unit_type, false)) {
        mark_game_end(state, kGameplayEndResultDefeat);
    }
}

void CheckLocalVictoryCondition(GameplayEndConditionState& state) {
    if (local_player_is_observer(state)) {
        CheckAllActivePlayersAlliedVictory(state);
        return;
    }
    if (state.session_mode != 5) {
        CheckNoHostileEliteUnitVictory(state);
        return;
    }
    CheckScenarioVictoryCondition(state);
}

void CheckAllActivePlayersAlliedVictory(GameplayEndConditionState& state) {
    CountActiveGameplayPlayerSlots(state);
    if (all_competing_slots_mutually_related(state)) {
        mark_game_end(state, kGameplayEndResultAlliedOrEarlyVictory);
    }
}

void CheckNoHostileEliteUnitVictory(GameplayEndConditionState& state) {
    if (!hostile_unit_exists_for_elimination_victory(state, victory_elite_unit_type)) {
        const u32 result = state.frame_counter > 0x707 ?
            kGameplayEndResultVictory : kGameplayEndResultAlliedOrEarlyVictory;
        mark_game_end(state, result);
    }
}

void CheckNoHostileUnitVictory(GameplayEndConditionState& state) {
    if (!hostile_unit_exists_for_elimination_victory(state, nullptr)) {
        const u32 result = state.frame_counter > 0x707 ?
            kGameplayEndResultVictory : kGameplayEndResultAlliedOrEarlyVictory;
        mark_game_end(state, result);
    }
}

void CheckScenarioDefeatCondition(GameplayEndConditionState& state) {
    apply_scenario_defeat_mask(state, state.scenario_defeat_condition_mask);
}

bool CheckLocalHasAnyLiveUnit(const GameplayEndConditionState& state) {
    return unit_matches_local(state, nullptr, false);
}

bool CheckLocalHasAnyLiveNonEliteUnit(const GameplayEndConditionState& state) {
    return unit_matches_local(state, non_elite_unit_type, false);
}

bool CheckLocalHasAnyLiveEliteUnit(const GameplayEndConditionState& state) {
    return unit_matches_local(state, elite_unit_type, false);
}

void CheckScenarioVictoryCondition(GameplayEndConditionState& state) {
    apply_scenario_victory_mask(state, state.scenario_victory_condition_mask);
}

bool CheckHostileUnitExists(const GameplayEndConditionState& state) {
    return hostile_unit_exists(state, nullptr, false);
}

bool CheckHostileNonEliteUnitExists(const GameplayEndConditionState& state) {
    return hostile_unit_exists(state, non_elite_unit_type, false);
}

bool CheckHostileEliteUnitExists(const GameplayEndConditionState& state) {
    return hostile_unit_exists(state, elite_unit_type, false);
}

u32 CountActiveGameplayPlayerSlots(GameplayEndConditionState& state) {
    if (state.players == nullptr) {
        state.active_non_observer_slot_count = 0;
        return 0;
    }

    u32 count = 0;
    for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
        if (active_competing_slot(state, slot)) {
            ++count;
        }
    }
    state.active_non_observer_slot_count = count;
    return count;
}

}
