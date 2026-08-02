#pragma once

#include "ranker_types.h"
#include "ranker_unit_movement.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

struct ReplayRecordingState;
struct UnitEffectRuntimeState;

constexpr u32 kP2PFlightFrameCount = 512;

struct P2PFlightUnitRecord {
    u32 active_order = 0;
    u32 id = 0;
    u32 runtime_slot_index = 0;
    u32 type_id = 0;
    u32 owner_id = 0;
    u32 command_state = 0;
    u32 previous_command_state = 0;
    u32 command_flags = 0;
    u32 runtime_flags = 0;
    u32 movement_flags = 0;
    u32 direction = 0;
    u32 animation_frame = 0;
    u32 movement_state = 0;
    u32 movement_turn_ticks = 0;
    u32 movement_step_accumulator = 0;
    i32 x = 0;
    i32 y = 0;
    i32 destination_x = 0;
    i32 destination_y = 0;
    i32 path_target_x = 0;
    i32 path_target_y = 0;
    i32 next_path_x = 0;
    i32 next_path_y = 0;
    i32 current_cell_x = 0;
    i32 current_cell_y = 0;
    u32 target_id = 0;
    u32 health = 0;
    u32 max_health = 0;
    u32 command_value = 0;
    u32 action_mode = 0;
    u32 work_timer = 0;
    u32 command_lockout_ticks = 0;
};

struct P2PFlightEffectRecord {
    u32 active_order = 0;
    u32 slot_index = 0;
    u32 effect_id = 0;
    u32 flags = 0;
    u32 tick = 0;
    u32 frame = 0;
    u32 amount = 0;
    u32 source_unit_id = 0;
    u32 target_unit_id = 0;
    u32 linked_unit_id = 0;
    i32 x = 0;
    i32 y = 0;
    i32 target_x = 0;
    i32 target_y = 0;
    i32 previous_x = 0;
    i32 previous_y = 0;
    u32 direction = 0;
    u32 chain_remaining = 0;
    u32 hit_unit_count = 0;
    bool initial_impact_applied = false;
};

struct P2PFlightFrameRecord {
    u32 simulation_frame = 0;
    u32 gameplay_rng_seed = 0;
    u32 unit_identity_checksum = 0;
    u32 unit_position_checksum = 0;
    u32 effect_checksum = 0;
    u32 combined_checksum = 0;
    std::vector<P2PFlightUnitRecord> units;
    std::vector<P2PFlightEffectRecord> effects;
};

struct P2PSyncMismatchCaptureInfo {
    u32 detected_frame = 0;
    u32 remote_channel = 0;
    u32 sequence = 0;
    u32 local_checksum = 0;
    u32 remote_checksum = 0;
};

struct P2PFlightRecorderState {
    std::array<P2PFlightFrameRecord, kP2PFlightFrameCount> frames{};
    u32 captured_frame_count = 0;
    bool drop_capture_attempted = false;
    bool replay_saved = false;
    bool trace_saved = false;
    std::string last_replay_path;
    std::string last_trace_path;
};

P2PFlightRecorderState& p2p_flight_recorder_state();
void ResetP2PFlightRecorder();
void CaptureP2PFlightFrame(u32 simulation_frame, u32 gameplay_rng_seed,
    const UnitMovementContext& movement, const UnitEffectRuntimeState& effects);
bool PersistP2PDropCapture(const P2PSyncMismatchCaptureInfo& mismatch,
    ReplayRecordingState& replay);

} // namespace ranker
