#include "ranker_ai_rl_features.h"
#include <vector>
#include <array>

#include "ranker_ai_actions.h"
#include "ranker_ai_expansion.h"
#include "ranker_ai_micro_executor.h"
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
    // Clamped to [0,1]: unclamped features blew past the trained range in
    // long games (frame/18000 reached 5.5 at the 100k cap, unit counts /50 hit
    // 4.9 at 243 units) and saturated the tanh trunk, making late-game states
    // indistinguishable.
    return std::min(static_cast<float>(value) / scale, 1.0f);
}

// Worker "currently gathering", mirroring the scripted bot's
// unit_is_harvesting so features agree with the policy's own view.
// cargo_amount is deliberately NOT consulted: raw +0x4c is a state-dependent
// union that ProcessWorkerDepositCargo never clears, so it stays non-zero for
// the rest of the game once a worker has mined once.
bool unit_is_harvesting(const AiObservedUnit& unit) {
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return (state >= kUnitStateWorkerApproachHarvest &&
            state <= kUnitStateWorkerHarvestFailed) ||
        (unit.command_flags & 4u) != 0;
}

// Literally standing around doing nothing: runtime idle states 0x00/0x01 with
// an empty command queue.  Mirrors the scripted bot's unit_is_idle.
bool unit_is_idle(const AiObservedUnit& unit) {
    if (!unit.controlled || !unit.alive || unit.under_construction ||
        unit.deferred_command_count != 0) {
        return false;
    }
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return state == 0u || state == kUnitStateRuntimeIdleAcquire;
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

// Audited primary cost of a structure type (ai_techtree_audit.txt); 0 for an
// unknown type.  Used by the reservation accounting below.
u32 building_cost_of(u32 type_id) {
    switch (type_id) {
    case 0x80u: return kBaseNestCost;
    case 0x82u: return kPopulationNestCost;
    case 0x83u: return kNest83Cost;
    case 0x84u: return kEggNestCost;
    case 0x85u: return kLandNestCost;
    case 0x86u: return kNest86Cost;
    case 0x87u: return kNest87Cost;
    case 0x88u: return kNest88Cost;
    case 0x89u: return kNest89Cost;
    case 0x8au: return kNest8aCost;
    default: return 0u;
    }
}

u32 AiRlBuildingCostOf(u32 structure_type) {
    return building_cost_of(structure_type);
}

void ApplyAiRlDecisionContext(AiRlStepEncoding& encoding, u32 frames_since,
    u32 triggers, const std::array<u32, 3>& autopilot_counts) {
    encoding.features[772] =
        std::min(static_cast<float>(frames_since) / 64.0f, 1.0f);
    for (u32 bit = 0; bit < 12u; ++bit) {
        encoding.features[773 + bit] =
            (triggers & (1u << bit)) != 0 ? 1.0f : 0.0f;
    }
    for (u32 slot = 0; slot < 3u; ++slot) {
        encoding.features[785 + slot] =
            std::min(static_cast<float>(autopilot_counts[slot]) / 8.0f, 1.0f);
    }
}

AiRlStepEncoding EncodeAiObservationForRl(const AiObservation& observation) {
    AiRlStepEncoding out{};

    // --- Aggregate the unit list once ---
    u32 workers = 0, worker_uc = 0, harvesting = 0, idle_workers = 0;
    // Reservation accounting: cost of every structure a worker is walking to
    // build (not yet debited by the engine).  Masks and features use
    // primary - reserved, so the policy cannot spend money it already
    // committed and have the build fail on arrival.
    u32 reserved_resources = 0, builds_in_flight = 0, nest_walkers = 0;
    u32 masos = 0, masos_uc = 0, dilophos = 0, dilophos_uc = 0;
    u32 base = 0, base_uc = 0, popnest = 0, popnest_uc = 0;
    u32 egg = 0, egg_uc = 0, land = 0, land_uc = 0;
    u32 unit22 = 0, unit22_uc = 0, nest86 = 0, nest86_uc = 0;
    u32 nest87 = 0, nest87_uc = 0;
    // A completed researcher (base/land/0x86) with an EMPTY queue: research is
    // only legal then, so the policy cannot restart an in-progress order (each
    // restart re-debits the cost and the order never completes).
    bool idle_researcher = false;
    // Completed own building of type 0x80+i with an empty production queue
    // (a busy researcher must not be re-enqueued: restart-drain).
    std::array<bool, 16> idle_researcher_by_type{};
    // A completed producer of each kind with room left in its production
    // queue.  "Completed producer exists" is not enough to make a produce
    // action legal: the planner rejects an order once every producer of that
    // kind is at kUnitProductionQueueLimit, and a masked-legal-but-rejected
    // pick burns the owner's whole decision cycle.
    bool producer_free_base = false;
    bool producer_free_egg = false;
    bool producer_free_87 = false;
    u32 own_total = 0, own_army = 0;
    u32 enemy_mobile = 0, enemy_building = 0, enemy_total = 0;
    bool have_nearest = false;
    i64 nearest_sq = 0;
    u32 neutral_count = 0;
    bool have_neutral = false;
    i64 neutral_nearest_sq = 0;
    // v2 aggregates (docs/AI_PLAY_TYRANO_FULL_CAPABILITY_DESIGN.md §3).
    // COMPLETED-only per-type counts for the extended roster 0x20..0x2f.
    std::array<u32, 16> roster_counts{};
    u32 nest83 = 0, nest88 = 0, nest89 = 0, nest8a = 0;
    u32 stance_active = 0, stance_ready = 0;
    u32 morphed = 0, morph_ready = 0;
    u32 attached_passengers = 0;
    u32 raid_enemies = 0;
    u32 queue_depth = 0;
    u32 cargo_workers = 0;
    u64 army_health = 0, army_max_health = 0;
    i64 army_centroid_x = 0, army_centroid_y = 0;
    // v4 engagement features: own fighters vs visible hostiles.
    std::vector<const AiObservedUnit*> own_fighters;
    std::vector<const AiObservedUnit*> visible_hostiles;
    constexpr u32 kStanceCommandBit = 1u << 0x14;
    constexpr u32 kStanceActiveFlag = 0x10000u;
    constexpr u32 kMorphCommandBit = 1u << 0x11;
    constexpr u32 kMorphedTypeFlag = 0x08000000u;
    constexpr u32 kAttachedState = 0x45u;
    // One screen (800 px) = the micro executor's defend bubble around every
    // own nest, so "raid" here means the same thing the executor reacts to.
    constexpr i64 kRaidRadiusSquared = 800 * 800;

    for (const AiObservedUnit& u : observation.units) {
        if (u.controlled && u.alive) {
            ++own_total;
            const bool uc = u.under_construction;
            if (const u32 walking = AiWalkingBuildTypeOf(u)) {
                reserved_resources += building_cost_of(walking);
                ++builds_in_flight;
                if (walking == 0x80u) {
                    ++nest_walkers;
                }
            }
            if (!uc && u.type_id >= 0x20u && u.type_id < 0x30u) {
                ++roster_counts[u.type_id - 0x20u];
            }
            if (!uc) {
                switch (u.type_id) {
                case 0x83u: ++nest83; break;
                case 0x88u: ++nest88; break;
                case 0x89u: ++nest89; break;
                case 0x8au: ++nest8a; break;
                default: break;
                }
            }
            if (u.type_id >= kMobileTypeLimit) {
                queue_depth += u.deferred_command_count;
            }
            if (u.type_id == kTypeWorker && (u.command_flags & 4u) != 0) {
                ++cargo_workers;
            }
            if (!uc && u.type_id < kMobileTypeLimit) {
                if ((u.command_flags & kStanceActiveFlag) != 0) {
                    ++stance_active;
                } else if ((u.type_flags & kStanceCommandBit) != 0 &&
                    u.action_mode != 0) {
                    ++stance_ready;
                }
                if ((u.type_flags & kMorphedTypeFlag) != 0) {
                    ++morphed;
                } else if ((u.type_flags & kMorphCommandBit) != 0) {
                    ++morph_ready;
                }
                if ((u.command_state & kUnitCommandStateMask) ==
                    kAttachedState) {
                    ++attached_passengers;
                }
            }
            switch (u.type_id) {
            case kTypeWorker:
                ++workers;
                if (uc) ++worker_uc;
                else if (unit_is_harvesting(u)) ++harvesting;
                else if (unit_is_idle(u)) ++idle_workers;
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
                army_health += u.health;
                army_max_health += u.max_health;
                army_centroid_x += u.x;
                army_centroid_y += u.y;
                own_fighters.push_back(&u);
            }
            if (!uc && (u.type_id == kTypeBaseNest ||
                    u.type_id == kTypeLandNest || u.type_id == kTypeNest86) &&
                u.queued_production_type_id == 0 &&
                u.deferred_command_count == 0) {
                idle_researcher = true;
            }
            if (!uc && u.type_id >= 0x80u && u.type_id < 0x90u &&
                u.queued_production_type_id == 0 &&
                u.deferred_command_count == 0) {
                idle_researcher_by_type[u.type_id - 0x80u] = true;
            }
            if (!uc &&
                u.deferred_command_count < kUnitProductionQueueLimit) {
                switch (u.type_id) {
                case kTypeBaseNest: producer_free_base = true; break;
                case kTypeEggNest: producer_free_egg = true; break;
                case kTypeNest87: producer_free_87 = true; break;
                default: break;
                }
            }
        }
        if (is_hostile_visible(observation, u)) {
            ++enemy_total;
            visible_hostiles.push_back(&u);
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
            if (d <= kRaidRadiusSquared) {
                ++raid_enemies;
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

    // Population semantics (CheckUnitProductionRequirements, unit_lifecycle
    // :924-934): population_used is the SUPPLY provided by class-2 buildings
    // (audit "pop=" column: base nest 9, pop nest 8, egg nest 1 ...),
    // population_reserved is the live demand incl. queued production, and the
    // validator requires reserved + cost <= min(supply, hard limit).  The v1
    // encoder computed limit - used, which is not a resource at all — the
    // produce mask was optimistic and every over-pop pick was wasted.
    const u32 pop_supply = observation.population_limit == 0
        ? observation.population_used
        : std::min(observation.population_used, observation.population_limit);
    const u32 pop_free = pop_supply > observation.population_reserved
        ? pop_supply - observation.population_reserved
        : 0u;

    // --- Feature vector (fixed layout; keep in sync with kAiRlFeatureCount) ---
    std::size_t i = 0;
    auto put = [&](float v) {
        if (i < out.features.size()) {
            out.features[i++] = v;
        }
    };

    // Global scalars [0..12]
    // Frame scale matches real game length: eliminations land at 31k-40k
    // frames (safety cap 100k); /18000 saturated in the opening third.
    put(norm(observation.simulation_frame, 60000.0f));    // 0
    put(norm(observation.primary_resources, 1000.0f));    // 1
    // 2: the engine carries a second owner resource slot, but this game has a
    // single resource — always 0 here (kept for the append-only layout).
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

    // --- v2 features [46..79] ---
    // Research levels [46..55] for the remaining audited orders (per-order max
    // from ai_techtree_audit.txt).
    struct ResearchFeature {
        u32 order;
        float max_levels;
    };
    static constexpr ResearchFeature kResearchFeatures[] = {
        {0x1au, 5.0f}, {0x1cu, 5.0f}, {0x1du, 5.0f}, {0x18u, 1.0f},
        {0x2au, 1.0f}, {0x38u, 1.0f}, {0x2bu, 1.0f}, {0x1bu, 1.0f},
        {0x2du, 1.0f}, {0x1eu, 1.0f},
    };
    for (const ResearchFeature& r : kResearchFeatures) {
        put(std::min(static_cast<float>(
            observation.research_order_levels[r.order]) / r.max_levels, 1.0f));
    }
    // Completed roster counts [56..66]: 0x23,0x25,0x26,0x27,0x28,0x29,0x2a,
    // 0x2b,0x2c,0x2d,0x2e.
    static constexpr u32 kRosterFeatureTypes[] = {
        0x23u, 0x25u, 0x26u, 0x27u, 0x28u, 0x29u, 0x2au, 0x2bu, 0x2cu, 0x2du,
        0x2eu,
    };
    for (const u32 type : kRosterFeatureTypes) {
        const float scale = (type == 0x2bu || type == 0x2cu) ? 10.0f : 50.0f;
        put(norm(roster_counts[type - 0x20u], scale));
    }
    // Completed extra-building counts [67..70].
    put(norm(nest83, 5.0f));
    put(norm(nest88, 5.0f));
    put(norm(nest89, 5.0f));
    put(norm(nest8a, 5.0f));
    // Mechanic aggregates [71..79].
    put(norm(stance_active, 20.0f));       // 71
    put(norm(stance_ready, 20.0f));        // 72
    put(norm(morphed, 20.0f));             // 73
    put(norm(attached_passengers, 14.0f)); // 74
    put(army_max_health != 0 ? static_cast<float>(army_health) /
        static_cast<float>(army_max_health) : 0.0f);  // 75 army HP ratio
    put(norm(raid_enemies, 20.0f));        // 76 enemies inside base perimeter
    // 77: army centroid distance to the nearest non-own map start CANDIDATE
    // (march progress; 1.0 = no army or no candidate).  Candidates are
    // anonymous — which one the enemy actually occupies must be scouted.
    float march_distance = 1.0f;
    if (own_army != 0 && observation.start_candidate_mask != 0) {
        const i32 centroid_x =
            static_cast<i32>(army_centroid_x / static_cast<i64>(own_army));
        const i32 centroid_y =
            static_cast<i32>(army_centroid_y / static_cast<i64>(own_army));
        bool have_start = false;
        i64 best = 0;
        for (u32 slot = 0; slot < 8u; ++slot) {
            if ((observation.start_candidate_mask & (1u << slot)) == 0) {
                continue;
            }
            const i32 sx = observation.start_candidate_x[slot];
            const i32 sy = observation.start_candidate_y[slot];
            if (sx == observation.start_x && sy == observation.start_y) {
                continue;  // our own start slot
            }
            const i64 d = sq_dist(centroid_x, centroid_y, sx, sy);
            if (!have_start || d < best) {
                have_start = true;
                best = d;
            }
        }
        if (have_start) {
            march_distance = std::min(
                std::sqrt(static_cast<float>(best)) / 32.0f / 128.0f, 1.0f);
        }
    }
    put(march_distance);                   // 77
    put(norm(queue_depth, 20.0f));         // 78 total production queue depth
    put(norm(cargo_workers, 50.0f));       // 79

    // --- v4 features [80..85]: what the micro executor is doing and how the
    // fight is going, so the policy can decide attack/defend/retreat on the
    // same terms the executor acts on (docs/AI_PLAY_MICRO_EXECUTOR_DESIGN.md).
    // "In contact" mirrors the executor: a hostile inside
    // max(own range, hostile range) + 64 px.  Force ratio compares the health
    // of own fighters in contact with the health of hostile mobiles within
    // twice that envelope of any own fighter (0.5 = even / no contact).
    u32 engaged = 0;
    u64 engaged_health = 0;
    u64 enemy_near_health = 0;
    std::vector<const AiObservedUnit*> counted_enemies;
    constexpr i64 kContactMargin = 64;
    for (const AiObservedUnit* fighter : own_fighters) {
        bool in_contact = false;
        for (const AiObservedUnit* hostile : visible_hostiles) {
            const i64 envelope = static_cast<i64>(std::max(fighter->attack_range,
                hostile->attack_range)) + kContactMargin;
            const i64 d = sq_dist(fighter->x, fighter->y, hostile->x, hostile->y);
            if (d <= envelope * envelope) {
                in_contact = true;
            }
            if (hostile->type_id < kMobileTypeLimit &&
                d <= envelope * envelope * 4 &&
                std::find(counted_enemies.begin(), counted_enemies.end(),
                    hostile) == counted_enemies.end()) {
                counted_enemies.push_back(hostile);
                enemy_near_health += hostile->health;
            }
        }
        if (in_contact) {
            ++engaged;
            engaged_health += fighter->health;
        }
    }
    put(own_army != 0 ? static_cast<float>(engaged) /
        static_cast<float>(own_army) : 0.0f);                      // 80 engaged fraction
    put(engaged_health + enemy_near_health != 0 ?
        static_cast<float>(engaged_health) /
        static_cast<float>(engaged_health + enemy_near_health) : 0.5f); // 81 local force ratio
    // 82..84: army objective one-hot (attack / defend / retreat); harvest and
    // scout never apply to the army group, "none" = all zero.
    const u32 objective = observation.army_objective_kind;
    put(objective == 2u ? 1.0f : 0.0f);    // 82 attack   (AiMicroObjectiveKind::attack + 1)
    put(objective == 3u ? 1.0f : 0.0f);    // 83 defend
    put(objective == 4u ? 1.0f : 0.0f);    // 84 retreat
    put(own_army != 0 ? std::min(static_cast<float>(
        observation.army_pulling_back) / static_cast<float>(own_army), 1.0f)
        : 0.0f);                           // 85 fighters pulling back (low hp)

    // An enemy building is KNOWN: visible now, or remembered in the fog
    // memory (set in the v5 memory pass below).  Gates attack_enemy_base
    // (needs one) against search_enemy_base (needs none).
    bool enemy_base_known = enemy_building > 0;

    // ======================================================================
    // v5 features [86..530] — the information the v4 vector discarded.
    // ======================================================================
    {
        constexpr u32 kGrid = 8u;
        constexpr std::size_t kCells = static_cast<std::size_t>(kGrid) * kGrid;
        const i64 map_w = static_cast<i64>(
            std::max<u32>(observation.map_width_tiles, 1u)) * 32;
        const i64 map_h = static_cast<i64>(
            std::max<u32>(observation.map_height_tiles, 1u)) * 32;
        const auto cell_of = [&](i32 x, i32 y) -> std::size_t {
            const i64 cx = std::clamp<i64>(
                static_cast<i64>(std::max(x, 0)) * kGrid / map_w, 0, kGrid - 1);
            const i64 cy = std::clamp<i64>(
                static_cast<i64>(std::max(y, 0)) * kGrid / map_h, 0, kGrid - 1);
            return static_cast<std::size_t>(cy * kGrid + cx);
        };
        const auto tribe_of = [](u32 type_id) -> i32 {
            if (type_id < 0x40u) return static_cast<i32>(type_id >> 4);
            if (type_id >= 0x60u && type_id < 0xa0u) {
                return static_cast<i32>((type_id - 0x60u) >> 4);
            }
            return -1;
        };
        const auto unit_role = [](const AiObservedUnit& u) -> u32 {
            // 0 worker, 1 melee, 2 ranged, 3 transport, 4 other
            if ((u.type_flags & (1u << 7)) != 0) return 0u;
            const bool attacks = (u.type_flags & (1u << 5)) != 0;
            if (u.transport_capacity > 0 && !attacks) return 3u;
            if (!attacks) return 4u;
            return (u.attack_range != 0 && u.attack_range <= 64u) ? 1u : 2u;
        };
        const auto in_attack_state = [](const AiObservedUnit& u) {
            const u32 s = u.command_state & kUnitCommandStateMask;
            return s == 0x03u || s == 0x04u || (s >= 0x1cu && s <= 0x22u) ||
                s == 0x39u || s == 0x3au;
        };

        // ---- spatial grid [86..469] --------------------------------------
        std::array<float, kCells> g_own_buildings{}, g_own_army{},
            g_enemy_mobile{}, g_enemy_building{}, g_resource{}, g_explored{},
            g_cell_tiles{};
        for (const AiObservedUnit& u : observation.units) {
            if (u.controlled && u.alive) {
                if (u.type_id >= kMobileTypeLimit) {
                    g_own_buildings[cell_of(u.x, u.y)] += 1.0f;
                } else if (u.type_id != kTypeWorker && !u.under_construction) {
                    g_own_army[cell_of(u.x, u.y)] += 1.0f;
                }
            } else if (is_hostile_visible(observation, u)) {
                if (u.type_id >= kMobileTypeLimit) {
                    g_enemy_building[cell_of(u.x, u.y)] += 1.0f;
                } else {
                    g_enemy_mobile[cell_of(u.x, u.y)] += 1.0f;
                }
            }
        }
        const bool tiles_valid = observation.map_width_tiles != 0 &&
            observation.tiles.size() == static_cast<std::size_t>(
                observation.map_width_tiles) * observation.map_height_tiles;
        const bool have_memory = tiles_valid &&
            observation.enemy_building_memory.size() == observation.tiles.size();
        std::vector<UnitMovementPoint> remembered_buildings;
        if (tiles_valid) {
            for (std::size_t t = 0; t < observation.tiles.size(); ++t) {
                const u32 tx = static_cast<u32>(t % observation.map_width_tiles);
                const u32 ty = static_cast<u32>(t / observation.map_width_tiles);
                const i32 wx = static_cast<i32>(tx * 32u + 16u);
                const i32 wy = static_cast<i32>(ty * 32u + 16u);
                const std::size_t c = cell_of(wx, wy);
                g_cell_tiles[c] += 1.0f;
                const AiObservedMapTile& tile = observation.tiles[t];
                if (tile.explored) {
                    g_explored[c] += 1.0f;
                }
                // Berries are public map knowledge (v7): the known amount is
                // summed whether or not the tile has been explored.  (Berry
                // tiles are not `passable` - engine class 0x100.)
                g_resource[c] += static_cast<float>(tile.resource_amount);
                if (have_memory && observation.enemy_building_memory[t] != 0 &&
                    !tile.visible) {
                    // Visible tiles are covered by the live unit pass above.
                    g_enemy_building[c] += 1.0f;
                    remembered_buildings.push_back({wx, wy});
                    enemy_base_known = true;
                }
            }
        }
        for (std::size_t c = 0; c < kCells; ++c) put(norm(g_own_buildings[c], 5.0f));
        for (std::size_t c = 0; c < kCells; ++c) put(norm(g_own_army[c], 20.0f));
        for (std::size_t c = 0; c < kCells; ++c) put(norm(g_enemy_mobile[c], 20.0f));
        for (std::size_t c = 0; c < kCells; ++c) put(norm(g_enemy_building[c], 5.0f));
        for (std::size_t c = 0; c < kCells; ++c) put(norm(g_resource[c], 5000.0f));
        for (std::size_t c = 0; c < kCells; ++c) {
            put(g_cell_tiles[c] > 0.0f ? g_explored[c] / g_cell_tiles[c] : 0.0f);
        }
        // ---- own start cell [470..471] -----------------------------------
        const std::size_t start_cell = cell_of(std::max(observation.start_x, 0),
            std::max(observation.start_y, 0));
        put(static_cast<float>(start_cell % kGrid) / 7.0f);
        put(static_cast<float>(start_cell / kGrid) / 7.0f);

        // ---- direction / distance vectors [472..486] --------------------
        // (dx, dy) unit vector mapped to [0,1], distance / 2048 px, has-flag.
        const auto put_vector = [&](bool has, i32 fx, i32 fy, i32 tx, i32 ty,
                                    bool with_flag) {
            if (!has) {
                put(0.5f); put(0.5f); put(1.0f);
                if (with_flag) put(0.0f);
                return;
            }
            const double dx = static_cast<double>(tx) - fx;
            const double dy = static_cast<double>(ty) - fy;
            const double len = std::sqrt(dx * dx + dy * dy);
            put(len > 0.0 ? static_cast<float>((dx / len + 1.0) / 2.0) : 0.5f);
            put(len > 0.0 ? static_cast<float>((dy / len + 1.0) / 2.0) : 0.5f);
            put(std::min(static_cast<float>(len / 2048.0), 1.0f));
            if (with_flag) put(1.0f);
        };
        const i32 home_x = std::max(observation.start_x, 0);
        const i32 home_y = std::max(observation.start_y, 0);
        const i32 acx = own_army != 0 ?
            static_cast<i32>(army_centroid_x / static_cast<i64>(own_army)) : home_x;
        const i32 acy = own_army != 0 ?
            static_cast<i32>(army_centroid_y / static_cast<i64>(own_army)) : home_y;
        // army -> nearest visible enemy
        {
            const AiObservedUnit* best = nullptr; i64 best_d = 0;
            for (const AiObservedUnit* h : visible_hostiles) {
                const i64 d = sq_dist(acx, acy, h->x, h->y);
                if (best == nullptr || d < best_d) { best = h; best_d = d; }
            }
            put_vector(best != nullptr, acx, acy, best ? best->x : 0,
                best ? best->y : 0, true);                        // 472..475
        }
        // army -> nearest enemy building (visible or remembered)
        {
            bool has = false; i32 bx = 0, by = 0; i64 best_d = 0;
            for (const AiObservedUnit* h : visible_hostiles) {
                if (h->type_id < kMobileTypeLimit) continue;
                const i64 d = sq_dist(acx, acy, h->x, h->y);
                if (!has || d < best_d) { has = true; best_d = d; bx = h->x; by = h->y; }
            }
            for (const UnitMovementPoint& p : remembered_buildings) {
                const i64 d = sq_dist(acx, acy, p.x, p.y);
                if (!has || d < best_d) { has = true; best_d = d; bx = p.x; by = p.y; }
            }
            put_vector(has, acx, acy, bx, by, true);                // 476..479
        }
        // own start -> nearest UNEXPLORED start candidate (where an enemy
        // base can still be).
        i32 unexplored_x = 0, unexplored_y = 0; bool have_unexplored = false;
        {
            i64 best_d = 0;
            for (u32 slot = 0; slot < 8u; ++slot) {
                if ((observation.start_candidate_mask & (1u << slot)) == 0) continue;
                const i32 sx = observation.start_candidate_x[slot];
                const i32 sy = observation.start_candidate_y[slot];
                if (sx == observation.start_x && sy == observation.start_y) continue;
                if (tiles_valid) {
                    const u32 tx = static_cast<u32>(std::max(sx, 0)) >> 5;
                    const u32 ty = static_cast<u32>(std::max(sy, 0)) >> 5;
                    const std::size_t ti = static_cast<std::size_t>(ty) *
                        observation.map_width_tiles + tx;
                    if (ti < observation.tiles.size() &&
                        observation.tiles[ti].explored) continue;
                }
                const i64 d = sq_dist(home_x, home_y, sx, sy);
                if (!have_unexplored || d < best_d) {
                    have_unexplored = true; best_d = d;
                    unexplored_x = sx; unexplored_y = sy;
                }
            }
            put_vector(have_unexplored, home_x, home_y, unexplored_x,
                unexplored_y, true);                                // 480..483
        }
        // army -> home (nearest own base nest, else any own building, else start)
        {
            bool has = false; i32 bx = home_x, by = home_y; i64 best_d = 0;
            for (int pass = 0; pass < 2 && !has; ++pass) {
                for (const AiObservedUnit& u : observation.units) {
                    if (!u.controlled || !u.alive || u.under_construction ||
                        u.type_id < kMobileTypeLimit) continue;
                    if (pass == 0 && u.type_id != kTypeBaseNest) continue;
                    const i64 d = sq_dist(acx, acy, u.x, u.y);
                    if (!has || d < best_d) { has = true; best_d = d; bx = u.x; by = u.y; }
                }
            }
            put_vector(true, acx, acy, bx, by, false);              // 484..486
        }

        // ---- enemy composition / tribe / stats [487..497] ----------------
        std::array<u32, 5> enemy_roles{};
        std::array<u32, 4> enemy_tribes{};
        u64 enemy_health = 0; u32 enemy_uc = 0; u32 enemy_max_range = 0;
        for (const AiObservedUnit* h : visible_hostiles) {
            enemy_health += h->health;
            if (h->type_id >= kMobileTypeLimit) {
                if (h->under_construction) ++enemy_uc;
            } else {
                ++enemy_roles[unit_role(*h)];
            }
            const i32 tribe = tribe_of(h->type_id);
            if (tribe >= 0 && tribe < 4) ++enemy_tribes[static_cast<std::size_t>(tribe)];
            enemy_max_range = std::max(enemy_max_range, h->attack_range);
        }
        for (std::size_t r = 0; r < 4; ++r) put(norm(enemy_roles[r], 20.0f)); // 487..490 worker/melee/ranged/transport
        {
            std::size_t best = 0; u32 best_n = 0;
            for (std::size_t t = 0; t < 4; ++t) {
                if (enemy_tribes[t] > best_n) { best_n = enemy_tribes[t]; best = t; }
            }
            for (std::size_t t = 0; t < 4; ++t) put(best_n != 0 && t == best ? 1.0f : 0.0f); // 491..494
        }
        put(norm(static_cast<u32>(std::min<u64>(enemy_health, 1000000u)), 10000.0f)); // 495
        put(norm(enemy_uc, 10.0f));                                 // 496
        put(norm(enemy_max_range, 500.0f));                          // 497

        // ---- own army state [498..508] ----------------------------------
        u32 hp_low = 0, hp_mid = 0, hp_high = 0, idle_fighters = 0,
            attacking = 0, far_from_home = 0, own_max_range = 0;
        std::array<u32, 5> own_roles{};
        double spread = 0.0;
        for (const AiObservedUnit* f : own_fighters) {
            if (f->max_health != 0) {
                const u64 pct = static_cast<u64>(f->health) * 100u / f->max_health;
                if (pct < 30u) ++hp_low; else if (pct < 70u) ++hp_mid; else ++hp_high;
            }
            if (unit_is_idle(*f)) ++idle_fighters;
            if (in_attack_state(*f)) ++attacking;
            spread += std::sqrt(static_cast<double>(sq_dist(acx, acy, f->x, f->y)));
            own_max_range = std::max(own_max_range, f->attack_range);
            ++own_roles[unit_role(*f)];
        }
        {
            // distance from the nearest own base (or start) for each fighter
            std::vector<UnitMovementPoint> bases;
            for (const AiObservedUnit& u : observation.units) {
                if (u.controlled && u.alive && !u.under_construction &&
                    u.type_id == kTypeBaseNest) bases.push_back({u.x, u.y});
            }
            if (bases.empty()) bases.push_back({home_x, home_y});
            for (const AiObservedUnit* f : own_fighters) {
                i64 best_d = -1;
                for (const UnitMovementPoint& b : bases) {
                    const i64 d = sq_dist(f->x, f->y, b.x, b.y);
                    if (best_d < 0 || d < best_d) best_d = d;
                }
                if (best_d > 800ll * 800ll) ++far_from_home;
            }
        }
        const float army_f = own_army != 0 ? static_cast<float>(own_army) : 1.0f;
        put(own_army != 0 ? hp_low / army_f : 0.0f);                // 498
        put(own_army != 0 ? hp_mid / army_f : 0.0f);                // 499
        put(own_army != 0 ? hp_high / army_f : 0.0f);               // 500
        put(own_army != 0 ? std::min(static_cast<float>(spread / own_army / 512.0), 1.0f) : 0.0f); // 501
        put(own_army != 0 ? idle_fighters / army_f : 0.0f);         // 502
        put(own_army != 0 ? attacking / army_f : 0.0f);             // 503
        put(own_army != 0 ? far_from_home / army_f : 0.0f);         // 504
        put(norm(own_roles[1], 50.0f));                              // 505 melee
        put(norm(own_roles[2], 50.0f));                              // 506 ranged
        put(norm(own_roles[3], 50.0f));                              // 507 transport
        put(norm(own_max_range, 500.0f));                            // 508

        // ---- production pipeline [509..525] ------------------------------
        // Units queued/in production per type, from every producer's current
        // production and deferred produce commands (state 0x10, value = type).
        static constexpr u32 kPendingTypes[] = {0x20u, 0x21u, 0x22u, 0x24u,
            0x25u, 0x27u, 0x28u, 0x29u, 0x2au, 0x2cu, 0x2eu};
        std::array<u32, 16> pending{};
        std::array<u32, 16> building_uc{};
        for (const AiObservedUnit& u : observation.units) {
            if (!u.controlled || !u.alive) continue;
            if (u.type_id >= 0x80u && u.type_id < 0x90u && u.under_construction) {
                ++building_uc[u.type_id - 0x80u];
            }
            if (u.type_id < kMobileTypeLimit || u.under_construction) continue;
            const u32 q = u.queued_production_type_id;
            if (q >= 0x20u && q < 0x30u) ++pending[q - 0x20u];
            const u32 n = std::min<u32>(u.deferred_command_count,
                static_cast<u32>(u.deferred_commands.size()));
            for (u32 k = 0; k < n; ++k) {
                const AiObservedQueuedCommand& cmd = u.deferred_commands[k];
                const u32 v = static_cast<u32>(cmd.command_value_or_target);
                if ((cmd.state & kUnitCommandStateMask) == 0x10u &&
                    v >= 0x20u && v < 0x30u) ++pending[v - 0x20u];
            }
        }
        for (const u32 t : kPendingTypes) put(norm(pending[t - 0x20u], 5.0f)); // 509..519
        static constexpr u32 kUcBuildings[] = {0x83u, 0x86u, 0x87u, 0x88u, 0x89u, 0x8au};
        for (const u32 b : kUcBuildings) put(norm(building_uc[b - 0x80u], 3.0f)); // 520..525

        // ---- scout [526..530] --------------------------------------------
        const AiObservedUnit* scout = nullptr;
        if (observation.scout_unit_id != 0) {
            for (const AiObservedUnit& u : observation.units) {
                if (u.id == observation.scout_unit_id && u.controlled && u.alive) {
                    scout = &u; break;
                }
            }
        }
        put(scout != nullptr ? 1.0f : 0.0f);                         // 526
        if (scout != nullptr) {
            put(std::clamp(static_cast<float>(
                (static_cast<double>(scout->x - home_x) / map_w + 1.0) / 2.0), 0.0f, 1.0f)); // 527
            put(std::clamp(static_cast<float>(
                (static_cast<double>(scout->y - home_y) / map_h + 1.0) / 2.0), 0.0f, 1.0f)); // 528
            bool sees = false;
            const i64 sight = scout->sight_range != 0 ? scout->sight_range : 160;
            for (const AiObservedUnit* h : visible_hostiles) {
                if (sq_dist(scout->x, scout->y, h->x, h->y) <= sight * sight) { sees = true; break; }
            }
            put(sees ? 1.0f : 0.0f);                                 // 529
            put(have_unexplored ? std::min(static_cast<float>(std::sqrt(
                static_cast<double>(sq_dist(scout->x, scout->y, unexplored_x,
                    unexplored_y))) / 2048.0), 1.0f) : 1.0f);        // 530
        } else {
            put(0.5f); put(0.5f); put(0.0f); put(1.0f);
        }
    }

    // ======================================================================
    // v6 features [531..533] - what the army group is running right now.
    // ======================================================================
    put(objective == 6u ? 1.0f : 0.0f);    // 531 search  (AiMicroObjectiveKind::search + 1)
    // Attack tactic one-hot (units_first = both zero); meaningful with 82.
    put(objective == 2u && observation.army_attack_tactic == 1u ? 1.0f : 0.0f); // 532 buildings_first
    put(objective == 2u && observation.army_attack_tactic == 2u ? 1.0f : 0.0f); // 533 neutral_only

    // ======================================================================
    // v7 features [534..540] - expansion: where the next base nest goes and
    // whether it can be ordered yet, plus the reservation accounting.
    // ======================================================================
    const AiExpansionPlan expansion = ComputeAiExpansionPlan(observation);
    {
        // own start -> next expansion site: dx, dy ((v+1)/2), distance/2048,
        // lit (explored -> expand_base_nest can be ordered).
        const i32 home_x = std::max(observation.start_x, 0);
        const i32 home_y = std::max(observation.start_y, 0);
        if (expansion.has_target) {
            const double dx = static_cast<double>(expansion.target_x - home_x);
            const double dy = static_cast<double>(expansion.target_y - home_y);
            const double length = std::sqrt(dx * dx + dy * dy);
            put(length > 0.0 ? static_cast<float>((dx / length + 1.0) / 2.0) : 0.5f); // 534
            put(length > 0.0 ? static_cast<float>((dy / length + 1.0) / 2.0) : 0.5f); // 535
            put(std::min(static_cast<float>(length / 2048.0), 1.0f));              // 536
            put(expansion.target_explored ? 1.0f : 0.0f);                          // 537
        } else {
            put(0.5f); put(0.5f); put(1.0f); put(0.0f);
        }
        put(observation.berry_scout_unit_id != 0 ? 1.0f : 0.0f);   // 538 berry scout alive
        put(norm(reserved_resources, 1000.0f));                    // 539 reserved by walking builds
        put(norm(builds_in_flight, 5.0f));                         // 540 builds walking
        // A unit stands on the expansion site's footprint (neutral monster,
        // enemy, or own): the engine refuses the placement while it does.
        put(expansion.has_target && expansion.target_blocked ? 1.0f : 0.0f); // 541 site blocked
        // Search split (v7): the single-unit explorer / roamer exist.
        put(observation.explorer_unit_id != 0 ? 1.0f : 0.0f);     // 542 explorer alive
        put(observation.roamer_unit_id != 0 ? 1.0f : 0.0f);       // 543 roamer alive
        // A build order of ours was refused by the placement gate within the
        // back-off window (the mask closes that structure type meanwhile).
        put(observation.last_build_reject_frames_ago < 64u ? 1.0f : 0.0f); // 544 build recently rejected
    }

    // ======================================================================
    // v8 features [545..771] (docs/2순위.md) - enemy composition by type,
    // OP-DP firepower, enemy-army fog memory, terrain channels, raid state.
    // ======================================================================
    {
        const u32 w = observation.map_width_tiles;
        const u32 h = observation.map_height_tiles;
        const bool tiles_ok = w != 0 && h != 0 &&
            observation.tiles.size() == static_cast<std::size_t>(w) * h;
        // [545..560] visible enemy MOBILES by tribe-relative type slot
        // (type_id & 0x0f - the type layout is tribe-symmetric: worker =
        // tribe*0x10, units follow to +0x0f; the dominant-tribe one-hot
        // already in the vector disambiguates the tribe).
        std::array<u32, 16> enemy_slots{};
        for (const AiObservedUnit* hostile : visible_hostiles) {
            if (hostile->type_id < kMobileTypeLimit) {
                ++enemy_slots[hostile->type_id & 0x0fu];
            }
        }
        for (const u32 slot_count : enemy_slots) {
            put(norm(slot_count, 20.0f));                       // 545..560
        }
        // [561..567] firepower by the ENGINE's damage stats (damage = attacker
        // attack_power - defender defense_power): melee/ranged offense sums
        // and anti-air capability per side, and the power-based force ratio.
        // The old health-based ratio [81] stays (append-only); this one ranks
        // 트리세스 4 above 마소스 10 the way real combat does.
        u64 own_melee_power = 0, own_ranged_power = 0;
        bool own_anti_air = false;
        for (const AiObservedUnit* fighter : own_fighters) {
            if (fighter->attack_range_base <= 64u) {
                own_melee_power += fighter->attack_power;
            } else {
                own_ranged_power += fighter->attack_power;
            }
            if ((fighter->attackable_class_mask & (1u << 3)) != 0 &&
                fighter->attack_range_vs_air != 0) {
                own_anti_air = true;
            }
        }
        u64 enemy_melee_power = 0, enemy_ranged_power = 0;
        bool enemy_anti_air = false;
        for (const AiObservedUnit* hostile : visible_hostiles) {
            if (hostile->type_id >= kMobileTypeLimit) {
                continue;
            }
            if (hostile->attack_range_base <= 64u) {
                enemy_melee_power += hostile->attack_power;
            } else {
                enemy_ranged_power += hostile->attack_power;
            }
            if ((hostile->attackable_class_mask & (1u << 3)) != 0 &&
                hostile->attack_range_vs_air != 0) {
                enemy_anti_air = true;
            }
        }
        put(norm(static_cast<float>(own_melee_power), 5000.0f));    // 561
        put(norm(static_cast<float>(own_ranged_power), 5000.0f));   // 562
        put(own_anti_air ? 1.0f : 0.0f);                            // 563
        put(norm(static_cast<float>(enemy_melee_power), 5000.0f));  // 564
        put(norm(static_cast<float>(enemy_ranged_power), 5000.0f)); // 565
        put(enemy_anti_air ? 1.0f : 0.0f);                          // 566
        const u64 own_power = own_melee_power + own_ranged_power;
        const u64 enemy_power = enemy_melee_power + enemy_ranged_power;
        put(own_power + enemy_power == 0 ? 0.5f :
            static_cast<float>(static_cast<double>(own_power) /
                static_cast<double>(own_power + enemy_power)));     // 567
        // [568..631] grid channel 7: enemy-army fog memory density (per-tile
        // last-seen hostile-mobile counts folded into the 8x8 cells).
        constexpr std::size_t kMemCells = 64;
        constexpr u32 kMemGrid = 8;
        std::array<float, kMemCells> cell_army_memory{};
        std::array<u32, kMemCells> cell_passable{};
        std::array<u32, kMemCells> cell_buildable{};
        std::array<u32, kMemCells> cell_tiles{};
        if (tiles_ok) {
            const bool army_memory_ok =
                observation.enemy_army_memory.size() == observation.tiles.size();
            for (u32 ty = 0; ty < h; ++ty) {
                const u32 cy = ty * kMemGrid / h;
                for (u32 tx = 0; tx < w; ++tx) {
                    const std::size_t tile = static_cast<std::size_t>(ty) * w + tx;
                    const std::size_t cell = cy * kMemGrid + tx * kMemGrid / w;
                    ++cell_tiles[cell];
                    if (army_memory_ok) {
                        cell_army_memory[cell] += static_cast<float>(
                            observation.enemy_army_memory[tile]);
                    }
                    if (observation.tiles[tile].passable) {
                        ++cell_passable[cell];
                    }
                    if (observation.tiles[tile].buildable) {
                        ++cell_buildable[cell];
                    }
                }
            }
        }
        for (std::size_t c = 0; c < kMemCells; ++c) {
            put(norm(cell_army_memory[c], 20.0f));              // 568..631
        }
        // [632..635] most recent enemy-army sighting: frames since (1.0 =
        // never seen), direction from own start ((v+1)/2 unit vector), size.
        const bool army_seen =
            observation.enemy_army_seen_frames_ago != 0xffffffffu;
        put(army_seen ?
            norm(observation.enemy_army_seen_frames_ago, 60000.0f) : 1.0f); // 632
        if (army_seen && observation.enemy_army_seen_x >= 0) {
            const double dx = static_cast<double>(
                observation.enemy_army_seen_x - std::max(observation.start_x, 0));
            const double dy = static_cast<double>(
                observation.enemy_army_seen_y - std::max(observation.start_y, 0));
            const double length = std::sqrt(dx * dx + dy * dy);
            put(length > 0.0 ?
                static_cast<float>((dx / length + 1.0) / 2.0) : 0.5f);      // 633
            put(length > 0.0 ?
                static_cast<float>((dy / length + 1.0) / 2.0) : 0.5f);      // 634
        } else {
            put(0.5f);
            put(0.5f);
        }
        put(norm(observation.enemy_army_seen_count, 50.0f));                // 635
        // [636..699] grid channel 8: passable-terrain ratio per cell.
        // [700..763] grid channel 9: buildable ratio per cell.  Terrain is
        // public in the original game (the minimap shows it), so these are
        // not fog-gated.
        for (std::size_t c = 0; c < kMemCells; ++c) {
            put(cell_tiles[c] != 0 ? static_cast<float>(cell_passable[c]) /
                static_cast<float>(cell_tiles[c]) : 0.0f);      // 636..699
        }
        for (std::size_t c = 0; c < kMemCells; ++c) {
            put(cell_tiles[c] != 0 ? static_cast<float>(cell_buildable[c]) /
                static_cast<float>(cell_tiles[c]) : 0.0f);      // 700..763
        }
        // [764..771] raid-group state (docs/1순위.md §1.4): size, aliveness,
        // objective one-hot (kind encoding = AiMicroObjectiveKind + 1, same
        // as army_objective_kind), buildings_first tactic, main-army size,
        // search flag.  Without this the policy cannot see its own raid.
        put(norm(observation.raid_unit_count, 14.0f));                      // 764
        put(observation.raid_unit_count > 0 ? 1.0f : 0.0f);                 // 765
        put(observation.raid_objective_kind == 2u ? 1.0f : 0.0f);           // 766 attack
        put(observation.raid_objective_kind == 3u ? 1.0f : 0.0f);           // 767 defend
        put(observation.raid_objective_kind == 4u ? 1.0f : 0.0f);           // 768 retreat
        put(observation.raid_objective_kind == 2u &&
            observation.raid_attack_tactic == 1u ? 1.0f : 0.0f);            // 769 buildings_first
        put(norm(observation.army_group_unit_count, 50.0f));                // 770
        put(observation.raid_objective_kind == 6u ? 1.0f : 0.0f);           // 771 search
    }

    // ======================================================================
    // v9 decision context [772..787]
    // (docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md) - written as ZEROS here.
    // These describe the decision-gate/pump state, not the observation; the
    // caller patches them via ApplyAiRlDecisionContext:
    //   [772]      frames since the last policy decision (/64 clamp)
    //   [773..784] the 12 AiDecisionTrigger bits that fired this decision
    //   [785..787] autopilot firings since the last decision
    //              (workers / pop nests / fighters, /8 clamp)
    // ======================================================================
    for (std::size_t context = 0; context < 16; ++context) {
        put(0.0f);
    }

    // ======================================================================
    // v10 [788..801] - raid_b / raid_c group state (7 slots each, mirroring
    // the raid slots 764..769,771: size, alive, attack, defend, retreat,
    // buildings_first, search).  Four fighting bodies (user directive).
    // ======================================================================
    for (const auto& squad : {
             std::make_pair(std::make_pair(observation.raid_b_unit_count,
                 observation.raid_b_objective_kind),
                 observation.raid_b_attack_tactic),
             std::make_pair(std::make_pair(observation.raid_c_unit_count,
                 observation.raid_c_objective_kind),
                 observation.raid_c_attack_tactic)}) {
        const u32 count = squad.first.first;
        const u32 kind = squad.first.second;
        const u32 tactic = squad.second;
        put(norm(count, 14.0f));
        put(count > 0 ? 1.0f : 0.0f);
        put(kind == 2u ? 1.0f : 0.0f);                    // attack
        put(kind == 3u ? 1.0f : 0.0f);                    // defend
        put(kind == 4u ? 1.0f : 0.0f);                    // retreat
        put(kind == 2u && tactic == 1u ? 1.0f : 0.0f);    // buildings_first
        put(kind == 6u ? 1.0f : 0.0f);                    // search
    }
    // Map knowledge gates for the split search actions: an unexplored start
    // candidate left (search_enemy_base), and a frontier tile left - an
    // unexplored passable tile bordering explored passable ground
    // (explore_frontier; reachability is the executor's per-unit call, an air
    // explorer reaches everything).
    bool unexplored_start_left = false;
    bool frontier_left = false;
    {
        const bool valid = observation.map_width_tiles != 0 &&
            observation.tiles.size() == static_cast<std::size_t>(
                observation.map_width_tiles) * observation.map_height_tiles;
        const u32 w = observation.map_width_tiles;
        const u32 h = observation.map_height_tiles;
        for (u32 slot = 0; slot < 8u && valid; ++slot) {
            if ((observation.start_candidate_mask & (1u << slot)) == 0) continue;
            const i32 sx = observation.start_candidate_x[slot];
            const i32 sy = observation.start_candidate_y[slot];
            if (sx == observation.start_x && sy == observation.start_y) continue;
            const u32 tx = static_cast<u32>(std::max(sx, 0)) >> 5;
            const u32 ty = static_cast<u32>(std::max(sy, 0)) >> 5;
            if (tx < w && ty < h && !observation.tiles[ty * w + tx].explored) {
                unexplored_start_left = true;
                break;
            }
        }
        for (u32 ty = 0; ty < h && valid && !frontier_left; ++ty) {
            for (u32 tx = 0; tx < w; ++tx) {
                const AiObservedMapTile& tile = observation.tiles[ty * w + tx];
                if (tile.explored || !tile.passable) continue;
                const i32 offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& o : offsets) {
                    const i64 nx = static_cast<i64>(tx) + o[0];
                    const i64 ny = static_cast<i64>(ty) + o[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const AiObservedMapTile& n = observation.tiles[
                        static_cast<std::size_t>(ny) * w + static_cast<std::size_t>(nx)];
                    if (n.explored && n.passable) { frontier_left = true; break; }
                }
                if (frontier_left) break;
            }
        }
    }

    // --- Legal-action mask ---
    auto set_legal = [&](AiRlHighLevelAction a, bool legal) {
        out.legal_mask[static_cast<std::size_t>(a)] = legal ? 1u : 0u;
    };
    // Available = owned - reserved by builds still walking (see
    // walking_build_type_of): the engine debits on arrival, so spending the
    // reserved part now would make that build fail when the worker gets there.
    const u32 prim = observation.primary_resources > reserved_resources ?
        observation.primary_resources - reserved_resources : 0u;
    // Per-unit population gate (audited pop costs): the old single
    // `pop_free > 0` check marked every produce action legal with 1-3 pop
    // left while the validator then rejected the 2-4 pop units — wasted picks.
    const auto pop_ok = [&](u32 pop_cost) {
        return observation.population_limit == 0 || pop_free >= pop_cost;
    };
    const bool has_pop_room = pop_ok(1u);
    const bool has_builder = idle_workers > 0 || harvesting > 0 || workers > 0;
    // Shared placement rule (v7): an ordinary structure is legal only when a
    // statically valid, explored, unblocked site for its footprint exists in
    // the base area - the same site the translator will use, so a legal build
    // is one the planner accepts (no more "mask open, placement rejected").
    const AiExpansionConfig placement_config{};
    // Back-off after a refused build order of the same structure type (the
    // engine placement gate sees fog-hidden units the observation cannot):
    // the type stays illegal for kBuildRejectBackoffFrames.
    constexpr u32 kBuildRejectBackoffFrames = 64u;
    const auto recently_rejected = [&](u32 structure_type) {
        return observation.last_build_reject_type == structure_type &&
            observation.last_build_reject_frames_ago < kBuildRejectBackoffFrames;
    };
    const auto site_ok = [&](u32 structure_type) {
        return !recently_rejected(structure_type) &&
            FindAiBuildSite(observation, structure_type,
                std::max(observation.start_x, 0), std::max(observation.start_y, 0),
                placement_config.base_site_radius_tiles, placement_config).found;
    };
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
        producer_free_base && has_pop_room && prim >= kWorkerCost);
    set_legal(AiRlHighLevelAction::produce_masos,
        producer_free_egg && pop_ok(1u) && prim >= kMasosCost);
    set_legal(AiRlHighLevelAction::produce_dilophos,
        producer_free_egg && pop_ok(2u) && prim >= kDilophosCost);
    set_legal(AiRlHighLevelAction::build_population_nest,
        has_builder && prim >= kPopulationNestCost && site_ok(0x82u));
    set_legal(AiRlHighLevelAction::build_egg_nest,
        has_builder && completed_base && prim >= kEggNestCost && site_ok(0x84u));
    set_legal(AiRlHighLevelAction::build_land_nest,
        has_builder && completed_egg && prim >= kLandNestCost && site_ok(0x85u));
    // v7 expansion chain, taught by the mask: scout_berry while the next
    // expansion site is dark, expand_base_nest once it is lit.  Only one nest
    // build may be WALKING at a time (0x23/0x25 - uncommitted, may still
    // fail); a nest under construction (paid) does not block the next.
    set_legal(AiRlHighLevelAction::scout_berry,
        own_total > 0 && expansion.has_target && !expansion.target_explored);
    set_legal(AiRlHighLevelAction::expand_base_nest,
        has_builder && prim >= kBaseNestCost && expansion.has_target &&
        expansion.target_explored && !expansion.target_blocked &&
        nest_walkers == 0 && !recently_rejected(0x80u));
    // Per-order research: an IDLE completed researcher building of the right
    // type (re-enqueueing onto a busy one re-debits and resets progress),
    // level below the cap, and the audited cost for the current level.
    (void)idle_researcher;
    for (const AiRlResearchAction& entry : kAiRlResearchActions) {
        const u32 level = entry.order < observation.research_order_levels.size()
            ? observation.research_order_levels[entry.order] : 0u;
        const u32 cost = entry.cost_by_level[std::min<u32>(level, 2u)];
        set_legal(entry.action,
            level < entry.max_levels &&
            idle_researcher_by_type[entry.researcher_type - 0x80u] &&
            prim >= cost);
    }
    // scout_map posts an early-warning picket between home and the known
    // enemy base - it needs a known enemy building to stand in front of.
    set_legal(AiRlHighLevelAction::scout_map, own_total > 0 && enemy_base_known);
    // v6: the two enemy attack actions differ only in class priority (units
    // first / buildings first); both need a KNOWN enemy location - a visible
    // hostile or a remembered building - to march on.  search is for when no
    // enemy building is known.
    set_legal(AiRlHighLevelAction::attack_nearest_enemy,
        has_army && (have_nearest || enemy_base_known));
    set_legal(AiRlHighLevelAction::attack_enemy_base,
        has_army && (have_nearest || enemy_base_known));
    // search_enemy_base: the army sweeps the unexplored start candidates;
    // done (masked off) once every candidate has been checked.
    set_legal(AiRlHighLevelAction::search_enemy_base,
        has_army && unexplored_start_left);
    // explore_frontier: one unit walks the frontier; off when none is left.
    // roam_scout: one unit patrols outside the active vision, indefinitely.
    set_legal(AiRlHighLevelAction::explore_frontier,
        own_total > 0 && frontier_left);
    set_legal(AiRlHighLevelAction::roam_scout, own_total > 0);
    // v10.2 defend gate (2026-09-01 user replay report: the policy spammed
    // defend at random): defend is a REACTION - legal only while a visible
    // hostile COMBAT mobile stands near an own building (or near the start
    // when no building stands).  Surprise attacks are the v9 reflex's job,
    // and hold_army/patrol_defense stay open for stances.
    bool defend_threat = false;
    {
        constexpr i64 kDefendGateRadius = 1200;
        std::vector<std::pair<i32, i32>> own_buildings;
        for (const AiObservedUnit& u : observation.units) {
            if (u.controlled && u.alive && u.type_id >= kMobileTypeLimit) {
                own_buildings.push_back({u.x, u.y});
            }
        }
        if (own_buildings.empty()) {
            own_buildings.push_back({std::max(observation.start_x, 0),
                std::max(observation.start_y, 0)});
        }
        for (const AiObservedUnit* hostile : visible_hostiles) {
            if (hostile->type_id >= kMobileTypeLimit ||
                hostile->attack_range == 0) {
                continue;  // buildings / non-combat are not a defend trigger
            }
            for (const std::pair<i32, i32>& building : own_buildings) {
                if (sq_dist(building.first, building.second, hostile->x,
                        hostile->y) <= kDefendGateRadius * kDefendGateRadius) {
                    defend_threat = true;
                    break;
                }
            }
            if (defend_threat) {
                break;
            }
        }
    }
    set_legal(AiRlHighLevelAction::defend_base, has_army && defend_threat);
    set_legal(AiRlHighLevelAction::retreat, has_army);
    // Hunting needs a fighting force and a visible neutral monster the
    // forces can actually REACH (v10 guard: a monster on a disconnected
    // walkable island parked the army at a cliff - such a hunt is illegal).
    const bool huntable_neutral = have_neutral &&
        AiMicroHuntableNeutralExists(observation);
    // v10 hunt distance gate (user directive): the MAIN army only hunts
    // monsters near its centroid - far expeditions are the raid slots' job.
    const AiMicroExecutorConfig micro_defaults{};
    const i32 hunt_cx = observation.army_centroid_x >= 0 ?
        observation.army_centroid_x : std::max(observation.start_x, 0);
    const i32 hunt_cy = observation.army_centroid_y >= 0 ?
        observation.army_centroid_y : std::max(observation.start_y, 0);
    const bool huntable_near_army = have_neutral &&
        AiMicroHuntableNeutralExists(observation, micro_defaults, hunt_cx,
            hunt_cy, micro_defaults.army_hunt_radius);
    set_legal(AiRlHighLevelAction::hunt_neutral_monster,
        has_army && huntable_near_army);
    // Tech-tree extension.  Exact producer/prereqs live in the game tables the
    // live validator consults; the mask pre-gates on the audited cost
    // (ai_techtree_audit.txt) and a plausible completed producer being present.
    // Producer map (session reference tables): egg 0x84 makes the fighter
    // roster, base 0x80 makes 0x2c, 0x87 makes 0x29/0x2a; workers build all
    // 0x8x structures.
    // Audited prerequisite tree (ai_techtree_audit prereq=[] rows): 딜로포스/
    // 에그스로워 need the land nest, 람포스/트윈람포스/둥가리 the sky nest,
    // 프테라스 sky+upgrade, 트리세스 land nisdos, 켄트로스 land+upgrade,
    // 티라노스 every advanced building; the upgrade nest itself needs
    // land+sky, the nisdos pair land/sky + upgrade.
    const bool completed_86 = nest86 > nest86_uc;
    set_legal(AiRlHighLevelAction::produce_unit_x22,
        producer_free_egg && pop_ok(1u) && prim >= kUnit22Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x25,
        producer_free_egg && completed_86 && pop_ok(1u) && prim >= kUnit25Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x27,
        producer_free_egg && completed_86 && nest88 >= 1 && pop_ok(2u) &&
        prim >= kUnit27Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x28,
        producer_free_egg && nest89 >= 1 && pop_ok(4u) && prim >= kUnit28Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x2e,
        producer_free_egg && completed_land && nest88 >= 1 && pop_ok(3u) &&
        prim >= kUnit2eCost);
    // 티라노스 reserves 25 population, so the generic one-slot headroom check
    // is not enough.
    set_legal(AiRlHighLevelAction::produce_unit_x2c,
        producer_free_base && completed_land && completed_86 && nest88 >= 1 &&
        nest89 >= 1 && nest8a >= 1 && prim >= kUnit2cCost &&
        (observation.population_limit == 0 || pop_free >= kUnit2cPopulation));
    set_legal(AiRlHighLevelAction::produce_unit_x29,
        producer_free_87 && completed_86 && pop_ok(2u) && prim >= kUnit29Cost);
    set_legal(AiRlHighLevelAction::produce_unit_x2a,
        producer_free_87 && completed_land && pop_ok(4u) &&
        prim >= kUnit2aCost);
    set_legal(AiRlHighLevelAction::build_nest_x86,
        has_builder && completed_egg && prim >= kNest86Cost && site_ok(0x86u));
    set_legal(AiRlHighLevelAction::build_nest_x87,
        has_builder && completed_egg && prim >= kNest87Cost && site_ok(0x87u));
    set_legal(AiRlHighLevelAction::build_nest_x83,
        has_builder && completed_egg && prim >= kNest83Cost && site_ok(0x83u));
    set_legal(AiRlHighLevelAction::build_nest_x88,
        has_builder && completed_land && completed_86 && prim >= kNest88Cost &&
        site_ok(0x88u));
    set_legal(AiRlHighLevelAction::build_nest_x89,
        has_builder && completed_land && nest88 >= 1 && prim >= kNest89Cost &&
        site_ok(0x89u));
    set_legal(AiRlHighLevelAction::build_nest_x8a,
        has_builder && completed_86 && nest88 >= 1 && prim >= kNest8aCost &&
        site_ok(0x8au));

    // --- v2 actions (audited mechanics) ---
    // Merges need two completed units of the base type (pair) or the exact
    // mutant trio plus its research gate; the planner re-validates capability
    // bits and merge states at execution.
    set_legal(AiRlHighLevelAction::merge_twin_velocis,
        roster_counts[0x22u - 0x20u] >= 2);
    set_legal(AiRlHighLevelAction::merge_twin_rhampos,
        roster_counts[0x25u - 0x20u] >= 2);
    set_legal(AiRlHighLevelAction::merge_twin_pteras,
        roster_counts[0x27u - 0x20u] >= 2);
    set_legal(AiRlHighLevelAction::merge_mutant,
        observation.research_order_levels[0x18u] != 0 &&
        roster_counts[0x24u - 0x20u] >= 1 &&
        roster_counts[0x27u - 0x20u] >= 1 &&
        roster_counts[0x28u - 0x20u] >= 1);
    // Wild-dino morph is UI-gated on research 0x2a; exit needs a morphed unit.
    set_legal(AiRlHighLevelAction::morph_enter_army,
        observation.research_order_levels[0x2au] != 0 && morph_ready > 0);
    set_legal(AiRlHighLevelAction::morph_exit_army, morphed > 0);
    set_legal(AiRlHighLevelAction::stance_on_army, stance_ready > 0);
    set_legal(AiRlHighLevelAction::stance_off_army, stance_active > 0);
    set_legal(AiRlHighLevelAction::hold_army, has_army);
    set_legal(AiRlHighLevelAction::patrol_defense, has_army);
    // Drop attack needs the 둥가리 carrier plus a fighting squad to load.
    set_legal(AiRlHighLevelAction::drop_attack,
        roster_counts[0x29u - 0x20u] >= 1 && has_army);

    // --- v8 raid actions (docs/1순위.md) ---
    // Group sizes come from the executor via the observation; with no
    // executor driving the owner both counts are 0 and every raid action
    // stays closed (e.g. imitation logging of the built-in AI).  Detaching a
    // small main army is a self-defeating split, hence the floor.
    constexpr u32 kRaidDetachArmyFloor = 6u;
    const bool has_raid = observation.raid_unit_count > 0;
    set_legal(AiRlHighLevelAction::detach_raid,
        observation.army_group_unit_count >= kRaidDetachArmyFloor && !has_raid);
    set_legal(AiRlHighLevelAction::merge_raid, has_raid);
    set_legal(AiRlHighLevelAction::raid_attack_units,
        has_raid && (have_nearest || enemy_base_known));
    set_legal(AiRlHighLevelAction::raid_attack_base,
        has_raid && (have_nearest || enemy_base_known));
    set_legal(AiRlHighLevelAction::raid_defend_base,
        has_raid && defend_threat);
    set_legal(AiRlHighLevelAction::raid_retreat, has_raid);
    set_legal(AiRlHighLevelAction::raid_hunt_neutral,
        has_raid && huntable_neutral);
    set_legal(AiRlHighLevelAction::raid_search,
        has_raid && unexplored_start_left);
    // --- v10: the two extra raid slots (four fighting bodies) ---
    // Same gates as the raid, driven by each slot's own count.  Detaching
    // always splits the MAIN army, so every detach shares the army floor.
    {
        struct RaidSlotActions {
            u32 count;
            AiRlHighLevelAction detach;
            AiRlHighLevelAction merge;
            AiRlHighLevelAction attack_units;
            AiRlHighLevelAction attack_base;
            AiRlHighLevelAction defend;
            AiRlHighLevelAction retreat;
            AiRlHighLevelAction hunt;
            AiRlHighLevelAction search;
        };
        const RaidSlotActions slots[2] = {
            {observation.raid_b_unit_count,
                AiRlHighLevelAction::detach_raid_b,
                AiRlHighLevelAction::merge_raid_b,
                AiRlHighLevelAction::raid_b_attack_units,
                AiRlHighLevelAction::raid_b_attack_base,
                AiRlHighLevelAction::raid_b_defend_base,
                AiRlHighLevelAction::raid_b_retreat,
                AiRlHighLevelAction::raid_b_hunt_neutral,
                AiRlHighLevelAction::raid_b_search},
            {observation.raid_c_unit_count,
                AiRlHighLevelAction::detach_raid_c,
                AiRlHighLevelAction::merge_raid_c,
                AiRlHighLevelAction::raid_c_attack_units,
                AiRlHighLevelAction::raid_c_attack_base,
                AiRlHighLevelAction::raid_c_defend_base,
                AiRlHighLevelAction::raid_c_retreat,
                AiRlHighLevelAction::raid_c_hunt_neutral,
                AiRlHighLevelAction::raid_c_search},
        };
        for (const RaidSlotActions& slot : slots) {
            const bool has_slot = slot.count > 0;
            set_legal(slot.detach, observation.army_group_unit_count >=
                kRaidDetachArmyFloor && !has_slot);
            set_legal(slot.merge, has_slot);
            set_legal(slot.attack_units,
                has_slot && (have_nearest || enemy_base_known));
            set_legal(slot.attack_base,
                has_slot && (have_nearest || enemy_base_known));
            set_legal(slot.defend, has_slot && defend_threat);
            set_legal(slot.retreat, has_slot);
            set_legal(slot.hunt, has_slot && huntable_neutral);
            set_legal(slot.search, has_slot && unexplored_start_left);
        }
    }

    // --- v10 attack-commit lock (user directive: an ordered attack must at
    // least MEET the enemy).  While a fighting group is marching under an
    // attack objective with a target and has not entered weapon contact yet
    // (and the commitment has not timed out), its re-tasking actions are
    // masked off; retreat stays open as the escape hatch, and everything
    // unrelated (production, research, other groups) is untouched.
    {
        constexpr u32 kCommitTimeoutFrames = 2400;
        const auto committed = [&](u32 kind, u32 has_target, u32 engaged,
            u32 age) {
            return kind == 2u && has_target != 0 && engaged == 0 &&
                age < kCommitTimeoutFrames;
        };
        const auto lock = [&](AiRlHighLevelAction action) {
            out.legal_mask[static_cast<std::size_t>(action)] = 0u;
        };
        if (committed(observation.army_objective_kind,
                observation.army_attack_has_target,
                observation.army_engaged_since_set,
                observation.army_objective_age)) {
            lock(AiRlHighLevelAction::attack_nearest_enemy);
            lock(AiRlHighLevelAction::attack_enemy_base);
            lock(AiRlHighLevelAction::defend_base);
            lock(AiRlHighLevelAction::hunt_neutral_monster);
            lock(AiRlHighLevelAction::search_enemy_base);
            lock(AiRlHighLevelAction::hold_army);
            lock(AiRlHighLevelAction::patrol_defense);
        }
        struct CommitSlot {
            u32 kind;
            u32 has_target;
            u32 engaged;
            u32 age;
            AiRlHighLevelAction actions[6];
        };
        const CommitSlot commit_slots[3] = {
            {observation.raid_objective_kind,
                observation.raid_attack_has_target,
                observation.raid_engaged_since_set,
                observation.raid_objective_age,
                {AiRlHighLevelAction::raid_attack_units,
                    AiRlHighLevelAction::raid_attack_base,
                    AiRlHighLevelAction::raid_defend_base,
                    AiRlHighLevelAction::raid_hunt_neutral,
                    AiRlHighLevelAction::raid_search,
                    AiRlHighLevelAction::merge_raid}},
            {observation.raid_b_objective_kind,
                observation.raid_b_attack_has_target,
                observation.raid_b_engaged_since_set,
                observation.raid_b_objective_age,
                {AiRlHighLevelAction::raid_b_attack_units,
                    AiRlHighLevelAction::raid_b_attack_base,
                    AiRlHighLevelAction::raid_b_defend_base,
                    AiRlHighLevelAction::raid_b_hunt_neutral,
                    AiRlHighLevelAction::raid_b_search,
                    AiRlHighLevelAction::merge_raid_b}},
            {observation.raid_c_objective_kind,
                observation.raid_c_attack_has_target,
                observation.raid_c_engaged_since_set,
                observation.raid_c_objective_age,
                {AiRlHighLevelAction::raid_c_attack_units,
                    AiRlHighLevelAction::raid_c_attack_base,
                    AiRlHighLevelAction::raid_c_defend_base,
                    AiRlHighLevelAction::raid_c_hunt_neutral,
                    AiRlHighLevelAction::raid_c_search,
                    AiRlHighLevelAction::merge_raid_c}},
        };
        for (const CommitSlot& slot : commit_slots) {
            if (committed(slot.kind, slot.has_target, slot.engaged,
                    slot.age)) {
                for (const AiRlHighLevelAction action : slot.actions) {
                    lock(action);
                }
            }
        }
    }

    // --- v8 spatial-target cell mask ---
    // A cell the owner has any knowledge of: an explored tile, a remembered
    // enemy building, or a map start candidate.  Row-major 8x8 over the map.
    {
        const u32 w = observation.map_width_tiles;
        const u32 h = observation.map_height_tiles;
        const bool valid = w != 0 && h != 0 &&
            observation.tiles.size() == static_cast<std::size_t>(w) * h;
        const bool memory_valid = valid &&
            observation.enemy_building_memory.size() == observation.tiles.size();
        if (valid) {
            for (u32 ty = 0; ty < h; ++ty) {
                const u32 cy = ty * kAiRlTargetGridWidth / h;
                for (u32 tx = 0; tx < w; ++tx) {
                    const std::size_t tile = static_cast<std::size_t>(ty) * w + tx;
                    if (!observation.tiles[tile].explored &&
                        !(memory_valid &&
                            observation.enemy_building_memory[tile] != 0)) {
                        continue;
                    }
                    const u32 cx = tx * kAiRlTargetGridWidth / w;
                    out.target_mask[cy * kAiRlTargetGridWidth + cx] = 1u;
                }
            }
            for (u32 slot = 0; slot < 8u; ++slot) {
                if ((observation.start_candidate_mask & (1u << slot)) == 0) {
                    continue;
                }
                const u32 tx = static_cast<u32>(
                    std::max(observation.start_candidate_x[slot], 0)) >> 5;
                const u32 ty = static_cast<u32>(
                    std::max(observation.start_candidate_y[slot], 0)) >> 5;
                if (tx >= w || ty >= h) {
                    continue;
                }
                const u32 cx = tx * kAiRlTargetGridWidth / w;
                const u32 cy = ty * kAiRlTargetGridWidth / h;
                out.target_mask[cy * kAiRlTargetGridWidth + cx] = 1u;
            }
        }
        // With no legal cell at all the spatial actions themselves close
        // (never happens once the own base is explored; belt and braces).
        bool any_cell = false;
        for (const std::uint8_t cell : out.target_mask) {
            if (cell != 0) {
                any_cell = true;
                break;
            }
        }
        if (!any_cell) {
            for (std::size_t a = 0; a < kAiRlActionCount; ++a) {
                if (AiRlActionTakesTargetCell(
                        static_cast<AiRlHighLevelAction>(a))) {
                    out.legal_mask[a] = 0u;
                }
            }
        }
    }

    return out;
}

} // namespace ranker
