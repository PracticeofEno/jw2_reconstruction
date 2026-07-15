#include "ranker_ui_overlay.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_active_production_uses_raw_command_value() {
    // Captured Tyrano Nest worker production state: raw +0x68 is 0x20 while
    // its separately reconstructed target pointer is null.
    constexpr u32 requested_unit_type = 0x20u;

    constexpr u32 payload = ranker::ResolveUiOverlayActiveQueueRecordPayload(
        requested_unit_type, 0u);
    expect(payload == requested_unit_type,
        "null typed target selected Buildman frame zero instead of requested unit");
    expect(ranker::ResolveUiOverlayActiveQueueRecordPayload(
               requested_unit_type, 0x13579u) == requested_unit_type,
        "typed target id replaced the original raw +0x68 icon payload");

    constexpr ranker::UiOverlayIconBlitRequest request =
        ranker::ResolveUiOverlayIndexedQueueIconRequest(0x1aau, payload);
    expect(request.kind == ranker::UiOverlayIconBlitKind::base,
        "0x1aa active production did not select the base unit icon table");
    expect(request.item_id == requested_unit_type,
        "Tyrano worker production did not dispatch icon request item_id 0x20");
    expect(request.item_id != 0u,
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

} // namespace

int main() {
    test_active_production_uses_raw_command_value();
    test_other_active_queue_kinds_share_the_raw_union();
    std::cout << "ACTIVE_QUEUE_ICON_PAYLOAD_PASS tyrano-worker=0x20 "
                 "source=raw+0x68 tables=base+production+equipment\n";
    return 0;
}
