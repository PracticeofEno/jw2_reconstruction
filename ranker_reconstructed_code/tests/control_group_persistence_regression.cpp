#include "ranker_control_group_persistence.h"

#include "ranker_ui_overlay.h"
#include "ranker_unit_movement.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "control-group persistence regression failed at line " << line
              << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fail(#expression, __LINE__); \
        } \
    } while (false)

bool contains(const UiOverlayControlGroup& group, u32 unit_id) {
    return std::find(group.unit_ids.begin(), group.unit_ids.end(), unit_id) !=
        group.unit_ids.end();
}

UnitMovementUnit make_unit(u32 id, u32 raw_flags) {
    UnitMovementUnit unit{};
    unit.id = id;
    unit.active = true;
    unit.scenario_string_slot = raw_flags;
    return unit;
}

void test_load_import_preserves_raw_group_and_other_flag_bits() {
    UiOverlayState overlay{};
    UnitMovementUnit a = make_unit(101u, 0x00000181u);
    UnitMovementUnit b = make_unit(102u, 0x00000203u);
    UnitMovementUnit c = make_unit(103u, 0x00000400u);
    a.owner_id = 0u;
    b.owner_id = 7u;
    c.owner_id = 9u;
    std::vector<UnitMovementUnit*> units{&a, &b, &c};

    REQUIRE(InitializeUiOverlayControlGroupsFromUnitFlagsOnce(overlay, units));
    REQUIRE(overlay.control_groups_initialized_from_unit_flags);
    REQUIRE(contains(overlay.control_groups[1], 101u));
    // FUN_004e74e2's digit-recall walk has no per-unit owner comparison, so a
    // loaded remote-owner nibble remains represented too.  The scan-0x29 `~`
    // cycle applies its local-owner filter later, in CycleSelectedControlGroup.
    REQUIRE(contains(overlay.control_groups[3], 102u));
    REQUIRE(overlay.control_groups[0].unit_ids.empty());

    MirrorUiOverlayControlGroupsToUnitFlags(overlay, units);
    REQUIRE(a.scenario_string_slot == 0x00000181u);
    REQUIRE(b.scenario_string_slot == 0x00000203u);
    REQUIRE(c.scenario_string_slot == 0x00000400u);
}

void test_intentional_clear_is_not_resurrected_by_a_second_import() {
    UiOverlayState overlay{};
    UnitMovementUnit a = make_unit(201u, 0x91u);
    UnitMovementUnit b = make_unit(202u, 0x11u);
    std::vector<UnitMovementUnit*> units{&a, &b};

    REQUIRE(InitializeUiOverlayControlGroupsFromUnitFlagsOnce(overlay, units));
    overlay.control_groups[1].unit_ids.clear();
    MirrorUiOverlayControlGroupsToUnitFlags(overlay, units);

    REQUIRE(a.scenario_string_slot == 0x90u);
    REQUIRE(b.scenario_string_slot == 0x10u);
    REQUIRE(!InitializeUiOverlayControlGroupsFromUnitFlagsOnce(overlay, units));
    REQUIRE(overlay.control_groups[1].unit_ids.empty());
    REQUIRE((a.scenario_string_slot & kOriginalUnitControlGroupMask) == 0u);
    REQUIRE((b.scenario_string_slot & kOriginalUnitControlGroupMask) == 0u);
}

void test_assignment_moves_units_and_clears_the_replaced_group() {
    UiOverlayState overlay{};
    UnitMovementUnit old_target = make_unit(301u, 0x21u);
    UnitMovementUnit selected = make_unit(302u, 0xa3u);
    UnitMovementUnit untouched = make_unit(303u, 0x45u);
    old_target.owner_id = 7u;
    selected.owner_id = 0u;
    untouched.owner_id = 7u;
    std::vector<UnitMovementUnit*> units{&old_target, &selected, &untouched};

    SynchronizeUiOverlayControlGroupsWithUnitFlags(overlay, units);
    overlay.control_groups[1].unit_ids.clear();
    overlay.control_groups[3].unit_ids.clear();
    overlay.control_groups[1].unit_ids.push_back(selected.id);
    MirrorUiOverlayControlGroupsToUnitFlags(overlay, units);

    // FUN_004e74b9 clears the replaced nibble across the complete active list
    // after the one primary-selection local-owner gate, including this remote
    // owner.  An unrelated remote group remains untouched.
    REQUIRE(old_target.scenario_string_slot == 0x20u);
    REQUIRE(selected.scenario_string_slot == 0xa1u);
    REQUIRE(untouched.scenario_string_slot == 0x45u);
}

void test_session_reset_reimports_saved_membership_once() {
    UnitMovementUnit saved = make_unit(401u, 0xc7u);
    std::vector<UnitMovementUnit*> units{&saved};

    UiOverlayState first_session{};
    SynchronizeUiOverlayControlGroupsWithUnitFlags(first_session, units);
    first_session.control_groups[7].unit_ids.clear();
    first_session.control_groups[2].unit_ids.push_back(saved.id);
    MirrorUiOverlayControlGroupsToUnitFlags(first_session, units);
    REQUIRE(saved.scenario_string_slot == 0xc2u);

    // A mode-5 load/rejoin materializes raw +0x08 before ResetUiOverlayState.
    // A fresh overlay therefore imports the saved group on its first sync.
    UiOverlayState rejoined_session{};
    REQUIRE(InitializeUiOverlayControlGroupsFromUnitFlagsOnce(
        rejoined_session, units));
    REQUIRE(contains(rejoined_session.control_groups[2], saved.id));
    REQUIRE(!contains(rejoined_session.control_groups[7], saved.id));
}

void test_fixed_pool_generation_forgets_dead_unit_identity() {
    UiOverlayState overlay{};
    overlay.control_groups_initialized_from_unit_flags = true;
    overlay.control_groups[4].unit_ids = {501u};
    overlay.selected_unit_ids = {501u};
    overlay.selected_unit_id = 501u;
    overlay.selected_unit_type = 0x20u;
    overlay.selected_unit_owner = 0u;
    overlay.selected_unit_count = 1u;

    REQUIRE(ForgetUiOverlayUnitIdentityForNewGeneration(overlay, 501u));
    REQUIRE(overlay.control_groups[4].unit_ids.empty());
    REQUIRE(overlay.selected_unit_ids.empty());
    REQUIRE(overlay.selected_unit_id == 0u);
    REQUIRE(overlay.selected_unit_type == 0u);
    REQUIRE(overlay.selected_unit_owner == 0u);
    REQUIRE(overlay.selected_unit_count == 0u);

    UnitMovementUnit replacement = make_unit(501u, 0x80u);
    std::vector<UnitMovementUnit*> active_units{&replacement};
    MirrorUiOverlayControlGroupsToUnitFlags(overlay, active_units);
    REQUIRE((replacement.scenario_string_slot &
        kOriginalUnitControlGroupMask) == 0u);
    REQUIRE((replacement.scenario_string_slot & 0x80u) != 0u);
}

} // namespace

int main() {
    test_load_import_preserves_raw_group_and_other_flag_bits();
    test_intentional_clear_is_not_resurrected_by_a_second_import();
    test_assignment_moves_units_and_clears_the_replaced_group();
    test_session_reset_reimports_saved_membership_once();
    test_fixed_pool_generation_forgets_dead_unit_identity();
    return EXIT_SUCCESS;
}
