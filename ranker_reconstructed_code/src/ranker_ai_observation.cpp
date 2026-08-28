#include "ranker_ai_observation.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <unordered_set>

namespace ranker {
namespace {

constexpr u32 kAiStaticTerrainMask =
    kMapCellTerrainMask | kMapCellBlockedTerrain;

bool unit_sort_less(const UnitMovementUnit* left,
    const UnitMovementUnit* right) {
    const bool left_has_slot =
        left->runtime_slot_index != kInvalidUnitRuntimeSlotIndex;
    const bool right_has_slot =
        right->runtime_slot_index != kInvalidUnitRuntimeSlotIndex;
    if (left_has_slot != right_has_slot) {
        return left_has_slot;
    }
    if (left->runtime_slot_index != right->runtime_slot_index) {
        return left->runtime_slot_index < right->runtime_slot_index;
    }
    if (left->id != right->id) {
        return left->id < right->id;
    }
    if (left->owner_id != right->owner_id) {
        return left->owner_id < right->owner_id;
    }
    return left->type_id < right->type_id;
}

bool is_observable_unit(const AiObservationBuildInput& input,
    const UnitMovementUnit& unit) {
    if (unit.owner_id == input.local_owner) {
        return true;
    }
    return input.unit_visible != nullptr && input.unit_visible(
        unit, input.local_owner, input.unit_visibility_user_data);
}

void hash_byte(u64& hash, u8 value) {
    constexpr u64 kFnvPrime = 1099511628211ull;
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u32(u64& hash, u32 value) {
    for (u32 shift = 0; shift < 32; shift += 8) {
        hash_byte(hash, static_cast<u8>((value >> shift) & 0xffu));
    }
}

void hash_bool(u64& hash, bool value) {
    hash_byte(hash, value ? 1u : 0u);
}

void hash_string(u64& hash, const std::string& value) {
    hash_u32(hash, static_cast<u32>(value.size()));
    for (const unsigned char byte : value) {
        hash_byte(hash, byte);
    }
}

} // namespace

AiObservationBuildResult BuildAiObservationV1(
    const AiObservationBuildInput& input) {
    AiObservationBuildResult result{};
    if (input.players == nullptr) {
        result.code = AiObservationBuildCode::missing_players;
        return result;
    }
    if (input.movement == nullptr) {
        result.code = AiObservationBuildCode::missing_movement;
        return result;
    }
    if (input.local_owner >= kPlayerSlotCount) {
        result.code = AiObservationBuildCode::invalid_local_owner;
        return result;
    }

    const UnitMovementMap& map = input.movement->map;
    if (map.width == 0 || map.height == 0 ||
        map.width > std::numeric_limits<std::size_t>::max() / map.height) {
        result.code = AiObservationBuildCode::invalid_map_dimensions;
        return result;
    }
    const std::size_t tile_count =
        static_cast<std::size_t>(map.width) * map.height;
    const u32 stride = map.stride_tiles != 0 ? map.stride_tiles : map.width;
    if (stride < map.width || map.height >
            std::numeric_limits<std::size_t>::max() / stride ||
        map.cells.size() < static_cast<std::size_t>(stride) * map.height) {
        result.code = AiObservationBuildCode::invalid_map_storage;
        return result;
    }
    if (input.explored_tiles == nullptr) {
        result.code = AiObservationBuildCode::missing_explored_tiles;
        return result;
    }
    if (input.visible_tiles == nullptr) {
        result.code = AiObservationBuildCode::missing_visible_tiles;
        return result;
    }
    if (input.explored_tiles->size() != tile_count) {
        result.code = AiObservationBuildCode::invalid_explored_tile_count;
        return result;
    }
    if (input.visible_tiles->size() != tile_count) {
        result.code = AiObservationBuildCode::invalid_visible_tile_count;
        return result;
    }

    AiObservation& observation = result.observation;
    observation.schema_version = kAiObservationSchemaVersion;
    observation.simulation_frame = input.simulation_frame;
    observation.map_relative_path = input.map_relative_path;
    observation.map_sha256 = input.map_sha256;
    observation.map_width_tiles = map.width;
    observation.map_height_tiles = map.height;
    observation.local_owner = input.local_owner;
    observation.local_faction = input.local_faction;
    observation.active_owner_mask = input.players->global_active_slot_mask;
    observation.local_relation_mask =
        input.players->owner_relation_masks[input.local_owner];
    observation.primary_resources =
        input.players->owner_primary_resources[input.local_owner];
    observation.secondary_resources =
        input.players->owner_secondary_resources[input.local_owner];
    observation.auxiliary_resources =
        input.players->owner_aux_resources[input.local_owner];
    observation.population_used = input.population_used;
    observation.population_reserved = input.population_reserved;
    observation.population_limit = input.population_limit;
    // The runtime's owner_start_x table is MAP-SLOT-ordered; once the start
    // shuffle decouples owners from map slots, indexing it by owner id points
    // at another slot's base.  Prefer the caller-supplied per-owner start
    // (from the startup owner slots) and fall back for legacy callers.
    if ((input.competitor_start_mask & (1u << input.local_owner)) != 0) {
        observation.start_x = input.owner_start_x[input.local_owner];
        observation.start_y = input.owner_start_y[input.local_owner];
    } else {
        observation.start_x = input.players->owner_start_x[input.local_owner];
        observation.start_y = input.players->owner_start_y[input.local_owner];
    }
    observation.game_ended = input.game_ended;
    observation.game_end_reason = input.game_end_reason;
    observation.research_levels = input.research_levels;
    observation.research_order_levels = input.research_order_levels;
    observation.owner_start_x = input.owner_start_x;
    observation.owner_start_y = input.owner_start_y;
    observation.competitor_start_mask = input.competitor_start_mask;

    observation.tiles.reserve(tile_count);
    for (u32 y = 0; y < map.height; ++y) {
        for (u32 x = 0; x < map.width; ++x) {
            const std::size_t compact_index =
                static_cast<std::size_t>(y) * map.width + x;
            const std::size_t map_index =
                static_cast<std::size_t>(y) * stride + x;
            const UnitMovementCell& source = map.cells[map_index];
            const bool visible = (*input.visible_tiles)[compact_index] != 0;
            const bool explored =
                (*input.explored_tiles)[compact_index] != 0 || visible;
            // Harvestable-terrain amounts are revealed for any explored tile,
            // not only currently-visible ones.  The engine only maintains the
            // "currently lit" fog grid for the local viewing player, so an
            // AI-controlled owner has an empty current-visibility layer and
            // would otherwise never see a single resource tile.  Exposing the
            // last-known amount on explored terrain is legitimate AI memory and
            // does not leak an opponent's private unit/production state.
            observation.tiles.push_back(AiObservedMapTile{
                source.flags & kAiStaticTerrainMask,
                explored ?
                    (source.flags & kMapCellHarvestAmountMask) >>
                        kMapCellHarvestAmountShift :
                    0u,
                (source.flags & kMapCellTerrainMask) ==
                        kMapCellPassableTerrain &&
                    (source.flags & kMapCellBlockedTerrain) == 0,
                explored,
                visible,
            });
        }
    }

    std::vector<const UnitMovementUnit*> observable_units;
    observable_units.reserve(input.movement->active_units.size());
    for (const UnitMovementUnit* unit : input.movement->active_units) {
        if (unit == nullptr || !unit->active ||
            !is_observable_unit(input, *unit)) {
            continue;
        }
        observable_units.push_back(unit);
    }
    std::sort(observable_units.begin(), observable_units.end(), unit_sort_less);

    std::unordered_set<u32> observable_ids;
    observable_ids.reserve(observable_units.size());
    for (const UnitMovementUnit* unit : observable_units) {
        observable_ids.insert(unit->id);
    }

    observation.units.reserve(observable_units.size());
    for (const UnitMovementUnit* unit : observable_units) {
        const bool controlled = unit->owner_id == input.local_owner;
        AiObservedUnit observed{};
        observed.id = unit->id;
        observed.runtime_slot_index = unit->runtime_slot_index;
        observed.type_id = unit->type_id;
        observed.owner_id = unit->owner_id;
        observed.type_flags = unit->type_flags;
        observed.x = unit->x;
        observed.y = unit->y;
        observed.health = unit->health;
        observed.max_health = unit->max_health;
        observed.secondary_value = unit->secondary_value;
        observed.max_secondary_value = unit->max_secondary_value;
        observed.controlled = controlled;
        observed.visible = true;
        observed.alive = (unit->command_state & kUnitCommandDead) == 0;
        observed.under_construction = unit->under_construction;

        if (controlled) {
            observed.command_state = unit->command_state;
            observed.command_flags = unit->command_flags;
            observed.command_value = unit->command_value;
            observed.action_mode = unit->action_mode;
            observed.movement_state = unit->movement_state;
            observed.destination_x = unit->destination_x;
            observed.destination_y = unit->destination_y;
            observed.path_target_x = unit->path_target_x;
            observed.path_target_y = unit->path_target_y;
            if (unit->target != nullptr &&
                observable_ids.find(unit->target->id) != observable_ids.end()) {
                observed.target_id = unit->target->id;
            }
            observed.cargo_amount = unit->cargo_amount;
            observed.cargo_capacity = unit->cargo_capacity;
            observed.queued_production_type_id =
                unit->queued_production_type_id;
            observed.production_variant = unit->production_variant;
            observed.deferred_command_count = std::min<u32>(
                unit->deferred_command_count,
                static_cast<u32>(unit->deferred_commands.size()));
            observed.deferred_commands.reserve(
                observed.deferred_command_count);
            for (u32 index = 0; index <
                    observed.deferred_command_count; ++index) {
                const UnitQueuedCommand& command =
                    unit->deferred_commands[index];
                observed.deferred_commands.push_back(
                    AiObservedQueuedCommand{
                        command.state, command.x, command.y, command.value});
            }
        }
        observation.units.push_back(observed);
    }

    result.code = AiObservationBuildCode::okay;
    return result;
}

u64 HashAiObservationV1(const AiObservation& observation) {
    u64 hash = 14695981039346656037ull;
    hash_u32(hash, observation.schema_version);
    hash_u32(hash, observation.simulation_frame);
    hash_string(hash, observation.map_relative_path);
    hash_string(hash, observation.map_sha256);
    hash_u32(hash, observation.map_width_tiles);
    hash_u32(hash, observation.map_height_tiles);
    hash_u32(hash, observation.local_owner);
    hash_u32(hash, observation.local_faction);
    hash_u32(hash, observation.active_owner_mask);
    hash_u32(hash, observation.local_relation_mask);
    hash_u32(hash, observation.primary_resources);
    hash_u32(hash, observation.secondary_resources);
    hash_u32(hash, observation.auxiliary_resources);
    hash_u32(hash, observation.population_used);
    hash_u32(hash, observation.population_reserved);
    hash_u32(hash, observation.population_limit);
    hash_u32(hash, static_cast<u32>(observation.start_x));
    hash_u32(hash, static_cast<u32>(observation.start_y));
    hash_bool(hash, observation.game_ended);
    hash_u32(hash, observation.game_end_reason);

    hash_u32(hash, static_cast<u32>(observation.tiles.size()));
    for (const AiObservedMapTile& tile : observation.tiles) {
        hash_u32(hash, tile.terrain_flags);
        hash_u32(hash, tile.resource_amount);
        hash_bool(hash, tile.passable);
        hash_bool(hash, tile.explored);
        hash_bool(hash, tile.visible);
    }

    hash_u32(hash, static_cast<u32>(observation.units.size()));
    for (const AiObservedUnit& unit : observation.units) {
        hash_u32(hash, unit.id);
        hash_u32(hash, unit.runtime_slot_index);
        hash_u32(hash, unit.type_id);
        hash_u32(hash, unit.owner_id);
        hash_u32(hash, unit.type_flags);
        hash_u32(hash, static_cast<u32>(unit.x));
        hash_u32(hash, static_cast<u32>(unit.y));
        hash_u32(hash, unit.health);
        hash_u32(hash, unit.max_health);
        hash_u32(hash, unit.secondary_value);
        hash_u32(hash, unit.max_secondary_value);
        hash_bool(hash, unit.controlled);
        hash_bool(hash, unit.visible);
        hash_bool(hash, unit.alive);
        hash_bool(hash, unit.under_construction);
        hash_u32(hash, unit.command_state);
        hash_u32(hash, unit.command_flags);
        hash_u32(hash, unit.command_value);
        hash_u32(hash, unit.action_mode);
        hash_u32(hash, unit.movement_state);
        hash_u32(hash, static_cast<u32>(unit.destination_x));
        hash_u32(hash, static_cast<u32>(unit.destination_y));
        hash_u32(hash, static_cast<u32>(unit.path_target_x));
        hash_u32(hash, static_cast<u32>(unit.path_target_y));
        hash_u32(hash, unit.target_id);
        hash_u32(hash, unit.cargo_amount);
        hash_u32(hash, unit.cargo_capacity);
        hash_u32(hash, unit.queued_production_type_id);
        hash_u32(hash, unit.production_variant);
        hash_u32(hash, unit.deferred_command_count);
        hash_u32(hash, static_cast<u32>(unit.deferred_commands.size()));
        for (const AiObservedQueuedCommand& command :
                unit.deferred_commands) {
            hash_u32(hash, command.state);
            hash_u32(hash,
                static_cast<u32>(command.command_value_or_target));
            hash_u32(hash, static_cast<u32>(command.x_payload));
            hash_u32(hash, command.y_payload);
        }
    }
    return hash;
}

} // namespace ranker
