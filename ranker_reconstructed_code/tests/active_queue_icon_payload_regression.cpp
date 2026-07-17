#include "ranker_ui_overlay.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_all_tribe_worker_production_uses_raw_command_value() {
    // Live original/reconstruction captures publish one 0x1aa record per
    // tribe with aux 0x00/0x10/0x20/0x30.  The separately reconstructed
    // target pointer is not the icon payload; raw unit +0x68 is.
    constexpr std::array<u32, 4> requested_unit_types{
        0x00u, 0x10u, 0x20u, 0x30u};
    for (const u32 requested_unit_type : requested_unit_types) {
        const u32 payload = ranker::ResolveUiOverlayActiveQueueRecordPayload(
            requested_unit_type, 0u);
        expect(payload == requested_unit_type,
            "null typed target replaced a tribe worker queue payload");
        expect(ranker::ResolveUiOverlayActiveQueueRecordPayload(
                   requested_unit_type, 0x13579u) == requested_unit_type,
            "typed target id replaced the original raw +0x68 icon payload");

        const ranker::UiOverlayIconBlitRequest request =
            ranker::ResolveUiOverlayIndexedQueueIconRequest(0x1aau, payload);
        expect(request.kind == ranker::UiOverlayIconBlitKind::base,
            "0x1aa active production did not select the base unit icon table");
        expect(request.item_id == requested_unit_type,
            "tribe worker production changed the queue icon payload");
    }

    constexpr u32 tyrano_worker_type = 0x20u;
    constexpr ranker::UiOverlayIconBlitRequest tyrano_request =
        ranker::ResolveUiOverlayIndexedQueueIconRequest(
            0x1aau, tyrano_worker_type);
    expect(tyrano_request.item_id != 0u,
        "Tyrano worker production regressed to Buildman frame zero");
}

void test_other_active_queue_kinds_share_the_raw_union() {
    constexpr u32 production_order_id = 0x17u;
    constexpr u32 equipment_id = 0x42u;

    expect(ranker::ResolveUiOverlayActiveQueueRecordPayload(
               production_order_id, 0x777u) == production_order_id,
        "0x1ab active order icon did not retain raw +0x68");
    expect(ranker::ResolveUiOverlayActiveQueueRecordPayload(
               equipment_id, 0x888u) == equipment_id,
        "0x1ac active equipment icon did not retain raw +0x68");

    constexpr ranker::UiOverlayIconBlitRequest order_request =
        ranker::ResolveUiOverlayIndexedQueueIconRequest(
            0x1abu, production_order_id);
    expect(order_request.kind == ranker::UiOverlayIconBlitKind::production &&
            order_request.item_id == production_order_id,
        "0x1ab did not dispatch the raw payload through the production table");

    constexpr ranker::UiOverlayIconBlitRequest equipment_request =
        ranker::ResolveUiOverlayIndexedQueueIconRequest(
            0x1acu, equipment_id);
    expect(equipment_request.kind == ranker::UiOverlayIconBlitKind::equipment &&
            equipment_request.item_id == equipment_id,
        "0x1ac did not dispatch the raw payload through the equipment table");
}

void test_exact_active_and_deferred_state_dispatch() {
    expect(ranker::ResolveUiOverlayActiveQueueDispatchItem(0x50u) == 0x1aau &&
            ranker::ResolveUiOverlayActiveQueueDispatchItem(0x51u) == 0x1aau,
        "unit-production states did not dispatch item 0x1aa");
    expect(ranker::ResolveUiOverlayActiveQueueDispatchItem(0x4du) == 0x1abu &&
            ranker::ResolveUiOverlayActiveQueueDispatchItem(0x4eu) == 0x1abu,
        "production-order states did not dispatch item 0x1ab");
    expect(ranker::ResolveUiOverlayActiveQueueDispatchItem(0x82u) == 0x1acu &&
            ranker::ResolveUiOverlayActiveQueueDispatchItem(0x83u) == 0x1acu,
        "equipment states did not dispatch item 0x1ac");
    expect(ranker::ResolveUiOverlayActiveQueueDispatchItem(0x10000050u) == 0u &&
            ranker::ResolveUiOverlayActiveQueueDispatchItem(0x52u) == 0u,
        "active queue state dispatch compared only the low byte");

    expect(ranker::ResolveUiOverlayDeferredQueueDispatchItem(0x10u) == 0x1aau,
        "deferred unit-production state did not dispatch item 0x1aa");
    expect(ranker::ResolveUiOverlayDeferredQueueDispatchItem(0x17u) == 0x1abu,
        "deferred production-order state did not dispatch item 0x1ab");
    expect(ranker::ResolveUiOverlayDeferredQueueDispatchItem(0x22u) == 0x1acu,
        "deferred equipment state did not dispatch item 0x1ac");
    expect(ranker::ResolveUiOverlayDeferredQueueDispatchItem(0x10000010u) == 0u &&
            ranker::ResolveUiOverlayDeferredQueueDispatchItem(0x11u) == 0u,
        "deferred queue state dispatch compared only the low byte");
}

void test_original_queue_publication_gate() {
    using ranker::ShouldPublishUiOverlaySelectedStructureQueue;

    expect(ShouldPublishUiOverlaySelectedStructureQueue(
               1u, 0x80u, false, 0u, 0u, 0u),
        "local single selected structure queue was hidden");
    expect(!ShouldPublishUiOverlaySelectedStructureQueue(
               1u, 0x80u, false, 0u, 1u, 0u),
        "remote or allied structure queue leaked without an override");
    expect(ShouldPublishUiOverlaySelectedStructureQueue(
               1u, 0x80u, false, 2u, 1u, 0u),
        "player-type-2 observer could not inspect a remote queue");
    expect(ShouldPublishUiOverlaySelectedStructureQueue(
               1u, 0x80u, true, 0u, 1u, 0u),
        "replay/scenario override could not inspect a remote queue");
    expect(!ShouldPublishUiOverlaySelectedStructureQueue(
               2u, 0x80u, true, 2u, 0u, 0u),
        "multi-selection incorrectly published the primary structure queue");
    expect(!ShouldPublishUiOverlaySelectedStructureQueue(
               1u, 0x20u, true, 2u, 0u, 0u),
        "mobile selection incorrectly published a structure queue");
}

} // namespace

int main() {
    test_all_tribe_worker_production_uses_raw_command_value();
    test_other_active_queue_kinds_share_the_raw_union();
    test_exact_active_and_deferred_state_dispatch();
    test_original_queue_publication_gate();
    std::cout << "ACTIVE_QUEUE_ICON_PAYLOAD_PASS tribe-workers=0/16/32/48 "
                 "source=raw+0x68 tables=base+production+equipment "
                 "state-map=exact gate=single-owned-or-observer\n";
    return 0;
}
