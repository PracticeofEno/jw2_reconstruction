#include "ranker_unit_lifecycle.h"

#include "ranker_unit_commands.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace ranker {
namespace {

u32 g_transient_object_lifecycle_period = 1;

constexpr u32 kUnitStateLifecycleConstructionActivation = 0x10000077u;

bool has_movement(const UnitLifecycleContext& context) {
    return context.movement != nullptr;
}

bool has_owner_slot(const UnitLifecycleContext& context, u32 owner_id) {
    return owner_id < context.owner_primary_resources.size();
}

bool has_owner_type_count_slot(u32 owner_id, u32 type_id) {
    return owner_id < kUnitOwnerTypeCountOwners && type_id < kUnitOwnerTypeCountTypes;
}

bool has_original_owner_counter_slot(u32 owner_id) {
    return owner_id < kUnitOwnerTypeCountOwners;
}

const UnitMovementDefinition& definition_for(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.find_definition != nullptr) {
        if (const UnitMovementDefinition* definition =
                context.callbacks.find_definition(context, unit.type_id)) {
            return *definition;
        }
    }
    return unit.definition;
}

const UnitMovementDefinition* definition_for_type(UnitLifecycleContext& context,
    u32 type_id, const UnitMovementUnit* fallback_unit = nullptr) {
    if (context.callbacks.find_definition != nullptr) {
        if (const UnitMovementDefinition* definition =
                context.callbacks.find_definition(context, type_id)) {
            return definition;
        }
    }
    if (fallback_unit != nullptr && fallback_unit->type_id == type_id) {
        return &fallback_unit->definition;
    }
    return nullptr;
}

u32 population_cost_for_type(UnitLifecycleContext& context, u32 type_id,
    const UnitMovementUnit* fallback_unit = nullptr) {
    const UnitMovementDefinition* definition =
        definition_for_type(context, type_id, fallback_unit);
    return definition != nullptr ? definition->production_population_cost : 0;
}

void increment_owner_type_count(UnitLifecycleContext& context, u32 owner_id,
    u32 type_id) {
    if (has_owner_type_count_slot(owner_id, type_id)) {
        u32& count = context.owner_unit_type_counts[owner_id][type_id];
        count = static_cast<u8>(count + 1u);
    }
}

void decrement_owner_type_count(UnitLifecycleContext& context, u32 owner_id,
    u32 type_id) {
    if (has_owner_type_count_slot(owner_id, type_id)) {
        u32& count = context.owner_unit_type_counts[owner_id][type_id];
        count = static_cast<u8>(count - 1u);
    }
}

bool owner_has_type(UnitLifecycleContext& context, u32 owner_id, u32 type_id) {
    return has_owner_type_count_slot(owner_id, type_id) &&
        context.owner_unit_type_counts[owner_id][type_id] != 0;
}

u32 lifecycle_random_limit(UnitLifecycleContext& context, u32 limit) {
    if (limit == 0 || context.callbacks.random_limit == nullptr) {
        return 0;
    }
    const u32 value = context.callbacks.random_limit(context, limit);
    return value < limit ? value : value % limit;
}

bool type_uses_any_prerequisite(u32 type_id) {
    return type_id == 0x76 || type_id == 0x78 || type_id == 0x68 ||
        type_id == 0x88 || type_id == 0x7b || type_id == 0x67;
}

u32 footprint_width(const UnitMovementDefinition& definition) {
    return std::max<u32>(definition.footprint_width_tiles, 1);
}

u32 footprint_height(const UnitMovementDefinition& definition) {
    return std::max<u32>(definition.footprint_height_tiles, 1);
}

bool signed_tile_in_bounds(UnitLifecycleContext& context, i32 tile_x, i32 tile_y) {
    return has_movement(context) && tile_x >= 0 && tile_y >= 0 &&
        static_cast<u32>(tile_x) < context.movement->map.width &&
        static_cast<u32>(tile_y) < context.movement->map.height;
}

bool signed_world_tile_in_bounds(UnitLifecycleContext& context, i32 x, i32 y) {
    return signed_tile_in_bounds(context, x >> 5, y >> 5);
}

u32 footprint_world_to_tile(i32 value) {
    return static_cast<u32>(value) >> 5;
}

u32 collision_world_to_tile(i32 value) {
    return static_cast<u32>(value >> 5);
}

i32 tile_to_center(u32 tile) {
    return static_cast<i32>(tile * 32 + 16);
}

i32 tile_to_origin(i32 tile) {
    return tile * 32;
}

bool is_building_type(u32 type_id) {
    return type_id >= 0x60;
}

u32 unit_score_value(UnitLifecycleContext& context, UnitMovementUnit& unit) {
    const UnitMovementDefinition& definition = definition_for(context, unit);
    return definition.production_resource_cost +
        definition.production_secondary_cost;
}

void for_each_footprint_cell(UnitLifecycleContext& context, UnitMovementUnit& unit,
    void (*callback)(UnitMovementCell& cell, UnitMovementUnit& unit)) {
    if (!has_movement(context)) {
        return;
    }

    const UnitMovementDefinition& definition = definition_for(context, unit);
    const u32 base_x = footprint_world_to_tile(unit.x);
    const u32 base_y = footprint_world_to_tile(unit.y);
    const u32 width = footprint_width(definition);
    const u32 height = footprint_height(definition);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            UnitMovementCell* cell = GetMovementCell(context.movement->map,
                base_x + x, base_y + y);
            if (cell != nullptr) {
                callback(*cell, unit);
            }
        }
    }
}

UnitMovementCell* footprint_base_cell(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    if (!has_movement(context)) {
        return nullptr;
    }
    return GetMovementCell(context.movement->map, footprint_world_to_tile(unit.x),
        footprint_world_to_tile(unit.y));
}

void set_footprint_payload_cell(UnitMovementCell& cell, UnitMovementUnit& unit) {
    cell.visibility_flags &= 0x7ffff000;
    cell.visibility_flags |= (unit.type_id & kUnitFootprintTypeMask);
    cell.visibility_flags |= unit.owner_id << kUnitFootprintOwnerShift;
    if (unit.action_mode_gate == 1) {
        cell.visibility_flags |= kUnitFootprintUnderConstruction;
    }
}

void set_footprint_occupied_cell(UnitMovementCell& cell, UnitMovementUnit& unit) {
    if (unit.type_id == 0x6a) {
        cell.flags |= kUnitFootprintSpecialOccupied;
        return;
    }
    cell.visibility_flags |= kUnitFootprintOccupied;
}

void clear_footprint_payload_cell(UnitMovementCell& cell) {
    cell.visibility_flags &= 0x7ffff000;
}

void clear_footprint_occupied_cell(UnitMovementCell& cell, UnitMovementUnit& unit) {
    if (unit.type_id == 0x6a) {
        cell.flags &= ~kUnitFootprintSpecialOccupied;
        return;
    }
    cell.visibility_flags &= ~kUnitFootprintOccupied;
}

void start_lifecycle_command_lockout(UnitMovementUnit& unit, u32 ticks) {
    if (ticks == 0) {
        return;
    }
    unit.command_state |= 0x40000000u;
    unit.animation_timer = 0;
    unit.command_entry_lockout_ticks = ticks;
}

bool advance_lifecycle_command_lockout(UnitMovementUnit& unit) {
    ++unit.animation_timer;
    return unit.animation_timer >= unit.command_entry_lockout_ticks;
}

void reset_runtime_fields(UnitMovementUnit& unit) {
    // InitializePlacedUnitFromMapSlot 0x004cf2b5/0x004cf2c1 writes raw +0x08
    // and +0x0c to zero for every fixed-pool activation.  In particular,
    // retaining +0x0c bit 31 makes a recycled unit acquire the opposite
    // lifecycle target class.
    unit.scenario_string_slot = 0;
    unit.area_marker_flags = 0;
    unit.command_state = kUnitStateRuntimeIdleAcquire;
    unit.command_flags = 0;
    // InitializePlacedUnitFromMapSlot (0x004cf454) writes raw +0x5c to zero.
    // The definition word at the similarly inferred catalog offset is a type
    // flag field, not the unit's mutable subtype-06 command-bit state.
    unit.command_bits.fill(0);
    unit.script_bit_flags = 0;
    unit.runtime_flags = 1;
    unit.draw_flags = 0;
    // Original 0x004cf229 does not write raw +0x74, +0x78/+0x7c, +0x98,
    // +0xec or +0xf4.  Those words retain the prior fixed-pool generation's
    // previous state, destination/cell-render scratch, and timer residues.
    unit.action_mode = 0;
    unit.action_mode_gate = 0;
    unit.command_value = 0;
    unit.target = nullptr;
    unit.linked_unit = nullptr;
    unit.destination_aux_state = 0;
    unit.movement_step_accumulator = 0;
    // InitializePlacedUnitFromMapSlot 0x004cf3b7..0x004cf3d5 clears original
    // raw +0x114/+0x118 and +0x11c/+0x120 for every fixed-pool activation.
    // These are the integer movement residuals followed by the floating-point
    // interpolation accumulators.  Retaining either pair lets a newly
    // produced unit inherit the previous generation's movement path.
    unit.movement_residual_x = 0;
    unit.movement_residual_y = 0;
    unit.movement_interpolation_x = 0.0f;
    unit.movement_interpolation_y = 0.0f;
    unit.work_timer = 0;
    // InitializePlacedUnitFromMapSlot clears raw +0x4c.  State 0x58's typed
    // OBB mirror follows that union, while independent raw +0xf0 deliberately
    // retains its fixed-pool backing value until a shield writes it again.
    unit.cargo_amount = 0;
    unit.reserved_tile_effect = nullptr;
    unit.reserved_tile_effect_slot_offset = 0;
    unit.effect_timer = 0;
    unit.distance_check_mode = 0;
    unit.placement_reset_scratch = 0;
    unit.animation_frame = 0;
    // Raw +0x48 is the dynamic string slot and is explicitly zeroed.
    unit.string_slot = 0;
    unit.item_slots = {};
    for (std::size_t slot = 0;
         slot < unit.item_slots.size() && slot < unit.equipment_slots.size();
         ++slot) {
        unit.equipment_slots[slot] = unit.item_slots[slot];
    }
    unit.status_timer = 0;
    unit.production_variant = 0;
    // InitializePlacedUnitFromMapSlot 0x004cf399 clears raw unit +0x50.
    // Fixed-pool activations must not inherit elite progress from the slot's
    // previous unit generation.
    unit.elite_progress_value = 0;
    // Only the state word of the pending tuple and the deferred count are
    // cleared.  The remaining pending words, the active tuple, and all
    // deferred tuple storage deliberately remain fixed-pool residue.
    unit.pending_command.state = 0;
    unit.deferred_command_count = 0;
    unit.deferred_command_state = 0;
    unit.ability_id = 0;
    unit.spawn_type_id = 0;
    unit.queued_production_type_id = 0;
    unit.extended_production_type_index = 0;
    unit.transfer_limit = 0;
    unit.cargo_capacity = 0;
    unit.harvest_tile_index = 0;
    unit.active = true;
    unit.attached_to_parent = false;
    unit.under_construction = false;
    unit.footprint_registered = false;
    unit.production_reserved = false;
}

bool same_tile_as_active_unit(UnitLifecycleContext& context, UnitMovementUnit& unit,
    u32 tile_x, u32 tile_y) {
    if (!has_movement(context)) {
        return false;
    }
    for (UnitMovementUnit* candidate : context.movement->active_units) {
        if (candidate == nullptr || candidate == &unit ||
            (unit.id != 0 && candidate->id == unit.id) || !candidate->active ||
            (candidate->runtime_flags & 0x80) != 0) {
            continue;
        }
        const UnitMovementDefinition& definition = definition_for(context, *candidate);
        if (definition.movement_class == 3) {
            continue;
        }
        if (collision_world_to_tile(candidate->x) == tile_x &&
            collision_world_to_tile(candidate->y) == tile_y) {
            return true;
        }
    }
    return false;
}

bool footprint_fits_at(UnitLifecycleContext& context, UnitMovementUnit& unit,
    u32 tile_x, u32 tile_y, bool require_matching_terrain, u32 terrain_class) {
    const UnitMovementDefinition& definition = definition_for(context, unit);
    const u32 width = footprint_width(definition);
    const u32 height = footprint_height(definition);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            if (!CheckPlacementFootprintCell(context, unit, tile_x + x, tile_y + y,
                    terrain_class, require_matching_terrain)) {
                return false;
            }
        }
    }
    return true;
}

bool small_unit_placement_tile_fits(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32 tile_x, i32 tile_y, u32 terrain_class,
    bool require_matching_terrain) {
    if (!signed_tile_in_bounds(context, tile_x, tile_y)) {
        return false;
    }
    const u32 ux = static_cast<u32>(tile_x);
    const u32 uy = static_cast<u32>(tile_y);
    if (require_matching_terrain &&
        GetPlacementTerrainClass(context, ux, uy) != terrain_class) {
        return false;
    }
    return CheckUnitCanEnterTerrainCell(*context.movement, unit,
        tile_to_origin(tile_x), tile_to_origin(tile_y));
}

bool find_unit_tile(UnitLifecycleContext& context, UnitMovementUnit& unit,
    i32& x, i32& y, bool require_matching_terrain) {
    if (!has_movement(context)) {
        return false;
    }
    const u32 terrain_class =
        require_matching_terrain && context.placement_terrain_class_override_enabled ?
            context.placement_terrain_class_override :
            signed_world_tile_in_bounds(context, x, y) ?
                GetPlacementTerrainClass(context, static_cast<u32>(x >> 5),
                    static_cast<u32>(y >> 5)) :
                3;
    i32 scan_left = x >> 5;
    i32 scan_top = y >> 5;
    u32 scan_size = 1;
    for (i32 radius = 0; radius < 10; ++radius) {
        for (u32 row = 0; row < scan_size; ++row) {
            const i32 tile_y = scan_top + static_cast<i32>(row);
            for (u32 column = 0; column < scan_size; ++column) {
                const i32 tile_x = scan_left + static_cast<i32>(column);
                if (small_unit_placement_tile_fits(context, unit, tile_x, tile_y,
                        terrain_class, require_matching_terrain)) {
                    x = tile_to_origin(static_cast<u32>(tile_x));
                    y = tile_to_origin(static_cast<u32>(tile_y));
                    return true;
                }
            }
        }
        --scan_left;
        --scan_top;
        scan_size += 2;
    }
    return false;
}

bool find_large_footprint_tile(UnitLifecycleContext& context, UnitMovementUnit& unit,
    i32& x, i32& y, bool require_matching_terrain) {
    static_cast<void>(require_matching_terrain);
    const UnitMovementDefinition& definition = definition_for(context, unit);
    // Original strict/matching helpers 0x004cf546/0x004cf5b7 only enter the
    // structure cell loop when both definition footprint counts are nonzero.
    if (definition.footprint_width_tiles == 0 ||
        definition.footprint_height_tiles == 0) {
        return false;
    }
    const i32 signed_tile_x = x >> 5;
    const i32 signed_tile_y = y >> 5;
    if (!signed_tile_in_bounds(context, signed_tile_x, signed_tile_y)) {
        return false;
    }
    const u32 tile_x = static_cast<u32>(signed_tile_x);
    const u32 tile_y = static_cast<u32>(signed_tile_y);
    const u32 terrain_class = GetPlacementTerrainClass(context, tile_x, tile_y);
    if (!footprint_fits_at(context, unit, tile_x, tile_y, true, terrain_class)) {
        return false;
    }
    x = tile_to_origin(tile_x);
    y = tile_to_origin(tile_y);
    return true;
}

bool uses_nearby_passable_placement_fallback(u32 type_id) {
    return type_id == 0x60 || type_id == 0x70 ||
        type_id == 0x80 || type_id == 0x90;
}

bool find_nearby_passable_placement_tile(UnitLifecycleContext& context, u32 tile_x,
    u32 tile_y, u32& out_tile_x, u32& out_tile_y) {
    if (!has_movement(context)) {
        return false;
    }
    const i32 base_x = static_cast<i32>(tile_x);
    const i32 base_y = static_cast<i32>(tile_y);
    for (i32 y = base_y - 4; y <= base_y + 4; ++y) {
        for (i32 x = base_x - 4; x <= base_x + 4; ++x) {
            if (x < 0 || y < 0) {
                continue;
            }
            const UnitMovementCell* cell = GetMovementCell(context.movement->map,
                static_cast<u32>(x), static_cast<u32>(y));
            if (cell != nullptr &&
                (cell->flags & kMapCellTerrainMask) == kMapCellPassableTerrain) {
                out_tile_x = static_cast<u32>(x);
                out_tile_y = static_cast<u32>(y);
                return true;
            }
        }
    }
    return false;
}

} // namespace

void HandleUnitCreationRegisterFootprint(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    unit.under_construction = false;
    unit.action_mode_gate = 0;
    unit.linked_object_id = unit.id;
    unit.linked_unit = &unit;
    // Original 0x004ce43d copies raw +0xc0/+0xc4 (the aligned current-cell
    // coordinates) to raw +0xc8/+0xcc.  The typed reconstruction keeps both
    // the next-path scratch and the saved rally value, so mirror both aliases.
    unit.next_path_x = unit.current_cell_x;
    unit.next_path_y = unit.current_cell_y;
    unit.saved_path_target_x = unit.current_cell_x;
    unit.saved_path_target_y = unit.current_cell_y;
    increment_owner_type_count(context, unit.owner_id, unit.type_id);

    const UnitMovementDefinition& definition = definition_for(context, unit);
    if ((definition.footprint_flags & 2) != 0) {
        unit.command_flags |= 0x40;
    }
    SetUnitFootprintOccupancyBits(context, unit);
}

void SetUnitLifecycleTransientObjectPeriod(u32 period) {
    g_transient_object_lifecycle_period = std::max<u32>(period, 1);
}

bool CheckOwnerResourceAdjustmentGate(const UnitLifecycleContext& context,
    u32 owner_id, u32 amount) {
    return has_owner_slot(context, owner_id) &&
        context.owner_primary_resources[owner_id] >= amount;
}

bool HandleOwnerPrimaryResourceSpendIfAllowed(UnitLifecycleContext& context,
    u32 owner_id, u32 amount) {
    if (!CheckOwnerResourceAdjustmentGate(context, owner_id, amount)) {
        return false;
    }
    context.owner_primary_resources[owner_id] -= amount;
    return true;
}

void HandleUnitPrimaryResourceCostRefund(UnitLifecycleContext& context,
    UnitMovementUnit& unit, u32 type_id) {
    if (!has_owner_slot(context, unit.owner_id)) {
        return;
    }
    const UnitMovementDefinition* definition =
        definition_for_type(context, type_id, &unit);
    if (definition == nullptr) {
        return;
    }
    context.owner_primary_resources[unit.owner_id] +=
        definition->production_resource_cost;
}

void HandleUnitLifecycleDispatchListTick(UnitLifecycleContext& context) {
    if (!has_movement(context)) {
        return;
    }

    UnitMovementUnit* unit = context.movement->lifecycle_units.empty() ?
        nullptr : context.movement->lifecycle_units.front();
    UnitIntrusiveListCursor cursor{&context.movement->lifecycle_units, 0};
    while (unit != nullptr) {
        // Original 0x004ce7e0 saves raw +0x1cc before processing the current
        // node.  The saved node can meanwhile move to another pool list; once
        // reached, its now-current raw successor must be used.
        UnitMovementUnit* next =
            GetUnitIntrusiveListNext(*context.movement, *unit, cursor);
        if ((unit->command_flags & 0x80) != 0) {
            --unit->work_timer;
            if (unit->work_timer == 0) {
                // The original uses one raw +0x64 word for the active frame
                // and the lifecycle countdown.  Keep the split typed views
                // coherent before this fixed-pool node enters the free list.
                unit->animation_frame = unit->work_timer;
                if (unit->string_slot != 0) {
                    const u32 string_slot = unit->string_slot;
                    unit->string_slot = 0;
                    ClearUnitStringSlotIfUnused(*context.movement, string_slot);
                }
                if (context.callbacks.on_unit_lifecycle_removed != nullptr) {
                    context.callbacks.on_unit_lifecycle_removed(context, *unit);
                }
                HandleLifecycleUnitFreeListMove(*context.movement, *unit);
            }
        }
        else if ((unit->command_flags & 0x200) != 0) {
            unit->command_flags &= ~0x200u;
            // Lifecycle activation exposes raw +0x64 as an animation frame
            // again; carry over the same physical word's current value.
            unit->animation_frame = unit->work_timer;
            HandleLifecycleUnitActiveListMove(*context.movement, *unit);
        }
        else {
            const UnitMovementDefinition& definition = definition_for(context, *unit);
            if (definition.lifecycle_class == 2) {
                HandleUnitLifecycleDecayTimer(context, *unit);
            }
            else {
                HandleUnitLifecycleGrowthOrDecay(context, *unit);
            }
        }
        unit = next;
    }
}

void HandleUnitLifecycleGrowthOrDecay(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    const UnitMovementDefinition& definition = definition_for(context, unit);
    if ((unit.runtime_flags & 0x10) != 0) {
        ++unit.work_timer;
        if (unit.work_timer >= g_transient_object_lifecycle_period) {
            unit.path_target_x = 1;
            unit.command_flags |= 0x80;
            unit.work_timer = 0xdc;
        }
        return;
    }

    // Original HandleUnitLifecycleGrowthOrDecay (0x004ce876) enters the
    // activation/revival block only for the exact raw state 0x10000077.
    // under_construction is a typed mirror of raw +0x30 and is not a state
    // discriminator here.
    if (unit.command_state == kUnitStateLifecycleConstructionActivation) {
        HandleUnitConstructionActivation(context, unit);
        return;
    }

    if ((unit.command_state & 0x40000000u) == 0) {
        ++unit.work_timer;
        const u32 elapsed_ticks = unit.work_timer;
        if (elapsed_ticks < definition.lifecycle_growth_period) {
            return;
        }
        --unit.work_timer;
        if (definition.lifecycle_lockout_period != 0) {
            start_lifecycle_command_lockout(unit, definition.lifecycle_lockout_period);
            return;
        }
    }
    else if (!advance_lifecycle_command_lockout(unit)) {
        return;
    }

    unit.path_target_x = 1;
    if ((unit.command_flags & 0x400000u) == 0 &&
        ((unit.runtime_flags & 0x40000u) != 0 || definition.lifecycle_class != 1)) {
        unit.command_flags |= 0x80;
        unit.work_timer = 0xdc;
        return;
    }
    unit.command_state = kUnitStateLifecycleConstructionActivation;
    unit.work_timer = 0;
}

bool HandleUnitConstructionActivation(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    const UnitMovementDefinition& definition = definition_for(context, unit);
    ++unit.work_timer;
    if (unit.work_timer < std::max<u32>(definition.production_spawn_time, 1)) {
        return false;
    }
    if (definition.lifecycle_class == 1) {
        unit.action_mode = definition.production_resource_cost;
        unit.owner_id = 8;
    }

    i32 placed_x = unit.anchor_x;
    i32 placed_y = unit.anchor_y;
    // Original 0x004ce959..0x004ce96c checks the placement helper's carry
    // result and retries on a later tick without activating the unit.
    if (!FindStrictUnitPlacementPoint(context, unit, placed_x, placed_y)) {
        return false;
    }
    unit.x = placed_x;
    unit.y = placed_y;
    // Original 0x004ce96e..0x004ce980 updates raw current-cell and world
    // coordinates but deliberately preserves destination +0x78/+0x7c.
    unit.current_cell_x = placed_x & ~0x1f;
    unit.current_cell_y = placed_y & ~0x1f;
    unit.command_state = kUnitStateRuntimeIdleAcquire;
    unit.work_timer = 0;
    unit.command_flags &= 0x400000u;
    unit.runtime_flags = 1;
    unit.distance_check_mode = 0;
    unit.deferred_command_count = 0;
    unit.active_command_payload = {};
    unit.pending_command = {};
    unit.health = unit.max_health;
    unit.secondary_value = unit.max_secondary_value;
    unit.under_construction = false;
    unit.action_mode_gate = 0;
    unit.runtime_stat_28 = 0;
    unit.elite_progress_value = 0;
    unit.status_timer = 0;
    unit.production_variant = 0;
    unit.item_slots = {};
    for (std::size_t slot = 0;
         slot < unit.item_slots.size() && slot < unit.equipment_slots.size();
         ++slot) {
        unit.equipment_slots[slot] = unit.item_slots[slot];
    }
    SetUnitFootprintOccupancyBits(context, unit);
    unit.command_flags |= 0x200;
    if (context.callbacks.on_unit_became_active != nullptr) {
        context.callbacks.on_unit_became_active(context, unit);
    }
    return true;
}

void HandleUnitLifecycleDecayTimer(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    const UnitMovementDefinition& definition = definition_for(context, unit);
    if (definition.lifecycle_decay_mode == 0) {
        if ((unit.command_state & 0x40000000u) == 0) {
            start_lifecycle_command_lockout(unit, 0xd8);
            return;
        }
    }
    else if ((unit.command_state & 0x40000000u) == 0) {
        ++unit.work_timer;
        if (unit.work_timer < 0x13) {
            return;
        }
        if (definition.lifecycle_decay_lockout_gate != 0 &&
            definition.lifecycle_lockout_period != 0) {
            start_lifecycle_command_lockout(unit, definition.lifecycle_lockout_period);
            return;
        }
    }
    if ((unit.command_state & 0x40000000u) != 0 &&
        !advance_lifecycle_command_lockout(unit)) {
        return;
    }

    unit.command_flags |= 0x80;
    unit.work_timer = 0xdc;
}

bool HandleUnitLifecycleTimedRemoval(UnitMovementUnit& unit, u32 duration) {
    ++unit.work_timer;
    if (unit.work_timer < duration) {
        return false;
    }
    unit.path_target_y = 1;
    unit.command_flags |= 0x80;
    unit.work_timer = 0xdc;
    return true;
}

void HandleUnitSimulationListTick(UnitLifecycleContext& context) {
    HandleOwnerPopulationReservationTotals(context);
    if (!has_movement(context)) {
        return;
    }

    UnitMovementContext& movement = *context.movement;
    ProcessInvalidUnitReservedTiles(movement);
    if (context.callbacks.on_before_active_simulation != nullptr) {
        context.callbacks.on_before_active_simulation(context);
    }

    UnitMovementUnit* unit = movement.active_units.empty() ?
        nullptr : movement.active_units.front();
    UnitIntrusiveListCursor cursor{&movement.active_units, 0};
    while (unit != nullptr) {
        // Original 0x004cebb9 pushes raw active-next before dispatch and pops
        // that saved pointer afterwards.  If an earlier handler moves that
        // saved node, processing resumes through the node's updated shared
        // intrusive next link instead of the frame-entry active-list tail.
        UnitMovementUnit* next =
            GetUnitIntrusiveListNext(movement, *unit, cursor);
        if (context.callbacks.on_active_unit_runtime_dispatch != nullptr) {
            context.callbacks.on_active_unit_runtime_dispatch(context, *unit);
        }
        else if (unit->type_id < 0x60) {
            ProcessUnitRuntimeStateTick(movement, *unit);
        }
        unit = next;
    }
}

void HandleOwnerPopulationReservationTotals(UnitLifecycleContext& context) {
    context.owner_population_used = {};
    context.owner_population_reserved = {};
    if (!has_movement(context)) {
        return;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || !unit->active || (unit->runtime_flags & 0x10) != 0) {
            continue;
        }
        if (!has_owner_slot(context, unit->owner_id)) {
            continue;
        }

        const UnitMovementDefinition* definition = &definition_for(context, *unit);
        if ((unit->runtime_flags & 0x40000u) != 0) {
            if (const UnitMovementDefinition* alternate =
                    definition_for_type(context, definition->alternate_type_id, unit)) {
                definition = alternate;
            }
        }

        const u32 cost = definition->production_population_cost;
        if (definition->lifecycle_class == 2) {
            if (unit->action_mode_gate != 1) {
                context.owner_population_used[unit->owner_id] += cost;
                if ((unit->command_state & 0xffffffu) ==
                        kUnitStateProductionSpawnCycle &&
                    unit->spawn_type_id != 0xffffffffu) {
                    context.owner_population_reserved[unit->owner_id] +=
                        population_cost_for_type(context, unit->spawn_type_id, unit);
                }
            }
        }
        else if ((unit->command_state & 0xffffffu) != kUnitStateLinkedUnitReleaseCycle ||
            // HandleOwnerPopulationReservationTotals (0x004ceeb3) tests raw
            // unit +0x4c (DAT_00a04004), the cargo/transport occupancy value,
            // while state 0x60 is releasing a linked unit.  command_value is
            // raw +0x68 and can hold an unrelated production/type payload.
            unit->cargo_amount != 0) {
            context.owner_population_reserved[unit->owner_id] += cost;
        }
    }
}

void HandleUnitDeathLifecycleTransition(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    unit.runtime_flags |= 4;
    unit.command_state &= ~0x40000000u;
    unit.animation_frame = 0;
    unit.work_timer = 0;
    unit.draw_flags = 0;
    ClearUnitFootprintOccupancyBits(context, unit);
    if (unit.action_mode_gate == 1) {
        return;
    }
    if (unit.under_construction) {
        HandleUnitPrimaryResourceCostRefund(context, unit, unit.type_id);
    }
    else {
        HandleUnitDeathOwnerCounters(context, unit);
    }
}

void SetUnitFootprintOccupancyBits(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    const UnitMovementDefinition& definition = definition_for(context, unit);
    if (definition.lifecycle_class != 2 || definition.footprint_width_tiles == 0 ||
        definition.footprint_height_tiles == 0) {
        return;
    }
    if (UnitMovementCell* cell = footprint_base_cell(context, unit)) {
        set_footprint_payload_cell(*cell, unit);
    }
    for_each_footprint_cell(context, unit, set_footprint_occupied_cell);
    unit.footprint_registered = true;
    if (context.callbacks.on_footprint_changed != nullptr) {
        context.callbacks.on_footprint_changed(context, unit);
    }
}

void ClearUnitFootprintOccupancyBits(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    const UnitMovementDefinition& definition = definition_for(context, unit);
    if (definition.movement_class != 1 || definition.footprint_width_tiles == 0 ||
        definition.footprint_height_tiles == 0) {
        return;
    }
    if (!unit.footprint_registered) {
        return;
    }
    if (UnitMovementCell* cell = footprint_base_cell(context, unit)) {
        clear_footprint_payload_cell(*cell);
    }
    for_each_footprint_cell(context, unit, clear_footprint_occupied_cell);
    unit.footprint_registered = false;
    if (context.callbacks.on_footprint_changed != nullptr) {
        context.callbacks.on_footprint_changed(context, unit);
    }
}

bool FindStrictUnitPlacementPoint(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32& x, i32& y) {
    if (unit.type_id < 0x60) {
        return find_unit_tile(context, unit, x, y, false);
    }
    return find_large_footprint_tile(context, unit, x, y, false);
}

bool FindMatchingTerrainUnitPlacementPoint(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32& x, i32& y) {
    if (unit.type_id < 0x60) {
        return find_unit_tile(context, unit, x, y, true);
    }
    return find_large_footprint_tile(context, unit, x, y, true);
}

u32 GetPlacementTerrainClass(UnitLifecycleContext& context, u32 tile_x, u32 tile_y) {
    if (!has_movement(context)) {
        return 3;
    }
    const UnitMovementCell* cell = GetMovementCell(context.movement->map, tile_x, tile_y);
    if (cell == nullptr) {
        return 3;
    }
    return (cell->alternate_flags & 0x1c000000) >> 26;
}

bool CheckPlacementFootprintCell(UnitLifecycleContext& context,
    UnitMovementUnit& unit, u32 tile_x, u32 tile_y, u32 terrain_class,
    bool require_matching_terrain) {
    if (!has_movement(context)) {
        return false;
    }
    const UnitMovementCell* cell = GetMovementCell(context.movement->map, tile_x, tile_y);
    if (cell == nullptr) {
        return false;
    }
    constexpr u32 kPlacementTerrainValidFlag = 0x80000000u;
    if ((cell->flags & kMapCellTerrainMask) != 0 ||
        (cell->alternate_flags & kPlacementTerrainValidFlag) == 0 ||
        (cell->visibility_flags & kUnitFootprintOccupied) != 0 ||
        (cell->flags & kUnitFootprintSpecialOccupied) != 0) {
        return false;
    }
    if (require_matching_terrain &&
        ((cell->alternate_flags & 0x1c000000) >> 26) != terrain_class) {
        return false;
    }
    if (uses_nearby_passable_placement_fallback(unit.type_id)) {
        u32 nearby_tile_x = tile_x;
        u32 nearby_tile_y = tile_y;
        if (find_nearby_passable_placement_tile(context, tile_x, tile_y,
                nearby_tile_x, nearby_tile_y)) {
            return false;
        }
    }
    return !same_tile_as_active_unit(context, unit, tile_x, tile_y);
}

bool CheckUnitPlacementFootprintArea(UnitLifecycleContext& context,
    UnitMovementUnit& unit, i32 x, i32 y, bool require_matching_terrain) {
    static_cast<void>(require_matching_terrain);
    const i32 signed_tile_x = x >> 5;
    const i32 signed_tile_y = y >> 5;
    if (!signed_tile_in_bounds(context, signed_tile_x, signed_tile_y)) {
        return false;
    }
    const u32 tile_x = static_cast<u32>(signed_tile_x);
    const u32 tile_y = static_cast<u32>(signed_tile_y);
    const u32 terrain_class = GetPlacementTerrainClass(context, tile_x, tile_y);
    return footprint_fits_at(context, unit, tile_x, tile_y, true, terrain_class);
}

bool FindNearbyPassablePlacementTile(UnitLifecycleContext& context, u32 tile_x,
    u32 tile_y, u32& out_tile_x, u32& out_tile_y) {
    return find_nearby_passable_placement_tile(context, tile_x, tile_y,
        out_tile_x, out_tile_y);
}

UnitProductionRequirementCode CheckUnitProductionRequirements(
    UnitLifecycleContext& context, u32 owner_id, u32 type_id) {
    const UnitMovementDefinition* definition = definition_for_type(context, type_id);
    if (definition == nullptr || !has_owner_slot(context, owner_id)) {
        return UnitProductionRequirementCode::missing_prerequisite;
    }

    if (context.owner_primary_resources[owner_id] <
        definition->production_resource_cost) {
        return UnitProductionRequirementCode::missing_primary_resource;
    }
    if (context.owner_secondary_resources[owner_id] <
        definition->production_secondary_cost) {
        return UnitProductionRequirementCode::missing_secondary_resource;
    }
    if (definition->lifecycle_class != 2) {
        const u32 projected = context.owner_population_reserved[owner_id] +
            definition->production_population_cost;
        if (context.owner_population_limit[owner_id] < projected) {
            return UnitProductionRequirementCode::population_limit;
        }
        if (context.owner_population_used[owner_id] < projected) {
            return static_cast<UnitProductionRequirementCode>(
                static_cast<u32>(UnitProductionRequirementCode::population_reserved_base) +
                context.owner_faction_ids[owner_id]);
        }
    }
    if (!CheckUnitProductionPrerequisites(context, owner_id, type_id)) {
        return UnitProductionRequirementCode::missing_prerequisite;
    }
    return UnitProductionRequirementCode::ok;
}

bool CheckUnitProductionPrerequisites(UnitLifecycleContext& context,
    u32 owner_id, u32 type_id) {
    const UnitMovementDefinition* definition = definition_for_type(context, type_id);
    if (definition == nullptr || definition->prerequisite_count == 0) {
        return true;
    }

    const u32 count = std::min<u32>(definition->prerequisite_count,
        definition->prerequisite_type_ids.size());
    if (type_uses_any_prerequisite(type_id)) {
        for (u32 i = 0; i < count; ++i) {
            if (owner_has_type(context, owner_id, definition->prerequisite_type_ids[i])) {
                return true;
            }
        }
        return false;
    }

    for (u32 i = 0; i < count; ++i) {
        if (!owner_has_type(context, owner_id, definition->prerequisite_type_ids[i])) {
            return false;
        }
    }
    return true;
}

bool InitializePlacedUnitFromMapSlot(UnitLifecycleContext& context,
    UnitMovementUnit& unit, u32 type_id, u32 owner_id, i32 x, i32 y) {
    const UnitMovementDefinition* definition = definition_for_type(context, type_id);
    if (definition == nullptr) {
        return false;
    }

    // Original raw +0x78/+0x7c are one pair of fixed-pool words.  Mobile
    // units interpret them as destination x/y while types >= 0x60 reuse the
    // same storage as cell-render scratch.  The typed runtime splits those
    // interpretations, so carry the raw bit pattern across a recycled slot's
    // type-class transition before replacing type_id.  FUN_004cf229 leaves
    // both words untouched during placed-unit initialization.
    const bool previous_uses_cell_scratch = unit.type_id >= 0x60u;
    const bool placed_uses_cell_scratch = type_id >= 0x60u;
    if (!previous_uses_cell_scratch && placed_uses_cell_scratch) {
        unit.cell_channel_additive_frame = static_cast<u32>(unit.destination_x);
        unit.cell_flag40_animation_frame = static_cast<u32>(unit.destination_y);
    }
    else if (previous_uses_cell_scratch && !placed_uses_cell_scratch) {
        unit.destination_x = static_cast<i32>(unit.cell_channel_additive_frame);
        unit.destination_y = static_cast<i32>(unit.cell_flag40_animation_frame);
    }

    unit.type_id = type_id;
    unit.owner_id = owner_id;
    unit.definition = *definition;
    unit.type_flags = definition->type_flags;
    if (context.callbacks.find_placement != nullptr) {
        if (!context.callbacks.find_placement(context, unit, x, y)) {
            return false;
        }
    }
    else {
        // Original InitializePlacedUnitFromMapSlot 0x004cf251 resolves the
        // type row and 0x004cf258 compares original definition +0x14c with
        // three.  Live original/rebuild definition probes confirm that raw
        // field is the reconstructed movement_class (type 24 is 3/3 while
        // lifecycle_class is 0), so only that class selects the strict scan.
        const bool placed = definition->movement_class == 3
            ? FindStrictUnitPlacementPoint(context, unit, x, y)
            : FindMatchingTerrainUnitPlacementPoint(context, unit, x, y);
        if (!placed) {
            return false;
        }
    }

    unit.x = x;
    unit.y = y;
    // InitializePlacedUnitFromMapSlot (original 0x004cf229) copies the
    // resolved point to raw world +0xb8/+0xbc, current-cell +0xc0/+0xc4 and
    // anchor +0xd0/+0xd4.  It never writes destination +0x78/+0x7c, so a
    // reused fixed-pool node retains those words.  Fresh detached records
    // still start at their default zero values.
    unit.current_cell_x = x & ~0x1f;
    unit.current_cell_y = y & ~0x1f;
    // InitializePlacedUnitFromMapSlot (original 0x004cf229) clears the path
    // target at raw +0x6c/+0x70 and copies the placement point to +0xb8/+0xbc,
    // +0xc0/+0xc4 and +0xd0/+0xd4.  It does not write +0xc8/+0xcc at all.
    // Detached typed startup records begin with zero here; the later fixed-
    // pool assignment restores the consumed scenario node's residual words.
    // HandleUnitCreationRegisterFootprint overwrites them for the first base.
    unit.path_target_x = 0;
    unit.path_target_y = 0;
    unit.next_path_x = 0;
    unit.next_path_y = 0;
    unit.anchor_x = x;
    unit.anchor_y = y;
    reset_runtime_fields(unit);
    unit.script_bit_flags = definition->initial_script_bit_flags;
    unit.direction = lifecycle_random_limit(context, 8) + 1;
    unit.animation_frame = lifecycle_random_limit(context, 0x40);
    unit.max_health = definition->initial_max_health;
    unit.health = unit.max_health;
    unit.max_secondary_value = definition->initial_max_secondary_value;
    unit.secondary_value = (unit.max_secondary_value >> 2) +
        (unit.max_secondary_value >> 3);
    // Original 0x004cf300/0x004cf30c copy the definition's two runtime profile
    // fields into raw +0x1c/+0x20 for every newly placed unit.
    unit.runtime_stat_1c = definition->profile_offense_value;
    unit.runtime_stat_20 = definition->profile_defense_value;
    unit.runtime_stat_28 = definition->initial_secondary_value;

    // InitializePlacedUnitFromMapSlot (0x004cf4a4..0x004cf4d2) sets raw unit
    // +0x9c bit 0x40 only for lifecycle classes other than two when definition
    // raw +0x1f8 bit 1 requests it (the fresh unit's equipment slots were just
    // cleared above).  Footprint dimensions are not part of this gate.  Treating
    // any nonzero dimensions
    // as the same flag made ordinary members of a mixed selection enter the
    // cloaked render path when the actual Hide-capable member used action 0x13.
    if (definition->lifecycle_class != 2 &&
        (definition->footprint_flags & 2) != 0) {
        unit.command_flags |= 0x40;
    }

    // Original 0x004cf499 registers the footprint before 0x004cf4f9 applies
    // the movement-class-1 construction state.  In particular, raw +0x30 is
    // still zero while the base footprint payload is written.
    SetUnitFootprintOccupancyBits(context, unit);
    if (owner_id < kUnitOwnerTypeCountOwners) {
        const u32 score = definition->production_resource_cost +
            definition->production_secondary_cost;
        if (type_id < 0x60) {
            ++context.owner_unit_active_count[owner_id];
            context.owner_unit_score[owner_id] += score;
            increment_owner_type_count(context, owner_id, type_id);
        }
        else {
            ++context.owner_building_active_count[owner_id];
            context.owner_building_score[owner_id] += score;
        }
    }

    if (definition->movement_class == 1) {
        unit.under_construction = true;
        unit.action_mode_gate = 1;
        unit.animation_frame = 0;
        unit.work_timer = 0;
        unit.action_mode = definition->production_spawn_time / 10;
        // Raw +0x34 is the construction accumulator for lifecycle-class-1
        // structures.  It aliases generic equipment slot 1; raw +0x28 keeps
        // the definition's ordinary runtime statistic throughout construction.
        unit.equipment_slots[1] =
            unit.action_mode * definition->initial_max_health;
        unit.item_slots[1] = unit.equipment_slots[1];
        // InitializePlacedUnitFromMapSlot 0x004cf538 writes raw health +0x18
        // to one unconditionally for construction-class placements, including
        // definitions whose initial max health is zero.
        unit.health = 1;
    }
    return true;
}

void HandleUnitRemovalAccounting(UnitLifecycleContext& context, UnitMovementUnit& unit) {
    ClearUnitFootprintOccupancyBits(context, unit);
    if (unit.string_slot != 0) {
        const u32 string_slot = unit.string_slot;
        unit.string_slot = 0;
        if (has_movement(context) && string_slot != kInvalidUnitStringSlot) {
            ClearUnitStringSlotIfUnused(*context.movement, string_slot);
        }
    }
    if (has_original_owner_counter_slot(unit.owner_id)) {
        if (is_building_type(unit.type_id)) {
            if (context.owner_building_active_count[unit.owner_id] != 0) {
                --context.owner_building_active_count[unit.owner_id];
            }
        }
        else if (context.owner_unit_active_count[unit.owner_id] != 0) {
            --context.owner_unit_active_count[unit.owner_id];
        }
    }
    if (!(is_building_type(unit.type_id) && unit.action_mode_gate == 1)) {
        decrement_owner_type_count(context, unit.owner_id, unit.type_id);
    }
    if (has_movement(context)) {
        HandleActiveUnitFreeListMove(*context.movement, unit);
    }
    else {
        unit.active = false;
    }
}

void HandleUnitKillOwnerCounters(UnitLifecycleContext& context,
    UnitMovementUnit& attacker, UnitMovementUnit& defeated) {
    // Original 0x004cf911 indexes all four counters by the attacker owner in
    // ESI: unit/building kills at 0x70726c/0x70728c and cumulative scores at
    // 0x70736c/0x70738c.  EDI is used only to select the defeated type bucket.
    if (!has_original_owner_counter_slot(attacker.owner_id)) {
        return;
    }
    const u32 owner_id = attacker.owner_id;
    const u32 score = unit_score_value(context, attacker);
    if (is_building_type(defeated.type_id)) {
        ++context.owner_building_kill_count[owner_id];
        context.owner_building_score[owner_id] += score;
        // Original 0x004cf95d clears defeated raw +0x4c.  The same field is
        // the cargo/transport occupancy value consumed by 0x004ca3b3 and
        // 0x004ca8e2, represented by cargo_amount in the typed runtime.
        defeated.cargo_amount = 0;
        return;
    }
    ++context.owner_unit_kill_count[owner_id];
    context.owner_unit_score[owner_id] += score;
}

void HandleUnitDeathOwnerCounters(UnitLifecycleContext& context,
    UnitMovementUnit& unit) {
    if (has_original_owner_counter_slot(unit.owner_id)) {
        if (is_building_type(unit.type_id)) {
            ++context.owner_building_lost_count[unit.owner_id];
        }
        else {
            ++context.owner_unit_lost_count[unit.owner_id];
        }
    }
    // Original 0x004cf8e5..0x004cf90c decrements the building-active total,
    // then returns without touching DAT_00707430 when raw +0x30 is one.  A
    // construction-class building is not added to the completed-type table
    // until HandleUnitCreationRegisterFootprint, so subtracting it on death
    // creates a wrapped/stale prerequisite count that the original never has.
    if (is_building_type(unit.type_id) && unit.action_mode_gate == 1) {
        return;
    }
    u32 counted_type = unit.type_id;
    if ((unit.runtime_flags & 0x40000) != 0) {
        counted_type = unit.definition.alternate_type_id;
    }
    decrement_owner_type_count(context, unit.owner_id, counted_type);
}

void HandleOwnerUnitTypeCountRebuild(UnitLifecycleContext& context) {
    for (auto& owner_counts : context.owner_unit_type_counts) {
        owner_counts = {};
    }
    if (!has_movement(context)) {
        return;
    }
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || !unit->active) {
            continue;
        }
        if (unit->type_id >= 0x60 && unit->action_mode_gate == 1) {
            continue;
        }
        // A state-0x56 morph replaces the live definition/type but leaves raw
        // runtime flag 0x40000 set until state 0x57 restores the original
        // form.  The original owner-count table continues to account that
        // object in the pre-morph bucket during this interval.  The death
        // path above already applies the same reverse-definition rule; the
        // full per-frame rebuild must not silently move it to the visual
        // alternate bucket one frame after the morph completes.
        u32 counted_type = unit->type_id;
        if ((unit->command_state & kUnitCommandStateMask) ==
                kUnitStateLinkedUnitReleaseCycle &&
            unit->cargo_amount != 0) {
            // During state 0x60 the visual parent has already changed to its
            // linked result, but raw +0x4c still holds the consumed source
            // type.  The original completed-type table remains in the source
            // bucket until the release lockout commits and frees the hidden
            // children.  Rebuilding from the live visual type one frame early
            // makes requirements and AI state diverge even though unit rows
            // themselves still match.
            counted_type = unit->cargo_amount;
        }
        else if ((unit->runtime_flags & 0x40000u) != 0 &&
            unit->definition.alternate_type_id != 0) {
            counted_type = unit->definition.alternate_type_id;
        }
        if (!has_owner_type_count_slot(unit->owner_id, counted_type)) {
            continue;
        }
        u32& count =
            context.owner_unit_type_counts[unit->owner_id][counted_type];
        count = static_cast<u8>(count + 1u);
    }
}

} // namespace ranker
