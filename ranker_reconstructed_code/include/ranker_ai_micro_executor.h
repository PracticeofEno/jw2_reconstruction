#pragma once

#include "ranker_ai_actions.h"
#include "ranker_ai_observation.h"

#include <array>
#include <vector>

namespace ranker {

// Group-objective micro executor (docs/AI_PLAY_MICRO_EXECUTOR_DESIGN.md).
//
// The RL policy only changes a GROUP's objective (every decision cycle).  This
// executor runs EVERY frame and computes, for each own unit, the default
// behavior that follows from (its group's objective, its own situation, the
// hostiles around it).  It never invents objectives: "when / what" is the
// policy's; "how" is this file's.  Orders are issued only when the desired
// order changed (re-issuing the same order each frame resets unit pathing).

enum class AiMicroGroup : u32 {
    economy = 0,  // workers
    army = 1,     // every other mobile fighter
    scout = 2,    // 0..1 units split off by scout_map (early-warning picket)
    // 0..1 units split off by scout_berry: walks to the next expansion site
    // to light it, evades like the picket, never fights; released back to
    // its default group once the site tile is explored.
    berry_scout = 3,
    // 0..1 units split off by explore_frontier: walks the exploration
    // frontier it can reach (air units reach everything); released when no
    // reachable frontier is left.
    explorer = 4,
    // 0..1 units split off by roam_scout: keeps walking to random reachable
    // ground outside the owner's active vision (a moving picket).
    roamer = 5,
    // v8: the detachable strike group.  Filled only by detach_raid (mobility-
    // first 30% of the main army), emptied by merge_raid or by dying; newly
    // produced fighters always default to `army`, so a spent raid must be
    // re-detached (an intended cost).  Holds its own objective, so the policy
    // can play main-army defend + raid attack at the same time.
    raid = 6,
    // v10 (user directive: run up to FOUR fighting bodies): two more raid
    // slots with the exact raid semantics - main army + raid + raid_b +
    // raid_c.  Each has its own detach/merge/objective actions.
    raid_b = 7,
    raid_c = 8,
};
constexpr std::size_t kAiMicroGroupCount = 9;

// The detachable fighting groups (everything the raid machinery applies to).
constexpr AiMicroGroup kAiMicroRaidGroups[3] = {AiMicroGroup::raid,
    AiMicroGroup::raid_b, AiMicroGroup::raid_c};
constexpr bool AiMicroIsRaidGroup(AiMicroGroup group) {
    return group == AiMicroGroup::raid || group == AiMicroGroup::raid_b ||
        group == AiMicroGroup::raid_c;
}

enum class AiMicroObjectiveKind : u32 {
    harvest = 0,
    attack,
    defend,
    retreat,
    scout,
    // Army sweep of the UNEXPLORED START CANDIDATES only (where an enemy
    // base can be).  Plain move, no engagement: whether to fight what the
    // sweep reveals is the policy's decision.  Stands when none is left.
    search,
    // Single-unit frontier sweep: the nearest unexplored passable tile
    // bordering explored ground that the unit can reach (air: anything).
    explore,
    // Single-unit random patrol: keeps picking a reachable passable tile
    // outside the owner's active vision (deterministic xorshift).
    roam,
};

// The attack TACTIC the policy chose.  It is an intent, not a value: the
// executor re-derives the group's current target from it every frame, so the
// policy's choice keeps applying after the first target dies instead of
// collapsing into "nearest visible hostile".
enum class AiMicroAttackTactic : u32 {
    units_first = 0,   // attack_nearest_enemy: enemy army, buildings if none
    buildings_first,   // attack_enemy_base: enemy buildings (visible, else the
                       // remembered location), enemy army if none in sight
    neutral_only,      // hunt_neutral_monster: neutral monsters only
};

struct AiMicroObjective {
    AiMicroObjectiveKind kind = AiMicroObjectiveKind::defend;
    // attack: how the group picks its target (set by the policy).
    AiMicroAttackTactic tactic = AiMicroAttackTactic::units_first;
    // attack: the group's CURRENT target unit (0 = none / point target).
    // Maintained by the executor from `tactic`; the policy never sets it.
    u32 target_unit_id = 0;
    // attack march point (executor-derived: remembered enemy building) /
    // search sweep point (executor-derived) / defend post / retreat
    // destination / scout post.  -1 = none.
    i32 target_x = -1;
    i32 target_y = -1;
    // v8 policy-chosen strike zone (attack only): the spatial-target head's
    // cell centre.  While set, target pick and march stay inside
    // preferred_zone_radius of it; cleared by the executor once the group
    // arrives there and finds nothing (falls back to the global chain).
    // -1 = none (v7 behavior).
    i32 preferred_x = -1;
    i32 preferred_y = -1;
    // defend: bubble radius around the post (px).
    i32 radius = 0;
    // search: frame the current sweep point was picked, and the rotating
    // exploration-cycle cursor used once the whole map is explored.
    u32 sweep_pick_frame = 0;
    u32 sweep_cursor = 0;
    // search: the current sweep point came from the exploration cycle (the
    // whole map is explored), so it is only replaced on arrival.
    bool sweep_cycle = false;
    u32 set_frame = 0;
    // False until something (policy or executor default) assigned it; the
    // executor fills unassigned objectives with the group default on its
    // first step, never overwriting one the policy already set.
    bool assigned = false;
};

enum class AiMicroUnitState : u32 {
    normal = 0,
    pulling_back,  // fighter under 30% hp leaving contact toward a nest
    fleeing,       // worker running from a hostile fighter
    evading,       // scout stepping away from a sighted hostile
    returning,     // defender walking back to its post (leash hysteresis)
};

enum class AiMicroRole : u32 {
    worker = 0,
    melee,
    ranged,
    transport,
    building,
    other,
};

struct AiMicroUnitRecord {
    u32 unit_id = 0;
    AiMicroGroup group = AiMicroGroup::army;
    AiMicroUnitState state = AiMicroUnitState::normal;
    u32 state_since_frame = 0;
    // Last order this executor issued to the unit ("already told it").
    AiSemanticActionKind last_kind = AiSemanticActionKind::no_op;
    u32 last_target_id = 0;
    i32 last_x = 0;
    i32 last_y = 0;
    u32 last_issue_frame = 0xffffffffu;
    // The policy tasked this unit directly (build/produce/...): leave it alone
    // until this frame so the executor does not immediately re-task it.
    u32 policy_hold_until_frame = 0;
    // Earliest frame at which the unit may enter `pulling_back` again.  Without
    // it a wounded fighter oscillates: pull back, leave contact, walk in, drop
    // under the threshold, pull back again.
    u32 pullback_ready_frame = 0;
    // Cohesion hysteresis: 1 while the unit is walking back to its group
    // (entered beyond cohesion_engage_radius, held until back inside
    // cohesion_radius).
    u32 cohesion_returning = 0;
    // Stuck recovery: last observed position and the frame it last changed.
    // A unit whose order the engine dropped in a non-idle state never reaches
    // the idle re-issue path, so movement is what tells us it is alive.
    i32 last_position_x = 0;
    i32 last_position_y = 0;
    u32 stationary_since_frame = 0;
    // 0 = none, else 1 + direction index: a stable deterministic offset added
    // to the destination while the unit is stuck, so the re-issued order is
    // not byte-identical to the one the engine already failed to path.
    u32 stuck_jitter = 0;
    // Harvest spread: map tile index this worker is assigned to
    // (kAiMicroNoResourceTile = none).
    u32 assigned_resource_tile = 0xffffffffu;
    // v10.4 attack wave (user design): the wave this fighter marches with.
    // 0 = none/staging.  Members present when the policy sets an ATTACK
    // objective form the FIRST wave; fighters joining later STAGE at the
    // base (defend) until attack_wave_minimum of them gather, then leave as
    // their own wave.  Cohesion applies within a wave only.
    u32 attack_wave = 0;
};

constexpr u32 kAiMicroNoResourceTile = 0xffffffffu;

struct AiMicroExecutorConfig {
    u32 reissue_interval_frames = 4;
    u32 policy_hold_frames = 16;
    u32 base_type_id = 0x80;          // 티라노 네스트: harvest drop-off / posts
    i32 defend_radius = 800;          // one screen width (800x600 @ 32px tiles)
    i32 hold_radius = 128;
    i32 patrol_radius = 320;
    i32 return_radius = 192;          // defend: come back to the post beyond this
    i32 arrival_radius = 96;
    i32 contact_margin = 64;          // added to weapon ranges for "in contact"
    i32 melee_reach = 160;            // melee target pick radius beyond range
    // Audited Tyrano roster: every melee type has action_range_base 50
    // (마소스/벨로시스/트윈 벨로시스/티라노스), ranged types 170..430.
    u32 melee_range_threshold = 64;   // 0 < attack_range <= this -> melee
    std::size_t melee_per_target = 3; // melee spread: max attackers per target
    u32 low_health_percent = 30;
    // Attack cohesion: a fighter this far from its group's centroid AND ahead
    // of it (closer to the objective) walks back to the group instead of
    // arriving alone.  Laggards keep advancing, so the centroid moves forward
    // and the leaders are released - the gate cannot deadlock.
    i32 cohesion_radius = 256;
    // Cohesion hysteresis band (2026-08-31 user replay report: fog-edge
    // trembling).  The return state ENTERS only beyond this radius and
    // RELEASES once back inside cohesion_radius.  One shared threshold made
    // a leader on the boundary alternate advance/return every frame — and a
    // changed order is issued immediately, so the unit vibrated in place.
    // With the band each phase covers real walking distance.
    i32 cohesion_engage_radius = 320;
    // Defend leash hysteresis: a defender leaves the bubble at `radius` and
    // stops returning only once back inside this percentage of it.  Sharing
    // one threshold made units on the boundary flip orders every frame.
    u32 leash_return_percent = 75;
    // Low-health pull-back cooldown (see AiMicroUnitRecord::pullback_ready_frame).
    u32 pullback_cooldown_frames = 120;
    // Stuck recovery: a unit under a move/harvest order that has not moved
    // more than `stuck_move_epsilon` px for this many frames, while still far
    // from its destination and out of contact, gets the order re-issued.
    u32 stuck_frames = 48;
    i32 stuck_jitter = 96;
    i32 stuck_move_epsilon = 8;
    // Harvest spread: workers assigned to one berry tile before the next idle
    // worker is sent to the next-nearest tile.  Round-trip time, not tile
    // capacity - tune against measured harvest rate.
    std::size_t workers_per_resource_tile = 3;
    // Search sweep: minimum frames between two frontier re-picks, so
    // revealing the current target does not restart the map scan every frame.
    u32 scout_repick_interval_frames = 8;
    // Scout picket: the post sits this far along the home -> nearest known
    // enemy building line (forward of the midpoint, to see an attack early),
    // but never closer than picket_enemy_gap px to that building (to live).
    u32 picket_forward_percent = 66;
    i32 picket_enemy_gap = 800;
    // v8 strike zone: how far around a policy-chosen attack point (the 8x8
    // target cell centre) targets are hunted before the zone is abandoned.
    // One grid cell = 512 px; the radius covers the cell from its centre.
    i32 preferred_zone_radius = 512;
    // v8 raid detach: mobility-first share of the main army split off (and
    // the floor below which detaching is pointless).
    u32 raid_detach_percent = 30;
    std::size_t raid_detach_minimum = 3;
    // Render class the engine treats as flying (terrain-independent).  Its
    // attackers resolve a separate range and damage profile.
    u32 flying_render_class = 3;
    // v9 base-defense reflex (docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md): when
    // a hostile COMBAT mobile stands within defend_radius of an own building
    // (or an own completed building lost health), the army group - and the
    // raid, if it is within reflex_raid_join_radius of the anchor - fights
    // under a temporary defend overlay at the threatened building.  The
    // policy's objectives[] are never touched; the overlay simply takes
    // precedence until the threat is gone for threat_clear_frames.  A policy
    // retreat objective is respected (the reflex never overrides fleeing).
    bool reflex_enabled = true;
    u32 threat_clear_frames = 120;
    i32 reflex_raid_join_radius = 1600;  // defend_radius * 2
    // v10 threat-proportional reflex (2026-08-31 user replay review: ONE
    // enemy harasser pulled the whole main army home).  The reflex details
    // only enough fighters to outmatch the measured threat - combat power of
    // the hostiles in the bubble times this margin - picked nearest-first to
    // the anchor; the rest of the army keeps the policy's objective.  The
    // detail is sticky while the overlay stands (members are added when the
    // threat grows, released only when it clears) so it cannot flap.  A
    // building losing health with NO visible attacker is an unmeasurable
    // threat: a small investigation picket of at most reflex_unseen_defenders
    // fighters responds (user directive: <= 3), never the whole army.
    u32 reflex_margin_percent = 150;
    std::size_t reflex_min_defenders = 2;
    std::size_t reflex_unseen_defenders = 3;
    // v10 hunt distance gate (user directive): the MAIN army only hunts
    // monsters within this radius of its centroid - far expeditions are the
    // raid slots' job.  0 disables the bound; raids are never bounded.
    i32 army_hunt_radius = 1200;
    // v10.4: staging fighters (produced after the attack order) leave as
    // their own wave once this many have gathered at the base.
    std::size_t attack_wave_minimum = 6;
    // v9 meat pickup (user directive: "고기를 줍기만 하면 알아서 사용된다"):
    // a fighter under an ATTACK objective with nothing left to fight in reach
    // walks onto the nearest visible dropped meat within this radius.  The
    // engine's cmd-5 point path collects it on arrival and consumes the
    // reserve automatically for passive recovery.
    i32 meat_pickup_radius = 800;
};

// v9 base-defense threat overlay (see AiMicroExecutorConfig::reflex_enabled).
// An OVERLAY, deliberately not an objective rewrite: the policy's objectives[]
// stay untouched, so when the threat clears the groups resume exactly what
// the policy last ordered with no restore step.
struct AiMicroThreatOverlay {
    bool active = false;
    u32 since_frame = 0;
    i32 anchor_x = -1;          // the threatened own building
    i32 anchor_y = -1;
    u32 last_seen_frame = 0;    // last frame a threat was inside the bubble
};

struct AiMicroExecutorState {
    std::array<AiMicroObjective, kAiMicroGroupCount> objectives{};
    std::vector<AiMicroUnitRecord> units;  // sorted by unit_id
    AiMicroThreatOverlay threat;
    // v10: the sticky reflex defense detail (unit ids).  Only these army
    // members fight under the threat overlay (empty = nobody detailed).
    // Dead members are pruned, new ones added when the measured threat
    // outgrows the detail's combat power (unseen attacker: topped up to
    // reflex_unseen_defenders only).
    std::vector<u32> reflex_defenders;
    // v10 attack-commit tracking: 1 once any member of the group entered
    // weapon contact since the group's objective was last SET by the policy
    // (AiMicroSetObjective resets it).  Feeds the mask's attack-commit lock.
    std::array<u32, kAiMicroGroupCount> group_engaged{};
    // v10.1 sticky meat assignments (map effect id -> collector unit id).
    // Re-picking the nearest collector EVERY frame flapped between two
    // moving units (2026-09-01 user replay report: trembling next to the
    // drop, never collected); an assignment now holds until the drop is
    // gone/claimed or the collector stops being eligible.
    std::vector<std::pair<u32, u32>> meat_assignments;
    // v10.4 attack waves: per-group wave sequence and the "assign the current
    // members to the new wave on the next step" latch set by a fresh ATTACK
    // objective.
    std::array<u32, kAiMicroGroupCount> attack_wave_seq{};
    std::array<u8, kAiMicroGroupCount> attack_wave_pending{};
    // HP-decrease half of the reflex trigger: sum of completed own building
    // health last frame (0xffffffffffffffff = not yet sampled).
    u64 last_building_health = 0xffffffffffffffffull;
    u32 last_step_frame = 0xffffffffu;
    bool initialized = false;
    // Diagnostics (headless logs / tests).
    u32 orders_issued = 0;
    u32 targets_retargeted = 0;
    u32 stuck_reissues = 0;
    u32 cohesion_holds = 0;
    u32 scout_sweep_picks = 0;
    // Army search objective: unexplored start candidates picked.
    u32 search_sweep_picks = 0;
    // Explorer frontier picks and roamer random picks.
    u32 explore_picks = 0;
    u32 roam_picks = 0;
    // Deterministic xorshift32 state for the roamer's random targets.
    u32 roam_rng = 0x9e3779b9u;
    // Targets rejected because the attacker's weapon cannot engage the
    // target's render class.  A non-zero count means the pre-v3 executor
    // would have parked those units on an order the engine rejects.
    u32 unattackable_targets_skipped = 0;
    // v9: times the base-defense reflex ACTIVATED (rising edges only).
    u32 reflex_activations = 0;
    // v9: meat-pickup move orders issued (hunt micro).
    u32 meat_pickup_orders = 0;
    // v10: neutral monsters skipped as hunt targets because the group's
    // ground fighters cannot reach them (disconnected walkable region).
    u32 hunt_unreachable_skipped = 0;
};

AiMicroRole AiMicroRoleOf(const AiObservedUnit& unit,
    const AiMicroExecutorConfig& config = {});

// v10 combat power of one unit (threat measurement): current durability times
// damage output.  A relative score - both sides are measured with the same
// formula, so only the comparison matters, not the scale.
u64 AiMicroCombatPower(const AiObservedUnit& unit);

// v10: true when at least one VISIBLE neutral monster is huntable by the
// owner's current forces - the owner fields a flying attacker (reaches
// everything), or the monster stands in / adjacent to the walkable region
// 4-connected to the owner's start.  Gates the hunt actions in the RL mask so
// the policy stops ordering hunts on monsters no ground army can reach.
// With radius > 0 only monsters within that distance of (center_x,
// center_y) count (v10 army hunt distance gate); radius 0 = unbounded.
bool AiMicroHuntableNeutralExists(const AiObservation& observation,
    const AiMicroExecutorConfig& config = {}, i32 center_x = -1,
    i32 center_y = -1, i32 radius = 0);

void AiMicroReset(AiMicroExecutorState& state);
void AiMicroSetObjective(AiMicroExecutorState& state, AiMicroGroup group,
    const AiMicroObjective& objective);
const AiMicroObjective& AiMicroObjectiveOf(const AiMicroExecutorState& state,
    AiMicroGroup group);
// Moves (or registers) a unit into a group.
void AiMicroAssignGroup(AiMicroExecutorState& state, u32 unit_id,
    AiMicroGroup group);
// Which group a unit belongs to (registers it with the role default if new).
AiMicroGroup AiMicroGroupOf(AiMicroExecutorState& state,
    const AiObservedUnit& unit, const AiMicroExecutorConfig& config = {});
// Units the policy just ordered directly: hold off re-tasking them.
void AiMicroHoldUnits(AiMicroExecutorState& state,
    const std::vector<u32>& unit_ids, u32 until_frame);
// Alive controlled members of a group (in unit-id order).
std::vector<const AiObservedUnit*> AiMicroGroupMembers(
    AiMicroExecutorState& state, const AiObservation& observation,
    AiMicroGroup group, const AiMicroExecutorConfig& config = {});
// Nearest own base building to (x, y); falls back to any own building, then
// to the start position.  Never returns -1 when the observation has a start.
UnitMovementPoint AiMicroNearestBase(const AiObservation& observation,
    i32 x, i32 y, const AiMicroExecutorConfig& config = {});
// Centroid of a group's alive members; false when the group is empty.
bool AiMicroGroupCentroid(AiMicroExecutorState& state,
    const AiObservation& observation, AiMicroGroup group,
    UnitMovementPoint& centroid, const AiMicroExecutorConfig& config = {});

// One executor frame: syncs unit records with the observation, applies the
// objective transitions (retreat arrival -> defend, no berries -> defend,
// dead attack target -> next visible hostile), and returns the orders that
// changed since the last frame, batched by (kind, target) into planner-sized
// (<= kAiMaximumUnitsPerAction) actions.
std::vector<AiSemanticAction> AiMicroExecutorStep(AiMicroExecutorState& state,
    const AiObservation& observation, const AiMicroExecutorConfig& config = {});

} // namespace ranker
