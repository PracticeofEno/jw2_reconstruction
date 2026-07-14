#include "ranker_owner_ai.h"

#include "ranker_rng.h"
#include "ranker_unit_damage.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

namespace ranker {
namespace {

OwnerAiRuntimeState g_owner_ai_state;

// Original ranker.exe keeps the saved "AI" record and the live owner-AI
// runtime in the same 0x9d80-byte BSS span (DAT_0122ff28).  The original
// layout is structure-of-arrays, so it cannot be memcpy'd into the buildable
// C++ structure-of-structures representation.  Keep the raw image as the
// backing store for fields which are not reconstructed yet and explicitly
// mirror every evidenced typed field below.
namespace owner_ai_snapshot_layout {

constexpr u32 kOwnerDwordStride = sizeof(u32);
constexpr u32 kOwnerPointStride = sizeof(u32) * 2;

constexpr u32 kScriptHalted = 0x0000;
constexpr u32 kPrimaryInterval = 0x0020;
constexpr u32 kPrimaryRadius = 0x0040;
constexpr u32 kPrimaryBudget = 0x0060;

constexpr u32 kSharedCounterTable0 = 0x02e0;
constexpr u32 kSharedCounterTable1 = 0x03a0;
constexpr u32 kSharedCounterTable2 = 0x0460;

constexpr u32 kRouteLoadPercent = 0x05e0;
constexpr u32 kSupportInterval = 0x0600;
constexpr u32 kSupportRadius = 0x0620;
constexpr u32 kSupportTargetOwner = 0x0640;
constexpr u32 kSupportMode = 0x0660;
constexpr u32 kSupportBudget = 0x0680;
constexpr u32 kSupportAnchorPoint = 0x06a0;
constexpr u32 kSecondaryBudget = 0x06e0;
constexpr u32 kResourceBudgetPercent = 0x0700;
constexpr u32 kSecondaryMode = 0x0720;
constexpr u32 kProfileCounter = 0x0a00;

constexpr u32 kUnitDemand = 0x0b00;
constexpr u32 kUnitDemandShadow = 0x2040;
constexpr u32 kUnitDemandOwnerStride = kOwnerAiUnitTypeCount * sizeof(u32);

// DAT_012334a8 is the nearest/current hostile selected by FUN_0043c5c0 and
// consumed by the script's enemy predicates and strategic planners.
constexpr u32 kPrimaryTargetOwner = 0x3580;
constexpr u32 kScriptCycleCounter = 0x3600;
constexpr u32 kPreviousScriptCycleCounter = 0x3620;
constexpr u32 kScriptEnabled = 0x3640;
constexpr u32 kLastTimingFrame = 0x3660;
constexpr u32 kBuildBudget = 0x3680;
constexpr u32 kProductionBudget = 0x36a0;
constexpr u32 kRallyDelay = 0x36c0;
constexpr u32 kReserveBudget = 0x36e0;
constexpr u32 kReserveDelay = 0x3700;
constexpr u32 kStrategicRetargetQuotaFloor = 0x3720;
constexpr u32 kRouteTargetScore = 0x3740;
constexpr u32 kProfileGateFlag = 0x3760;
constexpr u32 kSharedPlannerTable = 0x37a0;

constexpr u32 kRouteRadius = 0x8fa0;
constexpr u32 kPrimaryTargetPoint = 0x8fc0;
constexpr u32 kPrimaryTargetRadius = 0x9000;
constexpr u32 kPrimaryTargetFlags = 0x9020;
constexpr u32 kNeutralRouteTargetPoint = 0x9040;
constexpr u32 kNeutralRouteTargetOwnerStride = 0x20;
constexpr u32 kPlacementRadius = 0x9140;
constexpr u32 kPlacementRecord = 0x9160;
constexpr u32 kPlacementRecordOwnerStride = 0x30;
constexpr u32 kThreatPoints = 0x92e0;
constexpr u32 kThreatPointOwnerStride = 0x20;
constexpr u32 kPlacementTargetRadius = 0x93e0;
constexpr u32 kAttackInterval = 0x9400;
constexpr u32 kAttackRadius = 0x9420;
constexpr u32 kAttackTargetRadius = 0x9440;
constexpr u32 kAttackTargetOwner = 0x9460;
constexpr u32 kSupportTargetSlot = 0x9480;
constexpr u32 kFallbackTargetSlot = 0x94a0;
// FUN_0044e200 clears/accumulates reserved production cost here and
// HandleOwnerProductionDemandAndBuildPlan gates on the same owner dword.
constexpr u32 kProductionPauseFlag = 0x94c0;
constexpr u32 kSharedGridTable = 0x94e0;
constexpr u32 kProfileStateFlag = 0x9ce0;
constexpr u32 kProfileAge = 0x9d00;
constexpr u32 kProfileRecordIndices = 0x9d20;

constexpr u32 owner_dword(u32 base, u32 owner) {
    return base + owner * kOwnerDwordStride;
}

constexpr u32 owner_point(u32 base, u32 owner) {
    return base + owner * kOwnerPointStride;
}

static_assert(kProfileRecordIndices +
        kOwnerAiOwnerCount * sizeof(i32) <= kOwnerAiSnapshotByteCount,
    "owner AI snapshot layout exceeds the original record");
static_assert(kSharedPlannerTable +
        kOwnerAiSharedPlannerDwordCount * sizeof(u32) == kRouteRadius,
    "owner AI planner span must end at the original route-radius table");
static_assert(kSharedGridTable +
        kOwnerAiSharedGridDwordCount * sizeof(u32) == kProfileStateFlag,
    "owner AI grid span must end at the original profile-state table");
static_assert(kProductionPauseFlag +
        kOwnerAiOwnerCount * sizeof(u32) == kSharedGridTable,
    "owner AI production reservation table must precede the grid");

} // namespace owner_ai_snapshot_layout

u32 read_owner_ai_snapshot_u32(
    const std::array<u8, kOwnerAiSnapshotByteCount>& bytes, u32 offset) {
    u32 value = 0;
    if (offset <= bytes.size() - sizeof(value)) {
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    }
    return value;
}

i32 read_owner_ai_snapshot_i32(
    const std::array<u8, kOwnerAiSnapshotByteCount>& bytes, u32 offset) {
    i32 value = 0;
    if (offset <= bytes.size() - sizeof(value)) {
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    }
    return value;
}

void write_owner_ai_snapshot_u32(
    std::array<u8, kOwnerAiSnapshotByteCount>& bytes, u32 offset, u32 value) {
    if (offset <= bytes.size() - sizeof(value)) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
}

void write_owner_ai_snapshot_i32(
    std::array<u8, kOwnerAiSnapshotByteCount>& bytes, u32 offset, i32 value) {
    if (offset <= bytes.size() - sizeof(value)) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
}

void hydrate_owner_ai_runtime_from_snapshot(OwnerAiRuntimeState& state) {
    using namespace owner_ai_snapshot_layout;

    const auto& bytes = state.snapshot_bytes;
    for (u32 owner_index = 0; owner_index < kOwnerAiOwnerCount; ++owner_index) {
        OwnerAiSlotRuntime& owner = state.owners[owner_index];
        owner.script_halted = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kScriptHalted, owner_index));
        owner.primary_interval = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryInterval, owner_index));
        owner.primary_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryRadius, owner_index));
        owner.primary_budget = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryBudget, owner_index));
        owner.primary_target_owner = read_owner_ai_snapshot_i32(
            bytes, owner_dword(kPrimaryTargetOwner, owner_index));
        owner.route_load_percent = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kRouteLoadPercent, owner_index));
        owner.support_interval = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kSupportInterval, owner_index));
        owner.support_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kSupportRadius, owner_index));
        owner.support_target_owner = read_owner_ai_snapshot_i32(
            bytes, owner_dword(kSupportTargetOwner, owner_index));
        owner.support_mode = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kSupportMode, owner_index));
        owner.support_budget = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kSupportBudget, owner_index));
        owner.support_anchor = read_owner_ai_snapshot_i32(
            bytes, owner_point(kSupportAnchorPoint, owner_index));
        owner.support_anchor_y = read_owner_ai_snapshot_i32(
            bytes, owner_point(kSupportAnchorPoint, owner_index) + sizeof(i32));
        owner.secondary_budget = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kSecondaryBudget, owner_index));
        owner.resource_budget_percent = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kResourceBudgetPercent, owner_index));
        owner.secondary_mode = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kSecondaryMode, owner_index));
        owner.profile_counter = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kProfileCounter, owner_index));

        const u32 demand_base =
            kUnitDemand + owner_index * kUnitDemandOwnerStride;
        const u32 shadow_base =
            kUnitDemandShadow + owner_index * kUnitDemandOwnerStride;
        for (u32 type = 0; type < kOwnerAiUnitTypeCount; ++type) {
            owner.unit_demand[type] = read_owner_ai_snapshot_u32(
                bytes, demand_base + type * sizeof(u32));
            owner.unit_demand_shadow[type] = read_owner_ai_snapshot_u32(
                bytes, shadow_base + type * sizeof(u32));
        }

        owner.script_cycle_counter = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kScriptCycleCounter, owner_index));
        owner.previous_script_cycle_counter = read_owner_ai_snapshot_i32(
            bytes, owner_dword(kPreviousScriptCycleCounter, owner_index));
        owner.script_enabled = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kScriptEnabled, owner_index));
        owner.last_timing_frame = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kLastTimingFrame, owner_index));
        owner.build_budget = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kBuildBudget, owner_index));
        owner.production_budget = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kProductionBudget, owner_index));
        owner.rally_delay = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kRallyDelay, owner_index));
        owner.reserve_budget = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kReserveBudget, owner_index));
        owner.reserve_delay = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kReserveDelay, owner_index));
        owner.strategic_retarget_quota_floor = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kStrategicRetargetQuotaFloor, owner_index));
        owner.route_target_score = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kRouteTargetScore, owner_index));
        owner.profile_gate_flag = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kProfileGateFlag, owner_index));
        owner.production_pause_flag = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kProductionPauseFlag, owner_index));
        owner.route_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kRouteRadius, owner_index));
        owner.primary_target_point.x = read_owner_ai_snapshot_i32(
            bytes, owner_point(kPrimaryTargetPoint, owner_index));
        owner.primary_target_point.y = read_owner_ai_snapshot_i32(
            bytes, owner_point(kPrimaryTargetPoint, owner_index) + sizeof(i32));
        owner.primary_target_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryTargetRadius, owner_index));
        owner.primary_target_flags = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryTargetFlags, owner_index));
        owner.neutral_route_target_point.x = read_owner_ai_snapshot_i32(
            bytes, kNeutralRouteTargetPoint +
                owner_index * kNeutralRouteTargetOwnerStride);
        owner.neutral_route_target_point.y = read_owner_ai_snapshot_i32(
            bytes, kNeutralRouteTargetPoint +
                owner_index * kNeutralRouteTargetOwnerStride + sizeof(i32));
        owner.placement_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kPlacementRadius, owner_index));

        const u32 placement_base =
            kPlacementRecord + owner_index * kPlacementRecordOwnerStride;
        for (u32 index = 0; index < owner.placement_record.size(); ++index) {
            owner.placement_record[index] = read_owner_ai_snapshot_i32(
                bytes, placement_base + index * sizeof(i32));
        }

        const u32 threat_base =
            kThreatPoints + owner_index * kThreatPointOwnerStride;
        for (u32 index = 0; index < owner.threat_points.size(); ++index) {
            owner.threat_points[index].x = read_owner_ai_snapshot_i32(
                bytes, threat_base + index * kOwnerPointStride);
            owner.threat_points[index].y = read_owner_ai_snapshot_i32(
                bytes, threat_base + index * kOwnerPointStride + sizeof(i32));
        }

        owner.placement_target_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kPlacementTargetRadius, owner_index));
        owner.attack_interval = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kAttackInterval, owner_index));
        owner.attack_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kAttackRadius, owner_index));
        owner.attack_target_radius = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kAttackTargetRadius, owner_index));
        owner.attack_target_owner = read_owner_ai_snapshot_i32(
            bytes, owner_dword(kAttackTargetOwner, owner_index));
        owner.support_target_slot = read_owner_ai_snapshot_i32(
            bytes, owner_dword(kSupportTargetSlot, owner_index));
        owner.fallback_target_slot = read_owner_ai_snapshot_i32(
            bytes, owner_dword(kFallbackTargetSlot, owner_index));
        owner.profile_state_flag = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kProfileStateFlag, owner_index));
        owner.profile_age = read_owner_ai_snapshot_u32(
            bytes, owner_dword(kProfileAge, owner_index));
        state.profile_record_indices[owner_index] = read_owner_ai_snapshot_i32(
            bytes, owner_dword(kProfileRecordIndices, owner_index));
    }

    for (u32 index = 0; index < kOwnerAiSharedCounterDwordCount; ++index) {
        const u32 byte_index = index * sizeof(u32);
        state.shared_counter_table0[index] = read_owner_ai_snapshot_u32(
            bytes, kSharedCounterTable0 + byte_index);
        state.shared_counter_table1[index] = read_owner_ai_snapshot_u32(
            bytes, kSharedCounterTable1 + byte_index);
        state.shared_counter_table2[index] = read_owner_ai_snapshot_u32(
            bytes, kSharedCounterTable2 + byte_index);
    }
    for (u32 index = 0; index < kOwnerAiSharedPlannerDwordCount; ++index) {
        state.shared_planner_table[index] = read_owner_ai_snapshot_u32(
            bytes, kSharedPlannerTable + index * sizeof(u32));
    }
    for (u32 index = 0; index < kOwnerAiSharedGridDwordCount; ++index) {
        state.shared_grid_table[index] = read_owner_ai_snapshot_u32(
            bytes, kSharedGridTable + index * sizeof(u32));
    }
}

void overlay_owner_ai_runtime_on_snapshot(const OwnerAiRuntimeState& state,
    std::array<u8, kOwnerAiSnapshotByteCount>& bytes) {
    using namespace owner_ai_snapshot_layout;

    for (u32 owner_index = 0; owner_index < kOwnerAiOwnerCount; ++owner_index) {
        const OwnerAiSlotRuntime& owner = state.owners[owner_index];
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kScriptHalted, owner_index), owner.script_halted);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryInterval, owner_index), owner.primary_interval);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryRadius, owner_index), owner.primary_radius);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kPrimaryBudget, owner_index), owner.primary_budget);
        write_owner_ai_snapshot_i32(bytes,
            owner_dword(kPrimaryTargetOwner, owner_index), owner.primary_target_owner);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kRouteLoadPercent, owner_index), owner.route_load_percent);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kSupportInterval, owner_index), owner.support_interval);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kSupportRadius, owner_index), owner.support_radius);
        write_owner_ai_snapshot_i32(bytes,
            owner_dword(kSupportTargetOwner, owner_index), owner.support_target_owner);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kSupportMode, owner_index), owner.support_mode);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kSupportBudget, owner_index), owner.support_budget);
        write_owner_ai_snapshot_i32(bytes,
            owner_point(kSupportAnchorPoint, owner_index), owner.support_anchor);
        write_owner_ai_snapshot_i32(bytes,
            owner_point(kSupportAnchorPoint, owner_index) + sizeof(i32),
            owner.support_anchor_y);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kSecondaryBudget, owner_index), owner.secondary_budget);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kResourceBudgetPercent, owner_index),
            owner.resource_budget_percent);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kSecondaryMode, owner_index), owner.secondary_mode);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kProfileCounter, owner_index), owner.profile_counter);

        const u32 demand_base =
            kUnitDemand + owner_index * kUnitDemandOwnerStride;
        const u32 shadow_base =
            kUnitDemandShadow + owner_index * kUnitDemandOwnerStride;
        for (u32 type = 0; type < kOwnerAiUnitTypeCount; ++type) {
            write_owner_ai_snapshot_u32(bytes,
                demand_base + type * sizeof(u32), owner.unit_demand[type]);
            write_owner_ai_snapshot_u32(bytes,
                shadow_base + type * sizeof(u32), owner.unit_demand_shadow[type]);
        }

        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kScriptCycleCounter, owner_index),
            owner.script_cycle_counter);
        write_owner_ai_snapshot_i32(bytes,
            owner_dword(kPreviousScriptCycleCounter, owner_index),
            owner.previous_script_cycle_counter);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kScriptEnabled, owner_index), owner.script_enabled);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kLastTimingFrame, owner_index), owner.last_timing_frame);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kBuildBudget, owner_index), owner.build_budget);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kProductionBudget, owner_index), owner.production_budget);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kRallyDelay, owner_index), owner.rally_delay);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kReserveBudget, owner_index), owner.reserve_budget);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kReserveDelay, owner_index), owner.reserve_delay);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kStrategicRetargetQuotaFloor, owner_index),
            owner.strategic_retarget_quota_floor);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kRouteTargetScore, owner_index), owner.route_target_score);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kProfileGateFlag, owner_index), owner.profile_gate_flag);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kProductionPauseFlag, owner_index),
            owner.production_pause_flag);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kRouteRadius, owner_index), owner.route_radius);
        write_owner_ai_snapshot_i32(bytes,
            owner_point(kPrimaryTargetPoint, owner_index),
            owner.primary_target_point.x);
        write_owner_ai_snapshot_i32(bytes,
            owner_point(kPrimaryTargetPoint, owner_index) + sizeof(i32),
            owner.primary_target_point.y);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kPrimaryTargetRadius, owner_index),
            owner.primary_target_radius);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kPrimaryTargetFlags, owner_index),
            owner.primary_target_flags);
        write_owner_ai_snapshot_i32(bytes,
            kNeutralRouteTargetPoint +
                owner_index * kNeutralRouteTargetOwnerStride,
            owner.neutral_route_target_point.x);
        write_owner_ai_snapshot_i32(bytes,
            kNeutralRouteTargetPoint +
                owner_index * kNeutralRouteTargetOwnerStride + sizeof(i32),
            owner.neutral_route_target_point.y);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kPlacementRadius, owner_index), owner.placement_radius);

        const u32 placement_base =
            kPlacementRecord + owner_index * kPlacementRecordOwnerStride;
        for (u32 index = 0; index < owner.placement_record.size(); ++index) {
            write_owner_ai_snapshot_i32(bytes,
                placement_base + index * sizeof(i32),
                owner.placement_record[index]);
        }

        const u32 threat_base =
            kThreatPoints + owner_index * kThreatPointOwnerStride;
        for (u32 index = 0; index < owner.threat_points.size(); ++index) {
            write_owner_ai_snapshot_i32(bytes,
                threat_base + index * kOwnerPointStride,
                owner.threat_points[index].x);
            write_owner_ai_snapshot_i32(bytes,
                threat_base + index * kOwnerPointStride + sizeof(i32),
                owner.threat_points[index].y);
        }

        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kPlacementTargetRadius, owner_index),
            owner.placement_target_radius);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kAttackInterval, owner_index), owner.attack_interval);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kAttackRadius, owner_index), owner.attack_radius);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kAttackTargetRadius, owner_index),
            owner.attack_target_radius);
        write_owner_ai_snapshot_i32(bytes,
            owner_dword(kAttackTargetOwner, owner_index),
            owner.attack_target_owner);
        write_owner_ai_snapshot_i32(bytes,
            owner_dword(kSupportTargetSlot, owner_index),
            owner.support_target_slot);
        write_owner_ai_snapshot_i32(bytes,
            owner_dword(kFallbackTargetSlot, owner_index),
            owner.fallback_target_slot);
        write_owner_ai_snapshot_u32(bytes,
            owner_dword(kProfileStateFlag, owner_index),
            owner.profile_state_flag);
        write_owner_ai_snapshot_u32(
            bytes, owner_dword(kProfileAge, owner_index), owner.profile_age);
        write_owner_ai_snapshot_i32(bytes,
            owner_dword(kProfileRecordIndices, owner_index),
            state.profile_record_indices[owner_index]);
    }

    for (u32 index = 0; index < kOwnerAiSharedCounterDwordCount; ++index) {
        const u32 byte_index = index * sizeof(u32);
        write_owner_ai_snapshot_u32(bytes,
            kSharedCounterTable0 + byte_index, state.shared_counter_table0[index]);
        write_owner_ai_snapshot_u32(bytes,
            kSharedCounterTable1 + byte_index, state.shared_counter_table1[index]);
        write_owner_ai_snapshot_u32(bytes,
            kSharedCounterTable2 + byte_index, state.shared_counter_table2[index]);
    }
    for (u32 index = 0; index < kOwnerAiSharedPlannerDwordCount; ++index) {
        write_owner_ai_snapshot_u32(bytes,
            kSharedPlannerTable + index * sizeof(u32),
            state.shared_planner_table[index]);
    }
    for (u32 index = 0; index < kOwnerAiSharedGridDwordCount; ++index) {
        write_owner_ai_snapshot_u32(bytes,
            kSharedGridTable + index * sizeof(u32),
            state.shared_grid_table[index]);
    }
}

struct OwnerAiCommandDefinition {
    const char* token;
    u32 argument_class;
};

constexpr std::array<OwnerAiCommandDefinition, 0x66> kOwnerAiCommandDefinitions{{
    {"AICMD_NULL", 0},
    {"PRIORITY_GATHER", 2},
    {"SET_NO_BUILDER_COUNTER", 1},
    {"ADD_NO_BUILDER_COUNTER", 2},
    {"SUB_NO_BUILDER_COUNTER", 2},
    {"SET_NO_BUILDER_BERY", 1},
    {"ADD_NO_BUILDER_BERY", 2},
    {"SUB_NO_BUILDER_BERY", 2},
    {"SET_NEW_BASE_BERY", 1},
    {"ADD_NEW_BASE_BERY", 2},
    {"SUB_NEW_BASE_BERY", 2},
    {"PRIORITY_UNITMAKE", 2},
    {"SET_NO_SUPPLY", 1},
    {"ADD_NO_SUPPLY", 2},
    {"SUB_NO_SUPPLY", 2},
    {"SET_NO_SUPPLY_BY_UNIT", 1},
    {"ADD_NO_SUPPLY_BY_UNIT", 2},
    {"SUB_NO_SUPPLY_BY_UNIT", 2},
    {"SET_NO_BASE_UNIT", 1},
    {"ADD_NO_BASE_UNIT", 2},
    {"SUB_NO_BASE_UNIT", 2},
    {"SET_NO_MIDDLE_UNIT", 1},
    {"ADD_NO_MIDDLE_UNIT", 2},
    {"SUB_NO_MIDDLE_UNIT", 2},
    {"SET_NO_HIGH_UNIT", 1},
    {"ADD_NO_HIGH_UNIT", 2},
    {"SUB_NO_HIGH_UNIT", 2},
    {"SET_UNIT_MAKE", 2},
    {"ADD_UNIT_MAKE", 3},
    {"SUB_UNIT_MAKE", 3},
    {"SET_UNIT_BY_ENEMY", 1},
    {"ADD_UNIT_BY_ENEMY", 2},
    {"SUB_UNIT_BY_ENEMY", 2},
    {"SET_COUNTER_UNIT", 7},
    {"SET_ATTACK_INTERVAL", 1},
    {"ADD_ATTACK_INTERVAL", 2},
    {"SUB_ATTACK_INTERVAL", 2},
    {"SET_ATTACK_COUNTER", 1},
    {"ADD_ATTACK_COUNTER", 2},
    {"SUB_ATTACK_COUNTER", 2},
    {"SET_ATTACK_WINABLE", 1},
    {"ADD_ATTACK_WINABLE", 2},
    {"SUB_ATTACK_WINABLE", 2},
    {"SET_ATTACK_BY_ENEMY", 1},
    {"ADD_ATTACK_BY_ENEMY", 2},
    {"SUB_ATTACK_BY_ENEMY", 2},
    {"SET_ATTACK_BACK", 1},
    {"ADD_ATTACK_BACK", 2},
    {"SUB_ATTACK_BACK", 2},
    {"SET_ATTACK_UNIT", 1},
    {"ADD_ATTACK_UNIT", 2},
    {"SUB_ATTACK_UNIT", 2},
    {"SET_ATTACK_UNIT_PS", 1},
    {"ADD_ATTACK_UNIT_PS", 2},
    {"SUB_ATTACK_UNIT_PS", 2},
    {"SET_ATTACK_MOVE", 1},
    {"ADD_ATTACK_MOVE", 2},
    {"SUB_ATTACK_MOVE", 2},
    {"SET_DEFENSE_POS", 1},
    {"ADD_DEFENSE_POS", 2},
    {"SUB_DEFENSE_POS", 2},
    {"SET_DEFENSE_TOWER", 1},
    {"ADD_DEFENSE_TOWER", 2},
    {"SUB_DEFENSE_TOWER", 2},
    {"PRIORITY_UPGRADE", 2},
    {"SET_UPGRADE", 1},
    {"ADD_UPGRADE", 2},
    {"SUB_UPGRADE", 2},
    {"SET_UPGRADE_BERY", 1},
    {"SET_UPGRADE_EXP", 1},
    {"SET_UPGRADE_DETECT", 1},
    {"SET_HUNT", 1},
    {"ADD_HUNT", 2},
    {"SUB_HUNT", 2},
    {"GO", 4},
    {"GO_RANDOM", 6},
    {"GO_IF_ISLAND", 4},
    {"GO_IF_NO_ISLAND", 4},
    {"SET_RUNCNT", 1},
    {"SET_ATTCNT", 1},
    {"GO_IF_OVER_RUNCNT", 5},
    {"GO_IF_UNDER_RUNCNT", 5},
    {"GO_IF_OVER_ATTCNT", 5},
    {"GO_IF_UNDER_ATTCNT", 5},
    {"GO_IF_OVER_RELCNT", 5},
    {"GO_IF_UNDER_RELCNT", 5},
    {"GO_IF_OVER_MY_UNIT", 6},
    {"GO_IF_UNDER_MY_UNIT", 6},
    {"GO_IF_OVER_MY_ATTACK_UNIT", 5},
    {"GO_IF_UNDER_MY_ATTACK_UNIT", 5},
    {"GO_IF_OVER_ENEMY_UNIT", 6},
    {"GO_IF_UNDER_ENEMY_UNIT", 6},
    {"GO_IF_OVER_ENEMY_ATTACK_UNIT", 5},
    {"GO_IF_UNDER_ENEMY_ATTACK_UNIT", 5},
    {"GO_IF_MY_TRIBE", 5},
    {"GO_IF_ENEMY_TRIBE", 5},
    {"LOAD_SCENARIO", 1},
    {"UNUSED_SCRIPT", 0},
    {"UNUSE_SCRIPT", 0},
    {"ON_EXCEPT_TRIGGER_TEAM", 0},
    {"OFF_EXCEPT_TRIGGER_TEAM", 0},
    {"END", 0},
}};

i32 signed_i32_from_wrapped_u32(u32 value) {
    i32 signed_value = 0;
    std::memcpy(&signed_value, &value, sizeof(signed_value));
    return signed_value;
}

constexpr std::array<u32, 4> kOwnerAiPostInitRequirementUnitTypes{{
    0x00, 0x10, 0x20, 0x30,
}};

constexpr std::array<std::array<i32, 4>, 4> kOwnerAiBaseUnitDemandGroups{{
    {{0x01, 0x03, -1, 0}},
    {{0x11, 0x13, -1, 0}},
    {{0x21, 0x22, 0x24, -1}},
    {{0x31, 0x32, 0x36, -1}},
}};

constexpr std::array<std::array<i32, 7>, 4> kOwnerAiMiddleUnitDemandGroups{{
    {{0x02, 0x04, 0x07, 0x06, 0x0f, -1, 0}},
    {{0x12, 0x14, 0x15, 0x17, 0x1c, 0x1d, -1}},
    {{0x23, 0x25, 0x28, 0x27, 0x2a, 0x2e, -1}},
    {{0x39, 0x37, 0x38, 0x3e, 0x3f, -1, 0}},
}};

constexpr std::array<std::array<i32, 4>, 4> kOwnerAiHighUnitDemandGroups{{
    {{0x08, 0x09, -1, 0}},
    {{0x19, 0x1a, 0x1b, -1}},
    {{0x2d, 0x2b, 0x2c, -1}},
    {{0x35, 0x3a, -1, 0}},
}};

bool owner_slot_valid(u32 owner_slot) {
    return owner_slot < kOwnerAiOwnerCount;
}

char upper_ascii(char value) {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - ('a' - 'A'));
    }
    return value;
}

bool owner_ai_token_delimiter(char value) {
    return value == ' ' || value == ',' || value == '\t';
}

bool owner_ai_line_delimiter(char value) {
    return value == '\r' || value == '\n';
}

std::vector<std::string> tokenize_owner_ai_line(const char* line) {
    std::vector<std::string> tokens;
    if (line == nullptr) {
        return tokens;
    }

    const char* cursor = line;
    while (*cursor != '\0') {
        while (*cursor != '\0' && owner_ai_token_delimiter(*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        const char* begin = cursor;
        while (*cursor != '\0' && !owner_ai_token_delimiter(*cursor)) {
            ++cursor;
        }
        tokens.emplace_back(begin, cursor);
    }
    return tokens;
}

std::vector<std::string> split_owner_ai_profile_lines(const char* text) {
    std::vector<std::string> lines;
    if (text == nullptr) {
        return lines;
    }

    const char* cursor = text;
    while (*cursor != '\0') {
        while (*cursor != '\0' && owner_ai_line_delimiter(*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        const char* begin = cursor;
        while (*cursor != '\0' && !owner_ai_line_delimiter(*cursor)) {
            ++cursor;
        }
        lines.emplace_back(begin, cursor);
    }
    return lines;
}

void uppercase_in_place(std::string& value) {
    for (char& ch : value) {
        ch = upper_ascii(ch);
    }
}

i32 parse_owner_ai_int(const std::string& token) {
    return static_cast<i32>(std::strtol(token.c_str(), nullptr, 10));
}

const OwnerAiCommandDefinition* find_owner_ai_command_definition(
    const std::string& token, u32& command_id) {
    for (u32 index = 0; index < kOwnerAiCommandDefinitions.size(); ++index) {
        if (token == kOwnerAiCommandDefinitions[index].token) {
            command_id = index;
            return &kOwnerAiCommandDefinitions[index];
        }
    }
    return nullptr;
}

bool owner_is_player_controlled(const PlayerSlotRuntimeState& player_slots, u32 owner_slot) {
    return owner_slot_valid(owner_slot) &&
        player_slots.slot_states[owner_slot] ==
            static_cast<u8>(PlayerSlotState::player_controlled);
}

OwnerAiPoint owner_start_point(const PlayerSlotRuntimeState& player_slots, u32 owner_slot) {
    OwnerAiPoint point{};
    if (owner_slot_valid(owner_slot)) {
        point.x = player_slots.owner_start_x[owner_slot];
        point.y = player_slots.owner_start_y[owner_slot];
    }
    return point;
}

bool use_generic_ai_profile_table(const OwnerAiRuntimeState& state) {
    return state.skirmish_profile_mode ||
        state.network_profile_override ||
        state.scenario_profile_override;
}

u32 stage_specific_profile_record(const OwnerAiRuntimeState& state, u32 owner_slot) {
    return owner_slot + state.session_mode * 8 + state.selected_faction * 0x40;
}

void clear_shared_reset_tables(OwnerAiRuntimeState& state) {
    state.shared_counter_table0.fill(0);
    state.shared_counter_table1.fill(0);
    state.shared_counter_table2.fill(0);
    state.shared_planner_table.fill(0);
    state.shared_grid_table.fill(0);
}

void reset_owner_threat_points(OwnerAiSlotRuntime& owner) {
    for (OwnerAiPoint& point : owner.threat_points) {
        point.x = -1;
    }
}

void copy_upper_label_name(std::array<char, kOwnerAiProfileLabelNameLength>& destination,
    const char* source) {
    destination.fill(0);
    if (source == nullptr) {
        return;
    }

    for (u32 index = 0; index + 1 < destination.size() && source[index] != '\0';
         ++index) {
        destination[index] = upper_ascii(source[index]);
    }
}

OwnerAiCommandRecord make_command_record4(i32 command_id, i32 arg0, i32 arg1,
    i32 arg2) {
    OwnerAiCommandRecord record{};
    record.words[0] = command_id;
    record.words[1] = arg0;
    record.words[2] = arg1;
    record.words[3] = arg2;
    record.words[9] = 0;
    record.words[10] = 0;
    return record;
}

OwnerAiCommandRecord make_command_record9(i32 command_id, i32 arg5, i32 arg6,
    i32 arg7) {
    OwnerAiCommandRecord record{};
    record.words[0] = command_id;
    record.words[1] = arg6;
    record.words[2] = arg7;
    record.words[3] = arg5;
    record.words[9] = 0;
    record.words[10] = 0;
    return record;
}

void append_command_record(OwnerAiRuntimeState& state,
    const OwnerAiCommandRecord& record) {
    if (state.parser_populate_records && owner_slot_valid(state.parser_owner_slot)) {
        state.profile_commands[state.parser_owner_slot].push_back(record);
    }
    ++state.parser_command_count;
}

u32 field_bits(i32 value) {
    return static_cast<u32>(value);
}

void set_field(u32& field, i32 value) {
    field = static_cast<u32>(value);
}

void set_field(i32& field, i32 value) {
    field = value;
}

void set_field_bits(i32& field, u32 value) {
    field = static_cast<i32>(value);
}

u32 add_clamped_max(u32 value, i32 amount, i32 maximum) {
    u32 result = value + static_cast<u32>(amount);
    const u32 max_value = static_cast<u32>(maximum);
    if (max_value < result) {
        result = max_value;
    }
    return result;
}

u32 sub_clamped_min(u32 value, i32 amount, i32 minimum) {
    u32 result = value - static_cast<u32>(amount);
    const u32 min_value = static_cast<u32>(minimum);
    if (result < min_value) {
        result = min_value;
    }
    return result;
}

u32 sub_clamped_max(u32 value, i32 amount, i32 maximum) {
    u32 result = value - static_cast<u32>(amount);
    const u32 max_value = static_cast<u32>(maximum);
    if (max_value < result) {
        result = max_value;
    }
    return result;
}

template <typename Field>
void add_field_clamped_max(Field& field, i32 amount, i32 maximum) {
    if constexpr (std::is_same_v<Field, i32>) {
        set_field_bits(field, add_clamped_max(field_bits(field), amount, maximum));
    } else {
        field = add_clamped_max(field, amount, maximum);
    }
}

template <typename Field>
void sub_field_clamped_min(Field& field, i32 amount, i32 minimum) {
    if constexpr (std::is_same_v<Field, i32>) {
        set_field_bits(field, sub_clamped_min(field_bits(field), amount, minimum));
    } else {
        field = sub_clamped_min(field, amount, minimum);
    }
}

template <typename Field>
void sub_field_clamped_max(Field& field, i32 amount, i32 maximum) {
    if constexpr (std::is_same_v<Field, i32>) {
        set_field_bits(field, sub_clamped_max(field_bits(field), amount, maximum));
    } else {
        field = sub_clamped_max(field, amount, maximum);
    }
}

bool owner_script_cycle_changed(const OwnerAiSlotRuntime& owner) {
    return owner.previous_script_cycle_counter !=
        static_cast<i32>(owner.script_cycle_counter);
}

bool owner_ai_faction_valid(u32 faction_id) {
    return faction_id < kOwnerAiPostInitRequirementUnitTypes.size();
}

bool owner_ai_unit_type_valid(i32 unit_type) {
    return unit_type >= 0 &&
        static_cast<u32>(unit_type) < kOwnerAiUnitTypeCount;
}

void set_owner_ai_unit_demand_max(OwnerAiSlotRuntime& owner, i32 unit_type, i32 value) {
    if (!owner_ai_unit_type_valid(unit_type)) {
        return;
    }

    u32& demand = owner.unit_demand[static_cast<u32>(unit_type)];
    if (demand < static_cast<u32>(value)) {
        demand = static_cast<u32>(value);
    }
}

void set_owner_ai_unit_demand(OwnerAiSlotRuntime& owner, i32 unit_type, i32 value) {
    if (owner_ai_unit_type_valid(unit_type)) {
        owner.unit_demand[static_cast<u32>(unit_type)] = static_cast<u32>(value);
    }
}

void add_owner_ai_unit_demand(OwnerAiSlotRuntime& owner, i32 unit_type, i32 amount,
    i32 maximum) {
    if (!owner_ai_unit_type_valid(unit_type)) {
        return;
    }

    u32& demand = owner.unit_demand[static_cast<u32>(unit_type)];
    demand = add_clamped_max(demand, amount, maximum);
}

void sub_owner_ai_unit_demand(OwnerAiSlotRuntime& owner, i32 unit_type, i32 amount,
    i32 minimum) {
    if (!owner_ai_unit_type_valid(unit_type)) {
        return;
    }

    u32& demand = owner.unit_demand[static_cast<u32>(unit_type)];
    demand = sub_clamped_min(demand, amount, minimum);
}

template <std::size_t GroupSize, typename Operation>
void apply_owner_ai_unit_group(const std::array<i32, GroupSize>& group,
    Operation operation) {
    for (i32 unit_type : group) {
        if (unit_type == -1) {
            break;
        }
        operation(unit_type);
    }
}

void jump_owner_ai_command(OwnerAiCommandRecord& command, u32& command_index) {
    ++command.words[10];
    command_index = static_cast<u32>(command.words[1]);
}

void finish_owner_ai_command_script(const OwnerAiRuntimeState& state, u32 owner_slot,
    OwnerAiCommandRecord& command, u32& command_index) {
    ++command.words[10];
    command_index = owner_slot_valid(owner_slot)
        ? static_cast<u32>(state.profile_commands[owner_slot].size())
        : 0;
}

bool owner_ai_condition_jump(bool condition, OwnerAiCommandRecord& command,
    u32& command_index) {
    if (!condition) {
        return false;
    }

    jump_owner_ai_command(command, command_index);
    return true;
}

u32 owner_ai_nearest_hostile_slot(const OwnerAiRuntimeState& state, u32 owner_slot) {
    if (state.command_player_slots == nullptr || !owner_slot_valid(owner_slot)) {
        return owner_slot;
    }
    return state.command_player_slots->nearest_hostile_slots[owner_slot];
}

u32 owner_ai_current_target_owner(const OwnerAiRuntimeState& state,
    u32 owner_slot) {
    if (!owner_slot_valid(owner_slot)) {
        return kOwnerAiOwnerCount;
    }

    const i32 target_owner = state.owners[owner_slot].primary_target_owner;
    return target_owner >= 0 && owner_slot_valid(static_cast<u32>(target_owner))
        ? static_cast<u32>(target_owner)
        : kOwnerAiOwnerCount;
}

u32 owner_ai_unit_count(const OwnerAiRuntimeState& state, u32 owner_slot,
    i32 unit_type) {
    if (!owner_slot_valid(owner_slot) || !owner_ai_unit_type_valid(unit_type)) {
        return 0;
    }
    return state.owner_unit_type_counts[owner_slot][static_cast<u32>(unit_type)];
}

bool owner_ai_eligible_summary(const OwnerAiRuntimeState& state, u32 owner_slot,
    OwnerAiEligibleUnitSummary& summary) {
    summary = OwnerAiEligibleUnitSummary{};
    return state.eligible_unit_summary != nullptr &&
        state.eligible_unit_summary(state, owner_slot, summary,
            state.eligible_unit_summary_user_data);
}

OwnerAiEligibleUnitSummary owner_ai_eligible_summary_or_unit_counts(
    const OwnerAiRuntimeState& state, u32 owner_slot) {
    OwnerAiEligibleUnitSummary summary;
    if (owner_ai_eligible_summary(state, owner_slot, summary)) {
        return summary;
    }
    if (!owner_slot_valid(owner_slot)) {
        return summary;
    }

    for (u32 type = 0; type < kOwnerAiCounterRuleUnitTypeCount; ++type) {
        const u32 count = state.owner_unit_type_counts[owner_slot][type];
        summary.count += count;
        summary.weight += static_cast<i32>(count);
    }
    return summary;
}

bool owner_ai_population_gap_open(const OwnerAiStrategicRetargetGateInput& input,
    u32 owner_slot) {
    if (!owner_slot_valid(owner_slot)) {
        return false;
    }
    // 0x0044095f subtracts in a 32-bit register and follows CMP with signed
    // JL.  Preserve both the wrap and the signed interpretation, including
    // the hard-limit 0..5 and high-bit boundaries.
    const u32 wrapped_threshold = input.owner_population_limit[owner_slot] -
        kOwnerAiPopulationRetargetReserve;
    return signed_i32_from_wrapped_u32(
               input.owner_population_used[owner_slot]) <
        signed_i32_from_wrapped_u32(wrapped_threshold);
}

bool owner_ai_default_pressure_unit_eligible(const UnitMovementUnit& unit) {
    return unit.active && unit.type_id < kOwnerAiCounterRuleUnitTypeCount &&
        (unit.command_state & kUnitCommandDead) == 0 &&
        (unit.runtime_flags & 0x80u) == 0;
}

bool owner_ai_pressure_unit_eligible(const UnitMovementUnit& unit,
    const OwnerAiStrategicRetargetGateInput& input) {
    if (input.pressure_unit_eligible != nullptr) {
        return input.pressure_unit_eligible(unit, input.user_data);
    }
    return owner_ai_default_pressure_unit_eligible(unit);
}

u32 owner_ai_unit_weight(const UnitMovementUnit& unit,
    const OwnerAiStrategicRetargetGateInput& input) {
    if (input.unit_weight != nullptr) {
        return input.unit_weight(unit, input.user_data);
    }
    return 1;
}

bool owner_ai_counter_rule_gate_satisfied(const OwnerAiRuntimeState& state,
    u32 owner_slot, const OwnerAiStrategicRetargetGateInput& input) {
    if (input.counter_rules == nullptr || !owner_slot_valid(owner_slot)) {
        return false;
    }

    const OwnerAiSlotRuntime& owner = state.owners[owner_slot];
    const u32 faction = state.owner_faction_ids[owner_slot];
    if (faction >= input.counter_rules->size()) {
        return false;
    }
    const u32 target_owner = owner_ai_current_target_owner(state, owner_slot);
    if (!owner_slot_valid(target_owner)) {
        return false;
    }

    u32 desired_count = 0;
    u32 owned_counter_count = 0;
    for (u32 target_type = 0; target_type < kOwnerAiCounterRuleUnitTypeCount;
         ++target_type) {
        const u32 target_count =
            state.owner_unit_type_counts[target_owner][target_type];
        if (target_count == 0) {
            continue;
        }

        const auto& rules = (*input.counter_rules)[faction][target_type];
        for (const OwnerAiCounterUnitRule& rule : rules) {
            if (rule.unit_type < 0 ||
                static_cast<u32>(rule.unit_type) >= kOwnerAiUnitTypeCount) {
                continue;
            }
            desired_count += static_cast<u32>(
                (static_cast<u64>(target_count) *
                    (rule.percent_bonus + owner.profile_counter)) /
                100u);
            owned_counter_count +=
                state.owner_unit_type_counts[owner_slot]
                    [static_cast<u32>(rule.unit_type)];
        }
    }

    return desired_count <= owned_counter_count && owned_counter_count > 3;
}

const ProductionOrderDefinition* owner_ai_find_production_order_definition(
    const ProductionOrderCatalog& catalog, u32 order_id) {
    for (const ProductionOrderDefinition& definition : catalog.definitions) {
        if (definition.id == order_id) {
            return &definition;
        }
    }
    return nullptr;
}

u32 owner_ai_primary_production_unit_type(
    const ProductionOrderDefinition& definition) {
    if (definition.affected_type_ids.empty()) {
        return 0xffffffffu;
    }
    return definition.affected_type_ids.front();
}

bool owner_ai_default_production_producer_ready(const UnitMovementUnit& unit) {
    return unit.active && (unit.command_state & kUnitCommandDead) == 0 &&
        (unit.runtime_flags & 0x80u) == 0;
}

bool owner_ai_production_producer_ready(const UnitMovementUnit& unit,
    u32 order_id, const OwnerAiProductionOrderPlanningInput& input) {
    if (input.producer_ready != nullptr) {
        return input.producer_ready(unit, order_id, input.user_data);
    }
    return owner_ai_default_production_producer_ready(unit);
}

UnitMovementUnit* owner_ai_find_production_producer(u32 owner_slot,
    u32 unit_type, u32 order_id, const OwnerAiProductionOrderPlanningInput& input) {
    if (input.movement == nullptr || unit_type >= kOwnerAiUnitTypeCount) {
        return nullptr;
    }

    for (UnitMovementUnit* unit : input.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_slot ||
            unit->type_id != unit_type) {
            continue;
        }
        if (owner_ai_production_producer_ready(*unit, order_id, input)) {
            return unit;
        }
    }
    return nullptr;
}

u32 owner_ai_queued_extended_count(const OwnerAiRuntimeState& state,
    u32 owner_slot, u32 unit_type,
    const OwnerAiProductionOrderPlanningInput& input) {
    if (input.queued_extended_count != nullptr) {
        return input.queued_extended_count(state, owner_slot, unit_type,
            input.user_data);
    }
    return 0;
}

void owner_ai_raise_unit_demand(OwnerAiRuntimeState& state, u32 owner_slot,
    u32 unit_type, OwnerAiProductionOrderPlanResult& result,
    const OwnerAiProductionOrderPlanningInput& input) {
    if (!owner_slot_valid(owner_slot) || unit_type >= kOwnerAiUnitTypeCount) {
        return;
    }
    if (owner_ai_queued_extended_count(state, owner_slot, unit_type, input) != 0) {
        return;
    }
    state.owners[owner_slot].unit_demand[unit_type] = 1;
    ++result.raised_unit_demand_count;
}

void run_owner_ai_maintenance_callbacks(OwnerAiRuntimeState& state,
    u32 owner_slot, const OwnerAiMaintenanceCallbacks& callbacks,
    void* user_data, bool include_first_frame_setup) {
    if (include_first_frame_setup) {
        if (callbacks.rebuild_route_targets != nullptr) {
            callbacks.rebuild_route_targets(state, owner_slot, user_data);
        }
        if (callbacks.refresh_placement_anchors != nullptr) {
            callbacks.refresh_placement_anchors(state, owner_slot, user_data);
        }
    }

    if (callbacks.maintain_transport_route_targets != nullptr) {
        callbacks.maintain_transport_route_targets(state, owner_slot, user_data);
    }
    if (callbacks.process_production_orders != nullptr) {
        callbacks.process_production_orders(state, owner_slot, user_data);
    }
    if (callbacks.process_production_demand != nullptr) {
        callbacks.process_production_demand(state, owner_slot, user_data);
    }
    if (callbacks.retarget_strategic_queue != nullptr) {
        callbacks.retarget_strategic_queue(state, owner_slot, user_data);
    }
    if (callbacks.dispatch_threat_points != nullptr) {
        callbacks.dispatch_threat_points(state, owner_slot, user_data);
    }
    if (callbacks.maintain_transport_queue != nullptr) {
        callbacks.maintain_transport_queue(state, owner_slot, user_data);
    }
}

}

OwnerAiRuntimeState& owner_ai_runtime_state() {
    return g_owner_ai_state;
}


const OwnerAiCounterRuleTable& DefaultOwnerAiCounterRuleTable() {
    // Generated from ranker.exe VA 0x00702778, 4 factions * 0x60 target types * 3 rules.
    constexpr u32 kFlatRuleCount =
        kOwnerAiCounterRuleFactionCount * kOwnerAiCounterRuleUnitTypeCount * 3;
    static constexpr std::array<u32, kFlatRuleCount> kPackedRules{{
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x00010064u, 0x0006001eu, 0xffff001eu, 0x000d0064u, 0x00030014u,
        0xffff000au, 0x00040064u, 0xffff0014u, 0xffff001eu, 0x00040064u, 0x0007001eu, 0xffff0032u, 0x000b0064u,
        0x0007001eu, 0xffff000au, 0x00040096u, 0x00070032u, 0xffff000au, 0x00070064u, 0x00080064u, 0xffff000au,
        0x00080064u, 0x000d001eu, 0xffff000au, 0x00090064u, 0x000d0032u, 0x00080032u, 0x00010064u, 0x0006001eu,
        0xffff000au, 0x000d0064u, 0x00060032u, 0xffff000au, 0x00060064u, 0x00080032u, 0xffff000au, 0x00040064u,
        0x0008001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x00050064u, 0x00020032u, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x00010064u, 0x00060032u, 0xffff000au, 0x00040064u, 0x000c0064u,
        0xffff0032u, 0x00010064u, 0x00020032u, 0xffff000au, 0x000d0064u, 0x00070032u, 0xffff000au, 0x000d0032u,
        0xffff0032u, 0xffff000au, 0x00050032u, 0xffff001eu, 0xffff000au, 0x00060032u, 0x000c0032u, 0xffff000au,
        0x000d0064u, 0xffff001eu, 0xffff000au, 0x0007012cu, 0x000c0064u, 0xffff000au, 0x000c00c8u, 0x0008001eu,
        0x0007000au, 0x000801f4u, 0xffff001eu, 0xffff000au, 0x0002003cu, 0x000c001eu, 0xffff000au, 0x00050064u,
        0x000d001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x00010064u, 0x0002001eu, 0xffff000au, 0x000100c8u, 0x000c0032u,
        0xffff001eu, 0x000d0064u, 0x00010064u, 0xffff0064u, 0x000d00c8u, 0x000c0064u, 0xffff001eu, 0x00020064u,
        0x0004001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x000c00c8u, 0x00070032u, 0xffff000au,
        0x000d0064u, 0x00020032u, 0xffff000au, 0xffff0064u, 0xffff001eu, 0xffff000au, 0x000d00c8u, 0x000700c8u,
        0xffff000au, 0x00080096u, 0x0007001eu, 0xffff000au, 0x00090064u, 0x00080032u, 0xffff000au, 0x00070064u,
        0x00080032u, 0xffff000au, 0x000d0064u, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x00010064u, 0x0004001eu, 0xffff000au, 0x00050032u, 0x00020032u,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x00050064u, 0xffff001eu, 0xffff000au, 0x00070064u,
        0x005c0032u, 0xffff000au, 0x000d0064u, 0xffff0032u, 0xffff000au, 0x00060064u, 0x000c0032u, 0xffff000au,
        0x000d0190u, 0x00060032u, 0xffff000au, 0x000c0096u, 0x000b0032u, 0xffff000au, 0x00090064u, 0x00080032u,
        0xffff000au, 0x00010032u, 0xffff001eu, 0xffff000au, 0x00010032u, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0x000d003cu, 0xffff001eu, 0xffff000au, 0x000d003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x00010082u, 0xffff001eu,
        0xffff000au, 0x0005003cu, 0x000d001eu, 0xffff000au, 0x000d003cu, 0x000c001eu, 0xffff000au, 0x000d003cu,
        0x000c001eu, 0xffff000au, 0x000d003cu, 0xffff001eu, 0xffff000au, 0x00040096u, 0x00070032u, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x001100c8u, 0x001c0032u, 0x001d0032u, 0x001200c8u, 0x00150064u,
        0x00190032u, 0x00120032u, 0x0015001eu, 0xffff001eu, 0x00120064u, 0x001d0032u, 0x0019001eu, 0x001d003cu,
        0x00110064u, 0xffff000au, 0x00120064u, 0x0019001eu, 0x0015001eu, 0x00120064u, 0xffff0032u, 0xffff000au,
        0x0012012cu, 0x001a0064u, 0x00150064u, 0x001b0064u, 0x001900c8u, 0xffff000au, 0x00110064u, 0x00150032u,
        0xffff000au, 0x00110050u, 0x00150032u, 0xffff000au, 0x00110050u, 0x00150032u, 0xffff000au, 0x00120064u,
        0x00150032u, 0x0019000au, 0xffff003cu, 0x00150032u, 0xffff000au, 0x00160064u, 0x00120064u, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x001100c8u, 0xffff001eu, 0xffff0014u, 0x001200c8u, 0x0015001eu,
        0xffff0014u, 0x001100c8u, 0xffff0032u, 0xffff0014u, 0x00120064u, 0xffff001eu, 0xffff000au, 0x00120064u,
        0xffff001eu, 0xffff000au, 0xffff0064u, 0xffff001eu, 0xffff000au, 0x00120064u, 0xffff001eu, 0xffff000au,
        0xffff0064u, 0xffff001eu, 0xffff000au, 0x001a00c8u, 0x00150064u, 0xffff0064u, 0x00110064u, 0x00120064u,
        0xffff000au, 0x001b003cu, 0xffff012cu, 0xffff000au, 0x00110064u, 0xffff001eu, 0xffff000au, 0x00160064u,
        0x00120032u, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x001100c8u, 0xffff0014u, 0xffff000au, 0x00120046u, 0xffff0032u,
        0xffff0032u, 0x00110064u, 0x0019001eu, 0x00150064u, 0x001200c8u, 0x00150032u, 0xffff001eu, 0x001200c8u,
        0x0015001eu, 0xffff000au, 0x00160064u, 0xffff001eu, 0xffff000au, 0x001100c8u, 0x001a0064u, 0xffff000au,
        0x001900c8u, 0x00150032u, 0xffff0032u, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x00120064u, 0x0019001eu,
        0xffff000au, 0x0012012cu, 0x001a00c8u, 0x001500c8u, 0x001b0064u, 0x00190064u, 0x00150064u, 0x001a0064u,
        0xffff001eu, 0xffff000au, 0x00120064u, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x001100c8u, 0xffff0014u, 0xffff000au, 0x00110032u, 0x00160032u,
        0xffff0032u, 0xffff0064u, 0xffff001eu, 0xffff000au, 0xffff0064u, 0xffff001eu, 0xffff000au, 0x001200c8u,
        0x001a0064u, 0xffff000au, 0x00120064u, 0x0015001eu, 0x0019001eu, 0x00110064u, 0x00120032u, 0x0015001eu,
        0x00120064u, 0x00190032u, 0xffff0064u, 0x00120064u, 0x001a0064u, 0xffff000au, 0x001b0064u, 0x001900c8u,
        0xffff000au, 0x00110032u, 0xffff001eu, 0xffff000au, 0x00110032u, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0x0012003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x001100c8u, 0xffff001eu,
        0xffff000au, 0x00160064u, 0x00120064u, 0xffff000au, 0x001100c8u, 0xffff001eu, 0xffff000au, 0x001100c8u,
        0xffff001eu, 0xffff000au, 0x001200c8u, 0x00150064u, 0x00190032u, 0x00120064u, 0x0019001eu, 0x0015001eu,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x0021012cu, 0x002d000au, 0xffff000au, 0x002400c8u, 0x002d000au,
        0xffff000au, 0x002400c8u, 0x002e001eu, 0x0028000au, 0x002400c8u, 0x002e0032u, 0x002d000au, 0xffff003cu,
        0xffff0064u, 0xffff000au, 0x00240064u, 0x002d000au, 0xffff000au, 0x00270064u, 0x002b000au, 0xffff000au,
        0x002700c8u, 0x00280064u, 0x002b0032u, 0x002c0064u, 0x00280064u, 0xffff000au, 0x00230064u, 0xffff001eu,
        0xffff000au, 0x00240064u, 0xffff001eu, 0xffff000au, 0x00240064u, 0xffff001eu, 0xffff000au, 0x002a0032u,
        0x00270032u, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x00240064u, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x0021012cu, 0xffff0014u, 0xffff000au, 0x002400c8u, 0xffff001eu,
        0xffff000au, 0x002400c8u, 0xffff001eu, 0xffff000au, 0x002300c8u, 0xffff0032u, 0xffff0014u, 0x00240032u,
        0xffff001eu, 0xffff000au, 0xffff0064u, 0xffff001eu, 0xffff000au, 0x00240064u, 0xffff001eu, 0xffff000au,
        0xffff0064u, 0xffff001eu, 0xffff000au, 0x00240064u, 0x00280064u, 0xffff001eu, 0x00240064u, 0x00280032u,
        0xffff0014u, 0x002b0064u, 0x00280064u, 0xffff000au, 0x00240064u, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x00210064u, 0x002e0014u, 0xffff000au, 0x0021012cu, 0xffff001eu,
        0xffff000au, 0x00230064u, 0xffff001eu, 0xffff000au, 0x002400c8u, 0xffff0014u, 0xffff000au, 0x00250064u,
        0xffff001eu, 0xffff000au, 0x00260032u, 0xffff001eu, 0xffff000au, 0x002400c8u, 0xffff001eu, 0xffff000au,
        0x002400c8u, 0xffff000au, 0xffff000au, 0xffff0064u, 0xffff001eu, 0xffff000au, 0x00240064u, 0x00270032u,
        0xffff000au, 0x002701f4u, 0x002400c8u, 0xffff000au, 0x002c0064u, 0x0028012cu, 0xffff000au, 0x0027012cu,
        0xffff001eu, 0xffff000au, 0x002400c8u, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x0021012cu, 0xffff0014u, 0xffff000au, 0x00260064u, 0x002400c8u,
        0xffff000au, 0xffff0032u, 0xffff001eu, 0xffff000au, 0x002400c8u, 0xffff001eu, 0xffff000au, 0x002700c8u,
        0x00240064u, 0xffff000au, 0x002400c8u, 0x00230064u, 0xffff000au, 0x002500c8u, 0x002100c8u, 0xffff000au,
        0x0024012cu, 0x00280064u, 0xffff000au, 0x002400c8u, 0x00270064u, 0xffff000au, 0x002c0064u, 0x0028012cu,
        0xffff000au, 0x00210032u, 0xffff001eu, 0xffff000au, 0x00210032u, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0x00210032u, 0xffff001eu, 0xffff000au, 0x00210032u, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x00220064u, 0xffff001eu,
        0xffff000au, 0x00240064u, 0xffff001du, 0x000a0000u, 0x00240064u, 0xffff001eu, 0xffff000au, 0x00240064u,
        0xffff001eu, 0xffff000au, 0x002400c8u, 0x002d000au, 0xffff000au, 0x00240064u, 0x002d000au, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x0031012cu, 0xffff001eu, 0xffff000au, 0x00320064u, 0xffff0032u,
        0xffff000au, 0x00380032u, 0x00310064u, 0xffff001eu, 0x0039012cu, 0xffff0032u, 0xffff001eu, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0x00380032u, 0x00390032u, 0xffff000au, 0x00390064u, 0x00350032u, 0xffff000au,
        0x00350064u, 0x003900c8u, 0xffff000au, 0x003a0064u, 0x003800c8u, 0xffff000au, 0x00380032u, 0xffff001eu,
        0xffff000au, 0xffff0032u, 0x00350014u, 0xffff000au, 0xffff0032u, 0x00350014u, 0xffff000au, 0x00380064u,
        0x00350028u, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x00380032u, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x00310064u, 0x00320032u, 0xffff000au, 0x00320064u, 0x00380032u,
        0xffff0014u, 0x003100c8u, 0xffff001eu, 0xffff000au, 0x00380064u, 0xffff0014u, 0xffff000au, 0x00310064u,
        0xffff001eu, 0xffff000au, 0xffff0064u, 0xffff001eu, 0xffff000au, 0x003900c8u, 0xffff001eu, 0xffff000au,
        0xffff0064u, 0xffff001eu, 0xffff000au, 0x003f00c8u, 0xffff001eu, 0xffff000au, 0x003f0064u, 0xffff0032u,
        0xffff000au, 0x003a0064u, 0x003f012cu, 0xffff000au, 0x00380032u, 0xffff001eu, 0xffff000au, 0x00380032u,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff0032u, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x00310064u, 0xffff0014u, 0xffff000au, 0x003100c8u, 0x0032001eu,
        0xffff000au, 0x003200c8u, 0xffff0032u, 0xffff000au, 0x003100c8u, 0x0032001eu, 0xffff0014u, 0x00380064u,
        0xffff0046u, 0xffff000au, 0xffff0032u, 0xffff001eu, 0xffff000au, 0x00390064u, 0x003f0032u, 0xffff000au,
        0x00380064u, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0x003200c8u, 0x00390032u,
        0xffff000au, 0x003f012cu, 0xffff0064u, 0xffff000au, 0x003a0064u, 0x003200c8u, 0x00380064u, 0x003f012cu,
        0xffff001eu, 0xffff000au, 0x003200c8u, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0x003100c8u, 0xffff0014u, 0xffff000au, 0x00330064u, 0x0032001eu,
        0xffff000au, 0xffff0032u, 0xffff001eu, 0xffff000au, 0xffff0064u, 0xffff001eu, 0xffff000au, 0x003f012cu,
        0xffff001eu, 0xffff000au, 0x00390064u, 0x0038001eu, 0xffff000au, 0x00380064u, 0xffff001eu, 0xffff000au,
        0x003900c8u, 0x00380032u, 0xffff000au, 0x003f00c8u, 0xffff001eu, 0xffff000au, 0x003a0064u, 0x003200c8u,
        0xffff000au, 0x00310032u, 0xffff001eu, 0xffff000au, 0x00310032u, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0x0038001eu, 0xffff001eu, 0xffff000au, 0x00380032u, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu,
        0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au,
        0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu, 0xffff000au, 0xffff003cu, 0xffff001eu,
        0xffff000au, 0x00380032u, 0xffff001eu, 0xffff000au, 0x00380032u, 0xffff001eu, 0xffff000au, 0x00380032u,
        0xffff001eu, 0xffff000au, 0x00320064u, 0xffff0032u, 0xffff000au, 0x00380032u, 0x00390032u, 0xffff000au,
    }};
    static const OwnerAiCounterRuleTable table = []() {
        OwnerAiCounterRuleTable result{};
        u32 flat = 0;
        for (u32 faction = 0; faction < kOwnerAiCounterRuleFactionCount; ++faction) {
            for (u32 target_type = 0; target_type < kOwnerAiCounterRuleUnitTypeCount;
                 ++target_type) {
                for (u32 slot = 0; slot < 3; ++slot) {
                    const u32 packed = kPackedRules[flat++];
                    const u32 encoded_unit = packed >> 16;
                    result[faction][target_type][slot].unit_type =
                        encoded_unit == 0xffffu ? -1 : static_cast<i32>(encoded_unit);
                    result[faction][target_type][slot].percent_bonus =
                        packed & 0xffffu;
                }
            }
        }
        return result;
    }();
    return table;
}

void ResetOwnerAiRuntime(OwnerAiRuntimeState& state) {
    state = OwnerAiRuntimeState{};
    state.profile_record_indices.fill(kOwnerAiInvalidProfileRecord);
}

bool LoadOwnerAiTargetProfile(OwnerAiRuntimeState& state, const char* archive_name,
    u32 record_index, u32 owner_slot) {
    if (!owner_slot_valid(owner_slot)) {
        return false;
    }

    if (state.load_profile_text != nullptr) {
        std::string profile_text;
        if (!state.load_profile_text(archive_name, record_index, owner_slot,
                profile_text, state.load_profile_text_user_data)) {
            return false;
        }
        if (!ParseOwnerAiProfileText(state, owner_slot, profile_text.c_str(), false)) {
            return false;
        }

        profile_text.clear();
        if (!state.load_profile_text(archive_name, record_index, owner_slot,
                profile_text, state.load_profile_text_user_data)) {
            return false;
        }
        return ParseOwnerAiProfileText(state, owner_slot, profile_text.c_str(), true);
    }

    if (state.load_profile == nullptr) {
        return false;
    }

    std::vector<OwnerAiCommandRecord> commands;
    if (!state.load_profile(archive_name, record_index, owner_slot, commands,
            state.load_profile_user_data)) {
        return false;
    }

    state.profile_commands[owner_slot] = std::move(commands);
    return true;
}

void BeginOwnerAiProfileParsePass(OwnerAiRuntimeState& state, u32 owner_slot,
    bool populate_records) {
    state.parser_owner_slot = owner_slot;
    state.parser_populate_records = populate_records;
    if (!populate_records) {
        state.profile_label_count = 0;
        state.parser_command_count = 0;
        return;
    }

    if (owner_slot_valid(owner_slot)) {
        std::vector<OwnerAiCommandRecord>& commands = state.profile_commands[owner_slot];
        commands.clear();
        commands.reserve(state.parser_command_count);
    }
    state.parser_command_count = 0;
}

bool RecordOwnerAiProfileLabel(OwnerAiRuntimeState& state, const char* marker,
    u32 command_index) {
    if (state.profile_label_count >= kOwnerAiProfileLabelCapacity || marker == nullptr) {
        return false;
    }

    OwnerAiProfileLabel& label = state.profile_labels[state.profile_label_count];
    copy_upper_label_name(label.name, marker + 1);
    label.command_index = command_index;
    ++state.profile_label_count;
    return true;
}

i32 ResolveOwnerAiProfileLabel(OwnerAiRuntimeState& state, const char* label) {
    if (!state.parser_populate_records) {
        return 0;
    }

    std::array<char, kOwnerAiProfileLabelNameLength> normalized{};
    copy_upper_label_name(normalized, label);
    for (u32 index = 0; index < state.profile_label_count; ++index) {
        if (std::strcmp(state.profile_labels[index].name.data(),
                normalized.data()) == 0) {
            return static_cast<i32>(state.profile_labels[index].command_index);
        }
    }
    return -1;
}

void AppendOwnerAiCommandRecord4(OwnerAiRuntimeState& state, i32 command_id,
    i32 arg0, i32 arg1, i32 arg2) {
    append_command_record(state, make_command_record4(command_id, arg0, arg1, arg2));
}

void AppendOwnerAiCommandRecord9(OwnerAiRuntimeState& state, i32 command_id,
    i32 arg0, i32 arg1, i32 arg2, i32 arg3, i32 arg4, i32 arg5, i32 arg6,
    i32 arg7) {
    static_cast<void>(arg0);
    static_cast<void>(arg1);
    static_cast<void>(arg2);
    static_cast<void>(arg3);
    static_cast<void>(arg4);
    append_command_record(state, make_command_record9(command_id, arg5, arg6, arg7));
}

bool ParseOwnerAiProfileCommandLine(OwnerAiRuntimeState& state, const char* line) {
    std::vector<std::string> tokens = tokenize_owner_ai_line(line);
    if (tokens.empty() || tokens[0].empty() || tokens[0][0] == ';') {
        return true;
    }

    uppercase_in_place(tokens[0]);
    if (tokens[0][0] == ':') {
        if (state.parser_populate_records) {
            return true;
        }
        return RecordOwnerAiProfileLabel(state, tokens[0].c_str(),
            state.parser_command_count);
    }

    u32 command_id = 0;
    const OwnerAiCommandDefinition* definition =
        find_owner_ai_command_definition(tokens[0], command_id);
    if (definition == nullptr) {
        return false;
    }

    const auto require_token = [&tokens](std::size_t index) -> const std::string* {
        return index < tokens.size() ? &tokens[index] : nullptr;
    };
    const auto resolve_label = [&state](const std::string& token, i32& value) -> bool {
        value = ResolveOwnerAiProfileLabel(state, token.c_str());
        return value != -1;
    };

    switch (definition->argument_class) {
    case 0:
        AppendOwnerAiCommandRecord4(state, static_cast<i32>(command_id), 0, 0, 0);
        return true;
    case 1: {
        const std::string* arg0 = require_token(1);
        if (arg0 == nullptr) {
            return false;
        }
        AppendOwnerAiCommandRecord4(state, static_cast<i32>(command_id),
            parse_owner_ai_int(*arg0), 0, 0);
        return true;
    }
    case 2: {
        const std::string* arg0 = require_token(1);
        const std::string* arg1 = require_token(2);
        if (arg0 == nullptr || arg1 == nullptr) {
            return false;
        }
        AppendOwnerAiCommandRecord4(state, static_cast<i32>(command_id),
            parse_owner_ai_int(*arg0), parse_owner_ai_int(*arg1), 0);
        return true;
    }
    case 3: {
        const std::string* arg0 = require_token(1);
        const std::string* arg1 = require_token(2);
        const std::string* arg2 = require_token(3);
        if (arg0 == nullptr || arg1 == nullptr || arg2 == nullptr) {
            return false;
        }
        AppendOwnerAiCommandRecord4(state, static_cast<i32>(command_id),
            parse_owner_ai_int(*arg0), parse_owner_ai_int(*arg1),
            parse_owner_ai_int(*arg2));
        return true;
    }
    case 4: {
        const std::string* label = require_token(1);
        i32 target = 0;
        if (label == nullptr || !resolve_label(*label, target)) {
            return false;
        }
        AppendOwnerAiCommandRecord4(state, static_cast<i32>(command_id), target, 0, 0);
        return true;
    }
    case 5: {
        const std::string* label = require_token(1);
        const std::string* arg1 = require_token(2);
        i32 target = 0;
        if (label == nullptr || arg1 == nullptr || !resolve_label(*label, target)) {
            return false;
        }
        AppendOwnerAiCommandRecord4(state, static_cast<i32>(command_id), target,
            parse_owner_ai_int(*arg1), 0);
        return true;
    }
    case 6: {
        const std::string* label = require_token(1);
        const std::string* arg1 = require_token(2);
        const std::string* arg2 = require_token(3);
        i32 target = 0;
        if (label == nullptr || arg1 == nullptr || arg2 == nullptr ||
            !resolve_label(*label, target)) {
            return false;
        }
        AppendOwnerAiCommandRecord4(state, static_cast<i32>(command_id), target,
            parse_owner_ai_int(*arg1), parse_owner_ai_int(*arg2));
        return true;
    }
    case 7: {
        if (tokens.size() < 9) {
            return false;
        }
        AppendOwnerAiCommandRecord9(state, static_cast<i32>(command_id),
            parse_owner_ai_int(tokens[1]), parse_owner_ai_int(tokens[2]),
            parse_owner_ai_int(tokens[3]), parse_owner_ai_int(tokens[4]),
            parse_owner_ai_int(tokens[5]), parse_owner_ai_int(tokens[6]),
            parse_owner_ai_int(tokens[7]), parse_owner_ai_int(tokens[8]));
        return true;
    }
    default:
        return false;
    }
}

bool ParseOwnerAiProfileText(OwnerAiRuntimeState& state, u32 owner_slot,
    const char* profile_text, bool populate_records) {
    BeginOwnerAiProfileParsePass(state, owner_slot, populate_records);

    bool parsed_all_lines = true;
    for (const std::string& line : split_owner_ai_profile_lines(profile_text)) {
        if (!ParseOwnerAiProfileCommandLine(state, line.c_str())) {
            parsed_all_lines = false;
        }
    }
    return parsed_all_lines;
}

void RefreshOwnerTargetDataForSlot(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots, u32 owner_slot) {
    if (!owner_slot_valid(owner_slot)) {
        return;
    }

    state.command_player_slots = &player_slots;
    state.owners[owner_slot].primary_target_owner =
        static_cast<i32>(player_slots.nearest_hostile_slots[owner_slot]);
    const bool generic_profile = use_generic_ai_profile_table(state);
    const char* archive_name = generic_profile ? "JW2_14.TRC" : "JW2_16.TRC";
    const u32 record_index =
        generic_profile ? 0 : stage_specific_profile_record(state, owner_slot);

    if (LoadOwnerAiTargetProfile(state, archive_name, record_index, owner_slot)) {
        state.profile_record_indices[owner_slot] = static_cast<i32>(record_index);
    } else {
        state.profile_record_indices[owner_slot] = kOwnerAiInvalidProfileRecord;
    }

    if (state.frame_counter == 0) {
        ResetOwnerAiSlotRuntime(state, player_slots, owner_slot);
    }
}

void ReloadSkirmishOwnerTargetProfiles(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots) {
    state.command_player_slots = &player_slots;
    for (u32 owner_slot = 0; owner_slot < kOwnerAiOwnerCount; ++owner_slot) {
        if (!owner_is_player_controlled(player_slots, owner_slot)) {
            continue;
        }

        const i32 record_index = state.profile_record_indices[owner_slot];
        if (!LoadOwnerAiTargetProfile(state, "JW2_14.TRC",
                static_cast<u32>(record_index), owner_slot)) {
            state.profile_record_indices[owner_slot] = kOwnerAiInvalidProfileRecord;
        } else {
            state.profile_record_indices[owner_slot] = record_index;
        }
    }
}

void ResetOwnerAiSlotRuntime(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots, u32 owner_slot) {
    if (!owner_slot_valid(owner_slot)) {
        return;
    }

    OwnerAiSlotRuntime& owner = state.owners[owner_slot];
    const OwnerAiPoint start = owner_start_point(player_slots, owner_slot);

    owner.script_halted = 0;
    owner.primary_interval = 10;
    owner.primary_radius = 0x46;
    owner.primary_budget = 0;
    clear_shared_reset_tables(state);
    owner.route_load_percent = 10;
    owner.support_interval = 10;
    owner.support_radius = 0x46;
    owner.support_target_owner = -1;
    owner.support_mode = 0;
    owner.support_budget = 0;
    owner.support_anchor = -1;
    owner.secondary_budget = 0;
    owner.resource_budget_percent = 0x7d;
    owner.secondary_mode = 0;
    owner.profile_counter = 0;
    owner.unit_demand.fill(0);
    owner.unit_demand_shadow.fill(0);
    owner.route_refresh_counter = 0;
    owner.script_cycle_counter = 0;
    owner.previous_script_cycle_counter = -1;
    owner.script_enabled = 1;
    owner.last_timing_frame = 0;
    owner.build_budget = 0;
    owner.production_budget = 0;
    owner.rally_delay = 0x78;
    owner.reserve_budget = 0;
    owner.reserve_delay = 0x78;
    owner.strategic_retarget_quota_floor = 0xffffffffu;
    owner.route_target_score = 100;
    owner.profile_gate_flag = 0;
    owner.production_pause_flag = 0;
    owner.route_radius = 0x46;
    owner.primary_target_point = start;
    owner.primary_target_radius = 0x14;
    owner.primary_target_flags = 0;
    owner.placement_radius = 0x14;
    owner.placement_record[0] = start.x;
    owner.placement_record[1] = start.y;
    owner.placement_target_radius = 0x14;
    reset_owner_threat_points(owner);
    owner.attack_interval = 10;
    owner.attack_radius = 0x46;
    owner.attack_target_radius = 0x14;
    owner.attack_target_owner = -1;
    owner.support_target_slot = -1;
    owner.fallback_target_slot = -1;
    owner.profile_state_flag = 0;
    owner.profile_age = 0;
}

void AdvanceOwnerAiCommandRecord(OwnerAiCommandRecord& command, u32& command_index) {
    ++command.words[10];
    ++command_index;
}

u32 SelectOwnerAiRandomValue(OwnerAiRuntimeState& state, u32 limit) {
    if (limit == 0) {
        return 0;
    }

    const u32 dividend = state.random_seed + state.frame_counter;
    const u32 quotient = dividend / limit;
    const u32 result = dividend % limit;
    state.random_seed += quotient;
    state.random_seed ^=
        ReadOriginalRandomScrambleDword(state.random_scramble,
            (quotient >> 28) & 0x0f) + 1u;
    ++state.random_call_count;
    return result;
}

void ApplyOwnerAiCommand(OwnerAiRuntimeState& state, u32 owner_slot,
    OwnerAiCommandRecord& command, u32& command_index) {
    if (!owner_slot_valid(owner_slot)) {
        return;
    }

    OwnerAiSlotRuntime& owner = state.owners[owner_slot];
    const i32* words = command.words.data();
    const bool cycle_changed = owner_script_cycle_changed(owner);
    const u32 faction_id = state.owner_faction_ids[owner_slot];
    const bool faction_valid = owner_ai_faction_valid(faction_id);

    switch (words[0]) {
    case 1:
        set_field(owner.primary_interval, words[1]);
        set_field(owner.primary_radius, words[2]);
        break;
    case 2:
        if (faction_valid) {
            set_owner_ai_unit_demand_max(owner,
                static_cast<i32>(kOwnerAiPostInitRequirementUnitTypes[faction_id]),
                words[1]);
        }
        break;
    case 3:
        if (faction_valid) {
            add_owner_ai_unit_demand(owner,
                static_cast<i32>(kOwnerAiPostInitRequirementUnitTypes[faction_id]),
                words[1], words[2]);
        }
        break;
    case 4:
        if (faction_valid) {
            sub_owner_ai_unit_demand(owner,
                static_cast<i32>(kOwnerAiPostInitRequirementUnitTypes[faction_id]),
                words[1], words[2]);
        }
        break;
    case 5:
        set_field(owner.primary_budget, words[1]);
        break;
    case 6:
        add_field_clamped_max(owner.primary_budget, words[1], words[2]);
        break;
    case 7:
        sub_field_clamped_min(owner.primary_budget, words[1], words[2]);
        break;
    case 8:
        set_field(owner.route_load_percent, words[1]);
        break;
    case 9:
        add_field_clamped_max(owner.route_load_percent, words[1], words[2]);
        break;
    case 10:
        sub_field_clamped_min(owner.route_load_percent, words[1], words[2]);
        break;
    case 11:
        set_field(owner.support_interval, words[1]);
        set_field(owner.support_radius, words[2]);
        break;
    case 12:
        set_field(owner.secondary_budget, words[1]);
        break;
    case 13:
        add_field_clamped_max(owner.secondary_budget, words[1], words[2]);
        break;
    case 14:
        sub_field_clamped_min(owner.secondary_budget, words[1], words[2]);
        break;
    case 15:
        set_field(owner.resource_budget_percent, words[1]);
        break;
    case 16:
        add_field_clamped_max(owner.resource_budget_percent, words[1], words[2]);
        break;
    case 17:
        sub_field_clamped_min(owner.resource_budget_percent, words[1], words[2]);
        break;
    case 18:
        if (faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiBaseUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    set_owner_ai_unit_demand(owner, unit_type, words[1]);
                });
        }
        break;
    case 19:
        if (cycle_changed && faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiBaseUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    add_owner_ai_unit_demand(owner, unit_type, words[1], words[2]);
                });
        }
        break;
    case 20:
        if (cycle_changed && faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiBaseUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    sub_owner_ai_unit_demand(owner, unit_type, words[1], words[2]);
                });
        }
        break;
    case 21:
        if (faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiMiddleUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    set_owner_ai_unit_demand(owner, unit_type, words[1]);
                });
        }
        break;
    case 22:
        if (cycle_changed && faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiMiddleUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    add_owner_ai_unit_demand(owner, unit_type, words[1], words[2]);
                });
        }
        break;
    case 23:
        if (cycle_changed && faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiMiddleUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    sub_owner_ai_unit_demand(owner, unit_type, words[1], words[2]);
                });
        }
        break;
    case 24:
        if (faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiHighUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    set_owner_ai_unit_demand(owner, unit_type, words[1]);
                });
        }
        break;
    case 25:
        if (cycle_changed && faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiHighUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    add_owner_ai_unit_demand(owner, unit_type, words[1], words[2]);
                });
        }
        break;
    case 26:
        if (cycle_changed && faction_valid) {
            apply_owner_ai_unit_group(kOwnerAiHighUnitDemandGroups[faction_id],
                [&owner, words](i32 unit_type) {
                    sub_owner_ai_unit_demand(owner, unit_type, words[1], words[2]);
                });
        }
        break;
    case 27:
        set_owner_ai_unit_demand(owner, words[1], words[2]);
        break;
    case 28:
        if (cycle_changed) {
            add_owner_ai_unit_demand(owner, words[1], words[2], words[3]);
        }
        break;
    case 29:
        if (cycle_changed) {
            sub_owner_ai_unit_demand(owner, words[1], words[2], words[3]);
        }
        break;
    case 30:
        set_field(owner.profile_counter, words[1]);
        break;
    case 31:
        if (cycle_changed) {
            add_field_clamped_max(owner.profile_counter, words[1], words[2]);
        }
        break;
    case 32:
        if (cycle_changed) {
            sub_field_clamped_min(owner.profile_counter, words[1], words[2]);
        }
        break;
    case 34:
        set_field(owner.build_budget, words[1]);
        break;
    case 35:
        if (cycle_changed) {
            add_field_clamped_max(owner.build_budget, words[1], words[2]);
        }
        break;
    case 36:
        if (cycle_changed) {
            sub_field_clamped_max(owner.build_budget, words[1], words[2]);
        }
        break;
    case 37:
        set_field(owner.production_budget, words[1]);
        break;
    case 38:
        if (cycle_changed) {
            add_field_clamped_max(owner.production_budget, words[1], words[2]);
        }
        break;
    case 39:
        if (cycle_changed) {
            sub_field_clamped_min(owner.production_budget, words[1], words[2]);
        }
        break;
    case 40:
        set_field(owner.rally_delay, words[1]);
        break;
    case 41:
        if (cycle_changed) {
            add_field_clamped_max(owner.rally_delay, words[1], words[2]);
        }
        break;
    case 42:
        if (cycle_changed) {
            sub_field_clamped_min(owner.rally_delay, words[1], words[2]);
        }
        break;
    case 43:
        set_field(owner.reserve_budget, words[1]);
        break;
    case 44:
        if (cycle_changed) {
            add_field_clamped_max(owner.reserve_budget, words[1], words[2]);
        }
        break;
    case 45:
        if (cycle_changed) {
            sub_field_clamped_min(owner.reserve_budget, words[1], words[2]);
        }
        break;
    case 46:
        set_field(owner.reserve_delay, words[1]);
        break;
    case 47:
        if (cycle_changed) {
            add_field_clamped_max(owner.reserve_delay, words[1], words[2]);
        }
        break;
    case 48:
        if (cycle_changed) {
            sub_field_clamped_min(owner.reserve_delay, words[1], words[2]);
        }
        break;
    case 49:
        set_field(owner.strategic_retarget_quota_floor, words[1]);
        break;
    case 50:
        if (cycle_changed) {
            add_field_clamped_max(owner.strategic_retarget_quota_floor,
                words[1], words[2]);
        }
        break;
    case 51:
        if (cycle_changed) {
            sub_field_clamped_min(owner.strategic_retarget_quota_floor,
                words[1], words[2]);
        }
        break;
    case 52:
        set_field(owner.route_target_score, words[1]);
        break;
    case 53:
        if (cycle_changed) {
            add_field_clamped_max(owner.route_target_score, words[1], words[2]);
        }
        break;
    case 54:
        if (cycle_changed) {
            sub_field_clamped_min(owner.route_target_score, words[1], words[2]);
        }
        break;
    case 55:
        set_field(owner.route_radius, words[1]);
        break;
    case 56:
        if (cycle_changed) {
            add_field_clamped_max(owner.route_radius, words[1], words[2]);
        }
        break;
    case 57:
        if (cycle_changed) {
            sub_field_clamped_min(owner.route_radius, words[1], words[2]);
        }
        break;
    case 58:
        set_field(owner.placement_radius, words[1]);
        break;
    case 59:
        if (cycle_changed) {
            add_field_clamped_max(owner.placement_radius, words[1], words[2]);
        }
        break;
    case 60:
        if (cycle_changed) {
            sub_field_clamped_min(owner.placement_radius, words[1], words[2]);
        }
        break;
    case 61:
        set_field(owner.placement_target_radius, words[1]);
        break;
    case 62:
        if (cycle_changed) {
            add_field_clamped_max(owner.placement_target_radius, words[1], words[2]);
        }
        break;
    case 63:
        if (cycle_changed) {
            sub_field_clamped_min(owner.placement_target_radius, words[1], words[2]);
        }
        break;
    case 64:
        set_field(owner.attack_interval, words[1]);
        set_field(owner.attack_radius, words[2]);
        break;
    case 65:
        set_field(owner.attack_target_radius, words[1]);
        break;
    case 66:
        if (cycle_changed) {
            add_field_clamped_max(owner.attack_target_radius, words[1], words[2]);
        }
        break;
    case 67:
        if (cycle_changed) {
            sub_field_clamped_min(owner.attack_target_radius, words[1], words[2]);
        }
        break;
    case 68:
        set_field(owner.attack_target_owner, words[1]);
        break;
    case 69:
        set_field(owner.support_target_slot, words[1]);
        break;
    case 70:
        set_field(owner.fallback_target_slot, words[1]);
        break;
    case 71:
        set_field(owner.primary_target_radius, words[1]);
        break;
    case 72:
        if (cycle_changed) {
            add_field_clamped_max(owner.primary_target_radius, words[1], words[2]);
        }
        break;
    case 73:
        if (cycle_changed) {
            sub_field_clamped_min(owner.primary_target_radius, words[1], words[2]);
        }
        break;
    case 74:
        jump_owner_ai_command(command, command_index);
        return;
    case 75: {
        const u32 random_value = state.random_value != nullptr
            ? state.random_value(state, static_cast<u32>(words[3]),
                  state.random_value_user_data)
            : SelectOwnerAiRandomValue(state, static_cast<u32>(words[3]));
        const bool should_jump = random_value > static_cast<u32>(words[2]);
        if (owner_ai_condition_jump(should_jump, command, command_index)) {
            return;
        }
        break;
    }
    case 76:
        if (owner_ai_condition_jump(owner.profile_state_flag == 1, command,
                command_index)) {
            return;
        }
        break;
    case 77:
        if (owner_ai_condition_jump(owner.profile_state_flag == 0, command,
                command_index)) {
            return;
        }
        break;
    case 78:
        set_field(owner.profile_age, words[1]);
        break;
    case 79:
        set_field(owner.script_cycle_counter, words[1]);
        break;
    case 80:
        if (owner_ai_condition_jump(owner.profile_age >= static_cast<u32>(words[2]),
                command, command_index)) {
            return;
        }
        break;
    case 81:
        if (owner_ai_condition_jump(owner.profile_age < static_cast<u32>(words[2]),
                command, command_index)) {
            return;
        }
        break;
    case 82:
        if (owner_ai_condition_jump(
                owner.script_cycle_counter >= static_cast<u32>(words[2]), command,
                command_index)) {
            return;
        }
        break;
    case 83:
        if (owner_ai_condition_jump(
                owner.script_cycle_counter < static_cast<u32>(words[2]), command,
                command_index)) {
            return;
        }
        break;
    case 84:
        if (owner_ai_condition_jump(state.frame_counter >= static_cast<u32>(words[2]),
                command, command_index)) {
            return;
        }
        break;
    case 85:
        if (owner_ai_condition_jump(state.frame_counter < static_cast<u32>(words[2]),
                command, command_index)) {
            return;
        }
        break;
    case 86:
        if (owner_ai_condition_jump(
                owner_ai_unit_count(state, owner_slot, words[2]) >=
                    static_cast<u32>(words[3]),
                command, command_index)) {
            return;
        }
        break;
    case 87:
        if (owner_ai_condition_jump(
                owner_ai_unit_count(state, owner_slot, words[2]) <
                    static_cast<u32>(words[3]),
                command, command_index)) {
            return;
        }
        break;
    case 88: {
        OwnerAiEligibleUnitSummary summary;
        const bool should_jump = owner_ai_eligible_summary(state, owner_slot, summary) &&
            summary.count >= static_cast<u32>(words[2]);
        if (owner_ai_condition_jump(should_jump, command, command_index)) {
            return;
        }
        break;
    }
    case 89: {
        OwnerAiEligibleUnitSummary summary;
        const bool should_jump = owner_ai_eligible_summary(state, owner_slot, summary) &&
            summary.count < static_cast<u32>(words[2]);
        if (owner_ai_condition_jump(should_jump, command, command_index)) {
            return;
        }
        break;
    }
    case 90: {
        const u32 hostile_slot = owner_ai_current_target_owner(state, owner_slot);
        if (owner_ai_condition_jump(
                owner_ai_unit_count(state, hostile_slot, words[2]) >=
                    static_cast<u32>(words[3]),
                command, command_index)) {
            return;
        }
        break;
    }
    case 91: {
        const u32 hostile_slot = owner_ai_current_target_owner(state, owner_slot);
        if (owner_ai_condition_jump(
                owner_ai_unit_count(state, hostile_slot, words[2]) <
                    static_cast<u32>(words[3]),
                command, command_index)) {
            return;
        }
        break;
    }
    case 92: {
        const u32 hostile_slot = owner_ai_current_target_owner(state, owner_slot);
        OwnerAiEligibleUnitSummary summary;
        const bool should_jump = owner_ai_eligible_summary(state, hostile_slot, summary) &&
            summary.count >= static_cast<u32>(words[2]);
        if (owner_ai_condition_jump(should_jump, command, command_index)) {
            return;
        }
        break;
    }
    case 93: {
        const u32 hostile_slot = owner_ai_current_target_owner(state, owner_slot);
        OwnerAiEligibleUnitSummary summary;
        const bool should_jump = owner_ai_eligible_summary(state, hostile_slot, summary) &&
            summary.count < static_cast<u32>(words[2]);
        if (owner_ai_condition_jump(should_jump, command, command_index)) {
            return;
        }
        break;
    }
    case 94:
        if (owner_ai_condition_jump(state.owner_faction_ids[owner_slot] ==
                    static_cast<u32>(words[2]),
                command, command_index)) {
            return;
        }
        break;
    case 95: {
        const u32 hostile_slot = owner_ai_current_target_owner(state, owner_slot);
        const bool should_jump = owner_slot_valid(hostile_slot) &&
            state.owner_faction_ids[hostile_slot] == static_cast<u32>(words[2]);
        if (owner_ai_condition_jump(should_jump, command, command_index)) {
            return;
        }
        break;
    }
    case 96: {
        const bool loaded = LoadOwnerAiTargetProfile(state, "JW2_14.TRC",
            static_cast<u32>(words[1]), owner_slot);
        state.profile_record_indices[owner_slot] =
            loaded ? words[1] : kOwnerAiInvalidProfileRecord;
        if (state.command_player_slots != nullptr) {
            ResetOwnerAiSlotRuntime(state, *state.command_player_slots, owner_slot);
        } else {
            PlayerSlotRuntimeState fallback_slots;
            ResetOwnerAiSlotRuntime(state, fallback_slots, owner_slot);
        }
        command_index = 0;
        return;
    }
    case 97:
    case 98:
        owner.script_halted = 1;
        finish_owner_ai_command_script(state, owner_slot, command, command_index);
        return;
    case 99:
        owner.profile_gate_flag = 1;
        break;
    case 100:
        owner.profile_gate_flag = 0;
        break;
    case 101:
        finish_owner_ai_command_script(state, owner_slot, command, command_index);
        return;
    default:
        break;
    }

    AdvanceOwnerAiCommandRecord(command, command_index);
}

void ApplyOwnerAiCommandRecord(OwnerAiRuntimeState& state, u32 owner_slot,
    OwnerAiCommandRecord& command, u32& command_index) {
    ApplyOwnerAiCommand(state, owner_slot, command, command_index);
}

void TickOwnerAiCommandScript(OwnerAiRuntimeState& state, u32 owner_slot) {
    if (!owner_slot_valid(owner_slot) ||
        state.profile_record_indices[owner_slot] == kOwnerAiInvalidProfileRecord) {
        return;
    }

    std::vector<OwnerAiCommandRecord>& commands = state.profile_commands[owner_slot];
    u32 command_index = 0;
    while (command_index < commands.size()) {
        OwnerAiCommandRecord& command = commands[command_index];
        const u32 previous_index = command_index;
        if (state.command_handler != nullptr) {
            state.command_handler(state, owner_slot, command, command_index,
                state.command_handler_user_data);
            if (command_index == previous_index) {
                AdvanceOwnerAiCommandRecord(command, command_index);
            }
        } else {
            ApplyOwnerAiCommand(state, owner_slot, command, command_index);
        }
    }

    OwnerAiSlotRuntime& owner = state.owners[owner_slot];
    owner.previous_script_cycle_counter = static_cast<i32>(owner.script_cycle_counter);
    ++owner.profile_age;
}

void TickOwnerAiMaintenance(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots,
    const OwnerAiMaintenanceCallbacks& callbacks, void* user_data) {
    state.command_player_slots = &player_slots;

    if (state.frame_counter == 1) {
        state.owner_population_used.fill(0);
        state.owner_population_reserved.fill(0);
        for (u32 owner_slot = 0; owner_slot < kOwnerAiOwnerCount; ++owner_slot) {
            if (!owner_is_player_controlled(player_slots, owner_slot)) {
                continue;
            }

            TickOwnerAiCommandScript(state, owner_slot);
            if (state.owners[owner_slot].script_halted == 0) {
                run_owner_ai_maintenance_callbacks(state, owner_slot, callbacks,
                    user_data, true);
            }
        }
        return;
    }

    if ((state.frame_counter & 0x0fu) != 0) {
        return;
    }

    const u32 owner_slot = (state.frame_counter >> 4) & 7u;
    if (!owner_is_player_controlled(player_slots, owner_slot) ||
        state.owners[owner_slot].script_halted != 0) {
        return;
    }

    TickOwnerAiCommandScript(state, owner_slot);
    run_owner_ai_maintenance_callbacks(state, owner_slot, callbacks,
        user_data, false);
}

OwnerAiStrategicPressureSummary CalculateOwnerAiStrategicTargetPressureSummary(
    const OwnerAiRuntimeState& state, u32 owner_slot,
    const OwnerAiStrategicRetargetGateInput& input) {
    (void)state;
    OwnerAiStrategicPressureSummary summary;
    if (!owner_slot_valid(owner_slot)) {
        summary.weight = 20000;
        return summary;
    }

    if (input.movement == nullptr) {
        summary.weight = 20000;
        return summary;
    }

    const UnitMovementUnit* route_target =
        input.strategic_route_targets[owner_slot];
    if (route_target == nullptr) {
        summary.weight = 20000;
        return summary;
    }

    const i32 reference_x = route_target->x;
    const i32 reference_y = route_target->y;
    for (const UnitMovementUnit* unit : input.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_slot ||
            !owner_ai_pressure_unit_eligible(*unit, input)) {
            continue;
        }

        const u32 distance = CalculateApproxUnitDistance(reference_x, reference_y,
            unit->x, unit->y);
        if (distance >= kOwnerAiStrategicPressureDistance) {
            continue;
        }

        ++summary.count;
        summary.weight += owner_ai_unit_weight(*unit, input);
    }
    return summary;
}

bool ShouldOwnerAiRunStrategicQueueRetarget(OwnerAiRuntimeState& state,
    u32 owner_slot, const OwnerAiStrategicRetargetGateInput& input) {
    if (!owner_slot_valid(owner_slot) ||
        input.owner_phase_state != 1) {
        return false;
    }

    OwnerAiSlotRuntime& owner = state.owners[owner_slot];
    const bool population_gap_open =
        owner_ai_population_gap_open(input, owner_slot);

    if (owner.build_budget != 0) {
        const u32 elapsed_frames = state.frame_counter - owner.last_timing_frame;
        if (elapsed_frames / 0x16u > owner.build_budget) {
            owner.last_timing_frame = state.frame_counter;
            return true;
        }
    }

    OwnerAiEligibleUnitSummary own_summary;
    bool own_summary_loaded = false;
    const auto load_own_summary = [&]() -> const OwnerAiEligibleUnitSummary& {
        if (!own_summary_loaded) {
            own_summary = owner_ai_eligible_summary_or_unit_counts(state,
                owner_slot);
            own_summary_loaded = true;
        }
        return own_summary;
    };

    if (owner.production_budget != 0) {
        const OwnerAiEligibleUnitSummary& summary = load_own_summary();
        if (summary.count >= owner.production_budget || !population_gap_open) {
            return true;
        }
    }

    if (owner.rally_delay != 0) {
        const OwnerAiEligibleUnitSummary& summary = load_own_summary();
        const u32 target_owner =
            owner_ai_current_target_owner(state, owner_slot);
        const OwnerAiStrategicPressureSummary target_summary =
            CalculateOwnerAiStrategicTargetPressureSummary(state, target_owner,
                input);
        const u32 ratio = static_cast<u32>(
            (static_cast<u64>(std::max<i32>(summary.weight, 0)) * 100u) /
            (target_summary.weight + 1u));
        if (ratio >= owner.rally_delay || !population_gap_open) {
            return true;
        }
    }

    if (owner.profile_counter != 0 &&
        owner_ai_counter_rule_gate_satisfied(state, owner_slot, input)) {
        return true;
    }

    if (owner.reserve_delay == 0) {
        return false;
    }

    const OwnerAiEligibleUnitSummary& summary = load_own_summary();
    const u32 target_owner =
        owner_ai_current_target_owner(state, owner_slot);
    const OwnerAiStrategicPressureSummary pressure =
        CalculateOwnerAiStrategicTargetPressureSummary(state, target_owner,
            input);
    const u32 pressure_ratio = static_cast<u32>(
        (static_cast<u64>(std::max<i32>(summary.weight, 0)) * 100u) /
        (pressure.weight + 1u));
    return pressure_ratio >= owner.reserve_delay;
}

bool TickOwnerAiStrategicQueueRetargetGate(OwnerAiRuntimeState& state,
    u32 owner_slot, const OwnerAiStrategicRetargetGateInput& input) {
    if (!ShouldOwnerAiRunStrategicQueueRetarget(state, owner_slot, input)) {
        return false;
    }
    if (input.retarget_queue != nullptr) {
        input.retarget_queue(state, owner_slot, input.user_data);
    }
    return true;
}

OwnerAiRoutePathProbeResult ProbeOwnerAiRoutePath(UnitMovementContext& movement,
    UnitMovementUnit& probe, UnitMovementPoint start,
    UnitMovementPoint destination) {
    OwnerAiRoutePathProbeResult result;
    result.attempted = true;
    result.start = start;
    result.destination = destination;
    result.start_tile = UnitMovementPoint{start.x / 32, start.y / 32};
    result.destination_tile =
        UnitMovementPoint{destination.x / 32, destination.y / 32};

    probe.x = start.x;
    probe.y = start.y;
    probe.current_cell_x = start.x & ~0x1f;
    probe.current_cell_y = start.y & ~0x1f;
    probe.destination_x = destination.x;
    probe.destination_y = destination.y;
    probe.path_target_x = destination.x;
    probe.path_target_y = destination.y;
    probe.saved_path_target_x = destination.x;
    probe.saved_path_target_y = destination.y;
    probe.next_path_x = start.x;
    probe.next_path_y = start.y;

    result.reachable = movement.callbacks.run_pathfinder != nullptr
        ? movement.callbacks.run_pathfinder(movement, probe)
        : RunLegacyUnitPathfinder(movement, probe, &result.path_tiles);
    result.final_path_target = {probe.path_target_x, probe.path_target_y};
    result.next_path_point = {probe.next_path_x, probe.next_path_y};
    result.final_path_target_tile =
        UnitMovementPoint{probe.path_target_x / 32, probe.path_target_y / 32};
    result.next_path_tile =
        UnitMovementPoint{probe.next_path_x / 32, probe.next_path_y / 32};
    result.path_target_adjusted =
        result.final_path_target.x != destination.x ||
        result.final_path_target.y != destination.y;

    // RunLegacyUnitPathfinder 0x00508f0f..0x00508f51 publishes found=0,
    // direct=0 and path_count=0 when a blocked requested goal is adjusted
    // all the way back to the start tile.  The reconstructed pathfinder's
    // convenient same-tile return is true, so restore the original probe
    // metadata before any owner-AI caller consumes it.
    if (result.path_target_adjusted &&
        result.final_path_target_tile.x == result.start_tile.x &&
        result.final_path_target_tile.y == result.start_tile.y) {
        result.reachable = false;
        result.path_tiles.clear();
    }

    if (!result.reachable) {
        result.path_cost = 0xffffffffu;
        return result;
    }

    // RunLegacyUnitPathfinder's direct-path flag is distinct from both
    // reachability and target adjustment.  Preserve it explicitly instead
    // of inferring it from next_path, whose straight-path output has legacy
    // global semantics that differ from the probe's initialized value.
    result.direct_path = CheckStraightUnitPathTiles(movement, probe,
        result.start_tile, result.final_path_target_tile);
    if (result.direct_path) {
        // Legacy direct paths leave path_count at zero and publish the goal
        // as next_path.  Route-helper scoring consumes that zero directly.
        result.path_tiles.clear();
        result.path_cost = 0;
        result.next_path_point = result.final_path_target;
        result.next_path_tile = result.final_path_target_tile;
        return result;
    }

    if (!result.path_tiles.empty()) {
        result.path_cost = static_cast<u32>(result.path_tiles.size() - 1);
        return result;
    }

    const i32 dx = std::abs(result.destination_tile.x - result.start_tile.x);
    const i32 dy = std::abs(result.destination_tile.y - result.start_tile.y);
    result.path_cost = static_cast<u32>(std::max(dx, dy));
    return result;
}

OwnerAiRoutePathProbeResult ProbeOwnerAiRoutePath(UnitMovementContext& movement,
    const UnitMovementUnit* reusable_unit, UnitMovementPoint start,
    UnitMovementPoint destination) {
    UnitMovementUnit probe = reusable_unit != nullptr
        ? *reusable_unit
        : UnitMovementUnit{};
    return ProbeOwnerAiRoutePath(movement, probe, start, destination);
}

OwnerAiProductionOrderSelection SelectOwnerAiProductionOrderAction(
    OwnerAiRuntimeState& state, u32 owner_slot, u32 order_id,
    u32 reserved_primary_cost, const OwnerAiProductionOrderPlanningInput& input) {
    OwnerAiProductionOrderSelection selection;
    selection.order_id = order_id;
    if (!owner_slot_valid(owner_slot) || order_id >= kProductionOrderCount ||
        input.catalog == nullptr || input.production_state == nullptr) {
        selection.action = OwnerAiProductionOrderActionCode::unavailable;
        return selection;
    }

    const ProductionOrderDefinition* definition =
        owner_ai_find_production_order_definition(*input.catalog, order_id);
    if (definition == nullptr) {
        selection.action = OwnerAiProductionOrderActionCode::unavailable;
        return selection;
    }

    ProductionOrderRuntimeState& production = *input.production_state;
    selection.variant = production.variant_counts[owner_slot][order_id];
    if ((production.lock_flags[owner_slot][order_id] & 3u) != 0 ||
        definition->max_variant_count <= selection.variant) {
        selection.action = OwnerAiProductionOrderActionCode::unavailable;
        return selection;
    }

    const u32 primary_unit_type = owner_ai_primary_production_unit_type(*definition);
    if (primary_unit_type < kOwnerAiUnitTypeCount &&
        state.owner_unit_type_counts[owner_slot][primary_unit_type] == 0) {
        selection.action = OwnerAiProductionOrderActionCode::demand_primary_unit;
        selection.missing_unit_type = primary_unit_type;
        return selection;
    }

    for (u32 prerequisite : definition->prerequisite_type_ids) {
        if (prerequisite >= kOwnerAiUnitTypeCount) {
            continue;
        }
        if (state.owner_unit_type_counts[owner_slot][prerequisite] == 0) {
            selection.action =
                OwnerAiProductionOrderActionCode::demand_prerequisite_unit;
            selection.missing_unit_type = prerequisite;
            return selection;
        }
    }

    selection.primary_cost = CalculateProductionOrderCost(definition->primary_cost,
        selection.variant);
    const i32 available_primary = signed_i32_from_wrapped_u32(
        production.owner_primary_resources[owner_slot] - reserved_primary_cost);
    if (signed_i32_from_wrapped_u32(selection.primary_cost) > available_primary) {
        selection.action = OwnerAiProductionOrderActionCode::reserve_primary_cost;
        return selection;
    }

    selection.producer = owner_ai_find_production_producer(owner_slot,
        primary_unit_type, order_id, input);
    if (selection.producer == nullptr) {
        selection.action = OwnerAiProductionOrderActionCode::no_ready_producer;
        return selection;
    }

    selection.action = OwnerAiProductionOrderActionCode::issue_order;
    return selection;
}

OwnerAiProductionOrderPlanResult ProcessOwnerAiProductionOrderRequests(
    OwnerAiRuntimeState& state, u32 owner_slot,
    const OwnerAiProductionOrderPlanningInput& input) {
    OwnerAiProductionOrderPlanResult result;
    if (!owner_slot_valid(owner_slot)) {
        return result;
    }

    OwnerAiSlotRuntime& owner = state.owners[owner_slot];
    owner.production_pause_flag = 0;
    if (input.production_state == nullptr) {
        return result;
    }

    const u32 faction = state.owner_faction_ids[owner_slot];
    if (faction < input.faction_primary_unit_types.size()) {
        const u32 primary_type = input.faction_primary_unit_types[faction];
        const u32 opening_order = input.faction_opening_order_ids[faction];
        if (primary_type < kOwnerAiUnitTypeCount &&
            opening_order < kOwnerAiSharedGridDwordCount / kOwnerAiOwnerCount) {
            const u32 owned_primary =
                state.owner_unit_type_counts[owner_slot][primary_type];
            const bool opening_ready = owner.attack_target_owner < 0
                ? owner.unit_demand[primary_type] <= owned_primary
                : static_cast<u32>(owner.attack_target_owner) <= owned_primary;
            if (opening_ready) {
                state.shared_grid_table[owner_slot * kProductionOrderCount +
                    opening_order] = 1;
            }
        }
    }

    for (u32 order_id = 0; order_id < kProductionOrderCount; ++order_id) {
        const u32 reserved_primary_cost = input.initial_reserved_primary_cost;
        const u32 desired_variant =
            state.shared_grid_table[owner_slot * kProductionOrderCount + order_id];
        const u32 current_variant =
            input.production_state->variant_counts[owner_slot][order_id];
        if (current_variant >= desired_variant) {
            continue;
        }

        const OwnerAiProductionOrderSelection selection =
            SelectOwnerAiProductionOrderAction(state, owner_slot, order_id,
                reserved_primary_cost, input);
        switch (selection.action) {
        case OwnerAiProductionOrderActionCode::reserve_primary_cost:
            result.reserved_primary_cost += selection.primary_cost;
            owner.production_pause_flag += selection.primary_cost;
            break;
        case OwnerAiProductionOrderActionCode::demand_primary_unit:
        case OwnerAiProductionOrderActionCode::demand_prerequisite_unit:
            owner_ai_raise_unit_demand(state, owner_slot,
                selection.missing_unit_type, result, input);
            break;
        case OwnerAiProductionOrderActionCode::issue_order:
            if (selection.producer != nullptr) {
                if (input.issue_order != nullptr) {
                    input.issue_order(state, *selection.producer, order_id,
                        input.user_data);
                }
                input.production_state->owner_primary_resources[owner_slot] -=
                    selection.primary_cost;
                ++result.issued_order_count;
            }
            break;
        case OwnerAiProductionOrderActionCode::unavailable:
        case OwnerAiProductionOrderActionCode::no_ready_producer:
            ++result.unavailable_order_count;
            break;
        case OwnerAiProductionOrderActionCode::none:
        default:
            break;
        }
    }

    return result;
}

bool ImportOwnerAiSnapshot(OwnerAiRuntimeState& state, const u8* bytes, u32 byte_count) {
    if (bytes == nullptr || byte_count > kOwnerAiSnapshotByteCount) {
        return false;
    }

    state.snapshot_bytes.fill(0);
    std::memcpy(state.snapshot_bytes.data(), bytes, byte_count);
    hydrate_owner_ai_runtime_from_snapshot(state);
    return true;
}

bool ExportOwnerAiSnapshot(const OwnerAiRuntimeState& state, u8* bytes, u32 byte_count) {
    if (bytes == nullptr || byte_count < kOwnerAiSnapshotByteCount) {
        return false;
    }

    std::array<u8, kOwnerAiSnapshotByteCount> snapshot = state.snapshot_bytes;
    overlay_owner_ai_runtime_on_snapshot(state, snapshot);
    std::memcpy(bytes, snapshot.data(), snapshot.size());
    return true;
}

}
