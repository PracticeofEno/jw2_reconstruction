#include "ranker_ai_expansion.h"

#include <algorithm>
#include <cmath>

namespace ranker {
namespace {

constexpr u32 kMobileTypeLimitLocal = 0x60u;
constexpr i32 kTilePixels = 32;

i64 squared_distance(i32 x0, i32 y0, i32 x1, i32 y1) {
    const i64 dx = static_cast<i64>(x0) - x1;
    const i64 dy = static_cast<i64>(y0) - y1;
    return dx * dx + dy * dy;
}

struct TilePoint {
    u32 tile_x;
    u32 tile_y;
};

bool tiles_valid(const AiObservation& observation) {
    return observation.map_width_tiles != 0 && observation.map_height_tiles != 0 &&
        observation.tiles.size() ==
            static_cast<std::size_t>(observation.map_width_tiles) *
                observation.map_height_tiles;
}

const AiObservedMapTile& tile_at(const AiObservation& observation, u32 x, u32 y) {
    return observation.tiles[
        static_cast<std::size_t>(y) * observation.map_width_tiles + x];
}

// Harvestable now (known amount left) - what a cluster is made of.
bool is_berry(const AiObservation& observation, u32 x, u32 y) {
    return tile_at(observation, x, y).resource_amount != 0;
}

// Berry TERRAIN (engine class 0x100), amount or not - what the placement
// gate's nearby-berry probe rejects.  A harvested-out patch still blocks a
// base nest; the observation's static terrain flags carry the class.
bool is_berry_terrain(const AiObservation& observation, u32 x, u32 y) {
    return (tile_at(observation, x, y).terrain_flags & 0x700u) == 0x100u;
}

} // namespace

u32 AiWalkingBuildTypeOf(const AiObservedUnit& unit) {
    if (!unit.controlled || !unit.alive ||
        unit.type_id >= kMobileTypeLimitLocal) {
        return 0u;
    }
    const u32 state = unit.command_state & 0x00ffffffu;
    if (state != 0x23u && state != 0x25u) {
        return 0u;
    }
    return unit.command_value < kMobileTypeLimitLocal ?
        unit.command_value + kMobileTypeLimitLocal : unit.command_value;
}

// Interaction bounds (px) of a Tyrano structure type (headless "ai-expand:
// footprint ... interaction=WxH").  A walking builder's path target is the
// site plus half of these (offset_spawn_target_by_interaction_bounds), so
// the pending site's anchor tile = (path_target - bounds/2) >> 5.
AiBuildingFootprint AiBuildingInteractionOf(u32 type_id) {
    switch (type_id) {
    case 0x80u: return {203, 140};
    case 0x82u: return {115, 86};
    case 0x83u: return {79, 71};
    case 0x84u: return {137, 92};
    case 0x85u: return {141, 128};
    case 0x86u: return {145, 140};
    case 0x87u: return {158, 108};
    case 0x88u: return {158, 109};
    case 0x89u: return {147, 154};
    case 0x8au: return {140, 164};
    case 0x8fu: return {118, 107};
    default: return {0, 0};
    }
}

AiBuildingFootprint AiBuildingFootprintOf(u32 type_id) {
    // Measured from the unit definitions (headless "ai-expand: footprint"):
    switch (type_id) {
    case 0x80u: return {6, 4};   // 티라노 네스트
    case 0x82u: return {3, 2};   // 네스트 (인구)
    case 0x83u: return {2, 2};   // 에스코모이드
    case 0x84u: return {4, 2};   // 에그 네스트
    case 0x85u: return {4, 3};   // 랜드 네스트
    case 0x86u: return {4, 3};   // 스카이 네스트
    case 0x87u: return {5, 3};   // 스로우 네스트
    case 0x88u: return {5, 3};   // 업그레이드 네스트
    case 0x89u: return {4, 3};   // 랜드 니스도스
    case 0x8au: return {4, 3};   // 스카이 니스도스
    case 0x8fu: return {4, 3};   // 서머닝 네스트
    default: return {1, 1};
    }
}

std::vector<u8> AiBuildOccupancyGrid(const AiObservation& observation) {
    std::vector<u8> occupancy;
    if (!tiles_valid(observation)) {
        return occupancy;
    }
    const i64 width = observation.map_width_tiles;
    const i64 height = observation.map_height_tiles;
    occupancy.assign(observation.tiles.size(), 0u);
    for (const AiObservedUnit& unit : observation.units) {
        if (!unit.alive || !unit.visible || unit.type_id < kMobileTypeLimitLocal) {
            continue;
        }
        // Structure footprint anchored at the unit's tile (top-left, the
        // engine's footprint_world_to_tile convention).  Unknown (other
        // tribe) types get the common 4x3 so at least the bulk is covered.
        AiBuildingFootprint footprint = AiBuildingFootprintOf(unit.type_id);
        if (footprint.width == 1 && footprint.height == 1) {
            footprint = {4, 3};
        }
        const i64 ax = unit.x >> 5;
        const i64 ay = unit.y >> 5;
        for (i64 y = ay; y < ay + footprint.height; ++y) {
            for (i64 x = ax; x < ax + footprint.width; ++x) {
                if (x >= 0 && y >= 0 && x < width && y < height) {
                    occupancy[static_cast<std::size_t>(y) * width +
                        static_cast<std::size_t>(x)] = 1u;
                }
            }
        }
    }
    // Pending sites: a worker walking to build reserves its footprint in the
    // engine (placement TemporaryBlock) until it arrives, so ordering another
    // structure onto it is refused.  The anchor is recovered from the walk
    // target (site + interaction/2); one tile of margin covers the rounding.
    for (const AiObservedUnit& unit : observation.units) {
        const u32 pending_type = AiWalkingBuildTypeOf(unit);
        if (pending_type == 0) {
            continue;
        }
        const AiBuildingFootprint footprint = AiBuildingFootprintOf(pending_type);
        const AiBuildingFootprint bounds = AiBuildingInteractionOf(pending_type);
        const i64 ax = (unit.path_target_x - static_cast<i32>(bounds.width) / 2) >> 5;
        const i64 ay = (unit.path_target_y - static_cast<i32>(bounds.height) / 2) >> 5;
        for (i64 y = ay - 1; y <= ay + footprint.height; ++y) {
            for (i64 x = ax - 1; x <= ax + footprint.width; ++x) {
                if (x >= 0 && y >= 0 && x < width && y < height) {
                    occupancy[static_cast<std::size_t>(y) * width +
                        static_cast<std::size_t>(x)] = 1u;
                }
            }
        }
    }
    return occupancy;
}


bool AiBuildSiteKeepsLocalPaths(const AiObservation& observation,
    const std::vector<u8>& occupancy, u32 type_id, i32 tile_x, i32 tile_y,
    bool require_berry_reach, const AiExpansionConfig& config) {
    if (!tiles_valid(observation) || tile_x < 0 || tile_y < 0) {
        return false;
    }
    const i64 width = observation.map_width_tiles;
    const i64 height = observation.map_height_tiles;
    const AiBuildingFootprint footprint = AiBuildingFootprintOf(type_id);
    const i64 fw = std::max<u32>(footprint.width, 1u);
    const i64 fh = std::max<u32>(footprint.height, 1u);
    const i64 tx = tile_x;
    const i64 ty = tile_y;
    if (tx + fw > width || ty + fh > height) {
        return false;
    }
    const i64 margin = std::max(config.path_window_margin_tiles, 1);
    const i64 win_lo_x = std::max<i64>(tx - margin, 0);
    const i64 win_lo_y = std::max<i64>(ty - margin, 0);
    const i64 win_hi_x = std::min<i64>(tx + fw - 1 + margin, width - 1);
    const i64 win_hi_y = std::min<i64>(ty + fh - 1 + margin, height - 1);
    const i64 win_w = win_hi_x - win_lo_x + 1;
    const i64 win_h = win_hi_y - win_lo_y + 1;
    const auto in_footprint = [&](i64 cell_x, i64 cell_y) {
        return cell_x >= tx && cell_x < tx + fw && cell_y >= ty &&
            cell_y < ty + fh;
    };
    const auto open_cell = [&](i64 cell_x, i64 cell_y) {
        if (cell_x < win_lo_x || cell_y < win_lo_y || cell_x > win_hi_x ||
            cell_y > win_hi_y || in_footprint(cell_x, cell_y)) {
            return false;
        }
        const AiObservedMapTile& tile = tile_at(observation,
            static_cast<u32>(cell_x), static_cast<u32>(cell_y));
        if (!tile.passable) {
            return false;
        }
        return occupancy.size() != observation.tiles.size() ||
            occupancy[static_cast<std::size_t>(cell_y) * width + cell_x] == 0;
    };
    // Open cells on the one-tile ring around the footprint: the endpoints the
    // placement must keep mutually connected.
    std::vector<std::pair<i64, i64>> ring_open;
    for (i64 cell_x = tx - 1; cell_x <= tx + fw; ++cell_x) {
        if (open_cell(cell_x, ty - 1)) ring_open.push_back({cell_x, ty - 1});
        if (open_cell(cell_x, ty + fh)) ring_open.push_back({cell_x, ty + fh});
    }
    for (i64 cell_y = ty; cell_y <= ty + fh - 1; ++cell_y) {
        if (open_cell(tx - 1, cell_y)) ring_open.push_back({tx - 1, cell_y});
        if (open_cell(tx + fw, cell_y)) ring_open.push_back({tx + fw, cell_y});
    }
    if (ring_open.empty()) {
        return false;  // fully sealed in - nothing could even reach the site
    }
    // 4-connected BFS over the window from the first ring cell.
    std::vector<u8> visited(static_cast<std::size_t>(win_w * win_h), 0u);
    const auto visit_index = [&](i64 cell_x, i64 cell_y) {
        return static_cast<std::size_t>((cell_y - win_lo_y) * win_w +
            (cell_x - win_lo_x));
    };
    std::vector<std::pair<i64, i64>> queue;
    queue.push_back(ring_open.front());
    visited[visit_index(ring_open.front().first, ring_open.front().second)] = 1u;
    bool berry_reached = false;
    const auto touches_berry = [&](i64 cell_x, i64 cell_y) {
        for (i32 dy = -1; dy <= 1; ++dy) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                const i64 nx = cell_x + dx;
                const i64 ny = cell_y + dy;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                    continue;
                }
                if (is_berry_terrain(observation, static_cast<u32>(nx),
                        static_cast<u32>(ny)) ||
                    is_berry(observation, static_cast<u32>(nx),
                        static_cast<u32>(ny))) {
                    return true;
                }
            }
        }
        return false;
    };
    while (!queue.empty()) {
        const std::pair<i64, i64> cell = queue.back();
        queue.pop_back();
        if (require_berry_reach && !berry_reached &&
            touches_berry(cell.first, cell.second)) {
            berry_reached = true;
        }
        static const i32 kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& step : kSteps) {
            const i64 nx = cell.first + step[0];
            const i64 ny = cell.second + step[1];
            if (!open_cell(nx, ny) || visited[visit_index(nx, ny)] != 0) {
                continue;
            }
            visited[visit_index(nx, ny)] = 1u;
            queue.push_back({nx, ny});
        }
    }
    for (const std::pair<i64, i64>& cell : ring_open) {
        if (visited[visit_index(cell.first, cell.second)] == 0) {
            return false;  // the placement would split its neighbourhood
        }
    }
    return !require_berry_reach || berry_reached;
}

bool AiBuildSiteCandidateOk(const AiObservation& observation, u32 type_id,
    i32 tile_x, i32 tile_y, bool require_explored, bool* blocked,
    const AiExpansionConfig& config) {
    return AiBuildSiteCandidateOk(observation, AiBuildOccupancyGrid(observation),
        type_id, tile_x, tile_y, require_explored, blocked, config);
}

bool AiBuildSiteCandidateOk(const AiObservation& observation,
    const std::vector<u8>& occupancy, u32 type_id, i32 tile_x, i32 tile_y,
    bool require_explored, bool* blocked, const AiExpansionConfig& config,
    bool* ring_fully_open) {
    if (blocked != nullptr) {
        *blocked = false;
    }
    if (ring_fully_open != nullptr) {
        *ring_fully_open = false;
    }
    if (!tiles_valid(observation) || tile_x < 0 || tile_y < 0) {
        return false;
    }
    const i64 width = observation.map_width_tiles;
    const i64 height = observation.map_height_tiles;
    const AiBuildingFootprint footprint = AiBuildingFootprintOf(type_id);
    const i64 fw = std::max<u32>(footprint.width, 1u);
    const i64 fh = std::max<u32>(footprint.height, 1u);
    const i64 tx = tile_x;
    const i64 ty = tile_y;
    if (tx + fw > width || ty + fh > height) {
        return false;
    }
    const AiObservedMapTile& anchor =
        tile_at(observation, static_cast<u32>(tx), static_cast<u32>(ty));
    const i64 clearance = type_id == config.base_type_id ?
        std::max(config.berry_clearance_tiles, 0) : 0;
    for (i64 fy = ty - clearance; fy <= ty + fh - 1 + clearance; ++fy) {
        for (i64 fx = tx - clearance; fx <= tx + fw - 1 + clearance; ++fx) {
            if (fx < 0 || fy < 0 || fx >= width || fy >= height) {
                continue;
            }
            const u32 ux = static_cast<u32>(fx);
            const u32 uy = static_cast<u32>(fy);
            if (is_berry_terrain(observation, ux, uy) || is_berry(observation, ux, uy)) {
                return false;  // berry terrain inside the footprint / clearance
            }
            const bool inside = fx >= tx && fx < tx + fw && fy >= ty && fy < ty + fh;
            if (!inside) {
                continue;
            }
            const AiObservedMapTile& part = tile_at(observation, ux, uy);
            if (!part.buildable || part.placement_class != anchor.placement_class) {
                return false;
            }
            if (occupancy.size() == observation.tiles.size() &&
                occupancy[static_cast<std::size_t>(uy) * width + ux] != 0) {
                return false;  // a standing structure's footprint
            }
            if (require_explored && !part.explored) {
                return false;
            }
        }
    }
    // Corridor guard (user replay report: buildings walled off berry access /
    // movement).  Walk the one-tile ring around the footprint in cyclic order
    // and count contiguous BLOCKED arcs (map edge, impassable terrain - berry
    // fields and cliffs - or a standing/pending structure).  0 or 1 arc means
    // the building can be walked around through the remaining open arc, so it
    // cannot seal anything on its own; >= 2 arcs means the footprint would
    // BRIDGE two separate obstacles - exactly the placement that turns a
    // passage into a wall - and is refused.  Local and cheap (~2(w+h)+4
    // cells), shared by the mask and the translator like every other rule.
    {
        std::vector<u8> ring_blocked;
        ring_blocked.reserve(static_cast<std::size_t>(2 * (fw + fh) + 4));
        const auto push_cell = [&](i64 cx, i64 cy) {
            bool cell_blocked = cx < 0 || cy < 0 || cx >= width || cy >= height;
            if (!cell_blocked) {
                const AiObservedMapTile& tile = tile_at(observation,
                    static_cast<u32>(cx), static_cast<u32>(cy));
                cell_blocked = !tile.passable ||
                    (occupancy.size() == observation.tiles.size() &&
                        occupancy[static_cast<std::size_t>(cy) * width + cx]
                            != 0);
            }
            ring_blocked.push_back(cell_blocked ? 1u : 0u);
        };
        for (i64 cx = tx - 1; cx <= tx + fw; ++cx) push_cell(cx, ty - 1);
        for (i64 cy = ty; cy <= ty + fh - 1; ++cy) push_cell(tx + fw, cy);
        for (i64 cx = tx + fw; cx >= tx - 1; --cx) push_cell(cx, ty + fh);
        for (i64 cy = ty + fh - 1; cy >= ty; --cy) push_cell(tx - 1, cy);
        u32 arcs = 0;
        bool any_open = false;
        bool any_blocked = false;
        for (std::size_t index = 0; index < ring_blocked.size(); ++index) {
            const u8 current = ring_blocked[index];
            const u8 previous = ring_blocked[
                (index + ring_blocked.size() - 1) % ring_blocked.size()];
            if (current != 0 && previous == 0) {
                ++arcs;
            }
            any_open = any_open || current == 0;
            any_blocked = any_blocked || current != 0;
        }
        if (!any_open || arcs >= 2) {
            return false;
        }
        if (ring_fully_open != nullptr) {
            *ring_fully_open = !any_blocked;
        }
    }
    if (blocked != nullptr) {
        for (const AiObservedUnit& unit : observation.units) {
            if (!unit.alive || !unit.visible) {
                continue;
            }
            const i64 ux = unit.x >> 5;
            const i64 uy = unit.y >> 5;
            if (ux >= tx && ux < tx + fw && uy >= ty && uy < ty + fh) {
                *blocked = true;
                break;
            }
        }
    }
    return true;
}

AiBuildSite FindAiBuildSite(const AiObservation& observation, u32 type_id,
    i32 center_x, i32 center_y, i32 radius_tiles,
    const AiExpansionConfig& config) {
    AiBuildSite site;
    if (!tiles_valid(observation)) {
        return site;
    }
    const i64 width = observation.map_width_tiles;
    const i64 height = observation.map_height_tiles;
    const i64 cx = std::max(center_x, 0) >> 5;
    const i64 cy = std::max(center_y, 0) >> 5;
    const i64 radius = std::max(radius_tiles, 0);
    const std::vector<u8> occupancy = AiBuildOccupancyGrid(observation);
    i64 best_distance = 0;
    i64 best_blocked_distance = 0;
    bool have_blocked = false;
    // Cheap static checks first; the chosen candidates are then BFS-verified
    // (nearest first) so a site never seals its own neighbourhood.
    struct Candidate {
        i64 tx;
        i64 ty;
        i64 distance;
        bool ring_open;
    };
    std::vector<Candidate> candidates;
    for (i64 ty = std::max<i64>(cy - radius, 0);
         ty <= std::min<i64>(cy + radius, height - 1); ++ty) {
        for (i64 tx = std::max<i64>(cx - radius, 0);
             tx <= std::min<i64>(cx + radius, width - 1); ++tx) {
            bool blocked = false;
            bool ring_open = false;
            if (!AiBuildSiteCandidateOk(observation, occupancy, type_id,
                    static_cast<i32>(tx), static_cast<i32>(ty), true, &blocked,
                    config, &ring_open)) {
                continue;
            }
            const i32 world_x = static_cast<i32>(tx) * kTilePixels + kTilePixels / 2;
            const i32 world_y = static_cast<i32>(ty) * kTilePixels + kTilePixels / 2;
            const i64 distance = squared_distance(world_x, world_y,
                std::max(center_x, 0), std::max(center_y, 0));
            if (blocked) {
                if (!have_blocked || distance < best_blocked_distance) {
                    have_blocked = true;
                    best_blocked_distance = distance;
                }
                continue;
            }
            candidates.push_back({tx, ty, distance, ring_open});
        }
    }
    // v10 (user replay report): distance-first placement stacked structures
    // flush against each other into walls.  A site whose whole one-tile ring
    // stays walkable ranks FIRST, so every building keeps a walk-around gap
    // whenever the base area allows one; flush sites remain the fallback.
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.ring_open != rhs.ring_open) {
                return lhs.ring_open;
            }
            if (lhs.distance != rhs.distance) {
                return lhs.distance < rhs.distance;
            }
            return lhs.ty != rhs.ty ? lhs.ty < rhs.ty : lhs.tx < rhs.tx;
        });
    u32 verified = 0;
    for (const Candidate& candidate : candidates) {
        if (verified >= config.path_verify_max_candidates) {
            break;
        }
        ++verified;
        if (!AiBuildSiteKeepsLocalPaths(observation, occupancy, type_id,
                static_cast<i32>(candidate.tx), static_cast<i32>(candidate.ty),
                false, config)) {
            continue;
        }
        site.found = true;
        best_distance = candidate.distance;
        site.x = static_cast<i32>(candidate.tx) * kTilePixels + kTilePixels / 2;
        site.y = static_cast<i32>(candidate.ty) * kTilePixels + kTilePixels / 2;
        break;
    }
    site.nearest_blocked = have_blocked &&
        (!site.found || best_blocked_distance < best_distance);
    return site;
}

AiExpansionPlan ComputeAiExpansionPlan(const AiObservation& observation,
    const AiExpansionConfig& config) {
    AiExpansionPlan plan;
    if (!tiles_valid(observation)) {
        return plan;
    }
    const u32 width = observation.map_width_tiles;
    const u32 height = observation.map_height_tiles;

    // ---- own nests: completed, under construction, and walking builders --
    std::vector<UnitMovementPoint> nests;
    for (const AiObservedUnit& unit : observation.units) {
        if (!unit.controlled || !unit.alive) {
            continue;
        }
        if (unit.type_id == config.base_type_id) {
            nests.push_back({unit.x, unit.y});
            continue;
        }
        if (AiWalkingBuildTypeOf(unit) == config.base_type_id) {
            nests.push_back({unit.path_target_x, unit.path_target_y});
            ++plan.nest_walkers;
        }
    }
    plan.nest_count = static_cast<u32>(nests.size());
    const std::vector<u8> occupancy = AiBuildOccupancyGrid(observation);

    // ---- clusters: 8-connected components of berry tiles -----------------
    std::vector<u32> label(observation.tiles.size(), 0u);
    std::vector<TilePoint> stack;
    u32 next_label = 0;
    for (u32 seed_y = 0; seed_y < height; ++seed_y) {
        for (u32 seed_x = 0; seed_x < width; ++seed_x) {
            const std::size_t seed_index =
                static_cast<std::size_t>(seed_y) * width + seed_x;
            if (!is_berry(observation, seed_x, seed_y) || label[seed_index] != 0) {
                continue;
            }
            ++next_label;
            AiBerryCluster cluster;
            std::vector<TilePoint> members;
            i64 sum_x = 0;
            i64 sum_y = 0;
            u32 min_x = seed_x, max_x = seed_x, min_y = seed_y, max_y = seed_y;
            label[seed_index] = next_label;
            stack.clear();
            stack.push_back({seed_x, seed_y});
            while (!stack.empty()) {
                const TilePoint tile = stack.back();
                stack.pop_back();
                members.push_back(tile);
                cluster.known_amount +=
                    tile_at(observation, tile.tile_x, tile.tile_y).resource_amount;
                ++cluster.tile_count;
                sum_x += tile.tile_x;
                sum_y += tile.tile_y;
                min_x = std::min(min_x, tile.tile_x);
                max_x = std::max(max_x, tile.tile_x);
                min_y = std::min(min_y, tile.tile_y);
                max_y = std::max(max_y, tile.tile_y);
                const i32 gap = std::max(config.cluster_merge_gap_tiles, 1);
                for (i32 dy = -gap; dy <= gap; ++dy) {
                    for (i32 dx = -gap; dx <= gap; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const i64 nx = static_cast<i64>(tile.tile_x) + dx;
                        const i64 ny = static_cast<i64>(tile.tile_y) + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }
                        const std::size_t neighbour_index =
                            static_cast<std::size_t>(ny) * width +
                            static_cast<std::size_t>(nx);
                        if (label[neighbour_index] != 0 ||
                            !is_berry(observation, static_cast<u32>(nx),
                                static_cast<u32>(ny))) {
                            continue;
                        }
                        label[neighbour_index] = next_label;
                        stack.push_back({static_cast<u32>(nx), static_cast<u32>(ny)});
                    }
                }
            }
            cluster.centroid_x = static_cast<i32>(
                sum_x / static_cast<i64>(members.size())) * kTilePixels +
                kTilePixels / 2;
            cluster.centroid_y = static_cast<i32>(
                sum_y / static_cast<i64>(members.size())) * kTilePixels +
                kTilePixels / 2;

            // ---- site: valid nest anchor minimising Σ amount · distance --
            // Candidates: the bounding box grown by the margin, filtered by
            // the shared static placement rule (footprint, class, berry
            // clearance).  Ties: closer to the own start, then tile index.
            const i32 margin = std::max(config.site_margin_tiles, 0);
            const i64 lo_x = std::max<i64>(static_cast<i64>(min_x) - margin, 0);
            const i64 lo_y = std::max<i64>(static_cast<i64>(min_y) - margin, 0);
            const i64 hi_x = std::min<i64>(static_cast<i64>(max_x) + margin,
                static_cast<i64>(width) - 1);
            const i64 hi_y = std::min<i64>(static_cast<i64>(max_y) + margin,
                static_cast<i64>(height) - 1);
            struct SiteCandidate {
                i64 tx;
                i64 ty;
                double score;
                i64 start_distance;
                bool blocked;
            };
            std::vector<SiteCandidate> site_candidates;
            for (i64 ty = lo_y; ty <= hi_y; ++ty) {
                for (i64 tx = lo_x; tx <= hi_x; ++tx) {
                    bool blocked = false;
                    if (!AiBuildSiteCandidateOk(observation, occupancy,
                            config.base_type_id, static_cast<i32>(tx),
                            static_cast<i32>(ty), false, &blocked, config)) {
                        continue;
                    }
                    const i32 world_x = static_cast<i32>(tx) * kTilePixels +
                        kTilePixels / 2;
                    const i32 world_y = static_cast<i32>(ty) * kTilePixels +
                        kTilePixels / 2;
                    double score = 0.0;
                    for (const TilePoint& member : members) {
                        const i32 bx = static_cast<i32>(member.tile_x) *
                            kTilePixels + kTilePixels / 2;
                        const i32 by = static_cast<i32>(member.tile_y) *
                            kTilePixels + kTilePixels / 2;
                        score += static_cast<double>(
                            tile_at(observation, member.tile_x, member.tile_y)
                                .resource_amount) *
                            std::sqrt(static_cast<double>(
                                squared_distance(world_x, world_y, bx, by)));
                    }
                    const i64 start_distance = squared_distance(world_x, world_y,
                        std::max(observation.start_x, 0),
                        std::max(observation.start_y, 0));
                    site_candidates.push_back(
                        {tx, ty, score, start_distance, blocked});
                }
            }
            // Best score first; the chosen site must then pass the local
            // path/berry-reach verification (v9): the nest must stand on the
            // same walkable side as its berries (hill/cliff guard) and must
            // not seal its neighbourhood.
            std::sort(site_candidates.begin(), site_candidates.end(),
                [](const SiteCandidate& lhs, const SiteCandidate& rhs) {
                    if (lhs.score != rhs.score) {
                        return lhs.score < rhs.score;
                    }
                    if (lhs.start_distance != rhs.start_distance) {
                        return lhs.start_distance < rhs.start_distance;
                    }
                    return lhs.ty != rhs.ty ? lhs.ty < rhs.ty : lhs.tx < rhs.tx;
                });
            bool have_site = false;
            u32 site_verified = 0;
            for (const SiteCandidate& candidate : site_candidates) {
                if (site_verified >= config.path_verify_max_candidates) {
                    break;
                }
                ++site_verified;
                if (!AiBuildSiteKeepsLocalPaths(observation, occupancy,
                        config.base_type_id, static_cast<i32>(candidate.tx),
                        static_cast<i32>(candidate.ty),
                        /*require_berry_reach=*/true, config)) {
                    continue;
                }
                have_site = true;
                cluster.site_x = static_cast<i32>(candidate.tx) * kTilePixels +
                    kTilePixels / 2;
                cluster.site_y = static_cast<i32>(candidate.ty) * kTilePixels +
                    kTilePixels / 2;
                cluster.site_explored = tile_at(observation,
                    static_cast<u32>(candidate.tx),
                    static_cast<u32>(candidate.ty)).explored;
                cluster.site_blocked = candidate.blocked;
                cluster.site_distance_from_start_sq = candidate.start_distance;
                break;
            }
            if (have_site) {
                const i64 radius = config.developed_radius;
                for (const UnitMovementPoint& nest : nests) {
                    if (squared_distance(cluster.site_x, cluster.site_y,
                            nest.x, nest.y) <= radius * radius) {
                        cluster.developed = true;
                        break;
                    }
                }
            }
            plan.clusters.push_back(cluster);
        }
    }

    // ---- target: nearest undeveloped cluster with a site ------------------
    for (std::size_t index = 0; index < plan.clusters.size(); ++index) {
        const AiBerryCluster& cluster = plan.clusters[index];
        if (cluster.site_x < 0 || cluster.developed) {
            continue;
        }
        if (!plan.has_target ||
            cluster.site_distance_from_start_sq <
                plan.clusters[plan.target_index].site_distance_from_start_sq) {
            plan.has_target = true;
            plan.target_index = index;
        }
    }
    if (plan.has_target) {
        const AiBerryCluster& target = plan.clusters[plan.target_index];
        plan.target_x = target.site_x;
        plan.target_y = target.site_y;
        plan.target_explored = target.site_explored;
        plan.target_blocked = target.site_blocked;
    }
    return plan;
}

} // namespace ranker
