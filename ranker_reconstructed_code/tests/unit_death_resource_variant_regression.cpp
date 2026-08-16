#include "ranker_resource_store.h"
#include "ranker_unit_death_resources.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {

using namespace ranker;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_sparse_original_manifest_parsing() {
    constexpr char manifest[] =
        "; Die spr list\r\n"
        "0 = \"Yes\"\t; enabled\r\n"
        "3 = \"No\"\r\n"
        "15=\"yEs\"\n"
        "169 = \"YES\"\n"
        "170 = \"Yes\"\n";
    const auto selected = ParseUnitDeathResourceManifest(
        reinterpret_cast<const u8*>(manifest), std::strlen(manifest));

    expect(selected[0], "manifest did not select unit 0");
    expect(!selected[3], "manifest selected a non-Yes row");
    expect(selected[15], "manifest comparison was not ASCII case-insensitive");
    expect(selected[169], "manifest did not select the final valid unit type");
    std::size_t count = 0;
    for (bool value : selected) {
        count += value ? 1u : 0u;
    }
    expect(count == 3, "manifest accepted a comment or out-of-range row");
    expect(kUnitDeathAnimationImageGroup == 3,
        "Change Death must replace original animation image group 3");
}

void test_in_place_resource_replacement() {
    ResetResourceStore();
    u32 entry_index = kInvalidResourceEntry;
    void* payload = nullptr;
    expect(AllocateResourceEntry(3, &entry_index, &payload),
        "could not allocate test resource entry");
    const std::array<u32, 6> initial_metadata{1, 2, 3, 4, 5, 6};
    expect(ConfigureResourceEntry(entry_index, initial_metadata, 42),
        "could not configure test resource entry");
    std::memcpy(payload, "old", 3);

    const ResourceStoreEntry* before = GetResourceEntry(entry_index);
    expect(before != nullptr, "allocated resource entry was unavailable");
    const u64 allocation_serial = before != nullptr ? before->allocation_serial : 0;
    const u32 next_entry = resource_store_state().next_entry;
    const std::array<u32, 6> replacement_metadata{7, 8, 9, 10, 11, 12};
    constexpr char replacement[] = "death";
    expect(ReplaceResourceEntryPayload(entry_index, replacement_metadata,
        replacement, sizeof(replacement) - 1),
        "in-place resource replacement failed");

    const ResourceStoreEntry* after = GetResourceEntry(entry_index);
    expect(after != nullptr, "replaced resource entry was unavailable");
    if (after != nullptr) {
        expect(after->metadata == replacement_metadata,
            "replacement did not publish serialized resource metadata");
        expect(after->palette_slot == 42,
            "replacement changed the unit palette binding");
        expect(after->allocation_serial == allocation_serial,
            "replacement changed the stable resource identity");
        expect(after->payload.size() == sizeof(replacement) - 1 &&
                std::memcmp(after->payload.data(), replacement,
                    sizeof(replacement) - 1) == 0,
            "replacement did not publish the death sprite payload");
    }
    expect(resource_store_state().next_entry == next_entry,
        "replacement allocated a new resource index");
}

}

int main() {
    test_sparse_original_manifest_parsing();
    test_in_place_resource_replacement();
    if (failures != 0) {
        std::fprintf(stderr, "%d regression(s) failed\n", failures);
        return 1;
    }
    std::puts("unit death resource variant regressions passed");
    return 0;
}
