#include "ranker_unit_commands.h"

#include "ranker_map_effects.h"
#include "ranker_production_orders.h"
#include "ranker_unit_action.h"
#include "ranker_unit_damage.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_target_helpers.h"

#include <algorithm>
#include <limits>

namespace ranker {
namespace {

constexpr u32 kDefaultWorkerHarvestAmount = 12;
constexpr u32 kReservedTileCompletionActionId = 0x26;
constexpr u32 kSelectedUnitActionEffectBase = 0x3d;
constexpr u32 kReservedTileCompletionEffectId =
    kSelectedUnitActionEffectBase + kReservedTileCompletionActionId;

UnitMovementContext& movement(UnitCommandContext& context) {
    return *context.movement;
}

bool has_movement(UnitCommandContext& context) {
    return context.movement != nullptr;
}

bool target_alive(const UnitMovementUnit* target) {
    return target != nullptr && target->active &&
        (target->command_state & kUnitCommandDead) == 0 &&
        (target->runtime_flags & 0x84) == 0;
}

UnitMovementUnit* find_target(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (context.callbacks.find_target != nullptr) {
        return context.callbacks.find_target(context, unit);
    }
    if (!has_movement(context)) {
        return nullptr;
    }
    UnitMovementUnit* best = nullptr;
    u32 best_distance = 0xffffffff;
    for (UnitMovementUnit* candidate : context.movement->active_units) {
        if (candidate == nullptr || candidate == &unit || !target_alive(candidate)) {
            continue;
        }
        if (candidate->owner_id == unit.owner_id) {
            continue;
        }
        const u32 distance = CalculateApproxUnitDistance(unit.x, unit.y,
            candidate->x, candidate->y);
        if (distance < best_distance) {
            best = candidate;
            best_distance = distance;
        }
    }
    return best;
}

UnitMovementUnit* find_nearby_follow_target(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.find_nearby_follow_target != nullptr) {
        return context.callbacks.find_nearby_follow_target(context, unit);
    }
    return nullptr;
}

UnitMovementUnit* find_dropoff(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (context.callbacks.find_dropoff != nullptr) {
        return context.callbacks.find_dropoff(context, unit);
    }
    return has_movement(context) ?
        FindNearestOwnedDropoffBuilding(movement(context), unit).unit : nullptr;
}

bool TryStartNearbyFollowCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.definition.movement_class == 3 || unit.definition.movement_class == 1) {
        return false;
    }
    if ((unit.command_state & kUnitCommandDead) != 0 ||
        (unit.runtime_flags & kUnitMovementSkipMask) != 0) {
        return false;
    }

    UnitMovementUnit* target = find_nearby_follow_target(context, unit);
    if (target == nullptr ||
        target->command_state == kUnitStateTransportDockStart ||
        target->command_state == kUnitStateTransportDockSearch) {
        return false;
    }

    unit.command_state = kUnitStateTransportDockStart;
    unit.target = target;
    return true;
}

u32 legacy_transport_direction_remap(u32 direction) {
    switch (direction) {
    case 1: return 5;
    case 2: return 6;
    case 3: return 7;
    case 4: return 8;
    case 5: return 1;
    case 6: return 2;
    case 7: return 3;
    case 8: return 4;
    default: return 1;
    }
}

u32 legacy_random_relocation_direction(u32 direction) {
    static constexpr std::array<u8, 16> kDirectionCycle{
        0, 2, 3, 4, 5, 6, 7, 8,
        1, 0, 8, 1, 2, 3, 4, 5};
    return direction < kDirectionCycle.size() ? kDirectionCycle[direction] : 0;
}

u32 command_metadata_flags(UnitCommandContext& context, const UnitMovementUnit& unit) {
    if (context.callbacks.command_metadata_flags != nullptr) {
        return context.callbacks.command_metadata_flags(context, unit);
    }
    return GetUnitCommandMetadataFlags(unit);
}

u32 command_random_limit(UnitCommandContext& context, u32 limit) {
    if (limit == 0 || context.callbacks.random_limit == nullptr) {
        return 0;
    }
    return context.callbacks.random_limit(context, limit) % limit;
}

bool can_attack(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    if (context.callbacks.can_attack_target != nullptr) {
        return context.callbacks.can_attack_target(context, unit, target);
    }
    return target_alive(&target) && unit.owner_id != target.owner_id;
}

bool validate_action_target_for_runtime(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementUnit& target) {
    if (context.callbacks.validate_action_target != nullptr) {
        return context.callbacks.validate_action_target(context, unit, target);
    }
    return can_attack(context, unit, target);
}

bool can_follow(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    if (context.callbacks.can_follow_target != nullptr) {
        return context.callbacks.can_follow_target(context, unit, target);
    }
    return target_alive(&target);
}

void dispatch_attack(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (context.callbacks.dispatch_attack != nullptr) {
        context.callbacks.dispatch_attack(context, unit);
    }
}

void notify_target_validation_failed(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (unit.owner_id == context.local_owner_id &&
        context.callbacks.on_target_validation_failed != nullptr) {
        context.callbacks.on_target_validation_failed(context, unit);
    }
}

bool movement_step(UnitCommandContext& context, UnitMovementUnit& unit) {
    return has_movement(context) && ProcessUnitMovementStep(movement(context), unit);
}

const ProductionOrderRuntimeState& command_production_state_or_empty(
    const UnitCommandContext& context) {
    static const ProductionOrderRuntimeState empty_state{};
    if (context.production_state != nullptr) {
        return *context.production_state;
    }
    if (context.movement != nullptr && context.movement->production_state != nullptr) {
        return *context.movement->production_state;
    }
    return empty_state;
}

const UnitEquipmentCatalog* command_equipment_catalog(
    const UnitCommandContext& context) {
    if (context.equipment_catalog != nullptr) {
        return context.equipment_catalog;
    }
    return context.movement != nullptr ? context.movement->equipment_catalog : nullptr;
}

i32 command_additional_movement_modifier(const UnitCommandContext& context,
    const UnitMovementUnit& unit) {
    if (context.movement == nullptr || (unit.runtime_flags & 0x10000u) == 0) {
        return 0;
    }
    return context.movement->additional_movement_modifier;
}

UnitMovementPoint command_movement_frame_delta(UnitCommandContext& context,
    const UnitMovementUnit& unit) {
    return CalculateUnitMovementFrameDeltaWithProductionEffects(
        command_production_state_or_empty(context), unit,
        command_additional_movement_modifier(context, unit),
        command_equipment_catalog(context));
}

void copy_target_position_to_path(UnitMovementUnit& unit, const UnitMovementUnit& target) {
    unit.path_target_x = target.x;
    unit.path_target_y = target.y;
}

void path_to_target(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    copy_target_position_to_path(unit, target);
    unit.command_flags |= 8;
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

void path_to_target_without_command_flag(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementUnit& target) {
    copy_target_position_to_path(unit, target);
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

bool simple_target_in_attack_range(UnitMovementUnit& unit, UnitMovementUnit& target) {
    const u32 distance = CalculateApproxUnitDistance(unit.x, unit.y, target.x, target.y);
    const u32 range = std::max<u32>(unit.definition.range_threshold, 0x23);
    return distance <= range;
}

bool target_in_attack_range(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    if (context.callbacks.target_in_action_range != nullptr) {
        return context.callbacks.target_in_action_range(context, unit, target);
    }
    return simple_target_in_attack_range(unit, target);
}

bool target_in_transport_boarding_range(UnitMovementUnit& unit, UnitMovementUnit& target) {
    const u32 distance = CalculateApproxUnitDistance(unit.x, unit.y, target.x, target.y);
    return distance <= 0x20;
}

u32 unit_center_distance(const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    const UnitMovementPoint source_center = CalculateUnitCenterPoint(source);
    const UnitMovementPoint target_center = CalculateUnitCenterPoint(target);
    return CalculateApproxUnitDistance(source_center.x, source_center.y,
        target_center.x, target_center.y);
}

u32 command_interaction_range(UnitCommandContext& context, const UnitMovementUnit& unit) {
    return CalculateUnitInteractionRangeWithProductionAndEquipmentEffects(
        command_production_state_or_empty(context), unit,
        unit.definition.effect_adjusted_interaction_range_base,
        command_equipment_catalog(context));
}

bool target_in_follow_entry_range(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    const u32 distance = unit_center_distance(unit, target);
    u32 range = command_interaction_range(context, unit) >> 2;
    if (range <= 0x20) {
        range = 0x23;
    }
    return distance <= range;
}

bool target_in_follow_hold_range(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    const u32 distance = unit_center_distance(unit, target);
    const u32 interaction_range = command_interaction_range(context, unit);
    u32 range = (interaction_range >> 1) + (interaction_range >> 2);
    if (range < 0x21) {
        range = 0x23;
    }
    return distance <= range;
}

bool follow_target_path_point_enterable(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementUnit& target) {
    return !has_movement(context) ||
        CheckUnitCanEnterTerrainCell(movement(context), unit, target.current_cell_x,
            target.current_cell_y);
}

bool unit_can_carry(const UnitMovementUnit& unit) {
    constexpr u32 kTypeFlagCarrier = 0x400;
    return (unit.type_flags & kTypeFlagCarrier) != 0;
}

u32 transport_size(const UnitMovementUnit& unit) {
    return std::max<u32>(unit.definition.transport_size, 1);
}

bool unit_can_be_boarded(const UnitMovementUnit& unit) {
    constexpr u32 kTransportBoardableFlag = 4;
    return (unit.definition.transport_flags & kTransportBoardableFlag) != 0;
}

bool transport_capacity_allows(UnitCommandContext& context, UnitMovementUnit& carrier,
    UnitMovementUnit& passenger) {
    return carrier.cargo_amount + transport_size(passenger) <=
        CalculateUnitTransportCapacity(carrier, context.production_state);
}

bool can_start_transport_boarding(UnitCommandContext& context, UnitMovementUnit& carrier,
    UnitMovementUnit& passenger) {
    if (&carrier == &passenger || !target_alive(&carrier) || !target_alive(&passenger)) {
        return false;
    }
    if (!unit_can_carry(carrier) || unit_can_carry(passenger)) {
        return false;
    }
    return transport_capacity_allows(context, carrier, passenger);
}

bool can_continue_transport_boarding(UnitCommandContext& context, UnitMovementUnit& carrier,
    UnitMovementUnit& passenger) {
    if (!can_start_transport_boarding(context, carrier, passenger)) {
        return false;
    }
    if (passenger.command_state == kUnitStateTransportAttached ||
        (passenger.runtime_flags & 0x86) != 0) {
        return false;
    }
    return true;
}

bool can_board_transport(UnitCommandContext& context, UnitMovementUnit& carrier,
    UnitMovementUnit& passenger) {
    if (!can_continue_transport_boarding(context, carrier, passenger) ||
        !unit_can_be_boarded(passenger)) {
        return false;
    }
    if (context.callbacks.can_board_transport != nullptr) {
        return context.callbacks.can_board_transport(context, carrier, passenger);
    }
    return true;
}

bool command_is_reciprocal_boarding_candidate(UnitCommandContext& context,
    const UnitMovementUnit& unit) {
    return (command_metadata_flags(context, unit) & 1u) != 0;
}

void board_unit(UnitCommandContext& context, UnitMovementUnit& carrier,
    UnitMovementUnit& passenger) {
    carrier.cargo_amount += transport_size(passenger);
    passenger.target = &carrier;
    passenger.command_state = kUnitStateTransportAttached;
    passenger.attached_to_parent = true;
    passenger.runtime_flags &= ~1u;
    passenger.runtime_flags |= 0x80;
    passenger.command_flags &= ~0x1010u;
    passenger.work_timer = 0;
    if (context.callbacks.on_unit_boarded != nullptr) {
        context.callbacks.on_unit_boarded(context, carrier, passenger);
    }
}

void refresh_transport_boarding_path(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    if (unit.animation_frame != 0) {
        return;
    }
    path_to_target_without_command_flag(context, unit, target);
    unit.animation_frame = 0;
}

void advance_wrapping_command_frame(UnitMovementUnit& unit) {
    ++unit.animation_frame;
    if (unit.definition.animation_frame_count != 0 &&
        unit.animation_frame >= unit.definition.animation_frame_count) {
        unit.animation_frame = 0;
    }
}

void advance_reserved_tile_work_frame(UnitMovementUnit& unit) {
    ++unit.animation_frame;
}

bool consume_transport_unload_delay(UnitMovementUnit& carrier) {
    if (carrier.work_timer == 0) {
        return false;
    }
    --carrier.work_timer;
    return true;
}

bool place_unloaded_unit(UnitCommandContext& context, UnitMovementUnit& carrier,
    UnitMovementUnit& passenger) {
    UnitMovementPoint point{
        carrier.x + carrier.definition.transport_offset_x,
        carrier.y + carrier.definition.transport_offset_y};
    bool placement_found = false;
    if (context.callbacks.find_strict_placement_point != nullptr) {
        placement_found =
            context.callbacks.find_strict_placement_point(context, passenger, point);
    }
    else if (!has_movement(context) ||
        CheckUnitCanEnterTerrainCell(movement(context), passenger, point.x, point.y)) {
        placement_found = true;
    }
    if (!placement_found) {
        return false;
    }

    passenger.x = point.x;
    passenger.y = point.y;
    passenger.saved_path_target_x = point.x;
    passenger.saved_path_target_y = point.y;
    passenger.destination_x = point.x & ~0x1f;
    passenger.destination_y = point.y & ~0x1f;
    passenger.current_cell_x = point.x & ~0x1f;
    passenger.current_cell_y = point.y & ~0x1f;
    passenger.path_target_x = point.x;
    passenger.path_target_y = point.y;
    passenger.target = nullptr;
    passenger.command_state = 0;
    passenger.runtime_flags |= 1u;
    passenger.runtime_flags &= ~0x88u;
    passenger.attached_to_parent = false;
    if (carrier.cargo_amount >= transport_size(passenger)) {
        carrier.cargo_amount -= transport_size(passenger);
    }
    else {
        carrier.cargo_amount = 0;
    }
    carrier.work_timer = 2;
    if (context.callbacks.on_unit_unloaded != nullptr) {
        context.callbacks.on_unit_unloaded(context, carrier, passenger);
    }
    passenger.work_timer = 0;
    PopDeferredUnitCommandOrReturnIdle(context, passenger);
    return true;
}

void process_patrol_path(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

void begin_patrol_leg(UnitCommandContext& context, UnitMovementUnit& unit,
    i32 x, i32 y, u32 state, bool set_command_flag = false) {
    unit.target = nullptr;
    unit.path_target_x = x;
    unit.path_target_y = y;
    process_patrol_path(context, unit);
    unit.command_state = state;
    if (set_command_flag) {
        unit.command_flags |= 8u;
    }
}

void begin_patrol_target_leg(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit& target, u32 state) {
    copy_target_position_to_path(unit, target);
    process_patrol_path(context, unit);
    unit.command_state = state;
}

UnitMovementUnit* scan_patrol_target(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if ((unit.type_flags & 0x20u) == 0 &&
        (unit.script_bit_flags & 0x7242u) == 0) {
        return nullptr;
    }
    UnitMovementUnit* target = find_target(context, unit);
    if (target == nullptr || !can_attack(context, unit, *target)) {
        return nullptr;
    }
    unit.target = target;
    return target;
}

void enter_patrol_combat(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 combat_state) {
    unit.command_state = combat_state;
    unit.command_flags &= ~8u;
    if (combat_state == kUnitStatePatrolReturnCombat) {
        HandleUnitPatrolReturnCombatTarget(context, unit);
    }
    else {
        HandleUnitPatrolOutboundCombatTarget(context, unit);
    }
}

void handle_patrol_leg(UnitCommandContext& context, UnitMovementUnit& unit,
    i32 next_x, i32 next_y, u32 next_state, u32 combat_state) {
    scan_patrol_target(context, unit);
    if (unit.target != nullptr && can_attack(context, unit, *unit.target) &&
        target_in_attack_range(context, unit, *unit.target)) {
        enter_patrol_combat(context, unit, combat_state);
        return;
    }
    if (!movement_step(context, unit)) {
        if (unit.target != nullptr) {
            enter_patrol_combat(context, unit, combat_state);
            return;
        }
        begin_patrol_leg(context, unit, next_x, next_y, next_state);
    }
}

void handle_patrol_combat(UnitCommandContext& context, UnitMovementUnit& unit,
    i32 fallback_x, i32 fallback_y, u32 fallback_state) {
    // States 0x39/0x3a call FUN_004c1c87 before any outer target/range gate.
    if (context.callbacks.dispatch_attack != nullptr) {
        dispatch_attack(context, unit);
        return;
    }

    // Callback-free harness fallback.
    UnitMovementUnit* target = unit.target;
    if (target != nullptr && can_attack(context, unit, *target)) {
        if (target_in_attack_range(context, unit, *target)) {
            dispatch_attack(context, unit);
            return;
        }
        begin_patrol_target_leg(context, unit, *target, fallback_state);
        return;
    }

    UnitMovementUnit* replacement = find_target(context, unit);
    if (replacement != nullptr && can_attack(context, unit, *replacement)) {
        SetUnitCommandTarget(unit, replacement);
        if (target_in_attack_range(context, unit, *replacement)) {
            dispatch_attack(context, unit);
            return;
        }
        begin_patrol_target_leg(context, unit, *replacement, fallback_state);
        return;
    }

    begin_patrol_leg(context, unit, fallback_x, fallback_y, fallback_state, true);
}

UnitMovementUnit* find_attached_child(UnitCommandContext& context,
    UnitMovementUnit& carrier) {
    if (!has_movement(context)) {
        return nullptr;
    }
    for (UnitMovementUnit* unit : movement(context).active_units) {
        if (unit != nullptr && unit->command_state == kUnitStateTransportAttached &&
            unit->target == &carrier) {
            return unit;
        }
    }
    return nullptr;
}

UnitMovementUnit* find_unit_by_id(UnitCommandContext& context, u32 id) {
    if (!has_movement(context) || id == 0) {
        return nullptr;
    }
    for (UnitMovementUnit* unit : movement(context).active_units) {
        if (unit != nullptr && unit->id == id) {
            return unit;
        }
    }
    // Resurrection-style ability payloads deliberately name a corpse that is
    // no longer in active_units.  The original lookup walks the active list
    // first and then the lifecycle/free list, so retain that ordering when an
    // id happens to be stale or duplicated.
    for (UnitMovementUnit* unit : movement(context).lifecycle_units) {
        if (unit != nullptr && unit->id == id) {
            return unit;
        }
    }

    // Saved command payloads may still contain a scenario slot index, while
    // original network commands contain the corresponding byte offset in the
    // 0x1d0-byte unit pool.  Once runtime ids use the wire-compatible offset,
    // resolve both representations through the stable runtime slot.
    constexpr u32 kOriginalUnitPoolStride = 0x1d0u;
    constexpr u32 kOriginalUnitPoolSlotCount = 0x800u;
    u32 runtime_slot_index = kInvalidUnitRuntimeSlotIndex;
    if (id % kOriginalUnitPoolStride == 0) {
        const u32 candidate = id / kOriginalUnitPoolStride;
        if (candidate != 0 && candidate < kOriginalUnitPoolSlotCount) {
            runtime_slot_index = candidate;
        }
    }
    else if (id < kOriginalUnitPoolSlotCount) {
        runtime_slot_index = id;
    }
    if (runtime_slot_index == kInvalidUnitRuntimeSlotIndex) {
        return nullptr;
    }

    for (UnitMovementUnit* unit : movement(context).active_units) {
        if (unit != nullptr && unit->runtime_slot_index == runtime_slot_index) {
            return unit;
        }
    }
    for (UnitMovementUnit* unit : movement(context).lifecycle_units) {
        if (unit != nullptr && unit->runtime_slot_index == runtime_slot_index) {
            return unit;
        }
    }
    return nullptr;
}

UnitMovementUnit* resolve_command_target(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (unit.target == nullptr && unit.command_value != 0) {
        unit.target = find_unit_by_id(context, unit.command_value);
    }
    return unit.target;
}

u32 active_payload_target_id(const UnitMovementUnit& unit) {
    return static_cast<u32>(unit.active_command_payload.x);
}

UnitMovementUnit* resolve_command_payload_target(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    unit.target = nullptr;
    unit.target = find_unit_by_id(context, active_payload_target_id(unit));
    return unit.target;
}

bool command_payload_carries_target_id(u32 command_state) {
    if ((command_state & kUnitCommandDeferredEntryFlag) != 0) {
        return true;
    }
    switch (command_state & kUnitCommandStateMask) {
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x0a:
    case 0x0b:
    case 0x0d:
    case 0x16:
    case 0x23:
        return true;
    default:
        return false;
    }
}

UnitMovementUnit* resolve_active_payload_target(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    const u32 target_id = active_payload_target_id(unit);
    if (target_id == 0) {
        return unit.target;
    }
    unit.target = find_unit_by_id(context, target_id);
    return unit.target;
}

UnitMovementUnit* resolve_active_payload_target_or_clear(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    const u32 target_id = active_payload_target_id(unit);
    unit.target = target_id != 0 ? find_unit_by_id(context, target_id) : nullptr;
    return unit.target;
}

enum class UnitActionTargetStatus {
    invalid,
    needs_approach,
    ready,
};

UnitActionTargetStatus validate_action_target(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_command_target(context, unit);
    if (target == nullptr || !can_attack(context, unit, *target)) {
        return UnitActionTargetStatus::invalid;
    }
    return target_in_attack_range(context, unit, *target) ?
        UnitActionTargetStatus::ready : UnitActionTargetStatus::needs_approach;
}

void set_anchor_to_current_position(UnitMovementUnit& unit) {
    unit.anchor_x = unit.x;
    unit.anchor_y = unit.y;
}

void set_saved_anchor_from_current_command(UnitMovementUnit& unit) {
    // Each new guard command replaces the raw active command tuple.  The
    // original state-0x1c/0x1f entries copy that tuple unconditionally, so a
    // repeated point order must replace the preceding return anchor.
    unit.saved_path_target_x = unit.path_target_x;
    unit.saved_path_target_y = unit.path_target_y;
}

void path_to_saved_anchor(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.path_target_x = unit.saved_path_target_x;
    unit.path_target_y = unit.saved_path_target_y;
    unit.command_flags |= 8;
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

void mark_equipment_slots_changed(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    unit.equipment_flags |= 1;
    if (context.callbacks.on_equipment_slots_changed != nullptr) {
        context.callbacks.on_equipment_slots_changed(context, unit);
    }
}

bool try_collect_map_effect_at_command_point(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.map_effects == nullptr || context.equipment_catalog == nullptr) {
        return false;
    }
    return TryCollectUnitEquipmentFromMapEffects(context, *context.map_effects,
        unit, unit.path_target_x, unit.path_target_y,
        *context.equipment_catalog);
}

bool try_start_idle_map_effect_interaction(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.map_effects == nullptr || context.equipment_catalog == nullptr ||
        (unit.type_flags & 0x80000000u) == 0 ||
        (unit.type_flags & kUnitEquipmentPickupEnabledFlag) == 0) {
        return false;
    }

    MapEffectInstance* effect =
        FindNearbyInteractableMapEffectForUnit(*context.map_effects, unit);
    if (effect == nullptr) {
        return false;
    }

    unit.active_command_payload.x = static_cast<i32>(effect->id);
    unit.path_target_x = effect->x;
    unit.path_target_y = effect->y;

    // The original idle branch only links and enters state 5.  Even when the
    // effect is already in the unit's tile, collection occurs on the next
    // state-5 dispatch.  An immobile unit that still needs an approach falls
    // through to ordinary enemy acquisition without claiming the effect.
    if (!CheckPathTargetWithinAxisTile(unit) &&
        (unit.runtime_flags & 8u) != 0) {
        return false;
    }

    effect->flags = kMapEffectLinkedFlag;
    effect->linked_unit = &unit;
    unit.command_state = kUnitStateAssistTarget;
    return true;
}

void complete_point_equipment_to_idle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    try_collect_map_effect_at_command_point(context, unit);
    mark_equipment_slots_changed(context, unit);
    StartUnitCommandLockoutTimer(context, unit, 2);
    HandleUnitReturnToIdleState(context, unit);
}

void anchor_and_pop_deferred(UnitCommandContext& context, UnitMovementUnit& unit) {
    set_anchor_to_current_position(unit);
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void complete_point_equipment_and_pop(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    try_collect_map_effect_at_command_point(context, unit);
    mark_equipment_slots_changed(context, unit);
    anchor_and_pop_deferred(context, unit);
}

UnitMovementPoint unit_target_center_or_path(const UnitMovementUnit& unit) {
    if (unit.target == nullptr) {
        return UnitMovementPoint{unit.path_target_x, unit.path_target_y};
    }
    return CalculateUnitCenterPoint(*unit.target);
}

void path_to_current_target_center(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    const UnitMovementPoint center = unit_target_center_or_path(unit);
    unit.path_target_x = center.x;
    unit.path_target_y = center.y;
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

UnitTargetHelperContext make_target_progress_context(UnitCommandContext& context) {
    UnitTargetHelperContext helper;
    helper.movement_context = context.movement;
    helper.local_owner_id = context.local_owner_id;
    for (std::size_t i = 0; i < helper.owner_primary_progress.size() &&
        i < context.owner_resources.size(); ++i) {
        helper.owner_primary_progress[i] = context.owner_resources[i];
        helper.owner_secondary_progress[i] = context.owner_secondary_resources[i];
    }
    helper.callbacks.on_target_progress = context.callbacks.on_target_progress;
    helper.callbacks.on_target_progress_complete =
        context.callbacks.on_target_progress_complete;
    helper.callbacks.on_local_progress_blocked =
        context.callbacks.on_local_target_progress_blocked;
    return helper;
}

void copy_target_progress_resources_back(UnitCommandContext& context,
    const UnitTargetHelperContext& helper) {
    for (std::size_t i = 0; i < helper.owner_primary_progress.size() &&
        i < context.owner_resources.size(); ++i) {
        context.owner_resources[i] = helper.owner_primary_progress[i];
        context.owner_secondary_resources[i] = helper.owner_secondary_progress[i];
    }
}

UnitTargetProgressResult apply_target_progress(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitTargetHelperContext helper = make_target_progress_context(context);
    UnitTargetProgressResult result =
        ApplyCurrentTargetBuildOrRepairProgress(helper, unit);
    copy_target_progress_resources_back(context, helper);
    return result;
}

u32 target_priority(const UnitMovementUnit& unit) {
    return unit.definition.target_selection_priority != 0xffffffffu
        ? unit.definition.target_selection_priority
        : unit.type_id;
}

bool prefer_guard_target(const UnitMovementUnit* candidate,
    const UnitMovementUnit* current) {
    if (candidate == nullptr) {
        return false;
    }
    if (current == nullptr || candidate == current) {
        return true;
    }
    return target_priority(*candidate) < target_priority(*current);
}

bool has_reserved_tile_linked_object(const UnitMovementUnit& unit) {
    const UnitEffectRuntime* effect = unit.reserved_tile_effect;
    return effect != nullptr && effect->active &&
        effect->effect_id == kReservedTileCompletionEffectId &&
        effect->source_unit_id == unit.id;
}

u32 construction_health_step(const UnitMovementUnit& unit) {
    // FUN_004c9f9d reads the construction increment directly from definition
    // +0x154 (DAT_0087c44c).  Runtime max-health effects do not substitute for
    // a zero definition value during construction.
    return unit.definition.initial_max_health;
}

void seed_construction_progress(UnitMovementUnit& unit) {
    if (!unit.under_construction && unit.action_mode_gate != 1) {
        return;
    }
    const u32 step = construction_health_step(unit);
    unit.action_mode = unit.definition.production_spawn_time / 10u;
    unit.runtime_stat_28 = unit.action_mode * step;
    if (unit.health == 0) {
        unit.health = 1;
    }
}

UnitMovementUnit* create_legacy_spawned_unit(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 type_id) {
    if (context.callbacks.create_unit == nullptr) {
        return nullptr;
    }
    // Original FUN_004c9e8f loads the placement coordinates from the active
    // command tuple (+0xdc/+0xe0) immediately before
    // InitializePlacedUnitFromMapSlot (0x004c9ef3..0x004c9f06).  path_target
    // has already been shifted by half the footprint solely so the worker can
    // approach the building center; using it here shifts the actual structure
    // away from the tile preview and wire-command position.
    // Original construction sets DAT_0072d99c to the worker before calling
    // InitializePlacedUnitFromMapSlot, making placement ignore that source
    // object. Legacy workers approach the footprint centre, so without this
    // exemption the reconstructed occupancy scan rejects its own builder.
    const bool already_placement_ignored = (unit.runtime_flags & 0x80u) != 0;
    unit.runtime_flags |= 0x80u;
    UnitMovementUnit* spawned = context.callbacks.create_unit(context, unit,
        type_id, unit.active_command_payload.y,
        static_cast<i32>(unit.active_command_payload.value));
    if (!already_placement_ignored) {
        unit.runtime_flags &= ~0x80u;
    }
    if (spawned == nullptr) {
        return nullptr;
    }
    spawned->owner_id = unit.owner_id;
    spawned->target = &unit;
    spawned->animation_frame = 0;
    spawned->under_construction = true;
    seed_construction_progress(*spawned);
    unit.target = spawned;
    // FUN_004ca105 (0x004ca13b..0x004ca145) stores the new structure's pool
    // offset in the builder's shared raw +0x68 target/value word while the
    // reverse link stores the builder offset in the structure.  Keep the
    // produced type in spawn_type_id and mirror that persistent forward link.
    unit.command_value = spawned->id;
    if (context.callbacks.on_unit_spawned != nullptr) {
        context.callbacks.on_unit_spawned(context, unit, *spawned);
    }
    return spawned;
}

u32 spawn_cycle_duration(const UnitMovementUnit& unit) {
    return unit.definition.spawn_frame_count;
}

u32 ability_cycle_duration(const UnitMovementUnit& unit) {
    return unit.definition.ability_timer_period;
}

u32 timed_phase_a_duration(const UnitMovementUnit& unit) {
    return unit.definition.timed_flag_phase_a_period;
}

u32 timed_phase_b_duration(const UnitMovementUnit& unit) {
    return unit.definition.timed_flag_phase_b_period;
}

u32 completion_effect_duration(const UnitMovementUnit& unit) {
    return unit.definition.completion_effect_period;
}

u32 completion_announcement_duration(const UnitMovementUnit& unit) {
    return unit.definition.completion_announcement_period;
}

u32 production_cycle_duration(const UnitMovementUnit& unit) {
    return unit.definition.production_cycle_period;
}

u32 production_spawn_duration(const UnitMovementUnit& unit) {
    return unit.definition.production_spawn_time;
}

bool advance_idle_frame(UnitMovementUnit& unit, u32 frame_limit) {
    if (frame_limit == 0) {
        return false;
    }
    ++unit.animation_frame;
    if (unit.animation_frame >= frame_limit) {
        unit.animation_frame = 0;
        return true;
    }
    return false;
}

bool try_start_idle_random_relocation(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if ((unit.type_flags & 0x10u) == 0 || unit.owner_id != 8 ||
        !has_movement(context)) {
        return false;
    }

    // Original ProcessUnitIdleAcquireCommand (0x004c921a..0x004c9272): each
    // elapsed idle period consumes one limit-10 roll and relocates only on 8.
    // The two axes then consume limit-0xc0 rolls, quantized to 32-pixel steps
    // around the unit's anchor.  Using the generic +/-0x40 relocation callback
    // here changes both the deterministic RNG stream and neutral movement.
    if (command_random_limit(context, 10) != 8) {
        return false;
    }

    UnitMovementMap& map = movement(context).map;
    if (map.width == 0 || map.height == 0) {
        return false;
    }
    const auto relocation_axis = [&](i32 anchor, u32 tile_extent) {
        const i32 offset = static_cast<i32>(
            command_random_limit(context, 0xc0) & ~0x1fu) - 0x60;
        const i64 extent_pixels = static_cast<i64>(tile_extent) * 0x20;
        const i64 target = static_cast<i64>(anchor) + offset;
        if (target < 0) {
            return 0;
        }
        // Original 0x004c9243/0x004c9261 preserves sub-tile coordinates in
        // the final map cell.  It moves to extent-32 only when the generated
        // coordinate is at/after the full pixel extent; clamping every value
        // to extent-32 turns a valid 3071 target into 3040 on a 96-tile map.
        if (extent_pixels <= target) {
            return static_cast<i32>(std::max<i64>(extent_pixels - 0x20, 0));
        }
        return static_cast<i32>(std::min<i64>(
            target, std::numeric_limits<i32>::max()));
    };

    // The original rolls Y first, then X.
    const i32 target_y = relocation_axis(unit.anchor_y, map.height);
    const i32 target_x = relocation_axis(unit.anchor_x, map.width);

    unit.target = nullptr;
    unit.path_target_x = target_x;
    unit.path_target_y = target_y;
    unit.command_state = kUnitStateTravel;
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
    return true;
}

u32 runtime_state(const UnitCommandContext& context, const UnitMovementUnit& unit) {
    const u32 command_id = GetUnitCommandIdLow24(unit);
    if (context.command_runtime_state_table != nullptr &&
        command_id < context.command_runtime_state_table->size()) {
        return (*context.command_runtime_state_table)[command_id];
    }
    return ResolveUnitRuntimeStateFromCommandTable(unit);
}

u32 spawn_type_for_unit(const UnitMovementUnit& unit, u32 fallback) {
    if (unit.spawn_type_id != 0) {
        return unit.spawn_type_id;
    }
    if (fallback != 0) {
        return fallback;
    }
    return unit.type_id + 0x60;
}

const UnitMovementDefinition& definition_for_type_or(UnitCommandContext& context,
    u32 type_id, const UnitMovementDefinition& fallback) {
    if (context.callbacks.find_definition == nullptr) {
        return fallback;
    }
    const UnitMovementDefinition* definition =
        context.callbacks.find_definition(context, type_id);
    return definition != nullptr ? *definition : fallback;
}

u32 production_type_for_unit(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (context.callbacks.production_type_id != nullptr) {
        const u32 type_id = context.callbacks.production_type_id(context, unit);
        const u32 command_id = GetUnitCommandIdLow24(unit);
        if (type_id != 0 || command_id == kUnitStateProductionSpawnStart ||
            command_id == kUnitStateProductionSpawnCycle) {
            return type_id;
        }
    }
    if (unit.spawn_type_id != 0) {
        return unit.spawn_type_id;
    }
    return unit.type_id;
}

u32 production_resource_cost_for_unit(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.production_resource_cost != nullptr) {
        return context.callbacks.production_resource_cost(context, unit);
    }
    return unit.definition.production_resource_cost;
}

u32 production_secondary_cost_for_unit(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.production_secondary_cost != nullptr) {
        return context.callbacks.production_secondary_cost(context, unit);
    }
    return unit.definition.production_secondary_cost;
}

u32 production_population_cost_for_unit(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.production_population_cost != nullptr) {
        return context.callbacks.production_population_cost(context, unit);
    }
    return unit.definition.production_population_cost;
}

struct UnitProductionResourceCheck {
    bool available = true;
    UnitProductionStartFailure failure = UnitProductionStartFailure::none;
};

bool has_owner_slot(const UnitCommandContext& context, u32 owner_id) {
    return owner_id < context.owner_resources.size();
}

UnitProductionResourceCheck check_production_resources(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitProductionResourceCheck result;
    if (!has_owner_slot(context, unit.owner_id)) {
        return result;
    }

    const u32 owner = unit.owner_id;
    const u32 resource_cost = production_resource_cost_for_unit(context, unit);
    const u32 secondary_cost = production_secondary_cost_for_unit(context, unit);
    const u32 population_cost = production_population_cost_for_unit(context, unit);
    if (context.owner_resources[owner] < resource_cost) {
        result.available = false;
        result.failure = UnitProductionStartFailure::primary_resources;
        return result;
    }
    if (context.owner_secondary_resources[owner] < secondary_cost) {
        result.available = false;
        result.failure = UnitProductionStartFailure::secondary_resources;
        return result;
    }

    const u32 projected_population =
        context.owner_population_reserved[owner] + population_cost;
    if (context.owner_population_limit[owner] < projected_population) {
        result.available = false;
        result.failure = UnitProductionStartFailure::population_limit;
        return result;
    }
    if (context.owner_population_used[owner] < projected_population) {
        result.available = false;
        result.failure = UnitProductionStartFailure::population_capacity;
        return result;
    }
    return result;
}

UnitProductionResourceCheck check_production_population_capacity(
    UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitProductionResourceCheck result;
    if (!has_owner_slot(context, unit.owner_id)) {
        return result;
    }

    const u32 owner = unit.owner_id;
    const u32 projected_population =
        context.owner_population_reserved[owner] +
        production_population_cost_for_unit(context, unit);
    if (context.owner_population_limit[owner] < projected_population) {
        result.available = false;
        result.failure = UnitProductionStartFailure::population_limit;
        return result;
    }
    if (context.owner_population_used[owner] < projected_population) {
        result.available = false;
        result.failure = UnitProductionStartFailure::population_capacity;
    }
    return result;
}

bool has_production_resources(UnitCommandContext& context, UnitMovementUnit& unit) {
    return check_production_resources(context, unit).available;
}

void reserve_production_resources(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_owner_slot(context, unit.owner_id)) {
        unit.production_reserved = true;
        return;
    }

    const u32 owner = unit.owner_id;
    HandleOwnerUnitProductionCostDebit(context, owner,
        production_resource_cost_for_unit(context, unit),
        production_secondary_cost_for_unit(context, unit));
    context.owner_population_reserved[owner] +=
        production_population_cost_for_unit(context, unit);
    unit.production_reserved = true;
}

void reserve_production_population(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (has_owner_slot(context, unit.owner_id)) {
        context.owner_population_reserved[unit.owner_id] +=
            production_population_cost_for_unit(context, unit);
    }
    unit.production_reserved = true;
}

void release_production_population(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (!unit.production_reserved) {
        return;
    }
    if (has_owner_slot(context, unit.owner_id)) {
        const u32 owner = unit.owner_id;
        const u32 population_cost = production_population_cost_for_unit(context, unit);
        context.owner_population_reserved[owner] =
            context.owner_population_reserved[owner] >= population_cost ?
            context.owner_population_reserved[owner] - population_cost : 0;
    }
    unit.production_reserved = false;
}

void refund_production_resources(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!unit.production_reserved) {
        return;
    }

    if (has_owner_slot(context, unit.owner_id)) {
        const u32 owner = unit.owner_id;
        HandleOwnerUnitProductionCostRefund(context, owner,
            production_resource_cost_for_unit(context, unit),
            production_secondary_cost_for_unit(context, unit));
    }

    release_production_population(context, unit);
    if (context.callbacks.on_production_refunded != nullptr) {
        context.callbacks.on_production_refunded(context, unit);
    }
}

u32 building_primary_resource_cost(UnitCommandContext& context,
    const UnitMovementUnit& unit, u32 type_id) {
    const UnitMovementDefinition& definition =
        definition_for_type_or(context, type_id, unit.definition);
    return definition.production_resource_cost;
}

bool reserve_building_primary_resource(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 type_id) {
    if (!has_owner_slot(context, unit.owner_id)) {
        unit.production_reserved = true;
        return true;
    }
    const u32 cost = building_primary_resource_cost(context, unit, type_id);
    u32& resources = context.owner_resources[unit.owner_id];
    if (resources < cost) {
        return false;
    }
    resources -= cost;
    unit.production_reserved = true;
    return true;
}

void refund_building_primary_resource(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 type_id) {
    if (!unit.production_reserved) {
        return;
    }
    if (has_owner_slot(context, unit.owner_id)) {
        context.owner_resources[unit.owner_id] +=
            building_primary_resource_cost(context, unit, type_id);
    }
    unit.production_reserved = false;
    if (context.callbacks.on_production_refunded != nullptr) {
        context.callbacks.on_production_refunded(context, unit);
    }
}

void refund_active_production_on_death(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (!unit.production_reserved) {
        return;
    }

    // Original HandleUnitPrimaryResourceCostRefund (0x004ce4bd), used by
    // HandleUnitDeathCommandQueueSideEffects, restores only the primary unit
    // cost.  The normal placement-failure path above restores both costs.
    if (has_owner_slot(context, unit.owner_id)) {
        context.owner_resources[unit.owner_id] +=
            production_resource_cost_for_unit(context, unit);
    }
    release_production_population(context, unit);
    if (context.callbacks.on_production_refunded != nullptr) {
        context.callbacks.on_production_refunded(context, unit);
    }
}

void notify_production_start_failed_once(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitProductionStartFailure failure) {
    if (unit.owner_id != context.local_owner_id ||
        unit.item_slots.size() <= 2 || unit.item_slots[2] == 1) {
        return;
    }

    unit.item_slots[2] = 1;
    if (unit.equipment_slots.size() > 2) {
        unit.equipment_slots[2] = unit.item_slots[2];
    }
    if (context.callbacks.on_production_start_failed_reason != nullptr) {
        context.callbacks.on_production_start_failed_reason(context, unit, failure);
        return;
    }
    if (context.callbacks.on_production_start_failed != nullptr) {
        context.callbacks.on_production_start_failed(context, unit);
    }
}

void pop_completed_runtime_state(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.command_flags &= ~0x10u;
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void offset_spawn_target_by_footprint(UnitMovementUnit& unit,
    const UnitMovementDefinition& definition) {
    unit.path_target_x += static_cast<i32>(definition.footprint_width_tiles * 16);
    unit.path_target_y += static_cast<i32>(definition.footprint_height_tiles * 16);
}

void offset_spawn_target_by_interaction_bounds(UnitMovementUnit& unit,
    const UnitMovementDefinition& definition) {
    // Original state 0x23 (FUN_004c9e8f) centers the approach point with
    // definition +0x378/+0x37c, not the tile-footprint dimensions.
    unit.path_target_x += definition.interaction_bounds_width >> 1;
    unit.path_target_y += definition.interaction_bounds_height >> 1;
}

bool targeted_spawn_needs_approach(UnitMovementUnit& unit, UnitMovementUnit& target) {
    if (!CheckCurrentTargetOutsideExpandedFootprint(unit)) {
        return false;
    }
    const UnitMovementPoint center = CalculateUnitCenterPoint(target);
    unit.path_target_x = center.x;
    unit.path_target_y = center.y;
    return true;
}

void enter_spawn_cycle(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 state) {
    unit.command_state = state;
    unit.animation_frame = 0;
    unit.work_timer = 0;
    unit.direction = CalculateUnitDirectionToPoint(unit, unit.path_target_x,
        unit.path_target_y);
}

bool create_spawned_unit(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 type_id) {
    if (context.callbacks.create_unit == nullptr) {
        return false;
    }

    UnitMovementUnit* spawned = context.callbacks.create_unit(context, unit, type_id,
        unit.path_target_x, unit.path_target_y);
    if (spawned == nullptr) {
        return false;
    }

    spawned->target = &unit;
    spawned->owner_id = unit.owner_id;
    seed_construction_progress(*spawned);
    SetUnitCommandTarget(unit, spawned);
    if (context.callbacks.on_unit_spawned != nullptr) {
        context.callbacks.on_unit_spawned(context, unit, *spawned);
    }
    return true;
}

UnitMovementUnit* create_production_unit(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 type_id) {
    if (context.callbacks.create_unit == nullptr) {
        return nullptr;
    }

    // Original HandleUnitProductionSpawnCycle (0x004ce0d5..0x004ce0ed) seeds
    // placement from the producer position plus definition +0x2410/+0x2414.
    // path_target is the rally/command destination and must not be used as a
    // spawn point.
    UnitMovementUnit* produced = context.callbacks.create_unit(
        context, unit, type_id,
        unit.x + unit.definition.transport_offset_x,
        unit.y + unit.definition.transport_offset_y);
    if (produced == nullptr) {
        return nullptr;
    }

    produced->owner_id = unit.owner_id;
    produced->target = nullptr;
    if (produced->max_health != 0 && produced->health == 0) {
        produced->health = produced->max_health;
    }
    if (context.callbacks.on_unit_spawned != nullptr) {
        context.callbacks.on_unit_spawned(context, unit, *produced);
    }
    return produced;
}

bool grow_spawned_unit(UnitMovementUnit& spawned) {
    const u32 duration = production_spawn_duration(spawned);
    const u32 step = construction_health_step(spawned);
    if (spawned.action_mode < duration) {
        ++spawned.action_mode;
        spawned.runtime_stat_28 += step;
        while (true) {
            if (spawned.runtime_stat_28 < duration) {
                return false;
            }
            if (step <= spawned.health) {
                break;
            }
            spawned.runtime_stat_28 -= duration;
            ++spawned.health;
        }
    }
    return true;
}

void complete_spawned_construction(UnitCommandContext& context,
    UnitMovementUnit& spawned) {
    // Original HandleUnitCreationRegisterFootprint (0x004ce42d) deliberately
    // leaves the construction accumulator and action counter intact.  They are
    // runtime unit stats after completion; clearing them here changed both
    // subsequent simulation and the sprite/status state of completed buildings.
    if (context.callbacks.on_construction_completed != nullptr) {
        context.callbacks.on_construction_completed(context, spawned);
        return;
    }

    spawned.under_construction = false;
    spawned.action_mode_gate = 0;
    spawned.linked_object_id = spawned.id;
    spawned.linked_unit = &spawned;
    spawned.next_path_x = spawned.current_cell_x;
    spawned.next_path_y = spawned.current_cell_y;
    spawned.saved_path_target_x = spawned.current_cell_x;
    spawned.saved_path_target_y = spawned.current_cell_y;
    if ((spawned.definition.footprint_flags & 2u) != 0) {
        spawned.command_flags |= 0x40u;
    }
    if (context.callbacks.set_footprint != nullptr) {
        context.callbacks.set_footprint(context, spawned);
    }
}

void start_produced_unit_rally_command(UnitCommandContext& context,
    const UnitMovementUnit& producer, UnitMovementUnit& produced) {
    // Original HandleUnitProductionSpawnCycle (0x004ce182..0x004ce1b3)
    // copies the producer's saved rally target and point to the new unit and
    // enters command state 0x14.  Without this, trained workers remain at the
    // producer even when the player has set a rally point.
    if (producer.linked_object_id == producer.id) {
        return;
    }

    produced.target = producer.linked_object_id != 0 ?
        find_unit_by_id(context, producer.linked_object_id) : nullptr;
    produced.path_target_x = producer.saved_path_target_x;
    produced.path_target_y = producer.saved_path_target_y;
    StartUnitState14AndClearRuntimeFlag(context, produced);
}

void handle_spawn_cycle(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 type_id) {
    UnitMovementUnit* spawned = unit.target;
    if (spawned != nullptr && spawned->target == &unit && target_alive(spawned)) {
        if (grow_spawned_unit(*spawned)) {
            complete_spawned_construction(context, *spawned);
            unit.production_reserved = false;
            PopDeferredUnitCommandOrReturnIdle(context, unit);
        }
        return;
    }

    ++unit.animation_frame;
    if (unit.animation_frame < spawn_cycle_duration(unit)) {
        return;
    }

    unit.animation_frame = 0;
    if (!reserve_building_primary_resource(context, unit, type_id)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!create_spawned_unit(context, unit, type_id)) {
        refund_building_primary_resource(context, unit, type_id);
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

bool ability_target_in_range(UnitMovementUnit& unit, UnitMovementUnit* target) {
    if (target != nullptr) {
        return simple_target_in_attack_range(unit, *target);
    }
    const u32 distance = CalculateApproxUnitDistance(unit.x, unit.y,
        unit.path_target_x, unit.path_target_y);
    return distance <= std::max<u32>(unit.definition.range_threshold, 0x23);
}

bool can_use_ability(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit* target) {
    if (context.callbacks.can_use_ability != nullptr) {
        return context.callbacks.can_use_ability(context, unit, target, unit.ability_id);
    }
    return target == nullptr || target_alive(target);
}

UnitCommandAbilityGateResult check_ability_gate(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementUnit* target) {
    if (context.callbacks.ability_gate != nullptr) {
        return context.callbacks.ability_gate(context, unit, target,
            unit.ability_id);
    }
    if (!can_use_ability(context, unit, target)) {
        return UnitCommandAbilityGateResult::fail;
    }
    return ability_target_in_range(unit, target) ?
        UnitCommandAbilityGateResult::ready :
        UnitCommandAbilityGateResult::approach;
}

void execute_ability(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit* target) {
    if (context.callbacks.execute_ability != nullptr) {
        context.callbacks.execute_ability(context, unit, target, unit.ability_id);
    }
}

bool start_ability_attachment(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit* target) {
    if (context.callbacks.start_ability_attachment != nullptr) {
        return context.callbacks.start_ability_attachment(context, unit, target,
            unit.ability_id);
    }
    return true;
}

void set_unit_type_for_command(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 type_id) {
    unit.type_id = type_id;
    if (context.callbacks.find_definition == nullptr) {
        return;
    }
    const UnitMovementDefinition* definition =
        context.callbacks.find_definition(context, type_id);
    if (definition != nullptr) {
        unit.definition = *definition;
    }
}

u32 ability_secondary_cost(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit* target) {
    if (context.callbacks.ability_secondary_cost != nullptr) {
        return context.callbacks.ability_secondary_cost(context, unit, target,
            unit.ability_id);
    }
    return 1;
}

i32 ability_target_health_delta(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit* target) {
    if (context.callbacks.ability_target_health_delta != nullptr) {
        return context.callbacks.ability_target_health_delta(context, unit, target,
            unit.ability_id);
    }
    return 1;
}

bool ability_updates_direction(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit* target) {
    if (context.callbacks.ability_updates_direction != nullptr) {
        return context.callbacks.ability_updates_direction(context, unit, target,
            unit.ability_id);
    }
    return true;
}

void enter_special_ability_timer(UnitCommandContext& context, UnitMovementUnit& unit,
    UnitMovementUnit* target) {
    unit.command_state = kUnitStateSpecialAbilityTimer;
    unit.command_flags |= 0x10;
    unit.animation_frame = 0;
    if (ability_updates_direction(context, unit, target)) {
        unit.direction = CalculateUnitDirectionToPoint(unit, unit.path_target_x,
            unit.path_target_y);
    }
}

void path_to_ability_target(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.target != nullptr) {
        const UnitMovementPoint center = CalculateUnitCenterPoint(*unit.target);
        unit.path_target_x = center.x;
        unit.path_target_y = center.y;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        return;
    }
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

constexpr u32 kItemSlotUsableItemId = 0x1b;
constexpr u32 kItemSlotUseActionEffectId = 0x25;

u32* selected_item_slot(UnitMovementUnit& unit) {
    if (unit.command_value <= 2) {
        return nullptr;
    }
    if (unit.command_value == 3) {
        return &unit.item_slots[0];
    }
    if (unit.command_value == 4) {
        return &unit.item_slots[1];
    }
    if (unit.command_value == 5) {
        return &unit.item_slots[2];
    }
    return &unit.item_slots[3];
}

bool execute_item_slot_use(UnitCommandContext& context, UnitMovementUnit& unit,
    u32 item_id) {
    if (item_id != kItemSlotUsableItemId) {
        return false;
    }
    if (context.callbacks.use_item_slot != nullptr) {
        return context.callbacks.use_item_slot(context, unit, kItemSlotUseActionEffectId,
            unit.path_target_x, unit.path_target_y);
    }
    return true;
}

bool linked_release_cycle_unit_alive(const UnitMovementUnit* unit) {
    return unit != nullptr && unit->active &&
        (unit->command_state & kUnitCommandDead) == 0 &&
        (unit->runtime_flags & 4) == 0;
}

UnitRuntimeStatBlock linked_release_runtime_stats(const UnitMovementUnit& unit) {
    UnitRuntimeStatBlock stats{};
    stats.max_health = unit.max_health;
    stats.max_secondary_value = unit.max_secondary_value;
    stats.health = unit.health;
    stats.stat_1c = unit.runtime_stat_1c;
    stats.stat_20 = unit.runtime_stat_20;
    stats.secondary_value = unit.secondary_value;
    stats.stat_28 = unit.runtime_stat_28;
    return stats;
}

void release_linked_unit(UnitCommandContext& context, UnitMovementUnit& parent,
    UnitMovementUnit& child) {
    child.runtime_flags |= 1;
    child.runtime_flags &= ~0x80u;
    child.attached_to_parent = false;
    child.command_state = 0;
    child.target = nullptr;
    if (context.callbacks.on_linked_unit_detached != nullptr) {
        context.callbacks.on_linked_unit_detached(context, parent, child);
    }
}

void enter_linked_release_child_cycle(UnitCommandContext& context,
    UnitMovementUnit& parent, UnitMovementUnit& child) {
    child.command_state = kUnitStateLinkedUnitReleaseCycle;
    child.animation_frame = 0;
    child.command_value = 0;
    child.runtime_flags &= ~1u;
    child.runtime_flags |= 0x80;
    child.attached_to_parent = true;
    child.scenario_string_slot &= ~0x80u;
    if (context.callbacks.on_linked_unit_attached != nullptr) {
        context.callbacks.on_linked_unit_attached(context, parent, child);
    }
}

} // namespace

u32 GetUnitCommandIdLow24(const UnitMovementUnit& unit) {
    return unit.command_state & 0x00ffffffu;
}

u32 ResolveUnitRuntimeStateFromCommandTable(
    const UnitMovementUnit& unit,
    const std::vector<u32>* command_state_table) {
    const u32 command_id = GetUnitCommandIdLow24(unit);
    if (command_state_table != nullptr && command_id < command_state_table->size()) {
        return (*command_state_table)[command_id];
    }
    return command_id & 0xffu;
}

u32 GetUnitCommandMetadataFlags(const UnitMovementUnit& unit,
    const std::vector<u32>* command_metadata_table) {
    const u32 command_id = GetUnitCommandIdLow24(unit);
    if (command_metadata_table != nullptr &&
        command_id < command_metadata_table->size()) {
        return (*command_metadata_table)[command_id];
    }
    return 0;
}

void SetUnitCommandTarget(UnitMovementUnit& unit, UnitMovementUnit* target) {
    unit.target = target;
    if (target != nullptr) {
        unit.path_target_x = target->x;
        unit.path_target_y = target->y;
    }
}

void SetUnitPathTarget(UnitMovementUnit& unit, i32 x, i32 y) {
    unit.target = nullptr;
    unit.path_target_x = x;
    unit.path_target_y = y;
}

bool CheckPathTargetWithinAxisTile(const UnitMovementUnit& unit) {
    if (unit.distance_check_mode == 1) {
        return false;
    }
    return std::abs(unit.x - unit.path_target_x) <= 0x20 &&
        std::abs(unit.y - unit.path_target_y) <= 0x20;
}

namespace {

bool check_target_footprint_separated(const UnitMovementUnit& unit,
    const UnitMovementUnit* target) {
    if (target == nullptr) {
        return true;
    }

    const i32 source_left = unit.x + unit.definition.bounds_left * 2;
    const i32 source_top = unit.y + unit.definition.bounds_top * 2;
    const i32 source_right = source_left + unit.definition.bounds_width * 2;
    const i32 source_bottom = source_top + unit.definition.bounds_height * 2;
    const i32 target_left = target->x + target->definition.bounds_left;
    const i32 target_top = target->y + target->definition.bounds_top;
    const i32 target_right = target_left + target->definition.bounds_width;
    const i32 target_bottom = target_top + target->definition.bounds_height;

    return target_left > source_right || target_top > source_bottom ||
        source_left > target_right || source_top > target_bottom;
}

} // namespace

bool CheckCurrentTargetFootprintSeparated(const UnitMovementUnit& unit) {
    return check_target_footprint_separated(unit, unit.target);
}

bool CheckCurrentTargetFootprintSeparated(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    return check_target_footprint_separated(unit,
        resolve_active_payload_target_or_clear(context, unit));
}

bool CheckNearbyFollowCommandTarget(UnitCommandContext& context, UnitMovementUnit& unit) {
    return TryStartNearbyFollowCommand(context, unit);
}

bool CheckOwnerHasUnitProductionCosts(const UnitCommandContext& context,
    u32 owner_id, u32 primary_cost, u32 secondary_cost) {
    if (owner_id >= context.owner_resources.size()) {
        return true;
    }
    return context.owner_resources[owner_id] >= primary_cost &&
        context.owner_secondary_resources[owner_id] >= secondary_cost;
}

void HandleOwnerUnitProductionCostDebit(UnitCommandContext& context,
    u32 owner_id, u32 primary_cost, u32 secondary_cost) {
    if (owner_id >= context.owner_resources.size()) {
        return;
    }
    context.owner_resources[owner_id] -= primary_cost;
    context.owner_secondary_resources[owner_id] -= secondary_cost;
}

void HandleOwnerUnitProductionCostRefund(UnitCommandContext& context,
    u32 owner_id, u32 primary_cost, u32 secondary_cost) {
    if (owner_id >= context.owner_resources.size()) {
        return;
    }
    context.owner_resources[owner_id] += primary_cost;
    context.owner_secondary_resources[owner_id] += secondary_cost;
}

void ResetUnitCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.target = nullptr;
    unit.distance_check_mode = 0;
    unit.anchor_x = unit.x;
    unit.anchor_y = unit.y;
    unit.command_flags &= ~0x28u;
    if (context.callbacks.reset_to_idle != nullptr) {
        context.callbacks.reset_to_idle(context, unit);
    }
}

void HandleUnitReturnToIdleState(UnitCommandContext&, UnitMovementUnit& unit) {
    if (unit.command_state == kUnitStateRuntimeIdleAcquire) {
        return;
    }
    unit.command_state = kUnitStateRuntimeIdleAcquire;
    if (unit.definition.animation_timer_period <= unit.animation_frame) {
        unit.animation_frame = 0;
    }
}

void StartUnitValueTransferEntry(UnitCommandContext&, UnitMovementUnit& unit,
    u32 command_value) {
    unit.command_value = command_value;
    unit.command_state = kUnitStateValueTransferStart;
}

void StartUnitCommandState08Entry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStateCommand08;
}

void StartUnitTargetOrPointCommandEntry(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    const u32 payload_target = active_payload_target_id(unit);
    const bool encoded_point_payload = (payload_target & 0x80000000u) != 0;
    if (!encoded_point_payload) {
        UnitMovementUnit* target = resolve_command_payload_target(context, unit);
        unit.command_value = static_cast<u32>(unit.path_target_x);
        if (target != nullptr) {
            unit.path_target_x = target->x;
            unit.path_target_y = target->y;
        }
    }
    else {
        unit.command_value = payload_target & 0x7fffffffu;
        unit.active_command_payload.x = 0;
    }
    unit.command_state = kUnitStateTargetOrPointCommand;
}

void StartUnitState10OrTargetedSpawnEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = unit.type_id == 0x10 ?
        kUnitStateTargetedSpawnPlacementStart : kUnitStateCommand10;
}

void StartUnitState14AndClearRuntimeFlag(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStateCommand14;
    unit.runtime_flags &= ~0x8u;
}

void StartTargetValidationClearingTargetFlag(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // The original stores the active tuple's target offset directly in the
    // unit target field (+0x20).  Our tuple and resolved pointer are separate,
    // so resolve the packet target before entering runtime state 0x1c.
    resolve_command_payload_target(context, unit);
    if (active_payload_target_id(unit) != unit.id) {
        unit.area_marker_flags &= ~0x80000000u;
        unit.command_state = kUnitStateRuntimeTargetValidationStart;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void StartTargetValidationSettingTargetFlag(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    resolve_command_payload_target(context, unit);
    if (active_payload_target_id(unit) != unit.id) {
        unit.area_marker_flags |= 0x80000000u;
        unit.command_state = kUnitStateRuntimeTargetValidationStart;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void StartUnitItemSlotUseEntry(UnitCommandContext& context, UnitMovementUnit& unit) {
    const u32 payload_target = active_payload_target_id(unit);
    const bool encoded_point_payload = (payload_target & 0x80000000u) != 0;
    if (!encoded_point_payload) {
        UnitMovementUnit* target = resolve_command_payload_target(context, unit);
        unit.command_value = static_cast<u32>(unit.path_target_x);
        if (target != nullptr) {
            unit.path_target_x = target->x;
            unit.path_target_y = target->y;
        }
        unit.active_command_payload.y = unit.path_target_x;
        unit.active_command_payload.value = static_cast<u32>(unit.path_target_y);
        unit.saved_path_target_x = unit.path_target_x;
        unit.saved_path_target_y = unit.path_target_y;
    }
    else {
        unit.command_value = payload_target & 0x7fffffffu;
        unit.target = nullptr;
        unit.active_command_payload.x = 0;
    }
    unit.command_state = kUnitStateItemSlotUseStart;
}

void StartUnitMorphEnterIfAvailable(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (unit.definition.morph_type_id != 0 && (unit.type_flags & 0x20000u) != 0) {
        unit.command_state = kUnitStateMorphEnterTimer;
        unit.animation_frame = 0;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void StartUnitMorphExitIfFlagged(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if ((unit.type_flags & 0x08000000u) != 0) {
        unit.command_state = kUnitStateMorphExitTimer;
        unit.animation_frame = 0;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

bool SetUnitTargetReservationFlag(UnitMovementUnit* unit) {
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;
    constexpr u32 kTargetReservationAreaMarkerFlag = 0x80000000u;
    if (unit == nullptr ||
        (unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0) {
        return false;
    }
    unit->area_marker_flags |= kTargetReservationAreaMarkerFlag;
    return true;
}

bool ClearUnitTargetReservationFlag(UnitMovementUnit* unit) {
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;
    constexpr u32 kTargetReservationAreaMarkerFlag = 0x80000000u;
    if (unit == nullptr ||
        (unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0) {
        return false;
    }
    unit->area_marker_flags &= ~kTargetReservationAreaMarkerFlag;
    return true;
}

void StartState23OrSpawnPlacementEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = unit.type_id == 0x10 ?
        kUnitStateSpawnPlacementStart : kUnitStateCommand23;
}

void StartHarvestOrReservedWorkEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = unit.type_id == 0x10 ?
        kUnitStateReservedTileStart : kUnitStateWorkerApproachHarvest;
}

void StartPatrolCommandEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStatePatrolStart;
}

void StartUnitCommandState3cEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStateCommand3c;
}

void StartTransportUnloadCommandEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStateTransportUnloadStart;
}

void StartLinkedReleaseOrPopNextCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if ((unit.type_flags & 0x800u) != 0) {
        unit.command_state = kUnitStateLinkedUnitReleaseStart;
        unit.runtime_flags &= ~0x40000u;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void StartUnitProductionSpawnEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStateProductionSpawnStart;
}

void StartUnitCompletionAnnouncementEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStateCompletionAnnouncementStart;
}

void StartCompletionEffectCommandEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state = kUnitStateCompletionEffectStart;
}

void MarkUnitCommandDeadEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state |= kUnitCommandDead;
}

void MarkUnitCommandDeferredEntry(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.command_state |= kUnitCommandDeferredEntryFlag;
}

void StartUnitCommandLockoutTimer(UnitCommandContext&, UnitMovementUnit& unit,
    u32 ticks) {
    if (ticks == 0) {
        return;
    }
    unit.command_state |= 0x40000000u;
    unit.animation_timer = 0;
    unit.command_entry_lockout_ticks = ticks;
}

void ClearUnitRuntimeFlagAndReturnIdle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    unit.runtime_flags &= ~0x8u;
    HandleUnitReturnToIdleState(context, unit);
}

bool PushDeferredUnitCommand(UnitMovementUnit& unit,
    const UnitQueuedCommand& command, u32 max_deferred_count) {
    constexpr u32 kDeferredCommandCapacity = 10;
    const u32 bounded_limit = std::min<u32>(max_deferred_count,
        std::min<u32>(kDeferredCommandCapacity,
            static_cast<u32>(unit.deferred_commands.size())));
    if (unit.deferred_command_count >= bounded_limit) {
        return false;
    }

    unit.deferred_commands[unit.deferred_command_count] = command;
    ++unit.deferred_command_count;
    return true;
}

bool CommitThenPushDeferredUnitCommand(UnitMovementUnit& unit,
    const UnitQueuedCommand& command, u32 max_deferred_count,
    UnitDeferredCommandCommitCallback commit, void* user_data) {
    if (commit == nullptr ||
        !commit(unit, static_cast<u32>(command.x), user_data)) {
        return false;
    }
    // The subtype-0x0c original deliberately performs its debit/lock before
    // the bounded push.  A full queue reports failure without rolling either
    // committed mutation back.
    return PushDeferredUnitCommand(unit, command, max_deferred_count);
}

bool IsProductionOrderCancelLogicalIndexAllowed(u32 logical_index) {
    // 0xffffffff is the implicit/latest form.  The explicit original handler
    // indexes only its five cancellation entries even though enqueue has ten
    // deferred slots.
    return logical_index == 0xffffffffu || logical_index < 5u;
}

bool SetOrQueueUnitCommandPayload(UnitMovementUnit* unit,
    const UnitQueuedCommand& command, bool enqueue_deferred,
    u32 max_deferred_count) {
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;

    if (unit == nullptr ||
        (unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0) {
        return false;
    }
    if (enqueue_deferred) {
        return PushDeferredUnitCommand(*unit, command, max_deferred_count);
    }
    unit->pending_command = command;
    return true;
}

bool SetOrQueueUnitPointCommand01(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    constexpr u32 kPointCommand01AcceptFlag = 2;
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;

    if (unit == nullptr ||
        (unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0 ||
        (unit->command_flags & kPointCommand01AcceptFlag) == 0) {
        return false;
    }

    const UnitQueuedCommand command{
        1,
        0,
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitCommand00(UnitMovementUnit* unit, bool enqueue_deferred) {
    const UnitQueuedCommand command{};
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitTargetCommand03(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred) {
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;
    if (target_unit == nullptr ||
        (target_unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0) {
        return false;
    }
    const UnitQueuedCommand command{
        3,
        static_cast<i32>(target_unit->id),
        target_unit->x,
        static_cast<u32>(target_unit->y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitCommand02(UnitMovementUnit* unit, u32 fallback_command_value,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        2,
        static_cast<i32>(target_unit != nullptr ? target_unit->id :
            fallback_command_value + 0x80000000u),
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitTargetPointCommand04(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        4,
        static_cast<i32>(target_unit != nullptr ? target_unit->id : 0),
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitConditionalTargetPointCommand05(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred) {
    constexpr u32 kRequiresState05CommandFlag = 0x20;

    if (unit == nullptr) {
        return false;
    }
    if ((unit->command_flags & kRequiresState05CommandFlag) == 0) {
        return SetOrQueueUnitTargetPointCommand04(
            unit, nullptr, x, y, enqueue_deferred);
    }

    const UnitQueuedCommand command{
        5,
        static_cast<i32>(target_unit != nullptr ? target_unit->id : 0),
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitAlignedPointCommand06(UnitMovementUnit* unit, u32 command_value,
    i32 x, i32 y, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        6,
        static_cast<i32>(command_value),
        x & ~0x1f,
        static_cast<u32>(y & ~0x1f),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitPointCommand07(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    const UnitQueuedCommand command{
        7,
        0,
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitPointCommand09(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    const UnitQueuedCommand command{
        9,
        0,
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitTargetCommand0a(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        0x0a,
        static_cast<i32>(target_unit != nullptr ? target_unit->id : 0),
        target_unit != nullptr ? target_unit->x : 0,
        static_cast<u32>(target_unit != nullptr ? target_unit->y : 0),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitTargetCommand0b(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        0x0b,
        static_cast<i32>(target_unit != nullptr ? target_unit->id : 0),
        target_unit != nullptr ? target_unit->x : 0,
        static_cast<u32>(target_unit != nullptr ? target_unit->y : 0),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitCommand10(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred) {
    constexpr u32 kWrapperDeferredLimit = 5;
    const UnitQueuedCommand command{
        0x10,
        static_cast<i32>(command_value),
        0,
        0,
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred,
        kWrapperDeferredLimit);
}

bool SetOrQueueUnitCommand11(UnitMovementUnit* unit, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        0x11,
        0,
        0,
        0,
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitCommand17(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred) {
    constexpr u32 kWrapperDeferredLimit = 5;
    const UnitQueuedCommand command{
        0x17,
        static_cast<i32>(command_value),
        0,
        0,
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred,
        kWrapperDeferredLimit);
}

bool SetOrQueueUnitCommand1b(UnitMovementUnit* unit, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        0x1b,
        0,
        0,
        0,
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetUnitCommandTargetReferencePoint(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y) {
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;
    if (unit == nullptr ||
        (unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0) {
        return false;
    }
    unit->target = target_unit;
    unit->path_target_x = x;
    unit->path_target_y = y;
    return true;
}

bool SetOrQueueUnitCommand21AndSetRuntimeFlag(UnitMovementUnit* unit,
    bool enqueue_deferred) {
    constexpr u32 kRuntimeFlag08 = 8;
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;
    if (unit == nullptr ||
        (unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0) {
        return false;
    }
    unit->runtime_flags |= kRuntimeFlag08;
    const UnitQueuedCommand command{
        0x21,
        0,
        0,
        0,
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitCommand22(UnitMovementUnit* unit, u32 command_value,
    bool enqueue_deferred) {
    constexpr u32 kCommandState22 = 0x22;
    constexpr u32 kWrapperDeferredLimit = 5;

    const UnitQueuedCommand command{
        kCommandState22,
        static_cast<i32>(command_value),
        0,
        0,
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred,
        kWrapperDeferredLimit);
}

bool SetOrQueueUnitTargetCommand23(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, bool enqueue_deferred) {
    const UnitQueuedCommand command{
        0x23,
        static_cast<i32>(target_unit != nullptr ? target_unit->id : 0),
        x,
        0,
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

bool SetOrQueueUnitPointCommand24(UnitMovementUnit* unit, i32 x, i32 y,
    bool enqueue_deferred) {
    const UnitQueuedCommand command{
        0x24,
        0,
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

void HandleNoOpUnitCommandEntry() {}

bool SetOrQueueUnitExtendedStateCommand(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, u32 state_index,
    bool enqueue_deferred) {
    const UnitQueuedCommand command{
        0x20000000u + state_index,
        static_cast<i32>(target_unit != nullptr ? target_unit->id : 0),
        x,
        static_cast<u32>(y),
    };
    return SetOrQueueUnitCommandPayload(unit, command, enqueue_deferred);
}

void WriteOrQueueUnitExtendedCommandPayload(UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, u32 state_index,
    bool enqueue_deferred) {
    SetOrQueueUnitExtendedStateCommand(unit, target_unit, x, y, state_index,
        enqueue_deferred);
}

void PopDeferredUnitCommandOrReturnIdle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (unit.deferred_command_count == 0) {
        unit.active_command_payload.state = 0;
        HandleUnitReturnToIdleState(context, unit);
        return;
    }

    unit.pending_command = unit.deferred_commands[0];
    --unit.deferred_command_count;
    if (unit.deferred_command_count != 0) {
        for (u32 i = 0; i + 1 < unit.deferred_commands.size(); ++i) {
            unit.deferred_commands[i] = unit.deferred_commands[i + 1];
        }
    }
}

bool FilterPendingUnitCommandInterrupt(UnitMovementUnit& unit) {
    if (unit.distance_check_mode != 0 ||
        (unit.command_state & 0x50000000u) != 0 ||
        (unit.command_flags & 0x10u) != 0 ||
        (unit.runtime_flags & 0x08020062u) != 0) {
        return false;
    }

    const u32 current_state = unit.command_state & kUnitCommandStateMask;
    const u32 pending_state = unit.pending_command.state & kUnitCommandStateMask;
    if (current_state == kUnitStateMorphEnterTimer ||
        current_state == kUnitStateMorphExitTimer) {
        return false;
    }
    if (current_state == kUnitStateLinkedUnitReleaseCycle && pending_state == 0x0b) {
        return static_cast<i32>(unit.pending_command.value) == -1;
    }
    if (current_state == kUnitStateLinkedUnitReleaseCycle) {
        return false;
    }
    if (current_state == kUnitStateSpawnCreateCycle && pending_state == 0x06) {
        return static_cast<i32>(unit.pending_command.value) == -1;
    }
    if (current_state == kUnitStateSpawnCreateCycle) {
        return false;
    }

    if (pending_state == 0x24 && (unit.type_flags & 0x400u) == 0) {
        if (current_state == kUnitStateTransportAttached) {
            return true;
        }
        unit.pending_command = {};
        return false;
    }
    if (current_state == kUnitStateTransportAttached) {
        return false;
    }
    return true;
}

void DispatchUnitCommandStateEntry(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 command_state) {
    unit.command_flags &= ~0x20u;
    const u32 state = command_state & kUnitCommandStateMask;
    if ((command_state & kUnitCommandDeferredEntryFlag) != 0) {
        unit.ability_id = state;
        unit.command_state = kUnitStateSpecialAbilityStart;
        return;
    }

    switch (state) {
    case 0x00:
        ClearUnitRuntimeFlagAndReturnIdle(context, unit);
        return;
    case 0x01:
        StartUnitCommandState08Entry(context, unit);
        return;
    case 0x02:
        StartUnitTargetOrPointCommandEntry(context, unit);
        return;
    case 0x03:
        StartUnitState10OrTargetedSpawnEntry(context, unit);
        return;
    case 0x04:
        StartUnitState14AndClearRuntimeFlag(context, unit);
        return;
    case 0x05:
        StartTargetValidationClearingTargetFlag(context, unit);
        return;
    case 0x06:
        StartState23OrSpawnPlacementEntry(context, unit);
        return;
    case 0x07:
        StartHarvestOrReservedWorkEntry(context, unit);
        return;
    case 0x09:
        StartPatrolCommandEntry(context, unit);
        return;
    case 0x0a:
        StartUnitCommandState3cEntry(context, unit);
        return;
    case 0x0b:
        StartLinkedReleaseOrPopNextCommand(context, unit);
        return;
    case 0x0d:
        StartTargetValidationSettingTargetFlag(context, unit);
        return;
    case 0x10:
        StartUnitProductionSpawnEntry(context, unit);
        return;
    case 0x11:
        StartUnitMorphEnterIfAvailable(context, unit);
        return;
    case 0x16:
        StartUnitItemSlotUseEntry(context, unit);
        return;
    case 0x17:
        StartUnitCompletionAnnouncementEntry(context, unit);
        return;
    case 0x1b:
        StartUnitMorphExitIfFlagged(context, unit);
        return;
    case 0x21:
        HandleUnitReturnToIdleState(context, unit);
        return;
    case 0x22:
        StartCompletionEffectCommandEntry(context, unit);
        return;
    case 0x23:
        StartUnitValueTransferEntry(context, unit,
            static_cast<u32>(unit.path_target_x));
        return;
    case 0x24:
        StartTransportUnloadCommandEntry(context, unit);
        return;
    default:
        return;
    }
}

void HandlePendingUnitCommandDispatch(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (unit.command_entry_lockout_ticks != 0) {
        --unit.command_entry_lockout_ticks;
        if (unit.command_entry_lockout_ticks == 0) {
            unit.command_state &= ~0x40000000u;
            unit.runtime_flags &= 0xfffdff9fu;
        }
    }

    if (unit.pending_command.state == 0) {
        return;
    }
    if ((unit.pending_command.state & kUnitCommandNoInterruptFlag) == 0 &&
        !FilterPendingUnitCommandInterrupt(unit)) {
        return;
    }

    UnitQueuedCommand command = unit.pending_command;
    command.state &= ~kUnitCommandMirrorClearFlag;
    unit.active_command_payload = command;
    unit.command_value = static_cast<u32>(command.x);
    // Original 0x004cfe37 stores one raw target-or-value word in a shared
    // field.  The reconstruction separates command_value from the resolved
    // pointer, so only target-bearing states may interpret that word as a unit
    // id.  A point move must still clear an idle-acquired target, while a
    // production command (state 0x10) must not mistake its type id for a unit.
    if (command_payload_carries_target_id(command.state)) {
        resolve_command_payload_target(context, unit);
    }
    else {
        unit.target = nullptr;
    }
    unit.path_target_x = command.y;
    unit.path_target_y = static_cast<i32>(command.value);
    DispatchUnitCommandStateEntry(context, unit, command.state);
    unit.pending_command = {};
    if (context.callbacks.on_command_acknowledged != nullptr) {
        context.callbacks.on_command_acknowledged(context, unit);
    }
}

void HandleRuntimeDeathPartialResourceRefund(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // HandleUnitRuntimeDispatchTick (0x004cdc0a) gates the 75-percent
    // under-construction refund on raw unit +0x4c (DAT_00a04004).  That field
    // is the cargo/transport occupancy value in the typed runtime; command_value
    // is raw +0x68 and can contain an unrelated command payload.
    if (unit.owner_id >= context.owner_resources.size() || unit.cargo_amount == 0) {
        return;
    }
    const u32 cost = unit.definition.production_resource_cost;
    context.owner_resources[unit.owner_id] += cost - cost / 4u;
}

void HandleUnitRuntimeDispatchTick(UnitCommandContext& context, UnitMovementUnit& unit) {
    HandlePendingUnitCommandDispatch(context, unit);

    if (unit.previous_command_state == 1) {
        TickUnitRuntimeAuxTimerReset(unit);
        return;
    }

    if ((unit.draw_flags & 0x7f) != 0) {
        --unit.draw_flags;
    }
    ++unit.item_slots[3];
    if (unit.item_slots[3] > 0x14u) {
        unit.item_slots[3] = 0;
    }
    if (unit.equipment_slots.size() > 3) {
        unit.equipment_slots[3] = unit.item_slots[3];
    }

    if ((unit.command_state & kUnitCommandDead) != 0) {
        unit.runtime_flags |= 4;
        unit.command_state &= ~0x40000000u;
        unit.animation_frame = 0;
        unit.draw_flags = 0;
        if (context.callbacks.on_runtime_death_marked != nullptr) {
            context.callbacks.on_runtime_death_marked(context, unit);
        }
        if (context.callbacks.clear_footprint != nullptr) {
            context.callbacks.clear_footprint(context, unit);
        }
        if (context.callbacks.on_runtime_death_sound != nullptr) {
            context.callbacks.on_runtime_death_sound(context, unit);
        }
        if (unit.action_mode_gate == 1) {
            HandleRuntimeDeathPartialResourceRefund(context, unit);
        }
        else {
            // Original high-type death path 0x004cdbfe..0x004cdc03 accounts
            // the loss after footprint/sound side effects but before walking
            // the active and deferred production command queues.
            if (context.callbacks.on_runtime_death_accounting != nullptr) {
                context.callbacks.on_runtime_death_accounting(context, unit);
            }
            HandleUnitDeathCommandQueueSideEffects(context, unit);
        }
        return;
    }

    if ((context.frame_counter & 0x3fu) == 0) {
        if (unit.type_id >= 0x60 && unit.health < unit.max_health &&
            unit.definition.passive_recovery_enabled != 0) {
            ++unit.health;
        }
        if (unit.type_id == 0x7d) {
            --unit.secondary_value;
            if (unit.secondary_value == 0) {
                MarkUnitCommandDeadEntry(context, unit);
                return;
            }
        }
    }

    if ((unit.command_state & 0x40000000u) != 0) {
        return;
    }

    if (unit.action_mode_gate == 1) {
        UnitMovementUnit* target = unit.target;
        if (target != nullptr &&
            (((target->runtime_flags & 4u) != 0) ||
                (target->type_id == 0x10 &&
                    GetUnitCommandIdLow24(*target) != kUnitStateSpawnCreateCycle))) {
            unit.target = nullptr;
        }
        return;
    }

    // Original HandleUnitRuntimeDispatchTick (0x004cd988) advances the
    // animation and consumes the recovery counter on every simulation tick.
    // Unit actions seed command_lockout_ticks after an impact; leaving it
    // unchanged traps an attacker on the first attack frame forever.
    if (unit.command_lockout_ticks != 0) {
        ProcessUnitAnimationTimer(unit);
        --unit.command_lockout_ticks;
        if (unit.command_lockout_ticks == 0) {
            unit.command_flags &= ~0x10u;
        }
    }

    DispatchExtendedUnitRuntimeCommandState(context, unit);
}

void DispatchUnitRuntimeCommandState(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // The original dispatch table is indexed by the raw low-24-bit command
    // id.  The runtime-state table is only a command classification table;
    // using it here collapses raw commands 0x01..0x06 to the idle class and
    // prevents travel/attack handlers from ever running.
    switch (GetUnitCommandIdLow24(unit)) {
    case 0:
        return;
    case kUnitStateRuntimeIdleAcquire:
        ProcessUnitIdleAcquireCommand(context, unit);
        return;
    case kUnitStateTravel:
        ProcessUnitTravelCommand(context, unit);
        return;
    case kUnitStateAttackTravel:
        ProcessUnitAttackTravelCommand(context, unit);
        return;
    case kUnitStateRuntimeAttackTarget:
        ProcessUnitAttackTargetCommand(context, unit);
        return;
    case kUnitStateAssistTarget:
        StartUnitEquipmentPointCommand(context, unit);
        return;
    case kUnitStateEquipmentPointTravel:
        HandleUnitEquipmentPointTravel(context, unit);
        return;
    case kUnitStateCommand08:
        StartUnitPointActionCommand(context, unit);
        return;
    case kUnitStatePointActionReady:
        HandleUnitPointActionCycle(context, unit);
        return;
    case kUnitStatePointActionTravel:
        HandleUnitPointActionTravel(context, unit);
        return;
    case kUnitStateTargetOrPointCommand:
        StartUnitTargetInteractionCommand(context, unit);
        return;
    case kUnitStateTargetInteractionCycle:
        HandleUnitTargetInteractionCycle(context, unit);
        return;
    case kUnitStateTargetInteractionApproach:
        HandleUnitTargetInteractionApproach(context, unit);
        return;
    case kUnitStateTargetProgressStart:
        StartUnitTargetProgressCommand(context, unit);
        return;
    case kUnitStateTargetProgressCycle:
        HandleUnitTargetProgressCycle(context, unit);
        return;
    case kUnitStateTargetProgressApproach:
        HandleUnitTargetProgressApproach(context, unit);
        return;
    case kUnitStateCommand14:
        ProcessUnitFollowTargetStart(context, unit);
        return;
    case kUnitStateFollowPoint:
        ProcessUnitFollowPointTravel(context, unit);
        return;
    case kUnitStateFollowMovingTarget:
        ProcessUnitFollowMovingTarget(context, unit);
        return;
    case kUnitStateFollowHoldRange:
        ProcessUnitFollowHoldRange(context, unit);
        return;
    case kUnitStateRuntimeTargetValidationStart:
        StartUnitGuardAnchorCommand(context, unit);
        return;
    case kUnitStateRuntimeTargetValidation:
        HandleUnitGuardAnchorAction(context, unit);
        return;
    case kUnitStateGuardAnchorApproach:
        HandleUnitGuardAnchorApproach(context, unit);
        return;
    case kUnitStateGuardReturnCommand:
        StartUnitGuardReturnCommand(context, unit);
        return;
    case kUnitStateGuardCombatCycle:
        HandleUnitGuardCombatCycle(context, unit);
        return;
    case kUnitStateGuardReturnTravel:
        HandleUnitGuardReturnTravel(context, unit);
        return;
    case kUnitStateGuardPursueTarget:
        HandleUnitGuardPursueTarget(context, unit);
        return;
    case kUnitStateLegacySpawnPlacementStart:
        StartUnitLegacySpawnPlacementCommand(context, unit);
        return;
    case kUnitStateLegacySpawnConstruction:
        HandleLegacySpawnedConstructionRelease(context, unit);
        return;
    case kUnitStateLegacySpawnPlacementApproach:
        HandleUnitLegacySpawnPlacementApproach(context, unit);
        return;
    case kUnitStateWorkerApproachHarvest:
        // Original jump table DAT_0072d080 maps raw state 0x28 to
        // ProcessWorkerReturnToDropoff (0x004ca14c), which prepares the
        // harvest path and advances to state 0x2a.
        ProcessWorkerReturnToDropoff(context, unit);
        return;
    case kUnitStateWorkerReservedHarvest:
        ProcessWorkerHarvestTile(context, unit);
        return;
    case kUnitStateWorkerReturnToDropoff:
        // Raw state 0x2a is the actual approach/movement handler in the
        // original table (0x004ca2ab).  Reversing these two entries leaves AI
        // workers endlessly rebuilding a path without taking a movement step.
        ProcessWorkerApproachHarvestTile(context, unit);
        return;
    case kUnitStateWorkerApproachDropoff:
        ProcessWorkerApproachDropoff(context, unit);
        return;
    case kUnitStateWorkerDepositCargo:
        ProcessWorkerDepositCargo(context, unit);
        return;
    case kUnitStateWorkerHarvestFailed:
        ProcessWorkerReservedHarvestWait(context, unit);
        return;
    case kUnitStatePatrolStart:
        StartUnitPatrolRouteCommand(context, unit);
        return;
    case kUnitStatePatrolNoop:
        return;
    case kUnitStatePatrolReturnLeg:
        HandleUnitPatrolReturnLeg(context, unit);
        return;
    case kUnitStatePatrolOutboundLeg:
        HandleUnitPatrolOutboundLeg(context, unit);
        return;
    case kUnitStatePatrolReturnCombat:
        HandleUnitPatrolReturnCombatTarget(context, unit);
        return;
    case kUnitStatePatrolOutboundCombat:
        HandleUnitPatrolOutboundCombatTarget(context, unit);
        return;
    case kUnitStateCommand3c:
        BeginUnitCarrierBoardingCommand(context, unit);
        return;
    case kUnitStateCarrierImmediateBoarding:
        HandleCarrierImmediateBoarding(context, unit);
        return;
    case kUnitStateCarrierApproachBoarding:
        HandleCarrierApproachBoardingTarget(context, unit);
        return;
    case kUnitStatePassengerApproachCarrier:
        HandlePassengerApproachCarrier(context, unit);
        return;
    case kUnitStateTransportUnloadStart:
        ProcessTransportUnloadStart(context, unit);
        return;
    case kUnitStateTransportUnloadChildren:
        ProcessTransportUnloadChildren(context, unit);
        return;
    case kUnitStateTransportUnloadWait:
        ProcessTransportUnloadWait(context, unit);
        return;
    case kUnitStateTransportAttached:
        return;
    case kUnitStateTransportDockStart:
        ProcessTransportDockStart(context, unit);
        return;
    case kUnitStateTransportDockSearch:
        ProcessTransportDockSearch(context, unit);
        return;
    case kUnitStateCompletionAnnouncementStart:
        StartUnitCompletionAnnouncementCommand(context, unit);
        return;
    case kUnitStateCompletionAnnouncementTimer:
        HandleUnitCompletionAnnouncementTimer(context, unit);
        return;
    case kUnitStateReservedTileStart:
        StartReservedTileWorkCommand(context, unit);
        return;
    case kUnitStateReservedTileWork:
        HandleReservedTileWorkCycle(context, unit);
        return;
    case kUnitStateReservedTileApproach:
        HandleReservedTileApproach(context, unit);
        return;
    case kUnitStateReservedTileBlockedWait:
        HandleReservedTileWait(context, unit);
        return;
    case kUnitStateReservedTileRetryDelay:
        HandleReservedTileRetryDelay(context, unit);
        return;
    case kUnitStateReservedTileLinkedObject:
        HandleReservedTileLinkedObject(context, unit);
        return;
    case kUnitStateSpawnPlacementStart:
        StartUnitSpawnPlacementCommand(context, unit);
        return;
    case kUnitStateSpawnCreateCycle:
        HandleUnitSpawnCreateCycle(context, unit);
        return;
    case kUnitStateSpawnPlacementWait:
        HandleUnitSpawnPlacementWait(context, unit);
        return;
    case kUnitStateLinkedUnitReleaseStart:
        StartLinkedUnitReleaseCommand(context, unit);
        return;
    case kUnitStateLinkedUnitReleaseCycle:
        HandleLinkedUnitReleaseCycle(context, unit);
        return;
    case kUnitStateLinkedUnitReleaseApproach:
        HandleLinkedUnitReleaseApproach(context, unit);
        return;
    case kUnitStateSpecialAbilityStart:
        StartUnitSpecialAbilityCommand(context, unit);
        return;
    case kUnitStateSpecialAbilityTimer:
        HandleUnitSpecialAbilityTimer(context, unit);
        return;
    case kUnitStateSpecialAbilityApproach:
        HandleUnitSpecialAbilityApproach(context, unit);
        return;
    case kUnitStateRestoreTargetCycle:
        HandleUnitRestoreTargetCycle(context, unit);
        return;
    case kUnitStateRestoreTargetApproach:
        HandleUnitRestoreTargetApproach(context, unit);
        return;
    case kUnitStateRandomRelocation:
        HandleUnitRandomRelocation(context, unit);
        return;
    case kUnitStateMorphEnterTimer:
        HandleUnitMorphEnterTimer(context, unit);
        return;
    case kUnitStateMorphExitTimer:
        HandleUnitMorphExitTimer(context, unit);
        return;
    case kUnitStateValueTransferStart:
        StartUnitValueTransferCommand(context, unit);
        return;
    case kUnitStateValueTransferCycle:
        HandleUnitValueTransferCycle(context, unit);
        return;
    case kUnitStateValueTransferApproach:
        HandleUnitValueTransferApproach(context, unit);
        return;
    case kUnitStateAnimationTimerOnly:
        if (context.callbacks.on_regen_error != nullptr) {
            context.callbacks.on_regen_error(context, unit);
        }
        return;
    case kUnitStateTimedFlagPhaseA:
        HandleUnitTimedFlagPhaseA(context, unit);
        return;
    case kUnitStateTimedFlagPhaseB:
        HandleUnitTimedFlagPhaseB(context, unit);
        return;
    case kUnitStateTargetedSpawnPlacementStart:
        StartTargetedUnitSpawnPlacement(context, unit);
        return;
    case kUnitStateTargetedSpawnCycle:
        HandleTargetedUnitSpawnCycle(context, unit);
        return;
    case kUnitStateTargetedSpawnApproach:
        HandleTargetedUnitSpawnApproach(context, unit);
        return;
    case kUnitStateItemSlotUseStart:
        StartUnitItemSlotUseCommand(context, unit);
        return;
    case kUnitStateItemSlotUseAction:
        HandleUnitItemSlotUseAction(context, unit);
        return;
    case kUnitStateItemSlotUseApproach:
        HandleUnitItemSlotUseApproach(context, unit);
        return;
    default:
        return;
    }
}

void DispatchExtendedUnitRuntimeCommandState(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // The original extended-unit jump table is also indexed by the raw
    // low-24-bit command id.  Keep the explicit start/cycle cases first, then
    // use the same raw id for the remaining handlers.
    switch (GetUnitCommandIdLow24(unit)) {
    case kUnitStateAttackTravel:
        ProcessUnitAttackTravelCommand(context, unit);
        return;
    case kUnitStateAttackTarget:
        ProcessUnitAttackTargetCommand(context, unit);
        return;
    case kUnitStateRuntimeTargetValidationStart:
        StartUnitRuntimeTargetValidationState(context, unit);
        return;
    case kUnitStateRuntimeTargetValidation:
        HandleUnitRuntimeTargetValidationState(context, unit);
        return;
    case kUnitStateCompletionAnnouncementStart:
        StartUnitCompletionAnnouncementCommand(context, unit);
        return;
    case kUnitStateCompletionAnnouncementTimer:
        HandleUnitCompletionAnnouncementTimer(context, unit);
        return;
    case kUnitStateProductionSpawnStart:
        StartUnitProductionSpawnCommand(context, unit);
        return;
    case kUnitStateProductionSpawnCycle:
        HandleUnitProductionSpawnCycle(context, unit);
        return;
    case kUnitStateCompletionEffectStart:
        StartUnitCompletionEffectSequence(context, unit);
        return;
    case kUnitStateCompletionEffectTimer:
        HandleUnitCompletionEffectTimer(context, unit);
        return;
    default:
        break;
    }
    switch (GetUnitCommandIdLow24(unit)) {
    case kUnitStateRuntimeIdleAcquire:
        HandleUnitRuntimeIdleAcquireState(context, unit);
        return;
    case kUnitStateRuntimeAttackTarget:
        HandleUnitRuntimeAttackTargetState(context, unit);
        return;
    case kUnitStateRuntimeTargetValidationStart:
        StartUnitRuntimeTargetValidationState(context, unit);
        return;
    case kUnitStateRuntimeTargetValidation:
        HandleUnitRuntimeTargetValidationState(context, unit);
        return;
    case kUnitStateCompletionAnnouncementStart:
        StartUnitCompletionAnnouncementCommand(context, unit);
        return;
    case kUnitStateCompletionAnnouncementTimer:
        HandleUnitCompletionAnnouncementTimer(context, unit);
        return;
    case kUnitStateProductionSpawnStart:
        StartUnitProductionSpawnCommand(context, unit);
        return;
    case kUnitStateProductionSpawnCycle:
        HandleUnitProductionSpawnCycle(context, unit);
        return;
    case kUnitStateCompletionEffectStart:
        StartUnitCompletionEffectSequence(context, unit);
        return;
    case kUnitStateCompletionEffectTimer:
        HandleUnitCompletionEffectTimer(context, unit);
        return;
    default:
        return;
    }
}

void HandleUnitRuntimeIdleAcquireState(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (unit.deferred_command_count != 0) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if ((unit.type_flags & 0x20) != 0 && unit.animation_frame == 0) {
        UnitMovementUnit* target =
            unit.type_id == 0x6a &&
                context.callbacks.find_source_bounds_target != nullptr
            ? context.callbacks.find_source_bounds_target(context, unit)
            : find_target(context, unit);
        if (target != nullptr && can_attack(context, unit, *target)) {
            SetUnitCommandTarget(unit, target);
            unit.command_state = kUnitStateRuntimeAttackTarget;
            unit.command_flags &= ~8u;
            unit.animation_frame = 0;
            HandleUnitRuntimeAttackTargetState(context, unit);
            return;
        }
    }

    // Original 0x004cda9f selects the definition's frame count only when
    // movement_animation_frame_count (+0x2218) is nonzero.  Stationary
    // structures therefore use the hard-coded eight-frame idle cycle even
    // though the loader normalizes animation_frame_count to one.
    const u32 frame_limit = unit.definition.movement_animation_frame_count != 0 ?
        unit.definition.animation_frame_count : 8;
    advance_idle_frame(unit, frame_limit);
}

void HandleUnitRuntimeAttackTargetState(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.dispatch_attack != nullptr) {
        dispatch_attack(context, unit);
        return;
    }

    UnitMovementUnit* target = unit.target;
    if (target != nullptr && can_attack(context, unit, *target)) {
        if (target_in_attack_range(context, unit, *target)) {
            dispatch_attack(context, unit);
            return;
        }
        path_to_target(context, unit, *target);
        unit.command_state = kUnitStateAttackTravel;
        return;
    }

    UnitMovementUnit* replacement = find_target(context, unit);
    if (replacement != nullptr && can_attack(context, unit, *replacement)) {
        SetUnitCommandTarget(unit, replacement);
        unit.command_flags &= ~8u;
        return;
    }

    unit.command_flags &= ~0x10u;
    pop_completed_runtime_state(context, unit);
}

void TickUnitRuntimeAuxTimerReset(UnitMovementUnit& unit) {
    ++unit.cell_channel_additive_frame;
    if (unit.cell_channel_additive_frame > 0x1e) {
        unit.previous_command_state = 0;
        unit.cell_channel_additive_frame = 0;
    }
}

void StartUnitRuntimeTargetValidationState(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    unit.command_state = kUnitStateRuntimeTargetValidation;
    unit.animation_frame = 0;
    UnitMovementUnit* target = unit.target;
    if (target == nullptr ||
        !validate_action_target_for_runtime(context, unit, *target)) {
        if (unit.owner_id == context.local_owner_id) {
            notify_target_validation_failed(context, unit);
            unit.command_flags &= ~0x10u;
        }
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    HandleUnitRuntimeTargetValidationState(context, unit);
}

void HandleUnitRuntimeTargetValidationState(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = unit.target;
    if (unit.type_id == 0x73 && target != nullptr &&
        (target->runtime_flags & 0x20000u) != 0) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (context.callbacks.dispatch_attack != nullptr) {
        dispatch_attack(context, unit);
        return;
    }
    if (target == nullptr || !can_attack(context, unit, *target)) {
        unit.command_flags &= ~0x10u;
        pop_completed_runtime_state(context, unit);
        return;
    }

    if (target_in_attack_range(context, unit, *target)) {
        dispatch_attack(context, unit);
        return;
    }
    path_to_target(context, unit, *target);
}

UnitQueuedCommand make_active_death_refund_command(
    const UnitMovementUnit& unit, u32 state) {
    UnitQueuedCommand active = unit.active_command_payload;
    active.state = state;
    active.x = static_cast<i32>(unit.command_value);
    return active;
}

void dispatch_death_command_refund(UnitCommandContext& context,
    UnitMovementUnit& unit, const UnitQueuedCommand& command) {
    if (context.callbacks.on_deferred_death_command_refund != nullptr) {
        context.callbacks.on_deferred_death_command_refund(context, unit, command);
    }
}

void HandleUnitDeathCommandQueueSideEffects(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    const u32 command_id = GetUnitCommandIdLow24(unit);
    if (command_id == kUnitStateProductionSpawnStart ||
        command_id == kUnitStateProductionSpawnCycle) {
        if (unit.production_reserved) {
            refund_active_production_on_death(context, unit);
        }
        else {
            dispatch_death_command_refund(context, unit,
                make_active_death_refund_command(unit, command_id));
        }
    }
    else if (command_id == kUnitStateCompletionAnnouncementStart ||
        command_id == kUnitStateCompletionAnnouncementTimer) {
        dispatch_death_command_refund(context, unit,
            make_active_death_refund_command(unit, command_id));
    }
    else if (command_id == kUnitStateCompletionEffectStart ||
        command_id == kUnitStateCompletionEffectTimer) {
        dispatch_death_command_refund(context, unit,
            make_active_death_refund_command(unit, command_id));
    }
    for (u32 index = 0; index < unit.deferred_command_count &&
         index < unit.deferred_commands.size(); ++index) {
        const UnitQueuedCommand& queued = unit.deferred_commands[index];
        const u32 queued_state = queued.state;
        if (queued_state == 0x17 || queued_state == 0x22 ||
            queued_state == 0x10) {
            dispatch_death_command_refund(context, unit, queued);
        }
    }
}

void StartUnitCompletionEffectSequence(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.action_mode = 0;
    if (unit.item_slots.size() > 2) {
        unit.item_slots[2] = 0;
        if (unit.equipment_slots.size() > 2) {
            unit.equipment_slots[2] = 0;
        }
    }
    unit.effect_timer = 0;
    unit.cell_flag40_animation_frame = 0;
    unit.command_state = kUnitStateCompletionEffectTimer;
}

void HandleUnitCompletionEffectTimer(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    ++unit.effect_timer;
    if (unit.effect_timer >= completion_effect_duration(unit)) {
        unit.effect_timer = 0;
    }
    unit.cell_flag40_animation_frame = unit.effect_timer;

    const bool completed =
        context.callbacks.advance_completion_effect != nullptr ?
        context.callbacks.advance_completion_effect(context, unit) :
        unit.effect_timer == 0;
    if (!completed) {
        return;
    }

    if (context.callbacks.on_completion_effect != nullptr) {
        context.callbacks.on_completion_effect(context, unit);
    }
    pop_completed_runtime_state(context, unit);
    HandleUnitRuntimeDispatchTick(context, unit);
}

void StartUnitCompletionAnnouncementCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.can_start_completion_announcement != nullptr &&
        !context.callbacks.can_start_completion_announcement(context, unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    unit.effect_timer = 0;
    unit.command_state = kUnitStateCompletionAnnouncementTimer;
}

void HandleUnitCompletionAnnouncementTimer(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    ++unit.effect_timer;
    const u32 visual_duration = completion_announcement_duration(unit);
    const bool visual_cycle_done = visual_duration <= unit.effect_timer;
    if (visual_cycle_done) {
        unit.effect_timer = 0;
    }

    const bool completed =
        context.callbacks.advance_completion_announcement != nullptr ?
        context.callbacks.advance_completion_announcement(context, unit) :
        visual_cycle_done;
    if (!completed) {
        return;
    }

    if (context.callbacks.on_completion_announcement != nullptr) {
        context.callbacks.on_completion_announcement(context, unit);
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
    HandleUnitRuntimeDispatchTick(context, unit);
}

void StartUnitProductionSpawnCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    const UnitProductionResourceCheck check =
        check_production_population_capacity(context, unit);
    if (!check.available) {
        notify_production_start_failed_once(context, unit, check.failure);
        return;
    }

    // The subtype-01 command enqueue path (and the owner-AI queue path) has
    // already debited the production cost.  Original state 0x50 only reserves
    // population before entering state 0x51.
    reserve_production_population(context, unit);
    unit.spawn_type_id = production_type_for_unit(context, unit);
    unit.queued_production_type_id = unit.spawn_type_id;
    unit.command_state = kUnitStateProductionSpawnCycle;
    unit.animation_frame = 0;
    unit.effect_timer = 0;
    // Original StartUnitProductionSpawnCommand (0x004cdf35/0x004cdff6)
    // clears raw unit +0x7c before immediately dispatching state 0x51.  For
    // structures that word is the cell flag-0x40 image-frame scratch, not a
    // movement destination.
    unit.cell_flag40_animation_frame = 0;
    if (context.callbacks.on_production_started != nullptr) {
        context.callbacks.on_production_started(context, unit);
    }
    HandleUnitProductionSpawnCycle(context, unit);
}

void HandleUnitProductionSpawnCycle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // Original HandleUnitProductionSpawnCycle (0x004ce00f..0x004ce023)
    // advances raw unit +0x7c using the producer's cell-animation period
    // before it advances the production completion timer at raw +0x64.  Keep
    // the typed compatibility timer mirrored, but make the render scratch the
    // canonical value so loaded/in-progress structures resume the exact frame.
    ++unit.cell_flag40_animation_frame;
    if (unit.cell_flag40_animation_frame >= production_cycle_duration(unit)) {
        unit.cell_flag40_animation_frame = 0;
    }
    unit.effect_timer = unit.cell_flag40_animation_frame;

    ++unit.animation_frame;
    const u32 type_id = production_type_for_unit(context, unit);
    // Original HandleUnitProductionSpawnCycle (0x004ce002) indexes the
    // completion timer through the queued unit type at unit +0x68
    // (DAT_00a04020), then reads that produced type's Jw2_10 duration at
    // DAT_0087c484.  Using the producer definition here made a headquarters
    // train a worker for the headquarters' 2100 ticks instead of the worker's
    // 220 ticks.
    const UnitMovementDefinition& produced_definition =
        definition_for_type_or(context, type_id, unit.definition);
    if (unit.animation_frame < produced_definition.production_spawn_time) {
        return;
    }

    UnitMovementUnit* produced = create_production_unit(context, unit, type_id);
    if (produced == nullptr) {
        refund_production_resources(context, unit);
        unit.spawn_type_id = 0;
        unit.queued_production_type_id = 0;
        if (context.callbacks.on_production_start_failed_reason != nullptr) {
            context.callbacks.on_production_start_failed_reason(
                context, unit, UnitProductionStartFailure::placement_failed);
        }
        unit.animation_frame = 0;
        pop_completed_runtime_state(context, unit);
        HandleUnitRuntimeDispatchTick(context, unit);
        return;
    }

    if (unit.item_slots.size() > 2) {
        unit.item_slots[2] = 0;
        if (unit.equipment_slots.size() > 2) {
            unit.equipment_slots[2] = 0;
        }
    }
    unit.production_reserved = false;
    unit.animation_frame = 0;
    start_produced_unit_rally_command(context, unit, *produced);
    if (context.callbacks.on_production_completed != nullptr) {
        context.callbacks.on_production_completed(context, unit, *produced);
    }
    unit.spawn_type_id = 0;
    unit.queued_production_type_id = 0;
    pop_completed_runtime_state(context, unit);
    HandleUnitRuntimeDispatchTick(context, unit);
}

void ProcessUnitIdleAcquireCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.deferred_command_count != 0) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    unit.command_flags |= 0x20;
    unit.distance_check_mode = 0;

    if ((unit.animation_frame & 0x1f) == 0) {
        if (TryStartNearbyFollowCommand(context, unit)) {
            return;
        }
        if ((unit.runtime_flags & 0x40000u) == 0 &&
            (unit.type_flags & 0x20u) != 0) {
            if (try_start_idle_map_effect_interaction(context, unit)) {
                return;
            }
            UnitMovementUnit* target = find_target(context, unit);
            if (target != nullptr) {
                SetUnitCommandTarget(unit, target);
                if (can_attack(context, unit, *target)) {
                    unit.command_state = kUnitStateAttackTarget;
                    unit.animation_frame = 0;
                    unit.command_flags &= ~8u;
                    ProcessUnitAttackTargetCommand(context, unit);
                    return;
                }
                if ((unit.runtime_flags & 8) == 0) {
                    unit.command_state = kUnitStateAttackTravel;
                    unit.command_flags &= ~8u;
                    path_to_target_without_command_flag(context, unit, *target);
                    return;
                }
            }
        }
    }

    if (advance_idle_frame(unit, unit.definition.animation_timer_period)) {
        try_start_idle_random_relocation(context, unit);
    }
}

void ProcessUnitTravelCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!movement_step(context, unit)) {
        HandleUnitReturnToIdleState(context, unit);
    }
}

void ProcessUnitAttackTravelCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* original_target = unit.target;
    if (original_target != nullptr && can_attack(context, unit, *original_target) &&
        target_in_attack_range(context, unit, *original_target)) {
        copy_target_position_to_path(unit, *original_target);
        unit.command_state = kUnitStateAttackTarget;
        ProcessUnitAttackTargetCommand(context, unit);
        return;
    }

    UnitMovementUnit* candidate = find_target(context, unit);
    if (candidate != nullptr && candidate != original_target &&
        can_attack(context, unit, *candidate) &&
        prefer_guard_target(candidate, original_target)) {
        SetUnitCommandTarget(unit, candidate);
        if (target_in_attack_range(context, unit, *candidate)) {
            copy_target_position_to_path(unit, *candidate);
            unit.command_state = kUnitStateAttackTarget;
            ProcessUnitAttackTargetCommand(context, unit);
            return;
        }
    }

    if (!movement_step(context, unit)) {
        if (unit.target != nullptr && can_attack(context, unit, *unit.target)) {
            const i32 previous_x = unit.path_target_x;
            const i32 previous_y = unit.path_target_y;
            path_to_target(context, unit, *unit.target);
            if (target_in_attack_range(context, unit, *unit.target)) {
                unit.command_state = kUnitStateAttackTarget;
                ProcessUnitAttackTargetCommand(context, unit);
            }
            else if (unit.path_target_x == previous_x &&
                unit.path_target_y == previous_y) {
                PopDeferredUnitCommandOrReturnIdle(context, unit);
            }
            return;
        }
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

void ProcessUnitAttackTargetCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    // State 0x04 begins with FUN_004c1c87 at 0x004c93d7.  Its result handler
    // owns target loss and travel transitions in the runtime configuration.
    if (context.callbacks.dispatch_attack != nullptr) {
        dispatch_attack(context, unit);
        return;
    }

    // Callback-free harness fallback.
    UnitMovementUnit* target = unit.target;
    if (target != nullptr && can_attack(context, unit, *target)) {
        if (target_in_attack_range(context, unit, *target)) {
            dispatch_attack(context, unit);
            return;
        }
        path_to_target(context, unit, *target);
        unit.command_state = kUnitStateAttackTravel;
        return;
    }

    UnitMovementUnit* replacement = find_target(context, unit);
    if (replacement != nullptr && can_attack(context, unit, *replacement)) {
        SetUnitCommandTarget(unit, replacement);
        path_to_target(context, unit, *replacement);
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void StartUnitEquipmentPointCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (!CheckPathTargetWithinAxisTile(unit)) {
        unit.command_state = kUnitStateEquipmentPointTravel;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        return;
    }
    complete_point_equipment_to_idle(context, unit);
}

void HandleUnitEquipmentPointTravel(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (movement_step(context, unit)) {
        return;
    }
    complete_point_equipment_to_idle(context, unit);
}

void StartUnitPointActionCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (!CheckPathTargetWithinAxisTile(unit)) {
        unit.command_state = kUnitStatePointActionTravel;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        return;
    }
    unit.command_state = kUnitStatePointActionReady;
}

void HandleUnitPointActionCycle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    complete_point_equipment_and_pop(context, unit);
}

void HandleUnitPointActionTravel(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (movement_step(context, unit)) {
        return;
    }
    unit.command_state = kUnitStatePointActionReady;
}

void StartUnitTargetInteractionCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target != nullptr && CheckTargetInteractionNeedsApproach(unit)) {
        path_to_target_without_command_flag(context, unit, *target);
        unit.command_state = kUnitStateTargetInteractionApproach;
        return;
    }
    unit.command_state = kUnitStateTargetInteractionCycle;
}

void HandleUnitTargetInteractionCycle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target != nullptr) {
        if (context.equipment_catalog != nullptr) {
            TransferUnitEquipmentSlot(context, unit, *target, unit.command_value,
                *context.equipment_catalog);
        }
        mark_equipment_slots_changed(context, *target);
        mark_equipment_slots_changed(context, unit);
        StartUnitCommandLockoutTimer(context, unit, 2);
    }
    else {
        const bool cleared = context.equipment_catalog != nullptr &&
            ClearUnitEquipmentSlot(context, unit, unit.command_value,
                *context.equipment_catalog);
        mark_equipment_slots_changed(context, unit);
        if (!cleared) {
            StartUnitCommandLockoutTimer(context, unit, 2);
        }
    }
    anchor_and_pop_deferred(context, unit);
}

void HandleUnitTargetInteractionApproach(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target)) {
        unit.command_state = kUnitStateTargetInteractionCycle;
        return;
    }
    if (!CheckTargetInteractionNeedsApproach(unit)) {
        unit.command_state = kUnitStateTargetInteractionCycle;
        return;
    }
    if (movement_step(context, unit)) {
        return;
    }
    if (unit.saved_path_target_x != target->x || unit.saved_path_target_y != target->y) {
        unit.saved_path_target_x = target->x;
        unit.saved_path_target_y = target->y;
        path_to_target_without_command_flag(context, unit, *target);
        return;
    }
    anchor_and_pop_deferred(context, unit);
}

void StartUnitTargetProgressCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (CheckCurrentTargetOutsideExpandedFootprint(unit)) {
        unit.command_state = kUnitStateTargetProgressApproach;
        path_to_current_target_center(context, unit);
        return;
    }
    const UnitMovementPoint center = CalculateUnitCenterPoint(*target);
    const u32 direction = CalculateUnitDirectionToPoint(unit, center.x, center.y);
    if (direction != 0) {
        unit.direction = direction;
    }
    unit.command_state = kUnitStateTargetProgressCycle;
    unit.animation_frame = 0;
}

void HandleUnitTargetProgressCycle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target)) {
        anchor_and_pop_deferred(context, unit);
        return;
    }
    if (CheckCurrentTargetOutsideExpandedFootprint(unit)) {
        unit.command_state = kUnitStateTargetProgressApproach;
        path_to_current_target_center(context, unit);
        return;
    }
    if (!CheckCurrentTargetBelowStoredHealthCap(unit)) {
        anchor_and_pop_deferred(context, unit);
        return;
    }

    ++unit.animation_frame;
    const u32 period = std::max<u32>(
        unit.definition.target_progress_animation_period, 1);
    if (unit.animation_frame >= period) {
        unit.animation_frame = 0;
    }
    if ((context.frame_counter & 3) != 0) {
        return;
    }

    const UnitTargetProgressResult result = apply_target_progress(context, unit);
    if (result.completed || result.blocked) {
        anchor_and_pop_deferred(context, unit);
    }
}

void HandleUnitTargetProgressApproach(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target) ||
        !CheckCurrentTargetBelowStoredHealthCap(unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!CheckCurrentTargetOutsideExpandedFootprint(unit)) {
        const UnitMovementPoint center = CalculateUnitCenterPoint(*target);
        const u32 direction = CalculateUnitDirectionToPoint(unit, center.x, center.y);
        if (direction != 0) {
            unit.direction = direction;
        }
        unit.command_state = kUnitStateTargetProgressCycle;
        unit.animation_frame = 0;
        return;
    }
    if (movement_step(context, unit)) {
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void StartUnitGuardAnchorCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    set_saved_anchor_from_current_command(unit);
    if (unit.command_value == 0) {
        unit.command_flags |= 0x20;
        UnitMovementUnit* target = find_target(context, unit);
        if (target != nullptr && can_attack(context, unit, *target)) {
            SetUnitCommandTarget(unit, target);
            if (!target_in_attack_range(context, unit, *target)) {
                path_to_target_without_command_flag(context, unit, *target);
                unit.command_state = kUnitStateGuardReturnTravel;
                HandleUnitGuardReturnTravel(context, unit);
                return;
            }
            unit.command_state = kUnitStateGuardCombatCycle;
            unit.animation_frame = 0;
            return;
        }
        path_to_saved_anchor(context, unit);
        unit.command_state = kUnitStateGuardReturnTravel;
        HandleUnitGuardReturnTravel(context, unit);
        return;
    }

    switch (validate_action_target(context, unit)) {
    case UnitActionTargetStatus::invalid:
        notify_target_validation_failed(context, unit);
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    case UnitActionTargetStatus::needs_approach:
        // Original StartUnitGuardAnchorCommand 0x004c99f3 calls
        // ProcessUnitPathToDestination with the point carried by the input
        // command.  It does not replace that point with target.x/target.y;
        // target-origin replanning happens later in the state-0x1d/0x1e
        // handlers (0x004c9a66 and 0x004c9ae2).  Replacing the initial click
        // point made attacks on tall sprites begin along a different line.
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        unit.command_state = kUnitStateGuardAnchorApproach;
        unit.command_flags |= 8;
        HandleUnitGuardAnchorApproach(context, unit);
        return;
    case UnitActionTargetStatus::ready:
        unit.command_state = kUnitStateGuardAnchorAction;
        unit.animation_frame = 0;
        unit.command_flags &= ~8u;
        HandleUnitGuardAnchorAction(context, unit);
        return;
    }
}

void HandleUnitGuardAnchorAction(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // State 0x1d enters FUN_004c1c87 unconditionally at 0x004c9a53.  The
    // runtime dispatcher owns dead/transient target handling, range recovery,
    // and cycle completion.  In particular an already-started cycle still
    // advances once after an earlier unit kills its target in the same frame.
    if (context.callbacks.dispatch_attack != nullptr) {
        dispatch_attack(context, unit);
        return;
    }

    // Callback-free harness fallback.
    UnitMovementUnit* target = resolve_command_target(context, unit);
    if (target != nullptr && can_attack(context, unit, *target)) {
        // Original state 0x1d handler (0x004c9a53) always enters
        // FUN_004c1c87 first.  Raw command-flags bit 4 takes its already-
        // running action-cycle path at 0x004c1c93/0x004c1d16 without another
        // range check.  A nonzero raw +0xf4 recovery counter likewise keeps
        // the current action state while only advancing its facing/cycle.
        if ((unit.command_flags & 0x10u) != 0 ||
            unit.command_lockout_ticks != 0) {
            dispatch_attack(context, unit);
            return;
        }
        if (target_in_attack_range(context, unit, *target)) {
            dispatch_attack(context, unit);
            return;
        }
        // Original state-0x1d out-of-range branch 0x004c9a66..0x004c9a89
        // copies the moving target position and replans without setting raw
        // command-flags bit 3.  Only the state-0x1e movement-failure replan
        // at 0x004c9afa sets that bit.
        path_to_target_without_command_flag(context, unit, *target);
        unit.command_state = kUnitStateGuardAnchorApproach;
        return;
    }
    set_anchor_to_current_position(unit);
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleUnitGuardAnchorApproach(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_command_target(context, unit);
    if (target == nullptr || !can_attack(context, unit, *target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (target_in_attack_range(context, unit, *target)) {
        unit.command_state = kUnitStateGuardAnchorAction;
        unit.animation_frame = 0;
        unit.command_flags &= ~8u;
        HandleUnitGuardAnchorAction(context, unit);
        return;
    }
    const i32 previous_x = unit.path_target_x;
    const i32 previous_y = unit.path_target_y;
    if (!movement_step(context, unit)) {
        path_to_target(context, unit, *target);
        if (unit.path_target_x == previous_x && unit.path_target_y == previous_y) {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
        }
    }
}

void StartUnitGuardReturnCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    set_saved_anchor_from_current_command(unit);
    unit.command_flags |= 0x20;
    UnitMovementUnit* target = find_target(context, unit);
    if (target != nullptr && can_attack(context, unit, *target)) {
        SetUnitCommandTarget(unit, target);
        if (!target_in_attack_range(context, unit, *target)) {
            path_to_target_without_command_flag(context, unit, *target);
            unit.command_state = kUnitStateGuardReturnTravel;
            HandleUnitGuardReturnTravel(context, unit);
            return;
        }
        unit.command_state = kUnitStateGuardCombatCycle;
        unit.animation_frame = 0;
        return;
    }
    path_to_saved_anchor(context, unit);
    unit.command_state = kUnitStateGuardReturnTravel;
    unit.command_flags |= 8;
    HandleUnitGuardReturnTravel(context, unit);
}

void HandleUnitGuardCombatCycle(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // State 0x20 likewise enters FUN_004c1c87 unconditionally (0x004c9be3).
    if (context.callbacks.dispatch_attack != nullptr) {
        dispatch_attack(context, unit);
        return;
    }

    // Callback-free harness fallback.
    UnitMovementUnit* current = resolve_command_target(context, unit);
    if (current != nullptr && can_attack(context, unit, *current) &&
        target_in_attack_range(context, unit, *current)) {
        dispatch_attack(context, unit);
        return;
    }

    UnitMovementUnit* candidate = find_target(context, unit);
    if (candidate != nullptr && can_attack(context, unit, *candidate) &&
        prefer_guard_target(candidate, current)) {
        SetUnitCommandTarget(unit, candidate);
        if (target_in_attack_range(context, unit, *candidate)) {
            unit.command_state = kUnitStateGuardCombatCycle;
            unit.command_flags &= ~8u;
            return;
        }
        path_to_target(context, unit, *candidate);
        unit.command_state = kUnitStateGuardPursueTarget;
        unit.command_flags |= 8u;
        return;
    }

    unit.target = nullptr;
    path_to_saved_anchor(context, unit);
    unit.command_state = kUnitStateGuardReturnTravel;
    unit.command_flags |= 8u;
}

void HandleUnitGuardReturnTravel(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* target = find_target(context, unit);
    if (target != nullptr && can_attack(context, unit, *target)) {
        SetUnitCommandTarget(unit, target);
        if (target_in_attack_range(context, unit, *target)) {
            unit.command_state = kUnitStateGuardCombatCycle;
            unit.animation_frame = 0;
            unit.command_flags &= ~8u;
            HandleUnitGuardCombatCycle(context, unit);
            return;
        }
        unit.command_state = kUnitStateGuardPursueTarget;
        unit.animation_frame = 0;
        unit.command_flags |= 8u;
        path_to_target(context, unit, *target);
        return;
    }
    if (movement_step(context, unit)) {
        return;
    }
    if (unit.target == nullptr) {
        set_anchor_to_current_position(unit);
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    unit.command_state = kUnitStateGuardCombatCycle;
    unit.command_flags &= ~8u;
    HandleUnitGuardCombatCycle(context, unit);
}

void HandleUnitGuardPursueTarget(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* current = resolve_command_target(context, unit);
    UnitMovementUnit* candidate = find_target(context, unit);
    if (candidate != nullptr && can_attack(context, unit, *candidate) &&
        prefer_guard_target(candidate, current)) {
        SetUnitCommandTarget(unit, candidate);
        if (target_in_attack_range(context, unit, *candidate)) {
            unit.command_state = kUnitStateGuardCombatCycle;
            unit.animation_frame = 0;
            unit.command_flags &= ~8u;
            HandleUnitGuardCombatCycle(context, unit);
            return;
        }
        path_to_target_without_command_flag(context, unit, *candidate);
        return;
    }
    if (current == nullptr || !can_attack(context, unit, *current)) {
        unit.target = nullptr;
        path_to_saved_anchor(context, unit);
        unit.command_state = kUnitStateGuardReturnTravel;
        unit.command_flags |= 8u;
        return;
    }
    if (target_in_attack_range(context, unit, *current)) {
        unit.command_state = kUnitStateGuardCombatCycle;
        unit.animation_frame = 0;
        unit.command_flags &= ~8u;
        HandleUnitGuardCombatCycle(context, unit);
        return;
    }
    if (!movement_step(context, unit)) {
        path_to_target_without_command_flag(context, unit, *current);
    }
}

bool complete_legacy_spawn_placement(UnitCommandContext& context,
    UnitMovementUnit& unit, u32 type_id) {
    if (!reserve_building_primary_resource(context, unit, type_id)) {
        if (context.callbacks.on_production_start_failed_reason != nullptr) {
            context.callbacks.on_production_start_failed_reason(
                context, unit, UnitProductionStartFailure::primary_resources);
        }
        return false;
    }
    UnitMovementUnit* spawned = create_legacy_spawned_unit(context, unit, type_id);
    if (spawned == nullptr) {
        refund_building_primary_resource(context, unit, type_id);
        if (context.callbacks.on_production_start_failed_reason != nullptr) {
            context.callbacks.on_production_start_failed_reason(
                context, unit, UnitProductionStartFailure::placement_failed);
        }
        return false;
    }
    // Original FUN_004c9e8f/FUN_004ca105 link the newly allocated structure
    // and worker, then put the saved worker in construction state 0x24.
    unit.runtime_flags |= 0x82u;
    unit.command_state = kUnitStateLegacySpawnConstruction;
    unit.animation_frame = 0;
    return true;
}

void StartUnitLegacySpawnPlacementCommand(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    // Original state 0x23 treats the packet payload as a building-table index
    // and converts it to the actual unit type by adding 0x60.  Using the
    // worker's own type here made every build icon create the same structure.
    if (unit.command_value < 0x60u) {
        unit.command_value += 0x60u;
    }
    const u32 type_id = unit.command_value;
    unit.spawn_type_id = type_id;
    const UnitMovementDefinition& spawn_definition =
        definition_for_type_or(context, type_id, unit.definition);
    offset_spawn_target_by_interaction_bounds(unit, spawn_definition);
    if (CheckUnitDistanceAtLeastOneTile(unit, unit.path_target_x, unit.path_target_y)) {
        const i32 requested_x = unit.path_target_x;
        const i32 requested_y = unit.path_target_y;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        if (unit.path_target_x == requested_x && unit.path_target_y == requested_y) {
            unit.command_state = kUnitStateLegacySpawnPlacementApproach;
        }
        else {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
        }
        return;
    }

    if (!complete_legacy_spawn_placement(context, unit, type_id)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

void HandleLegacySpawnedConstructionRelease(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* spawned = unit.target;
    if (spawned == nullptr) {
        spawned = &unit;
    }
    const bool spawned_destroyed = (spawned->runtime_flags & 4u) != 0;
    // Original construction state 0x24 (0x004c9f9d) runs the definition
    // timer/accumulator for every live spawned object.  It has no max-health
    // gate; zero-health special objects still wait out their construction
    // duration before footprint registration and worker release.
    if (!spawned_destroyed && !grow_spawned_unit(*spawned)) {
        return;
    }

    unit.production_reserved = false;
    unit.runtime_flags &= ~0x82u;
    unit.under_construction = false;
    if (spawned != &unit) {
        // The original releases the worker when a structure is destroyed
        // during construction, but does not register that dead structure as
        // completed (0x004c9fa3 -> 0x004ca051).
        if (!spawned_destroyed) {
            complete_spawned_construction(context, *spawned);
        }

        UnitMovementPoint release_point{
            spawned->x + spawned->definition.transport_offset_x,
            spawned->y + spawned->definition.transport_offset_y};
        bool release_point_found = false;
        if (context.callbacks.find_matching_terrain_placement_point != nullptr) {
            release_point_found =
                context.callbacks.find_matching_terrain_placement_point(
                    context, unit, release_point);
        }
        if (!release_point_found &&
            context.callbacks.find_strict_placement_point != nullptr) {
            context.callbacks.find_strict_placement_point(
                context, unit, release_point);
        }
        unit.x = release_point.x;
        unit.y = release_point.y;
        unit.current_cell_x = unit.x & ~0x1f;
        unit.current_cell_y = unit.y & ~0x1f;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleUnitLegacySpawnPlacementApproach(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    const UnitMovementDefinition& spawn_definition =
        definition_for_type_or(context, unit.spawn_type_id, unit.definition);
    // Original state 0x25 (FUN_004ca105) retains the same definition
    // +0x378/+0x37c half-bounds center calculated by state 0x23.
    const i32 placement_center_x = unit.active_command_payload.y +
        (spawn_definition.interaction_bounds_width >> 1);
    const i32 placement_center_y =
        static_cast<i32>(unit.active_command_payload.value) +
        (spawn_definition.interaction_bounds_height >> 1);
    const auto try_complete_placement = [&]() {
        if (CheckUnitDistanceAtLeastOneTile(
                unit, placement_center_x, placement_center_y)) {
            return false;
        }
        // Original state 0x25 (FUN_004ca105) does not re-enter state 0x23.
        // It spends and creates directly once the worker is within one tile.
        // The original keeps the centered command coordinates in +0x6c/+0x70,
        // while the reconstructed path engine can repurpose path_target during
        // travel. Rebuild the center from the stable raw command payload so the
        // endpoint tick cannot return the worker to idle without creating.
        const u32 type_id = unit.spawn_type_id != 0 ?
            unit.spawn_type_id : unit.command_value;
        if (!complete_legacy_spawn_placement(context, unit, type_id)) {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
        }
        return true;
    };

    // Original state 0x25 moves first and tests the placement radius on the
    // same successful movement tick (0x004ca105..0x004ca11f).  The rebuilt
    // movement helper can also report false on the endpoint tick, so retain
    // the endpoint compensation by testing once after either return value.
    const bool moved = movement_step(context, unit);
    if (try_complete_placement()) {
        return;
    }
    if (!moved) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

void ProcessUnitFollowTargetStart(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
    unit.command_flags |= 8;
    if (unit.target != nullptr && unit.target->type_id < 0x60) {
        unit.command_state = kUnitStateFollowMovingTarget;
        ProcessUnitFollowMovingTarget(context, unit);
        return;
    }
    unit.target = nullptr;
    unit.command_state = kUnitStateFollowPoint;
    ProcessUnitFollowPointTravel(context, unit);
}

void ProcessUnitFollowPointTravel(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!movement_step(context, unit)) {
        unit.anchor_x = unit.x;
        unit.anchor_y = unit.y;
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

void ProcessUnitFollowMovingTarget(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.target == nullptr || !can_follow(context, unit, *unit.target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!target_in_follow_entry_range(context, unit, *unit.target)) {
        if (!follow_target_path_point_enterable(context, unit, *unit.target)) {
            unit.command_state = kUnitStateFollowHoldRange;
            unit.animation_frame = 0;
            return;
        }
        if (movement_step(context, unit) && unit.animation_frame != 0) {
            return;
        }
        unit.path_target_x = unit.target->current_cell_x;
        unit.path_target_y = unit.target->current_cell_y;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        unit.animation_frame = 0;
        return;
    }
    unit.command_state = kUnitStateFollowHoldRange;
    unit.animation_frame = 0;
}

void ProcessUnitFollowHoldRange(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.target == nullptr || !can_follow(context, unit, *unit.target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!target_in_follow_hold_range(context, unit, *unit.target)) {
        if (follow_target_path_point_enterable(context, unit, *unit.target)) {
            unit.path_target_x = unit.target->current_cell_x;
            unit.path_target_y = unit.target->current_cell_y;
            unit.command_state = kUnitStateFollowMovingTarget;
            if (has_movement(context)) {
                ProcessUnitPathToDestination(movement(context), unit);
            }
            return;
        }
    }
    ProcessUnitAnimationTimer(unit);
}

namespace {

enum class WorkerDropoffPathAnchor {
    footprint,
    center_bounds,
};

bool TrySelectWorkerDropoffPath(UnitCommandContext& context, UnitMovementUnit& unit,
    WorkerDropoffPathAnchor anchor) {
    UnitMovementUnit* dropoff = find_dropoff(context, unit);
    if (dropoff == nullptr) {
        return false;
    }

    SetUnitCommandTarget(unit, dropoff);
    // The original uses two distinct target calculations here.  Initial
    // return setup (0x004ca1e3) calls FindNearestPointInTargetFootprint,
    // while both ProcessWorkerApproachDropoff recovery branches
    // (0x004ca4ae and 0x004ca4e7) call 0x004c36de.  The latter returns the
    // target definition's +0x360 center-bounds center, not a footprint edge.
    const UnitMovementPoint point = anchor == WorkerDropoffPathAnchor::footprint ?
        FindNearestPointInTargetFootprint(unit, *dropoff) :
        CalculateUnitMovementCenterPoint(*dropoff);
    unit.path_target_x = point.x;
    unit.path_target_y = point.y;
    return true;
}

} // namespace

void ProcessWorkerReturnToDropoff(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (has_movement(context)) {
        u32 tile_x = static_cast<u32>(unit.path_target_x) >> 5;
        const u32 tile_y = static_cast<u32>(unit.path_target_y) >> 5;
        const UnitMovementCell* cell = GetMovementCell(movement(context).map, tile_x, tile_y);
        if (cell != nullptr && (cell->flags & kMapCellBlockedTerrain) != 0) {
            --tile_x;
        }

        unit.path_target_x = static_cast<i32>(tile_x * 32 + 31);
        unit.path_target_y = static_cast<i32>(tile_y * 32 + 15);
        unit.destination_x = unit.path_target_x;
        unit.destination_y = unit.path_target_y;
    }
    unit.destination_aux_state = 0;

    if ((unit.command_value & 0x80000000u) == 0 && (unit.command_flags & 4u) == 0) {
        unit.command_flags &= ~7u;
        unit.command_flags |= 1u;
        unit.command_state = kUnitStateWorkerReturnToDropoff;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        return;
    }

    if (!TrySelectWorkerDropoffPath(context, unit,
            WorkerDropoffPathAnchor::footprint)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    unit.command_state = kUnitStateWorkerApproachDropoff;
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

void ProcessWorkerHarvestTile(UnitCommandContext& context, UnitMovementUnit& unit) {
    ++unit.animation_frame;
    if (context.callbacks.on_harvest_frame != nullptr) {
        context.callbacks.on_harvest_frame(context, unit);
    }
    // Original ProcessWorkerHarvestTile (0x004ca214) compares raw +0x64
    // against definition +0x13e4 (DAT_0087d6dc), not the generic animation
    // timer at +0x13d4.  The latter belongs to the blocked/reservation wait
    // loop and can give workers a different harvest cadence.
    const u32 harvest_period = unit.definition.timed_flag_phase_b_period;
    if (unit.animation_frame < harvest_period) {
        return;
    }
    // The original leaves raw +0x64 at the completed harvest frame while the
    // worker starts state 0x2c.  State 0x2a/0x2d explicitly reset it when the
    // worker reaches or retries the resource tile.  Clearing it here changes
    // the first return-leg direction update and the visible carry animation.
    UnitReservedTileReleaseResult release;
    if (has_movement(context)) {
        release = ReleaseUnitReservedMapTileWithIndex(movement(context), unit);
        if (!release.released) {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
    }

    // Original 0x004ca238 records period < frame before releasing the tile.
    // Equality continues into harvesting; an overshot frame releases the
    // reservation and then aborts through PopDeferredUnitCommandOrReturnIdle.
    if (unit.animation_frame > harvest_period) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    const u32 amount = context.callbacks.harvest_amount != nullptr ?
        context.callbacks.harvest_amount(context, unit) : kDefaultWorkerHarvestAmount;
    u32 consumed = amount;
    if (has_movement(context) && release.released) {
        consumed =
            ProcessHarvestableTileAmount(movement(context).map, release.tile_index, amount);
    }
    unit.cargo_amount = consumed;
    unit.command_flags |= 4;

    // Original ProcessWorkerHarvestTile (0x004ca214) keeps raw target +0x80
    // across harvest trips and searches for a dropoff only when it is null.
    UnitMovementUnit* dropoff = unit.target;
    if (dropoff == nullptr) {
        dropoff = find_dropoff(context, unit);
        if (dropoff == nullptr) {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
        SetUnitCommandTarget(unit, dropoff);
        unit.command_value = 0;
    }
    const UnitMovementPoint point = FindNearestPointInTargetFootprint(unit, *dropoff);
    unit.path_target_x = point.x;
    unit.path_target_y = point.y;
    unit.command_state = kUnitStateWorkerApproachDropoff;
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

void ProcessWorkerApproachHarvestTile(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_movement(context)) {
        return;
    }

    const UnitArrivalCheck arrival = CheckUnitDestinationArrivalStatus(movement(context), unit);
    if (arrival.direction_ready) {
        unit.movement_flags &= ~1u;
        unit.direction = arrival.direction;
        unit.command_state = kUnitStateWorkerReservedHarvest;
        unit.animation_frame = 0;
        RegisterUnitReservedMapTile(movement(context), unit);
        StartUnitCommandLockoutTimer(context, unit, 4);
        return;
    }

    if (arrival.status == 2) {
        const UnitTileSearchResult tile = FindNearestPassableTerrainTile(movement(context),
            unit.destination_x, unit.destination_y);
        if (tile.found) {
            unit.path_target_x = tile.x;
            unit.path_target_y = tile.y;
            unit.command_state = kUnitStateWorkerApproachHarvest;
            ProcessWorkerReturnToDropoff(context, unit);
            return;
        }
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    else if (arrival.status > 2) {
        const UnitTileSearchResult tile = FindNearestUnoccupiedTerrainTile(movement(context),
            unit.destination_x, unit.destination_y);
        if (tile.found) {
            unit.path_target_x = tile.x;
            unit.path_target_y = tile.y;
            // ProcessWorkerApproachHarvestTile (0x004ca2fb..0x004ca325)
            // re-enters the state-0x28 setup helper after resolving an
            // unoccupied replacement tile.  State 0x2a is assigned by that
            // helper only after it has normalized the destination/carry
            // flags, just like the status-2 recovery path below.
            unit.command_state = kUnitStateWorkerApproachHarvest;
            ProcessWorkerReturnToDropoff(context, unit);
            return;
        }
        unit.command_state = kUnitStateWorkerHarvestFailed;
        unit.cargo_amount = 0;
        return;
    }

    if (arrival.status != 1) {
        unit.movement_flags |= 1;
        if (unit.animation_frame == 0) {
            unit.path_target_x = unit.destination_x;
            unit.path_target_y = unit.destination_y;
            unit.direction = CalculateUnitDirectionToPoint(unit, unit.path_target_x,
                unit.path_target_y);
        }
    }
    const bool moved = ProcessUnitMovementStep(movement(context), unit);
    unit.movement_flags &= ~1u;
    if (moved) {
        return;
    }

    const UnitTileSearchResult tile = FindNearestPassableTerrainTile(movement(context),
        unit.destination_x, unit.destination_y);
    if (tile.found) {
        unit.path_target_x = tile.x;
        unit.path_target_y = tile.y;
        unit.command_state = kUnitStateWorkerApproachHarvest;
        ProcessWorkerReturnToDropoff(context, unit);
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void ProcessWorkerDepositCargo(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.owner_id < context.owner_resources.size()) {
        context.owner_resources[unit.owner_id] += unit.cargo_amount;
    }
    // DAT_007072ac is advanced by the same cargo value in the original.  This
    // counter feeds result/score conditions independently of the spendable
    // resource balance.
    if (unit.owner_id < context.owner_resource_score.size()) {
        context.owner_resource_score[unit.owner_id] += unit.cargo_amount;
    }
    unit.command_flags &= ~4u;
    unit.command_state = kUnitStateWorkerReturnToDropoff;
    unit.path_target_x = unit.destination_x;
    unit.path_target_y = unit.destination_y;
    if (context.callbacks.on_cargo_deposited != nullptr) {
        context.callbacks.on_cargo_deposited(context, unit);
    }
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
}

void ProcessWorkerApproachDropoff(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_movement(context)) {
        return;
    }

    const UnitArrivalCheck destination_arrival =
        CheckUnitDestinationArrivalStatus(movement(context), unit);
    if (destination_arrival.direction_ready || destination_arrival.status == 0) {
        unit.movement_flags |= 1u;
        if (unit.animation_frame == 0) {
            unit.direction = CalculateUnitDirectionToPoint(unit, unit.path_target_x,
                unit.path_target_y);
        }
    }

    const UnitTargetBoundsMovementStatus target_status =
        CheckUnitTargetBoundsMovementStatus(unit);
    if (target_status.reached) {
        unit.movement_flags &= ~1u;
        unit.direction = target_status.direction;
        unit.command_state = kUnitStateWorkerDepositCargo;
        StartUnitCommandLockoutTimer(context, unit, 4);
        return;
    }

    if (target_status.status == 0) {
        unit.target = nullptr;
        if (!TrySelectWorkerDropoffPath(context, unit,
                WorkerDropoffPathAnchor::center_bounds)) {
            unit.movement_flags &= ~1u;
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
        ProcessUnitPathToDestination(movement(context), unit);
        unit.movement_flags &= ~1u;
        return;
    }

    if (target_status.status > 1) {
        unit.path_target_x = target_status.suggested_path_x;
        unit.path_target_y = target_status.suggested_path_y;
        unit.movement_flags |= 1u;
        if (unit.animation_frame == 0) {
            unit.direction = CalculateUnitDirectionToPoint(unit, unit.path_target_x,
                unit.path_target_y);
        }
    }

    const bool moved = ProcessUnitMovementStep(movement(context), unit);
    unit.movement_flags &= ~1u;
    if (!moved) {
        if (!TrySelectWorkerDropoffPath(context, unit,
                WorkerDropoffPathAnchor::center_bounds)) {
            unit.movement_flags &= ~1u;
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
        ProcessUnitPathToDestination(movement(context), unit);
        unit.movement_flags &= ~1u;
    }
}

void ProcessWorkerReservedHarvestWait(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.movement_flags &= ~1u;
    if (!has_movement(context)) {
        return;
    }

    if ((unit.animation_frame & 1) == 0) {
        const UnitArrivalCheck arrival = CheckUnitDestinationArrivalStatus(movement(context),
            unit);
        if (arrival.direction_ready) {
            unit.direction = arrival.direction;
            unit.command_state = kUnitStateWorkerHarvestTile;
            unit.animation_frame = 0;
            RegisterUnitReservedMapTile(movement(context), unit);
            StartUnitCommandLockoutTimer(context, unit, 4);
            return;
        }
        if (arrival.status != 3) {
            unit.command_state = kUnitStateWorkerReturnToDropoff;
            return;
        }
    }
    // Original ProcessWorkerReservedHarvestWait (0x004ca513) advances raw
    // unit +0x64 and wraps it with definition DAT_0087d6cc.  The generic
    // animation helper advances raw +0xec instead, which can freeze parity.
    const u32 period = unit.definition.animation_timer_period;
    if (period != 0) {
        ++unit.animation_frame;
        if (unit.animation_frame >= period) {
            unit.animation_frame = 0;
        }
    }
}

void StartUnitPatrolRouteCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.anchor_x = unit.x;
    unit.anchor_y = unit.y;
    if (unit.destination_x == unit.x && unit.destination_y == unit.y) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    UnitMovementUnit* target = scan_patrol_target(context, unit);
    if (target != nullptr) {
        if (target_in_attack_range(context, unit, *target)) {
            unit.command_state = kUnitStatePatrolOutboundCombat;
            HandleUnitPatrolOutboundCombatTarget(context, unit);
            return;
        }
        begin_patrol_target_leg(context, unit, *target, kUnitStatePatrolOutboundLeg);
        return;
    }
    begin_patrol_leg(context, unit, unit.destination_x, unit.destination_y,
        kUnitStatePatrolOutboundLeg);
}

void HandleUnitPatrolReturnLeg(UnitCommandContext& context, UnitMovementUnit& unit) {
    handle_patrol_leg(context, unit, unit.destination_x, unit.destination_y,
        kUnitStatePatrolOutboundLeg, kUnitStatePatrolReturnCombat);
}

void HandleUnitPatrolOutboundLeg(UnitCommandContext& context, UnitMovementUnit& unit) {
    handle_patrol_leg(context, unit, unit.anchor_x, unit.anchor_y,
        kUnitStatePatrolReturnLeg, kUnitStatePatrolOutboundCombat);
}

void HandleUnitPatrolReturnCombatTarget(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    handle_patrol_combat(context, unit, unit.anchor_x, unit.anchor_y,
        kUnitStatePatrolReturnLeg);
}

void HandleUnitPatrolOutboundCombatTarget(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    handle_patrol_combat(context, unit, unit.destination_x, unit.destination_y,
        kUnitStatePatrolOutboundLeg);
}

void BeginUnitCarrierBoardingCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = unit.target;
    if (target == nullptr) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (can_start_transport_boarding(context, unit, *target)) {
        path_to_target_without_command_flag(context, unit, *target);
        unit.command_state = kUnitStateCarrierApproachBoarding;
        if (command_is_reciprocal_boarding_candidate(context, *target)) {
            target->target = &unit;
            target->command_state = kUnitStatePassengerApproachCarrier;
            target->path_target_x = unit.x;
            target->path_target_y = unit.y;
            if (has_movement(context)) {
                ProcessUnitPathToDestination(movement(context), *target);
            }
        }
        return;
    }

    if (can_start_transport_boarding(context, *target, unit)) {
        path_to_target_without_command_flag(context, unit, *target);
        unit.command_state = kUnitStatePassengerApproachCarrier;
        if (command_is_reciprocal_boarding_candidate(context, *target)) {
            target->target = &unit;
            target->command_state = kUnitStateCarrierApproachBoarding;
            target->path_target_x = unit.x;
            target->path_target_y = unit.y;
            if (has_movement(context)) {
                ProcessUnitPathToDestination(movement(context), *target);
            }
        }
        return;
    }

    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleCarrierImmediateBoarding(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* passenger = unit.target;
    if (passenger != nullptr && can_board_transport(context, unit, *passenger)) {
        board_unit(context, unit, *passenger);
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleCarrierApproachBoardingTarget(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    UnitMovementUnit* passenger = unit.target;
    if (passenger == nullptr ||
        !can_continue_transport_boarding(context, unit, *passenger)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (target_in_transport_boarding_range(unit, *passenger)) {
        HandleCarrierImmediateBoarding(context, unit);
        return;
    }

    if (movement_step(context, unit)) {
        refresh_transport_boarding_path(context, unit, *passenger);
        return;
    }

    if (!can_continue_transport_boarding(context, unit, *passenger)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    refresh_transport_boarding_path(context, unit, *passenger);
}

void HandlePassengerApproachCarrier(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* carrier = unit.target;
    if (carrier == nullptr ||
        !can_continue_transport_boarding(context, *carrier, unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (target_in_transport_boarding_range(unit, *carrier)) {
        if (can_board_transport(context, *carrier, unit)) {
            board_unit(context, *carrier, unit);
            return;
        }
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (movement_step(context, unit)) {
        refresh_transport_boarding_path(context, unit, *carrier);
        return;
    }

    if (!can_continue_transport_boarding(context, *carrier, unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (unit.animation_frame == 0 &&
        carrier->command_state == kUnitStateRuntimeIdleAcquire &&
        can_continue_transport_boarding(context, *carrier, unit)) {
        carrier->target = &unit;
        carrier->path_target_x = unit.x;
        carrier->path_target_y = unit.y;
        carrier->command_state = kUnitStateCarrierApproachBoarding;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), *carrier);
        }
        carrier->animation_frame = 0;
    }
    refresh_transport_boarding_path(context, unit, *carrier);
}

void ProcessTransportUnloadStart(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!unit_can_carry(unit)) {
        if (unit.target == nullptr) {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
        if (consume_transport_unload_delay(*unit.target)) {
            return;
        }
        if (!place_unloaded_unit(context, *unit.target, unit)) {
            unit.command_state = kUnitStateTransportAttached;
        }
        return;
    }

    if (unit.path_target_x != unit.x || unit.path_target_y != unit.y) {
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        unit.command_state = kUnitStateTransportUnloadWait;
        return;
    }

    unit.animation_frame = 0;
    unit.command_state = kUnitStateTransportUnloadChildren;
    ProcessTransportUnloadChildren(context, unit);
}

void ProcessTransportUnloadChildren(UnitCommandContext& context, UnitMovementUnit& unit) {
    advance_wrapping_command_frame(unit);
    if (consume_transport_unload_delay(unit)) {
        return;
    }

    if (!unit_can_carry(unit) || unit.cargo_amount == 0) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    UnitMovementUnit* child = find_attached_child(context, unit);
    if (child == nullptr) {
        unit.cargo_amount = 0;
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (!place_unloaded_unit(context, unit, *child)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (unit.cargo_amount != 0) {
        unit.command_state = kUnitStateTransportUnloadChildren;
    }
    else {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

void ProcessTransportUnloadWait(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (movement_step(context, unit)) {
        return;
    }
    unit.command_state = kUnitStateTransportUnloadChildren;
}

void ProcessTransportDockStart(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.target == nullptr) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    const u32 direction = CalculateUnitDirectionToPoint(unit, unit.target->x,
        unit.target->y);
    unit.direction = direction == 0 ? command_random_limit(context, 8) + 1 :
        legacy_transport_direction_remap(direction);
    unit.command_state = kUnitStateTransportDockSearch;
    unit.animation_frame = 0;
}

void ProcessTransportDockSearch(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_movement(context)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    ++unit.animation_frame;
    if (unit.definition.animation_frame_count != 0 &&
        unit.animation_frame >= unit.definition.animation_frame_count) {
        unit.animation_frame = 0;
    }

    const UnitMovementPoint delta = command_movement_frame_delta(context, unit);
    const i32 next_x = unit.x + delta.x;
    const i32 next_y = unit.y + delta.y;
    const UnitTerrainClassProbeResult probe =
        DispatchTerrainClassEntryProbeDetailed(movement(context), unit, next_x, next_y);
    if (probe.status == 0) {
        unit.x = next_x;
        unit.y = next_y;
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (probe.status == 2 && probe.blocking_unit != nullptr) {
        if (unit.target == probe.blocking_unit || unit.animation_frame != 0) {
            unit.x = next_x;
            unit.y = next_y;
            unit.current_cell_x = next_x & ~0x1f;
            unit.current_cell_y = next_y & ~0x1f;
            return;
        }

        UnitMovementUnit& blocker = *probe.blocking_unit;
        if ((blocker.runtime_flags & 0x08020060u) == 0 &&
            blocker.command_state != kUnitStateTransportDockSearch &&
            blocker.command_state != kUnitStateTransportDockStart &&
            (command_metadata_flags(context, blocker) & 1u) != 0) {
            blocker.target = &unit;
            blocker.command_state = kUnitStateTransportDockStart;
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
    }

    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void ProcessTransportTargetRelease(UnitCommandContext& context,
    UnitMovementUnit& unit, bool release_failed) {
    if (release_failed) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    unit.command_state = kUnitStateCompletionAnnouncementTimer;
    unit.command_value = 0;
}

bool ProcessTransportValidateTarget(UnitCommandContext& context,
    UnitMovementUnit& unit, bool progress_ready) {
    if (!progress_ready) {
        return false;
    }

    PopDeferredUnitCommandOrReturnIdle(context, unit);
    return true;
}

void StartReservedTileWorkCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_movement(context)) {
        return;
    }
    unit.reserved_tile_effect = nullptr;
    unit.linked_effect_slot_offset = 0;
    u32 tile_x = static_cast<u32>(unit.path_target_x) >> 5;
    const u32 tile_y = static_cast<u32>(unit.path_target_y) >> 5;
    const UnitMovementCell* cell = GetMovementCell(movement(context).map, tile_x, tile_y);
    if (cell != nullptr && (cell->flags & kMapCellBlockedTerrain) != 0) {
        --tile_x;
    }

    unit.path_target_x = static_cast<i32>(tile_x * 32 + 31);
    unit.path_target_y = static_cast<i32>(tile_y * 32 + 15);
    unit.destination_x = unit.path_target_x;
    unit.destination_y = unit.path_target_y;
    unit.destination_aux_state = 0;
    unit.anchor_x = unit.path_target_x;
    unit.anchor_y = unit.path_target_y;

    const UnitArrivalCheck arrival = CheckUnitRangeDestinationStatus(movement(context), unit);
    if (arrival.direction_ready) {
        unit.direction = arrival.direction;
        unit.command_state = kUnitStateReservedTileWork;
        unit.animation_frame = 0;
        unit.work_timer = 0;
        RegisterUnitReservedMapTile(movement(context), unit);
        StartUnitCommandLockoutTimer(context, unit, 4);
        return;
    }

    unit.command_flags &= ~7u;
    unit.command_flags |= 1;
    unit.command_state = kUnitStateReservedTileApproach;
    ProcessUnitPathToDestination(movement(context), unit);
    HandleReservedTileApproach(context, unit);
}

void HandleReservedTileWorkCycle(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.animation_frame == 0) {
        unit.work_timer = 0;
        unit.reserved_tile_effect = nullptr;
        unit.linked_effect_slot_offset = 0;
    }
    advance_reserved_tile_work_frame(unit);
    if (context.callbacks.on_harvest_frame != nullptr) {
        context.callbacks.on_harvest_frame(context, unit);
    }
    ++unit.work_timer;
    if (unit.work_timer < 0x3e) {
        return;
    }

    if (!has_movement(context)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    const UnitReservedTileReleaseResult release =
        ReleaseUnitReservedMapTileWithIndex(movement(context), unit);
    if (!release.released) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    const u32 amount = context.callbacks.harvest_amount != nullptr ?
        context.callbacks.harvest_amount(context, unit) : kDefaultWorkerHarvestAmount;
    unit.work_timer =
        ProcessHarvestableTileAmount(movement(context).map, release.tile_index, amount);

    UnitMovementUnit* target = unit.target;
    if (target == nullptr || !target_alive(target)) {
        target = find_dropoff(context, unit);
    }
    if (target == nullptr) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    unit.target = target;
    unit.reserved_tile_effect = nullptr;
    unit.linked_effect_slot_offset = 0;

    if (context.callbacks.on_reserved_tile_work_complete != nullptr) {
        context.callbacks.on_reserved_tile_work_complete(context, unit);
    }
    if (has_reserved_tile_linked_object(unit)) {
        unit.command_state = kUnitStateReservedTileLinkedObject;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleReservedTileLinkedObject(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_reserved_tile_linked_object(unit)) {
        unit.reserved_tile_effect = nullptr;
        unit.linked_effect_slot_offset = 0;
        unit.command_state = kUnitStateReservedTileStart;
        unit.animation_frame = 0;
        return;
    }
    const UnitEffectRuntime& effect = *unit.reserved_tile_effect;
    if (((effect.x + effect.y) & 7) == 0) {
        unit.direction = CalculateUnitDirectionToPoint(unit, effect.x, effect.y);
    }
}

void HandleReservedTileApproach(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_movement(context)) {
        return;
    }

    const UnitArrivalCheck arrival = CheckUnitRangeDestinationStatus(movement(context), unit);
    if (arrival.direction_ready) {
        unit.direction = arrival.direction;
        unit.command_state = kUnitStateReservedTileWork;
        unit.animation_frame = 0;
        unit.work_timer = 0;
        RegisterUnitReservedMapTile(movement(context), unit);
        StartUnitCommandLockoutTimer(context, unit, 4);
        return;
    }

    if (arrival.status == 2) {
        const UnitTileSearchResult tile = FindNearestPassableTerrainTile(movement(context),
            unit.anchor_x, unit.anchor_y);
        if (tile.found) {
            unit.path_target_x = tile.x;
            unit.path_target_y = tile.y;
            unit.command_state = kUnitStateReservedTileStart;
            return;
        }
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    else if (arrival.status > 2) {
        const UnitTileSearchResult tile = FindNearestUnoccupiedTerrainTile(movement(context),
            unit.anchor_x, unit.anchor_y);
        if (tile.found) {
            unit.path_target_x = tile.x;
            unit.path_target_y = tile.y;
            unit.command_state = kUnitStateReservedTileStart;
            StartReservedTileWorkCommand(context, unit);
            return;
        }
        unit.command_state = kUnitStateReservedTileBlockedWait;
        unit.work_timer = 0;
        return;
    }

    const bool moved = ProcessUnitMovementStep(movement(context), unit);
    if (moved) {
        return;
    }

    const UnitTileSearchResult tile = FindNearestPassableTerrainTile(movement(context),
        unit.anchor_x, unit.anchor_y);
    if (tile.found) {
        unit.path_target_x = tile.x;
        unit.path_target_y = tile.y;
        unit.command_state = kUnitStateReservedTileStart;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleReservedTileWait(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!has_movement(context)) {
        return;
    }
    if ((unit.animation_frame & 1) == 0) {
        const UnitArrivalCheck arrival = CheckUnitRangeDestinationStatus(movement(context), unit);
        if (arrival.direction_ready) {
            unit.direction = arrival.direction;
            unit.command_state = kUnitStateReservedTileWork;
            unit.work_timer = 0;
            RegisterUnitReservedMapTile(movement(context), unit);
            StartUnitCommandLockoutTimer(context, unit, 4);
            return;
        }
        if (arrival.status != 3) {
            unit.command_state = kUnitStateReservedTileApproach;
            return;
        }
    }
    advance_wrapping_command_frame(unit);
}

void HandleReservedTileRetryDelay(UnitCommandContext& context, UnitMovementUnit& unit) {
    ++unit.effect_timer;
    if (unit.effect_timer < 100) {
        advance_wrapping_command_frame(unit);
        return;
    }
    unit.animation_frame = 0;
    unit.effect_timer = 0;
    unit.command_state = kUnitStateReservedTileStart;
}

void StartUnitSpawnPlacementCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.path_target_y == -1) {
        if (unit.target != nullptr) {
            unit.target->target = nullptr;
        }
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    // State 0x5a uses the same building-index payload convention as state
    // 0x23.  Preserve the converted type through approach/wait/cycle states.
    if (unit.command_value < 0x60u) {
        unit.command_value += 0x60u;
    }
    const u32 type_id = unit.command_value;
    unit.spawn_type_id = type_id;
    const UnitMovementDefinition& spawn_definition =
        definition_for_type_or(context, type_id, unit.definition);
    offset_spawn_target_by_footprint(unit, spawn_definition);
    if (!CheckUnitSpawnDistanceThreshold(
            context, unit, unit.path_target_x, unit.path_target_y)) {
        enter_spawn_cycle(context, unit, kUnitStateSpawnCreateCycle);
        return;
    }

    const i32 requested_x = unit.path_target_x;
    const i32 requested_y = unit.path_target_y;
    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
    if (unit.path_target_x == requested_x && unit.path_target_y == requested_y) {
        unit.command_state = kUnitStateSpawnPlacementWait;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleUnitSpawnCreateCycle(UnitCommandContext& context, UnitMovementUnit& unit) {
    handle_spawn_cycle(context, unit, spawn_type_for_unit(unit, unit.type_id + 0x60));
}

void HandleUnitSpawnPlacementWait(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (!movement_step(context, unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!CheckUnitSpawnDistanceThreshold(
            context, unit, unit.path_target_x, unit.path_target_y)) {
        enter_spawn_cycle(context, unit, kUnitStateSpawnCreateCycle);
    }
}

void StartTargetedUnitSpawnPlacement(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = unit.target;
    if (target == nullptr || !target_alive(target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    unit.spawn_type_id = spawn_type_for_unit(unit, target->type_id);
    offset_spawn_target_by_footprint(unit, target->definition);
    if (!CheckUnitSpawnDistanceThreshold(
            context, unit, unit.path_target_x, unit.path_target_y)) {
        enter_spawn_cycle(context, unit, kUnitStateTargetedSpawnCycle);
        return;
    }

    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
    unit.command_state = kUnitStateTargetedSpawnApproach;
}

void HandleTargetedUnitSpawnCycle(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = unit.target;
    if (target == nullptr || !target_alive(target) ||
        !CheckCurrentTargetBelowStoredHealthCap(unit)) {
        anchor_and_pop_deferred(context, unit);
        return;
    }
    if (targeted_spawn_needs_approach(unit, *target)) {
        unit.command_state = kUnitStateTargetedSpawnApproach;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        return;
    }

    if (unit.animation_frame + 1 <= spawn_cycle_duration(unit)) {
        ++unit.animation_frame;
        return;
    }

    if (target->action_mode_gate == 1 && target->target == nullptr) {
        target->target = &unit;
        unit.command_state = kUnitStateSpawnCreateCycle;
        unit.animation_frame = 0;
        return;
    }

    if ((context.frame_counter & 3u) == 0) {
        const UnitTargetProgressResult result = apply_target_progress(context, unit);
        if (result.completed || result.blocked) {
            anchor_and_pop_deferred(context, unit);
        }
    }
}

void HandleTargetedUnitSpawnApproach(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = unit.target;
    if (target == nullptr || !target_alive(target) ||
        !CheckCurrentTargetBelowStoredHealthCap(unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!targeted_spawn_needs_approach(unit, *target)) {
        enter_spawn_cycle(context, unit, kUnitStateTargetedSpawnCycle);
        return;
    }
    if (!movement_step(context, unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

void StartLinkedUnitReleaseCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* linked = unit.target != nullptr ?
        unit.target : find_unit_by_id(context, unit.linked_object_id);
    if (unit.path_target_y != -1 && linked != nullptr && target_alive(linked)) {
        SetUnitCommandTarget(unit, linked);
        unit.command_state = kUnitStateLinkedUnitReleaseApproach;
        unit.animation_frame = 0;
        path_to_target_without_command_flag(context, unit, *linked);
        return;
    }

    UnitMovementUnit* reciprocal = linked != nullptr && linked->target != &unit ?
        linked->target : nullptr;
    const bool release_primary = linked_release_cycle_unit_alive(linked);
    const bool release_reciprocal = unit.type_id == 0x2b &&
        linked_release_cycle_unit_alive(reciprocal);
    if (release_primary) {
        release_linked_unit(context, unit, *linked);
        PopDeferredUnitCommandOrReturnIdle(context, *linked);
    }
    if (release_reciprocal) {
        release_linked_unit(context, unit, *reciprocal);
        PopDeferredUnitCommandOrReturnIdle(context, *reciprocal);
    }

    const u32 restore_type = unit.command_value != 0 ?
        unit.command_value : unit.saved_type_id;
    if (restore_type != 0) {
        set_unit_type_for_command(context, unit, restore_type);
        unit.saved_type_id = 0;
    }
    unit.command_lockout_ticks = 0;
    unit.animation_timer = 0;
    unit.linked_object_id = 0;
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleLinkedUnitReleaseCycle(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* linked = unit.target;
    if (!linked_release_cycle_unit_alive(linked)) {
        unit.command_state |= kUnitCommandDead;
        return;
    }
    if (unit.command_value == 0) {
        return;
    }

    ++unit.animation_frame;
    if (unit.animation_frame > 0x1f) {
        unit.animation_frame = 0;
    }
    if (unit.command_lockout_ticks != 0) {
        return;
    }

    UnitMovementUnit* reciprocal = linked->target != &unit ? linked->target : nullptr;
    const bool consume_reciprocal = unit.type_id == 0x2b &&
        linked_release_cycle_unit_alive(reciprocal);

    if (context.callbacks.clear_footprint != nullptr) {
        context.callbacks.clear_footprint(context, unit);
    }

    UnitRuntimeStatBlock unit_stats = linked_release_runtime_stats(unit);
    const UnitRuntimeStatBlock linked_stats = linked_release_runtime_stats(*linked);
    UnitRuntimeStatBlock reciprocal_stats{};
    if (consume_reciprocal) {
        reciprocal_stats = linked_release_runtime_stats(*reciprocal);
    }

    // FUN_00408c80 commits the already-selected +0x1d4/0x2b type.  It does
    // not restore saved_type_id: the linked children are consumed into the new
    // unit and their health/secondary ratios, progress and level are merged.
    unit.type_flags = unit.definition.type_flags;
    unit.script_bit_flags = unit.definition.initial_script_bit_flags;
    unit.command_bits.fill(0);
    const bool equipment_footprint_flag = context.equipment_catalog != nullptr &&
        CalculateUnitEquipmentCommandFlagModifier(
            unit, *context.equipment_catalog) != 0;
    if ((unit.definition.footprint_flags & 2u) != 0 ||
        equipment_footprint_flag) {
        unit.command_flags |= 0x40u;
    }
    RebuildUnitRuntimeStatsFromDefinitionAndParents(unit, unit_stats,
        linked, &linked_stats,
        consume_reciprocal ? reciprocal : nullptr,
        consume_reciprocal ? &reciprocal_stats : nullptr,
        context.callbacks.variant_random_limit);

    if (context.callbacks.clear_footprint != nullptr) {
        context.callbacks.clear_footprint(context, *linked);
        if (consume_reciprocal) {
            context.callbacks.clear_footprint(context, *reciprocal);
        }
    }
    linked->command_flags |= 0x100u;
    if (consume_reciprocal) {
        reciprocal->command_flags |= 0x100u;
    }

    if (context.callbacks.on_linked_unit_released != nullptr) {
        context.callbacks.on_linked_unit_released(context, unit, *linked);
        if (consume_reciprocal) {
            context.callbacks.on_linked_unit_released(context, unit, *reciprocal);
        }
    }
    const u32 old_type = unit.command_value != 0 ?
        unit.command_value : unit.saved_type_id;
    if (context.callbacks.on_unit_type_replaced != nullptr &&
        old_type != unit.type_id) {
        context.callbacks.on_unit_type_replaced(
            context, unit, old_type, unit.type_id);
    }
    unit.saved_type_id = 0;
    unit.linked_object_id = 0;
    if (context.callbacks.set_footprint != nullptr) {
        context.callbacks.set_footprint(context, unit);
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

enum class LinkedUnitReleaseReadiness {
    ready,
    retry,
    blocked,
};

LinkedUnitReleaseReadiness CheckLinkedUnitReleaseReadiness(
    UnitCommandContext& context, UnitMovementUnit& unit);

void HandleLinkedUnitReleaseApproach(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* linked = unit.target;
    if (linked == nullptr || !target_alive(linked)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    const LinkedUnitReleaseReadiness readiness =
        CheckLinkedUnitReleaseReadiness(context, unit);
    if (readiness == LinkedUnitReleaseReadiness::blocked) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (readiness == LinkedUnitReleaseReadiness::retry) {
        if (!movement_step(context, unit) || unit.animation_frame == 0) {
            path_to_target_without_command_flag(context, unit, *linked);
        }
        return;
    }

    unit.command_state = kUnitStateLinkedUnitReleaseCycle;
    unit.animation_frame = 0;
    unit.saved_type_id = unit.type_id;
    unit.command_value = unit.type_id;
    const bool direct_linked_release = linked->target == &unit;
    const u32 release_type = direct_linked_release ?
        unit.definition.linked_release_type_id : 0x2b;
    set_unit_type_for_command(context, unit, release_type);
    unit.command_lockout_ticks = unit.definition.production_spawn_time;
    unit.animation_timer = 0;
    enter_linked_release_child_cycle(context, unit, *linked);
    if (unit.type_id == 0x2b && linked->target != nullptr &&
        linked->target != &unit) {
        enter_linked_release_child_cycle(context, unit, *linked->target);
    }
}

void StartUnitSpecialAbilityCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.saved_path_target_x = unit.path_target_x;
    unit.saved_path_target_y = unit.path_target_y;
    unit.active_command_payload.y = unit.path_target_x;
    unit.active_command_payload.value = static_cast<u32>(unit.path_target_y);
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    const UnitCommandAbilityGateResult gate =
        check_ability_gate(context, unit, target);
    if (gate == UnitCommandAbilityGateResult::fail) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (gate == UnitCommandAbilityGateResult::approach) {
        unit.command_state = kUnitStateSpecialAbilityApproach;
        if (has_movement(context)) {
            ProcessUnitPathToDestination(movement(context), unit);
        }
        return;
    }

    if (unit.ability_id == 0x18) {
        execute_ability(context, unit, target);
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (unit.ability_id == 0x0a || unit.ability_id == 0x1c) {
        if (target == nullptr || (target->runtime_flags & 4u) == 0 ||
            target->path_target_x == 1) {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
    }

    // Original helper 0x004ef6cd returns carry only for an unavailable action,
    // insufficient resources, or an enabled attachment whose pool allocation
    // fails.  Every successful non-Corrupt action enters state 0x65; corpse
    // marker zero and action 0x19 are not immediate-pop cases.
    if (!start_ability_attachment(context, unit, target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    enter_special_ability_timer(context, unit, target);
}

void HandleUnitSpecialAbilityTimer(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (CheckUnitActionImpactFrame(unit)) {
        execute_ability(context, unit, unit.target);
        if (unit.ability_id == 0 || unit.ability_id == 0x10) {
            unit.command_flags &= ~0x10u;
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
    }
    ++unit.animation_frame;
    if (unit.animation_frame < ability_cycle_duration(unit)) {
        return;
    }

    unit.command_flags &= ~0x10u;
    if (unit.ability_id == 9) {
        unit.command_state |= kUnitCommandDead;
        return;
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleUnitSpecialAbilityApproach(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    const UnitCommandAbilityGateResult gate =
        check_ability_gate(context, unit, target);
    if (gate == UnitCommandAbilityGateResult::fail) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (gate == UnitCommandAbilityGateResult::ready) {
        unit.command_state = kUnitStateSpecialAbilityStart;
        unit.animation_frame = 0;
        if (target == nullptr) {
            unit.path_target_x = unit.active_command_payload.y;
            unit.path_target_y = static_cast<i32>(unit.active_command_payload.value);
        }
        return;
    }
    if (!movement_step(context, unit)) {
        if (target == nullptr) {
            PopDeferredUnitCommandOrReturnIdle(context, unit);
            return;
        }
        path_to_ability_target(context, unit);
    }
}

void HandleUnitRestoreTargetCycle(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target) ||
        target->max_health == 0 || target->health >= target->max_health) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    const UnitCommandAbilityGateResult gate =
        check_ability_gate(context, unit, target);
    if (gate == UnitCommandAbilityGateResult::fail) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (gate == UnitCommandAbilityGateResult::approach) {
        unit.command_state = kUnitStateRestoreTargetApproach;
        path_to_target_without_command_flag(context, unit, *target);
        return;
    }
    const u32 secondary_cost = ability_secondary_cost(context, unit, target);
    if (unit.secondary_value < secondary_cost) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    unit.secondary_value -= secondary_cost;
    target->runtime_flags |= 0x200;
    const i32 health_delta = ability_target_health_delta(context, unit, target);
    if (health_delta >= 0) {
        const u32 delta = static_cast<u32>(health_delta);
        target->health = target->max_health - target->health < delta
            ? target->max_health
            : target->health + delta;
    }
    else {
        const u32 delta = static_cast<u32>(-health_delta);
        target->health = target->health > delta ? target->health - delta : 0;
    }
    ++unit.animation_frame;
    if (unit.animation_frame >= ability_cycle_duration(unit)) {
        unit.animation_frame = 0;
    }
}

void HandleUnitRestoreTargetApproach(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    const UnitCommandAbilityGateResult gate =
        check_ability_gate(context, unit, target);
    if (gate == UnitCommandAbilityGateResult::fail) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (gate == UnitCommandAbilityGateResult::ready) {
        unit.command_state = kUnitStateRestoreTargetCycle;
        unit.animation_frame = 0;
        return;
    }
    if (!movement_step(context, unit)) {
        path_to_target_without_command_flag(context, unit, *target);
    }
}

void HandleUnitRandomRelocation(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.direction = legacy_random_relocation_direction(unit.direction);
    const u32 relocation_ticks =
        static_cast<u32>(unit.active_command_payload.x) + 1u;
    unit.active_command_payload.x = static_cast<i32>(relocation_ticks);
    if (relocation_ticks <= 0x0e) {
        return;
    }

    if (context.callbacks.clear_footprint != nullptr) {
        context.callbacks.clear_footprint(context, unit);
    }

    UnitMovementPoint point{unit.x, unit.y};
    bool placement_found = context.callbacks.find_relocation_point != nullptr &&
        context.callbacks.find_relocation_point(context, unit, point);
    if (placement_found && context.callbacks.find_strict_placement_point != nullptr) {
        placement_found =
            context.callbacks.find_strict_placement_point(context, unit, point);
    }
    if (placement_found) {
        unit.x = point.x;
        unit.y = point.y;
        unit.destination_x = point.x;
        unit.destination_y = point.y;
        unit.current_cell_x = point.x & ~0x1f;
        unit.current_cell_y = point.y & ~0x1f;
    }

    unit.active_command_payload.x = 0;
    unit.runtime_flags &= 0xf7ffffffu;
    unit.command_flags &= 0xffffefefu;
    HandleUnitReturnToIdleState(context, unit);
    if (context.callbacks.set_footprint != nullptr) {
        context.callbacks.set_footprint(context, unit);
    }
}

void HandleUnitMorphEnterTimer(UnitCommandContext& context, UnitMovementUnit& unit) {
    ++unit.animation_frame;
    if (unit.animation_frame < 0x10) {
        return;
    }

    unit.runtime_flags |= 0x40000;
    if (unit.type_id == 0x28) {
        unit.runtime_stat_20 += 0x1e;
    }
    const u32 morph_type = unit.definition.morph_type_id;
    if (morph_type != 0) {
        set_unit_type_for_command(context, unit, morph_type);
    }
    unit.saved_type_flags = unit.type_flags;
    unit.type_flags = 0x08002011;
    unit.movement_step_accumulator = 0;
    unit.work_timer = 0;
    unit.pending_command = {};
    unit.deferred_commands = {};
    unit.deferred_command_count = 0;
    unit.action_mode_gate = 0;
    unit.linked_object_id = 0;
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleUnitMorphExitTimer(UnitCommandContext& context, UnitMovementUnit& unit) {
    ++unit.animation_frame;
    if (unit.animation_frame < 0x10) {
        return;
    }

    unit.runtime_flags &= ~0x40000u;
    const u32 exit_type = unit.definition.morph_type_id;
    set_unit_type_for_command(context, unit, exit_type);
    if (unit.type_id == 0x28) {
        unit.runtime_stat_20 -= 0x1e;
    }
    unit.type_flags = unit.saved_type_flags;
    unit.movement_step_accumulator = 0;
    unit.work_timer = 0;
    unit.pending_command = {};
    unit.deferred_commands = {};
    unit.deferred_command_count = 0;
    unit.action_mode_gate = 0;
    unit.linked_object_id = 0;
    if (unit.type_id == 0x25 &&
        context.callbacks.find_strict_placement_point != nullptr) {
        UnitMovementPoint point{unit.x, unit.y};
        if (context.callbacks.find_strict_placement_point(context, unit, point)) {
            unit.x = point.x;
            unit.y = point.y;
            unit.current_cell_x = point.x & ~0x1f;
            unit.current_cell_y = point.y & ~0x1f;
        }
    }
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void StartUnitValueTransferCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!CheckCurrentTargetFootprintSeparated(context, unit)) {
        unit.command_state = kUnitStateValueTransferCycle;
        unit.animation_frame = 0;
        HandleUnitReturnToIdleState(context, *target);
        return;
    }

    unit.command_state = kUnitStateValueTransferApproach;
    path_to_target_without_command_flag(context, unit, *target);
    HandleUnitReturnToIdleState(context, *target);
}

void HandleUnitValueTransferCycle(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    if (unit.action_mode > unit.command_value) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    const u32 amount = unit.command_value - unit.action_mode;
    if (target->action_mode <= amount) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    unit.action_mode += amount;
    target->action_mode -= amount;
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleUnitValueTransferApproach(UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* target = resolve_active_payload_target_or_clear(context, unit);
    if (target == nullptr || !target_alive(target)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }
    if (!CheckCurrentTargetFootprintSeparated(context, unit)) {
        unit.command_state = kUnitStateValueTransferCycle;
        unit.animation_frame = 0;
        return;
    }
    const bool advanced = movement_step(context, unit);
    if (!advanced || unit.animation_frame == 0) {
        path_to_target_without_command_flag(context, unit, *target);
    }
}

void HandleUnitTimedFlagPhaseA(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.runtime_flags &= 0xf9ffffffu;
    unit.runtime_flags |= 0x01000000u;
    ++unit.animation_frame;
    if (unit.animation_frame < timed_phase_a_duration(unit)) {
        return;
    }
    unit.runtime_flags &= 0xfeffffffu;
    unit.runtime_flags |= 0x00800000u;
    unit.command_state = unit.previous_command_state;
}

void HandleUnitTimedFlagPhaseB(UnitCommandContext&, UnitMovementUnit& unit) {
    unit.runtime_flags &= 0xfe7fffffu;
    unit.runtime_flags |= 0x04000000u;
    ++unit.animation_frame;
    if (unit.animation_frame < timed_phase_b_duration(unit)) {
        return;
    }
    unit.runtime_flags &= 0xfbffffffu;
    unit.runtime_flags |= 0x02000000u;
    unit.command_state = unit.previous_command_state;
    unit.animation_frame = 0;
}

void StartUnitItemSlotUseCommand(UnitCommandContext& context, UnitMovementUnit& unit) {
    unit.saved_path_target_x = unit.path_target_x;
    unit.saved_path_target_y = unit.path_target_y;
    unit.active_command_payload.y = unit.path_target_x;
    unit.active_command_payload.value = static_cast<u32>(unit.path_target_y);
    if (CheckSavedCommandPointWithinOneTile(unit)) {
        unit.command_state = kUnitStateItemSlotUseAction;
        unit.animation_frame = 0;
        return;
    }

    if (has_movement(context)) {
        ProcessUnitPathToDestination(movement(context), unit);
    }
    unit.command_state = kUnitStateItemSlotUseApproach;
}

void HandleUnitItemSlotUseAction(UnitCommandContext& context, UnitMovementUnit& unit) {
    u32* slot = selected_item_slot(unit);
    if (slot == nullptr || *slot == 0) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    const u32 item_id = *slot;
    if (item_id != kItemSlotUsableItemId) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
        return;
    }

    execute_item_slot_use(context, unit, item_id);
    *slot = 0;
    PopDeferredUnitCommandOrReturnIdle(context, unit);
}

void HandleUnitItemSlotUseApproach(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (CheckSavedCommandPointWithinOneTile(unit)) {
        unit.path_target_x = unit.active_command_payload.y;
        unit.path_target_y = static_cast<i32>(unit.active_command_payload.value);
        unit.command_state = kUnitStateItemSlotUseAction;
        unit.animation_frame = 0;
        return;
    }

    if (!movement_step(context, unit)) {
        PopDeferredUnitCommandOrReturnIdle(context, unit);
    }
}

void HandleUnitPassiveRecoveryAndTimedRemoval(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if ((unit.runtime_flags & 0x10) != 0) {
        if ((context.frame_counter & 0x1f) != 0 ||
            unit.command_state == kUnitStateTransportAttached) {
            return;
        }
        --unit.secondary_value;
        if (unit.secondary_value == 0) {
            unit.command_state |= kUnitCommandDead;
        }
        return;
    }

    const u32 max_health = unit.max_health;
    if (max_health != 0 && unit.health < max_health &&
        unit.definition.passive_recovery_enabled != 0 &&
        (unit.command_flags & 0x2000) == 0) {
        bool recover_health = false;
        if (unit.action_mode != 0) {
            --unit.action_mode;
            recover_health = true;
        } else if ((context.frame_counter & 0x1f) == 0 &&
            unit.definition.passive_recovery_flags == 0) {
            recover_health = true;
        }
        if (recover_health) {
            ++unit.health;
            if (unit.health > max_health) {
                unit.health = max_health;
            }
        }
    }

    if ((context.frame_counter & 0xf) != 0 || (unit.command_flags & 0x800) != 0) {
        return;
    }
    if ((unit.draw_flags & 0x80) != 0) {
        if (unit.secondary_value != 0) {
            --unit.secondary_value;
            if (unit.secondary_value != 0) {
                return;
            }
        }
        unit.draw_flags &= ~0x80u;
        unit.command_flags &= ~0x40u;
        return;
    }

    const u32 max_secondary =
        CalculateUnitRuntimeMaxSecondaryValueWithProductionEffect01(
            command_production_state_or_empty(context), unit);
    if (max_secondary > unit.secondary_value) {
        ++unit.secondary_value;
        if (unit.secondary_value > max_secondary) {
            unit.secondary_value = max_secondary;
        }
    }
}

u32 status_secondary_recharge_amount(UnitCommandContext& context,
    UnitMovementUnit& unit) {
    if (context.callbacks.status_secondary_recharge_amount != nullptr) {
        return context.callbacks.status_secondary_recharge_amount(context, unit);
    }
    return 1;
}

void AddUnitSecondaryValueClamped(UnitMovementUnit& unit, u32 amount) {
    unit.secondary_value = std::min(unit.secondary_value + amount,
        unit.max_secondary_value);
}

void HandleUnitStatusEffectTimer(UnitCommandContext& context, UnitMovementUnit& unit) {
    if (unit.action_mode == 0) {
        unit.command_flags &= 0xfffc3fffu;
        return;
    }

    if ((unit.command_flags & 0x8000) == 0) {
        --unit.action_mode;
        return;
    }

    if (unit.secondary_value < unit.max_secondary_value) {
        --unit.action_mode;
        AddUnitSecondaryValueClamped(unit,
            status_secondary_recharge_amount(context, unit));
    }
}

bool CheckUnitDistanceAtLeastOneTile(const UnitMovementUnit& unit, i32 x, i32 y) {
    if (unit.distance_check_mode == 1) {
        return true;
    }
    return CalculateApproxUnitDistance(unit.x, unit.y, x, y) >= 0x20;
}

bool CheckUnitSpawnDistanceThreshold(const UnitCommandContext& context,
    const UnitMovementUnit& unit, i32 x, i32 y) {
    if (unit.distance_check_mode == 1) {
        return true;
    }
    const u32 distance = CalculateApproxUnitDistance(unit.x, unit.y, x, y);
    const u32 interaction_range =
        CalculateUnitInteractionRangeWithProductionAndEquipmentEffects(
            command_production_state_or_empty(context), unit,
            unit.definition.effect_adjusted_interaction_range_base,
            command_equipment_catalog(context));
    return distance > (interaction_range >> 1);
}

bool linked_release_definition_transport_size(UnitCommandContext& context,
    u32 type_id, u32& transport_size) {
    if (context.callbacks.find_definition == nullptr || type_id == 0) {
        return false;
    }
    const UnitMovementDefinition* definition =
        context.callbacks.find_definition(context, type_id);
    if (definition == nullptr) {
        return false;
    }
    transport_size = definition->transport_size;
    return true;
}

bool linked_release_population_gate_allows(UnitCommandContext& context,
    UnitMovementUnit& unit, UnitMovementUnit& linked,
    UnitProductionStartFailure* failure = nullptr) {
    constexpr u32 kLinkedReleaseTypeA = 0x24;
    constexpr u32 kLinkedReleaseTypeB = 0x27;
    constexpr u32 kLinkedReleaseTypeC = 0x28;
    constexpr u32 kLinkedReleaseCombinedType = 0x2b;

    if (failure != nullptr) {
        *failure = UnitProductionStartFailure::none;
    }

    u32 before_transport_size = 0;
    u32 after_transport_size = 0;
    if (linked.target == &unit) {
        before_transport_size = unit.definition.transport_size * 2;
        if (!linked_release_definition_transport_size(context,
                unit.definition.linked_release_type_id, after_transport_size)) {
            return true;
        }
    }
    else {
        u32 type_a = 0;
        u32 type_b = 0;
        u32 type_c = 0;
        if (!linked_release_definition_transport_size(context, kLinkedReleaseTypeA,
                type_a) ||
            !linked_release_definition_transport_size(context, kLinkedReleaseTypeB,
                type_b) ||
            !linked_release_definition_transport_size(context, kLinkedReleaseTypeC,
                type_c) ||
            !linked_release_definition_transport_size(context,
                kLinkedReleaseCombinedType, after_transport_size)) {
            return true;
        }
        before_transport_size = type_a + type_b + type_c;
    }

    if (after_transport_size == before_transport_size) {
        return true;
    }
    const u32 owner = unit.owner_id;
    if (owner >= context.owner_population_reserved.size() ||
        owner >= context.owner_population_used.size() ||
        owner >= context.owner_population_limit.size()) {
        return true;
    }
    const i64 projected = static_cast<i64>(after_transport_size) +
        static_cast<i64>(context.owner_population_reserved[owner]) -
        static_cast<i64>(before_transport_size);
    const i64 available = std::min(context.owner_population_used[owner],
        context.owner_population_limit[owner]);
    if (projected <= available) {
        return true;
    }
    if (failure != nullptr) {
        *failure = available < context.owner_population_limit[owner] ?
            UnitProductionStartFailure::population_capacity :
            UnitProductionStartFailure::population_limit;
    }
    return false;
}

u32 linked_release_center_distance(const UnitMovementUnit& unit,
    const UnitMovementUnit& target) {
    const UnitMovementPoint source_center = CalculateUnitCenterPoint(unit);
    const UnitMovementPoint target_center = CalculateUnitCenterPoint(target);
    return CalculateApproxUnitDistance(source_center.x, source_center.y,
        target_center.x, target_center.y);
}

LinkedUnitReleaseReadiness CheckLinkedUnitReleaseReadiness(
    UnitCommandContext& context, UnitMovementUnit& unit) {
    UnitMovementUnit* linked = unit.target != nullptr ?
        unit.target : find_unit_by_id(context, unit.linked_object_id);
    if (linked == nullptr || !target_alive(linked)) {
        return LinkedUnitReleaseReadiness::blocked;
    }
    UnitProductionStartFailure population_failure =
        UnitProductionStartFailure::none;
    if (!linked_release_population_gate_allows(context, unit, *linked,
            &population_failure)) {
        if (unit.owner_id == context.local_owner_id &&
            population_failure != UnitProductionStartFailure::none &&
            context.callbacks.on_linked_release_population_blocked != nullptr) {
            context.callbacks.on_linked_release_population_blocked(
                context, unit, population_failure);
        }
        return LinkedUnitReleaseReadiness::blocked;
    }
    if ((linked->runtime_flags & 4) != 0 ||
        (command_metadata_flags(context, *linked) & 0x80u) == 0) {
        return LinkedUnitReleaseReadiness::blocked;
    }
    if (linked_release_center_distance(unit, *linked) > 10) {
        return LinkedUnitReleaseReadiness::retry;
    }

    if (linked->target == &unit) {
        return LinkedUnitReleaseReadiness::ready;
    }

    UnitMovementUnit* reciprocal = linked->target;
    if (reciprocal == nullptr || reciprocal->target != &unit ||
        !target_alive(reciprocal) || (reciprocal->runtime_flags & 4) != 0 ||
        (command_metadata_flags(context, *reciprocal) & 0x80u) == 0) {
        return LinkedUnitReleaseReadiness::blocked;
    }
    return linked_release_center_distance(unit, *reciprocal) <= 10 ?
        LinkedUnitReleaseReadiness::ready : LinkedUnitReleaseReadiness::retry;
}

bool CheckLinkedUnitReleaseReady(UnitCommandContext& context, UnitMovementUnit& unit) {
    return CheckLinkedUnitReleaseReadiness(context, unit) ==
        LinkedUnitReleaseReadiness::ready;
}

bool CheckSavedCommandPointWithinOneTile(const UnitMovementUnit& unit) {
    return CalculateApproxUnitDistance(unit.x, unit.y, unit.saved_path_target_x,
        unit.saved_path_target_y) < 0x20;
}

u32 CalculateOwnerTransportGroupRequiredCarrierCount(const UnitCommandContext& context,
    u32 owner_id, u32 group_id, u32 carrier_capacity) {
    if (group_id >= kOwnerTransportQueueSlotCount || carrier_capacity == 0 ||
        context.movement == nullptr) {
        return 0;
    }

    u32 required_capacity = 0;
    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr) {
            continue;
        }
        if (unit->owner_id != owner_id ||
            (unit->area_marker_flags & 0xffu) != group_id) {
            continue;
        }
        if ((unit->definition.transport_flags & 4) == 0 ||
            (unit->runtime_flags & 4) != 0) {
            continue;
        }
        const u32 unit_transport_size = transport_size(*unit);
        required_capacity += unit_transport_size;
        if (unit_transport_size > carrier_capacity &&
            context.callbacks.on_oversized_transport_passenger != nullptr) {
            context.callbacks.on_oversized_transport_passenger(context, *unit,
                owner_id, group_id, carrier_capacity);
        }
    }
    if (required_capacity == 0) {
        return 0;
    }
    return (required_capacity - 1 + carrier_capacity) / carrier_capacity;
}

u32 CountOwnerTransportGroupReservedCarriers(const OwnerTransportQueueState& queue,
    u32 group_id) {
    u32 count = 0;
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.linked_group != group_id) {
            continue;
        }
        if (slot.state >= kOwnerTransportQueueStatePendingA &&
            slot.state <= kOwnerTransportQueueStatePendingC) {
            count += slot.count;
        }
    }
    return count;
}

u32 CalculateOwnerTransportCarrierDeficit(const UnitCommandContext& context,
    const OwnerTransportQueueState& queue, u32 owner_id, u32 group_id,
    u32 carrier_capacity) {
    const u32 required = CalculateOwnerTransportGroupRequiredCarrierCount(context,
        owner_id, group_id, carrier_capacity);
    const u32 reserved = CountOwnerTransportGroupReservedCarriers(queue, group_id);
    return reserved < required ? required - reserved : 0;
}

u32 CalculateOwnerTransportCarrierDeficitTotal(const UnitCommandContext& context,
    const OwnerTransportQueueState& queue, u32 owner_id, u32 carrier_capacity) {
    u32 deficit = 0;
    for (u32 slot_index = 1; slot_index < queue.slots.size(); ++slot_index) {
        const u32 state = queue.slots[slot_index].state;
        if (state != 3 && state != kOwnerTransportQueueStateStrategicTargetAlt) {
            continue;
        }
        deficit += CalculateOwnerTransportCarrierDeficit(context, queue, owner_id,
            slot_index, carrier_capacity);
    }
    return deficit;
}

u32 FindOwnerTransportLinkedState0eSlot(const OwnerTransportQueueState& queue,
    u32 linked_group) {
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.linked_group == linked_group &&
            slot.state == kOwnerTransportQueueStateLinkedGroup) {
            return static_cast<u32>(index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

u32 FindOwnerTransportLinkedState0aSlot(const OwnerTransportQueueState& queue,
    u32 linked_group) {
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.linked_group == linked_group &&
            slot.state == kOwnerTransportQueueStateLinkedGroup0a) {
            return static_cast<u32>(index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

namespace {

struct OwnerTransportRelayCandidateSelection {
    u32 slot_index = kInvalidOwnerTransportQueueSlot;
    bool second_range_blocked_by_linked_slot = false;
};

template <typename LinkedSlotFinder>
OwnerTransportRelayCandidateSelection select_owner_transport_relay_candidate(
    const OwnerTransportQueueState& queue, LinkedSlotFinder linked_slot_finder) {
    constexpr std::array<std::pair<u32, u32>, 3> kRelayStateRanges{{
        {0x1f, 0x21},
        {0x16, 0x1a},
        {0x1c, 0x1e},
    }};

    OwnerTransportRelayCandidateSelection result;
    for (std::size_t range_index = 0; range_index < kRelayStateRanges.size();
         ++range_index) {
        const auto& range = kRelayStateRanges[range_index];
        for (std::size_t index = 1; index < queue.slots.size(); ++index) {
            const u32 state = queue.slots[index].state;
            if (state < range.first || state > range.second) {
                continue;
            }
            if (linked_slot_finder(queue, static_cast<u32>(index)) ==
                kInvalidOwnerTransportQueueSlot) {
                result.slot_index = static_cast<u32>(index);
                return result;
            }
            if (range_index == 1) {
                result.second_range_blocked_by_linked_slot = true;
            }
        }
    }
    return result;
}

} // namespace

u32 FindOwnerTransportRelayCandidateWithoutLinkedState0a(
    const OwnerTransportQueueState& queue) {
    return select_owner_transport_relay_candidate(queue,
        [](const OwnerTransportQueueState& relay_queue, u32 linked_group) {
            return FindOwnerTransportLinkedState0aSlot(relay_queue, linked_group);
        }).slot_index;
}

u32 FindOwnerTransportRelayCandidateWithoutLinkedState0e(
    const OwnerTransportQueueState& queue) {
    return select_owner_transport_relay_candidate(queue,
        [](const OwnerTransportQueueState& relay_queue, u32 linked_group) {
            return FindOwnerTransportLinkedState0eSlot(relay_queue, linked_group);
        }).slot_index;
}

void ReassignOwnerTransportQueueSlotReferences(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, u32 from_slot, u32 to_slot) {
    if (from_slot >= queue.slots.size() || to_slot >= queue.slots.size()) {
        return;
    }

    if (context.movement != nullptr) {
        for (UnitMovementUnit* unit : context.movement->active_units) {
            if (unit == nullptr || unit->owner_id != owner_id) {
                continue;
            }
            if ((unit->area_marker_flags & 0xffu) == from_slot) {
                unit->area_marker_flags =
                    (unit->area_marker_flags & 0xffffff00u) | to_slot;
            }
        }
    }

    queue.slots[to_slot].count += queue.slots[from_slot].count;
    queue.slots[from_slot].count = 0;
    queue.slots[from_slot].state = 0;
}

void PrepareOwnerTransportQueueSlotAsState0b(OwnerTransportQueueState& queue,
    u32 slot_index, const UnitMovementUnit* fallback_target) {
    if (slot_index >= queue.slots.size()) {
        return;
    }

    OwnerTransportQueueSlot& slot = queue.slots[slot_index];
    slot.state = kOwnerTransportQueueStateFallback0b;
    slot.linked_group = 0;
    slot.completed_count = 0;
    slot.phase_ticks = 0;
    slot.aux_value = 0;
    if (fallback_target != nullptr) {
        slot.match_value = fallback_target->id;
        slot.target_x = fallback_target->x;
        slot.target_y = fallback_target->y;
        slot.route_index = 0;
    }
}

namespace {

bool owner_transport_maintenance_unit_matches(const UnitMovementUnit& unit,
    u32 owner_id) {
    return unit.owner_id == owner_id && unit.type_id < 0x60;
}

void clear_owner_transport_slot_runtime_fields(OwnerTransportQueueSlot& slot,
    bool clear_linked_group) {
    slot.completed_count = 0;
    slot.phase_ticks = 0;
    slot.aux_value = 0;
    slot.match_value = 0;
    if (clear_linked_group) {
        slot.linked_group = 0;
    }
}

void set_owner_transport_slot_point(OwnerTransportQueueSlot& slot,
    UnitMovementPoint point) {
    slot.target_x = point.x;
    slot.target_y = point.y;
}

UnitMovementPoint owner_transport_preferred_target_point(
    const OwnerStrategicTargetState* strategic_target) {
    if (strategic_target == nullptr) {
        return {-1, -1};
    }
    if (strategic_target->has_preferred_target) {
        return strategic_target->preferred_target_point;
    }
    return strategic_target->strategic_point;
}

UnitMovementPoint owner_transport_primary_target_point(
    const OwnerStrategicTargetState* strategic_target) {
    if (strategic_target == nullptr) {
        return {-1, -1};
    }
    return strategic_target->strategic_point;
}

bool refresh_owner_transport_strategic_point(UnitCommandContext& context,
    u32 owner_id, const OwnerTransportQueueMaintenanceInput& input) {
    OwnerStrategicTargetState* strategic_target = input.strategic_target;
    if (strategic_target == nullptr) {
        return false;
    }
    if (input.callbacks.refresh_strategic_point != nullptr) {
        return input.callbacks.refresh_strategic_point(context, owner_id,
            *strategic_target, input.user_data);
    }
    return strategic_target->has_strategic_point;
}

u32 find_owner_transport_fallback0b_slot(const OwnerTransportQueueState& queue) {
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        if (queue.slots[index].state == kOwnerTransportQueueStateFallback0b) {
            return static_cast<u32>(index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

void refresh_owner_transport_slot_assigned_unit_target(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 slot_index, u32 linked_group) {
    if (slot_index >= queue.slots.size()) {
        return;
    }

    UnitMovementUnit* assigned = FindOwnerTransportQueueAssignedUnit(context, queue,
        owner_id, linked_group);
    if (assigned == nullptr) {
        return;
    }

    OwnerTransportQueueSlot& slot = queue.slots[slot_index];
    slot.match_value = assigned->id;
    slot.target_x = assigned->x;
    slot.target_y = assigned->y;
}

void enter_owner_transport_linked_slot(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, u32 slot_index, u32 next_state,
    u32 linked_group) {
    if (slot_index >= queue.slots.size()) {
        return;
    }

    OwnerTransportQueueSlot& slot = queue.slots[slot_index];
    slot.state = next_state;
    slot.linked_group = linked_group;
    clear_owner_transport_slot_runtime_fields(slot, false);
    refresh_owner_transport_slot_assigned_unit_target(context, queue, owner_id,
        slot_index, linked_group);
}

void reset_owner_transport_linked_slot_to_pending(OwnerTransportQueueSlot& slot,
    u32 pending_state, const UnitMovementUnit* fallback_target) {
    slot.state = pending_state;
    clear_owner_transport_slot_runtime_fields(slot, true);
    if (fallback_target != nullptr) {
        slot.target_x = fallback_target->x;
        slot.target_y = fallback_target->y;
    }
}

bool owner_transport_linked_slot_active(const OwnerTransportQueueState& queue,
    const OwnerTransportQueueMaintenanceScratch& scratch, u32 linked_group) {
    return linked_group < queue.slots.size() &&
        queue.slots[linked_group].state != 0 &&
        scratch.active_slot_counts[linked_group] != 0;
}

u32 owner_transport_unit_metadata_flags(
    const UnitMovementUnit& unit,
    const OwnerTransportQueueMaintenanceInput& input) {
    if (input.callbacks.unit_metadata_flags != nullptr) {
        return input.callbacks.unit_metadata_flags(unit, input.user_data);
    }
    return GetUnitCommandMetadataFlags(unit);
}

u32 resolve_owner_transport_carrier_capacity(const UnitCommandContext& context,
    const OwnerTransportQueueState& queue, u32 owner_id, u32 carrier_slot_index,
    u32 passenger_group, const OwnerTransportQueueMaintenanceInput& input,
    UnitMovementUnit* carrier) {
    if (input.callbacks.carrier_capacity != nullptr) {
        const u32 capacity = input.callbacks.carrier_capacity(context, queue,
            owner_id, carrier_slot_index, passenger_group, input.user_data);
        if (capacity != 0) {
            return capacity;
        }
    }
    if (carrier != nullptr) {
        return CalculateUnitTransportCapacity(*carrier, context.production_state);
    }
    return 0;
}

bool owner_transport_has_boardable_passenger_waiting(
    UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 carrier_slot_index, u32 passenger_group,
    const OwnerTransportQueueMaintenanceInput& input) {
    if (context.movement == nullptr || passenger_group >= queue.slots.size()) {
        return false;
    }

    UnitMovementUnit* carrier = FindOwnerTransportQueueAssignedUnit(context, queue,
        owner_id, carrier_slot_index);
    if (carrier == nullptr || (carrier->runtime_flags & 4u) != 0) {
        return false;
    }

    const u32 capacity = resolve_owner_transport_carrier_capacity(context, queue,
        owner_id, carrier_slot_index, passenger_group, input, carrier);
    if (capacity == 0 || carrier->cargo_amount >= capacity) {
        return false;
    }

    for (UnitMovementUnit* passenger : context.movement->active_units) {
        if (passenger == nullptr || passenger == carrier ||
            passenger->owner_id != owner_id) {
            continue;
        }
        if ((passenger->area_marker_flags & 0xffu) != passenger_group) {
            continue;
        }
        if ((passenger->definition.transport_flags & 4u) == 0 ||
            (passenger->runtime_flags & 4u) != 0) {
            continue;
        }
        if ((owner_transport_unit_metadata_flags(*passenger, input) & 8u) != 0) {
            continue;
        }
        return true;
    }
    return false;
}

void promote_owner_transport_carrier_and_group_to_primary_target(
    OwnerTransportQueueState& queue, u32 carrier_slot_index, u32 passenger_group,
    UnitMovementPoint primary_target) {
    if (carrier_slot_index >= queue.slots.size() ||
        passenger_group >= queue.slots.size()) {
        return;
    }

    OwnerTransportQueueSlot& carrier_slot = queue.slots[carrier_slot_index];
    carrier_slot.state = kOwnerTransportQueueStatePendingC;
    carrier_slot.phase_ticks = 0;
    set_owner_transport_slot_point(carrier_slot, primary_target);

    OwnerTransportQueueSlot& passenger_slot = queue.slots[passenger_group];
    passenger_slot.state = kOwnerTransportQueueStateStrategicTarget;
    passenger_slot.phase_ticks = 0;
    set_owner_transport_slot_point(passenger_slot, primary_target);
}

} // namespace

u32 SelectOwnerTransportState0aRelaySlotOrPrepareState0b(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 source_slot, const UnitMovementUnit* fallback_target) {
    const OwnerTransportRelayCandidateSelection selection =
        select_owner_transport_relay_candidate(queue,
            [](const OwnerTransportQueueState& relay_queue, u32 linked_group) {
                return FindOwnerTransportLinkedState0aSlot(relay_queue,
                    linked_group);
            });
    if (selection.slot_index != kInvalidOwnerTransportQueueSlot) {
        return selection.slot_index;
    }

    const u32 fallback_slot = find_owner_transport_fallback0b_slot(queue);
    if (fallback_slot != kInvalidOwnerTransportQueueSlot) {
        if (selection.second_range_blocked_by_linked_slot) {
            ReassignOwnerTransportQueueSlotReferences(context, queue, owner_id,
                source_slot, 0);
        }
        return kInvalidOwnerTransportQueueSlot;
    }

    PrepareOwnerTransportQueueSlotAsState0b(queue, source_slot, fallback_target);
    return kInvalidOwnerTransportQueueSlot;
}

u32 SelectOwnerTransportState0eRelaySlotOrMerge(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 source_slot) {
    const OwnerTransportRelayCandidateSelection selection =
        select_owner_transport_relay_candidate(queue,
            [](const OwnerTransportQueueState& relay_queue, u32 linked_group) {
                return FindOwnerTransportLinkedState0eSlot(relay_queue,
                    linked_group);
            });
    if (selection.slot_index != kInvalidOwnerTransportQueueSlot) {
        return selection.slot_index;
    }

    if (selection.second_range_blocked_by_linked_slot) {
        ReassignOwnerTransportQueueSlotReferences(context, queue, owner_id,
            source_slot, 0);
    }
    return kInvalidOwnerTransportQueueSlot;
}

void ResetOwnerTransportQueueMaintenanceScratch(OwnerTransportQueueState& queue,
    OwnerTransportQueueMaintenanceScratch& scratch) {
    scratch = OwnerTransportQueueMaintenanceScratch{};
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state == 0) {
            continue;
        }
        slot.completed_count = 0;
        if (slot.state == kOwnerTransportQueueStateThreatResponse) {
            slot.aux_value = 0;
        }
    }
}

void RebuildOwnerTransportQueueActiveSlotCounts(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceCallbacks& callbacks,
    void* user_data, OwnerTransportQueueMaintenanceScratch& scratch) {
    if (context.movement == nullptr) {
        for (OwnerTransportQueueSlot& slot : queue.slots) {
            slot.count = 0;
        }
        return;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr ||
            !owner_transport_maintenance_unit_matches(*unit, owner_id)) {
            continue;
        }

        if ((unit->area_marker_flags & 0xffu) == 0 &&
            callbacks.assign_queue_slot != nullptr) {
            callbacks.assign_queue_slot(context, queue, owner_id, *unit, user_data);
            ++scratch.assigned_unit_count;
        }

        const u32 slot_index = unit->area_marker_flags & 0xffu;
        if (slot_index != 0 && slot_index < scratch.active_slot_counts.size()) {
            ++scratch.active_slot_counts[slot_index];
        }
    }

    for (std::size_t index = 0; index < queue.slots.size(); ++index) {
        queue.slots[index].count = scratch.active_slot_counts[index];
    }
}

void DispatchOwnerTransportQueueUnitTicks(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceCallbacks& callbacks,
    void* user_data, OwnerTransportQueueMaintenanceScratch& scratch) {
    if (context.movement == nullptr || callbacks.tick_unit == nullptr) {
        return;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr ||
            !owner_transport_maintenance_unit_matches(*unit, owner_id)) {
            continue;
        }

        callbacks.tick_unit(context, queue, owner_id, *unit, user_data);
        ++scratch.ticked_unit_count;
    }
}

OwnerTransportQueueMaintenanceScratch TickOwnerTransportQueueMaintenance(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    const OwnerTransportQueueMaintenanceInput& input) {
    OwnerTransportQueueMaintenanceScratch scratch;
    ResetOwnerTransportQueueMaintenanceScratch(queue, scratch);
    RebuildOwnerTransportQueueActiveSlotCounts(context, queue, owner_id,
        input.callbacks, input.user_data, scratch);
    DispatchOwnerTransportQueueUnitTicks(context, queue, owner_id,
        input.callbacks, input.user_data, scratch);
    for (std::size_t index = 0; index < queue.slots.size(); ++index) {
        scratch.active_slot_counts[index] = queue.slots[index].count;
    }

    scratch.owner_phase_state = 1;
    const UnitMovementUnit* fallback_target = nullptr;
    if (input.route_state != nullptr && input.route_state->route_count != 0 &&
        !input.route_state->targets.empty()) {
        fallback_target = input.route_state->targets[0].unit;
    }

    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state != 0 && scratch.active_slot_counts[index] == 0) {
            slot.state = 0;
            slot.count = 0;
            ++scratch.cleared_slot_count;
            continue;
        }
        if (slot.state == 0) {
            continue;
        }

        switch (slot.state) {
        case kOwnerTransportQueueStateWorkTarget:
            if (input.route_state != nullptr &&
                slot.route_index < input.route_state->targets.size()) {
                const OwnerTransportRouteTarget& target =
                    input.route_state->targets[slot.route_index];
                slot.target_x = target.target.target_x;
                slot.target_y = target.target.target_y;
            }
            break;

        case kOwnerTransportQueueStateRelay0aPending: {
            const u32 relay_slot =
                SelectOwnerTransportState0aRelaySlotOrPrepareState0b(context,
                    queue, owner_id, static_cast<u32>(index), fallback_target);
            if (relay_slot != kInvalidOwnerTransportQueueSlot) {
                enter_owner_transport_linked_slot(context, queue, owner_id,
                    static_cast<u32>(index),
                    kOwnerTransportQueueStateLinkedGroup0a, relay_slot);
            }
            break;
        }

        case kOwnerTransportQueueStateLinkedGroup0a:
            if (!owner_transport_linked_slot_active(queue, scratch,
                    slot.linked_group)) {
                reset_owner_transport_linked_slot_to_pending(slot,
                    kOwnerTransportQueueStateRelay0aPending, fallback_target);
            }
            else {
                refresh_owner_transport_slot_assigned_unit_target(context, queue,
                    owner_id, static_cast<u32>(index), slot.linked_group);
            }
            break;

        case kOwnerTransportQueueStateRelay0ePending: {
            const u32 relay_slot = SelectOwnerTransportState0eRelaySlotOrMerge(
                context, queue, owner_id, static_cast<u32>(index));
            if (relay_slot != kInvalidOwnerTransportQueueSlot) {
                enter_owner_transport_linked_slot(context, queue, owner_id,
                    static_cast<u32>(index),
                    kOwnerTransportQueueStateLinkedGroup, relay_slot);
            }
            break;
        }

        case kOwnerTransportQueueStateLinkedGroup:
            if (!owner_transport_linked_slot_active(queue, scratch,
                    slot.linked_group)) {
                reset_owner_transport_linked_slot_to_pending(slot,
                    kOwnerTransportQueueStateRelay0ePending, fallback_target);
            }
            else {
                refresh_owner_transport_slot_assigned_unit_target(context, queue,
                    owner_id, static_cast<u32>(index), slot.linked_group);
            }
            break;

        case kOwnerTransportQueueStatePendingA:
            if (input.carrier_population_gate_open) {
                slot.state = kOwnerTransportQueueStatePendingB;
                break;
            }
            else {
                UnitMovementUnit* carrier = FindOwnerTransportQueueAssignedUnit(
                    context, queue, owner_id, static_cast<u32>(index));
                const u32 capacity = resolve_owner_transport_carrier_capacity(
                    context, queue, owner_id, static_cast<u32>(index),
                    slot.linked_group, input, carrier);
                const u32 required =
                    CalculateOwnerTransportGroupRequiredCarrierCount(context,
                        owner_id, slot.linked_group, capacity);
                if (required <= scratch.active_slot_counts[index]) {
                    slot.state = kOwnerTransportQueueStatePendingB;
                }
            }
            break;

        case kOwnerTransportQueueStatePendingB:
            if (owner_transport_has_boardable_passenger_waiting(context, queue,
                    owner_id, static_cast<u32>(index), slot.linked_group,
                    input)) {
                break;
            }
            if (refresh_owner_transport_strategic_point(context, owner_id, input)) {
                promote_owner_transport_carrier_and_group_to_primary_target(queue,
                    static_cast<u32>(index), slot.linked_group,
                    owner_transport_primary_target_point(input.strategic_target));
            }
            break;

        case kOwnerTransportQueueStateStrategicTarget:
        case 0x17:
            if (scratch.active_slot_counts[index] <= slot.completed_count) {
                slot.state = kOwnerTransportQueueStateStrategicTargetHold;
                slot.phase_ticks = 0;
                set_owner_transport_slot_point(slot,
                    owner_transport_preferred_target_point(input.strategic_target));
            }
            scratch.owner_phase_state = kOwnerTransportQueueStateStrategicTarget;
            ++slot.phase_ticks;
            if (slot.phase_ticks > 8) {
                if (!refresh_owner_transport_strategic_point(context, owner_id,
                        input)) {
                    slot.state = 6;
                    slot.phase_ticks = 0;
                }
                else {
                    slot.state = kOwnerTransportQueueStateStrategicTargetHold;
                    slot.phase_ticks = 0;
                    set_owner_transport_slot_point(slot,
                        owner_transport_preferred_target_point(
                            input.strategic_target));
                }
            }
            break;

        case kOwnerTransportQueueStateStrategicTargetHold:
        case 0x19:
        case 0x1a:
            if (scratch.active_slot_counts[index] <= slot.completed_count ||
                slot.phase_ticks > 5) {
                if (!refresh_owner_transport_strategic_point(context, owner_id,
                        input)) {
                    scratch.aborted_for_missing_strategic_point = true;
                    return scratch;
                }
                slot.state = kOwnerTransportQueueStateStrategicTarget;
                slot.phase_ticks = 0;
                set_owner_transport_slot_point(slot,
                    owner_transport_primary_target_point(input.strategic_target));
            }
            break;

        case kOwnerTransportQueueStateStrategicTargetAlt:
            scratch.owner_phase_state = kOwnerTransportQueueStateStrategicTarget;
            break;

        case kOwnerTransportQueueStateThreatResponse:
        case 0x20:
        case 0x21:
            if (scratch.active_slot_counts[index] <= slot.completed_count ||
                slot.phase_ticks > 5) {
                slot.state = 6;
                slot.phase_ticks = 0;
            }
            break;

        default:
            break;
        }
    }

    return scratch;
}

OwnerTransportQueueLoadSummary CalculateOwnerTransportQueueLoadSummary(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, OwnerTransportQueueUnitWeightCallback weight_callback) {
    OwnerTransportQueueLoadSummary summary;
    if (context.movement == nullptr) {
        return summary;
    }

    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id || unit->type_id >= 0x60) {
            continue;
        }

        const u32 slot_index = unit->area_marker_flags & 0xffu;
        if (slot_index >= queue.slots.size()) {
            continue;
        }

        const u32 state = queue.slots[slot_index].state;
        const u32 weight = weight_callback != nullptr ?
            weight_callback(*unit) :
            CalculateOwnerTransportUnitWeight(*unit);
        switch (state) {
        case 2:
        case 3:
            ++summary.state02_03_unit_count;
            break;
        case 6:
        case 7:
            summary.state06_07_weight += weight;
            break;
        case 8:
            summary.state08_weight += weight;
            break;
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
            summary.state16_1b_weight += weight;
            break;
        case 0x1c:
        case 0x1d:
        case 0x1e:
            summary.state1c_1e_weight += weight;
            break;
        case 0x1f:
        case 0x20:
        case 0x21:
            summary.state1f_21_weight += weight;
            break;
        default:
            break;
        }
    }

    return summary;
}

u32 CalculateOwnerTransportUnitWeight(const UnitMovementUnit& unit) {
    return unit.health + unit.runtime_stat_1c + unit.runtime_stat_20;
}

namespace {

u32 owner_transport_assignment_unit_weight(const UnitMovementUnit& unit) {
    return CalculateOwnerTransportUnitWeight(unit);
}

bool owner_transport_assignment_point_valid(UnitMovementPoint point) {
    return point.x != -1 && point.y != -1;
}

OwnerTransportQueueTargetSnapshot owner_transport_primary_route_snapshot(
    const OwnerTransportQueueAssignmentInput& input,
    const UnitMovementUnit& fallback_unit) {
    OwnerTransportQueueTargetSnapshot snapshot;
    snapshot.match_value = 0;
    snapshot.target_x = fallback_unit.anchor_x;
    snapshot.target_y = fallback_unit.anchor_y;
    snapshot.route_index = 0;

    if (input.route_state == nullptr || input.route_state->route_count == 0 ||
        input.route_state->targets.empty()) {
        return snapshot;
    }

    const OwnerTransportRouteTarget& target = input.route_state->targets[0];
    if (target.unit != nullptr) {
        snapshot.target_x = target.unit->x;
        snapshot.target_y = target.unit->y;
    }
    return snapshot;
}

UnitMovementPoint owner_transport_assignment_strategic_point(
    const OwnerTransportQueueAssignmentInput& input,
    const UnitMovementUnit& fallback_unit) {
    if (input.strategic_queue_target_enabled &&
        owner_transport_assignment_point_valid(input.strategic_queue_target_point)) {
        return input.strategic_queue_target_point;
    }
    if (input.strategic_target != nullptr &&
        input.strategic_target->has_strategic_point) {
        return input.strategic_target->strategic_point;
    }
    return {fallback_unit.x, fallback_unit.y};
}

u32 owner_transport_route_index_for_unit(
    const OwnerTransportQueueAssignmentInput& input,
    const UnitMovementUnit* unit) {
    if (unit == nullptr || input.route_state == nullptr) {
        return kInvalidOwnerTransportQueueSlot;
    }

    const u32 route_count = std::min<u32>(input.route_state->route_count,
        static_cast<u32>(input.route_state->targets.size()));
    for (u32 index = 0; index < route_count; ++index) {
        const OwnerTransportRouteTarget& target = input.route_state->targets[index];
        if (target.unit == unit) {
            return index;
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

OwnerTransportQueueTargetSnapshot owner_transport_dropoff_work_snapshot(
    const OwnerTransportQueueAssignmentInput& input,
    const UnitMovementUnit* dropoff) {
    OwnerTransportQueueTargetSnapshot snapshot;
    if (dropoff == nullptr) {
        snapshot.match_value = 0;
        return snapshot;
    }

    snapshot.match_value = dropoff->id;
    const u32 route_index = owner_transport_route_index_for_unit(input, dropoff);
    if (route_index != kInvalidOwnerTransportQueueSlot &&
        input.route_state != nullptr) {
        snapshot = input.route_state->targets[route_index].target;
        snapshot.route_index = route_index;
        snapshot.match_value = dropoff->id;
        return snapshot;
    }

    return snapshot;
}

u32 owner_transport_find_or_allocate_plain_state(
    OwnerTransportQueueState& queue, u32 state) {
    OwnerTransportQueueTargetSnapshot target;
    return FindOrAllocateOwnerTransportQueueSlotByStateAndValue(queue, state,
        target);
}

u32 owner_transport_find_or_allocate_target_state(
    OwnerTransportQueueState& queue, u32 state,
    const OwnerTransportQueueTargetSnapshot& target) {
    return FindOrAllocateOwnerTransportQueueSlotByStateAndValue(queue, state,
        target);
}

u32 owner_transport_assign_general_queue_slot(
    const UnitCommandContext& context, OwnerTransportQueueState& queue,
    u32 owner_id, UnitMovementUnit& unit,
    const OwnerTransportQueueAssignmentInput& input) {
    const OwnerTransportQueueUnitWeightCallback weight =
        input.unit_weight != nullptr ? input.unit_weight :
        owner_transport_assignment_unit_weight;
    const OwnerTransportQueueLoadSummary summary =
        CalculateOwnerTransportQueueLoadSummary(context, queue, owner_id, weight);
    const u32 strategic_ratio =
        (summary.state08_weight * 100u) / (summary.state06_07_weight + 1u);

    const bool use_strategic_queue =
        input.strategic_queue_target_enabled &&
        (unit.definition.support_target_flags & 2u) != 0 &&
        strategic_ratio < input.strategic_queue_load_percent;
    if (use_strategic_queue) {
        OwnerTransportQueueTargetSnapshot target;
        const UnitMovementPoint point =
            owner_transport_assignment_strategic_point(input, unit);
        target.target_x = point.x;
        target.target_y = point.y;
        return owner_transport_find_or_allocate_target_state(queue, 8, target);
    }

    u32 slot_index = FindOwnerTransportQueueSlotByStateAndValue(queue, 6, 0);
    if (slot_index != kInvalidOwnerTransportQueueSlot) {
        return slot_index;
    }
    slot_index = FindOwnerTransportQueueSlotByStateAndValue(queue, 7, 0);
    if (slot_index != kInvalidOwnerTransportQueueSlot) {
        return slot_index;
    }
    return AllocateOwnerTransportQueueSlot(queue, 6);
}

u32 owner_transport_assign_worker_queue_slot(
    UnitCommandContext& context, OwnerTransportQueueState& queue,
    UnitMovementUnit& unit, const OwnerTransportQueueAssignmentInput& input) {
    UnitMovementUnit* dropoff = context.movement != nullptr ?
        FindNearestOwnedDropoffBuilding(*context.movement, unit).unit : nullptr;
    OwnerTransportQueueTargetSnapshot target =
        owner_transport_dropoff_work_snapshot(input, dropoff);

    u32 slot_index = owner_transport_find_or_allocate_target_state(queue,
        kOwnerTransportQueueStateWorkTarget, target);
    if (slot_index == kInvalidOwnerTransportQueueSlot && target.match_value != 0) {
        target = OwnerTransportQueueTargetSnapshot{};
        slot_index = owner_transport_find_or_allocate_target_state(queue,
            kOwnerTransportQueueStateWorkTarget, target);
    }
    return slot_index;
}

u32 owner_transport_assign_targeted_state(
    OwnerTransportQueueState& queue, UnitMovementUnit& unit,
    const OwnerTransportQueueAssignmentInput& input, u32 state) {
    const OwnerTransportQueueTargetSnapshot target =
        owner_transport_primary_route_snapshot(input, unit);
    return owner_transport_find_or_allocate_target_state(queue, state, target);
}

u32 owner_transport_assign_dropoff_slot(
    OwnerTransportQueueState& queue, UnitMovementUnit& unit,
    const OwnerTransportQueueAssignmentInput& input) {
    return owner_transport_assign_targeted_state(queue, unit, input,
        kOwnerTransportQueueStateDropoff);
}

u32 owner_transport_assign_carrier_queue_slot(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementUnit& unit, const OwnerTransportQueueAssignmentInput& input,
    u32& overflow_moved_count) {
    u32 carrier_capacity = input.carrier_capacity;
    if (carrier_capacity == 0) {
        carrier_capacity = CalculateUnitTransportCapacity(unit,
            context.production_state);
    }

    u32 slot_index = FindOwnerTransportQueueSlotNeedingCarrier(context, queue,
        owner_id, carrier_capacity);
    if (slot_index != kInvalidOwnerTransportQueueSlot) {
        return slot_index;
    }

    const u32 passenger_group =
        FindOwnerTransportPassengerGroupWithoutCarrierReservation(queue);
    if (passenger_group == kInvalidOwnerTransportQueueSlot) {
        return owner_transport_assign_dropoff_slot(queue, unit, input);
    }

    slot_index = FindOwnerTransportQueueSlotByStateAndValue(queue,
        kOwnerTransportQueueStateDropoff, 0);
    if (slot_index == kInvalidOwnerTransportQueueSlot) {
        slot_index = AllocateOwnerTransportQueueSlot(queue,
            kOwnerTransportQueueStatePendingA);
        if (slot_index != kInvalidOwnerTransportQueueSlot) {
            queue.slots[slot_index].match_value = 0;
            queue.slots[slot_index].linked_group = passenger_group;
        }
        return slot_index;
    }

    OwnerTransportQueueSlot& reservation = queue.slots[slot_index];
    reservation.state = kOwnerTransportQueueStatePendingA;
    reservation.linked_group = passenger_group;
    const u32 required = CalculateOwnerTransportGroupRequiredCarrierCount(context,
        owner_id, passenger_group, carrier_capacity);
    if (required < reservation.count) {
        const u32 dropoff_slot = owner_transport_assign_dropoff_slot(queue, unit,
            input);
        if (dropoff_slot != kInvalidOwnerTransportQueueSlot) {
            overflow_moved_count =
                ReassignOwnerTransportUnitsBetweenQueueSlots(context, queue,
                    owner_id, slot_index, dropoff_slot,
                    reservation.count - required);
            slot_index = dropoff_slot;
        }
    }
    return slot_index;
}

bool owner_transport_primary_combat_shortfall_redirect(
    const UnitMovementUnit& unit, const OwnerTransportQueueAssignmentInput& input,
    u32 carrier_unit_type, u32 primary_combat_unit_type) {
    if (unit.type_id != carrier_unit_type || input.target_owner_counts == nullptr ||
        input.owner_unit_counts == nullptr ||
        primary_combat_unit_type >= input.owner_unit_counts->counts.size()) {
        return false;
    }

    const u32 desired =
        CalculateOwnerPrimaryUnitDesiredCountFromTargetOwner(
            *input.target_owner_counts);
    return desired < input.owner_unit_counts->counts[primary_combat_unit_type];
}

} // namespace

OwnerTransportQueueAssignmentResult AssignOwnerTransportQueueSlotForUnit(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementUnit& unit, const OwnerTransportQueueAssignmentInput& input) {
    OwnerTransportQueueAssignmentResult result;
    if (unit.owner_id != owner_id || unit.type_id >= 0x60) {
        return result;
    }

    u32 slot_index = kInvalidOwnerTransportQueueSlot;
    if (input.script_trigger_gate_open &&
        input.unit_in_active_script_trigger_group) {
        slot_index = owner_transport_find_or_allocate_plain_state(queue, 1);
        result.used_script_trigger_group =
            slot_index != kInvalidOwnerTransportQueueSlot;
    }
    else {
        const u32 faction = input.owner_faction;
        const u32 carrier_unit_type =
            faction < input.faction_carrier_unit_types.size()
                ? input.faction_carrier_unit_types[faction]
                : kInvalidOwnerTransportQueueSlot;
        const u32 primary_combat_unit_type =
            faction < input.faction_primary_combat_unit_types.size()
                ? input.faction_primary_combat_unit_types[faction]
                : kInvalidOwnerTransportQueueSlot;
        const u32 source_flags = unit.definition.support_source_flags;
        const bool primary_combat_redirect =
            (source_flags & 1u) == 0 || unit.type_id != primary_combat_unit_type ||
            owner_transport_primary_combat_shortfall_redirect(unit, input,
                carrier_unit_type, primary_combat_unit_type);

        if (primary_combat_redirect) {
            if ((source_flags & 0x10u) == 0 && (source_flags & 4u) == 0) {
                if (unit.type_id == carrier_unit_type) {
                    slot_index = owner_transport_assign_carrier_queue_slot(context,
                        queue, owner_id, unit, input,
                        result.overflow_moved_count);
                }
                else if ((unit.type_flags & 0x40u) == 0) {
                    slot_index = owner_transport_assign_general_queue_slot(context,
                        queue, owner_id, unit, input);
                }
                else {
                    slot_index = owner_transport_assign_worker_queue_slot(context,
                        queue, unit, input);
                }
            }
            else {
                slot_index = owner_transport_assign_targeted_state(queue, unit,
                    input, kOwnerTransportQueueStateRelay0ePending);
            }
        }
        else {
            slot_index = owner_transport_assign_targeted_state(queue, unit, input,
                kOwnerTransportQueueStateRelay0aPending);
        }
    }

    if (slot_index == kInvalidOwnerTransportQueueSlot) {
        return result;
    }

    AssignUnitToOwnerTransportQueueSlot(queue, unit, slot_index);
    result.slot_index = slot_index;
    result.queue_state = queue.slots[slot_index].state;
    result.assigned = true;
    return result;
}

OwnerUnitCountAndWeightSummary CalculateOwnerUnitCountAndWeightSummary(
    const UnitCommandContext& context, u32 owner_id,
    OwnerUnitEligibilityCallback eligibility_callback,
    OwnerTransportQueueUnitWeightCallback weight_callback) {
    OwnerUnitCountAndWeightSummary summary;
    if (context.movement == nullptr) {
        return summary;
    }

    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id) {
            continue;
        }
        const bool eligible = eligibility_callback != nullptr ?
            eligibility_callback(*unit) : CheckOwnerEligibleRetargetUnit(*unit);
        if (!eligible) {
            continue;
        }

        ++summary.count;
        if (weight_callback != nullptr) {
            summary.weight += weight_callback(*unit);
        }
    }
    return summary;
}

bool CheckOwnerEligibleRetargetUnit(const UnitMovementUnit& unit) {
    if (unit.definition.lifecycle_class != 0) {
        return false;
    }
    if ((unit.definition.type_flags & 0x40u) != 0) {
        return false;
    }
    return (unit.definition.type_flags & 0x20u) != 0 ||
        (unit.definition.initial_script_bit_flags & 0x7242u) != 0;
}

bool CheckOwnerEligibleRetargetUnitWithScriptGate(
    const UnitMovementUnit& unit, bool owner_script_trigger_gate_open,
    bool unit_in_active_script_trigger_group) {
    if (!CheckOwnerEligibleRetargetUnit(unit)) {
        return false;
    }
    return !owner_script_trigger_gate_open ||
        !unit_in_active_script_trigger_group;
}

bool CheckOwnerCanTargetOwner(const OwnerStrategicTargetState& state,
    u32 owner_id) {
    if (owner_id >= 32) {
        return false;
    }
    return (state.blocked_owner_mask & (1u << owner_id)) == 0;
}

UnitMovementUnit* FindOwnerRouteOrBuildingTargetForCurrentTargetOwner(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    OwnerStrategicUnitPredicate route_target_predicate,
    OwnerStrategicUnitPredicate building_target_predicate) {
    if (context.movement == nullptr ||
        !CheckOwnerCanTargetOwner(state, state.target_owner_id)) {
        return nullptr;
    }

    UnitMovementUnit* first_building = nullptr;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != state.target_owner_id) {
            continue;
        }
        if (route_target_predicate != nullptr && route_target_predicate(*unit)) {
            return unit;
        }
        if (first_building == nullptr &&
            building_target_predicate != nullptr && building_target_predicate(*unit)) {
            first_building = unit;
        }
    }
    return first_building;
}

UnitMovementUnit* FindOwnerFallbackTargetForCurrentTargetOwner(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    OwnerStrategicUnitPredicate fallback_predicate) {
    if (context.movement == nullptr ||
        !CheckOwnerCanTargetOwner(state, state.target_owner_id)) {
        return nullptr;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != state.target_owner_id) {
            continue;
        }
        if (fallback_predicate == nullptr || fallback_predicate(*unit)) {
            return unit;
        }
    }
    return nullptr;
}

bool SelectNearestAttackableOwnerForStrategicTarget(
    const UnitCommandContext& context, const OwnerStrategicTargetState& state,
    UnitMovementPoint reference_point, u32& selected_owner) {
    selected_owner = kInvalidOwnerTransportQueueSlot;
    if (context.movement == nullptr) {
        return false;
    }

    u32 best_score = 1000000;
    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id >= 8 ||
            !CheckOwnerCanTargetOwner(state, unit->owner_id)) {
            continue;
        }

        u32 bias = 100000;
        if (CheckOwnerTransportRouteTargetUnitType(unit->type_id)) {
            bias = 0;
        }
        else if (unit->type_id < 0x60) {
            bias = 200000;
        }

        const u32 distance = CalculateApproxUnitDistance(unit->x, unit->y,
            reference_point.x, reference_point.y);
        const u32 score = bias + distance;
        if (score < best_score) {
            selected_owner = unit->owner_id;
            best_score = score;
        }
    }
    return selected_owner != kInvalidOwnerTransportQueueSlot;
}

void SetOwnerStrategicPointFromUnit(OwnerStrategicTargetState& state,
    const UnitMovementUnit& target) {
    state.preferred_target = const_cast<UnitMovementUnit*>(&target);
    state.preferred_target_point = {target.x, target.y};
    state.strategic_point = state.preferred_target_point;
    state.has_preferred_target = true;
    state.has_strategic_point = true;
}

bool CheckOwnerStrategicPathWindowTileOpen(const UnitMovementCell& cell) {
    return (cell.flags & 0x20000000u) != 0;
}

u32 CalculateOwnerStrategicPathWindowOpenScore(const UnitMovementMap& map,
    UnitMovementPoint start_tile, u32 max_score,
    OwnerStrategicTilePredicate open_predicate) {
    const OwnerStrategicTilePredicate predicate = open_predicate != nullptr
        ? open_predicate
        : CheckOwnerStrategicPathWindowTileOpen;

    u32 score = 0;
    u32 side_length = 0;
    UnitMovementPoint tile = start_tile;
    auto is_open = [&](UnitMovementPoint candidate) {
        if (candidate.x < 0 || candidate.y < 0 ||
            static_cast<u32>(candidate.x) >= map.width ||
            static_cast<u32>(candidate.y) >= map.height) {
            return false;
        }
        const UnitMovementCell* cell = GetMovementCell(map,
            static_cast<u32>(candidate.x), static_cast<u32>(candidate.y));
        return cell != nullptr && predicate(*cell);
    };

    while (score < max_score) {
        for (u32 i = 0; i < side_length + 1; ++i) {
            ++tile.x;
            if (!is_open(tile)) {
                return score;
            }
        }
        for (u32 i = 0; i < side_length + 1; ++i) {
            ++tile.y;
            if (!is_open(tile)) {
                return score + 1;
            }
        }
        side_length += 2;
        for (u32 i = 0; i < side_length; ++i) {
            --tile.x;
            if (!is_open(tile)) {
                return score + 2;
            }
        }
        for (u32 i = 0; i < side_length; ++i) {
            --tile.y;
            if (!is_open(tile)) {
                return score + 3;
            }
        }
        score += 4;
    }
    return score;
}

OwnerPathWindowSelection SelectOwnerBestOpenPathWindowPoint(
    const UnitMovementMap& map, const std::vector<UnitMovementPoint>& path_tiles,
    u32 progress_percent, u32 window_count, u32 max_score,
    OwnerStrategicTilePredicate open_predicate) {
    OwnerPathWindowSelection selection;
    if (path_tiles.empty() || window_count == 0) {
        return selection;
    }

    const u32 scaled_index = static_cast<u32>(
        (static_cast<unsigned long long>(path_tiles.size()) * progress_percent) / 100);
    const u32 start_index = scaled_index > window_count ? scaled_index - window_count : 0;
    const u32 end_index = std::min<u32>(start_index + window_count,
        static_cast<u32>(path_tiles.size()));
    for (u32 index = start_index; index < end_index; ++index) {
        const UnitMovementPoint tile = path_tiles[index];
        const u32 score = CalculateOwnerStrategicPathWindowOpenScore(map, tile,
            max_score, open_predicate);
        if (selection.score < score) {
            selection.tile = tile;
            selection.world_point = {tile.x << 5, tile.y << 5};
            selection.score = score;
            selection.has_point = true;
        }
    }
    return selection;
}

bool CheckUnitRecordsOwnerThreatPoint(const UnitMovementUnit& unit) {
    return (unit.type_flags & 0x40u) != 0 || unit.type_id > 0x5f;
}

void RecordOwnerThreatPointForUnit(OwnerStrategicPointList& point_list,
    const UnitMovementUnit& threat_unit, u32 merge_distance) {
    const UnitMovementPoint point{threat_unit.x, threat_unit.y};
    for (std::size_t index = 0; index < point_list.points.size(); ++index) {
        UnitMovementPoint& existing = point_list.points[index];
        if (existing.x == -1) {
            continue;
        }
        const u32 distance = CalculateApproxUnitDistance(existing.x, existing.y,
            point.x, point.y);
        if (distance < merge_distance) {
            existing = point;
            return;
        }
    }

    for (std::size_t index = 0; index < point_list.points.size(); ++index) {
        UnitMovementPoint& existing = point_list.points[index];
        if (existing.x == -1) {
            existing = point;
            return;
        }
    }
}

void RecordOwnerThreatPointIfStrategicTarget(OwnerStrategicPointList& point_list,
    const UnitMovementUnit& strategic_target_unit,
    const UnitMovementUnit& threat_unit, u32 merge_distance) {
    if (!CheckUnitRecordsOwnerThreatPoint(strategic_target_unit)) {
        return;
    }
    RecordOwnerThreatPointForUnit(point_list, threat_unit, merge_distance);
}

u32 ProcessOwnerThreatPointList(OwnerStrategicPointList& point_list,
    u32 owner_id, OwnerThreatPointHandler handler, void* user_data) {
    u32 processed_count = 0;
    for (UnitMovementPoint& point : point_list.points) {
        if (point.x == -1) {
            continue;
        }
        if (handler != nullptr) {
            handler(owner_id, point, user_data);
        }
        ++processed_count;
    }
    return processed_count;
}

u32 FindOwnerThreatResponseQueueSlotNearPoint(
    const OwnerTransportQueueState& queue, UnitMovementPoint point,
    u32 max_distance) {
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state != kOwnerTransportQueueStateThreatResponse) {
            continue;
        }
        const u32 distance = CalculateApproxUnitDistance(point.x, point.y,
            slot.target_x, slot.target_y);
        if (distance <= max_distance) {
            return static_cast<u32>(index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

u32 PrepareOwnerThreatResponseQueueSlot(OwnerTransportQueueState& queue,
    UnitMovementPoint point) {
    const u32 slot_index = AllocateOwnerTransportQueueSlot(queue,
        kOwnerTransportQueueStateThreatResponse);
    if (slot_index == kInvalidOwnerTransportQueueSlot) {
        return kInvalidOwnerTransportQueueSlot;
    }

    OwnerTransportQueueSlot& slot = queue.slots[slot_index];
    slot.count = 0;
    slot.target_x = point.x;
    slot.target_y = point.y;
    slot.aux_value = 0;
    return slot_index;
}

void ClearOwnerThreatPointAndQueueSlot(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, UnitMovementPoint& point,
    u32 slot_index) {
    point.x = -1;
    if (slot_index >= queue.slots.size()) {
        return;
    }

    queue.slots[slot_index].state = 0;
    queue.slots[slot_index].count = 0;
    if (context.movement == nullptr) {
        return;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id) {
            continue;
        }
        if ((unit->area_marker_flags & 0xffu) == slot_index) {
            unit->area_marker_flags &= 0xffffff00u;
        }
    }
}

OwnerThreatPointPressureSummary CalculateOwnerThreatPointPressureSummary(
    const UnitCommandContext& context, UnitMovementPoint point,
    OwnerUnitEligibilityCallback eligibility_callback,
    OwnerTransportQueueUnitWeightCallback weight_callback,
    u32 max_distance) {
    OwnerThreatPointPressureSummary summary;
    if (context.movement == nullptr) {
        return summary;
    }

    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr) {
            continue;
        }
        if (eligibility_callback != nullptr && !eligibility_callback(*unit)) {
            continue;
        }
        const u32 distance = CalculateApproxUnitDistance(point.x, point.y,
            unit->x, unit->y);
        if (distance > max_distance) {
            continue;
        }

        ++summary.count;
        if (weight_callback != nullptr) {
            summary.weight += weight_callback(*unit);
        }
    }
    return summary;
}

namespace {

bool owner_threat_primary_response_source_state(u32 state) {
    return state == 6 || state == 7 || state == 8;
}

bool owner_threat_unit_active(const UnitMovementUnit& unit) {
    return unit.active;
}

u32 owner_threat_unit_weight(const UnitMovementUnit& unit,
    OwnerTransportQueueUnitWeightCallback weight_callback) {
    if (weight_callback == nullptr) {
        return 1;
    }
    return std::max<u32>(1, weight_callback(unit));
}

bool owner_threat_unit_hostile_to_owner(const UnitCommandContext& context,
    u32 owner_id, const UnitMovementUnit& unit) {
    if (unit.owner_id == owner_id) {
        return false;
    }
    if (owner_id >= context.owner_relation_masks.size() || unit.owner_id >= 32) {
        return true;
    }
    const u32 unit_bit = 1u << unit.owner_id;
    return (context.owner_relation_masks[owner_id] & unit_bit) == 0;
}

bool owner_threat_pressure_candidate_for_owner(const UnitCommandContext& context,
    u32 owner_id, const UnitMovementUnit& unit) {
    if (!owner_threat_unit_hostile_to_owner(context, owner_id, unit)) {
        return false;
    }
    return (unit.type_flags & 0x20u) != 0 ||
        (unit.owner_id < 8 && unit.type_id < 0x60);
}

bool owner_threat_related_response_owner(const UnitCommandContext& context,
    u32 response_owner, u32 threatened_owner) {
    if (response_owner == threatened_owner) {
        return false;
    }
    if (response_owner >= context.owner_relation_masks.size() ||
        threatened_owner >= 32) {
        return false;
    }
    return (context.owner_relation_masks[response_owner] &
        (1u << threatened_owner)) != 0;
}

} // namespace

OwnerThreatPointResponseResult HandleOwnerThreatPointResponseQueue(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    UnitMovementPoint& point,
    OwnerUnitEligibilityCallback hostile_pressure_eligibility,
    OwnerUnitEligibilityCallback response_unit_eligibility,
    OwnerTransportQueueUnitWeightCallback weight_callback,
    u32 max_distance,
    u32 response_weight_multiplier,
    const OwnerThreatPointCrossOwnerResponseQueues* cross_owner_response) {
    OwnerThreatPointResponseResult result;
    if (point.x == -1 || context.movement == nullptr) {
        return result;
    }

    result.slot_index = FindOwnerThreatResponseQueueSlotNearPoint(queue, point,
        max_distance);
    if (result.slot_index == kInvalidOwnerTransportQueueSlot) {
        result.slot_index = PrepareOwnerThreatResponseQueueSlot(queue, point);
        result.allocated_slot =
            result.slot_index != kInvalidOwnerTransportQueueSlot;
    }
    if (result.slot_index == kInvalidOwnerTransportQueueSlot) {
        return result;
    }

    OwnerTransportQueueSlot& response_slot = queue.slots[result.slot_index];
    response_slot.state = kOwnerTransportQueueStateThreatResponse;
    response_slot.target_x = point.x;
    response_slot.target_y = point.y;

    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || !owner_threat_unit_active(*unit)) {
            continue;
        }
        if (hostile_pressure_eligibility != nullptr) {
            if (!hostile_pressure_eligibility(*unit)) {
                continue;
            }
        }
        else if (!owner_threat_pressure_candidate_for_owner(context, owner_id,
                *unit)) {
            continue;
        }

        const u32 distance = CalculateApproxUnitDistance(point.x, point.y,
            unit->x, unit->y);
        if (distance > max_distance) {
            continue;
        }
        ++result.pressure.count;
        result.pressure.weight += owner_threat_unit_weight(*unit,
            weight_callback);
    }

    if (result.pressure.count == 0) {
        ClearOwnerThreatPointAndQueueSlot(context, queue, owner_id, point,
            result.slot_index);
        result.cleared = true;
        return result;
    }

    const u32 multiplier = std::max<u32>(response_weight_multiplier, 1);
    result.requested_weight =
        result.pressure.weight >
            std::numeric_limits<u32>::max() / multiplier ?
        std::numeric_limits<u32>::max() : result.pressure.weight * multiplier;
    if (response_slot.aux_value >= result.pressure.weight) {
        return result;
    }

    u32 matched_weight = response_slot.aux_value;
    auto reassign_response_unit = [&](UnitMovementUnit& unit, u32 source_slot) {
        const u32 weight = owner_threat_unit_weight(unit, weight_callback);
        AssignUnitToOwnerTransportQueueSlot(queue, unit, result.slot_index);
        if (queue.slots[source_slot].count != 0) {
            --queue.slots[source_slot].count;
        }
        ++result.moved_count;
        result.moved_weight += weight;
        matched_weight += weight;
        response_slot.aux_value += weight;
    };

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (matched_weight >= result.requested_weight) {
            break;
        }
        if (unit == nullptr || unit->owner_id != owner_id ||
            unit->type_id >= 0x60 || !owner_threat_unit_active(*unit)) {
            continue;
        }
        if (response_unit_eligibility != nullptr &&
            !response_unit_eligibility(*unit)) {
            continue;
        }

        const u32 source_slot = unit->area_marker_flags & 0xffu;
        if (source_slot == 0 || source_slot == result.slot_index ||
            source_slot >= queue.slots.size() ||
            !owner_threat_primary_response_source_state(
                queue.slots[source_slot].state)) {
            continue;
        }

        reassign_response_unit(*unit, source_slot);
    }

    if (matched_weight < result.requested_weight &&
        cross_owner_response != nullptr) {
        std::array<u32, 8> cross_owner_slots{};
        cross_owner_slots.fill(kInvalidOwnerTransportQueueSlot);
        for (u32 response_owner = 0; response_owner < cross_owner_slots.size();
             ++response_owner) {
            if (response_owner >= cross_owner_response->queues.size() ||
                response_owner >= cross_owner_response->active_owner_slots.size() ||
                !cross_owner_response->active_owner_slots[response_owner] ||
                !owner_threat_related_response_owner(context, response_owner,
                    owner_id)) {
                continue;
            }

            OwnerTransportQueueState* response_queue =
                cross_owner_response->queues[response_owner];
            if (response_queue == nullptr) {
                continue;
            }

            u32 cross_slot = FindOwnerThreatResponseQueueSlotNearPoint(
                *response_queue, point, max_distance);
            if (cross_slot == kInvalidOwnerTransportQueueSlot) {
                cross_slot = PrepareOwnerThreatResponseQueueSlot(*response_queue,
                    point);
            }
            cross_owner_slots[response_owner] = cross_slot;
        }

        for (UnitMovementUnit* unit : context.movement->active_units) {
            if (matched_weight >= result.requested_weight) {
                break;
            }
            if (unit == nullptr || unit->owner_id >= cross_owner_slots.size() ||
                unit->owner_id == owner_id || unit->type_id >= 0x60 ||
                !owner_threat_unit_active(*unit)) {
                continue;
            }
            if (response_unit_eligibility != nullptr &&
                !response_unit_eligibility(*unit)) {
                continue;
            }

            const u32 destination_slot = cross_owner_slots[unit->owner_id];
            if (destination_slot == kInvalidOwnerTransportQueueSlot) {
                continue;
            }
            OwnerTransportQueueState* response_queue =
                cross_owner_response->queues[unit->owner_id];
            if (response_queue == nullptr ||
                destination_slot >= response_queue->slots.size()) {
                continue;
            }

            const u32 source_slot = unit->area_marker_flags & 0xffu;
            if (source_slot == 0 || source_slot == destination_slot ||
                source_slot >= response_queue->slots.size() ||
                !owner_threat_primary_response_source_state(
                    response_queue->slots[source_slot].state)) {
                continue;
            }

            const u32 weight = owner_threat_unit_weight(*unit, weight_callback);
            AssignUnitToOwnerTransportQueueSlot(*response_queue, *unit,
                destination_slot);
            if (response_queue->slots[source_slot].count != 0) {
                --response_queue->slots[source_slot].count;
            }
            ++result.moved_count;
            result.moved_weight += weight;
            matched_weight += weight;
            response_slot.aux_value += weight;
        }
    }

    if (matched_weight != 0) {
        return result;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (matched_weight >= result.requested_weight) {
            break;
        }
        if (unit == nullptr || unit->owner_id != owner_id ||
            unit->type_id >= 0x60 || !owner_threat_unit_active(*unit)) {
            continue;
        }
        if (response_unit_eligibility != nullptr &&
            !response_unit_eligibility(*unit)) {
            continue;
        }

        const u32 source_slot = unit->area_marker_flags & 0xffu;
        if (source_slot == 0 || source_slot == result.slot_index ||
            source_slot >= queue.slots.size() ||
            queue.slots[source_slot].state != kOwnerTransportQueueStateWorkTarget) {
            continue;
        }

        reassign_response_unit(*unit, source_slot);
    }

    return result;
}

void SetOwnerTransportQueueSlotTarget(OwnerTransportQueueSlot& slot,
    const OwnerTransportQueueTargetSnapshot& target) {
    slot.match_value = target.match_value;
    slot.target_x = target.target_x;
    slot.target_y = target.target_y;
    slot.route_index = target.route_index;
}

u32 FindOrAllocateOwnerTransportQueueSlotByStateAndValue(
    OwnerTransportQueueState& queue, u32 state,
    const OwnerTransportQueueTargetSnapshot& target) {
    u32 slot_index = FindOwnerTransportQueueSlotByStateAndValue(queue, state,
        target.match_value);
    if (slot_index != kInvalidOwnerTransportQueueSlot) {
        return slot_index;
    }

    slot_index = AllocateOwnerTransportQueueSlot(queue, state);
    if (slot_index == kInvalidOwnerTransportQueueSlot) {
        return kInvalidOwnerTransportQueueSlot;
    }
    SetOwnerTransportQueueSlotTarget(queue.slots[slot_index], target);
    return slot_index;
}

void AssignUnitToOwnerTransportQueueSlot(OwnerTransportQueueState& queue,
    UnitMovementUnit& unit, u32 slot_index) {
    if (slot_index >= queue.slots.size()) {
        return;
    }
    unit.area_marker_flags =
        (unit.area_marker_flags & 0xffffff00u) | slot_index;
    ++queue.slots[slot_index].count;
}

u32 CalculateOwnerTransportQueueTargetDistanceThreshold(
    const OwnerTransportQueueSlot& slot, u32 base_tiles) {
    return ((slot.count >> 1) + base_tiles) * 0x20;
}

bool CheckOwnerTransportQueueUnitWithinTargetThreshold(
    const UnitMovementUnit& unit, const OwnerTransportQueueSlot& slot,
    u32 base_tiles) {
    const u32 distance = CalculateApproxUnitDistance(unit.x, unit.y,
        slot.target_x, slot.target_y);
    return distance <=
        CalculateOwnerTransportQueueTargetDistanceThreshold(slot, base_tiles);
}

bool AdvanceOwnerTransportQueueProgressNearTarget(
    const UnitMovementUnit& unit, OwnerTransportQueueSlot& slot, u32 base_tiles) {
    if (!CheckOwnerTransportQueueUnitWithinTargetThreshold(unit, slot, base_tiles)) {
        return false;
    }
    ++slot.completed_count;
    return true;
}

OwnerTransportQueueRetargetResult ReassignOwnerTransportUnitsInTransitRangeToTarget(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 max_units, u32 destination_state,
    const OwnerTransportQueueTargetSnapshot& target) {
    OwnerTransportQueueRetargetResult result;
    if (context.movement == nullptr || max_units == 0) {
        return result;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id || unit->type_id >= 0x60) {
            continue;
        }

        const u32 source_slot = unit->area_marker_flags & 0xffu;
        if (source_slot >= queue.slots.size()) {
            continue;
        }
        const u32 source_state = queue.slots[source_slot].state;
        if (source_state <= 5 || source_state >= 9) {
            continue;
        }

        if (result.destination_slot == kInvalidOwnerTransportQueueSlot) {
            result.destination_slot = AllocateOwnerTransportQueueSlot(queue,
                destination_state);
            if (result.destination_slot == kInvalidOwnerTransportQueueSlot) {
                return result;
            }
            SetOwnerTransportQueueSlotTarget(queue.slots[result.destination_slot],
                target);
        }

        AssignUnitToOwnerTransportQueueSlot(queue, *unit, result.destination_slot);
        if (queue.slots[source_slot].count != 0) {
            --queue.slots[source_slot].count;
        }
        ++result.moved_count;
        if (result.moved_count >= max_units) {
            break;
        }
    }
    return result;
}

u32 ReassignOwnerTransportUnitsBetweenQueueSlots(UnitCommandContext& context,
    OwnerTransportQueueState& queue, u32 owner_id, u32 source_slot,
    u32 destination_slot, u32 max_units, bool skip_suppressed_units) {
    if (context.movement == nullptr || max_units == 0 ||
        source_slot == destination_slot || source_slot >= queue.slots.size() ||
        destination_slot >= queue.slots.size()) {
        return 0;
    }

    u32 moved_count = 0;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id || unit->type_id >= 0x60) {
            continue;
        }
        if ((unit->area_marker_flags & 0xffu) != source_slot) {
            continue;
        }
        if (skip_suppressed_units && (unit->runtime_flags & 4u) != 0) {
            continue;
        }

        ReleaseUnitOwnerTransportQueueSlotReference(queue, *unit);
        AssignUnitToOwnerTransportQueueSlot(queue, *unit, destination_slot);
        ++moved_count;
        if (moved_count >= max_units) {
            break;
        }
    }
    return moved_count;
}

u32 AssignEmptyCarrierToOwnerDropoffQueueSlot(OwnerTransportQueueState& queue,
    UnitMovementUnit& carrier, const OwnerTransportQueueTargetSnapshot& target) {
    if (carrier.cargo_amount != 0) {
        return kInvalidOwnerTransportQueueSlot;
    }

    OwnerTransportQueueTargetSnapshot dropoff_target = target;
    dropoff_target.match_value = 0;
    const u32 slot_index = FindOrAllocateOwnerTransportQueueSlotByStateAndValue(
        queue, kOwnerTransportQueueStateDropoff, dropoff_target);
    if (slot_index == kInvalidOwnerTransportQueueSlot) {
        return kInvalidOwnerTransportQueueSlot;
    }

    SetOwnerTransportQueueSlotTarget(queue.slots[slot_index], dropoff_target);
    AssignUnitToOwnerTransportQueueSlot(queue, carrier, slot_index);
    return slot_index;
}

u32 CalculateOwnerTransportRouteDesiredSlotCount(
    const OwnerTransportRouteState& route_state, u32 route_index) {
    return CalculateOwnerTransportRouteDesiredSlotCountWithPercent(route_state,
        route_index, route_state.load_percent);
}

u32 CalculateOwnerTransportRouteDesiredSlotCountWithPercent(
    const OwnerTransportRouteState& route_state, u32 route_index, u32 percent) {
    if (route_index >= route_state.targets.size()) {
        return 0;
    }

    u32 desired = route_state.targets[route_index].desired_count_base *
        percent / 100;
    if (route_index == 0) {
        desired += 4;
    }
    return desired;
}

bool CheckOwnerTransportRouteMetricTile(const UnitMovementCell& cell) {
    return (cell.flags & kMapCellTerrainMask) == kMapCellPassableTerrain &&
        (cell.flags & kMapCellBlockedTerrain) == 0;
}

OwnerTransportRouteMetrics CalculateOwnerTransportRouteMetricsAroundTile(
    const UnitMovementMap& map, UnitMovementPoint center_tile, u32 radius) {
    OwnerTransportRouteMetrics metrics;
    u32 nearest_distance = 0xffffffff;
    const i32 radius_signed = static_cast<i32>(radius);
    for (i32 y = center_tile.y - radius_signed; y < center_tile.y + radius_signed; ++y) {
        for (i32 x = center_tile.x - radius_signed; x < center_tile.x + radius_signed; ++x) {
            if (x < 0 || y < 0 ||
                static_cast<u32>(x) >= map.width ||
                static_cast<u32>(y) >= map.height) {
                continue;
            }

            const UnitMovementCell* cell = GetMovementCell(map,
                static_cast<u32>(x), static_cast<u32>(y));
            if (cell == nullptr || !CheckOwnerTransportRouteMetricTile(*cell)) {
                continue;
            }

            ++metrics.passable_tile_count;
            metrics.harvest_amount_sum +=
                (cell->flags & kMapCellHarvestAmountMask) >>
                kMapCellHarvestAmountShift;
            const u32 distance = CalculateApproxUnitDistance(center_tile.x,
                center_tile.y, x, y);
            if (distance < nearest_distance) {
                nearest_distance = distance;
                metrics.nearest_tile = {x, y};
                metrics.found_nearest_tile = true;
            }
        }
    }
    return metrics;
}

OwnerTransportRouteMetrics CalculateOwnerTransportRouteMetricsForUnit(
    const UnitMovementMap& map, const UnitMovementUnit& unit, u32 radius) {
    const u32 metric_x = static_cast<u32>(unit.x) +
        static_cast<u32>(unit.definition.owner_transport_route_metric_offset);
    const u32 metric_y = static_cast<u32>(unit.y) +
        static_cast<u32>(unit.definition.owner_transport_route_metric_offset);
    const UnitMovementPoint center_tile{
        static_cast<i32>(metric_x >> 5),
        static_cast<i32>(metric_y >> 5)};
    return CalculateOwnerTransportRouteMetricsAroundTile(map, center_tile, radius);
}

u32 RebuildOwnerTransportRouteTargetsForOwnedUnits(
    const UnitCommandContext& context, OwnerTransportRouteState& route_state,
    u32 owner_id) {
    route_state.route_count = 0;
    if (context.movement == nullptr) {
        return 0;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id ||
            !CheckOwnerTransportRouteTargetUnitType(unit->type_id)) {
            continue;
        }
        if (route_state.route_count >= route_state.targets.size()) {
            break;
        }

        const OwnerTransportRouteMetrics metrics =
            CalculateOwnerTransportRouteMetricsForUnit(context.movement->map, *unit);
        OwnerTransportRouteTarget& target =
            route_state.targets[route_state.route_count];
        target.unit = unit;
        target.target.match_value = unit->id;
        target.target.target_x = metrics.nearest_tile.x;
        target.target.target_y = metrics.nearest_tile.y;
        target.desired_count_base = std::max(target.desired_count_base,
            metrics.passable_tile_count);
        target.secondary_count_base = std::max(target.secondary_count_base,
            metrics.harvest_amount_sum);
        if (metrics.harvest_amount_sum == 0) {
            target.flags |= 2u;
        }
        ++route_state.route_count;
    }
    return route_state.route_count;
}

u32 FindAndAppendOwnerTransportRouteTargetUnit(
    const UnitCommandContext& context, OwnerTransportRouteState& route_state,
    u32 owner_id) {
    if (context.movement == nullptr ||
        route_state.route_count >= route_state.targets.size()) {
        return kInvalidOwnerTransportQueueSlot;
    }

    const u32 route_count = std::min<u32>(route_state.route_count,
        static_cast<u32>(route_state.targets.size()));
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id ||
            !CheckOwnerTransportRouteTargetUnitType(unit->type_id)) {
            continue;
        }

        bool already_present = false;
        for (u32 index = 0; index < route_count; ++index) {
            const OwnerTransportRouteTarget& target = route_state.targets[index];
            if (target.unit == unit || target.target.match_value == unit->id) {
                already_present = true;
                break;
            }
        }
        if (already_present) {
            continue;
        }

        const OwnerTransportRouteMetrics metrics =
            CalculateOwnerTransportRouteMetricsForUnit(context.movement->map, *unit);
        OwnerTransportRouteTarget route_target =
            route_state.targets[route_state.route_count];
        route_target.unit = unit;
        route_target.target.match_value = unit->id;
        route_target.target.target_x = metrics.nearest_tile.x;
        route_target.target.target_y = metrics.nearest_tile.y;
        route_target.desired_count_base = std::max(route_target.desired_count_base,
            metrics.passable_tile_count);
        route_target.secondary_count_base =
            std::max(route_target.secondary_count_base, metrics.harvest_amount_sum);
        if (metrics.harvest_amount_sum == 0) {
            route_target.flags |= 2u;
        }
        return AppendOwnerTransportRouteTargetIfMissing(route_state, route_target);
    }
    return kInvalidOwnerTransportQueueSlot;
}

OwnerTransportRouteMetrics CalculateOwnerTransportRouteMetricsForOwnedTargets(
    const UnitCommandContext& context, u32 owner_id) {
    OwnerTransportRouteMetrics total;
    if (context.movement == nullptr) {
        return total;
    }

    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id ||
            !CheckOwnerTransportRouteTargetUnitType(unit->type_id)) {
            continue;
        }

        const OwnerTransportRouteMetrics metrics =
            CalculateOwnerTransportRouteMetricsForUnit(context.movement->map, *unit);
        total.passable_tile_count += metrics.passable_tile_count;
        total.harvest_amount_sum += metrics.harvest_amount_sum;
        if (metrics.found_nearest_tile) {
            total.nearest_tile = metrics.nearest_tile;
            total.found_nearest_tile = true;
        }
    }
    return total;
}

void RefreshOwnerTransportRouteTargetMetrics(
    const UnitCommandContext& context, OwnerTransportRouteState& route_state) {
    if (context.movement == nullptr) {
        return;
    }

    const u32 route_count = std::min<u32>(route_state.route_count,
        static_cast<u32>(route_state.targets.size()));
    for (u32 index = 0; index < route_count; ++index) {
        OwnerTransportRouteTarget& target = route_state.targets[index];
        if (target.unit == nullptr) {
            continue;
        }

        const OwnerTransportRouteMetrics metrics =
            CalculateOwnerTransportRouteMetricsForUnit(context.movement->map,
                *target.unit);
        target.priority = metrics.harvest_amount_sum;
        if (metrics.harvest_amount_sum == 0) {
            target.flags |= 2u;
            target.desired_count_base = 0;
            continue;
        }

        target.target.target_x = metrics.nearest_tile.x;
        target.target.target_y = metrics.nearest_tile.y;
    }
}

OwnerRouteHelperProductionSummary
QueueOwnerRouteHelperProductionForUnderloadedTargets(
    UnitCommandContext& context, const OwnerTransportQueueState& queue,
    const OwnerTransportRouteState& route_state, u32 owner_id,
    u32 helper_load_percent, u32 production_unit_type,
    u32 production_resource_cost) {
    OwnerRouteHelperProductionSummary summary;
    if (context.movement == nullptr || helper_load_percent == 0 ||
        production_unit_type == kInvalidOwnerTransportQueueSlot ||
        owner_id >= context.owner_resources.size()) {
        return summary;
    }

    const u32 route_count = std::min<u32>(route_state.route_count,
        static_cast<u32>(route_state.targets.size()));
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state != kOwnerTransportQueueStateWorkTarget ||
            slot.match_value == 0 || slot.route_index >= route_count) {
            continue;
        }

        ++summary.scanned_slot_count;
        const OwnerTransportRouteTarget& route_target =
            route_state.targets[slot.route_index];
        const u32 desired_count = static_cast<u32>(
            (static_cast<u64>(route_target.desired_count_base) *
                helper_load_percent) / 100u);
        if (slot.count >= desired_count) {
            continue;
        }

        ++summary.underloaded_slot_count;
        if (production_resource_cost > context.owner_resources[owner_id]) {
            summary.stopped_for_resource_limit = true;
            break;
        }

        UnitMovementUnit* target_unit = find_unit_by_id(context, slot.match_value);
        if (target_unit == nullptr ||
            command_metadata_flags(context, *target_unit) != 1) {
            ++summary.metadata_blocked_count;
            continue;
        }

        SetOrQueueUnitCommand10(target_unit, production_unit_type, true);
        context.owner_resources[owner_id] -= production_resource_cost;
        summary.resource_spent += production_resource_cost;
        ++summary.queued_command_count;
    }
    return summary;
}

namespace {

u32 redistribute_owner_transport_work_target_overflow(
    UnitCommandContext& context, OwnerTransportQueueState& queue,
    const OwnerTransportRouteState& route_state, u32 owner_id, u32 source_slot,
    u32 desired_count, u32 overflow_percent) {
    const u32 overflow_count = CalculateOwnerTransportQueueSlotOverflow(queue,
        source_slot, desired_count);
    if (overflow_count == 0) {
        return 0;
    }

    OwnerTransportQueueOverflowTarget target =
        FindOwnerTransportUnderfilledWorkTargetSlot(queue, route_state,
            overflow_count, overflow_percent);
    u32 destination_slot = target.slot_index;
    if (destination_slot == kInvalidOwnerTransportQueueSlot) {
        target.route_index = FindOwnerTransportHighestPriorityOpenRouteTarget(
            route_state);
        if (target.route_index == kInvalidOwnerTransportQueueSlot) {
            return 0;
        }

        OwnerTransportQueueTargetSnapshot snapshot =
            route_state.targets[target.route_index].target;
        snapshot.route_index = target.route_index;
        destination_slot = FindOrAllocateOwnerTransportQueueSlotByStateAndValue(
            queue, kOwnerTransportQueueStateWorkTarget, snapshot);
        if (destination_slot == kInvalidOwnerTransportQueueSlot) {
            return 0;
        }
        queue.slots[destination_slot].route_index = target.route_index;
        target.move_count = overflow_count;
    }

    return ReassignOwnerTransportUnitsBetweenQueueSlots(context, queue,
        owner_id, source_slot, destination_slot,
        std::min(overflow_count, target.move_count), true);
}

} // namespace

OwnerTransportRouteTargetMaintenanceResult HandleOwnerTransportRouteTargetMaintenance(
    UnitCommandContext& context, OwnerTransportQueueState& queue,
    OwnerTransportRouteState& route_state, u32 owner_id,
    u32 overflow_percent, u32 production_load_percent,
    u32 helper_load_percent, u32 helper_unit_type,
    const UnitMovementDefinition* helper_definition, u32 helper_unit_resource_cost,
    const std::array<u32, 4>* route_helper_unit_types,
    OwnerRouteHelperPathScoreCallback path_score,
    OwnerRouteHelperPlacementPredicate placement_predicate, void* user_data,
    u32 owner_profile_age, u32 route_helper_producer_unit_type,
    bool route_helper_dispatch_prerequisites_met,
    u32 route_helper_producer_resource_cost) {
    OwnerTransportRouteTargetMaintenanceResult result;
    if (context.movement == nullptr) {
        return result;
    }

    route_state.load_percent = overflow_percent;
    result.route_count = std::min<u32>(route_state.route_count,
        static_cast<u32>(route_state.targets.size()));
    for (u32 route_index = 0; route_index < result.route_count; ++route_index) {
        OwnerTransportRouteTarget& route_target =
            route_state.targets[route_index];
        if (route_target.unit == nullptr) {
            continue;
        }

        const OwnerTransportRouteMetrics metrics =
            CalculateOwnerTransportRouteMetricsForUnit(context.movement->map,
                *route_target.unit);
        route_target.priority = metrics.harvest_amount_sum;
        if (metrics.harvest_amount_sum == 0) {
            route_target.flags |= 2u;
            route_target.desired_count_base = 0;
            ++result.depleted_target_count;

            const u32 source_slot = FindOwnerTransportQueueSlotByStateAndValue(
                queue, kOwnerTransportQueueStateWorkTarget,
                route_target.target.match_value);
            if (source_slot != kInvalidOwnerTransportQueueSlot) {
                if (route_index == 0) {
                    if (queue.slots[source_slot].count > 4) {
                        const u32 moved =
                            redistribute_owner_transport_work_target_overflow(
                                context, queue, route_state, owner_id,
                                source_slot, 4, overflow_percent);
                        if (moved != 0) {
                            ++result.overflow_action_count;
                            result.overflow_moved_count += moved;
                        }
                    }
                }
                else {
                    queue.slots[source_slot].match_value = 0;
                }
            }
            continue;
        }

        if (metrics.found_nearest_tile) {
            route_target.target.target_x = metrics.nearest_tile.x;
            route_target.target.target_y = metrics.nearest_tile.y;
        }
    }

    const u32 zero_target_slot = FindOwnerTransportQueueSlotByStateAndValue(queue,
        kOwnerTransportQueueStateWorkTarget, 0);
    if (zero_target_slot != kInvalidOwnerTransportQueueSlot) {
        const u32 moved = redistribute_owner_transport_work_target_overflow(
            context, queue, route_state, owner_id, zero_target_slot, 0,
            overflow_percent);
        if (moved != 0) {
            ++result.overflow_action_count;
            result.overflow_moved_count += moved;
        }
    }

    const bool has_route_helper_producer_type =
        route_helper_producer_unit_type != kInvalidOwnerTransportQueueSlot;
    result.helper_production_summary =
        QueueOwnerRouteHelperProductionForUnderloadedTargets(context, queue,
            route_state, owner_id, production_load_percent,
            has_route_helper_producer_type ? route_helper_producer_unit_type :
                helper_unit_type,
            has_route_helper_producer_type ? route_helper_producer_resource_cost :
                helper_unit_resource_cost);

    if (helper_unit_type == kInvalidOwnerTransportQueueSlot ||
        helper_definition == nullptr) {
        return result;
    }

    result.helper_queue_summary = SummarizeOwnerRouteHelperQueueSlots(queue,
        helper_unit_resource_cost);
    if (result.helper_queue_summary.active_slot_count != 0) {
        return result;
    }
    if (helper_load_percent == 0 || (owner_profile_age & 0x1fu) != 0) {
        return result;
    }
    if (!route_helper_dispatch_prerequisites_met) {
        return result;
    }

    UnitMovementPoint route_helper_origin_world{-1, -1};
    UnitMovementPoint route_helper_reference_tile{-1, -1};
    if (result.route_count != 0 && route_state.targets[0].unit != nullptr) {
        const UnitMovementUnit& route_origin = *route_state.targets[0].unit;
        route_helper_origin_world = {route_origin.x, route_origin.y};
        route_helper_reference_tile = {
            static_cast<i32>(static_cast<u32>(route_origin.x) >> 5),
            static_cast<i32>(static_cast<u32>(route_origin.y) >> 5),
        };
    }

    std::array<u32, 4> helper_types{
        helper_unit_type, helper_unit_type, helper_unit_type, helper_unit_type};
    if (route_helper_unit_types != nullptr) {
        helper_types = *route_helper_unit_types;
    }

    for (u32 route_index = 0; route_index < result.route_count; ++route_index) {
        OwnerTransportRouteTarget& target = route_state.targets[route_index];
        if (target.unit == nullptr) {
            continue;
        }

        const OwnerTransportRouteMetrics metrics =
            CalculateOwnerTransportRouteMetricsForUnit(context.movement->map,
                *target.unit);
        if (!CheckOwnerRouteTargetBelowHelperThreshold(target, metrics,
                helper_load_percent)) {
            continue;
        }
        target.flags |= 1u;

        const UnitMovementPoint route_tile{
            target.target.target_x,
            target.target.target_y,
        };
        const UnitMovementPoint candidate_reference_tile =
            route_helper_reference_tile.x >= 0 && route_helper_reference_tile.y >= 0
            ? route_helper_reference_tile : route_tile;
        OwnerRouteHelperCandidateSet candidates =
            BuildOwnerRouteHelperCandidateClusters(context.movement->map,
                candidate_reference_tile);
        MarkOwnerRouteHelperCandidateClustersOccupiedByUnits(candidates, context,
            helper_types);
        const UnitMovementPoint origin_world =
            route_helper_origin_world.x >= 0 && route_helper_origin_world.y >= 0
            ? route_helper_origin_world
            : UnitMovementPoint{target.target.target_x * 32,
                target.target.target_y * 32};
        result.helper_placement = SelectOwnerRouteHelperCandidateTile(
            context.movement->map, candidates, origin_world, *helper_definition,
            path_score, placement_predicate, user_data);
        if (!result.helper_placement.found) {
            break;
        }

        result.helper_dispatch = DispatchOwnerRouteHelperProducer(context, queue,
            owner_id, helper_unit_type, result.helper_placement.tile,
            route_state.route_count, route_helper_producer_unit_type);
        if (result.helper_dispatch.assigned) {
            break;
        }
        if (result.helper_dispatch.producer_unit == nullptr) {
            target.flags &= ~1u;
            break;
        }
    }

    return result;
}

OwnerRouteTargetProbe FindNearestNeutralUnitToPrimaryRouteTarget(
    const UnitCommandContext& context, const OwnerTransportRouteState& route_state,
    u32 neutral_owner_id) {
    OwnerRouteTargetProbe probe;
    if (context.movement == nullptr || route_state.targets.empty() ||
        route_state.targets[0].unit == nullptr) {
        return probe;
    }

    const UnitMovementUnit& route_target = *route_state.targets[0].unit;
    u32 best_distance = 0xffffffff;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != neutral_owner_id ||
            unit->type_id >= 0x60) {
            continue;
        }

        const u32 distance = CalculateApproxUnitDistance(route_target.x,
            route_target.y, unit->x, unit->y);
        if (distance < best_distance) {
            probe.unit = unit;
            probe.point = {unit->x, unit->y};
            probe.has_point = true;
            best_distance = distance;
        }
    }
    return probe;
}

OwnerRouteHelperQueueSummary SummarizeOwnerRouteHelperQueueSlots(
    const OwnerTransportQueueState& queue, u32 helper_unit_resource_cost) {
    OwnerRouteHelperQueueSummary summary;
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const u32 state = queue.slots[index].state;
        if (state != kOwnerTransportQueueStateRouteHelperPending &&
            state != kOwnerTransportQueueStateRouteHelperActive) {
            continue;
        }
        ++summary.active_slot_count;
        summary.reserved_resource_cost += helper_unit_resource_cost;
    }
    return summary;
}

bool CheckOwnerRouteTargetBelowHelperThreshold(
    const OwnerTransportRouteTarget& target,
    const OwnerTransportRouteMetrics& current_metrics, u32 helper_load_percent) {
    if (target.unit == nullptr || (target.flags & 3u) != 0) {
        return false;
    }

    const u32 threshold = static_cast<u32>(
        (static_cast<unsigned long long>(target.secondary_count_base) *
            helper_load_percent) / 100);
    return current_metrics.harvest_amount_sum <= threshold;
}

u32 GetOwnerRouteHelperTerrainGroup(const UnitMovementCell& cell) {
    return (cell.alternate_flags >> 26) & 7u;
}

u32 GetOwnerRouteHelperResourceAmount(const UnitMovementCell& cell) {
    return (cell.flags >> kMapCellHarvestAmountShift) & 0xffffu;
}

bool CheckOwnerRouteHelperClusterSourceTile(const UnitMovementCell& cell) {
    return (cell.flags & kMapCellTerrainMask) == kMapCellPassableTerrain &&
        (cell.flags & kMapCellBlockedTerrain) == 0;
}

bool FindOwnerRouteHelperNearbyPassablePlacementTile(const UnitMovementMap& map,
    UnitMovementPoint tile) {
    for (i32 y = tile.y - 4; y <= tile.y + 4; ++y) {
        for (i32 x = tile.x - 4; x <= tile.x + 4; ++x) {
            if (x < 0 || y < 0) {
                continue;
            }
            const UnitMovementCell* cell = GetMovementCell(map,
                static_cast<u32>(x), static_cast<u32>(y));
            if (cell != nullptr &&
                (cell->flags & kMapCellTerrainMask) == kMapCellPassableTerrain) {
                return true;
            }
        }
    }
    return false;
}

bool AppendOwnerRouteHelperCandidateCluster(
    OwnerRouteHelperCandidateSet& candidate_set, UnitMovementPoint tile,
    u32 terrain_group, u32 resource_amount,
    UnitMovementPoint replacement_reference_tile) {
    for (u32 index = candidate_set.cluster_count; index-- > 0;) {
        OwnerRouteHelperCandidateCluster& cluster = candidate_set.clusters[index];
        if (cluster.terrain_group != terrain_group ||
            cluster.tile_count >= kOwnerRouteHelperCandidateClusterTileCapacity) {
            continue;
        }

        if (std::abs(tile.x - cluster.center_tile.x) >
                static_cast<i32>(kOwnerRouteHelperCandidateClusterMergeAxisDistance) ||
            std::abs(tile.y - cluster.center_tile.y) >
                static_cast<i32>(kOwnerRouteHelperCandidateClusterMergeAxisDistance)) {
            continue;
        }

        const i32 dx = tile.x - cluster.center_tile.x;
        const i32 dy = tile.y - cluster.center_tile.y;
        cluster.resource_amount_sum += resource_amount;
        cluster.center_tile.x += dx / 2;
        cluster.center_tile.y += dy / 2;
        cluster.tiles[cluster.tile_count++] = tile;
        return true;
    }

    u32 slot_index = candidate_set.cluster_count;
    if (slot_index >= candidate_set.clusters.size()) {
        if (replacement_reference_tile.x < 0 || replacement_reference_tile.y < 0) {
            return false;
        }

        u32 farthest_distance = 0;
        bool found_replacement = false;
        for (u32 index = 0; index < candidate_set.cluster_count; ++index) {
            const OwnerRouteHelperCandidateCluster& cluster =
                candidate_set.clusters[index];
            const u32 distance = CalculateApproxUnitDistance(
                replacement_reference_tile.x, replacement_reference_tile.y,
                cluster.center_tile.x, cluster.center_tile.y);
            if (!found_replacement || distance > farthest_distance) {
                found_replacement = true;
                farthest_distance = distance;
                slot_index = index;
            }
        }
        if (!found_replacement) {
            return false;
        }
    }
    else {
        ++candidate_set.cluster_count;
    }

    OwnerRouteHelperCandidateCluster& cluster = candidate_set.clusters[slot_index];
    cluster = {};
    cluster.terrain_group = terrain_group;
    cluster.resource_amount_sum = resource_amount;
    cluster.center_tile = tile;
    cluster.tiles[0] = tile;
    cluster.tile_count = 1;
    return true;
}

OwnerRouteHelperCandidateSet BuildOwnerRouteHelperCandidateClusters(
    const UnitMovementMap& map, UnitMovementPoint replacement_reference_tile) {
    OwnerRouteHelperCandidateSet candidate_set;
    if (map.width == 0 || map.height == 0) {
        return candidate_set;
    }

    for (u32 y = 0; y < map.height; ++y) {
        for (u32 x = 0; x < map.width; ++x) {
            const UnitMovementCell* cell = GetMovementCell(map, x, y);
            if (cell == nullptr || !CheckOwnerRouteHelperClusterSourceTile(*cell)) {
                continue;
            }

            AppendOwnerRouteHelperCandidateCluster(candidate_set,
                UnitMovementPoint{static_cast<i32>(x), static_cast<i32>(y)},
                GetOwnerRouteHelperTerrainGroup(*cell),
                GetOwnerRouteHelperResourceAmount(*cell),
                replacement_reference_tile);
        }
    }
    return candidate_set;
}

void MarkOwnerRouteHelperCandidateClustersOccupiedByUnits(
    OwnerRouteHelperCandidateSet& candidate_set, const UnitCommandContext& context,
    const std::array<u32, 4>& route_helper_unit_types) {
    if (context.movement == nullptr || context.movement->map.width == 0 ||
        context.movement->map.height == 0) {
        return;
    }

    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr ||
            std::find(route_helper_unit_types.begin(), route_helper_unit_types.end(),
                unit->type_id) == route_helper_unit_types.end()) {
            continue;
        }

        const UnitMovementPoint unit_tile{
            static_cast<i32>(static_cast<u32>(unit->x) >> 5),
            static_cast<i32>(static_cast<u32>(unit->y) >> 5),
        };
        if (unit_tile.x < 0 || unit_tile.y < 0 ||
            static_cast<u32>(unit_tile.x) >= context.movement->map.width ||
            static_cast<u32>(unit_tile.y) >= context.movement->map.height) {
            continue;
        }

        const UnitMovementCell* cell = GetMovementCell(context.movement->map,
            static_cast<u32>(unit_tile.x), static_cast<u32>(unit_tile.y));
        const u32 terrain_group = cell != nullptr ?
            GetOwnerRouteHelperTerrainGroup(*cell) : 0;

        for (u32 index = 0; index < candidate_set.cluster_count; ++index) {
            OwnerRouteHelperCandidateCluster& cluster = candidate_set.clusters[index];
            if (cluster.terrain_group != terrain_group) {
                continue;
            }

            const u32 distance = CalculateApproxUnitDistance(unit_tile.x, unit_tile.y,
                cluster.center_tile.x, cluster.center_tile.y);
            if (distance < kOwnerRouteHelperExistingUnitBlockDistance) {
                cluster.blocked = true;
            }
        }
    }
}

bool CheckOwnerRouteHelperPlacementFootprint(const UnitMovementMap& map,
    UnitMovementPoint tile, const UnitMovementDefinition& helper_definition,
    u32 terrain_group) {
    constexpr u32 kRouteHelperLayerRequiredMask = 0xa0000000u;
    constexpr u32 kRouteHelperTerrainBlockMask = kMapCellTerrainMask | 0x40000000u;
    const u32 width = std::max<u32>(helper_definition.footprint_width_tiles, 1);
    const u32 height = std::max<u32>(helper_definition.footprint_height_tiles, 1);
    if (tile.x < 0 || tile.y < 0 ||
        static_cast<u32>(tile.x) + width > map.width ||
        static_cast<u32>(tile.y) + height > map.height) {
        return false;
    }

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const UnitMovementCell* cell = GetMovementCell(map,
                static_cast<u32>(tile.x) + x, static_cast<u32>(tile.y) + y);
            if (cell == nullptr) {
                return false;
            }

            const u32 route_layer = cell->alternate_flags;
            if (((route_layer >> 26) & 7u) != terrain_group ||
                (route_layer & kRouteHelperLayerRequiredMask) !=
                    kRouteHelperLayerRequiredMask ||
                (cell->flags & kRouteHelperTerrainBlockMask) != 0 ||
                FindOwnerRouteHelperNearbyPassablePlacementTile(map,
                    UnitMovementPoint{
                        tile.x + static_cast<i32>(x),
                        tile.y + static_cast<i32>(y),
                    })) {
                return false;
            }
        }
    }
    return true;
}

OwnerRouteHelperPlacementSearchResult FindOwnerRouteHelperPlacementNearTile(
    const UnitMovementMap& map, UnitMovementPoint center_tile, u32 terrain_group,
    const UnitMovementDefinition& helper_definition, u32 max_spiral_steps,
    OwnerRouteHelperPlacementPredicate predicate, void* user_data) {
    OwnerRouteHelperPlacementSearchResult result;
    UnitMovementPoint cursor = center_tile;
    const i32 half_width =
        static_cast<i32>(std::max<u32>(helper_definition.footprint_width_tiles, 1) / 2);
    const i32 half_height =
        static_cast<i32>(std::max<u32>(helper_definition.footprint_height_tiles, 1) / 2);
    u32 best_distance = 0xffffffffu;

    auto try_candidate = [&](UnitMovementPoint candidate) {
        if (!CheckOwnerRouteHelperPlacementFootprint(map, candidate,
                helper_definition, terrain_group)) {
            return;
        }
        if (predicate != nullptr &&
            !predicate(map, candidate, helper_definition, terrain_group, user_data)) {
            return;
        }
        const u32 distance = CalculateApproxUnitDistance(center_tile.x,
            center_tile.y, candidate.x + half_width, candidate.y + half_height);
        if (distance >= best_distance) {
            return;
        }
        best_distance = distance;
        result.found = true;
        result.tile = candidate;
    };

    u32 completed_sides = 0;
    for (u32 step = 1; completed_sides < max_spiral_steps; ++step) {
        for (u32 i = 0; i < step; ++i) {
            ++cursor.x;
            try_candidate(cursor);
        }
        ++completed_sides;

        for (u32 i = 0; i < step; ++i) {
            ++cursor.y;
            try_candidate(cursor);
        }
        ++completed_sides;
        ++step;

        for (u32 i = 0; i < step; ++i) {
            --cursor.x;
            try_candidate(cursor);
        }
        ++completed_sides;

        for (u32 i = 0; i < step; ++i) {
            --cursor.y;
            try_candidate(cursor);
        }
        ++completed_sides;
    }
    return result;
}

OwnerRouteHelperPlacementSearchResult SelectOwnerRouteHelperCandidateTile(
    const UnitMovementMap& map, OwnerRouteHelperCandidateSet& candidate_set,
    UnitMovementPoint origin_world, const UnitMovementDefinition& helper_definition,
    OwnerRouteHelperPathScoreCallback path_score,
    OwnerRouteHelperPlacementPredicate placement_predicate, void* user_data) {
    OwnerRouteHelperPlacementSearchResult result;

    auto cluster_average = [](const OwnerRouteHelperCandidateCluster& cluster) {
        if (cluster.tile_count == 0) {
            return cluster.center_tile;
        }

        i32 sum_x = 0;
        i32 sum_y = 0;
        for (u32 i = 0; i < cluster.tile_count; ++i) {
            sum_x += cluster.tiles[i].x;
            sum_y += cluster.tiles[i].y;
        }
        return UnitMovementPoint{
            sum_x / static_cast<i32>(cluster.tile_count),
            sum_y / static_cast<i32>(cluster.tile_count),
        };
    };

    for (;;) {
        u32 selected_index = kInvalidOwnerTransportQueueSlot;
        u32 selected_score = 0xffffffff;

        if (path_score != nullptr) {
            for (u32 index = 0; index < candidate_set.cluster_count; ++index) {
                const OwnerRouteHelperCandidateCluster& cluster =
                    candidate_set.clusters[index];
                if (cluster.blocked) {
                    continue;
                }

                const UnitMovementPoint candidate_world{
                    static_cast<i32>(static_cast<u32>(cluster.center_tile.x) << 5),
                    static_cast<i32>(static_cast<u32>(cluster.center_tile.y) << 5),
                };
                u32 score = 0xffffffff;
                if (path_score(origin_world, candidate_world, score, user_data) &&
                    score < selected_score) {
                    selected_index = index;
                    selected_score = score;
                }
            }
        }

        if (selected_index == kInvalidOwnerTransportQueueSlot) {
            for (u32 index = 0; index < candidate_set.cluster_count; ++index) {
                const OwnerRouteHelperCandidateCluster& cluster =
                    candidate_set.clusters[index];
                if (cluster.blocked) {
                    continue;
                }

                const UnitMovementPoint candidate_world{
                    static_cast<i32>(static_cast<u32>(cluster.center_tile.x) << 5),
                    static_cast<i32>(static_cast<u32>(cluster.center_tile.y) << 5),
                };
                const u32 score = CalculateApproxUnitDistance(origin_world.x,
                    origin_world.y, candidate_world.x, candidate_world.y);
                if (score < selected_score) {
                    selected_index = index;
                    selected_score = score;
                }
            }
        }

        if (selected_index == kInvalidOwnerTransportQueueSlot) {
            return result;
        }

        OwnerRouteHelperCandidateCluster& cluster =
            candidate_set.clusters[selected_index];
        OwnerRouteHelperPlacementSearchResult placement =
            FindOwnerRouteHelperPlacementNearTile(map, cluster_average(cluster),
                cluster.terrain_group, helper_definition, 0x40,
                placement_predicate, user_data);
        placement.cluster_index = selected_index;
        placement.rejected_cluster_count = result.rejected_cluster_count;
        placement.score = selected_score;
        if (placement.found) {
            return placement;
        }

        ++result.rejected_cluster_count;
        if (result.rejected_cluster_count >= kOwnerRouteHelperPlacementRetryLimit) {
            return result;
        }
        cluster.blocked = true;
    }
}

UnitMovementUnit* FindNearestOwnerRouteHelperProducer(
    const UnitCommandContext& context, u32 owner_id, u32 producer_unit_type,
    UnitMovementPoint target_tile) {
    if (context.movement == nullptr) {
        return nullptr;
    }

    UnitMovementPoint target_world{
        static_cast<i32>(static_cast<u32>(target_tile.x) << 5),
        static_cast<i32>(static_cast<u32>(target_tile.y) << 5),
    };
    UnitMovementUnit* best = nullptr;
    u32 best_distance = 0xffffffff;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id ||
            unit->type_id != producer_unit_type ||
            (unit->runtime_flags & 0x80) != 0) {
            continue;
        }

        const u32 state = runtime_state(context, *unit);
        if (state != kUnitStateRuntimeIdleAcquire &&
            state != kUnitStateCommand08) {
            continue;
        }

        const u32 distance = CalculateApproxUnitDistance(unit->x, unit->y,
            target_world.x, target_world.y);
        if (distance < best_distance) {
            best_distance = distance;
            best = unit;
        }
    }
    return best;
}

OwnerRouteHelperDispatchResult DispatchOwnerRouteHelperProducer(
    UnitCommandContext& context, OwnerTransportQueueState& queue, u32 owner_id,
    u32 helper_unit_type, UnitMovementPoint target_tile, u32 next_route_index,
    u32 producer_unit_type) {
    OwnerRouteHelperDispatchResult result;
    result.target_tile = target_tile;
    result.target_world_point = {
        static_cast<i32>(static_cast<u32>(target_tile.x) << 5),
        static_cast<i32>(static_cast<u32>(target_tile.y) << 5),
    };

    const u32 resolved_producer_unit_type =
        producer_unit_type != kInvalidOwnerTransportQueueSlot
        ? producer_unit_type : helper_unit_type;
    result.producer_unit = FindNearestOwnerRouteHelperProducer(context, owner_id,
        resolved_producer_unit_type, target_tile);
    if (result.producer_unit == nullptr) {
        return result;
    }

    ReleaseUnitOwnerTransportQueueSlotReference(queue, *result.producer_unit);
    const u32 slot_index = AllocateOwnerTransportQueueSlot(queue,
        kOwnerTransportQueueStateRouteHelperPending);
    if (slot_index == kInvalidOwnerTransportQueueSlot) {
        return result;
    }

    OwnerTransportQueueSlot& slot = queue.slots[slot_index];
    slot.completed_count = 0;
    slot.phase_ticks = 0;
    slot.match_value = 0;
    slot.target_x = result.target_world_point.x;
    slot.target_y = result.target_world_point.y;
    slot.route_index = next_route_index;
    slot.aux_value = helper_unit_type;
    AssignUnitToOwnerTransportQueueSlot(queue, *result.producer_unit, slot_index);
    SetOrQueueUnitAlignedPointCommand06(result.producer_unit,
        helper_unit_type - kOwnerExtendedProductionUnitTypeBase,
        result.target_world_point.x, result.target_world_point.y, false);

    result.slot_index = slot_index;
    result.assigned = true;
    return result;
}

bool CheckOwnerTransportRouteTargetUnitType(u32 type_id) {
    return std::find(kOwnerTransportRouteTargetUnitTypes.begin(),
        kOwnerTransportRouteTargetUnitTypes.end(), type_id) !=
        kOwnerTransportRouteTargetUnitTypes.end();
}

u32 FindOwnerTransportRouteTargetIndex(
    const OwnerTransportRouteState& route_state, u32 match_value) {
    const u32 route_count = std::min<u32>(route_state.route_count,
        static_cast<u32>(route_state.targets.size()));
    for (u32 index = 0; index < route_count; ++index) {
        if (route_state.targets[index].target.match_value == match_value) {
            return index;
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

u32 AppendOwnerTransportRouteTargetIfMissing(
    OwnerTransportRouteState& route_state,
    const OwnerTransportRouteTarget& route_target) {
    const u32 match_value = route_target.target.match_value;
    if (match_value == 0 && route_target.unit == nullptr) {
        return kInvalidOwnerTransportQueueSlot;
    }

    if (route_target.unit != nullptr) {
        const u32 route_count = std::min<u32>(route_state.route_count,
            static_cast<u32>(route_state.targets.size()));
        for (u32 index = 0; index < route_count; ++index) {
            if (route_state.targets[index].unit == route_target.unit) {
                return index;
            }
        }
    }

    const u32 existing_index = FindOwnerTransportRouteTargetIndex(route_state,
        match_value);
    if (existing_index != kInvalidOwnerTransportQueueSlot) {
        return existing_index;
    }
    if (route_state.route_count >= route_state.targets.size()) {
        return kInvalidOwnerTransportQueueSlot;
    }

    const u32 new_index = route_state.route_count++;
    route_state.targets[new_index] = route_target;
    return new_index;
}

bool PromoteOwnerTransportQueueSlotToRouteWorkTarget(
    OwnerTransportQueueState& queue, u32 slot_index, u32 route_index,
    const OwnerTransportRouteState& route_state,
    const OwnerUnitTypeCounts& owner_unit_counts, u32 aux_unit_type,
    bool route_gate_open) {
    if (!route_gate_open || slot_index >= queue.slots.size() ||
        aux_unit_type >= owner_unit_counts.counts.size()) {
        return false;
    }

    if (route_index >= route_state.targets.size() ||
        route_index >= owner_unit_counts.counts[aux_unit_type]) {
        return false;
    }

    OwnerTransportQueueSlot& slot = queue.slots[slot_index];
    slot.state = kOwnerTransportQueueStateWorkTarget;
    SetOwnerTransportQueueSlotTarget(slot, route_state.targets[route_index].target);
    slot.route_index = route_index;
    return true;
}

OwnerTransportQueueLimitPlan BuildOwnerTransportWorkTargetLimitPlan(
    const OwnerTransportQueueState& queue,
    const OwnerTransportRouteState& route_state) {
    OwnerTransportQueueLimitPlan plan;
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state != kOwnerTransportQueueStateWorkTarget) {
            continue;
        }

        const u32 desired = CalculateOwnerTransportRouteDesiredSlotCount(
            route_state, slot.route_index);
        if (desired < slot.count && plan.action_count < plan.actions.size()) {
            plan.actions[plan.action_count++] = {
                static_cast<u32>(index), desired};
        }
    }
    return plan;
}

u32 CalculateOwnerTransportQueueSlotOverflow(
    const OwnerTransportQueueState& queue, u32 slot_index, u32 desired_count) {
    if (slot_index >= queue.slots.size()) {
        return 0;
    }

    const u32 count = queue.slots[slot_index].count;
    return count > desired_count ? count - desired_count : 0;
}

OwnerTransportQueueOverflowTarget FindOwnerTransportUnderfilledWorkTargetSlot(
    const OwnerTransportQueueState& queue,
    const OwnerTransportRouteState& route_state, u32 overflow_count,
    u32 overflow_percent) {
    OwnerTransportQueueOverflowTarget target;
    if (overflow_count == 0) {
        return target;
    }

    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state != kOwnerTransportQueueStateWorkTarget ||
            slot.match_value == 0) {
            continue;
        }

        const u32 desired = CalculateOwnerTransportRouteDesiredSlotCountWithPercent(
            route_state, slot.route_index, overflow_percent);
        if (slot.count < desired) {
            target.slot_index = static_cast<u32>(index);
            target.route_index = slot.route_index;
            target.move_count = std::min(overflow_count, slot.count);
        }
        break;
    }
    return target;
}

u32 FindOwnerTransportHighestPriorityOpenRouteTarget(
    const OwnerTransportRouteState& route_state) {
    u32 best_priority = 0;
    u32 best_index = kInvalidOwnerTransportQueueSlot;
    const u32 route_count = std::min<u32>(route_state.route_count,
        static_cast<u32>(route_state.targets.size()));
    for (u32 index = 0; index < route_count; ++index) {
        const OwnerTransportRouteTarget& target = route_state.targets[index];
        if ((target.flags & 2) != 0 ||
            (target.target.match_value == 0 && target.unit == nullptr)) {
            continue;
        }
        if (best_priority < target.priority) {
            best_priority = target.priority;
            best_index = index;
        }
    }
    return best_index;
}

u32 RedistributeOwnerTransportWorkTargetOverflow(
    UnitCommandContext& context, OwnerTransportQueueState& queue,
    OwnerTransportRouteState& route_state, u32 owner_id, u32 source_slot,
    u32 desired_count, u32 overflow_percent) {
    const u32 overflow_count =
        CalculateOwnerTransportQueueSlotOverflow(queue, source_slot, desired_count);
    if (overflow_count == 0) {
        return 0;
    }

    OwnerTransportQueueOverflowTarget target =
        FindOwnerTransportUnderfilledWorkTargetSlot(
            queue, route_state, overflow_count, overflow_percent);
    u32 destination_slot = target.slot_index;
    if (destination_slot == kInvalidOwnerTransportQueueSlot) {
        target.route_index = FindOwnerTransportHighestPriorityOpenRouteTarget(
            route_state);
        if (target.route_index == kInvalidOwnerTransportQueueSlot) {
            return 0;
        }

        OwnerTransportQueueTargetSnapshot snapshot =
            route_state.targets[target.route_index].target;
        snapshot.route_index = target.route_index;
        destination_slot = FindOrAllocateOwnerTransportQueueSlotByStateAndValue(
            queue, kOwnerTransportQueueStateWorkTarget, snapshot);
        if (destination_slot == kInvalidOwnerTransportQueueSlot) {
            return 0;
        }
        queue.slots[destination_slot].route_index = target.route_index;
        target.move_count = overflow_count;
    }

    return ReassignOwnerTransportUnitsBetweenQueueSlots(context, queue, owner_id,
        source_slot, destination_slot, std::min(overflow_count, target.move_count),
        true);
}

u32 BoardOwnerTransportPassengersFromLinkedGroup(UnitCommandContext& context,
    const OwnerTransportQueueState& queue, UnitMovementUnit& carrier,
    u32 carrier_slot_index) {
    if (context.movement == nullptr || carrier_slot_index >= queue.slots.size()) {
        return 0;
    }

    const u32 passenger_group = queue.slots[carrier_slot_index].linked_group;
    const u32 capacity = CalculateUnitTransportCapacity(carrier,
        context.production_state);
    u32 projected_cargo = carrier.cargo_amount;
    u32 queued_count = 0;
    for (UnitMovementUnit* passenger : context.movement->active_units) {
        if (projected_cargo >= capacity) {
            break;
        }
        if (passenger == nullptr || passenger == &carrier ||
            passenger->owner_id != carrier.owner_id) {
            continue;
        }
        if ((passenger->area_marker_flags & 0xffu) != passenger_group) {
            continue;
        }
        if ((passenger->definition.transport_flags & 4) == 0 ||
            (passenger->runtime_flags & 4) != 0) {
            continue;
        }
        if ((command_metadata_flags(context, *passenger) & 8u) != 0) {
            continue;
        }
        const u32 passenger_size = transport_size(*passenger);
        if (projected_cargo + passenger_size > capacity) {
            continue;
        }

        SetOrQueueUnitTargetCommand0a(passenger, &carrier, true);
        projected_cargo += passenger_size;
        ++queued_count;
    }
    return queued_count;
}

u32 FindOwnerTransportQueueSlotByStateAndValue(const OwnerTransportQueueState& queue,
    u32 state, u32 value) {
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state == state && slot.match_value == value) {
            return static_cast<u32>(index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

u32 AllocateOwnerTransportQueueSlot(OwnerTransportQueueState& queue, u32 state) {
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state == 0) {
            slot.state = state;
            slot.phase_ticks = 0;
            return static_cast<u32>(index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

void ReleaseOwnerTransportQueueSlotReference(OwnerTransportQueueState& queue,
    u32 slot_index) {
    if (slot_index >= queue.slots.size()) {
        return;
    }

    OwnerTransportQueueSlot& slot = queue.slots[slot_index];
    if (slot.state == 0) {
        return;
    }
    if (slot.count != 0) {
        --slot.count;
    }
    if (slot.count == 0) {
        slot.state = 0;
    }
}

void ReleaseUnitOwnerTransportQueueSlotReference(OwnerTransportQueueState& queue,
    const UnitMovementUnit& unit) {
    ReleaseOwnerTransportQueueSlotReference(
        queue, unit.area_marker_flags & 0xffu);
}

UnitMovementUnit* FindOwnerTransportQueueAssignedUnit(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 slot_index) {
    if (slot_index >= queue.slots.size() || context.movement == nullptr) {
        return nullptr;
    }
    if (queue.slots[slot_index].count == 0) {
        return nullptr;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr) {
            continue;
        }
        if (unit->owner_id != owner_id ||
            (unit->area_marker_flags & 0xffu) != slot_index) {
            continue;
        }
        if ((unit->runtime_flags & 4) == 0) {
            return unit;
        }
    }
    return nullptr;
}

namespace {

constexpr std::array<OwnerProductionDemandAlias, 14> kOwnerProductionDemandAliases{{
    {0x22, 0x23, 2},
    {0x25, 0x26, 2},
    {0x27, 0x2d, 2},
    {0x24, 0x2b, 1},
    {0x27, 0x2b, 1},
    {0x28, 0x2b, 1},
    {0x01, 0x0a, 1},
    {0x03, 0x0b, 1},
    {0x03, 0x0c, 1},
    {0x04, 0x0d, 1},
    {0x0f, 0x5b, 1},
    {0x5c, 0x5d, 1},
    {0x02, 0x5e, 1},
    {0x06, 0x5f, 1},
}};

void RemoveOwnerProductionDemand(OwnerUnitTypeCounts& demand, u32 unit_type,
    u32 amount) {
    if (unit_type >= demand.counts.size() || amount == 0) {
        return;
    }
    demand.counts[unit_type] =
        demand.counts[unit_type] > amount ? demand.counts[unit_type] - amount : 0;
}

bool IsCountableOwnerProductionUnit(const UnitCommandContext& context,
    const UnitMovementUnit* unit, u32 owner_id, u32 producer_unit_type,
    u32 command_state) {
    return unit != nullptr &&
        unit->owner_id == owner_id &&
        unit->type_id == producer_unit_type &&
        runtime_state(context, *unit) == command_state;
}

u32 FindFirstMissingOwnerProductionDependency(const OwnerUnitTypeCounts& counts,
    const std::array<u32, kOwnerProductionDependencyListSlots>& dependencies,
    u32 dependency_count) {
    const u32 count = std::min<u32>(dependency_count,
        static_cast<u32>(dependencies.size()));
    for (u32 index = 0; index < count; ++index) {
        const u32 unit_type = dependencies[index];
        if (unit_type == kInvalidOwnerTransportQueueSlot) {
            continue;
        }
        if (unit_type >= counts.counts.size() || counts.counts[unit_type] == 0) {
            return unit_type;
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

bool HasOwnerProductionUnitCount(const OwnerUnitTypeCounts& counts, u32 unit_type) {
    return unit_type < counts.counts.size() && counts.counts[unit_type] != 0;
}

bool IsReadyOwnerProductionProducer(UnitCommandContext& context,
    const UnitMovementUnit& unit, u32 owner_id, u32 producer_unit_type) {
    if (unit.owner_id != owner_id || unit.type_id != producer_unit_type ||
        unit.target != nullptr) {
        return false;
    }

    return command_metadata_flags(context, unit) == 1;
}

bool IsReadyOwnerProductionPairUnit(const UnitMovementUnit& unit, u32 owner_id,
    u32 unit_type) {
    if (unit.owner_id != owner_id || unit.type_id != unit_type) {
        return false;
    }

    return GetUnitCommandIdLow24(unit) == kUnitStateRuntimeIdleAcquire;
}

constexpr u32 kOwnerProductionPlacementGateBlockedCellMask = 0x40000700;
constexpr u32 kOwnerProductionPlacementTerrainValidFlag = 0x80000000;
constexpr u32 kOwnerProductionPlacementTerrainClassMask = 0x1c000000;
constexpr u32 kOwnerProductionPlacementTerrainClassShift = 26;
constexpr u32 kOwnerProductionPlacementIgnoreUnitRuntimeFlag = 0x80;
constexpr u32 kOwnerProductionPlacementSourceNoIgnoreType = 0x10;
constexpr u32 kOwnerProductionPlacementSpecialBypassType = 0x6a;
constexpr u32 kOwnerProductionPlacementNearbyProbeDistance = 0x400;
constexpr std::array<UnitMovementPoint, 9> kOwnerProductionDirectionDeltas = {{
    {0, 0}, {0, -1}, {1, -1}, {1, 0}, {1, 1},
    {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
}};
constexpr u32 kOwnerProductionPlacementDirectionRecordDwords = 7;
constexpr std::array<u32, 64> kOwnerProductionPlacementDirectionLayout = {{
    1, 1, 1, 1, 1, 1, 1,
    6, 4, 3, 7, 8, 2, 1,
    7, 5, 4, 8, 1, 3, 2,
    8, 6, 5, 1, 2, 4, 3,
    1, 7, 6, 2, 3, 5, 4,
    2, 8, 7, 3, 4, 6, 5,
    3, 1, 8, 4, 5, 7, 6,
    4, 2, 1, 5, 6, 8, 7,
    5, 3, 2, 6, 7, 1, 8,
    0,
}};

OwnerProductionPlacementGateResult MakeOwnerProductionPlacementGateBlock(
    OwnerProductionPlacementGateBlockReason reason, UnitMovementPoint tile) {
    OwnerProductionPlacementGateResult result;
    result.blocked = true;
    result.reason = reason;
    result.tile = tile;
    return result;
}

bool OwnerProductionTileInBounds(const UnitMovementMap& map,
    UnitMovementPoint tile) {
    return tile.x >= 0 && tile.y >= 0 &&
        static_cast<u32>(tile.x) < map.width &&
        static_cast<u32>(tile.y) < map.height;
}

bool FindOwnerProductionNearbyPassablePlacementTile(
    const UnitMovementMap& map, UnitMovementPoint tile) {
    for (i32 y = tile.y - 4; y <= tile.y + 4; ++y) {
        for (i32 x = tile.x - 4; x <= tile.x + 4; ++x) {
            if (x < 0 || y < 0) {
                continue;
            }
            const UnitMovementCell* cell = GetMovementCell(map,
                static_cast<u32>(x), static_cast<u32>(y));
            if (cell != nullptr &&
                (cell->flags & kMapCellTerrainMask) == kMapCellPassableTerrain) {
                return true;
            }
        }
    }
    return false;
}

bool CheckOwnerProductionPlacementActiveUnitCollision(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    UnitMovementPoint tile) {
    if (context.movement == nullptr) {
        return false;
    }

    const UnitMovementUnit* ignored_unit =
        source_unit != nullptr &&
            source_unit->type_id != kOwnerProductionPlacementSourceNoIgnoreType ?
        source_unit : nullptr;
    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit == ignored_unit ||
            (unit->runtime_flags & kOwnerProductionPlacementIgnoreUnitRuntimeFlag) != 0 ||
            unit->definition.movement_class == 3) {
            continue;
        }
        const i32 unit_tile_x = ConvertOwnerProductionWorldToTileSar(unit->x);
        const i32 unit_tile_y = ConvertOwnerProductionWorldToTileSar(unit->y);
        if (unit_tile_x == tile.x && unit_tile_y == tile.y) {
            return true;
        }
    }
    return false;
}

bool OwnerProductionPlacementPathProbePasses(
    const UnitCommandContext& context, const UnitMovementUnit& producer_unit,
    const OwnerProductionPlacementPathProbeRequest& request,
    OwnerProductionPlacementPathProbeCallback path_probe, void* user_data) {
    if (path_probe == nullptr) {
        return true;
    }
    return path_probe(context, producer_unit, request, user_data);
}

bool OwnerProductionPlacementDefinitionRequiresPathProbe(
    const UnitMovementDefinition& definition) {
    return definition.placement_path_reference_count != 0 ||
        definition.placement_small_reference_count != 0;
}

bool OwnerProductionNearbyPlacementUnitRequiresPathProbe(
    const UnitCommandContext& context, const UnitMovementDefinition& definition) {
    if (definition.placement_small_reference_count != 0) {
        return true;
    }

    const u32 reference_count = std::min<u32>(
        definition.placement_path_reference_count,
        static_cast<u32>(definition.placement_path_reference_type_ids.size()));
    for (u32 index = 0; index < reference_count; ++index) {
        const u32 reference_type =
            definition.placement_path_reference_type_ids[index];
        const UnitMovementDefinition* reference_definition = nullptr;
        if (context.callbacks.find_definition != nullptr) {
            UnitCommandContext& mutable_context =
                const_cast<UnitCommandContext&>(context);
            reference_definition =
                context.callbacks.find_definition(mutable_context,
                    reference_type);
        }
        const u32 movement_class = reference_definition != nullptr ?
            reference_definition->movement_class : 0xffffffffu;
        if (movement_class != 1 && movement_class != 3) {
            return true;
        }
    }
    return false;
}

bool DefaultOwnerProductionNearbyPlacementProbePredicate(
    const UnitCommandContext& context, const UnitMovementUnit& unit) {
    return unit.definition.lifecycle_class == 2 &&
        OwnerProductionNearbyPlacementUnitRequiresPathProbe(context,
            unit.definition);
}

bool IsIgnoredOwnerProductionRouteUnit(const UnitMovementUnit& unit,
    const std::vector<const UnitMovementUnit*>* ignored_route_units) {
    return ignored_route_units != nullptr &&
        std::find(ignored_route_units->begin(), ignored_route_units->end(),
            &unit) != ignored_route_units->end();
}

UnitMovementPoint OwnerProductionTileToWorldPoint(i32 tile_x, i32 tile_y) {
    return UnitMovementPoint{static_cast<i32>(static_cast<u32>(tile_x) << 5),
        static_cast<i32>(static_cast<u32>(tile_y) << 5)};
}

u32 ReadOwnerProductionPlacementDirectionLayout(u32 direction, u32 field_index) {
    const u32 index =
        direction * kOwnerProductionPlacementDirectionRecordDwords + field_index;
    if (index >= kOwnerProductionPlacementDirectionLayout.size()) {
        return 0;
    }
    return kOwnerProductionPlacementDirectionLayout[index];
}

UnitMovementPoint OffsetOwnerProductionTile(UnitMovementPoint tile,
    u32 direction, i32 steps) {
    if (direction >= kOwnerProductionDirectionDeltas.size()) {
        return tile;
    }

    const UnitMovementPoint delta = kOwnerProductionDirectionDeltas[direction];
    tile.x += delta.x * steps;
    tile.y += delta.y * steps;
    return tile;
}

} // namespace

void AddOwnerProductionDemand(OwnerUnitTypeCounts& demand, u32 unit_type,
    u32 amount) {
    if (unit_type >= demand.counts.size() || amount == 0) {
        return;
    }
    demand.counts[unit_type] += amount;
}

u32 CalculateOwnerResourceBudgetUnitDemand(u32 resource_budget,
    u32 build_percent, u32 cap_base, u32 unit_cost) {
    if (unit_cost == 0) {
        return 0;
    }

    u32 budget = static_cast<u32>(
        (static_cast<unsigned long long>(resource_budget) * build_percent) / 100);
    budget = std::min(budget, cap_base + 10);
    u32 demand = budget / unit_cost;
    if ((budget % unit_cost) != 0) {
        ++demand;
    }
    return demand;
}

void AddOwnerTargetCompositionDemand(OwnerUnitTypeCounts& bonus_demand,
    const OwnerUnitTypeCounts& target_owner_counts,
    const std::array<OwnerProductionDemandRule, kOwnerUnitTypeCountSlots>& rules,
    u32 percent_bonus) {
    for (std::size_t source_type = 0; source_type < 0x60; ++source_type) {
        const u32 target_count = target_owner_counts.counts[source_type];
        if (target_count == 0) {
            continue;
        }

        const OwnerProductionDemandRule& rule = rules[source_type];
        for (std::size_t index = 0; index < rule.unit_types.size(); ++index) {
            const u32 unit_type = rule.unit_types[index];
            if (unit_type == kInvalidOwnerTransportQueueSlot ||
                unit_type >= bonus_demand.counts.size()) {
                continue;
            }

            const u32 amount = static_cast<u32>(
                (static_cast<unsigned long long>(target_count) *
                    (rule.percents[index] + percent_bonus)) / 100);
            AddOwnerProductionDemand(bonus_demand, unit_type, amount);
        }
    }
}

void AddOwnerPrimaryAndCarrierDemand(OwnerProductionDemandState& demand_state,
    u32 primary_unit_type, const OwnerUnitTypeCounts& target_owner_counts,
    u32 carrier_unit_type, u32 carrier_deficit, u32 existing_carrier_count) {
    AddOwnerProductionDemand(demand_state.bonus_demand, primary_unit_type,
        CalculateOwnerPrimaryUnitDesiredCountFromTargetOwner(target_owner_counts));
    AddOwnerProductionDemand(demand_state.bonus_demand, carrier_unit_type,
        carrier_deficit + existing_carrier_count);
}

void ApplyOwnerProductionDemandAliases(OwnerUnitTypeCounts& demand) {
    for (const OwnerProductionDemandAlias& alias : kOwnerProductionDemandAliases) {
        if (alias.source_unit_type >= demand.counts.size()) {
            continue;
        }

        AddOwnerProductionDemand(demand, alias.target_unit_type,
            demand.counts[alias.source_unit_type] * alias.multiplier);
    }
}

void RemoveOwnerProductionDemandAliases(OwnerUnitTypeCounts& demand) {
    for (const OwnerProductionDemandAlias& alias : kOwnerProductionDemandAliases) {
        if (alias.source_unit_type >= demand.counts.size()) {
            continue;
        }

        RemoveOwnerProductionDemand(demand, alias.target_unit_type,
            demand.counts[alias.source_unit_type] * alias.multiplier);
    }
}

u32 ResolveQueuedOwnerProductionUnitType(const UnitMovementUnit& unit) {
    const u32 command_id = GetUnitCommandIdLow24(unit);
    const u32 active_entry_id =
        unit.active_command_payload.state & kUnitCommandStateMask;
    if (command_id == kUnitStateProductionSpawnStart ||
        command_id == kUnitStateProductionSpawnCycle) {
        // The active raw 0x10 command payload is authoritative.  The cached
        // fields survive until completion and otherwise make a producer's
        // second order reuse the first order's type (and therefore its cost).
        return active_entry_id == kUnitStateCommand10 ?
            static_cast<u32>(unit.active_command_payload.x) : unit.command_value;
    }
    if (unit.queued_production_type_id != 0) {
        return unit.queued_production_type_id;
    }
    if (unit.spawn_type_id != 0) {
        return unit.spawn_type_id;
    }
    if (unit.active_command_payload.x != 0) {
        return static_cast<u32>(unit.active_command_payload.x);
    }
    if (unit.command_value != 0) {
        return unit.command_value;
    }
    return unit.type_id;
}

u32 CountOwnerQueuedPrimaryProductionUnits(
    const UnitCommandContext& context, u32 owner_id, u32 unit_type,
    const std::array<u32, kOwnerUnitTypeCountSlots>& producer_unit_types) {
    if (context.movement == nullptr || unit_type >= producer_unit_types.size()) {
        return 0;
    }

    const u32 producer_unit_type = producer_unit_types[unit_type];
    u32 count = 0;
    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (!IsCountableOwnerProductionUnit(context, unit, owner_id, producer_unit_type,
                kUnitStateCommand10)) {
            continue;
        }
        if (unit->queued_production_type_id == unit_type) {
            ++count;
        }
    }
    return count;
}

u32 CountOwnerQueuedExtendedProductionUnits(
    const UnitCommandContext& context, u32 owner_id, u32 unit_type,
    const std::array<u32, kOwnerUnitTypeCountSlots>& producer_unit_types,
    u32 extended_type_base) {
    if (context.movement == nullptr || unit_type >= producer_unit_types.size()) {
        return 0;
    }

    const u32 producer_unit_type = producer_unit_types[unit_type];
    u32 count = 0;
    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (!IsCountableOwnerProductionUnit(
                context, unit, owner_id, producer_unit_type, 7)) {
            continue;
        }
        if (unit->extended_production_type_index + extended_type_base == unit_type) {
            ++count;
        }
    }
    return count;
}

OwnerProductionBuildActionResult SelectOwnerProductionDependencyBuildAction(
    UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts,
    const OwnerProductionDependencyRequest& request,
    UnitMovementUnit* producer_cursor,
    u32 reserved_resource_cost) {
    OwnerProductionBuildActionResult result;
    if (request.direct_dependency_unit_type != kInvalidOwnerTransportQueueSlot) {
        result.action = OwnerProductionBuildAction::resolve_transport_dependency;
        result.dependency_unit_type = request.direct_dependency_unit_type;
        return result;
    }

    const bool special_pair_type = request.unit_type == 0x23 ||
        request.unit_type == 0x26 || request.unit_type == 0x2d;
    if (special_pair_type) {
        const u32 missing = FindFirstMissingOwnerProductionDependency(
            owner_unit_counts, request.special_dependency_unit_types,
            request.special_dependency_count);
        if (missing != kInvalidOwnerTransportQueueSlot) {
            result.action = OwnerProductionBuildAction::build_missing_prerequisite;
            result.dependency_unit_type = missing;
            return result;
        }

        result.action = OwnerProductionBuildAction::run_special_pairing;
        return result;
    }

    if (request.unit_type == 0x2b) {
        if (!request.unlock_dependency_available) {
            result.action = OwnerProductionBuildAction::unlock_missing_dependency;
            result.dependency_unit_type = request.unlock_dependency_unit_type;
            return result;
        }

        result.action = OwnerProductionBuildAction::run_special_pairing;
        return result;
    }

    if (!HasOwnerProductionUnitCount(owner_unit_counts,
            request.producer_unit_type)) {
        result.action = OwnerProductionBuildAction::missing_producer_unit;
        result.dependency_unit_type = request.producer_unit_type;
        return result;
    }

    if (owner_id < context.owner_resources.size()) {
        const u32 available = context.owner_resources[owner_id] >
                reserved_resource_cost ?
            context.owner_resources[owner_id] - reserved_resource_cost : 0;
        if (request.resource_cost > available) {
            result.action =
                OwnerProductionBuildAction::blocked_by_resource_budget;
            return result;
        }
    }

    const u32 missing = FindFirstMissingOwnerProductionDependency(
        owner_unit_counts, request.prerequisite_unit_types,
        request.prerequisite_count);
    if (missing != kInvalidOwnerTransportQueueSlot) {
        result.action = OwnerProductionBuildAction::build_missing_prerequisite;
        result.dependency_unit_type = missing;
        return result;
    }

    if (context.movement == nullptr) {
        return result;
    }

    bool reached_cursor = producer_cursor == nullptr;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr) {
            continue;
        }
        if (!reached_cursor) {
            reached_cursor = unit == producer_cursor;
            if (!reached_cursor) {
                continue;
            }
        }
        if (!IsReadyOwnerProductionProducer(context, *unit, owner_id,
                request.producer_unit_type)) {
            continue;
        }

        result.action = OwnerProductionBuildAction::use_producer_unit;
        result.producer_unit = unit;
        return result;
    }
    return result;
}

void QueueOwnerProductionLinkCommand(UnitMovementUnit& unit,
    UnitMovementUnit& target) {
    SetUnitCommandTarget(unit, &target);
    unit.active_command_payload.state = 0x0b;
    unit.active_command_payload.x = static_cast<i32>(target.id);
    unit.active_command_payload.y = target.x;
    unit.active_command_payload.value = static_cast<u32>(target.y);
}

bool LinkOwnerProductionPairUnits(
    UnitCommandContext& context, u32 owner_id, u32 pair_unit_type) {
    if (context.movement == nullptr) {
        return false;
    }

    for (std::size_t source_index = 0;
         source_index < context.movement->active_units.size(); ++source_index) {
        UnitMovementUnit* source = context.movement->active_units[source_index];
        if (source == nullptr ||
            !IsReadyOwnerProductionPairUnit(*source, owner_id, pair_unit_type)) {
            continue;
        }

        UnitMovementUnit* partner = nullptr;
        u32 best_distance = 0xffffffffu;
        for (std::size_t index = source_index + 1;
             index < context.movement->active_units.size(); ++index) {
            UnitMovementUnit* candidate = context.movement->active_units[index];
            if (candidate == nullptr ||
                !IsReadyOwnerProductionPairUnit(*candidate, owner_id,
                    pair_unit_type)) {
                continue;
            }
            const u32 distance = CalculateApproxUnitDistance(source->x, source->y,
                candidate->x, candidate->y);
            if (distance < best_distance) {
                best_distance = distance;
                partner = candidate;
            }
        }

        if (partner == nullptr) {
            return false;
        }

        QueueOwnerProductionLinkCommand(*source, *partner);
        QueueOwnerProductionLinkCommand(*partner, *source);
        return true;
    }
    return false;
}

bool LinkOwnerProductionTriadUnits(UnitCommandContext& context, u32 owner_id,
    u32 source_unit_type, u32 first_partner_type, u32 second_partner_type) {
    if (context.movement == nullptr) {
        return false;
    }

    for (UnitMovementUnit* source : context.movement->active_units) {
        if (source == nullptr ||
            !IsReadyOwnerProductionPairUnit(*source, owner_id, source_unit_type)) {
            continue;
        }

        UnitMovementUnit* first_partner = nullptr;
        UnitMovementUnit* second_partner = nullptr;
        u32 first_distance = 0xffffffffu;
        u32 second_distance = 0xffffffffu;
        for (UnitMovementUnit* candidate : context.movement->active_units) {
            if (candidate == nullptr ||
                candidate == source ||
                candidate->owner_id != owner_id ||
                GetUnitCommandIdLow24(*candidate) !=
                    kUnitStateRuntimeIdleAcquire) {
                continue;
            }

            if (candidate->type_id == first_partner_type) {
                const u32 distance = CalculateApproxUnitDistance(source->x,
                    source->y, candidate->x, candidate->y);
                if (distance < first_distance) {
                    first_distance = distance;
                    first_partner = candidate;
                }
            }
            else if (candidate->type_id == second_partner_type) {
                const u32 distance = CalculateApproxUnitDistance(source->x,
                    source->y, candidate->x, candidate->y);
                if (distance < second_distance) {
                    second_distance = distance;
                    second_partner = candidate;
                }
            }
        }

        if (first_partner == nullptr || second_partner == nullptr) {
            return false;
        }

        QueueOwnerProductionLinkCommand(*source, *first_partner);
        QueueOwnerProductionLinkCommand(*first_partner, *second_partner);
        QueueOwnerProductionLinkCommand(*second_partner, *source);
        return true;
    }
    return false;
}

bool RunOwnerProductionSpecialPairing(
    UnitCommandContext& context, u32 owner_id, u32 demanded_unit_type) {
    switch (demanded_unit_type) {
    case 0x23:
        return LinkOwnerProductionPairUnits(context, owner_id, 0x22);
    case 0x26:
        return LinkOwnerProductionPairUnits(context, owner_id, 0x25);
    case 0x2b:
        return LinkOwnerProductionTriadUnits(context, owner_id, 0x28, 0x24, 0x27);
    case 0x2d:
        return LinkOwnerProductionPairUnits(context, owner_id, 0x27);
    default:
        return false;
    }
}

i32 ConvertOwnerProductionWorldToTile(i32 world_coord) {
    return world_coord / 32;
}

i32 ConvertOwnerProductionWorldToTileShr(i32 world_coord) {
    return static_cast<i32>(static_cast<u32>(world_coord) >> 5);
}

i32 ConvertOwnerProductionWorldToTileSar(i32 world_coord) {
    if (world_coord >= 0) {
        return world_coord / 32;
    }
    return -((31 - world_coord) / 32);
}

bool OwnerProductionPlacementTargetTemporarilyBlocked(
    const UnitMovementMap& map, UnitMovementPoint target_point) {
    if (target_point.x < 0 || target_point.y < 0) {
        return false;
    }

    const UnitMovementPoint target_tile{
        ConvertOwnerProductionWorldToTile(target_point.x),
        ConvertOwnerProductionWorldToTile(target_point.y),
    };
    if (!OwnerProductionTileInBounds(map, target_tile)) {
        return false;
    }

    const UnitMovementCell* cell = GetMovementCell(map,
        static_cast<u32>(target_tile.x), static_cast<u32>(target_tile.y));
    return cell != nullptr &&
        (cell->visibility_flags & kOwnerProductionPlacementTemporaryBlockFlag) != 0;
}

bool ApplyOwnerProductionPlacementFootprintOverlay(UnitMovementMap& map,
    OwnerProductionPlacementFootprintOverlay& overlay,
    UnitMovementPoint origin_world_point, u32 width, u32 height) {
    if (overlay.applied || width == 0 || height == 0 ||
        width > kOwnerProductionPlacementMaxFootprintWidth ||
        height > kOwnerProductionPlacementMaxFootprintHeight) {
        return false;
    }

    const i32 origin_tile_x =
        ConvertOwnerProductionWorldToTile(origin_world_point.x);
    const i32 origin_tile_y =
        ConvertOwnerProductionWorldToTile(origin_world_point.y);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const i32 tile_x = origin_tile_x + static_cast<i32>(x);
            const i32 tile_y = origin_tile_y + static_cast<i32>(y);
            if (tile_x < 0 || tile_y < 0 ||
                GetMovementCell(map, static_cast<u32>(tile_x),
                    static_cast<u32>(tile_y)) == nullptr) {
                return false;
            }
        }
    }

    overlay.origin_tile = {origin_tile_x, origin_tile_y};
    overlay.width = width;
    overlay.height = height;
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            UnitMovementCell* cell = GetMovementCell(map,
                static_cast<u32>(origin_tile_x + static_cast<i32>(x)),
                static_cast<u32>(origin_tile_y + static_cast<i32>(y)));
            const u32 index = y * kOwnerProductionPlacementMaxFootprintWidth + x;
            overlay.original_visibility_flags[index] = cell->visibility_flags;
            cell->visibility_flags |= kOwnerProductionPlacementTemporaryBlockFlag;
        }
    }
    overlay.applied = true;
    return true;
}

void RestoreOwnerProductionPlacementFootprintOverlay(UnitMovementMap& map,
    OwnerProductionPlacementFootprintOverlay& overlay) {
    if (!overlay.applied) {
        return;
    }

    for (u32 y = 0; y < overlay.height; ++y) {
        for (u32 x = 0; x < overlay.width; ++x) {
            UnitMovementCell* cell = GetMovementCell(map,
                static_cast<u32>(overlay.origin_tile.x + static_cast<i32>(x)),
                static_cast<u32>(overlay.origin_tile.y + static_cast<i32>(y)));
            if (cell == nullptr) {
                continue;
            }

            const u32 index = y * kOwnerProductionPlacementMaxFootprintWidth + x;
            cell->visibility_flags = overlay.original_visibility_flags[index];
        }
    }
    overlay.applied = false;
}

u32 GetOwnerProductionPlacementTerrainClass(const UnitMovementMap& map,
    UnitMovementPoint tile) {
    const UnitMovementCell* cell = GetMovementCell(map,
        static_cast<u32>(tile.x), static_cast<u32>(tile.y));
    if (!OwnerProductionTileInBounds(map, tile) || cell == nullptr) {
        return 3;
    }
    const u32 placement_layer = cell->alternate_flags;
    return (placement_layer & kOwnerProductionPlacementTerrainClassMask) >>
        kOwnerProductionPlacementTerrainClassShift;
}

OwnerProductionPlacementGateResult CheckOwnerProductionPlacementGateCell(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    UnitMovementPoint tile, u32 terrain_class, bool route_target_type) {
    if (context.movement == nullptr) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::out_of_bounds, tile);
    }

    const UnitMovementMap& map = context.movement->map;
    const UnitMovementCell* cell = GetMovementCell(map,
        static_cast<u32>(tile.x), static_cast<u32>(tile.y));
    if (!OwnerProductionTileInBounds(map, tile) || cell == nullptr) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::out_of_bounds, tile);
    }
    if ((cell->flags & kOwnerProductionPlacementGateBlockedCellMask) != 0 ||
        (cell->visibility_flags & kOwnerProductionPlacementTemporaryBlockFlag) !=
            0) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::blocked_cell, tile);
    }
    const u32 placement_layer = cell->alternate_flags;
    if ((placement_layer & kOwnerProductionPlacementTerrainValidFlag) == 0) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::blocked_cell, tile);
    }
    const u32 cell_terrain_class =
        (placement_layer & kOwnerProductionPlacementTerrainClassMask) >>
        kOwnerProductionPlacementTerrainClassShift;
    if (cell_terrain_class != terrain_class) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::terrain_class_mismatch,
            tile);
    }
    if (route_target_type &&
        FindOwnerProductionNearbyPassablePlacementTile(map, tile)) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::route_target_near_passable_tile,
            tile);
    }
    if (CheckOwnerProductionPlacementActiveUnitCollision(context, source_unit,
            tile)) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::active_unit_collision, tile);
    }
    return {};
}

OwnerProductionPlacementGateResult CheckOwnerProductionPlacementCandidateGate(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint world_point) {
    if (context.movement == nullptr || world_point.x < 0 || world_point.y < 0) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::out_of_bounds, {-1, -1});
    }

    const UnitMovementMap& map = context.movement->map;
    const UnitMovementPoint origin_tile{
        ConvertOwnerProductionWorldToTileSar(world_point.x),
        ConvertOwnerProductionWorldToTileSar(world_point.y),
    };
    if (!OwnerProductionTileInBounds(map, origin_tile)) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::out_of_bounds, origin_tile);
    }
    if (placement_definition.footprint_width_tiles == 0 ||
        placement_definition.footprint_height_tiles == 0) {
        return MakeOwnerProductionPlacementGateBlock(
            OwnerProductionPlacementGateBlockReason::missing_definition,
            origin_tile);
    }

    const u32 terrain_class =
        GetOwnerProductionPlacementTerrainClass(map, origin_tile);
    const bool route_target_type = CheckOwnerTransportRouteTargetUnitType(unit_type);
    for (u32 y = 0; y < placement_definition.footprint_height_tiles; ++y) {
        for (u32 x = 0; x < placement_definition.footprint_width_tiles; ++x) {
            const UnitMovementPoint tile{
                origin_tile.x + static_cast<i32>(x),
                origin_tile.y + static_cast<i32>(y),
            };
            OwnerProductionPlacementGateResult gate =
                CheckOwnerProductionPlacementGateCell(context, source_unit, tile,
                    terrain_class, route_target_type);
            if (gate.blocked) {
                return gate;
            }
        }
    }
    return {};
}

OwnerProductionPlacementGateResult CheckOwnerProductionPlacementFootprintGateCells(
    const UnitCommandContext& context, const UnitMovementUnit* source_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint world_point) {
    return CheckOwnerProductionPlacementCandidateGate(context, source_unit,
        placement_definition, unit_type, world_point);
}

UnitMovementPoint CalculateOwnerProductionPlacementPathProbeTile(
    UnitMovementPoint world_point, const UnitMovementDefinition& definition) {
    return {
        ConvertOwnerProductionWorldToTile(
            world_point.x + definition.transport_offset_x),
        ConvertOwnerProductionWorldToTile(
            world_point.y + definition.transport_offset_y),
    };
}

UnitMovementPoint CalculateOwnerProductionNearbyPathProbeTile(
    UnitMovementPoint world_point, const UnitMovementDefinition& definition) {
    return {
        ConvertOwnerProductionWorldToTileShr(
            world_point.x + definition.transport_offset_x),
        ConvertOwnerProductionWorldToTileShr(
            world_point.y + definition.transport_offset_y),
    };
}

OwnerProductionPlacementPathAvailabilityResult
CheckOwnerProductionPlacementPathProbeAvailability(
    const UnitCommandContext& context, u32 owner_id,
    const UnitMovementUnit& producer_unit,
    const UnitMovementDefinition& placement_definition, u32 unit_type,
    UnitMovementPoint origin_world_point, UnitMovementPoint owner_target_point,
    bool direct_probe_required,
    OwnerProductionPlacementPathProbeCallback path_probe,
    void* user_data,
    OwnerProductionPlacementNearbyProbePredicate nearby_probe_predicate,
    const std::vector<const UnitMovementUnit*>* ignored_route_units,
    OwnerProductionPlacementTargetRefreshCallback target_refresh,
    void* target_refresh_user_data,
    const OwnerTransportRouteState* route_state) {
    OwnerProductionPlacementPathAvailabilityResult result;
    result.target_point = owner_target_point;

    OwnerProductionPlacementFootprintOverlay overlay;
    UnitMovementMap* map =
        context.movement != nullptr ? &context.movement->map : nullptr;
    if (map != nullptr) {
        if (!ApplyOwnerProductionPlacementFootprintOverlay(*map, overlay,
                origin_world_point, placement_definition.footprint_width_tiles,
                placement_definition.footprint_height_tiles)) {
            return result;
        }
        result.footprint_overlay_applied = true;
    }

    auto finish = [&](OwnerProductionPlacementPathAvailabilityResult value) {
        if (map != nullptr) {
            RestoreOwnerProductionPlacementFootprintOverlay(*map, overlay);
        }
        return value;
    };

    if (unit_type == kOwnerProductionPlacementSpecialBypassType) {
        result.available = true;
        result.special_type_bypass = true;
        return result;
    }

    if (map != nullptr &&
        OwnerProductionPlacementTargetTemporarilyBlocked(*map,
            owner_target_point)) {
        result.target_refresh_checked = true;
        if (target_refresh != nullptr && route_state != nullptr) {
            OwnerProductionPlacementTargetRefreshRequest request;
            request.owner_id = owner_id;
            request.route_state = route_state;
            request.current_target_point = owner_target_point;
            UnitMovementPoint refreshed_target = owner_target_point;
            if (target_refresh(context, producer_unit, request,
                    refreshed_target, target_refresh_user_data)) {
                result.target_refreshed =
                    refreshed_target.x != owner_target_point.x ||
                    refreshed_target.y != owner_target_point.y;
                owner_target_point = refreshed_target;
                result.target_point = refreshed_target;
            }
        }
    }

    if (direct_probe_required) {
        OwnerProductionPlacementPathProbeRequest request;
        request.start_tile =
            CalculateOwnerProductionPlacementPathProbeTile(
                origin_world_point, placement_definition);
        request.target_point = owner_target_point;
        request.distance = CalculateApproxUnitDistance(request.start_tile.x,
            request.start_tile.y, owner_target_point.x, owner_target_point.y);
        result.direct_probe_checked = true;
        if (!OwnerProductionPlacementPathProbePasses(
                context, producer_unit, request, path_probe, user_data)) {
            result.failed_start_tile = request.start_tile;
            return finish(result);
        }
    }

    if (context.movement == nullptr) {
        result.available = true;
        return finish(result);
    }

    for (const UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id ||
            IsIgnoredOwnerProductionRouteUnit(*unit, ignored_route_units)) {
            continue;
        }
        const bool should_probe = nearby_probe_predicate != nullptr ?
            nearby_probe_predicate(*unit, user_data) :
            DefaultOwnerProductionNearbyPlacementProbePredicate(context, *unit);
        if (!should_probe) {
            continue;
        }

        OwnerProductionPlacementPathProbeRequest request;
        request.related_unit = unit;
        request.start_tile = CalculateOwnerProductionNearbyPathProbeTile(
            {unit->x, unit->y}, unit->definition);
        request.target_point = owner_target_point;
        request.distance = CalculateApproxUnitDistance(request.start_tile.x,
            request.start_tile.y, owner_target_point.x, owner_target_point.y);
        if (request.distance >= kOwnerProductionPlacementNearbyProbeDistance) {
            continue;
        }

        ++result.nearby_probe_count;
        if (!OwnerProductionPlacementPathProbePasses(
                context, producer_unit, request, path_probe, user_data)) {
            result.failed_unit = unit;
            result.failed_start_tile = request.start_tile;
            return finish(result);
        }
    }

    result.available = true;
    return finish(result);
}

OwnerProductionPlacementSearchResult FindOwnerProductionPlacementPointSpiral(
    UnitMovementPoint start_tile,
    OwnerProductionPlacementCandidatePredicate predicate,
    void* user_data, u32 max_rings) {
    OwnerProductionPlacementSearchResult result;
    if (predicate == nullptr) {
        return result;
    }

    i32 tile_x = start_tile.x;
    i32 tile_y = start_tile.y;
    u32 run_length = 0;
    const auto try_candidate = [&]() {
        const UnitMovementPoint world_point{tile_x * 32, tile_y * 32};
        if (!predicate(world_point, user_data)) {
            return false;
        }

        result.point = world_point;
        result.found = true;
        return true;
    };

    for (u32 ring = 0; ring < max_rings; ++ring) {
        ++run_length;
        for (u32 step = 0; step < run_length; ++step) {
            ++tile_x;
            if (try_candidate()) {
                return result;
            }
        }
        for (u32 step = 0; step < run_length; ++step) {
            ++tile_y;
            if (try_candidate()) {
                return result;
            }
        }

        ++run_length;
        for (u32 step = 0; step < run_length; ++step) {
            --tile_x;
            if (try_candidate()) {
                return result;
            }
        }
        for (u32 step = 0; step < run_length; ++step) {
            --tile_y;
            if (try_candidate()) {
                return result;
            }
        }
    }
    return result;
}

u32 MeasureOwnerProductionPlacementBlockedSpiralScore(
    UnitMovementPoint start_tile,
    OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data, u32 max_spiral_sides) {
    if (blocked_predicate == nullptr || max_spiral_sides == 0) {
        return 0;
    }

    i32 tile_x = start_tile.x;
    i32 tile_y = start_tile.y;
    u32 completed_sides = 0;
    u32 run_length = 0;
    const auto blocked = [&]() {
        return blocked_predicate(
            OwnerProductionTileToWorldPoint(tile_x, tile_y), user_data);
    };
    const auto finish_side = [&]() {
        if (completed_sides != max_spiral_sides) {
            ++completed_sides;
        }
        return completed_sides == max_spiral_sides;
    };

    while (completed_sides < max_spiral_sides) {
        ++run_length;
        for (u32 step = 0; step < run_length; ++step) {
            ++tile_x;
            if (blocked()) {
                return completed_sides;
            }
        }
        if (finish_side()) {
            return completed_sides;
        }

        for (u32 step = 0; step < run_length; ++step) {
            ++tile_y;
            if (blocked()) {
                return completed_sides;
            }
        }
        if (finish_side()) {
            return completed_sides;
        }

        ++run_length;
        for (u32 step = 0; step < run_length; ++step) {
            --tile_x;
            if (blocked()) {
                return completed_sides;
            }
        }
        if (finish_side()) {
            return completed_sides;
        }

        for (u32 step = 0; step < run_length; ++step) {
            --tile_y;
            if (blocked()) {
                return completed_sides;
            }
        }
        if (finish_side()) {
            return completed_sides;
        }
    }
    return completed_sides;
}

OwnerProductionPlacementClearanceResult
SelectOwnerProductionForwardClearancePoint(UnitMovementPoint start_tile,
    u32 direction, OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data, u32 forward_steps, u32 max_spiral_sides) {
    OwnerProductionPlacementClearanceResult result;
    if (blocked_predicate == nullptr ||
        direction >= kOwnerProductionDirectionDeltas.size()) {
        return result;
    }

    const UnitMovementPoint delta = kOwnerProductionDirectionDeltas[direction];
    i32 tile_x = start_tile.x;
    i32 tile_y = start_tile.y;
    for (u32 step = 0; step < forward_steps; ++step) {
        tile_x += delta.x;
        tile_y += delta.y;
        const u32 score = MeasureOwnerProductionPlacementBlockedSpiralScore(
            UnitMovementPoint{tile_x, tile_y}, blocked_predicate, user_data,
            max_spiral_sides);
        if (result.score < score) {
            result.point = UnitMovementPoint{tile_x, tile_y};
            result.score = score;
            result.found = true;
        }
    }
    return result;
}

OwnerProductionPlacementAnchorSet BuildOwnerProductionPlacementAnchorSet(
    UnitMovementPoint base_tile, UnitMovementPoint source_center_tile,
    u32 direction, OwnerProductionPlacementCandidatePredicate blocked_predicate,
    void* user_data) {
    OwnerProductionPlacementAnchorSet anchors;
    anchors.base_tile = base_tile;
    anchors.source_center_tile = source_center_tile;
    anchors.placement_class_1_points = {base_tile, base_tile};
    anchors.placement_class_2_points = {
        source_center_tile, source_center_tile, source_center_tile};
    anchors.placement_class_3_points = {base_tile, base_tile};
    anchors.placement_class_4_points = {
        source_center_tile, source_center_tile, source_center_tile};
    anchors.placement_class_5_point = base_tile;

    const auto apply_clearance = [&](UnitMovementPoint& point, u32 field_index) {
        const u32 clearance_direction =
            ReadOwnerProductionPlacementDirectionLayout(direction, field_index);
        const OwnerProductionPlacementClearanceResult clearance =
            SelectOwnerProductionForwardClearancePoint(source_center_tile,
                clearance_direction, blocked_predicate, user_data);
        if (clearance.score != 0) {
            point = clearance.point;
        }
    };

    apply_clearance(anchors.placement_class_1_points[0], 2);
    apply_clearance(anchors.placement_class_1_points[1], 3);

    apply_clearance(anchors.placement_class_2_points[0], 6);
    apply_clearance(anchors.placement_class_2_points[1], 5);
    apply_clearance(anchors.placement_class_2_points[2], 4);

    anchors.placement_class_3_points = anchors.placement_class_1_points;
    anchors.placement_class_4_points[0] = OffsetOwnerProductionTile(
        source_center_tile,
        ReadOwnerProductionPlacementDirectionLayout(direction, 0), 4);
    anchors.placement_class_4_points[1] = OffsetOwnerProductionTile(
        source_center_tile,
        ReadOwnerProductionPlacementDirectionLayout(direction, 1), 4);
    anchors.placement_class_4_points[1] = OffsetOwnerProductionTile(
        anchors.placement_class_4_points[1],
        ReadOwnerProductionPlacementDirectionLayout(direction, 7), 7);
    return anchors;
}

UnitMovementPoint SelectOwnerProductionPlacementAnchorPoint(
    const OwnerProductionPlacementAnchorSet& anchors, u32 placement_class,
    u32 produced_unit_count) {
    switch (placement_class) {
    case 1:
        return anchors.placement_class_1_points[produced_unit_count % 2];
    case 2:
        return anchors.placement_class_2_points[produced_unit_count % 3];
    case 3:
        return anchors.placement_class_3_points[produced_unit_count % 2];
    case 4:
        return anchors.placement_class_4_points[produced_unit_count % 3];
    case 5:
        return anchors.placement_class_5_point;
    default:
        return anchors.base_tile;
    }
}

UnitMovementUnit* FindOwnerProductionPlacementProducer(
    const UnitCommandContext& context, u32 owner_id, u32 producer_unit_type,
    UnitMovementPoint placement_point, UnitMovementUnit* producer_cursor,
    OwnerProductionPlacementProducerPredicate predicate, void* user_data) {
    if (context.movement == nullptr) {
        return nullptr;
    }

    bool reached_cursor = producer_cursor == nullptr;
    UnitMovementUnit* best = nullptr;
    u32 best_distance = 0xffffffff;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr) {
            continue;
        }
        if (!reached_cursor) {
            reached_cursor = unit == producer_cursor;
            if (!reached_cursor) {
                continue;
            }
        }
        if (unit->owner_id != owner_id || unit->type_id != producer_unit_type ||
            (unit->runtime_flags & 0x80) != 0) {
            continue;
        }

        const u32 state = runtime_state(context, *unit);
        if (state != kUnitStateRuntimeIdleAcquire && state != kUnitStateCommand08) {
            continue;
        }

        const u32 distance = CalculateApproxUnitDistance(unit->x, unit->y,
            placement_point.x, placement_point.y);
        if (distance >= best_distance) {
            continue;
        }
        if (predicate != nullptr &&
            !predicate(*unit, placement_point, user_data)) {
            continue;
        }

        best_distance = distance;
        best = unit;
    }
    return best;
}

OwnerProductionBuildActionResult SelectOwnerProductionPlacementBuildAction(
    const UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts,
    const OwnerProductionDependencyRequest& request,
    UnitMovementPoint placement_point,
    UnitMovementUnit* producer_cursor,
    u32 reserved_resource_cost,
    OwnerProductionPlacementProducerPredicate predicate,
    void* user_data) {
    OwnerProductionBuildActionResult result;
    if (owner_id < context.owner_resources.size()) {
        const u32 available = context.owner_resources[owner_id] >
                reserved_resource_cost ?
            context.owner_resources[owner_id] - reserved_resource_cost : 0;
        if (request.resource_cost > available) {
            result.action =
                OwnerProductionBuildAction::blocked_by_resource_budget;
            return result;
        }
    }

    if (!HasOwnerProductionUnitCount(owner_unit_counts,
            request.producer_unit_type)) {
        result.action = OwnerProductionBuildAction::missing_producer_unit;
        result.dependency_unit_type = request.producer_unit_type;
        return result;
    }

    const u32 missing = FindFirstMissingOwnerProductionDependency(
        owner_unit_counts, request.prerequisite_unit_types,
        request.prerequisite_count);
    if (missing != kInvalidOwnerTransportQueueSlot) {
        result.action = OwnerProductionBuildAction::build_missing_prerequisite;
        result.dependency_unit_type = missing;
        return result;
    }

    result.producer_unit = FindOwnerProductionPlacementProducer(context, owner_id,
        request.producer_unit_type, placement_point, producer_cursor, predicate,
        user_data);
    if (result.producer_unit != nullptr) {
        result.action = OwnerProductionBuildAction::use_producer_unit;
    }
    return result;
}

u32 CalculateOwnerProductionAuxDependencyProducerDemand(u32 dependency_demand) {
    return dependency_demand / 5 + 1;
}

OwnerProductionBuildActionResult SelectOwnerProductionAuxDependencyBuildAction(
    UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_unit_counts, u32 producer_unit_type,
    u32 resource_cost, UnitMovementUnit* producer_cursor,
    u32 reserved_resource_cost) {
    OwnerProductionBuildActionResult result;
    result.dependency_unit_type = producer_unit_type;
    if (owner_id < context.owner_resources.size()) {
        const u32 available = context.owner_resources[owner_id] >
                reserved_resource_cost ?
            context.owner_resources[owner_id] - reserved_resource_cost : 0;
        if (resource_cost > available) {
            result.action =
                OwnerProductionBuildAction::blocked_by_resource_budget;
            return result;
        }
    }

    if (!HasOwnerProductionUnitCount(owner_unit_counts, producer_unit_type)) {
        result.action = OwnerProductionBuildAction::missing_producer_unit;
        return result;
    }

    if (context.movement == nullptr) {
        return result;
    }

    bool reached_cursor = producer_cursor == nullptr;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr) {
            continue;
        }
        if (!reached_cursor) {
            reached_cursor = unit == producer_cursor;
            if (!reached_cursor) {
                continue;
            }
        }
        if (!IsReadyOwnerProductionProducer(context, *unit, owner_id,
                producer_unit_type)) {
            continue;
        }

        result.action = OwnerProductionBuildAction::use_producer_unit;
        result.producer_unit = unit;
        return result;
    }
    return result;
}

namespace {

u32 owner_count_for_type(const OwnerUnitTypeCounts& counts, u32 unit_type) {
    return unit_type < counts.counts.size() ? counts.counts[unit_type] : 0;
}

u32 owner_producer_unit_type(
    const OwnerProductionDemandBuildPlanInput& input, u32 unit_type) {
    if (input.producer_unit_types == nullptr ||
        unit_type >= input.producer_unit_types->size()) {
        return kInvalidOwnerTransportQueueSlot;
    }
    return (*input.producer_unit_types)[unit_type];
}

const UnitMovementDefinition* lookup_owner_production_definition(
    const OwnerProductionDemandBuildPlanInput& input, u32 unit_type) {
    return input.definition_lookup != nullptr
        ? input.definition_lookup(unit_type, input.definition_lookup_user_data)
        : nullptr;
}

const OwnerProductionRouteObjectRequirement*
lookup_owner_production_route_object_requirement(
    const OwnerProductionDemandBuildPlanInput& input, u32 unit_type) {
    if (input.route_object_requirements == nullptr ||
        unit_type >= input.route_object_requirements->size()) {
        return nullptr;
    }

    const OwnerProductionRouteObjectRequirement& requirement =
        (*input.route_object_requirements)[unit_type];
    return requirement.required_unit_type != kInvalidOwnerTransportQueueSlot
        ? &requirement : nullptr;
}

OwnerProductionDependencyRequest build_owner_production_dependency_request(
    const OwnerProductionDemandBuildPlanInput& input, u32 unit_type) {
    OwnerProductionDependencyRequest request;
    request.unit_type = unit_type;
    request.producer_unit_type = owner_producer_unit_type(input, unit_type);

    if (unit_type < kOwnerExtendedProductionUnitTypeBase) {
        if (const OwnerProductionRouteObjectRequirement* requirement =
                lookup_owner_production_route_object_requirement(input,
                    unit_type)) {
            request.direct_dependency_unit_type = requirement->object_type;
            return request;
        }
    }

    const UnitMovementDefinition* definition =
        lookup_owner_production_definition(input, unit_type);
    if (definition == nullptr) {
        return request;
    }

    request.resource_cost = definition->production_resource_cost;
    request.prerequisite_count = std::min<u32>(definition->prerequisite_count,
        static_cast<u32>(request.prerequisite_unit_types.size()));
    for (u32 index = 0; index < request.prerequisite_count; ++index) {
        request.prerequisite_unit_types[index] =
            definition->prerequisite_type_ids[index];
        request.special_dependency_unit_types[index] =
            definition->prerequisite_type_ids[index];
    }
    request.special_dependency_count = request.prerequisite_count;
    return request;
}

UnitMovementUnit* next_active_unit_after(
    const UnitCommandContext& context, const UnitMovementUnit* current) {
    if (context.movement == nullptr || current == nullptr) {
        return nullptr;
    }

    bool seen_current = false;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (!seen_current) {
            seen_current = unit == current;
            continue;
        }
        if (unit != nullptr) {
            return unit;
        }
    }
    return nullptr;
}

void raise_owner_producer_demand(UnitCommandContext& context, u32 owner_id,
    u32 demanded_unit_type, u32 desired_count, u32 current_count,
    OwnerProductionDemandBuildPlanResult& result,
    const OwnerProductionDemandBuildPlanInput& input) {
    const u32 producer_unit_type = owner_producer_unit_type(input,
        demanded_unit_type);
    if (producer_unit_type >= result.demand_state.base_demand.counts.size()) {
        ++result.unavailable_producer_count;
        return;
    }

    const OwnerUnitTypeCounts& owner_counts =
        input.owner_unit_counts != nullptr ? *input.owner_unit_counts :
        OwnerUnitTypeCounts{};
    const u32 missing_count = desired_count - current_count;
    const u32 requested_producer_count = (missing_count + 5u) / 6u;
    const u32 producer_current_count =
        owner_count_for_type(owner_counts, producer_unit_type) +
        CountOwnerQueuedExtendedProductionUnits(context, owner_id,
            producer_unit_type,
            input.producer_unit_types != nullptr ? *input.producer_unit_types :
                std::array<u32, kOwnerUnitTypeCountSlots>{});
    if (producer_current_count < requested_producer_count &&
        result.demand_state.base_demand.counts[producer_unit_type] <
            requested_producer_count) {
        result.demand_state.base_demand.counts[producer_unit_type] =
            requested_producer_count;
        ++result.raised_producer_demand_count;
    }
}

void apply_owner_production_generated_demands(
    OwnerProductionDemandBuildPlanResult& result,
    const OwnerProductionDemandBuildPlanInput& input) {
    result.demand_state.bonus_demand = {};

    const u32 faction = input.owner_faction;
    if (faction < input.faction_resource_budget_unit_types.size()) {
        const u32 resource_unit_type =
            input.faction_resource_budget_unit_types[faction];
        const UnitMovementDefinition* definition =
            lookup_owner_production_definition(input, resource_unit_type);
        const u32 unit_cost = definition != nullptr ?
            std::max<u32>(definition->production_resource_cost, 1) : 1;
        const u32 resource_demand = CalculateOwnerResourceBudgetUnitDemand(
            input.resource_budget_base, input.resource_budget_percent,
            input.resource_budget_cap_base, unit_cost);
        if (resource_unit_type < result.demand_state.base_demand.counts.size()) {
            result.demand_state.base_demand.counts[resource_unit_type] =
                resource_demand;
        }
    }

    if (input.target_owner_counts != nullptr &&
        input.target_composition_rules != nullptr &&
        input.target_composition_percent_bonus != 0) {
        AddOwnerTargetCompositionDemand(result.demand_state.bonus_demand,
            *input.target_owner_counts, *input.target_composition_rules,
            input.target_composition_percent_bonus);
    }

    if (input.target_owner_counts != nullptr &&
        faction < input.faction_primary_combat_unit_types.size() &&
        faction < input.faction_carrier_unit_types.size()) {
        const OwnerUnitTypeCounts& owner_counts =
            input.owner_unit_counts != nullptr ? *input.owner_unit_counts :
            OwnerUnitTypeCounts{};
        const u32 carrier_type = input.faction_carrier_unit_types[faction];
        AddOwnerPrimaryAndCarrierDemand(result.demand_state,
            input.faction_primary_combat_unit_types[faction],
            *input.target_owner_counts,
            carrier_type,
            input.carrier_deficit,
            owner_count_for_type(owner_counts, carrier_type));
    }

    ApplyOwnerProductionDemandAliases(result.demand_state.bonus_demand);
    ApplyOwnerProductionDemandAliases(result.demand_state.base_demand);
}

void issue_owner_primary_production_command(UnitCommandContext& context,
    u32 owner_id, UnitMovementUnit& producer, u32 unit_type, u32 resource_cost,
    OwnerProductionDemandBuildPlanResult& result) {
    if (!SetOrQueueUnitCommand10(&producer, unit_type, true)) {
        ++result.unavailable_producer_count;
        return;
    }
    if (owner_id < context.owner_resources.size()) {
        context.owner_resources[owner_id] -=
            std::min(context.owner_resources[owner_id], resource_cost);
    }
    ++result.issued_primary_count;
}

void add_owner_aux_dependency_demand(
    std::array<u32, kOwnerProductionAuxDependencySlotCount>& dependency_demand,
    u32 dependency_slot, u32 amount) {
    if (dependency_slot < dependency_demand.size()) {
        dependency_demand[dependency_slot] += amount;
    }
}

u32 owner_aux_dependency_producer_unit_type(
    const OwnerProductionDemandBuildPlanInput& input, u32 dependency_slot) {
    if (input.aux_dependency_producer_unit_types == nullptr ||
        dependency_slot >= input.aux_dependency_producer_unit_types->size()) {
        return kInvalidOwnerTransportQueueSlot;
    }
    return (*input.aux_dependency_producer_unit_types)[dependency_slot];
}

u32 owner_aux_dependency_resource_cost(
    const OwnerProductionDemandBuildPlanInput& input, u32 producer_unit_type) {
    const UnitMovementDefinition* definition =
        lookup_owner_production_definition(input, producer_unit_type);
    return definition != nullptr ? definition->production_resource_cost : 0;
}

void raise_owner_aux_dependency_producer_demand(
    UnitCommandContext& context, u32 owner_id,
    const OwnerUnitTypeCounts& owner_counts,
    OwnerProductionDemandBuildPlanResult& result,
    const OwnerProductionDemandBuildPlanInput& input,
    u32 producer_unit_type, u32 dependency_demand) {
    if (producer_unit_type >= result.demand_state.base_demand.counts.size()) {
        return;
    }

    const std::array<u32, kOwnerUnitTypeCountSlots> empty_producer_types{};
    const auto& producer_types = input.producer_unit_types != nullptr
        ? *input.producer_unit_types : empty_producer_types;
    const u32 planned_total =
        result.demand_state.base_demand.counts[producer_unit_type] +
        result.demand_state.bonus_demand.counts[producer_unit_type] +
        owner_count_for_type(owner_counts, producer_unit_type) +
        CountOwnerQueuedExtendedProductionUnits(context, owner_id,
            producer_unit_type, producer_types);
    const u32 required_total =
        CalculateOwnerProductionAuxDependencyProducerDemand(dependency_demand);
    if (planned_total < required_total) {
        ++result.demand_state.base_demand.counts[producer_unit_type];
        ++result.raised_aux_dependency_demand_count;
    }
}

bool owner_production_point_valid(UnitMovementPoint point) {
    return point.x >= 0 && point.y >= 0;
}

UnitMovementUnit* find_owner_extended_placement_anchor_unit(
    const UnitCommandContext& context, u32 owner_id, u32 primary_unit_type,
    UnitMovementPoint placement_reference_point) {
    if (context.movement == nullptr) {
        return nullptr;
    }

    UnitMovementUnit* best = nullptr;
    u32 best_distance = 0xffffffff;
    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit == nullptr || unit->owner_id != owner_id ||
            unit->type_id != primary_unit_type) {
            continue;
        }
        if (!owner_production_point_valid(placement_reference_point)) {
            return unit;
        }

        const u32 distance = CalculateApproxUnitDistance(unit->x, unit->y,
            placement_reference_point.x, placement_reference_point.y);
        if (distance < best_distance) {
            best_distance = distance;
            best = unit;
        }
    }
    return best;
}

struct OwnerExtendedPlacementSearchContext {
    const UnitCommandContext* context = nullptr;
    const OwnerProductionDemandBuildPlanInput* input = nullptr;
    const UnitMovementDefinition* definition = nullptr;
    const UnitMovementUnit* anchor_unit = nullptr;
    const std::vector<const UnitMovementUnit*>* ignored_route_units = nullptr;
    UnitMovementPoint owner_target_point{-1, -1};
    u32 owner_id = 0;
    u32 unit_type = 0;
    bool require_path_availability = true;
};

bool owner_extended_placement_candidate(UnitMovementPoint world_point,
    void* user_data) {
    auto* search =
        static_cast<OwnerExtendedPlacementSearchContext*>(user_data);
    if (search == nullptr || search->context == nullptr ||
        search->input == nullptr || search->definition == nullptr ||
        search->anchor_unit == nullptr) {
        return false;
    }

    const OwnerProductionPlacementGateResult gate =
        CheckOwnerProductionPlacementCandidateGate(*search->context,
            search->anchor_unit, *search->definition, search->unit_type,
            world_point);
    if (gate.blocked) {
        return false;
    }
    if (!search->require_path_availability ||
        !owner_production_point_valid(search->owner_target_point)) {
        return true;
    }

    const OwnerProductionPlacementPathAvailabilityResult path =
        CheckOwnerProductionPlacementPathProbeAvailability(*search->context,
            search->owner_id, *search->anchor_unit, *search->definition,
            search->unit_type, world_point,
            search->owner_target_point,
            OwnerProductionPlacementDefinitionRequiresPathProbe(
                *search->definition),
            search->input->placement_path_probe,
            search->input->placement_path_probe_user_data,
            search->input->placement_nearby_probe,
            search->ignored_route_units,
            search->input->placement_target_refresh,
            search->input->placement_target_refresh_user_data,
            search->input->route_state);
    if (path.target_refreshed) {
        search->owner_target_point = path.target_point;
    }
    return path.available;
}

std::vector<const UnitMovementUnit*> build_owner_extended_placement_ignored_route_units(
    const OwnerProductionDemandBuildPlanInput& input) {
    std::vector<const UnitMovementUnit*> ignored_units;
    if (input.route_state == nullptr) {
        return ignored_units;
    }

    const u32 route_count = std::min<u32>(input.route_state->route_count,
        static_cast<u32>(input.route_state->targets.size()));
    for (u32 route_index = 1; route_index < route_count; ++route_index) {
        const UnitMovementUnit* unit =
            input.route_state->targets[route_index].unit;
        if (unit != nullptr) {
            ignored_units.push_back(unit);
        }
    }
    return ignored_units;
}

OwnerProductionPlacementSearchResult find_owner_extended_placement_point(
    const UnitCommandContext& context, u32 owner_id, u32 unit_type,
    const OwnerUnitTypeCounts& owner_counts,
    const OwnerProductionDemandBuildPlanInput& input) {
    OwnerProductionPlacementSearchResult result;
    if (owner_id >= context.owner_resources.size()) {
        return result;
    }

    const UnitMovementDefinition* definition =
        lookup_owner_production_definition(input, unit_type);
    if (definition == nullptr ||
        definition->production_resource_cost > context.owner_resources[owner_id]) {
        return result;
    }

    const u32 faction = input.owner_faction;
    if (faction >= input.faction_primary_unit_types.size()) {
        return result;
    }
    const u32 primary_unit_type = input.faction_primary_unit_types[faction];
    if (!HasOwnerProductionUnitCount(owner_counts, primary_unit_type)) {
        return result;
    }

    UnitMovementUnit* anchor_unit =
        find_owner_extended_placement_anchor_unit(context, owner_id,
            primary_unit_type,
            input.placement_anchors != nullptr ?
                input.placement_anchors->base_tile : UnitMovementPoint{-1, -1});
    if (anchor_unit == nullptr) {
        return result;
    }

    OwnerExtendedPlacementSearchContext search;
    search.context = &context;
    search.input = &input;
    search.definition = definition;
    search.anchor_unit = anchor_unit;
    search.owner_target_point = input.owner_target_point;
    search.owner_id = owner_id;
    search.unit_type = unit_type;
    const std::vector<const UnitMovementUnit*> ignored_route_units =
        build_owner_extended_placement_ignored_route_units(input);
    search.ignored_route_units =
        ignored_route_units.empty() ? nullptr : &ignored_route_units;

    // The refreshed placement anchors are strategic hints, not a permanent
    // reason to stop production.  They can be stale after a worker moves or a
    // route target changes.  Preserve the original, fully path-constrained
    // search first, then retry around the actual primary/producer unit.
    if (input.placement_anchors != nullptr) {
        const UnitMovementPoint anchor_tile =
            SelectOwnerProductionPlacementAnchorPoint(*input.placement_anchors,
                definition->placement_class, owner_count_for_type(owner_counts,
                    unit_type));
        result = FindOwnerProductionPlacementPointSpiral(anchor_tile,
            owner_extended_placement_candidate, &search);
        if (result.found) {
            return result;
        }
    }

    const UnitMovementPoint producer_tile{
        ConvertOwnerProductionWorldToTileSar(anchor_unit->x),
        ConvertOwnerProductionWorldToTileSar(anchor_unit->y),
    };
    result = FindOwnerProductionPlacementPointSpiral(producer_tile,
        owner_extended_placement_candidate, &search, 0x10);
    if (result.found) {
        return result;
    }

    // A temporarily unreachable/stale strategic target must not deadlock the
    // owner's entire construction tree.  The final retry is deliberately
    // local to the producer and relaxes only that route-to-target probe.  The
    // normal footprint, bounds, terrain, occupancy and unit-collision gates
    // still run for every candidate, and the worker must still accept command
    // 0x06 before any resources are reserved.
    search.require_path_availability = false;
    return FindOwnerProductionPlacementPointSpiral(producer_tile,
        owner_extended_placement_candidate, &search, 8);
}

void issue_owner_extended_placement_command(UnitCommandContext& context,
    u32 owner_id, UnitMovementUnit& producer, u32 unit_type,
    UnitMovementPoint placement_point, u32 resource_cost,
    OwnerProductionDemandBuildPlanResult& result) {
    if (!SetOrQueueUnitAlignedPointCommand06(&producer,
            unit_type - kOwnerExtendedProductionUnitTypeBase,
            placement_point.x, placement_point.y, false)) {
        ++result.unavailable_producer_count;
        return;
    }
    result.reserved_resource_cost += resource_cost;
    ++result.issued_extended_count;
}

void process_owner_extended_production_demand(UnitCommandContext& context,
    u32 owner_id, const OwnerUnitTypeCounts& owner_counts,
    OwnerProductionDemandBuildPlanResult& result,
    const OwnerProductionDemandBuildPlanInput& input,
    const std::array<u32, kOwnerUnitTypeCountSlots>& producer_types) {
    for (u32 unit_type = kOwnerExtendedProductionUnitTypeBase;
         unit_type < result.demand_state.base_demand.counts.size(); ++unit_type) {
        const u32 desired_count =
            result.demand_state.base_demand.counts[unit_type];
        const u32 current_count = owner_count_for_type(owner_counts, unit_type) +
            CountOwnerQueuedExtendedProductionUnits(context, owner_id,
                unit_type, producer_types);
        if (current_count >= desired_count) {
            continue;
        }

        const OwnerProductionPlacementSearchResult placement =
            find_owner_extended_placement_point(context, owner_id, unit_type,
                owner_counts, input);
        if (!placement.found) {
            ++result.extended_missing_placement_count;
            continue;
        }

        OwnerProductionDependencyRequest request =
            build_owner_production_dependency_request(input, unit_type);
        OwnerProductionBuildActionResult action =
            SelectOwnerProductionPlacementBuildAction(context, owner_id,
                owner_counts, request, placement.point, nullptr,
                result.reserved_resource_cost,
                input.placement_producer_predicate,
                input.placement_producer_user_data);
        switch (action.action) {
        case OwnerProductionBuildAction::no_producer_available:
        case OwnerProductionBuildAction::missing_producer_unit:
            ++result.unavailable_producer_count;
            return;
        case OwnerProductionBuildAction::blocked_by_resource_budget:
            ++result.extended_blocked_by_resource_count;
            continue;
        case OwnerProductionBuildAction::build_missing_prerequisite:
            if (action.dependency_unit_type <
                result.demand_state.base_demand.counts.size()) {
                result.demand_state.base_demand
                    .counts[action.dependency_unit_type] = 1;
                ++result.raised_extended_dependency_demand_count;
            }
            continue;
        case OwnerProductionBuildAction::use_producer_unit:
            if (action.producer_unit == nullptr) {
                ++result.unavailable_producer_count;
                return;
            }
            issue_owner_extended_placement_command(context, owner_id,
                *action.producer_unit, unit_type, placement.point,
                request.resource_cost, result);
            return;
        default:
            return;
        }
    }
}

} // namespace

void ProcessOwnerProductionAuxDependencyDemand(UnitCommandContext& context,
    u32 owner_id, const OwnerUnitTypeCounts& owner_unit_counts,
    OwnerProductionDemandBuildPlanResult& result,
    const OwnerProductionDemandBuildPlanInput& input,
    const std::array<u32, kOwnerProductionAuxDependencySlotCount>&
        dependency_demand) {
    for (u32 dependency_slot = 0; dependency_slot < dependency_demand.size();
         ++dependency_slot) {
        u32 remaining = dependency_demand[dependency_slot];
        if (remaining == 0) {
            continue;
        }

        const u32 producer_unit_type =
            owner_aux_dependency_producer_unit_type(input, dependency_slot);
        if (producer_unit_type == kInvalidOwnerTransportQueueSlot) {
            continue;
        }

        UnitMovementUnit* producer_cursor = nullptr;
        u32 guard = 0;
        while (remaining != 0 && guard++ < 64) {
            const u32 resource_cost =
                owner_aux_dependency_resource_cost(input, producer_unit_type);
            OwnerProductionBuildActionResult action =
                SelectOwnerProductionAuxDependencyBuildAction(context, owner_id,
                    owner_unit_counts, producer_unit_type, resource_cost,
                    producer_cursor, result.reserved_resource_cost);
            switch (action.action) {
            case OwnerProductionBuildAction::blocked_by_resource_budget:
                ++result.aux_blocked_by_resource_count;
                remaining = 0;
                break;
            case OwnerProductionBuildAction::missing_producer_unit:
                raise_owner_aux_dependency_producer_demand(context, owner_id,
                    owner_unit_counts, result, input, producer_unit_type,
                    dependency_demand[dependency_slot]);
                remaining = 0;
                break;
            case OwnerProductionBuildAction::use_producer_unit:
                if (action.producer_unit == nullptr) {
                    remaining = 0;
                    break;
                }
                if (SetOrQueueUnitCommand22(action.producer_unit,
                        dependency_slot, false)) {
                    ++result.issued_aux_dependency_count;
                    --remaining;
                }
                producer_cursor = next_active_unit_after(context,
                    action.producer_unit);
                if (producer_cursor == nullptr) {
                    remaining = 0;
                }
                break;
            default:
                remaining = 0;
                break;
            }
        }
    }
}

OwnerProductionDemandBuildPlanResult
ProcessOwnerProductionDemandAndBuildPlan(UnitCommandContext& context,
    u32 owner_id, OwnerProductionDemandState demand_state,
    const OwnerProductionDemandBuildPlanInput& input) {
    OwnerProductionDemandBuildPlanResult result;
    result.demand_state = demand_state;
    result.reserved_resource_cost = input.reserved_resource_cost;
    if (owner_id >= context.owner_resources.size()) {
        return result;
    }

    apply_owner_production_generated_demands(result, input);

    const OwnerUnitTypeCounts& owner_counts =
        input.owner_unit_counts != nullptr ? *input.owner_unit_counts :
        OwnerUnitTypeCounts{};
    const std::array<u32, kOwnerUnitTypeCountSlots> empty_producer_types{};
    const auto& producer_types = input.producer_unit_types != nullptr
        ? *input.producer_unit_types : empty_producer_types;
    std::array<u32, kOwnerProductionAuxDependencySlotCount>
        aux_dependency_demand{};

    for (u32 unit_type = 0; unit_type < kOwnerExtendedProductionUnitTypeBase;
         ++unit_type) {
        const u32 desired_count =
            result.demand_state.base_demand.counts[unit_type] +
            result.demand_state.bonus_demand.counts[unit_type];
        u32 current_count = owner_count_for_type(owner_counts, unit_type) +
            CountOwnerQueuedPrimaryProductionUnits(context, owner_id,
                unit_type, producer_types);
        UnitMovementUnit* producer_cursor = nullptr;

        u32 guard = 0;
        while (current_count < desired_count && guard++ < 64) {
            OwnerProductionDependencyRequest request =
                build_owner_production_dependency_request(input, unit_type);
            OwnerProductionBuildActionResult action =
                SelectOwnerProductionDependencyBuildAction(context, owner_id,
                    owner_counts, request, producer_cursor,
                    result.reserved_resource_cost);
            switch (action.action) {
            case OwnerProductionBuildAction::no_producer_available:
            case OwnerProductionBuildAction::missing_producer_unit:
                raise_owner_producer_demand(context, owner_id, unit_type,
                    desired_count, current_count, result, input);
                current_count = desired_count;
                break;
            case OwnerProductionBuildAction::blocked_by_resource_budget:
                ++result.blocked_by_resource_count;
                current_count = desired_count;
                break;
            case OwnerProductionBuildAction::build_missing_prerequisite:
                if (action.dependency_unit_type <
                    result.demand_state.base_demand.counts.size()) {
                    result.demand_state.base_demand
                        .counts[action.dependency_unit_type] = 1;
                    ++result.raised_dependency_demand_count;
                }
                current_count = desired_count;
                break;
            case OwnerProductionBuildAction::unlock_missing_dependency:
                if (input.owner_shared_dependency_flags != nullptr &&
                    action.dependency_unit_type <
                        input.owner_shared_dependency_flag_count) {
                    input.owner_shared_dependency_flags[
                        action.dependency_unit_type] = 1;
                    ++result.marked_unlock_dependency_count;
                }
                current_count = desired_count;
                break;
            case OwnerProductionBuildAction::resolve_transport_dependency: {
                const OwnerProductionRouteObjectRequirement* requirement =
                    lookup_owner_production_route_object_requirement(
                        input, unit_type);
                bool assigned = false;
                if (requirement != nullptr && input.transport_queue != nullptr &&
                    input.route_objects != nullptr) {
                    OwnerProductionRouteObjectAssignmentResult assignment =
                        AssignOwnerProductionRouteObject(context,
                            *input.transport_queue, owner_id,
                            requirement->required_unit_type,
                            requirement->object_type,
                            *input.route_objects);
                    if (assignment.code ==
                        OwnerProductionRouteObjectAssignmentCode::assigned) {
                        ++result.route_object_assigned_count;
                        ++current_count;
                        assigned = true;
                    }
                    else if (assignment.code ==
                        OwnerProductionRouteObjectAssignmentCode::no_visible_object) {
                        add_owner_aux_dependency_demand(aux_dependency_demand,
                            requirement->object_type,
                            desired_count - current_count);
                        ++result.route_object_unhandled_count;
                    }
                    else {
                        ++result.route_object_unhandled_count;
                    }
                }
                else {
                    ++result.route_object_unhandled_count;
                }
                if (!assigned) {
                    current_count = desired_count;
                }
                break;
            }
            case OwnerProductionBuildAction::run_special_pairing:
                if (RunOwnerProductionSpecialPairing(context, owner_id, unit_type)) {
                    ++result.special_pairing_count;
                }
                current_count = desired_count;
                break;
            case OwnerProductionBuildAction::use_producer_unit:
                if (action.producer_unit == nullptr) {
                    ++result.unavailable_producer_count;
                    current_count = desired_count;
                    break;
                }
                issue_owner_primary_production_command(context, owner_id,
                    *action.producer_unit, unit_type, request.resource_cost, result);
                ++current_count;
                producer_cursor = next_active_unit_after(context,
                    action.producer_unit);
                if (producer_cursor == nullptr) {
                    current_count = desired_count;
                }
                break;
            }
        }
    }

    ProcessOwnerProductionAuxDependencyDemand(context, owner_id, owner_counts,
        result, input, aux_dependency_demand);
    process_owner_extended_production_demand(context, owner_id, owner_counts,
        result, input, producer_types);

    RemoveOwnerProductionDemandAliases(result.demand_state.base_demand);
    return result;
}

bool CheckOwnerProductionRouteWorkerNeedsObject(
    const UnitMovementUnit& unit, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type) {
    if (unit.owner_id != owner_id || unit.type_id != required_unit_type) {
        return false;
    }

    const u32 slot_index = unit.area_marker_flags & 0xffu;
    if (slot_index >= queue.slots.size()) {
        return false;
    }
    const u32 state = queue.slots[slot_index].state;
    if (state < 6 || state > 8) {
        return false;
    }

    return unit.active_command_payload.state == 0 ||
        unit.active_command_payload.value == 0 ||
        unit.active_command_payload.x == 0 ||
        unit.active_command_payload.y == 0;
}

UnitMovementUnit* FindOwnerProductionRouteWorker(
    const UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type) {
    if (context.movement == nullptr) {
        return nullptr;
    }

    for (UnitMovementUnit* unit : context.movement->active_units) {
        if (unit != nullptr &&
            CheckOwnerProductionRouteWorkerNeedsObject(*unit, queue, owner_id,
                required_unit_type)) {
            return unit;
        }
    }
    return nullptr;
}

OwnerProductionRouteObjectCandidate*
FindNearestOwnerProductionRouteObjectCandidate(
    std::vector<OwnerProductionRouteObjectCandidate>& objects,
    const UnitMovementUnit& worker, u32 owner_id, u32 object_type) {
    OwnerProductionRouteObjectCandidate* best = nullptr;
    u32 best_distance = 0xffffffff;
    const u32 owner_mask = owner_id < 32 ? (1u << owner_id) : 0;
    for (OwnerProductionRouteObjectCandidate& object : objects) {
        if (object.object_type != object_type || (object.flags & 1u) != 0) {
            continue;
        }
        if (owner_mask != 0 &&
            (object.owner_visibility_mask & owner_mask) == 0) {
            continue;
        }

        const u32 distance = CalculateApproxUnitDistance(worker.x, worker.y,
            object.point.x, object.point.y);
        if (distance < best_distance) {
            best_distance = distance;
            best = &object;
        }
    }
    return best;
}

OwnerProductionRouteObjectAssignmentResult AssignOwnerProductionRouteObject(
    UnitCommandContext& context, const OwnerTransportQueueState& queue,
    u32 owner_id, u32 required_unit_type, u32 object_type,
    std::vector<OwnerProductionRouteObjectCandidate>& objects) {
    OwnerProductionRouteObjectAssignmentResult result;
    if (object_type == 0) {
        result.code = OwnerProductionRouteObjectAssignmentCode::missing_object_type;
        return result;
    }

    result.worker = FindOwnerProductionRouteWorker(context, queue, owner_id,
        required_unit_type);
    if (result.worker == nullptr) {
        result.code =
            OwnerProductionRouteObjectAssignmentCode::missing_required_unit;
        return result;
    }

    result.object = FindNearestOwnerProductionRouteObjectCandidate(objects,
        *result.worker, owner_id, object_type);
    if (result.object == nullptr) {
        result.code = OwnerProductionRouteObjectAssignmentCode::no_visible_object;
        return result;
    }

    SetOrQueueUnitPointCommand01(
        result.worker, result.object->point.x, result.object->point.y, false);
    result.object->flags |= 1u;
    result.object->assigned_unit = result.worker;
    result.code = OwnerProductionRouteObjectAssignmentCode::assigned;
    return result;
}

u32 CalculateOwnerPrimaryUnitDesiredCountFromTargetOwner(
    const OwnerUnitTypeCounts& target_owner_counts) {
    u32 tracked_count = 0;
    for (u32 type_index : kOwnerPrimaryDemandTrackedUnitTypes) {
        tracked_count += target_owner_counts.counts[type_index];
    }
    return tracked_count == 0 ? 0 : tracked_count / 4 + 1;
}

u32 FindOwnerTransportQueueSlotNeedingCarrier(const UnitCommandContext& context,
    const OwnerTransportQueueState& queue, u32 owner_id, u32 carrier_capacity) {
    for (std::size_t index = 1; index < queue.slots.size(); ++index) {
        const OwnerTransportQueueSlot& slot = queue.slots[index];
        if (slot.state < kOwnerTransportQueueStatePendingA ||
            slot.state > kOwnerTransportQueueStatePendingB) {
            continue;
        }
        const u32 required = CalculateOwnerTransportGroupRequiredCarrierCount(context,
            owner_id, slot.linked_group, carrier_capacity);
        if (slot.count < required) {
            return static_cast<u32>(index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

u32 FindOwnerTransportPassengerGroupWithoutCarrierReservation(
    const OwnerTransportQueueState& queue) {
    for (std::size_t group_index = 1; group_index < queue.slots.size(); ++group_index) {
        const u32 state = queue.slots[group_index].state;
        if (state != 3 && state != 0x1b) {
            continue;
        }

        bool has_reservation = false;
        for (std::size_t reservation_index = 1; reservation_index < queue.slots.size();
             ++reservation_index) {
            const OwnerTransportQueueSlot& reservation = queue.slots[reservation_index];
            if (reservation.linked_group != group_index) {
                continue;
            }
            if (reservation.state >= kOwnerTransportQueueStatePendingA &&
                reservation.state <= kOwnerTransportQueueStatePendingC) {
                has_reservation = true;
                break;
            }
        }
        if (!has_reservation) {
            return static_cast<u32>(group_index);
        }
    }
    return kInvalidOwnerTransportQueueSlot;
}

u32 CalculateUnitTransportCapacity(const UnitMovementUnit& unit,
    const ProductionOrderRuntimeState* production_state) {
    const u32 base_capacity = unit.definition.transport_capacity;
    if (production_state == nullptr) {
        return base_capacity;
    }
    return CalculateUnitTransportCapacityWithProductionEffect09(*production_state, unit,
        base_capacity);
}

} // namespace ranker
