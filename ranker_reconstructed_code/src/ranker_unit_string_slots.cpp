#include "ranker_unit_string_slots.h"

#include <cstring>

namespace ranker {

bool ImportUnitStringSlotsFromSessionHeader(UnitStringSlotTable& slots,
    const u8* header, std::size_t header_size) {
    if (header == nullptr || header_size <
            kSessionUnitStringTableOffset + kSessionUnitStringTableBytes) {
        return false;
    }

    std::memcpy(slots.data(), header + kSessionUnitStringTableOffset,
        kSessionUnitStringTableBytes);
    return true;
}

bool ExportUnitStringSlotsToSessionHeader(const UnitStringSlotTable& slots,
    u8* header, std::size_t header_size) {
    if (header == nullptr || header_size <
            kSessionUnitStringTableOffset + kSessionUnitStringTableBytes) {
        return false;
    }

    std::memcpy(header + kSessionUnitStringTableOffset, slots.data(),
        kSessionUnitStringTableBytes);
    return true;
}

}
