#pragma once

#include "ranker_types.h"

#include <array>

namespace ranker {

struct MinimapTerrainRenderConfig;

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

} // namespace ranker
