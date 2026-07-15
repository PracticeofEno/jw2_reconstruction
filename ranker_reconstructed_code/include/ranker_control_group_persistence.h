#pragma once

#include "ranker_types.h"

#include <vector>

namespace ranker {

struct UiOverlayState;
struct UnitMovementUnit;

constexpr u32 kOriginalUnitControlGroupMask = 0x0fu;

// Raw unit +0x08 is the original persistent source for control-group
// membership.  Digit recall and Ctrl+digit walk the complete active list, so
// this import deliberately preserves entries from every owner.  (Only the
// separate `~` cycle scan filters candidates by local owner.)  Import exactly
// once after a session's units have been materialized; after that point an
// empty overlay group is an intentional clear and must not be repopulated
// from stale raw flags.
bool InitializeUiOverlayControlGroupsFromUnitFlagsOnce(
    UiOverlayState& state, const std::vector<UnitMovementUnit*>& active_units);

// Once the one-time import has completed, publish the overlay's authoritative
// group membership back to raw +0x08 without disturbing the selected bit
// (0x80) or any of the other scenario/runtime flags in that DWORD.
void MirrorUiOverlayControlGroupsToUnitFlags(
    const UiOverlayState& state, const std::vector<UnitMovementUnit*>& active_units);

// Session initialization calls the import explicitly.  This combined helper
// is also safe at render-time and supplies a fallback for focused harnesses
// that enter rendering without running the normal session initializer.
void SynchronizeUiOverlayControlGroupsWithUnitFlags(
    UiOverlayState& state, const std::vector<UnitMovementUnit*>& active_units);

// A fixed-pool address/id denotes a new unit generation after it is popped
// from the free list. Remove the previous occupant from UI identity state
// before the new unit can be mirrored back into raw +0x08.
bool ForgetUiOverlayUnitIdentityForNewGeneration(
    UiOverlayState& state, u32 unit_id);

} // namespace ranker
