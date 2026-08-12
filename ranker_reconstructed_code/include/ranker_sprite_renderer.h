#pragma once

#include "ranker_types.h"

#include <cstddef>

namespace ranker {

struct SpriteRenderTarget {
    u16* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;
    u32 stride_words = 0;
};

struct IndexedSpriteRenderTarget {
    u8* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;
    u32 stride_bytes = 0;
};

struct SpriteRenderState {
    SpriteRenderTarget target{};
    IndexedSpriteRenderTarget indexed_target{};
    u32 last_entry_index = 0;
    u32 last_mode = 0;
    u8 active_unit_palette_ramp = 0;
    bool active = false;
    bool indexed_active = false;
    bool blend_tables_built = false;
    bool blend_tables_pixel_mode_555 = false;
};

constexpr bool ShouldRebuildSpriteBlendTables(
    bool tables_built, bool cached_pixel_mode_555,
    bool requested_pixel_mode_555) {
    return !tables_built ||
        cached_pixel_mode_555 != requested_pixel_mode_555;
}

struct SpritePixelMaskConstants {
    u16 high_red = 0;
    u16 high_green = 0;
    u16 high_blue = 0;
    u16 high_red_green = 0;
    u16 low_blue_a = 0;
    u16 low_blue_b = 0;
    bool pixel_mode_555 = false;
};

enum class SpritePixelMorphStyle : u32 {
    unit_ramp_token1_shadow,
    palette,
    resource_mode,
    or_mask_token1_shadow,
    grayscale_token1_shadow,
    blend_factor_token2_plus,
    neighbor_copy,
    unit_ramp_or_mask_token1_shadow,
    channel_add_token1_shadow,
};

struct SpritePixelMorphDrawOptions {
    SpritePixelMorphStyle style =
        SpritePixelMorphStyle::unit_ramp_token1_shadow;
    u32 mode_or_factor = 0;
    u16 mask = 0;
    u16 red_delta = 0;
    u16 green_delta = 0;
    u16 blue_delta = 0;
    bool flipped = false;
};

void SetSpriteRenderTarget(u16* pixels, u32 width, u32 height, u32 stride_words = 0);
void SetIndexedSpriteRenderTarget(u8* pixels, u32 width, u32 height, u32 stride_bytes = 0);
void ClearSpriteRenderTarget();
void ClearIndexedSpriteRenderTarget();
void BuildSpriteBlendTables(bool pixel_mode_555);
void ConfigureSpritePixelMaskConstants(bool pixel_mode_555);
void SetSpriteUnitPaletteRamp(u8 ramp);
u8 SpriteUnitPaletteRamp();

bool DrawResourceSpriteNormal(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlipped(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteToken2Plus(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedToken2Plus(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteToken1Shadow(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteNeighborCopy(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedNeighborCopy(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteBlend(u32 entry_index, i32 x, i32 y, u32 blend_mode);
bool DrawResourceSpriteMode(u32 entry_index, i32 x, i32 y, u32 mode);
bool DrawImageResourceNormal(u32 entry_index, i32 x, i32 y);

bool BlitResourceSpriteNormal(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteFlipped(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteToken1Shadow(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteToken2Plus(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteFlippedToken2Plus(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteNeighborCopy(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteFlippedNeighborCopy(u32 entry_index, i32 x, i32 y);
bool DispatchResourceSpriteBlitMode(u32 entry_index, i32 x, i32 y, u32 mode);
bool BlitResourceSpriteBlendMode1(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteBlendMode2(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteBlendMode3(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteBlendMode4(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteBlendMode5(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteBlendMode6(u32 entry_index, i32 x, i32 y);
bool BlitResourceSpriteBlendMode7(u32 entry_index, i32 x, i32 y);
bool BlitImageResourceNormal(u32 entry_index, i32 x, i32 y);

bool DrawIndexedResourceSpriteToken1Preserve(u32 entry_index, i32 x, i32 y);
bool TouchIndexedResourceSpriteCoverage(u32 entry_index, i32 x, i32 y);
bool TouchIndexedResourceSpritePaletteRampCoverage(u32 entry_index, i32 x, i32 y);

bool DrawResourceSpriteUnitRampToken1Shadow(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedUnitRampToken1Shadow(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteBlendFactor(u32 entry_index, i32 x, i32 y, u32 source_weight_31);
bool DrawResourceSpriteFlippedBlendFactor(u32 entry_index, i32 x, i32 y, u32 source_weight_31);
bool DrawResourceSpriteDirectBlendFactor(u32 entry_index, i32 x, i32 y, u32 source_weight_31);
bool DrawResourceSpriteUnitRampPixelMorphTransition(u32 unit_type,
    u32 image_group, u32 source_frame, u32 target_frame,
    u32 source_entry_index, u32 target_entry_index, i32 x, i32 y,
    u32 subframe_index, bool flipped);
bool DrawResourceSpritePixelMorphTransition(u32 unit_type, u32 image_group,
    u32 source_frame, u32 target_frame, u32 source_entry_index,
    u32 target_entry_index, i32 x, i32 y, u32 subframe_index,
    const SpritePixelMorphDrawOptions& options);
void ResetResourceSpritePixelMorphCache();
bool DrawResourceSpriteDirectToken1Shadow(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteWidthLimitedToken1Shadow(
    u32 entry_index, i32 x, i32 y, u32 row_pixel_limit);
bool DrawResourceSpritePaletteIndexOffset(u32 entry_index, i32 x, i32 y, u8 index_offset);
bool DrawResourceSpriteToken1ShadowOrMask(u32 entry_index, i32 x, i32 y, u16 mask);
bool DrawResourceSpriteFlippedToken1ShadowOrMask(u32 entry_index, i32 x, i32 y, u16 mask);
bool DrawResourceSpriteHighRedMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedHighRedMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteHighGreenMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedHighGreenMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteHighBlueMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedHighBlueMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteUnitRampLowBlueMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedUnitRampLowBlueMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteGrayscale(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedGrayscale(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteLowBlueMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteFlippedLowBlueMask(u32 entry_index, i32 x, i32 y);
bool DrawResourceSpriteChannelAdditiveTint(
    u32 entry_index, i32 x, i32 y, u16 red_delta, u16 green_delta, u16 blue_delta);
bool DrawResourceSpriteFlippedChannelAdditiveTint(
    u32 entry_index, i32 x, i32 y, u16 red_delta, u16 green_delta, u16 blue_delta);
bool DrawResourceSpriteTableBlend(u32 entry_index, i32 x, i32 y);

const SpriteRenderState& sprite_render_state();
const SpritePixelMaskConstants& sprite_pixel_mask_constants();

}
