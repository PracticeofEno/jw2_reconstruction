#include "ranker_production_orders.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main() {
    using namespace ranker;

    // Packet source byte +0x0c remains authoritative even when a stale or
    // malformed peer references a unit whose owner is different.
    assert(ResolveProductionOrderPacketEnqueueOwner(0u) == 0u);
    assert(ResolveProductionOrderPacketEnqueueOwner(2u) == 2u);
    assert(ResolveProductionOrderPacketEnqueueOwner(7u) == 7u);

    const u32 primary = static_cast<u32>(
        ProductionOrderAvailabilityCode::missing_primary_resource);
    const u32 secondary = static_cast<u32>(
        ProductionOrderAvailabilityCode::missing_secondary_resource);
    const u32 variant = static_cast<u32>(
        ProductionOrderAvailabilityCode::variant_limit_reached);
    const u32 prerequisite = static_cast<u32>(
        ProductionOrderAvailabilityCode::missing_prerequisite);
    const u32 locked = static_cast<u32>(
        ProductionOrderAvailabilityCode::locked);

    assert(IsProductionOrderPacketResourceFailureFeedbackCode(primary));
    assert(IsProductionOrderPacketResourceFailureFeedbackCode(secondary));
    assert(!IsProductionOrderPacketResourceFailureFeedbackCode(variant));
    assert(!IsProductionOrderPacketResourceFailureFeedbackCode(prerequisite));
    assert(!IsProductionOrderPacketResourceFailureFeedbackCode(locked));
    return 0;
}
