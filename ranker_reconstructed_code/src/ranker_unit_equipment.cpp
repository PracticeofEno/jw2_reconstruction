#include "ranker_unit_equipment.h"

#include "ranker_indexed_text_table.h"
#include "ranker_map_effects.h"
#include "ranker_meat_pipeline.h"
#include "ranker_miles.h"
#include "ranker_trc.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace ranker {
namespace {

enum class PickupApplyResult {
    failed = 0,
    consume_map_effect = 1,
    keep_map_effect = 2,
};

u32 apply_signed_delta(u32 value, i32 delta, u32 minimum = 0) {
    if (delta < 0) {
        const u32 amount = static_cast<u32>(-delta);
        return value > amount + minimum ? value - amount : minimum;
    }
    return value + static_cast<u32>(delta);
}

u32 clamp_to_max(u32 value, u32 maximum) {
    return std::min(value, maximum);
}

u32 apply_signed_clamped_delta(u32 value, i32 delta, u32 maximum,
    u32 minimum = 0) {
    return clamp_to_max(apply_signed_delta(value, delta, minimum), maximum);
}

void apply_owner_delta(std::array<u32, 16>& values, u32 owner, i32 delta) {
    if (owner >= values.size()) {
        return;
    }
    values[owner] = apply_signed_delta(values[owner], delta);
}

UnitRuntimeStatBlock runtime_stats_from_unit(const UnitMovementUnit& unit) {
    UnitRuntimeStatBlock stats{};
    stats.max_health = unit.max_health;
    stats.max_secondary_value = unit.max_secondary_value;
    stats.health = unit.health;
    stats.stat_1c = unit.runtime_stat_1c;
    stats.stat_20 = unit.runtime_stat_20;
    stats.secondary_value = unit.secondary_value;
    stats.stat_28 = unit.runtime_stat_28;
    return stats;
}

void apply_unit_experience_delta(UnitCommandContext& context,
    UnitMovementUnit& unit, i32 delta) {
    unit.elite_progress_value =
        apply_signed_delta(unit.elite_progress_value, delta);
    if (delta <= 0 || context.production_state == nullptr) {
        return;
    }

    UnitRuntimeStatBlock stats = runtime_stats_from_unit(unit);
    bool rank_up = false;
    ApplyUnitVariantProgressFromStoredValue(*context.production_state, unit, stats,
        &rank_up, context.callbacks.variant_random_limit);
    // Original FUN_004027cf, called by ApplyUnitEquipmentEffect at
    // 0x004106ba, publishes action 0x2d once when stored experience crosses
    // one or more rank thresholds.  This is the same attachment used after a
    // kill-award rank-up; omitting it changes the intrusive unit-effect pool.
    if (rank_up && context.callbacks.start_ability_attachment != nullptr) {
        context.callbacks.start_ability_attachment(context, unit, &unit, 0x2du);
    }
}

void remove_unit_experience_bonus(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 amount) {
    if (amount == 0) {
        return;
    }
    if (context.production_state == nullptr) {
        unit.elite_progress_value =
            amount > unit.elite_progress_value ? 0 : unit.elite_progress_value - amount;
        return;
    }

    u32 remaining = amount;
    while (remaining != 0) {
        if (unit.elite_progress_value >= remaining) {
            unit.elite_progress_value -= remaining;
            break;
        }

        remaining -= unit.elite_progress_value;
        if (unit.production_variant == 0) {
            unit.elite_progress_value = 0;
            break;
        }

        --unit.production_variant;
        unit.elite_progress_value = CalculateOrder2bAdjustedUnitValue(
            *context.production_state, unit,
            unit.definition.variant_progress_base_cost,
            unit.definition.variant_progress_cost_per_level);
        ++unit.production_variant;

        UnitRuntimeStatBlock stats = runtime_stats_from_unit(unit);
        if (!DecreaseUnitVariantStats(
                unit, stats, context.callbacks.variant_random_limit)) {
            unit.elite_progress_value = 0;
            break;
        }
        --unit.production_variant;
        if (unit.status_timer != 0) {
            --unit.status_timer;
        }
    }
}

void apply_unit_level_delta(UnitCommandContext& context, UnitMovementUnit& unit,
    i32 delta) {
    if (delta == 0) {
        return;
    }

    UnitRuntimeStatBlock stats = runtime_stats_from_unit(unit);
    if (delta > 0) {
        // ApplyUnitEquipmentEffect (0x00410300) deliberately runs every
        // FUN_00409ac0 growth roll against the same raw +0x54 level, then adds
        // the complete level delta once after the loop.  Advancing the level
        // after each roll changes both the roll budget and the shared RNG
        // sequence for multi-level equipment effects.
        for (i32 index = 0; index < delta; ++index) {
            IncreaseUnitVariantStats(
                unit, stats, context.callbacks.variant_random_limit);
        }
        unit.production_variant += static_cast<u32>(delta);
        unit.status_timer += static_cast<u32>(delta);
        return;
    }

    for (i32 index = 0; index > delta; --index) {
        if (unit.production_variant == 0) {
            unit.status_timer = 0;
            break;
        }
        if (!DecreaseUnitVariantStats(
                unit, stats, context.callbacks.variant_random_limit)) {
            unit.status_timer = 0;
            break;
        }
        --unit.production_variant;
        if (unit.status_timer != 0) {
            --unit.status_timer;
        }
    }
}

i32 definition_delta(u32 replacement, u32 original) {
    return static_cast<i32>(replacement) - static_cast<i32>(original);
}

void apply_type_replacement_stat_delta(UnitMovementUnit& unit,
    const UnitMovementDefinition& original,
    const UnitMovementDefinition& replacement) {
    const i32 health_delta =
        definition_delta(replacement.initial_max_health, original.initial_max_health);
    unit.max_health = apply_signed_delta(unit.max_health, health_delta, 1);
    unit.health = apply_signed_delta(unit.health, health_delta, 1);

    const i32 secondary_delta = definition_delta(
        replacement.initial_max_secondary_value,
        original.initial_max_secondary_value);
    unit.max_secondary_value =
        apply_signed_delta(unit.max_secondary_value, secondary_delta);
    unit.secondary_value = apply_signed_delta(unit.secondary_value, secondary_delta);

    unit.runtime_stat_1c = apply_signed_delta(unit.runtime_stat_1c,
        definition_delta(replacement.profile_offense_value,
            original.profile_offense_value));
    unit.runtime_stat_20 = apply_signed_delta(unit.runtime_stat_20,
        definition_delta(replacement.profile_defense_value,
            original.profile_defense_value));
    unit.runtime_stat_28 = apply_signed_delta(unit.runtime_stat_28,
        definition_delta(replacement.initial_secondary_value,
            original.initial_secondary_value));
}

void apply_unit_type_replacement(UnitCommandContext& context, UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect) {
    if (effect.replacement_type_id == kInvalidUnitEquipmentType ||
        effect.replacement_type_id == unit.type_id) {
        return;
    }

    const u32 original_type = unit.type_id;
    const UnitMovementDefinition original_definition = unit.definition;
    const UnitMovementDefinition* replacement_definition = nullptr;
    if (context.callbacks.find_definition != nullptr) {
        replacement_definition =
            context.callbacks.find_definition(context, effect.replacement_type_id);
    }

    if (replacement_definition != nullptr) {
        apply_type_replacement_stat_delta(
            unit, original_definition, *replacement_definition);
    }

    unit.type_id = effect.replacement_type_id;
    if (replacement_definition != nullptr) {
        unit.definition = *replacement_definition;
        unit.type_flags = UnitEquipmentReplacementRuntimeTypeFlags(
            unit.type_flags, replacement_definition->type_flags);
    }

    if (context.callbacks.on_unit_type_replaced != nullptr) {
        context.callbacks.on_unit_type_replaced(
            context, unit, original_type, unit.type_id);
    }

    // ApplyUnitEquipmentEffect 0x00410529 reads the replacement definition's
    // raw +0x1f8 word (DAT_0087c4f0), not raw unit +0x58/type_flags.  Primitive
    // upgrade types 0x0b and 0x5e have type_flags bit 0x2 but no +0x1f8
    // cloaking bit; treating the former as the latter leaves command flag
    // 0x40 set and makes neutral damage reactions take the visibility/flee
    // branch instead of the original combat-reaction branch.
    const i32 equipment_command_flag_modifier =
        context.equipment_catalog != nullptr
            ? CalculateUnitEquipmentCommandFlagModifier(
                unit, *context.equipment_catalog)
            : 0;
    if (ShouldSetUnitEquipmentReplacementCommandFlag(
            unit.definition.footprint_flags,
            equipment_command_flag_modifier)) {
        unit.command_flags |= 0x40;
    }
}

u32 target_slot_for_effect(u32 requested_slot,
    const UnitEquipmentEffectDefinition& effect) {
    if (effect.category == UnitEquipmentCategory::Primary) {
        return kUnitEquipmentPrimarySlot;
    }
    if (effect.category == UnitEquipmentCategory::Secondary) {
        return kUnitEquipmentSecondarySlot;
    }
    return requested_slot;
}

bool slot_index_valid(u32 slot_index) {
    return slot_index < kUnitEquipmentSlotCount;
}

bool original_slot_code_to_slot_index(u32 original_slot_code, u32& slot_index) {
    if (original_slot_code == kUnitEquipmentOriginalPrimarySlotCode) {
        slot_index = kUnitEquipmentPrimarySlot;
        return true;
    }
    if (original_slot_code == kUnitEquipmentOriginalSecondarySlotCode) {
        slot_index = kUnitEquipmentSecondarySlot;
        return true;
    }
    if (original_slot_code >= kUnitEquipmentOriginalGenericSlotBaseCode &&
        original_slot_code < kUnitEquipmentOriginalGenericSlotBaseCode + 4) {
        slot_index = original_slot_code - kUnitEquipmentOriginalGenericSlotBaseCode;
        return true;
    }
    return false;
}

u32 original_slot_code_for_slot_index(u32 slot_index) {
    if (slot_index == kUnitEquipmentPrimarySlot) {
        return kUnitEquipmentOriginalPrimarySlotCode;
    }
    if (slot_index == kUnitEquipmentSecondarySlot) {
        return kUnitEquipmentOriginalSecondarySlotCode;
    }
    if (slot_index < 4) {
        return kUnitEquipmentOriginalGenericSlotBaseCode + slot_index;
    }
    return 0;
}

void set_unit_equipment_slot(UnitMovementUnit& unit, u32 slot_index, u32 effect_id) {
    if (!slot_index_valid(slot_index)) {
        return;
    }
    unit.equipment_slots[slot_index] = effect_id;
    if (slot_index < unit.item_slots.size()) {
        unit.item_slots[slot_index] = effect_id;
    }
}

bool remove_effect_when_generic_slot_clears(
    const UnitEquipmentEffectDefinition& effect) {
    return effect.category == UnitEquipmentCategory::Generic && effect.mode == 0;
}

bool start_equipment_progress_effect_for_clear(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 effect_id) {
    if (context.callbacks.start_equipment_progress_effect == nullptr) {
        return true;
    }
    return context.callbacks.start_equipment_progress_effect(context, unit, effect_id);
}

u32 read_le_u32(const u8* data) {
    return static_cast<u32>(data[0]) |
        (static_cast<u32>(data[1]) << 8) |
        (static_cast<u32>(data[2]) << 16) |
        (static_cast<u32>(data[3]) << 24);
}

i32 read_le_i32(const u8* data) {
    return WrappedU32ToI32(read_le_u32(data));
}

bool has_range(std::size_t size, std::size_t offset, std::size_t bytes) {
    return offset <= size && bytes <= size - offset;
}

std::string read_fixed_record_string(
    const u8* record, std::size_t offset, std::size_t capacity) {
    const auto* text = reinterpret_cast<const char*>(record + offset);
    std::size_t length = 0;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return std::string(text, length);
}

void apply_indexed_text_row(std::string& target, const IndexedTextTableContext& table,
    u32 row) {
    if (row >= table.rows.size()) {
        return;
    }
    const std::string_view text = GetIndexedTextTableRow(table, row);
    target.assign(text.begin(), text.end());
}

void ApplyStartupUnitEquipmentText(UnitEquipmentCatalog& catalog) {
    const IndexedTextTableContext& names = StartupAuxiliaryIndexedTextTable(5);
    const IndexedTextTableContext& details = StartupAuxiliaryIndexedTextTable(6);
    for (UnitEquipmentEffectDefinition& effect : catalog.effects) {
        apply_indexed_text_row(effect.display_name, names, effect.id);
        apply_indexed_text_row(effect.detail_text, details, effect.id);
    }
}

UnitEquipmentCategory category_from_u32(u32 value) {
    // JW2_10 +0x84 is a raw DWORD.  FUN_00411350 performs the category > 3
    // rejection itself; normalizing unknown values to Generic would bypass
    // that unsigned boundary and consume malformed effects.
    return static_cast<UnitEquipmentCategory>(value);
}

bool type_filter_contains(const UnitEquipmentEffectDefinition& effect, u32 type_id) {
    return std::find(effect.type_filter_type_ids.begin(),
        effect.type_filter_type_ids.end(), type_id) != effect.type_filter_type_ids.end();
}

}

const UnitEquipmentEffectDefinition* FindUnitEquipmentEffect(
    const UnitEquipmentCatalog& catalog, u32 id) {
    if (id == kInvalidUnitEquipmentId) {
        return nullptr;
    }
    for (const auto& effect : catalog.effects) {
        if (effect.id == id) {
            return &effect;
        }
    }
    return nullptr;
}

bool UnitEquipmentEffectAllowsUnitType(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect) {
    switch (effect.type_filter_mode) {
    case 0:
        return true;
    case 1:
        return type_filter_contains(effect, unit.type_id);
    case 2:
        return !type_filter_contains(effect, unit.type_id);
    case 3:
        return unit.definition.lifecycle_class == 0;
    case 4:
        return unit.definition.lifecycle_class == 2;
    default:
        return false;
    }
}

bool CheckUnitEquipmentPickupEligible(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect) {
    if ((unit.type_flags & kUnitEquipmentPickupEnabledFlag) == 0) {
        return false;
    }

    switch (effect.pickup_filter_mode) {
    case 0:
        return unit.definition.lifecycle_class == 0 ||
            unit.definition.lifecycle_class == 1;
    case 1:
        return unit.definition.lifecycle_class == 2;
    case 2:
        return true;
    case 3:
        return UnitEquipmentEffectAllowsUnitType(unit, effect);
    case 4:
    default:
        return false;
    }
}

bool IsUnitEquipmentEffectActiveForUnit(const UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect) {
    return effect.id != kInvalidUnitEquipmentId &&
        unit.type_id < 0x60 &&
        UnitEquipmentEffectAllowsUnitType(unit, effect) &&
        effect.category == UnitEquipmentCategory::Generic &&
        effect.mode <= 1;
}

bool AddUnitEquipmentGenericSlot(UnitMovementUnit& unit, u32 effect_id,
    u32* original_slot_code) {
    return AssignUnitEquipmentGenericSlot(unit, effect_id, original_slot_code);
}

bool AssignUnitEquipmentGenericSlot(UnitMovementUnit& unit, u32 effect_id,
    u32* original_slot_code) {
    for (u32 slot = 0; slot < 4; ++slot) {
        if (unit.equipment_slots[slot] == kInvalidUnitEquipmentId) {
            set_unit_equipment_slot(unit, slot, effect_id);
            if (original_slot_code != nullptr) {
                *original_slot_code = original_slot_code_for_slot_index(slot);
            }
            return true;
        }
    }
    return false;
}

PickupApplyResult apply_unit_equipment_effect_to_unit(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 amount,
    const UnitEquipmentCatalog& catalog, u32* original_slot_code) {
    const UnitEquipmentEffectDefinition* effect =
        FindUnitEquipmentEffect(catalog, effect_id);
    if (effect == nullptr || !CheckUnitEquipmentPickupEligible(unit, *effect)) {
        return PickupApplyResult::failed;
    }

    const bool type_allowed = UnitEquipmentEffectAllowsUnitType(unit, *effect);
    switch (effect->category) {
    case UnitEquipmentCategory::Generic:
        if (!type_allowed) {
            return AddUnitEquipmentGenericSlot(unit, effect_id, original_slot_code) ?
                PickupApplyResult::consume_map_effect :
                PickupApplyResult::failed;
        }

        switch (effect->mode) {
        case 0: {
            if (!AddUnitEquipmentGenericSlot(unit, effect_id, original_slot_code)) {
                return PickupApplyResult::failed;
            }
            return ApplyUnitEquipmentEffect(context, unit, *effect) ?
                PickupApplyResult::consume_map_effect :
                PickupApplyResult::failed;
        }
        case 1:
        case 2:
        case 3:
        case 6:
            return AddUnitEquipmentGenericSlot(unit, effect_id, original_slot_code) ?
                PickupApplyResult::consume_map_effect :
                PickupApplyResult::failed;
        case 4:
            return ApplyUnitEquipmentEffect(context, unit, *effect) ?
                PickupApplyResult::consume_map_effect :
                PickupApplyResult::failed;
        case 5:
            return ApplyUnitEquipmentEffect(context, unit, *effect) ?
                PickupApplyResult::keep_map_effect :
                PickupApplyResult::failed;
        default:
            return PickupApplyResult::failed;
        }

    case UnitEquipmentCategory::Primary:
        if (!type_allowed ||
            unit.equipment_slots[kUnitEquipmentPrimarySlot] != kInvalidUnitEquipmentId) {
            return AddUnitEquipmentGenericSlot(unit, effect_id, original_slot_code) ?
                PickupApplyResult::consume_map_effect :
                PickupApplyResult::failed;
        }
        if (ApplyUnitEquipmentEffect(context, unit, *effect)) {
            set_unit_equipment_slot(unit, kUnitEquipmentPrimarySlot, effect_id);
            if (original_slot_code != nullptr) {
                *original_slot_code = kUnitEquipmentOriginalPrimarySlotCode;
            }
            return PickupApplyResult::consume_map_effect;
        }
        return PickupApplyResult::failed;

    case UnitEquipmentCategory::Secondary:
        if (!type_allowed ||
            unit.equipment_slots[kUnitEquipmentSecondarySlot] != kInvalidUnitEquipmentId) {
            return AddUnitEquipmentGenericSlot(unit, effect_id, original_slot_code) ?
                PickupApplyResult::consume_map_effect :
                PickupApplyResult::failed;
        }
        if (ApplyUnitEquipmentEffect(context, unit, *effect)) {
            set_unit_equipment_slot(unit, kUnitEquipmentSecondarySlot, effect_id);
            if (original_slot_code != nullptr) {
                *original_slot_code = kUnitEquipmentOriginalSecondarySlotCode;
            }
            return PickupApplyResult::consume_map_effect;
        }
        return PickupApplyResult::failed;

    case UnitEquipmentCategory::Amount:
        // Original FUN_00411350 category 3 adds the map-effect repeat count
        // to raw unit +0x2c.  That word is the passive food/recovery reserve
        // (typed action_mode), while raw +0x4c/cargo_amount is worker cargo.
        AddUnitMeatReserve(unit, amount);
        return PickupApplyResult::consume_map_effect;
    }
    // FUN_00411350 compares the raw category with 3 and takes an unsigned JA
    // directly to its zero result.  Unknown categories must leave both the
    // unit reserve and the map effect untouched.
    return PickupApplyResult::failed;
}

bool TryApplyUnitEquipmentEffectToUnit(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 amount,
    const UnitEquipmentCatalog& catalog, u32* original_slot_code) {
    return HandleUnitEquipmentPickupApply(context, unit, effect_id, amount,
        catalog, original_slot_code) != 0;
}

u32 HandleUnitEquipmentPickupApply(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 amount,
    const UnitEquipmentCatalog& catalog, u32* original_slot_code) {
    return static_cast<u32>(apply_unit_equipment_effect_to_unit(
        context, unit, effect_id, amount, catalog, original_slot_code));
}

bool HandleUnitEquipmentMapEffectCollect(UnitCommandContext& context,
    MapEffectContext& map_effects, UnitMovementUnit& unit, i32 x, i32 y,
    const UnitEquipmentCatalog& catalog) {
    const i32 tile_x = x & ~0x1f;
    const i32 tile_y = y & ~0x1f;

    for (const u32 effect_index : map_effects.active_effect_indices) {
        if (effect_index >= map_effects.effects.size()) {
            continue;
        }
        MapEffectInstance& effect = map_effects.effects[effect_index];
        if (!effect.active ||
            (effect.x & ~0x1f) != tile_x ||
            (effect.y & ~0x1f) != tile_y) {
            continue;
        }

        const u32 result = HandleUnitEquipmentPickupApply(
            context, unit, effect.effect_id, effect.repeat_count, catalog, nullptr);
        if (result == static_cast<u32>(PickupApplyResult::consume_map_effect)) {
            ClearMapEffectTileOccupied(map_effects, effect);
            ReleaseMapEffect(map_effects, effect);
            return true;
        }
        if (result == static_cast<u32>(PickupApplyResult::keep_map_effect)) {
            return true;
        }
        return false;
    }
    return false;
}

bool TryCollectUnitEquipmentFromMapEffects(UnitCommandContext& context,
    MapEffectContext& map_effects, UnitMovementUnit& unit, i32 x, i32 y,
    const UnitEquipmentCatalog& catalog) {
    return HandleUnitEquipmentMapEffectCollect(context, map_effects, unit, x, y,
        catalog);
}

bool ClearUnitEquipmentSlot(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 original_slot_code, const UnitEquipmentCatalog& catalog) {
    if (original_slot_code == 0) {
        return start_equipment_progress_effect_for_clear(context, unit, 1);
    }

    u32 slot_index = 0;
    if (!original_slot_code_to_slot_index(original_slot_code, slot_index)) {
        return false;
    }

    const u32 effect_id = unit.equipment_slots[slot_index];
    if (effect_id == kInvalidUnitEquipmentId) {
        // FUN_00411890 treats every valid slot code as a successful clear.
        // Effect id zero reaches StartUnitProgressMapEffect, whose zero-id
        // return leaves carry clear, so the caller must not add its two-tick
        // failure lockout for an already-empty slot.
        return true;
    }

    if (!start_equipment_progress_effect_for_clear(context, unit, effect_id)) {
        return false;
    }

    const UnitEquipmentEffectDefinition* effect =
        FindUnitEquipmentEffect(catalog, effect_id);
    if (effect != nullptr) {
        if (slot_index == kUnitEquipmentPrimarySlot ||
            slot_index == kUnitEquipmentSecondarySlot ||
            remove_effect_when_generic_slot_clears(*effect)) {
            RemoveUnitEquipmentEffect(context, unit, *effect, &catalog);
        }
    }
    set_unit_equipment_slot(unit, slot_index, kInvalidUnitEquipmentId);
    return true;
}

bool TransferUnitEquipmentSlot(UnitCommandContext& context, UnitMovementUnit& source,
    UnitMovementUnit& target, u32 original_slot_code,
    const UnitEquipmentCatalog& catalog) {
    if (!UnitEquipmentTransferCapabilityEnabled(target.type_flags)) {
        return false;
    }

    if (original_slot_code == 0) {
        // Original FUN_00411b70 slot zero transfers the food reserve at raw
        // +0x2c and deliberately leaves 50 behind when the source has more.
        // It must not consume berry/resource cargo from raw +0x4c.
        if (source.action_mode == 0) {
            return false;
        }
        u32 amount = source.action_mode;
        // Original 0x00411bcc uses a signed JLE after CMP 0x32.  High-bit
        // reserve values therefore transfer in full instead of leaving 50.
        if (static_cast<i32>(amount) > 0x32) {
            amount -= 0x32;
        }
        if (TryApplyUnitEquipmentEffectToUnit(context, target, 1, amount, catalog)) {
            source.action_mode -= std::min(source.action_mode, amount);
            return true;
        }
        return false;
    }

    u32 slot_index = 0;
    if (!original_slot_code_to_slot_index(original_slot_code, slot_index)) {
        return false;
    }

    const u32 effect_id = source.equipment_slots[slot_index];
    if (effect_id == kInvalidUnitEquipmentId) {
        return false;
    }

    if (!TryApplyUnitEquipmentEffectToUnit(context, target, effect_id, 0, catalog)) {
        return false;
    }

    const UnitEquipmentEffectDefinition* effect =
        FindUnitEquipmentEffect(catalog, effect_id);
    if (effect != nullptr) {
        if (slot_index == kUnitEquipmentPrimarySlot ||
            slot_index == kUnitEquipmentSecondarySlot ||
            remove_effect_when_generic_slot_clears(*effect)) {
            RemoveUnitEquipmentEffect(context, source, *effect, &catalog);
        }
    }
    set_unit_equipment_slot(source, slot_index, kInvalidUnitEquipmentId);
    return true;
}

u32 CountUnitEquipmentEffectSlots(const UnitMovementUnit& unit, u32 effect_id) {
    u32 count = 0;
    for (u32 slot_effect : unit.equipment_slots) {
        if (slot_effect == effect_id) {
            ++count;
        }
    }
    return count;
}

i32 CalculateUnitEquipmentGenericModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog, u32 modifier_index,
    bool require_owner_below_8) {
    if (modifier_index >= kUnitEquipmentGenericModifierCount ||
        (require_owner_below_8 && unit.owner_id >= 8)) {
        return 0;
    }

    constexpr std::array<u32, kUnitEquipmentSlotCount> kOriginalSlotOrder = {
        kUnitEquipmentPrimarySlot,
        kUnitEquipmentSecondarySlot,
        kUnitEquipmentGenericSlotBase,
        kUnitEquipmentGenericSlotBase + 1,
        kUnitEquipmentGenericSlotBase + 2,
        kUnitEquipmentGenericSlotBase + 3,
    };

    i32 modifier = 0;
    for (u32 slot : kOriginalSlotOrder) {
        if (slot >= unit.equipment_slots.size()) {
            continue;
        }
        const UnitEquipmentEffectDefinition* effect =
            FindUnitEquipmentEffect(catalog, unit.equipment_slots[slot]);
        if (effect != nullptr && IsUnitEquipmentEffectActiveForUnit(unit, *effect)) {
            modifier += effect->generic_modifiers[modifier_index];
        }
    }
    return modifier;
}

i32 CalculateUnitEquipmentActionRecoveryModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentGenericModifier(unit, catalog,
        kUnitEquipmentGenericModifierActionRecovery);
}

i32 CalculateUnitEquipmentModifier22c(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentActionRecoveryModifier(unit, catalog);
}

i32 CalculateUnitEquipmentActionRangeModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentGenericModifier(unit, catalog,
        kUnitEquipmentGenericModifierActionRange);
}

i32 CalculateUnitEquipmentModifier230(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentActionRangeModifier(unit, catalog);
}

i32 CalculateUnitEquipmentMovementFrameModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentGenericModifier(unit, catalog,
        kUnitEquipmentGenericModifierMovementFrameDelta);
}

i32 CalculateUnitEquipmentInteractionRangeModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentGenericModifier(unit, catalog,
        kUnitEquipmentGenericModifierInteractionRange);
}

i32 CalculateUnitEquipmentReserved240Modifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentGenericModifier(unit, catalog,
        kUnitEquipmentGenericModifierReserved240);
}

i32 CalculateUnitEquipmentModifier240(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentReserved240Modifier(unit, catalog);
}

i32 CalculateUnitEquipmentCommandGateModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentGenericModifier(unit, catalog,
        kUnitEquipmentGenericModifierCommandGate, true);
}

i32 CalculateUnitEquipmentCommandFlagModifier(const UnitMovementUnit& unit,
    const UnitEquipmentCatalog& catalog) {
    return CalculateUnitEquipmentGenericModifier(unit, catalog,
        kUnitEquipmentGenericModifierCommandFlag, true);
}

bool ApplyUnitEquipmentEffect(UnitCommandContext& context, UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect) {
    if (effect.id == kInvalidUnitEquipmentId ||
        static_cast<u32>(effect.category) > static_cast<u32>(UnitEquipmentCategory::Secondary)) {
        return false;
    }

    apply_unit_type_replacement(context, unit, effect);

    unit.max_health = apply_signed_delta(unit.max_health, effect.max_health_delta, 1);
    unit.max_secondary_value =
        apply_signed_delta(unit.max_secondary_value, effect.max_secondary_delta);
    unit.health =
        apply_signed_clamped_delta(unit.health, effect.health_delta, unit.max_health);
    unit.secondary_value =
        apply_signed_clamped_delta(unit.secondary_value, effect.secondary_delta,
            unit.max_secondary_value);
    unit.runtime_stat_1c =
        apply_signed_delta(unit.runtime_stat_1c, effect.runtime_stat_1c_delta);
    unit.runtime_stat_20 =
        apply_signed_delta(unit.runtime_stat_20, effect.runtime_stat_20_delta);
    unit.runtime_stat_28 =
        apply_signed_delta(unit.runtime_stat_28, effect.runtime_stat_28_delta);
    // Original 0x0041065e calls FUN_00402ad6, which totals currently stored
    // active equipment slots.  A direct subtype-03 effect is not implicitly
    // installed into a slot, so its own modifier must not set this flag.
    if (context.equipment_catalog != nullptr &&
        CalculateUnitEquipmentCommandFlagModifier(
            unit, *context.equipment_catalog) > 0) {
        unit.command_flags |= 0x40;
    }
    apply_unit_experience_delta(context, unit, effect.experience_delta);
    apply_unit_level_delta(context, unit, effect.level_delta);
    unit.command_value = apply_signed_delta(unit.command_value, effect.command_value_delta);

    apply_owner_delta(context.owner_resources, unit.owner_id, effect.owner_resource_delta);
    apply_owner_delta(context.owner_resource_score, unit.owner_id,
        effect.owner_resource_delta);
    unit.equipment_flags |= 1;
    return true;
}

bool RemoveUnitEquipmentEffect(UnitCommandContext& context, UnitMovementUnit& unit,
    const UnitEquipmentEffectDefinition& effect,
    const UnitEquipmentCatalog* catalog) {
    if (effect.id == kInvalidUnitEquipmentId) {
        return false;
    }

    if (effect.replacement_type_id != kInvalidUnitEquipmentType) {
        return false;
    }

    unit.health =
        apply_signed_clamped_delta(unit.health, -effect.health_delta, unit.max_health);
    unit.secondary_value =
        apply_signed_clamped_delta(unit.secondary_value, -effect.secondary_delta,
            unit.max_secondary_value);
    unit.max_health = apply_signed_delta(unit.max_health, -effect.max_health_delta, 1);
    unit.max_secondary_value =
        apply_signed_delta(unit.max_secondary_value, -effect.max_secondary_delta);
    unit.health = clamp_to_max(unit.health, unit.max_health);
    unit.secondary_value = clamp_to_max(unit.secondary_value, unit.max_secondary_value);
    unit.runtime_stat_1c =
        apply_signed_delta(unit.runtime_stat_1c, -effect.runtime_stat_1c_delta);
    unit.runtime_stat_20 =
        apply_signed_delta(unit.runtime_stat_20, -effect.runtime_stat_20_delta);
    unit.runtime_stat_28 =
        apply_signed_delta(unit.runtime_stat_28, -effect.runtime_stat_28_delta);
    if ((unit.command_flags & 0x40u) != 0 &&
        effect.generic_modifiers[kUnitEquipmentGenericModifierCommandFlag] > 0 &&
        catalog != nullptr &&
        CalculateUnitEquipmentCommandFlagModifier(unit, *catalog) <= 1 &&
        // RemoveUnitEquipmentEffect 0x00410a92 indexes the unit definition
        // by raw type id and tests definition +0x1f8 bit 1.  It does not test
        // mutable raw unit +0x58/type_flags; BuildMan deliberately has those
        // values opposed and must clear flag 0x40 after transferring effect
        // 0x58 from its generic slot.
        (unit.definition.footprint_flags & 0x2u) == 0 &&
        (unit.command_flags & 0x800u) == 0) {
        unit.command_flags &= ~0x40u;
    }
    if (effect.experience_delta > 0) {
        remove_unit_experience_bonus(context, unit,
            static_cast<u32>(effect.experience_delta));
    }
    apply_unit_level_delta(context, unit, -effect.level_delta);
    unit.command_value = apply_signed_delta(unit.command_value, -effect.command_value_delta);

    apply_owner_delta(context.owner_resources, unit.owner_id, -effect.owner_resource_delta);
    apply_owner_delta(context.owner_resource_score, unit.owner_id,
        -effect.owner_resource_delta);
    return true;
}

bool ToggleUnitEquipmentSlotEffect(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 slot_index, u32 effect_id, const UnitEquipmentCatalog& catalog) {
    if (!slot_index_valid(slot_index)) {
        return false;
    }

    const UnitEquipmentEffectDefinition* requested =
        FindUnitEquipmentEffect(catalog, effect_id);
    if (effect_id != kInvalidUnitEquipmentId && requested == nullptr) {
        return false;
    }

    const u32 target_slot =
        requested != nullptr ? target_slot_for_effect(slot_index, *requested) : slot_index;
    if (!slot_index_valid(target_slot)) {
        return false;
    }

    const u32 previous_id = unit.equipment_slots[target_slot];
    if (previous_id != kInvalidUnitEquipmentId) {
        if (const UnitEquipmentEffectDefinition* previous =
                FindUnitEquipmentEffect(catalog, previous_id)) {
            RemoveUnitEquipmentEffect(context, unit, *previous, &catalog);
        }
        set_unit_equipment_slot(unit, target_slot, kInvalidUnitEquipmentId);
    }

    if (requested == nullptr || previous_id == effect_id) {
        return true;
    }

    if (!ApplyUnitEquipmentEffect(context, unit, *requested)) {
        return false;
    }
    set_unit_equipment_slot(unit, target_slot, effect_id);
    if (target_slot != slot_index && slot_index_valid(slot_index)) {
        set_unit_equipment_slot(unit, slot_index, kInvalidUnitEquipmentId);
    }
    return true;
}

u32 ToggleUnitEquipmentOriginalSlotEffect(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 original_slot_code,
    const UnitEquipmentCatalog& catalog) {
    if (original_slot_code == kUnitEquipmentOriginalPrimarySlotCode ||
        original_slot_code == kUnitEquipmentOriginalSecondarySlotCode) {
        const u32 slot = original_slot_code == kUnitEquipmentOriginalPrimarySlotCode
            ? kUnitEquipmentPrimarySlot
            : kUnitEquipmentSecondarySlot;
        const u32 effect_id = unit.equipment_slots[slot];
        if (effect_id == kInvalidUnitEquipmentId ||
            !AddUnitEquipmentGenericSlot(unit, effect_id)) {
            return 0;
        }
        if (const UnitEquipmentEffectDefinition* effect =
                FindUnitEquipmentEffect(catalog, effect_id)) {
            RemoveUnitEquipmentEffect(context, unit, *effect, &catalog);
        }
        set_unit_equipment_slot(unit, slot, kInvalidUnitEquipmentId);
        return 1;
    }

    u32 generic_slot = 0;
    if (!original_slot_code_to_slot_index(original_slot_code, generic_slot) ||
        generic_slot >= kUnitEquipmentPrimarySlot) {
        return 0;
    }

    const u32 effect_id = unit.equipment_slots[generic_slot];
    const UnitEquipmentEffectDefinition* effect =
        FindUnitEquipmentEffect(catalog, effect_id);
    if (effect == nullptr) {
        return 0;
    }

    if (effect->category == UnitEquipmentCategory::Generic) {
        switch (effect->mode) {
        case 0:
        case 1:
            return 1;
        case 2:
        case 3:
        case 4:
            if (!ApplyUnitEquipmentEffect(context, unit, *effect)) {
                return 0;
            }
            set_unit_equipment_slot(unit, generic_slot, kInvalidUnitEquipmentId);
            return 1;
        case 5:
            return ApplyUnitEquipmentEffect(context, unit, *effect) ? 1 : 0;
        case 6:
            return 2;
        default:
            return 0;
        }
    }

    const bool primary = effect->category == UnitEquipmentCategory::Primary;
    const bool secondary = effect->category == UnitEquipmentCategory::Secondary;
    if (!primary && !secondary) {
        return 0;
    }

    const u32 target_slot =
        primary ? kUnitEquipmentPrimarySlot : kUnitEquipmentSecondarySlot;
    const u32 previous_id = unit.equipment_slots[target_slot];
    if (previous_id == effect_id ||
        !ApplyUnitEquipmentEffect(context, unit, *effect)) {
        return 0;
    }

    if (previous_id == kInvalidUnitEquipmentId) {
        set_unit_equipment_slot(unit, generic_slot, kInvalidUnitEquipmentId);
    } else {
        if (const UnitEquipmentEffectDefinition* previous =
                FindUnitEquipmentEffect(catalog, previous_id)) {
            RemoveUnitEquipmentEffect(context, unit, *previous, &catalog);
        }
        set_unit_equipment_slot(unit, generic_slot, previous_id);
    }
    set_unit_equipment_slot(unit, target_slot, effect_id);
    return 1;
}

void PublishUnitEquipmentSlots(UnitEquipmentPublishState& state,
    const UnitMovementUnit& unit, u32 group_index) {
    const u32 owner = unit.owner_id < state.owner_slots.size() ? unit.owner_id : 0;
    state.owner_slots[owner] = unit.equipment_slots;
    if (group_index < state.local_group_slots.size()) {
        state.local_group_slots[group_index] = unit.equipment_slots;
    }
    state.last_owner = owner;
    state.last_group = group_index;
    state.dirty = true;
    ++state.publish_count;
}

bool PublishLocalEquipmentSlotIfAllowed(const UnitMovementUnit& unit, u32 original_slot_code,
    u32 local_owner, const UnitEquipmentCatalog& catalog,
    UnitEquipmentCommandPublishCallback publish_command) {
    if (original_slot_code == 0 || unit.owner_id != local_owner) {
        return false;
    }

    u32 slot_index = 0;
    if (!original_slot_code_to_slot_index(original_slot_code, slot_index)) {
        return false;
    }
    const u32 equipment_id = unit.equipment_slots[slot_index];
    if (equipment_id == kInvalidUnitEquipmentId) {
        return false;
    }

    const UnitEquipmentEffectDefinition* effect =
        FindUnitEquipmentEffect(catalog, equipment_id);
    if (effect != nullptr && !UnitEquipmentEffectAllowsUnitType(unit, *effect)) {
        return false;
    }
    if (publish_command != nullptr) {
        return publish_command(unit, original_slot_code, equipment_id);
    }
    return true;
}

bool LoadUnitEquipmentCatalogFromBytes(const void* data, std::size_t size,
    UnitEquipmentCatalog& catalog, u32* version) {
    catalog.effects.clear();
    if (version != nullptr) {
        *version = 0;
    }
    if (data == nullptr || !has_range(size, 0, 8)) {
        return false;
    }

    const auto* bytes = static_cast<const u8*>(data);
    const u32 file_version = read_le_u32(bytes);
    const u32 count = read_le_u32(bytes + 4);
    if (version != nullptr) {
        *version = file_version;
    }
    if (file_version != 0x65 || count >= 0x97) {
        return false;
    }

    constexpr std::size_t kHeaderBytes = 8;
    constexpr std::size_t kRecordBytes = 0x28c;
    constexpr std::size_t kDisplayNameOffset = 0x00;
    constexpr std::size_t kDisplayNameBytes = 0x40;
    constexpr std::size_t kDetailTextOffset = 0x40;
    constexpr std::size_t kDetailTextBytes = 0x40;
    if (!has_range(size, kHeaderBytes, static_cast<std::size_t>(count) * kRecordBytes)) {
        return false;
    }

    catalog.effects.reserve(count);
    for (u32 index = 0; index < count; ++index) {
        const u8* record = bytes + kHeaderBytes + static_cast<std::size_t>(index) * kRecordBytes;
        UnitEquipmentEffectDefinition effect{};
        effect.id = index;
        effect.display_name = read_fixed_record_string(
            record, kDisplayNameOffset, kDisplayNameBytes);
        effect.detail_text = read_fixed_record_string(
            record, kDetailTextOffset, kDetailTextBytes);
        effect.type_filter_mode = read_le_u32(record + 0x100);
        const u32 type_filter_count = std::min<u32>(
            read_le_u32(record + 0x104), kUnitEquipmentTypeFilterMaxCount);
        effect.type_filter_type_ids.reserve(type_filter_count);
        for (u32 type_index = 0; type_index < type_filter_count; ++type_index) {
            effect.type_filter_type_ids.push_back(record[0x108 + type_index]);
        }
        effect.pickup_filter_mode = read_le_u32(record + 0xa8);
        effect.category = category_from_u32(read_le_u32(record + 0x84));
        effect.icon_frame_index = read_le_u32(record + 0x88);
        effect.icon_marker_code = record[0xd8];
        effect.mode = read_le_u32(record + 0x208);
        effect.ambient_flags = read_le_u32(record + 0xa4);
        effect.ambient_spawn_rate = read_le_u32(record + 0xb4);
        effect.map_effect_frame_period = read_le_u32(record + 0xb0);
        effect.completion_terrain_effect_period = read_le_u32(record + 0xc8);
        effect.tooltip_primary_cost = read_le_u32(record + 0xcc);
        effect.tooltip_secondary_cost = read_le_u32(record + 0xd4);
        effect.attachment_definition_id = read_le_u32(record + 0x260);
        effect.replacement_type_id = read_le_u32(record + 0x26c);
        effect.max_health_delta = read_le_i32(record + 0x210);
        effect.max_secondary_delta = read_le_i32(record + 0x214);
        effect.health_delta = read_le_i32(record + 0x218);
        effect.secondary_delta = read_le_i32(record + 0x21c);
        effect.runtime_stat_1c_delta = read_le_i32(record + 0x220);
        effect.runtime_stat_20_delta = read_le_i32(record + 0x224);
        effect.runtime_stat_28_delta = read_le_i32(record + 0x228);
        effect.experience_delta = read_le_i32(record + 0x24c);
        effect.level_delta = read_le_i32(record + 0x250);
        effect.owner_resource_delta = read_le_i32(record + 0x254);
        effect.command_value_delta = read_le_i32(record + 0x258);
        effect.generic_modifiers[kUnitEquipmentGenericModifierActionRecovery] =
            read_le_i32(record + 0x22c);
        effect.generic_modifiers[kUnitEquipmentGenericModifierActionRange] =
            read_le_i32(record + 0x230);
        effect.generic_modifiers[kUnitEquipmentGenericModifierMovementFrameDelta] =
            read_le_i32(record + 0x234);
        effect.generic_modifiers[kUnitEquipmentGenericModifierInteractionRange] =
            read_le_i32(record + 0x238);
        effect.generic_modifiers[kUnitEquipmentGenericModifierReserved240] =
            read_le_i32(record + 0x240);
        effect.generic_modifiers[kUnitEquipmentGenericModifierCommandGate] =
            read_le_i32(record + 0x244);
        effect.generic_modifiers[kUnitEquipmentGenericModifierCommandFlag] =
            read_le_i32(record + 0x248);
        effect.movement_frame_delta_modifier =
            effect.generic_modifiers[kUnitEquipmentGenericModifierMovementFrameDelta];
        catalog.effects.push_back(std::move(effect));
    }
    return true;
}

bool LoadUnitEquipmentCatalogFromJw210Trc(UnitEquipmentCatalog& catalog,
    const char* archive_name, u32 record_index, u32* version) {
    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    std::array<u8, 8> header{};
    if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size())) {
        CloseTrcRecordReader(reader);
        return false;
    }

    const u32 file_version = read_le_u32(header.data());
    const u32 count = read_le_u32(header.data() + 4);
    if (version != nullptr) {
        *version = file_version;
    }
    if (file_version != 0x65 || count >= 0x97) {
        CloseTrcRecordReader(reader);
        return false;
    }

    constexpr std::size_t kHeaderBytes = 8;
    constexpr std::size_t kRecordBytes = 0x28c;
    std::vector<u8> payload(kHeaderBytes +
        static_cast<std::size_t>(count) * kRecordBytes);
    std::copy(header.begin(), header.end(), payload.begin());

    ServeMilesSound();
    if (!ReadOpenTrcRecordBytes(reader, payload.data() + kHeaderBytes,
            payload.size() - kHeaderBytes)) {
        CloseTrcRecordReader(reader);
        return false;
    }
    CloseTrcRecordReader(reader);

    if (!LoadUnitEquipmentCatalogFromBytes(payload.data(), payload.size(), catalog,
            version)) {
        return false;
    }
    ApplyStartupUnitEquipmentText(catalog);
    return true;
}

bool LoadUnitEquipmentCatalogFromJw210TrcRecord2(UnitEquipmentCatalog& catalog,
    u32* version) {
    return LoadUnitEquipmentCatalogFromJw210Trc(catalog, "JW2_10.TRC", 2,
        version);
}

}
