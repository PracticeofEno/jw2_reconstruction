#pragma once

#include "ranker_production_orders.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_movement.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace ranker {

struct GameplayRenderCommandQueue;
struct GameplayVisibilityGrid;
struct PlayerSlotRuntimeState;
struct UnitLifecycleContext;

constexpr u32 kUnitActionCommandStarted = 0x00000010;
constexpr u32 kUnitActionImpactApplied = 0x00000400;
constexpr u32 kUnitActionCommandClearMask = 0x00000407;
constexpr u32 kUnitActionTargetTransient = 0x00000004;
constexpr u32 kUnitActionTargetInactive = 0x00000080;
constexpr u32 kUnitActionTargetClassBlocked = 0x20000000;

enum class UnitActionTickCode : u32 {
    lost_target = 0,
    cycle_complete = 1,
    cycle_started = 2,
    cycle_in_progress = 3,
    turning_to_target = 4,
};

struct UnitActionContext;
struct UnitActionTargetValidation;
struct UnitEffectRuntime;
struct UnitEffectRuntimeState;

using UnitActionCanTargetCallback = bool (*)(UnitActionContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
using UnitActionDistanceCallback = u32 (*)(UnitActionContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
using UnitActionTargetReachCallback = UnitActionTargetValidation (*)(
    UnitActionContext& context, const UnitMovementUnit& source,
    const UnitMovementUnit& target);
using UnitActionPairCallback = void (*)(UnitActionContext& context,
    UnitMovementUnit& source, UnitMovementUnit& target);
using UnitActionCallback = void (*)(UnitActionContext& context,
    UnitMovementUnit& source);
using UnitActionTargetFinder = UnitMovementUnit* (*)(UnitActionContext& context,
    UnitMovementUnit& source);
using UnitActionAutoEffectCallback = bool (*)(UnitActionContext& context,
    UnitMovementUnit& source, i32 target_x, i32 target_y);
using UnitEffectCameraShakeCallback = void (*)(
    UnitEffectRuntimeState& state, const UnitEffectRuntime& effect);
using UnitEffectSelectedProductionGateCallback = bool (*)(
    UnitEffectRuntimeState& state, UnitMovementUnit& source, u32 selector);
using UnitEffectCreateUnitCallback = UnitMovementUnit* (*)(
    UnitEffectRuntimeState& state, UnitMovementUnit& source, u32 type_id,
    i32 x, i32 y);
using UnitEffectImpactDamageCallback = u32 (*)(
    UnitEffectRuntimeState& state, const UnitEffectRuntime& effect,
    UnitMovementUnit* source, UnitMovementUnit& target);

struct UnitActionCallbacks {
    UnitActionCanTargetCallback can_target = nullptr;
    UnitActionDistanceCallback distance_to_target = nullptr;
    UnitActionTargetReachCallback validate_target_reach = nullptr;
    UnitActionTargetFinder find_replacement_target = nullptr;
    UnitActionCanTargetCallback can_replace_transient_target = nullptr;
    UnitActionAutoEffectCallback try_auto_effect_command = nullptr;
    UnitActionCallback on_begin_action_animation = nullptr;
    UnitActionCallback on_action_sound = nullptr;
    UnitActionPairCallback on_action_impact = nullptr;
    UnitActionCallback on_action_cycle_complete = nullptr;
    UnitActionCallback on_target_lost = nullptr;
};

struct UnitActionContext {
    UnitMovementContext* movement_context = nullptr;
    const ProductionOrderRuntimeState* production_state = nullptr;
    const UnitEquipmentCatalog* equipment_catalog = nullptr;
    UnitActionCallbacks callbacks;
};

struct UnitEffectRuntimeCallbacks {
    UnitEffectCameraShakeCallback on_camera_shake = nullptr;
    UnitEffectSelectedProductionGateCallback selected_production_gate = nullptr;
    UnitEffectCreateUnitCallback create_unit = nullptr;
    UnitEffectImpactDamageCallback calculate_impact_damage = nullptr;
};

struct UnitActionTargetValidation {
    bool valid = false;
    bool in_range = false;
    u32 distance = 0xffffffffu;
    u32 range = 0;
    i32 command_x = 0;
    i32 command_y = 0;
};

struct UnitActionTickResult {
    UnitActionTickCode code = UnitActionTickCode::lost_target;
    UnitMovementUnit* target = nullptr;
    u32 distance = 0xffffffffu;
    bool valid_target = false;
    bool impact_frame = false;
};

struct UnitEffectActionTargetGateResult {
    u32 value = 0;
    bool carry = false;
};

constexpr u32 kUnitEffectFlagStartup = 0x02;
constexpr u32 kUnitEffectFlagImpact = 0x80;
constexpr u32 kUnitEffectFlagRefundOnFinish = 0x40;
constexpr u32 kUnitEffectFlagProjectileYMajor = 0x01;
constexpr u32 kUnitEffectFlagProjectileBoundsEntered = 0x100;
constexpr u32 kUnitEffectFlagAfterimageClone = 0x400;
// DAT_01426c00 contains 512 raw 0xa8-byte blocks, but offset zero is the
// intrusive-list null sentinel.  Only offsets 0xa8..0x14f58 are allocatable.
constexpr u32 kUnitEffectDefaultSlotCapacity = 511;

enum class UnitEffectEventKind : u32 {
    started = 0,
    frame_sound = 1,
    impact = 2,
    render = 3,
    refunded = 4,
    finished = 5,
    target_lockout = 6,
};

enum class UnitEffectSoundSpatialKind : u32 {
    world_point = 0,
    source_unit_current_tile = 1,
    linked_unit_current_tile = 2,
};

struct UnitEffectDefinition {
    u32 id = 0;
    u32 startup_ticks = 0;
    u32 active_frames = 1;
    u32 active_step_iterations = 1;
    u32 render_ticks = 1;
    u32 impact_render_ticks = 1;
    u32 impact_radius = 0;
    u32 damage_amount = 0;
    u32 sprite_entry = 0;
    u32 startup_draw_mode = 0;
    u32 active_draw_mode = 0;
    u32 impact_draw_mode = 0;
    u32 impact_class_stride_factor = 0;
    u32 impact_class_frame_count = 0;
    u32 behavior_flags = 0;
    u32 render_sort_handler = 0xffffffffu;
    u32 action_direction_mode = 0;
    u32 action_path_control = 0;
    u32 action_projectile_loop_ticks = 0;
    u32 action_source_health_cost = 0;
    u32 action_source_stat20_delta = 0;
    u32 action_secondary_cost = 1;
    u32 action_create_unit_refund_secondary = 0;
    u32 action_create_unit_secondary_value = 0;
    u32 action_create_unit_type_id = 0;
    u32 action_secondary_area_debit_limit = 0;
    u32 action_area_target_render_class_mask = 0xffffffffu;
    u32 action_channel_linked_damage_period = 0;
    u32 action_aura_frame_limit = 0;
    u32 action_aura_tick_reset_value = 0;
    u32 action_aura_tick_reset_threshold = 0;
    u32 action_aura_radius = 0;
    u32 action_area_damage_radius = 0;
    u32 action_target_lock_frame = 0;
    u32 action_startup_ticks = 0;
    u32 action_nearby_marker_radius = 0;
    i32 action_target_health_delta = 1;
    std::array<u32, 3> action_projectile_impact_percent{{100, 100, 100}};
    u32 start_sound_slot = 0xffffffffu;
    u32 allowed_target_render_class_mask = 0xffffffffu;
    std::vector<u32> image_resource_entries;
    std::vector<u32> startup_sprite_entries;
    std::vector<u32> active_sprite_entries;
    std::vector<u32> impact_sprite_entries;
    std::vector<u32> impact_image_indices;
    std::vector<u32> action_sequence_image_indices;
    bool projectile = false;
    bool directional_active_frames = false;
    bool spawn_impact_child = false;
    bool area_damage_allows_related_targets = false;
    bool startup_uses_source_muzzle = false;
    std::vector<u32> impact_frames;
    std::vector<u32> sound_frames;
    std::vector<std::pair<u32, u32>> frame_sound_slots;
};

struct UnitEffectRuntime {
    bool active = false;
    u32 effect_id = 0;
    u32 flags = 0;
    u32 tick = 0;
    u32 frame = 0;
    u32 amount = 0;
    u32 source_unit_id = 0;
    u32 target_unit_id = 0;
    u32 linked_unit_id = 0;
    i32 x = 0;
    i32 y = 0;
    i32 target_x = 0;
    i32 target_y = 0;
    i32 delta_x = 0;
    i32 delta_y = 0;
    i32 previous_x = 0;
    i32 previous_y = 0;
    i32 step_x = 0;
    i32 step_y = 0;
    u32 abs_delta_x = 0;
    u32 abs_delta_y = 0;
    u32 accumulator_x = 0;
    u32 accumulator_y = 0;
    u32 direction = 0;
    u32 range = 0;
    u32 closest_distance = 0xffffffffu;
    u32 chain_remaining = 0;
    std::array<u32, 3> chained_target_ids{};
    std::vector<u32> hit_unit_ids;
    bool initial_impact_applied = false;
};

struct UnitEffectEvent {
    UnitEffectEventKind kind = UnitEffectEventKind::started;
    UnitEffectSoundSpatialKind sound_spatial =
        UnitEffectSoundSpatialKind::world_point;
    bool sound_spatial_position_valid = true;
    u32 effect_id = 0;
    u32 unit_id = 0;
    u32 target_id = 0;
    u32 value = 0;
    i32 x = 0;
    i32 y = 0;
};

struct UnitEffectTrailSegment {
    u32 effect_id = 0;
    i32 x0 = 0;
    i32 y0 = 0;
    i32 x1 = 0;
    i32 y1 = 0;
    u32 width = 0;
    u16 color = 0;
};

struct UnitEffectRenderPalette {
    u16 highlight = 0xcf7f;
    u16 midtone = 0x76bf;
    u16 shadow = 0x057f;
};

struct UnitEffectImpactLinePalette {
    u16 highlight = 0xf988;
    u16 midtone = 0xc986;
    u16 shadow = 0x9186;
};

struct UnitEffectRuntimeState {
    std::vector<UnitEffectDefinition> definitions;
    std::vector<UnitMovementUnit> units;
    std::vector<UnitMovementUnit*> unit_refs;
    std::vector<UnitEffectEvent> events;
    std::vector<UnitEffectTrailSegment> trail_segments;
    std::array<u32, kProductionOrderOwnerCount> owner_primary_resources{};
    std::array<u32, kProductionOrderOwnerCount> owner_resource_score{};
    UnitEffectRuntimeCallbacks callbacks;
    const ProductionOrderRuntimeState* production_state = nullptr;
    const UnitEquipmentCatalog* equipment_catalog = nullptr;
    UnitLifecycleContext* lifecycle_context = nullptr;
    std::vector<u32> command_state_table;
    GameplayRenderCommandQueue* render_queue = nullptr;
    std::vector<UnitEffectRuntime> effect_slots;
    std::vector<std::size_t> active_effect_indices;
    std::vector<std::size_t> free_effect_indices;
    std::size_t effect_slot_capacity = kUnitEffectDefaultSlotCapacity;
    GameplayVisibilityGrid* visibility_grid = nullptr;
    PlayerSlotRuntimeState* players = nullptr;
    i32 viewport_left = 0;
    i32 viewport_top = 0;
    i32 viewport_right = 0;
    i32 viewport_bottom = 0;
    u32 local_player_slot = 0;
    u32 frame_counter = 0;
    u32 linked_health_restore_secondary_cost = 0;
    u32 linked_health_restore_amount = 0;
    bool linked_health_restore_globals_loaded = false;
    bool require_revealed_visibility = false;
    UnitEffectRenderPalette render_palette;
    UnitEffectImpactLinePalette impact_line_palette;
};

bool CheckIncomingActionTargetTransientFlag(const UnitMovementUnit& target);
bool CheckStoredActionTargetTransientFlag(const UnitMovementUnit& source);
bool CheckUnitActionImpactFrame(const UnitMovementUnit& source);
bool CheckUnitActionTargetClassGate(UnitActionContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
UnitActionTargetValidation ValidateUnitActionTarget(UnitActionContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target);
void BeginUnitActionAnimation(UnitActionContext& context, UnitMovementUnit& source);
void ApplyUnitActionImpact(UnitActionContext& context, UnitMovementUnit& source,
    UnitMovementUnit& target);
UnitActionTickResult ProcessUnitActionCycle(UnitActionContext& context,
    UnitMovementUnit& source);
void ApplyUnitActionEffectTargetLockoutIfFlagged(UnitMovementUnit& target,
    u32 lockout_ticks);

void TickUnitEffectRuntime(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void TickUnitEffectStartupDelay(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void DispatchUnitEffectActiveState(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void DispatchUnitEffectImpactState(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void TickUnitEffectChainImpact(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void TickUnitEffectSourceMuzzleLineImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void TickUnitEffectInitialDamageImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void TickUnitEffectTargetMarkerImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void TickUnitEffectPathActive(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
bool BeginUnitEffectStartup(UnitEffectRuntimeState& state, UnitEffectRuntime& effect,
    u32 effect_id, UnitMovementUnit& source, UnitMovementUnit* target = nullptr);
void DispatchUnitEffectStartByAction(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 effect_id, UnitMovementUnit& source,
    UnitMovementUnit* target = nullptr);
bool BeginUnitEffectImmediate(UnitEffectRuntimeState& state, UnitEffectRuntime& effect,
    u32 effect_id, UnitMovementUnit& source, UnitMovementUnit* target = nullptr);
void QueueUnitEffectStartSoundIfAny(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
UnitMovementUnit* FindUnitEffectImpactTarget(UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect);
void RenderUnitEffectRuntimeSprite(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void DispatchUnitActionEffectCommand(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 action_id);
void TickUnitEffectFrameAndApplyImpacts(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void TickUnitEffectLoopOrRefund(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void TickUnitEffectStartupTimer(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void PlayUnitEffectFrameSound(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void TickUnitEffectLinkedTargetFrames(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void TickUnitEffectAreaStunFrames(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void TickUnitEffectTargetLockFrames(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
void TickUnitEffectAreaDamageFrames(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
bool BeginSelectedUnitAttachmentEffect(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 effect_id, UnitMovementUnit& source,
    UnitMovementUnit* attachment);
bool StartSelectedUnitAttachmentEffect(UnitEffectRuntimeState& state,
    u32 effect_id, UnitMovementUnit& source, UnitMovementUnit* attachment,
    UnitEffectRuntime** created_effect = nullptr);
bool DispatchSelectedUnitActionEffect(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 action_id, UnitMovementUnit& source,
    UnitMovementUnit* target, i32 world_x, i32 world_y);
using UnitEffectRandomLimitFunction = u32 (*)(u32 limit, void* user_data);
bool DispatchSelectedUnitScatterActionEffect(UnitEffectRuntimeState& state,
    u32 action_id, UnitMovementUnit& source, i32 world_x, i32 world_y,
    UnitEffectRandomLimitFunction random_limit, void* random_user_data = nullptr);
void InitializeUnitEffectProjectilePath(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, UnitMovementUnit& source, UnitMovementUnit* target,
    i32 world_x, i32 world_y);
void QueueVisibleUnitEffectRenderCommands(UnitEffectRuntimeState& state);
void RenderUnitEffectOrProjectileRuntime(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void ConfigureUnitEffectRenderPalette(UnitEffectRuntimeState& state, bool use_rgb565);
void TickUnitEffectRuntimeList(UnitEffectRuntimeState& state);
void InitializeUnitEffectPathToTarget(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, UnitMovementUnit& source, UnitMovementUnit& target);
u32 CalculateUnitEffectSourceAndTargetCenters(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, const UnitMovementUnit& source,
    const UnitMovementUnit* target = nullptr, i32 world_x = 0, i32 world_y = 0);
u32 LookupUnitEffectDirection(const UnitEffectRuntime& effect);
void AdvanceUnitEffectProjectileTowardTarget(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void RetargetUnitEffectProjectilePath(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
void ApplyUnitEffectPointImpactAndSpawnChildren(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
bool CheckUnitEffectAlreadyHitTarget(const UnitEffectRuntime& effect, u32 target_unit_id);
UnitEffectRuntime* AllocateUnitEffectSlot(UnitEffectRuntimeState& state);
void ReleaseUnitEffectSlot(UnitEffectRuntimeState& state, UnitEffectRuntime& effect);
bool DispatchUnitEffectProjectileTrailRenderer(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 effect_id,
    i32 captured_screen_x, i32 captured_screen_y);
bool ResolveUnitEffectGenericSpriteRender(const UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect, u32& sprite_entry, u32& draw_mode);
void PrepareUnitEffectProjectileTrailRender(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 screen_x, i32 screen_y, u32 render_kind);
void DrawUnitEffectWideProjectileTrail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1);
void DrawUnitEffectWideImpactLineTrail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1);
void DrawUnitEffectNarrowProjectileTrail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1);
bool InitializeUnitEffectProjectileOrMeleePath(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, UnitMovementUnit& source, UnitMovementUnit* target);
u32 UnitEffectCommandDistanceGate(const UnitMovementUnit& source);
UnitEffectActionTargetGateResult EvaluateUnitEffectActionTargetGate(
    UnitEffectRuntimeState& state, UnitMovementUnit& source, u32 action_id);
u32 CheckUnitEffectActionTargetGate(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 action_id);
void ApplyUnitEffectAreaDamageByRenderClassMask(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 amount, u32 radius, u32 render_class_mask);
void ApplyUnitEffectAreaDamageByOwnerMask(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 amount, u32 radius, u32 blocked_owner_mask);
void ApplyUnitEffectAreaDamageByUnitFlagMask(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 amount, u32 radius, u32 unit_flag_mask);
bool AdvanceUnitEffectPathStepAndCheckTargetBounds(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect);
bool DispatchSelectedUnitAutoEffectCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, i32 target_x, i32 target_y);
bool IssueSelectedUnitPrimaryTargetEffectCommand(UnitMovementUnit& source);
bool IssueSelectedUnitTargetFlagCommandIfAllowed(UnitMovementUnit& source,
    u32 required_target_flags);
bool IssueSelectedUnitRepairNearestAllyCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range);
u32 PassThroughUnitEffectGateValueA(u32 value);
u32 PassThroughUnitEffectGateValueB(u32 value);
bool IssueSelectedUnitSecondaryTargetEffectCommand(UnitMovementUnit& source);
u32 PassThroughUnitEffectGateValueC(u32 value);
u32 PassThroughUnitEffectGateValueD(u32 value);
bool IssueSelectedUnitHarvestNearestResourceCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range);
bool IssueSelectedUnitSourceOnlyTargetCommand(UnitMovementUnit& source);
bool IssueSelectedUnitNearestHostileCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range);
bool IssueSelectedUnitNearestFlaggedHostileCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range, u32 required_target_flags);
u32 PassThroughUnitEffectGateValueE(u32 value);
bool IssueSelectedUnitFullHealthTargetCommand(UnitMovementUnit& source,
    UnitMovementUnit* target, i32 target_x, i32 target_y);
u32 PassThroughUnitEffectGateValueF(u32 value);

}
