#pragma once

#include "ranker_unit_commands.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

struct MapEffectContext;

constexpr u32 kUnitEquipmentSlotCount = 6;
constexpr u32 kUnitEquipmentGenericSlotBase = 0;
constexpr u32 kUnitEquipmentPrimarySlot = 4;
constexpr u32 kUnitEquipmentSecondarySlot = 5;
constexpr u32 kUnitEquipmentOriginalPrimarySlotCode = 1;
constexpr u32 kUnitEquipmentOriginalSecondarySlotCode = 2;
constexpr u32 kUnitEquipmentOriginalGenericSlotBaseCode = 3;
constexpr u32 kUnitEquipmentPickupEnabledFlag = 0x2;
constexpr u32 kInvalidUnitEquipmentId = 0;
constexpr u32 kInvalidUnitEquipmentType = 0xffffffffu;
constexpr u32 kUnitEquipmentTypeFilterMaxCount = 0x80;
constexpr u32 kUnitEquipmentGenericModifierCount = 7;
constexpr u32 kUnitEquipmentGenericModifierActionRecovery = 0;
constexpr u32 kUnitEquipmentGenericModifierActionRange = 1;
constexpr u32 kUnitEquipmentGenericModifierMovementFrameDelta = 2;
constexpr u32 kUnitEquipmentGenericModifierInteractionRange = 3;
constexpr u32 kUnitEquipmentGenericModifierReserved240 = 4;
constexpr u32 kUnitEquipmentGenericModifierCommandGate = 5;
constexpr u32 kUnitEquipmentGenericModifierCommandFlag = 6;

enum class UnitEquipmentCategory : u32 {
    Generic = 0,
    Primary = 1,
    Secondary = 2,
    Amount = 3,
};

struct UnitEquipmentEffectDefinition {
    u32 id = kInvalidUnitEquipmentId;
    std::string display_name;
    std::string detail_text;
    u32 type_filter_mode = 0;
    std::vector<u32> type_filter_type_ids;
    u32 pickup_filter_mode = 0;
    UnitEquipmentCategory category = UnitEquipmentCategory::Generic;
    u32 icon_frame_index = 0;
    u32 icon_marker_code = 0;
    u32 mode = 0;
    u32 ambient_flags = 0;
    u32 ambient_spawn_rate = 0;
    u32 map_effect_frame_period = 0;
    u32 completion_terrain_effect_period = 0;
    u32 tooltip_primary_cost = 0;
    u32 tooltip_secondary_cost = 0;
    u32 attachment_definition_id = 0;
    u32 replacement_type_id = kInvalidUnitEquipmentType;
    i32 max_health_delta = 0;
    i32 health_delta = 0;
    i32 max_secondary_delta = 0;
    i32 secondary_delta = 0;
    i32 runtime_stat_1c_delta = 0;
    i32 runtime_stat_20_delta = 0;
    i32 runtime_stat_28_delta = 0;
    i32 experience_delta = 0;
    i32 level_delta = 0;
    i32 owner_resource_delta = 0;
    i32 command_value_delta = 0;
    std::array<i32, kUnitEquipmentGenericModifierCount> generic_modifiers{};
    i32 movement_frame_delta_modifier = 0;
};

struct UnitEquipmentCatalog {
    std::vector<UnitEquipmentEffectDefinition> effects;
};

struct UnitEquipmentPublishState {
    std::array<std::array<u32, kUnitEquipmentSlotCount>, 16> owner_slots{};
    std::array<std::array<u32, kUnitEquipmentSlotCount>, 4> local_group_slots{};
    u32 publish_count = 0;
    u32 last_group = 0;
    u32 last_owner = 0;
    bool dirty = false;
};

using UnitEquipmentCommandPublishCallback =
    bool (*)(const UnitMovementUnit& unit, u32 original_slot_code, u32 equipment_id);

const UnitEquipmentEffectDefinition* FindUnitEquipmentEffect(
    const UnitEquipmentCatalog& catalog, u32 id);
bool UnitEquipmentEffectAllowsUnitType(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect);
bool CheckUnitEquipmentPickupEligible(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect);
bool IsUnitEquipmentEffectActiveForUnit(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect);
bool AddUnitEquipmentGenericSlot(UnitMovementUnit& unit, u32 effect_id,
    u32* original_slot_code = nullptr);
bool AssignUnitEquipmentGenericSlot(UnitMovementUnit& unit, u32 effect_id,
    u32* original_slot_code = nullptr);
u32 HandleUnitEquipmentPickupApply(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 amount,
    const UnitEquipmentCatalog& catalog, u32* original_slot_code = nullptr);
bool TryApplyUnitEquipmentEffectToUnit(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 amount,
    const UnitEquipmentCatalog& catalog, u32* original_slot_code = nullptr);
bool HandleUnitEquipmentMapEffectCollect(UnitCommandContext& context,
    MapEffectContext& map_effects, UnitMovementUnit& unit, i32 x, i32 y,
    const UnitEquipmentCatalog& catalog);
bool TryCollectUnitEquipmentFromMapEffects(UnitCommandContext& context,
    MapEffectContext& map_effects, UnitMovementUnit& unit, i32 x, i32 y,
    const UnitEquipmentCatalog& catalog);
bool ClearUnitEquipmentSlot(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 original_slot_code, const UnitEquipmentCatalog& catalog);
bool TransferUnitEquipmentSlot(UnitCommandContext& context, UnitMovementUnit& source,
    UnitMovementUnit& target, u32 original_slot_code,
    const UnitEquipmentCatalog& catalog);
u32 CountUnitEquipmentEffectSlots(const UnitMovementUnit& unit, u32 effect_id);
i32 CalculateUnitEquipmentGenericModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog, u32 modifier_index,
    bool require_owner_below_8 = false);
i32 CalculateUnitEquipmentActionRecoveryModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentModifier22c(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentActionRangeModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentModifier230(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentMovementFrameModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentInteractionRangeModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentReserved240Modifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentModifier240(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentCommandGateModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
i32 CalculateUnitEquipmentCommandFlagModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog);
bool ApplyUnitEquipmentEffect(UnitCommandContext& context, UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect);
bool RemoveUnitEquipmentEffect(UnitCommandContext& context, UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect,
    const UnitEquipmentCatalog* catalog = nullptr);
bool ToggleUnitEquipmentSlotEffect(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 slot_index, u32 effect_id, const UnitEquipmentCatalog& catalog);
u32 ToggleUnitEquipmentOriginalSlotEffect(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 original_slot_code,
    const UnitEquipmentCatalog& catalog);
void PublishUnitEquipmentSlots(UnitEquipmentPublishState& state,
    const UnitMovementUnit& unit, u32 group_index);
bool PublishLocalEquipmentSlotIfAllowed(const UnitMovementUnit& unit, u32 original_slot_code,
    u32 local_owner, const UnitEquipmentCatalog& catalog,
    UnitEquipmentCommandPublishCallback publish_command = nullptr);
bool LoadUnitEquipmentCatalogFromBytes(const void* data, std::size_t size,
    UnitEquipmentCatalog& catalog, u32* version = nullptr);
bool LoadUnitEquipmentCatalogFromJw210Trc(UnitEquipmentCatalog& catalog,
    const char* archive_name = "JW2_10.TRC", u32 record_index = 2,
    u32* version = nullptr);
bool LoadUnitEquipmentCatalogFromJw210TrcRecord2(UnitEquipmentCatalog& catalog,
    u32* version = nullptr);

}
