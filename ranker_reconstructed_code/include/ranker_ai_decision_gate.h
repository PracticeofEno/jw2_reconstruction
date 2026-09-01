#ifndef RANKER_AI_DECISION_GATE_H
#define RANKER_AI_DECISION_GATE_H

#include "ranker_ai_observation.h"
#include "ranker_ai_rl_features.h"

#include <array>

namespace ranker {

// Event-based decision gate (docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md).
//
// The v8 policy was invoked unconditionally every 8 frames, which made 73% of
// the imitation labels "no_op", let the sampled army objective thrash (the
// same state re-sampled independently every 8 frames), and stretched episodes
// to 5000-10000 steps.  This gate calls the policy only when something
// DECISION-RELEVANT happened since its last decision (or max_interval frames
// passed).  Pure function of simulation-derived inputs + explicit state, so
// the RL pump and the imitation logger share one rule and same-seed runs stay
// byte-identical.

struct AiDecisionGateConfig {
    // Never decide more often than this (matches the executor's policy-hold
    // granularity; also the caller's evaluation cadence).
    u32 min_interval_frames = 8;
    // Decide at least this often even with no events (keeps the policy able
    // to start attacks/expansions on its own schedule).
    u32 max_interval_frames = 64;
};

// Bitmask (append-only).  Feature [773..784] one-hot these in bit order.
enum AiDecisionTrigger : u32 {
    trigger_max_interval    = 1u << 0,
    // A produce_*/build_*/research_* action that was ILLEGAL at the last
    // decision became legal (producer idle + resources reached + prereqs).
    trigger_production_open = 1u << 1,
    // Completed own unit/building count or a research level rose.
    trigger_completion      = 1u << 2,
    trigger_enemy_sighted   = 1u << 3,  // visible enemy total 0 -> >0
    trigger_enemy_lost      = 1u << 4,  // visible enemy total >0 -> 0
    trigger_contact         = 1u << 5,  // engaged fraction 0 <-> >0
    // A hostile combat mobile within defend-radius of an own building, or an
    // own completed building lost health.
    trigger_base_threat     = 1u << 6,
    trigger_own_loss        = 1u << 7,  // cumulative value lost (vl/bl) rose
    // The executor finished/changed what the policy last ordered: army attack
    // ran out of targets, retreat arrived (auto defend), raid died out, a
    // single-unit group (scout/berry/explorer/roamer) was released.
    trigger_objective_done  = 1u << 8,
    trigger_build_rejected  = 1u << 9,  // a build order was refused since
    trigger_owner_packet    = 1u << 10, // imitation only: owner sent packets
    trigger_first           = 1u << 11, // first decision of the match
};
constexpr u32 kAiDecisionTriggerCount = 12;

struct AiDecisionGateState {
    u32 last_decision_frame = 0xffffffffu;
    // Caller-side evaluation cadence (the caller evaluates when
    // frame - last_check_frame >= min_interval_frames).
    u32 last_check_frame = 0xffffffffu;
    // --- snapshot at the last DECISION (triggers compare against these) ---
    std::array<std::uint8_t, kAiRlActionCount> prev_mask{};
    u32 prev_completed = 0;         // completed own units + buildings
    u32 prev_research_levels = 0;   // sum of research_order_levels
    bool prev_enemy_visible = false;
    bool prev_engaged = false;
    // Edge detection for the presence half of trigger_base_threat (the HP
    // half compares prev_building_health) - a persistent siege must not fire
    // a decision every check.
    bool prev_base_threat = false;
    u64 prev_building_health = 0;   // sum HP of completed own buildings
    std::array<u32, 4> prev_losses{};
    u32 prev_reject_frame = 0xffffffffu;  // frame of last observed build reject
    // Executor-objective summary (mirrored observation fields; all 0 when no
    // executor drives the owner, e.g. imitation logging).
    u32 prev_army_kind = 0;
    bool prev_army_had_target = false;
    u32 prev_raid_members = 0;
    // v10: the two extra raid slots (four fighting bodies).
    u32 prev_raid_b_members = 0;
    u32 prev_raid_c_members = 0;
    u32 prev_scout_id = 0;
    u32 prev_berry_id = 0;
    u32 prev_explorer_id = 0;
    u32 prev_roamer_id = 0;
    bool has_snapshot = false;
};

struct AiDecisionGateResult {
    bool due = false;
    u32 triggers = 0;
    u32 frames_since_last = 0;
};

// True when the caller should run an evaluation this frame (cheap; does not
// touch the snapshot).  The caller then builds the encoding and calls
// AiDecisionGateEvaluate, which stamps last_check_frame.
bool AiDecisionGateCheckDue(const AiDecisionGateState& state, u32 frame,
    const AiDecisionGateConfig& config = {});

// Evaluate the gate: compute the trigger set relative to the last-decision
// snapshot and decide whether the policy is due.  On due=true the snapshot is
// refreshed from the current observation/encoding (the OBJECTIVE summary is
// refreshed from the observation's mirrored fields, which are pre-decision;
// call AiDecisionGateSnapshotObjectives after the decision was translated so
// the policy's own objective change does not re-trigger the gate).
// `owner_packet_pending` is the imitation-mode trigger (the recorded owner
// sent packets since the last sample); pass false in the RL pump.
AiDecisionGateResult AiDecisionGateEvaluate(AiDecisionGateState& state,
    const AiObservation& observation, const AiRlStepEncoding& encoding,
    const std::array<u32, 4>& losses, bool owner_packet_pending, u32 frame,
    const AiDecisionGateConfig& config = {});

// Refresh the objective-summary snapshot AFTER the decision was translated
// (the translator may have changed group objectives; without this refresh the
// policy's own change would fire trigger_objective_done next check).
void AiDecisionGateSnapshotObjectives(AiDecisionGateState& state,
    u32 army_kind, bool army_has_target, u32 raid_members, u32 scout_id,
    u32 berry_id, u32 explorer_id, u32 roamer_id, u32 raid_b_members = 0,
    u32 raid_c_members = 0);

} // namespace ranker

#endif // RANKER_AI_DECISION_GATE_H
