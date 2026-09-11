// Only our own engagements and currently visible enemy capabilities are used.
// A passive building being demolished is not a fight that needs reinforcements.
struct CommanderSupportContact {
    const AiObservedUnit* enemy = nullptr;
    const AiObservedUnit* ally = nullptr;
};
bool support_threat(const AiObservation& o,const AiObservedUnit& enemy) {
    return enemy.alive&&enemy.visible&&!enemy.under_construction&&
        enemy.attack_power>0&&hostile(o,enemy);
}
std::vector<CommanderSupportContact> combat_support_contacts(
    const CommanderState& s,const AiObservation& o,const CommanderView& v) {
    std::vector<CommanderSupportContact> contacts;
    for(const auto& ally:v.own) {
        if(!ally.alive||!ally.controlled||ally.under_construction||!combat(ally)||
            ally.id==s.recon_scout_id)continue;
        const auto* focus=find_unit(v.visible_enemies,ally.target_id);
        const bool fighting=(ally.command_flags&0x10)!=0;
        const auto damage=s.damage_interrupt_frames.find(ally.id);
        const bool hurt=damage!=s.damage_interrupt_frames.end()&&v.frame-damage->second<=32;
        const auto outgoing=focus&&focus->render_class==3?ally.attack_range_vs_air:ally.attack_range;
        const auto incoming=focus?(ally.render_class==3?focus->attack_range_vs_air:focus->attack_range):0;
        const bool active_target=focus&&support_threat(o,*focus)&&near(point(ally),point(*focus),
            i32(std::max(outgoing,incoming)+96));
        if(!fighting&&!hurt&&!active_target)continue;
        for(const auto& enemy:v.visible_enemies) {
            if(!support_threat(o,enemy))continue;
            const auto range=ally.render_class==3?enemy.attack_range_vs_air:enemy.attack_range;
            const bool threatens=ally.render_class<32&&
                (enemy.attackable_class_mask&(1u<<ally.render_class))&&
                near(point(ally),point(enemy),i32(std::max(96u,range+64)));
            if(threatens||(active_target&&focus->id==enemy.id))contacts.push_back({&enemy,&ally});
        }
    }
    return contacts;
}
CommanderSupportContact combat_support_target(const std::vector<CommanderSupportContact>& contacts,
    const AiObservedUnit& helper,const CommanderSquadState& rule,CommanderPoint post) {
    if(rule.intent==CommanderIntent::retreat||rule.intent==CommanderIntent::scout||
        helper.attack_power==0||(rule.roe!=CommanderRoe::aggressive&&hp(helper)<0.25f))return {};
    const bool posted=rule.intent==CommanderIntent::hold||rule.intent==CommanderIntent::defend;
    CommanderSupportContact best;float score=std::numeric_limits<float>::max();
    for(const auto& contact:contacts) {
        const auto& enemy=*contact.enemy;
        if(enemy.render_class>=32||!(helper.attackable_class_mask&(1u<<enemy.render_class))||
            !near(point(helper),point(*contact.ally),640)||!near(point(helper),point(enemy),896))continue;
        if(posted&&(!near(post,point(*contact.ally),448)||!near(post,point(enemy),512)))continue;
        // Keep a useful ongoing fight stable, regardless of other enemies' HP.
        if(helper.target_id==enemy.id)return contact;
        const float candidate=distance(point(helper),point(enemy))+
            .25f*distance(point(helper),point(*contact.ally));
        if(candidate<score){score=candidate;best=contact;}
    }
    return best;
}
