#pragma once

#include "ranker_production_orders.h"
#include "ranker_unit_movement.h"

#include <array>

namespace ranker {

constexpr u32 kUnitSupportRuntimeInactive = 0x00000080;
constexpr u32 kUnitSupportRuntimeSourceSkipMask = 0x000000b0;
constexpr u32 kUnitSupportRuntimeMarkedByAura = 0x00001000;
constexpr u32 kUnitSupportSourceSecondaryTransfer = 0x00000004;
constexpr u32 kUnitSupportSourceMarkerAura = 0x00000008;
constexpr u32 kUnitSupportSourceHealthRestore = 0x00000010;
constexpr u32 kUnitSupportSourceKillHealthRestore = 0x00000020;
constexpr u32 kUnitSupportTargetHealthRestore = 0x00000010;
constexpr u32 kUnitSupportTargetMarkerAura = 0x00000400;
constexpr u32 kUnitSupportFrameSlotMask = 0x0000000f;
constexpr u32 kUnitSupportMarkerAuraFrameMask = 0x0000001f;
constexpr u32 kUnitSupportSecondaryExcludedType = 0x17;

struct UnitSupportEffectContext;

using UnitSupportCanAffectCallback = bool (*)(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
using UnitSupportPairCallback = void (*)(UnitSupportEffectContext& context,
    UnitMovementUnit& source, UnitMovementUnit& target);
using UnitSupportCallback = void (*)(UnitSupportEffectContext& context,
    UnitMovementUnit& unit);
using UnitSupportNoArgCallback = void (*)(UnitSupportEffectContext& context);

struct UnitSupportEffectCallbacks {
    UnitSupportCanAffectCallback can_affect = nullptr;
    UnitSupportPairCallback on_secondary_transfer = nullptr;
    UnitSupportCallback on_secondary_transfer_batch_complete = nullptr;
    UnitSupportPairCallback on_health_restore_target_selected = nullptr;
    UnitSupportPairCallback on_health_restore_effect = nullptr;
    UnitSupportNoArgCallback on_marker_aura_sound = nullptr;
    UnitSupportPairCallback on_kill_health_restore = nullptr;
};

struct UnitSupportEffectContext {
    const ProductionOrderRuntimeState* production_state = nullptr;
    UnitMovementContext* movement_context = nullptr;
    UnitSupportEffectCallbacks callbacks;
    std::array<u32, kProductionOrderOwnerCount> source_owner_masks{};
    std::array<u32, kProductionOrderOwnerCount> target_owner_masks{};
    u32 frame_counter = 0;
    u32 health_restore_secondary_threshold = 0;
    u32 marker_aura_sound_base_slot = 0;
    u32 marker_aura_sound_offset = 0;
    i32 marker_aura_sound_x = 0;
    i32 marker_aura_sound_y = 0;
    u32 prepared_source_owner_mask = 0;
    u32 prepared_support_range = 0;
    bool marker_aura_sound_enabled = true;
    bool marker_aura_sound_point_valid = false;
};

bool IsUnitSupportFrameSlot(const UnitMovementUnit& source, u32 frame_counter);
u32 GetUnitSupportRange(const UnitMovementUnit& source);
void PrepareUnitSecondaryTransferScan(UnitSupportEffectContext& context,
    const UnitMovementUnit& source);
void PrepareUnitHealthRestoreScan(UnitSupportEffectContext& context,
    const UnitMovementUnit& source);
bool CheckUnitSupportOwnerMask(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
UnitMovementUnit* FindNextSecondaryTransferTarget(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit* after = nullptr);
UnitMovementUnit* FindNextHealthRestoreTarget(UnitSupportEffectContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit* after = nullptr);
void ProcessUnitSecondaryTransferSupport(UnitSupportEffectContext& context,
    UnitMovementUnit& source);
void ProcessUnitHealthRestoreSupport(UnitSupportEffectContext& context,
    UnitMovementUnit& source);
void ProcessUnitSupportEffectsByDefinition(UnitSupportEffectContext& context,
    UnitMovementUnit& source);
void ProcessPeriodicUnitSupportMarkerAura(UnitSupportEffectContext& context);
void ApplyUnitKillHealthRestoreSupport(UnitSupportEffectContext& context,
    UnitMovementUnit& source, UnitMovementUnit& defeated);

}
