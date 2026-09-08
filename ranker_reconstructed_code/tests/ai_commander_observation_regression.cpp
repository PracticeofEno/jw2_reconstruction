#include "ranker_ai_commander.h"
#include "ranker_ai_commander_rollout.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ranker;
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void close(float a,float b,const char* text){require(std::abs(a-b)<1e-6f,text);}
float lg(float x,float scale){return std::log1p(x)/std::log1p(scale);}
AiObservedUnit unit(u32 id,u32 type,i32 x,i32 y){
    AiObservedUnit u;u.id=id;u.runtime_slot_index=id;u.type_id=type;u.owner_id=0;
    u.controlled=u.visible=u.alive=true;u.x=x;u.y=y;u.health=u.max_health=300;u.command_state=1;
    u.type_flags=type<0x40?0x20:0;u.attack_power=type==0x22?30:0;
    u.attack_range_base=u.attack_range=50;u.movement_step_limit=6;return u;
}
AiObservation world(u32 frame=1){
    AiObservation o;o.simulation_frame=frame;o.local_owner=0;o.local_faction=2;o.local_relation_mask=1;
    o.map_width_tiles=o.map_height_tiles=32;o.tiles.resize(1024);
    for(auto& t:o.tiles){t.passable=t.buildable=t.explored=t.visible=true;t.placement_class=1;}
    o.start_x=o.start_y=320;o.population_limit=o.population_used=180;o.primary_resources=1000;
    o.units={unit(1,0x80,320,320),unit(2,0x22,336,320),unit(3,0x22,352,320)};return o;
}
CommanderServices services(){CommanderServices s;s.autoscout=false;s.validator=[](const AiSemanticAction&){return true;};return s;}
void same_legacy(const CommanderView& a,const CommanderView& b){
    require(std::equal(a.input.vector.begin(),a.input.vector.begin()+542,b.input.vector.begin()),"original542vector changed");
    require(std::equal(a.input.map.begin(),a.input.map.begin()+2304,b.input.map.begin()),"original9maps changed");
    require(a.mask==b.mask,"observation-only state changed legality");
}
void assignments_and_hidden_information(){
    auto o=world();auto a=unit(5,0x20,384,384),b=unit(6,0x20,400,384);a.command_state=b.command_state=0x29;
    o.units.push_back(a);o.units.push_back(b);const u32 first=12*32+12,second=12*32+14;
    o.tiles[first].resource_amount=o.tiles[second].resource_amount=1000;
    CommanderState s;const auto sv=services();BuildCommanderView(s,o,sv);
    s.units.at(5).harvest_tile=first;s.units.at(6).harvest_tile=first;auto split=s;split.units.at(6).harvest_tile=second;
    const auto together=BuildCommanderView(s,o,sv),separate=BuildCommanderView(split,o,sv);same_legacy(together,separate);
    close(together.input.vector[549],lg(1,32),"unassigned berry tiles missing");close(separate.input.vector[549],0,"assigned tiles counted free");
    close(together.input.vector[550],lg(2,80),"assigned workers missing");
    close(together.input.vector[551],lg(2,8),"tile congestion missing");close(separate.input.vector[551],lg(1,8),"spread assignments still congested");
    auto hidden=o;auto enemy=unit(99,0x02,384,384);enemy.owner_id=1;enemy.controlled=enemy.visible=false;enemy.health=999999;hidden.units.push_back(enemy);
    auto one=s,two=s;const auto honest=BuildCommanderView(one,o,sv),unseen=BuildCommanderView(two,hidden,sv);
    require(honest.input.vector==unseen.input.vector&&honest.input.map==unseen.input.map&&honest.mask==unseen.mask,"hidden enemy leaked into observation");
}
void terrain_and_visibility(){
    auto low=world(),high=low;for(auto& t:high.tiles)t.placement_class=2;CommanderState a,b;const auto sv=services();
    const auto one=BuildCommanderView(a,low,sv),two=BuildCommanderView(b,high,sv);same_legacy(one,two);
    close(one.input.map[2048],1,"old height channel changed");
    close(one.input.map[2304],std::round(255.0f/7)/255,"low height wrong");close(two.input.map[2304],std::round(510.0f/7)/255,"high height collapsed");
    require(two.input.map==BuildCommanderView(b,high,sv).input.map,"terrain caching changes maps");
    auto fog=world(),mixed=fog;
    for(u32 y=24;y<32;++y)for(u32 x=24;x<32;++x){fog.tiles[y*32+x].visible=false;mixed.tiles[y*32+x].explored=mixed.tiles[y*32+x].visible=x<28;}
    CommanderState f,g;const auto clear=BuildCommanderView(f,fog,sv),partial=BuildCommanderView(g,mixed,sv);const u32 cell=3*16+3;
    close(clear.input.map[1536+cell],partial.input.map[1536+cell],"fixture does not alias old visibility");
    close(clear.input.map[2560+cell],0,"fogged cell marked visible");close(clear.input.map[2816+cell],1,"explored fog lost");
    close(partial.input.map[2560+cell],128.0f/255,"visible fraction wrong");close(partial.input.map[2816+cell],128.0f/255,"explored fraction wrong");
}
void unclipped_values_and_execution_history(){
    auto o=world();CommanderState seed;auto sv=services();BuildCommanderView(seed,o,sv);o.simulation_frame=221;
    auto a=seed,b=seed;sv.cumulative_gathered=100;const auto normal=BuildCommanderView(a,o,sv);sv.cumulative_gathered=2000;const auto rich=BuildCommanderView(b,o,sv);
    close(normal.input.vector[9],1,"old income should be saturated");close(rich.input.vector[9],1,"old income changed");
    require(rich.input.vector[542]>1&&rich.input.vector[542]>normal.input.vector[542],"new income still clips");
    auto before=seed,after=seed;after.units.at(2).regrouping=true;after.units.at(2).applied_intent_serial=after.squads[0].serial;after.squads[0].last_intent_frame=200;
    const auto old=BuildCommanderView(before,o,services()),now=BuildCommanderView(after,o,services());same_legacy(old,now);
    close(now.input.vector[569],0.5f,"regroup state missing");close(now.input.vector[570],0.5f,"unapplied intent state missing");close(now.input.vector[576],lg(21,60000),"intent age is not in frames");
    auto wide=world(),wider=wide;wide.units[1].x=160;wide.units[2].x=800;wider.units[1].x=160;wider.units[2].x=1120;
    CommanderState c,d;const auto e=BuildCommanderView(c,wide,services()),f=BuildCommanderView(d,wider,services());
    close(e.input.vector[171],1,"fixture spread below legacy ceiling");close(f.input.vector[171],1,"legacy spread changed");require(f.input.vector[567]>e.input.vector[567],"new spread remains saturated");
}
void construction_reservation_and_empty_state(){
    auto o=world();o.units.push_back(unit(5,0x20,400,400));auto u=unit(7,0x82,800,800);u.under_construction=true;o.units.push_back(u);
    CommanderState s;auto sv=services();sv.construction_progress=[](u32){return 0.8f;};BuildCommanderView(s,o,sv);
    CommanderBuildReservation r;r.order.unit_ids={5};r.order.production_id=0x84;r.order.target_x=r.order.target_y=640;r.issued_frame=1;r.attempts=2;r.cost=50;s.builds.push_back(r);
    CommanderMergeReservation m;m.units={2,3};m.generations={0,0};m.started_frame=1;s.merges.push_back(m);o.simulation_frame=65;
    const auto v=BuildCommanderView(s,o,sv);
    close(v.input.vector[554],lg(50,4000),"reserved resources missing");close(v.input.vector[556],lg(1,8),"unacknowledged build missing");
    close(v.input.vector[557],lg(64,60000),"build age wrong");close(v.input.vector[558],lg(2,8),"retry count missing");
    close(v.input.vector[560],0.8f,"construction work progress confused with HP");close(v.input.vector[561],0.8f,"minimum progress wrong");close(v.input.vector[562],lg(2,180),"merge membership missing");
    auto copy=s;sv.construction_progress={};const auto missing=BuildCommanderView(copy,o,sv);close(missing.input.vector[560],-1,"unknown progress silently zeroed");
    auto empty=world();empty.units.clear();CommanderState blank;const auto zero=BuildCommanderView(blank,empty,services());
    for(u32 i=564;i<606;++i)close(zero.input.vector[i],0,"empty squad has nonzero aggregates");
}
}
int main(){
    static_assert(kCommanderVectorSize==606&&kCommanderMapSize==3072&&kCommanderRolloutRecordBytes==4446);
    try{assignments_and_hidden_information();terrain_and_visibility();unclipped_values_and_execution_history();construction_reservation_and_empty_state();}
    catch(const std::exception& e){std::cerr<<"ai_commander_observation_regression: "<<e.what()<<'\n';return 1;}
    std::cout<<"ai_commander_observation_regression: 4 groups passed\n";return 0;
}
