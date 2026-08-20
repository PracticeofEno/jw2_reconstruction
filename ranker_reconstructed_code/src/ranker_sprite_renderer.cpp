#include "ranker_sprite_renderer.h"

#include "ranker_palette_cache.h"
#include "ranker_resource_store.h"

#include <algorithm>
#include <array>

namespace ranker {
namespace {

constexpr u32 kBlendModeCount = 8;
constexpr u32 kFiveBitTableSide = 0x20;
constexpr u32 kFiveBitTableEntries = kFiveBitTableSide * kFiveBitTableSide;
constexpr u32 kGreenTableSide = 0x40;
constexpr u32 kGreenTableEntries = kGreenTableSide * kGreenTableSide;

enum class OffsetPair {
    Normal,
    Flipped,
};

enum class SpritePixelOp {
    Palette,
    PaletteToken2Plus,
    Token1Shadow,
    NeighborCopy,
    Blend,
    BlendFactorToken2Plus,
    OrMaskToken1Shadow,
    GrayscaleToken1Shadow,
    ChannelAddToken1Shadow,
    PaletteIndexOffset,
    WidthLimitedToken1Shadow,
};

enum class IndexedSpritePixelOp {
    Token1Preserve,
    TouchCoverage,
    TouchPaletteRampCoverage,
};

struct SpriteDrawOptions {
    OffsetPair offset_pair = OffsetPair::Normal;
    SpritePixelOp pixel_op = SpritePixelOp::Palette;
    u32 blend_mode = 0;
    u32 source_weight_31 = 0;
    u16 or_mask = 0;
    u32 red_delta = 0;
    u32 green_delta = 0;
    u32 blue_delta = 0;
    u8 palette_index_offset = 0;
    u32 row_pixel_limit = 0xffffffffu;
    bool apply_unit_palette_ramp = false;
};

struct SpriteBlendTables {
    std::array<std::array<u16, kFiveBitTableEntries>, kBlendModeCount> red{};
    std::array<std::array<u16, kGreenTableEntries>, kBlendModeCount> green{};
    std::array<std::array<u16, kFiveBitTableEntries>, kBlendModeCount> blue{};
};

SpriteRenderState g_sprite_render_state;
SpriteBlendTables g_blend_tables;
SpritePixelMaskConstants g_pixel_mask_constants;

u16 read_le_u16(const u8* p) {
    return static_cast<u16>(p[0]) | static_cast<u16>(p[1] << 8);
}

i32 signed_metadata(u32 value) {
    return static_cast<i32>(value);
}

bool render_target_valid(const SpriteRenderTarget& target) {
    return target.pixels != nullptr && target.width != 0 && target.height != 0 &&
        target.stride_words != 0;
}

bool indexed_target_valid(const IndexedSpriteRenderTarget& target) {
    return target.pixels != nullptr && target.width != 0 && target.height != 0 &&
        target.stride_bytes != 0;
}

bool surface_pixel_mode_555() {
    return SurfacePixelMode555();
}

u32 red_shift_for_mode(bool pixel_mode_555) {
    return pixel_mode_555 ? 10u : 11u;
}

u32 green_range_for_mode(bool pixel_mode_555) {
    return pixel_mode_555 ? 0x20u : 0x40u;
}

u16 surface_green_mask() {
    return surface_pixel_mode_555() ? 0x03e0u : 0x07e0u;
}

u32 green_shift_for_grayscale(bool pixel_mode_555) {
    return pixel_mode_555 ? 5u : 6u;
}

u32 screen_channel(u32 dst, u32 src, u32 range) {
    const u32 denom = range - src;
    const u32 value = denom == 0 ? range - 1 : (dst * range) / denom;
    return std::min(value, range - 1);
}

u32 soft_max_channel(u32 dst, u32 src, u32 range) {
    const u32 high = std::max(dst, src);
    const u32 low = std::min(dst, src);
    return high + ((range - high) / range) * low;
}

u32 blend_channel_value(u32 dst, u32 src, u32 range, u32 mode) {
    switch (mode) {
    case 1:
        return screen_channel(dst, src, range);
    case 2:
        return std::max(dst, src);
    case 3:
        return soft_max_channel(dst, src, range);
    case 4: {
        const u32 screened = screen_channel(dst, src, range);
        return soft_max_channel(src, screened, range);
    }
    case 5:
        return screen_channel(soft_max_channel(dst, src, range), src, range);
    case 6:
        return (dst * (range - src)) / range;
    case 7:
        return std::max(dst, src) - std::min(dst, src);
    default:
        return src;
    }
}

std::size_t five_bit_index(u32 dst, u32 src) {
    return static_cast<std::size_t>(dst * kFiveBitTableSide + src);
}

std::size_t green_index(u32 dst, u32 src) {
    return static_cast<std::size_t>(dst * kGreenTableSide + src);
}

void ensure_blend_tables() {
    const bool pixel_mode_555 = surface_pixel_mode_555();
    if (ShouldRebuildSpriteBlendTables(
            g_sprite_render_state.blend_tables_built,
            g_sprite_render_state.blend_tables_pixel_mode_555,
            pixel_mode_555)) {
        BuildSpriteBlendTables(pixel_mode_555);
    }
}

u16 blend_pixels_with_table(u16 dst, u16 src, u32 mode) {
    ensure_blend_tables();
    if (mode == 0 || mode >= kBlendModeCount) {
        return src;
    }

    const bool pixel_mode_555 = g_sprite_render_state.blend_tables_pixel_mode_555;
    const u32 red_shift = red_shift_for_mode(pixel_mode_555);
    const u32 green_mask = pixel_mode_555 ? 0x03e0u : 0x07e0u;
    const u32 green_limit = green_range_for_mode(pixel_mode_555) - 1;

    const u32 dst_red = (dst >> red_shift) & 0x1fu;
    const u32 src_red = (src >> red_shift) & 0x1fu;
    const u32 dst_green = (dst >> 5) & green_limit;
    const u32 src_green = (src >> 5) & green_limit;
    const u32 dst_blue = dst & 0x1fu;
    const u32 src_blue = src & 0x1fu;

    return static_cast<u16>(
        g_blend_tables.red[mode][five_bit_index(dst_red, src_red)] |
        (g_blend_tables.green[mode][green_index(dst_green, src_green)] & green_mask) |
        g_blend_tables.blue[mode][five_bit_index(dst_blue, src_blue)]);
}

u16 blend_pixels_by_factor(u16 dst, u16 src, u32 source_weight_31) {
    // FUN_004d31d8 stores the caller's factor verbatim in DAT_007589e0 and
    // computes 0x1f-factor with 32-bit wraparound.  Do not clamp here: raw HP
    // ratios can temporarily exceed 31 when production effects raise current
    // HP above the unmodified +0x10 maximum, and the original preserves that
    // packed-arithmetic quirk.
    const u32 src_weight = source_weight_31;
    const u32 dst_weight = 0x1fu - src_weight;
    const u16 red_blue_mask = surface_pixel_mode_555() ? 0x7c1fu : 0xf81fu;
    const u16 green_mask = surface_green_mask();

    const u16 dst_part = static_cast<u16>(
        (((dst & red_blue_mask) * dst_weight) >> 5) & red_blue_mask |
        (((dst & green_mask) * dst_weight) >> 5) & green_mask);
    const u16 src_part = static_cast<u16>(
        (((src & red_blue_mask) * src_weight) >> 5) & red_blue_mask |
        (((src & green_mask) * src_weight) >> 5) & green_mask);
    return static_cast<u16>(dst_part + src_part);
}

u16 grayscale_sprite_pixel(u16 src) {
    const bool pixel_mode_555 = surface_pixel_mode_555();
    const u32 red_shift = red_shift_for_mode(pixel_mode_555);
    const u32 green_shift = green_shift_for_grayscale(pixel_mode_555);
    const u32 red = (src >> red_shift) & 0x1fu;
    const u32 green = (src & surface_green_mask()) >> green_shift;
    const u32 blue = src & 0x1fu;
    const u32 gray = (red + green + blue) >> 2;

    if (pixel_mode_555) {
        return static_cast<u16>((gray << 10) | (gray << 5) | gray);
    }
    return static_cast<u16>((gray << 11) | (gray << 6) | gray);
}

u16 add_sprite_channel_tint(u16 src, u32 red_delta, u32 green_delta, u32 blue_delta) {
    const u16 red_mask = SurfaceRedMask();
    const u16 green_mask = surface_green_mask();
    constexpr u16 blue_mask = 0x001f;

    // FUN_004d485e/FUN_004d4a24 add the raw DWORD ramp values to each
    // already-masked palette channel and clamp the sum to that channel's
    // mask.  The ramp deliberately reaches one step beyond the representable
    // range at index 15 (for example blue delta 32 and RGB565 red delta
    // 0x10000).  Masking or narrowing the delta before the addition wraps
    // those values to zero instead of producing the original saturated color.
    const u16 red = static_cast<u16>(
        std::min<u32>(red_mask, (src & red_mask) + red_delta));
    const u16 green = static_cast<u16>(
        std::min<u32>(green_mask, (src & green_mask) + green_delta));
    const u16 blue = static_cast<u16>(
        std::min<u32>(blue_mask, (src & blue_mask) + blue_delta));
    return static_cast<u16>(red | green | blue);
}

bool validate_common_entry(u32 entry_index, const ResourceStoreEntry** entry_out) {
    const ResourceStoreEntry* entry = GetResourceEntry(entry_index);
    if (entry_out != nullptr) {
        *entry_out = entry;
    }
    return entry != nullptr && !entry->payload.empty() &&
        IsPaletteCacheSlotActive(entry->palette_slot);
}

bool draw_resource_sprite_rle(u32 entry_index, i32 x, i32 y,
    const SpriteDrawOptions& options) {
    g_sprite_render_state.last_entry_index = entry_index;
    g_sprite_render_state.last_mode = options.blend_mode;

    const auto& target = g_sprite_render_state.target;
    if (!render_target_valid(target)) {
        return false;
    }

    const ResourceStoreEntry* entry = nullptr;
    if (!validate_common_entry(entry_index, &entry)) {
        return false;
    }

    if (options.pixel_op == SpritePixelOp::Blend &&
        (options.blend_mode == 0 || options.blend_mode >= kBlendModeCount)) {
        return false;
    }
    if (options.apply_unit_palette_ramp) {
        ApplyPaletteCacheUnitRamp(entry->palette_slot,
            g_sprite_render_state.active_unit_palette_ramp);
    }

    const u32 sprite_width = entry->metadata[0];
    const u32 sprite_height = entry->metadata[1];
    if (sprite_width == 0 || sprite_height == 0) {
        return true;
    }

    const bool flipped = options.offset_pair == OffsetPair::Flipped;
    const i32 offset_x = signed_metadata(entry->metadata[flipped ? 4 : 2]);
    const i32 offset_y = signed_metadata(entry->metadata[flipped ? 5 : 3]);
    const i32 direction = flipped ? -1 : 1;
    const i32 draw_x = x + offset_x + (flipped ? static_cast<i32>(sprite_width) : 0);
    const i32 draw_y = y + offset_y;
    const i64 left = direction > 0 ? draw_x : static_cast<i64>(draw_x) - sprite_width + 1;
    const i64 right = direction > 0 ? static_cast<i64>(draw_x) + sprite_width : draw_x + 1;
    const i64 bottom = static_cast<i64>(draw_y) + sprite_height;
    if (left >= static_cast<i64>(target.width) || draw_y >= static_cast<i32>(target.height) ||
        right <= 0 || bottom <= 0) {
        return true;
    }

    const auto& palette = palette_cache_state().pixel_slots[entry->palette_slot];
    const u16 transparent_mask = palette_cache_state().transparent_mask;
    const auto& payload = entry->payload;
    std::size_t cursor = 0;

    for (u32 row = 0; row < sprite_height; ++row) {
        if (cursor + 2 > payload.size()) {
            return false;
        }

        const u16 encoded_row_bytes = read_le_u16(payload.data() + cursor);
        cursor += 2;
        const std::size_t row_start = cursor;
        const std::size_t row_end = row_start + encoded_row_bytes;
        if (row_end < row_start || row_end > payload.size()) {
            return false;
        }

        const i32 target_y = draw_y + static_cast<i32>(row);
        if (target_y >= 0 && target_y < static_cast<i32>(target.height)) {
            i32 target_x = draw_x;
            u32 remaining = sprite_width;
            u32 row_pixel = 0;
            std::size_t p = row_start;

            while (remaining != 0 && p < row_end) {
                const u8 token = payload[p++];
                if (token == 0) {
                    if (p >= row_end) {
                        break;
                    }
                    const u8 skip = payload[p++];
                    target_x += static_cast<i32>(skip) * direction;
                    row_pixel += skip;
                    remaining = skip >= remaining ? 0 : remaining - skip;
                    continue;
                }

                if (target_x >= 0 && target_x < static_cast<i32>(target.width)) {
                    u16& pixel = target.pixels[
                        static_cast<std::size_t>(target_y) * target.stride_words +
                        static_cast<std::size_t>(target_x)];
                    switch (options.pixel_op) {
                    case SpritePixelOp::Palette:
                        pixel = palette[token];
                        break;
                    case SpritePixelOp::PaletteToken2Plus:
                        if (token > 1) {
                            pixel = palette[token];
                        }
                        break;
                    case SpritePixelOp::Token1Shadow:
                        if (token == 1) {
                            pixel = static_cast<u16>((pixel & transparent_mask) >> 1);
                        }
                        else {
                            pixel = palette[token];
                        }
                        break;
                    case SpritePixelOp::NeighborCopy: {
                        const i32 source_x = target_x + direction;
                        const bool can_copy = direction > 0 ?
                            target.width > 2 &&
                                target_x < static_cast<i32>(target.width) - 2 :
                            target_x >= 2;
                        if (token > 1 && can_copy && source_x >= 0 &&
                            source_x < static_cast<i32>(target.width)) {
                            pixel = target.pixels[
                                static_cast<std::size_t>(target_y) * target.stride_words +
                                static_cast<std::size_t>(source_x)];
                        }
                        break;
                    }
                    case SpritePixelOp::Blend:
                        pixel = blend_pixels_with_table(pixel, palette[token], options.blend_mode);
                        break;
                    case SpritePixelOp::BlendFactorToken2Plus:
                        if (token > 1) {
                            pixel = blend_pixels_by_factor(
                                pixel, palette[token], options.source_weight_31);
                        }
                        break;
                    case SpritePixelOp::OrMaskToken1Shadow:
                        if (token == 1) {
                            pixel = static_cast<u16>((pixel & transparent_mask) >> 1);
                        }
                        else {
                            pixel = static_cast<u16>(palette[token] | options.or_mask);
                        }
                        break;
                    case SpritePixelOp::GrayscaleToken1Shadow:
                        if (token == 1) {
                            pixel = static_cast<u16>((pixel & transparent_mask) >> 1);
                        }
                        else {
                            pixel = grayscale_sprite_pixel(palette[token]);
                        }
                        break;
                    case SpritePixelOp::ChannelAddToken1Shadow:
                        if (token == 1) {
                            pixel = static_cast<u16>((pixel & transparent_mask) >> 1);
                        }
                        else {
                            pixel = add_sprite_channel_tint(
                                palette[token], options.red_delta, options.green_delta,
                                options.blue_delta);
                        }
                        break;
                    case SpritePixelOp::PaletteIndexOffset:
                        pixel = palette[static_cast<u8>(token + options.palette_index_offset)];
                        break;
                    case SpritePixelOp::WidthLimitedToken1Shadow:
                        if (token == 1) {
                            pixel = static_cast<u16>((pixel & transparent_mask) >> 1);
                        }
                        else if (row_pixel < options.row_pixel_limit) {
                            pixel = palette[token];
                        }
                        break;
                    }
                }
                target_x += direction;
                ++row_pixel;
                --remaining;
            }
        }

        cursor = row_end;
    }

    return true;
}

bool draw_resource_sprite_rle(u32 entry_index, i32 x, i32 y, OffsetPair offset_pair,
    SpritePixelOp pixel_op, u32 blend_mode = 0) {
    SpriteDrawOptions options{};
    options.offset_pair = offset_pair;
    options.pixel_op = pixel_op;
    options.blend_mode = blend_mode;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool draw_indexed_resource_sprite_rle(u32 entry_index, i32 x, i32 y,
    IndexedSpritePixelOp pixel_op) {
    g_sprite_render_state.last_entry_index = entry_index;
    g_sprite_render_state.last_mode = 0;

    const auto& target = g_sprite_render_state.indexed_target;
    if (!indexed_target_valid(target)) {
        return false;
    }

    const ResourceStoreEntry* entry = GetResourceEntry(entry_index);
    if (entry == nullptr || entry->payload.empty()) {
        return false;
    }

    const u32 sprite_width = entry->metadata[0];
    const u32 sprite_height = entry->metadata[1];
    if (sprite_width == 0 || sprite_height == 0) {
        return true;
    }

    const i32 draw_x = x + signed_metadata(entry->metadata[2]);
    const i32 draw_y = y + signed_metadata(entry->metadata[3]);
    const i64 right = static_cast<i64>(draw_x) + sprite_width;
    const i64 bottom = static_cast<i64>(draw_y) + sprite_height;
    if (draw_x >= static_cast<i32>(target.width) || draw_y >= static_cast<i32>(target.height) ||
        right <= 0 || bottom <= 0) {
        return true;
    }

    const auto& payload = entry->payload;
    std::size_t cursor = 0;
    bool touched = false;

    for (u32 row = 0; row < sprite_height; ++row) {
        if (cursor + 2 > payload.size()) {
            return false;
        }

        const u16 encoded_row_bytes = read_le_u16(payload.data() + cursor);
        cursor += 2;
        const std::size_t row_start = cursor;
        const std::size_t row_end = row_start + encoded_row_bytes;
        if (row_end < row_start || row_end > payload.size()) {
            return false;
        }

        const i32 target_y = draw_y + static_cast<i32>(row);
        if (target_y >= 0 && target_y < static_cast<i32>(target.height)) {
            i32 target_x = draw_x;
            u32 remaining = sprite_width;
            std::size_t p = row_start;

            while (remaining != 0 && p < row_end) {
                const u8 token = payload[p++];
                if (token == 0) {
                    if (p >= row_end) {
                        break;
                    }
                    const u8 skip = payload[p++];
                    target_x += skip;
                    remaining = skip >= remaining ? 0 : remaining - skip;
                    continue;
                }

                if (target_x >= 0 && target_x < static_cast<i32>(target.width)) {
                    u8& pixel = target.pixels[
                        static_cast<std::size_t>(target_y) * target.stride_bytes +
                        static_cast<std::size_t>(target_x)];
                    switch (pixel_op) {
                    case IndexedSpritePixelOp::Token1Preserve:
                        if (token != 1) {
                            pixel = token;
                        }
                        break;
                    case IndexedSpritePixelOp::TouchCoverage:
                    case IndexedSpritePixelOp::TouchPaletteRampCoverage:
                        touched = true;
                        break;
                    }
                }
                ++target_x;
                --remaining;
            }
        }

        cursor = row_end;
    }

    return pixel_op == IndexedSpritePixelOp::Token1Preserve || touched;
}

bool draw_image_resource_normal(u32 entry_index, i32 x, i32 y, bool clip_pixels) {
    g_sprite_render_state.last_entry_index = entry_index;
    g_sprite_render_state.last_mode = 0;

    const auto& target = g_sprite_render_state.target;
    if (!render_target_valid(target)) {
        return false;
    }

    const ResourceStoreEntry* entry = nullptr;
    if (!validate_common_entry(entry_index, &entry)) {
        return false;
    }

    const u32 image_width = entry->metadata[0];
    const u32 image_height = entry->metadata[1];
    if (image_width == 0 || image_height == 0) {
        return true;
    }
    if (entry->payload.size() < static_cast<std::size_t>(image_width) * image_height) {
        return false;
    }

    const i32 draw_x = x + signed_metadata(entry->metadata[2]);
    const i32 draw_y = y + signed_metadata(entry->metadata[3]);
    const auto& palette = palette_cache_state().pixel_slots[entry->palette_slot];

    if (!clip_pixels) {
        const i64 right = static_cast<i64>(draw_x) + image_width;
        const i64 bottom = static_cast<i64>(draw_y) + image_height;
        if (draw_x < 0 || draw_y < 0 || right > static_cast<i64>(target.width) ||
            bottom > static_cast<i64>(target.height)) {
            return false;
        }

        std::size_t source = 0;
        for (u32 row = 0; row < image_height; ++row) {
            std::size_t dest =
                static_cast<std::size_t>(draw_y + static_cast<i32>(row)) *
                    target.stride_words +
                static_cast<std::size_t>(draw_x);
            for (u32 col = 0; col < image_width; ++col) {
                const u8 index = entry->payload[source++];
                if (index != 0) {
                    target.pixels[dest] = palette[index];
                }
                ++dest;
            }
        }
        return true;
    }

    std::size_t source = 0;
    for (u32 row = 0; row < image_height; ++row) {
        const i32 target_y = draw_y + static_cast<i32>(row);
        for (u32 col = 0; col < image_width; ++col) {
            const u8 index = entry->payload[source++];
            if (index == 0) {
                continue;
            }

            const i32 target_x = draw_x + static_cast<i32>(col);
            if (target_x >= 0 && target_x < static_cast<i32>(target.width) &&
                target_y >= 0 && target_y < static_cast<i32>(target.height)) {
                target.pixels[static_cast<std::size_t>(target_y) * target.stride_words +
                    static_cast<std::size_t>(target_x)] = palette[index];
            }
        }
    }

    return true;
}

} // namespace

void SetSpriteRenderTarget(u16* pixels, u32 width, u32 height, u32 stride_words) {
    g_sprite_render_state.target.pixels = pixels;
    g_sprite_render_state.target.width = width;
    g_sprite_render_state.target.height = height;
    g_sprite_render_state.target.stride_words = stride_words != 0 ? stride_words : width;
    g_sprite_render_state.active = render_target_valid(g_sprite_render_state.target);
}

void SetIndexedSpriteRenderTarget(u8* pixels, u32 width, u32 height, u32 stride_bytes) {
    g_sprite_render_state.indexed_target.pixels = pixels;
    g_sprite_render_state.indexed_target.width = width;
    g_sprite_render_state.indexed_target.height = height;
    g_sprite_render_state.indexed_target.stride_bytes = stride_bytes != 0 ? stride_bytes : width;
    g_sprite_render_state.indexed_active =
        indexed_target_valid(g_sprite_render_state.indexed_target);
}

void ClearSpriteRenderTarget() {
    g_sprite_render_state.target = {};
    g_sprite_render_state.active = false;
}

void ClearIndexedSpriteRenderTarget() {
    g_sprite_render_state.indexed_target = {};
    g_sprite_render_state.indexed_active = false;
}

void BuildSpriteBlendTables(bool pixel_mode_555) {
    // FUN_004f4060 builds these division-heavy channel tables when the
    // DirectDraw pixel format is configured.  BindGameplayRenderTarget runs
    // every gameplay frame, but rebinding the same 555/565 surface must not
    // rebuild all 43k entries again: that reconstruction-only work delayed
    // input, edge scrolling, and the next lockstep round by roughly 50 ms.
    if (!ShouldRebuildSpriteBlendTables(
            g_sprite_render_state.blend_tables_built,
            g_sprite_render_state.blend_tables_pixel_mode_555,
            pixel_mode_555)) {
        return;
    }

    const u32 red_shift = red_shift_for_mode(pixel_mode_555);
    for (auto& table : g_blend_tables.red) {
        table.fill(0);
    }
    for (auto& table : g_blend_tables.green) {
        table.fill(0);
    }
    for (auto& table : g_blend_tables.blue) {
        table.fill(0);
    }

    for (u32 dst = 0; dst < kFiveBitTableSide; ++dst) {
        for (u32 src = 0; src < kFiveBitTableSide; ++src) {
            const std::size_t index = five_bit_index(dst, src);
            for (u32 mode = 1; mode < kBlendModeCount; ++mode) {
                const u32 value = blend_channel_value(dst, src, kFiveBitTableSide, mode);
                g_blend_tables.red[mode][index] = static_cast<u16>(value << red_shift);
                g_blend_tables.blue[mode][index] = static_cast<u16>(value);
            }
        }
    }

    const u32 green_range = green_range_for_mode(pixel_mode_555);
    for (u32 dst = 0; dst < green_range; ++dst) {
        for (u32 src = 0; src < green_range; ++src) {
            const std::size_t index = green_index(dst, src);
            for (u32 mode = 1; mode < kBlendModeCount; ++mode) {
                const u32 value = blend_channel_value(dst, src, green_range, mode);
                g_blend_tables.green[mode][index] = static_cast<u16>(value << 5);
            }
        }
    }

    g_sprite_render_state.blend_tables_built = true;
    g_sprite_render_state.blend_tables_pixel_mode_555 = pixel_mode_555;
}

void ConfigureSpritePixelMaskConstants(bool pixel_mode_555) {
    g_pixel_mask_constants.pixel_mode_555 = pixel_mode_555;
    if (pixel_mode_555) {
        g_pixel_mask_constants.high_red = 0x6000;
        g_pixel_mask_constants.high_green = 0x0300;
        g_pixel_mask_constants.high_blue = 0x0018;
        g_pixel_mask_constants.high_red_green = 0x6300;
    }
    else {
        g_pixel_mask_constants.high_red = 0xc000;
        g_pixel_mask_constants.high_green = 0x0600;
        g_pixel_mask_constants.high_blue = 0x0018;
        g_pixel_mask_constants.high_red_green = 0x6600;
    }
    g_pixel_mask_constants.low_blue_a = 0x0018;
    g_pixel_mask_constants.low_blue_b = 0x0018;
}

void SetSpriteUnitPaletteRamp(u8 ramp) {
    g_sprite_render_state.active_unit_palette_ramp = static_cast<u8>(ramp & 0x0f);
}

u8 SpriteUnitPaletteRamp() {
    return g_sprite_render_state.active_unit_palette_ramp;
}

bool DrawResourceSpriteNormal(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Normal, SpritePixelOp::Palette);
}

bool DrawResourceSpriteFlipped(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Flipped, SpritePixelOp::Palette);
}

bool DrawResourceSpriteToken2Plus(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Normal, SpritePixelOp::PaletteToken2Plus);
}

bool DrawResourceSpriteFlippedToken2Plus(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Flipped, SpritePixelOp::PaletteToken2Plus);
}

bool DrawResourceSpriteToken1Shadow(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Normal, SpritePixelOp::Token1Shadow);
}

bool DrawResourceSpriteNeighborCopy(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Normal, SpritePixelOp::NeighborCopy);
}

bool DrawResourceSpriteFlippedNeighborCopy(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Flipped, SpritePixelOp::NeighborCopy);
}

bool DrawResourceSpriteBlend(u32 entry_index, i32 x, i32 y, u32 blend_mode) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Normal, SpritePixelOp::Blend, blend_mode);
}

bool DrawResourceSpriteMode(u32 entry_index, i32 x, i32 y, u32 mode) {
    switch (mode) {
    case 0:
        return DrawResourceSpriteNormal(entry_index, x, y);
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return DrawResourceSpriteBlend(entry_index, x, y, mode);
    case 8:
        return DrawResourceSpriteNeighborCopy(entry_index, x, y);
    case 9:
        return DrawResourceSpriteToken1Shadow(entry_index, x, y);
    default:
        g_sprite_render_state.last_entry_index = entry_index;
        g_sprite_render_state.last_mode = mode;
        return false;
    }
}

bool BlitResourceSpriteNormal(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteNormal(entry_index, x, y);
}

bool BlitResourceSpriteFlipped(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteFlipped(entry_index, x, y);
}

bool BlitResourceSpriteToken1Shadow(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteToken1Shadow(entry_index, x, y);
}

bool BlitResourceSpriteToken2Plus(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteToken2Plus(entry_index, x, y);
}

bool BlitResourceSpriteFlippedToken2Plus(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteFlippedToken2Plus(entry_index, x, y);
}

bool BlitResourceSpriteNeighborCopy(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteNeighborCopy(entry_index, x, y);
}

bool BlitResourceSpriteFlippedNeighborCopy(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteFlippedNeighborCopy(entry_index, x, y);
}

bool DispatchResourceSpriteBlitMode(u32 entry_index, i32 x, i32 y, u32 mode) {
    return DrawResourceSpriteMode(entry_index, x, y, mode);
}

bool BlitResourceSpriteBlendMode1(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteBlend(entry_index, x, y, 1);
}

bool BlitResourceSpriteBlendMode2(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteBlend(entry_index, x, y, 2);
}

bool BlitResourceSpriteBlendMode3(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteBlend(entry_index, x, y, 3);
}

bool BlitResourceSpriteBlendMode4(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteBlend(entry_index, x, y, 4);
}

bool BlitResourceSpriteBlendMode5(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteBlend(entry_index, x, y, 5);
}

bool BlitResourceSpriteBlendMode6(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteBlend(entry_index, x, y, 6);
}

bool BlitResourceSpriteBlendMode7(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteBlend(entry_index, x, y, 7);
}

bool DrawIndexedResourceSpriteToken1Preserve(u32 entry_index, i32 x, i32 y) {
    return draw_indexed_resource_sprite_rle(
        entry_index, x, y, IndexedSpritePixelOp::Token1Preserve);
}

bool TouchIndexedResourceSpriteCoverage(u32 entry_index, i32 x, i32 y) {
    return draw_indexed_resource_sprite_rle(
        entry_index, x, y, IndexedSpritePixelOp::TouchCoverage);
}

bool TouchIndexedResourceSpritePaletteRampCoverage(u32 entry_index, i32 x, i32 y) {
    return draw_indexed_resource_sprite_rle(
        entry_index, x, y, IndexedSpritePixelOp::TouchPaletteRampCoverage);
}

bool DrawResourceSpriteUnitRampToken1Shadow(u32 entry_index, i32 x, i32 y) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::Token1Shadow;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteFlippedUnitRampToken1Shadow(u32 entry_index, i32 x, i32 y) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Flipped;
    options.pixel_op = SpritePixelOp::Token1Shadow;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteBlendFactor(
    u32 entry_index, i32 x, i32 y, u32 source_weight_31) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::BlendFactorToken2Plus;
    options.source_weight_31 = source_weight_31;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteFlippedBlendFactor(
    u32 entry_index, i32 x, i32 y, u32 source_weight_31) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Flipped;
    options.pixel_op = SpritePixelOp::BlendFactorToken2Plus;
    options.source_weight_31 = source_weight_31;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteDirectBlendFactor(
    u32 entry_index, i32 x, i32 y, u32 source_weight_31) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::BlendFactorToken2Plus;
    options.source_weight_31 = source_weight_31;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteDirectToken1Shadow(u32 entry_index, i32 x, i32 y) {
    return draw_resource_sprite_rle(
        entry_index, x, y, OffsetPair::Normal, SpritePixelOp::Token1Shadow);
}

bool DrawResourceSpriteWidthLimitedToken1Shadow(
    u32 entry_index, i32 x, i32 y, u32 row_pixel_limit) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::WidthLimitedToken1Shadow;
    options.row_pixel_limit = row_pixel_limit;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpritePaletteIndexOffset(
    u32 entry_index, i32 x, i32 y, u8 index_offset) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::PaletteIndexOffset;
    options.palette_index_offset = index_offset;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteToken1ShadowOrMask(u32 entry_index, i32 x, i32 y, u16 mask) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::OrMaskToken1Shadow;
    options.or_mask = mask;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteFlippedToken1ShadowOrMask(
    u32 entry_index, i32 x, i32 y, u16 mask) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Flipped;
    options.pixel_op = SpritePixelOp::OrMaskToken1Shadow;
    options.or_mask = mask;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteHighRedMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.high_red);
}

bool DrawResourceSpriteFlippedHighRedMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteFlippedToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.high_red);
}

bool DrawResourceSpriteHighGreenMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.high_green);
}

bool DrawResourceSpriteFlippedHighGreenMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteFlippedToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.high_green);
}

bool DrawResourceSpriteHighBlueMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.high_blue);
}

bool DrawResourceSpriteFlippedHighBlueMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteFlippedToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.high_blue);
}

bool DrawResourceSpriteUnitRampLowBlueMask(u32 entry_index, i32 x, i32 y) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::OrMaskToken1Shadow;
    options.or_mask = g_pixel_mask_constants.low_blue_a;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteFlippedUnitRampLowBlueMask(u32 entry_index, i32 x, i32 y) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Flipped;
    options.pixel_op = SpritePixelOp::OrMaskToken1Shadow;
    options.or_mask = g_pixel_mask_constants.low_blue_a;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteGrayscale(u32 entry_index, i32 x, i32 y) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::GrayscaleToken1Shadow;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteFlippedGrayscale(u32 entry_index, i32 x, i32 y) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Flipped;
    options.pixel_op = SpritePixelOp::GrayscaleToken1Shadow;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteLowBlueMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.low_blue_b);
}

bool DrawResourceSpriteFlippedLowBlueMask(u32 entry_index, i32 x, i32 y) {
    return DrawResourceSpriteFlippedToken1ShadowOrMask(
        entry_index, x, y, g_pixel_mask_constants.low_blue_b);
}

bool DrawResourceSpriteChannelAdditiveTint(
    u32 entry_index, i32 x, i32 y, u32 red_delta, u32 green_delta, u32 blue_delta) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::ChannelAddToken1Shadow;
    options.red_delta = red_delta;
    options.green_delta = green_delta;
    options.blue_delta = blue_delta;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteFlippedChannelAdditiveTint(
    u32 entry_index, i32 x, i32 y, u32 red_delta, u32 green_delta, u32 blue_delta) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Flipped;
    options.pixel_op = SpritePixelOp::ChannelAddToken1Shadow;
    options.red_delta = red_delta;
    options.green_delta = green_delta;
    options.blue_delta = blue_delta;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawResourceSpriteTableBlend(u32 entry_index, i32 x, i32 y) {
    SpriteDrawOptions options{};
    options.offset_pair = OffsetPair::Normal;
    options.pixel_op = SpritePixelOp::Blend;
    options.blend_mode = 4;
    options.apply_unit_palette_ramp = true;
    return draw_resource_sprite_rle(entry_index, x, y, options);
}

bool DrawImageResourceNormal(u32 entry_index, i32 x, i32 y) {
    return draw_image_resource_normal(entry_index, x, y, true);
}

bool BlitImageResourceNormal(u32 entry_index, i32 x, i32 y) {
    return draw_image_resource_normal(entry_index, x, y, false);
}

const SpriteRenderState& sprite_render_state() {
    return g_sprite_render_state;
}

const SpritePixelMaskConstants& sprite_pixel_mask_constants() {
    return g_pixel_mask_constants;
}

}
