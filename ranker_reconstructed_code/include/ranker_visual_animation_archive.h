#pragma once

#include "ranker_types.h"

#include <cstddef>
#include <string>

namespace ranker {

constexpr u32 kVisualAnimationArchiveIntervalCount = 12;
constexpr u32 kVisualAnimationArchiveIntermediateFrameCount = 11;

enum class VisualAnimationArchiveStatus {
    unloaded,
    loaded,
    file_unavailable,
    invalid_archive,
    source_mismatch,
};

struct VisualAnimationArchiveState {
    VisualAnimationArchiveStatus status = VisualAnimationArchiveStatus::unloaded;
    std::string archive_path;
    std::string source_path;
    std::string detail;
    u32 transition_count = 0;
    u64 payload_bytes = 0;
    u64 cache_bytes = 0;
    u32 cache_entries = 0;
    u64 lookup_hits = 0;
    u64 lookup_misses = 0;
    u64 decompression_failures = 0;
};

struct VisualAnimationArchiveFrameView {
    i32 left = 0;
    i32 top = 0;
    u32 width = 0;
    u32 height = 0;
    const u8* payload = nullptr;
    std::size_t payload_size = 0;
    i32 flip_origin_x = 0;
    i32 flip_delta_y = 0;
    bool flipped = false;
};

// Loads only the header and transition directory. Compressed frame data stays
// on disk and is decompressed into a bounded cache when a transition is drawn.
// Passing an empty source path skips source verification for focused tests.
bool LoadVisualAnimationArchive(
    const std::string& archive_path, const std::string& source_path);
void UnloadVisualAnimationArchive();
void ResetVisualAnimationArchiveCache();

bool FindVisualAnimationArchiveFrame(u32 unit_type, u32 image_group,
    u32 source_frame, u32 target_frame, u32 subframe_index, bool flipped,
    VisualAnimationArchiveFrameView& result);

const VisualAnimationArchiveState& visual_animation_archive_state();

} // namespace ranker
