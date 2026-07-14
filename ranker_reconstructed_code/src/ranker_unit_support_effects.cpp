#include "ranker_unit_support_effects.h"

#include "ranker_unit_damage.h"

#include <algorithm>

namespace ranker {
namespace {

const ProductionOrderRuntimeState& production_state_or_empty(
    const UnitSupportEffectContext& context) {
    static const ProductionOrderRuntimeState empty_state{};
    return context.production_state != nullptr ? *context.production_state : empty_state;
}

bool has_owner_masks(const UnitSupportEffectContext& context) {
    const auto has_value = [](const auto& values) {
        return std::any_of(values.begin(), values.end(), [](u32 value) {
            return value != 0;
        });
    };
    return has_value(context.source_owner_masks) || has_value(context.target_owner_masks);
}

u32 owner_mask_index(u32 owner_id) {
    return owner_id < kProductionOrderOwnerCount ? owner_id : kProductionOrderOwnerCount;
}

UnitMovementUnit* first_unit_after(UnitMovementContext& movement_context,
    const UnitMovementUnit* after, std::size_t& index) {
    index = 0;
    if (after == nullptr) {
        return movement_context.active_units.empty() ? nullptr : movement_context.active_units[0];
    }

    auto it = std::find(movement_context.active_units.begin(),
        movement_context.active_units.end(), after);
    if (it == movement_context.active_units.end()) {
        return nullptr;
    }

    ++it;
    index = static_cast<std::size_t>(it - movement_context.active_units.begin());
    return it == movement_context.active_units.end() ? nullptr : *it;
}

bool unit_distance_in_support_range(const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    return CalculateApproxUnitDistance(source.x, source.y, target.x, target.y) <=
        GetUnitSupportRange(source);
}

bool secondary_transfer_candidate(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    if ((target.runtime_flags & kUnitSupportRuntimeInactive) != 0) {
        return false;
    }
    if (!CheckUnitSupportOwnerMask(context, source, target)) {
        return false;
    }
    if (!CheckUnitBelowRuntimeMaxSecondaryValueWithProductionEffect01(
            production_state_or_empty(context), target)) {
        return false;
    }
    if (target.type_id == kUnitSupportSecondaryExcludedType) {
        return false;
    }
    return unit_distance_in_support_range(source, target);
}

bool health_restore_candidate(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    if (&source == &target) {
        return false;
    }
    if ((target.runtime_flags & kUnitSupportRuntimeInactive) != 0) {
        return false;
    }
    if (!CheckUnitSupportOwnerMask(context, source, target)) {
        return false;
    }
    if ((target.definition.support_target_flags & kUnitSupportTargetHealthRestore) == 0) {
        return false;
    }
    if (!CheckUnitBelowRuntimeMaxHealthWithProductionEffect00(
            production_state_or_empty(context), target)) {
        return false;
    }
    return unit_distance_in_support_range(source, target);
}

} // namespace

bool IsUnitSupportFrameSlot(const UnitMovementUnit& source, u32 frame_counter) {
    const u32 slot_index = source.runtime_slot_index != kInvalidUnitRuntimeSlotIndex
        ? source.runtime_slot_index
        : source.id;
    return (frame_counter & kUnitSupportFrameSlotMask) ==
        (slot_index & kUnitSupportFrameSlotMask);
}

u32 GetUnitSupportRange(const UnitMovementUnit& source) {
    if (source.definition.support_range != 0) {
        return source.definition.support_range;
    }
    return source.definition.range_threshold;
}

void PrepareUnitSecondaryTransferScan(UnitSupportEffectContext& context,
    const UnitMovementUnit& source) {
    const u32 source_index = owner_mask_index(source.owner_id);
    context.prepared_source_owner_mask =
        source_index < kProductionOrderOwnerCount
            ? context.source_owner_masks[source_index]
            : 0;
    context.prepared_support_range = GetUnitSupportRange(source);
}

void PrepareUnitHealthRestoreScan(UnitSupportEffectContext& context,
    const UnitMovementUnit& source) {
    PrepareUnitSecondaryTransferScan(context, source);
}

bool CheckUnitSupportOwnerMask(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    if (context.callbacks.can_affect != nullptr) {
        return context.callbacks.can_affect(context, source, target);
    }

    const u32 source_index = owner_mask_index(source.owner_id);
    const u32 target_index = owner_mask_index(target.owner_id);
    if (source_index < kProductionOrderOwnerCount &&
        target_index < kProductionOrderOwnerCount && has_owner_masks(context)) {
        return (context.source_owner_masks[source_index] &
            context.target_owner_masks[target_index]) != 0;
    }

    return source.owner_id == target.owner_id;
}

UnitMovementUnit* FindNextSecondaryTransferTarget(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit* after) {
    if (context.movement_context == nullptr) {
        return nullptr;
    }

    std::size_t index = 0;
    UnitMovementUnit* current = first_unit_after(*context.movement_context, after, index);
    while (current != nullptr) {
        if (secondary_transfer_candidate(context, source, *current)) {
            return current;
        }
        ++index;
        current = index < context.movement_context->active_units.size()
            ? context.movement_context->active_units[index]
            : nullptr;
    }
    return nullptr;
}

UnitMovementUnit* FindNextHealthRestoreTarget(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit* after) {
    if (context.movement_context == nullptr) {
        return nullptr;
    }

    std::size_t index = 0;
    UnitMovementUnit* current = first_unit_after(*context.movement_context, after, index);
    while (current != nullptr) {
        if (health_restore_candidate(context, source, *current)) {
            return current;
        }
        ++index;
        current = index < context.movement_context->active_units.size()
            ? context.movement_context->active_units[index]
            : nullptr;
    }
    return nullptr;
}

void ProcessUnitSecondaryTransferSupport(UnitSupportEffectContext& context,
    UnitMovementUnit& source) {
    if (source.secondary_value == 0 ||
        !IsUnitSupportFrameSlot(source, context.frame_counter)) {
        return;
    }
    PrepareUnitSecondaryTransferScan(context, source);

    bool transferred = false;
    UnitMovementUnit* current_target = nullptr;
    while (source.secondary_value != 0) {
        UnitMovementUnit* target = FindNextSecondaryTransferTarget(
            context, source, current_target);
        if (target == nullptr) {
            break;
        }

        if (context.callbacks.on_secondary_transfer != nullptr) {
            context.callbacks.on_secondary_transfer(context, source, *target);
        }
        else {
            AddUnitSecondaryValueClampedToProductionEffect01(
                production_state_or_empty(context), *target, 1);
        }

        --source.secondary_value;
        transferred = true;
        current_target = target;
    }

    if (transferred && context.callbacks.on_secondary_transfer_batch_complete != nullptr) {
        context.callbacks.on_secondary_transfer_batch_complete(context, source);
    }
}

void ProcessUnitHealthRestoreSupport(UnitSupportEffectContext& context,
    UnitMovementUnit& source) {
    if ((source.runtime_flags & kUnitSupportRuntimeInactive) != 0 ||
        source.secondary_value <= context.health_restore_secondary_threshold ||
        !IsUnitSupportFrameSlot(source, context.frame_counter)) {
        return;
    }
    PrepareUnitHealthRestoreScan(context, source);

    UnitMovementUnit* best = nullptr;
    u32 best_priority = 0xffffffffu;
    UnitMovementUnit* after = nullptr;
    for (;;) {
        UnitMovementUnit* target = FindNextHealthRestoreTarget(context, source, after);
        if (target == nullptr) {
            break;
        }
        if (target->definition.support_priority < best_priority) {
            best_priority = target->definition.support_priority;
            best = target;
        }
        after = target;
    }

    if (best == nullptr) {
        return;
    }
    if (context.callbacks.on_health_restore_target_selected != nullptr) {
        context.callbacks.on_health_restore_target_selected(context, source, *best);
    }
    if (context.callbacks.on_health_restore_effect != nullptr) {
        context.callbacks.on_health_restore_effect(context, source, *best);
    }
}

void ProcessUnitSupportEffectsByDefinition(UnitSupportEffectContext& context,
    UnitMovementUnit& source) {
    if ((source.runtime_flags & kUnitSupportRuntimeSourceSkipMask) != 0) {
        return;
    }

    if ((source.definition.support_source_flags &
            kUnitSupportSourceSecondaryTransfer) != 0) {
        ProcessUnitSecondaryTransferSupport(context, source);
        return;
    }

    if ((source.definition.support_source_flags &
            kUnitSupportSourceHealthRestore) != 0) {
        ProcessUnitHealthRestoreSupport(context, source);
    }
}

void ProcessPeriodicUnitSupportMarkerAura(UnitSupportEffectContext& context) {
    if (context.movement_context == nullptr ||
        (context.frame_counter & kUnitSupportMarkerAuraFrameMask) != 0) {
        return;
    }

    for (UnitMovementUnit* unit : context.movement_context->active_units) {
        if (unit != nullptr) {
            unit->runtime_flags &= ~kUnitSupportRuntimeMarkedByAura;
        }
    }

    bool marked_any = false;
    for (UnitMovementUnit* source : context.movement_context->active_units) {
        if (source == nullptr ||
            (source->runtime_flags & 0x90u) != 0 ||
            (source->definition.support_source_flags &
                kUnitSupportSourceMarkerAura) == 0) {
            continue;
        }

        for (UnitMovementUnit* target : context.movement_context->active_units) {
            if (target == nullptr ||
                (target->runtime_flags & kUnitSupportRuntimeInactive) != 0 ||
                (target->runtime_flags & kUnitSupportRuntimeMarkedByAura) != 0 ||
                (target->definition.support_target_flags &
                    kUnitSupportTargetMarkerAura) == 0 ||
                !CheckUnitSupportOwnerMask(context, *source, *target) ||
                !unit_distance_in_support_range(*source, *target)) {
                continue;
            }

            target->runtime_flags |= kUnitSupportRuntimeMarkedByAura;
            marked_any = true;
        }
    }

    if (marked_any && context.marker_aura_sound_enabled &&
        context.callbacks.on_marker_aura_sound != nullptr) {
        context.callbacks.on_marker_aura_sound(context);
    }
}

void ApplyUnitKillHealthRestoreSupport(UnitSupportEffectContext& context,
    UnitMovementUnit& source, UnitMovementUnit& defeated) {
    if ((source.definition.support_source_flags &
            kUnitSupportSourceKillHealthRestore) == 0 ||
        (defeated.definition.support_target_flags &
            kUnitSupportTargetHealthRestore) == 0 ||
        !CheckUnitBelowRuntimeMaxHealthWithProductionEffect00(
            production_state_or_empty(context), source)) {
        return;
    }

    AddUnitHealthClampedToProductionEffect00(production_state_or_empty(context),
        source, defeated.max_health >> 2);
    if (context.callbacks.on_kill_health_restore != nullptr) {
        context.callbacks.on_kill_health_restore(context, source, defeated);
    }
}

} // namespace ranker
