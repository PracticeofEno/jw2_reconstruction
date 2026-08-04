#include "ranker_gameplay_production_actions.h"
#include "ranker_reliable_packets.h"
#include "ranker_runtime_resources.h"
#include "ranker_unit_movement.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace ranker {
namespace {

GameplayProductionActionState g_gameplay_production_action_state;

constexpr u32 kSubtypeUnitOrder = 0x02;
constexpr u32 kSubtypeEquipmentCommand = 0x03;
constexpr u32 kSubtypeEquipmentToggle = 0x04;
constexpr u32 kSubtypeAuxVector = 0x08;
constexpr u32 kSubtypeExtendedUnitOrder = 0x09;
constexpr u32 kSubtypeStatusMask = 0x0b;
constexpr u32 kSubtypePlacementCommand = 0x0c;
constexpr u32 kSubtypeProductionCost = 0x1a;
constexpr u32 kSubtypeVoteCompletion = 0x1d;
constexpr u32 kSpecialPreviewPlacementSelector = 0x17;
constexpr u32 kSpecialPreviewPlacementUnitType = 0x7d;
constexpr u32 kPreviewPlacementBlockedMask = 0x700;
constexpr u32 kPreviewPlacementPassableTerrain = 0x100;
constexpr u32 kPreviewPlacementTerrainValidFlag = 0x80000000;
constexpr u32 kPreviewPlacementTerrainClassMask = 0x1c000000;
constexpr u32 kPreviewPlacementTerrainClassShift = 26;
constexpr u32 kPreviewPlacementTemporaryBlock = 0x20000000;
constexpr u32 kPreviewPlacementRouteRequiredFlag = 0x10000000;
constexpr u32 kPreviewPlacementRouteBlockedFlag = 0x40000000;
constexpr u32 kPreviewPlacementCollisionFlag = 0x08000000;
constexpr u32 kPreviewPlacementVisibilityStateFlag = 0x40;
constexpr u32 kPreviewPlacementVisibilityCommandBit = 7;
constexpr u32 kPreviewPlacementCurrentOwnerShift = 0x12;
constexpr u32 kSelectedUnitMarkerFlag = 0x80;
constexpr u32 kIgnoredCommandHighBit = 0x80000000;
constexpr u32 kStatusMaskCommand = 0x0d;
constexpr u32 kLastDropSessionArchiveSymbol = 0x00862914;
constexpr char kLastDropSessionArchiveName[] = "LastDrop.Jw2";
constexpr std::array<std::size_t, kGameplayProductionEquipmentSlots>
    kOriginalEquipmentSlotCodeToStorageIndex = {4, 5, 0, 1, 2, 3};

GameplayPublishedAction make_action(const GameplayProductionActionState& state,
    u32 subtype, u32 unit_offset = 0, u32 arg0 = 0, u32 arg1 = 0,
    u32 arg2 = 0, u32 arg3 = 0) {
    GameplayPublishedAction action{};
    action.subtype = subtype;
    action.player = state.local_player_index;
    action.packed_opcode = (subtype << 24) | (state.local_player_index & 0xffu);
    action.unit_offset = unit_offset;
    action.arg0 = arg0;
    action.arg1 = arg1;
    action.arg2 = arg2;
    action.arg3 = arg3;
    return action;
}

bool publish(GameplayProductionActionState& state, const GameplayPublishedAction& action) {
    state.published_actions.push_back(action);
    return state.callbacks.publish_action == nullptr ||
        state.callbacks.publish_action(state, action);
}

bool export_loaded_gameplay_session_bundle(const char* archive_name) {
    const GameplaySessionLoadState& load = gameplay_session_load_state();
    if (archive_name == nullptr || archive_name[0] == '\0' || !load.loaded) {
        return false;
    }

    std::vector<TrcWriteRecord> records;
    const std::vector<GameplaySessionExportRecordSpec>& specs =
        gameplay_session_export_specs();
    records.reserve(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const GameplaySessionExportRecordSpec& spec = specs[i];
        TrcWriteRecord record{};
        record.name = spec.name;
        record.method = spec.original_method;
        if (i < load.records.size() && load.record_loaded[i]) {
            record.payload = load.records[i];
        }
        else if (spec.byte_count != 0) {
            record.payload.assign(spec.byte_count, 0);
        }
        records.push_back(record);
    }

    return HandleGameplaySessionBundleExport(archive_name, records);
}

GameplayPublishedAction make_queued_action(
    const GameplayProductionQueuedCommand& command) {
    GameplayPublishedAction action{};
    action.packed_opcode = command.player_opcode;
    action.subtype = (command.player_opcode >> 24) & 0xffu;
    action.player = command.player_opcode & 0xffu;
    action.arg0 = command.selector;
    action.unit_offset = command.unit_offset;
    action.arg1 = command.aux;
    action.arg2 = static_cast<u32>(command.world_x);
    action.arg3 = static_cast<u32>(command.world_y);
    return action;
}

GameplayProductionUnitState* find_unit(GameplayProductionActionState& state,
    u32 unit_offset) {
    auto it = std::find_if(state.units.begin(), state.units.end(),
        [unit_offset](const GameplayProductionUnitState& unit) {
            return unit.offset == unit_offset;
        });
    return it == state.units.end() ? nullptr : &*it;
}

const GameplayProductionUnitState* find_unit(
    const GameplayProductionActionState& state, u32 unit_offset) {
    auto it = std::find_if(state.units.begin(), state.units.end(),
        [unit_offset](const GameplayProductionUnitState& unit) {
            return unit.offset == unit_offset;
        });
    return it == state.units.end() ? nullptr : &*it;
}

GameplayProductionUnitState* selected_unit(GameplayProductionActionState& state) {
    const u32 unit_offset =
        state.current_unit_offset != 0 ? state.current_unit_offset :
        state.selected_unit_offset;
    return find_unit(state, unit_offset);
}

u32 primary_unit_offset(const GameplayProductionActionState& state) {
    return state.current_unit_offset != 0 ? state.current_unit_offset :
        state.selected_unit_offset;
}

bool unit_is_local_active(const GameplayProductionActionState& state,
    const GameplayProductionUnitState& unit) {
    return unit.active && unit.owner == state.local_player_index;
}

bool contains_value(const std::vector<u32>& values, u32 value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

u32 definition_index_for_selector(const GameplayProductionActionState& state,
    u32 selector) {
    if (selector >= state.selector_definition_indices.size()) {
        return selector;
    }
    return state.selector_definition_indices[selector];
}

const GameplayProductionActionDefinition* definition_for_selector(
    const GameplayProductionActionState& state, u32 selector) {
    const u32 index = definition_index_for_selector(state, selector);
    if (index >= state.definitions.size()) {
        return nullptr;
    }
    return &state.definitions[index];
}

const GameplayProductionUnitFootprintDefinition* footprint_for_unit_type(
    const GameplayProductionActionState& state, u32 unit_type) {
    const auto it = std::find_if(state.unit_footprints.begin(),
        state.unit_footprints.end(),
        [unit_type](const GameplayProductionUnitFootprintDefinition& footprint) {
            return footprint.unit_type == unit_type;
        });
    return it == state.unit_footprints.end() ? nullptr : &*it;
}

const GameplayProductionUnitFootprintDefinition* footprint_for_command(
    const GameplayProductionActionState& state,
    const GameplayProductionQueuedCommand& command,
    const GameplayProductionUnitState* unit) {
    if (unit != nullptr) {
        return footprint_for_unit_type(state, unit->type);
    }
    return footprint_for_unit_type(state, command.unit_type);
}

u32 clamp_world_axis(i32 value, u32 tile_count) {
    const i32 limit = tile_count == 0 ? 0 : static_cast<i32>(tile_count * 0x20u) - 1;
    if (value < 0) {
        return 0;
    }
    if (value > limit) {
        return static_cast<u32>(limit);
    }
    return static_cast<u32>(value);
}

void reset_validation_state(GameplayProductionActionState& state) {
    state.last_validation_flags = 0;
    state.last_validation_unit_offset = 0;
    state.last_validation_blocking_unit_offset = 0;
}

bool point_in_unit_bounds(
    const GameplayProductionUnitState& unit, i32 world_x, i32 world_y) {
    const i32 width = unit.bounds_width > 0 ? unit.bounds_width : 0x20;
    const i32 height = unit.bounds_height > 0 ? unit.bounds_height : 0x20;
    const i32 left = unit.x + unit.bounds_left;
    const i32 top = unit.y + unit.bounds_top;
    const i32 right = left + width;
    const i32 bottom = top + height;
    return left <= world_x && world_x <= right && top <= world_y && world_y <= bottom;
}

bool production_unit_blocks_action(const GameplayProductionUnitState& unit) {
    // FUN_004e96ae is already walking the active-unit list.  It only tests
    // raw +0xa0 bit 0x80 here; the command/lifecycle state is not part of
    // this target gate.
    return unit.active && (unit.runtime_flags & 0x80u) == 0;
}

bool preview_collision_visibility_gate(
    const GameplayProductionActionState& state,
    const GameplayProductionPlacementCell& cell,
    const GameplayProductionUnitState& unit, u32 source_owner) {
    // FUN_004d6cb0 returns visible immediately for ordinary units.  Units
    // carrying raw +0x9c bit 0x40 or raw +0x5c bit 0x80 instead use
    // FUN_004d6cca's owner/current-visibility test.
    const bool requires_visibility_test =
        (unit.command_flags & kPreviewPlacementVisibilityStateFlag) != 0 ||
        (unit.runtime_command_bits &
            (1u << kPreviewPlacementVisibilityCommandBit)) != 0;
    if (!requires_visibility_test) {
        return true;
    }
    if (source_owner >= 32) {
        return false;
    }
    if (unit.owner < state.owner_visibility_masks.size() &&
        (state.owner_visibility_masks[unit.owner] & (1u << source_owner)) != 0) {
        return true;
    }
    const u32 current_bit = source_owner + kPreviewPlacementCurrentOwnerShift;
    return current_bit < 32 &&
        (cell.current_visibility_flags & (1u << current_bit)) != 0;
}

bool production_action_target_visible(
    const GameplayProductionActionState& state,
    const GameplayProductionUnitState& unit) {
    if (!unit.visible) {
        return false;
    }
    if (state.placement_map.width == 0 || state.placement_map.height == 0 ||
        state.placement_map.cells.empty()) {
        return true;
    }
    if (unit.x < 0 || unit.y < 0) {
        return false;
    }
    const u32 tile_x = static_cast<u32>(unit.x) >> 5;
    const u32 tile_y = static_cast<u32>(unit.y) >> 5;
    if (tile_x >= state.placement_map.width ||
        tile_y >= state.placement_map.height) {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(tile_y) * state.placement_map.width + tile_x;
    return index < state.placement_map.cells.size() &&
        preview_collision_visibility_gate(state,
            state.placement_map.cells[index], unit, state.local_player_index);
}

bool default_validate_low_action(
    GameplayProductionActionState& state, i32 world_x, i32 world_y) {
    reset_validation_state(state);
    const GameplayProductionUnitState* enemy_mobile = nullptr;
    const GameplayProductionUnitState* local_structure = nullptr;
    const GameplayProductionUnitState* enemy_structure = nullptr;
    for (const GameplayProductionUnitState& unit : state.units) {
        if (!production_unit_blocks_action(unit) ||
            !production_action_target_visible(state, unit) ||
            !point_in_unit_bounds(unit, world_x, world_y)) {
            continue;
        }

        if (unit.type < 0x60u) {
            if (unit.owner == state.local_player_index) {
                // FUN_004e96ae returns immediately for the first local mobile
                // hit with DAT_008686c0 still zero; no lower-priority
                // structure can replace it.
                state.last_validation_unit_offset = unit.offset;
                state.last_validation_blocking_unit_offset = unit.offset;
                return true;
            }
            enemy_mobile = &unit;
            state.last_validation_flags |= 2u;
        }
        else if (unit.owner == state.local_player_index) {
            local_structure = &unit;
            state.last_validation_flags |= 4u;
        }
        else {
            enemy_structure = &unit;
            state.last_validation_flags |= 8u;
        }
    }

    const GameplayProductionUnitState* selected = enemy_mobile != nullptr
        ? enemy_mobile
        : (local_structure != nullptr ? local_structure : enemy_structure);
    if (selected != nullptr) {
        state.last_validation_unit_offset = selected->offset;
        state.last_validation_blocking_unit_offset = selected->offset;
        return true;
    }
    return false;
}

bool default_validate_high_action(
    GameplayProductionActionState& state, i32 world_x, i32 world_y) {
    reset_validation_state(state);
    for (const GameplayProductionUnitState& unit : state.units) {
        // FUN_004e98c5 scans DAT_007071dc (the lifecycle list), not the
        // ordinary active list used by FUN_004e96ae.
        if (unit.active ||
            !production_action_target_visible(state, unit) ||
            unit.type >= 0x60u ||
            (unit.runtime_flags & 0x80u) != 0 ||
            (unit.runtime_flags & 4u) == 0 ||
            !point_in_unit_bounds(unit, world_x, world_y)) {
            continue;
        }

        state.last_validation_unit_offset = unit.offset;
        state.last_validation_blocking_unit_offset = unit.offset;
        return true;
    }
    return false;
}

bool validate_action(GameplayProductionActionState& state, u32 selector,
    i32 world_x, i32 world_y, bool high_mode) {
    GameplayProductionValidateCallback callback =
        high_mode ? state.callbacks.validate_high_action :
        state.callbacks.validate_low_action;
    if (callback != nullptr) {
        return callback(state, selector, world_x, world_y);
    }
    (void)selector;
    return high_mode ? default_validate_high_action(state, world_x, world_y) :
        default_validate_low_action(state, world_x, world_y);
}

u32 select_action_index(GameplayProductionActionState& state, u32 selector,
    i32 world_x, i32 world_y) {
    if (state.callbacks.select_action_index != nullptr) {
        return state.callbacks.select_action_index(state, selector, world_x, world_y);
    }
    (void)world_x;
    (void)world_y;
    return selector;
}

void start_hud_pulse(GameplayProductionActionState& state, i32 world_x, i32 world_y) {
    if (state.callbacks.start_hud_pulse != nullptr) {
        state.callbacks.start_hud_pulse(state, world_x, world_y);
    }
}

void stop_hud_pulse(GameplayProductionActionState& state) {
    if (state.callbacks.stop_hud_pulse != nullptr) {
        state.callbacks.stop_hud_pulse(state);
    }
}

void reject_action(GameplayProductionActionState& state) {
    state.last_dispatch_failed = true;
    if (state.callbacks.rejected_action_feedback != nullptr) {
        state.callbacks.rejected_action_feedback(state);
    }
}

void reject_action_silently(GameplayProductionActionState& state) {
    // Production-table entry 0x1e targets 0x004db5df (STC; RET).  It does
    // not execute the shared 0x004db5cd error-feedback tail.
    state.last_dispatch_failed = true;
    state.last_gate_failure = GameplayProductionGateFailure::none;
}

void acknowledge_action(GameplayProductionActionState& state, u32 unit_offset) {
    if (state.callbacks.accepted_action_feedback != nullptr) {
        state.callbacks.accepted_action_feedback(state, unit_offset);
    }
}

bool unit_is_selected_local_action_candidate(const GameplayProductionActionState& state,
    const GameplayProductionUnitState& unit) {
    return unit.active && unit.owner == state.local_player_index &&
        unit.selected && (unit.status_flags & kSelectedUnitMarkerFlag) != 0;
}

bool production_gate_for_unit(GameplayProductionActionState& state,
    const GameplayProductionUnitState& unit, u32 action_index) {
    const u32 saved_current = state.current_unit_offset;
    state.current_unit_offset = unit.offset;
    const bool result = CheckSelectedUnitProductionActionGate(state, action_index);
    state.current_unit_offset = saved_current;
    return result;
}

void apply_result_state_for_unit(GameplayProductionActionState& state,
    GameplayProductionUnitState& unit, u32 action_index) {
    if (action_index < state.selector_result_states.size()) {
        unit.result_state = state.selector_result_states[action_index];
    }
}

bool publish_extended_unit_order(GameplayProductionActionState& state,
    GameplayProductionUnitState& unit, u32 command, u32 aux) {
    const u32 packet_command = state.shift_modifier_down
        ? command | kIgnoredCommandHighBit
        : command;
    return publish(state, make_action(state, kSubtypeExtendedUnitOrder,
        unit.offset, packet_command, aux, state.last_world_x, state.last_world_y));
}

GameplayProductionQueuedCommand make_extended_unit_order_queue_command(
    const GameplayProductionActionState& state, const GameplayProductionUnitState& unit,
    u32 command, u32 aux) {
    GameplayProductionQueuedCommand queued{};
    queued.player_opcode =
        (kSubtypeExtendedUnitOrder << 24) |
        (state.local_player_index & 0xffu);
    queued.selector = command;
    queued.unit_offset = unit.offset;
    queued.aux = aux;
    queued.world_x = static_cast<i32>(state.last_world_x);
    queued.world_y = static_cast<i32>(state.last_world_y);
    queued.unit_type = unit.type;
    if (const GameplayProductionUnitFootprintDefinition* footprint =
            footprint_for_unit_type(state, unit.type)) {
        queued.footprint_width = static_cast<u32>(
            std::max<i32>(0, footprint->layout_width));
        queued.footprint_height = static_cast<u32>(
            std::max<i32>(0, footprint->layout_height));
    }
    queued.unit_x = unit.x;
    queued.unit_y = unit.y;
    return queued;
}

u32 publish_all_selected_units_for_action(GameplayProductionActionState& state,
    u32 command, u32 aux, bool queue_commands) {
    u32 published = 0;
    for (GameplayProductionUnitState& unit : state.units) {
        if (!unit_is_selected_local_action_candidate(state, unit) ||
            !production_gate_for_unit(state, unit, command)) {
            continue;
        }
        if (queue_commands) {
            QueueProductionPlacementCommand(state,
                make_extended_unit_order_queue_command(state, unit, command, aux));
        }
        else {
            // FUN_004de640 has no packet-rejection carry result.  Once the
            // production gate passes, the table handler counts the command.
            publish_extended_unit_order(state, unit, command, aux);
        }
        ++published;
    }
    return published;
}

bool publish_first_matching_unit_for_action(GameplayProductionActionState& state,
    u32 command, u32 aux) {
    if (GameplayProductionUnitState* unit = selected_unit(state)) {
        if (unit_is_local_active(state, *unit) &&
            production_gate_for_unit(state, *unit, command)) {
            publish_extended_unit_order(state, *unit, command, aux);
            acknowledge_action(state, unit->offset);
            return true;
        }
    }

    for (GameplayProductionUnitState& unit : state.units) {
        if (!unit_is_selected_local_action_candidate(state, unit) ||
            !production_gate_for_unit(state, unit, command)) {
            continue;
        }
        publish_extended_unit_order(state, unit, command, aux);
        acknowledge_action(state, unit.offset);
        return true;
    }
    return false;
}

bool action_allows_unit_movement_class(
    const GameplayProductionActionDefinition& definition,
    const GameplayProductionUnitState& unit) {
    if (unit.movement_class >= 32) {
        return true;
    }
    return (definition.allowed_movement_class_mask &
        (1u << unit.movement_class)) != 0;
}

bool owner_requirement_allows_unit(const GameplayProductionActionState& state,
    const GameplayProductionActionDefinition& definition,
    const GameplayProductionUnitState& unit) {
    if (definition.owner_requirement == 0xffffffffu) {
        return true;
    }
    // FUN_004db92c/FUN_004db9de index DAT_00708970 by the source owner and
    // JW2_11 +0x15c, then require a nonzero completed variant count.  The
    // record field is a production-order dependency, not an owner relation.
    return unit.owner < state.owner_production_order_variant_counts.size() &&
        definition.owner_requirement <
            state.owner_production_order_variant_counts[unit.owner].size() &&
        state.owner_production_order_variant_counts
            [unit.owner][definition.owner_requirement] != 0;
}

bool finish_dispatch_success(GameplayProductionActionState& state,
    GameplayProductionUnitState* target, u32 action_index,
    bool write_result_state) {
    if (write_result_state && target != nullptr) {
        apply_result_state_for_unit(state, *target, action_index);
    }
    return true;
}

bool production_handler_uses_selected_group(u32 action_index) {
    // Exact aliases in the 32-pointer table at 0x004db238:
    // 0x004db31d is the all-selected publisher, while 0x004db47c is its
    // queued/formation variant used only by selector 4.
    switch (action_index) {
    case 0x08:
    case 0x09:
    case 0x0c:
    case 0x0d:
    case 0x0f:
    case 0x12:
    case 0x13:
    case 0x18:
        return true;
    default:
        return false;
    }
}

bool production_target_precondition_allows(
    const GameplayProductionActionState& state, u32 action_index,
    const GameplayProductionUnitState* target) {
    switch (action_index) {
    case 0x00:
        return target != nullptr &&
            (target->definition_action_flags & 0x10u) != 0;
    case 0x08:
        return target != nullptr &&
            (target->definition_action_flags & 0x20u) != 0;
    case 0x0a:
    case 0x1c:
        return target != nullptr &&
            (target->command_flags & 0x003c0000u) == 0 &&
            (target->definition_action_flags & 0x40u) != 0;
    case 0x18:
        return target != nullptr &&
            (target->runtime_flags & 0x20000000u) == 0;
    case 0x19:
        return target != nullptr &&
            (target->command_flags & 0x003c0000u) == 0 &&
            target->type == 0x31u;
    case 0x1b:
        return target != nullptr && target->type == 0x31u &&
            target->owner < state.owner_relation_masks.size() &&
            state.local_player_index < 32u &&
            (state.owner_relation_masks[target->owner] &
                (1u << state.local_player_index)) != 0;
    default:
        return true;
    }
}

u32 publish_selector_1f_units(GameplayProductionActionState& state) {
    // 0x004db4ec hard-codes EAX=0x13 and EDI=-1; it deliberately bypasses
    // FUN_004db92c and only requires selected/local plus raw +0x9c bit 0x800.
    u32 published = 0;
    for (GameplayProductionUnitState& unit : state.units) {
        if (!unit_is_selected_local_action_candidate(state, unit) ||
            (unit.command_flags & 0x800u) == 0) {
            continue;
        }
        publish_extended_unit_order(state, unit, 0x13u, 0xffffffffu);
        ++published;
    }
    return published;
}

bool dispatch_action_index(GameplayProductionActionState& state, u32 action_index,
    u32 target_offset, bool write_result_state = false) {
    state.last_action_index = action_index;
    state.last_dispatch_failed = false;

    if (action_index >= kGameplayProductionSelectorCount) {
        // The original UI can only index 0xd4..0xf3.  Keep malformed synthetic
        // callers from reading beyond the 32-entry table, with the same
        // carry-set/no-feedback behavior as the adjacent 0x1e stub.
        reject_action_silently(state);
        return false;
    }

    GameplayProductionUnitState* target =
        write_result_state ? find_unit(state, target_offset) : nullptr;
    const u32 packet_target = target != nullptr ? target->offset : 0u;
    state.last_validation_unit_offset = packet_target;

    if (action_index == 0x07u) {
        // Table entry 7 -> 0x004db55d (CLC; RET).
        return finish_dispatch_success(
            state, target, action_index, write_result_state);
    }
    if (action_index == 0x1eu) {
        // Table entry 0x1e -> 0x004db5df (STC; RET), not the shared feedback
        // tail at 0x004db5cd.
        reject_action_silently(state);
        return false;
    }
    if (!production_target_precondition_allows(
            state, action_index, target)) {
        reject_action(state);
        return false;
    }

    if (action_index == 0x1fu) {
        if (publish_selector_1f_units(state) == 0) {
            reject_action(state);
            return false;
        }
        acknowledge_action(state, primary_unit_offset(state));
        return finish_dispatch_success(
            state, target, action_index, write_result_state);
    }

    if (action_index == 0x04u) {
        ResetQueuedProductionPlacementCommands(state);
        if (publish_all_selected_units_for_action(state, action_index,
                packet_target, true) == 0) {
            reject_action(state);
            return false;
        }
        FlushQueuedProductionPlacementCommands(state);
        acknowledge_action(state, primary_unit_offset(state));
        return finish_dispatch_success(
            state, target, action_index, write_result_state);
    }

    if (production_handler_uses_selected_group(action_index)) {
        if (publish_all_selected_units_for_action(state, action_index,
                packet_target, false) == 0) {
            reject_action(state);
            return false;
        }
        acknowledge_action(state, primary_unit_offset(state));
        return finish_dispatch_success(
            state, target, action_index, write_result_state);
    }

    if (publish_first_matching_unit_for_action(
            state, action_index, packet_target)) {
        return finish_dispatch_success(
            state, target, action_index, write_result_state);
    }

    reject_action(state);
    return false;
}

GameplayProductionPlacementCell* placement_cell(GameplayProductionPlacementMap& map,
    u32 x, u32 y) {
    if (x >= map.width || y >= map.height) {
        return nullptr;
    }
    const u32 index = y * map.width + x;
    if (index >= map.cells.size()) {
        return nullptr;
    }
    return &map.cells[index];
}

const GameplayProductionPlacementCell* placement_cell(
    const GameplayProductionPlacementMap& map, u32 x, u32 y) {
    if (x >= map.width || y >= map.height) {
        return nullptr;
    }
    const u32 index = y * map.width + x;
    if (index >= map.cells.size()) {
        return nullptr;
    }
    return &map.cells[index];
}

u32 terrain_class(const GameplayProductionPlacementCell& cell) {
    return (cell.owner_flags & kPreviewPlacementTerrainClassMask) >>
        kPreviewPlacementTerrainClassShift;
}

bool has_nearby_passable_placement_tile(const GameplayProductionActionState& state,
    i32 tile_x, i32 tile_y) {
    for (i32 y = tile_y - 4; y <= tile_y + 4; ++y) {
        for (i32 x = tile_x - 4; x <= tile_x + 4; ++x) {
            if (x < 0 || y < 0) {
                continue;
            }
            const GameplayProductionPlacementCell* cell = placement_cell(
                state.placement_map, static_cast<u32>(x), static_cast<u32>(y));
            if (cell != nullptr &&
                (cell->live_terrain_flags & kPreviewPlacementBlockedMask) ==
                    kPreviewPlacementPassableTerrain) {
                return true;
            }
        }
    }
    return false;
}

i32 arithmetic_shift_right(i32 value, u32 bits) {
    if (value >= 0) {
        return value >> bits;
    }
    const i32 positive = -value - 1;
    return -((positive >> bits) + 1);
}

i32 world_to_tile(i32 world) {
    return arithmetic_shift_right(world, 5);
}

u32 unit_footprint_area(const GameplayProductionQueuedCommand& command) {
    return (command.footprint_width + 2) * (command.footprint_height + 2);
}

i32 command_layout_width(const GameplayProductionQueuedCommand& command,
    const GameplayProductionUnitFootprintDefinition* footprint) {
    return footprint == nullptr ? static_cast<i32>(command.footprint_width) :
        std::max<i32>(0, footprint->layout_width);
}

i32 command_layout_height(const GameplayProductionQueuedCommand& command,
    const GameplayProductionUnitFootprintDefinition* footprint) {
    return footprint == nullptr ? static_cast<i32>(command.footprint_height) :
        std::max<i32>(0, footprint->layout_height);
}

u32 unit_layout_area(const GameplayProductionQueuedCommand& command,
    const GameplayProductionUnitFootprintDefinition* footprint) {
    const i32 width = command_layout_width(command, footprint);
    const i32 height = command_layout_height(command, footprint);
    return static_cast<u32>((width + 2) * (height + 2));
}

} // namespace

GameplayProductionActionState& gameplay_production_action_state() {
    return g_gameplay_production_action_state;
}

void InitializeOriginalGameplayProductionSelectorTables(
    GameplayProductionActionState& state) {
    // DAT_008629be, immediately before the 32 production selectors.
    static constexpr std::array<u8, kGameplayProductionSelectorCount>
        kRedirectFlags = {
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
            1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
        };
    // DAT_00862a14, written to target raw +0xa4 after a CLC return.
    static constexpr std::array<u8, kGameplayProductionSelectorCount>
        kResultStates = {
            0x88, 0x88, 0x08, 0x88, 0x08, 0x88, 0x08, 0x08,
            0x88, 0x88, 0x08, 0x88, 0x88, 0x88, 0x88, 0x88,
            0x88, 0x08, 0x88, 0x08, 0x08, 0x88, 0x88, 0x08,
            0x88, 0x08, 0x08, 0x08, 0x08, 0x88, 0x00, 0x00,
        };

    state.selector_redirect_flags = kRedirectFlags;
    state.selector_result_states = kResultStates;
    for (u32 selector = 0; selector < kGameplayProductionSelectorCount;
         ++selector) {
        state.selector_definition_indices[selector] = selector;
    }
}

bool DefaultValidateLowGameplayProductionAction(
    GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y) {
    (void)selector;
    return default_validate_low_action(state, world_x, world_y);
}

bool DefaultValidateHighGameplayProductionAction(
    GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y) {
    (void)selector;
    return default_validate_high_action(state, world_x, world_y);
}

u32 DefaultSelectGameplayProductionActionIndex(
    GameplayProductionActionState& state, u32 selector, i32 world_x, i32 world_y) {
    (void)state;
    (void)world_x;
    (void)world_y;
    return selector;
}

void DefaultDispatchGameplayProductionQueuedCommand(
    GameplayProductionActionState& state,
    const GameplayProductionQueuedCommand& command) {
    publish(state, make_queued_action(command));
}

void DefaultBroadcastGameplayProductionReliableRange(
    GameplayProductionActionState& state, u32 start_sequence, u32 end_sequence) {
    (void)state;
    BroadcastMode1PacketRange(start_sequence, end_sequence);
}

bool DefaultExportGameplayProductionSessionBundle(
    GameplayProductionActionState& state, const char* archive_name,
    const char* mirror_archive_name) {
    (void)state;
    (void)mirror_archive_name;
    return export_loaded_gameplay_session_bundle(archive_name);
}
u32 DispatchOwnerProductionActionCommand(GameplayProductionActionState& state,
    u32 selector, i32 screen_x, i32 screen_y, u32 unit_offset) {
    state.selected_action_selector = selector;
    state.current_unit_offset = unit_offset;
    state.last_dispatch_failed = false;
    state.last_gate_failure = GameplayProductionGateFailure::none;

    i32 world_x = screen_x + static_cast<i32>(state.map_origin_x);
    i32 world_y = screen_y + static_cast<i32>(state.map_origin_y);
    state.last_world_x = static_cast<u32>(world_x);
    state.last_world_y = static_cast<u32>(world_y);

    if (selector >= kGameplayProductionSelectorCount) {
        reject_action_silently(state);
        return selector;
    }

    // FUN_004db0f7 has one explicit pre-dispatch footprint check: selector
    // 0x17 validates unit type 0x7d at an aligned world point.
    if (selector == kSpecialPreviewPlacementSelector &&
        !CheckPreviewProductionPlacementFootprintGateCells(state,
            kSpecialPreviewPlacementUnitType, world_x & ~0x1f,
            world_y & ~0x1f, state.selected_unit_offset)) {
        reject_action(state);
        return selector;
    }

    // DAT_008629be is exactly 32 bytes.  A one starts the click marker before
    // the JW2_11 mode is examined.
    if (state.selector_redirect_flags[selector] == 1u) {
        start_hud_pulse(state, world_x, world_y);
    }

    const GameplayProductionActionDefinition* definition =
        definition_for_selector(state, selector);
    if (definition == nullptr) {
        reject_action_silently(state);
        return selector;
    }

    if (definition->mode == 0u) {
        dispatch_action_index(state, selector, 0, false);
        return selector;
    }

    world_x = static_cast<i32>(
        clamp_world_axis(world_x, state.map_width_tiles));
    world_y = static_cast<i32>(
        clamp_world_axis(world_y, state.map_height_tiles));
    state.last_world_x = static_cast<u32>(world_x);
    state.last_world_y = static_cast<u32>(world_y);

    if (definition->mode == 1u) {
        dispatch_action_index(state, selector, 0, false);
        return selector;
    }

    if (state.current_snapshot_field2 == 1u) {
        if (definition->mode <= 2u) {
            dispatch_action_index(state, selector, 0, false);
        }
        else {
            stop_hud_pulse(state);
            reject_action(state);
        }
        return selector;
    }

    const bool high_mode = definition->mode > 3u;
    const bool has_action_hit =
        validate_action(state, selector, world_x, world_y, high_mode);
    if (!has_action_hit) {
        if (definition->mode < 3u) {
            dispatch_action_index(state, selector, 0, false);
        }
        else {
            // Low-mode 3 explicitly stops the marker at 0x004db1bd; the
            // high-mode validation failure jumps straight to the shared
            // feedback tail.
            if (!high_mode) {
                stop_hud_pulse(state);
            }
            reject_action(state);
        }
        return selector;
    }

    GameplayProductionUnitState* target =
        find_unit(state, state.last_validation_unit_offset);
    if (target == nullptr ||
        !action_allows_unit_movement_class(*definition, *target)) {
        if (definition->mode == 2u) {
            dispatch_action_index(state, selector, 0, false);
        }
        else {
            reject_action(state);
        }
        return selector;
    }

    stop_hud_pulse(state);
    dispatch_action_index(state, selector, target->offset, true);
    return selector;
}

bool PublishLinkedUnitCommand24IfIdle(GameplayProductionActionState& state) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr || unit->command_state != 0x45 ||
        unit->linked_unit_offset == 0 || unit->linked_unit_runtime_state != 0) {
        return false;
    }
    return publish(state, make_action(state, kSubtypeUnitOrder,
        unit->offset, 0x24, unit->linked_unit_offset));
}

bool PublishSelectedUnitEquipmentCommand(GameplayProductionActionState& state,
    u32 equipment_type) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr || !unit_is_local_active(state, *unit) ||
        !contains_value(unit->equipment_type_filter, equipment_type)) {
        return false;
    }
    return publish(state, make_action(state, kSubtypeEquipmentCommand,
        unit->offset, equipment_type));
}

bool DispatchCurrentEquipmentSlotAtPoint(GameplayProductionActionState& state,
    i32 screen_x, i32 screen_y) {
    const u32 world_x = clamp_world_axis(
        screen_x + static_cast<i32>(state.map_origin_x), state.map_width_tiles);
    const u32 world_y = clamp_world_axis(
        screen_y + static_cast<i32>(state.map_origin_y), state.map_height_tiles);
    state.last_world_x = world_x;
    state.last_world_y = world_y;
    start_hud_pulse(state, static_cast<i32>(world_x), static_cast<i32>(world_y));
    const u32 slot = select_action_index(state, state.selected_attachment_slot,
        static_cast<i32>(world_x), static_cast<i32>(world_y));
    return PublishSelectedUnitEquipmentSlotToggle(state, slot);
}

u32 visibility_layer_flags_or(const UnitMovementVisibilityLayers& layers,
    const std::vector<u32>* values, u32 tile_x, u32 tile_y, u32 fallback) {
    const u32 stride = layers.stride_tiles != 0 ? layers.stride_tiles : layers.width;
    if (values == nullptr || stride == 0 ||
        tile_x >= layers.width || tile_y >= layers.height) {
        return fallback;
    }

    const std::size_t index =
        static_cast<std::size_t>(tile_y) * stride + tile_x;
    return index < values->size() ? (*values)[index] : fallback;
}

void MirrorGameplayProductionPlacementMapFromTerrainFlags(
    GameplayProductionActionState& state, u32 width_tiles, u32 height_tiles,
    const std::vector<u32>& tile_flags, u32 stride_tiles) {
    state.placement_map.width = width_tiles;
    state.placement_map.height = height_tiles;
    const std::size_t cell_count =
        static_cast<std::size_t>(width_tiles) * height_tiles;
    state.placement_map.cells.assign(cell_count, GameplayProductionPlacementCell{});
    const u32 source_stride =
        std::max(width_tiles, stride_tiles != 0 ? stride_tiles : width_tiles);
    for (u32 y = 0; y < height_tiles; ++y) {
        for (u32 x = 0; x < width_tiles; ++x) {
            const std::size_t source_index =
                static_cast<std::size_t>(y) * source_stride + x;
            const std::size_t target_index =
                static_cast<std::size_t>(y) * width_tiles + x;
            if (source_index >= tile_flags.size() ||
                target_index >= state.placement_map.cells.size()) {
                continue;
            }
            const u32 flags = tile_flags[source_index];
            GameplayProductionPlacementCell& cell =
                state.placement_map.cells[target_index];
            cell.terrain_flags = flags;
            cell.live_terrain_flags = flags;
            cell.owner_flags = flags;
            cell.route_flags = flags;
            cell.current_visibility_flags = flags;
        }
    }
}

void MirrorGameplayProductionPlacementMapFromMovementMap(
    GameplayProductionActionState& state, const UnitMovementContext& movement) {
    const UnitMovementMap& map = movement.map;
    state.placement_map.width = map.width;
    state.placement_map.height = map.height;
    const std::size_t declared_count =
        static_cast<std::size_t>(map.width) * map.height;
    state.placement_map.cells.assign(declared_count,
        GameplayProductionPlacementCell{});
    for (u32 y = 0; y < map.height; ++y) {
        for (u32 x = 0; x < map.width; ++x) {
            const std::size_t source_index = UnitMovementMapTileIndex(map, x, y);
            const std::size_t target_index =
                static_cast<std::size_t>(y) * map.width + x;
            if (source_index >= map.cells.size() ||
                target_index >= state.placement_map.cells.size()) {
                continue;
            }
            const UnitMovementCell& source = map.cells[source_index];
            GameplayProductionPlacementCell& cell =
                state.placement_map.cells[target_index];
            cell.terrain_flags = visibility_layer_flags_or(
                movement.visibility_layers,
                movement.visibility_layers.terrain_backup_flags,
                x, y, source.flags);
            cell.live_terrain_flags = source.flags;
            cell.owner_flags = source.alternate_flags;
            cell.route_flags = visibility_layer_flags_or(
                movement.visibility_layers,
                movement.visibility_layers.previous_flags,
                x, y, source.visibility_flags);
            cell.current_visibility_flags = source.visibility_flags;
        }
    }
}

bool PublishSelectedUnitEquipmentSlotToggle(GameplayProductionActionState& state,
    u32 slot) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr || !unit_is_local_active(state, *unit) || slot == 0) {
        return false;
    }

    const u32 slot_code_index =
        std::min<u32>(slot, kGameplayProductionEquipmentSlots) - 1;
    const std::size_t index =
        kOriginalEquipmentSlotCodeToStorageIndex[slot_code_index];
    const u32 equipment_type = unit->equipment_slots[index];
    if (equipment_type == 0 ||
        !contains_value(unit->equipment_type_filter, equipment_type)) {
        return false;
    }
    const u32 command = state.shift_modifier_down
        ? slot | kIgnoredCommandHighBit
        : slot;
    return publish(state, make_action(state, kSubtypeEquipmentToggle,
        unit->offset, command, 0, state.last_world_x, state.last_world_y));
}

bool PublishSelectedUnitProductionCostAction(GameplayProductionActionState& state,
    u32 production) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr || !unit_is_local_active(state, *unit) ||
        unit->deferred_command_count >= 4) {
        return false;
    }

    const auto production_it = std::find(unit->production_cost_actions.begin(),
        unit->production_cost_actions.end(), production);
    if (production_it == unit->production_cost_actions.end()) {
        return false;
    }
    const u32 list_index = static_cast<u32>(
        std::distance(unit->production_cost_actions.begin(), production_it));
    const u32 remaining_count = static_cast<u32>(
        unit->production_cost_actions.size() - list_index);
    return publish(state, make_action(state, kSubtypeProductionCost,
        unit->offset, production, 0, remaining_count, list_index));
}

bool PublishSelectedUnitProductionCostCancel(GameplayProductionActionState& state) {
    const u32 command =
        state.last_action_index != 0 ? state.last_action_index :
        state.selected_action_selector;
    return PublishSelectedUnitProductionCostCancel(state, command, 0);
}

bool PublishSelectedUnitProductionCostCancel(GameplayProductionActionState& state,
    u32 command, u32 logical_index) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr) {
        return false;
    }
    return publish(state, make_action(state, kSubtypeProductionCost,
        unit->offset, command, 1, logical_index));
}

bool PublishSelectedUnitsStatusMaskToggle(GameplayProductionActionState& state,
    u32 required_high_flag) {
    const u32 target_high_flag = required_high_flag;
    bool published = false;
    state.selected_count = 0;
    for (const GameplayProductionUnitState& unit : state.units) {
        if ((unit.status_flags & kSelectedUnitMarkerFlag) == 0 ||
            unit.owner != state.local_player_index ||
            (unit.area_marker_flags & kIgnoredCommandHighBit) ==
                target_high_flag) {
            continue;
        }
        if ((unit.command_bits & (1u << 5)) != 0) {
            publish(state, make_action(state, kSubtypeStatusMask, unit.offset,
                kStatusMaskCommand, target_high_flag,
                state.last_world_x, state.last_world_y));
            ++state.selected_count;
            published = true;
        }
    }
    return published;
}

bool PublishSelectedUnitAuxStateAction(GameplayProductionActionState& state) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr || unit->action_mode_gate == 1 ||
        (unit->command_state & 0x10000000u) != 0) {
        return false;
    }
    return publish(state, make_action(state, kSubtypeAuxVector, unit->offset,
        0x1f, state.last_validation_unit_offset, state.last_world_x,
        state.last_world_y));
}

void NoOpProductionActionHandler(GameplayProductionActionState&) {
}

bool PublishVoteCompletionAndFlushReliableRange(GameplayProductionActionState& state,
    u32 marker_player) {
    const u32 payload_player =
        marker_player == 0xffffffffu ? state.local_player_index : marker_player;
    const bool result = publish(state, make_action(state, kSubtypeVoteCompletion, 0,
        payload_player));

    u32 start_sequence = state.reliable_range_start;
    u32 end_sequence = state.reliable_range_end;
    if (end_sequence <= start_sequence) {
        const Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
        start_sequence = reliable.local_broadcast_start;
        end_sequence = reliable.local_broadcast_end;
    }

    if (end_sequence > start_sequence) {
        if (state.callbacks.broadcast_reliable_range != nullptr) {
            state.callbacks.broadcast_reliable_range(state, start_sequence,
                end_sequence - 1);
        }
        else {
            BroadcastMode1PacketRange(start_sequence, end_sequence - 1);
        }
    }
    return result;
}

bool FindSelectedUnitMatchingAttachmentSlot(GameplayProductionActionState& state,
    u32 definition_id) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr) {
        return false;
    }

    for (u32 slot = 0; slot < kGameplayProductionAttachmentSlots; ++slot) {
        state.selected_attachment_slot = slot;
        // FUN_004db855 scans raw +0x30/+0x34/+0x38/+0x3c.  Those are the
        // four item/attachment slots, not the six equipment command slots.
        const bool attachment_present = unit->attachment_slots[slot] != 0;
        const u32 attachment_definition = unit->attachment_definition_ids[slot];
        if (attachment_present && attachment_definition != 0 &&
            attachment_definition == definition_id) {
            return true;
        }
    }
    return false;
}

bool CheckSelectedUnitCommandBit(GameplayProductionActionState& state, u32 bit_index) {
    GameplayProductionUnitState* unit = selected_unit(state);
    if (unit == nullptr || bit_index >= 32) {
        return false;
    }
    return (unit->command_bits & (1u << bit_index)) != 0;
}

bool CheckSelectedUnitProductionActionGate(GameplayProductionActionState& state,
    u32 selector) {
    GameplayProductionUnitState* unit = selected_unit(state);
    const GameplayProductionActionDefinition* definition =
        definition_for_selector(state, selector);
    if (unit == nullptr || definition == nullptr) {
        state.last_gate_failure = GameplayProductionGateFailure::none;
        return false;
    }
    if (FindSelectedUnitMatchingAttachmentSlot(state, selector)) {
        state.last_gate_failure = GameplayProductionGateFailure::none;
        return true;
    }
    static_cast<void>(definition->requires_capability_bit);
    static_cast<void>(definition->command_bit);
    if (selector >= kGameplayProductionSelectorCount ||
        (unit->production_bits & (1ull << selector)) == 0) {
        state.last_gate_failure = GameplayProductionGateFailure::none;
        return false;
    }
    if (!owner_requirement_allows_unit(state, *definition, *unit)) {
        state.last_gate_failure = GameplayProductionGateFailure::owner_requirement;
        return false;
    }
    state.last_gate_failure = GameplayProductionGateFailure::active_limit;
    if (definition->active_limit >= unit->active_count_metric + 1) {
        return false;
    }
    state.last_gate_failure = GameplayProductionGateFailure::queued_limit;
    if (definition->queued_limit >= unit->queued_count_metric + 1) {
        return false;
    }
    state.last_gate_failure = GameplayProductionGateFailure::resource_limit;
    if (definition->resource_limit >= unit->resource_metric) {
        return false;
    }
    state.last_gate_failure = GameplayProductionGateFailure::none;
    return true;
}

bool CheckLiveUnitProductionActionGate(GameplayProductionActionState& state,
    const UnitMovementUnit& source, u32 selector) {
    GameplayProductionUnitState* unit = find_unit(state, source.id);
    if (unit == nullptr) {
        state.last_gate_failure = GameplayProductionGateFailure::none;
        return false;
    }

    // FUN_004db92c reads these values directly from the raw source unit when
    // a deferred state-0x64 ability reaches its simulation-time gate.  The
    // UI/production mirror can be older than that command tick in P2P play.
    unit->owner = source.owner_id;
    unit->production_bits = source.script_bit_flags;
    unit->active_count_metric = source.status_timer;
    unit->queued_count_metric = source.secondary_value;
    unit->resource_metric = source.health;

    const u32 saved_current = state.current_unit_offset;
    state.current_unit_offset = source.id;
    const bool allowed = CheckSelectedUnitProductionActionGate(state, selector);
    state.current_unit_offset = saved_current;
    return allowed;
}

bool CheckProductionOwnerRequirementGate(GameplayProductionActionState& state,
    u32 selector) {
    GameplayProductionUnitState* unit = selected_unit(state);
    const GameplayProductionActionDefinition* definition =
        definition_for_selector(state, selector);
    if (unit == nullptr || definition == nullptr) {
        return false;
    }
    if (!owner_requirement_allows_unit(state, *definition, *unit)) {
        state.last_gate_failure = GameplayProductionGateFailure::owner_requirement;
        return false;
    }
    state.last_gate_failure = GameplayProductionGateFailure::active_limit;
    return definition->active_limit < unit->active_count_metric + 1;
}

bool CheckPreviewProductionPlacementFootprintGateCells(
    GameplayProductionActionState& state, u32 unit_type, i32 world_x, i32 world_y,
    u32 source_unit_offset) {
    const i32 tile_x = world_to_tile(world_x);
    const i32 tile_y = world_to_tile(world_y);
    if (tile_x < 0 || tile_y < 0 ||
        static_cast<u32>(tile_x) >= state.placement_map.width ||
        static_cast<u32>(tile_y) >= state.placement_map.height) {
        return false;
    }

    const GameplayProductionUnitFootprintDefinition* footprint =
        footprint_for_unit_type(state, unit_type);
    const u32 width = footprint == nullptr ? 1 : footprint->width;
    const u32 height = footprint == nullptr ? 1 : footprint->height;
    if (width == 0 || height == 0) {
        return false;
    }
    const GameplayProductionPlacementCell* origin =
        placement_cell(state.placement_map, static_cast<u32>(tile_x),
            static_cast<u32>(tile_y));
    state.preview_placement_terrain_class =
        origin != nullptr ? terrain_class(*origin) : 3;

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            if (!CheckPreviewProductionPlacementGateCell(state,
                    tile_x + static_cast<i32>(x),
                    tile_y + static_cast<i32>(y), source_unit_offset,
                    unit_type == 0x60 || unit_type == 0x70 ||
                        unit_type == 0x80 || unit_type == 0x90)) {
                return false;
            }
        }
    }
    return true;
}

bool CheckPreviewProductionPlacementGateCell(GameplayProductionActionState& state,
    i32 tile_x, i32 tile_y, u32 source_unit_offset, bool allow_nearby_probe) {
    if (tile_x < 0 || tile_y < 0) {
        return false;
    }
    const GameplayProductionPlacementCell* cell =
        placement_cell(state.placement_map, static_cast<u32>(tile_x),
            static_cast<u32>(tile_y));
    if (cell == nullptr ||
        // FUN_004dbae2:004dbaff reads the last-visible E59E74 layer here;
        // F19E74 is consulted separately below and by the nearby probe.
        (cell->terrain_flags & kPreviewPlacementBlockedMask) != 0 ||
        (cell->owner_flags & kPreviewPlacementTerrainValidFlag) == 0 ||
        (cell->route_flags & kPreviewPlacementTemporaryBlock) != 0) {
        return false;
    }

    // 798D40 bits 27/28 are the local process' explored-fog projection.  The
    // original executable consequently sees different bit 28 values on two
    // P2P peers when only one player's units have explored a cell.  Action
    // 0x17 consults this presentation-local bit from simulation code, which
    // is an original desync bug.  Project the command source owner's
    // persistent explored bit from DAT_007d8d40 so both simulations make the
    // same decision without rejecting a placement that the caster can see.
    bool authority_route_visible =
        (cell->route_flags & kPreviewPlacementRouteRequiredFlag) != 0;
    if (state.preview_placement_authority_player < 32u &&
        state.preview_placement_authority_player != state.local_player_index) {
        authority_route_visible =
            (cell->visibility_owner_flags &
                (1u << state.preview_placement_authority_player)) != 0;
    }
    if (!authority_route_visible) {
        return false;
    }

    if (terrain_class(*cell) != state.preview_placement_terrain_class) {
        return false;
    }
    const GameplayProductionUnitState* source_unit =
        find_unit(state, source_unit_offset);
    const u32 source_owner = source_unit != nullptr ?
        source_unit->owner : state.local_player_index;
    const u32 source_current_bit =
        source_owner + kPreviewPlacementCurrentOwnerShift;
    const bool source_currently_sees_cell = source_current_bit < 32 &&
        (cell->current_visibility_flags & (1u << source_current_bit)) != 0;
    // FUN_004dbae2:004dbb53..004dbb74 only applies F19E74 bit 30 when
    // the selected source owner's current-visibility bit is present.
    if (source_currently_sees_cell &&
        (cell->live_terrain_flags & kPreviewPlacementRouteBlockedFlag) != 0) {
        return false;
    }
    if (allow_nearby_probe &&
        has_nearby_passable_placement_tile(state, tile_x, tile_y)) {
        return false;
    }

    const u32 ignored_source_offset =
        source_unit != nullptr && source_unit->type == 0x10 ?
        std::numeric_limits<u32>::max() : source_unit_offset;
    for (const GameplayProductionUnitState& unit : state.units) {
        if (!unit.active || unit.offset == ignored_source_offset ||
            (unit.runtime_flags & 0x80u) != 0 || unit.movement_class == 3) {
            continue;
        }
        if (world_to_tile(unit.x) == tile_x && world_to_tile(unit.y) == tile_y) {
            if (!preview_collision_visibility_gate(
                    state, *cell, unit, source_owner)) {
                continue;
            }
            if ((cell->route_flags & kPreviewPlacementCollisionFlag) != 0) {
                return false;
            }
        }
    }
    return true;
}

void ResetQueuedProductionPlacementCommands(GameplayProductionActionState& state) {
    state.queued_commands.clear();
}

void QueueProductionPlacementCommand(GameplayProductionActionState& state,
    const GameplayProductionQueuedCommand& command) {
    state.queued_commands.push_back(command);
}

void FlushQueuedProductionPlacementCommands(GameplayProductionActionState& state) {
    if (state.queued_commands.empty()) {
        return;
    }

    i32 min_x = std::numeric_limits<i32>::max();
    i32 min_y = std::numeric_limits<i32>::max();
    i32 max_x = 0;
    i32 max_y = 0;
    u32 footprint_area = 0;

    for (const GameplayProductionQueuedCommand& command : state.queued_commands) {
        const GameplayProductionUnitState* unit = find_unit(state, command.unit_offset);
        const GameplayProductionUnitFootprintDefinition* footprint =
            footprint_for_command(state, command, unit);
        const i32 unit_x = unit == nullptr ? command.unit_x : unit->x;
        const i32 unit_y = unit == nullptr ? command.unit_y : unit->y;
        const i32 layout_x = footprint == nullptr ? 0 : footprint->layout_offset_x;
        const i32 layout_y = footprint == nullptr ? 0 : footprint->layout_offset_y;
        const i32 layout_width = command_layout_width(command, footprint);
        const i32 layout_height = command_layout_height(command, footprint);
        const i32 left = unit_x + layout_x;
        const i32 top = unit_y + layout_y;
        min_x = std::min(min_x, left);
        min_y = std::min(min_y, top);
        max_x = std::max(max_x, left + layout_width + 2);
        max_y = std::max(max_y, top + layout_height + 2);
        footprint_area += unit_layout_area(command, footprint);
    }

    state.layout_metrics.min_x = min_x;
    state.layout_metrics.min_y = min_y;
    state.layout_metrics.max_x = max_x;
    state.layout_metrics.max_y = max_y;
    state.layout_metrics.footprint_area = footprint_area;
    state.layout_metrics.bounding_area =
        static_cast<u32>(std::max(0, max_x - min_x) * std::max(0, max_y - min_y));

    GameplayProductionQueuedCommand& first = state.queued_commands.front();
    const bool anchor_outside =
        first.world_x < min_x || first.world_x > max_x ||
        first.world_y < min_y || first.world_y > max_y;
    if (footprint_area < state.layout_metrics.bounding_area && anchor_outside &&
        state.layout_metrics.bounding_area / 4 <= footprint_area) {
        const i32 center_x = min_x + ((max_x - min_x) >> 1);
        const i32 center_y = min_y + ((max_y - min_y) >> 1);
        for (GameplayProductionQueuedCommand& command : state.queued_commands) {
            const GameplayProductionUnitState* unit =
                find_unit(state, command.unit_offset);
            const i32 unit_x = unit == nullptr ? command.unit_x : unit->x;
            const i32 unit_y = unit == nullptr ? command.unit_y : unit->y;
            command.world_x += unit_x - center_x;
            command.world_y += unit_y - center_y;
        }
    }
    else if (footprint_area < state.layout_metrics.bounding_area) {
        const u32 side = static_cast<u32>(
            std::lround(std::sqrt(static_cast<double>(footprint_area))));
        i32 x = first.world_x - static_cast<i32>(side >> 1);
        i32 y = first.world_y - static_cast<i32>(side >> 1);
        const i32 row_start = x;
        const i32 row_end = row_start + static_cast<i32>((side >> 1) * 2);
        u32 row_height = 0;
        for (GameplayProductionQueuedCommand& command : state.queued_commands) {
            const GameplayProductionUnitState* unit =
                find_unit(state, command.unit_offset);
            const GameplayProductionUnitFootprintDefinition* footprint =
                footprint_for_command(state, command, unit);
            const i32 layout_width = command_layout_width(command, footprint);
            const i32 layout_height = command_layout_height(command, footprint);
            command.world_x = x;
            command.world_y = y;
            x += layout_width + 2;
            row_height = std::max<u32>(row_height,
                static_cast<u32>(layout_height + 2));
            if (x > row_end) {
                y += static_cast<i32>(row_height);
                row_height = 0;
                x = row_start;
            }
        }
    }

    for (const GameplayProductionQueuedCommand& command : state.queued_commands) {
        GameplayProductionQueuedCommand emitted = command;
        if (state.shift_modifier_down) {
            emitted.selector |= kIgnoredCommandHighBit;
        }
        if (state.callbacks.dispatch_queued_command != nullptr) {
            state.callbacks.dispatch_queued_command(state, emitted);
        }
        else {
            publish(state, make_queued_action(emitted));
        }
    }
    state.queued_commands.clear();
}

void ExportLastDropSessionBundleMirror(GameplayProductionActionState& state) {
    state.last_drop_path_symbol = kLastDropSessionArchiveSymbol;
    state.session_export_archive_name = kLastDropSessionArchiveName;
    state.session_export_mirror_archive_name = kLastDropSessionArchiveName;
    state.session_export_requested = true;
    if (state.callbacks.export_session_bundle != nullptr) {
        state.session_export_succeeded = state.callbacks.export_session_bundle(
            state, state.session_export_archive_name.c_str(),
            state.session_export_mirror_archive_name.c_str());
        return;
    }
    state.session_export_succeeded =
        export_loaded_gameplay_session_bundle(state.session_export_archive_name.c_str());
}

}
