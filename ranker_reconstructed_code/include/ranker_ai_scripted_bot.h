#pragma once

#include "ranker_ai_actions.h"
#include "ranker_ai_observation.h"
#include "ranker_ai_rl_features.h"

#include <array>

namespace ranker {

constexpr u32 kTyranoFactionId = 2u;
constexpr u32 kTyranoWorkerType = 0x20u;
constexpr u32 kTyranoMasosType = 0x21u;
constexpr u32 kTyranoUnit22Type = 0x22u;  // mid-tier fighter (built-in AI main)
constexpr u32 kTyranoDilophosType = 0x24u;
constexpr u32 kTyranoNestType = 0x80u;
constexpr u32 kTyranoPopulationNestType = 0x82u;
constexpr u32 kTyranoEggNestType = 0x84u;
constexpr u32 kTyranoLandNestType = 0x85u;
constexpr u32 kTyranoNest86Type = 0x86u;  // late-tech building (built-in AI)
constexpr u32 kTyranoNest87Type = 0x87u;  // late-tech building (built-in AI)
constexpr u32 kTyranoMobileTypeLimit = 0x60u;  // < 0x60 mobile, >= 0x80 building
constexpr u32 kNeutralMonsterOwnerId = 8u;     // kOwnerNeutralRouteProbeOwnerId
constexpr u32 kTyranoHarvestUpgradeOrder = 0x14u;
constexpr u32 kTyranoMovementUpgradeOrder = 0x16u;
constexpr u32 kTyranoGroundAttackUpgradeOrder = 0x19u;

struct TyranoScriptedBotConfig {
    u32 decision_interval_frames = 8;
    u32 desired_worker_count = 6;
    u32 desired_harvester_count = 4;
    u32 worker_primary_resource_cost = 100;
    u32 desired_masos_count = 15;
    u32 desired_dilophos_count = 6;
};

enum class TyranoScriptedBotDecisionCode : u32 {
    action_ready = 0,
    not_due,
    game_ended,
    wrong_faction,
    no_controlled_units,
    no_action,
};

enum class TyranoScriptedBotIntent : u32 {
    none = 0,
    build_starting_nest,
    set_starting_rally,
    harvest_visible_resource,
    produce_worker,
    attack_visible_enemy,
    explore,
    build_population_nest,
    build_egg_nest,
    produce_masos,
    research_harvest_upgrade,
    build_land_nest,
    research_ground_attack,
    build_second_tyrano_nest,
    research_movement_upgrade,
    produce_dilophos,
};

constexpr std::size_t kTyranoScriptedBotIntentCount = 16;

struct TyranoScriptedBotState {
    u32 last_decision_frame = 0xffffffffu;
    u32 placement_probe_index = 0;
    u32 exploration_index = 0;
    u32 decisions_emitted = 0;
    u32 actions_committed = 0;
    // Frame the army was last sent to an exploration/attack-move objective.  The
    // army holds that objective for a dwell window so it actually travels to and
    // fights at each enemy start instead of re-pathing to a new point every
    // decision cycle (which left it wandering and never committing to a kill).
    u32 last_army_objective_frame = 0xffffffffu;
    // Defense autopilot throttle: the intruder last engaged and when, so the
    // executor re-issues the defend order only on a target change or after a
    // dwell window (re-spamming attack orders every cycle resets pathing).
    u32 last_defense_target_id = 0;
    u32 last_defense_order_frame = 0xffffffffu;
    std::array<u32, kTyranoScriptedBotIntentCount> intent_retry_after_frame{};
    bool rally_configured = false;
    bool harvest_upgrade_requested = false;
    bool ground_attack_upgrade_requested = false;
    bool movement_upgrade_requested = false;
};

struct TyranoScriptedBotDecision {
    TyranoScriptedBotDecisionCode code =
        TyranoScriptedBotDecisionCode::no_action;
    TyranoScriptedBotIntent intent = TyranoScriptedBotIntent::none;
    AiSemanticAction action;

    explicit operator bool() const {
        return code == TyranoScriptedBotDecisionCode::action_ready;
    }
};

void ResetTyranoScriptedBot(TyranoScriptedBotState& state);
bool IsAiPlayCommandLineEnabled(const char* command_line);
TyranoScriptedBotDecision DecideTyranoScriptedBotAction(
    TyranoScriptedBotState& state, const AiObservation& observation,
    const TyranoScriptedBotConfig& config = {});

// Hierarchical-RL micro executor: given a high-level (strategy) action chosen by
// an external policy, translate it into a concrete semantic-action decision
// using the scripted bot's own unit-selection / build-placement / targeting
// micro.  This is how the RL policy drives the game — it picks the high-level
// action, this turns it into the same AiSemanticAction the scripted policy would
// emit.  Returns a not-ready decision (code != action_ready) when the action
// cannot currently be carried out.
TyranoScriptedBotDecision DecideTyranoScriptedBotForHighLevelAction(
    TyranoScriptedBotState& state, const AiObservation& observation,
    AiRlHighLevelAction action, const TyranoScriptedBotConfig& config = {});

// Executor micro helpers exposed for the hierarchical-RL pump.
//
// Next spiral placement probe point (advances the bot's probe cursor).  The RL
// pump uses it to retry a failed build placement with further points within the
// same decision cycle instead of wasting the cycle.
UnitMovementPoint TyranoScriptedBotNextBuildPoint(TyranoScriptedBotState& state,
    const AiObservation& observation);

// Economy autopilot: plan harvest orders for up to max_actions idle workers
// (nearest explored resource each), skipping units in exclude_unit_ids (e.g.
// the worker the policy just tasked this cycle).  Keeping workers mining is the
// micro executor's job in the hierarchy; the policy only decides strategy.
std::vector<AiSemanticAction> PlanTyranoIdleWorkerHarvest(
    const AiObservation& observation, std::size_t max_actions,
    const std::vector<u32>& exclude_unit_ids = {});

// Defense autopilot: if a visible hostile unit is inside the defense radius of
// any own building (or the start position), order every non-worker fighter to
// attack the nearest such intruder.  Base defense must not wait for the macro
// policy to happen to pick a defend action — in the hierarchy, reacting to a
// raid is executor micro.  Throttled via the bot state so the order is only
// re-issued on a target change or after a dwell window.  Returns 0 or 1 action.
std::vector<AiSemanticAction> PlanTyranoDefenseAutopilot(
    TyranoScriptedBotState& state, const AiObservation& observation);
void CommitTyranoScriptedBotDecision(TyranoScriptedBotState& state,
    const TyranoScriptedBotDecision& decision, bool published,
    AiActionPlanCode plan_code = AiActionPlanCode::okay);

} // namespace ranker
