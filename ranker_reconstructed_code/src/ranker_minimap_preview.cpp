#include "ranker_minimap_preview.h"

#include "ranker_display_constants.h"
#include "ranker_gameplay_session_format.h"
#include "ranker_gameplay_terrain_layout.h"
#include "ranker_map_brush.h"
#include "ranker_trc.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ranker {
namespace {

u32 ReadLittleEndianU32Or(const std::vector<u8>& bytes,
    std::size_t offset, u32 fallback = 0) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(u32)) {
        return fallback;
    }
    return static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

u32 ReadMapLayerCell(const std::vector<u8>& bytes, u32 cell_index) {
    return ReadLittleEndianU32Or(
        bytes, static_cast<std::size_t>(cell_index) * sizeof(u32));
}

} // namespace

bool RenderGameplaySessionMinimapPreview(const char* archive_path,
    u32 preview_width, u32 preview_height, std::vector<u16>& pixels) {
    pixels.clear();
    if (archive_path == nullptr || archive_path[0] == '\0' ||
        preview_width == 0 || preview_height == 0 ||
        preview_width > kMinimapScratchPitchPixels) {
        return false;
    }

    std::vector<u8> map_record;
    std::vector<u8> terrain_record;
    std::vector<u8> overlay_record;
    if (!LoadTrcRecordAlloc(
            archive_path, kGameplaySessionMapRecordIndex, map_record) ||
        !LoadTrcRecordAlloc(
            archive_path, kGameplayMapSourceLayerRecordIndex, terrain_record) ||
        !LoadTrcRecordAlloc(
            archive_path, kGameplayMapOverlayLayerRecordIndex, overlay_record)) {
        return false;
    }

    const u32 terrain_bank = ReadLittleEndianU32Or(
        map_record, kSessionMapRecordTerrainBankOffset);
    const u32 map_width = std::min<u32>(
        ReadLittleEndianU32Or(
            map_record, kSessionMapRecordWidthTilesOffset),
        kGameplayScenarioMapLayerStrideTiles);
    const u32 map_height = std::min<u32>(
        ReadLittleEndianU32Or(
            map_record, kSessionMapRecordHeightTilesOffset),
        kGameplayScenarioMapLayerStrideTiles);
    if (map_width == 0 || map_height == 0) {
        return false;
    }

    const std::size_t layer_cells =
        static_cast<std::size_t>(kGameplayScenarioMapLayerStrideTiles) *
        map_height;
    const std::size_t layer_bytes = layer_cells * sizeof(u32);
    if (terrain_record.size() < layer_bytes ||
        overlay_record.size() < layer_bytes) {
        return false;
    }

    MinimapTerrainLayer layer{};
    layer.width_tiles = map_width;
    layer.height_tiles = map_height;
    layer.stride_tiles = kGameplayScenarioMapLayerStrideTiles;
    layer.terrain_flags.resize(layer_cells);
    layer.overlay_flags.resize(layer_cells);
    for (u32 y = 0; y < map_height; ++y) {
        for (u32 x = 0; x < map_width; ++x) {
            const u32 cell =
                y * kGameplayScenarioMapLayerStrideTiles + x;
            layer.terrain_flags[cell] = ReadMapLayerCell(terrain_record, cell);
            layer.overlay_flags[cell] = ReadMapLayerCell(overlay_record, cell);
        }
    }

    MinimapPixelFormat pixel_format{};
    std::vector<u16> average_pixels;
    if (!LoadTerrainTileAveragePalette("JW2_03.TRC",
            static_cast<i32>(terrain_bank), pixel_format,
            kOriginalColorDepth, average_pixels)) {
        return false;
    }

    MinimapTerrainRenderConfig config{};
    ConfigureGameplayMinimapTerrainLayout(config);
    config.pixel_format = pixel_format;
    config.animated_terrain_bank = 0;
    config.minimap_tile_palette = std::move(average_pixels);
    for (u32 y = 0; y < kMinimapNoiseTableWidth; ++y) {
        for (u32 x = 0; x < kMinimapNoiseTableWidth; ++x) {
            // Lobby previews must not advance the process-wide C RNG before
            // the synchronized gameplay RNG is initialized.
            const u32 mixed = x * 0x45d9f3bu ^ y * 0x119de1f3u ^
                (x + y) * 0x27d4eb2du;
            config.brightness_noise[
                static_cast<std::size_t>(y) * kMinimapNoiseTableWidth + x] =
                static_cast<u8>(6u + mixed % 3u);
        }
    }
    config.brightness_noise_initialized = true;

    MinimapRenderState preview{};
    preview.minimap_width_pixels = preview_width;
    preview.minimap_height_pixels = preview_height;
    // Gameplay keeps the original 100% cap, but the lobby owns a dedicated
    // preview frame.  Small maps should grow to the largest contained size in
    // that frame instead of remaining as a small native-pixel island.
    preview.allow_terrain_upscale = true;
    preview.output_pitch_pixels = preview_width;
    preview.output_width_pixels = preview_width;
    preview.output_height_pixels = preview_height;
    RenderMinimapTerrainBase(preview, layer, config);
    CopyMinimapScratchToOutput(preview);
    if (preview.output_pixels.size() <
        static_cast<std::size_t>(preview_width) * preview_height) {
        return false;
    }

    pixels = std::move(preview.output_pixels);
    return true;
}

} // namespace ranker
