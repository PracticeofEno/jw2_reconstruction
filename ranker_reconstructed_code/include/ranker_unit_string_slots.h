#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>

namespace ranker {

constexpr u32 kUnitStringSlotCount = 0x100;
constexpr u32 kUnitStringSlotBytes = 0x14;
constexpr u32 kInvalidUnitStringSlot = 0xffffffffu;

// The original JW2 primary session record starts with "JWAR" and a DWORD,
// followed by the complete 256 x 20-byte dynamic unit-name table. OBC raw
// +0x48 stores an index into this table; slot zero is the unnamed sentinel.
constexpr std::size_t kSessionUnitStringTableOffset = 0x08;
constexpr std::size_t kSessionUnitStringTableBytes =
    static_cast<std::size_t>(kUnitStringSlotCount) * kUnitStringSlotBytes;

using UnitStringSlot = std::array<char, kUnitStringSlotBytes>;
using UnitStringSlotTable = std::array<UnitStringSlot, kUnitStringSlotCount>;
static_assert(sizeof(UnitStringSlot) == kUnitStringSlotBytes);
static_assert(sizeof(UnitStringSlotTable) == kSessionUnitStringTableBytes);

bool ImportUnitStringSlotsFromSessionHeader(UnitStringSlotTable& slots,
    const u8* header, std::size_t header_size);
bool ExportUnitStringSlotsToSessionHeader(const UnitStringSlotTable& slots,
    u8* header, std::size_t header_size);

}
