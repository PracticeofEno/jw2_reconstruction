#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>

namespace ranker {

template <std::size_t N>
u32 ReadOriginalRandomScrambleDword(const std::array<u32, N>& scramble,
    u32 byte_offset) {
    const std::size_t offset = static_cast<std::size_t>(byte_offset);
    if (offset + sizeof(u32) > N * sizeof(u32)) {
        return 0;
    }

    u32 value = 0;
    for (std::size_t i = 0; i < sizeof(u32); ++i) {
        const std::size_t source_byte = offset + i;
        const u32 source_word = scramble[source_byte / sizeof(u32)];
        const u32 byte_value =
            (source_word >> ((source_byte % sizeof(u32)) * 8u)) & 0xffu;
        value |= byte_value << (i * 8u);
    }
    return value;
}

} // namespace ranker
