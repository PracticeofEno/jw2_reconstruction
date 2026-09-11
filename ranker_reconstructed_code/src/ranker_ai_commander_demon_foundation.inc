// Demon rule teacher validated against normal built-in AI; see
// docs/JW2_COMMANDER_DEMON_TEACHER_20260910.md. Actor schema unchanged.
// Original rules and actor schema are unchanged.
static CommanderAction DemonFoundationTeacher(const CommanderState& s,const CommanderView& v) {
    CommanderAction a{};const auto& z=v.input.vector;const auto& cat=race_actions(v.services);
    const auto done=[&](u32 t){return v.own_counts[count_index(t)];};
    const auto pending=[&](u32 t){return v.pending_counts[count_index(t)];};
    const auto total=[&](u32 t){return done(t)+pending(t);};
    const auto ground=[](const AiObservedUnit& u){return u.alive&&!u.under_construction&&combat(u)&&u.attack_power>0&&(u.attackable_class_mask&7u)!=0;};
    // Direct ground-weapon proxy only: visible spell/caster threat is not fully
    // modeled. Air-only Mana, unarmed support and buildings do not pad it.
    const auto living_combat=[&](const AiObservedUnit& u){return u.visible&&ground(u);};
    const float spendable=std::expm1(z[553]*std::log1p(20000.0f));
    const float margin=z[24]*16;const bool income=v.income_rate>0;
    u32 ground_count=0;float ground_weight=0;
    for(const auto& u:v.own)if(ground(u)){++ground_count;ground_weight+=weight(u);}
    const u32 basic=done(0x31)+done(0x36);
    float home_enemy=0,home_own=0;
    auto at_base=[&](CommanderPoint p,i32 radius){for(const auto& b:v.own)if(b.alive&&!b.under_construction&&b.type_id==0x90&&near(p,point(b),radius))return true;return false;};
    for(const auto& u:v.visible_enemies)if(living_combat(u)&&at_base(point(u),640))home_enemy+=weight(u);
    for(const auto& u:v.own)if(ground(u)&&at_base(point(u),640))home_own+=weight(u);
    const bool home_contact=home_enemy>0,critical_home=home_enemy>=600&&home_enemy>.8f*home_own;
    bool saving=false;
    const auto pick=[&](u32 macro){if(!a[0]&&!saving&&v.mask[macro]){a[0]=u8(macro);return true;}return false;};
    const auto usable_builder=[&](){for(const auto& u:v.own)if(worker(u)&&u.alive&&u.controlled&&!busy(u)&&!construction_worker(s,u.id))return true;return false;};
    // Legality wins over decoded float affordability. A funded-but-masked request
    // always releases. The half-resource tolerance covers round-trip float noise.
    const auto request=[&](u32 macro,bool reserve){
        if(a[0]||saving)return;
        const auto& p=cat[macro];
        if(p.kind==1&&!done(p.producer))return;
        if(p.kind==2&&(!usable_builder()||(v.services.building_prerequisite&&!v.services.building_prerequisite(p.type))))return;
        if(v.mask[macro]){a[0]=u8(macro);return;}
        if(!reserve||!income||!v.services.production_info||!v.services.validator)return;
        const auto kind=p.kind==1?AiProductionRequestKind::unit:p.kind==2?AiProductionRequestKind::building:AiProductionRequestKind::research;
        const u32 cost=info(v.services,kind,p.type).cost;
        if(cost&&spendable+.5f<float(cost))saving=true;
    };
    const auto worker_goal=[&](u32 goal){if(v.workers+pending(0x30)<std::min(v.worker_cap,goal)&&pending(0x30)<2)request(1,false);};
    const auto skeleton=[&](){if(pending(0x31)<2*done(0x94))pick(2);};
    const auto kelpa=[&](){if(pending(0x36)+pending(0x3f)<2*done(0x96))pick(42);};
    const auto mix=[&](){
        const u32 target=done(0x9b)?2u:std::clamp((total(0x31)+2)/4,2u,4u);
        if(total(0x36)<target)kelpa();
        if(done(0x9b)&&pending(0x36)+pending(0x3f)<2*done(0x96)&&total(0x3f)<std::max(4u,total(0x31)/2))pick(44);
        skeleton();kelpa();
    };
    const auto hall_goal=[&](u32 type,u32 macro,u32 n,bool allowed){
        if(allowed&&total(type)<n&&!pending(type))request(macro,!home_contact&&v.frame<12000);
    };
    bool research_pending=false,research_producer=false;
    for(const auto& u:v.own){
        research_pending|=u.alive&&u.controlled&&order_count(u,AiSemanticActionKind::research,31)!=0;
        if(u.type_id==0x90&&u.alive&&u.controlled&&!u.under_construction&&!busy(u)&&!researching(u)&&u.deferred_command_count<10)research_producer=true;
    }
    for(const auto& r:s.receipts)if(r.order.kind==AiSemanticActionKind::research&&r.order.production_id==31&&
        v.frame>=r.frame&&v.frame-r.frame<=8&&!r.order.unit_ids.empty()) {
        const auto* producer=find_unit(v.own,r.order.unit_ids[0]);
        research_pending|=producer&&producer->alive&&producer->controlled;
    }
    const bool economy_started=s.last_research[31]||research_pending;
    const u32 economy_cost=info(v.services,AiProductionRequestKind::research,31).cost;
    const bool opening_window=v.frame<6000;
    const bool funded_illegal=economy_cost&&spendable+.5f>=float(economy_cost)&&!v.mask[22];
    const bool economy_release=!opening_window||!income||funded_illegal;
    const auto economy=[&](bool late){
        if(economy_started||v.workers<14||home_contact)return;
        if(late&&basic<20)return;
        if(v.mask[22]||research_producer)request(22,v.frame<(late?20000u:6000u));
    };
    // Explicit finite opening priorities. Pending structures/research release
    // the budget so workers and existing Hall queues continue in parallel.
    if(margin<3&&!pending(0x92))request(13,!home_contact&&v.frame<50000);
    if(v.workers<8)worker_goal(8);
    if(home_contact){mix();hall_goal(0x94,15,1,true);}
    // Recovery for off-policy states; this teacher never creates those splits.
    if(!home_contact){if(!v.squads[1].members.empty())pick(39);if(!v.squads[2].members.empty())pick(41);}
    // Candidate opening caps completed+pending Insects14 until harvest starts.
    // The worker threshold is evaluated in battles, not claimed as an economic optimum.
    worker_goal(14);
    if(v.frame<6000)economy(false);
    const bool first_hall_ready=home_contact||economy_started||
        (v.workers>=14&&economy_release)||!opening_window;
    hall_goal(0x94,15,1,first_hall_ready);
    hall_goal(0x94,15,2,v.workers>=14&&done(0x94)>=1&&!home_contact);
    hall_goal(0x96,17,1,total(0x94)>=2&&!home_contact);
    if(total(0x96)>=1)worker_goal(16);
    if(total(0x31)<4)skeleton();
    if(total(0x36)<2)kelpa();
    if(total(0x31)>=4&&total(0x36)>=2)worker_goal(20);
    hall_goal(0x94,15,3,basic>=8&&v.workers>=16&&margin>=6&&!home_contact&&income);
    if(v.frame>=6000)economy(true);
    // Nightmare uses MagicDen and requires only the original EvilPortal prerequisite.
    if(basic>=12&&v.workers>=20&&!home_contact&&income&&!total(0x9b))request(45,v.frame<12000);
    // Expansion uses the already public, occupancy-aware terrain components.
    // No cost-insensitive calls into the engine or hidden enemy state.
    bool expansion_site=false,expansion_scout=false;
    const bool first_base=done(0x90)==1;
    const bool depletion=first_base?z[16]<=.35f:(z[16]<=.35f&&z[17]<=.35f);
    const bool expansion_due=done(0x90)>=1&&done(0x90)<3&&!pending(0x90)&&v.workers>=16&&
        basic>=(depletion?8u:20u)&&!home_contact&&((first_base&&(v.frame>=8000||z[16]<=.6f))||depletion);
    if(expansion_due&&s.expansion.has_target&&s.expansion.target_index<s.expansion.clusters.size()) {
        const auto& c=s.expansion.clusters[s.expansion.target_index];
        const auto comp=[&](i32 x,i32 y){
            const u32 w=s.build_component_width;if(!w||x<0||y<0||u32(x)>=w)return 0u;
            const std::size_t i=std::size_t(y)*w+u32(x);return i<s.build_components.size()?s.build_components[i]:0u;
        };
        const u32 target_component=comp(c.site_x/32,c.site_y/32);
        bool reaches=false;
        if(c.site_x>=0&&c.site_y>=0&&target_component)for(const auto& u:v.own)if(worker(u)&&u.alive&&u.controlled&&!busy(u)&&!construction_worker(s,u.id)) {
            const i32 x=u.x/32,y=u.y/32;const u32 current=comp(x,y);
            if(current==target_component||(!current&&(comp(x-1,y)==target_component||comp(x+1,y)==target_component||comp(x,y-1)==target_component||comp(x,y+1)==target_component)))reaches=true;
        }
        const bool candidate=c.site_x>=0&&c.site_y>=0&&c.known_amount&&!c.developed&&!c.site_blocked&&reaches;
        expansion_site=candidate&&c.site_explored;
        expansion_scout=candidate&&!c.site_explored&&v.anchors[3].valid;
        if(expansion_site)request(12,v.frame<50000);
    }
    if(done(0x90)>=2)worker_goal(28);
    mix();
    const auto squads=projected_squads(v,a[0]);const auto& main=squads[0];
    u32 main_count=0,clustered=0;float main_weight=0,health=0,max_health=0;
    for(u32 id:main.members){const auto* u=find_unit(v.own,id);if(!u||!ground(*u))continue;
        ++main_count;main_weight+=weight(*u);health+=u->health;max_health+=u->max_health;
        clustered+=near(point(*u),main.center,384);
    }
    float local_own=0,local_enemy=0;i64 ex=0,ey=0;u32 local_count=0;
    for(const auto& u:v.own)if(ground(u)&&near(point(u),main.center,512))local_own+=weight(u);
    const AiObservedUnit* contact_seed=nullptr;float seed_distance=std::numeric_limits<float>::max();
    for(const auto& u:v.visible_enemies)if(living_combat(u)&&(near(point(u),main.center,512)||at_base(point(u),640))) {
        if(near(point(u),main.center,512))local_enemy+=weight(u);
        if(critical_home&&!at_base(point(u),640))continue;
        const float d=distance(point(u),main.center.valid?main.center:v.anchors[0]);
        if(d<seed_distance){seed_distance=d;contact_seed=&u;}
    }
    if(contact_seed)for(const auto& u:v.visible_enemies)if(living_combat(u)&&near(point(u),point(*contact_seed),320)) {
        ex+=u.x;ey+=u.y;++local_count;
    }
    const CommanderPoint contact{local_count?i32(ex/local_count):0,local_count?i32(ey/local_count):0,local_count!=0};
    const u8 home=v.closed_plateau&&v.anchors[2].valid?2:v.anchors[1].valid?1:0;
    const auto known_hq=[&](u8 anchor){if(!v.anchors[anchor].valid)return false;for(const auto& e:v.enemies)
        if(building(e.seen)&&(e.seen.type_id&15u)==0&&near(point(e.seen),v.anchors[anchor],32))return true;return false;};
    u8 destination=0;
    if(v.anchors[6].valid)destination=6;else if(known_hq(4))destination=4;else if(known_hq(9))destination=9;else if(v.anchors[12].valid)destination=12;
    const auto& current=s.squads[0];
    const bool offensive_anchor=current.anchor==6||(current.anchor==4&&known_hq(4))||(current.anchor==9&&known_hq(9))||current.anchor==12;
    const bool was_offensive=current.intent==CommanderIntent::attack_move&&offensive_anchor;
    const bool away=!at_base(main.center,640)&&!near(main.center,v.anchors[home],640);
    const bool outmatched=local_enemy>0&&local_enemy>1.5f*local_own&&away;
    float remembered=0;for(const auto& e:v.enemies)if(ground(e.seen))remembered+=weight(e.seen)*std::pow(.97f,float(v.frame-e.last_seen_frame)/64.f);
    const bool ready=ground_count>=20&&main_count*100>=ground_count*85&&clustered*100>=main_count*75&&
        max_health>0&&health>=.65f*max_health&&!home_contact&&main_weight>=1.1f*remembered;
    bool attack=destination&&(was_offensive?!critical_home&&main_count>=6&&!outmatched:ready);
    // No-income last army may still finish known targets instead of waiting
    // forever below20. This is an explicit terminal-economy entry exception.
    if(!attack&&destination&&v.workers+pending(0x30)==0&&!income&&main_count>=6&&!home_contact&&!outmatched)attack=true;
    u8 intent=u8(CommanderIntent::hold),anchor=home,roe=u8(CommanderRoe::aggressive);
    if(attack){intent=u8(CommanderIntent::attack_move);anchor=destination;}
    else if(contact.valid) {
        if(outmatched){intent=u8(CommanderIntent::retreat);anchor=0;}
        else {
            intent=u8(CommanderIntent::attack_move);anchor=home;
            // A second distant visible group must not drag defense across map.
            const auto actual_contact=[&](u8 id){if(!v.anchors[id].valid)return false;for(const auto& u:v.visible_enemies)
                if(living_combat(u)&&near(point(u),contact,384)&&near(point(u),v.anchors[id],192))return true;return false;};
            if(near(v.anchors[10],contact,256)&&actual_contact(10))anchor=10;
            else if(near(v.anchors[7],contact,256)&&near(v.anchors[7],main.center,640)&&actual_contact(7))anchor=7;
            else if(v.anchors[3].valid&&near(v.anchors[3],contact,640))anchor=3;
        }
    } else if(was_offensive&&main_count<6&&away){intent=u8(CommanderIntent::retreat);anchor=0;}
    else {
        const u32 age=s.decision_count-current.changed_decision;
        const bool scouting=current.intent==CommanderIntent::scout;
        const bool stale=z[47]>=.5f;
        const bool at_rally=near(main.center,v.anchors[home],384);
        bool scout_healthy=true;const AiObservedUnit* fastest=nullptr;
        for(u32 id:main.members){const auto* u=find_unit(v.own,id);if(u&&(!fastest||speed(*u)>speed(*fastest)))fastest=u;}
        if(fastest)scout_healthy=hp(*fastest)>=.6f;
        const u8 scout_target=expansion_scout?3:known_hq(4)?4:v.anchors[12].valid?12:0;
        if(scouting&&age<20&&scout_healthy&&(stale||current.anchor==3)&&v.anchors[current.anchor].valid) {
            intent=u8(CommanderIntent::scout);anchor=current.anchor;roe=u8(CommanderRoe::cautious);
        } else if(!scouting&&current.intent==CommanderIntent::hold&&age>=5&&at_rally&&ground_count>=8&&scout_healthy&&scout_target&&(stale||expansion_scout)) {
            intent=u8(CommanderIntent::scout);anchor=scout_target;roe=u8(CommanderRoe::cautious);
        }
    }
    bool fresh_asset_reflex=false;
    if(s.threat.valid&&v.frame>=s.threat_frame&&v.frame-s.threat_frame<=8)
        for(const auto& u:v.own)if(building(u)&&near(point(u),s.threat,640)){fresh_asset_reflex=true;break;}
    if(!main.members.empty()&&(fresh_asset_reflex||u8(current.intent)!=intent||current.anchor!=anchor||u8(current.roe)!=roe)) {
        a[2]=1;a[3]=intent;a[4]=anchor;a[5]=roe;
    }
    a[1]=a[0]==12?3:0;a[6]=0;a[7]=0;
    CommanderMask mask=v.mask;
    for(std::size_t h=0;h<8;++h){CommanderLegalHeadMask(v,a,h,mask);const auto off=kCommanderHeadOffsets[h];
        if(a[h]>=kCommanderHeadSizes[h]||!mask[off+a[h]]){a[h]=0;for(u32 i=0;i<kCommanderHeadSizes[h];++i)if(mask[off+i]){a[h]=u8(i);break;}}
    }
    return a;
}
