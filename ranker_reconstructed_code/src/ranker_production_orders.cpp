#include "ranker_production_orders.h"

#include "ranker_indexed_text_table.h"
#include "ranker_miles.h"
#include "ranker_trc.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace ranker {
namespace {

bool order_index_valid(u32 order_id) {
    return order_id < kProductionOrderCount;
}

bool owner_index_valid(u32 owner) {
    return owner < kProductionOrderOwnerCount;
}

constexpr std::array<std::size_t, kProductionOrderCompletionEffectCount>
    kCompletionEffectRuleOffsets{
        0x274, 0x28c, 0x2a4, 0x2bc, 0x2d4, 0x2ec,
        0x304, 0x31c, 0x334, 0x34c, 0x364, 0x37c,
        0x394, 0x3ac, 0x3c4, 0x3dc, 0x3f4, 0x40c,
    };

u32 read_le_u32(const u8* data) {
    return static_cast<u32>(data[0]) |
        (static_cast<u32>(data[1]) << 8) |
        (static_cast<u32>(data[2]) << 16) |
        (static_cast<u32>(data[3]) << 24);
}

i32 read_le_i32(const u8* data) {
    return static_cast<i32>(read_le_u32(data));
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

void ApplyStartupProductionOrderText(ProductionOrderCatalog& catalog) {
    const IndexedTextTableContext& names = StartupAuxiliaryIndexedTextTable(3);
    const IndexedTextTableContext& details = StartupAuxiliaryIndexedTextTable(4);
    for (ProductionOrderDefinition& definition : catalog.definitions) {
        apply_indexed_text_row(definition.display_name, names, definition.id);
        apply_indexed_text_row(definition.detail_text, details, definition.id);
    }
}

bool prerequisite_check_bypassed(u32 order_id, u32 variant) {
    if (order_id == 3 || order_id == 4 || order_id == 0x21 || order_id == 0x22) {
        return variant == 0;
    }
    if (order_id == 0x19 || order_id == 0x1a || order_id == 0x1c ||
        order_id == 0x1d) {
        return variant < 2;
    }
    return false;
}

bool has_prerequisites(const ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner, u32 variant) {
    if (definition.prerequisite_type_ids.empty() ||
        prerequisite_check_bypassed(definition.id, variant)) {
        return true;
    }

    for (u32 type_id : definition.prerequisite_type_ids) {
        if (type_id >= kProductionOrderTypeCount ||
            state.completed_type_counts[owner][type_id] == 0) {
            return false;
        }
    }
    return true;
}

u32 positive_cost(i32 value) {
    return value > 0 ? static_cast<u32>(value) : 0;
}

ProductionOrderCostRule read_cost_rule(const u8* record, std::size_t offset) {
    ProductionOrderCostRule rule{};
    rule.base = read_le_i32(record + offset);
    rule.mode = read_le_u32(record + offset + 4);
    rule.linear = read_le_i32(record + offset + 8);
    // CalculateProductionOrderCostRule (0x0043b580) consumes the fifth
    // argument loaded from rule +0x0c.  The two trailing DWORDs at +0x10 and
    // +0x14 are passed by the wrappers but are not read by modes 0..4.
    rule.extra = read_le_i32(record + offset + 0x0c);
    return rule;
}

}

i32 CalculateProductionOrderRuleValue(const ProductionOrderCostRule& rule, u32 variant) {
    i32 value = rule.base;
    const i32 signed_variant = static_cast<i32>(variant);
    switch (rule.mode) {
    case 0:
        break;
    case 1:
        value += rule.linear * signed_variant;
        break;
    case 2:
        value += rule.linear * signed_variant + rule.extra;
        break;
    case 3:
        if (rule.extra != 0) {
            value += rule.linear * (signed_variant / rule.extra);
        }
        break;
    case 4: {
        i32 triangular = 0;
        for (u32 index = 1; index <= variant; ++index) {
            triangular += rule.extra * static_cast<i32>(index);
        }
        value += rule.linear * signed_variant + triangular;
        break;
    }
    default:
        break;
    }
    return value;
}

u32 CalculateProductionOrderCost(const ProductionOrderCostRule& rule, u32 variant) {
    return positive_cost(CalculateProductionOrderRuleValue(rule, variant));
}

u32 CalculateProductionOrderDuration(const ProductionOrderDefinition& definition,
    u32 variant) {
    return positive_cost(CalculateProductionOrderRuleValue(definition.duration_ticks, variant));
}

void ResetProductionOrderRuntimeState(ProductionOrderRuntimeState& state) {
    state = ProductionOrderRuntimeState{};
}

bool SwapProductionOrderOwnerState(ProductionOrderRuntimeState& state, u32 first_owner,
    u32 second_owner) {
    if (!owner_index_valid(first_owner) || !owner_index_valid(second_owner)) {
        return false;
    }
    if (first_owner == second_owner) {
        return true;
    }

    for (u32 order = 0; order < kProductionOrderCount; ++order) {
        std::swap(state.variant_counts[first_owner][order],
            state.variant_counts[second_owner][order]);
        std::swap(state.lock_flags[first_owner][order],
            state.lock_flags[second_owner][order]);
    }

    std::swap(state.order_2b_bonus_totals[first_owner],
        state.order_2b_bonus_totals[second_owner]);

    for (u32 type_id = 0; type_id < kProductionOrderTypeCount; ++type_id) {
        for (auto& table : state.completion_effect_totals) {
            std::swap(table[first_owner][type_id], table[second_owner][type_id]);
        }
    }
    return true;
}

i32 GetProductionOrderCompletionEffectTotal(const ProductionOrderRuntimeState& state,
    u32 effect_index, u32 owner, u32 type_id) {
    if (effect_index >= kProductionOrderCompletionEffectCount ||
        !owner_index_valid(owner) || type_id >= kProductionOrderTypeCount) {
        return 0;
    }
    return state.completion_effect_totals[effect_index][owner][type_id];
}

i32 GetProductionOrderOrder2bBonusTotal(const ProductionOrderRuntimeState& state,
    u32 owner) {
    return owner_index_valid(owner) ? state.order_2b_bonus_totals[owner] : 0;
}

std::array<i32, kProductionOrderCompletionEffectCount>
CalculateProductionOrderCompletionEffects(const ProductionOrderDefinition& definition,
    u32 variant) {
    std::array<i32, kProductionOrderCompletionEffectCount> effects{};
    for (std::size_t index = 0; index < effects.size(); ++index) {
        effects[index] =
            CalculateProductionOrderRuleValue(definition.completion_effects[index], variant);
    }
    return effects;
}

i32 CalculateProductionOrderCompletionEffectSlot(
    const ProductionOrderDefinition& definition, u32 variant, u32 effect_index) {
    if (effect_index >= definition.completion_effects.size()) {
        return 0;
    }
    return CalculateProductionOrderRuleValue(
        definition.completion_effects[effect_index], variant);
}

i32 CalculateProductionOrderCompletionEffect00(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 0);
}

i32 CalculateProductionOrderCompletionEffect01(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 1);
}

i32 CalculateProductionOrderCompletionEffect02(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 2);
}

i32 CalculateProductionOrderCompletionEffect03(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 3);
}

i32 CalculateProductionOrderCompletionEffect04(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 4);
}

i32 CalculateProductionOrderCompletionEffect05(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 5);
}

i32 CalculateProductionOrderCompletionEffect06(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 6);
}

i32 CalculateProductionOrderCompletionEffect07(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 7);
}

i32 CalculateProductionOrderCompletionEffect08(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 8);
}

i32 CalculateProductionOrderCompletionEffect09(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 9);
}

i32 CalculateProductionOrderCompletionEffect10(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 10);
}

i32 CalculateProductionOrderCompletionEffect11(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 11);
}

i32 CalculateProductionOrderCompletionEffect12(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 12);
}

i32 CalculateProductionOrderCompletionEffect13(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 13);
}

i32 CalculateProductionOrderCompletionEffect14(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 14);
}

i32 CalculateProductionOrderCompletionEffect15(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 15);
}

i32 CalculateProductionOrderCompletionEffect16(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 16);
}

i32 CalculateProductionOrderCompletionEffect17(
    const ProductionOrderDefinition& definition, u32 variant) {
    return CalculateProductionOrderCompletionEffectSlot(definition, variant, 17);
}

ProductionOrderCheckResult CheckProductionOrderAvailability(
    const ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner) {
    ProductionOrderCheckResult result{};
    result.order_id = definition.id;
    result.owner = owner;

    if (!owner_index_valid(owner) || !order_index_valid(definition.id)) {
        result.code = static_cast<u32>(ProductionOrderAvailabilityCode::locked);
        return result;
    }

    const u32 variant = state.variant_counts[owner][definition.id];
    result.variant = variant;
    result.primary_cost = CalculateProductionOrderCost(definition.primary_cost, variant);
    result.secondary_cost = CalculateProductionOrderCost(definition.secondary_cost, variant);

    if (definition.max_variant_count <= variant) {
        result.code =
            static_cast<u32>(ProductionOrderAvailabilityCode::variant_limit_reached);
        return result;
    }
    if ((state.lock_flags[owner][definition.id] & 3) != 0) {
        result.code = static_cast<u32>(ProductionOrderAvailabilityCode::locked);
        return result;
    }
    if (!has_prerequisites(state, definition, owner, variant)) {
        result.code =
            static_cast<u32>(ProductionOrderAvailabilityCode::missing_prerequisite);
        return result;
    }
    if (state.owner_primary_resources[owner] < result.primary_cost) {
        result.code =
            static_cast<u32>(ProductionOrderAvailabilityCode::missing_primary_resource);
        return result;
    }
    if (state.owner_secondary_resources[owner] < result.secondary_cost) {
        result.code =
            static_cast<u32>(ProductionOrderAvailabilityCode::missing_secondary_resource);
        return result;
    }

    result.available = true;
    result.code = definition.id;
    return result;
}

ProductionOrderCheckResult CheckProductionOrderAvailabilityForUi(
    const ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner) {
    return CheckProductionOrderAvailability(state, definition, owner);
}

ProductionOrderCheckResult CheckProductionOrderStartPrerequisites(
    const ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner) {
    ProductionOrderCheckResult result{};
    result.order_id = definition.id;
    result.owner = owner;

    if (!owner_index_valid(owner) || !order_index_valid(definition.id)) {
        result.code = static_cast<u32>(ProductionOrderAvailabilityCode::locked);
        return result;
    }

    const u32 variant = state.variant_counts[owner][definition.id];
    result.variant = variant;
    result.primary_cost = CalculateProductionOrderCost(definition.primary_cost, variant);
    result.secondary_cost = CalculateProductionOrderCost(definition.secondary_cost, variant);

    if (definition.max_variant_count <= variant) {
        result.code =
            static_cast<u32>(ProductionOrderAvailabilityCode::variant_limit_reached);
        return result;
    }
    if ((state.lock_flags[owner][definition.id] & 1) != 0) {
        result.code = static_cast<u32>(ProductionOrderAvailabilityCode::locked);
        return result;
    }
    if (!has_prerequisites(state, definition, owner, variant)) {
        result.code =
            static_cast<u32>(ProductionOrderAvailabilityCode::missing_prerequisite);
        return result;
    }

    result.available = true;
    result.code = definition.id;
    return result;
}

void ClearProductionOrderLockFlags(ProductionOrderRuntimeState& state, u32 order_id,
    u32 owner) {
    if (!owner_index_valid(owner) || !order_index_valid(order_id)) {
        return;
    }
    state.lock_flags[owner][order_id] &= ~3u;
}

bool StartSelectedUnitProductionOrder(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, ProductionOrderUnitRuntime& unit) {
    const ProductionOrderCheckResult check =
        CheckProductionOrderStartPrerequisites(state, definition, unit.owner);
    if (!check.available) {
        return false;
    }

    unit.current_order_id = definition.id;
    unit.progress_ticks = 0;
    unit.queued_order_id = 0;
    unit.active = true;
    state.lock_flags[unit.owner][definition.id] |= 1u;
    state.lock_flags[unit.owner][definition.id] &= ~2u;
    return true;
}

bool DebitProductionOrderPrimaryCost(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner) {
    if (!owner_index_valid(owner) || !order_index_valid(definition.id)) {
        return false;
    }

    const u32 variant = state.variant_counts[owner][definition.id];
    const u32 cost = CalculateProductionOrderCost(definition.primary_cost, variant);
    if (state.owner_primary_resources[owner] < cost) {
        return false;
    }

    state.owner_primary_resources[owner] -= cost;
    return true;
}

void RefundProductionOrderCosts(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner,
    bool secondary_uses_primary_formula) {
    if (!owner_index_valid(owner) || !order_index_valid(definition.id)) {
        return;
    }

    const u32 variant = state.variant_counts[owner][definition.id];
    const u32 primary = CalculateProductionOrderCost(definition.primary_cost, variant);
    const u32 secondary = secondary_uses_primary_formula
        ? primary
        : CalculateProductionOrderCost(definition.secondary_cost, variant);
    state.owner_primary_resources[owner] += primary;
    state.owner_secondary_resources[owner] += secondary;
}

ProductionOrderCompletionResult CompleteProductionOrder(
    ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner) {
    ProductionOrderCompletionResult result{};
    result.order_id = definition.id;
    result.owner = owner;

    if (!owner_index_valid(owner) || !order_index_valid(definition.id)) {
        return result;
    }

    state.lock_flags[owner][definition.id] &= ~1u;
    ++state.variant_counts[owner][definition.id];

    result.completed = true;
    result.variant = state.variant_counts[owner][definition.id];
    result.effect_deltas =
        CalculateProductionOrderCompletionEffects(definition, result.variant);

    for (u32 type_id : definition.affected_type_ids) {
        if (type_id >= kProductionOrderTypeCount) {
            continue;
        }
        for (std::size_t effect = 0; effect < result.effect_deltas.size(); ++effect) {
            state.completion_effect_totals[effect][owner][type_id] +=
                result.effect_deltas[effect];
        }
    }

    if (definition.id == 0x2b) {
        result.order_2b_bonus_delta = result.effect_deltas[11];
        state.order_2b_bonus_totals[owner] += result.order_2b_bonus_delta;
        ++state.order_2b_refresh_requests;
        result.order_2b_refresh_requested = true;
    }

    return result;
}

ProductionOrderCompletionResult AdvanceProductionOrderProgress(
    ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner, u32& progress_ticks) {
    ProductionOrderCompletionResult result{};
    result.order_id = definition.id;
    result.owner = owner;

    if (!owner_index_valid(owner) || !order_index_valid(definition.id)) {
        return result;
    }

    ++progress_ticks;
    const u32 variant = state.variant_counts[owner][definition.id];
    if (CalculateProductionOrderDuration(definition, variant) <= progress_ticks) {
        return CompleteProductionOrder(state, definition, owner);
    }
    return result;
}

bool LoadProductionOrderCatalogFromBytes(const void* data, std::size_t size,
    ProductionOrderCatalog& catalog) {
    catalog = ProductionOrderCatalog{};
    if (data == nullptr || !has_range(size, 0, 8)) {
        return false;
    }

    const auto* bytes = static_cast<const u8*>(data);
    catalog.version = read_le_u32(bytes);
    const u32 count = read_le_u32(bytes + 4);
    if (count > kProductionOrderCount) {
        return false;
    }

    constexpr std::size_t kHeaderBytes = 8;
    constexpr std::size_t kRecordBytes = 0x44c;
    constexpr std::size_t kDisplayNameOffset = 0x00;
    constexpr std::size_t kDisplayNameBytes = 0x80;
    constexpr std::size_t kDetailTextOffset = 0x80;
    constexpr std::size_t kDetailTextBytes = 0x80;
    constexpr std::size_t kAffectedTypeCountOffset = 0x104;
    constexpr std::size_t kAffectedTypeListOffset = 0x108;
    constexpr std::size_t kPrerequisiteCountOffset = 0x188;
    constexpr std::size_t kPrerequisiteListOffset = 0x18c;
    constexpr std::size_t kIconMarkerOffset = 0x20c;
    constexpr std::size_t kMaxVariantOffset = 0x210;
    constexpr std::size_t kDurationOffset = 0x214;
    constexpr std::size_t kPrimaryCostOffset = 0x22c;
    constexpr std::size_t kAuxiliaryCostOffset = 0x244;
    constexpr std::size_t kSecondaryCostOffset = 0x25c;

    if (!has_range(size, kHeaderBytes, static_cast<std::size_t>(count) * kRecordBytes)) {
        return false;
    }

    catalog.definitions.reserve(count);
    for (u32 index = 0; index < count; ++index) {
        const u8* record = bytes + kHeaderBytes + static_cast<std::size_t>(index) * kRecordBytes;
        ProductionOrderDefinition definition{};
        definition.id = index;
        definition.display_name = read_fixed_record_string(
            record, kDisplayNameOffset, kDisplayNameBytes);
        definition.detail_text = read_fixed_record_string(
            record, kDetailTextOffset, kDetailTextBytes);
        definition.icon_marker_code = record[kIconMarkerOffset];
        definition.max_variant_count = read_le_u32(record + kMaxVariantOffset);
        definition.duration_ticks = read_cost_rule(record, kDurationOffset);
        definition.primary_cost = read_cost_rule(record, kPrimaryCostOffset);
        definition.auxiliary_cost = read_cost_rule(record, kAuxiliaryCostOffset);
        definition.secondary_cost = read_cost_rule(record, kSecondaryCostOffset);

        for (std::size_t effect = 0; effect < definition.completion_effects.size();
             ++effect) {
            definition.completion_effects[effect] =
                read_cost_rule(record, kCompletionEffectRuleOffsets[effect]);
        }

        const u32 affected_type_count = read_le_u32(record + kAffectedTypeCountOffset);
        const u32 clamped_affected_types = std::min<u32>(affected_type_count, 0x20);
        definition.affected_type_ids.reserve(clamped_affected_types);
        for (u32 affected = 0; affected < clamped_affected_types; ++affected) {
            definition.affected_type_ids.push_back(
                read_le_u32(record + kAffectedTypeListOffset + affected * 4));
        }

        const u32 prerequisite_count = read_le_u32(record + kPrerequisiteCountOffset);
        const u32 clamped_prerequisites = std::min<u32>(prerequisite_count, 0x20);
        definition.prerequisite_type_ids.reserve(clamped_prerequisites);
        for (u32 prereq = 0; prereq < clamped_prerequisites; ++prereq) {
            definition.prerequisite_type_ids.push_back(
                read_le_u32(record + kPrerequisiteListOffset + prereq * 4));
        }
        catalog.definitions.push_back(std::move(definition));
    }
    return true;
}

bool LoadProductionOrderCatalogFromJw210Trc(ProductionOrderCatalog& catalog,
    const char* archive_name, u32 record_index) {
    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    std::array<u8, 8> header{};
    ServeMilesSound();
    if (!ReadOpenTrcRecordBytes(reader, header.data(), 4)) {
        CloseTrcRecordReader(reader);
        return false;
    }
    ServeMilesSound();
    if (!ReadOpenTrcRecordBytes(reader, header.data() + 4, 4)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    const u32 count = read_le_u32(header.data() + 4);
    if (count > kProductionOrderCount) {
        CloseTrcRecordReader(reader);
        return false;
    }

    constexpr std::size_t kHeaderBytes = 8;
    constexpr std::size_t kRecordBytes = 0x44c;
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

    ServeMilesSound();
    if (!LoadProductionOrderCatalogFromBytes(payload.data(), payload.size(), catalog)) {
        return false;
    }
    ApplyStartupProductionOrderText(catalog);
    return true;
}

bool LoadProductionOrderCatalogFromJw210TrcRecord0(
    ProductionOrderCatalog& catalog) {
    return LoadProductionOrderCatalogFromJw210Trc(catalog, "JW2_10.TRC", 0);
}

u32 CalculateWorkerHarvestAmountWithProductionEffect10(
    const ProductionOrderRuntimeState& state, u32 owner, u32 type_id) {
    return static_cast<u32>(
        GetProductionOrderCompletionEffectTotal(state, 10, owner, type_id)) + 0x0cu;
}

}
