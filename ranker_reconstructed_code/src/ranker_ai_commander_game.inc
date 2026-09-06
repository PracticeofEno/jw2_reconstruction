// Included once inside ranker_winmain.cpp's anonymous namespace. All writes
// here are self-play diagnostics/controller state or ordered gameplay packets.
struct AiCommanderOwner {
    CommanderState state;
    CommanderModel model;
    CommanderPcg32 rng;
    CommanderRolloutWriter rollout;
    bool initialized = false;
    u32 hostile_mask = 0, packets = 0, interrupted = 0;
    std::vector<double> decision_ms, observation_ms;
    std::vector<double> snapshot_ms, view_ms, inference_ms, executor_ms;
    std::vector<std::pair<u32,u32>> enemy_income_history;
    std::array<float,2> enemy_center_prev{};
    bool enemy_center_valid = false;
    std::map<std::array<i32,4>, std::vector<UnitMovementPoint>> route_cache;
};
struct AiCommanderPlay {
    std::array<AiCommanderOwner, kPlayerSlotCount> owners;
    std::array<u32, kPlayerSlotCount> kills{}, losses{};
    std::set<u64> accounted;
    bool relations_sent = false, terminal = false;
};
AiCommanderPlay& ai_commander_play() {
    static AiCommanderPlay play;
    return play;
}
void ai_commander_reset_session() { ai_commander_play() = AiCommanderPlay{}; }

float ai_commander_construction_progress(const UnitMovementUnit& unit) {
    if (!unit.under_construction) return 1.0f;
    return std::clamp(static_cast<float>(unit.action_mode) /
        std::max(1u, unit.definition.production_spawn_time), 0.0f, 1.0f);
}

u32 ai_commander_investment(const UnitMovementUnit& unit) {
    const u32 type = unit.saved_type_id != 0 && unit.type_id >= 0x40u &&
        unit.type_id < 0x60u ? unit.saved_type_id : unit.type_id;
    switch (type) {
    case 0x23: return 500;
    case 0x26: return 600;
    case 0x2d: return 900;
    case 0x2b: return 1500;
    default: break;
    }
    UnitLifecycleContext* lifecycle = g_runtime.gameplay_startup_state.lifecycle;
    const UnitMovementDefinition* definition = lifecycle ?
        default_unit_lifecycle_find_definition(*lifecycle, type) : nullptr;
    return definition ? definition->production_resource_cost :
        unit.definition.production_resource_cost;
}
void ai_commander_note_threat(const UnitMovementUnit& unit) {
    if (!p2p_network_launch_parameters().self_play_commander || unit.owner_id >= 8 ||
        !((unit.type_id >= 0x60 && unit.type_id < 0xa0) ||
          (unit.type_id < 0x40 && (unit.type_id & 15) == 0))) return;
    auto& state = ai_commander_play().owners[unit.owner_id].state;
    const u32 frame = gameplay_loop_state().simulation_frame_counter;
    state.threat = {unit.x, unit.y, true}; // the damaged owner's own location
    state.threat_frame = frame;
    state.external_damage_pending = true;
    state.external_damage_unit_id = unit.id;
    if (state.damage_frames.empty() || state.damage_frames.back() != frame)
        state.damage_frames.push_back(frame);
}
void ai_commander_account_death(const UnitMovementUnit& unit,
    const UnitMovementUnit* attacker) {
    if (!p2p_network_launch_parameters().self_play_commander || unit.owner_id >= 8)
        return;
    if (attacker) ai_commander_note_threat(unit);
    AiCommanderPlay& play = ai_commander_play();
    const AiEntityRegistryRecord* identity =
        AiEntityRegistryFindByUnit(ai_entity_registry(), unit);
    // A merge consumes its parents and a self-cancel removes construction;
    // neither is a combat loss. A real kill while merging still counts.
    if (attacker == nullptr) {
        if (unit.under_construction) return;
        for (const auto& merge : play.owners[unit.owner_id].state.merges)
            if (merge.issued) for (std::size_t i = 0; i < merge.units.size(); ++i)
                if (merge.units[i] == unit.id && i < merge.generations.size() &&
                    merge.generations[i] == (identity ? identity->generation : 0u)) return;
    }
    const u64 key = (static_cast<u64>(unit.id) << 32) |
        (identity ? identity->generation : 0u);
    if (!play.accounted.insert(key).second) return;
    // Offline loss diagnosis only; never feed attacker identity or these
    // records into observations, action masks, or the teacher's decisions.
    const auto& controller = play.owners[unit.owner_id];
    if (controller.initialized) {
        const auto tracked = controller.state.units.find(unit.id);
        const u32 squad = tracked == controller.state.units.end() ? 3u : tracked->second.squad;
        append_startup_log("ai-commander: death frame=%lu victim=%lu type=%lu xy=%ld,%ld squad=%lu intent=%lu state=%lu flags=%lu target=%lu killer=%lu killer_type=%lu killer_owner=%lu regroup=%lu order=%lu order_frame=%lu order_xy=%ld,%ld",
            static_cast<unsigned long>(gameplay_loop_state().simulation_frame_counter),
            static_cast<unsigned long>(unit.id), static_cast<unsigned long>(unit.type_id),
            static_cast<long>(unit.x), static_cast<long>(unit.y), static_cast<unsigned long>(squad),
            static_cast<unsigned long>(squad < 3 ? static_cast<u32>(controller.state.squads[squad].intent) : 8u),
            static_cast<unsigned long>(unit.command_state), static_cast<unsigned long>(unit.command_flags),
            static_cast<unsigned long>(unit.target ? unit.target->id : 0u), static_cast<unsigned long>(attacker ? attacker->id : 0u),
            static_cast<unsigned long>(attacker ? attacker->type_id : 0xffffffffu),
            static_cast<unsigned long>(attacker ? attacker->owner_id : 0xffffffffu),
            static_cast<unsigned long>(tracked != controller.state.units.end() && tracked->second.regrouping),
            static_cast<unsigned long>(tracked == controller.state.units.end() ? 0u : static_cast<u32>(tracked->second.last_order.kind)),
            static_cast<unsigned long>(tracked == controller.state.units.end() ? 0u : tracked->second.last_order_frame),
            static_cast<long>(tracked == controller.state.units.end() ? 0 : tracked->second.last_order.target_x),
            static_cast<long>(tracked == controller.state.units.end() ? 0 : tracked->second.last_order.target_y));
    }
    const u32 value = ai_commander_investment(unit);
    play.losses[unit.owner_id] += value; // includes full sunk construction cost
    if (attacker && attacker->owner_id < 8 && attacker->owner_id != unit.owner_id &&
        (g_runtime.gameplay_player_slots.owner_relation_masks[attacker->owner_id] &
            (1u << unit.owner_id)) == 0)
        play.kills[attacker->owner_id] += static_cast<u32>(
            value * ai_commander_construction_progress(unit));
}

void ai_commander_fail(GameplayLoopState& loop, const std::string& reason) {
    append_startup_log("ai-commander: FAILED frame=%lu: %s",
        static_cast<unsigned long>(loop.simulation_frame_counter), reason.c_str());
    ai_commander_play().terminal = true;
    // A file without a terminal record is intentionally unusable for training.
    for (auto& owner : ai_commander_play().owners) owner.rollout.close();
    loop.leave_requested = true;
    loop.process_shutdown_requested = true;
    g_runtime.gameplay_session_flow.process_shutdown_requested = true;
}

CommanderPoint ai_commander_route(const AiObservation& observation,
    const UnitMovementMap& map, CommanderPoint a, CommanderPoint b, float fraction) {
    if (!a.valid || !b.valid || !map.width || !map.height) return {};
    auto& cache = ai_commander_play().owners[observation.local_owner].route_cache;
    const std::array<i32,4> key{a.x,a.y,b.x,b.y};
    const auto cached = cache.find(key);
    if (cached != cache.end()) {
        const auto selected = SelectOwnerBestOpenPathWindowPoint(map, cached->second,
            static_cast<u32>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f));
        return {selected.world_point.x, selected.world_point.y, selected.has_point};
    }
    const i32 width = static_cast<i32>(map.width), height = static_cast<i32>(map.height);
    const auto nearest = [&](CommanderPoint p) {
        i32 best = -1;
        i64 distance = std::numeric_limits<i64>::max();
        for (i32 y = std::max(0, (p.y >> 5) - 6); y <= std::min(height - 1, (p.y >> 5) + 6); ++y)
            for (i32 x = std::max(0, (p.x >> 5) - 6); x <= std::min(width - 1, (p.x >> 5) + 6); ++x) {
                const i32 index = y * width + x;
                if (static_cast<std::size_t>(index) >= observation.tiles.size() ||
                    !observation.tiles[index].passable) continue;
                const i64 dx = x * 32 + 16 - p.x, dy = y * 32 + 16 - p.y;
                if (dx * dx + dy * dy < distance) {
                    best = index;
                    distance = dx * dx + dy * dy;
                }
            }
        return best;
    };
    const i32 start = nearest(a), goal = nearest(b);
    if (start < 0 || goal < 0) return {};
    std::vector<i32> parents(static_cast<std::size_t>(width) * height, -1), queue{start};
    parents[start] = start;
    constexpr i32 offsets[4][2] = {{0,-1},{-1,0},{1,0},{0,1}};
    for (std::size_t cursor = 0; cursor < queue.size() && parents[goal] == -1; ++cursor) {
        const i32 current = queue[cursor];
        for (const auto& delta : offsets) {
            const i32 x = current % width + delta[0], y = current / width + delta[1];
            if (x < 0 || y < 0 || x >= width || y >= height) continue;
            const i32 index = y * width + x;
            if (parents[index] != -1 || !observation.tiles[index].passable) continue;
            parents[index] = current;
            queue.push_back(index);
        }
    }
    if (parents[goal] == -1) return {};
    std::vector<UnitMovementPoint> path;
    for (i32 index = goal;; index = parents[index]) {
        path.push_back({index % width, index / width});
        if (index == start) break;
    }
    std::reverse(path.begin(), path.end());
    if (cache.size() >= 64) cache.clear();
    cache.emplace(key, path);
    const auto selected = SelectOwnerBestOpenPathWindowPoint(map, path,
        static_cast<u32>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f));
    return {selected.world_point.x, selected.world_point.y, selected.has_point};
}

void ai_commander_privileged(CommanderInput& input, u32 owner, u32 hostile_mask,
    u32 frame, const UnitMovementContext& movement) {
    auto& p = input.privileged;
    p.fill(0);
    float resource = 0, sx = 0, sy = 0;
    u32 gathered = 0;
    const auto& players = g_runtime.gameplay_player_slots;
    const UnitLifecycleContext* lifecycle = g_runtime.gameplay_startup_state.lifecycle;
    u32 first_hostile = kPlayerSlotCount;
    for (u32 other = 0; other < kPlayerSlotCount; ++other) {
        if (!(hostile_mask & (1u << other))) continue;
        if (first_hostile == kPlayerSlotCount) first_hostile = other;
        resource += lifecycle ? lifecycle->owner_primary_resources[other] :
            players.owner_primary_resources[other];
        gathered += g_runtime.gameplay_unit_commands.owner_resource_score[other];
        if (lifecycle) {
            p[2] += lifecycle->owner_population_reserved[other] / 180.0f;
            // In this engine "used" is building-supplied capacity; the
            // reserved row is consumption and limit is the fixed 180 cap.
            p[3] += lifecycle->owner_population_used[other] / 180.0f;
        }
        for (u8 level : g_runtime.gameplay_production_runtime.variant_counts[other])
            p[9] += level / 25.0f;
        for (u8 locked : g_runtime.gameplay_production_runtime.lock_flags[other])
            if (locked) p[22] = 1.0f;
    }
    p[0] = std::log1p(resource) / std::log1p(20000.0f);
    auto& income = ai_commander_play().owners[owner].enemy_income_history;
    if (income.empty() || income.back().first != frame) income.push_back({frame,gathered});
    while (income.size() > 1 && frame - income[1].first >= 220) income.erase(income.begin());
    if (!income.empty()) p[1] = std::clamp((gathered - income.front().second) / 1000.0f, 0.0f, 1.0f);
    // Critic-only privileged view. Every field is read from generic unit and
    // owner state that exists for any opponent (built-in, policy or human);
    // no opponent-implementation internals are used (design 3.10).
    u32 army = 0, engaged = 0, enemy_workers = 0, towers = 0, constructing = 0;
    std::vector<std::pair<float,float>> positions;
    for (const UnitMovementUnit* unit : movement.active_units) {
        if (!unit || !unit->active || unit->owner_id >= kPlayerSlotCount ||
            !(hostile_mask & (1u << unit->owner_id))) continue;
        if (unit->type_id >= 0x60 && unit->type_id < 0xa0) {
            p[7] += 1.0f / 20;
            if ((unit->type_id & 15) == 0) p[8] += 1.0f / 3;
            if ((unit->type_id & 15) == 3) ++towers;
            if (unit->under_construction) ++constructing;
            p[10] += unit->deferred_command_count / 8.0f;
        } else if (unit->type_id < 0x40 && (unit->type_id & 15) == 0) {
            ++enemy_workers;
        } else if ((unit->type_flags & 0x20) && (unit->type_id & 15) != 0) {
            ++army;
            p[5] += (unit->health + unit->runtime_stat_1c + unit->runtime_stat_20) / 20000.0f;
            p[6] += ai_commander_investment(*unit) / 10000.0f;
            sx += unit->x;
            sy += unit->y;
            engaged += (unit->command_flags & 0x10) != 0;
            positions.push_back({float(unit->x), float(unit->y)});
        }
    }
    p[4] = army / 40.0f;
    auto& self = ai_commander_play().owners[owner];
    if (army) {
        p[11] = sx / (army * 4096.0f);
        p[12] = sy / (army * 4096.0f);
        if (self.enemy_center_valid) {
            const float dx = p[11] - self.enemy_center_prev[0], dy = p[12] - self.enemy_center_prev[1];
            const float length = std::hypot(dx, dy);
            if (length > 1e-6f) { p[14] = dx / length; p[15] = dy / length; }
        }
        self.enemy_center_prev = {p[11], p[12]};
        self.enemy_center_valid = true;
        float spread = 0;
        for (const auto& position : positions)
            spread += std::hypot(position.first / 4096.0f - p[11], position.second / 4096.0f - p[12]);
        p[20] = std::clamp(spread / army * 4096.0f / 256.0f, 0.0f, 1.0f);
        p[17] = float(engaged) / army;
    } else {
        self.enemy_center_valid = false;
    }
    if (first_hostile < kPlayerSlotCount) {
        const auto& slot = g_runtime.gameplay_startup_state.owner_slots[first_hostile];
        if (army) p[13] = std::hypot(p[11] - slot.start_x / 4096.0f,
            p[12] - slot.start_y / 4096.0f);
    }
    if (army) {
        const auto& own_slot = g_runtime.gameplay_startup_state.owner_slots[owner];
        p[16] = std::hypot(p[11] - own_slot.start_x / 4096.0f, p[12] - own_slot.start_y / 4096.0f);
    }
    p[18] = std::min(1.0f, constructing / 4.0f);
    p[19] = std::min(1.0f, enemy_workers / 40.0f);
    p[21] = std::min(1.0f, towers / 4.0f);
    p[23] = ai_commander_play().kills[owner] / 10000.0f;
    p[24] = ai_commander_play().losses[owner] / 10000.0f;
}

CommanderServices ai_commander_services(u32 owner, const AiObservation& observation,
    const AiActionPlanInput& plan) {
    CommanderServices services;
    services.diagnostic = [](const std::string& message) {
        append_startup_log("%s", message.c_str());
    };
    std::map<u32,u32> generations;
    if (plan.movement) for (const auto* unit : plan.movement->active_units) {
        if (!unit || !unit->active) continue;
        const auto* identity = AiEntityRegistryFindByUnit(ai_entity_registry(), *unit);
        if (identity) generations.emplace(unit->id, identity->generation);
    }
    services.generation_of = [generations = std::move(generations)](u32 id) {
        const auto found = generations.find(id);
        return found == generations.end() ? 0u : found->second;
    };
    services.packet_budget = p2p_network_launch_parameters().self_play_versus ? 32u : 64u;
    services.construction_progress = [&plan, owner](u32 id) {
        if (plan.movement) for (const auto* unit : plan.movement->active_units)
            if (unit && unit->active && unit->id == id && unit->owner_id == owner)
                return ai_commander_construction_progress(*unit);
        return 1.0f;
    };
    services.validator = [&plan](const AiSemanticAction& action) {
        return static_cast<bool>(PlanAiSemanticActionV1(plan, action));
    };
    services.production_info = [owner](AiProductionRequestKind kind, u32 id) {
        CommanderProductionInfo info;
        if (kind == AiProductionRequestKind::research) {
            const auto* definition = default_production_order_definition(id);
            if (definition) info.cost = CheckProductionOrderAvailability(
                g_runtime.gameplay_production_runtime, *definition, owner).primary_cost;
        } else {
            auto* lifecycle = g_runtime.gameplay_startup_state.lifecycle;
            const auto* definition = lifecycle ? default_unit_lifecycle_find_definition(*lifecycle, id) : nullptr;
            if (definition) {
                info.cost = definition->production_resource_cost;
                info.population = definition->production_population_cost;
                info.width = definition->footprint_width_tiles;
                info.height = definition->footprint_height_tiles;
            }
        }
        return info;
    };
    services.route_point = [&observation, &plan](CommanderPoint a, CommanderPoint b, float fraction) {
        return ai_commander_route(observation, plan.movement->map, a, b, fraction);
    };
    services.cumulative_gathered = g_runtime.gameplay_unit_commands.owner_resource_score[owner];
    services.kills_investment = ai_commander_play().kills[owner];
    services.losses_investment = ai_commander_play().losses[owner];
    services.autoscout = p2p_network_launch_parameters().self_play_autoscout;
    services.curriculum_stage = p2p_network_launch_parameters().self_play_curriculum;
    services.teacher_variant = owner == 2 && p2p_network_launch_parameters().self_play_versus ?
        p2p_network_launch_parameters().self_play_teacher_variant2 :
        p2p_network_launch_parameters().self_play_teacher_variant;
    for (u32 other = 0; other < kPlayerSlotCount; ++other)
        if (ai_commander_play().owners[owner].hostile_mask & (1u << other)) {
            services.public_enemy_tribe = g_runtime.gameplay_startup_state.owner_faction_ids[other];
            break;
        }
    return services;
}

AiProductionAvailability ai_commander_check_research(u32 owner, u32 order, void*) {
    AiProductionAvailability result;
    const auto* lifecycle = g_runtime.gameplay_startup_state.lifecycle;
    const auto* definition = default_production_order_definition(order);
    if (!lifecycle || !definition || owner >= kPlayerSlotCount) return result;
    // Read-only copy: keep the engine's resource mirrors and update cadence
    // intact while applying the same research check to the current balance.
    auto snapshot = g_runtime.gameplay_production_runtime;
    snapshot.owner_primary_resources[owner] = lifecycle->owner_primary_resources[owner];
    snapshot.owner_secondary_resources[owner] = lifecycle->owner_secondary_resources[owner];
    const auto check = CheckProductionOrderAvailability(snapshot, *definition, owner);
    result.available = check.available;
    result.code = check.code;
    result.primary_cost = check.primary_cost;
    result.secondary_cost = check.secondary_cost;
    return result;
}

// Per-owner teacher role: -AITEACHER makes every policy owner a rule
// commander; -AITEACHER2 (with -AIVS) makes only the second owner one, so a
// learned policy can play against a rule-commander variant.
bool ai_commander_owner_teacher_flag(u32 owner_id) {
    const auto& launch = p2p_network_launch_parameters();
    return launch.self_play_teacher ||
        (owner_id == 2 && launch.self_play_versus && launch.self_play_teacher2);
}

CommanderDecision ai_commander_decide(AiCommanderOwner& owner, const CommanderView& view) {
    const auto& launch = p2p_network_launch_parameters();
    const u32 owner_index = static_cast<u32>(&owner - ai_commander_play().owners.data());
    if (owner.model.loaded()) return owner.model.decide(view.input, view.mask, owner.rng,
        launch.self_play_deterministic, [&](std::size_t head, const CommanderAction& prefix,
            CommanderMask& mask) { CommanderLegalHeadMask(view, prefix, head, mask); });
    CommanderDecision decision;
    decision.mask = view.mask;
    const bool teacher = ai_commander_owner_teacher_flag(owner_index);
    if (teacher) decision.action = CommanderTeacherAction(owner.state, view);
    for (std::size_t head = 0; head < 8; ++head) {
        CommanderLegalHeadMask(view, decision.action, head, decision.mask);
        const std::size_t offset = kCommanderHeadOffsets[head];
        std::vector<u8> legal;
        for (std::size_t a = 0; a < kCommanderHeadSizes[head]; ++a)
            if (decision.mask[offset + a]) legal.push_back(static_cast<u8>(a));
        if (legal.empty()) throw std::runtime_error("empty commander head mask");
        if (teacher) {
            if (decision.action[head] >= kCommanderHeadSizes[head] ||
                !decision.mask[offset + decision.action[head]])
                throw std::runtime_error("teacher selected a masked action");
        } else {
            decision.action[head] = legal[owner.rng.next() % legal.size()];
            decision.logp[head] = -std::log(static_cast<float>(legal.size()));
        }
    }
    return decision;
}

void run_ai_commander_frame(GameplayLoopState& loop) {
    AiCommanderPlay& play = ai_commander_play();
    const auto& launch = p2p_network_launch_parameters();
    const u32 frame = loop.simulation_frame_counter;
    if (play.terminal || frame % 8 != 1) return;
    UnitMovementContext* movement = default_gameplay_movement_context();
    if (!movement) return;
    try {
        u32 controlled_mask = 0;
        for (u32 owner = 0; owner < kPlayerSlotCount; ++owner)
            if (g_runtime.ai_play_owner_slots[owner]) controlled_mask |= 1u << owner;
        if (launch.self_play_1v1) controlled_mask |= 1u;
        if (launch.self_play_versus && !play.relations_sent) {
            std::vector<Mode1AiBatchPacketRequest> relations;
            for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
                if (!(controlled_mask & (1u << owner))) continue;
                Mode1AiBatchPacketRequest packet;
                packet.packed_opcode = (0x14u << 24) | owner;
                packet.arg0 = (g_runtime.gameplay_player_slots.global_active_slot_mask & (1u << owner)) ? 1u : 0u;
                packet.arg2 = (g_runtime.gameplay_player_slots.owner_relation_masks[owner] & ~controlled_mask) | (1u << owner);
                packet.arg3 = (g_runtime.gameplay_player_slots.owner_visibility_masks[owner] & ~controlled_mask) | (1u << owner);
                relations.push_back(packet);
            }
            if (PublishAiMode1GameplayPacketBatch(relations) != Mode1AiBatchCode::accepted)
                throw std::runtime_error("cannot publish explicit self-play relations");
            play.relations_sent = true;
            append_startup_log("ai-commander: relation packets owners=0x%lx frame=%lu",
                static_cast<unsigned long>(controlled_mask), static_cast<unsigned long>(frame));
            // Relations must pass through the frame gate before observations.
            return;
        }
        GameplayProductionActionState& production = gameplay_production_action_state();
        sync_default_gameplay_production_action_units(production, ui_overlay_state());
        struct PendingOwner {
            u32 id = 0, packet_count = 0, event = 0;
            bool decision_due = false;
            double elapsed_ms = 0;
            CommanderState state;
            CommanderInput input;
            CommanderDecision decision;
            // DAgger: the rule commander's decision for the same view.
            CommanderDecision label;
            bool has_label = false;
            std::array<float,4> potential{};
        };
        std::vector<PendingOwner> pending;
        std::vector<Mode1AiBatchPacketRequest> batch;
        for (u32 owner_id = 0; owner_id < kPlayerSlotCount; ++owner_id) {
            if (!(controlled_mask & (1u << owner_id))) continue;
            auto& owner = play.owners[owner_id];
            if (!owner.initialized) {
                owner.hostile_mask = g_runtime.gameplay_started_player_mask &
                    ~g_runtime.gameplay_player_slots.owner_relation_masks[owner_id] & ~(1u << owner_id);
                const char* weights = owner_id == 2 && launch.self_play_weights2[0] ?
                    launch.self_play_weights2.data() : launch.self_play_weights.data();
                std::string error;
                const bool teacher_owner = ai_commander_owner_teacher_flag(owner_id);
                if (!teacher_owner && *weights && !owner.model.load(weights, &error))
                    throw std::runtime_error("weight load: " + error);
                if (!teacher_owner && !owner.model.loaded() && !launch.self_play_random)
                    throw std::runtime_error("commander requires -AIWEIGHTS, -AITEACHER or -AIRANDOM");
                owner.rng.seed(launch.self_play_seed, owner_id, owner.model.version());
                std::string rollout_path;
                if (launch.self_play_rollout[0]) {
                    rollout_path = launch.self_play_rollout.data();
                    if (owner_id == 2 && launch.self_play_versus) rollout_path += ".owner2.rlo";
                } else rollout_path = ai_selfplay_output_path(
                    ("commander_" + std::to_string(owner_id) + ".rlo").c_str());
                if (!owner.rollout.open(rollout_path, owner_id, launch.self_play_seed, owner.model.version()))
                    throw std::runtime_error("cannot open rollout " + rollout_path);
                owner.initialized = true;
                append_startup_log("ai-commander: owner=%lu version=%lu hostile=0x%lx teacher=%d rollout=%s",
                    static_cast<unsigned long>(owner_id), static_cast<unsigned long>(owner.model.version()),
                    static_cast<unsigned long>(owner.hostile_mask), teacher_owner, rollout_path.c_str());
            }
            const auto started = std::chrono::steady_clock::now();
            std::vector<u8> explored, visible;
            AiObservationBuildResult observation;
            if (!build_default_ai_play_observation(loop, owner_id, explored, visible, observation, true))
                throw std::runtime_error("commander observation failed");
            const auto snapshot_done = std::chrono::steady_clock::now();
            AiLiveProductionValidationContext live;
            live.unit_references = &g_runtime.unit_reference_tables;
            live.check_unit_requirements = default_ai_play_check_unit_requirements;
            live.check_research_requirements = ai_commander_check_research;
            live.check_placement = default_ai_play_check_placement;
            AiActionPlanInput plan;
            plan.local_owner = owner_id;
            plan.players = &g_runtime.gameplay_player_slots;
            plan.movement = movement;
            plan.unit_visible = default_ai_play_unit_visible;
            plan.visible_tiles = &visible;
            plan.explored_tiles = &explored;
            plan.production_available = CheckAiLiveProductionAvailability;
            plan.production_availability_user_data = &live;
            plan.ability_available = default_ai_play_check_ability;
            const CommanderServices services = ai_commander_services(owner_id, observation.observation, plan);
            CommanderView view = BuildCommanderView(owner.state, observation.observation, services);
            ai_commander_privileged(view.input, owner_id, owner.hostile_mask, frame, *movement);
            QuantizeCommanderMap(view.input);
            const auto view_done = std::chrono::steady_clock::now();
            owner.observation_ms.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count());
            CommanderDecision decision;
            if (view.decision_due) decision = ai_commander_decide(owner, view);
            // -AIDAGGER: while the learned policy acts, record what the rule
            // commander would have done here (legal under masks conditioned
            // on its own head prefix) so BC can learn on the policy's states.
            CommanderDecision label;
            bool has_label = false;
            if (view.decision_due && launch.self_play_dagger && !ai_commander_owner_teacher_flag(owner_id)) {
                label.mask = view.mask;
                label.action = CommanderTeacherAction(owner.state, view);
                for (std::size_t head = 0; head < 8; ++head)
                    CommanderLegalHeadMask(view, label.action, head, label.mask);
                has_label = true;
            }
            const auto inference_done = std::chrono::steady_clock::now();
            CommanderState candidate = owner.state;
            const auto actions = CommanderExecute(candidate, observation.observation, view,
                view.decision_due ? &decision.action : nullptr);
            std::vector<Mode1AiBatchPacketRequest> packets;
            for (const auto& action : actions) {
                // Movement is always point-only. Target-id orders undergo the
                // same current owner-visibility predicate as the engine.
                if (action.kind == AiSemanticActionKind::move && action.target_unit_id)
                    throw std::runtime_error("commander move contains target id");
                const AiActionPlanResult result = PlanAiSemanticActionV1(plan, action);
                if (!result) throw std::runtime_error("commander executor action rejected: " +
                    std::to_string(static_cast<u32>(result.code)));
                for (const auto& action_packet : result.packets) {
                    Mode1AiBatchPacketRequest packet;
                    packet.packed_opcode = action_packet.packed_opcode;
                    packet.arg0 = action_packet.arg0;
                    packet.unit_offset = action_packet.unit_offset;
                    packet.arg1 = action_packet.arg1;
                    packet.arg2 = action_packet.arg2;
                    packet.arg3 = action_packet.arg3;
                    packet.local_broadcast = !g_runtime.ai_play_owner_slots[owner_id];
                    packets.push_back(packet);
                }
            }
            if (packets.size() > 64) throw std::runtime_error("commander frame packet budget exceeded");
            if (view.decision_due) {
                const auto ms = [](auto a, auto b) { return std::chrono::duration<double,std::milli>(b-a).count(); };
                owner.snapshot_ms.push_back(ms(started,snapshot_done));
                owner.view_ms.push_back(ms(snapshot_done,view_done));
                owner.inference_ms.push_back(ms(view_done,inference_done));
                owner.executor_ms.push_back(ms(inference_done,std::chrono::steady_clock::now()));
            }
            PendingOwner staged;
            staged.id = owner_id;
            staged.packet_count = static_cast<u32>(packets.size());
            staged.event = view.event;
            staged.decision_due = view.decision_due;
            staged.elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            staged.state = std::move(candidate);
            staged.input = view.input;
            staged.decision = decision;
            staged.label = label;
            staged.has_label = has_label;
            staged.potential = view.potential_components;
            pending.push_back(std::move(staged));
            batch.insert(batch.end(), packets.begin(), packets.end());
        }
        if (batch.size() > 64) throw std::runtime_error("combined commander frame packet budget exceeded");
        const auto published = PublishAiMode1GameplayPacketBatch(batch);
        if (published != Mode1AiBatchCode::accepted && published != Mode1AiBatchCode::empty)
            throw std::runtime_error("commander atomic publish failed");
        for (auto& staged : pending) {
            auto& owner = play.owners[staged.id];
            owner.state = std::move(staged.state);
            owner.packets += staged.packet_count;
            if (staged.decision_due) {
                owner.interrupted += staged.event != 0;
                owner.decision_ms.push_back(staged.elapsed_ms);
                if (!owner.rollout.append(frame, static_cast<u8>(staged.event), ai_commander_owner_teacher_flag(staged.id),
                    CommanderRolloutStatus::decision, staged.input, staged.decision, staged.potential,
                    staged.has_label ? &staged.label : nullptr))
                    throw std::runtime_error("rollout write failed");
            }
        }
    } catch (const std::exception& error) { ai_commander_fail(loop, error.what()); }
}

void ai_commander_finish(GameplayLoopState& loop, bool timed_out) {
    auto& play = ai_commander_play();
    if (play.terminal) return;
    play.terminal = true;
    try {
    const auto* movement = default_gameplay_movement_context();
    if (!movement) return;
    const u32 frame = loop.simulation_frame_counter;
    // Reuse the engine's existing flight checksums without publishing a sync
    // packet or changing recorder state. This supports same-seed repeat gates.
    const auto& flight = p2p_flight_recorder_state();
    const u32 trace_count = std::min(flight.captured_frame_count, kP2PFlightFrameCount);
    const u32 trace_first = flight.captured_frame_count > kP2PFlightFrameCount ?
        flight.captured_frame_count % kP2PFlightFrameCount : 0;
    std::ofstream trace(ai_selfplay_output_path("commander_sync.csv"));
    trace << "frame,rng,identity,position,effects,combined\n";
    for (u32 i = 0; i < trace_count; ++i) {
        const auto& row = flight.frames[(trace_first + i) % kP2PFlightFrameCount];
        trace << row.simulation_frame << ',' << row.gameplay_rng_seed << ','
            << row.unit_identity_checksum << ',' << row.unit_position_checksum << ','
            << row.effect_checksum << ',' << row.combined_checksum << '\n';
    }
    trace.flush();
    if (!trace) throw std::runtime_error("commander checksum trace write failed");
    std::array<u32, kPlayerSlotCount> buildings{};
    for (const auto* unit : movement->active_units)
        if (unit && unit->active && unit->health > 0 && unit->owner_id < kPlayerSlotCount &&
            unit->type_id >= 0x60 && unit->type_id < 0xa0 && unit->type_id != kGameplayEndExcludedEliteType)
            ++buildings[unit->owner_id];
    for (u32 owner_id = 0; owner_id < kPlayerSlotCount; ++owner_id) {
        auto& owner = play.owners[owner_id];
        if (!owner.initialized) continue;
        std::vector<u8> explored, visible;
        AiObservationBuildResult observation;
        if (!build_default_ai_play_observation(loop, owner_id, explored, visible, observation, true)) {
            throw std::runtime_error("terminal commander observation failed");
        }
        AiActionPlanInput plan;
        plan.local_owner = owner_id;
        plan.movement = movement;
        CommanderServices services = ai_commander_services(owner_id, observation.observation, plan);
        services.validator = [](const AiSemanticAction&) { return false; };
        CommanderView view = BuildCommanderView(owner.state, observation.observation, services);
        ai_commander_privileged(view.input, owner_id, owner.hostile_mask, frame, *movement);
        QuantizeCommanderMap(view.input);
        CommanderDecision terminal;
        if (owner.model.loaded()) {
            auto rng = owner.rng;
            // Value depends only on trunk and privileged features, so forced
            // no-op heads are sufficient to evaluate the truncation bootstrap.
            CommanderMask mask{};
            for (std::size_t h = 0; h < 8; ++h) mask[kCommanderHeadOffsets[h]] = 1;
            terminal = owner.model.decide(view.input, mask, rng, true);
        }
        bool hostile_alive = false;
        for (u32 other = 0; other < kPlayerSlotCount; ++other)
            if ((owner.hostile_mask & (1u << other)) && buildings[other]) hostile_alive = true;
        CommanderRolloutStatus status = CommanderRolloutStatus::invalid;
        // Elimination at the cap takes precedence over a time-limit bootstrap.
        if (frame >= 1800 && !buildings[owner_id]) status = CommanderRolloutStatus::loss;
        else if (frame >= 1800 && owner.hostile_mask && !hostile_alive) status = CommanderRolloutStatus::win;
        else if (timed_out) status = CommanderRolloutStatus::truncated;
        if (!owner.rollout.append(frame, 0, ai_commander_owner_teacher_flag(owner_id),
            status, view.input, terminal, view.potential_components))
            throw std::runtime_error("terminal rollout write failed");
        owner.rollout.close();
        const auto percentile = [](std::vector<double> values, double fraction) {
            if (values.empty()) return 0.0;
            std::sort(values.begin(), values.end());
            return values[static_cast<std::size_t>((values.size() - 1) * fraction)];
        };
        const std::string path = ai_selfplay_output_path(
            ("commander_metrics_" + std::to_string(owner_id) + ".json").c_str());
        std::ofstream out(path);
        out << "{\"owner\":" << owner_id << ",\"end_frame\":" << frame
            << ",\"status\":" << unsigned(status) << ",\"decisions\":" << owner.state.decision_count
            << ",\"interrupts\":" << owner.interrupted << ",\"packets\":" << owner.packets
            << ",\"silent_rejections\":" << owner.state.silent_rejections
            << ",\"mask_violations\":" << owner.state.mask_violations
            << ",\"checksum_frames\":" << trace_count
            << ",\"kills_investment\":" << play.kills[owner_id]
            << ",\"losses_investment\":" << play.losses[owner_id]
            << ",\"decision_p50_ms\":" << percentile(owner.decision_ms, 0.5)
            << ",\"decision_p99_ms\":" << percentile(owner.decision_ms, 0.99)
            << ",\"observation_p99_ms\":" << percentile(owner.observation_ms, 0.99)
            << ",\"snapshot_p99_ms\":" << percentile(owner.snapshot_ms, 0.99)
            << ",\"commander_view_p99_ms\":" << percentile(owner.view_ms, 0.99)
            << ",\"inference_p99_ms\":" << percentile(owner.inference_ms, 0.99)
            << ",\"executor_p99_ms\":" << percentile(owner.executor_ms, 0.99) << "}\n";
    }
    } catch (const std::exception& error) { ai_commander_fail(loop, error.what()); }
}
