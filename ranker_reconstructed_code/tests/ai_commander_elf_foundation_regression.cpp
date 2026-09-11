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
        v.services.own_tribe=1;v.services.public_enemy_tribe=test_opponent;v.services.teacher_variant=kCommanderTeacherElfMatchup;
        v.services.coordinated_transfers=true;v.services.autoscout=false;
        v.services.validator=[](const AiSemanticAction&){return true;};
        v.services.building_prerequisite=[](u32){return true;};
        v.services.production_info=[](AiProductionRequestKind k,u32 t){CommanderProductionInfo p;p.cost=k==AiProductionRequestKind::research?500:t==0x70?1200:t==0x72?200:t>=0x60?400:t==0x10?80:200;p.population=t==0x70?11:t==0x72?8:t<0x60?1:0;return p;};
        v.mask.fill(1);for(u32 n=1;n<96;++n)v.mask[n]=0;
        v.input.vector[16]=v.input.vector[17]=1;v.input.vector[22]=float(initial_workers)/180;v.input.vector[23]=float(initial_workers+16)/180;v.input.vector[24]=1;v.input.vector[47]=0;
        v.anchors[0]=v.anchors[1]=v.anchors[2]={512,512,true};v.anchors[6]={3000,1000,true};v.anchors[12]=v.anchors[6];
        v.anchors[3]={1600,512,true};v.input.vector[260]=1;
        s.build_component_width=128;s.build_components.assign(128*128,1);
        resources(200);add(0x70,1);add(0x10,initial_workers);
    }
    void resources(float r){v.input.vector[8]=v.input.vector[553]=std::log1p(r)/std::log1p(20000.f);}
    void add(u32 type,u32 n,u8 q=0,bool wounded=false){for(u32 k=0;k<n;++k){
        AiObservedUnit u;u.id=id++;u.type_id=type;u.owner_id=0;u.controlled=u.alive=u.visible=true;u.command_state=1;
        u.x=512+i32((u.id%5)*16);u.y=512+i32((u.id/5%5)*16);u.health=u.max_health=type>=0x60?2000:210;
        if(type<0x60&&(type&15u)){u.type_flags=0x20;u.attack_power=type==0x13?30:20;u.defense_power=1;
            u.attack_range=u.attack_range_vs_air=u.attack_range_base=type==0x13?50:240;u.attackable_class_mask=type==0x17?8:31;u.movement_step_limit=4;
            if(wounded)u.health=42;
        }
        v.own.push_back(u);auto& st=s.units[u.id];st.squad=type>=0x60||(type&15u)==0?3:q;st.type=type;st.completed=true;
        st.x=u.x;st.y=u.y;st.health=u.health;st.last_progress=v.frame;st.applied_intent_serial=s.squads[q].serial;
    }recount();}
    void recount(){v.own_counts.fill(0);v.workers=v.army_count=0;v.own_weight=0;v.squads={};
        for(const auto& u:v.own){if(!u.alive||u.under_construction)continue;u32 ix=u.type_id<0x60?u.type_id-0x10:16+u.type_id-0x70;
            if(ix<48)++v.own_counts[ix];if(u.type_id==0x10)++v.workers;
            if(u.type_flags&0x20){++v.army_count;v.own_weight+=u.health+u.attack_power+u.defense_power;auto& sq=v.squads[std::min<u8>(2,s.units.at(u.id).squad)];
                sq.members.push_back(u.id);sq.center.x+=u.x;sq.center.y+=u.y;sq.weight+=u.health+u.attack_power+u.defense_power;}}
        for(u32 q=0;q<3;++q){auto& sq=v.squads[q];if(!sq.members.empty()){sq.center.x/=i32(sq.members.size());sq.center.y/=i32(sq.members.size());sq.center.valid=true;}
            sq.intent=s.squads[q].intent;sq.anchor=s.squads[q].anchor;sq.roe=s.squads[q].roe;}
    }
    void settled(bool researched=true){s.last_research[11]=1;add(0x74,3);add(0x75,1);add(0x10,20-v.workers);v.frame=9001;s.decision_count=101;
        // Isolate expansion/mission checks from the separately tested research budget.
        if(test_opponent==2&&researched){add(0x78,1);s.last_research[13]=s.last_research[14]=1;}}
    CommanderAction action(){auto a=CommanderTeacherAction(s,v);std::cout<<"action";for(u8 x:a)std::cout<<','<<unsigned(x);std::cout<<'\n';return a;}
    void macro(u32 expected,const char* why){ok(action()[0]==expected,why);}
    void enemy(i32 x,i32 y,u32 count=1,u32 hp=210){for(u32 k=0;k<count;++k){AiObservedUnit e;e.id=10000+u32(v.visible_enemies.size());e.type_id=0x11;e.owner_id=1;e.type_flags=0x20;e.alive=e.visible=true;e.health=e.max_health=hp;e.attack_power=20;e.defense_power=1;e.x=x+k*8;e.y=y;e.attackable_class_mask=31;v.visible_enemies.push_back(e);}}
    void expansion(){s.expansion.has_target=true;s.expansion.target_index=0;AiBerryCluster c;c.site_x=1600;c.site_y=512;c.site_explored=true;c.known_amount=20000;s.expansion.clusters={c};v.hq_build_plans[3].push_back({});}
    AiObservation observation(){AiObservation o;o.simulation_frame=v.frame;o.local_owner=0;o.map_width_tiles=o.map_height_tiles=128;o.start_x=o.start_y=512;o.tiles.resize(128*128);
        for(auto& t:o.tiles)t.passable=t.explored=t.visible=true;o.units=v.own;return o;}
};
int main(int argc,char** argv){try {
    if(argc>1)test_opponent=u32(std::stoul(argv[1]));ok(test_opponent<4,"all four public opponent scopes");
    {Fixture f;f.v.mask[15]=1;f.macro(0,"14 completed workers reserve economy before first Hall");}
    {Fixture f;f.resources(500);f.v.mask[22]=f.v.mask[15]=1;f.macro(22,"funded economy11 macro22 first");}
    {Fixture f;f.resources(500);f.v.mask[15]=1;f.macro(15,"funded masked research releases");}
    {Fixture f;f.v.income_rate=0;f.v.mask[15]=1;f.macro(15,"no income research releases");}
    {Fixture f;f.v.frame=6001;f.v.mask[15]=1;f.macro(15,"opening research deadline");}
    {Fixture f(12);f.v.pending_counts[0]=2;f.v.mask[22]=f.v.mask[15]=1;f.macro(0,"pendingworkers not completed14 and no premature Hall");}
    {Fixture f(8);f.v.pending_counts[0]=2;f.resources(400);f.v.mask[15]=1;f.macro(0,"eight completed pluspending cannot divert to firstHall");}
    {Fixture f(13);f.v.mask[1]=f.v.mask[15]=1;f.macro(1,"thirteenth tofourteenth Wizard beforeHall");f.v.pending_counts[0]=1;f.macro(0,"completed pluspending14 caps openingqueue");}
    {Fixture f;f.v.own[0].command_state=0x4d;f.v.own[0].command_value=11;f.v.mask[15]=1;f.macro(15,"active economy releases");}
    {Fixture f;f.v.own[0].deferred_commands.push_back({0x17,11,0,0});f.v.mask[15]=1;f.macro(15,"queued economy releases");}
    {Fixture f;f.s.last_research[11]=1;f.v.mask[15]=1;f.macro(15,"completed economy releases");}
    {Fixture f;CommanderReceipt r;r.frame=f.v.frame;r.order.kind=AiSemanticActionKind::research;r.order.production_id=11;r.order.unit_ids={1};f.s.receipts.push_back(r);f.v.mask[15]=1;f.macro(15,"fresh live receipt releases");r.frame=f.v.frame-16;f.s.receipts={r};f.macro(0,"stale receipt retries budget");}
    {Fixture f;f.s.last_research[11]=1;f.resources(399);f.v.mask[1]=1;f.macro(0,"first Hall actual400 reserve");}
    {Fixture f;f.s.last_research[11]=1;f.v.pending_counts[20]=1;f.v.mask[1]=1;f.macro(0,"first Hall pending doesnotgrow beyond14 beforeBlue");}
    {Fixture f;f.s.last_research[11]=1;f.add(0x74,1);f.add(0x10,2);f.v.mask[3]=1;f.macro(0,"second Hall before unboundedRed");f.resources(400);f.v.mask[15]=1;f.macro(15,"funded secondHall");}
    {Fixture f;f.s.last_research[11]=1;f.add(0x74,1);f.add(0x10,2);f.v.pending_counts[20]=1;f.v.mask[16]=1;f.macro(16,"secondHall pending releases BlueHall");}
    {Fixture f;f.s.last_research[11]=1;f.add(0x74,2);f.v.pending_counts[21]=1;f.v.mask[1]=1;f.macro(1,"Blue pending grows workers16");}
    {Fixture f;f.s.last_research[11]=1;f.add(0x74,2);f.add(0x75,1);f.add(0x10,4);f.v.mask[3]=f.v.mask[2]=1;f.macro(3,"fourRed floor");f.v.pending_counts[1]=4;f.macro(2,"twoRanger floor after fourRed pending");}
    {Fixture f;f.s.last_research[11]=1;f.add(0x74,2);f.add(0x75,1);f.add(0x10,4);f.add(0x11,4);f.add(0x13,2);f.v.mask[1]=f.v.mask[3]=1;f.macro(1,"sixbasic demand before workers20");}
    {Fixture f;f.s.last_research[11]=1;f.add(0x74,2);f.add(0x75,1);f.add(0x10,8);f.add(0x11,6);f.add(0x13,2);f.v.mask[3]=1;f.macro(0,"thirdHall eightcompleted floor saves");f.v.pending_counts[20]=1;f.macro(3,"pending third releases units");}
    {Fixture f;f.settled();f.add(0x11,6);f.add(0x13,2);f.v.mask[15]=f.v.mask[3]=1;f.macro(3,"threeHall hardgoal");}
    {Fixture f;f.settled();f.add(0x11,6);f.add(0x13,2);f.v.pending_counts[1]=6;f.v.pending_counts[3]=2;f.v.mask[3]=f.v.mask[2]=1;f.macro(0,"two pending per producer queuecap");f.v.pending_counts[3]=1;f.macro(2,"other legal queue remains usable");}
    {Fixture f;f.settled();f.add(0x11,40);f.add(0x13,8);f.v.pending_counts[1]=6;f.v.pending_counts[3]=2;for(u32 x:{17u,19u,21u,25u,26u,43u,9u})f.v.mask[x]=1;f.macro(0,"no expensive support fallback");}
    {Fixture f;f.v.input.vector[24]=2.f/16;f.v.mask[13]=f.v.mask[22]=1;f.macro(13,"urgent supply precedes economy");}
    {Fixture f;f.s.last_research[11]=1;f.add(0x74,2);f.add(0x75,1);f.add(0x72,2);f.resources(100);f.v.queued_population=1;f.v.input.vector[22]=18.f/180;f.v.input.vector[23]=19.f/180;f.v.input.vector[24]=1.f/16;f.v.mask[1]=1;f.macro(1,"covered stale capacity cannot reserveTree200 instead of nextWizard");f.v.own.back().alive=false;f.macro(0,"provider lost restores realTree saving");}
    {Fixture f;f.add(0x74,1);f.enemy(700,512);f.v.mask[3]=f.v.mask[22]=1;f.macro(3,"homecontact releases savings forRed");}
    {Fixture f(10);f.enemy(700,512);f.v.mask[1]=f.v.mask[15]=1;f.macro(15,"actual home contact builds firstHall before14/research");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5);f.expansion();f.resources(1100);f.v.mask[3]=1;f.macro(0,"HQ actual1200reserve");f.resources(1200);f.macro(3,"funded illegal HQ releases");f.v.mask[12]=1;auto a=f.action();ok(a[0]==12&&a[1]==3,"funded legal HQ anchor3");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5);f.expansion();f.s.expansion.clusters[0].site_blocked=true;f.v.mask[3]=1;f.macro(3,"blocked expansion no reserve");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5);f.expansion();f.s.build_components[16*128+50]=2;f.v.mask[3]=1;f.macro(3,"unreachable expansion no reserve");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5);f.expansion();f.v.income_rate=0;f.v.mask[3]=1;f.macro(3,"noincomeHQ reserve release");f.v.income_rate=1;f.v.frame=50001;f.macro(3,"HQreserve deadline");}
    {Fixture f;f.settled();f.add(0x11,6);f.add(0x13,2);f.expansion();f.v.input.vector[16]=.34f;f.v.mask[3]=1;f.macro(0,"depletedHQ relaxes army20to8");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5);auto a=f.action();ok(a[2]==1&&a[3]==2&&a[4]==6&&a[5]==0&&a[7]==0,"ground20 MAIN aggressive offense");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,4);f.add(0x17,10);auto a=f.action();ok(a[3]!=2,"aironlyMana cannot pad ground20");}
    {Fixture f;f.settled();f.add(0x11,20);f.enemy(700,512,30);for(auto& e:f.v.visible_enemies)e.attackable_class_mask=8;auto a=f.action();ok(a[3]==2&&a[4]==6,"aironly enemy excluded from directground homeproxy");for(auto& e:f.v.visible_enemies){e.attackable_class_mask=31;e.attack_power=0;}a=f.action();ok(a[3]==2&&a[4]==6,"unarmed enemy excluded from directground homeproxy");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5,1);f.v.mask[39]=1;f.v.transfer_members[1]=f.v.squads[1].members;auto a=f.action();ok(a[0]==39&&a[2]==1&&a[3]==2,"same action coordinated consolidation20");f.v.services.coordinated_transfers=false;a=f.action();ok(a[3]!=2,"legacy transfer cannot fabricate projected20");}
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5);for(auto& u:f.v.own)if(u.type_flags&0x20)u.health=100;f.recount();ok(f.action()[3]!=2,"low pooledHP blocks newoffense");}
    {Fixture f;f.settled();f.add(0x11,20);for(auto& u:f.v.own)if(u.type_flags&0x20)u.x=(u.id%2)?100:1400;f.recount();ok(f.action()[3]!=2,"fragmented20 notready");}
    {Fixture f;f.settled();f.add(0x11,6);f.enemy(820,540,2);f.v.anchors[7]={824,540,true};auto a=f.action();ok(a[3]==2&&a[4]==7&&a[5]==0,"local threat attacks below20");}
    {Fixture f;f.settled();f.add(0x11,6);f.enemy(820,540);f.enemy(3000,3000);f.v.anchors[7]={1900,1770,true};auto a=f.action();ok(a[4]!=7,"remote globalcentroid not defense destination");}
    {Fixture f;f.settled();f.add(0x11,8);f.s.squads[0].intent=CommanderIntent::attack_move;f.s.squads[0].anchor=6;f.s.squads[0].roe=CommanderRoe::normal;f.v.input.vector[47]=1;auto a=f.action();ok(a[3]==2&&a[4]==6&&a[5]==0,"no entryreadiness recheck during offense");}
    {Fixture f;f.settled();f.add(0x11,20);for(auto& u:f.v.own)if(u.type_flags&0x20){u.x+=1800;u.y+=1800;}f.recount();f.s.squads[0].intent=CommanderIntent::attack_move;f.s.squads[0].anchor=6;f.enemy(2500,2500,30,300);auto a=f.action();ok(a[3]==u8(CommanderIntent::retreat)&&a[4]==0,"remote visibleground1point5 advantage releasescommitment toretreat");}
    {Fixture f;f.settled();f.add(0x11,20);f.v.squads[0].center={2900,1000,true};auto a=f.action();ok(a[3]!=3,"no automaticSIEGE conversion");}
    {Fixture f;f.settled();f.add(0x11,8);f.v.anchors[6].valid=false;f.v.input.vector[47]=1;auto a=f.action();ok(a[3]==6&&a[4]==12,"bounded singlefastest scout");f.s.squads[0].intent=CommanderIntent::scout;f.s.squads[0].anchor=12;f.s.squads[0].changed_decision=f.s.decision_count-20;a=f.action();ok(a[3]==0,"scout20decisions ends");}
    {Fixture f;f.settled();f.add(0x11,6);f.s.decision_count=104;f.enemy(700,520);AiSemanticAction o;o.kind=AiSemanticActionKind::use_ability;o.ability_id=1;o.unit_ids={f.v.squads[0].members[0]};o.target_unit_id=o.unit_ids[0];f.v.mask[64]=1;f.v.macro_plans[64]={o};f.macro(0,"Thunder self target rejected");o.target_unit_id=f.v.visible_enemies[0].id;f.v.macro_plans[64]={o};f.macro(64,"Thunder visible hostile accepted");o.ability_id=20;f.v.macro_plans[64]={o};f.macro(0,"no offensive Interlace");o.ability_id=4;f.macro(0,"no selfTeleport scan");}
    // Actual unchanged executor packets, beyond logged intents. All combat
    // sources are initialized and public; open map is a controlled fixture.
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5);f.add(0x11,4,0,true);auto a=f.action();auto o=f.observation();
        auto out=CommanderExecute(f.s,o,f.v,&a);unsigned forward=0,home=0;
        for(const auto& q:out)if(q.kind==AiSemanticActionKind::attack_move&&q.target_x>700)++forward;else if(q.kind==AiSemanticActionKind::move&&q.target_x==512&&q.target_y==512)++home;
        ok(forward>=20,"healthy20 plus4wounded emits forward attack packets");ok(home==0,"aggressive mixedHP emits no individualhome moves");
    }
    {Fixture f;f.settled();f.add(0x11,15);f.add(0x13,5,1);f.add(0x11,4,0,true);f.v.mask[39]=1;f.v.transfer_members[1]=f.v.squads[1].members;const auto transferred=f.v.transfer_members[1];auto a=f.action();auto o=f.observation();auto out=CommanderExecute(f.s,o,f.v,&a);unsigned joined=0;for(u32 id:transferred){bool commanded=false;for(const auto& q:out)if(q.kind==AiSemanticActionKind::attack_move&&q.target_x>700&&std::find(q.unit_ids.begin(),q.unit_ids.end(),id)!=q.unit_ids.end())commanded=true;joined+=commanded;}ok(joined==5,"coordinated transferredRangers receive same-tickforwardpackets");}
    {Fixture f;f.settled();f.add(0x11,20);f.s.squads[0].intent=CommanderIntent::attack_move;f.s.squads[0].anchor=6;f.s.squads[0].roe=CommanderRoe::aggressive;
        f.s.threat={600,512,true};f.s.threat_frame=f.v.frame;f.v.anchors[10]=f.s.threat;auto a=f.action();ok(a[2]==1&&a[3]==2,"same mission explicit MAIN protects assetreflex");
        auto o=f.observation();CommanderExecute(f.s,o,f.v,&a);ok(f.s.squads[0].intent==CommanderIntent::attack_move,"same tick defense does not overwriteoffense");}
    if(test_opponent==2){
        {Fixture f;f.settled(false);f.add(0x11,19);f.v.mask[19]=f.v.mask[3]=1;f.macro(3,"under20 basic preserves production");}
        {Fixture f;f.settled(false);f.add(0x11,20);f.v.mask[19]=f.v.mask[3]=1;f.macro(19,"20 basic funds Library");}
        {Fixture f;f.settled(false);f.add(0x11,20);f.v.pending_counts[24]=1;f.v.mask[3]=1;f.macro(3,"pending Library releases production");}
        {Fixture f;f.settled(false);f.add(0x11,20);f.add(0x78,1);f.v.mask[25]=f.v.mask[26]=1;f.macro(25,"attack1 first");f.s.last_research[13]=1;f.macro(26,"defense1 after attack1");f.s.last_research[14]=1;f.macro(0,"no higher upgrades");}
        {Fixture f;f.settled(false);f.add(0x11,20);f.add(0x78,1);f.v.own.back().command_state=0x4d;f.v.own.back().command_value=13;f.v.mask[26]=1;f.macro(26,"active attack research releases defense choice");}
        {Fixture f;f.settled(false);f.add(0x11,20);f.enemy(700,512);f.v.mask[19]=f.v.mask[3]=1;f.macro(3,"home contact preserves army");}
        {Fixture f;f.settled(false);f.add(0x11,20);f.v.services.public_enemy_tribe=1;f.v.mask[19]=f.v.mask[3]=1;f.macro(3,"mirror unchanged");}
    }
    std::cout<<checks<<" integrated foundation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
