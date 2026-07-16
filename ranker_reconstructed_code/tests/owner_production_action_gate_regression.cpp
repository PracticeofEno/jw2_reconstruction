#include "ranker_unit_commands.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace ranker {

u32 CalculateApproxUnitDistance(i32, i32, i32, i32) {
    return 0;
}

UnitMovementCell* GetMovementCell(UnitMovementMap&, u32, u32) {
    return nullptr;
}

const UnitMovementCell* GetMovementCell(const UnitMovementMap&, u32, u32) {
    return nullptr;
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "OWNER_PRODUCTION_ACTION_GATE_FAIL " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

u32 idle_producer_metadata(UnitCommandContext&, const UnitMovementUnit&) {
    return 1;
}

OwnerProductionBuildActionResult select_producer(UnitMovementUnit& producer,
    UnitMovementUnit& linked_builder) {
    UnitMovementContext movement{};
    movement.active_units.push_back(&producer);

    UnitCommandContext context{};
    context.movement = &movement;
    context.owner_resources[2] = 244;
    context.callbacks.command_metadata_flags = idle_producer_metadata;

    OwnerUnitTypeCounts owner_counts{};
    owner_counts.counts[148] = 1;

    OwnerProductionDependencyRequest request{};
    request.unit_type = 49;
    request.producer_unit_type = 148;
    request.resource_cost = 150;

    producer.owner_id = 2;
    producer.type_id = 148;
    producer.target = &linked_builder;
    return SelectOwnerProductionDependencyBuildAction(context, 2,
        owner_counts, request, nullptr, 0);
}

const UnitMovementDefinition* lookup_definition(u32 unit_type,
    void* user_data) {
    auto* definitions = static_cast<
        std::array<UnitMovementDefinition, kOwnerUnitTypeCountSlots>*>(user_data);
    return unit_type < definitions->size() ? &(*definitions)[unit_type] : nullptr;
}

void require_original_special_pair_dependency_source(u32 demanded_unit_type,
    u32 paired_unit_type, u32 alias_target_type) {
    constexpr u32 kOriginalDependencyType = 134;
    constexpr u32 kDemandedRowDependencyType = 138;

    std::array<UnitMovementDefinition, kOwnerUnitTypeCountSlots> definitions{};
    definitions[demanded_unit_type].prerequisite_count = 1;
    definitions[demanded_unit_type].prerequisite_type_ids[0] =
        kDemandedRowDependencyType;
    definitions[paired_unit_type].prerequisite_count = 1;
    definitions[paired_unit_type].prerequisite_type_ids[0] =
        kOriginalDependencyType;

    OwnerUnitTypeCounts owner_counts{};
    // Demand aliases add two of the paired unit while planning the twin.
    // Mark those two as present so this fixture isolates the twin branch.
    owner_counts.counts[alias_target_type] = 2;

    OwnerProductionDemandState demand{};
    demand.base_demand.counts[demanded_unit_type] = 1;

    OwnerProductionDemandBuildPlanInput input{};
    input.owner_unit_counts = &owner_counts;
    input.definition_lookup = lookup_definition;
    input.definition_lookup_user_data = &definitions;

    UnitCommandContext context{};
    context.owner_resources[2] = 1000;
    const OwnerProductionDemandBuildPlanResult result =
        ProcessOwnerProductionDemandAndBuildPlan(context, 2, demand, input);
    require(result.demand_state.base_demand.counts[kOriginalDependencyType] == 1,
        "special twin pairing did not use the paired unit prerequisite row");
    require(result.demand_state.base_demand.counts[kDemandedRowDependencyType] == 0,
        "special twin pairing used the demanded twin's prerequisite row");
}

} // namespace

int main() {
    using namespace ranker;

    UnitMovementUnit builder{};
    UnitMovementUnit completed{};
    completed.action_mode_gate = 0;
    const OwnerProductionBuildActionResult linked_ready =
        select_producer(completed, builder);
    require(linked_ready.action == OwnerProductionBuildAction::use_producer_unit &&
            linked_ready.producer_unit == &completed,
        "completed producer was rejected solely because its builder link remained");

    UnitMovementUnit gated{};
    gated.action_mode_gate = 1;
    const OwnerProductionBuildActionResult construction_gated =
        select_producer(gated, builder);
    require(construction_gated.action ==
                OwnerProductionBuildAction::no_producer_available &&
            construction_gated.producer_unit == nullptr,
        "raw +0x30 construction gate did not reject the producer");

    // Original fixed globals DAT_008d1420 and DAT_008d5d98 point at the
    // prerequisite rows for types 0x25 and 0x27, respectively.
    require_original_special_pair_dependency_source(0x26, 0x25, 0x25);
    require_original_special_pair_dependency_source(0x2d, 0x27, 0x27);

    std::cout << "OWNER_PRODUCTION_ACTION_GATE_PASS "
                 "linked_target=allowed raw30_nonzero=blocked "
                 "special_pair_prerequisites=paired_unit_rows\n";
    return EXIT_SUCCESS;
}
