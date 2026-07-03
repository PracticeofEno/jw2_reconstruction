#pragma once

#include "ranker_unit_damage.h"

#include <vector>

namespace ranker {

struct UnitDirectionLookupTable;

constexpr u32 kUnitTargetActiveListSkipMask = 0x20020084;
constexpr u32 kUnitTargetSpatialSkipMask = 0x84;
constexpr u32 kUnitTargetBoundsSkipMask = 0x20000084;

struct UnitTargetingContext;

using UnitTargetPredicate = bool (*)(UnitTargetingContext& context,
    const UnitRecord& source, const UnitRecord& candidate);
using UnitTargetDistanceCallback = u32 (*)(UnitTargetingContext& context,
    const UnitRecord& source, const UnitRecord& candidate);
using UnitTargetPriorityCallback = u32 (*)(UnitTargetingContext& context,
    const UnitRecord& source, const UnitRecord& candidate);

struct UnitTargetingCallbacks {
    UnitTargetPredicate can_target = nullptr;
    UnitTargetDistanceCallback path_distance = nullptr;
    UnitTargetPriorityCallback priority = nullptr;
};

struct UnitTargetingResult {
    UnitRecord* target = nullptr;
    u32 priority = 0xffffffff;
    u32 tiebreaker = 0xffffffff;
    u32 distance = 0xffffffff;
};

struct UnitTargetingContext {
    UnitTargetingCallbacks callbacks;
    UnitRecord* source = nullptr;
    std::vector<UnitRecord*> active_units;
    std::vector<UnitRecord*> spatial_candidates;
    UnitTargetingResult result;
    u32 owner_relation_mask = 0;
    u32 interaction_range = 0;
    bool source_has_forced_target_mode = false;
    bool prefer_point_targets = false;
};

u32 GetUnitInteractionRange(const UnitRecord& source);
u32 CalculateUnitCenterPathDistance(const UnitRecord& source,
    const UnitRecord& target, const UnitDirectionLookupTable& lookup);
UnitTargetingResult FindBestUnitTargetUsingSpatialIndex(UnitTargetingContext& context);
UnitTargetingResult FindBestUnitTargetByActiveList(UnitTargetingContext& context);
UnitTargetingResult FindUnitTargetInsideSourceBounds(UnitTargetingContext& context);

}
