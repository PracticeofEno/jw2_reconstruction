#include "ranker_ai_autopilot.h"

#include "ranker_ai_expansion.h"

#include <algorithm>

namespace ranker {
namespace {

constexpr u32 kWorkerType = 0x20u;
constexpr u32 kEggNestType = 0x84u;
constexpr u32 kPopulationNestType = 0x82u;
constexpr u32 kBuildingTypeStart = 0x60u;

bool legal(const AiRlStepEncoding& encoding, AiRlHighLevelAction action) {
    return encoding.legal_mask[static_cast<std::size_t>(action)] != 0;
}

bool is_build_action(AiRlHighLevelAction action) {
    switch (action) {
    case AiRlHighLevelAction::build_population_nest:
    case AiRlHighLevelAction::build_egg_nest:
    case AiRlHighLevelAction::build_land_nest:
    case AiRlHighLevelAction::expand_base_nest:
    case AiRlHighLevelAction::build_nest_x86:
    case AiRlHighLevelAction::build_nest_x87:
    case AiRlHighLevelAction::build_nest_x83:
    case AiRlHighLevelAction::build_nest_x88:
    case AiRlHighLevelAction::build_nest_x89:
    case AiRlHighLevelAction::build_nest_x8a:
        return true;
    default:
        return false;
    }
}

// Uses the BASE nest producer (0x80) - collides with the worker rule.
bool uses_base_producer(AiRlHighLevelAction action) {
    return action == AiRlHighLevelAction::produce_worker ||
        action == AiRlHighLevelAction::produce_unit_x2c;
}

} // namespace

bool AiAutopilotIsEggFighterAction(AiRlHighLevelAction action) {
    switch (action) {
    case AiRlHighLevelAction::produce_masos:
    case AiRlHighLevelAction::produce_dilophos:
    case AiRlHighLevelAction::produce_unit_x22:
    case AiRlHighLevelAction::produce_unit_x25:
    case AiRlHighLevelAction::produce_unit_x27:
    case AiRlHighLevelAction::produce_unit_x28:
    case AiRlHighLevelAction::produce_unit_x2e:
        return true;
    default:
        return false;
    }
}

AiAutopilotRule AiAutopilotRuleOf(AiRlHighLevelAction action) {
    if (action == AiRlHighLevelAction::produce_worker) {
        return autopilot_rule_worker;
    }
    if (action == AiRlHighLevelAction::build_population_nest) {
        return autopilot_rule_pop_nest;
    }
    return autopilot_rule_fighter;
}

std::vector<AiRlHighLevelAction> AiAutopilotPlan(AiAutopilotState& state,
    const AiObservation& observation, const AiRlStepEncoding& encoding,
    AiRlHighLevelAction policy_action, u32 frame,
    const AiAutopilotConfig& config) {
    std::vector<AiRlHighLevelAction> actions;

    // The policy's fighter choices steer the idle-producer guard.
    if (AiAutopilotIsEggFighterAction(policy_action)) {
        state.last_fighter_action = policy_action;
    }

    // --- shared counts ------------------------------------------------------
    u32 workers = 0;
    u32 pop_nest_uc = 0;
    bool egg_idle = false;
    u32 reserved = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (!unit.controlled || !unit.alive) {
            continue;
        }
        if (unit.type_id == kWorkerType) {
            ++workers;
            // Resources committed to builds still walking: the mask already
            // subtracts them per action, but the bank threshold must too so
            // the guard cannot starve a reserved expansion.
            if (const u32 pending = AiWalkingBuildTypeOf(unit)) {
                reserved += AiRlBuildingCostOf(pending);
            }
        }
        if (unit.type_id == kPopulationNestType && unit.under_construction) {
            ++pop_nest_uc;
        }
        if (unit.type_id == kEggNestType && !unit.under_construction &&
            unit.queued_production_type_id == 0 &&
            unit.deferred_command_count == 0) {
            egg_idle = true;
        }
    }
    // Workers queued at the base nest count toward the floor (the floor must
    // not queue one per check while the first is still training).
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive &&
            unit.type_id >= kBuildingTypeStart &&
            unit.queued_production_type_id == kWorkerType) {
            ++workers;
        }
    }
    // Idle-producer clock: continuous idleness, reset the moment every egg
    // producer is busy.
    if (egg_idle) {
        if (state.egg_idle_since_frame == 0xffffffffu) {
            state.egg_idle_since_frame = frame;
        }
    } else {
        state.egg_idle_since_frame = 0xffffffffu;
    }

    // --- rule 1: worker floor ----------------------------------------------
    if (workers < config.worker_floor &&
        legal(encoding, AiRlHighLevelAction::produce_worker) &&
        !uses_base_producer(policy_action)) {
        actions.push_back(AiRlHighLevelAction::produce_worker);
    }

    // --- rule 2: pop guard --------------------------------------------------
    // Engine semantics: population_used = SUPPLY from class-2 buildings,
    // population_reserved = live demand incl. queued production.
    const u32 supply = observation.population_limit == 0 ?
        observation.population_used :
        std::min(observation.population_used, observation.population_limit);
    if (observation.population_reserved + config.pop_margin >= supply &&
        pop_nest_uc == 0 &&
        legal(encoding, AiRlHighLevelAction::build_population_nest) &&
        !is_build_action(policy_action)) {
        actions.push_back(AiRlHighLevelAction::build_population_nest);
    }

    // --- rule 3: idle-producer guard ---------------------------------------
    const u32 bank = observation.primary_resources > reserved ?
        observation.primary_resources - reserved : 0u;
    if (state.egg_idle_since_frame != 0xffffffffu &&
        frame - state.egg_idle_since_frame >= config.producer_idle_frames &&
        bank >= config.bank_threshold &&
        legal(encoding, state.last_fighter_action) &&
        !AiAutopilotIsEggFighterAction(policy_action)) {
        actions.push_back(state.last_fighter_action);
    }

    return actions;
}

} // namespace ranker
