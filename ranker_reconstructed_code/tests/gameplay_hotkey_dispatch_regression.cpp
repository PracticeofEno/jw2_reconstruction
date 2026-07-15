#include "ranker_control_group_persistence.h"
#include "ranker_ui_overlay.h"
#include "ranker_unit_movement.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace ranker {

// DispatchGameplayUiKeyboardInput contains the original minimap-toggle branch,
// which keeps ConfigureGameplayUiOverlayLayout reachable even though this
// focused test never selects it.  Rendering hardware is intentionally outside
// this test, so provide its sole retained backend query locally.
bool SurfacePixelMode555() {
    return false;
}

} // namespace ranker

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "gameplay hotkey dispatch regression failed at line " << line
              << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fail(#expression, __LINE__); \
        } \
    } while (false)

u32 pause_menu_call_count = 0;
u32 save_menu_call_count = 0;
u32 load_menu_call_count = 0;
u32 click_sound_call_count = 0;

void count_pause_menu_open(UiOverlayState&) {
    ++pause_menu_call_count;
}

void count_save_menu_open(UiOverlayState&) {
    ++save_menu_call_count;
}

void count_load_menu_open(UiOverlayState&) {
    ++load_menu_call_count;
}

void count_click_sound(UiOverlayState&) {
    ++click_sound_call_count;
}

void test_w_keydown_activates_exactly_once_and_wm_char_does_not_repeat() {
    UiOverlayState state{};
    constexpr u32 kCommandId = 0xd4u;
    AppendUiOverlayCommandSlot(state, kCommandId, 7u, 0u, 'W');

    REQUIRE(state.hot_regions.size() == 1u);
    REQUIRE(state.hot_regions.front().hotkey == 'W');

    DispatchGameplayUiKeyboardInput(state, 0x11u, 0u); // WM_KEYDOWN W
    REQUIRE(state.command_actions.size() == 1u);
    REQUIRE(state.command_actions.front().item_id == kCommandId);
    REQUIRE(state.command_actions.front().aux == 7u);
    REQUIRE(state.last_hotkey_command == kCommandId);

    DispatchGameplayUiKeyboardInput(state, 0u, 'W'); // matching WM_CHAR
    REQUIRE(state.command_actions.size() == 1u);
}

void test_flag_one_offscreen_record_is_not_keyboard_active() {
    UiOverlayState state{};
    constexpr u32 kDefinitionId = 9u;
    state.unit_definition_icon_markers.resize(kDefinitionId + 1u);
    state.unit_definition_icon_markers[kDefinitionId] = 'W';

    QueueUnitDefinitionDynamicIconOrHotRegion(
        state, kDefinitionId, 3u, 0x01u);

    REQUIRE(state.hot_regions.size() == 1u);
    REQUIRE(state.hot_regions.front().record.icon_marker == 0u);
    REQUIRE(state.hot_regions.front().hotkey == 0u);

    DispatchGameplayUiKeyboardInput(state, 0x11u, 0u);
    REQUIRE(state.command_actions.empty());
    REQUIRE(state.last_hotkey_command == 0u);
}

void test_chat_scan_escape_is_ignored_and_wm_char_escape_closes() {
    UiOverlayState state{};
    state.chat_active = true;
    state.chat_cursor_visible = true;
    state.chat_input_text = "message";

    DispatchGameplayUiKeyboardInput(state, 0x01u, 0u); // WM_KEYDOWN Escape
    REQUIRE(state.chat_active);
    REQUIRE(state.chat_cursor_visible);
    REQUIRE(state.chat_input_text == "message");

    DispatchGameplayUiKeyboardInput(state, 0u, 0x1bu); // WM_CHAR Escape
    REQUIRE(!state.chat_active);
    REQUIRE(!state.chat_cursor_visible);
    REQUIRE(state.chat_input_text.empty());
}

void test_f10_opens_pause_menu_once_without_mutating_selected_stats() {
    UiOverlayState state{};
    state.selected_unit_health = 23u;
    state.selected_unit_secondary = 41u;
    state.callbacks.open_pause_menu = count_pause_menu_open;
    pause_menu_call_count = 0;

    DispatchGameplayUiKeyboardInput(state, 0x44u, 0u); // F10

    REQUIRE(pause_menu_call_count == 1u);
    REQUIRE(state.selected_unit_health == 23u);
    REQUIRE(state.selected_unit_secondary == 41u);
    REQUIRE(state.command_actions.empty());
}

void test_f2_f3_bookmarks_only_in_generic_ai_profile_mode() {
    UiOverlayState generic{};
    generic.generic_ai_profile_mode = true;
    generic.shift_modifier_down = true;
    generic.camera_x = 111;
    generic.camera_y = 222;
    generic.callbacks.open_save_session_dialog = count_save_menu_open;
    generic.callbacks.open_load_session_dialog = count_load_menu_open;
    save_menu_call_count = 0;
    load_menu_call_count = 0;

    DispatchGameplayUiKeyboardInput(generic, 0x3cu, 0u); // F2, bookmark 1
    REQUIRE(generic.camera_bookmarks[1].valid);
    REQUIRE(generic.camera_bookmarks[1].camera_x == 111);
    REQUIRE(generic.camera_bookmarks[1].camera_y == 222);

    generic.camera_x = 333;
    generic.camera_y = 444;
    DispatchGameplayUiKeyboardInput(generic, 0x3du, 0u); // F3, bookmark 2
    REQUIRE(generic.camera_bookmarks[2].valid);
    REQUIRE(generic.camera_bookmarks[2].camera_x == 333);
    REQUIRE(generic.camera_bookmarks[2].camera_y == 444);
    REQUIRE(save_menu_call_count == 0u);
    REQUIRE(load_menu_call_count == 0u);

    generic.shift_modifier_down = false;
    generic.camera_max_x = 10;
    generic.camera_max_y = 20;
    generic.camera_x = 0;
    generic.camera_y = 0;
    DispatchGameplayUiKeyboardInput(generic, 0x3cu, 0u); // raw recall, no clamp
    REQUIRE(generic.camera_x == 111);
    REQUIRE(generic.camera_y == 222);

    UiOverlayState scenario_only{};
    scenario_only.scenario_ai_profile_override = true;
    scenario_only.callbacks.open_save_session_dialog = count_save_menu_open;
    scenario_only.callbacks.open_load_session_dialog = count_load_menu_open;
    DispatchGameplayUiKeyboardInput(scenario_only, 0x3cu, 0u);
    DispatchGameplayUiKeyboardInput(scenario_only, 0x3du, 0u);
    REQUIRE(save_menu_call_count == 1u);
    REQUIRE(load_menu_call_count == 1u);
    REQUIRE(!scenario_only.camera_bookmarks[1].valid);
    REQUIRE(!scenario_only.camera_bookmarks[2].valid);

    UiOverlayState ordinary{};
    ordinary.callbacks.open_save_session_dialog = count_save_menu_open;
    ordinary.callbacks.open_load_session_dialog = count_load_menu_open;
    DispatchGameplayUiKeyboardInput(ordinary, 0x3cu, 0u);
    DispatchGameplayUiKeyboardInput(ordinary, 0x3du, 0u);
    REQUIRE(save_menu_call_count == 2u);
    REQUIRE(load_menu_call_count == 2u);
}

void test_escape_cancels_placement_or_activates_first_back_record() {
    UiOverlayState placement{};
    placement.placement_mode = 6u;
    placement.placement_definition_id = 0x82u;
    placement.staged_unit_action_id = 4u;
    placement.selected_production_category = 3u;
    placement.command_slot_count = 2u;
    placement.context_cursor.animation_mode = 7u;
    placement.callbacks.play_click_sound = count_click_sound;
    click_sound_call_count = 0;

    DispatchGameplayUiKeyboardInput(placement, 0x01u, 0u);
    REQUIRE(placement.placement_mode == 0u);
    REQUIRE(placement.staged_unit_action_id == 0xffffffffu);
    REQUIRE(placement.selected_production_category == 0u);
    REQUIRE(placement.command_slot_count == 0u);
    REQUIRE(placement.context_cursor.animation_mode == 0u);
    REQUIRE(placement.command_actions.empty());
    REQUIRE(click_sound_call_count == 1u);

    UiOverlayState category{};
    category.callbacks.play_click_sound = count_click_sound;
    AppendUiOverlayCommandSlot(category, 0xc6u, 5u, 0u);
    DispatchGameplayUiKeyboardInput(category, 0x01u, 0u);
    REQUIRE(category.command_actions.size() == 1u);
    REQUIRE(category.command_actions.front().item_id == 0xc6u);
    REQUIRE(category.command_actions.front().aux == 5u);
    REQUIRE(category.last_hotkey_command == 0xc6u);
    REQUIRE(click_sound_call_count == 2u);

    UiOverlayState blocked{};
    blocked.callbacks.play_click_sound = count_click_sound;
    AppendUiOverlayCommandSlot(blocked, 0xc6u, 3u, 0x02u);
    AppendUiOverlayCommandSlot(blocked, 0xc6u, 4u, 0u);
    DispatchGameplayUiKeyboardInput(blocked, 0x01u, 0u);
    REQUIRE(blocked.command_actions.empty());
    REQUIRE(blocked.last_hotkey_command == 0xc6u);
    REQUIRE(blocked.last_hotkey_aux == 3u);
    REQUIRE(click_sound_call_count == 2u);

    UiOverlayState alternate{};
    alternate.callbacks.play_click_sound = count_click_sound;
    AppendUiOverlayCommandSlot(alternate, 0xc6u, 6u, 0x04u);
    DispatchGameplayUiKeyboardInput(alternate, 0x01u, 0u);
    REQUIRE(alternate.command_actions.size() == 1u);
    REQUIRE(alternate.command_actions.front().item_id == 0xc6u);
    REQUIRE(alternate.command_actions.front().aux == 6u);
    REQUIRE(click_sound_call_count == 3u);

    UiOverlayState option_disabled{};
    UiOverlayCommandOption option{};
    option.item_id = 0xc6u;
    option.enabled = false;
    option_disabled.command_options.push_back(option);
    option_disabled.callbacks.play_click_sound = count_click_sound;
    AppendUiOverlayCommandSlot(option_disabled, 0xc6u, 1u, 0u);
    DispatchGameplayUiKeyboardInput(option_disabled, 0x01u, 0u);
    REQUIRE(option_disabled.command_actions.empty());
    REQUIRE(click_sound_call_count == 3u);
}

void test_f11_toggles_gameplay_debug_overlay_flag() {
    UiOverlayState state{};
    REQUIRE(!state.gameplay_overlay_flag);
    DispatchGameplayUiKeyboardInput(state, 0x57u, 0u);
    REQUIRE(state.gameplay_overlay_flag);
    DispatchGameplayUiKeyboardInput(state, 0x57u, 0u);
    REQUIRE(!state.gameplay_overlay_flag);
}

void test_speed_keys_follow_generic_profile_gate_and_boundaries() {
    UiOverlayState p2p{};
    p2p.generic_ai_profile_mode = true;
    p2p.game_speed = 4u;
    DispatchGameplayUiKeyboardInput(p2p, 0x0cu, 0u);
    DispatchGameplayUiKeyboardInput(p2p, 0x4eu, 0u);
    REQUIRE(p2p.game_speed == 4u);

    UiOverlayState scenario{};
    scenario.scenario_ai_profile_override = true;
    scenario.game_speed = 4u;
    DispatchGameplayUiKeyboardInput(scenario, 0x4au, 0u);
    REQUIRE(scenario.game_speed == 5u);
    DispatchGameplayUiKeyboardInput(scenario, 0x0du, 0u);
    REQUIRE(scenario.game_speed == 4u);

    UiOverlayState bounded{};
    bounded.max_game_speed = 3u;
    bounded.game_speed = 3u;
    DispatchGameplayUiKeyboardInput(bounded, 0x0cu, 0u);
    REQUIRE(bounded.game_speed == 3u);
    bounded.game_speed = 0u;
    DispatchGameplayUiKeyboardInput(bounded, 0x0du, 0u);
    REQUIRE(bounded.game_speed == 0u);
}

void test_loaded_control_groups_recall_and_cycle_through_dispatcher() {
    UiOverlayState state{};
    state.local_player_slot = 0u;

    UnitMovementUnit local_three{};
    local_three.id = 601u;
    local_three.active = true;
    local_three.owner_id = 0u;
    local_three.scenario_string_slot = 0x03u;
    UnitMovementUnit remote_four{};
    remote_four.id = 602u;
    remote_four.active = true;
    remote_four.owner_id = 7u;
    remote_four.scenario_string_slot = 0x04u;
    UnitMovementUnit local_five{};
    local_five.id = 603u;
    local_five.active = true;
    local_five.owner_id = 0u;
    local_five.scenario_string_slot = 0x05u;
    std::vector<UnitMovementUnit*> units{
        &local_three, &remote_four, &local_five};
    REQUIRE(InitializeUiOverlayControlGroupsFromUnitFlagsOnce(state, units));

    const auto append_minimap_unit = [&state](const UnitMovementUnit& source) {
        UiOverlayMinimapUnit unit{};
        unit.unit_id = source.id;
        unit.owner_id = source.owner_id;
        unit.selection_score = 1u;
        state.minimap_units.push_back(unit);
    };
    append_minimap_unit(local_three);
    append_minimap_unit(remote_four);
    append_minimap_unit(local_five);

    DispatchGameplayUiKeyboardInput(state, 0x04u, 0u); // digit 3
    REQUIRE(state.selected_unit_ids.size() == 1u);
    REQUIRE(state.selected_unit_id == local_three.id);

    // Scan 0x29 cycles only groups containing a local-owner unit, so remote
    // group four is skipped and group five becomes the next selection.
    DispatchGameplayUiKeyboardInput(state, 0x29u, 0u);
    REQUIRE(state.selected_unit_ids.size() == 1u);
    REQUIRE(state.selected_unit_id == local_five.id);
}

void test_control_group_recall_uses_active_order_exclusion_and_14_unit_cap() {
    UiOverlayState state{};
    std::array<UnitMovementUnit, 16> source_units{};
    std::vector<UnitMovementUnit*> active_units;
    active_units.reserve(source_units.size());
    for (std::size_t index = 0; index < source_units.size(); ++index) {
        UnitMovementUnit& source = source_units[index];
        source.id = 700u + static_cast<u32>(index);
        source.active = true;
        source.scenario_string_slot = 0x06u;
        active_units.push_back(&source);
    }
    REQUIRE(InitializeUiOverlayControlGroupsFromUnitFlagsOnce(
        state, active_units));

    // Publish the active-list view in a deliberately different order from
    // the imported assignment vector.  The first active unit is ineligible;
    // the next fourteen win and the final eligible unit is capped out.
    for (std::size_t reverse = source_units.size(); reverse != 0; --reverse) {
        UiOverlayMinimapUnit unit{};
        unit.unit_id = source_units[reverse - 1].id;
        unit.runtime_flags = reverse == source_units.size() ? 0x80u : 0u;
        if (reverse == 11u || reverse == 10u) {
            unit.selection_score = 99u;
        }
        state.minimap_units.push_back(unit);
    }

    DispatchGameplayUiKeyboardInput(state, 0x07u, 0u); // digit 6
    REQUIRE(state.selected_unit_ids.size() == 14u);
    REQUIRE(state.selected_unit_ids.front() == source_units[14].id);
    REQUIRE(state.selected_unit_ids.back() == source_units[1].id);
    REQUIRE(state.selected_unit_id == source_units[10].id);
    REQUIRE(std::find(state.selected_unit_ids.begin(),
                state.selected_unit_ids.end(), source_units[15].id) ==
        state.selected_unit_ids.end());
    REQUIRE(std::find(state.selected_unit_ids.begin(),
                state.selected_unit_ids.end(), source_units[0].id) ==
        state.selected_unit_ids.end());
}

void test_control_group_cycle_does_not_arm_digit_double_tap_focus() {
    UiOverlayState state{};
    state.local_player_slot = 0u;
    state.screen_width = 800u;
    state.screen_height = 600u;
    state.camera_max_x = 2000;
    state.camera_max_y = 2000;
    state.camera_x = 10;
    state.camera_y = 20;
    state.control_groups[1].unit_ids.push_back(801u);
    state.control_groups[2].unit_ids.push_back(802u);

    UiOverlayMinimapUnit first{};
    first.unit_id = 801u;
    first.owner_id = 0u;
    first.selection_score = 1u;
    first.world_x = 900;
    first.world_y = 900;
    UiOverlayMinimapUnit second = first;
    second.unit_id = 802u;
    second.world_x = 1200;
    second.world_y = 1200;
    state.minimap_units = {first, second};

    state.current_tick_ms = 1000u;
    DispatchGameplayUiKeyboardInput(state, 0x02u, 0u); // digit 1
    REQUIRE(state.last_control_group == 1u);
    REQUIRE(state.last_control_group_tick_ms == 1000u);

    state.current_tick_ms = 1200u;
    DispatchGameplayUiKeyboardInput(state, 0x29u, 0u); // cycle to group 2
    REQUIRE(state.selected_unit_id == 802u);
    REQUIRE(state.last_control_group == 1u);
    REQUIRE(state.last_control_group_tick_ms == 1000u);

    state.current_tick_ms = 1300u;
    DispatchGameplayUiKeyboardInput(state, 0x03u, 0u); // digit 2
    REQUIRE(state.camera_x == 10);
    REQUIRE(state.camera_y == 20);
}

void test_control_group_recall_preserves_null_primary_for_zero_scores() {
    UiOverlayState state{};
    state.control_groups[1].unit_ids.push_back(901u);
    UiOverlayMinimapUnit unit{};
    unit.unit_id = 901u;
    unit.type_id = 0x44u;
    unit.owner_id = 3u;
    unit.selection_score = 0u;
    state.minimap_units.push_back(unit);

    DispatchGameplayUiKeyboardInput(state, 0x02u, 0u);
    REQUIRE(state.selected_unit_ids.size() == 1u);
    REQUIRE(state.selected_unit_count == 1u);
    REQUIRE(state.selected_unit_id == 0u);
    REQUIRE(state.selected_unit_type == 0u);
    REQUIRE(state.selected_unit_owner == 0u);
    RecountGameplaySelectedUnits(state);
    REQUIRE(state.selected_unit_count == 1u);
    REQUIRE(state.selected_unit_id == 0u);
}

void test_control_group_recall_silently_cancels_placement_and_panel() {
    UiOverlayState state{};
    state.control_groups[2].unit_ids.push_back(911u);
    UiOverlayMinimapUnit unit{};
    unit.unit_id = 911u;
    unit.selection_score = 1u;
    state.minimap_units.push_back(unit);
    AppendUiOverlayCommandSlot(state, 0xd4u, 7u, 0u, 'W');
    state.placement_mode = 6u;
    state.placement_definition_id = 0x82u;
    state.placement_equipment_slot_code = 3u;
    state.staged_unit_action_id = 4u;
    state.selected_production_category = 5u;
    state.callbacks.play_click_sound = count_click_sound;
    click_sound_call_count = 0;

    DispatchGameplayUiKeyboardInput(state, 0x03u, 0u); // digit 2
    REQUIRE(state.selected_unit_id == 911u);
    REQUIRE(state.placement_mode == 0u);
    REQUIRE(state.placement_definition_id == 0u);
    REQUIRE(state.placement_equipment_slot_code == 0u);
    REQUIRE(state.staged_unit_action_id == 0xffffffffu);
    REQUIRE(state.selected_production_category == 0u);
    REQUIRE(state.hot_regions.empty());
    REQUIRE(state.queued_records.empty());
    REQUIRE(click_sound_call_count == 0u);
}

void test_control_group_cycle_scans_past_unselected_member_in_same_group() {
    UiOverlayState state{};
    state.local_player_slot = 0u;
    state.control_groups[1].unit_ids = {1001u, 1002u};
    state.control_groups[2].unit_ids = {1003u};
    for (u32 id = 1001u; id <= 1003u; ++id) {
        UiOverlayMinimapUnit unit{};
        unit.unit_id = id;
        unit.owner_id = 0u;
        unit.selection_score = 1u;
        state.minimap_units.push_back(unit);
    }
    state.selected_unit_ids = {1002u};
    state.selected_unit_id = 1002u;
    state.selected_unit_count = 1u;

    DispatchGameplayUiKeyboardInput(state, 0x29u, 0u);
    REQUIRE(state.selected_unit_ids.size() == 1u);
    REQUIRE(state.selected_unit_id == 1003u);
}

void test_control_group_assignment_publishes_raw_nibble_before_next_event() {
    UiOverlayState state{};
    state.local_player_slot = 0u;
    state.control_groups_initialized_from_unit_flags = true;
    state.control_group_assign_mode = true;
    state.selected_unit_ids = {1101u};
    state.selected_unit_id = 1101u;
    state.selected_unit_owner = 0u;
    state.selected_unit_count = 1u;
    state.control_groups[1].unit_ids = {1101u};

    UnitMovementUnit unit{};
    unit.id = 1101u;
    unit.active = true;
    unit.scenario_string_slot = 0x81u;
    std::vector<UnitMovementUnit*> active_units{&unit};

    DispatchGameplayUiKeyboardInput(state, 0x03u, 0u); // Ctrl+digit 2
    REQUIRE(state.control_groups_dirty_for_unit_flags);
    MirrorUiOverlayControlGroupsToUnitFlags(state, active_units);
    REQUIRE((unit.scenario_string_slot &
        kOriginalUnitControlGroupMask) == 2u);
    REQUIRE((unit.scenario_string_slot & 0x80u) != 0u);
}

} // namespace

int main() {
    test_w_keydown_activates_exactly_once_and_wm_char_does_not_repeat();
    test_flag_one_offscreen_record_is_not_keyboard_active();
    test_chat_scan_escape_is_ignored_and_wm_char_escape_closes();
    test_f10_opens_pause_menu_once_without_mutating_selected_stats();
    test_f2_f3_bookmarks_only_in_generic_ai_profile_mode();
    test_escape_cancels_placement_or_activates_first_back_record();
    test_f11_toggles_gameplay_debug_overlay_flag();
    test_speed_keys_follow_generic_profile_gate_and_boundaries();
    test_loaded_control_groups_recall_and_cycle_through_dispatcher();
    test_control_group_recall_uses_active_order_exclusion_and_14_unit_cap();
    test_control_group_cycle_does_not_arm_digit_double_tap_focus();
    test_control_group_recall_preserves_null_primary_for_zero_scores();
    test_control_group_recall_silently_cancels_placement_and_panel();
    test_control_group_cycle_scans_past_unselected_member_in_same_group();
    test_control_group_assignment_publishes_raw_nibble_before_next_event();
    return EXIT_SUCCESS;
}
