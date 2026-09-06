#include "ranker_ai_actions.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace ranker;
void require(bool result, const char* message) {
    if (!result) { std::cerr << "commander actions: " << message << '\n'; std::exit(1); }
}
bool visible(const UnitMovementUnit& unit, u32, void* context) {
    return unit.id == *static_cast<u32*>(context);
}
UnitMovementUnit unit(u32 id, u32 owner, u32 type, u32 flags) {
    UnitMovementUnit result;
    result.id = id;
    result.active = true;
    result.owner_id = owner;
    result.type_id = type;
    result.type_flags = flags;
    result.health = 100;
    result.x = 96;
    result.y = 160;
    return result;
}
void packet(const GameplayPublishedAction& actual, u32 subtype, u32 source,
    u32 command, u32 mode, u32 x, u32 y) {
    require(actual.packed_opcode == (subtype << 24) && actual.player == 0 &&
        actual.subtype == subtype && actual.unit_offset == source &&
        actual.arg0 == command && actual.arg1 == mode && actual.arg2 == x && actual.arg3 == y,
        "wrong command packet tuple");
}
}

int main() {
    using namespace ranker;
    auto hunter = unit(11, 0, 0x22, (1u << 5) | (1u << 13));
    auto other = unit(12, 0, 0x24, 1u << 5);
    auto building = unit(13, 0, 0x84, 0);
    building.under_construction = true;
    building.action_mode_gate = 1;
    auto animal = unit(21, 8, 0x41, 0);
    animal.x = 288;
    animal.y = 416;
    auto enemy = unit(22, 1, 0x21, 1u << 5);
    PlayerSlotRuntimeState players;
    players.owner_relation_masks[0] = 1u;
    UnitMovementContext movement;
    movement.map.width = 32;
    movement.map.height = 32;
    movement.active_units = {&hunter, &other, &building, &animal, &enemy};
    u32 visible_id = animal.id;
    AiActionPlanInput input;
    input.local_owner = 0;
    input.players = &players;
    input.movement = &movement;
    input.unit_visible = visible;
    input.unit_visibility_user_data = &visible_id;

    AiSemanticAction cancel;
    cancel.kind = AiSemanticActionKind::cancel_construction;
    cancel.unit_ids = {building.id};
    auto result = PlanAiSemanticActionV1(input, cancel);
    require(bool(result) && result.packets.size() == 1, "unfinished building cancellation rejected");
    packet(result.packets[0], 7, building.id, 0x1c, 0, 96, 160);
    require(building.active && building.health == 100, "planner changed live construction state");
    building.under_construction = false;
    require(PlanAiSemanticActionV1(input, cancel).code == AiActionPlanCode::nothing_to_cancel,
        "completed building accepted by destructive subtype07");
    building.under_construction = true;
    building.action_mode_gate = 0;
    require(PlanAiSemanticActionV1(input, cancel).code == AiActionPlanCode::nothing_to_cancel,
        "inconsistent construction mirrors accepted");
    building.action_mode_gate = 1;
    cancel.queued = true;
    require(PlanAiSemanticActionV1(input, cancel).code == AiActionPlanCode::queued_flag_unsupported,
        "construction cancellation queued flag accepted");
    cancel.queued = false;
    cancel.target_unit_id = animal.id;
    require(PlanAiSemanticActionV1(input, cancel).code == AiActionPlanCode::unexpected_target,
        "construction cancellation accepted external target");
    cancel.target_unit_id = 0;
    building.owner_id = 1;
    require(PlanAiSemanticActionV1(input, cancel).code == AiActionPlanCode::unit_not_owned,
        "opponent construction cancellation accepted");
    building.owner_id = 0;
    cancel.unit_ids = {hunter.id};
    hunter.under_construction = true;
    hunter.action_mode_gate = 1;
    require(PlanAiSemanticActionV1(input, cancel).code == AiActionPlanCode::nothing_to_cancel,
        "mobile unit accepted by construction cancellation");
    hunter.under_construction = false;
    hunter.action_mode_gate = 0;
    cancel.unit_ids = {hunter.id, building.id};
    require(PlanAiSemanticActionV1(input, cancel).code == AiActionPlanCode::requires_single_unit,
        "construction cancellation accepted group");

    AiSemanticAction marker;
    marker.kind = AiSemanticActionKind::set_hunt_marker;
    marker.unit_ids = {hunter.id};
    hunter.area_marker_flags = 0x17;
    hunter.command_flags = 0x80000000;
    result = PlanAiSemanticActionV1(input, marker);
    require(bool(result) && result.packets.size() == 1, "marker enable rejected");
    packet(result.packets[0], 11, hunter.id, 0x0d, 0x80000000, 96, 160);
    require(hunter.area_marker_flags == 0x17, "marker planner wrote engine state directly");
    hunter.area_marker_flags |= 0x80000000;
    result = PlanAiSemanticActionV1(input, marker);
    require(bool(result) && result.packets.empty(), "unchanged marker emitted duplicate packet");
    marker.stance_on = false;
    result = PlanAiSemanticActionV1(input, marker);
    require(bool(result) && result.packets.size() == 1, "marker disable rejected");
    packet(result.packets[0], 11, hunter.id, 0x0d, 0, 96, 160);
    marker.queued = true;
    require(PlanAiSemanticActionV1(input, marker).code == AiActionPlanCode::queued_flag_unsupported,
        "queued marker accepted");
    marker.queued = false;
    marker.stance_on = true;
    marker.unit_ids = {other.id};
    result = PlanAiSemanticActionV1(input, marker);
    require(bool(result) && result.packets.size() == 1,
        "marker incorrectly requires capability13 instead of human capability5");
    other.type_flags = 1u << 13;
    require(PlanAiSemanticActionV1(input, marker).code == AiActionPlanCode::unit_action_unsupported,
        "marker accepted noncombat unit");

    AiSemanticAction hunt;
    hunt.kind = AiSemanticActionKind::hunt_unit;
    hunt.unit_ids = {hunter.id};
    hunt.target_unit_id = animal.id;
    result = PlanAiSemanticActionV1(input, hunt);
    require(bool(result) && result.packets.size() == 1, "visible neutral hunt rejected");
    packet(result.packets[0], 2, hunter.id, 0x0d, animal.id, 288, 416);
    hunt.queued = true;
    result = PlanAiSemanticActionV1(input, hunt);
    require(bool(result), "queued visible hunt rejected");
    packet(result.packets[0], 2, hunter.id, 0x8000000d, animal.id, 288, 416);
    hunt.queued = false;
    visible_id = 0;
    result = PlanAiSemanticActionV1(input, hunt);
    require(result.code == AiActionPlanCode::target_not_visible && result.packets.empty(),
        "hunt exposed hidden neutral target id/position");
    input.unit_visible = nullptr;
    require(PlanAiSemanticActionV1(input, hunt).code == AiActionPlanCode::target_not_visible,
        "hunt succeeded without visibility validator");
    input.unit_visible = visible;
    visible_id = enemy.id;
    hunt.target_unit_id = enemy.id;
    require(PlanAiSemanticActionV1(input, hunt).code == AiActionPlanCode::target_not_neutral,
        "ordinary enemy accepted as neutral hunt");
    hunt.target_unit_id = hunter.id;
    require(PlanAiSemanticActionV1(input, hunt).code == AiActionPlanCode::target_is_friendly,
        "friendly target accepted as neutral hunt");
    hunt.target_unit_id = 0;
    require(PlanAiSemanticActionV1(input, hunt).code == AiActionPlanCode::missing_target,
        "point-only hunt accepted");
    hunt.target_unit_id = animal.id;
    visible_id = animal.id;
    animal.active = false;
    require(PlanAiSemanticActionV1(input, hunt).code == AiActionPlanCode::target_inactive,
        "dead neutral accepted as hunt");
    animal.active = true;
    hunt.unit_ids = {hunter.id, other.id};
    result = PlanAiSemanticActionV1(input, hunt);
    require(result.code == AiActionPlanCode::unit_action_unsupported && result.packets.empty(),
        "mixed unsupported hunt group published partial commands");
    hunter.type_flags = 1u << 5;
    hunt.unit_ids = {hunter.id};
    require(PlanAiSemanticActionV1(input, hunt).code == AiActionPlanCode::unit_action_unsupported,
        "unsupported acquire command fell back to ordinary move");
    std::cout << "commander semantic action wire/fairness regression passed\n";
}
