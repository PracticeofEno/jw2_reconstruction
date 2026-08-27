#ifndef RANKER_AI_RL_REWARD_H
#define RANKER_AI_RL_REWARD_H

#include "ranker_ai_observation.h"
#include "ranker_ai_rl_features.h"

#include <array>
#include <vector>

namespace ranker {

// Per-step reward signal for the RL policy, computed as a pure, deterministic
// function of consecutive observations.  Two components:
//
//   1. Sparse terminal reward: +win / -loss (0 while the game is ongoing).  This
//      is the only reward that carries the true objective; everything else is a
//      shaping aid that must not change the optimal policy.
//   2. Potential-based shaping: F(s, s') = gamma * Phi(s') - Phi(s), where Phi
//      is a potential over the state (economy + buildings + army - visible enemy
//      army).  Ng et al. (1999) proves any reward of this exact form leaves the
//      set of optimal policies unchanged, so it only speeds learning; it cannot
//      teach the wrong thing.  Phi is a linear function of the RL feature vector
//      so it stays consistent with the network input and is naturally normalized.
//
// Design: docs/AI_PLAY_RL_STRUCTURE.md.  Keeping this a pure function (no engine
// state) makes the reward time-series reproducible and unit-testable.

enum class AiRlTerminalOutcome : u32 {
    ongoing = 0,  // game not ended
    win,          // local owner survives, no hostile visible
    loss,         // local owner has no controlled units left
    draw,         // game ended but neither win nor loss is decisive
};

struct AiRlRewardConfig {
    // Discount used inside the shaping term (should match the learner's gamma).
    float discount = 0.997f;

    // Sparse terminal rewards.
    float win_reward = 1.0f;
    float loss_reward = -1.0f;
    float draw_reward = 0.0f;

    // Potential Phi weights, applied to the (already normalized) RL features.
    float w_resources = 0.05f;   // f[1]  primary resources
    float w_worker = 0.04f;      // f[13] worker count
    float w_pop_limit = 0.03f;   // f[4]  population limit (tech/expansion proxy)
    float w_building = 0.06f;    // f[19]+f[21]+f[23]+f[25] own buildings
    float w_army = 0.10f;        // f[29] own army strength
    float w_enemy_army = 0.08f;  // f[31]+f[32] visible enemy strength (subtracted)
    float w_research = 0.04f;    // f[36..38] completed research/upgrade levels

    // Global scale on the shaping term (0 disables shaping, leaving only the
    // sparse terminal signal).
    float shaping_scale = 1.0f;
};

struct AiRlStepReward {
    float potential_prev = 0.0f;
    float potential_cur = 0.0f;
    float shaping = 0.0f;   // shaping_scale * (gamma * Phi(cur) - Phi(prev))
    float terminal = 0.0f;  // sparse win/loss/draw at cur (0 while ongoing)
    float total = 0.0f;     // shaping + terminal
    AiRlTerminalOutcome outcome = AiRlTerminalOutcome::ongoing;
};

// Potential Phi(s) as a linear combination of the RL feature vector.
float AiRlPotentialFromFeatures(
    const std::array<float, kAiRlFeatureCount>& features,
    const AiRlRewardConfig& config = {});

// Convenience overload: encode the observation, then evaluate Phi.
float AiRlPotential(const AiObservation& observation,
    const AiRlRewardConfig& config = {});

// Classify the terminal outcome for the local owner from an observation.
AiRlTerminalOutcome ClassifyAiRlTerminal(const AiObservation& observation);

// Compute the full per-step reward from the previous and current observation.
AiRlStepReward ComputeAiRlStepReward(const AiObservation& previous,
    const AiObservation& current, const AiRlRewardConfig& config = {});

// Lower-level form used by the live pump, which already has both potentials and
// the current observation's terminal outcome in hand (avoids re-encoding).
AiRlStepReward ComputeAiRlStepRewardFromPotentials(float potential_prev,
    float potential_cur, AiRlTerminalOutcome outcome,
    const AiRlRewardConfig& config = {});

// --- Live per-owner reward time-series (episode trace) ---
//
// One record per decision transition: the high-level action taken, the state
// potential when it was taken, and the reward it earned on the way to the next
// decision.  This is the (a_t, r_t) time-series a learner replays.  Full state
// vectors are added in the episode-data-collection step (#5); this keeps the
// live trace compact.

struct AiRlTraceStep {
    u32 frame = 0;         // simulation frame the action was chosen
    u32 action = 0;        // AiRlHighLevelAction value
    float potential = 0.0f;  // Phi at the decision state
    float shaping = 0.0f;    // shaping component of the reward
    float terminal = 0.0f;   // sparse terminal component (win/loss/draw)
    float total = 0.0f;      // shaping + terminal
    bool done = false;       // this is the terminal transition of the episode
    // Full state at the decision (s_t): the exact policy/value-network input and
    // legal-action mask.  Stored so an off-sim learner (Python) gets complete
    // (s, a, r, done) transitions; s' is the next step's features (or terminal).
    std::array<float, kAiRlFeatureCount> features{};
    std::array<std::uint8_t, kAiRlActionCount> legal_mask{};
};

struct AiRlOwnerTrace {
    bool has_prev = false;
    u32 prev_frame = 0;
    u32 prev_action = 0;
    float prev_potential = 0.0f;
    std::array<float, kAiRlFeatureCount> prev_features{};
    std::array<std::uint8_t, kAiRlActionCount> prev_mask{};
    float return_sum = 0.0f;  // undiscounted sum of total rewards so far
    AiRlTerminalOutcome final_outcome = AiRlTerminalOutcome::ongoing;
    std::vector<AiRlTraceStep> steps;

    void reset() { *this = AiRlOwnerTrace{}; }
};

// Record a decision: emit the reward for the *previous* action (aligned s,a,r,s')
// and remember this action + its full state for the next call.  ongoing outcome
// mid-game.  The encoding is the state s_t in which `action` was chosen.
void AiRlTraceRecordDecision(AiRlOwnerTrace& trace, u32 frame, u32 action,
    const AiRlStepEncoding& encoding, const AiRlRewardConfig& config = {});

// Flush the terminal transition for the last pending action with the given
// end-of-game outcome (Phi(next) = 0).
void AiRlTraceFinalize(AiRlOwnerTrace& trace, AiRlTerminalOutcome outcome,
    const AiRlRewardConfig& config = {});

} // namespace ranker

#endif // RANKER_AI_RL_REWARD_H
