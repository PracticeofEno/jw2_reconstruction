#include "ranker_ai_commander.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ranker;
namespace {
void require(bool okay,const char* message) {if(!okay)throw std::runtime_error(message);}
AiObservation world() {
    AiObservation o;o.simulation_frame=1;o.local_owner=0;o.local_faction=2;o.local_relation_mask=1;
    o.map_width_tiles=o.map_height_tiles=32;o.tiles.resize(1024);
    for(auto& t:o.tiles){t.passable=true;t.buildable=true;t.explored=true;t.visible=true;}
    o.start_x=320;o.start_y=320;o.primary_resources=1000;o.population_used=40;o.population_reserved=5;o.population_limit=180;
    o.start_candidate_mask=3;o.start_candidate_x[0]=320;o.start_candidate_y[0]=320;o.start_candidate_x[1]=864;o.start_candidate_y[1]=864;
    return o;
}
AiObservedUnit unit(u32 id,u32 type,i32 x,i32 y,bool own=true) {
    AiObservedUnit u;u.id=id;u.runtime_slot_index=id;u.type_id=type;u.owner_id=own?0:1;u.controlled=own;u.visible=true;u.alive=true;
    u.x=x;u.y=y;u.health=u.max_health=300;u.type_flags=0x20;u.command_state=1;u.attack_power=30;u.attack_range_base=u.attack_range=50;u.movement_step_limit=6;
    if(type>=0x60){u.type_flags=0;u.health=u.max_health=1500;}
    return u;
}
CommanderServices services() {
    CommanderServices s;s.autoscout=false;s.validator=[](const AiSemanticAction&){return true;};return s;
}
void add_berries(AiObservation& o) {
    for(u32 x=15;x<19;++x){auto& t=o.tiles[10*32+x];t.resource_amount=4000;t.passable=false;t.buildable=false;}
}
void fog_honesty() {
    auto o=world();o.units={unit(1,0x80,320,320),unit(2,0x22,350,350)};o.primary_resources=0;
    for(auto& t:o.tiles)t.visible=false;
    auto hidden=unit(90,0x72,700,700,false);hidden.visible=false;o.units.push_back(hidden);
    auto sa=services();CommanderState a,b;auto va=BuildCommanderView(a,o,sa);o.units.pop_back();auto vb=BuildCommanderView(b,o,sa);
    require(va.input.vector==vb.input.vector&&va.input.map==vb.input.map&&va.mask==vb.mask,"hidden entity changed policy observation");
    require(a.ghosts.empty(),"unseen entity entered ghost memory");
    o.units.push_back(unit(90,0x72,700,700,false));auto seen=BuildCommanderView(a,o,sa);require(a.ghosts.size()==1&&seen.anchors[6].valid,"visible building not remembered");
    o.units.pop_back();o.simulation_frame=2209;auto ghost=BuildCommanderView(a,o,sa);require(ghost.enemies.size()==1&&!ghost.enemies[0].visible_now,"building ghost expired under fog");
    o.tiles[21*32+21].visible=true;o.simulation_frame=2217;BuildCommanderView(a,o,sa);require(a.ghosts.empty(),"empty visible tile kept building ghost");
    o.tiles[21*32+21].visible=false;o.units.push_back(unit(90,0x12,700,700,false));o.simulation_frame=2225;BuildCommanderView(a,o,sa);
    o.units.pop_back();o.simulation_frame=4425;BuildCommanderView(a,o,sa);require(a.ghosts.empty(),"mobile ghost lived beyond2200frames");
}
void masks_and_generation() {
    auto o=world();o.primary_resources=0;o.units={unit(1,0x80,320,320),unit(2,0x22,500,500)};auto sv=services();u32 generation=1;sv.generation_of=[&](u32){return generation;};
    CommanderState s;auto v=BuildCommanderView(s,o,sv);require(v.mask[59]&&!v.mask[60]&&!v.mask[61],"empty squad selectable");
    CommanderAction p{};CommanderMask m=v.mask;CommanderLegalHeadMask(v,p,3,m);require(m[62]&&!m[63],"NONE squad has nontrivial intent distribution");
    p[2]=1;p[3]=0;CommanderLegalHeadMask(v,p,4,m);for(u32 i=4;i<16;++i)require(!m[70+i],"HOLD allowed hostile anchor");
    p[0]=0;CommanderLegalHeadMask(v,p,1,m);require(m[42]&&std::count(m.begin()+43,m.begin()+58,1)==0,"irrelevant placement head not collapsed");
    s.units.at(2).squad=2;s.units.at(2).last_order_frame=99;++generation;o.simulation_frame=9;BuildCommanderView(s,o,sv);
    require(s.units.at(2).squad==0&&s.units.at(2).last_order_frame==0,"recycled identity inherited old squad/order");
}
void macro_and_queue_gate() {
    auto o=world();add_berries(o);o.units={unit(1,0x80,320,320),unit(2,0x20,370,330),unit(3,0x84,420,420),unit(4,0x85,450,450)};
    auto sv=services();CommanderState s;auto v=BuildCommanderView(s,o,sv);require(v.worker_cap==10&&v.mask[1]&&v.mask[3],"worker/army production unexpectedly closed");
    o.units[2].deferred_commands={{0x10,0x28,0,0}};o.units[2].deferred_command_count=1;o.population_reserved=36;o.simulation_frame=9;
    v=BuildCommanderView(s,o,sv);require(v.queued_population==4&&!v.mask[3],"waiting production population was not reserved");
    o.population_reserved=5;o.research_order_levels[0x19]=2;o.simulation_frame=17;v=BuildCommanderView(s,o,sv);require(!v.mask[25],"level3 research bypassed UI prerequisite");
    o.research_order_levels[0x19]=1;o.simulation_frame=25;v=BuildCommanderView(s,o,sv);require(v.mask[25],"level2 research incorrectly requires upgraded nest");
    o.units.push_back(unit(5,0x22,400,500));o.units.push_back(unit(6,0x22,410,500));o.simulation_frame=33;v=BuildCommanderView(s,o,sv);require(v.mask[35],"healthy twin merge not exposed");
    o.units[4].health=100;o.simulation_frame=41;v=BuildCommanderView(s,o,sv);require(!v.mask[35],"damaged merge material accepted");
}
void workers_and_cadence() {
    auto o=world();add_berries(o);o.primary_resources=0;o.units={unit(1,0x80,320,320),unit(2,0x20,380,320)};auto sv=services();CommanderState s;
    auto v=BuildCommanderView(s,o,sv);require(v.decision_due,"initial decision missing");CommanderAction action{};auto actions=CommanderExecute(s,o,v,&action);
    const auto harvest=std::find_if(actions.begin(),actions.end(),[](const auto& a){return a.kind==AiSemanticActionKind::harvest;});require(harvest!=actions.end(),"idle worker not assigned berries");
    o.units[1].command_state=0x2c;o.units[1].cargo_amount=12;o.simulation_frame=9;v=BuildCommanderView(s,o,sv);actions=CommanderExecute(s,o,v);require(std::none_of(actions.begin(),actions.end(),[](const auto& a){return a.kind==AiSemanticActionKind::harvest||a.kind==AiSemanticActionKind::return_cargo;}),"healthy cargo loop was interrupted");
    o.simulation_frame=33;v=BuildCommanderView(s,o,sv);require(v.decision_due&&v.event==0,"fixed32 heartbeat drifted");
}
void bounded_deterministic_packets() {
    auto o=world();o.primary_resources=0;o.units.push_back(unit(1,0x80,320,320));for(u32 i=0;i<100;++i)o.units.push_back(unit(10+i,0x22,400+int(i%10)*16,400+int(i/10)*16));
    auto sv=services();sv.packet_budget=32;CommanderState s;auto v=BuildCommanderView(s,o,sv);auto t=s;
    CommanderAction a{};a[2]=1;a[3]=0;a[4]=1;a[5]=0;
    auto first=CommanderExecute(s,o,v,&a),second=CommanderExecute(t,o,v,&a);require(first.size()<=32&&first.size()==second.size(),"packet budget/determinism failed");
    for(std::size_t i=0;i<first.size();++i)require(first[i].kind==second[i].kind&&first[i].unit_ids==second[i].unit_ids&&first[i].target_x==second[i].target_x&&first[i].target_y==second[i].target_y,"same state produced different commands");
    std::size_t untouched=0;for(const auto& pair:s.units)if(pair.second.type==0x22&&pair.second.applied_intent_serial==0)++untouched;
    require(untouched>=68,"unpublished overflow marked applied");
}
void reservations_and_receipts() {
    auto o=world();add_berries(o);o.units={unit(1,0x80,320,320),unit(2,0x20,380,320),unit(3,0x84,500,500)};auto sv=services();CommanderState s;
    CommanderBuildReservation r;r.cost=950;r.issued_frame=1;r.order.kind=AiSemanticActionKind::build;r.order.production_id=0x85;r.order.unit_ids={2};r.order.target_x=800;r.order.target_y=800;s.builds.push_back(r);
    auto v=BuildCommanderView(s,o,sv);require(v.reserved_resources==950&&!v.mask[3],"walking build funds double-spent");
    s.builds.clear();o.simulation_frame=9;v=BuildCommanderView(s,o,sv);CommanderAction a{};a[0]=3;CommanderExecute(s,o,v,&a);require(s.receipts.size()==1,"production receipt missing");
    o.units[2].queued_production_type_id=0x22;o.simulation_frame=17;BuildCommanderView(s,o,sv);require(s.silent_rejections==0&&s.receipts.empty(),"acknowledged production counted rejected");
    o.simulation_frame=25;v=BuildCommanderView(s,o,sv);CommanderExecute(s,o,v,&a);o.simulation_frame=33;BuildCommanderView(s,o,sv);require(s.silent_rejections==1,"silently rejected production was not diagnosed");
}
void potential_and_teacher() {
    auto o=world();add_berries(o);o.units={unit(1,0x80,320,320),unit(2,0x20,380,320)};auto sv=services();sv.kills_investment=4000;sv.losses_investment=1000;sv.cumulative_gathered=15000;o.research_order_levels[0x14]=1;
    CommanderState s;auto v=BuildCommanderView(s,o,sv);require(std::abs(v.potential_components[0]-0.25f*std::tanh(0.75f))<1e-6f&&v.potential_components[1]==0.05f,"potential formula differs from design");
    const auto action=CommanderTeacherAction(s,v);CommanderMask mask=v.mask;for(u32 h=0;h<8;++h){CommanderLegalHeadMask(v,action,h,mask);require(mask[kCommanderHeadOffsets[h]+action[h]]!=0,"teacher selected illegal conditional action");}
    for(float x:v.input.vector)require(std::isfinite(x),"nonfinite vector");
    for(float x:v.input.map)require(std::abs(x*255-std::round(x*255))<0.001f,"map does not match uint8 rollout quantization");
}
void visible_hunting_and_damage_reflex() {
    auto o=world();o.primary_resources=0;o.units={unit(1,0x80,320,320),unit(2,0x22,450,450),unit(3,0x22,470,450),unit(90,0x41,480,450,false)};
    o.units.back().owner_id=8;o.units.back().attack_power=0;
    auto sv=services();sv.construction_progress=[](u32){return 0.2f;};CommanderState s;auto v=BuildCommanderView(s,o,sv);
    s.squads[0].intent=CommanderIntent::hunt;s.squads[0].anchor=11;
    auto actions=CommanderExecute(s,o,v);
    require(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::set_hunt_marker&&a.stance_on;}),"HUNT marker omitted");
    require(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::hunt_unit&&a.target_unit_id==90;}),"HUNT did not use neutral acquire attack");
    o.units.pop_back();s.decision_count=1;s.last_decision_frame=1;
    s.external_damage_pending=true;s.external_damage_unit_id=2;s.threat={450,450,true};s.threat_frame=9;o.simulation_frame=9;
    v=BuildCommanderView(s,o,sv);require(v.decision_due&&v.event==3,"damage between snapshots did not triggerE3");
    actions=CommanderExecute(s,o,v);for(const auto&a:actions)require(a.target_unit_id!=90,"hidden neutral targeted by id");
    auto unfinished=unit(4,0x82,600,600);unfinished.under_construction=true;unfinished.health=50;o.units.push_back(unfinished);
    s.threat={600,600,true};s.threat_frame=17;o.simulation_frame=17;v=BuildCommanderView(s,o,sv);actions=CommanderExecute(s,o,v);
    require(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::cancel_construction&&a.unit_ids[0]==4;}),"damaged early construction not canceled");
    sv.construction_progress=[](u32){return 0.8f;};s.threat_frame=25;o.simulation_frame=25;
    v=BuildCommanderView(s,o,sv);actions=CommanderExecute(s,o,v);
    require(std::none_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::cancel_construction;}),"damage was mistaken for early construction progress");
}
void recovery_kiting() {
    auto o=world();o.primary_resources=0;o.units={unit(1,0x80,320,320),unit(2,0x24,450,450),unit(90,0x01,480,450,false)};
    o.units[1].attack_range=o.units[1].attack_range_base=230;o.units[1].command_lockout_ticks=8;o.units[2].movement_step_limit=3;
    auto sv=services();CommanderState s;auto v=BuildCommanderView(s,o,sv);auto actions=CommanderExecute(s,o,v);
    require(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::move&&a.unit_ids[0]==2&&a.target_x<450;}),"weapon recovery failed to permit kiting");
    o.units[1].command_lockout_ticks=0;CommanderState ready;v=BuildCommanderView(ready,o,sv);actions=CommanderExecute(ready,o,v);
    require(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::attack_unit&&a.unit_ids[0]==2;}),"ready weapon kited instead of firing");
}
void busy_constructor_not_reassigned() {
    auto o=world();add_berries(o);o.units={unit(1,0x80,320,320),unit(2,0x20,380,320)};
    // A Tyrano worker remains in0x24 after its structure becomes visible,
    // and the flight reservation is already released. Its runtime0x82 bits
    // keep new pending orders from executing until construction completes.
    o.units[1].command_state=0x24;
    auto sv=services();CommanderState s;auto v=BuildCommanderView(s,o,sv);
    require(!v.mask[13]&&!v.mask[15],"active constructor exposed as an available builder");
    o.units.push_back(unit(3,0x20,750,400));o.simulation_frame=9;v=BuildCommanderView(s,o,sv);
    require(v.mask[13]&&v.macro_plans[13][0].unit_ids[0]==3,"available secondworker not chosen over busyconstructor");
}
void expansion_cache_refreshes_live_knowledge() {
    auto o=world();o.primary_resources=0;o.map_width_tiles=64;o.tiles.resize(64*32);
    for(auto& t:o.tiles){t.passable=true;t.buildable=true;t.explored=true;t.visible=true;}
    for(u32 x=50;x<54;++x){auto& t=o.tiles[10*64+x];t.resource_amount=4000;t.passable=false;t.buildable=false;}
    o.units={unit(1,0x80,320,320)};auto sv=services();CommanderState s;
    BuildCommanderView(s,o,sv);require(s.expansion.has_target&&s.expansion.clusters.size()==1,"natural expansion geometry missing");
    const auto site=CommanderPoint{s.expansion.target_x,s.expansion.target_y,true};
    o.tiles[10*64+50].resource_amount=0;o.simulation_frame=9;BuildCommanderView(s,o,sv);
    require(s.expansion.clusters[0].known_amount==12000&&s.expansion.clusters[0].tile_count==3,"cached cluster depletion was stale");
    auto base=unit(2,0x80,site.x,site.y);base.under_construction=true;o.units.push_back(base);o.simulation_frame=17;BuildCommanderView(s,o,sv);
    require(!s.expansion.has_target&&s.expansion.clusters[0].developed,"new HQ did not reserve cached expansion");
    o.units.pop_back();o.simulation_frame=25;BuildCommanderView(s,o,sv);require(s.expansion.has_target,"lost HQ left permanent developed marker");
    for(u32 x=50;x<54;++x)o.tiles[10*64+x].resource_amount=0;
    o.simulation_frame=33;BuildCommanderView(s,o,sv);
    require(!s.expansion.has_target&&s.expansion.clusters[0].known_amount==0,"depleted cached cluster remained an expansion target");
}
void construction_return_waits_for_acknowledgment() {
    auto o=world();add_berries(o);o.units={unit(1,0x80,320,320),unit(2,0x20,380,320)};
    o.units[1].command_state=0x2c;o.units[1].cargo_amount=12;
    auto sv=services();CommanderState s;auto v=BuildCommanderView(s,o,sv);
    require(v.mask[13],"returning worker has no build candidate");
    CommanderAction action{};action[0]=13;
    auto actions=CommanderExecute(s,o,v,&action);
    require(s.builds.size()==1&&s.units.at(2).post_build_harvest.valid,"build lost postconstruction return assignment");
    require(std::none_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::harvest;}),"harvest queued before pending BUILD entered");
    const auto build=s.builds.front().order;
    o.simulation_frame=9;o.units[1].command_state=0x23;o.units[1].command_value=0x82;
    v=BuildCommanderView(s,o,sv);actions=CommanderExecute(s,o,v);
    require(std::none_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::harvest;}),"harvest queued before construction creation");
    o.simulation_frame=17;o.units[1].command_state=0x24;
    auto site=unit(3,0x82,build.target_x,build.target_y);site.under_construction=true;o.units.push_back(site);
    v=BuildCommanderView(s,o,sv);actions=CommanderExecute(s,o,v);
    require(s.builds.empty(),"created structure retained walking reservation");
    require(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::harvest&&a.queued&&a.unit_ids[0]==2;}),"active constructor did not queue postconstruction harvest");
    o.simulation_frame=25;v=BuildCommanderView(s,o,sv);actions=CommanderExecute(s,o,v);
    require(std::none_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::harvest;}),"postconstruction harvest queued twice");
}
void unreachable_builder_and_reselection() {
    auto o=world();add_berries(o);o.units={unit(1,0x80,320,320),unit(2,0x20,640,640),unit(3,0x20,950,950)};
    for(i32 y=19;y<=21;++y)for(i32 x=19;x<=21;++x)if(x!=20||y!=20) {
        auto& tile=o.tiles[y*32+x];tile.passable=false;tile.buildable=false;
    }
    auto sv=services();CommanderState s;auto v=BuildCommanderView(s,o,sv);
    require(v.mask[13]&&v.macro_plans[13][0].unit_ids[0]==3,"trapped closest builder selected over reachable worker");
    for(i32 y=19;y<=21;++y)for(i32 x=19;x<=21;++x) {auto& tile=o.tiles[y*32+x];tile.passable=true;tile.buildable=true;}
    o.simulation_frame=9;v=BuildCommanderView(s,o,sv);
    require(v.macro_plans[13][0].unit_ids[0]==2,"changed passability left stale builder component");
    CommanderAction action{};action[0]=13;CommanderExecute(s,o,v,&action);
    require(s.builds.size()==1&&s.builds[0].order.unit_ids[0]==2,"initial build assignment missing");
    o.primary_resources=200;o.simulation_frame=25;v=BuildCommanderView(s,o,sv);
    const auto actions=CommanderExecute(s,o,v);
    require(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::build&&a.unit_ids[0]==3;}),"failed build did not reselect another reachable worker");
    require(s.builds[0].order.unit_ids[0]==3&&s.units.at(3).post_build_harvest.valid&&!s.units.at(2).post_build_harvest.valid,"reselected worker lost construction return assignment");
}
void recycled_sources_retire_pending_work() {
    auto o=world();add_berries(o);o.units={unit(1,0x80,320,320),unit(2,0x20,380,320),unit(3,0x84,500,500),unit(4,0x22,400,450),unit(5,0x22,500,450),unit(6,0x85,700,700)};
    std::array<u32,7> generations{};generations.fill(1);auto sv=services();sv.generation_of=[&](u32 id){return generations.at(id);};
    CommanderState builder;auto v=BuildCommanderView(builder,o,sv);CommanderAction action{};action[0]=13;
    CommanderExecute(builder,o,v,&action);require(builder.builds.size()==1,"build reservation not created for generation test");
    ++generations[2];o.simulation_frame=9;v=BuildCommanderView(builder,o,sv);
    require(builder.builds.empty()&&!builder.units.at(2).post_build_harvest.valid&&v.reserved_resources==0,"recycled worker inherited obsolete construction");
    o.simulation_frame=1;CommanderState producer;v=BuildCommanderView(producer,o,sv);action={};action[0]=3;
    CommanderExecute(producer,o,v,&action);require(producer.receipts.size()==1,"production receipt not created for generation test");
    ++generations[3];o.simulation_frame=9;BuildCommanderView(producer,o,sv);
    require(producer.receipts.empty()&&producer.silent_rejections==0,"recycled producer treated as source of old receipt");
    o.simulation_frame=1;CommanderState merger;v=BuildCommanderView(merger,o,sv);action={};action[0]=35;
    CommanderExecute(merger,o,v,&action);require(merger.merges.size()==1&&!merger.merges[0].issued,"pending merge not created for generation test");
    ++generations[4];o.simulation_frame=9;v=BuildCommanderView(merger,o,sv);
    const auto actions=CommanderExecute(merger,o,v);
    require(merger.merges.empty()&&std::none_of(actions.begin(),actions.end(),[](const auto&a){return a.kind==AiSemanticActionKind::merge_units;}),"recycled material inherited obsolete merge order");
}
void scouting_party_lifecycle() {
    auto o=world();o.primary_resources=0;o.simulation_frame=6001;
    o.tiles[27*32+27].explored=false;o.tiles[27*32+27].visible=false;
    o.units={unit(1,0x80,320,320)};
    for(u32 id=10;id<26;++id)o.units.push_back(unit(id,0x22,400,400));
    auto sv=services();CommanderState s;BuildCommanderView(s,o,sv);
    for(u32 id=22;id<26;++id)s.units.at(id).squad=1;
    for(auto& squad:s.squads)squad.roe=CommanderRoe::aggressive;
    auto v=BuildCommanderView(s,o,sv);auto a=CommanderTeacherAction(s,v);
    require(a[0]==40,"stale intelligence did not launch scouting party");
    CommanderExecute(s,o,v,&a);
    o.simulation_frame+=8;v=BuildCommanderView(s,o,sv);a=CommanderTeacherAction(s,v);
    require(a[0]!=41&&a[2]==3&&a[3]==u8(CommanderIntent::scout),"new scouting party recalled before receiving SCOUT");
    CommanderExecute(s,o,v,&a);
    // One scout has crossed beyond the other three waiting members. It must
    // continue toward its destination, rather than regroup with the waiters.
    o.units[1].x=900;o.units[1].y=700;o.simulation_frame+=8;
    v=BuildCommanderView(s,o,sv);s.units.at(10).regrouping=true;
    const auto actions=CommanderExecute(s,o,v);
    const auto movement=std::find_if(actions.begin(),actions.end(),[](const auto& order){return order.kind==AiSemanticActionKind::move&&order.unit_ids[0]==10;});
    require(movement!=actions.end()&&movement->target_x>=800&&movement->target_y>=800,"single scout was pulled back to waiting squad");
    // Interrupts must not shorten the scouting trip to a few frames.
    s.decision_count+=100;o.simulation_frame+=8;v=BuildCommanderView(s,o,sv);a=CommanderTeacherAction(s,v);
    require(a[0]!=41,"interrupt count expired scouting trip before travel time");
    o.simulation_frame=7201;v=BuildCommanderView(s,o,sv);a=CommanderTeacherAction(s,v);
    require(a[0]==41,"scouting trip did not return after its frame deadline");
    CommanderExecute(s,o,v,&a);
    o.simulation_frame+=8;v=BuildCommanderView(s,o,sv);a=CommanderTeacherAction(s,v);
    require(a[0]!=40,"scouting party relaunched immediately after recall");
}
void reinforcement_does_not_split_attacking_main() {
    auto o=world();o.primary_resources=0;o.simulation_frame=12001;
    o.tiles[27*32+27].explored=false;o.tiles[27*32+27].visible=false;
    o.units={unit(1,0x80,320,320)};
    for(u32 id=10;id<26;++id)o.units.push_back(unit(id,0x22,700,700));
    auto sv=services();CommanderState s;BuildCommanderView(s,o,sv);
    s.squads[0].intent=CommanderIntent::attack_move;
    auto v=BuildCommanderView(s,o,sv);auto a=CommanderTeacherAction(s,v);
    require(a[0]!=38,"attacking MAIN was split to recreate empty GUARD");
    for(u32 id=30;id<40;++id)o.units.push_back(unit(id,0x22,400,400));
    o.simulation_frame+=8;BuildCommanderView(s,o,sv);
    for(u32 id=30;id<40;++id)s.units.at(id).squad=1;
    v=BuildCommanderView(s,o,sv);a=CommanderTeacherAction(s,v);
    require(a[0]==39,"accumulated reinforcements did not join attacking MAIN");
    CommanderExecute(s,o,v,&a);
    o.simulation_frame+=8;v=BuildCommanderView(s,o,sv);a=CommanderTeacherAction(s,v);
    require(a[0]!=38,"reinforcements immediately sent back to GUARD");
    s.squads[0].intent=CommanderIntent::hold;
    v=BuildCommanderView(s,o,sv);a=CommanderTeacherAction(s,v);
    require(a[0]!=38,"recent regroup immediately reversed while holding");
}
void commander_can_override_defense_reflex() {
    auto o=world();o.primary_resources=0;o.simulation_frame=9;
    o.units={unit(1,0x80,320,320),unit(2,0x22,450,450)};
    auto sv=services();CommanderState s;
    s.threat={500,500,true};s.threat_frame=9;
    auto v=BuildCommanderView(s,o,sv);
    CommanderAction action{};action[2]=1;action[3]=u8(CommanderIntent::hold);action[4]=1;
    CommanderExecute(s,o,v,&action);
    require(s.squads[0].intent==CommanderIntent::hold&&s.squads[0].anchor==1,"defense reflex overwrote commander's same-tick HOLD");
    // A new threat between commander decisions still triggers defense.
    o.simulation_frame=17;s.threat_frame=17;v=BuildCommanderView(s,o,sv);
    CommanderExecute(s,o,v);
    require(s.squads[0].intent==CommanderIntent::defend&&s.squads[0].anchor==10,"autonomous defense no longer handles new threat");
    const u32 serial=s.squads[0].serial;
    o.simulation_frame=25;v=BuildCommanderView(s,o,sv);CommanderExecute(s,o,v);
    require(s.squads[0].serial==serial,"unchanged defense reflex reissued squad intent");
}
void marching_squads_are_not_merge_material() {
    auto o=world();o.primary_resources=0;
    o.units={unit(1,0x80,320,320),unit(2,0x85,500,500),unit(10,0x22,420,420),unit(11,0x22,430,420)};
    auto sv=services();CommanderState s;auto v=BuildCommanderView(s,o,sv);
    require(v.mask[35],"holding healthy pair lost merge option");
    s.squads[0].intent=CommanderIntent::attack_move;o.simulation_frame=9;v=BuildCommanderView(s,o,sv);
    require(!v.mask[35],"marching MAIN exposed as merge material");
    o.units.push_back(unit(12,0x22,450,450));o.units.push_back(unit(13,0x22,460,450));
    o.simulation_frame=17;BuildCommanderView(s,o,sv);s.units.at(12).squad=s.units.at(13).squad=1;
    v=BuildCommanderView(s,o,sv);
    require(v.mask[35]&&v.macro_plans[35][0].unit_ids==std::vector<u32>({12,13}),"safe GUARD pair not used while MAIN marches");
    s.squads[1].intent=CommanderIntent::defend;o.simulation_frame=25;v=BuildCommanderView(s,o,sv);
    require(!v.mask[35],"defending pair exposed as merge material");
}
void engage_reflex_does_not_alternate_with_home_order() {
    for(u32 members:{1u,49u}) {
    auto o=world();o.primary_resources=0;
    o.units={unit(1,0x80,320,320),unit(90,0x21,600,320,false)};
    for(u32 id=10;id<10+members;++id)o.units.push_back(unit(id,0x22,500,320));
    auto sv=services();CommanderState s;auto v=BuildCommanderView(s,o,sv);
    s.squads[0].roe=CommanderRoe::aggressive;s.squads[0].serial=7;
    v.anchors[1]={320,320,true};
    const auto first=CommanderExecute(s,o,v);
    require(std::any_of(first.begin(),first.end(),[](const auto&a){return a.unit_ids[0]==10&&a.kind==AiSemanticActionKind::attack_move&&a.target_x>=528;}),"idle defender did not respond to visible enemy");
    o.simulation_frame=9;
    for(auto& u:o.units)if(u.id>=10&&u.id<10+members){u.x=520;u.command_state=0x15;}
    v=BuildCommanderView(s,o,sv);v.anchors[1]={320,320,true};
    const auto next=CommanderExecute(s,o,v);
    require(std::none_of(next.begin(),next.end(),[](const auto&a){return a.unit_ids[0]==10&&a.kind==AiSemanticActionKind::attack_move;}),"unchanged engagement was interrupted by fallback home order");
    }
}
}
int main() {
    try {fog_honesty();masks_and_generation();macro_and_queue_gate();workers_and_cadence();bounded_deterministic_packets();reservations_and_receipts();potential_and_teacher();visible_hunting_and_damage_reflex();recovery_kiting();busy_constructor_not_reassigned();expansion_cache_refreshes_live_knowledge();construction_return_waits_for_acknowledgment();unreachable_builder_and_reselection();recycled_sources_retire_pending_work();scouting_party_lifecycle();reinforcement_does_not_split_attacking_main();commander_can_override_defense_reflex();marching_squads_are_not_merge_material();engage_reflex_does_not_alternate_with_home_order();}
    catch(const std::exception& e){std::cerr<<"ai_commander_regression: "<<e.what()<<'\n';return 1;}
    std::cout<<"ai_commander_regression: 19 groups passed\n";return 0;
}
