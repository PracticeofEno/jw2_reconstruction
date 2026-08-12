#include "ranker_visual_animation.h"

#include <algorithm>
#include <unordered_map>

namespace ranker {
namespace {

constexpr u32 kInvalidEntry = 0xffffffffu;

struct VisualAnimationTransitionKeyHash {
    std::size_t operator()(const VisualAnimationTransitionKey& key) const {
        std::size_t hash = key.runtime_slot_index;
        const auto mix = [&hash](u32 value) {
            hash ^= static_cast<std::size_t>(value) +
                static_cast<std::size_t>(0x9e3779b9u) + (hash << 6) + (hash >> 2);
        };
        mix(key.type_id);
        mix(key.sequence);
        mix(key.image_group);
        mix(key.direction_row);
        mix(key.draw_kind);
        mix(key.resource_draw_mode);
        mix(key.flipped ? 1u : 0u);
        return hash;
    }
};

struct VisualAnimationTransitionState {
    u32 source_entry = kInvalidEntry;
    u32 target_entry = kInvalidEntry;
    u64 source_allocation_serial = 0;
    u64 target_allocation_serial = 0;
    u32 simulation_frame = 0;
    bool initialized = false;
};

std::unordered_map<VisualAnimationTransitionKey,
    VisualAnimationTransitionState, VisualAnimationTransitionKeyHash>
    g_visual_animation_transitions;

VisualAnimationTransitionSelection endpoint_selection(
    u32 entry, u64 allocation_serial, u32 subframe) {
    VisualAnimationTransitionSelection result{};
    result.endpoint_entry = entry;
    result.source_entry = entry;
    result.target_entry = entry;
    result.source_allocation_serial = allocation_serial;
    result.target_allocation_serial = allocation_serial;
    result.subframe_index = subframe;
    return result;
}

} // namespace

VisualAnimationTransitionSelection ResolveVisualAnimationTransition(
    const VisualAnimationTransitionKey& key, u32 current_entry,
    u64 current_allocation_serial, u32 simulation_frame, u32 alpha_16_16) {
    const u32 subframe = QuantizeVisualAnimationSubframe(alpha_16_16);
    if (current_entry == kInvalidEntry || current_allocation_serial == 0 ||
        key.runtime_slot_index == 0 || key.runtime_slot_index == 0xffffffffu) {
        return endpoint_selection(current_entry, current_allocation_serial, subframe);
    }

    VisualAnimationTransitionState& state = g_visual_animation_transitions[key];
    if (!state.initialized) {
        state.source_entry = current_entry;
        state.target_entry = current_entry;
        state.source_allocation_serial = current_allocation_serial;
        state.target_allocation_serial = current_allocation_serial;
        state.simulation_frame = simulation_frame;
        state.initialized = true;
        return endpoint_selection(current_entry, current_allocation_serial, subframe);
    }

    if (state.simulation_frame != simulation_frame) {
        const bool consecutive = simulation_frame == state.simulation_frame + 1u;
        const bool prior_target_valid =
            state.target_entry != kInvalidEntry &&
            state.target_allocation_serial != 0;
        if (consecutive && prior_target_valid) {
            state.source_entry = state.target_entry;
            state.source_allocation_serial = state.target_allocation_serial;
        }
        else {
            state.source_entry = current_entry;
            state.source_allocation_serial = current_allocation_serial;
        }
        state.target_entry = current_entry;
        state.target_allocation_serial = current_allocation_serial;
        state.simulation_frame = simulation_frame;
    }
    else if (state.target_entry != current_entry ||
        state.target_allocation_serial != current_allocation_serial) {
        // More than one body layer reused the same transition key in a single
        // simulation frame.  Snap instead of blending unrelated resources.
        state.source_entry = current_entry;
        state.target_entry = current_entry;
        state.source_allocation_serial = current_allocation_serial;
        state.target_allocation_serial = current_allocation_serial;
    }

    if (state.source_entry == state.target_entry &&
        state.source_allocation_serial == state.target_allocation_serial) {
        return endpoint_selection(
            state.target_entry, state.target_allocation_serial, subframe);
    }
    if (subframe == 0) {
        return endpoint_selection(
            state.source_entry, state.source_allocation_serial, subframe);
    }
    if (subframe >= kVisualAnimationIntervalCount) {
        return endpoint_selection(
            state.target_entry, state.target_allocation_serial, subframe);
    }

    VisualAnimationTransitionSelection result{};
    result.source_entry = state.source_entry;
    result.target_entry = state.target_entry;
    result.source_allocation_serial = state.source_allocation_serial;
    result.target_allocation_serial = state.target_allocation_serial;
    result.subframe_index = subframe;
    result.interpolate = true;
    return result;
}

void ResetVisualAnimationTransitionCache() {
    g_visual_animation_transitions.clear();
}

std::size_t VisualAnimationTransitionCacheSize() {
    return g_visual_animation_transitions.size();
}

} // namespace ranker
