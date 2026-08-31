#pragma once

#include "ranker_ai_observation.h"

#include <vector>

namespace ranker {

// Expansion planning (docs/AI_PLAY_MICRO_EXECUTOR_DESIGN.md §7): where the
// next base nest should go.  Pure calculation over the observation - no
// state, no timing.  The policy decides WHEN (scout_berry / expand_base_nest);
// this decides WHERE and whether that place is ready.
//
// Berry positions and initial amounts are map data and public (the
// observation's resource memory is seeded from the map), so the clusters are
// known from frame 0.  Only "is the site explored" and depletion are fog
// dependent.

struct AiExpansionConfig {
    // Candidate sites are searched inside the cluster's bounding box grown by
    // this many tiles on every side.
    i32 site_margin_tiles = 12;
    // Berry tiles within this Chebyshev distance belong to ONE cluster.
    // 1 = strict 8-neighbour connectivity, which split visually-adjacent
    // patches (1-2 empty tiles apart) into separate clusters — the site then
    // optimised against only one patch and ignored the berries next door
    // (2026-08-31 user replay review: nest not at the berry-optimal spot).
    // 3 bridges gaps of up to two empty tiles, so the site minimises the
    // amount-weighted distance over the whole neighbourhood patch group.
    i32 cluster_merge_gap_tiles = 3;
    // Engine rule for base nests (find_nearby_passable_placement_tile): no
    // berry (harvest terrain) tile within +-4 tiles of ANY footprint cell.
    // The footprint grown by this clearance must be berry-free.
    i32 berry_clearance_tiles = 4;
    // Ordinary structures are placed within this many tiles of the own start
    // (nearest valid site); the mask and the translator use the same search.
    i32 base_site_radius_tiles = 16;
    // A cluster is "developed" when an own base nest (completed, under
    // construction, or a worker walking to build one) is within this many px
    // of the cluster's site.
    i32 developed_radius = 512;
    u32 base_type_id = 0x80;
    // Base nest footprint in tiles, anchored at the site tile (top-left, the
    // engine's find_large_footprint_tile convention).  Every footprint tile
    // must be buildable, berry-free, and share the anchor's placement class;
    // the ring around the footprint must be berry-free (harvest paths).
    // 티라노 네스트 definition 0x80: 6x4 (headless log "nest_footprint=6x4").
    u32 footprint_width_tiles = 6;
    u32 footprint_height_tiles = 4;
    // v9 local-connectivity verification (user replay report: buildings
    // sealed unit paths / expansion nests placed across a cliff from the
    // berries).  BFS window = footprint grown by this margin; only this many
    // best candidates are BFS-verified per search (cost bound).
    i32 path_window_margin_tiles = 8;
    u32 path_verify_max_candidates = 24;
};

struct AiBerryCluster {
    // Sum of the currently KNOWN amounts (map initial for never-seen tiles,
    // last-seen otherwise).  A fully harvested cluster disappears.
    u32 known_amount = 0;
    u32 tile_count = 0;
    i32 centroid_x = 0;
    i32 centroid_y = 0;
    // Best nest site for this cluster: the buildable tile minimising the
    // amount-weighted distance sum to the cluster's berry tiles.  -1 = no
    // buildable tile around the cluster.
    i32 site_x = -1;
    i32 site_y = -1;
    bool site_explored = false;
    // A unit (any owner - a neutral monster guarding the patch, an enemy, or
    // one of ours) stands on the nest footprint: the engine placement gate
    // refuses the site while it does (route collision flag), so expanding
    // there has to wait or the blocker must be removed (hunt / attack).
    bool site_blocked = false;
    bool developed = false;
    // Squared distance from the own start to the site (candidate ordering).
    i64 site_distance_from_start_sq = 0;
};

struct AiExpansionPlan {
    std::vector<AiBerryCluster> clusters;
    // The next expansion target: the nearest (to the own start) undeveloped
    // cluster that has a site.  has_target = false when every cluster is
    // developed or unbuildable.
    bool has_target = false;
    std::size_t target_index = 0;
    i32 target_x = -1;
    i32 target_y = -1;
    bool target_explored = false;
    bool target_blocked = false;
    // Own base nests counted for the developed test: completed + under
    // construction + walking builders (see AiWalkingBuildTypeOf).
    u32 nest_count = 0;
    u32 nest_walkers = 0;
};

// Structure type a controlled mobile unit is walking to build (command state
// 0x23 placement-start / 0x25 placement-approach); 0 when it is not.  The
// engine debits the cost only on arrival, so such a walk is an uncommitted
// claim on the owner's resources.  State 0x23 still carries the raw
// building-table index (type - 0x60); 0x25 the converted type id.
u32 AiWalkingBuildTypeOf(const AiObservedUnit& unit);

AiExpansionPlan ComputeAiExpansionPlan(const AiObservation& observation,
    const AiExpansionConfig& config = {});

// ---- shared placement planning (every structure, not only the nest) -------

// Footprint of a Tyrano structure type in tiles (measured from the unit
// definitions, headless log "ai-expand: footprint").  Unknown type -> 1x1.
struct AiBuildingFootprint {
    u32 width = 1;
    u32 height = 1;
};
AiBuildingFootprint AiBuildingFootprintOf(u32 type_id);
// Interaction bounds (px) of a structure type; a walking builder's path
// target = site + bounds/2 (used to recover pending sites).  0x0 unknown.
AiBuildingFootprint AiBuildingInteractionOf(u32 type_id);

// Static placement validity of anchoring `type_id`'s footprint at a tile,
// mirroring the engine gate's static part: every footprint tile buildable,
// berry-free and of the anchor's placement class; base nests additionally
// berry-free within +-berry_clearance_tiles of the footprint.  Optionally
// every footprint tile explored (the planner's fog gate checks the anchor;
// requiring the whole footprint keeps the walk from ending in the dark).
// `blocked` reports a visible alive unit standing on the footprint (the
// engine refuses the placement while one does).
bool AiBuildSiteCandidateOk(const AiObservation& observation, u32 type_id,
    i32 tile_x, i32 tile_y, bool require_explored, bool* blocked,
    const AiExpansionConfig& config = {});

// v9 chosen-site verification: with the candidate footprint blocked, every
// open (passable, unoccupied) cell touching its one-tile ring must stay
// mutually connected inside the local window - a placement that splits its
// neighbourhood (U-yard closure, corridor sealing, penning produced units)
// is refused.  With require_berry_reach the connected region must also touch
// a cell adjacent to a berry tile (expansion sites: the nest must be on the
// same walkable side as its berries - the hill/cliff guard).  Runs only on
// the finally-chosen candidates (BFS is too costly per spiral probe).
bool AiBuildSiteKeepsLocalPaths(const AiObservation& observation,
    const std::vector<u8>& occupancy, u32 type_id, i32 tile_x, i32 tile_y,
    bool require_berry_reach, const AiExpansionConfig& config = {});

// Per-tile occupancy by visible structures (their whole footprint, any
// owner): a candidate footprint overlapping one is invalid, matching the
// engine's footprint-occupied gate.  One byte per map tile; computed once
// per planning call and passed to the overload below.
std::vector<u8> AiBuildOccupancyGrid(const AiObservation& observation);
bool AiBuildSiteCandidateOk(const AiObservation& observation,
    const std::vector<u8>& occupancy, u32 type_id, i32 tile_x, i32 tile_y,
    bool require_explored, bool* blocked, const AiExpansionConfig& config = {});

struct AiBuildSite {
    bool found = false;
    i32 x = -1;         // world px of the anchor tile centre
    i32 y = -1;
    // The nearest statically valid site is currently blocked by a unit
    // (found = false then; the policy can wait or clear it).
    bool nearest_blocked = false;
};

// Nearest valid (explored, unblocked) site for `type_id` within
// `radius_tiles` of a centre - the base-area placement every ordinary
// structure uses (centre = own start).  Deterministic: distance, then tile
// index.
AiBuildSite FindAiBuildSite(const AiObservation& observation, u32 type_id,
    i32 center_x, i32 center_y, i32 radius_tiles,
    const AiExpansionConfig& config = {});

} // namespace ranker
