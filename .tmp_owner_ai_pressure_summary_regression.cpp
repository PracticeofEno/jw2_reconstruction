#include "ranker_owner_ai.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace ranker;

    UnitMovementUnit route{};
    route.owner_id = 1;
    route.x = 100;
    route.y = 100;

    UnitMovementUnit eligible{};
    eligible.owner_id = 1;
    eligible.type_id = 0x64;
    eligible.x = 120;
    eligible.y = 100;
    eligible.active = false;
    eligible.command_state = kUnitCommandDead;
    eligible.runtime_flags = 0x80;
    eligible.definition.type_flags = 0x20;
    eligible.health = 10;
    eligible.runtime_stat_1c = 20;
    eligible.runtime_stat_20 = 30;

    UnitMovementUnit excluded{};
    excluded.owner_id = 1;
    excluded.x = 130;
    excluded.y = 100;
    excluded.definition.type_flags = 0x40;
    excluded.health = 1000;

    UnitMovementUnit script_eligible{};
    script_eligible.owner_id = 1;
    script_eligible.x = 140;
    script_eligible.y = 100;
    script_eligible.definition.type_flags = 0x40;
    script_eligible.definition.initial_script_bit_flags = 0x2;
    script_eligible.health = 1;
    script_eligible.runtime_stat_1c = 2;
    script_eligible.runtime_stat_20 = 3;

    UnitMovementContext movement{};
    movement.active_units = {&eligible, &excluded, &script_eligible};

    OwnerAiRuntimeState state{};
    OwnerAiStrategicRetargetGateInput input{};
    input.movement = &movement;
    input.strategic_route_targets[1] = &route;

    const OwnerAiStrategicPressureSummary summary =
        CalculateOwnerAiStrategicTargetPressureSummary(state, 1, input);
    assert(summary.count == 2);
    assert(summary.weight == 66);

    input.strategic_route_targets[1] = nullptr;
    const OwnerAiStrategicPressureSummary sentinel =
        CalculateOwnerAiStrategicTargetPressureSummary(state, 1, input);
    assert(sentinel.count == 20000);
    assert(sentinel.weight == 0x00895440u);

    std::printf(
        "OWNER_AI_PRESSURE_SUMMARY_PASS definition-filter=2 weight=66 "
        "sentinel=20000/0x895440\n");
    return 0;
}
