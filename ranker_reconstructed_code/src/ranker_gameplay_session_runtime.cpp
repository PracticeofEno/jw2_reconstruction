#include "ranker_gameplay_session_runtime.h"

#include "ranker_game_session_tables.h"
#include "ranker_gameplay_script.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace ranker {
namespace {

constexpr u32 kGameplayStartupPlacedUnitIdBase = 0x70000000u;

bool slot_state_is_disabled(u8 state) {
    return state == static_cast<u8>(PlayerSlotState::disabled);
}

bool slot_state_is_player(u8 state) {
    return state == static_cast<u8>(PlayerSlotState::player_controlled);
}

bool slot_state_is_observer(u8 state) {
    return state == static_cast<u8>(PlayerSlotState::observer);
}

bool slot_state_is_rotation_reserve(u8 state) {
    return state == static_cast<u8>(PlayerSlotState::rotation_reserve);
}

bool owner_slot_is_disabled(const GameplaySessionRuntimeResetState& state,
    u32 owner) {
    return state.players != nullptr && owner < state.players->slot_states.size() &&
        slot_state_is_disabled(state.players->slot_states[owner]);
}

void clear_normal_session_preserved_unit_slots(UnitMovementUnit& unit) {
    // FUN_00426770 clears exactly the six raw +0x30..+0x44 words of a
    // preserved owner>=8 unit.  Command state, action_mode (neutral meat
    // amount), targets and deferred commands all survive this pass.
    unit.equipment_slots.fill(0);
    unit.item_slots.fill(0);
    if (unit.type_id >= 0x60) {
        // Structures interpret the first raw word as the construction gate
        // rather than an equipment id; keep that typed alias coherent too.
        unit.action_mode_gate = 0;
        unit.under_construction = false;
    }
}

void erase_inactive_units(UnitMovementContext& movement) {
    movement.active_units.erase(
        std::remove_if(movement.active_units.begin(), movement.active_units.end(),
            [](const UnitMovementUnit* unit) {
                return unit == nullptr || !unit->active;
            }),
        movement.active_units.end());
}

void reset_units_for_session_runtime(GameplaySessionRuntimeResetState& state) {
    if (state.lifecycle == nullptr || state.lifecycle->movement == nullptr) {
        return;
    }

    UnitMovementContext& movement = *state.lifecycle->movement;
    const std::vector<UnitMovementUnit*> active_units = movement.active_units;
    for (UnitMovementUnit* unit : active_units) {
        if (unit == nullptr || !unit->active) {
            continue;
        }

        const bool owner_scoped = unit->owner_id < kPlayerSlotCount;
        bool remove_unit = false;
        if (state.session_mode == 5) {
            remove_unit = owner_scoped && owner_slot_is_disabled(state, unit->owner_id);
        }
        else {
            remove_unit = owner_scoped;
        }

        if (remove_unit) {
            HandleUnitRemovalAccounting(*state.lifecycle, *unit);
            ++state.units_removed;
        }
        else {
            if (state.session_mode != 5) {
                clear_normal_session_preserved_unit_slots(*unit);
            }
            ++state.units_preserved;
        }

        if (state.callbacks.on_unit_reset_or_removed != nullptr) {
            state.callbacks.on_unit_reset_or_removed(state, *unit);
        }
    }

    erase_inactive_units(movement);
    HandleOwnerUnitTypeCountRebuild(*state.lifecycle);
}

void import_session_runtime_tables(GameplaySessionRuntimeResetState& state) {
    if (state.import_state == nullptr || state.active_definitions == nullptr ||
        state.staged_definitions == nullptr) {
        return;
    }

    const bool base_imported = ImportSessionRuntimeDefinitionTables(
        *state.import_state, *state.active_definitions, *state.staged_definitions);
    state.definition_tables_imported = base_imported;

    if (state.session_mode == 5) {
        state.non_empty_runtime_tables_imported =
            ImportNonEmptySessionRuntimeDefinitionTables(*state.import_state,
                *state.active_definitions, *state.staged_definitions);
        state.definition_tables_imported =
            base_imported && state.non_empty_runtime_tables_imported;
    }
}

void reset_owner_counters(GameplaySessionRuntimeResetState& state) {
    if (state.owner_counters != nullptr) {
        ResetOwnerSessionCounterTables(*state.owner_counters);
        state.owner_counters_reset = true;
    }
}

void reset_script_runtime(GameplaySessionRuntimeResetState& state) {
    if (state.script_dialog != nullptr) {
        ResetGameplayScriptDialogRuntimeState(*state.script_dialog);
        state.script_dialog_reset = true;
    }
    if (state.script_triggers != nullptr) {
        InitializeGameplayScriptTriggerState(*state.script_triggers);
        state.script_trigger_initialized = true;
    }
}

void refresh_owner_display_names(GameplaySessionRuntimeResetState& state) {
    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        if (state.players != nullptr &&
            state.players->slot_states[owner] ==
                static_cast<u8>(PlayerSlotState::player_controlled)) {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%s %u",
                state.default_player_name.c_str(), owner);
            state.owner_display_names[owner] = buffer;
        }
        else if (state.owner_display_names[owner].empty()) {
            state.owner_display_names[owner] = state.default_player_name;
        }

        if (state.callbacks.update_owner_display_name != nullptr) {
            state.callbacks.update_owner_display_name(state, owner);
        }
    }
}

void apply_post_init_transition_state(GameplaySessionRuntimeResetState& state) {
    state.post_init_transition_pending =
        state.session_mode > 2 &&
        (state.session_mode < 5 || state.session_mode == 7);

    if (state.post_init_transition_pending && state.post_init_snapshot != nullptr) {
        RestorePostInitTransitionRuntimeSnapshot(*state.post_init_snapshot);
        state.post_init_snapshot_restored = true;
    }

    if (state.unit_reference_tables != nullptr) {
        state.unit_reference_tables->post_init_transition_pending =
            state.post_init_transition_pending;
        ApplyPostInitUnitRequirementToggle(*state.unit_reference_tables);
        state.unit_requirement_toggle_applied = true;
        RebuildUnitTypeReverseReferenceTables(*state.unit_reference_tables);
        state.reverse_reference_tables_rebuilt = true;
    }
}

bool mode_sets_runtime_command_defaults(u32 session_mode) {
    switch (session_mode) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 7:
    case 8:
        return true;
    default:
        return false;
    }
}

bool should_place_start_units(
    const GameplayScenarioOwnerSlot& slot, u32 session_mode, bool exclude_observer_slots) {
    if (slot_state_is_disabled(slot.slot_state) || session_mode == 5) {
        return false;
    }
    if (slot_state_is_player(slot.slot_state)) {
        return true;
    }
    if (slot_state_is_rotation_reserve(slot.slot_state)) {
        return false;
    }
    if (exclude_observer_slots && slot_state_is_observer(slot.slot_state)) {
        return false;
    }
    return true;
}

void mirror_owner_slots(GameplaySessionStartupState& state) {
    if (state.players == nullptr) {
        return;
    }

    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        const GameplayScenarioOwnerSlot& slot = state.owner_slots[owner];
        state.players->slot_states[owner] = slot.slot_state;
        state.owner_faction_ids[owner] = slot.faction_id;
        state.owner_tribe_ids[owner] = slot.tribe_id;
        // The original runtime keeps its start-coordinate table in raw map
        // slot order (DAT_007253D4/D8), separately from the owner-to-map-slot
        // permutation used to place each owner's units.
        const u32 map_slot = std::min<u32>(slot.map_slot, kPlayerSlotCount - 1);
        state.players->owner_start_x[map_slot] = slot.start_x;
        state.players->owner_start_y[map_slot] = slot.start_y;
        if (state.lifecycle != nullptr &&
            owner < state.lifecycle->owner_faction_ids.size()) {
            state.lifecycle->owner_faction_ids[owner] = slot.faction_id;
        }
    }

    state.players->local_player_slot = std::min<u32>(state.local_owner_id,
        kPlayerSlotCount - 1);
    InitializeGameplaySessionPlayerSlotState(
        *state.players, state.session_mode, state.rotation_reset_units);
}

void append_snapshot(GameplaySessionStartupState& state) {
    state.snapshot.assign(kGameplaySessionSnapshotBytes, 0);
    if (state.players != nullptr) {
        for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
            const std::size_t base = owner * 8;
            if (base + 7 >= state.snapshot.size()) {
                break;
            }
            state.snapshot[base] = state.players->slot_states[owner];
            state.snapshot[base + 1] = static_cast<u8>(state.owner_faction_ids[owner]);
            state.snapshot[base + 2] = static_cast<u8>(state.owner_tribe_ids[owner]);
            state.snapshot[base + 3] = static_cast<u8>(state.players->nearest_hostile_slots[owner]);
            const u32 sx = static_cast<u32>(state.players->owner_start_x[owner]);
            const u32 sy = static_cast<u32>(state.players->owner_start_y[owner]);
            state.snapshot[base + 4] = static_cast<u8>(sx);
            state.snapshot[base + 5] = static_cast<u8>(sx >> 8);
            state.snapshot[base + 6] = static_cast<u8>(sy);
            state.snapshot[base + 7] = static_cast<u8>(sy >> 8);
        }
    }

    if (state.callbacks.after_session_snapshot != nullptr) {
        state.callbacks.after_session_snapshot(state.snapshot);
    }
}

void place_starting_units(GameplaySessionStartupState& state,
    const GameplayScenarioOwnerSlot& slot, u32 owner) {
    if (state.lifecycle == nullptr) {
        return;
    }

    UnitMovementUnit first_unit;
    i32 first_x = slot.start_x;
    i32 first_y = slot.start_y;
    if (!InitializePlacedUnitFromMapSlot(
            *state.lifecycle, first_unit, slot.starting_unit_type, owner,
            first_x, first_y)) {
        return;
    }
    first_unit.id = kGameplayStartupPlacedUnitIdBase +
        static_cast<u32>(state.placed_units.size() + 1);
    HandleUnitCreationRegisterFootprint(*state.lifecycle, first_unit);
    first_unit.health = first_unit.max_health;
    state.placed_units.push_back(first_unit);
    if (state.callbacks.on_unit_placed != nullptr) {
        state.callbacks.on_unit_placed(state.placed_units.back());
    }

    if (owner == state.local_owner_id && state.callbacks.initialize_local_camera != nullptr) {
        state.callbacks.initialize_local_camera(first_unit.x, first_unit.y);
        state.local_camera_initialized = true;
    }

    const u32 saved_terrain_class = state.lifecycle->placement_terrain_class_override;
    const bool saved_terrain_override =
        state.lifecycle->placement_terrain_class_override_enabled;
    state.lifecycle->placement_terrain_class_override =
        GetPlacementTerrainClass(*state.lifecycle,
            static_cast<u32>(slot.start_x) >> 5,
            static_cast<u32>(slot.start_y) >> 5);
    state.lifecycle->placement_terrain_class_override_enabled = true;

    const UnitMovementDefinition first_definition = first_unit.definition;
    UnitMovementUnit secondary_probe;
    const UnitMovementDefinition* secondary_definition = nullptr;
    if (state.lifecycle->callbacks.find_definition != nullptr) {
        secondary_definition = state.lifecycle->callbacks.find_definition(
            *state.lifecycle, slot.secondary_starting_unit_type);
    }
    if (secondary_definition == nullptr) {
        secondary_probe.type_id = slot.secondary_starting_unit_type;
        secondary_probe.definition = first_definition;
        secondary_definition = &secondary_probe.definition;
    }

    i32 planned_x = slot.start_x + first_definition.startup_followup_offset.x;
    const i32 y = slot.start_y + first_definition.startup_followup_offset.y;
    i32 step_x = secondary_definition->startup_secondary_step_x + 2;

    for (u32 index = 1; index < kGameplayStartupUnitsPerSlot; ++index) {
        // InitializePlacedUnitFromMapSlot may move its x/y reference to a
        // nearby legal tile.  The original preserves the planned sequence
        // coordinates across each call, so keep placement output separate.
        planned_x += step_x;
        i32 placed_x = planned_x;
        i32 placed_y = y;
        UnitMovementUnit unit;
        if (!InitializePlacedUnitFromMapSlot(
                *state.lifecycle, unit, slot.secondary_starting_unit_type,
                owner, placed_x, placed_y)) {
            continue;
        }
        unit.id = kGameplayStartupPlacedUnitIdBase +
            static_cast<u32>(state.placed_units.size() + 1);
        state.placed_units.push_back(unit);
        if (state.callbacks.on_unit_placed != nullptr) {
            state.callbacks.on_unit_placed(state.placed_units.back());
        }
    }

    state.lifecycle->placement_terrain_class_override = saved_terrain_class;
    state.lifecycle->placement_terrain_class_override_enabled =
        saved_terrain_override;
}

void start_gameplay_session(GameplaySessionStartupState& state,
    bool exclude_observer_slots) {
    if (state.callbacks.reset_runtime_objects != nullptr) {
        state.callbacks.reset_runtime_objects();
    }

    ResetGameplayPlayerReadyFlags(state);
    state.non_player_slot_count = 0;
    state.frame_counter = 0;
    state.local_camera_initialized = false;
    state.placed_units.clear();
    state.placed_units.reserve(kPlayerSlotCount * kGameplayStartupUnitsPerSlot);
    mirror_owner_slots(state);

    if (state.callbacks.reset_owner_ai != nullptr) {
        state.callbacks.reset_owner_ai();
    }

    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        const GameplayScenarioOwnerSlot& slot = state.owner_slots[owner];
        if (!slot_state_is_disabled(slot.slot_state) && !slot_state_is_player(slot.slot_state)) {
            ++state.non_player_slot_count;
        }
        if (!should_place_start_units(slot, state.session_mode, exclude_observer_slots)) {
            continue;
        }

        place_starting_units(state, slot, owner);
    }

    if (state.players != nullptr) {
        SelectNearestHostilePlayerSlots(*state.players);
    }
    append_snapshot(state);
}

void queue_online_request(
    const GameplayOnlineTransitionState& state, GameplayOnlineRequest request) {
    if (state.callbacks.queue_request != nullptr) {
        state.callbacks.queue_request(request);
    }
}

} // namespace

void ConfigureGameplayDisplay800x600(GameplayDisplayState& state) {
    if (state.callbacks.reset_runtime_overlay != nullptr) {
        state.callbacks.reset_runtime_overlay();
    }

    state.local_owner_id = std::min<u32>(state.local_owner_id, kPlayerSlotCount - 1);
    state.session_start_requested = true;
    state.width = kGameplayDefaultScreenWidth;
    state.height = kGameplayDefaultScreenHeight;

    if (state.callbacks.frame_boundary != nullptr) {
        state.callbacks.frame_boundary();
    }
    if (state.callbacks.configure_surfaces != nullptr) {
        state.callbacks.configure_surfaces(state.width, state.height, state.color_depth);
    }
}

void ResetGameplaySessionRuntimeUnits(GameplaySessionRuntimeResetState& state) {
    reset_units_for_session_runtime(state);
}

void InitializeGameplaySessionRuntimeState(GameplaySessionRuntimeResetState& state) {
    import_session_runtime_tables(state);
    ResetGameplaySessionRuntimeUnits(state);

    if (state.callbacks.reset_effect_runtime != nullptr) {
        state.callbacks.reset_effect_runtime(state);
    }
    state.effect_runtime_reset = true;

    // FUN_00426770 fills DAT_00725474 only for normal sessions, after the
    // runtime-definition and unit/effect reset passes.  Mode 5 preserves the
    // availability imported from the session and non-empty overrides.
    if (state.session_mode != 5 && state.import_state != nullptr) {
        for (auto& owner : state.import_state->owner_unit_availability) {
            owner.fill(1);
        }
    }

    reset_owner_counters(state);
    reset_script_runtime(state);

    if (state.players != nullptr) {
        InitializeGameplaySessionPlayerSlotState(
            *state.players, state.session_mode, state.rotation_reset_units);
    }

    refresh_owner_display_names(state);
    apply_post_init_transition_state(state);

    if (mode_sets_runtime_command_defaults(state.session_mode)) {
        state.command_mode_a = 4;
        state.command_mode_b = 4;
    }

    if (state.callbacks.reset_ui_runtime_flags != nullptr) {
        state.callbacks.reset_ui_runtime_flags(state);
    }
    state.ui_runtime_flags_reset = true;
}

void StartGameplaySessionFromScenarioSlots(GameplaySessionStartupState& state) {
    start_gameplay_session(state, true);
}

void StartGameplaySessionFromScenarioSlotsIncludingObservers(
    GameplaySessionStartupState& state) {
    start_gameplay_session(state, false);
}

void ResetGameplayPlayerReadyFlags(GameplaySessionStartupState& state) {
    state.ready_flags.fill(0);
}

void RequestGameplayStateWhenNoPlayersReady(
    GameplaySessionStartupState& state, u32 requested_state) {
    if (state.requested_state != 0) {
        return;
    }
    if (std::any_of(state.ready_flags.begin(), state.ready_flags.end(),
            [](u8 ready) { return ready == 1; })) {
        return;
    }
    state.requested_state = requested_state;
}

void UpdateGameplayOnlineTransitionState(GameplayOnlineTransitionState& state) {
    const u32 original_state = state.current_state;
    state.previous_state = original_state;

    if (state.remote_player_count < 2) {
        state.current_state = 2;
        if (original_state > 2) {
            const bool can_auto_win =
                state.frame_counter >= kGameplayOnlineAutoTransitionFrame &&
                !slot_state_is_observer(state.local_slot_state);
            state.previous_state = can_auto_win ? 1 : 2;
        }
    } else {
        if (state.current_state == 1) {
            queue_online_request(state, GameplayOnlineRequest::locale_flag1);
            goto publish;
        }
        if (state.current_state != 2) {
            if (state.current_state != 3 && state.current_state != 4) {
                queue_online_request(state, GameplayOnlineRequest::locale_flag0);
                goto publish;
            }
            if (state.frame_counter >= kGameplayOnlineAutoTransitionFrame &&
                !slot_state_is_observer(state.local_slot_state)) {
                state.current_state = 1;
                state.previous_state = 1;
                queue_online_request(state, GameplayOnlineRequest::locale_flag1);
                goto publish;
            }
            state.previous_state = 2;
            state.current_state = 2;
        }
    }

    queue_online_request(state, GameplayOnlineRequest::locale_flag2);

publish:
    if (state.callbacks.publish_state != nullptr) {
        state.callbacks.publish_state(state.previous_state);
    }
    if (state.callbacks.fade_to_black != nullptr) {
        state.callbacks.fade_to_black();
    }
    if (state.callbacks.frame_boundary != nullptr) {
        state.callbacks.frame_boundary();
    }
}

void LeaveGameplaySessionAndResetReplay(GameplayLeaveState& state) {
    state.leave_request_cleared = true;
    if (state.callbacks.set_music_policy_mode != nullptr) {
        state.callbacks.set_music_policy_mode(2);
    }
    state.receive_dispatch_mode = 0;
    if (state.callbacks.set_receive_dispatch_mode != nullptr) {
        state.callbacks.set_receive_dispatch_mode(0);
    }

    const bool keep_directplay =
        state.network_mode == 0 || state.network_mode == 1 ||
        state.network_mode == 2 || state.network_mode == 6;
    if (!keep_directplay) {
        if (state.callbacks.close_directplay_player != nullptr) {
            state.callbacks.close_directplay_player();
        }
        if (state.callbacks.shutdown_directplay_session != nullptr) {
            state.callbacks.shutdown_directplay_session();
        }
    }

    if (state.callbacks.reset_replay_state != nullptr) {
        state.callbacks.reset_replay_state("Test Rec");
    }
}

} // namespace ranker
