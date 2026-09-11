// A moving destination keeps faster members near the slowest member already
// travelling with the army. Original movement decelerates near its destination;
// using that behavior also makes these orders replayable by the original game.
struct MarchProjection { float progress=0, distance=std::numeric_limits<float>::max(); };
MarchProjection march_projection(const std::vector<CommanderPoint>& route,CommanderPoint p) {
    MarchProjection best;float along=0;
    for(std::size_t i=1;i<route.size();++i) {
        const auto a=route[i-1],b=route[i];const float length=distance(a,b);
        if(length==0)continue;
        const float t=std::clamp(float(i64(p.x-a.x)*(b.x-a.x)+i64(p.y-a.y)*(b.y-a.y))/(length*length),0.f,1.f);
        const CommanderPoint closest{a.x+i32(std::lround((b.x-a.x)*t)),a.y+i32(std::lround((b.y-a.y)*t)),true};
        const float d=distance(p,closest);
        if(d<best.distance){best.distance=d;best.progress=along+t*length;}
        along+=length;
    }
    return best;
}
CommanderPoint march_point(const std::vector<CommanderPoint>& route,float progress) {
    if(route.empty())return {};
    for(std::size_t i=1;i<route.size();++i) {
        const auto a=route[i-1],b=route[i];const float length=distance(a,b);
        if(length>0&&progress<=length) {
            const float t=std::clamp(progress/length,0.f,1.f);
            return {a.x+i32(std::lround((b.x-a.x)*t)),a.y+i32(std::lround((b.y-a.y)*t)),true};
        }
        progress-=length;
    }
    return route.back();
}
bool paced_march_intent(CommanderIntent intent) {
    return intent==CommanderIntent::attack_move||intent==CommanderIntent::siege||intent==CommanderIntent::harass;
}
struct MarchPlan {
    const AiObservedUnit* pacer=nullptr;
    float progress=0, lead=0;
    bool active=false;
};
MarchPlan prepare_march(CommanderState& s,const AiObservation& o,const CommanderView& v,
    u32 q,const std::vector<u32>& ids,CommanderPoint center,bool contact,const std::set<u32>& merging) {
    auto& rule=s.squads[q];MarchPlan plan;
    const auto goal=v.anchors[rule.anchor];
    if(!paced_march_intent(rule.intent)||!goal.valid||contact||ids.size()<2)return plan;
    auto eligible=[&](const AiObservedUnit& u) {
        const auto& state=s.units.at(u.id);
        return u.alive&&u.controlled&&!u.under_construction&&u.movement_step_limit>0&&
            u.id!=s.recon_scout_id&&!merging.count(u.id)&&!busy(u)&&!(u.command_flags&0x10)&&
            (rule.roe==CommanderRoe::aggressive||hp(u)>=0.25f)&&
            v.frame-state.last_progress<256;
    };
    const auto* previous=find_unit(v.own,rule.march_pacer);
    if(previous&&(s.units.at(previous->id).squad!=q||
        s.units.at(previous->id).generation!=rule.march_pacer_generation||!eligible(*previous)))previous=nullptr;
    const auto origin=previous?point(*previous):center;
    const AiObservedUnit* pacer=previous;
    // Distant reinforcements catch the moving formation. They join its pace
    // only once nearby; a blocked unit expires rather than freezing the army.
    for(u32 id:ids) {
        const auto& u=*find_unit(v.own,id);
        if(!eligible(u)||!near(point(u),origin,previous?384:512))continue;
        // The native accumulator converges to step_limit * period, then
        // divides by period: step_limit is cruising speed; period is ramp-up.
        if(!pacer||u.movement_step_limit<pacer->movement_step_limit||
            (u.movement_step_limit==pacer->movement_step_limit&&u.movement_period>pacer->movement_period))pacer=&u;
    }
    if(!pacer)return plan;
    const bool changed=rule.march_pacer!=pacer->id||rule.march_pacer_generation!=s.units.at(pacer->id).generation||
        rule.march_serial!=rule.serial||!near(rule.march_goal,goal,96);
    if(changed||rule.march_route.size()<2) {
        rule.march_route=pacer->render_class==3?std::vector<CommanderPoint>{point(*pacer),goal}:path(o,point(*pacer),goal);
        if(rule.march_route.size()<2)return plan;
        rule.march_pacer=pacer->id;rule.march_pacer_generation=s.units.at(pacer->id).generation;
        rule.march_serial=rule.serial;rule.march_goal=goal;
    }
    auto projection=march_projection(rule.march_route,point(*pacer));
    if(projection.distance>192) {
        rule.march_route=pacer->render_class==3?std::vector<CommanderPoint>{point(*pacer),goal}:path(o,point(*pacer),goal);
        if(rule.march_route.size()<2)return plan;
        projection=march_projection(rule.march_route,point(*pacer));
    }
    plan.pacer=pacer;plan.progress=projection.progress;
    // Enough lookahead for acceleration and a 32-frame refresh; no repeated
    // 128-frame HOLD/64-frame sprint cycle. Targets stay on the ramp corridor.
    plan.lead=std::clamp(float(pacer->movement_step_limit)*24.f,64.f,160.f);
    plan.active=!near(point(*pacer),goal,256);
    if(plan.active&&v.services.diagnostic&&(changed||(v.frame-1)%128==0)) {
        std::ostringstream line;line<<"ai-commander: march-pace frame="<<v.frame<<" owner="<<pacer->owner_id
            <<" squad="<<q<<" pacer="<<pacer->id<<" step="<<pacer->movement_step_limit<<" period="<<pacer->movement_period
            <<" progress="<<plan.progress<<" lead="<<plan.lead<<" members="<<ids.size();
        v.services.diagnostic(line.str());
    }
    return plan;
}
