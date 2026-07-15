#include "ranker_owner_ai.h"

#include <cassert>
#include <cstdio>

namespace {

i32 g_target_weight = 1000;

bool eligible_summary(const ranker::OwnerAiRuntimeState&, u32 owner,
    ranker::OwnerAiEligibleUnitSummary& summary, void*) {
    summary.count = 10;
    summary.weight = owner == 0 ? 100 : g_target_weight;
    return true;
}

} // namespace

int main() {
    using namespace ranker;

    OwnerAiRuntimeState state{};
    state.owners[0].primary_target_owner = 1;
    state.owners[0].rally_delay = 50;
    state.owners[0].reserve_delay = 0;
    state.eligible_unit_summary = eligible_summary;

    UnitMovementUnit target{};
    target.active = true;
    target.owner_id = 1;
    target.type_id = 0;
    UnitMovementContext movement{};
    movement.active_units.push_back(&target);

    OwnerAiStrategicRetargetGateInput input{};
    input.movement = &movement;
    input.owner_phase_state = 1;
    input.owner_population_limit[0] = 20;
    input.owner_population_used[0] = 0;
    input.strategic_route_targets[1] = &target;

    // Original 0x004409d1..0x00440a29 uses the complete eligible target
    // weight: 100 * 100 / (1000 + 1), below the 50-percent rally gate.
    assert(!ShouldOwnerAiRunStrategicQueueRetarget(state, 0, input));

    // Once the complete target army is light enough, the same branch opens.
    g_target_weight = 50;
    assert(ShouldOwnerAiRunStrategicQueueRetarget(state, 0, input));

    std::printf(
        "OWNER_AI_RALLY_TARGET_SUMMARY_PASS whole-army=1000 blocked "
        "whole-army=50 retarget route-local-not-used\n");
    return 0;
}
