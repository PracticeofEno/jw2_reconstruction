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

constexpr u32 kBaseNestType = 0x80u;

u32 count_base_nests(const AiObservation& observation) {
    u32 base_nests = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive &&
            (unit.type_id == kBaseNestType ||
                AiWalkingBuildTypeOf(unit) == kBaseNestType)) {
            ++base_nests;
        }
    }
    return base_nests;
}

u32 effective_base_target(u32 frame, const AiAutopilotConfig& config) {
    return frame < config.expansion_late_base_frame ?
        std::min(config.expansion_base_target, 2u) :
        config.expansion_base_target;
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

bool AiAutopilotExpansionSaving(AiAutopilotState& state,
    const AiObservation& observation, u32 frame,
    const AiAutopilotConfig& config) {
    if (!config.expansion_guard_enabled ||
        frame < config.expansion_start_frame ||
        count_base_nests(observation) >=
            effective_base_target(frame, config)) {
        state.saving_since_frame = 0xffffffffu;
        return false;
    }
    // Funds already there with margin: if expand is still illegal the
    // blocker is the site, not the bank — do not freeze the economy.
    const u32 cost = AiRlBuildingCostOf(kBaseNestType);
    if (observation.primary_resources >= cost + cost / 4u) {
        return false;
    }
    if (state.saving_since_frame == 0xffffffffu) {
        state.saving_since_frame = frame;
    }
    // Duty cycle: save for a stretch, then spend freely for a stretch, so a
    // slow or blocked expansion can never starve the army/workers for good.
    const u32 duty = std::max(config.expansion_saving_duty_frames, 1u);
    return ((frame - state.saving_since_frame) / duty) % 2u == 0u;
}

AiAutopilotRule AiAutopilotRuleOf(AiRlHighLevelAction action) {
    if (action == AiRlHighLevelAction::produce_worker) {
        return autopilot_rule_worker;
    }
    if (action == AiRlHighLevelAction::build_population_nest ||
        action == AiRlHighLevelAction::expand_base_nest) {
        // Expansion guard shares the base-infrastructure slot.
        return autopilot_rule_pop_nest;
    }
    if (action == AiRlHighLevelAction::explore_frontier ||
        action == AiRlHighLevelAction::scout_berry) {
        return autopilot_rule_scout;
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

    // --- rule 2.4: berry-scout guard.  expand_base_nest is only legal once
    // the next expansion site is LIT, and entity mode masks the policy's
    // scout_berry — so nobody would ever light it.  Fired without a legal()
    // check: the translator no-ops itself when there is no dark target, and
    // the action spends nothing.  (Entity mode also makes the translator
    // pick a WORKER scout — see berry_scout_prefer_worker.)
    if (config.expansion_guard_enabled &&
        frame >= config.expansion_start_frame &&
        count_base_nests(observation) <
            effective_base_target(frame, config) &&
        (state.last_berry_scout_frame == 0xffffffffu ||
            frame - state.last_berry_scout_frame >=
                config.berry_scout_cooldown_frames)) {
        state.last_berry_scout_frame = frame;
        actions.push_back(AiRlHighLevelAction::scout_berry);
    }

    // --- rule 2.5: expansion guard (before the idle-producer guard so the
    // expansion claims the bank first; income is berry-saturated on one base
    // and the bank floats near zero, so the saving mode below withholds the
    // macro's spend actions until the expand action is cost-legal).  Counted
    // under the pop-nest slot (base infrastructure).
    if (config.expansion_guard_enabled &&
        frame >= config.expansion_start_frame &&
        !is_build_action(policy_action) &&
        (state.last_expansion_guard_frame == 0xffffffffu ||
            frame - state.last_expansion_guard_frame >=
                config.expansion_cooldown_frames)) {
        if (count_base_nests(observation) <
                effective_base_target(frame, config) &&
            legal(encoding, AiRlHighLevelAction::expand_base_nest)) {
            state.last_expansion_guard_frame = frame;
            actions.push_back(AiRlHighLevelAction::expand_base_nest);
        }
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

    // --- rule 4: scout guard (user directive) -------------------------------
    // No enemy building known (visible or fog-remembered), no explorer out,
    // opening grace passed: send one explorer.  It checks the unexplored
    // start candidates first, then the frontier (executor rule); the guard
    // stops firing the moment an enemy building is known - finding the base
    // IS its job, what to do about it stays the policy's.
    bool enemy_base_known = false;
    for (const AiObservedUnit& unit : observation.units) {
        if (!unit.controlled && unit.visible && unit.alive &&
            unit.type_id >= kBuildingTypeStart && unit.owner_id < 8u &&
            unit.owner_id != observation.local_owner &&
            (observation.local_relation_mask & (1u << unit.owner_id)) == 0 &&
            (observation.active_owner_mask & (1u << unit.owner_id)) != 0) {
            enemy_base_known = true;
            break;
        }
    }
    if (!enemy_base_known) {
        for (const u8 remembered : observation.enemy_building_memory) {
            if (remembered != 0) {
                enemy_base_known = true;
                break;
            }
        }
    }
    const bool policy_scouts =
        policy_action == AiRlHighLevelAction::explore_frontier ||
        policy_action == AiRlHighLevelAction::roam_scout ||
        policy_action == AiRlHighLevelAction::scout_map ||
        policy_action == AiRlHighLevelAction::search_enemy_base;
    if (config.scout_guard_enabled &&
        !enemy_base_known && frame >= config.scout_guard_start_frame &&
        observation.explorer_unit_id == 0 && !policy_scouts &&
        legal(encoding, AiRlHighLevelAction::explore_frontier) &&
        (state.last_scout_guard_frame == 0xffffffffu ||
            frame - state.last_scout_guard_frame >=
                config.scout_guard_cooldown_frames)) {
        state.last_scout_guard_frame = frame;
        actions.push_back(AiRlHighLevelAction::explore_frontier);
    }

    // --- rule 5: tech guard (2026-09-01 user replay report) -----------------
    // The policy owns WHEN to tech, but an early policy that never picks a
    // build_nest action stalls the whole tree.  With a fat unreserved bank,
    // build the FIRST missing building of the audited chain (egg -> land ->
    // sky -> throw -> upgrade -> land nisdos -> sky nisdos); one per
    // cooldown, never colliding with a policy build, and a building of the
    // type already standing/under construction/walking skips its slot.
    if (config.tech_guard_enabled &&
        bank >= config.tech_bank_threshold && !is_build_action(policy_action) &&
        (state.last_tech_guard_frame == 0xffffffffu ||
            frame - state.last_tech_guard_frame >=
                config.tech_guard_cooldown_frames)) {
        struct TechSlot {
            u32 type;
            AiRlHighLevelAction action;
        };
        static const TechSlot kTechChain[] = {
            {0x84u, AiRlHighLevelAction::build_egg_nest},
            {0x85u, AiRlHighLevelAction::build_land_nest},
            {0x86u, AiRlHighLevelAction::build_nest_x86},
            {0x87u, AiRlHighLevelAction::build_nest_x87},
            {0x88u, AiRlHighLevelAction::build_nest_x88},
            {0x89u, AiRlHighLevelAction::build_nest_x89},
            {0x8au, AiRlHighLevelAction::build_nest_x8a},
        };
        for (const TechSlot& slot : kTechChain) {
            bool present = false;
            for (const AiObservedUnit& unit : observation.units) {
                if (unit.controlled && unit.alive &&
                    (unit.type_id == slot.type ||
                        AiWalkingBuildTypeOf(unit) == slot.type)) {
                    present = true;
                    break;
                }
            }
            if (present) {
                continue;
            }
            if (legal(encoding, slot.action)) {
                state.last_tech_guard_frame = frame;
                actions.push_back(slot.action);
            }
            // Missing but illegal (cost/prereq/site): wait - never skip
            // ahead, the chain order IS the prerequisite order.
            break;
        }
    }

    return actions;
}

} // namespace ranker
