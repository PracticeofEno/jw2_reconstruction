// Public-state planning guard only. Do not alter population counters, engine
// construction, pending reservations, existing retry orders, or observations.
static bool ElfTreeSupplyTransitionNeedsDeferral(const AiObservation& o,
    const CommanderView& v) {
    if(v.services.own_tribe!=1||
        (v.services.public_enemy_tribe>3)||
        !v.services.production_info)return false;
    // The stock Heaven/ElfTree are lifecycle-class2 supply providers. Query
    // their actual public definition population field (11/8 in original TRC).
    // Other Elf Hall providers add positive supply; omitting them makes this
    // a conservative lower bound, not a fabricated total-capacity estimate.
    u64 reflected_floor=0;
    for(const auto& u:v.own) {
        if(!u.controlled||u.owner_id!=o.local_owner||!u.alive||!u.health||
            u.under_construction||(u.type_id!=0x70&&u.type_id!=0x72))continue;
        reflected_floor+=v.services.production_info(
            AiProductionRequestKind::building,u.type_id).population;
    }
    // Existing executor requests another Tree at demand+queued+4>=capacity.
    // Even during a delayed capacity update, permit a genuinely needed Tree
    // when the already-completed providers would not clear that same margin.
    const u64 outstanding_margin=u64(o.population_reserved)+v.queued_population+4;
    return reflected_floor>o.population_used&&reflected_floor>outstanding_margin;
}
