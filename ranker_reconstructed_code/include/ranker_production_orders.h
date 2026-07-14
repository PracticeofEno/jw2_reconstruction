#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kProductionOrderOwnerCount = 8;
constexpr u32 kProductionOrderCount = 0x40;
constexpr u32 kProductionOrderTypeCount = 0xaa;
constexpr u32 kProductionOrderCompletionEffectCount = 18;

using ProductionOrderOwnerTypeTable =
    std::array<std::array<i32, kProductionOrderTypeCount>, kProductionOrderOwnerCount>;

enum class ProductionOrderAvailabilityCode : u32 {
    missing_primary_resource = 0,
    missing_secondary_resource = 1,
    variant_limit_reached = 3,
    missing_prerequisite = 4,
    locked = 0x10,
};

struct ProductionOrderCostRule {
    i32 base = 0;
    u32 mode = 0;
    i32 linear = 0;
    i32 extra = 0;
};

struct ProductionOrderDefinition {
    u32 id = 0;
    std::string display_name;
    std::string detail_text;
    u32 max_variant_count = 0;
    u32 icon_marker_code = 0;
    ProductionOrderCostRule duration_ticks;
    ProductionOrderCostRule primary_cost;
    ProductionOrderCostRule auxiliary_cost;
    ProductionOrderCostRule secondary_cost;
    std::array<ProductionOrderCostRule, kProductionOrderCompletionEffectCount>
        completion_effects{};
    std::vector<u32> affected_type_ids;
    std::vector<u32> prerequisite_type_ids;
};

struct ProductionOrderRuntimeState {
    std::array<std::array<u8, kProductionOrderCount>, kProductionOrderOwnerCount>
        variant_counts{};
    std::array<std::array<u8, kProductionOrderCount>, kProductionOrderOwnerCount>
        lock_flags{};
    std::array<std::array<std::array<u8, 2>, kProductionOrderCount>,
        kProductionOrderOwnerCount> order_cell_opaque_bytes{};
    std::array<std::array<u32, kProductionOrderTypeCount>, kProductionOrderOwnerCount>
        completed_type_counts{};
    std::array<ProductionOrderOwnerTypeTable, kProductionOrderCompletionEffectCount>
        completion_effect_totals{};
    std::array<u32, kProductionOrderOwnerCount> owner_primary_resources{};
    std::array<u32, kProductionOrderOwnerCount> owner_secondary_resources{};
    std::array<i32, kProductionOrderOwnerCount> order_2b_bonus_totals{};
    u32 order_2b_refresh_requests = 0;
};

struct ProductionOrderCatalog {
    u32 version = 0;
    std::vector<ProductionOrderDefinition> definitions;
};

struct ProductionOrderCheckResult {
    u32 code = static_cast<u32>(ProductionOrderAvailabilityCode::locked);
    u32 order_id = 0;
    u32 owner = 0;
    u32 variant = 0;
    u32 primary_cost = 0;
    u32 secondary_cost = 0;
    bool available = false;
};

struct ProductionOrderCompletionResult {
    bool completed = false;
    u32 order_id = 0;
    u32 owner = 0;
    u32 variant = 0;
    std::array<i32, kProductionOrderCompletionEffectCount> effect_deltas{};
    i32 order_2b_bonus_delta = 0;
    bool order_2b_refresh_requested = false;
};

struct ProductionOrderUnitRuntime {
    u32 owner = 0;
    u32 current_order_id = 0;
    u32 progress_ticks = 0;
    u32 queued_order_id = 0;
    bool active = false;
};

i32 CalculateProductionOrderRuleValue(const ProductionOrderCostRule& rule, u32 variant);
u32 CalculateProductionOrderCost(const ProductionOrderCostRule& rule, u32 variant);
u32 CalculateProductionOrderDuration(const ProductionOrderDefinition& definition,
    u32 variant);
void ResetProductionOrderRuntimeState(ProductionOrderRuntimeState& state);
bool SwapProductionOrderOwnerState(ProductionOrderRuntimeState& state, u32 first_owner,
    u32 second_owner);
i32 GetProductionOrderCompletionEffectTotal(const ProductionOrderRuntimeState& state,
    u32 effect_index, u32 owner, u32 type_id);
i32 GetProductionOrderOrder2bBonusTotal(const ProductionOrderRuntimeState& state,
    u32 owner);
std::array<i32, kProductionOrderCompletionEffectCount>
CalculateProductionOrderCompletionEffects(const ProductionOrderDefinition& definition,
    u32 variant);
i32 CalculateProductionOrderCompletionEffectSlot(
    const ProductionOrderDefinition& definition, u32 variant, u32 effect_index);
i32 CalculateProductionOrderCompletionEffect00(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect01(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect02(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect03(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect04(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect05(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect06(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect07(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect08(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect09(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect10(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect11(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect12(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect13(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect14(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect15(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect16(
    const ProductionOrderDefinition& definition, u32 variant);
i32 CalculateProductionOrderCompletionEffect17(
    const ProductionOrderDefinition& definition, u32 variant);
ProductionOrderCheckResult CheckProductionOrderAvailability(
    const ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner);
ProductionOrderCheckResult CheckProductionOrderAvailabilityForUi(
    const ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner);
ProductionOrderCheckResult CheckProductionOrderStartPrerequisites(
    const ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner);
void ClearProductionOrderLockFlags(ProductionOrderRuntimeState& state, u32 order_id,
    u32 owner);
bool StartSelectedUnitProductionOrder(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, ProductionOrderUnitRuntime& unit);
bool DebitProductionOrderPrimaryCost(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner);
void RefundProductionOrderCosts(ProductionOrderRuntimeState& state,
    const ProductionOrderDefinition& definition, u32 owner,
    bool secondary_uses_primary_formula = true);
ProductionOrderCompletionResult CompleteProductionOrder(
    ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner);
ProductionOrderCompletionResult AdvanceProductionOrderProgress(
    ProductionOrderRuntimeState& state, const ProductionOrderDefinition& definition,
    u32 owner, u32& progress_ticks);
bool LoadProductionOrderCatalogFromBytes(const void* data, std::size_t size,
    ProductionOrderCatalog& catalog);
bool LoadProductionOrderCatalogFromJw210Trc(ProductionOrderCatalog& catalog,
    const char* archive_name = "JW2_10.TRC", u32 record_index = 0);
bool LoadProductionOrderCatalogFromJw210TrcRecord0(
    ProductionOrderCatalog& catalog);
u32 CalculateWorkerHarvestAmountWithProductionEffect10(
    const ProductionOrderRuntimeState& state, u32 owner, u32 type_id);

}
