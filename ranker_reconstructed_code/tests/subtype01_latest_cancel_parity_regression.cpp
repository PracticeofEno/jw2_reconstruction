#include "ranker_gameplay_packets.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* message) {
    std::cerr << "SUBTYPE01_LATEST_CANCEL_PARITY_FAIL " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect_route(bool queued, u32 state, Subtype01LatestCancelRoute expected,
    const char* message) {
    if (ResolveSubtype01LatestCancelRoute(queued, state) != expected) {
        fail(message);
    }
}

} // namespace

int main() {
    // P2PDrop_20260805_010429: ESC publishes logical index -1 while the
    // selected type-112 producer has two queued ordinary state-0x10 orders.
    // Original 0x004dcb64 falls through and removes/refunds the queue tail.
    expect_route(true, 0x10u,
        Subtype01LatestCancelRoute::ordinary_production,
        "queued ordinary production was rejected");

    expect_route(true, 0x17u,
        Subtype01LatestCancelRoute::placement_resource,
        "queued placement did not reroute to subtype 0x0c");
    expect_route(true, 0x22u,
        Subtype01LatestCancelRoute::production_cost,
        "queued production cost did not reroute to subtype 0x1a");
    expect_route(true, 0x99u,
        Subtype01LatestCancelRoute::ordinary_production,
        "original queued-state fallthrough was not preserved");

    expect_route(false, 0x50u,
        Subtype01LatestCancelRoute::ordinary_production,
        "active production-start state was rejected");
    expect_route(false, 0x51u,
        Subtype01LatestCancelRoute::ordinary_production,
        "active production-cycle state was rejected");
    expect_route(false, 0x4du,
        Subtype01LatestCancelRoute::placement_resource,
        "active placement state was not rerouted");
    expect_route(false, 0x82u,
        Subtype01LatestCancelRoute::production_cost,
        "active production-cost state was not rerouted");
    expect_route(false, 0x01u,
        Subtype01LatestCancelRoute::reject,
        "unrelated active state was accepted");

    std::cout << "SUBTYPE01_LATEST_CANCEL_PARITY_PASS\n";
    return EXIT_SUCCESS;
}
