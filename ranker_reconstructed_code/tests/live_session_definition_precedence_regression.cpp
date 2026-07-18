#include "ranker_game_session_tables.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

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

bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    constexpr u32 kWorkerType = 32;
    constexpr std::size_t kDefinitionBytes = 0x24bc;
    constexpr std::size_t kPrimaryCountOffset = 0x240;
    constexpr std::size_t kPrimaryBaseOffset = 0x244;

    std::vector<u8> catalog(kDefinitionBytes, 0);
    write_u32(catalog, kPrimaryCountOffset, 11);
    write_u32(catalog, kPrimaryBaseOffset + 10 * sizeof(u32), 143);

    std::vector<ranker::RuntimeDefinitionRecord> active(0xaa);
    active[kWorkerType].bytes = catalog;
    write_u32(active[kWorkerType].bytes, kPrimaryCountOffset, 10);

    const std::vector<u8>* selected =
        ranker::SelectLiveSessionUnitDefinitionBytes(
            active, kWorkerType, &catalog, kDefinitionBytes);
    bool ok = true;
    ok = require(selected == &active[kWorkerType].bytes,
             "complete active session definition must override the TRC catalog") && ok;
    ok = require(read_u32(*selected, kPrimaryCountOffset) == 10,
             "the session-removed type-143 reference must remain removed") && ok;

    active[kWorkerType].bytes.resize(0xd0);
    selected = ranker::SelectLiveSessionUnitDefinitionBytes(
        active, kWorkerType, &catalog, kDefinitionBytes);
    ok = require(selected == &catalog,
             "a compact US_UB row cannot replace a complete definition") && ok;
    ok = require(read_u32(*selected, kPrimaryCountOffset) == 11 &&
             read_u32(*selected, kPrimaryBaseOffset + 10 * sizeof(u32)) == 143,
             "catalog fallback contents changed unexpectedly") && ok;

    if (ok) {
        std::cout << "LIVE_SESSION_DEFINITION_PRECEDENCE_PASS\n";
        return 0;
    }
    return 1;
}
