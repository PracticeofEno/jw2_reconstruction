#include "ranker_sprite_renderer.h"

#include "ranker_palette_cache.h"
#include "ranker_resource_store.h"
#include "ranker_visual_animation.h"
#include "ranker_visual_animation_archive.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

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
    u16 red_delta = 0;
    u16 green_delta = 0;
    u16 blue_delta = 0;
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

u16 add_sprite_channel_tint(u16 src, u16 red_delta, u16 green_delta, u16 blue_delta) {
    const u16 red_mask = SurfaceRedMask();
    const u16 green_mask = surface_green_mask();
    constexpr u16 blue_mask = 0x001f;

    const u16 red = static_cast<u16>(
        std::min<u32>(red_mask, (src & red_mask) + (red_delta & red_mask)));
    const u16 green = static_cast<u16>(
        std::min<u32>(green_mask, (src & green_mask) + (green_delta & green_mask)));
    const u16 blue = static_cast<u16>(
        std::min<u32>(blue_mask, (src & blue_mask) + (blue_delta & blue_mask)));
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

#if defined(RANKER_ENABLE_LEGACY_RUNTIME_PIXEL_MORPH_GENERATOR)
// Kept only as a source-level reference for reproducing old previews. The
// production build deliberately never compiles or calls this expensive pixel
// correspondence generator; gameplay uses Jw2_09_144hz.rfa exclusively.
constexpr std::size_t kPixelMorphCacheEntryLimit = 512;
constexpr std::size_t kPixelMorphCacheCellLimit = 8u * 1024u * 1024u;
constexpr i32 kPixelMorphSameTokenRadius = 6;
constexpr i32 kPixelMorphSameClassRadius = 2;

struct PixelMorphDot {
    i32 x = 0;
    i32 y = 0;
    u8 token = 0;
};

struct PixelMorphPair {
    i32 source_x = 0;
    i32 source_y = 0;
    i32 target_x = 0;
    i32 target_y = 0;
    u8 source_token = 0;
    u8 target_token = 0;
};

struct PixelMorphFrame {
    std::vector<u16> dots;
};

struct PixelMorphTransition {
    i32 left = 0;
    i32 top = 0;
    u32 width = 0;
    u32 height = 0;
    std::array<PixelMorphFrame, kVisualAnimationIntermediateFrameCount> frames{};
    std::size_t cell_count = 0;
};

struct PixelMorphCacheKey {
    u32 source_entry = 0;
    u32 target_entry = 0;
    u64 source_serial = 0;
    u64 target_serial = 0;
    bool flipped = false;

    bool operator==(const PixelMorphCacheKey& other) const {
        return source_entry == other.source_entry &&
            target_entry == other.target_entry &&
            source_serial == other.source_serial &&
            target_serial == other.target_serial && flipped == other.flipped;
    }
};

struct PixelMorphCacheKeyHash {
    std::size_t operator()(const PixelMorphCacheKey& key) const {
        std::size_t hash = key.source_entry;
        const auto mix = [&hash](u64 value) {
            hash ^= static_cast<std::size_t>(value) +
                static_cast<std::size_t>(0x9e3779b9u) +
                (hash << 6) + (hash >> 2);
        };
        mix(key.target_entry);
        mix(key.source_serial);
        mix(key.target_serial);
        mix(key.flipped ? 1u : 0u);
        return hash;
    }
};

struct PixelMorphCacheValue {
    PixelMorphTransition transition;
    u64 last_used = 0;
};

std::unordered_map<PixelMorphCacheKey, PixelMorphCacheValue,
    PixelMorphCacheKeyHash> g_pixel_morph_cache;
std::size_t g_pixel_morph_cache_cells = 0;
u64 g_pixel_morph_cache_clock = 0;

void evict_pixel_morph_cache_until(std::size_t incoming_cells) {
    while (!g_pixel_morph_cache.empty() &&
        (g_pixel_morph_cache.size() >= kPixelMorphCacheEntryLimit ||
            g_pixel_morph_cache_cells + incoming_cells >
                kPixelMorphCacheCellLimit)) {
        auto oldest = g_pixel_morph_cache.begin();
        for (auto it = std::next(g_pixel_morph_cache.begin());
             it != g_pixel_morph_cache.end(); ++it) {
            if (it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        g_pixel_morph_cache_cells -= oldest->second.transition.cell_count;
        g_pixel_morph_cache.erase(oldest);
    }
}

u64 pixel_morph_coordinate_key(i32 x, i32 y) {
    return (static_cast<u64>(static_cast<u32>(x)) << 32) |
        static_cast<u32>(y);
}

bool decode_pixel_morph_dots(const ResourceStoreEntry& entry, bool flipped,
    std::vector<PixelMorphDot>& dots) {
    const u32 width = entry.metadata[0];
    const u32 height = entry.metadata[1];
    const i32 offset_x = signed_metadata(entry.metadata[flipped ? 4 : 2]);
    const i32 offset_y = signed_metadata(entry.metadata[flipped ? 5 : 3]);
    const auto& payload = entry.payload;
    std::size_t cursor = 0;

    dots.clear();
    for (u32 row = 0; row < height; ++row) {
        if (cursor + 2 > payload.size()) {
            return false;
        }
        const u16 encoded_row_bytes = read_le_u16(payload.data() + cursor);
        cursor += 2;
        const std::size_t row_end = cursor + encoded_row_bytes;
        if (row_end < cursor || row_end > payload.size()) {
            return false;
        }

        u32 column = 0;
        while (column < width && cursor < row_end) {
            const u8 token = payload[cursor++];
            if (token == 0) {
                if (cursor >= row_end) {
                    break;
                }
                const u8 skip = payload[cursor++];
                column = skip >= width - column ? width : column + skip;
                continue;
            }
            const i32 local_x = !flipped
                ? offset_x + static_cast<i32>(column)
                : offset_x + static_cast<i32>(width) - static_cast<i32>(column);
            dots.push_back(PixelMorphDot{
                local_x, offset_y + static_cast<i32>(row), token});
            ++column;
        }
        cursor = row_end;
    }
    return true;
}

bool pixel_morph_same_class(u8 source_token, u8 target_token) {
    return (source_token == 1) == (target_token == 1);
}

i32 pixel_morph_nearest_target(i32 x, i32 y, u8 source_token,
    i32 radius_limit, bool require_same_token,
    const std::vector<PixelMorphDot>& targets,
    const std::unordered_map<u64, std::size_t>& target_at,
    const std::vector<bool>& target_used) {
    for (i32 radius = 1; radius <= radius_limit; ++radius) {
        i32 best_index = -1;
        i32 best_distance = std::numeric_limits<i32>::max();
        for (i32 dy = -radius; dy <= radius; ++dy) {
            for (i32 dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const auto candidate_it = target_at.find(
                    pixel_morph_coordinate_key(x + dx, y + dy));
                if (candidate_it == target_at.end() ||
                    target_used[candidate_it->second]) {
                    continue;
                }
                const PixelMorphDot& target = targets[candidate_it->second];
                if (!pixel_morph_same_class(source_token, target.token) ||
                    (require_same_token && source_token != target.token)) {
                    continue;
                }
                const i32 distance = dx * dx + dy * dy;
                if (distance < best_distance) {
                    best_distance = distance;
                    best_index = static_cast<i32>(candidate_it->second);
                }
            }
        }
        if (best_index >= 0) {
            return best_index;
        }
    }
    return -1;
}

constexpr std::array<u8, 16> kPixelMorphBayer4x4{
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5,
};

u32 pixel_morph_dither_rank(i32 x, i32 y) {
    const u32 column = static_cast<u32>(x) & 3u;
    const u32 row = static_cast<u32>(y) & 3u;
    return kPixelMorphBayer4x4[row * 4u + column];
}

i32 pixel_morph_lerp_coordinate(i32 source, i32 target, u32 step) {
    const i64 numerator = static_cast<i64>(target - source) * step;
    const i64 rounded = numerator >= 0
        ? numerator + kVisualAnimationIntervalCount / 2u
        : numerator - kVisualAnimationIntervalCount / 2u;
    return source + static_cast<i32>(
        rounded / static_cast<i64>(kVisualAnimationIntervalCount));
}

bool build_pixel_morph_transition(const ResourceStoreEntry& source_entry,
    const ResourceStoreEntry& target_entry, bool flipped,
    PixelMorphTransition& transition) {
    std::vector<PixelMorphDot> sources;
    std::vector<PixelMorphDot> targets;
    if (!decode_pixel_morph_dots(source_entry, flipped, sources) ||
        !decode_pixel_morph_dots(target_entry, flipped, targets)) {
        return false;
    }
    if (sources.empty() && targets.empty()) {
        transition = {};
        return true;
    }

    std::unordered_map<u64, std::size_t> target_at;
    target_at.reserve(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        target_at.emplace(pixel_morph_coordinate_key(targets[i].x, targets[i].y), i);
    }
    std::vector<bool> target_used(targets.size(), false);
    std::vector<i32> source_match(sources.size(), -1);

    for (std::size_t i = 0; i < sources.size(); ++i) {
        const PixelMorphDot& source = sources[i];
        const auto candidate_it = target_at.find(
            pixel_morph_coordinate_key(source.x, source.y));
        if (candidate_it == target_at.end() || target_used[candidate_it->second]) {
            continue;
        }
        const PixelMorphDot& target = targets[candidate_it->second];
        if (!pixel_morph_same_class(source.token, target.token)) {
            continue;
        }
        source_match[i] = static_cast<i32>(candidate_it->second);
        target_used[candidate_it->second] = true;
    }

    const auto pair_nearby = [&](i32 radius, bool require_same_token) {
        for (std::size_t i = 0; i < sources.size(); ++i) {
            if (source_match[i] >= 0) {
                continue;
            }
            const PixelMorphDot& source = sources[i];
            const i32 candidate = pixel_morph_nearest_target(
                source.x, source.y, source.token, radius, require_same_token,
                targets, target_at, target_used);
            if (candidate >= 0) {
                source_match[i] = candidate;
                target_used[static_cast<std::size_t>(candidate)] = true;
            }
        }
    };
    pair_nearby(kPixelMorphSameTokenRadius, true);
    pair_nearby(kPixelMorphSameClassRadius, false);

    std::vector<PixelMorphPair> pairs;
    pairs.reserve(sources.size() + targets.size());
    i32 left = std::numeric_limits<i32>::max();
    i32 top = std::numeric_limits<i32>::max();
    i32 right = std::numeric_limits<i32>::min();
    i32 bottom = std::numeric_limits<i32>::min();
    const auto extend_bounds = [&](i32 x, i32 y) {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
    };
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const PixelMorphDot& source = sources[i];
        const i32 target_index = source_match[i];
        if (target_index >= 0) {
            const PixelMorphDot& target = targets[static_cast<std::size_t>(target_index)];
            pairs.push_back(PixelMorphPair{source.x, source.y, target.x,
                target.y, source.token, target.token});
            extend_bounds(target.x, target.y);
        }
        else {
            pairs.push_back(PixelMorphPair{source.x, source.y, source.x,
                source.y, source.token, 0});
        }
        extend_bounds(source.x, source.y);
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (target_used[i]) {
            continue;
        }
        const PixelMorphDot& target = targets[i];
        pairs.push_back(PixelMorphPair{target.x, target.y, target.x,
            target.y, 0, target.token});
        extend_bounds(target.x, target.y);
    }

    const i64 width_64 = static_cast<i64>(right) - left + 1;
    const i64 height_64 = static_cast<i64>(bottom) - top + 1;
    if (width_64 <= 0 || height_64 <= 0 || width_64 > 4096 || height_64 > 4096 ||
        width_64 * height_64 > static_cast<i64>(
            kPixelMorphCacheCellLimit /
            kVisualAnimationIntermediateFrameCount)) {
        return false;
    }
    transition = {};
    transition.left = left;
    transition.top = top;
    transition.width = static_cast<u32>(width_64);
    transition.height = static_cast<u32>(height_64);
    const std::size_t cells_per_frame =
        static_cast<std::size_t>(transition.width) * transition.height;
    transition.cell_count = cells_per_frame * kVisualAnimationIntermediateFrameCount;

    for (u32 step = 1; step < kVisualAnimationIntervalCount; ++step) {
        std::vector<u16>& frame = transition.frames[step - 1].dots;
        frame.assign(cells_per_frame, 0);
        const u32 threshold = (step * 16u + kVisualAnimationIntervalCount / 2u) /
            kVisualAnimationIntervalCount;
        for (const PixelMorphPair& pair : pairs) {
            i32 dot_x = 0;
            i32 dot_y = 0;
            u8 token = 0;
            bool target_palette = false;
            if (pair.source_token != 0 && pair.target_token != 0) {
                dot_x = pixel_morph_lerp_coordinate(pair.source_x, pair.target_x, step);
                dot_y = pixel_morph_lerp_coordinate(pair.source_y, pair.target_y, step);
                target_palette = step * 2u >= kVisualAnimationIntervalCount;
                token = target_palette ? pair.target_token : pair.source_token;
            }
            else if (pair.source_token != 0) {
                dot_x = pair.source_x;
                dot_y = pair.source_y;
                token = pair.source_token;
                if (pixel_morph_dither_rank(dot_x, dot_y) < threshold) {
                    continue;
                }
            }
            else {
                dot_x = pair.target_x;
                dot_y = pair.target_y;
                token = pair.target_token;
                target_palette = true;
                if (pixel_morph_dither_rank(dot_x, dot_y) >= threshold) {
                    continue;
                }
            }

            const std::size_t cell =
                static_cast<std::size_t>(dot_y - transition.top) * transition.width +
                static_cast<std::size_t>(dot_x - transition.left);
            const u8 existing_token = static_cast<u8>(frame[cell] & 0xffu);
            if (existing_token == 0 || existing_token <= 1 || token > 1) {
                frame[cell] = static_cast<u16>(token |
                    (target_palette ? 0x100u : 0u));
            }
        }
    }
    return true;
}

bool draw_cached_pixel_morph_transition(const PixelMorphTransition& transition,
    const ResourceStoreEntry& source_entry, const ResourceStoreEntry& target_entry,
    i32 x, i32 y, u32 subframe_index,
    const SpritePixelMorphDrawOptions& options) {
    if (subframe_index == 0 || subframe_index >= kVisualAnimationIntervalCount) {
        return false;
    }
    const auto& target = g_sprite_render_state.target;
    if (!render_target_valid(target)) {
        return false;
    }
    const bool apply_unit_ramp =
        options.style == SpritePixelMorphStyle::unit_ramp_token1_shadow ||
        options.style == SpritePixelMorphStyle::blend_factor_token2_plus ||
        options.style == SpritePixelMorphStyle::unit_ramp_or_mask_token1_shadow ||
        options.style == SpritePixelMorphStyle::channel_add_token1_shadow;
    if (apply_unit_ramp) {
        ApplyPaletteCacheUnitRamp(source_entry.palette_slot,
            g_sprite_render_state.active_unit_palette_ramp);
        ApplyPaletteCacheUnitRamp(target_entry.palette_slot,
            g_sprite_render_state.active_unit_palette_ramp);
    }
    const auto& source_palette =
        palette_cache_state().pixel_slots[source_entry.palette_slot];
    const auto& target_palette =
        palette_cache_state().pixel_slots[target_entry.palette_slot];
    const u16 transparent_mask = palette_cache_state().transparent_mask;
    const std::vector<u16>& frame = transition.frames[subframe_index - 1].dots;

    for (u32 shadow_pass = 0; shadow_pass < 2; ++shadow_pass) {
        for (u32 row = 0; row < transition.height; ++row) {
            const i32 target_y = y + transition.top + static_cast<i32>(row);
            if (target_y < 0 || target_y >= static_cast<i32>(target.height)) {
                continue;
            }
            for (u32 column = 0; column < transition.width; ++column) {
                const u16 encoded = frame[
                    static_cast<std::size_t>(row) * transition.width + column];
                const u8 token = static_cast<u8>(encoded & 0xffu);
                if (token == 0 || (token == 1) != (shadow_pass == 0)) {
                    continue;
                }
                const i32 target_x = x + transition.left + static_cast<i32>(column);
                if (target_x < 0 || target_x >= static_cast<i32>(target.width)) {
                    continue;
                }
                u16& pixel = target.pixels[
                    static_cast<std::size_t>(target_y) * target.stride_words +
                    static_cast<std::size_t>(target_x)];
                const bool target_palette_selected = (encoded & 0x100u) != 0;
                const u16 palette_pixel = target_palette_selected
                    ? target_palette[token] : source_palette[token];
                switch (options.style) {
                case SpritePixelMorphStyle::unit_ramp_token1_shadow:
                    pixel = token == 1
                        ? static_cast<u16>((pixel & transparent_mask) >> 1)
                        : palette_pixel;
                    break;
                case SpritePixelMorphStyle::palette:
                    pixel = palette_pixel;
                    break;
                case SpritePixelMorphStyle::resource_mode:
                    if (options.mode_or_factor == 0) {
                        pixel = palette_pixel;
                    }
                    else if (options.mode_or_factor <= 7) {
                        pixel = blend_pixels_with_table(
                            pixel, palette_pixel, options.mode_or_factor);
                    }
                    else if (options.mode_or_factor == 8) {
                        if (token > 1) {
                            const i32 neighbor_x = target_x +
                                (options.flipped ? -1 : 1);
                            if (neighbor_x >= 0 &&
                                neighbor_x < static_cast<i32>(target.width)) {
                                pixel = target.pixels[
                                    static_cast<std::size_t>(target_y) *
                                        target.stride_words +
                                    static_cast<std::size_t>(neighbor_x)];
                            }
                        }
                    }
                    else if (options.mode_or_factor == 9) {
                        pixel = token == 1
                            ? static_cast<u16>((pixel & transparent_mask) >> 1)
                            : palette_pixel;
                    }
                    else {
                        return false;
                    }
                    break;
                case SpritePixelMorphStyle::or_mask_token1_shadow:
                case SpritePixelMorphStyle::unit_ramp_or_mask_token1_shadow:
                    pixel = token == 1
                        ? static_cast<u16>((pixel & transparent_mask) >> 1)
                        : static_cast<u16>(palette_pixel | options.mask);
                    break;
                case SpritePixelMorphStyle::grayscale_token1_shadow:
                    pixel = token == 1
                        ? static_cast<u16>((pixel & transparent_mask) >> 1)
                        : grayscale_sprite_pixel(palette_pixel);
                    break;
                case SpritePixelMorphStyle::blend_factor_token2_plus:
                    if (token > 1) {
                        pixel = blend_pixels_by_factor(
                            pixel, palette_pixel, options.mode_or_factor);
                    }
                    break;
                case SpritePixelMorphStyle::neighbor_copy:
                    if (token > 1) {
                        const i32 neighbor_x = target_x +
                            (options.flipped ? -1 : 1);
                        if (neighbor_x >= 0 &&
                            neighbor_x < static_cast<i32>(target.width)) {
                            pixel = target.pixels[
                                static_cast<std::size_t>(target_y) *
                                    target.stride_words +
                                static_cast<std::size_t>(neighbor_x)];
                        }
                    }
                    break;
                case SpritePixelMorphStyle::channel_add_token1_shadow:
                    pixel = token == 1
                        ? static_cast<u16>((pixel & transparent_mask) >> 1)
                        : add_sprite_channel_tint(palette_pixel,
                            options.red_delta, options.green_delta,
                            options.blue_delta);
                    break;
                }
            }
        }
    }
    return true;
}
#endif

bool draw_precomputed_pixel_morph_frame(
    const VisualAnimationArchiveFrameView& frame,
    const ResourceStoreEntry& target_entry, i32 x, i32 y,
    const SpritePixelMorphDrawOptions& options) {
    const auto& target = g_sprite_render_state.target;
    if (!render_target_valid(target) || frame.payload == nullptr ||
        !IsPaletteCacheSlotActive(target_entry.palette_slot)) {
        return false;
    }
    const bool apply_unit_ramp =
        options.style == SpritePixelMorphStyle::unit_ramp_token1_shadow ||
        options.style == SpritePixelMorphStyle::blend_factor_token2_plus ||
        options.style == SpritePixelMorphStyle::unit_ramp_or_mask_token1_shadow ||
        options.style == SpritePixelMorphStyle::channel_add_token1_shadow;
    if (apply_unit_ramp) {
        ApplyPaletteCacheUnitRamp(target_entry.palette_slot,
            g_sprite_render_state.active_unit_palette_ramp);
    }
    const auto& palette =
        palette_cache_state().pixel_slots[target_entry.palette_slot];
    const u16 transparent_mask = palette_cache_state().transparent_mask;

    for (u32 shadow_pass = 0; shadow_pass < 2; ++shadow_pass) {
        std::size_t cursor = 0;
        for (u32 row = 0; row < frame.height; ++row) {
            if (cursor + 2 > frame.payload_size) {
                return false;
            }
            const std::size_t row_size = read_le_u16(frame.payload + cursor);
            cursor += 2;
            if (row_size > frame.payload_size - cursor) {
                return false;
            }
            const std::size_t row_end = cursor + row_size;
            const i32 target_y = y + frame.top + static_cast<i32>(row) +
                (frame.flipped ? frame.flip_delta_y : 0);
            i32 target_x = x + (frame.flipped
                ? frame.flip_origin_x - frame.left : frame.left);
            u32 remaining = frame.width;
            while (cursor < row_end && remaining != 0) {
                const u8 token = frame.payload[cursor++];
                if (token == 0) {
                    if (cursor >= row_end) {
                        return false;
                    }
                    const u8 skip = frame.payload[cursor++];
                    if (skip == 0 || skip > remaining) {
                        return false;
                    }
                    target_x += frame.flipped
                        ? -static_cast<i32>(skip) : static_cast<i32>(skip);
                    remaining -= skip;
                    continue;
                }
                if ((token == 1) == (shadow_pass == 0) && target_x >= 0 &&
                    target_x < static_cast<i32>(target.width) && target_y >= 0 &&
                    target_y < static_cast<i32>(target.height)) {
                    u16& pixel = target.pixels[
                        static_cast<std::size_t>(target_y) * target.stride_words +
                        static_cast<std::size_t>(target_x)];
                    const u16 palette_pixel = palette[token];
                    switch (options.style) {
                    case SpritePixelMorphStyle::unit_ramp_token1_shadow:
                        pixel = token == 1
                            ? static_cast<u16>((pixel & transparent_mask) >> 1)
                            : palette_pixel;
                        break;
                    case SpritePixelMorphStyle::palette:
                        pixel = palette_pixel;
                        break;
                    case SpritePixelMorphStyle::resource_mode:
                        if (options.mode_or_factor == 0) {
                            pixel = palette_pixel;
                        }
                        else if (options.mode_or_factor <= 7) {
                            pixel = blend_pixels_with_table(
                                pixel, palette_pixel, options.mode_or_factor);
                        }
                        else if (options.mode_or_factor == 8) {
                            if (token > 1) {
                                const i32 neighbor_x = target_x +
                                    (options.flipped ? -1 : 1);
                                if (neighbor_x >= 0 &&
                                    neighbor_x < static_cast<i32>(target.width)) {
                                    pixel = target.pixels[
                                        static_cast<std::size_t>(target_y) *
                                            target.stride_words +
                                        static_cast<std::size_t>(neighbor_x)];
                                }
                            }
                        }
                        else if (options.mode_or_factor == 9) {
                            pixel = token == 1
                                ? static_cast<u16>(
                                    (pixel & transparent_mask) >> 1)
                                : palette_pixel;
                        }
                        else {
                            return false;
                        }
                        break;
                    case SpritePixelMorphStyle::or_mask_token1_shadow:
                    case SpritePixelMorphStyle::unit_ramp_or_mask_token1_shadow:
                        pixel = token == 1
                            ? static_cast<u16>((pixel & transparent_mask) >> 1)
                            : static_cast<u16>(palette_pixel | options.mask);
                        break;
                    case SpritePixelMorphStyle::grayscale_token1_shadow:
                        pixel = token == 1
                            ? static_cast<u16>((pixel & transparent_mask) >> 1)
                            : grayscale_sprite_pixel(palette_pixel);
                        break;
                    case SpritePixelMorphStyle::blend_factor_token2_plus:
                        if (token > 1) {
                            pixel = blend_pixels_by_factor(
                                pixel, palette_pixel, options.mode_or_factor);
                        }
                        break;
                    case SpritePixelMorphStyle::neighbor_copy:
                        if (token > 1) {
                            const i32 neighbor_x = target_x +
                                (options.flipped ? -1 : 1);
                            if (neighbor_x >= 0 &&
                                neighbor_x < static_cast<i32>(target.width)) {
                                pixel = target.pixels[
                                    static_cast<std::size_t>(target_y) *
                                        target.stride_words +
                                    static_cast<std::size_t>(neighbor_x)];
                            }
                        }
                        break;
                    case SpritePixelMorphStyle::channel_add_token1_shadow:
                        pixel = token == 1
                            ? static_cast<u16>((pixel & transparent_mask) >> 1)
                            : add_sprite_channel_tint(palette_pixel,
                                options.red_delta, options.green_delta,
                                options.blue_delta);
                        break;
                    }
                }
                target_x += frame.flipped ? -1 : 1;
                --remaining;
            }
            if (cursor != row_end) {
                return false;
            }
        }
        if (cursor != frame.payload_size) {
            return false;
        }
    }
    return true;
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

bool DrawResourceSpritePixelMorphTransition(u32 unit_type, u32 image_group,
    u32 source_frame, u32 target_frame, u32 source_entry_index,
    u32 target_entry_index, i32 x, i32 y, u32 subframe_index,
    const SpritePixelMorphDrawOptions& options) {
    const auto draw_original = [&](u32 entry_index) {
        switch (options.style) {
        case SpritePixelMorphStyle::unit_ramp_token1_shadow:
            return options.flipped
                ? DrawResourceSpriteFlippedUnitRampToken1Shadow(entry_index, x, y)
                : DrawResourceSpriteUnitRampToken1Shadow(entry_index, x, y);
        case SpritePixelMorphStyle::palette:
            return options.flipped
                ? DrawResourceSpriteFlipped(entry_index, x, y)
                : DrawResourceSpriteNormal(entry_index, x, y);
        case SpritePixelMorphStyle::resource_mode:
            return DrawResourceSpriteMode(entry_index, x, y,
                options.mode_or_factor);
        case SpritePixelMorphStyle::or_mask_token1_shadow:
            return options.flipped
                ? DrawResourceSpriteFlippedToken1ShadowOrMask(
                    entry_index, x, y, options.mask)
                : DrawResourceSpriteToken1ShadowOrMask(
                    entry_index, x, y, options.mask);
        case SpritePixelMorphStyle::grayscale_token1_shadow:
            return options.flipped
                ? DrawResourceSpriteFlippedGrayscale(entry_index, x, y)
                : DrawResourceSpriteGrayscale(entry_index, x, y);
        case SpritePixelMorphStyle::blend_factor_token2_plus:
            return options.flipped
                ? DrawResourceSpriteFlippedBlendFactor(
                    entry_index, x, y, options.mode_or_factor)
                : DrawResourceSpriteBlendFactor(
                    entry_index, x, y, options.mode_or_factor);
        case SpritePixelMorphStyle::neighbor_copy:
            return options.flipped
                ? DrawResourceSpriteFlippedNeighborCopy(entry_index, x, y)
                : DrawResourceSpriteNeighborCopy(entry_index, x, y);
        case SpritePixelMorphStyle::unit_ramp_or_mask_token1_shadow:
            return options.flipped
                ? DrawResourceSpriteFlippedUnitRampLowBlueMask(entry_index, x, y)
                : DrawResourceSpriteUnitRampLowBlueMask(entry_index, x, y);
        case SpritePixelMorphStyle::channel_add_token1_shadow:
            return options.flipped
                ? DrawResourceSpriteFlippedChannelAdditiveTint(entry_index, x, y,
                    options.red_delta, options.green_delta, options.blue_delta)
                : DrawResourceSpriteChannelAdditiveTint(entry_index, x, y,
                    options.red_delta, options.green_delta, options.blue_delta);
        }
        return false;
    };

    const ResourceStoreEntry* source_entry = GetResourceEntry(source_entry_index);
    const ResourceStoreEntry* target_entry = GetResourceEntry(target_entry_index);
    if (source_entry == nullptr || target_entry == nullptr ||
        source_entry->payload.empty() || target_entry->payload.empty() ||
        !IsPaletteCacheSlotActive(source_entry->palette_slot) ||
        !IsPaletteCacheSlotActive(target_entry->palette_slot)) {
        return false;
    }
    if (subframe_index == 0 || subframe_index >= kVisualAnimationIntervalCount) {
        const u32 endpoint = subframe_index == 0
            ? source_entry_index : target_entry_index;
        return draw_original(endpoint);
    }

    VisualAnimationArchiveFrameView frame{};
    if (!FindVisualAnimationArchiveFrame(unit_type, image_group,
            source_frame, target_frame, subframe_index, options.flipped, frame)) {
        // The animation archive is presentation-only. If it is absent,
        // mismatched, or lacks this transition, draw the authoritative current
        // TRC sprite instead of synthesizing pixels during gameplay.
        return draw_original(target_entry_index);
    }

    g_sprite_render_state.last_entry_index = target_entry_index;
    g_sprite_render_state.last_mode = options.mode_or_factor;
    if (draw_precomputed_pixel_morph_frame(
            frame, *target_entry, x, y, options)) {
        return true;
    }
    return draw_original(target_entry_index);
}

bool DrawResourceSpriteUnitRampPixelMorphTransition(u32 unit_type,
    u32 image_group, u32 source_frame, u32 target_frame,
    u32 source_entry_index, u32 target_entry_index, i32 x, i32 y,
    u32 subframe_index, bool flipped) {
    SpritePixelMorphDrawOptions options{};
    options.flipped = flipped;
    return DrawResourceSpritePixelMorphTransition(unit_type, image_group,
        source_frame, target_frame, source_entry_index, target_entry_index,
        x, y, subframe_index, options);
}

void ResetResourceSpritePixelMorphCache() {
    ResetVisualAnimationArchiveCache();
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
    u32 entry_index, i32 x, i32 y, u16 red_delta, u16 green_delta, u16 blue_delta) {
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
    u32 entry_index, i32 x, i32 y, u16 red_delta, u16 green_delta, u16 blue_delta) {
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
