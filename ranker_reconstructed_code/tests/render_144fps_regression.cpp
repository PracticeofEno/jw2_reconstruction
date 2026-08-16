#include "ranker_client_config.h"
#include "ranker_game_loop.h"
#include "ranker_palette_cache.h"
#include "ranker_resource_store.h"
#include "ranker_sprite_renderer.h"
#include "ranker_unit_render_queue.h"
#include "ranker_visual_animation.h"
#include "ranker_visual_animation_archive.h"

#include "zlib.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include "ranker_crt_runtime.h"
#include "ranker_directx.h"
#endif

#ifdef _WIN32
namespace ranker {

const DirectDrawRuntimeState& direct_draw_state() {
    static DirectDrawRuntimeState state{};
    return state;
}

FILE* CrtFopen(const char*, const char*) {
    return nullptr;
}

int CrtFclose(FILE*) {
    return 0;
}

std::size_t CrtFread(void*, std::size_t, std::size_t, FILE*) {
    return 0;
}

HRESULT LockBackBufferSpriteRenderTarget(SpriteRenderTarget&) {
    return E_FAIL;
}

HRESULT UnlockBackBufferSpriteRenderTarget() {
    return E_FAIL;
}

HRESULT PresentBackBufferToPrimary() {
    return E_FAIL;
}

} // namespace ranker
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_palette_slot_lifetime() {
    using namespace ranker;
    ResetPaletteCache();

    const u32 first = AllocatePaletteCacheSlot();
    const u64 first_serial = GetPaletteCacheSlotAllocationSerial(first);
    require(first == 0 && first_serial != 0 && IsPaletteCacheSlotActive(first),
        "the first palette allocation must publish a live identity");
    require(PaletteCacheSlotAllocationMatches(first, first_serial),
        "the live palette identity must match its allocation");

    ReleasePaletteCacheSlotsFrom(first);
    require(!IsPaletteCacheSlotActive(first) &&
            GetPaletteCacheSlotAllocationSerial(first) == 0 &&
            !PaletteCacheSlotAllocationMatches(first, first_serial),
        "a rewound palette slot must immediately become invalid");

    std::array<u8, kPaletteRawBytesPerSlot> raw{};
    require(!SetPaletteCacheRawSlot(first, raw.data(), raw.size()),
        "a released palette slot must reject writes");

    const u32 reused = AllocatePaletteCacheSlot();
    const u64 reused_serial = GetPaletteCacheSlotAllocationSerial(reused);
    require(reused == first && reused_serial != 0 && reused_serial != first_serial,
        "a reused numeric slot must receive a new allocation identity");
    require(!PaletteCacheSlotAllocationMatches(reused, first_serial) &&
            PaletteCacheSlotAllocationMatches(reused, reused_serial),
        "a persistent palette reference must not bind to a later reuse");
}

std::vector<u8> encode_sprite_rows(
    const std::vector<std::vector<u8>>& rows) {
    std::vector<u8> payload;
    for (const auto& row : rows) {
        std::vector<u8> encoded;
        std::size_t column = 0;
        while (column < row.size()) {
            if (row[column] != 0) {
                encoded.push_back(row[column++]);
                continue;
            }
            std::size_t run = 1;
            while (column + run < row.size() && row[column + run] == 0 &&
                run < 255) {
                ++run;
            }
            encoded.push_back(0);
            encoded.push_back(static_cast<u8>(run));
            column += run;
        }
        payload.push_back(static_cast<u8>(encoded.size() & 0xffu));
        payload.push_back(static_cast<u8>(encoded.size() >> 8));
        payload.insert(payload.end(), encoded.begin(), encoded.end());
    }
    return payload;
}

void append_u16(std::vector<u8>& bytes, u16 value) {
    bytes.push_back(static_cast<u8>(value));
    bytes.push_back(static_cast<u8>(value >> 8u));
}

void append_u32(std::vector<u8>& bytes, u32 value) {
    append_u16(bytes, static_cast<u16>(value));
    append_u16(bytes, static_cast<u16>(value >> 16u));
}

void append_u64(std::vector<u8>& bytes, u64 value) {
    append_u32(bytes, static_cast<u32>(value));
    append_u32(bytes, static_cast<u32>(value >> 32u));
}

std::filesystem::path create_test_animation_archive() {
    const std::vector<u8> pose = encode_sprite_rows({
        {2, 0, 0, 0},
        {0, 1, 2, 3},
        {0, 0, 1, 3},
    });
    std::vector<u8> raw_transition;
    for (u32 frame = 0; frame < 11; ++frame) {
        append_u32(raw_transition, static_cast<u32>(pose.size()));
        raw_transition.insert(raw_transition.end(), pose.begin(), pose.end());
    }
    uLongf compressed_size = static_cast<uLongf>(
        raw_transition.size() + raw_transition.size() / 1000 + 32);
    std::vector<u8> compressed(compressed_size);
    require(compress2(compressed.data(), &compressed_size,
                raw_transition.data(), static_cast<uLong>(raw_transition.size()),
                Z_BEST_COMPRESSION) == Z_OK,
        "the test transition must compress");
    compressed.resize(compressed_size);

    constexpr u64 header_size = 104;
    constexpr u64 directory_size = 40;
    constexpr u64 payload_offset = header_size + directory_size;
    std::vector<u8> directory;
    append_u16(directory, 3); // unit type
    directory.push_back(1);  // image group
    directory.push_back(0);  // normal data; flipped rendering mirrors it
    append_u16(directory, 0); // source frame
    append_u16(directory, 1); // target frame
    append_u16(directory, 0); // left
    append_u16(directory, 0); // top
    append_u16(directory, 4); // width
    append_u16(directory, 3); // height
    append_u64(directory, payload_offset);
    append_u32(directory, static_cast<u32>(compressed.size()));
    append_u32(directory, static_cast<u32>(raw_transition.size()));
    append_u32(directory, static_cast<u32>(crc32(0,
        raw_transition.data(), static_cast<uInt>(raw_transition.size()))));
    append_u16(directory, 1); // flipped x = 1 - normal x
    append_u16(directory, 0); // flipped y = normal y
    require(directory.size() == directory_size,
        "the test archive directory must match the production format");

    std::vector<u8> archive;
    archive.insert(archive.end(), {'R', '1', '4', '4', 'R', 'F', 'A', 0});
    append_u32(archive, 2); // version
    append_u32(archive, static_cast<u32>(header_size));
    append_u32(archive, 1); // transition count
    append_u32(archive, static_cast<u32>(directory_size));
    append_u64(archive, header_size);
    append_u64(archive, payload_offset);
    append_u64(archive, compressed.size());
    append_u64(archive, 0); // source bytes; source verification is skipped
    archive.insert(archive.end(), 32, 0); // source SHA-256
    append_u32(archive, 0); // source CRC-32
    append_u32(archive, static_cast<u32>(crc32(0, directory.data(),
        static_cast<uInt>(directory.size()))));
    append_u32(archive, 12);
    append_u32(archive, 11);
    require(archive.size() == header_size,
        "the test archive header must match the production format");
    archive.insert(archive.end(), directory.begin(), directory.end());
    archive.insert(archive.end(), compressed.begin(), compressed.end());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "ranker_render_144fps_regression.rfa";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "the test archive must open");
    stream.write(reinterpret_cast<const char*>(archive.data()), archive.size());
    require(static_cast<bool>(stream), "the test archive must write");
    return path;
}

u32 make_sprite_resource(
    const std::vector<std::vector<u8>>& rows, u32 palette_slot) {
    using namespace ranker;
    const std::vector<u8> payload = encode_sprite_rows(rows);
    u32 entry = kInvalidResourceEntry;
    void* destination = nullptr;
    require(AllocateResourceEntry(payload.size(), &entry, &destination),
        "the synthetic sprite resource must allocate");
    std::memcpy(destination, payload.data(), payload.size());
    const u32 width = static_cast<u32>(rows.front().size());
    const u32 height = static_cast<u32>(rows.size());
    const std::array<u32, 6> metadata{
        width, height, 0, 0, static_cast<u32>(-static_cast<i32>(width)), 0};
    require(ConfigureResourceEntry(entry, metadata, palette_slot),
        "the synthetic sprite metadata must configure");
    return entry;
}

void test_pre_generated_pixel_morph_sprites() {
    using namespace ranker;
    ResetResourceStore();
    ResetPaletteCache();
    ResetResourceSpritePixelMorphCache();
    const std::filesystem::path archive_path = create_test_animation_archive();
    require(LoadVisualAnimationArchive(archive_path.string(), ""),
        "the pre-generated animation archive must load");

    std::array<u8, kPaletteRawBytesPerSlot> raw_palette{};
    raw_palette[2 * 4 + 0] = 248;
    raw_palette[2 * 4 + 1] = 32;
    raw_palette[2 * 4 + 2] = 16;
    raw_palette[3 * 4 + 0] = 24;
    raw_palette[3 * 4 + 1] = 240;
    raw_palette[3 * 4 + 2] = 48;
    const u32 ramp_slot = AllocatePaletteCacheSlot();
    const u32 sprite_slot = AllocatePaletteCacheSlot();
    require(ramp_slot == 0 && sprite_slot == 1,
        "the unit-ramp source and sprite palettes must use canonical slots");
    require(SetPaletteCacheRawSlot(ramp_slot, raw_palette.data(), raw_palette.size()) &&
            SetPaletteCacheRawSlot(sprite_slot, raw_palette.data(), raw_palette.size()),
        "the synthetic sprite palettes must load");
    ConvertPaletteCacheSlot(ramp_slot);
    ConvertPaletteCacheSlot(sprite_slot);
    RefreshPaletteTransparentMask();

    const u32 source = make_sprite_resource({
        {0, 2, 2, 0},
        {1, 2, 3, 0},
        {1, 0, 3, 0},
    }, sprite_slot);
    const u32 target = make_sprite_resource({
        {0, 0, 2, 2},
        {0, 1, 3, 2},
        {0, 1, 0, 3},
    }, sprite_slot);
    const ResourceStoreEntry source_snapshot = *GetResourceEntry(source);
    const ResourceStoreEntry target_snapshot = *GetResourceEntry(target);

    constexpr u16 background = 0x7befu;
    constexpr u32 surface_width = 12;
    constexpr u32 surface_height = 9;
    const auto render = [&](u32 step, bool flipped, bool direct_target) {
        std::vector<u16> pixels(surface_width * surface_height, background);
        SetSpriteRenderTarget(pixels.data(), surface_width, surface_height);
        SetSpriteUnitPaletteRamp(0);
        bool drew = false;
        if (direct_target) {
            drew = flipped
                ? DrawResourceSpriteFlippedUnitRampToken1Shadow(target, 6, 3)
                : DrawResourceSpriteUnitRampToken1Shadow(target, 3, 3);
        }
        else {
            drew = DrawResourceSpriteUnitRampPixelMorphTransition(
                3, 1, 0, 1, source, target,
                flipped ? 6 : 3, 3, step, flipped);
        }
        require(drew, "the pixel-morph frame must render");
        ClearSpriteRenderTarget();
        return pixels;
    };

    const std::vector<u16> source_direct = [&]() {
        std::vector<u16> pixels(surface_width * surface_height, background);
        SetSpriteRenderTarget(pixels.data(), surface_width, surface_height);
        require(DrawResourceSpriteUnitRampToken1Shadow(source, 3, 3),
            "the source endpoint must render");
        ClearSpriteRenderTarget();
        return pixels;
    }();
    const std::vector<u16> source_endpoint = render(0, false, false);
    const std::vector<u16> target_endpoint = render(
        kVisualAnimationIntervalCount, false, false);
    const std::vector<u16> target_direct = render(0, false, true);
    require(source_endpoint == source_direct,
        "subframe zero must be bit-exact with the original source sprite");
    require(target_endpoint == target_direct,
        "the final subframe must be bit-exact with the original target sprite");

    const std::vector<u16> midpoint = render(6, false, false);
    require(midpoint != source_direct && midpoint != target_direct,
        "the midpoint must come from the pre-generated pose, not an endpoint copy");
    const auto& palette = palette_cache_state().pixel_slots[sprite_slot];
    const u16 shadow = static_cast<u16>(
        (background & palette_cache_state().transparent_mask) >> 1);
    for (u16 pixel : midpoint) {
        require(pixel == background || pixel == shadow ||
                pixel == palette[2] || pixel == palette[3],
            "pixel morphing must retain opaque original-palette dots");
    }
    (void)render(6, true, false);

    ConfigureSpritePixelMaskConstants(false);
    std::array<SpritePixelMorphDrawOptions, 10> style_options{};
    style_options[0].style = SpritePixelMorphStyle::palette;
    style_options[1].style = SpritePixelMorphStyle::resource_mode;
    style_options[1].mode_or_factor = 4;
    style_options[2].style = SpritePixelMorphStyle::resource_mode;
    style_options[2].mode_or_factor = 8;
    style_options[3].style = SpritePixelMorphStyle::resource_mode;
    style_options[3].mode_or_factor = 9;
    style_options[4].style = SpritePixelMorphStyle::or_mask_token1_shadow;
    style_options[4].mask = sprite_pixel_mask_constants().high_green;
    style_options[5].style = SpritePixelMorphStyle::grayscale_token1_shadow;
    style_options[6].style = SpritePixelMorphStyle::blend_factor_token2_plus;
    style_options[6].mode_or_factor = 15;
    style_options[7].style = SpritePixelMorphStyle::neighbor_copy;
    style_options[8].style =
        SpritePixelMorphStyle::unit_ramp_or_mask_token1_shadow;
    style_options[8].mask = sprite_pixel_mask_constants().low_blue_a;
    style_options[9].style = SpritePixelMorphStyle::channel_add_token1_shadow;
    style_options[9].red_delta = 0x0800;
    style_options[9].green_delta = 0x0040;
    style_options[9].blue_delta = 0x0002;
    for (const SpritePixelMorphDrawOptions& options : style_options) {
        std::vector<u16> pixels(surface_width * surface_height, background);
        SetSpriteRenderTarget(pixels.data(), surface_width, surface_height);
        require(DrawResourceSpritePixelMorphTransition(
                3, 1, 0, 1, source, target, 3, 3, 6, options),
            "every unit draw style must accept a pre-generated midpoint pose");
        ClearSpriteRenderTarget();
    }

    require(visual_animation_archive_state().lookup_hits >= 12,
        "intermediate poses must be read from the archive cache");

    UnloadVisualAnimationArchive();
    const std::filesystem::path mismatched_source =
        std::filesystem::temp_directory_path() /
        "ranker_render_144fps_mismatched_source.trc";
    {
        std::ofstream source_stream(
            mismatched_source, std::ios::binary | std::ios::trunc);
        source_stream.put('x');
    }
    require(!LoadVisualAnimationArchive(
                archive_path.string(), mismatched_source.string()) &&
            visual_animation_archive_state().status ==
                VisualAnimationArchiveStatus::source_mismatch,
        "an archive built from a different TRC must be rejected");
    const std::vector<u16> missing_archive_fallback = render(6, false, false);
    require(missing_archive_fallback == target_direct,
        "a mismatched archive must fall back to the exact current TRC frame");
    UnloadVisualAnimationArchive();
    std::remove(mismatched_source.string().c_str());
    std::remove(archive_path.string().c_str());

    const ResourceStoreEntry* source_after = GetResourceEntry(source);
    const ResourceStoreEntry* target_after = GetResourceEntry(target);
    require(source_after != nullptr && target_after != nullptr &&
            source_after->allocation_serial == source_snapshot.allocation_serial &&
            target_after->allocation_serial == target_snapshot.allocation_serial &&
            source_after->metadata == source_snapshot.metadata &&
            target_after->metadata == target_snapshot.metadata &&
            source_after->payload == source_snapshot.payload &&
            target_after->payload == target_snapshot.payload,
        "presentation archive rendering must not mutate original resources");
}

} // namespace

int main() {
    using namespace ranker;

    require(GameplayLoopState{}.render_target_fps ==
            kGameplayDefaultRenderFramesPerSecond,
        "gameplay rendering must default to 60 FPS");
    require(NormalizeConfiguredRenderFramesPerSecond(0) == 0 &&
            NormalizeConfiguredRenderFramesPerSecond(29) == 0 &&
            NormalizeConfiguredRenderFramesPerSecond(361) == 0,
        "disabled and out-of-range render FPS values must use original pacing");
    require(NormalizeConfiguredRenderFramesPerSecond(60) == 60 &&
            NormalizeConfiguredRenderFramesPerSecond(144) == 144,
        "supported render FPS values must remain configurable");
    require(kGameplayDefaultRenderFramesPerSecond == 60,
        "the default gameplay render rate must remain 60 FPS");
    GameplayLoopState toggle_state{};
    toggle_state.next_present_tick_ns = 123456789ull;
    toggle_state.render_schedule_initialized = true;
    require(ToggleGameplayRenderFramesPerSecond(toggle_state) == 144 &&
            toggle_state.render_target_fps == 144 &&
            toggle_state.next_present_tick_ns == 0 &&
            !toggle_state.render_schedule_initialized,
        "F11 must switch the 60 Hz default to 144 Hz and restart presentation pacing");
    toggle_state.next_present_tick_ns = 987654321ull;
    toggle_state.render_schedule_initialized = true;
    require(ToggleGameplayRenderFramesPerSecond(toggle_state) == 60 &&
            toggle_state.render_target_fps == 60 &&
            toggle_state.next_present_tick_ns == 0 &&
            !toggle_state.render_schedule_initialized,
        "a second F11 must switch 144 Hz back to 60 Hz");
    toggle_state.render_target_fps = 0;
    require(ToggleGameplayRenderFramesPerSecond(toggle_state) == 144,
        "F11 from a non-toggle configured rate must enter the 144 Hz state");
    const u64 interval_ns = GameplayTargetRenderIntervalNanoseconds(
        kGameplayDefaultRenderFramesPerSecond);
    require(interval_ns == 16666666ull,
        "the 60 Hz interval must use the high-resolution render clock");

    bool initialized = false;
    u64 next_present_ns = 0;
    require(ShouldPresentGameplayTargetFrame(900000000ull,
            next_present_ns, initialized, 0) && !initialized,
        "zero render FPS must preserve the uncapped original presentation path");
    require(ShouldPresentGameplayTargetFrame(1000000000ull,
            next_present_ns, initialized, 60),
        "the first frame must present immediately");
    require(!ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns - 1,
            next_present_ns, initialized, 60),
        "a frame before the 60 Hz deadline must stay presentation-only idle");
    require(ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns,
            next_present_ns, initialized, 60),
        "the 60 Hz deadline must publish a frame");
    require(ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns * 5,
            next_present_ns, initialized, 60),
        "a late renderer must skip missed deadlines without catch-up redraws");
    require(!ShouldPresentGameplayTargetFrame(1000000000ull + interval_ns * 5,
            next_present_ns, initialized, 60),
        "a skipped deadline must not redraw twice at the same clock value");

    constexpr u64 simulation_interval_ns = 45000000ull;
    require(GameplayRenderInterpolationAlpha(2000000000ull, 2000000000ull,
            simulation_interval_ns) == 0,
        "a new simulation snapshot must begin at its previous position");
    require(GameplayRenderInterpolationAlpha(2022500000ull, 2000000000ull,
            simulation_interval_ns) == kGameplayRenderInterpolationOne / 2,
        "half a simulation interval must produce a half interpolation step");
    require(GameplayRenderInterpolationAlpha(2045000000ull, 2000000000ull,
            simulation_interval_ns) == kGameplayRenderInterpolationOne,
        "a complete simulation interval must reach the authoritative target");

    UnitRenderQueueContext render_queue{};
    UnitRenderItem moving{};
    moving.x = 100;
    moving.y = 200;
    moving.interpolation_start_x = 100;
    moving.interpolation_start_y = 200;
    moving.interpolation_target_x = 112;
    moving.interpolation_target_y = 192;
    moving.interpolated_x = moving.x;
    moving.interpolated_y = moving.y;
    moving.interpolation_enabled = true;
    render_queue.units.push_back(moving);

    ApplyUnitRenderInterpolation(
        render_queue, kGameplayRenderInterpolationOne / 2);
    require(render_queue.units[0].interpolated_x == 106 &&
            render_queue.units[0].interpolated_y == 196,
        "the renderer must interpolate only the draw coordinates");
    require(render_queue.units[0].x == 100 && render_queue.units[0].y == 200,
        "interpolation must not alter visibility or sort coordinates");
    require(render_queue.units[0].interpolation_target_x == 112 &&
            render_queue.units[0].interpolation_target_y == 192,
        "interpolation must preserve the authoritative render snapshot");

    ApplyUnitRenderInterpolation(render_queue, kGameplayRenderInterpolationOne);
    require(render_queue.units[0].interpolated_x == 112 &&
            render_queue.units[0].interpolated_y == 192,
        "the final visual frame must land exactly on the simulation snapshot");

    require(kVisualAnimationIntermediateFrameCount == 11,
        "exact 60-to-144 animation must retain eleven intermediate phases");
    require(!ShouldInterpolateVisualAnimation(0) &&
            !ShouldInterpolateVisualAnimation(60) &&
            ShouldInterpolateVisualAnimation(144),
        "animation frame synthesis must leave the original and 60 Hz paths unchanged");
    require(QuantizeVisualAnimationSubframe(0) == 0 &&
            QuantizeVisualAnimationSubframe(kVisualAnimationInterpolationOne / 2) == 6 &&
            QuantizeVisualAnimationSubframe(kVisualAnimationInterpolationOne) == 12,
        "visual animation alpha must select all twelve source phases");
    constexpr std::array<u32, 12> expected_polyphase{
        0, 5, 10, 3, 8, 1, 6, 11, 4, 9, 2, 7};
    for (u32 frame = 0; frame < expected_polyphase.size(); ++frame) {
        require(VisualAnimationSixtyHzPhaseForPresentationFrame(frame) ==
                expected_polyphase[frame],
            "60-to-144 presentation phases must follow the exact 12/5 cycle");
    }

    ResetVisualAnimationTransitionCache();
    VisualAnimationTransitionKey animation_key{};
    animation_key.runtime_slot_index = 0x100;
    animation_key.type_id = 3;
    animation_key.sequence = 1;
    animation_key.image_group = 1;
    animation_key.direction_row = 2;

    VisualAnimationTransitionSelection visual = ResolveVisualAnimationTransition(
        animation_key, 10, 1000, 40, kVisualAnimationInterpolationOne);
    require(!visual.interpolate && visual.endpoint_entry == 10,
        "the first observed animation pose must remain an exact original sprite");

    visual = ResolveVisualAnimationTransition(animation_key, 20, 2000, 41, 0);
    require(!visual.interpolate && visual.endpoint_entry == 10,
        "a new simulation pose must begin on the prior authoritative sprite");

    visual = ResolveVisualAnimationTransition(animation_key, 20, 2000, 41,
        kVisualAnimationInterpolationOne / 2);
    require(visual.interpolate && visual.source_entry == 10 &&
            visual.target_entry == 20 && visual.subframe_index == 6,
        "the midpoint must be a render-only pixel morph of consecutive sprites");

    visual = ResolveVisualAnimationTransition(animation_key, 20, 2000, 41,
        kVisualAnimationInterpolationOne);
    require(!visual.interpolate && visual.endpoint_entry == 20,
        "the final visual pose must be the exact current simulation sprite");

    animation_key.direction_row = 5;
    visual = ResolveVisualAnimationTransition(animation_key, 30, 3000, 42,
        kVisualAnimationInterpolationOne / 2);
    require(!visual.interpolate && visual.endpoint_entry == 30 &&
            VisualAnimationTransitionCacheSize() == 2,
        "a direction-row change must not interpolate pixels from another view");

    visual = ResolveVisualAnimationTransition(animation_key, 40, 4000, 44, 0);
    require(!visual.interpolate && visual.endpoint_entry == 40,
        "a skipped simulation frame must snap instead of blending stale poses");
    require(VisualAnimationTransitionCacheSize() == 2,
        "visual animation state must isolate independent direction rows");

    test_palette_slot_lifetime();
    test_pre_generated_pixel_morph_sprites();

    std::cout << "144 fps render pacing regression: PASS\n";
    return EXIT_SUCCESS;
}
