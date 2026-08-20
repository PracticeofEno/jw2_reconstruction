#pragma once

#include "ranker_types.h"

#include <array>

namespace ranker {

struct MinimapTerrainRenderConfig;
struct MinimapPixelFormat;

extern const std::array<i32, 8> kGameplayTerrainTileOffsets;
extern const std::array<u32, 6> kGameplayTerrainBlendCodeShifts;
extern const i32 kGameplayTerrainBlendMaskBaseOffset;
extern const std::array<i32, 132> kGameplayTerrainBlendMaskOffsets;
extern const i32 kGameplayOverlayPaletteBaseOffset;
extern const i32 kGameplayOverlayBlendMaskBaseOffset;
extern const std::array<i32, 256> kGameplayOverlayVariantOffsets;
extern const std::array<i32, 64> kGameplayOverlayBlendMaskOffsets;
extern const std::array<u8, 64> kGameplayOverlayMaskedKinds;
extern const std::array<i32, 82> kGameplaySpecialOverlayPaletteOffsets;
extern const std::array<i32, 82> kGameplaySpecialOverlayBlendMaskOffsets;

// Populate the immutable palette/mask lookup portion shared by live gameplay
// minimaps and lobby previews. Callers provide the active pixel format,
// animation bank, and loaded tile palette.
void ConfigureGameplayMinimapTerrainLayout(
    MinimapTerrainRenderConfig& config);

// Match FUN_00422280/FUN_004230e0/FUN_00423330: each source contribution is
// truncated to RGB555/565 independently before the two packed pixels are
// added. Combining the numerators before >> 5 produces terrain that is one
// component brighter.
u16 BlendGameplayTerrainPixels(const MinimapPixelFormat& format,
    u16 primary, u16 secondary, u32 weight31);

// FUN_00422280 handles width % 4 leading pixels one at a time, then its MMX
// loop reads one mask word for each four-pixel source block. All four pixels
// therefore share the first mask word's channel and weight.
u32 ResolveGameplayTerrainBlendMaskSampleColumn(
    u32 clipped_width, u32 clipped_column);

} // namespace ranker
