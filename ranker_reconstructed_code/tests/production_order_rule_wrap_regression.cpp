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

} // namespace

int main() {
    using namespace ranker;

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

    std::cout <<
        "PRODUCTION_ORDER_RULE_WRAP_PASS modes=0/1/2/3/4 "
        "cost=raw-u32 duration=raw-u32 availability=0/1\n";
    return EXIT_SUCCESS;
}
