#pragma once

#include "ranker_types.h"

#include <array>
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
    std::array<u32, kPostInitUnitRequirementCount> post_init_unit_types{};
    std::array<u32, kPostInitUnitRequirementCount> post_init_required_types{};
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
