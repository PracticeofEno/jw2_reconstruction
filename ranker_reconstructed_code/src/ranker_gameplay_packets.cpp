#include "ranker_gameplay_packets.h"
#include "ranker_gameplay_cheats.h"
#include "ranker_gameplay_production_actions.h"
#include "ranker_player_slots.h"

#ifdef _WIN32
#include "ranker_directplay.h"
#endif

#include <algorithm>
#include <cstring>

namespace ranker {
namespace {

constexpr std::array<u32, kMode1GameplayPacketSubtypeCount> kOriginalMode1PacketHandlers = {
    0x004dca04, 0x00403486, 0x00401e3d, 0x004011d6,
    0x00402360, 0x00402d24, 0x004036fc, 0x00402072,
    0x0040188e, 0x00401f14, 0x004020cc, 0x004032ba,
    0x00401014, 0x00401f2d, 0x004dca04, 0x00402d51,
    0x0040319d, 0x004023bf, 0x004dca04, 0x00402874,
    0x00401e1f, 0x00403481, 0x00401edd, 0x004dca04,
    0x004dca04, 0x004019a1, 0x00402e8c, 0x004dca04,
    0x004dca04, 0x00402770, 0x00000000, 0x004dd13d,
    0x004dd15f, 0x004dd17c, 0x004dd198, 0x004dd1b4,
    0x004dd1d0, 0x004dd1ec, 0x004dd208, 0x004dd224,
    0x004dd240, 0x004dd25c, 0x004dd278, 0x004dd294,
    0x004dd2b0, 0x004dd2cc, 0x004dd2e8, 0x004dd2f8,
    0x004dd339, 0x004dd3a6, 0x004dd3b6, 0x004dd3b6,
    0x004dd3b6, 0x004dd3b6, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

constexpr std::array<u32, 0x1e> kOriginalLowSubtypeTargets = {
    0x004dca04, 0x004dca58, 0x004dd55f, 0x004dd9c0,
    0x004dd9f3, 0x004dccee, 0x004dda9a, 0x004ddab6,
    0x004ddad3, 0x004dd75e, 0x004dd827, 0x004dd83d,
    0x004dcf81, 0x004dd133, 0x004dca04, 0x004dde72,
    0x004296f0, 0x004ddaf2, 0x004dca04, 0x004ddaf3,
    0x004ddc2e, 0x004ddc5c, 0x004dde5d, 0x004dca04,
    0x004dca04, 0x004dca05, 0x004dd3b7, 0x004dca04,
    0x004dca04, 0x004ddbe1,
};

constexpr std::array<u32, 0x17> kOriginalSubtype13NestedTargets = {
    0x004dd13d, 0x004dd15f, 0x004dd17c, 0x004dd198,
    0x004dd1b4, 0x004dd1d0, 0x004dd1ec, 0x004dd208,
    0x004dd224, 0x004dd240, 0x004dd25c, 0x004dd278,
    0x004dd294, 0x004dd2b0, 0x004dd2cc, 0x004dd2e8,
    0x004dd2f8, 0x004dd339, 0x004dd3a6, 0x004dd3b6,
    0x004dd3b6, 0x004dd3b6, 0x004dd3b6,
};

Mode1GameplayPacketDispatchState g_packet_dispatch_state;

struct PacketFields {
    u8 channel = 0;
    u8 subtype = 0;
    u32 command = 0;
    u32 unit_offset = 0;
    i32 mode = 0;
    u32 arg1 = 0;
    u32 arg2 = 0;
};

void handle_placement_resource_packet(const Mode1ReliablePacket& packet, void*);
void handle_production_cost_packet(const Mode1ReliablePacket& packet, void*);

u32 read_packet_u32(const Mode1ReliablePacket& packet, u32 offset) {
    if (offset > packet.bytes.size() || packet.bytes.size() - offset < sizeof(u32)) {
        return 0;
    }

    u32 value = 0;
    std::memcpy(&value, packet.bytes.data() + offset, sizeof(value));
    return value;
}

i32 read_packet_i32(const Mode1ReliablePacket& packet, u32 offset) {
    return WrappedU32ToI32(read_packet_u32(packet, offset));
}

u8 packet_channel(const Mode1ReliablePacket& packet) {
    return packet.size > 0x0c ? packet.bytes[0x0c] : packet.channel;
}

void write_packet_u32(Mode1ReliablePacket& packet, u32 offset, u32 value) {
    if (offset > packet.bytes.size() || packet.bytes.size() - offset < sizeof(value)) {
        return;
    }
    std::memcpy(packet.bytes.data() + offset, &value, sizeof(value));
}

bool packet_channel_is_local(const Mode1ReliablePacket& packet) {
    return packet_channel(packet) == mode1_reliable_state().local_player_index;
}

PacketFields packet_fields(const Mode1ReliablePacket& packet) {
    PacketFields fields{};
    fields.channel = packet_channel(packet);
    fields.subtype = packet.size > 0x0f ? packet.bytes[0x0f] : packet.subtype;
    fields.command = read_packet_u32(packet, 0x10);
    fields.unit_offset = read_packet_u32(packet, 0x14);
    fields.mode = read_packet_i32(packet, 0x18);
    fields.arg1 = read_packet_u32(packet, 0x1c);
    fields.arg2 = read_packet_u32(packet, 0x20);
    return fields;
}

Mode1GameplayUnitPacketState& unit_state_for(u32 unit_offset, u8 owner) {
    auto& unit = g_packet_dispatch_state.units_by_offset[unit_offset];
    unit.unit_offset = unit_offset;
    unit.owner = owner;
    unit.touched = true;
    return unit;
}

Mode1GameplayUnitPacketState& unit_state_for_packet(const Mode1ReliablePacket& packet) {
    const PacketFields fields = packet_fields(packet);
    return unit_state_for(fields.unit_offset, fields.channel);
}

Mode1GameplayCommandRecord command_record(u32 command, u32 arg0, u32 arg1, u32 arg2) {
    Mode1GameplayCommandRecord record{};
    record.command = command;
    record.arg0 = arg0;
    record.arg1 = arg1;
    record.arg2 = arg2;
    return record;
}

void set_current_command(Mode1GameplayUnitPacketState& unit,
    const Mode1GameplayCommandRecord& record, bool clear_deferred = true) {
    unit.current_command = record;
    if (clear_deferred) {
        unit.queued_count = 0;
    }
    ++unit.command_apply_count;
    ++g_packet_dispatch_state.command_packets;
}

bool push_deferred_command(Mode1GameplayUnitPacketState& unit,
    const Mode1GameplayCommandRecord& record, u32 max_count = 10) {
    const u32 limit = std::min<u32>(max_count,
        static_cast<u32>(unit.queued_commands.size()));
    if (unit.queued_count >= limit) {
        ++unit.queue_overflow_count;
        ++unit.command_reject_count;
        return false;
    }

    unit.queued_commands[unit.queued_count++] = record;
    ++g_packet_dispatch_state.queued_command_packets;
    return true;
}

void retire_oldest_shadow_deferred_command(
    Mode1GameplayUnitPacketState& unit) {
    if (unit.queued_count == 0) {
        return;
    }

    for (u32 index = 1; index < unit.queued_count; ++index) {
        unit.queued_commands[index - 1] = unit.queued_commands[index];
    }
    --unit.queued_count;
    if (unit.queued_count < unit.queued_commands.size()) {
        unit.queued_commands[unit.queued_count] = Mode1GameplayCommandRecord{};
    }
}

void mirror_runtime_command_payload(u32 unit_offset,
    const Mode1GameplayCommandRecord& record, bool enqueue_deferred,
    bool clear_deferred) {
    if (g_packet_dispatch_state.runtime_callbacks.set_unit_command_payload == nullptr) {
        return;
    }
    g_packet_dispatch_state.runtime_callbacks.set_unit_command_payload(
        g_packet_dispatch_state.runtime_user_data, unit_offset, record.command,
        static_cast<i32>(record.arg0), static_cast<i32>(record.arg1),
        record.arg2, enqueue_deferred, clear_deferred);
}

bool mirror_runtime_deferred_resource_command(u32 unit_offset, u32 category_flag,
    u32 internal_command, u32 payload, i32 mode, u32 arg1, u32 arg2, bool enqueue,
    u32 logical_index, u8 source_channel) {
    if (g_packet_dispatch_state.runtime_callbacks
            .set_unit_deferred_resource_command == nullptr) {
        return true;
    }
    return g_packet_dispatch_state.runtime_callbacks.set_unit_deferred_resource_command(
        g_packet_dispatch_state.runtime_user_data, unit_offset, category_flag,
        internal_command, payload, mode, arg1, arg2, enqueue, logical_index,
        source_channel);
}

bool has_runtime_deferred_resource_command_callback() {
    return g_packet_dispatch_state.runtime_callbacks
        .set_unit_deferred_resource_command != nullptr;
}

bool mirror_runtime_command_flag(u32 unit_offset, u32 flag, u32 command,
    i32 mode, bool enabled) {
    if (g_packet_dispatch_state.runtime_callbacks.set_unit_command_flag == nullptr) {
        return true;
    }
    return g_packet_dispatch_state.runtime_callbacks.set_unit_command_flag(
        g_packet_dispatch_state.runtime_user_data, unit_offset, flag, command,
        mode, enabled);
}

bool has_runtime_command_flag_callback() {
    return g_packet_dispatch_state.runtime_callbacks.set_unit_command_flag !=
        nullptr;
}

void mirror_runtime_equipment_apply(u32 unit_offset, u32 effect_id) {
    if (g_packet_dispatch_state.runtime_callbacks.apply_unit_equipment_effect ==
        nullptr) {
        return;
    }
    g_packet_dispatch_state.runtime_callbacks.apply_unit_equipment_effect(
        g_packet_dispatch_state.runtime_user_data, unit_offset, effect_id);
}

u32 mirror_runtime_equipment_toggle(u32 unit_offset, u32 original_slot_code) {
    if (g_packet_dispatch_state.runtime_callbacks.toggle_unit_equipment_slot ==
        nullptr) {
        return 1;
    }
    return g_packet_dispatch_state.runtime_callbacks.toggle_unit_equipment_slot(
        g_packet_dispatch_state.runtime_user_data, unit_offset, original_slot_code);
}

void clear_current_command(Mode1GameplayUnitPacketState& unit) {
    unit.current_command = Mode1GameplayCommandRecord{};
}

bool remove_deferred_command(Mode1GameplayUnitPacketState& unit, u32 logical_index,
    u32 expected_command = 0xffffffffu) {
    if (logical_index == 0xffffffffu) {
        logical_index = unit.queued_count == 0 ? 0 : unit.queued_count;
    }

    if (logical_index == 0) {
        if (expected_command == 0xffffffffu ||
            unit.current_command.command == expected_command) {
            clear_current_command(unit);
            ++g_packet_dispatch_state.cancelled_command_packets;
            return true;
        }
        return false;
    }

    const u32 queue_index = logical_index - 1;
    if (queue_index >= unit.queued_count) {
        return false;
    }
    if (expected_command != 0xffffffffu &&
        unit.queued_commands[queue_index].command != expected_command) {
        return false;
    }

    for (u32 index = queue_index + 1; index < unit.queued_count; ++index) {
        unit.queued_commands[index - 1] = unit.queued_commands[index];
    }
    --unit.queued_count;
    ++g_packet_dispatch_state.cancelled_command_packets;
    return true;
}

void clear_queue_tail_slot(Mode1GameplayUnitPacketState& unit) {
    if (unit.queued_count < unit.queued_commands.size()) {
        unit.queued_commands[unit.queued_count] = Mode1GameplayCommandRecord{};
    }
}

bool command_payload_matches(const Mode1GameplayCommandRecord& record,
    u32 expected_payload) {
    if (record.command == 0xffffffffu) {
        return false;
    }
    return record.arg0 == expected_payload;
}

bool remove_deferred_resource_command(Mode1GameplayUnitPacketState& unit,
    u32 logical_index, u32 expected_payload) {
    if (logical_index == 0xffffffffu) {
        logical_index = unit.queued_count == 0 ? 0 : unit.queued_count;
        if (logical_index != 0) {
            expected_payload = unit.queued_commands[logical_index - 1].arg0;
        }
        else if (unit.current_command.command != 0xffffffffu) {
            expected_payload = unit.current_command.arg0;
        }
    }

    if (logical_index == 0) {
        if (command_payload_matches(unit.current_command, expected_payload)) {
            clear_current_command(unit);
            ++g_packet_dispatch_state.cancelled_command_packets;
            return true;
        }
        return false;
    }

    if (logical_index > unit.queued_count) {
        return false;
    }

    const u32 queue_index = logical_index - 1;
    if (!command_payload_matches(unit.queued_commands[queue_index],
            expected_payload)) {
        return false;
    }

    for (u32 index = queue_index + 1; index < unit.queued_count; ++index) {
        unit.queued_commands[index - 1] = unit.queued_commands[index];
    }
    --unit.queued_count;
    clear_queue_tail_slot(unit);
    ++g_packet_dispatch_state.cancelled_command_packets;
    return true;
}

bool resolve_deferred_resource_target(const Mode1GameplayUnitPacketState& unit,
    u32 logical_index, u32& resolved_index,
    Mode1GameplayCommandRecord& record) {
    resolved_index = logical_index;
    if (resolved_index == 0xffffffffu) {
        resolved_index = unit.queued_count == 0 ? 0 : unit.queued_count;
    }

    if (resolved_index == 0) {
        if (unit.current_command.command == 0xffffffffu) {
            return false;
        }
        record = unit.current_command;
        return true;
    }

    if (resolved_index > unit.queued_count) {
        return false;
    }

    record = unit.queued_commands[resolved_index - 1];
    return record.command != 0xffffffffu;
}

void debit_command_cost(Mode1GameplayUnitPacketState& unit, u32 primary = 1,
    u32 secondary = 0) {
    unit.resource_primary += primary;
    unit.resource_secondary += secondary;
    ++unit.cost_debit_count;
}

void refund_command_cost(Mode1GameplayUnitPacketState& unit, u32 primary = 1,
    u32 secondary = 0) {
    unit.resource_primary = primary < unit.resource_primary ? unit.resource_primary - primary : 0;
    unit.resource_secondary =
        secondary < unit.resource_secondary ? unit.resource_secondary - secondary : 0;
    ++unit.cost_refund_count;
}

bool active_player(u32 player) {
    const PlayerSlotRuntimeState& slots = player_slot_state();
    if (player >= slots.slot_states.size()) {
        return false;
    }

    // HandleSubtype15PlayerConsensusPacket (0x004ddc90) reads the eight raw
    // DAT_007251F4 bytes for every vote/authority decision.  Its packet-side
    // mirrors may legitimately be stale immediately after a reset or a
    // DirectPlay departure and are not an additional eligibility gate.
    const u8 state = slots.slot_states[player];
    return state != static_cast<u8>(PlayerSlotState::player_controlled) &&
        state != static_cast<u8>(PlayerSlotState::disabled);
}

bool active_players_have_vote(u32 vote_bit) {
    for (u32 player = 0; player < g_packet_dispatch_state.players.size(); ++player) {
        if (active_player(player) &&
            (g_packet_dispatch_state.players[player].vote_mask & vote_bit) == 0) {
            return false;
        }
    }
    return true;
}

void apply_inactive_player_slot_side_effects(PlayerSlotRuntimeState& slots,
    u32 player, u8 lobby_code, bool update_lobby_rotation) {
    if (player >= kPlayerSlotCount) {
        return;
    }

    slots.slot_states[player] = static_cast<u8>(PlayerSlotState::disabled);
    if (update_lobby_rotation) {
        HandleTeamRotationPlayerSlotDisabled(slots, player);
    }

    const u32 bit = 1u << player;
    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        slots.owner_relation_masks[owner] |= bit;
        slots.owner_visibility_masks[owner] &= ~bit;
    }
    slots.owner_relation_masks[player] = 0xffffffffu;
    slots.owner_visibility_masks[player] = 0;
    slots.global_active_slot_mask |= bit;
    slots.inactive_publish_states[player] = lobby_code;
}

void apply_inactive_player_slot_side_effects(u32 player, u8 lobby_code) {
    apply_inactive_player_slot_side_effects(player_slot_state(), player,
        lobby_code, true);
}

void record_player_inactive_notification(u32 target_slot, u32 source_slot,
    u8 lobby_code) {
    ++g_packet_dispatch_state.player_inactive_notifications;
    g_packet_dispatch_state.last_inactive_notification_target = target_slot;
    g_packet_dispatch_state.last_inactive_notification_source = source_slot;
    g_packet_dispatch_state.last_inactive_notification_code = lobby_code;
    g_packet_dispatch_state.player_inactive_notification_pending = true;
    if (g_packet_dispatch_state.runtime_callbacks
            .queue_player_inactive_notification != nullptr) {
        g_packet_dispatch_state.runtime_callbacks
            .queue_player_inactive_notification(
                g_packet_dispatch_state.runtime_user_data, target_slot,
                source_slot, lobby_code);
    }
}

bool apply_subtype15_inactive_marker(PlayerSlotRuntimeState& slots,
    u32 target_slot, u32 source_slot, u32 frame_tick) {
    if (target_slot >= g_packet_dispatch_state.players.size() ||
        target_slot >= kPlayerSlotCount) {
        return false;
    }

    Mode1GameplayPlayerPacketState& player =
        g_packet_dispatch_state.players[target_slot];

    slots.inactive_target_slot = target_slot;
    slots.inactive_source_slot = source_slot;

    player.status = static_cast<u8>(PlayerSlotState::disabled);
    player.inactive = true;
    player.lobby_code = frame_tick >= 0x708 ? 1 : 2;
    player.ready_flag = player.lobby_code;
    if (g_packet_dispatch_state.active_player_count != 0) {
        --g_packet_dispatch_state.active_player_count;
    }
    ++g_packet_dispatch_state.player_inactive_packets;

    const u32 bit = 1u << target_slot;
    for (auto& relation : g_packet_dispatch_state.players) {
        relation.relation_mask |= bit;
        relation.relation_block_mask &= ~bit;
    }
    player.relation_mask = 0xffffffffu;
    player.relation_block_mask = 0;

    // MarkPlayerInactiveAndBroadcastSubtype15 (0x004ddd0f) changes only the
    // gameplay/runtime status table.  The mode-8 lobby rotation helper is
    // exclusive to subtype 0x13 (0x004ddaf3 -> 0x00427320).
    apply_inactive_player_slot_side_effects(slots, target_slot,
        player.lobby_code, false);
    SetMode1ReliablePlayerStatus(target_slot,
        static_cast<u8>(PlayerSlotState::disabled));

    Mode1ReliablePacket marker = BuildMode1GameplayPacket(
        (0x15u << 24) | (target_slot & 0xffu), frame_tick);
    const bool accepted = AcceptMode1OrderedPacket(marker.bytes.data(),
        kMode1ReliablePacketBytes);
    record_player_inactive_notification(target_slot, source_slot,
        player.ready_flag);
    return accepted;
}

void ApplySubtype15ConsensusDecision(u32 source_slot, u32 target_slot) {
    const u32 local = g_packet_dispatch_state.local_player_index;
    PlayerSlotRuntimeState& slots = player_slot_state();
    if (local >= kPlayerSlotCount || target_slot != local ||
        !active_player(local)) {
        return;
    }

    g_packet_dispatch_state.last_consensus_target = local;
    g_packet_dispatch_state.last_consensus_source = source_slot;

    slots.local_player_slot = local;
    if (source_slot == local) {
        for (u32 slot = 0; slot < kPlayerSlotCount; ++slot) {
            if (slot == local || !active_player(slot)) {
                continue;
            }
            MarkPlayerInactiveAndBroadcastSubtype15(slots, slot, source_slot);
        }
        g_packet_dispatch_state.session_complete_requested = true;
        return;
    }

    MarkPlayerInactiveAndBroadcastSubtype15(slots, source_slot, source_slot);
}

void mirror_runtime_high_cluster_transition(i32 transition_index,
    bool write_transition_index, bool local_scene_change) {
    if (g_packet_dispatch_state.runtime_callbacks
            .apply_high_cluster_transition == nullptr) {
        return;
    }
    g_packet_dispatch_state.runtime_callbacks.apply_high_cluster_transition(
        g_packet_dispatch_state.runtime_user_data, transition_index,
        write_transition_index, local_scene_change);
}

void request_high_cluster_transition(i32 index) {
    g_packet_dispatch_state.transition_requested = true;
    g_packet_dispatch_state.transition_index = index;
    g_packet_dispatch_state.transition_timer = 0;
    mirror_runtime_high_cluster_transition(index, true, false);
}

void promote_high_cluster_fog_visible_tiles(bool require_current_visible) {
    ++g_packet_dispatch_state.fog_promote_requests;
    g_packet_dispatch_state.last_fog_promote_requires_current_visible =
        require_current_visible;
    if (g_packet_dispatch_state.runtime_callbacks.promote_fog_visible_tiles !=
        nullptr) {
        g_packet_dispatch_state.runtime_callbacks.promote_fog_visible_tiles(
            g_packet_dispatch_state.runtime_user_data, require_current_visible);
    }
}

void refresh_consensus_decision_mask() {
    const u32 threshold = g_packet_dispatch_state.active_player_count >> 1;
    u32 decision_mask = 0;
    for (u32 bit_index = 0; bit_index < 8; ++bit_index) {
        const u32 bit = 1u << bit_index;
        u32 votes = 0;
        for (u32 player = 0; player < g_packet_dispatch_state.players.size(); ++player) {
            if (active_player(player) &&
                (g_packet_dispatch_state.players[player].consensus_mask & bit) != 0) {
                ++votes;
            }
        }
        if (votes >= threshold) {
            decision_mask |= bit;
            ApplySubtype15ConsensusDecision(bit_index,
                g_packet_dispatch_state.local_player_index);
        }
    }

    if (decision_mask != g_packet_dispatch_state.consensus_decision_mask) {
        g_packet_dispatch_state.consensus_decision_mask = decision_mask;
        ++g_packet_dispatch_state.consensus_epoch;
    }
}

void apply_high_cluster_command(u32 command_index, const Mode1ReliablePacket& packet) {
    const GameplayCheatTransitionRequest transition =
        ResolveGameplayCheatTransitionRequest(command_index);
    if (transition.requested) {
        if (transition.local_scene_change) {
            if (packet_channel_is_local(packet)) {
                g_packet_dispatch_state.local_scene_change_requested = true;
                g_packet_dispatch_state.transition_requested = true;
                g_packet_dispatch_state.transition_timer = 0;
                mirror_runtime_high_cluster_transition(
                    transition.transition_index,
                    transition.write_transition_index,
                    transition.local_scene_change);
            }
            return;
        }
        request_high_cluster_transition(transition.transition_index);
        return;
    }

    switch (command_index) {
    case 0x01:
        if (packet_channel_is_local(packet)) {
            g_packet_dispatch_state.local_toggle_state =
                !g_packet_dispatch_state.local_toggle_state;
            promote_high_cluster_fog_visible_tiles(
                g_packet_dispatch_state.local_toggle_state);
        }
        return;
    default:
        break;
    }

    const u8 channel = packet_channel(packet);
    if (command_index == 0x0f) {
        if (channel < player_slot_state().owner_primary_resources.size()) {
            u32& primary =
                player_slot_state().owner_primary_resources[channel];
            primary = ApplyGameplayCheatPrimaryResourceBonus(primary);
            if (g_packet_dispatch_state.runtime_callbacks
                    .set_owner_primary_resource != nullptr) {
                g_packet_dispatch_state.runtime_callbacks
                    .set_owner_primary_resource(
                        g_packet_dispatch_state.runtime_user_data,
                        channel, primary);
            }
        }
        return;
    }
    if (command_index == 0x10) {
        const u32 unit_offset = read_packet_u32(packet, 0x14);
        g_packet_dispatch_state.last_unit_pointer = unit_offset;
        ++g_packet_dispatch_state.unit_bonus_packets;
        if (g_packet_dispatch_state.runtime_callbacks.apply_unit_bonus != nullptr) {
            g_packet_dispatch_state.runtime_callbacks.apply_unit_bonus(
                g_packet_dispatch_state.runtime_user_data, unit_offset);
        }
        return;
    }
    if (command_index == 0x11) {
        ++g_packet_dispatch_state.global_reset_packets;
        if (g_packet_dispatch_state.runtime_callbacks.apply_global_reset != nullptr) {
            g_packet_dispatch_state.runtime_callbacks.apply_global_reset(
                g_packet_dispatch_state.runtime_user_data);
        }
        return;
    }
    if (command_index == 0x12) {
        if (channel < g_packet_dispatch_state.owner_population_limits.size()) {
            g_packet_dispatch_state.owner_population_limits[channel] = 500;
            if (g_packet_dispatch_state.runtime_callbacks
                    .set_owner_population_limit != nullptr) {
                g_packet_dispatch_state.runtime_callbacks
                    .set_owner_population_limit(
                        g_packet_dispatch_state.runtime_user_data, channel, 500);
            }
        }
        return;
    }

    ++g_packet_dispatch_state.no_op_packets;
}

void handle_no_op_packet(const Mode1ReliablePacket&, void*) {
    ++g_packet_dispatch_state.no_op_packets;
}

void handle_high_cluster_packet(const Mode1ReliablePacket& packet, void*) {
    const u8 subtype = packet.size > 0x0f ? packet.bytes[0x0f] : packet.subtype;
    if (subtype < 0x1f) {
        ++g_packet_dispatch_state.no_op_packets;
        return;
    }
    apply_high_cluster_command(subtype - 0x1f, packet);
}

void handle_resource_deferred_command(const Mode1ReliablePacket& packet,
    u32 category_flag, u32 queued_internal_command, u32 max_queued_count = 4,
    bool latest_cancel_allowed = false,
    bool synthesize_owner_queue_tail = false) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);

    if (fields.mode == 1) {
        const bool original_five_slot_cancel_table =
            (category_flag == 0x0000000cu &&
                queued_internal_command == 0x17u) ||
            (category_flag == 0x0000001au &&
                queued_internal_command == 0x22u);
        if ((fields.arg1 == 0xffffffffu && !latest_cancel_allowed) ||
            (original_five_slot_cancel_table && fields.arg1 > 4u)) {
            ++unit.command_reject_count;
            return;
        }
        const bool runtime_queue_is_authoritative =
            has_runtime_deferred_resource_command_callback();
        // The original 0x01/0x05/0x0c/0x1a handlers validate the live tuple
        // (queue index and payload) before shifting any queue entry or
        // refunding its cost.  Mutating the packet-side shadow first meant a
        // rejected runtime cancel still erased a reconstructed queue entry.
        // Let the authoritative callback accept the cancel first; only then
        // mirror the queue compaction locally.  With no runtime callback the
        // shadow remains the authoritative fallback and is validated once.
        if (runtime_queue_is_authoritative &&
            !mirror_runtime_deferred_resource_command(fields.unit_offset,
                category_flag, queued_internal_command, fields.command,
                fields.mode, fields.arg1, fields.arg2, false, fields.arg1,
                fields.channel)) {
            ++unit.command_reject_count;
            return;
        }

        const bool removed_local = remove_deferred_resource_command(unit,
            fields.arg1, fields.command);
        if (!removed_local) {
            return;
        }
        refund_command_cost(unit);
        return;
    }

    const bool runtime_queue_is_authoritative =
        has_runtime_deferred_resource_command_callback();
    if (!runtime_queue_is_authoritative &&
        unit.queued_count >= max_queued_count) {
        ++unit.command_reject_count;
        return;
    }

    // HandleSubtype0cPlacementResourcePacket does not preserve the two zero
    // scratch dwords normally published at +0x1c/+0x20.  After debiting the
    // order cost, the original leaves EDX=source_channel*0x100 and
    // EBX=source_channel, then FUN_004dd866 writes those registers as the
    // queue tuple's final two fields (0x004dcf95..0x004dcfb2).  Other resource
    // subtypes reload EDX/EBX from the wire immediately before queueing.
    const u32 queue_arg1 = synthesize_owner_queue_tail
        ? static_cast<u32>(fields.channel) * 0x100u
        : fields.arg1;
    const u32 queue_arg2 = synthesize_owner_queue_tail
        ? static_cast<u32>(fields.channel)
        : fields.arg2;

    if (!mirror_runtime_deferred_resource_command(fields.unit_offset,
            category_flag, queued_internal_command, fields.command, fields.mode,
            queue_arg1, queue_arg2, true, 0, fields.channel)) {
        ++unit.command_reject_count;
        return;
    }

    // HandleSubtype01ProductionCommandPacket checks the live unit queue count
    // at raw unit +0x124 (0x004dca68). PopDeferredUnitCommandOrReturnIdle then
    // decrements that same count when simulation consumes an entry
    // (0x004cfe00). The packet-state queue below is only a reconstructed mirror;
    // it receives packet enqueues but has no simulation-side pop notification.
    // Treating that stale mirror as the capacity gate permanently rejected a
    // producer after four lifetime commands even when its live queue was empty.
    // A successful runtime callback proves that the live queue had room. Retire
    // the oldest stale mirror entry only as needed to keep the diagnostic/cancel
    // mirror bounded, while leaving the callback as the authoritative gate.
    if (runtime_queue_is_authoritative) {
        const u32 bounded_limit = std::min<u32>(max_queued_count,
            static_cast<u32>(unit.queued_commands.size()));
        while (bounded_limit != 0 && unit.queued_count >= bounded_limit) {
            retire_oldest_shadow_deferred_command(unit);
        }
    }

    if (push_deferred_command(unit,
            command_record(queued_internal_command, fields.command, queue_arg1,
                queue_arg2),
            max_queued_count)) {
        debit_command_cost(unit);
    }
}

bool reroute_subtype01_latest_resource_cancel(const Mode1ReliablePacket& packet) {
    const PacketFields fields = packet_fields(packet);
    if (fields.mode != 1 || fields.arg1 != 0xffffffffu) {
        return false;
    }

    // The packet-side queue is only a diagnostic mirror and is not notified
    // when simulation consumes an entry.  With a live runtime callback,
    // default_mode1_packet_set_unit_deferred_resource_command resolves -1
    // against the authoritative UnitMovementUnit active/deferred tuples, just
    // as HandleSubtype01ProductionCommandPacket (0x004dca58) does.  Rerouting
    // from this stale shadow can otherwise send a normal production cancel to
    // the previous upgrade/placement refund handler.
    if (has_runtime_deferred_resource_command_callback()) {
        return false;
    }

    Mode1GameplayUnitPacketState& unit =
        unit_state_for(fields.unit_offset, fields.channel);
    u32 resolved_index = 0;
    Mode1GameplayCommandRecord record{};
    if (!resolve_deferred_resource_target(unit, fields.arg1, resolved_index,
            record)) {
        return false;
    }

    if (record.command != 0x17 && record.command != 0x22) {
        return false;
    }

    Mode1ReliablePacket rerouted = packet;
    write_packet_u32(rerouted, 0x10, record.arg0);
    write_packet_u32(rerouted, 0x1c, resolved_index);
    if (record.command == 0x17) {
        handle_placement_resource_packet(rerouted, nullptr);
    }
    else if (record.command == 0x22) {
        handle_production_cost_packet(rerouted, nullptr);
    }
    else {
        handle_resource_deferred_command(rerouted, 0x00000017, 0x10);
    }
    return true;
}

void handle_unit_production_packet(const Mode1ReliablePacket& packet, void*) {
    ++g_packet_dispatch_state.production_packets;
    if (reroute_subtype01_latest_resource_cancel(packet)) {
        return;
    }
    handle_resource_deferred_command(packet, 0x00000017, 0x10, 4, true);
}

void apply_special_order_flag(Mode1GameplayUnitPacketState& unit,
    const PacketFields& fields, u32 command, u32 flag) {
    if (fields.mode == 1) {
        if (mirror_runtime_command_flag(fields.unit_offset, flag, command,
                fields.mode, false)) {
            unit.state_flags &= ~flag;
        }
        return;
    }
    if (fields.mode != 0) {
        return;
    }
    if (mirror_runtime_command_flag(fields.unit_offset, flag, command,
            fields.mode, true)) {
        debit_command_cost(unit);
        unit.state_flags |= flag;
    }
}

void handle_unit_order_with_command(const Mode1ReliablePacket& packet,
    u32 command, u32 prefix, bool apply_basic_special_flags) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);

    if (apply_basic_special_flags) {
        switch (command) {
        case 0x12:
            apply_special_order_flag(unit, fields, command, 0x00004000);
            return;
        case 0x13:
            apply_special_order_flag(unit, fields, command, 0x00008000);
            return;
        case 0x14:
            apply_special_order_flag(unit, fields, command, 0x00010000);
            return;
        case 0x15:
            apply_special_order_flag(unit, fields, command, 0x00020000);
            return;
        default:
            break;
        }
    }

    const Mode1GameplayCommandRecord record =
        command_record((command & 0x7fffffffu) | prefix,
            static_cast<u32>(fields.mode), fields.arg1, fields.arg2);
    if ((command & 0x80000000u) == 0) {
        unit.state_flags &= ~0x20u;
        set_current_command(unit, record);
        mirror_runtime_command_payload(fields.unit_offset, record, false, true);
        return;
    }

    // The packet-side queue is only a diagnostic mirror.  Simulation removes
    // commands from UnitMovementUnit without retiring this shadow, so after a
    // long shift-queue sequence it can remain full while the authoritative
    // raw queue has free slots.  Original 0x004dd55f delegates the packet to
    // raw queue helper 0x004dd866; never let the stale mirror suppress that
    // runtime delivery.  Retire only the shadow's oldest entry to keep its
    // bounded diagnostics moving forward.
    if (ShouldRetireMode1PacketShadowBeforeRuntimeQueue(
            unit.queued_count, static_cast<u32>(unit.queued_commands.size()),
            g_packet_dispatch_state.runtime_callbacks.set_unit_command_payload !=
                nullptr)) {
        retire_oldest_shadow_deferred_command(unit);
    }
    if (push_deferred_command(unit, record)) {
        mirror_runtime_command_payload(fields.unit_offset, record, true, false);
    }
}

void handle_basic_unit_order_with_command(const Mode1ReliablePacket& packet,
    u32 command, u32 prefix) {
    handle_unit_order_with_command(packet, command, prefix, true);
}

void handle_basic_unit_order_packet(const Mode1ReliablePacket& packet, void*) {
    handle_basic_unit_order_with_command(packet, read_packet_u32(packet, 0x10),
        0x01000000);
}

void handle_unit_apply_command_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    unit_state_for(fields.unit_offset, fields.channel);
    mirror_runtime_equipment_apply(fields.unit_offset,
        fields.command & 0x7fffffffu);
}

void handle_unit_apply_or_fallback_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);
    const u32 command = fields.command & 0x7fffffffu;
    const u32 toggle_result =
        mirror_runtime_equipment_toggle(fields.unit_offset, command);
    if (toggle_result <= 1) {
        return;
    }

    const u32 fallback_arg0 = fields.mode != 0
        ? static_cast<u32>(fields.mode)
        : command | 0x80000000u;
    const u32 fallback_arg1 = fields.mode != 0 ? command : fields.arg1;
    const Mode1GameplayCommandRecord fallback =
        command_record(0x01000016, fallback_arg0, fallback_arg1, fields.arg2);
    if ((fields.command & 0x80000000u) == 0) {
        unit.state_flags &= ~0x20u;
        set_current_command(unit, fallback);
        mirror_runtime_command_payload(fields.unit_offset, fallback, false, true);
        return;
    }
    if (push_deferred_command(unit, fallback)) {
        mirror_runtime_command_payload(fields.unit_offset, fallback, true, false);
    }
}

void handle_resource_build_packet(const Mode1ReliablePacket& packet, void*) {
    ++g_packet_dispatch_state.resource_packets;
    handle_resource_deferred_command(packet, 0x00000005, 0x10, 4, true);
}

void handle_unit_command_bit_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);
    const u32 bit = fields.command;
    const u32 byte_index = bit >> 3;
    if (byte_index >= unit.command_bits.size()) {
        return;
    }

    const u8 mask = static_cast<u8>(1u << (bit & 7));
    const bool enabled = fields.mode != 1;
    if (fields.mode != 1) {
        unit.command_bits[byte_index] |= mask;
    }
    else {
        unit.command_bits[byte_index] &= static_cast<u8>(~mask);
    }
    if (g_packet_dispatch_state.runtime_callbacks.set_unit_command_bit != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.set_unit_command_bit(
            g_packet_dispatch_state.runtime_user_data, fields.unit_offset, bit,
            enabled);
    }
}

void handle_unit_death_mark_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);
    unit.order_flags |= 0x10000000;
    unit.runtime_state = 1;
    unit.death_marked = true;
    if (g_packet_dispatch_state.runtime_callbacks.mark_unit_death != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.mark_unit_death(
            g_packet_dispatch_state.runtime_user_data, fields.unit_offset);
    }
}

void handle_unit_aux_vector_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);
    unit.aux0 = static_cast<u32>(fields.mode);
    unit.aux1 = fields.arg1;
    unit.aux2 = fields.arg2;
    if (g_packet_dispatch_state.runtime_callbacks.set_unit_aux_vector != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.set_unit_aux_vector(
            g_packet_dispatch_state.runtime_user_data, fields.unit_offset,
            static_cast<u32>(fields.mode), static_cast<i32>(fields.arg1),
            static_cast<i32>(fields.arg2));
    }
}

void handle_extended_unit_order_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);
    if (fields.command == 0x13) {
        if (fields.mode == -1) {
            if (mirror_runtime_command_flag(fields.unit_offset, 0x840u,
                    fields.command, fields.mode, false)) {
                unit.state_flags &= ~0x840u;
            }
        }
        else {
            const bool packet_state_allows = (unit.state_flags & 0x800u) == 0;
            if ((has_runtime_command_flag_callback() || packet_state_allows) &&
                mirror_runtime_command_flag(fields.unit_offset, 0x840u,
                    fields.command, fields.mode, true)) {
                debit_command_cost(unit, 1, 1);
                unit.state_flags |= 0x840u;
            }
        }
        return;
    }

    // Subtype 0x09 selectors 0x12/0x14/0x15 are ordinary deferred ability
    // commands.  Only subtype 0x02 owns the similarly numbered 0x4000,
    // 0x10000 and 0x20000 flag toggles.  Sharing its dispatcher silently
    // converted Phantom action 0x12 and Dark-Elf actions 0x14/0x15 into the
    // wrong packet family, so the original peer executed an effect while the
    // reconstructed peer stayed idle.
    handle_unit_order_with_command(
        packet, fields.command, 0x21000000, false);
}

void handle_forced_order21_packet(const Mode1ReliablePacket& packet, void*) {
    auto& unit = unit_state_for_packet(packet);
    unit.order_flags |= 8;
    const PacketFields fields = packet_fields(packet);
    if (g_packet_dispatch_state.runtime_callbacks.force_order21 != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.force_order21(
            g_packet_dispatch_state.runtime_user_data, fields.unit_offset);
    }
    handle_basic_unit_order_with_command(packet, 0x21, 0x01000000);
}

void handle_unit_status_mask_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    auto& unit = unit_state_for(fields.unit_offset, fields.channel);
    unit.status_mask = (unit.status_mask & 0x7fffffffu) | static_cast<u32>(fields.mode);
    if (g_packet_dispatch_state.runtime_callbacks.set_unit_status_mask != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.set_unit_status_mask(
            g_packet_dispatch_state.runtime_user_data, fields.unit_offset,
            static_cast<u32>(fields.mode));
    }
}

void handle_placement_resource_packet(const Mode1ReliablePacket& packet, void*) {
    ++g_packet_dispatch_state.resource_packets;
    handle_resource_deferred_command(packet, 0x0000000c, 0x17, 10, false,
        true);
}

void handle_nested_subtype13_packet(const Mode1ReliablePacket& packet, void*) {
    const u32 command_index = read_packet_u32(packet, 0x10);
    ++g_packet_dispatch_state.nested_subtype13_dispatches;
    if (command_index >= kOriginalSubtype13NestedTargets.size()) {
        g_packet_dispatch_state.missing_handler_seen = true;
        return;
    }
    apply_high_cluster_command(command_index, packet);
}

void handle_catchup_target_packet(const Mode1ReliablePacket& packet, void*) {
    HandleSubtype0fCatchupTargetPacket(packet);
}

void handle_consumed_ack_packet(const Mode1ReliablePacket& packet, void*) {
    const u8 channel = packet_channel(packet);
    if (channel < g_packet_dispatch_state.player_wait_budget.size() &&
        g_packet_dispatch_state.player_wait_budget[channel] < 40000) {
        g_packet_dispatch_state.player_wait_budget[channel] += 10;
    }

    Mode1ReliableRuntimeState& reliable = mode1_reliable_state();
    if (channel >= kMode1ReliableChannelCount) {
        return;
    }

    if (reliable.wait_budget[channel] < 40000) {
        reliable.wait_budget[channel] += 10;
    }
    reliable.last_progress_time[channel] = reliable.current_time_ms;
    reliable.sync_consumed_flags[channel] = 1;

    const u32 local = reliable.local_player_index;
    if (local >= kMode1ReliableChannelCount || local == channel ||
        reliable.subtype10_counts[local] == 0 ||
        reliable.subtype10_counts[channel] == 0) {
        return;
    }

    const u32 local_value = reliable.subtype10_values[local][0];
    const u32 remote_value = reliable.subtype10_values[channel][0];
    if (local_value == remote_value) {
        return;
    }
    if (!g_packet_dispatch_state.generic_ai_profile_mode) {
        return;
    }

    ++g_packet_dispatch_state.corrective_mismatch_packets;
    g_packet_dispatch_state.corrective_mismatch_detected = true;
    g_packet_dispatch_state.last_corrective_channel = channel;
    g_packet_dispatch_state.last_corrective_sequence = read_packet_u32(packet, 0x08);
    g_packet_dispatch_state.last_corrective_local_value = local_value;
    g_packet_dispatch_state.last_corrective_remote_value = remote_value;
    g_packet_dispatch_state.last_corrective_frame = reliable.replay_frame_tick;

    // HandleMode1ConsumedPacketAck (0x004297a3..0x004297b7) leaves the
    // mismatching remote channel in EDX, the local checksum in ESI and the
    // remote checksum in EDI before PublishMode1CorrectiveChecksum.  Its
    // packet builder (0x004de71d) consequently writes those values to +0x1c,
    // +0x14 and +0x18 respectively.  In particular, +0x1c is the target read
    // by HandleSubtype15PlayerConsensusPacket.  Publishing a zero-filled
    // corrective packet only happened to work when the mismatching peer was
    // slot zero; a reconstructed slot-zero host would vote against itself
    // instead of the remote peer.
    BroadcastMode1GameplayPacket((0x15u << 24) | (local & 0xffu), 0,
        local_value, remote_value, channel, 0);
    ++g_packet_dispatch_state.corrective_checksum_broadcasts;

    Mode1ReliablePacket corrective = BuildMode1GameplayPacket(
        (0x1bu << 24) | channel,
        g_packet_dispatch_state.last_corrective_sequence,
        local_value,
        remote_value,
        channel,
        g_packet_dispatch_state.last_corrective_frame);
    g_packet_dispatch_state.last_corrective_packet = corrective;
    AcceptMode1OrderedPacket(corrective.bytes.data(), kMode1ReliablePacketBytes);
}

void handle_player_inactive_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    // FUN_004db82d publishes the pre-removal subtype-1d completion packet for
    // the departing slot.  The captured original peer does the same: after a
    // slot-1 subtype-13 surrender it sends subtype 0x1d with packet +0x10 == 1.
    // The subtype-1d receiver compares this field with its local slot before
    // recording the source channel's vote, so substituting the subtype value
    // (0x13) leaves the surrendering reconstruction in the checksum loop.
    PublishVoteCompletionAndFlushReliableRange(gameplay_production_action_state(),
        fields.channel);

    const u32 player = fields.channel;
    if (player >= g_packet_dispatch_state.players.size()) {
        return;
    }
    if (player == g_packet_dispatch_state.local_player_index) {
        // This marker is intentionally set by ordered dispatch, not by the
        // publisher.  FUN_004db82d reaches this point only after the local
        // terminal packet has advanced through the reliable read sequence.
        g_packet_dispatch_state.local_inactive_packet_consumed = true;
        return;
    }

    // HandleSubtype13PlayerInactivePacket writes the shared status byte read
    // by PumpMode1ReliablePackets, not only its gameplay-dispatch mirror.  The
    // setter also removes a departed channel from an already-open sync round.
    SetMode1ReliablePlayerStatus(player, 0x14);
    auto& state = g_packet_dispatch_state.players[player];
    state.status = 0x14;
    state.inactive = true;
    state.lobby_code = static_cast<u8>(fields.arg1);
    state.ready_flag = state.lobby_code;
    if (g_packet_dispatch_state.active_player_count != 0) {
        --g_packet_dispatch_state.active_player_count;
    }
    ++g_packet_dispatch_state.player_inactive_packets;

    const u32 bit = 1u << player;
    for (auto& relation : g_packet_dispatch_state.players) {
        relation.relation_mask |= bit;
        relation.relation_block_mask &= ~bit;
    }
    state.relation_mask = 0xffffffffu;
    state.relation_block_mask = 0;
    apply_inactive_player_slot_side_effects(player, state.lobby_code);
    if (fields.command != 3) {
        // HandleSubtype13PlayerInactivePacket uses +0x1c only for the remote
        // slot's lobby/ready code.  The notification suffix is selected by
        // packet +0x10 (0=left, 1=defeated, 2=dropped); 3 suppresses it.
        record_player_inactive_notification(player, player,
            static_cast<u8>(fields.command));
    }

#ifdef _WIN32
    ClearDirectPlayPlayerSlotId(player, false);
#endif
}

void handle_player_relation_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    if (fields.channel >= g_packet_dispatch_state.players.size()) {
        return;
    }

    auto& player = g_packet_dispatch_state.players[fields.channel];
    player.relation_mask = fields.arg1;
    player.relation_block_mask = fields.arg2;
    player.relation_dirty = fields.command == 1;
    ++g_packet_dispatch_state.player_relation_packets;

    if (fields.channel < kPlayerSlotCount) {
        PlayerSlotRuntimeState& slots = player_slot_state();
        const u32 bit = 1u << fields.channel;
        slots.owner_relation_masks[fields.channel] = fields.arg1;
        slots.owner_visibility_masks[fields.channel] = fields.arg2;
        if (fields.command == 1) {
            slots.global_active_slot_mask |= bit;
        }
        else {
            slots.global_active_slot_mask &= ~bit;
        }
    }
}

void handle_player_consensus_packet(const Mode1ReliablePacket& packet, void*) {
    const PacketFields fields = packet_fields(packet);
    if (fields.channel >= g_packet_dispatch_state.players.size()) {
        return;
    }

    if (fields.command != 0) {
        g_packet_dispatch_state.players[fields.channel].consensus_mask =
            static_cast<u8>(fields.arg1);
        refresh_consensus_decision_mask();
        return;
    }

    const u32 target = fields.arg1;
    if (target >= g_packet_dispatch_state.players.size() ||
        target != g_packet_dispatch_state.local_player_index) {
        return;
    }

    ApplySubtype15ConsensusDecision(fields.channel, target);
    if (fields.channel == g_packet_dispatch_state.local_player_index) {
        return;
    }
    g_packet_dispatch_state.vote_collection_active = true;
    const u32 vote_bit = 1u << fields.channel;
    g_packet_dispatch_state.players[target].vote_mask |= vote_bit;
    g_packet_dispatch_state.all_votes_received = active_players_have_vote(vote_bit);
}

void handle_modal_pause_packet(const Mode1ReliablePacket& packet, void*) {
    const bool visible = read_packet_u32(packet, 0x10) == 1;
    const bool was_visible = g_packet_dispatch_state.modal_pause_visible;
    const u8 channel = packet_channel(packet);
    if (visible && !was_visible &&
        g_packet_dispatch_state.generic_ai_profile_mode &&
        channel < g_packet_dispatch_state.player_modal_pause_uses_remaining.size()) {
        u8& remaining =
            g_packet_dispatch_state.player_modal_pause_uses_remaining[channel];
        if (remaining != 0) {
            --remaining;
        }
    }
    g_packet_dispatch_state.modal_pause_visible = visible;
    if (g_packet_dispatch_state.runtime_callbacks.set_modal_pause != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.set_modal_pause(
            g_packet_dispatch_state.runtime_user_data, visible);
    }
}

void handle_pending_unit_action_packet(const Mode1ReliablePacket& packet, void*) {
    HandleSubtype19PendingUnitActionPacket(packet);
}

void handle_production_cost_packet(const Mode1ReliablePacket& packet, void*) {
    ++g_packet_dispatch_state.production_packets;
    handle_resource_deferred_command(packet, 0x0000001a, 0x22);
}

void handle_vote_completion_packet(const Mode1ReliablePacket& packet, void*) {
    HandleSubtype1dVoteCompletionPacket(packet);
}

bool should_flush_published_packet_range() {
    if (g_packet_dispatch_state.auto_broadcast_published_packets) {
        return true;
    }
#ifdef _WIN32
    return async_com_state().active_network_transport_mode == 3 &&
        g_packet_dispatch_state.active_player_count > 1;
#else
    return false;
#endif
}

u32 allocate_packet_string_slot_fallback() {
    const u32 slot = g_packet_dispatch_state.next_string_slot;
    g_packet_dispatch_state.next_string_slot =
        g_packet_dispatch_state.next_string_slot == 0xff ?
        1 : g_packet_dispatch_state.next_string_slot + 1;
    return slot;
}

void clear_unit_pending_string_slot(u32 unit_offset,
    Mode1GameplayUnitPacketState& unit) {
    const u32 previous_slot = unit.pending_string_slot;
    if (previous_slot != 0) {
        ++g_packet_dispatch_state.string_slot_clear_packets;
    }
    // The packet-side mirror can be empty after a load/reset while the live
    // unit still owns a string slot.  Let the authoritative callback inspect
    // the unit even when the shadow previous_slot is zero.  The default
    // callback derives the real slot from unit_offset and treats zero as a
    // harmless no-op when neither side owns one.
    if (g_packet_dispatch_state.runtime_callbacks.clear_unit_string != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.clear_unit_string(
            g_packet_dispatch_state.runtime_user_data, unit_offset, previous_slot);
    }
    unit.pending_string_slot = 0;
}

u32 intern_unit_pending_string_slot(u32 unit_offset, const char* text) {
    if (g_packet_dispatch_state.runtime_callbacks.intern_unit_string == nullptr) {
        return 0xffffffffu;
    }

    return g_packet_dispatch_state.runtime_callbacks.intern_unit_string(
        g_packet_dispatch_state.runtime_user_data, unit_offset, text);
}

Mode1GameplayPacketDispatchState make_initial_dispatch_state() {
    Mode1GameplayPacketDispatchState state{};
    state.original_handler_addresses = kOriginalMode1PacketHandlers;
    state.original_low_subtype_targets = kOriginalLowSubtypeTargets;
    state.original_nested_subtype13_targets = kOriginalSubtype13NestedTargets;
    state.active_player_count = kPlayerSlotCount;
    state.owner_population_limits.fill(180);
    state.player_modal_pause_uses_remaining.fill(4);

    state.handlers[0x00] = handle_no_op_packet;
    state.handlers[0x01] = handle_unit_production_packet;
    state.handlers[0x02] = handle_basic_unit_order_packet;
    state.handlers[0x03] = handle_unit_apply_command_packet;
    state.handlers[0x04] = handle_unit_apply_or_fallback_packet;
    state.handlers[0x05] = handle_resource_build_packet;
    state.handlers[0x06] = handle_unit_command_bit_packet;
    state.handlers[0x07] = handle_unit_death_mark_packet;
    state.handlers[0x08] = handle_unit_aux_vector_packet;
    state.handlers[0x09] = handle_extended_unit_order_packet;
    state.handlers[0x0a] = handle_forced_order21_packet;
    state.handlers[0x0b] = handle_unit_status_mask_packet;
    state.handlers[0x0c] = handle_placement_resource_packet;
    state.handlers[0x0d] = handle_nested_subtype13_packet;
    state.handlers[0x0e] = handle_no_op_packet;
    state.handlers[0x0f] = handle_catchup_target_packet;
    state.handlers[0x10] = handle_consumed_ack_packet;
    state.handlers[0x11] = handle_no_op_packet;
    state.handlers[0x12] = handle_no_op_packet;
    state.handlers[0x13] = handle_player_inactive_packet;
    state.handlers[0x14] = handle_player_relation_packet;
    state.handlers[0x15] = handle_player_consensus_packet;
    state.handlers[0x16] = handle_modal_pause_packet;
    state.handlers[0x17] = handle_no_op_packet;
    state.handlers[0x18] = handle_no_op_packet;
    state.handlers[0x19] = handle_pending_unit_action_packet;
    state.handlers[0x1a] = handle_production_cost_packet;
    state.handlers[0x1b] = handle_no_op_packet;
    state.handlers[0x1c] = handle_no_op_packet;
    state.handlers[0x1d] = handle_vote_completion_packet;
    for (u8 subtype = 0x1f; subtype <= 0x35; ++subtype) {
        state.handlers[subtype] = handle_high_cluster_packet;
    }
    return state;
}

const bool g_packet_dispatch_state_initialized = []() {
    g_packet_dispatch_state = make_initial_dispatch_state();
    return true;
}();

}

Mode1GameplayPacketDispatchState& mode1_gameplay_packet_dispatch_state() {
    return g_packet_dispatch_state;
}

void ResetMode1GameplayPacketDispatch() {
    void* const user_data = g_packet_dispatch_state.user_data;
    void* const runtime_user_data = g_packet_dispatch_state.runtime_user_data;
    const Mode1GameplayRuntimeCallbacks runtime_callbacks =
        g_packet_dispatch_state.runtime_callbacks;
    const auto handlers = g_packet_dispatch_state.handlers;
    const u32 local_player = g_packet_dispatch_state.local_player_index;
    const bool generic_ai_profile_mode = g_packet_dispatch_state.generic_ai_profile_mode;
    const u32 active_player_count = g_packet_dispatch_state.active_player_count;
    g_packet_dispatch_state = Mode1GameplayPacketDispatchState{};
    g_packet_dispatch_state.original_handler_addresses = kOriginalMode1PacketHandlers;
    g_packet_dispatch_state.original_low_subtype_targets = kOriginalLowSubtypeTargets;
    g_packet_dispatch_state.original_nested_subtype13_targets =
        kOriginalSubtype13NestedTargets;
    g_packet_dispatch_state.handlers = handlers;
    g_packet_dispatch_state.local_player_index = local_player;
    g_packet_dispatch_state.generic_ai_profile_mode = generic_ai_profile_mode;
    g_packet_dispatch_state.active_player_count =
        active_player_count != 0 ? active_player_count : kPlayerSlotCount;
    g_packet_dispatch_state.owner_population_limits.fill(180);
    g_packet_dispatch_state.player_modal_pause_uses_remaining.fill(4);
    g_packet_dispatch_state.user_data = user_data;
    g_packet_dispatch_state.runtime_callbacks = runtime_callbacks;
    g_packet_dispatch_state.runtime_user_data = runtime_user_data;
}

void SetMode1GameplayPacketHandler(u8 subtype, Mode1GameplayPacketHandler handler) {
    if (subtype < kMode1GameplayPacketSubtypeCount) {
        g_packet_dispatch_state.handlers[subtype] = handler;
    }
}

void SetMode1GameplayPacketDispatchUserData(void* user_data) {
    g_packet_dispatch_state.user_data = user_data;
}

void SetMode1GameplayRuntimeCallbacks(
    const Mode1GameplayRuntimeCallbacks& callbacks, void* user_data) {
    g_packet_dispatch_state.runtime_callbacks = callbacks;
    g_packet_dispatch_state.runtime_user_data = user_data;
}

void SetMode1GameplayActivePlayerCount(u32 active_player_count) {
    g_packet_dispatch_state.active_player_count =
        std::min<u32>(std::max<u32>(active_player_count, 1), kPlayerSlotCount);
}

void SetMode1GameplayGenericAiProfileMode(bool enabled) {
    g_packet_dispatch_state.generic_ai_profile_mode = enabled;
}

void ResetMode1GameplayVoteCompletionGate() {
    g_packet_dispatch_state.vote_completion_seen.fill(0);
    g_packet_dispatch_state.vote_completion_gate_open = true;
}

void BroadcastMode1GameplayPlayerInactive(PlayerSlotRuntimeState& state,
    u32 target_slot, u32 source_slot) {
    apply_subtype15_inactive_marker(state, target_slot,
        source_slot, mode1_reliable_state().replay_frame_tick);
    // ApplySubtype15ConsensusDecision writes ready/inactive state 4 in its
    // local-authority loop after MarkPlayerInactiveAndBroadcastSubtype15
    // returns (0x004ddcf9).  That write is independent of whether the nested
    // AcceptMode1OrderedPacket call accepted its synthesized subtype-15
    // marker.  Keep both the packet mirror and the live player-slot state in
    // that post-call state instead of leaving the latter at reason 1/2.
    if (source_slot == state.local_player_slot &&
        target_slot != state.local_player_slot &&
        target_slot < g_packet_dispatch_state.players.size() &&
        g_packet_dispatch_state.players[target_slot].inactive) {
        g_packet_dispatch_state.players[target_slot].ready_flag = 4;
        state.inactive_publish_states[target_slot] = 4;
    }
}

void MarkPlayerInactiveAndBroadcastSubtype15(PlayerSlotRuntimeState& state,
    u32 target_slot, u32 source_slot) {
    BroadcastMode1GameplayPlayerInactive(state, target_slot, source_slot);
}

void ShowGameplayModalPauseOverlay() {
    g_packet_dispatch_state.modal_pause_visible = true;
    if (g_packet_dispatch_state.runtime_callbacks.set_modal_pause != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.set_modal_pause(
            g_packet_dispatch_state.runtime_user_data, true);
    }
}

void HideGameplayModalPauseOverlay() {
    g_packet_dispatch_state.modal_pause_visible = false;
    if (g_packet_dispatch_state.runtime_callbacks.set_modal_pause != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.set_modal_pause(
            g_packet_dispatch_state.runtime_user_data, false);
    }
}

void HandleSubtype01ProductionCommandPacket(const Mode1ReliablePacket& packet) {
    handle_unit_production_packet(packet, nullptr);
}

void HandleSubtype02UnitOrderPacket(const Mode1ReliablePacket& packet) {
    handle_basic_unit_order_packet(packet, nullptr);
}

void ApplySubtype03UnitCommandPacket(const Mode1ReliablePacket& packet) {
    handle_unit_apply_command_packet(packet, nullptr);
}

void ApplySubtype04UnitCommandFallbackPacket(const Mode1ReliablePacket& packet) {
    handle_unit_apply_or_fallback_packet(packet, nullptr);
}

void HandleSubtype05ResourceBuildPacket(const Mode1ReliablePacket& packet) {
    handle_resource_build_packet(packet, nullptr);
}

void HandleSubtype06UnitCommandBitPacket(const Mode1ReliablePacket& packet) {
    handle_unit_command_bit_packet(packet, nullptr);
}

void HandleSubtype07UnitDeathMarkPacket(const Mode1ReliablePacket& packet) {
    handle_unit_death_mark_packet(packet, nullptr);
}

void HandleSubtype08UnitAuxVectorPacket(const Mode1ReliablePacket& packet) {
    handle_unit_aux_vector_packet(packet, nullptr);
}

void HandleSubtype09ExtendedUnitOrderPacket(const Mode1ReliablePacket& packet) {
    handle_extended_unit_order_packet(packet, nullptr);
}

void HandleSubtype0aForcedOrder21Packet(const Mode1ReliablePacket& packet) {
    handle_forced_order21_packet(packet, nullptr);
}

void HandleSubtype0bUnitStatusMaskPacket(const Mode1ReliablePacket& packet) {
    handle_unit_status_mask_packet(packet, nullptr);
}

void HandleSubtype0cPlacementResourcePacket(const Mode1ReliablePacket& packet) {
    handle_placement_resource_packet(packet, nullptr);
}

void DispatchSubtype0dNestedGameplayCommand(const Mode1ReliablePacket& packet) {
    handle_nested_subtype13_packet(packet, nullptr);
}

void HandleSubtype13PlayerInactivePacket(const Mode1ReliablePacket& packet) {
    handle_player_inactive_packet(packet, nullptr);
}

void HandleSubtype14PlayerRelationPacket(const Mode1ReliablePacket& packet) {
    handle_player_relation_packet(packet, nullptr);
}

void HandleSubtype15PlayerConsensusPacket(const Mode1ReliablePacket& packet) {
    handle_player_consensus_packet(packet, nullptr);
}

void HandleSubtype16ModalPausePacket(const Mode1ReliablePacket& packet) {
    handle_modal_pause_packet(packet, nullptr);
}

void HandleSubtype1aProductionCostPacket(const Mode1ReliablePacket& packet) {
    handle_production_cost_packet(packet, nullptr);
}

bool DispatchMode1GameplayPacket(const Mode1ReliablePacket& packet) {
    // The original handlers read DAT_00725100, the live gameplay owner, not
    // the reliable transport channel.  They normally hold the same 0..7
    // value, but replay playback publishes the no-local-player owner 9 while
    // its recorded reliable channel remains a real player slot.  Keeping the
    // transport value here makes subtype 0x15 treat an observer as that
    // recorded player and incorrectly disable a participant.
    g_packet_dispatch_state.local_player_index =
        player_slot_state().local_player_slot;
    const u8 subtype = packet.size > 0x0f ? packet.bytes[0x0f] : packet.subtype;
    g_packet_dispatch_state.last_subtype = subtype;
    ++g_packet_dispatch_state.total_dispatches;
    if (subtype >= kMode1GameplayPacketSubtypeCount) {
        g_packet_dispatch_state.missing_handler_seen = true;
        return false;
    }

    ++g_packet_dispatch_state.dispatch_counts[subtype];
    Mode1GameplayPacketHandler handler = g_packet_dispatch_state.handlers[subtype];
    if (handler == nullptr) {
        g_packet_dispatch_state.missing_handler_seen = true;
        return false;
    }

    handler(packet, g_packet_dispatch_state.user_data);
    return true;
}

void HandleSubtype19PendingUnitActionPacket(const Mode1ReliablePacket& packet) {
    const u32 unit_offset = read_packet_u32(packet, 0x10);
    Mode1GameplayUnitPacketState& unit =
        unit_state_for(unit_offset, packet_channel(packet));
    std::array<u32, 4> payload{};
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = read_packet_u32(packet,
            static_cast<u32>(0x14 + index * sizeof(u32)));
    }

    const bool has_text = packet.size > 0x14 && packet.bytes[0x14] != 0;
    if (!has_text) {
        clear_unit_pending_string_slot(unit_offset, unit);
        unit.pending_action_payload = {};
        unit.pending_action_text = {};
        unit.action_pending = false;
        unit.pending_action_value = 0;
        return;
    }

    clear_unit_pending_string_slot(unit_offset, unit);

    unit.pending_action_payload = payload;
    unit.pending_action_text = {};
    std::size_t out = 0;
    for (std::size_t i = 0x14; i < packet.size && i < packet.bytes.size() &&
         out + 1 < unit.pending_action_text.size(); ++i) {
        const char value = static_cast<char>(packet.bytes[i]);
        if (value == '\0') {
            break;
        }
        unit.pending_action_text[out++] = value;
    }

    const u32 runtime_slot = intern_unit_pending_string_slot(unit_offset,
        unit.pending_action_text.data());
    if (runtime_slot != 0 && runtime_slot != 0xffffffffu) {
        unit.pending_string_slot = runtime_slot;
    } else if (g_packet_dispatch_state.runtime_callbacks.intern_unit_string == nullptr) {
        unit.pending_string_slot = allocate_packet_string_slot_fallback();
    } else {
        unit.pending_action_payload = {};
        unit.pending_action_text = {};
        unit.action_pending = false;
        unit.pending_action_value = 0;
        return;
    }
    unit.action_pending = true;
    unit.pending_action_value = unit.pending_string_slot;
    ++g_packet_dispatch_state.string_slot_intern_packets;
}

bool QueueCurrentPacketDeferredUnitCommand(const Mode1ReliablePacket& packet,
    u32 command) {
    const PacketFields fields = packet_fields(packet);
    Mode1GameplayUnitPacketState& unit = unit_state_for(fields.unit_offset,
        fields.channel);
    Mode1GameplayCommandRecord record =
        command_record(command & 0x7fffffffu, fields.mode, fields.arg1, fields.arg2);
    return push_deferred_command(unit, record);
}

bool QueueDeferredUnitCommandRecord(u32 unit_offset, u8 owner,
    const Mode1GameplayCommandRecord& record) {
    Mode1GameplayUnitPacketState& unit = unit_state_for(unit_offset, owner);
    return push_deferred_command(unit, record);
}

void HandleSubtype1dVoteCompletionPacket(const Mode1ReliablePacket& packet) {
    const PacketFields fields = packet_fields(packet);
    if (!g_packet_dispatch_state.vote_completion_gate_open ||
        fields.command != g_packet_dispatch_state.local_player_index ||
        fields.channel >= g_packet_dispatch_state.vote_completion_seen.size()) {
        return;
    }

    g_packet_dispatch_state.vote_completion_seen[fields.channel] = 1;
    for (u32 player = 0; player < g_packet_dispatch_state.players.size(); ++player) {
        const u8 status = g_packet_dispatch_state.players[player].status;
        if (status != 1 && status != 0x14 &&
            g_packet_dispatch_state.vote_completion_seen[player] == 0) {
            return;
        }
    }
    g_packet_dispatch_state.session_complete_requested = true;
}

void HandleSubtype0fCatchupTargetPacket(const Mode1ReliablePacket& packet) {
    const u32 raw_value = read_packet_u32(packet, 0x10);
    g_packet_dispatch_state.catchup_target_enabled = raw_value != 0;
    g_packet_dispatch_state.catchup_target_value = raw_value;
    if (g_packet_dispatch_state.runtime_callbacks.apply_catchup_target != nullptr) {
        g_packet_dispatch_state.runtime_callbacks.apply_catchup_target(
            g_packet_dispatch_state.runtime_user_data, raw_value,
            raw_value != 0);
    }
}

Mode1ReliablePacket BuildMode1GameplayPacket(u32 packed_opcode, u32 arg0,
    u32 unit_offset, u32 arg1, u32 arg2, u32 arg3) {
    Mode1ReliablePacket packet{};
    packet.size = kMode1ReliablePacketBytes;
    packet.channel = static_cast<u8>(packed_opcode & 0xffu);
    packet.subtype = static_cast<u8>((packed_opcode >> 24) & 0xffu);
    packet.sequence = GetMode1ReliableExpectedSequence(packet.channel);
    write_packet_u32(packet, 0x00, 1);
    write_packet_u32(packet, 0x04, kMode1ReliablePacketBytes);
    write_packet_u32(packet, 0x08, packet.sequence);
    write_packet_u32(packet, 0x0c, packed_opcode);
    write_packet_u32(packet, 0x10, arg0);
    write_packet_u32(packet, 0x14, unit_offset);
    write_packet_u32(packet, 0x18, arg1);
    write_packet_u32(packet, 0x1c, arg2);
    write_packet_u32(packet, 0x20, arg3);
    return packet;
}

Mode1ReliablePacket build_mode1_broadcast_gameplay_packet(u32 packed_opcode, u32 arg0,
    u32 unit_offset, u32 arg1, u32 arg2, u32 arg3) {
    Mode1ReliablePacket packet{};
    packet.size = kMode1ReliablePacketBytes;
    packet.channel = static_cast<u8>(packed_opcode & 0xffu);
    packet.subtype = static_cast<u8>((packed_opcode >> 24) & 0xffu);
    write_packet_u32(packet, 0x00, 1);
    write_packet_u32(packet, 0x04, kMode1ReliablePacketBytes);
    write_packet_u32(packet, 0x0c, packed_opcode);
    write_packet_u32(packet, 0x10, arg0);
    write_packet_u32(packet, 0x14, unit_offset);
    write_packet_u32(packet, 0x18, arg1);
    write_packet_u32(packet, 0x1c, arg2);
    write_packet_u32(packet, 0x20, arg3);
    return packet;
}

bool PublishLocalMode1GameplayPacket(u32 packed_opcode, u32 arg0,
    u32 unit_offset, u32 arg1, u32 arg2, u32 arg3) {
    Mode1ReliablePacket packet =
        BuildMode1GameplayPacket(packed_opcode, arg0, unit_offset, arg1, arg2, arg3);
    const bool accepted =
        AcceptMode1OrderedPacket(packet.bytes.data(), kMode1ReliablePacketBytes);
    if (accepted) {
        MarkMode1ReliableLocalBroadcastEnd(packet.sequence + 1);
    }
    ++g_packet_dispatch_state.published_local_packets;
    if (accepted && should_flush_published_packet_range() &&
        mode1_reliable_state().local_broadcast_start <
            mode1_reliable_state().local_broadcast_end) {
        BroadcastMode1PacketRange(mode1_reliable_state().local_broadcast_start,
            mode1_reliable_state().local_broadcast_end - 1);
        mode1_reliable_state().local_broadcast_start =
            mode1_reliable_state().local_broadcast_end;
    }
    return accepted;
}

bool PublishLocalMode1GameplayPacketPreserveResult(u32 packed_opcode, u32 arg0,
    u32 unit_offset, u32 arg1, u32 arg2, u32 arg3) {
    return PublishLocalMode1GameplayPacket(packed_opcode, arg0, unit_offset,
        arg1, arg2, arg3);
}

bool BroadcastMode1GameplayPacket(u32 packed_opcode, u32 arg0, u32 unit_offset,
    u32 arg1, u32 arg2, u32 arg3) {
    Mode1ReliablePacket packet =
        build_mode1_broadcast_gameplay_packet(
            packed_opcode, arg0, unit_offset, arg1, arg2, arg3);
    ++g_packet_dispatch_state.broadcast_packets;
    return BroadcastMode1ReliablePayload(
        packet.bytes.data(), kMode1ReliablePacketBytes) >= 0;
}

}
