#include "ranker_p2p_flight_recorder.h"

#include "ranker_replay.h"
#include "ranker_replay_dialogs.h"
#include "ranker_unit_action.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ranker {
namespace {

P2PFlightRecorderState g_p2p_flight_recorder;
constexpr u32 kScenarioUnitSlotCount = 0x800;
constexpr u32 kScenarioUnitSlotStride = 0x1d0;
constexpr u32 kEffectSlotStride = 0xa8;

std::string capture_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04d%02d%02d_%02d%02d%02d",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);
    return text;
}

std::filesystem::path capture_directory() {
    std::error_code error;
    std::filesystem::path directory =
        std::filesystem::current_path(error) / "Replays";
    if (error) {
        directory = "Replays";
    }
    std::filesystem::create_directories(directory, error);
    return directory;
}

const char* capture_trigger_name(P2PDropCaptureTrigger trigger) {
    switch (trigger) {
    case P2PDropCaptureTrigger::remote_player_inactive:
        return "remote_player_inactive";
    case P2PDropCaptureTrigger::corrective_checksum:
    default:
        return "corrective_checksum";
    }
}

std::vector<const P2PFlightFrameRecord*> ordered_frames(
    const P2PFlightRecorderState& state) {
    const u32 count = std::min<u32>(state.captured_frame_count,
        kP2PFlightFrameCount);
    std::vector<const P2PFlightFrameRecord*> result;
    result.reserve(count);
    const u32 first = state.captured_frame_count > kP2PFlightFrameCount ?
        state.captured_frame_count % kP2PFlightFrameCount : 0;
    for (u32 index = 0; index < count; ++index) {
        result.push_back(&state.frames[(first + index) % kP2PFlightFrameCount]);
    }
    return result;
}

bool write_trace_csv(const std::filesystem::path& path,
    const P2PFlightRecorderState& state,
    const P2PSyncMismatchCaptureInfo& mismatch) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    file << "# ranker_rebuild P2P synchronization flight trace\n";
    file << "# trigger=" << capture_trigger_name(mismatch.trigger)
         << ",detected_frame=" << mismatch.detected_frame
         << ",remote_channel=" << mismatch.remote_channel
         << ",sequence=" << mismatch.sequence
         << ",local_checksum=" << mismatch.local_checksum
         << ",remote_checksum=" << mismatch.remote_checksum << "\n";
    file << "# frames\n";
    file << "simulation_frame,rng_seed,unit_identity_checksum,"
            "unit_position_checksum,effect_checksum,combined_checksum,"
            "active_unit_count,active_effect_count\n";
    for (const P2PFlightFrameRecord* frame : ordered_frames(state)) {
        if (frame == nullptr) {
            continue;
        }
        file << frame->simulation_frame << ',' << frame->gameplay_rng_seed << ','
             << frame->unit_identity_checksum << ','
             << frame->unit_position_checksum << ',' << frame->effect_checksum
             << ',' << frame->combined_checksum << ',' << frame->units.size()
             << ',' << frame->effects.size() << '\n';
    }

    file << "# units\n";
    file << "simulation_frame,rng_seed,active_order,id,runtime_slot,type,owner,"
            "command_state,previous_command_state,command_flags,command_bit_mask,"
            "script_bit_flags,draw_flags,animation_flags,definition_footprint_flags,"
            "runtime_flags,"
            "movement_flags,direction,animation_frame,movement_state,turn_ticks,"
            "step_accumulator,x,y,destination_x,destination_y,path_x,path_y,"
            "next_path_x,next_path_y,current_cell_x,current_cell_y,target_id,"
            "health,max_health,command_value,action_mode,work_timer,lockout_ticks\n";

    for (const P2PFlightFrameRecord* frame : ordered_frames(state)) {
        if (frame == nullptr) {
            continue;
        }
        for (const P2PFlightUnitRecord& unit : frame->units) {
            file << frame->simulation_frame << ',' << frame->gameplay_rng_seed << ','
                 << unit.active_order << ',' << unit.id << ','
                 << unit.runtime_slot_index << ',' << unit.type_id << ','
                 << unit.owner_id << ',' << unit.command_state << ','
                 << unit.previous_command_state << ',' << unit.command_flags << ','
                 << unit.command_bit_mask << ',' << unit.script_bit_flags << ','
                 << unit.draw_flags << ',' << unit.animation_flags << ','
                 << unit.definition_footprint_flags << ','
                 << unit.runtime_flags << ',' << unit.movement_flags << ','
                 << unit.direction << ',' << unit.animation_frame << ','
                 << unit.movement_state << ',' << unit.movement_turn_ticks << ','
                 << unit.movement_step_accumulator << ',' << unit.x << ',' << unit.y
                 << ',' << unit.destination_x << ',' << unit.destination_y << ','
                 << unit.path_target_x << ',' << unit.path_target_y << ','
                 << unit.next_path_x << ',' << unit.next_path_y << ','
                 << unit.current_cell_x << ',' << unit.current_cell_y << ','
                 << unit.target_id << ',' << unit.health << ',' << unit.max_health
                 << ',' << unit.command_value << ',' << unit.action_mode << ','
                 << unit.work_timer << ',' << unit.command_lockout_ticks << '\n';
        }
    }

    file << "# effects\n";
    file << "simulation_frame,rng_seed,active_order,slot_index,effect_id,flags,"
            "tick,effect_frame,amount,source_unit_id,target_unit_id,linked_unit_id,"
            "x,y,target_x,target_y,previous_x,previous_y,direction,chain_remaining,"
            "hit_unit_count,initial_impact_applied\n";
    for (const P2PFlightFrameRecord* frame : ordered_frames(state)) {
        if (frame == nullptr) {
            continue;
        }
        for (const P2PFlightEffectRecord& effect : frame->effects) {
            file << frame->simulation_frame << ',' << frame->gameplay_rng_seed << ','
                 << effect.active_order << ',' << effect.slot_index << ','
                 << effect.effect_id << ',' << effect.flags << ',' << effect.tick
                 << ',' << effect.frame << ',' << effect.amount << ','
                 << effect.source_unit_id << ',' << effect.target_unit_id << ','
                 << effect.linked_unit_id << ',' << effect.x << ',' << effect.y
                 << ',' << effect.target_x << ',' << effect.target_y << ','
                 << effect.previous_x << ',' << effect.previous_y << ','
                 << effect.direction << ',' << effect.chain_remaining << ','
                 << effect.hit_unit_count << ','
                 << (effect.initial_impact_applied ? 1 : 0) << '\n';
        }
    }
    return file.good();
}

} // namespace

P2PFlightRecorderState& p2p_flight_recorder_state() {
    return g_p2p_flight_recorder;
}

void ResetP2PFlightRecorder() {
    for (P2PFlightFrameRecord& frame : g_p2p_flight_recorder.frames) {
        frame.simulation_frame = 0;
        frame.gameplay_rng_seed = 0;
        frame.unit_identity_checksum = 0;
        frame.unit_position_checksum = 0;
        frame.effect_checksum = 0;
        frame.combined_checksum = 0;
        frame.units.clear();
        frame.effects.clear();
    }
    g_p2p_flight_recorder.captured_frame_count = 0;
    g_p2p_flight_recorder.drop_capture_attempted = false;
    g_p2p_flight_recorder.replay_saved = false;
    g_p2p_flight_recorder.trace_saved = false;
    g_p2p_flight_recorder.last_replay_path.clear();
    g_p2p_flight_recorder.last_trace_path.clear();
}

void CaptureP2PFlightFrame(u32 simulation_frame, u32 gameplay_rng_seed,
    const UnitMovementContext& movement, const UnitEffectRuntimeState& effects) {
    P2PFlightRecorderState& state = g_p2p_flight_recorder;
    if (state.drop_capture_attempted) {
        return;
    }

    P2PFlightFrameRecord& frame =
        state.frames[state.captured_frame_count % kP2PFlightFrameCount];
    frame.simulation_frame = simulation_frame;
    frame.gameplay_rng_seed = gameplay_rng_seed;
    frame.unit_identity_checksum = 0;
    frame.unit_position_checksum = 0;
    frame.effect_checksum = 0;
    frame.units.clear();
    frame.units.reserve(movement.active_units.size());

    for (std::size_t index = 0; index < movement.active_units.size(); ++index) {
        const UnitMovementUnit* unit = movement.active_units[index];
        if (unit == nullptr || !unit->active) {
            continue;
        }
        P2PFlightUnitRecord record{};
        record.active_order = static_cast<u32>(index);
        record.id = unit->id;
        record.runtime_slot_index = unit->runtime_slot_index;
        record.type_id = unit->type_id;
        record.owner_id = unit->owner_id;
        record.command_state = unit->command_state;
        record.previous_command_state = unit->previous_command_state;
        record.command_flags = unit->command_flags;
        const u32 command_bit_bytes = std::min<u32>(4,
            static_cast<u32>(unit->command_bits.size()));
        for (u32 byte = 0; byte < command_bit_bytes; ++byte) {
            record.command_bit_mask |=
                static_cast<u32>(unit->command_bits[byte]) << (byte * 8u);
        }
        record.script_bit_flags = unit->script_bit_flags;
        record.draw_flags = unit->draw_flags;
        record.animation_flags = unit->scenario_string_slot;
        record.definition_footprint_flags = unit->definition.footprint_flags;
        record.runtime_flags = unit->runtime_flags;
        record.movement_flags = unit->movement_flags;
        record.direction = unit->direction;
        record.animation_frame = unit->animation_frame;
        record.movement_state = unit->movement_state;
        record.movement_turn_ticks = unit->movement_turn_ticks;
        record.movement_step_accumulator = unit->movement_step_accumulator;
        record.x = unit->x;
        record.y = unit->y;
        record.destination_x = unit->destination_x;
        record.destination_y = unit->destination_y;
        record.path_target_x = unit->path_target_x;
        record.path_target_y = unit->path_target_y;
        record.next_path_x = unit->next_path_x;
        record.next_path_y = unit->next_path_y;
        record.current_cell_x = unit->current_cell_x;
        record.current_cell_y = unit->current_cell_y;
        record.target_id = unit->target != nullptr ? unit->target->id : 0;
        record.health = unit->health;
        record.max_health = unit->max_health;
        record.command_value = unit->command_value;
        record.action_mode = unit->action_mode;
        record.work_timer = unit->work_timer;
        record.command_lockout_ticks = unit->command_lockout_ticks;
        frame.units.push_back(record);

        if (unit->runtime_slot_index != 0 &&
            unit->runtime_slot_index != kInvalidUnitRuntimeSlotIndex &&
            unit->runtime_slot_index < kScenarioUnitSlotCount) {
            const u32 offset =
                unit->runtime_slot_index * kScenarioUnitSlotStride;
            frame.unit_identity_checksum += unit->direction + offset + 1u;
            frame.unit_position_checksum +=
                static_cast<u32>(unit->x) + static_cast<u32>(unit->y);
        }
    }

    frame.effects.clear();
    frame.effects.reserve(effects.active_effect_indices.size());
    for (std::size_t order = 0; order < effects.active_effect_indices.size(); ++order) {
        const std::size_t slot = effects.active_effect_indices[order];
        if (slot >= effects.effect_slots.size()) {
            continue;
        }
        const UnitEffectRuntime& effect = effects.effect_slots[slot];
        P2PFlightEffectRecord record{};
        record.active_order = static_cast<u32>(order);
        record.slot_index = static_cast<u32>(slot);
        record.effect_id = effect.effect_id;
        record.flags = effect.flags;
        record.tick = effect.tick;
        record.frame = effect.frame;
        record.amount = effect.amount;
        record.source_unit_id = effect.source_unit_id;
        record.target_unit_id = effect.target_unit_id;
        record.linked_unit_id = effect.linked_unit_id;
        record.x = effect.x;
        record.y = effect.y;
        record.target_x = effect.target_x;
        record.target_y = effect.target_y;
        record.previous_x = effect.previous_x;
        record.previous_y = effect.previous_y;
        record.direction = effect.direction;
        record.chain_remaining = effect.chain_remaining;
        record.hit_unit_count = static_cast<u32>(effect.hit_unit_ids.size());
        record.initial_impact_applied = effect.initial_impact_applied;
        frame.effects.push_back(record);

        const u32 offset =
            (static_cast<u32>(slot) + 1u) * kEffectSlotStride;
        frame.effect_checksum += static_cast<u32>(effect.x) +
            static_cast<u32>(effect.y) + offset;
    }
    frame.combined_checksum = frame.unit_position_checksum +
        frame.unit_identity_checksum + frame.gameplay_rng_seed;
    ++state.captured_frame_count;
}

bool PersistP2PDropCapture(const P2PSyncMismatchCaptureInfo& mismatch,
    ReplayRecordingState& replay) {
    P2PFlightRecorderState& state = g_p2p_flight_recorder;
    if (state.drop_capture_attempted || replay.playback_mode) {
        return state.replay_saved || state.trace_saved;
    }
    state.drop_capture_attempted = true;

    const std::string stem = "P2PDrop_" + capture_timestamp() + "_" +
        capture_trigger_name(mismatch.trigger) + "_f" +
        std::to_string(mismatch.detected_frame) + "_s" +
        std::to_string(mismatch.sequence);
    const std::filesystem::path directory = capture_directory();
    const std::filesystem::path replay_path = directory / (stem + ".ply");
    const std::filesystem::path trace_path = directory / (stem + ".sync.csv");

    state.trace_saved = write_trace_csv(trace_path, state, mismatch);
    if (state.trace_saved) {
        state.last_trace_path = trace_path.string();
    }
    state.replay_saved = SaveReplayRecordingArchive(replay_path.string().c_str(), replay);
    if (state.replay_saved) {
        state.last_replay_path = replay_path.string();
    }
    return state.replay_saved || state.trace_saved;
}

} // namespace ranker
