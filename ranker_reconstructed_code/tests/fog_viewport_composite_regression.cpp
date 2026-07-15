#include "ranker_gameplay_visibility.h"
#include "ranker_trc.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>
#include <vector>

namespace ranker {

bool g_fog_composite_test_555 = false;

bool SurfacePixelMode555() {
    return g_fog_composite_test_555;
}

bool LoadTrcRecordAlloc(const char*, u32, std::vector<u8>&,
    std::size_t, TrcDirectoryEntry*) {
    return false;
}

}  // namespace ranker

namespace {

using namespace ranker;

constexpr std::array<i32, 8> kDx{{0, 1, 1, 1, 0, -1, -1, -1}};
constexpr std::array<i32, 8> kDy{{-1, -1, 0, 1, 1, 1, 0, -1}};
constexpr u32 kOriginalClassStride = 0x28;
constexpr u32 kOriginalClassRows = 0x28;

bool in_map(const GameplayVisibilityGrid& grid, i32 x, i32 y) {
    return x >= 0 && y >= 0 && static_cast<u32>(x) < grid.width &&
        static_cast<u32>(y) < grid.height;
}

u8 original_raw_class(const GameplayVisibilityGrid& grid, i32 x, i32 y) {
    // FUN_004202f0 reads the fixed 256x256 visibility layer.  Valid gameplay
    // cameras can touch one cleared tile beyond the scenario dimensions; the
    // typed reconstruction represents that cleared tail as an out-of-map zero.
    if (!in_map(grid, x, y)) {
        return 0;
    }
    const u32 flags = grid.current[
        static_cast<std::size_t>(y) * grid.width + static_cast<u32>(x)];
    if ((flags & 0x08000000u) != 0) {
        return 2;
    }
    return (flags & 0x10000000u) != 0 ? 1 : 0;
}

u8 original_smoothed_class(const GameplayVisibilityGrid& grid, i32 x, i32 y) {
    const u8 current = original_raw_class(grid, x, y);
    if (current != 2) {
        return current;
    }
    // FUN_00420370 only lets actual in-map class-zero neighbors demote a
    // fully revealed tile.  The fixed storage tail is not a smoothing input.
    for (std::size_t direction = 0; direction < kDx.size(); ++direction) {
        const i32 nx = x + kDx[direction];
        const i32 ny = y + kDy[direction];
        if (in_map(grid, nx, ny) && original_raw_class(grid, nx, ny) == 0) {
            return 1;
        }
    }
    return 2;
}

struct OriginalClassGrid {
    std::array<u8, kOriginalClassStride * kOriginalClassRows> values{};
    u32 columns = 0;
    u32 rows = 0;
};

OriginalClassGrid build_original_class_grid(const GameplayVisibilityGrid& grid,
    i32 camera_x, i32 camera_y, u32 viewport_width, u32 viewport_height) {
    OriginalClassGrid result{};
    const i32 start_x = camera_x >= 0 ? camera_x >> 5 : (camera_x + 31) >> 5;
    const i32 start_y = camera_y >= 0 ? camera_y >> 5 : (camera_y + 31) >> 5;
    result.columns = static_cast<u32>(
        (static_cast<u32>(camera_x + static_cast<i32>(viewport_width)) >> 5) -
        start_x + 1);
    result.rows = static_cast<u32>(
        (static_cast<u32>(camera_y + static_cast<i32>(viewport_height)) >> 5) -
        start_y + 1);
    if (result.columns > kOriginalClassStride ||
        result.rows > kOriginalClassRows) {
        std::cerr << "test viewport exceeds original 40x40 fog class buffer\n";
        std::exit(EXIT_FAILURE);
    }
    for (u32 row = 0; row < result.rows; ++row) {
        for (u32 column = 0; column < result.columns; ++column) {
            result.values[static_cast<std::size_t>(row) *
                    kOriginalClassStride + column] =
                original_smoothed_class(grid,
                    start_x + static_cast<i32>(column),
                    start_y + static_cast<i32>(row));
        }
    }
    return result;
}

u32 original_resolve_mask(const OriginalClassGrid& classes,
    u32 viewport_width, u32 viewport_height, u32 x, u32 y) {
    const auto class_at = [&classes](u32 column, u32 row) {
        return classes.values[static_cast<std::size_t>(row) *
            kOriginalClassStride + column];
    };
    const u8 current = class_at(x, y);
    if (current == 2) {
        return 0xff;
    }

    const u32 neighbor_width = (viewport_width >> 5) + 1;
    const u32 neighbor_height = (viewport_height >> 5) + 1;
    const auto neighbor_mask = [&](u8 expected) {
        u32 mask = 0;
        for (u32 direction = 0; direction < kDx.size(); ++direction) {
            const i32 nx = static_cast<i32>(x) + kDx[direction];
            const i32 ny = static_cast<i32>(y) + kDy[direction];
            if (nx >= 0 && ny >= 0 && static_cast<u32>(nx) < neighbor_width &&
                static_cast<u32>(ny) < neighbor_height &&
                class_at(static_cast<u32>(nx), static_cast<u32>(ny)) == expected) {
                mask |= 1u << direction;
            }
        }
        return mask;
    };

    const u32 revealed_neighbors = neighbor_mask(2);
    if (revealed_neighbors != 0) {
        return revealed_neighbors;
    }
    if (current == 1) {
        return 0x1ff;
    }
    return 0x100 | neighbor_mask(1);
}

u16 original_scalar_pixel(u16 pixel, u8 factor, bool mode_555) {
    if (factor == 0) {
        return 0;
    }
    if (factor == 0x1f) {
        return pixel;
    }
    const u16 rb = mode_555 ? 0x7c1fu : 0xf81fu;
    const u16 green = mode_555 ? 0x03e0u : 0x07e0u;
    switch (factor) {
    case 1:
    case 2:
        return static_cast<u16>((pixel & (mode_555 ? 0x4210u : 0x8610u)) >> 4);
    case 4:
        return static_cast<u16>((pixel & (mode_555 ? 0x6318u : 0xc718u)) >> 3);
    case 8:
        return static_cast<u16>((pixel & (mode_555 ? 0x739cu : 0xe79cu)) >> 2);
    case 0x0f:
    case 0x10:
        return static_cast<u16>((pixel & (mode_555 ? 0x7bdeu : 0xf7deu)) >> 1);
    default:
        return static_cast<u16>(((((pixel & rb) * factor) >> 5) & rb) |
            ((((pixel & green) * factor) >> 5) & green));
    }
}

u16 original_fast_pixel(u16 pixel, u8 factor, bool mode_555) {
    return factor == 0x1e ? pixel : original_scalar_pixel(pixel, factor, mode_555);
}

void original_clear_block(std::vector<u16>& pixels, u32 width, u32 height,
    u32 stride, i32 x, i32 y) {
    for (i32 row = 0; row < 32; ++row) {
        const i32 py = y + row;
        if (py < 0 || static_cast<u32>(py) >= height) {
            continue;
        }
        for (i32 column = 0; column < 32; ++column) {
            const i32 px = x + column;
            if (px >= 0 && static_cast<u32>(px) < width) {
                pixels[static_cast<std::size_t>(py) * stride +
                    static_cast<u32>(px)] = 0;
            }
        }
    }
}

void original_draw_block(std::vector<u16>& pixels, u32 width, u32 height,
    u32 stride, const std::vector<u8>& masks, u32 mask, i32 x, i32 y,
    bool mode_555) {
    const u8* source = masks.data() +
        static_cast<std::size_t>(mask) * kGameplayFogMaskBytesPerBlock;
    const bool fully_inside = x >= 0 && y >= 0 &&
        static_cast<u32>(x + 32) <= width && static_cast<u32>(y + 32) <= height;
    if (fully_inside) {
        for (u32 row = 0; row < 32; ++row) {
            u16* destination = pixels.data() +
                static_cast<std::size_t>(y + static_cast<i32>(row)) * stride + x;
            const u8* source_row = source + static_cast<std::size_t>(row) * 32;
            for (u32 column = 0; column < 32; column += 2) {
                const u8 factor = source_row[column];
                destination[column] = original_fast_pixel(
                    destination[column], factor, mode_555);
                destination[column + 1] = original_fast_pixel(
                    destination[column + 1], factor, mode_555);
            }
        }
        return;
    }
    for (i32 row = 0; row < 32; ++row) {
        const i32 py = y + row;
        if (py < 0 || static_cast<u32>(py) >= height) {
            continue;
        }
        const u8* source_row = source + static_cast<std::size_t>(row) * 32;
        for (i32 column = 0; column < 32; ++column) {
            const i32 px = x + column;
            if (px >= 0 && static_cast<u32>(px) < width) {
                u16& pixel = pixels[static_cast<std::size_t>(py) * stride +
                    static_cast<u32>(px)];
                pixel = original_scalar_pixel(
                    pixel, source_row[column], mode_555);
            }
        }
    }
}

void original_render(std::vector<u16>& pixels, u32 width, u32 height,
    u32 stride, const GameplayVisibilityGrid& grid,
    const std::vector<u8>& masks, i32 camera_x, i32 camera_y, bool mode_555) {
    const OriginalClassGrid classes = build_original_class_grid(
        grid, camera_x, camera_y, width, height);
    const i32 offset_x = camera_x & 31;
    const i32 offset_y = camera_y & 31;
    for (i32 block_y = 0; block_y - offset_y < static_cast<i32>(height);
         block_y += 32) {
        for (i32 block_x = 0; block_x - offset_x < static_cast<i32>(width);
             block_x += 32) {
            const u32 column = static_cast<u32>(block_x >> 5);
            const u32 row = static_cast<u32>(block_y >> 5);
            const u32 mask = original_resolve_mask(
                classes, width, height, column, row);
            const i32 draw_x = block_x - offset_x;
            const i32 draw_y = block_y - offset_y;
            if (mask == 0x100) {
                original_clear_block(pixels, width, height, stride, draw_x, draw_y);
            } else if (mask != 0xff) {
                original_draw_block(pixels, width, height, stride,
                    masks, mask, draw_x, draw_y, mode_555);
            }
        }
    }
}

GameplayVisibilityGrid make_grid(u32 width, u32 height, u32 pattern) {
    GameplayVisibilityGrid grid{};
    grid.width = width;
    grid.height = height;
    grid.current.resize(static_cast<std::size_t>(width) * height);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            u32 tile_class = 0;
            if (pattern == 0) {
                const i32 dx = static_cast<i32>(x) - 48;
                const i32 dy = static_cast<i32>(y) - 42;
                const u32 distance = static_cast<u32>(std::abs(dx) + std::abs(dy));
                tile_class = distance < 16 ? 2 : (distance < 28 ? 1 : 0);
                if (((x + y * 3) % 23) == 0) {
                    tile_class = (tile_class + 1) % 3;
                }
            } else {
                const u32 hash = x * 0x45d9f3bu ^ y * 0x119de1f3u ^
                    ((x + 11) * (y + 29) * 17u);
                tile_class = (hash >> ((x + y) & 7)) % 3;
            }
            u32 flags = tile_class == 2 ? 0x08000000u :
                (tile_class == 1 ? 0x10000000u : 0u);
            if (tile_class == 2 && ((x ^ y) & 7) == 0) {
                flags |= 0x10000000u;  // verify bit-27 priority in the composite.
            }
            grid.current[static_cast<std::size_t>(y) * width + x] = flags;
        }
    }
    return grid;
}

std::vector<u8> make_mask_table() {
    std::vector<u8> masks(kGameplayFogMaskTableBytes);
    for (u32 mask = 0; mask < kGameplayFogMaskCount; ++mask) {
        for (u32 y = 0; y < 32; ++y) {
            for (u32 x = 0; x < 32; ++x) {
                masks[static_cast<std::size_t>(mask) * 1024 + y * 32 + x] =
                    static_cast<u8>((mask * 13u + y * 7u + x * 11u +
                        ((x ^ y) * 3u)) & 0x1fu);
            }
        }
    }
    return masks;
}

std::vector<u16> make_pixels(u32 height, u32 stride, u32 salt) {
    std::vector<u16> pixels(static_cast<std::size_t>(stride) * height);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < stride; ++x) {
            const u32 value = x * 0x421u + y * 0x139u + (x ^ y) * 0x51u + salt;
            pixels[static_cast<std::size_t>(y) * stride + x] =
                static_cast<u16>((value ^ 0xa55au) & 0xffffu);
        }
    }
    return pixels;
}

void compare_case(const GameplayVisibilityGrid& grid,
    const std::vector<u8>& masks, u32 width, u32 height,
    i32 camera_x, i32 camera_y, bool mode_555, u32 pattern,
    std::size_t& comparisons) {
    const u32 stride = width + 7;
    const u32 salt = static_cast<u32>(camera_x * 3 + camera_y * 5) +
        (mode_555 ? 0x5555u : 0x5656u) + pattern * 0x1111u;
    std::vector<u16> expected = make_pixels(height, stride, salt);
    std::vector<u16> actual = expected;
    original_render(expected, width, height, stride,
        grid, masks, camera_x, camera_y, mode_555);

    g_fog_composite_test_555 = mode_555;
    GameplayFogRenderContext context{};
    context.grid = const_cast<GameplayVisibilityGrid*>(&grid);
    context.target = {actual.data(), width, height, stride};
    context.fog_mask_table = masks.data();
    context.fog_mask_table_bytes = masks.size();
    context.camera_x = camera_x;
    context.camera_y = camera_y;
    // Deliberately stale metrics exercise the renderer's resolution refresh.
    context.metrics = {1, 1, -1, -1, -1};
    RenderGameplayFogOverlay(context);

    ++comparisons;
    if (actual == expected) {
        return;
    }
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < stride; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * stride + x;
            if (actual[index] != expected[index]) {
                std::cerr << "FOG_VIEWPORT_COMPOSITE_MISMATCH"
                          << " size=" << width << 'x' << height
                          << " camera=" << camera_x << ',' << camera_y
                          << " mode=" << (mode_555 ? 555 : 565)
                          << " pattern=" << pattern
                          << " pixel=" << x << ',' << y
                          << " expected=0x" << std::hex << expected[index]
                          << " actual=0x" << actual[index] << std::dec << '\n';
                std::exit(EXIT_FAILURE);
            }
        }
    }
}

}  // namespace

int main() {
    constexpr u32 kMapWidth = 128;
    constexpr u32 kMapHeight = 96;
    const std::vector<u8> masks = make_mask_table();
    const std::array<std::pair<u32, u32>, 5> resolutions{{
        {640, 480}, {800, 600}, {1024, 768}, {801, 487}, {641, 496},
    }};
    std::size_t comparisons = 0;
    for (u32 pattern = 0; pattern < 2; ++pattern) {
        const GameplayVisibilityGrid grid = make_grid(kMapWidth, kMapHeight, pattern);
        for (const auto& resolution : resolutions) {
            const u32 width = resolution.first;
            const u32 height = resolution.second;
            const i32 max_x = static_cast<i32>(kMapWidth * 32 - width);
            const i32 max_y = static_cast<i32>(kMapHeight * 32 - height);
            std::set<std::pair<i32, i32>> cameras{{
                {0, 0}, {1, 1}, {15, 17}, {31, 31}, {32, 32}, {63, 95},
                {max_x, max_y}, {std::max(0, max_x - 31), std::max(0, max_y - 17)},
            }};
            for (const auto& camera : cameras) {
                for (bool mode_555 : {false, true}) {
                    compare_case(grid, masks, width, height,
                        camera.first, camera.second, mode_555,
                        pattern, comparisons);
                }
            }
        }
    }
    std::cout << "FOG_VIEWPORT_COMPOSITE_PASS comparisons=" << comparisons
              << " resolutions=" << resolutions.size()
              << " patterns=2 modes=565,555 camera_edges=covered\n";
    return EXIT_SUCCESS;
}
