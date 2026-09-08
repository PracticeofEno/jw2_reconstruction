#include "ranker_ai_commander.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ranker;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
AiObservedUnit unit(u32 id,u32 type,i32 x,i32 y,bool own=true) {
    AiObservedUnit u;u.id=id;u.runtime_slot_index=id;u.type_id=type;u.owner_id=own?0:1;
    u.controlled=own;u.visible=true;u.alive=true;u.x=x;u.y=y;
    u.health=u.max_health=300;u.command_state=1;u.type_flags=type>=0x60?0:0x20;
    u.attack_power=30;u.attack_range_base=u.attack_range=50;u.movement_step_limit=6;
    return u;
}
AiObservation world() {
    AiObservation o;o.simulation_frame=30001;o.local_owner=0;o.local_faction=2;o.local_relation_mask=1;
    o.map_width_tiles=o.map_height_tiles=64;o.tiles.resize(64*64);
    for(auto& tile:o.tiles){tile.passable=true;tile.buildable=true;tile.visible=true;tile.explored=true;}
    o.start_x=o.start_y=320;o.primary_resources=50;
    o.population_used=40;o.population_reserved=1;o.population_limit=180;
    o.units={unit(1,0x80,320,320),unit(2,0x20,352,320)};
    return o;
}
void berry(AiObservation& o,u32 x,u32 y=10,bool explored=true) {
    auto& tile=o.tiles[y*o.map_width_tiles+x];tile.resource_amount=4000;
    tile.passable=false;tile.buildable=false;tile.explored=explored;tile.visible=explored;
}
CommanderServices services() {
    CommanderServices sv;sv.autoscout=false;
    sv.validator=[](const AiSemanticAction&){return true;};return sv;
}
std::size_t harvests(const std::vector<AiSemanticAction>& actions,i32 x=-1) {
    return std::count_if(actions.begin(),actions.end(),[&](const auto& a){
        return a.kind==AiSemanticActionKind::harvest&&(x<0||a.target_x==x);});
}
void dry_no_income_recovers_outside_the_ordinary_radius() {
    auto o=world();berry(o,38);CommanderState s;
    const auto view=BuildCommanderView(s,o,services());
    require(view.input.vector[9]==0&&view.input.vector[16]==0,"fixture is not a dry economy");
    const float distance=std::hypot(38*32+16-320.0f,16.0f);
    require(distance>768&&distance<1536,"recovery berry does not distinguish the distance limits");
    const auto actions=CommanderExecute(s,o,view);
    require(harvests(actions,38*32)==1,"dry zero-income worker did not receive the newly reachable harvest order");
    require(s.mask_violations==0&&s.silent_rejections==0,"recovery changed diagnostics counters");
}
void earning_economy_keeps_the_ordinary_radius() {
    auto o=world();berry(o,38);CommanderState s;
    auto view=BuildCommanderView(s,o,services());view.input.vector[9]=0.5f;
    require(view.input.vector[47]>=0.5f,"fixture closes the far-harvest threat-age gate");
    require(harvests(CommanderExecute(s,o,view))==0,"income-positive worker used the emergency range");
    auto near_o=world();berry(near_o,28);CommanderState near_s;
    auto near_view=BuildCommanderView(near_s,near_o,services());near_view.input.vector[9]=0.5f;
    require(harvests(CommanderExecute(near_s,near_o,near_view),28*32)==1,"ordinary 24-tile far harvesting regressed");
}
void a_home_patch_keeps_the_ordinary_radius_even_when_fully_assigned() {
    auto o=world();berry(o,15);berry(o,38);
    for(u32 id=3;id<=5;++id){auto u=unit(id,0x20,480+i32(id),320);u.command_state=0x28;o.units.push_back(u);}
    CommanderState s;auto view=BuildCommanderView(s,o,services());
    for(u32 id=3;id<=5;++id)s.units.at(id).harvest_tile=10*64+15;
    const auto actions=CommanderExecute(s,o,view);
    require(harvests(actions)==0,"a saturated live home patch incorrectly unlocked the emergency range");
}
void range_and_fog_limits_are_preserved() {
    auto beyond=world();berry(beyond,60);CommanderState far_s;
    auto v=BuildCommanderView(far_s,beyond,services());
    require(harvests(CommanderExecute(far_s,beyond,v))==0,"worker exceeded the 48-tile emergency bound");
    auto unexplored=world();berry(unexplored,38,10,false);CommanderState unknown_s;
    v=BuildCommanderView(unknown_s,unexplored,services());
    require(harvests(CommanderExecute(unknown_s,unexplored,v))==0,"emergency recovery used an unexplored berry");
    auto hidden=world();berry(hidden,38);
    for(auto& tile:hidden.tiles)tile.visible=false;
    auto enemy=unit(90,0x73,1232,336,false);enemy.visible=false;hidden.units.push_back(enemy);
    CommanderState hidden_s;v=BuildCommanderView(hidden_s,hidden,services());
    require(v.enemies.empty(),"hidden enemy entered the decision view");
    require(harvests(CommanderExecute(hidden_s,hidden,v),38*32)==1,"hidden enemy changed the recovery decision");
}
void known_dangers_still_exclude_the_patch() {
    auto building=world();berry(building,38);building.units.push_back(unit(90,0x73,1232,336,false));
    CommanderState building_s;auto v=BuildCommanderView(building_s,building,services());
    require(harvests(CommanderExecute(building_s,building,v))==0,"worker ignored a known enemy building");
    auto force=world();berry(force,38);force.units.push_back(unit(90,0x12,1232,336,false));
    CommanderState force_s;v=BuildCommanderView(force_s,force,services());
    require(harvests(CommanderExecute(force_s,force,v))==0,"worker ignored a visible enemy force");
    auto raid=world();berry(raid,38);CommanderState raid_s;v=BuildCommanderView(raid_s,raid,services());
    raid_s.threat={1232,336,true};raid_s.threat_frame=raid.simulation_frame;
    require(harvests(CommanderExecute(raid_s,raid,v))==0,"worker ignored a recent raid site");
}
void patch_capacity_and_authoritative_validation_are_preserved() {
    auto o=world();berry(o,38);
    for(u32 id=3;id<=5;++id)o.units.push_back(unit(id,0x20,352+i32(id)*8,320));
    CommanderState s;auto v=BuildCommanderView(s,o,services());
    require(harvests(CommanderExecute(s,o,v),38*32)==3,"emergency patch exceeded three assigned workers");
    auto denied=world();berry(denied,38);CommanderState denied_s;auto sv=services();
    sv.validator=[](const AiSemanticAction& a){return a.kind!=AiSemanticActionKind::harvest;};
    v=BuildCommanderView(denied_s,denied,sv);
    require(harvests(CommanderExecute(denied_s,denied,v))==0,"emergency recovery bypassed the authoritative action validator");
}
}
int main() {
    try {
        dry_no_income_recovers_outside_the_ordinary_radius();
        earning_economy_keeps_the_ordinary_radius();
        a_home_patch_keeps_the_ordinary_radius_even_when_fully_assigned();
        range_and_fog_limits_are_preserved();
        known_dangers_still_exclude_the_patch();
        patch_capacity_and_authoritative_validation_are_preserved();
    } catch(const std::exception& e) {std::cerr<<"ai_commander_economy_recovery_regression: "<<e.what()<<'\n';return 1;}
    std::cout<<"ai_commander_economy_recovery_regression: 6 groups passed\n";return 0;
}
