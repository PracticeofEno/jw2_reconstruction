#include "ranker_runtime_resources.h"

#include "ranker_indexed_text_table.h"

#include "ranker_directx.h"
#include "ranker_miles.h"
#include "ranker_setup_data.h"
#include "ranker_trc.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ranker {
namespace {

constexpr char kJw201Archive[] = "JW2_01.TRC";
constexpr char kJw202Archive[] = "JW2_02.TRC";
constexpr char kJw207Archive[] = "JW2_07.TRC";
constexpr char kJw218Archive[] = "JW2_18.TRC";
constexpr u32 kSharedUiPaletteRecord = 5;
constexpr u32 kInterfaceThemeRecordBase = 0x133;
constexpr u32 kInterfaceThemeRecordStride = 8;
constexpr u32 kUnitDefinitionImageCountOffset = 0x2214;
constexpr u32 kUnitDefinitionSoundCountOffset = 0x2424;
constexpr u32 kUnitDefinitionConstructionTimerOffset = 0x18c;
constexpr std::size_t kUnitDefinitionNameOffset = 0x10c;
constexpr std::size_t kUnitDefinitionNameBytes = 0x40;
constexpr u32 kSetupUnitResourcePackVariantBit = 0x20;
constexpr std::size_t kUnitDefinitionAnimationFrameOffsetTableBase = 0x140c;
constexpr std::size_t kUnitDefinitionAnimationFrameOffsetTableStride = 0x100;
constexpr std::size_t kUnitDefinitionAnimationRowOffsetTableBase = 0x2248;
constexpr std::size_t kUnitDefinitionAnimationRowOffsetTableStride = 0x20;

CommandThemeResourceState g_command_theme_resources;
InterfaceResourceState g_interface_resources;
GameplayUiResourceState g_gameplay_ui_resources;
Jw207ResourcePackState g_jw207_resources;
UnitDefinitionResourceCatalogState g_unit_definition_resources;
AuxiliaryRuntimeCatalogState g_jw212_catalog;
AuxiliaryRuntimeCatalogState g_jw211_catalog;
std::vector<std::vector<u8>> g_jw212_pristine_definition_bytes;
std::vector<std::vector<u8>> g_jw211_pristine_definition_bytes;
GameplaySessionLoadState g_gameplay_session_load;
GameplaySessionExportState g_gameplay_session_export;

bool append_bounded_unit_definition_name(std::vector<u8>& bytes,
    const char* suffix, std::size_t suffix_length) {
    if (suffix == nullptr ||
        bytes.size() < kUnitDefinitionNameOffset + kUnitDefinitionNameBytes) {
        return false;
    }

    u8* const name = bytes.data() + kUnitDefinitionNameOffset;
    std::size_t name_length = 0;
    while (name_length < kUnitDefinitionNameBytes && name[name_length] != 0) {
        ++name_length;
    }
    if (name_length == kUnitDefinitionNameBytes) {
        name[kUnitDefinitionNameBytes - 1] = 0;
        return true;
    }

    const std::size_t append_length = std::min<std::size_t>(
        suffix_length, kUnitDefinitionNameBytes - name_length - 1);
    if (append_length != 0) {
        std::memcpy(name + name_length, suffix, append_length);
    }
    name[name_length + append_length] = 0;
    return true;
}

void append_runtime_resource_log(const char* format, ...) {
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

const std::vector<GameplaySessionExportRecordSpec>& default_session_export_specs() {
    static const std::vector<GameplaySessionExportRecordSpec> specs{
        {"JW2", 0x1c960, 2},
        {"P_MAP", 0x170, 2},
        {"P_SCENA", 0x2898, 2},
        {"P_PLAYE", 0x374, 2},
        {"P_UB", 0x550, 2},
        {"TRIGGERS", 0, 2},
        {"OBB", 0x15000, 2},
        {"OBC", 0xe8000, 2},
        {"OBI", 0x7800, 2},
        {"AVATAR", 0, 2},
        {"MAP1", 0x40000, 2},
        {"MAP2", 0x40000, 2},
        {"MAP3", 0x40000, 2},
        {"BGI", 0x40000, 2},
        {"FOGBGI", 0x40000, 2},
        {"BRUSH", 4, 0},
        {"HILL", 4, 0},
        {"FOGMAP3", 0x40000, 2},
        {"VSBGI", 0x40000, 2},
        {"AI", 0x9d80, 2},
    };
    return specs;
}

const std::vector<GameplaySessionFixedRecordSpec>& default_session_import_specs() {
    static const std::vector<GameplaySessionFixedRecordSpec> specs{
        {0, "JW2", 0x1c960, 2},
        {1, "P_MAP", 0x170, 2},
        {2, "P_SCENA", 0x2898, 2},
        {3, "P_PLAYE", 0x374, 2},
        {4, "P_UB", 0x550, 2},
        {5, "TRIGGERS", 0, 2},
        {6, "OBB", 0x15000, 2},
        {7, "OBC", 0xe8000, 2},
        {8, "OBI", 0x7800, 2},
        {9, "AVATAR", 0, 2},
        {10, "MAP1", 0x40000, 2},
        {11, "MAP2", 0x40000, 2},
        {12, "MAP3", 0x40000, 2},
        {13, "BGI", 0x40000, 2},
        {14, "FOGBGI", 0x40000, 2},
        {17, "FOGMAP3", 0x40000, 2},
        {18, "VSBGI", 0x40000, 2},
        {19, "AI", 0x9d80, 2},
    };
    return specs;
}

const GameplaySessionFixedRecordSpec* default_session_import_spec_for(
    u32 relative_record) {
    const auto& specs = default_session_import_specs();
    const auto it = std::find_if(specs.begin(), specs.end(),
        [relative_record](const GameplaySessionFixedRecordSpec& spec) {
            return spec.relative_record == relative_record;
        });
    return it != specs.end() ? &*it : nullptr;
}

u32 normalized_theme(u32 theme_index) {
    return theme_index < kCommandThemeCount ? theme_index : 0;
}

const u16* palette_pixels_for_slot(u32 slot) {
    if (slot >= kPaletteCacheSlotCount) {
        return nullptr;
    }
    return palette_cache_state().pixel_slots[slot].data();
}

PaletteSlotRef make_palette_ref(u32 slot) {
    return PaletteSlotRef{slot, palette_pixels_for_slot(slot)};
}

void set_failure(RuntimeResourceFailure& failure, RuntimeResourceFailureStage stage,
    const char* archive, u32 record_index) {
    failure.stage = stage;
    failure.archive = archive != nullptr ? archive : "";
    failure.record_index = record_index;
}

void clear_failure(RuntimeResourceFailure& failure) {
    failure.stage = RuntimeResourceFailureStage::None;
    failure.archive.clear();
    failure.record_index = 0;
}

u32 read_le_u32(const std::vector<u8>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    return static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

u32 read_le_u32(const u8* bytes) {
    return static_cast<u32>(bytes[0]) |
        (static_cast<u32>(bytes[1]) << 8) |
        (static_cast<u32>(bytes[2]) << 16) |
        (static_cast<u32>(bytes[3]) << 24);
}

void reset_command_loaded_state(u32 theme_index, u32 rewind_slot) {
    g_command_theme_resources.loaded = false;
    g_command_theme_resources.theme_index = theme_index;
    g_command_theme_resources.record_base_index = theme_index * kCommandThemeRecordStride;
    g_command_theme_resources.palette_rewind_slot = rewind_slot;
    clear_failure(g_command_theme_resources.last_failure);

    for (auto& ref : g_command_theme_resources.palettes) {
        ref = {};
    }
    for (auto& ref : g_command_theme_resources.grayscale_palettes) {
        ref = {};
    }
    for (auto& ref : g_command_theme_resources.red_adjusted_palettes) {
        ref = {};
    }
    for (auto& blob : g_command_theme_resources.blobs) {
        blob.clear();
    }
}

void reset_interface_loaded_state(u32 theme_index, bool replay_controls_enabled,
    u32 shared_ui_palette_slot, u32 resource_rewind_entry, u32 palette_rewind_slot) {
    g_interface_resources.loaded = false;
    g_interface_resources.theme_index = theme_index;
    g_interface_resources.record_base_index =
        kInterfaceThemeRecordBase + theme_index * kInterfaceThemeRecordStride;
    g_interface_resources.replay_controls_enabled = replay_controls_enabled;
    g_interface_resources.shared_ui_palette_slot = shared_ui_palette_slot;
    g_interface_resources.resource_rewind_entry = resource_rewind_entry;
    g_interface_resources.palette_rewind_slot = palette_rewind_slot;
    clear_failure(g_interface_resources.last_failure);

    g_interface_resources.primary_palette = {};
    g_interface_resources.background_image_entry = kInvalidResourceEntry;
    g_interface_resources.primary_resource_start = kInvalidResourceEntry;
    g_interface_resources.primary_resource_entries.fill(kInvalidResourceEntry);
    g_interface_resources.replay_timer_resource_start = kInvalidResourceEntry;
    g_interface_resources.replay_timer_resource_entries.fill(kInvalidResourceEntry);
    g_interface_resources.replay_control_palette = {};
    g_interface_resources.replay_control_resource_start = kInvalidResourceEntry;
    g_interface_resources.replay_control_resource_entries.fill(kInvalidResourceEntry);
}

void reset_sequence_result(PaletteResourceSequenceResult& result) {
    result.palette = {};
    result.resource_start = kInvalidResourceEntry;
    result.resource_entries.clear();
    clear_failure(result.last_failure);
}

void reset_gameplay_ui_loaded_state() {
    g_gameplay_ui_resources.loaded = false;
    reset_sequence_result(g_gameplay_ui_resources.small_character);
    reset_sequence_result(g_gameplay_ui_resources.green_numbers);
    reset_sequence_result(g_gameplay_ui_resources.misc_icons);
    reset_sequence_result(g_gameplay_ui_resources.command_ack);
    g_gameplay_ui_resources.small_character_aliases.fill(kInvalidResourceEntry);
    g_gameplay_ui_resources.green_numbers_start = kInvalidResourceEntry;
    g_gameplay_ui_resources.misc_icons_start = kInvalidResourceEntry;
    g_gameplay_ui_resources.misc_icon_tail_aliases.fill(kInvalidResourceEntry);
    g_gameplay_ui_resources.command_ack_start = kInvalidResourceEntry;
    clear_failure(g_gameplay_ui_resources.last_failure);
}

void reset_jw207_loaded_state() {
    g_jw207_resources.loaded = false;
    reset_sequence_result(g_jw207_resources.under_attack);
    reset_sequence_result(g_jw207_resources.start_locations);
    for (auto& sequence : g_jw207_resources.berry_groups) {
        reset_sequence_result(sequence);
    }
    for (auto& sequence : g_jw207_resources.unit_groups) {
        reset_sequence_result(sequence);
    }
    for (auto& sequence : g_jw207_resources.destruction_groups) {
        reset_sequence_result(sequence);
    }
    for (auto& sequence : g_jw207_resources.debris_groups) {
        reset_sequence_result(sequence);
    }
    reset_sequence_result(g_jw207_resources.item_group);

    g_jw207_resources.under_attack_start = kInvalidResourceEntry;
    g_jw207_resources.start_location_start = kInvalidResourceEntry;
    g_jw207_resources.berry_start = kInvalidResourceEntry;
    g_jw207_resources.unit_start = kInvalidResourceEntry;
    g_jw207_resources.destruction_start = kInvalidResourceEntry;
    g_jw207_resources.debris_start = kInvalidResourceEntry;
    g_jw207_resources.item_start = kInvalidResourceEntry;
    clear_failure(g_jw207_resources.last_failure);
}

void release_unit_definition_catalog_allocations() {
    if (g_unit_definition_resources.resource_store_start_entry !=
            kInvalidResourceEntry) {
        ReleaseResourceEntriesFrom(
            g_unit_definition_resources.resource_store_start_entry);
    }
    if (g_unit_definition_resources.palette_rewind_slot !=
            kInvalidPaletteCacheSlot) {
        ReleasePaletteCacheSlotsFrom(
            g_unit_definition_resources.palette_rewind_slot);
    }
#ifdef _WIN32
    if (g_unit_definition_resources.sound_rewind_slot != 0xffffffffu) {
        ReleaseDirectSoundBufferSlotsFrom(
            g_unit_definition_resources.sound_rewind_slot);
    }
#endif
}

void reset_unit_definition_catalog_state(bool keep_variant_mode) {
    const bool alternate_pack_active = keep_variant_mode &&
        g_unit_definition_resources.alternate_pack_active;
    g_unit_definition_resources.loaded = false;
    g_unit_definition_resources.alternate_pack_active = alternate_pack_active;
    g_unit_definition_resources.definition_record_size = kUnitDefinitionRecordBytes;
    g_unit_definition_resources.resource_store_start_entry =
        kInvalidResourceEntry;
    g_unit_definition_resources.resource_store_end_entry =
        kInvalidResourceEntry;
    g_unit_definition_resources.resource_store_tail_allocation_serial = 0;
    g_unit_definition_resources.palette_rewind_slot =
        kInvalidPaletteCacheSlot;
    g_unit_definition_resources.sound_rewind_slot = 0xffffffffu;
    g_unit_definition_resources.variant_metadata.clear();
    clear_failure(g_unit_definition_resources.last_failure);
    for (u32 i = 0; i < kUnitDefinitionResourceCount; ++i) {
        g_unit_definition_resources.definition_offsets[i] =
            i * kUnitDefinitionRecordBytes;
        UnitDefinitionResourceRecord& record = g_unit_definition_resources.records[i];
        record = UnitDefinitionResourceRecord{};
        record.source_record_index = i;
    }
}

void reset_auxiliary_catalog(AuxiliaryRuntimeCatalogState& state, const char* archive_name,
    u32 record_count) {
    state.loaded = false;
    state.archive_name = archive_name != nullptr ? archive_name : "";
    state.records.assign(record_count, AuxiliaryRuntimeCatalogRecord{});
    clear_failure(state.last_failure);
}

u32 auxiliary_catalog_image_count(const std::vector<u8>& bytes) {
    return read_le_u32(bytes, 0x200);
}

u32 auxiliary_catalog_sound_count(const std::vector<u8>& bytes) {
    return read_le_u32(bytes, 0x830);
}

bool load_embedded_palette_block_stream(TrcRecordReader& reader, u32& palette_slot,
    RuntimeResourceFailure& failure, const char* archive_name, u32 record_index) {
    std::array<u8, kPaletteRawBytesPerSlot> raw_palette{};
    if (!ReadOpenTrcRecordBytes(reader, raw_palette.data(), raw_palette.size())) {
        set_failure(failure, RuntimeResourceFailureStage::Palette, archive_name, record_index);
        return false;
    }

    const u32 slot = AllocatePaletteCacheSlot();
    if (slot == kInvalidPaletteCacheSlot ||
        !SetPaletteCacheRawSlot(slot, raw_palette.data(), raw_palette.size())) {
        set_failure(failure, RuntimeResourceFailureStage::Palette, archive_name, record_index);
        return false;
    }

    ConvertPaletteCacheSlot(slot);
    palette_slot = slot;
    return true;
}

bool load_embedded_image_resource_stream(TrcRecordReader& reader, u32 palette_slot,
    u32& entry_index, RuntimeResourceFailure& failure, const char* archive_name,
    u32 record_index) {
    std::array<u8, 0x20> header{};
    if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size())) {
        set_failure(failure, RuntimeResourceFailureStage::Resource, archive_name,
            record_index);
        return false;
    }

    std::array<u32, 6> metadata{};
    for (std::size_t j = 0; j < metadata.size(); ++j) {
        metadata[j] = read_le_u32(header.data() + j * sizeof(u32));
    }

    void* payload = nullptr;
    const u32 payload_size = read_le_u32(header.data() + 0x18);
    if (!AllocateResourceEntry(payload_size, &entry_index, &payload) ||
        !ReadOpenTrcRecordBytes(reader, payload, payload_size)) {
        set_failure(failure, RuntimeResourceFailureStage::Resource, archive_name,
            record_index);
        return false;
    }

    ConfigureResourceEntry(entry_index, metadata, palette_slot);
    return true;
}

bool skip_embedded_wave_stream(TrcRecordReader& reader) {
    std::array<u8, 12> riff_header{};
    if (!ReadOpenTrcRecordBytes(reader, riff_header.data(), riff_header.size())) {
        return false;
    }
    if (riff_header[0] != 'R' || riff_header[1] != 'I' ||
        riff_header[2] != 'F' || riff_header[3] != 'F' ||
        riff_header[8] != 'W' || riff_header[9] != 'A' ||
        riff_header[10] != 'V' || riff_header[11] != 'E') {
        return false;
    }

    const u32 riff_payload_size = read_le_u32(riff_header.data() + 4);
    if (riff_payload_size < 4) {
        return false;
    }

    std::size_t remaining = static_cast<std::size_t>(riff_payload_size) + 8u -
        riff_header.size();
    std::array<u8, 4096> scratch{};
    while (remaining != 0) {
        const std::size_t chunk = std::min<std::size_t>(remaining, scratch.size());
        if (!ReadOpenTrcRecordBytes(reader, scratch.data(), chunk)) {
            return false;
        }
        remaining -= chunk;
    }
    return true;
}

bool load_embedded_wave_slot_stream(TrcRecordReader& reader, u32& slot_out,
    RuntimeResourceFailure& failure, const char* archive_name, u32 record_index) {
#ifdef _WIN32
    slot_out = 0xffffffffu;
    if (direct_sound_state().active) {
        // Unit and auxiliary definition records carry their WAV payloads
        // inline after the image streams.  The original loader allocates a
        // real secondary-buffer slot for each entry and records the first
        // slot of every sound group.  Merely skipping the RIFF bytes left all
        // group bases at 0xffffffff/zero, so selection, combat, harvest, and
        // production-complete cues could resolve to the common UI bank.
        slot_out = LoadOpenTrcWaveIntoSoundBufferSlot(reader);
        if (slot_out == 0xffffffffu) {
            set_failure(failure, RuntimeResourceFailureStage::Resource,
                archive_name, record_index);
            return false;
        }
        return true;
    }
    if (!skip_embedded_wave_stream(reader)) {
        set_failure(failure, RuntimeResourceFailureStage::Resource, archive_name,
            record_index);
        return false;
    }
    return true;
#else
    (void)reader;
    (void)slot_out;
    (void)failure;
    (void)archive_name;
    (void)record_index;
    return true;
#endif
}

bool load_auxiliary_runtime_catalog_record(AuxiliaryRuntimeCatalogState& state,
    const char* archive_name, u32 record_index, u32 catalog_index) {
    if (catalog_index >= state.records.size()) {
        set_failure(state.last_failure, RuntimeResourceFailureStage::TrcBlob, archive_name,
            record_index);
        return false;
    }

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        state.records[catalog_index] = AuxiliaryRuntimeCatalogRecord{};
        set_failure(state.last_failure, RuntimeResourceFailureStage::TrcBlob, archive_name,
            record_index);
        return false;
    }

    AuxiliaryRuntimeCatalogRecord& record = state.records[catalog_index];
    record = AuxiliaryRuntimeCatalogRecord{};
    if (reader.entry.original_size < 0x14) {
        CloseTrcRecordReader(reader);
        return true;
    }

    record.loaded = true;
    record.source_record_index = record_index;

    ServeMilesSound();
    record.definition_bytes.assign(kAuxiliaryRuntimeCatalogRecordBytes, 0);
    if (!ReadOpenTrcRecordBytes(reader, record.definition_bytes.data(),
            record.definition_bytes.size())) {
        CloseTrcRecordReader(reader);
        record = AuxiliaryRuntimeCatalogRecord{};
        set_failure(state.last_failure, RuntimeResourceFailureStage::TrcBlob, archive_name,
            record_index);
        return false;
    }
    record.image_count = auxiliary_catalog_image_count(record.definition_bytes);
    record.sound_count = auxiliary_catalog_sound_count(record.definition_bytes);

    ServeMilesSound();
    const bool palette_expected =
        state.records.size() == kJw211RuntimeCatalogCount || record.image_count != 0;
    if (palette_expected &&
        !load_embedded_palette_block_stream(reader, record.palette_slot,
            state.last_failure, archive_name, record_index)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    ServeMilesSound();
    // FUN_005056xx records the current global image-resource stack base even
    // for a zero-image row.  Effect tables add their raw indices to this base;
    // Shoot sword, for example, intentionally reaches the following row.
    record.image_resource_base_entry = resource_store_state().next_entry;
    for (u32 i = 0; i < record.image_count; ++i) {
        ServeMilesSound();
        u32 entry_index = kInvalidResourceEntry;
        if (!load_embedded_image_resource_stream(reader, record.palette_slot, entry_index,
                state.last_failure, archive_name, record_index)) {
            CloseTrcRecordReader(reader);
            return false;
        }
        record.image_resource_entries.push_back(entry_index);
    }

#ifdef _WIN32
    if (record.sound_count != 0 && direct_sound_state().active) {
        SetNextSoundBufferStaticFlag();
    }
#endif
    for (u32 i = 0; i < record.sound_count; ++i) {
        ServeMilesSound();
        u32 slot = 0;
        if (!load_embedded_wave_slot_stream(reader, slot, state.last_failure,
                archive_name, record_index)) {
            CloseTrcRecordReader(reader);
            return false;
        }
        record.sound_slots.push_back(slot);
    }
    CloseTrcRecordReader(reader);
    return true;
}

bool snapshot_auxiliary_runtime_definition_bytes(
    const AuxiliaryRuntimeCatalogState& state, std::size_t expected_count,
    std::vector<std::vector<u8>>& definitions) {
    definitions.clear();
    if (state.records.size() != expected_count) {
        return false;
    }
    definitions.reserve(state.records.size());
    for (const AuxiliaryRuntimeCatalogRecord& record : state.records) {
        if (!record.loaded) {
            if (!record.definition_bytes.empty()) {
                definitions.clear();
                return false;
            }
            // The original allocates the complete live table up front.  TRC
            // sentinel records (original size < 0x14) therefore remain valid
            // zero-filled 0xadc rows even though no auxiliary resources were
            // loaded for them.
            definitions.emplace_back(kAuxiliaryRuntimeCatalogRecordBytes, 0);
            continue;
        }
        if (record.definition_bytes.size() !=
            kAuxiliaryRuntimeCatalogRecordBytes) {
            definitions.clear();
            return false;
        }
        definitions.push_back(record.definition_bytes);
    }
    return true;
}

template <std::size_t TailCount>
bool validate_auxiliary_session_tail_definition_bytes(
    const AuxiliaryRuntimeCatalogState& state, std::size_t expected_count,
    const std::vector<std::vector<u8>>& definitions,
    const std::vector<std::vector<u8>>& pristine_definitions,
    const std::array<std::size_t, TailCount>& tail_offsets) {
    if (!state.loaded || state.records.size() != expected_count ||
        definitions.size() != expected_count ||
        pristine_definitions.size() != expected_count) {
        return false;
    }
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const std::vector<u8>& definition = definitions[index];
        const std::vector<u8>& pristine = pristine_definitions[index];
        if (definition.size() != kAuxiliaryRuntimeCatalogRecordBytes ||
            pristine.size() != kAuxiliaryRuntimeCatalogRecordBytes) {
            return false;
        }
        for (std::size_t offset = 0; offset < definition.size(); ++offset) {
            const bool session_tail_byte = std::any_of(
                tail_offsets.begin(), tail_offsets.end(),
                [offset](std::size_t tail_offset) {
                    return offset >= tail_offset &&
                        offset < tail_offset + sizeof(u32);
                });
            if (!session_tail_byte && definition[offset] != pristine[offset]) {
                return false;
            }
        }
    }
    return true;
}

void commit_auxiliary_runtime_definition_bytes(
    AuxiliaryRuntimeCatalogState& state,
    std::vector<std::vector<u8>>& definitions) {
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        // Map runtime overrides change only the live 0xadc definition row.
        // Palette/image/sound allocations remain owned by the pristine TRC
        // record exactly as they do in the original live-table copies.
        state.records[index].definition_bytes.swap(definitions[index]);
    }
}

void reset_gameplay_session_load_state(const char* archive_name, u32 base_record_index) {
    g_gameplay_session_load.loaded = false;
    g_gameplay_session_load.archive_name = archive_name != nullptr ? archive_name : "";
    g_gameplay_session_load.base_record_index = base_record_index;
    g_gameplay_session_load.failed_relative_record = 0;
    g_gameplay_session_load.last_stage = GameplaySessionLoadStage::None;
    for (auto& record : g_gameplay_session_load.records) {
        record.clear();
    }
    g_gameplay_session_load.record_loaded.fill(false);
    g_gameplay_session_load.record_size_matches.fill(false);
}

void reset_gameplay_session_export_state(const char* archive_name, u16 requested_method) {
    g_gameplay_session_export.saved = false;
    g_gameplay_session_export.unsupported_compression = false;
    g_gameplay_session_export.archive_name = archive_name != nullptr ? archive_name : "";
    g_gameplay_session_export.failed_record = 0;
    g_gameplay_session_export.requested_method = requested_method;
    g_gameplay_session_export.specs = default_session_export_specs();
}

bool load_palette_ref(const char* archive, u32 record_index, PaletteSlotRef& out,
    RuntimeResourceFailure& failure) {
    const u32 slot = LoadPaletteCacheTrcRecord(archive, record_index);
    if (slot == kInvalidPaletteCacheSlot) {
        set_failure(failure, RuntimeResourceFailureStage::Palette, archive, record_index);
        return false;
    }

    out = make_palette_ref(slot);
    return true;
}

bool load_trc_blob(const char* archive, u32 record_index, std::vector<u8>& out,
    RuntimeResourceFailure& failure) {
    if (!LoadTrcRecordAlloc(archive, record_index, out)) {
        set_failure(failure, RuntimeResourceFailureStage::TrcBlob, archive, record_index);
        return false;
    }
    return true;
}

bool load_image_resource(const char* archive, u32 record_index, u32 palette_slot,
    u32& entry_out, RuntimeResourceFailure& failure) {
    const u32 entry = LoadImageResourceTrcRecord(archive, record_index);
    if (entry == kInvalidResourceEntry) {
        set_failure(failure, RuntimeResourceFailureStage::ImageResource, archive, record_index);
        return false;
    }
    SetResourceEntryPaletteSlot(entry, palette_slot);
    entry_out = entry;
    return true;
}

bool load_palette_bound_resource(const char* archive, u32 record_index, u32 palette_slot,
    u32& entry_out, RuntimeResourceFailure& failure) {
    const u32 entry = LoadResourceTrcRecord(archive, record_index);
    if (entry == kInvalidResourceEntry) {
        set_failure(failure, RuntimeResourceFailureStage::Resource, archive, record_index);
        return false;
    }
    SetResourceEntryPaletteSlot(entry, palette_slot);
    entry_out = entry;
    return true;
}

bool load_gameplay_session_record(u32 relative_record) {
    if (relative_record >= g_gameplay_session_load.records.size()) {
        return false;
    }
    g_gameplay_session_load.last_stage =
        relative_record == 0 ? GameplaySessionLoadStage::Header : GameplaySessionLoadStage::Record;
    g_gameplay_session_load.failed_relative_record = relative_record;
    const GameplaySessionFixedRecordSpec* spec =
        default_session_import_spec_for(relative_record);
    const u32 record_index = g_gameplay_session_load.base_record_index + relative_record;
    bool loaded = false;
    std::size_t loaded_size = 0;
    std::vector<u8>& record = g_gameplay_session_load.records[relative_record];
    if (spec != nullptr && spec->byte_count != 0) {
        record.assign(spec->byte_count, 0);
        loaded = LoadTrcRecordIntoBuffer(g_gameplay_session_load.archive_name.c_str(),
            record_index, record.data(), record.size(), &loaded_size);
        if (loaded) {
            record.resize(loaded_size);
        } else {
            record.clear();
        }
    } else {
        loaded = LoadTrcRecordAlloc(g_gameplay_session_load.archive_name.c_str(),
            record_index, record);
        loaded_size = record.size();
    }

    g_gameplay_session_load.record_loaded[relative_record] = loaded;
    if (loaded) {
        g_gameplay_session_load.record_size_matches[relative_record] =
            spec == nullptr || spec->byte_count == 0 ||
            loaded_size == spec->byte_count;
    }
    return loaded;
}

bool build_palette_variant(PaletteSlotRef& out, u32 source_slot, bool grayscale, i16 red_delta,
    RuntimeResourceFailure& failure, u32 source_record_index) {
    const u32 slot = grayscale ? BuildGrayscalePaletteVariant(source_slot) :
        BuildRedAdjustedPaletteVariant(source_slot, red_delta);
    if (slot == kInvalidPaletteCacheSlot) {
        set_failure(failure, RuntimeResourceFailureStage::Palette, kJw202Archive,
            source_record_index);
        return false;
    }

    out = make_palette_ref(slot);
    return true;
}

} // namespace

void SetRuntimeResourceThemeIndex(u32 theme_index) {
    const u32 normalized = normalized_theme(theme_index);
    g_command_theme_resources.theme_index = normalized;
    g_interface_resources.theme_index = normalized;
}

void SetReplayControlsEnabled(bool enabled) {
    g_interface_resources.replay_controls_enabled = enabled;
}

void SetSharedUiPaletteSlot(u32 palette_slot) {
    g_interface_resources.shared_ui_palette_slot =
        palette_slot < kPaletteCacheSlotCount ? palette_slot : kInvalidPaletteCacheSlot;
}

bool EnsureSharedUiPaletteSlot() {
    if (g_interface_resources.shared_ui_palette_slot < kPaletteCacheSlotCount &&
        palette_pixels_for_slot(g_interface_resources.shared_ui_palette_slot) != nullptr) {
        return true;
    }

    PaletteSlotRef ref{};
    RuntimeResourceFailure failure{};
    if (!load_palette_ref(kJw201Archive, kSharedUiPaletteRecord, ref, failure)) {
        g_interface_resources.last_failure = failure;
        return false;
    }

    g_interface_resources.shared_ui_palette_slot = ref.slot;
    return true;
}

void MarkInterfaceResourceRewindPoints() {
    g_interface_resources.resource_rewind_entry = resource_store_state().next_entry;
    g_interface_resources.palette_rewind_slot = palette_cache_state().next_slot;
}

bool LoadPaletteBoundResourceSequence(const char* archive_name, u32 palette_record_index,
    u32 total_record_count, PaletteResourceSequenceResult* result) {
    if (result == nullptr || archive_name == nullptr || total_record_count == 0) {
        return false;
    }

    reset_sequence_result(*result);
    auto& failure = result->last_failure;
    if (!load_palette_ref(archive_name, palette_record_index, result->palette, failure)) {
        return false;
    }

    result->resource_start = resource_store_state().next_entry;
    result->resource_entries.reserve(total_record_count - 1);
    for (u32 i = 1; i < total_record_count; ++i) {
        u32 entry = kInvalidResourceEntry;
        if (!load_palette_bound_resource(archive_name, palette_record_index + i,
                result->palette.slot, entry, failure)) {
            return false;
        }
        result->resource_entries.push_back(entry);
    }

    return true;
}

bool LoadCommandThemeResourcePack() {
    const u32 theme_index = normalized_theme(g_command_theme_resources.theme_index);
    if (g_command_theme_resources.palette_rewind_slot != kInvalidPaletteCacheSlot) {
        ReleasePaletteCacheSlotsFrom(g_command_theme_resources.palette_rewind_slot);
    }

    const u32 rewind_slot = palette_cache_state().next_slot;
    reset_command_loaded_state(theme_index, rewind_slot);
    const u32 base_record = g_command_theme_resources.record_base_index;
    auto& failure = g_command_theme_resources.last_failure;

    auto& palettes = g_command_theme_resources.palettes;
    auto& blobs = g_command_theme_resources.blobs;
    if (!load_palette_ref(kJw202Archive, base_record + 0,
            palettes[static_cast<std::size_t>(CommandPaletteKind::SmallCharacter)], failure)) {
        return false;
    }
    if (!load_trc_blob(kJw202Archive, base_record + 1,
            blobs[static_cast<std::size_t>(CommandBlobKind::SmallCharacterTable)], failure)) {
        return false;
    }
    if (!load_trc_blob(kJw202Archive, base_record + 2,
            blobs[static_cast<std::size_t>(CommandBlobKind::MiddleCharacterTable)], failure)) {
        return false;
    }
    if (!load_palette_ref(kJw202Archive, base_record + 3,
            palettes[static_cast<std::size_t>(CommandPaletteKind::Action)], failure)) {
        return false;
    }
    if (!load_trc_blob(kJw202Archive, base_record + 4,
            blobs[static_cast<std::size_t>(CommandBlobKind::ActionTable)], failure)) {
        return false;
    }
    if (!load_palette_ref(kJw202Archive, base_record + 5,
            palettes[static_cast<std::size_t>(CommandPaletteKind::Magic)], failure)) {
        return false;
    }
    if (!load_trc_blob(kJw202Archive, base_record + 6,
            blobs[static_cast<std::size_t>(CommandBlobKind::MagicTable)], failure)) {
        return false;
    }
    if (!load_palette_ref(kJw202Archive, base_record + 9,
            palettes[static_cast<std::size_t>(CommandPaletteKind::Upgrade)], failure)) {
        return false;
    }
    if (!load_trc_blob(kJw202Archive, base_record + 10,
            blobs[static_cast<std::size_t>(CommandBlobKind::UpgradeTable)], failure)) {
        return false;
    }
    if (!load_palette_ref(kJw202Archive, base_record + 11,
            palettes[static_cast<std::size_t>(CommandPaletteKind::Item)], failure)) {
        return false;
    }
    if (!load_trc_blob(kJw202Archive, base_record + 12,
            blobs[static_cast<std::size_t>(CommandBlobKind::ItemTable)], failure)) {
        return false;
    }

    for (std::size_t i = 0; i < palettes.size(); ++i) {
        if (!build_palette_variant(g_command_theme_resources.grayscale_palettes[i],
                palettes[i].slot, true, 0, failure, base_record)) {
            return false;
        }
    }

    g_command_theme_resources.red_adjusted_palettes[0] =
        palettes[static_cast<std::size_t>(CommandPaletteKind::SmallCharacter)];
    for (u32 i = 1; i < kRedAdjustedPaletteCount; ++i) {
        if (!build_palette_variant(g_command_theme_resources.red_adjusted_palettes[i],
                rewind_slot, false, static_cast<i16>(i), failure, base_record)) {
            return false;
        }
    }

    g_command_theme_resources.loaded = true;
    return true;
}

bool LoadInterfaceResourcePack() {
    const u32 theme_index = normalized_theme(g_interface_resources.theme_index);
    const bool replay_controls_enabled = g_interface_resources.replay_controls_enabled;
    u32 shared_ui_palette_slot = g_interface_resources.shared_ui_palette_slot;

    if (g_interface_resources.resource_rewind_entry != kInvalidResourceEntry) {
        ReleaseResourceEntriesFrom(g_interface_resources.resource_rewind_entry);
    }
    if (g_interface_resources.palette_rewind_slot != kInvalidPaletteCacheSlot) {
        if (shared_ui_palette_slot >= g_interface_resources.palette_rewind_slot) {
            shared_ui_palette_slot = kInvalidPaletteCacheSlot;
        }
        ReleasePaletteCacheSlotsFrom(g_interface_resources.palette_rewind_slot);
    }

    reset_interface_loaded_state(theme_index, replay_controls_enabled, shared_ui_palette_slot,
        resource_store_state().next_entry, palette_cache_state().next_slot);
    if (!EnsureSharedUiPaletteSlot()) {
        return false;
    }

    reset_interface_loaded_state(theme_index, replay_controls_enabled,
        g_interface_resources.shared_ui_palette_slot, resource_store_state().next_entry,
        palette_cache_state().next_slot);

    const u32 base_record = g_interface_resources.record_base_index;
    auto& failure = g_interface_resources.last_failure;
    if (!load_palette_ref(kJw202Archive, base_record, g_interface_resources.primary_palette,
            failure)) {
        return false;
    }

    u32 record_index = base_record + 1;
    if (!load_image_resource(kJw202Archive, record_index, g_interface_resources.primary_palette.slot,
            g_interface_resources.background_image_entry, failure)) {
        return false;
    }

    g_interface_resources.primary_resource_start = resource_store_state().next_entry;
    for (std::size_t i = 0; i < g_interface_resources.primary_resource_entries.size(); ++i) {
        ++record_index;
        if (!load_palette_bound_resource(kJw202Archive, record_index,
                g_interface_resources.primary_palette.slot,
                g_interface_resources.primary_resource_entries[i], failure)) {
            return false;
        }
    }

    g_interface_resources.replay_timer_resource_start = resource_store_state().next_entry;
    for (std::size_t i = 0; i < g_interface_resources.replay_timer_resource_entries.size(); ++i) {
        if (!load_palette_bound_resource(kJw218Archive, static_cast<u32>(i),
                g_interface_resources.shared_ui_palette_slot,
                g_interface_resources.replay_timer_resource_entries[i], failure)) {
            return false;
        }
    }

    if (replay_controls_enabled) {
        if (!load_palette_ref(kJw218Archive, 8, g_interface_resources.replay_control_palette,
                failure)) {
            return false;
        }

        g_interface_resources.replay_control_resource_start = resource_store_state().next_entry;
        for (std::size_t i = 0; i < g_interface_resources.replay_control_resource_entries.size();
             ++i) {
            const u32 replay_record = static_cast<u32>(i) + 9;
            if (!load_palette_bound_resource(kJw218Archive, replay_record,
                    g_interface_resources.replay_control_palette.slot,
                    g_interface_resources.replay_control_resource_entries[i], failure)) {
                return false;
            }
        }
    }

    g_interface_resources.loaded = true;
    return true;
}

bool LoadGameplayUiResourcePacks() {
    reset_gameplay_ui_loaded_state();

    auto copy_failure = [](const PaletteResourceSequenceResult& sequence,
                           RuntimeResourceFailure& target) {
        target = sequence.last_failure;
    };

    if (!LoadPaletteBoundResourceSequence(kJw202Archive, 0x8e, 0x32,
            &g_gameplay_ui_resources.small_character)) {
        copy_failure(g_gameplay_ui_resources.small_character, g_gameplay_ui_resources.last_failure);
        return false;
    }
    for (std::size_t i = 0; i < g_gameplay_ui_resources.small_character_aliases.size(); ++i) {
        g_gameplay_ui_resources.small_character_aliases[i] =
            g_gameplay_ui_resources.small_character.resource_start + static_cast<u32>(i);
    }

    if (!LoadPaletteBoundResourceSequence(kJw202Archive, 0xc0, 0x41,
            &g_gameplay_ui_resources.green_numbers)) {
        copy_failure(g_gameplay_ui_resources.green_numbers, g_gameplay_ui_resources.last_failure);
        return false;
    }
    g_gameplay_ui_resources.green_numbers_start =
        g_gameplay_ui_resources.green_numbers.resource_start;

    if (!LoadPaletteBoundResourceSequence(kJw202Archive, 0x101, 0x29,
            &g_gameplay_ui_resources.misc_icons)) {
        copy_failure(g_gameplay_ui_resources.misc_icons, g_gameplay_ui_resources.last_failure);
        return false;
    }
    g_gameplay_ui_resources.misc_icons_start = g_gameplay_ui_resources.misc_icons.resource_start;
    for (std::size_t i = 0; i < g_gameplay_ui_resources.misc_icon_tail_aliases.size(); ++i) {
        g_gameplay_ui_resources.misc_icon_tail_aliases[i] =
            g_gameplay_ui_resources.misc_icons.resource_start + 0x20u + static_cast<u32>(i);
    }

    if (!LoadPaletteBoundResourceSequence(kJw202Archive, 0x12a, 9,
            &g_gameplay_ui_resources.command_ack)) {
        copy_failure(g_gameplay_ui_resources.command_ack, g_gameplay_ui_resources.last_failure);
        return false;
    }
    g_gameplay_ui_resources.command_ack_start =
        g_gameplay_ui_resources.command_ack.resource_start;

    g_gameplay_ui_resources.loaded = true;
    return true;
}

bool LoadJw207GameplayResourcePacks() {
    reset_jw207_loaded_state();

    auto fail_from = [](const PaletteResourceSequenceResult& sequence) {
        g_jw207_resources.last_failure = sequence.last_failure;
        return false;
    };

    ServeMilesSound();
    if (!LoadPaletteBoundResourceSequence(kJw207Archive, 2, 9,
            &g_jw207_resources.under_attack)) {
        return fail_from(g_jw207_resources.under_attack);
    }
    g_jw207_resources.under_attack_start = g_jw207_resources.under_attack.resource_start;

    ServeMilesSound();
    if (!LoadPaletteBoundResourceSequence(kJw207Archive, 0x0b, 9,
            &g_jw207_resources.start_locations)) {
        return fail_from(g_jw207_resources.start_locations);
    }
    g_jw207_resources.start_location_start =
        g_jw207_resources.start_locations.resource_start;

    ServeMilesSound();
    for (std::size_t i = 0; i < g_jw207_resources.berry_groups.size(); ++i) {
        const u32 record_index = 0x14u + static_cast<u32>(i) * 5u;
        if (!LoadPaletteBoundResourceSequence(kJw207Archive, record_index, 5,
                &g_jw207_resources.berry_groups[i])) {
            return fail_from(g_jw207_resources.berry_groups[i]);
        }
        if (i == 0) {
            g_jw207_resources.berry_start =
                g_jw207_resources.berry_groups[i].resource_start;
        }
    }

    ServeMilesSound();
    for (std::size_t i = 0; i < g_jw207_resources.unit_groups.size(); ++i) {
        const u32 record_index = 0x28u + static_cast<u32>(i) * 0x40u;
        if (!LoadPaletteBoundResourceSequence(kJw207Archive, record_index, 0x40,
                &g_jw207_resources.unit_groups[i])) {
            return fail_from(g_jw207_resources.unit_groups[i]);
        }
        if (i == 0) {
            g_jw207_resources.unit_start = g_jw207_resources.unit_groups[i].resource_start;
        }
    }

    ServeMilesSound();
    for (std::size_t i = 0; i < g_jw207_resources.destruction_groups.size(); ++i) {
        const u32 record_index = 0x268u + static_cast<u32>(i) * 0x14u;
        if (!LoadPaletteBoundResourceSequence(kJw207Archive, record_index, 0x14,
                &g_jw207_resources.destruction_groups[i])) {
            return fail_from(g_jw207_resources.destruction_groups[i]);
        }
        if (i == 0) {
            g_jw207_resources.destruction_start =
                g_jw207_resources.destruction_groups[i].resource_start;
        }
    }

    ServeMilesSound();
    for (std::size_t i = 0; i < g_jw207_resources.debris_groups.size(); ++i) {
        const u32 record_index = 0x2ccu + static_cast<u32>(i) * 4u;
        if (!LoadPaletteBoundResourceSequence(kJw207Archive, record_index, 4,
                &g_jw207_resources.debris_groups[i])) {
            return fail_from(g_jw207_resources.debris_groups[i]);
        }
        if (i == 0) {
            g_jw207_resources.debris_start =
                g_jw207_resources.debris_groups[i].resource_start;
        }
    }

    ServeMilesSound();
    if (!LoadPaletteBoundResourceSequence(kJw207Archive, 0x324, 0x79,
            &g_jw207_resources.item_group)) {
        return fail_from(g_jw207_resources.item_group);
    }
    g_jw207_resources.item_start = g_jw207_resources.item_group.resource_start;

    ServeMilesSound();
    if (!LoadGameplayUiResourcePacks()) {
        g_jw207_resources.last_failure = g_gameplay_ui_resources.last_failure;
        return false;
    }

    g_jw207_resources.loaded = true;
    return true;
}

bool LoadUnitDefinitionResourceRecord(const char* archive_name, u32 source_record_index,
    u32 definition_id) {
    if (archive_name == nullptr || definition_id >= kUnitDefinitionResourceCount) {
        return false;
    }

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, source_record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        set_failure(g_unit_definition_resources.last_failure,
            RuntimeResourceFailureStage::TrcBlob, archive_name, source_record_index);
        return false;
    }
    if (source_record_index < 3 || (source_record_index % 10u) == 0) {
        append_runtime_resource_log(
            "unit-def record=%lu open method=%u original_size=%lu",
            static_cast<unsigned long>(source_record_index),
            static_cast<unsigned>(reader.entry.method),
            static_cast<unsigned long>(reader.entry.original_size));
    }

    UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[definition_id];
    record = UnitDefinitionResourceRecord{};
    record.loaded = true;
    record.source_record_index = source_record_index;
    if (reader.entry.original_size < 0x14) {
        record.definition_bytes.assign(kUnitDefinitionRecordBytes, 0);
        CloseTrcRecordReader(reader);
        return true;
    }

    ServeMilesSound();
    record.definition_bytes.assign(kUnitDefinitionRecordBytes, 0);
    if (!ReadOpenTrcRecordBytes(reader, record.definition_bytes.data(),
            record.definition_bytes.size())) {
        CloseTrcRecordReader(reader);
        set_failure(g_unit_definition_resources.last_failure,
            RuntimeResourceFailureStage::TrcBlob, archive_name, source_record_index);
        return false;
    }

    // FUN_0040abc0 replaces the internal JW2_09 name immediately after the
    // 0x24bc definition body is read: FUN_00437fd0(definition_id) resolves
    // the localized JW2_17 row and FUN_0051e070 copies it to raw +0x10c.
    // Keeping the English archive name here leaks names such as TyranoNest
    // into the selected-unit HUD and into session-runtime name snapshots.
    const std::string_view localized_name = GetIndexedTextTableRow(
        StartupAuxiliaryIndexedTextTable(0), definition_id);
    if (!localized_name.empty()) {
        u8* const destination =
            record.definition_bytes.data() + kUnitDefinitionNameOffset;
        std::fill_n(destination, kUnitDefinitionNameBytes, 0);
        const std::size_t copy_size = std::min<std::size_t>(
            localized_name.size(), kUnitDefinitionNameBytes - 1);
        std::memcpy(destination, localized_name.data(), copy_size);
    }

    if (reader.entry.original_size == kUnitDefinitionRecordBytes) {
        std::fill_n(record.definition_bytes.begin(), sizeof(u32), 0);
        CloseTrcRecordReader(reader);
        return true;
    }

    ServeMilesSound();
    if (!load_embedded_palette_block_stream(reader, record.palette_slot,
            g_unit_definition_resources.last_failure, archive_name, source_record_index)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    u32 total_image_count = 0;
    for (u32 group = 0; group < kUnitDefinitionImageGroupCount; ++group) {
        total_image_count += read_le_u32(record.definition_bytes,
            kUnitDefinitionImageCountOffset + group * 4);
    }
    u32 total_sound_count = 0;
    for (u32 group = 0; group < kUnitDefinitionSoundGroupCount; ++group) {
        total_sound_count += read_le_u32(record.definition_bytes,
            kUnitDefinitionSoundCountOffset + group * 4);
    }
    if (source_record_index < 3 || (source_record_index % 10u) == 0) {
        append_runtime_resource_log(
            "unit-def record=%lu counts images=%lu sounds=%lu resource_next=%lu",
            static_cast<unsigned long>(source_record_index),
            static_cast<unsigned long>(total_image_count),
            static_cast<unsigned long>(total_sound_count),
            static_cast<unsigned long>(resource_store_state().next_entry));
    }

    for (u32 group = 0; group < kUnitDefinitionImageGroupCount; ++group) {
        const u32 image_count =
            read_le_u32(record.definition_bytes, kUnitDefinitionImageCountOffset + group * 4);
        record.image_group_offsets[group] =
            static_cast<u32>(record.image_resource_entries.size());
        record.image_group_counts[group] = image_count;
        for (u32 i = 0; i < image_count; ++i) {
            ServeMilesSound();
            u32 entry_index = kInvalidResourceEntry;
            if (!load_embedded_image_resource_stream(reader, record.palette_slot,
                    entry_index, g_unit_definition_resources.last_failure, archive_name,
                    source_record_index)) {
                CloseTrcRecordReader(reader);
                return false;
            }
            if (i == 0) {
                record.first_image_entries[group] = entry_index;
            }
            record.image_resource_entries.push_back(entry_index);
        }
    }
    if (source_record_index < 3 || (source_record_index % 10u) == 0) {
        append_runtime_resource_log(
            "unit-def record=%lu images done resource_next=%lu",
            static_cast<unsigned long>(source_record_index),
            static_cast<unsigned long>(resource_store_state().next_entry));
    }

#ifdef _WIN32
    if (direct_sound_state().active) {
        SetNextSoundBufferStaticFlag();
    }
#endif
    for (u32 group = 0; group < kUnitDefinitionSoundGroupCount; ++group) {
        const u32 sound_count =
            read_le_u32(record.definition_bytes, kUnitDefinitionSoundCountOffset + group * 4);
        for (u32 i = 0; i < sound_count; ++i) {
            ServeMilesSound();
            u32 slot = 0;
            if (!load_embedded_wave_slot_stream(reader, slot,
                    g_unit_definition_resources.last_failure, archive_name,
                    source_record_index)) {
                CloseTrcRecordReader(reader);
                return false;
            }
            if (i == 0) {
                record.first_sound_slots[group] = slot;
            }
        }
    }
    if (source_record_index < 3 || (source_record_index % 10u) == 0) {
        append_runtime_resource_log(
            "unit-def record=%lu sounds done resource_next=%lu",
            static_cast<unsigned long>(source_record_index),
            static_cast<unsigned long>(resource_store_state().next_entry));
    }

    CloseTrcRecordReader(reader);
    return true;
}

bool load_unit_variant_metadata_record() {
    std::vector<u8> metadata;
    if (!LoadTrcRecordAlloc("JW2_20.TRC", 0, metadata, 1)) {
        set_failure(g_unit_definition_resources.last_failure,
            RuntimeResourceFailureStage::TrcBlob, "JW2_20.TRC", 0);
        return false;
    }
    g_unit_definition_resources.variant_metadata = std::move(metadata);
    return true;
}

bool ReloadUnitImageResourcesFromJw209Archive() {
    if (!load_unit_variant_metadata_record()) {
        return false;
    }
    g_unit_definition_resources.alternate_pack_active = true;
    return true;
}

bool ReloadUnitImageResourcesFromJw220Archive() {
    if (!load_unit_variant_metadata_record()) {
        return false;
    }
    g_unit_definition_resources.alternate_pack_active = false;
    return true;
}

bool reload_unit_resource_pack_variant(bool alternate_pack_active) {
    return alternate_pack_active
        ? ReloadUnitImageResourcesFromJw209Archive()
        : ReloadUnitImageResourcesFromJw220Archive();
}

bool ReloadUnitResourcePackForCurrentVariant() {
    const u32 ui_flags = ImportSetupU32(kSetupUiFlagsOffset,
        g_unit_definition_resources.alternate_pack_active ?
            kSetupUnitResourcePackVariantBit : 0u);
    return reload_unit_resource_pack_variant(
        (ui_flags & kSetupUnitResourcePackVariantBit) != 0);
}

bool ToggleUnitResourcePackVariantAndReload(bool* setup_write_requested) {
    const u32 previous_flags = ImportSetupU32(kSetupUiFlagsOffset,
        g_unit_definition_resources.alternate_pack_active ?
            kSetupUnitResourcePackVariantBit : 0u);
    const u32 next_flags = previous_flags ^ kSetupUnitResourcePackVariantBit;
    if (!reload_unit_resource_pack_variant(
            (next_flags & kSetupUnitResourcePackVariantBit) != 0)) {
        g_unit_definition_resources.alternate_pack_active =
            (previous_flags & kSetupUnitResourcePackVariantBit) != 0;
        if (setup_write_requested != nullptr) {
            *setup_write_requested = false;
        }
        return false;
    }

    ExportSetupU32(kSetupUiFlagsOffset, next_flags);
    if (setup_write_requested != nullptr) {
        *setup_write_requested = true;
    }
    return true;
}

u32 ResetLoadedUnitDefinitionConstructionTimers(u32 ticks) {
    u32 reset_count = 0;
    for (UnitDefinitionResourceRecord& record : g_unit_definition_resources.records) {
        if (!record.loaded ||
            record.definition_bytes.size() < kUnitDefinitionConstructionTimerOffset + 4) {
            continue;
        }
        record.definition_bytes[kUnitDefinitionConstructionTimerOffset + 0] =
            static_cast<u8>(ticks & 0xffu);
        record.definition_bytes[kUnitDefinitionConstructionTimerOffset + 1] =
            static_cast<u8>((ticks >> 8) & 0xffu);
        record.definition_bytes[kUnitDefinitionConstructionTimerOffset + 2] =
            static_cast<u8>((ticks >> 16) & 0xffu);
        record.definition_bytes[kUnitDefinitionConstructionTimerOffset + 3] =
            static_cast<u8>((ticks >> 24) & 0xffu);
        ++reset_count;
    }
    return reset_count;
}

u32 GetUnitDefinitionImageResourceEntry(u32 unit_type, u32 image_group) {
    if (unit_type >= g_unit_definition_resources.records.size() ||
        image_group >= kUnitDefinitionImageGroupCount) {
        return kInvalidResourceEntry;
    }
    if (!UnitDefinitionResourceCatalogImageResourcesValid() &&
        !LoadUnitDefinitionResourceCatalog()) {
        return kInvalidResourceEntry;
    }
    const UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    if (!record.loaded) {
        return kInvalidResourceEntry;
    }
    return record.first_image_entries[image_group];
}

u32 GetUnitDefinitionImageFrameResourceEntry(
    u32 unit_type, u32 image_group, u32 frame_index) {
    if (unit_type >= g_unit_definition_resources.records.size() ||
        image_group >= kUnitDefinitionImageGroupCount) {
        return kInvalidResourceEntry;
    }
    if (!UnitDefinitionResourceCatalogImageResourcesValid() &&
        !LoadUnitDefinitionResourceCatalog()) {
        return kInvalidResourceEntry;
    }
    const UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    if (!record.loaded || frame_index >= record.image_group_counts[image_group]) {
        return kInvalidResourceEntry;
    }
    const std::size_t entry_index =
        static_cast<std::size_t>(record.image_group_offsets[image_group]) +
        frame_index;
    if (entry_index >= record.image_resource_entries.size()) {
        return kInvalidResourceEntry;
    }
    return record.image_resource_entries[entry_index];
}

bool GetUnitDefinitionImageFrameIndex(u32 unit_type, u32 image_group,
    u32 resource_entry, u32& frame_index) {
    frame_index = 0;
    if (unit_type >= g_unit_definition_resources.records.size() ||
        image_group >= kUnitDefinitionImageGroupCount ||
        resource_entry == kInvalidResourceEntry ||
        !UnitDefinitionResourceCatalogImageResourcesValid()) {
        return false;
    }
    const UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    if (!record.loaded) {
        return false;
    }
    const u32 group_offset = record.image_group_offsets[image_group];
    const u32 group_count = record.image_group_counts[image_group];
    if (group_offset > record.image_resource_entries.size() ||
        group_count > record.image_resource_entries.size() - group_offset) {
        return false;
    }
    for (u32 index = 0; index < group_count; ++index) {
        if (record.image_resource_entries[group_offset + index] ==
            resource_entry) {
            frame_index = index;
            return true;
        }
    }
    return false;
}

u32 GetUnitDefinitionAnimationFrameResourceEntry(
    u32 unit_type, u32 image_group, u32 animation_frame, u32 frame_table_group) {
    if (unit_type >= g_unit_definition_resources.records.size() ||
        image_group >= kUnitDefinitionImageGroupCount) {
        return kInvalidResourceEntry;
    }

    if (!UnitDefinitionResourceCatalogImageResourcesValid() &&
        !LoadUnitDefinitionResourceCatalog()) {
        return kInvalidResourceEntry;
    }

    const UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    const std::size_t offset =
        kUnitDefinitionAnimationFrameOffsetTableBase +
        static_cast<std::size_t>(frame_table_group) *
            kUnitDefinitionAnimationFrameOffsetTableStride +
        static_cast<std::size_t>(animation_frame) * sizeof(u32);
    if (!record.loaded || offset + sizeof(u32) > record.definition_bytes.size()) {
        return kInvalidResourceEntry;
    }

    const u32 frame_offset = read_le_u32(record.definition_bytes, offset);
    if (frame_offset >= record.image_group_counts[image_group]) {
        return kInvalidResourceEntry;
    }
    const std::size_t entry_index =
        static_cast<std::size_t>(record.image_group_offsets[image_group]) +
        frame_offset;
    if (entry_index >= record.image_resource_entries.size()) {
        return kInvalidResourceEntry;
    }
    return record.image_resource_entries[entry_index];
}

u32 GetUnitDefinitionAnimationRowFrameResourceEntry(
    u32 unit_type, u32 image_group, u32 animation_frame,
    u32 frame_table_group, u32 row_table_group, u32 row_index) {
    if (unit_type >= g_unit_definition_resources.records.size() ||
        image_group >= kUnitDefinitionImageGroupCount ||
        frame_table_group >= kUnitDefinitionImageGroupCount ||
        row_table_group >= kUnitDefinitionImageGroupCount) {
        return kInvalidResourceEntry;
    }

    if (!UnitDefinitionResourceCatalogImageResourcesValid() &&
        !LoadUnitDefinitionResourceCatalog()) {
        return kInvalidResourceEntry;
    }

    const UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    const std::size_t frame_offset =
        kUnitDefinitionAnimationFrameOffsetTableBase +
        static_cast<std::size_t>(frame_table_group) *
            kUnitDefinitionAnimationFrameOffsetTableStride +
        static_cast<std::size_t>(animation_frame) * sizeof(u32);
    const std::size_t row_offset =
        kUnitDefinitionAnimationRowOffsetTableBase +
        static_cast<std::size_t>(row_table_group) *
            kUnitDefinitionAnimationRowOffsetTableStride +
        static_cast<std::size_t>(row_index) * sizeof(u32);
    if (!record.loaded ||
        frame_offset + sizeof(u32) > record.definition_bytes.size() ||
        row_offset + sizeof(u32) > record.definition_bytes.size()) {
        return kInvalidResourceEntry;
    }

    const u32 frame_index =
        read_le_u32(record.definition_bytes, frame_offset) +
        read_le_u32(record.definition_bytes, row_offset);
    if (frame_index >= record.image_group_counts[image_group]) {
        return kInvalidResourceEntry;
    }
    const std::size_t entry_index =
        static_cast<std::size_t>(record.image_group_offsets[image_group]) +
        frame_index;
    if (entry_index >= record.image_resource_entries.size()) {
        return kInvalidResourceEntry;
    }
    return record.image_resource_entries[entry_index];
}

bool GetUnitDefinitionGameplaySoundProfile(u32 unit_type,
    GameplayUnitSoundDefinition& definition, GameplayUnitSoundBaseSlots& base_slots) {
    if (unit_type >= g_unit_definition_resources.records.size()) {
        return false;
    }

    const UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    if (!record.loaded || record.definition_bytes.empty() ||
        !DecodeGameplayUnitSoundDefinition(record.definition_bytes.data(),
            record.definition_bytes.size(), definition)) {
        return false;
    }

    std::array<u32, kGameplayUnitSoundGroupCount> first_sound_slots{};
    for (std::size_t i = 0; i < first_sound_slots.size() &&
        i < record.first_sound_slots.size(); ++i) {
        first_sound_slots[i] = record.first_sound_slots[i];
    }
    base_slots = BuildGameplayUnitSoundBaseSlots(first_sound_slots);
    return true;
}

bool LoadUnitDefinitionResourceCatalog() {
    // Session teardown rewinds image resources without rewinding the palette
    // and DirectSound stacks that were allocated by the same catalog.  Rewind
    // all three stacks to the catalog marks before a second-session reload.
    // Keeping the marks during a failed attempt also makes retries idempotent
    // instead of exhausting slots a little further on every render frame.
    release_unit_definition_catalog_allocations();
    reset_unit_definition_catalog_state(true);
    g_unit_definition_resources.resource_store_start_entry =
        resource_store_state().next_entry;
    g_unit_definition_resources.palette_rewind_slot =
        palette_cache_state().next_slot;
#ifdef _WIN32
    g_unit_definition_resources.sound_rewind_slot =
        direct_sound_state().next_allocated_slot;
#endif
    append_runtime_resource_log("unit-def catalog begin count=%lu",
        static_cast<unsigned long>(kUnitDefinitionResourceCount));
    for (u32 unit_type = 0; unit_type < kUnitDefinitionResourceCount; ++unit_type) {
        ServeMilesSound();
        if (!LoadUnitDefinitionResourceRecord("JW2_09.TRC", unit_type, unit_type)) {
            append_runtime_resource_log(
                "unit-def catalog failed record=%lu failure_stage=%lu failure_record=%lu",
                static_cast<unsigned long>(unit_type),
                static_cast<unsigned long>(g_unit_definition_resources.last_failure.stage),
                static_cast<unsigned long>(
                    g_unit_definition_resources.last_failure.record_index));
            g_unit_definition_resources.loaded = false;
            return false;
        }
        if (unit_type < 3 || (unit_type % 10u) == 0) {
            append_runtime_resource_log("unit-def catalog loaded record=%lu",
                static_cast<unsigned long>(unit_type));
        }
    }
    ServeMilesSound();
    g_unit_definition_resources.alternate_pack_active = true;
    const u32 ui_flags = ImportSetupU32(kSetupUiFlagsOffset, 0);
    if ((ui_flags & kSetupUnitResourcePackVariantBit) == 0) {
        (void)reload_unit_resource_pack_variant(false);
    }
    g_unit_definition_resources.loaded = true;
    g_unit_definition_resources.resource_store_end_entry =
        resource_store_state().next_entry;
    if (g_unit_definition_resources.resource_store_end_entry >
        g_unit_definition_resources.resource_store_start_entry) {
        g_unit_definition_resources.resource_store_tail_allocation_serial =
            GetResourceEntryAllocationSerial(
                g_unit_definition_resources.resource_store_end_entry - 1u);
    }
    append_runtime_resource_log("unit-def catalog ok");
    return true;
}

bool UnitDefinitionResourceCatalogImageResourcesValid() {
    if (!g_unit_definition_resources.loaded) {
        return false;
    }

    const u32 start =
        g_unit_definition_resources.resource_store_start_entry;
    const u32 end = g_unit_definition_resources.resource_store_end_entry;
    if (start == kInvalidResourceEntry || end == kInvalidResourceEntry ||
        end <= start || resource_store_state().next_entry < end) {
        return false;
    }

    const ResourceStoreEntry* tail = GetResourceEntry(end - 1u);
    return tail != nullptr && !tail->payload.empty() &&
        tail->allocation_serial ==
            g_unit_definition_resources.resource_store_tail_allocation_serial;
}

bool AppendLoadedUnitDefinitionResourceName(u32 unit_type, const char* suffix,
    std::size_t suffix_length) {
    if (unit_type >= g_unit_definition_resources.records.size() || suffix == nullptr) {
        return false;
    }
    if (!g_unit_definition_resources.loaded && !LoadUnitDefinitionResourceCatalog()) {
        return false;
    }

    UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    return record.loaded && append_bounded_unit_definition_name(
        record.definition_bytes, suffix, suffix_length);
}

bool SetLoadedUnitDefinitionResourceNameField(u32 unit_type, const u8* field,
    std::size_t field_length) {
    if (unit_type >= g_unit_definition_resources.records.size() || field == nullptr ||
        field_length < kUnitDefinitionNameBytes) {
        return false;
    }
    if (!g_unit_definition_resources.loaded && !LoadUnitDefinitionResourceCatalog()) {
        return false;
    }

    UnitDefinitionResourceRecord& record =
        g_unit_definition_resources.records[unit_type];
    if (!record.loaded || record.definition_bytes.size() <
            kUnitDefinitionNameOffset + kUnitDefinitionNameBytes) {
        return false;
    }
    std::memcpy(record.definition_bytes.data() + kUnitDefinitionNameOffset,
        field, kUnitDefinitionNameBytes);
    return true;
}

bool LoadJw212RuntimeCatalogRecord(const char* archive_name, u32 record_index,
    u32 catalog_index) {
    if (g_jw212_catalog.records.empty()) {
        reset_auxiliary_catalog(g_jw212_catalog, archive_name, kJw212RuntimeCatalogCount);
    }
    g_jw212_catalog.loaded = false;
    g_jw212_pristine_definition_bytes.clear();
    return load_auxiliary_runtime_catalog_record(g_jw212_catalog, archive_name, record_index,
        catalog_index);
}

bool LoadJw212RuntimeCatalog(const char* archive_name) {
    g_jw212_pristine_definition_bytes.clear();
    reset_auxiliary_catalog(g_jw212_catalog, archive_name, kJw212RuntimeCatalogCount);
    if (g_jw212_catalog.archive_name.empty()) {
        return false;
    }
    for (u32 index = 0; index < kJw212RuntimeCatalogCount; ++index) {
        ServeMilesSound();
        if (!LoadJw212RuntimeCatalogRecord(g_jw212_catalog.archive_name.c_str(), index,
                index)) {
            g_jw212_catalog.loaded = false;
            return false;
        }
    }
    g_jw212_catalog.loaded = true;
    if (!snapshot_auxiliary_runtime_definition_bytes(g_jw212_catalog,
            kJw212RuntimeCatalogCount, g_jw212_pristine_definition_bytes)) {
        g_jw212_catalog.loaded = false;
        return false;
    }
    return true;
}

bool CopyPristineJw212RuntimeDefinitionBytes(
    std::vector<std::vector<u8>>& definitions) {
    if (g_jw212_pristine_definition_bytes.size() !=
        kJw212RuntimeCatalogCount) {
        if (!g_jw212_catalog.loaded && !LoadJw212RuntimeCatalog()) {
            definitions.clear();
            return false;
        }
        if (!snapshot_auxiliary_runtime_definition_bytes(g_jw212_catalog,
                kJw212RuntimeCatalogCount,
                g_jw212_pristine_definition_bytes)) {
            definitions.clear();
            return false;
        }
    }
    definitions = g_jw212_pristine_definition_bytes;
    return true;
}

bool LoadJw211RuntimeCatalogRecord(const char* archive_name, u32 record_index,
    u32 catalog_index) {
    if (g_jw211_catalog.records.empty()) {
        reset_auxiliary_catalog(g_jw211_catalog, archive_name, kJw211RuntimeCatalogCount);
    }
    g_jw211_catalog.loaded = false;
    g_jw211_pristine_definition_bytes.clear();
    return load_auxiliary_runtime_catalog_record(g_jw211_catalog, archive_name, record_index,
        catalog_index);
}

bool LoadJw211RuntimeCatalog(const char* archive_name) {
    g_jw211_pristine_definition_bytes.clear();
    reset_auxiliary_catalog(g_jw211_catalog, archive_name, kJw211RuntimeCatalogCount);
    if (g_jw211_catalog.archive_name.empty()) {
        return false;
    }
    append_runtime_resource_log("jw211 catalog begin count=%lu archive=%s",
        static_cast<unsigned long>(kJw211RuntimeCatalogCount),
        g_jw211_catalog.archive_name.c_str());
    for (u32 index = 0; index < kJw211RuntimeCatalogCount; ++index) {
        ServeMilesSound();
        if (!LoadJw211RuntimeCatalogRecord(g_jw211_catalog.archive_name.c_str(), index,
                index)) {
            append_runtime_resource_log(
                "jw211 catalog record=%lu failed stage=%lu failure_record=%lu",
                static_cast<unsigned long>(index),
                static_cast<unsigned long>(g_jw211_catalog.last_failure.stage),
                static_cast<unsigned long>(g_jw211_catalog.last_failure.record_index));
            g_jw211_catalog.loaded = false;
            return false;
        }
    }
    g_jw211_catalog.loaded = true;
    if (!snapshot_auxiliary_runtime_definition_bytes(g_jw211_catalog,
            kJw211RuntimeCatalogCount, g_jw211_pristine_definition_bytes)) {
        g_jw211_catalog.loaded = false;
        return false;
    }
    append_runtime_resource_log("jw211 catalog ok");
    return true;
}

bool CopyPristineJw211RuntimeDefinitionBytes(
    std::vector<std::vector<u8>>& definitions) {
    if (g_jw211_pristine_definition_bytes.size() !=
        kJw211RuntimeCatalogCount) {
        if (!g_jw211_catalog.loaded && !LoadJw211RuntimeCatalog()) {
            definitions.clear();
            return false;
        }
        if (!snapshot_auxiliary_runtime_definition_bytes(g_jw211_catalog,
                kJw211RuntimeCatalogCount,
                g_jw211_pristine_definition_bytes)) {
            definitions.clear();
            return false;
        }
    }
    definitions = g_jw211_pristine_definition_bytes;
    return true;
}

bool PublishJw21xSessionRuntimeTailDefinitionBytes(
    std::vector<std::vector<u8>> jw212_definitions,
    std::vector<std::vector<u8>> jw211_definitions) {
    // Session publication is deliberately side-effect free on failure.  The
    // bootstrap/pristine-copy path must have prepared both catalogs first;
    // attempting a sequential lazy reload here could otherwise leave only
    // one resource catalog replaced when the second archive is damaged.
    if (!g_jw212_catalog.loaded || !g_jw211_catalog.loaded ||
        g_jw212_pristine_definition_bytes.size() !=
            kJw212RuntimeCatalogCount ||
        g_jw211_pristine_definition_bytes.size() !=
            kJw211RuntimeCatalogCount) {
        return false;
    }

    static constexpr std::array<std::size_t, 8> kJw212TailOffsets{
        0x170, 0x174, 0x178, 0x17c, 0x180, 0x184, 0x188, 0x18c,
    };
    static constexpr std::array<std::size_t, 4> kJw211TailOffsets{
        0x1e0, 0x1e4, 0x15c, 0x160,
    };
    if (!validate_auxiliary_session_tail_definition_bytes(g_jw212_catalog,
            kJw212RuntimeCatalogCount, jw212_definitions,
            g_jw212_pristine_definition_bytes, kJw212TailOffsets) ||
        !validate_auxiliary_session_tail_definition_bytes(g_jw211_catalog,
            kJw211RuntimeCatalogCount, jw211_definitions,
            g_jw211_pristine_definition_bytes, kJw211TailOffsets)) {
        return false;
    }

    // All validation and allocation happened before this point.  Vector swap
    // makes the two live-table commits non-throwing, so a failed second table
    // can never leave a mixed JW2_12/JW2_11 session catalog.
    commit_auxiliary_runtime_definition_bytes(
        g_jw212_catalog, jw212_definitions);
    commit_auxiliary_runtime_definition_bytes(
        g_jw211_catalog, jw211_definitions);
    return true;
}

bool HandleGameplaySessionBundleImport(const char* archive_name, u32 base_record_index) {
    reset_gameplay_session_load_state(archive_name, base_record_index);
    if (g_gameplay_session_load.archive_name.empty()) {
        return false;
    }

    if (!load_gameplay_session_record(0)) {
        return false;
    }
    const auto& header = g_gameplay_session_load.records[0];
    if (read_le_u32(header, 0) != 0x5241574a || read_le_u32(header, 4) != 0x97) {
        return false;
    }

    InitializeBriefingBinkMediaState();
    for (const GameplaySessionFixedRecordSpec& spec : default_session_import_specs()) {
        if (spec.relative_record == 0) {
            continue;
        }
        if (!load_gameplay_session_record(spec.relative_record)) {
            return false;
        }
    }
    if (!ResolveBriefingStartBinkSourceRecord(
            g_gameplay_session_load.archive_name.c_str()) ||
        !ResolveBriefingEndBinkSourceRecord(g_gameplay_session_load.archive_name.c_str()) ||
        !LoadMilesEffectPlaylistInfoFromTrc(
            g_gameplay_session_load.archive_name.c_str())) {
        return false;
    }
    SetBriefingBinkArchiveName(g_gameplay_session_load.archive_name.c_str());

    g_gameplay_session_load.last_stage = GameplaySessionLoadStage::CommandTheme;
    if (!LoadCommandThemeResourcePack()) {
        return false;
    }

    g_gameplay_session_load.last_stage = GameplaySessionLoadStage::Interface;
    if (!LoadInterfaceResourcePack()) {
        return false;
    }

    g_gameplay_session_load.last_stage = GameplaySessionLoadStage::Complete;
    g_gameplay_session_load.loaded = true;
    return true;
}

const std::vector<GameplaySessionFixedRecordSpec>& gameplay_session_import_specs() {
    return default_session_import_specs();
}

const std::vector<GameplaySessionExportRecordSpec>& gameplay_session_export_specs() {
    return default_session_export_specs();
}

bool HandleGameplaySessionBundleExport(const char* archive_name,
    const std::vector<TrcWriteRecord>& records, u16 requested_method,
    u32 directory_growth) {
    reset_gameplay_session_export_state(archive_name, requested_method);
    if (g_gameplay_session_export.archive_name.empty() || records.empty()) {
        return false;
    }
    if (!MaterializeBriefingBinkSourcesForArchive(
            g_gameplay_session_export.archive_name.c_str())) {
        return false;
    }

    std::vector<TrcWriteRecord> normalized = records;
    bool needs_zlib = false;
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (requested_method != 0) {
            normalized[i].method = requested_method;
        }
        if (normalized[i].method == 2) {
            needs_zlib = true;
        }
    }

    const u32 directory_slots =
        static_cast<u32>(normalized.size()) + std::max<u32>(directory_growth, 0);
    if (!WriteTrcRecords(archive_name, normalized, directory_slots)) {
        if (needs_zlib && !IsZlibRuntimeAvailable()) {
            g_gameplay_session_export.unsupported_compression = true;
        }
        return false;
    }
    if (!SaveBriefingStartBinkSourceToTrc(archive_name) ||
        !SaveBriefingEndBinkSourceToTrc(archive_name)) {
        return false;
    }
    if (!SaveMilesEffectPlaylistToTrc(archive_name)) {
        return false;
    }

    g_gameplay_session_export.saved = true;
    return true;
}

std::vector<u8>* MutableGameplaySessionLoadedRecord(u32 relative_record) {
    if (relative_record >= g_gameplay_session_load.records.size() ||
        !g_gameplay_session_load.record_loaded[relative_record]) {
        return nullptr;
    }
    return &g_gameplay_session_load.records[relative_record];
}

const CommandThemeResourceState& command_theme_resource_state() {
    return g_command_theme_resources;
}

const InterfaceResourceState& interface_resource_state() {
    return g_interface_resources;
}

const GameplayUiResourceState& gameplay_ui_resource_state() {
    return g_gameplay_ui_resources;
}

const Jw207ResourcePackState& jw207_resource_pack_state() {
    return g_jw207_resources;
}

const UnitDefinitionResourceCatalogState& unit_definition_resource_catalog_state() {
    return g_unit_definition_resources;
}

const AuxiliaryRuntimeCatalogState& jw212_runtime_catalog_state() {
    return g_jw212_catalog;
}

const AuxiliaryRuntimeCatalogState& jw211_runtime_catalog_state() {
    return g_jw211_catalog;
}

const GameplaySessionLoadState& gameplay_session_load_state() {
    return g_gameplay_session_load;
}

const GameplaySessionExportState& gameplay_session_export_state() {
    return g_gameplay_session_export;
}

}
