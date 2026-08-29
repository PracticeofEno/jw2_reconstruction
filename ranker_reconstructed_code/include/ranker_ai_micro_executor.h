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
    scout = 2,    // 0..1 units split off by scout_map
};
constexpr std::size_t kAiMicroGroupCount = 3;

enum class AiMicroObjectiveKind : u32 {
    harvest = 0,
    attack,
    defend,
    retreat,
    scout,
};

struct AiMicroObjective {
    AiMicroObjectiveKind kind = AiMicroObjectiveKind::defend;
    // attack: a unit target (0 = point target / free engagement).
    u32 target_unit_id = 0;
    // attack point / defend post / retreat destination / scout post.
    // -1 = none.
    i32 target_x = -1;
    i32 target_y = -1;
    // defend: bubble radius around the post (px).
    i32 radius = 0;
    // attack: neutral monsters (owner 8) are valid targets too (hunt).
    bool include_neutral = false;
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
};

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
};

struct AiMicroExecutorState {
    std::array<AiMicroObjective, kAiMicroGroupCount> objectives{};
    std::vector<AiMicroUnitRecord> units;  // sorted by unit_id
    u32 last_step_frame = 0xffffffffu;
    bool initialized = false;
    // Diagnostics (headless logs / tests).
    u32 orders_issued = 0;
    u32 targets_retargeted = 0;
};

AiMicroRole AiMicroRoleOf(const AiObservedUnit& unit,
    const AiMicroExecutorConfig& config = {});

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
