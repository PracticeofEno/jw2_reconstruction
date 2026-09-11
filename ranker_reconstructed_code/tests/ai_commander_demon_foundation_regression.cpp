#include "ranker_ai_commander.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ranker;
static unsigned checks=0;
static u32 test_opponent=1;
static void ok(bool b,const char* name){++checks;if(!b)throw std::runtime_error(name);}
struct Fixture {
    CommanderState s;CommanderView v;u32 id=1;
    Fixture(u32 initial_workers=14){
        s.decision_count=101;v.frame=2001;v.worker_cap=40;v.income_rate=200;
        v.services.own_tribe=3;v.services.public_enemy_tribe=test_opponent;v.services.teacher_variant=0;
        v.services.coordinated_transfers=true;v.services.autoscout=false;
        v.services.validator=[](const AiSemanticAction&){return true;};
        v.services.building_prerequisite=[](u32){return true;};
        v.services.production_info=[](AiProductionRequestKind k,u32 t){CommanderProductionInfo p;p.cost=k==AiProductionRequestKind::research?500:t==0x90?1200:t==0x92?200:t>=0x60?400:t==0x30?100:t==0x31?150:400;p.population=t==0x90?10:t==0x92?8:t<0x60?1:0;return p;};
        v.mask.fill(1);for(u32 n=1;n<96;++n)v.mask[n]=0;
        v.input.vector[16]=v.input.vector[17]=1;v.input.vector[22]=float(initial_workers)/180;v.input.vector[23]=float(initial_workers+16)/180;v.input.vector[24]=1;v.input.vector[47]=0;
        v.anchors[0]=v.anchors[1]=v.anchors[2]={512,512,true};v.anchors[6]={3000,1000,true};v.anchors[12]=v.anchors[6];
        v.anchors[3]={1600,512,true};v.input.vector[260]=1;
        s.build_component_width=128;s.build_components.assign(128*128,1);
        resources(200);add(0x90,1);add(0x30,initial_workers);
    }
    void resources(float r){v.input.vector[8]=v.input.vector[553]=std::log1p(r)/std::log1p(20000.f);}
    void add(u32 type,u32 n,u8 q=0,bool wounded=false){for(u32 k=0;k<n;++k){
        AiObservedUnit u;u.id=id++;u.type_id=type;u.owner_id=0;u.controlled=u.alive=u.visible=true;u.command_state=1;
        u.x=512+i32((u.id%5)*16);u.y=512+i32((u.id/5%5)*16);u.health=u.max_health=type>=0x60?2000:210;
        if(type<0x60&&(type&15u)){u.type_flags=0x20;u.attack_power=type==0x36?30:20;u.defense_power=1;
            u.attack_range=u.attack_range_vs_air=u.attack_range_base=type==0x36?50:240;u.attackable_class_mask=type==0x17?8:31;u.movement_step_limit=4;
            if(wounded)u.health=42;
        }
        v.own.push_back(u);auto& st=s.units[u.id];st.squad=type>=0x60||(type&15u)==0?3:q;st.type=type;st.completed=true;
        st.x=u.x;st.y=u.y;st.health=u.health;st.last_progress=v.frame;st.applied_intent_serial=s.squads[q].serial;
    }recount();}
    void recount(){v.own_counts.fill(0);v.workers=v.army_count=0;v.own_weight=0;v.squads={};
        for(const auto& u:v.own){if(!u.alive||u.under_construction)continue;u32 ix=u.type_id<0x60?u.type_id-0x30:16+u.type_id-0x90;
            if(ix<48)++v.own_counts[ix];if(u.type_id==0x30)++v.workers;
            if(u.type_flags&0x20){++v.army_count;v.own_weight+=u.health+u.attack_power+u.defense_power;auto& sq=v.squads[std::min<u8>(2,s.units.at(u.id).squad)];
                sq.members.push_back(u.id);sq.center.x+=u.x;sq.center.y+=u.y;sq.weight+=u.health+u.attack_power+u.defense_power;}}
        for(u32 q=0;q<3;++q){auto& sq=v.squads[q];if(!sq.members.empty()){sq.center.x/=i32(sq.members.size());sq.center.y/=i32(sq.members.size());sq.center.valid=true;}
            sq.intent=s.squads[q].intent;sq.anchor=s.squads[q].anchor;sq.roe=s.squads[q].roe;}
    }
    void settled(){s.last_research[31]=1;add(0x94,3);add(0x96,1);add(0x30,20-v.workers);v.frame=9001;s.decision_count=101;}
    CommanderAction action(){auto a=CommanderTeacherAction(s,v);std::cout<<"action";for(u8 x:a)std::cout<<','<<unsigned(x);std::cout<<'\n';return a;}
    void macro(u32 expected,const char* why){ok(action()[0]==expected,why);}
    void enemy(i32 x,i32 y,u32 count=1,u32 hp=210){for(u32 k=0;k<count;++k){AiObservedUnit e;e.id=10000+u32(v.visible_enemies.size());e.type_id=0x31;e.owner_id=1;e.type_flags=0x20;e.alive=e.visible=true;e.health=e.max_health=hp;e.attack_power=20;e.defense_power=1;e.x=x+k*8;e.y=y;e.attackable_class_mask=31;v.visible_enemies.push_back(e);}}
    void expansion(){s.expansion.has_target=true;s.expansion.target_index=0;AiBerryCluster c;c.site_x=1600;c.site_y=512;c.site_explored=true;c.known_amount=20000;s.expansion.clusters={c};v.hq_build_plans[3].push_back({});}
    AiObservation observation(){AiObservation o;o.simulation_frame=v.frame;o.local_owner=0;o.map_width_tiles=o.map_height_tiles=128;o.start_x=o.start_y=512;o.tiles.resize(128*128);
        for(auto& t:o.tiles)t.passable=t.explored=t.visible=true;o.units=v.own;return o;}
};
int main(){try{for(u32 opponent=0;opponent<4;++opponent){test_opponent=opponent;
      {Fixture f;f.resources(500);f.v.mask[22]=f.v.mask[15]=1;f.macro(22,"funded harvest31 before first Den");}
      {Fixture f(13);f.v.mask[1]=f.v.mask[15]=1;f.macro(1,"Insect14 before first Den");f.v.pending_counts[0]=1;f.macro(0,"worker completed+pending14 cap");}
      {Fixture f;f.s.last_research[31]=1;f.resources(400);f.v.mask[15]=1;f.macro(15,"accepted harvest releases DeathDen");}
      {Fixture f;f.settled();f.add(0x31,16);f.add(0x36,4);auto a=f.action();ok(a[2]==1&&a[3]==2&&a[5]==0,"20 ground MAIN attacks");auto o=f.observation();auto out=CommanderExecute(f.s,o,f.v,&a);unsigned forward=0;for(const auto& order:out)forward+=order.kind==AiSemanticActionKind::attack_move&&order.target_x>700;ok(forward>=20,"actual forward packets");}
      {Fixture f;f.settled();f.add(0x31,10);f.add(0x36,2);f.resources(1000);f.v.mask[45]=f.v.mask[2]=1;f.macro(45,"funded Nightmare tech prerequisite");f.add(0x9b,1);f.v.mask[44]=1;f.macro(44,"Nightmare transition");f.v.pending_counts[6]=1;f.v.pending_counts[15]=1;f.macro(2,"shared MagicDen queue falls back to Skeleton");}
      {Fixture f;f.settled();f.add(0x31,16);f.add(0x36,4);f.add(0x9b,1);f.expansion();f.resources(1100);f.v.mask[2]=1;f.macro(0,"real DemonDen1200 reserve");f.resources(1200);f.v.mask[12]=1;auto a=f.action();ok(a[0]==12&&a[1]==3,"legal reachable expansion");}
    }std::cout<<checks<<" Demon foundation checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
