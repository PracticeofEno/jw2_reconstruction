#pragma once

#include "ranker_palette_cache.h"
#include "ranker_gameplay_sound.h"
#include "ranker_resource_store.h"
#include "ranker_types.h"
#include "ranker_trc.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kCommandThemeCount = 4;
constexpr u32 kCommandThemeRecordStride = 13;
constexpr u32 kRedAdjustedPaletteCount = 32;
constexpr u32 kUnitDefinitionResourceCount = 0xaa;
constexpr u32 kUnitDefinitionRecordBytes = 0x24bc;
constexpr u32 kUnitDefinitionImageGroupCount = 0x0e;
constexpr u32 kUnitDefinitionSoundGroupCount = 9;
constexpr u32 kJw212RuntimeCatalogCount = 0x3d;
constexpr u32 kJw211RuntimeCatalogCount = 0x2e;
constexpr u32 kAuxiliaryRuntimeCatalogRecordBytes = 0xadc;
constexpr u32 kGameplaySessionLoadRecordCapacity = 0x14;

enum class CommandPaletteKind : std::size_t {
    SmallCharacter = 0,
    Action = 1,
    Magic = 2,
    Upgrade = 3,
    Item = 4,
    Count = 5,
};

enum class CommandBlobKind : std::size_t {
    SmallCharacterTable = 0,
    MiddleCharacterTable = 1,
    ActionTable = 2,
    MagicTable = 3,
    UpgradeTable = 4,
    ItemTable = 5,
    Count = 6,
};

enum class RuntimeResourceFailureStage {
    None,
    Palette,
    TrcBlob,
    ImageResource,
    Resource,
};

enum class GameplaySessionLoadStage {
    None,
    Header,
    Record,
    CommandTheme,
    Interface,
    Complete,
};

struct PaletteSlotRef {
    u32 slot = kInvalidPaletteCacheSlot;
    const u16* pixels = nullptr;
};

struct RuntimeResourceFailure {
    RuntimeResourceFailureStage stage = RuntimeResourceFailureStage::None;
    std::string archive;
    u32 record_index = 0;
};

template <std::size_t Count>
constexpr std::array<u32, Count> InvalidResourceEntryArray() {
    std::array<u32, Count> entries{};
    for (std::size_t i = 0; i < Count; ++i) {
        entries[i] = kInvalidResourceEntry;
    }
    return entries;
}

struct CommandThemeResourceState {
    u32 theme_index = 0;
    u32 record_base_index = 0;
    u32 palette_rewind_slot = kInvalidPaletteCacheSlot;
    bool loaded = false;

    std::array<PaletteSlotRef, static_cast<std::size_t>(CommandPaletteKind::Count)> palettes{};
    std::array<PaletteSlotRef, static_cast<std::size_t>(CommandPaletteKind::Count)> grayscale_palettes{};
    std::array<PaletteSlotRef, kRedAdjustedPaletteCount> red_adjusted_palettes{};
    std::array<std::vector<u8>, static_cast<std::size_t>(CommandBlobKind::Count)> blobs{};

    RuntimeResourceFailure last_failure{};
};

struct InterfaceResourceState {
    u32 theme_index = 0;
    u32 record_base_index = 0;
    u32 resource_rewind_entry = kInvalidResourceEntry;
    u32 palette_rewind_slot = kInvalidPaletteCacheSlot;
    bool replay_controls_enabled = false;
    bool loaded = false;

    u32 shared_ui_palette_slot = kInvalidPaletteCacheSlot;
    PaletteSlotRef primary_palette{};
    u32 background_image_entry = kInvalidResourceEntry;
    u32 primary_resource_start = kInvalidResourceEntry;
    std::array<u32, 6> primary_resource_entries = InvalidResourceEntryArray<6>();

    u32 replay_timer_resource_start = kInvalidResourceEntry;
    std::array<u32, 8> replay_timer_resource_entries = InvalidResourceEntryArray<8>();
    PaletteSlotRef replay_control_palette{};
    u32 replay_control_resource_start = kInvalidResourceEntry;
    std::array<u32, 18> replay_control_resource_entries = InvalidResourceEntryArray<18>();

    RuntimeResourceFailure last_failure{};
};

struct PaletteResourceSequenceResult {
    PaletteSlotRef palette{};
    u32 resource_start = kInvalidResourceEntry;
    std::vector<u32> resource_entries;
    RuntimeResourceFailure last_failure{};
};

struct GameplayUiResourceState {
    bool loaded = false;
    PaletteResourceSequenceResult small_character;
    PaletteResourceSequenceResult green_numbers;
    PaletteResourceSequenceResult misc_icons;
    PaletteResourceSequenceResult command_ack;

    std::array<u32, 7> small_character_aliases = InvalidResourceEntryArray<7>();
    u32 green_numbers_start = kInvalidResourceEntry;
    u32 misc_icons_start = kInvalidResourceEntry;
    std::array<u32, 4> misc_icon_tail_aliases = InvalidResourceEntryArray<4>();
    u32 command_ack_start = kInvalidResourceEntry;

    RuntimeResourceFailure last_failure{};
};

struct Jw207ResourcePackState {
    bool loaded = false;

    PaletteResourceSequenceResult under_attack;
    PaletteResourceSequenceResult start_locations;
    std::array<PaletteResourceSequenceResult, 4> berry_groups{};
    std::array<PaletteResourceSequenceResult, 9> unit_groups{};
    std::array<PaletteResourceSequenceResult, 5> destruction_groups{};
    std::array<PaletteResourceSequenceResult, 22> debris_groups{};
    PaletteResourceSequenceResult item_group;

    u32 under_attack_start = kInvalidResourceEntry;
    u32 start_location_start = kInvalidResourceEntry;
    u32 berry_start = kInvalidResourceEntry;
    u32 unit_start = kInvalidResourceEntry;
    u32 destruction_start = kInvalidResourceEntry;
    u32 debris_start = kInvalidResourceEntry;
    u32 item_start = kInvalidResourceEntry;

    RuntimeResourceFailure last_failure{};
};

struct UnitDefinitionResourceRecord {
    bool loaded = false;
    u32 source_record_index = 0;
    u32 palette_slot = kInvalidPaletteCacheSlot;
    std::vector<u8> definition_bytes;
    std::array<u32, kUnitDefinitionImageGroupCount> first_image_entries =
        InvalidResourceEntryArray<kUnitDefinitionImageGroupCount>();
    std::array<u32, kUnitDefinitionImageGroupCount> image_group_offsets{};
    std::array<u32, kUnitDefinitionImageGroupCount> image_group_counts{};
    std::vector<u32> image_resource_entries;
    std::array<u32, kUnitDefinitionSoundGroupCount> first_sound_slots{};
};

struct UnitDefinitionResourceCatalogState {
    bool loaded = false;
    bool alternate_pack_active = false;
    u32 definition_record_size = kUnitDefinitionRecordBytes;
    std::array<u32, kUnitDefinitionResourceCount> definition_offsets{};
    std::array<UnitDefinitionResourceRecord, kUnitDefinitionResourceCount> records{};
    std::vector<u8> variant_metadata;
    RuntimeResourceFailure last_failure{};
};

struct AuxiliaryRuntimeCatalogRecord {
    bool loaded = false;
    u32 source_record_index = 0;
    u32 palette_slot = kInvalidPaletteCacheSlot;
    std::string display_name;
    std::vector<u8> definition_bytes;
    std::vector<u32> image_resource_entries;
    std::vector<u32> sound_slots;
    u32 image_count = 0;
    u32 sound_count = 0;
};

struct AuxiliaryRuntimeCatalogState {
    bool loaded = false;
    std::string archive_name;
    std::vector<AuxiliaryRuntimeCatalogRecord> records;
    RuntimeResourceFailure last_failure{};
};

struct GameplaySessionLoadState {
    bool loaded = false;
    std::string archive_name;
    u32 base_record_index = 0;
    u32 failed_relative_record = 0;
    GameplaySessionLoadStage last_stage = GameplaySessionLoadStage::None;
    std::array<std::vector<u8>, kGameplaySessionLoadRecordCapacity> records{};
    std::array<bool, kGameplaySessionLoadRecordCapacity> record_loaded{};
    std::array<bool, kGameplaySessionLoadRecordCapacity> record_size_matches{};
};

struct GameplaySessionExportRecordSpec {
    std::string name;
    u32 byte_count = 0;
    u16 original_method = 2;
};

struct GameplaySessionFixedRecordSpec {
    u32 relative_record = 0;
    std::string name;
    u32 byte_count = 0;
    u16 original_method = 2;
};

struct GameplaySessionExportState {
    bool saved = false;
    bool unsupported_compression = false;
    std::string archive_name;
    u32 failed_record = 0;
    u16 requested_method = 0;
    std::vector<GameplaySessionExportRecordSpec> specs;
};

void SetRuntimeResourceThemeIndex(u32 theme_index);
void SetReplayControlsEnabled(bool enabled);
void SetSharedUiPaletteSlot(u32 palette_slot);
bool EnsureSharedUiPaletteSlot();

void MarkInterfaceResourceRewindPoints();
bool LoadPaletteBoundResourceSequence(const char* archive_name, u32 palette_record_index,
    u32 total_record_count, PaletteResourceSequenceResult* result);
bool LoadCommandThemeResourcePack();
bool LoadInterfaceResourcePack();
bool LoadGameplayUiResourcePacks();
bool LoadJw207GameplayResourcePacks();
bool LoadUnitDefinitionResourceRecord(const char* archive_name, u32 source_record_index,
    u32 definition_id);
bool LoadUnitDefinitionResourceCatalog();
bool AppendLoadedUnitDefinitionResourceName(u32 unit_type, const char* suffix,
    std::size_t suffix_length);
bool SetLoadedUnitDefinitionResourceNameField(u32 unit_type, const u8* field,
    std::size_t field_length);
bool ReloadUnitImageResourcesFromJw209Archive();
bool ReloadUnitImageResourcesFromJw220Archive();
bool LoadJw212RuntimeCatalogRecord(const char* archive_name, u32 record_index,
    u32 catalog_index);
bool LoadJw212RuntimeCatalog(const char* archive_name = "JW2_12.TRC");
bool LoadJw211RuntimeCatalogRecord(const char* archive_name, u32 record_index,
    u32 catalog_index);
bool LoadJw211RuntimeCatalog(const char* archive_name = "JW2_11.TRC");
bool ReloadUnitResourcePackForCurrentVariant();
bool ToggleUnitResourcePackVariantAndReload(bool* setup_write_requested = nullptr);
u32 ResetLoadedUnitDefinitionConstructionTimers(u32 ticks = 10);
u32 GetUnitDefinitionImageResourceEntry(u32 unit_type, u32 image_group);
u32 GetUnitDefinitionImageFrameResourceEntry(
    u32 unit_type, u32 image_group, u32 frame_index);
u32 GetUnitDefinitionAnimationFrameResourceEntry(
    u32 unit_type, u32 image_group, u32 animation_frame,
    u32 frame_table_group = 0);
u32 GetUnitDefinitionAnimationRowFrameResourceEntry(
    u32 unit_type, u32 image_group, u32 animation_frame,
    u32 frame_table_group, u32 row_table_group, u32 row_index);
bool GetUnitDefinitionGameplaySoundProfile(u32 unit_type,
    GameplayUnitSoundDefinition& definition, GameplayUnitSoundBaseSlots& base_slots);
bool HandleGameplaySessionBundleImport(const char* archive_name, u32 base_record_index);
const std::vector<GameplaySessionFixedRecordSpec>& gameplay_session_import_specs();
const std::vector<GameplaySessionExportRecordSpec>& gameplay_session_export_specs();
bool HandleGameplaySessionBundleExport(const char* archive_name,
    const std::vector<TrcWriteRecord>& records, u16 requested_method = 0,
    u32 directory_growth = 0x32);
std::vector<u8>* MutableGameplaySessionLoadedRecord(u32 relative_record);

const CommandThemeResourceState& command_theme_resource_state();
const InterfaceResourceState& interface_resource_state();
const GameplayUiResourceState& gameplay_ui_resource_state();
const Jw207ResourcePackState& jw207_resource_pack_state();
const UnitDefinitionResourceCatalogState& unit_definition_resource_catalog_state();
const AuxiliaryRuntimeCatalogState& jw212_runtime_catalog_state();
const AuxiliaryRuntimeCatalogState& jw211_runtime_catalog_state();
const GameplaySessionLoadState& gameplay_session_load_state();
const GameplaySessionExportState& gameplay_session_export_state();

}
