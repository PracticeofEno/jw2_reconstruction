#pragma once

#include "ranker_ai_actions.h"
#include "ranker_ai_observation.h"

#include <array>

namespace ranker {

constexpr u32 kTyranoFactionId = 2u;
constexpr u32 kTyranoWorkerType = 0x20u;
constexpr u32 kTyranoMasosType = 0x21u;
constexpr u32 kTyranoDilophosType = 0x24u;
constexpr u32 kTyranoNestType = 0x80u;
constexpr u32 kTyranoPopulationNestType = 0x82u;
constexpr u32 kTyranoEggNestType = 0x84u;
constexpr u32 kTyranoLandNestType = 0x85u;
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
void CommitTyranoScriptedBotDecision(TyranoScriptedBotState& state,
    const TyranoScriptedBotDecision& decision, bool published,
    AiActionPlanCode plan_code = AiActionPlanCode::okay);

} // namespace ranker
