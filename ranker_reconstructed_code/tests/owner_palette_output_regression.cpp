#include "ranker_directx.h"
#include "ranker_palette_cache.h"
#include "ranker_resource_store.h"
#include "ranker_sprite_renderer.h"
#include "ranker_trc.h"
#include "ranker_ui_overlay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32
const DirectDrawRuntimeState& direct_draw_state() {
    static const DirectDrawRuntimeState state{};
    return state;
}
#endif

} // namespace ranker

namespace {

using namespace ranker;

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void set_raw_rgb(std::array<u8, kPaletteRawBytesPerSlot>& raw, u32 index,
    u8 red, u8 green, u8 blue) {
    const std::size_t base = static_cast<std::size_t>(index) * 4u;
    raw[base] = red;
    raw[base + 1] = green;
    raw[base + 2] = blue;
    raw[base + 3] = 0;
}

std::array<u8, kPaletteRawBytesPerSlot> make_owner_palette_record() {
    std::array<u8, kPaletteRawBytesPerSlot> raw{};
    // JW2_01.TRC record 5 reserves sixteen 0x10-entry owner rows.  Owners
    // 0..8 have ten visible ramp words, while the shipped 9..15 rows are
    // zero-filled and must remain zero instead of taking a fallback colour.
    for (u32 owner = 0; owner < 9; ++owner) {
        for (u32 index = 0; index < 10; ++index) {
            set_raw_rgb(raw, owner * 0x10u + index,
                static_cast<u8>(32u + owner * 24u),
                static_cast<u8>(16u + index * 20u),
                static_cast<u8>(232u - owner * 24u));
        }
    }
    return raw;
}

class TemporaryTrcArchive {
public:
    explicit TemporaryTrcArchive(
        const std::array<u8, kPaletteRawBytesPerSlot>& palette) {
        const auto nonce = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("ranker-owner-palette-" + std::to_string(nonce) + ".trc");

        std::vector<TrcWriteRecord> records(6);
        for (u32 index = 0; index < 5; ++index) {
            records[index].name = "padding" + std::to_string(index);
            records[index].payload = {static_cast<u8>(index)};
            records[index].method = 0;
        }
        records[5].name = "palette5";
        records[5].payload.assign(palette.begin(), palette.end());
        records[5].method = 0;
        require(WriteTrcRecords(path_.string().c_str(), records, 6),
            "could not write the synthetic six-record TRC archive");
    }

    TemporaryTrcArchive(const TemporaryTrcArchive&) = delete;
    TemporaryTrcArchive& operator=(const TemporaryTrcArchive&) = delete;

    ~TemporaryTrcArchive() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

u32 allocate_owner_ramp_sprite(u32 palette_slot) {
    std::vector<u8> payload{10, 0};
    for (u32 token = 0xf5; token <= 0xfe; ++token) {
        payload.push_back(static_cast<u8>(token));
    }

    u32 entry = kInvalidResourceEntry;
    void* destination = nullptr;
    require(AllocateResourceEntry(payload.size(), &entry, &destination) &&
            destination != nullptr,
        "could not allocate the owner-ramp sprite");
    std::memcpy(destination, payload.data(), payload.size());

    std::array<u32, 6> metadata{};
    metadata[0] = 10;
    metadata[1] = 1;
    require(ConfigureResourceEntry(entry, metadata, palette_slot),
        "could not configure the owner-ramp sprite");
    return entry;
}

const UiOverlayMinimapMarker* marker_for_owner(
    const UiOverlayState& state, u32 owner) {
    const auto it = std::find_if(state.minimap_markers.begin(),
        state.minimap_markers.end(), [&](const UiOverlayMinimapMarker& marker) {
            return marker.kind == UiOverlayMinimapMarkerKind::active_unit &&
                marker.owner_id == owner;
        });
    return it == state.minimap_markers.end() ? nullptr : &*it;
}

void verify_trc_palette_loader_contract(
    const std::array<u8, kPaletteRawBytesPerSlot>& expected_raw,
    const TemporaryTrcArchive& archive) {
    ResetPaletteCache();
    const std::string archive_path = archive.path().string();
    const u32 slot = LoadPaletteCacheTrcRecord(archive_path.c_str(), 5);
    require(slot == 0,
        "TRC record 5 was not installed as the unit-ramp source slot");
    require(palette_cache_state().next_slot == 1,
        "palette loader did not commit exactly one cache slot");
    require(palette_cache_state().raw_slots[slot] == expected_raw,
        "TRC record 5 bytes changed at the loader/palette-cache boundary");

    const auto& pixels = palette_cache_state().pixel_slots[slot];
    for (u32 owner = 0; owner < 16; ++owner) {
        bool row_nonzero = false;
        for (u32 index = 0; index < 10; ++index) {
            row_nonzero = row_nonzero || pixels[owner * 0x10u + index] != 0;
        }
        require(row_nonzero == (owner < 9),
            "owner palette visibility rows no longer match the record-5 contract");
    }
}

void verify_world_owner_actual_pixels() {
    const u32 sprite_palette_slot = AllocatePaletteCacheSlot();
    require(sprite_palette_slot == 1,
        "world sprite palette must follow the source owner-ramp slot");
    std::array<u8, kPaletteRawBytesPerSlot> sprite_raw{};
    require(SetPaletteCacheRawSlot(sprite_palette_slot,
            sprite_raw.data(), sprite_raw.size()),
        "could not install the synthetic world sprite palette");
    ConvertPaletteCacheSlot(sprite_palette_slot);
    RefreshPaletteTransparentMask();

    ResetResourceStore();
    const u32 sprite_entry = allocate_owner_ramp_sprite(sprite_palette_slot);
    std::vector<u16> output(16u * 10u, 0xffffu);
    SetSpriteRenderTarget(output.data(), 10, 16, 10);

    for (u32 owner = 0; owner < 16; ++owner) {
        SetSpriteUnitPaletteRamp(static_cast<u8>(owner));
        require(DrawResourceSpriteUnitRampToken1Shadow(
                sprite_entry, 0, static_cast<i32>(owner)),
            "owner-ramp sprite draw failed");
    }

    const auto& source = palette_cache_state().pixel_slots[0];
    for (u32 owner = 0; owner < 16; ++owner) {
        for (u32 index = 0; index < 10; ++index) {
            require(output[owner * 10u + index] ==
                    source[owner * 0x10u + index],
                "world sprite pixel did not use its owner's record-5 row");
        }
    }
    require(!std::equal(output.begin(), output.begin() + 10,
            output.begin() + 10),
        "owners zero and one collapsed to the same world pixels");
    require(std::all_of(output.begin() + 90, output.end(),
            [](u16 pixel) { return pixel == 0; }),
        "zero-filled owner rows 9..15 acquired a fallback world colour");
    ClearSpriteRenderTarget();
}

void verify_minimap_owner_actual_colors() {
    const auto& source = palette_cache_state().pixel_slots[0];
    UiOverlayState state{};
    state.local_player_slot = 0;
    state.minimap_local_unit_color = 0x1234;
    state.minimap_local_footprint_color = 0x2345;
    state.map_width_tiles = 16;
    state.map_height_tiles = 1;
    state.minimap.map_width_tiles = 16;
    state.minimap.map_height_tiles = 1;
    state.minimap.minimap_width_pixels = 160;
    state.minimap.minimap_height_pixels = 10;

    for (u32 owner = 0; owner < 16; ++owner) {
        UiOverlayMinimapUnit unit{};
        unit.unit_id = owner + 1;
        unit.type_id = 1;
        unit.owner_id = owner;
        unit.world_x = static_cast<i32>(owner * 0x20u);
        state.minimap_units.push_back(unit);
    }
    RenderMinimapUnitMarkers(state);
    require(state.minimap_markers.size() == 16,
        "minimap did not publish one marker per visible owner");
    for (u32 owner = 0; owner < 16; ++owner) {
        const UiOverlayMinimapMarker* marker = marker_for_owner(state, owner);
        require(marker != nullptr, "minimap owner marker is missing");
        const u16 expected = owner == 0 ? state.minimap_local_unit_color :
            source[owner * 0x10u];
        require(marker->color == expected,
            "minimap unit marker did not use its owner's palette word zero");
    }
    require(marker_for_owner(state, 0)->color != marker_for_owner(state, 1)->color,
        "local and remote minimap units collapsed to one colour");
    require(marker_for_owner(state, 9)->color == 0,
        "owner nine minimap unit acquired a fallback colour");

    state.minimap_markers.clear();
    state.minimap_units.clear();
    state.minimap_visibility_flags.assign(16, 0x18000000u);
    state.minimap_definition_footprints.resize(2);
    state.minimap_definition_footprints[1] = {0, 0, 1, 1};
    for (u32 owner = 0; owner < 16; ++owner) {
        state.minimap_object_flags.push_back(1u | (owner << 8));
    }
    RenderMinimapObjectAndTerrainMarkers(state);
    require(state.minimap_markers.size() == 16,
        "minimap did not publish one footprint per visible owner");
    for (u32 owner = 0; owner < 16; ++owner) {
        const u16 expected = owner == 0 ? state.minimap_local_footprint_color :
            source[owner * 0x10u + 3u];
        require(state.minimap_markers[owner].color == expected,
            "minimap footprint did not use its owner's palette word three");
    }
    require(state.minimap_markers[9].color == 0,
        "owner nine footprint acquired a fallback colour");
}

} // namespace

int main() {
    const auto raw_palette = make_owner_palette_record();
    const TemporaryTrcArchive archive(raw_palette);
    verify_trc_palette_loader_contract(raw_palette, archive);
    verify_world_owner_actual_pixels();
    verify_minimap_owner_actual_colors();
    std::cout << "OWNER_PALETTE_OUTPUT_PASS "
                 "trc_record=5 owners=0..15 world_pixels=160 minimap=unit+footprint\n";
    return EXIT_SUCCESS;
}
