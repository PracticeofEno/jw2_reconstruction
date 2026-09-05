#include "ranker_ai_entity_control.h"

#include "ranker_ai_actions.h"
#include "ranker_ai_micro_executor.h"
#include "ranker_unit_action.h"
#include "ranker_unit_commands.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace ranker {

static_assert(kAiEntityTargetFlagTransient == kUnitActionTargetTransient,
    "mirrored transient flag drifted from the engine constant");
static_assert(kAiEntityTargetFlagInactive == kUnitActionTargetInactive,
    "mirrored inactive flag drifted from the engine constant");
static_assert(kAiEntityTargetFlagClassBlocked == kUnitActionTargetClassBlocked,
    "mirrored class-blocked flag drifted from the engine constant");

namespace {

constexpr u32 kMobileTypeLimit = 0x60u;
constexpr u32 kNeutralOwnerId = 8u;
constexpr u32 kEntityCapabilityBitsMask = (1u << 4) | (1u << 5) | (1u << 9);
constexpr u32 kAttackCapabilityBit = 1u << 5;
constexpr u32 kMoveCapabilityBit = 1u << 4;
constexpr u32 kPatrolCapabilityBit = 1u << 9;
constexpr u32 kHarvestCapabilityBit = 1u << 7;
constexpr u32 kMeleeRangeThreshold = 64u;

// Live-unit role vocabulary for the control signature (matches
// AiMicroRoleOf's decision order on the raw definition).
enum : u8 {
    kLiveRoleWorker = 0,
    kLiveRoleMelee = 1,
    kLiveRoleRanged = 2,
    kLiveRoleTransport = 3,
    kLiveRoleBuilding = 4,
    kLiveRoleOther = 5,
};

u8 live_role_of(const UnitMovementUnit& unit) {
    if (unit.type_id >= kMobileTypeLimit) {
        return kLiveRoleBuilding;
    }
    if ((unit.type_flags & kHarvestCapabilityBit) != 0) {
        return kLiveRoleWorker;
    }
    const bool attacks = (unit.type_flags & kAttackCapabilityBit) != 0;
    if (unit.definition.transport_capacity > 0 && !attacks) {
        return kLiveRoleTransport;
    }
    if (!attacks) {
        return kLiveRoleOther;
    }
    return unit.definition.action_range_base != 0 &&
        unit.definition.action_range_base <= kMeleeRangeThreshold ?
        kLiveRoleMelee : kLiveRoleRanged;
}

void set_registry_fatal(AiEntityRegistry& registry, const char* reason) {
    if (!registry.contract_fatal) {
        registry.contract_fatal = true;
        registry.fatal_reason = reason;
    }
}

AiEntityRegistryRecord* find_or_create_record(AiEntityRegistry& registry,
    const UnitMovementUnit& unit) {
    if (unit.runtime_slot_index < kAiEntityWireRowLimit) {
        return &registry.fixed[unit.runtime_slot_index];
    }
    return &registry.detached[static_cast<const void*>(&unit)];
}

const AiEntityRegistryRecord* find_record(const AiEntityRegistry& registry,
    const UnitMovementUnit& unit) {
    if (unit.runtime_slot_index < kAiEntityWireRowLimit) {
        const AiEntityRegistryRecord& record =
            registry.fixed[unit.runtime_slot_index];
        return record.generation != 0 ? &record : nullptr;
    }
    const auto it = registry.detached.find(static_cast<const void*>(&unit));
    return it != registry.detached.end() ? &it->second : nullptr;
}

// Commits one inactive->active transition on an already-resolved record.
void commit_activation_record(AiEntityRegistry& registry,
    AiEntityRegistryRecord& record, const UnitMovementUnit& unit) {
    if (record.engine_active && record.runtime_id == unit.id) {
        return;   // overlapping helper call of the same activation
    }
    if (record.generation == 0xffffffffu) {
        set_registry_fatal(registry, "activation generation u32 wrap");
        return;
    }
    record.runtime_id = unit.id;
    record.generation += 1;
    record.control_epoch = 1;
    record.engine_active = true;
    record.has_signature = true;
    record.signature = AiEntityControlSignatureOf(unit);
}

}  // namespace

AiEntityControlSignature AiEntityControlSignatureOf(
    const UnitMovementUnit& unit) {
    AiEntityControlSignature signature;
    signature.owner_id = unit.owner_id;
    signature.type_id = unit.type_id;
    signature.role = live_role_of(unit);
    signature.capability_bits = unit.type_flags & kEntityCapabilityBitsMask;
    signature.movement_class = unit.definition.movement_class;
    signature.direct_eligible = unit.type_id < kMobileTypeLimit &&
        (signature.role == kLiveRoleMelee || signature.role == kLiveRoleRanged);
    return signature;
}

void AiEntityRegistryReset(AiEntityRegistry& registry) {
    for (AiEntityRegistryRecord& record : registry.fixed) {
        record = AiEntityRegistryRecord{};
    }
    registry.detached.clear();
    registry.contract_fatal = false;
    registry.fatal_reason.clear();
}

void AiEntityRegistryCommitActivation(AiEntityRegistry& registry,
    const UnitMovementUnit& unit) {
    if (registry.contract_fatal) {
        return;
    }
    AiEntityRegistryRecord* record = find_or_create_record(registry, unit);
    commit_activation_record(registry, *record, unit);
}

void AiEntityRegistryMarkDeactivated(AiEntityRegistry& registry,
    const UnitMovementUnit& unit) {
    if (registry.contract_fatal) {
        return;
    }
    AiEntityRegistryRecord* record = nullptr;
    if (unit.runtime_slot_index < kAiEntityWireRowLimit) {
        record = &registry.fixed[unit.runtime_slot_index];
    } else {
        const auto it = registry.detached.find(
            static_cast<const void*>(&unit));
        if (it == registry.detached.end()) {
            return;
        }
        record = &it->second;
    }
    if (!record->engine_active) {
        return;
    }
    record->engine_active = false;
    if (registry.events.on_deactivated != nullptr) {
        const AiEntityKey key{record->runtime_id, record->generation};
        registry.events.on_deactivated(registry.events.ctx, key);
    }
}

void AiEntityRegistryAuditFrame(AiEntityRegistry& registry,
    const std::vector<UnitMovementUnit*>& active_units) {
    if (registry.contract_fatal) {
        return;
    }
    std::unordered_set<u32> seen_ids;
    std::unordered_set<const AiEntityRegistryRecord*> seen_records;
    seen_ids.reserve(active_units.size());
    seen_records.reserve(active_units.size());
    for (UnitMovementUnit* unit : active_units) {
        if (unit == nullptr || !unit->active) {
            continue;
        }
        if (!seen_ids.insert(unit->id).second) {
            // Two simultaneously active records with one runtime id cannot
            // address ordered command targets uniquely: no silent tie-break.
            set_registry_fatal(registry, "duplicate active runtime id");
            return;
        }
        AiEntityRegistryRecord* record = find_or_create_record(registry, *unit);
        if (!record->engine_active || record->runtime_id != unit->id) {
            commit_activation_record(registry, *record, *unit);
            if (registry.contract_fatal) {
                return;
            }
        } else {
            const AiEntityControlSignature signature =
                AiEntityControlSignatureOf(*unit);
            if (!record->has_signature) {
                record->has_signature = true;
                record->signature = signature;
            } else if (signature != record->signature) {
                if (record->control_epoch == 0xffffffffu) {
                    set_registry_fatal(registry, "control epoch u32 wrap");
                    return;
                }
                record->control_epoch += 1;
                record->signature = signature;
                if (registry.events.on_control_epoch_changed != nullptr) {
                    const AiEntityKey key{record->runtime_id,
                        record->generation};
                    registry.events.on_control_epoch_changed(
                        registry.events.ctx, key, record->control_epoch);
                }
            }
        }
        seen_records.insert(record);
    }
    // Records still marked active but absent from the engine's active set.
    std::vector<AiEntityKey> deactivated;
    auto sweep = [&](AiEntityRegistryRecord& record) {
        if (record.engine_active && seen_records.count(&record) == 0) {
            record.engine_active = false;
            deactivated.push_back(
                AiEntityKey{record.runtime_id, record.generation});
        }
    };
    for (AiEntityRegistryRecord& record : registry.fixed) {
        sweep(record);
    }
    for (auto& entry : registry.detached) {
        sweep(entry.second);
    }
    // Deterministic event order regardless of map iteration order.
    std::sort(deactivated.begin(), deactivated.end());
    if (registry.events.on_deactivated != nullptr) {
        for (const AiEntityKey& key : deactivated) {
            registry.events.on_deactivated(registry.events.ctx, key);
        }
    }
}

const AiEntityRegistryRecord* AiEntityRegistryFindByUnit(
    const AiEntityRegistry& registry, const UnitMovementUnit& unit) {
    return find_record(registry, unit);
}

const AiEntityRegistryRecord* AiEntityRegistryFindByObserved(
    const AiEntityRegistry& registry, u32 runtime_id, u32 runtime_slot_index) {
    if (runtime_slot_index < kAiEntityWireRowLimit) {
        const AiEntityRegistryRecord& record =
            registry.fixed[runtime_slot_index];
        return record.generation != 0 ? &record : nullptr;
    }
    // Detached lookup by id: active record first, then any historical one.
    const AiEntityRegistryRecord* fallback = nullptr;
    for (const auto& entry : registry.detached) {
        if (entry.second.runtime_id != runtime_id) {
            continue;
        }
        if (entry.second.engine_active) {
            return &entry.second;
        }
        if (fallback == nullptr) {
            fallback = &entry.second;
        }
    }
    return fallback;
}

// ---------------------------------------------------------------------------
// Point geometry v1
// ---------------------------------------------------------------------------

namespace {

u32 map_stride_tiles(const UnitMovementMap& map) {
    const u32 stride = map.stride_tiles != 0 ? map.stride_tiles : map.width;
    return std::max(stride, map.width);
}

const UnitMovementCell* map_cell_at(const UnitMovementMap& map, u32 tile_x,
    u32 tile_y) {
    if (tile_x >= map.width || tile_y >= map.height) {
        return nullptr;
    }
    const std::size_t index =
        static_cast<std::size_t>(tile_y) * map_stride_tiles(map) + tile_x;
    if (index >= map.cells.size()) {
        return nullptr;
    }
    return &map.cells[index];
}

// Fixed 8-neighbor order shared with the engine pathfinder: N,E,S,W,NW,NE,
// SE,SW.  Diagonals check the destination tile only (no corner-clear rule).
struct TileDelta {
    i32 dx;
    i32 dy;
};
constexpr std::array<TileDelta, 8> kEntityNeighborOffsets = {{
    {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    {-1, -1}, {1, -1}, {1, 1}, {-1, 1},
}};

// Local point-token offsets (plan section 7): direction order E,SE,S,SW,W,
// NW,N,NE with y+ = south; diagonal components approximate the Euclidean
// radius per radius step {64,128,256,512} -> {45,91,181,362}.
constexpr std::array<i32, 4> kEntityLocalRadiusPx = {64, 128, 256, 512};
constexpr std::array<i32, 4> kEntityLocalDiagonalPx = {45, 91, 181, 362};

void local_token_offset(u32 radius_index, u32 direction_index, i32* out_dx,
    i32* out_dy) {
    const i32 r = kEntityLocalRadiusPx[radius_index];
    const i32 d = kEntityLocalDiagonalPx[radius_index];
    switch (direction_index) {
    case 0: *out_dx = r;  *out_dy = 0;  break;   // E
    case 1: *out_dx = d;  *out_dy = d;  break;   // SE
    case 2: *out_dx = 0;  *out_dy = r;  break;   // S
    case 3: *out_dx = -d; *out_dy = d;  break;   // SW
    case 4: *out_dx = -r; *out_dy = 0;  break;   // W
    case 5: *out_dx = -d; *out_dy = -d; break;   // NW
    case 6: *out_dx = 0;  *out_dy = -r; break;   // N
    default: *out_dx = d; *out_dy = -d; break;   // NE
    }
}

// Reachable component set of a unit standing at (tile_x, tile_y).  The
// source tile is a virtual flood seed even when static-invalid: expansion
// continues only through statically enterable tiles.
struct ReachableFrom {
    // Component ids reachable from the seed (sorted, unique).
    std::array<u32, 9> components{};
    u32 count = 0;

    bool contains(u32 component) const {
        for (u32 i = 0; i < count; ++i) {
            if (components[i] == component) {
                return true;
            }
        }
        return false;
    }
};

u32 reach_component_at(const AiEntityReachability& reach, u32 tile_x,
    u32 tile_y) {
    if (tile_x >= reach.width || tile_y >= reach.height) {
        return 0;
    }
    return reach.component[
        static_cast<std::size_t>(tile_y) * reach.width + tile_x];
}

ReachableFrom reachable_from_tile(const AiEntityReachability& reach,
    u32 tile_x, u32 tile_y) {
    ReachableFrom result;
    auto add = [&result](u32 component) {
        if (component == 0 || result.contains(component)) {
            return;
        }
        result.components[result.count++] = component;
    };
    add(reach_component_at(reach, tile_x, tile_y));
    if (result.count == 0) {
        // Virtual seed: join the components of the enterable 8-neighbors.
        for (const TileDelta& delta : kEntityNeighborOffsets) {
            const i64 nx = static_cast<i64>(tile_x) + delta.dx;
            const i64 ny = static_cast<i64>(tile_y) + delta.dy;
            if (nx < 0 || ny < 0) {
                continue;
            }
            add(reach_component_at(reach, static_cast<u32>(nx),
                static_cast<u32>(ny)));
        }
    }
    std::sort(result.components.begin(),
        result.components.begin() + result.count);
    return result;
}

bool tile_reachable(const AiEntityReachability& reach,
    const ReachableFrom& from, u32 tile_x, u32 tile_y) {
    const u32 component = reach_component_at(reach, tile_x, tile_y);
    return component != 0 && from.contains(component);
}

// Best enterable+reachable tile of global cell `token` (0..63): minimal
// squared distance from the tile center to the cell's rational geometric
// center; ties break on the smaller row-major tile index.  Returns false if
// the cell holds no such tile.
bool resolve_global_cell_tile(const UnitMovementMap& map,
    const AiEntityReachability& reach, const ReachableFrom& from, u32 token,
    u32* out_tile_x, u32* out_tile_y) {
    const u32 cell_x = token % kAiEntityPointGridWidth;
    const u32 cell_y = token / kAiEntityPointGridWidth;
    const u32 x0 = static_cast<u32>((static_cast<u64>(cell_x) * map.width) /
        kAiEntityPointGridWidth);
    const u32 x1 = static_cast<u32>(
        (static_cast<u64>(cell_x + 1) * map.width) / kAiEntityPointGridWidth);
    const u32 y0 = static_cast<u32>((static_cast<u64>(cell_y) * map.height) /
        kAiEntityPointGridWidth);
    const u32 y1 = static_cast<u32>(
        (static_cast<u64>(cell_y + 1) * map.height) / kAiEntityPointGridWidth);
    // Doubled coordinates keep the rational center exact: tile center is
    // (2t+1), the cell center is (x0+x1).
    const i64 center_x2 = static_cast<i64>(x0) + x1;
    const i64 center_y2 = static_cast<i64>(y0) + y1;
    bool found = false;
    i64 best_distance = 0;
    u64 best_index = 0;
    u32 best_x = 0;
    u32 best_y = 0;
    for (u32 ty = y0; ty < y1; ++ty) {
        for (u32 tx = x0; tx < x1; ++tx) {
            if (!tile_reachable(reach, from, tx, ty)) {
                continue;
            }
            const i64 dx = 2 * static_cast<i64>(tx) + 1 - center_x2;
            const i64 dy = 2 * static_cast<i64>(ty) + 1 - center_y2;
            const i64 distance = dx * dx + dy * dy;
            const u64 index = static_cast<u64>(ty) * map.width + tx;
            if (!found || distance < best_distance ||
                (distance == best_distance && index < best_index)) {
                found = true;
                best_distance = distance;
                best_index = index;
                best_x = tx;
                best_y = ty;
            }
        }
    }
    if (found) {
        *out_tile_x = best_x;
        *out_tile_y = best_y;
    }
    return found;
}

bool resolve_local_token_point(const UnitMovementMap& map,
    const AiEntityReachability& reach, const ReachableFrom& from,
    i32 unit_x, i32 unit_y, u32 token, i32* out_x, i32* out_y) {
    const u32 local = token - kAiEntityPointGlobalTokenCount;
    const u32 radius_index = local / 8u;
    const u32 direction_index = local % 8u;
    i32 dx = 0;
    i32 dy = 0;
    local_token_offset(radius_index, direction_index, &dx, &dy);
    const i64 x = static_cast<i64>(unit_x) + dx;
    const i64 y = static_cast<i64>(unit_y) + dy;
    if (x < 0 || y < 0 ||
        x >= static_cast<i64>(map.width) * 32 ||
        y >= static_cast<i64>(map.height) * 32) {
        return false;
    }
    const u32 tile_x = static_cast<u32>(x) >> 5;
    const u32 tile_y = static_cast<u32>(y) >> 5;
    if (!tile_reachable(reach, from, tile_x, tile_y)) {
        return false;
    }
    *out_x = static_cast<i32>(x);
    *out_y = static_cast<i32>(y);
    return true;
}

}  // namespace

bool AiEntityStaticCellEnterable(const UnitMovementMap& map,
    u32 movement_class, u32 tile_x, u32 tile_y) {
    const UnitMovementCell* cell = map_cell_at(map, tile_x, tile_y);
    if (cell == nullptr) {
        return false;
    }
    const bool has_legacy_layers = map.legacy_entry_layers_present ||
        cell->alternate_flags != 0 || cell->visibility_flags != 0;
    if (!has_legacy_layers) {
        // No legacy entry layers: static terrain only (the engine's
        // IsPassableTerrainCell rule), rejecting static kMapCellBlockedTerrain
        // and deliberately ignoring the dynamic kMapCellReservedByUnit bit.
        return (cell->flags & kMapCellTerrainMask) == kMapCellPassableTerrain &&
            (cell->flags & kMapCellBlockedTerrain) == 0;
    }
    // legacy_movement_class_can_enter_cell with allow_command_shortcut=false
    // and the runtime building-footprint bit cleared from the static copy.
    const u32 decoration_flags = cell->alternate_flags;
    const u32 brush_flags = cell->visibility_flags & ~0x20000000u;
    const bool terrain_clear = (cell->flags & kMapCellTerrainMask) == 0;
    const bool brush_clear = (brush_flags & 0x20000000u) == 0;
    switch (movement_class) {
    case 0:
        if ((decoration_flags & 0x20000000u) == 0 ||
            (decoration_flags & 0x60000000u) == 0) {
            return false;
        }
        return terrain_clear && brush_clear;
    case 1:
        return false;
    case 2:
        if ((decoration_flags & 0x60000000u) == 0) {
            return false;
        }
        return terrain_clear && brush_clear;
    case 3:
        return true;
    case 4:
        if ((decoration_flags & 0x40000000u) == 0) {
            return false;
        }
        return terrain_clear && brush_clear;
    default:
        return false;
    }
}

AiEntityReachability BuildAiEntityReachability(const UnitMovementMap& map,
    u32 movement_class) {
    AiEntityReachability reach;
    reach.movement_class = movement_class;
    reach.width = map.width;
    reach.height = map.height;
    const std::size_t tile_count =
        static_cast<std::size_t>(map.width) * map.height;
    reach.component.assign(tile_count, 0);
    if (tile_count == 0) {
        return reach;
    }
    std::vector<u8> enterable(tile_count, 0);
    for (u32 ty = 0; ty < map.height; ++ty) {
        for (u32 tx = 0; tx < map.width; ++tx) {
            enterable[static_cast<std::size_t>(ty) * map.width + tx] =
                AiEntityStaticCellEnterable(map, movement_class, tx, ty) ?
                1 : 0;
        }
    }
    u32 next_component = 0;
    std::vector<u32> queue;
    for (u32 ty = 0; ty < map.height; ++ty) {
        for (u32 tx = 0; tx < map.width; ++tx) {
            const std::size_t index =
                static_cast<std::size_t>(ty) * map.width + tx;
            if (enterable[index] == 0 || reach.component[index] != 0) {
                continue;
            }
            ++next_component;
            reach.component[index] = next_component;
            queue.clear();
            queue.push_back(static_cast<u32>(index));
            while (!queue.empty()) {
                const u32 current = queue.back();
                queue.pop_back();
                const u32 cx = current % map.width;
                const u32 cy = current / map.width;
                for (const TileDelta& delta : kEntityNeighborOffsets) {
                    const i64 nx = static_cast<i64>(cx) + delta.dx;
                    const i64 ny = static_cast<i64>(cy) + delta.dy;
                    if (nx < 0 || ny < 0 || nx >= map.width ||
                        ny >= map.height) {
                        continue;
                    }
                    const std::size_t neighbor =
                        static_cast<std::size_t>(ny) * map.width +
                        static_cast<std::size_t>(nx);
                    if (enterable[neighbor] == 0 ||
                        reach.component[neighbor] != 0) {
                        continue;
                    }
                    reach.component[neighbor] = next_component;
                    queue.push_back(static_cast<u32>(neighbor));
                }
            }
        }
    }
    return reach;
}

bool AiEntityTileReachableFromUnit(const AiEntityReachability& reach,
    i32 unit_x, i32 unit_y, u32 tile_x, u32 tile_y) {
    const u32 seed_x = static_cast<u32>(std::max(unit_x, 0)) >> 5;
    const u32 seed_y = static_cast<u32>(std::max(unit_y, 0)) >> 5;
    const ReachableFrom from = reachable_from_tile(reach, seed_x, seed_y);
    return tile_reachable(reach, from, tile_x, tile_y);
}

void BuildAiEntityPointMask(const UnitMovementMap& map,
    const AiEntityReachability& reach, i32 unit_x, i32 unit_y,
    std::array<u32, kAiEntityPointMaskWords>& mask_out) {
    mask_out.fill(0);
    const u32 seed_x = static_cast<u32>(std::max(unit_x, 0)) >> 5;
    const u32 seed_y = static_cast<u32>(std::max(unit_y, 0)) >> 5;
    const ReachableFrom from = reachable_from_tile(reach, seed_x, seed_y);
    auto set_bit = [&mask_out](u32 token) {
        mask_out[token >> 5] |= 1u << (token & 31u);
    };
    for (u32 token = 0; token < kAiEntityPointGlobalTokenCount; ++token) {
        u32 tile_x = 0;
        u32 tile_y = 0;
        if (resolve_global_cell_tile(map, reach, from, token, &tile_x,
                &tile_y)) {
            set_bit(token);
        }
    }
    for (u32 token = kAiEntityPointGlobalTokenCount;
        token < kAiEntityPointTokenCount; ++token) {
        i32 x = 0;
        i32 y = 0;
        if (resolve_local_token_point(map, reach, from, unit_x, unit_y, token,
                &x, &y)) {
            set_bit(token);
        }
    }
}

AiEntityPointResolveResult ResolveAiEntityPointToken(const UnitMovementMap& map,
    const AiEntityReachability& reach, i32 unit_x, i32 unit_y, u32 token) {
    AiEntityPointResolveResult result;
    if (token >= kAiEntityPointTokenCount) {
        return result;
    }
    const u32 seed_x = static_cast<u32>(std::max(unit_x, 0)) >> 5;
    const u32 seed_y = static_cast<u32>(std::max(unit_y, 0)) >> 5;
    const ReachableFrom from = reachable_from_tile(reach, seed_x, seed_y);
    if (token < kAiEntityPointGlobalTokenCount) {
        u32 tile_x = 0;
        u32 tile_y = 0;
        if (!resolve_global_cell_tile(map, reach, from, token, &tile_x,
                &tile_y)) {
            return result;
        }
        result.valid = true;
        result.x = static_cast<i32>(tile_x * 32u + 16u);
        result.y = static_cast<i32>(tile_y * 32u + 16u);
        return result;
    }
    i32 x = 0;
    i32 y = 0;
    if (!resolve_local_token_point(map, reach, from, unit_x, unit_y, token,
            &x, &y)) {
        return result;
    }
    result.valid = true;
    result.x = x;
    result.y = y;
    return result;
}

// ---------------------------------------------------------------------------
// Authoritative attack pair predicate
// ---------------------------------------------------------------------------

AiEntityPairDecision AiEntityEvaluateAttackPair(
    const AiEntityPairSource& source, const AiEntityPairTarget& target,
    const AiEntityLiveHooks& live) {
    AiEntityPairDecision decision;
    if (!source.active_owned_alive) {
        decision.reject = AiEntityRejectCode::ownership;
        return decision;
    }
    if (!source.has_attack_capability ||
        source.distance_check_mode == 1u) {
        decision.reject = AiEntityRejectCode::capability;
        return decision;
    }
    if (!target.active_alive) {
        decision.reject = AiEntityRejectCode::inactive;
        return decision;
    }
    if (!target.visible) {
        decision.reject = AiEntityRejectCode::visibility;
        return decision;
    }
    if (!target.non_friendly) {
        decision.reject = AiEntityRejectCode::hostility;
        return decision;
    }
    if ((target.runtime_flags & (kAiEntityTargetFlagTransient |
            kAiEntityTargetFlagInactive)) != 0) {
        decision.reject = AiEntityRejectCode::inactive;
        return decision;
    }
    if ((target.runtime_flags & kAiEntityTargetFlagClassBlocked) != 0) {
        decision.reject = AiEntityRejectCode::render_class;
        return decision;
    }
    if (target.render_class < kAiEntityRenderClassLimit) {
        if ((source.attackable_class_mask & (1u << target.render_class)) ==
            0) {
            decision.reject = AiEntityRejectCode::render_class;
            return decision;
        }
    }
    // render_class >= 32: engine-permissive, the mask gate is skipped.
    if (target.render_class == 2u && source.render_class2_terrain_gate == 0u) {
        if (live.source_can_enter_cell != nullptr &&
            !live.source_can_enter_cell(live.ctx, source.runtime_id,
                target.x & ~31, target.y & ~31)) {
            decision.reject = AiEntityRejectCode::terrain;
            return decision;
        }
    }
    decision.legal = true;
    return decision;
}

// ---------------------------------------------------------------------------
// Snapshot builder
// ---------------------------------------------------------------------------

namespace {

bool is_neutral_monster_unit(const AiObservedUnit& unit) {
    return unit.visible && unit.alive && !unit.controlled &&
        unit.owner_id == kNeutralOwnerId && unit.type_id < kMobileTypeLimit;
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

float clamp01(float value) {
    return std::min(std::max(value, 0.0f), 1.0f);
}

float ratio_or_zero(u32 numerator, u32 denominator) {
    if (denominator == 0) {
        return 0.0f;
    }
    return clamp01(static_cast<float>(numerator) /
        static_cast<float>(denominator));
}

// Facing unit vector.  Direction index 0 is "no facing"; 8-direction units
// use indices 1..8 of the engine's 8-delta table, 16-direction units use
// 1..16 of the 16-delta table (observation schema v5 carries the lookup
// flag so the encoders agree on the interpretation).
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
    // Screen coordinates: sin pairs with x, cos with -y (north = angle 0).
    *out_sin = dx / length;
    *out_cos = -dy / length;
}

struct RowSortEntry {
    AiEntityKey key;
    u32 unit_index = 0;
};

}  // namespace

AiEntitySnapshot BuildAiEntitySnapshot(const AiEntitySnapshotInput& input) {
    AiEntitySnapshot snapshot;
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

    // ---- row selection (plan section 5.1 / 5.2) ----
    std::vector<RowSortEntry> own_entries;
    std::vector<RowSortEntry> target_entries;
    for (u32 index = 0; index < observation.units.size(); ++index) {
        const AiObservedUnit& unit = observation.units[index];
        const AiMicroRole role = AiMicroRoleOf(unit);
        if (unit.controlled && unit.alive && !unit.under_construction &&
            unit.type_id < kMobileTypeLimit &&
            (role == AiMicroRole::melee || role == AiMicroRole::ranged)) {
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

    // ---- target rows ----
    std::unordered_map<u32, i32> target_row_by_id;
    snapshot.targets.reserve(target_entries.size());
    for (const RowSortEntry& entry : target_entries) {
        const AiObservedUnit& unit = observation.units[entry.unit_index];
        AiEntityTargetRow row;
        row.key = entry.key;
        row.type_id = static_cast<u16>(unit.type_id);
        row.owner_id = static_cast<u8>(unit.owner_id);
        const AiMicroRole role = AiMicroRoleOf(unit);
        row.role = role == AiMicroRole::melee ? 0 :
            role == AiMicroRole::ranged ? 1 : 2;
        row.render_class = unit.render_class;
        row.kind_bits = 0;
        if (unit.type_id < kMobileTypeLimit) {
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
        target_row_by_id[unit.id] =
            static_cast<i32>(snapshot.targets.size());
        snapshot.targets.push_back(row);
    }

    // ---- point-mask reachability caches per movement class ----
    std::unordered_map<u32, AiEntityReachability> reachability;
    if (input.movement_map != nullptr) {
        for (const RowSortEntry& entry : own_entries) {
            const AiObservedUnit& unit = observation.units[entry.unit_index];
            if (reachability.find(unit.movement_class) ==
                reachability.end()) {
                reachability.emplace(unit.movement_class,
                    BuildAiEntityReachability(*input.movement_map,
                        unit.movement_class));
            }
        }
    }

    // ---- own rows ----
    const u32 own_count = static_cast<u32>(own_entries.size());
    const u32 target_count = static_cast<u32>(snapshot.targets.size());
    const u32 pair_words_per_row = (target_count + 31u) / 32u;
    snapshot.attack_pair_mask.assign(
        static_cast<std::size_t>(own_count) * pair_words_per_row, 0u);
    snapshot.own.reserve(own_count);
    for (u32 own_index = 0; own_index < own_count; ++own_index) {
        const RowSortEntry& entry = own_entries[own_index];
        const AiObservedUnit& unit = observation.units[entry.unit_index];
        AiEntityOwnRow row;
        row.key = entry.key;
        row.control_epoch = epoch_of(unit);
        row.x = unit.x;
        row.y = unit.y;
        row.type_id = static_cast<u16>(unit.type_id);
        row.movement_class = unit.movement_class;
        row.distance_check_mode = unit.distance_check_mode;
        const AiMicroRole role = AiMicroRoleOf(unit);
        row.role = role == AiMicroRole::melee ? 0 : 1;
        row.render_class = unit.render_class;
        row.command_base_state = unit.command_state & kUnitCommandStateMask;
        row.command_state_high_flags =
            unit.command_state & ~kUnitCommandStateMask;
        row.unit_command_flags = unit.command_flags;
        row.movement_state = unit.movement_state;
        // Phase A: no active-order store yet.  A non-idle engine order with
        // no semantic record encodes as EXTERNAL_UNKNOWN (plan section 5.3).
        const bool engine_idle = row.command_base_state == 0u ||
            row.command_base_state == 1u;
        row.semantic_order = engine_idle ?
            static_cast<u8>(AiEntityWireSemanticOrder::none) :
            static_cast<u8>(AiEntityWireSemanticOrder::external_unknown);
        row.order_status = static_cast<u8>(AiEntityOrderStatus::none);
        row.engine_order_match = kAiEntityEngineOrderNoRecord;
        row.last_attempt_command = kAiEntityLastAttemptNone;
        row.last_attempt_result = kAiEntityLastAttemptNone;
        row.last_reject_code = static_cast<u16>(AiEntityRejectCode::none);
        row.attackable_class_mask = unit.attackable_class_mask;

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
        f[17] = static_cast<float>(unit.destination_x - unit.x) /
            diagonal_safe;
        f[18] = static_cast<float>(unit.destination_y - unit.y) /
            diagonal_safe;
        f[19] = static_cast<float>(unit.path_target_x - unit.x) /
            diagonal_safe;
        f[20] = static_cast<float>(unit.path_target_y - unit.y) /
            diagonal_safe;
        if (engine_target_row >= 0) {
            const AiEntityTargetRow& target =
                snapshot.targets[static_cast<std::size_t>(engine_target_row)];
            const float dx = static_cast<float>(target.x - unit.x);
            const float dy = static_cast<float>(target.y - unit.y);
            f[21] = dx / diagonal_safe;
            f[22] = dy / diagonal_safe;
            f[23] = clamp01(std::sqrt(dx * dx + dy * dy) / diagonal_safe);
        }
        // f[24..26] issued semantic point, f[27] order age, f[28] issue age,
        // f[29] idle candidates, f[30] progress age: Phase B order store.
        f[31] = clamp01(
            static_cast<float>(unit.level) / kAiEntityNormLevelScale);
        f[32] = clamp01(static_cast<float>(unit.experience) /
            kAiEntityNormExperienceScale);

        // ---- point mask ----
        row.point_mask.fill(0);
        if (input.movement_map != nullptr) {
            const auto it = reachability.find(unit.movement_class);
            if (it != reachability.end()) {
                BuildAiEntityPointMask(*input.movement_map, it->second,
                    unit.x, unit.y, row.point_mask);
            }
        }
        const bool any_point = row.point_mask[0] != 0 ||
            row.point_mask[1] != 0 || row.point_mask[2] != 0;

        // ---- attack pair row ----
        AiEntityPairSource pair_source;
        pair_source.runtime_id = unit.id;
        pair_source.active_owned_alive = true;
        pair_source.has_attack_capability =
            (unit.type_flags & kAttackCapabilityBit) != 0;
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
        bool any_pair = false;
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
                    static_cast<std::size_t>(own_index) * pair_words_per_row +
                    (target_index >> 5)] |= 1u << (target_index & 31u);
            }
        }

        // ---- command mask (plan section 8; strategy is never masked) ----
        u32 command_mask =
            1u << static_cast<u32>(AiEntityCommand::keep_current_order);
        if ((unit.type_flags & kMoveCapabilityBit) != 0 && any_point) {
            command_mask |= 1u << static_cast<u32>(AiEntityCommand::move);
        }
        if ((unit.type_flags & kAttackCapabilityBit) != 0 && any_point) {
            command_mask |=
                1u << static_cast<u32>(AiEntityCommand::attack_move);
        }
        if ((unit.type_flags & kPatrolCapabilityBit) != 0 && any_point) {
            command_mask |= 1u << static_cast<u32>(AiEntityCommand::patrol);
        }
        if (any_pair) {
            command_mask |=
                1u << static_cast<u32>(AiEntityCommand::attack_unit);
        }
        command_mask |=
            1u << static_cast<u32>(AiEntityCommand::hold_position);
        command_mask |= 1u << static_cast<u32>(AiEntityCommand::stop);
        row.command_mask = command_mask;
        row.presence_bits = presence;
        snapshot.own.push_back(row);
    }
    return snapshot;
}

// ---------------------------------------------------------------------------
// act2 wire
// ---------------------------------------------------------------------------

namespace {

struct WireWriter {
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

struct WireReader {
    const u8* data;
    std::size_t length;
    std::size_t offset = 0;

    bool remaining(std::size_t bytes) const {
        return length - offset >= bytes;
    }
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
};

bool wire_error(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

constexpr std::array<u16, 6> kEntityWireVersions = {
    kAiEntityObservationSchemaVersion,
    kAiEntityGlobalFeatureVersion,
    kAiEntityFeatureVersion,
    kAiEntityActionVersion,
    kAiEntitySemanticActionVersion,
    kAiEntityPointGeometryVersion,
};

}  // namespace

u32 AiEntityCrc32(const u8* data, std::size_t length) {
    static u32 table[256];
    static bool table_ready = false;
    if (!table_ready) {
        for (u32 i = 0; i < 256; ++i) {
            u32 value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0 ?
                    (value >> 1) ^ 0xedb88320u : value >> 1;
            }
            table[i] = value;
        }
        table_ready = true;
    }
    u32 crc = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xffu];
    }
    return crc ^ 0xffffffffu;
}

void AiEntityWriteWireHeader(const AiEntityWireHeader& header,
    u8 (&out)[kAiEntityWireHeaderBytes]) {
    std::memset(out, 0, kAiEntityWireHeaderBytes);
    std::vector<u8> bytes;
    bytes.reserve(kAiEntityWireHeaderBytes);
    WireWriter w{&bytes};
    for (char c : kAiEntityWireMagic) {
        w.u8v(static_cast<u8>(c));
    }
    w.u16v(kAiEntityWireHeaderBytes);
    w.u16v(kAiEntityProtocolVersion);
    w.u16v(header.kind);
    w.u16v(header.flags);
    w.u32v(header.payload_bytes);
    for (char c : kAiEntityContractId) {
        w.u8v(static_cast<u8>(c));
    }
    for (u16 version : kEntityWireVersions) {
        w.u16v(version);
    }
    w.u32v(header.owner);
    w.u32v(header.episode);
    w.u32v(header.frame);
    w.u32v(header.sequence);
    w.u32v(header.reply_to_sequence);
    w.u32v(header.own_rows);
    w.u32v(header.target_rows);
    w.u32v(header.global_count);
    w.u32v(header.macro_action_count);
    w.u32v(header.command_count);
    w.u32v(header.point_count);
    w.u32v(header.payload_crc32);
    w.u32v(header.entity_policy_version);
    w.u32v(header.macro_policy_version);
    w.u32v(0);   // reserved
    std::memcpy(out, bytes.data(), kAiEntityWireHeaderBytes);
}

bool AiEntityParseWireHeader(const u8* data, std::size_t length,
    AiEntityWireHeader& out, std::string* error) {
    if (data == nullptr || length < kAiEntityWireHeaderBytes) {
        return wire_error(error, "short header");
    }
    WireReader r{data, length};
    for (char c : kAiEntityWireMagic) {
        if (r.u8v() != static_cast<u8>(c)) {
            return wire_error(error, "bad magic");
        }
    }
    if (r.u16v() != kAiEntityWireHeaderBytes) {
        return wire_error(error, "bad header size");
    }
    if (r.u16v() != kAiEntityProtocolVersion) {
        return wire_error(error, "bad protocol version");
    }
    out.kind = r.u16v();
    if (out.kind < static_cast<u16>(AiEntityWireKind::hello) ||
        out.kind > static_cast<u16>(AiEntityWireKind::error)) {
        return wire_error(error, "unknown frame kind");
    }
    out.flags = r.u16v();
    if ((out.flags & ~(kAiEntityWireFlagMacroDue | kAiEntityWireFlagTerminated |
            kAiEntityWireFlagTruncated)) != 0) {
        return wire_error(error, "undefined flags set");
    }
    if ((out.flags & kAiEntityWireFlagTerminated) != 0 &&
        (out.flags & kAiEntityWireFlagTruncated) != 0) {
        return wire_error(error, "terminated and truncated both set");
    }
    out.payload_bytes = r.u32v();
    if (out.payload_bytes > kAiEntityWireMaxPayloadBytes) {
        return wire_error(error, "payload above limit");
    }
    for (char c : kAiEntityContractId) {
        if (r.u8v() != static_cast<u8>(c)) {
            return wire_error(error, "bad contract id");
        }
    }
    for (u16 version : kEntityWireVersions) {
        if (r.u16v() != version) {
            return wire_error(error, "contract version mismatch");
        }
    }
    out.owner = r.u32v();
    out.episode = r.u32v();
    out.frame = r.u32v();
    out.sequence = r.u32v();
    out.reply_to_sequence = r.u32v();
    out.own_rows = r.u32v();
    out.target_rows = r.u32v();
    out.global_count = r.u32v();
    out.macro_action_count = r.u32v();
    out.command_count = r.u32v();
    out.point_count = r.u32v();
    if (out.global_count != kAiEntityGlobalFeatureCount ||
        out.macro_action_count != kAiEntityMacroActionCount ||
        out.command_count != kAiEntityCommandCount ||
        out.point_count != kAiEntityPointTokenCount) {
        return wire_error(error, "fixed count mismatch");
    }
    if (out.own_rows > kAiEntityWireRowLimit ||
        out.target_rows > kAiEntityWireRowLimit) {
        return wire_error(error, "row count above wire hard limit");
    }
    out.payload_crc32 = r.u32v();
    out.entity_policy_version = r.u32v();
    out.macro_policy_version = r.u32v();
    if (r.u32v() != 0) {
        return wire_error(error, "reserved field nonzero");
    }
    return true;
}

u64 AiEntityActRequestPayloadBytes(u32 own_rows, u32 target_rows,
    bool terminal) {
    if (own_rows > kAiEntityWireRowLimit ||
        target_rows > kAiEntityWireRowLimit) {
        return 0;
    }
    const u64 u = own_rows;
    const u64 e = target_rows;
    const u64 pair_words = (e + 31u) / 32u;
    u64 bytes = 3260u + 207u * u + 76u * e + 4u * u * pair_words;
    if (terminal) {
        bytes += 4u;
    }
    return bytes;
}

namespace {

void encode_act_request_body(WireWriter& w,
    const AiEntityActRequestBody& body) {
    const AiEntitySnapshot& snapshot = body.snapshot;
    for (float value : body.global) {
        w.f32v(value);
    }
    w.f32v(body.macro_gate[0]);
    w.f32v(body.macro_gate[1]);
    for (u32 word : body.macro_mask) {
        w.u32v(word);
    }
    for (u64 value : body.cumulative_losses) {
        w.u64v(value);
    }
    const std::size_t u = snapshot.own.size();
    const std::size_t e = snapshot.targets.size();
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].key.runtime_id);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].key.activation_generation);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].control_epoch);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u16v(snapshot.own[i].type_id);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].movement_class);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].distance_check_mode);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u8v(snapshot.own[i].role);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].render_class);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].command_base_state);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].command_state_high_flags);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].unit_command_flags);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].movement_state);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u8v(snapshot.own[i].semantic_order);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u8v(snapshot.own[i].order_status);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u8v(snapshot.own[i].presence_bits);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u8v(snapshot.own[i].engine_order_match);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u8v(snapshot.own[i].last_attempt_command);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u8v(snapshot.own[i].last_attempt_result);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u16v(snapshot.own[i].last_reject_code);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.i32v(snapshot.own[i].active_target_row);
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].attackable_class_mask);
    }
    for (std::size_t i = 0; i < u; ++i) {
        for (float value : snapshot.own[i].feature) {
            w.f32v(value);
        }
    }
    for (std::size_t i = 0; i < u; ++i) {
        w.u32v(snapshot.own[i].command_mask);
    }
    for (std::size_t i = 0; i < u; ++i) {
        for (u32 word : snapshot.own[i].point_mask) {
            w.u32v(word);
        }
    }
    for (std::size_t i = 0; i < e; ++i) {
        w.u32v(snapshot.targets[i].key.runtime_id);
    }
    for (std::size_t i = 0; i < e; ++i) {
        w.u32v(snapshot.targets[i].key.activation_generation);
    }
    for (std::size_t i = 0; i < e; ++i) {
        w.u16v(snapshot.targets[i].type_id);
    }
    for (std::size_t i = 0; i < e; ++i) {
        w.u8v(snapshot.targets[i].owner_id);
    }
    for (std::size_t i = 0; i < e; ++i) {
        w.u8v(snapshot.targets[i].role);
    }
    for (std::size_t i = 0; i < e; ++i) {
        w.u32v(snapshot.targets[i].render_class);
    }
    for (std::size_t i = 0; i < e; ++i) {
        w.u32v(snapshot.targets[i].kind_bits);
    }
    for (std::size_t i = 0; i < e; ++i) {
        for (float value : snapshot.targets[i].feature) {
            w.f32v(value);
        }
    }
    for (u32 word : snapshot.attack_pair_mask) {
        w.u32v(word);
    }
}

}  // namespace

std::vector<u8> EncodeAiEntityActRequestPayload(
    const AiEntityActRequestBody& body) {
    std::vector<u8> bytes;
    const u64 expected = AiEntityActRequestPayloadBytes(
        static_cast<u32>(body.snapshot.own.size()),
        static_cast<u32>(body.snapshot.targets.size()), false);
    if (expected == 0 && !body.snapshot.own.empty()) {
        return bytes;
    }
    bytes.reserve(static_cast<std::size_t>(expected));
    WireWriter w{&bytes};
    encode_act_request_body(w, body);
    return bytes;
}

std::vector<u8> EncodeAiEntityTerminalPayload(
    const AiEntityActRequestBody& body, u32 terminal_outcome) {
    std::vector<u8> bytes;
    const u64 expected = AiEntityActRequestPayloadBytes(
        static_cast<u32>(body.snapshot.own.size()),
        static_cast<u32>(body.snapshot.targets.size()), true);
    bytes.reserve(static_cast<std::size_t>(expected));
    WireWriter w{&bytes};
    w.u32v(terminal_outcome);
    encode_act_request_body(w, body);
    return bytes;
}

std::vector<u8> EncodeAiEntityReplyPayload(const AiEntityReplyBody& body) {
    std::vector<u8> bytes;
    bytes.reserve(8 + body.command.size() * 9);
    WireWriter w{&bytes};
    w.i32v(body.macro);
    w.i32v(body.macro_target);
    for (u8 command : body.command) {
        w.u8v(command);
    }
    for (i32 point : body.point) {
        w.i32v(point);
    }
    for (i32 target : body.target) {
        w.i32v(target);
    }
    return bytes;
}

bool DecodeAiEntityReplyPayload(const u8* data, std::size_t length,
    u32 own_rows, AiEntityReplyBody& out, std::string* error) {
    const std::size_t expected = 8u + static_cast<std::size_t>(own_rows) * 9u;
    if (data == nullptr || length != expected) {
        return wire_error(error, "reply payload size mismatch");
    }
    WireReader r{data, length};
    out.macro = r.i32v();
    out.macro_target = r.i32v();
    if (out.macro < 0 ||
        out.macro >= static_cast<i32>(kAiEntityMacroActionCount)) {
        return wire_error(error, "macro action out of range");
    }
    if (out.macro_target < -1 || out.macro_target >= 64) {
        return wire_error(error, "macro target out of range");
    }
    out.command.resize(own_rows);
    out.point.assign(own_rows, -1);
    out.target.assign(own_rows, -1);
    for (u32 i = 0; i < own_rows; ++i) {
        out.command[i] = r.u8v();
        if (out.command[i] >= kAiEntityCommandCount) {
            return wire_error(error, "entity command out of range");
        }
    }
    for (u32 i = 0; i < own_rows; ++i) {
        out.point[i] = r.i32v();
        if (out.point[i] < -1 ||
            out.point[i] >= static_cast<i32>(kAiEntityPointTokenCount)) {
            return wire_error(error, "point token out of range");
        }
    }
    for (u32 i = 0; i < own_rows; ++i) {
        out.target[i] = r.i32v();
        if (out.target[i] < -1 ||
            out.target[i] >= static_cast<i32>(kAiEntityWireRowLimit)) {
            return wire_error(error, "target row out of range");
        }
    }
    return true;
}

std::vector<u8> EncodeAiEntityOutcomePayload(const AiEntityOutcomeBody& body) {
    std::vector<u8> bytes;
    WireWriter w{&bytes};
    w.u16v(body.macro_result);
    w.u16v(body.macro_reject_code);
    w.u8v(body.macro_trainable);
    w.u8v(0);
    w.u8v(0);
    w.u8v(0);
    for (u16 result : body.entity_result) {
        w.u16v(result);
    }
    for (u16 code : body.entity_reject_code) {
        w.u16v(code);
    }
    for (u32 word : body.trainable_mask) {
        w.u32v(word);
    }
    return bytes;
}

bool DecodeAiEntityOutcomePayload(const u8* data, std::size_t length,
    u32 own_rows, AiEntityOutcomeBody& out, std::string* error) {
    const std::size_t mask_words = (static_cast<std::size_t>(own_rows) + 31u) /
        32u;
    const std::size_t expected = 8u +
        static_cast<std::size_t>(own_rows) * 4u + mask_words * 4u;
    if (data == nullptr || length != expected) {
        return wire_error(error, "outcome payload size mismatch");
    }
    WireReader r{data, length};
    out.macro_result = r.u16v();
    out.macro_reject_code = r.u16v();
    out.macro_trainable = r.u8v();
    if (r.u8v() != 0 || r.u8v() != 0 || r.u8v() != 0) {
        return wire_error(error, "outcome reserved bytes nonzero");
    }
    out.entity_result.resize(own_rows);
    out.entity_reject_code.resize(own_rows);
    out.trainable_mask.resize(mask_words);
    for (u32 i = 0; i < own_rows; ++i) {
        out.entity_result[i] = r.u16v();
    }
    for (u32 i = 0; i < own_rows; ++i) {
        out.entity_reject_code[i] = r.u16v();
    }
    for (std::size_t i = 0; i < mask_words; ++i) {
        out.trainable_mask[i] = r.u32v();
    }
    return true;
}

std::vector<u8> EncodeAiEntityHelloPayload(const AiEntityHelloBody& body) {
    std::vector<u8> bytes;
    bytes.reserve(16 + body.owners.size() * 48);
    WireWriter w{&bytes};
    w.u32v(body.max_payload_bytes);
    w.u32v(body.reply_timeout_ms);
    w.u32v(body.run_mode);
    w.u32v(body.controlled_owner_mask);
    for (const AiEntityHelloOwnerRecord& record : body.owners) {
        w.u32v(record.owner);
        w.u32v(record.frozen_hostile_owner_mask);
        w.u32v(record.requested_entity_version);
        w.u32v(record.requested_macro_version);
        for (u8 byte : record.requested_checkpoint_sha256) {
            w.u8v(byte);
        }
    }
    return bytes;
}

bool DecodeAiEntityHelloPayload(const u8* data, std::size_t length,
    AiEntityHelloBody& out, std::string* error) {
    if (data == nullptr || length < 16 || (length - 16) % 48 != 0) {
        return wire_error(error, "hello payload size mismatch");
    }
    WireReader r{data, length};
    out.max_payload_bytes = r.u32v();
    out.reply_timeout_ms = r.u32v();
    out.run_mode = r.u32v();
    out.controlled_owner_mask = r.u32v();
    if (out.run_mode > 1) {
        return wire_error(error, "hello run mode out of range");
    }
    const std::size_t record_count = (length - 16) / 48;
    u32 mask_owner_count = 0;
    for (u32 mask = out.controlled_owner_mask; mask != 0; mask &= mask - 1) {
        ++mask_owner_count;
    }
    if (record_count != mask_owner_count) {
        return wire_error(error, "hello owner record count mismatch");
    }
    out.owners.clear();
    out.owners.reserve(record_count);
    i64 previous_owner = -1;
    for (std::size_t i = 0; i < record_count; ++i) {
        AiEntityHelloOwnerRecord record;
        record.owner = r.u32v();
        record.frozen_hostile_owner_mask = r.u32v();
        record.requested_entity_version = r.u32v();
        record.requested_macro_version = r.u32v();
        for (u8& byte : record.requested_checkpoint_sha256) {
            byte = r.u8v();
        }
        if (static_cast<i64>(record.owner) <= previous_owner) {
            return wire_error(error, "hello owner records not ascending");
        }
        if (record.owner >= 32 ||
            (out.controlled_owner_mask & (1u << record.owner)) == 0) {
            return wire_error(error, "hello owner not in controlled mask");
        }
        previous_owner = record.owner;
        out.owners.push_back(record);
    }
    return true;
}

std::vector<u8> EncodeAiEntityErrorPayload(u16 code,
    const std::string& message) {
    std::vector<u8> bytes;
    const std::size_t clamped = std::min<std::size_t>(message.size(), 1024);
    WireWriter w{&bytes};
    w.u16v(code);
    w.u16v(static_cast<u16>(clamped));
    for (std::size_t i = 0; i < clamped; ++i) {
        w.u8v(static_cast<u8>(message[i]));
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// Direct order latch (§9 state machine)
// ---------------------------------------------------------------------------

namespace {

i64 squared_px_distance(i32 ax, i32 ay, i32 bx, i32 by) {
    const i64 dx = static_cast<i64>(ax) - bx;
    const i64 dy = static_cast<i64>(ay) - by;
    return dx * dx + dy * dy;
}

bool order_command_is_point(u8 command) {
    return command == static_cast<u8>(AiEntityCommand::move) ||
        command == static_cast<u8>(AiEntityCommand::attack_move) ||
        command == static_cast<u8>(AiEntityCommand::patrol);
}

bool patrol_family_state(u32 base_state) {
    return base_state >= 0x35u && base_state <= 0x3au;
}

bool patrol_translation_leg(u32 base_state) {
    return base_state == 0x37u || base_state == 0x38u;
}

bool patrol_combat_state(u32 base_state) {
    return base_state == 0x39u || base_state == 0x3au;
}

}  // namespace

u64 AiEntityPackKey(const AiEntityKey& key) {
    return (static_cast<u64>(key.runtime_id) << 32) |
        key.activation_generation;
}

bool AiEntityOrderTrackFrame(AiEntityActiveOrder& order,
    const AiEntityOrderFrameView& view, u32 frame) {
    // Outer gate (§9): invalid source purges the record; an invalid
    // AWAITING/ACTIVE attack target closes it as TARGET_LOST (old key kept
    // for the next observation).
    if (!view.source_alive_active || !view.control_epoch_matches) {
        return false;
    }
    const bool tracking = order.status == AiEntityOrderStatus::awaiting_apply ||
        order.status == AiEntityOrderStatus::active;
    if (tracking &&
        order.command == static_cast<u8>(AiEntityCommand::attack_unit) &&
        !view.target_valid) {
        order.status = AiEntityOrderStatus::target_lost;
        return true;
    }
    if (order.status == AiEntityOrderStatus::awaiting_apply) {
        // Only exact-origin ACK handling and the delivery/absolute timeouts
        // run here — never completion/mismatch/idle/stall judgments.
        if (view.acknowledged_matching) {
            order.status = AiEntityOrderStatus::active;
            order.applied_frame = frame;
            order.idle_candidate_frames = 0;
            order.last_progress_x = view.unit_x;
            order.last_progress_y = view.unit_y;
            order.last_progress_frame = frame;
            // ACTIVE rules start on the NEXT simulation frame.
            return true;
        }
        if (view.delivery_origin_seen &&
            order.delivery_seen_frame == 0xffffffffu) {
            order.delivery_seen_frame = frame;
        }
        // While AWAITING, idle_candidate_frames counts consecutive frames of
        // the delivery-escape condition (completion timers are all frozen,
        // so the field is otherwise unused here).
        const bool escape_condition = view.origin_replaced ||
            (view.consumer_passed_sequence && !view.delivery_origin_seen);
        if (escape_condition) {
            order.idle_candidate_frames += 1;
        } else {
            order.idle_candidate_frames = 0;
        }
        if (order.idle_candidate_frames >= kAiEntityAwaitDeliveryFrames ||
            frame - order.issued_frame >= kAiEntityAwaitAbsoluteFrames) {
            order.status = AiEntityOrderStatus::interrupted;
            order.idle_candidate_frames = 0;
        }
        return true;
    }
    if (order.status != AiEntityOrderStatus::active) {
        // Terminal states never auto-resume; only a new policy ISSUE does.
        return true;
    }
    // ACTIVE rules run only from the frame AFTER the matching ACK.
    if (frame <= order.applied_frame) {
        return true;
    }
    const u8 command = order.command;
    // 1. COMPLETED (highest priority).
    if (command == static_cast<u8>(AiEntityCommand::move) ||
        command == static_cast<u8>(AiEntityCommand::attack_move)) {
        if (view.idle && squared_px_distance(view.unit_x, view.unit_y,
                order.target_x, order.target_y) <=
                static_cast<i64>(kAiEntityCompletionRadiusPx) *
                kAiEntityCompletionRadiusPx) {
            order.status = AiEntityOrderStatus::completed;
            return true;
        }
    } else if (command == static_cast<u8>(AiEntityCommand::stop)) {
        if (view.idle) {
            order.status = AiEntityOrderStatus::completed;
            return true;
        }
    } else if (command == static_cast<u8>(AiEntityCommand::patrol)) {
        // Immediate idle/pop when the requested point equals the current
        // position (the engine popped the order instantly).
        if (view.idle && !patrol_family_state(view.command_base_state) &&
            view.unit_x == order.target_x && view.unit_y == order.target_y) {
            order.status = AiEntityOrderStatus::completed;
            return true;
        }
    }
    // 2. Canonical payload state.
    if (view.engine_order_match == kAiEntityEngineOrderDifferent) {
        // External override interrupts every command (HOLD/STOP included).
        order.status = AiEntityOrderStatus::interrupted;
        return true;
    }
    bool idle_candidate = false;
    if (view.engine_order_match == kAiEntityEngineOrderCleared) {
        // STOP already completed above; the rest escape after 4 idle frames.
        idle_candidate = view.idle;
    } else if (view.engine_order_match == kAiEntityEngineOrderMatch) {
        // 3. Command-specific MATCH rules.
        if (command == static_cast<u8>(AiEntityCommand::move)) {
            idle_candidate = view.idle;
        } else if (command == static_cast<u8>(AiEntityCommand::attack_move)) {
            idle_candidate = view.idle && view.attack_recovery == 0 &&
                !view.engine_target_valid_in_range;
        } else if (command == static_cast<u8>(AiEntityCommand::attack_unit)) {
            idle_candidate = view.target_valid &&
                !view.engine_target_matches && view.idle &&
                view.attack_recovery == 0;
        } else if (command == static_cast<u8>(AiEntityCommand::patrol)) {
            idle_candidate = !patrol_family_state(view.command_base_state) &&
                view.idle;
        }
        // HOLD holds; STOP closed by completion.
    }
    if (idle_candidate) {
        order.idle_candidate_frames += 1;
        if (order.idle_candidate_frames >= kAiEntityIdleInterruptFrames) {
            if (command == static_cast<u8>(AiEntityCommand::patrol) &&
                view.engine_order_match == kAiEntityEngineOrderMatch &&
                view.unit_x == order.target_x &&
                view.unit_y == order.target_y) {
                order.status = AiEntityOrderStatus::completed;
            } else {
                order.status = AiEntityOrderStatus::interrupted;
            }
            order.idle_candidate_frames = 0;
            return true;
        }
    } else {
        // Strictly consecutive frames only.
        order.idle_candidate_frames = 0;
    }
    // 4. STALLED: only where translation progress is genuinely required.
    bool progress_required = false;
    if (command == static_cast<u8>(AiEntityCommand::move)) {
        progress_required = view.attack_recovery == 0;
    } else if (command == static_cast<u8>(AiEntityCommand::attack_move)) {
        progress_required = view.attack_recovery == 0 &&
            !view.engine_target_valid_in_range;
    } else if (command == static_cast<u8>(AiEntityCommand::attack_unit)) {
        progress_required = view.attack_recovery == 0 &&
            view.target_valid && view.target_out_of_reach;
    } else if (command == static_cast<u8>(AiEntityCommand::patrol)) {
        progress_required = view.attack_recovery == 0 &&
            patrol_translation_leg(view.command_base_state) &&
            !patrol_combat_state(view.command_base_state);
    }
    if (!progress_required) {
        // Freeze frames advance the baseline so frozen time never leaks
        // into a later 48-frame difference.
        order.last_progress_x = view.unit_x;
        order.last_progress_y = view.unit_y;
        order.last_progress_frame = frame;
        return true;
    }
    if (squared_px_distance(view.unit_x, view.unit_y, order.last_progress_x,
            order.last_progress_y) >=
        static_cast<i64>(kAiEntityProgressEpsilonPx) *
            kAiEntityProgressEpsilonPx) {
        order.last_progress_x = view.unit_x;
        order.last_progress_y = view.unit_y;
        order.last_progress_frame = frame;
        return true;
    }
    if (frame - order.last_progress_frame >= kAiEntityStallFrames) {
        order.status = AiEntityOrderStatus::stalled;
    }
    return true;
}

AiEntityDecisionRowOutcome AiEntityEvaluateDecisionRow(
    const AiEntityActiveOrder* order, const AiEntityDecisionRowInput& input) {
    AiEntityDecisionRowOutcome outcome;
    if (input.command ==
        static_cast<u8>(AiEntityCommand::keep_current_order)) {
        outcome.result = AiEntityAttemptResult::kept;
        return outcome;
    }
    if (order != nullptr && order->command == input.command) {
        bool same_payload = true;
        if (input.command == static_cast<u8>(AiEntityCommand::attack_unit)) {
            same_payload = order->target == input.target;
        } else if (order_command_is_point(input.command)) {
            same_payload = order->target_x == input.point_x &&
                order->target_y == input.point_y;
        }
        if (same_payload) {
            switch (order->status) {
            case AiEntityOrderStatus::awaiting_apply:
            case AiEntityOrderStatus::active:
                outcome.result = AiEntityAttemptResult::deduped;
                return outcome;
            case AiEntityOrderStatus::completed:
                // A terminal record still suppresses the same ISSUE while
                // the state already satisfies it (§9 "같은 ISSUE").
                if (input.command == static_cast<u8>(AiEntityCommand::stop) &&
                    input.unit_idle) {
                    outcome.result = AiEntityAttemptResult::deduped;
                    return outcome;
                }
                if ((input.command ==
                        static_cast<u8>(AiEntityCommand::move) ||
                     input.command ==
                        static_cast<u8>(AiEntityCommand::attack_move)) &&
                    squared_px_distance(input.unit_x, input.unit_y,
                        input.point_x, input.point_y) <=
                        static_cast<i64>(kAiEntityCompletionRadiusPx) *
                            kAiEntityCompletionRadiusPx) {
                    outcome.result = AiEntityAttemptResult::deduped;
                    return outcome;
                }
                break;
            case AiEntityOrderStatus::target_lost:
            case AiEntityOrderStatus::stalled:
            case AiEntityOrderStatus::interrupted:
                // A same ISSUE is a fresh policy choice here.
                break;
            default:
                break;
            }
        }
    }
    outcome.result = AiEntityAttemptResult::published;
    outcome.needs_packet = true;
    return outcome;
}

// ---------------------------------------------------------------------------
// Shadow teacher labels
// ---------------------------------------------------------------------------

namespace {

u64 pack_entity_key(const AiEntityKey& key) {
    return AiEntityPackKey(key);
}

// External command index of a teacher order kind; 0xff = unsupported in v1.
u8 shadow_command_of(AiSemanticActionKind kind) {
    switch (kind) {
    case AiSemanticActionKind::move:
        return static_cast<u8>(AiEntityCommand::move);
    case AiSemanticActionKind::attack_move:
        return static_cast<u8>(AiEntityCommand::attack_move);
    case AiSemanticActionKind::patrol:
        return static_cast<u8>(AiEntityCommand::patrol);
    case AiSemanticActionKind::attack_unit:
        return static_cast<u8>(AiEntityCommand::attack_unit);
    case AiSemanticActionKind::hold_position:
        return static_cast<u8>(AiEntityCommand::hold_position);
    case AiSemanticActionKind::stop:
        return static_cast<u8>(AiEntityCommand::stop);
    default:
        return 0xff;
    }
}

bool shadow_command_is_point(u8 command) {
    return command == static_cast<u8>(AiEntityCommand::move) ||
        command == static_cast<u8>(AiEntityCommand::attack_move) ||
        command == static_cast<u8>(AiEntityCommand::patrol);
}

}  // namespace

std::vector<AiEntityShadowLabel> BuildAiEntityShadowLabels(
    const AiEntitySnapshot& snapshot, const UnitMovementMap* movement_map,
    const std::vector<AiEntityShadowDesiredOrder>& desired,
    AiEntityShadowState& state, u32 max_point_error_px) {
    std::vector<AiEntityShadowLabel> labels(snapshot.own.size());
    std::unordered_map<u32, const AiEntityShadowDesiredOrder*> desired_by_id;
    desired_by_id.reserve(desired.size());
    for (const AiEntityShadowDesiredOrder& order : desired) {
        desired_by_id[order.unit_id] = &order;
    }
    std::unordered_map<u32, i32> target_row_by_id;
    for (std::size_t i = 0; i < snapshot.targets.size(); ++i) {
        target_row_by_id[snapshot.targets[i].key.runtime_id] =
            static_cast<i32>(i);
    }
    std::unordered_map<u32, AiEntityReachability> reachability;
    const u32 pair_words = (static_cast<u32>(snapshot.targets.size()) + 31u) /
        32u;

    for (std::size_t index = 0; index < snapshot.own.size(); ++index) {
        const AiEntityOwnRow& row = snapshot.own[index];
        AiEntityShadowLabel& label = labels[index];
        const u64 latch_key = pack_entity_key(row.key);
        const auto desired_it = desired_by_id.find(row.key.runtime_id);
        if (desired_it == desired_by_id.end()) {
            label.label = kAiEntityShadowKeep;   // teacher keeps the order
            continue;
        }
        const AiEntityShadowDesiredOrder& order = *desired_it->second;
        const u8 command = shadow_command_of(order.kind);
        auto exclude = [&label](AiEntityShadowExcludeReason reason) {
            label.label = kAiEntityShadowExcluded;
            label.exclude_reason = static_cast<u16>(reason);
        };
        if (command == 0xff) {
            exclude(AiEntityShadowExcludeReason::unsupported_kind);
            continue;
        }
        if ((row.command_mask & (1u << command)) == 0) {
            exclude(AiEntityShadowExcludeReason::masked);
            continue;
        }
        AiEntityShadowLatch next_latch;
        next_latch.valid = true;
        next_latch.command = command;
        if (command == static_cast<u8>(AiEntityCommand::attack_unit)) {
            const auto target_it = target_row_by_id.find(order.target_id);
            if (target_it == target_row_by_id.end()) {
                exclude(AiEntityShadowExcludeReason::stale_target);
                continue;
            }
            const i32 target_row = target_it->second;
            const u32 word = static_cast<u32>(index) * pair_words +
                (static_cast<u32>(target_row) >> 5);
            if (word >= snapshot.attack_pair_mask.size() ||
                (snapshot.attack_pair_mask[word] >>
                    (static_cast<u32>(target_row) & 31u) & 1u) == 0) {
                exclude(AiEntityShadowExcludeReason::masked);
                continue;
            }
            label.target = target_row;
            next_latch.target =
                snapshot.targets[static_cast<std::size_t>(target_row)].key;
        } else if (shadow_command_is_point(command)) {
            if (movement_map == nullptr) {
                exclude(AiEntityShadowExcludeReason::point_error);
                continue;
            }
            auto reach_it = reachability.find(row.movement_class);
            if (reach_it == reachability.end()) {
                reach_it = reachability.emplace(row.movement_class,
                    BuildAiEntityReachability(*movement_map,
                        row.movement_class)).first;
            }
            // Nearest mask-legal token to the teacher's world point,
            // re-evaluated through the same resolver (plan 13.1 step 4).
            i32 best_token = -1;
            i64 best_distance = 0;
            i32 best_x = 0;
            i32 best_y = 0;
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
                    best_x = point.x;
                    best_y = point.y;
                }
            }
            const i64 max_error = static_cast<i64>(max_point_error_px) *
                max_point_error_px;
            if (best_token < 0 || best_distance > max_error) {
                exclude(AiEntityShadowExcludeReason::point_error);
                continue;
            }
            label.point = best_token;
            next_latch.x = best_x;
            next_latch.y = best_y;
        }
        // KEEP vs ISSUE against the shadow's own 8-frame latch, not the
        // executor's every-frame record (plan 13.1 step 2).
        const auto latch_it = state.latches.find(latch_key);
        const AiEntityShadowLatch* latch = latch_it != state.latches.end() ?
            &latch_it->second : nullptr;
        bool same = latch != nullptr && latch->valid &&
            latch->command == command;
        if (same && command == static_cast<u8>(AiEntityCommand::attack_unit)) {
            same = latch->target == next_latch.target;
        } else if (same && shadow_command_is_point(command)) {
            same = latch->x == next_latch.x && latch->y == next_latch.y;
        }
        if (same) {
            label.label = kAiEntityShadowKeep;
            label.point = -1;
            label.target = -1;
        } else {
            label.label = kAiEntityShadowIssue;
            label.command = command;
            state.latches[latch_key] = next_latch;
        }
    }
    return labels;
}

std::vector<u8> EncodeAiEntityShadowRecord(const AiEntityWireHeader& header,
    const std::vector<u8>& payload,
    const std::vector<AiEntityShadowLabel>& labels) {
    std::vector<u8> record;
    const std::size_t body_bytes = kAiEntityWireHeaderBytes + payload.size() +
        4 + labels.size() * 16;
    record.reserve(8 + body_bytes);
    WireWriter w{&record};
    for (char c : kAiEntityShadowRecordMagic) {
        w.u8v(static_cast<u8>(c));
    }
    w.u32v(static_cast<u32>(body_bytes));
    u8 header_bytes[kAiEntityWireHeaderBytes];
    AiEntityWriteWireHeader(header, header_bytes);
    record.insert(record.end(), header_bytes,
        header_bytes + kAiEntityWireHeaderBytes);
    record.insert(record.end(), payload.begin(), payload.end());
    w.u32v(static_cast<u32>(labels.size()));
    for (const AiEntityShadowLabel& label : labels) {
        w.u8v(label.label);
        w.u8v(label.command);
        w.u16v(label.exclude_reason);
        w.i32v(label.point);
        w.i32v(label.target);
        w.f32v(label.inclusion_probability);
    }
    return record;
}

AiEntityWireSemanticOrder AiEntityWireSemanticOrderOf(
    AiSemanticActionKind kind) {
    switch (kind) {
    case AiSemanticActionKind::move:
        return AiEntityWireSemanticOrder::move;
    case AiSemanticActionKind::attack_move:
        return AiEntityWireSemanticOrder::attack_move;
    case AiSemanticActionKind::patrol:
        return AiEntityWireSemanticOrder::patrol;
    case AiSemanticActionKind::attack_unit:
        return AiEntityWireSemanticOrder::attack_unit;
    case AiSemanticActionKind::hold_position:
        return AiEntityWireSemanticOrder::hold;
    case AiSemanticActionKind::stop:
        return AiEntityWireSemanticOrder::stop;
    default:
        return AiEntityWireSemanticOrder::external_unknown;
    }
}

}  // namespace ranker
