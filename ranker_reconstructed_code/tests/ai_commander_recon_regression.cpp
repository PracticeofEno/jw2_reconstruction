#include "ranker_ai_commander.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ranker;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
AiObservedUnit unit(u32 id,u32 type,i32 x,i32 y) {
    AiObservedUnit u;u.id=u.runtime_slot_index=id;u.type_id=type;u.owner_id=0;
    u.controlled=u.visible=u.alive=true;u.x=x;u.y=y;u.health=u.max_health=300;
    u.type_flags=type<0x60?0x20:0;u.command_state=1;u.attack_power=type<0x60?30:0;
    u.attack_range_base=u.attack_range=50;u.movement_step_limit=6;return u;
}
struct World {
    AiObservation o;CommanderServices sv;CommanderState s;
    World(u32 race) {
        o.simulation_frame=4001;o.local_owner=0;o.local_relation_mask=1;
        o.start_x=o.start_y=320;o.map_width_tiles=o.map_height_tiles=64;
        o.tiles.resize(4096);for(auto& t:o.tiles)t.passable=t.buildable=t.explored=t.visible=true;
        for(u32 y=10;y<18;++y)for(u32 x=48;x<55;++x)o.tiles[y*64+x].visible=o.tiles[y*64+x].explored=false;
        o.population_used=o.population_limit=180;o.units.push_back(unit(1,0x60+16*race,320,320));
        for(u32 i=0;i<12;++i)o.units.push_back(unit(10+i,16*race+1,400+16*i,400));
        auto enemy=unit(90,0x60,1500,1500);enemy.controlled=false;enemy.owner_id=1;o.units.push_back(enemy);
        sv.own_tribe=race;sv.public_enemy_tribe=0;sv.autoscout=true;
        sv.validator=[](const AiSemanticAction&){return true;};
        sv.production_info=[](AiProductionRequestKind,u32){return CommanderProductionInfo{100,1,1,1};};
    }
    CommanderView view(){return BuildCommanderView(s,o,sv);}
};
bool moves(const std::vector<AiSemanticAction>& rows,u32 id) {
    return std::any_of(rows.begin(),rows.end(),[&](const auto& a){return a.kind==AiSemanticActionKind::move&&a.unit_ids[0]==id;});
}
void persistent_scout(u32 race) {
    World w(race);auto v=w.view();check(v.input.vector[40]>0&&v.anchors[12].valid,"known enemy suppressed fog target");
    auto rows=CommanderExecute(w.s,w.o,v);const auto scout=w.s.recon_scout_id;
    check(scout&&moves(rows,scout),"combat scout did not launch after finding enemy");
    w.o.simulation_frame+=8;v=w.view();
    for(const auto& q:v.squads)check(std::find(q.members.begin(),q.members.end(),scout)==q.members.end(),"scout distorted combat formation");
    check(v.army_count==12,"reserved scout disappeared from army accounting");
    CommanderExecute(w.s,w.o,v);check(w.s.recon_scout_id==scout&&!w.s.recon_returning,"known base recalled healthy scout");
    auto i=std::find_if(w.o.units.begin(),w.o.units.end(),[&](const auto& u){return u.id==scout;});
    i->x=1000;i->y=500;i->health=100;w.o.simulation_frame+=8;v=w.view();
    rows=CommanderExecute(w.s,w.o,v);check(w.s.recon_returning&&moves(rows,scout),"damaged scout failed to withdraw");
    i->x=i->y=320;w.o.simulation_frame+=8;v=w.view();CommanderExecute(w.s,w.o,v);
    check(!w.s.recon_scout_id&&w.s.recon_next_frame>w.o.simulation_frame,"returned scout was not released with cooldown");
    w.o.simulation_frame+=8;v=w.view();CommanderExecute(w.s,w.o,v);check(!w.s.recon_scout_id,"scout relaunched immediately");
}
void scout_death_and_budget() {
    World w(0);auto v=w.view();CommanderExecute(w.s,w.o,v);auto scout=w.s.recon_scout_id;
    w.o.units.erase(std::remove_if(w.o.units.begin(),w.o.units.end(),[&](const auto& u){return u.id==scout;}),w.o.units.end());
    w.o.simulation_frame+=8;v=w.view();CommanderExecute(w.s,w.o,v);
    check(!w.s.recon_scout_id&&w.s.recon_next_frame>w.o.simulation_frame,"dead scout replaced without cooldown");
    World b(1);b.sv.packet_budget=0;v=b.view();check(CommanderExecute(b.s,b.o,v).empty(),"recon bypassed packet budget");
    World small(2);small.o.units.erase(small.o.units.begin()+4,small.o.units.begin()+13);v=small.view();CommanderExecute(small.s,small.o,v);
    check(!small.s.recon_scout_id,"small defending army lost a unit to recon");
    World recycled(2);v=recycled.view();CommanderExecute(recycled.s,recycled.o,v);scout=recycled.s.recon_scout_id;
    for(auto& u:recycled.o.units)if(u.id==scout)u.type_id=0x80;
    recycled.o.simulation_frame+=8;recycled.view();
    check(!recycled.s.recon_scout_id,"replacement identity inherited reconnaissance reservation");
}
void hunts_choose_feasible_targets(u32 race) {
    World w(race);w.sv.autoscout=false;w.o.units.pop_back();
    auto giant=unit(90,0x41,530,400);giant.owner_id=8;giant.controlled=false;giant.attack_power=0;giant.health=giant.max_health=10000;
    auto prey=giant;prey.id=prey.runtime_slot_index=91;prey.x=650;prey.health=prey.max_health=200;
    w.o.units.push_back(giant);w.o.units.push_back(prey);auto v=w.view();
    check(v.anchors[11].valid&&v.anchors[11].x==650,"expensive nearest neutral displaced cheaper prey");
    CommanderAction a{};a[2]=1;a[3]=5;a[4]=11;CommanderMask mask=v.mask;CommanderLegalHeadMask(v,a,3,mask);
    check(mask[kCommanderHeadOffsets[3]+5],"safe hunt unavailable");
    auto rows=CommanderExecute(w.s,w.o,v,&a);
    check(std::any_of(rows.begin(),rows.end(),[](const auto& x){return x.kind==AiSemanticActionKind::hunt_unit&&x.target_unit_id==91;}),"feasible neutral never received hunt order");
    auto enemy=unit(92,0x22,700,400);enemy.controlled=false;enemy.owner_id=1;w.o.units.push_back(enemy);w.o.simulation_frame+=8;v=w.view();
    CommanderLegalHeadMask(v,a,3,mask);check(mask[kCommanderHeadOffsets[3]+5],"hostile pressure incorrectly prohibited hunting");
    w.o.units.erase(w.o.units.begin()+14); // Only the much stronger, armed monster remains.
    w.o.units[13].attack_power=1000;w.o.simulation_frame+=8;v=w.view();
    CommanderLegalHeadMask(v,a,3,mask);check(mask[kCommanderHeadOffsets[3]+5],"strong armed monster was prohibited");
    w.o.units.pop_back();for(auto& u:w.o.units)if(u.controlled&&u.type_id<0x60)u.action_mode=100;
    w.o.simulation_frame+=8;v=w.view();rows=CommanderExecute(w.s,w.o,v,&a);
    check(std::any_of(rows.begin(),rows.end(),[](const auto& x){return x.kind==AiSemanticActionKind::attack_unit&&x.target_unit_id==90;}),"loaded fighter cannot hunt without enabling auto pickup");
    check(std::none_of(rows.begin(),rows.end(),[](const auto& x){return x.kind==AiSemanticActionKind::hunt_unit||(x.kind==AiSemanticActionKind::set_hunt_marker&&x.stance_on);}),"loaded hunter enabled auto pickup");
    for(auto& u:w.o.units)if(u.controlled&&u.type_id<0x60)u.attackable_class_mask=0;
    w.o.simulation_frame+=8;v=w.view();check(!v.anchors[11].valid,"incapable weapons supplied hunt strength");
}
void idle_hunt_labels_do_not_override_the_actor(u32 race) {
    World w(race);w.sv.autoscout=false;w.o.units.pop_back();
    auto monster=unit(90,0x41,600,400);monster.owner_id=8;monster.controlled=false;monster.attack_power=1000;
    monster.health=monster.max_health=10000;w.o.units.push_back(monster);
    auto v=w.view();w.s.squads[0].anchor=0;w.s.squads[0].last_intent_frame=w.o.simulation_frame-200;v=w.view();CommanderAction actor{};
    const auto label=CommanderIdleHuntLabel(w.s,v,actor);
    check(label[2]==1&&label[3]==5&&label[4]==11,"idle supervision excluded a dangerous neutral");
    check(w.s.squads[0].intent==CommanderIntent::hold,"label generation changed the live mission");
    auto rows=CommanderExecute(w.s,w.o,v,&actor);
    check(std::none_of(rows.begin(),rows.end(),[](const auto& x){return x.kind==AiSemanticActionKind::hunt_unit;}),"unselected idle hunt executed anyway");
    for(u8 intent:{u8(2),u8(3),u8(4),u8(6),u8(7)}) {
        actor[2]=1;actor[3]=intent;actor[4]=6;
        check(CommanderIdleHuntLabel(w.s,v,actor)==actor,"hunt label displaced an active mission");
    }
    actor={};for(auto& u:w.o.units)if(u.controlled&&u.type_id<0x60){u.command_state=2;u.x+=8;}
    w.o.simulation_frame+=8;v=w.view();
    check(CommanderIdleHuntLabel(w.s,v,actor)==actor,"travelling squad labelled idle");
}
void hunt_meat_goes_only_to_empty_units(u32 race) {
    World w(race);w.sv.autoscout=false;w.o.units.pop_back();
    for(auto& u:w.o.units)if(u.type_id<0x60){u.type_flags|=2;u.action_mode=100;}
    w.o.units[2].action_mode=0;const u32 collector=w.o.units[2].id;
    w.o.map_effects.push_back({5,1,560,400,100,false});
    w.o.map_effects.push_back({6,1,580,400,100,false});
    auto v=w.view();CommanderAction a{};a[2]=1;a[3]=5;a[4]=11;
    CommanderMask mask=v.mask;CommanderLegalHeadMask(v,a,3,mask);
    check(mask[kCommanderHeadOffsets[3]+5],"meat-only HUNT unavailable");
    auto rows=CommanderExecute(w.s,w.o,v,&a);
    check(w.s.meat.size()==1&&w.s.meat[0].unit==collector,"meat assigned to loaded or duplicate collector");
    check(std::count_if(rows.begin(),rows.end(),[&](const auto& x){return x.kind==AiSemanticActionKind::pickup_move&&x.unit_ids[0]==collector;})==1,"empty collector did not receive pickup command");
    for(const auto& x:rows)if(x.kind==AiSemanticActionKind::set_hunt_marker&&x.stance_on)check(x.unit_ids[0]==collector,"loaded fighter enabled automatic meat pickup");
    w.o.map_effects[0].linked=true;w.o.map_effects[1].linked=true;w.o.units[2].command_state=5;w.o.simulation_frame+=8;v=w.view();
    CommanderLegalHeadMask(v,a,3,mask);check(mask[kCommanderHeadOffsets[3]+5],"last linked drop prematurely masked HUNT");
    rows=CommanderExecute(w.s,w.o,v);
    check(w.s.meat.size()==1&&w.s.meat[0].unit==collector,"claimed meat lost its original collector");
    check(std::none_of(rows.begin(),rows.end(),[&](const auto& x){return x.unit_ids[0]==collector;}),"native collection was interrupted");
    w.o.units[2].action_mode=99;w.o.units[2].command_state=1;w.o.map_effects.erase(w.o.map_effects.begin());
    w.o.simulation_frame+=8;v=w.view();rows=CommanderExecute(w.s,w.o,v);
    check(w.s.meat.empty(),"loaded collector retained a drop reservation");
    check(std::none_of(rows.begin(),rows.end(),[](const auto& x){return x.kind==AiSemanticActionKind::pickup_move;}),"loaded collector was sent for more meat");
    w.o.units[2].action_mode=0;w.o.map_effects[0].linked=false;w.o.simulation_frame+=8;v=w.view();CommanderExecute(w.s,w.o,v,&a);
    check(w.s.meat.size()==1,"empty collector could not collect another drop");
    w.o.simulation_frame+=8;v=w.view();a[3]=7;a[4]=0;
    // Explicit mission change, independent of the near-home retreat mask.
    w.s.squads[0].intent=CommanderIntent::retreat;w.s.squads[0].anchor=0;++w.s.squads[0].serial;
    CommanderExecute(w.s,w.o,v);check(w.s.meat.empty(),"old meat task survived a new retreat mission");
}
void hunt_meat_visibility_and_budget() {
    for(int scenario=0;scenario<4;++scenario) {
        World w(1);w.sv.autoscout=false;w.o.units.pop_back();
        for(auto& u:w.o.units)u.type_flags|=2;
        w.o.map_effects.push_back({5,1,560,400,100,scenario==1});
        if(scenario==0)w.o.tiles[12*64+17].visible=false;
        if(scenario==2)w.sv.packet_budget=0;
        auto v=w.view();if(scenario!=3){w.s.squads[0].intent=CommanderIntent::hunt;w.s.squads[0].anchor=11;}
        const auto rows=CommanderExecute(w.s,w.o,v);
        check(w.s.meat.empty(),"hidden/claimed/budget-blocked meat or a HOLD mission created a collection task");
        check(std::none_of(rows.begin(),rows.end(),[](const auto& x){return x.kind==AiSemanticActionKind::pickup_move;}),"invalid meat pickup published");
    }
}
void no_safe_route_does_not_repeat_launches() {
    World w(0);
    for(auto& t:w.o.tiles)t.passable=false;
    for(u32 x=0;x<64;++x)for(u32 y=9;y<=11;++y)w.o.tiles[y*64+x].passable=true;
    for(auto& u:w.o.units)u.y=320;
    w.o.units.back().x=900;
    auto v=w.view();check(v.anchors[6].valid&&!v.anchors[12].valid,"unreachable fog selected across known enemy corridor");
    CommanderExecute(w.s,w.o,v);check(!w.s.recon_scout_id,"scout launched without safe reachable fog");
    w.o.simulation_frame+=1024;v=w.view();CommanderExecute(w.s,w.o,v);
    check(!w.s.recon_scout_id,"unsafe corridor caused a repeated scout launch");
}
void lost_enemy_memory_keeps_safe_reconnaissance() {
    World w(0);w.s.recon_started_frame=2000;w.o.units.pop_back();
    w.o.start_candidate_mask=2;w.o.start_candidate_x[1]=w.o.start_candidate_y[1]=1900;
    w.o.tiles[59*64+59].visible=w.o.tiles[59*64+59].explored=false;
    auto v=w.view();check(v.input.vector[40]==0,"test still knows an enemy base");
    check(v.anchors[12].valid&&v.anchors[12].y<1000,"loss of enemy memory restored an obsolete start-slot mission");
}
void exhausted_hunt_does_not_send_the_army_home(u32 race) {
    World w(race);w.sv.autoscout=false;w.o.units.pop_back();
    for(auto& u:w.o.units)if(u.type_id<0x60){u.x=1500;u.y=600;}
    auto v=w.view();w.s.squads[0].intent=CommanderIntent::hunt;w.s.squads[0].anchor=11;
    check(!v.anchors[11].valid,"exhausted hunt fixture has prey");
    auto rows=CommanderExecute(w.s,w.o,v);bool local=false;
    for(const auto& a:rows)if(a.kind==AiSemanticActionKind::move||a.kind==AiSemanticActionKind::attack_move) {
        check(a.target_x>1000,"missing hunt target recalled army to HQ");local=true;
    }
    check(local,"exhausted hunt did not issue a local fallback");
    w.s.squads[0].intent=CommanderIntent::retreat;w.s.squads[0].anchor=0;++w.s.squads[0].serial;
    w.o.simulation_frame+=8;v=w.view();rows=CommanderExecute(w.s,w.o,v);
    check(std::any_of(rows.begin(),rows.end(),[](const auto& a){return a.kind==AiSemanticActionKind::move&&a.target_x<600;}),"local fallback suppressed explicit retreat");
}
void region_requires_more_than_one_glimpse() {
    World w(1);w.view();w.s.recon_scout_id=10;w.s.recon_started_frame=4001;w.s.sweep_target={};auto v=w.view();const auto goal=v.anchors[12];
    check(w.s.sweep_region.size()>10,"recon did not choose a substantial fog region");
    const auto region=w.s.sweep_region;
    auto& tile=w.o.tiles[(goal.y/32)*64+goal.x/32];tile.visible=tile.explored=true;
    w.o.simulation_frame+=8;v=w.view();
    check(w.s.sweep_region==region,"one glimpse retired regional exploration");
    const u32 waypoint=u32(v.anchors[12].y/32)*64+u32(v.anchors[12].x/32);
    check(std::find(region.begin(),region.end(),waypoint)!=region.end(),"scout abandoned the region after one glimpse");
    for(u32 t:region)w.o.tiles[t].visible=w.o.tiles[t].explored=true;
    w.o.simulation_frame+=8;v=w.view();
    check(!v.anchors[12].valid||v.anchors[12].x!=goal.x||v.anchors[12].y!=goal.y,"finished region never advanced");
}
void fog_gain_beats_a_tiny_fringe() {
    World w(1);for(auto& t:w.o.tiles)t.visible=t.explored=true;
    for(u32 y=10;y<=16;++y)for(u32 x=22;x<=28;++x)w.o.tiles[y*64+x].visible=w.o.tiles[y*64+x].explored=false;
    w.o.tiles[13*64+19].visible=w.o.tiles[13*64+19].explored=false;
    w.view();w.s.recon_scout_id=10;w.s.recon_started_frame=4001;w.s.sweep_target={};
    const auto v=w.view();check(v.anchors[12].x>700,"tiny fringe outweighed a nearby rich region");
}
void unexplored_precedes_old_territory() {
    World w(1);w.view();
    for(u32 y=9;y<=15;++y)for(u32 x=15;x<=21;++x)w.o.tiles[y*64+x].visible=false;
    w.o.simulation_frame+=2200;w.s.recon_scout_id=10;w.s.recon_started_frame=4001;w.s.sweep_target={};
    const auto v=w.view();check(v.anchors[12].x>1000,"nearby old territory displaced reachable black fog");
}
void reached_region_does_not_wait_for_occluded_cells() {
    World w(1);w.view();w.s.recon_scout_id=10;w.s.recon_started_frame=4001;w.s.sweep_target={};auto v=w.view();
    const auto goal=v.anchors[12];const auto cells=w.s.sweep_region;
    for(std::size_t i=0;i<cells.size()*3/5;++i)w.o.tiles[cells[i]].visible=w.o.tiles[cells[i]].explored=true;
    for(auto& u:w.o.units)if(u.id==10){u.x=goal.x;u.y=goal.y;}
    w.o.simulation_frame+=8;v=w.view();
    check(w.s.sweep_region==cells,"partially explored region was abandoned");
    const auto next=v.anchors[12];
    check(next.x!=goal.x||next.y!=goal.y,"scout stood at a visible waypoint instead of exploring its region");
    for(u32 t:cells)if(!w.o.tiles[t].visible)w.o.tiles[t].passable=false;
    w.o.simulation_frame+=128;v=w.view();
    check(w.s.sweep_region!=cells,"scout kept waiting for unreachable regional cells");
}

}
int main() {
    try{for(u32 race=0;race<4;++race){persistent_scout(race);hunts_choose_feasible_targets(race);hunt_meat_goes_only_to_empty_units(race);idle_hunt_labels_do_not_override_the_actor(race);exhausted_hunt_does_not_send_the_army_home(race);}hunt_meat_visibility_and_budget();scout_death_and_budget();no_safe_route_does_not_repeat_launches();lost_enemy_memory_keeps_safe_reconnaissance();region_requires_more_than_one_glimpse();fog_gain_beats_a_tiny_fringe();reached_region_does_not_wait_for_occluded_cells();unexplored_precedes_old_territory();
        std::cout<<"four-race persistent reconnaissance and hunt checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
