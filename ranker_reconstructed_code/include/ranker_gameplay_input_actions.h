#pragma once

#include "ranker_input.h"
#include "ranker_types.h"

#include <array>
#include <vector>

namespace ranker {

constexpr u32 kGameplayInputSnapshotBytes = 0x14;
constexpr u32 kGameplayInputSnapshotRingBytes = 0x280;
constexpr u32 kGameplayInputSnapshotSlots =
    kGameplayInputSnapshotRingBytes / kGameplayInputSnapshotBytes;
constexpr u32 kGameplayActionSelectorCount = 0x40;
constexpr u32 kGameplayPlayerSlots = 8;
constexpr u32 kGameplayPlayerResetFlagBytes = 0x14;

struct GameplayInputSnapshot {
    u32 field0 = 0;
    u32 field1 = 0;
    u32 field2 = 0;
    u32 field3 = 0;
    u32 field4 = 0;
};

struct GameplayChecksumObject {
    u32 offset = 0;
    u32 identity = 0;
    i32 x = 0;
    i32 y = 0;
    u32 flags = 0;
    u8 owner = 0;
    bool active = true;
};

struct GameplayActionUnitState {
    u32 offset = 0;
    u32 type = 0;
    u32 owner = 0;
    u32 runtime_state = 0;
    u32 command_state = 0;
    u32 flags = 0;
    u32 runtime_flags = 0;
    u32 action_mode_gate = 0;
    i32 x = 0;
    i32 y = 0;
    i32 bounds_left = 0;
    i32 bounds_top = 0;
    i32 bounds_width = 0;
    i32 bounds_height = 0;
    std::vector<u32> command_capabilities;
    std::vector<u32> production_capabilities;
    bool active = true;
    bool visible = true;
    bool selected = false;
};

struct GameplayPublishedAction {
    u32 subtype = 0;
    u32 player = 0;
    u32 packed_opcode = 0;
    u32 unit_offset = 0;
    u32 arg0 = 0;
    u32 arg1 = 0;
    u32 arg2 = 0;
    u32 arg3 = 0;
};

struct GameplayInputActionProductionAvailability {
    bool available = true;
    u32 code = 0;
    u32 primary_cost = 0;
    u32 secondary_cost = 0;
};

struct GameplayInputActionState;

using GameplayInputActionBoolCallback = bool (*)(GameplayInputActionState& state);
using GameplayInputActionSimpleCallback = void (*)(GameplayInputActionState& state);
using GameplayInputActionEventPopCallback =
    bool (*)(GameplayInputActionState& state, InputEvent& out);
using GameplayInputActionEventCallback =
    void (*)(GameplayInputActionState& state, const InputEvent& event);
using GameplayInputActionPublishCallback =
    bool (*)(GameplayInputActionState& state, const GameplayPublishedAction& action);
using GameplayInputActionValidateCallback = bool (*)(
    GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y);
using GameplayInputActionSelectCallback =
    u32 (*)(GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y);
using GameplayInputActionDispatchCallback =
    bool (*)(GameplayInputActionState& state, u32 action_index);
using GameplayInputActionPointCallback =
    void (*)(GameplayInputActionState& state, i32 world_x, i32 world_y);
using GameplayInputActionProductionAvailabilityCallback =
    GameplayInputActionProductionAvailability (*)(
        GameplayInputActionState& state, u32 production, u32 unit_offset);
using GameplayInputActionIndexedPayloadGateCallback =
    bool (*)(GameplayInputActionState& state, u32 unit_offset, u32 payload, u32 index);

struct GameplayInputActionCallbacks {
    GameplayInputActionBoolCallback can_skip_input_drain = nullptr;
    GameplayInputActionBoolCallback has_pending_input_event = nullptr;
    GameplayInputActionEventPopCallback pop_input_event = nullptr;
    GameplayInputActionEventCallback handle_pointer_event = nullptr;
    GameplayInputActionEventCallback handle_keyboard_event = nullptr;
    GameplayInputActionSimpleCallback pre_cursor_update = nullptr;
    GameplayInputActionSimpleCallback post_cursor_update = nullptr;
    GameplayInputActionSimpleCallback set_game_cursor_index = nullptr;
    GameplayInputActionSimpleCallback restore_game_cursor = nullptr;
    GameplayInputActionSimpleCallback finalize_cursor_frame = nullptr;
    GameplayInputActionSimpleCallback reset_snapshot_side_state = nullptr;
    GameplayInputActionPublishCallback publish_action = nullptr;
    GameplayInputActionPublishCallback publish_corrective_action = nullptr;
    GameplayInputActionValidateCallback validate_low_action = nullptr;
    GameplayInputActionValidateCallback validate_high_action = nullptr;
    GameplayInputActionSelectCallback select_action_index = nullptr;
    GameplayInputActionDispatchCallback dispatch_action = nullptr;
    GameplayInputActionPointCallback start_hud_pulse = nullptr;
    GameplayInputActionSimpleCallback stop_hud_pulse = nullptr;
    GameplayInputActionProductionAvailabilityCallback
        check_production_availability = nullptr;
    GameplayInputActionIndexedPayloadGateCallback indexed_payload_blocked = nullptr;
    GameplayInputActionSimpleCallback rejected_action_feedback = nullptr;
};

struct GameplayInputActionState {
    GameplayInputActionCallbacks callbacks;
    std::array<GameplayInputSnapshot, kGameplayInputSnapshotSlots> snapshot_ring{};
    GameplayInputSnapshot live_snapshot{};
    GameplayInputSnapshot current_snapshot{};
    std::array<u8, kGameplayPlayerResetFlagBytes> player_reset_flags{};
    std::array<u32, kGameplayPlayerSlots> unit_identity_checksums{};
    std::array<u32, kGameplayPlayerSlots> unit_position_checksums{};
    std::array<u32, kGameplayPlayerSlots> effect_checksums{};
    std::array<u8, kGameplayActionSelectorCount> selector_redirect_flags{};
    std::array<u8, kGameplayActionSelectorCount> selector_modes{};
    std::array<u8, kGameplayActionSelectorCount> selector_result_states{};
    std::array<u8, kGameplayActionSelectorCount> selector_enabled{};
    std::vector<GameplayChecksumObject> checksum_units;
    std::vector<GameplayChecksumObject> checksum_effects;
    std::vector<GameplayActionUnitState> units;
    std::vector<u32> indexed_payloads;
    std::vector<GameplayPublishedAction> published_actions;
    u32 snapshot_write_offset = 0;
    u32 snapshot_read_offset = 0;
    u32 local_player_index = 0;
    u32 cursor_mode = 0;
    u32 cursor_index = 0;
    u32 current_unit_offset = 0;
    u32 selected_unit_offset = 0;
    u32 multi_select_count = 0;
    u32 map_origin_x = 0;
    u32 map_origin_y = 0;
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
    u32 checksum_tail_value = 0;
    u32 catchup_target_state = 0;
    u32 pending_action_arg0 = 0;
    u32 pending_action_arg1 = 0;
    u32 pending_action_arg2 = 0;
    u32 pending_action_arg3 = 0;
    u32 pending_text_length = 0;
    u32 pointer_aux_state = 0;
    u32 last_selector = 0;
    u32 last_action_index = 0;
    u32 last_action_world_x = 0;
    u32 last_action_world_y = 0;
    u32 last_validation_flags = 0;
    u32 last_validation_unit_offset = 0;
    u32 last_validation_local_unit_offset = 0;
    u32 last_validation_enemy_unit_offset = 0;
    u32 last_validation_special_unit_offset = 0;
    u32 last_production_availability_code = 0;
    u32 dispatch_success_count = 0;
    u32 validation_fail_count = 0;
    u32 rejected_feedback_count = 0;
    u32 production_feedback_count = 0;
    bool snapshot_side_flag = false;
    bool keyboard_filter_active = false;
    bool modal_route_blocked = false;
    bool player_reset_gate = false;
    bool last_dispatch_failed = false;
};

GameplayInputActionState& gameplay_input_action_state();

bool DefaultHasPendingGameplayInputEvent(GameplayInputActionState& state);
bool DefaultPopGameplayInputEvent(
    GameplayInputActionState& state, InputEvent& out);
bool DefaultValidateLowGameplayInputAction(
    GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y);
bool DefaultValidateHighGameplayInputAction(
    GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y);
u32 DefaultSelectGameplayInputActionIndex(
    GameplayInputActionState& state, u32 selector, i32 world_x, i32 world_y);
void InitializeOriginalGameplayInputActionTables(GameplayInputActionState& state);

void PumpGameplayInputAndCursorFrame(GameplayInputActionState& state);
void DrainGameplayInputEvents(GameplayInputActionState& state);
void ResetGameplayInputSnapshotRing(GameplayInputActionState& state);
bool PopGameplayInputSnapshot(GameplayInputActionState& state);
bool PushGameplayInputSnapshot(GameplayInputActionState& state);
void ResetGameplayInputPointerState(GameplayInputActionState& state);

void SnapshotLocalGameplayChecksum(GameplayInputActionState& state);
bool PublishMode1RelationMaskAction(GameplayInputActionState& state);
void PublishMode1CorrectiveChecksum(GameplayInputActionState& state);
bool PublishMode1ModalPauseAction(GameplayInputActionState& state);
bool PublishGameplayCatchupTargetState(GameplayInputActionState& state);
bool ResetAndPublishPlayerInactiveState(GameplayInputActionState& state);
bool PublishSelectedUnitsPendingAction(GameplayInputActionState& state, u32 text_length);
bool PublishMode1NoOp11Action(GameplayInputActionState& state);
bool PublishSelectedUnitCapabilityAction(GameplayInputActionState& state, u32 capability);
bool PublishSelectedUnitIndexedPayloadAction(GameplayInputActionState& state, u32 index);
bool PublishSelectedUnitPrimaryAction(GameplayInputActionState& state);
bool PublishSelectedUnitPrimaryAction(GameplayInputActionState& state,
    u32 command, u32 logical_index);
bool PublishSelectedUnitProductionAction(GameplayInputActionState& state, u32 production);
bool PublishNestedSelectedCommandAction(GameplayInputActionState& state);
bool PublishSelectedUnitPlacementAction(GameplayInputActionState& state);
bool PublishSelectedUnitPlacementAction(GameplayInputActionState& state,
    u32 command, u32 logical_index);
u32 DispatchSelectedUnitActionCommand(GameplayInputActionState& state, u32 selector,
    i32 screen_x, i32 screen_y, u32 unit_offset);

}
