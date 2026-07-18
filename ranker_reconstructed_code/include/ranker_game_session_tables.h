#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

struct PlayerSlotRuntimeState;
struct MilesComputedStreamContext;
struct TrcWriteRecord;

constexpr u32 kGameSessionOwnerCount = 8;
constexpr u32 kGameSessionUnitTypeCount = 0xaa;
constexpr u32 kGameSessionProductionVariantStride = 0x100;
constexpr u32 kGameSessionCompletionReverseCount = 0x40;
constexpr u32 kGameSessionUnitReferenceCapacity = 16;
constexpr u32 kGameSessionSmallReferenceCapacity = 16;
constexpr u32 kPostInitUnitRequirementCount = 4;
// ranker.exe: DAT_00705030 and DAT_00705020, consumed by FUN_004abbe0.
// The fourth pair is intentionally retained even though type 48 does not
// contain type 159 in the pristine JW2_09 table: transition modes append it.
inline constexpr std::array<u32, kPostInitUnitRequirementCount>
    kPostInitUnitTypes{{0, 16, 32, 48}};
inline constexpr std::array<u32, kPostInitUnitRequirementCount>
    kPostInitRequiredTypes{{111, 127, 143, 159}};
constexpr u32 kPostInitTransitionSnapshotBytes = 0x4f10;
constexpr u32 kGameSessionAvatarPlayerCount = 0x14;
constexpr u32 kGameSessionAvatarSlotCount = 8;
constexpr u32 kGameSessionAvatarRecordBytes = 0x74;
constexpr u32 kGameSessionAvatarPlayerBytes = 0x3f4;
constexpr u32 kGameSessionAvatarRuntimeBytes =
    kGameSessionAvatarPlayerCount * kGameSessionAvatarPlayerBytes;
constexpr u32 kGameSessionAvatarInvalidMarkerOffset = 0x14;
constexpr u32 kGameSessionAvatarMaxHealthOffset = 0x18;
constexpr u32 kGameSessionAvatarMaxSecondaryOffset = 0x1c;
constexpr u32 kGameSessionAvatarStat1cOffset = 0x20;
constexpr u32 kGameSessionAvatarStat20Offset = 0x24;
constexpr u32 kGameSessionAvatarLevelOffset = 0x28;
constexpr u32 kGameSessionAvatarProgressOffset = 0x2c;
constexpr u32 kGameSessionAvatarPrimaryEquipmentOffset = 0x34;
constexpr u32 kGameSessionAvatarSecondaryEquipmentOffset = 0x38;
constexpr u32 kGameSessionAvatarPickupEffectOffset = 0x3c;
constexpr u32 kInvalidGameSessionUnitType = 0xffffffffu;
constexpr u32 kSessionRuntimeBufferPairCount = 5;
constexpr u32 kSessionRuntimeForcesRecordIndex = 0;
constexpr u32 kSessionRuntimeForcesArchiveRecordIndex = 0x1a;
constexpr u32 kSessionRuntimeForcesFixedRecordBytes = 0x110;
constexpr u32 kSessionRuntimeForcesDirectoryGrowth = 0x32;
constexpr u16 kSessionRuntimeForcesStorageMethod = 2;
constexpr u32 kSessionRuntimeUserRecordFirstIndex = 0x14;
constexpr u32 kSessionRuntimeUserRecordCount = 5;

enum class SessionRuntimeOverrideLoadStatus : u32 {
    Failed = 0,
    Loaded = 1,
    RestoredDefaults = 2,
};

struct OwnerSessionCounterTables {
    std::array<std::array<u32, kGameSessionOwnerCount>, 7> tables{};
};

struct UnitTypeSessionDefinition {
    bool present = false;
    u32 primary_reference_count = 0;
    u32 alternate_reference_count = 0;
    u32 completion_reference_count = 0;
    u8 small_reference_count = 0;
    std::array<u32, kGameSessionUnitReferenceCapacity> primary_references{};
    std::array<u32, kGameSessionUnitReferenceCapacity> alternate_references{};
    std::array<u32, kGameSessionUnitReferenceCapacity> completion_references{};
    std::array<u8, kGameSessionSmallReferenceCapacity> small_references{};
};

struct GameSessionUnitReferenceTables {
    std::array<UnitTypeSessionDefinition, kGameSessionUnitTypeCount> definitions{};
    std::array<u32, kGameSessionUnitTypeCount> primary_or_alternate_reverse{};
    std::array<u32, kGameSessionCompletionReverseCount> completion_reverse{};
    std::array<u32, 0x100> small_reverse{};
    std::array<u32, kPostInitUnitRequirementCount> post_init_unit_types =
        kPostInitUnitTypes;
    std::array<u32, kPostInitUnitRequirementCount> post_init_required_types =
        kPostInitRequiredTypes;
    bool post_init_transition_pending = false;
};

struct PostInitTransitionSnapshot {
    std::array<u8, kPostInitTransitionSnapshotBytes> active{};
    std::array<u8, kPostInitTransitionSnapshotBytes> saved{};
};

struct GameSessionAvatarRuntime {
    std::array<u8, kGameSessionAvatarRuntimeBytes> bytes{};
};

struct GameSessionAvatarRecord {
    std::string name;
    u32 unit_type = kInvalidGameSessionUnitType;
    u32 max_health = 0;
    u32 max_secondary_value = 0;
    u32 runtime_stat_1c = 0;
    u32 runtime_stat_20 = 0;
    u32 level = 0;
    u32 progress = 0;
    u32 primary_equipment = 0;
    u32 secondary_equipment = 0;
    std::array<u32, 4> pickup_effects{};
};

struct GameSessionAvatarProductionDefinition {
    u32 support_cost = 0;
    u32 build_ticks = 0;
    u32 resource_cost = 0;
};

struct RuntimeDefinitionRecord {
    std::vector<u8> bytes;
};

inline bool ApplyPostInitUnitRequirementToggleToRuntimeDefinitions(
    std::vector<RuntimeDefinitionRecord>& records, bool transition_pending) {
    constexpr std::size_t kPrimaryReferenceCountOffset = 0x240;
    constexpr std::size_t kPrimaryReferenceBaseOffset = 0x244;
    constexpr std::size_t kRequiredDefinitionBytes =
        kPrimaryReferenceBaseOffset +
        kGameSessionUnitReferenceCapacity * sizeof(u32);

    const auto read_u32 = [](const std::vector<u8>& bytes,
                              std::size_t offset) -> u32 {
        return static_cast<u32>(bytes[offset + 0]) |
            (static_cast<u32>(bytes[offset + 1]) << 8) |
            (static_cast<u32>(bytes[offset + 2]) << 16) |
            (static_cast<u32>(bytes[offset + 3]) << 24);
    };
    const auto write_u32 = [](std::vector<u8>& bytes,
                               std::size_t offset, u32 value) {
        bytes[offset + 0] = static_cast<u8>(value & 0xffu);
        bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xffu);
    };

    bool changed = false;
    for (u32 pair = 0; pair < kPostInitUnitRequirementCount; ++pair) {
        const u32 unit_type = kPostInitUnitTypes[pair];
        const u32 required_type = kPostInitRequiredTypes[pair];
        if (unit_type >= records.size() ||
            records[unit_type].bytes.size() < kRequiredDefinitionBytes) {
            continue;
        }

        std::vector<u8>& bytes = records[unit_type].bytes;
        const u32 raw_count = read_u32(bytes, kPrimaryReferenceCountOffset);
        const u32 count = raw_count < kGameSessionUnitReferenceCapacity ?
            raw_count : kGameSessionUnitReferenceCapacity;
        u32 index = 0;
        while (index < count &&
               read_u32(bytes, kPrimaryReferenceBaseOffset +
                       index * sizeof(u32)) != required_type) {
            ++index;
        }

        if (transition_pending) {
            if (index >= count && raw_count < kGameSessionUnitReferenceCapacity) {
                write_u32(bytes, kPrimaryReferenceBaseOffset +
                        raw_count * sizeof(u32), required_type);
                write_u32(bytes, kPrimaryReferenceCountOffset, raw_count + 1);
                changed = true;
            }
        }
        else if (index < count) {
            // FUN_004abbe0 clears the matched cell and decrements the count;
            // it does not compact the remaining array.
            write_u32(bytes, kPrimaryReferenceBaseOffset +
                    index * sizeof(u32), 0);
            write_u32(bytes, kPrimaryReferenceCountOffset, raw_count - 1);
            changed = true;
        }
    }
    return changed;
}

inline const std::vector<u8>* SelectLiveSessionUnitDefinitionBytes(
    const std::vector<RuntimeDefinitionRecord>& active_records, u32 unit_type,
    const std::vector<u8>* catalog_fallback,
    std::size_t minimum_complete_bytes) {
    if (unit_type < active_records.size()) {
        const std::vector<u8>& active = active_records[unit_type].bytes;
        if (active.size() >= minimum_complete_bytes) {
            return &active;
        }
    }
    return catalog_fallback;
}

constexpr std::size_t kSessionUnitDefinitionNameOffset = 0x10c;
constexpr std::size_t kSessionUnitDefinitionNameBytes = 0x40;

struct SessionUnitDefinitionNameField {
    bool present = false;
    std::string text;
};

inline SessionUnitDefinitionNameField ReadSessionUnitDefinitionNameField(
    const std::vector<u8>& bytes) {
    SessionUnitDefinitionNameField field{};
    if (bytes.size() < kSessionUnitDefinitionNameOffset +
            kSessionUnitDefinitionNameBytes) {
        return field;
    }
    field.present = true;
    const char* const name = reinterpret_cast<const char*>(
        bytes.data() + kSessionUnitDefinitionNameOffset);
    std::size_t length = 0;
    while (length < kSessionUnitDefinitionNameBytes && name[length] != '\0') {
        ++length;
    }
    field.text.assign(name, length);
    return field;
}

inline SessionUnitDefinitionNameField ReadSessionUnitDefinitionNameField(
    const RuntimeDefinitionRecord& record) {
    return ReadSessionUnitDefinitionNameField(record.bytes);
}

struct SessionRuntimeDefinitionTableSet {
    std::vector<RuntimeDefinitionRecord> fixed44_records;
    std::vector<RuntimeDefinitionRecord> unit_records;
    std::vector<RuntimeDefinitionRecord> production_order_records;
    std::vector<RuntimeDefinitionRecord> tail8_records;
    std::vector<RuntimeDefinitionRecord> tail4_records;
};

struct SessionRuntimeBufferPair {
    u32 record_size = 0;
    u32 count = 0;
    std::vector<u8> active;
    std::vector<u8> snapshot;
};

struct SessionRuntimeBufferPairs {
    std::array<SessionRuntimeBufferPair, kSessionRuntimeBufferPairCount> categories{};
};

using ProductionOrderCatchupCallback = void (*)(u32 owner, u32 order_id);
using SessionRuntimeBufferInitializeCallback = void (*)(SessionRuntimeBufferPairs& buffers);

struct SessionRuntimeImportState {
    std::array<std::array<u8, kGameSessionUnitTypeCount>, kGameSessionOwnerCount>
        owner_unit_availability{};
    std::array<std::array<u8, kGameSessionProductionVariantStride>, kGameSessionOwnerCount>
        owner_order_variants{};
    ProductionOrderCatchupCallback apply_production_completion = nullptr;
};

struct SessionRuntimeExportState {
    std::array<std::array<u8, kGameSessionProductionVariantStride>, kGameSessionOwnerCount>
        owner_order_variants{};
};

void ResetOwnerSessionCounterTables(OwnerSessionCounterTables& state);
u32 GetOwnerSessionCounterTotal(const OwnerSessionCounterTables& state, u32 owner);
void SetOwnerSessionCounterTable2Only(OwnerSessionCounterTables& state, u32 owner, u32 value);
void SnapshotPostInitTransitionRuntimeSnapshot(PostInitTransitionSnapshot& state);
void RestorePostInitTransitionRuntimeSnapshot(PostInitTransitionSnapshot& state);
void ApplyPostInitUnitRequirementToggle(GameSessionUnitReferenceTables& state);
void ResetGameSessionAvatarRuntime(GameSessionAvatarRuntime& state);
bool ReadGameSessionAvatarRecord(const GameSessionAvatarRuntime& state,
    u32 player_index, u32 avatar_slot, GameSessionAvatarRecord& record);
bool LoadGameSessionAvatarRuntimeRecord(GameSessionAvatarRuntime& state,
    const char* archive_name, u32 record_index);
bool AppendGameSessionAvatarRuntimeRecord(const GameSessionAvatarRuntime& state,
    const char* archive_name, const char* record_name = "AVATAR");
std::string BuildGameSessionAvatarDisplayName(const GameSessionAvatarRuntime& state,
    u32 player_index, u32 avatar_slot, const char* format = nullptr);
u32 CalculateGameSessionAvatarResourceCost(const GameSessionAvatarRuntime& state,
    u32 player_index, u32 avatar_slot,
    const std::vector<GameSessionAvatarProductionDefinition>& definitions);
u32 CalculateGameSessionAvatarBuildTicks(const GameSessionAvatarRuntime& state,
    u32 player_index, u32 avatar_slot,
    const std::vector<GameSessionAvatarProductionDefinition>& definitions);
u32 CalculateGameSessionAvatarBuildTicks(
    u32 avatar_id, u32 avatar_level, u32 base_ticks);
u32 GetGameSessionAvatarSupportCost(const GameSessionAvatarRuntime& state,
    u32 player_index, u32 avatar_slot,
    const std::vector<GameSessionAvatarProductionDefinition>& definitions);
bool IsGameSessionAvatarProductionAvailable();
void RebuildUnitTypeReverseReferenceTables(GameSessionUnitReferenceTables& state);
bool CopyFixed44SessionRuntimeRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ImportFixed44SessionRuntimeRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ExportUnitSessionRuntimeDefinitionRecord(u32 unit_type,
    RuntimeDefinitionRecord& destination, const RuntimeDefinitionRecord& source);
bool ImportUnitSessionRuntimeDefinitionRecord(SessionRuntimeImportState& state,
    u32 unit_type, RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ExportProductionOrderSessionRuntimeRecord(const SessionRuntimeExportState& state,
    u32 order_id, RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ImportProductionOrderSessionRuntimeRecord(SessionRuntimeImportState& state,
    u32 order_id, RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ExportEightDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ImportEightDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ExportFourDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ImportFourDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source);
bool ImportSessionRuntimeDefinitionTables(SessionRuntimeImportState& state,
    SessionRuntimeDefinitionTableSet& active,
    const SessionRuntimeDefinitionTableSet& staged);
bool ImportNonEmptySessionRuntimeDefinitionTables(SessionRuntimeImportState& state,
    SessionRuntimeDefinitionTableSet& active,
    const SessionRuntimeDefinitionTableSet& staged);
void RebuildSessionPlayerSlotMasksFromFixed44Records(PlayerSlotRuntimeState& slots,
    const std::vector<RuntimeDefinitionRecord>& fixed44_records);
void InitializeSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers);
bool ResizeSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers,
    u32 fixed44_count, u32 unit_count, u32 production_order_count,
    u32 tail44_count, u32 tail34_count,
    SessionRuntimeBufferInitializeCallback initialize_records = nullptr);
void ReleaseSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers);
void SnapshotSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers);
bool AppendForcesSessionRuntimeRecord(const char* archive_name,
    const std::vector<u8>& fixed44_records);
bool AppendForcesSessionRuntimeRecord(const char* archive_name,
    const SessionRuntimeBufferPairs& buffers);
bool AppendForcesSessionRuntimeWriteRecord(std::vector<TrcWriteRecord>& records,
    const SessionRuntimeBufferPairs& buffers);
bool LoadForcesSessionRuntimeRecord(const char* archive_name,
    SessionRuntimeBufferPairs& buffers, std::vector<u8>& fixed44_records);
bool AppendUserSessionRuntimeOverrideRecords(const char* archive_name,
    const SessionRuntimeBufferPairs& buffers);
bool AppendUserSessionRuntimeOverrideWriteRecords(
    std::vector<TrcWriteRecord>& records, const SessionRuntimeBufferPairs& buffers);
SessionRuntimeOverrideLoadStatus LoadUserForceSessionRuntimeOverrideRecord(
    const char* archive_name, SessionRuntimeBufferPairs& buffers);
bool LoadUserSessionRuntimeOverrideRecords(const char* archive_name,
    SessionRuntimeBufferPairs& buffers);
bool BuildDefaultSessionRuntimeStagingBuffers(SessionRuntimeBufferPairs& buffers,
    const SessionRuntimeDefinitionTableSet& live,
    const SessionRuntimeExportState& state);

}
