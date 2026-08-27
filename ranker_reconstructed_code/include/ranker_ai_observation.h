#pragma once

#include "ranker_player_slots.h"
#include "ranker_types.h"
#include "ranker_unit_movement.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kAiObservationSchemaVersion = 1;

// Number of research/upgrade orders whose completion level is surfaced in the
// observation (Tyrano MVP: harvest, movement, ground-attack upgrades).
constexpr std::size_t kAiObservationTrackedResearchCount = 3;
// Full production-order level table (order ids are < 0x40 in the catalog).
constexpr std::size_t kAiObservationResearchOrderCount = 64;

using AiUnitVisibilityCallback = bool (*)(const UnitMovementUnit& unit,
    u32 local_owner, void* user_data);

struct AiObservedMapTile {
    u32 terrain_flags = 0;
    // Harvestable terrain is authoritative only while the tile is currently
    // visible. Zero therefore means either no remaining resource or no
    // current vision; policy code must also test visible.
    u32 resource_amount = 0;
    bool passable = false;
    bool explored = false;
    bool visible = false;
};

struct AiObservedQueuedCommand {
    u32 state = 0;
    i32 command_value_or_target = 0;
    i32 x_payload = 0;
    u32 y_payload = 0;
};

struct AiObservedUnit {
    u32 id = 0;
    u32 runtime_slot_index = kInvalidUnitRuntimeSlotIndex;
    u32 type_id = 0;
    u32 owner_id = 0;
    u32 type_flags = 0;
    i32 x = 0;
    i32 y = 0;
    u32 health = 0;
    u32 max_health = 0;
    u32 secondary_value = 0;
    u32 max_secondary_value = 0;
    bool controlled = false;
    bool visible = false;
    bool alive = false;
    bool under_construction = false;

    // These fields are intentionally populated only for controlled units.
    // Visible opponents do not expose their private destination, queue or
    // current target through the AI interface.
    u32 command_state = 0;
    u32 command_flags = 0;
    u32 command_value = 0;
    u32 action_mode = 0;
    u32 movement_state = 0;
    i32 destination_x = 0;
    i32 destination_y = 0;
    i32 path_target_x = 0;
    i32 path_target_y = 0;
    u32 target_id = 0;
    u32 cargo_amount = 0;
    u32 cargo_capacity = 0;
    u32 queued_production_type_id = 0;
    u32 production_variant = 0;
    u32 deferred_command_count = 0;
    std::vector<AiObservedQueuedCommand> deferred_commands;
};

struct AiObservation {
    u32 schema_version = kAiObservationSchemaVersion;
    u32 simulation_frame = 0;
    std::string map_relative_path;
    std::string map_sha256;
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
    u32 local_owner = 0;
    u32 local_faction = 0;
    u32 active_owner_mask = 0;
    u32 local_relation_mask = 0;
    u32 primary_resources = 0;
    u32 secondary_resources = 0;
    u32 auxiliary_resources = 0;
    u32 population_used = 0;
    u32 population_reserved = 0;
    u32 population_limit = 0;
    i32 start_x = 0;
    i32 start_y = 0;
    bool game_ended = false;
    u32 game_end_reason = 0;
    // Completed research/upgrade level for a small set of tracked orders (the
    // caller fills these from the per-owner order-upgrade table).  0 = not
    // researched.  Exposes tech progress the unit/building counts cannot.
    std::array<u8, kAiObservationTrackedResearchCount> research_levels{};
    // Completed level for EVERY production order id < 64 (the full per-owner
    // variant_counts row).  The executor's research cycle consults this so it
    // can run the whole audited research tree, not just the tracked trio.
    std::array<u8, kAiObservationResearchOrderCount> research_order_levels{};
    std::vector<AiObservedMapTile> tiles;
    std::vector<AiObservedUnit> units;
};

struct AiObservationBuildInput {
    u32 simulation_frame = 0;
    std::string map_relative_path;
    std::string map_sha256;
    u32 local_owner = 0;
    u32 local_faction = 0;
    u32 population_used = 0;
    u32 population_reserved = 0;
    u32 population_limit = 0;
    bool game_ended = false;
    u32 game_end_reason = 0;
    // Completed research/upgrade level per tracked order (see AiObservation).
    std::array<u8, kAiObservationTrackedResearchCount> research_levels{};
    // Full per-order level row (see AiObservation::research_order_levels).
    std::array<u8, kAiObservationResearchOrderCount> research_order_levels{};
    const PlayerSlotRuntimeState* players = nullptr;
    const UnitMovementContext* movement = nullptr;

    // The caller must project fog state into local-owner masks. Both vectors
    // use tightly packed row-major map tiles (width * height), not map stride.
    const std::vector<u8>* explored_tiles = nullptr;
    const std::vector<u8>* visible_tiles = nullptr;

    // Controlled units are always observable. Every other unit is excluded
    // unless this callback explicitly confirms current local visibility.
    AiUnitVisibilityCallback unit_visible = nullptr;
    void* unit_visibility_user_data = nullptr;
};

enum class AiObservationBuildCode : u32 {
    okay = 0,
    missing_players,
    missing_movement,
    invalid_local_owner,
    invalid_map_dimensions,
    invalid_map_storage,
    missing_explored_tiles,
    missing_visible_tiles,
    invalid_explored_tile_count,
    invalid_visible_tile_count,
};

struct AiObservationBuildResult {
    AiObservationBuildCode code = AiObservationBuildCode::missing_movement;
    AiObservation observation;

    explicit operator bool() const {
        return code == AiObservationBuildCode::okay;
    }
};

AiObservationBuildResult BuildAiObservationV1(
    const AiObservationBuildInput& input);
u64 HashAiObservationV1(const AiObservation& observation);

} // namespace ranker
