#pragma once

#include "ranker_ai_observation.h"
#include "ranker_gameplay_input_actions.h"

#include <cstddef>
#include <vector>

namespace ranker {

constexpr u32 kAiActionSchemaVersion = 2;
constexpr std::size_t kAiMaximumUnitsPerAction = 14;
constexpr u32 kAiNoProductionId = 0xffffffffu;
constexpr u32 kAiLatestQueueIndex = 0xffffffffu;
constexpr u32 kAiNoAbilityId = 0xffffffffu;
constexpr u32 kAiNoStanceId = 0xffffffffu;
// Wire ability ids are the subtype-0x09 command byte; the execution layer
// rejects ids >= 0x2e (default_unit_command_can_use_ability).
constexpr u32 kAiAbilityIdLimit = 0x2eu;
constexpr u32 kAiStanceCount = 4;
// A producer building holds at most this many deferred production commands;
// PlanAiSemanticActionV1 rejects a produce order past it with
// AiActionPlanCode::production_queue_full.  The RL legal-action mask reads the
// same limit so "masked legal" means "the planner will accept it".
constexpr u32 kUnitProductionQueueLimit = 4u;

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
    // Schema v2. Wire
    // routes are the audited human paths: subtype 0x02 command byte unless
    // noted otherwise.
    stop,               // cmd 0x00 -> immediate idle
    hold_position,      // subtype 0x0a, cmd 0x21
    patrol,             // cmd 0x09 + point
    use_ability,        // subtype 0x09, command byte = ability_id
    morph_enter,        // cmd 0x11
    morph_exit,         // cmd 0x1b
    merge_units,        // cmd 0x0b, 2-unit mirrored pair or 3-unit ring
    board_transport,    // cmd 0x0a, passengers -> carrier target
    unload_transport,   // cmd 0x24, carrier + point
    transfer_secondary, // cmd 0x23, balance action_mode across the group
    set_stance,         // cmd 0x12+stance_id on / arg1=1+flag off
    return_cargo,       // cmd 0x07 + target 0x80000000
    use_item,           // cmd 0x16
    // v9: point order via wire cmd 0x0d - the ACQUIRE variant of the attack
    // command: identical target-validation runtime, but the entry SETS
    // area_marker_flags bit 31 (cmd 0x05 clears it), which is the engine's
    // auto-pickup enable.  A unit sent to a meat drop this way collects it
    // on going idle at the point (idle-acquire -> state 5 -> collect).
    pickup_move,        // cmd 0x0d + point
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
    // v2 fields; defaults keep v1-style actions valid.
    u32 ability_id = kAiNoAbilityId;
    u32 stance_id = kAiNoStanceId;
    bool stance_on = true;
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

struct AiAbilityAvailability {
    bool available = false;
    u32 code = 0;
    u32 secondary_cost = 0;
};

// use_ability fails closed unless the live session supplies a validator that
// applies the authoritative JW2_11 cost and effect-target rules (mirrors the
// production-availability pattern above).
using AiAbilityAvailabilityCallback = AiAbilityAvailability (*)(
    const UnitMovementUnit& source, u32 ability_id, u32 target_unit_id,
    i32 world_x, i32 world_y, u32 local_owner, void* user_data);

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
    AiAbilityAvailabilityCallback ability_available = nullptr;
    void* ability_availability_user_data = nullptr;
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
    // Schema v2 codes (append-only).
    unexpected_ability,
    invalid_ability_id,
    missing_ability_validator,
    ability_unavailable,
    morph_unavailable,
    not_morphed,
    merge_arity_invalid,
    merge_recipe_invalid,
    target_not_carrier,
    passenger_cannot_board,
    nothing_to_transfer,
    invalid_stance,
    stance_unavailable,
    stance_inactive,
    nothing_to_return,
    missing_item,
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
