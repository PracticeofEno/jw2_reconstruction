// Included in CommanderExecute: one empty collector per visible meat drop.
// No mission is created here: only squads already executing HUNT participate.
std::set<u32> collecting_meat, reserved_meat;
auto meat_log=[&](const char* event,const CommanderMeatReservation& r,const AiObservedUnit& u) {
    if(!v.services.diagnostic)return;
    std::ostringstream line;line<<"ai-commander: meat-"<<event<<" frame="<<f
        <<" owner="<<o.local_owner<<" source="<<u.id<<" effect="<<r.effect
        <<" reserve="<<u.action_mode<<" site="<<r.x<<','<<r.y;
    v.services.diagnostic(line.str());
};
auto collecting_eligible=[&](const AiObservedUnit& u) {
    return meat_collector(u)&&u.id!=s.recon_scout_id&&!merging.count(u.id)&&
        s.squads[std::min<u8>(s.units.at(u.id).squad,2)].intent==CommanderIntent::hunt;
};
std::vector<CommanderMeatReservation> kept_meat;
for(const auto& r:s.meat) {
    const auto* u=find_unit(v.own,r.unit);
    if(!u||s.units.at(u->id).generation!=r.generation)continue;
    if(u->action_mode>0) {
        meat_log("collected",r,*u);
        auto& us=s.units.at(u->id);
        if(us.hunting){auto a=order(AiSemanticActionKind::set_hunt_marker,u->id);a.stance_on=false;if(publish(a,false))us.hunting=false;}
        continue;
    }
    const auto e=std::find_if(v.visible_meat.begin(),v.visible_meat.end(),[&](const auto& e){return e.id==r.effect&&e.x==r.x&&e.y==r.y;});
    if(!collecting_eligible(*u)||e==v.visible_meat.end()||f-r.started_frame>=512||!near(point(*u),{r.x,r.y,true},1024))continue;
    kept_meat.push_back(r);reserved_meat.insert(r.effect);collecting_meat.insert(r.unit);
}
s.meat=std::move(kept_meat);
for(const auto& e:v.visible_meat) {
    if(e.linked||reserved_meat.count(e.id))continue;
    const AiObservedUnit* collector=nullptr;float best=800;
    for(const auto& u:v.own)if(collecting_eligible(u)&&!collecting_meat.count(u.id)&&!commanded.count(u.id)&&!busy(u)&&!(u.command_flags&0x10)) {
        const float d=distance(point(u),{e.x,e.y,true});
        if(d>=best)continue;
        if(u.render_class!=3) {
            const auto route=path(o,point(u),{e.x,e.y,true});
            if(route.empty()||route.size()>33)continue;
        }
        best=d;collector=&u;
    }
    if(!collector)continue;
    const CommanderMeatReservation r{e.id,collector->id,s.units.at(collector->id).generation,f,e.x,e.y};
    // Reserve only after the authoritative planner and packet budget accept.
    if(publish(order(AiSemanticActionKind::pickup_move,r.unit,{r.x,r.y,true}))) {
        s.units.at(r.unit).hunting=true;s.meat.push_back(r);
        collecting_meat.insert(r.unit);reserved_meat.insert(r.effect);meat_log("pickup",r,*collector);
    }
}
for(const auto& r:s.meat) {
    const auto& u=*find_unit(v.own,r.unit);auto& us=s.units.at(r.unit);
    if(commanded.count(r.unit)||busy(u)||(u.command_flags&0x10))continue;
    const auto e=std::find_if(v.visible_meat.begin(),v.visible_meat.end(),[&](const auto& e){return e.id==r.effect;});
    // A linked effect is already in native acquisition: do not cancel it by
    // repeating a movement order or by pursuing the next neutral monster.
    if(e!=v.visible_meat.end()&&e->linked)continue;
    if(f-us.last_order_frame<64||(u.command_state!=1&&f-us.last_progress<64))continue;
    if(publish(order(AiSemanticActionKind::pickup_move,r.unit,{r.x,r.y,true}))) {
        us.hunting=true;meat_log("pickup",r,u);
    }
}
