#include "ranker_ai_observation.h"

#include "ranker_map_effects.h"

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
    // at another slot's base.  Prefer the caller-supplied own start (from the
    // startup owner slots) and fall back for legacy callers.
    if (input.own_start_valid) {
        observation.start_x = input.own_start_x;
        observation.start_y = input.own_start_y;
    } else {
        observation.start_x = input.players->owner_start_x[input.local_owner];
        observation.start_y = input.players->owner_start_y[input.local_owner];
    }
    observation.game_ended = input.game_ended;
    observation.game_end_reason = input.game_end_reason;
    observation.research_levels = input.research_levels;
    observation.research_order_levels = input.research_order_levels;
    observation.start_candidate_x = input.start_candidate_x;
    observation.start_candidate_y = input.start_candidate_y;
    observation.start_candidate_mask = input.start_candidate_mask;

    // Berry positions and INITIAL amounts are map data (public, like terrain
    // and the start candidates), so a fresh memory is seeded from the map's
    // current amounts - at the first observation nobody has harvested yet,
    // so those are the initial amounts.  Only DEPLETION stays fog-honest:
    // the memory is updated while a tile is in this owner's vision.
    if (input.resource_memory != nullptr &&
        input.resource_memory->size() != tile_count) {
        std::vector<u32>& memory = *input.resource_memory;
        memory.assign(tile_count, 0);
        for (u32 y = 0; y < map.height; ++y) {
            for (u32 x = 0; x < map.width; ++x) {
                memory[static_cast<std::size_t>(y) * map.width + x] =
                    (map.cells[static_cast<std::size_t>(y) * stride + x].flags &
                        kMapCellHarvestAmountMask) >> kMapCellHarvestAmountShift;
            }
        }
    }
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
            const u32 live_amount =
                (source.flags & kMapCellHarvestAmountMask) >>
                    kMapCellHarvestAmountShift;
            // Harvestable amount under fog-of-war rules: with a per-owner
            // resource memory the reported value is the LAST-SEEN snapshot —
            // updated only while the tile is in this owner's active vision —
            // so depletion elsewhere is not observable through fog (a stale
            // value self-corrects when any own unit re-lights the tile, and
            // a harvester walking to a remembered-but-empty patch is exactly
            // real-RTS behavior).  Legacy callers without a memory keep the
            // relaxed explored-reveals-live behavior.
            u32 reported_amount;
            if (input.resource_memory != nullptr) {
                std::vector<u32>& memory = *input.resource_memory;
                if (visible) {
                    memory[compact_index] = live_amount;
                }
                // Reported even when unexplored: the memory started as the
                // public map amounts, so an unexplored tile reports its
                // initial amount (knowledge), not 0.  Orders on it are still
                // refused by the planner's explored gate (harvest / build).
                reported_amount = memory[compact_index];
            } else {
                reported_amount = explored ? live_amount : 0u;
            }
            // Walkable = the engine's ground entry rule for movement class 0
            // (legacy_movement_class_can_enter_cell), static part: terrain
            // class 0 (terrain_clear) and the decoration-layer entry bits
            // 0x20000000 and 0x60000000 of alternate_flags.  Berry tiles
            // (class 0x100) are NOT walkable - workers enter them only through
            // the harvest command shortcut - so they are reported by
            // resource_amount, not by passable.  The dynamic part (a building
            // footprint on the cell, visibility_flags 0x20000000) is left to
            // the engine's pathing.  Buildable = the engine placement rule's
            // static part (terrain class 0 + placement-valid brush flag).
            constexpr u32 kPlacementTerrainValidFlag = 0x80000000u;
            constexpr u32 kGroundEntryFlag = 0x20000000u;
            constexpr u32 kGroundEntryClassMask = 0x60000000u;
            const u32 terrain_class = source.flags & kMapCellTerrainMask;
            const bool blocked = (source.flags & kMapCellBlockedTerrain) != 0;
            observation.tiles.push_back(AiObservedMapTile{
                source.flags & kAiStaticTerrainMask,
                reported_amount,
                terrain_class == 0u &&
                    (source.alternate_flags & kGroundEntryFlag) != 0 &&
                    (source.alternate_flags & kGroundEntryClassMask) != 0,
                explored,
                visible,
                terrain_class == 0u && !blocked &&
                    (source.alternate_flags & kPlacementTerrainValidFlag) != 0,
                (source.alternate_flags & 0x1c000000u) >> 26,
            });
        }
    }

    // v9 ground pickups: meat map effects (ids 1..4, ranker_meat_pipeline.h)
    // on currently VISIBLE tiles only - the same fog rule as units.  Walking
    // a pickup-capable unit onto one collects it (engine cmd-5 point path).
    if (input.map_effects != nullptr && observation.map_width_tiles != 0) {
        for (const u32 index : input.map_effects->active_effect_indices) {
            if (index >= input.map_effects->effects.size()) {
                continue;
            }
            const MapEffectInstance& effect = input.map_effects->effects[index];
            if (!effect.active || effect.effect_id == 0 ||
                effect.effect_id >= 5) {
                continue;
            }
            const u32 tile_x = static_cast<u32>(std::max(effect.x, 0)) >> 5;
            const u32 tile_y = static_cast<u32>(std::max(effect.y, 0)) >> 5;
            if (tile_x >= observation.map_width_tiles ||
                tile_y >= observation.map_height_tiles) {
                continue;
            }
            const std::size_t tile = static_cast<std::size_t>(tile_y) *
                observation.map_width_tiles + tile_x;
            if (tile >= observation.tiles.size() ||
                !observation.tiles[tile].visible) {
                continue;
            }
            AiObservedMapEffect observed{};
            observed.id = effect.id;
            observed.effect_id = effect.effect_id;
            observed.x = effect.x;
            observed.y = effect.y;
            observed.amount = effect.repeat_count;
            observed.linked = (effect.flags & kMapEffectLinkedFlag) != 0;
            observation.map_effects.push_back(observed);
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
        observed.sight_range =
            unit->definition.effect_adjusted_interaction_range_base;
        observed.transport_capacity = unit->definition.transport_capacity;
        observed.render_class = unit->definition.render_class;
        observed.movement_step_limit = unit->definition.movement_step_limit;
        observed.movement_period = std::max<u32>(unit->definition.movement_period, 1u);
        observed.attack_range_base = unit->definition.action_range_base;
        observed.attack_cooldown_ticks =
            unit->definition.action_recovery_base_ticks;
        // Public type stats first; the optional hook replaces them with the
        // engine's effective values where the caller can resolve them.
        AiUnitCombatProfile profile{};
        profile.attack_range = unit->definition.action_range_base;
        profile.attack_range_vs_air =
            unit->definition.action_range_base_vs_class3;
        // v8: the engine's OP-DP damage stats, public base definition values
        // (the hook applies research/buffs for controlled units only).
        profile.attack_power = unit->definition.profile_offense_value;
        profile.defense_power = unit->definition.profile_defense_value;
        if (input.unit_combat_profile != nullptr) {
            input.unit_combat_profile(*unit, controlled, profile,
                input.unit_combat_profile_user_data);
        }
        observed.attack_range = profile.attack_range;
        observed.attack_range_vs_air = profile.attack_range_vs_air;
        observed.attackable_class_mask = profile.attackable_class_mask;
        observed.attack_power = profile.attack_power;
        observed.defense_power = profile.defense_power;
        // Rendered state is public: facing/animation are drawn on screen and
        // the overlays display level (status_timer + 1) and experience
        // (elite_progress_value) for any selected unit.
        observed.direction = unit->direction;
        observed.animation_frame = unit->animation_frame;
        observed.animation_timer = unit->animation_timer;
        observed.level = unit->status_timer;
        observed.experience = unit->elite_progress_value;
        // v5 public type info (facing-table resolution).
        observed.use_16_direction_lookup =
            unit->definition.use_16_direction_lookup;

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
            observed.command_entry_lockout_ticks =
                unit->command_entry_lockout_ticks;
            observed.command_lockout_ticks = unit->command_lockout_ticks;
            observed.effect_timer = unit->effect_timer;
            // v5 controlled-only entity-control inputs.
            observed.movement_class = unit->definition.movement_class;
            observed.distance_check_mode = unit->distance_check_mode;
            observed.equipment_flags = unit->equipment_flags;
            observed.item_slots = unit->item_slots;
            observed.equipment_slots = unit->equipment_slots;
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
        hash_bool(hash, tile.buildable);
        hash_u32(hash, tile.placement_class);
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
        hash_u32(hash, unit.attack_range);
        hash_u32(hash, unit.attack_range_vs_air);
        hash_u32(hash, unit.attack_range_base);
        hash_u32(hash, unit.sight_range);
        hash_u32(hash, unit.transport_capacity);
        hash_u32(hash, unit.movement_step_limit);
        hash_u32(hash, unit.movement_period);
        hash_u32(hash, unit.render_class);
        hash_u32(hash, unit.attackable_class_mask);
        hash_u32(hash, unit.direction);
        hash_u32(hash, unit.animation_frame);
        hash_u32(hash, unit.animation_timer);
        hash_u32(hash, unit.level);
        hash_u32(hash, unit.experience);
        hash_bool(hash, unit.use_16_direction_lookup);
        hash_u32(hash, unit.movement_class);
        hash_u32(hash, unit.distance_check_mode);
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
        hash_u32(hash, unit.command_entry_lockout_ticks);
        hash_u32(hash, unit.command_lockout_ticks);
        hash_u32(hash, unit.effect_timer);
        hash_u32(hash, unit.equipment_flags);
        for (const u32 slot : unit.item_slots) {
            hash_u32(hash, slot);
        }
        for (const u32 slot : unit.equipment_slots) {
            hash_u32(hash, slot);
        }
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
