#pragma once

#include "ranker_ai_observation.h"
#include "ranker_gameplay_input_actions.h"

#include <cstddef>
#include <vector>

namespace ranker {

constexpr u32 kAiActionSchemaVersion = 1;
constexpr std::size_t kAiMaximumUnitsPerAction = 14;
constexpr u32 kAiNoProductionId = 0xffffffffu;
constexpr u32 kAiLatestQueueIndex = 0xffffffffu;

enum class AiSemanticActionKind : u32 {
    no_op = 0,
    move,
    attack_move,
    attack_unit,
    harvest,
    produce_unit,
    research,
    build,
    set_rally,
    cancel_production,
};

struct AiSemanticAction {
    u32 schema_version = kAiActionSchemaVersion;
    AiSemanticActionKind kind = AiSemanticActionKind::no_op;
    std::vector<u32> unit_ids;
    u32 target_unit_id = 0;
    i32 target_x = 0;
    i32 target_y = 0;
    u32 production_id = kAiNoProductionId;
    u32 queue_index = kAiLatestQueueIndex;
    bool queued = false;
};

enum class AiProductionRequestKind : u32 {
    unit = 0,
    building,
    research,
};

struct AiProductionAvailability {
    bool available = false;
    u32 code = 0;
    u32 primary_cost = 0;
    u32 secondary_cost = 0;
};

using AiProductionAvailabilityCallback = AiProductionAvailability (*)(
    AiProductionRequestKind kind, const UnitMovementUnit& source,
    u32 production_id, i32 world_x, i32 world_y, u32 local_owner,
    void* user_data);

struct AiActionPlanInput {
    u32 local_owner = 0;
    const PlayerSlotRuntimeState* players = nullptr;
    const UnitMovementContext* movement = nullptr;
    AiUnitVisibilityCallback unit_visible = nullptr;
    void* unit_visibility_user_data = nullptr;
    // Harvest(point) fails closed without the local owner's vision projection.
    // This prevents action validation from becoming an oracle for resources
    // hidden by fog of war.  The current-visibility layer is only maintained
    // for the local viewing player, so an AI owner authorizes harvest against
    // explored terrain when that projection is supplied (falling back to the
    // current-visibility layer otherwise).
    const std::vector<u8>* visible_tiles = nullptr;
    const std::vector<u8>* explored_tiles = nullptr;
    // Produce/research/build fail closed unless the live session supplies a
    // validator that applies the authoritative catalog, resource,
    // prerequisite, population, queue and placement rules.
    AiProductionAvailabilityCallback production_available = nullptr;
    void* production_availability_user_data = nullptr;
};

enum class AiActionPlanCode : u32 {
    okay = 0,
    unsupported_schema,
    unsupported_action,
    invalid_local_owner,
    missing_players,
    missing_movement,
    invalid_map_dimensions,
    empty_unit_selection,
    too_many_units,
    duplicate_unit_id,
    unknown_unit_id,
    unit_not_owned,
    unit_inactive,
    unit_action_unsupported,
    unexpected_target,
    missing_target,
    target_inactive,
    target_not_visible,
    target_is_friendly,
    invalid_visible_tile_count,
    target_not_harvestable,
    point_out_of_bounds,
    requires_single_unit,
    unexpected_production,
    invalid_production_id,
    missing_production_validator,
    production_unavailable,
    production_queue_full,
    queued_flag_unsupported,
    rally_source_unsupported,
    unsupported_queue_index,
    nothing_to_cancel,
};

struct AiActionPlanResult {
    AiActionPlanCode code = AiActionPlanCode::unsupported_action;
    std::vector<GameplayPublishedAction> packets;

    explicit operator bool() const {
        return code == AiActionPlanCode::okay;
    }
};

AiActionPlanResult PlanAiSemanticActionV1(const AiActionPlanInput& input,
    const AiSemanticAction& action);

} // namespace ranker
