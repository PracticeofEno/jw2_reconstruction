#include "ranker_ai_live_validator.h"

#include <algorithm>

namespace ranker {
namespace {

template <typename Values>
bool contains_reference(const Values& values, u32 raw_count, u32 value) {
    const u32 count = std::min<u32>(raw_count,
        static_cast<u32>(values.size()));
    return std::find(values.begin(), values.begin() + count, value) !=
        values.begin() + count;
}

AiProductionAvailability reject(u32 code) {
    AiProductionAvailability result{};
    result.code = code;
    return result;
}

} // namespace

AiProductionAvailability CheckAiLiveProductionAvailability(
    AiProductionRequestKind kind, const UnitMovementUnit& source,
    u32 production_id, i32 world_x, i32 world_y, u32 local_owner,
    void* user_data) {
    const auto* context =
        static_cast<const AiLiveProductionValidationContext*>(user_data);
    if (context == nullptr || context->unit_references == nullptr) {
        return reject(kAiLiveValidationMissingContext);
    }
    if (!source.active || source.owner_id != local_owner ||
        (source.command_state & kUnitCommandDead) != 0 ||
        source.type_id >= context->unit_references->definitions.size()) {
        return reject(kAiLiveValidationInvalidSource);
    }

    const UnitTypeSessionDefinition& references =
        context->unit_references->definitions[source.type_id];
    if (!references.present) {
        return reject(kAiLiveValidationMissingReference);
    }

    if (kind == AiProductionRequestKind::unit) {
        if (!contains_reference(references.alternate_references,
                references.alternate_reference_count, production_id)) {
            return reject(kAiLiveValidationMissingReference);
        }
        if (context->check_unit_requirements == nullptr) {
            return reject(kAiLiveValidationMissingContext);
        }
        return context->check_unit_requirements(local_owner, production_id,
            context->callback_user_data);
    }

    if (kind == AiProductionRequestKind::research) {
        if (!contains_reference(references.completion_references,
                references.completion_reference_count, production_id)) {
            return reject(kAiLiveValidationMissingReference);
        }
        if (context->check_research_requirements == nullptr) {
            return reject(kAiLiveValidationMissingContext);
        }
        return context->check_research_requirements(local_owner, production_id,
            context->callback_user_data);
    }

    if (kind == AiProductionRequestKind::building) {
        if (!contains_reference(references.primary_references,
                references.primary_reference_count, production_id)) {
            return reject(kAiLiveValidationMissingReference);
        }
        if (context->check_unit_requirements == nullptr ||
            context->check_placement == nullptr) {
            return reject(kAiLiveValidationMissingContext);
        }
        AiProductionAvailability availability =
            context->check_unit_requirements(local_owner, production_id,
                context->callback_user_data);
        if (!availability.available) {
            return availability;
        }
        if (!context->check_placement(source, production_id, world_x, world_y,
                local_owner, context->callback_user_data)) {
            availability.available = false;
            availability.code = kAiLiveValidationInvalidPlacement;
        }
        return availability;
    }

    return reject(kAiLiveValidationMissingReference);
}

} // namespace ranker
