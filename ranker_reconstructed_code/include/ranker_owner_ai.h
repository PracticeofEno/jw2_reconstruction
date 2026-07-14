#pragma once

#include "ranker_player_slots.h"
#include "ranker_production_orders.h"
#include "ranker_types.h"
#include "ranker_unit_movement.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kOwnerAiOwnerCount = kPlayerSlotCount;
constexpr u32 kOwnerAiCommandDwordCount = 0x2c / sizeof(i32);
constexpr u32 kOwnerAiUnitTypeCount = 0xaa;
constexpr u32 kOwnerAiSharedCounterDwordCount = 0xc0 / sizeof(u32);
constexpr u32 kOwnerAiSharedPlannerDwordCount = 0x5800 / sizeof(u32);
constexpr u32 kOwnerAiSharedGridDwordCount = 0x800 / sizeof(u32);
constexpr u32 kOwnerAiThreatPointCount = 4;
constexpr u32 kOwnerAiPlacementRecordDwordCount = 0x30 / sizeof(i32);
constexpr u32 kOwnerAiSnapshotByteCount = 0x9d80;
constexpr u32 kOwnerAiProfileLabelCapacity = 100;
constexpr u32 kOwnerAiProfileLabelNameLength = 0x20;
constexpr u32 kOwnerAiCounterRuleFactionCount = 4;
constexpr u32 kOwnerAiCounterRuleUnitTypeCount = 0x60;
constexpr u32 kOwnerAiStrategicPressureDistance = 0x280;
constexpr u32 kOwnerAiPopulationRetargetReserve = 5;
constexpr i32 kOwnerAiInvalidProfileRecord = -1;

struct OwnerAiCommandRecord {
    std::array<i32, kOwnerAiCommandDwordCount> words{};
};

struct OwnerAiPoint {
    i32 x = 0;
    i32 y = 0;
};

struct OwnerAiProfileLabel {
    std::array<char, kOwnerAiProfileLabelNameLength> name{};
    u32 command_index = 0;
};

struct OwnerAiEligibleUnitSummary {
    u32 count = 0;
    i32 weight = 0;
};

struct OwnerAiStrategicPressureSummary {
    u32 count = 0;
    u32 weight = 0;
};

struct OwnerAiRoutePathProbeResult {
    bool attempted = false;
    bool reachable = false;
    UnitMovementPoint start{};
    UnitMovementPoint destination{};
    UnitMovementPoint start_tile{};
    UnitMovementPoint destination_tile{};
    UnitMovementPoint final_path_target{};
    UnitMovementPoint next_path_point{};
    UnitMovementPoint final_path_target_tile{};
    UnitMovementPoint next_path_tile{};
    std::vector<UnitMovementPoint> path_tiles;
    u32 path_cost = 0;
    bool path_target_adjusted = false;
    bool direct_path = false;
};

struct OwnerAiCounterUnitRule {
    i32 unit_type = -1;
    u32 percent_bonus = 0;
};

using OwnerAiCounterRuleTable = std::array<
    std::array<std::array<OwnerAiCounterUnitRule, 3>,
        kOwnerAiCounterRuleUnitTypeCount>,
    kOwnerAiCounterRuleFactionCount>;

struct OwnerAiSlotRuntime {
    u32 script_halted = 0;
    u32 primary_interval = 10;
    u32 primary_radius = 0x46;
    u32 primary_budget = 0;
    i32 primary_target_owner = -1;
    u32 route_load_percent = 10;
    u32 support_interval = 10;
    u32 support_radius = 0x46;
    i32 support_target_owner = -1;
    u32 support_mode = 0;
    u32 support_budget = 0;
    i32 support_anchor = -1;
    i32 support_anchor_y = 0;
    u32 secondary_budget = 0;
    u32 resource_budget_percent = 0x7d;
    u32 secondary_mode = 0;
    u32 profile_counter = 0;
    std::array<u32, kOwnerAiUnitTypeCount> unit_demand{};
    std::array<u32, kOwnerAiUnitTypeCount> unit_demand_shadow{};
    u32 route_refresh_counter = 0;
    u32 script_cycle_counter = 0;
    i32 previous_script_cycle_counter = -1;
    u32 script_enabled = 1;
    u32 last_timing_frame = 0;
    u32 build_budget = 0;
    u32 production_budget = 0;
    u32 rally_delay = 0x78;
    u32 reserve_budget = 0;
    u32 reserve_delay = 0x78;
    u32 strategic_retarget_quota_floor = 0xffffffffu;
    u32 route_target_score = 100;
    u32 profile_gate_flag = 0;
    u32 production_pause_flag = 0;
    u32 route_radius = 0x46;
    OwnerAiPoint primary_target_point{};
    u32 primary_target_radius = 0x14;
    u32 primary_target_flags = 0;
    OwnerAiPoint neutral_route_target_point{};
    u32 placement_radius = 0x14;
    std::array<i32, kOwnerAiPlacementRecordDwordCount> placement_record{};
    u32 placement_target_radius = 0x14;
    std::array<OwnerAiPoint, kOwnerAiThreatPointCount> threat_points{};
    u32 attack_interval = 10;
    u32 attack_radius = 0x46;
    u32 attack_target_radius = 0x14;
    i32 attack_target_owner = -1;
    i32 support_target_slot = -1;
    i32 fallback_target_slot = -1;
    u32 profile_state_flag = 0;
    u32 profile_age = 0;
};

struct OwnerAiRuntimeState;

using OwnerAiMaintenanceCallback = void (*)(
    OwnerAiRuntimeState& state, u32 owner_slot, void* user_data);

struct OwnerAiMaintenanceCallbacks {
    OwnerAiMaintenanceCallback rebuild_route_targets = nullptr;
    OwnerAiMaintenanceCallback refresh_placement_anchors = nullptr;
    OwnerAiMaintenanceCallback maintain_transport_route_targets = nullptr;
    OwnerAiMaintenanceCallback process_production_orders = nullptr;
    OwnerAiMaintenanceCallback process_production_demand = nullptr;
    OwnerAiMaintenanceCallback retarget_strategic_queue = nullptr;
    OwnerAiMaintenanceCallback dispatch_threat_points = nullptr;
    OwnerAiMaintenanceCallback maintain_transport_queue = nullptr;
};

using OwnerAiProfileLoadCallback = bool (*)(
    const char* archive_name,
    u32 record_index,
    u32 owner_slot,
    std::vector<OwnerAiCommandRecord>& commands,
    void* user_data);
using OwnerAiProfileTextLoadCallback = bool (*)(
    const char* archive_name,
    u32 record_index,
    u32 owner_slot,
    std::string& profile_text,
    void* user_data);
using OwnerAiCommandHandler = void (*)(
    OwnerAiRuntimeState& state,
    u32 owner_slot,
    OwnerAiCommandRecord& command,
    u32& command_index,
    void* user_data);
using OwnerAiEligibleUnitSummaryCallback = bool (*)(
    const OwnerAiRuntimeState& state,
    u32 owner_slot,
    OwnerAiEligibleUnitSummary& summary,
    void* user_data);
using OwnerAiRandomCallback = u32 (*)(
    OwnerAiRuntimeState& state,
    u32 seed,
    void* user_data);
using OwnerAiUnitPredicate = bool (*)(const UnitMovementUnit& unit,
    void* user_data);
using OwnerAiUnitWeightCallback = u32 (*)(const UnitMovementUnit& unit,
    void* user_data);
using OwnerAiStrategicQueueRetargetCallback = void (*)(
    OwnerAiRuntimeState& state, u32 owner_slot, void* user_data);

struct OwnerAiStrategicRetargetGateInput {
    const UnitMovementContext* movement = nullptr;
    const OwnerAiCounterRuleTable* counter_rules = nullptr;
    std::array<const UnitMovementUnit*, kOwnerAiOwnerCount> strategic_route_targets{};
    std::array<u32, kOwnerAiOwnerCount> owner_population_used{};
    std::array<u32, kOwnerAiOwnerCount> owner_population_limit{};
    u32 owner_phase_state = 1;
    OwnerAiUnitPredicate pressure_unit_eligible = nullptr;
    OwnerAiUnitWeightCallback unit_weight = nullptr;
    OwnerAiStrategicQueueRetargetCallback retarget_queue = nullptr;
    void* user_data = nullptr;
};

enum class OwnerAiProductionOrderActionCode : u32 {
    none = 0,
    reserve_primary_cost = 1,
    demand_primary_unit = 2,
    demand_prerequisite_unit = 3,
    unavailable = 4,
    no_ready_producer = 5,
    issue_order = 6,
};

using OwnerAiProductionProducerPredicate = bool (*)(
    const UnitMovementUnit& unit, u32 order_id, void* user_data);
using OwnerAiProductionQueuedCountCallback = u32 (*)(
    const OwnerAiRuntimeState& state, u32 owner_slot, u32 unit_type,
    void* user_data);
using OwnerAiProductionIssueOrderCallback = void (*)(
    OwnerAiRuntimeState& state, UnitMovementUnit& producer, u32 order_id,
    void* user_data);

struct OwnerAiProductionOrderPlanningInput {
    const UnitMovementContext* movement = nullptr;
    const ProductionOrderCatalog* catalog = nullptr;
    ProductionOrderRuntimeState* production_state = nullptr;
    std::array<u32, kOwnerAiCounterRuleFactionCount> faction_primary_unit_types{
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    std::array<u32, kOwnerAiCounterRuleFactionCount> faction_opening_order_ids{
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    OwnerAiProductionProducerPredicate producer_ready = nullptr;
    OwnerAiProductionQueuedCountCallback queued_extended_count = nullptr;
    OwnerAiProductionIssueOrderCallback issue_order = nullptr;
    u32 initial_reserved_primary_cost = 0;
    void* user_data = nullptr;
};

struct OwnerAiProductionOrderSelection {
    OwnerAiProductionOrderActionCode action = OwnerAiProductionOrderActionCode::none;
    u32 order_id = 0;
    u32 variant = 0;
    u32 primary_cost = 0;
    u32 missing_unit_type = 0xffffffffu;
    UnitMovementUnit* producer = nullptr;
};

struct OwnerAiProductionOrderPlanResult {
    u32 reserved_primary_cost = 0;
    u32 issued_order_count = 0;
    u32 raised_unit_demand_count = 0;
    u32 unavailable_order_count = 0;
};

struct OwnerAiRuntimeState {
    std::array<OwnerAiSlotRuntime, kOwnerAiOwnerCount> owners{};
    std::array<i32, kOwnerAiOwnerCount> profile_record_indices{};
    std::array<std::vector<OwnerAiCommandRecord>, kOwnerAiOwnerCount> profile_commands{};
    std::array<OwnerAiProfileLabel, kOwnerAiProfileLabelCapacity> profile_labels{};
    std::array<u32, kOwnerAiOwnerCount> owner_faction_ids{};
    std::array<std::array<u32, kOwnerAiUnitTypeCount>, kOwnerAiOwnerCount>
        owner_unit_type_counts{};
    u32 random_seed = 0;
    u32 random_call_count = 0;
    std::array<u32, 16> random_scramble{
        0xa075a321u, 0xb304323cu, 0xc43a2059u, 0xd3d4f745u,
        0xe9999996u, 0xf124e654u, 0x158c6670u, 0x28374832u,
        0x3a576385u, 0x4748e52du, 0x54302323u, 0x696d376fu,
        0x7a323d17u, 0x81c97674u, 0x99a213eeu, 0xa4020f23u};
    std::array<u32, kOwnerAiSharedCounterDwordCount> shared_counter_table0{};
    std::array<u32, kOwnerAiSharedCounterDwordCount> shared_counter_table1{};
    std::array<u32, kOwnerAiSharedCounterDwordCount> shared_counter_table2{};
    std::array<u32, kOwnerAiSharedPlannerDwordCount> shared_planner_table{};
    std::array<u32, kOwnerAiSharedGridDwordCount> shared_grid_table{};
    std::array<u32, kPlayerOwnerResourceSlots> owner_population_used{};
    std::array<u32, kPlayerOwnerResourceSlots> owner_population_reserved{};
    std::array<u8, kOwnerAiSnapshotByteCount> snapshot_bytes{};
    bool skirmish_profile_mode = false;
    bool network_profile_override = false;
    bool scenario_profile_override = false;
    u32 session_mode = 0;
    u32 selected_faction = 0;
    u32 frame_counter = 0;
    bool parser_populate_records = false;
    u32 parser_owner_slot = 0;
    u32 parser_command_count = 0;
    u32 profile_label_count = 0;
    OwnerAiProfileLoadCallback load_profile = nullptr;
    void* load_profile_user_data = nullptr;
    OwnerAiProfileTextLoadCallback load_profile_text = nullptr;
    void* load_profile_text_user_data = nullptr;
    OwnerAiCommandHandler command_handler = nullptr;
    void* command_handler_user_data = nullptr;
    OwnerAiEligibleUnitSummaryCallback eligible_unit_summary = nullptr;
    void* eligible_unit_summary_user_data = nullptr;
    OwnerAiRandomCallback random_value = nullptr;
    void* random_value_user_data = nullptr;
    const PlayerSlotRuntimeState* command_player_slots = nullptr;
};

OwnerAiRuntimeState& owner_ai_runtime_state();
const OwnerAiCounterRuleTable& DefaultOwnerAiCounterRuleTable();
void ResetOwnerAiRuntime(OwnerAiRuntimeState& state);
bool LoadOwnerAiTargetProfile(OwnerAiRuntimeState& state, const char* archive_name,
    u32 record_index, u32 owner_slot);
void BeginOwnerAiProfileParsePass(OwnerAiRuntimeState& state, u32 owner_slot,
    bool populate_records);
bool RecordOwnerAiProfileLabel(OwnerAiRuntimeState& state, const char* marker,
    u32 command_index);
i32 ResolveOwnerAiProfileLabel(OwnerAiRuntimeState& state, const char* label);
void AppendOwnerAiCommandRecord4(OwnerAiRuntimeState& state, i32 command_id,
    i32 arg0, i32 arg1, i32 arg2);
void AppendOwnerAiCommandRecord9(OwnerAiRuntimeState& state, i32 command_id,
    i32 arg0, i32 arg1, i32 arg2, i32 arg3, i32 arg4, i32 arg5, i32 arg6,
    i32 arg7);
bool ParseOwnerAiProfileCommandLine(OwnerAiRuntimeState& state, const char* line);
bool ParseOwnerAiProfileText(OwnerAiRuntimeState& state, u32 owner_slot,
    const char* profile_text, bool populate_records);
void RefreshOwnerTargetDataForSlot(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots, u32 owner_slot);
void ReloadSkirmishOwnerTargetProfiles(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots);
void ResetOwnerAiSlotRuntime(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots, u32 owner_slot);
void AdvanceOwnerAiCommandRecord(OwnerAiCommandRecord& command, u32& command_index);
u32 SelectOwnerAiRandomValue(OwnerAiRuntimeState& state, u32 limit);
void ApplyOwnerAiCommand(OwnerAiRuntimeState& state, u32 owner_slot,
    OwnerAiCommandRecord& command, u32& command_index);
void ApplyOwnerAiCommandRecord(OwnerAiRuntimeState& state, u32 owner_slot,
    OwnerAiCommandRecord& command, u32& command_index);
void TickOwnerAiCommandScript(OwnerAiRuntimeState& state, u32 owner_slot);
void TickOwnerAiMaintenance(OwnerAiRuntimeState& state,
    const PlayerSlotRuntimeState& player_slots,
    const OwnerAiMaintenanceCallbacks& callbacks = {},
    void* user_data = nullptr);
OwnerAiStrategicPressureSummary CalculateOwnerAiStrategicTargetPressureSummary(
    const OwnerAiRuntimeState& state, u32 owner_slot,
    const OwnerAiStrategicRetargetGateInput& input);
bool ShouldOwnerAiRunStrategicQueueRetarget(OwnerAiRuntimeState& state,
    u32 owner_slot, const OwnerAiStrategicRetargetGateInput& input);
bool TickOwnerAiStrategicQueueRetargetGate(OwnerAiRuntimeState& state,
    u32 owner_slot, const OwnerAiStrategicRetargetGateInput& input);
OwnerAiRoutePathProbeResult ProbeOwnerAiRoutePath(UnitMovementContext& movement,
    const UnitMovementUnit* reusable_unit, UnitMovementPoint start,
    UnitMovementPoint destination);
OwnerAiRoutePathProbeResult ProbeOwnerAiRoutePath(UnitMovementContext& movement,
    UnitMovementUnit& probe_unit, UnitMovementPoint start,
    UnitMovementPoint destination);
OwnerAiProductionOrderSelection SelectOwnerAiProductionOrderAction(
    OwnerAiRuntimeState& state, u32 owner_slot, u32 order_id,
    u32 reserved_primary_cost, const OwnerAiProductionOrderPlanningInput& input);
OwnerAiProductionOrderPlanResult ProcessOwnerAiProductionOrderRequests(
    OwnerAiRuntimeState& state, u32 owner_slot,
    const OwnerAiProductionOrderPlanningInput& input);
bool ImportOwnerAiSnapshot(OwnerAiRuntimeState& state, const u8* bytes, u32 byte_count);
bool ExportOwnerAiSnapshot(const OwnerAiRuntimeState& state, u8* bytes, u32 byte_count);

}
