#pragma once

#include "ranker_replay.h"

#include <vector>

namespace ranker {

// Persists the finalized replay packet payload as the last TRC record and,
// when recording is active, writes the matching camera-position sidecar.
bool PersistReplayRecordingArchive(const char* output_path,
    const ReplayRecordingState& recording, const std::vector<u8>& payload);

} // namespace ranker
