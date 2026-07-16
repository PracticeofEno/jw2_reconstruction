#include "ranker_production_orders.h"
#include "ranker_unit_commands.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

constexpr i32 kStartX = 650;
constexpr i32 kStartY = 650;
constexpr i32 kBaseDeltaX = 2;
constexpr i32 kBaseDeltaY = -3;
constexpr i32 kAdditionalModifier = 4;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "MOVEMENT_MODIFIER_COMMAND_FLAG_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool allow_enter(UnitMovementContext&, const UnitMovementUnit&, i32, i32) {
    return true;
}

UnitMovementUnit make_unit(u32 owner_id, u32 command_flags, u32 runtime_flags) {
    UnitMovementUnit unit{};
    unit.owner_id = owner_id;
    unit.type_id = 0;
    unit.command_flags = command_flags;
    unit.runtime_flags = runtime_flags;
    unit.command_state = kUnitStateTransportDockSearch;
    unit.x = kStartX;
    unit.y = kStartY;
    unit.next_path_x = 1000;
    unit.next_path_y = 0;
    unit.path_target_x = unit.next_path_x;
    unit.path_target_y = unit.next_path_y;
    unit.direction = 1;
    unit.animation_frame = 0;
    unit.definition.movement_class = 2;
    unit.definition.animation_frame_count = 1;
    unit.definition.animation_timer_period = 1;
    unit.definition.bounds_width = 1;
    unit.definition.bounds_height = 1;
    unit.definition.frame_delta_by_direction[1][0] =
        UnitMovementPoint{kBaseDeltaX, kBaseDeltaY};
    return unit;
}

UnitMovementContext make_movement_context(
    const ProductionOrderRuntimeState& production_state) {
    UnitMovementContext context{};
    context.map.width = 64;
    context.map.height = 64;
    context.map.stride_tiles = 64;
    context.callbacks.can_enter_cell = allow_enter;
    context.production_state = &production_state;
    context.additional_movement_modifier = kAdditionalModifier;
    return context;
}

void require_position(const UnitMovementUnit& unit, i32 expected_x, i32 expected_y,
    const char* message) {
    require(unit.x == expected_x && unit.y == expected_y, message);
}

void check_common_movement(UnitMovementContext& movement, u32 owner_id,
    u32 command_flags, u32 runtime_flags, i32 expected_x, i32 expected_y,
    const char* message) {
    UnitMovementUnit unit = make_unit(owner_id, command_flags, runtime_flags);
    require(ProcessUnitMovementStep(movement, unit), message);
    require_position(unit, expected_x, expected_y, message);
}

void check_transport_dock(UnitMovementContext& movement,
    const ProductionOrderRuntimeState& production_state, u32 owner_id,
    u32 command_flags, u32 runtime_flags, i32 expected_x, i32 expected_y,
    const char* message) {
    UnitCommandContext commands{};
    commands.movement = &movement;
    commands.production_state = &production_state;

    UnitMovementUnit unit = make_unit(owner_id, command_flags, runtime_flags);
    ProcessTransportDockSearch(commands, unit);
    require_position(unit, expected_x, expected_y, message);
}

void check_draw_feedback_is_independent_from_raw_command_bits() {
    UnitCommandContext context{};
    context.frame_counter = 0x10;

    UnitMovementUnit feedback{};
    feedback.draw_flags = 0x85u;
    feedback.command_flags = 0x40u;
    HandleUnitPassiveRecoveryAndTimedRemoval(context, feedback);
    require(feedback.draw_flags == 0x85u &&
            (feedback.command_flags & 0x40u) != 0,
        "raw +0xa4 red feedback was consumed as raw +0x5c command bit 0x80");

    UnitMovementUnit timed{};
    timed.draw_flags = 0x85u;
    timed.command_bits[0] = 0x80u;
    timed.command_flags = 0x40u;
    HandleUnitPassiveRecoveryAndTimedRemoval(context, timed);
    require(timed.draw_flags == 0x85u &&
            (timed.command_bits[0] & 0x80u) == 0 &&
            (timed.command_flags & 0x40u) == 0,
        "raw +0x5c timed bit did not clear independently from draw feedback");
}

} // namespace

int main() {
    using namespace ranker;

    const ProductionOrderRuntimeState production_state{};
    UnitMovementContext movement = make_movement_context(production_state);

    constexpr i32 boosted_x = kStartX + kBaseDeltaX + kAdditionalModifier;
    constexpr i32 boosted_y = kStartY + kBaseDeltaY - kAdditionalModifier;
    constexpr i32 base_x = kStartX + kBaseDeltaX;
    constexpr i32 base_y = kStartY + kBaseDeltaY;

    check_common_movement(movement, 0, 0x10000u, 0, boosted_x, boosted_y,
        "owner<8 common movement must use command flag 0x10000");
    check_common_movement(movement, 0, 0, 0x10000u, base_x, base_y,
        "owner<8 common movement must ignore runtime flag 0x10000");
    check_transport_dock(movement, production_state, 0, 0x10000u, 0,
        boosted_x, boosted_y,
        "owner<8 transport dock must use command flag 0x10000");
    check_transport_dock(movement, production_state, 0, 0, 0x10000u,
        base_x, base_y,
        "owner<8 transport dock must ignore runtime flag 0x10000");

    check_common_movement(movement, 8, 0x10000u, 0, base_x, base_y,
        "owner>=8 common movement must bypass the modifier");
    check_common_movement(movement, 8, 0, 0x10000u, base_x, base_y,
        "owner>=8 common movement must ignore the runtime flag");
    check_transport_dock(movement, production_state, 8, 0x10000u, 0,
        base_x, base_y,
        "owner>=8 transport dock must bypass the modifier");
    check_transport_dock(movement, production_state, 8, 0, 0x10000u,
        base_x, base_y,
        "owner>=8 transport dock must ignore the runtime flag");
    check_draw_feedback_is_independent_from_raw_command_bits();

    std::cout << "MOVEMENT_MODIFIER_COMMAND_FLAG_PASS "
                 "owner0-command={6,-7} owner0-runtime={2,-3} "
                 "owner8-command/runtime={2,-3} paths=common+dock "
                 "draw-feedback=raw-a4 command-bit=raw-5c\n";
    return EXIT_SUCCESS;
}
