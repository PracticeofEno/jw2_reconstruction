#include "ranker_gameplay_terrain_layout.h"

#include "ranker_map_brush.h"

namespace ranker {

const std::array<i32, 8> kGameplayTerrainTileOffsets{
    0x000, 0x0c8, 0x190, 0x258, 0x320, 0x3e8, 0x4b0, 0x960};
const std::array<u32, 6> kGameplayTerrainBlendCodeShifts{
    0, 6, 0x0c, 3, 9, 0x0f};
const i32 kGameplayTerrainBlendMaskBaseOffset = 0x0bb8;
const std::array<i32, 132> kGameplayTerrainBlendMaskOffsets{
    0x85, 0x14d, 0x15, 0xdd, 0x18, 0xe0, 0x1b, 0xe3, 0x1e, 0xe6, 0x21, 0xe9,
    0x35, 0xfd, 0x49, 0x111, 0x5d, 0x125, 0x24, 0xec, 0x38, 0x100, 0x4c, 0x114,
    0x3d, 0x105, 0x40, 0x108, 0x43, 0x10b, 0x46, 0x10e, 0x65, 0x12d, 0x79, 0x141,
    0x68, 0x130, 0x7c, 0x144, 0x6b, 0x133, 0x7f, 0x147, 0x6e, 0x136, 0x82, 0x14a,
    0xb5, 0x17d, 0xa4, 0x16c, 0xb8, 0x180, 0xa7, 0x16f, 0xaa, 0x172, 0xad, 0x175,
    0xc1, 0x189, 0xc4, 0x18c, 0xbb, 0x183, 0xbe, 0x186, 0x74, 0x13c, 0x9c, 0x164,
    0x9e, 0x166, 0x76, 0x13e, 0x4e, 0x116, 0x3a, 0x102, 0xc6, 0x18e, 0x26, 0xee,
    0x0, 0xc8, 0x3, 0xcb, 0x6, 0xce, 0x9, 0xd1, 0xc, 0xd4, 0xf, 0xd7,
    0x12, 0xda, 0x28, 0xf0, 0x2b, 0xf3, 0xa1, 0x169, 0x2e, 0xf6, 0x31, 0xf9,
    0x50, 0x118, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
const i32 kGameplayOverlayPaletteBaseOffset = 0x0960;
const i32 kGameplayOverlayBlendMaskBaseOffset = 0x0d48;
const std::array<i32, 256> kGameplayOverlayVariantOffsets{
    0, 0, 0, 0, 23, 33, 223, 233, 24, 34, 224, 234, 25, 35, 225, 235,
    26, 36, 226, 236, 42, 52, 242, 252, 43, 53, 243, 253, 44, 54, 244, 254,
    45, 55, 245, 255, 46, 56, 246, 256, 47, 57, 247, 257, 61, 71, 261, 271,
    62, 72, 262, 272, 63, 73, 263, 273, 64, 74, 264, 274, 65, 75, 265, 275,
    66, 76, 266, 276, 67, 77, 267, 277, 68, 78, 268, 278, 81, 91, 281, 291,
    82, 92, 282, 292, 83, 93, 283, 293, 84, 94, 284, 294, 85, 95, 285, 295,
    86, 96, 286, 296, 87, 97, 287, 297, 88, 98, 288, 298, 101, 111, 301, 311,
    102, 112, 302, 312, 103, 113, 303, 313, 104, 114, 304, 314, 105, 115, 305, 315,
    106, 116, 306, 316, 107, 117, 307, 317, 108, 118, 308, 318, 121, 131, 321, 331,
    122, 132, 322, 332, 123, 133, 323, 333, 124, 134, 324, 334, 125, 135, 325, 335,
    126, 136, 326, 336, 127, 137, 327, 337, 128, 138, 328, 338, 143, 153, 343, 353,
    144, 154, 344, 354, 145, 155, 345, 355, 146, 156, 346, 356, 161, 171, 361, 371,
    162, 172, 362, 372, 181, 191, 381, 391, 182, 192, 382, 392, 0, 16842752, 257, 16843008,
    1, 0, 0, 0, 0, 0, 0, 16777216, 65793, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, 23, 24, -1, -1, -1, -1, 29, 30, 43, 44, 45, 46};
const std::array<i32, 64> kGameplayOverlayBlendMaskOffsets{
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 23, 24, -1, -1, -1,
    -1, 29, 30, 43, 44, 45, 46, 47, 48, 49, 50, 65, 66, 67, 68, -1,
    -1, -1, -1, 0, 10, 0, 0, 0, 0, 0, 5, 7, 0, 0, 0, 0};
const std::array<u8, 64> kGameplayOverlayMaskedKinds{
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
const std::array<i32, 82> kGameplaySpecialOverlayPaletteOffsets{
    0, 21, 22, 41, 42, 43, 44, 61, 62, 63, 64, 65, 66, 81,
    82, 83, 84, 85, 86, 101, 102, 103, 104, 105, 106, 121, 122, 123,
    124, 125, 126, 141, 142, 143, 144, 145, 146, 163, 164, 165, 166, 0,
    31, 32, 49, 50, 51, 52, 67, 68, 69, 70, 71, 72, 87, 88,
    89, 90, 91, 92, 107, 108, 109, 110, 111, 112, 127, 128, 129, 130,
    131, 132, 147, 148, 149, 150, 151, 152, 167, 168, 169, 170};
const std::array<i32, 82> kGameplaySpecialOverlayBlendMaskOffsets{
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 45,
    46, -1, -1, -1, -1, 65, 21, -1, -1, -1, -1, 45, 46, -1,
    -1, -1, -1, 65, 66, 45, 46, 47, 48, 65, 66, 67, 68, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 47, 48, -1, -1, -1, -1, 22, 68, -1, -1, -1, -1,
    47, 48, 45, 46, 47, 48, 67, 68, 65, 66, 67, 68};

void ConfigureGameplayMinimapTerrainLayout(
    MinimapTerrainRenderConfig& config) {
    config.terrain_palette_offsets = kGameplayTerrainTileOffsets;
    config.overlay_palette_base_offset = kGameplayOverlayPaletteBaseOffset;
    for (std::size_t overlay = 0;
         overlay < config.overlay_variant_offsets.size(); ++overlay) {
        for (std::size_t variant = 0;
             variant < config.overlay_variant_offsets[overlay].size();
             ++variant) {
            const std::size_t index = overlay * 4u + variant;
            config.overlay_variant_offsets[overlay][variant] =
                index < kGameplayOverlayVariantOffsets.size() ?
                    kGameplayOverlayVariantOffsets[index] : 0;
        }
    }

    config.terrain_blend_mask_base_offset =
        kGameplayTerrainBlendMaskBaseOffset;
    for (std::size_t mask = 0;
         mask < config.terrain_blend_mask_offsets.size(); ++mask) {
        for (std::size_t variant = 0;
             variant < config.terrain_blend_mask_offsets[mask].size();
             ++variant) {
            const std::size_t index = mask * 2u + variant;
            config.terrain_blend_mask_offsets[mask][variant] =
                index < kGameplayTerrainBlendMaskOffsets.size() ?
                    kGameplayTerrainBlendMaskOffsets[index] : 0;
        }
    }

    config.overlay_blend_mask_base_offset =
        kGameplayOverlayBlendMaskBaseOffset;
    for (std::size_t overlay = 0;
         overlay < config.overlay_blend_mask_offsets.size(); ++overlay) {
        config.overlay_blend_mask_offsets[overlay] =
            overlay < kGameplayOverlayBlendMaskOffsets.size() ?
                kGameplayOverlayBlendMaskOffsets[overlay] : -1;
    }
}

} // namespace ranker
