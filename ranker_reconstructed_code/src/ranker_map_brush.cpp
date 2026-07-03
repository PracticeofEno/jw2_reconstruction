#include "ranker_map_brush.h"

#include "ranker_miles.h"
#include "ranker_palette_cache.h"
#include "ranker_resource_store.h"
#include "ranker_trc.h"
#include "ranker_unit_movement.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace ranker {
namespace {

constexpr std::size_t kMinimapBrushRecordBytes = 0x40000;

constexpr std::array<std::array<i32, 2>, 8> kNeighborOffsets{{
    {{0, -1}},
    {{1, -1}},
    {{1, 0}},
    {{1, 1}},
    {{0, 1}},
    {{-1, 1}},
    {{-1, 0}},
    {{-1, -1}},
}};

i32 arithmetic_shift_right(i32 value, u32 bits) {
    if (value >= 0) {
        return value >> bits;
    }
    const i32 positive = -value - 1;
    return -((positive >> bits) + 1);
}

i32 original_world_to_tile(i32 value) {
    return arithmetic_shift_right(value + (value < 0 ? 0x1f : 0), 5);
}

u32 tile_index(const MapBrushViewportState& state, i32 tile_x, i32 tile_y) {
    return static_cast<u32>(tile_y) * state.map_width_tiles +
        static_cast<u32>(tile_x);
}

bool in_map_bounds(const MapBrushViewportState& state, i32 tile_x, i32 tile_y) {
    return tile_x >= 0 && tile_y >= 0 &&
        static_cast<u32>(tile_x) < state.map_width_tiles &&
        static_cast<u32>(tile_y) < state.map_height_tiles;
}

u32 terrain_layer_stride(const MinimapTerrainLayer& layer) {
    if (layer.stride_tiles != 0) {
        return layer.stride_tiles;
    }
    return layer.width_tiles;
}

bool in_minimap_layer_bounds(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    return tile_x >= 0 && tile_y >= 0 &&
        static_cast<u32>(tile_x) < layer.width_tiles &&
        static_cast<u32>(tile_y) < layer.height_tiles;
}

std::size_t terrain_layer_index(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    return static_cast<std::size_t>(tile_y) * terrain_layer_stride(layer) +
        static_cast<std::size_t>(tile_x);
}

u32 terrain_flags_at(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    if (!in_minimap_layer_bounds(layer, tile_x, tile_y)) {
        return 0;
    }
    const std::size_t index = terrain_layer_index(layer, tile_x, tile_y);
    if (index >= layer.terrain_flags.size()) {
        return 0;
    }
    return layer.terrain_flags[index];
}

u32* mutable_terrain_flags_at(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    if (!in_minimap_layer_bounds(layer, tile_x, tile_y)) {
        return nullptr;
    }
    const std::size_t index = terrain_layer_index(layer, tile_x, tile_y);
    if (index >= layer.terrain_flags.size()) {
        return nullptr;
    }
    return &layer.terrain_flags[index];
}

u32 packed_overlay_flags_at(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    if (!in_minimap_layer_bounds(layer, tile_x, tile_y)) {
        return 0;
    }
    const std::size_t index = terrain_layer_index(layer, tile_x, tile_y);
    if (index >= layer.overlay_flags.size()) {
        return 0;
    }
    return layer.overlay_flags[index];
}

u32* mutable_overlay_flags_at(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    if (!in_minimap_layer_bounds(layer, tile_x, tile_y)) {
        return nullptr;
    }
    const std::size_t index = terrain_layer_index(layer, tile_x, tile_y);
    if (index >= layer.overlay_flags.size()) {
        return nullptr;
    }
    return &layer.overlay_flags[index];
}

u8 viewport_class_at(const MapBrushViewportState& state, i32 grid_x, i32 grid_y) {
    if (grid_x < 0 || grid_y < 0 ||
        grid_x >= static_cast<i32>(kMapBrushViewportGridWidth) ||
        grid_y >= static_cast<i32>(kMapBrushViewportGridWidth)) {
        return 0;
    }
    return state.tile_classes[static_cast<std::size_t>(grid_x) +
        static_cast<std::size_t>(grid_y) * kMapBrushViewportGridWidth];
}

void set_minimap_palette(MapBrushArchiveState& state, bool high_color_mode) {
    if (!high_color_mode) {
        state.minimap_palette = {
            0x8610, 0xf7de, 0x07e0, 0xf81f, 0xf81f, 0x07e0, 0xe79c, 0xc718};
    } else {
        state.minimap_palette = {
            0x4210, 0x7bde, 0x03e0, 0x7c1f, 0x7c1f, 0x03e0, 0x739c, 0x6318};
    }
}

u32 component_limit(u32 mask, u32 shift) {
    if (mask == 0) {
        return 0;
    }
    return mask >> (shift & 0x1f);
}

u32 pixel_component(u16 pixel, u32 mask, u32 shift) {
    return (static_cast<u32>(pixel) & mask) >> (shift & 0x1f);
}

u16 pack_pixel_components(
    const MinimapPixelFormat& format, u32 red, u32 green, u32 blue) {
    red = std::min(red, component_limit(format.red_mask, format.red_shift));
    green = std::min(green, component_limit(format.green_mask, format.green_shift));
    blue = std::min(blue, component_limit(format.blue_mask, format.blue_shift));
    return static_cast<u16>(
        ((red & 0xffffu) << (format.red_shift & 0x1f)) |
        ((green & 0xffffu) << (format.green_shift & 0x1f)) |
        ((blue & 0xffffu) << (format.blue_shift & 0x1f)));
}

u16 minimap_palette_pixel(
    const MinimapTerrainRenderConfig& config, i32 palette_index) {
    if (palette_index < 0 ||
        static_cast<std::size_t>(palette_index) >= config.minimap_tile_palette.size()) {
        return 0;
    }
    return config.minimap_tile_palette[static_cast<std::size_t>(palette_index)];
}

void add_pixel_components(const MinimapPixelFormat& format, u16 pixel,
    u32 multiplier, u32& red, u32& green, u32& blue) {
    red += pixel_component(pixel, format.red_mask, format.red_shift) * multiplier;
    green += pixel_component(pixel, format.green_mask, format.green_shift) * multiplier;
    blue += pixel_component(pixel, format.blue_mask, format.blue_shift) * multiplier;
}

void ensure_minimap_brightness_noise(MinimapTerrainRenderConfig& config) {
    if (config.brightness_noise_initialized) {
        return;
    }
    for (u32 y = 0; y < kMinimapNoiseTableWidth; ++y) {
        for (u32 x = 0; x < kMinimapNoiseTableWidth; ++x) {
            config.brightness_noise[static_cast<std::size_t>(y) *
                kMinimapNoiseTableWidth + x] =
                static_cast<u8>(std::rand() % 3 + 6);
        }
    }
    config.brightness_noise_initialized = true;
}

u32 minimap_noise_at(
    const MinimapTerrainRenderConfig& config, u32 screen_x, u32 screen_y) {
    if (screen_x >= kMinimapNoiseTableWidth || screen_y >= kMinimapNoiseTableWidth) {
        return 8;
    }
    return config.brightness_noise[static_cast<std::size_t>(screen_y) *
        kMinimapNoiseTableWidth + screen_x];
}

bool load_fixed_record_into_vector(const char* archive_name, u32 record_index,
    std::vector<u8>& destination, std::size_t expected_size) {
    destination.assign(expected_size, 0);
    if (!LoadTrcRecordIntoBuffer(archive_name, record_index, destination.data(),
            destination.size())) {
        destination.clear();
        return false;
    }
    return true;
}

bool load_trc_record_payload_streamed(const char* archive_name, u32 record_index,
    std::vector<u8>& payload) {
    payload.clear();
    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    payload.assign(reader.entry.original_size, 0);
    if (!ReadOpenTrcRecordBytes(reader, payload.data(), payload.size())) {
        CloseTrcRecordReader(reader);
        payload.clear();
        return false;
    }
    CloseTrcRecordReader(reader);
    return true;
}

bool append_fixed_record(const char* archive_name, const char* name,
    const std::vector<u8>& source, std::size_t byte_count) {
    std::vector<u8> payload(byte_count, 0);
    std::copy_n(source.begin(), std::min(source.size(), byte_count), payload.begin());
    return HandleTrcMemoryRecordAppend(archive_name, name,
        payload.data(), payload.size(), 0x14, 2);
}

bool ensure_minimap_scratch(MinimapRenderState& state) {
    if (state.minimap_width_pixels == 0 || state.minimap_height_pixels == 0) {
        return false;
    }
    const std::size_t required =
        static_cast<std::size_t>(kMinimapScratchPitchPixels) *
        state.minimap_height_pixels;
    if (state.scratch_pixels.size() < required) {
        state.scratch_pixels.resize(required);
    }
    return true;
}

bool minimap_point_in_scratch(const MinimapRenderState& state, i32 x, i32 y) {
    return x >= 0 && y >= 0 &&
        static_cast<u32>(x) < state.minimap_width_pixels &&
        static_cast<u32>(x) < kMinimapScratchPitchPixels &&
        static_cast<u32>(y) < state.minimap_height_pixels;
}

void put_minimap_scratch_pixel(
    MinimapRenderState& state, i32 x, i32 y, u16 color) {
    if (!minimap_point_in_scratch(state, x, y)) {
        return;
    }
    state.scratch_pixels[static_cast<std::size_t>(y) * kMinimapScratchPitchPixels +
        static_cast<std::size_t>(x)] = color;
}

void put_minimap_marker_2x2(
    MinimapRenderState& state, i32 x, i32 y, u16 color) {
    put_minimap_scratch_pixel(state, x, y, color);
    if (static_cast<u32>(x) < state.minimap_width_pixels - 1 &&
        static_cast<u32>(y) < state.minimap_height_pixels - 1) {
        put_minimap_scratch_pixel(state, x + 1, y, color);
        put_minimap_scratch_pixel(state, x, y + 1, color);
        put_minimap_scratch_pixel(state, x + 1, y + 1, color);
    }
}

MinimapOwnerMarkerColors owner_marker_colors(
    const std::vector<MinimapOwnerMarkerColors>& owner_colors, u32 owner_id) {
    if (owner_id >= owner_colors.size()) {
        return {};
    }
    return owner_colors[owner_id];
}

u32 overlay_stride(const MinimapOverlayLayer& overlay_layer) {
    if (overlay_layer.stride_tiles != 0) {
        return overlay_layer.stride_tiles;
    }
    return overlay_layer.width_tiles;
}

u32 terrain_decoration_stride(const TerrainDecorationLayer& layer) {
    if (layer.stride_tiles != 0) {
        return layer.stride_tiles;
    }
    return layer.width_tiles;
}

u32 terrain_decoration_flags_at(
    const TerrainDecorationLayer& layer, i32 tile_x, i32 tile_y) {
    if (tile_x < 0 || tile_y < 0 ||
        static_cast<u32>(tile_x) >= layer.width_tiles ||
        static_cast<u32>(tile_y) >= layer.height_tiles) {
        return 0;
    }
    const std::size_t index =
        static_cast<std::size_t>(tile_y) * terrain_decoration_stride(layer) +
        static_cast<std::size_t>(tile_x);
    if (index >= layer.tile_flags.size()) {
        return 0;
    }
    return layer.tile_flags[index];
}

i32 terrain_brush_resource_base(
    const TerrainDecorationResourceTable& resources, u32 brush_id) {
    if (brush_id >= resources.resource_base_by_id.size()) {
        return 0;
    }
    return resources.resource_base_by_id[brush_id];
}

u32 terrain_target_pitch(const TerrainViewportRenderState& state) {
    return state.target_pitch_pixels != 0 ?
        state.target_pitch_pixels : state.target_width_pixels;
}

bool ensure_terrain_pixels(TerrainViewportRenderState& state) {
    const u32 pitch = terrain_target_pitch(state);
    if (pitch == 0 || state.target_height_pixels == 0) {
        return false;
    }
    const std::size_t required =
        static_cast<std::size_t>(pitch) * state.target_height_pixels;
    if (state.pixels.size() < required) {
        state.pixels.resize(required);
    }
    return true;
}

u32 signed_mod32(i32 value) {
    i32 result = value & 0x1f;
    if (value < 0) {
        result = ((result - 1) | ~0x1f) + 1;
    }
    return static_cast<u32>(result);
}

u32 read_le_u32(const u8* value) {
    return static_cast<u32>(value[0]) |
        (static_cast<u32>(value[1]) << 8) |
        (static_cast<u32>(value[2]) << 16) |
        (static_cast<u32>(value[3]) << 24);
}

u16 read_le_u16(const u8* value) {
    return static_cast<u16>(value[0]) | static_cast<u16>(value[1] << 8);
}

std::array<u32, 6> read_resource_metadata(const u8* header) {
    std::array<u32, 6> metadata{};
    for (std::size_t i = 0; i < metadata.size(); ++i) {
        metadata[i] = read_le_u32(header + i * 4);
    }
    return metadata;
}

void set_terrain_pixel(
    TerrainViewportRenderState& state, i32 x, i32 y, u16 color) {
    if (x < 0 || y < 0 ||
        static_cast<u32>(x) >= state.target_width_pixels ||
        static_cast<u32>(y) >= state.target_height_pixels) {
        return;
    }
    state.pixels[static_cast<std::size_t>(y) * terrain_target_pitch(state) +
        static_cast<std::size_t>(x)] = color;
}

TerrainTileDrawCommand* append_terrain_draw_command(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, TerrainTileDrawKind kind,
    i32 screen_x, i32 screen_y, i32 tile_x, i32 tile_y, u32 overlay_slot) {
    TerrainTileClipRect clip{};
    clip.dst_x = screen_x;
    clip.dst_y = screen_y;
    if (!ClipTerrainTileDrawRect(
            clip, state.target_width_pixels, state.target_height_pixels)) {
        return nullptr;
    }

    TerrainTileDrawCommand command{};
    command.kind = kind;
    command.screen_x = screen_x;
    command.screen_y = screen_y;
    command.tile_x = tile_x;
    command.tile_y = tile_y;
    command.terrain_flags = terrain_flags_at(layer, tile_x, tile_y);
    command.overlay_flags = packed_overlay_flags_at(layer, tile_x, tile_y);
    command.overlay_slot = overlay_slot;
    command.clip = clip;
    state.draw_commands.push_back(command);
    return &state.draw_commands.back();
}

void push_terrain_decoration_command(TerrainDecorationRenderState& state,
    TerrainDecorationRenderKind kind, u32 render_layer, u32 sort_key,
    i32 tile_x, i32 tile_y, i32 resource_index, u32 flags) {
    TerrainDecorationRenderCommand command{};
    command.kind = kind;
    command.render_layer = render_layer;
    command.sort_key = sort_key;
    command.tile_x = tile_x;
    command.tile_y = tile_y;
    command.screen_x = tile_x * 0x20 - state.camera_x;
    command.screen_y = tile_y * 0x20 - state.camera_y;
    command.resource_index = resource_index;
    command.flags = flags;
    state.render_commands.push_back(command);
}

u32 terrain_decoration_center_sort_key(i32 tile_x, i32 tile_y) {
    return static_cast<u32>(((tile_y * 0x20 + 0x10) << 0x0d) +
        0x50000020 + tile_x * 0x20);
}

u32 terrain_decoration_tile_sort_key(i32 tile_x, i32 tile_y) {
    return static_cast<u32>((tile_y * 0x20 << 0x0d) + 0x50000000 + tile_x * 0x20);
}

} // namespace

MapBrushTileClass ClassifyMapBrushTile(
    const MapBrushViewportState& state, i32 tile_x, i32 tile_y) {
    if (!in_map_bounds(state, tile_x, tile_y)) {
        return MapBrushTileClass::clear;
    }
    const u32 index = tile_index(state, tile_x, tile_y);
    if (index >= state.tile_flags.size()) {
        return MapBrushTileClass::clear;
    }

    const u32 flags = state.tile_flags[index];
    if (((flags >> 0x1b) & 1u) != 0) {
        return MapBrushTileClass::filled;
    }
    if (((flags >> 0x1c) & 1u) != 0) {
        return MapBrushTileClass::edge;
    }
    return MapBrushTileClass::clear;
}

void BuildViewportBrushClassification(MapBrushViewportState& state, i32 camera_x,
    i32 camera_y) {
    state.camera_x = camera_x;
    state.camera_y = camera_y;

    const i32 start_tile_x = original_world_to_tile(camera_x);
    const i32 start_tile_y = original_world_to_tile(camera_y);
    const i32 grid_width =
        (static_cast<i32>(static_cast<u32>(camera_x + state.viewport_width_pixels) >> 5) -
            start_tile_x) + 1;
    const i32 grid_height =
        (static_cast<i32>(static_cast<u32>(camera_y + state.viewport_height_pixels) >> 5) -
            start_tile_y) + 1;

    state.tile_classes.fill(0);
    for (i32 y = 0; y < grid_height && y < static_cast<i32>(kMapBrushViewportGridWidth);
         ++y) {
        for (i32 x = 0; x < grid_width && x < static_cast<i32>(kMapBrushViewportGridWidth);
             ++x) {
            const i32 tile_x = start_tile_x + x;
            const i32 tile_y = start_tile_y + y;
            MapBrushTileClass tile_class = ClassifyMapBrushTile(state, tile_x, tile_y);
            if (tile_class == MapBrushTileClass::filled) {
                for (const auto& offset : kNeighborOffsets) {
                    const i32 nx = tile_x + offset[0];
                    const i32 ny = tile_y + offset[1];
                    if (in_map_bounds(state, nx, ny) &&
                        ClassifyMapBrushTile(state, nx, ny) == MapBrushTileClass::clear) {
                        tile_class = MapBrushTileClass::edge;
                        break;
                    }
                }
            }
            state.tile_classes[static_cast<std::size_t>(x) +
                static_cast<std::size_t>(y) * kMapBrushViewportGridWidth] =
                static_cast<u8>(tile_class);
        }
    }
}

u32 ResolveViewportBrushMask(
    const MapBrushViewportState& state, i32 grid_x, i32 grid_y) {
    if (viewport_class_at(state, grid_x, grid_y) == static_cast<u8>(MapBrushTileClass::filled)) {
        return 0xff;
    }

    const i32 grid_width = static_cast<i32>((state.viewport_width_pixels >> 5) + 1);
    const i32 grid_height = static_cast<i32>((state.viewport_height_pixels >> 5) + 1);
    u32 mask = 0;
    for (u32 i = 0; i < kNeighborOffsets.size(); ++i) {
        const i32 nx = grid_x + kNeighborOffsets[i][0];
        const i32 ny = grid_y + kNeighborOffsets[i][1];
        if (nx >= 0 && nx < grid_width && ny >= 0 && ny < grid_height &&
            viewport_class_at(state, nx, ny) == static_cast<u8>(MapBrushTileClass::filled)) {
            mask |= 1u << i;
        }
    }
    if (mask != 0) {
        return mask;
    }

    if (viewport_class_at(state, grid_x, grid_y) == static_cast<u8>(MapBrushTileClass::edge)) {
        return 0x1ff;
    }

    mask = 0x100;
    for (u32 i = 0; i < kNeighborOffsets.size(); ++i) {
        const i32 nx = grid_x + kNeighborOffsets[i][0];
        const i32 ny = grid_y + kNeighborOffsets[i][1];
        if (nx >= 0 && nx < grid_width && ny >= 0 && ny < grid_height &&
            viewport_class_at(state, nx, ny) == static_cast<u8>(MapBrushTileClass::edge)) {
            mask |= 1u << i;
        }
    }
    return mask;
}

void BuildViewportBrushDrawCommands(MapBrushViewportState& state) {
    BuildViewportBrushClassification(state, state.camera_x, state.camera_y);
    state.viewport_mod_x = state.camera_x % 0x20;
    state.viewport_mod_y = state.camera_y % 0x20;
    state.draw_commands.clear();

    for (i32 screen_y = 0; screen_y - state.viewport_mod_y <
         static_cast<i32>(state.viewport_height_pixels); screen_y += 0x20) {
        for (i32 screen_x = 0; screen_x - state.viewport_mod_x <
             static_cast<i32>(state.viewport_width_pixels); screen_x += 0x20) {
            MapBrushDrawCommand command{};
            command.screen_x = screen_x;
            command.screen_y = screen_y;
            command.mask = ResolveViewportBrushMask(state,
                original_world_to_tile(screen_x), original_world_to_tile(screen_y));
            state.draw_commands.push_back(command);
        }
    }
}

bool TerrainTileUsesUniformBase(u32 terrain_flags) {
    return (((terrain_flags >> 3) & 7u) == (terrain_flags & 7u)) &&
        (((terrain_flags >> 9) & 7u) == ((terrain_flags >> 6) & 7u)) &&
        (((terrain_flags >> 0x0f) & 7u) == ((terrain_flags >> 0x0c) & 7u));
}

bool ClipTerrainTileDrawRect(
    TerrainTileClipRect& rect, u32 target_width_pixels, u32 target_height_pixels) {
    const i32 target_width = static_cast<i32>(target_width_pixels);
    const i32 target_height = static_cast<i32>(target_height_pixels);
    if (target_width <= 0 || target_height <= 0) {
        return false;
    }

    if (rect.dst_x < 0 || rect.dst_y < 0 ||
        target_width < rect.dst_x + rect.width ||
        target_height < rect.dst_y + rect.height) {
        if (rect.dst_x >= target_width || rect.dst_y >= target_height) {
            return false;
        }
        if (rect.dst_x + rect.width < 1 || rect.dst_y + rect.height < 1) {
            return false;
        }
        if (rect.dst_x < 0) {
            rect.src_x -= rect.dst_x;
            rect.width -= rect.src_x;
            rect.dst_x = 0;
        }
        if (rect.dst_y < 0) {
            rect.src_y -= rect.dst_y;
            rect.height -= rect.src_y;
            rect.dst_y = 0;
        }
        if (target_width < rect.dst_x + rect.width) {
            rect.width -= (rect.dst_x + rect.width) - target_width;
        }
        if (target_height < rect.dst_y + rect.height) {
            rect.height -= (rect.dst_y + rect.height) - target_height;
        }
        if (rect.width < 1 || rect.height < 1) {
            return false;
        }
    }
    return true;
}

void AppendUniformTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y) {
    append_terrain_draw_command(state, layer, TerrainTileDrawKind::uniform,
        screen_x, screen_y, tile_x, tile_y, 0xffffffffu);
}

void AppendBlendedTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y) {
    append_terrain_draw_command(state, layer, TerrainTileDrawKind::blended,
        screen_x, screen_y, tile_x, tile_y, 0xffffffffu);
}

void AppendOverlayTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y) {
    TerrainTileDrawCommand* command = append_terrain_draw_command(state,
        layer, TerrainTileDrawKind::overlay,
        screen_x, screen_y, tile_x, tile_y, 0xffffffffu);
    if (command != nullptr) {
        command->overlay_mode = TerrainOverlayDrawMode::dispatch;
    }
}

void AppendMaskedOverlayTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, i32 overlay_palette_index) {
    TerrainTileDrawCommand* command = append_terrain_draw_command(state,
        layer, TerrainTileDrawKind::overlay,
        screen_x, screen_y, tile_x, tile_y, 0xffffffffu);
    if (command != nullptr) {
        command->overlay_mode = TerrainOverlayDrawMode::masked;
        command->overlay_palette_index = overlay_palette_index;
    }
}

void AppendTransparentOverlayTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, u32 overlay_slot) {
    TerrainTileDrawCommand* command = append_terrain_draw_command(state,
        layer, TerrainTileDrawKind::overlay,
        screen_x, screen_y, tile_x, tile_y, overlay_slot);
    if (command != nullptr) {
        command->overlay_mode = TerrainOverlayDrawMode::transparent;
    }
}

void AppendBlendedOverlayTerrainTileDrawCommand565(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, u32 overlay_slot,
    i32 overlay_palette_index, i32 mask_palette_index) {
    TerrainTileDrawCommand* command = append_terrain_draw_command(state,
        layer, TerrainTileDrawKind::overlay,
        screen_x, screen_y, tile_x, tile_y, overlay_slot);
    if (command != nullptr) {
        command->overlay_mode = TerrainOverlayDrawMode::blended_565;
        command->overlay_palette_index = overlay_palette_index;
        command->overlay_mask_palette_index = mask_palette_index;
    }
}

void AppendBlendedOverlayTerrainTileDrawCommand555(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, u32 overlay_slot,
    i32 overlay_palette_index, i32 mask_palette_index) {
    TerrainTileDrawCommand* command = append_terrain_draw_command(state,
        layer, TerrainTileDrawKind::overlay,
        screen_x, screen_y, tile_x, tile_y, overlay_slot);
    if (command != nullptr) {
        command->overlay_mode = TerrainOverlayDrawMode::blended_555;
        command->overlay_palette_index = overlay_palette_index;
        command->overlay_mask_palette_index = mask_palette_index;
    }
}

void BuildTerrainTileDrawCommands(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 camera_x, i32 camera_y) {
    state.draw_commands.clear();
    const i32 start_tile_x = original_world_to_tile(camera_x);
    const i32 start_tile_y = original_world_to_tile(camera_y);
    const i32 end_tile_x =
        static_cast<i32>(static_cast<u32>(camera_x + state.target_width_pixels) >> 5);
    const i32 end_tile_y =
        static_cast<i32>(static_cast<u32>(camera_y + state.target_height_pixels) >> 5);

    i32 screen_y = start_tile_y * 0x20 - camera_y;
    for (i32 tile_y = start_tile_y; tile_y <= end_tile_y; ++tile_y) {
        i32 screen_x = start_tile_x * 0x20 - camera_x;
        for (i32 tile_x = start_tile_x; tile_x <= end_tile_x; ++tile_x) {
            const u32 terrain_flags = terrain_flags_at(layer, tile_x, tile_y);
            if (TerrainTileUsesUniformBase(terrain_flags)) {
                AppendUniformTerrainTileDrawCommand(
                    state, layer, screen_x, screen_y, tile_x, tile_y);
            } else {
                AppendBlendedTerrainTileDrawCommand(
                    state, layer, screen_x, screen_y, tile_x, tile_y);
            }
            if ((packed_overlay_flags_at(layer, tile_x, tile_y) & 0x3fu) != 0) {
                AppendOverlayTerrainTileDrawCommand(
                    state, layer, screen_x, screen_y, tile_x, tile_y);
            }
            screen_x += 0x20;
        }
        screen_y += 0x20;
    }
}

void BuildVisibleTerrainDecorationRenderCommands(TerrainDecorationRenderState& state,
    const TerrainDecorationLayer& terrain_decoration_layer,
    const TerrainDecorationLayer& brush_layer,
    const TerrainDecorationResourceTable& brush_resources,
    const TerrainTileSheetState& tile_sheet) {
    state.render_commands.clear();

    const i32 start_tile_x = original_world_to_tile(state.visible_left_world);
    const i32 start_tile_y = original_world_to_tile(state.visible_top_world);
    i32 end_tile_x = original_world_to_tile(state.visible_right_world);
    i32 end_tile_y = original_world_to_tile(state.visible_bottom_world);
    const i32 max_tile_x = 0xff;
    const i32 max_tile_y = 0xff;
    end_tile_x = std::min(end_tile_x, max_tile_x);
    end_tile_y = std::min(end_tile_y, max_tile_y);

    for (i32 tile_y = start_tile_y; tile_y <= end_tile_y; ++tile_y) {
        for (i32 tile_x = start_tile_x; tile_x <= end_tile_x; ++tile_x) {
            if (tile_x < 0 || tile_y < 0) {
                continue;
            }

            const u32 terrain_flags =
                terrain_decoration_flags_at(terrain_decoration_layer, tile_x, tile_y);
            const u32 decoration_type = (terrain_flags >> 8) & 7u;
            if (decoration_type == 1) {
                if (((terrain_flags >> 0x0b) & 1u) == 0) {
                    push_terrain_decoration_command(state,
                        TerrainDecorationRenderKind::terrain_type1, 2,
                        terrain_decoration_center_sort_key(tile_x, tile_y),
                        tile_x, tile_y,
                        static_cast<i32>((terrain_flags & 0xffu) +
                            static_cast<u32>(state.terrain_type1_resource_base)),
                        (terrain_flags >> 0x1d) & 1u);
                }
            } else if (decoration_type == 3) {
                push_terrain_decoration_command(state,
                    TerrainDecorationRenderKind::terrain_type3, 2,
                    terrain_decoration_tile_sort_key(tile_x, tile_y),
                    tile_x, tile_y,
                    static_cast<i32>((terrain_flags & 0xffu) + tile_sheet.resource_start),
                    0);
            }

            const u32 brush_flags = terrain_decoration_flags_at(brush_layer, tile_x, tile_y);
            if (((brush_flags >> 0x1b) & 1u) == 0 &&
                ((brush_flags >> 0x1c) & 1u) != 0 &&
                (brush_flags & 0xffu) != 0) {
                const u32 brush_id = brush_flags & 0xffu;
                const u32 variant = (brush_flags >> 0x12) & 0x0fu;
                push_terrain_decoration_command(state,
                    TerrainDecorationRenderKind::brush_edge, 10,
                    terrain_decoration_tile_sort_key(tile_x, tile_y),
                    tile_x, tile_y,
                    terrain_brush_resource_base(brush_resources, brush_id) +
                        static_cast<i32>(variant),
                    brush_flags);
            }
        }
    }
}

bool LoadMinimapBrushRecords(MapBrushArchiveState& state,
    const char* archive_name, bool high_color_mode) {
    state.minimap_record_153.assign(kMinimapBrushRecordBytes, 0);
    if (!LoadTrcRecordIntoBuffer(archive_name, 0x153,
            state.minimap_record_153.data(), state.minimap_record_153.size())) {
        state.minimap_record_153.clear();
        return false;
    }
    ServeMilesSound();
    state.minimap_record_154.assign(kMinimapBrushRecordBytes, 0);
    if (!LoadTrcRecordIntoBuffer(archive_name, 0x154,
            state.minimap_record_154.data(), state.minimap_record_154.size())) {
        state.minimap_record_154.clear();
        return false;
    }
    set_minimap_palette(state, high_color_mode);
    return true;
}

bool DecodeMapBrushArchivePayload(MapBrushArchiveState& state,
    const std::vector<u8>& payload) {
    if (payload.empty()) {
        return false;
    }
    state.terrain_layer_a = payload;
    state.terrain_layer_b = payload;
    state.terrain_layer_c = payload;
    return true;
}

bool SaveMapBrushRecord(const char* archive_name, const MapBrushArchiveState& state) {
    return append_fixed_record(archive_name, "BRUSH", state.brush_record,
        kMapBrushRecordBytes);
}

bool LoadMapBrushRecord(MapBrushArchiveState& state,
    const char* archive_name, u32 record_index) {
    return load_fixed_record_into_vector(archive_name, record_index, state.brush_record,
        kMapBrushRecordBytes);
}

bool SaveMapHillBrushRecord(const char* archive_name, const MapBrushArchiveState& state) {
    return append_fixed_record(archive_name, "HILL_BRUSH", state.hill_brush_record,
        kMapHillBrushRecordBytes);
}

bool LoadMapHillBrushRecord(MapBrushArchiveState& state,
    const char* archive_name, u32 record_index) {
    return load_fixed_record_into_vector(archive_name, record_index, state.hill_brush_record,
        kMapHillBrushRecordBytes);
}

i32 MinimapScreenToWorldX(const MinimapRenderState& state, i32 screen_x) {
    if (state.scale_percent == 0) {
        return 0;
    }
    return ((screen_x - state.inset_x) * 0xc80) / static_cast<i32>(state.scale_percent);
}

i32 MinimapScreenToWorldY(const MinimapRenderState& state, i32 screen_y) {
    if (state.scale_percent == 0) {
        return 0;
    }
    return ((screen_y - state.inset_y) * 0xc80) / static_cast<i32>(state.scale_percent);
}

i32 MinimapWorldToScreenX(const MinimapRenderState& state, i32 world_x) {
    return (original_world_to_tile(world_x) * static_cast<i32>(state.scale_percent)) /
        100 + state.inset_x;
}

i32 MinimapWorldToScreenY(const MinimapRenderState& state, i32 world_y) {
    return (original_world_to_tile(world_y) * static_cast<i32>(state.scale_percent)) /
        100 + state.inset_y;
}

i32 ResolveMinimapTilePatternIndex(i32 tile_x, i32 tile_y) {
    return (tile_y % 10) * 0x14 + (tile_x % 0x14);
}

i32 ResolveMinimapTerrainPaletteOffset(
    const MinimapTerrainRenderConfig& config, u32 terrain_code) {
    if (terrain_code >= config.terrain_palette_offsets.size()) {
        return 0;
    }
    if (terrain_code == 6) {
        return static_cast<i32>(config.animated_terrain_bank * 0xc8) +
            config.terrain_palette_offsets[terrain_code];
    }
    return config.terrain_palette_offsets[terrain_code];
}

u32 GetMinimapOverlayId(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot) {
    const u32 flags = packed_overlay_flags_at(layer, tile_x, tile_y);
    switch (slot) {
    case 0:
        return flags & 0x3fu;
    case 1:
        return (flags >> 8) & 0x3fu;
    case 2:
        return (flags >> 0x10) & 0x3fu;
    case 3:
        return (flags >> 0x18) & 0x3fu;
    default:
        return 0;
    }
}

u32 GetMinimapOverlayVariant(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot) {
    const u32 flags = packed_overlay_flags_at(layer, tile_x, tile_y);
    switch (slot) {
    case 0:
        return (flags >> 6) & 3u;
    case 1:
        return (flags >> 0xe) & 3u;
    case 2:
        return (flags >> 0x16) & 3u;
    case 3:
        return flags >> 0x1e;
    default:
        return 0;
    }
}

i32 ResolveMinimapOverlayPaletteIndex(
    const MinimapTerrainLayer& layer, const MinimapTerrainRenderConfig& config,
    i32 tile_x, i32 tile_y, u32 slot) {
    const u32 overlay_id = GetMinimapOverlayId(layer, tile_x, tile_y, slot);
    const u32 variant = GetMinimapOverlayVariant(layer, tile_x, tile_y, slot);
    if (overlay_id >= config.overlay_variant_offsets.size() ||
        variant >= config.overlay_variant_offsets[overlay_id].size()) {
        return config.overlay_palette_base_offset;
    }
    return config.overlay_palette_base_offset +
        config.overlay_variant_offsets[overlay_id][variant];
}

i32 ResolveTerrainBlendMaskPaletteIndex(
    const MinimapTerrainLayer& layer, const MinimapTerrainRenderConfig& config,
    i32 tile_x, i32 tile_y) {
    const u32 flags = terrain_flags_at(layer, tile_x, tile_y);
    const u32 mask_id = (flags >> 0x12) & 0x3fu;
    const u32 variant = (flags >> 0x18) & 3u;
    i32 base = config.terrain_blend_mask_base_offset;
    if ((tile_x & 1) == 0) {
        ++base;
    }
    if (mask_id >= config.terrain_blend_mask_offsets.size() ||
        variant >= config.terrain_blend_mask_offsets[mask_id].size()) {
        return base;
    }
    return base + config.terrain_blend_mask_offsets[mask_id][variant];
}

i32 ResolveMinimapOverlayBlendMaskPaletteIndex(
    const MinimapTerrainLayer& layer, const MinimapTerrainRenderConfig& config,
    i32 tile_x, i32 tile_y, u32 slot) {
    const u32 overlay_id = GetMinimapOverlayId(layer, tile_x, tile_y, slot);
    if (overlay_id >= config.overlay_blend_mask_offsets.size()) {
        return -1;
    }
    const i32 offset = config.overlay_blend_mask_offsets[overlay_id];
    if (offset < 0) {
        return offset;
    }
    return config.overlay_blend_mask_base_offset + offset;
}

bool CheckMinimapOverlaySlotOccupied(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot) {
    return GetMinimapOverlayId(layer, tile_x, tile_y, slot) != 0;
}

bool IsMinimapTerrainTileInBounds(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    return in_minimap_layer_bounds(layer, tile_x, tile_y);
}

void SetTerrainBlendMaskFields(MinimapTerrainLayer& layer,
    i32 tile_x, i32 tile_y, u32 mask_id, u32 variant) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags == nullptr) {
        return;
    }
    *flags = (*flags & 0xff03ffffu) | ((mask_id & 0x3fu) << 0x12);
    *flags = (*flags & 0xfcffffffu) | ((variant & 3u) << 0x18);
}

void SetTerrainFlagA(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, bool enabled) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags != nullptr) {
        *flags = (*flags & 0xdfffffffu) | (static_cast<u32>(enabled) << 0x1d);
    }
}

void SetTerrainFlagB(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, bool enabled) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags != nullptr) {
        *flags = (*flags & 0xbfffffffu) | (static_cast<u32>(enabled) << 0x1e);
    }
}

void SetTerrainFlagC(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, bool enabled) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags != nullptr) {
        *flags = (*flags & 0x7fffffffu) | (static_cast<u32>(enabled) << 0x1f);
    }
}

void SetTerrainHeightShade(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 shade_code) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags != nullptr) {
        *flags = (*flags & 0xe3ffffffu) | ((shade_code & 7u) << 0x1a);
    }
}

void SetMinimapOverlaySlot(MinimapTerrainLayer& layer,
    const MinimapTerrainRenderConfig& config, i32 tile_x, i32 tile_y,
    u32 slot, u32 overlay_id, u32 variant) {
    u32* overlay_flags = mutable_overlay_flags_at(layer, tile_x, tile_y);
    if (overlay_flags == nullptr) {
        return;
    }
    switch (slot) {
    case 0:
        *overlay_flags = (*overlay_flags & 0xffffffc0u) | (overlay_id & 0x3fu);
        *overlay_flags = (*overlay_flags & 0xffffff3fu) | ((variant & 3u) << 6);
        break;
    case 1:
        *overlay_flags = (*overlay_flags & 0xffffc0ffu) | ((overlay_id & 0x3fu) << 8);
        *overlay_flags = (*overlay_flags & 0xffff3fffu) | ((variant & 3u) << 0x0e);
        break;
    case 2:
        *overlay_flags = (*overlay_flags & 0xffc0ffffu) | ((overlay_id & 0x3fu) << 0x10);
        *overlay_flags = (*overlay_flags & 0xff3fffffu) | ((variant & 3u) << 0x16);
        break;
    case 3:
        *overlay_flags = (*overlay_flags & 0xc0ffffffu) | ((overlay_id & 0x3fu) << 0x18);
        *overlay_flags = (*overlay_flags & 0x3fffffffu) | ((variant & 3u) << 0x1e);
        break;
    default:
        return;
    }

    const u32 primary_overlay_id = *overlay_flags & 0x3fu;
    const bool flag_a = primary_overlay_id < config.overlay_sets_terrain_flag_a.size() &&
        config.overlay_sets_terrain_flag_a[primary_overlay_id] == 1;
    const bool flag_c = primary_overlay_id < config.overlay_sets_terrain_flag_c.size() &&
        config.overlay_sets_terrain_flag_c[primary_overlay_id] == 1;
    SetTerrainFlagA(layer, tile_x, tile_y, flag_a);
    SetTerrainFlagB(layer, tile_x, tile_y, false);
    SetTerrainFlagC(layer, tile_x, tile_y, flag_c);

    i32 shade = -1;
    if (primary_overlay_id < config.overlay_height_offsets.size()) {
        shade = config.overlay_height_base + config.overlay_height_offsets[primary_overlay_id];
    }
    SetTerrainHeightShade(layer, tile_x, tile_y, shade < 0 ? 0u : static_cast<u32>(shade));
}

void SetTerrainUpperCornerCode(MinimapTerrainLayer& layer,
    i32 tile_x, i32 tile_y, u32 terrain_code, u32 corner) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags == nullptr) {
        return;
    }
    if (corner == 0) {
        *flags = (*flags & 0xffffffc7u) | ((terrain_code & 7u) << 3);
    } else if (corner == 1) {
        *flags = (*flags & 0xfffff1ffu) | ((terrain_code & 7u) << 9);
    } else if (corner == 2) {
        *flags = (*flags & 0xfffc7fffu) | ((terrain_code & 7u) << 0x0f);
    }
}

void SetTerrainLowerCornerCode(MinimapTerrainLayer& layer,
    i32 tile_x, i32 tile_y, u32 terrain_code, u32 corner) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags == nullptr) {
        return;
    }
    if (corner == 0) {
        *flags = (*flags & 0xfffffff8u) | (terrain_code & 7u);
    } else if (corner == 1) {
        *flags = (*flags & 0xfffffe3fu) | ((terrain_code & 7u) << 6);
    } else if (corner == 2) {
        *flags = (*flags & 0xffff8fffu) | ((terrain_code & 7u) << 0x0c);
    }
}

bool TerrainTileUsesTerrainCode6(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    const u32 flags = terrain_flags_at(layer, tile_x, tile_y);
    constexpr std::array<u32, 6> kTerrainCodeShifts{0, 6, 0x0c, 3, 9, 0x0f};
    for (u32 shift : kTerrainCodeShifts) {
        if (((flags >> shift) & 7u) == 6u) {
            return true;
        }
    }
    return false;
}

void SetTerrainCodeAndRefreshFlags(MinimapTerrainLayer& layer,
    i32 upper_half, i32 tile_x, i32 tile_y, u32 terrain_code, u32 corner) {
    if (upper_half == 0) {
        SetTerrainLowerCornerCode(layer, tile_x, tile_y, terrain_code, corner);
    } else {
        SetTerrainUpperCornerCode(layer, tile_x, tile_y, terrain_code, corner);
    }

    if ((packed_overlay_flags_at(layer, tile_x, tile_y) & 0x3fu) != 0) {
        return;
    }
    if (terrain_code == 6) {
        SetTerrainFlagA(layer, tile_x, tile_y, false);
        SetTerrainFlagB(layer, tile_x, tile_y, true);
        SetTerrainFlagC(layer, tile_x, tile_y, false);
    } else {
        SetTerrainFlagA(layer, tile_x, tile_y, true);
        SetTerrainFlagB(layer, tile_x, tile_y, false);
        SetTerrainFlagC(layer, tile_x, tile_y, true);
    }
}

void ClearTerrainUpperCornerCode(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 corner) {
    u32* flags = mutable_terrain_flags_at(layer, tile_x, tile_y);
    if (flags == nullptr) {
        return;
    }
    if (corner == 0) {
        *flags &= 0xffffffc7u;
    } else if (corner == 1) {
        *flags &= 0xfffff1ffu;
    } else if (corner == 2) {
        *flags &= 0xfffc7fffu;
    }
}

void ClearTerrainUpperCornerCodes01(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    for (u32 corner = 0; corner < 2; ++corner) {
        ClearTerrainUpperCornerCode(layer, tile_x, tile_y, corner);
    }
}

void ClearMinimapOverlaySlot(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot) {
    u32* overlay_flags = mutable_overlay_flags_at(layer, tile_x, tile_y);
    if (overlay_flags == nullptr) {
        return;
    }
    switch (slot) {
    case 0:
        *overlay_flags &= 0xffffffc0u;
        break;
    case 1:
        *overlay_flags &= 0xffffc0ffu;
        break;
    case 2:
        *overlay_flags &= 0xffc0ffffu;
        break;
    case 3:
        *overlay_flags &= 0xc0ffffffu;
        break;
    default:
        return;
    }
    if (slot != 0) {
        return;
    }
    if (TerrainTileUsesTerrainCode6(layer, tile_x, tile_y)) {
        SetTerrainFlagA(layer, tile_x, tile_y, false);
        SetTerrainFlagB(layer, tile_x, tile_y, true);
        SetTerrainFlagC(layer, tile_x, tile_y, false);
    } else {
        SetTerrainFlagA(layer, tile_x, tile_y, true);
        SetTerrainFlagB(layer, tile_x, tile_y, false);
        SetTerrainFlagC(layer, tile_x, tile_y, true);
    }
}

void ClearAllMinimapOverlaySlots(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    for (u32 slot = 0; slot < 4; ++slot) {
        ClearMinimapOverlaySlot(layer, tile_x, tile_y, slot);
    }
}

void ResetTerrainOverlayFlags(MinimapTerrainLayer& layer) {
    for (u32 tile_y = 0; tile_y < layer.height_tiles; ++tile_y) {
        for (u32 tile_x = 0; tile_x < layer.width_tiles; ++tile_x) {
            SetTerrainHeightShade(layer, static_cast<i32>(tile_x), static_cast<i32>(tile_y), 0);
            ClearAllMinimapOverlaySlots(
                layer, static_cast<i32>(tile_x), static_cast<i32>(tile_y));
        }
    }
}

void ClearTerrainRenderGridLines(
    TerrainViewportRenderState& state, i32 camera_x, i32 camera_y) {
    if (!ensure_terrain_pixels(state)) {
        return;
    }
    const u32 pitch = terrain_target_pitch(state);
    const i32 first_row = 0x1f - static_cast<i32>(signed_mod32(camera_y));
    for (i32 y = first_row; y < static_cast<i32>(state.target_height_pixels); y += 0x20) {
        if (y < 0) {
            continue;
        }
        const std::size_t row = static_cast<std::size_t>(y) * pitch;
        std::fill_n(state.pixels.begin() + static_cast<std::ptrdiff_t>(row),
            state.target_width_pixels, 0);
    }

    const i32 first_col = 0x1f - static_cast<i32>(signed_mod32(camera_x));
    for (i32 x = first_col; x < static_cast<i32>(state.target_width_pixels); x += 0x20) {
        if (x < 0) {
            continue;
        }
        for (u32 y = 0; y < state.target_height_pixels; ++y) {
            state.pixels[static_cast<std::size_t>(y) * pitch +
                static_cast<std::size_t>(x)] = 0;
        }
    }
}

void DrawClippedTerrainDiagonalLine(TerrainViewportRenderState& state,
    i32 start_x, i32 start_y, i32 end_x, i32 end_y, u16 color, u32 thickness) {
    if (!ensure_terrain_pixels(state) || thickness == 0) {
        return;
    }
    if (!((((start_x >= 0) || (end_x >= 0)) &&
            ((start_x < static_cast<i32>(state.target_width_pixels)) ||
                (end_x < static_cast<i32>(state.target_width_pixels)))) &&
           (((start_y >= 0) || (end_y >= 0)) &&
            ((start_y < static_cast<i32>(state.target_height_pixels)) ||
                (end_y < static_cast<i32>(state.target_height_pixels))))) ||
        start_y == end_y || start_x == end_x) {
        return;
    }

    const i32 max_x = static_cast<i32>(state.target_width_pixels) - 1;
    const i32 max_y = static_cast<i32>(state.target_height_pixels) - 1;
    if (end_x < start_x) {
        std::swap(start_x, end_x);
        std::swap(start_y, end_y);
    }
    const bool descending = end_y <= start_y;
    if (start_x < 0) {
        const i32 delta = descending ? start_x : -start_x;
        start_y += delta / 2;
        start_x = 0;
    }
    if (max_x < end_x) {
        const i32 overflow = end_x - max_x;
        end_y += (descending ? overflow : -overflow) / 2;
        end_x = max_x;
    }
    if (start_y < 0) {
        start_x += start_y * -2;
        start_y = 0;
    }
    if (max_y < start_y) {
        start_x += (start_y - max_y) * 2;
        start_y = max_y;
    }
    if (end_y < 0) {
        end_x += end_y * 2;
        end_y = 0;
    }
    if (max_y < end_y) {
        end_x += (end_y - max_y) * -2;
        end_y = max_y;
    }

    if (start_y < end_y) {
        for (u32 row = 0; row < thickness; ++row) {
            i32 y = start_y + static_cast<i32>(row);
            for (i32 x = start_x; x < end_x; x += 2, ++y) {
                set_terrain_pixel(state, x, y, color);
                set_terrain_pixel(state, x + 1, y, color);
            }
        }
    } else {
        for (u32 row = 0; row < thickness; ++row) {
            i32 y = start_y - static_cast<i32>(row);
            for (i32 x = start_x; x < end_x; x += 2, --y) {
                set_terrain_pixel(state, x, y, color);
                set_terrain_pixel(state, x + 1, y, color);
            }
        }
    }
}

void ResetTerrainTilePulseState(TerrainTilePulseState& pulse_state) {
    pulse_state.slots = {};
}

void UpdateTerrainTilePulseState(
    TerrainTilePulseState& pulse_state, MinimapTerrainLayer& layer) {
    for (TerrainTilePulseSlot& slot : pulse_state.slots) {
        if (slot.timer == 0) {
            continue;
        }
        --slot.timer;
        SetTerrainFlagA(layer, slot.tile_x, slot.tile_y, (slot.timer & 4) != 0);
    }
}

void StartTerrainTilePulse(TerrainTilePulseState& pulse_state,
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y) {
    if (!in_minimap_layer_bounds(layer, tile_x, tile_y)) {
        return;
    }

    TerrainTilePulseSlot* free_slot = nullptr;
    for (TerrainTilePulseSlot& slot : pulse_state.slots) {
        if (slot.timer == 0) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) {
        return;
    }

    if (((terrain_flags_at(layer, tile_x, tile_y) >> 0x0b) & 1u) != 0) {
        --tile_x;
    }
    free_slot->timer = 0x0f;
    free_slot->tile_y = tile_y;
    free_slot->tile_x = tile_x;
}

void ConvertTerrainTileSheetPixelsForSurface(
    TerrainTileSheetState& tile_sheet, const MinimapPixelFormat& pixel_format) {
    if (!pixel_format.pixel_mode_555) {
        return;
    }

    const u16 red_mask = pixel_format.red_mask == 0xf800u ?
        0x7c00u : static_cast<u16>(pixel_format.red_mask);
    const u16 green_mask = pixel_format.green_mask == 0x07e0u ?
        0x03e0u : static_cast<u16>(pixel_format.green_mask);
    const u16 blue_mask = static_cast<u16>(pixel_format.blue_mask);
    const u16 red_green_mask = static_cast<u16>(red_mask | green_mask);
    for (u16& pixel : tile_sheet.tile_pixels) {
        pixel = static_cast<u16>(((pixel >> 1) & red_green_mask) | (pixel & blue_mask));
    }
}

bool BuildTerrainTileAverageColors(
    TerrainTileSheetState& tile_sheet, const MinimapPixelFormat& pixel_format) {
    if (tile_sheet.tile_count == 0 ||
        tile_sheet.tile_pixels.size() <
            static_cast<std::size_t>(tile_sheet.tile_count) * kTerrainTilePixelsPerTile) {
        return false;
    }

    tile_sheet.average_pixels.assign(tile_sheet.tile_count, 0);
    const u16 transparent_green = static_cast<u16>(pixel_format.green_mask);
    const u16 transparent_magenta =
        static_cast<u16>(pixel_format.red_mask | pixel_format.blue_mask);
    const u32 sample_step = kTerrainTilePixelsPerTile / 0x40;

    for (u32 tile = 0; tile < tile_sheet.tile_count; ++tile) {
        const std::size_t tile_base =
            static_cast<std::size_t>(tile) * kTerrainTilePixelsPerTile;
        i32 red = 0;
        i32 green = 0;
        i32 blue = 0;
        i32 samples = 0;
        for (u32 pixel_index = 0; pixel_index < kTerrainTilePixelsPerTile;
             pixel_index += sample_step) {
            const u16 pixel = tile_sheet.tile_pixels[tile_base + pixel_index];
            if (pixel == transparent_green || pixel == transparent_magenta) {
                continue;
            }
            red += static_cast<i32>(
                pixel_component(pixel, pixel_format.red_mask, pixel_format.red_shift));
            green += static_cast<i32>(
                pixel_component(pixel, pixel_format.green_mask, pixel_format.green_shift));
            blue += static_cast<i32>(
                pixel_component(pixel, pixel_format.blue_mask, pixel_format.blue_shift));
            ++samples;
        }
        if (samples != 0) {
            red /= samples;
            green /= samples;
            blue /= samples;
        }
        tile_sheet.average_pixels[tile] =
            pack_pixel_components(pixel_format, static_cast<u32>(red),
                static_cast<u32>(green), static_cast<u32>(blue));
    }
    return true;
}

bool LoadTerrainDecorationResources(
    TerrainTileSheetState& tile_sheet, const char* archive_name, i32 bank_index) {
    if (archive_name == nullptr || bank_index < 0) {
        return false;
    }

    if (tile_sheet.resource_start != kInvalidTerrainTileResourceIndex) {
        ReleaseResourceEntriesFrom(tile_sheet.resource_start);
    }
    tile_sheet.resource_start = kInvalidTerrainTileResourceIndex;
    tile_sheet.resource_count = 0;
    if (tile_sheet.palette_start != kInvalidTerrainTileResourceIndex) {
        ReleasePaletteCacheSlotsFrom(tile_sheet.palette_start);
    }
    tile_sheet.palette_start = kInvalidTerrainTileResourceIndex;
    tile_sheet.palette_count = 0;

    const u32 first_record =
        static_cast<u32>(bank_index) * kTerrainTileBankRecordStride + 1;
    for (u32 record = 0; record < kTerrainTileDecorationRecordCount; ++record) {
        TrcRecordReader reader;
        if (!OpenTrcRecordDirectoryEntry(reader, archive_name, first_record + record) ||
            !OpenTrcRecordPayload(reader)) {
            CloseTrcRecordReader(reader);
            return false;
        }
        auto fail_record = [&reader]() {
            CloseTrcRecordReader(reader);
            return false;
        };

        if (!ReadOpenTrcRecordBytes(reader,
                tile_sheet.decoration_headers[record].data(),
                kTerrainTileDecorationHeaderBytes)) {
            return fail_record();
        }
        u32 active_palette_slot = kInvalidPaletteCacheSlot;
        u32 section_type = 0;
        while (section_type != 0x0a) {
            std::array<u8, 8> section_header{};
            if (!ReadOpenTrcRecordBytes(reader, section_header.data(),
                    section_header.size())) {
                return fail_record();
            }
            section_type = read_le_u32(section_header.data());
            const u32 section_bytes = read_le_u32(section_header.data() + 4);

            if (section_type == 0) {
                std::array<u8, kPaletteRawBytesPerSlot> palette{};
                if (!ReadOpenTrcRecordBytes(reader, palette.data(), palette.size())) {
                    return fail_record();
                }
                const u32 slot = AllocatePaletteCacheSlot();
                if (slot == kInvalidPaletteCacheSlot ||
                    !SetPaletteCacheRawSlot(
                        slot, palette.data(), palette.size())) {
                    return fail_record();
                }
                ConvertPaletteCacheSlot(slot);
                if (tile_sheet.palette_start == kInvalidTerrainTileResourceIndex) {
                    tile_sheet.palette_start = slot;
                }
                ++tile_sheet.palette_count;
                active_palette_slot = slot;
            } else if (section_type == 1) {
                std::array<u8, 0x20> header{};
                if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size())) {
                    return fail_record();
                }
                const u32 payload_size = read_le_u32(header.data() + 0x18);
                if (section_bytes < header.size()) {
                    return fail_record();
                }

                u32 entry_index = kInvalidResourceEntry;
                void* resource_payload = nullptr;
                if (!AllocateResourceEntry(payload_size, &entry_index, &resource_payload) ||
                    (payload_size != 0 && resource_payload == nullptr)) {
                    return fail_record();
                }
                if (!ReadOpenTrcRecordBytes(reader, resource_payload, payload_size)) {
                    return fail_record();
                }
                if (!ConfigureResourceEntry(
                        entry_index, read_resource_metadata(header.data()),
                        active_palette_slot)) {
                    return fail_record();
                }
                if (tile_sheet.resource_start == kInvalidTerrainTileResourceIndex) {
                    tile_sheet.resource_start = entry_index;
                }
                ++tile_sheet.resource_count;
            } else if (section_type != 0x0a) {
                return fail_record();
            }
        }
        CloseTrcRecordReader(reader);
    }
    return true;
}

void ReleaseTerrainTileSheetBankResources(TerrainTileSheetState& tile_sheet) {
    tile_sheet.tile_pixels.clear();
    tile_sheet.tile_pixels.shrink_to_fit();
    tile_sheet.average_pixels.clear();
    tile_sheet.average_pixels.shrink_to_fit();
    tile_sheet.tile_count = 0;

    if (tile_sheet.resource_start != kInvalidTerrainTileResourceIndex) {
        ReleaseResourceEntriesFrom(tile_sheet.resource_start);
        tile_sheet.resource_start = kInvalidTerrainTileResourceIndex;
    }
    tile_sheet.resource_count = 0;

    if (tile_sheet.palette_start != kInvalidTerrainTileResourceIndex) {
        ReleasePaletteCacheSlotsFrom(tile_sheet.palette_start);
        tile_sheet.palette_start = kInvalidTerrainTileResourceIndex;
    }
    tile_sheet.palette_count = 0;
    tile_sheet.active_bank = -1;
}

bool LoadTerrainTileSheetBank(TerrainTileSheetState& tile_sheet,
    const char* archive_name, i32 bank_index, const MinimapPixelFormat& pixel_format,
    u32 color_depth_bits) {
    if (archive_name == nullptr || bank_index < 0) {
        return false;
    }

    if (tile_sheet.active_bank == bank_index) {
        if (tile_sheet.resource_start != kInvalidTerrainTileResourceIndex) {
            ReleaseResourceEntriesFrom(tile_sheet.resource_start + tile_sheet.resource_count);
        }
        if (tile_sheet.palette_start != kInvalidTerrainTileResourceIndex) {
            ReleasePaletteCacheSlotsFrom(tile_sheet.palette_start + tile_sheet.palette_count);
        }
        return true;
    }

    std::vector<u8> payload;
    const u32 terrain_record =
        static_cast<u32>(bank_index) * kTerrainTileBankRecordStride;
    if (!load_trc_record_payload_streamed(archive_name, terrain_record, payload)) {
        return false;
    }
    const u32 bytes_per_pixel = color_depth_bits >> 3;
    const u32 bytes_per_tile = bytes_per_pixel * kTerrainTilePixelsPerTile;
    if (bytes_per_pixel != sizeof(u16) || bytes_per_tile == 0 ||
        payload.size() % bytes_per_tile != 0) {
        return false;
    }

    tile_sheet.tile_count = static_cast<u32>(payload.size() / bytes_per_tile);
    tile_sheet.tile_pixels.resize(payload.size() / sizeof(u16));
    for (std::size_t i = 0; i < tile_sheet.tile_pixels.size(); ++i) {
        tile_sheet.tile_pixels[i] = read_le_u16(payload.data() + i * sizeof(u16));
    }

    ConvertTerrainTileSheetPixelsForSurface(tile_sheet, pixel_format);
    if (!BuildTerrainTileAverageColors(tile_sheet, pixel_format) ||
        !LoadTerrainDecorationResources(tile_sheet, archive_name, bank_index)) {
        return false;
    }
    tile_sheet.active_bank = bank_index;
    return true;
}

void RenderMinimapTerrainBase(MinimapRenderState& state,
    const MinimapTerrainLayer& layer, MinimapTerrainRenderConfig& config) {
    const u32 map_width = layer.width_tiles != 0 ? layer.width_tiles : state.map_width_tiles;
    const u32 map_height =
        layer.height_tiles != 0 ? layer.height_tiles : state.map_height_tiles;
    if (map_width == 0 || map_height == 0 || state.minimap_width_pixels == 0 ||
        state.minimap_height_pixels == 0 || !ensure_minimap_scratch(state)) {
        return;
    }

    state.map_width_tiles = map_width;
    state.map_height_tiles = map_height;
    const u32 x_scale = (state.minimap_width_pixels * 100u) / map_width;
    const u32 y_scale = (state.minimap_height_pixels * 100u) / map_height;
    state.scale_percent = std::min({x_scale, y_scale, 100u});
    state.scaled_map_width_pixels = (map_width * state.scale_percent) / 100u;
    state.scaled_map_height_pixels = (map_height * state.scale_percent) / 100u;
    state.inset_x = state.scaled_map_width_pixels < state.minimap_width_pixels ?
        static_cast<i32>((state.minimap_width_pixels - state.scaled_map_width_pixels) / 2) :
        0;
    state.inset_y = state.scaled_map_height_pixels < state.minimap_height_pixels ?
        static_cast<i32>((state.minimap_height_pixels - state.scaled_map_height_pixels) / 2) :
        0;

    ensure_minimap_brightness_noise(config);

    constexpr std::array<u32, 6> kTerrainCodeShifts{0, 6, 0x0c, 3, 9, 0x0f};
    for (u32 y = 0; y < state.minimap_height_pixels; ++y) {
        for (u32 x = 0; x < state.minimap_width_pixels; ++x) {
            const bool inside_scaled_map =
                static_cast<i32>(x) >= state.inset_x &&
                static_cast<i32>(y) >= state.inset_y &&
                x < static_cast<u32>(state.inset_x) + state.scaled_map_width_pixels &&
                y < static_cast<u32>(state.inset_y) + state.scaled_map_height_pixels;
            if (!inside_scaled_map) {
                put_minimap_scratch_pixel(state, static_cast<i32>(x), static_cast<i32>(y), 0);
                continue;
            }

            const i32 tile_x =
                original_world_to_tile(MinimapScreenToWorldX(state, static_cast<i32>(x)));
            const i32 tile_y =
                original_world_to_tile(MinimapScreenToWorldY(state, static_cast<i32>(y)));
            const u32 terrain_flags = terrain_flags_at(layer, tile_x, tile_y);
            const i32 pattern_index = ResolveMinimapTilePatternIndex(tile_x, tile_y);

            u32 red = 0;
            u32 green = 0;
            u32 blue = 0;
            for (u32 shift : kTerrainCodeShifts) {
                const u32 terrain_code = (terrain_flags >> shift) & 7u;
                const i32 palette_index =
                    pattern_index + ResolveMinimapTerrainPaletteOffset(config, terrain_code);
                add_pixel_components(config.pixel_format,
                    minimap_palette_pixel(config, palette_index), 1, red, green, blue);
            }

            if (GetMinimapOverlayId(layer, tile_x, tile_y, 0) != 0) {
                const i32 overlay_index =
                    ResolveMinimapOverlayPaletteIndex(layer, config, tile_x, tile_y, 0);
                add_pixel_components(config.pixel_format,
                    minimap_palette_pixel(config, overlay_index), 8, red, green, blue);
                red >>= 4;
                green >>= 4;
                blue >>= 4;
            } else {
                red >>= 3;
                green >>= 3;
                blue >>= 3;
            }

            const u32 height_shade_code = (terrain_flags >> 0x1a) & 7u;
            const u32 shade = height_shade_code == 0 ? 0 : height_shade_code * 2 + 2;
            red += shade;
            green += shade * (config.pixel_format.pixel_mode_555 ? 1u : 2u);
            blue += shade;

            const u32 noise = minimap_noise_at(config, x, y);
            red = (red * noise) >> 3;
            green = (green * noise) >> 3;
            blue = (blue * noise) >> 3;
            put_minimap_scratch_pixel(state, static_cast<i32>(x), static_cast<i32>(y),
                pack_pixel_components(config.pixel_format, red, green, blue));
        }
    }
}

void ApplyMinimapUnitAndOverlayMarkers(MinimapRenderState& state,
    const std::vector<UnitMovementUnit*>& active_units,
    const std::vector<MinimapOwnerMarkerColors>& owner_colors,
    const MinimapOverlayLayer& overlay_layer) {
    if (!ensure_minimap_scratch(state)) {
        return;
    }

    for (const UnitMovementUnit* unit : active_units) {
        if (unit == nullptr || !unit->active) {
            continue;
        }

        const MinimapOwnerMarkerColors colors =
            owner_marker_colors(owner_colors, unit->owner_id);
        if (unit->type_id < 0x60) {
            const i32 x = MinimapWorldToScreenX(state, unit->x);
            const i32 y = MinimapWorldToScreenY(state, unit->y);
            put_minimap_marker_2x2(state, x, y, colors.unit_color);
            continue;
        }

        for (u32 tile_y = 0; tile_y < unit->definition.footprint_height_tiles; ++tile_y) {
            for (u32 tile_x = 0; tile_x < unit->definition.footprint_width_tiles; ++tile_x) {
                const i32 x = MinimapWorldToScreenX(
                    state, unit->x + static_cast<i32>(tile_x * 0x20));
                const i32 y = MinimapWorldToScreenY(
                    state, unit->y + static_cast<i32>(tile_y * 0x20));
                put_minimap_scratch_pixel(state, x, y, colors.footprint_color);
            }
        }
    }

    const u32 width = overlay_layer.width_tiles != 0 ?
        overlay_layer.width_tiles : state.map_width_tiles;
    const u32 height = overlay_layer.height_tiles != 0 ?
        overlay_layer.height_tiles : state.map_height_tiles;
    const u32 stride = overlay_stride(overlay_layer);
    if (stride == 0) {
        return;
    }

    for (u32 tile_y = 0; tile_y < height; ++tile_y) {
        for (u32 tile_x = 0; tile_x < width; ++tile_x) {
            const std::size_t index =
                static_cast<std::size_t>(tile_y) * stride + tile_x;
            if (index >= overlay_layer.tile_flags.size()) {
                continue;
            }
            if (((overlay_layer.tile_flags[index] >> 8) & 7u) != 1u) {
                continue;
            }
            const i32 x = MinimapWorldToScreenX(state, static_cast<i32>(tile_x << 5));
            const i32 y = MinimapWorldToScreenY(state, static_cast<i32>(tile_y << 5));
            put_minimap_marker_2x2(state, x, y, 0x1f);
        }
    }
}

void CopyMinimapScratchToOutput(MinimapRenderState& state) {
    if (state.output_pitch_pixels == 0 || state.minimap_width_pixels == 0 ||
        state.minimap_height_pixels == 0) {
        return;
    }
    const std::size_t required_output =
        static_cast<std::size_t>(state.output_pitch_pixels) *
        std::max<u32>(state.output_height_pixels,
            static_cast<u32>(state.output_y) + state.minimap_height_pixels);
    if (state.output_pixels.size() < required_output) {
        state.output_pixels.resize(required_output);
    }

    const std::size_t required_scratch =
        static_cast<std::size_t>(kMinimapScratchPitchPixels) *
        state.minimap_height_pixels;
    if (state.scratch_pixels.size() < required_scratch) {
        return;
    }

    for (u32 y = 0; y < state.minimap_height_pixels; ++y) {
        const std::size_t src =
            static_cast<std::size_t>(y) * kMinimapScratchPitchPixels;
        const std::size_t dst =
            static_cast<std::size_t>(state.output_y + static_cast<i32>(y)) *
                state.output_pitch_pixels +
            static_cast<std::size_t>(state.output_x);
        std::copy_n(state.scratch_pixels.begin() + static_cast<std::ptrdiff_t>(src),
            state.minimap_width_pixels,
            state.output_pixels.begin() + static_cast<std::ptrdiff_t>(dst));
    }
}

void DrawMinimapViewportBorder(MinimapRenderState& state, i32 world_x, i32 world_y) {
    if (state.output_pitch_pixels == 0 || state.map_width_tiles == 0 ||
        state.map_height_tiles == 0) {
        return;
    }

    const i32 left = state.output_x + MinimapWorldToScreenX(state, world_x);
    const i32 top = state.output_y + MinimapWorldToScreenY(state, world_y);
    const u32 width = static_cast<u32>(
        (static_cast<u64>((state.viewport_width_pixels + 0x1f) >> 5) *
            state.minimap_width_pixels) /
        state.map_width_tiles);
    const u32 height = static_cast<u32>(
        (static_cast<u64>((state.viewport_height_pixels + 0x1f) >> 5) *
            state.minimap_height_pixels) /
        state.map_height_tiles);
    if (width == 0 || height == 0) {
        return;
    }

    const u32 required_height =
        std::max<u32>(state.output_height_pixels, static_cast<u32>(top + height + 1));
    const std::size_t required_output =
        static_cast<std::size_t>(state.output_pitch_pixels) * required_height;
    if (state.output_pixels.size() < required_output) {
        state.output_pixels.resize(required_output);
    }

    auto set_pixel = [&](i32 x, i32 y) {
        if (x < 0 || y < 0 ||
            static_cast<u32>(x) >= state.output_pitch_pixels ||
            static_cast<u32>(y) >= required_height) {
            return;
        }
        state.output_pixels[static_cast<std::size_t>(y) * state.output_pitch_pixels +
            static_cast<std::size_t>(x)] = 0xffff;
    };

    for (u32 x = 0; x < width; ++x) {
        set_pixel(left + static_cast<i32>(x), top);
        set_pixel(left + static_cast<i32>(x), top + static_cast<i32>(height) - 1);
    }
    for (u32 y = 0; y < height; ++y) {
        set_pixel(left, top + static_cast<i32>(y));
        set_pixel(left + static_cast<i32>(width) - 1, top + static_cast<i32>(y));
    }
}

} // namespace ranker
