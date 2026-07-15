#include "ranker_unit_commands.h"

#include <cassert>
#include <iostream>

using namespace ranker;

namespace {

u32 raw_weight(const UnitMovementUnit& unit) {
    return unit.health + unit.runtime_stat_1c + unit.runtime_stat_20;
}

} // namespace

int main() {
    UnitMovementContext movement{};
    UnitCommandContext context{};
    context.movement = &movement;
    context.owner_relation_masks.fill(0);

    UnitMovementUnit hostile{};
    // Membership in active_units is canonical, just like the original active
    // linked list; the reconstruction-only mirror bit must not add a filter.
    hostile.active = false;
    hostile.owner_id = 1;
    hostile.type_id = 0x20;
    hostile.type_flags = 0x20;
    hostile.x = 250;
    hostile.y = 100;
    movement.active_units.push_back(&hostile);

    // An existing response point within 0x100 is reused without moving its
    // original coordinates.  Zero raw weight remains zero even though the
    // hostile still contributes to the pressure count.
    OwnerTransportQueueState existing_queue{};
    existing_queue.slots[1].state = kOwnerTransportQueueStateThreatResponse;
    existing_queue.slots[1].target_x = 100;
    existing_queue.slots[1].target_y = 100;
    UnitMovementPoint merged_point{250, 100};
    const OwnerThreatPointResponseResult merged =
        HandleOwnerThreatPointResponseQueue(context, existing_queue, 0,
            merged_point, nullptr, nullptr, raw_weight, 0x100, 2, nullptr);
    assert(merged.slot_index == 1);
    assert(!merged.allocated_slot);
    assert(!merged.cleared);
    assert(merged.pressure.count == 1);
    assert(merged.pressure.weight == 0);
    assert(existing_queue.slots[1].target_x == 100);
    assert(existing_queue.slots[1].target_y == 100);

    // No nearby hostile clears the threat point without transiently
    // allocating a state-0x1f queue slot.
    OwnerTransportQueueState empty_queue{};
    UnitMovementPoint empty_point{1000, 1000};
    const OwnerThreatPointResponseResult empty =
        HandleOwnerThreatPointResponseQueue(context, empty_queue, 0,
            empty_point, nullptr, nullptr, raw_weight, 0x100, 2, nullptr);
    assert(empty.cleared);
    assert(!empty.allocated_slot);
    assert(empty.slot_index == kInvalidOwnerTransportQueueSlot);
    assert(empty_point.x == -1);
    for (const OwnerTransportQueueSlot& slot : empty_queue.slots) {
        assert(slot.state == 0);
    }

    // The original x2 requested weight uses wrapping 32-bit arithmetic.
    hostile.health = 0x80000001u;
    OwnerTransportQueueState wrap_queue{};
    UnitMovementPoint wrap_point{250, 100};
    const OwnerThreatPointResponseResult wrap =
        HandleOwnerThreatPointResponseQueue(context, wrap_queue, 0,
            wrap_point, nullptr, nullptr, raw_weight, 0x100, 2, nullptr);
    assert(wrap.allocated_slot);
    assert(wrap.pressure.weight == 0x80000001u);
    assert(wrap.requested_weight == 2u);
    assert(wrap_queue.slots[wrap.slot_index].target_x == 250);
    assert(wrap_queue.slots[wrap.slot_index].target_y == 100);

    // Cross-owner responders change their low-byte assignment, but the
    // original temporarily applies count/weight deltas through the threatened
    // owner's queue indexes until normal queue maintenance rebuilds them.
    hostile.owner_id = 2;
    hostile.health = 4;
    context.owner_relation_masks[0] = 1u << 1;
    context.owner_relation_masks[1] = 1u << 0;
    UnitMovementUnit ally{};
    ally.owner_id = 1;
    ally.type_id = 0x10;
    ally.area_marker_flags = 6;
    ally.health = 8;
    movement.active_units.push_back(&ally);

    OwnerTransportQueueState threatened_queue{};
    threatened_queue.slots[2].count = 7;
    threatened_queue.slots[6].count = 3;
    OwnerTransportQueueState ally_queue{};
    ally_queue.slots[1].state = kOwnerTransportQueueStateWorkTarget;
    ally_queue.slots[6].state = 6;
    ally_queue.slots[6].count = 1;
    OwnerThreatPointCrossOwnerResponseQueues cross{};
    cross.queues[0] = &threatened_queue;
    cross.queues[1] = &ally_queue;
    cross.active_owner_slots[1] = true;
    UnitMovementPoint cross_point{250, 100};
    const OwnerThreatPointResponseResult cross_result =
        HandleOwnerThreatPointResponseQueue(context, threatened_queue, 0,
            cross_point, nullptr, nullptr, raw_weight, 0x100, 2, &cross);
    assert(cross_result.pressure.weight == 4);
    assert(cross_result.requested_weight == 8);
    assert(cross_result.moved_count == 1);
    assert((ally.area_marker_flags & 0xffu) == 2);
    assert(threatened_queue.slots[6].count == 2);
    assert(threatened_queue.slots[2].count == 8);
    assert(threatened_queue.slots[2].aux_value == 8);
    assert(ally_queue.slots[6].count == 1);
    assert(ally_queue.slots[2].count == 0);
    assert(ally_queue.slots[2].aux_value == 0);

    std::cout << "OWNER_AI_THREAT_RESPONSE_PASS existing-target=100,100"
                 " active-list-only zero-weight-count=1"
                 " no-pressure=no-allocation wrap-x2=2"
                 " cross-owner=original-transient-bookkeeping\n";
    return 0;
}
