#pragma once

#include "ranker_types.h"

#include <array>
#include <vector>

namespace ranker {

struct UnitMovementUnit;
struct UnitMovementMap;
struct TerrainTileSheetState;

constexpr u32 kMapBrushViewportGridWidth = 0x28;
constexpr u32 kMapBrushViewportGridCells =
    kMapBrushViewportGridWidth * kMapBrushViewportGridWidth;
constexpr u32 kMapBrushRecordBytes = 0x110400;
constexpr u32 kMapHillBrushRecordBytes = 0x8820;
constexpr u32 kMinimapScratchPitchPixels = 0x100;
constexpr u32 kMinimapNoiseTableWidth = 0x100;
constexpr u32 kMinimapNoiseTableCells =
    kMinimapNoiseTableWidth * kMinimapNoiseTableWidth;
constexpr u32 kMinimapOverlayIdCount = 0x40;
constexpr u32 kTerrainTilePulseSlotCount = 4;
constexpr u32 kTerrainTilePixelsPerTile = 0x400;
constexpr u32 kTerrainTileDecorationRecordCount = 0x0e;
constexpr u32 kTerrainTileDecorationHeaderBytes = 0x394;
constexpr u32 kTerrainTileBankRecordStride = 0x0f;
constexpr u32 kInvalidTerrainTileResourceIndex = 0xffffffffu;

constexpr u32 ResolveMinimapTerrainScalePercent(u32 minimap_width,
    u32 minimap_height, u32 map_width, u32 map_height, bool allow_upscale) {
    if (minimap_width == 0 || minimap_height == 0 ||
        map_width == 0 || map_height == 0) {
        return 0;
    }
    const u32 x_scale = (minimap_width * 100u) / map_width;
    const u32 y_scale = (minimap_height * 100u) / map_height;
    const u32 contained_scale = x_scale < y_scale ? x_scale : y_scale;
    return allow_upscale || contained_scale < 100u ? contained_scale : 100u;
}

enum class MapBrushTileClass : u8 {
    clear = 0,
    edge = 1,
    filled = 2,
};

enum class TerrainTileDrawKind : u8 {
    uniform = 0,
    blended = 1,
    overlay = 2,
};

enum class TerrainOverlayDrawMode : u8 {
    none = 0,
    dispatch = 1,
    masked = 2,
    transparent = 3,
    blended_565 = 4,
    blended_555 = 5,
};

enum class TerrainDecorationRenderKind : u8 {
    terrain_type1 = 0,
    terrain_type3 = 1,
    brush_edge = 2,
};

struct MapBrushDrawCommand {
    i32 screen_x = 0;
    i32 screen_y = 0;
    u32 mask = 0;
};

struct TerrainTileClipRect {
    i32 dst_x = 0;
    i32 dst_y = 0;
    i32 src_x = 0;
    i32 src_y = 0;
    i32 width = 0x20;
    i32 height = 0x20;
};

struct TerrainTileDrawCommand {
    TerrainTileDrawKind kind = TerrainTileDrawKind::uniform;
    i32 screen_x = 0;
    i32 screen_y = 0;
    i32 tile_x = 0;
    i32 tile_y = 0;
    u32 terrain_flags = 0;
    u32 overlay_flags = 0;
    u32 overlay_slot = 0xffffffffu;
    TerrainOverlayDrawMode overlay_mode = TerrainOverlayDrawMode::none;
    i32 overlay_palette_index = -1;
    i32 overlay_mask_palette_index = -1;
    TerrainTileClipRect clip{};
};

struct TerrainViewportRenderState {
    u32 target_width_pixels = 0;
    u32 target_height_pixels = 0;
    u32 target_pitch_pixels = 0;
    std::vector<u16> pixels;
    std::vector<TerrainTileDrawCommand> draw_commands;
};

struct TerrainDecorationLayer {
    u32 width_tiles = 0x100;
    u32 height_tiles = 0x100;
    u32 stride_tiles = 0x100;
    std::vector<u32> tile_flags;
};

struct TerrainDecorationResourceTable {
    std::vector<i32> resource_base_by_id;
};

struct TerrainDecorationRenderCommand {
    TerrainDecorationRenderKind kind = TerrainDecorationRenderKind::terrain_type1;
    u32 render_layer = 0;
    u32 sort_key = 0;
    i32 screen_x = 0;
    i32 screen_y = 0;
    i32 tile_x = 0;
    i32 tile_y = 0;
    i32 resource_index = -1;
    u32 flags = 0;
};

struct TerrainDecorationRenderState {
    i32 visible_left_world = 0;
    i32 visible_top_world = 0;
    i32 visible_right_world = 0;
    i32 visible_bottom_world = 0;
    i32 camera_x = 0;
    i32 camera_y = 0;
    i32 terrain_type1_resource_base = 0;
    std::vector<TerrainDecorationRenderCommand> render_commands;
};

struct MapBrushViewportState {
    i32 camera_x = 0;
    i32 camera_y = 0;
    u32 viewport_width_pixels = 0;
    u32 viewport_height_pixels = 0;
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
    std::vector<u32> tile_flags;
    std::array<u8, kMapBrushViewportGridCells> tile_classes{};
    i32 viewport_mod_x = 0;
    i32 viewport_mod_y = 0;
    std::vector<MapBrushDrawCommand> draw_commands;
};

struct MapBrushArchiveState {
    std::vector<u8> brush_record;
    std::vector<u8> hill_brush_record;
    std::vector<u8> terrain_layer_a;
    std::vector<u8> terrain_layer_b;
    std::vector<u8> terrain_layer_c;
    std::vector<u8> minimap_record_153;
    std::vector<u8> minimap_record_154;
    std::array<u16, 8> minimap_palette{};
};

struct MinimapRenderState {
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
    u32 output_pitch_pixels = 0;
    u32 output_width_pixels = 0;
    u32 output_height_pixels = 0;
    i32 output_x = 0;
    i32 output_y = 0;
    u32 minimap_width_pixels = 0;
    u32 minimap_height_pixels = 0;
    bool allow_terrain_upscale = false;
    u32 scale_percent = 100;
    u32 scaled_map_width_pixels = 0;
    u32 scaled_map_height_pixels = 0;
    i32 inset_x = 0;
    i32 inset_y = 0;
    u32 viewport_width_pixels = 0;
    u32 viewport_height_pixels = 0;
    std::vector<u16> scratch_pixels;
    std::vector<u16> output_pixels;
};

struct MinimapOwnerMarkerColors {
    u16 unit_color = 0;
    u16 footprint_color = 0;
};

struct MinimapOverlayLayer {
    u32 width_tiles = 0;
    u32 height_tiles = 0;
    u32 stride_tiles = 0;
    std::vector<u32> tile_flags;
};

struct MinimapTerrainLayer {
    u32 width_tiles = 0;
    u32 height_tiles = 0;
    u32 stride_tiles = 0;
    std::vector<u32> terrain_flags;
    std::vector<u32> overlay_flags;
};

struct TerrainTilePulseSlot {
    i32 tile_x = 0;
    i32 tile_y = 0;
    i32 timer = 0;
};

struct TerrainTilePulseState {
    std::array<TerrainTilePulseSlot, kTerrainTilePulseSlotCount> slots{};
};

struct TerrainTileSheetState {
    i32 active_bank = -1;
    u32 tile_count = 0;
    std::vector<u16> tile_pixels;
    std::vector<u16> average_pixels;
    std::array<std::array<u8, kTerrainTileDecorationHeaderBytes>,
        kTerrainTileDecorationRecordCount> decoration_headers{};
    u32 resource_start = kInvalidTerrainTileResourceIndex;
    u32 resource_count = 0;
    u64 resource_tail_allocation_serial = 0;
    u32 palette_start = kInvalidTerrainTileResourceIndex;
    u32 palette_count = 0;
    u64 palette_tail_allocation_serial = 0;
};

struct MinimapPixelFormat {
    u32 red_mask = 0xf800;
    u32 green_mask = 0x07e0;
    u32 blue_mask = 0x001f;
    u32 red_shift = 11;
    u32 green_shift = 5;
    u32 blue_shift = 0;
    bool pixel_mode_555 = false;
};

struct MinimapTerrainRenderConfig {
    MinimapPixelFormat pixel_format{};
    std::array<i32, 8> terrain_palette_offsets{};
    u32 animated_terrain_bank = 0;
    i32 overlay_palette_base_offset = 0;
    std::array<std::array<i32, 4>, kMinimapOverlayIdCount> overlay_variant_offsets{};
    i32 terrain_blend_mask_base_offset = 0;
    std::array<std::array<i32, 4>, kMinimapOverlayIdCount> terrain_blend_mask_offsets{};
    i32 overlay_blend_mask_base_offset = 0;
    std::array<i32, kMinimapOverlayIdCount> overlay_blend_mask_offsets{};
    i32 overlay_height_base = 0;
    std::array<u8, kMinimapOverlayIdCount> overlay_sets_terrain_flag_a{};
    std::array<u8, kMinimapOverlayIdCount> overlay_sets_terrain_flag_c{};
    std::array<i32, kMinimapOverlayIdCount> overlay_height_offsets{};
    std::array<u8, kMinimapNoiseTableCells> brightness_noise{};
    bool brightness_noise_initialized = false;
    std::vector<u16> minimap_tile_palette;
};

MapBrushTileClass ClassifyMapBrushTile(
    const MapBrushViewportState& state, i32 tile_x, i32 tile_y);
void BuildViewportBrushClassification(MapBrushViewportState& state, i32 camera_x,
    i32 camera_y);
u32 ResolveViewportBrushMask(
    const MapBrushViewportState& state, i32 grid_x, i32 grid_y);
void BuildViewportBrushDrawCommands(MapBrushViewportState& state);

bool TerrainTileUsesUniformBase(u32 terrain_flags);
bool ClipTerrainTileDrawRect(
    TerrainTileClipRect& rect, u32 target_width_pixels, u32 target_height_pixels);
void AppendUniformTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y);
void AppendBlendedTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y);
void AppendOverlayTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y);
void AppendMaskedOverlayTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, i32 overlay_palette_index = -1);
void AppendTransparentOverlayTerrainTileDrawCommand(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, u32 overlay_slot);
void AppendBlendedOverlayTerrainTileDrawCommand565(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, u32 overlay_slot,
    i32 overlay_palette_index = -1, i32 mask_palette_index = -1);
void AppendBlendedOverlayTerrainTileDrawCommand555(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 screen_x, i32 screen_y,
    i32 tile_x, i32 tile_y, u32 overlay_slot,
    i32 overlay_palette_index = -1, i32 mask_palette_index = -1);
void BuildTerrainTileDrawCommands(TerrainViewportRenderState& state,
    const MinimapTerrainLayer& layer, i32 camera_x, i32 camera_y);
void BuildVisibleTerrainDecorationRenderCommands(TerrainDecorationRenderState& state,
    const TerrainDecorationLayer& terrain_decoration_layer,
    const TerrainDecorationLayer& brush_layer,
    const TerrainDecorationResourceTable& brush_resources,
    const TerrainTileSheetState& tile_sheet);

bool LoadMinimapBrushRecords(MapBrushArchiveState& state,
    const char* archive_name = "JW2_02.TRC", bool high_color_mode = false);
bool DecodeMapBrushArchivePayload(MapBrushArchiveState& state,
    const std::vector<u8>& payload);
bool SaveMapBrushRecord(const char* archive_name, const MapBrushArchiveState& state);
bool LoadMapBrushRecord(MapBrushArchiveState& state,
    const char* archive_name, u32 record_index);
bool SaveMapHillBrushRecord(const char* archive_name, const MapBrushArchiveState& state);
bool LoadMapHillBrushRecord(MapBrushArchiveState& state,
    const char* archive_name, u32 record_index);

i32 MinimapScreenToWorldX(const MinimapRenderState& state, i32 screen_x);
i32 MinimapScreenToWorldY(const MinimapRenderState& state, i32 screen_y);
i32 MinimapWorldToScreenX(const MinimapRenderState& state, i32 world_x);
i32 MinimapWorldToScreenY(const MinimapRenderState& state, i32 world_y);
i32 ResolveMinimapTilePatternIndex(i32 tile_x, i32 tile_y);
i32 ResolveMinimapTerrainPaletteOffset(
    const MinimapTerrainRenderConfig& config, u32 terrain_code);
u32 GetMinimapOverlayId(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot);
u32 GetMinimapOverlayVariant(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot);
i32 ResolveMinimapOverlayPaletteIndex(
    const MinimapTerrainLayer& layer, const MinimapTerrainRenderConfig& config,
    i32 tile_x, i32 tile_y, u32 slot);
i32 ResolveTerrainBlendMaskPaletteIndex(
    const MinimapTerrainLayer& layer, const MinimapTerrainRenderConfig& config,
    i32 tile_x, i32 tile_y);
i32 ResolveMinimapOverlayBlendMaskPaletteIndex(
    const MinimapTerrainLayer& layer, const MinimapTerrainRenderConfig& config,
    i32 tile_x, i32 tile_y, u32 slot);
bool CheckMinimapOverlaySlotOccupied(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot);
bool IsMinimapTerrainTileInBounds(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y);
void SetTerrainBlendMaskFields(MinimapTerrainLayer& layer,
    i32 tile_x, i32 tile_y, u32 mask_id, u32 variant);
void SetMinimapOverlaySlot(MinimapTerrainLayer& layer,
    const MinimapTerrainRenderConfig& config, i32 tile_x, i32 tile_y,
    u32 slot, u32 overlay_id, u32 variant);
void SetTerrainUpperCornerCode(MinimapTerrainLayer& layer,
    i32 tile_x, i32 tile_y, u32 terrain_code, u32 corner);
void SetTerrainLowerCornerCode(MinimapTerrainLayer& layer,
    i32 tile_x, i32 tile_y, u32 terrain_code, u32 corner);
void SetTerrainCodeAndRefreshFlags(MinimapTerrainLayer& layer,
    i32 upper_half, i32 tile_x, i32 tile_y, u32 terrain_code, u32 corner);
void ClearTerrainUpperCornerCode(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 corner);
void ClearTerrainUpperCornerCodes01(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y);
void ClearMinimapOverlaySlot(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 slot);
void ClearAllMinimapOverlaySlots(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y);
bool TerrainTileUsesTerrainCode6(
    const MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y);
void SetTerrainFlagA(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, bool enabled);
void SetTerrainFlagB(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, bool enabled);
void SetTerrainFlagC(MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, bool enabled);
void SetTerrainHeightShade(
    MinimapTerrainLayer& layer, i32 tile_x, i32 tile_y, u32 shade_code);
void ResetTerrainOverlayFlags(MinimapTerrainLayer& layer);
void ClearTerrainRenderGridLines(
    TerrainViewportRenderState& state, i32 camera_x, i32 camera_y);
void DrawClippedTerrainDiagonalLine(TerrainViewportRenderState& state,
    i32 start_x, i32 start_y, i32 end_x, i32 end_y, u16 color, u32 thickness);
void ResetTerrainTilePulseState(TerrainTilePulseState& pulse_state);
void UpdateTerrainTilePulseState(
    TerrainTilePulseState& pulse_state, MinimapTerrainLayer& layer);
void StartTerrainTilePulse(
    TerrainTilePulseState& pulse_state, const MinimapTerrainLayer& layer,
    i32 tile_x, i32 tile_y);
void UpdateTerrainTilePulseState(
    TerrainTilePulseState& pulse_state, UnitMovementMap& map);
void StartTerrainTilePulse(
    TerrainTilePulseState& pulse_state, const UnitMovementMap& map,
    i32 tile_x, i32 tile_y);
bool LoadTerrainTileSheetBank(TerrainTileSheetState& tile_sheet,
    const char* archive_name, i32 bank_index, const MinimapPixelFormat& pixel_format,
    u32 color_depth_bits = 16);
bool TerrainTileSheetBankAllocationsValid(const TerrainTileSheetState& tile_sheet);
void ConvertTerrainTileSheetPixelsForSurface(
    TerrainTileSheetState& tile_sheet, const MinimapPixelFormat& pixel_format);
bool BuildTerrainTileAverageColors(
    TerrainTileSheetState& tile_sheet, const MinimapPixelFormat& pixel_format);
bool LoadTerrainTileAveragePalette(const char* archive_name, i32 bank_index,
    const MinimapPixelFormat& pixel_format, u32 color_depth_bits,
    std::vector<u16>& average_pixels);
bool LoadTerrainDecorationResources(
    TerrainTileSheetState& tile_sheet, const char* archive_name, i32 bank_index);
void ReleaseTerrainTileSheetBankResources(TerrainTileSheetState& tile_sheet);
void RenderMinimapTerrainBase(MinimapRenderState& state,
    const MinimapTerrainLayer& layer, MinimapTerrainRenderConfig& config);
void ApplyMinimapUnitAndOverlayMarkers(MinimapRenderState& state,
    const std::vector<UnitMovementUnit*>& active_units,
    const std::vector<MinimapOwnerMarkerColors>& owner_colors,
    const MinimapOverlayLayer& overlay_layer);
void CopyMinimapScratchToOutput(MinimapRenderState& state);
void DrawMinimapViewportBorder(MinimapRenderState& state, i32 world_x, i32 world_y);

} // namespace ranker
