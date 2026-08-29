#pragma once

#include "ranker_player_slots.h"
#include "ranker_types.h"
#include "ranker_unit_movement.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

// v2: per-owner true start positions replaced by anonymous map start
// candidates + explicit own start (fog-honest opening knowledge).
constexpr u32 kAiObservationSchemaVersion = 2;

// Number of research/upgrade orders whose completion level is surfaced in the
// observation (Tyrano MVP: harvest, movement, ground-attack upgrades).
constexpr std::size_t kAiObservationTrackedResearchCount = 3;
// Full production-order level table (order ids are < 0x40 in the catalog).
constexpr std::size_t kAiObservationResearchOrderCount = 64;

using AiUnitVisibilityCallback = bool (*)(const UnitMovementUnit& unit,
    u32 local_owner, void* user_data);

struct AiObservedMapTile {
    u32 terrain_flags = 0;
    // Harvestable amount under fog-of-war rules: the live value while the
    // tile is currently visible, the last-seen snapshot while it is merely
    // explored (when the caller supplies resource_memory), and 0 when
    // unexplored or never actually seen.  A stale snapshot self-corrects the
    // moment any own unit re-lights the tile.
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
    // Unit-type public stats (a unit's type is public knowledge, so these are
    // filled for visible opponents too).  Pixels; base definition values
    // (research/equipment bonuses not applied).
    u32 attack_range = 0;
    u32 sight_range = 0;
    u32 transport_capacity = 0;

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
    // The map's start-position CANDIDATES (fair opening knowledge: knowing
    // the map's start slots is public, knowing WHICH one an opponent occupies
    // is not — that must be scouted).  Anonymous and map-slot-ordered; the
    // observer's own start is included (identifiable by comparing against
    // start_x/start_y).  Replaces the per-owner true start positions the
    // observation used to leak.
    std::array<i32, 8> start_candidate_x{};
    std::array<i32, 8> start_candidate_y{};
    // Bit i set -> start_candidate_x/y[i] is a valid map start slot.
    u32 start_candidate_mask = 0;
    // Micro-executor summary (filled by the AI-play pump from the owner's
    // executor state before RL encoding; 0 when no executor drives the owner,
    // e.g. imitation logging of the built-in AI).  army_objective_kind =
    // AiMicroObjectiveKind + 1 (0 = none).
    u32 army_objective_kind = 0;
    u32 army_pulling_back = 0;
    // Unit id of the executor's scout (0 = none).
    u32 scout_unit_id = 0;
    // Fog-honest memory of enemy BUILDINGS: one byte per map tile (same
    // layout as `tiles`), 1 where a hostile building was last seen and the
    // tile has not been re-lit empty since.  Maintained per owner by the
    // AI-play pump; empty when no memory is kept.
    std::vector<u8> enemy_building_memory;
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
    // Map start candidates (see AiObservation::start_candidate_x).
    std::array<i32, 8> start_candidate_x{};
    std::array<i32, 8> start_candidate_y{};
    u32 start_candidate_mask = 0;
    // The observing owner's TRUE start position (its own — always fair).
    // Preferred over the legacy players->owner_start table, which is
    // MAP-SLOT-ordered and returns another slot's base once the start
    // shuffle decouples owners from map slots.
    i32 own_start_x = 0;
    i32 own_start_y = 0;
    bool own_start_valid = false;
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

    // Optional per-owner "last seen harvestable amount" memory (fog-honest
    // resource observation).  When supplied, the builder updates the entry
    // for every currently-visible tile from the live map and reports the
    // remembered value on explored-but-unwatched tiles — the live amount no
    // longer leaks through fog.  The buffer persists across frames on the
    // caller's side (one per observing owner) and is (re)sized to the map
    // tile count here; zero means "never actually seen".  When null, the
    // legacy relaxed behavior applies: explored tiles expose the live value.
    std::vector<u32>* resource_memory = nullptr;
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
