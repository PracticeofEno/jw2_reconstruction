#ifndef RANKER_AI_AUTOPILOT_H
#define RANKER_AI_AUTOPILOT_H

#include "ranker_ai_observation.h"
#include "ranker_ai_rl_features.h"

#include <array>
#include <vector>

namespace ranker {

// Macro autopilot: the
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
    // Entity mode keeps this rule but the translator picks a WORKER scout
    // (berry_scout_prefer_worker) so no direct-controlled fighter is
    // re-owned (plan section 4.2); without it far-spawn games never reveal
    // the enemy base and every knowledge-gated behavior stays locked.
    bool scout_guard_enabled = true;
    u32 scout_guard_start_frame = 1200;
    u32 scout_guard_cooldown_frames = 256;
    // v10 tech guard (2026-09-01): with this much UNRESERVED bank, the first
    // missing tech building of the audited chain is built.  DEFAULT OFF
    // (user decision: tech timing is the policy's to LEARN - the bootstrap
    // is fixed by longer imitation games instead, so BC carries tech-build
    // labels).  Kept as a last-resort safety net behind this flag.
    bool tech_guard_enabled = false;
    u32 tech_bank_threshold = 1200;
    u32 tech_guard_cooldown_frames = 192;
    // Expansion guard (2026-09-02, entity mode): one base's berries saturate
    // around the starting worker count, income floats near zero bank, and
    // the built-in AI out-booms us 2-3x by taking expansions.  There is no
    // bank to trigger on (everything is spent), so the guard fires on a
    // cadence: fewer standing/walking base nests than the target and the
    // expand action legal (cost + reachable site) -> expand.  Planned before
    // the idle-producer guard so the expansion claims the bank first.
    // DEFAULT OFF; the entity transaction enables it.
    bool expansion_guard_enabled = false;
    u32 expansion_base_target = 3;    // total base nests incl. the start one
    u32 expansion_start_frame = 1600;
    u32 expansion_cooldown_frames = 512;
    // Before this frame only ONE expansion is owed (early saving is cheap —
    // army production has not ramped; a full 3-base saving push mid-game
    // starved the army and died to the first waves, A/B 2026-09-02).
    u32 expansion_late_base_frame = 12000;
    // Saving duty cycle: save this many frames, then spend freely for the
    // same span, alternating until the expansion lands.  Uninterrupted
    // saving deadlocked (workers died unreplaced with 1.1k banked, A/B
    // expand3): the relax half lets the economy breathe.
    u32 expansion_saving_duty_frames = 1600;
    // Berry-scout guard: entity mode masks the policy's scout_berry, so the
    // autopilot sends one (worker) scout whenever the next expansion site is
    // dark — without it expand_base_nest can never become legal.
    u32 berry_scout_cooldown_frames = 256;
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
    // Expansion-guard issuance throttle (set on emit, not publish).
    u32 last_expansion_guard_frame = 0xffffffffu;
    // First frame of the current saving stretch (duty cycle anchor);
    // 0xffffffff = not saving.
    u32 saving_since_frame = 0xffffffffu;
    // Berry-scout guard issuance throttle.
    u32 last_berry_scout_frame = 0xffffffffu;
    // Frame since which a completed egg-nest producer has been continuously
    // idle (queue empty, no deferred commands); 0xffffffff = none idle.
    u32 egg_idle_since_frame = 0xffffffffu;
    // Scout-guard issuance throttle (set on emit, not publish).
    u32 last_scout_guard_frame = 0xffffffffu;
    u32 last_tech_guard_frame = 0xffffffffu;
    // Rule firings: total (result JSON) and since the last policy decision
    // (features [785..787] carry the first three; the pump resets the window
    // at each policy decision).  Slot order = AiAutopilotRule.
    std::array<u32, 4> fired_total{};
    std::array<u32, 4> fired_since_decision{};
};

// True for the fighter-produce actions whose pick should update
// last_fighter_action (the egg-nest roster the idle guard can repeat).
bool AiAutopilotIsEggFighterAction(AiRlHighLevelAction action);

// Expansion saving mode (entity transaction): true while the frame's macro
// mask should withhold spend actions (all but produce_worker and the expand
// itself) so the bank can reach the expansion cost (income floats near zero
// bank; without saving the guard's expand action never becomes cost-legal).
// False once the base target is met, before the start frame, when the bank
// already exceeds the cost with margin (site problem, not funds), or during
// the relax half of the duty cycle (uninterrupted saving deadlocked the
// economy).  Mutates the state's duty-cycle anchor.
bool AiAutopilotExpansionSaving(AiAutopilotState& state,
    const AiObservation& observation, u32 frame,
    const AiAutopilotConfig& config);

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
