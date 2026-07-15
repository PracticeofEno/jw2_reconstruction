#include "ranker_reconstructed_code/src/ranker_winmain.cpp"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "UPGRADE_COMPLETION_SOUND_MATRIX_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void reset_fixture() {
    g_runtime.gameplay_sound = {};
    g_runtime.gameplay_sound.bank_loaded = true;
    g_runtime.gameplay_sound.listener_position_offset = 0;
    g_runtime.gameplay_hud_alert_markers = {};
    g_runtime.gameplay_hud_text = {};
    g_runtime.gameplay_production_catalog = {};
    g_runtime.gameplay_startup_state.owner_faction_ids = {};
}

void test_all_faction_slots_and_local_owner_gate() {
    for (u32 faction = 0; faction < 4; ++faction) {
        reset_fixture();
        UnitCommandContext context{};
        context.local_owner_id = 2;
        UnitMovementUnit unit{};
        unit.owner_id = 2;
        unit.x = 1234;
        unit.y = 5678;
        g_runtime.gameplay_startup_state.owner_faction_ids[2] = faction;

        default_unit_command_completion_announcement(context, unit);

        require(direct_sound_state().current_slot_index == 0x1du + faction,
            "local completion chose the wrong faction sound slot");
        require(direct_sound_state().last_volume == 0 &&
                direct_sound_state().last_pan == 0,
            "completion sound did not use zero world delta/pan");
        require(g_runtime.gameplay_hud_alert_markers.markers[0].active &&
                g_runtime.gameplay_hud_alert_markers.markers[0].kind == 1 &&
                g_runtime.gameplay_hud_alert_markers.markers[0].world_x == 1234 &&
                g_runtime.gameplay_hud_alert_markers.markers[0].world_y == 5678,
            "local completion marker contract changed");
    }

    reset_fixture();
    SetCurrentDirectSoundBufferSlotIndex(0x2ffu);
    UnitCommandContext context{};
    context.local_owner_id = 1;
    UnitMovementUnit remote{};
    remote.owner_id = 2;
    remote.x = 77;
    remote.y = 88;
    g_runtime.gameplay_startup_state.owner_faction_ids[2] = 3;
    default_unit_command_completion_announcement(context, remote);
    require(direct_sound_state().current_slot_index == 0x2ffu,
        "remote upgrade completion emitted local audio");
    for (const GameplayHudAlertMarker& marker :
         g_runtime.gameplay_hud_alert_markers.markers) {
        require(!marker.active,
            "remote upgrade completion emitted a local marker");
    }
}

} // namespace

int main() {
    test_all_faction_slots_and_local_owner_gate();
    std::cout << "UPGRADE_COMPLETION_SOUND_MATRIX_PASS "
                 "factions=0..3 slots=29..32 volume=0 pan=0 "
                 "local=marker1 remote=silent\n";
    return EXIT_SUCCESS;
}
