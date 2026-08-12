#pragma once

#include "ranker_types.h"

#include <cstddef>

namespace ranker {

// A twelve-phase pose bank exactly represents the phase locations needed when
// a 60 Hz visual stream is presented at 144 Hz (720 Hz common clock). Ranker's
// authoritative simulation normally advances at a configurable 45 ms or
// slower interval, so runtime selection uses that real transition alpha rather
// than pretending sprite poses arrive once per 60 Hz display refresh.
constexpr u32 kVisualAnimationIntervalCount = 12;
constexpr u32 kVisualAnimationIntermediateFrameCount =
    kVisualAnimationIntervalCount - 1;
constexpr u32 kVisualAnimationInterpolationOne = 0x10000u;

constexpr bool ShouldInterpolateVisualAnimation(u32 render_target_fps) {
    return render_target_fps > 60u;
}

struct VisualAnimationTransitionKey {
    u32 runtime_slot_index = 0;
    u32 type_id = 0;
    u32 sequence = 0;
    u32 image_group = 0;
    u32 direction_row = 0;
    u32 draw_kind = 0;
    u32 resource_draw_mode = 0;
    bool flipped = false;

    bool operator==(const VisualAnimationTransitionKey& other) const {
        return runtime_slot_index == other.runtime_slot_index &&
            type_id == other.type_id && sequence == other.sequence &&
            image_group == other.image_group &&
            direction_row == other.direction_row &&
            draw_kind == other.draw_kind &&
            resource_draw_mode == other.resource_draw_mode &&
            flipped == other.flipped;
    }
};

struct VisualAnimationTransitionSelection {
    u32 endpoint_entry = 0xffffffffu;
    u32 source_entry = 0xffffffffu;
    u32 target_entry = 0xffffffffu;
    u64 source_allocation_serial = 0;
    u64 target_allocation_serial = 0;
    u32 subframe_index = 0;
    bool interpolate = false;
};

constexpr u32 QuantizeVisualAnimationSubframe(u32 alpha_16_16) {
    if (alpha_16_16 >= kVisualAnimationInterpolationOne) {
        return kVisualAnimationIntervalCount;
    }
    return static_cast<u32>(
        (static_cast<u64>(alpha_16_16) * kVisualAnimationIntervalCount +
            kVisualAnimationInterpolationOne / 2u) /
        kVisualAnimationInterpolationOne);
}

constexpr u32 VisualAnimationSixtyHzPhaseForPresentationFrame(
    u64 presentation_frame) {
    return static_cast<u32>((presentation_frame * 5u) %
        kVisualAnimationIntervalCount);
}

VisualAnimationTransitionSelection ResolveVisualAnimationTransition(
    const VisualAnimationTransitionKey& key, u32 current_entry,
    u64 current_allocation_serial, u32 simulation_frame, u32 alpha_16_16);
void ResetVisualAnimationTransitionCache();
std::size_t VisualAnimationTransitionCacheSize();

} // namespace ranker
