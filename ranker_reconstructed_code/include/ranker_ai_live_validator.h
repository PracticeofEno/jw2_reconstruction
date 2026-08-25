#pragma once

#include "ranker_ai_actions.h"
#include "ranker_game_session_tables.h"

namespace ranker {

constexpr u32 kAiLiveValidationMissingContext = 0x10000u;
constexpr u32 kAiLiveValidationInvalidSource = 0x10001u;
constexpr u32 kAiLiveValidationMissingReference = 0x10002u;
constexpr u32 kAiLiveValidationInvalidPlacement = 0x10003u;

using AiLiveUnitRequirementCallback = AiProductionAvailability (*)(
    u32 owner, u32 unit_type, void* user_data);
using AiLiveResearchRequirementCallback = AiProductionAvailability (*)(
    u32 owner, u32 order_id, void* user_data);
using AiLivePlacementCallback = bool (*)(const UnitMovementUnit& source,
    u32 building_type, i32 world_x, i32 world_y, u32 owner,
    void* user_data);

struct AiLiveProductionValidationContext {
    const GameSessionUnitReferenceTables* unit_references = nullptr;
    AiLiveUnitRequirementCallback check_unit_requirements = nullptr;
    AiLiveResearchRequirementCallback check_research_requirements = nullptr;
    AiLivePlacementCallback check_placement = nullptr;
    void* callback_user_data = nullptr;
};

AiProductionAvailability CheckAiLiveProductionAvailability(
    AiProductionRequestKind kind, const UnitMovementUnit& source,
    u32 production_id, i32 world_x, i32 world_y, u32 local_owner,
    void* user_data);

} // namespace ranker
