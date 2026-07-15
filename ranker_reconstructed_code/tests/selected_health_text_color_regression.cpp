#include "ranker_ui_overlay.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using ranker::ResolveSelectedUnitHealthTextColor;

constexpr std::array<u32, 10> kExpected{{
    0x09u, 0x09u, 0x99u, 0x11u, 0x11u,
    0xa9u, 0xc1u, 0xc9u, 0xd1u, 0x71u,
}};

constexpr u32 original_health_color_step(
    u32 health, u32 adjusted_max_health) {
    return adjusted_max_health == 0u
        ? 0u
        : static_cast<u32>(
              static_cast<std::uint64_t>(health) * 4u /
              adjusted_max_health);
}

} // namespace

int main() {
    for (u32 index = 0; index < kExpected.size(); ++index) {
        if (ResolveSelectedUnitHealthTextColor(index) != kExpected[index]) {
            std::cerr << "selected HP color table mismatch at index "
                      << index << '\n';
            return 1;
        }
    }

    constexpr u32 kTailStep = original_health_color_step(10u, 4u);
    static_assert(kTailStep == 10u);
    constexpr std::array<u32, 5> kTailIndices{{
        kTailStep, 11u, 31u, 0x100u,
        std::numeric_limits<u32>::max(),
    }};
    for (u32 index : kTailIndices) {
        if (ResolveSelectedUnitHealthTextColor(index) != 0u) {
            std::cerr << "selected HP color table tail must be zero at index "
                      << index << '\n';
            return 1;
        }
    }

    std::cout << "SELECTED_HEALTH_TEXT_COLOR_PASS "
                 "entries=10 tail10+=0 unclamped-step=10\n";
    return 0;
}
