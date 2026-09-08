#include "ranker_ai_commander.h"
#include "ranker_ai_commander_rollout.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ranker;
namespace {
void require(bool okay,const char* message) {if(!okay)throw std::runtime_error(message);}
void close(float actual,float expected,const char* message) {
    require(std::abs(actual-expected)<1e-7f,message);
}
AiObservation world(u32 frame=1) {
    AiObservation o;o.simulation_frame=frame;o.local_owner=0;o.local_faction=2;o.local_relation_mask=1;
    o.map_width_tiles=o.map_height_tiles=32;o.tiles.resize(1024);
    for(auto& tile:o.tiles){tile.passable=true;tile.buildable=true;tile.explored=true;tile.visible=true;}
    o.start_x=320;o.start_y=320;o.population_limit=180;
    for(u32 id=1;id<=4;++id) {
        AiObservedUnit u;u.id=id;u.runtime_slot_index=id;u.type_id=id==1?0x80:0x22;
        u.owner_id=0;u.controlled=true;u.visible=true;u.alive=true;
        u.x=320+i32(id)*16;u.y=320;u.health=u.max_health=300;
        u.type_flags=id==1?0:0x20;u.command_state=1;u.attack_power=30;
        u.attack_range_base=u.attack_range=50;u.movement_step_limit=6;
        o.units.push_back(u);
    }
    return o;
}
CommanderServices services(u32 variant=0) {
    CommanderServices sv;sv.autoscout=false;sv.teacher_variant=variant;
    sv.validator=[](const AiSemanticAction&){return true;};return sv;
}
void context_exposes_history_without_changing_existing_observation() {
    auto o=world(3001);const auto sv=services();CommanderState baseline;
    BuildCommanderView(baseline,o,sv);
    auto recent=baseline;recent.last_transfer_frame={1,1001,2001,3001};
    const auto before=BuildCommanderView(baseline,o,sv),after=BuildCommanderView(recent,o,sv);
    require(std::equal(before.input.vector.begin(),before.input.vector.begin()+528,after.input.vector.begin()),
            "transfer history changed an existing actor feature");
    require(before.input.map==after.input.map&&before.mask==after.mask,"transfer context changed map or legal actions");
    for(u32 i=0;i<4;++i) {
        close(before.input.vector[528+i],3001.0f/6000,"never-transfer clock lost elapsed episode frames");
        close(after.input.vector[528+i],float(3000-1000*i)/6000,"transfer clocks are not independent");
        require(before.input.vector[532+i]==0&&after.input.vector[532+i]==1,"transfer performed flags are absent");
    }
    o.simulation_frame=12001;
    baseline.last_transfer_frame={0,1,12001,13001};
    const auto bounded=BuildCommanderView(baseline,o,sv);
    const float clocks[4]={1,1,0,0},flags[4]={0,1,1,1};
    for(u32 i=0;i<4;++i) {
        require(bounded.input.vector[528+i]==clocks[i],"elapsed transfer input is not bounded");
        require(bounded.input.vector[532+i]==flags[i],"saturated clocks conflate never and performed transfers");
    }
}
void transfer_context_is_pre_decision_and_uses_executed_actions() {
    auto o=world();const auto sv=services();CommanderState s;
    const auto before=BuildCommanderView(s,o,sv);
    CommanderAction transfer{};transfer[0]=38;
    require(before.mask[38],"test MAIN has no legal transfer");
    CommanderExecute(s,o,before,&transfer);
    require(before.input.vector[532]==0,"execution mutated its recorded pre-decision view");
    require(s.last_transfer_frame[0]==1,"executed transfer did not start the clock");
    auto after=BuildCommanderView(s,o,sv);
    require(after.input.vector[528]==0&&after.input.vector[532]==1,"same-frame post-execution clock is wrong");
    o.simulation_frame=33;after=BuildCommanderView(s,o,sv);
    close(after.input.vector[528],32.0f/6000,"next decision does not see elapsed transfer time");
    CommanderAction none{};CommanderExecute(s,o,after,&none);
    require(s.last_transfer_frame[0]==1,"unrelated decision reset transfer history");
    CommanderState empty_guard;o=world();const auto blocked=BuildCommanderView(empty_guard,o,sv);
    require(!blocked.mask[39],"test GUARD unexpectedly has units");
    transfer[0]=39;CommanderExecute(empty_guard,o,blocked,&transfer);
    require(empty_guard.last_transfer_frame[1]==0,"masked transfer was recorded as executed");
}
void strategy_is_actor_visible_and_preserves_default() {
    auto o=world();CommanderState base;BuildCommanderView(base,o,services());
    auto variant_state=base;
    const auto standard=BuildCommanderView(base,o,services());
    const auto variant=BuildCommanderView(variant_state,o,services(7));
    require(std::equal(standard.input.vector.begin(),standard.input.vector.begin()+536,variant.input.vector.begin()),
            "teacher style altered an existing observation or transfer input");
    require(standard.input.map==variant.input.map&&standard.mask==variant.mask,"teacher style changed map or legal actions");
    const float defaults[6]={0.25f,0,1,0,0,0};
    for(u32 i=0;i<6;++i)close(standard.input.vector[536+i],defaults[i],"default teacher context changed");
    const auto p=CommanderTeacherVariant(7);
    const float expected[6]={float(p.opening_velocis)/8,float(p.tower_frame)/5000,p.attack_ratio/1.5f,
        float(p.expansion_shift)/3000,float(p.harass_period)/6000,float(p.target_priority)/2};
    for(u32 i=0;i<6;++i)close(variant.input.vector[536+i],expected[i],"teacher parameter has incorrect feature position or normalization");
    require(!std::equal(standard.input.vector.begin()+536,standard.input.vector.end(),variant.input.vector.begin()+536),
            "different teacher strategies remain indistinguishable");
}
}
int main() {
    static_assert(kCommanderVectorSize==606&&kCommanderRolloutFormatVersion==4&&kCommanderRolloutRecordBytes==4446);
    try {
        context_exposes_history_without_changing_existing_observation();
        transfer_context_is_pre_decision_and_uses_executed_actions();
        strategy_is_actor_visible_and_preserves_default();
    } catch(const std::exception& e){std::cerr<<"ai_commander_context_regression: "<<e.what()<<'\n';return 1;}
    std::cout<<"ai_commander_context_regression: 3 groups passed\n";return 0;
}
