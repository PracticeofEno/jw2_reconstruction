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

namespace {
// Mop-up helpers (defined ahead of PlanTyranoOffenseAutopilot below; shared
// with the attack_enemy_base executor).
void update_enemy_building_memory(TyranoScriptedBotState& state,
    const AiObservation& observation);
const TyranoScriptedBotState::RememberedEnemyBuilding*
nearest_remembered_building(const TyranoScriptedBotState& state, i32 from_x,
    i32 from_y);
UnitMovementPoint nearest_unexplored_point(const AiObservation& observation,
    i32 from_x, i32 from_y);
} // namespace

TyranoScriptedBotDecision DecideTyranoScriptedBotForHighLevelAction(
    TyranoScriptedBotState& state, const AiObservation& observation,
    AiRlHighLevelAction action, const TyranoScriptedBotConfig& config) {
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
    const auto build_structure = [&](u32 structure_type,
                                     TyranoScriptedBotIntent intent) {
        const AiObservedUnit* builder = select_builder(workers);
        if (builder == nullptr) {
            return decision;
        }
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
    case AiRlHighLevelAction::expand_base_nest:
        return build_structure(kTyranoNestType,
            TyranoScriptedBotIntent::build_second_tyrano_nest);
    case AiRlHighLevelAction::research_next: {
        // Walk the full audited Tyrano research tree (ai_techtree_audit.txt):
        // request the first order below its max level whose researcher building
        // exists.  The order->building mapping is the session reference table's
        // completion_references; a wrong building made the live validator
        // reject every request after the first completed order.
        struct ResearchStep {
            u32 order;
            u32 researcher_type;
            u8 max_levels;
        };
        static constexpr ResearchStep kSteps[] = {
            {kTyranoHarvestUpgradeOrder, kTyranoNestType, 1},        // 0x14
            {kTyranoGroundAttackUpgradeOrder, kTyranoLandNestType, 5},  // 0x19
            {0x1au, kTyranoLandNestType, 5},   // ground defense up
            {kTyranoMovementUpgradeOrder, kTyranoLandNestType, 1},   // 0x16
            {0x1cu, kTyranoNest86Type, 5},     // air attack up
            {0x1du, kTyranoNest86Type, 5},     // air defense up
            {0x18u, kTyranoNest86Type, 1},     // mutant merge
            {0x2au, kTyranoNestType, 1},       // dino morph
            {0x38u, kTyranoNestType, 1},       // haste
            {0x2bu, kTyranoNestType, 1},       // level-up exp down
            {0x1bu, 0x89u, 1},                 // melee reinforce (land nisdos)
            {0x2du, 0x89u, 1},                 // triceps speed
            {0x1eu, 0x8au, 1},                 // air reinforce (sky nisdos)
        };
        for (const ResearchStep& step : kSteps) {
            if (step.order < observation.research_order_levels.size() &&
                observation.research_order_levels[step.order] >=
                    step.max_levels) {
                continue;
            }
            // Idle producers only: re-enqueueing onto a busy researcher
            // re-debits and resets the in-progress order (restart-drain).
            // Skipping a busy building also spreads research across nests.
            const AiObservedUnit* producer = select_idle_completed_type(
                observation, step.researcher_type);
            if (producer == nullptr) {
                continue;
            }
            AiSemanticAction act{};
            act.kind = AiSemanticActionKind::research;
            act.unit_ids = {producer->id};
            act.production_id = step.order;
            ++state.decisions_emitted;
            return ready(TyranoScriptedBotIntent::research_harvest_upgrade,
                std::move(act));
        }
        return decision;
    }
    case AiRlHighLevelAction::harvest_saturate: {
        const HarvestAssignment assignment =
            nearest_visible_resource_assignment(observation, workers);
        if (assignment.worker == nullptr) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::harvest;
        act.unit_ids = {assignment.worker->id};
        act.target_x = assignment.point.x;
        act.target_y = assignment.point.y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::harvest_visible_resource,
            std::move(act));
    }
    case AiRlHighLevelAction::scout_map: {
        // Send one idle-ish unit (prefer a worker, else any) on a plain move
        // so scouting never commits the army.  Destination priority:
        //   1. the nearest competitor start whose tile is still UNEXPLORED —
        //      that is where an enemy base can actually be; and
        //   2. once every start is explored, the generic center/corner sweep
        //      (advancing the cursor each pick so coverage progresses).
        const AiObservedUnit* scout = workers.empty() ? any_own : workers.front();
        if (scout == nullptr) {
            return decision;
        }
        i32 scout_x = -1;
        i32 scout_y = -1;
        i64 scout_distance = 0;
        for (u32 slot = 0; slot < 8u; ++slot) {
            if ((observation.start_candidate_mask & (1u << slot)) == 0) {
                continue;
            }
            const i32 sx = observation.start_candidate_x[slot];
            const i32 sy = observation.start_candidate_y[slot];
            const i64 distance = squared_distance(observation.start_x,
                observation.start_y, sx, sy);
            if (distance == 0) {
                continue;  // our own start slot
            }
            const u32 tile_x = static_cast<u32>(std::max(sx, 0)) >> 5;
            const u32 tile_y = static_cast<u32>(std::max(sy, 0)) >> 5;
            const std::size_t tile_index =
                static_cast<std::size_t>(tile_y) * observation.map_width_tiles +
                tile_x;
            if (tile_x >= observation.map_width_tiles ||
                tile_y >= observation.map_height_tiles ||
                tile_index >= observation.tiles.size() ||
                observation.tiles[tile_index].explored) {
                continue;
            }
            if (scout_x < 0 || distance < scout_distance) {
                scout_x = sx;
                scout_y = sy;
                scout_distance = distance;
            }
        }
        if (scout_x < 0) {
            ++state.exploration_index;
            const UnitMovementPoint point =
                exploration_point(state, observation);
            scout_x = point.x;
            scout_y = point.y;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::move;
        act.unit_ids = {scout->id};
        act.target_x = scout_x;
        act.target_y = scout_y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::explore, std::move(act));
    }
    case AiRlHighLevelAction::attack_nearest_enemy:
        if (!combat_units.empty() && nearest_enemy != nullptr) {
            AiSemanticAction act{};
            act.kind = AiSemanticActionKind::attack_unit;
            act.unit_ids = std::move(combat_units);
            act.target_unit_id = nearest_enemy->id;
            ++state.decisions_emitted;
            return ready(TyranoScriptedBotIntent::attack_visible_enemy,
                std::move(act));
        }
        return decision;
    case AiRlHighLevelAction::attack_enemy_base: {
        // Siege: the elimination objective is the enemy's BUILDINGS, so send
        // the whole army at the nearest visible one (previously this was a
        // plain exploration move that never razed anything — armies of 200+
        // units stalemated for 100k frames).  Fall back to the nearest enemy
        // unit; with nothing visible, MARCH ON THE NEAREST ENEMY START —
        // corner-sweeping never found the base, so sieges never engaged.
        const AiObservedUnit* target = nearest_enemy_building != nullptr ?
            nearest_enemy_building : nearest_enemy;
        if (!combat_units.empty() && target != nullptr) {
            AiSemanticAction act{};
            act.kind = AiSemanticActionKind::attack_unit;
            act.unit_ids = std::move(combat_units);
            act.target_unit_id = target->id;
            ++state.decisions_emitted;
            return ready(TyranoScriptedBotIntent::attack_visible_enemy,
                std::move(act));
        }
        // Mop-up chain (mirrors the offense autopilot): remembered building
        // -> unexplored enemy start -> unexplored-region sweep -> rotating
        // exploration cycle.  Camping an explored-empty start stalemated
        // decided games at the frame cap.
        update_enemy_building_memory(state, observation);
        const i32 home_x = std::max(observation.start_x, 0);
        const i32 home_y = std::max(observation.start_y, 0);
        const TyranoScriptedBotState::RememberedEnemyBuilding* remembered =
            nearest_remembered_building(state, home_x, home_y);
        if (remembered != nullptr) {
            return move_army_to(remembered->x, remembered->y,
                TyranoScriptedBotIntent::attack_visible_enemy);
        }
        i64 best_distance = 0;
        i32 march_x = -1;
        i32 march_y = -1;
        for (u32 slot = 0; slot < 8u; ++slot) {
            if ((observation.start_candidate_mask & (1u << slot)) == 0) {
                continue;
            }
            const i32 sx = observation.start_candidate_x[slot];
            const i32 sy = observation.start_candidate_y[slot];
            const i64 distance =
                squared_distance(observation.start_x, observation.start_y,
                    sx, sy);
            if (distance == 0) {
                continue;  // our own start slot
            }
            const u32 tile_x = static_cast<u32>(std::max(sx, 0)) >> 5;
            const u32 tile_y = static_cast<u32>(std::max(sy, 0)) >> 5;
            const std::size_t tile_index = static_cast<std::size_t>(tile_y) *
                observation.map_width_tiles + tile_x;
            if (tile_index < observation.tiles.size() &&
                observation.tiles[tile_index].explored) {
                continue;  // already checked and empty
            }
            if (march_x < 0 || distance < best_distance) {
                best_distance = distance;
                march_x = sx;
                march_y = sy;
            }
        }
        if (march_x >= 0) {
            return move_army_to(march_x, march_y,
                TyranoScriptedBotIntent::attack_visible_enemy);
        }
        const UnitMovementPoint sweep =
            nearest_unexplored_point(observation, home_x, home_y);
        if (sweep.x >= 0) {
            return move_army_to(sweep.x, sweep.y,
                TyranoScriptedBotIntent::explore);
        }
        ++state.exploration_index;
        const UnitMovementPoint point = exploration_point(state, observation);
        return move_army_to(point.x, point.y,
            TyranoScriptedBotIntent::explore);
    }
    case AiRlHighLevelAction::retreat: {
        // Retreat != defend: pull back ONLY the units currently in a combat
        // cycle (attack travel/target, guard combat/pursuit, patrol combat)
        // with a plain move so they actually disengage; unengaged units stay
        // where they are.  Sharing defend_base's case made the two actions
        // identical (41 actions were effectively 40).
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::move;
        for (const AiObservedUnit& unit : observation.units) {
            if (act.unit_ids.size() >= kAiMaximumUnitsPerAction) {
                break;
            }
            if (!unit.controlled || !unit.alive ||
                unit.type_id == kTyranoWorkerType ||
                unit.type_id >= kTyranoMobileTypeLimit) {
                continue;
            }
            const u32 command_state = unit.command_state & 0x00ffffffu;
            const bool engaged =
                command_state == kUnitStateAttackTravel ||
                command_state == kUnitStateAttackTarget ||
                (command_state >= kUnitStateRuntimeTargetValidationStart &&
                    command_state <= kUnitStateGuardPursueTarget) ||
                command_state == kUnitStatePatrolReturnCombat ||
                command_state == kUnitStatePatrolOutboundCombat;
            if (engaged) {
                act.unit_ids.push_back(unit.id);
            }
        }
        if (act.unit_ids.empty()) {
            return decision;
        }
        act.target_x = std::max(observation.start_x, 0);
        act.target_y = std::max(observation.start_y, 0);
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::explore, std::move(act));
    }
    case AiRlHighLevelAction::defend_base: {
        // With the army already home, HOLD instead of re-pathing: a plain
        // move let defenders chase whatever crossed their acquisition radius
        // and scatter off the base again.
        if (!combat_units.empty()) {
            constexpr i64 kHomeRadiusSquared = 96 * 96;
            i64 centroid_x = 0;
            i64 centroid_y = 0;
            std::size_t counted = 0;
            for (const AiObservedUnit& unit : observation.units) {
                if (unit.controlled && unit.alive &&
                    std::find(combat_units.begin(), combat_units.end(),
                        unit.id) != combat_units.end()) {
                    centroid_x += unit.x;
                    centroid_y += unit.y;
                    ++counted;
                }
            }
            if (counted != 0) {
                centroid_x /= static_cast<i64>(counted);
                centroid_y /= static_cast<i64>(counted);
                const i64 home_distance = squared_distance(
                    static_cast<i32>(centroid_x), static_cast<i32>(centroid_y),
                    std::max(observation.start_x, 0),
                    std::max(observation.start_y, 0));
                if (home_distance <= kHomeRadiusSquared) {
                    AiSemanticAction act{};
                    act.kind = AiSemanticActionKind::hold_position;
                    act.unit_ids = std::move(combat_units);
                    ++state.decisions_emitted;
                    return ready(TyranoScriptedBotIntent::hold_army,
                        std::move(act));
                }
            }
        }
        return move_army_to(std::max(observation.start_x, 0),
            std::max(observation.start_y, 0),
            TyranoScriptedBotIntent::explore);
    }
    case AiRlHighLevelAction::hunt_neutral_monster:
        if (!combat_units.empty() && nearest_neutral != nullptr) {
            AiSemanticAction act{};
            act.kind = AiSemanticActionKind::attack_unit;
            act.unit_ids = std::move(combat_units);
            act.target_unit_id = nearest_neutral->id;
            ++state.decisions_emitted;
            return ready(TyranoScriptedBotIntent::attack_visible_enemy,
                std::move(act));
        }
        return decision;
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
    case AiRlHighLevelAction::hold_army: {
        if (combat_units.empty()) {
            return decision;
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::hold_position;
        act.unit_ids = std::move(combat_units);
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::hold_army, std::move(act));
    }
    case AiRlHighLevelAction::patrol_defense: {
        if (combat_units.empty()) {
            return decision;
        }
        // Patrol leg: current position <-> the nearest explored resource
        // cluster (the harvest line is what raids hit first).
        i32 patrol_x = -1;
        i32 patrol_y = -1;
        i64 patrol_distance = 0;
        for (u32 tile_y = 0; tile_y < observation.map_height_tiles; ++tile_y) {
            for (u32 tile_x = 0; tile_x < observation.map_width_tiles;
                 ++tile_x) {
                const std::size_t index = static_cast<std::size_t>(tile_y) *
                    observation.map_width_tiles + tile_x;
                if (index >= observation.tiles.size()) {
                    continue;
                }
                const AiObservedMapTile& tile = observation.tiles[index];
                if (!tile.explored || tile.resource_amount == 0) {
                    continue;
                }
                const i32 world_x = static_cast<i32>(tile_x << 5) + 16;
                const i32 world_y = static_cast<i32>(tile_y << 5) + 16;
                const i64 distance = squared_distance(observation.start_x,
                    observation.start_y, world_x, world_y);
                if (patrol_x < 0 || distance < patrol_distance) {
                    patrol_x = world_x;
                    patrol_y = world_y;
                    patrol_distance = distance;
                }
            }
        }
        if (patrol_x < 0) {
            patrol_x = std::max(observation.start_x, 0);
            patrol_y = std::max(observation.start_y, 0);
        }
        AiSemanticAction act{};
        act.kind = AiSemanticActionKind::patrol;
        act.unit_ids = std::move(combat_units);
        act.target_x = patrol_x;
        act.target_y = patrol_y;
        ++state.decisions_emitted;
        return ready(TyranoScriptedBotIntent::patrol_defense, std::move(act));
    }
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
        if (unit.controlled && unit.alive && unit_can_harvest(unit) &&
            !unit_is_harvesting(unit) && !unit_is_constructing(unit) &&
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

std::vector<AiSemanticAction> PlanTyranoDefenseAutopilot(
    TyranoScriptedBotState& state, const AiObservation& observation) {
    // ~12 tiles: raids inside this ring around a building are a base threat.
    constexpr i64 kDefenseRadiusSq = 384ll * 384ll;
    constexpr u32 kDefenseReorderFrames = 64u;

    std::vector<AiSemanticAction> actions;
    std::vector<UnitMovementPoint> base_points;
    std::vector<u32> defenders;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive) {
            if (unit.type_id >= 0x80u) {
                base_points.push_back({unit.x, unit.y});
            } else if (unit.type_id != kTyranoWorkerType &&
                       unit_can_attack(unit) && !unit_is_constructing(unit)) {
                defenders.push_back(unit.id);
            }
        }
    }
    if (base_points.empty()) {
        base_points.push_back({std::max(observation.start_x, 0),
            std::max(observation.start_y, 0)});
    }
    if (defenders.empty()) {
        return actions;
    }

    const AiObservedUnit* intruder = nullptr;
    i64 intruder_distance = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (!is_hostile_visible_unit(observation, unit)) {
            continue;
        }
        for (const UnitMovementPoint& base : base_points) {
            const i64 distance =
                squared_distance(base.x, base.y, unit.x, unit.y);
            if (distance > kDefenseRadiusSq) {
                continue;
            }
            if (intruder == nullptr || distance < intruder_distance ||
                (distance == intruder_distance && unit.id < intruder->id)) {
                intruder = &unit;
                intruder_distance = distance;
            }
        }
    }
    if (intruder == nullptr) {
        return actions;
    }
    // Re-issue only on a new target or after the dwell window; spamming the
    // same attack order every cycle resets unit pathing.
    if (intruder->id == state.last_defense_target_id &&
        state.last_defense_order_frame != 0xffffffffu &&
        observation.simulation_frame - state.last_defense_order_frame <
            kDefenseReorderFrames) {
        return actions;
    }
    state.last_defense_target_id = intruder->id;
    state.last_defense_order_frame = observation.simulation_frame;

    AiSemanticAction action{};
    action.kind = AiSemanticActionKind::attack_unit;
    action.unit_ids = std::move(defenders);
    action.target_unit_id = intruder->id;
    // >14-unit orders are rejected by the planner (too_many_units) — a large
    // garrison must answer in chunks or it silently never answers at all.
    for (AiSemanticAction& chunk : ChunkAiSemanticActionUnits(action)) {
        actions.push_back(std::move(chunk));
    }
    return actions;
}

namespace {

// Refresh the mop-up memory from this observation (idempotent per frame):
// upsert every visible hostile building, drop entries whose tile is currently
// visible without their building (confirmed razed).
void update_enemy_building_memory(TyranoScriptedBotState& state,
    const AiObservation& observation) {
    if (state.building_memory_frame == observation.simulation_frame) {
        return;
    }
    state.building_memory_frame = observation.simulation_frame;
    const auto find_remembered = [&](u32 id) -> u32 {
        for (u32 i = 0; i < state.remembered_enemy_building_count; ++i) {
            if (state.remembered_enemy_buildings[i].id == id) {
                return i;
            }
        }
        return 0xffffffffu;
    };
    for (const AiObservedUnit& unit : observation.units) {
        if (!is_hostile_visible_unit(observation, unit) ||
            unit.type_id < kTyranoMobileTypeLimit) {
            continue;
        }
        const u32 slot = find_remembered(unit.id);
        if (slot != 0xffffffffu) {
            state.remembered_enemy_buildings[slot].x = unit.x;
            state.remembered_enemy_buildings[slot].y = unit.y;
        } else if (state.remembered_enemy_building_count <
            TyranoScriptedBotState::kRememberedEnemyBuildingCapacity) {
            state.remembered_enemy_buildings[
                state.remembered_enemy_building_count++] = {unit.id, unit.x,
                unit.y};
        }
    }
    // Confirm razes: a remembered building whose tile we can SEE right now,
    // without that id among visible hostiles, is gone.
    u32 write = 0;
    for (u32 i = 0; i < state.remembered_enemy_building_count; ++i) {
        const TyranoScriptedBotState::RememberedEnemyBuilding& entry =
            state.remembered_enemy_buildings[i];
        bool keep = true;
        const u32 tile_x = static_cast<u32>(std::max(entry.x, 0)) >> 5;
        const u32 tile_y = static_cast<u32>(std::max(entry.y, 0)) >> 5;
        const std::size_t tile_index = static_cast<std::size_t>(tile_y) *
            observation.map_width_tiles + tile_x;
        if (tile_x < observation.map_width_tiles &&
            tile_y < observation.map_height_tiles &&
            tile_index < observation.tiles.size() &&
            observation.tiles[tile_index].visible) {
            keep = false;
            for (const AiObservedUnit& unit : observation.units) {
                if (unit.id == entry.id &&
                    is_hostile_visible_unit(observation, unit)) {
                    keep = true;
                    break;
                }
            }
        }
        if (keep) {
            state.remembered_enemy_buildings[write++] =
                state.remembered_enemy_buildings[i];
        }
    }
    state.remembered_enemy_building_count = write;
}

// Nearest remembered enemy building to a reference point; nullptr if none.
const TyranoScriptedBotState::RememberedEnemyBuilding*
nearest_remembered_building(const TyranoScriptedBotState& state, i32 from_x,
    i32 from_y) {
    const TyranoScriptedBotState::RememberedEnemyBuilding* best = nullptr;
    i64 best_distance = 0;
    for (u32 i = 0; i < state.remembered_enemy_building_count; ++i) {
        const TyranoScriptedBotState::RememberedEnemyBuilding& entry =
            state.remembered_enemy_buildings[i];
        const i64 distance =
            squared_distance(from_x, from_y, entry.x, entry.y);
        if (best == nullptr || distance < best_distance ||
            (distance == best_distance && entry.id < best->id)) {
            best = &entry;
            best_distance = distance;
        }
    }
    return best;
}

// Nearest UNEXPLORED tile center to a reference point ({-1,-1} when the map
// is fully explored).  Coarse 2-tile stride keeps the scan cheap; fog reveals
// a radius anyway.
UnitMovementPoint nearest_unexplored_point(const AiObservation& observation,
    i32 from_x, i32 from_y) {
    UnitMovementPoint best{-1, -1};
    i64 best_distance = 0;
    for (u32 tile_y = 0; tile_y < observation.map_height_tiles; tile_y += 2) {
        for (u32 tile_x = 0; tile_x < observation.map_width_tiles;
             tile_x += 2) {
            const std::size_t index = static_cast<std::size_t>(tile_y) *
                observation.map_width_tiles + tile_x;
            if (index >= observation.tiles.size() ||
                observation.tiles[index].explored) {
                continue;
            }
            const i32 world_x = static_cast<i32>(tile_x << 5) + 16;
            const i32 world_y = static_cast<i32>(tile_y << 5) + 16;
            const i64 distance =
                squared_distance(from_x, from_y, world_x, world_y);
            if (best.x < 0 || distance < best_distance) {
                best = {world_x, world_y};
                best_distance = distance;
            }
        }
    }
    return best;
}

} // namespace

std::vector<AiSemanticAction> PlanTyranoOffenseAutopilot(
    TyranoScriptedBotState& state, const AiObservation& observation) {
    // Enough fighters to raze a base without feeding the army piecemeal.
    constexpr std::size_t kOffenseMinArmy = 15;
    // A blind march commits the army harder, so demand a bigger force.
    constexpr std::size_t kOffenseMarchMinArmy = 25;
    // Sieges are long; re-order rarely so unit pathing is not reset.
    constexpr u32 kOffenseReorderFrames = 128u;
    // Marker id for the march order in the reorder throttle.
    constexpr u32 kOffenseMarchTargetId = 0xfffffffeu;

    std::vector<AiSemanticAction> actions;
    update_enemy_building_memory(state, observation);
    std::vector<u32> fighters;
    i64 army_x = 0;
    i64 army_y = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (unit.controlled && unit.alive && !unit.under_construction &&
            unit.type_id != kTyranoWorkerType &&
            unit.type_id < kTyranoMobileTypeLimit &&
            unit_can_attack(unit) && !unit_is_constructing(unit)) {
            fighters.push_back(unit.id);
            army_x += unit.x;
            army_y += unit.y;
        }
    }
    if (fighters.size() < kOffenseMinArmy) {
        return actions;
    }
    army_x /= static_cast<i64>(fighters.size());
    army_y /= static_cast<i64>(fighters.size());

    // Nearest visible enemy building — the elimination objective.
    const AiObservedUnit* target = nullptr;
    i64 target_distance = 0;
    for (const AiObservedUnit& unit : observation.units) {
        if (!is_hostile_visible_unit(observation, unit) ||
            unit.type_id < kTyranoMobileTypeLimit) {
            continue;
        }
        const i64 distance = squared_distance(observation.start_x,
            observation.start_y, unit.x, unit.y);
        if (target == nullptr || distance < target_distance ||
            (distance == target_distance && unit.id < target->id)) {
            target = &unit;
            target_distance = distance;
        }
    }
    if (target == nullptr) {
        // No enemy building in SIGHT.  Mop-up chain (each tier only when the
        // previous is empty):
        //   1. march on the nearest REMEMBERED building (seen earlier,
        //      currently fogged) — a relocated/hidden base is usually here;
        //   2. march on the nearest enemy start whose tile is still
        //      UNEXPLORED (camping an explored-empty start caused the
        //      100k-frame stalemates the last record replay showed);
        //   3. sweep the nearest unexplored region from the army centroid;
        //   4. fully explored and no memory: rotate the coarse exploration
        //      cycle so re-fogged areas get re-checked for rebuilt bases.
        if (fighters.size() < kOffenseMarchMinArmy) {
            return actions;
        }
        constexpr u32 kOffenseSweepTargetId = 0xfffffffdu;
        i32 march_x = -1;
        i32 march_y = -1;
        u32 march_marker = kOffenseMarchTargetId;
        const TyranoScriptedBotState::RememberedEnemyBuilding* remembered =
            nearest_remembered_building(state,
                static_cast<i32>(army_x), static_cast<i32>(army_y));
        if (remembered != nullptr) {
            march_x = remembered->x;
            march_y = remembered->y;
            march_marker = remembered->id;
        }
        if (march_x < 0) {
            i64 best_distance = 0;
            for (u32 slot = 0; slot < 8u; ++slot) {
                if ((observation.start_candidate_mask & (1u << slot)) == 0) {
                    continue;
                }
                const i32 sx = observation.start_candidate_x[slot];
                const i32 sy = observation.start_candidate_y[slot];
                const i64 distance = squared_distance(observation.start_x,
                    observation.start_y, sx, sy);
                if (distance == 0) {
                    continue;  // our own start slot
                }
                const u32 tile_x = static_cast<u32>(std::max(sx, 0)) >> 5;
                const u32 tile_y = static_cast<u32>(std::max(sy, 0)) >> 5;
                const std::size_t tile_index =
                    static_cast<std::size_t>(tile_y) *
                    observation.map_width_tiles + tile_x;
                if (tile_index < observation.tiles.size() &&
                    observation.tiles[tile_index].explored) {
                    continue;  // already checked; do not camp an empty start
                }
                if (march_x < 0 || distance < best_distance) {
                    best_distance = distance;
                    march_x = sx;
                    march_y = sy;
                }
            }
        }
        if (march_x < 0) {
            const UnitMovementPoint sweep = nearest_unexplored_point(
                observation, static_cast<i32>(army_x),
                static_cast<i32>(army_y));
            if (sweep.x >= 0) {
                march_x = sweep.x;
                march_y = sweep.y;
                march_marker = kOffenseSweepTargetId;
            }
        }
        if (march_x < 0) {
            ++state.exploration_index;
            const UnitMovementPoint cycle =
                exploration_point(state, observation);
            march_x = cycle.x;
            march_y = cycle.y;
            march_marker = kOffenseSweepTargetId;
        }
        if (state.last_offense_target_id == march_marker &&
            state.last_offense_order_frame != 0xffffffffu &&
            observation.simulation_frame - state.last_offense_order_frame <
                kOffenseReorderFrames) {
            return actions;
        }
        state.last_offense_target_id = march_marker;
        state.last_offense_order_frame = observation.simulation_frame;
        AiSemanticAction march{};
        march.kind = AiSemanticActionKind::attack_move;
        march.unit_ids = std::move(fighters);
        march.target_x = march_x;
        march.target_y = march_y;
        for (AiSemanticAction& chunk : ChunkAiSemanticActionUnits(march)) {
            actions.push_back(std::move(chunk));
        }
        return actions;
    }
    if (target->id == state.last_offense_target_id &&
        state.last_offense_order_frame != 0xffffffffu &&
        observation.simulation_frame - state.last_offense_order_frame <
            kOffenseReorderFrames) {
        return actions;
    }
    state.last_offense_target_id = target->id;
    state.last_offense_order_frame = observation.simulation_frame;

    AiSemanticAction action{};
    action.kind = AiSemanticActionKind::attack_unit;
    action.unit_ids = std::move(fighters);
    action.target_unit_id = target->id;
    for (AiSemanticAction& chunk : ChunkAiSemanticActionUnits(action)) {
        actions.push_back(std::move(chunk));
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
