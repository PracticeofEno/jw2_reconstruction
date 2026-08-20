#pragma once

#include "ranker_gameplay_sound.h"
#include "ranker_map_brush.h"
#include "ranker_palette_cache.h"
#include "ranker_production_orders.h"
#include "ranker_types.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_movement.h"

#include <array>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {

constexpr u32 kFrontendStartupFontRecordCount = 5;
constexpr u32 kFrontendStartupFrameRecordIndex = 0x0b;
constexpr u32 kFrontendStartupLoadingStepCount = 12;

enum class FrontendBootstrapFailureStage : u32 {
    None = 0,
    DrawEnvironment,
    StartupArchive,
    PaletteRecord5,
    StartupFrameTable,
    FontRecords,
    PaletteRecord4,
    GameplaySoundBank,
    DirectionLookup,
    UnitDefinitionCatalog,
    AnimationCatalog,
    RuntimeCatalog,
    ProductionCatalog,
    UnitEquipmentCatalog,
    MiscStartupCatalog,
    Jw207ResourcePacks,
};

struct FrontendStartupFrame {
    u32 metadata0 = 0;
    u32 metadata1 = 0;
    u32 metadata2 = 0;
    u32 metadata3 = 0;
    std::vector<u8> pixels;
};

struct FrontendStartupState {
    std::array<std::vector<u8>, kFrontendStartupFontRecordCount> font_records;
    u64 last_filetime = 0;
    u32 loading_step = 0;
    u32 loading_total = 0;
    u32 loading_progress_pixels = 0;
    bool loading_progress_enabled = false;
    bool save_directory_created = false;
};

struct FrontendBootstrapState {
    bool draw_environment_ready = true;
    bool supports_800x600 = true;
    bool supports_1024x768 = true;
    bool setup_data_needs_reset = false;
    bool intro_sequence_requested = false;
    bool fatal_error_sent = false;
    bool last_run_success = false;
    FrontendBootstrapFailureStage failure_stage = FrontendBootstrapFailureStage::None;
    u32 configured_width = 800;
    u32 configured_height = 600;
    u32 launch_count = 0;
    u32 startup_palette_slot = 0xffffffffu;
    u32 loading_bar_palette_slot = 0xffffffffu;
    u32 loading_bar_resource_entry = 0xffffffffu;
    bool loading_bar_load_attempted = false;
    bool loading_bar_resources_loaded = false;
    u32 frontend_palette_slot = 0xffffffffu;
    u32 startup_frame_bits_per_pixel = 0;
    u32 startup_frame_width = 0;
    u32 startup_frame_height = 0;
    bool normalize_16bpp_frames = false;
    std::array<u16, 0x100> frontend_palette_555{};
    std::array<u16, 0x100> frontend_palette_565{};
    std::array<u8, kPaletteRawBytesPerSlot> frontend_palette_raw{};
    std::vector<FrontendStartupFrame> startup_frames;
    GameplaySoundState gameplay_sound;
    UnitDirectionLookupTable direction_lookup_8;
    UnitDirectionLookupTable direction_lookup_16;
    ProductionOrderCatalog production_catalog;
    UnitEquipmentCatalog equipment_catalog;
    MapBrushArchiveState minimap_brush_archive;
    u32 startup_resource_window_group = 0;
    u32 startup_resource_window_type_count = 0;
    u32 startup_resource_window_first_resource = 0;
    u32 startup_resource_window_last_resource = 0;
    bool unit_definition_catalog_loaded = false;
    bool animation_catalog_loaded = false;
    bool runtime_catalog_loaded = false;
    bool misc_startup_catalog_loaded = false;
};

using FrontendBootstrapCallback = bool (*)(FrontendBootstrapState& state);
using FrontendBootstrapVoidCallback = void (*)(FrontendBootstrapState& state);
using FrontendBootstrapRectCallback = void (*)(FrontendBootstrapState& state,
    u32 group, u32 type_count, u32 first_resource, u32 last_resource);

struct FrontendBootstrapCallbacks {
    FrontendBootstrapCallback check_draw_environment = nullptr;
    FrontendBootstrapVoidCallback send_fatal_error = nullptr;
    FrontendBootstrapVoidCallback play_intro_sequence = nullptr;
    FrontendBootstrapVoidCallback prepare_startup_archive = nullptr;
    FrontendBootstrapVoidCallback prepare_loading_surface = nullptr;
    FrontendBootstrapCallback load_unit_definition_catalog = nullptr;
    FrontendBootstrapCallback load_animation_catalog = nullptr;
    FrontendBootstrapCallback load_runtime_catalog = nullptr;
    FrontendBootstrapCallback load_misc_startup_catalog = nullptr;
    FrontendBootstrapRectCallback configure_startup_resource_window = nullptr;
    FrontendBootstrapVoidCallback finish_bootstrap = nullptr;
    FrontendBootstrapVoidCallback release_additional_resources = nullptr;
};

FrontendStartupState& frontend_startup_state();
FrontendBootstrapState& frontend_bootstrap_state();
void StampFrontendStartupFileTime(FrontendStartupState& state);
void ReleaseFrontendStartupFontRecords(FrontendStartupState& state);
bool LoadFrontendStartupFontRecords(FrontendStartupState& state,
    const char* archive_name = "JW2_01.TRC");
bool HandleFrontendStartupFontRecordImport(FrontendStartupState& state,
    const char* archive_name = "JW2_01.TRC");
void CreateSaveDirectory(FrontendStartupState& state, const char* path = "Save");
void EnsureSaveDirectoryExists(FrontendStartupState& state, const char* path = "Save");
void UpdateFrontendLoadingProgress(FrontendStartupState& state, u32 current_step,
    u32 total_steps);
void ResetFrontendBootstrapState(FrontendBootstrapState& state);
bool LoadFrontendStartupFrameTable(FrontendBootstrapState& state,
    const char* archive_name = "JW2_01.TRC",
    u32 record_index = kFrontendStartupFrameRecordIndex);
bool RunFrontendStartupBootstrap(FrontendStartupState& startup_state,
    FrontendBootstrapState& bootstrap_state,
    const FrontendBootstrapCallbacks& callbacks = {});
void ShutdownFrontendStartupResources(FrontendStartupState& startup_state,
    FrontendBootstrapState& bootstrap_state,
    const FrontendBootstrapCallbacks& callbacks = {});

#ifdef _WIN32
bool RegisterReconstructedFrontendWindowClasses(HINSTANCE instance);
#endif

}
