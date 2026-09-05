#include "ranker_ai_entity_economy.h"

#include "ranker_ai_actions.h"
#include "ranker_unit_commands.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace ranker {

namespace {

constexpr u32 kNeutralOwnerId = 8u;
constexpr u32 kMoveBit = 1u << 4;
constexpr u32 kAttackBit = 1u << 5;
constexpr u32 kBuildBit = 1u << 6;
constexpr u32 kHarvestBit = 1u << 7;
constexpr u32 kPatrolBit = 1u << 9;
constexpr u32 kMeleeRangeThreshold = 64u;
constexpr u32 kResearchOrderLimit = 0x40u;

float clamp01(float value) {
    return std::min(std::max(value, 0.0f), 1.0f);
}

float ratio_or_zero(u64 numerator, u64 denominator) {
    if (denominator == 0) {
        return 0.0f;
    }
    return clamp01(static_cast<float>(static_cast<double>(numerator) /
        static_cast<double>(denominator)));
}

u32 sat_sub(u32 a, u32 b) {
    return a > b ? a - b : 0u;
}

// Checked u64 accumulation: overflow is contract-fatal (plan 14.2).
bool add_checked(u64& accumulator, u64 value) {
    if (accumulator > 0xffffffffffffffffull - value) {
        return false;
    }
    accumulator += value;
    return true;
}

void direction_sin_cos(u32 direction, bool use_16, float* out_sin,
    float* out_cos) {
    *out_sin = 0.0f;
    *out_cos = 0.0f;
    if (direction == 0) {
        return;
    }
    static const float kDelta8[9][2] = {
        {0, 0}, {0, -4}, {2, -2}, {4, 0}, {2, 2},
        {0, 4}, {-2, 2}, {-4, 0}, {-2, -2},
    };
    static const float kDelta16[17][2] = {
        {0, 0}, {0, -4}, {1, -3}, {2, -2}, {3, -1},
        {4, 0}, {3, 1}, {2, 2}, {1, 3}, {0, 4},
        {-1, 3}, {-2, 2}, {-3, 1}, {-4, 0}, {-3, -1},
        {-2, -2}, {-1, -3},
    };
    float dx = 0.0f;
    float dy = 0.0f;
    if (use_16) {
        if (direction > 16) {
            return;
        }
        dx = kDelta16[direction][0];
        dy = kDelta16[direction][1];
    } else {
        if (direction > 8) {
            return;
        }
        dx = kDelta8[direction][0];
        dy = kDelta8[direction][1];
    }
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) {
        return;
    }
    *out_sin = dx / length;
    *out_cos = -dy / length;
}

bool is_neutral_monster_unit(const AiObservedUnit& unit) {
    return unit.visible && unit.alive && !unit.controlled &&
        unit.owner_id == kNeutralOwnerId &&
        unit.type_id < kAiEntity2MobileTypeLimit;
}

bool is_hostile_visible_unit(const AiObservation& observation,
    const AiObservedUnit& unit) {
    if (!unit.visible || !unit.alive || unit.controlled ||
        unit.owner_id >= 32u ||
        (observation.active_owner_mask & (1u << unit.owner_id)) == 0) {
        return false;
    }
    return (observation.local_relation_mask & (1u << unit.owner_id)) == 0;
}

bool tiles_valid(const AiObservation& observation) {
    return observation.map_width_tiles != 0 &&
        observation.map_height_tiles != 0 &&
        observation.tiles.size() ==
            static_cast<std::size_t>(observation.map_width_tiles) *
                observation.map_height_tiles;
}

const AiObservedMapTile* tile_at(const AiObservation& observation, i64 x,
    i64 y) {
    if (x < 0 || y < 0 || x >= observation.map_width_tiles ||
        y >= observation.map_height_tiles) {
        return nullptr;
    }
    return &observation.tiles[
        static_cast<std::size_t>(y) * observation.map_width_tiles +
        static_cast<std::size_t>(x)];
}

bool harvest_family_state(u32 base_state) {
    return base_state >= kAiEntity2StateHarvestFirst &&
        base_state <= kAiEntity2StateHarvestLast;
}

bool build_walk_state(u32 base_state) {
    return base_state == kAiEntity2StateBuildPlacementStart ||
        base_state == kAiEntity2StateBuildPlacementApproach;
}

bool research_state(u32 base_state) {
    return base_state == kAiEntity2StateResearchStart ||
        base_state == kAiEntity2StateResearchTimer;
}

u32 reference_list(const GameSessionUnitReferenceTables* tables, u32 type_id,
    int which, u32* out, u32 cap) {
    if (tables == nullptr || type_id >= kGameSessionUnitTypeCount) {
        return 0;
    }
    const UnitTypeSessionDefinition& definition = tables->definitions[type_id];
    if (!definition.present) {
        return 0;
    }
    const std::array<u32, kGameSessionUnitReferenceCapacity>* list = nullptr;
    u32 count = 0;
    switch (which) {
    case 0:
        list = &definition.primary_references;
        count = definition.primary_reference_count;
        break;
    case 1:
        list = &definition.alternate_references;
        count = definition.alternate_reference_count;
        break;
    default:
        list = &definition.completion_references;
        count = definition.completion_reference_count;
        break;
    }
    count = std::min<u32>(count, kGameSessionUnitReferenceCapacity);
    count = std::min<u32>(count, cap);
    for (u32 i = 0; i < count; ++i) {
        out[i] = (*list)[i];
    }
    return count;
}

bool list_contains(const u32* list, u32 count, u32 value) {
    for (u32 i = 0; i < count; ++i) {
        if (list[i] == value) {
            return true;
        }
    }
    return false;
}

struct RowSortEntry {
    AiEntityKey key;
    u32 unit_index = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// Vocabulary helpers
// ---------------------------------------------------------------------------

AiEntity2RejectCode AiEntity2RejectOfPair(AiEntityRejectCode code) {
    switch (code) {
    case AiEntityRejectCode::none: return AiEntity2RejectCode::none;
    case AiEntityRejectCode::out_of_range: return AiEntity2RejectCode::out_of_range;
    case AiEntityRejectCode::masked: return AiEntity2RejectCode::masked;
    case AiEntityRejectCode::stale_source: return AiEntity2RejectCode::stale_source;
    case AiEntityRejectCode::stale_target: return AiEntity2RejectCode::stale_target;
    case AiEntityRejectCode::ownership: return AiEntity2RejectCode::ownership;
    case AiEntityRejectCode::inactive: return AiEntity2RejectCode::inactive;
    case AiEntityRejectCode::visibility: return AiEntity2RejectCode::visibility;
    case AiEntityRejectCode::hostility: return AiEntity2RejectCode::hostility;
    case AiEntityRejectCode::capability: return AiEntity2RejectCode::capability;
    case AiEntityRejectCode::render_class: return AiEntity2RejectCode::render_class;
    case AiEntityRejectCode::terrain: return AiEntity2RejectCode::terrain;
    case AiEntityRejectCode::point: return AiEntity2RejectCode::point;
    case AiEntityRejectCode::planner: return AiEntity2RejectCode::planner;
    case AiEntityRejectCode::encode: return AiEntity2RejectCode::encode;
    case AiEntityRejectCode::transport_capacity:
        return AiEntity2RejectCode::transport_capacity;
    default: return AiEntity2RejectCode::internal_error;
    }
}

AiEntity2Command AiEntity2EngineCommandOf(u8 policy_command) {
    switch (static_cast<AiEntity2PolicyCommand>(policy_command)) {
    case AiEntity2PolicyCommand::move: return AiEntity2Command::move;
    case AiEntity2PolicyCommand::attack_move: return AiEntity2Command::attack_move;
    case AiEntity2PolicyCommand::patrol: return AiEntity2Command::patrol;
    case AiEntity2PolicyCommand::attack_unit: return AiEntity2Command::attack_unit;
    case AiEntity2PolicyCommand::hold: return AiEntity2Command::hold_position;
    case AiEntity2PolicyCommand::harvest: return AiEntity2Command::harvest;
    case AiEntity2PolicyCommand::build: return AiEntity2Command::build;
    case AiEntity2PolicyCommand::produce_unit: return AiEntity2Command::produce_unit;
    case AiEntity2PolicyCommand::research_upgrade:
        return AiEntity2Command::research_upgrade;
    default: return AiEntity2Command::keep_current_order;
    }
}

u8 AiEntity2PolicyCommandOf(AiEntity2Command command) {
    switch (command) {
    case AiEntity2Command::keep_current_order:
        return static_cast<u8>(AiEntity2PolicyCommand::keep);
    case AiEntity2Command::move: return static_cast<u8>(AiEntity2PolicyCommand::move);
    case AiEntity2Command::attack_move:
        return static_cast<u8>(AiEntity2PolicyCommand::attack_move);
    case AiEntity2Command::patrol: return static_cast<u8>(AiEntity2PolicyCommand::patrol);
    case AiEntity2Command::attack_unit:
        return static_cast<u8>(AiEntity2PolicyCommand::attack_unit);
    case AiEntity2Command::hold_position:
        return static_cast<u8>(AiEntity2PolicyCommand::hold);
    case AiEntity2Command::harvest: return static_cast<u8>(AiEntity2PolicyCommand::harvest);
    case AiEntity2Command::build: return static_cast<u8>(AiEntity2PolicyCommand::build);
    case AiEntity2Command::produce_unit:
        return static_cast<u8>(AiEntity2PolicyCommand::produce_unit);
    case AiEntity2Command::research_upgrade:
        return static_cast<u8>(AiEntity2PolicyCommand::research_upgrade);
    default: return kAiEntityLastAttemptNone;
    }
}

bool AiEntity2CommandIsPoint(AiEntity2Command command) {
    return command == AiEntity2Command::move ||
        command == AiEntity2Command::attack_move ||
        command == AiEntity2Command::patrol;
}

bool AiEntity2CommandIsEconomy(AiEntity2Command command) {
    return command == AiEntity2Command::harvest ||
        command == AiEntity2Command::build ||
        command == AiEntity2Command::produce_unit ||
        command == AiEntity2Command::research_upgrade;
}

AiEntity2Command AiEntity2CommandOfKind(AiEntity2CandidateKind kind) {
    switch (kind) {
    case AiEntity2CandidateKind::resource: return AiEntity2Command::harvest;
    case AiEntity2CandidateKind::build_site: return AiEntity2Command::build;
    case AiEntity2CandidateKind::produce_unit:
        return AiEntity2Command::produce_unit;
    default: return AiEntity2Command::research_upgrade;
    }
}

bool AiEntity2KindOfCommand(AiEntity2Command command,
    AiEntity2CandidateKind* out_kind) {
    switch (command) {
    case AiEntity2Command::harvest:
        *out_kind = AiEntity2CandidateKind::resource;
        return true;
    case AiEntity2Command::build:
        *out_kind = AiEntity2CandidateKind::build_site;
        return true;
    case AiEntity2Command::produce_unit:
        *out_kind = AiEntity2CandidateKind::produce_unit;
        return true;
    case AiEntity2Command::research_upgrade:
        *out_kind = AiEntity2CandidateKind::research_upgrade;
        return true;
    default:
        return false;
    }
}

u64 AiEntity2ResourceKey(u32 compact_tile_index) {
    return compact_tile_index;
}

u64 AiEntity2BuildKey(u32 building_type, u32 tile_x, u32 tile_y) {
    return (static_cast<u64>(building_type) << 32) |
        (static_cast<u64>(tile_y & 0xffffu) << 16) |
        static_cast<u64>(tile_x & 0xffffu);
}

u64 AiEntity2ProduceKey(u32 unit_type) {
    return unit_type;
}

u64 AiEntity2ResearchKey(u32 order_id, u32 next_level) {
    return (static_cast<u64>(order_id) << 32) | next_level;
}

AiEntity2TileRect AiEntity2FootprintRectOf(const AiEntity2Candidate& candidate) {
    AiEntity2TileRect rect;
    rect.x0 = candidate.x >> 5;
    rect.y0 = candidate.y >> 5;
    rect.x1 = rect.x0 + static_cast<i32>(candidate.footprint_width());
    rect.y1 = rect.y0 + static_cast<i32>(candidate.footprint_height());
    return rect;
}

bool AiEntity2RectsOverlap(const AiEntity2TileRect& a,
    const AiEntity2TileRect& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

AiEntity2Role AiEntity2RoleOf(const AiObservedUnit& unit) {
    if (unit.type_id >= kAiEntity2MobileTypeLimit) {
        return AiEntity2Role::building;
    }
    if ((unit.type_flags & kHarvestBit) != 0) {
        return AiEntity2Role::worker;
    }
    const bool attacks = (unit.type_flags & kAttackBit) != 0;
    if (unit.transport_capacity > 0 && !attacks) {
        return AiEntity2Role::transport;
    }
    if (!attacks) {
        return AiEntity2Role::other;
    }
    return unit.attack_range_base != 0 &&
        unit.attack_range_base <= kMeleeRangeThreshold ?
        AiEntity2Role::melee : AiEntity2Role::ranged;
}

AiEntity2WireSemanticOrder AiEntity2WireSemanticOrderOf(
    AiSemanticActionKind kind) {
    switch (kind) {
    case AiSemanticActionKind::move: return AiEntity2WireSemanticOrder::move;
    case AiSemanticActionKind::attack_move:
        return AiEntity2WireSemanticOrder::attack_move;
    case AiSemanticActionKind::patrol: return AiEntity2WireSemanticOrder::patrol;
    case AiSemanticActionKind::attack_unit:
        return AiEntity2WireSemanticOrder::attack_unit;
    case AiSemanticActionKind::hold_position:
        return AiEntity2WireSemanticOrder::hold;
    case AiSemanticActionKind::stop: return AiEntity2WireSemanticOrder::stop;
    case AiSemanticActionKind::harvest:
        return AiEntity2WireSemanticOrder::harvest;
    case AiSemanticActionKind::build: return AiEntity2WireSemanticOrder::build;
    case AiSemanticActionKind::produce_unit:
        return AiEntity2WireSemanticOrder::produce_unit;
    case AiSemanticActionKind::research:
        return AiEntity2WireSemanticOrder::research_upgrade;
    default:
        // return_cargo included: never a semantic order (plan 11.4).
        return AiEntity2WireSemanticOrder::external_unknown;
    }
}

AiEntity2WireSemanticOrder AiEntity2WireSemanticOrderOfCommand(
    AiEntity2Command command) {
    switch (command) {
    case AiEntity2Command::move: return AiEntity2WireSemanticOrder::move;
    case AiEntity2Command::attack_move:
        return AiEntity2WireSemanticOrder::attack_move;
    case AiEntity2Command::patrol: return AiEntity2WireSemanticOrder::patrol;
    case AiEntity2Command::attack_unit:
        return AiEntity2WireSemanticOrder::attack_unit;
    case AiEntity2Command::hold_position:
        return AiEntity2WireSemanticOrder::hold;
    case AiEntity2Command::stop: return AiEntity2WireSemanticOrder::stop;
    case AiEntity2Command::harvest: return AiEntity2WireSemanticOrder::harvest;
    case AiEntity2Command::build: return AiEntity2WireSemanticOrder::build;
    case AiEntity2Command::produce_unit:
        return AiEntity2WireSemanticOrder::produce_unit;
    case AiEntity2Command::research_upgrade:
        return AiEntity2WireSemanticOrder::research_upgrade;
    default: return AiEntity2WireSemanticOrder::none;
    }
}

// ---------------------------------------------------------------------------
// Snapshot accessors
// ---------------------------------------------------------------------------

bool AiEntity2Snapshot::attack_pair_bit(u32 row, u32 target) const {
    const u32 words = attack_words_per_row();
    const std::size_t index = static_cast<std::size_t>(row) * words +
        (target >> 5);
    if (index >= attack_pair_mask.size()) {
        return false;
    }
    return (attack_pair_mask[index] >> (target & 31u) & 1u) != 0;
}

bool AiEntity2Snapshot::economy_pair_bit(u32 row, u32 candidate) const {
    const u32 words = economy_words_per_row();
    const std::size_t index = static_cast<std::size_t>(row) * words +
        (candidate >> 5);
    if (index >= economy_pair_mask.size()) {
        return false;
    }
    return (economy_pair_mask[index] >> (candidate & 31u) & 1u) != 0;
}

i32 AiEntity2Snapshot::candidate_row_of(u8 kind, u64 key) const {
    u32 begin = 0;
    u32 count = 0;
    switch (kind) {
    case 0: begin = 0; count = resource_rows; break;
    case 1: begin = resource_rows; count = build_rows; break;
    case 2: begin = resource_rows + build_rows; count = produce_rows; break;
    case 3:
        begin = resource_rows + build_rows + produce_rows;
        count = research_rows;
        break;
    default: return -1;
    }
    u32 lo = begin;
    u32 hi = begin + count;
    while (lo < hi) {
        const u32 mid = lo + (hi - lo) / 2;
        const u64 mid_key = candidates[mid].key;
        if (mid_key == key) {
            return static_cast<i32>(mid);
        }
        if (mid_key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Snapshot builder
// ---------------------------------------------------------------------------

namespace {

struct BuildTypeInfo {
    u32 type_id = 0;
    AiEntity2UnitCatalogEntry catalog;
    AiBuildingFootprint footprint;
};

// Bucket-local deterministic site search: the FindAiBuildSite ranking (ring
// open first, distance to the bucket centre, then tile index) restricted to
// the bucket's tiles, BFS-verified nearest first.  At most one site per
// (type, bucket).
bool find_bucket_site(const AiObservation& observation,
    const std::vector<u8>& occupancy, u32 type_id, u32 x0, u32 x1, u32 y0,
    u32 y1, const AiExpansionConfig& config, u32* out_tx, u32* out_ty) {
    struct Candidate {
        u32 tx;
        u32 ty;
        i64 distance;
        bool ring_open;
    };
    std::vector<Candidate> candidates;
    const i64 center_x2 = static_cast<i64>(x0) + x1;   // doubled coordinates
    const i64 center_y2 = static_cast<i64>(y0) + y1;
    for (u32 ty = y0; ty < y1; ++ty) {
        for (u32 tx = x0; tx < x1; ++tx) {
            bool blocked = false;
            bool ring_open = false;
            if (!AiBuildSiteCandidateOk(observation, occupancy, type_id,
                    static_cast<i32>(tx), static_cast<i32>(ty), true, &blocked,
                    config, &ring_open) || blocked) {
                continue;
            }
            const i64 dx = 2 * static_cast<i64>(tx) + 1 - center_x2;
            const i64 dy = 2 * static_cast<i64>(ty) + 1 - center_y2;
            candidates.push_back({tx, ty, dx * dx + dy * dy, ring_open});
        }
    }
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
        *out_tx = candidate.tx;
        *out_ty = candidate.ty;
        return true;
    }
    return false;
}

bool bucket_has_explored_buildable(const AiObservation& observation, u32 x0,
    u32 x1, u32 y0, u32 y1) {
    for (u32 ty = y0; ty < y1; ++ty) {
        for (u32 tx = x0; tx < x1; ++tx) {
            const AiObservedMapTile* tile = tile_at(observation, tx, ty);
            if (tile != nullptr && tile->explored && tile->buildable) {
                return true;
            }
        }
    }
    return false;
}

// Any tile of the one-tile ring around a footprint (or the footprint itself
// for a 0-size rect) reachable from the unit.
bool ring_reachable(const AiEntityReachability& reach, i32 unit_x, i32 unit_y,
    const AiEntity2TileRect& rect) {
    for (i32 ty = rect.y0 - 1; ty <= rect.y1; ++ty) {
        for (i32 tx = rect.x0 - 1; tx <= rect.x1; ++tx) {
            const bool inside = tx >= rect.x0 && tx < rect.x1 &&
                ty >= rect.y0 && ty < rect.y1;
            if (inside || tx < 0 || ty < 0) {
                continue;
            }
            if (AiEntityTileReachableFromUnit(reach, unit_x, unit_y,
                    static_cast<u32>(tx), static_cast<u32>(ty))) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

AiEntity2Snapshot BuildAiEntity2Snapshot(const AiEntity2SnapshotInput& input) {
    AiEntity2Snapshot snapshot;
    if (input.observation == nullptr) {
        snapshot.contract_error = true;
        snapshot.error = "missing observation";
        return snapshot;
    }
    const AiObservation& observation = *input.observation;
    snapshot.owner = observation.local_owner;
    snapshot.frame = observation.simulation_frame;
    if (input.registry != nullptr && input.registry->contract_fatal) {
        snapshot.contract_error = true;
        snapshot.error = "entity registry contract-fatal: " +
            input.registry->fatal_reason;
        return snapshot;
    }
    if (!tiles_valid(observation)) {
        snapshot.contract_error = true;
        snapshot.error = "observation tiles do not match the map dimensions";
        return snapshot;
    }
    const u32 frame = observation.simulation_frame;
    const u32 owner = observation.local_owner;
    const AiEntity2OrderStore* store = input.orders;

    auto key_of = [&](const AiObservedUnit& unit) {
        AiEntityKey key{unit.id, 0};
        if (input.registry != nullptr) {
            const AiEntityRegistryRecord* record =
                AiEntityRegistryFindByObserved(*input.registry, unit.id,
                    unit.runtime_slot_index);
            if (record != nullptr && record->runtime_id == unit.id) {
                key.activation_generation = record->generation;
            }
        }
        return key;
    };
    auto epoch_of = [&](const AiObservedUnit& unit) -> u32 {
        if (input.registry == nullptr) {
            return 0;
        }
        const AiEntityRegistryRecord* record =
            AiEntityRegistryFindByObserved(*input.registry, unit.id,
                unit.runtime_slot_index);
        return record != nullptr && record->runtime_id == unit.id ?
            record->control_epoch : 0;
    };

    // ---- row selection: every controlled alive unit is an own row ----
    std::vector<RowSortEntry> own_entries;
    std::vector<RowSortEntry> target_entries;
    for (u32 index = 0; index < observation.units.size(); ++index) {
        const AiObservedUnit& unit = observation.units[index];
        if (unit.controlled && unit.alive) {
            own_entries.push_back({key_of(unit), index});
        }
        if (is_hostile_visible_unit(observation, unit) ||
            is_neutral_monster_unit(unit)) {
            target_entries.push_back({key_of(unit), index});
        }
    }
    auto by_key = [](const RowSortEntry& a, const RowSortEntry& b) {
        if (!(a.key == b.key)) {
            return a.key < b.key;
        }
        return a.unit_index < b.unit_index;
    };
    std::sort(own_entries.begin(), own_entries.end(), by_key);
    std::sort(target_entries.begin(), target_entries.end(), by_key);
    for (std::size_t i = 1; i < own_entries.size(); ++i) {
        if (own_entries[i].key.runtime_id ==
            own_entries[i - 1].key.runtime_id) {
            snapshot.contract_error = true;
            snapshot.error = "duplicate own runtime id in snapshot";
            return snapshot;
        }
    }
    if (own_entries.size() > kAiEntityWireRowLimit ||
        target_entries.size() > kAiEntityWireRowLimit) {
        snapshot.contract_error = true;
        snapshot.error = "entity row count above wire hard limit 2048";
        return snapshot;
    }

    const float map_width_px =
        static_cast<float>(observation.map_width_tiles) * 32.0f;
    const float map_height_px =
        static_cast<float>(observation.map_height_tiles) * 32.0f;
    const float diagonal_px = std::sqrt(
        map_width_px * map_width_px + map_height_px * map_height_px);
    const float diagonal_safe = diagonal_px > 0.0f ? diagonal_px : 1.0f;
    const float width_safe = map_width_px > 0.0f ? map_width_px : 1.0f;
    const float height_safe = map_height_px > 0.0f ? map_height_px : 1.0f;
    const i32 start_x = std::max(observation.start_x, 0);
    const i32 start_y = std::max(observation.start_y, 0);
    auto start_distance = [&](i32 x, i32 y) {
        const float dx = static_cast<float>(x - start_x);
        const float dy = static_cast<float>(y - start_y);
        return clamp01(std::sqrt(dx * dx + dy * dy) / diagonal_safe);
    };

    // ---- target rows (ENTCMD01 target block, unchanged) ----
    std::unordered_map<u32, i32> target_row_by_id;
    snapshot.targets.reserve(target_entries.size());
    for (const RowSortEntry& entry : target_entries) {
        const AiObservedUnit& unit = observation.units[entry.unit_index];
        AiEntityTargetRow row;
        row.key = entry.key;
        row.type_id = static_cast<u16>(unit.type_id);
        row.owner_id = static_cast<u8>(unit.owner_id);
        const AiEntity2Role role = AiEntity2RoleOf(unit);
        row.role = role == AiEntity2Role::melee ? 0 :
            role == AiEntity2Role::ranged ? 1 : 2;
        row.render_class = unit.render_class;
        row.kind_bits = 0;
        if (unit.type_id < kAiEntity2MobileTypeLimit) {
            row.kind_bits |= kAiEntityTargetKindMobile;
        } else {
            row.kind_bits |= kAiEntityTargetKindBuilding;
        }
        if (unit.owner_id == kNeutralOwnerId) {
            row.kind_bits |= kAiEntityTargetKindNeutral;
        }
        if (unit.render_class >= kAiEntityRenderClassLimit) {
            row.kind_bits |= kAiEntityTargetKindRenderClassOob;
        }
        row.x = unit.x;
        row.y = unit.y;
        auto& f = row.feature;
        f[0] = clamp01(static_cast<float>(unit.x) / width_safe);
        f[1] = clamp01(static_cast<float>(unit.y) / height_safe);
        f[2] = ratio_or_zero(unit.health, unit.max_health);
        f[3] = ratio_or_zero(unit.secondary_value, unit.max_secondary_value);
        f[4] = clamp01(static_cast<float>(unit.movement_step_limit) /
            (static_cast<float>(std::max(unit.movement_period, 1u)) *
                kAiEntityNormSpeedScale));
        f[5] = clamp01(static_cast<float>(unit.sight_range) / diagonal_safe);
        f[6] = clamp01(static_cast<float>(unit.attack_range) / diagonal_safe);
        f[7] = clamp01(
            static_cast<float>(unit.attack_range_vs_air) / diagonal_safe);
        f[8] = clamp01(
            static_cast<float>(unit.attack_power) / kAiEntityNormPowerScale);
        f[9] = clamp01(
            static_cast<float>(unit.defense_power) / kAiEntityNormPowerScale);
        direction_sin_cos(unit.direction, unit.use_16_direction_lookup,
            &f[10], &f[11]);
        f[12] = static_cast<float>(unit.x - observation.start_x) /
            diagonal_safe;
        f[13] = static_cast<float>(unit.y - observation.start_y) /
            diagonal_safe;
        target_row_by_id[unit.id] = static_cast<i32>(snapshot.targets.size());
        snapshot.targets.push_back(row);
    }

    // ---- reachability caches per movement class (mobile own rows) ----
    std::unordered_map<u32, AiEntityReachability> reachability;
    if (input.movement_map != nullptr) {
        for (const RowSortEntry& entry : own_entries) {
            const AiObservedUnit& unit = observation.units[entry.unit_index];
            if (unit.type_id >= kAiEntity2MobileTypeLimit) {
                continue;
            }
            if (reachability.find(unit.movement_class) == reachability.end()) {
                reachability.emplace(unit.movement_class,
                    BuildAiEntityReachability(*input.movement_map,
                        unit.movement_class));
            }
        }
    }

    // ---- persistent claims -> spendable budget ----
    u64 claimed_primary = 0;
    u64 claimed_secondary = 0;
    u64 claimed_population = 0;
    if (store != nullptr) {
        for (const auto& entry : store->economy) {
            const AiEntity2EconomyOrder& order = entry.second;
            if (order.controller_owner != owner || !order.cost_claimed) {
                continue;
            }
            claimed_primary += order.primary_cost;
            claimed_secondary += order.secondary_cost;
        }
        for (const AiEntity2EconomyEvent& event : store->events) {
            if (event.controller_owner != owner) {
                continue;
            }
            if (event.resource_claimed) {
                claimed_primary += event.primary_cost;
                claimed_secondary += event.secondary_cost;
            }
            if (event.population_claimed) {
                claimed_population += event.population_cost;
            }
        }
    }
    snapshot.spendable_primary = static_cast<u32>(sat_sub(
        observation.primary_resources,
        static_cast<u32>(std::min<u64>(claimed_primary, 0xffffffffull))));
    snapshot.spendable_secondary = static_cast<u32>(sat_sub(
        observation.secondary_resources,
        static_cast<u32>(std::min<u64>(claimed_secondary, 0xffffffffull))));
    {
        // population_used = supply, population_reserved = demand (engine
        // semantics; see ranker_ai_rl_features.cpp).
        const u32 supply = std::min(observation.population_used,
            observation.population_limit);
        const u32 demand = observation.population_reserved +
            static_cast<u32>(std::min<u64>(claimed_population, 0xffffffffull));
        snapshot.spendable_population = sat_sub(supply, demand);
    }

    // ---- own economy sources (for candidate generation) ----
    struct OwnSourceInfo {
        const AiObservedUnit* unit = nullptr;
        AiEntityKey key;
        AiEntity2Role role = AiEntity2Role::other;
        bool completed = false;
        const AiEntity2EconomyOrder* economy_order = nullptr;   // awaiting/active
        u32 awaiting_events = 0;
        u32 awaiting_research_order = 0xffffffffu;
        u32 walking_build_type = 0;
    };
    std::vector<OwnSourceInfo> sources(own_entries.size());
    std::unordered_set<u32> builder_types;
    for (std::size_t i = 0; i < own_entries.size(); ++i) {
        OwnSourceInfo& info = sources[i];
        info.unit = &observation.units[own_entries[i].unit_index];
        info.key = own_entries[i].key;
        info.role = AiEntity2RoleOf(*info.unit);
        info.completed = !info.unit->under_construction;
        info.walking_build_type = AiWalkingBuildTypeOf(*info.unit);
        if (store != nullptr) {
            const auto it = store->economy.find(AiEntityPackKey(info.key));
            if (it != store->economy.end() &&
                (it->second.status == AiEntityOrderStatus::awaiting_apply ||
                    it->second.status == AiEntityOrderStatus::active)) {
                info.economy_order = &it->second;
            }
            for (const AiEntity2EconomyEvent& event : store->events) {
                if (event.source == info.key &&
                    event.status == AiEntity2EventStatus::awaiting_apply) {
                    ++info.awaiting_events;
                    if (event.command == AiEntity2Command::research_upgrade) {
                        info.awaiting_research_order = event.object_id;
                    }
                }
            }
        }
        if (info.role == AiEntity2Role::worker && info.completed &&
            (info.unit->type_flags & kBuildBit) != 0) {
            builder_types.insert(info.unit->type_id);
        }
    }

    // ---- team-intent slots: membership per row, SCOUT capacity ----
    std::vector<u8> row_slot(own_entries.size(), kAiEntity2SlotNone);
    std::array<u32, kAiEntity2SlotCount> slot_members{};
    for (std::size_t i = 0; i < own_entries.size(); ++i) {
        const OwnSourceInfo& info = sources[i];
        const bool combat_row = info.completed &&
            (info.role == AiEntity2Role::melee || info.role == AiEntity2Role::ranged);
        if (!combat_row) {
            continue;
        }
        u8 slot = kAiEntity2SlotMain;
        if (input.slots != nullptr) {
            const auto it = input.slots->membership.find(AiEntityPackKey(info.key));
            if (it != input.slots->membership.end() && it->second < kAiEntity2SlotCount) {
                slot = it->second;
            }
        }
        row_slot[i] = slot;
        ++slot_members[slot];
    }
    snapshot.scout_free_at_snapshot = sat_sub(kAiEntity2ScoutCapacity,
        slot_members[kAiEntity2SlotScout]);
    static const AiEntity2SlotState kEmptySlots{};
    const AiEntity2SlotState& slot_state =
        input.slots != nullptr ? *input.slots : kEmptySlots;

    // ---- candidate table ----
    std::vector<AiEntity2Candidate> resource_candidates;
    std::map<u64, AiEntity2Candidate> build_candidates;
    std::map<u64, AiEntity2Candidate> produce_candidates;
    std::map<u64, AiEntity2Candidate> research_candidates;
    const bool economy_enabled = input.catalog.unit_references != nullptr;

    // R: explored tiles with a remembered amount, compact index ascending.
    std::unordered_map<u32, u32> targeting_workers;
    for (const OwnSourceInfo& info : sources) {
        if (info.role != AiEntity2Role::worker) {
            continue;
        }
        u32 tile_index = 0;
        bool have = false;
        if (input.economy_live.unit_harvest_tile != nullptr) {
            have = input.economy_live.unit_harvest_tile(
                input.economy_live.ctx, info.unit->id, &tile_index);
        } else if (info.economy_order != nullptr &&
            info.economy_order->command == AiEntity2Command::harvest) {
            have = true;
            tile_index = info.economy_order->object_id;
        }
        if (have) {
            ++targeting_workers[tile_index];
        }
    }
    for (u32 ty = 0; ty < observation.map_height_tiles; ++ty) {
        for (u32 tx = 0; tx < observation.map_width_tiles; ++tx) {
            const AiObservedMapTile& tile = *tile_at(observation, tx, ty);
            if (!tile.explored || tile.resource_amount == 0) {
                continue;
            }
            const u32 compact = ty * observation.map_width_tiles + tx;
            AiEntity2Candidate candidate;
            candidate.key = AiEntity2ResourceKey(compact);
            candidate.kind = static_cast<u8>(AiEntity2CandidateKind::resource);
            candidate.flags = kAiEntity2CandidateFlagExplored;
            if (tile.visible) {
                candidate.flags |= kAiEntity2CandidateFlagVisible;
            } else {
                candidate.flags |= kAiEntity2CandidateFlagRemembered;
            }
            candidate.object_id = 0;
            candidate.x = static_cast<i32>(tx * 32u + 16u);
            candidate.y = static_cast<i32>(ty * 32u + 16u);
            candidate.raw0 = compact;
            candidate.raw1 = tile.resource_amount;
            const auto workers_it = targeting_workers.find(compact);
            candidate.raw2 = workers_it != targeting_workers.end() ?
                workers_it->second : 0u;
            auto& f = candidate.feature;
            f[0] = clamp01(static_cast<float>(candidate.x) / width_safe);
            f[1] = clamp01(static_cast<float>(candidate.y) / height_safe);
            f[5] = clamp01(static_cast<float>(tile.resource_amount) / 4095.0f);
            f[6] = clamp01(static_cast<float>(candidate.raw2) / 8.0f);
            f[7] = start_distance(candidate.x, candidate.y);
            resource_candidates.push_back(candidate);
        }
    }

    // B: buildable types from the builders' primary references.
    std::vector<BuildTypeInfo> build_types;
    if (economy_enabled) {
        std::map<u32, BuildTypeInfo> by_type;
        for (u32 builder_type : builder_types) {
            u32 refs[kGameSessionUnitReferenceCapacity];
            const u32 count = reference_list(input.catalog.unit_references,
                builder_type, 0, refs, kGameSessionUnitReferenceCapacity);
            for (u32 i = 0; i < count; ++i) {
                const u32 type_id = refs[i];
                if (type_id < kAiEntity2MobileTypeLimit ||
                    type_id >= kGameSessionUnitTypeCount ||
                    by_type.count(type_id) != 0) {
                    continue;
                }
                BuildTypeInfo info;
                info.type_id = type_id;
                if (input.catalog.unit_catalog != nullptr &&
                    !input.catalog.unit_catalog(input.catalog.ctx, owner,
                        type_id, &info.catalog)) {
                    continue;
                }
                info.footprint = AiBuildingFootprintOf(type_id);
                by_type.emplace(type_id, info);
            }
        }
        for (const auto& entry : by_type) {
            build_types.push_back(entry.second);
        }
    }
    auto make_build_candidate = [&](const BuildTypeInfo& info, u32 tx, u32 ty,
                                    u8 extra_flags) {
        const u64 key = AiEntity2BuildKey(info.type_id, tx, ty);
        auto it = build_candidates.find(key);
        if (it != build_candidates.end()) {
            it->second.flags |= extra_flags;
            return;
        }
        const AiObservedMapTile* anchor = tile_at(observation, tx, ty);
        AiEntity2Candidate candidate;
        candidate.key = key;
        candidate.kind = static_cast<u8>(AiEntity2CandidateKind::build_site);
        candidate.flags = extra_flags;
        if (anchor != nullptr && anchor->explored) {
            candidate.flags |= kAiEntity2CandidateFlagExplored;
        }
        if (anchor != nullptr && anchor->visible) {
            candidate.flags |= kAiEntity2CandidateFlagVisible;
        }
        candidate.object_id = static_cast<u16>(info.type_id);
        candidate.x = static_cast<i32>(tx * 32u);
        candidate.y = static_cast<i32>(ty * 32u);
        candidate.raw0 = info.catalog.primary_cost;
        candidate.raw1 = info.catalog.secondary_cost;
        const u32 placement_class = anchor != nullptr ?
            (anchor->placement_class & 0xffu) : 0u;
        candidate.raw2 = (info.footprint.width & 0xffu) |
            ((info.footprint.height & 0xffu) << 8) | (placement_class << 16);
        auto& f = candidate.feature;
        f[0] = clamp01(static_cast<float>(candidate.x) / width_safe);
        f[1] = clamp01(static_cast<float>(candidate.y) / height_safe);
        f[2] = clamp01(static_cast<float>(candidate.raw0) / 1000.0f);
        f[3] = clamp01(static_cast<float>(candidate.raw1) / 1000.0f);
        f[5] = clamp01(static_cast<float>(
            info.footprint.width * info.footprint.height) / 64.0f);
        f[7] = start_distance(candidate.x, candidate.y);
        build_candidates.emplace(key, candidate);
    };
    if (!build_types.empty()) {
        const std::vector<u8> occupancy = AiBuildOccupancyGrid(observation);
        const u32 map_w = observation.map_width_tiles;
        const u32 map_h = observation.map_height_tiles;
        for (u32 cell = 0; cell < kAiEntityPointGlobalTokenCount; ++cell) {
            const u32 cell_x = cell % kAiEntityPointGridWidth;
            const u32 cell_y = cell / kAiEntityPointGridWidth;
            const u32 x0 = static_cast<u32>(
                (static_cast<u64>(cell_x) * map_w) / kAiEntityPointGridWidth);
            const u32 x1 = static_cast<u32>(
                (static_cast<u64>(cell_x + 1) * map_w) / kAiEntityPointGridWidth);
            const u32 y0 = static_cast<u32>(
                (static_cast<u64>(cell_y) * map_h) / kAiEntityPointGridWidth);
            const u32 y1 = static_cast<u32>(
                (static_cast<u64>(cell_y + 1) * map_h) / kAiEntityPointGridWidth);
            if (!bucket_has_explored_buildable(observation, x0, x1, y0, y1)) {
                continue;
            }
            for (const BuildTypeInfo& info : build_types) {
                u32 tx = 0;
                u32 ty = 0;
                if (find_bucket_site(observation, occupancy, info.type_id, x0,
                        x1, y0, y1, input.expansion, &tx, &ty)) {
                    make_build_candidate(info, tx, ty, 0);
                }
            }
        }
        // Expansion: one base-type candidate per undeveloped berry cluster.
        for (const BuildTypeInfo& info : build_types) {
            if (info.type_id != input.expansion.base_type_id) {
                continue;
            }
            const AiExpansionPlan plan =
                ComputeAiExpansionPlan(observation, input.expansion);
            for (const AiBerryCluster& cluster : plan.clusters) {
                // A site with a unit standing on the footprint is refused by
                // the engine placement gate: not a candidate this tick (the
                // bucket generator skips blocked sites the same way).
                if (cluster.site_x < 0 || cluster.site_y < 0 ||
                    cluster.developed || cluster.site_blocked) {
                    continue;
                }
                make_build_candidate(info,
                    static_cast<u32>(cluster.site_x) >> 5,
                    static_cast<u32>(cluster.site_y) >> 5,
                    kAiEntity2CandidateFlagExpansionSite);
            }
        }
        // Teacher-proposed exact sites (SHD2 taps).
        for (const AiEntity2SnapshotInput::ExtraBuildSite& site :
            input.extra_build_sites) {
            for (const BuildTypeInfo& info : build_types) {
                if (info.type_id == site.building_type &&
                    site.tile_x < observation.map_width_tiles &&
                    site.tile_y < observation.map_height_tiles) {
                    make_build_candidate(info, site.tile_x, site.tile_y, 0);
                }
            }
        }
        // Active / awaiting BUILD latches keep their attention row.
        if (store != nullptr) {
            for (const auto& entry : store->economy) {
                const AiEntity2EconomyOrder& order = entry.second;
                if (order.controller_owner != owner ||
                    order.command != AiEntity2Command::build ||
                    (order.status != AiEntityOrderStatus::awaiting_apply &&
                        order.status != AiEntityOrderStatus::active)) {
                    continue;
                }
                const BuildTypeInfo* info = nullptr;
                for (const BuildTypeInfo& candidate : build_types) {
                    if (candidate.type_id == order.object_id) {
                        info = &candidate;
                    }
                }
                if (info == nullptr) {
                    continue;
                }
                make_build_candidate(*info, static_cast<u32>(order.x) >> 5,
                    static_cast<u32>(order.y) >> 5,
                    kAiEntity2CandidateFlagActiveOrReserved);
            }
        }
    }

    // P / Q: producer alternate references and researcher completion refs.
    struct ProducerInfo {
        u32 type_id;
        u32 count;
    };
    std::map<u32, u32> completed_producers_by_unit_type;
    if (economy_enabled) {
        for (const OwnSourceInfo& info : sources) {
            if (info.role != AiEntity2Role::building || !info.completed) {
                continue;
            }
            u32 refs[kGameSessionUnitReferenceCapacity];
            u32 count = reference_list(input.catalog.unit_references,
                info.unit->type_id, 1, refs, kGameSessionUnitReferenceCapacity);
            for (u32 i = 0; i < count; ++i) {
                const u32 unit_type = refs[i];
                if (unit_type >= kAiEntity2MobileTypeLimit) {
                    continue;
                }
                ++completed_producers_by_unit_type[unit_type];
                if (produce_candidates.count(unit_type) != 0) {
                    continue;
                }
                AiEntity2UnitCatalogEntry catalog;
                if (input.catalog.unit_catalog != nullptr &&
                    !input.catalog.unit_catalog(input.catalog.ctx, owner,
                        unit_type, &catalog)) {
                    continue;
                }
                AiEntity2Candidate candidate;
                candidate.key = AiEntity2ProduceKey(unit_type);
                candidate.kind =
                    static_cast<u8>(AiEntity2CandidateKind::produce_unit);
                candidate.object_id = static_cast<u16>(unit_type);
                candidate.raw0 = catalog.primary_cost;
                candidate.raw1 = catalog.secondary_cost;
                candidate.raw2 = catalog.population_cost;
                auto& f = candidate.feature;
                f[2] = clamp01(static_cast<float>(candidate.raw0) / 1000.0f);
                f[3] = clamp01(static_cast<float>(candidate.raw1) / 1000.0f);
                f[4] = clamp01(static_cast<float>(candidate.raw2) / 100.0f);
                produce_candidates.emplace(candidate.key, candidate);
            }
            count = reference_list(input.catalog.unit_references,
                info.unit->type_id, 2, refs, kGameSessionUnitReferenceCapacity);
            for (u32 i = 0; i < count; ++i) {
                const u32 order_id = refs[i];
                if (order_id >= kResearchOrderLimit ||
                    input.catalog.research_catalog == nullptr) {
                    continue;
                }
                AiEntity2ResearchCatalogEntry catalog;
                if (!input.catalog.research_catalog(input.catalog.ctx, owner,
                        order_id, &catalog)) {
                    continue;
                }
                if (catalog.max_level != 0 &&
                    catalog.next_level > catalog.max_level) {
                    continue;   // fully researched
                }
                const u64 key = AiEntity2ResearchKey(order_id,
                    catalog.next_level);
                if (research_candidates.count(key) != 0) {
                    continue;
                }
                AiEntity2Candidate candidate;
                candidate.key = key;
                candidate.kind =
                    static_cast<u8>(AiEntity2CandidateKind::research_upgrade);
                candidate.object_id = static_cast<u16>(order_id);
                candidate.raw0 = catalog.primary_cost;
                candidate.raw1 = catalog.secondary_cost;
                candidate.raw2 = catalog.next_level;
                auto& f = candidate.feature;
                f[2] = clamp01(static_cast<float>(candidate.raw0) / 1000.0f);
                f[3] = clamp01(static_cast<float>(candidate.raw1) / 1000.0f);
                f[5] = catalog.max_level == 0 ? 0.0f :
                    clamp01(static_cast<float>(catalog.next_level) /
                        static_cast<float>(catalog.max_level));
                research_candidates.emplace(key, candidate);
            }
        }
        for (auto& entry : produce_candidates) {
            const auto it = completed_producers_by_unit_type.find(
                static_cast<u32>(entry.first));
            entry.second.feature[5] = it == completed_producers_by_unit_type.end() ?
                0.0f : clamp01(static_cast<float>(it->second) / 8.0f);
        }
    }
    if (resource_candidates.size() > kAiEntity2CandidateSegmentLimit ||
        build_candidates.size() > kAiEntity2CandidateSegmentLimit ||
        produce_candidates.size() > kAiEntity2CandidateSegmentLimit ||
        research_candidates.size() > kAiEntity2CandidateSegmentLimit) {
        snapshot.contract_error = true;
        snapshot.error = "economy candidate segment above the 2048 cap";
        return snapshot;
    }
    snapshot.candidates.reserve(resource_candidates.size() +
        build_candidates.size() + produce_candidates.size() +
        research_candidates.size());
    for (const AiEntity2Candidate& candidate : resource_candidates) {
        snapshot.candidates.push_back(candidate);
    }
    for (const auto& entry : build_candidates) {
        snapshot.candidates.push_back(entry.second);
    }
    for (const auto& entry : produce_candidates) {
        snapshot.candidates.push_back(entry.second);
    }
    for (const auto& entry : research_candidates) {
        snapshot.candidates.push_back(entry.second);
    }
    snapshot.resource_rows = static_cast<u32>(resource_candidates.size());
    snapshot.build_rows = static_cast<u32>(build_candidates.size());
    snapshot.produce_rows = static_cast<u32>(produce_candidates.size());
    snapshot.research_rows = static_cast<u32>(research_candidates.size());
    const u32 candidate_count = snapshot.candidate_rows();
    const u32 resource_begin = 0;
    const u32 build_begin = snapshot.resource_rows;
    const u32 produce_begin = build_begin + snapshot.build_rows;
    const u32 research_begin = produce_begin + snapshot.produce_rows;

    // Research orders already claimed by an awaiting event of any source
    // (research lock; plan 8.3 / 11.3).
    std::unordered_set<u32> research_orders_claimed;
    if (store != nullptr) {
        for (const AiEntity2EconomyEvent& event : store->events) {
            if (event.controller_owner == owner &&
                event.command == AiEntity2Command::research_upgrade &&
                event.research_claimed) {
                research_orders_claimed.insert(event.object_id);
            }
        }
    }
    // Sites reserved by other sources' walking/awaiting BUILD latches.
    std::vector<std::pair<AiEntityKey, AiEntity2TileRect>> reserved_sites;
    if (store != nullptr) {
        for (const auto& entry : store->economy) {
            const AiEntity2EconomyOrder& order = entry.second;
            if (order.controller_owner != owner ||
                order.command != AiEntity2Command::build || !order.site_claimed) {
                continue;
            }
            AiEntity2TileRect rect;
            rect.x0 = order.x >> 5;
            rect.y0 = order.y >> 5;
            rect.x1 = rect.x0 + static_cast<i32>(order.footprint_width);
            rect.y1 = rect.y0 + static_cast<i32>(order.footprint_height);
            reserved_sites.emplace_back(order.source, rect);
        }
    }

    // ---- own rows ----
    const u32 own_count = static_cast<u32>(own_entries.size());
    const u32 target_count = static_cast<u32>(snapshot.targets.size());
    const u32 pair_words = (target_count + 31u) / 32u;
    const u32 econ_words = (candidate_count + 31u) / 32u;
    snapshot.attack_pair_mask.assign(
        static_cast<std::size_t>(own_count) * pair_words, 0u);
    snapshot.economy_pair_mask.assign(
        static_cast<std::size_t>(own_count) * econ_words, 0u);
    snapshot.own.reserve(own_count);
    snapshot.own_appendix.reserve(own_count);
    std::vector<u8> candidate_available(candidate_count, 0u);

    for (u32 own_index = 0; own_index < own_count; ++own_index) {
        const OwnSourceInfo& info = sources[own_index];
        const AiObservedUnit& unit = *info.unit;
        AiEntityOwnRow row;
        AiEntity2OwnAppendix appendix;
        row.key = info.key;
        row.control_epoch = epoch_of(unit);
        row.x = unit.x;
        row.y = unit.y;
        row.type_id = static_cast<u16>(unit.type_id);
        row.movement_class = unit.movement_class;
        row.distance_check_mode = unit.distance_check_mode;
        row.role = static_cast<u8>(info.role);
        row.render_class = unit.render_class;
        row.command_base_state = unit.command_state & kUnitCommandStateMask;
        row.command_state_high_flags =
            unit.command_state & ~kUnitCommandStateMask;
        row.unit_command_flags = unit.command_flags;
        row.movement_state = unit.movement_state;
        row.attackable_class_mask = unit.attackable_class_mask;
        const bool mobile = unit.type_id < kAiEntity2MobileTypeLimit;
        const bool engine_idle = row.command_base_state == 0u ||
            row.command_base_state == 1u;
        const bool carrying = (unit.command_flags & kAiEntity2CarryFlag) != 0;

        u8 presence = kAiEntityPresenceDestination | kAiEntityPresencePathTarget;
        if (unit.render_class >= kAiEntityRenderClassLimit) {
            presence |= kAiEntityPresenceRenderClassOob;
        }
        if (row.command_base_state >= kAiEntityCommandBaseStateLimit) {
            presence |= kAiEntityPresenceCommandBaseOob;
        }
        if (unit.movement_state >= kAiEntityMovementStateLimit) {
            presence |= kAiEntityPresenceMovementStateOob;
        }
        if (unit.movement_class >= kAiEntityMovementClassLimit) {
            presence |= kAiEntityPresenceMovementClassOob;
        }
        i32 engine_target_row = -1;
        if (unit.target_id != 0) {
            const auto it = target_row_by_id.find(unit.target_id);
            if (it != target_row_by_id.end()) {
                engine_target_row = it->second;
                presence |= kAiEntityPresenceEngineTarget;
            }
        }
        row.active_target_row = engine_target_row;

        // ---- continuous features (ENTCMD01 prefix) ----
        auto& f = row.feature;
        f[0] = clamp01(static_cast<float>(unit.x) / width_safe);
        f[1] = clamp01(static_cast<float>(unit.y) / height_safe);
        f[2] = ratio_or_zero(unit.health, unit.max_health);
        f[3] = ratio_or_zero(unit.secondary_value, unit.max_secondary_value);
        f[4] = clamp01(static_cast<float>(unit.action_mode) /
            kAiEntityNormActionModeScale);
        f[5] = clamp01(static_cast<float>(unit.movement_step_limit) /
            (static_cast<float>(std::max(unit.movement_period, 1u)) *
                kAiEntityNormSpeedScale));
        f[6] = clamp01(static_cast<float>(unit.sight_range) / diagonal_safe);
        f[7] = clamp01(static_cast<float>(unit.attack_range) / diagonal_safe);
        f[8] = clamp01(
            static_cast<float>(unit.attack_range_vs_air) / diagonal_safe);
        f[9] = clamp01(
            static_cast<float>(unit.attack_power) / kAiEntityNormPowerScale);
        f[10] = clamp01(
            static_cast<float>(unit.defense_power) / kAiEntityNormPowerScale);
        f[11] = clamp01(static_cast<float>(unit.attack_cooldown_ticks) /
            kAiEntityNormRecoveryScale);
        f[12] = clamp01(static_cast<float>(unit.command_entry_lockout_ticks) /
            kAiEntityNormLockoutScale);
        f[13] = clamp01(static_cast<float>(unit.command_lockout_ticks) /
            kAiEntityNormLockoutScale);
        f[14] = clamp01(static_cast<float>(unit.effect_timer) /
            kAiEntityNormEffectTimerScale);
        direction_sin_cos(unit.direction, unit.use_16_direction_lookup,
            &f[15], &f[16]);
        f[17] = static_cast<float>(unit.destination_x - unit.x) / diagonal_safe;
        f[18] = static_cast<float>(unit.destination_y - unit.y) / diagonal_safe;
        f[19] = static_cast<float>(unit.path_target_x - unit.x) / diagonal_safe;
        f[20] = static_cast<float>(unit.path_target_y - unit.y) / diagonal_safe;
        if (engine_target_row >= 0) {
            const AiEntityTargetRow& target =
                snapshot.targets[static_cast<std::size_t>(engine_target_row)];
            const float dx = static_cast<float>(target.x - unit.x);
            const float dy = static_cast<float>(target.y - unit.y);
            f[21] = dx / diagonal_safe;
            f[22] = dy / diagonal_safe;
            f[23] = clamp01(std::sqrt(dx * dx + dy * dy) / diagonal_safe);
        }
        f[31] = clamp01(static_cast<float>(unit.level) / kAiEntityNormLevelScale);
        f[32] = clamp01(static_cast<float>(unit.experience) /
            kAiEntityNormExperienceScale);

        // ---- latch-derived order fields (plan 11 completion condition) ----
        row.semantic_order = engine_idle ?
            static_cast<u8>(AiEntity2WireSemanticOrder::none) :
            static_cast<u8>(AiEntity2WireSemanticOrder::external_unknown);
        row.order_status = static_cast<u8>(AiEntityOrderStatus::none);
        row.engine_order_match = kAiEntityEngineOrderNoRecord;
        row.last_attempt_command = kAiEntityLastAttemptNone;
        row.last_attempt_result = kAiEntityLastAttemptNone;
        row.last_reject_code = 0;
        if (store != nullptr) {
            const u64 packed = AiEntityPackKey(info.key);
            const auto combat_it = store->combat.find(packed);
            const auto economy_it = store->economy.find(packed);
            auto age = [&](u32 since, float scale) {
                return clamp01(static_cast<float>(
                    frame >= since ? frame - since : 0u) / scale);
            };
            if (economy_it != store->economy.end()) {
                const AiEntity2EconomyOrder& order = economy_it->second;
                row.semantic_order = static_cast<u8>(
                    AiEntity2WireSemanticOrderOfCommand(order.command));
                row.order_status = static_cast<u8>(order.status);
                const float dx = static_cast<float>(order.x - unit.x);
                const float dy = static_cast<float>(order.y - unit.y);
                f[24] = dx / diagonal_safe;
                f[25] = dy / diagonal_safe;
                f[26] = clamp01(std::sqrt(dx * dx + dy * dy) / diagonal_safe);
                f[27] = age(order.issued_frame, kAiEntityNormOrderAgeScale);
                f[28] = age(order.issued_frame, kAiEntityNormIssueAgeScale);
                f[29] = clamp01(static_cast<float>(order.idle_frames) /
                    kAiEntityNormIdleScale);
                presence |= kAiEntityPresenceSemanticPoint;
                if (harvest_family_state(row.command_base_state) ||
                    build_walk_state(row.command_base_state)) {
                    row.engine_order_match = kAiEntityEngineOrderMatch;
                } else if (engine_idle) {
                    row.engine_order_match = kAiEntityEngineOrderCleared;
                } else {
                    row.engine_order_match = kAiEntityEngineOrderDifferent;
                }
            } else if (combat_it != store->combat.end()) {
                const AiEntityActiveOrder& order = combat_it->second;
                row.semantic_order = static_cast<u8>(
                    AiEntity2WireSemanticOrderOfCommand(
                        static_cast<AiEntity2Command>(order.command)));
                row.order_status = static_cast<u8>(order.status);
                if (order.command != static_cast<u8>(AiEntity2Command::attack_unit) &&
                    order.command != static_cast<u8>(AiEntity2Command::hold_position) &&
                    order.command != static_cast<u8>(AiEntity2Command::stop)) {
                    const float dx = static_cast<float>(order.target_x - unit.x);
                    const float dy = static_cast<float>(order.target_y - unit.y);
                    f[24] = dx / diagonal_safe;
                    f[25] = dy / diagonal_safe;
                    f[26] = clamp01(std::sqrt(dx * dx + dy * dy) / diagonal_safe);
                    presence |= kAiEntityPresenceSemanticPoint;
                }
                f[27] = age(order.issued_frame, kAiEntityNormOrderAgeScale);
                f[28] = age(order.last_issue_frame, kAiEntityNormIssueAgeScale);
                f[29] = clamp01(static_cast<float>(order.idle_candidate_frames) /
                    kAiEntityNormIdleScale);
                f[30] = age(order.last_progress_frame,
                    kAiEntityNormProgressAgeScale);
            } else {
                // Most recent event of the source (enqueue orders).
                const AiEntity2EconomyEvent* latest = nullptr;
                for (const AiEntity2EconomyEvent& event : store->events) {
                    if (event.source == info.key &&
                        (latest == nullptr ||
                            event.issued_frame >= latest->issued_frame)) {
                        latest = &event;
                    }
                }
                if (latest != nullptr) {
                    row.semantic_order = static_cast<u8>(
                        AiEntity2WireSemanticOrderOfCommand(latest->command));
                    switch (latest->status) {
                    case AiEntity2EventStatus::awaiting_apply:
                        row.order_status =
                            static_cast<u8>(AiEntityOrderStatus::awaiting_apply);
                        break;
                    case AiEntity2EventStatus::engine_queued:
                        row.order_status =
                            static_cast<u8>(AiEntityOrderStatus::active);
                        break;
                    case AiEntity2EventStatus::completed:
                        row.order_status =
                            static_cast<u8>(AiEntityOrderStatus::completed);
                        break;
                    default:
                        row.order_status =
                            static_cast<u8>(AiEntityOrderStatus::interrupted);
                        break;
                    }
                    f[27] = age(latest->issued_frame, kAiEntityNormOrderAgeScale);
                    f[28] = age(latest->issued_frame, kAiEntityNormIssueAgeScale);
                }
            }
            const auto attempt_it = store->attempts.find(packed);
            if (attempt_it != store->attempts.end()) {
                row.last_attempt_command = attempt_it->second.requested_command;
                row.last_attempt_result =
                    static_cast<u8>(attempt_it->second.result);
                row.last_reject_code =
                    static_cast<u16>(attempt_it->second.reject_code);
            }
        }

        // ---- point mask (mobile rows only) ----
        row.point_mask.fill(0);
        if (mobile && input.movement_map != nullptr) {
            const auto it = reachability.find(unit.movement_class);
            if (it != reachability.end()) {
                BuildAiEntityPointMask(*input.movement_map, it->second,
                    unit.x, unit.y, row.point_mask);
            }
        }
        const bool any_point = row.point_mask[0] != 0 ||
            row.point_mask[1] != 0 || row.point_mask[2] != 0;
        const AiEntityReachability* reach = nullptr;
        if (mobile) {
            const auto it = reachability.find(unit.movement_class);
            if (it != reachability.end()) {
                reach = &it->second;
            }
        }

        // ---- attack pair row (capability-driven, any role) ----
        bool any_pair = false;
        if (info.completed && (unit.type_flags & kAttackBit) != 0) {
            AiEntityPairSource pair_source;
            pair_source.runtime_id = unit.id;
            pair_source.active_owned_alive = true;
            pair_source.has_attack_capability = true;
            pair_source.distance_check_mode = unit.distance_check_mode;
            pair_source.attackable_class_mask = unit.attackable_class_mask;
            pair_source.render_class2_terrain_gate = 1;
            if (input.live.source_class2_gate != nullptr) {
                u32 gate = 1;
                if (input.live.source_class2_gate(input.live.ctx, unit.id,
                        &gate)) {
                    pair_source.render_class2_terrain_gate = gate;
                }
            }
            for (u32 target_index = 0; target_index < target_count;
                ++target_index) {
                const AiEntityTargetRow& target = snapshot.targets[target_index];
                AiEntityPairTarget pair_target;
                pair_target.runtime_id = target.key.runtime_id;
                pair_target.active_alive = true;
                pair_target.visible = true;
                pair_target.non_friendly = true;
                pair_target.runtime_flags = 0;
                if (input.live.unit_runtime_flags != nullptr) {
                    u32 flags = 0;
                    if (input.live.unit_runtime_flags(input.live.ctx,
                            target.key.runtime_id, &flags)) {
                        pair_target.runtime_flags = flags;
                    }
                }
                pair_target.render_class = target.render_class;
                pair_target.x = target.x;
                pair_target.y = target.y;
                const AiEntityPairDecision decision = AiEntityEvaluateAttackPair(
                    pair_source, pair_target, input.live.pair_hooks);
                if (decision.legal) {
                    any_pair = true;
                    snapshot.attack_pair_mask[
                        static_cast<std::size_t>(own_index) * pair_words +
                        (target_index >> 5)] |= 1u << (target_index & 31u);
                }
            }
        }

        // ---- appendix: capability, queue, state ----
        u32 capability = 0;
        if ((unit.type_flags & kMoveBit) != 0) capability |= kAiEntity2CapMove;
        if ((unit.type_flags & kAttackBit) != 0) capability |= kAiEntity2CapAttack;
        if ((unit.type_flags & kPatrolBit) != 0) capability |= kAiEntity2CapPatrol;
        if (mobile && (unit.type_flags & kMoveBit) != 0) {
            capability |= kAiEntity2CapHold;
        }
        if ((unit.type_flags & kHarvestBit) != 0) capability |= kAiEntity2CapHarvest;
        if ((unit.type_flags & kBuildBit) != 0) capability |= kAiEntity2CapBuild;
        u32 primary_refs[kGameSessionUnitReferenceCapacity];
        u32 alternate_refs[kGameSessionUnitReferenceCapacity];
        u32 completion_refs[kGameSessionUnitReferenceCapacity];
        const u32 primary_count = reference_list(input.catalog.unit_references,
            unit.type_id, 0, primary_refs, kGameSessionUnitReferenceCapacity);
        const u32 alternate_count = reference_list(input.catalog.unit_references,
            unit.type_id, 1, alternate_refs, kGameSessionUnitReferenceCapacity);
        const u32 completion_count = reference_list(
            input.catalog.unit_references, unit.type_id, 2, completion_refs,
            kGameSessionUnitReferenceCapacity);
        if (!mobile && alternate_count != 0) capability |= kAiEntity2CapProduce;
        if (!mobile && completion_count != 0) capability |= kAiEntity2CapResearch;
        appendix.capability_bits = capability;
        appendix.queued_production_type_id = unit.queued_production_type_id != 0 ?
            unit.queued_production_type_id : kAiEntity2TypeSentinel;
        appendix.production_variant = unit.production_variant;
        appendix.walking_build_type_id = info.walking_build_type != 0 ?
            info.walking_build_type : kAiEntity2TypeSentinel;
        appendix.cargo_ratio = carrying ?
            ratio_or_zero(unit.cargo_amount, unit.cargo_capacity) : 0.0f;

        // Effective queue: engine active + deferred + awaiting events.
        u32 slot_count = 0;
        u32 engine_deferred = 0;
        bool research_busy = false;
        bool production_busy = false;
        {
            AiEntity2QueueOriginView origins[11];
            u32 origin_count = 0;
            if (input.economy_live.unit_queue_origins != nullptr) {
                origin_count = input.economy_live.unit_queue_origins(
                    input.economy_live.ctx, unit.id, origins, 11);
            }
            auto origin_of = [&](u32 slot, AiEntity2QueueSlot& out) {
                out.origin_channel = kAiEntity2QueueOriginUnknown;
                out.origin_sequence = 0;
                if (slot < origin_count && origins[slot].channel !=
                        kUnitCommandOriginInvalidChannel &&
                    origins[slot].channel < 0xffffu) {
                    out.origin_channel = static_cast<u16>(origins[slot].channel);
                    out.origin_sequence = origins[slot].sequence;
                }
            };
            auto push_slot = [&](const AiEntity2QueueSlot& slot) {
                if (slot_count < kAiEntity2QueueSlotCount) {
                    appendix.queue[slot_count] = slot;
                }
                ++slot_count;
            };
            if (!mobile && research_state(row.command_base_state)) {
                AiEntity2QueueSlot slot;
                slot.kind = kAiEntity2QueueKindResearch;
                slot.status = kAiEntity2QueueStatusEngineActive;
                slot.object_id = unit.command_value;
                slot.queue_ordinal = 0;
                origin_of(0, slot);
                push_slot(slot);
                research_busy = true;
            } else if (!mobile && unit.queued_production_type_id != 0) {
                AiEntity2QueueSlot slot;
                slot.kind = kAiEntity2QueueKindProduce;
                slot.status = kAiEntity2QueueStatusEngineActive;
                slot.object_id = unit.queued_production_type_id;
                slot.queue_ordinal = 0;
                origin_of(0, slot);
                push_slot(slot);
                production_busy = true;
            }
            const u32 deferred_count = std::min<u32>(unit.deferred_command_count,
                static_cast<u32>(unit.deferred_commands.size()));
            for (u32 k = 0; k < deferred_count; ++k) {
                const AiObservedQueuedCommand& command = unit.deferred_commands[k];
                const u32 state = command.state & kUnitCommandStateMask;
                AiEntity2QueueSlot slot;
                if (state == kAiEntity2DeferredProduceState) {
                    slot.kind = kAiEntity2QueueKindProduce;
                } else if (state == kAiEntity2DeferredResearchState) {
                    slot.kind = kAiEntity2QueueKindResearch;
                } else {
                    ++engine_deferred;
                    continue;
                }
                slot.status = kAiEntity2QueueStatusEngineDeferred;
                slot.object_id = static_cast<u32>(command.command_value_or_target);
                slot.queue_ordinal = ++engine_deferred;
                origin_of(1 + k, slot);
                push_slot(slot);
            }
            if (store != nullptr) {
                for (const AiEntity2EconomyEvent& event : store->events) {
                    if (event.source != info.key ||
                        event.status != AiEntity2EventStatus::awaiting_apply) {
                        continue;
                    }
                    AiEntity2QueueSlot slot;
                    slot.kind = event.command == AiEntity2Command::produce_unit ?
                        kAiEntity2QueueKindProduce : kAiEntity2QueueKindResearch;
                    slot.status = kAiEntity2QueueStatusAwaitingApply;
                    slot.object_id = event.object_id;
                    slot.queue_ordinal = event.queue_ordinal;
                    slot.origin_channel = event.origin.valid() &&
                        event.origin.channel < 0xffffu ?
                        static_cast<u16>(event.origin.channel) :
                        kAiEntity2QueueOriginUnknown;
                    slot.origin_sequence = event.origin.valid() ?
                        event.origin.sequence : 0u;
                    push_slot(slot);
                }
            }
        }
        if (slot_count > kAiEntity2QueueSlotCount) {
            snapshot.contract_error = true;
            snapshot.error = "effective queue exceeds five slots";
            return snapshot;
        }
        const u32 effective_deferred = std::min<u32>(
            engine_deferred + info.awaiting_events, kAiEntity2ProductionQueueLimit);
        appendix.deferred_command_count = effective_deferred;
        appendix.queue_fill_ratio = clamp01(static_cast<float>(
            effective_deferred) / static_cast<float>(kAiEntity2ProductionQueueLimit));

        u32 state_bits = 0;
        if (info.completed) state_bits |= kAiEntity2StateCompleted;
        if (unit.under_construction) state_bits |= kAiEntity2StateUnderConstruction;
        if (carrying) state_bits |= kAiEntity2StateCargoNonzero;
        if (effective_deferred >= kAiEntity2ProductionQueueLimit) {
            state_bits |= kAiEntity2StateQueueFull;
        }
        if (info.economy_order != nullptr) {
            state_bits |= kAiEntity2StateActiveEconomyOrder;
        }
        if (info.awaiting_events != 0 ||
            (info.economy_order != nullptr &&
                (info.economy_order->cost_claimed ||
                    info.economy_order->site_claimed))) {
            state_bits |= kAiEntity2StateOutstandingReservation;
        }
        appendix.source_state_bits = state_bits;
        appendix.active_economy_candidate_row = -1;
        if (info.economy_order != nullptr) {
            appendix.active_economy_candidate_row = snapshot.candidate_row_of(
                info.economy_order->candidate_kind,
                info.economy_order->candidate_key);
        }

        // ---- economy pair row ----
        bool any_resource = false;
        bool any_build = false;
        bool any_produce = false;
        bool any_research = false;
        auto set_econ_bit = [&](u32 candidate_index) {
            snapshot.economy_pair_mask[
                static_cast<std::size_t>(own_index) * econ_words +
                (candidate_index >> 5)] |= 1u << (candidate_index & 31u);
            candidate_available[candidate_index] = 1u;
        };
        // Source-state table (plan 5 + the §4a relaxation): a worker on an
        // active HARVEST keeps BUILD (when not carrying) and STOP open; a
        // walking BUILD source is KEEP-only.
        const bool harvest_locked = info.economy_order != nullptr &&
            info.economy_order->command == AiEntity2Command::harvest;
        const bool build_locked = info.economy_order != nullptr &&
            info.economy_order->command == AiEntity2Command::build;
        const bool worker_free = info.role == AiEntity2Role::worker &&
            info.completed && info.economy_order == nullptr;
        const bool worker_can_build = info.role == AiEntity2Role::worker &&
            info.completed && !build_locked;
        if (worker_free && (unit.type_flags & kHarvestBit) != 0) {
            for (u32 c = resource_begin; c < resource_begin + snapshot.resource_rows;
                ++c) {
                const AiEntity2Candidate& candidate = snapshot.candidates[c];
                if (reach != nullptr) {
                    AiEntity2TileRect rect;
                    rect.x0 = candidate.x >> 5;
                    rect.y0 = candidate.y >> 5;
                    rect.x1 = rect.x0 + 1;
                    rect.y1 = rect.y0 + 1;
                    if (!ring_reachable(*reach, unit.x, unit.y, rect)) {
                        continue;
                    }
                }
                any_resource = true;
                set_econ_bit(c);
            }
        }
        // 2026-09-04 user rule: a harvesting worker (carrying or not) may be
        // sent to build — the engine accepts it — so the carry flag is not a
        // BUILD gate; only a walking BUILD source stays KEEP-only.
        if (worker_can_build && (unit.type_flags & kBuildBit) != 0) {
            for (u32 c = build_begin; c < build_begin + snapshot.build_rows; ++c) {
                const AiEntity2Candidate& candidate = snapshot.candidates[c];
                if ((candidate.flags & kAiEntity2CandidateFlagExplored) == 0 ||
                    (candidate.flags & kAiEntity2CandidateFlagActiveOrReserved) != 0) {
                    continue;
                }
                if (!list_contains(primary_refs, primary_count,
                        candidate.object_id)) {
                    continue;
                }
                const BuildTypeInfo* type_info = nullptr;
                for (const BuildTypeInfo& entry : build_types) {
                    if (entry.type_id == candidate.object_id) {
                        type_info = &entry;
                    }
                }
                if (type_info == nullptr || !type_info->catalog.prerequisites_ok) {
                    continue;
                }
                if (candidate.raw0 > snapshot.spendable_primary ||
                    candidate.raw1 > snapshot.spendable_secondary) {
                    continue;
                }
                const AiEntity2TileRect rect = AiEntity2FootprintRectOf(candidate);
                bool reserved = false;
                for (const auto& site : reserved_sites) {
                    if (!(site.first == info.key) &&
                        AiEntity2RectsOverlap(site.second, rect)) {
                        reserved = true;
                        break;
                    }
                }
                if (reserved) {
                    continue;
                }
                if (reach != nullptr && !ring_reachable(*reach, unit.x, unit.y,
                        rect)) {
                    continue;
                }
                any_build = true;
                set_econ_bit(c);
            }
        }
        if (info.role == AiEntity2Role::building && info.completed) {
            if (effective_deferred < kAiEntity2ProductionQueueLimit) {
                for (u32 c = produce_begin; c < produce_begin + snapshot.produce_rows;
                    ++c) {
                    const AiEntity2Candidate& candidate = snapshot.candidates[c];
                    if (!list_contains(alternate_refs, alternate_count,
                            candidate.object_id)) {
                        continue;
                    }
                    AiEntity2UnitCatalogEntry catalog;
                    if (input.catalog.unit_catalog != nullptr &&
                        (!input.catalog.unit_catalog(input.catalog.ctx, owner,
                            candidate.object_id, &catalog) ||
                            !catalog.prerequisites_ok)) {
                        continue;
                    }
                    if (candidate.raw0 > snapshot.spendable_primary ||
                        candidate.raw1 > snapshot.spendable_secondary ||
                        candidate.raw2 > snapshot.spendable_population) {
                        continue;
                    }
                    any_produce = true;
                    set_econ_bit(c);
                }
            }
            const bool researcher_idle = !research_busy && !production_busy &&
                engine_deferred == 0 && info.awaiting_events == 0;
            if (researcher_idle) {
                for (u32 c = research_begin;
                    c < research_begin + snapshot.research_rows; ++c) {
                    const AiEntity2Candidate& candidate = snapshot.candidates[c];
                    if (!list_contains(completion_refs, completion_count,
                            candidate.object_id)) {
                        continue;
                    }
                    AiEntity2ResearchCatalogEntry catalog;
                    if (input.catalog.research_catalog == nullptr ||
                        !input.catalog.research_catalog(input.catalog.ctx, owner,
                            candidate.object_id, &catalog) ||
                        !catalog.prerequisites_ok || catalog.max_level == 0 ||
                        catalog.next_level > catalog.max_level ||
                        catalog.next_level != candidate.raw2) {
                        continue;
                    }
                    if (candidate.raw0 > snapshot.spendable_primary ||
                        candidate.raw1 > snapshot.spendable_secondary) {
                        continue;
                    }
                    if (research_orders_claimed.count(candidate.object_id) != 0) {
                        continue;
                    }
                    any_research = true;
                    set_econ_bit(c);
                }
            }
        }

        // ---- command mask (policy vocabulary, action v4 role table) ----
        //   fighter               KEEP MOVE ATTACK_MOVE PATROL ATTACK_UNIT HOLD
        //   idle worker           KEEP HARVEST BUILD
        //   harvesting worker     KEEP BUILD (a harvest that ended reopens
        //                         HARVEST: the order is then no longer active)
        //   threatened worker     KEEP, MOVE to local tokens only, ATTACK_UNIT
        //                         within kAiEntity2WorkerThreatRadiusPx
        //   awaiting ACK / build  KEEP
        //   building              KEEP PRODUCE RESEARCH
        //   stuck                 no policy choice: watchdog reset (engine STOP)
        u32 command_mask = 1u << static_cast<u32>(AiEntity2PolicyCommand::keep);
        const bool context_row = !info.completed ||
            info.role == AiEntity2Role::transport ||
            info.role == AiEntity2Role::other;
        const bool fighter = !context_row && mobile &&
            (info.role == AiEntity2Role::melee || info.role == AiEntity2Role::ranged);
        const bool worker = !context_row && info.role == AiEntity2Role::worker;
        const bool awaiting_ack = info.economy_order != nullptr &&
            info.economy_order->status == AiEntityOrderStatus::awaiting_apply;
        const bool worker_blocked = worker && (awaiting_ack || build_locked);
        bool worker_threatened = false;
        if (worker && !worker_blocked) {
            const i64 radius = static_cast<i64>(kAiEntity2WorkerThreatRadiusPx);
            for (const AiEntityTargetRow& target : snapshot.targets) {
                if ((target.kind_bits & kAiEntityTargetKindMobile) == 0 ||
                    (target.kind_bits & kAiEntityTargetKindNeutral) != 0 ||
                    target.feature[8] <= 0.0f) {
                    continue;   // structures, neutrals, unarmed
                }
                const i64 dx = static_cast<i64>(target.x) - unit.x;
                const i64 dy = static_cast<i64>(target.y) - unit.y;
                if (dx * dx + dy * dy <= radius * radius) {
                    worker_threatened = true;
                    break;
                }
            }
        }
        if (fighter) {
            if ((unit.type_flags & kMoveBit) != 0 && any_point) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::move);
            }
            if ((unit.type_flags & kAttackBit) != 0 && any_point) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::attack_move);
            }
            if ((unit.type_flags & kPatrolBit) != 0 && any_point) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::patrol);
            }
            if ((capability & kAiEntity2CapHold) != 0) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::hold);
            }
            if (any_pair) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::attack_unit);
            }
        } else if (worker && worker_threatened) {
            // Retreat moves only: the global cell tokens are cleared, the
            // 32 local tokens stay; attacks only against threats in radius.
            row.point_mask[0] = 0;
            row.point_mask[1] = 0;
            if ((unit.type_flags & kMoveBit) != 0 && row.point_mask[2] != 0) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::move);
            }
            bool any_close_pair = false;
            for (u32 t = 0; t < snapshot.targets.size(); ++t) {
                const std::size_t word = static_cast<std::size_t>(own_index) * pair_words +
                    (t >> 5);
                if (((snapshot.attack_pair_mask[word] >> (t & 31u)) & 1u) == 0) {
                    continue;
                }
                const AiEntityTargetRow& target = snapshot.targets[t];
                const i64 dx = static_cast<i64>(target.x) - unit.x;
                const i64 dy = static_cast<i64>(target.y) - unit.y;
                const i64 radius = static_cast<i64>(kAiEntity2WorkerThreatRadiusPx);
                if (dx * dx + dy * dy > radius * radius) {
                    snapshot.attack_pair_mask[word] &= ~(1u << (t & 31u));
                } else {
                    any_close_pair = true;
                }
            }
            if (any_close_pair) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::attack_unit);
            }
        } else if (worker) {
            // Calm worker: no point moves and no attacks; the pair row is
            // cleared so the wire carries exactly what the mask allows.
            for (u32 w = 0; w < pair_words; ++w) {
                snapshot.attack_pair_mask[static_cast<std::size_t>(own_index) * pair_words +
                    w] = 0;
            }
        }
        if (worker && !worker_blocked && !worker_threatened) {
            if (any_resource && !harvest_locked) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::harvest);
            }
            if (any_build) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::build);
            }
        }
        if (info.role == AiEntity2Role::building) {
            if (any_produce) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::produce_unit);
            }
            if (any_research) {
                command_mask |= 1u << static_cast<u32>(AiEntity2PolicyCommand::research_upgrade);
            }
        }
        if (!worker && !fighter && info.role != AiEntity2Role::building) {
            // Context rows / transports / others: KEEP only.
            command_mask = 1u << static_cast<u32>(AiEntity2PolicyCommand::keep);
        }

        // ---- team-intent slot fields + disobedience mask ----
        const u8 slot = row_slot[own_index];
        appendix.slot_id = slot;
        appendix.assign_mask = 0;
        appendix.slot_order_relation = kAiEntity2SlotRelationNone;
        if (slot != kAiEntity2SlotNone) {
            const auto cooldown_it = slot_state.assigned_frame.find(AiEntityPackKey(info.key));
            const bool assign_cooling = cooldown_it != slot_state.assigned_frame.end() &&
                frame >= cooldown_it->second &&
                frame - cooldown_it->second < kAiEntity2AssignCooldownFrames;
            for (u8 target = 0; target < kAiEntity2SlotCount && !assign_cooling; ++target) {
                if (target == slot) {
                    continue;
                }
                if (target == kAiEntity2SlotScout &&
                    snapshot.scout_free_at_snapshot == 0) {
                    continue;
                }
                appendix.assign_mask |= 1u << target;
            }
            const AiEntity2SlotOrder& slot_order = slot_state.orders[slot];
            if (slot_order.active) {
                const AiEntityActiveOrder* latch = nullptr;
                if (store != nullptr) {
                    const auto it = store->combat.find(AiEntityPackKey(info.key));
                    if (it != store->combat.end()) {
                        latch = &it->second;
                    }
                }
                const bool tracking = latch != nullptr &&
                    (latch->status == AiEntityOrderStatus::awaiting_apply ||
                        latch->status == AiEntityOrderStatus::active);
                const auto assigned_it =
                    slot_state.assigned_frame.find(AiEntityPackKey(info.key));
                const bool just_assigned = assigned_it != slot_state.assigned_frame.end() &&
                    frame - assigned_it->second < 8u;
                if (tracking && latch->origin_slot == slot) {
                    appendix.slot_order_relation = kAiEntity2SlotRelationMatch;
                } else if (tracking) {
                    appendix.slot_order_relation = kAiEntity2SlotRelationDiffers;
                } else if (just_assigned) {
                    appendix.slot_order_relation = kAiEntity2SlotRelationJustAssigned;
                } else {
                    appendix.slot_order_relation = kAiEntity2SlotRelationNone;
                }
                // Disobedience mask (user rule 2026-09-04): while the slot
                // marches, a member may not wander off on a personal point
                // order; ATTACK_UNIT / HOLD / STOP and slot moves stay open.
                if (AiEntity2SlotCommandIsPoint(slot_order.command)) {
                    command_mask &= ~((1u << static_cast<u32>(AiEntity2PolicyCommand::move)) |
                        (1u << static_cast<u32>(AiEntity2PolicyCommand::attack_move)) |
                        (1u << static_cast<u32>(AiEntity2PolicyCommand::patrol)));
                }
            }
        }
        row.command_mask = command_mask;
        row.presence_bits = presence;
        snapshot.own.push_back(row);
        snapshot.own_appendix.push_back(appendix);
    }

    // ---- slot blocks, slot masks, start candidates, intent material ----
    {
        std::array<i64, kAiEntity2SlotCount> sum_x{};
        std::array<i64, kAiEntity2SlotCount> sum_y{};
        std::array<std::array<u32, 2>, kAiEntity2SlotCount> cell_union{};
        std::array<u32, 2> any_union{};
        for (u32 own_index = 0; own_index < own_count; ++own_index) {
            const u8 slot = row_slot[own_index];
            if (slot == kAiEntity2SlotNone) {
                continue;
            }
            const AiEntityOwnRow& row = snapshot.own[own_index];
            AiEntity2SlotBlock& block = snapshot.slots[slot];
            ++block.member_count;
            sum_x[slot] += row.x;
            sum_y[slot] += row.y;
            cell_union[slot][0] |= row.point_mask[0];
            cell_union[slot][1] |= row.point_mask[1];
            any_union[0] |= row.point_mask[0];
            any_union[1] |= row.point_mask[1];
            const AiEntity2OwnAppendix& appendix = snapshot.own_appendix[own_index];
            if (appendix.slot_order_relation == kAiEntity2SlotRelationMatch) {
                ++block.pursuing;
            } else if (appendix.slot_order_relation == kAiEntity2SlotRelationDiffers) {
                ++block.differing;
            }
            if (store != nullptr) {
                const auto it = store->combat.find(AiEntityPackKey(row.key));
                if (it != store->combat.end() && it->second.origin_slot == slot &&
                    (it->second.status == AiEntityOrderStatus::completed ||
                        it->second.status == AiEntityOrderStatus::interrupted ||
                        it->second.status == AiEntityOrderStatus::stalled ||
                        it->second.status == AiEntityOrderStatus::target_lost)) {
                    ++block.terminal;
                }
            }
        }
        // HUNT_NEUTRAL needs a visible neutral monster somewhere on the map.
        bool neutral_visible = false;
        for (const AiEntityTargetRow& target : snapshot.targets) {
            if ((target.kind_bits & kAiEntityTargetKindNeutral) != 0 &&
                (target.kind_bits & kAiEntityTargetKindBuilding) == 0) {
                neutral_visible = true;
                break;
            }
        }
        for (u8 slot = 0; slot < kAiEntity2SlotCount; ++slot) {
            AiEntity2SlotBlock& block = snapshot.slots[slot];
            if (block.member_count != 0) {
                block.centroid_x = static_cast<i32>(sum_x[slot] / block.member_count);
                block.centroid_y = static_cast<i32>(sum_y[slot] / block.member_count);
            }
            const AiEntity2SlotOrder& order = slot_state.orders[slot];
            block.active = order.active ? 1u : 0u;
            block.command = order.active ? static_cast<u8>(order.command) : 0u;
            block.cell = order.active ? order.cell : -1;
            block.age_frames = order.active && frame >= order.issued_frame ?
                frame - order.issued_frame : 0u;
            // An empty slot may be commanded ahead of its members: any
            // combat unit could be assigned, so its cell mask is the union.
            snapshot.slot_cell_mask[slot] = block.member_count != 0 ?
                cell_union[slot] : any_union;
            u32 mask = 1u << static_cast<u32>(AiEntity2SlotCommand::keep);
            const bool any_cell = snapshot.slot_cell_mask[slot][0] != 0 ||
                snapshot.slot_cell_mask[slot][1] != 0;
            if (any_cell) {
                mask |= 1u << static_cast<u32>(AiEntity2SlotCommand::move);
                mask |= 1u << static_cast<u32>(AiEntity2SlotCommand::attack_move);
                mask |= 1u << static_cast<u32>(AiEntity2SlotCommand::patrol);
            }
            if (block.member_count != 0 || any_cell) {
                mask |= 1u << static_cast<u32>(AiEntity2SlotCommand::hold);
                if (neutral_visible) {
                    mask |= 1u << static_cast<u32>(AiEntity2SlotCommand::hunt_neutral);
                }
            }
            if (order.active) {
                mask |= 1u << static_cast<u32>(AiEntity2SlotCommand::stop);
            }
            snapshot.slot_command_mask[slot] = mask;
        }
        // Start candidates: the map's 2..8 start slots as 8x8 cells, with
        // their explored bit and whether it is our own start.
        auto cell_of = [&](i32 x, i32 y) -> i32 {
            if (x < 0 || y < 0) {
                return -1;
            }
            const u32 tile_x = static_cast<u32>(x) >> 5;
            const u32 tile_y = static_cast<u32>(y) >> 5;
            if (tile_x >= observation.map_width_tiles ||
                tile_y >= observation.map_height_tiles) {
                return -1;
            }
            auto cell_axis = [](u32 tile, u32 extent) {
                u32 cell = 0;
                for (u32 c = 1; c < kAiEntityPointGridWidth; ++c) {
                    const u32 lower = static_cast<u32>(
                        (static_cast<u64>(c) * extent) / kAiEntityPointGridWidth);
                    if (lower <= tile) {
                        cell = c;
                    }
                }
                return cell;
            };
            return static_cast<i32>(cell_axis(tile_y, observation.map_height_tiles) *
                kAiEntityPointGridWidth + cell_axis(tile_x, observation.map_width_tiles));
        };
        u64 candidate_count = 0;
        u64 explored_count = 0;
        for (u32 i = 0; i < kAiEntity2StartCandidateCount; ++i) {
            AiEntity2StartCandidate& candidate = snapshot.start_candidates[i];
            candidate = AiEntity2StartCandidate{};
            if ((observation.start_candidate_mask & (1u << i)) == 0) {
                continue;
            }
            const i32 x = observation.start_candidate_x[i];
            const i32 y = observation.start_candidate_y[i];
            candidate.cell = cell_of(x, y);
            if (candidate.cell < 0) {
                continue;
            }
            const AiObservedMapTile* tile = tile_at(observation,
                static_cast<u32>(x) >> 5, static_cast<u32>(y) >> 5);
            candidate.explored = tile != nullptr && tile->explored ? 1u : 0u;
            candidate.is_own = x == observation.start_x && y == observation.start_y ?
                1u : 0u;
            ++candidate_count;
            if (candidate.explored) {
                ++explored_count;
            }
        }
        snapshot.intent_reward_material[0] = explored_count;
        snapshot.intent_reward_material[1] = candidate_count;
        // Enemy base knowledge + army distance to the nearest remembered
        // enemy building (fog-honest memory kept by the pump).
        u64 enemy_known = 0;
        u64 nearest = 0xffffffffull;
        const AiEntity2SlotBlock& main = snapshot.slots[kAiEntity2SlotMain];
        i64 army_x = main.member_count != 0 ? main.centroid_x : start_x;
        i64 army_y = main.member_count != 0 ? main.centroid_y : start_y;
        if (observation.enemy_building_memory.size() == observation.tiles.size()) {
            for (u32 ty = 0; ty < observation.map_height_tiles; ++ty) {
                for (u32 tx = 0; tx < observation.map_width_tiles; ++tx) {
                    if (observation.enemy_building_memory[
                            static_cast<std::size_t>(ty) * observation.map_width_tiles +
                            tx] == 0) {
                        continue;
                    }
                    enemy_known = 1;
                    const i64 dx = static_cast<i64>(tx) * 32 + 16 - army_x;
                    const i64 dy = static_cast<i64>(ty) * 32 + 16 - army_y;
                    const u64 distance = static_cast<u64>(
                        std::sqrt(static_cast<double>(dx * dx + dy * dy)));
                    nearest = std::min(nearest, distance);
                }
            }
        }
        snapshot.intent_reward_material[2] = enemy_known;
        snapshot.intent_reward_material[3] = nearest;
    }

    // ---- any-source availability flag / feature 6 ----
    for (u32 c = 0; c < candidate_count; ++c) {
        AiEntity2Candidate& candidate = snapshot.candidates[c];
        if (candidate_available[c] != 0) {
            candidate.flags |= kAiEntity2CandidateFlagAnySourceAvailable;
        }
        if (candidate.kind != static_cast<u8>(AiEntity2CandidateKind::resource)) {
            candidate.feature[6] = candidate_available[c] != 0 ? 1.0f : 0.0f;
        }
    }

    // ---- reward material (plan 14.2), checked u64 ----
    {
        std::array<u64, 10>& material = snapshot.economy_reward_material;
        material.fill(0);
        material[0] = observation.primary_resources;
        material[1] = observation.secondary_resources;
        material[9] = std::min(observation.population_used,
            observation.population_limit);
        bool ok = true;
        auto unit_cost = [&](u32 type_id) -> u64 {
            AiEntity2UnitCatalogEntry catalog;
            if (input.catalog.unit_catalog == nullptr ||
                !input.catalog.unit_catalog(input.catalog.ctx, owner, type_id,
                    &catalog)) {
                return 0;
            }
            return static_cast<u64>(catalog.primary_cost) +
                catalog.secondary_cost;
        };
        for (const OwnSourceInfo& info : sources) {
            const AiObservedUnit& unit = *info.unit;
            const u64 cost = unit_cost(unit.type_id);
            if (unit.under_construction) {
                ok = ok && add_checked(material[5], cost);
                continue;
            }
            if (info.role == AiEntity2Role::worker) {
                ok = ok && add_checked(material[2], cost);
            } else if (unit.type_id < kAiEntity2MobileTypeLimit) {
                ok = ok && add_checked(material[3], cost);
            } else {
                ok = ok && add_checked(material[4], cost);
                if (unit.queued_production_type_id != 0) {
                    ok = ok && add_checked(material[6],
                        unit_cost(unit.queued_production_type_id));
                }
                if (research_state(unit.command_state & kUnitCommandStateMask) &&
                    input.catalog.research_catalog != nullptr) {
                    AiEntity2ResearchCatalogEntry catalog;
                    if (input.catalog.research_catalog(input.catalog.ctx, owner,
                            unit.command_value, &catalog)) {
                        ok = ok && add_checked(material[8],
                            static_cast<u64>(catalog.primary_cost) +
                                catalog.secondary_cost);
                    }
                }
                const u32 deferred_count = std::min<u32>(
                    unit.deferred_command_count,
                    static_cast<u32>(unit.deferred_commands.size()));
                for (u32 k = 0; k < deferred_count; ++k) {
                    const AiObservedQueuedCommand& command =
                        unit.deferred_commands[k];
                    const u32 state = command.state & kUnitCommandStateMask;
                    const u32 value =
                        static_cast<u32>(command.command_value_or_target);
                    if (state == kAiEntity2DeferredProduceState) {
                        ok = ok && add_checked(material[6], unit_cost(value));
                    } else if (state == kAiEntity2DeferredResearchState &&
                        input.catalog.research_catalog != nullptr) {
                        AiEntity2ResearchCatalogEntry catalog;
                        if (input.catalog.research_catalog(input.catalog.ctx,
                                owner, value, &catalog)) {
                            ok = ok && add_checked(material[8],
                                static_cast<u64>(catalog.primary_cost) +
                                    catalog.secondary_cost);
                        }
                    }
                }
            }
        }
        if (input.catalog.research_catalog != nullptr) {
            for (u32 order = 0; order < kResearchOrderLimit; ++order) {
                if (observation.research_order_levels[order] == 0) {
                    continue;
                }
                AiEntity2ResearchCatalogEntry catalog;
                if (input.catalog.research_catalog(input.catalog.ctx, owner,
                        order, &catalog)) {
                    ok = ok && add_checked(material[7],
                        static_cast<u64>(catalog.invested_primary) +
                            catalog.invested_secondary);
                }
            }
        }
        if (!ok) {
            snapshot.contract_error = true;
            snapshot.error = "economy reward material overflow";
            return snapshot;
        }
    }
    return snapshot;
}

// ---------------------------------------------------------------------------
// Same-tick economy ledger (plan section 10)
// ---------------------------------------------------------------------------

void AiEntity2LedgerInit(AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot) {
    ledger.remaining_primary = snapshot.spendable_primary;
    ledger.remaining_secondary = snapshot.spendable_secondary;
    ledger.remaining_population = snapshot.spendable_population;
    ledger.reserved_sites.clear();
    ledger.reserved_site_keys.clear();
    ledger.reserved_research.clear();
}

bool AiEntity2LedgerCandidateAvailable(const AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, u32 index) {
    if (index >= snapshot.candidates.size()) {
        return false;
    }
    const AiEntity2Candidate& candidate = snapshot.candidates[index];
    switch (static_cast<AiEntity2CandidateKind>(candidate.kind)) {
    case AiEntity2CandidateKind::resource:
        return true;
    case AiEntity2CandidateKind::build_site: {
        if (candidate.raw0 > ledger.remaining_primary ||
            candidate.raw1 > ledger.remaining_secondary) {
            return false;
        }
        for (u64 key : ledger.reserved_site_keys) {
            if (key == candidate.key) {
                return false;
            }
        }
        const AiEntity2TileRect rect = AiEntity2FootprintRectOf(candidate);
        for (const AiEntity2TileRect& reserved : ledger.reserved_sites) {
            if (AiEntity2RectsOverlap(rect, reserved)) {
                return false;
            }
        }
        return true;
    }
    case AiEntity2CandidateKind::produce_unit:
        return candidate.raw0 <= ledger.remaining_primary &&
            candidate.raw1 <= ledger.remaining_secondary &&
            candidate.raw2 <= ledger.remaining_population;
    case AiEntity2CandidateKind::research_upgrade: {
        if (candidate.raw0 > ledger.remaining_primary ||
            candidate.raw1 > ledger.remaining_secondary) {
            return false;
        }
        for (u32 order : ledger.reserved_research) {
            if (order == candidate.object_id) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

AiEntity2RejectCode AiEntity2LedgerConflictOf(const AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, u32 index) {
    if (index >= snapshot.candidates.size()) {
        return AiEntity2RejectCode::out_of_range;
    }
    const AiEntity2Candidate& candidate = snapshot.candidates[index];
    if (candidate.kind == static_cast<u8>(AiEntity2CandidateKind::resource)) {
        return AiEntity2RejectCode::none;
    }
    // Structural conflicts (same site / duplicate research) are reported
    // before budget conflicts: they identify the earlier canonical winner.
    if (candidate.kind == static_cast<u8>(AiEntity2CandidateKind::build_site)) {
        for (u64 key : ledger.reserved_site_keys) {
            if (key == candidate.key) {
                return AiEntity2RejectCode::site_conflict;
            }
        }
        const AiEntity2TileRect rect = AiEntity2FootprintRectOf(candidate);
        for (const AiEntity2TileRect& reserved : ledger.reserved_sites) {
            if (AiEntity2RectsOverlap(rect, reserved)) {
                return AiEntity2RejectCode::site_conflict;
            }
        }
    } else if (candidate.kind ==
        static_cast<u8>(AiEntity2CandidateKind::research_upgrade)) {
        for (u32 order : ledger.reserved_research) {
            if (order == candidate.object_id) {
                return AiEntity2RejectCode::research_conflict;
            }
        }
    }
    if (candidate.raw0 > ledger.remaining_primary ||
        candidate.raw1 > ledger.remaining_secondary) {
        return AiEntity2RejectCode::resource_conflict;
    }
    if (candidate.kind == static_cast<u8>(AiEntity2CandidateKind::produce_unit) &&
        candidate.raw2 > ledger.remaining_population) {
        return AiEntity2RejectCode::population_conflict;
    }
    return AiEntity2RejectCode::none;
}

void AiEntity2LedgerDynamicMasks(const AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, u32 row, u32* out_command_mask,
    std::vector<u32>& out_pair_words) {
    const u32 candidate_count = snapshot.candidate_rows();
    const u32 words = snapshot.economy_words_per_row();
    out_pair_words.assign(words, 0u);
    bool kinds[4] = {false, false, false, false};
    for (u32 c = 0; c < candidate_count; ++c) {
        if (!snapshot.economy_pair_bit(row, c) ||
            !AiEntity2LedgerCandidateAvailable(ledger, snapshot, c)) {
            continue;
        }
        out_pair_words[c >> 5] |= 1u << (c & 31u);
        const u8 kind = snapshot.candidates[c].kind;
        if (kind < 4) {
            kinds[kind] = true;
        }
    }
    // Policy-vocabulary mask: the non-economy bits (below HARVEST) pass
    // through; each economy bit needs a candidate of its kind still open.
    const u32 base = row < snapshot.own.size() ? snapshot.own[row].command_mask : 1u;
    u32 mask = base & ((1u << static_cast<u32>(AiEntity2PolicyCommand::harvest)) - 1u);
    for (u8 kind = 0; kind < 4; ++kind) {
        const u32 command = AiEntity2PolicyCommandOf(
            AiEntity2CommandOfKind(static_cast<AiEntity2CandidateKind>(kind)));
        if (((base >> command) & 1u) != 0 && kinds[kind]) {
            mask |= 1u << command;
        }
    }
    *out_command_mask = mask;
}

bool AiEntity2ChoiceLegal(const AiEntity2Snapshot& snapshot, u32 row,
    u32 dynamic_command_mask, const std::vector<u32>& dynamic_pair_words,
    AiEntity2Command command, i32 argument) {
    // The mask is in the policy vocabulary; an engine command without a
    // policy equivalent (stop) is never a legal policy choice.
    const u32 command_index = AiEntity2PolicyCommandOf(command);
    if (command_index >= kAiEntity2PolicyCommandCount ||
        ((dynamic_command_mask >> command_index) & 1u) == 0) {
        return false;
    }
    if (command == AiEntity2Command::keep_current_order ||
        command == AiEntity2Command::hold_position) {
        return argument == -1;
    }
    if (row >= snapshot.own.size()) {
        return false;
    }
    if (AiEntity2CommandIsPoint(command)) {
        if (argument < 0 || argument >= static_cast<i32>(kAiEntityPointTokenCount)) {
            return false;
        }
        const u32 token = static_cast<u32>(argument);
        return (snapshot.own[row].point_mask[token >> 5] >> (token & 31u) & 1u) != 0;
    }
    if (command == AiEntity2Command::attack_unit) {
        if (argument < 0 || argument >= static_cast<i32>(snapshot.targets.size())) {
            return false;
        }
        return snapshot.attack_pair_bit(row, static_cast<u32>(argument));
    }
    AiEntity2CandidateKind kind;
    if (!AiEntity2KindOfCommand(command, &kind)) {
        return false;
    }
    if (argument < 0 || argument >= static_cast<i32>(snapshot.candidates.size())) {
        return false;
    }
    const u32 c = static_cast<u32>(argument);
    if (snapshot.candidates[c].kind != static_cast<u8>(kind)) {
        return false;
    }
    if ((c >> 5) >= dynamic_pair_words.size()) {
        return false;
    }
    return (dynamic_pair_words[c >> 5] >> (c & 31u) & 1u) != 0;
}

void AiEntity2LedgerReserve(AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, AiEntity2Command command,
    i32 argument) {
    if (!AiEntity2CommandIsEconomy(command) || argument < 0 ||
        argument >= static_cast<i32>(snapshot.candidates.size())) {
        return;
    }
    const AiEntity2Candidate& candidate =
        snapshot.candidates[static_cast<std::size_t>(argument)];
    if (candidate.kind == static_cast<u8>(AiEntity2CandidateKind::resource)) {
        return;
    }
    ledger.remaining_primary = sat_sub(ledger.remaining_primary, candidate.raw0);
    ledger.remaining_secondary =
        sat_sub(ledger.remaining_secondary, candidate.raw1);
    switch (static_cast<AiEntity2CandidateKind>(candidate.kind)) {
    case AiEntity2CandidateKind::build_site:
        ledger.reserved_sites.push_back(AiEntity2FootprintRectOf(candidate));
        ledger.reserved_site_keys.push_back(candidate.key);
        break;
    case AiEntity2CandidateKind::produce_unit:
        ledger.remaining_population =
            sat_sub(ledger.remaining_population, candidate.raw2);
        break;
    case AiEntity2CandidateKind::research_upgrade:
        ledger.reserved_research.push_back(candidate.object_id);
        break;
    default:
        break;
    }
}

AiEntity2LedgerReplay AiEntity2ReplayLedger(const AiEntity2Snapshot& snapshot,
    const std::vector<u8>& command, const std::vector<i32>& argument,
    u32 unresolved_from, const std::vector<u8>* assign) {
    AiEntity2LedgerReplay replay;
    const u32 own_count = static_cast<u32>(snapshot.own.size());
    const u32 words = snapshot.economy_words_per_row();
    replay.dynamic_command_mask.resize(own_count);
    replay.remaining_budget.resize(own_count);
    replay.dynamic_economy_pair_mask.assign(
        static_cast<std::size_t>(own_count) * words, 0u);
    replay.choice_legal.assign(own_count, 0u);
    replay.dynamic_assign_mask.assign(own_count, 0u);
    replay.assign_legal.assign(own_count, 0u);
    AiEntity2Ledger ledger;
    AiEntity2LedgerInit(ledger, snapshot);
    AiEntity2AssignLedger assign_ledger;
    std::vector<u32> pair_words;
    for (u32 row = 0; row < own_count; ++row) {
        // Assign ledger (SCOUT capacity) runs in the same canonical order.
        const u32 assign_mask = AiEntity2DynamicAssignMask(assign_ledger,
            snapshot.own_appendix[row].assign_mask, snapshot.scout_free_at_snapshot);
        replay.dynamic_assign_mask[row] = assign_mask;
        if (assign != nullptr && row < assign->size() && (*assign)[row] != 0) {
            const u8 chosen = (*assign)[row];
            if (chosen <= kAiEntity2SlotCount &&
                ((assign_mask >> (chosen - 1u)) & 1u) != 0) {
                replay.assign_legal[row] = 1u;
                AiEntity2AssignLedgerApply(assign_ledger, chosen);
            }
        } else {
            replay.assign_legal[row] = 1u;   // keep is always legal
        }
        const bool unresolved = row >= unresolved_from;
        u32 mask = 0;
        if (unresolved) {
            mask = snapshot.own[row].command_mask &
                ((1u << static_cast<u32>(AiEntity2PolicyCommand::harvest)) - 1u);
            pair_words.assign(words, 0u);
        } else {
            AiEntity2LedgerDynamicMasks(ledger, snapshot, row, &mask, pair_words);
        }
        replay.dynamic_command_mask[row] = mask;
        replay.remaining_budget[row] = {ledger.remaining_primary,
            ledger.remaining_secondary, ledger.remaining_population};
        for (u32 w = 0; w < words; ++w) {
            replay.dynamic_economy_pair_mask[
                static_cast<std::size_t>(row) * words + w] = pair_words[w];
        }
        if (unresolved || row >= command.size() || row >= argument.size()) {
            continue;
        }
        const AiEntity2Command chosen = AiEntity2EngineCommandOf(command[row]);
        if (command[row] < kAiEntity2PolicyCommandCount &&
            AiEntity2ChoiceLegal(snapshot, row, mask, pair_words, chosen,
                argument[row])) {
            replay.choice_legal[row] = 1u;
            if (AiEntity2CommandIsEconomy(chosen)) {
                AiEntity2LedgerReserve(ledger, snapshot, chosen, argument[row]);
            }
        }
    }
    return replay;
}

bool AiEntity2RowStochastic(u32 dynamic_command_mask) {
    return (dynamic_command_mask & ~1u) != 0;
}

// ---------------------------------------------------------------------------
// Team-intent slot helpers
// ---------------------------------------------------------------------------

bool AiEntity2SlotCommandIsPoint(AiEntity2SlotCommand command) {
    return command == AiEntity2SlotCommand::move ||
        command == AiEntity2SlotCommand::attack_move ||
        command == AiEntity2SlotCommand::patrol;
}

u32 AiEntity2DynamicAssignMask(const AiEntity2AssignLedger& ledger,
    u32 base_assign_mask, u32 scout_free_at_snapshot) {
    u32 mask = base_assign_mask & ((1u << kAiEntity2SlotCount) - 1u);
    if (ledger.scout_taken >= scout_free_at_snapshot) {
        mask &= ~(1u << kAiEntity2SlotScout);
    }
    return mask;
}

void AiEntity2AssignLedgerApply(AiEntity2AssignLedger& ledger, u8 assign) {
    if (assign == kAiEntity2SlotScout + 1u) {
        ++ledger.scout_taken;
    }
}

bool AiEntity2SlotChoiceLegal(const AiEntity2Snapshot& snapshot, u32 slot,
    u8 command, i32 cell) {
    if (slot >= kAiEntity2SlotCount || command >= kAiEntity2SlotCommandCount) {
        return false;
    }
    if (((snapshot.slot_command_mask[slot] >> command) & 1u) == 0) {
        return false;
    }
    const AiEntity2SlotCommand typed = static_cast<AiEntity2SlotCommand>(command);
    if (AiEntity2SlotCommandIsPoint(typed)) {
        if (cell < 0 || cell >= static_cast<i32>(kAiEntityPointGlobalTokenCount)) {
            return false;
        }
        const u32 c = static_cast<u32>(cell);
        return (snapshot.slot_cell_mask[slot][c >> 5] >> (c & 31u) & 1u) != 0;
    }
    return cell == -1;
}

bool AiEntity2SlotMemberNeedsOrder(const AiEntity2SlotOrder& order,
    const AiEntity2SlotMemberView& member) {
    if (!order.active || member.has_personal_issue) {
        return false;
    }
    if (AiEntity2SlotCommandIsPoint(order.command)) {
        if (member.arrived) {
            return false;   // standing inside the slot cell: nothing to re-derive
        }
        if (order.command == AiEntity2SlotCommand::patrol &&
            member.latch_matches_slot) {
            return false;   // a patrol loops on its own
        }
        if (member.slot_changed || member.just_assigned) {
            return true;
        }
        // Auto re-guidance (EASY §4 rule (i)): a member whose derived order
        // ended (or who never got one) is re-sent while the slot marches.
        return !member.latch_matches_slot &&
            (member.latch_terminal || !member.has_latch);
    }
    if (order.command == AiEntity2SlotCommand::hold) {
        return (member.slot_changed || member.just_assigned) &&
            !member.latch_matches_slot;
    }
    if (order.command == AiEntity2SlotCommand::hunt_neutral) {
        // Persistent hunt: a member whose derived attack ended (kill / lost
        // target) or who never got one is re-targeted while the slot hunts;
        // the caller picks the monster (nearest visible neutral it may hit).
        return !member.latch_matches_slot &&
            (member.slot_changed || member.just_assigned ||
                member.latch_terminal || !member.has_latch);
    }
    return false;
}

// ---------------------------------------------------------------------------
// RAI3 wire (plan section 12)
// ---------------------------------------------------------------------------

namespace {

struct Writer2 {
    std::vector<u8>* out;

    void u8v(u8 value) { out->push_back(value); }
    void u16v(u16 value) {
        out->push_back(static_cast<u8>(value & 0xff));
        out->push_back(static_cast<u8>((value >> 8) & 0xff));
    }
    void u32v(u32 value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out->push_back(static_cast<u8>((value >> shift) & 0xff));
        }
    }
    void u64v(u64 value) {
        for (int shift = 0; shift < 64; shift += 8) {
            out->push_back(static_cast<u8>((value >> shift) & 0xff));
        }
    }
    void i32v(i32 value) { u32v(static_cast<u32>(value)); }
    void f32v(float value) {
        u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32v(bits);
    }
};

struct Reader2 {
    const u8* data;
    std::size_t length;
    std::size_t offset = 0;

    bool remaining(std::size_t bytes) const { return length - offset >= bytes; }
    u8 u8v() { return data[offset++]; }
    u16 u16v() {
        const u16 value = static_cast<u16>(data[offset]) |
            (static_cast<u16>(data[offset + 1]) << 8);
        offset += 2;
        return value;
    }
    u32 u32v() {
        u32 value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<u32>(data[offset++]) << shift;
        }
        return value;
    }
    u64 u64v() {
        u64 value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<u64>(data[offset++]) << shift;
        }
        return value;
    }
    i32 i32v() { return static_cast<i32>(u32v()); }
    float f32v() {
        const u32 bits = u32v();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
};

bool wire_fail(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

constexpr std::array<u16, 8> kEntity2WireVersions = {
    kAiEntityObservationSchemaVersion,
    kAiEntityGlobalFeatureVersion,
    kAiEntity2FeatureVersion,
    kAiEntity2ActionVersion,
    kAiEntity2SemanticVocabularyVersion,
    kAiEntityPointGeometryVersion,
    kAiEntity2CandidateVersion,
    kAiEntity2OutcomeVersion,
};

bool finite_float(float value) {
    return std::isfinite(value);
}

}  // namespace

void AiEntity2WriteWireHeader(const AiEntity2WireHeader& header,
    u8 (&out)[kAiEntity2WireHeaderBytes]) {
    std::memset(out, 0, kAiEntity2WireHeaderBytes);
    std::vector<u8> bytes;
    bytes.reserve(kAiEntity2WireHeaderBytes);
    Writer2 w{&bytes};
    for (char c : kAiEntity2WireMagic) {
        w.u8v(static_cast<u8>(c));
    }
    w.u16v(kAiEntity2WireHeaderBytes);
    w.u16v(kAiEntity2ProtocolVersion);
    w.u16v(header.kind);
    w.u16v(header.flags);
    w.u32v(header.payload_bytes);
    for (char c : kAiEntity2ContractId) {
        w.u8v(static_cast<u8>(c));
    }
    for (u16 version : kEntity2WireVersions) {
        w.u16v(version);
    }
    w.u32v(header.owner);
    w.u32v(header.episode);
    w.u32v(header.frame);
    w.u32v(header.sequence);
    w.u32v(header.reply_to_sequence);
    w.u32v(header.own_rows);
    w.u32v(header.target_rows);
    w.u32v(header.resource_rows);
    w.u32v(header.build_rows);
    w.u32v(header.produce_rows);
    w.u32v(header.research_rows);
    w.u32v(kAiEntityGlobalFeatureCount);
    w.u32v(kAiEntity2PolicyCommandCount);
    w.u32v(kAiEntityPointTokenCount);
    w.u32v(header.payload_crc32);
    w.u32v(header.policy_version);
    for (int i = 0; i < 6; ++i) {
        w.u32v(0);
    }
    std::memcpy(out, bytes.data(), kAiEntity2WireHeaderBytes);
}

bool AiEntity2ParseWireHeader(const u8* data, std::size_t length,
    AiEntity2WireHeader& out, std::string* error) {
    if (data == nullptr || length < kAiEntity2WireHeaderBytes) {
        return wire_fail(error, "short header");
    }
    Reader2 r{data, length};
    for (char c : kAiEntity2WireMagic) {
        if (r.u8v() != static_cast<u8>(c)) {
            return wire_fail(error, "bad magic");
        }
    }
    if (r.u16v() != kAiEntity2WireHeaderBytes) {
        return wire_fail(error, "bad header size");
    }
    if (r.u16v() != kAiEntity2ProtocolVersion) {
        return wire_fail(error, "bad protocol version");
    }
    out.kind = r.u16v();
    if (out.kind < static_cast<u16>(AiEntityWireKind::hello) ||
        out.kind > static_cast<u16>(AiEntityWireKind::error)) {
        return wire_fail(error, "unknown frame kind");
    }
    out.flags = r.u16v();
    if ((out.flags & ~(kAiEntity2WireFlagTerminated |
            kAiEntity2WireFlagTruncated)) != 0) {
        return wire_fail(error, "undefined flags set");
    }
    if ((out.flags & kAiEntity2WireFlagTerminated) != 0 &&
        (out.flags & kAiEntity2WireFlagTruncated) != 0) {
        return wire_fail(error, "terminated and truncated both set");
    }
    out.payload_bytes = r.u32v();
    if (out.payload_bytes > kAiEntityWireMaxPayloadBytes) {
        return wire_fail(error, "payload above limit");
    }
    for (char c : kAiEntity2ContractId) {
        if (r.u8v() != static_cast<u8>(c)) {
            return wire_fail(error, "bad contract id");
        }
    }
    for (u16 version : kEntity2WireVersions) {
        if (r.u16v() != version) {
            return wire_fail(error, "contract version mismatch");
        }
    }
    out.owner = r.u32v();
    out.episode = r.u32v();
    out.frame = r.u32v();
    out.sequence = r.u32v();
    out.reply_to_sequence = r.u32v();
    out.own_rows = r.u32v();
    out.target_rows = r.u32v();
    out.resource_rows = r.u32v();
    out.build_rows = r.u32v();
    out.produce_rows = r.u32v();
    out.research_rows = r.u32v();
    if (r.u32v() != kAiEntityGlobalFeatureCount ||
        r.u32v() != kAiEntity2PolicyCommandCount ||
        r.u32v() != kAiEntityPointTokenCount) {
        return wire_fail(error, "fixed count mismatch");
    }
    if (out.own_rows > kAiEntityWireRowLimit ||
        out.target_rows > kAiEntityWireRowLimit ||
        out.resource_rows > kAiEntity2CandidateSegmentLimit ||
        out.build_rows > kAiEntity2CandidateSegmentLimit ||
        out.produce_rows > kAiEntity2CandidateSegmentLimit ||
        out.research_rows > kAiEntity2CandidateSegmentLimit) {
        return wire_fail(error, "row count above wire hard limit");
    }
    out.payload_crc32 = r.u32v();
    out.policy_version = r.u32v();
    for (int i = 0; i < 6; ++i) {
        if (r.u32v() != 0) {
            return wire_fail(error, "reserved field nonzero");
        }
    }
    return true;
}

u64 AiEntity2ActRequestPayloadBytes(u32 own_rows, u32 target_rows,
    u32 candidate_rows, bool terminal) {
    if (own_rows > kAiEntityWireRowLimit || target_rows > kAiEntityWireRowLimit ||
        candidate_rows > kAiEntity2CandidateLimit) {
        return 0;
    }
    const u64 u = own_rows;
    const u64 e = target_rows;
    const u64 c = candidate_rows;
    const u64 pair_words = (e + 31u) / 32u;
    const u64 econ_words = (c + 31u) / 32u;
    u64 bytes = kAiEntity2FixedPrefixBytes +
        (kAiEntity2OwnPrefixBytes + kAiEntity2OwnAppendixBytes +
            kAiEntity2QueueSlotCount * kAiEntity2QueueSlotBytes) * u +
        kAiEntity2TargetRowBytes * e + kAiEntity2CandidateRowBytes * c +
        4u * u * (pair_words + econ_words);
    if (terminal) {
        bytes += 4u;
    }
    return bytes;
}

namespace {

void encode_request_body(Writer2& w, const AiEntity2ActRequestBody& body) {
    const AiEntity2Snapshot& s = body.snapshot;
    for (float value : body.global) {
        w.f32v(value);
    }
    w.u32v(s.spendable_primary);
    w.u32v(s.spendable_secondary);
    w.u32v(s.spendable_population);
    w.u32v(0);
    for (u64 value : body.cumulative_losses) {
        w.u64v(value);
    }
    for (u64 value : s.economy_reward_material) {
        w.u64v(value);
    }
    // ---- intent prefix (feature v3) ----
    for (const AiEntity2SlotBlock& block : s.slots) {
        w.u32v(block.member_count);
        w.i32v(block.centroid_x);
        w.i32v(block.centroid_y);
        w.u8v(block.command);
        w.u8v(block.active);
        w.u16v(block.reserved);
        w.i32v(block.cell);
        w.u32v(block.age_frames);
        w.u32v(block.pursuing);
        w.u32v(block.terminal);
        w.u32v(block.differing);
    }
    for (const AiEntity2StartCandidate& candidate : s.start_candidates) {
        w.i32v(candidate.cell);
        w.u8v(candidate.explored);
        w.u8v(candidate.is_own);
        w.u16v(0);
    }
    for (u32 mask : s.slot_command_mask) {
        w.u32v(mask);
    }
    for (const std::array<u32, 2>& words : s.slot_cell_mask) {
        w.u32v(words[0]);
        w.u32v(words[1]);
    }
    for (u64 value : s.intent_reward_material) {
        w.u64v(value);
    }
    const std::size_t u = s.own.size();
    const std::size_t e = s.targets.size();
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].key.runtime_id);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].key.activation_generation);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].control_epoch);
    for (std::size_t i = 0; i < u; ++i) w.u16v(s.own[i].type_id);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].movement_class);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].distance_check_mode);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own[i].role);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].render_class);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].command_base_state);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].command_state_high_flags);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].unit_command_flags);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].movement_state);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own[i].semantic_order);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own[i].order_status);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own[i].presence_bits);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own[i].engine_order_match);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own[i].last_attempt_command);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own[i].last_attempt_result);
    for (std::size_t i = 0; i < u; ++i) w.u16v(s.own[i].last_reject_code);
    for (std::size_t i = 0; i < u; ++i) w.i32v(s.own[i].active_target_row);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].attackable_class_mask);
    for (std::size_t i = 0; i < u; ++i) {
        for (float value : s.own[i].feature) {
            w.f32v(value);
        }
    }
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own[i].command_mask);
    for (std::size_t i = 0; i < u; ++i) {
        for (u32 word : s.own[i].point_mask) {
            w.u32v(word);
        }
    }
    // entity-feature-v2 appendix (SoA)
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own_appendix[i].capability_bits);
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(s.own_appendix[i].queued_production_type_id);
    }
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own_appendix[i].production_variant);
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(s.own_appendix[i].deferred_command_count);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(s.own_appendix[i].walking_build_type_id);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.i32v(s.own_appendix[i].active_economy_candidate_row);
    }
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own_appendix[i].source_state_bits);
    for (std::size_t i = 0; i < u; ++i) w.f32v(s.own_appendix[i].cargo_ratio);
    for (std::size_t i = 0; i < u; ++i) w.f32v(s.own_appendix[i].queue_fill_ratio);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own_appendix[i].slot_id);
    for (std::size_t i = 0; i < u; ++i) w.u8v(s.own_appendix[i].slot_order_relation);
    for (std::size_t i = 0; i < u; ++i) w.u32v(s.own_appendix[i].assign_mask);
    // effective queue slots (AoS)
    for (std::size_t i = 0; i < u; ++i) {
        for (const AiEntity2QueueSlot& slot : s.own_appendix[i].queue) {
            w.u8v(slot.kind);
            w.u8v(slot.status);
            w.u16v(slot.origin_channel);
            w.u32v(slot.object_id);
            w.u32v(slot.origin_sequence);
            w.u32v(slot.queue_ordinal);
        }
    }
    // target SoA
    for (std::size_t i = 0; i < e; ++i) w.u32v(s.targets[i].key.runtime_id);
    for (std::size_t i = 0; i < e; ++i) {
        w.u32v(s.targets[i].key.activation_generation);
    }
    for (std::size_t i = 0; i < e; ++i) w.u16v(s.targets[i].type_id);
    for (std::size_t i = 0; i < e; ++i) w.u8v(s.targets[i].owner_id);
    for (std::size_t i = 0; i < e; ++i) w.u8v(s.targets[i].role);
    for (std::size_t i = 0; i < e; ++i) w.u32v(s.targets[i].render_class);
    for (std::size_t i = 0; i < e; ++i) w.u32v(s.targets[i].kind_bits);
    for (std::size_t i = 0; i < e; ++i) {
        for (float value : s.targets[i].feature) {
            w.f32v(value);
        }
    }
    // candidate AoS
    for (const AiEntity2Candidate& candidate : s.candidates) {
        w.u64v(candidate.key);
        w.u8v(candidate.kind);
        w.u8v(candidate.flags);
        w.u16v(candidate.object_id);
        w.i32v(candidate.x);
        w.i32v(candidate.y);
        w.u32v(candidate.raw0);
        w.u32v(candidate.raw1);
        w.u32v(candidate.raw2);
        for (float value : candidate.feature) {
            w.f32v(value);
        }
    }
    for (u32 word : s.attack_pair_mask) {
        w.u32v(word);
    }
    for (u32 word : s.economy_pair_mask) {
        w.u32v(word);
    }
}

}  // namespace

std::vector<u8> EncodeAiEntity2ActRequestPayload(
    const AiEntity2ActRequestBody& body) {
    std::vector<u8> bytes;
    const AiEntity2Snapshot& s = body.snapshot;
    const u64 expected = AiEntity2ActRequestPayloadBytes(
        static_cast<u32>(s.own.size()), static_cast<u32>(s.targets.size()),
        s.candidate_rows(), false);
    if (expected == 0 || s.own_appendix.size() != s.own.size() ||
        s.candidates.size() != s.candidate_rows()) {
        return bytes;
    }
    bytes.reserve(static_cast<std::size_t>(expected));
    Writer2 w{&bytes};
    encode_request_body(w, body);
    if (bytes.size() != expected) {
        bytes.clear();
    }
    return bytes;
}

std::vector<u8> EncodeAiEntity2TerminalPayload(
    const AiEntity2ActRequestBody& body, u32 terminal_outcome) {
    std::vector<u8> bytes;
    const AiEntity2Snapshot& s = body.snapshot;
    const u64 expected = AiEntity2ActRequestPayloadBytes(
        static_cast<u32>(s.own.size()), static_cast<u32>(s.targets.size()),
        s.candidate_rows(), true);
    if (expected == 0 || s.own_appendix.size() != s.own.size() ||
        s.candidates.size() != s.candidate_rows()) {
        return bytes;
    }
    bytes.reserve(static_cast<std::size_t>(expected));
    Writer2 w{&bytes};
    w.u32v(terminal_outcome);
    encode_request_body(w, body);
    if (bytes.size() != expected) {
        bytes.clear();
    }
    return bytes;
}

bool DecodeAiEntity2ActRequestPayload(const u8* data, std::size_t length,
    const AiEntity2WireHeader& header, bool terminal,
    AiEntity2ActRequestBody& out, u32* out_terminal_outcome,
    std::string* error) {
    const u32 u = header.own_rows;
    const u32 e = header.target_rows;
    const u32 c = header.candidate_rows();
    const u64 expected = AiEntity2ActRequestPayloadBytes(u, e, c, terminal);
    if (data == nullptr || expected == 0 || length != expected) {
        return wire_fail(error, "ACT_REQ payload size mismatch");
    }
    Reader2 r{data, length};
    AiEntity2Snapshot& s = out.snapshot;
    s = AiEntity2Snapshot{};
    s.owner = header.owner;
    s.frame = header.frame;
    if (terminal) {
        const u32 outcome = r.u32v();
        if (outcome > 3) {
            return wire_fail(error, "terminal outcome out of range");
        }
        if (out_terminal_outcome != nullptr) {
            *out_terminal_outcome = outcome;
        }
    }
    for (float& value : out.global) {
        value = r.f32v();
        if (!finite_float(value)) {
            return wire_fail(error, "non-finite global feature");
        }
    }
    s.spendable_primary = r.u32v();
    s.spendable_secondary = r.u32v();
    s.spendable_population = r.u32v();
    if (r.u32v() != 0) {
        return wire_fail(error, "budget reserved word nonzero");
    }
    for (u64& value : out.cumulative_losses) {
        value = r.u64v();
    }
    for (u64& value : s.economy_reward_material) {
        value = r.u64v();
    }
    for (AiEntity2SlotBlock& block : s.slots) {
        block.member_count = r.u32v();
        block.centroid_x = r.i32v();
        block.centroid_y = r.i32v();
        block.command = r.u8v();
        block.active = r.u8v();
        block.reserved = r.u16v();
        block.cell = r.i32v();
        block.age_frames = r.u32v();
        block.pursuing = r.u32v();
        block.terminal = r.u32v();
        block.differing = r.u32v();
        if (block.command >= kAiEntity2SlotCommandCount || block.active > 1 ||
            block.reserved != 0 || block.cell < -1 ||
            block.cell >= static_cast<i32>(kAiEntityPointGlobalTokenCount)) {
            return wire_fail(error, "slot block out of range");
        }
    }
    for (AiEntity2StartCandidate& candidate : s.start_candidates) {
        candidate.cell = r.i32v();
        candidate.explored = r.u8v();
        candidate.is_own = r.u8v();
        if (r.u16v() != 0 || candidate.cell < -1 ||
            candidate.cell >= static_cast<i32>(kAiEntityPointGlobalTokenCount) ||
            candidate.explored > 1 || candidate.is_own > 1) {
            return wire_fail(error, "start candidate out of range");
        }
    }
    for (u32& mask : s.slot_command_mask) {
        mask = r.u32v();
        if ((mask & ~((1u << kAiEntity2SlotCommandCount) - 1u)) != 0 ||
            (mask & 1u) == 0) {
            return wire_fail(error, "slot command mask out of range");
        }
    }
    for (std::array<u32, 2>& words : s.slot_cell_mask) {
        words[0] = r.u32v();
        words[1] = r.u32v();
    }
    for (u64& value : s.intent_reward_material) {
        value = r.u64v();
    }
    s.own.resize(u);
    s.own_appendix.resize(u);
    s.targets.resize(e);
    s.candidates.resize(c);
    s.resource_rows = header.resource_rows;
    s.build_rows = header.build_rows;
    s.produce_rows = header.produce_rows;
    s.research_rows = header.research_rows;
    for (u32 i = 0; i < u; ++i) s.own[i].key.runtime_id = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].key.activation_generation = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].control_epoch = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].type_id = r.u16v();
    for (u32 i = 0; i < u; ++i) s.own[i].movement_class = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].distance_check_mode = r.u32v();
    for (u32 i = 0; i < u; ++i) {
        s.own[i].role = r.u8v();
        if (s.own[i].role > static_cast<u8>(AiEntity2Role::other)) {
            return wire_fail(error, "own role out of vocabulary");
        }
    }
    for (u32 i = 0; i < u; ++i) s.own[i].render_class = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].command_base_state = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].command_state_high_flags = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].unit_command_flags = r.u32v();
    for (u32 i = 0; i < u; ++i) s.own[i].movement_state = r.u32v();
    for (u32 i = 0; i < u; ++i) {
        s.own[i].semantic_order = r.u8v();
        if (s.own[i].semantic_order >
            static_cast<u8>(AiEntity2WireSemanticOrder::research_upgrade)) {
            return wire_fail(error, "semantic order out of vocabulary");
        }
    }
    for (u32 i = 0; i < u; ++i) s.own[i].order_status = r.u8v();
    for (u32 i = 0; i < u; ++i) s.own[i].presence_bits = r.u8v();
    for (u32 i = 0; i < u; ++i) s.own[i].engine_order_match = r.u8v();
    for (u32 i = 0; i < u; ++i) s.own[i].last_attempt_command = r.u8v();
    for (u32 i = 0; i < u; ++i) s.own[i].last_attempt_result = r.u8v();
    for (u32 i = 0; i < u; ++i) s.own[i].last_reject_code = r.u16v();
    for (u32 i = 0; i < u; ++i) s.own[i].active_target_row = r.i32v();
    for (u32 i = 0; i < u; ++i) s.own[i].attackable_class_mask = r.u32v();
    for (u32 i = 0; i < u; ++i) {
        for (float& value : s.own[i].feature) {
            value = r.f32v();
            if (!finite_float(value)) {
                return wire_fail(error, "non-finite own feature");
            }
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own[i].command_mask = r.u32v();
        if ((s.own[i].command_mask & ~kAiEntity2CommandMaskBits) != 0 ||
            (s.own[i].command_mask & 1u) == 0) {
            return wire_fail(error, "command mask high bits / KEEP bit");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        for (u32& word : s.own[i].point_mask) {
            word = r.u32v();
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].capability_bits = r.u32v();
        if ((s.own_appendix[i].capability_bits & ~0xffu) != 0) {
            return wire_fail(error, "capability high bits nonzero");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].queued_production_type_id = r.u32v();
    }
    for (u32 i = 0; i < u; ++i) s.own_appendix[i].production_variant = r.u32v();
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].deferred_command_count = r.u32v();
        if (s.own_appendix[i].deferred_command_count > kAiEntity2ProductionQueueLimit) {
            return wire_fail(error, "deferred command count above 4");
        }
    }
    for (u32 i = 0; i < u; ++i) s.own_appendix[i].walking_build_type_id = r.u32v();
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].active_economy_candidate_row = r.i32v();
        const i32 row = s.own_appendix[i].active_economy_candidate_row;
        if (row < -1 || row >= static_cast<i32>(c)) {
            return wire_fail(error, "active economy candidate row out of range");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].source_state_bits = r.u32v();
        if ((s.own_appendix[i].source_state_bits & ~0x3fu) != 0) {
            return wire_fail(error, "source state high bits nonzero");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].cargo_ratio = r.f32v();
        if (!finite_float(s.own_appendix[i].cargo_ratio)) {
            return wire_fail(error, "non-finite cargo ratio");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].queue_fill_ratio = r.f32v();
        if (!finite_float(s.own_appendix[i].queue_fill_ratio)) {
            return wire_fail(error, "non-finite queue ratio");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].slot_id = r.u8v();
        if (s.own_appendix[i].slot_id >= kAiEntity2SlotCount &&
            s.own_appendix[i].slot_id != kAiEntity2SlotNone) {
            return wire_fail(error, "slot id out of range");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].slot_order_relation = r.u8v();
        if (s.own_appendix[i].slot_order_relation > kAiEntity2SlotRelationJustAssigned) {
            return wire_fail(error, "slot relation out of range");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        s.own_appendix[i].assign_mask = r.u32v();
        if ((s.own_appendix[i].assign_mask & ~((1u << kAiEntity2SlotCount) - 1u)) != 0) {
            return wire_fail(error, "assign mask high bits nonzero");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        for (AiEntity2QueueSlot& slot : s.own_appendix[i].queue) {
            slot.kind = r.u8v();
            slot.status = r.u8v();
            slot.origin_channel = r.u16v();
            slot.object_id = r.u32v();
            slot.origin_sequence = r.u32v();
            slot.queue_ordinal = r.u32v();
            if (slot.kind > kAiEntity2QueueKindResearch ||
                slot.status > kAiEntity2QueueStatusAwaitingApply ||
                (slot.kind == kAiEntity2QueueKindEmpty) !=
                    (slot.status == kAiEntity2QueueStatusEmpty) ||
                slot.queue_ordinal > kAiEntity2ProductionQueueLimit) {
                return wire_fail(error, "queue slot enum out of range");
            }
            if (slot.kind == kAiEntity2QueueKindEmpty &&
                (slot.origin_channel != 0 || slot.object_id != 0 ||
                    slot.origin_sequence != 0 || slot.queue_ordinal != 0)) {
                return wire_fail(error, "empty queue slot carries values");
            }
        }
    }
    for (u32 i = 0; i < e; ++i) s.targets[i].key.runtime_id = r.u32v();
    for (u32 i = 0; i < e; ++i) s.targets[i].key.activation_generation = r.u32v();
    for (u32 i = 0; i < e; ++i) s.targets[i].type_id = r.u16v();
    for (u32 i = 0; i < e; ++i) s.targets[i].owner_id = r.u8v();
    for (u32 i = 0; i < e; ++i) s.targets[i].role = r.u8v();
    for (u32 i = 0; i < e; ++i) s.targets[i].render_class = r.u32v();
    for (u32 i = 0; i < e; ++i) s.targets[i].kind_bits = r.u32v();
    for (u32 i = 0; i < e; ++i) {
        for (float& value : s.targets[i].feature) {
            value = r.f32v();
            if (!finite_float(value)) {
                return wire_fail(error, "non-finite target feature");
            }
        }
    }
    const u32 segment_counts[4] = {header.resource_rows, header.build_rows,
        header.produce_rows, header.research_rows};
    u32 index = 0;
    for (u8 kind = 0; kind < 4; ++kind) {
        bool have_previous = false;
        u64 previous_key = 0;
        for (u32 k = 0; k < segment_counts[kind]; ++k, ++index) {
            AiEntity2Candidate& candidate = s.candidates[index];
            candidate.key = r.u64v();
            candidate.kind = r.u8v();
            candidate.flags = r.u8v();
            candidate.object_id = r.u16v();
            candidate.x = r.i32v();
            candidate.y = r.i32v();
            candidate.raw0 = r.u32v();
            candidate.raw1 = r.u32v();
            candidate.raw2 = r.u32v();
            for (float& value : candidate.feature) {
                value = r.f32v();
                if (!finite_float(value)) {
                    return wire_fail(error, "non-finite candidate feature");
                }
            }
            if (candidate.kind != kind) {
                return wire_fail(error, "candidate segment kind mismatch");
            }
            if ((candidate.flags & ~0x3fu) != 0) {
                return wire_fail(error, "candidate flag high bits nonzero");
            }
            if (kind == static_cast<u8>(AiEntity2CandidateKind::build_site) &&
                (candidate.raw2 >> 24) != 0) {
                return wire_fail(error, "BUILD packed word high bits nonzero");
            }
            if ((kind == static_cast<u8>(AiEntity2CandidateKind::produce_unit) ||
                    kind == static_cast<u8>(AiEntity2CandidateKind::research_upgrade)) &&
                (candidate.x != 0 || candidate.y != 0)) {
                return wire_fail(error, "non-site candidate carries coordinates");
            }
            if (have_previous && candidate.key <= previous_key) {
                return wire_fail(error, "candidate segment not in canonical order");
            }
            have_previous = true;
            previous_key = candidate.key;
        }
    }
    const u32 pair_words = (e + 31u) / 32u;
    const u32 econ_words = (c + 31u) / 32u;
    s.attack_pair_mask.resize(static_cast<std::size_t>(u) * pair_words);
    for (u32& word : s.attack_pair_mask) {
        word = r.u32v();
    }
    s.economy_pair_mask.resize(static_cast<std::size_t>(u) * econ_words);
    for (u32& word : s.economy_pair_mask) {
        word = r.u32v();
    }
    // High bits of the last mask word above E / C must be zero.
    if (pair_words != 0 && (e & 31u) != 0) {
        const u32 allowed = (1u << (e & 31u)) - 1u;
        for (u32 i = 0; i < u; ++i) {
            if ((s.attack_pair_mask[static_cast<std::size_t>(i) * pair_words +
                    pair_words - 1] & ~allowed) != 0) {
                return wire_fail(error, "attack pair mask high bits nonzero");
            }
        }
    }
    if (econ_words != 0 && (c & 31u) != 0) {
        const u32 allowed = (1u << (c & 31u)) - 1u;
        for (u32 i = 0; i < u; ++i) {
            if ((s.economy_pair_mask[static_cast<std::size_t>(i) * econ_words +
                    econ_words - 1] & ~allowed) != 0) {
                return wire_fail(error, "economy pair mask high bits nonzero");
            }
        }
    }
    if (r.offset != length) {
        return wire_fail(error, "payload has trailing bytes");
    }
    return true;
}

bool AiEntity2ArgumentDomainOk(AiEntity2Command command, i32 argument,
    u32 target_rows, u32 candidate_rows) {
    switch (command) {
    case AiEntity2Command::keep_current_order:
    case AiEntity2Command::hold_position:
    case AiEntity2Command::stop:
        return argument == -1;
    case AiEntity2Command::move:
    case AiEntity2Command::attack_move:
    case AiEntity2Command::patrol:
        return argument >= 0 &&
            argument < static_cast<i32>(kAiEntityPointTokenCount);
    case AiEntity2Command::attack_unit:
        return argument >= 0 && argument < static_cast<i32>(target_rows);
    case AiEntity2Command::harvest:
    case AiEntity2Command::build:
    case AiEntity2Command::produce_unit:
    case AiEntity2Command::research_upgrade:
        return argument >= 0 && argument < static_cast<i32>(candidate_rows);
    default:
        return false;
    }
}

std::vector<u8> EncodeAiEntity2ReplyPayload(const AiEntity2ReplyBody& body) {
    std::vector<u8> bytes;
    bytes.reserve(body.command.size() * 6 + 20);
    Writer2 w{&bytes};
    for (u8 command : body.command) {
        w.u8v(command);
    }
    for (i32 argument : body.argument) {
        w.i32v(argument);
    }
    for (std::size_t i = 0; i < body.command.size(); ++i) {
        w.u8v(i < body.assign.size() ? body.assign[i] : 0u);
    }
    for (u8 command : body.slot_command) {
        w.u8v(command);
    }
    for (i32 cell : body.slot_cell) {
        w.i32v(cell);
    }
    return bytes;
}

bool DecodeAiEntity2ReplyPayload(const u8* data, std::size_t length,
    const AiEntity2WireHeader& header, AiEntity2ReplyBody& out,
    std::string* error) {
    const u32 u = header.own_rows;
    const std::size_t expected = static_cast<std::size_t>(u) * 6u + 4u +
        kAiEntity2SlotCount * 4u;
    if ((data == nullptr && expected != 0) || length != expected) {
        return wire_fail(error, "reply payload size mismatch");
    }
    Reader2 r{data, length};
    out.command.resize(u);
    out.argument.assign(u, -1);
    out.assign.assign(u, 0);
    for (u32 i = 0; i < u; ++i) {
        out.command[i] = r.u8v();
        if (out.command[i] >= kAiEntity2PolicyCommandCount) {
            return wire_fail(error, "entity command out of range");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        out.argument[i] = r.i32v();
        if (!AiEntity2ArgumentDomainOk(
                AiEntity2EngineCommandOf(out.command[i]), out.argument[i],
                header.target_rows, header.candidate_rows())) {
            return wire_fail(error, "argument outside the command domain");
        }
    }
    for (u32 i = 0; i < u; ++i) {
        out.assign[i] = r.u8v();
        if (out.assign[i] > kAiEntity2SlotCount) {
            return wire_fail(error, "assign out of range");
        }
    }
    for (u8& command : out.slot_command) {
        command = r.u8v();
        if (command >= kAiEntity2SlotCommandCount) {
            return wire_fail(error, "slot command out of range");
        }
    }
    for (u32 s = 0; s < kAiEntity2SlotCount; ++s) {
        out.slot_cell[s] = r.i32v();
        const bool point = AiEntity2SlotCommandIsPoint(
            static_cast<AiEntity2SlotCommand>(out.slot_command[s]));
        if (point ? (out.slot_cell[s] < 0 ||
                out.slot_cell[s] >= static_cast<i32>(kAiEntityPointGlobalTokenCount)) :
            out.slot_cell[s] != -1) {
            return wire_fail(error, "slot cell outside the command domain");
        }
    }
    return true;
}

std::vector<u8> EncodeAiEntity2OutcomePayload(const AiEntity2OutcomeBody& body) {
    std::vector<u8> bytes;
    Writer2 w{&bytes};
    for (u16 result : body.result) {
        w.u16v(result);
    }
    for (u16 code : body.reject_code) {
        w.u16v(code);
    }
    for (u32 word : body.trainable_mask) {
        w.u32v(word);
    }
    for (u16 result : body.slot_result) {
        w.u16v(result);
    }
    for (u16 code : body.slot_reject_code) {
        w.u16v(code);
    }
    w.u32v(body.slot_trainable_bits);
    const std::size_t words = (body.result.size() + 31u) / 32u;
    for (std::size_t i = 0; i < words; ++i) {
        w.u32v(i < body.assign_trainable_mask.size() ? body.assign_trainable_mask[i] : 0u);
    }
    return bytes;
}

bool DecodeAiEntity2OutcomePayload(const u8* data, std::size_t length,
    u32 own_rows, AiEntity2OutcomeBody& out, std::string* error) {
    const std::size_t mask_words = (static_cast<std::size_t>(own_rows) + 31u) / 32u;
    const std::size_t expected = static_cast<std::size_t>(own_rows) * 4u +
        mask_words * 4u + kAiEntity2SlotCount * 4u + 4u + mask_words * 4u;
    if ((data == nullptr && expected != 0) || length != expected) {
        return wire_fail(error, "outcome payload size mismatch");
    }
    Reader2 r{data, length};
    out.result.resize(own_rows);
    out.reject_code.resize(own_rows);
    out.trainable_mask.resize(mask_words);
    for (u32 i = 0; i < own_rows; ++i) {
        out.result[i] = r.u16v();
        if (out.result[i] > static_cast<u16>(AiEntity2AttemptResult::controller_failed)) {
            return wire_fail(error, "outcome result out of range");
        }
    }
    for (u32 i = 0; i < own_rows; ++i) {
        out.reject_code[i] = r.u16v();
        if (out.reject_code[i] > static_cast<u16>(AiEntity2RejectCode::slot_command)) {
            return wire_fail(error, "outcome reject code out of range");
        }
        const bool success =
            out.result[i] <= static_cast<u16>(AiEntity2AttemptResult::published);
        if (success != (out.reject_code[i] == 0)) {
            return wire_fail(error, "outcome result/reject disagree");
        }
    }
    for (std::size_t i = 0; i < mask_words; ++i) {
        out.trainable_mask[i] = r.u32v();
    }
    for (u32 i = 0; i < own_rows; ++i) {
        const bool bit = (out.trainable_mask[i >> 5] >> (i & 31u) & 1u) != 0;
        if (bit && out.result[i] > static_cast<u16>(AiEntity2AttemptResult::published)) {
            return wire_fail(error, "trainable bit set on a failed row");
        }
    }
    if (mask_words != 0 && (own_rows & 31u) != 0 &&
        (out.trainable_mask[mask_words - 1] & ~((1u << (own_rows & 31u)) - 1u)) != 0) {
        return wire_fail(error, "trainable mask high bits nonzero");
    }
    for (u16& result : out.slot_result) {
        result = r.u16v();
        if (result > static_cast<u16>(AiEntity2AttemptResult::controller_failed)) {
            return wire_fail(error, "slot result out of range");
        }
    }
    for (u32 s = 0; s < kAiEntity2SlotCount; ++s) {
        out.slot_reject_code[s] = r.u16v();
        if (out.slot_reject_code[s] > static_cast<u16>(AiEntity2RejectCode::slot_command)) {
            return wire_fail(error, "slot reject code out of range");
        }
        const bool success =
            out.slot_result[s] <= static_cast<u16>(AiEntity2AttemptResult::published);
        if (success != (out.slot_reject_code[s] == 0)) {
            return wire_fail(error, "slot result/reject disagree");
        }
    }
    out.slot_trainable_bits = r.u32v();
    if ((out.slot_trainable_bits & ~((1u << kAiEntity2SlotCount) - 1u)) != 0) {
        return wire_fail(error, "slot trainable high bits nonzero");
    }
    out.assign_trainable_mask.resize(mask_words);
    for (std::size_t i = 0; i < mask_words; ++i) {
        out.assign_trainable_mask[i] = r.u32v();
    }
    if (mask_words != 0 && (own_rows & 31u) != 0 &&
        (out.assign_trainable_mask[mask_words - 1] &
            ~((1u << (own_rows & 31u)) - 1u)) != 0) {
        return wire_fail(error, "assign trainable mask high bits nonzero");
    }
    return true;
}

std::vector<u8> EncodeAiEntity2HelloPayload(const AiEntity2HelloBody& body) {
    std::vector<u8> bytes;
    bytes.reserve(16 + body.owners.size() * 48);
    Writer2 w{&bytes};
    w.u32v(body.max_payload_bytes);
    w.u32v(body.reply_timeout_ms);
    w.u32v(body.run_mode);
    w.u32v(body.controlled_owner_mask);
    for (const AiEntity2HelloOwnerRecord& record : body.owners) {
        w.u32v(record.owner);
        w.u32v(record.frozen_hostile_owner_mask);
        w.u32v(record.requested_policy_version);
        w.u32v(0);
        for (u8 byte : record.requested_checkpoint_sha256) {
            w.u8v(byte);
        }
    }
    return bytes;
}

bool DecodeAiEntity2HelloPayload(const u8* data, std::size_t length,
    AiEntity2HelloBody& out, std::string* error) {
    if (data == nullptr || length < 16 || (length - 16) % 48 != 0) {
        return wire_fail(error, "hello payload size mismatch");
    }
    Reader2 r{data, length};
    out.max_payload_bytes = r.u32v();
    out.reply_timeout_ms = r.u32v();
    out.run_mode = r.u32v();
    out.controlled_owner_mask = r.u32v();
    if (out.run_mode > 1) {
        return wire_fail(error, "hello run mode out of range");
    }
    const std::size_t record_count = (length - 16) / 48;
    u32 mask_owner_count = 0;
    for (u32 mask = out.controlled_owner_mask; mask != 0; mask &= mask - 1) {
        ++mask_owner_count;
    }
    if (record_count != mask_owner_count) {
        return wire_fail(error, "hello owner record count mismatch");
    }
    out.owners.clear();
    out.owners.reserve(record_count);
    i64 previous_owner = -1;
    for (std::size_t i = 0; i < record_count; ++i) {
        AiEntity2HelloOwnerRecord record;
        record.owner = r.u32v();
        record.frozen_hostile_owner_mask = r.u32v();
        record.requested_policy_version = r.u32v();
        if (r.u32v() != 0) {
            return wire_fail(error, "hello record reserved word nonzero");
        }
        for (u8& byte : record.requested_checkpoint_sha256) {
            byte = r.u8v();
        }
        if (static_cast<i64>(record.owner) <= previous_owner) {
            return wire_fail(error, "hello owner records not ascending");
        }
        if (record.owner >= 32 ||
            (out.controlled_owner_mask & (1u << record.owner)) == 0) {
            return wire_fail(error, "hello owner not in controlled mask");
        }
        previous_owner = record.owner;
        out.owners.push_back(record);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Economy order / event tracking (plan section 11)
// ---------------------------------------------------------------------------

bool AiEntity2TrackEconomyOrderFrame(AiEntity2EconomyOrder& order,
    const AiEntity2EconomyOrderFrameView& view, u32 frame) {
    if (!view.source_alive_active || !view.control_epoch_matches) {
        return false;
    }
    if (order.status == AiEntityOrderStatus::awaiting_apply) {
        if (view.acknowledged_matching) {
            order.status = AiEntityOrderStatus::active;
            order.applied_frame = frame;
            order.escape_frames = 0;
            order.idle_frames = 0;
            return true;
        }
        if (view.delivery_origin_seen &&
            order.delivery_seen_frame == 0xffffffffu) {
            order.delivery_seen_frame = frame;
        }
        const bool escape = view.origin_replaced ||
            (view.consumer_passed_sequence && !view.delivery_origin_seen);
        order.escape_frames = escape ? order.escape_frames + 1 : 0;
        if (order.escape_frames >= kAiEntityAwaitDeliveryFrames ||
            frame - order.issued_frame >= kAiEntityAwaitAbsoluteFrames) {
            order.status = AiEntityOrderStatus::interrupted;
            order.cost_claimed = false;
            order.site_claimed = false;
            order.escape_frames = 0;
        }
        return true;
    }
    if (order.status != AiEntityOrderStatus::active) {
        // Terminal states never auto-resume; only a new ISSUE does.
        return true;
    }
    if (frame <= order.applied_frame) {
        return true;
    }
    if (order.command == AiEntity2Command::harvest) {
        // The whole 0x28..0x2d family (including the automatic return /
        // deposit legs) is one ACTIVE order; carrying cargo keeps it active
        // even after the tile ran out.
        const bool in_family = harvest_family_state(view.command_base_state);
        if (in_family || view.carrying) {
            order.idle_frames = 0;
            return true;
        }
        order.idle_frames += 1;
        if (order.idle_frames < kAiEntity2EconomyIdleFrames) {
            return true;
        }
        order.status = view.resource_depleted ?
            AiEntityOrderStatus::completed : AiEntityOrderStatus::interrupted;
        order.idle_frames = 0;
        return true;
    }
    // BUILD: approach 0x23 / 0x25, spawned construction 0x24 (tracked on
    // the spawned structure), completion.
    if (view.spawned_present) {
        if (!(order.spawned_building == view.spawned_key)) {
            order.spawned_building = view.spawned_key;
        }
        // Bank debit and authoritative occupancy both exist once the
        // structure spawned: release the controller claims.
        order.cost_claimed = false;
        order.site_claimed = false;
        order.idle_frames = 0;
        if (view.spawned_completed) {
            order.status = AiEntityOrderStatus::completed;
        }
        return true;
    }
    if (order.spawned_building.runtime_id != 0) {
        // The spawned structure vanished before completion.
        order.status = AiEntityOrderStatus::interrupted;
        return true;
    }
    if (build_walk_state(view.command_base_state)) {
        order.idle_frames = 0;
        return true;
    }
    order.idle_frames += 1;
    if (order.idle_frames >= kAiEntity2EconomyIdleFrames) {
        order.status = AiEntityOrderStatus::interrupted;
        order.cost_claimed = false;
        order.site_claimed = false;
        order.idle_frames = 0;
    }
    return true;
}

bool AiEntity2TrackEventFrame(AiEntity2EconomyEvent& event,
    const AiEntity2EventFrameView& view, u32 frame) {
    if (!view.source_alive_active || !view.control_epoch_matches) {
        return false;
    }
    if (event.status == AiEntity2EventStatus::completed ||
        event.status == AiEntity2EventStatus::handler_rejected) {
        return true;
    }
    const bool origin_seen = view.origin_in_pending || view.origin_in_active ||
        view.origin_in_deferred;
    if (origin_seen) {
        event.origin_ever_seen = true;
        event.missing_frames = 0;
        if (view.origin_in_active || view.origin_in_deferred) {
            // Enqueued: the receiver debited the cost with the enqueue and
            // the queue slot is real now.
            event.status = AiEntity2EventStatus::engine_queued;
            event.resource_claimed = false;
            event.queue_claimed = false;
        }
        if (event.command == AiEntity2Command::produce_unit &&
            view.origin_in_active && view.population_reserved_by_engine) {
            event.population_claimed = false;
        }
        if (event.command == AiEntity2Command::research_upgrade &&
            (view.research_active_matching ||
                view.owner_research_level > event.level_at_issue)) {
            event.research_claimed = false;
        }
        return true;
    }
    if (event.command == AiEntity2Command::research_upgrade &&
        view.owner_research_level > event.level_at_issue) {
        event.status = AiEntity2EventStatus::completed;
        event.resource_claimed = false;
        event.queue_claimed = false;
        event.research_claimed = false;
        return true;
    }
    if (event.status == AiEntity2EventStatus::engine_queued) {
        // Left the queue after being seen: PRODUCE spawned, RESEARCH
        // reached completion on the owner side (checked above) or was
        // cancelled externally — either way the event is over.
        event.status = AiEntity2EventStatus::completed;
        event.population_claimed = false;
        event.research_claimed = false;
        return true;
    }
    // AWAITING_APPLY and not visible anywhere.
    const bool timed_out = frame - event.issued_frame >= kAiEntity2EventAbsoluteFrames;
    if (view.consumer_passed_sequence) {
        event.missing_frames += 1;
    }
    if (event.missing_frames >= kAiEntity2EventMissingFrames || timed_out) {
        event.status = AiEntity2EventStatus::handler_rejected;
        event.resource_claimed = false;
        event.population_claimed = false;
        event.queue_claimed = false;
        event.research_claimed = false;
    }
    return true;
}

AiEntity2DecisionRowOutcome AiEntity2EvaluateEconomyRow(
    const AiEntity2OrderStore& store, const AiEntityKey& source,
    const AiEntity2EconomyDecisionInput& input) {
    AiEntity2DecisionRowOutcome outcome;
    if (input.command == AiEntity2Command::keep_current_order) {
        outcome.result = AiEntity2AttemptResult::kept;
        return outcome;
    }
    if (input.command == AiEntity2Command::harvest ||
        input.command == AiEntity2Command::build) {
        const auto it = store.economy.find(AiEntityPackKey(source));
        if (it != store.economy.end()) {
            const AiEntity2EconomyOrder& order = it->second;
            const bool tracking =
                order.status == AiEntityOrderStatus::awaiting_apply ||
                order.status == AiEntityOrderStatus::active;
            if (tracking && order.command == input.command &&
                order.candidate_kind == input.candidate_kind &&
                order.candidate_key == input.candidate_key) {
                outcome.result = AiEntity2AttemptResult::deduped;
                return outcome;
            }
        }
    } else if (input.command == AiEntity2Command::research_upgrade) {
        for (const AiEntity2EconomyEvent& event : store.events) {
            if (event.source == source &&
                event.command == AiEntity2Command::research_upgrade &&
                event.object_id == input.object_id &&
                event.status == AiEntity2EventStatus::awaiting_apply) {
                outcome.result = AiEntity2AttemptResult::deduped;
                return outcome;
            }
        }
    }
    // PRODUCE_UNIT never dedupes (repeat enqueue is a legitimate choice).
    outcome.result = AiEntity2AttemptResult::published;
    outcome.needs_packet = true;
    return outcome;
}

// ---------------------------------------------------------------------------
// SHD2 shadow teacher labels (plan section 15.1)
// ---------------------------------------------------------------------------

namespace {

// Teacher order kind -> policy command (action v4); STOP has no policy
// equivalent and is excluded like any unmapped kind.
u8 shadow2_command_of(AiSemanticActionKind kind) {
    switch (kind) {
    case AiSemanticActionKind::move: return static_cast<u8>(AiEntity2PolicyCommand::move);
    case AiSemanticActionKind::attack_move:
        return static_cast<u8>(AiEntity2PolicyCommand::attack_move);
    case AiSemanticActionKind::patrol:
        return static_cast<u8>(AiEntity2PolicyCommand::patrol);
    case AiSemanticActionKind::attack_unit:
        return static_cast<u8>(AiEntity2PolicyCommand::attack_unit);
    case AiSemanticActionKind::hold_position:
        return static_cast<u8>(AiEntity2PolicyCommand::hold);
    case AiSemanticActionKind::harvest:
        return static_cast<u8>(AiEntity2PolicyCommand::harvest);
    case AiSemanticActionKind::build: return static_cast<u8>(AiEntity2PolicyCommand::build);
    case AiSemanticActionKind::produce_unit:
        return static_cast<u8>(AiEntity2PolicyCommand::produce_unit);
    case AiSemanticActionKind::research:
        return static_cast<u8>(AiEntity2PolicyCommand::research_upgrade);
    default: return 0xff;
    }
}

}  // namespace

namespace {

// Global 8x8 cell token of a pixel point (same axis split as the point
// grid); -1 when the point is off the map.
i32 shadow2_cell_of_point(const UnitMovementMap& map, i32 x, i32 y) {
    if (x < 0 || y < 0 || map.width == 0 || map.height == 0) {
        return -1;
    }
    const u32 tile_x = static_cast<u32>(x) >> 5;
    const u32 tile_y = static_cast<u32>(y) >> 5;
    if (tile_x >= map.width || tile_y >= map.height) {
        return -1;
    }
    auto axis = [](u32 tile, u32 extent) {
        u32 cell = 0;
        for (u32 c = 1; c < kAiEntityPointGridWidth; ++c) {
            if (static_cast<u32>((static_cast<u64>(c) * extent) /
                    kAiEntityPointGridWidth) <= tile) {
                cell = c;
            }
        }
        return cell;
    };
    return static_cast<i32>(axis(tile_y, map.height) * kAiEntityPointGridWidth +
        axis(tile_x, map.width));
}

u8 shadow2_unit_command_of_slot(AiEntity2SlotCommand command) {
    switch (command) {
    case AiEntity2SlotCommand::move: return static_cast<u8>(AiEntity2Command::move);
    case AiEntity2SlotCommand::attack_move:
        return static_cast<u8>(AiEntity2Command::attack_move);
    case AiEntity2SlotCommand::patrol: return static_cast<u8>(AiEntity2Command::patrol);
    case AiEntity2SlotCommand::hold: return static_cast<u8>(AiEntity2Command::hold_position);
    case AiEntity2SlotCommand::stop: return static_cast<u8>(AiEntity2Command::stop);
    default: return static_cast<u8>(AiEntity2Command::keep_current_order);
    }
}

}  // namespace

std::vector<AiEntity2ShadowLabel> BuildAiEntity2ShadowLabels(
    const AiEntity2Snapshot& snapshot, const UnitMovementMap* movement_map,
    const std::vector<AiEntity2ShadowDesiredOrder>& desired,
    AiEntity2ShadowState& state, AiEntity2LedgerReplay& out_replay,
    u32 max_point_error_px, const AiEntity2ShadowTeacherIntent* intent,
    std::array<AiEntity2ShadowSlotLabel, kAiEntity2SlotCount>* out_slot_labels) {
    const std::size_t own_count = snapshot.own.size();
    std::vector<AiEntity2ShadowLabel> labels(own_count);
    // ---- slot moves (assign) from the teacher's group membership, through
    // the same SCOUT-capacity ledger the policy samples with ----
    std::vector<u8> assign_vec(own_count, 0);
    {
        AiEntity2AssignLedger assign_ledger;
        for (std::size_t index = 0; index < own_count; ++index) {
            const AiEntity2OwnAppendix& appendix = snapshot.own_appendix[index];
            const u32 dynamic_mask = AiEntity2DynamicAssignMask(assign_ledger,
                appendix.assign_mask, snapshot.scout_free_at_snapshot);
            if (intent == nullptr || appendix.slot_id == kAiEntity2SlotNone) {
                continue;
            }
            const auto it = intent->desired_slot.find(snapshot.own[index].key.runtime_id);
            if (it == intent->desired_slot.end() || it->second >= kAiEntity2SlotCount ||
                it->second == appendix.slot_id) {
                continue;
            }
            if (((dynamic_mask >> it->second) & 1u) == 0) {
                labels[index].assign_label = kAiEntityShadowExcluded;
                continue;
            }
            labels[index].assign_label = kAiEntityShadowIssue;
            labels[index].assign = static_cast<u8>(it->second + 1u);
            assign_vec[index] = labels[index].assign;
            AiEntity2AssignLedgerApply(assign_ledger, labels[index].assign);
        }
    }
    // ---- commander (per-slot) labels ----
    if (out_slot_labels != nullptr) {
        for (u32 slot = 0; slot < kAiEntity2SlotCount; ++slot) {
            AiEntity2ShadowSlotLabel& label = (*out_slot_labels)[slot];
            label = AiEntity2ShadowSlotLabel{};
            const AiEntity2SlotBlock& current = snapshot.slots[slot];
            if (intent == nullptr) {
                continue;
            }
            const AiEntity2SlotOrder& want = intent->desired[slot];
            if (want.active) {
                const u8 command = static_cast<u8>(want.command);
                const i32 cell = AiEntity2SlotCommandIsPoint(want.command) ?
                    want.cell : -1;
                if (current.active != 0 && current.command == command &&
                    current.cell == cell) {
                    continue;   // KEEP
                }
                if (!AiEntity2SlotChoiceLegal(snapshot, slot, command, cell)) {
                    label.label = kAiEntityShadowExcluded;
                    continue;
                }
                label.label = kAiEntityShadowIssue;
                label.command = command;
                label.cell = cell;
            } else if (current.active != 0) {
                // The teacher's objective ended: v5 has no CLEAR, the order
                // ends through STOP (members stop, then follow new orders).
                const u8 stop = static_cast<u8>(AiEntity2SlotCommand::stop);
                if (AiEntity2SlotChoiceLegal(snapshot, slot, stop, -1)) {
                    label.label = kAiEntityShadowIssue;
                    label.command = stop;
                    label.cell = -1;
                } else {
                    label.label = kAiEntityShadowExcluded;
                }
            }
        }
    }
    // Teacher orders per unit; a second order for the same unit in one tick
    // is MULTIPLE_DESIRED.
    std::unordered_map<u32, const AiEntity2ShadowDesiredOrder*> desired_by_id;
    std::unordered_set<u32> multiple;
    for (const AiEntity2ShadowDesiredOrder& order : desired) {
        if (!desired_by_id.emplace(order.unit_id, &order).second) {
            multiple.insert(order.unit_id);
        }
    }
    std::unordered_map<u32, i32> target_row_by_id;
    for (std::size_t i = 0; i < snapshot.targets.size(); ++i) {
        target_row_by_id[snapshot.targets[i].key.runtime_id] = static_cast<i32>(i);
    }
    std::unordered_map<u32, AiEntityReachability> reachability;
    std::vector<u8> commands(own_count, 0);
    std::vector<i32> arguments(own_count, -1);
    // Rows the teacher wanted but that could not be mapped to a candidate /
    // legal choice while the teacher event still consumed budget or a site:
    // every economy row from there on is PREFIX_UNRESOLVED.
    u32 unresolved_from = kAiEntity2NoUnresolvedRow;

    auto exclude = [&](std::size_t index, AiEntity2ShadowExcludeReason reason) {
        labels[index].label = kAiEntityShadowExcluded;
        labels[index].command = kAiEntity2ShadowExcludedCommand;
        labels[index].exclude_reason = static_cast<u16>(reason);
        labels[index].argument = -1;
    };
    auto mark_unresolved = [&](std::size_t from) {
        if (from < unresolved_from) {
            unresolved_from = static_cast<u32>(from);
        }
    };

    // First pass: map teacher orders to (command, argument) per row and
    // KEEP/ISSUE against the shadow latch.
    for (std::size_t index = 0; index < own_count; ++index) {
        const AiEntityOwnRow& row = snapshot.own[index];
        AiEntity2ShadowLabel& label = labels[index];
        const u64 latch_key = AiEntityPackKey(row.key);
        const auto desired_it = desired_by_id.find(row.key.runtime_id);
        if (desired_it == desired_by_id.end()) {
            label.label = kAiEntityShadowKeep;
            continue;
        }
        if (multiple.count(row.key.runtime_id) != 0) {
            exclude(index, AiEntity2ShadowExcludeReason::multiple_desired);
            continue;
        }
        const AiEntity2ShadowDesiredOrder& order = *desired_it->second;
        if (order.kind == AiSemanticActionKind::return_cargo) {
            exclude(index, AiEntity2ShadowExcludeReason::return_cargo);
            continue;
        }
        const u8 command = shadow2_command_of(order.kind);
        if (command == 0xff) {
            exclude(index, AiEntity2ShadowExcludeReason::stale);
            continue;
        }
        AiEntity2ShadowLatch next_latch;
        next_latch.valid = true;
        next_latch.command = command;
        i32 argument = -1;
        const AiEntity2Command typed = AiEntity2EngineCommandOf(command);
        // A member's point order that its slot (after this tick's assign /
        // commander labels) already carries is the slot's business: live
        // derives it per member, so the personal label is KEEP.  Only a
        // personal order that DISAGREES with the slot order is labelled
        // (and then hits the disobedience mask like any policy choice).
        if (AiEntity2CommandIsPoint(typed) && movement_map != nullptr &&
            index < snapshot.own_appendix.size()) {
            u8 slot = snapshot.own_appendix[index].slot_id;
            if (label.assign_label == kAiEntityShadowIssue && label.assign != 0) {
                slot = static_cast<u8>(label.assign - 1u);
            }
            if (slot < kAiEntity2SlotCount) {
                AiEntity2SlotOrder effective;
                if (intent != nullptr && intent->desired[slot].active) {
                    effective = intent->desired[slot];
                } else if (snapshot.slots[slot].active != 0) {
                    effective.active = true;
                    effective.command = static_cast<AiEntity2SlotCommand>(
                        snapshot.slots[slot].command);
                    effective.cell = snapshot.slots[slot].cell;
                }
                if (effective.active && AiEntity2SlotCommandIsPoint(effective.command) &&
                    AiEntity2PolicyCommandOf(static_cast<AiEntity2Command>(
                        shadow2_unit_command_of_slot(effective.command))) == command &&
                    shadow2_cell_of_point(*movement_map, order.x, order.y) ==
                        effective.cell) {
                    label.label = kAiEntityShadowKeep;
                    continue;
                }
            }
        }
        if (typed == AiEntity2Command::attack_unit) {
            const auto target_it = target_row_by_id.find(order.target_id);
            if (target_it == target_row_by_id.end()) {
                exclude(index, AiEntity2ShadowExcludeReason::target_missing);
                continue;
            }
            argument = target_it->second;
            next_latch.target = snapshot.targets[static_cast<std::size_t>(argument)].key;
        } else if (AiEntity2CommandIsPoint(typed)) {
            if (movement_map == nullptr) {
                exclude(index, AiEntity2ShadowExcludeReason::stale);
                continue;
            }
            auto reach_it = reachability.find(row.movement_class);
            if (reach_it == reachability.end()) {
                reach_it = reachability.emplace(row.movement_class,
                    BuildAiEntityReachability(*movement_map,
                        row.movement_class)).first;
            }
            // Far teacher points (a march to the enemy base, a scout walk to
            // a start candidate) are labelled by CELL CONTAINMENT: the global
            // token of the 8x8 cell holding the teacher point, when that
            // token is legal for the row.  Only when the containing cell is
            // not addressable does the nearest-token-within-64px rule apply.
            i32 best_token = -1;
            i64 best_distance = 0;
            if (order.x >= 0 && order.y >= 0 && movement_map->width != 0 &&
                movement_map->height != 0) {
                const u32 tile_x = static_cast<u32>(order.x) >> 5;
                const u32 tile_y = static_cast<u32>(order.y) >> 5;
                auto cell_of = [](u32 tile, u32 extent) {
                    u32 cell = 0;
                    for (u32 c = 1; c < kAiEntityPointGridWidth; ++c) {
                        const u32 lower = static_cast<u32>(
                            (static_cast<u64>(c) * extent) / kAiEntityPointGridWidth);
                        if (lower <= tile) {
                            cell = c;
                        }
                    }
                    return cell;
                };
                const u32 token = cell_of(tile_y, movement_map->height) *
                    kAiEntityPointGridWidth + cell_of(tile_x, movement_map->width);
                if ((row.point_mask[token >> 5] >> (token & 31u) & 1u) != 0) {
                    const AiEntityPointResolveResult point =
                        ResolveAiEntityPointToken(*movement_map, reach_it->second,
                            row.x, row.y, token);
                    if (point.valid) {
                        best_token = static_cast<i32>(token);
                        best_distance = 0;
                        next_latch.x = point.x;
                        next_latch.y = point.y;
                    }
                }
            }
            if (best_token < 0) {
                for (u32 token = 0; token < kAiEntityPointTokenCount; ++token) {
                    if ((row.point_mask[token >> 5] >> (token & 31u) & 1u) == 0) {
                        continue;
                    }
                    const AiEntityPointResolveResult point =
                        ResolveAiEntityPointToken(*movement_map, reach_it->second,
                            row.x, row.y, token);
                    if (!point.valid) {
                        continue;
                    }
                    const i64 dx = static_cast<i64>(point.x) - order.x;
                    const i64 dy = static_cast<i64>(point.y) - order.y;
                    const i64 distance = dx * dx + dy * dy;
                    if (best_token < 0 || distance < best_distance) {
                        best_token = static_cast<i32>(token);
                        best_distance = distance;
                        next_latch.x = point.x;
                        next_latch.y = point.y;
                    }
                }
                const i64 max_error = static_cast<i64>(max_point_error_px) *
                    max_point_error_px;
                if (best_token < 0 || best_distance > max_error) {
                    exclude(index, AiEntity2ShadowExcludeReason::stale);
                    continue;
                }
            }
            argument = best_token;
        } else if (AiEntity2CommandIsEconomy(typed)) {
            u8 kind = 0;
            u64 key = 0;
            switch (typed) {
            case AiEntity2Command::harvest: {
                kind = static_cast<u8>(AiEntity2CandidateKind::resource);
                i32 x = order.x;
                i32 y = order.y;
                if (order.target_id != 0) {
                    // Resource ordered by target: not resolvable here.
                    exclude(index, AiEntity2ShadowExcludeReason::candidate_missing);
                    continue;
                }
                if (x < 0 || y < 0) {
                    exclude(index, AiEntity2ShadowExcludeReason::candidate_missing);
                    continue;
                }
                // Nearest resource candidate to the teacher's point within
                // the point-error tolerance (the executor nudges a stuck
                // worker's point by a few px, which can cross a tile edge).
                i32 found = -1;
                i64 found_distance = 0;
                for (u32 c = 0; c < snapshot.resource_rows; ++c) {
                    const AiEntity2Candidate& candidate = snapshot.candidates[c];
                    const i64 dx = static_cast<i64>(candidate.x) - x;
                    const i64 dy = static_cast<i64>(candidate.y) - y;
                    const i64 distance = dx * dx + dy * dy;
                    if (found < 0 || distance < found_distance) {
                        found = static_cast<i32>(c);
                        found_distance = distance;
                    }
                }
                if (found >= 0 && found_distance >
                    static_cast<i64>(max_point_error_px) * max_point_error_px) {
                    found = -1;
                }
                if (found < 0) {
                    exclude(index, AiEntity2ShadowExcludeReason::candidate_missing);
                    continue;
                }
                key = snapshot.candidates[static_cast<std::size_t>(found)].key;
                argument = found;
                break;
            }
            // A budget/site/queue-consuming teacher event that cannot be
            // resolved makes this row and every later economy row
            // PREFIX_UNRESOLVED (plan 15.1); only HARVEST keeps the plain
            // CANDIDATE_MISSING reason since it consumes nothing.
            case AiEntity2Command::build: {
                kind = static_cast<u8>(AiEntity2CandidateKind::build_site);
                if (order.x < 0 || order.y < 0) {
                    exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
                    mark_unresolved(index);
                    continue;
                }
                key = AiEntity2BuildKey(order.production_id,
                    static_cast<u32>(order.x) >> 5, static_cast<u32>(order.y) >> 5);
                argument = snapshot.candidate_row_of(kind, key);
                if (argument < 0) {
                    exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
                    mark_unresolved(index);
                    continue;
                }
                break;
            }
            case AiEntity2Command::produce_unit:
                kind = static_cast<u8>(AiEntity2CandidateKind::produce_unit);
                key = AiEntity2ProduceKey(order.production_id);
                argument = snapshot.candidate_row_of(kind, key);
                if (argument < 0) {
                    exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
                    mark_unresolved(index);
                    continue;
                }
                break;
            default: {
                kind = static_cast<u8>(AiEntity2CandidateKind::research_upgrade);
                // Next level is the candidate's; search the order id.
                argument = -1;
                const u32 begin = snapshot.resource_rows + snapshot.build_rows +
                    snapshot.produce_rows;
                for (u32 c = begin; c < begin + snapshot.research_rows; ++c) {
                    if (snapshot.candidates[c].object_id == order.production_id) {
                        argument = static_cast<i32>(c);
                        break;
                    }
                }
                if (argument < 0) {
                    exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
                    mark_unresolved(index);
                    continue;
                }
                key = snapshot.candidates[static_cast<std::size_t>(argument)].key;
                break;
            }
            }
            next_latch.candidate_kind = kind;
            next_latch.candidate_key = key;
        }
        // Base command mask gate (dynamic legality is checked below).
        if ((row.command_mask & (1u << command)) == 0) {
            if (AiEntity2CommandIsEconomy(typed) && typed != AiEntity2Command::harvest) {
                exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
                mark_unresolved(index);
            } else {
                exclude(index, AiEntity2ShadowExcludeReason::mask_mismatch);
            }
            continue;
        }
        // KEEP vs ISSUE against the shadow's own latch (persistent orders
        // only; PRODUCE/RESEARCH are enqueue events and always ISSUE).
        const auto latch_it = state.latches.find(latch_key);
        const AiEntity2ShadowLatch* latch =
            latch_it != state.latches.end() ? &latch_it->second : nullptr;
        bool same = latch != nullptr && latch->valid && latch->command == command;
        if (same && typed == AiEntity2Command::attack_unit) {
            same = latch->target == next_latch.target;
        } else if (same && AiEntity2CommandIsPoint(typed)) {
            same = latch->x == next_latch.x && latch->y == next_latch.y;
        } else if (same && (typed == AiEntity2Command::harvest ||
                typed == AiEntity2Command::build)) {
            same = latch->candidate_kind == next_latch.candidate_kind &&
                latch->candidate_key == next_latch.candidate_key;
        } else if (same) {
            same = false;
        }
        if (same) {
            label.label = kAiEntityShadowKeep;
            label.command = 0;
            label.argument = -1;
        } else {
            label.label = kAiEntityShadowIssue;
            label.command = command;
            label.argument = argument;
            commands[index] = command;
            arguments[index] = argument;
            if (typed != AiEntity2Command::produce_unit &&
                typed != AiEntity2Command::research_upgrade) {
                state.latches[latch_key] = next_latch;
            }
        }
    }

    // Second pass: replay the teacher prefix; an ISSUE that is illegal under
    // its own dynamic mask becomes MASK_MISMATCH (economy: unresolved too).
    out_replay = AiEntity2ReplayLedger(snapshot, commands, arguments,
        unresolved_from, &assign_vec);
    for (std::size_t index = 0; index < own_count; ++index) {
        if (labels[index].label != kAiEntityShadowIssue) {
            continue;
        }
        const bool unresolved = index >= unresolved_from;
        if (!unresolved && out_replay.choice_legal[index] != 0) {
            continue;
        }
        const AiEntity2Command typed = AiEntity2EngineCommandOf(labels[index].command);
        if (AiEntity2CommandIsEconomy(typed) && typed != AiEntity2Command::harvest) {
            mark_unresolved(index);
            exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
        } else if (unresolved && AiEntity2CommandIsEconomy(typed)) {
            exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
        } else {
            exclude(index, AiEntity2ShadowExcludeReason::mask_mismatch);
        }
        commands[index] = 0;
        arguments[index] = -1;
    }
    if (unresolved_from != kAiEntity2NoUnresolvedRow) {
        // Every economy row at/after the unresolved point is excluded with
        // PREFIX_UNRESOLVED; combat labels stay usable.
        for (std::size_t index = unresolved_from; index < own_count; ++index) {
            const AiEntityOwnRow& row = snapshot.own[index];
            const bool economy_row = row.role == static_cast<u8>(AiEntity2Role::worker) ||
                row.role == static_cast<u8>(AiEntity2Role::building);
            if (!economy_row || labels[index].label == kAiEntityShadowExcluded) {
                continue;   // an excluded row keeps its own reason
            }
            if (labels[index].label == kAiEntityShadowIssue &&
                !AiEntity2CommandIsEconomy(
                    AiEntity2EngineCommandOf(labels[index].command))) {
                continue;   // a worker's combat ISSUE stays
            }
            exclude(index, AiEntity2ShadowExcludeReason::prefix_unresolved);
            commands[index] = 0;
            arguments[index] = -1;
        }
        out_replay = AiEntity2ReplayLedger(snapshot, commands, arguments,
            unresolved_from, &assign_vec);
    }
    return labels;
}

std::vector<u8> EncodeAiEntity2ShadowRecord(const AiEntity2WireHeader& header,
    const std::vector<u8>& payload,
    const std::vector<AiEntity2ShadowLabel>& labels,
    const AiEntity2LedgerReplay& replay,
    const std::array<AiEntity2ShadowSlotLabel, kAiEntity2SlotCount>& slot_labels) {
    std::vector<u8> record;
    const u32 u = header.own_rows;
    const u32 words = (header.candidate_rows() + 31u) / 32u;
    // SHD3 body: header + payload + u32 U + 16-byte labels + dynamic command
    // masks + budgets + dynamic pair words + dynamic assign masks + 4 slot
    // labels (8 bytes each).
    const std::size_t body_bytes = kAiEntity2WireHeaderBytes + payload.size() +
        4 + static_cast<std::size_t>(u) * (16 + 4 + 12 + 4) +
        static_cast<std::size_t>(u) * words * 4 + kAiEntity2SlotCount * 8;
    record.reserve(8 + body_bytes);
    Writer2 w{&record};
    for (char c : kAiEntity2ShadowRecordMagic) {
        w.u8v(static_cast<u8>(c));
    }
    w.u32v(static_cast<u32>(body_bytes));
    u8 header_bytes[kAiEntity2WireHeaderBytes];
    AiEntity2WriteWireHeader(header, header_bytes);
    record.insert(record.end(), header_bytes,
        header_bytes + kAiEntity2WireHeaderBytes);
    record.insert(record.end(), payload.begin(), payload.end());
    w.u32v(u);
    for (u32 i = 0; i < u; ++i) {
        const AiEntity2ShadowLabel& label = i < labels.size() ?
            labels[i] : AiEntity2ShadowLabel{};
        w.u8v(label.label);
        w.u8v(label.command);
        w.u16v(label.exclude_reason);
        w.i32v(label.argument);
        w.f32v(label.inclusion_probability);
        w.u8v(label.assign_label);
        w.u8v(label.assign);
        w.u16v(0);
    }
    for (u32 i = 0; i < u; ++i) {
        w.u32v(i < replay.dynamic_command_mask.size() ?
            replay.dynamic_command_mask[i] : 1u);
    }
    for (u32 i = 0; i < u; ++i) {
        const std::array<u32, 3> budget = i < replay.remaining_budget.size() ?
            replay.remaining_budget[i] : std::array<u32, 3>{0, 0, 0};
        for (u32 value : budget) {
            w.u32v(value);
        }
    }
    for (u32 i = 0; i < u; ++i) {
        for (u32 word = 0; word < words; ++word) {
            const std::size_t index = static_cast<std::size_t>(i) * words + word;
            w.u32v(index < replay.dynamic_economy_pair_mask.size() ?
                replay.dynamic_economy_pair_mask[index] : 0u);
        }
    }
    for (u32 i = 0; i < u; ++i) {
        w.u32v(i < replay.dynamic_assign_mask.size() ?
            replay.dynamic_assign_mask[i] : 0u);
    }
    for (const AiEntity2ShadowSlotLabel& label : slot_labels) {
        w.u8v(label.label);
        w.u8v(label.command);
        w.u16v(0);
        w.i32v(label.cell);
    }
    return record;
}

}  // namespace ranker
