#pragma once

#include <cstdint>
#include <cstring>

using i8 = std::int8_t;
using u8 = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;

namespace ranker {

// Interpret an original 32-bit register/field value as signed without changing
// its bit pattern. A static_cast would be implementation-defined for values
// above INT32_MAX, which are common in reconstructed wraparound arithmetic.
inline i32 WrappedU32ToI32(u32 value) noexcept {
    i32 signed_value = 0;
    static_assert(sizeof(signed_value) == sizeof(value),
        "ranker requires 32-bit i32/u32 values");
    std::memcpy(&signed_value, &value, sizeof(signed_value));
    return signed_value;
}

} // namespace ranker

struct RankerProcessState {
    u32 image_base = 0x00400000;
    u32 original_entry = 0x005257d0;
};
