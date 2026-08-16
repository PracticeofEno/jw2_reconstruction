#include "ranker_unit_string_slots.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* message) {
    std::cerr << "UNIT_STRING_SLOT_SESSION_HEADER_FAIL " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void write_slot(std::vector<u8>& header, u32 slot, const char* text) {
    const std::size_t offset = kSessionUnitStringTableOffset +
        static_cast<std::size_t>(slot) * kUnitStringSlotBytes;
    std::memcpy(header.data() + offset, text,
        std::min<std::size_t>(std::strlen(text), kUnitStringSlotBytes));
}

void verify_campaign_names_import_from_primary_record() {
    std::vector<u8> header(
        kSessionUnitStringTableOffset + kSessionUnitStringTableBytes + 4, 0);
    std::memcpy(header.data(), "JWAR", 4);
    // CP949 bytes for the authored Demon mission name "Kiana" (키아나).
    const std::array<char, 6> kiana{{
        static_cast<char>(0xc5), static_cast<char>(0xb0),
        static_cast<char>(0xbe), static_cast<char>(0xc6),
        static_cast<char>(0xb3), static_cast<char>(0xaa)}};
    const std::size_t kiana_offset = kSessionUnitStringTableOffset +
        2 * kUnitStringSlotBytes;
    std::memcpy(header.data() + kiana_offset, kiana.data(), kiana.size());
    write_slot(header, 3, "DarkElf");

    UnitStringSlotTable slots{};
    require(ImportUnitStringSlotsFromSessionHeader(
        slots, header.data(), header.size()), "valid primary table was rejected");
    require(std::memcmp(slots[2].data(), kiana.data(), kiana.size()) == 0,
        "CP949 campaign hero name was not imported exactly");
    require(std::strcmp(slots[3].data(), "DarkElf") == 0,
        "second authored unit name was not imported");
    require(slots[0][0] == '\0', "unnamed slot zero changed");
}

void verify_full_width_slot_and_save_round_trip() {
    UnitStringSlotTable slots{};
    for (std::size_t i = 0; i < slots[255].size(); ++i) {
        slots[255][i] = static_cast<char>('A' + i % 26);
    }

    std::vector<u8> header(
        kSessionUnitStringTableOffset + kSessionUnitStringTableBytes, 0xcc);
    require(ExportUnitStringSlotsToSessionHeader(
        slots, header.data(), header.size()), "valid export buffer was rejected");

    UnitStringSlotTable restored{};
    require(ImportUnitStringSlotsFromSessionHeader(
        restored, header.data(), header.size()), "exported table did not import");
    require(restored == slots, "256-slot table did not round-trip byte exactly");
}

void verify_truncated_primary_record_is_rejected() {
    std::vector<u8> short_header(
        kSessionUnitStringTableOffset + kSessionUnitStringTableBytes - 1, 0);
    UnitStringSlotTable slots{};
    slots[1][0] = 'X';
    require(!ImportUnitStringSlotsFromSessionHeader(
        slots, short_header.data(), short_header.size()),
        "truncated import buffer was accepted");
    require(slots[1][0] == 'X', "failed import modified the live table");
    require(!ExportUnitStringSlotsToSessionHeader(
        slots, short_header.data(), short_header.size()),
        "truncated export buffer was accepted");
}

}

int main() {
    verify_campaign_names_import_from_primary_record();
    verify_full_width_slot_and_save_round_trip();
    verify_truncated_primary_record_is_rejected();
    return EXIT_SUCCESS;
}
