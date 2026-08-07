#include "ranker_game_session_tables.h"

#include "ranker_miles.h"
#include "ranker_player_slots.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace ranker {
namespace {

constexpr std::array<const char*, kSessionRuntimeUserRecordCount>
    kSessionRuntimeUserRecordNames{
        "US_FORCE", "US_UB", "US_UPG", "US_WEAPON", "US_MAGIC",
    };
constexpr std::array<u32, kSessionRuntimeUserRecordCount>
    kSessionRuntimeUserArchiveRecordIndices{
        0x14, 0x15, 0x16, 0x17, 0x18,
    };
constexpr u32 kGameSessionAvatarArchiveDirectoryGrowth = 0x14;
constexpr u16 kGameSessionAvatarArchiveStorageMethod = 2;
constexpr std::size_t kStartupAvatarDisplayNameFormatRow = 250;

u32 bounded_count(u32 count) {
    return std::min<u32>(count, kGameSessionUnitReferenceCapacity);
}

void remove_reference(UnitTypeSessionDefinition& definition, u32 required_type) {
    const u32 count = bounded_count(definition.primary_reference_count);
    for (u32 index = 0; index < count; ++index) {
        if (definition.primary_references[index] == required_type) {
            definition.primary_references[index] = 0;
            definition.primary_reference_count = count - 1;
            return;
        }
    }
}

void add_reference(UnitTypeSessionDefinition& definition, u32 required_type) {
    const u32 count = bounded_count(definition.primary_reference_count);
    for (u32 index = 0; index < count; ++index) {
        if (definition.primary_references[index] == required_type) {
            return;
        }
    }
    if (count < kGameSessionUnitReferenceCapacity) {
        definition.primary_references[count] = required_type;
        definition.primary_reference_count = count + 1;
    }
}

void fill_reverse_from_u32_list(std::array<u32, kGameSessionUnitTypeCount>& reverse,
    const std::array<u32, kGameSessionUnitReferenceCapacity>& values, u32 count,
    u32 owner_type) {
    const u32 limit = bounded_count(count);
    for (u32 index = 0; index < limit; ++index) {
        const u32 referenced_type = values[index];
        if (referenced_type < reverse.size()) {
            reverse[referenced_type] = owner_type;
        }
    }
}

void fill_completion_reverse_from_u32_list(
    std::array<u32, kGameSessionCompletionReverseCount>& reverse,
    const std::array<u32, kGameSessionUnitReferenceCapacity>& values, u32 count,
    u32 owner_type) {
    const u32 limit = bounded_count(count);
    for (u32 index = 0; index < limit; ++index) {
        const u32 referenced_type = values[index];
        if (referenced_type < reverse.size()) {
            reverse[referenced_type] = owner_type;
        }
    }
}

bool has_record_range(const RuntimeDefinitionRecord& record, std::size_t offset,
    std::size_t size) {
    return offset <= record.bytes.size() && size <= record.bytes.size() - offset;
}

bool ensure_record_range(RuntimeDefinitionRecord& record, std::size_t offset,
    std::size_t size) {
    if (offset > record.bytes.max_size() || size > record.bytes.max_size() - offset) {
        return false;
    }
    if (record.bytes.size() < offset + size) {
        record.bytes.resize(offset + size);
    }
    return true;
}

bool copy_live_fixed44_records_from_staged_buffer(
    std::vector<u8>& fixed44_records, const SessionRuntimeBufferPair& staged) {
    const std::size_t expected_size =
        static_cast<std::size_t>(staged.count) * staged.record_size;
    if (expected_size == 0) {
        fixed44_records.clear();
        return true;
    }
    if (staged.snapshot.size() < expected_size) {
        return false;
    }

    fixed44_records.resize(expected_size);
    for (u32 index = 0; index < staged.count; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index) * staged.record_size;
        std::memcpy(fixed44_records.data() + offset, staged.snapshot.data() + offset,
            staged.record_size);
    }
    return true;
}

u32 session_runtime_pair_byte_count(const SessionRuntimeBufferPair& pair) {
    return pair.count * pair.record_size;
}

bool ensure_snapshot_buffer_size(SessionRuntimeBufferPair& pair, u32 expected_size) {
    try {
        pair.snapshot.resize(expected_size);
    }
    catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool load_session_runtime_active_record(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, SessionRuntimeBufferPair& pair) {
    const u32 expected_size = session_runtime_pair_byte_count(pair);
    if (!LoadMilesTrcRecordIntoArchiveContext(context, archive_name, record_index) ||
        context.original_size != expected_size) {
        return false;
    }

    return ReadMilesTrcArchiveStreamBytes(
        context, pair.active.data(), expected_size);
}

bool copy_record_range(RuntimeDefinitionRecord& destination, std::size_t dst_offset,
    const RuntimeDefinitionRecord& source, std::size_t src_offset, std::size_t size) {
    if (!has_record_range(source, src_offset, size) ||
        !ensure_record_range(destination, dst_offset, size)) {
        return false;
    }
    std::memcpy(destination.bytes.data() + dst_offset, source.bytes.data() + src_offset,
        size);
    return true;
}

bool copy_record_c_string(RuntimeDefinitionRecord& destination, std::size_t dst_offset,
    const RuntimeDefinitionRecord& source, std::size_t src_offset, std::size_t max_size) {
    if (!has_record_range(source, src_offset, max_size) ||
        !ensure_record_range(destination, dst_offset, max_size)) {
        return false;
    }

    for (std::size_t index = 0; index < max_size; ++index) {
        const u8 value = source.bytes[src_offset + index];
        destination.bytes[dst_offset + index] = value;
        if (value == 0) {
            return true;
        }
    }
    return true;
}

u32 read_record_u32(const RuntimeDefinitionRecord& record, std::size_t offset) {
    if (!has_record_range(record, offset, sizeof(u32))) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, record.bytes.data() + offset, sizeof(value));
    return value;
}

void write_record_u32(RuntimeDefinitionRecord& record, std::size_t offset, u32 value) {
    if (ensure_record_range(record, offset, sizeof(value))) {
        std::memcpy(record.bytes.data() + offset, &value, sizeof(value));
    }
}

void write_buffer_u32(std::vector<u8>& buffer, std::size_t offset, u32 value) {
    if (offset <= buffer.size() && sizeof(value) <= buffer.size() - offset) {
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
    }
}

bool assign_zeroed_runtime_record(RuntimeDefinitionRecord& record, std::size_t size) {
    try {
        record.bytes.assign(size, 0);
    }
    catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool clear_session_runtime_pair_snapshot(SessionRuntimeBufferPair& pair) {
    const u32 byte_count = session_runtime_pair_byte_count(pair);
    try {
        pair.snapshot.assign(byte_count, 0);
    }
    catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool copy_record_to_pair_snapshot(SessionRuntimeBufferPair& pair, u32 index,
    const RuntimeDefinitionRecord& record) {
    if (index >= pair.count || record.bytes.size() < pair.record_size) {
        return false;
    }
    const std::size_t offset = static_cast<std::size_t>(index) * pair.record_size;
    if (offset > pair.snapshot.size() ||
        pair.record_size > pair.snapshot.size() - offset) {
        return false;
    }
    std::memcpy(pair.snapshot.data() + offset, record.bytes.data(), pair.record_size);
    return true;
}

RuntimeDefinitionRecord& ensure_record_index(
    std::vector<RuntimeDefinitionRecord>& records, std::size_t index) {
    if (records.size() <= index) {
        records.resize(index + 1);
    }
    return records[index];
}

bool record_first_dword_nonzero(const RuntimeDefinitionRecord& record) {
    return read_record_u32(record, 0) != 0;
}

std::size_t production_variant_byte_offset(u32 order_id) {
    return static_cast<std::size_t>(order_id) * sizeof(u32);
}

constexpr std::array<std::array<std::size_t, 3>, 25> kUnitRuntimeFieldCopies{{
    {{0x4c, 0x154, 4}}, {{0x50, 0x160, 4}}, {{0x54, 0x158, 4}},
    {{0x58, 0x15c, 4}}, {{0x5c, 0x18c, 4}}, {{0x60, 0x190, 4}},
    {{0x64, 0x194, 4}}, {{0x80, 0x168, 4}}, {{0x84, 0x16c, 4}},
    {{0x88, 0x170, 4}}, {{0x8c, 0x174, 4}}, {{0x90, 0x178, 4}},
    {{0x94, 0x1a8, 4}}, {{0x98, 0x1ac, 4}}, {{0x9c, 0x1b8, 1}},
    {{0x9d, 0x1b9, 1}}, {{0x9e, 0x1ba, 1}}, {{0x9f, 0x1bb, 1}},
    {{0xa0, 0x1bc, 4}}, {{0xa4, 0x1e8, 4}}, {{0xa8, 0x320, 1}},
    {{0xa9, 0x321, 1}}, {{0xaa, 0x322, 1}}, {{0xab, 0x323, 1}},
    {{0xac, 0x324, 1}},
}};

constexpr std::array<std::array<std::size_t, 3>, 6> kUnitRuntimeTailCopies{{
    {{0xad, 0x325, 1}}, {{0xae, 0x326, 2}}, {{0xb0, 0x328, 2}},
    {{0xb2, 0x32a, 2}}, {{0xb4, 0x32c, 2}}, {{0xb6, 0x32e, 2}},
}};

std::size_t avatar_record_offset(u32 player_index, u32 avatar_slot) {
    return static_cast<std::size_t>(player_index) * kGameSessionAvatarPlayerBytes +
        static_cast<std::size_t>(avatar_slot) * kGameSessionAvatarRecordBytes;
}

bool avatar_record_bounds(u32 player_index, u32 avatar_slot) {
    return player_index < kGameSessionAvatarPlayerCount &&
        avatar_slot < kGameSessionAvatarSlotCount;
}

u32 read_avatar_u32(const GameSessionAvatarRuntime& state, std::size_t offset) {
    if (offset + sizeof(u32) > state.bytes.size()) {
        return 0;
    }
    return static_cast<u32>(state.bytes[offset]) |
        (static_cast<u32>(state.bytes[offset + 1]) << 8) |
        (static_cast<u32>(state.bytes[offset + 2]) << 16) |
        (static_cast<u32>(state.bytes[offset + 3]) << 24);
}

void write_avatar_u32(GameSessionAvatarRuntime& state, std::size_t offset, u32 value) {
    if (offset + sizeof(u32) > state.bytes.size()) {
        return;
    }
    state.bytes[offset] = static_cast<u8>(value & 0xff);
    state.bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
    state.bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
    state.bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

std::string avatar_record_name(const GameSessionAvatarRuntime& state, std::size_t offset) {
    const std::size_t end = std::min<std::size_t>(
        offset + kGameSessionAvatarInvalidMarkerOffset, state.bytes.size());
    std::size_t length = 0;
    while (offset + length < end && state.bytes[offset + length] != 0) {
        ++length;
    }
    return std::string(reinterpret_cast<const char*>(state.bytes.data() + offset), length);
}

u32 avatar_record_id(const GameSessionAvatarRuntime& state, u32 player_index,
        u32 avatar_slot) {
    if (!avatar_record_bounds(player_index, avatar_slot)) {
        return kInvalidGameSessionUnitType;
    }
    return read_avatar_u32(state,
        avatar_record_offset(player_index, avatar_slot) +
            kGameSessionAvatarInvalidMarkerOffset);
}

u32 avatar_record_level(const GameSessionAvatarRuntime& state, u32 player_index,
        u32 avatar_slot) {
    if (!avatar_record_bounds(player_index, avatar_slot)) {
        return 0;
    }
    return read_avatar_u32(state,
        avatar_record_offset(player_index, avatar_slot) + kGameSessionAvatarLevelOffset);
}

const GameSessionAvatarProductionDefinition* avatar_definition(
        const GameSessionAvatarRuntime& state, u32 player_index, u32 avatar_slot,
        const std::vector<GameSessionAvatarProductionDefinition>& definitions) {
    const u32 avatar_id = avatar_record_id(state, player_index, avatar_slot);
    if (avatar_id >= definitions.size()) {
        return nullptr;
    }
    return &definitions[avatar_id];
}

bool uses_tripled_avatar_build_ticks(u32 avatar_id) {
    return avatar_id == 0x23 || avatar_id == 0x2b ||
        avatar_id == 0x2d || avatar_id == 0x26;
}

}

void ResetOwnerSessionCounterTables(OwnerSessionCounterTables& state) {
    for (auto& table : state.tables) {
        table.fill(0);
    }
}

u32 GetOwnerSessionCounterTotal(const OwnerSessionCounterTables& state, u32 owner) {
    if (owner >= kGameSessionOwnerCount) {
        return 0;
    }
    return state.tables[0][owner] + state.tables[1][owner] + state.tables[2][owner] +
        state.tables[5][owner] + state.tables[6][owner];
}

void SetOwnerSessionCounterTable2Only(OwnerSessionCounterTables& state, u32 owner, u32 value) {
    if (owner >= kGameSessionOwnerCount) {
        return;
    }
    state.tables[0][owner] = 0;
    state.tables[1][owner] = 0;
    state.tables[2][owner] = value;
    state.tables[5][owner] = 0;
    state.tables[6][owner] = 0;
}

void SnapshotPostInitTransitionRuntimeSnapshot(PostInitTransitionSnapshot& state) {
    state.saved = state.active;
}

void RestorePostInitTransitionRuntimeSnapshot(PostInitTransitionSnapshot& state) {
    state.active = state.saved;
}

void ResetGameSessionAvatarRuntime(GameSessionAvatarRuntime& state) {
    state.bytes.fill(0);
    for (u32 player = 0; player < kGameSessionAvatarPlayerCount; ++player) {
        for (u32 slot = 0; slot < kGameSessionAvatarSlotCount; ++slot) {
            write_avatar_u32(state,
                avatar_record_offset(player, slot) + kGameSessionAvatarInvalidMarkerOffset,
                kInvalidGameSessionUnitType);
        }
    }
}

bool ReadGameSessionAvatarRecord(const GameSessionAvatarRuntime& state,
        u32 player_index, u32 avatar_slot, GameSessionAvatarRecord& record) {
    record = GameSessionAvatarRecord{};
    if (!avatar_record_bounds(player_index, avatar_slot)) {
        return false;
    }

    const std::size_t offset = avatar_record_offset(player_index, avatar_slot);
    record.name = avatar_record_name(state, offset);
    record.unit_type = read_avatar_u32(
        state, offset + kGameSessionAvatarInvalidMarkerOffset);
    record.max_health = read_avatar_u32(
        state, offset + kGameSessionAvatarMaxHealthOffset);
    record.max_secondary_value = read_avatar_u32(
        state, offset + kGameSessionAvatarMaxSecondaryOffset);
    record.runtime_stat_1c = read_avatar_u32(
        state, offset + kGameSessionAvatarStat1cOffset);
    record.runtime_stat_20 = read_avatar_u32(
        state, offset + kGameSessionAvatarStat20Offset);
    record.level = read_avatar_u32(
        state, offset + kGameSessionAvatarLevelOffset);
    record.progress = read_avatar_u32(
        state, offset + kGameSessionAvatarProgressOffset);
    record.primary_equipment = read_avatar_u32(
        state, offset + kGameSessionAvatarPrimaryEquipmentOffset);
    record.secondary_equipment = read_avatar_u32(
        state, offset + kGameSessionAvatarSecondaryEquipmentOffset);
    for (u32 index = 0; index < record.pickup_effects.size(); ++index) {
        record.pickup_effects[index] = read_avatar_u32(state,
            offset + kGameSessionAvatarPickupEffectOffset + index * sizeof(u32));
    }
    return record.unit_type != kInvalidGameSessionUnitType;
}

bool LoadGameSessionAvatarRuntimeRecord(GameSessionAvatarRuntime& state,
        const char* archive_name, u32 record_index) {
    ResetGameSessionAvatarRuntime(state);
    if (archive_name == nullptr ||
        QueryTrcRecordOriginalSize(archive_name, record_index) >
            kGameSessionAvatarRuntimeBytes) {
        return false;
    }
    std::size_t bytes_read = 0;
    return read_trc_record_bytes(archive_name, record_index, state.bytes.data(),
        state.bytes.size(), &bytes_read);
}

bool AppendGameSessionAvatarRuntimeRecord(const GameSessionAvatarRuntime& state,
        const char* archive_name, const char* record_name) {
    if (archive_name == nullptr || record_name == nullptr) {
        return false;
    }
    return HandleTrcMemoryRecordAppend(archive_name, record_name, state.bytes.data(),
        state.bytes.size(), kGameSessionAvatarArchiveDirectoryGrowth,
        kGameSessionAvatarArchiveStorageMethod);
}

std::string BuildGameSessionAvatarDisplayName(const GameSessionAvatarRuntime& state,
        u32 player_index, u32 avatar_slot, const char* format) {
    if (!avatar_record_bounds(player_index, avatar_slot)) {
        return {};
    }
    const std::size_t offset = avatar_record_offset(player_index, avatar_slot);
    const std::string name = avatar_record_name(state, offset);
    const u32 level = avatar_record_level(state, player_index, avatar_slot) + 1;
    char buffer[256]{};
    const char* fmt = format != nullptr ? format :
        startup_platform_row(kStartupAvatarDisplayNameFormatRow, "%s Lv.%d");
    std::snprintf(buffer, sizeof(buffer), fmt, name.c_str(), level);
    return buffer;
}

u32 CalculateGameSessionAvatarResourceCost(const GameSessionAvatarRuntime& state,
        u32 player_index, u32 avatar_slot,
        const std::vector<GameSessionAvatarProductionDefinition>& definitions) {
    const GameSessionAvatarProductionDefinition* definition =
        avatar_definition(state, player_index, avatar_slot, definitions);
    if (definition == nullptr) {
        return 0;
    }
    return definition->resource_cost +
        avatar_record_level(state, player_index, avatar_slot) * 10;
}

u32 CalculateGameSessionAvatarBuildTicks(const GameSessionAvatarRuntime& state,
        u32 player_index, u32 avatar_slot,
        const std::vector<GameSessionAvatarProductionDefinition>& definitions) {
    const GameSessionAvatarProductionDefinition* definition =
        avatar_definition(state, player_index, avatar_slot, definitions);
    if (definition == nullptr) {
        return 0;
    }
    const u32 avatar_id = avatar_record_id(state, player_index, avatar_slot);
    return CalculateGameSessionAvatarBuildTicks(avatar_id,
        avatar_record_level(state, player_index, avatar_slot),
        definition->build_ticks);
}

u32 CalculateGameSessionAvatarBuildTicks(
        u32 avatar_id, u32 avatar_level, u32 base_ticks) {
    const u32 adjusted_base_ticks = uses_tripled_avatar_build_ticks(avatar_id)
        ? base_ticks * 3
        : base_ticks;
    return adjusted_base_ticks + (avatar_level * base_ticks) / 10;
}

u32 GetGameSessionAvatarSupportCost(const GameSessionAvatarRuntime& state,
        u32 player_index, u32 avatar_slot,
        const std::vector<GameSessionAvatarProductionDefinition>& definitions) {
    const GameSessionAvatarProductionDefinition* definition =
        avatar_definition(state, player_index, avatar_slot, definitions);
    return definition != nullptr ? definition->support_cost : 0;
}

bool IsGameSessionAvatarProductionAvailable() {
    return true;
}

void ApplyPostInitUnitRequirementToggle(GameSessionUnitReferenceTables& state) {
    for (u32 index = 0; index < kPostInitUnitRequirementCount; ++index) {
        const u32 unit_type = state.post_init_unit_types[index];
        const u32 required_type = state.post_init_required_types[index];
        if (unit_type >= state.definitions.size()) {
            continue;
        }

        UnitTypeSessionDefinition& definition = state.definitions[unit_type];
        if (state.post_init_transition_pending) {
            add_reference(definition, required_type);
        } else {
            remove_reference(definition, required_type);
        }
    }
}

void RebuildUnitTypeReverseReferenceTables(GameSessionUnitReferenceTables& state) {
    state.primary_or_alternate_reverse.fill(kInvalidGameSessionUnitType);
    state.completion_reverse.fill(kInvalidGameSessionUnitType);
    state.small_reverse.fill(kInvalidGameSessionUnitType);

    for (u32 unit_type = 0; unit_type < state.definitions.size(); ++unit_type) {
        const UnitTypeSessionDefinition& definition = state.definitions[unit_type];
        if (!definition.present) {
            continue;
        }

        fill_reverse_from_u32_list(state.primary_or_alternate_reverse,
            definition.primary_references, definition.primary_reference_count, unit_type);
        fill_reverse_from_u32_list(state.primary_or_alternate_reverse,
            definition.alternate_references, definition.alternate_reference_count, unit_type);
        fill_completion_reverse_from_u32_list(state.completion_reverse,
            definition.completion_references, definition.completion_reference_count,
            unit_type);

        const u32 small_count = std::min<u32>(definition.small_reference_count,
            kGameSessionSmallReferenceCapacity);
        for (u32 index = 0; index < small_count; ++index) {
            state.small_reverse[definition.small_references[index]] = unit_type;
        }
    }
}

bool CopyFixed44SessionRuntimeRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    return copy_record_range(destination, 0, source, 0, 0x44);
}

bool ImportFixed44SessionRuntimeRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    return CopyFixed44SessionRuntimeRecord(destination, source);
}

bool ExportUnitSessionRuntimeDefinitionRecord(u32 unit_type,
    RuntimeDefinitionRecord& destination, const RuntimeDefinitionRecord& source) {
    if (unit_type >= kGameSessionUnitTypeCount ||
        !assign_zeroed_runtime_record(destination, 0x0d0)) {
        return false;
    }

    for (u32 owner = 0; owner < kGameSessionOwnerCount; ++owner) {
        destination.bytes[4 + owner] = 1;
    }

    bool ok = copy_record_c_string(destination, 0x0c, source, 0x10c, 0x40);
    for (const auto& copy : kUnitRuntimeFieldCopies) {
        ok = copy_record_range(destination, copy[0], source, copy[1], copy[2]) && ok;
    }
    for (const auto& copy : kUnitRuntimeTailCopies) {
        ok = copy_record_range(destination, copy[0], source, copy[1], copy[2]) && ok;
    }
    return ok;
}

bool ImportUnitSessionRuntimeDefinitionRecord(SessionRuntimeImportState& state,
    u32 unit_type, RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    if (unit_type >= kGameSessionUnitTypeCount || !has_record_range(source, 4, 8)) {
        return false;
    }

    for (u32 owner = 0; owner < kGameSessionOwnerCount; ++owner) {
        state.owner_unit_availability[owner][unit_type] = source.bytes[4 + owner];
    }

    bool ok = copy_record_c_string(destination, 0x10c, source, 0x0c, 0x40);
    for (const auto& copy : kUnitRuntimeFieldCopies) {
        ok = copy_record_range(destination, copy[1], source, copy[0], copy[2]) && ok;
    }
    for (const auto& copy : kUnitRuntimeTailCopies) {
        ok = copy_record_range(destination, copy[1], source, copy[0], copy[2]) && ok;
    }
    return ok;
}

bool ExportProductionOrderSessionRuntimeRecord(const SessionRuntimeExportState& state,
    u32 order_id, RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    const std::size_t variant_offset = production_variant_byte_offset(order_id);
    if (variant_offset >= kGameSessionProductionVariantStride ||
        !assign_zeroed_runtime_record(destination, 0x470)) {
        return false;
    }

    bool ok = copy_record_range(destination, 4, source, 0, 0x44c);
    for (u32 owner = 0; owner < kGameSessionOwnerCount; ++owner) {
        write_record_u32(destination, 0x450 + owner * sizeof(u32),
            static_cast<u32>(state.owner_order_variants[owner][variant_offset]) + 1);
    }
    return ok;
}

bool ImportProductionOrderSessionRuntimeRecord(SessionRuntimeImportState& state,
    u32 order_id, RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    const std::size_t variant_offset = production_variant_byte_offset(order_id);
    if (variant_offset >= kGameSessionProductionVariantStride) {
        return false;
    }
    bool ok = copy_record_range(destination, 0, source, 4, 0x44c);
    if (static_cast<i32>(read_record_u32(source, 0x214)) != -1) {
        for (u32 owner = 0; owner < kGameSessionOwnerCount; ++owner) {
            const i32 target =
                static_cast<i32>(read_record_u32(source, 0x450 + owner * sizeof(u32)));
            while (static_cast<i32>(state.owner_order_variants[owner][variant_offset]) <
                target - 1) {
                const u8 before = state.owner_order_variants[owner][variant_offset];
                if (state.apply_production_completion != nullptr) {
                    state.apply_production_completion(owner, order_id);
                }
                if (state.owner_order_variants[owner][variant_offset] == before) {
                    ++state.owner_order_variants[owner][variant_offset];
                }
            }
        }
    }
    return ok;
}

bool ExportEightDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    static constexpr std::array<std::size_t, 8> kSourceOffsets{
        0x170, 0x174, 0x178, 0x17c, 0x180, 0x184, 0x188, 0x18c,
    };
    if (!assign_zeroed_runtime_record(destination, 0x44)) {
        return false;
    }
    bool ok = true;
    for (std::size_t index = 0; index < kSourceOffsets.size(); ++index) {
        ok = copy_record_range(destination, 4 + index * sizeof(u32), source,
            kSourceOffsets[index], sizeof(u32)) && ok;
    }
    return ok;
}

bool ImportEightDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    static constexpr std::array<std::size_t, 8> kDestinationOffsets{
        0x170, 0x174, 0x178, 0x17c, 0x180, 0x184, 0x188, 0x18c,
    };
    bool ok = true;
    for (std::size_t index = 0; index < kDestinationOffsets.size(); ++index) {
        ok = copy_record_range(destination, kDestinationOffsets[index], source,
            4 + index * sizeof(u32), sizeof(u32)) && ok;
    }
    return ok;
}

bool ExportFourDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    static constexpr std::array<std::array<std::size_t, 2>, 4> kCopies{{
        {{0x04, 0x1e0}}, {{0x08, 0x1e4}}, {{0x0c, 0x15c}}, {{0x10, 0x160}},
    }};
    if (!assign_zeroed_runtime_record(destination, 0x34)) {
        return false;
    }
    bool ok = true;
    for (const auto& copy : kCopies) {
        ok = copy_record_range(destination, copy[0], source, copy[1], sizeof(u32)) && ok;
    }
    return ok;
}

bool ImportFourDwordSessionRuntimeTailRecord(RuntimeDefinitionRecord& destination,
    const RuntimeDefinitionRecord& source) {
    static constexpr std::array<std::array<std::size_t, 2>, 4> kCopies{{
        {{0x04, 0x1e0}}, {{0x08, 0x1e4}}, {{0x0c, 0x15c}}, {{0x10, 0x160}},
    }};
    bool ok = true;
    for (const auto& copy : kCopies) {
        ok = copy_record_range(destination, copy[1], source, copy[0], sizeof(u32)) && ok;
    }
    return ok;
}

bool ImportSessionRuntimeDefinitionTables(SessionRuntimeImportState& state,
    SessionRuntimeDefinitionTableSet& active,
    const SessionRuntimeDefinitionTableSet& staged) {
    bool ok = true;

    for (std::size_t index = 0; index < staged.fixed44_records.size(); ++index) {
        ok = ImportFixed44SessionRuntimeRecord(
                 ensure_record_index(active.fixed44_records, index),
                 staged.fixed44_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.unit_records.size(); ++index) {
        ok = ImportUnitSessionRuntimeDefinitionRecord(state, static_cast<u32>(index),
                 ensure_record_index(active.unit_records, index),
                 staged.unit_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.production_order_records.size(); ++index) {
        ok = ImportProductionOrderSessionRuntimeRecord(state, static_cast<u32>(index),
                 ensure_record_index(active.production_order_records, index),
                 staged.production_order_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.tail8_records.size(); ++index) {
        ok = ImportEightDwordSessionRuntimeTailRecord(
                 ensure_record_index(active.tail8_records, index),
                 staged.tail8_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.tail4_records.size(); ++index) {
        ok = ImportFourDwordSessionRuntimeTailRecord(
                 ensure_record_index(active.tail4_records, index),
                 staged.tail4_records[index]) &&
            ok;
    }

    return ok;
}

bool ImportNonEmptySessionRuntimeDefinitionTables(SessionRuntimeImportState& state,
    SessionRuntimeDefinitionTableSet& active,
    const SessionRuntimeDefinitionTableSet& staged) {
    bool ok = true;

    for (std::size_t index = 0; index < staged.fixed44_records.size(); ++index) {
        ok = ImportFixed44SessionRuntimeRecord(
                 ensure_record_index(active.fixed44_records, index),
                 staged.fixed44_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.unit_records.size(); ++index) {
        if (!record_first_dword_nonzero(staged.unit_records[index])) {
            continue;
        }
        ok = ImportUnitSessionRuntimeDefinitionRecord(state, static_cast<u32>(index),
                 ensure_record_index(active.unit_records, index),
                 staged.unit_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.production_order_records.size(); ++index) {
        if (!record_first_dword_nonzero(staged.production_order_records[index])) {
            continue;
        }
        ok = ImportProductionOrderSessionRuntimeRecord(state, static_cast<u32>(index),
                 ensure_record_index(active.production_order_records, index),
                 staged.production_order_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.tail8_records.size(); ++index) {
        if (!record_first_dword_nonzero(staged.tail8_records[index])) {
            continue;
        }
        ok = ImportEightDwordSessionRuntimeTailRecord(
                 ensure_record_index(active.tail8_records, index),
                 staged.tail8_records[index]) &&
            ok;
    }
    for (std::size_t index = 0; index < staged.tail4_records.size(); ++index) {
        if (!record_first_dword_nonzero(staged.tail4_records[index])) {
            continue;
        }
        ok = ImportFourDwordSessionRuntimeTailRecord(
                 ensure_record_index(active.tail4_records, index),
                 staged.tail4_records[index]) &&
            ok;
    }

    return ok;
}

void RebuildSessionPlayerSlotMasksFromFixed44Records(PlayerSlotRuntimeState& slots,
    const std::vector<RuntimeDefinitionRecord>& fixed44_records) {
    slots.global_active_slot_mask = 0;

    const std::size_t group_count =
        std::min<std::size_t>(fixed44_records.size(), kPostInitUnitRequirementCount);
    for (std::size_t group = 0; group < group_count; ++group) {
        const RuntimeDefinitionRecord& record = fixed44_records[group];
        const u32 owner_mask = read_record_u32(record, 0x20);
        const bool use_shared_relation_mask = read_record_u32(record, 0x24) != 0;
        const bool mark_active = read_record_u32(record, 0x2c) != 0;
        const bool use_shared_visibility_mask = read_record_u32(record, 0x30) != 0;

        for (u32 slot = 0; slot < kGameSessionOwnerCount; ++slot) {
            const u32 bit = 1u << slot;
            if ((owner_mask & bit) == 0) {
                continue;
            }

            slots.owner_relation_masks[slot] =
                use_shared_relation_mask ? owner_mask : bit;
            slots.owner_visibility_masks[slot] =
                use_shared_visibility_mask ? owner_mask : bit;
            if (mark_active) {
                slots.global_active_slot_mask |= bit;
            }
        }
    }
}

void InitializeSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers) {
    static constexpr std::array<u32, kSessionRuntimeBufferPairCount> kRecordSizes{
        0x44, 0x0d0, 0x470, 0x44, 0x34,
    };
    for (std::size_t index = 0; index < buffers.categories.size(); ++index) {
        buffers.categories[index].record_size = kRecordSizes[index];
    }
}

bool ResizeSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers,
    u32 fixed44_count, u32 unit_count, u32 production_order_count,
    u32 tail44_count, u32 tail34_count,
    SessionRuntimeBufferInitializeCallback initialize_records) {
    InitializeSessionRuntimeBufferPairs(buffers);
    const std::array<u32, kSessionRuntimeBufferPairCount> counts{
        fixed44_count, unit_count, production_order_count, tail44_count, tail34_count,
    };

    try {
        for (std::size_t index = 0; index < buffers.categories.size(); ++index) {
            SessionRuntimeBufferPair& pair = buffers.categories[index];
            if (pair.count == counts[index]) {
                continue;
            }
            pair.count = counts[index];
            const std::size_t byte_count =
                static_cast<std::size_t>(pair.count) * pair.record_size;
            pair.active.assign(byte_count, 0);
            pair.snapshot.assign(byte_count, 0);
        }
    }
    catch (const std::bad_alloc&) {
        return false;
    }

    if (initialize_records != nullptr) {
        initialize_records(buffers);
    }
    SnapshotSessionRuntimeBufferPairs(buffers);
    return true;
}

bool BuildDefaultSessionRuntimeStagingBuffers(SessionRuntimeBufferPairs& buffers,
    const SessionRuntimeDefinitionTableSet& live,
    const SessionRuntimeExportState& state) {
    InitializeSessionRuntimeBufferPairs(buffers);

    for (SessionRuntimeBufferPair& pair : buffers.categories) {
        if (!clear_session_runtime_pair_snapshot(pair)) {
            return false;
        }
    }

    SessionRuntimeBufferPair& fixed44 = buffers.categories[0];
    if (fixed44.count != 0) {
        write_buffer_u32(fixed44.snapshot, 0x20, 0xff);
    }
    for (u32 index = 0; index < fixed44.count; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index) * fixed44.record_size;
        write_buffer_u32(fixed44.snapshot, offset + 0x24, 1);
        write_buffer_u32(fixed44.snapshot, offset + 0x28, 1);
        write_buffer_u32(fixed44.snapshot, offset + 0x2c, 1);
        write_buffer_u32(fixed44.snapshot, offset + 0x30, 1);
    }

    SessionRuntimeBufferPair& units = buffers.categories[1];
    for (u32 index = 0; index < units.count; ++index) {
        if (index >= live.unit_records.size()) {
            return false;
        }
        RuntimeDefinitionRecord compact;
        if (!ExportUnitSessionRuntimeDefinitionRecord(
                index, compact, live.unit_records[index]) ||
            !copy_record_to_pair_snapshot(units, index, compact)) {
            return false;
        }
    }

    SessionRuntimeBufferPair& production_orders = buffers.categories[2];
    for (u32 index = 0; index < production_orders.count; ++index) {
        if (index >= live.production_order_records.size()) {
            return false;
        }
        RuntimeDefinitionRecord compact;
        if (!ExportProductionOrderSessionRuntimeRecord(
                state, index, compact, live.production_order_records[index]) ||
            !copy_record_to_pair_snapshot(production_orders, index, compact)) {
            return false;
        }
    }

    SessionRuntimeBufferPair& tail8 = buffers.categories[3];
    for (u32 index = 0; index < tail8.count; ++index) {
        if (index >= live.tail8_records.size()) {
            return false;
        }
        RuntimeDefinitionRecord compact;
        if (!ExportEightDwordSessionRuntimeTailRecord(
                compact, live.tail8_records[index]) ||
            !copy_record_to_pair_snapshot(tail8, index, compact)) {
            return false;
        }
    }

    SessionRuntimeBufferPair& tail4 = buffers.categories[4];
    for (u32 index = 0; index < tail4.count; ++index) {
        if (index >= live.tail4_records.size()) {
            return false;
        }
        RuntimeDefinitionRecord compact;
        if (!ExportFourDwordSessionRuntimeTailRecord(
                compact, live.tail4_records[index]) ||
            !copy_record_to_pair_snapshot(tail4, index, compact)) {
            return false;
        }
    }

    return true;
}

void ReleaseSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers) {
    for (SessionRuntimeBufferPair& pair : buffers.categories) {
        pair.count = 0;
        pair.active.clear();
        pair.snapshot.clear();
        pair.active.shrink_to_fit();
        pair.snapshot.shrink_to_fit();
    }
}

void SnapshotSessionRuntimeBufferPairs(SessionRuntimeBufferPairs& buffers) {
    for (SessionRuntimeBufferPair& pair : buffers.categories) {
        pair.active = pair.snapshot;
    }
}

bool AppendForcesSessionRuntimeRecord(const char* archive_name,
    const std::vector<u8>& fixed44_records) {
    if (fixed44_records.size() < kSessionRuntimeForcesFixedRecordBytes) {
        return false;
    }
    return HandleTrcMemoryRecordAppend(archive_name, "FORCES", fixed44_records.data(),
        kSessionRuntimeForcesFixedRecordBytes, kSessionRuntimeForcesDirectoryGrowth,
        kSessionRuntimeForcesStorageMethod);
}

bool AppendForcesSessionRuntimeRecord(const char* archive_name,
    const SessionRuntimeBufferPairs& buffers) {
    const SessionRuntimeBufferPair& forces =
        buffers.categories[kSessionRuntimeForcesRecordIndex];
    return AppendForcesSessionRuntimeRecord(archive_name, forces.active);
}

bool AppendForcesSessionRuntimeWriteRecord(std::vector<TrcWriteRecord>& records,
    const SessionRuntimeBufferPairs& buffers) {
    const SessionRuntimeBufferPair& forces =
        buffers.categories[kSessionRuntimeForcesRecordIndex];
    if (forces.active.size() < kSessionRuntimeForcesFixedRecordBytes) {
        return false;
    }

    TrcWriteRecord record{};
    record.name = "FORCES";
    record.method = kSessionRuntimeForcesStorageMethod;
    record.payload.assign(forces.active.begin(),
        forces.active.begin() + kSessionRuntimeForcesFixedRecordBytes);
    records.push_back(record);
    return true;
}

bool LoadForcesSessionRuntimeRecord(const char* archive_name,
    SessionRuntimeBufferPairs& buffers, std::vector<u8>& fixed44_records) {
    InitializeSessionRuntimeBufferPairs(buffers);
    const SessionRuntimeBufferPair& forces =
        buffers.categories[kSessionRuntimeForcesRecordIndex];
    const u32 expected_size = forces.count * forces.record_size;

    MilesComputedStreamContext context;
    InitializeMilesTrcArchiveStreamContext(context);
    const bool record_loaded = LoadMilesTrcRecordIntoArchiveContext(
        context, archive_name, kSessionRuntimeForcesArchiveRecordIndex);
    if (!record_loaded || context.original_size != expected_size) {
        const bool restored =
            copy_live_fixed44_records_from_staged_buffer(fixed44_records, forces);
        ReleaseMilesTrcArchiveStreamContext(context);
        return restored;
    }

    fixed44_records.resize(expected_size);
    const bool read_ok = ReadMilesTrcArchiveStreamBytes(
        context, fixed44_records.data(), expected_size);
    ReleaseMilesTrcArchiveStreamContext(context);
    return read_ok;
}

bool AppendUserSessionRuntimeOverrideRecords(const char* archive_name,
    const SessionRuntimeBufferPairs& buffers) {
    for (std::size_t index = 0; index < kSessionRuntimeUserRecordCount; ++index) {
        const SessionRuntimeBufferPair& pair = buffers.categories[index];
        const u32 byte_count = session_runtime_pair_byte_count(pair);
        if (pair.active.size() < byte_count ||
            !HandleTrcMemoryRecordAppend(archive_name, kSessionRuntimeUserRecordNames[index],
                pair.active.data(), byte_count, kSessionRuntimeForcesDirectoryGrowth,
                kSessionRuntimeForcesStorageMethod)) {
            return false;
        }
    }
    return true;
}

bool AppendUserSessionRuntimeOverrideWriteRecords(
    std::vector<TrcWriteRecord>& records, const SessionRuntimeBufferPairs& buffers) {
    for (std::size_t index = 0; index < kSessionRuntimeUserRecordCount; ++index) {
        const SessionRuntimeBufferPair& pair = buffers.categories[index];
        const u32 byte_count = session_runtime_pair_byte_count(pair);
        if (pair.active.size() < byte_count) {
            return false;
        }

        TrcWriteRecord record{};
        record.name = kSessionRuntimeUserRecordNames[index];
        record.method = kSessionRuntimeForcesStorageMethod;
        record.payload.assign(pair.active.begin(), pair.active.begin() + byte_count);
        records.push_back(record);
    }
    return true;
}

SessionRuntimeOverrideLoadStatus LoadUserForceSessionRuntimeOverrideRecord(
    const char* archive_name, SessionRuntimeBufferPairs& buffers) {
    InitializeSessionRuntimeBufferPairs(buffers);
    MilesComputedStreamContext context;
    InitializeMilesTrcArchiveStreamContext(context);

    SessionRuntimeBufferPair& force_pair = buffers.categories[0];
    const u32 expected_size = session_runtime_pair_byte_count(force_pair);
    const bool loaded = LoadMilesTrcRecordIntoArchiveContext(
        context, archive_name, kSessionRuntimeUserArchiveRecordIndices[0]);
    if (!loaded || context.original_size != expected_size) {
        SnapshotSessionRuntimeBufferPairs(buffers);
        ReleaseMilesTrcArchiveStreamContext(context);
        return SessionRuntimeOverrideLoadStatus::RestoredDefaults;
    }

    const bool read_ok = force_pair.active.size() >= expected_size &&
        ReadMilesTrcArchiveStreamBytes(context, force_pair.active.data(), expected_size);
    ReleaseMilesTrcArchiveStreamContext(context);
    return read_ok ? SessionRuntimeOverrideLoadStatus::Loaded
                   : SessionRuntimeOverrideLoadStatus::Failed;
}

bool LoadUserSessionRuntimeOverrideRecords(const char* archive_name,
    SessionRuntimeBufferPairs& buffers) {
    InitializeSessionRuntimeBufferPairs(buffers);
    const SessionRuntimeOverrideLoadStatus force_status =
        LoadUserForceSessionRuntimeOverrideRecord(archive_name, buffers);
    if (force_status == SessionRuntimeOverrideLoadStatus::Failed) {
        return false;
    }
    if (force_status == SessionRuntimeOverrideLoadStatus::RestoredDefaults) {
        return true;
    }

    MilesComputedStreamContext context;
    InitializeMilesTrcArchiveStreamContext(context);
    for (std::size_t index = 1; index < kSessionRuntimeUserRecordCount; ++index) {
        SessionRuntimeBufferPair& pair = buffers.categories[index];
        const bool read_ok = load_session_runtime_active_record(context, archive_name,
            kSessionRuntimeUserArchiveRecordIndices[index], pair);
        ReleaseMilesTrcArchiveStreamContext(context);
        if (!read_ok) {
            return false;
        }
    }
    return true;
}

}
