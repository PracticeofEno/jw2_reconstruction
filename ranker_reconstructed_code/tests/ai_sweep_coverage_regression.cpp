#include "ranker_ai_commander.h"

#include <iostream>
#include <stdexcept>

using namespace ranker;
namespace {
void require(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
AiObservedUnit unit(u32 id, u32 type, i32 x, i32 y) {
    AiObservedUnit u;
    u.id = id; u.runtime_slot_index = id; u.type_id = type;
    u.owner_id = 0; u.controlled = true; u.visible = true; u.alive = true;
    u.x = x; u.y = y; u.health = u.max_health = 1000;
    u.type_flags = type < 0x60 ? 0x20 : 0;
    u.command_state = 1; u.attack_power = 30;
    u.attack_range_base = u.attack_range = 50; u.movement_step_limit = 6;
    return u;
}
AiObservation world() {
    AiObservation o;
    o.simulation_frame = 1; o.local_owner = 0; o.local_faction = 2;
    o.local_relation_mask = 1; o.map_width_tiles = o.map_height_tiles = 64;
    o.tiles.resize(64 * 64);
    for (auto& t : o.tiles) {
        t.passable = t.buildable = t.explored = t.visible = true;
    }
    o.start_x = o.start_y = 320;
    o.start_candidate_mask = 3;
    o.start_candidate_x[0] = o.start_candidate_y[0] = 320;
    o.start_candidate_x[1] = o.start_candidate_y[1] = 864;
    o.units = {unit(1, 0x80, 320, 320), unit(2, 0x22, 450, 450)};
    o.population_limit = 180;
    return o;
}
CommanderServices services() {
    CommanderServices s;
    s.autoscout = false;
    s.validator = [](const AiSemanticAction&) { return true; };
    return s;
}
void conceal_region(AiObservation& o) {
    for (u32 y = 14; y < 20; ++y) for (u32 x = 48; x < 54; ++x) {
        auto& t = o.tiles[y * 64 + x]; t.explored = t.visible = false;
    }
}
bool in_region(CommanderPoint p) {
    return p.valid && p.x / 32 >= 48 && p.x / 32 < 54 &&
           p.y / 32 >= 14 && p.y / 32 < 20;
}
void covers_ground_away_from_resources() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    const auto v = BuildCommanderView(s, o, services());
    require(in_region(v.anchors[12]), "search omitted reachable fog outside resource sites");
}
void start_slots_keep_priority() {
    auto o = world(); conceal_region(o);
    o.start_candidate_x[1] = o.start_candidate_y[1] = 54 * 32;
    o.tiles[54 * 64 + 54].visible = o.tiles[54 * 64 + 54].explored = false;
    CommanderState s;
    const auto v = BuildCommanderView(s, o, services());
    require(v.anchors[12].valid && v.anchors[12].x == 54 * 32 &&
            v.anchors[12].y == 54 * 32, "broad search displaced an unexplored start");
}
void target_persists_until_observed_then_advances() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12]; require(in_region(target), "missing initial fog target");
    o.units[1].x = 1800; o.units[1].y = 1800; o.simulation_frame = 65;
    v = BuildCommanderView(s, o, services());
    require(v.anchors[12].x == target.x && v.anchors[12].y == target.y,
            "moving squad repeatedly changed an unobserved search target");
    auto& t = o.tiles[(target.y / 32) * 64 + target.x / 32];
    t.visible = t.explored = true; o.simulation_frame = 73;
    v = BuildCommanderView(s, o, services());
    require(in_region(v.anchors[12]) &&
            (v.anchors[12].x != target.x || v.anchors[12].y != target.y),
            "search did not advance after observing its target");
}
void skips_disconnected_fog() {
    auto o = world(); conceal_region(o);
    for (u32 y = 13; y <= 20; ++y) for (u32 x = 47; x <= 54; ++x) {
        if (y == 13 || y == 20 || x == 47 || x == 54)
            o.tiles[y * 64 + x].passable = false;
    }
    CommanderState s;
    const auto v = BuildCommanderView(s, o, services());
    require(!v.anchors[12].valid, "search selected disconnected terrain");
}
void hidden_enemy_does_not_select_the_target() {
    auto o = world(); conceal_region(o);
    CommanderState a, b;
    const auto va = BuildCommanderView(a, o, services());
    auto hidden = unit(90, 0x60, 1664, 608);
    hidden.controlled = hidden.visible = false; hidden.owner_id = 1;
    o.units.push_back(hidden);
    const auto vb = BuildCommanderView(b, o, services());
    require(va.input.vector == vb.input.vector && va.input.map == vb.input.map &&
            va.mask == vb.mask, "hidden enemy changed search observation");
    require(vb.anchors[12].x == va.anchors[12].x && vb.anchors[12].y == va.anchors[12].y,
            "hidden enemy changed search target");
}
void known_building_keeps_priority() {
    auto o = world(); conceal_region(o);
    auto enemy = unit(90, 0x60, 1000, 1000);
    enemy.controlled = false; enemy.owner_id = 1; o.units.push_back(enemy);
    CommanderState s;
    const auto v = BuildCommanderView(s, o, services());
    require(v.anchors[6].valid && v.anchors[6].x==1000 && in_region(v.anchors[12]),
            "known attack target and independent fog search did not coexist");
}
void observing_target_during_combat_retires_it() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12]; require(in_region(target), "missing initial fog target");
    auto& t = o.tiles[(target.y / 32) * 64 + target.x / 32];
    t.visible = t.explored = true;
    auto enemy = unit(90, 0x60, 1000, 1000);
    enemy.controlled = false; enemy.owner_id = 1; o.units.push_back(enemy);
    o.simulation_frame = 9; BuildCommanderView(s, o, services());
    o.units.pop_back(); t.visible = false; o.simulation_frame = 17;
    v = BuildCommanderView(s, o, services());
    require(in_region(v.anchors[12]) &&
            (v.anchors[12].x != target.x || v.anchors[12].y != target.y),
            "search reused a target observed while attacking a known building");
}
void search_main(CommanderState& s) {
    s.squads[0].anchor = 12;
    s.squads[0].intent = CommanderIntent::attack_move;
}
bool same_target(CommanderPoint a, CommanderPoint b) {
    return a.valid == b.valid && a.x == b.x && a.y == b.y;
}
void stalled_search_advances_without_inventing_vision() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12]; search_main(s);
    o.units[1].x = target.x - 64; o.units[1].y = target.y;
    o.simulation_frame = 65; v = BuildCommanderView(s, o, services());
    require(same_target(v.anchors[12], target), "approaching a target retired it too soon");
    const auto before = v.input.map;
    o.simulation_frame = 1200; v = BuildCommanderView(s, o, services());
    require(in_region(v.anchors[12]) && !same_target(v.anchors[12], target),
            "stalled search target never advanced");
    for (u32 i = 6 * 256; i < 7 * 256; ++i)
        require(v.input.map[i] == before[i], "search retry invented explored or visible terrain");
    require(s.sweep_last_visible[(target.y / 32) * 64 + target.x / 32] == 0,
            "unsuccessful search was stored as a real sighting");
    o.simulation_frame = 1208; v = BuildCommanderView(s, o, services());
    require(!same_target(v.anchors[12], target), "failed target was immediately selected again");
}
void long_march_with_progress_keeps_its_target() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12]; search_main(s);
    for (u32 step = 1; step <= 3; ++step) {
        o.units[1].x = 450 + i32(step) * 200;
        o.simulation_frame = 1 + step * 900;
        v = BuildCommanderView(s, o, services());
        require(same_target(v.anchors[12], target), "progressing march lost its search target");
    }
}
void unassigned_search_does_not_expire() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12];
    o.simulation_frame = 20000; v = BuildCommanderView(s, o, services());
    require(same_target(v.anchors[12], target), "unused search anchor expired without an attempt");
}
void failed_target_can_be_retried_later() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12]; search_main(s);
    o.units[1].x = target.x - 64; o.units[1].y = target.y;
    o.simulation_frame = 65; BuildCommanderView(s, o, services());
    o.simulation_frame = 1200; v = BuildCommanderView(s, o, services());
    const auto other = v.anchors[12];
    require(!same_target(other, target), "missing deferred-target setup");
    auto& t = o.tiles[(other.y / 32) * 64 + other.x / 32];
    t.visible = t.explored = true;
    o.simulation_frame = 6000; v = BuildCommanderView(s, o, services());
    require(same_target(v.anchors[12], target), "failed target was permanently excluded");
}
void combat_pause_does_not_count_as_search_stagnation() {
    auto o = world(); conceal_region(o);
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12]; search_main(s);
    auto enemy = unit(90, 0x60, 1000, 1000);
    enemy.controlled = false; enemy.owner_id = 1; o.units.push_back(enemy);
    // Knowing a building alone no longer pauses reconnaissance. A real
    // attack animation still suspends the assigned formation's progress clock.
    o.units[1].command_flags|=0x10;
    o.simulation_frame = 5000; BuildCommanderView(s, o, services());
    o.units.pop_back();o.units[1].command_flags&=~0x10u; o.simulation_frame = 5008;
    v = BuildCommanderView(s, o, services());
    require(same_target(v.anchors[12], target), "combat pause counted as an unsuccessful search");
}
void moving_scout_is_not_a_stationary_formation() {
    auto o = world(); conceal_region(o);
    for (u32 id = 3; id < 13; ++id) o.units.push_back(unit(id, 0x22, 450, 450));
    o.units[1].movement_step_limit = 8;
    CommanderState s;
    auto v = BuildCommanderView(s, o, services());
    const auto target = v.anchors[12]; search_main(s);
    for (auto& entry : s.units) if (entry.first >= 2) entry.second.squad = 0;
    s.squads[0].intent = CommanderIntent::scout;
    o.units[1].x = 650; o.simulation_frame = 901;
    v = BuildCommanderView(s, o, services());
    o.simulation_frame = 1200; v = BuildCommanderView(s, o, services());
    require(same_target(v.anchors[12], target),
            "moving scout was timed out because its formation waited");
}
}
int main() {
    try {
        covers_ground_away_from_resources();
        start_slots_keep_priority();
        target_persists_until_observed_then_advances();
        skips_disconnected_fog();
        hidden_enemy_does_not_select_the_target();
        known_building_keeps_priority();
        observing_target_during_combat_retires_it();
        stalled_search_advances_without_inventing_vision();
        long_march_with_progress_keeps_its_target();
        unassigned_search_does_not_expire();
        failed_target_can_be_retried_later();
        combat_pause_does_not_count_as_search_stagnation();
        moving_scout_is_not_a_stationary_formation();
    } catch (const std::exception& e) {
        std::cerr << "ai_sweep_coverage_regression: " << e.what() << '\n'; return 1;
    }
    std::cout << "ai_sweep_coverage_regression: 13 groups passed\n"; return 0;
}
