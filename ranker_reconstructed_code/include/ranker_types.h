#pragma once

#include <cstdint>

using i8 = std::int8_t;
using u8 = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;

struct RankerProcessState {
    u32 image_base = 0x00400000;
    u32 original_entry = 0x005257d0;
};
