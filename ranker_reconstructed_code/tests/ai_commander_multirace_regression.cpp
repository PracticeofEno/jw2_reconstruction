#include "ranker_ai_commander.h"
#include "ranker_ai_commander_races.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace ranker;
void require(bool ok,const char* message) { if(!ok){std::cerr<<message<<'\n';std::exit(1);} }

AiObservedUnit make_unit(u32 id,u32 type,i32 x,i32 y) {
    AiObservedUnit u;u.id=id;u.runtime_slot_index=id;u.type_id=type;u.owner_id=1;
    u.controlled=u.visible=u.alive=true;u.x=x;u.y=y;u.health=u.max_health=1000;
    u.command_state=1;u.type_flags=type<0x60?0x20:0;u.attack_power=20;
    return u;
}

CommanderView teacher_view(u32 tribe) {
    CommanderView v;v.services.own_tribe=tribe;v.frame=6400;
    v.services.teacher_variant=kCommanderTeacherRaceRoles;
    v.workers=20;v.worker_cap=32;v.army_count=20;v.own_weight=5000;
    v.mask.fill(true);
    std::fill(v.mask.begin(),v.mask.begin()+kCommanderMacroCount,false);v.mask[0]=true;
    for(auto& point:v.anchors){point.valid=true;point.x=point.y=320;}
    v.input.vector[8]=std::log1p(800.0f)/std::log1p(20000.0f);
    v.input.vector[16]=1;v.input.vector[24]=1;v.input.vector[513]=1;
    v.own_counts[16]=1;v.own_counts[19]=2; // HQ and two towers.
    v.squads[0].weight=5000;
    for(u32 id=1;id<=20;++id)v.squads[0].members.push_back(id);
    return v;
}

void teacher_role_regressions() {
    CommanderState state;state.decision_count=101;
    auto elf=teacher_view(1);
    elf.own_counts[20]=1;elf.own_counts[21]=2;elf.own_counts[22]=1;elf.own_counts[24]=1;
    elf.own_counts[1]=20; // RedElves, the Elf opening's ranged core.
    elf.own_counts[5]=2;  // Two Unicorns, with zero normal offense.
    auto action=CommanderTeacherAction(state,elf);
    require(!(action[2]==1&&action[3]==u8(CommanderIntent::attack_move)),
        "Unicorn healers incorrectly authorized a plateau siege");
    elf.own_counts[7]=2; // ManaSpread, actual 400-range siege damage.
    action=CommanderTeacherAction(state,elf);
    require(action[2]==1&&action[3]==u8(CommanderIntent::attack_move),
        "actual Elf siege force did not authorize the prepared attack");
    elf.own_counts[1]=0;elf.own_counts[3]=4;
    elf.mask[2]=elf.mask[3]=true;
    action=CommanderTeacherAction(state,elf);
    require(action[0]==3,"Elf opening failed to reinforce its RedElf core");

    auto demon=teacher_view(3);demon.own_counts[1]=10;demon.own_counts[20]=2;
    demon.mask[5]=demon.mask[8]=true;
    action=CommanderTeacherAction(state,demon);
    require(action[0]==8,"Demon combat-air fallback recruited a zero-attack DeathEye");
    demon.services.teacher_variant=0;
    action=CommanderTeacherAction(state,demon);
    require(action[0]==5,"experimental race recipe changed the original teacher variant");
    const auto original=CommanderTeacherVariant(7);
    const auto experimental=CommanderTeacherVariant(7|kCommanderTeacherRaceRoles|kCommanderTeacherRaceEconomy);
    require(original.opening_velocis==experimental.opening_velocis&&
            original.tower_frame==experimental.tower_frame&&
            original.attack_ratio==experimental.attack_ratio&&
            original.expansion_shift==experimental.expansion_shift&&
            original.harass_period==experimental.harass_period&&
            original.target_priority==experimental.target_priority,
            "race recipe flag changed the underlying teacher strategy");

    auto primitive=teacher_view(0);primitive.own_counts[1]=20;
    primitive.own_counts[20]=1;primitive.own_counts[21]=2;primitive.own_counts[24]=1;
    primitive.mask[3]=primitive.mask[19]=true;
    action=CommanderTeacherAction(state,primitive);
    require(action[0]==19,"Primitive siege plan omitted BowMachine's Blacksmith prerequisite");

    for(u32 tribe:{0u,1u,3u}) {
        auto economy=teacher_view(tribe);economy.frame=9000;
        economy.services.teacher_variant=kCommanderTeacherRaceEconomy;
        economy.own_counts[20]=3;economy.own_counts[21]=1;
        const auto main_type=kCommanderRaceActions[tribe][3].type;
        economy.own_counts[main_type&15]=20;
        economy.mask[3]=true;
        economy.input.vector[8]=std::log1p(1100.0f)/std::log1p(20000.0f);
        action=CommanderTeacherAction(state,economy);
        require(action[0]==0,"non-Tyrano teacher spent its HQ savings before reaching 1200");
        economy.input.vector[8]=std::log1p(1300.0f)/std::log1p(20000.0f);
        economy.mask[12]=true;
        action=CommanderTeacherAction(state,economy);
        require(action[0]==12,"affordable non-Tyrano expansion was not constructed");
        economy.services.teacher_variant=0;economy.mask[12]=false;
        economy.input.vector[8]=std::log1p(1100.0f)/std::log1p(20000.0f);
        action=CommanderTeacherAction(state,economy);
        require(action[0]==3,"optional economy correction changed the default teacher");
    }
}

void hq_retry_clearance_regression() {
    for(u32 tribe=0;tribe<4;++tribe) {
        AiObservation o;o.local_owner=1;o.simulation_frame=1;
        o.map_width_tiles=o.map_height_tiles=80;o.tiles.resize(80*80);
        for(auto& t:o.tiles)t.passable=t.buildable=t.visible=t.explored=true;
        o.start_x=o.start_y=320;o.primary_resources=20000;
        o.population_used=180;o.population_reserved=2;
        for(u32 y=50;y<54;++y)for(u32 x=50;x<58;++x) {
            auto& t=o.tiles[y*80+x];t.resource_amount=4000;t.terrain_flags=0x100;
            t.passable=t.buildable=false;
        }
        const u32 base=0x60+16*tribe;const auto fp=AiBuildingFootprintOf(base);
        o.units={make_unit(1,base,320,320),make_unit(2,16*tribe,600,400)};
        CommanderServices sv;sv.own_tribe=tribe;sv.autoscout=false;
        sv.validator=[](const AiSemanticAction&){return true;};
        CommanderState state;BuildCommanderView(state,o,sv);
        CommanderBuildReservation reservation;
        reservation.order.kind=AiSemanticActionKind::build;reservation.order.unit_ids={2};
        reservation.order.production_id=base;
        reservation.order.target_x=(50-i32(fp.width)-4)*32;
        reservation.order.target_y=50*32;
        reservation.source_generation=state.units.at(2).generation;
        reservation.cost=tribe==2?1000:1200;reservation.issued_frame=1;
        state.builds.push_back(reservation);o.simulation_frame=25;
        const auto view=BuildCommanderView(state,o,sv);
        const auto orders=CommanderExecute(state,o,view);bool retried=false;
        for(const auto& a:orders)if(a.kind==AiSemanticActionKind::build&&a.production_id==base) {
            retried=true;const i32 tx=a.target_x/32,ty=a.target_y/32;
            // Retry shifts the old site by +64,+32. The engine checks four
            // tiles around EVERY HQ footprint, including depleted berries.
            for(i32 y=ty-4;y<ty+i32(fp.height)+4;++y)
                for(i32 x=tx-4;x<tx+i32(fp.width)+4;++x)
                    if(x>=0&&y>=0&&x<80&&y<80)
                        require((o.tiles[y*80+x].terrain_flags&0x700)!=0x100,
                            "HQ retry violates the engine's four-tile berry clearance");
        }
        require(retried,"fixture did not exercise HQ retry publication");
    }
}

void elf_matchup_teacher_regressions() {
    CommanderState state;state.decision_count=101;
    auto elf=teacher_view(1);
    elf.workers=8;elf.army_count=0;elf.own_counts[0]=8;
    elf.input.vector[8]=std::log1p(300.0f)/std::log1p(20000.0f);elf.mask[1]=true;
    for(u32 opponent=1;opponent<3;++opponent) {
        elf.services.public_enemy_tribe=opponent;elf.services.teacher_variant=0;
        const auto expected=CommanderTeacherAction(state,elf);
        elf.services.teacher_variant=kCommanderTeacherElfMatchup;
        require(CommanderTeacherAction(state,elf)==expected,"Elf recipe changed an unrelated opponent matchup");
    }
    for(u32 opponent:{0u,3u}) {
        elf.services.public_enemy_tribe=opponent;
        elf.pending_counts[20]=0;elf.mask[15]=false;
        elf.input.vector[8]=std::log1p(300.0f)/std::log1p(20000.0f);
        require(CommanderTeacherAction(state,elf)[0]==0,"Elf matchup teacher spent first-hall savings on workers");
        elf.input.vector[8]=std::log1p(450.0f)/std::log1p(20000.0f);elf.mask[15]=true;
        require(CommanderTeacherAction(state,elf)[0]==15,"Elf matchup teacher did not select RedHall");
        elf.pending_counts[20]=1;elf.mask[15]=false;
        require(CommanderTeacherAction(state,elf)[0]==1,"Funded Elf hall stalled worker production");
    }
    const auto plain=CommanderTeacherVariant(0),tagged=CommanderTeacherVariant(kCommanderTeacherElfMatchup);
    require(plain.opening_velocis==tagged.opening_velocis&&plain.tower_frame==tagged.tower_frame&&
        plain.attack_ratio==tagged.attack_ratio&&plain.expansion_shift==tagged.expansion_shift&&
        plain.harass_period==tagged.harass_period&&plain.target_priority==tagged.target_priority,
        "Elf recipe tag leaked into actor strategy context");
    for(u32 race:{0u,2u,3u}) {
        auto other=teacher_view(race);other.services.public_enemy_tribe=3;
        other.mask[5]=other.mask[8]=true;
        const auto expected=CommanderTeacherAction(state,other);
        other.services.teacher_variant|=kCommanderTeacherElfMatchup;
        require(CommanderTeacherAction(state,other)==expected,"Elf recipe changed another race's teacher");
    }
}

void elf_last_army_regression() {
    // Reproduce a completed native match: 49 survivors, no workers or income,
    // and MAIN already attacking. Normal ROE kept half the army at home.
    auto v=teacher_view(1);v.services.teacher_variant=kCommanderTeacherElfMatchup;
    v.services.public_enemy_tribe=2;v.frame=32001;v.workers=0;v.army_count=49;
    v.own_weight=9800;v.squads[0].weight=8000;
    v.squads[0].center={1000,1000,true};v.anchors[6]={3200,3200,true};
    v.own_counts[1]=40;v.own_counts[18]=4;v.own_counts[20]=2;v.own_counts[21]=1;
    v.input.vector[8]=std::log1p(26.f)/std::log1p(20000.f);
    v.input.vector[9]=0;v.input.vector[16]=v.input.vector[17]=0;
    v.input.vector[22]=49.f/180;v.input.vector[33]=.5f;v.input.vector[36]=.8f;
    CommanderState state;state.decision_count=101;
    state.squads[0].intent=CommanderIntent::attack_move;
    state.squads[0].anchor=6;state.squads[0].roe=CommanderRoe::normal;
    const auto aggressive_main=[](const CommanderAction& a) {
        return a[2]==1&&a[5]==u8(CommanderRoe::aggressive)&&
            (a[3]==u8(CommanderIntent::attack_move)||a[3]==u8(CommanderIntent::siege));
    };
    for(u32 opponent:{1u,2u}) {
        v.services.public_enemy_tribe=opponent;
        const auto action=CommanderTeacherAction(state,v);
        require(aggressive_main(action)&&action[4]==6,
            "stranded Elf army did not commit to its existing target");
        auto recover=v;recover.workers=1;
        require(!aggressive_main(CommanderTeacherAction(state,recover)),
            "surviving Elf worker incorrectly triggered last-army commitment");
        recover=v;recover.pending_counts[0]=1;
        require(!aggressive_main(CommanderTeacherAction(state,recover)),
            "pending Elf worker incorrectly triggered last-army commitment");
        recover=v;recover.input.vector[9]=.05f;
        require(!aggressive_main(CommanderTeacherAction(state,recover)),
            "recovering Elf income incorrectly triggered last-army commitment");
        recover=v;recover.input.vector[8]=std::log1p(1001.f)/std::log1p(20000.f);
        require(!aggressive_main(CommanderTeacherAction(state,recover)),
            "Elf rebuilding resources incorrectly triggered last-army commitment");
        auto waiting=state;waiting.squads[0].intent=CommanderIntent::hold;
        require(!aggressive_main(CommanderTeacherAction(waiting,v)),
            "last-army commitment changed entry into a new attack");
        recover=v;recover.input.vector[46]=1;
        require(!aggressive_main(CommanderTeacherAction(state,recover)),
            "last-army commitment overrode the home-danger stop condition");
    }
}

int main() {
    elf_last_army_regression();
    elf_matchup_teacher_regressions();
    hq_retry_clearance_regression();
    teacher_role_regressions();
    auto elf_builder=make_unit(1,0x10,320,320);
    for(u32 state:{0x5au,0x5bu,0x5cu}) {
        elf_builder.command_state=state;
        elf_builder.command_value=state==0x5a?0x12:0x72;
        require(AiWalkingBuildTypeOf(elf_builder)==0x72,"Elf pending construction not recognized");
    }
    elf_builder.command_value=0x1d0;
    require(AiWalkingBuildTypeOf(elf_builder)==0,"construction backlink interpreted as a type");
    elf_builder.command_state=0x7f;elf_builder.command_value=0x72;
    require(AiWalkingBuildTypeOf(elf_builder)==0,"repair target interpreted as new construction");
    for(u32 tribe=0;tribe<4;++tribe) {
        AiObservation o;o.local_owner=1;o.simulation_frame=1;o.map_width_tiles=o.map_height_tiles=48;
        o.tiles.resize(48*48);for(auto& t:o.tiles)t.passable=t.buildable=t.visible=t.explored=true;
        o.start_x=o.start_y=320;o.primary_resources=20000;o.population_used=180;o.population_reserved=5;
        for(u32 x=15;x<19;++x){auto& t=o.tiles[10*48+x];t.resource_amount=4000;t.passable=t.buildable=false;}
        const u32 base=0x60+16*tribe;
        o.units={make_unit(1,base,320,320),make_unit(2,16*tribe,370,330)};
        for(u32 t=base+2;t<base+16;++t)o.units.push_back(make_unit(t,t,32*i32(t-base)+600,800));
        CommanderServices sv;sv.own_tribe=tribe;sv.autoscout=false;sv.curriculum_stage=4;
        sv.validator=[&](const AiSemanticAction& a) {
            const auto& source=*std::find_if(o.units.begin(),o.units.end(),[&](const auto& u){return u.id==a.unit_ids[0];});
            if(a.kind==AiSemanticActionKind::produce_unit||a.kind==AiSemanticActionKind::research||a.kind==AiSemanticActionKind::build) {
                const u32 kind=a.kind==AiSemanticActionKind::produce_unit?1:a.kind==AiSemanticActionKind::build?2:3;
                return std::any_of(kCommanderRaceActions[tribe].begin(),kCommanderRaceActions[tribe].end(),
                    [&](const auto& spec){return spec.kind==kind&&spec.type==a.production_id&&spec.producer==source.type_id;});
            }
            return true;
        };
        CommanderState state;auto view=BuildCommanderView(state,o,sv);
        require(view.anchors[0].x==320&&view.worker_cap==10,"race HQ/harvest window missing");
        require(view.mask[1]&&view.macro_plans[1][0].production_id==16*tribe,"race worker training missing");
        require(view.input.vector[606+tribe]==1,"own race not recorded");
        require(tribe==2||(!view.mask[35]&&!view.mask[36]),"foreign race received Tyrano merge");
        auto orders=CommanderExecute(state,o,view);
        require(std::any_of(orders.begin(),orders.end(),[](const auto& a){return a.kind==AiSemanticActionKind::harvest;}),"race worker did not harvest");
        if(tribe!=2) {
            u32 extra=42;while(extra<64&&kCommanderRaceActions[tribe][extra].kind!=1)++extra;
            require(extra<64&&view.mask[extra],"appended production action unavailable");
            CommanderAction a{};a[0]=u8(extra);orders=CommanderExecute(state,o,view,&a);
            require(std::any_of(orders.begin(),orders.end(),[&](const auto& order){return order.kind==AiSemanticActionKind::produce_unit&&order.production_id==kCommanderRaceActions[tribe][extra].type;}),"appended action executed as squad transfer");
            require(state.last_transfer_frame==std::array<u32,4>{},"extra action corrupted transfer clocks");
        }
        if(tribe==0) {
            o.simulation_frame=9;o.units[0].queued_production_type_id=0;o.units[0].command_state=0x51;
            view=BuildCommanderView(state,o,sv);require(view.pending_counts[0]==1,"type-zero worker queue lost");
        }
        if(tribe==3) {
            o.simulation_frame=9;o.units.push_back(make_unit(500,0x5a,550,550));
            view=BuildCommanderView(state,o,sv);
            require(view.army_count==1&&view.own_counts[42]==1&&view.own_counts[10]==0,"BoneFighter confused with Devil");
            auto enemy=make_unit(900,0x5a,650,650);enemy.owner_id=2;enemy.controlled=false;
            o.units.push_back(enemy);o.simulation_frame=17;
            view=BuildCommanderView(state,o,sv);
            require(view.input.vector[144]>0.09f,"special enemy disappeared from the legacy histogram");
            require(view.own_counts[42]==1,"enemy special identity entered our roster");
        }
    }
    std::cout<<"four race economy, production, extended actions and special identities passed\n";
}
