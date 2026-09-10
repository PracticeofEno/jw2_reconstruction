// Included inside commander.cpp's anonymous namespace. Actor candidates contain
// only own state, public points and currently observed targets; never ghosts.
struct CommanderSkillCandidate {
    AiSemanticAction action;
    std::array<float, kCommanderSkillFeatureSize> features{};
    float priority = 0;
    bool legal = false;
};

void plan_commander_skills(const CommanderState& state, const AiObservation& o, CommanderView& v) {
    if (!v.services.skill_source) return;
    std::array<std::vector<CommanderSkillCandidate>, 41> groups;
    const auto coordinate_x = [&](i32 x) { return norm(float(x), float(std::max(1u, o.map_width_tiles) * 32)); };
    const auto coordinate_y = [&](i32 y) { return norm(float(y), float(std::max(1u, o.map_height_tiles) * 32)); };
    for (const auto& source : v.own) {
        if (building(source) || source.under_construction) continue;
        const auto own = v.services.skill_source(source.id);
        const auto registered = state.units.find(source.id);
        // Ordered commands enter simulation after publication. Protect that
        // interval as well as all special-ability/restore approach and timer states.
        const bool pending = registered != state.units.end() &&
            registered->second.last_order.kind == AiSemanticActionKind::use_ability &&
            v.frame - registered->second.last_order_frame < 16;
        const auto add = [&](AiSemanticAction a, u32 group, u32 selector,
                             const AiObservedUnit* target, CommanderPoint destination) {
            CommanderSkillCandidate c;
            c.action = std::move(a);
            auto readiness = source;
            // The original morph state deliberately replaces the normal
            // capability word with 0x08002011. It must still be able to exit.
            if (c.action.kind == AiSemanticActionKind::morph_exit) readiness.type_flags = 0;
            c.legal = !busy(readiness) && !pending && valid(v.services, c.action);
            const AiSkillDefinition no_cost{0, 0, never, 0, 0, 0, 0, true};
            const auto& d = selector < 36 ? kAiSkillDefinitions[selector] : no_cost;
            auto& z = c.features;
            z[0] = 1; z[1] = c.legal; z[2] = float(c.action.kind) / 32;
            z[3] = norm(float(selector), 45); z[4] = norm(float(source.type_id), 169);
            z[5] = hp(source); z[6] = norm(float(source.secondary_value), float(std::max(1u, source.max_secondary_value)));
            const bool attachment = selector < 32 && (own.item_ability_mask & (1u << selector));
            z[7] = attachment ? 0 : norm(float(d.health_cost), float(std::max(1u, source.max_health)));
            z[8] = attachment ? 0 : norm(float(d.mana_cost), float(std::max(1u, source.max_secondary_value)));
            z[9] = norm(float(own.status), 10); z[10] = norm(float(d.minimum_status), 10);
            z[11] = norm(float(own.range), 1024); z[12] = coordinate_x(source.x); z[13] = coordinate_y(source.y);
            z[14] = target ? (!target->alive ? 1.0f : target->id == source.id ? 0.25f : hostile(o, *target) ? 0.75f : 0.5f) : 0;
            z[15] = target ? norm(float(target->type_id), 169) : 0;
            z[16] = target ? hp(*target) : 0; z[17] = coordinate_x(destination.x); z[18] = coordinate_y(destination.y);
            const float dist = distance(point(source), destination);
            z[19] = norm(dist, 2048); z[20] = float((source.command_flags >> 11) & 0x7fu) / 127;
            z[21] = norm(float(source.command_entry_lockout_ticks), 600);
            z[22] = d.research == never || (d.research < o.research_order_levels.size() && o.research_order_levels[d.research]);
            z[23] = c.action.kind == AiSemanticActionKind::set_stance ? c.action.stance_on : attachment;
            c.priority = (c.legal ? 10000.0f : 0.0f) - dist;
            // These preferences rank a bounded menu, not automatic casts. Both
            // friendly and enemy alternatives remain selectable when legal.
            const bool friendly = target && !hostile(o, *target);
            const bool benefit = selector == 0 || selector == 2 || selector == 8 || selector == 10 ||
                selector == 17 || selector == 21 || selector == 25 || selector == 27 || selector == 28;
            if (target && friendly == benefit) c.priority += 256;
            if (target && !target->alive) c.priority += 128;
            groups[group].push_back(std::move(c));
        };
        u32 mask = own.ability_mask;
        if (source.command_flags & 0x800u) { mask &= ~(1u << 19); mask |= 1u << 31; }
        for (u32 ability = 0; ability < 32; ++ability) {
            if (!(mask & (1u << ability)) || !kAiSkillDefinitions[ability].present || ability == 7) continue;
            const auto& d = kAiSkillDefinitions[ability];
            auto a = order(AiSemanticActionKind::use_ability, source.id, point(source)); a.ability_id = ability;
            if (d.mode == 0) { add(a, ability, ability, nullptr, point(source)); continue; }
            std::vector<const AiObservedUnit*> targets;
            const auto collect = [&](const auto& list) {
                std::vector<const AiObservedUnit*> nearest;
                for (const auto& target : list) {
                    if (d.mode >= 2 && v.services.skill_target && !v.services.skill_target(ability, target.id)) continue;
                    if (!target.visible || (d.target_classes && (target.render_class >= 32 ||
                        !(d.target_classes & (1u << target.render_class))))) continue;
                    if ((ability == 25 || ability == 27) && target.type_id != 0x31) continue;
                    nearest.push_back(&target);
                }
                std::sort(nearest.begin(), nearest.end(), [&](const auto* a, const auto* b) {
                    return std::make_tuple(distance2(point(source), point(*a)), a->id) <
                        std::make_tuple(distance2(point(source), point(*b)), b->id);
                });
                if (nearest.size() > 4) nearest.resize(4);
                targets.insert(targets.end(), nearest.begin(), nearest.end());
            };
            if (d.mode == 4) collect(v.services.visible_corpses);
            else { collect(v.own); collect(v.visible_allies); collect(v.visible_enemies); collect(v.visible_neutrals); }
            for (const auto* target : targets) {
                auto cast = a; cast.target_x = target->x; cast.target_y = target->y;
                // Mode 1 is terrain-only; mode 2 accepts either units or terrain.
                if (d.mode != 1) cast.target_unit_id = target->id;
                add(cast, ability, ability, target, point(*target));
            }
            if (d.mode < 3) {
                add(a, ability, ability, nullptr, point(source));
                for (u32 anchor : {0u, 2u, 6u, 10u}) if (v.anchors[anchor].valid && explored_tile(o, v.anchors[anchor])) {
                    auto cast = a; cast.target_x = v.anchors[anchor].x; cast.target_y = v.anchors[anchor].y;
                    add(cast, ability, ability, nullptr, v.anchors[anchor]);
                }
            } else if (targets.empty()) {
                // Preserve resource/level information while no target is visible.
                add(a, ability, ability, nullptr, point(source));
            }
        }
        for (u32 stance = 0; stance < 4; ++stance) if (source.type_flags & (1u << (0x12 + stance))) {
            auto a = order(AiSemanticActionKind::set_stance, source.id); a.stance_id = stance;
            a.stance_on = !(source.command_flags & (0x4000u << stance));
            add(a, 32 + stance, 32 + stance, nullptr, point(source));
        }
        if (source.type_flags & 0x20000u) add(order(AiSemanticActionKind::morph_enter, source.id), 36, 36, nullptr, point(source));
        if (own.runtime_flags & 0x40000u) add(order(AiSemanticActionKind::morph_exit, source.id), 37, 37, nullptr, point(source));
        // Carrier and mana-sharing unit roles use the same audited command API.
        if ((source.type_flags & 0x400u) && own.loaded_transport) {
            auto unload = order(AiSemanticActionKind::unload_transport, source.id, point(source));
            add(unload, 39, 39, nullptr, point(source));
        }
        if (own.boardable && !(source.type_flags & 0x400u)) for (const auto& carrier : v.own) if (carrier.id != source.id &&
            (carrier.type_flags & 0x400u) && near(point(source), point(carrier), 320)) {
            auto board = order(AiSemanticActionKind::board_transport, source.id); board.target_unit_id = carrier.id;
            add(board, 38, 38, &carrier, point(carrier));
        }
        if (source.type_flags & 2u) for (const auto& peer : v.own) if (source.id < peer.id && (peer.type_flags & 2u) &&
            near(point(source), point(peer), 320) && source.action_mode != peer.action_mode && !busy(peer)) {
            auto share = order(AiSemanticActionKind::transfer_secondary, source.id); share.unit_ids.push_back(peer.id);
            add(share, 40, 40, &peer, point(peer));
            break;
        }
    }
    for (auto& group : groups) {
        std::stable_sort(group.begin(), group.end(), [](const auto& a, const auto& b) { return a.priority > b.priority; });
        // Rotate equally good casters without using the gameplay RNG.
        if (group.size() > 8) group.resize(8);
    }
    // One candidate per distinct skill first, then alternatives. More groups
    // than slots rotate deterministically so an ability cannot be starved.
    u32 slot = 0;
    const u32 start = (v.frame / 88) % groups.size();
    for (u32 depth = 0; depth < 8 && slot < kCommanderSkillSlots; ++depth)
        for (u32 index = 0; index < groups.size() && slot < kCommanderSkillSlots; ++index) {
            const auto& group = groups[(start + index) % groups.size()];
            if (depth >= group.size()) continue;
            const auto& candidate = group[depth];
            std::copy(candidate.features.begin(), candidate.features.end(),
                v.input.vector.begin() + kCommanderRaceVectorSize + slot * kCommanderSkillFeatureSize);
            if (candidate.legal) v.macro_plans[kCommanderSkillMacroBegin + slot] = {candidate.action};
            ++slot;
        }
}
