#include "ranker_ai_scripted_bot.h"
#include "ranker_unit_commands.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ranker {
namespace {

constexpr u32 kAttackCommand = 5u;
constexpr u32 kPlayerOwnerCount = 8u;
constexpr u32 kMacroRetryBackoffFrames = 64u;

bool command_line_token_equal(const std::string& token, const char* expected) {
    const std::size_t expected_size = std::char_traits<char>::length(expected);
    if (token.size() != expected_size) {
        return false;
    }
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(token[index])) !=
            std::tolower(static_cast<unsigned char>(expected[index]))) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> tokenize_command_line(const char* command_line) {
    std::vector<std::string> tokens;
    if (command_line == nullptr) {
        return tokens;
    }
    std::string token;
    bool quoted = false;
    for (const char* cursor = command_line;; ++cursor) {
        const char value = *cursor;
        if (value == '"') {
            quoted = !quoted;
            continue;
        }
        if (value == '\0' ||
            (!quoted && std::isspace(static_cast<unsigned char>(value)))) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            if (value == '\0') {
                break;
            }
            continue;
        }
        token.push_back(value);
    }
    return tokens;
}

bool unit_can_attack(const AiObservedUnit& unit) {
    return unit.controlled && unit.alive && unit.type_id < 0x60u &&
        kAttackCommand < 32u &&
        (unit.type_flags & (1u << kAttackCommand)) != 0;
}

bool unit_can_harvest(const AiObservedUnit& unit) {
    constexpr u32 kHarvestCommand = 7u;
    return unit.controlled && unit.alive && unit.type_id < 0x60u &&
        (unit.type_flags & (1u << kHarvestCommand)) != 0;
}

bool unit_is_harvesting(const AiObservedUnit& unit) {
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return (state >= kUnitStateWorkerApproachHarvest &&
            state <= kUnitStateWorkerHarvestFailed) ||
        unit.cargo_amount != 0 || (unit.command_flags & 4u) != 0;
}

bool unit_is_constructing(const AiObservedUnit& unit) {
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return state >= kUnitStateLegacySpawnPlacementStart &&
        state <= kUnitStateLegacySpawnPlacementApproach;
}

u32 pending_unit_count(const AiObservedUnit& producer, u32 unit_type) {
    u32 count = producer.queued_production_type_id == unit_type ? 1u : 0u;
    const u32 command_count = std::min<u32>(producer.deferred_command_count,
        static_cast<u32>(producer.deferred_commands.size()));
    for (u32 index = 0; index < command_count; ++index) {
        const AiObservedQueuedCommand& command =
            producer.deferred_commands[index];
        if ((command.state & kUnitCommandStateMask) == 0x10u &&
            static_cast<u32>(command.command_value_or_target) ==
                unit_type) {
            ++count;
        }
    }
    return count;
}

bool is_hostile_visible_unit(const AiObservation& observation,
    const AiObservedUnit& unit) {
    if (!unit.visible || !unit.alive || unit.owner_id == observation.local_owner ||
        unit.owner_id >= kPlayerOwnerCount ||
        (observation.active_owner_mask & (1u << unit.owner_id)) == 0) {
        return false;
    }
    return (observation.local_relation_mask & (1u << unit.owner_id)) == 0;
}

i64 squared_distance(i32 x0, i32 y0, i32 x1, i32 y1) {
    const i64 dx = static_cast<i64>(x0) - x1;
    const i64 dy = static_cast<i64>(y0) - y1;
    return dx * dx + dy * dy;
}

UnitMovementPoint spiral_offset(u32 index) {
    if (index == 0) {
        return {};
    }
    u32 ring = 1;
    while (index > 8u * ring) {
        index -= 8u * ring;
        ++ring;
    }
    const i32 radius = static_cast<i32>(ring);
    const u32 side_length = ring * 2u;
    const u32 side = (index - 1u) / side_length;
    const i32 offset = static_cast<i32>((index - 1u) % side_length);
    switch (side) {
    case 0:
        return {-radius + offset, -radius};
    case 1:
        return {radius, -radius + offset};
    case 2:
        return {radius - offset, radius};
    default:
        return {-radius, radius - offset};
    }
}

UnitMovementPoint next_build_point(TyranoScriptedBotState& state,
    const AiObservation& observation) {
    const i32 center_x = observation.start_x >> 5;
    const i32 center_y = observation.start_y >> 5;
    const u32 maximum_attempts = std::max<u32>(
        observation.map_width_tiles * observation.map_height_tiles, 1u);
    for (u32 attempt = 0; attempt < maximum_attempts; ++attempt) {
        const UnitMovementPoint offset =
            spiral_offset(state.placement_probe_index++);
        const i32 tile_x = center_x + offset.x;
        const i32 tile_y = center_y + offset.y;
        if (tile_x >= 0 && tile_y >= 0 &&
            static_cast<u32>(tile_x) < observation.map_width_tiles &&
            static_cast<u32>(tile_y) < observation.map_height_tiles) {
            return {tile_x << 5, tile_y << 5};
        }
    }
    return {std::max(observation.start_x, 0),
        std::max(observation.start_y, 0)};
}

UnitMovementPoint exploration_point(const TyranoScriptedBotState& state,
    const AiObservation& observation) {
    const i32 max_x = observation.map_width_tiles == 0 ? 0 :
        static_cast<i32>((observation.map_width_tiles - 1u) * 32u);
    const i32 max_y = observation.map_height_tiles == 0 ? 0 :
        static_cast<i32>((observation.map_height_tiles - 1u) * 32u);
    switch (state.exploration_index % 5u) {
    case 0:
        return {max_x / 2, max_y / 2};
    case 1:
        return {32, 32};
    case 2:
        return {std::max(max_x - 32, 0), 32};
    case 3:
        return {std::max(max_x - 32, 0), std::max(max_y - 32, 0)};
    default:
        return {32, std::max(max_y - 32, 0)};
    }
}

struct HarvestAssignment {
    const AiObservedUnit* worker = nullptr;
    u32 tile_index = 0;
    UnitMovementPoint point{};
    i64 distance = 0;
};

HarvestAssignment nearest_visible_resource_assignment(
    const AiObservation& observation,
    const std::vector<const AiObservedUnit*>& workers) {
    HarvestAssignment best{};
    const u64 expected_tile_count =
        static_cast<u64>(observation.map_width_tiles) *
        observation.map_height_tiles;
    if (expected_tile_count != observation.tiles.size() ||
        observation.map_width_tiles == 0) {
        return best;
    }

    for (const AiObservedUnit* worker : workers) {
        if (worker == nullptr || !unit_can_harvest(*worker) ||
            unit_is_harvesting(*worker) || unit_is_constructing(*worker)) {
            continue;
        }
        for (u32 tile_index = 0; tile_index < observation.tiles.size();
             ++tile_index) {
            const AiObservedMapTile& tile = observation.tiles[tile_index];
            if (!tile.visible || !tile.passable || tile.resource_amount == 0) {
                continue;
            }
            const u32 tile_x = tile_index % observation.map_width_tiles;
            const u32 tile_y = tile_index / observation.map_width_tiles;
            const UnitMovementPoint point{
                static_cast<i32>(tile_x * 32u + 16u),
                static_cast<i32>(tile_y * 32u + 16u)};
            const i64 distance = squared_distance(
                worker->x, worker->y, point.x, point.y);
            if (best.worker == nullptr || distance < best.distance ||
                (distance == best.distance && worker->id < best.worker->id) ||
                (distance == best.distance && worker->id == best.worker->id &&
                    tile_index < best.tile_index)) {
                best.worker = worker;
                best.tile_index = tile_index;
                best.point = point;
                best.distance = distance;
            }
        }
    }
    return best;
}

bool intent_retry_ready(const TyranoScriptedBotState& state,
    TyranoScriptedBotIntent intent, u32 frame) {
    const std::size_t index = static_cast<std::size_t>(intent);
    if (index >= state.intent_retry_after_frame.size()) {
        return false;
    }
    const u32 retry_frame = state.intent_retry_after_frame[index];
    return retry_frame == 0 || static_cast<i32>(frame - retry_frame) >= 0;
}

u32 live_type_count(const AiObservation& observation, u32 type_id,
    bool completed_only) {
    return static_cast<u32>(std::count_if(observation.units.begin(),
        observation.units.end(), [=](const AiObservedUnit& unit) {
            return unit.controlled && unit.alive && unit.type_id == type_id &&
                (!completed_only || !unit.under_construction);
        }));
}

const AiObservedUnit* select_completed_type(const AiObservation& observation,
    u32 type_id) {
    const AiObservedUnit* selected = nullptr;
    for (const AiObservedUnit& unit : observation.units) {
        if (!unit.controlled || !unit.alive || unit.under_construction ||
            unit.type_id != type_id) {
            continue;
        }
        if (selected == nullptr ||
            unit.deferred_command_count < selected->deferred_command_count ||
            (unit.deferred_command_count == selected->deferred_command_count &&
                unit.id < selected->id)) {
            selected = &unit;
        }
    }
    return selected;
}

const AiObservedUnit* select_builder(
    const std::vector<const AiObservedUnit*>& workers) {
    const AiObservedUnit* fallback = nullptr;
    for (const AiObservedUnit* worker : workers) {
        if (worker == nullptr || !worker->alive ||
            unit_is_constructing(*worker)) {
            continue;
        }
        if (fallback == nullptr || worker->id < fallback->id) {
            fallback = worker;
        }
        if (!unit_is_harvesting(*worker)) {
            return worker;
        }
    }
    return fallback;
}

u32 pending_type_count(const AiObservation& observation, u32 producer_type,
    u32 produced_type) {
    u32 count = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive && !unit.under_construction &&
            unit.type_id == producer_type) {
            count += pending_unit_count(unit, produced_type);
        }
    }
    return count;
}

enum class TyranoMacroGoalKind : u32 {
    build,
    produce,
    research,
};

struct TyranoMacroGoal {
    TyranoMacroGoalKind kind;
    u32 target_id;
    u32 target_count;
    u32 primary_cost;
    u32 source_type;
    TyranoScriptedBotIntent intent;
};

constexpr std::array<TyranoMacroGoal, 20> kReplayDerivedBuildOrder{{
    {TyranoMacroGoalKind::build, kTyranoPopulationNestType, 1, 200,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_population_nest},
    {TyranoMacroGoalKind::build, kTyranoEggNestType, 1, 400,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_egg_nest},
    {TyranoMacroGoalKind::build, kTyranoEggNestType, 2, 400,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_egg_nest},
    {TyranoMacroGoalKind::produce, kTyranoMasosType, 3, 100,
        kTyranoEggNestType, TyranoScriptedBotIntent::produce_masos},
    {TyranoMacroGoalKind::build, kTyranoPopulationNestType, 2, 200,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_population_nest},
    {TyranoMacroGoalKind::produce, kTyranoMasosType, 5, 100,
        kTyranoEggNestType, TyranoScriptedBotIntent::produce_masos},
    {TyranoMacroGoalKind::build, kTyranoEggNestType, 3, 400,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_egg_nest},
    {TyranoMacroGoalKind::produce, kTyranoMasosType, 7, 100,
        kTyranoEggNestType, TyranoScriptedBotIntent::produce_masos},
    {TyranoMacroGoalKind::build, kTyranoPopulationNestType, 3, 200,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_population_nest},
    {TyranoMacroGoalKind::produce, kTyranoMasosType, 9, 100,
        kTyranoEggNestType, TyranoScriptedBotIntent::produce_masos},
    {TyranoMacroGoalKind::research, kTyranoHarvestUpgradeOrder, 1, 500,
        kTyranoNestType, TyranoScriptedBotIntent::research_harvest_upgrade},
    {TyranoMacroGoalKind::produce, kTyranoMasosType, 15, 100,
        kTyranoEggNestType, TyranoScriptedBotIntent::produce_masos},
    {TyranoMacroGoalKind::build, kTyranoPopulationNestType, 4, 200,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_population_nest},
    {TyranoMacroGoalKind::build, kTyranoLandNestType, 1, 600,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_land_nest},
    {TyranoMacroGoalKind::build, kTyranoPopulationNestType, 5, 200,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_population_nest},
    {TyranoMacroGoalKind::research, kTyranoGroundAttackUpgradeOrder, 1, 200,
        kTyranoLandNestType, TyranoScriptedBotIntent::research_ground_attack},
    {TyranoMacroGoalKind::build, kTyranoNestType, 2, 1000,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_second_tyrano_nest},
    {TyranoMacroGoalKind::build, kTyranoPopulationNestType, 6, 200,
        kTyranoWorkerType, TyranoScriptedBotIntent::build_population_nest},
    {TyranoMacroGoalKind::research, kTyranoMovementUpgradeOrder, 1, 400,
        kTyranoLandNestType, TyranoScriptedBotIntent::research_movement_upgrade},
    {TyranoMacroGoalKind::produce, kTyranoDilophosType, 6, 250,
        kTyranoEggNestType, TyranoScriptedBotIntent::produce_dilophos},
}};

bool research_goal_requested(const TyranoScriptedBotState& state,
    u32 order_id) {
    switch (order_id) {
    case kTyranoHarvestUpgradeOrder:
        return state.harvest_upgrade_requested;
    case kTyranoGroundAttackUpgradeOrder:
        return state.ground_attack_upgrade_requested;
    case kTyranoMovementUpgradeOrder:
        return state.movement_upgrade_requested;
    default:
        return false;
    }
}

TyranoScriptedBotDecision ready(TyranoScriptedBotIntent intent,
    AiSemanticAction action) {
    TyranoScriptedBotDecision decision{};
    decision.code = TyranoScriptedBotDecisionCode::action_ready;
    decision.intent = intent;
    decision.action = std::move(action);
    return decision;
}

} // namespace

void ResetTyranoScriptedBot(TyranoScriptedBotState& state) {
    state = TyranoScriptedBotState{};
}

bool IsAiPlayCommandLineEnabled(const char* command_line) {
    const std::vector<std::string> tokens = tokenize_command_line(command_line);
    return std::any_of(tokens.begin(), tokens.end(), [](const std::string& token) {
        return command_line_token_equal(token, "--ai-play") ||
            command_line_token_equal(token, "/ai-play");
    });
}

TyranoScriptedBotDecision DecideTyranoScriptedBotAction(
    TyranoScriptedBotState& state, const AiObservation& observation,
    const TyranoScriptedBotConfig& config) {
    TyranoScriptedBotDecision decision{};
    if (observation.game_ended) {
        decision.code = TyranoScriptedBotDecisionCode::game_ended;
        return decision;
    }
    if (observation.local_faction != kTyranoFactionId) {
        decision.code = TyranoScriptedBotDecisionCode::wrong_faction;
        return decision;
    }
    if (state.last_decision_frame != 0xffffffffu &&
        observation.simulation_frame - state.last_decision_frame <
            std::max<u32>(config.decision_interval_frames, 1u)) {
        decision.code = TyranoScriptedBotDecisionCode::not_due;
        return decision;
    }
    state.last_decision_frame = observation.simulation_frame;

    std::vector<u32> combat_units;
    std::vector<const AiObservedUnit*> workers;
    const AiObservedUnit* worker = nullptr;
    const AiObservedUnit* nest = nullptr;
    const AiObservedUnit* nearest_enemy = nullptr;
    u32 worker_count = 0;
    i64 nearest_enemy_distance = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive) {
            if (unit.type_id == kTyranoWorkerType) {
                ++worker_count;
                workers.push_back(&unit);
                if (worker == nullptr || unit.id < worker->id) {
                    worker = &unit;
                }
            }
            if (unit.type_id == kTyranoNestType &&
                (nest == nullptr || unit.id < nest->id)) {
                nest = &unit;
            }
            if (unit_can_attack(unit) && !unit_is_harvesting(unit) &&
                !unit_is_constructing(unit)) {
                combat_units.push_back(unit.id);
            }
        }
        if (is_hostile_visible_unit(observation, unit)) {
            const i64 distance = squared_distance(observation.start_x,
                observation.start_y, unit.x, unit.y);
            if (nearest_enemy == nullptr || distance < nearest_enemy_distance ||
                (distance == nearest_enemy_distance && unit.id < nearest_enemy->id)) {
                nearest_enemy = &unit;
                nearest_enemy_distance = distance;
            }
        }
    }

    if (worker == nullptr && nest == nullptr && combat_units.empty()) {
        decision.code = TyranoScriptedBotDecisionCode::no_controlled_units;
        return decision;
    }

    if (nest == nullptr && worker != nullptr) {
        const UnitMovementPoint point = next_build_point(state, observation);
        AiSemanticAction action{};
        action.kind = AiSemanticActionKind::build;
        action.unit_ids = {worker->id};
        action.production_id = kTyranoNestType;
        action.target_x = point.x;
        action.target_y = point.y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::build_starting_nest,
            std::move(action));
    }

    if (nest != nullptr && !state.rally_configured) {
        const UnitMovementPoint point = exploration_point(state, observation);
        AiSemanticAction action{};
        action.kind = AiSemanticActionKind::set_rally;
        action.unit_ids = {nest->id};
        action.target_x = point.x;
        action.target_y = point.y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::set_starting_rally,
            std::move(action));
    }

    const u32 active_harvester_count = static_cast<u32>(std::count_if(
        workers.begin(), workers.end(), [](const AiObservedUnit* candidate) {
            return candidate != nullptr && unit_is_harvesting(*candidate);
        }));
    const u32 harvester_target = std::min(
        worker_count, config.desired_harvester_count);
    if (active_harvester_count < harvester_target) {
        const HarvestAssignment assignment =
            nearest_visible_resource_assignment(observation, workers);
        if (assignment.worker != nullptr) {
            AiSemanticAction action{};
            action.kind = AiSemanticActionKind::harvest;
            action.unit_ids = {assignment.worker->id};
            action.target_x = assignment.point.x;
            action.target_y = assignment.point.y;
            ++state.decisions_emitted;
            return ready(TyranoScriptedBotIntent::harvest_visible_resource,
                std::move(action));
        }
    }

    const u32 pending_workers = nest != nullptr ?
        pending_unit_count(*nest, kTyranoWorkerType) : 0u;
    if (nest != nullptr &&
        worker_count + pending_workers < config.desired_worker_count &&
        observation.primary_resources >= config.worker_primary_resource_cost) {
        AiSemanticAction action{};
        action.kind = AiSemanticActionKind::produce_unit;
        action.unit_ids = {nest->id};
        action.production_id = kTyranoWorkerType;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::produce_worker,
            std::move(action));
    }

    for (const TyranoMacroGoal& goal : kReplayDerivedBuildOrder) {
        u32 target_count = goal.target_count;
        if (goal.kind == TyranoMacroGoalKind::produce &&
            goal.target_id == kTyranoMasosType) {
            target_count = std::min(target_count, config.desired_masos_count);
        }
        if (goal.kind == TyranoMacroGoalKind::produce &&
            goal.target_id == kTyranoDilophosType) {
            target_count = std::min(target_count,
                config.desired_dilophos_count);
        }

        bool complete = false;
        if (goal.kind == TyranoMacroGoalKind::build) {
            complete = live_type_count(observation, goal.target_id, true) >=
                target_count;
        }
        else if (goal.kind == TyranoMacroGoalKind::produce) {
            complete = live_type_count(observation, goal.target_id, false) +
                pending_type_count(observation, goal.source_type,
                    goal.target_id) >= target_count;
        }
        else {
            complete = research_goal_requested(state, goal.target_id);
        }
        if (complete) {
            continue;
        }

        if (!intent_retry_ready(state, goal.intent,
                observation.simulation_frame) ||
            observation.primary_resources < goal.primary_cost) {
            break;
        }

        AiSemanticAction action{};
        if (goal.kind == TyranoMacroGoalKind::build) {
            if (live_type_count(observation, goal.target_id, false) >=
                    target_count) {
                break;
            }
            const AiObservedUnit* builder = select_builder(workers);
            if (builder == nullptr) {
                break;
            }
            const UnitMovementPoint point = next_build_point(state, observation);
            action.kind = AiSemanticActionKind::build;
            action.unit_ids = {builder->id};
            action.production_id = goal.target_id;
            action.target_x = point.x;
            action.target_y = point.y;
        }
        else if (goal.kind == TyranoMacroGoalKind::produce) {
            const AiObservedUnit* producer =
                select_completed_type(observation, goal.source_type);
            if (producer == nullptr) {
                break;
            }
            action.kind = AiSemanticActionKind::produce_unit;
            action.unit_ids = {producer->id};
            action.production_id = goal.target_id;
        }
        else {
            const AiObservedUnit* producer =
                select_completed_type(observation, goal.source_type);
            if (producer == nullptr) {
                break;
            }
            action.kind = AiSemanticActionKind::research;
            action.unit_ids = {producer->id};
            action.production_id = goal.target_id;
        }
        ++state.decisions_emitted;
        return ready(goal.intent, std::move(action));
    }

    if (!combat_units.empty() && nearest_enemy != nullptr) {
        AiSemanticAction action{};
        action.kind = AiSemanticActionKind::attack_unit;
        action.unit_ids = std::move(combat_units);
        action.target_unit_id = nearest_enemy->id;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::attack_visible_enemy,
            std::move(action));
    }

    if (!combat_units.empty()) {
        const UnitMovementPoint point = exploration_point(state, observation);
        AiSemanticAction action{};
        action.kind = AiSemanticActionKind::attack_move;
        action.unit_ids = std::move(combat_units);
        action.target_x = point.x;
        action.target_y = point.y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::explore, std::move(action));
    }

    decision.code = TyranoScriptedBotDecisionCode::no_action;
    return decision;
}

void CommitTyranoScriptedBotDecision(TyranoScriptedBotState& state,
    const TyranoScriptedBotDecision& decision, bool published,
    AiActionPlanCode plan_code) {
    if (!decision) {
        return;
    }
    const std::size_t intent_index =
        static_cast<std::size_t>(decision.intent);
    if (!published) {
        if (intent_index < state.intent_retry_after_frame.size()) {
            const u32 backoff =
                plan_code == AiActionPlanCode::production_unavailable ?
                kMacroRetryBackoffFrames : kMacroRetryBackoffFrames / 2u;
            state.intent_retry_after_frame[intent_index] =
                state.last_decision_frame + backoff;
        }
        return;
    }
    if (intent_index < state.intent_retry_after_frame.size()) {
        state.intent_retry_after_frame[intent_index] = 0;
    }
    ++state.actions_committed;
    if (decision.intent == TyranoScriptedBotIntent::set_starting_rally) {
        state.rally_configured = true;
    }
    if (decision.intent == TyranoScriptedBotIntent::explore) {
        ++state.exploration_index;
    }
    if (decision.intent ==
            TyranoScriptedBotIntent::research_harvest_upgrade) {
        state.harvest_upgrade_requested = true;
    }
    if (decision.intent ==
            TyranoScriptedBotIntent::research_ground_attack) {
        state.ground_attack_upgrade_requested = true;
    }
    if (decision.intent ==
            TyranoScriptedBotIntent::research_movement_upgrade) {
        state.movement_upgrade_requested = true;
    }
}

} // namespace ranker
