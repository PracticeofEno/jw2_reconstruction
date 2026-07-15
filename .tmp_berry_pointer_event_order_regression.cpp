#include "ranker_reconstructed_code/src/ranker_winmain.cpp"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

std::vector<Mode1ReliablePacket> g_packets;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

u32 packed_point(i32 x, i32 y) {
    return (static_cast<u32>(y) << 16) |
        (static_cast<u32>(x) & 0xffffu);
}

bool publish_ok(GameplayInputActionState&, const GameplayPublishedAction&) {
    return true;
}

void capture_packet(const Mode1ReliablePacket& packet, void*) {
    g_packets.push_back(packet);
}

u32 packet_u32(const Mode1ReliablePacket& packet, std::size_t offset) {
    u32 value = 0;
    require(offset + sizeof(value) <= packet.bytes.size(),
        "packet field must be in bounds");
    std::memcpy(&value, packet.bytes.data() + offset, sizeof(value));
    return value;
}

struct Fixture {
    static constexpr u32 kWidth = 128;
    static constexpr u32 kHeight = 128;
    static constexpr i32 kMouseX = 144;
    static constexpr i32 kMouseY = 316;
    static constexpr i32 kCameraY = 1908;
    static constexpr u32 kWorkerId = 28768;

    UnitMovementUnit worker{};

    Fixture() {
        ResetInputState();
        ResetMode1GameplayPacketDispatch();
        ResetMode1ReliablePacketState(0);
        Mode1ReliableCallbacks reliable_callbacks{};
        reliable_callbacks.packet_advanced = capture_packet;
        SetMode1ReliableCallbacks(reliable_callbacks);
        SetMode1ReliableLocalPlayerIndex(1);
        g_packets.clear();

        UiOverlayState& overlay = ui_overlay_state();
        overlay = {};
        overlay.screen_width = 800;
        overlay.screen_height = 600;
        overlay.camera_x = 0;
        overlay.camera_y = kCameraY;
        overlay.selected_unit_ids = {kWorkerId};
        overlay.selected_unit_id = kWorkerId;
        overlay.selected_unit_count = 1;
        overlay.local_player_slot = 1;
        overlay.held_command_id = 0xffffffffu;
        overlay.pressed_command_id = 0xffffffffu;
        overlay.minimap.output_x = 339;
        overlay.minimap.output_y = 484;
        overlay.minimap.output_width_pixels = 96;
        overlay.minimap.output_height_pixels = 96;

        worker.id = kWorkerId;
        worker.owner_id = 1;
        worker.type_id = 0x01;
        worker.type_flags = (1u << 4) | (1u << 7);
        worker.command_state = 1;
        worker.command_flags = 0x20;
        worker.x = 448;
        worker.y = 2080;
        worker.max_health = 100;
        worker.health = 100;
        worker.runtime_flags = 1u;
        worker.definition.bounds_width = 0x20;
        worker.definition.bounds_height = 0x20;
        worker.active = true;

        UnitMovementContext& movement = g_runtime.gameplay_movement_context;
        movement = {};
        movement.map.width = kWidth;
        movement.map.height = kHeight;
        movement.map.stride_tiles = kWidth;
        movement.map.cells.resize(kWidth * kHeight);
        movement.active_units.push_back(&worker);

        g_runtime.gameplay_lifecycle_context = {};
        g_runtime.gameplay_lifecycle_context.movement = &movement;
        g_runtime.gameplay_startup_state.lifecycle =
            &g_runtime.gameplay_lifecycle_context;
        g_runtime.gameplay_player_slots.local_player_slot = 1;
        g_runtime.gameplay_player_slots.slot_states[1] = 1;
        ResetTerrainTilePulseState(g_runtime.gameplay_terrain_pulse_state);

        GameplayVisibilityGrid& visibility = g_runtime.gameplay_visibility_grid;
        visibility = {};
        visibility.width = kWidth;
        visibility.height = kHeight;
        visibility.current.assign(kWidth * kHeight, 0x18000000u);
        visibility.previous.assign(kWidth * kHeight, 0x10000000u);
        visibility.owner.assign(kWidth * kHeight, 0);
        visibility.terrain.assign(kWidth * kHeight, 0);
        visibility.terrain_backup.assign(kWidth * kHeight, 0);
        const u32 tile_x = static_cast<u32>(kMouseX) >> 5;
        const u32 tile_y = static_cast<u32>(kCameraY + kMouseY) >> 5;
        visibility.terrain_backup[tile_y * kWidth + tile_x] =
            (4000u << 12) | 0x100u;
        movement.map.cells[tile_y * kWidth + tile_x].flags =
            visibility.terrain_backup[tile_y * kWidth + tile_x];
        movement.visibility_layers.previous_flags = &visibility.previous;
        movement.visibility_layers.terrain_backup_flags =
            &visibility.terrain_backup;
        movement.visibility_layers.width = kWidth;
        movement.visibility_layers.height = kHeight;
        movement.visibility_layers.stride_tiles = kWidth;

        GameplayTooltipState& tooltip = gameplay_tooltip_state();
        tooltip.map_width_tiles = kWidth;
        tooltip.map_height_tiles = kHeight;
        tooltip.camera_x = 0;
        tooltip.camera_y = kCameraY;
        tooltip.terrain_flags = visibility.terrain_backup;

        GameplayInputActionState& input = gameplay_input_action_state();
        input = {};
        InitializeOriginalGameplayInputActionTables(input);
        configure_default_gameplay_input_action_context(input);
        input.callbacks.publish_action = publish_ok;

        // The JW2_11 effect record for action 7 carries direction mode 3.
        // That field feeds effect execution; it is not FUN_004da02c's
        // selector-mode byte, whose original action-7 value is 1.
        g_runtime.gameplay_unit_effect_definitions_initialized = true;
        UnitEffectRuntimeState& effects =
            g_runtime.gameplay_unit_effect_runtime;
        effects.definitions.clear();
        UnitEffectDefinition action7_effect{};
        action7_effect.id = 0x3du + 7u;
        action7_effect.action_direction_mode = 3;
        action7_effect.action_area_target_render_class_mask = 0xffffffffu;
        effects.definitions.push_back(action7_effect);
    }

    void send_move_down_up_without_intervening_frame() {
        const u32 point = packed_point(kMouseX, kMouseY);
        require(HandlePointerMotion(point), "WM_MOUSEMOVE must be accepted");
        require(!HasQueuedInputEvent(),
            "WM_MOUSEMOVE must reproduce the unqueued original input path");
        require(HandleRightButtonDown(2, point),
            "WM_RBUTTONDOWN must be queued");
        require(HandleRightButtonUp(0, point),
            "WM_RBUTTONUP must be queued");
        PumpGameplayInputAndCursorFrame(gameplay_input_action_state());
    }
};

} // namespace

int main() {
    Fixture fixture;
    UiOverlayState& overlay = ui_overlay_state();
    GameplayInputActionState& input = gameplay_input_action_state();
    sync_default_gameplay_input_action_units(input, overlay);
    require(input.selector_modes[7] == 1u,
        "runtime effect direction mode must not overwrite original selector 7 mode");
    require(overlay.hover_context.kind == 0,
        "fixture must start without a pre-resolved hover");

    fixture.send_move_down_up_without_intervening_frame();

    require(overlay.hover_context.kind == 0x0cu,
        "pointer drain must resolve berry hover by the end of the event");
    require(overlay.command_actions.size() == 1,
        "right-button drain must retain one pending world command");
    require(overlay.command_actions.front().item_id == 0xb1u,
        "the same WM_MOUSEMOVE/RBUTTONDOWN drain must use current berry hover");
    require(overlay.command_actions.front().aux == 0x0cu,
        "the queued action must retain raw terrain class 0x0c");

    overlay.selected_production_category = 2;
    input.pointer_aux_state = 2;
    process_default_ui_overlay_command_actions();
    require(overlay.command_actions.empty(),
        "pending action drain must consume the contextual command");
    require(input.last_action_index == 7u,
        "pending action drain must dispatch original action 7");
    require(input.current_snapshot.field2 == 0x0cu &&
            input.current_snapshot.field3 == 0x0cu,
        "contextual dispatch did not restore original hover kind/aux snapshot");
    require(input.dispatch_success_count == 1u,
        "original action-7 dispatcher must accept the berry point");
    require(overlay.selected_production_category == 2 &&
            input.pointer_aux_state == 2,
        "mode-zero contextual dispatch cleared the persistent category state");
    require(!g_packets.empty(),
        "action 7 must publish a lockstep command");
    require(g_packets.back().subtype == 0x02u,
        "action 7 must publish the original subtype-0x02 unit command");
    require(packet_u32(g_packets.back(), 0x10) == 7u,
        "action 7 must retain command 7 on wire");
    require(packet_u32(g_packets.back(), 0x14) == Fixture::kWorkerId,
        "action 7 must retain the selected worker source on wire");
    require(packet_u32(g_packets.back(), 0x18) == 0x0cu,
        "action 7 must publish original raw terrain selector 0x0c");
    const u32 pulse_tile_x = static_cast<u32>(Fixture::kMouseX) >> 5;
    const u32 pulse_tile_y =
        static_cast<u32>(Fixture::kCameraY + Fixture::kMouseY) >> 5;
    const TerrainTilePulseSlot& pulse =
        g_runtime.gameplay_terrain_pulse_state.slots[0];
    require(pulse.timer == 0x0f &&
            pulse.tile_x == static_cast<i32>(pulse_tile_x) &&
            pulse.tile_y == static_cast<i32>(pulse_tile_y),
        "accepted action 7 did not start the original 15-tick terrain pulse");

    UnitMovementMap& pulse_map = g_runtime.gameplay_movement_context.map;
    UnitMovementCell* pulse_cell = GetMovementCell(
        pulse_map, pulse_tile_x, pulse_tile_y);
    require(pulse_cell != nullptr, "pulse tile must remain in the live map");
    GameplayLoopState pulse_loop{};
    const auto tick_pulse = [&]() {
        default_gameplay_loop_simulation_phase<12>(pulse_loop);
    };
    const auto rendered_pulse_enabled = [&]() {
        const std::size_t index = pulse_tile_y * Fixture::kWidth + pulse_tile_x;
        return (g_runtime.gameplay_visibility_grid.terrain_backup[index] &
            0x20000000u) != 0;
    };
    tick_pulse();
    require(pulse.timer == 14 && (pulse_cell->flags & 0x20000000u) != 0 &&
            rendered_pulse_enabled(),
        "pulse tick 14 did not enable the alternate terrain frame");
    for (int tick = 0; tick < 3; ++tick) {
        tick_pulse();
    }
    require(pulse.timer == 11 && (pulse_cell->flags & 0x20000000u) == 0 &&
            !rendered_pulse_enabled(),
        "pulse tick 11 did not disable the alternate terrain frame");
    for (int tick = 0; tick < 4; ++tick) {
        tick_pulse();
    }
    require(pulse.timer == 7 && (pulse_cell->flags & 0x20000000u) != 0 &&
            rendered_pulse_enabled(),
        "pulse tick 7 did not re-enable the alternate terrain frame");
    for (int tick = 0; tick < 4; ++tick) {
        tick_pulse();
    }
    require(pulse.timer == 3 && (pulse_cell->flags & 0x20000000u) == 0 &&
            !rendered_pulse_enabled(),
        "pulse tick 3 did not clear the alternate terrain frame");
    for (int tick = 0; tick < 3; ++tick) {
        tick_pulse();
    }
    require(pulse.timer == 0 && (pulse_cell->flags & 0x20000000u) == 0 &&
            !rendered_pulse_enabled(),
        "completed pulse left the alternate terrain frame enabled");

    ResetTerrainTilePulseState(g_runtime.gameplay_terrain_pulse_state);
    pulse_cell->flags |= kMapCellBlockedTerrain;
    StartTerrainTilePulse(g_runtime.gameplay_terrain_pulse_state, pulse_map,
        static_cast<i32>(pulse_tile_x), static_cast<i32>(pulse_tile_y));
    require(g_runtime.gameplay_terrain_pulse_state.slots[0].tile_x ==
            static_cast<i32>(pulse_tile_x) - 1,
        "blocked terrain pulse did not apply the original x-minus-one alias");

    ResetTerrainTilePulseState(g_runtime.gameplay_terrain_pulse_state);
    pulse_cell->flags &= ~kMapCellBlockedTerrain;
    for (u32 slot = 0; slot < kTerrainTilePulseSlotCount; ++slot) {
        StartTerrainTilePulse(g_runtime.gameplay_terrain_pulse_state, pulse_map,
            static_cast<i32>(pulse_tile_x + slot),
            static_cast<i32>(pulse_tile_y));
    }
    StartTerrainTilePulse(g_runtime.gameplay_terrain_pulse_state, pulse_map,
        static_cast<i32>(pulse_tile_x + kTerrainTilePulseSlotCount),
        static_cast<i32>(pulse_tile_y));
    bool pulse_slots_intact = true;
    for (u32 slot = 0; slot < kTerrainTilePulseSlotCount; ++slot) {
        const TerrainTilePulseSlot& occupied =
            g_runtime.gameplay_terrain_pulse_state.slots[slot];
        pulse_slots_intact = pulse_slots_intact && occupied.timer == 0x0f &&
            occupied.tile_x == static_cast<i32>(pulse_tile_x + slot);
    }
    require(pulse_slots_intact,
        "full four-slot pulse table did not reject the fifth request");

    overlay.placement_mode = 6;
    overlay.context_cursor.animation_mode = 6;
    overlay.staged_unit_action_id = 6;
    overlay.selected_production_category = 2;
    input.pointer_aux_state = 2;
    InputEvent cancel_event{};
    cancel_event.kind = InputEventKind::mouse;
    cancel_event.message = 0x0204;
    cancel_event.x = Fixture::kMouseX;
    cancel_event.y = Fixture::kMouseY;
    default_gameplay_input_handle_pointer_event(input, cancel_event);
    require(overlay.placement_mode == 0 && input.pointer_aux_state == 0,
        "RBUTTON placement cancel did not clear the underlying aux category");

    std::cout << "BERRY_ACTION7_PACKET_MODE=0x" << std::hex
              << packet_u32(g_packets.back(), 0x18) << std::dec << '\n';
    configure_default_mode1_gameplay_runtime_callbacks();
    HandleSubtype02UnitOrderPacket(g_packets.back());
    require((fixture.worker.pending_command.state & kUnitCommandStateMask) == 7u,
        "subtype-02 handler must stage action 7 on the worker");
    UnitCommandContext& commands = prepare_default_mode1_packet_command_context();
    HandlePendingUnitCommandDispatch(commands, fixture.worker);
    require(fixture.worker.command_value == 0x0cu,
        "subtype-02 apply must preserve raw terrain selector 0x0c");
    std::cout << "BERRY_ACTION7_RUNTIME_VALUE=0x" << std::hex
              << fixture.worker.command_value << std::dec << '\n';

    std::cout << "BERRY_POINTER_EVENT_ORDER_PASS\n";
    return 0;
}
