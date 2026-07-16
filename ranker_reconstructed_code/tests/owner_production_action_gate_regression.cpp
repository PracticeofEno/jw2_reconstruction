#include "ranker_unit_commands.h"

#include <cstdlib>
#include <iostream>

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

    std::cout << "OWNER_PRODUCTION_ACTION_GATE_PASS "
                 "linked_target=allowed raw30_nonzero=blocked\n";
    return EXIT_SUCCESS;
}
