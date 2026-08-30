#include "ranker_ai_rl_reward.h"

namespace ranker {

float AiRlPotentialFromFeatures(
    const std::array<float, kAiRlFeatureCount>& f,
    const AiRlRewardConfig& config) {
    // Feature indices mirror EncodeAiObservationForRl's fixed layout:
    //   [1]  primary resources / 1000
    //   [4]  population limit   / 100
    //   [13] workers            / 50
    //   [19] base nests / [21] pop nests / [23] egg nests / [25] land nests /10
    //   [29] own army strength  / 50
    //   [31] visible enemy mobile /50, [32] visible enemy buildings /20
    const float buildings = f[19] + f[21] + f[23] + f[25] + f[44] + f[45];
    const float enemy = f[31] + f[32];
    const float research = f[36] + f[37] + f[38];
    return config.w_resources * f[1] +
        config.w_worker * f[13] +
        config.w_pop_limit * f[4] +
        config.w_building * buildings +
        config.w_army * f[29] +
        config.w_research * research -
        config.w_enemy_army * enemy;
}

float AiRlPotential(const AiObservation& observation,
    const AiRlRewardConfig& config) {
    return AiRlPotentialFromFeatures(
        EncodeAiObservationForRl(observation).features, config);
}

AiRlTerminalOutcome ClassifyAiRlTerminal(const AiObservation& observation) {
    if (!observation.game_ended) {
        return AiRlTerminalOutcome::ongoing;
    }
    // BUILDING-based judgment, matching the melee elimination rule the
    // harness and league use everywhere else (zero buildings = eliminated;
    // stray mobile units do not keep an owner alive).  The old unit-count
    // rule disagreed with the gate/records and mis-labeled outcomes.
    constexpr u32 kBuildingTypeBase = 0x60u;
    bool have_own_building = false;
    bool have_enemy_building = false;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.type_id < kBuildingTypeBase || !unit.alive) {
            continue;
        }
        if (unit.controlled) {
            have_own_building = true;
        } else if (unit.visible && unit.owner_id < 32u &&
                   (observation.active_owner_mask & (1u << unit.owner_id)) != 0 &&
                   (observation.local_relation_mask &
                       (1u << unit.owner_id)) == 0) {
            have_enemy_building = true;
        }
    }
    if (!have_own_building) {
        return AiRlTerminalOutcome::loss;
    }
    if (!have_enemy_building) {
        return AiRlTerminalOutcome::win;
    }
    return AiRlTerminalOutcome::draw;
}

AiRlStepReward ComputeAiRlStepRewardFromPotentials(float potential_prev,
    float potential_cur, AiRlTerminalOutcome outcome,
    const AiRlRewardConfig& config) {
    AiRlStepReward out{};
    out.potential_prev = potential_prev;
    out.potential_cur = potential_cur;
    out.outcome = outcome;

    // Potential-based shaping: gamma * Phi(s') - Phi(s).  At a terminal state the
    // conventional formulation drops Phi(s') to 0 so the shaping telescopes
    // cleanly and the sparse terminal reward stands alone.
    const bool terminal = outcome != AiRlTerminalOutcome::ongoing;
    const float phi_next = terminal ? 0.0f : potential_cur;
    out.shaping = config.shaping_scale *
        (config.discount * phi_next - potential_prev);

    switch (outcome) {
    case AiRlTerminalOutcome::win:
        out.terminal = config.win_reward;
        break;
    case AiRlTerminalOutcome::loss:
        out.terminal = config.loss_reward;
        break;
    case AiRlTerminalOutcome::draw:
        out.terminal = config.draw_reward;
        break;
    case AiRlTerminalOutcome::ongoing:
        out.terminal = 0.0f;
        break;
    }

    out.total = out.shaping + out.terminal;
    return out;
}

AiRlStepReward ComputeAiRlStepReward(const AiObservation& previous,
    const AiObservation& current, const AiRlRewardConfig& config) {
    const float phi_prev = AiRlPotential(previous, config);
    const float phi_cur = AiRlPotential(current, config);
    return ComputeAiRlStepRewardFromPotentials(phi_prev, phi_cur,
        ClassifyAiRlTerminal(current), config);
}

namespace {

AiRlTraceStep make_trace_step(const AiRlOwnerTrace& trace,
    const AiRlStepReward& reward, bool done) {
    AiRlTraceStep step{};
    step.frame = trace.prev_frame;
    step.action = trace.prev_action;
    step.potential = trace.prev_potential;
    step.shaping = reward.shaping;
    step.terminal = reward.terminal;
    step.total = reward.total;
    step.done = done;
    step.features = trace.prev_features;
    step.legal_mask = trace.prev_mask;
    step.losses = trace.prev_losses;
    step.target = trace.prev_target;
    step.triggers = trace.prev_triggers;
    step.dt = trace.prev_dt;
    return step;
}

} // namespace

void AiRlTraceRecordDecision(AiRlOwnerTrace& trace, u32 frame, u32 action,
    const AiRlStepEncoding& encoding, const AiRlRewardConfig& config,
    const std::array<u32, 4>& losses, i32 target, u32 triggers, u32 dt) {
    const float potential_now =
        AiRlPotentialFromFeatures(encoding.features, config);
    if (trace.has_prev) {
        const AiRlStepReward reward = ComputeAiRlStepRewardFromPotentials(
            trace.prev_potential, potential_now,
            AiRlTerminalOutcome::ongoing, config);
        trace.steps.push_back(make_trace_step(trace, reward, false));
        trace.return_sum += reward.total;
    }
    trace.has_prev = true;
    trace.prev_frame = frame;
    trace.prev_action = action;
    trace.prev_potential = potential_now;
    trace.prev_features = encoding.features;
    trace.prev_mask = encoding.legal_mask;
    trace.prev_losses = losses;
    trace.prev_target = target;
    trace.prev_triggers = triggers;
    trace.prev_dt = dt;
}

void AiRlTraceFinalize(AiRlOwnerTrace& trace, AiRlTerminalOutcome outcome,
    const AiRlRewardConfig& config) {
    trace.final_outcome = outcome;
    if (!trace.has_prev) {
        return;
    }
    // Terminal transition: Phi(next) drops to 0, add the sparse win/loss/draw.
    const AiRlStepReward reward = ComputeAiRlStepRewardFromPotentials(
        trace.prev_potential, 0.0f, outcome, config);
    trace.steps.push_back(make_trace_step(trace, reward, true));
    trace.return_sum += reward.total;
    trace.has_prev = false;
}

} // namespace ranker
