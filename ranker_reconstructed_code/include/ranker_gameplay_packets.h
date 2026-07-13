#pragma once

#include "ranker_reliable_packets.h"

#include <array>
#include <string>
#include <unordered_map>

namespace ranker {

constexpr u32 kMode1GameplayPacketSubtypeCount = 0x40;

struct PlayerSlotRuntimeState;

using Mode1GameplayPacketHandler = void (*)(const Mode1ReliablePacket& packet,
    void* user_data);
using Mode1GameplayStringInternCallback = u32 (*)(void* user_data,
    u32 unit_offset, const char* text);
using Mode1GameplayStringClearCallback = void (*)(void* user_data,
    u32 unit_offset, u32 previous_slot);
using Mode1GameplayUnitDeathMarkCallback = void (*)(void* user_data,
    u32 unit_offset);
using Mode1GameplayUnitCommandBitCallback = void (*)(void* user_data,
    u32 unit_offset, u32 bit, bool enabled);
using Mode1GameplayUnitCommandPayloadCallback = void (*)(void* user_data,
    u32 unit_offset, u32 state, i32 command_value_or_target, i32 x_payload,
    u32 y_payload,
    bool enqueue_deferred, bool clear_deferred);
using Mode1GameplayUnitDeferredResourceCommandCallback = bool (*)(
    void* user_data, u32 unit_offset, u32 category_flag, u32 internal_command,
    u32 payload, i32 mode, u32 arg1, u32 arg2, bool enqueue,
    u32 logical_index, u8 source_channel);
using Mode1GameplayUnitCommandFlagCallback = bool (*)(void* user_data,
    u32 unit_offset, u32 flag, u32 command, i32 mode, bool enabled);
using Mode1GameplayUnitEquipmentApplyCallback = void (*)(void* user_data,
    u32 unit_offset, u32 effect_id);
using Mode1GameplayUnitEquipmentToggleCallback = u32 (*)(void* user_data,
    u32 unit_offset, u32 original_slot_code);
using Mode1GameplayUnitStatusMaskCallback = void (*)(void* user_data,
    u32 unit_offset, u32 status_mask);
using Mode1GameplayUnitAuxVectorCallback = void (*)(void* user_data,
    u32 unit_offset, u32 linked_unit_offset, i32 x, i32 y);
using Mode1GameplayForcedOrder21Callback = void (*)(void* user_data,
    u32 unit_offset);
using Mode1GameplayUnitBonusCallback = void (*)(void* user_data,
    u32 unit_offset);
using Mode1GameplayGlobalResetCallback = void (*)(void* user_data);
using Mode1GameplayCatchupTargetCallback = void (*)(void* user_data,
    u32 raw_value, bool enabled);
using Mode1GameplayModalPauseCallback = void (*)(void* user_data, bool visible);
using Mode1GameplayPopulationLimitCallback = void (*)(void* user_data,
    u32 owner, u32 limit);
using Mode1GameplayHighClusterTransitionCallback = void (*)(void* user_data,
    i32 transition_index, bool write_transition_index,
    bool local_scene_change);
using Mode1GameplayPlayerInactiveNotificationCallback = void (*)(void* user_data,
    u32 target_slot, u32 source_slot, u32 lobby_code);
using Mode1GameplayFogPromoteCallback = void (*)(void* user_data,
    bool require_current_visible);

struct Mode1GameplayRuntimeCallbacks {
    Mode1GameplayStringInternCallback intern_unit_string = nullptr;
    Mode1GameplayStringClearCallback clear_unit_string = nullptr;
    Mode1GameplayUnitDeathMarkCallback mark_unit_death = nullptr;
    Mode1GameplayUnitCommandBitCallback set_unit_command_bit = nullptr;
    Mode1GameplayUnitCommandPayloadCallback set_unit_command_payload = nullptr;
    Mode1GameplayUnitDeferredResourceCommandCallback
        set_unit_deferred_resource_command = nullptr;
    Mode1GameplayUnitCommandFlagCallback set_unit_command_flag = nullptr;
    Mode1GameplayUnitEquipmentApplyCallback apply_unit_equipment_effect = nullptr;
    Mode1GameplayUnitEquipmentToggleCallback toggle_unit_equipment_slot = nullptr;
    Mode1GameplayUnitStatusMaskCallback set_unit_status_mask = nullptr;
    Mode1GameplayUnitAuxVectorCallback set_unit_aux_vector = nullptr;
    Mode1GameplayForcedOrder21Callback force_order21 = nullptr;
    Mode1GameplayUnitBonusCallback apply_unit_bonus = nullptr;
    Mode1GameplayGlobalResetCallback apply_global_reset = nullptr;
    Mode1GameplayCatchupTargetCallback apply_catchup_target = nullptr;
    Mode1GameplayModalPauseCallback set_modal_pause = nullptr;
    Mode1GameplayPopulationLimitCallback set_owner_population_limit = nullptr;
    Mode1GameplayHighClusterTransitionCallback apply_high_cluster_transition =
        nullptr;
    Mode1GameplayPlayerInactiveNotificationCallback
        queue_player_inactive_notification = nullptr;
    Mode1GameplayFogPromoteCallback promote_fog_visible_tiles = nullptr;
};

struct Mode1GameplayCommandRecord {
    u32 command = 0xffffffffu;
    u32 arg0 = 0;
    u32 arg1 = 0;
    u32 arg2 = 0;
};

struct Mode1GameplayUnitPacketState {
    std::array<Mode1GameplayCommandRecord, 10> queued_commands{};
    std::array<u8, 32> command_bits{};
    Mode1GameplayCommandRecord current_command{};
    u32 unit_offset = 0xffffffffu;
    u32 owner = 0;
    u32 runtime_state = 0;
    u32 order_flags = 0;
    u32 state_flags = 0;
    u32 status_mask = 0;
    u32 queued_count = 0;
    u32 command_apply_count = 0;
    u32 queue_overflow_count = 0;
    u32 command_reject_count = 0;
    u32 cost_debit_count = 0;
    u32 cost_refund_count = 0;
    u32 resource_primary = 0;
    u32 resource_secondary = 0;
    u32 aux0 = 0;
    u32 aux1 = 0;
    u32 aux2 = 0;
    u32 pending_action_value = 0;
    std::array<u32, 4> pending_action_payload{};
    u32 pending_string_slot = 0;
    std::array<char, 20> pending_action_text{};
    bool touched = false;
    bool death_marked = false;
    bool action_pending = false;
};

struct Mode1GameplayPlayerPacketState {
    u32 relation_mask = 0;
    u32 relation_block_mask = 0;
    u32 vote_mask = 0;
    u8 status = 0;
    u8 lobby_code = 0;
    u8 ready_flag = 0;
    u8 consensus_mask = 0;
    bool inactive = false;
    bool relation_dirty = false;
};

struct Mode1GameplayPacketDispatchState {
    std::array<u32, kMode1GameplayPacketSubtypeCount> original_handler_addresses{};
    std::array<u32, 0x1e> original_low_subtype_targets{};
    std::array<u32, 0x17> original_nested_subtype13_targets{};
    std::array<Mode1GameplayPacketHandler, kMode1GameplayPacketSubtypeCount> handlers{};
    std::array<u32, kMode1GameplayPacketSubtypeCount> dispatch_counts{};
    std::array<u32, 8> player_wait_budget{};
    std::array<u32, 8> owner_population_limits{};
    // Original DAT_011b5a2c: each player begins with four network modal
    // pauses; the first subtype-0x16 show transition from that source spends
    // one use.
    std::array<u8, 8> player_modal_pause_uses_remaining{};
    std::array<u8, 8> vote_completion_seen{};
    std::array<Mode1GameplayPlayerPacketState, 8> players{};
    std::unordered_map<u32, Mode1GameplayUnitPacketState> units_by_offset{};
    u32 total_dispatches = 0;
    u32 global_reset_packets = 0;
    u32 unit_bonus_packets = 0;
    u32 no_op_packets = 0;
    u32 nested_subtype13_dispatches = 0;
    u32 command_packets = 0;
    u32 queued_command_packets = 0;
    u32 cancelled_command_packets = 0;
    u32 player_inactive_packets = 0;
    u32 player_inactive_notifications = 0;
    u32 fog_promote_requests = 0;
    u32 player_relation_packets = 0;
    u32 active_player_count = 0;
    u32 production_packets = 0;
    u32 resource_packets = 0;
    u32 string_slot_clear_packets = 0;
    u32 string_slot_intern_packets = 0;
    u32 next_string_slot = 1;
    u32 catchup_target_value = 0;
    u32 published_local_packets = 0;
    u32 broadcast_packets = 0;
    u32 consensus_epoch = 0;
    u32 consensus_decision_mask = 0;
    u32 last_consensus_target = 0xffffffffu;
    u32 last_consensus_source = 0xffffffffu;
    u32 last_unit_pointer = 0;
    u32 corrective_mismatch_packets = 0;
    u32 corrective_checksum_broadcasts = 0;
    u32 last_corrective_channel = 0;
    u32 last_corrective_sequence = 0;
    u32 last_corrective_local_value = 0;
    u32 last_corrective_remote_value = 0;
    u32 last_corrective_frame = 0;
    u32 last_inactive_notification_target = 0xffffffffu;
    u32 last_inactive_notification_source = 0xffffffffu;
    u32 last_inactive_notification_code = 0;
    u32 local_player_index = 0;
    u8 last_subtype = 0;
    i32 transition_index = -2;
    u32 transition_timer = 0;
    bool transition_requested = false;
    bool local_scene_change_requested = false;
    bool local_toggle_state = false;
    bool last_fog_promote_requires_current_visible = false;
    bool catchup_target_enabled = false;
    bool modal_pause_visible = false;
    bool vote_completion_gate_open = false;
    bool session_complete_requested = false;
    bool auto_broadcast_published_packets = false;
    bool vote_collection_active = false;
    bool all_votes_received = false;
    bool player_inactive_notification_pending = false;
    bool missing_handler_seen = false;
    bool corrective_mismatch_detected = false;
    bool generic_ai_profile_mode = false;
    Mode1ReliablePacket last_corrective_packet{};
    Mode1GameplayRuntimeCallbacks runtime_callbacks{};
    void* user_data = nullptr;
    void* runtime_user_data = nullptr;
};

Mode1GameplayPacketDispatchState& mode1_gameplay_packet_dispatch_state();

void ResetMode1GameplayPacketDispatch();
void SetMode1GameplayPacketHandler(u8 subtype, Mode1GameplayPacketHandler handler);
void SetMode1GameplayPacketDispatchUserData(void* user_data);
void SetMode1GameplayRuntimeCallbacks(
    const Mode1GameplayRuntimeCallbacks& callbacks,
    void* user_data = nullptr);
void SetMode1GameplayActivePlayerCount(u32 active_player_count);
void SetMode1GameplayGenericAiProfileMode(bool enabled);
void ResetMode1GameplayVoteCompletionGate();
void ApplySubtype15ConsensusDecision(u32 source_slot, u32 target_slot);
void BroadcastMode1GameplayPlayerInactive(PlayerSlotRuntimeState& state,
    u32 target_slot, u32 source_slot);
void MarkPlayerInactiveAndBroadcastSubtype15(PlayerSlotRuntimeState& state,
    u32 target_slot, u32 source_slot);
void ShowGameplayModalPauseOverlay();
void HideGameplayModalPauseOverlay();
bool DispatchMode1GameplayPacket(const Mode1ReliablePacket& packet);
void HandleSubtype01ProductionCommandPacket(const Mode1ReliablePacket& packet);
void HandleSubtype02UnitOrderPacket(const Mode1ReliablePacket& packet);
void ApplySubtype03UnitCommandPacket(const Mode1ReliablePacket& packet);
void ApplySubtype04UnitCommandFallbackPacket(const Mode1ReliablePacket& packet);
void HandleSubtype05ResourceBuildPacket(const Mode1ReliablePacket& packet);
void HandleSubtype06UnitCommandBitPacket(const Mode1ReliablePacket& packet);
void HandleSubtype07UnitDeathMarkPacket(const Mode1ReliablePacket& packet);
void HandleSubtype08UnitAuxVectorPacket(const Mode1ReliablePacket& packet);
void HandleSubtype09ExtendedUnitOrderPacket(const Mode1ReliablePacket& packet);
void HandleSubtype0aForcedOrder21Packet(const Mode1ReliablePacket& packet);
void HandleSubtype0bUnitStatusMaskPacket(const Mode1ReliablePacket& packet);
void HandleSubtype0cPlacementResourcePacket(const Mode1ReliablePacket& packet);
void DispatchSubtype0dNestedGameplayCommand(const Mode1ReliablePacket& packet);
void HandleSubtype13PlayerInactivePacket(const Mode1ReliablePacket& packet);
void HandleSubtype14PlayerRelationPacket(const Mode1ReliablePacket& packet);
void HandleSubtype15PlayerConsensusPacket(const Mode1ReliablePacket& packet);
void HandleSubtype16ModalPausePacket(const Mode1ReliablePacket& packet);
void HandleSubtype1aProductionCostPacket(const Mode1ReliablePacket& packet);
void HandleSubtype19PendingUnitActionPacket(const Mode1ReliablePacket& packet);
bool QueueCurrentPacketDeferredUnitCommand(const Mode1ReliablePacket& packet,
    u32 command);
bool QueueDeferredUnitCommandRecord(u32 unit_offset, u8 owner,
    const Mode1GameplayCommandRecord& record);
void HandleSubtype1dVoteCompletionPacket(const Mode1ReliablePacket& packet);
void HandleSubtype0fCatchupTargetPacket(const Mode1ReliablePacket& packet);
Mode1ReliablePacket BuildMode1GameplayPacket(u32 packed_opcode, u32 arg0 = 0,
    u32 unit_offset = 0, u32 arg1 = 0, u32 arg2 = 0, u32 arg3 = 0);
bool PublishLocalMode1GameplayPacket(u32 packed_opcode, u32 arg0 = 0,
    u32 unit_offset = 0, u32 arg1 = 0, u32 arg2 = 0, u32 arg3 = 0);
bool PublishLocalMode1GameplayPacketPreserveResult(u32 packed_opcode, u32 arg0 = 0,
    u32 unit_offset = 0, u32 arg1 = 0, u32 arg2 = 0, u32 arg3 = 0);
bool BroadcastMode1GameplayPacket(u32 packed_opcode, u32 arg0 = 0,
    u32 unit_offset = 0, u32 arg1 = 0, u32 arg2 = 0, u32 arg3 = 0);

}
