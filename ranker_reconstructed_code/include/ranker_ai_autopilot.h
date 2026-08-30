#ifndef RANKER_AI_AUTOPILOT_H
#define RANKER_AI_AUTOPILOT_H

#include "ranker_ai_observation.h"
#include "ranker_ai_rl_features.h"

#include <array>
#include <vector>

namespace ranker {

// Macro autopilot (docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md §3.2): the
// deterministic "keep the economy breathing" reflexes the built-in AI has as
// rules and a young policy dies without - worker floor, supply guard, and an
// idle-producer guard.  Runs in the RL pump on the check cadence and emits
// 0..n AUXILIARY high-level actions that go through the same translator and
// live validator as policy actions.  It respects the legal mask (which
// already subtracts resources reserved by walking builds, so the autopilot
// cannot starve the policy's expansion), and never touches group objectives -
// WHAT to fight and WHEN stays the policy's.
//
// Priority: policy action > worker floor > pop guard > idle-producer guard.
// A rule whose producer the policy just used this frame is skipped, so one
// producer never receives two same-frame orders.

struct AiAutopilotConfig {
    u32 worker_floor = 10;        // keep at least this many workers (incl. uc)
    u32 pop_margin = 2;           // build supply when demand >= supply - margin
    // Idle-producer guard needs this much banked (net of reserved builds).
    // 600 -> 1500 per user tuning (2026-08-30): leave the policy a real
    // bank to spend before the autopilot starts draining it into units.
    u32 bank_threshold = 1500;
    u32 producer_idle_frames = 96;  // egg producer idle this long -> produce
    // Scout guard (user directive: the bot must actually SCOUT the enemy
    // base): with no enemy building known after the opening grace, send one
    // explorer - it visits the unexplored START CANDIDATES first and only
    // then sweeps the frontier (executor rule), and the guard deactivates
    // the moment an enemy building is seen or remembered.
    u32 scout_guard_start_frame = 1200;
    u32 scout_guard_cooldown_frames = 256;
};

// Autopilot rule slots (the counter/feature order).
enum AiAutopilotRule : u32 {
    autopilot_rule_worker = 0,
    autopilot_rule_pop_nest = 1,
    autopilot_rule_fighter = 2,
    autopilot_rule_scout = 3,
};

struct AiAutopilotState {
    // The last FIGHTER produce action the policy chose - the idle-producer
    // guard repeats it (composition stays the policy's); masos by default.
    AiRlHighLevelAction last_fighter_action =
        AiRlHighLevelAction::produce_masos;
    // Frame since which a completed egg-nest producer has been continuously
    // idle (queue empty, no deferred commands); 0xffffffff = none idle.
    u32 egg_idle_since_frame = 0xffffffffu;
    // Scout-guard issuance throttle (set on emit, not publish).
    u32 last_scout_guard_frame = 0xffffffffu;
    // Rule firings: total (result JSON) and since the last policy decision
    // (features [785..787] carry the first three; the pump resets the window
    // at each policy decision).  Slot order = AiAutopilotRule.
    std::array<u32, 4> fired_total{};
    std::array<u32, 4> fired_since_decision{};
};

// True for the fighter-produce actions whose pick should update
// last_fighter_action (the egg-nest roster the idle guard can repeat).
bool AiAutopilotIsEggFighterAction(AiRlHighLevelAction action);

// Plan the auxiliary actions for this check.  `policy_action` is the action
// the policy chose this frame (no_op when the gate did not fire) - used for
// the same-producer skip rules.  Counters are NOT incremented here; the pump
// increments them per rule only after the action actually planned+published.
std::vector<AiRlHighLevelAction> AiAutopilotPlan(AiAutopilotState& state,
    const AiObservation& observation, const AiRlStepEncoding& encoding,
    AiRlHighLevelAction policy_action, u32 frame,
    const AiAutopilotConfig& config = {});

// Map an autopilot action back to its rule slot (worker/pop/fighter).
AiAutopilotRule AiAutopilotRuleOf(AiRlHighLevelAction action);

} // namespace ranker

#endif // RANKER_AI_AUTOPILOT_H
