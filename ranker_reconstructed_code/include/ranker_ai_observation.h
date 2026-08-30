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
// v3: per-unit render class, weapon target-class mask and effective action
// ranges (the engine gates every attack on the attacker's damage profile vs
// the target's render class; the executor used to ignore both).
// v4: per-unit runtime state a raw-level policy needs and the engine already
// tracks — facing/animation phase, avatar level/experience (public: the
// overlays render them for any visible unit), and the controlled-only command
// lockout timers, effect timer and item/equipment slots.  Without these,
// observation logs recorded today cannot train raw micro later (the engine's
// "can this unit act now" state was unobservable).
constexpr u32 kAiObservationSchemaVersion = 4;

// Number of research/upgrade orders whose completion level is surfaced in the
// observation (Tyrano MVP: harvest, movement, ground-attack upgrades).
constexpr std::size_t kAiObservationTrackedResearchCount = 3;
// Full production-order level table (order ids are < 0x40 in the catalog).
constexpr std::size_t kAiObservationResearchOrderCount = 64;

using AiUnitVisibilityCallback = bool (*)(const UnitMovementUnit& unit,
    u32 local_owner, void* user_data);

// Weapon envelope of one unit, as the AI is allowed to know it.
struct AiUnitCombatProfile {
    // Effective action range (px) against a non-class-3 target and against a
    // render-class-3 (flying) target.  The engine resolves these separately
    // (`action_range_base` vs `action_range_base_vs_class3`).
    u32 attack_range = 0;
    u32 attack_range_vs_air = 0;
    // Bit i set -> this unit's weapon may engage a target whose render class
    // is i.  Mirrors the engine gate: the damage profile selected by
    // `action_profile_index` and its allowed_target_render_class_mask.
    // 0xffffffff when the profile is unknown (permissive, engine behavior for
    // an out-of-range profile index).
    u32 attackable_class_mask = 0xffffffffu;
};

// Optional hook that upgrades the base definition stats above into the
// engine's effective values.  The builder pre-fills `profile` from the raw
// definition and calls this only when it is supplied, so a caller without the
// runtime damage-profile / production / equipment tables (tests, tools) keeps
// the public type stats.  `controlled` is false for a visible opponent: fill
// research/equipment-derived values only for controlled units, otherwise the
// observation leaks the opponent's private upgrade state.
using AiUnitCombatProfileCallback = void (*)(const UnitMovementUnit& unit,
    bool controlled, AiUnitCombatProfile& profile, void* user_data);

struct AiObservedMapTile {
    u32 terrain_flags = 0;
    // Harvestable amount.  Berry POSITIONS and INITIAL amounts are map data
    // and therefore public (like terrain and the start candidates): with a
    // per-owner resource_memory the value is the live amount while the tile
    // is visible, else the last-seen snapshot, else - never seen - the map's
    // initial amount.  Only depletion is fog-honest.  Orders (harvest, build)
    // on an unexplored tile are still refused by the planner.  Legacy callers
    // without a memory keep explored-reveals-live / unexplored = 0.
    u32 resource_amount = 0;
    // Ground-walkable per the engine's movement-class-0 entry rule
    // (legacy_movement_class_can_enter_cell), static part: terrain class 0
    // and the decoration-layer entry bits of alternate_flags.  Berry tiles
    // (terrain class 0x100 - the engine's misnamed "PassableTerrain") are
    // NOT walkable; they are reported by resource_amount.  Building
    // footprints (dynamic) are not folded in.  Flyers ignore this.
    bool passable = false;
    bool explored = false;
    bool visible = false;
    // A structure may be placed here as far as the STATIC terrain goes
    // (engine placement rule: terrain class 0 and the placement-valid brush
    // flag).  Footprint occupancy is dynamic and left to the live validator.
    // v7 (appended last so positional initialisers stay valid).
    bool buildable = false;
    // Engine placement terrain class of the tile (alternate_flags bits
    // 26..28): a structure's whole footprint must share the class of its
    // anchor tile.  Static public terrain.  v7.
    u32 placement_class = 0;
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
    // filled for visible opponents too).  Pixels.  For CONTROLLED units these
    // are the engine's effective values (variant/research/equipment applied);
    // for a visible opponent they stay at the public base definition values,
    // because the opponent's upgrade state is private.
    u32 attack_range = 0;
    // Effective range against a render-class-3 (flying) target: the engine
    // resolves it from `action_range_base_vs_class3`, a different stat.
    u32 attack_range_vs_air = 0;
    // Raw definition range, never upgraded.  Role classification (melee vs
    // ranged) must use this: deriving it from the effective range would let a
    // range upgrade silently reclassify a melee unit mid-match.
    u32 attack_range_base = 0;
    u32 sight_range = 0;
    u32 transport_capacity = 0;
    // Movement speed of the unit's TYPE (public): the engine advances
    // `movement_step_limit` px every `movement_period` ticks
    // (UnitMovementDefinition).  Speed ~ step_limit / period.  v7.
    u32 movement_step_limit = 0;
    u32 movement_period = 1;
    // Render class of the unit's TYPE (public).  The engine's attack gate
    // tests this against the attacker's allowed target-class mask.
    u32 render_class = 0;
    // Bit i set -> this unit's weapon may engage render class i.  Ordering a
    // unit onto a target outside this mask is rejected by the engine, which
    // leaves the unit idle (see AiMicroExecutorStep's re-issue rule).
    u32 attackable_class_mask = 0xffffffffu;
    // v4 public runtime state, filled for visible opponents too because the
    // renderer draws all of it: sprite facing (engine `direction`), animation
    // phase, and the avatar level system.  `level` mirrors engine
    // `status_timer` (the HUD displays level + 1) and `experience` mirrors
    // `elite_progress_value`; both appear in the selection overlays for any
    // selected unit, so they are public knowledge like health.
    u32 direction = 0;
    u32 animation_frame = 0;
    u32 animation_timer = 0;
    u32 level = 0;
    u32 experience = 0;

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
    // v4 controlled-only runtime state.  The lockout ticks are the engine's
    // command-gating timers ("can this unit accept/execute an order right
    // now") — the readiness signal raw micro-control needs, invisible to an
    // opponent.  effect_timer is the internal work/completion-effect pacing.
    // Items and equipment are private inventory.
    u32 command_entry_lockout_ticks = 0;
    u32 command_lockout_ticks = 0;
    u32 effect_timer = 0;
    u32 equipment_flags = 0;
    std::array<u32, 4> item_slots{};
    std::array<u32, 6> equipment_slots{};
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
    // AiMicroAttackTactic of the army's attack objective (0 units_first,
    // 1 buildings_first, 2 neutral_only); meaningful when the kind is attack.
    u32 army_attack_tactic = 0;
    u32 army_pulling_back = 0;
    // Unit id of the executor's scout (0 = none).
    u32 scout_unit_id = 0;
    // Unit id of the executor's berry scout - the unit lighting the next
    // expansion site (0 = none).  v7.
    u32 berry_scout_unit_id = 0;
    // Executor explorer (frontier sweep) and roamer (random patrol of ground
    // outside active vision) unit ids (0 = none).  v7.
    u32 explorer_unit_id = 0;
    u32 roamer_unit_id = 0;
    // The owner's most recent REFUSED build order (the engine's placement
    // gate said no - information the owner receives, like a human's "cannot
    // build here"): structure type and frames since (0xffffffff = none).
    // The encoder backs the refused structure type off for a while, since a
    // fog-hidden unit on the site is invisible to the observation.  v7.
    u32 last_build_reject_type = 0;
    u32 last_build_reject_frames_ago = 0xffffffffu;
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

    // Optional effective-combat-stat hook (see AiUnitCombatProfileCallback).
    // Null -> every unit reports its raw definition stats.
    AiUnitCombatProfileCallback unit_combat_profile = nullptr;
    void* unit_combat_profile_user_data = nullptr;

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
