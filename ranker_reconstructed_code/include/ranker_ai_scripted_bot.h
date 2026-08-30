#pragma once

#include "ranker_ai_actions.h"
#include "ranker_ai_micro_executor.h"
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
// v2 roster/mechanics (audited caps rows in ai_techtree_audit.txt).
constexpr u32 kTyranoRhamposType = 0x25u;
constexpr u32 kTyranoPterasType = 0x27u;
constexpr u32 kTyranoTricepsType = 0x28u;
constexpr u32 kTyranoCarrierType = 0x29u;      // 둥가리: pure transport
constexpr u32 kTyranoMutantType = 0x2bu;
constexpr u32 kTyranoMorphResearchOrder = 0x2au;   // 공룡 변신 업그레이드
constexpr u32 kTyranoMutantMergeResearchOrder = 0x18u;
constexpr u32 kTyranoHasteResearchOrder = 0x38u;
constexpr u32 kTyranoStanceId = 2u;            // command 0x14 / flag 0x10000
constexpr u32 kTyranoStanceCommandBit = 1u << 0x14;
constexpr u32 kTyranoStanceActiveFlag = 0x10000u;
constexpr u32 kTyranoMorphCommandBit = 1u << 0x11;
constexpr u32 kTyranoMorphedTypeFlag = 0x08000000u;
constexpr u32 kTyranoMergeCommandBit = 1u << 0x0b;
constexpr u32 kTyranoTransportAttachedState = 0x45u;

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
    // The action changed a group objective in the micro executor instead of
    // emitting a semantic action; the executor issues the orders each frame.
    objective_updated,
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
    // v2 intents (retry-backoff slots for the new executors).
    merge_pair,
    merge_mutant,
    morph_shift,
    stance_toggle,
    hold_army,
    patrol_defense,
    drop_attack,
};

constexpr std::size_t kTyranoScriptedBotIntentCount = 24;

struct TyranoScriptedBotState {
    u32 last_decision_frame = 0xffffffffu;
    u32 placement_probe_index = 0;
    // Centre of the spiral placement probe in TILES (-1 = the own start).
    // expand_base_nest points it at the chosen expansion site so the
    // placement retries stay around that site instead of the main base.
    i32 placement_center_tile_x = -1;
    i32 placement_center_tile_y = -1;
    u32 exploration_index = 0;
    u32 decisions_emitted = 0;
    u32 actions_committed = 0;
    // Frame the army was last sent to an exploration/attack-move objective.  The
    // army holds that objective for a dwell window so it actually travels to and
    // fights at each enemy start instead of re-pathing to a new point every
    // decision cycle (which left it wandering and never committing to a kill).
    u32 last_army_objective_frame = 0xffffffffu;
    // Group-objective micro executor (docs/AI_PLAY_MICRO_EXECUTOR_DESIGN.md):
    // the policy's army/economy/scout actions only set group objectives here;
    // the executor runs every frame and issues the unit orders.
    AiMicroExecutorState micro{};
    // Drop-attack composite (board -> travel -> unload).  The policy action
    // only INITIATES the run; the drop autopilot advances it every decision
    // cycle so the policy is free to pick other actions meanwhile.  All
    // transitions are frame/observation-driven (deterministic).
    u32 drop_stage = 0;  // 0 idle, 1 boarding, 2 travelling, 3 unloading
    u32 drop_carrier_id = 0;
    u32 drop_stage_frame = 0xffffffffu;
    i32 drop_target_x = 0;
    i32 drop_target_y = 0;
    // (Enemy-building memory lives in the observation now -
    // AiObservation::enemy_building_memory, maintained by the AI-play pump -
    // and the micro executor reads it for the attack_enemy_base march point.)
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
// target_cell (v8): the spatial-target head's 8x8 grid cell for the actions
// that take one (AiRlActionTakesTargetCell) - the attack strike zone / defend
// post; -1 = none (v7 behavior, executor-derived location).
TyranoScriptedBotDecision DecideTyranoScriptedBotForHighLevelAction(
    TyranoScriptedBotState& state, const AiObservation& observation,
    AiRlHighLevelAction action, const TyranoScriptedBotConfig& config = {},
    i32 target_cell = -1);

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

// (The former defense / offense autopilots were absorbed by the group-objective
// micro executor: base defense is the army's default `defend` objective, and
// "when to attack" is the policy's decision, not an autopilot's.)

// Drop-attack autopilot: advances the composite started by the drop_attack
// high-level action (board passengers onto the 둥가리 carrier, travel to the
// nearest enemy start, unload, then release the state machine).  Runs every
// decision cycle while a run is active; returns the next step's actions.
std::vector<AiSemanticAction> PlanTyranoDropAttackAutopilot(
    TyranoScriptedBotState& state, const AiObservation& observation);

// Split a multi-unit action into planner-sized chunks
// (kAiMaximumUnitsPerAction).  Army orders beyond 14 units were silently
// rejected by the planner (too_many_units) — every army-scale caller must
// chunk.  Non-unit fields are copied into every chunk.
std::vector<AiSemanticAction> ChunkAiSemanticActionUnits(
    const AiSemanticAction& action);
void CommitTyranoScriptedBotDecision(TyranoScriptedBotState& state,
    const TyranoScriptedBotDecision& decision, bool published,
    AiActionPlanCode plan_code = AiActionPlanCode::okay);

} // namespace ranker
