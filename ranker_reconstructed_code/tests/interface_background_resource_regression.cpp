#include "ranker_trc.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

namespace {

using namespace ranker;

u16 read_u16(const std::vector<u8>& bytes, std::size_t offset) {
    return static_cast<u16>(bytes[offset]) |
        static_cast<u16>(static_cast<u16>(bytes[offset + 1]) << 8);
}

u16 pack_565(const std::vector<u8>& palette, u8 index) {
    const std::size_t offset = static_cast<std::size_t>(index) * 4;
    const u8 red = palette[offset];
    const u8 green = palette[offset + 1];
    const u8 blue = palette[offset + 2];
    return static_cast<u16>(((red >> 3) << 11) |
        ((green >> 2) << 5) | (blue >> 3));
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct RegionStats {
    std::size_t pixels = 0;
    std::size_t transparent = 0;
    std::size_t black_mapped = 0;
    std::map<u8, std::size_t> indices;
};

RegionStats inspect_region(const std::vector<u8>& image,
    const std::vector<u8>& palette, u32 width, u32 height,
    u32 left, u32 top, u32 right, u32 bottom) {
    RegionStats stats{};
    right = std::min(right, width);
    bottom = std::min(bottom, height);
    for (u32 y = top; y < bottom; ++y) {
        for (u32 x = left; x < right; ++x) {
            const u8 index = image[12u + static_cast<std::size_t>(y) * width + x];
            ++stats.pixels;
            ++stats.indices[index];
            if (index == 0) {
                ++stats.transparent;
            }
            else if (pack_565(palette, index) == 0) {
                ++stats.black_mapped;
            }
        }
    }
    return stats;
}

void print_stats(const char* name, const RegionStats& stats) {
    std::cout << name << " pixels=" << stats.pixels
              << " transparent=" << stats.transparent
              << " black_mapped=" << stats.black_mapped
              << " unique=" << stats.indices.size() << '\n';
}

} // namespace

int main() {
    constexpr u32 kTheme = 2;
    constexpr u32 kBaseRecord = 0x133u + kTheme * 8u;
    std::vector<u8> palette;
    std::vector<u8> image;
    require(read_trc_record("RankerOCPV_Win/JW2_02.TRC", kBaseRecord, palette),
        "interface palette record must load");
    require(read_trc_record("RankerOCPV_Win/JW2_02.TRC", kBaseRecord + 1u, image),
        "interface background record must load");
    require(palette.size() >= 256u * 4u,
        "interface palette must contain 256 RGBA entries");
    require(image.size() >= 12u, "interface image header must load");

    const u32 width = read_u16(image, 0);
    const u32 height = read_u16(image, 2);
    std::cout << "image width=" << width << " height=" << height
              << " offset_x=" << static_cast<i16>(read_u16(image, 4))
              << " offset_y=" << static_cast<i16>(read_u16(image, 6)) << '\n';
    require(width == 800u, "theme-two interface image must be 800 pixels wide");
    require(height == 179u, "theme-two interface image must be 179 pixels high");
    require(image.size() >= 12u + static_cast<std::size_t>(width) * height,
        "interface image must contain the complete indexed surface");

    // At 800x600 theme two is placed at y=421.  These source-space regions
    // cover the visibly textured left selection panel and right command panel
    // while avoiding their outer bone borders.
    const RegionStats left = inspect_region(
        image, palette, width, height, 20, 98, 305, 178);
    const RegionStats right = inspect_region(
        image, palette, width, height, 465, 98, 790, 178);
    print_stats("left", left);
    print_stats("right", right);
    require(left.transparent < left.pixels,
        "left panel texture must contain drawable image indices");
    require(right.transparent < right.pixels,
        "right panel texture must contain drawable image indices");
    require(left.black_mapped * 20u < left.pixels &&
            right.black_mapped * 20u < right.pixels,
        "the original panel resource must be predominantly textured, not black");
    require(left.indices.size() > 128u && right.indices.size() > 128u,
        "both panel regions must retain the original textured color variety");

    std::cout << "INTERFACE_BACKGROUND_RESOURCE_PASS width=" << width
              << " height=" << height << "\n";
    return EXIT_SUCCESS;
}
