#include "ranker_game_session_tables.h"

#include <cstddef>
#include <iostream>
#include <vector>

namespace {

constexpr std::size_t kDefinitionBytes = 0x24bc;
constexpr std::size_t kPrimaryCountOffset = 0x240;
constexpr std::size_t kPrimaryBaseOffset = 0x244;

void write_u32(std::vector<u8>& bytes, std::size_t offset, u32 value) {
    bytes[offset + 0] = static_cast<u8>(value & 0xffu);
    bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xffu);
}

u32 read_u32(const std::vector<u8>& bytes, std::size_t offset) {
    return static_cast<u32>(bytes[offset + 0]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

void seed_primary(std::vector<ranker::RuntimeDefinitionRecord>& records,
    u32 type, const std::vector<u32>& references) {
    records[type].bytes.assign(kDefinitionBytes, 0);
    write_u32(records[type].bytes, 0, 1);
    write_u32(records[type].bytes, kPrimaryCountOffset,
        static_cast<u32>(references.size()));
    for (std::size_t index = 0; index < references.size(); ++index) {
        write_u32(records[type].bytes,
            kPrimaryBaseOffset + index * sizeof(u32), references[index]);
    }
}

bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    std::vector<ranker::RuntimeDefinitionRecord> records(
        ranker::kGameSessionUnitTypeCount);
    seed_primary(records, 0, {96, 111});
    seed_primary(records, 16, {127});
    seed_primary(records, 32, {128, 143});
    seed_primary(records, 48, {});

    bool ok = true;
    ok = require(ranker::kPostInitUnitTypes ==
            std::array<u32, 4>{{0, 16, 32, 48}} &&
            ranker::kPostInitRequiredTypes ==
            std::array<u32, 4>{{111, 127, 143, 159}},
        "the original DAT_00705030/DAT_00705020 pairs changed") && ok;

    ok = require(
        ranker::ApplyPostInitUnitRequirementToggleToRuntimeDefinitions(
            records, false),
        "normal sessions must remove the pristine references") && ok;
    ok = require(read_u32(records[0].bytes, kPrimaryCountOffset) == 1 &&
            read_u32(records[0].bytes, kPrimaryBaseOffset + sizeof(u32)) == 0,
        "type 0 did not remove reference 111 with original zero/no-compaction semantics") && ok;
    ok = require(read_u32(records[16].bytes, kPrimaryCountOffset) == 0 &&
            read_u32(records[16].bytes, kPrimaryBaseOffset) == 0,
        "type 16 did not remove reference 127") && ok;
    ok = require(read_u32(records[32].bytes, kPrimaryCountOffset) == 1 &&
            read_u32(records[32].bytes, kPrimaryBaseOffset + sizeof(u32)) == 0,
        "type 32 did not remove reference 143") && ok;
    ok = require(read_u32(records[48].bytes, kPrimaryCountOffset) == 0,
        "normal sessions must leave the absent type 48 -> 159 pair absent") && ok;
    ok = require(
        !ranker::ApplyPostInitUnitRequirementToggleToRuntimeDefinitions(
            records, false),
        "normal-session removal must be idempotent") && ok;

    ok = require(
        ranker::ApplyPostInitUnitRequirementToggleToRuntimeDefinitions(
            records, true),
        "transition sessions must append all four references") && ok;
    for (u32 pair = 0; pair < ranker::kPostInitUnitRequirementCount; ++pair) {
        const u32 type = ranker::kPostInitUnitTypes[pair];
        const u32 count = read_u32(records[type].bytes, kPrimaryCountOffset);
        ok = require(count != 0 &&
                read_u32(records[type].bytes,
                    kPrimaryBaseOffset + (count - 1) * sizeof(u32)) ==
                    ranker::kPostInitRequiredTypes[pair],
            "transition-session reference append mismatch") && ok;
    }
    ok = require(
        !ranker::ApplyPostInitUnitRequirementToggleToRuntimeDefinitions(
            records, true),
        "transition-session append must be idempotent") && ok;

    if (ok) {
        std::cout << "POST_INIT_UNIT_REQUIREMENT_RUNTIME_DEFINITION_PASS\n";
        return 0;
    }
    return 1;
}
