#include "ranker_production_orders.h"
#include "ranker_unit_commands.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {

std::array<ranker::UnitMovementDefinition, ranker::kOwnerUnitTypeCountSlots>
    g_definitions{};

const ranker::UnitMovementDefinition* lookup_definition(
    u32 unit_type, void*) {
    return unit_type < g_definitions.size() ? &g_definitions[unit_type] : nullptr;
}

ranker::OwnerProductionDemandBuildPlanResult run_plan(
    ranker::UnitCommandContext& context,
    ranker::OwnerProductionDemandBuildPlanInput& input,
    ranker::OwnerUnitTypeCounts& counts,
    u32* desired_orders) {
    ranker::OwnerProductionDemandState demand{};
    demand.base_demand.counts[0x2b] = 1;
    input.owner_unit_counts = &counts;
    input.owner_shared_dependency_flags = desired_orders;
    input.owner_shared_dependency_flag_count = ranker::kProductionOrderCount;
    return ranker::ProcessOwnerProductionDemandAndBuildPlan(
        context, 2, demand, input);
}

} // namespace

int main() {
    using namespace ranker;

    constexpr u32 kOwner = 2;
    constexpr u32 kRequiredOrder = 24;
    g_definitions[0x2b].first_completion_order_id = kRequiredOrder;

    ProductionOrderRuntimeState production{};
    UnitMovementContext movement{};
    UnitCommandContext context{};
    context.movement = &movement;
    context.production_state = &production;

    OwnerUnitTypeCounts counts{};
    std::array<u32, kOwnerUnitTypeCountSlots> producer_types{};
    producer_types.fill(0xffffffffu);
    std::array<u32, kProductionOrderCount> desired_orders{};

    OwnerProductionDemandBuildPlanInput input{};
    input.producer_unit_types = &producer_types;
    input.definition_lookup = lookup_definition;

    const OwnerProductionDemandBuildPlanResult locked =
        run_plan(context, input, counts, desired_orders.data());
    assert(locked.marked_unlock_dependency_count == 1);
    assert(locked.special_pairing_count == 0);
    assert(desired_orders[kRequiredOrder] == 1);

    desired_orders.fill(0);
    production.variant_counts[kOwner][kRequiredOrder] = 1;

    UnitMovementUnit source{};
    source.active = true;
    source.owner_id = kOwner;
    source.type_id = 0x28;
    source.command_state = kUnitStateRuntimeIdleAcquire;
    UnitMovementUnit first{};
    first.active = true;
    first.owner_id = kOwner;
    first.type_id = 0x24;
    first.command_state = kUnitStateRuntimeIdleAcquire;
    UnitMovementUnit second{};
    second.active = true;
    second.owner_id = kOwner;
    second.type_id = 0x27;
    second.command_state = kUnitStateRuntimeIdleAcquire;
    movement.active_units = {&source, &first, &second};

    const OwnerProductionDemandBuildPlanResult unlocked =
        run_plan(context, input, counts, desired_orders.data());
    assert(unlocked.marked_unlock_dependency_count == 0);
    assert(unlocked.special_pairing_count == 1);
    assert(desired_orders[kRequiredOrder] == 0);
    assert(source.active_command_payload.state == 0x0b);
    assert(first.active_command_payload.state == 0x0b);
    assert(second.active_command_payload.state == 0x0b);

    std::printf(
        "OWNER_AI_TYPE2B_UPGRADE_GATE_PASS owner=2 order=24 "
        "locked=request-upgrade unlocked=triad\n");
    return 0;
}
