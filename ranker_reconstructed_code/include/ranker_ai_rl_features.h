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
constexpr u32 kAiRlFeatureVersion = 3u;

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
    research_next,
    harvest_saturate,
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
};

constexpr std::size_t kAiRlActionCount = 41;

// Fixed feature-vector layout (see the .cpp for the exact per-index meaning).
// v1: 36 base + 3 research levels + 3 neutral-monster + 4 tech-tree = 46.
// v2 appends: 10 research levels, 11 unit counts, 4 building counts, and 9
// mechanic aggregates (stance/morph/transport/army/queue state) = 80.
constexpr std::size_t kAiRlFeatureCount = 80;

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
