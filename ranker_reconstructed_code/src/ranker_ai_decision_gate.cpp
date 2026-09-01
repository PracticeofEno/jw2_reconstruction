#include "ranker_ai_decision_gate.h"

#include "ranker_ai_micro_executor.h"

#include <algorithm>

namespace ranker {
namespace {

constexpr u32 kBuildingTypeStart = 0x60u;
constexpr u32 kSpecialBuildingSkip = 0x6au;  // per the elimination rule
constexpr u32 kNeutralOwnerLimit = 8u;

// The "economy opportunity" action set for trigger_production_open: an action
// here flipping illegal -> legal since the last decision means a producer
// went idle / resources reached a cost / a prerequisite completed.
constexpr bool is_production_action(AiRlHighLevelAction action) {
    switch (action) {
    case AiRlHighLevelAction::produce_worker:
    case AiRlHighLevelAction::produce_masos:
    case AiRlHighLevelAction::produce_dilophos:
    case AiRlHighLevelAction::produce_unit_x22:
    case AiRlHighLevelAction::produce_unit_x25:
    case AiRlHighLevelAction::produce_unit_x27:
    case AiRlHighLevelAction::produce_unit_x28:
    case AiRlHighLevelAction::produce_unit_x2e:
    case AiRlHighLevelAction::produce_unit_x2c:
    case AiRlHighLevelAction::produce_unit_x29:
    case AiRlHighLevelAction::produce_unit_x2a:
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
    case AiRlHighLevelAction::research_harvest:
    case AiRlHighLevelAction::research_ground_attack:
    case AiRlHighLevelAction::research_ground_defense:
    case AiRlHighLevelAction::research_movement:
    case AiRlHighLevelAction::research_air_attack:
    case AiRlHighLevelAction::research_air_defense:
    case AiRlHighLevelAction::research_mutant_merge:
    case AiRlHighLevelAction::research_morph:
    case AiRlHighLevelAction::research_haste:
    case AiRlHighLevelAction::research_exp_down:
    case AiRlHighLevelAction::research_melee_reinforce:
    case AiRlHighLevelAction::research_triceps_speed:
    case AiRlHighLevelAction::research_air_reinforce:
        return true;
    default:
        return false;
    }
}

bool is_hostile_unit(const AiObservation& observation,
    const AiObservedUnit& unit) {
    return !unit.controlled && unit.visible && unit.alive &&
        unit.owner_id < kNeutralOwnerLimit &&
        unit.owner_id != observation.local_owner &&
        (observation.local_relation_mask & (1u << unit.owner_id)) == 0 &&
        (observation.active_owner_mask & (1u << unit.owner_id)) != 0;
}

i64 sq(i64 v) { return v * v; }

} // namespace

bool AiDecisionGateCheckDue(const AiDecisionGateState& state, u32 frame,
    const AiDecisionGateConfig& config) {
    return state.last_check_frame == 0xffffffffu ||
        frame - state.last_check_frame >=
            std::max<u32>(config.min_interval_frames, 1u);
}

AiDecisionGateResult AiDecisionGateEvaluate(AiDecisionGateState& state,
    const AiObservation& observation, const AiRlStepEncoding& encoding,
    const std::array<u32, 4>& losses, bool owner_packet_pending, u32 frame,
    const AiDecisionGateConfig& config) {
    state.last_check_frame = frame;
    AiDecisionGateResult result{};
    const bool first = state.last_decision_frame == 0xffffffffu;
    result.frames_since_last = first ? 0u : frame - state.last_decision_frame;

    // --- current values (all simulation-derived; wall clock never enters) ---
    u32 completed = 0;
    u64 building_health = 0;
    bool base_threat = false;
    const AiMicroExecutorConfig micro_config{};
    // Own completed buildings (threat anchors + HP tracking).
    for (const AiObservedUnit& unit : observation.units) {
        if (!unit.controlled || !unit.alive || unit.under_construction) {
            continue;
        }
        ++completed;
        if (unit.type_id >= kBuildingTypeStart &&
            unit.type_id != kSpecialBuildingSkip) {
            building_health += unit.health;
        }
    }
    // A hostile COMBAT mobile (role melee/ranged - workers harvesting nearby
    // are not a base threat) within the defend bubble of any own building.
    for (const AiObservedUnit& hostile : observation.units) {
        if (base_threat) {
            break;
        }
        if (!is_hostile_unit(observation, hostile) ||
            hostile.type_id >= kBuildingTypeStart) {
            continue;
        }
        const AiMicroRole role = AiMicroRoleOf(hostile, micro_config);
        if (role != AiMicroRole::melee && role != AiMicroRole::ranged) {
            continue;
        }
        for (const AiObservedUnit& own : observation.units) {
            if (!own.controlled || !own.alive || own.under_construction ||
                own.type_id < kBuildingTypeStart ||
                own.type_id == kSpecialBuildingSkip) {
                continue;
            }
            if (sq(own.x - hostile.x) + sq(own.y - hostile.y) <=
                sq(micro_config.defend_radius)) {
                base_threat = true;
                break;
            }
        }
    }
    u32 research_levels = 0;
    for (const u32 level : observation.research_order_levels) {
        research_levels += level;
    }
    const bool enemy_visible = encoding.features[33] > 0.0f;  // enemy total
    const bool engaged = encoding.features[80] > 0.0f;        // engaged frac
    const u32 reject_frame =
        observation.last_build_reject_frames_ago == 0xffffffffu ||
            observation.last_build_reject_frames_ago > frame ?
        0xffffffffu : frame - observation.last_build_reject_frames_ago;

    // --- triggers vs the last-decision snapshot -----------------------------
    u32 triggers = 0;
    if (first) {
        triggers |= trigger_first;
    }
    if (result.frames_since_last >= config.max_interval_frames && !first) {
        triggers |= trigger_max_interval;
    }
    if (state.has_snapshot) {
        for (std::size_t a = 0; a < kAiRlActionCount; ++a) {
            if (encoding.legal_mask[a] != 0 && state.prev_mask[a] == 0 &&
                is_production_action(static_cast<AiRlHighLevelAction>(a))) {
                triggers |= trigger_production_open;
                break;
            }
        }
        if (completed > state.prev_completed ||
            research_levels > state.prev_research_levels) {
            triggers |= trigger_completion;
        }
        if (enemy_visible && !state.prev_enemy_visible) {
            triggers |= trigger_enemy_sighted;
        }
        if (!enemy_visible && state.prev_enemy_visible) {
            triggers |= trigger_enemy_lost;
        }
        if (engaged != state.prev_engaged) {
            triggers |= trigger_contact;
        }
        if (building_health < state.prev_building_health) {
            triggers |= trigger_base_threat;
        }
        if (losses[0] > state.prev_losses[0] ||
            losses[1] > state.prev_losses[1]) {
            triggers |= trigger_own_loss;
        }
        if (reject_frame != 0xffffffffu &&
            reject_frame != state.prev_reject_frame &&
            (state.last_decision_frame == 0xffffffffu ||
                reject_frame >= state.last_decision_frame)) {
            triggers |= trigger_build_rejected;
        }
        // Executor-objective completions (fields are 0 without an executor).
        const bool army_attack = observation.army_objective_kind == 2u;
        const bool army_has_target =
            observation.army_attack_has_target != 0;
        if ((state.prev_army_kind != 0 &&
                observation.army_objective_kind != state.prev_army_kind) ||
            (state.prev_army_kind == 2u && army_attack &&
                state.prev_army_had_target && !army_has_target) ||
            (state.prev_raid_members > 0 &&
                observation.raid_unit_count == 0) ||
            (state.prev_raid_b_members > 0 &&
                observation.raid_b_unit_count == 0) ||
            (state.prev_raid_c_members > 0 &&
                observation.raid_c_unit_count == 0) ||
            (state.prev_scout_id != 0 && observation.scout_unit_id == 0) ||
            (state.prev_berry_id != 0 &&
                observation.berry_scout_unit_id == 0) ||
            (state.prev_explorer_id != 0 &&
                observation.explorer_unit_id == 0) ||
            (state.prev_roamer_id != 0 && observation.roamer_unit_id == 0)) {
            triggers |= trigger_objective_done;
        }
    }
    // Threat PRESENCE is edge-triggered (a persistent siege re-snapshots at
    // each decision and must not fire every check); the HP-decrease half is
    // handled above against the snapshot.
    if (base_threat && !state.prev_base_threat) {
        triggers |= trigger_base_threat;
    }
    // v10.2 (user replay report: the army kept defending with no enemy in
    // sight): a policy DEFEND is done once the base threat CLEARS.  The
    // falling edge fires objective_done so the policy re-decides right away
    // - the defend mask is closed again by then, so a stale defend objective
    // cannot outlive its threat.
    if (state.has_snapshot && !base_threat && state.prev_base_threat &&
        observation.army_objective_kind == 3u) {
        triggers |= trigger_objective_done;
    }
    if (owner_packet_pending) {
        triggers |= trigger_owner_packet;
    }

    result.triggers = triggers;
    result.due = first ||
        (result.frames_since_last >=
                std::max<u32>(config.min_interval_frames, 1u) &&
            triggers != 0);
    if (!result.due) {
        return result;
    }

    // --- refresh the snapshot (this decision becomes the new baseline) -----
    state.last_decision_frame = frame;
    state.prev_mask = encoding.legal_mask;
    state.prev_completed = completed;
    state.prev_research_levels = research_levels;
    state.prev_enemy_visible = enemy_visible;
    state.prev_engaged = engaged;
    state.prev_base_threat = base_threat;
    state.prev_building_health = building_health;
    state.prev_losses = losses;
    state.prev_reject_frame = reject_frame;
    AiDecisionGateSnapshotObjectives(state, observation.army_objective_kind,
        observation.army_attack_has_target != 0, observation.raid_unit_count,
        observation.scout_unit_id, observation.berry_scout_unit_id,
        observation.explorer_unit_id, observation.roamer_unit_id,
        observation.raid_b_unit_count, observation.raid_c_unit_count);
    state.has_snapshot = true;
    return result;
}

void AiDecisionGateSnapshotObjectives(AiDecisionGateState& state,
    u32 army_kind, bool army_has_target, u32 raid_members, u32 scout_id,
    u32 berry_id, u32 explorer_id, u32 roamer_id, u32 raid_b_members,
    u32 raid_c_members) {
    state.prev_army_kind = army_kind;
    state.prev_army_had_target = army_has_target;
    state.prev_raid_members = raid_members;
    state.prev_raid_b_members = raid_b_members;
    state.prev_raid_c_members = raid_c_members;
    state.prev_scout_id = scout_id;
    state.prev_berry_id = berry_id;
    state.prev_explorer_id = explorer_id;
    state.prev_roamer_id = roamer_id;
}

} // namespace ranker
