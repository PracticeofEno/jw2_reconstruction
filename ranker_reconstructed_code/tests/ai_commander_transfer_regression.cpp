#include "ranker_ai_commander.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ranker;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
AiObservedUnit unit(u32 id,u32 type,i32 x,i32 y,bool own=true) {
    AiObservedUnit u;u.id=id;u.runtime_slot_index=id;u.type_id=type;
    u.owner_id=own?0:1;u.controlled=own;u.visible=true;u.alive=true;
    u.x=x;u.y=y;u.health=u.max_health=300;u.type_flags=0x20;u.command_state=1;
    u.attack_power=30;u.attack_range_base=u.attack_range=50;u.attackable_class_mask=~u32{0};
    u.movement_step_limit=6;u.movement_period=1;
    if(type>=0x60){u.type_flags=0;u.health=u.max_health=1500;}
    return u;
}
AiObservation world() {
    AiObservation o;o.simulation_frame=1;o.local_owner=0;o.local_faction=2;o.local_relation_mask=1;
    o.map_width_tiles=o.map_height_tiles=64;o.tiles.resize(4096);
    for(auto& tile:o.tiles){tile.passable=true;tile.buildable=true;tile.explored=true;tile.visible=true;}
    o.start_x=o.start_y=128;o.primary_resources=0;o.population_used=180;o.population_limit=180;
    o.units={unit(1,0x80,128,128),unit(99,0x60,1700,1400,false)};
    return o;
}
CommanderServices services(bool coordinated=true) {
    CommanderServices s;s.autoscout=false;s.coordinated_transfers=coordinated;
    s.validator=[](const AiSemanticAction&){return true;};return s;
}
CommanderMask conditional(const CommanderView& view,CommanderAction action,std::size_t head) {
    auto mask=view.mask;CommanderLegalHeadMask(view,action,head,mask);return mask;
}
std::vector<u32> members(const CommanderState& state,u32 squad) {
    std::vector<u32> result;
    for(const auto& pair:state.units)if(pair.second.squad==squad&&pair.second.type<0x40&&(pair.second.type&15)!=0)result.push_back(pair.first);
    return result;
}
void populate_main(AiObservation& o) {
    o.units.push_back(unit(10,0x22,300,300));o.units.push_back(unit(11,0x22,320,300));
    for(u32 id=20;id<24;++id){auto u=unit(id,0x22,1280+i32(id-20)*16,1280);u.movement_step_limit=10+id;o.units.push_back(u);}
}

void new_raid_receives_same_decision_mission() {
    auto o=world();populate_main(o);auto sv=services();CommanderState s;
    const auto view=BuildCommanderView(s,o,sv);
    require(!view.mask[61]&&view.mask[40],"pre-decision RAID should be empty and transfer available");
    CommanderAction action{};action[0]=40;action[2]=3;action[3]=u8(CommanderIntent::attack_move);action[4]=6;
    require(conditional(view,action,2)[61],"new RAID cannot receive a same-decision mission");
    require(conditional(view,action,3)[62+u8(CommanderIntent::retreat)],"new RAID center was treated as empty/home");
    const auto packets=CommanderExecute(s,o,view,&action);
    require(s.mask_violations==0&&s.previous_action==action,"legal composed transfer/mission was replaced");
    require(members(s,2)==std::vector<u32>({20,21,22,23}),"fastest four transfer selection changed");
    require(s.squads[2].intent==CommanderIntent::attack_move&&s.squads[2].anchor==6,
        "new RAID did not retain its same-decision intent");
    require(s.squads[2].decision_weight==1320&&s.squads[2].last_intent_frame==1,
        "new RAID decision snapshot used pre-transfer weight/frame");
    require(s.last_transfer_frame[2]==1,"actual transfer clock not recorded");
    for(u32 id:{20u,21u,22u,23u}) {
        const auto found=std::find_if(packets.begin(),packets.end(),[id](const auto& a){return a.unit_ids==std::vector<u32>{id}&&(a.kind==AiSemanticActionKind::move||a.kind==AiSemanticActionKind::attack_move);});
        require(found!=packets.end()&&found->target_x>800&&found->target_y>800,
            "new RAID micro used an empty/pre-transfer center and returned home");
    }
    require(view.squads[2].members.empty()&&!view.mask[61],"projection mutated the original observation");
    o.simulation_frame=9;const auto next=BuildCommanderView(s,o,sv);
    require(next.mask[41],"coordinated transfers introduced a reversal cooldown");
}

void guard_selection_preserves_investment_order() {
    auto o=world();for(u32 id=10;id<16;++id)o.units.push_back(unit(id,0x22,1000+i32(id)*8,1100));
    CommanderState s;auto sv=services();BuildCommanderView(s,o,sv);
    s.units.at(11).investment=900;s.units.at(12).investment=700;
    for(u32 id:{10u,13u,14u,15u})s.units.at(id).investment=10;
    o.simulation_frame=9;const auto view=BuildCommanderView(s,o,sv);
    require(view.transfer_members[0]==std::vector<u32>({11,12})&&view.transfer_investment[0]==1600,
        "transfer plan lost registry investment (including merged-unit investment)");
    CommanderAction action{};action[0]=38;action[2]=2;action[3]=u8(CommanderIntent::hold);action[4]=0;
    require(conditional(view,action,2)[60],"new GUARD is masked");
    CommanderExecute(s,o,view,&action);
    require(members(s,1)==std::vector<u32>({11,12})&&s.squads[1].decision_weight==660,
        "GUARD execution differs from the projected membership/weight");
    require(s.squads[1].last_intent_frame==9,"changed mission frame not stamped");
}

void projected_hunt_weight_and_drained_source_masks() {
    auto o=world();populate_main(o);
    auto neutral=unit(90,0x42,1200,1200,false);neutral.owner_id=8;neutral.attack_power=0;
    neutral.health=neutral.max_health=900;o.units.push_back(neutral);
    CommanderState s;auto sv=services();auto view=BuildCommanderView(s,o,sv);
    CommanderAction action{};action[0]=40;action[2]=1;
    require(!conditional(view,action,3)[62+u8(CommanderIntent::hunt)],
        "HUNT retained the MAIN weight of units transferred out");
    action[2]=3;
    require(!conditional(view,action,3)[62+u8(CommanderIntent::hunt)],
        "HUNT overestimated the new RAID's strength");
    action[2]=0;CommanderExecute(s,o,view,&action);
    o.simulation_frame=9;view=BuildCommanderView(s,o,sv);
    action={};action[0]=41;action[2]=1;action[3]=u8(CommanderIntent::hunt);action[4]=11;
    const auto mask=conditional(view,action,2);
    require(mask[59]&&!mask[61],"empty post-transfer RAID remains selectable");
    require(conditional(view,action,3)[62+u8(CommanderIntent::hunt)],
        "HUNT ignored units transferred into MAIN");
    CommanderExecute(s,o,view,&action);
    require(s.mask_violations==0&&members(s,2).empty()&&members(s,0).size()==6&&s.squads[0].decision_weight==1980,
        "return transfer/HUNT execution differs from its conditional masks");

    auto lone=world();lone.units.push_back(unit(2,0x22,900,900));CommanderState t;
    BuildCommanderView(t,lone,sv);t.units.at(2).squad=1;lone.simulation_frame=9;
    const auto lone_view=BuildCommanderView(t,lone,sv);action={};action[0]=39;action[2]=1;
    const auto return_mask=conditional(lone_view,action,2);
    require(return_mask[59]&&!return_mask[60],"GUARD-to-MAIN projection keeps the drained source selectable");
    CommanderExecute(t,lone,lone_view,&action);
    require(members(t,0)==std::vector<u32>{2}&&members(t,1).empty()&&t.last_transfer_frame[1]==9,
        "GUARD-to-MAIN applied different units or omitted the transfer clock");
}

void reflex_sees_new_guard_members() {
    auto o=world();o.units.push_back(unit(2,0x22,170,170));CommanderState s;
    s.threat={180,180,true};s.threat_frame=1;
    const auto view=BuildCommanderView(s,o,services());CommanderAction action{};action[0]=38;
    const auto mask=conditional(view,action,2);
    require(!mask[59]&&mask[60],"all MAIN members transferred but source is still selectable");
    CommanderExecute(s,o,view,&action);
    require(s.squads[1].intent==CommanderIntent::defend&&s.squads[0].intent==CommanderIntent::hold,
        "defense reflex used pre-transfer squad membership");
}

bool same_action(const AiSemanticAction& a,const AiSemanticAction& b) {
    return a.kind==b.kind&&a.unit_ids==b.unit_ids&&a.target_x==b.target_x&&a.target_y==b.target_y&&
        a.target_unit_id==b.target_unit_id&&a.production_id==b.production_id&&a.queued==b.queued&&
        a.stance_id==b.stance_id&&a.stance_on==b.stance_on;
}
void default_mode_and_nontransfer_behavior_preserved() {
    require(!CommanderServices{}.coordinated_transfers,"legacy transfer behavior no longer defaults off");
    auto o=world();populate_main(o);CommanderState legacy,updated;
    const auto old_view=BuildCommanderView(legacy,o,services(false));
    const auto new_view=BuildCommanderView(updated,o,services(true));
    CommanderAction action{};action[0]=40;
    require(!conditional(old_view,action,2)[61]&&old_view.transfer_members[2].empty(),
        "disabled option changes legacy transfer masks/planning");
    require(conditional(new_view,action,2)[61],"enabled option has no composed transfer action");
    action={};action[2]=1;action[3]=u8(CommanderIntent::attack_move);action[4]=6;
    const auto old_packets=CommanderExecute(legacy,o,old_view,&action);
    const auto new_packets=CommanderExecute(updated,o,new_view,&action);
    require(old_packets.size()==new_packets.size(),"non-transfer packet count changed");
    for(std::size_t i=0;i<old_packets.size();++i)require(same_action(old_packets[i],new_packets[i]),"non-transfer packet/order changed");
    require(legacy.previous_action==updated.previous_action&&legacy.last_transfer_frame==updated.last_transfer_frame,
        "non-transfer action/clock changed");
}
}

int main() {
    try {
        new_raid_receives_same_decision_mission();
        guard_selection_preserves_investment_order();
        projected_hunt_weight_and_drained_source_masks();
        reflex_sees_new_guard_members();
        default_mode_and_nontransfer_behavior_preserved();
    } catch(const std::exception& error) {std::cerr<<"commander transfer: "<<error.what()<<'\n';return 1;}
    std::cout<<"commander coordinated transfer regression passed\n";
}
