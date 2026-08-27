#include "ranker_ai_rl_features.h"

#include "ranker_unit_commands.h"

#include <algorithm>
#include <cmath>

namespace ranker {
namespace {

// Tyrano unit/building type ids (mirror ranker_ai_scripted_bot.h; kept local so
// this encoder does not depend on the scripted policy).
constexpr u32 kTypeWorker = 0x20u;       // Dinos
constexpr u32 kTypeMasos = 0x21u;
constexpr u32 kTypeUnit22 = 0x22u;       // mid-tier fighter
constexpr u32 kTypeDilophos = 0x24u;
constexpr u32 kTypeBaseNest = 0x80u;
constexpr u32 kTypePopulationNest = 0x82u;
constexpr u32 kTypeEggNest = 0x84u;
constexpr u32 kTypeLandNest = 0x85u;
constexpr u32 kTypeNest86 = 0x86u;       // late-tech buildings
constexpr u32 kTypeNest87 = 0x87u;

// Approximate build/production costs (from kReplayDerivedBuildOrder).  Used only
// to gate the legal-action mask; the live validator is authoritative at
// execution time.
constexpr u32 kWorkerCost = 100u;
constexpr u32 kMasosCost = 100u;
constexpr u32 kDilophosCost = 250u;
constexpr u32 kPopulationNestCost = 200u;
constexpr u32 kEggNestCost = 400u;
constexpr u32 kLandNestCost = 600u;
constexpr u32 kBaseNestCost = 1000u;
// Audited costs (ai_techtree_audit.txt dump of UnitMovementDefinition
// production_resource_cost / production_population_cost).
constexpr u32 kUnit22Cost = 250u;   // 벨로시스
constexpr u32 kUnit25Cost = 300u;   // 람포스
constexpr u32 kUnit27Cost = 450u;   // 프테라스
constexpr u32 kUnit28Cost = 800u;   // 트리세스
constexpr u32 kUnit2eCost = 600u;   // 켄트로스
constexpr u32 kUnit2cCost = 5000u;  // 티라노스
constexpr u32 kUnit2cPopulation = 25u;
constexpr u32 kUnit29Cost = 400u;   // 둥가리
constexpr u32 kUnit2aCost = 600u;   // 에그 스로워
constexpr u32 kNest83Cost = 350u;   // 에스코모이드
constexpr u32 kNest86Cost = 600u;   // 스카이 네스트
constexpr u32 kNest87Cost = 400u;   // 스로우 네스트
constexpr u32 kNest88Cost = 500u;   // 업그레이드 네스트
constexpr u32 kNest89Cost = 800u;   // 랜드 니스도스
constexpr u32 kNest8aCost = 800u;   // 스카이 니스도스

constexpr u32 kMobileTypeLimit = 0x60u;  // < 0x60 mobile, >= 0x80 buildings
constexpr u32 kHarvestCommand = 7u;
constexpr u32 kNeutralOwnerId = 8u;      // kOwnerNeutralRouteProbeOwnerId

float norm(u32 value, float scale) {
    return static_cast<float>(value) / scale;
}

// Worker "currently gathering" heuristic, mirroring the scripted bot's
// unit_is_harvesting so features agree with the policy's own view.
bool unit_is_harvesting(const AiObservedUnit& unit) {
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return (state >= kUnitStateWorkerApproachHarvest &&
            state <= kUnitStateWorkerHarvestFailed) ||
        unit.cargo_amount != 0 || (unit.command_flags & 4u) != 0;
}

bool unit_is_constructing(const AiObservedUnit& unit) {
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return state >= kUnitStateLegacySpawnPlacementStart &&
        state <= kUnitStateLegacySpawnPlacementApproach;
}

bool is_completed_owned_type(const AiObservedUnit& unit, u32 type_id) {
    return unit.controlled && unit.alive && !unit.under_construction &&
        unit.type_id == type_id;
}

// Neutral monster = a visible, mobile unit owned by the neutral slot (owner 8).
// These are huntable for resources and are not player-hostile.
bool is_neutral_monster(const AiObservedUnit& unit) {
    return unit.visible && unit.alive && !unit.controlled &&
        unit.owner_id == kNeutralOwnerId && unit.type_id < kMobileTypeLimit;
}

bool is_hostile_visible(const AiObservation& obs, const AiObservedUnit& unit) {
    if (!unit.visible || !unit.alive || unit.controlled ||
        unit.owner_id >= 32u ||
        (obs.active_owner_mask & (1u << unit.owner_id)) == 0) {
        return false;
    }
    return (obs.local_relation_mask & (1u << unit.owner_id)) == 0;
}

i64 sq_dist(i32 ax, i32 ay, i32 bx, i32 by) {
    const i64 dx = static_cast<i64>(ax) - bx;
    const i64 dy = static_cast<i64>(ay) - by;
    return dx * dx + dy * dy;
}

} // namespace

AiRlStepEncoding EncodeAiObservationForRl(const AiObservation& observation) {
    AiRlStepEncoding out{};

    // --- Aggregate the unit list once ---
    u32 workers = 0, worker_uc = 0, harvesting = 0, idle_workers = 0;
    u32 masos = 0, masos_uc = 0, dilophos = 0, dilophos_uc = 0;
    u32 base = 0, base_uc = 0, popnest = 0, popnest_uc = 0;
    u32 egg = 0, egg_uc = 0, land = 0, land_uc = 0;
    u32 unit22 = 0, unit22_uc = 0, nest86 = 0, nest86_uc = 0;
    u32 nest87 = 0, nest87_uc = 0;
    // A completed researcher (base/land/0x86) with an EMPTY queue: research is
    // only legal then, so the policy cannot restart an in-progress order (each
    // restart re-debits the cost and the order never completes).
    bool idle_researcher = false;
    u32 own_total = 0, own_army = 0;
    u32 enemy_mobile = 0, enemy_building = 0, enemy_total = 0;
    bool have_nearest = false;
    i64 nearest_sq = 0;
    u32 neutral_count = 0;
    bool have_neutral = false;
    i64 neutral_nearest_sq = 0;

    for (const AiObservedUnit& u : observation.units) {
        if (u.controlled && u.alive) {
            ++own_total;
            const bool uc = u.under_construction;
            switch (u.type_id) {
            case kTypeWorker:
                ++workers;
                if (uc) ++worker_uc;
                else if (unit_is_harvesting(u)) ++harvesting;
                else if (!unit_is_constructing(u)) ++idle_workers;
                break;
            case kTypeMasos: ++masos; if (uc) ++masos_uc; break;
            case kTypeUnit22: ++unit22; if (uc) ++unit22_uc; break;
            case kTypeDilophos: ++dilophos; if (uc) ++dilophos_uc; break;
            case kTypeBaseNest: ++base; if (uc) ++base_uc; break;
            case kTypePopulationNest: ++popnest; if (uc) ++popnest_uc; break;
            case kTypeEggNest: ++egg; if (uc) ++egg_uc; break;
            case kTypeLandNest: ++land; if (uc) ++land_uc; break;
            case kTypeNest86: ++nest86; if (uc) ++nest86_uc; break;
            case kTypeNest87: ++nest87; if (uc) ++nest87_uc; break;
            default: break;
            }
            if (u.type_id < kMobileTypeLimit && u.type_id != kTypeWorker &&
                !uc) {
                ++own_army;
            }
            if (!uc && (u.type_id == kTypeBaseNest ||
                    u.type_id == kTypeLandNest || u.type_id == kTypeNest86) &&
                u.queued_production_type_id == 0 &&
                u.deferred_command_count == 0) {
                idle_researcher = true;
            }
        }
        if (is_hostile_visible(observation, u)) {
            ++enemy_total;
            // Buildings start at 0x60 across ALL tribes (Primitive 0x60-,
            // Elf 0x70-, Tyrano 0x80-, Demon 0x90-); testing >= 0x80 would
            // misread Primitive/Elf structures as mobile army.
            if (u.type_id >= kMobileTypeLimit) ++enemy_building;
            else ++enemy_mobile;
            const i64 d = sq_dist(observation.start_x, observation.start_y,
                u.x, u.y);
            if (!have_nearest || d < nearest_sq) {
                have_nearest = true;
                nearest_sq = d;
            }
        }
        if (is_neutral_monster(u)) {
            ++neutral_count;
            const i64 d = sq_dist(observation.start_x, observation.start_y,
                u.x, u.y);
            if (!have_neutral || d < neutral_nearest_sq) {
                have_neutral = true;
                neutral_nearest_sq = d;
            }
        }
    }

    const u32 pop_free = observation.population_limit > observation.population_used
        ? observation.population_limit - observation.population_used
        : 0u;

    // --- Feature vector (fixed layout; keep in sync with kAiRlFeatureCount) ---
    std::size_t i = 0;
    auto put = [&](float v) {
        if (i < out.features.size()) {
            out.features[i++] = v;
        }
    };

    // Global scalars [0..12]
    put(norm(observation.simulation_frame, 18000.0f));    // 0
    put(norm(observation.primary_resources, 1000.0f));    // 1
    put(norm(observation.secondary_resources, 1000.0f));  // 2
    put(norm(observation.population_used, 100.0f));        // 3
    put(norm(observation.population_limit, 100.0f));       // 4
    put(norm(pop_free, 100.0f));                           // 5
    put(norm(observation.population_reserved, 100.0f));    // 6
    for (u32 f = 0; f < 5u; ++f) {                         // 7..11 faction one-hot
        put(observation.local_faction == f ? 1.0f : 0.0f);
    }
    put(observation.game_ended ? 1.0f : 0.0f);            // 12

    // Own units by type: count, under-construction [13..26]
    put(norm(workers, 50.0f));   put(norm(worker_uc, 10.0f));    // 13,14
    put(norm(masos, 50.0f));     put(norm(masos_uc, 10.0f));     // 15,16
    put(norm(dilophos, 50.0f));  put(norm(dilophos_uc, 10.0f));  // 17,18
    put(norm(base, 10.0f));      put(norm(base_uc, 5.0f));       // 19,20
    put(norm(popnest, 10.0f));   put(norm(popnest_uc, 5.0f));    // 21,22
    put(norm(egg, 10.0f));       put(norm(egg_uc, 5.0f));        // 23,24
    put(norm(land, 10.0f));      put(norm(land_uc, 5.0f));       // 25,26

    // Own derived [27..30]
    put(norm(harvesting, 50.0f));     // 27
    put(norm(idle_workers, 50.0f));   // 28
    put(norm(own_army, 50.0f));       // 29
    put(norm(own_total, 100.0f));     // 30

    // Visible enemy [31..35]
    put(norm(enemy_mobile, 50.0f));   // 31
    put(norm(enemy_building, 20.0f)); // 32
    put(norm(enemy_total, 50.0f));    // 33
    // Nearest enemy distance in tiles, normalized by map diagonal-ish 128.
    const float nearest_tiles = have_nearest ?
        std::sqrt(static_cast<float>(nearest_sq)) / 32.0f : 0.0f;
    put(std::min(nearest_tiles / 128.0f, 1.0f)); // 34
    put(have_nearest ? 1.0f : 0.0f);  // 35

    // Research/upgrade levels [36..38] (tracked orders: harvest, movement,
    // ground-attack).  Normalized by a small cap so multiple levels saturate.
    for (std::size_t r = 0; r < kAiObservationTrackedResearchCount; ++r) {
        put(std::min(static_cast<float>(observation.research_levels[r]) / 3.0f,
            1.0f));                       // 36,37,38
    }

    // Neutral monsters (huntable) [39..41]
    put(norm(neutral_count, 20.0f));      // 39
    const float neutral_tiles = have_neutral ?
        std::sqrt(static_cast<float>(neutral_nearest_sq)) / 32.0f : 0.0f;
    put(std::min(neutral_tiles / 128.0f, 1.0f)); // 40 nearest neutral distance
    put(have_neutral ? 1.0f : 0.0f);      // 41

    // Full-tech-tree extension [42..45]
    put(norm(unit22, 50.0f));    // 42 mid-tier fighter count
    put(norm(unit22_uc, 10.0f)); // 43
    put(norm(nest86, 5.0f));     // 44 late-tech buildings
    put(norm(nest87, 5.0f));     // 45

    // --- Legal-action mask ---
    auto set_legal = [&](AiRlHighLevelAction a, bool legal) {
        out.legal_mask[static_cast<std::size_t>(a)] = legal ? 1u : 0u;
    };
    const u32 prim = observation.primary_resources;
    const bool has_pop_room =
        observation.population_limit == 0 || pop_free > 0;
    const bool has_builder = idle_workers > 0 || harvesting > 0 || workers > 0;
    const bool completed_base = base > base_uc;   // at least one finished nest
    const bool completed_egg = egg > egg_uc;
    const bool completed_land = land > land_uc;
    const bool completed_producer = completed_base || completed_egg ||
        completed_land;
    const bool has_army = own_army > 0;
    // A currently-visible harvestable tile (resource_amount is only nonzero on
    // explored/visible harvest terrain).
    bool have_resource_tile = false;
    for (const AiObservedMapTile& t : observation.tiles) {
        if (t.passable && t.resource_amount != 0) {
            have_resource_tile = true;
            break;
        }
    }

    set_legal(AiRlHighLevelAction::no_op, true);
    set_legal(AiRlHighLevelAction::produce_worker,
        completed_base && has_pop_room && prim >= kWorkerCost);
    set_legal(AiRlHighLevelAction::produce_masos,
        completed_egg && has_pop_room && prim >= kMasosCost);
    set_legal(AiRlHighLevelAction::produce_dilophos,
        completed_egg && has_pop_room && prim >= kDilophosCost);
    set_legal(AiRlHighLevelAction::build_population_nest,
        has_builder && prim >= kPopulationNestCost);
    set_legal(AiRlHighLevelAction::build_egg_nest,
        has_builder && prim >= kEggNestCost);
    set_legal(AiRlHighLevelAction::build_land_nest,
        has_builder && prim >= kLandNestCost);
    set_legal(AiRlHighLevelAction::expand_base_nest,
        has_builder && prim >= kBaseNestCost);
    // Research costs 500 primary (order 0x14 level 1).  Requires an idle
    // researcher so an in-progress order cannot be restarted (restart re-debits
    // the cost and resets progress — observed live).
    set_legal(AiRlHighLevelAction::research_next,
        idle_researcher && prim >= 500u);
    set_legal(AiRlHighLevelAction::harvest_saturate,
        idle_workers > 0 && have_resource_tile);
    set_legal(AiRlHighLevelAction::scout_map, own_total > 0);
    set_legal(AiRlHighLevelAction::attack_nearest_enemy,
        has_army && have_nearest);
    set_legal(AiRlHighLevelAction::attack_enemy_base, has_army);
    set_legal(AiRlHighLevelAction::defend_base, has_army);
    set_legal(AiRlHighLevelAction::retreat, has_army);
    // Hunting needs a fighting force and a visible neutral monster.
    set_legal(AiRlHighLevelAction::hunt_neutral_monster,
        has_army && have_neutral);
    // Tech-tree extension.  Exact producer/prereqs live in the game tables the
    // live validator consults; the mask pre-gates on the audited cost
    // (ai_techtree_audit.txt) and a plausible completed producer being present.
    // Producer map (session reference tables): egg 0x84 makes the fighter
    // roster, base 0x80 makes 0x2c, 0x87 makes 0x29/0x2a; workers build all
    // 0x8x structures.
    const bool completed_87 = nest87 > nest87_uc;
    set_legal(AiRlHighLevelAction::produce_unit_x22,
        completed_egg && has_pop_room && prim >= kUnit22Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x25,
        completed_egg && has_pop_room && prim >= kUnit25Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x27,
        completed_egg && has_pop_room && prim >= kUnit27Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x28,
        completed_egg && has_pop_room && prim >= kUnit28Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x2e,
        completed_egg && has_pop_room && prim >= kUnit2eCost);
    // 티라노스 reserves 25 population, so the generic one-slot headroom check
    // is not enough.
    set_legal(AiRlHighLevelAction::produce_unit_x2c,
        completed_base && prim >= kUnit2cCost &&
        (observation.population_limit == 0 || pop_free >= kUnit2cPopulation));
    set_legal(AiRlHighLevelAction::produce_unit_x29,
        completed_87 && has_pop_room && prim >= kUnit29Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x2a,
        completed_87 && has_pop_room && prim >= kUnit2aCost);
    set_legal(AiRlHighLevelAction::build_nest_x86,
        has_builder && prim >= kNest86Cost);
    set_legal(AiRlHighLevelAction::build_nest_x87,
        has_builder && prim >= kNest87Cost);
    set_legal(AiRlHighLevelAction::build_nest_x83,
        has_builder && prim >= kNest83Cost);
    set_legal(AiRlHighLevelAction::build_nest_x88,
        has_builder && prim >= kNest88Cost);
    set_legal(AiRlHighLevelAction::build_nest_x89,
        has_builder && prim >= kNest89Cost);
    set_legal(AiRlHighLevelAction::build_nest_x8a,
        has_builder && prim >= kNest8aCost);

    return out;
}

} // namespace ranker
