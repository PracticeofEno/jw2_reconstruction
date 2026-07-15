#include "ranker_gameplay_visibility.h"
#include "ranker_trc.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace ranker {

bool LoadTrcRecordAlloc(const char*, u32, std::vector<u8>&,
    std::size_t, TrcDirectoryEntry*) {
    return false;
}

bool SurfacePixelMode555() {
    return false;
}

} // namespace ranker

namespace {

using namespace ranker;

constexpr std::array<i32, 8> kDx{{0, 1, 1, 1, 0, -1, -1, -1}};
constexpr std::array<i32, 8> kDy{{-1, -1, 0, 1, 1, 1, 0, -1}};

u32 class_flags(u32 tile_class) {
    switch (tile_class) {
    case 1:
        return 0x10000000u;
    case 2:
        return 0x08000000u;
    default:
        return 0u;
    }
}

bool in_bounds(const GameplayVisibilityGrid& grid, i32 x, i32 y) {
    return x >= 0 && y >= 0 && static_cast<u32>(x) < grid.width &&
        static_cast<u32>(y) < grid.height;
}

u32 raw_class(const GameplayVisibilityGrid& grid, i32 x, i32 y) {
    if (!in_bounds(grid, x, y)) {
        return 0u;
    }
    const u32 flags = grid.current[
        static_cast<std::size_t>(y) * grid.width + static_cast<u32>(x)];
    if ((flags & 0x08000000u) != 0u) {
        return 2u;
    }
    return (flags & 0x10000000u) != 0u ? 1u : 0u;
}

u32 smoothed_class(const GameplayVisibilityGrid& grid, i32 x, i32 y) {
    const u32 current = raw_class(grid, x, y);
    if (current != 2u) {
        return current;
    }
    for (std::size_t direction = 0; direction < kDx.size(); ++direction) {
        const i32 neighbor_x = x + kDx[direction];
        const i32 neighbor_y = y + kDy[direction];
        if (in_bounds(grid, neighbor_x, neighbor_y) &&
            raw_class(grid, neighbor_x, neighbor_y) == 0u) {
            return 1u;
        }
    }
    return 2u;
}

u32 original_mask_oracle(
    const GameplayVisibilityGrid& grid, i32 x, i32 y) {
    const u32 current = smoothed_class(grid, x, y);
    if (current == 2u) {
        return 0xffu;
    }

    u32 revealed_neighbors = 0u;
    for (u32 direction = 0; direction < kDx.size(); ++direction) {
        const i32 neighbor_x = x + kDx[direction];
        const i32 neighbor_y = y + kDy[direction];
        if (in_bounds(grid, neighbor_x, neighbor_y) &&
            smoothed_class(grid, neighbor_x, neighbor_y) == 2u) {
            revealed_neighbors |= 1u << direction;
        }
    }
    if (revealed_neighbors != 0u) {
        return revealed_neighbors;
    }
    if (current == 1u) {
        return 0x1ffu;
    }

    u32 mask = 0x100u;
    for (u32 direction = 0; direction < kDx.size(); ++direction) {
        const i32 neighbor_x = x + kDx[direction];
        const i32 neighbor_y = y + kDy[direction];
        if (in_bounds(grid, neighbor_x, neighbor_y) &&
            smoothed_class(grid, neighbor_x, neighbor_y) == 1u) {
            mask |= 1u << direction;
        }
    }
    return mask;
}

GameplayVisibilityGrid make_grid(u32 width, u32 height, u32 fill_class) {
    GameplayVisibilityGrid grid{};
    grid.width = width;
    grid.height = height;
    grid.current.assign(
        static_cast<std::size_t>(width) * height, class_flags(fill_class));
    return grid;
}

void set_class(GameplayVisibilityGrid& grid, i32 x, i32 y, u32 tile_class) {
    grid.current[static_cast<std::size_t>(y) * grid.width +
        static_cast<u32>(x)] = class_flags(tile_class);
}

void require_mask_match(const GameplayVisibilityGrid& grid, i32 x, i32 y,
    std::size_t& comparisons) {
    const u32 expected = original_mask_oracle(grid, x, y);
    const u32 actual = ResolveGameplayFogBlockMask(grid, x, y);
    ++comparisons;
    if (actual != expected) {
        std::cerr << "fog mask mismatch at (" << x << ',' << y
                  << "): expected=0x" << std::hex << expected
                  << " actual=0x" << actual << std::dec << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void exhaust_three_by_three_boundaries(std::size_t& comparisons) {
    constexpr u32 kConfigurations = 19683u; // 3^9
    for (u32 encoded = 0; encoded < kConfigurations; ++encoded) {
        GameplayVisibilityGrid grid = make_grid(3, 3, 0);
        u32 digits = encoded;
        for (i32 y = 0; y < 3; ++y) {
            for (i32 x = 0; x < 3; ++x) {
                set_class(grid, x, y, digits % 3u);
                digits /= 3u;
            }
        }
        for (i32 y = 0; y < 3; ++y) {
            for (i32 x = 0; x < 3; ++x) {
                require_mask_match(grid, x, y, comparisons);
            }
        }
    }
}

void exhaust_interior_neighbor_patterns(std::size_t& comparisons) {
    constexpr std::array<i32, 9> kPatternX{{
        2, 2, 3, 3, 3, 2, 1, 1, 1,
    }};
    constexpr std::array<i32, 9> kPatternY{{
        2, 1, 1, 2, 3, 3, 3, 2, 1,
    }};
    constexpr u32 kConfigurations = 19683u; // 3^9
    for (u32 background = 0; background < 3; ++background) {
        for (u32 encoded = 0; encoded < kConfigurations; ++encoded) {
            GameplayVisibilityGrid grid = make_grid(5, 5, background);
            u32 digits = encoded;
            for (std::size_t index = 0; index < kPatternX.size(); ++index) {
                set_class(grid, kPatternX[index], kPatternY[index], digits % 3u);
                digits /= 3u;
            }
            require_mask_match(grid, 2, 2, comparisons);
        }
    }
}

void test_revealed_bit_priority(std::size_t& comparisons) {
    GameplayVisibilityGrid grid = make_grid(3, 3, 2);
    grid.current[4] = 0x18000000u;
    require_mask_match(grid, 1, 1, comparisons);
    if (ResolveGameplayFogBlockMask(grid, 1, 1) != 0xffu) {
        std::cerr << "bit 27 must take priority over bit 28\n";
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    std::size_t comparisons = 0;
    exhaust_three_by_three_boundaries(comparisons);
    exhaust_interior_neighbor_patterns(comparisons);
    test_revealed_bit_priority(comparisons);
    std::cout << "FOG_SMOOTHING_MASK_PASS comparisons=" << comparisons
              << " directions=N,NE,E,SE,S,SW,W,NW\n";
    return EXIT_SUCCESS;
}
