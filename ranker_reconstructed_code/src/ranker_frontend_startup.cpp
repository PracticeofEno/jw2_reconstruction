#include "ranker_frontend_startup.h"

#include "ranker_cursor.h"
#include "ranker_directx.h"
#include "ranker_gameplay_sound.h"
#include "ranker_gameplay_tooltips.h"
#include "ranker_map_brush.h"
#include "ranker_miles.h"
#include "ranker_palette_cache.h"
#include "ranker_production_orders.h"
#include "ranker_resource_store.h"
#include "ranker_runtime_resources.h"
#include "ranker_setup_data.h"
#include "ranker_system_ui.h"
#include "ranker_text_renderer.h"
#include "ranker_trc.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_movement.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <new>

namespace ranker {
namespace {

FrontendStartupState g_frontend_startup_state;
FrontendBootstrapState g_frontend_bootstrap_state;

void append_frontend_bootstrap_log(const char* format, ...) {
    FILE* file = std::fopen("Jw2.log", "a");
    if (file == nullptr) {
        return;
    }

    std::fputs("[rebuild] ", file);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}

u32 read_le_u32_or(const std::vector<u8>& bytes, std::size_t offset, u32 fallback) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(u32)) {
        return fallback;
    }
    return static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

u32 read_le_u32_at(const std::vector<u8>& bytes, std::size_t offset) {
    return read_le_u32_or(bytes, offset, 0);
}

u16 read_le_u16_at(const std::vector<u8>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(u16)) {
        return 0;
    }
    return static_cast<u16>(bytes[offset]) |
        static_cast<u16>(static_cast<u16>(bytes[offset + 1]) << 8);
}

struct StartupFontRegistration {
    u8 flags = 0;
    u32 max_width = 0;
    u32 height = 0;
};

constexpr std::array<StartupFontRegistration, kFrontendStartupFontRecordCount>
    kStartupFontRegistrations = {{
        {0x07, 0x08, 0x09},
        {0x07, 0x0a, 0x0b},
        {0x07, 0x0c, 0x0f},
        {0x15, 0x0f, 0x0f},
        {0x08, 0x08, 0x10},
    }};

void register_frontend_font_record(u32 index, const std::vector<u8>& record) {
    if (index >= kStartupFontRegistrations.size()) {
        return;
    }

    const StartupFontRegistration& registration = kStartupFontRegistrations[index];
    const u8* glyph_data = record.empty() ? nullptr : record.data();
    RegisterTextFontDefinition(index, registration.flags, registration.max_width,
        registration.height, glyph_data, record.size());
}

bool call_gate(FrontendBootstrapCallback callback, FrontendBootstrapState& state) {
    return callback == nullptr || callback(state);
}

void call_void(FrontendBootstrapVoidCallback callback, FrontendBootstrapState& state) {
    if (callback != nullptr) {
        callback(state);
    }
}

bool fail_bootstrap(FrontendBootstrapState& state, FrontendBootstrapFailureStage stage) {
    state.failure_stage = stage;
    state.last_run_success = false;
    return false;
}

bool load_unit_definition_catalog(
    FrontendBootstrapState& state, FrontendBootstrapCallback callback) {
    state.unit_definition_catalog_loaded = callback != nullptr ?
        callback(state) : LoadUnitDefinitionResourceCatalog();
    return state.unit_definition_catalog_loaded;
}

bool load_animation_catalog(
    FrontendBootstrapState& state, FrontendBootstrapCallback callback) {
    state.animation_catalog_loaded = callback != nullptr ?
        callback(state) : LoadJw212RuntimeCatalog();
    return state.animation_catalog_loaded;
}

bool load_runtime_catalog(
    FrontendBootstrapState& state, FrontendBootstrapCallback callback) {
    state.runtime_catalog_loaded = callback != nullptr ?
        callback(state) : LoadJw211RuntimeCatalog();
    return state.runtime_catalog_loaded;
}

void configure_startup_resource_window(
    FrontendBootstrapState& state, const FrontendBootstrapCallbacks& callbacks) {
    state.startup_resource_window_group = 4;
    state.startup_resource_window_type_count = 0xaa;
    state.startup_resource_window_first_resource = 0x40;
    state.startup_resource_window_last_resource = 0x3d;
    if (callbacks.configure_startup_resource_window != nullptr) {
        callbacks.configure_startup_resource_window(state,
            state.startup_resource_window_group,
            state.startup_resource_window_type_count,
            state.startup_resource_window_first_resource,
            state.startup_resource_window_last_resource);
    }
}

bool load_misc_startup_catalog(
    FrontendBootstrapState& state, FrontendBootstrapCallback callback) {
    state.misc_startup_catalog_loaded = callback != nullptr ?
        callback(state) : LoadMinimapBrushRecords(state.minimap_brush_archive);
    return state.misc_startup_catalog_loaded;
}

void advance_loading_progress(FrontendStartupState& startup_state,
    FrontendBootstrapState& bootstrap_state) {
    UpdateFrontendLoadingProgress(startup_state, startup_state.loading_step + 1,
        kFrontendStartupLoadingStepCount);
    (void)bootstrap_state;
    ServeMilesSound();
}

void publish_frontend_palette_copy(FrontendBootstrapState& state, u32 slot) {
    if (slot >= kPaletteCacheSlotCount) {
        return;
    }

    const PaletteCacheState& cache = palette_cache_state();
    state.frontend_palette_555 = cache.pixel_slots[slot];
    state.frontend_palette_raw = cache.raw_slots[slot];
    for (u32 i = 0; i < kPalettePixelCount; ++i) {
        SetTextColorPixel(i, state.frontend_palette_555[i]);
        const std::size_t base = static_cast<std::size_t>(i) * 4u;
        SetTextColorRef(i,
            static_cast<u32>(state.frontend_palette_raw[base]) |
                (static_cast<u32>(state.frontend_palette_raw[base + 1]) << 8) |
                (static_cast<u32>(state.frontend_palette_raw[base + 2]) << 16));
    }
}

u16 normalize_565_to_555_like_original(u16 pixel) {
    const u16 red = static_cast<u16>((pixel >> 1) & 0x7c00u);
    const u16 green = static_cast<u16>((pixel >> 1) & 0x03e0u);
    const u16 blue = static_cast<u16>(pixel & 0x001fu);
    return static_cast<u16>(red + green + blue);
}

void normalize_16bpp_frame(std::vector<u8>& pixels) {
    for (std::size_t i = 0; i + 1 < pixels.size(); i += 2) {
        const u16 pixel = static_cast<u16>(pixels[i]) |
            static_cast<u16>(static_cast<u16>(pixels[i + 1]) << 8);
        const u16 normalized = normalize_565_to_555_like_original(pixel);
        pixels[i] = static_cast<u8>(normalized & 0xffu);
        pixels[i + 1] = static_cast<u8>(normalized >> 8);
    }
}

#ifdef _WIN32
bool register_frontend_window_class(HINSTANCE instance, const char* class_name) {
    if (class_name == nullptr || class_name[0] == '\0') {
        return false;
    }

    WNDCLASSA existing{};
    if (GetClassInfoA(instance, class_name, &existing) != 0) {
        return true;
    }

    WNDCLASSA window_class{};
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    window_class.lpfnWndProc = DefWindowProcA;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(0x65));
    window_class.hCursor = GetFrontendGameCursor();
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = class_name;

    return RegisterClassA(&window_class) != 0 ||
        GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
#endif

} // namespace

FrontendStartupState& frontend_startup_state() {
    return g_frontend_startup_state;
}

FrontendBootstrapState& frontend_bootstrap_state() {
    return g_frontend_bootstrap_state;
}

#ifdef _WIN32
bool RegisterReconstructedFrontendWindowClasses(HINSTANCE instance) {
    constexpr std::array<const char*, 22> kClassNames{
        "Account",
        "Avatar",
        "Barter",
        "Change Lobby",
        "ChangePassword",
        "Connect",
        "Create Game",
        "Emo",
        "FIGS",
        "IPX",
        "IPX Game",
        "Join Game",
        "Light",
        "Link",
        "Lobby",
        "Memo",
        "P2P",
        "Player Profile",
        "Replay",
        "ReplaySave",
        "Search",
        "ViewRank",
    };

    for (const char* class_name : kClassNames) {
        if (!register_frontend_window_class(instance, class_name)) {
            return false;
        }
    }
    return true;
}
#endif

void StampFrontendStartupFileTime(FrontendStartupState& state) {
#ifdef _WIN32
    FILETIME filetime{};
    GetSystemTimeAsFileTime(&filetime);
    state.last_filetime =
        (static_cast<u64>(filetime.dwHighDateTime) << 32) | filetime.dwLowDateTime;
#else
    using clock = std::chrono::system_clock;
    state.last_filetime = static_cast<u64>(std::chrono::duration_cast<
        std::chrono::microseconds>(clock::now().time_since_epoch()).count());
#endif
}

void ReleaseFrontendStartupFontRecords(FrontendStartupState& state) {
    for (std::vector<u8>& record : state.font_records) {
        record.clear();
        record.shrink_to_fit();
    }
}

bool LoadFrontendStartupFontRecords(FrontendStartupState& state,
    const char* archive_name) {
    ReleaseFrontendStartupFontRecords(state);
    if (archive_name == nullptr) {
        return false;
    }

    for (u32 record_index = 0; record_index < 4; ++record_index) {
        ServeMilesSound();
        if (!LoadTrcRecordAlloc(archive_name, record_index,
                state.font_records[record_index])) {
            return false;
        }
        register_frontend_font_record(record_index, state.font_records[record_index]);
    }

    ServeMilesSound();
    register_frontend_font_record(4, state.font_records[4]);
    SelectTextDrawFont(1);
    SelectTextMetricFont(3);
    return true;
}

bool HandleFrontendStartupFontRecordImport(FrontendStartupState& state,
    const char* archive_name) {
    return LoadFrontendStartupFontRecords(state, archive_name);
}

void CreateSaveDirectory(FrontendStartupState& state, const char* path) {
    if (path == nullptr) {
        return;
    }
#ifdef _WIN32
    state.save_directory_created = CreateDirectoryA(path, nullptr) != FALSE;
#else
    state.save_directory_created = true;
#endif
}

void EnsureSaveDirectoryExists(FrontendStartupState& state, const char* path) {
    CreateSaveDirectory(state, path);
}

void UpdateFrontendLoadingProgress(FrontendStartupState& state, u32 current_step,
    u32 total_steps) {
    if (total_steps == 0) {
        total_steps = 1;
    }
    if (current_step > total_steps) {
        current_step = total_steps;
    }

    state.loading_step = current_step;
    state.loading_total = total_steps;
    state.loading_progress_enabled = true;

    const unsigned long long scaled =
        static_cast<unsigned long long>(current_step) * 0x14dull;
    state.loading_progress_pixels = static_cast<u32>(scaled / total_steps);
#ifdef _WIN32
    PresentBackBufferToPrimary();
#endif
}

void ResetFrontendBootstrapState(FrontendBootstrapState& state) {
    const bool draw_environment_ready = state.draw_environment_ready;
    const bool supports_800x600 = state.supports_800x600;
    const bool supports_1024x768 = state.supports_1024x768;
    const bool normalize_16bpp_frames = state.normalize_16bpp_frames;
    state = FrontendBootstrapState{};
    state.draw_environment_ready = draw_environment_ready;
    state.supports_800x600 = supports_800x600;
    state.supports_1024x768 = supports_1024x768;
    state.normalize_16bpp_frames = normalize_16bpp_frames;
}

bool LoadFrontendStartupFrameTable(FrontendBootstrapState& state,
    const char* archive_name, u32 record_index) {
    if (archive_name == nullptr) {
        return false;
    }

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    ServeMilesSound();
    std::array<u8, 0x10> header{};
    if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size())) {
        CloseTrcRecordReader(reader);
        return false;
    }

    const auto header_u32 = [&header](std::size_t offset) {
        return static_cast<u32>(header[offset]) |
            (static_cast<u32>(header[offset + 1]) << 8) |
            (static_cast<u32>(header[offset + 2]) << 16) |
            (static_cast<u32>(header[offset + 3]) << 24);
    };
    const u32 bits_per_pixel = header_u32(0x00);
    const u32 frame_count = header_u32(0x04);
    const u32 width = header_u32(0x08);
    const u32 height = header_u32(0x0c);
    const u32 bytes_per_frame = width * height * (bits_per_pixel >> 3);

    state.startup_frame_bits_per_pixel = bits_per_pixel;
    state.startup_frame_width = width;
    state.startup_frame_height = height;

    for (u32 frame_index = 0; frame_index < frame_count; ++frame_index) {
        ServeMilesSound();
        std::array<u8, 0x10> metadata{};
        if (!ReadOpenTrcRecordBytes(reader, metadata.data(), metadata.size())) {
            CloseTrcRecordReader(reader);
            return false;
        }

        const auto metadata_u32 = [&metadata](std::size_t offset) {
            return static_cast<u32>(metadata[offset]) |
                (static_cast<u32>(metadata[offset + 1]) << 8) |
                (static_cast<u32>(metadata[offset + 2]) << 16) |
                (static_cast<u32>(metadata[offset + 3]) << 24);
        };
        FrontendStartupFrame frame;
        frame.metadata0 = metadata_u32(0x00);
        frame.metadata1 = metadata_u32(0x04);
        frame.metadata2 = metadata_u32(0x08);
        frame.metadata3 = metadata_u32(0x0c);

        try {
            frame.pixels.assign(static_cast<std::size_t>(bytes_per_frame), 0);
        } catch (const std::bad_alloc&) {
            CloseTrcRecordReader(reader);
            return false;
        }
        if (!ReadOpenTrcRecordBytes(reader, frame.pixels.data(), frame.pixels.size())) {
            CloseTrcRecordReader(reader);
            return false;
        }
        if (state.normalize_16bpp_frames && bits_per_pixel == 0x10) {
            normalize_16bpp_frame(frame.pixels);
        }

        try {
            if (frame_index >= state.startup_frames.size()) {
                state.startup_frames.resize(static_cast<std::size_t>(frame_index) + 1u);
            }
            state.startup_frames[frame_index] = std::move(frame);
        } catch (const std::bad_alloc&) {
            CloseTrcRecordReader(reader);
            return false;
        }
    }

    CloseTrcRecordReader(reader);
    return true;
}

bool RunFrontendStartupBootstrap(FrontendStartupState& startup_state,
    FrontendBootstrapState& bootstrap_state,
    const FrontendBootstrapCallbacks& callbacks) {
    bootstrap_state.last_run_success = false;
    bootstrap_state.failure_stage = FrontendBootstrapFailureStage::None;
    startup_state.loading_step = 0;
    startup_state.loading_total = kFrontendStartupLoadingStepCount;

    append_frontend_bootstrap_log("bootstrap step init begin");
    RefreshPaletteTransparentMask();
    InitializeUiFontHandles();
#ifdef _WIN32
    if (InitializeSoftwareCursorSurfaces()) {
        LoadSoftwareCursorResourcesFromJw201Trc();
    }
#endif

    if (!bootstrap_state.draw_environment_ready ||
        !call_gate(callbacks.check_draw_environment, bootstrap_state)) {
        bootstrap_state.fatal_error_sent = true;
        call_void(callbacks.send_fatal_error, bootstrap_state);
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::DrawEnvironment);
    }
    append_frontend_bootstrap_log("bootstrap step init ok");

    ResetResourceStore();
    append_frontend_bootstrap_log("bootstrap step setup begin");
    const bool setup_loaded = LoadDefaultSetupDataBuffer();
    const bool reset_setup_data =
        bootstrap_state.setup_data_needs_reset || !setup_loaded;
    if (reset_setup_data) {
        InitializeDefaultSetupDataBuffer();
        ExportSetupU32(kSetupScreenWidthOffset, bootstrap_state.configured_width);
        ExportSetupU32(kSetupScreenHeightOffset, bootstrap_state.configured_height);
        ExportSetupU32(kSetupPrimaryMusicRawVolumeOffset,
            miles_music_state().primary_policy_raw_volume);
        WriteDefaultSetupDataBuffer();
        bootstrap_state.intro_sequence_requested = true;
    }
    bootstrap_state.setup_data_needs_reset = reset_setup_data;
    bootstrap_state.launch_count =
        ImportSetupU32(kSetupLaunchCountOffset, bootstrap_state.launch_count) + 1u;
    ExportSetupU32(kSetupLaunchCountOffset, bootstrap_state.launch_count);
    WriteDefaultSetupDataBuffer();
    bootstrap_state.configured_width =
        ImportSetupU32(kSetupScreenWidthOffset, bootstrap_state.configured_width);
    bootstrap_state.configured_height =
        ImportSetupU32(kSetupScreenHeightOffset, bootstrap_state.configured_height);
    SetPrimaryMilesMusicPolicyRawVolume(std::min<u32>(
        ImportSetupU32(kSetupPrimaryMusicRawVolumeOffset,
            miles_music_state().primary_policy_raw_volume),
        0xffffu));
    append_frontend_bootstrap_log("bootstrap step setup ok loaded=%s reset=%s",
        setup_loaded ? "yes" : "no",
        reset_setup_data ? "yes" : "no");

    append_frontend_bootstrap_log("bootstrap step music begin");
    InitializePrimaryMilesMusicPolicy();
    ApplyPrimaryMilesMusicPolicyVolume();
    if (bootstrap_state.configured_width == 800 && !bootstrap_state.supports_800x600) {
        bootstrap_state.configured_width = 640;
        bootstrap_state.configured_height = 480;
    } else if (bootstrap_state.configured_width == 1024 &&
        !bootstrap_state.supports_1024x768) {
        bootstrap_state.configured_width = 640;
        bootstrap_state.configured_height = 480;
    }
    ExportSetupU32(kSetupScreenWidthOffset, bootstrap_state.configured_width);
    ExportSetupU32(kSetupScreenHeightOffset, bootstrap_state.configured_height);

    if (bootstrap_state.intro_sequence_requested) {
        call_void(callbacks.play_intro_sequence, bootstrap_state);
    }
    append_frontend_bootstrap_log("bootstrap step music ok width=%lu height=%lu",
        static_cast<unsigned long>(bootstrap_state.configured_width),
        static_cast<unsigned long>(bootstrap_state.configured_height));

    u32 record_count = 0;
    append_frontend_bootstrap_log("bootstrap step startup archive begin");
    if (!QueryTrcArchiveRecordCount("JW2_01.TRC", &record_count) || record_count == 0) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::StartupArchive);
    }
    append_frontend_bootstrap_log("bootstrap step startup archive ok records=%lu",
        static_cast<unsigned long>(record_count));

    SetPrimaryMilesMusicPolicyMode(2);
    call_void(callbacks.prepare_startup_archive, bootstrap_state);
    call_void(callbacks.prepare_loading_surface, bootstrap_state);
    ServeMilesSound();

    bootstrap_state.startup_palette_slot = LoadPaletteCacheTrcRecord("JW2_01.TRC", 5);
    if (bootstrap_state.startup_palette_slot == 0xffffffffu) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::PaletteRecord5);
    }
    // FUN_00414da0 publishes JW2_01 record 5 as DAT_00b88eb0 before any
    // command-theme rewind point is captured.  Interface resources then bind
    // JW2_18 records 0..7 to this startup-owned palette for the process
    // lifetime instead of allocating a duplicate inside the session stack.
    SetSharedUiPaletteSlot(bootstrap_state.startup_palette_slot);
    append_frontend_bootstrap_log("bootstrap step palette5 ok slot=%lu",
        static_cast<unsigned long>(bootstrap_state.startup_palette_slot));
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step frame table begin");
    if (!LoadFrontendStartupFrameTable(bootstrap_state)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::StartupFrameTable);
    }
    append_frontend_bootstrap_log(
        "bootstrap step frame table ok frames=%zu bpp=%lu size=%lux%lu",
        bootstrap_state.startup_frames.size(),
        static_cast<unsigned long>(bootstrap_state.startup_frame_bits_per_pixel),
        static_cast<unsigned long>(bootstrap_state.startup_frame_width),
        static_cast<unsigned long>(bootstrap_state.startup_frame_height));
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step fonts begin");
    if (!LoadFrontendStartupFontRecords(startup_state)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::FontRecords);
    }
    append_frontend_bootstrap_log("bootstrap step fonts ok");
    advance_loading_progress(startup_state, bootstrap_state);

    bootstrap_state.frontend_palette_slot = LoadPaletteCacheTrcRecord("JW2_01.TRC", 4);
    if (bootstrap_state.frontend_palette_slot == 0xffffffffu) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::PaletteRecord4);
    }
    publish_frontend_palette_copy(bootstrap_state, bootstrap_state.frontend_palette_slot);
    ReleasePaletteCacheSlotsFrom(bootstrap_state.frontend_palette_slot);
    append_frontend_bootstrap_log("bootstrap step palette4 ok slot=%lu",
        static_cast<unsigned long>(bootstrap_state.frontend_palette_slot));

    append_frontend_bootstrap_log("bootstrap step gameplay sound begin");
    InitializeDefaultGameplaySoundAttenuation(bootstrap_state.gameplay_sound);
    if (!InitializeGameplaySoundEffectBank(bootstrap_state.gameplay_sound)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::GameplaySoundBank);
    }
    append_frontend_bootstrap_log("bootstrap step gameplay sound ok");
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step direction begin");
    // JW2_07.TRC record 0 is the 16-way table used by
    // HandlePointDirection16LookupEntry, while record 1 is the 8-way table
    // used by PointDirectionLookupLowThunk.  Keeping these in archive order
    // here is important: swapping them changes neutral-unit turn direction
    // before the shared gameplay RNG itself has diverged.
    if (!LoadJw207DirectionLookupRecords(bootstrap_state.direction_lookup_16,
            bootstrap_state.direction_lookup_8)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::DirectionLookup);
    }
    append_frontend_bootstrap_log("bootstrap step direction ok");
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step unit catalog begin");
    if (!load_unit_definition_catalog(
            bootstrap_state, callbacks.load_unit_definition_catalog)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::UnitDefinitionCatalog);
    }
    append_frontend_bootstrap_log("bootstrap step unit catalog ok");
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step animation catalog begin");
    if (!load_animation_catalog(bootstrap_state, callbacks.load_animation_catalog)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::AnimationCatalog);
    }
    append_frontend_bootstrap_log("bootstrap step animation catalog ok");
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step runtime catalog begin");
    if (!load_runtime_catalog(bootstrap_state, callbacks.load_runtime_catalog)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::RuntimeCatalog);
    }
    append_frontend_bootstrap_log("bootstrap step runtime catalog ok");
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step production catalog begin");
    if (!LoadProductionOrderCatalogFromJw210Trc(bootstrap_state.production_catalog)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::ProductionCatalog);
    }
    LoadGameplayTooltipProductionOrderDefinitionsFromCatalog(gameplay_tooltip_state(),
        bootstrap_state.production_catalog);
    append_frontend_bootstrap_log("bootstrap step production catalog ok");
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step equipment catalog begin");
    if (!LoadUnitEquipmentCatalogFromJw210Trc(bootstrap_state.equipment_catalog)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::UnitEquipmentCatalog);
    }
    LoadGameplayTooltipEquipmentDefinitionsFromCatalog(gameplay_tooltip_state(),
        bootstrap_state.equipment_catalog);
    append_frontend_bootstrap_log("bootstrap step equipment catalog ok");
    advance_loading_progress(startup_state, bootstrap_state);

    configure_startup_resource_window(bootstrap_state, callbacks);
    append_frontend_bootstrap_log("bootstrap step misc catalog begin");
    if (!load_misc_startup_catalog(
            bootstrap_state, callbacks.load_misc_startup_catalog)) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::MiscStartupCatalog);
    }
    append_frontend_bootstrap_log("bootstrap step misc catalog ok");
    advance_loading_progress(startup_state, bootstrap_state);

    append_frontend_bootstrap_log("bootstrap step jw207 packs begin");
    if (!LoadJw207GameplayResourcePacks()) {
        return fail_bootstrap(bootstrap_state,
            FrontendBootstrapFailureStage::Jw207ResourcePacks);
    }
    append_frontend_bootstrap_log("bootstrap step jw207 packs ok");
    advance_loading_progress(startup_state, bootstrap_state);

    call_void(callbacks.finish_bootstrap, bootstrap_state);
    bootstrap_state.last_run_success = true;
    bootstrap_state.failure_stage = FrontendBootstrapFailureStage::None;
    return true;
}

void ShutdownFrontendStartupResources(FrontendStartupState& startup_state,
    FrontendBootstrapState& bootstrap_state,
    const FrontendBootstrapCallbacks& callbacks) {
    ReleaseResourceEntriesFrom(0);
    ReleasePaletteCacheSlotsFrom(0);
    ReleaseFrontendStartupFontRecords(startup_state);
    ReleaseFrontendStartupFontRecords(startup_state);
    bootstrap_state.startup_frames.clear();
    ShutdownUiFontHandles();
    call_void(callbacks.release_additional_resources, bootstrap_state);
}

} // namespace ranker
