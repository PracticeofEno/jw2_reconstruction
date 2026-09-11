#include "ranker_ai_commander.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ranker;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct March {
    AiObservation o;CommanderState s;CommanderView v;
    March(u32 race=0) {
        o.simulation_frame=v.frame=129;o.map_width_tiles=256;o.map_height_tiles=32;
        o.population_used=180;o.tiles.resize(256*32);
        for(auto& t:o.tiles){t.passable=t.buildable=t.explored=t.visible=true;}
        v.services.own_tribe=race;v.services.autoscout=false;
        v.services.validator=[](const AiSemanticAction&){return true;};
        v.anchors[0]={64,512,true};v.anchors[6]={7000,512,true};
        auto& rule=s.squads[0];rule.intent=CommanderIntent::attack_move;rule.anchor=6;rule.roe=CommanderRoe::aggressive;
        for(u32 i=0;i<3;++i) {
            AiObservedUnit u;u.id=10+i;u.type_id=16*race+1;u.owner_id=1;
            u.alive=u.visible=u.controlled=true;u.type_flags=0x20;
            u.x=i==0?1700:900;u.y=512;u.health=u.max_health=300;
            u.attack_power=30;u.attack_range=u.attack_range_base=50;
            u.attackable_class_mask=~0u;u.movement_step_limit=i==0?6:3;u.command_state=2;
            v.own.push_back(u);auto& state=s.units[u.id];state.squad=0;
            state.last_progress=129;state.last_order.kind=AiSemanticActionKind::attack_move;
            state.last_order.target_x=7000;state.last_order.target_y=512;state.applied_intent_serial=1;
        }
        refresh();
    }
    void refresh() {
        auto& sq=v.squads[0];sq.members.clear();sq.center={0,0,true};sq.weight=0;
        for(const auto& u:v.own){sq.members.push_back(u.id);sq.center.x+=u.x;sq.center.y+=u.y;sq.weight+=300;}
        sq.center.x/=i32(v.own.size());sq.center.y/=i32(v.own.size());
    }
    auto tick(u32 frame) {v.frame=o.simulation_frame=frame;refresh();return CommanderExecute(s,o,v);}
};
bool has(const std::vector<AiSemanticAction>& rows,u32 id,AiSemanticActionKind kind) {
    return std::any_of(rows.begin(),rows.end(),[&](const auto& a){return a.unit_ids[0]==id&&a.kind==kind;});
}
void leader_wait_and_release(u32 race) {
    March m(race);auto rows=m.tick(129);
    check(has(rows,10,AiSemanticActionKind::hold_position),"march leader walked back instead of waiting");
    check(!has(rows,10,AiSemanticActionKind::move),"leader received backward gather move");
    // Validate the real wire planner too. HOLD has no semantic coordinates;
    // the planner obtains the standing location from the live unit itself.
    auto action=*std::find_if(rows.begin(),rows.end(),[](const auto& a){return a.unit_ids[0]==10&&a.kind==AiSemanticActionKind::hold_position;});
    UnitMovementUnit live;live.id=10;live.owner_id=1;live.type_id=race*16+1;
    live.active=true;live.type_flags=0x20;live.health=300;live.x=1700;live.y=512;
    PlayerSlotRuntimeState players;players.owner_relation_masks[1]=2;
    UnitMovementContext movement;movement.map.width=256;movement.map.height=32;movement.active_units={&live};
    AiActionPlanInput input;input.local_owner=1;input.players=&players;input.movement=&movement;
    auto plan=PlanAiSemanticActionV1(input,action);
    check(bool(plan)&&plan.packets.size()==1,"real native planner rejected commander HOLD");
    const auto& packet=plan.packets[0];
    check(packet.subtype==0x0a&&packet.arg0==0x21&&packet.arg2==1700&&packet.arg3==512,"HOLD wire packet has wrong standing position");
    m.v.own[0].command_state=0x21;
    check(!has(m.tick(137),10,AiSemanticActionKind::hold_position),"wait command repeated every tick");
    m.v.own[1].x=m.v.own[2].x=1680;
    check(has(m.tick(145),10,AiSemanticActionKind::attack_move),"caught-up leader failed to resume unchanged attack");
    check(!m.s.units.at(10).regrouping,"completed wait remained active");
}
void finite_wait_and_waypoint_cooldown() {
    March m;check(has(m.tick(129),10,AiSemanticActionKind::hold_position),"wait missing");
    m.v.own[0].command_state=0x21;
    m.s.units.at(10).last_progress=401;
    check(has(m.tick(401),10,AiSemanticActionKind::attack_move),"stranded follower caused unlimited wait");
    check(!has(m.tick(409),10,AiSemanticActionKind::hold_position),"expired pacer immediately stopped the leader");
}
void follower_receipt() {
    March m;m.v.own[0].x=500;m.v.own[1].x=m.v.own[2].x=1500;
    auto rows=m.tick(129);check(has(rows,10,AiSemanticActionKind::attack_move),"straggler did not catch up on the attack route");
    check(m.s.units.at(10).last_order.target_x>500,"straggler catch-up moved backward");
    m.v.own[0].x=1400;m.s.units.at(10).last_progress=137;
    check(!has(m.tick(137),10,AiSemanticActionKind::move),"joining follower received a backward center order");
}
void leader_already_beyond_a_bend() {
    March m;m.o.map_height_tiles=128;m.o.tiles.resize(256*128);
    for(auto& t:m.o.tiles)t.passable=t.buildable=t.explored=t.visible=true;
    // The center must first go east around a horizontal wall. The leader
    // has already crossed it and is south-west on the later route segment.
    for(u32 x=0;x<49;++x)m.o.tiles[25*256+x].passable=false;
    m.v.own[0].x=500;m.v.own[0].y=1100;
    m.v.own[1].x=m.v.own[2].x=1250;m.v.own[1].y=m.v.own[2].y=218;
    m.v.anchors[6]={500,2500,true};
    auto rows=m.tick(129);
    check(has(rows,10,AiSemanticActionKind::hold_position),"leader beyond ramp was recalled against route progress");
    check(!has(rows,10,AiSemanticActionKind::move),"ramp leader received backward gather order");
}
void combat_orders_and_budget() {
    March m;m.tick(129);auto enemy=m.v.own[1];enemy.id=90;enemy.owner_id=2;enemy.controlled=false;enemy.x=1350;
    m.v.visible_enemies={enemy};auto rows=m.tick(137);
    check(has(rows,10,AiSemanticActionKind::attack_move),"visible battle failed to release waiting leader");
    March retreat;retreat.tick(129);retreat.s.squads[0].intent=CommanderIntent::retreat;
    retreat.s.squads[0].anchor=0;++retreat.s.squads[0].serial;
    check(has(retreat.tick(137),10,AiSemanticActionKind::move),"cohesion suppressed explicit retreat");
    check(!retreat.s.units.at(10).regrouping,"retreat retained cohesion wait");
    March posted;posted.s.squads[0].intent=CommanderIntent::hold;posted.s.squads[0].anchor=0;
    check(has(posted.tick(129),10,AiSemanticActionKind::move),"defensive post leash was removed");
    March scout;scout.s.squads[0].intent=CommanderIntent::scout;++scout.s.squads[0].serial;
    rows=scout.tick(129);check(!has(rows,10,AiSemanticActionKind::hold_position),"single scout waited for its squad");
    March rejected;rejected.v.services.packet_budget=0;
    check(rejected.tick(129).empty(),"packet budget bypassed");
    check(rejected.s.units.at(10).last_order.kind==AiSemanticActionKind::attack_move,"unpublished wait got a receipt");
    rejected.v.services.packet_budget=64;
    check(has(rejected.tick(137),10,AiSemanticActionKind::hold_position),"rejected wait was never retried");
}
struct Travel {double backward=0;u32 waits=0,arrival=0,peak_gap=0,longest_stop=0,packets=0;};
Travel measure(u32 race,bool mixed=true) {
    March m(race);for(auto& u:m.v.own)u.x=700;
    if(!mixed)for(auto& u:m.v.own)u.movement_step_limit=3;
    for(auto& pair:m.s.units)pair.second.applied_intent_serial=0;
    std::array<UnitMovementUnit,3> live{};std::array<u32,3> turn{},stopped{};
    Travel result;
    for(u32 frame=129;frame<=6129;++frame) {
        if((frame-1)%8==0) {
            const auto orders=m.tick(frame);result.packets+=u32(orders.size());
            for(const auto& a:orders) {
                if(a.kind==AiSemanticActionKind::hold_position)++result.waits;
                // Native successful path replans reset acceleration and spend
                // four turn ticks, even when an order merely changes a point.
                const auto slot=a.unit_ids[0]-10;live[slot].movement_step_accumulator=0;turn[slot]=4;
            }
        }
        for(std::size_t i=0;i<m.v.own.size();++i) {
            auto& u=m.v.own[i];
            auto& state=m.s.units.at(u.id);const auto& a=state.last_order;
            const auto old=u.x;
            if(a.kind==AiSemanticActionKind::hold_position)u.command_state=0x21;
            else if(turn[i])--turn[i];
            else {
                // Straight open lane; use the actual engine braking curve and
                // accumulator rather than an instantaneous constant-speed mock.
                const i32 dx=a.target_x-u.x;const auto d=u32(std::abs(dx));
                const auto step=std::min(d,live[i].movement_step_accumulator/std::max(1u,u.movement_period));
                u.x+=dx<0?-i32(step):i32(step);u.y=a.target_y;
                const auto limit=ResolveMovementStepLimitForDistance(d-step,u.movement_period,u.movement_step_limit);
                AdjustUnitMovementStepAccumulatorTowardLimit(live[i],u.movement_period,limit);
                u.command_state=d<=step?1:2;
            }
            result.backward+=std::max(0,old-u.x);
            if(u.x!=old){state.last_progress=frame;stopped[i]=0;}
            else if(frame>193&&u.x<6400)result.longest_stop=std::max(result.longest_stop,++stopped[i]);
        }
        const auto lo=std::min_element(m.v.own.begin(),m.v.own.end(),[](const auto& a,const auto& b){return a.x<b.x;})->x;
        const auto hi=std::max_element(m.v.own.begin(),m.v.own.end(),[](const auto& a,const auto& b){return a.x<b.x;})->x;
        result.peak_gap=std::max(result.peak_gap,u32(hi-lo));if(lo>=6400){result.arrival=frame-129;break;}
    }
    std::cout<<"{\"race\":"<<race<<",\"mixed\":"<<mixed<<",\"backward_distance\":"<<result.backward<<",\"arrival_frames\":"<<result.arrival
        <<",\"waits\":"<<result.waits<<",\"peak_gap\":"<<result.peak_gap<<",\"longest_stop\":"<<result.longest_stop<<",\"packets\":"<<result.packets<<"}\n";
    return result;
}
void reinforcements_and_budget() {
    March m;for(auto& u:m.v.own)u.x=1000;
    m.tick(129);
    auto reinforcement=m.v.own.back();reinforcement.id=13;reinforcement.x=0;reinforcement.movement_step_limit=1;
    m.v.own.push_back(reinforcement);m.s.units[13]=m.s.units[12];m.s.units[13].applied_intent_serial=0;
    m.tick(161);check(m.s.squads[0].march_pacer!=13,"distant reinforcement slowed the formation before joining");
    m.v.own.back().x=1000;m.s.units[13].last_progress=193;m.tick(193);
    check(m.s.squads[0].march_pacer==13,"joined slower member was ignored");
    m.v.own.back().alive=false;m.tick(201);
    check(m.s.squads[0].march_pacer!=13,"dead pacer continued to govern movement");
    March large;large.v.own.clear();large.s.units.clear();
    for(u32 i=0;i<96;++i) {
        AiObservedUnit u=m.v.own[0];u.id=10+i;u.x=1000;u.movement_step_limit=i==95?3:6;
        large.v.own.push_back(u);auto& state=large.s.units[u.id];state.squad=0;state.last_progress=129;
    }
    large.v.services.packet_budget=8;
    for(u32 f=129;f<=321;f+=8)check(large.tick(f).size()<=8,"march exceeded packet budget");
    for(const auto& pair:large.s.units)check(pair.second.last_order_frame!=0,"budget-starved follower never received its march order");
}
}
int main(int argc,char**) {
    try{if(argc>1){for(u32 race=0;race<4;++race)measure(race);return 0;}
        for(u32 race=0;race<4;++race)leader_wait_and_release(race);
        finite_wait_and_waypoint_cooldown();follower_receipt();leader_already_beyond_a_bend();combat_orders_and_budget();reinforcements_and_budget();
        for(u32 race=0;race<4;++race) {
            const auto mixed=measure(race),slow=measure(race,false);
            check(mixed.backward==0&&mixed.waits==0,"assembled army resumed alternating HOLD/sprint cycles");
            check(mixed.peak_gap<=200,"fast half separated from the slow half");
            check(mixed.arrival>0&&mixed.arrival<=slow.arrival*1.1,"pacing delayed arrival beyond the slow unit's travel time");
            check(mixed.longest_stop<40,"paced army still stops for long regroup intervals");
        }
        std::cout<<"four-race march cohesion checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
