#include "ranker_control_group_persistence.h"

#include "ranker_ui_overlay.h"
#include "ranker_unit_movement.h"

#include <algorithm>

namespace ranker {
namespace {

u32 find_overlay_control_group_for_unit(
    const UiOverlayState& state, u32 unit_id) {
    const u32 group_count = std::min<u32>(
        static_cast<u32>(state.control_groups.size()),
        kOriginalUnitControlGroupMask + 1u);
    for (u32 group = 1; group < group_count; ++group) {
        const auto& ids = state.control_groups[group].unit_ids;
        if (std::find(ids.begin(), ids.end(), unit_id) != ids.end()) {
            return group;
        }
    }
    return 0;
}

} // namespace

bool InitializeUiOverlayControlGroupsFromUnitFlagsOnce(
    UiOverlayState& state, const std::vector<UnitMovementUnit*>& active_units) {
    if (state.control_groups_initialized_from_unit_flags) {
        return false;
    }

    for (UiOverlayControlGroup& group : state.control_groups) {
        group.unit_ids.clear();
    }

    for (const UnitMovementUnit* unit : active_units) {
        if (unit == nullptr || !unit->active || unit->id == 0) {
            continue;
        }
        const u32 group = unit->scenario_string_slot & kOriginalUnitControlGroupMask;
        if (group == 0 || group >= state.control_groups.size()) {
            continue;
        }

        auto& ids = state.control_groups[group].unit_ids;
        if (std::find(ids.begin(), ids.end(), unit->id) == ids.end()) {
            ids.push_back(unit->id);
        }
    }

    state.control_groups_initialized_from_unit_flags = true;
    state.control_groups_dirty_for_unit_flags = false;
    return true;
}

void MirrorUiOverlayControlGroupsToUnitFlags(
    const UiOverlayState& state, const std::vector<UnitMovementUnit*>& active_units) {
    if (!state.control_groups_initialized_from_unit_flags) {
        return;
    }

    for (UnitMovementUnit* unit : active_units) {
        if (unit == nullptr || !unit->active || unit->id == 0) {
            continue;
        }
        const u32 group = find_overlay_control_group_for_unit(state, unit->id);
        unit->scenario_string_slot =
            (unit->scenario_string_slot & ~kOriginalUnitControlGroupMask) | group;
    }
}

void SynchronizeUiOverlayControlGroupsWithUnitFlags(
    UiOverlayState& state, const std::vector<UnitMovementUnit*>& active_units) {
    InitializeUiOverlayControlGroupsFromUnitFlagsOnce(state, active_units);
    MirrorUiOverlayControlGroupsToUnitFlags(state, active_units);
    state.control_groups_dirty_for_unit_flags = false;
}

bool ForgetUiOverlayUnitIdentityForNewGeneration(
    UiOverlayState& state, u32 unit_id) {
    if (unit_id == 0) {
        return false;
    }

    bool removed = false;
    for (UiOverlayControlGroup& group : state.control_groups) {
        const std::size_t before = group.unit_ids.size();
        group.unit_ids.erase(std::remove(
            group.unit_ids.begin(), group.unit_ids.end(), unit_id),
            group.unit_ids.end());
        removed = removed || group.unit_ids.size() != before;
    }

    const std::size_t before_selected = state.selected_unit_ids.size();
    state.selected_unit_ids.erase(std::remove(state.selected_unit_ids.begin(),
        state.selected_unit_ids.end(), unit_id), state.selected_unit_ids.end());
    removed = removed || state.selected_unit_ids.size() != before_selected;
    state.selected_unit_count = static_cast<u32>(state.selected_unit_ids.size());
    if (state.selected_unit_id == unit_id) {
        state.selected_unit_id = 0;
        state.selected_unit_type = 0;
        state.selected_unit_owner = 0;
        removed = true;
    }
    return removed;
}

} // namespace ranker
