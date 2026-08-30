#ifndef RANKER_AI_RL_FEATURES_H
#define RANKER_AI_RL_FEATURES_H

#include "ranker_ai_observation.h"

#include <array>

namespace ranker {

// Reinforcement-learning encoding of an AiObservation: a fixed-size, normalized
// feature vector (policy/value network input) plus a legal-action mask over the
// high-level (hierarchical) action set the RL policy chooses from.  This is a
// pure function of the observation so it is deterministic and testable: the
// same observation always yields byte-identical features and mask.
//
// Design: docs/AI_PLAY_RL_STRUCTURE.md.  The high-level actions are strategy
// decisions; the scripted bot executes their micro (unit selection, pathing,
// build placement).  The mask is a learning aid computed from the observation;
// the live validator remains authoritative at execution time.

// v3: clamped normalization + frame scale 60000 (same layout/count as v2).
// v4: +6 executor/combat features [80..85], harvest_saturate removed (the
// micro executor keeps idle workers mining every frame; the action had no
// effect left and was permanently masked).
// v5: +445 = 8x8 spatial grid x6 channels [86..469], start cell [470..471],
// direction/distance vectors [472..486], enemy composition/tribe [487..497],
// own-army state [498..508], production pipeline [509..525], scout
// [526..530].  Everything the v4 encoder threw away (map, enemy types,
// per-unit state, queues, scout) is now summarized; layout is append-only.
// v6 appends the army objective/tactic the executor is running [531..533]:
// search one-hot, buildings_first, neutral_only (attack one-hot stays at 82).
// v7 appends expansion [534..540]: next expansion site dx/dy/distance/lit,
// berry scout alive, reserved resources (builds still walking), builds in
// flight.
constexpr u32 kAiRlFeatureVersion = 7u;

// High-level (strategy) action space the RL policy samples from.
enum class AiRlHighLevelAction : u32 {
    no_op = 0,
    produce_worker,
    produce_masos,
    produce_dilophos,
    build_population_nest,
    build_egg_nest,
    build_land_nest,
    expand_base_nest,
    scout_map,
    attack_nearest_enemy,
    attack_enemy_base,
    defend_base,
    retreat,
    hunt_neutral_monster,
    // Full-tech-tree extension (appended so earlier indices stay stable).
    // Producer map from the session reference tables (alternate=units,
    // completion=research, primary=builds):
    //   worker 0x20 builds 0x80 0x82 0x83 0x84 0x85 0x86 0x87 0x88 0x89 0x8a
    //   base 0x80 -> units 0x20 0x2c, research 0x14 0x2a 0x38 0x2b
    //   egg 0x84 -> units 0x21 0x22 0x24 0x25 0x27 0x28 0x2e
    //   land 0x85 research 0x19 0x1a 0x16; 0x86 research 0x1c 0x1d 0x18
    //   0x87 -> units 0x29 0x2a
    produce_unit_x22,   // 0x22 mid-tier fighter, the built-in AI's main army
    build_nest_x86,     // 0x86 research building
    build_nest_x87,     // 0x87 unit-production building
    produce_unit_x25,   // egg-nest roster
    produce_unit_x27,
    produce_unit_x28,
    produce_unit_x2e,
    produce_unit_x2c,   // base-nest unit
    produce_unit_x29,   // 0x87 roster
    produce_unit_x2a,
    build_nest_x83,     // remaining worker-buildable structures
    build_nest_x88,
    build_nest_x89,
    build_nest_x8a,
    // v2 (docs/AI_PLAY_TYRANO_FULL_CAPABILITY_DESIGN.md §4, audited caps):
    // the Tyrano-specific mechanics the packet interface can now express.
    merge_twin_velocis, // 벨로시스 x2 -> 트윈 벨로시스 (0x22 -> 0x23)
    merge_twin_rhampos, // 람포스 x2 -> 트윈 람포스 (0x25 -> 0x26)
    merge_twin_pteras,  // 프테라스 x2 -> 트윈 프테라스 (0x27 -> 0x2d)
    merge_mutant,       // 딜로포스+프테라스+트리세스 -> 뮤턴트 (0x2b)
    morph_enter_army,   // wild-dino morph (research 0x2a gate)
    morph_exit_army,
    stance_on_army,     // Tyrano stance = command 0x14 (flag 0x10000)
    stance_off_army,
    hold_army,          // hold position at the current spot
    patrol_defense,     // patrol base <-> nearest resource cluster
    drop_attack,        // 둥가리 board -> enemy start -> unload (autopilot)
    // Per-order research (replaces the single research_next walker, so the
    // policy chooses WHAT to research).  Audited Tyrano tree: see
    // kAiRlResearchActions for order id / researcher building / levels / cost.
    research_harvest,          // 0x14 베리 채집량 증가        (티라노 네스트)
    research_ground_attack,    // 0x19 지상 유닛 공격 업 x5     (랜드 네스트)
    research_ground_defense,   // 0x1a 지상 유닛 방어 업 x5     (랜드 네스트)
    research_movement,         // 0x16 벨로시스·딜로포스 속도   (랜드 네스트)
    research_air_attack,       // 0x1c 공중 유닛 공격 업 x5     (스카이 네스트)
    research_air_defense,      // 0x1d 공중 유닛 방어 업 x5     (스카이 네스트)
    research_mutant_merge,     // 0x18 뮤턴트 합체             (스카이 네스트)
    research_morph,            // 0x2a 공룡 변신               (티라노 네스트)
    research_haste,            // 0x38 헤이스트                (티라노 네스트)
    research_exp_down,         // 0x2b 레벨 업 경험치 감소      (티라노 네스트)
    research_melee_reinforce,  // 0x1b 근접 강화               (랜드 니스도스)
    research_triceps_speed,    // 0x2d 트리세스 속도            (랜드 니스도스)
    research_air_reinforce,    // 0x1e 공중 강화               (스카이 니스도스)
    // v6 - the three army strategies are explicit and disjoint:
    //   search_enemy_base   (no enemy building known)  army sweeps the map
    //   attack_enemy_base   (an enemy building known)  army = buildings_first
    //   defend_base                                    army holds own buildings
    search_enemy_base,
    // v7 - expansion is a two-step chain the MASK teaches: scout_berry sends
    // one unit to light the next expansion site (legal while that site is
    // dark), expand_base_nest builds there (legal once it is lit).
    scout_berry,
    // v7 - the search is split by purpose (masked off when its job is done):
    //   search_enemy_base  army sweeps the UNEXPLORED START CANDIDATES
    //   explore_frontier   one unit (air first) walks its reachable frontier
    //   roam_scout         one unit (air, then fastest) patrols ground outside
    //                      the active vision at random, indefinitely
    explore_frontier,
    roam_scout,
};

constexpr std::size_t kAiRlActionCount = 56;

// One row per research action: the production order it starts, the building
// type that researches it (session completion_references), the level cap and
// the primary cost per current level (ai_techtree_audit.txt cost_v0..v2).
struct AiRlResearchAction {
    AiRlHighLevelAction action;
    u32 order;
    u32 researcher_type;
    u8 max_levels;
    u32 cost_by_level[3];
};
constexpr std::size_t kAiRlResearchActionCount = 13;
constexpr AiRlResearchAction kAiRlResearchActions[kAiRlResearchActionCount] = {
    {AiRlHighLevelAction::research_harvest,         0x14u, 0x80u, 1, {500, 500, 500}},
    {AiRlHighLevelAction::research_ground_attack,   0x19u, 0x85u, 5, {200, 400, 600}},
    {AiRlHighLevelAction::research_ground_defense,  0x1au, 0x85u, 5, {200, 400, 600}},
    {AiRlHighLevelAction::research_movement,        0x16u, 0x85u, 1, {400, 400, 400}},
    {AiRlHighLevelAction::research_air_attack,      0x1cu, 0x86u, 5, {200, 400, 600}},
    {AiRlHighLevelAction::research_air_defense,     0x1du, 0x86u, 5, {200, 400, 600}},
    {AiRlHighLevelAction::research_mutant_merge,    0x18u, 0x86u, 1, {300, 300, 300}},
    {AiRlHighLevelAction::research_morph,           0x2au, 0x80u, 1, {300, 300, 300}},
    {AiRlHighLevelAction::research_haste,           0x38u, 0x80u, 1, {300, 300, 300}},
    {AiRlHighLevelAction::research_exp_down,        0x2bu, 0x80u, 1, {500, 700, 1000}},
    {AiRlHighLevelAction::research_melee_reinforce, 0x1bu, 0x89u, 1, {600, 600, 600}},
    {AiRlHighLevelAction::research_triceps_speed,   0x2du, 0x89u, 1, {500, 500, 500}},
    {AiRlHighLevelAction::research_air_reinforce,   0x1eu, 0x8au, 1, {600, 600, 600}},
};
constexpr const AiRlResearchAction* FindAiRlResearchAction(
    AiRlHighLevelAction action) {
    for (const AiRlResearchAction& entry : kAiRlResearchActions) {
        if (entry.action == action) {
            return &entry;
        }
    }
    return nullptr;
}

// Fixed feature-vector layout (see the .cpp for the exact per-index meaning).
// v1: 36 base + 3 research levels + 3 neutral-monster + 4 tech-tree = 46.
// v2 appends: 10 research levels, 11 unit counts, 4 building counts, and 9
// mechanic aggregates (stance/morph/transport/army/queue state) = 80.
constexpr std::size_t kAiRlFeatureCount = 545;

struct AiRlStepEncoding {
    // Normalized policy/value-network input.
    std::array<float, kAiRlFeatureCount> features{};
    // legal_mask[i] == 1 -> action i is currently permissible (approx, from the
    // observation).  0 -> mask its logit to -inf so the policy never picks it.
    std::array<std::uint8_t, kAiRlActionCount> legal_mask{};
};

// Encode one observation into the RL feature vector and legal-action mask.
AiRlStepEncoding EncodeAiObservationForRl(const AiObservation& observation);

} // namespace ranker

#endif // RANKER_AI_RL_FEATURES_H
