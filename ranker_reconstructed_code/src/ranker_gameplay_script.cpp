#include "ranker_gameplay_script.h"

#include "ranker_miles.h"
#include "ranker_trc.h"
#include "ranker_unit_commands.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_lifecycle.h"
#include "ranker_unit_movement.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ranker {
namespace {

GameplayScriptDialogState g_dialog_state;
GameplayScriptTriggerState g_trigger_state;

void encode_trigger(GameplayScriptTriggerState& state, u32 trigger_index);

i32 signed_i32_from_wrapped_u32(u32 value) {
    i32 signed_value = 0;
    std::memcpy(&signed_value, &value, sizeof(signed_value));
    return signed_value;
}

u32 read_le_u32(const std::vector<u8>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(u32)) {
        return 0;
    }
    return static_cast<u32>(bytes[offset]) |
        (static_cast<u32>(bytes[offset + 1]) << 8) |
        (static_cast<u32>(bytes[offset + 2]) << 16) |
        (static_cast<u32>(bytes[offset + 3]) << 24);
}

void write_le_u32(std::vector<u8>& bytes, std::size_t offset, u32 value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(u32)) {
        return;
    }
    bytes[offset] = static_cast<u8>(value & 0xffu);
    bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xffu);
}

bool object_removed_by_state(const GameplayScriptTriggerState& state, u32 object_index) {
    return object_index < state.objects.size() &&
        state.objects[object_index].remove_from_triggers;
}

bool object_removed_by_callback(const GameplayScriptTriggerCallbacks& callbacks,
    u32 object_index) {
    return callbacks.is_object_removed != nullptr &&
        callbacks.is_object_removed(object_index, callbacks.user);
}

const GameplayScriptOwnerConditionState* owner_state(
    const GameplayScriptConditionContext& context, u32 owner_id) {
    if (owner_id >= context.owners.size()) {
        return nullptr;
    }
    return &context.owners[owner_id];
}

GameplayScriptOwnerConditionState* owner_state(
    GameplayScriptConditionContext& context, u32 owner_id) {
    if (owner_id >= context.owners.size()) {
        return nullptr;
    }
    return &context.owners[owner_id];
}

bool owner_active(const GameplayScriptConditionContext& context, u32 owner_id) {
    const GameplayScriptOwnerConditionState* owner = owner_state(context, owner_id);
    return owner != nullptr && owner->status != kGameplayScriptOwnerInactiveStatus;
}

bool owner_has_active_objects(
    const GameplayScriptConditionContext& context, u32 owner_id) {
    const GameplayScriptOwnerConditionState* owner = owner_state(context, owner_id);
    if (owner == nullptr) {
        return false;
    }
    if (context.owner_active_counts_available &&
        owner_id < context.owner_unit_active_count.size() &&
        owner_id < context.owner_building_active_count.size()) {
        return context.owner_unit_active_count[owner_id] +
            context.owner_building_active_count[owner_id] != 0;
    }
    return std::any_of(owner->unit_type_counts.begin(), owner->unit_type_counts.end(),
        [](u32 count) { return count != 0; });
}

const GameplayScriptTriggerObjectState* object_state(
    const GameplayScriptTriggerState& state, u32 object_index) {
    if (object_index == 0 || object_index >= state.objects.size()) {
        return nullptr;
    }
    return &state.objects[object_index];
}

GameplayScriptTriggerObjectState* object_state(
    GameplayScriptTriggerState& state, u32 object_index) {
    if (object_index == 0 || object_index >= state.objects.size()) {
        return nullptr;
    }
    return &state.objects[object_index];
}

bool object_alive(const GameplayScriptTriggerObjectState* object) {
    return object != nullptr && !object->remove_from_triggers &&
        (object->flags & 4u) == 0;
}

bool object_visible_for_area_scan(const GameplayScriptTriggerObjectState& object) {
    return object_alive(&object) && (object.flags & 0x80u) == 0;
}

u32 object_command_flags(const GameplayScriptTriggerObjectState& object) {
    return object.unit != nullptr ? object.unit->command_flags : object.command_flags;
}

bool area_contains_point(const GameplayScriptArea& area, i32 x, i32 y) {
    return area.left <= x && x <= area.right && area.top <= y && y <= area.bottom;
}

bool area_contains_object(const GameplayScriptArea& area,
    const GameplayScriptTriggerObjectState& object) {
    return area_contains_point(area, object.x, object.y);
}

bool object_bounds_inside_area(const GameplayScriptArea& area,
    const GameplayScriptTriggerObjectState& object) {
    const i32 left = object.x + object.bounds.left;
    const i32 top = object.y + object.bounds.top;
    const i32 right = object.x + object.bounds.right;
    const i32 bottom = object.y + object.bounds.bottom;
    return area.left < left && right < area.right &&
        area.top < top && bottom < area.bottom;
}

u32 object_stat_by_mode(const GameplayScriptTriggerObjectState& object, u32 mode) {
    switch (mode) {
    case 0:
        return object.stat_20;
    case 1:
        return object.stat_secondary_current;
    case 2:
        return object.stat_24;
    case 3:
        return object.stat_1c;
    case 4:
        return object.stat_54;
    default:
        return 0;
    }
}

u32 object_equipment_count(const GameplayScriptTriggerObjectState& object, u32 effect_id) {
    if (object.unit != nullptr) {
        return CountUnitEquipmentEffectSlots(*object.unit, effect_id);
    }

    u32 count = 0;
    for (u32 slot : object.equipment_slots) {
        if (slot == effect_id) {
            ++count;
        }
    }
    return count;
}

u32 object_runtime_category(const GameplayScriptTriggerObjectState& object) {
    if (object.unit != nullptr) {
        return ClassifyGameplayScriptUnitRuntimeState(*object.unit);
    }
    return object.script_state;
}

void mark_gameplay_script_object_dead(GameplayScriptTriggerObjectState& object) {
    object.flags |= 4u;
    object.remove_from_triggers = true;
    object.script_removal_requested = true;
    if (object.unit != nullptr) {
        object.unit->command_state |= kUnitCommandDead;
    }
}

void mutate_gameplay_script_object_runtime_flags(
    GameplayScriptTriggerObjectState& object, u32 clear_mask, u32 set_mask) {
    object.flags = (object.flags & ~clear_mask) | set_mask;
    if (object.unit != nullptr) {
        object.unit->runtime_flags =
            (object.unit->runtime_flags & ~clear_mask) | set_mask;
    }
}

void mutate_gameplay_script_object_script_bit_flags(
    GameplayScriptTriggerObjectState& object, u32 clear_mask, u32 set_mask) {
    object.script_bit_flags =
        (object.script_bit_flags & ~clear_mask) | set_mask;
    if (object.unit != nullptr) {
        object.unit->script_bit_flags =
            (object.unit->script_bit_flags & ~clear_mask) | set_mask;
    }
}

u32 gameplay_script_object_runtime_flags(
    const GameplayScriptTriggerObjectState& object) {
    return object.unit != nullptr ? object.unit->runtime_flags : object.flags;
}

void mark_gameplay_script_object_command_dead(
    GameplayScriptTriggerObjectState& object) {
    // Opcodes 0x61/0x62 set only raw unit +0x60 bit 0x10000000.  Runtime
    // flags, trigger membership, and lifecycle-list movement are handled by
    // the subsequent ordinary unit-runtime phase.
    object.command_state_raw |= kUnitCommandDead;
    if (object.unit != nullptr) {
        object.unit->command_state |= kUnitCommandDead;
    }
}

void mutate_gameplay_script_object_selection_flag(
    GameplayScriptTriggerObjectState& object, bool selected) {
    if (selected) {
        object.string_slot |= 0x80u;
    } else {
        object.string_slot &= ~0x80u;
    }
    if (object.unit != nullptr) {
        if (selected) {
            object.unit->scenario_string_slot |= 0x80u;
        } else {
            object.unit->scenario_string_slot &= ~0x80u;
        }
    }
}

void mutate_gameplay_script_object_direction(
    GameplayScriptTriggerObjectState& object, u32 direction) {
    object.direction = direction;
    if (object.unit != nullptr) {
        object.unit->direction = direction;
    }
}

u32 gameplay_script_object_lifecycle_class(
    const GameplayScriptTriggerObjectState& object) {
    return object.unit != nullptr ?
        object.unit->definition.lifecycle_class : object.definition_class;
}

void begin_single_gameplay_script_selection_request(
    GameplayScriptOpcodeContext& opcode, u32 object_index) {
    opcode.selection_request_active = true;
    opcode.selected_object_index = object_index;
    opcode.group_selection_request = false;
    opcode.group_selected_object_count = 0;
    opcode.group_selected_object_indices.fill(0);
}

void clear_active_gameplay_script_selection(GameplayScriptTriggerState& state) {
    // FUN_004ead09 walks the active-unit list, clearing raw +0x08 bit 0x80.
    // Mirror the same field on scenario objects so the later object publish
    // pass cannot restore a stale selected bit.
    for (GameplayScriptTriggerObjectState& object : state.objects) {
        if (object.unit != nullptr) {
            if (object.unit->active) {
                mutate_gameplay_script_object_selection_flag(object, false);
            }
        } else if (object_alive(&object)) {
            mutate_gameplay_script_object_selection_flag(object, false);
        }
    }

    // Some active runtime units may not yet have a scenario-object mirror.
    if (state.opcode_context.movement != nullptr) {
        for (UnitMovementUnit* unit : state.opcode_context.movement->active_units) {
            if (unit != nullptr && unit->active) {
                unit->scenario_string_slot &= ~0x80u;
            }
        }
    }
}

void begin_group_gameplay_script_selection_request(
    GameplayScriptTriggerState& state, const GameplayScriptTriggerGroup& group) {
    clear_active_gameplay_script_selection(state);

    GameplayScriptOpcodeContext& opcode = state.opcode_context;
    opcode.selection_request_active = true;
    opcode.selected_object_index = 0;
    opcode.group_selection_request = true;
    opcode.group_selected_object_count = 0;
    opcode.group_selected_object_indices.fill(0);

    const u32 limit = std::min<u32>(
        group.reference_count, kGameplayScriptSelectionGroupLimit);
    for (u32 slot = 0; slot < limit; ++slot) {
        const u32 object_index = group.object_indices[slot];
        GameplayScriptTriggerObjectState* object = object_state(state, object_index);
        if (object == nullptr) {
            continue;
        }

        mutate_gameplay_script_object_selection_flag(*object, true);
        if (opcode.selected_object_index == 0) {
            opcode.selected_object_index = object_index;
        }
        opcode.group_selected_object_indices[opcode.group_selected_object_count++] =
            object_index;
    }
}

void sync_script_object_identity_to_unit(GameplayScriptTriggerObjectState& object) {
    if (object.unit == nullptr) {
        return;
    }
    object.unit->type_id = object.type_id;
    object.unit->owner_id = object.owner_id;
    object.unit->scenario_string_slot = object.string_slot;
}

void rebuild_owner_unit_type_counts(GameplayScriptTriggerState& state) {
    for (GameplayScriptOwnerConditionState& owner : state.condition_context.owners) {
        owner.unit_type_counts.fill(0);
    }

    if (state.opcode_context.lifecycle != nullptr) {
        HandleOwnerUnitTypeCountRebuild(*state.opcode_context.lifecycle);
        const UnitLifecycleContext& lifecycle = *state.opcode_context.lifecycle;
        const u32 owner_limit = std::min<u32>(
            static_cast<u32>(lifecycle.owner_unit_type_counts.size()),
            static_cast<u32>(state.condition_context.owners.size()));
        for (u32 owner = 0; owner < owner_limit; ++owner) {
            state.condition_context.owners[owner].unit_type_counts =
                lifecycle.owner_unit_type_counts[owner];
        }
        return;
    }

    for (u32 index : state.condition_context.active_object_order) {
        if (index >= state.objects.size()) {
            continue;
        }
        const GameplayScriptTriggerObjectState& object = state.objects[index];
        if (object.owner_id >= kUnitOwnerTypeCountOwners ||
            object.type_id >= kGameplayScriptOwnerUnitTypeCount) {
            continue;
        }
        if (object.unit != nullptr && !object.unit->active) {
            continue;
        }
        const u32 construction_gate = object.unit != nullptr ?
            object.unit->action_mode_gate : static_cast<u32>(object.stat_30);
        if (object.type_id >= 0x60 && construction_gate == 1) {
            continue;
        }
        ++state.condition_context.owners[object.owner_id]
            .unit_type_counts[object.type_id];
    }
}

void convert_script_object_type(GameplayScriptTriggerObjectState& object,
    u32 new_type, const UnitMovementDefinition& definition,
    UnitLifecycleContext* lifecycle) {
    object.type_id = new_type;
    object.definition_class = definition.lifecycle_class;
    object.stat_18 = definition.initial_max_health;
    object.stat_20 = definition.initial_max_health;
    object.stat_1c = definition.profile_offense_value;
    object.stat_24 = definition.profile_defense_value;
    object.stat_secondary_max = definition.initial_max_secondary_value;
    object.stat_secondary_current = definition.initial_max_secondary_value;
    object.stat_28 = definition.initial_secondary_value;
    object.type_flags = definition.type_flags;
    object.script_bit_flags = definition.initial_script_bit_flags;
    object.bounds.left = definition.bounds_left;
    object.bounds.top = definition.bounds_top;
    object.bounds.right = definition.bounds_width;
    object.bounds.bottom = definition.bounds_height;
    object.stat_recompute_required = false;

    if (object.unit == nullptr) {
        return;
    }
    UnitMovementUnit& unit = *object.unit;
    unit.type_id = new_type;
    unit.definition = definition;
    unit.max_health = definition.initial_max_health;
    unit.health = definition.initial_max_health;
    unit.runtime_stat_1c = definition.profile_offense_value;
    unit.runtime_stat_20 = definition.profile_defense_value;
    unit.max_secondary_value = definition.initial_max_secondary_value;
    unit.secondary_value = definition.initial_max_secondary_value;
    unit.runtime_stat_28 = definition.initial_secondary_value;
    unit.type_flags = definition.type_flags;
    unit.script_bit_flags = definition.initial_script_bit_flags;
    if (lifecycle != nullptr) {
        SetUnitFootprintOccupancyBits(*lifecycle, unit);
    }
}

void set_script_object_owner(GameplayScriptTriggerObjectState& object, u32 owner_id) {
    object.owner_id = owner_id;
    sync_script_object_identity_to_unit(object);
}

void set_script_object_type(GameplayScriptTriggerObjectState& object, u32 type_id) {
    object.type_id = type_id;
    sync_script_object_identity_to_unit(object);
}

u32 default_gameplay_script_trigger_owner_phase(u32 lookup_index) {
    switch (lookup_index) {
    case 0x00:
    case 0x01:
    case 0x19:
    case 0x22:
    case 0x59:
        return 1;
    case 0x79:
        return 0xffffffffu;
    default:
        return 0;
    }
}

u32 clamp_to_primary_current_range(u32 value, u32 maximum, bool force_nonzero) {
    if (maximum < value) {
        value = maximum;
    }
    if (force_nonzero && value == 0) {
        value = 1;
    }
    return value;
}

void publish_numeric_stat_to_attached_unit(
    GameplayScriptTriggerObjectState& object, u32 opcode) {
    UnitMovementUnit* unit = object.unit;
    if (unit == nullptr) {
        return;
    }
    switch (opcode) {
    case 0x3c:
        unit->action_mode = static_cast<u32>(object.stat_2c);
        break;
    case 0x3e:
    case 0x3f:
        unit->max_health = object.stat_18;
        unit->health = object.stat_20;
        break;
    case 0x40:
    case 0x41:
        unit->health = object.stat_20;
        break;
    case 0x42:
    case 0x43:
        unit->max_secondary_value = object.stat_secondary_max;
        unit->secondary_value = object.stat_secondary_current;
        break;
    case 0x44:
    case 0x45:
        unit->secondary_value = object.stat_secondary_current;
        break;
    case 0x46:
    case 0x47:
        unit->runtime_stat_1c = object.stat_1c;
        break;
    case 0x48:
    case 0x49:
        unit->runtime_stat_20 = object.stat_24;
        break;
    case 0x4a:
    case 0x4b:
        unit->status_timer = object.stat_54;
        unit->production_variant = object.stat_54;
        break;
    case 0x4c:
    case 0x4d:
        unit->elite_progress_value = object.stat_50;
        break;
    default:
        break;
    }
}

void set_object_stat_by_mode(GameplayScriptTriggerObjectState& object, u32 mode, u32 value) {
    switch (mode) {
    case 0:
        object.stat_20 = clamp_to_primary_current_range(value, object.stat_18, false);
        break;
    case 1:
        object.stat_secondary_current = std::min(value, object.stat_secondary_max);
        break;
    case 2:
        object.stat_24 = value;
        break;
    case 3:
        object.stat_1c = value;
        break;
    case 4:
        object.stat_54 = std::min<u32>(value, 999);
        break;
    default:
        break;
    }
}

void add_equipment_effect_slot(GameplayScriptTriggerObjectState& object, u32 effect_id) {
    if ((object_command_flags(object) & 2u) == 0 || effect_id == 0) {
        return;
    }
    const auto& current_slots = object.unit != nullptr ?
        object.unit->equipment_slots : object.equipment_slots;
    if (std::find(current_slots.begin(), current_slots.end(), effect_id) !=
        current_slots.end()) {
        return;
    }
    // Opcode 0x5e checks all six raw +0x30..+0x44 slots for duplicates, but
    // only +0x30..+0x3c are mutable insertion candidates.  The last two are
    // passive/reserved equipment fields.
    constexpr std::size_t kMutableSlotCount = 4;
    for (std::size_t slot = 0; slot < kMutableSlotCount; ++slot) {
        if (current_slots[slot] != 0) {
            continue;
        }
        object.equipment_slots[slot] = effect_id;
        if (object.unit != nullptr) {
            object.unit->equipment_slots[slot] = effect_id;
            object.unit->item_slots[slot] = effect_id;
        }
        break;
    }
}

void remove_equipment_effect_slot(GameplayScriptTriggerObjectState& object, u32 effect_id) {
    if ((object_command_flags(object) & 2u) == 0) {
        return;
    }
    constexpr std::size_t kMutableSlotCount = 4;
    const auto& current_slots = object.unit != nullptr ?
        object.unit->equipment_slots : object.equipment_slots;
    for (std::size_t slot = 0; slot < kMutableSlotCount; ++slot) {
        if (current_slots[slot] != effect_id) {
            continue;
        }
        object.equipment_slots[slot] = 0;
        if (object.unit != nullptr) {
            object.unit->equipment_slots[slot] = 0;
            object.unit->item_slots[slot] = 0;
        }
    }
}

std::vector<u32> active_object_indices(const GameplayScriptTriggerState& state) {
    if (!state.condition_context.active_object_order.empty()) {
        return state.condition_context.active_object_order;
    }

    std::vector<u32> indices;
    indices.reserve(state.objects.size());
    for (u32 index = 1; index < state.objects.size(); ++index) {
        if (object_alive(object_state(state, index))) {
            indices.push_back(index);
        }
    }
    return indices;
}

const GameplayScriptArea* area_state(const GameplayScriptTriggerState& state,
    u32 area_index) {
    if (area_index >= state.areas.size()) {
        return nullptr;
    }
    return &state.areas[area_index];
}

GameplayScriptTriggerGroup* group_state(GameplayScriptTriggerState& state,
    u32 group_index) {
    if (group_index >= state.groups.size()) {
        return nullptr;
    }
    return &state.groups[group_index];
}

const GameplayScriptTriggerGroup* group_state(const GameplayScriptTriggerState& state,
    u32 group_index) {
    if (group_index >= state.groups.size()) {
        return nullptr;
    }
    return &state.groups[group_index];
}

u32 signed_group_prefix_limit(const GameplayScriptTriggerGroup& group) {
    const i32 signed_reference_count =
        signed_i32_from_wrapped_u32(group.reference_count);
    if (signed_reference_count <= 0) {
        return 0;
    }
    return std::min<u32>(static_cast<u32>(signed_reference_count),
        kGameplayScriptTriggerReferencesPerGroup);
}

u32 group_alive_count(const GameplayScriptTriggerState& state,
    const GameplayScriptTriggerGroup& group) {
    u32 count = 0;
    const u32 limit = signed_group_prefix_limit(group);
    for (u32 slot = 0; slot < limit; ++slot) {
        const GameplayScriptTriggerObjectState* object =
            object_state(state, group.object_indices[slot]);
        if (object != nullptr &&
            (gameplay_script_object_runtime_flags(*object) & 4u) == 0) {
            ++count;
        }
    }
    return count;
}

u32 group_dead_count(const GameplayScriptTriggerState& state,
    const GameplayScriptTriggerGroup& group) {
    u32 count = 0;
    const u32 limit = signed_group_prefix_limit(group);
    for (u32 slot = 0; slot < limit; ++slot) {
        const u32 object_index = group.object_indices[slot];
        const GameplayScriptTriggerObjectState* object =
            object_state(state, object_index);
        if (object != nullptr &&
            (gameplay_script_object_runtime_flags(*object) & 4u) != 0) {
            ++count;
        }
    }
    return count;
}

bool group_all_dead(const GameplayScriptTriggerState& state,
    const GameplayScriptTriggerGroup& group) {
    const u32 limit = signed_group_prefix_limit(group);
    for (u32 slot = 0; slot < limit; ++slot) {
        const u32 object_index = group.object_indices[slot];
        const GameplayScriptTriggerObjectState* object =
            object_state(state, object_index);
        if (object != nullptr &&
            (gameplay_script_object_runtime_flags(*object) & 4u) == 0) {
            return false;
        }
    }
    return true;
}

const GameplayScriptTriggerObjectState* first_group_slot_object(
    const GameplayScriptTriggerState& state, const GameplayScriptTriggerGroup& group) {
    return object_state(state, group.object_indices[0]);
}

GameplayScriptTriggerObjectState* first_group_slot_object(
    GameplayScriptTriggerState& state, const GameplayScriptTriggerGroup& group) {
    // Several command opcodes use the raw first pointer, even when that unit
    // is already dead.  They do not search forward for the first live entry.
    return object_state(state, group.object_indices[0]);
}

u32 count_group_objects_in_area(const GameplayScriptTriggerState& state,
    const GameplayScriptTriggerGroup& group, const GameplayScriptArea& area) {
    u32 count = 0;
    const u32 limit = std::min<u32>(
        group.reference_count, kGameplayScriptTriggerReferencesPerGroup);
    for (u32 slot = 0; slot < limit; ++slot) {
        const GameplayScriptTriggerObjectState* object =
            object_state(state, group.object_indices[slot]);
        if (object_alive(object) && area_contains_object(area, *object)) {
            ++count;
        }
    }
    return count;
}

u32 count_raw_group_objects_in_area(const GameplayScriptTriggerState& state,
    const GameplayScriptTriggerGroup& group, const GameplayScriptArea& area) {
    u32 count = 0;
    const u32 limit = signed_group_prefix_limit(group);
    for (u32 slot = 0; slot < limit; ++slot) {
        const GameplayScriptTriggerObjectState* object =
            object_state(state, group.object_indices[slot]);
        if (object != nullptr &&
            (gameplay_script_object_runtime_flags(*object) & 4u) == 0 &&
            area_contains_object(area, *object)) {
            ++count;
        }
    }
    return count;
}

template <typename Func>
void for_each_group_object(GameplayScriptTriggerState& state,
    GameplayScriptTriggerGroup& group, Func func) {
    const u32 limit = std::min<u32>(
        group.reference_count, kGameplayScriptTriggerReferencesPerGroup);
    for (u32 slot = 0; slot < limit; ++slot) {
        GameplayScriptTriggerObjectState* object =
            object_state(state, group.object_indices[slot]);
        if (object != nullptr) {
            func(*object);
        }
    }
}

template <typename Func>
void for_each_group_slot_object(GameplayScriptTriggerState& state,
    GameplayScriptTriggerGroup& group, Func func) {
    // Most command opcodes scan the fixed 64-pointer group array after first
    // checking reference_count.  Removed references leave sparse zero holes;
    // reference_count is a live count, not a compact prefix length.
    for (u32 object_index : group.object_indices) {
        GameplayScriptTriggerObjectState* object = object_state(state, object_index);
        if (object != nullptr) {
            func(*object);
        }
    }
}

i32 area_center_x(const GameplayScriptArea& area) {
    return area.left + (area.right - area.left) / 2;
}

i32 area_center_y(const GameplayScriptArea& area) {
    return area.top + (area.bottom - area.top) / 2;
}

i32 area_translation_center_x(const GameplayScriptArea& area) {
    return static_cast<i32>(
        static_cast<u32>(area.left) + static_cast<u32>(area.right)) / 2;
}

i32 area_translation_center_y(const GameplayScriptArea& area) {
    return static_cast<i32>(
        static_cast<u32>(area.top) + static_cast<u32>(area.bottom)) / 2;
}

i32 wrap_add_i32(i32 left, i32 right) {
    return static_cast<i32>(
        static_cast<u32>(left) + static_cast<u32>(right));
}

i32 wrap_sub_i32(i32 left, i32 right) {
    return static_cast<i32>(
        static_cast<u32>(left) - static_cast<u32>(right));
}

bool relocate_script_object_strict(GameplayScriptTriggerState& state,
    GameplayScriptTriggerObjectState& object, i32 candidate_x, i32 candidate_y) {
    UnitMovementUnit* unit = object.unit;
    if (unit == nullptr) {
        return false;
    }

    i32 placed_x = candidate_x;
    i32 placed_y = candidate_y;
    if (state.opcode_context.find_strict_placement == nullptr ||
        !state.opcode_context.find_strict_placement(
            *unit, placed_x, placed_y,
            state.opcode_context.strict_placement_user)) {
        return false;
    }

    // Original 0x00417080/0x00417cdc updates only the world point and the
    // aligned current-cell cache.  Destination/path/next-path/anchor remain
    // untouched.  The idle transition is immediate, so later triggers in the
    // same phase must observe the new state through the script mirror too.
    unit->x = placed_x;
    unit->y = placed_y;
    unit->current_cell_x = placed_x & ~0x1f;
    unit->current_cell_y = placed_y & ~0x1f;
    UnitCommandContext command_context{};
    HandleUnitReturnToIdleState(command_context, *unit);

    object.x = unit->x;
    object.y = unit->y;
    object.current_cell_x = unit->current_cell_x;
    object.current_cell_y = unit->current_cell_y;
    object.command_state_raw = unit->command_state;
    object.script_state = unit->command_state & 0x00ffffffu;
    object.animation_frame = unit->animation_frame;
    return true;
}

void set_runtime_trigger_state(GameplayScriptTriggerState& state,
    u32 trigger_index, u8 trigger_state) {
    if (trigger_index >= state.triggers.size()) {
        return;
    }
    state.triggers[trigger_index].state = trigger_state;
    encode_trigger(state, trigger_index);
}

void set_runtime_trigger_enabled(GameplayScriptTriggerState& state,
    u32 trigger_index, bool enabled) {
    if (trigger_index >= state.triggers.size()) {
        return;
    }
    state.triggers[trigger_index].trigger_enabled = enabled;
    // Opcodes 0x5b/0x5c write only raw trigger byte +3.  Re-encoding the
    // complete 0x608-byte record here can publish unrelated mirror fields
    // that the original command never touches.
    const std::size_t enabled_offset = kGameplayScriptTriggerRuntimeOffset +
        static_cast<std::size_t>(trigger_index) *
            kGameplayScriptTriggerRuntimeRecordSize + 3;
    if (enabled_offset < state.serialized_triggers.size()) {
        state.serialized_triggers[enabled_offset] = enabled ? 1 : 0;
    }
}

std::string command_string_from(
    const std::array<u32, 0x155>& command, std::size_t word_offset) {
    if (word_offset >= command.size()) {
        return {};
    }

    const char* bytes = reinterpret_cast<const char*>(command.data() + word_offset);
    const std::size_t max_length = (command.size() - word_offset) * sizeof(u32);
    std::size_t length = 0;
    while (length < max_length && bytes[length] != '\0') {
        ++length;
    }
    return std::string(bytes, length);
}

void set_text_overlay(GameplayScriptOpcodeContext& context, i32 x, i32 y,
    const std::string& text, u32 counter_owner = 0) {
    context.text_overlay_active = true;
    context.text_x = x;
    context.text_y = y;
    context.text_counter_owner = counter_owner;
    context.text_overlay = text;
}

void append_spawn_request(GameplayScriptTriggerState& state, u32 opcode, u32 owner_id,
    u32 type_or_effect_id, i32 x, i32 y, bool map_effect, bool remove_from_area = false,
    const GameplayScriptArea* area = nullptr) {
    GameplayScriptSpawnRequest request{};
    request.opcode = opcode;
    request.owner_id = owner_id;
    request.type_or_effect_id = type_or_effect_id;
    request.x = x;
    request.y = y;
    request.map_effect = map_effect;
    request.remove_from_area = remove_from_area;
    if (area != nullptr) {
        request.area_left = area->left;
        request.area_top = area->top;
        request.area_right = area->right;
        request.area_bottom = area->bottom;
        request.has_area_bounds = true;
    }
    if (state.opcode_context.spawn_immediate != nullptr) {
        state.opcode_context.spawn_immediate(
            request, state.opcode_context.spawn_immediate_user);
        return;
    }
    state.opcode_context.spawn_requests.push_back(request);
}

void append_definition_name_request(GameplayScriptTriggerState& state,
    u32 type_id, std::string suffix) {
    GameplayScriptUnitNameAppendRequest request{};
    request.type_id = type_id;
    request.suffix = std::move(suffix);
    state.opcode_context.unit_name_append_requests.push_back(
        std::move(request));
}

u32 trigger_runtime_id(const GameplayScriptTriggerState& state,
    const GameplayScriptTriggerRuntimeRecord& trigger) {
    const GameplayScriptTriggerRuntimeRecord* first = state.triggers.data();
    const GameplayScriptTriggerRuntimeRecord* last = first + state.triggers.size();
    if (&trigger < first || &trigger >= last) {
        return 0xffffffffu;
    }
    return static_cast<u32>(&trigger - first) + 1;
}

std::size_t group_offset(u32 group_index) {
    return kGameplayScriptTriggerGroupOffset +
        static_cast<std::size_t>(group_index) * kGameplayScriptTriggerGroupRecordSize;
}

std::size_t area_offset(u32 area_index) {
    return kGameplayScriptAreaOffset +
        static_cast<std::size_t>(area_index) * kGameplayScriptAreaRecordSize;
}

std::size_t trigger_offset(u32 trigger_index) {
    return kGameplayScriptTriggerRuntimeOffset +
        static_cast<std::size_t>(trigger_index) * kGameplayScriptTriggerRuntimeRecordSize;
}

void decode_area(GameplayScriptTriggerState& state, u32 area_index) {
    const std::size_t base = area_offset(area_index);
    if (base + 0x11 > state.serialized_triggers.size()) {
        state.areas[area_index] = GameplayScriptArea{};
        return;
    }

    GameplayScriptArea area{};
    area.left = static_cast<i32>(read_le_u32(state.serialized_triggers, base + 0x00));
    area.top = static_cast<i32>(read_le_u32(state.serialized_triggers, base + 0x04));
    area.right = static_cast<i32>(read_le_u32(state.serialized_triggers, base + 0x08));
    area.bottom = static_cast<i32>(read_le_u32(state.serialized_triggers, base + 0x0c));
    area.active = state.serialized_triggers[base + 0x10] != 0;
    state.areas[area_index] = area;
}

void decode_group(GameplayScriptTriggerState& state, u32 group_index) {
    const std::size_t base = group_offset(group_index);
    if (base + 0x12d > state.serialized_triggers.size()) {
        state.groups[group_index] = GameplayScriptTriggerGroup{};
        return;
    }

    GameplayScriptTriggerGroup group{};
    group.active = state.serialized_triggers[base + 0x12c] != 0;
    group.timestamp_tick = read_le_u32(state.serialized_triggers, base + 0x20);
    group.reference_count = read_le_u32(state.serialized_triggers, base + 0x28);
    for (u32 i = 0; i < kGameplayScriptTriggerReferencesPerGroup; ++i) {
        group.object_indices[i] = read_le_u32(state.serialized_triggers, base + 0x2c + i * 4);
    }
    state.groups[group_index] = group;
}

void encode_group(GameplayScriptTriggerState& state, u32 group_index) {
    const std::size_t base = group_offset(group_index);
    if (base + 0x12d > state.serialized_triggers.size()) {
        return;
    }

    const GameplayScriptTriggerGroup& group = state.groups[group_index];
    state.serialized_triggers[base + 0x12c] = group.active ? 1 : 0;
    write_le_u32(state.serialized_triggers, base + 0x20, group.timestamp_tick);
    write_le_u32(state.serialized_triggers, base + 0x28, group.reference_count);
    for (u32 i = 0; i < kGameplayScriptTriggerReferencesPerGroup; ++i) {
        write_le_u32(state.serialized_triggers, base + 0x2c + i * 4,
            group.object_indices[i]);
    }
}

void decode_trigger(GameplayScriptTriggerState& state, u32 trigger_index) {
    const std::size_t base = trigger_offset(trigger_index);
    if (base + kGameplayScriptTriggerRuntimeRecordSize > state.serialized_triggers.size()) {
        state.triggers[trigger_index] = GameplayScriptTriggerRuntimeRecord{};
        return;
    }

    GameplayScriptTriggerRuntimeRecord trigger{};
    trigger.state = state.serialized_triggers[base + 0x00];
    trigger.blocked = state.serialized_triggers[base + 0x01];
    trigger.condition_enabled = state.serialized_triggers[base + 0x02] != 0;
    trigger.trigger_enabled = state.serialized_triggers[base + 0x03] != 0;
    trigger.last_fired_tick = read_le_u32(state.serialized_triggers, base + 0x04);
    for (u32 i = 0; i < trigger.condition_words.size(); ++i) {
        trigger.condition_words[i] = read_le_u32(state.serialized_triggers, base + 0x90 + i * 4);
    }
    for (u32 i = 0; i < trigger.command_words.size(); ++i) {
        trigger.command_words[i] = read_le_u32(state.serialized_triggers, base + 0xb4 + i * 4);
    }
    trigger.owner_phase_lookup = trigger.command_words[0];
    state.triggers[trigger_index] = trigger;
}

void encode_trigger(GameplayScriptTriggerState& state, u32 trigger_index) {
    const std::size_t base = trigger_offset(trigger_index);
    if (base + kGameplayScriptTriggerRuntimeRecordSize > state.serialized_triggers.size()) {
        return;
    }

    const GameplayScriptTriggerRuntimeRecord& trigger = state.triggers[trigger_index];
    state.serialized_triggers[base + 0x00] = trigger.state;
    state.serialized_triggers[base + 0x01] = trigger.blocked;
    state.serialized_triggers[base + 0x02] = trigger.condition_enabled ? 1 : 0;
    state.serialized_triggers[base + 0x03] = trigger.trigger_enabled ? 1 : 0;
    write_le_u32(state.serialized_triggers, base + 0x04, trigger.last_fired_tick);
    for (u32 i = 0; i < trigger.condition_words.size(); ++i) {
        write_le_u32(state.serialized_triggers, base + 0x90 + i * 4,
            trigger.condition_words[i]);
    }
    for (u32 i = 0; i < trigger.command_words.size(); ++i) {
        write_le_u32(state.serialized_triggers, base + 0xb4 + i * 4,
            trigger.command_words[i]);
    }
}

void decode_trigger_state(GameplayScriptTriggerState& state) {
    for (u32 area = 0; area < kGameplayScriptAreaCount; ++area) {
        decode_area(state, area);
    }
    for (u32 group = 0; group < kGameplayScriptTriggerGroupCount; ++group) {
        decode_group(state, group);
    }
    for (u32 trigger = 0; trigger < kGameplayScriptTriggerRuntimeCount; ++trigger) {
        decode_trigger(state, trigger);
    }
}

bool default_condition_result(GameplayScriptTriggerState& state,
    GameplayScriptTriggerRuntimeRecord& trigger,
    const GameplayScriptTriggerCallbacks& callbacks) {
    if (callbacks.check_condition != nullptr) {
        return callbacks.check_condition(trigger, callbacks.user);
    }
    if (state.condition_context.enabled) {
        return EvaluateGameplayScriptTriggerCondition(state, trigger);
    }
    return true;
}

bool default_command_result(GameplayScriptTriggerState& state,
    GameplayScriptTriggerRuntimeRecord& trigger,
    const GameplayScriptTriggerCallbacks& callbacks) {
    if (callbacks.dispatch_command != nullptr) {
        return callbacks.dispatch_command(trigger, callbacks.user);
    }
    if (state.opcode_context.enabled) {
        return DispatchGameplayScriptOpcode(state, trigger);
    }
    return true;
}

}

u32 CalculateGameplayScriptTextDurationFrames(const char* text) {
    const std::size_t length = text != nullptr ? std::strlen(text) : 0;
    return static_cast<u32>((length >> 1) + 0x50);
}

void ResetGameplayScriptDialogRuntimeState(GameplayScriptDialogState& state) {
    state.active_cue_id = 0;
    state.elapsed_frames = 0;
    state.force_complete = false;
    state.condition13_latch = false;
    state.advance_flags.fill(0);
    state.previous_advance_flag = nullptr;
    state.visible_text.clear();
}

void HandleGameplayScriptTextEffectCue(GameplayScriptDialogState& state,
    const GameplayScriptTextCueCommand& command, u32 frame_tick) {
    state.visible_text = command.text != nullptr ? command.text : "";
    state.text_x = command.use_custom_position ? command.x : 100;
    state.text_y = command.use_custom_position ? command.y : 300;
    state.last_duration_frames =
        CalculateGameplayScriptTextDurationFrames(state.visible_text.c_str());
    state.last_effect_entry = command.effect_entry_index;

    if (state.active_cue_id == command.cue_id) {
        if (state.last_frame_tick != frame_tick) {
            state.last_frame_tick = frame_tick;
            ++state.elapsed_frames;
        }

        if (command.wait_for_effect && state.effect_playback_enabled) {
            const int status =
                GetMilesEffectPlaylistEntryStatus(command.effect_entry_index);
            state.elapsed_frames = status == 0 ? state.last_duration_frames :
                state.last_duration_frames - 1;
        }
    } else {
        state.active_cue_id = command.cue_id;
        state.elapsed_frames = 0;
        if (state.previous_advance_flag != nullptr) {
            *state.previous_advance_flag = 1;
        }
        state.previous_advance_flag = state.advance_flags.data();

        if (command.wait_for_effect && state.effect_playback_enabled &&
            GetMilesEffectPlaylistEntryStatus(command.effect_entry_index) == 0) {
            PlayMilesEffectPlaylistEntry(command.effect_entry_index);
        }
    }

    if (state.force_complete) {
        state.elapsed_frames = state.last_duration_frames;
        if (state.effect_playback_enabled &&
            GetMilesEffectPlaylistEntryStatus(command.effect_entry_index) != 0) {
            CloseMilesEffectPlaylistEntry(command.effect_entry_index);
        }
        state.force_complete = false;
    }

    if (state.elapsed_frames < state.last_duration_frames) {
        state.advance_flags[1] = 1;
    } else {
        state.advance_flags[1] = 0;
        state.advance_flags[2] = 0;
        state.active_cue_id = 0;
    }
}

void HandleGameplayScriptImmediateEffectCue(GameplayScriptDialogState& state,
    bool trigger_enabled, u32 effect_entry_index) {
    if (trigger_enabled && state.effect_playback_enabled) {
        PlayMilesEffectPlaylistEntry(effect_entry_index);
    }
}

void InitializeGameplayScriptTriggerState(GameplayScriptTriggerState& state,
    u32 serialized_capacity) {
    if (serialized_capacity == 0) {
        serialized_capacity = kGameplayScriptTriggerRuntimeOffset +
            kGameplayScriptTriggerRuntimeCount * kGameplayScriptTriggerRuntimeRecordSize;
    }

    state.serialized_capacity = serialized_capacity;
    state.loaded_byte_count = 0;
    state.post_nonzero_phase_resets = 0;
    state.serialized_triggers.assign(serialized_capacity, 0);
    state.owner_phase_lookup.clear();
    state.objects.clear();
    state.condition_context = GameplayScriptConditionContext{};
    state.opcode_context = GameplayScriptOpcodeContext{};
    for (GameplayScriptTriggerRuntimeRecord& trigger : state.triggers) {
        trigger = GameplayScriptTriggerRuntimeRecord{};
        trigger.trigger_enabled = true;
    }
    for (u32 trigger = 0; trigger < kGameplayScriptTriggerRuntimeCount; ++trigger) {
        const std::size_t base = trigger_offset(trigger);
        if (base + 3 < state.serialized_triggers.size()) {
            state.serialized_triggers[base + 0x03] = 1;
        }
    }
    for (GameplayScriptTriggerGroup& group : state.groups) {
        group = GameplayScriptTriggerGroup{};
    }
    for (GameplayScriptArea& area : state.areas) {
        area = GameplayScriptArea{};
    }
}

bool LoadGameplayScriptRecord5ExactSize(const char* archive_name, void* destination,
    u32 byte_count) {
    if (archive_name == nullptr || destination == nullptr) {
        return false;
    }

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, 5) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }
    if (reader.entry.original_size != byte_count) {
        CloseTrcRecordReader(reader);
        return false;
    }
    if (!ReadOpenTrcRecordBytes(reader, destination, byte_count)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    CloseTrcRecordReader(reader);
    return true;
}

bool LoadGameplayScriptTriggersRecord(GameplayScriptTriggerState& state,
    const char* archive_name, u32 record_index, u32 serialized_capacity) {
    if (archive_name == nullptr) {
        return false;
    }

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    const u32 record_size = reader.entry.original_size;
    if (serialized_capacity == 0) {
        serialized_capacity = std::max<u32>(state.serialized_capacity,
            record_size);
    }
    if (record_size > serialized_capacity) {
        CloseTrcRecordReader(reader);
        return false;
    }

    InitializeGameplayScriptTriggerState(state, serialized_capacity);
    if (!ReadOpenTrcRecordBytes(reader, state.serialized_triggers.data(), record_size)) {
        CloseTrcRecordReader(reader);
        return false;
    }
    CloseTrcRecordReader(reader);

    state.loaded_byte_count = record_size;
    decode_trigger_state(state);
    return true;
}

bool SaveGameplayScriptTriggersRecord(const GameplayScriptTriggerState& state,
    const char* archive_name) {
    if (archive_name == nullptr || state.serialized_triggers.empty()) {
        return false;
    }
    return HandleTrcMemoryRecordAppend(archive_name, "TRIGGERS",
        state.serialized_triggers.data(), state.serialized_triggers.size(), 0x32, 2);
}

u32 CountActiveGameplayScriptAreasBefore(const GameplayScriptTriggerState& state,
    u32 limit) {
    const u32 capped = std::min<u32>(limit, kGameplayScriptAreaCount);
    u32 count = 0;
    for (u32 i = 0; i < capped; ++i) {
        if (state.areas[i].active) {
            ++count;
        }
    }
    return count;
}

i32 FindNthActiveGameplayScriptArea(const GameplayScriptTriggerState& state,
    u32 ordinal) {
    u32 count = 0;
    for (u32 i = 0; i < kGameplayScriptAreaCount; ++i) {
        if (!state.areas[i].active) {
            continue;
        }
        if (count == ordinal) {
            return static_cast<i32>(i);
        }
        ++count;
    }
    return 0;
}

i32 FindFreeGameplayScriptArea(const GameplayScriptTriggerState& state) {
    for (u32 i = 0; i < kGameplayScriptAreaCount; ++i) {
        if (!state.areas[i].active) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

u32 CountActiveGameplayScriptGroupsBefore(const GameplayScriptTriggerState& state,
    u32 limit) {
    const u32 capped = std::min<u32>(limit, kGameplayScriptTriggerGroupCount);
    u32 count = 0;
    for (u32 i = 0; i < capped; ++i) {
        if (state.groups[i].active) {
            ++count;
        }
    }
    return count;
}

i32 FindNthActiveGameplayScriptGroup(const GameplayScriptTriggerState& state,
    u32 ordinal) {
    u32 count = 0;
    for (u32 i = 0; i < kGameplayScriptTriggerGroupCount; ++i) {
        if (!state.groups[i].active) {
            continue;
        }
        if (count == ordinal) {
            return static_cast<i32>(i);
        }
        ++count;
    }
    return 0;
}

i32 FindFreeGameplayScriptGroup(const GameplayScriptTriggerState& state) {
    for (u32 i = 0; i < kGameplayScriptTriggerGroupCount; ++i) {
        if (!state.groups[i].active) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

u32 CountActiveGameplayScriptRuntimeTriggersBefore(
    const GameplayScriptTriggerState& state, u32 limit) {
    const u32 capped = std::min<u32>(limit, kGameplayScriptTriggerRuntimeCount);
    u32 count = 0;
    for (u32 i = 0; i < capped; ++i) {
        if (state.triggers[i].condition_enabled) {
            ++count;
        }
    }
    return count;
}

i32 FindNthActiveGameplayScriptRuntimeTrigger(
    const GameplayScriptTriggerState& state, u32 ordinal) {
    u32 count = 0;
    for (u32 i = 0; i < kGameplayScriptTriggerRuntimeCount; ++i) {
        if (!state.triggers[i].condition_enabled) {
            continue;
        }
        if (count == ordinal) {
            return static_cast<i32>(i);
        }
        ++count;
    }
    return 0;
}

i32 FindFreeGameplayScriptRuntimeTrigger(const GameplayScriptTriggerState& state) {
    for (u32 i = 0; i < kGameplayScriptTriggerRuntimeCount; ++i) {
        if (!state.triggers[i].condition_enabled) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

bool EvaluateGameplayScriptTriggerCondition(GameplayScriptTriggerState& state,
    GameplayScriptTriggerRuntimeRecord& trigger) {
    auto& words = trigger.condition_words;
    const GameplayScriptConditionContext& context = state.condition_context;

    // Original FUN_0041dc60 consumes total score in cases 2/7/8, primary
    // resources in cases 3/4/5, and 0x70726c + 0x70728c kills in cases 9/10.
    switch (words[0]) {
    case 0:
        return true;
    case 1:
        return state.current_tick >= words[1];
    case 2: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner_active(context, words[1]) &&
            owner->score >= static_cast<i32>(words[2]);
    }
    case 3: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner_active(context, words[1]) &&
            owner->resource_a >= static_cast<i32>(words[2]);
    }
    case 4: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        if (!owner_active(context, words[1])) {
            return false;
        }
        for (u32 other = 0; other < kGameplayScriptOwnerCount; ++other) {
            const GameplayScriptOwnerConditionState* other_owner =
                owner_state(context, other);
            if (!owner_active(context, other) ||
                !owner_has_active_objects(context, other)) {
                continue;
            }
            if (owner->resource_a < other_owner->resource_a) {
                return false;
            }
        }
        return true;
    }
    case 5: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        if (!owner_active(context, words[1])) {
            return false;
        }
        for (u32 other = 0; other < kGameplayScriptOwnerCount; ++other) {
            const GameplayScriptOwnerConditionState* other_owner =
                owner_state(context, other);
            if (!owner_active(context, other) ||
                !owner_has_active_objects(context, other)) {
                continue;
            }
            if (owner->resource_a >= other_owner->resource_a) {
                return false;
            }
        }
        return true;
    }
    case 7: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        if (!owner_active(context, words[1])) {
            return false;
        }
        for (u32 other = 0; other < kGameplayScriptOwnerCount; ++other) {
            const GameplayScriptOwnerConditionState* other_owner =
                owner_state(context, other);
            if (!owner_active(context, other) ||
                !owner_has_active_objects(context, other)) {
                continue;
            }
            if (owner->score < other_owner->score) {
                return false;
            }
        }
        return true;
    }
    case 8: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        if (!owner_active(context, words[1])) {
            return false;
        }
        for (u32 other = 0; other < kGameplayScriptOwnerCount; ++other) {
            const GameplayScriptOwnerConditionState* other_owner =
                owner_state(context, other);
            if (!owner_active(context, other) ||
                !owner_has_active_objects(context, other)) {
                continue;
            }
            if (owner->score >= other_owner->score) {
                return false;
            }
        }
        return true;
    }
    case 9: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        if (!owner_active(context, words[1])) {
            return false;
        }
        for (u32 other = 0; other < kGameplayScriptOwnerCount; ++other) {
            const GameplayScriptOwnerConditionState* other_owner =
                owner_state(context, other);
            if (!owner_active(context, other) ||
                !owner_has_active_objects(context, other)) {
                continue;
            }
            if (static_cast<u32>(owner->metric) <
                static_cast<u32>(other_owner->metric)) {
                return false;
            }
        }
        return true;
    }
    case 10: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        if (!owner_active(context, words[1])) {
            return false;
        }
        for (u32 other = 0; other < kGameplayScriptOwnerCount; ++other) {
            const GameplayScriptOwnerConditionState* other_owner =
                owner_state(context, other);
            if (!owner_active(context, other) ||
                !owner_has_active_objects(context, other)) {
                continue;
            }
            if (static_cast<u32>(owner->metric) >=
                static_cast<u32>(other_owner->metric)) {
                return false;
            }
        }
        return true;
    }
    case 0x0b: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[2]);
        return owner_active(context, words[2]) &&
            static_cast<u32>(owner->metric) >= words[1];
    }
    case 0x0c: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        if (!owner_active(context, words[1])) {
            return false;
        }
        u32 count = 0;
        for (u32 other = 0; other < kGameplayScriptOwnerCount; ++other) {
            const u32 mask = 1u << other;
            if ((owner->blocked_relation_mask & mask) == 0 &&
                (context.owner_population_demand[other] != 0 ||
                    context.owner_population_capacity[other] != 0)) {
                ++count;
            }
        }
        return count == words[2];
    }
    case 0x0d: {
        if (!owner_active(context, words[1])) {
            return false;
        }
        for (u32 index : active_object_indices(state)) {
            const GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object_alive(object) && object->owner_id == words[1] &&
                object->stat_54 >= words[2]) {
                return true;
            }
        }
        return false;
    }
    case 0x0e: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        const GameplayScriptArea* area = area_state(state, words[2]);
        return group != nullptr && area != nullptr &&
            count_raw_group_objects_in_area(state, *group, *area) != 0;
    }
    case 0x0f: {
        GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        if (group == nullptr) {
            return false;
        }
        if (words[3] == 0) {
            group->timestamp_tick = state.current_tick;
            words[3] = 1;
            encode_group(state, words[1]);
        }
        return state.current_tick - group->timestamp_tick >= words[2];
    }
    case 0x10: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[2]);
        const GameplayScriptArea* area = area_state(state, words[3]);
        if (group == nullptr || area == nullptr) {
            return false;
        }
        const u32 count = count_raw_group_objects_in_area(state, *group, *area);
        return count != 0 && static_cast<i32>(count) >= static_cast<i32>(words[1]);
    }
    case 0x11: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        return group != nullptr && group_all_dead(state, *group);
    }
    case 0x12: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        return group != nullptr &&
            static_cast<i32>(group_alive_count(state, *group)) <=
                static_cast<i32>(words[2]);
    }
    case 0x13: {
        GameplayScriptDialogState& dialog = gameplay_script_dialog_state();
        if (dialog.force_complete && dialog.last_effect_entry == 0) {
            dialog.force_complete = false;
            dialog.condition13_latch = true;
        }
        return dialog.condition13_latch;
    }
    case 0x14: {
        if (words[1] >= state.triggers.size()) {
            return false;
        }
        const GameplayScriptTriggerRuntimeRecord& other = state.triggers[words[1]];
        return other.state != 0 && state.current_tick - other.last_fired_tick >= words[2];
    }
    case 0x15: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner != nullptr && owner->trigger_counter == words[2];
    }
    case 0x16: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        return group != nullptr &&
            static_cast<i32>(group_dead_count(state, *group)) >
                static_cast<i32>(words[2]);
    }
    case 0x17:
        if (state.current_tick - words[2] >= words[1]) {
            words[2] = state.current_tick;
            return true;
        }
        return false;
    case 0x18: {
        if (words[2] > 4) {
            return false;
        }
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        const GameplayScriptTriggerObjectState* object =
            group != nullptr ? first_group_slot_object(state, *group) : nullptr;
        return object != nullptr &&
            (gameplay_script_object_runtime_flags(*object) & 4u) == 0 &&
            object_stat_by_mode(*object, words[2]) <= words[3];
    }
    case 0x19: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner_active(context, words[1]) &&
            words[2] < owner->script_values.size() &&
            owner->script_values[words[2]] == words[3];
    }
    case 0x1a: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        if (group == nullptr) {
            return false;
        }
        u32 count = 0;
        const u32 limit = signed_group_prefix_limit(*group);
        for (u32 slot = 0; slot < limit; ++slot) {
            const GameplayScriptTriggerObjectState* object =
                object_state(state, group->object_indices[slot]);
            if (object == nullptr ||
                (gameplay_script_object_runtime_flags(*object) & 4u) != 0) {
                continue;
            }
            count += object_equipment_count(*object, words[2]);
            if (static_cast<i32>(count) >= static_cast<i32>(words[3])) {
                return true;
            }
        }
        return false;
    }
    case 0x1c: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner_active(context, words[1]) &&
            words[2] < owner->script_values.size() &&
            owner->script_values[words[2]] != 0;
    }
    case 0x1d:
        return !owner_active(context, words[1]) ||
            (context.owner_population_demand[words[1]] <= 0 &&
                context.owner_population_capacity[words[1]] <= 0);
    case 0x1e: {
        if (!owner_active(context, words[1])) {
            return false;
        }
        const GameplayScriptArea* area = area_state(state, words[2]);
        if (area == nullptr) {
            return false;
        }
        for (u32 index : active_object_indices(state)) {
            const GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object != nullptr &&
                (gameplay_script_object_runtime_flags(*object) & 4u) == 0 &&
                object->owner_id == words[1] &&
                area_contains_object(*area, *object)) {
                return true;
            }
        }
        return false;
    }
    case 0x1f: {
        if (words[1] >= state.triggers.size() || state.triggers[words[1]].state == 0) {
            return false;
        }
        const GameplayScriptTriggerGroup* group = group_state(state, words[2]);
        const GameplayScriptTriggerObjectState* object =
            group != nullptr ? first_group_slot_object(state, *group) : nullptr;
        const u32 type_flags = object != nullptr && object->unit != nullptr ?
            object->unit->type_flags : (object != nullptr ? object->type_flags : 0);
        return object != nullptr && type_flags == 1;
    }
    case 0x20: {
        const GameplayScriptArea* area = area_state(state, words[1]);
        if (area == nullptr) {
            return false;
        }
        u32 count = 0;
        for (u32 index : active_object_indices(state)) {
            const GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object != nullptr &&
                (gameplay_script_object_runtime_flags(*object) & 4u) == 0 &&
                area_contains_object(*area, *object) &&
                object->type_id == words[2]) {
                ++count;
                if (static_cast<i32>(count) >= static_cast<i32>(words[3])) {
                    return true;
                }
            }
        }
        return false;
    }
    case 0x21: {
        const GameplayScriptArea* area = area_state(state, words[1]);
        if (area == nullptr) {
            return false;
        }
        for (u32 index : active_object_indices(state)) {
            const GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object != nullptr &&
                (gameplay_script_object_runtime_flags(*object) & 4u) == 0 &&
                area_contains_object(*area, *object)) {
                return true;
            }
        }
        return false;
    }
    case 0x22: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[3]);
        const GameplayScriptTriggerObjectState* object =
            group != nullptr ? first_group_slot_object(state, *group) : nullptr;
        return object != nullptr && (object->flags & 4u) == 0 &&
            object_stat_by_mode(*object, words[2]) > words[1];
    }
    case 0x23: {
        const GameplayScriptArea* area = area_state(state, words[1]);
        const GameplayScriptTriggerGroup* group = group_state(state, words[2]);
        return area != nullptr && group != nullptr &&
            count_group_objects_in_area(state, *group, *area) == group->reference_count;
    }
    case 0x24:
        return state.current_tick == words[1];
    case 0x25: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        if (group == nullptr || group->reference_count == 0) {
            return false;
        }
        u32 total = 0;
        const u32 limit = std::min<u32>(
            group->reference_count, kGameplayScriptTriggerReferencesPerGroup);
        for (u32 slot = 0; slot < limit; ++slot) {
            const GameplayScriptTriggerObjectState* object =
                object_state(state, group->object_indices[slot]);
            if (object_alive(object)) {
                total += object->stat_54;
            }
        }
        return words[2] <= total / group->reference_count;
    }
    case 0x26: {
        if (!owner_active(context, words[2])) {
            return false;
        }
        const GameplayScriptArea* area = area_state(state, words[1]);
        if (area == nullptr) {
            return false;
        }
        for (u32 index : active_object_indices(state)) {
            const GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object != nullptr && object->owner_id == words[2] &&
                object_bounds_inside_area(*area, *object)) {
                return true;
            }
        }
        return false;
    }
    case 0x29:
    case 0x2a: {
        const GameplayScriptArea* area = area_state(state, words[1]);
        if (area == nullptr) {
            return false;
        }
        for (u32 index : active_object_indices(state)) {
            const GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object == nullptr || !object_visible_for_area_scan(*object) ||
                !area_contains_object(*area, *object)) {
                continue;
            }
            const bool equals = object_runtime_category(*object) == words[2];
            if ((words[0] == 0x29 && equals) || (words[0] == 0x2a && !equals)) {
                return true;
            }
        }
        return false;
    }
    case 0x2b: {
        const GameplayScriptTriggerGroup* group = group_state(state, words[1]);
        const GameplayScriptArea* area = area_state(state, words[2]);
        return group != nullptr && area != nullptr &&
            count_group_objects_in_area(state, *group, *area) == 0;
    }
    case 0x2c: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner != nullptr && owner->trigger_counter >= words[2];
    }
    case 0x2d: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner != nullptr && owner->trigger_counter <= words[2];
    }
    case 0x2e: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner != nullptr && words[2] < owner->unit_type_counts.size() &&
            owner->unit_type_counts[words[2]] >= words[3];
    }
    case 0x2f: {
        const GameplayScriptOwnerConditionState* owner = owner_state(context, words[1]);
        return owner != nullptr && words[2] < owner->unit_type_counts.size() &&
            owner->unit_type_counts[words[2]] < words[3];
    }
    default:
        return false;
    }
}

bool DispatchGameplayScriptOpcode(GameplayScriptTriggerState& state,
    GameplayScriptTriggerRuntimeRecord& trigger) {
    auto& command = trigger.command_words;

    switch (command[0]) {
    case 0x00:
        state.opcode_context.camera_request_active = true;
        state.opcode_context.camera_x = static_cast<i32>(command[1]);
        state.opcode_context.camera_y = static_cast<i32>(command[2]);
        return true;
    case 0x01: {
        const std::string text = command_string_from(command, 1);
        GameplayScriptTextCueCommand cue{};
        cue.cue_id = trigger_runtime_id(state, trigger);
        cue.text = text.c_str();
        HandleGameplayScriptTextEffectCue(gameplay_script_dialog_state(), cue,
            state.current_tick);
        trigger.blocked = gameplay_script_dialog_state().advance_flags[1] != 0 ? 1 : 0;
        return true;
    }
    case 0x02: {
        if (!owner_active(state.condition_context, command[3])) {
            return true;
        }
        const GameplayScriptArea* area = area_state(state, command[4]);
        if (area == nullptr) {
            return true;
        }
        const i32 x = area_translation_center_x(*area);
        const i32 y = area_translation_center_y(*area);
        const bool map_effect = static_cast<i32>(command[2]) >= 1000;
        if (map_effect) {
            // Original 0x00416ab2 ignores command[1] on the effect branch.
            append_spawn_request(state, command[0], command[3],
                command[2] - 1000u, x, y, true);
            return true;
        }

        // The original uses a DEC/JNZ do-loop and malformed count zero would
        // run 2^32 attempts.  No shipped scenario uses zero; keep malformed
        // input safe while preserving every valid count exactly.
        for (u32 attempt = 0; attempt < command[1]; ++attempt) {
            append_spawn_request(state, command[0], command[3] & 0xffu,
                command[2] & 0xffu, x, y, false);
        }
        return true;
    }
    case 0x03: {
        GameplayScriptDialogState& dialog = gameplay_script_dialog_state();
        const bool has_condition13 = std::any_of(
            state.triggers.begin(), state.triggers.end(),
            [](const GameplayScriptTriggerRuntimeRecord& candidate) {
                return candidate.condition_enabled &&
                    candidate.condition_words[0] == 0x13;
            });
        if (has_condition13 && !dialog.condition13_latch) {
            dialog.condition13_latch = true;
            return true;
        }
        dialog.force_complete = true;
        dialog.last_effect_entry = 0;
        state.opcode_context.stage_result_pending = true;
        state.opcode_context.stage_result = 0;
        return true;
    }
    case 0x04:
        state.opcode_context.stage_result_pending = true;
        state.opcode_context.stage_result = 1;
        return true;
    case 0x05:
        state.opcode_context.stage_result_pending = true;
        state.opcode_context.stage_result = 2;
        return true;
    case 0x06: {
        const GameplayScriptArea* area = area_state(state, command[2]);
        if (area == nullptr) {
            return true;
        }
        for (u32 index : active_object_indices(state)) {
            GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object_alive(object) && area_contains_object(*area, *object)) {
                set_script_object_owner(*object, command[1]);
            }
        }
        rebuild_owner_unit_type_counts(state);
        return true;
    }
    case 0x07: {
        const GameplayScriptArea* area = area_state(state, command[1]);
        if (area == nullptr) {
            return true;
        }
        for (u32 index : active_object_indices(state)) {
            GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object_alive(object) && area_contains_object(*area, *object)) {
                mark_gameplay_script_object_dead(*object);
            }
        }
        return true;
    }
    case 0x08: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        const GameplayScriptArea* area = area_state(state, command[3]);
        GameplayScriptTriggerObjectState* object =
            group != nullptr && group->reference_count != 0 ?
                first_group_slot_object(state, *group) : nullptr;
        if (object != nullptr && object->unit != nullptr && area != nullptr) {
            SetOrQueueUnitAlignedPointCommand06(object->unit, command[2],
                area_center_x(*area), area_center_y(*area), true);
        }
        return true;
    }
    case 0x09: {
        const GameplayScriptArea* source_area = area_state(state, command[1]);
        const GameplayScriptArea* target_area = area_state(state, command[2]);
        if (source_area == nullptr || target_area == nullptr) {
            return true;
        }
        const i32 x = area_center_x(*target_area);
        const i32 y = area_center_y(*target_area);
        for (u32 index : active_object_indices(state)) {
            GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object_alive(object) && area_contains_object(*source_area, *object)) {
                relocate_script_object_strict(state, *object, x, y);
            }
        }
        return true;
    }
    case 0x0a: {
        GameplayScriptTriggerGroup* group = group_state(state, command[2]);
        const GameplayScriptArea* area = area_state(state, command[3]);
        if (group == nullptr || area == nullptr) {
            return true;
        }
        const i32 x = area_center_x(*area);
        const i32 y = area_center_y(*area);
        for_each_group_object(state, *group, [&](GameplayScriptTriggerObjectState& object) {
            if (object.unit != nullptr) {
                DispatchGameplayScriptUnitCommand(command[1], object.unit, nullptr, x, y, false);
            }
        });
        return true;
    }
    case 0x0b:
        for (u32 i = 0; i < state.opcode_context.copied_owner_table_a.size(); ++i) {
            state.opcode_context.copied_owner_table_a[i] = command[1 + i];
            state.opcode_context.copied_owner_table_b[i] = command[9 + i];
            state.condition_context.owners[i].blocked_relation_mask = command[1 + i];
        }
        state.opcode_context.copied_owner_tables_dirty = true;
        return true;
    case 0x0c: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        GameplayScriptTriggerObjectState* object =
            group != nullptr && group->reference_count != 0 ?
                first_group_slot_object(state, *group) : nullptr;
        if (object != nullptr && object->unit != nullptr) {
            SetOrQueueUnitCommand10(object->unit, command[2], true);
        }
        return true;
    }
    case 0x0d:
        state.opcode_context.scenario_message_text = command_string_from(command, 1);
        return true;
    case 0x0e: {
        GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, command[1]);
        if (owner != nullptr) {
            owner->resource_a = static_cast<i32>(command[2]);
            state.opcode_context.owner_resource_dirty[command[1]] = true;
        }
        return true;
    }
    case 0x0f: {
        GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, command[1]);
        if (owner != nullptr) {
            owner->secondary_score = static_cast<i32>(command[2]);
            owner->score = static_cast<i32>(command[2]);
            state.opcode_context.owner_score_component_dirty[command[1]] = true;
            state.opcode_context.owner_score_reset_dirty[command[1]] = true;
        }
        return true;
    }
    case 0x10: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        const GameplayScriptArea* area = area_state(state, command[2]);
        if (group == nullptr || area == nullptr || group->reference_count == 0) {
            return true;
        }
        const i32 target_x = area_translation_center_x(*area);
        const i32 target_y = area_translation_center_y(*area);
        u32 total_x = 0;
        u32 total_y = 0;
        for_each_group_object(state, *group, [&](GameplayScriptTriggerObjectState& object) {
            object.pending_command = {4, 0, target_x, static_cast<u32>(target_y)};
            if (object.unit != nullptr) {
                object.unit->pending_command = {
                    4, 0, target_x, static_cast<u32>(target_y)};
            }
            total_x += static_cast<u32>(object.x);
            total_y += static_cast<u32>(object.y);
        });
        const i32 average_x = static_cast<i32>(total_x) /
            static_cast<i32>(group->reference_count);
        const i32 average_y = static_cast<i32>(total_y) /
            static_cast<i32>(group->reference_count);
        state.opcode_context.camera_request_active = true;
        state.opcode_context.camera_x = average_x;
        state.opcode_context.camera_y = average_y;
        trigger.blocked = area_contains_point(*area, average_x, average_y) ? 0 : 1;
        return true;
    }
    case 0x11:
        set_runtime_trigger_state(state, command[1], 0);
        return true;
    case 0x12:
        set_runtime_trigger_state(state, command[1], 1);
        return true;
    case 0x13: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        const UnitMovementDefinition* definition = nullptr;
        if (state.opcode_context.lifecycle != nullptr &&
            state.opcode_context.lifecycle->callbacks.find_definition != nullptr) {
            definition = state.opcode_context.lifecycle->callbacks.find_definition(
                *state.opcode_context.lifecycle, command[3]);
        }
        if (group != nullptr && definition != nullptr) {
            for_each_group_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                if (object.type_id == command[2]) {
                    convert_script_object_type(object, command[3], *definition,
                        state.opcode_context.lifecycle);
                }
            });
        }
        rebuild_owner_unit_type_counts(state);
        return true;
    }
    case 0x14: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr) {
            const u32 limit = std::min<u32>(
                group->reference_count, kGameplayScriptTriggerReferencesPerGroup);
            for (u32 slot = 0; slot < limit; ++slot) {
                const u32 object_index = group->object_indices[slot];
                if (object_index != 0) {
                    begin_single_gameplay_script_selection_request(
                        state.opcode_context, object_index);
                    break;
                }
            }
        }
        return true;
    }
    case 0x15: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr) {
            for_each_group_object(state, *group, [](GameplayScriptTriggerObjectState& object) {
                mark_gameplay_script_object_dead(object);
            });
        }
        return true;
    }
    case 0x16: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr) {
            for_each_group_slot_object(state, *group,
                [](GameplayScriptTriggerObjectState& object) {
                mutate_gameplay_script_object_runtime_flags(
                    object, 0x80u, 1u);
            });
        }
        return true;
    }
    case 0x17: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr) {
            for_each_group_slot_object(state, *group,
                [](GameplayScriptTriggerObjectState& object) {
                mutate_gameplay_script_object_runtime_flags(
                    object, 1u, 0x80u);
            });
        }
        return true;
    }
    case 0x18: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        const GameplayScriptArea* area = area_state(state, command[2]);
        if (group == nullptr || area == nullptr || group->reference_count == 0) {
            return true;
        }
        u32 total_x = 0;
        u32 total_y = 0;
        for_each_group_slot_object(state, *group,
            [&](GameplayScriptTriggerObjectState& object) {
            total_x += static_cast<u32>(object.x);
            total_y += static_cast<u32>(object.y);
        });
        const i32 average_x =
            static_cast<i32>(total_x) / static_cast<i32>(group->reference_count);
        const i32 average_y =
            static_cast<i32>(total_y) / static_cast<i32>(group->reference_count);
        const i32 fallback_x = area_center_x(*area);
        const i32 fallback_y = area_center_y(*area);
        const i32 delta_x = wrap_sub_i32(
            area_translation_center_x(*area), average_x);
        const i32 delta_y = wrap_sub_i32(
            area_translation_center_y(*area), average_y);
        for_each_group_slot_object(state, *group,
            [&](GameplayScriptTriggerObjectState& object) {
            i32 candidate_x = wrap_add_i32(object.x, delta_x);
            i32 candidate_y = wrap_add_i32(object.y, delta_y);
            if (!area_contains_point(*area, candidate_x, candidate_y)) {
                candidate_x = fallback_x;
                candidate_y = fallback_y;
            }
            relocate_script_object_strict(
                state, object, candidate_x, candidate_y);
        });
        return true;
    }
    case 0x19: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group == nullptr || group->reference_count == 0) {
            return true;
        }
        i32 total_x = 0;
        i32 total_y = 0;
        u32 count = 0;
        for_each_group_slot_object(state, *group,
            [&](GameplayScriptTriggerObjectState& object) {
            total_x += object.x;
            total_y += object.y;
            ++count;
        });
        if (count != 0) {
            state.opcode_context.camera_request_active = true;
            state.opcode_context.camera_x = total_x / static_cast<i32>(count);
            state.opcode_context.camera_y = total_y / static_cast<i32>(count);
        }
        return true;
    }
    case 0x1a: {
        GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, command[1]);
        if (owner != nullptr) {
            ++owner->trigger_counter;
        }
        return true;
    }
    case 0x1b:
        state.opcode_context.global_flag_22358 = false;
        return true;
    case 0x1c:
        state.opcode_context.global_flag_22358 = true;
        return true;
    case 0x1d: {
        const GameplayScriptArea* source_area = area_state(state, command[2]);
        const GameplayScriptArea* target_area = area_state(state, command[3]);
        if (source_area == nullptr || target_area == nullptr) {
            return true;
        }
        const i32 x = area_center_x(*target_area);
        const i32 y = area_center_y(*target_area);
        for (u32 index : active_object_indices(state)) {
            GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object_alive(object) && object->unit != nullptr &&
                object_bounds_inside_area(*source_area, *object)) {
                DispatchGameplayScriptUnitCommand(command[1], object->unit, nullptr, x, y,
                    false);
            }
        }
        return true;
    }
    case 0x1e:
        state.opcode_context.resource_hud_flags |= 0x40;
        state.opcode_context.countdown_x = 400;
        state.opcode_context.countdown_y = 0x0c;
        state.opcode_context.countdown_color = 1;
        state.opcode_context.game_clock_decrements = true;
        state.opcode_context.game_clock_ticks = command[1];
        return true;
    case 0x1f: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr && group->reference_count != 0) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                set_object_stat_by_mode(object, command[2], command[3]);
            });
        }
        return true;
    }
    case 0x20:
        append_definition_name_request(
            state, command[1], command_string_from(command, 2));
        return true;
    case 0x21: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group == nullptr || static_cast<i32>(group->reference_count) <= 0 ||
            state.opcode_context.movement == nullptr) {
            return true;
        }
        const std::string text = command_string_from(command, 2);
        const u32 string_slot =
            InternUnitStringSlot(*state.opcode_context.movement, text.c_str());
        if (string_slot == kInvalidUnitStringSlot) {
            return true;
        }
        for_each_group_slot_object(state, *group,
            [string_slot](GameplayScriptTriggerObjectState& object) {
            object.dynamic_string_slot = string_slot;
            if (object.unit != nullptr) {
                object.unit->string_slot = string_slot;
            }
        });
        return true;
    }
    case 0x22: {
        const std::string text = command_string_from(command, 6);
        GameplayScriptTextCueCommand cue{};
        cue.cue_id = trigger_runtime_id(state, trigger);
        cue.use_custom_position = command[1] != 0;
        cue.x = static_cast<i32>(command[2]);
        cue.y = static_cast<i32>(command[3]);
        cue.wait_for_effect = command[4] != 0;
        cue.effect_entry_index = command[5];
        cue.text = text.c_str();
        HandleGameplayScriptTextEffectCue(gameplay_script_dialog_state(), cue,
            state.current_tick);
        trigger.blocked = gameplay_script_dialog_state().advance_flags[1] != 0 ? 1 : 0;
        return true;
    }
    case 0x23: {
        const GameplayScriptArea* area = area_state(state, command[1]);
        if (area == nullptr) {
            return true;
        }
        for (u32 index : active_object_indices(state)) {
            GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object_alive(object) && area_contains_object(*area, *object)) {
                mark_gameplay_script_object_dead(*object);
            }
        }
        return true;
    }
    case 0x24: {
        GameplayScriptOwnerConditionState* owner =
            command[1] < state.condition_context.owners.size() ?
            &state.condition_context.owners[command[1]] : nullptr;
        if (owner != nullptr) {
            owner->resource_a = wrap_add_i32(
                owner->resource_a, static_cast<i32>(command[2]));
            state.opcode_context.owner_resource_dirty[command[1]] = true;
        }
        return true;
    }
    case 0x25: {
        GameplayScriptOwnerConditionState* owner =
            command[1] < state.condition_context.owners.size() ?
            &state.condition_context.owners[command[1]] : nullptr;
        if (owner != nullptr) {
            owner->secondary_score = wrap_add_i32(
                owner->secondary_score, static_cast<i32>(command[2]));
            owner->score = wrap_add_i32(
                owner->score, static_cast<i32>(command[2]));
            state.opcode_context.owner_score_component_dirty[command[1]] = true;
        }
        return true;
    }
    case 0x26: {
        GameplayScriptOwnerConditionState* owner =
            command[1] < state.condition_context.owners.size() ?
            &state.condition_context.owners[command[1]] : nullptr;
        if (owner != nullptr) {
            owner->resource_a = std::max<i32>(0,
                wrap_sub_i32(owner->resource_a, static_cast<i32>(command[2])));
            state.opcode_context.owner_resource_dirty[command[1]] = true;
        }
        return true;
    }
    case 0x27: {
        GameplayScriptOwnerConditionState* owner =
            command[1] < state.condition_context.owners.size() ?
            &state.condition_context.owners[command[1]] : nullptr;
        if (owner != nullptr) {
            const i32 old_component = owner->secondary_score;
            const i32 new_component = std::max<i32>(0,
                wrap_sub_i32(old_component, static_cast<i32>(command[2])));
            owner->secondary_score = new_component;
            owner->score = wrap_add_i32(
                owner->score, wrap_sub_i32(new_component, old_component));
            state.opcode_context.owner_score_component_dirty[command[1]] = true;
        }
        return true;
    }
    case 0x28: {
        GameplayScriptTriggerGroup* source_group = group_state(state, command[1]);
        GameplayScriptTriggerGroup* target_group = group_state(state, command[2]);
        GameplayScriptTriggerObjectState* source =
            source_group != nullptr ? first_group_slot_object(state, *source_group) : nullptr;
        if (source != nullptr && target_group != nullptr &&
            target_group->reference_count != 0) {
            for_each_group_slot_object(state, *target_group,
                [&](GameplayScriptTriggerObjectState& object) {
                    set_script_object_owner(object, source->owner_id);
                });
            rebuild_owner_unit_type_counts(state);
            trigger.blocked = 0;
        }
        return true;
    }
    case 0x29:
        state.opcode_context.resource_hud_flags |= 0x40;
        state.opcode_context.countdown_x = 400;
        state.opcode_context.countdown_y = 0x0c;
        state.opcode_context.countdown_color = 1;
        state.opcode_context.game_clock_decrements = false;
        state.opcode_context.game_clock_ticks = command[1];
        trigger.blocked = 0;
        return true;
    case 0x2a:
        begin_single_gameplay_script_selection_request(state.opcode_context, 0);
        return true;
    case 0x2b: {
        const GameplayScriptArea* area = area_state(state, command[1]);
        if (area != nullptr) {
            append_spawn_request(state, command[0], 0, command[2],
                area_center_x(*area), area_center_y(*area), true);
            trigger.blocked = 0;
        }
        return true;
    }
    case 0x2c: {
        const GameplayScriptArea* area = area_state(state, command[1]);
        if (area != nullptr) {
            append_spawn_request(state, command[0], 0, 0,
                area_center_x(*area), area_center_y(*area), true, true, area);
            trigger.blocked = 0;
        }
        return true;
    }
    case 0x2d: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        GameplayScriptTriggerObjectState* object =
            group != nullptr && group->reference_count != 0 ?
                first_group_slot_object(state, *group) : nullptr;
        if (object != nullptr && object->unit != nullptr) {
            SetOrQueueUnitCommand17(object->unit, command[2], true);
        }
        return true;
    }
    case 0x2e: {
        GameplayScriptTriggerGroup* source_group = group_state(state, command[1]);
        GameplayScriptTriggerGroup* target_group = group_state(state, command[2]);
        GameplayScriptTriggerObjectState* target =
            target_group != nullptr ? first_group_slot_object(state, *target_group) : nullptr;
        if (source_group == nullptr || source_group->reference_count == 0 ||
            target == nullptr) {
            return true;
        }
        for (u32 slot = 0; slot < source_group->object_indices.size(); ++slot) {
            GameplayScriptTriggerObjectState* object =
                object_state(state, source_group->object_indices[slot]);
            if (object_alive(object) && object->unit != nullptr) {
                DispatchGameplayScriptUnitCommand(command[3], object->unit, target->unit,
                    target->x, target->y, true);
            }
        }
        trigger.blocked = 0;
        return true;
    }
    case 0x2f:
        state.opcode_context.resource_hud_flags |= 0x40;
        state.opcode_context.countdown_x = 400;
        state.opcode_context.countdown_y = 0x0c;
        state.opcode_context.countdown_color = 1;
        return true;
    case 0x30:
        state.opcode_context.resource_hud_flags &= ~0x40u;
        return true;
    case 0x31: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        GameplayScriptTriggerObjectState* object =
            group != nullptr && group->reference_count != 0 ?
                first_group_slot_object(state, *group) : nullptr;
        if (object != nullptr && object->unit != nullptr) {
            SetOrQueueUnitCommand22(object->unit, command[2], true);
        }
        return true;
    }
    case 0x32:
        return true;
    case 0x33: {
        GameplayScriptTriggerGroup* source_group = group_state(state, command[1]);
        GameplayScriptTriggerGroup* target_group = group_state(state, command[2]);
        GameplayScriptTriggerObjectState* source =
            source_group != nullptr ? first_group_slot_object(state, *source_group) : nullptr;
        if (source == nullptr || source->unit == nullptr || target_group == nullptr ||
            target_group->reference_count == 0) {
            return true;
        }
        u32 issued = 0;
        for (u32 slot = 0;
             slot < target_group->object_indices.size() && issued < 8; ++slot) {
            GameplayScriptTriggerObjectState* target =
                object_state(state, target_group->object_indices[slot]);
            if (target != nullptr && target->unit != nullptr &&
                object_visible_for_area_scan(*target)) {
                SetOrQueueUnitTargetCommand0a(source->unit, target->unit, true);
                ++issued;
            }
        }
        trigger.blocked = 0;
        return true;
    }
    case 0x34: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        GameplayScriptTriggerObjectState* object =
            group != nullptr ? first_group_slot_object(state, *group) : nullptr;
        const GameplayScriptArea* area = area_state(state, command[2]);
        if (object != nullptr && object->unit != nullptr && area != nullptr) {
            const i32 x = area->left + (area->right - area->left) / 2;
            const i32 y = area->top + (area->bottom - area->top) / 2;
            SetOrQueueUnitPointCommand24(object->unit, x, y, true);
            SetOrQueueUnitCommand00(object->unit, true);
            trigger.blocked = 0;
        }
        return true;
    }
    case 0x35:
        state.opcode_context.resource_hud_flags |= 1u << (command[1] & 0x1fu);
        return true;
    case 0x36:
        state.opcode_context.resource_hud_flags &= ~(1u << (command[1] & 0x1fu));
        return true;
    case 0x37: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group == nullptr || group->reference_count == 0) {
            return true;
        }
        for (u32 slot = 0; slot < group->object_indices.size(); ++slot) {
            GameplayScriptTriggerObjectState* object =
                object_state(state, group->object_indices[slot]);
            if (object != nullptr && object->unit != nullptr) {
                SetOrQueueUnitCommand11(object->unit, true);
            }
        }
        return true;
    }
    case 0x38: {
        if (state.opcode_context.owner_unit_availability == nullptr) {
            return true;
        }
        const u8* low_bytes = reinterpret_cast<const u8*>(command.data() + 1);
        const u8* high_bytes = reinterpret_cast<const u8*>(command.data()) + 0x304;
        auto& availability = *state.opcode_context.owner_unit_availability;
        for (u32 owner_index = 0; owner_index < availability.size(); ++owner_index) {
            for (u32 i = 0; i < 0x60; ++i) {
                availability[owner_index][i] =
                    low_bytes[owner_index * 0x60 + i];
            }
            for (u32 i = 0; i < 0x4a; ++i) {
                availability[owner_index][0x60 + i] =
                    high_bytes[owner_index * 0x4a + i];
            }
        }
        return true;
    }
    case 0x39: {
        GameplayScriptTriggerGroup* first_group = group_state(state, command[1]);
        GameplayScriptTriggerGroup* second_group = group_state(state, command[2]);
        GameplayScriptTriggerObjectState* first =
            first_group != nullptr && first_group->reference_count != 0 ?
                first_group_slot_object(state, *first_group) : nullptr;
        GameplayScriptTriggerObjectState* second =
            second_group != nullptr && second_group->reference_count != 0 ?
                first_group_slot_object(state, *second_group) : nullptr;
        if (first != nullptr && first->unit != nullptr &&
            second != nullptr && second->unit != nullptr) {
            SetOrQueueUnitTargetCommand0b(first->unit, second->unit, true);
            SetOrQueueUnitTargetCommand0b(second->unit, first->unit, true);
            trigger.blocked = 0;
        }
        return true;
    }
    case 0x3a:
    case 0x3b:
    case 0x3c:
    case 0x3e:
    case 0x3f:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group == nullptr ||
            signed_i32_from_wrapped_u32(group->reference_count) <= 0) {
            return true;
        }
        for_each_group_slot_object(state, *group,
            [&](GameplayScriptTriggerObjectState& object) {
            if (command[0] == 0x3a) {
                mutate_gameplay_script_object_runtime_flags(
                    object, 0, 0x20000000u);
            } else if (command[0] == 0x3b) {
                mutate_gameplay_script_object_script_bit_flags(
                    object, 0, 1u << (command[2] & 0x1fu));
            } else if (command[0] == 0x3c) {
                object.stat_2c = signed_i32_from_wrapped_u32(
                    static_cast<u32>(object.stat_2c) + command[2]);
            } else if (command[0] == 0x3e) {
                object.stat_18 += command[2];
                object.stat_20 = std::min(object.stat_20, object.stat_18);
            } else if (command[0] == 0x3f) {
                object.stat_18 = command[2];
                object.stat_20 = std::min(object.stat_20, object.stat_18);
            } else if (command[0] == 0x40) {
                object.stat_20 = clamp_to_primary_current_range(
                    object.stat_20 + command[2], object.stat_18, true);
            } else if (command[0] == 0x41) {
                object.stat_20 =
                    clamp_to_primary_current_range(command[2], object.stat_18, true);
            } else if (command[0] == 0x42) {
                object.stat_secondary_max += command[2];
                object.stat_secondary_current =
                    std::min(object.stat_secondary_current, object.stat_secondary_max);
            } else if (command[0] == 0x43) {
                object.stat_secondary_max = command[2];
                object.stat_secondary_current =
                    std::min(object.stat_secondary_current, object.stat_secondary_max);
            } else if (command[0] == 0x44) {
                object.stat_secondary_current =
                    std::min(object.stat_secondary_current + command[2],
                        object.stat_secondary_max);
            } else if (command[0] == 0x45) {
                object.stat_secondary_current =
                    std::min(command[2], object.stat_secondary_max);
            } else if (command[0] == 0x46) {
                object.stat_1c += command[2];
            } else if (command[0] == 0x47) {
                object.stat_1c = command[2];
            } else if (command[0] == 0x48) {
                object.stat_24 += command[2];
            } else if (command[0] == 0x49) {
                object.stat_24 = command[2];
            } else if (command[0] == 0x4a) {
                // Original 0x0041a2b5 performs the 32-bit ADD first and only
                // then clamps values greater than 999.  Preserve wraparound
                // for script deltas encoded as large unsigned values.
                object.stat_54 += command[2];
                if (object.stat_54 > 999) {
                    object.stat_54 = 999;
                }
            } else if (command[0] == 0x4b) {
                object.stat_54 = std::min<u32>(command[2], 999);
            } else if (command[0] == 0x4c) {
                object.stat_50 += command[2];
                object.stat_recompute_required = true;
            } else if (command[0] == 0x4d) {
                object.stat_50 = command[2];
                object.stat_recompute_required = true;
            }
            if (command[0] >= 0x3c) {
                publish_numeric_stat_to_attached_unit(object, command[0]);
            }
            if ((command[0] == 0x4c || command[0] == 0x4d) &&
                state.opcode_context.apply_variant_progress_immediate != nullptr) {
                state.opcode_context.apply_variant_progress_immediate(object,
                    state.opcode_context.apply_variant_progress_immediate_user);
            }
        });
        return true;
    }
    case 0x3d:
        state.opcode_context.game_clock_ticks += command[1];
        return true;
    case 0x4e: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        const GameplayScriptArea* area = area_state(state, command[2]);
        if (group != nullptr && group->reference_count != 0 && area != nullptr) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                if (object.unit != nullptr && (object_command_flags(object) & 2u) != 0) {
                    SetOrQueueUnitPointCommand01(object.unit, area->left, area->top, false);
                }
            });
        }
        return true;
    }
    case 0x4f: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        const GameplayScriptArea* area = area_state(state, command[3]);
        if (group != nullptr && group->reference_count != 0 && area != nullptr) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                if (object.unit != nullptr) {
                    SetOrQueueUnitCommand02(object.unit, command[2], nullptr, area->left,
                        area->top, false);
                }
            });
        }
        return true;
    }
    case 0x50:
        state.opcode_context.global_flag_22344 = false;
        return true;
    case 0x51:
        state.opcode_context.global_flag_22344 = true;
        return true;
    case 0x52:
    case 0x53:
        return true;
    case 0x54:
        state.opcode_context.global_flag_22348 = false;
        return true;
    case 0x55:
        state.opcode_context.global_flag_22348 = true;
        return true;
    case 0x56:
        HandleGameplayScriptImmediateEffectCue(gameplay_script_dialog_state(),
            command[1] != 0, command[2]);
        return true;
    case 0x57:
        return true;
    case 0x58: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr && group->reference_count != 0) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                set_script_object_owner(object, command[2]);
            });
            rebuild_owner_unit_type_counts(state);
        }
        return true;
    }
    case 0x59: {
        const i32 x = command[1] != 0 ? static_cast<i32>(command[2]) : 0;
        const i32 y = command[1] != 0 ? static_cast<i32>(command[3]) : 0;
        const u32 counter_owner = command[4];
        u32 counter = 0;
        const GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, counter_owner);
        if (owner != nullptr) {
            counter = owner->trigger_counter;
        }
        const std::string format = command_string_from(command, 5);
        char buffer[80]{};
        std::snprintf(buffer, sizeof(buffer), format.c_str(), counter);
        set_text_overlay(state.opcode_context, x, y, buffer, counter_owner);
        return true;
    }
    case 0x5a: {
        const i32 direction = signed_i32_from_wrapped_u32(command[2]);
        if (direction <= 0 || direction >= 9) {
            return true;
        }
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr &&
            signed_i32_from_wrapped_u32(group->reference_count) > 0) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                if (gameplay_script_object_lifecycle_class(object) != 2) {
                    mutate_gameplay_script_object_direction(object, command[2]);
                }
            });
        }
        return true;
    }
    case 0x5b:
        set_runtime_trigger_enabled(state, command[1], false);
        return true;
    case 0x5c:
        set_runtime_trigger_enabled(state, command[1], true);
        return true;
    case 0x5d:
    case 0x60: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group == nullptr ||
            signed_i32_from_wrapped_u32(group->reference_count) <= 0) {
            return true;
        }

        begin_group_gameplay_script_selection_request(state, *group);
        const i32 direction = signed_i32_from_wrapped_u32(command[2]);
        if (command[0] == 0x5d && direction > 0 && direction < 9) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                if (gameplay_script_object_lifecycle_class(object) != 2) {
                    mutate_gameplay_script_object_direction(object, command[2]);
                }
            });
        }
        if (command[0] == 0x5d) {
            trigger.blocked = 0;
            trigger.state = 1;
        }
        return true;
    }
    case 0x5e: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr &&
            signed_i32_from_wrapped_u32(group->reference_count) > 0) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                add_equipment_effect_slot(object, command[2]);
            });
        }
        return true;
    }
    case 0x5f: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr &&
            signed_i32_from_wrapped_u32(group->reference_count) > 0) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                remove_equipment_effect_slot(object, command[2]);
            });
        }
        return true;
    }
    case 0x61:
    case 0x62:
    case 0x63: {
        const GameplayScriptArea* area = area_state(state, command[1]);
        if (area == nullptr) {
            return true;
        }
        for (u32 index : active_object_indices(state)) {
            GameplayScriptTriggerObjectState* object = object_state(state, index);
            if (object == nullptr || !object_bounds_inside_area(*area, *object)) {
                continue;
            }
            if (command[0] == 0x61) {
                if ((gameplay_script_object_runtime_flags(*object) & 4u) == 0 &&
                    gameplay_script_object_lifecycle_class(*object) == 2) {
                    mark_gameplay_script_object_command_dead(*object);
                }
            } else if (object->owner_id == command[2] && object->type_id == command[3]) {
                if (command[0] == 0x62 &&
                    (gameplay_script_object_runtime_flags(*object) & 4u) == 0) {
                    mark_gameplay_script_object_command_dead(*object);
                } else if (command[0] == 0x63) {
                    mutate_gameplay_script_object_runtime_flags(
                        *object, 0, 0x20000000u);
                }
            }
        }
        return true;
    }
    case 0x64:
        if (command[1] < state.opcode_context.owner_external_values.size()) {
            state.opcode_context.owner_external_values[command[1]] = command[2];
            state.opcode_context.owner_population_limit_dirty[command[1]] = true;
        }
        return true;
    case 0x65: {
        GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, command[1]);
        if (owner != nullptr) {
            owner->trigger_counter += command[2];
        }
        return true;
    }
    case 0x66: {
        GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, command[1]);
        if (owner != nullptr) {
            owner->trigger_counter -= command[2];
        }
        return true;
    }
    case 0x67:
    case 0x6f:
        if (command[1] < state.opcode_context.owner_ai_halt_values.size()) {
            state.opcode_context.owner_ai_halt_values[command[1]] = 0;
            state.opcode_context.owner_ai_halt_dirty[command[1]] = true;
        }
        return true;
    case 0x68:
    case 0x70:
        if (command[1] < state.opcode_context.owner_ai_halt_values.size()) {
            state.opcode_context.owner_ai_halt_values[command[1]] = 1;
            state.opcode_context.owner_ai_halt_dirty[command[1]] = true;
        }
        return true;
    case 0x69:
        if (command[1] < 7) {
            state.opcode_context.resource_hud_flags |= 1u << command[1];
            if (command[1] == 6) {
                state.opcode_context.countdown_x = 400;
                state.opcode_context.countdown_y = 0x0c;
                state.opcode_context.countdown_color = 1;
            }
        }
        return true;
    case 0x6a:
        if (command[1] < 7) {
            state.opcode_context.resource_hud_flags &= ~(1u << command[1]);
        }
        return true;
    case 0x6b:
    case 0x71:
        state.opcode_context.resource_hud_flags &= ~0x80u;
        return true;
    case 0x6c:
    case 0x72:
        state.opcode_context.resource_hud_flags |= 0x80u;
        return true;
    case 0x6d: {
        GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, command[1]);
        if (owner != nullptr) {
            owner->trigger_counter = command[2];
        }
        return true;
    }
    case 0x6e: {
        const GameplayScriptArea* area = area_state(state, command[2]);
        if (state.opcode_context.local_owner_id == command[1] && area != nullptr) {
            state.opcode_context.camera_request_active = true;
            state.opcode_context.camera_x = area_center_x(*area);
            state.opcode_context.camera_y = area_center_y(*area);
        }
        return true;
    }
    case 0x73:
    case 0x77: {
        auto* availability = state.opcode_context.owner_unit_availability;
        if (availability != nullptr && command[1] < availability->size() &&
            command[2] < (*availability)[command[1]].size()) {
            (*availability)[command[1]][command[2]] =
                command[0] == 0x73 ? 1 : 0;
        }
        return true;
    }
    case 0x74:
    case 0x75: {
        GameplayScriptOwnerConditionState* owner =
            owner_state(state.condition_context, command[1]);
        if (owner != nullptr) {
            // Original DAT_0070728c/DAT_0070726c are the building/unit kill
            // buckets.  Both aliases clear the former and set the latter.
            owner->metric = static_cast<i32>(command[2]);
            state.opcode_context.owner_kill_counts_dirty[command[1]] = true;
        }
        return true;
    }
    case 0x76:
        state.opcode_context.resource_hud_start_x = static_cast<i32>(command[1]);
        state.opcode_context.resource_hud_start_y = static_cast<i32>(command[2]);
        return true;
    case 0x78: {
        GameplayScriptTriggerGroup* group = group_state(state, command[1]);
        if (group != nullptr &&
            signed_i32_from_wrapped_u32(group->reference_count) > 0) {
            for_each_group_slot_object(state, *group,
                [&](GameplayScriptTriggerObjectState& object) {
                mutate_gameplay_script_object_script_bit_flags(
                    object, 1u << (command[2] & 0x1fu), 0);
            });
        }
        return true;
    }
    case 0x79:
        return true;
    default:
        return true;
    }
}

bool DispatchGameplayScriptUnitCommand(u32 command_kind, UnitMovementUnit* unit,
    UnitMovementUnit* target_unit, i32 x, i32 y, bool enqueue_deferred) {
    constexpr u32 kSuppressedCommandRuntimeFlag = 4;

    if (unit == nullptr || (unit->runtime_flags & kSuppressedCommandRuntimeFlag) != 0) {
        return false;
    }

    switch (command_kind) {
    case 0:
        return SetOrQueueUnitCommand00(unit, enqueue_deferred);
    case 1:
        return SetOrQueueUnitTargetCommand03(unit, target_unit, enqueue_deferred);
    case 2:
        return SetOrQueueUnitTargetPointCommand04(
            unit, target_unit, x, y, enqueue_deferred);
    case 3:
        return SetOrQueueUnitConditionalTargetPointCommand05(
            unit, target_unit, x, y, enqueue_deferred);
    case 4:
        return SetOrQueueUnitPointCommand07(unit, x, y, enqueue_deferred);
    case 5:
        return SetOrQueueUnitPointCommand09(unit, x, y, enqueue_deferred);
    case 6:
        return SetOrQueueUnitTargetCommand0a(unit, target_unit, enqueue_deferred);
    case 7:
        return SetOrQueueUnitPointCommand24(unit, x, y, enqueue_deferred);
    case 8:
        return SetOrQueueUnitTargetCommand0b(unit, target_unit, enqueue_deferred);
    case 9:
        return SetUnitTargetReservationFlag(unit);
    case 10:
        return ClearUnitTargetReservationFlag(unit);
    case 0x0b:
        return SetOrQueueUnitCommand11(unit, enqueue_deferred);
    case 0x0c:
        return SetOrQueueUnitCommand1b(unit, enqueue_deferred);
    case 0x0d:
        return SetUnitCommandTargetReferencePoint(unit, target_unit, x, y);
    case 0x0e:
        return SetOrQueueUnitCommand21AndSetRuntimeFlag(unit, enqueue_deferred);
    case 0x0f:
        return SetOrQueueUnitPointCommand01(unit, x, y, enqueue_deferred);
    default:
        break;
    }

    if (command_kind >= 0x10 && command_kind <= 0x3d) {
        return SetOrQueueUnitExtendedStateCommand(
            unit, target_unit, x, y, command_kind - 0x10, enqueue_deferred);
    }
    return false;
}

u32 ClassifyGameplayScriptUnitRuntimeState(const UnitMovementUnit& unit,
    const std::vector<u32>* command_state_table) {
    return ResolveUnitRuntimeStateFromCommandTable(unit, command_state_table);
}

void ProcessGameplayScriptTriggers(GameplayScriptTriggerState& state, u32 phase,
    const GameplayScriptTriggerCallbacks& callbacks) {
    for (u32 group_index = 0; group_index < kGameplayScriptTriggerGroupCount; ++group_index) {
        GameplayScriptTriggerGroup& group = state.groups[group_index];
        if (!group.active) {
            continue;
        }

        u32 remaining = group.reference_count;
        for (u32& object_index : group.object_indices) {
            if (object_index != 0 &&
                (object_removed_by_state(state, object_index) ||
                    object_removed_by_callback(callbacks, object_index))) {
                object_index = 0;
                if (remaining != 0) {
                    --remaining;
                }
            }
        }
        group.reference_count = remaining;
        encode_group(state, group_index);
    }

    for (u32 trigger_index = 0; trigger_index < kGameplayScriptTriggerRuntimeCount;
         ++trigger_index) {
        GameplayScriptTriggerRuntimeRecord& trigger = state.triggers[trigger_index];
        if (!trigger.condition_enabled || !trigger.trigger_enabled || trigger.state == 1) {
            continue;
        }
        if (GetGameplayScriptTriggerOwnerPhase(state, trigger.owner_phase_lookup) != phase) {
            continue;
        }
        if (!default_condition_result(state, trigger, callbacks)) {
            encode_trigger(state, trigger_index);
            continue;
        }
        encode_trigger(state, trigger_index);
        if (!default_command_result(state, trigger, callbacks) || trigger.blocked == 1) {
            continue;
        }

        trigger.state = 1;
        trigger.last_fired_tick = state.current_tick;
        encode_trigger(state, trigger_index);
    }

    if (phase != 0) {
        ++state.post_nonzero_phase_resets;
        gameplay_script_dialog_state().force_complete = false;
    }
}

i32 FindGameplayScriptTriggerGroupForObject(const GameplayScriptTriggerState& state,
    const void* object_pointer) {
    for (u32 group_index = 0; group_index < kGameplayScriptTriggerGroupCount; ++group_index) {
        const GameplayScriptTriggerGroup& group = state.groups[group_index];
        if (!group.active) {
            continue;
        }
        for (u32 object_index : group.object_indices) {
            if (object_index < state.objects.size() &&
                state.objects[object_index].object_pointer == object_pointer) {
                return static_cast<i32>(group_index);
            }
        }
    }
    return -1;
}

u32 GetGameplayScriptTriggerOwnerPhase(const GameplayScriptTriggerState& state,
    u32 lookup_index) {
    if (lookup_index < state.owner_phase_lookup.size()) {
        return state.owner_phase_lookup[lookup_index];
    }
    return default_gameplay_script_trigger_owner_phase(lookup_index);
}

GameplayScriptDialogState& gameplay_script_dialog_state() {
    return g_dialog_state;
}

GameplayScriptTriggerState& gameplay_script_trigger_state() {
    return g_trigger_state;
}

}
