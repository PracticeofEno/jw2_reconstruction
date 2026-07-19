#include "ranker_production_orders.h"

#include "ranker_indexed_text_table.h"
#include "ranker_trc.h"

#include <cstdlib>
#include <iostream>
#include <limits>

// This focused executable links ranker_production_orders.cpp directly.  The
// catalog-loader dependencies are not exercised here, but COFF keeps that
// public section, so provide inert test-only endpoints instead of pulling the
// complete Win32/TRC/audio runtime into an arithmetic regression.
namespace ranker {

bool OpenTrcRecordDirectoryEntry(TrcRecordReader&, const char*, u32) {
    return false;
}

bool OpenTrcRecordPayload(TrcRecordReader&) {
    return false;
}

bool ReadOpenTrcRecordBytes(TrcRecordReader&, void*, std::size_t) {
    return false;
}

void CloseTrcRecordReader(TrcRecordReader&) {
}

void ServeMilesSound() {
}

IndexedTextTableContext& StartupAuxiliaryIndexedTextTable(u32) {
    static IndexedTextTableContext table;
    return table;
}

std::string_view GetIndexedTextTableRow(
    const IndexedTextTableContext&, u32) {
    return {};
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "PRODUCTION_ORDER_RULE_WRAP_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

ProductionOrderCostRule rule(i32 base, u32 mode, i32 linear, i32 extra) {
    ProductionOrderCostRule result{};
    result.base = base;
    result.mode = mode;
    result.linear = linear;
    result.extra = extra;
    return result;
}

void test_owner_slot_production_state_swap() {
    ProductionOrderRuntimeState state{};
    constexpr u32 kFirstOwner = 1;
    constexpr u32 kSecondOwner = 6;

    for (u32 order = 0; order < kProductionOrderCount; ++order) {
        state.variant_counts[kFirstOwner][order] =
            static_cast<u8>(order + 1);
        state.lock_flags[kFirstOwner][order] =
            static_cast<u8>(0x40u + order);
        state.order_cell_opaque_bytes[kFirstOwner][order] = {
            static_cast<u8>(0x80u + order),
            static_cast<u8>(0xc0u + order)};

        state.variant_counts[kSecondOwner][order] =
            static_cast<u8>(0xf0u - order);
        state.lock_flags[kSecondOwner][order] =
            static_cast<u8>(0xb0u - order);
        state.order_cell_opaque_bytes[kSecondOwner][order] = {
            static_cast<u8>(0x70u - order),
            static_cast<u8>(0x30u - order)};
    }
    state.order_2b_bonus_totals[kFirstOwner] = 111;
    state.order_2b_bonus_totals[kSecondOwner] = -222;
    for (u32 effect = 0;
         effect < kProductionOrderCompletionEffectCount; ++effect) {
        for (u32 type = 0; type < kProductionOrderTypeCount; ++type) {
            state.completion_effect_totals[effect][kFirstOwner][type] =
                static_cast<i32>(effect * 1000u + type);
            state.completion_effect_totals[effect][kSecondOwner][type] =
                -static_cast<i32>(effect * 1000u + type + 1u);
        }
    }

    // HandleOwnerSlotTransferAndStateSwap (0x00427600) exchanges the complete
    // four-byte owner/order cell, the order-0x2b owner bonus, and all eighteen
    // 0xaa-entry effect tables.  Unit counts and resources are handled by the
    // surrounding ownership-transfer path and are deliberately not part of
    // this focused helper.
    require(SwapProductionOrderOwnerState(
                state, kFirstOwner, kSecondOwner),
        "valid owner-slot production state swap was rejected");
    for (u32 order = 0; order < kProductionOrderCount; ++order) {
        require(state.variant_counts[kFirstOwner][order] ==
                    static_cast<u8>(0xf0u - order) &&
                state.lock_flags[kFirstOwner][order] ==
                    static_cast<u8>(0xb0u - order) &&
                state.order_cell_opaque_bytes[kFirstOwner][order][0] ==
                    static_cast<u8>(0x70u - order) &&
                state.order_cell_opaque_bytes[kFirstOwner][order][1] ==
                    static_cast<u8>(0x30u - order) &&
                state.variant_counts[kSecondOwner][order] ==
                    static_cast<u8>(order + 1) &&
                state.lock_flags[kSecondOwner][order] ==
                    static_cast<u8>(0x40u + order) &&
                state.order_cell_opaque_bytes[kSecondOwner][order][0] ==
                    static_cast<u8>(0x80u + order) &&
                state.order_cell_opaque_bytes[kSecondOwner][order][1] ==
                    static_cast<u8>(0xc0u + order),
            "owner-slot swap did not preserve the original four-byte order cell");
    }
    require(state.order_2b_bonus_totals[kFirstOwner] == -222 &&
            state.order_2b_bonus_totals[kSecondOwner] == 111,
        "owner-slot swap omitted the order-0x2b bonus");
    for (u32 effect = 0;
         effect < kProductionOrderCompletionEffectCount; ++effect) {
        for (u32 type = 0; type < kProductionOrderTypeCount; ++type) {
            require(state.completion_effect_totals[effect]
                        [kFirstOwner][type] ==
                        -static_cast<i32>(effect * 1000u + type + 1u) &&
                    state.completion_effect_totals[effect]
                        [kSecondOwner][type] ==
                        static_cast<i32>(effect * 1000u + type),
                "owner-slot swap omitted an upgrade effect table entry");
        }
    }

    const ProductionOrderRuntimeState swapped = state;
    require(SwapProductionOrderOwnerState(state, kFirstOwner, kFirstOwner) &&
            state.variant_counts == swapped.variant_counts &&
            !SwapProductionOrderOwnerState(
                state, kProductionOrderOwnerCount, kSecondOwner),
        "owner-slot swap bounds/no-op behavior differs from the original domain");
}

} // namespace

int main() {
    using namespace ranker;

    test_owner_slot_production_state_swap();

    constexpr i32 kMinI32 = std::numeric_limits<i32>::min();
    constexpr i32 kMaxI32 = std::numeric_limits<i32>::max();

    // CalculateProductionOrderCostRule (0x0043b580) returns the raw EAX
    // DWORD.  Its cost and duration callers compare that DWORD unsigned.
    require(CalculateProductionOrderCost(rule(-1, 0, 0, 0), 0) ==
            0xffffffffu,
        "negative fixed cost was clamped instead of preserving EAX bits");

    ProductionOrderDefinition duration_definition{};
    duration_definition.duration_ticks = rule(-2, 0, 0, 0);
    require(CalculateProductionOrderDuration(duration_definition, 0) ==
            0xfffffffeu,
        "negative duration was clamped instead of preserving EAX bits");

    // IMUL and ADD at 0x0043b5ba..0x0043b5d4 keep their low 32 bits.
    require(CalculateProductionOrderRuleValue(
            rule(kMaxI32, 1, 2, 0), 1) == kMinI32 + 1,
        "mode-one multiply/add did not wrap to its low DWORD");
    require(CalculateProductionOrderRuleValue(
            rule(kMinI32, 2, kMinI32, -1), 2) == kMaxI32,
        "mode-two chained additions did not wrap to their low DWORD");

    // CDQ/IDIV at 0x0043b5e6 is signed and truncates toward zero.
    require(CalculateProductionOrderRuleValue(
            rule(7, 3, 9, 2), 0xffffffffu) == 7,
        "mode-three treated high-bit variant as unsigned");
    require(CalculateProductionOrderRuleValue(
            rule(7, 3, 9, 2), 0xfffffffeu) == -2,
        "mode-three signed division did not truncate toward zero");

    // Mode four uses signed JG at 0x0043b619 and wrapping IMUL/ADD in the
    // accumulator.  A negative variant skips the loop entirely.
    require(CalculateProductionOrderRuleValue(
            rule(kMaxI32, 4, kMaxI32, kMaxI32), 2) == -6,
        "mode-four triangular accumulation did not wrap per instruction");
    require(CalculateProductionOrderRuleValue(
            rule(5, 4, 3, 11), 0xffffffffu) == 2,
        "mode-four used an unsigned loop comparison");

    ProductionOrderRuntimeState runtime{};
    ProductionOrderDefinition cost_definition{};
    cost_definition.id = 7;
    cost_definition.max_variant_count = 1;
    cost_definition.primary_cost = rule(-1, 0, 0, 0);
    runtime.owner_primary_resources[0] = 100;
    runtime.owner_secondary_resources[0] = 100;
    const ProductionOrderCheckResult primary_shortage =
        CheckProductionOrderAvailability(runtime, cost_definition, 0);
    require(!primary_shortage.available && primary_shortage.code ==
            static_cast<u32>(
                ProductionOrderAvailabilityCode::missing_primary_resource) &&
            primary_shortage.primary_cost == 0xffffffffu,
        "availability treated a negative primary rule as a free order");

    cost_definition.primary_cost = {};
    cost_definition.secondary_cost = rule(-2, 0, 0, 0);
    const ProductionOrderCheckResult secondary_shortage =
        CheckProductionOrderAvailability(runtime, cost_definition, 0);
    require(!secondary_shortage.available && secondary_shortage.code ==
            static_cast<u32>(
                ProductionOrderAvailabilityCode::missing_secondary_resource) &&
            secondary_shortage.secondary_cost == 0xfffffffeu,
        "availability treated a negative secondary rule as a free order");

    duration_definition.id = 8;
    duration_definition.max_variant_count = 1;
    duration_definition.duration_ticks = rule(-1, 0, 0, 0);
    u32 progress = 0;
    const ProductionOrderCompletionResult progress_result =
        AdvanceProductionOrderProgress(
            runtime, duration_definition, 0, progress);
    require(!progress_result.completed && progress == 1,
        "negative duration completed immediately instead of comparing unsigned");

    // Exercise the complete subtype-0x0c upgrade lifecycle rather than only
    // its arithmetic helpers.  The receive path reserves the order with lock
    // bit 1 (value 2), while FUN_004ebed0 starts it with lock bit 0 (value 1)
    // and clears the reservation bit.  Completion increments the variant
    // before evaluating the effect rules.
    ProductionOrderRuntimeState lifecycle{};
    ProductionOrderDefinition upgrade{};
    upgrade.id = 7;
    upgrade.max_variant_count = 2;
    upgrade.duration_ticks = rule(3, 1, 2, 0);
    upgrade.primary_cost = rule(80, 1, 10, 0);
    upgrade.secondary_cost = rule(40, 1, 5, 0);
    upgrade.completion_effects[0] = rule(5, 1, 2, 0);
    upgrade.completion_effects[10] = rule(1, 1, 3, 0);
    upgrade.affected_type_ids = {32, 130, 0xffffffffu};
    lifecycle.owner_primary_resources[1] = 500;
    lifecycle.owner_secondary_resources[1] = 300;

    const ProductionOrderCheckResult lifecycle_available =
        CheckProductionOrderAvailability(lifecycle, upgrade, 1);
    require(lifecycle_available.available &&
            lifecycle_available.primary_cost == 80 &&
            lifecycle_available.secondary_cost == 40,
        "initial upgrade availability/costs did not use variant zero");
    require(DebitProductionOrderPrimaryCost(lifecycle, upgrade, 1) &&
            lifecycle.owner_primary_resources[1] == 420 &&
            lifecycle.owner_secondary_resources[1] == 300,
        "packet enqueue did not debit only the original primary-cost path");
    lifecycle.lock_flags[1][upgrade.id] |= 2u;

    ProductionOrderUnitRuntime upgrading_unit{};
    upgrading_unit.owner = 1;
    require(StartSelectedUnitProductionOrder(
                lifecycle, upgrade, upgrading_unit) &&
            upgrading_unit.active && upgrading_unit.current_order_id == upgrade.id &&
            upgrading_unit.progress_ticks == 0 &&
            lifecycle.lock_flags[1][upgrade.id] == 1,
        "selected structure did not convert reservation lock 2 to active lock 1");

    ProductionOrderCompletionResult lifecycle_tick{};
    lifecycle_tick = AdvanceProductionOrderProgress(
        lifecycle, upgrade, 1, upgrading_unit.progress_ticks);
    require(!lifecycle_tick.completed && upgrading_unit.progress_ticks == 1,
        "upgrade completed before its first duration threshold");
    lifecycle_tick = AdvanceProductionOrderProgress(
        lifecycle, upgrade, 1, upgrading_unit.progress_ticks);
    require(!lifecycle_tick.completed && upgrading_unit.progress_ticks == 2,
        "upgrade completed before its second duration threshold");
    lifecycle_tick = AdvanceProductionOrderProgress(
        lifecycle, upgrade, 1, upgrading_unit.progress_ticks);
    require(lifecycle_tick.completed && lifecycle_tick.variant == 1 &&
            upgrading_unit.progress_ticks == 3 &&
            lifecycle.variant_counts[1][upgrade.id] == 1 &&
            lifecycle.lock_flags[1][upgrade.id] == 0,
        "upgrade completion did not increment variant and release active lock");
    require(lifecycle_tick.effect_deltas[0] == 7 &&
            lifecycle_tick.effect_deltas[10] == 4 &&
            GetProductionOrderCompletionEffectTotal(
                lifecycle, 0, 1, 32) == 7 &&
            GetProductionOrderCompletionEffectTotal(
                lifecycle, 0, 1, 130) == 7 &&
            GetProductionOrderCompletionEffectTotal(
                lifecycle, 10, 1, 32) == 4 &&
            GetProductionOrderCompletionEffectTotal(
                lifecycle, 0, 1, 0xffffffffu) == 0,
        "completion effects were not evaluated with the incremented variant");
    require(CalculateWorkerHarvestAmountWithProductionEffect10(
                lifecycle, 1, 32) == 0x10,
        "completed harvest upgrade did not feed the worker collection amount");

    // The original cancellation helper restores the primary formula to both
    // owner resource arrays.  This asymmetry is intentional and must not be
    // normalized to the definition's secondary formula.
    require(DebitProductionOrderPrimaryCost(lifecycle, upgrade, 1) &&
            lifecycle.owner_primary_resources[1] == 330,
        "second-variant upgrade debit did not use the incremented variant");
    lifecycle.lock_flags[1][upgrade.id] = 2;
    ClearProductionOrderLockFlags(lifecycle, upgrade.id, 1);
    RefundProductionOrderCosts(lifecycle, upgrade, 1);
    require(lifecycle.lock_flags[1][upgrade.id] == 0 &&
            lifecycle.owner_primary_resources[1] == 420 &&
            lifecycle.owner_secondary_resources[1] == 390,
        "upgrade cancellation did not reproduce the original dual refund");

    ProductionOrderRuntimeState order_2b_runtime{};
    ProductionOrderDefinition order_2b{};
    order_2b.id = 0x2b;
    order_2b.max_variant_count = 1;
    order_2b.completion_effects[11] = rule(4, 1, 3, 0);
    order_2b.affected_type_ids = {32};
    const ProductionOrderCompletionResult order_2b_result =
        CompleteProductionOrder(order_2b_runtime, order_2b, 2);
    require(order_2b_result.completed && order_2b_result.variant == 1 &&
            order_2b_result.order_2b_bonus_delta == 7 &&
            order_2b_result.order_2b_refresh_requested &&
            GetProductionOrderOrder2bBonusTotal(order_2b_runtime, 2) == 7 &&
            order_2b_runtime.order_2b_refresh_requests == 1,
        "order 0x2b did not publish its active-unit refresh side effect");

    std::cout <<
        "PRODUCTION_ORDER_RULE_WRAP_PASS modes=0/1/2/3/4 "
        "cost=raw-u32 duration=raw-u32 availability=0/1 "
        "lifecycle=reserve/start/complete/cancel order2b=refresh "
        "owner-swap=64x4+18x170\n";
    return EXIT_SUCCESS;
}
