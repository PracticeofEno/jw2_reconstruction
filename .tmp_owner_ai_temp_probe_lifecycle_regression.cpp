#include "ranker_reconstructed_code/src/ranker_winmain.cpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace ranker;

UnitMovementDefinition g_probe_placement_definition{};
UnitMovementDefinition g_probe_movement_definition{};
UnitMovementDefinition g_existing_definition{};
bool g_accept_placement = true;
std::vector<UnitMovementUnit*> g_pathfinder_units;
u32 g_actual_type0_population_cost = 0xffffffffu;
u32 g_actual_type7_population_cost = 0xffffffffu;
u32 g_actual_type7_primary_cost = 0xffffffffu;
u32 g_actual_type7_secondary_cost = 0xffffffffu;
u32 g_actual_type7_lifecycle_class = 0xffffffffu;
u32 g_actual_type7_movement_class = 0xffffffffu;
u32 g_actual_type7_footprint_width = 0xffffffffu;
u32 g_actual_type7_footprint_height = 0xffffffffu;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

const UnitMovementDefinition* find_definition(
    UnitLifecycleContext&, u32 type_id) {
    switch (type_id) {
    case kDefaultOwnerAiTemporaryPathProbePlacementType:
        return &g_probe_placement_definition;
    case kDefaultOwnerAiTemporaryPathProbeMovementType:
        return &g_probe_movement_definition;
    case 5:
        return &g_existing_definition;
    default:
        return nullptr;
    }
}

bool place_probe(UnitLifecycleContext&, UnitMovementUnit&, i32&, i32&) {
    return g_accept_placement;
}

u32 zero_random(UnitLifecycleContext&, u32) {
    return 0;
}

bool record_failed_pathfinder(UnitMovementContext&, UnitMovementUnit& unit) {
    g_pathfinder_units.push_back(&unit);
    return false;
}

void configure_definitions() {
    g_probe_placement_definition = {};
    g_probe_placement_definition.production_resource_cost =
        g_actual_type7_primary_cost;
    g_probe_placement_definition.production_secondary_cost =
        g_actual_type7_secondary_cost;
    g_probe_placement_definition.production_population_cost =
        g_actual_type7_population_cost;

    g_probe_movement_definition = {};
    g_probe_movement_definition.production_population_cost =
        g_actual_type0_population_cost;

    g_existing_definition = {};
    g_existing_definition.production_population_cost = 3;
}

u32 read_catalog_u32(const std::vector<u8>& bytes, std::size_t offset) {
    u32 value = 0;
    if (offset + sizeof(value) <= bytes.size()) {
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    }
    return value;
}

void read_actual_probe_catalog_costs() {
    char previous_directory[MAX_PATH]{};
    require(GetCurrentDirectoryA(MAX_PATH, previous_directory) != 0,
        "capture current directory");
    require(SetCurrentDirectoryA("RankerOCPV_Win") != FALSE,
        "enter original data directory");
    require(LoadUnitDefinitionResourceCatalog(),
        "load original JW2_09 unit catalog");
    const UnitDefinitionResourceCatalogState& catalog =
        unit_definition_resource_catalog_state();
    require(catalog.records[0].loaded && catalog.records[7].loaded,
        "type-0/type-7 catalog records loaded");
    g_actual_type0_population_cost = read_catalog_u32(
        catalog.records[0].definition_bytes, 0x184);
    g_actual_type7_population_cost = read_catalog_u32(
        catalog.records[7].definition_bytes, 0x184);
    g_actual_type7_primary_cost = read_catalog_u32(
        catalog.records[7].definition_bytes, 0x190);
    g_actual_type7_secondary_cost = read_catalog_u32(
        catalog.records[7].definition_bytes, 0x194);
    g_actual_type7_lifecycle_class = read_catalog_u32(
        catalog.records[7].definition_bytes, 0x14c);
    g_actual_type7_movement_class = read_catalog_u32(
        catalog.records[7].definition_bytes, 0x17c);
    g_actual_type7_footprint_width = read_catalog_u32(
        catalog.records[7].definition_bytes, 0x330);
    g_actual_type7_footprint_height = read_catalog_u32(
        catalog.records[7].definition_bytes, 0x334);
    require(SetCurrentDirectoryA(previous_directory) != FALSE,
        "restore current directory");
}

void test_success_two_pass_release_is_balanced() {
    configure_definitions();
    g_accept_placement = true;
    g_pathfinder_units.clear();

    UnitMovementContext movement{};
    movement.callbacks.run_pathfinder = record_failed_pathfinder;

    UnitMovementUnit existing{};
    existing.id = 41;
    existing.type_id = 5;
    existing.owner_id = 1;
    existing.definition = g_existing_definition;
    existing.active = true;

    UnitMovementUnit lifecycle_sentinel{};
    lifecycle_sentinel.id = 42;

    UnitMovementUnit probe_slot{};
    probe_slot.id = 77;
    probe_slot.runtime_slot_index = 123;
    probe_slot.next_path_x = 37;
    probe_slot.next_path_y = 41;

    UnitMovementUnit free_tail{};
    free_tail.id = 78;

    movement.active_units = {&existing};
    movement.lifecycle_units = {&lifecycle_sentinel};
    movement.free_units = {&probe_slot, &free_tail};

    UnitLifecycleContext lifecycle{};
    lifecycle.movement = &movement;
    lifecycle.callbacks.find_definition = find_definition;
    lifecycle.callbacks.find_placement = place_probe;
    lifecycle.callbacks.random_limit = zero_random;
    lifecycle.owner_unit_active_count[0] = 4;
    lifecycle.owner_unit_score[0] = 100;
    lifecycle.owner_unit_type_counts[0]
        [kDefaultOwnerAiTemporaryPathProbePlacementType] = 2;

    HandleOwnerPopulationReservationTotals(lifecycle);
    require(lifecycle.owner_population_reserved[0] == 0 &&
            lifecycle.owner_population_reserved[1] == 3,
        "baseline population totals");

    g_runtime.gameplay_startup_state.lifecycle = &lifecycle;
    DefaultOwnerAiTemporaryPathProbeLease lease;
    require(default_owner_ai_acquire_temporary_path_probe(
            &lifecycle, {320, 416}, lease),
        "probe acquisition");
    require(lease.lifecycle == &lifecycle && lease.unit == &probe_slot,
        "lease points at consumed free-list head");
    require(probe_slot.id == 77 && probe_slot.runtime_slot_index == 123,
        "pool identity survives placement initialization");
    require(probe_slot.type_id ==
            kDefaultOwnerAiTemporaryPathProbeMovementType &&
            probe_slot.owner_id == 0 && probe_slot.active,
        "active owner-0 type-0 scratch state");
    require(movement.active_units.size() == 2 &&
            movement.active_units[0] == &probe_slot &&
            movement.active_units[1] == &existing,
        "free head activated once at active-list head");
    require(movement.free_units.size() == 1 &&
            movement.free_units[0] == &free_tail,
        "free-list head consumed exactly once");
    require(movement.lifecycle_units.size() == 1 &&
            movement.lifecycle_units[0] == &lifecycle_sentinel,
        "unrelated lifecycle list preserved on acquire");
    require(lifecycle.owner_unit_active_count[0] == 5 &&
            lifecycle.owner_unit_score[0] ==
                100 + g_actual_type7_primary_cost +
                    g_actual_type7_secondary_cost &&
            lifecycle.owner_unit_type_counts[0]
                [kDefaultOwnerAiTemporaryPathProbePlacementType] == 3,
        "placement accounting applied once before type swap");

    HandleOwnerPopulationReservationTotals(lifecycle);
    require(lifecycle.owner_population_reserved[0] ==
                g_actual_type0_population_cost &&
            lifecycle.owner_population_reserved[1] == 3,
        "mid-lease population sees the type-0 scratch exactly once");

    (void)ProbeOwnerAiRoutePath(movement,
        static_cast<const UnitMovementUnit*>(lease.unit),
        {640, 640}, {96, 96});
    (void)ProbeOwnerAiRoutePath(movement,
        static_cast<const UnitMovementUnit*>(lease.unit),
        {128, 160}, {704, 736});
    require(g_pathfinder_units.size() == 2 &&
            g_pathfinder_units[0] != &probe_slot &&
            g_pathfinder_units[1] != &probe_slot,
        "both path passes use detached typed mirrors");
    require(probe_slot.next_path_x == 37 && probe_slot.next_path_y == 41,
        "path probes preserve live fixed-pool next-path payload");
    require(movement.active_units.size() == 2 &&
            movement.free_units.size() == 1,
        "path passes do not alter active/free membership");

    default_owner_ai_release_temporary_path_probe(lease);
    require(lease.lifecycle == nullptr && lease.unit == nullptr &&
            lease.cleanup_definition == nullptr,
        "release clears lease");
    require(movement.active_units.size() == 1 &&
            movement.active_units[0] == &existing,
        "scratch removed from active list exactly once");
    require(movement.free_units.size() == 2 &&
            movement.free_units[0] == &probe_slot &&
            movement.free_units[1] == &free_tail,
        "scratch returned once to original free-list head");
    require(probe_slot.next_path_x == 37 && probe_slot.next_path_y == 41,
        "scratch release restores fixed-pool next-path payload");
    require(movement.lifecycle_units.size() == 1 &&
            movement.lifecycle_units[0] == &lifecycle_sentinel,
        "unrelated lifecycle list preserved on release");
    require(lifecycle.owner_unit_active_count[0] == 4 &&
            lifecycle.owner_unit_score[0] == 100 &&
            lifecycle.owner_unit_type_counts[0]
                [kDefaultOwnerAiTemporaryPathProbePlacementType] == 2,
        "active/score/type accounting restored exactly");

    HandleOwnerPopulationReservationTotals(lifecycle);
    require(lifecycle.owner_population_reserved[0] == 0 &&
            lifecycle.owner_population_reserved[1] == 3,
        "population totals restore after release");

    // A cleared lease must make accidental duplicate cleanup harmless.
    default_owner_ai_release_temporary_path_probe(lease);
    require(movement.active_units.size() == 1 &&
            movement.free_units.size() == 2 &&
            lifecycle.owner_unit_active_count[0] == 4 &&
            lifecycle.owner_unit_score[0] == 100,
        "second release is an accounting/list no-op");
    g_runtime.gameplay_startup_state.lifecycle = nullptr;
}

void test_failed_placement_restores_free_head() {
    configure_definitions();
    g_accept_placement = false;

    UnitMovementContext movement{};
    UnitMovementUnit probe_slot{};
    UnitMovementUnit free_tail{};
    probe_slot.id = 91;
    free_tail.id = 92;
    movement.free_units = {&probe_slot, &free_tail};

    UnitLifecycleContext lifecycle{};
    lifecycle.movement = &movement;
    lifecycle.callbacks.find_definition = find_definition;
    lifecycle.callbacks.find_placement = place_probe;
    lifecycle.callbacks.random_limit = zero_random;
    lifecycle.owner_unit_active_count[0] = 6;
    lifecycle.owner_unit_score[0] = 200;

    g_runtime.gameplay_startup_state.lifecycle = &lifecycle;
    DefaultOwnerAiTemporaryPathProbeLease lease;
    require(!default_owner_ai_acquire_temporary_path_probe(
            &lifecycle, {320, 416}, lease),
        "rejected placement must fail acquisition");
    require(lease.lifecycle == nullptr && lease.unit == nullptr,
        "failed acquisition leaves no lease");
    require(movement.active_units.empty() &&
            movement.lifecycle_units.empty() &&
            movement.free_units.size() == 2 &&
            movement.free_units[0] == &probe_slot &&
            movement.free_units[1] == &free_tail,
        "failed placement restores exact free-list order");
    require(lifecycle.owner_unit_active_count[0] == 6 &&
            lifecycle.owner_unit_score[0] == 200,
        "failed placement performs no accounting");
    g_runtime.gameplay_startup_state.lifecycle = nullptr;
}

} // namespace

int main() {
    read_actual_probe_catalog_costs();
    test_success_two_pass_release_is_balanced();
    test_failed_placement_restores_free_head();
    std::cout << "OWNER_AI_TEMP_PROBE_LIFECYCLE_PASS"
              << " acquire=1 two_pass_same_slot=1 release=1"
              << " score=balanced population=balanced"
              << " active_free=balanced failure_restore=1"
              << " actual_type0_pop=" << g_actual_type0_population_cost
              << " actual_type7_pop=" << g_actual_type7_population_cost
              << " actual_type7_primary=" << g_actual_type7_primary_cost
              << " actual_type7_secondary=" << g_actual_type7_secondary_cost
              << " actual_type7_lifecycle=" << g_actual_type7_lifecycle_class
              << " actual_type7_movement=" << g_actual_type7_movement_class
              << " actual_type7_footprint=" << g_actual_type7_footprint_width
              << 'x' << g_actual_type7_footprint_height
              << '\n';
    return EXIT_SUCCESS;
}
