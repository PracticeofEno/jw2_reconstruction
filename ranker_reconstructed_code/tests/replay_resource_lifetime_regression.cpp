#include "ranker_resource_store.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void allocate_resources(u32 count) {
    for (u32 i = 0; i < count; ++i) {
        u32 entry = ranker::kInvalidResourceEntry;
        require(ranker::AllocateResourceEntry(1, &entry, nullptr),
            "resource allocation must succeed");
    }
}

} // namespace

int main() {
    using namespace ranker;

    ResetResourceStore();

    // This is the old reconstructed order: ordinary interface, terrain, unit.
    allocate_resources(15);
    u32 terrain_resource_start = resource_store_state().next_entry;
    allocate_resources(56);
    u32 terrain_resource_end = resource_store_state().next_entry;
    u64 terrain_resource_tail =
        GetResourceEntryAllocationSerial(
            terrain_resource_end - 1u);
    require(ResourceEntryRangeAllocationMatches(terrain_resource_start,
            terrain_resource_end, terrain_resource_tail),
        "first-session terrain ranges must be live");

    const u32 stale_unit_resource_start = resource_store_state().next_entry;
    allocate_resources(8);
    const u32 stale_unit_resource_end = resource_store_state().next_entry;
    const u64 stale_unit_resource_tail =
        GetResourceEntryAllocationSerial(stale_unit_resource_end - 1u);

    // Starting a replay used to rewind the ordinary interface and silently
    // discard both ranges while leaving active_bank/catalog.loaded unchanged.
    ReleaseResourceEntriesFrom(0);
    allocate_resources(33);
    require(!ResourceEntryRangeAllocationMatches(terrain_resource_start,
            terrain_resource_end, terrain_resource_tail),
        "a reused numeric terrain range must fail generation validation");
    require(!ResourceEntryRangeAllocationMatches(stale_unit_resource_start,
            stale_unit_resource_end, stale_unit_resource_tail),
        "a second-session unit reload must not release through stale images");
    // Rebuild terrain above the larger replay interface.  Its indices must no
    // longer overlap the 18 replay-only controls added after the normal HUD.
    terrain_resource_start = resource_store_state().next_entry;
    allocate_resources(56);
    terrain_resource_end = resource_store_state().next_entry;
    terrain_resource_tail =
        GetResourceEntryAllocationSerial(
            terrain_resource_end - 1u);
    require(terrain_resource_start == 33,
        "rebuilt terrain must start after all replay controls");
    require(ResourceEntryRangeAllocationMatches(terrain_resource_start,
            terrain_resource_end, terrain_resource_tail),
        "rebuilt second-session terrain ranges must be live");

    std::cout << "replay resource lifetime regression: PASS\n";
    return EXIT_SUCCESS;
}
