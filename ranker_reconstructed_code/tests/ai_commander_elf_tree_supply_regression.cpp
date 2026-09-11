#include "ranker_ai_commander.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ranker;
#include "ranker_ai_commander_elf_tree_supply.inc"
static unsigned checks=0;
static void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
static AiObservedUnit unit(u32 id,u32 type,i32 x,i32 y) {
    AiObservedUnit u;u.id=id;u.type_id=type;u.owner_id=1;u.alive=u.visible=u.controlled=true;
    u.health=u.max_health=1000;u.command_state=1;u.x=x;u.y=y;return u;
}
static CommanderServices services() {
    CommanderServices s;s.own_tribe=1;s.public_enemy_tribe=1;s.autoscout=false;
    s.validator=[](const AiSemanticAction&){return true;};
    s.building_prerequisite=[](u32){return true;};
    s.production_info=[](AiProductionRequestKind kind,u32 type){
        CommanderProductionInfo p;
        if(kind==AiProductionRequestKind::research){p.cost=500;return p;}
        p.cost=type==0x10?80:type==0x70?1200:200;
        p.population=type==0x70?11:type==0x72?8:type==0x74?1:type<0x60?1:0;
        return p;
    };
    return s;
}
static AiObservation observation() {
    AiObservation o;o.simulation_frame=3481;o.local_owner=1;o.local_faction=1;o.primary_resources=1664;
    o.population_used=19;o.population_reserved=15;o.population_limit=180;
    o.map_width_tiles=o.map_height_tiles=64;o.start_x=o.start_y=512;
    o.tiles.resize(4096);for(auto& t:o.tiles)t.buildable=t.passable=t.visible=t.explored=true;
    auto h=unit(1,0x70,512,512);h.command_state=0x51;h.command_value=16;h.queued_production_type_id=16;
    h.deferred_command_count=1;h.deferred_commands.push_back({0x10,16,0,0});o.units.push_back(h);
    o.units.push_back(unit(2,0x72,1280,800));o.units.push_back(unit(3,0x72,1280,1024));
    for(u32 i=0;i<14;++i)o.units.push_back(unit(10+i,0x10,800+i*8,768));
    return o;
}
static CommanderView direct(const AiObservation& o) {
    CommanderView v;v.services=services();v.own=o.units;v.queued_population=1;return v;
}
static bool builds_tree(const std::vector<AiSemanticAction>& a){return std::any_of(a.begin(),a.end(),[](const auto& x){return x.kind==AiSemanticActionKind::build&&x.production_id==0x72;});}
int main(){try {
    auto o=observation();auto v=direct(o);
    check(ElfTreeSupplyTransitionNeedsDeferral(o,v),"actual14Wizard/2Tree/HQ capacity19 queued1 transition");
    auto q=o;q.population_used=27;check(!ElfTreeSupplyTransitionNeedsDeferral(q,v),"provided capacity reflected releases");
    q=o;q.population_reserved=23;check(!ElfTreeSupplyTransitionNeedsDeferral(q,v),"same original margin equality still needsTree");
    q=o;q.population_reserved=22;check(!ElfTreeSupplyTransitionNeedsDeferral(q,v),"queued population counted at boundary");
    q=o;q.population_reserved=21;check(ElfTreeSupplyTransitionNeedsDeferral(q,v),"one surplus after full margin defers");
    q=o;q.population_reserved=40;check(!ElfTreeSupplyTransitionNeedsDeferral(q,v),"real shortage despite delayed capacity releases");
    auto w=v;w.own[2].alive=false;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"provider death cannot create fake supply");
    w=v;w.own[2].health=0;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"zeroHP provider excluded");
    w=v;w.own[2].under_construction=true;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"pendingTree is not completed supply");
    w=v;w.own[2].controlled=false;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"uncontrolled provider excluded");
    w=v;w.own[2].owner_id=2;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"ally or enemy provider excluded");
    w=v;w.own[2].type_id=0x74;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"Hall+1 is not assumedTree+8");
    w=v;w.services.production_info={};check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"missing definition callback conservatively releases");
    w=v;w.services.production_info=[](AiProductionRequestKind,u32){CommanderProductionInfo p;p.population=1;return p;};
    check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"uses supplied definition not fabricated11/8");
    w=v;w.pending_counts[18]=50;check(ElfTreeSupplyTransitionNeedsDeferral(o,w),"no artificial pending supply counted");
    w=v;w.services.public_enemy_tribe=2;check(ElfTreeSupplyTransitionNeedsDeferral(o,w),"public Tyrano scope");
    for(u32 own:{0u,2u,3u}){w=v;w.services.own_tribe=own;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"other owned races unchanged");}
    for(u32 enemy:{0u,3u}){w=v;w.services.public_enemy_tribe=enemy;check(ElfTreeSupplyTransitionNeedsDeferral(o,w),"Primitive/Demon also defer covered supply");}
    w=v;w.services.public_enemy_tribe=4;check(!ElfTreeSupplyTransitionNeedsDeferral(o,w),"unknown public opponent unchanged");
    CommanderState state;auto before=o;auto built=BuildCommanderView(state,o,services());
    check(built.own_counts[18]==2&&built.queued_population==1,"realBuildView completedTree/pendingWizard counts");
    check(!state.cached_build_plans[13].empty(),"valid Tree plan remains cached for prompt release");
    check(built.macro_plans[13].empty()&&!built.mask[13],"shared plan and macro mask block duplicateTree");
    check(o.population_used==before.population_used&&o.population_reserved==before.population_reserved,"observed population fields unchanged");
    check(built.pending_counts[18]==0&&state.builds.empty(),"no invented pendingTree reservation");
    auto executing=state;CommanderAction noop{};auto emitted=CommanderExecute(executing,o,built,&noop);
    check(!builds_tree(emitted),"NOOP executor automatic fallback cannot duplicateTree");
    // A now-real shortage must reuse the existing valid cache immediately.
    auto shortage=o;shortage.simulation_frame+=8;shortage.population_used=27;shortage.population_reserved=23;
    auto released=BuildCommanderView(state,shortage,services());
    check(released.mask[13]&&!released.macro_plans[13].empty(),"real shortage releases cached Tree plan next sample");
    executing=state;emitted=CommanderExecute(executing,shortage,released,&noop);
    check(builds_tree(emitted),"real supply shortage retains automatic Tree execution");
    // A provider disappears rather than completing: the old shortage remains.
    auto lost=o;lost.units.erase(lost.units.begin()+2);CommanderState lost_state;
    auto loss_view=BuildCommanderView(lost_state,lost,services());
    check(loss_view.mask[13]&&!loss_view.macro_plans[13].empty(),"provider lost restores genuine shortage planning");
    std::cout<<checks<<" Tree supply transition checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
