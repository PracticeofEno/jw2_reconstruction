#include "ranker_ai_scripted_bot.h"
#include "ranker_ai_expansion.h"
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
    // cargo_amount (raw +0x4c) is a state-dependent union that
    // ProcessWorkerDepositCargo never resets, so it stays non-zero forever
    // once a worker has mined once.  command_flags bit 2 is set only by
    // ProcessWorkerHarvestTile and cleared only on deposit, so it is the
    // reliable "carrying harvested cargo" signal.
    return (state >= kUnitStateWorkerApproachHarvest &&
            state <= kUnitStateWorkerHarvestFailed) ||
        (unit.command_flags & 4u) != 0;
}

// Literally standing around doing nothing: the runtime idle states (0x00 = no
// command at all, 0x01 = idle/auto-acquire) with an empty command queue.
// ProcessUnitIdleAcquireCommand pops a deferred command before anything else,
// so a unit sitting in 0x01 with a non-empty queue resumes work next tick and
// is not a reassignment candidate.  Defining idle positively (rather than as
// "not harvesting and not constructing") keeps workers that are travelling,
// fighting back, fleeing or mid-order out of the idle set.
bool unit_is_idle(const AiObservedUnit& unit) {
    if (!unit.controlled || !unit.alive || unit.under_construction ||
        unit.deferred_command_count != 0) {
        return false;
    }
    const u32 state = unit.command_state & kUnitCommandStateMask;
    return state == 0u || state == kUnitStateRuntimeIdleAcquire;
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
    const i32 center_x = state.placement_center_tile_x >= 0 ?
        state.placement_center_tile_x : observation.start_x >> 5;
    const i32 center_y = state.placement_center_tile_y >= 0 ?
        state.placement_center_tile_y : observation.start_y >> 5;
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
            !unit_is_idle(*worker)) {
            continue;
        }
        for (u32 tile_index = 0; tile_index < observation.tiles.size();
             ++tile_index) {
            const AiObservedMapTile& tile = observation.tiles[tile_index];
            // Resource amounts are exposed for explored terrain (the current
            // visibility layer is local-viewer only), so a non-zero amount on a
            // passable tile is a valid harvest candidate whether or not it is
            // lit right now.
            if (!tile.passable || tile.resource_amount == 0) {
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

// A completed, fully idle producer of the given type (no queued production,
// no deferred commands).  Research must only target such a building: enqueueing
// an order onto a busy researcher re-debits its cost and resets its progress
// (the restart-drain failure observed live).
const AiObservedUnit* select_idle_completed_type(
    const AiObservation& observation, u32 type_id) {
    const AiObservedUnit* selected = nullptr;
    for (const AiObservedUnit& unit : observation.units) {
        if (!unit.controlled || !unit.alive || unit.under_construction ||
            unit.type_id != type_id ||
            unit.queued_production_type_id != 0 ||
            unit.deferred_command_count != 0) {
            continue;
        }
        if (selected == nullptr || unit.id < selected->id) {
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

    // Army fallback: once the rigid replay build order has run (or stalled on an
    // unaffordable/backed-off goal) keep pumping Masos from any idle egg nest
    // while population and money allow.  Placed AFTER the build order so the
    // replay-derived opening (starting with a population Nest) is unchanged, but
    // a resource surplus is always converted into military instead of hoarded.
    {
        constexpr u32 kMasosPrimaryCost = 100u;
        const bool has_population_room =
            observation.population_limit == 0 ||
            observation.population_used < observation.population_limit;
        if (has_population_room &&
            observation.primary_resources >= kMasosPrimaryCost) {
            for (const AiObservedUnit& unit : observation.units) {
                if (!unit.controlled || !unit.alive ||
                    unit.type_id != kTyranoEggNestType ||
                    unit.under_construction) {
                    continue;
                }
                if (pending_unit_count(unit, kTyranoMasosType) != 0) {
                    continue;
                }
                AiSemanticAction action{};
                action.kind = AiSemanticActionKind::produce_unit;
                action.unit_ids = {unit.id};
                action.production_id = kTyranoMasosType;
                ++state.decisions_emitted;
                return ready(TyranoScriptedBotIntent::produce_masos,
                    std::move(action));
            }
        }
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
        // Hold the current attack-move objective for a dwell window.  Without
        // this the whole army was re-issued a fresh objective every decision
        // cycle and cycled through the map's start corners so quickly that it
        // never stayed anywhere long enough to destroy an enemy base.
        constexpr u32 kArmyObjectiveDwellFrames = 160u;
        if (state.last_army_objective_frame != 0xffffffffu &&
            observation.simulation_frame - state.last_army_objective_frame <
                kArmyObjectiveDwellFrames) {
            decision.code = TyranoScriptedBotDecisionCode::no_action;
            return decision;
        }
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

// v10: which raid SLOT a raid-family action drives (four fighting bodies).
// Non-raid actions map to the main army by convention (callers test
// membership separately).
AiMicroGroup raid_slot_of_action(AiRlHighLevelAction action) {
    switch (action) {
    case AiRlHighLevelAction::detach_raid_b:
    case AiRlHighLevelAction::merge_raid_b:
    case AiRlHighLevelAction::raid_b_attack_units:
    case AiRlHighLevelAction::raid_b_attack_base:
    case AiRlHighLevelAction::raid_b_defend_base:
    case AiRlHighLevelAction::raid_b_retreat:
    case AiRlHighLevelAction::raid_b_hunt_neutral:
    case AiRlHighLevelAction::raid_b_search:
        return AiMicroGroup::raid_b;
    case AiRlHighLevelAction::detach_raid_c:
    case AiRlHighLevelAction::merge_raid_c:
    case AiRlHighLevelAction::raid_c_attack_units:
    case AiRlHighLevelAction::raid_c_attack_base:
    case AiRlHighLevelAction::raid_c_defend_base:
    case AiRlHighLevelAction::raid_c_retreat:
    case AiRlHighLevelAction::raid_c_hunt_neutral:
    case AiRlHighLevelAction::raid_c_search:
        return AiMicroGroup::raid_c;
    default:
        return AiMicroGroup::raid;
    }
}

TyranoScriptedBotDecision DecideTyranoScriptedBotForHighLevelAction(
    TyranoScriptedBotState& state, const AiObservation& observation,
    AiRlHighLevelAction action, const TyranoScriptedBotConfig& config,
    i32 target_cell) {
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

    // Scan the observation once, mirroring DecideTyranoScriptedBotAction.
    std::vector<u32> combat_units;
    std::vector<const AiObservedUnit*> workers;
    const AiObservedUnit* nest = nullptr;
    const AiObservedUnit* nearest_enemy = nullptr;
    const AiObservedUnit* nearest_enemy_building = nullptr;
    const AiObservedUnit* nearest_neutral = nullptr;
    const AiObservedUnit* any_own = nullptr;
    i64 nearest_enemy_distance = 0;
    i64 nearest_enemy_building_distance = 0;
    i64 nearest_neutral_distance = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive) {
            if (any_own == nullptr) {
                any_own = &unit;
            }
            if (unit.type_id == kTyranoWorkerType) {
                workers.push_back(&unit);
            }
            if (unit.type_id == kTyranoNestType &&
                (nest == nullptr || unit.id < nest->id)) {
                nest = &unit;
            }
            // Workers are excluded from the army: drafting them into attack /
            // hunt orders sent the whole worker line to war in the opening
            // (they can technically attack, but they are harvesters).
            if (unit.type_id != kTyranoWorkerType &&
                unit.type_id < kTyranoMobileTypeLimit &&
                unit_can_attack(unit) && !unit_is_harvesting(unit) &&
                !unit_is_constructing(unit)) {
                combat_units.push_back(unit.id);
            }
        }
        if (is_hostile_visible_unit(observation, unit)) {
            const i64 distance = squared_distance(observation.start_x,
                observation.start_y, unit.x, unit.y);
            if (nearest_enemy == nullptr || distance < nearest_enemy_distance ||
                (distance == nearest_enemy_distance &&
                    unit.id < nearest_enemy->id)) {
                nearest_enemy = &unit;
                nearest_enemy_distance = distance;
            }
            // Buildings (>= 0x60 in every tribe) are the elimination
            // objective, so the siege action tracks them separately.
            if (unit.type_id >= kTyranoMobileTypeLimit &&
                (nearest_enemy_building == nullptr ||
                    distance < nearest_enemy_building_distance ||
                    (distance == nearest_enemy_building_distance &&
                        unit.id < nearest_enemy_building->id))) {
                nearest_enemy_building = &unit;
                nearest_enemy_building_distance = distance;
            }
        }
        // Neutral monster (owner 8, mobile): huntable for resources.
        if (unit.visible && unit.alive && !unit.controlled &&
            unit.owner_id == kNeutralMonsterOwnerId &&
            unit.type_id < kTyranoMobileTypeLimit) {
            const i64 distance = squared_distance(observation.start_x,
                observation.start_y, unit.x, unit.y);
            if (nearest_neutral == nullptr ||
                distance < nearest_neutral_distance ||
                (distance == nearest_neutral_distance &&
                    unit.id < nearest_neutral->id)) {
                nearest_neutral = &unit;
                nearest_neutral_distance = distance;
            }
        }
    }

    const auto produce_from = [&](u32 producer_type, u32 unit_type,
                                  TyranoScriptedBotIntent intent) {
        // Prefer a completed producer with an EMPTY queue so repeated produce
        // actions spread across all buildings of the type (parallel production)
        // instead of stacking the first one's queue.
        const AiObservedUnit* producer = nullptr;
        for (const AiObservedUnit& unit : observation.units) {
            if (unit.controlled && unit.alive && !unit.under_construction &&
                unit.type_id == producer_type &&
                unit.queued_production_type_id == 0 &&
                unit.deferred_command_count == 0 &&
                (producer == nullptr || unit.id < producer->id)) {
                producer = &unit;
            }
        }
        if (producer == nullptr) {
            producer = select_completed_type(observation, producer_type);
        }
        if (producer == nullptr) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::produce_unit;
        act.unit_ids = {producer->id};
        act.production_id = unit_type;
        ++state.decisions_emitted;
        return ready(intent, std::move(act));
    };
    // Nearest available worker to a point (the builder for every structure).
    // Nearest available worker to a point (the builder for every structure).
    // Harvesting/carrying workers are eligible: the engine accepts a build
    // order in every harvest state (2026-09-04 user rule).
    const auto nearest_worker_to = [&](i32 x, i32 y) -> const AiObservedUnit* {
        const AiObservedUnit* best = nullptr;
        i64 best_distance = 0;
        for (const AiObservedUnit* worker : workers) {
            if (worker == nullptr || !worker->alive ||
                unit_is_constructing(*worker)) {
                continue;
            }
            const i64 distance = squared_distance(worker->x, worker->y, x, y);
            if (best == nullptr || distance < best_distance ||
                (distance == best_distance && worker->id < best->id)) {
                best = worker;
                best_distance = distance;
            }
        }
        return best;
    };
    const auto build_structure = [&](u32 structure_type,
                                     TyranoScriptedBotIntent intent) {
        // Shared placement rule (v7): the nearest statically valid, explored,
        // unblocked site for this footprint in the base area - the same
        // search the mask ran, so a legal action is a placeable one.  The
        // placement retries spiral around that site.
        const AiExpansionConfig placement_config{};
        const AiBuildSite site = FindAiBuildSite(observation, structure_type,
            std::max(observation.start_x, 0), std::max(observation.start_y, 0),
            placement_config.base_site_radius_tiles, placement_config);
        if (!site.found) {
            return decision;
        }
        const AiObservedUnit* builder = nearest_worker_to(site.x, site.y);
        if (builder == nullptr) {
            return decision;
        }
        state.placement_center_tile_x = site.x >> 5;
        state.placement_center_tile_y = site.y >> 5;
        state.placement_probe_index = 0;  // first probe = the site itself
        const UnitMovementPoint point = next_build_point(state, observation);
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::build;
        act.unit_ids = {builder->id};
        act.production_id = structure_type;
        act.target_x = point.x;
        act.target_y = point.y;
        ++state.decisions_emitted;
        return ready(intent, std::move(act));
    };
    const auto move_army_to = [&](i32 x, i32 y, TyranoScriptedBotIntent intent) {
        if (combat_units.empty()) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::attack_move;
        act.unit_ids = std::move(combat_units);
        act.target_x = x;
        act.target_y = y;
        ++state.decisions_emitted;
        return ready(intent, std::move(act));
    };
    // ---- group-objective helpers (micro executor) -------------------------
    // Army / economy / scout actions no longer emit a semantic action: they
    // set the group's objective and the per-frame micro executor issues the
    // unit orders (docs/AI_PLAY_MICRO_EXECUTOR_DESIGN.md).
    const AiMicroExecutorConfig micro_config{};
    const auto objective_updated = [&](AiMicroGroup group,
                                       AiMicroObjective objective) {
        objective.set_frame = observation.simulation_frame;
        AiMicroSetObjective(state.micro, group, objective);
        ++state.decisions_emitted;
        TyranoScriptedBotDecision updated{};
        updated.code = TyranoScriptedBotDecisionCode::objective_updated;
        return updated;
    };
    const auto group_anchor = [&](AiMicroGroup group) {
        UnitMovementPoint anchor{std::max(observation.start_x, 0),
            std::max(observation.start_y, 0)};
        AiMicroGroupCentroid(state.micro, observation, group, anchor,
            micro_config);
        return anchor;
    };
    const auto army_anchor = [&]() { return group_anchor(AiMicroGroup::army); };
    // v8 spatial target: the policy's 8x8 grid cell as a map point (cell
    // centre), or (-1,-1) when the action carried no cell.
    const auto cell_point = [&]() -> UnitMovementPoint {
        const i32 width_px = static_cast<i32>(observation.map_width_tiles) * 32;
        const i32 height_px = static_cast<i32>(observation.map_height_tiles) * 32;
        if (target_cell < 0 ||
            target_cell >= static_cast<i32>(kAiRlTargetCellCount) ||
            width_px <= 0 || height_px <= 0) {
            return {-1, -1};
        }
        const i32 grid = static_cast<i32>(kAiRlTargetGridWidth);
        const i32 cx = target_cell % grid;
        const i32 cy = target_cell / grid;
        return {cx * width_px / grid + width_px / (2 * grid),
            cy * height_px / grid + height_px / (2 * grid)};
    };
    // Attack objectives carry a TACTIC, not a target: the executor derives
    // the group's current target from the tactic every frame, so the policy's
    // choice keeps applying after the first kill.  An optional preferred
    // point (the spatial-target cell) makes the group hunt there first.
    const auto attack_tactic_objective = [&](AiMicroAttackTactic tactic,
        AiMicroGroup group = AiMicroGroup::army,
        UnitMovementPoint preferred = {-1, -1}) {
        AiMicroObjective objective;
        objective.kind = AiMicroObjectiveKind::attack;
        objective.tactic = tactic;
        objective.preferred_x = preferred.x;
        objective.preferred_y = preferred.y;
        return objective_updated(group, objective);
    };
    const auto defend_objective = [&](UnitMovementPoint post, i32 radius,
        AiMicroGroup group = AiMicroGroup::army) {
        AiMicroObjective objective;
        objective.kind = AiMicroObjectiveKind::defend;
        objective.target_x = post.x;
        objective.target_y = post.y;
        objective.radius = radius;
        return objective_updated(group, objective);
    };
    const auto nearest_hostile_to = [&](i32 x, i32 y) -> const AiObservedUnit* {
        const AiObservedUnit* best = nullptr;
        i64 best_distance = 0;
        for (const AiObservedUnit& unit : observation.units) {
            if (!is_hostile_visible_unit(observation, unit)) {
                continue;
            }
            const i64 distance = squared_distance(x, y, unit.x, unit.y);
            if (best == nullptr || distance < best_distance ||
                (distance == best_distance && unit.id < best->id)) {
                best = &unit;
                best_distance = distance;
            }
        }
        return best;
    };
    (void)move_army_to;
    (void)nearest_hostile_to;
    // Mergeable = alive controlled mobile unit of the type with the audited
    // 0x0b capability bit that is not already inside a linked-release cycle
    // (states 0x5f..0x61).
    const auto is_mergeable = [](const AiObservedUnit& unit, u32 type) {
        const u32 command_state = unit.command_state & 0x00ffffffu;
        return unit.controlled && unit.alive && !unit.under_construction &&
            unit.type_id == type &&
            (unit.type_flags & kTyranoMergeCommandBit) != 0 &&
            (command_state < 0x5fu || command_state > 0x61u);
    };
    const auto nearest_mergeable_of_type = [&](u32 type,
        const AiObservedUnit* anchor) -> const AiObservedUnit* {
        const AiObservedUnit* best = nullptr;
        i64 best_distance = 0;
        for (const AiObservedUnit& unit : observation.units) {
            if (!is_mergeable(unit, type) ||
                (anchor != nullptr && unit.id == anchor->id)) {
                continue;
            }
            const i64 distance = anchor != nullptr ?
                squared_distance(anchor->x, anchor->y, unit.x, unit.y) : 0;
            if (best == nullptr || distance < best_distance ||
                (distance == best_distance && unit.id < best->id)) {
                best = &unit;
                best_distance = distance;
            }
        }
        return best;
    };
    const auto merge_pair_of_type = [&](u32 type) {
        // Nearest same-type pair, so newly produced units merge without long
        // walks.  O(n^2) over the type's roster is fine at RTS scales.
        const AiObservedUnit* best_a = nullptr;
        const AiObservedUnit* best_b = nullptr;
        i64 best_distance = 0;
        for (const AiObservedUnit& lhs : observation.units) {
            if (!is_mergeable(lhs, type)) {
                continue;
            }
            for (const AiObservedUnit& rhs : observation.units) {
                if (&rhs == &lhs || !is_mergeable(rhs, type) ||
                    rhs.id <= lhs.id) {
                    continue;
                }
                const i64 distance =
                    squared_distance(lhs.x, lhs.y, rhs.x, rhs.y);
                if (best_a == nullptr || distance < best_distance) {
                    best_a = &lhs;
                    best_b = &rhs;
                    best_distance = distance;
                }
            }
        }
        if (best_a == nullptr || best_b == nullptr) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::merge_units;
        act.unit_ids = {best_a->id, best_b->id};
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::merge_pair, std::move(act));
    };

    switch (action) {
    case AiRlHighLevelAction::no_op:
        decision.code = TyranoScriptedBotDecisionCode::no_action;
        return decision;
    case AiRlHighLevelAction::produce_worker:
        return produce_from(kTyranoNestType, kTyranoWorkerType,
            TyranoScriptedBotIntent::produce_worker);
    case AiRlHighLevelAction::produce_masos:
        return produce_from(kTyranoEggNestType, kTyranoMasosType,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::produce_dilophos:
        return produce_from(kTyranoEggNestType, kTyranoDilophosType,
            TyranoScriptedBotIntent::produce_dilophos);
    case AiRlHighLevelAction::build_population_nest:
        return build_structure(kTyranoPopulationNestType,
            TyranoScriptedBotIntent::build_population_nest);
    case AiRlHighLevelAction::build_egg_nest:
        return build_structure(kTyranoEggNestType,
            TyranoScriptedBotIntent::build_egg_nest);
    case AiRlHighLevelAction::build_land_nest:
        return build_structure(kTyranoLandNestType,
            TyranoScriptedBotIntent::build_land_nest);
    case AiRlHighLevelAction::expand_base_nest: {
        // Expansion: build the base nest at the next expansion site (the
        // nearest undeveloped berry cluster's best site), which the mask only
        // opens once that site is lit and no other nest build is walking.
        // Nearest worker to the site; placement retries spiral around the
        // site, not the main base.
        const AiExpansionPlan plan = ComputeAiExpansionPlan(observation);
        if (!plan.has_target || !plan.target_explored ||
            plan.target_blocked || plan.nest_walkers != 0) {
            return decision;
        }
        const AiObservedUnit* builder = nullptr;
        i64 builder_distance = 0;
        for (const AiObservedUnit* worker : workers) {
            if (worker == nullptr || !worker->alive ||
                unit_is_constructing(*worker)) {
                continue;
            }
            const i64 distance = squared_distance(worker->x, worker->y,
                plan.target_x, plan.target_y);
            if (builder == nullptr || distance < builder_distance ||
                (distance == builder_distance && worker->id < builder->id)) {
                builder = worker;
                builder_distance = distance;
            }
        }
        if (builder == nullptr) {
            return decision;
        }
        state.placement_center_tile_x = plan.target_x >> 5;
        state.placement_center_tile_y = plan.target_y >> 5;
        state.placement_probe_index = 0;  // first probe = the site itself
        const UnitMovementPoint point = next_build_point(state, observation);
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::build;
        act.unit_ids = {builder->id};
        act.production_id = kTyranoNestType;
        act.target_x = point.x;
        act.target_y = point.y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::build_second_tyrano_nest,
            std::move(act));
    }
    case AiRlHighLevelAction::scout_berry: {
        // Light the next expansion site so expand_base_nest becomes legal.
        // One unit in the berry_scout group: reuse a live one, else the
        // cheapest fighter, else a worker - never the picket scout.  The
        // executor walks it there (evading, never fighting) and releases it
        // once the site tile is explored.
        const AiExpansionPlan plan = ComputeAiExpansionPlan(observation);
        if (!plan.has_target || plan.target_explored) {
            return decision;
        }
        const AiObservedUnit* scout = nullptr;
        {
            const std::vector<const AiObservedUnit*> members =
                AiMicroGroupMembers(state.micro, observation,
                    AiMicroGroup::berry_scout, micro_config);
            if (!members.empty()) {
                scout = members.front();
            }
        }
        if (scout == nullptr && !config.berry_scout_prefer_worker) {
            for (const AiObservedUnit& unit : observation.units) {
                if (!unit.controlled || !unit.alive ||
                    unit.under_construction ||
                    unit.type_id >= kTyranoMobileTypeLimit ||
                    unit.type_id == kTyranoWorkerType ||
                    unit_is_constructing(unit) ||
                    AiMicroRoleOf(unit, micro_config) ==
                        AiMicroRole::transport ||
                    AiMicroGroupOf(state.micro, unit, micro_config) ==
                        AiMicroGroup::scout) {
                    continue;
                }
                const bool cheaper = unit.type_id == kTyranoMasosType;
                if (scout == nullptr ||
                    (cheaper && scout->type_id != kTyranoMasosType) ||
                    (unit.type_id == scout->type_id && unit.id < scout->id)) {
                    scout = &unit;
                }
            }
        }
        if (scout == nullptr) {
            for (const AiObservedUnit* worker : workers) {
                if (worker != nullptr && worker->alive &&
                    !unit_is_constructing(*worker) &&
                    AiMicroGroupOf(state.micro, *worker, micro_config) !=
                        AiMicroGroup::scout) {
                    scout = worker;
                    break;
                }
            }
        }
        if (scout == nullptr) {
            return decision;
        }
        AiMicroAssignGroup(state.micro, scout->id, AiMicroGroup::berry_scout);
        AiMicroObjective objective;
        objective.kind = AiMicroObjectiveKind::scout;
        objective.target_x = plan.target_x;
        objective.target_y = plan.target_y;
        return objective_updated(AiMicroGroup::berry_scout, objective);
    }
    case AiRlHighLevelAction::research_harvest:
    case AiRlHighLevelAction::research_ground_attack:
    case AiRlHighLevelAction::research_ground_defense:
    case AiRlHighLevelAction::research_movement:
    case AiRlHighLevelAction::research_air_attack:
    case AiRlHighLevelAction::research_air_defense:
    case AiRlHighLevelAction::research_mutant_merge:
    case AiRlHighLevelAction::research_morph:
    case AiRlHighLevelAction::research_haste:
    case AiRlHighLevelAction::research_exp_down:
    case AiRlHighLevelAction::research_melee_reinforce:
    case AiRlHighLevelAction::research_triceps_speed:
    case AiRlHighLevelAction::research_air_reinforce: {
        // The policy names the order; the executor only routes it to an IDLE
        // completed building of the audited researcher type (re-enqueueing
        // onto a busy researcher re-debits the cost and resets progress —
        // the restart-drain bug) and refuses orders already at their cap.
        const AiRlResearchAction* entry = FindAiRlResearchAction(action);
        if (entry == nullptr) {
            return decision;
        }
        if (entry->order < observation.research_order_levels.size() &&
            observation.research_order_levels[entry->order] >=
                entry->max_levels) {
            return decision;
        }
        const AiObservedUnit* producer = select_idle_completed_type(
            observation, entry->researcher_type);
        if (producer == nullptr) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::research;
        act.unit_ids = {producer->id};
        act.production_id = entry->order;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::research_harvest_upgrade,
            std::move(act));
    }
    case AiRlHighLevelAction::scout_map: {
        // Scout group (0..1 units): reuse the live scout, else split one unit
        // off the army (cheapest fighter first), else a worker.  Destination
        // priority:
        //   1. the nearest competitor start whose tile is still UNEXPLORED —
        //      that is where an enemy base can actually be; and
        //   2. once every start is explored, the generic center/corner sweep
        //      (advancing the cursor each pick so coverage progresses).
        // The scout objective then holds the unit at that post; the executor
        // steps it away from sighted hostiles and back again.
        const AiObservedUnit* scout = nullptr;
        {
            const std::vector<const AiObservedUnit*> members =
                AiMicroGroupMembers(state.micro, observation,
                    AiMicroGroup::scout, micro_config);
            if (!members.empty()) {
                scout = members.front();
            }
        }
        if (scout == nullptr) {
            for (const AiObservedUnit& unit : observation.units) {
                if (!unit.controlled || !unit.alive ||
                    unit.under_construction ||
                    unit.type_id >= kTyranoMobileTypeLimit ||
                    unit.type_id == kTyranoWorkerType ||
                    unit_is_constructing(unit) ||
                    AiMicroRoleOf(unit, micro_config) ==
                        AiMicroRole::transport) {
                    continue;
                }
                const bool cheaper = unit.type_id == kTyranoMasosType;
                if (scout == nullptr ||
                    (cheaper && scout->type_id != kTyranoMasosType) ||
                    (unit.type_id == scout->type_id && unit.id < scout->id)) {
                    scout = &unit;
                }
            }
        }
        if (scout == nullptr && !workers.empty()) {
            scout = workers.front();
        }
        if (scout == nullptr) {
            return decision;
        }
        // Picket, not base finder: needs a KNOWN enemy building (visible or
        // remembered) to stand between home and it.  The executor derives
        // the post every frame; the policy only decides to post a picket.
        bool known = nearest_enemy_building != nullptr;
        if (!known && observation.enemy_building_memory.size() ==
                observation.tiles.size()) {
            for (std::size_t t = 0; t < observation.tiles.size(); ++t) {
                if (observation.enemy_building_memory[t] != 0 &&
                    !observation.tiles[t].visible) {
                    known = true;
                    break;
                }
            }
        }
        if (!known) {
            return decision;
        }
        AiMicroAssignGroup(state.micro, scout->id, AiMicroGroup::scout);
        AiMicroObjective objective;
        objective.kind = AiMicroObjectiveKind::scout;
        return objective_updated(AiMicroGroup::scout, objective);
    }
    case AiRlHighLevelAction::attack_nearest_enemy:
    case AiRlHighLevelAction::attack_enemy_base:
    case AiRlHighLevelAction::raid_attack_units:
    case AiRlHighLevelAction::raid_attack_base:
    case AiRlHighLevelAction::raid_b_attack_units:
    case AiRlHighLevelAction::raid_b_attack_base:
    case AiRlHighLevelAction::raid_c_attack_units:
    case AiRlHighLevelAction::raid_c_attack_base: {
        // The policy chooses the class priority - enemy ARMY first or enemy
        // BUILDINGS first - and (v8) the GROUP: main army or the detached
        // raid.  Either way the executor attack-moves the group on the known
        // enemy location (a visible hostile, or a building remembered in the
        // fog memory) and engages what it meets in the tactic's order.  With
        // no enemy location known, search first.  The buildings_first actions
        // also take the spatial-target cell as the strike zone.
        const bool raid = action != AiRlHighLevelAction::attack_nearest_enemy &&
            action != AiRlHighLevelAction::attack_enemy_base;
        const AiMicroGroup attack_group = raid ? raid_slot_of_action(action) :
            AiMicroGroup::army;
        if (raid && AiMicroGroupMembers(state.micro, observation,
                attack_group, micro_config).empty()) {
            return decision;
        }
        bool known = nearest_enemy != nullptr ||
            nearest_enemy_building != nullptr;
        if (!known && observation.enemy_building_memory.size() ==
                observation.tiles.size()) {
            for (std::size_t t = 0; t < observation.tiles.size(); ++t) {
                if (observation.enemy_building_memory[t] != 0 &&
                    !observation.tiles[t].visible) {
                    known = true;
                    break;
                }
            }
        }
        if (!known) {
            return decision;
        }
        const bool buildings_first =
            action == AiRlHighLevelAction::attack_enemy_base ||
            action == AiRlHighLevelAction::raid_attack_base ||
            action == AiRlHighLevelAction::raid_b_attack_base ||
            action == AiRlHighLevelAction::raid_c_attack_base;
        return attack_tactic_objective(
            buildings_first ? AiMicroAttackTactic::buildings_first :
                AiMicroAttackTactic::units_first,
            attack_group,
            buildings_first ? cell_point() : UnitMovementPoint{-1, -1});
    }
    case AiRlHighLevelAction::search_enemy_base:
    case AiRlHighLevelAction::raid_search:
    case AiRlHighLevelAction::raid_b_search:
    case AiRlHighLevelAction::raid_c_search: {
        // Group sweep of the UNEXPLORED START CANDIDATES (where an enemy base
        // can be); the executor picks and advances the target.  Refused
        // (and masked off) once every candidate has been checked.
        const bool raid = action != AiRlHighLevelAction::search_enemy_base;
        const AiMicroGroup search_group = raid ? raid_slot_of_action(action) :
            AiMicroGroup::army;
        if (raid ? AiMicroGroupMembers(state.micro, observation,
                search_group, micro_config).empty() :
                combat_units.empty()) {
            return decision;
        }
        bool unexplored_start = false;
        for (u32 slot = 0; slot < 8u; ++slot) {
            if ((observation.start_candidate_mask & (1u << slot)) == 0) {
                continue;
            }
            const i32 sx = observation.start_candidate_x[slot];
            const i32 sy = observation.start_candidate_y[slot];
            if (sx == observation.start_x && sy == observation.start_y) {
                continue;
            }
            const u32 tx = static_cast<u32>(std::max(sx, 0)) >> 5;
            const u32 ty = static_cast<u32>(std::max(sy, 0)) >> 5;
            const std::size_t index =
                static_cast<std::size_t>(ty) * observation.map_width_tiles + tx;
            if (tx < observation.map_width_tiles &&
                ty < observation.map_height_tiles &&
                index < observation.tiles.size() &&
                !observation.tiles[index].explored) {
                unexplored_start = true;
                break;
            }
        }
        if (!unexplored_start) {
            return decision;
        }
        AiMicroObjective objective;
        objective.kind = AiMicroObjectiveKind::search;
        return objective_updated(search_group, objective);
    }
    case AiRlHighLevelAction::explore_frontier:
    case AiRlHighLevelAction::roam_scout: {
        // One unit each.  Priority: an AIR unit (reaches everything, sees
        // over terrain), then the fastest ground unit (movement_step_limit /
        // movement_period), then lowest id.  Never a worker unless nothing
        // else exists, never a transport, never a unit already serving as the
        // picket / berry scout / the other single-unit role.
        const AiMicroGroup group =
            action == AiRlHighLevelAction::explore_frontier ?
            AiMicroGroup::explorer : AiMicroGroup::roamer;
        const AiObservedUnit* chosen = nullptr;
        {
            const std::vector<const AiObservedUnit*> members =
                AiMicroGroupMembers(state.micro, observation, group,
                    micro_config);
            if (!members.empty()) {
                chosen = members.front();
            }
        }
        if (chosen == nullptr) {
            const auto rank = [&](const AiObservedUnit& unit) -> i64 {
                const bool air = unit.render_class == micro_config.flying_render_class;
                const i64 speed = static_cast<i64>(unit.movement_step_limit) * 1000 /
                    std::max<u32>(unit.movement_period, 1u);
                const bool worker = unit.type_id == kTyranoWorkerType;
                return (air ? 1'000'000'000LL : 0) + (worker ? 0 : 1'000'000LL) +
                    speed;
            };
            for (const AiObservedUnit& unit : observation.units) {
                if (!unit.controlled || !unit.alive || unit.under_construction ||
                    unit.type_id >= kTyranoMobileTypeLimit ||
                    unit_is_constructing(unit) ||
                    (config.berry_scout_prefer_worker &&
                        unit.type_id != kTyranoWorkerType) ||
                    AiMicroRoleOf(unit, micro_config) == AiMicroRole::transport) {
                    continue;
                }
                const AiMicroGroup current =
                    AiMicroGroupOf(state.micro, unit, micro_config);
                if (current == AiMicroGroup::scout ||
                    current == AiMicroGroup::berry_scout ||
                    (current == AiMicroGroup::explorer && group != current) ||
                    (current == AiMicroGroup::roamer && group != current)) {
                    continue;
                }
                if (chosen == nullptr || rank(unit) > rank(*chosen) ||
                    (rank(unit) == rank(*chosen) && unit.id < chosen->id)) {
                    chosen = &unit;
                }
            }
        }
        if (chosen == nullptr) {
            return decision;
        }
        AiMicroAssignGroup(state.micro, chosen->id, group);
        AiMicroObjective objective;
        objective.kind = group == AiMicroGroup::explorer ?
            AiMicroObjectiveKind::explore : AiMicroObjectiveKind::roam;
        return objective_updated(group, objective);
    }
    case AiRlHighLevelAction::retreat:
    case AiRlHighLevelAction::raid_retreat:
    case AiRlHighLevelAction::raid_b_retreat:
    case AiRlHighLevelAction::raid_c_retreat: {
        // Whole group to the nest nearest its centroid, no engagement; the
        // executor flips the objective to defend(that nest) on arrival.
        const bool raid = action != AiRlHighLevelAction::retreat;
        const AiMicroGroup group =
            raid ? raid_slot_of_action(action) : AiMicroGroup::army;
        if (raid && AiMicroGroupMembers(state.micro, observation,
                group, micro_config).empty()) {
            return decision;
        }
        const UnitMovementPoint anchor = group_anchor(group);
        const UnitMovementPoint nest = AiMicroNearestBase(observation,
            anchor.x, anchor.y, micro_config);
        AiMicroObjective objective;
        objective.kind = AiMicroObjectiveKind::retreat;
        objective.target_x = nest.x;
        objective.target_y = nest.y;
        return objective_updated(group, objective);
    }
    case AiRlHighLevelAction::defend_base:
    case AiRlHighLevelAction::raid_defend_base:
    case AiRlHighLevelAction::raid_b_defend_base:
    case AiRlHighLevelAction::raid_c_defend_base: {
        // Post = the spatial-target cell centre when the policy chose one
        // (v8), else the nest nearest the group; bubble = one screen around
        // every own nest (hostiles seen outside it are ignored, chasers
        // leash back).
        const bool raid = action != AiRlHighLevelAction::defend_base;
        const AiMicroGroup group =
            raid ? raid_slot_of_action(action) : AiMicroGroup::army;
        if (raid && AiMicroGroupMembers(state.micro, observation,
                group, micro_config).empty()) {
            return decision;
        }
        // v10.5 (user directive): defend anchors on the visible enemy force
        // near the base, not on the base itself.  Post priority: threat
        // centroid > policy cell > nest nearest the group.
        UnitMovementPoint post{-1, -1};
        {
            i64 sum_x = 0;
            i64 sum_y = 0;
            i64 count = 0;
            for (const AiObservedUnit& hostile : observation.units) {
                if (!is_hostile_visible_unit(observation, hostile) ||
                    hostile.type_id >= kTyranoMobileTypeLimit ||
                    hostile.attack_range == 0) {
                    continue;
                }
                bool near_base = false;
                for (const AiObservedUnit& own : observation.units) {
                    if (own.controlled && own.alive &&
                        own.type_id >= kTyranoMobileTypeLimit &&
                        squared_distance(own.x, own.y, hostile.x, hostile.y) <=
                            1200 * 1200) {
                        near_base = true;
                        break;
                    }
                }
                if (near_base) {
                    sum_x += hostile.x;
                    sum_y += hostile.y;
                    ++count;
                }
            }
            if (count > 0) {
                post = {static_cast<i32>(sum_x / count),
                    static_cast<i32>(sum_y / count)};
            }
        }
        if (post.x < 0) {
            post = cell_point();
        }
        if (post.x < 0) {
            const UnitMovementPoint anchor = group_anchor(group);
            post = AiMicroNearestBase(observation, anchor.x, anchor.y,
                micro_config);
        }
        return defend_objective(post, micro_config.defend_radius, group);
    }
    case AiRlHighLevelAction::hunt_neutral_monster:
    case AiRlHighLevelAction::raid_hunt_neutral:
    case AiRlHighLevelAction::raid_b_hunt_neutral:
    case AiRlHighLevelAction::raid_c_hunt_neutral: {
        if (nearest_neutral == nullptr) {
            return decision;
        }
        const bool raid = action != AiRlHighLevelAction::hunt_neutral_monster;
        const AiMicroGroup group =
            raid ? raid_slot_of_action(action) : AiMicroGroup::army;
        if (raid && AiMicroGroupMembers(state.micro, observation,
                group, micro_config).empty()) {
            return decision;
        }
        return attack_tactic_objective(AiMicroAttackTactic::neutral_only,
            group);
    }
    // Producer map from the session reference tables: egg 0x84 produces the
    // fighter roster, base 0x80 produces 0x2c, 0x87 produces 0x29/0x2a.
    case AiRlHighLevelAction::produce_unit_x22:
        return produce_from(kTyranoEggNestType, kTyranoUnit22Type,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::produce_unit_x25:
        return produce_from(kTyranoEggNestType, 0x25u,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::produce_unit_x27:
        return produce_from(kTyranoEggNestType, 0x27u,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::produce_unit_x28:
        return produce_from(kTyranoEggNestType, 0x28u,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::produce_unit_x2e:
        return produce_from(kTyranoEggNestType, 0x2eu,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::produce_unit_x2c:
        return produce_from(kTyranoNestType, 0x2cu,
            TyranoScriptedBotIntent::produce_worker);
    case AiRlHighLevelAction::produce_unit_x29:
        return produce_from(kTyranoNest87Type, 0x29u,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::produce_unit_x2a:
        return produce_from(kTyranoNest87Type, 0x2au,
            TyranoScriptedBotIntent::produce_masos);
    case AiRlHighLevelAction::build_nest_x86:
        return build_structure(kTyranoNest86Type,
            TyranoScriptedBotIntent::build_land_nest);
    case AiRlHighLevelAction::build_nest_x87:
        return build_structure(kTyranoNest87Type,
            TyranoScriptedBotIntent::build_land_nest);
    case AiRlHighLevelAction::build_nest_x83:
        return build_structure(0x83u,
            TyranoScriptedBotIntent::build_land_nest);
    case AiRlHighLevelAction::build_nest_x88:
        return build_structure(0x88u,
            TyranoScriptedBotIntent::build_land_nest);
    case AiRlHighLevelAction::build_nest_x89:
        return build_structure(0x89u,
            TyranoScriptedBotIntent::build_land_nest);
    case AiRlHighLevelAction::build_nest_x8a:
        return build_structure(0x8au,
            TyranoScriptedBotIntent::build_land_nest);
    case AiRlHighLevelAction::merge_twin_velocis:
        return merge_pair_of_type(kTyranoUnit22Type);
    case AiRlHighLevelAction::merge_twin_rhampos:
        return merge_pair_of_type(kTyranoRhamposType);
    case AiRlHighLevelAction::merge_twin_pteras:
        return merge_pair_of_type(kTyranoPterasType);
    case AiRlHighLevelAction::merge_mutant: {
        // 뮤턴트 triad (딜로포스+프테라스+트리세스) is gated on research 0x18.
        if (observation.research_order_levels[
                kTyranoMutantMergeResearchOrder] == 0) {
            return decision;
        }
        const AiObservedUnit* dilophos =
            nearest_mergeable_of_type(kTyranoDilophosType, nullptr);
        if (dilophos == nullptr) {
            return decision;
        }
        const AiObservedUnit* pteras =
            nearest_mergeable_of_type(kTyranoPterasType, dilophos);
        const AiObservedUnit* triceps =
            nearest_mergeable_of_type(kTyranoTricepsType, dilophos);
        if (pteras == nullptr || triceps == nullptr) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::merge_units;
        act.unit_ids = {dilophos->id, pteras->id, triceps->id};
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::merge_mutant, std::move(act));
    }
    case AiRlHighLevelAction::morph_enter_army: {
        // Wild-dino morph is human-reachable only after research 0x2a (the UI
        // gates the selector on the owner variant count).
        if (observation.research_order_levels[kTyranoMorphResearchOrder] == 0) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::morph_enter;
        for (const AiObservedUnit& unit : observation.units) {
            if (act.unit_ids.size() >= kAiMaximumUnitsPerAction) {
                break;
            }
            if (unit.controlled && unit.alive && !unit.under_construction &&
                unit.type_id < kTyranoMobileTypeLimit &&
                (unit.type_flags & kTyranoMorphCommandBit) != 0 &&
                (unit.type_flags & kTyranoMorphedTypeFlag) == 0) {
                act.unit_ids.push_back(unit.id);
            }
        }
        if (act.unit_ids.empty()) {
            return decision;
        }
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::morph_shift, std::move(act));
    }
    case AiRlHighLevelAction::morph_exit_army: {
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::morph_exit;
        for (const AiObservedUnit& unit : observation.units) {
            if (act.unit_ids.size() >= kAiMaximumUnitsPerAction) {
                break;
            }
            if (unit.controlled && unit.alive &&
                unit.type_id < kTyranoMobileTypeLimit &&
                (unit.type_flags & kTyranoMorphedTypeFlag) != 0) {
                act.unit_ids.push_back(unit.id);
            }
        }
        if (act.unit_ids.empty()) {
            return decision;
        }
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::morph_shift, std::move(act));
    }
    case AiRlHighLevelAction::stance_on_army:
    case AiRlHighLevelAction::stance_off_army: {
        const bool on = action == AiRlHighLevelAction::stance_on_army;
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::set_stance;
        act.stance_id = kTyranoStanceId;
        act.stance_on = on;
        for (const AiObservedUnit& unit : observation.units) {
            if (act.unit_ids.size() >= kAiMaximumUnitsPerAction) {
                break;
            }
            if (!unit.controlled || !unit.alive ||
                unit.type_id >= kTyranoMobileTypeLimit ||
                (unit.type_flags & kTyranoStanceCommandBit) == 0) {
                continue;
            }
            const bool active =
                (unit.command_flags & kTyranoStanceActiveFlag) != 0;
            if (on ? (unit.action_mode != 0 && !active) : active) {
                act.unit_ids.push_back(unit.id);
            }
        }
        if (act.unit_ids.empty()) {
            return decision;
        }
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::stance_toggle, std::move(act));
    }
    case AiRlHighLevelAction::hold_army:
        // Hold = defend with a tiny bubble at the army's own spot.
        return defend_objective(army_anchor(), micro_config.hold_radius);
    case AiRlHighLevelAction::patrol_defense:
        // Patrol = defend with a mid-size bubble at the army's own spot (the
        // engine patrol command is not used: leashed defense covers it).
        return defend_objective(army_anchor(), micro_config.patrol_radius);
    case AiRlHighLevelAction::drop_attack: {
        // Initiate only; PlanTyranoDropAttackAutopilot advances the composite
        // on subsequent decision cycles regardless of later policy picks.
        if (state.drop_stage != 0) {
            return decision;
        }
        const AiObservedUnit* carrier = nullptr;
        for (const AiObservedUnit& unit : observation.units) {
            if (unit.controlled && unit.alive && !unit.under_construction &&
                unit.type_id == kTyranoCarrierType &&
                (carrier == nullptr || unit.id < carrier->id)) {
                carrier = &unit;
            }
        }
        if (carrier == nullptr) {
            return decision;
        }
        // Boardable fighter set per the audited transport_flags rows (air and
        // composite units cannot board).
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::board_transport;
        act.target_unit_id = carrier->id;
        struct Candidate {
            u32 id;
            i64 distance;
        };
        std::vector<Candidate> candidates;
        for (const AiObservedUnit& unit : observation.units) {
            if (!unit.controlled || !unit.alive || unit.under_construction ||
                unit.type_id == kTyranoWorkerType ||
                unit.type_id >= kTyranoMobileTypeLimit ||
                !unit_can_attack(unit)) {
                continue;
            }
            switch (unit.type_id) {
            case 0x21u: case 0x22u: case 0x23u: case 0x24u:
            case 0x25u: case 0x28u: case 0x2au: case 0x2eu:
                break;
            default:
                continue;
            }
            candidates.push_back({unit.id, squared_distance(carrier->x,
                carrier->y, unit.x, unit.y)});
        }
        if (candidates.empty()) {
            return decision;
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
                return lhs.distance != rhs.distance ?
                    lhs.distance < rhs.distance : lhs.id < rhs.id;
            });
        constexpr std::size_t kDropSquadSize = 4;
        for (std::size_t i = 0; i < candidates.size() && i < kDropSquadSize;
             ++i) {
            act.unit_ids.push_back(candidates[i].id);
        }
        // March target: the nearest non-own start candidate (same objective
        // the siege path uses).
        i32 target_x = -1;
        i32 target_y = -1;
        i64 target_distance = 0;
        for (u32 slot = 0; slot < 8u; ++slot) {
            if ((observation.start_candidate_mask & (1u << slot)) == 0) {
                continue;
            }
            const i64 distance = squared_distance(observation.start_x,
                observation.start_y, observation.start_candidate_x[slot],
                observation.start_candidate_y[slot]);
            if (distance == 0) {
                continue;
            }
            if (target_x < 0 || distance < target_distance) {
                target_x = observation.start_candidate_x[slot];
                target_y = observation.start_candidate_y[slot];
                target_distance = distance;
            }
        }
        if (target_x < 0) {
            return decision;
        }
        state.drop_stage = 1;
        state.drop_carrier_id = carrier->id;
        state.drop_stage_frame = observation.simulation_frame;
        state.drop_target_x = target_x;
        state.drop_target_y = target_y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::drop_attack, std::move(act));
    }
    case AiRlHighLevelAction::detach_raid:
    case AiRlHighLevelAction::detach_raid_b:
    case AiRlHighLevelAction::detach_raid_c: {
        // Split a strike group off the main army (docs/1순위.md): a
        // deterministic mobility-first share - the policy cannot pick units
        // with a flat discrete head, so WHO goes is an executor rule and the
        // policy only decides THAT a raid exists and what it does.  Fastest
        // first (ranged breaks ties - the raid's job is hit-and-run), then
        // lowest id; 30% of the group, min 3, max one planner chunk.
        // v10: three independent slots (four fighting bodies).
        const AiMicroGroup slot = raid_slot_of_action(action);
        if (!AiMicroGroupMembers(state.micro, observation, slot,
                micro_config).empty()) {
            return decision;  // this raid slot already exists
        }
        std::vector<const AiObservedUnit*> members = AiMicroGroupMembers(
            state.micro, observation, AiMicroGroup::army, micro_config);
        std::vector<const AiObservedUnit*> fighters;
        for (const AiObservedUnit* unit : members) {
            if (unit->type_id == kTyranoWorkerType ||
                unit->type_id >= kTyranoMobileTypeLimit ||
                !unit_can_attack(*unit) || unit_is_constructing(*unit) ||
                AiMicroRoleOf(*unit, micro_config) == AiMicroRole::transport) {
                continue;
            }
            fighters.push_back(unit);
        }
        if (fighters.size() <
            static_cast<std::size_t>(micro_config.raid_detach_minimum) * 2) {
            return decision;  // too small to split (mask floor mirrors this)
        }
        const auto speed_of = [](const AiObservedUnit& unit) -> i64 {
            return static_cast<i64>(unit.movement_step_limit) * 1000 /
                std::max<u32>(unit.movement_period, 1u);
        };
        std::sort(fighters.begin(), fighters.end(),
            [&](const AiObservedUnit* lhs, const AiObservedUnit* rhs) {
                const i64 lhs_speed = speed_of(*lhs);
                const i64 rhs_speed = speed_of(*rhs);
                if (lhs_speed != rhs_speed) {
                    return lhs_speed > rhs_speed;
                }
                const bool lhs_ranged =
                    AiMicroRoleOf(*lhs, micro_config) == AiMicroRole::ranged;
                const bool rhs_ranged =
                    AiMicroRoleOf(*rhs, micro_config) == AiMicroRole::ranged;
                if (lhs_ranged != rhs_ranged) {
                    return lhs_ranged;
                }
                return lhs->id < rhs->id;
            });
        std::size_t count = fighters.size() *
            micro_config.raid_detach_percent / 100u;
        count = std::max<std::size_t>(count, micro_config.raid_detach_minimum);
        count = std::min<std::size_t>(count, kAiMaximumUnitsPerAction);
        count = std::min<std::size_t>(count, fighters.size() / 2);
        i64 sum_x = 0;
        i64 sum_y = 0;
        for (std::size_t index = 0; index < count; ++index) {
            AiMicroAssignGroup(state.micro, fighters[index]->id, slot);
            sum_x += fighters[index]->x;
            sum_y += fighters[index]->y;
        }
        // The fresh raid HOLDS where it stands until the policy directs it
        // (a tiny defend bubble at its own centroid).
        AiMicroObjective objective;
        objective.kind = AiMicroObjectiveKind::defend;
        objective.target_x = static_cast<i32>(sum_x / static_cast<i64>(count));
        objective.target_y = static_cast<i32>(sum_y / static_cast<i64>(count));
        objective.radius = micro_config.hold_radius;
        return objective_updated(slot, objective);
    }
    case AiRlHighLevelAction::merge_raid:
    case AiRlHighLevelAction::merge_raid_b:
    case AiRlHighLevelAction::merge_raid_c: {
        // Fold the raid slot back into the main army: pure group
        // re-assignment, the members pick up the army's current objective
        // next frame.
        const std::vector<const AiObservedUnit*> members = AiMicroGroupMembers(
            state.micro, observation, raid_slot_of_action(action),
            micro_config);
        if (members.empty()) {
            return decision;
        }
        for (const AiObservedUnit* unit : members) {
            AiMicroAssignGroup(state.micro, unit->id, AiMicroGroup::army);
        }
        ++state.decisions_emitted;
        TyranoScriptedBotDecision updated{};
        updated.code = TyranoScriptedBotDecisionCode::objective_updated;
        return updated;
    }
    }

    decision.code = TyranoScriptedBotDecisionCode::no_action;
    return decision;
}

UnitMovementPoint TyranoScriptedBotNextBuildPoint(TyranoScriptedBotState& state,
    const AiObservation& observation) {
    return next_build_point(state, observation);
}

std::vector<AiSemanticAction> PlanTyranoIdleWorkerHarvest(
    const AiObservation& observation, std::size_t max_actions,
    const std::vector<u32>& exclude_unit_ids) {
    std::vector<AiSemanticAction> actions;
    std::vector<const AiObservedUnit*> idle;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit_is_idle(unit) && unit_can_harvest(unit) &&
            std::find(exclude_unit_ids.begin(), exclude_unit_ids.end(),
                unit.id) == exclude_unit_ids.end()) {
            idle.push_back(&unit);
        }
    }
    while (actions.size() < max_actions && !idle.empty()) {
        const HarvestAssignment assignment =
            nearest_visible_resource_assignment(observation, idle);
        if (assignment.worker == nullptr) {
            break;
        }
        AiSemanticAction action{};
        action.kind = AiSemanticActionKind::harvest;
        action.unit_ids = {assignment.worker->id};
        action.target_x = assignment.point.x;
        action.target_y = assignment.point.y;
        actions.push_back(std::move(action));
        idle.erase(std::find(idle.begin(), idle.end(), assignment.worker));
    }
    return actions;
}

std::vector<AiSemanticAction> ChunkAiSemanticActionUnits(
    const AiSemanticAction& action) {
    std::vector<AiSemanticAction> chunks;
    if (action.unit_ids.size() <= kAiMaximumUnitsPerAction) {
        chunks.push_back(action);
        return chunks;
    }
    // merge_units arity is semantic (2 or 3), never chunked.
    if (action.kind == AiSemanticActionKind::merge_units) {
        chunks.push_back(action);
        return chunks;
    }
    for (std::size_t begin = 0; begin < action.unit_ids.size();
         begin += kAiMaximumUnitsPerAction) {
        AiSemanticAction chunk = action;
        chunk.unit_ids.assign(action.unit_ids.begin() + begin,
            action.unit_ids.begin() + std::min(action.unit_ids.size(),
                begin + kAiMaximumUnitsPerAction));
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

std::vector<AiSemanticAction> PlanTyranoDropAttackAutopilot(
    TyranoScriptedBotState& state, const AiObservation& observation) {
    // Deterministic composite: every transition keys off the observation and
    // decision frames only.  Stage 0 = idle (nothing to do here; initiation
    // is the drop_attack high-level action).
    constexpr u32 kBoardingMinFrames = 256u;
    constexpr u32 kBoardingTimeoutFrames = 1024u;
    constexpr u32 kTravelTimeoutFrames = 4096u;
    constexpr i64 kUnloadRadiusSquared = 192 * 192;
    constexpr i64 kSquadRallyRadiusSquared = 256 * 256;

    std::vector<AiSemanticAction> actions;
    if (state.drop_stage == 0) {
        return actions;
    }
    const AiObservedUnit* carrier = nullptr;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive &&
            unit.id == state.drop_carrier_id) {
            carrier = &unit;
            break;
        }
    }
    if (carrier == nullptr) {
        state.drop_stage = 0;
        return actions;
    }
    const u32 stage_age =
        observation.simulation_frame - state.drop_stage_frame;

    if (state.drop_stage == 1) {
        std::size_t attached = 0;
        for (const AiObservedUnit& unit : observation.units) {
            if (unit.controlled && unit.alive &&
                (unit.command_state & 0x00ffffffu) ==
                    kTyranoTransportAttachedState) {
                ++attached;
            }
        }
        const bool loaded_enough =
            attached != 0 && stage_age >= kBoardingMinFrames;
        if (!loaded_enough && stage_age < kBoardingTimeoutFrames) {
            return actions;
        }
        if (attached == 0) {
            // Nobody boarded before the timeout: abort the run.
            state.drop_stage = 0;
            return actions;
        }
        AiSemanticAction travel{};
        travel.kind = AiSemanticActionKind::move;
        travel.unit_ids = {carrier->id};
        travel.target_x = state.drop_target_x;
        travel.target_y = state.drop_target_y;
        actions.push_back(std::move(travel));
        state.drop_stage = 2;
        state.drop_stage_frame = observation.simulation_frame;
        return actions;
    }

    if (state.drop_stage == 2) {
        const i64 distance = squared_distance(carrier->x, carrier->y,
            state.drop_target_x, state.drop_target_y);
        if (distance > kUnloadRadiusSquared &&
            stage_age < kTravelTimeoutFrames) {
            return actions;
        }
        AiSemanticAction unload{};
        unload.kind = AiSemanticActionKind::unload_transport;
        unload.unit_ids = {carrier->id};
        unload.target_x = carrier->x;
        unload.target_y = carrier->y;
        actions.push_back(std::move(unload));
        state.drop_stage = 3;
        state.drop_stage_frame = observation.simulation_frame;
        return actions;
    }

    // Stage 3: the squad is on the ground next to the carrier — send it at
    // the drop objective and release the composite.
    AiSemanticAction assault{};
    assault.kind = AiSemanticActionKind::attack_move;
    assault.target_x = state.drop_target_x;
    assault.target_y = state.drop_target_y;
    for (const AiObservedUnit& unit : observation.units) {
        if (assault.unit_ids.size() >= kAiMaximumUnitsPerAction) {
            break;
        }
        if (unit.controlled && unit.alive &&
            unit.type_id != kTyranoWorkerType &&
            unit.type_id != kTyranoCarrierType &&
            unit.type_id < kTyranoMobileTypeLimit && unit_can_attack(unit) &&
            (unit.command_state & 0x00ffffffu) !=
                kTyranoTransportAttachedState &&
            squared_distance(carrier->x, carrier->y, unit.x, unit.y) <=
                kSquadRallyRadiusSquared) {
            assault.unit_ids.push_back(unit.id);
        }
    }
    state.drop_stage = 0;
    if (!assault.unit_ids.empty()) {
        actions.push_back(std::move(assault));
    }
    return actions;
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
        state.last_army_objective_frame = state.last_decision_frame;
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
