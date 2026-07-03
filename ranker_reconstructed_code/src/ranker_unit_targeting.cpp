#include "ranker_unit_targeting.h"

#include "ranker_unit_movement.h"

#include <algorithm>
#include <limits>

namespace ranker {
namespace {

bool is_target_inactive(const UnitRecord& unit) {
    return (unit.command_state & kUnitStateDead) != 0 ||
        (unit.runtime_flags & kUnitRuntimeHiddenOrInactive) != 0;
}

i32 center_x(const UnitRecord& unit) {
    if (unit.bounds_right > unit.bounds_left) {
        return unit.bounds_left + ((unit.bounds_right - unit.bounds_left) >> 1);
    }
    return unit.x;
}

i32 center_y(const UnitRecord& unit) {
    if (unit.bounds_bottom > unit.bounds_top) {
        return unit.bounds_top + ((unit.bounds_bottom - unit.bounds_top) >> 1);
    }
    return unit.y;
}

bool point_inside_source_bounds(const UnitRecord& source, const UnitRecord& candidate) {
    const bool has_interaction_bounds =
        source.interaction_bounds_right > source.interaction_bounds_left ||
        source.interaction_bounds_bottom > source.interaction_bounds_top;
    const i32 left = has_interaction_bounds
        ? source.interaction_bounds_left : source.bounds_left;
    const i32 top = has_interaction_bounds
        ? source.interaction_bounds_top : source.bounds_top;
    const i32 right = has_interaction_bounds
        ? source.interaction_bounds_right : source.bounds_right;
    const i32 bottom = has_interaction_bounds
        ? source.interaction_bounds_bottom : source.bounds_bottom;
    return left <= candidate.x && candidate.x <= right &&
        top <= candidate.y && candidate.y <= bottom;
}

u32 owner_bit(u32 owner_id) {
    return owner_id < 32 ? (1u << owner_id) : 0;
}

u32 axis_delta(i32 a, i32 b) {
    const i64 delta = static_cast<i64>(a) - static_cast<i64>(b);
    return static_cast<u32>(delta < 0 ? -delta : delta);
}

u32 active_list_tiebreaker(const UnitRecord& source, const UnitRecord& candidate,
    u32 distance) {
    const u32 minor_axis = std::min(axis_delta(source.x, candidate.x),
        axis_delta(source.y, candidate.y));
    return distance + (minor_axis >> 2);
}

u32 candidate_priority(UnitTargetingContext& context, const UnitRecord& candidate) {
    if (context.callbacks.priority != nullptr && context.source != nullptr) {
        return context.callbacks.priority(context, *context.source, candidate);
    }
    return candidate.target_priority;
}

struct CandidateMetrics {
    u32 distance = std::numeric_limits<u32>::max();
    u32 tiebreaker = std::numeric_limits<u32>::max();
};

CandidateMetrics candidate_metrics(UnitTargetingContext& context,
    const UnitRecord& candidate, bool active_list_scan) {
    CandidateMetrics metrics{};
    if (context.source == nullptr) {
        return metrics;
    }
    if (context.callbacks.path_distance != nullptr) {
        metrics.distance =
            context.callbacks.path_distance(context, *context.source, candidate);
        metrics.tiebreaker = metrics.distance;
        return metrics;
    }

    metrics.distance = CalculateApproxUnitDistance(context.source->x,
        context.source->y, candidate.x, candidate.y);
    metrics.tiebreaker = active_list_scan
        ? active_list_tiebreaker(*context.source, candidate, metrics.distance)
        : metrics.distance;
    return metrics;
}

bool can_consider_candidate(UnitTargetingContext& context, UnitRecord& candidate,
    u32 skip_mask) {
    if (context.source == nullptr || context.source == &candidate) {
        return false;
    }
    if (is_target_inactive(candidate) || (candidate.runtime_flags & skip_mask) != 0) {
        return false;
    }
    if ((context.owner_relation_mask & owner_bit(candidate.owner_id)) != 0) {
        return false;
    }
    if (context.callbacks.can_target != nullptr &&
        !context.callbacks.can_target(context, *context.source, candidate)) {
        return false;
    }
    return true;
}

void maybe_accept_candidate(UnitTargetingContext& context, UnitRecord& candidate,
    const CandidateMetrics& metrics) {
    const u32 priority = candidate_priority(context, candidate);
    UnitTargetingResult& result = context.result;
    if (priority < result.priority ||
        (priority == result.priority && metrics.tiebreaker <= result.tiebreaker)) {
        result.target = &candidate;
        result.priority = priority;
        result.tiebreaker = metrics.tiebreaker;
        result.distance = metrics.distance;
    }
}

UnitTargetingResult find_best_in_list(UnitTargetingContext& context,
    const std::vector<UnitRecord*>& units, u32 skip_mask, bool require_range,
    bool active_list_scan) {
    context.result = {};
    context.result.priority = std::numeric_limits<u32>::max();
    context.result.tiebreaker = std::numeric_limits<u32>::max();
    context.result.distance = std::numeric_limits<u32>::max();

    const u32 range = context.interaction_range != 0
        ? context.interaction_range
        : (context.source != nullptr ? GetUnitInteractionRange(*context.source) : 0);

    for (UnitRecord* unit : units) {
        if (unit == nullptr || !can_consider_candidate(context, *unit, skip_mask)) {
            continue;
        }
        const CandidateMetrics metrics =
            candidate_metrics(context, *unit, active_list_scan);
        if (require_range && metrics.distance > range) {
            continue;
        }
        maybe_accept_candidate(context, *unit, metrics);
    }
    return context.result;
}

} // namespace

u32 GetUnitInteractionRange(const UnitRecord& source) {
    return source.interaction_range;
}

u32 CalculateUnitCenterPathDistance(const UnitRecord& source,
    const UnitRecord& target, const UnitDirectionLookupTable& lookup) {
    const UnitMovementPoint source_center{center_x(source), center_y(source)};
    const UnitMovementPoint target_center{center_x(target), center_y(target)};
    return CalculatePointDirectionFromLookup(source_center, target_center, lookup);
}

UnitTargetingResult FindBestUnitTargetUsingSpatialIndex(UnitTargetingContext& context) {
    return find_best_in_list(context, context.spatial_candidates,
        kUnitTargetSpatialSkipMask, false, false);
}

UnitTargetingResult FindBestUnitTargetByActiveList(UnitTargetingContext& context) {
    return find_best_in_list(context, context.active_units,
        kUnitTargetActiveListSkipMask, true, true);
}

UnitTargetingResult FindUnitTargetInsideSourceBounds(UnitTargetingContext& context) {
    context.result = {};
    context.result.priority = std::numeric_limits<u32>::max();
    context.result.tiebreaker = std::numeric_limits<u32>::max();
    context.result.distance = std::numeric_limits<u32>::max();
    if (context.source == nullptr) {
        return context.result;
    }

    for (UnitRecord* unit : context.active_units) {
        if (unit == nullptr ||
            !can_consider_candidate(context, *unit, kUnitTargetBoundsSkipMask)) {
            continue;
        }
        if (!point_inside_source_bounds(*context.source, *unit)) {
            continue;
        }
        const CandidateMetrics metrics = candidate_metrics(context, *unit, false);
        context.result.target = unit;
        context.result.priority = candidate_priority(context, *unit);
        context.result.tiebreaker = metrics.tiebreaker;
        context.result.distance = metrics.distance;
        return context.result;
    }
    return context.result;
}

} // namespace ranker
